# DSA 02.03: Design a Hash Map

*Part of the [Hashing plan](00_plan.md) · Time: about 6 hours (2.5 reading, 3.5 problems) · You need: tutorial [01](01_hash_tables_inside.md) (chaining, linear probing, tombstones, load factor, `mix64`) and [02](02_hashing_patterns.md) (what programs do with a table) · Companion code: [`03_design_a_hash_map_demo.cpp`](03_design_a_hash_map_demo.cpp)*

By the end of this tutorial you can:

- say what `std::unordered_map` promises, why that forces one heap node per element, and what a flat table gives up to be faster;
- write Robin Hood hashing with backward-shift deletion and trace an insert and an erase by hand;
- explain cuckoo hashing (two buckets, kicks, rebuild) and how the bucket size moves the usable load from about 50% to above 95%;
- explain the Swiss-table idea: control bytes, 7-bit tags, matching a whole group with a few bit operations, and the rule for when an erase may free a slot;
- say, for each design, when entries move and what that does to pointers and iterators;
- build an LRU cache from a hash map and a list, with O(1) per operation;
- choose a design from the requirement: miss-heavy lookups, a hard latency bound, memory, many deletes, stable pointers.

The classes are in the companion file. It tests each one against `std::unordered_map` or `std::unordered_set` on random operation sequences, checks the table's own invariants as it goes, and prints every number quoted below. It compiles with `-Wall -Wextra -fsanitize=address,undefined`. The tables are teaching code (64-bit keys, simple growth), so the measurements describe them, on one machine, and not the production libraries.

---

## 1. What are we choosing between?

**The contract.** A map needs `find`, `put` (insert or update) and `erase`, each O(1) expected. `std::unordered_map` promises more: pointers and references to elements stay valid when the table grows (a rehash invalidates iterators, not references), an erase invalidates only the erased element, and the bucket interface (`bucket_count`, `bucket(k)`, `max_load_factor`) is part of the type. In effect only chaining with one heap node per element satisfies this (tutorial 01, section 9).

A table that drops those promises can be a few flat arrays. That is why the fast libraries (Abseil's `flat_hash_map`, Boost's `unordered_flat_map`, Go 1.24 maps) are flat. The price is that **entries move**, so a pointer into the table can go stale. All designs below are flat, and they answer four questions differently:

1. How many slots does a lookup touch on a hit, and on a miss? (A miss is the costly case in open addressing, because it has to prove that the key is absent.)
2. How full may the table get before it must grow?
3. What does an erase leave behind?
4. When do entries move?

**Where linear probing stands.** Tutorial 01 gave the expected probes at load α: a hit takes ½(1 + 1/(1 - α)) and a miss ½(1 + 1/(1 - α)²). At α = 0.5 that is 1.5 and 2.5; at α = 0.9 it is 5.5 and 50.5. The mean is acceptable, the **spread** is the problem: a few unlucky keys sit at the end of long clusters, and a miss must walk to the end of one. Robin Hood hashing attacks the spread.

---

## 2. Robin Hood hashing

### 2.1 The idea

In linear probing the first key to arrive keeps its home slot and later keys walk past it. Robin Hood changes one rule. Every entry remembers its **probe length**: 1 if it sits in its home slot, 2 if one slot further, and so on. When an insert reaches a resident whose probe length is **smaller** than the newcomer's current one, the two swap: the newcomer takes the slot, and the resident, which was close to home, carries on walking. Whoever has walked furthest is served first, so no entry ends up much further from home than the others.

Two consequences matter.

- Along a run of occupied slots, the probe length grows by at most 1 from one slot to the next.
- **Early stop.** A search for `x`, now at distance `d` from its home, can stop at the first slot whose resident has a probe length below `d`. If `x` were stored further on, it would have walked through this slot at distance `d` when it was inserted and swapped with this resident. Later inserts and erases keep that true. So a miss no longer walks to the next empty slot.

### 2.2 The code

```cpp
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

    // (helper functions for the tests are in the companion file)
};
```

- Three flat arrays; `probe_[i] == 0` marks an empty slot. The capacity is a power of two and `home` is a mask of the mixed hash (tutorial 01, section 2.5).
- `locate` is the search with the early stop, shared by `find` and `erase`; `examined_` counts the slots it looks at, for the experiments below.
- `insertNew` carries "the entry in hand" (`key`, `value`, `d`); each swap puts the newcomer in the slot and the old resident in hand. Keys are distinct, so no comparison is needed.
- `erase` uses **backward shift**: after emptying slot `i`, pull each following entry one slot back (its probe length drops by 1) until the next slot is empty (0) or holds an entry already at home (1), which cannot move closer. No tombstone is left, so deleted entries never lengthen a later search.
- `put` grows first when the new size would pass `maxLoad_` (0.9 by default); `grow` doubles the capacity and reinserts every entry.

### 2.3 A trace

Capacity 8, and to keep the numbers small the home slot is `key mod 8` (the companion test forces these homes on the real class by choosing keys). `3:1` means key 3 with probe length 1; `.` is an empty slot.

| Step | Table, slots 0 to 7 | What happened |
|---|---|---|
| insert 3 | `. . . 3:1 . . . .` | home 3, empty |
| insert 4 | `. . . 3:1 4:1 . . .` | home 4, empty |
| insert 11 | `. . . 3:1 11:2 4:2 . .` | home 3. Slot 3 holds 3:1, and 1 is not below 1, so 11 moves on. Slot 4 holds 4:1, and 1 < 2: 11 takes slot 4 and **4 walks on** to slot 5 with probe length 2 |
| insert 19 | `. . . 3:1 11:2 19:3 4:3 .` | home 3. Slot 4 holds 11:2, not below 2. Slot 5 holds 4:2, and 2 < 3: 19 takes slot 5 and 4 walks on to slot 6 with probe length 3 |
| find 27 | | home 3. Slots 3, 4, 5 hold probe lengths 1, 2, 3, not below 1, 2, 3. Slot 6 holds 3, below 4: stop, **absent after 4 slots**. Plain linear probing walks on to the empty slot 7: 5 slots |
| erase 11 | `. . . 3:1 19:2 4:2 . .` | slot 4 is emptied. Slot 5 (19:3) moves back to slot 4 as 19:2, slot 6 (4:3) moves back to slot 5 as 4:2, slot 7 is empty: stop |

### 2.4 What it buys

Insert random 64-bit keys up to a given load into a linear-probing table and into the Robin Hood table above (capacity 65536, load limit 1.0 so that nothing grows), then measure the probe length of every entry and the slots examined by about 20000 searches for absent keys. One run:

| Load | Hit: mean | Hit: std. dev. (linear / Robin Hood) | Longest probe (linear / Robin Hood) | Miss: slots examined (linear / Robin Hood) |
|---|---|---|---|---|
| 0.50 | 1.49 | 1.20 / 0.79 | 20 / 7 | 2.49 / 1.74 |
| 0.70 | 2.17 | 3.17 / 1.49 | 92 / 14 | 6.15 / 2.53 |
| 0.80 | 3.04 | 6.34 / 2.42 | 179 / 21 | 13.29 / 3.45 |
| 0.90 | 5.64 | 18.14 / 5.02 | 615 / 38 | 52.67 / 6.11 |
| 0.95 | 11.15 | 61.29 / 10.82 | 2672 / 72 | 215.26 / 11.61 |

- **The mean probe length of a hit is identical.** Both tables occupy exactly the same slots, because Robin Hood only changes which entry of a run sits where. The total probe length depends only on which slots are occupied and on the keys' home slots, so it is the same too. The companion checks both facts. The values are also close to the formula of section 1 (5.5 at load 0.9, 10.5 at 0.95).
- **The spread collapses.** At 0.9 the longest probe drops from 615 to 38.
- **A miss costs about as much as a hit** (6.11 against 5.64 at 0.9), because of the early stop. Linear probing pays 52.67. That is the reason Robin Hood tables are run at loads around 0.9.
- The price is more writes: an insert can shift a whole run, and each slot stores a probe length.

### 2.5 Entries move

In the trace, key 4 started in slot 4, was pushed to slot 5 by the insert of 11, to slot 6 by the insert of 19, and pulled back to slot 5 by the erase of 11. A pointer from `find(4)` taken before those operations would now point at a different entry. So do not keep a pointer or reference into a flat table across a `put` or an `erase`: use it at once, or copy the value out. Section 5 compares when each design moves entries.

---

## 3. Cuckoo hashing

### 3.1 The idea

Every key has **two** candidate buckets, given by two hash functions, and each bucket has `B` slots. A key is always stored in one of its two buckets. So:

- **find** reads at most two buckets, however full the table is;
- **erase** frees the slot, with no tombstone;
- **insert** uses a free slot in either bucket if there is one. Otherwise it picks a resident of the first bucket, puts the newcomer in that slot, and sends the evicted key to *its* other bucket. If that bucket is full too, it evicts again, and so on (the way a cuckoo chick pushes eggs out of the nest). After `kMaxKicks` kicks the insert gives up, and the table is rebuilt with new hash functions and more room.

### 3.2 A trace

Four buckets, `B = 1`, and hand-made bucket pairs `(h1, h2)`: A is `(0, 1)`, E is `(1, 2)`, D is `(0, 1)`. (The real class derives both buckets from `mix64`; the trace uses small numbers.)

| Step | Buckets 0 to 3 | What happened |
|---|---|---|
| insert A | `A . . .` | bucket 0 is free |
| insert E | `A E . .` | bucket 1 is free |
| insert D | `D A E .` | buckets 0 and 1 are full. D takes bucket 0 and **A is evicted**; A's other bucket is 1, holding E, so A takes bucket 1 and **E is evicted**; E's other bucket is 2, which is free. Two kicks |

`find(A)` reads buckets 0 and 1 and finds A in bucket 1. It never reads bucket 2 or 3.

### 3.3 The code

```cpp
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

    // (helper functions for the tests are in the companion file)
};
```

- `bucket1` and `bucket2` are `mix64` with two different seeds; every rebuild draws new seeds.
- `insertNoRebuild` tries both buckets, then runs the kick loop: pick a random slot of the current bucket (`rng_.below(B)`), swap it with the key in hand, and continue with the evicted key in its other bucket. If it gives up it returns the key still in hand.
- `rebuild` collects every key, including that one, and inserts them all into a table with twice the buckets and new seeds, trying larger tables until all fit. `put` records the number of keys at the first failure for the experiment below.

### 3.4 How full can it get?

Treat each bucket as a node and each key as an edge between its two buckets. If three keys all have the same two buckets and `B = 1`, two slots cannot hold three keys, whatever algorithm is used. At random this happens once the load passes about 50% for `B = 1`. The kick search is a random walk and can fail even when a placement exists, so the usable load is a little below the theoretical one. Larger buckets tolerate much more:

| `B` | First failure at load (min / mean / max over 8 seeds) |
|---|---|
| 1 | 0.493 / 0.510 / 0.525 |
| 2 | 0.867 / 0.875 / 0.882 |
| 4 | 0.958 / 0.964 / 0.968 |
| 8 | 0.988 / 0.991 / 0.992 |

(65536 slots, random 64-bit keys, 500 kicks per insert.)

### 3.5 What it is good for

- **A hard bound on lookups.** Two bucket reads, independent of the load, so the worst case equals the typical case, and both buckets can be requested from memory at once. With `B = 8` and 8-byte keys a bucket is one 64-byte cache line if the array is aligned.
- **Inserts are the weak spot.** Near the limit the kick chains get long, and a failure rebuilds the whole table (O(n)). Entries move on insert (the kicks), not on erase.
- DPDK's `rte_hash` is a cuckoo table: a key has a primary and a secondary bucket, each entry keeps a short signature of its hash that is compared before the key (the idea of the next section), and a batch lookup prefetches the next entries while it works on the current ones (see the sources at the end).

---

## 4. The Swiss-table idea

### 4.1 The idea

Keep a separate array with one **control byte** per slot. For a full slot the control byte is the low 7 bits of the key's hash, the **tag** (called H2). Two other values have the top bit set: empty (`0x80`) and deleted (`0xFE`). The rest of the hash (H1) picks the first **group** of slots; here a group is 8 slots, so its 8 control bytes fit in one 64-bit word. A lookup then goes:

1. Hash the key. H2 is the low 7 bits, H1 is the rest.
2. Load the control word of the group that H1 selects and find every byte equal to H2 with a few bit operations. These are the candidate slots.
3. Compare the key only in candidate slots (a wrong candidate has probability about 1/128 per full slot).
4. If the group has an empty byte, the key is absent: stop. Otherwise continue with another group.

A lookup therefore reads one small control word and, usually, one key. Abseil matches 16 bytes at a time with SSE (`_mm_cmpeq_epi8`, then `_mm_movemask_epi8`); Go 1.24 uses groups of 8 slots, a 64-bit control word, an 8-byte SIMD operation on amd64 and a portable fallback of ordinary arithmetic (see the sources). The code below uses the portable style, so it runs on any CPU.

### 4.2 Matching a group with bit operations

```cpp
// One control byte per slot; the eight control bytes of a group sit in one 64-bit word and are examined together.
// A full slot holds the low 7 bits of its key's hash (the tag); the two other states have the high bit set.
const uint8_t kEmpty = 0x80, kDeleted = 0xFE;

const uint64_t kLow = 0x0101010101010101ULL, kHigh = 0x8080808080808080ULL;
```

```cpp
// Bit 7 of each byte that equals tag. The subtraction can also flag a byte right above a real match (a false
// positive); the caller compares the keys anyway.
uint64_t matchTag(uint64_t group, uint8_t tag) {
    uint64_t x = group ^ (kLow * tag);                        // bytes equal to the tag become 0
    return (x - kLow) & ~x & kHigh;                           // flag the bytes that became 0
}

uint64_t matchEmpty(uint64_t group) { return group & ~(group << 6) & kHigh; }   // exactly 0x80: bit 7 set, bit 1 clear

uint64_t matchFree(uint64_t group) { return group & kHigh; }                    // empty or deleted
```

`matchTag` first XORs the group with the tag copied into every byte (`kLow * tag`), so a byte that equals the tag becomes `0x00`. Then `(x - kLow) & ~x & kHigh` flags the zero bytes: subtracting 1 turns `0x00` into `0xFF`, and the byte's top bit was clear before. The subtraction **borrows** from the byte above, so a byte directly above a zero byte can be flagged too: it must be `0x01` (the tag XOR 1), because `0x01 - 1 - 1 = 0xFF`. The result can contain **false positives but never misses**, and its lowest flagged byte is always a true match (the companion tests both on random groups). Every candidate is compared with the real key anyway, so a false positive costs one comparison, never a wrong answer.

An example: the group `11 2A 2B 80 2A FE 05 40` (slot 0 first) and the tag `2A`.

| Slot | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| control byte | 11 | 2A | 2B | 80 | 2A | FE | 05 | 40 |
| after XOR with 2A | 3B | 00 | 01 | AA | 00 | D4 | 2F | 6A |
| flagged by `matchTag` | | yes | yes | | yes | | | |

Slots 1 and 4 are real matches. Slot 2 is a false positive: the `01` sits directly above the zero in slot 1. `matchEmpty` flags exactly `0x80`: top bit set and bit 1 clear (`0xFE` has bit 1 set, a full byte has the top bit clear). It flags slot 3. `matchFree` flags every byte with the top bit set, here slots 3 and 5.

### 4.3 The code

```cpp
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

    // (helper functions for the tests are in the companion file)
};
```

- `ctrl_` has one word per group, `keys_` the keys in slot order (slot = group * 8 + position). A control byte below `0x80` is a tag, so a full slot is one with the top bit clear.
- `findSlot` tests the tag candidates of a group and stops at a group with an empty byte. Otherwise it moves on by 1, 2, 3, ... groups (offsets 1, 3, 6, ... from the start), which reaches every group when the number of groups is a power of two. `compared_` counts key comparisons for the experiments.
- `insertNew` takes the first free slot (empty or deleted) of the first group on the sequence that has one.
- `growthLeft_` is how many empty slots may still be used. It starts at 7/8 of the capacity, so at least 1/8 of the slots stay empty and every search ends. Reusing a deleted slot costs nothing.
- When `growthLeft_` reaches 0, `rehash` **doubles** the table if more than half of the allowed entries are live (`size_ > groups * 8 * 7 / 16`), and otherwise rebuilds at the **same size** just to drop the tombstones.

### 4.4 Erase: empty or tombstone?

A search continues past a group only if the group has no empty byte. So a slot may become empty again only if no search has ever continued past its group, and that is exactly the case when the group still has an empty byte. The rule in `erase`: if the group has an empty byte, write empty (and give the slot back to `growthLeft_`); otherwise write deleted.

Why is "has an empty byte now" enough? Only an insert removes empty bytes, and `erase` writes empty only into a group that already has one, so a group without an empty byte stays without until the next rebuild. A group that has one has never been full since then.

An example with two groups of 8 slots. Fill group 0 with 8 keys that start there. A ninth key X that also starts in group 0 finds it full and lands in group 1. Now:

- **Erase one of the 8 keys of group 0.** The group has no empty byte, so the slot becomes deleted. A search for X still passes group 0 and finds X in group 1. (With an empty byte in group 0, the search would stop there and X would be lost.)
- **Erase X.** Group 1 has empty bytes, so the slot becomes empty again.

The companion runs this exact scenario, and a second one that fills a table with tombstones to check that `rehash` cleans them without growing.

### 4.5 Measured

Fill 65536 slots to the 7/8 limit (57344 random keys) and count key comparisons: **0.17 per search for an absent key** and **1.03 per search for a present key**. So at that load more than four out of five searches for an absent key never look at a stored key.

---

## 5. Comparing the designs

| | Chaining (`std::unordered_map`) | Linear probing (01) | Robin Hood | Cuckoo, `B = 4` | Swiss-style |
|---|---|---|---|---|---|
| Usual load limit | 1.0 | about 0.7 | about 0.9 | about 0.95 (first failure at 0.96) | 7/8 |
| Hit | one node, plus the chain before it | grows with the load | same mean as linear, small spread | at most 2 buckets | 1 control word, about 1 key |
| Miss | about α nodes | grows fast | about the cost of a hit | exactly 2 buckets | 1 control word, 0.17 keys |
| Erase | free the node | tombstone, or shift back | backward shift, no tombstone | free the slot | empty or tombstone (4.4) |
| Entries move on | never | erase, if shifting back; growth | insert, erase, growth | insert (kicks), growth | growth only |
| Awkward case | long chains, a miss per node | clusters | long shifts on insert and erase | kick chains, rebuild on failure | rebuild spike |

"Entries move on" describes the classes in this tutorial. Production flat maps promise less than this table suggests (an insert may invalidate everything), so rely on the library's documentation, not on the table.

**Speed, for information only.** 700000 random 64-bit keys inserted into a table sized in advance (about a million slots; the standard set got `reserve(700000)`), then 700000 lookups of present keys and 700000 lookups of absent keys. Best of six repetitions on a 2-core 2.1 GHz Xeon virtual machine, GCC 13.3, `-O2`, libstdc++:

| Table | Insert | 700000 hits | 700000 misses |
|---|---|---|---|
| `std::unordered_set` | 0.049 s | 0.017 s | 0.025 s |
| `RobinHoodMap` | 0.023 s | 0.016 s | 0.016 s |
| `CuckooSet<4>` | 0.030 s | 0.019 s | 0.026 s |
| `SwissSet` | 0.015 s | 0.009 s | 0.007 s |

Treat the ratios as a hint. The flat tables are teaching code (64-bit keys, no allocator support; the Robin Hood table also stores a value), the keys are random, and a different machine, compiler or key distribution can change the order of the three flat tables. What they share is no allocation and no pointer per entry.

**Growth and the latency spike.** Every table here grows by allocating arrays twice as large and reinserting everything: O(1) amortised, but one unlucky insert costs O(n) and invalidates every pointer. For a data path there are three answers. Size the table once for its maximum. Grow incrementally: Redis keeps two tables while it grows, sends new writes to the new one, looks in both, and moves a few entries across with every operation; Go 1.24 maps are split into tables of at most 1024 entries that grow independently, so one insert does a bounded amount of growth work. Or evict when full, which makes it a cache (section 6).

**Iterating over a flat table** means scanning the arrays in slot order. A rehash changes the order, and an insert during the scan may make it skip or repeat entries. When you erase during a scan of the Robin Hood table, do **not** advance afterwards: the backward shift has just put the next entry into the slot you are standing on. The Swiss-style table erases without moving anything, so a scan may erase the current entry and go on.

---

## 6. An LRU cache

A cache of fixed capacity that, when full, drops the **least recently used** entry. `get(k)` returns the value or -1 and marks `k` as most recently used; `put(k, v)` inserts or updates `k`, marks it as most recently used, and evicts the oldest entry if the capacity is exceeded. Both must be O(1) (LeetCode 146).

Two needs, two structures. Finding an entry by key is a hash map. Moving an entry to the front and removing the last one is a doubly linked list *if you already hold the node*. So the map stores, for each key, an iterator to its node in the list.

```cpp
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
```

- `items_` is ordered from the most recently used to the least. `where_` maps a key to its list node.
- `std::list::splice` relinks a node at the front without allocating, copying or invalidating any iterator, so the iterators stored in `where_` stay valid when nodes move.
- Eviction removes the **map entry first** (it needs the key from the node) and then the node. Forgetting the map entry leaves a dangling iterator.
- `put` on an existing key updates the value **and** moves it to the front.

The LeetCode example, capacity 2. The list is shown most recent first, as `key:value`:

| Operation | Result | List afterwards |
|---|---|---|
| `put(1, 1)` | | `1:1` |
| `put(2, 2)` | | `2:2 1:1` |
| `get(1)` | 1 | `1:1 2:2` |
| `put(3, 3)` | evicts key 2 | `3:3 1:1` |
| `get(2)` | -1 | `3:3 1:1` |
| `put(4, 4)` | evicts key 1 | `4:4 3:3` |
| `get(1)` | -1 | `4:4 3:3` |
| `get(3)` | 3 | `3:3 4:4` |
| `get(4)` | 4 | `4:4 3:3` |

Notes for the interview:

- **A `get` writes.** It reorders the list, so a cache shared by threads cannot serve `get` under a shared (read) lock; use one mutex, or shard the cache by key.
- **Cost.** A list node holds two pointers on top of the entry, and the map adds a node of its own. A production cache uses links stored in an array of entries for less memory and fewer allocations.
- **Variants.** An entry with an expiry time (TTL) is dropped lazily when a `get` finds it expired. LFU evicts the *least frequently* used entry and needs a list per frequency (Problem 3). Operating systems approximate LRU rather than keep it exact, because an exact order needs an update on every access (Linux keeps active and inactive lists of pages).

---

## 7. Where this shows up in systems

- **Flat maps in libraries.** Abseil's `flat_hash_map` and Go 1.24's built-in `map` are Swiss-style tables (section 4).
- **Flow and connection tables.** DPDK's `rte_hash` is a cuckoo table with signatures, used for packet flow lookups, where a bounded number of memory reads matters more than insert speed.
- **Tables with a fixed size.** A driver or firmware that must not allocate after start-up keeps a presized power-of-two array of entries (a station table keyed by MAC address, say) and an eviction rule for when it is full. Robin Hood with backward shift fits, because no tombstones accumulate, and so does an LRU or aging order.
- **Caches.** DNS, neighbour (ARP) and file-system metadata caches are a hash map plus an order of use, as in section 6.

**Choosing.** Stable pointers: `std::unordered_map` or a node-based map. General speed and memory: a Swiss-style map from a library (do not write your own for production). Many misses: Swiss-style or Robin Hood. Many erases: Robin Hood. A hard bound on lookup reads in a mostly-read table: cuckoo with `B = 4` or 8. No allocation after start-up: a presized flat table plus an eviction rule. Stable handles with flat speed: the table stores indices into a dense array of entries. Fewer than a few dozen entries: a `vector` and a linear scan.

---

## 8. Mistakes that cost points

1. **Holding a pointer, reference or iterator into a flat table across a `put` or an `erase`.** Entries move (section 5).
2. **Emptying the slot on erase** in open addressing without a shift or a tombstone. Keys beyond it are lost (tutorial 01).
3. **Erase while scanning a Robin Hood table, then stepping forward.** The next entry was just shifted into the current slot, so it is skipped.
4. **Not counting tombstones in the load.** The Swiss-style `growthLeft_` charges a tombstone like a full slot, so a table never runs out of empty slots.
5. **Taking the tag from the same bits as the group index.** Every key in a group then has the same tag and every slot is a candidate. Use disjoint bits (H1 and H2).
6. **Two cuckoo hash functions that are the same function.** Every key then has one bucket, and the load limit drops far below 50%.
7. **LRU: evicting the node but not the map entry,** or updating a value without moving the entry to the front.
8. **Trusting a benchmark from another machine,** including the one in section 5.

---

## 9. Questions you will be asked

**Why is `std::unordered_map` slow compared with flat maps?** It must keep references valid when it grows and exposes buckets, so it is chaining with one allocated node per element. Every lookup follows pointers to scattered memory. Flat maps keep entries in a few arrays and give those promises up.

**What does Robin Hood hashing do?** An insert that meets a resident with a smaller probe length swaps with it, so entries that walked far are served first. The mean probe length stays the same as in linear probing, the spread collapses, a search can stop early, and backward-shift deletion needs no tombstones.

**How does cuckoo hashing guarantee two memory reads per lookup?** A key is only ever stored in one of its two buckets. Inserts pay for it: a chain of evictions, and a rebuild if the chain is too long.

**What is a Swiss table?** One control byte per slot (7 bits of the hash, or empty or deleted), probing by groups, matching a whole group of control bytes at once with SIMD or bit tricks, and comparing keys only for tag matches. Misses are cheap because they usually end after one control word.

**When can a Swiss-style erase empty a slot?** When the group still has an empty byte, because then no search ever went past that group. Otherwise it leaves a tombstone.

**Design an LRU cache.** A hash map from key to list node, and a doubly linked list in order of use. `get` and `put` move the node to the front with `splice`; eviction removes the last node and its map entry. All O(1).

---

## 10. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (harder) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). For a design problem, start with the four questions of section 1.

### On paper

**Problem A: Robin Hood inserts.** Capacity 8, home slot `key mod 8`, load limit high enough that nothing grows. Insert 5, 13, 6, 21, 14 in this order. Write the table (`key:probe`) after each insert and say where a swap happens. The table is circular.
*Where:* paper. *How:* the trace of 2.3; compare the probe length of the resident with the distance your key has walked.

**Problem B: Backward shift.** Erase 13 from the table of Problem A. Then count the slots that `find(29)` (home 5) examines, with the early stop and with plain linear probing on the same occupied slots.
*Where:* paper. *How:* the erase row of 2.3; the shift continues across the end of the array.

**Problem C: A kick chain.** `B = 1`, four buckets, keys with bucket pairs P `(0, 1)`, Q `(1, 2)`, R `(2, 3)`, S `(0, 1)`, inserted in this order. Write the table after each insert and the kick chain of S. Then insert T `(0, 1)`. Why can no algorithm store it, and what does the real class do?
*Where:* paper. *How:* the trace of 3.2; count the keys that can only live in buckets 0 and 1.

**Problem D: A group match.** The group `30 31 30 30 31 05 31 40` (slot 0 first), tag `31`. Which slots does `matchTag` flag? Which are real matches? How many key comparisons does a search for an absent key with this tag make in this group?
*Where:* paper. *How:* the table of 4.2: XOR with the tag first, then look for zeros and for `01` bytes above them.

**Problem E: LRU by hand.** Capacity 2: `put(1, 1)`, `put(2, 2)`, `put(1, 10)`, `put(3, 3)`, `get(2)`, `get(1)`, `put(4, 4)`, `get(3)`, `get(4)`. Write the list after each step and every result. Which `get` would answer differently if `put` on an existing key did not move it to the front?
*Where:* paper. *How:* the table of section 6.

### In code

**Problem 1: LRU Cache** (LeetCode 146, medium)
*Where:* leetcode.com/problems/lru-cache. *Goal:* `get` and `put` in O(1) with a fixed capacity.
*How:* section 6. Then write a brute-force version on a `vector` and compare both on random operations, as the companion does. Test capacity 1.

**Problem 2: Robin Hood map from scratch** (own code)
*Where:* your editor. *Goal:* `put`, `find` and `erase` with backward shift on three flat arrays, plus a `checkInvariants()` that verifies that the probe length grows by at most 1 from one slot to the next and that every entry's probe length matches its home slot. Test against `std::unordered_map` on random operations with a small capacity, so that runs wrap around the end of the array. Then measure the longest probe at load 0.9 against linear probing.
*How:* section 2. Write the erase on paper first, with a wrap-around case.

**Problem 3: LFU Cache** (LeetCode 460, hard)
*Where:* leetcode.com/problems/lfu-cache. *Goal:* `get` and `put` in O(1); evict the least frequently used entry, and the least recently used among equals.
*How:* section 6. Keep a map from key to value and frequency, a map from frequency to an LRU list, and the smallest frequency in use.

**Problem 4: How full can cuckoo get?** (own code)
*Where:* your editor. *Goal:* a bucketised cuckoo set for `B = 1, 2, 4` and the load at which the first insert fails, averaged over several seeds. Compare with the table in 3.4. Then replace the random walk by a search for the shortest eviction chain (breadth first) and compare the failure load and the longest chain.
*How:* section 3. If your numbers are far from the table, check that the two hash functions are independent and that you stop at the first failure.

**Problem 5: A mini Swiss table** (own code, stretch)
*Where:* your editor. *Goal:* groups of 8, one control byte per slot, the matching of 4.2, the erase rule of 4.4 and a rehash. Test against `std::unordered_set` with inserts and erases mixed so that tombstones appear. Then time 10^6 lookups against `std::unordered_set`.
*How:* sections 4.2 to 4.4. Write the tombstone scenario of 4.4 as a test before you write `erase`.

**Problem 6: A flow table with an idle timeout** (own code)
*Where:* your editor. *Goal:* `touch(key, now)` inserts or refreshes a flow; `expire(now)` removes flows idle for more than `T`; at capacity `touch` evicts the least recently used flow. Test against a brute-force scan with a fake clock.
*How:* section 6: the list is ordered by last touch, so `expire` only looks at the back.

**More practice:** All O`one Data Structure (432), Design In-Memory File System (588), Design Hit Counter (362), Logger Rate Limiter (359), Design Underground System (1396). Design HashSet and HashMap (705, 706) and Insert Delete GetRandom O(1) (380) are in tutorial 01.

---

## Answers

Try before reading. LeetCode wraps these functions in `class Solution { public: ... };`; the logic is the same.

### Problem A

| Insert | Table, slots 0 to 7 | What happened |
|---|---|---|
| 5 | `. . . . . 5:1 . .` | home 5, empty |
| 13 | `. . . . . 5:1 13:2 .` | home 5; slot 5 holds 5:1, not below 1; slot 6 is empty |
| 6 | `. . . . . 5:1 13:2 6:2` | home 6; slot 6 holds 13:2, not below 1; slot 7 is empty |
| 21 | `6:3 . . . . 5:1 13:2 21:3` | home 5; slots 5 and 6 hold 1 and 2, not below 1 and 2; slot 7 holds 6:2 and 2 < 3, so **21 takes slot 7** and 6 walks on, wraps to slot 0 with probe length 3 |
| 14 | `6:3 14:4 . . . 5:1 13:2 21:3` | home 6; slots 6, 7, 0 hold 2, 3, 3, not below 1, 2, 3; slot 1 is empty, probe length 4 |

### Problem B

Erasing 13 empties slot 6. Slot 7 (21:3) moves back to slot 6 as 21:2. Slot 0 (6:3) moves back to slot 7 as 6:2. Slot 1 (14:4) moves back to slot 0 as 14:3. Slot 2 is empty: stop. Result: `14:3 . . . . 5:1 21:2 6:2`.

`find(29)` (home 5) with the early stop: slot 5 (probe 1, not below 1), slot 6 (2, not below 2), slot 7 (2, below 3): absent after **3** slots. Plain linear probing walks slots 5, 6, 7, 0 and stops at the empty slot 1: **5** slots.

### Problem C

| Insert | Buckets 0 to 3 | What happened |
|---|---|---|
| P | `P . . .` | bucket 0 is free |
| Q | `P Q . .` | bucket 1 is free |
| R | `P Q R .` | bucket 2 is free |
| S | `S P Q R` | buckets 0 and 1 are full. S takes bucket 0 and P is evicted; P goes to bucket 1 and Q is evicted; Q goes to bucket 2 and R is evicted; R's other bucket is 3, free. **Three kicks** |

T has buckets 0 and 1, the same as P and S: three keys for two slots. No order of evictions fixes that, so the chain cycles until `kMaxKicks`, and then `put` calls `rebuild`: new hash functions (so the keys get new bucket pairs) and twice the buckets. With `B = 2` the three keys would fit.

### Problem D

The XOR with `31` gives `01 00 01 01 00 34 00 71`. The zero bytes, the real matches, are slots 1, 4 and 6. The borrow from slot 1 turns the `01` in slot 2 into `FF` (`01 - 1 - 1`), which borrows again and flags slot 3 as well. Slot 0 has nothing below it to borrow from, and slot 5 (`34 - 1 - 1 = 32`) has a clear top bit. So `matchTag` flags slots **1, 2, 3, 4 and 6**, and slots 2 and 3 are false positives. A search for an absent key with this tag makes **5** key comparisons in this group.

### Problem E

| Operation | Result | List afterwards |
|---|---|---|
| `put(1, 1)` | | `1:1` |
| `put(2, 2)` | | `2:2 1:1` |
| `put(1, 10)` | | `1:10 2:2` |
| `put(3, 3)` | evicts key 2 | `3:3 1:10` |
| `get(2)` | -1 | `3:3 1:10` |
| `get(1)` | 10 | `1:10 3:3` |
| `put(4, 4)` | evicts key 3 | `4:4 1:10` |
| `get(3)` | -1 | `4:4 1:10` |
| `get(4)` | 4 | `4:4 1:10` |

If `put(1, 10)` left key 1 where it was, the list after it would be `2:2 1:10`, `put(3, 3)` would evict key **1**, and `get(2)` would return 2 instead of -1.

### Problem 1: LRU Cache

The class of section 6 is the answer, for capacity at least 1 (LeetCode's constraint). Check these cases: capacity 1 (every `put` of a new key evicts), `put` of an existing key (update and refresh, no eviction), and `get` of an absent key (no change).

### Problem 2: Robin Hood map from scratch

Follow section 2.2. The erase loop is `for (next = i + 1; probe[next] > 1; ++next)`: copy the entry back, subtract 1 from its probe length, and continue from its old slot; every index is taken modulo the capacity. At load 0.9 and 65536 slots the companion measures a longest probe of 38 for Robin Hood against 615 for linear probing (one run, section 2.4); your numbers will differ with your hash, but the order of magnitude should hold.

### Problem 3: LFU Cache

Keep `info[key] = {value, frequency}`, `lists[f]` = keys of frequency `f` with the most recently used first, `where[key]` = an iterator into its list, and `minFreq`. A `get` or an update moves the key from `lists[f]` to the front of `lists[f + 1]`; if `lists[f]` becomes empty and `f == minFreq`, increase `minFreq`. A new key needs room first: evict the **back** of `lists[minFreq]`, then insert with frequency 1 and set `minFreq = 1`. Capacity 0 is a legal input: ignore every `put`.

### Problem 4: How full can cuckoo get?

Expect about 0.5, 0.87 and 0.96 for `B = 1, 2, 4` with a few hundred kicks, if you stop at the first failure and use independent hash functions. The breadth-first search finds the shortest eviction chain, so compare the longest chain you see as well as the failure load.

### Problem 5: A mini Swiss table

Write the three pieces separately and test each: `matchTag` against a loop over the 8 bytes (it may return extra bytes, never fewer), `matchEmpty` and `matchFree` exactly, then the table against `std::unordered_set` with erases. The tombstone scenario of 4.4 (fill a group, push a key past it, erase from the full group, look the pushed key up) catches the wrong erase rule at once.

### Problem 6: A flow table with an idle timeout

A hash map from key to list node and a list ordered by last touch, as in section 6, with the timestamp in the node. `touch` moves the node to the front. `expire(now)` pops from the back while `now - last > T`, erasing each map entry as it goes. Every entry is removed once, so expiry is O(1) amortised per flow.

---

## Cheat sheet

| Design | Core idea | Usable load | Erase | Entries move |
|---|---|---|---|---|
| Robin Hood | swap with a resident that has a smaller probe length; a search stops at such a resident | about 0.9 | backward shift | on insert and erase |
| Cuckoo (`B` slots) | two buckets per key; evict and re-home; rebuild after `kMaxKicks` | `B` = 1, 2, 4, 8: 0.5, 0.87, 0.96, 0.99 | free the slot | on insert |
| Swiss-style | control byte per slot; match a group at once; stop at a group with an empty byte | 7/8 | empty if the group has an empty byte, else tombstone | on rebuild |
| LRU | map from key to list node; `splice` to the front; evict the back | n/a | | |

**Sources** (checked while writing this tutorial):
- DPDK Programmer's Guide, Hash Library: [doc.dpdk.org/guides/prog_guide/hash_lib.html](https://doc.dpdk.org/guides/prog_guide/hash_lib.html) (cuckoo hash, two buckets, signature compared before the key, batch lookup with prefetch).
- Abseil, Swiss Tables design notes: [abseil.io/about/design/swisstables](https://abseil.io/about/design/swisstables) (one metadata byte per entry, H1 of 57 and H2 of 7 bits, SSE on 16 bytes, deleted entries do not end probing, empty ones do).
- The Go blog, Faster Go maps with Swiss Tables: [go.dev/blog/swisstable](https://go.dev/blog/swisstable) (groups of 8, 64-bit control word, 8-byte SIMD on amd64 with a portable fallback, tables of at most 1024 entries that grow independently).
- Redis, `dict.c`: [github.com/redis/redis/blob/unstable/src/dict.c](https://github.com/redis/redis/blob/unstable/src/dict.c) (two tables, `rehashidx`, a rehash step per operation, inserts into the new table, lookups in both).

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `03_design_a_hash_map_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** Hashing tutorial 04, probabilistic structures (Bloom filter, counting Bloom and cuckoo filters, count-min sketch, HyperLogLog, MinHash). Say "next" when you are ready.
