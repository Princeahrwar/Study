# DSA 12.01: KMP and the Z-function

*Part of the [String algorithms plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [String basics](../01_arrays_and_strings/07_string_basics.md) (how `std::string` stores and compares text), [Two pointers](../01_arrays_and_strings/02_two_pointers.md) (the pattern of two indices that only move forward), and [Prefix sums](../01_arrays_and_strings/04_prefix_sums_and_difference_arrays.md) (the "precompute once, answer fast" habit)*

Finding a short pattern inside a long text is the oldest string problem, and it has a subtle trap: the obvious search can take time proportional to the **product** of the two lengths. The fix, due to Knuth, Morris and Pratt, is to remember what the text has already told you so that the search never moves backwards. The same idea gives two linear-time tools, the **prefix function** and the **Z-function**, which answer far more than "where is the pattern": the shortest repeating block of a string, all of its borders, the shortest palindrome you can make by adding characters in front, and more.

By the end you can:

- say why the naive search is quadratic, and build an input that makes it so;
- compute the prefix function and the Z-function in `O(n)`, and explain why the loops are linear even though they contain `while` loops;
- write KMP search from memory, with overlapping matches and no off-by-one at the end of the pattern;
- use the prefix function to find the smallest period, all borders, the shortest palindrome by prepending, and the number of occurrences of each prefix;
- build the KMP automaton, and say when to prefer it;
- say honestly when KMP is *not* the fastest choice in practice.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 1,182,809 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran: `prefix_function` and `z_function` against direct definitions (try every length, compare the characters) on 20,000 random strings over alphabets of 1, 2 and 3 letters, on runs of one letter, on Fibonacci strings and on hand-picked strings, plus the property `pi[i] <= pi[i-1] + 1`; the four searches (`kmp_find_all`, `z_find_all`, the automaton, and `std::string::find` in a loop) against the naive search on 40,000 random text and pattern pairs, with overlapping matches; an instrumented copy of the KMP loop, which never made more than `2n` character comparisons (on random inputs and on the worst case of section 1); smallest period, repetition test, longest happy prefix and all borders against `O(n^2)` loops; shortest palindrome against a try-every-prefix loop; the occurrence counts of every prefix against direct counting; and rotation against trying all rotations. I also broke the code on purpose in four ways to check that the tests notice. Three were caught. The fourth, removing the `k == m` guard in the search loop, was not, and rightly: for a `std::string` the character after the end is `'\0'`, which never equals a text character, so the loop still behaves; the guard is there so the code does not rely on that.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs.

---

## 1. The problem, and why the obvious search is slow

Given a **text** `t` of length `n` and a **pattern** `p` of length `m`, report every position where `p` occurs in `t`. Occurrences may overlap: `aa` occurs at positions 0, 1, 2 and 3 in `aaaaa`.

The obvious method tries each start position and compares left to right until the first mismatch:

```cpp
// The obvious search: try every start, compare left to right, stop at the first mismatch.  O(n m) in the worst case.
vector<int> naive_find_all(const string& t, const string& p) {
    vector<int> r; int n = (int)t.size(), m = (int)p.size();
    for (int i = 0; i + m <= n; ++i) {
        int j = 0; while (j < m && t[i + j] == p[j]) ++j;
        if (j == m) r.push_back(i);
    }
    return r;
}
```

That is `O(n m)` in the worst case. The worst case takes one line to build. Take a text of all `a` and a pattern of `a`'s ending in a `b`: at every start the comparison runs almost the whole length of the pattern, and fails on the last character. For a text of 30 letters `a` and the pattern `aaaaaaab`, counting character comparisons (KMP's count is from an instrumented copy of the loop in section 3):

```
T1 text = 30 times 'a', pattern = 7 times 'a' then 'b': naive made 184 character comparisons, KMP made 53 (n = 30, so at most 2n = 60)
```

The naive search compares the same text characters again and again: after a failure at the end of the pattern it throws away everything it learned and restarts one position to the right. KMP's idea is that **what you have already matched tells you where the next match can possibly start**, so the text pointer never needs to go back.

## 2. Borders and the prefix function

A **border** of a string is a non-empty proper prefix that is also a suffix. `aba` is a border of `abacaba` (it starts it and ends it); so is `a`. A string with no border (`abc`) has none.

The **prefix function** of a string `s` is the array `pi` where `pi[i]` is the length of the longest border of the prefix `s[0..i]`. For `s = abacabab`:

```
T2 s = abacabab, pi = 0 0 1 0 1 2 3 2   (prefixes: a:0 ab:0 aba:1 abac:0 abaca:1 abacab:2 abacaba:3 abacabab:2)
```

Read one entry: `abacaba` (the prefix ending at index 6) has the border `aba`, so `pi[6] = 3`.

**Computing it in `O(n)`.** Suppose you know `pi[i-1] = k`: the prefix `s[0..i-1]` has the border `s[0..k-1]`. Now look at `s[i]`:

- If `s[i] == s[k]`, the border extends by one character, so `pi[i] = k + 1`.
- If not, you need a *shorter* border of the same prefix, and the next shorter border of `s[0..i-1]` is itself the border of `s[0..k-1]`, which is `pi[k-1]`. So set `k = pi[k-1]` and try again. If `k` reaches 0 and `s[i] != s[0]`, then `pi[i] = 0`.

```cpp
// Prefix function: pi[i] = the length of the longest PROPER prefix of s[0..i] that is also a suffix of s[0..i] (a "border").
// Each step starts from the border of the previous prefix, k = pi[i-1].  If s[i] extends that border (s[i] == s[k]) the new border is
// k + 1.  If not, fall back to the next shorter border of the same prefix, which is pi[k-1], and try again.
vector<int> prefix_function(const string& s) {
    int n = (int)s.size(); vector<int> pi(n, 0);
    for (int i = 1; i < n; ++i) {
        int k = pi[i - 1];
        while (k > 0 && s[i] != s[k]) k = pi[k - 1];
        if (s[i] == s[k]) ++k;
        pi[i] = k;
    }
    return pi;
}
```

A trace of one position with several fallbacks, the string `aabaaac` at its last character `c`:

```
T3 s = aabaaac, pi = 0 1 0 1 2 2 0; computing pi[6] (char 'c'): start k = pi[5] = 2, s[2]='b' != 'c' so k = pi[1] = 1, s[1]='a' != 'c' so k = pi[0] = 0, now k = 0 and s[0]='a' != 'c' too; pi[6] = 0
```

**Why it is linear.** The variable `k` starts at 0, rises by at most 1 per character (the `++k`), and each trip through the `while` body lowers it by at least 1. It never goes below 0. So the total number of fallbacks over the whole run is at most the total number of increases, which is at most `n - 1`. The `while` loop is *amortised* constant per character: total work `O(n)`. (This is the same argument as for the monotonic stack in [Stacks and queues 02](../05_stacks_and_queues/02_monotonic_stack.md): each unit of "potential" is spent at most once.)

## 3. KMP search

To search, run exactly the same loop over the text, with the pattern's prefix function. Let `k` be the number of pattern characters currently matched, ending at the current text position. When the next text character matches `p[k]`, extend. When it does not, fall back to the longest border of what you have matched, `pi[k-1]`, and try again. After a **full** match (`k == m`) fall back the same way, so the next overlapping match is found.

```cpp
// KMP search: the same loop, run over the text with the pattern's prefix function.  k = how many characters of the pattern match the
// text ending at the current position.  On a mismatch (or after a full match) fall back along the borders, never moving back in the
// text.  Returns every start index, overlapping matches included.  Precondition: the pattern is not empty.
vector<int> kmp_find_all(const string& t, const string& p) {
    vector<int> pi = prefix_function(p), r; int m = (int)p.size(), k = 0;
    for (int i = 0; i < (int)t.size(); ++i) {
        while (k > 0 && (k == m || t[i] != p[k])) k = pi[k - 1];
        if (t[i] == p[k]) ++k;
        if (k == m) r.push_back(i - m + 1);
    }
    return r;
}
```

Finding `abab` in `abababcabababab` (overlaps counted), checked against the naive search, the Z search of the next section and the automaton of section 6:

```
T4 occurrences of abab in abababcabababab (overlaps counted): KMP 0 2 7 9 11; naive same; Z same; automaton same
```

The matches at 0 and 2 overlap (`ababab`), as do 7, 9 and 11.

**Cost.** The text index `i` only goes forward; `k` follows the same amortised rule as before, so the search is `O(n + m)` with `O(m)` extra space. In the tests an instrumented copy of the loop made at most `2n` character comparisons on every input; one comparison per text character succeeds or ends the character's turn, and the rest are fallbacks, which are paid for by earlier increases.

**Common bugs.** (1) Reading `p[k]` when `k == m` (past the end) after a full match. (2) Falling back with `k = k - 1` instead of `k = pi[k-1]` (that is the naive search again, and wrong). (3) Using `i - m` instead of `i - m + 1` for the start. (4) An empty pattern: the code above requires `m >= 1`.

## 4. The Z-function

The **Z-function** of `s` is the array `z` where `z[i]` is the length of the longest common prefix of `s` and the suffix starting at `i`. By convention `z[0] = n`. For `s = aabxaab`:

```
T5 s = aabxaab, z = 7 1 0 0 3 1 0   (z[0] is n by convention; z[4] = 3 because aab matches the start)
```

`z[4] = 3` because the suffix `aab` starting at index 4 matches the first three characters of `s`.

**Computing it in `O(n)`.** Keep the **window** `[l, r)`: the rightmost stretch `s[l..r)` found so far that equals a prefix of `s`. For a new position `i` inside the window, the text there is a copy of the text at `i - l`, so `z[i]` starts at `min(r - i, z[i - l])` for free: a copy cannot extend beyond the window's edge. Only if it reaches the edge do you compare characters directly, and every successful comparison pushes `r` to the right.

```cpp
// Z-function: z[i] = the length of the longest common prefix of s and the suffix s[i..].  (z[0] is set to n by convention.)
// [l, r) is the rightmost window s[l..r) found so far that matches a prefix of s.  Inside it, position i is a copy of position i - l,
// so z[i] starts at min(r - i, z[i - l]) and only grows by direct comparison when it reaches the window's edge.
vector<int> z_function(const string& s) {
    int n = (int)s.size(); vector<int> z(n, 0); if (n == 0) return z;
    z[0] = n;
    for (int i = 1, l = 0, r = 0; i < n; ++i) {
        if (i < r) z[i] = std::min(r - i, z[i - l]);
        while (i + z[i] < n && s[z[i]] == s[i + z[i]]) ++z[i];
        if (i + z[i] > r) { l = i; r = i + z[i]; }
    }
    return z;
}
// Search with the Z-function: build p + separator + t; a position whose z value reaches |p| is a match.  The separator must not occur
// in either string; '\0' is fine for ordinary text.  Precondition: the pattern is not empty.
vector<int> z_find_all(const string& t, const string& p) {
    string s = p + '\0' + t; vector<int> z = z_function(s), r; int m = (int)p.size();
    for (int i = m + 1; i < (int)s.size(); ++i) if (z[i] >= m) r.push_back(i - m - 1);
    return r;
}
```

**Why it is linear.** `r` only moves right, and each character comparison that succeeds moves it right by one; the comparison that fails happens once per position. So the total is at most about `2n` comparisons.

**Searching with `z`.** Join the pattern, a separator that occurs in neither string, and the text: `p + sep + t`. A position in the text part whose `z` value is at least `m` is a match (the separator makes `z` stop at `m`). For the pattern `aabxaab` in `xaabxaabxaab`:

```
T6 Z-search of aabxaab in xaabxaabxaab: z values over the text part are [0 7 1 0 0 7 1 0 0 3 1 0], matches at 1 5
```

The two `7`'s in the text part are the matches at positions 1 and 5 of the text (they overlap in the shared `aab`).

**Prefix function or Z-function?** They carry the same information; each can be computed from the other in `O(n)` (an editor exercise). Use whichever reads better for the question:

- **Prefix function**: "the longest border of every prefix", periods, borders, the automaton, streaming search where text arrives one character at a time.
- **Z-function**: "how much does each suffix agree with the whole string", problems phrased as common prefixes (for example the sum of the scores of all prefixes' suffixes), and simple offline searches.

## 5. What the prefix function tells you

Most interview problems on this topic are not "find the pattern", they are "use the borders".

**The smallest period.** A string has period `p` if `s[i] == s[i + p]` wherever both sides exist. A period `p` exists exactly when `n - p` is a border length, so the **smallest period is `n - pi[n-1]`**, the longest border leaves the shortest period. If that period divides `n` and is less than `n`, the string is a block repeated several times (LeetCode 459). The examples:

```
T7 smallest period / repeated block: [abcabcabc: period 3, repetition] [abcabcab: period 3, not a repetition] [aaaa: period 1, repetition] [abcd: period 4, not a repetition]
```

(`abcabcab` has period 3 but the length 8 is not a multiple of 3, so it is not a whole number of repeats.)

**All borders.** The longest border is `pi[n-1]`; the next longest is the longest border of *that*, `pi[pi[n-1] - 1]`; and so on until 0. LeetCode 1392 (longest happy prefix) is the first of them.

**The shortest palindrome by adding characters in front (LeetCode 214).** Adding characters to the front means keeping some prefix of `s` and mirroring the rest, so you want the **longest palindromic prefix** of `s`. A prefix of `s` is a palindrome exactly when it equals the matching suffix of `reverse(s)`. So build `s + sep + reverse(s)`; the last value of its prefix function is the length of the longest prefix of `s` that is also a suffix of `reverse(s)`: the longest palindromic prefix.

**Counting how often each prefix occurs.** Every position `i` with border length `pi[i]` is an end of an occurrence of the prefix of that length, and (through the chain of borders) of each shorter border as well. Counting `pi` values and then adding each length's count into its border's count, from the longest down, gives all the occurrence counts in `O(n)`.

**Rotation (LeetCode 796).** `b` is a rotation of `a` exactly when the lengths agree and `b` occurs in `a + a`.

```cpp
// Smallest period: the least p with s[i] == s[i + p] for all valid i.  It is n - pi[n-1]: the longest border leaves exactly that much.
int smallest_period(const string& s) { return s.empty() ? 0 : (int)s.size() - prefix_function(s).back(); }
// LeetCode 459: is s a block repeated two or more times?  Only if the period divides n (and the period is less than n).
bool is_repetition(const string& s) {
    int n = (int)s.size(); if (n == 0) return false;
    int p = smallest_period(s); return p < n && n % p == 0;
}
// LeetCode 1392: the longest proper prefix that is also a suffix.
int longest_happy_prefix(const string& s) { return s.empty() ? 0 : prefix_function(s).back(); }
// Every border length, longest first: pi[n-1], then pi[pi[n-1] - 1], and so on down to 0.
vector<int> borders(const string& s) {
    vector<int> r; if (s.empty()) return r;
    vector<int> pi = prefix_function(s);
    for (int k = pi.back(); k > 0; k = pi[k - 1]) r.push_back(k);
    return r;
}
// LeetCode 214: the shortest palindrome made by adding characters in FRONT of s.  The longest palindromic prefix of s is the longest
// prefix of s that is also a suffix of reverse(s); find it as the last value of pi on s + separator + reverse(s).
string shortest_palindrome(const string& s) {
    string rev(s.rbegin(), s.rend()), t = s + '\0' + rev;
    int k = prefix_function(t).back();                    // length of the longest palindromic prefix of s
    return rev.substr(0, s.size() - k) + s;
}
// LeetCode 796: b is a rotation of a iff the lengths agree and b occurs in a + a.
bool is_rotation(const string& a, const string& b) {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return !kmp_find_all(a + a, b).empty();
}
// How many times does each prefix of s occur in s (overlaps counted)?  occ[len] for len = 1..n.  Every position i with border length
// pi[i] contributes one occurrence of the prefix of that length, and (through the border chain) one of each shorter border;
// add the counts down from the longest prefix, then add the occurrence at the prefix's own start.
vector<long long> prefix_occurrences(const string& s) {
    int n = (int)s.size(); vector<int> pi = prefix_function(s);
    vector<long long> occ(n + 1, 0);
    for (int i = 0; i < n; ++i) ++occ[pi[i]];
    for (int len = n - 1; len > 0; --len) occ[pi[len - 1]] += occ[len];
    for (int len = 1; len <= n; ++len) ++occ[len];
    return occ;
}
```

The borders, shortest palindromes, prefix counts and rotations from the program:

```
T8 all borders of abacabaabacaba, longest first: 7 3 1  (aba, a, and the longer abacaba)
T9 shortest palindrome by adding characters in front: aacecaaa -> aaacecaaa; abcd -> dcbabcd; abb -> bbabb
T10 occurrences of each prefix of ababab (lengths 1..6): 3 3 2 2 1 1
T11 rotations: waterbottle / erbottlewat -> yes; abcd / acbd -> no; ab / abc -> no
```

(The borders of `abacabaabacaba` are `abacaba` (7), `aba` (3) and `a` (1).)

## 6. The KMP automaton

The search loop above sometimes falls back several times for one text character. You can precompute the *result* of all those fallbacks: for each state `k` (characters matched so far) and each letter `c`, the state you would end in. This table is the **KMP automaton**. Row `k` is a copy of the row of the longest border, `pi[k-1]`, with one change: the single letter `p[k]` that advances the match goes to state `k + 1`.

```cpp
// The KMP automaton for a lowercase pattern: state k = "k characters of the pattern matched".  next[k][c] is the state after reading c.
// Row k copies the row of the longest border (pi[k-1]) and overrides the single character that extends the match.  Build O(26 m),
// then every character of the text costs exactly one table lookup (no loop, no fallback), which suits streams and hardware.
struct KmpAutomaton {
    int m; vector<std::array<int, 26>> next;
    explicit KmpAutomaton(const string& p) : m((int)p.size()), next(p.size() + 1) {
        vector<int> pi = prefix_function(p);
        next[0].fill(0); next[0][p[0] - 'a'] = 1;
        for (int k = 1; k <= m; ++k) {
            next[k] = next[pi[k - 1]];                    // the fallback behaviour
            if (k < m) next[k][p[k] - 'a'] = k + 1;       // the one character that advances
        }
    }
    vector<int> find_all(const string& t) const {
        vector<int> r; int k = 0;
        for (int i = 0; i < (int)t.size(); ++i) { k = next[k][t[i] - 'a']; if (k == m) r.push_back(i - m + 1); }
        return r;
    }
};
```

For the pattern `abab` (columns: next state on `a`, next state on `b`):

```
T12 KMP automaton for abab (state: next on 'a', next on 'b'): [0: 1,0] [1: 1,2] [2: 3,0] [3: 1,4] [4: 3,0]
```

For example, in state 3 (`aba` matched) reading `a` gives state 1, not 0: the text ends in `a`, the start of a new match. Reading `b` completes the match (state 4). Reading anything in state 4 falls back through the borders at once.

Each text character now costs exactly one table lookup, with no loop, which suits streams (feed characters as they arrive and report a match immediately) and hardware. The price is `O(m * sigma)` memory and set-up for an alphabet of size `sigma`. The same table is the starting point of the automaton chapter (12.05) and of "count strings that avoid a pattern" dynamic programs.

## 7. Measured: is KMP the fastest in practice?

Two inputs, one worst case for the naive search and one ordinary. Machine and method as above.

**Worst case**: a text of 1,000,000 letters `a` and a pattern of 999 `a`'s followed by `b`, so no match exists and every start fails at the last character.

| Method | Time, ms (run 1 / run 2) |
|---|---|
| my naive search | 306.4 / 312.1 |
| `std::string::find` | 17.9 / 17.1 |
| `strstr` | 0.0 / 0.0 |
| KMP | 2.5 / 2.6 |
| Z-function | 2.5 / 2.6 |
| KMP automaton (including building it) | 5.0 / 5.3 |

**Ordinary case**: a random text of 10,000,000 letters over four symbols and a random 40-letter pattern taken from the text with its last letter changed (so there is no match).

| Method | Time, ms (run 1 / run 2) |
|---|---|
| my naive search | 41.5 / 44.6 |
| `std::string::find` | 15.1 / 16.3 |
| `strstr` | 2.5 / 2.7 |
| KMP | 43.6 / 47.0 |
| Z-function | 77.1 / 78.0 |
| KMP automaton | 37.9 / 37.4 |

Building the prefix function alone over the 10,000,000-letter string took 76.9 / 76.8 ms and the Z-function 68.6 / 72.1 ms.

What to take from it:

- On the worst case KMP and Z are about a hundred times faster than my naive loop (306.4 / 312.1 ms against 2.5 / 2.6 ms) and still several times faster than `std::string::find`. The library's call is not linear in theory, but it is fast here. I did not read its source or the generated code; a likely reason is that it compares the pattern with an optimised block comparison, which does 999 characters per start very cheaply, but I did not verify that.
- `strstr` took about 0 ms on the worst case. That input has no letter `b` anywhere in the text, which a library routine may be able to notice at once; I did not profile it, so read the number as "this input is cheap for `strstr`", not as a general result. Do not conclude that `strstr` is immune to bad inputs.
- On ordinary text **KMP is not faster than the naive search** (a random text almost always mismatches at the first character, so the naive loop does about one comparison per position), and the library `find` and `strstr` beat both. KMP's value is the **guarantee** (no input can be slow), the ability to process a **stream**, and the toolbox of borders and periods, not typical-case speed.
- The Z search is slower than KMP search on the ordinary case; probably because it builds `p + sep + t` and an integer array of 4 bytes per character, though I did not profile it.

## 8. Complexities

| Operation | Time | Extra space |
|---|---|---|
| Naive search | `O(n m)` worst, about `O(n)` typical | `O(1)` |
| Prefix function of `s` | `O(n)` | `O(n)` |
| KMP search | `O(n + m)` | `O(m)` |
| Z-function of `s` | `O(n)` | `O(n)` |
| Z search | `O(n + m)` | `O(n + m)` |
| KMP automaton | `O(m sigma)` build, `O(1)` per character | `O(m sigma)` |
| Smallest period, borders, shortest palindrome, prefix counts | `O(n)` | `O(n)` |

## 9. Which tool for which problem

- "Find a pattern in text, once": the library's `find` is the practical answer; KMP or Z when the interviewer wants a guaranteed linear bound, or you are working on a stream or on non-character data.
- "Many different patterns at once": a trie with failure links, 12.04, multi-pattern matching. (The pattern-by-pattern version of this idea is also in [Trees 18](../07_trees/18_binary_tries_radix_trees_and_lpm.md).)
- "Compare many substrings fast": hashing, 12.02, rolling hash.
- "Repeated block", "period", "string equal to a rotation of itself": the prefix function.
- "Prepend characters to make a palindrome": prefix function on `s + sep + reverse(s)`.
- "How much of the string matches at every shift": the Z-function.
- "Count strings avoiding a pattern": the KMP automaton as the state of a dynamic program.

## 10. Common mistakes

1. **Falling back by one** (`k = k - 1`) instead of through `pi`. It is the naive search and quadratic.
2. **Reading `p[k]` with `k == m`**, or forgetting to fall back after a full match, so overlapping matches are missed.
3. **Off-by-one on the start index**: `i - m + 1`, not `i - m`.
4. **Separator that appears in the data** when searching on `p + sep + t` (or in the palindrome trick). `'\0'` is safe for ordinary text; for binary data search directly instead.
5. **An empty pattern**, which these functions do not accept; decide what the problem means (usually "match at every position") and handle it first.
6. **Mixing up the prefix function and the Z-function conventions**: `pi[i]` is about the prefix ending at `i`; `z[i]` is about the suffix starting at `i`; `z[0]` is `n` here (some texts use 0).
7. **Declaring the period `n - pi[n-1]` a repetition** without checking that it divides `n`.
8. **Believing KMP is the fastest search.** See section 7.
9. **Using `int` positions for texts over two billion characters.**
10. **Recomputing the prefix function for every query** when the pattern is fixed and only the text changes.

## 11. Questions to answer out loud

1. Why is the naive search `O(n m)`? Construct an input.
2. Define a border and the prefix function. Compute it for `abacabab` by hand.
3. Explain the fallback `k = pi[k-1]`: why is that the next shorter border?
4. Why is the prefix function computed in `O(n)` even though it has a `while` loop inside a `for`?
5. How does KMP search differ from computing the prefix function?
6. Define the Z-function. How does the window `[l, r)` let you reuse earlier work?
7. How do you search for a pattern with the Z-function? Why is a separator needed?
8. Find the smallest period of a string with the prefix function. When is the string a repeated block?
9. How do you build the shortest palindrome by adding characters to the front?
10. What is the KMP automaton? When would you use it instead of the loop?
11. When is KMP not the best practical choice?

## 12. Problems

### 12.1 On paper

Answer first, then run the code to check.

**A.** Compute the prefix function of `abcabcabd`.

**B.** Compute the Z-function of `aaaaa`.

**C.** What are the smallest period of `abcabcabcabc`, and is it a repetition? Is `abcabcabcab` one?

**D.** List all the borders of `abaababaab`.

**E.** How many times does each prefix (lengths 1 to 8) of `aabaabaa` occur in it?

**F.** Where does `aa` occur in `aaaaa`?

**G.** Write the KMP automaton (next states on `a` and `b`) for the pattern `aab`.

**H.** What is the shortest palindrome you can make from `aba` by adding characters to the front?

**I.** Is `cdeab` a rotation of `abcde`? Is `abced`?

**J.** What is the longest happy prefix (longest proper prefix that is also a suffix) of `ababab`?

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Find the Index of the First Occurrence in a String | 28 | LeetCode | KMP search, return the first match |
| Repeated Substring Pattern | 459 | LeetCode | `n - pi[n-1]` divides `n` and is less than `n` |
| Shortest Palindrome | 214 | LeetCode | Prefix function on `s + sep + reverse(s)` |
| Longest Happy Prefix | 1392 | LeetCode | `pi[n-1]` |
| Rotate String | 796 | LeetCode | Same length and `b` occurs in `a + a` |
| Repeated String Match | 686 | LeetCode | Repeat `a` until it is at least as long as `b`, plus one more copy; search for `b` |
| Maximum Repeating Substring | 1668 | LeetCode | Search for the word repeated `k` times, increasing `k` (or use `pi` on the word) |
| Sum of Scores of Built Strings | 2223 | LeetCode | Sum of the Z-function values |
| Find All Good Strings | 1397 | LeetCode (hard) | The KMP automaton as the state of a digit-style DP over the string, once for each of the two forbidden patterns |

### 12.3 In your editor

**A.** Write `prefix_from_z` and `z_from_prefix` (each in `O(n)`) and check them against the direct functions on random strings.

**B.** Write a streaming matcher: a class with `feed(char) -> bool` that reports a match as each character arrives, using the prefix function, and test it against the batch search.

**C.** Make KMP generic: search for a pattern in a vector of integers (or in a `std::deque` of tokens), with the comparison as a template argument.

**D.** Build an input that makes `std::string::find` slow on your machine (try longer patterns than 1,000) and compare it with KMP.

**E.** Write the Z-function search without concatenating: compute `z` on the pattern, then run the same window logic over the text directly, and compare memory and time with `z_find_all`.

### Answers

The program that checks the paper problems printed:

```
A 0 0 0 1 2 3 4 5 0
B 5 4 3 2 1
C 3 1 0
D 5 2
E 6 3 2 2 2 1 1 1
F 0 1 2 3
G [0: a->1 b->0] [1: a->2 b->0] [2: a->2 b->3] [3: a->1 b->0]
H aba
I 1 0
J 4
```

Reading it line by line:

**A.** `0 0 0 1 2 3 4 5 0`: the prefix `abcabcab` has border `abcab` (length 5), but the final `d` breaks every border.

**B.** `5 4 3 2 1`.

**C.** The period is 3, and `12` is a multiple of 3, so `abcabcabcabc` is a repetition (printed `3 1`); `abcabcabcab` has length 11, also with period 3, but 11 is not a multiple of 3, so it is not (printed `0`).

**D.** `5 2`: `abaab` is both the prefix and the suffix, and inside it `ab`.

**E.** `6 3 2 2 2 1 1 1`: `a` occurs 6 times, `aa` 3 times, and so on; each prefix of length 6 or more occurs once.

**F.** At `0 1 2 3`.

**G.** State 0: `a` goes to 1, `b` to 0. State 1: `a` to 2, `b` to 0. State 2: `a` stays at 2 (the text `aaa` still ends in `aa`), `b` goes to 3 (a match). State 3: `a` to 1, `b` to 0.

**H.** `aba`: it is already a palindrome, so nothing is added.

**I.** Yes (`cdeab` is `abcde` rotated by 2), then no.

**J.** `4` (`abab`).

## Cheat sheet

- **Border:** a non-empty proper prefix that is also a suffix. **`pi[i]`:** longest border of `s[0..i]`.
- **Prefix function:** `k = pi[i-1]`; `while (k && s[i] != s[k]) k = pi[k-1]; if (s[i] == s[k]) ++k;`. Linear because `k` rises by at most 1 per step.
- **KMP search:** the same loop over the text; on `k == m` record a match and fall back through `pi`. `O(n + m)`, at most `2n` comparisons.
- **Z-function:** `z[i]` = longest common prefix of `s` and `s[i..]`; window `[l, r)` and `min(r - i, z[i - l])`. Search on `p + sep + t`, match when `z >= m`.
- **Period:** `n - pi[n-1]`; a repetition if it divides `n` and is less than `n`. **Borders:** `pi[n-1]`, then `pi[k-1]` repeatedly.
- **Shortest palindrome by prepending:** last `pi` of `s + sep + reverse(s)`. **Rotation:** `b` in `a + a`.
- **Automaton:** `next[k] = next[pi[k-1]]` with `p[k]` advancing to `k + 1`; one lookup per character.
- **In practice:** the library `find` is fast; KMP gives the worst-case guarantee, streaming and the toolbox.

---

## Sources

- All code is tested here (1,182,809 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- The Knuth-Morris-Pratt algorithm (1977), the prefix function, the Z-algorithm and the automaton view are standard; see CLRS (the chapter on string matching) and Gusfield, *Algorithms on Strings, Trees, and Sequences* (the Z-algorithm and its uses). The cp-algorithms site has good write-ups of the prefix function and the Z-function; the applications in section 5 (period, shortest palindrome, prefix counts) follow the usual treatments, from memory. The explanations and code are my own.
- I did not read the source of `std::string::find` or `strstr`; section 7 reports only what I measured.
- LeetCode numbers and titles are from memory; check a number before you rely on it.
