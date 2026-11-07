# Hashing: Area Plan

Hash tables are the structure you reach for first in a coding round and meet again in every systems stack: flow tables, MAC tables, caches, symbol tables, deduplication. This area teaches how a hash table works inside (so you can reason about its cost and its failure modes), the patterns that use one, how to build your own, and the probabilistic structures that trade exactness for memory.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed or probe counts is checked by the companion code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.

Hash maps already appeared in the arrays area (subarray sum equals K in tutorial 04, grouping anagrams in tutorial 07). Here they become the subject.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_hash_tables_inside.md`](01_hash_tables_inside.md) | Hash functions (mixing, strings, combining fields), collisions and the birthday bound, chaining and linear probing with tombstones and probe counts checked against the textbook formulas, load factor and amortised resizing, universal hashing and hash flooding, what `std::unordered_map` does inside, systems links | Written |
| 02 | [`02_hashing_patterns.md`](02_hashing_patterns.md) | Seen-sets and cycle detection (Floyd), counting and top K (bucket sort, heap), complement lookup (Two Sum, counting pairs, 4Sum II by meeting in the middle), canonical keys and one-to-one mappings (isomorphic strings, word pattern, Sudoku bitmasks), vectors and directions as keys with the floating-point trap, longest consecutive sequence with the 2d-lookups argument, systems links (replay window, flow counters, hash join). Prefix-sum maps are in Arrays 04; the hash map plus array trick is in tutorial 01 | Written |
| 03 | [`03_design_a_hash_map.md`](03_design_a_hash_map.md) | Robin Hood hashing with backward-shift deletion and its probe statistics against linear probing, cuckoo hashing with bucket size against usable load, the Swiss-table idea (control bytes, tags, group matching with bit operations, the erase rule), when entries move, growth without a latency spike, LRU cache (hash map plus list); every class tested against the standard containers, measured numbers checked by the companion code | Written |
| 04 | [`04_probabilistic_structures.md`](04_probabilistic_structures.md) | Bloom filter with sizing from the false-positive arithmetic and double hashing, counting Bloom filter (saturation, safe removal), cuckoo filter (partial-key cuckoo hashing, undo of a failed insert, bits per key against a Bloom filter), count-min sketch (width and depth from error and confidence, conservative update), HyperLogLog (rank, harmonic mean, linear counting, union), MinHash with banding, where each is used and how to choose; every error measured against its formula by the companion code | Written |

---

## Problem list (about 15)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Design HashSet (705), Design HashMap (706), Insert Delete GetRandom O(1) (380); in your own editor: erase without tombstones, and break then fix `unordered_map` with crafted keys. More practice: 217, 349, 381, 535 |
| 02 | Contains Duplicate II (219), Happy Number (202), Isomorphic Strings (205), Word Pattern (290), Valid Sudoku (36), Longest Consecutive Sequence (128), Top K Frequent Elements (347), 4Sum II (454) |
| 03 | LRU Cache (146), LFU Cache (460); in your own editor: a Robin Hood map with a random differential test against `std::unordered_map`, the load at which a bucketised cuckoo set fails, a mini Swiss table, a flow table with an idle timeout |
| 04 | Majority Element II (229), Linked List Random Node (382); in your own editor: a Bloom filter and a count-min sketch measured against their formulas, a cuckoo filter with a failed-insert test, HyperLogLog from scratch, a near-duplicate finder with MinHash and banding. More practice: 169, 347, 692, 398 |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- MIT 6.006, Spring 2020, lecture 4 "Hashing" (ocw.mit.edu): chaining, universal hashing, open addressing, with the proofs. CLRS chapter 11 "Hash Tables" covers the same ground in print.
- Stanford CS166, lecture on linear probing and hashing (web.stanford.edu/class/archive/cs/cs166): why the quality of the hash function decides the running time of linear probing, and cuckoo hashing for tutorial 03.
- Abseil, "Swiss Tables Design Notes" (abseil.io/about/design/swisstables), and Matt Kulukundis, "Designing a Fast, Efficient, Cache-friendly Hash Table, Step by Step" (CppCon 2017, on YouTube): how Google's `flat_hash_map` is built, for tutorial 03.
- Crosby and Wallach, "Denial of Service via Algorithmic Complexity Attacks" (USENIX Security 2003): the hash-flooding attack on real systems, for tutorial 01.
- Knuth, *The Art of Computer Programming*, volume 3, section 6.4: the original analysis of probing.
- Fan, Andersen, Kaminsky, Mitzenmacher, "Cuckoo Filter: Practically Better Than Bloom" (CoNEXT 2014): the filter of tutorial 04 with its space analysis. Kirsch and Mitzenmacher, "Less Hashing, Same Performance: Building a Better Bloom Filter" (ESA 2006): the double hashing used there.
- Cormode and Muthukrishnan, "An Improved Data Stream Summary: The Count-Min Sketch and its Applications" (Journal of Algorithms, 2005), and Flajolet, Fusy, Gandouet and Meunier, "HyperLogLog: the analysis of a near-optimal cardinality estimation algorithm" (AofA 2007): the original analyses, for tutorial 04.
- Leskovec, Rajaraman and Ullman, *Mining of Massive Datasets* (free at mmds.org), chapter 3 "Finding Similar Items" (MinHash and banding) and chapter 4 "Mining Data Streams": for tutorial 04.
- NeetCode 150, section "Arrays & Hashing" (neetcode.io): the same problems with video walkthroughs, for tutorial 02.
- Linux kernel source, `include/linux/hash.h` and `include/linux/hashtable.h`: a production multiplicative hash and a chained table in about 100 lines each.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Hash tables inside | Written |
| 02 | Hashing patterns | Written |
| 03 | Design a hash map | Written |
| 04 | Probabilistic structures | Written |

All four tutorials are written. Solve the problems, then move on to the next area, binary search and sorting. Track view: [`../00_plan.md`](../00_plan.md).
