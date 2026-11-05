# DSA: Track Plan

> **Track code:** DSA · **Weeks:** 1–23, every week · **Budget:** ~230 hours for the core path, stretch topics extra · **Target:** ~250 problems owned, mediums in under 30 minutes, explained out loud

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
| --- | --- | --- | --- | --- | --- |
| 01 | [`01_arrays_and_strings/`](01_arrays_and_strings/00_plan.md) | 7 tutorials (all written) | ~32 | 1–2 | Written |
| 02 | [`02_hashing/`](02_hashing/00_plan.md) | 4 tutorials (all written) | ~15 | 3 | Written |
| 03 | [`03_binary_search_and_sorting/`](03_binary_search_and_sorting/00_plan.md) | 5 tutorials (all written) | ~20 | 4 | Written |
| 04 | [`04_linked_lists/`](04_linked_lists/00_plan.md) | 7 core topics (all written) | ~15 | 1–5 | Written |
| 05 | [`05_stacks_and_queues/`](05_stacks_and_queues/00_plan.md) | 4 tutorials (all written) | ~16 | 5 | Written |
| 06 | [`06_recursion_and_backtracking/`](06_recursion_and_backtracking/00_plan.md) | 4 tutorials (all written) | ~17 | 6 | Written |
| 07 | [`07_trees/`](07_trees/00_plan.md) | 34 topics: 26 core, 8 stretch (01 to 19 done) | ~45 | 1, 6–20 | In progress |
| 08 | `08_graphs/` | 9 tutorials: 8 core, 1 stretch | ~38 | 11–12 | To write |
| 09 | `09_greedy_and_intervals/` | 3 tutorials | ~15 | 13 | To write |
| 10 | `10_dynamic_programming/` | 8 tutorials: 7 core, 1 stretch | ~42 | 13–15 | To write |
| 11 | `11_bits_and_math/` | 4 tutorials | ~16 | 15 | To write |
| 12 | `12_string_algorithms/` | 6 tutorials: 4 core, 2 stretch | ~12 | 16 | To write |
| 13 | `13_design_style_coding/` | 4 tutorials | ~12 | 20 | To write |
| 14 | `14_theory_and_advanced_techniques/` | 4 tutorials: 1 core, 3 stretch | ~4 | 21 and after | To write |
| 15 | `15_pattern_map_and_drills/` | 1 tutorial and mixed timed sets | ~10 | 18 and after | To write |
| | **Total** | **104 tutorials: 89 core, 15 stretch** | **~310** | | |

Areas 02, 06, 08, 10, 11 and 12 gained tutorials, and areas 14 and 15 are new, after a coverage review against public syllabi (section 9). Everything added is marked core or stretch in section 4, so the schedule can drop the stretch items first.

---

## 4. Topics, area by area

Each area's own `00_plan.md` expands these into subtopics, a problem list and a progress table when the area starts.

### 01 — Arrays and strings (weeks 1–2)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Complexity analysis and arrays in memory | Big-O, amortised cost (why `push_back` is O(1)), the constraints-to-complexity table, contiguous memory and cache lines |
| 02 | Two pointers | Opposite ends, same direction (fast/slow), partitioning (Dutch national flag), why sorted input enables it |
| 03 | Sliding window | Fixed and variable windows, "at most K" to count "exactly K", window invariants |
| 04 | Prefix sums and difference arrays | Range sums in O(1), subarray sum equals K with a hash map (negatives allowed), prefix XOR and prefix remainders, 2D prefix sums, range updates with difference arrays in 1D and 2D, weighted random pick, why min and max need a different tool |
| 05 | Subarray patterns and Kadane | Maximum subarray, maximum product, circular variants, why Kadane is DP, Boyer-Moore majority vote and other one-pass streaming ideas, the stock-trading family as a first look at state |
| 06 | Matrix traversal and in-place tricks | Rotation, spiral order, set-matrix-zeroes in O(1) space, index marking and cyclic sort, next permutation, row-major layout and why traversal order matters |
| 07 | String basics | Character counting, anagrams, palindromes, parsing edge cases (atoi, version compare, big-number add and multiply on strings), encode and decode, the cost of string building in C++ |

### 02 — Hashing (week 3)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Hash tables inside | Hash functions (mixing, strings, combining fields), the birthday bound, chaining and linear probing with tombstones, load factor and amortised rehashing, universal hashing, hash flooding, `std::unordered_map` internals |
| 02 | Hashing patterns | Seen-sets and Floyd cycle detection, counting and top K, complement lookup (two-sum family, 4Sum II), canonical keys (isomorphic strings, Sudoku bitmasks, points on a line), vectors as keys, longest consecutive sequence |
| 03 | Design a hash map | Robin Hood hashing with backward-shift deletion, cuckoo hashing, the Swiss-table idea (control bytes, tags, group matching), when entries move and growth without a latency spike, LRU cache (hash map plus list), every class tested against the standard containers |
| 04 | Probabilistic structures | Bloom filter sizing and its false-positive arithmetic, counting Bloom and cuckoo filters, count-min sketch, HyperLogLog, MinHash with banding; where each is used (caches, packet dedup, telemetry); every error measured against its formula |

### 03 — Binary search and sorting (week 4)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Binary search templates | Loop invariants, `lower_bound` and `upper_bound`, proving there is no off-by-one |
| 02 | Binary search on the answer | Monotonic predicates: minimum speed, ship capacity, split array largest sum, a count as the test, searching real numbers |
| 03 | Rotated, peak and 2D search | Rotated arrays with and without duplicates, peak finding, sorted matrices, median and k-th of two sorted arrays by partition, ternary search, exponential search |
| 04 | Sorting algorithms inside | Merge sort, quicksort with Lomuto and Hoare partitions, heapsort, counting, bucket and radix sort, the n log n lower bound for comparison sorts, stability, introsort inside `std::sort`, external merge sort |
| 05 | Sorting patterns and comparators | Strict weak ordering (and the crash when you break it), quickselect and median of medians, top-K by heap versus selection versus bucket, interval sorting (merge intervals, meeting rooms), custom orderings (largest number, custom sort string) |

### 04 — Linked lists (weeks 1–5)

Already planned in detail in [`04_linked_lists/00_plan.md`](04_linked_lists/00_plan.md): 01 foundations, 02 core patterns, 03 doubly and circular lists, 04 merging and sorting and 05 classic problems including LRU and LFU and 06 and 07 systems-level lists (`list_head`, free lists, why lists are slow, XOR and unrolled lists; skip lists, lock-free lists, RCU lists) are all written, so the area is complete. The old Phases 7 and 8 are folded into the C++ track and the mock schedule.

### 05 — Stacks and queues (week 5)

Planned in detail in [`05_stacks_and_queues/00_plan.md`](05_stacks_and_queues/00_plan.md); all four tutorials are written, so the area is complete.

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Stack and queue fundamentals | Array and ring-buffer implementations, queue from two stacks (amortised), min-stack |
| 02 | Monotonic stack | Next greater element and its tie rules, daily temperatures, stock span, largest rectangle in a histogram, trapping rain water, sum of subarray minimums, remove K digits |
| 03 | Monotonic deque | Sliding window maximum and minimum (deque, ring, blocks), a min-queue from a deque, shortest subarray with sum at least K, longest subarray with a limit, DP with a window maximum |
| 04 | Expression evaluation and parsing | Balanced brackets and their variants (minimum removal, longest valid run, removing invalid parentheses), reverse Polish notation, the calculators, shunting-yard with precedence and unary minus, recursive descent and its depth limit, decode string, simplify path, stack simulations |

### 06 — Recursion and backtracking (week 6)

Planned in detail in [`06_recursion_and_backtracking/00_plan.md`](06_recursion_and_backtracking/00_plan.md); all four tutorials are written, so the area is complete.

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Recursion fundamentals | The call stack and its measured depth limit, recursion trees and exact call counts, base cases and progress, recursion on lists and grids, tail calls, an explicit stack with work frames, bounding the depth of quicksort |
| 02 | Subsets, permutations, combinations | The choose, explore, unchoose template, exact call counts of subsets and permutations, the duplicate-skipping rules and why they have their form, combination sum, parentheses and palindrome partitions, counting the search space before running it |
| 03 | Constraint backtracking | Checking rules at placement, N-queens with arrays and bitmasks, Sudoku with masks and the fewest-candidates order, word search and tries, IP addresses, equal-sum groups with equal-bucket skipping |
| 04 | Divide and conquer and recurrences | The recursion-tree method and the master theorem, merge-sort-style counting (inversions, reverse pairs), maximum subarray by divide and conquer, closest pair of points, fast exponentiation and Karatsuba, where divide and conquer parallelises |

### 07 — Trees (weeks 1 and 6–20)

The largest area, planned in detail in [`07_trees/00_plan.md`](07_trees/00_plan.md): binary trees, BSTs, AVL and red-black trees (with the Linux kernel rbtree), heaps, tries and longest-prefix match, range-query trees (Fenwick, segment trees), LCA, union-find, B-trees and B+ trees, and trees inside the kernel. Core topics are 01–13, 15–21, 24, 25, 28, 29, 31 and 34; the rest are stretch.

### 08 — Graphs (weeks 11–12)

| # | Tutorial | Key ideas | Priority |
|---|---|---|---|
| 01 | Representations and traversal | Adjacency list vs matrix vs edge list (and the compact compressed-row layout), BFS, iterative DFS, grids as implicit graphs | Core |
| 02 | Connectivity and cycles | Connected components, cycle detection in directed and undirected graphs, bipartite check, flood fill | Core |
| 03 | Topological sort | Kahn's algorithm and DFS order, course schedule, alien dictionary, build systems | Core |
| 04 | BFS on states and grids | Multi-source BFS (rotting oranges), word ladder, sliding puzzle and lock problems as state graphs, bitmask states, bidirectional BFS, 0-1 BFS, A* as guided search | Core |
| 05 | Shortest paths I: Dijkstra | Dijkstra with a heap, why negative edges break it, path reconstruction and counting paths, state-augmented Dijkstra (at most K stops), grid Dijkstra | Core |
| 06 | Shortest paths II: negative edges and all pairs | Bellman-Ford and negative cycles, shortest paths in a DAG, Floyd-Warshall, transitive closure, Johnson's algorithm | Core |
| 07 | Minimum spanning trees | Kruskal with union-find, Prim with a heap, the cut property, connecting points at minimum cost | Core |
| 08 | Advanced graphs | Strongly connected components (Tarjan, Kosaraju), bridges and articulation points, Euler paths (reconstruct itinerary), 2-SAT as an idea | Core |
| 09 | Network flow and bipartite matching | Ford-Fulkerson and Edmonds-Karp, Dinic, max-flow min-cut, matching and Hall's theorem, assignment problems | Stretch |

### 09 — Greedy and intervals (week 13)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Greedy algorithms and their proofs | Exchange argument, "greedy stays ahead", activity selection, fractional knapsack, Huffman coding, jump game, gas station, when greedy fails |
| 02 | Intervals and sweep line | Merge, insert, meeting rooms I and II, minimum removals, event sweeps |
| 03 | Greedy scheduling with heaps | Task scheduler, reorganise string, IPO, scheduling rules (shortest job first, earliest deadline, Johnson's two-machine rule), the heap as a greedy engine |

### 10 — Dynamic programming (weeks 13–15)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | DP fundamentals | From recursion to memoisation to tabulation, designing the state, space optimisation |
| 02 | One-dimensional DP | Coin change, word break, decode ways, LIS in O(n log n) |
| 03 | Grid and two-sequence DP | Unique paths, LCS, edit distance, distinct subsequences, regular-expression and wildcard matching |
| 04 | The knapsack family | 0/1, unbounded, subset sum, target sum, partition |
| 05 | Interval and game DP | Burst balloons, palindrome partitioning, stone games, Nim and the idea behind Sprague-Grundy |
| 06 | Bitmask and state-machine DP | Visiting all nodes, assignment problems, stock problems as state machines |
| 07 | DP on trees and DAGs | House robber III, longest path in a DAG; links to trees 26 |
| 08 | Counting DP, digit DP and DP optimisations (stretch) | Counting with a modulus, digit DP, expected-value DP, divide-and-conquer and Knuth optimisation, the convex hull trick |

### 11 — Bits and math (week 15)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Bit manipulation | `x & (x - 1)`, lowest set bit, XOR puzzles, counting bits, bitmasks as sets, submask enumeration; links to `02_c_language` 01 and CO 05 |
| 02 | Number theory and arithmetic | GCD and LCM, modular arithmetic and fast exponentiation, modular inverse, the sieve, factorisation, overflow-safe arithmetic, `Pow(x, n)`, square root by Newton's method |
| 03 | Combinatorics, probability and random sampling | nCr mod p, Catalan numbers, inclusion-exclusion, expected value, shuffling (Fisher-Yates), reservoir sampling, weighted sampling and the alias method, rejection sampling |
| 04 | Geometry for interviews | Cross product and orientation, segment intersection, polygon area, convex hull, points on a line with exact arithmetic, rectangle overlap, k closest points, closest pair |

### 12 — String algorithms (week 16)

| # | Tutorial | Key ideas | Priority |
|---|---|---|---|
| 01 | KMP and the Z-function | Prefix function, pattern matching in O(n + m), string periods | Core |
| 02 | Rolling hash and Rabin-Karp | Polynomial hashing, collision probability, double hashing, longest duplicate substring | Core |
| 03 | Palindromes and Manacher | Expand around centre; Manacher in O(n) | Core |
| 04 | Multi-pattern matching: tries and Aho-Corasick | Failure links on a trie, matching thousands of patterns in one pass, why intrusion-detection engines and `fgrep` use it | Core |
| 05 | Automata and regular expressions | DFAs and NFAs, Thompson's construction, regex matching without backtracking, why backtracking engines can blow up | Stretch |
| 06 | Suffix arrays and suffix automata | Suffix array with LCP, distinct substrings, the idea of a suffix automaton | Stretch |

### 13 — Design-style coding (week 20)

These overlap LLD and are common in rounds that blend design and code.

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Cache designs | LRU and LFU in O(1), with the linked-list and hash-map wiring |
| 02 | Iterators and streams | Flatten a nested list, peeking and zigzag iterators, median of a data stream |
| 03 | Time-based structures | Time-based key-value store, hit counter, snapshot array, rate limiter |
| 04 | In-memory systems | In-memory file system, design Twitter, autocomplete |

### 14: Theory and advanced techniques (week 21 and after)

| # | Tutorial | Key ideas | Priority |
|---|---|---|---|
| 01 | P, NP and what to do with a hard problem | Reductions and NP-completeness at interview depth, spotting an NP-hard problem, then exact search with pruning, DP over subsets, approximation, heuristics | Core |
| 02 | Randomised and approximate algorithms | Las Vegas and Monte Carlo, expected running time, a 2-approximation for vertex cover, greedy set cover, local search and simulated annealing | Stretch |
| 03 | Square-root techniques, offline queries and meet in the middle | Sqrt decomposition, Mo's algorithm, coordinate compression, offline processing, meet in the middle | Stretch |
| 04 | Persistent and rollback structures | Persistent arrays and segment trees, rollback union-find, snapshot designs | Stretch |

### 15: Pattern map and drills (week 18 and after)

| # | Tutorial | Key ideas |
|---|---|---|
| 01 | Recognising the pattern | A decision guide from the problem statement and constraints to a technique; the same problem solved three ways; the follow-up catalogue; mixed timed sets that draw on every area |

---

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
   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 04_prefix_sums_demo.cpp -o demo && ./demo
   ```

   `clang++` works the same way where its sanitizer runtime is installed (it is on macOS).

4. Areas overlap in the schedule on purpose: trees run alongside other areas from week 6 because the trees plan is long.

---

## 9. Coverage check against other syllabi

The plan was compared with the MIT 6.006 schedule (Spring 2020, 21 lectures), the Stanford CS161 schedule, the NeetCode 150 list, the CSES problem set (19 sections), the CP-Algorithms index and the table of contents of the *Competitive Programmer's Handbook*. This is where each topic lives.

| Topic in those sources | Where it is here |
|---|---|
| Asymptotics, worst-case analysis, amortised analysis | Arrays 01 |
| Recurrences, master theorem, divide and conquer, selection | Recursion 04, Sorting 05 |
| Sorting, linear-time sorts, comparison lower bound | Sorting 04 and 05 |
| Hashing, universal hashing, Bloom filters and sketches | Hashing 01 to 04 |
| Binary trees, AVL, red-black trees, heaps | Trees 01 to 16 |
| Tries, range queries, Fenwick and segment trees, LCA, union-find | Trees 17 to 28 |
| BFS, DFS, topological sort, connectivity | Graphs 01 to 04 |
| Dijkstra, Bellman-Ford, Floyd-Warshall, Johnson | Graphs 05 and 06 |
| Minimum spanning trees, SCC, bridges, Euler paths | Graphs 07 and 08 |
| Max flow, min cut, matching | Graphs 09 (stretch) |
| Dynamic programming, knapsack, LCS, bitmask DP | DP 01 to 07 |
| DP optimisations, digit DP, counting | DP 08 (stretch) |
| Greedy proofs, Huffman, scheduling | Greedy 01 and 03 |
| Number theory, combinatorics, probability | Bits and math 02 and 03 |
| Geometry | Bits and math 04 |
| String hashing, KMP, Z, Manacher | Strings 01 to 03 |
| Aho-Corasick, automata, suffix arrays | Strings 04 to 06 |
| NP-completeness (MIT lecture 19) | Theory 01 |
| Sqrt decomposition, Mo's algorithm, meet in the middle | Theory 03 (stretch) |
| Persistent structures | Theory 04 (stretch) |
| NeetCode 150: all 18 categories | Every category has a home above; the heap and trie categories sit in Trees 15 to 18, and "Math and Geometry" is split between Arrays 06 and Bits and math |

**Left out on purpose** because they are contest topics and almost never asked in interviews: fast Fourier transform and polynomials, Gaussian elimination, suffix trees, Delaunay triangulation, Burnside's lemma, push-relabel and min-cost flow beyond a mention, and game theory beyond Nim. If a team's interview list names one of them, it becomes a one-off tutorial.

---

## 10. Resources

The tutorials are self-contained; these are for a second explanation or more problems. The syllabi and problem lists were opened and read while planning this track; the video channels are listed by channel, because they were not re-watched video by video.

| Resource | Use it for |
|---|---|
| [MIT 6.006, Spring 2020](https://ocw.mit.edu/courses/6-006-introduction-to-algorithms-spring-2020/pages/calendar) | Lecture videos and notes from the mechanism up; the best free academic route through sorting, hashing, trees, graphs and DP |
| [Stanford CS161](https://web.stanford.edu/class/archive/cs/cs161/cs161.1236/index.html) | Recurrences, randomised algorithms, greedy proofs, flows; slides with careful proofs |
| Princeton *Algorithms*, Sedgewick and Wayne (book and Coursera parts I and II) | Clear implementations of symbol tables, heaps, graphs and string algorithms |
| [NeetCode 150 and 250](https://neetcode.io/practice/practice/neetcode150) | The core interview problem list with video explanations |
| [Striver's A2Z sheet](https://takeuforward.org/strivers-a2z-dsa-course/strivers-a2z-dsa-course-sheet-2/) | A longer, C++-oriented problem list (20 modules, 495 problems when checked) |
| [CSES Problem Set](https://cses.fi/problemset/) | 19 sections of graded problems with exact tests; best for building speed and rigour |
| [Competitive Programmer's Handbook](https://cses.fi/book/book.pdf) | A short book (Laaksonen) that covers the techniques in a few pages each |
| [USACO Guide](https://usaco.guide/) | Topic modules with problem lists sorted by difficulty |
| [CP-Algorithms](https://cp-algorithms.com/) | Rigorous write-ups of algorithms (segment trees, strings, graphs, number theory) |
| LeetCode company tags (Premium) | Weeks 18 to 23, company-specific sets |
| *Introduction to Algorithms* (CLRS) and *The Algorithm Design Manual* (Skiena) | Proofs when you want them; recognising which problem you are looking at |
| Video channels: MIT OpenCourseWare, NeetCode, William Fiset, Errichto, Abdul Bari, Back To Back SWE, takeUforward | A second voice on any topic; use after reading the tutorial, not instead of it |

---

## 11. Progress

| Area | Done | Next |
|---|---|---|
| 01 Arrays and strings | 01 to 07 written | Solve the problem lists |
| 02 Hashing | 01 to 04 written (area complete) | Solve its problems |
| 03 Binary search and sorting | 01 to 05 written (area complete) | Solve its problems |
| 04 Linked lists | 01 to 07 written (area complete) | Solve its problems |
| 05 Stacks and queues | 01 to 04 written (area complete) | Solve its problems |
| 06 Recursion and backtracking | 01 to 04 written (area complete) | Solve its problems |
| 07 Trees | 01 to 19 written | 20 Fenwick tree (binary indexed tree) |
| All other areas | Not started | Start in the week shown in section 3 |

Updated every time a tutorial is written or studied. The cross-track view is in [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
