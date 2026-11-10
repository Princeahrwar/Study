# Design-Style Coding: Area Plan

Interview questions that say "design a ..." and expect working code: caches, iterators and streams, time-based structures and small in-memory systems. The skill is choosing the data structures, stating the complexity of each operation and handling the edge cases and (when asked) the threads.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, counts or failure rates is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. There is no companion demo file: the code is inside the tutorial.
5. Every function in the code is tested against a brute force or a simple model on small and random inputs, under AddressSanitizer and UBSan (ThreadSanitizer where threads are involved).

---

## Tutorials

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 01 | [`01_cache_designs.md`](01_cache_designs.md) | The hit ratio and the policy versus mechanism split, FIFO, random, LRU, LFU, CLOCK, segmented LRU and a TinyLFU admission filter (count-min sketch), Belady's OPT and how an admission filter can beat it, Belady's anomaly, the loop of cache plus one keys (100% misses), four traces compared (zipf, loop, scan, shifting hot set), a flat-array LRU with lazy expiry, mutex versus sharded caches (measured, noisy), where real systems differ (from memory) | Core | Written |
| 02 | [`02_iterators_and_streams.md`](02_iterators_and_streams.md) | The iterator contract and its traps, flatten 2D with empty rows, zigzag over k sources, peeking and skip, the lazy nested-list iterator (2 elements examined against 1,000,003), the BST iterator (stack at most the height), merge of k sorted sources, a range-for adapter and its measured cost, moving average, k-th largest, running median (two heaps), sliding-window maximum (monotone deque, measured against a multiset and a rescan), first unique, reservoir sampling (tested by distribution), Misra-Gries frequent items, Welford variance and merging summaries | Core | Written |
| 03 | [`03_time_based_structures.md`](03_time_based_structures.md) | Time as an integer input and half-open windows, the time map (vector with `upper_bound` against an ordered map, measured), the snapshot array (changes only), hit counters (log against ring), four rate limiters (fixed window, sliding log, sliding counter, token bucket) with the burst that gets twice the limit through and the token bucket refill bug, an expiring map with versioned heap entries, timers as a heap and as a timing wheel (measured), Calendar I with two neighbours and Calendar III with a sweep | Core | Written |
| 04 | In-memory systems | Small file systems, key-value stores, autocomplete, task schedulers built from tries, maps and heaps | Core | To write |

---

## Problem list

| Tutorial | Problems (LeetCode number, from memory) |
|---|---|
| 01 | LRU Cache (146), LFU Cache (460), All O(1) Data Structure (432), Design HashMap (706), Design Hit Counter (362), Time Based Key-Value Store (981) |
| 02 | Flatten Nested List Iterator (341), Peeking Iterator (284), Binary Search Tree Iterator (173), Zigzag Iterator (281), Kth Largest Element in a Stream (703), Find Median from Data Stream (295), Sliding Window Maximum (239), Linked List Random Node (382), Sliding Window Median (480) |
| 03 | Time Based Key-Value Store (981), Snapshot Array (1146), Design Hit Counter (362), Logger Rate Limiter (359), My Calendar I (729), My Calendar II (731), My Calendar III (732) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Cache designs | Written |
| 02 | Iterators and streams | Written |
| 03 | Time-based structures | Written |
| 04 | In-memory systems | To write |

Track view: [`../00_plan.md`](../00_plan.md).
