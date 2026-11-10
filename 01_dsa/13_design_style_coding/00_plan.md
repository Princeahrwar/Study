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
| 02 | Iterators and streams | Iterator design (flatten, zip, peeking, skip), stream and window structures, data-stream statistics | Core | To write |
| 03 | Time-based structures | Time-keyed stores with binary search, hit counters, rate limiters, expiry, logger-style windows | Core | To write |
| 04 | In-memory systems | Small file systems, key-value stores, autocomplete, task schedulers built from tries, maps and heaps | Core | To write |

---

## Problem list

| Tutorial | Problems (LeetCode number, from memory) |
|---|---|
| 01 | LRU Cache (146), LFU Cache (460), All O(1) Data Structure (432), Design HashMap (706), Design Hit Counter (362), Time Based Key-Value Store (981) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Cache designs | Written |
| 02 | Iterators and streams | To write |
| 03 | Time-based structures | To write |
| 04 | In-memory systems | To write |

Track view: [`../00_plan.md`](../00_plan.md).
