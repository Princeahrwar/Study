# DSA 02.04: Probabilistic Structures

*Part of the [Hashing plan](00_plan.md) · Time: about 6 hours (2.5 reading, 3.5 problems) · You need: tutorial [01](01_hash_tables_inside.md) (hash functions, `mix64`) and [03](03_design_a_hash_map.md) (cuckoo hashing) · Companion code: [`04_probabilistic_structures_demo.cpp`](04_probabilistic_structures_demo.cpp)*

By the end of this tutorial you can:

- say what question each of six structures answers (Bloom filter, counting Bloom filter, cuckoo filter, count-min sketch, HyperLogLog, MinHash), which way it errs, and which ones merge;
- size a Bloom filter from the number of keys and the false-positive rate you accept, and say where the formula comes from;
- explain how a cuckoo filter finds a fingerprint's second bucket without the key, and why deleting a key you never inserted is dangerous;
- choose the width and depth of a count-min sketch from an error and a failure probability;
- explain how HyperLogLog turns leading zeros into a count, and what 12 KB buys;
- estimate set similarity with MinHash and find similar pairs with banding;
- pick a structure from a requirement, and say what it costs when it is wrong.

The classes are in the companion file. It checks the guarantees (a Bloom or cuckoo filter never forgets a key it holds, a count-min sketch never under-counts), compares each measured error with its formula, and prints every number quoted below. It compiles with `-Wall -Wextra -fsanitize=address,undefined`. The code is teaching code with 64-bit keys, and the measurements describe it, on one machine.

---

## 1. Why give up exactness?

An exact set of one billion 8-byte keys takes 8 GB. A Bloom filter that answers "is this key in the set?" and is wrong on 1% of the keys that are not in the set takes 9.59 bits per key: 1.2 GB (section 2.3). A HyperLogLog that counts the distinct keys of any stream to within about 1% takes 12 KB (section 6). The saving is paid for with a wrong answer now and then, and the useful part is that **the error is a number you can compute before you build the structure**.

Most uses put the structure in front of something expensive (a disk read, a network call, a database query). A wrong "yes" then costs one wasted access, while a wrong "no" would lose data. So the structures are built to be wrong in one direction only.

| Structure | Question | Wrong how | Memory | Delete / merge |
|---|---|---|---|---|
| Bloom filter | is `x` in the set? | false "yes" with probability `p`; never a false "no" | 1.44 log2(1/p) bits per key | no delete; merge by OR |
| Counting Bloom filter | same, and remove `x` | same | 3 to 4 times a Bloom filter | delete; add counters to merge |
| Cuckoo filter | same, and remove `x` | same | about (log2(1/p) + 3) / 0.95 bits per key | delete (keys that were inserted) |
| Count-min sketch | how often did `x` occur? | too high, by at most εN with probability 1 - δ; never too low | w × d counters, w = ⌈e/ε⌉, d = ⌈ln(1/δ)⌉ | merge by adding |
| HyperLogLog | how many distinct items? | relative error about 1.04 / √m either way | m registers of 6 bits | merge by max |
| MinHash | how similar are sets A and B? | error about √(J(1 - J)/k) either way | k hash values per set | none |

All six use what tutorial 01 built: a good 64-bit hash (`mix64`), several hash values per key made from one, and hashed positions. What is new is the probability: each error below comes from a short calculation.

---

## 2. The Bloom filter

### 2.1 The idea

Take an array of `m` bits, all 0, and `k` hash functions. To **add** a key, set the `k` bits its hashes point to. To **ask** about a key, look at its `k` bits: if any is 0 the key was never added (adding it would have set that bit), and if all are 1 the key was probably added. The "probably" is the catch, because other keys may have set those same bits.

A picture with `m = 12` and `k = 3`. Suppose the hashes put `apple` on bits 1, 5, 9 and `pear` on bits 2, 5, 10:

```
bit:    0 1 2 3 4 5 6 7 8 9 10 11
        0 1 1 0 0 1 0 0 0 1 1  0
```

- `fig` hashes to 1, 4, 9. Bit 4 is 0, so `fig` is **definitely not** in the set.
- `plum` hashes to 2, 9, 10. All three are 1 (set by `pear` and `apple`), so the filter says "present". `plum` was never added: a **false positive**.

There is no false negative: adding a key sets all its bits and a bit is never cleared. And nothing can be deleted: clearing a bit of `apple` could clear a bit that `pear` needs. Section 3 fixes that.

### 2.2 The code

Running `k` independent hash functions is slow. **Double hashing** makes all `k` positions from two hash values: position `i` is `h1 + i × h2`. Kirsch and Mitzenmacher showed that this costs almost nothing in false positives, and the measurements in 2.4 agree.

```cpp
// k bit positions from one key, using only two hashes: position i is h1 + i * h2 (double hashing).
struct TwoHashes { uint64_t h1, h2; };

TwoHashes twoHashes(uint64_t key) {
    uint64_t h = mix64(key);
    return {h, mix64(h) | 1};                                 // the second hash is made odd so that it never becomes 0
}
```

The second hash is made odd so that it is never 0. The filter:

```cpp
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
    // (helper functions for the tests are in the companion file)
};
```

`position` reduces modulo `m`, so `m` may be any number, not just a power of two. `mightContain` stops at the first 0 bit. `merge` is the union of two sets when both filters have the same `m`, `k` and hash.

### 2.3 The arithmetic

After `n` keys, `kn` bits were set (some more than once). A given bit is still 0 with probability (1 - 1/m)^(kn), which is about e^(-kn/m). A key that was never added passes only if all its `k` bits are 1, so

> p ≈ (1 - e^(-kn/m))^k

(This treats the bits as independent, so it is slightly optimistic; at the sizes below the difference is invisible.)

**Choosing k.** Write ρ = e^(-kn/m) for the fraction of zero bits. Then p = (1 - ρ)^k and k = -(m/n) ln ρ, so p = exp(-(m/n) ln ρ ln(1 - ρ)). That is smallest when ρ = 1/2, where the two logarithms are equal. So the best filter is **half full of ones**, and

> k = (m/n) ln 2,    p = 2^(-k) = e^(-(m/n)(ln 2)²),    m = -n ln p / (ln 2)²

Bits per key are -ln p / (ln 2)², which is 1.44 × log2(1/p). Every factor 10 in `p` costs 4.79 bits per key. For `n = 1000` and `p = 1%`: `m = 9586` bits and `k = 7`.

```cpp
// The sizing formulas, written as code.
double bloomFalsePositive(double m, double k, double n) { return std::pow(1.0 - std::exp(-k * n / m), k); }

size_t bloomBitsFor(size_t n, double p) { return (size_t)std::ceil(-(double)n * std::log(p) / (std::log(2.0) * std::log(2.0))); }

int bloomHashesFor(size_t m, size_t n) { return std::max(1, (int)std::lround((double)m / (double)n * std::log(2.0))); }
```

| p | bits per key | k |
|---|---|---|
| 10% | 4.79 | 3 |
| 1% | 9.59 | 7 |
| 0.1% | 14.38 | 10 |
| 0.01% | 19.17 | 13 |

(`k` is `log2(1/p)` rounded.) The known lower bound for any structure that answers this question with false-positive rate `p` is log2(1/p) bits per key, so a Bloom filter pays 44% more than it must; section 4 shows a structure that gets closer.

### 2.4 Measured

10 000 keys inserted, then 200 000 keys that were never inserted asked about. "Expected" is the formula; "measured" is the fraction of those that the filter accepted.

| bits per key | k | expected | measured |
|---|---|---|---|
| 4 | 3 | 0.1469 | 0.1473 |
| 8 | 6 | 0.0216 | 0.0215 |
| 10 | 7 | 0.0082 | 0.0082 |
| 12 | 8 | 0.0031 | 0.0032 |
| 16 | 11 | 0.0005 | 0.0005 |

The formula and the double-hashed filter agree to the printed digits. The best `k` matters: at 10 bits per key,

| k | 1 | 3 | 7 | 10 | 14 |
|---|---|---|---|---|---|
| expected | 0.0952 | 0.0174 | 0.0082 | 0.0102 | 0.0190 |
| measured | 0.0970 | 0.0172 | 0.0082 | 0.0099 | 0.0187 |

Too few hashes and a single collision lets a stranger through; too many and the array fills with ones. With 7 hashes the filter is 0.5045 ones, as the half-full rule predicts.

### 2.5 What else the bits tell you

- **How many keys are in it.** If `X` bits are set, the expected value of `X` is m(1 - e^(-kn/m)), so n ≈ -(m/k) ln(1 - X/m). With 10 000 keys the estimate was 10030.68 (0.3% high).
- **Union.** OR two filters of the same shape: the result is bit for bit the filter built from the union (the demo checks it).
- **The size is fixed.** Add more than the planned `n` and `p` climbs. Size for the largest plausible `n`, or rebuild.
- **Cache behaviour.** `k` random positions are `k` cache misses. A *blocked* filter puts all `k` bits of a key in one cache line, for one miss per lookup and a slightly higher `p`. RocksDB's filter works this way; its wiki gives an example of about 0.95% against 0.84% for a standard filter.

---

## 3. The counting Bloom filter

Replace each bit by a small counter. `add` increments the `k` counters, `remove` decrements them, and a key is present if all its counters are above 0. The class `CountingBloom` in the companion file is `BloomFilter` with a counter per position, and three details carry its correctness:

- **A counter at its maximum stays there.** A 4-bit counter cannot count past 15. If it wrapped to 0, a key that is present would look absent: a false negative. A saturated counter is never decremented either, since its true value is unknown. A key added 20 times and removed 20 times is therefore still "present", which is the price of staying safe.
- **Remove only keys that were added.** `remove` refuses a key with a zero counter, but a false positive passes that test and shares counters with real keys. Removing it damages them.
- **One counter per byte here.** Real code packs two 4-bit counters into a byte: 4 bits per position against 1, so 3 to 4 times a Bloom filter (the range Wikipedia gives for 3- and 4-bit counters).

Measured with 10 000 keys in 100 000 counters and `k = 7`: after removing the 5000 odd keys, all 5000 even keys are still present and 0 of the removed keys still show. For keys never added the rate is 0.00021, against 0.00020 from the formula with `n = 5000`. Section 4 gets delete for less memory.

---
## 4. The cuckoo filter

### 4.1 The idea

A cuckoo hash table (tutorial 03, section 3) in which the key is replaced by a short **fingerprint** of `f` bits. Each key has two buckets of four slots. To look a key up, compute its fingerprint and look for it in both buckets. To insert, put the fingerprint in a free slot of either bucket; if both are full, evict a resident and move it to its other bucket, and so on.

There is one problem. An evicted fingerprint is all that is left of its key, so how does it find its other bucket? **Partial-key cuckoo hashing** answers it:

> bucket 2 = bucket 1 xor hash(fingerprint)

XOR is its own inverse, so from either bucket and the fingerprint alone you get the other bucket. The key is not needed.

A trace with four buckets of one slot each, and a made-up `hash(fp) = fp mod 4`, so `other = bucket xor (fp mod 4)`. Keys are shown as (first bucket, fingerprint):

| key | first bucket | fingerprint | other bucket |
|---|---|---|---|
| A | 0 | 1 | 1 |
| B | 0 | 2 | 2 |
| C | 3 | 3 | 0 |
| D | 2 | 5 | 3 |

- Insert A: bucket 0 is free. Table `[1, -, -, -]`.
- Insert B: bucket 0 is full, bucket 2 is free. Table `[1, -, 2, -]`.
- Insert C: bucket 3 is free. Table `[1, -, 2, 3]`.
- Insert D: buckets 2 and 3 are both full. **Kick**: D's fingerprint 5 takes bucket 2 and B's fingerprint 2 is evicted. Its other bucket is 2 xor 2 = 0, which holds A's fingerprint 1: 2 takes bucket 0 and 1 is evicted. Its other bucket is 0 xor 1 = 1, which is free. Table `[2, 1, 5, 3]`.

### 4.2 The code

```cpp
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
    // (helper functions for the tests are in the companion file)
};
```

- A fingerprint is never 0, because 0 marks an empty slot (`probe` turns 0 into 1).
- `add` tries both buckets. If both are full it evicts a random resident, sends it to its other bucket, and repeats up to 500 times. **If it gives up it undoes every swap**, using the recorded `path`. A version that just returns `false` has already moved fingerprints around and one is homeless: a false negative. This one leaves the filter as it found it, and the caller builds a bigger filter.
- The number of buckets is a power of two, so `& mask_` is the modulo and the XOR trick stays inside the table.

### 4.3 Load, false positives and bits per key

A key that was never added passes if any of the `2b` slots it looks at holds its fingerprint. Each occupied slot matches with probability 2^(-f), and about 2bα of them are occupied at load α, so

> p ≈ 1 - (1 - 2^(-f))^(2bα) ≈ 2bα / 2^f

and a target `p` needs f ≥ log2(2b/p), with b = 4. The cost per key is f/α bits. Measured with 16 384 buckets of four slots, inserting until the first insert fails, then asking about 1 000 000 keys never added:

| f | load at first failure | bits per key | expected p | measured p |
|---|---|---|---|---|
| 8 | 0.9648 | 8.29 | 0.029757 | 0.030139 |
| 12 | 0.9660 | 12.42 | 0.001885 | 0.001959 |
| 16 | 0.9648 | 16.58 | 0.000118 | 0.000121 |

The load is about 96%, a little above the "about 95%" the cuckoo filter paper reports for four slots per bucket, and it hardly depends on `f`. The measured `p` is 1% to 4% above the formula in all three rows. The sampling noise of 1 000 000 probes explains part of that, and I did not look for the rest.

**Against a Bloom filter**, using the paper's 95% load and the smallest `f` that reaches `p`:

| p | Bloom bits per key | cuckoo f | cuckoo bits per key |
|---|---|---|---|
| 1% | 9.59 | 10 | 10.53 |
| 0.1% | 14.38 | 13 | 13.68 |
| 0.01% | 19.17 | 17 | 17.89 |

Setting 1.44 L = (L + 3) / 0.95 with L = log2(1/p) gives p of about 0.4%, where the two cost the same. Below that the cuckoo filter is smaller, because its cost grows with log2(1/p) plus a constant 3, while the Bloom filter's grows 1.44 times as fast. The paper reports that its *semi-sorted* variant (which packs the sorted fingerprints of a bucket) beats a space-optimised Bloom filter for p below 3%; this class does not do that.

### 4.4 Three rules

- **Delete only keys you inserted.** Removing a key that is a false positive erases the fingerprint of a real key. In the demo (16 buckets, 4-bit fingerprints), after adding key 1 the code finds another key that passes, removes it, and key 1 is gone.
- **A key can be added at most 2b times.** All its copies have the same fingerprint and the same two buckets: 8 slots. In the demo, 20 adds of one key are accepted 8 times.
- **The size is fixed.** Only fingerprints are stored, so the table cannot be rehashed into more buckets without the keys. A full filter is rebuilt from the source data.

---

## 5. The count-min sketch

### 5.1 The idea

`d` rows of `w` counters, with a different hash for each row. To **add** a key, increment its counter in every row. To **estimate** a key, take the **smallest** of its `d` counters. Every counter holds the true count of the key plus the counts of other keys that landed in the same cell, so every counter is at least the true count, and the least polluted row is the best answer. The estimate can be too high, never too low.

An example with `d = 3` rows and `w = 5` columns. The stream is `a b a c a b`, so `a` occurs 3 times, `b` twice and `c` once. The hashes put `a` in columns (1, 0, 2), `b` in (3, 0, 4) and `c` in (1, 0, 3), one column per row:

```
row 1:  0 4 0 2 0      a and c share column 1
row 2:  6 0 0 0 0      all three share column 0
row 3:  0 0 3 1 2
```

- `a`: min(4, 6, 3) = 3. `b`: min(2, 6, 2) = 2. `c`: min(4, 6, 1) = 1. All exact, because each key has one row with no collision.
- A key `d` that never occurred, hashed to columns (1, 0, 2), reads min(4, 6, 3) = 3. Over-counted, since its three cells all hold other keys.

### 5.2 The code

```cpp
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
    // (helper functions for the tests are in the companion file)
};
```

`addConservative` is a variant: it raises only the cells that are below the new estimate. It still never under-counts, and it adds less noise (5.4). The price is that the sketch is no longer a linear function of the stream, so you cannot subtract a stream or expect merged sketches to match a sketch of the combined stream.

### 5.3 Choosing the width and depth

In one row, the counter for a key `x` holds the count of `x` plus the counts of the others that collide with it. Each other key collides with probability 1/w, so the expected extra is at most N/w, where N is the total number of increments. By Markov's inequality, the extra exceeds e × N/w with probability at most 1/e. Choose w = ⌈e/ε⌉ so that e × N/w ≤ εN. The `d` rows use independent hashes, so all of them are bad with probability at most e^(-d), and d = ⌈ln(1/δ)⌉ makes that at most δ. So:

> true count ≤ estimate ≤ true count + εN, with probability at least 1 - δ

| ε | 0.01 | 0.001 | 0.0001 |
|---|---|---|---|
| width ⌈e/ε⌉ | 272 | 2719 | 27183 |

| δ | 0.1 | 0.01 | 0.001 | 0.000001 |
|---|---|---|---|---|
| depth ⌈ln(1/δ)⌉ | 3 | 5 | 7 | 14 |

Halving ε doubles the memory. Squaring δ only doubles the depth. The memory does not depend on the number of distinct keys.

### 5.4 Measured

A stream of N = 300 000 keys drawn from 20 000 possible keys, skewed so that a few are very frequent. The sketch has ε = 0.001, so w = 2719, and δ = 0.01, so d = 5: 2719 × 5 × 4 bytes = 54 380 bytes. The guarantee allows an error of εN = 300.

| | plain | conservative |
|---|---|---|
| largest error over all 20 000 keys | 190 | 96 |
| mean error | 28.58 | 14.29 |
| keys with error above 300 | 0 | |

Every estimate was at least the true count. The most frequent key occurred 796 times and was estimated at 836 (5% high). The 13 901 keys that occurred 10 times or fewer (some never) had a mean error of 28.61, the same as everyone else, which is a large error on a count of 1 to 10. **A count-min sketch finds the heavy keys. It cannot count the rare ones.**

Two sketches with the same shape and hashes **merge by adding their cells**, and the result is the sketch of the combined stream (the demo checks the cells are identical), which suits counting spread over machines. To find the most frequent keys, keep a small heap beside the sketch: on each item, update the sketch, read the estimate, and keep the key if it is large enough.

---
## 6. HyperLogLog

### 6.1 The idea

How many different items are in a stream, when the stream is too big to remember? Hash every item to a random 64-bit string. The chance that a hash starts with `r - 1` zeros and then a 1 is 2^(-r). Call `r` the **rank** (leading zeros plus one). Among `n` different items the largest rank is about log2(n): a rank of 20 was probably seen after about a million items. The same item always has the same hash, so repeats change nothing.

One maximum is a noisy estimate (a single lucky hash doubles it). So split the items into `m = 2^b` groups by the first `b` bits of the hash, keep the largest rank in each group (a **register**), and combine the `m` registers. A trace with `b = 2`, so four registers, and 8-bit hashes:

| hash | register (first 2 bits) | rank of the other 6 bits | register after |
|---|---|---|---|
| `10 010110` | 2 | 2 | `[0, 0, 2, 0]` |
| `01 000101` | 1 | 4 | `[0, 4, 2, 0]` |
| `00 100000` | 0 | 1 | `[1, 4, 2, 0]` |
| `11 000001` | 3 | 6 | `[1, 4, 2, 6]` |
| `01 001100` | 1 | 3 | `[1, 4, 2, 6]` (4 is larger than 3) |

### 6.2 The code

```cpp
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
    // (helper functions for the tests are in the companion file)
};
```

The register index and the rank use **different bits** of the hash: the first `b` bits choose the register, and `h << b_` moves the other `64 - b` bits to the top before counting zeros. `min(..., 64 - b_)` stops the count when the remaining bits are all zero.

### 6.3 The estimate

Register `j` holding `M[j]` suggests about 2^M[j] items in its group. The estimate combines them with the **harmonic mean**, which is dominated by small values and so is not thrown off by one lucky register:

> E = α_m × m² / Σ 2^(-M[j]),    α_m = 0.7213 / (1 + 1.079/m) for m ≥ 128

α_m corrects the bias of the harmonic mean. For m = 16, 32, 64 the constants are 0.673, 0.697 and 0.709. Two examples with `m = 16`:

- Registers `4 3 5 4 4 6 3 4 5 4 3 4 4 5 4 3`: the sum of 2^(-M) is 1.109375, so E = 0.673 × 256 / 1.109375 = 155.30.
- Registers `1 0 2 0 1 3 0 1 2 1 0 1 2 0 1 1`: the sum is 9.375 and the raw E = 18.38. That is at most 2.5 m = 40 and there are 5 empty registers, so the next rule applies.

**Few items: linear counting.** With `n` items, a register is empty with probability e^(-n/m), so about `V = m e^(-n/m)` registers are empty and n ≈ m ln(m/V). The code uses this when E ≤ 2.5 m and V > 0. In the second example it gives 16 ln(16/5) = 18.61. The raw formula is badly wrong there:

- `b = 10`, 1000 items: raw 1344.12 (34% high), corrected 1020.50;
- `b = 14`, 100 items: raw 11864.4, corrected 100.31 (with every register empty the raw formula returns about 0.72 m, whatever n is);
- `b = 10`, 3000 items: the raw estimate is 2977.59, above 2.5 m = 2560, so it is used as it is.

**Very many items.** With a 32-bit hash the estimate saturates near 2^32 and needs a third correction. With 64 bits there is nothing to correct at any realistic size, which is why this class does not have one.

**Error.** The relative standard error is 1.04 / √m. Measured, with the rms of the relative error over several trials of `n` different keys:

| b = 10 (m = 1024), 1.04/√m = 0.0325 | n = 10 | 1000 | 100 000 | 1 000 000 |
|---|---|---|---|---|
| trials | 50 | 50 | 20 | 8 |
| rms error | 0.0276 | 0.0259 | 0.0282 | 0.0368 |
| mean error | -0.0032 | -0.0012 | 0.0068 | -0.0049 |

| b = 14 (m = 16 384), 1.04/√m = 0.0081 | n = 100 | 10 000 | 100 000 | 1 000 000 |
|---|---|---|---|---|
| trials | 50 | 50 | 20 | 8 |
| rms error | 0.0054 | 0.0059 | 0.0065 | 0.0044 |
| mean error | -0.0010 | 0.0009 | -0.0003 | -0.0036 |

The error does not grow with `n`, and it falls as √m: 16 times the registers, a quarter of the error. (The rms of 8 trials is itself uncertain by about 25%.) Each register needs only 6 bits, since a rank cannot exceed 64 - b + 1 ≤ 61. For `b = 14` that is 16 384 × 6 bits = 12 288 bytes, and the error is 1.04/128 = 0.81%. Those are the figures Redis publishes for its HyperLogLog ("up to 12 KB", standard error 0.81%). The class keeps a whole byte per register to stay short.

### 6.4 Union, and what you cannot do

**Merge** two sketches by taking the larger register of each pair. The result is exactly the sketch of the union (the demo checks it on two overlapping ranges of 60 000 keys, and the estimate of the 90 000 distinct keys came out at 0.9907 of the true value, inside 1.04/64 = 1.6%). Merging is also why a sketch per hour can be summed into a sketch per day.

There is no merge for the **intersection**. You can compute |A ∩ B| = |A| + |B| - |A ∪ B|, but each term carries an error of about 1% of its own size, so a small intersection of two large sets disappears into the noise.

---

## 7. MinHash

### 7.1 The idea

The **Jaccard similarity** of two sets is J(A, B) = |A ∩ B| / |A ∪ B|. To compare two big sets (the words or word triples of two web pages) exactly, you walk both. To compare millions of pairs, you need a short summary per set.

Pick a hash function `h` and let `minhash(S)` be the smallest `h(x)` over the elements `x` of `S`. Then

> Probability( minhash(A) = minhash(B) ) = J(A, B)

Look at the union A ∪ B. The element with the smallest hash in the whole union is equally likely to be any of its elements. The two minimums are the same exactly when that element is in A ∩ B, which has probability |A ∩ B| / |A ∪ B|.

So use `k` hash functions. The **signature** of a set is its `k` minimums. The fraction of positions where two signatures agree estimates J. Each position is a coin that lands "agree" with probability J, so the estimate has standard deviation √(J(1 - J)/k): at most 0.5/√k.

A trace: A = {a, b, c}, B = {b, c, d}, so J = 2/4 = 0.5. Four hash functions, each given as the rank it assigns to a, b, c, d:

| | a | b | c | d | min over A | min over B |
|---|---|---|---|---|---|---|
| h1 | 3 | 1 | 4 | 2 | 1 | 1 |
| h2 | 2 | 4 | 1 | 3 | 1 | 1 |
| h3 | 1 | 3 | 2 | 4 | 1 | 2 |
| h4 | 4 | 2 | 1 | 3 | 1 | 1 |

The signatures agree in 3 positions of 4, so the estimate is 0.75. With k = 4 the standard deviation is 0.25, so this is one deviation off. It needs a larger `k`.

### 7.2 The code

```cpp
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
```

The functions that compare signatures are short:

```cpp
// The fraction of positions where two signatures agree estimates the Jaccard similarity of the sets.
double signatureSimilarity(const std::vector<uint64_t>& a, const std::vector<uint64_t>& b) {
    size_t same = 0;
    for (size_t i = 0; i < a.size(); ++i) same += a[i] == b[i];
    return (double)same / (double)a.size();
}
```

### 7.3 Measured

Two sets with 100 common elements and 50 private ones each, so J = 100/200 = 0.5. For each `k`, 100 trials with different elements; the table gives the rms error of the estimate.

| k | 16 | 64 | 256 | 1024 |
|---|---|---|---|---|
| rms error | 0.1214 | 0.0618 | 0.0288 | 0.0151 |
| √(J(1 - J)/k) | 0.1250 | 0.0625 | 0.0312 | 0.0156 |

Four times the hashes halve the error. (With 100 trials, each rms is itself uncertain by about 7%.)

### 7.4 Banding: find similar pairs without comparing all pairs

Even short signatures leave N(N - 1)/2 pairs to compare. **Banding** avoids that. Cut each signature of `k = b × r` values into `b` bands of `r` values. Two sets are a **candidate pair** if at least one band agrees completely. For sets with similarity `s`:

> P(one band agrees) = s^r,    P(candidate) = 1 - (1 - s^r)^b

This is an S-shaped curve with its steep part near (1/b)^(1/r). With `b = 20` and `r = 5` (signatures of 100 values) that is 0.55: pairs well above it are almost always found, pairs well below it almost never. In practice each band is hashed into a bucket, and only the sets that share a bucket are compared, so nobody looks at the other pairs; the demo compares two signatures directly:

```cpp
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
```

Measured on 500 pairs of sets for each similarity:

| J | expected | measured |
|---|---|---|
| 0.8 | 0.9996 | 1.0000 |
| 0.5 | 0.4701 | 0.4640 |
| 0.2 | 0.0064 | 0.0080 |

Candidates are then checked against the full signature or the real sets. Moving the threshold is a choice of `b` and `r`: more bands of fewer rows catch more true pairs and more false candidates.

---
## 8. Where this shows up in systems

- **Databases made of many sorted files (LSM trees).** A point lookup would have to search every file. RocksDB keeps a Bloom filter per file: if any probed bit is 0 the file is skipped, otherwise it is searched, so a false positive costs one wasted read. Its wiki lists 9.9 bits per key for about 1% false positives. Cassandra keeps a filter per SSTable, with `bloom_filter_fp_chance` defaulting to 0.01 (0.1 for leveled compaction); its documentation says 0.01 needs about three times the memory of 0.1, where section 2.3 gives two times (9.59 against 4.79 bits per key). I did not find out why they differ. Wikipedia lists Bigtable, HBase, ScyllaDB and PostgreSQL among the systems that use a filter to skip disk lookups for rows that do not exist.
- **Caches.** Akamai is reported to cache an object only on its second request, to keep one-hit objects out of the disk cache; a Bloom filter remembers what has been seen once.
- **Distinct counts.** Redis offers `PFADD`, `PFCOUNT` and `PFMERGE` for HyperLogLog, up to 12 KB per key with a standard error of 0.81%, whatever the number of items.
- **Counting events in streams.** Count-min sketches add, so they suit counting spread over many machines.
- **Near-duplicate documents.** Broder used MinHash in 1997 to remove near-duplicate pages from AltaVista results; it is also used to cluster documents and, in genomics, to compare genomes.
- **Where you might use them** (fits to the problem, not claims about a product): a Bloom or cuckoo filter in SRAM in front of a slow table, answering "have I seen this flow identifier?"; a count-min sketch for per-flow packet counts when there are more flows than counters; a HyperLogLog for distinct clients per hour on an access point or gateway.

**Choosing.**

| You need | Use | Watch for |
|---|---|---|
| yes/no membership, only adds | Bloom filter | size it for the largest `n` |
| membership and deletes, simplest code | counting Bloom filter | 3 to 4 times the memory; saturation |
| membership and deletes, smaller than that | cuckoo filter | delete only inserted keys; fixed size; an insert can fail |
| how often a key occurred; frequent keys | count-min sketch (with a heap) | rare keys are noise |
| how many different items | HyperLogLog | error about 1.04/√m; no intersection |
| similarity of sets; near duplicates | MinHash with banding | `k`, and `b` and `r` |
| combining results from many machines | Bloom (OR), count-min (add), HyperLogLog (max) | same size and same hash |
| the answer must be exact | a hash table; or a filter in front of the real lookup | |

Under about a million items an exact hash set is usually small enough, and simpler.

---

## 9. Mistakes that cost points

1. **Treating "present" as an answer.** A filter says "no" for certain, but a "yes" can be wrong: about a fraction `p` of the keys that were never added still get one. Confirm a "yes" against the real structure.
2. **Letting `n` grow past the planned size.** `p` climbs quickly. Compute it for the real `n`.
3. **Deleting from a plain Bloom filter** by clearing bits: false negatives for other keys.
4. **Removing a key that may not have been added** from a counting Bloom or cuckoo filter. It takes another key's counters or fingerprint.
5. **Wrapping a counter around.** A counter that overflows to 0 gives a false negative. Saturate, and never decrement a saturated counter.
6. **A cuckoo insert that gives up and loses a fingerprint.** Undo the swaps, and treat `false` as "full".
7. **Averaging the count-min cells, or expecting accuracy on rare keys.** Take the minimum. The error is about εN whatever the key's count.
8. **Using the same hash bits for two jobs.** In HyperLogLog the register index and the rank need different bits; so do a cuckoo fingerprint and its first bucket.
9. **HyperLogLog with no small-range rule, or an arithmetic mean of the registers.** The first is 34% high at n close to m (section 6.3); the second is wrecked by one lucky register.
10. **Merging sketches of different size or seed.** The code accepts it and the result is meaningless.
11. **Trusting a MinHash signature of a few values.** With `k = 4` the standard deviation is 0.25.
12. **Quoting `p` for hostile input.** The formulas assume the hash behaves randomly on the keys. If an attacker picks the keys, use a keyed hash.

---

## 10. Questions you will be asked

**A blacklist has 50 million URLs and the filter may use 100 MB. What false-positive rate can you get?** 100 MB is 800 million bits, which is 16 bits per URL. With the best `k` (11) that gives p = e^(-16 × (ln 2)²) = 0.0005, about 0.05%. Every "yes" is confirmed against the real list.

**Why can a Bloom filter never give a false negative?** Adding a key sets its `k` bits and nothing ever clears a bit, so all of them are still 1 when the key is asked about.

**How does a cuckoo filter find the other bucket of an evicted fingerprint?** bucket 2 = bucket 1 xor hash(fingerprint). XOR is its own inverse, so either bucket and the fingerprint give the other.

**How many counters for error 0.1% of the stream, with 99% confidence?** w = ⌈e/0.001⌉ = 2719 and d = ⌈ln 100⌉ = 5, so 13 595 counters.

**Explain HyperLogLog in a minute.** Hash each item. The first `b` bits pick one of `m` registers; the number of leading zeros of the rest, plus one, is a rank; each register keeps its largest rank. A rank `r` suggests about 2^r items, the registers are combined with a harmonic mean, and the error is 1.04/√m. Duplicates do not matter, and two sketches merge by taking the larger register.

**Find near-duplicate documents among ten million.** Cut each document into overlapping word groups (shingles), MinHash each set into a signature of 100 values, cut the signature into 20 bands of 5, put each band in a hash table, and compare only the documents that share a bucket. The curve 1 - (1 - s^5)^20 puts the threshold near a similarity of 0.55.

---
## 11. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Sizing.** A filter must hold 50 million keys with a false-positive rate of 0.1%. How many bits, how many megabytes (1 MB = 10^6 bytes) and how many hashes? And for 0.01%? Then 100 million keys are added to the 0.1% filter with `k` unchanged: what is the rate now, and what `k` would be best for that load?
*Where:* paper and a calculator. *How:* the formulas of 2.3.

**Problem B: A toy filter.** `m = 16`, `k = 2`. The hashes put `x` on bits 3 and 9, `y` on 9 and 14, `z` on 3 and 14, `w` on 5 and 9. Add `x` and `y`. What do `z` and `w` give, and which answer is wrong? Make it a counting filter with the same adds and remove `x`: what do `y` and `z` give? Start again and remove `z` instead (a false positive): what happens to `x` and `y`?
*Where:* paper. *How:* 2.1 and section 3.

**Problem C: A kick chain.** Four buckets of one slot, `other = bucket xor (fingerprint mod 4)`. Keys are (first bucket, fingerprint): P (0, 1), Q (0, 2), R (1, 6), S (2, 3), inserted in this order. Write the table after each insert and the chain of S, which evicts the resident of its first bucket. Then insert T (3, 5): why can no algorithm store it, and what does the class do after its 500 kicks?
*Where:* paper. *How:* the trace of 4.1.

**Problem D: A count-min sketch.** `d = 2` rows, `w = 4` columns. The hashes put `x` in columns (0, 2), `y` in (0, 3), `z` in (1, 2). The stream is `x y x z x y`. Write the table and the three estimates. Then do the same with the conservative update.
*Where:* paper. *How:* 5.1 and `addConservative`.

**Problem E: A HyperLogLog by hand.** Sixteen registers `2 1 0 3 1 1 2 0 4 1 2 1 0 2 1 3`. Which rule applies, and what is the estimate? The same for `5 4 6 5 4 5 5 6 4 5 5 4 5 6 5 4`.
*Where:* paper. *How:* the examples of 6.3; α is 0.673 for 16 registers.

**Problem F: A MinHash by hand.** A = {1, 2, 3, 4, 5}, B = {4, 5, 6, 7}, and `h1(x) = (3x + 1) mod 11`, `h2(x) = (5x + 2) mod 11`, `h3(x) = (7x + 4) mod 11`, `h4(x) = (2x + 9) mod 11`. Write both signatures and the estimate. What is the exact similarity, and why is the estimate so far from it?
*Where:* paper. *How:* the trace of 7.1.

### In code

**Problem 1: Bloom filter and count-min against their formulas** (own code)
*Where:* your editor. *Goal:* a Bloom filter with 100 000 keys, measuring the false-positive rate for 6, 10 and 14 bits per key; and a count-min sketch on a skewed stream, measuring the largest and mean error for several widths. Compare each with its formula (2.3, 5.3). Use your own hash and a fixed seed.
*How:* sections 2 and 5. If the Bloom rate is far above the formula, the `k` positions are not independent enough; if count-min errors are too large, two rows share a hash.

**Problem 2: Majority Element II** (LeetCode 229, medium)
*Where:* leetcode.com/problems/majority-element-ii. *Goal:* all elements that occur more than ⌊n/3⌋ times, in O(n) time and O(1) space.
*How:* the exact cousin of a heavy-hitter sketch. At most two elements can exceed n/3: keep two candidates with two counters.

**Problem 3: Linked List Random Node** (LeetCode 382, medium)
*Where:* leetcode.com/problems/linked-list-random-node. *Goal:* a uniformly random node of a list of unknown length, in O(1) extra space. Then pick `k` nodes.
*How:* reservoir sampling. Prove that each node survives with probability 1/n.

**Problem 4: A cuckoo filter** (own code)
*Where:* your editor. *Goal:* the class of 4.2 from scratch. Test that a failed `add` leaves every earlier key present. Measure the load at the first failure for 2, 4 and 8 slots per bucket, and the false-positive rate for 8, 12 and 16 bits against 2bα/2^f.
*How:* section 4. Write the undo of the kick chain first.

**Problem 5: HyperLogLog from scratch** (own code)
*Where:* your editor. *Goal:* the class of 6.2. Measure the rms error against 1.04/√m for `b` = 8, 10, 12. Then plot the error for `n` from 0.2 m to 20 m and look at the region near 2.5 m, where the estimator changes rule. Finally estimate |A ∩ B| for two sets that share 1% of their elements and report the error.
*How:* section 6. Use 4 to 8 trials per point.

**Problem 6: A near-duplicate finder** (own code)
*Where:* your editor. *Goal:* 2000 documents of 200 random words from a 5000-word vocabulary, 100 of them copies of another with 10% of the words replaced. Cut each into 3-word shingles, make MinHash signatures with `k = 100`, band them 20 × 5, and find candidate pairs through a hash table of bands. Report how many planted pairs you found, how many other candidates you got, and how that compares with all 1 999 000 pairs.
*How:* section 7.4. Check each candidate against the exact Jaccard.

**More practice:** Majority Element (169), Top K Frequent Elements (347), Top K Frequent Words (692), Random Pick Index (398).

---

## Answers

### Problem A

0.1% needs 14.38 bits per key: m = 718 879 379 bits, which is 89.86 MB, and k = 10. At 0.01% it is 19.17 bits per key: 119.81 MB and k = 13. With 100 million keys there are 7.19 bits per key and, with `k = 10`, the rate is (1 - e^(-10/7.19))^10 = 0.0572, about 5.7%. The best `k` for that load is 5 and gives 3.2%, still about 32 times the design value: a filter does not degrade gracefully past its `n`.

### Problem B

After adding `x` and `y`, bits 3, 9 and 14 are set. `z` needs 3 and 14: both set, so "present", which is wrong. `w` needs bit 5, which is 0: absent, correct. With counters: 3 → 1, 9 → 2, 14 → 1. Removing `x` leaves 3 → 0, 9 → 1, 14 → 1: `y` is still present and `z` is now absent, so the false positive is gone. Removing `z` instead leaves 3 → 0, 9 → 2, 14 → 0: `x` (needs 3) and `y` (needs 14) are both reported absent, two false negatives from one careless remove.

### Problem C

P: `[1, -, -, -]`. Q: its other bucket is 0 xor 2 = 2, free: `[1, -, 2, -]`. R: bucket 1 is free: `[1, 6, 2, -]`. S: first bucket 2 holds 2, the other bucket is 2 xor 3 = 1, which holds 6, so both are full. Kick 1: 3 takes bucket 2 and evicts 2, which goes to 2 xor 2 = 0. Kick 2: 2 takes bucket 0 and evicts 1, which goes to 0 xor 1 = 1. Kick 3: 1 takes bucket 1 and evicts 6, which goes to 1 xor 2 = 3, free. Final `[2, 1, 3, 6]`. T would need bucket 3 or bucket 2 (3 xor 1), but five fingerprints cannot fit in four slots. The class makes 500 kicks, undoes them all in reverse order, so the table is again `[2, 1, 3, 6]`, and returns `false`: the filter is full.

### Problem D

Plain: row 1 `[5, 1, 0, 0]` (column 0 holds `x` three times and `y` twice), row 2 `[0, 0, 4, 2]`. Estimates `x` = min(5, 4) = 4, `y` = min(5, 2) = 2, `z` = min(1, 4) = 1, against true counts 3, 2, 1: only `x` is too high. Conservative: row 1 `[3, 1, 0, 0]`, row 2 `[0, 0, 3, 2]`, estimates 3, 2, 1, all exact, because the second `y` found its clean cell in row 2 and left column 0 alone.

### Problem E

First: the sum of 2^(-M) is 3 + 6/2 + 4/4 + 2/8 + 1/16 = 7.3125, so the raw E = 0.673 × 256 / 7.3125 = 23.56. That is at most 2.5 m = 40 and 3 registers are empty, so linear counting: 16 ln(16/3) = 26.78. Second: no register is empty and the sum is 5/16 + 8/32 + 3/64 = 0.609375, so E = 172.288 / 0.609375 = 282.73, above 40, used as it is.

### Problem F

h1 gives A 4, 7, 10, 2, 5 and B 2, 5, 8, 0: minimums 2 and 0. h2: A 7, 1, 6, 0, 5 and B 0, 5, 10, 4: 0 and 0. h3: A 0, 7, 3, 10, 6 and B 10, 6, 2, 9: 0 and 2. h4: A 0, 2, 4, 6, 8 and B 6, 8, 10, 1: 0 and 1. The signatures are A = (2, 0, 0, 0) and B = (0, 0, 2, 1); they agree in one position, so the estimate is 0.25. The exact value is 2/7 = 0.2857. With k = 4 the standard deviation is about 0.23, so an error of 0.04 is unremarkable. (These hashes are toys; real MinHash needs hashes that behave like random permutations.)

### Problem 2: Majority Element II

Candidates `c1`, `c2`, counters `n1`, `n2`. For each `x`: if `x == c1` then `++n1`; else if `x == c2` then `++n2`; else if `n1 == 0` take `x` as `c1`; else if `n2 == 0` take it as `c2`; else `--n1` and `--n2`. Each decrement cancels three different elements, and an element above n/3 cannot be cancelled completely. A second pass counts both candidates.

### Problem 3: Linked List Random Node

Keep one node; when node i arrives, replace it with probability 1/i. Node j is kept at step j with probability 1/j and survives each later step i with probability 1 - 1/i, so its final probability is (1/j) × (j/(j+1)) × ... × ((n-1)/n) = 1/n. For `k` nodes keep the first `k`, and let node i > k replace a random kept one with probability k/i.

### Problems 4 and 5

Expect a load of about 84%, 95% and 98% for 2, 4 and 8 slots (the cuckoo filter paper's figures; the demo's class reached 96% for 4). In Problem 5, near 2.5 m the raw estimate is the one used; the demo's was 2977.59 for 3000 items with m = 1024. For the intersection, each term of |A| + |B| - |A ∪ B| has an error of about 1% of its size, so the error on a 1% intersection can be as large as the intersection.

### Problem 6: A near-duplicate finder

A 3-word shingle survives if none of its three words was replaced: probability 0.9^3 = 0.729. Two copies share about 0.729 of their shingles, so J = 0.729 / (2 - 0.729) = 0.57. That is at the threshold of 20 × 5 bands: a pair is a candidate with probability 1 - (1 - 0.57^5)^20 = 0.71, so expect about 70 of the 100. With 25 bands of 4 rows it is 1 - (1 - 0.57^4)^25 = 0.94. Unrelated documents share almost no shingle, so there are almost no other candidates. The choice of `b` and `r` is the knob.

---

## Cheat sheet

The table in section 1 says what each structure answers and how it merges. The formulas:

- **Bloom:** m = -n ln p / (ln 2)², k = (m/n) ln 2, p ≈ (1 - e^(-kn/m))^k; n ≈ -(m/k) ln(1 - X/m). Counting Bloom: 4-bit counters, saturate, 3 to 4 times the bits.
- **Cuckoo filter:** other bucket = bucket xor hash(fingerprint); f ≥ log2(2b/p); f/α bits per key; p ≈ 2bα/2^f; add can fail.
- **Count-min:** w = ⌈e/ε⌉, d = ⌈ln(1/δ)⌉; never low, high by at most εN with probability 1 - δ.
- **HyperLogLog:** m = 2^b registers; E = α m² / Σ 2^(-M); linear counting m ln(m/V) if E ≤ 2.5 m; error 1.04/√m.
- **MinHash:** P(equal minimum) = J; error √(J(1 - J)/k); banding 1 - (1 - s^r)^b.

**Sources** (checked while writing this tutorial; the formulas on the Wikipedia pages did not come through my fetch, so they were derived here and checked by measurement and by independent Python models):
- Wikipedia: [Bloom filter](https://en.wikipedia.org/wiki/Bloom_filter) (about 9.6 bits per element for 1%, 4.8 more per tenfold, union by OR, counting filters 3 to 4 times the space, named systems), [Count-min sketch](https://en.wikipedia.org/wiki/Count%E2%80%93min_sketch) (w, d, error εN, over-estimates only, merge by adding), [HyperLogLog](https://en.wikipedia.org/wiki/HyperLogLog) (merge by maximum, 64-bit hashes), [MinHash](https://en.wikipedia.org/wiki/MinHash) (equal-minimum probability is J, error O(1/√k), AltaVista).
- Fan, Andersen, Kaminsky, Mitzenmacher, [Cuckoo Filter: Practically Better Than Bloom, CoNEXT 2014](https://sigcommconfs.hosting2.acm.org/co-next/2014/CoNEXT_papers/p75.pdf) (bucket 2 = bucket 1 xor hash(fingerprint), about 95% load for four slots, the 2b/2^f bound, f/α bits, 500 kicks, delete and duplicate limits, 3% crossover for the semi-sorted variant).
- Redis, [HyperLogLog](https://redis.io/docs/data-types/hyperloglogs) (up to 12 KB, standard error 0.81%, `PFADD`, `PFCOUNT`, `PFMERGE`).
- Apache Cassandra, [Bloom filters](https://cassandra.apache.org/doc/4.0/operating/bloom_filters.html) (a filter per SSTable, `bloom_filter_fp_chance`, the memory comparison). RocksDB wiki, [RocksDB Bloom Filter](https://github.com/facebook/rocksdb/wiki/RocksDB-Bloom-Filter) (bits per key against rate, cache-local filters, point lookups).
- Apache Cloudberry's copy of PostgreSQL's [`hyperloglog.c`](https://apache.googlesource.com/cloudberry/+/HEAD/src/backend/lib/hyperloglog.c) (α constants 0.673, 0.697, 0.709 and 0.7213/(1 + 1.079/m), linear counting at 2.5 m, a large-range correction for 32-bit hashes).

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `04_probabilistic_structures_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** hashing is complete (tutorials 01 to 04). The next DSA area is 03, binary search and sorting, starting with tutorial 01, binary search templates (loop invariants, `lower_bound` and `upper_bound`). Say "next" when you are ready.
