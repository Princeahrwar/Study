# Recursion and Backtracking: Area Plan

Recursion is the way most of the later areas are written: trees, graphs and dynamic programming are all recursions with a different shape. This area starts with how a recursive call really works (the call stack, the recursion tree, the depth limit, turning recursion into a loop), then uses it to search: all subsets, permutations and combinations, then constraint problems such as N-queens and Sudoku where pruning decides whether a search finishes, and finally divide and conquer, where the cost of a recursion is solved with a recurrence.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, depth or operation counts is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. A companion `*_demo.cpp` exists only where a tutorial starts a new kind of structure or feeds a project. In this area the code is inside the tutorials.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_recursion_fundamentals.md`](01_recursion_fundamentals.md) | The three questions that write a recursive function and the induction that checks it; frames and the call stack traced; the measured stack depth limit at `-O0` and `-O2` (and why a debug build overflows where a release build does not); recursion trees, the exact call count of naive Fibonacci (`2 * F(n + 1) - 1`) against memoisation (`2n - 1`), depth against work; base-case and progress bugs; recursion on a list and on a grid; tail calls and what the compiler really did; an explicit stack with work frames (Hanoi, flood fill); bounding the depth of quicksort; kernel stacks and the MISRA rule against recursion; no companion file, every listing is in the tutorial and tested | Written |
| 02 | [`02_subsets_permutations_and_combinations.md`](02_subsets_permutations_and_combinations.md) | The choose, explore, unchoose template and the decision tree; subsets three ways (include or exclude, start index, bitmask) with exact call counts; permutations with a used array and with swaps (about `e * n!` calls); skipping duplicates in Subsets II, Permutations II and Combination Sum II and why each rule has its exact form; combinations with pruning by counting; combination sum; letter combinations, generate parentheses (Catalan) and palindrome partitioning; counting the search space and a measured walk of all permutations before running anything; no companion file, every listing is tested | Written |
| 03 | [`03_constraint_backtracking.md`](03_constraint_backtracking.md) | Checking rules at placement instead of at the leaf (8-queens: 2,057 calls against 109,601); N-queens with flag arrays and with bitmasks (same tree, about 7 times faster); Valid Sudoku and a Sudoku solver with row, column and box masks, and the fewest-candidates cell order measured on three puzzles (69 million calls down to 45 thousand); word search with in-place marking and a trie with a remaining-words counter; restore IP addresses with length pruning; equal-sum groups with largest-first order and equal-bucket skipping; no companion file, every listing is tested | Written |
| 04 | [`04_divide_and_conquer_and_recurrences.md`](04_divide_and_conquer_and_recurrences.md) | The divide, conquer, combine pattern with merge sort and its measured comparison counts; writing a recurrence from code; the recursion-tree method and the master theorem with exact numeric tables (the doubling ratio as a fingerprint); counting inversions, smaller numbers after self and reverse pairs inside a merge; maximum subarray by four-number summaries; quickselect with a three-way partition and measured visits; the parenthesis problem and why overlapping parts need memoisation; closest pair of points in `n log n`; Karatsuba (fewer operations, measured as slower here); where divide and conquer parallelises; no companion file, every listing is tested | Written |

---

## Problem list (about 30 with practice)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them. Difficulty labels are given only where the tutorial that lists the problem verified them. The numbers and titles of the problems in tutorial 01 were checked when it was written; those of tutorials 02 to 04 were checked again when each was written.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Fibonacci Number (509), Climbing Stairs (70), Pow(x, n) (50), Reverse Linked List (206), Merge Two Sorted Lists (21), Swap Nodes in Pairs (24), K-th Symbol in Grammar (779), Flood Fill (733), Number of Islands (200), Max Area of Island (695); in your editor: a recursion turned into an explicit stack. More practice: Reverse String (344) |
| 02 | Subsets (78), Subsets II (90), Permutations (46), Permutations II (47), Combinations (77), Combination Sum (39), Combination Sum II (40), Letter Combinations of a Phone Number (17), Generate Parentheses (22), Palindrome Partitioning (131) |
| 03 | N-Queens (51), N-Queens II (52), Valid Sudoku (36), Sudoku Solver (37), Word Search (79), Word Search II (212), Restore IP Addresses (93), Partition to K Equal Sum Subsets (698), Matchsticks to Square (473) |
| 04 | Maximum Subarray (53), Count of Smaller Numbers After Self (315), Reverse Pairs (493), Kth Largest Element in an Array (215), Different Ways to Add Parentheses (241), Multiply Strings (43); in your editor: the closest pair of points |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- Sedgewick and Wayne, *Algorithms*, sections 1.1 (recursion) and 2.3 (quicksort's recursion depth).
- CLRS chapter 4 "Divide-and-Conquer": the recursion-tree method and the master theorem (used from tutorial 04).
- Linux kernel documentation on stack usage and `CONFIG_FRAME_WARN`: why kernel code avoids deep recursion.
- NeetCode 150, section "Backtracking" (neetcode.io): the same problems with video walkthroughs.
- The Algorithms for Competitive Programming site (cp-algorithms.com): "Recursion" style problems with proofs for the search-space size.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Recursion fundamentals | Written |
| 02 | Subsets, permutations and combinations | Written |
| 03 | Constraint backtracking | Written |
| 04 | Divide and conquer and recurrences | Written |

All four tutorials are written, so the recursion and backtracking area is complete. Solve the problems at the end of each one. The DSA track continues in [`../07_trees/00_plan.md`](../07_trees/00_plan.md), where Trees 01 and 02 are written. Track view: [`../00_plan.md`](../00_plan.md).
