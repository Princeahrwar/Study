// Tutorial: hash tables inside. Every function and class the tutorial shows is here, followed by
// brute-force comparisons, simulations and the checks behind every number quoted in the text.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 01_hash_tables_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed". Timing lines are printed for information only.

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

// ---------------------------------------------------------------- section 2: hash functions
// The finalizer of splitmix64: every output bit depends on every input bit.
uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;              // fold the high bits down, then spread upwards
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// The exact inverse of mix64. A hash that is not keyed can be run backwards.
uint64_t unmix64(uint64_t x) {
    x ^= (x >> 31) ^ (x >> 62);                               // undo each step in reverse order
    x *= 0x319642B2D24D8EC3ULL;                               // inverse of 0x94D049BB133111EB modulo 2^64
    x ^= (x >> 27) ^ (x >> 54);
    x *= 0x96DE1B173F119089ULL;                               // inverse of 0xBF58476D1CE4E5B9 modulo 2^64
    x ^= (x >> 30) ^ (x >> 60);
    return x;
}

// Fibonacci hashing: multiply by 2^64 divided by the golden ratio and keep the TOP bits.
uint64_t fibonacciHash(uint64_t key, int bits) {
    return (key * 0x9E3779B97F4A7C15ULL) >> (64 - bits);
}

// FNV-1a over the bytes of a string: xor in a byte, then multiply by a prime.
uint64_t fnv1a(std::string_view s) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001B3ULL;
    }
    return h;
}

// The hash Java uses for strings: h = 31 * h + c. Fast, simple, and easy to attack.
uint32_t javaStringHash(std::string_view s) {
    uint32_t h = 0;
    for (unsigned char c : s) h = 31 * h + c;
    return h;
}

// Mix one more value into a running hash (the idea of boost::hash_combine).
size_t hashCombine(size_t seed, size_t value) {
    return seed ^ (mix64(value) + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2));
}

struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return hashCombine(mix64((uint64_t)p.first), (size_t)p.second);   // order matters: (a, b) differs from (b, a)
    }
};

// The tempting wrong way: xor. (a, b) and (b, a) collide, (a, a) is always 0, and the result stays small.
struct XorPairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return (size_t)p.first ^ (size_t)p.second;
    }
};

// ---------------------------------------------------------------- section 3: collisions
// The probability that n keys hashed uniformly into m slots are NOT all different (the birthday problem).
double collisionProbability(double n, double m) {
    double allDifferent = 1.0;
    for (int i = 0; i < (int)n; ++i) allDifferent *= 1.0 - i / m;
    return 1.0 - allDifferent;
}

// The same for huge m, where the loop above would be too long: 1 - e^(-n(n-1)/2m).
double collisionProbabilityApprox(double n, double m) {
    return -std::expm1(-n * (n - 1) / (2 * m));
}

// A small deterministic random number generator (splitmix64), so every run and every compiler sees the same numbers.
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t next() { return mix64(state += 0x9E3779B97F4A7C15ULL); }
    uint64_t below(uint64_t n) { return next() % n; }
};

size_t roundUpToPowerOfTwo(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// ---------------------------------------------------------------- section 4: chaining
// A hash table with separate chaining: 64-bit keys to ints. Each bucket holds a small vector of entries;
// a linked list would behave the same way. The seed makes the hash function depend on a secret.
class ChainedTable {
public:
    explicit ChainedTable(size_t buckets = 8, double maxLoad = 1.0, bool autoResize = true, uint64_t seed = 0)
        : buckets_(roundUpToPowerOfTwo(buckets)), maxLoad_(maxLoad), autoResize_(autoResize), seed_(seed) {}

    bool put(uint64_t key, int value) {                       // true if the key was new
        auto& chain = buckets_[indexOf(key)];
        for (auto& e : chain)
            if (e.key == key) { e.value = value; return false; }
        chain.push_back({key, value});
        ++size_;
        if (autoResize_ && size_ > maxLoad_ * buckets_.size()) rehash(buckets_.size() * 2);
        return true;
    }

    int* find(uint64_t key) {
        for (auto& e : buckets_[indexOf(key)]) {
            ++comparisons;
            if (e.key == key) return &e.value;
        }
        return nullptr;
    }

    bool erase(uint64_t key) {
        auto& chain = buckets_[indexOf(key)];
        for (size_t i = 0; i < chain.size(); ++i)
            if (chain[i].key == key) {
                chain[i] = chain.back();                      // order inside a chain does not matter
                chain.pop_back();
                --size_;
                return true;
            }
        return false;
    }

    size_t size() const { return size_; }
    size_t bucketCount() const { return buckets_.size(); }
    double loadFactor() const { return (double)size_ / buckets_.size(); }
    size_t longestChain() const {
        size_t longest = 0;
        for (const auto& chain : buckets_) longest = std::max(longest, chain.size());
        return longest;
    }

    long long comparisons = 0;                                // keys compared by find, for the measurements
    long long rehashMoves = 0;                                // entries moved by all rehashes so far
    long long lastRehashMoves = 0;                            // entries moved by the most recent rehash

private:
    struct Entry { uint64_t key; int value; };

    size_t indexOf(uint64_t key) const { return mix64(key ^ seed_) & (buckets_.size() - 1); }

    void rehash(size_t newCount) {
        std::vector<std::vector<Entry>> old(newCount);
        old.swap(buckets_);
        lastRehashMoves = 0;
        for (auto& chain : old)
            for (auto& e : chain) {
                buckets_[indexOf(e.key)].push_back(e);
                ++lastRehashMoves;
            }
        rehashMoves += lastRehashMoves;
    }

    std::vector<std::vector<Entry>> buckets_;
    size_t size_ = 0;
    double maxLoad_;
    bool autoResize_;
    uint64_t seed_;
};

// ---------------------------------------------------------------- section 5: open addressing
// A hash table with linear probing: all entries live in three flat arrays. Deleted entries leave tombstones.
class LinearProbingTable {
public:
    explicit LinearProbingTable(size_t capacity = 16, double maxLoad = 0.7, bool autoResize = true)
        : keys_(roundUpToPowerOfTwo(capacity)), values_(keys_.size()), state_(keys_.size(), Empty),
          maxLoad_(maxLoad), autoResize_(autoResize) {}

    bool put(uint64_t key, int value) {                       // true if the key was new
        const size_t mask = keys_.size() - 1, none = (size_t)-1;
        size_t i = mix64(key) & mask, firstFree = none;
        while (state_[i] != Empty) {                          // an Empty slot ends the search
            if (state_[i] == Deleted) {
                if (firstFree == none) firstFree = i;         // remember the first tombstone: we may reuse it
            } else if (keys_[i] == key) {
                values_[i] = value;
                return false;
            }
            i = (i + 1) & mask;
        }
        size_t slot = (firstFree != none) ? firstFree : i;    // the key is new: reuse a tombstone if we passed one
        if (state_[slot] == Empty) ++used_;                   // a tombstone was already counted as used
        keys_[slot] = key;
        values_[slot] = value;
        state_[slot] = Full;
        ++size_;
        if (autoResize_ && used_ > maxLoad_ * keys_.size())   // tombstones count towards the load
            rehash(size_ * 2 > maxLoad_ * keys_.size() ? keys_.size() * 2 : keys_.size());
        return true;
    }

    int* find(uint64_t key) {
        const size_t mask = keys_.size() - 1;
        size_t i = mix64(key) & mask;
        while (true) {                                        // ends: at least one slot is always Empty
            ++probes;
            if (state_[i] == Empty) return nullptr;           // a never-used slot: the key cannot be further on
            if (state_[i] == Full && keys_[i] == key) return &values_[i];
            i = (i + 1) & mask;                               // Deleted or another key: keep going
        }
    }

    bool erase(uint64_t key) {
        const size_t mask = keys_.size() - 1;
        size_t i = mix64(key) & mask;
        while (state_[i] != Empty) {
            if (state_[i] == Full && keys_[i] == key) {
                state_[i] = Deleted;                          // not Empty: that would cut the probe sequences through here
                --size_;
                return true;
            }
            i = (i + 1) & mask;
        }
        return false;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return keys_.size(); }
    size_t tombstones() const { return used_ - size_; }

    long long probes = 0;                                     // slots examined by find, for the measurements

private:
    enum State : unsigned char { Empty, Full, Deleted };

    void rehash(size_t newCapacity) {
        std::vector<uint64_t> oldKeys(newCapacity);
        std::vector<int> oldValues(newCapacity);
        std::vector<State> oldState(newCapacity, Empty);
        oldKeys.swap(keys_);
        oldValues.swap(values_);
        oldState.swap(state_);
        size_ = used_ = 0;
        for (size_t i = 0; i < oldKeys.size(); ++i)
            if (oldState[i] == Full) put(oldKeys[i], oldValues[i]);   // tombstones are dropped here
    }

    std::vector<uint64_t> keys_;
    std::vector<int> values_;
    std::vector<State> state_;
    size_t size_ = 0, used_ = 0;                              // used_ = full slots + tombstones
    double maxLoad_;
    bool autoResize_;
};

struct ProbeStats {
    double successful;                                        // average cost of finding a key that is in the table
    double unsuccessful;                                      // average cost of searching for a key that is not
};

// Fill a fixed-size chained table to the given load with random keys, then measure the average number of keys
// compared by successful and by unsuccessful searches.
ProbeStats measureChaining(size_t buckets, double load, uint64_t seed) {
    Rng rng(seed);
    ChainedTable table(buckets, 1.0, false);
    std::vector<uint64_t> keys;
    size_t target = (size_t)(load * table.bucketCount());
    while (keys.size() < target) {
        uint64_t k = rng.next();
        if (table.put(k, 0)) keys.push_back(k);
    }
    table.comparisons = 0;
    for (uint64_t k : keys) table.find(k);
    double successful = (double)table.comparisons / keys.size();
    table.comparisons = 0;
    const int queries = 200000;
    for (int q = 0; q < queries; ++q) table.find(rng.next());     // random keys: not in the table
    return {successful, (double)table.comparisons / queries};
}

// The same measurement for linear probing; the cost is the number of slots examined.
ProbeStats measureLinearProbing(size_t capacity, double load, uint64_t seed) {
    Rng rng(seed);
    LinearProbingTable table(capacity, 0.99, false);
    std::vector<uint64_t> keys;
    size_t target = (size_t)(load * table.capacity());
    while (keys.size() < target) {
        uint64_t k = rng.next();
        if (table.put(k, 0)) keys.push_back(k);
    }
    table.probes = 0;
    for (uint64_t k : keys) table.find(k);
    double successful = (double)table.probes / keys.size();
    table.probes = 0;
    const int queries = 200000;
    for (int q = 0; q < queries; ++q) table.find(rng.next());
    return {successful, (double)table.probes / queries};
}

// What the textbooks predict (Knuth) for linear probing at load alpha, and for chaining with n keys in m buckets.
double linearProbingSuccessful(double alpha) { return 0.5 * (1 + 1 / (1 - alpha)); }
double linearProbingUnsuccessful(double alpha) { return 0.5 * (1 + 1 / ((1 - alpha) * (1 - alpha))); }
double chainingSuccessful(double n, double m) { return 1 + (n - 1) / (2 * m); }

// ---------------------------------------------------------------- section 7: universal hashing
const uint64_t P61 = (1ULL << 61) - 1;                        // the Mersenne prime 2^61 - 1

// a * b mod (2^61 - 1) without a division: 2^61 is 1 modulo the prime, so the high part just adds to the low part.
uint64_t mulmod61(uint64_t a, uint64_t b) {
    __uint128_t t = (__uint128_t)a * b;
    uint64_t r = (uint64_t)(t & P61) + (uint64_t)(t >> 61);
    return r >= P61 ? r - P61 : r;
}

// h(x) = ((a * x + b) mod p), to be reduced to the table size by the caller. a and b are chosen at random
// when the table is created; for any two different keys below p, the chance over (a, b) that they collide
// after the final "mod m" is at most about 1 / m.
struct UniversalHash {
    uint64_t a, b;
    UniversalHash(uint64_t randomA, uint64_t randomB) : a(1 + randomA % (P61 - 1)), b(randomB % P61) {}
    uint64_t operator()(uint64_t x) const { return (mulmod61(a, x % P61) + b) % P61; }
};

// For every pair of different keys below the prime p, the fraction of all (a, b) choices that make them
// collide in a table of m slots. Returns the largest fraction over all pairs, by trying everything.
double worstCollisionFraction(int p, int m) {
    double worst = 0;
    for (int x = 0; x < p; ++x)
        for (int y = x + 1; y < p; ++y) {
            long long collide = 0;
            for (int a = 1; a < p; ++a)
                for (int b = 0; b < p; ++b)
                    if (((a * x + b) % p) % m == ((a * y + b) % p) % m) ++collide;
            worst = std::max(worst, (double)collide / ((long long)(p - 1) * p));
        }
    return worst;
}

// The same measurement for the fixed hash h(x) = x mod m, which has no random choice at all.
double worstCollisionFractionFixed(int p, int m) {
    double worst = 0;
    for (int x = 0; x < p; ++x)
        for (int y = x + 1; y < p; ++y) worst = std::max(worst, x % m == y % m ? 1.0 : 0.0);
    return worst;
}

// ---------------------------------------------------------------- section 8: hash flooding
// Keys that all land in bucket 0 of a ChainedTable with `bucketCount` buckets and seed 0: run the hash backwards.
std::vector<uint64_t> floodKeys(size_t count, size_t bucketCount) {
    std::vector<uint64_t> keys;
    for (size_t j = 0; j < count; ++j) keys.push_back(unmix64((uint64_t)j * bucketCount));   // mix64(key) = j * bucketCount
    return keys;
}

// A hash for std::unordered_map with a random secret chosen once per process, so an attacker cannot prepare
// colliding keys in advance. (A production system would use a keyed hash such as SipHash.)
struct RandomizedHash {
    size_t operator()(uint64_t key) const noexcept { return mix64(key ^ secret()); }

private:
    static uint64_t secret() {
        static const uint64_t value = mix64((uint64_t)std::chrono::steady_clock::now().time_since_epoch().count());
        return value;
    }
};

// ---------------------------------------------------------------- problem 3: insert, remove, getRandom in O(1)
// A vector gives a random element in O(1); a hash map from value to position gives find and erase in O(1).
// Removing swaps the last value into the hole, so the vector never has a gap (the idea of ChainedTable::erase).
class RandomizedSet {
public:
    bool insert(int value) {
        if (position_.count(value)) return false;
        position_[value] = (int)values_.size();
        values_.push_back(value);
        return true;
    }

    bool remove(int value) {
        auto it = position_.find(value);
        if (it == position_.end()) return false;
        int last = values_.back();
        values_[it->second] = last;                           // the last value takes the removed value's place
        position_[last] = it->second;
        values_.pop_back();
        position_.erase(value);                               // erase last: this is also right when value == last
        return true;
    }

    int getRandom(Rng& rng) const { return values_[rng.below(values_.size())]; }
    size_t size() const { return values_.size(); }

private:
    std::vector<int> values_;
    std::unordered_map<int, int> position_;
};

// ================================================================ tests
static double seconds(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

int main() {
    // ---- hash functions
    CHECK(fnv1a("") == 0xCBF29CE484222325ULL);
    CHECK(fnv1a("a") == 0xAF63DC4C8601EC8CULL);
    CHECK(fnv1a("foobar") == 0x85944171F73967E8ULL);
    CHECK(javaStringHash("Aa") == 2112 && javaStringHash("BB") == 2112);
    {
        // Every concatenation of the blocks "Aa" and "BB" has the same hash: 2^8 strings, one hash value.
        std::vector<std::string> strings = {""};
        for (int round = 0; round < 8; ++round) {
            std::vector<std::string> next;
            for (const auto& s : strings) { next.push_back(s + "Aa"); next.push_back(s + "BB"); }
            strings = next;
        }
        std::vector<uint32_t> hashes;
        for (const auto& s : strings) hashes.push_back(javaStringHash(s));
        std::sort(hashes.begin(), hashes.end());
        hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
        CHECK(strings.size() == 256 && hashes.size() == 1);
        std::unordered_map<std::string, int> distinctStrings;
        for (const auto& s : strings) distinctStrings[s] = 1;
        CHECK(distinctStrings.size() == 256);
    }
    {
        Rng rng(1);
        for (int i = 0; i < 20000; ++i) {
            uint64_t x = rng.next();
            CHECK(unmix64(mix64(x)) == x);
            CHECK(mix64(unmix64(x)) == x);
        }
        // Avalanche: flipping one input bit flips about half of the 64 output bits.
        long long flips = 0, trials = 0;
        for (int i = 0; i < 2000; ++i) {
            uint64_t x = rng.next();
            for (int b = 0; b < 64; ++b) {
                flips += std::popcount(mix64(x) ^ mix64(x ^ (1ULL << b)));
                ++trials;
            }
        }
        double avg = (double)flips / trials;
        std::printf("avalanche: mix64 flips %.2f of 64 output bits per flipped input bit\n", avg);
        CHECK(avg > 31.5 && avg < 32.5);
        // The identity hash flips exactly one bit.
        CHECK(std::popcount((uint64_t)5 ^ (uint64_t)(5 ^ 8)) == 1);
        // Keys that are multiples of 1024 in a table of 1024 buckets: the identity hash puts them all in bucket 0.
        const size_t N = 100000, B = 1024;
        std::vector<int> identity(B), mixed(B), fib(B);
        for (size_t i = 0; i < N; ++i) {
            uint64_t key = i * 1024;
            ++identity[key & (B - 1)];
            ++mixed[mix64(key) & (B - 1)];
            ++fib[fibonacciHash(key, 10)];
        }
        int idMax = *std::max_element(identity.begin(), identity.end());
        int mixMax = *std::max_element(mixed.begin(), mixed.end());
        int fibMax = *std::max_element(fib.begin(), fib.end());
        int fibMin = *std::min_element(fib.begin(), fib.end());
        std::printf("100000 keys that are multiples of 1024, 1024 buckets: largest bucket identity %d, mix64 %d, fibonacci %d (smallest %d)\n",
                    idMax, mixMax, fibMax, fibMin);
        CHECK(idMax == (int)N);
        CHECK(mixMax < 160);
        CHECK(fibMax - fibMin <= 8);
        // Sequential keys: the identity hash spreads them perfectly, Fibonacci hashing spreads them evenly as well.
        std::vector<int> seqFib(B);
        for (size_t i = 0; i < 4 * B; ++i) ++seqFib[fibonacciHash(i, 10)];
        std::printf("4096 sequential keys, 1024 buckets, Fibonacci hashing: largest bucket %d, smallest %d (ideal: 4)\n",
                    *std::max_element(seqFib.begin(), seqFib.end()), *std::min_element(seqFib.begin(), seqFib.end()));
        CHECK(*std::max_element(seqFib.begin(), seqFib.end()) - *std::min_element(seqFib.begin(), seqFib.end()) <= 4);
    }
    {
        // Pair hashing: xor versus a real combine, over all pairs (a, b) with a and b below 100.
        std::vector<size_t> xorHashes, goodHashes;
        for (int a = 0; a < 100; ++a)
            for (int b = 0; b < 100; ++b) {
                xorHashes.push_back(XorPairHash()({a, b}));
                goodHashes.push_back(PairHash()({a, b}));
            }
        auto distinct = [](std::vector<size_t> v) {
            std::sort(v.begin(), v.end());
            return (size_t)(std::unique(v.begin(), v.end()) - v.begin());
        };
        size_t xorDistinct = distinct(xorHashes), goodDistinct = distinct(goodHashes);
        std::printf("10000 pairs: xor gives %zu distinct hash values, hashCombine gives %zu\n", xorDistinct, goodDistinct);
        CHECK(xorDistinct == 128);
        CHECK(goodDistinct == 10000);
        CHECK(XorPairHash()({3, 9}) == XorPairHash()({9, 3}));
        CHECK(XorPairHash()({7, 7}) == 0);
        CHECK(PairHash()({3, 9}) != PairHash()({9, 3}));
        std::unordered_map<std::pair<int, int>, int, PairHash> grid;
        for (int a = 0; a < 50; ++a)
            for (int b = 0; b < 50; ++b) grid[{a, b}] = a * 50 + b;
        CHECK(grid.size() == 2500 && grid.at({7, 9}) == 359 && grid.count({9, 7}) == 1 && grid.at({9, 7}) == 457);
    }

    // ---- collisions and the birthday bound
    CHECK(std::fabs(collisionProbability(23, 365) - 0.507297) < 1e-5);
    CHECK(std::fabs(collisionProbabilityApprox(23, 365) - 0.5000) < 0.001);
    CHECK(std::fabs(collisionProbability(77163, 4294967296.0) - 0.5) < 0.001);
    CHECK(std::fabs(collisionProbabilityApprox(77163, 4294967296.0) - 0.5) < 0.001);
    CHECK(collisionProbabilityApprox(1e6, 4294967296.0) > 0.999999);                  // a million keys, 32-bit hashes
    {
        double p64 = collisionProbabilityApprox(1e6, 18446744073709551616.0);
        std::printf("1e6 keys with 64-bit hashes: P(any collision) = %.3g\n", p64);
        CHECK(p64 > 2.6e-8 && p64 < 2.8e-8);
        // A simulation agrees with the exact formula: 40 keys into 1000 buckets.
        Rng rng(5);
        int trials = 20000, withCollision = 0;
        for (int t = 0; t < trials; ++t) {
            std::vector<int> seen(1000, 0);
            bool collided = false;
            for (int i = 0; i < 40; ++i)
                if (seen[rng.below(1000)]++) collided = true;
            withCollision += collided;
        }
        double simulated = (double)withCollision / trials, exact = collisionProbability(40, 1000);
        std::printf("40 keys, 1000 buckets: simulated %.4f, formula %.4f\n", simulated, exact);
        CHECK(std::fabs(simulated - exact) < 0.02);
    }

    // ---- chaining: correctness against std::unordered_map
    {
        Rng rng(11);
        ChainedTable table(4);
        std::unordered_map<uint64_t, int> reference;
        for (int step = 0; step < 200000; ++step) {
            uint64_t key = rng.below(600);                    // a small key range forces collisions, updates and erases
            int op = (int)rng.below(10);
            if (op < 5) {
                int value = (int)rng.below(1000);
                bool fresh = table.put(key, value);
                CHECK(fresh == (reference.count(key) == 0));
                reference[key] = value;
            } else if (op < 8) {
                int* found = table.find(key);
                auto it = reference.find(key);
                CHECK((found != nullptr) == (it != reference.end()));
                if (found && it != reference.end()) CHECK(*found == it->second);
            } else {
                CHECK(table.erase(key) == (reference.erase(key) == 1));
            }
            CHECK(table.size() == reference.size());
        }
        CHECK(table.loadFactor() <= 1.0);
    }
    // ---- linear probing: correctness under heavy churn (tombstones)
    {
        Rng rng(12);
        LinearProbingTable table(4);
        std::unordered_map<uint64_t, int> reference;
        size_t maxTombstones = 0;
        for (int step = 0; step < 300000; ++step) {
            uint64_t key = rng.below(500);
            int op = (int)rng.below(10);
            if (op < 4) {
                int value = (int)rng.below(1000);
                bool fresh = table.put(key, value);
                CHECK(fresh == (reference.count(key) == 0));
                reference[key] = value;
            } else if (op < 7) {
                int* found = table.find(key);
                auto it = reference.find(key);
                CHECK((found != nullptr) == (it != reference.end()));
                if (found && it != reference.end()) CHECK(*found == it->second);
            } else {
                CHECK(table.erase(key) == (reference.erase(key) == 1));
            }
            CHECK(table.size() == reference.size());
            maxTombstones = std::max(maxTombstones, table.tombstones());
        }
        CHECK(table.size() + table.tombstones() <= 0.7 * table.capacity() + 1);
        std::printf("linear probing churn: capacity %zu, size %zu, most tombstones seen %zu\n",
                    table.capacity(), table.size(), maxTombstones);
        // A tombstone must not stop a search: put a, b, c in one run, erase b, c must still be found.
        LinearProbingTable small(8, 0.9, false);
        uint64_t k1 = 1;
        size_t h = mix64(k1) & 7;
        std::vector<uint64_t> sameSlot;                       // keys whose home slot is the same as k1's
        for (uint64_t k = 2; sameSlot.size() < 2; ++k)
            if ((mix64(k) & 7) == h) sameSlot.push_back(k);
        small.put(k1, 10);
        small.put(sameSlot[0], 20);
        small.put(sameSlot[1], 30);
        CHECK(small.erase(sameSlot[0]));
        CHECK(small.find(sameSlot[1]) != nullptr && *small.find(sameSlot[1]) == 30);
        CHECK(small.tombstones() == 1);
        small.put(sameSlot[0], 21);                           // reuses the tombstone
        CHECK(small.tombstones() == 0 && *small.find(sameSlot[0]) == 21);
    }

    // ---- the probe-count formulas, checked by simulation
    {
        std::printf("\nchaining, 4096 buckets (comparisons per search)\n  load  succ.meas  succ.formula  unsucc.meas  unsucc.formula\n");
        for (double load : {0.5, 1.0, 2.0, 4.0}) {
            ProbeStats s = measureChaining(4096, load, 100);
            double n = load * 4096;
            std::printf("  %4.1f  %9.3f  %12.3f  %11.3f  %14.3f\n", load, s.successful, chainingSuccessful(n, 4096), s.unsuccessful, load);
            CHECK(std::fabs(s.successful / chainingSuccessful(n, 4096) - 1) < 0.03);
            CHECK(std::fabs(s.unsuccessful / load - 1) < 0.03);
        }
        std::printf("\nlinear probing, 262144 slots (slots examined per search)\n  load  succ.meas  succ.formula  unsucc.meas  unsucc.formula\n");
        for (double load : {0.5, 0.7, 0.9}) {
            ProbeStats s = measureLinearProbing(262144, load, 200);
            std::printf("  %4.1f  %9.3f  %12.3f  %11.3f  %14.3f\n", load, s.successful, linearProbingSuccessful(load),
                        s.unsuccessful, linearProbingUnsuccessful(load));
            const double tolerance = 0.04;
            CHECK(std::fabs(s.successful / linearProbingSuccessful(load) - 1) < tolerance);
            CHECK(std::fabs(s.unsuccessful / linearProbingUnsuccessful(load) - 1) < tolerance);
        }
    }

    // ---- resizing and amortised cost
    {
        ChainedTable table(8);
        const size_t N = 100000;
        size_t biggestSingleRehash = 0, rehashes = 0;
        for (size_t i = 0; i < N; ++i) {
            long long before = table.rehashMoves;
            table.put(i * 7 + 1, 0);
            if (table.rehashMoves != before) {
                ++rehashes;
                biggestSingleRehash = std::max(biggestSingleRehash, (size_t)table.lastRehashMoves);
            }
        }
        std::printf("\n%zu inserts into a table that starts with 8 buckets: %zu rehashes, %lld entries moved in total, the largest single rehash moved %zu\n",
                    N, rehashes, table.rehashMoves, biggestSingleRehash);
        CHECK(table.rehashMoves < 2 * (long long)N);
        CHECK(rehashes == 14);                                // 8 -> 16 -> ... -> 131072
        CHECK(biggestSingleRehash == 65537);                  // inserting the 65537th key moves everything so far
        CHECK(table.bucketCount() == 131072);
        // reserve-style presizing: no rehash at all.
        ChainedTable presized(131072);
        for (size_t i = 0; i < N; ++i) presized.put(i * 7 + 1, 0);
        CHECK(presized.rehashMoves == 0);
    }

    // ---- universal hashing
    {
        Rng rng(21);
        for (int i = 0; i < 100000; ++i) {
            uint64_t a = rng.next() % P61, b = rng.next() % P61;
            CHECK(mulmod61(a, b) == (uint64_t)(((__uint128_t)a * b) % P61));
        }
        CHECK(mulmod61(P61 - 1, P61 - 1) == 1);               // (-1) * (-1) = 1
        CHECK(mulmod61(0, 12345) == 0);
        double w31 = worstCollisionFraction(31, 7), w101 = worstCollisionFraction(101, 10);
        double f31 = worstCollisionFractionFixed(31, 7), f101 = worstCollisionFractionFixed(101, 10);
        std::printf("\nworst collision fraction over all pairs of keys, all (a, b): p=31 m=7: %.4f (1/m = %.4f); p=101 m=10: %.4f (1/m = %.4f)\n",
                    w31, 1.0 / 7, w101, 1.0 / 10);
        std::printf("the fixed hash x mod m: worst fraction %.1f and %.1f\n", f31, f101);
        CHECK(w31 <= 1.0 / 7 + 1e-12);
        CHECK(w101 <= 1.0 / 10 + 1e-12);
        CHECK(f31 == 1.0 && f101 == 1.0);
        UniversalHash h(rng.next(), rng.next());
        CHECK(h(5) < P61 && h(P61 - 1) < P61);
        CHECK(h(123) == h(123) && h(123) != h(124));
    }

    // ---- hash flooding
    {
        const size_t N = 10000, B = 16384;
        std::vector<uint64_t> keys = floodKeys(N, B);
        // Against a table with an unkeyed hash, every key lands in bucket 0 ...
        ChainedTable victim(B, 1.0, false, 0);
        for (size_t i = 0; i < keys.size(); ++i) victim.put(keys[i], (int)i);
        CHECK(victim.size() == N && victim.longestChain() == N);
        victim.comparisons = 0;
        for (uint64_t k : keys) victim.find(k);
        CHECK(victim.comparisons == (long long)N * (N + 1) / 2);   // the key in position j of the chain costs j comparisons
        // ... against a table with a secret seed, the same keys spread out like random keys.
        ChainedTable keyed(B, 1.0, false, 0x5EC12E7ULL * 0x9E3779B97F4A7C15ULL);
        for (size_t i = 0; i < keys.size(); ++i) keyed.put(keys[i], (int)i);
        std::printf("\nflooding %zu crafted keys: longest chain %zu with seed 0, %zu with a secret seed\n", N, victim.longestChain(), keyed.longestChain());
        CHECK(keyed.size() == N && keyed.longestChain() <= 12);
        keyed.comparisons = 0;
        for (uint64_t k : keys) keyed.find(k);
        std::printf("comparisons to look all of them up again: %lld with seed 0, %lld with the secret seed\n", victim.comparisons, keyed.comparisons);
        CHECK(keyed.comparisons < 2 * (long long)N);
        // The same attack on std::unordered_map: keys that are multiples of the bucket count share a bucket.
        std::unordered_map<unsigned long long, int> m;
        m.reserve(N);
        size_t buckets = m.bucket_count();
        auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < N; ++i) m[(unsigned long long)i * buckets] = (int)i;
        auto t1 = std::chrono::steady_clock::now();
        CHECK(m.bucket_count() == buckets);                   // reserve: no rehash happened
        bool identityHash = std::hash<unsigned long long>()(12345) == 12345;   // true for libstdc++; other libraries may differ
        CHECK(!identityHash || m.bucket_size(0) == N);
        std::unordered_map<unsigned long long, int> fair;
        fair.reserve(N);
        Rng rng(31);
        for (size_t i = 0; i < N; ++i) fair[rng.next()] = (int)i;
        auto t2 = std::chrono::steady_clock::now();
        std::printf("std::unordered_map, %zu keys: multiples of the bucket count %.3f s (largest bucket %zu), random keys %.4f s\n",
                    N, seconds(t0, t1), m.bucket_size(0), seconds(t1, t2));
        // Mixing alone does not help once the attacker knows the function: run mix64 backwards for any table size.
        struct UnkeyedMix { size_t operator()(uint64_t key) const noexcept { return mix64(key); } };
        auto longestBucket = [](const auto& map) {
            size_t longest = 0;
            for (size_t b = 0; b < map.bucket_count(); ++b) longest = std::max(longest, map.bucket_size(b));
            return longest;
        };
        std::unordered_map<uint64_t, int, UnkeyedMix> mixed;
        mixed.reserve(N);
        for (size_t j = 0; j < N; ++j) mixed[unmix64((uint64_t)j * mixed.bucket_count())] = (int)j;
        std::unordered_map<uint64_t, int, RandomizedHash> randomized;
        randomized.reserve(N);
        for (size_t j = 0; j < N; ++j) randomized[unmix64((uint64_t)j * randomized.bucket_count())] = (int)j;
        std::unordered_map<uint64_t, int, RandomizedHash> randomized2;
        randomized2.reserve(N);
        for (size_t j = 0; j < N; ++j) randomized2[(uint64_t)j * randomized2.bucket_count()] = (int)j;
        std::printf("largest bucket with %zu crafted keys: fixed mix64 hash %zu, RandomizedHash %zu; multiples of the bucket count with RandomizedHash %zu\n",
                    N, longestBucket(mixed), longestBucket(randomized), longestBucket(randomized2));
        CHECK(longestBucket(mixed) == N);
        CHECK(longestBucket(randomized) <= 16);
        CHECK(longestBucket(randomized2) <= 16);
    }

    // ---- facts about std::unordered_map
    {
        std::unordered_map<int, int> m;
        std::printf("\nstd::hash<int>(5) = %zu, std::hash<int>(-1) = %zu, initial bucket_count = %zu, max_load_factor = %.1f\n",
                    std::hash<int>()(5), std::hash<int>()(-1), m.bucket_count(), m.max_load_factor());
        CHECK(m.max_load_factor() == 1.0f);
        m[1] = 1;
        int* ref = &m[1];
        size_t last = m.bucket_count();
        std::printf("bucket counts while inserting 1..100000: %zu", last);
        for (int i = 2; i <= 100000; ++i) {
            m[i] = i;
            if (m.bucket_count() != last) { last = m.bucket_count(); std::printf(", %zu", last); }
        }
        std::printf("\n");
        CHECK(ref == &m[1] && *ref == 1);                     // references to elements survive rehashing
        CHECK(m.size() == 100000);
        size_t before = m.size();
        (void)m[-5];                                          // operator[] inserts a missing key
        CHECK(m.size() == before + 1);
        CHECK(m.at(-5) == 0);
        std::unordered_map<int, int> reserved;
        reserved.reserve(5000);
        size_t buckets = reserved.bucket_count();
        for (int i = 0; i < 5000; ++i) reserved[i] = i;
        CHECK(reserved.bucket_count() == buckets);            // reserve(n) means n inserts cause no rehash
        char a[] = "abc", b[] = "abc";
        CHECK(std::hash<const char*>()(a) != std::hash<const char*>()(b));   // pointers are hashed, not the text
        CHECK(std::hash<std::string>()(std::string(a)) == std::hash<std::string>()(std::string(b)));
        CHECK(std::hash<double>()(0.0) == std::hash<double>()(-0.0));        // equal values must hash equally
    }

    // ---- RandomizedSet against std::set
    {
        Rng rng(51);
        RandomizedSet set;
        std::set<int> reference;
        for (int step = 0; step < 100000; ++step) {
            int value = (int)rng.below(60);
            int op = (int)rng.below(3);
            if (op == 0) CHECK(set.insert(value) == reference.insert(value).second);
            else if (op == 1) CHECK(set.remove(value) == (reference.erase(value) == 1));
            else CHECK(reference.empty() || reference.count(set.getRandom(rng)) == 1);
            CHECK(set.size() == reference.size());
        }
        RandomizedSet ten;
        for (int v = 0; v < 10; ++v) CHECK(ten.insert(v));
        CHECK(!ten.insert(3));
        int seen[10] = {};
        for (int i = 0; i < 10000; ++i) ++seen[ten.getRandom(rng)];
        CHECK(*std::min_element(seen, seen + 10) > 800 && *std::max_element(seen, seen + 10) < 1200);   // about 1000 each
        CHECK(ten.remove(9) && !ten.remove(9) && ten.size() == 9);
        for (int v = 0; v < 9; ++v) CHECK(ten.remove(v));
        CHECK(ten.size() == 0 && !ten.remove(0) && ten.insert(0));
    }

    // ---- speed (printed for information only; the numbers depend on the machine)
    {
        const int N = 1000000;
        Rng rng(41);
        std::vector<uint64_t> keys(N);
        for (auto& k : keys) k = rng.next();
        auto t0 = std::chrono::steady_clock::now();
        std::unordered_map<uint64_t, int> stdMap;
        for (int i = 0; i < N; ++i) stdMap[keys[i]] = i;
        long long sum1 = 0;
        for (int i = 0; i < N; ++i) sum1 += stdMap.find(keys[i])->second;
        auto t1 = std::chrono::steady_clock::now();
        LinearProbingTable flat(16, 0.5);
        for (int i = 0; i < N; ++i) flat.put(keys[i], i);
        long long sum2 = 0;
        for (int i = 0; i < N; ++i) sum2 += *flat.find(keys[i]);
        auto t2 = std::chrono::steady_clock::now();
        ChainedTable chained(16);
        for (int i = 0; i < N; ++i) chained.put(keys[i], i);
        long long sum3 = 0;
        for (int i = 0; i < N; ++i) sum3 += *chained.find(keys[i]);
        auto t3 = std::chrono::steady_clock::now();
        CHECK(sum1 == sum2 && sum2 == sum3);
        std::printf("\n1e6 inserts + 1e6 lookups of random 64-bit keys (information only): std::unordered_map %.3f s, LinearProbingTable %.3f s, ChainedTable (vector buckets) %.3f s\n",
                    seconds(t0, t1), seconds(t1, t2), seconds(t2, t3));
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
