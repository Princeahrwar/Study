# Theory and Advanced Techniques: Area Plan

What to do when a problem is hard or the data is huge: NP-hardness at interview depth, randomised and approximate algorithms, square-root decomposition, offline queries and meet in the middle, and persistent or rollback structures. The first tutorial is core; the other three are stretch material for senior interviews.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, counts or failure rates is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. There is no companion demo file: the code is inside the tutorial.
5. Every function in the code is tested against a brute force or a simple model on small and random inputs, under AddressSanitizer and UBSan.

---

## Tutorials

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 01 | [`01_p_np_and_hard_problems.md`](01_p_np_and_hard_problems.md) | P, NP and certificates (checking a Hamiltonian cycle in about 30 ns against 54 ms to find one at 20 dense vertices), the tractable twin of each hard problem, reductions and their direction, independent set, vertex cover and clique as one problem, 3-SAT to independent set with the assignment read back, exact search (all subsets, branching with safe reductions), the travelling salesman (brute force, Held-Karp, the MST 2-approximation, nearest neighbour and 2-opt measured against the optimum), a small parameter (vertex cover in `2^k`), small numbers (pseudo-polynomial subset sum), what to say in an interview | Core | Written |
| 02 | [`02_randomised_and_approximate_algorithms.md`](02_randomised_and_approximate_algorithms.md) | Las Vegas and Monte Carlo, one-sided error and amplification, randomised quicksort (exact expectation `2(n+1)H(n) - 4n`, 39,499 measured against 39,520) and quickselect, Freivalds' matrix product check, Miller-Rabin against Fermat and Carmichael numbers (twelve bases exact for 64 bits), Karger's contraction, the matching 2-approximation for vertex cover, greedy set cover and the doubling family where it takes `k` sets against 2, maximum cut by random split, local search (at least `m/2`) and simulated annealing, with a generator seeded for repeatable runs | Stretch | Written |
| 03 | [`03_sqrt_offline_and_meet_in_the_middle.md`](03_sqrt_offline_and_meet_in_the_middle.md) | The `B + n/B` balance and why the best block size was 64 and not `sqrt(n)` (measured), blocks with a lazy value and sorted copies (range add, sum, count at least `x`), square root on degrees for triangle counting (about 30 times faster than intersecting lists with a hub), Mo's algorithm with the odd-even order (26.4 against 41.9 million moves), coordinate compression and the Fenwick sweep for distinct values (faster than Mo), meet in the middle for subset sum (44 numbers in about a second), bidirectional BFS (176 against 95,491 expansions on a random graph, no gain on a grid corner to corner) | Stretch | Written |
| 04 | [`04_persistent_and_rollback_structures.md`](04_persistent_and_rollback_structures.md) | Versions and the ways to keep them, fat nodes for a snapshot array (399,993 entries against 40 GB of copies), path copying for a persistent array (17 nodes per update, 21 MB for 100,001 versions against 26 GB), a persistent segment tree for the k-th smallest in a range (about 1 microsecond a query against 0.43 ms by selecting), union-find with rollback, offline dynamic connectivity by a segment tree over time (under 0.2 s against about 20 s by rebuilding), undo-log transactions (about 1,000 times faster than copying the map) | Stretch | Written |


---

## Problem list

| Tutorial | Problems (LeetCode number, from memory) |
|---|---|
| 01 | Subsets (78), Partition Equal Subset Sum (416), Partition to K Equal Sum Subsets (698), Shortest Path Visiting All Nodes (847), Find the Shortest Superstring (943), Minimum Number of Work Sessions (1986), Smallest Sufficient Team (1125), Maximum Score Words Formed by Letters (1255) |
| 02 | Kth Largest Element in an Array (215), Sort an Array (912), Shuffle an Array (384), Random Pick with Weight (528), Implement Rand10() Using Rand7() (470), Generate Random Point in a Circle (478), Insert Delete GetRandom O(1) (380), Linked List Random Node (382), Count Primes (204) |
| 03 | Closest Subsequence Sum (1755), Tallest Billboard (956), Split Array With Same Average (805), Word Ladder (127), Open the Lock (752), Range Sum Query - Mutable (307), Count of Smaller Numbers After Self (315), Count of Range Sum (327), Reverse Pairs (493) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | P, NP and hard problems | Written |
| 02 | Randomised and approximate algorithms | Written |
| 03 | Sqrt, offline queries and meet in the middle | Written |
| 04 | Persistent and rollback structures | Written |

Track view: [`../00_plan.md`](../00_plan.md).
