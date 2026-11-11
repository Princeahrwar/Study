# Pattern Map and Drills: Area Plan

How to choose the technique when the problem does not name it: reading the constraints, a table from the words of a statement to a technique, one problem solved several ways, the follow-up questions interviewers ask, and mixed timed sets that draw on every earlier area. This is the last DSA area; it starts in week 18, after the technique areas are done, and continues as practice.

**Language:** C++ · **Time:** about 1 week for the tutorial, then timed sets on every practice day · **Order:** read the tutorial, run the four timed sets at its end, then go to the company-tagged lists.

---

## How the tutorial is built

1. Starts from the constraints and builds up to the follow-up questions.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, counts or failure rates is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. There is no companion demo file: the code is inside the tutorial.
5. Every function in the code is tested against a brute force or a simple model on small and random inputs, under AddressSanitizer and UBSan.

---

## Tutorials

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 01 | [`01_recognising_the_pattern.md`](01_recognising_the_pattern.md) | Reading the constraints (a table from `n` to the affordable growth rate, with one job per class measured at 5 to 160 ms), a table from the words of the statement to a technique with the area that teaches it, subarrays with sum `k` three ways (brute force 213 ms, hash map 2 ms, two pointers 0.4 ms at 30,000 elements) and what breaks when values go negative (two pointers wrong on 1,791 of 2,000 arrays), the k-th largest by sort, heap and selection (the heap loses its edge as `k` grows), a catalogue of follow-ups, how to run a timed set, 20 statements to name the pattern for, and four mixed timed sets of 16 problems | Core | Written |

---

## Problem list

| Tutorial | Problems (LeetCode number, from memory) |
|---|---|
| 01 | Set 1: Two Sum (1), Longest Substring Without Repeating Characters (3), Merge Intervals (56), Number of Islands (200). Set 2: Subarray Sum Equals K (560), Top K Frequent Elements (347), Course Schedule (207), Coin Change (322). Set 3: Daily Temperatures (739), Kth Largest Element in an Array (215), Word Search (79), Lowest Common Ancestor of a Binary Search Tree (235). Set 4: Trapping Rain Water (42), Median of Two Sorted Arrays (4), Word Ladder (127), Minimum Window Substring (76) |

After the four sets, use company-tagged lists for the target companies, as the [track plan](../00_plan.md) says. Log every attempt in [`../practice_log.md`](../practice_log.md), with the pattern you named before coding.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Recognising the pattern | Written |

Track view: [`../00_plan.md`](../00_plan.md).
