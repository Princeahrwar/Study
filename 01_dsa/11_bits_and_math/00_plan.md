# Bits and Math: Area Plan

Bit tricks, number theory, counting and probability, and a little geometry. These are the topics that show up as "clever" problems in coding rounds and as everyday tools in systems code: masks and flags, modular arithmetic for hashes and checksums, random sampling for load balancing, and exact integer geometry. Each one has a small set of ideas that repeat.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, counts or failure rates is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. There is no companion demo file: the code is inside the tutorial.
5. Every function in the code is tested against a brute force or the real operator on small and random inputs, under AddressSanitizer and UBSan.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_bit_manipulation.md`](01_bit_manipulation.md) | The three identities (`x & (x - 1)`, `x & -x`, `x \| (x + 1)`), four ways to count bits, `ctz`/`clz`/log2/reverse/parity, XOR puzzles (single number in the once, twice and thrice forms, missing number, `xor_upto`), Gray code, counting bits over ranges and pairs, bitmasks as sets (submask enumeration and its `3^n` cost, Gosper's hack, sum over subsets), addition/division/multiplication with bits, AND of a range, maximum XOR | Written |
| 02 | `02_number_theory_and_arithmetic.md` | GCD and LCM, modular arithmetic and fast exponentiation, modular inverse, the sieve, factorisation, overflow-safe arithmetic, `Pow(x, n)`, square root by Newton's method | To write |
| 03 | `03_combinatorics_probability_and_sampling.md` | nCr mod p, Catalan numbers, inclusion-exclusion, expected value, Fisher-Yates, reservoir sampling, weighted sampling and the alias method, rejection sampling | To write |
| 04 | `04_geometry_for_interviews.md` | Cross product and orientation, segment intersection, polygon area, convex hull, exact arithmetic for points on a line, rectangle overlap, k closest points, closest pair | To write |

---

## Problem list (about 16 with practice)

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Number of 1 Bits (191), Power of Two (231), Power of Four (342), Counting Bits (338), Single Number (136), Single Number II (137), Single Number III (260), Missing Number (268), Reverse Bits (190), Hamming Distance (461), Total Hamming Distance (477), Bitwise AND of Numbers Range (201), Sum of Two Integers (371), Divide Two Integers (29), Maximum XOR of Two Numbers in an Array (421), Subsets (78), Gray Code (89), UTF-8 Validation (393) |
| 02 to 04 | Added when each tutorial is written |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- Sean Eron Anderson, "Bit Twiddling Hacks" (Stanford, free online): the standard catalogue of tricks, including the SWAR popcount and the reverse.
- Henry S. Warren, *Hacker's Delight*: the book on bit-level arithmetic.
- Knuth, *The Art of Computer Programming*, volume 4A, section 7.1: bitwise tricks and techniques.
- Competitive Programmer's Handbook (Antti Laaksonen, free): the bit manipulation chapter, including subset enumeration.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Bit manipulation | Written |
| 02 | Number theory and arithmetic | To write |
| 03 | Combinatorics, probability and sampling | To write |
| 04 | Geometry for interviews | To write |

Track view: [`../00_plan.md`](../00_plan.md).
