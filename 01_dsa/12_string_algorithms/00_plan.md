# String Algorithms: Area Plan

Searching and analysing text: pattern matching in linear time, hashing substrings, palindromes, matching many patterns at once, automata and regular expressions, and suffix structures. Interviews ask for KMP, rolling hashes and palindromes directly; systems work uses the rest (intrusion detection, `grep`, log search, compilers, bioinformatics).

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

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 01 | [`01_kmp_and_the_z_function.md`](01_kmp_and_the_z_function.md) | Why the naive search is `O(n m)`, borders and the prefix function (and why it is linear), KMP search, the Z-function and Z search, smallest period and repeated blocks, all borders, the shortest palindrome by prepending, prefix occurrence counts, rotation, the KMP automaton, and measured comparisons with `std::string::find`, `strstr` and the naive loop | Core | Written |
| 02 | [`02_rolling_hash_and_rabin_karp.md`](02_rolling_hash_and_rabin_karp.md) | Polynomial substring hashes in `O(1)`, arithmetic modulo `2^61 - 1`, Rabin-Karp with and without verification (Las Vegas and Monte Carlo), the collision bound `(L-1)/M` and the birthday effect, why `mod 2^64` and a fixed base are broken (Thue-Morse), longest duplicate and common substring by binary search, `O(1)` palindrome queries, measured costs | Core | Written |
| 03 | `03_palindromes_and_manacher.md` | Expand around centre; Manacher in O(n) | Core | To write |
| 04 | `04_multi_pattern_matching_tries_and_aho_corasick.md` | Failure links on a trie, matching thousands of patterns in one pass (builds on Trees 18) | Core | To write |
| 05 | `05_automata_and_regular_expressions.md` | DFAs and NFAs, Thompson's construction, regex matching without backtracking, why backtracking engines can blow up | Stretch | To write |
| 06 | `06_suffix_arrays_and_automata.md` | Suffix array with LCP, distinct substrings, the idea of a suffix automaton | Stretch | To write |

---

## Problem list

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Find the Index of the First Occurrence in a String (28), Repeated Substring Pattern (459), Shortest Palindrome (214), Longest Happy Prefix (1392), Rotate String (796), Repeated String Match (686), Maximum Repeating Substring (1668), Sum of Scores of Built Strings (2223), Find All Good Strings (1397, hard) |
| 02 | Find the Index of the First Occurrence in a String (28), Repeated DNA Sequences (187), Longest Duplicate Substring (1044), Maximum Length of Repeated Subarray (718), Longest Common Subpath (1923), Distinct Echo Substrings (1316), Longest Chunked Palindrome Decomposition (1147), Find Substring With Given Hash Value (2156), Longest Happy Prefix (1392) |
| 03 to 06 | Added when each tutorial is written |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | KMP and the Z-function | Written |
| 02 | Rolling hash and Rabin-Karp | Written |
| 03 | Palindromes and Manacher | To write |
| 04 | Multi-pattern matching: tries and Aho-Corasick | To write |
| 05 | Automata and regular expressions | To write |
| 06 | Suffix arrays and suffix automata | To write |

Track view: [`../00_plan.md`](../00_plan.md).
