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
| 04 | [`04_prefix_sums_and_difference_arrays.md`](04_prefix_sums_and_difference_arrays.md) | Range sums in O(1), subarray sum equals K with negatives, prefix XOR and remainders, 2D prefix sums, range updates with difference arrays, weighted random pick | Written |
| 05 | [`05_subarray_patterns_and_kadane.md`](05_subarray_patterns_and_kadane.md) | Maximum subarray (Kadane, indices, divide and conquer), one deletion, maximum rectangle sum, maximum product, circular case, Boyer-Moore and Misra-Gries heavy hitters, the stock-trading family (one, many, fee, cooldown, k transactions) | Written |
| 06 | [`06_matrix_traversal_and_inplace_tricks.md`](06_matrix_traversal_and_inplace_tricks.md) | Spiral and diagonal order, rotate and flip, set zeroes in O(1) space, index marking (first missing positive, Floyd's cycle detection for the duplicate), Game of Life bit packing, next permutation | Written |
| 07 | [`07_string_basics.md`](07_string_basics.md) | Bytes versus characters, anagrams and canonical keys, palindromes by expanding around centres, parsing edge cases (atoi, split, version compare, reverse words, length-prefixed encoding), big-number add and multiply on strings, building strings efficiently, memmove and bounded copies | Written |

---

## Problem list (about 40)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Contains Duplicate (217), Remove Element (27), Remove Duplicates from Sorted Array (26), Move Zeroes (283), Best Time to Buy and Sell Stock (121), Merge Sorted Array (88), Rotate Array (189), Product of Array Except Self (238), Two Sum (1) |
| 02 | Valid Palindrome (125), Two Sum II (167), Container With Most Water (11), 3Sum (15), Sort Colors (75), Trapping Rain Water (42, hard) |
| 03 | Maximum Average Subarray I (643), Longest Substring Without Repeating Characters (3), Minimum Size Subarray Sum (209), Permutation in String (567), Longest Repeating Character Replacement (424), Minimum Window Substring (76, hard) |
| 04 | Range Sum Query - Immutable (303), Subarray Sum Equals K (560), Contiguous Array (525), Subarray Sums Divisible by K (974), Range Sum Query 2D (304), Corporate Flight Bookings (1109); more: Car Pooling (1094), Random Pick with Weight (528) |
| 05 | Maximum Subarray (53), Maximum Product Subarray (152), Maximum Sum Circular Subarray (918), Majority Element (169), Best Time to Buy and Sell Stock II (122); more: Majority Element II (229), Stock with Cooldown (309), Stock with Transaction Fee (714), Stock III and IV (123, 188), Delete One Element for Maximum Subarray (1186), Maximum Absolute Sum of Any Subarray (1749), Longest Turbulent Subarray (978), Max Sum of Rectangle No Larger Than K (363, hard) |
| 06 | Rotate Image (48), Spiral Matrix (54), Set Matrix Zeroes (73), First Missing Positive (41), Next Permutation (31), Find the Duplicate Number (287); more: Spiral Matrix II (59), Diagonal Traverse (498), Transpose Matrix (867), Toeplitz Matrix (766), Game of Life (289), Find All Duplicates (442), Find All Numbers Disappeared (448), Missing Number (268), Permutation Sequence (60) |
| 07 | Valid Anagram (242), Group Anagrams (49), Longest Palindromic Substring (5), String to Integer (8), Multiply Strings (43), Compare Version Numbers (165); more: First Unique Character (387), Palindromic Substrings (647), Reverse Words (151), Add Strings (415), Encode and Decode Strings (271) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- NeetCode 150, sections "Arrays & Hashing", "Two Pointers" and "Sliding Window" (neetcode.io): same problems with video walkthroughs.
- Striver's A2Z sheet, arrays section (takeuforward.org): more problems per pattern.
- USACO Guide, "Prefix Sums" and "More Prefix Sums" (usaco.guide/silver): the 1D, 2D and difference-array material with graded problems, for tutorial 04.
- CSES Problem Set, "Range Queries" and "Sliding Window Problems" (cses.fi/problemset): exact-test practice once the LeetCode problems feel easy. "Maximum Subarray Sum" and "Maximum Subarray Sum II" (Sorting and Searching) are the Kadane and prefix-minimum problems of tutorial 05 with exact tests.
- NeetCode 150, sections "Arrays & Hashing", "Greedy" and "Math & Geometry": 53, 169, 48, 54, 73, 242, 49 with walkthroughs, for tutorials 05 to 07.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Complexity and arrays in memory | Written |
| 02 | Two pointers | Written |
| 03 | Sliding window | Written |
| 04 | Prefix sums and difference arrays | Written |
| 05 | Subarray patterns and Kadane | Written |
| 06 | Matrix traversal and in-place tricks | Written |
| 07 | String basics | Written |

Track view: [`../00_plan.md`](../00_plan.md).
