# DSA: Track Plan

> **Track code:** DSA · **Weeks:** 1–23, every week · **Budget:** ~200 hours (about 8 h a week) · **Target:** ~250 problems, mediums in under 30 minutes, explained out loud

Every company on your list runs coding rounds, and a weak coding round ends a loop no matter how strong the systems rounds are. This track teaches each data structure and algorithm from the mechanism up, then drills the patterns until you recognise them in a problem you have never seen.

---

## 1. Why this track matters

Coding rounds are the one part of the loop every company shares, so this track gets the largest share of the time. Beyond a correct answer, interviewers look for:

- **Complete, correct code without running it.** Many screens use a shared document with no execution. Write the whole solution, then trace it by hand on a small input and an edge case.
- **The follow-up.** Medium–hard problems arrive with a twist: stream the input, cap the memory, make it concurrent, support updates. Prepare the follow-up for every problem you solve.
- **Speed on mediums.** Some loops expect two problems in 45 minutes. Time yourself.
- **Reading and fixing existing code.** Some rounds hand you a codebase to debug or extend instead of a blank page.
- **Language fluency.** In C++, be ready to discuss ownership, copies versus moves and what the code costs. For systems roles, solve some problems in C.

Formats change by team and year; confirm the format with the recruiter each time.
---

## 2. What you will be able to do after this track

1. Look at a new problem, read its constraints, and name the two or three patterns that could fit within the first five minutes.
2. State time and space complexity before writing code, and defend it.
3. Write correct C++ for any medium in under 30 minutes without running it, and trace it on an example.
4. Explain the internals of every structure you use (`std::vector` growth, `std::unordered_map` buckets, `std::map` as a red-black tree, `std::priority_queue` as a binary heap).
5. Handle the follow-up: a stream instead of an array, memory limits, concurrency, very large inputs.

---

## 3. How the track is organised

`01_dsa/` has one folder per area. Each area has its own `00_plan.md` (topics, problem set, resources, progress), numbered tutorials, and a tested companion `.cpp` for every tutorial with code.

The order below is the study order. Each area builds on the ones before it: hashing before graphs (visited sets), recursion before trees and DP, heaps (inside trees) before greedy scheduling.

| # | Area folder | Topics | Problems | Weeks | Status |
|---|---|---|---|---|---|
| 01 | [`01_arrays_and_strings/`](01_arrays_and_strings/00_plan.md) | 7 tutorials (01–03 written) | ~30 | 1–2 | In progress |
| 02 | `02_hashing/` | 3 tutorials | ~12 | 3 | To write |
| 03 | `03_binary_search_and_sorting/` | 5 tutorials | ~18 | 4 | To write |
| 04 | [`04_linked_lists/`](04_linked_lists/00_plan.md) | 6 core topics (01–02 done) | ~15 | 1–5 | In progress |
| 05 | `05_stacks_and_queues/` | 4 tutorials | ~15 | 5 | To write |
| 06 | `06_recursion_and_backtracking/` | 3 tutorials | ~15 | 6 | To write |
| 07 | [`07_trees/`](07_trees/00_plan.md) | 34 topics: 26 core, 8 stretch (01 done) | ~45 | 1, 6–20 | In progress |
| 08 | `08_graphs/` | 6 tutorials | ~30 | 11–12 | To write |
| 09 | `09_greedy_and_intervals/` | 3 tutorials | ~15 | 13 | To write |
| 10 | `10_dynamic_programming/` | 7 tutorials | ~40 | 13–15 | To write |
| 11 | `11_bits_and_math/` | 2 tutorials | ~10 | 15 | To write |
| 12 | `12_string_algorithms/` | 3 tutorials | ~8 | 16 | To write |
| 13 | `13_design_style_coding/` | 4 tutorials | ~12 | 20 | To write |
| | **Total** | **~87 tutorials** | **~265** | | |

---

## 4. Topics, area by area

Each area's own `00_plan.md` expands these into subtopics, a problem list and a progress table when the area starts.

### 01 — Arrays and strings (weeks 1–2)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Complexity analysis and arrays in memory | Big-O, amortised cost (why `push_back` is O(1)), the constraints-to-complexity table, contiguous memory and cache lines |
| 02 | Two pointers | Opposite ends, same direction (fast/slow), partitioning (Dutch national flag), why sorted input enables it |
| 03 | Sliding window | Fixed and variable windows, "at most K" to count "exactly K", window invariants |
| 04 | Prefix sums and difference arrays | Subarray sum equals K with a hash map, 2D prefix sums, range updates with difference arrays |
| 05 | Subarray patterns and Kadane | Maximum subarray, maximum product, circular variants, why Kadane is DP |
| 06 | Matrix traversal and in-place tricks | Rotation, spiral order, set-matrix-zeroes in O(1) space, index marking and cyclic sort |
| 07 | String basics | Character counting, anagrams, palindromes, the cost of string building in C++ |

### 02 — Hashing (week 3)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Hash tables inside | Hash functions, chaining vs open addressing, load factor and rehashing, `std::unordered_map` internals, hash flooding attacks |
| 02 | Hashing patterns | Counting, grouping by a canonical key, complement lookup (two-sum family), seen-sets, hashing tuples |
| 03 | Design a hash map | Open addressing with linear probing and tombstones, Robin Hood hashing, tested against `std::unordered_map` |

### 03 — Binary search and sorting (week 4)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Binary search templates | Loop invariants, `lower_bound` and `upper_bound`, proving there is no off-by-one |
| 02 | Binary search on the answer | Monotonic predicates: minimum speed, ship capacity, split array largest sum |
| 03 | Rotated, peak and 2D search | Rotated arrays with and without duplicates, peak finding, sorted matrices |
| 04 | Sorting algorithms inside | Merge sort, quicksort with Lomuto and Hoare partitions, heapsort, counting and radix sort, stability, introsort inside `std::sort` |
| 05 | Sorting patterns and comparators | Strict weak ordering (and the crash when you break it), quickselect, counting inversions with merge sort |

### 04 — Linked lists (weeks 1–5)

Already planned in detail in [`04_linked_lists/00_plan.md`](04_linked_lists/00_plan.md): 01 foundations and 02 core patterns are written; core topics 03 doubly and circular lists, 04 merging and sorting, 05 classic problems including LRU and LFU, 06 systems lists (`list_head`, skip lists). Topics 07–08 are folded into the C++ track and the mock schedule.

### 05 — Stacks and queues (week 5)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Stack and queue fundamentals | Array and ring-buffer implementations, queue from two stacks (amortised), min-stack |
| 02 | Monotonic stack | Next greater element, largest rectangle in a histogram, trapping rain water, stock span |
| 03 | Monotonic deque | Sliding window maximum, why each element is pushed and popped once |
| 04 | Expression evaluation and parsing | Balanced brackets, reverse Polish notation, basic calculator, decode string |

### 06 — Recursion and backtracking (week 6)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Recursion fundamentals | The call stack, recursion trees, base cases, stack depth limits, converting recursion to iteration |
| 02 | Subsets, permutations, combinations | The choose–explore–unchoose template, handling duplicates, counting the search space |
| 03 | Constraint backtracking | N-queens, Sudoku, word search, pruning, bitmasks as state |

### 07 — Trees (weeks 1 and 6–20)

The largest area, planned in detail in [`07_trees/00_plan.md`](07_trees/00_plan.md): binary trees, BSTs, AVL and red-black trees (with the Linux kernel rbtree), heaps, tries and longest-prefix match, range-query trees (Fenwick, segment trees), LCA, union-find, B-trees and B+ trees, and trees inside the kernel. Core topics are 01–13, 15–21, 24, 25, 28, 29, 31 and 34; the rest are stretch.

### 08 — Graphs (weeks 11–12)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Representations and traversal | Adjacency list vs matrix vs edge list, BFS, iterative DFS, grids as implicit graphs |
| 02 | Connectivity and cycles | Connected components, cycle detection in directed and undirected graphs, bipartite check, flood fill |
| 03 | Topological sort | Kahn's algorithm and DFS order, course schedule, alien dictionary, build systems |
| 04 | Shortest paths | BFS, 0-1 BFS, Dijkstra (and why negative edges break it), Bellman-Ford, Floyd-Warshall |
| 05 | Minimum spanning trees | Kruskal with union-find, Prim with a heap, the cut property |
| 06 | Advanced graphs | Strongly connected components (Tarjan, Kosaraju), bridges and articulation points, Euler paths |

### 09 — Greedy and intervals (week 13)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Greedy algorithms and their proofs | Exchange argument, "greedy stays ahead", jump game, gas station, when greedy fails |
| 02 | Intervals and sweep line | Merge, insert, meeting rooms I and II, minimum removals, event sweeps |
| 03 | Greedy scheduling with heaps | Task scheduler, reorganise string, IPO, the heap as a greedy engine |

### 10 — Dynamic programming (weeks 13–15)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | DP fundamentals | From recursion to memoisation to tabulation, designing the state, space optimisation |
| 02 | One-dimensional DP | Coin change, word break, decode ways, LIS in O(n log n) |
| 03 | Grid and two-sequence DP | Unique paths, LCS, edit distance, distinct subsequences |
| 04 | The knapsack family | 0/1, unbounded, subset sum, target sum, partition |
| 05 | Interval and game DP | Burst balloons, palindrome partitioning, stone games |
| 06 | Bitmask and state-machine DP | Visiting all nodes, assignment problems, stock problems as state machines |
| 07 | DP on trees and DAGs | House robber III, longest path in a DAG; links to trees 26 |

### 11 — Bits and math (week 15)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Bit manipulation | `x & (x - 1)`, lowest set bit, XOR puzzles, counting bits, bitmasks as sets; links to `02_c_language` 01 |
| 02 | Math for interviews | GCD and LCM, modular arithmetic and fast exponentiation, the sieve, nCr mod p, reservoir sampling |

### 12 — String algorithms (week 16)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | KMP and the Z-function | Prefix function, pattern matching in O(n + m), string periods |
| 02 | Rolling hash and Rabin-Karp | Polynomial hashing, collision probability, double hashing, longest duplicate substring |
| 03 | Palindromes and Manacher | Expand around centre; Manacher as stretch |

### 13 — Design-style coding (week 20)

These overlap LLD and are common in rounds that blend design and code.

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Cache designs | LRU and LFU in O(1), with the linked-list and hash-map wiring |
| 02 | Iterators and streams | Flatten a nested list, peeking and zigzag iterators, median of a data stream |
| 03 | Time-based structures | Time-based key-value store, hit counter, snapshot array, rate limiter |
| 04 | In-memory systems | In-memory file system, design Twitter, autocomplete |

---

## 5. How to solve a problem in an interview

Use the same six steps on every practice problem, so they are automatic in the room.

| Step | What you do | Minutes (of 45) |
|---|---|---|
| 1. **Understand** | Restate the problem. Ask about input size, value ranges, duplicates, empty input, sortedness. Write two examples, including one edge case. | 3–5 |
| 2. **Match** | Read the constraints and name candidate patterns (table below). Say the brute force and its complexity first. | 2–3 |
| 3. **Plan** | Choose the approach, state its time and space complexity, and get agreement before coding. | 3–5 |
| 4. **Implement** | Write clean code with meaningful names. Narrate the decisions, not the syntax. | 15–20 |
| 5. **Review** | Trace your code on the example by hand, line by line. Check the edge cases. | 5 |
| 6. **Evaluate** | Restate complexity. Discuss the follow-up: streaming input, memory limits, concurrency, scale. | 3–5 |

### Reading the constraints

The input size tells you the complexity the problem setter expects, assuming roughly 10⁸ simple operations per second.

| Input size n | Complexity that fits | Typical technique |
|---|---|---|
| n ≤ 10 | O(n!) | Permutations, brute force |
| n ≤ 20 | O(2ⁿ) | Subsets, bitmask DP |
| n ≤ 500 | O(n³) | Floyd-Warshall, interval DP |
| n ≤ 5,000 | O(n²) | 2D DP, all pairs |
| n ≤ 10⁶ | O(n log n) or O(n) | Sorting, heaps, two pointers, sliding window, hashing |
| n ≥ 10⁹ | O(log n) or O(1) | Binary search on the answer, math |

---

## 6. Practice system

Reading tutorials is not enough. Most of this track's hours go to solving problems, logged so that weak patterns become visible.

1. **Problem order.** Each area's problem list draws from NeetCode 150 first, then NeetCode 250 and Striver's SDE sheet, then LeetCode company tags from week 18. A tutorial's exercises are drawn from its area's problem list, so tutorial time and practice time overlap.
2. **Log every attempt** in [`practice_log.md`](practice_log.md): problem, pattern, difficulty, time taken, whether you needed a hint, and the date to revisit.
3. **Spaced revisits.** Re-solve any problem you needed help on after 3 days, 7 days and 21 days, cold. A problem is "owned" when you solve it cold twice.
4. **Timing targets.** Easy in 15 minutes, medium in 30, hard in 45. Stop at the limit, read the solution, then re-solve it cold the next day.
5. **No-execution practice.** From week 10, solve one problem in three in a plain text editor without compiling, then trace by hand (the format of many phone screens).
6. **Say it out loud.** Narrate at least one problem a day as if an interviewer were listening.
7. **Keep solutions in GitHub.** A public `dsa-solutions` repo with one folder per area, each solution with a two-line comment: the pattern, and the complexity.

### Weekly targets

| Weeks | Problems per week | Running total | Mix |
|---|---|---|---|
| 1–6 | 10 | ~60 | Mostly easy and medium in the current area |
| 7–14 | 11–12 | ~150 | Medium in the current area plus 2 revisits |
| 15–23 | 11–12 | ~250 | Medium and hard, then company-tagged and timed mixed sets |

---

## 7. Representative interview questions

These are the kinds of problems and follow-ups you should be able to handle by the end. Each area plan lists its own.

1. Given a stream of integers, return the median at any time. *Follow-up:* 99% of values are in [0, 100].
2. Find the longest substring with at most K distinct characters. *Follow-up:* the input is a stream that does not fit in memory.
3. Merge K sorted lists. *Follow-up:* the lists live on different machines.
4. Course schedule: can all courses be finished? Return an order. *Follow-up:* detect which courses form the cycle.
5. Network delay time (Dijkstra). *Follow-up:* why does Dijkstra fail with negative edges?
6. Implement an LRU cache. *Follow-up:* make it thread-safe; then make it scale across cores.
7. Word search II on a grid with a trie. *Follow-up:* prune the trie as words are found.
8. Minimum number of meeting rooms. *Follow-up:* return the room assignment.
9. Edit distance. *Follow-up:* reduce space to O(min(m, n)) and reconstruct the edits.
10. Count of smaller numbers after self (Fenwick tree or merge sort). *Follow-up:* values up to 10⁹.
11. Longest-prefix match for IP routing with a binary trie.
12. Serialize and deserialize a binary tree. *Follow-up:* the tree is too deep for recursion.

---

## 8. How to study this track

1. Follow the five-step topic method in [`../README.md`](../README.md): read, rebuild, run, practice, explain.
2. Study one area at a time in its own chat in the Study-Plan project ("Start DSA area 01, arrays and strings"). Say **"next"** for the following tutorial.
3. Every companion file compiles cleanly under `-Wall -Wextra -fsanitize=address,undefined` and prints `N/N checks passed`. Build one with:

   ```bash
   clang++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 01_binary_tree_fundamentals_demo.cpp -o demo && ./demo
   ```

4. Areas overlap in the schedule on purpose: trees run alongside other areas from week 6 because the trees plan is long.

---

## 9. Resources

The tutorials are self-contained; these are for a second explanation or more problems.

| Resource | Use it for |
|---|---|
| NeetCode roadmap and NeetCode 150/250 (neetcode.io) | The core problem list, with video explanations |
| Striver's A2Z sheet and SDE sheet (takeuforward.org) | Extra problems per pattern |
| LeetCode company tags (Premium) | Weeks 18–23, company-specific sets |
| cp-algorithms.com | Rigorous write-ups of algorithms (segment trees, string algorithms, graphs) |
| *Introduction to Algorithms* (CLRS) | Proofs, when you want them |
| *The Algorithm Design Manual*, Skiena | Recognising which problem you are looking at |

---

## 10. Progress

| Area | Done | Next |
|---|---|---|
| 01 Arrays and strings | 01–03 | 04 prefix sums (week 2) |
| 04 Linked lists | 01, 02 | 03 doubly and circular lists (week 2) |
| 07 Trees | 01 | 02 iterative traversals and Morris traversal (week 6) |
| All other areas | — | Start in the week shown in section 3 |

Updated every time a tutorial is written or studied. The cross-track view is in [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
