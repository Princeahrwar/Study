// Tutorial: probabilistic structures. Every class the tutorial shows is here, followed by checks of the guarantees
// (no false negatives, never under-counting), comparisons of measured error with the formulas, and the experiments
// behind every number quoted in the text.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 04_probabilistic_structures_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <set>
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

// ---------------------------------------------------------------- section 2: the Bloom filter
// k bit positions from one key, using only two hashes: position i is h1 + i * h2 (double hashing).
struct TwoHashes { uint64_t h1, h2; };
TwoHashes twoHashes(uint64_t key) {
    uint64_t h = mix64(key);
    return {h, mix64(h) | 1};                                 // the second hash is made odd so that it never becomes 0
}

// The sizing formulas, written as code.
double bloomFalsePositive(double m, double k, double n) { return std::pow(1.0 - std::exp(-k * n / m), k); }
size_t bloomBitsFor(size_t n, double p) { return (size_t)std::ceil(-(double)n * std::log(p) / (std::log(2.0) * std::log(2.0))); }
int bloomHashesFor(size_t m, size_t n) { return std::max(1, (int)std::lround((double)m / (double)n * std::log(2.0))); }

class BloomFilter {
    std::vector<uint64_t> words_;
    size_t m_;                                                // bits
    int k_;                                                   // positions per key

    size_t position(const TwoHashes& h, int i) const { return (h.h1 + (uint64_t)i * h.h2) % m_; }

public:
    BloomFilter(size_t bits, int hashes) : words_((bits + 63) / 64, 0), m_(bits), k_(hashes) {}

    void add(uint64_t key) {
        TwoHashes h = twoHashes(key);
        for (int i = 0; i < k_; ++i) {
            size_t b = position(h, i);
            words_[b >> 6] |= 1ULL << (b & 63);
        }
    }

    bool mightContain(uint64_t key) const {
        TwoHashes h = twoHashes(key);
        for (int i = 0; i < k_; ++i) {
            size_t b = position(h, i);
            if (!(words_[b >> 6] >> (b & 63) & 1)) return false;   // one zero bit: the key is definitely absent
        }
        return true;                                          // all ones: present, or a false positive
    }

    size_t bitsSet() const {
        size_t c = 0;
        for (uint64_t w : words_) c += std::popcount(w);
        return c;
    }

    double estimateCount() const { return -(double)m_ / k_ * std::log(1.0 - (double)bitsSet() / (double)m_); }

    void merge(const BloomFilter& other) {                    // the union of two filters with the same m and k
        for (size_t i = 0; i < words_.size(); ++i) words_[i] |= other.words_[i];
    }
    // ---- for the tests and the experiments
    bool sameBits(const BloomFilter& o) const { return words_ == o.words_; }
    double fillRatio() const { return (double)bitsSet() / (double)m_; }
};

// ---------------------------------------------------------------- section 3: the counting Bloom filter
// The same k positions, but each is a small counter instead of a bit, so a key can be taken out again.
class CountingBloom {
    static constexpr uint8_t kMax = 15;                       // a 4-bit counter; stored one per byte here to keep the code short
    std::vector<uint8_t> counters_;
    size_t m_;
    int k_;

public:
    CountingBloom(size_t counters, int hashes) : counters_(counters, 0), m_(counters), k_(hashes) {}

    void add(uint64_t key) {
        TwoHashes h = twoHashes(key);
        for (int i = 0; i < k_; ++i) {
            uint8_t& c = counters_[(h.h1 + (uint64_t)i * h.h2) % m_];
            if (c < kMax) ++c;                                // a counter at the maximum stays there
        }
    }

    bool mightContain(uint64_t key) const {
        TwoHashes h = twoHashes(key);
        for (int i = 0; i < k_; ++i)
            if (counters_[(h.h1 + (uint64_t)i * h.h2) % m_] == 0) return false;
        return true;
    }

    bool remove(uint64_t key) {                               // only call this for a key that was added
        if (!mightContain(key)) return false;                 // a zero counter must never be decremented
        TwoHashes h = twoHashes(key);
        for (int i = 0; i < k_; ++i) {
            uint8_t& c = counters_[(h.h1 + (uint64_t)i * h.h2) % m_];
            if (c < kMax) --c;                                // a saturated counter no longer knows its count: leave it
        }
        return true;
    }
};

// ---------------------------------------------------------------- section 4: the cuckoo filter
// A cuckoo hash table that stores only a short fingerprint of each key, in buckets of four slots.
class CuckooFilter {
    static constexpr int kSlots = 4;                          // fingerprints per bucket
    static constexpr int kMaxKicks = 500;
    std::vector<uint16_t> table_;                             // buckets * kSlots fingerprints; 0 means an empty slot
    size_t mask_;                                             // buckets - 1; the number of buckets is a power of two
    uint16_t fpMask_;
    size_t size_ = 0;
    Rng rng_{12345};

    struct Probe { size_t bucket; uint16_t fp; };
    Probe probe(uint64_t key) const {
        uint64_t h = mix64(key);
        uint16_t fp = (uint16_t)((h >> 32) & fpMask_);
        if (fp == 0) fp = 1;                                  // 0 is reserved for "empty"
        return {(size_t)(h & mask_), fp};
    }
    size_t other(size_t bucket, uint16_t fp) const { return (bucket ^ mix64(fp)) & mask_; }   // xor again to come back

    bool place(size_t bucket, uint16_t fp) {                  // put the fingerprint into a free slot of the bucket
        for (int s = 0; s < kSlots; ++s) {
            uint16_t& cell = table_[bucket * kSlots + s];
            if (cell == 0) { cell = fp; return true; }
        }
        return false;
    }

public:
    CuckooFilter(size_t buckets, int fingerprintBits)         // buckets: a power of two; fingerprintBits: 1 to 16
        : table_(buckets * kSlots, 0), mask_(buckets - 1), fpMask_((uint16_t)((1u << fingerprintBits) - 1)) {}

    bool add(uint64_t key) {
        Probe p = probe(key);
        size_t b2 = other(p.bucket, p.fp);
        if (place(p.bucket, p.fp) || place(b2, p.fp)) { ++size_; return true; }
        std::vector<std::pair<size_t, int>> path;             // (bucket, slot) of every swap, so that a failure can be undone
        size_t bucket = (rng_.next() & 1) ? p.bucket : b2;
        uint16_t fp = p.fp;
        for (int kick = 0; kick < kMaxKicks; ++kick) {
            int s = (int)rng_.below(kSlots);
            std::swap(fp, table_[bucket * kSlots + s]);       // take the new fingerprint's place, pick up the resident
            path.push_back({bucket, s});
            bucket = other(bucket, fp);                       // the resident moves to its other bucket
            if (place(bucket, fp)) { ++size_; return true; }
        }
        for (size_t j = path.size(); j-- > 0;)                // give up: undo every swap, so no fingerprint is lost
            std::swap(fp, table_[path[j].first * kSlots + path[j].second]);
        return false;
    }

    bool mightContain(uint64_t key) const {
        Probe p = probe(key);
        size_t b2 = other(p.bucket, p.fp);
        for (size_t bucket : {p.bucket, b2})
            for (int s = 0; s < kSlots; ++s)
                if (table_[bucket * kSlots + s] == p.fp) return true;
        return false;
    }

    bool remove(uint64_t key) {                               // only call this for a key that was added
        Probe p = probe(key);
        size_t b2 = other(p.bucket, p.fp);
        for (size_t bucket : {p.bucket, b2})
            for (int s = 0; s < kSlots; ++s) {
                uint16_t& cell = table_[bucket * kSlots + s];
                if (cell == p.fp) { cell = 0; --size_; return true; }
            }
        return false;
    }

    size_t size() const { return size_; }
    double load() const { return (double)size_ / (double)table_.size(); }
    // ---- for the tests and the experiments
    size_t otherBucket(size_t bucket, uint16_t fp) const { return other(bucket, fp); }
    size_t slots() const { return table_.size(); }
};

// ---------------------------------------------------------------- section 5: the count-min sketch
// d rows of w counters. Each row has its own hash. A key adds to one counter per row; the estimate is the smallest of them.
class CountMinSketch {
    size_t w_, d_;
    std::vector<uint32_t> cells_;
    uint64_t total_ = 0;                                      // N, the sum of all counts added

    size_t column(size_t row, uint64_t key) const { return mix64(key + mix64(row + 1)) % w_; }

public:
    CountMinSketch(size_t width, size_t depth) : w_(width), d_(depth), cells_(width * depth, 0) {}

    static size_t widthFor(double eps) { return (size_t)std::ceil(std::exp(1.0) / eps); }
    static size_t depthFor(double delta) { return (size_t)std::ceil(std::log(1.0 / delta)); }

    void add(uint64_t key, uint32_t count = 1) {
        for (size_t r = 0; r < d_; ++r) cells_[r * w_ + column(r, key)] += count;
        total_ += count;
    }

    uint32_t estimate(uint64_t key) const {
        uint32_t best = UINT32_MAX;
        for (size_t r = 0; r < d_; ++r) best = std::min(best, cells_[r * w_ + column(r, key)]);
        return best;
    }

    void addConservative(uint64_t key, uint32_t count = 1) {  // raise only the cells that are below the new estimate
        uint32_t target = estimate(key) + count;
        for (size_t r = 0; r < d_; ++r) {
            uint32_t& c = cells_[r * w_ + column(r, key)];
            c = std::max(c, target);
        }
        total_ += count;
    }

    uint64_t total() const { return total_; }

    void merge(const CountMinSketch& other) {                 // same width, depth and hashes: add cell by cell
        for (size_t i = 0; i < cells_.size(); ++i) cells_[i] += other.cells_[i];
        total_ += other.total_;
    }
    // ---- for the tests and the experiments
    bool sameCells(const CountMinSketch& o) const { return cells_ == o.cells_ && total_ == o.total_; }
};

// ---------------------------------------------------------------- section 6: HyperLogLog
// m = 2^b registers. A hash chooses a register with its first b bits and has a rank: the position of the first 1 bit
// in the rest. Each register keeps the largest rank it has seen.
class HyperLogLog {
    int b_;
    std::vector<uint8_t> reg_;

public:
    explicit HyperLogLog(int b) : b_(b), reg_(size_t(1) << b, 0) {}   // 4 <= b <= 16

    void add(uint64_t key) {
        uint64_t h = mix64(key);
        size_t index = (size_t)(h >> (64 - b_));              // the first b bits
        uint64_t rest = h << b_;                              // the other 64 - b bits, moved to the top
        int rank = std::min(std::countl_zero(rest), 64 - b_) + 1;   // leading zeros plus one
        reg_[index] = std::max<uint8_t>(reg_[index], (uint8_t)rank);
    }

    static double alpha(size_t m) {                           // corrects the bias of the harmonic mean
        if (m == 16) return 0.673;
        if (m == 32) return 0.697;
        if (m == 64) return 0.709;
        return 0.7213 / (1.0 + 1.079 / (double)m);
    }

    double estimateRaw() const {
        double sum = 0, m = (double)reg_.size();
        for (uint8_t r : reg_) sum += std::ldexp(1.0, -r);    // 2 to the power -r
        return alpha(reg_.size()) * m * m / sum;
    }

    double estimate() const {
        double e = estimateRaw(), m = (double)reg_.size();
        size_t zeros = (size_t)std::count(reg_.begin(), reg_.end(), 0);
        if (e <= 2.5 * m && zeros > 0) return m * std::log(m / (double)zeros);   // few items: linear counting
        return e;
    }

    void merge(const HyperLogLog& other) {                    // the union: the larger register of each pair
        for (size_t i = 0; i < reg_.size(); ++i) reg_[i] = std::max(reg_[i], other.reg_[i]);
    }

    static double stdError(size_t m) { return 1.04 / std::sqrt((double)m); }
    // ---- for the tests and the experiments
    bool sameRegisters(const HyperLogLog& o) const { return reg_ == o.reg_; }
    void fillForTest(uint8_t v) { std::fill(reg_.begin(), reg_.end(), v); }
};

// ---------------------------------------------------------------- section 7: MinHash
// k hash functions (k different seeds). The signature of a set is the smallest hash value under each of them.
class MinHash {
    std::vector<uint64_t> seeds_;

public:
    MinHash(int k, uint64_t seed) {
        Rng rng(seed);
        for (int i = 0; i < k; ++i) seeds_.push_back(rng.next());
    }

    std::vector<uint64_t> signature(const std::vector<uint64_t>& set) const {
        std::vector<uint64_t> sig(seeds_.size(), UINT64_MAX);
        for (uint64_t x : set)
            for (size_t i = 0; i < seeds_.size(); ++i) sig[i] = std::min(sig[i], mix64(x ^ seeds_[i]));
        return sig;
    }
};

// The fraction of positions where two signatures agree estimates the Jaccard similarity of the sets.
double signatureSimilarity(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
    size_t same = 0;
    for (size_t i = 0; i < a.size(); ++i) same += a[i] == b[i];
    return (double)same / (double)a.size();
}

// Banding: cut a signature into bands of r values. Two sets become a candidate pair if any whole band agrees.
bool shareABand(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b, int bands, int rows) {
    for (int band = 0; band < bands; ++band) {
        bool all = true;
        for (int r = 0; r < rows; ++r) all = all && a[band * rows + r] == b[band * rows + r];
        if (all) return true;
    }
    return false;
}

double bandCandidateProb(double s, int bands, int rows) { return 1.0 - std::pow(1.0 - std::pow(s, rows), bands); }

// ================================================================ tests and experiments
// Is a measured rate consistent with the expected one? Six standard deviations of a binomial count, plus 5% for the formula's own approximation.
static bool rateNear(double measured, double expected, double trials) {
    double sd = std::sqrt(expected * (1 - expected) / trials);
    return std::fabs(measured - expected) <= 6 * sd + 0.05 * expected;
}

// Test-only: the integers [from, to) as a set.
static std::vector<uint64_t> interval(uint64_t from, uint64_t to) {
    std::vector<uint64_t> v;
    for (uint64_t x = from; x < to; ++x) v.push_back(x);
    return v;
}

// Test-only: two sets with 'common' shared elements and 'onlyA', 'onlyB' private ones, all starting at 'base'.
static void makeSets(uint64_t base, int common, int onlyA, int onlyB, std::vector<uint64_t>& a, std::vector<uint64_t>& b) {
    a = interval(base, base + common + onlyA);
    b = interval(base, base + common);
    for (int i = 0; i < onlyB; ++i) b.push_back(base + common + onlyA + i);
}

static double exactJaccard(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
    std::set<uint64_t> sa(a.begin(), a.end()), sb(b.begin(), b.end()), both;
    both.insert(sa.begin(), sa.end());
    both.insert(sb.begin(), sb.end());
    size_t inter = 0;
    for (uint64_t x : sa) inter += sb.count(x);
    return (double)inter / (double)both.size();
}

// Test-only: a skewed stream of keys in [0, distinct): a few keys are very frequent, most are rare.
static uint64_t skewed(Rng& rng, uint64_t distinct) { return rng.below(rng.below(rng.below(distinct) + 1) + 1); }

int main() {
    const uint64_t kOutside = 1ULL << 40;                     // keys that are never inserted

    // ---------------------------------------------------------------- Bloom filter
    {
        CHECK(bloomBitsFor(1000, 0.01) == 9586);
        CHECK(bloomHashesFor(10, 100) == 1);                  // 0.1 bits per key would round to 0 hashes: at least 1
        for (uint64_t i = 0; i < 1000; ++i) CHECK(twoHashes(i).h2 & 1);
        BloomFilter tiny(70, 3);                              // every one of the 70 bits can be set, and no other
        for (uint64_t i = 0; i < 2000; ++i) tiny.add(i);
        CHECK(tiny.bitsSet() == 70);
        CHECK(bloomHashesFor(9586, 1000) == 7);
        std::printf("size n=1000 p=0.0100 m=%zu k=%d\n", bloomBitsFor(1000, 0.01), bloomHashesFor(9586, 1000));
        const double expectBits[] = {4.79, 9.59, 14.38, 19.17};
        int idx = 0;
        for (double p : {0.1, 0.01, 0.001, 0.0001}) {
            double perKey = (double)bloomBitsFor(1000000, p) / 1e6;
            CHECK(std::fabs(perKey - expectBits[idx++]) < 0.005);
            std::printf("bitsperkey p=%.4f bits=%.4f\n", p, perKey);
        }

        const size_t n = 10000, probes = 200000;
        auto run = [&](size_t m, int k) {
            BloomFilter f(m, k);
            for (uint64_t i = 0; i < n; ++i) f.add(i);
            for (uint64_t i = 0; i < n; ++i) CHECK(f.mightContain(i));          // never a false negative
            size_t fp = 0;
            for (uint64_t i = 0; i < probes; ++i) fp += f.mightContain(kOutside + i);
            return (double)fp / (double)probes;
        };
        for (size_t bpk : {4, 8, 10, 12, 16}) {
            size_t m = bpk * n;
            int k = bloomHashesFor(m, n);
            double expected = bloomFalsePositive((double)m, k, (double)n), measured = run(m, k);
            CHECK(rateNear(measured, expected, (double)probes));
            std::printf("bloom bits/key=%zu k=%d expected=%.4f measured=%.4f\n", bpk, k, expected, measured);
        }
        double best = 1;                                      // the effect of k at 10 bits per key
        double atSeven = 0;
        for (int k : {1, 3, 7, 10, 14}) {
            double expected = bloomFalsePositive(10.0 * n, k, (double)n), measured = run(10 * n, k);
            CHECK(rateNear(measured, expected, (double)probes));
            if (k == 7) atSeven = measured;
            best = std::min(best, measured);
            std::printf("bloom-k bits/key=10 k=%d expected=%.4f measured=%.4f\n", k, expected, measured);
        }
        CHECK(atSeven == best);

        BloomFilter f(10 * n, 7);
        for (uint64_t i = 0; i < n; ++i) f.add(i);
        double fill = f.fillRatio(), est = f.estimateCount();
        CHECK(fill > 0.48 && fill < 0.52);
        CHECK(std::fabs(est - (double)n) < 0.02 * (double)n);
        std::printf("bloom fill=%.4f estimate=%.4f\n", fill, est);

        BloomFilter a(10 * n, 7), b(10 * n, 7), both(10 * n, 7);       // union by OR
        for (uint64_t i = 0; i < 6000; ++i) { a.add(i); both.add(i); }
        for (uint64_t i = 4000; i < n; ++i) { b.add(i); both.add(i); }
        a.merge(b);
        CHECK(a.sameBits(both));
        for (uint64_t i = 0; i < n; ++i) CHECK(a.mightContain(i));
    }

    // ---------------------------------------------------------------- counting Bloom filter
    {
        const size_t n = 10000;
        CountingBloom cb(10 * n, 7);
        for (uint64_t i = 0; i < n; ++i) cb.add(i);
        for (uint64_t i = 0; i < n; ++i) CHECK(cb.mightContain(i));
        for (uint64_t i = 1; i < n; i += 2) CHECK(cb.remove(i));       // remove the odd keys
        for (uint64_t i = 0; i < n; i += 2) CHECK(cb.mightContain(i));  // the even keys must all still be there
        size_t stale = 0;
        for (uint64_t i = 1; i < n; i += 2) stale += cb.mightContain(i);
        CHECK(stale <= 15);
        const size_t probes = 400000;
        size_t fp = 0;
        for (uint64_t i = 0; i < probes; ++i) fp += cb.mightContain(kOutside + i);
        double expected = bloomFalsePositive(10.0 * n, 7, n / 2.0), measured = (double)fp / (double)probes;
        CHECK(rateNear(measured, expected, (double)probes));
        std::printf("counting removed-still-present=%zu of 5000, fp expected=%.5f measured=%.5f\n", stale, expected, measured);

        CountingBloom s(1000, 3);                             // a counter that reached its maximum never comes down again
        for (int i = 0; i < 20; ++i) s.add(5);
        for (int i = 0; i < 20; ++i) CHECK(s.remove(5));
        CHECK(s.mightContain(5));
        CountingBloom big(1000, 3);                           // 256 adds must not wrap a counter around to 0
        for (int i = 0; i < 256; ++i) big.add(7);
        CHECK(big.mightContain(7));
        CountingBloom t(1000, 3);                             // below the maximum, add and remove cancel exactly
        for (int i = 0; i < 10; ++i) t.add(6);
        for (int i = 0; i < 10; ++i) CHECK(t.remove(6));
        CHECK(!t.mightContain(6));
        CHECK(!t.remove(6));                                  // removing an absent key is refused
    }

    // ---------------------------------------------------------------- cuckoo filter
    {
        const size_t buckets = 1 << 14;
        const size_t probes = 1000000;
        for (int f : {8, 12, 16}) {
            CuckooFilter cf(buckets, f);
            uint64_t next = 0;
            for (; next < 50000; ++next) CHECK(cf.add(next));
            while (cf.add(next)) ++next;                      // go on until the first insert fails
            for (uint64_t i = 0; i < 50000; ++i) CHECK(cf.mightContain(i));
            bool rest = true;
            for (uint64_t i = 50000; i < next; ++i) rest = rest && cf.mightContain(i);
            CHECK(rest);                                      // the failed insert lost nothing
            double load = cf.load();
            CHECK(load >= 0.9);
            size_t fp = 0;
            for (uint64_t i = 0; i < probes; ++i) fp += cf.mightContain(kOutside + i);
            double expected = 1.0 - std::pow(1.0 - std::pow(2.0, -f), 8 * load), measured = (double)fp / (double)probes;
            CHECK(rateNear(measured, expected, (double)probes));
            std::printf("cuckoo f=%d load=%.4f bits/key=%.4f expected=%.6f measured=%.6f\n", f, load, f / load, expected, measured);
            bool removedAll = true;
            for (uint64_t i = 0; i < next; ++i) removedAll = removedAll && cf.remove(i);
            CHECK(removedAll);
            CHECK(cf.size() == 0);
        }

        CuckooFilter small(16, 12);
        Rng rng(5);
        for (int i = 0; i < 10000; ++i) {
            size_t bucket = rng.below(16);
            uint16_t fp = (uint16_t)(1 + rng.below(4095));
            CHECK(small.otherBucket(small.otherBucket(bucket, fp), fp) == bucket);   // xor twice comes back
        }

        CuckooFilter dup(1 << 10, 12);                        // the same key again and again fills only its two buckets
        for (uint64_t i = 0; i < 1000; ++i) CHECK(dup.add(i));
        int accepted = 0;
        for (int i = 0; i < 20; ++i) accepted += dup.add(kOutside);
        CHECK(accepted >= 4 && accepted <= 8);
        for (uint64_t i = 0; i < 1000; ++i) CHECK(dup.mightContain(i));
        std::printf("cuckoo duplicates accepted=%d of 20\n", accepted);

        CuckooFilter tiny(16, 4);                             // removing a key that was never added can erase another key
        tiny.add(1);
        uint64_t other = 2;
        while (!tiny.mightContain(other)) ++other;            // a false positive
        CHECK(tiny.remove(other));
        CHECK(!tiny.mightContain(1));
    }

    // ---------------------------------------------------------------- count-min sketch
    {
        CHECK(CountMinSketch::widthFor(0.01) == 272);
        CHECK(CountMinSketch::widthFor(0.001) == 2719);
        CHECK(CountMinSketch::widthFor(0.0001) == 27183);
        CHECK(CountMinSketch::depthFor(0.1) == 3);
        CHECK(CountMinSketch::depthFor(0.01) == 5);
        CHECK(CountMinSketch::depthFor(0.001) == 7);
        CHECK(CountMinSketch::depthFor(0.000001) == 14);

        const uint64_t distinct = 20000;
        const size_t N = 300000;
        Rng rng(777);
        std::vector<uint64_t> stream(N);
        std::vector<uint32_t> truth(distinct, 0);
        for (uint64_t& key : stream) { key = skewed(rng, distinct); ++truth[key]; }

        size_t w = CountMinSketch::widthFor(0.001), d = CountMinSketch::depthFor(0.01);
        CountMinSketch plain(w, d), conservative(w, d);
        for (uint64_t key : stream) { plain.add(key); conservative.addConservative(key); }
        CHECK(plain.total() == N);
        CHECK(conservative.total() == N);

        const double bound = 0.001 * (double)N;
        double plainSum = 0, consSum = 0, lightPlain = 0;
        uint32_t plainMax = 0, consMax = 0;
        size_t over = 0, light = 0, heaviest = 0;
        for (uint64_t key = 0; key < distinct; ++key) {
            uint32_t t = truth[key], p = plain.estimate(key), c = conservative.estimate(key);
            CHECK(p >= t);                                    // a count-min sketch never under-counts
            CHECK(c >= t);
            CHECK(c <= p);                                    // conservative update is never worse
            plainSum += p - t;
            consSum += c - t;
            plainMax = std::max(plainMax, p - t);
            consMax = std::max(consMax, c - t);
            over += (p - t) > bound;
            if (t <= 10) { lightPlain += p - t; ++light; }
            if (t > truth[heaviest]) heaviest = key;
        }
        CHECK((double)over / distinct <= 0.01);
        CHECK(plainMax <= bound);
        std::printf("cm N=%zu w=%zu d=%zu bound=%.1f plain: max=%u mean=%.4f over=%zu light-mean=%.4f (%zu light keys)\n",
                    N, w, d, bound, plainMax, plainSum / distinct, over, lightPlain / (double)light, light);
        std::printf("cm conservative: max=%u mean=%.4f\n", consMax, consSum / distinct);
        std::printf("cm heaviest key=%zu true=%u estimate=%u\n", heaviest, truth[heaviest], plain.estimate(heaviest));
        CHECK(consSum < plainSum);

        CountMinSketch two(2, 1);                             // both columns of a width-2 sketch are used
        for (uint64_t key = 0; key < 1000; ++key) two.add(key);
        uint32_t lowest = UINT32_MAX;
        for (uint64_t key = 0; key < 1000; ++key) lowest = std::min(lowest, two.estimate(key));
        CHECK(lowest < 700);

        CountMinSketch left(w, d), right(w, d);               // merging two sketches equals sketching the whole stream
        for (size_t i = 0; i < N; ++i) (i % 3 == 0 ? left : right).add(stream[i]);
        left.merge(right);
        CHECK(left.sameCells(plain));
    }

    // ---------------------------------------------------------------- HyperLogLog
    {
        auto run = [&](int b, uint64_t n, int trials) {
            double sq = 0, sum = 0, se = HyperLogLog::stdError(size_t(1) << b);
            for (int t = 0; t < trials; ++t) {
                HyperLogLog h(b);
                for (uint64_t i = 0; i < n; ++i) h.add(((uint64_t)t << 40) | i);
                double rel = (h.estimate() - (double)n) / (double)n;
                sq += rel * rel;
                sum += rel;
            }
            double rms = std::sqrt(sq / trials), bias = sum / trials;
            CHECK(rms <= 2 * se);
            CHECK(std::fabs(bias) <= 4 * se / std::sqrt((double)trials) + 0.002);
            std::printf("hll b=%d m=%d n=%llu trials=%d rms=%.4f bias=%.4f stdErr=%.4f\n", b, 1 << b,
                        (unsigned long long)n, trials, rms, bias, se);
        };
        for (uint64_t n : {10ULL, 100ULL, 1000ULL, 10000ULL}) run(10, n, 50);
        run(10, 100000, 20);
        run(10, 1000000, 8);
        for (uint64_t n : {100ULL, 1000ULL, 10000ULL}) run(14, n, 50);
        run(14, 100000, 20);
        run(14, 1000000, 8);

        CHECK(HyperLogLog(10).estimate() == 0);
        CHECK(HyperLogLog::alpha(16) == 0.673 && HyperLogLog::alpha(32) == 0.697 && HyperLogLog::alpha(64) == 0.709);
        CHECK(std::fabs(HyperLogLog::alpha(1024) - 0.7205) < 0.00005);
        CHECK(std::fabs(HyperLogLog::alpha(16384) - 0.72125) < 0.00001);
        CHECK(std::fabs(HyperLogLog::stdError(16384) - 0.0081) < 0.00005);
        std::printf("hll stdError m=16384 %.4f, 6 bits x m = %d bytes\n", HyperLogLog::stdError(16384), 16384 * 6 / 8);

        HyperLogLog mid(10);                                  // between 2.5 m and 5 m the raw estimate is used, not linear counting
        for (uint64_t i = 0; i < 3000; ++i) mid.add(i);
        CHECK(mid.estimateRaw() > 2.5 * 1024);
        CHECK(mid.estimate() == mid.estimateRaw());
        HyperLogLog low(10);
        for (uint64_t i = 0; i < 1000; ++i) low.add(i);
        CHECK(low.estimateRaw() <= 2.5 * 1024);
        CHECK(low.estimate() != low.estimateRaw());
        HyperLogLog ones(10);                                 // no empty register: never divide by zero
        ones.fillForTest(1);
        CHECK(ones.estimate() == ones.estimateRaw());
        std::printf("hll b=10 n=3000 raw=%.4f, n=1000 raw=%.4f corrected=%.4f\n", mid.estimateRaw(), low.estimateRaw(), low.estimate());

        HyperLogLog small(14);                                // few items: the raw formula is far off, linear counting is not
        for (uint64_t i = 0; i < 100; ++i) small.add(i);
        CHECK(small.estimateRaw() > 10000);
        CHECK(std::fabs(small.estimate() - 100) < 5);
        std::printf("hll b=14 n=100 raw=%.1f corrected=%.4f\n", small.estimateRaw(), small.estimate());

        HyperLogLog once(12), twice(12);                      // adding a key again changes nothing
        for (uint64_t i = 0; i < 20000; ++i) once.add(i);
        for (int rep = 0; rep < 2; ++rep)
            for (uint64_t i = 0; i < 20000; ++i) twice.add(i);
        CHECK(once.sameRegisters(twice));

        HyperLogLog a(12), b2(12), all(12);                   // merging equals counting the union
        for (uint64_t i = 0; i < 60000; ++i) { a.add(i); all.add(i); }
        for (uint64_t i = 30000; i < 90000; ++i) { b2.add(i); all.add(i); }
        a.merge(b2);
        CHECK(a.sameRegisters(all));
        double ratio = a.estimate() / 90000.0;
        CHECK(std::fabs(ratio - 1) < 3 * HyperLogLog::stdError(4096));
        std::printf("hll union b=12 estimate/90000=%.4f\n", ratio);
    }

    // ---------------------------------------------------------------- MinHash
    {
        MinHash mh(128, 1);
        std::vector<uint64_t> a = interval(0, 200), b = interval(1000, 1200);
        CHECK(signatureSimilarity(mh.signature(a), mh.signature(a)) == 1.0);
        CHECK(signatureSimilarity(mh.signature(a), mh.signature(b)) == 0.0);
        std::vector<uint64_t> sa, sb;
        makeSets(0, 100, 50, 50, sa, sb);
        CHECK(exactJaccard(sa, sb) == 0.5);

        for (int k : {16, 64, 256, 1024}) {
            MinHash h(k, 99);
            const int trials = 100;
            double sq = 0, sum = 0;
            for (int t = 0; t < trials; ++t) {
                makeSets((uint64_t)(t + 1) * 1000000, 100, 50, 50, sa, sb);
                double est = signatureSimilarity(h.signature(sa), h.signature(sb));
                sq += (est - 0.5) * (est - 0.5);
                sum += est - 0.5;
            }
            double rms = std::sqrt(sq / trials), expected = std::sqrt(0.25 / k);
            CHECK(rms > 0.7 * expected && rms < 1.3 * expected);
            CHECK(std::fabs(sum / trials) < 4 * expected / std::sqrt((double)trials));
            std::printf("minhash J=0.5000 k=%d rms=%.4f expected=%.4f\n", k, rms, expected);
        }

        MinHash lsh(100, 7);                                  // 20 bands of 5 values
        struct Case { int common, only; };
        for (Case c : {Case{80, 10}, Case{60, 30}, Case{50, 100}}) {
            double J = c.common / (double)(c.common + 2 * c.only);
            const int pairs = 500;
            int hits = 0;
            for (int t = 0; t < pairs; ++t) {
                makeSets((uint64_t)(t + 1) * 1000000, c.common, c.only, c.only, sa, sb);
                hits += shareABand(lsh.signature(sa), lsh.signature(sb), 20, 5);
            }
            double expected = bandCandidateProb(J, 20, 5), measured = (double)hits / pairs;
            CHECK(rateNear(measured, expected, pairs));
            std::printf("lsh J=%.4f bands=20 rows=5 expected=%.4f measured=%.4f\n", J, expected, measured);
        }
        CHECK(std::fabs(bandCandidateProb(0.8, 20, 5) - 0.9996) < 0.00005);
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
