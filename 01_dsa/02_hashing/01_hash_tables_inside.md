# DSA 02.01: Hash Tables Inside

*Part of the [Hashing plan](00_plan.md) · Time: about 5 hours (2 reading, 3 problems) · You need: tutorial [01](../01_arrays_and_strings/01_complexity_and_arrays_in_memory.md) (arrays in memory, amortised cost) and [07](../01_arrays_and_strings/07_string_basics.md) (a string key costs O(length) to hash) · Companion code: [`01_hash_tables_demo.cpp`](01_hash_tables_demo.cpp)*

By the end of this tutorial you can:

- explain why a hash table lookup is O(1) on average, what the average is taken over, and when it becomes O(n);
- write a hash function for integers, strings and pairs, and say why xor-ing two hashes is wrong;
- estimate how many keys collide with the birthday formula, and say why a table must handle collisions even when it is almost empty;
- write a chained table and a linear-probing table with erase (tombstones) and resizing;
- quote the expected search cost at a given load factor, and check it by experiment;
- explain why doubling makes insertion amortised O(1) while a single insert can still cost O(n);
- explain universal hashing and hash flooding, and how a secret seed defends against them;
- say what `std::unordered_map` really does, and when to use something else.

Every class in this tutorial is in the companion file, which compiles with `-Wall -Wextra -fsanitize=address,undefined`. It checks both tables against `std::unordered_map` on 500000 random operations, and it measures every cost formula quoted below.

---

## 1. The idea

An array finds element `i` in O(1) because the address is computed from `i`. A hash table does the same for keys that are not small integers: strings, 64-bit ids, coordinate pairs, MAC addresses.

1. A **hash function** turns the key into an integer `h(key)`.
2. That integer is reduced to a **bucket** number between 0 and m - 1 (`h(key) mod m`, or a bit mask when m is a power of two).
3. The entry is stored in that bucket. To find it later, compute the bucket again and look only there.

Two different keys can reach the same bucket. That is a **collision**, and what a table does about it is what separates the designs: **chaining** keeps a small list in each bucket, **open addressing** looks at other slots of the same array.

| Symbol | Meaning |
|---|---|
| n | number of entries stored |
| m | number of buckets (or slots) |
| α = n / m | **load factor**: the average number of entries per bucket |

| Operation | Cost |
|---|---|
| insert, find, erase | O(1) **expected**, O(n) worst case (every key in one bucket) |
| visit every entry | O(n + m); the order is unspecified |
| smallest key, next larger key, all keys in a range | no better than O(n): a hash table has no order (use `std::map` or a sorted array) |

"Expected O(1)" hides two separate averages. One is over the keys and the hash function: if they spread keys evenly, a bucket holds about α entries. The other is over time: the table keeps α below a constant by growing, and growing is amortised O(1) (section 6). Two rules make it all work: **equal keys must have equal hashes**, otherwise a lookup goes to the wrong bucket; different keys **may** have equal hashes, because the table compares the keys themselves.

---

## 2. Hash functions

### 2.1 What you need

Consistent (equal keys, equal hash, on every call in the run), fast (it runs on every operation) and well spread (similar keys should not land in similar buckets). A hash function does **not** need to be collision-free, and it does not need to be hard to invert unless an attacker picks the keys (section 8).

### 2.2 Integers

The identity function `h(x) = x` looks perfect: different keys never collide. It is what libstdc++ uses for `std::hash<int>`. The trouble is the reduction to a bucket. With 1024 buckets and a mask, the bucket is the low 10 bits of the key. Keys that are multiples of 1024 (page-aligned addresses, ids with a fixed low part) all land in bucket 0. The companion puts 100000 such keys into 1024 buckets: with the identity hash the largest bucket holds all 100000.

The fix is to **mix** the bits before reducing. This is the finalizer of the splitmix64 generator:

```cpp
// The finalizer of splitmix64: every output bit depends on every input bit.
uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;              // fold the high bits down, then spread upwards
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}
```

Each line xors the high half into the low half and multiplies by an odd constant, which pushes the influence of every bit upwards. The function is a bijection on 64-bit values, so mixing never creates a collision. The **avalanche** test flips one input bit and counts the output bits that change: for `mix64` the average is 32.01 of 64, as good as random (the identity hash changes exactly one). On the 100000 multiples of 1024, `mix64` gives a largest bucket of 129 against an ideal of 98.

A cheaper choice is **Fibonacci hashing**: multiply by 2^64 divided by the golden ratio, and keep the **top** bits.

```cpp
// Fibonacci hashing: multiply by 2^64 divided by the golden ratio and keep the TOP bits.
uint64_t fibonacciHash(uint64_t key, int bits) {
    return (key * 0x9E3779B97F4A7C15ULL) >> (64 - bits);
}
```

Why the top bits: in a product, bit `k` depends only on bits `0..k` of the key, so the low bits of `key * c` are as poor as the low bits of `key`, while the top bits depend on all of them. The same 100000 multiples of 1024 give a largest bucket of 100 (smallest 94), and 4096 consecutive keys give buckets of 2 to 5 entries against an ideal of 4. The Linux kernel's `hash_64` is this function, with the constant `0x61C8864680B583EB` (which is 2^64 minus ours) and the kernel source says that the high bits of the product must be used. Fibonacci hashing is fine for keys that are not chosen by an enemy; it is not a full mixer.

### 2.3 Strings

Hash every byte. FNV-1a xors in a byte, then multiplies by a prime:

```cpp
// FNV-1a over the bytes of a string: xor in a byte, then multiply by a prime.
uint64_t fnv1a(std::string_view s) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001B3ULL;
    }
    return h;
}
```

| Input | `fnv1a` |
|---|---|
| `""` | `cbf29ce484222325` |
| `"a"` | `af63dc4c8601ec8c` |
| `"foobar"` | `85944171f73967e8` |

Hashing costs O(length of the key), so a hash map of strings is O(L) per operation (tutorial 07). Java's `String.hashCode` is `h = 31 * h + c`:

```cpp
// The hash Java uses for strings: h = 31 * h + c. Fast, simple, and easy to attack.
uint32_t javaStringHash(std::string_view s) {
    uint32_t h = 0;
    for (unsigned char c : s) h = 31 * h + c;
    return h;
}
```

It is fast and weak: `"Aa"` and `"BB"` both hash to 2112 (65 × 31 + 97 = 66 × 31 + 66). Then every concatenation of these two blocks collides as well: the companion builds the 2^8 = 256 different strings of 16 letters made of `Aa` and `BB`, and they all have one hash value. Section 8 shows what an attacker does with that.

### 2.4 Pairs and records

C++ has no `std::hash` for `std::pair`, so `std::unordered_map<std::pair<int, int>, int>` does not compile until you supply a hash. The tempting version xors the parts:

```cpp
// The tempting wrong way: xor. (a, b) and (b, a) collide, (a, a) is always 0, and the result stays small.
struct XorPairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return (size_t)p.first ^ (size_t)p.second;
    }
};
```

It is wrong in three ways: `(a, b)` and `(b, a)` collide, `(a, a)` is always 0, and the result stays small when the parts are small. For all 10000 pairs with `a` and `b` below 100 it produces only **128** distinct values. Mix each part and combine in an order-sensitive way:

```cpp
// Mix one more value into a running hash (the idea of boost::hash_combine).
size_t hashCombine(size_t seed, size_t value) {
    return seed ^ (mix64(value) + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2));
}

struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return hashCombine(mix64((uint64_t)p.first), (size_t)p.second);   // order matters: (a, b) differs from (b, a)
    }
};
```

The same 10000 pairs give **10000** distinct hash values. For a record with more fields, fold them one after another: `h = hashCombine(h, field)`. Hash exactly the fields that `operator==` compares, no more and no fewer.

### 2.5 From hash to bucket

| Reduction | Cost | Needs |
|---|---|---|
| `h & (m - 1)`, m a power of two | one instruction | a hash that mixes its **low** bits well |
| `h % p`, p a prime | a division | nothing special; every bit of `h` matters |

libstdc++ takes the second road: the identity hash with a prime bucket count (13, 29, 59, 127, ... in section 9), which tolerates weak hashes at the price of a division on every operation. The tables below take the first road and call `mix64` first.

---

## 3. Collisions are certain

There are more possible keys than buckets, so some keys must share. But collisions come far earlier than that. If `n` keys fall uniformly into `m` buckets, the chance that they are **all different** is the product of `(1 - i/m)` for `i = 0 .. n - 1`; for large `m` the chance that at least two share is about `1 - e^(-n(n-1)/2m)`.

```cpp
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
```

| Keys n | Buckets m | Chance of at least one collision |
|---|---|---|
| 23 | 365 | 0.5073 (the birthday problem) |
| 40 | 1000 | 0.5464 |
| 77163 | 2^32 | 0.5 |
| 1000000 | 2^32 | more than 0.999999 |
| 1000000 | 2^64 | 2.71e-8 |

Three consequences:

- A table of 1000 buckets holding only 40 keys (load factor 0.04) already has a shared bucket more often than not. Collision handling is not an edge case.
- The scale is the **square root** of the number of values: about 1.18 × √m keys give an even chance of a repeat. A 32-bit hash identifies at most tens of thousands of items safely.
- Never trust the hash alone. After finding a bucket the table compares the **keys**. A system that stores only a hash (deduplication by fingerprint, say) accepts a small chance of error: 2.7e-8 for a million items with 64-bit hashes, and a near certainty with 32-bit hashes.

---

## 4. Separate chaining

Every bucket holds a small collection of entries. The textbook version uses a linked list; this one uses a `std::vector` per bucket, which behaves the same and keeps a chain contiguous in memory.

```cpp
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
```

- `put` scans the chain for the key. If it is there, the value is replaced; otherwise the entry is appended, and if the load factor passes `maxLoad` the table doubles (`rehash`, section 6).
- `find` returns a pointer to the value, or `nullptr`. It counts the keys it compares, so the experiments below can measure the cost.
- `erase` overwrites the entry with the last one of the chain and pops it. Order inside a chain does not matter.
- `indexOf` is `mix64(key ^ seed_) & (buckets_ - 1)`: mix, then mask. The `seed_` is used in section 8.

In the trace below the hash is `h(x) = x mod m` so that you can follow it by hand (the code calls `mix64`; the logic is the same). The table starts with 4 buckets and doubles when `size > m`:

| Insert | Buckets afterwards (bucket: chain) |
|---|---|
| 5 | 4 buckets, `1: [5]` |
| 9 | `1: [5, 9]` |
| 12 | `0: [12]`, `1: [5, 9]` |
| 1 | `0: [12]`, `1: [5, 9, 1]` |
| 17 | size 5 > 4, so **rehash to 8 buckets**: `1: [9, 1, 17]`, `4: [12]`, `5: [5]` |

Before the rehash, key 5 sat in bucket `5 mod 4 = 1`; after it, in bucket `5 mod 8 = 5`. The bucket depends on `m`, which is why growing means re-inserting every entry. A search for 25 now goes to bucket `25 mod 8 = 1` and compares 3 keys before giving up.

**Cost.** Assume the hash spreads keys evenly, so a chain has `α = n / m` entries on average.

- An **unsuccessful** search walks a whole chain: α comparisons.
- A **successful** search for the `i`-th inserted key meets the `(i - 1) / m` keys that were added to its chain before it, plus itself. Averaged over `i = 1 .. n` this is `1 + (n - 1) / (2m)`.

The companion fills 4096 buckets with random keys and measures:

| Load α | Successful (measured) | `1 + (n-1)/2m` | Unsuccessful (measured) | α |
|---|---|---|---|---|
| 0.5 | 1.257 | 1.250 | 0.500 | 0.5 |
| 1.0 | 1.504 | 1.500 | 0.999 | 1.0 |
| 2.0 | 2.026 | 2.000 | 1.996 | 2.0 |
| 4.0 | 3.009 | 3.000 | 3.994 | 4.0 |

Chaining degrades gracefully: even at α = 4 a search compares only about 3 to 4 keys. Its price is memory behaviour. With one separately allocated node per entry (as in `std::unordered_map`), every step of a chain is a pointer chase, usually a cache miss (CO 13).

---

## 5. Open addressing: linear probing

Here every entry lives in the array itself. A key goes to its home slot `h(key) mod m`; if that is taken, try the next slot, then the next, wrapping around at the end. A search follows the same path and stops when it finds the key or reaches a slot that was **never used**.

```cpp
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
```

There are three slot states, and the third is what makes erase work.

- `find` walks from the home slot. An `Empty` slot means "no key ever passed through here", so the key is absent. The loop ends because the load factor stays below 1, so some slot is always `Empty`.
- `erase` must **not** turn a slot back into `Empty`. Another key may have been pushed past this slot when it was full, and an `Empty` hole would end its search too early. So the slot becomes `Deleted`, a **tombstone**: searches walk over it.
- `put` remembers the first tombstone on its way and reuses it when the key turns out to be new. `used_` counts full slots **plus** tombstones, because both lengthen searches; when `used_` passes the limit, `rehash` rebuilds the table, drops the tombstones, and doubles the capacity only if the live entries need it.

Trace with `h(x) = x mod 8` (capacity 8, `.` is empty, `D` is a tombstone):

| Step | Slots 0 to 7 | Note |
|---|---|---|
| put 3 | `. . . 3 . . . .` | home slot 3 |
| put 11 | `. . . 3 11 . . .` | home 3 is taken, goes to 4 |
| put 19 | `. . . 3 11 19 . .` | home 3, then 4 taken, lands in 5 |
| put 4 | `. . . 3 11 19 4 .` | home 4 is taken, so 5, then 6 |
| erase 11 | `. . . 3 D 19 4 .` | slot 4 becomes a tombstone |
| find 19 | | probes slots 3, 4, 5: found in **3** probes |
| find 12 | | home 4: slots 4 (tombstone), 5, 6, 7 (empty): absent after **4** probes |
| put 27 | `. . . 3 27 19 4 .` | home 3, passes the tombstone at 4, reaches empty slot 7, so 27 is new: it takes slot 4 |

If `erase` had marked slot 4 `Empty`, `find 19` would stop at slot 4 and wrongly report "not found". The companion tests exactly this case.

**Clustering.** Keys 3, 11, 19, 4 have only two home slots between them, yet they form a run of four slots, and the run keeps growing because any key whose home falls inside it joins the end. Long runs make searches slow, and the damage grows quickly with the load. Knuth's analysis of linear probing at load α gives

- successful: `½ (1 + 1 / (1 - α))`
- unsuccessful: `½ (1 + 1 / (1 - α)²)`

and the companion measures it (262144 slots, random keys):

| Load α | Successful (measured) | Formula | Unsuccessful (measured) | Formula |
|---|---|---|---|---|
| 0.5 | 1.504 | 1.500 | 2.510 | 2.500 |
| 0.7 | 2.180 | 2.167 | 6.157 | 6.056 |
| 0.9 | 5.580 | 5.500 | 49.374 | 50.500 |

At α = 0.9 a miss costs about 50 probes, so linear probing tables keep α near 0.5 to 0.7 (the code stops at 0.7). That sounds wasteful, but the slots of a probe sequence are neighbours in memory: 8 keys of 8 bytes fit in one 64-byte cache line, so a probe run of a few slots touches one or two cache lines (two arrays here: states and keys), where chaining costs a miss per node.

| Variant | Idea | Seen in |
|---|---|---|
| Quadratic probing, double hashing | step by 1, 4, 9, ... or by a second hash of the key; breaks up the runs | textbooks, some runtimes |
| Robin Hood | on insert, an entry far from home displaces one that is closer; probe lengths even out; deletion can shift entries back and needs no tombstones | Rust's HashMap before 1.36 |
| Swiss table | one metadata byte per slot (7 bits of the hash); 16 bytes are compared with one SIMD instruction | Abseil `flat_hash_map`, Rust's `HashMap` (hashbrown), Go 1.24 maps |
| Cuckoo hashing | two possible slots per key, so a lookup checks at most two | DPDK's `rte_hash` (packet processing library) |

---

## 6. Growing the table

When α passes its limit, allocate a table twice as large and insert every entry again (its bucket depends on `m`). One rehash of `n` entries costs O(n), but they are rare: the companion inserts 100000 keys into a table that starts with 8 buckets.

| Quantity | Value |
|---|---|
| rehashes | 14 (8 buckets, then 16, 32, ..., 131072) |
| entries moved, all rehashes together | 131078 (1.31 per insert; the bound is below 2 per insert) |
| entries moved by the largest single rehash | 65537 |

The total is a geometric series: the last rehash moves about `n`, the one before about `n/2`, and so on, so the sum stays below `2n`. Spread over `n` inserts that is O(1) each, **amortised**. If the table grew by a fixed amount instead (say 1000 buckets each time), the sum would be quadratic (problem D).

Amortised is not the same as every operation being fast: the insert of the 65537th key moved 65537 entries at once. In a packet path or a game loop that pause is a bug. Two remedies: **presize** (`std::unordered_map::reserve(n)`; in the companion `ChainedTable presized(131072)` performs no rehash at all), or **rehash incrementally**, moving a few buckets with every operation (Redis does this).

---

## 7. Universal hashing

Any **fixed** hash function has sets of keys that all collide, and an enemy who knows the function can find them (section 8). The defence is to pick the function **at random** when the table is created. A family of functions is **universal** if, for any two different keys, a function drawn at random from the family makes them collide with probability at most 1/m. Then for **any** set of n keys the expected number of other keys in a given key's bucket is at most n/m. The average is over the random choice of function, not over the data, so no input is bad in advance.

The classic family (Carter and Wegman, 1979) uses a prime `p` larger than every key: `h(x) = ((a x + b) mod p) mod m`, with `a` random in `1 .. p - 1` and `b` random in `0 .. p - 1`. With the Mersenne prime `p = 2^61 - 1` the modulo needs no division, because `2^61` is 1 modulo `p`: the 122-bit product splits into a low and a high part, and the two parts are simply added.

```cpp
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
```

The caller takes the result `% m` to get the bucket. Why it works: for two different keys `x` and `y`, the pair `(a x + b, a y + b) mod p` takes every pair of **different** values exactly once as `(a, b)` ranges over all choices, so the two hashes behave like two different values drawn uniformly, and at most `(p - 1) / m` of the other values are equal to a given one modulo `m`.

The companion does not take this on trust. It tries **every** pair of different keys below `p` with **every** `(a, b)` and reports the worst pair:

```cpp
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
```

| p | m | Worst collision fraction over all pairs | 1 / m | Fixed hash `x mod m` |
|---|---|---|---|---|
| 31 | 7 | 0.1161 | 0.1429 | 1.0 |
| 101 | 10 | 0.0911 | 0.1000 | 1.0 |

The fixed hash is the worst possible: `x` and `x + m` collide for every table, with fraction 1.0. Two cautions: keys must be below `p` (a 64-bit key is first reduced modulo `p`, so two keys that differ by a multiple of `p` collide for every `(a, b)`), and universal only promises something about **pairs**. Servers that face hostile input use a keyed hash designed for it, such as SipHash.

---

## 8. Hash flooding

A hash table promises O(1) only if the keys spread out. If an attacker chooses the keys, all of them can go to one bucket, and n operations cost O(n²) instead of O(n): a cheap request makes the server burn CPU. Crosby and Wallach (USENIX Security 2003) demonstrated this against real network software, and in 2011 the same idea was shown against the hash tables that hold request parameters in many web platforms.

The companion plays both sides. The attacker needs keys that share a bucket. If the hash is `mix64`, run it **backwards**:

```cpp
// The exact inverse of mix64. A hash that is not keyed can be run backwards.
uint64_t unmix64(uint64_t x) {
    x ^= (x >> 31) ^ (x >> 62);                               // undo each step in reverse order
    x *= 0x319642B2D24D8EC3ULL;                               // inverse of 0x94D049BB133111EB modulo 2^64
    x ^= (x >> 27) ^ (x >> 54);
    x *= 0x96DE1B173F119089ULL;                               // inverse of 0xBF58476D1CE4E5B9 modulo 2^64
    x ^= (x >> 30) ^ (x >> 60);
    return x;
}
```

`unmix64` undoes the steps of `mix64` in reverse order, multiplying by the inverse constants modulo 2^64. With it the attacker chooses keys whose mixed value is `j × bucketCount`: their low bits are all zero, so every one of them lands in bucket 0 of a table with that many buckets.

```cpp
// Keys that all land in bucket 0 of a ChainedTable with `bucketCount` buckets and seed 0: run the hash backwards.
std::vector<uint64_t> floodKeys(size_t count, size_t bucketCount) {
    std::vector<uint64_t> keys;
    for (size_t j = 0; j < count; ++j) keys.push_back(unmix64((uint64_t)j * bucketCount));   // mix64(key) = j * bucketCount
    return keys;
}
```

The victim is a `ChainedTable` with 16384 buckets and seed 0; the defender is the same table with a secret seed. Both receive the 10000 crafted keys:

| Table | Longest chain | Comparisons to look all 10000 keys up |
|---|---|---|
| seed 0 (the attacker knows the function) | 10000 | 50005000 (that is N(N + 1)/2) |
| secret random seed | 5 | 13076 |

The secret seed turns the same keys into ordinary-looking input. The attack on `std::unordered_map` is even simpler, because libstdc++ hashes an integer to itself: keys that are multiples of the bucket count all share bucket 0. For 10000 of them the companion sees one bucket of 10000 entries and an insertion time of about 0.16 s, against about 0.0005 s for random keys (the test machine of section 9, `-O2`; the cost grows with the square, so ten times the keys cost a hundred times as much).

Mixing alone does not help: an `unordered_map` whose hash is `mix64(key)` and which receives `unmix64(j × bucket_count)` also gets a single bucket of 10000. A secret does:

```cpp
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
```

With this hash the same attack leaves the largest bucket in single digits (the companion checks that it is at most 16). `RandomizedHash` is the usual fix in competitive programming, where test data is written to break `unordered_map`, and it is enough for an ordinary program. Defences used in practice:

| Defence | Where |
|---|---|
| a secret random key inside the hash function | Python (SipHash for strings since 3.4), Rust's default `HashMap` (SipHash 1-3), Linux connection tracking (SipHash with a random key) |
| turn long chains into balanced trees, so the worst case is O(log n) | Java 8 and later `HashMap` (a bucket that grows past 8 entries becomes a tree if the table has at least 64 buckets; a smaller table is resized instead) |
| limit what one request may contain | a cap on the number of parameters in web servers (PHP added `max_input_vars` after the 2011 attacks) |

---

## 9. `std::unordered_map` in practice

The interface of `std::unordered_map` (buckets, `bucket_size`, references that survive growth) in effect requires **chaining with one separately allocated node per entry**. Everything below was measured with libstdc++ (GCC 13); libc++ and MSVC differ in the details.

- **Integers hash to themselves and the bucket count is prime.** `std::hash<int>()(5)` is 5. Inserting 1 to 100000 into an empty map grows the bucket count through 13, 29, 59, 127, 257, 541, 1109, 2357, 5087, 10273, 20753, 42043, 85229 and 172933. The maximum load factor is 1.0.
- **References stay valid, iterators do not.** A reference to an element taken at the start still works after 100000 inserts (the companion checks it), but the standard invalidates all iterators when a rehash happens.
- **`m[key]` inserts** a default value when the key is missing. To only look, use `find` or `count` (`contains` in C++20).
- **`reserve(n)`** sizes the table for `n` entries: 5000 inserts after `reserve(5000)` cause no rehash.
- **`std::hash<const char*>` hashes the pointer, not the text.** Two equal C strings at different addresses are different keys. Use `std::string` or `std::string_view`.
- **Equal keys must hash equally.** `0.0` and `-0.0` are equal and `std::hash<double>` gives them one hash. In your own hash, use exactly the fields that `operator==` compares.
- **Iteration order is unspecified** and changes when the table rehashes. To erase while iterating, write `it = m.erase(it)`.

Speed, for information only: 1 million inserts followed by 1 million lookups of random 64-bit keys, three runs on a 2-core 2.1 GHz Xeon virtual machine, GCC 13.3, `-O2`, libstdc++. Your machine will differ.

| Table | Time |
|---|---|
| `std::unordered_map` | 0.23 to 0.37 s |
| `ChainedTable` (a vector per bucket) | 0.24 to 0.28 s |
| `LinearProbingTable` (load at most 0.5) | 0.07 to 0.11 s |

The flat table is about three times faster here: no allocation per entry and few cache misses (CO 13, CO 14). The companion tables are teaching code (64-bit keys, `int` values), so treat the ratio as a hint, not a law; production flat maps such as Abseil's `flat_hash_map` and Boost's `unordered_flat_map` report similar gains. The price is that pointers and iterators die on insertion and entries move. Typical load limits: 1.0 for chaining in libstdc++, 0.75 for Java's `HashMap`, 2/3 for Python's `dict`, 7/8 for Abseil's Swiss tables, 0.7 for the table of section 5.

---

## 10. Where this shows up in systems

- **Kernel tables.** Linux keeps many tables as power-of-two arrays of chains and hashes with `hash_64` (section 2.2). Tables reachable from the network, such as connection tracking, mix a random key into the hash (section 8).
- **Flow and session tables.** A firewall, NAT or load balancer looks up the 5-tuple (addresses, ports, protocol) of every packet. The longest chain is a latency bound, a hostile sender chooses its own addresses and ports, and a rehash in the data path is a latency spike. Such tables are presized, seeded, and often use open addressing or cuckoo hashing (DPDK's `rte_hash`).
- **Receive-side scaling.** A NIC hashes the 5-tuple of each packet (with the Toeplitz function) to pick a receive queue, so that all packets of a flow reach the same core, in order. Equal keys, equal hash: the first rule of section 1, used as a feature.
- **Station tables in Wi-Fi software.** In the hostapd 0.5.8 source I checked, the station table has 256 chains (`STA_HASH_SIZE`) and the hash is the last byte of the MAC address (`STA_HASH(sta) (sta[5])`). That spreads well for addresses that differ in the last byte and not at all for ones that differ elsewhere, and a client that chooses its own address chooses its bucket. Newer versions may differ.
- **GPUs.** NVIDIA's cuCollections library builds GPU hash tables with open addressing and linear probing, where a group of threads probes a window of adjacent slots together: that fits the hardware better than chasing list pointers.
- **Caches and interning.** An LRU cache is a hash map plus a list (Hashing tutorial 03); symbol tables, string interning and deduplication are hash tables with the key-compare step of section 3.

---

## 11. Choosing the tool

| The task | Use |
|---|---|
| Keys are small integers (below about 10⁶) or bytes or letters | a plain array or `vector` indexed by the key; no hashing at all |
| Look up, count or deduplicate arbitrary keys, order not needed | `unordered_map` / `unordered_set`; call `reserve(n)` if you know the size |
| Order, ranges, next larger key | `std::map` / `std::set`, or a sorted `vector` |
| Millions of lookups in a hot path | a flat open-addressing map (Abseil, Boost, or the table of section 5) |
| Key is a pair or a record | a custom hash built with `hashCombine` and a matching `operator==` |
| Keys come from the network or a user | a keyed hash with a secret (section 8); never the identity or a Java-style hash |
| Pointers or references to entries must survive inserts | `std::unordered_map` (node based); flat tables move entries |
| A few dozen entries | a `vector` and a linear search often wins |

---

## 12. Mistakes that cost points

1. **Equal keys with different hashes.** Hashing a pointer when `==` compares text, hashing a field that `==` ignores, or `-0.0` against `0.0`. The lookup goes to the wrong bucket and the key "disappears".
2. **Xor to combine hashes.** Symmetric, and `(a, a)` is always 0. Combine with an order-sensitive mix.
3. **Identity hash with a power-of-two mask** on keys that share low bits (addresses, ids, timestamps, multiples of 1024).
4. **Open addressing: emptying a slot on erase.** Keys beyond it become unreachable. Use a tombstone or shift entries back.
5. **Not counting tombstones in the load.** A table of tombstones with no `Empty` slot makes `find` loop forever.
6. **`m[key]` to test for presence.** It inserts. Use `find`, `count` or `contains`.
7. **Holding an iterator, pointer or reference across inserts.** A rehash invalidates iterators; flat tables invalidate pointers too.
8. **Erasing inside a range-for loop.** Write `it = m.erase(it)` in an iterator loop.
9. **Saying "O(1)" without "expected".** The worst case is O(n), a string key costs O(length), and a single insert can rehash everything.
10. **A predictable hash on hostile keys.** Anything a client chooses (parameter names, ports, MAC addresses) needs a secret in the hash.
11. **No `reserve`** when you know the size: you pay about 1.3 moves per insert and a latency spike for nothing.
12. **`std::hash<const char*>`** as a string hash, and `std::pair` as a key without a hash (it does not compile).

---

## 13. Questions you will be asked

**How does a hash table work, and what does an operation cost?** Hash the key, reduce the hash to a bucket, search that bucket. Expected O(1) if the hash spreads keys evenly and the load factor is bounded; O(n) in the worst case. Inserts are amortised O(1) because the table doubles.

**Chaining or open addressing?** Chaining: simple, erase is easy, works above load 1, but a node and a cache miss per entry. Open addressing: flat arrays and fewer cache misses, but it needs load below about 0.7, it clusters, and erase needs tombstones or shifting.

**Why can't you empty a slot when you erase in open addressing?** A key may sit beyond that slot because the slot was full when the key was inserted. An empty slot ends a search, so that key would no longer be found. Use a tombstone (counted in the load, reused by insert, dropped by rehash) or shift the rest of the run back.

**What is the load factor, and what happens as it grows?** n / m. A chained miss costs α comparisons; a linear-probing miss costs `½ (1 + 1/(1 - α)²)`, about 50 probes at 0.9. The table doubles and re-inserts everything when α passes its limit.

**How do you hash a pair or a struct?** Mix each field and combine in an order-sensitive way (`hashCombine`), hash exactly the fields `operator==` compares, and never xor.

**Power-of-two or prime size?** Power of two: a mask instead of a division, but the hash must mix its low bits (mix first, or take the top bits as Fibonacci hashing does). Prime: tolerates weak hashes, costs a division.

**How can someone break a hash table, and what is the defence?** By choosing keys that collide, which turns n operations into O(n²). Defences: a secret random key in the hash (SipHash), universal hashing, trees in long buckets (Java). A fixed hash, even a good mixer, can be run backwards.

**Why can `std::unordered_map` be slow, and what do you use instead?** A heap node per entry costs allocation and cache misses, and integers are reduced with a division. For hot paths use a flat open-addressing map with `reserve`; for small integer keys use an array.

---

## 14. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). First ask: what is the key, what must be equal for two keys to be "the same", and what does a collision cost here?

### On paper

**Problem A: Chain it.** Use `h(x) = x mod m`, start with `m = 4` and double when `size > m` (section 4). Insert 8, 4, 12, 3, 7, 6 in this order. Draw the non-empty chains after each insert, say when the rehash happens, and count the keys compared by `find(12)` and `find(20)` in the final table.
*Where:* paper. *How:* the trace of section 4.

**Problem B: Probe it.** Linear probing, capacity 8, `h(x) = x mod 8`, no resizing. Insert 5, 13, 6, 14, 21 and draw the slots (watch the wrap-around). Erase 13. Give the number of probes of `find(14)`, `find(22)` and `find(21)`, then insert 29 and say which slot it takes.
*Where:* paper. *How:* the trace of section 5.

**Problem C: Birthday.** (a) A 24-bit hash has 16777216 values. About how many items give an even chance that two of them share a hash? (b) You fingerprint 10⁹ items with a 64-bit hash. What is the expected number of colliding pairs?
*Where:* paper and a calculator. *How:* section 3; for (b) the number of pairs is `n(n - 1)/2`, each colliding with probability `1/m`.

**Problem D: Amortise it.** A table starts with 1000 buckets. When entry number `capacity + 1` arrives, every entry (the new one included) is moved to a bigger table. Count the entries moved by 1000000 inserts if the table grows (a) by 1000 buckets each time, (b) by doubling.
*Where:* a few lines of code or a spreadsheet. *How:* write down the sizes at which a rehash happens, then add up what each one moves (section 6).

### In code

**Problem 1: Design HashSet** (LeetCode 705, easy)
*Where:* leetcode.com/problems/design-hashset. *Goal:* implement `add`, `remove` and `contains` without a built-in hash table.
*How:* section 4: an array of buckets, each a `vector<int>`, bucket = `key % 1009` (a prime). Then add the resizing of section 6, and compare with the "bit array of 10⁶ + 1 flags" solution that the key range allows.

**Problem 2: Design HashMap** (LeetCode 706, easy)
*Where:* leetcode.com/problems/design-hashmap. *Goal:* `put`, `get` (returns -1 if absent) and `remove`.
*How:* the same table with `(key, value)` pairs; `put` must update an existing key instead of adding a second entry.

**Problem 3: Insert Delete GetRandom O(1)** (LeetCode 380, medium)
*Where:* leetcode.com/problems/insert-delete-getrandom-o1. *Goal:* `insert`, `remove` and `getRandom` (each element equally likely), each in average O(1).
*How:* a hash map alone cannot pick a random element, and a vector alone cannot find one. Keep both: a vector of the values and a map from value to position. Think about how to remove from the middle of a vector in O(1) (the `erase` of section 4). Then try LeetCode 381, where duplicates are allowed.

**Problem 4: Delete without tombstones** (own code)
*Where:* your editor. *Goal:* give `LinearProbingTable` an `erase` that leaves no tombstone, and test it against `std::unordered_map` the way the companion does.
*How:* after emptying slot `i`, walk forward to the end of the run. An entry at slot `j` whose home slot lies **cyclically outside** `(i, j]` would no longer be reachable, so move it into `i` and continue from its old place. Draw the wrap-around case first.

**Problem 5: Break it, then fix it** (own code)
*Where:* your editor. *Goal:* build a `std::unordered_map<unsigned long long, int>` in which all N keys share one bucket, time the inserts for N = 5000, 10000, 20000 and 40000, then repeat with `RandomizedHash` as the third template argument.
*How:* `reserve(N)`, read `bucket_count()`, insert the keys `i * bucket_count()`. Predict the ratio of the times before you run it.

**More practice, once these feel easy:** Contains Duplicate (217), Intersection of Two Arrays (349), Insert Delete GetRandom with duplicates (381), Encode and Decode TinyURL (535), Longest Consecutive Sequence (128, tutorial 02). Write a table that stores `std::string` keys, and a version of the companion's `LinearProbingTable` with quadratic probing, and compare the probe counts with the formulas. NeetCode's "Arrays & Hashing" list has video walkthroughs.

---

## Answers

Try before reading. LeetCode wraps these functions in `class Solution { public: ... };`; the logic is the same.

### Problem A

| Insert | Non-empty chains afterwards |
|---|---|
| 8 | `0: [8]` |
| 4 | `0: [8, 4]` |
| 12 | `0: [8, 4, 12]` |
| 3 | `0: [8, 4, 12]`, `3: [3]` (size 4 is not above 4) |
| 7 | size 5 > 4: **rehash to 8 buckets**: `0: [8]`, `3: [3]`, `4: [4, 12]`, `7: [7]` |
| 6 | `0: [8]`, `3: [3]`, `4: [4, 12]`, `6: [6]`, `7: [7]` |

`find(12)` goes to bucket 4 and compares 4, then 12: **2** comparisons, found. `find(20)` also goes to bucket `20 mod 8 = 4`, compares 4 and 12, and gives up after **2**.

### Problem B

| Step | Slots 0 to 7 |
|---|---|
| put 5 | `. . . . . 5 . .` |
| put 13 | `. . . . . 5 13 .` (home 5 taken) |
| put 6 | `. . . . . 5 13 6` (home 6 taken) |
| put 14 | `14 . . . . 5 13 6` (home 6 and 7 taken, wraps to 0) |
| put 21 | `14 21 . . . 5 13 6` (home 5, 6, 7 and 0 taken) |
| erase 13 | `14 21 . . . 5 D 6` |

`find(14)`: slots 6 (tombstone), 7, 0 give **3** probes. `find(22)`: home 6, then 7, 0, 1 and the empty slot 2 give **5** probes. `find(21)`: slots 5, 6, 7, 0, 1 give **5** probes. `put(29)` has home 5, walks 5, 6 (tombstone, remembered), 7, 0, 1 and stops at the empty slot 2, so 29 is new and takes the tombstone slot **6**: `14 21 . . . 5 29 6`.

### Problem C

(a) The even-chance point is about `√(2 ln 2 × m)`, which for `m = 2^24` is **4823** items (1.18 × 4096). (b) `n(n - 1) / 2 / 2^64` with `n = 10⁹` is **0.0271**: about a 2.7% chance that some pair collides, which is small but not zero, so a deduplication system that trusts a 64-bit fingerprint accepts that risk.

### Problem D

(a) A rehash happens at the sizes 1001, 2001, ..., 999001, that is **999 rehashes**, and they move **499500999** entries, about `n² / 2000`. (b) The sizes are 1001, 2001, 4001, ..., 512001: **10 rehashes** moving **1023010** entries. Growing by a constant is about 488 times more work.

### Problem 1: Design HashSet

Section 4 with a fixed number of buckets: `vector<vector<int>> buckets(1009)`, `add` scans the chain and appends if absent, `remove` swaps with the last and pops, `contains` scans. O(n / 1009) per operation, which is fine for the 10⁴ calls LeetCode makes; with resizing (section 6) it is O(1) expected. The bit-array version is `vector<bool>(1000001)`: O(1) worst case, 125 KB.

### Problem 2: Design HashMap

The same, with `pair<int, int>` entries. `put` first scans for the key and overwrites the value; `get` returns the value or -1; `remove` is as above.

### Problem 3: Insert Delete GetRandom O(1)

```cpp
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
```

The key line is in `remove`: the last value moves into the removed value's place, so the vector never has a hole, and the map is updated **before** the erase, which also makes the case `value == last` come out right. All three operations are O(1) on average. For LeetCode, `getRandom` takes no argument and uses `rand()` in place of `rng`. For 381 (duplicates) keep an `unordered_map<int, unordered_set<int>>` of positions per value.

### Problem 4: Delete without tombstones

After emptying slot `i`, let `j` run forward from `i + 1` until an empty slot. For the entry at `j` with home slot `k`: if `k` lies cyclically in `(i, j]` (that is `i < k <= j` when `i <= j`, and `k > i or k <= j` when the run wraps around), the entry can stay, because its probe path starts after the hole. Otherwise move it into slot `i`, empty slot `j`, and set `i = j`. Stop at the first empty slot. The table then looks exactly as if the erased key had never been inserted, so no tombstone and no `used_` bookkeeping are needed.

### Problem 5: Break it, then fix it

Expect the time to grow about 4 times for every doubling of N, because inserting the j-th key walks a chain of length j. On the test machine (`-O2`, libstdc++) the four sizes took about 0.04, 0.16, 0.65 and 2.6 seconds. With `RandomizedHash` the same keys behave like random keys: milliseconds, and `bucket_size` stays in single digits.

---

## Cheat sheet

- **Idea:** `bucket = h(key) mod m`; equal keys must have equal hashes; the table compares keys. Expected O(1) insert, find and erase; O(n) worst case; no order.
- **Hash functions:** integers need mixing (`mix64`, or Fibonacci hashing with the **top** bits); strings are hashed over all bytes (O(length)); pairs use an order-sensitive combine, never xor.
- **Collisions:** the first one comes after about `1.18 √m` keys (birthday), so every table must handle them. `P(collision) ≈ 1 - e^(-n(n-1)/2m)`.
- **Chaining:** a miss compares `α` keys, a hit `1 + (n - 1)/(2m)`. **Linear probing:** a hit probes `½ (1 + 1/(1 - α))` slots, a miss `½ (1 + 1/(1 - α)²)`; keep α under about 0.7. Erase with a tombstone, and count tombstones in the load.
- **Growth:** double and re-insert everything; amortised O(1) (fewer than 2 moves per insert) with O(n) spikes; `reserve` when the size is known.
- **Universal hashing:** `((a x + b) mod p) mod m` with random `a`, `b`: collision probability at most `1/m` for any two keys.
- **Flooding:** attacker keys in one bucket turn n operations into O(n²); a fixed hash can be inverted; use a secret key (SipHash) or trees in buckets.
- **`std::unordered_map` (libstdc++):** node per entry, identity hash for integers, prime bucket counts, max load 1.0, references survive rehash, iterators do not, `m[key]` inserts.

---

## Sources

- Counts, outputs and timings in this tutorial were produced by running the code shown in it on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** Hashing tutorial 02, hashing patterns (seen-sets, counting and top K, two-sum style lookups, canonical keys, longest consecutive sequence; the prefix-sum map is in Arrays 04). Say "next" when you are ready.
