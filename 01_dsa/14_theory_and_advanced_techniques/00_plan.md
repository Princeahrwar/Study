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
| 03 | `03_sqrt_offline_and_meet_in_the_middle.md` | Square-root decomposition, Mo's algorithm and offline queries, meet in the middle | Stretch | To write |
| 04 | `04_persistent_and_rollback_structures.md` | Persistent arrays and trees, rollback union-find, undo-able structures | Stretch | To write |

The titles of 02 to 04 are working names; they are fixed when each is written.

---

## Problem list

| Tutorial | Problems (LeetCode number, from memory) |
|---|---|
| 01 | Subsets (78), Partition Equal Subset Sum (416), Partition to K Equal Sum Subsets (698), Shortest Path Visiting All Nodes (847), Find the Shortest Superstring (943), Minimum Number of Work Sessions (1986), Smallest Sufficient Team (1125), Maximum Score Words Formed by Letters (1255) |
| 02 | Kth Largest Element in an Array (215), Sort an Array (912), Shuffle an Array (384), Random Pick with Weight (528), Implement Rand10() Using Rand7() (470), Generate Random Point in a Circle (478), Insert Delete GetRandom O(1) (380), Linked List Random Node (382), Count Primes (204) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | P, NP and hard problems | Written |
| 02 | Randomised and approximate algorithms | Written |
| 03 | Sqrt, offline queries and meet in the middle | To write |
| 04 | Persistent and rollback structures | To write |

Track view: [`../00_plan.md`](../00_plan.md).
