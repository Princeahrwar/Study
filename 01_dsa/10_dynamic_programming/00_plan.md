# Dynamic Programming: Area Plan

Dynamic programming (DP) solves a problem by writing its answer in terms of the answers to smaller versions of the same problem, and making sure each smaller version is solved **once**. It is the tool for problems where greedy fails (the previous area) and brute force is exponential: counting paths, the longest common subsequence, edit distance, knapsack, game values, and the shortest tour through a few cities. The skill is almost entirely in choosing the **state** (what the subproblem is) and the **order** (which subproblems come first); the code is short once those are right. This area teaches the method on one-dimensional problems first, then two sequences, knapsack, intervals and games, bitmasks, trees and DAGs, and finally counting and optimisation techniques.

**Language:** C++ · **Time:** about 3 weeks · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, counts or failure rates is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. There is no companion demo file: the code is inside the tutorial.
5. Every DP in the code is tested against a brute-force search on small inputs, and the memoised, tabulated and space-optimised versions are checked against each other.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_dp_fundamentals.md`](01_dp_fundamentals.md) | Overlapping subproblems and optimal substructure; Fibonacci from exponential recursion to memoisation, tabulation and O(1) space, with measured call counts; the five-step recipe (state, transition, base, order, answer); stairs and house robber in all three forms plus reconstruction of the choice; grid paths with obstacles and a rolling row; designing the state (circular robber, LIS ending at i, coin change); sparse state spaces and memoisation with a hash map (Collatz, where the map turned out slower than recomputing); recursion depth and stack frames; how to test a DP | Written |
| 02 | [`02_one_dimensional_dp.md`](02_one_dimensional_dp.md) | Coin change (fewest coins, number of ways), word break, decode ways, perfect squares, maximum subarray and maximum product, LIS in O(n^2) and in O(n log n) with patience sorting, counting LIS | Written |
| 03 | [`03_grid_and_two_sequence_dp.md`](03_grid_and_two_sequence_dp.md) | Unique paths, minimum path sum, maximal square, LCS, edit distance, distinct subsequences, interleaving strings, wildcard and regular-expression matching; recovering the alignment | Written |
| 04 | [`04_the_knapsack_family.md`](04_the_knapsack_family.md) | 0/1 and unbounded knapsack, why the loop direction matters, subset sum, partition, target sum, bounded knapsack by binary splitting, bitset speed-up | Written |
| 05 | [`05_interval_and_game_dp.md`](05_interval_and_game_dp.md) | Interval DP (matrix chain, burst balloons, palindrome partitioning, strange printer), stone games and "predict the winner" as minimax DP, Nim and the idea behind Sprague-Grundy | Written |
| 06 | `06_bitmask_and_state_machine_dp.md` | Bitmask DP (the travelling-salesman tour, assignment problems, partition into equal-sum subsets), stock problems as state machines, profile DP on a grid | To write |
| 07 | `07_dp_on_trees_and_dags.md` | House robber III, tree cameras, diameter, rerooting, longest path in a DAG and DP over a topological order; links to the trees area | To write |
| 08 | `08_counting_digit_dp_and_optimisations.md` | Counting with a modulus, digit DP, expected-value DP, divide-and-conquer optimisation, Knuth optimisation, the convex hull trick (stretch) | To write |

---

## Problem list (about 42 with practice)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them. Numbers and titles are from memory (flagged in each tutorial); check a number before you rely on it.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Fibonacci Number (509), Climbing Stairs (70), Min Cost Climbing Stairs (746), House Robber (198), House Robber II (213), Unique Paths (62), Unique Paths II (63), Longest Increasing Path in a Matrix (329) as memoisation on a grid; in your editor: a memoisation harness that counts states and calls, and Collatz chain lengths with a cache |
| 02 | Coin Change (322), Coin Change II (518), Word Break (139), Decode Ways (91), Perfect Squares (279), Maximum Subarray (53), Maximum Product Subarray (152), Longest Increasing Subsequence (300), Number of Longest Increasing Subsequence (673) |
| 03 | Minimum Path Sum (64), Maximal Square (221), Longest Common Subsequence (1143), Edit Distance (72), Distinct Subsequences (115), Interleaving String (97), Wildcard Matching (44), Regular Expression Matching (10) |
| 04 | Partition Equal Subset Sum (416), Target Sum (494), Last Stone Weight II (1049), Ones and Zeroes (474), Coin Change II (518), Combination Sum IV (377) |
| 05 | Burst Balloons (312), Palindrome Partitioning II (132), Longest Palindromic Substring (5), Strange Printer (664), Stone Game (877), Predict the Winner (486), Minimum Cost to Merge Stones (1000) |
| 06 | Shortest Path Visiting All Nodes (847), Partition to K Equal Sum Subsets (698), Best Time to Buy and Sell Stock series (121, 122, 123, 188, 309, 714), Number of Ways to Wear Different Hats (1434) |
| 07 | House Robber III (337), Binary Tree Cameras (968), Diameter of Binary Tree (543), Longest Increasing Path in a Matrix (329), Unique Binary Search Trees (96) |
| 08 | Number of Dice Rolls With Target Sum (1155), Numbers At Most N Given Digit Set (902), Number of Digit One (233), Count Special Integers (2376), Knight Probability in Chessboard (688), Soup Servings (808) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- CLRS chapter 14 "Dynamic Programming" (rod cutting, matrix-chain multiplication, longest common subsequence, optimal binary search trees): the standard text for the method. The chapter number differs between editions; check yours.
- Jeff Erickson, *Algorithms*, chapter on dynamic programming (free online): recursion first, then memoisation, then the table; the best treatment of "design the recursion before the code".
- Kleinberg and Tardos, *Algorithm Design*, chapter 6: weighted interval scheduling, segmented least squares, knapsack, sequence alignment.
- Competitive Programmer's Handbook (Antti Laaksonen, free): the dynamic programming chapter, including bitmask DP and counting.
- NeetCode 150, sections "1-D Dynamic Programming" and "2-D Dynamic Programming" (neetcode.io): the same problems with video walkthroughs.
- The Atcoder Educational DP Contest (26 problems A to Z, atcoder.jp): a graded set that covers nearly every pattern in this area.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | DP fundamentals | Written |
| 02 | One-dimensional DP | Written |
| 03 | Grid and two-sequence DP | Written |
| 04 | The knapsack family | Written |
| 05 | Interval and game DP | Written |
| 06 | Bitmask and state-machine DP | To write |
| 07 | DP on trees and DAGs | To write |
| 08 | Counting, digit DP and optimisations | To write |

Next: tutorial 06. Track view: [`../00_plan.md`](../00_plan.md).
