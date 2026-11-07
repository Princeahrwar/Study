// Tutorial: design a hash map. Every class the tutorial shows is here, followed by differential tests against the
// standard containers, invariant checks, and the experiments behind every number quoted in the text.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 03_design_a_hash_map_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <list>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

// ---------------------------------------------------------------- from tutorial 01 (hash function and the generator)
uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// A small deterministic random number generator (splitmix64), so every run and every compiler sees the same numbers.
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t next() { return mix64(state += 0x9E3779B97F4A7C15ULL); }
    uint64_t below(uint64_t n) { return next() % n; }
};

// ---------------------------------------------------------------- section 2: Robin Hood hashing
// Open addressing in which every entry remembers how far it sits from its home slot. On insert, an entry that is
// closer to its home than the newcomer gives up its slot: take from the rich, give to the poor.
class RobinHoodMap {
    std::vector<uint64_t> keys_;
    std::vector<int> values_;
    std::vector<int> probe_;                                  // 0 = empty slot, otherwise 1 + distance from the home slot
    size_t mask_, size_ = 0;
    double maxLoad_;
    mutable long long examined_ = 0;                          // slots looked at by find, for the experiments

    size_t home(uint64_t key) const { return mix64(key) & mask_; }

    size_t locate(uint64_t key) const {                       // the slot of the key, or keys_.size() if it is absent
        size_t i = home(key);
        for (int d = 1;; ++d, i = (i + 1) & mask_) {
            ++examined_;
            if (probe_[i] < d) return keys_.size();           // empty, or a resident closer to home than we would be: absent
            if (keys_[i] == key) return i;
        }
    }

    void insertNew(uint64_t key, int value) {                 // the key is not in the table and there is room
        size_t i = home(key);
        int d = 1;                                            // 1 + distance of the entry in hand from its own home
        for (;; i = (i + 1) & mask_, ++d) {
            if (probe_[i] == 0) {
                keys_[i] = key;
                values_[i] = value;
                probe_[i] = d;
                return;
            }
            if (probe_[i] < d) {                              // the resident is closer to home: it moves on instead
                std::swap(key, keys_[i]);
                std::swap(value, values_[i]);
                std::swap(d, probe_[i]);
            }
        }
    }

    void grow() {
        RobinHoodMap bigger(keys_.size() * 2, maxLoad_);
        for (size_t i = 0; i < keys_.size(); ++i)
            if (probe_[i]) bigger.insertNew(keys_[i], values_[i]);
        bigger.size_ = size_;
        *this = std::move(bigger);
    }

public:
    explicit RobinHoodMap(size_t capacity = 16, double maxLoad = 0.9) : maxLoad_(maxLoad) {
        size_t c = 8;
        while (c < capacity) c *= 2;                          // a power of two, so that "mod" is a mask
        keys_.assign(c, 0);
        values_.assign(c, 0);
        probe_.assign(c, 0);
        mask_ = c - 1;
    }
    size_t size() const { return size_; }
    size_t capacity() const { return keys_.size(); }

    int* find(uint64_t key) {
        size_t i = locate(key);
        return i == keys_.size() ? nullptr : &values_[i];
    }

    bool put(uint64_t key, int value) {                       // true if the key is new
        if (int* v = find(key)) {
            *v = value;
            return false;
        }
        if (size_ + 1 > maxLoad_ * keys_.size()) grow();
        insertNew(key, value);
        ++size_;
        return true;
    }

    bool erase(uint64_t key) {
        size_t i = locate(key);
        if (i == keys_.size()) return false;
        // Backward shift: pull each following entry one slot back, until one is already at home or the run ends.
        for (size_t next = (i + 1) & mask_; probe_[next] > 1; next = (next + 1) & mask_) {
            keys_[i] = keys_[next];
            values_[i] = values_[next];
            probe_[i] = probe_[next] - 1;
            i = next;
        }
        probe_[i] = 0;
        --size_;
        return true;
    }

    // ---- for the tests and the experiments
    long long examined() const { return examined_; }
    int probeAt(size_t slot) const { return probe_[slot]; }
    uint64_t keyAt(size_t slot) const { return keys_[slot]; }
    size_t slotOf(uint64_t key) const {                       // capacity() if the key is absent
        for (size_t i = 0; i < keys_.size(); ++i)
            if (probe_[i] && keys_[i] == key) return i;
        return keys_.size();
    }
    bool checkInvariants() const {
        size_t count = 0;
        for (size_t i = 0; i < keys_.size(); ++i) {
            size_t next = (i + 1) & mask_;
            if (probe_[next] > probe_[i] + 1) return false;   // a distance grows by at most one per slot; after a gap it is 1
            if (!probe_[i]) continue;
            ++count;
            if (home(keys_[i]) != ((i + keys_.size() - (probe_[i] - 1)) & mask_)) return false;
        }
        return count == size_;
    }
};

// ---------------------------------------------------------------- section 3: cuckoo hashing
// Every key has two candidate buckets of B slots. A lookup reads at most those two buckets; an insert into full
// buckets kicks an entry out to its other bucket, and gives up after a fixed number of kicks.
template <int B>
class CuckooSet {
    static constexpr int kMaxKicks = 500;
    std::vector<uint64_t> keys_;
    std::vector<uint8_t> used_;
    size_t mask_ = 0, size_ = 0;                              // mask_ = number of buckets - 1
    uint64_t seed1_ = 0, seed2_ = 0;
    Rng rng_;
    int rebuilds_ = 0;
    long long firstFailureSize_ = -1;                         // number of keys stored when the first insert failed

    size_t bucket1(uint64_t k) const { return mix64(k ^ seed1_) & mask_; }
    size_t bucket2(uint64_t k) const { return mix64(k ^ seed2_) & mask_; }

    bool tryPlace(size_t bucket, uint64_t k) {
        for (int s = 0; s < B; ++s) {
            size_t i = bucket * B + s;
            if (!used_[i]) {
                keys_[i] = k;
                used_[i] = 1;
                return true;
            }
        }
        return false;
    }

    bool insertNoRebuild(uint64_t& k) {                       // on failure k is the key left without a home
        if (tryPlace(bucket1(k), k) || tryPlace(bucket2(k), k)) return true;
        size_t bucket = bucket1(k);
        for (int kick = 0; kick < kMaxKicks; ++kick) {
            size_t victim = bucket * B + rng_.below(B);
            std::swap(k, keys_[victim]);                      // the newcomer takes the slot; the old resident is in hand
            size_t a = bucket1(k), b = bucket2(k);
            bucket = (bucket == a) ? b : a;                   // move the resident to its other bucket
            if (tryPlace(bucket, k)) return true;
        }
        return false;
    }

    void resetTo(size_t buckets) {                            // empty arrays, new hash functions
        keys_.assign(buckets * B, 0);
        used_.assign(buckets * B, 0);
        mask_ = buckets - 1;
        seed1_ = rng_.next();
        seed2_ = rng_.next();
    }

    void rebuild(uint64_t homeless) {                         // double the table and insert everything again
        std::vector<uint64_t> all{homeless};
        for (size_t i = 0; i < keys_.size(); ++i)
            if (used_[i]) all.push_back(keys_[i]);
        ++rebuilds_;
        for (size_t buckets = (mask_ + 1) * 2;; buckets *= 2) {
            resetTo(buckets);
            bool ok = true;
            for (size_t j = 0; ok && j < all.size(); ++j) {
                uint64_t k = all[j];
                ok = insertNoRebuild(k);
            }
            if (ok) return;                                   // otherwise try again with an even larger table
        }
    }

public:
    explicit CuckooSet(size_t buckets, uint64_t seed = 1) : rng_(seed) {
        size_t n = 2;
        while (n < buckets) n *= 2;
        resetTo(n);
    }
    size_t size() const { return size_; }
    size_t capacity() const { return keys_.size(); }

    bool contains(uint64_t k) const {
        for (size_t bucket : {bucket1(k), bucket2(k)})
            for (int s = 0; s < B; ++s)
                if (used_[bucket * B + s] && keys_[bucket * B + s] == k) return true;
        return false;
    }

    bool put(uint64_t key) {
        if (contains(key)) return false;
        uint64_t k = key;
        if (!insertNoRebuild(k)) {
            if (firstFailureSize_ < 0) firstFailureSize_ = (long long)size_;
            rebuild(k);
        }
        ++size_;
        return true;
    }

    bool erase(uint64_t k) {                                  // no tombstones: the slot is simply free again
        for (size_t bucket : {bucket1(k), bucket2(k)})
            for (int s = 0; s < B; ++s)
                if (used_[bucket * B + s] && keys_[bucket * B + s] == k) {
                    used_[bucket * B + s] = 0;
                    --size_;
                    return true;
                }
        return false;
    }

    // ---- for the tests and the experiments
    int rebuilds() const { return rebuilds_; }
    long long firstFailureSize() const { return firstFailureSize_; }
    bool checkInvariants() const {                            // every key sits in one of its two buckets
        size_t count = 0;
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (!used_[i]) continue;
            ++count;
            size_t bucket = i / B;
            if (bucket != bucket1(keys_[i]) && bucket != bucket2(keys_[i])) return false;
        }
        return count == size_;
    }
};

// ---------------------------------------------------------------- section 4: the Swiss-table idea
// One control byte per slot; the eight control bytes of a group sit in one 64-bit word and are examined together.
// A full slot holds the low 7 bits of its key's hash (the tag); the two other states have the high bit set.
const uint8_t kEmpty = 0x80, kDeleted = 0xFE;
const uint64_t kLow = 0x0101010101010101ULL, kHigh = 0x8080808080808080ULL;

// Bit 7 of each byte that equals tag. The subtraction can also flag a byte right above a real match (a false
// positive); the caller compares the keys anyway.
uint64_t matchTag(uint64_t group, uint8_t tag) {
    uint64_t x = group ^ (kLow * tag);                        // bytes equal to the tag become 0
    return (x - kLow) & ~x & kHigh;                           // flag the bytes that became 0
}
uint64_t matchEmpty(uint64_t group) { return group & ~(group << 6) & kHigh; }   // exactly 0x80: bit 7 set, bit 1 clear
uint64_t matchFree(uint64_t group) { return group & kHigh; }                    // empty or deleted

class SwissSet {
    static constexpr size_t npos = ~size_t(0);
    std::vector<uint64_t> ctrl_;                              // one word per group of 8 slots
    std::vector<uint64_t> keys_;
    size_t groupMask_, size_ = 0, growthLeft_;                // growthLeft_: empty slots we may still fill (7/8 of all)
    mutable long long compared_ = 0;                          // key comparisons, for the experiments

    uint8_t ctrlAt(size_t slot) const { return (ctrl_[slot >> 3] >> ((slot & 7) * 8)) & 0xFF; }
    void setCtrl(size_t slot, uint8_t v) {
        int shift = (slot & 7) * 8;
        ctrl_[slot >> 3] = (ctrl_[slot >> 3] & ~(0xFFULL << shift)) | (uint64_t(v) << shift);
    }

    size_t findSlot(uint64_t key) const {
        uint64_t h = mix64(key);
        uint8_t tag = h & 0x7F;                               // H2: 7 bits that go into the control byte
        size_t g = (h >> 7) & groupMask_;                     // H1: the rest picks the first group
        for (size_t step = 1;; ++step) {
            uint64_t word = ctrl_[g];
            for (uint64_t m = matchTag(word, tag); m; m &= m - 1) {
                size_t slot = g * 8 + (std::countr_zero(m) >> 3);
                ++compared_;
                if (keys_[slot] == key) return slot;
            }
            if (matchEmpty(word)) return npos;                // a group with an empty slot ends every search
            g = (g + step) & groupMask_;                      // otherwise go on to another group
        }
    }

    void insertNew(uint64_t key) {                            // the key is absent and growthLeft_ > 0
        uint64_t h = mix64(key);
        size_t g = (h >> 7) & groupMask_;
        for (size_t step = 1;; ++step) {
            if (uint64_t freeSlots = matchFree(ctrl_[g])) {
                size_t slot = g * 8 + (std::countr_zero(freeSlots) >> 3);
                if (ctrlAt(slot) == kEmpty) --growthLeft_;    // reusing a deleted slot costs nothing
                setCtrl(slot, h & 0x7F);
                keys_[slot] = key;
                return;
            }
            g = (g + step) & groupMask_;
        }
    }

    void rehash() {                                           // double if mostly live entries, else just drop tombstones
        size_t groups = groupMask_ + 1;
        if (size_ > groups * 8 * 7 / 16) groups *= 2;
        SwissSet fresh(groups);
        for (size_t slot = 0; slot <= groupMask_ * 8 + 7; ++slot)
            if (!(ctrlAt(slot) & 0x80)) fresh.insertNew(keys_[slot]);
        fresh.size_ = size_;
        *this = std::move(fresh);
    }

public:
    explicit SwissSet(size_t groups = 1) {
        size_t n = 1;
        while (n < groups) n *= 2;
        ctrl_.assign(n, kHigh);                               // every byte 0x80 = empty
        keys_.assign(n * 8, 0);
        groupMask_ = n - 1;
        growthLeft_ = n * 7;
    }
    size_t size() const { return size_; }
    size_t capacity() const { return keys_.size(); }
    bool contains(uint64_t key) const { return findSlot(key) != npos; }

    bool put(uint64_t key) {
        if (findSlot(key) != npos) return false;
        if (growthLeft_ == 0) rehash();
        insertNew(key);
        ++size_;
        return true;
    }

    bool erase(uint64_t key) {
        size_t slot = findSlot(key);
        if (slot == npos) return false;
        // A group that still has an empty slot has never been full, so no search went past it: the slot can be
        // empty again. Otherwise some key may have been pushed beyond this group, and we leave a tombstone.
        if (matchEmpty(ctrl_[slot >> 3])) {
            setCtrl(slot, kEmpty);
            ++growthLeft_;
        } else {
            setCtrl(slot, kDeleted);
        }
        --size_;
        return true;
    }

    // ---- for the tests and the experiments
    long long compared() const { return compared_; }
    size_t slotOf(uint64_t key) const { return findSlot(key); }
    size_t groupOf(uint64_t key) const { return (mix64(key) >> 7) & groupMask_; }
    uint8_t ctrlByte(size_t slot) const { return ctrlAt(slot); }
    size_t growthLeft() const { return growthLeft_; }
    bool checkInvariants() const {
        size_t full = 0, deleted = 0, empty = 0;
        for (size_t slot = 0; slot < keys_.size(); ++slot) {
            uint8_t c = ctrlAt(slot);
            if (c == kEmpty) ++empty;
            else if (c == kDeleted) ++deleted;
            else if (c < 0x80) {
                ++full;
                if (findSlot(keys_[slot]) != slot || c != (mix64(keys_[slot]) & 0x7F)) return false;
            } else return false;
        }
        return full == size_ && growthLeft_ == keys_.size() / 8 * 7 - full - deleted && empty > 0;
    }
};

// ---------------------------------------------------------------- section 6: an LRU cache
// Get and put in O(1): a hash map finds the node, a doubly linked list keeps the order of use.
class LruCache {
    using Item = std::pair<int, int>;                         // (key, value)
    size_t capacity_;
    std::list<Item> items_;                                   // most recently used first
    std::unordered_map<int, std::list<Item>::iterator> where_;

public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    int get(int key) {                                        // -1 if absent
        auto it = where_.find(key);
        if (it == where_.end()) return -1;
        items_.splice(items_.begin(), items_, it->second);    // relink the node at the front: no copy, iterators stay valid
        return it->second->second;
    }

    void put(int key, int value) {
        auto it = where_.find(key);
        if (it != where_.end()) {
            it->second->second = value;
            items_.splice(items_.begin(), items_, it->second);
            return;
        }
        if (items_.size() == capacity_) {                     // full: drop the least recently used
            where_.erase(items_.back().first);
            items_.pop_back();
        }
        items_.emplace_front(key, value);
        where_[key] = items_.begin();
    }
};

// ================================================================ tests and experiments
struct Summary { double mean, sd; int max; };
static Summary summarize(const std::vector<int>& v) {
    double sum = 0, sq = 0;
    int mx = 0;
    for (int x : v) { sum += x; sq += (double)x * x; mx = std::max(mx, x); }
    double mean = sum / v.size();
    return {mean, std::sqrt(std::max(0.0, sq / v.size() - mean * mean)), mx};
}

// Plain linear probing with the same hash and the same home slots, for comparison with Robin Hood.
static std::vector<int> linearProbingProbes(const std::vector<uint64_t>& keys, size_t capacity, std::vector<char>& occupied) {
    occupied.assign(capacity, 0);
    std::vector<int> probes;
    for (uint64_t k : keys) {
        size_t i = mix64(k) & (capacity - 1);
        int d = 1;
        while (occupied[i]) { i = (i + 1) & (capacity - 1); ++d; }
        occupied[i] = 1;
        probes.push_back(d);
    }
    return probes;
}

static uint64_t packGroup(const uint8_t b[8]) {
    uint64_t w = 0;
    for (int i = 0; i < 8; ++i) w |= uint64_t(b[i]) << (8 * i);
    return w;
}
static std::vector<int> slotsOf(uint64_t mask) {                  // bit 7 of byte i set -> slot i
    std::vector<int> s;
    for (int i = 0; i < 8; ++i)
        if (mask >> (8 * i + 7) & 1) s.push_back(i);
    return s;
}

// Test-only: the first 'count' keys (1, 2, 3, ...) whose home slot in a table with this mask is 'home'.
static std::vector<uint64_t> keysWithHome(size_t mask, size_t home, int count) {
    std::vector<uint64_t> keys;
    for (uint64_t k = 1; (int)keys.size() < count; ++k)
        if ((mix64(k) & mask) == home) keys.push_back(k);
    return keys;
}

template <int B>
static void testCuckoo(uint64_t seed, size_t startBuckets, int ops, int keyRange) {
    CuckooSet<B> table(startBuckets, seed);
    std::unordered_set<uint64_t> ref;
    Rng rng(seed * 7919 + 1);
    for (int op = 0; op < ops; ++op) {
        uint64_t k = rng.below(keyRange);
        switch (rng.below(3)) {
            case 0: CHECK(table.put(k) == ref.insert(k).second); break;
            case 1: CHECK(table.erase(k) == (ref.erase(k) == 1)); break;
            default: CHECK(table.contains(k) == (ref.count(k) == 1)); break;
        }
        CHECK(table.size() == ref.size());
        if (op % 16 == 0) CHECK(table.checkInvariants());
    }
    bool all = true;
    for (uint64_t k = 0; k < (uint64_t)keyRange; ++k) all = all && table.contains(k) == (ref.count(k) == 1);
    CHECK(all);
}

int main() {
    // ---- group matching (section 4)
    {
        uint8_t example[8] = {0x11, 0x2A, 0x2B, 0x80, 0x2A, 0xFE, 0x05, 0x40};
        uint64_t word = packGroup(example);
        std::vector<int> exact, swar = slotsOf(matchTag(word, 0x2A));
        for (int i = 0; i < 8; ++i)
            if (example[i] == 0x2A) exact.push_back(i);
        CHECK((exact == std::vector<int>{1, 4}) && (swar == std::vector<int>{1, 2, 4}));
        CHECK((slotsOf(matchEmpty(word)) == std::vector<int>{3}) && (slotsOf(matchFree(word)) == std::vector<int>{3, 5}));
        std::printf("tag match example: exact slots 1 4, matchTag slots 1 2 4\n");
        Rng rng(11);
        long long extras = 0;
        for (int iter = 0; iter < 20000; ++iter) {
            uint8_t tag = (uint8_t)rng.below(128);
            uint8_t b[8];
            for (int i = 0; i < 8; ++i) {
                switch (rng.below(5)) {
                    case 0: b[i] = tag; break;
                    case 1: b[i] = tag ^ 1; break;
                    case 2: b[i] = kEmpty; break;
                    case 3: b[i] = kDeleted; break;
                    default: b[i] = (uint8_t)rng.below(128); break;
                }
            }
            uint64_t w = packGroup(b), m = matchTag(w, tag);
            uint64_t exactMask = 0, emptyMask = 0, freeMask = 0;
            for (int i = 0; i < 8; ++i) {
                if (b[i] == tag) exactMask |= 0x80ULL << (8 * i);
                if (b[i] == kEmpty) emptyMask |= 0x80ULL << (8 * i);
                if (b[i] == kEmpty || b[i] == kDeleted) freeMask |= 0x80ULL << (8 * i);
            }
            uint64_t extra = m & ~exactMask;
            extras += std::popcount(extra);
            CHECK((m & exactMask) == exactMask);                          // a real match is never missed
            CHECK(m == 0 || ((m & (~m + 1)) & exactMask) != 0);           // the lowest flagged byte is a real match
            CHECK((extra & ~(m << 8)) == 0);                              // a false positive sits right above a flagged byte
            bool extrasAreTagXor1 = true;
            for (int i = 0; i < 8; ++i)
                if ((extra >> (8 * i + 7) & 1) && b[i] != (tag ^ 1)) extrasAreTagXor1 = false;
            CHECK(extrasAreTagXor1);                                      // and its value is tag ^ 1
            CHECK(matchEmpty(w) == emptyMask && matchFree(w) == freeMask);
        }
        CHECK(extras > 0);
    }

    // ---- Robin Hood: differential test against std::unordered_map
    {
        for (int run = 0; run < 36; ++run) {
            double maxLoad = run % 3 == 0 ? 0.95 : 0.9;
            RobinHoodMap map(8, maxLoad);
            std::unordered_map<uint64_t, int> ref;
            Rng rng(100 + run);
            int keyRange = run % 3 == 0 ? 12 : (run % 3 == 1 ? 60 : 3000);
            for (int op = 0; op < 4000; ++op) {
                uint64_t k = rng.below(keyRange);
                int v = (int)rng.below(1000);
                switch (rng.below(3)) {
                    case 0: CHECK(map.put(k, v) == ref.insert_or_assign(k, v).second); break;
                    case 1: CHECK(map.erase(k) == (ref.erase(k) == 1)); break;
                    default: {
                        int* p = map.find(k);
                        auto it = ref.find(k);
                        CHECK((p != nullptr) == (it != ref.end()) && (p == nullptr || *p == it->second));
                    }
                }
                CHECK(map.size() == ref.size() && map.size() <= maxLoad * map.capacity());
                if (op % 8 == 0) CHECK(map.checkInvariants());
            }
            bool all = true;
            for (uint64_t k = 0; k < (uint64_t)keyRange; ++k) {
                int* p = map.find(k);
                auto it = ref.find(k);
                all = all && ((p != nullptr) == (it != ref.end())) && (p == nullptr || *p == it->second);
            }
            CHECK(all);
        }
        RobinHoodMap big;
        for (uint64_t k = 0; k < 100000; ++k) big.put(k * 1024, (int)k);       // keys that share low bits
        CHECK(big.size() == 100000 && big.checkInvariants() && (big.capacity() & (big.capacity() - 1)) == 0);
        CHECK(big.capacity() == 131072 && big.size() <= 0.9 * big.capacity() && *big.find(77 * 1024) == 77 && big.find(5) == nullptr);
        // The trace of section 2, on the real class: capacity 8, homes 3, 4, 3, 3.
        std::vector<uint64_t> h3 = keysWithHome(7, 3, 3), h4 = keysWithHome(7, 4, 1);
        uint64_t A = h3[0], B = h4[0], C = h3[1], D = h3[2];
        RobinHoodMap t(8, 1.0);
        t.put(A, 1);
        t.put(B, 2);
        CHECK(t.slotOf(A) == 3 && t.slotOf(B) == 4 && t.probeAt(3) == 1 && t.probeAt(4) == 1);
        t.put(C, 3);                                                       // C meets B (at home) and takes its slot
        CHECK(t.slotOf(A) == 3 && t.slotOf(C) == 4 && t.slotOf(B) == 5);
        CHECK(t.probeAt(4) == 2 && t.probeAt(5) == 2);
        t.put(D, 4);                                                       // D takes the slot of B, B moves on again
        CHECK(t.slotOf(D) == 5 && t.slotOf(B) == 6 && t.probeAt(5) == 3 && t.probeAt(6) == 3);
        CHECK(t.checkInvariants() && t.size() == 4);
        CHECK(t.erase(C));                                                 // backward shift pulls D and B back one slot each
        CHECK(t.slotOf(A) == 3 && t.slotOf(D) == 4 && t.slotOf(B) == 5 && t.probeAt(6) == 0);
        CHECK(t.probeAt(4) == 2 && t.probeAt(5) == 2 && t.checkInvariants());
        CHECK(*t.find(D) == 4 && *t.find(B) == 2 && t.find(C) == nullptr);
        std::printf("trace: B moved from slot 4 to 5 to 6 by two inserts, back to 5 by one erase\n");
    }

    // ---- Robin Hood against linear probing: same slots occupied, same total probes, fewer extremes
    {
        const size_t capacity = 65536;
        const double loads[] = {0.5, 0.7, 0.8, 0.9, 0.95};
        for (double load : loads) {
            size_t n = (size_t)(load * capacity);
            Rng rng(777), absent(4242);
            std::vector<uint64_t> keys;
            std::unordered_set<uint64_t> inserted;
            for (size_t i = 0; i < n; ++i) {
                uint64_t k = rng.next();
                keys.push_back(k);
                inserted.insert(k);
            }
            CHECK(inserted.size() == n);
            std::vector<char> occupied;
            std::vector<int> lp = linearProbingProbes(keys, capacity, occupied);
            RobinHoodMap rh(capacity, 1.0);
            for (uint64_t k : keys) rh.put(k, 1);
            CHECK(rh.capacity() == capacity && rh.size() == n && rh.checkInvariants());
            std::vector<int> rhProbes;
            bool sameSlots = true;
            for (size_t i = 0; i < capacity; ++i) {
                sameSlots = sameSlots && ((rh.probeAt(i) != 0) == (occupied[i] != 0));
                if (rh.probeAt(i)) rhProbes.push_back(rh.probeAt(i));
            }
            CHECK(sameSlots);
            Summary a = summarize(lp), b = summarize(rhProbes);
            long long sumA = 0, sumB = 0;
            for (int x : lp) sumA += x;
            for (int x : rhProbes) sumB += x;
            CHECK(sumA == sumB);                                              // the same total probe length
            CHECK(b.sd <= a.sd && b.max <= a.max);
            // unsuccessful searches
            long long lpMiss = 0, rhMiss = 0, misses = 0;
            for (int i = 0; i < 20000; ++i) {
                uint64_t k = absent.next();
                if (inserted.count(k)) continue;
                size_t j = mix64(k) & (capacity - 1);
                int c = 1;
                while (occupied[j]) { j = (j + 1) & (capacity - 1); ++c; }
                lpMiss += c;
                long long before = rh.examined();
                CHECK(rh.find(k) == nullptr);
                rhMiss += rh.examined() - before;
                ++misses;
            }
            CHECK(rhMiss >= misses && rhMiss < lpMiss && misses > 19000);
            std::printf("probe stats load %.2f: linear mean %.4f sd %.4f max %d | robin mean %.4f sd %.4f max %d | miss linear %.4f robin %.4f\n",
                        load, a.mean, a.sd, a.max, b.mean, b.sd, b.max, (double)lpMiss / misses, (double)rhMiss / misses);
        }
    }

    // ---- cuckoo hashing
    {
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            testCuckoo<1>(seed, 2, 3000, 40);
            testCuckoo<2>(seed, 2, 3000, 60);
            testCuckoo<4>(seed, 2, 3000, 120);
        }
        testCuckoo<4>(99, 4, 30000, 3000);
        testCuckoo<8>(98, 2, 8000, 500);
        CuckooSet<1> t(2, 5);
        for (uint64_t k = 0; k < 2000; ++k) CHECK(t.put(k));
        CHECK(t.rebuilds() > 0 && t.size() == 2000 && t.checkInvariants());
        bool all = true;
        for (uint64_t k = 0; k < 2000; ++k) all = all && t.contains(k);
        CHECK(all && !t.contains(5000));
        // the load at which the first insert fails
        const int widths[] = {1, 2, 4, 8};
        for (int w : widths) {
            double lo = 2, hi = 0, sum = 0;
            for (uint64_t seed = 1; seed <= 8; ++seed) {
                double load = 0;
                size_t slots = 65536;
                auto run = [&](auto& table) {
                    Rng rng(seed * 1000003);
                    while (table.rebuilds() == 0) table.put(rng.next());
                    load = (double)table.firstFailureSize() / slots;
                };
                if (w == 1) { CuckooSet<1> table(slots / 1, seed); run(table); }
                if (w == 2) { CuckooSet<2> table(slots / 2, seed); run(table); }
                if (w == 4) { CuckooSet<4> table(slots / 4, seed); run(table); }
                if (w == 8) { CuckooSet<8> table(slots / 8, seed); run(table); }
                lo = std::min(lo, load); hi = std::max(hi, load); sum += load;
            }
            std::printf("cuckoo B=%d: first failure at load min %.3f avg %.3f max %.3f over 8 seeds\n", w, lo, sum / 8, hi);
            const double lows[] = {0.35, 0.75, 0.90, 0.93}, highs[] = {0.60, 0.95, 0.995, 0.999};
            int idx = w == 1 ? 0 : w == 2 ? 1 : w == 4 ? 2 : 3;
            CHECK(lo > lows[idx] && hi < highs[idx]);
        }
    }

    // ---- Swiss-style set
    {
        Rng rng(31);
        for (int run = 0; run < 24; ++run) {
            SwissSet set(1);
            std::unordered_set<uint64_t> ref;
            int keyRange = run % 3 == 0 ? 20 : (run % 3 == 1 ? 90 : 2500);
            for (int op = 0; op < 5000; ++op) {
                uint64_t k = rng.below(keyRange);
                switch (rng.below(3)) {
                    case 0: CHECK(set.put(k) == ref.insert(k).second); break;
                    case 1: CHECK(set.erase(k) == (ref.erase(k) == 1)); break;
                    default: CHECK(set.contains(k) == (ref.count(k) == 1)); break;
                }
                CHECK(set.size() == ref.size());
                if (op % 16 == 0) CHECK(set.checkInvariants());
            }
            bool all = true;
            for (uint64_t k = 0; k < (uint64_t)keyRange; ++k) all = all && set.contains(k) == (ref.count(k) == 1);
            CHECK(all);
        }
        // empty or deleted? Two groups, group 0 filled completely.
        SwissSet s(2);
        std::vector<uint64_t> inGroup0;
        uint64_t extraKey = 0;
        for (uint64_t k = 1; inGroup0.size() < 9; ++k)
            if (s.groupOf(k) == 0) inGroup0.push_back(k);
        extraKey = inGroup0.back();
        inGroup0.pop_back();
        for (uint64_t k : inGroup0) s.put(k);                              // group 0 is full
        s.put(extraKey);                                                   // the ninth key has to go on to group 1
        CHECK(s.slotOf(extraKey) / 8 == 1 && s.checkInvariants());
        size_t slot = s.slotOf(inGroup0[3]);
        size_t before = s.growthLeft();
        CHECK(s.erase(inGroup0[3]));
        CHECK(s.ctrlByte(slot) == kDeleted && s.growthLeft() == before);   // full group: a tombstone
        CHECK(s.contains(extraKey) && s.checkInvariants());                // still reachable past the tombstone
        CHECK(s.put(inGroup0[3]) && s.slotOf(inGroup0[3]) == slot && s.growthLeft() == before);   // the tombstone is reused
        size_t slot1 = s.slotOf(extraKey);
        CHECK(s.erase(extraKey) && s.ctrlByte(slot1) == kEmpty && s.growthLeft() == before + 1);  // group 1 has empties: empty again
        CHECK(s.checkInvariants());
        // Entries do not move on insert or erase, only when the table is rebuilt.
        SwissSet still(8);
        std::vector<uint64_t> first, firstSlots;
        for (uint64_t k = 1; first.size() < 20; ++k) { first.push_back(k); still.put(k); firstSlots.push_back(still.slotOf(k)); }
        for (uint64_t k = 100; k < 130; ++k) still.put(k);
        for (uint64_t k = 100; k < 130; k += 2) still.erase(k);
        bool unmoved = still.capacity() == 64;
        for (size_t i = 0; i < first.size(); ++i) unmoved = unmoved && still.slotOf(first[i]) == firstSlots[i];
        CHECK(unmoved);
        // Probing order of groups: 0, 1, 3, 2 (steps 1, 2, 3) in a table of four groups.
        SwissSet order(4);
        std::vector<uint64_t> start0;
        for (uint64_t k = 1; start0.size() < 33; ++k)
            if (order.groupOf(k) == 0) start0.push_back(k);
        for (int i = 0; i < 24; ++i) order.put(start0[i]);
        CHECK(order.slotOf(start0[0]) / 8 == 0 && order.slotOf(start0[8]) / 8 == 1 && order.slotOf(start0[16]) / 8 == 3);
        std::printf("group order: keys 1 to 8 -> group 0, 9 to 16 -> group 1, 17 to 24 -> group 3\n");
        // Tombstones are cleaned out by a rehash without growing, if few entries are live.
        SwissSet r(2);                                                     // 16 slots, room for 14 non-empty ones
        std::vector<uint64_t> g0, g1;
        for (uint64_t k = 1000; g0.size() < 9 || g1.size() < 6; ++k) {
            if (r.groupOf(k) == 0 && g0.size() < 9) g0.push_back(k);
            if (r.groupOf(k) == 1 && g1.size() < 6) g1.push_back(k);
        }
        for (int i = 0; i < 8; ++i) r.put(g0[i]);
        for (uint64_t k : g1) r.put(k);
        CHECK(r.size() == 14 && r.growthLeft() == 0 && r.capacity() == 16);
        for (int i = 0; i < 8; ++i) CHECK(r.erase(g0[i]));
        CHECK(r.size() == 6 && r.growthLeft() == 0 && r.checkInvariants());   // 8 tombstones, 6 live keys
        CHECK(r.put(g0[8]));                                               // no growth left: rehash
        CHECK(r.capacity() == 16 && r.size() == 7 && r.growthLeft() == 7 && r.checkInvariants());
        bool kept = r.contains(g0[8]);
        for (uint64_t k : g1) kept = kept && r.contains(k);
        CHECK(kept);
        // ... and with many live entries the same situation doubles the table.
        SwissSet d(2);
        for (int i = 0; i < 8; ++i) d.put(g0[i]);
        for (uint64_t k : g1) d.put(k);
        CHECK(d.put(g0[8]) && d.capacity() == 32 && d.size() == 15 && d.checkInvariants());
        // experiments: key comparisons at load 7/8
        SwissSet full(8192);                                               // 65536 slots
        Rng keys(555), absent(556);
        std::vector<uint64_t> stored;
        while (full.size() < 7 * 8192) {
            uint64_t k = keys.next();
            if (full.put(k)) stored.push_back(k);
        }
        CHECK(full.size() == 57344 && full.growthLeft() == 0 && full.capacity() == 65536);
        long long c0 = full.compared();
        int misses = 0;
        for (int i = 0; i < 100000; ++i) {
            CHECK(!full.contains(absent.next()));
            ++misses;
        }
        double perMiss = (double)(full.compared() - c0) / misses;
        c0 = full.compared();
        for (size_t i = 0; i < 50000; ++i) CHECK(full.contains(stored[i]));
        double perHit = (double)(full.compared() - c0) / 50000;
        std::printf("swiss load 7/8: key comparisons per miss %.4f, per hit %.4f\n", perMiss, perHit);
        CHECK(perMiss < 0.5 && perHit < 1.5 && perHit >= 1.0);
    }

    // ---- LRU cache
    {
        LruCache c(2);
        c.put(1, 1);
        c.put(2, 2);
        CHECK(c.get(1) == 1);
        c.put(3, 3);                                                       // evicts 2
        CHECK(c.get(2) == -1);
        c.put(4, 4);                                                       // evicts 1
        CHECK(c.get(1) == -1 && c.get(3) == 3 && c.get(4) == 4);
        Rng rng(8);
        for (int run = 0; run < 300; ++run) {
            size_t capacity = 1 + rng.below(6);
            LruCache cache(capacity);
            std::vector<std::pair<int, int>> ref;                          // most recently used first
            for (int op = 0; op < 200; ++op) {
                int k = (int)rng.below(10);
                if (rng.below(2) == 0) {
                    int expected = -1;
                    for (size_t i = 0; i < ref.size(); ++i)
                        if (ref[i].first == k) {
                            expected = ref[i].second;
                            ref.insert(ref.begin(), ref[i]);
                            ref.erase(ref.begin() + i + 1);
                            break;
                        }
                    CHECK(cache.get(k) == expected);
                } else {
                    int v = (int)rng.below(100);
                    for (size_t i = 0; i < ref.size(); ++i)
                        if (ref[i].first == k) {
                            ref.erase(ref.begin() + i);
                            break;
                        }
                    ref.insert(ref.begin(), {k, v});
                    if (ref.size() > capacity) ref.pop_back();
                    cache.put(k, v);
                    CHECK(true);
                }
            }
            bool all = true;
            for (int k = 0; k < 10; ++k) {
                int expected = -1;
                for (auto& e : ref) if (e.first == k) expected = e.second;
                // reading changes the order, so read the reference first and then the cache in the same order
                all = all && cache.get(k) == expected;
                for (size_t i = 0; i < ref.size(); ++i)
                    if (ref[i].first == k) { ref.insert(ref.begin(), ref[i]); ref.erase(ref.begin() + i + 1); break; }
            }
            CHECK(all);
        }
    }

    // ---- speed (printed only when asked for; the numbers depend on the machine)
    if (std::getenv("HASH03_TIMING")) {
        const size_t n = 700000;
        for (int rep = 0; rep < 3; ++rep) {
            std::vector<uint64_t> keys, other;
            Rng a(1), b(2);
            for (size_t i = 0; i < n; ++i) { keys.push_back(a.next()); other.push_back(b.next()); }
            auto seconds = [](auto t0) { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
            auto bench = [&](const char* name, auto& table, auto put, auto has) {
                auto t0 = std::chrono::steady_clock::now();
                for (uint64_t k : keys) put(table, k);
                double tInsert = seconds(t0);
                t0 = std::chrono::steady_clock::now();
                size_t found = 0;
                for (uint64_t k : keys) found += has(table, k);
                double tHit = seconds(t0);
                t0 = std::chrono::steady_clock::now();
                size_t missed = 0;
                for (uint64_t k : other) missed += has(table, k);
                double tMiss = seconds(t0);
                std::printf("timing %-14s insert %.3f s, 700000 hits %.3f s, 700000 misses %.3f s (%zu %zu)\n", name, tInsert, tHit, tMiss, found, missed);
            };
            std::unordered_set<uint64_t> us;
            us.reserve(n);
            bench("unordered_set", us, [](auto& t, uint64_t k) { t.insert(k); }, [](auto& t, uint64_t k) { return t.count(k); });
            RobinHoodMap rh(1 << 20, 0.9);
            bench("RobinHoodMap", rh, [](auto& t, uint64_t k) { t.put(k, 0); }, [](auto& t, uint64_t k) { return t.find(k) != nullptr; });
            CuckooSet<4> ck(1 << 18);
            bench("CuckooSet<4>", ck, [](auto& t, uint64_t k) { t.put(k); }, [](auto& t, uint64_t k) { return t.contains(k); });
            SwissSet sw(1 << 17);
            bench("SwissSet", sw, [](auto& t, uint64_t k) { t.put(k); }, [](auto& t, uint64_t k) { return t.contains(k); });
        }
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
