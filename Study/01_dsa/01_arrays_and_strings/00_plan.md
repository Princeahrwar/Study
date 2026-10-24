# Arrays and Strings: Area Plan

Arrays are the first thing every other structure is compared with, and most interview problems start with one. This area teaches how an array really sits in memory, then the handful of patterns that solve most array and string problems.

**Language:** C++ · **Time:** about 2 weeks · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_complexity_and_arrays_in_memory.md`](01_complexity_and_arrays_in_memory.md) | Big-O and how to count it, amortised cost, how an array sits in memory, cache effects, in-place basics | Written |
| 02 | [`02_two_pointers.md`](02_two_pointers.md) | Opposite ends, one pointer per sequence, partition zones, why a move never skips the answer | Written |
| 03 | [`03_sliding_window.md`](03_sliding_window.md) | Fixed and variable windows, longest and shortest templates, "at most K" counting, when a window fails | Written |
| 04 | Prefix sums and difference arrays | Subarray sums with a hash map, 2D prefix sums, range updates | Next |
| 05 | Subarray patterns and Kadane | Maximum subarray, maximum product, circular case | |
| 06 | Matrix traversal and in-place tricks | Rotate, spiral, set zeroes, index marking | |
| 07 | String basics | Counting, anagrams, palindromes, building strings efficiently | |

---

## Problem list (about 35)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Contains Duplicate (217), Remove Element (27), Remove Duplicates from Sorted Array (26), Move Zeroes (283), Best Time to Buy and Sell Stock (121), Merge Sorted Array (88), Rotate Array (189), Product of Array Except Self (238), Two Sum (1) |
| 02 | Valid Palindrome (125), Two Sum II (167), Container With Most Water (11), 3Sum (15), Sort Colors (75), Trapping Rain Water (42, hard) |
| 03 | Maximum Average Subarray I (643), Longest Substring Without Repeating Characters (3), Minimum Size Subarray Sum (209), Permutation in String (567), Longest Repeating Character Replacement (424), Minimum Window Substring (76, hard) |
| 04 | Range Sum Query - Immutable (303), Subarray Sum Equals K (560), Contiguous Array (525), Subarray Sums Divisible by K (974) |
| 05 | Maximum Subarray (53), Maximum Product Subarray (152), Maximum Sum Circular Subarray (918) |
| 06 | Rotate Image (48), Spiral Matrix (54), Set Matrix Zeroes (73), First Missing Positive (41) |
| 07 | Valid Anagram (242), Group Anagrams (49), Longest Palindromic Substring (5) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- NeetCode 150, sections "Arrays & Hashing", "Two Pointers" and "Sliding Window" (neetcode.io): same problems with video walkthroughs.
- Striver's A2Z sheet, arrays section (takeuforward.org): more problems per pattern.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Complexity and arrays in memory | Written |
| 02 | Two pointers | Written |
| 03 | Sliding window | Written |
| 04–07 | Remaining tutorials | Written one at a time when you say "next" |

Track view: [`../00_plan.md`](../00_plan.md).
