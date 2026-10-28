# Binary Search and Sorting: Area Plan

Binary search is the algorithm every interviewer expects you to write correctly in five minutes, and sorting is the preprocessing step behind half the other patterns. This area teaches binary search from one template with a loop invariant (so there is no off-by-one to guess), then searching on an answer, the awkward shapes (rotated arrays, peaks, matrices, two arrays), how the sorting algorithms work inside and what `std::sort` does, and the comparator and selection patterns that interviews build on them.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, probe counts or comparisons is checked by the companion code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_binary_search_templates.md`](01_binary_search_templates.md) | One half-open template ("first index where a predicate is true") with its loop invariant, termination and exit argument; `lower_bound` and `upper_bound` and everything built from them (contains, count, first and last position, insertion point, floor, ceiling); searching over values (integer square root); the closed-interval template and the stall of `lo = mid`; the overflowing middle, unsigned sizes; what the search returns when the predicate is not monotone; probe counts; a branch-free version measured with two compilers; where it shows up (`git bisect`, kernel `bsearch`); every function tested against the standard library on all small sorted arrays | Written |
| 02 | [`02_binary_search_on_the_answer.md`](02_binary_search_on_the_answer.md) | The recipe (goal, test, monotonicity, range, `firstTrue`) and its cost; Koko eating bananas, ship capacity (with why the greedy test is right) and split array largest sum as one problem; maximising by searching the first "too large" value and subtracting one (magnetic force); a range whose end means "none" (bouquets, with the overflow in `m * k`); a count as the test (k-th smallest in a sorted matrix); real numbers with a fixed number of halvings and why the epsilon loop can run forever; doubling when there is no upper bound; non-monotone tests (`==`, thread counts); where it shows up (batch-size search, trace-replay sizing); every function tested against brute force or an independent dynamic program | Written |
| 03 | [`03_rotated_peak_and_2d_search.md`](03_rotated_peak_and_2d_search.md) | One idea for every shape: find a yes/no question about an index that goes no, ..., yes, ..., yes, then call `firstTrue`; rotated arrays (distinct values, then duplicates with the O(n) worst case and why it cannot be avoided); peaks and why they work, flat spots that break them, the single element among pairs; the two matrix searches (flat index and staircase) with a cost table; k-th element and median of two sorted arrays by cutting both; ternary search and what limits its precision; exponential search measured against plain binary search; a short companion file tested on all small inputs; five paper problems and eight code problems | Written |
| 04 | Sorting algorithms inside | Merge sort, quicksort with Lomuto and Hoare partitions, heapsort, counting, bucket and radix sort, the n log n lower bound for comparison sorts, stability, introsort inside `std::sort`, external merge sort | To write |
| 05 | Sorting patterns and comparators | Strict weak ordering (and the crash when you break it), quickselect and median of medians, top-K by heap versus selection versus bucket, counting inversions with merge sort, interval sorting | To write |

---

## Problem list (about 25)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Search Insert Position (35), Find First and Last Position (34), First Bad Version (278), Find K Closest Elements (658), Successful Pairs of Spells and Potions (2300); in your own editor: `firstTrue` with a differential test, a branch-free lower bound. More practice: 704, 69, 367, 744, 475, 436 |
| 02 | Koko Eating Bananas (875), Capacity To Ship Packages Within D Days (1011), Split Array Largest Sum (410), Minimum Number of Days to Make m Bouquets (1482), Find the Smallest Divisor Given a Threshold (1283), Magnetic Force Between Two Balls (1552), Kth Smallest Element in a Sorted Matrix (378). More practice: 1870, 1760, 2064, 2187, 719, 668; CSES Factory Machines and Array Division |
| 03 | Search in Rotated Sorted Array (33), Find Minimum in Rotated Sorted Array (153), Find Peak Element (162), Search a 2D Matrix (74), Search a 2D Matrix II (240), Single Element in a Sorted Array (540), Time Based Key-Value Store (981), Median of Two Sorted Arrays (4, hard), Find Minimum in Rotated Sorted Array II (154), Search in Rotated Sorted Array II (81), Peak Index in a Mountain Array (852); more practice: Find in Mountain Array (1095), H-Index II (275), Find a Peak Element II (1901) |
| 04 | Sort an Array (912), Sort List (148), Maximum Gap (164), Count of Smaller Numbers After Self (315), Reverse Pairs (493) (planned) |
| 05 | Kth Largest Element in an Array (215), Largest Number (179), Top K Frequent Elements (347), Custom Sort String (791), Merge Intervals (56), Meeting Rooms II (253) (planned) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- Jon Bentley, *Programming Pearls*, column 4 "Writing Correct Programs": the binary search as the example of reasoning with invariants. Joshua Bloch, "Nearly All Binary Searches and Mergesorts are Broken" (Google Research blog, 2006): the overflow bug in the JDK's binary search. For tutorial 01.
- Knuth, *The Art of Computer Programming*, volume 3, section 6.2.1 "Searching an ordered table", and volume 3 chapter 5 "Sorting": the classic analyses.
- MIT 6.006, Spring 2020 (ocw.mit.edu): the sorting lectures (insertion, selection, merge, heap, counting and radix sort).
- CLRS chapters 6 to 8 (heapsort, quicksort, sorting in linear time) and Sedgewick and Wayne, *Algorithms*, chapter 2 "Sorting".
- Algorithmica, "Binary Search" (en.algorithmica.org/hpc/data-structures/binary-search): the branch-free search and the Eytzinger layout beyond tutorial 01.
- CSES Problem Set, "Sorting and Searching" (cses.fi/problemset): exact-test practice.
- NeetCode 150, section "Binary Search" (neetcode.io): the same problems with video walkthroughs.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Binary search templates | Written |
| 02 | Binary search on the answer | Written |
| 03 | Rotated, peak and 2D search | Written |
| 04 | Sorting algorithms inside | To write |
| 05 | Sorting patterns and comparators | To write |

Track view: [`../00_plan.md`](../00_plan.md).
