# DSA 12.06: Suffix Arrays and Suffix Automata

*Part of the [String algorithms plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · Stretch topic, closes area 12 · You need: [KMP and the Z-function](01_kmp_and_the_z_function.md), [Rolling hash and Rabin-Karp](02_rolling_hash_and_rabin_karp.md) (the other way to answer "is this substring repeated?") and [Automata and regular expressions](05_automata_and_regular_expressions.md) (a suffix automaton is a DFA, built online)*

Every tool so far fixed the **pattern** and scanned the **text**. This tutorial flips it: preprocess the **text** once, so that any number of later questions about its substrings are fast. The idea behind both structures is the same sentence: *every substring is a prefix of some suffix*. So if you can organise the suffixes of a string, you can answer questions about all its substrings.

- A **suffix array** sorts all the suffixes. It is an array of `n` integers, and with the array of common prefixes between neighbours (the **LCP array**) it answers: where does this pattern occur, how many different substrings are there, what is the longest substring that repeats, what do two strings have in common.
- A **suffix automaton** is the smallest DFA that accepts exactly the suffixes of the text. It has fewer than `2n` states, is built letter by letter in `O(n)`, and answers the same questions plus counting occurrences, with a fast walk.

By the end you can:

- build a suffix array by prefix doubling and explain why it needs at most `log n` rounds;
- compute the LCP array with Kasai's algorithm in `O(n)` and say why it is linear;
- search a pattern with two binary searches, count the distinct substrings, and find the longest repeated and longest common substring;
- explain what a state of the suffix automaton stands for, what `len` and `link` mean, and why construction sometimes **clones** a state;
- state the size bounds (at most `2n - 1` states and `3n - 4` edges) and show where they are tight;
- choose between a suffix array, a suffix automaton, a rolling hash and a plain scan for a given problem.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 3,765,694 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran on 30,000 random strings (1 to 3 letters, length up to 13) and compared: the suffix array with a sort of all suffixes; every LCP value with a direct count; the number of distinct substrings (both structures) with the size of a `std::set` of all substrings; the occurrence count of every substring and of random patterns (both structures) with a scan; the longest repeated substring; the longest common substring (both methods) with a dynamic program; and the automaton's size bounds. Seven deliberate bugs were tried. Six were caught by the tests (stopping the doubling after one round, a wrong distinct-substring formula, a wrong upper bound in the search, never cloning a state, counting clones as occurrences, and not requiring the two neighbours to lie on different sides of the separator). The seventh, resetting the Kasai counter to zero instead of decrementing it, still produces correct answers and is **not** caught by any correctness test; it is a speed bug, and section 3 shows the time (0.2 ms against 2,192 ms on 100,000 letters `a`).

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions (one for the slowest rows), shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs. Statements about other tools and algorithms I did not implement are from memory and marked as such.

---

## 1. Substrings are prefixes of suffixes

Take `banana`. Its suffixes are `banana`, `anana`, `nana`, `ana`, `na`, `a`. The substring `ana` is a prefix of `anana` and of `ana`; the substring `nan` is a prefix of `nana`. Every substring starts somewhere, so it is a prefix of the suffix that starts there. The reverse also holds: every prefix of a suffix is a substring. That is why a structure on the suffixes is a structure on all `n(n+1)/2` substrings, using only `n` items.

The questions this section's structures answer well:

| Question | Without preprocessing | With a suffix array or automaton |
|---|---|---|
| Does `p` occur in the text? | `O(n)` per query | `O(m log n)` or `O(m)` |
| How many times? Where? | `O(n)` per query | `O(m log n)` (+ output), or `O(m)` for the count |
| How many **different** substrings? | hash all of them, `O(n^2)` | `O(n)` after building |
| Longest substring that occurs **twice**? | binary search + hashing, `O(n log n)` | one pass over the LCP array |
| Longest substring common to **two** strings? | `O(n m)` dynamic program | `O(n + m)` to `O((n + m) log)` |

## 2. The suffix array

The **suffix array** `sa` of a string `s` lists the starting positions of the suffixes in sorted (dictionary) order. For `banana`, with the LCP value alongside (explained in section 3):

```
T1 banana, rows (rank: suffix start, lcp with the row above, suffix): [0: 5, 0, a] [1: 3, 1, ana] [2: 1, 3, anana] [3: 0, 0, banana] [4: 4, 0, na] [5: 2, 2, nana]
```

Row 0 is the suffix `a` (it starts at position 5), row 1 is `ana` (position 3), and so on, ending with `nana`. Nothing is stored except the positions: `sa = 5 3 1 0 4 2`.

**Naive construction.** Sort the numbers `0 .. n-1` with a comparator that compares the two suffixes letter by letter. Each comparison costs up to `O(n)`, so the sort costs `O(n^2 log n)` in the worst case. On random text, comparisons stop after a few letters, so it is fast; on `aaaa...a` every comparison runs to the end. The measurement is in section 7.

**Prefix doubling.** The idea of Manber and Myers (from memory) avoids long comparisons by sorting on a growing prefix:

1. Round 1: rank every suffix by its first 2 letters (a pair: the first letter and the second).
2. After a round that sorted by the first `2k` letters, every suffix has a rank (equal prefixes of length `2k` get equal ranks). To sort by the first `4k` letters, compare the pair `(rank[i], rank[i + 2k])`: the rank of the first half and the rank of the second half. A suffix too short for a second half gets rank `-1`, so it sorts first.
3. Stop when all ranks are different.

Each round doubles the number of letters that decide the order, so there are at most `ceil(log2 n)` rounds, and each round is a sort of `n` pairs: `O(n log n)` with `std::sort`, `O(n log^2 n)` in total.

```cpp
// Suffix array by prefix doubling.  After the round with step k, rk[i] is the rank of the suffix i among all suffixes
// compared on their first 2k letters only; ties get equal ranks.  When all ranks differ, the order is final.
inline vector<int> suffix_array(const string& s, int* rounds = nullptr) {
    int n = (int)s.size(), r = 0;
    vector<int> sa(n), rk(n), tmp(n);
    for (int i = 0; i < n; ++i) { sa[i] = i; rk[i] = (unsigned char)s[i]; }
    for (int k = 1; n > 0; k <<= 1) {
        auto key = [&](int i) { return pair<int, int>(rk[i], i + k < n ? rk[i + k] : -1); };
        std::sort(sa.begin(), sa.end(), [&](int a, int b) { return key(a) < key(b); });
        tmp[sa[0]] = 0;
        for (int i = 1; i < n; ++i) tmp[sa[i]] = tmp[sa[i - 1]] + (key(sa[i - 1]) < key(sa[i]) ? 1 : 0);
        rk = tmp; ++r;
        if (rk[sa[n - 1]] == n - 1) break;
    }
    if (rounds) *rounds = r;
    return sa;
}
// Kasai: lcp[i] = length of the common prefix of the suffixes sa[i-1] and sa[i] (lcp[0] = 0), in O(n).
// The trick: if suffix i shares h letters with its predecessor in the order, suffix i+1 shares at least h-1.
inline vector<int> lcp_array(const string& s, const vector<int>& sa) {
    int n = (int)s.size();
    vector<int> rk(n), lcp(n, 0);
    for (int i = 0; i < n; ++i) rk[sa[i]] = i;
    for (int i = 0, h = 0; i < n; ++i) {
        if (rk[i] == 0) { h = 0; continue; }
        int j = sa[rk[i] - 1];
        while (i + h < n && j + h < n && s[i + h] == s[j + h]) ++h;
        lcp[rk[i]] = h;
        if (h > 0) --h;
    }
    return lcp;
}
```

(The same block also contains the LCP array of section 3.) The number of rounds depends on the text. The program counts them:

```
T5 doubling rounds: banana 2; a^1024 10; random 100000 letters 3; a^100000 17
```

`banana` needs 2 rounds; a random text needs only 3, because after a few letters the suffixes already differ; `aaaa...a` needs the full `log2 n` (10 rounds for 1024 letters, 17 for 100,000), since all the suffixes agree on a long prefix. Better algorithms exist, and I did not implement them: replacing `std::sort` by a radix sort gives `O(n log n)`; the SA-IS algorithm (induced sorting) and the DC3 / skew algorithm give `O(n)` (from memory). In practice libraries and contests use one of these for large inputs.

## 3. The LCP array and Kasai's algorithm

The **LCP array** stores, for each row `i >= 1`, the length of the longest common prefix of the suffixes in rows `i - 1` and `i`. In the table of section 2 the rows `ana` and `anana` share `ana`, so their LCP is 3.

The LCP of two **non-adjacent** rows is the **minimum** of the LCP values between them (a standard fact: anything shorter than the minimum is shared all the way, and the minimum row breaks it). That is why the adjacent values are enough for every query.

**Kasai's algorithm** computes the whole array in `O(n)`, in text order instead of sorted order. Let `h` be the LCP of the suffix `i` with its predecessor in the array. Then the suffix `i + 1` shares **at least** `h - 1` letters with *its* predecessor (drop the first letter of both suffixes: the shorter pair is still in sorted order, and still adjacent or separated only by suffixes that share the same prefix). So when moving from `i` to `i + 1`, `h` only drops by one; it rises only by comparing new letters. Over the whole loop `h` can fall at most `n` times in total and can never exceed `n`, so it can rise at most `2n` times: `O(n)` comparisons.

The decrement `if (h > 0) --h` is therefore the line that makes it linear. Replace it by `h = 0` and the algorithm is still correct, because it recomputes from scratch, but it takes `O(n^2)` in the worst case. This is the seventh deliberate bug of the test run: no correctness test fails. On 100,000 letters `a` the correct version takes **0.2 ms** and the broken one **2,192 ms** (measured once, in a separate run). A performance property needs a performance test.

## 4. What you can do with the array and the LCP

All of these are a few lines on top of `(sa, lcp)`:

```cpp
// Everything below is a few lines on top of (sa, lcp).
struct SuffixIndex {
    string s; vector<int> sa, lcp;
    explicit SuffixIndex(string t) : s(std::move(t)), sa(suffix_array(s)), lcp(lcp_array(s, sa)) {}
    // The suffixes that start with p form one block [lo, hi) of the array: two binary searches, O(|p| log n).
    pair<int, int> range(const string& p) const {
        int n = (int)s.size(), m = (int)p.size();
        int lo = 0, hi = n;
        while (lo < hi) { int mid = (lo + hi) / 2; if (s.compare(sa[mid], m, p) < 0) lo = mid + 1; else hi = mid; }
        int from = lo; hi = n;
        while (lo < hi) { int mid = (lo + hi) / 2; if (s.compare(sa[mid], m, p) <= 0) lo = mid + 1; else hi = mid; }
        return {from, lo};
    }
    int count(const string& p) const { auto r = range(p); return r.second - r.first; }
    long long distinct_substrings() const {            // all substrings, minus the ones counted twice
        long long n = (long long)s.size(), total = n * (n + 1) / 2;
        for (int x : lcp) total -= x;
        return total;
    }
    string longest_repeated() const {                  // the largest lcp value is the longest substring that occurs twice
        int best = 0, at = 0;
        for (size_t i = 1; i < lcp.size(); ++i) if (lcp[i] > best) { best = lcp[i]; at = sa[i]; }
        return s.substr(at, best);
    }
};
// Longest common substring of a and b: sort the suffixes of a + '\1' + b; the answer is the largest lcp
// between two NEIGHBOURS that start on different sides of the separator.
inline string longest_common_substring(const string& a, const string& b) {
    string t = a + '\1' + b;
    SuffixIndex ix(t);
    int na = (int)a.size(), best = 0, at = 0;
    for (size_t i = 1; i < t.size(); ++i) {
        bool x = ix.sa[i - 1] < na, y = ix.sa[i] < na;
        if (ix.sa[i - 1] != na && ix.sa[i] != na && x != y && ix.lcp[i] > best) { best = ix.lcp[i]; at = ix.sa[i]; }
    }
    return t.substr(at, best);
}
```

**Search.** The suffixes that start with `p` are exactly one block of consecutive rows, because the array is sorted. Two binary searches find its ends: the first row whose first `|p|` letters are `>= p`, and the first whose first `|p|` letters are `> p`. Each comparison costs `O(|p|)`, so the search costs `O(|p| log n)`, and the block's size is the number of occurrences. For `banana`:

```
T2 banana, the block of suffixes that start with: ana -> rows [1,3) = 2 at 3 1; an -> rows [1,3) = 2 at 3 1; a -> rows [0,3) = 3 at 5 3 1; nan -> rows [5,6) = 1 at 2; x -> rows [6,6) = 0;
```

The pattern `ana` selects rows 1 and 2 (positions 3 and 1); `a` selects three rows; a pattern that is absent selects an empty block (`[6,6)`).

**Distinct substrings.** The `n(n+1)/2` substrings, counted with repetition, include each repeated one once for every suffix it is a prefix of. Going through the rows in order, row `i` contributes `n - sa[i]` prefixes, of which the first `lcp[i]` were already counted by the row above. So the answer is `n(n+1)/2` minus the sum of the LCP array. Both structures agree:

```
T3 distinct substrings: banana 15 (21 minus lcp sum 6), automaton 15; aaaa 4, automaton 4
```

**Longest repeated substring.** A substring occurs twice exactly when it is a common prefix of two suffixes, so the largest LCP value is the length of the longest one, and the row tells you where it is.

**Longest common substring of two strings `a` and `b`.** Sort the suffixes of `a + '\1' + b`, where `'\1'` is a letter that appears in neither. A common substring is a common prefix of a suffix from `a` and a suffix from `b`. If two rows `i < j` from different sides share `L` letters, then every row between them shares them too (the LCP of far rows is the minimum over the range), and somewhere between them two **neighbouring** rows lie on different sides, each pair with an LCP of at least `L`. So it is enough to look at neighbouring rows from different sides and take the largest LCP. The separator stops any prefix from running across the boundary.

```
T4 longest repeated substring: banana -> ana; abcabcab -> abcab; longest common substring of xabcdy and zabcdw: abcd (suffix array), abcd (automaton)
```

The test that drops the "different sides" condition is the sixth mutant, and it fails: two suffixes of `a` alone can share a long prefix that is not common to `b`.

## 5. The suffix automaton

A DFA that accepts exactly the suffixes of `s` looks big: one state per suffix would be `n` states, but they share structure. The **suffix automaton** is the smallest such DFA, and the surprise is that it has at most `2n - 1` states and at most `3n - 4` edges, and can be built online, one letter at a time, in `O(n)` (alphabet size taken as a constant).

**What a state is.** For a substring `t` of `s`, look at the set of positions where `t` *ends* (its **endpos** set). Two substrings with the same endpos set behave identically for the future: whatever letter follows, they can be extended in exactly the same ways. Group the substrings by endpos set; each group is one state. The members of a group are suffixes of each other, with consecutive lengths: the group is "the strings of length `minlen .. len` that end at these positions", where `len` is the length of the longest member.

- `len[v]` is the length of the longest string in the state `v`.
- `link[v]` is the state of the longest suffix of the strings of `v` that has a **larger** endpos set (that is, belongs to a different state). The number of strings in `v` is therefore `len[v] - len[link[v]]`. The links form a tree rooted at the empty string, the **suffix link tree**.
- An edge `v --c--> w` means: appending `c` to the strings of `v` gives strings of `w`.

**Construction.** To add a letter `c`, create a state `cur` with `len = len[last] + 1`. Walk from `last` along suffix links; every state `p` on the way that has no edge for `c` gets an edge to `cur`. The walk stops at a state `p` that already has an edge `p --c--> q`, or at the beginning (then `link[cur]` is the root). If `len[p] + 1 == len[q]`, the string `p + c` is the longest one in `q`, and `link[cur] = q`. Otherwise `q` also contains strings *longer* than `p + c` that have a different endpos set, so `q` must be **split**: create a **clone** with `len = len[p] + 1` and the same edges as `q`, redirect the edges from `p` and its link ancestors to the clone, and make the clone the link of both `q` and `cur`. The clone is what keeps the automaton minimal.

```cpp
// Suffix automaton: the smallest DFA that accepts exactly the suffixes of s.  Every state is a class of substrings that end at the
// same set of positions; len = the longest member, link = the state of the longest suffix that is NOT in this class.
struct SuffixAutomaton {
    struct State { int len = 0, link = -1; std::array<int, 26> next; State() { next.fill(-1); } };
    vector<State> st{State()}; vector<char> is_clone{0}; vector<long long> cnt; int last = 0;
    void extend(char ch) {
        int c = ch - 'a', cur = (int)st.size();
        st.emplace_back(); is_clone.push_back(0); st[cur].len = st[last].len + 1;
        int p = last;
        while (p != -1 && st[p].next[c] == -1) { st[p].next[c] = cur; p = st[p].link; }
        if (p == -1) st[cur].link = 0;
        else {
            int q = st[p].next[c];
            if (st[p].len + 1 == st[q].len) st[cur].link = q;
            else {                                      // q is too long for this suffix: split it with a clone
                int cl = (int)st.size(); State copy = st[q];
                st.push_back(copy); is_clone.push_back(1); st[cl].len = st[p].len + 1;
                while (p != -1 && st[p].next[c] == q) { st[p].next[c] = cl; p = st[p].link; }
                st[q].link = st[cur].link = cl;
            }
        }
        last = cur;
    }
    explicit SuffixAutomaton(const string& s) { for (char ch : s) extend(ch); }
    long long distinct_substrings() const {
        long long total = 0;
        for (size_t v = 1; v < st.size(); ++v) total += st[v].len - st[st[v].link].len;
        return total;
    }
    long long transitions() const { long long t = 0; for (auto& x : st) for (int y : x.next) t += (y != -1); return t; }
    int walk(const string& p) const {                  // the state reached by p, or -1 if p is not a substring
        int v = 0;
        for (char ch : p) { v = st[v].next[ch - 'a']; if (v == -1) return -1; }
        return v;
    }
    bool contains(const string& p) const { return walk(p) != -1; }
    void prepare_counts() {                            // occurrences: 1 for each real end position, added up the suffix links, longest first
        cnt.assign(st.size(), 0);
        vector<int> order(st.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return st[a].len > st[b].len; });
        for (int v : order) { if (v && !is_clone[v]) cnt[v] += 1; if (v) cnt[st[v].link] += cnt[v]; }
    }
    long long count(const string& p) const { int v = walk(p); return v == -1 ? 0 : cnt[v]; }
};
// Longest common substring: build the automaton of a, then run b through it, falling back along suffix links on a mismatch.
inline string longest_common_substring_sam(const string& a, const string& b) {
    SuffixAutomaton A(a);
    int v = 0, l = 0, best = 0, end = 0;
    for (int i = 0; i < (int)b.size(); ++i) {
        int c = b[i] - 'a';
        while (v && A.st[v].next[c] == -1) { v = A.st[v].link; l = A.st[v].len; }
        if (A.st[v].next[c] != -1) { v = A.st[v].next[c]; ++l; } else { v = 0; l = 0; }
        if (l > best) { best = l; end = i; }
    }
    return best == 0 ? string() : b.substr(end - best + 1, best);
}
```

Small example, `abcbc`. Each entry reads `[state: len, link, edges]`; the clones are the states that appear when the second `b` and the second `c` arrive:

```
T6 automaton of abcbc has 8 states (state: len, link, edges): [0: 0, -1, a>1 b>5 c>7] [1: 1, 0, b>2] [2: 2, 5, c>3] [3: 3, 7, b>4] [4: 4, 5, c>6] [5: 1, 0, c>7 clone] [6: 5, 7,] [7: 2, 0, b>4 clone]; distinct substrings 12
```

The clones are states 5 (the string `b`) and 7 (the strings `bc` and `c`). Each was split off from a state that would otherwise have had to stand for strings with different end positions: `b` ends at positions 1 and 3, but `ab` ends only at position 1, so they cannot share a state. Check the definition on state 4: `len` 4, link to state 5 (length 1), so it holds the `4 - 1 = 3` strings `abcb`, `bcb` and `cb`. Adding `len - len[link]` over states 1 to 7 gives `1 + 1 + 1 + 3 + 1 + 3 + 2 = 12`, the number of distinct substrings printed at the end of the line. The automaton has 8 states for `n = 5` letters, within the bound `2n - 1 = 9`.

**The size bounds.** At most `2n - 1` states for `n >= 2` and at most `3n - 4` edges for `n >= 3` (known results, quoted from memory; each letter adds one state and at most one clone, which is why the count is about two per letter; the tests assert both bounds on 30,000 random strings). Where they are tight:

```
T7 sizes: a then 9 b (n=10): 19 states and 19 edges (limits 2n-1=19 and 3n-4=26); a^10: 11 states, 10 edges; random n=1000 over 26 letters: 1344 states, 2337 edges (limits 1999 and 2996); a, eight b, then c (n=10): 18 states and 26 edges
```

`a` followed by nine `b` reaches exactly `2n - 1 = 19` states; `a^n` needs only `n + 1` states and `n` edges; `a`, eight `b` and a `c` reaches the edge bound `3n - 4 = 26`. A random string has far fewer: 1,344 states for 1,000 letters. For 500,000 random letters:

```
T9 random 500000 letters: the automaton has 661553 states (1.32 per letter) and 1157732 edges; at 112 bytes a state that is 74.1 MB
```

**Memory.** With a 26-letter table per state, a state takes about 112 bytes in my layout (a table of 26 ints, `len`, `link`), so 662,000 states take 74 MB, about 18 times the 4 MB of a suffix array plus its LCP array (8 bytes per letter). That is the price of speed; for large alphabets the table must be replaced by a map or a sorted edge list.

## 6. What you can do with the automaton

**Distinct substrings.** Each state `v` stands for `len[v] - len[link[v]]` different strings, and the states partition all substrings, so the total is the sum over states. (Section 4 printed it: 15 for `banana`, from both structures.)

**Contains / walk.** Follow the letters of `p` from the root; the pattern occurs iff you never fall off. `O(m)`, no logarithm.

**Number of occurrences.** The occurrences of the strings in `v` are the end positions of `v`. A *non-clone* state was created by one letter of the text, so it accounts for one end position (the prefix that created it); a clone accounts for none of its own. The end positions of a state are the union of those of the states below it in the link tree. So: give 1 to every non-clone state, then add each state's total into its link, going from the longest `len` to the shortest. After that, the occurrences of `p` are the total of the state it reaches. Counting the clones as 1 as well is the fifth mutant, and the tests catch it.

```
T8 occurrences in banana by the automaton: a 3; na 2; ana 2; banana 1; nab 0; x 0;
```

**Longest common substring** of `a` and `b`. Build the automaton of `a`, then run `b` through it, tracking the length `l` of the current match. On a missing edge, follow suffix links (the length drops to the `len` of the new state) until an edge exists; on reaching the root without an edge, restart at 0. The largest `l` seen is the answer, in `O(|a| + |b|)`. Compare it with the suffix-array method of section 4:

```
T11 longest common substring of two random 50000-letter strings over {a,b}: length 30 by the suffix array, 30 by the automaton
```

Two different algorithms agree on every random test and on this larger one.

## 7. Measured costs

Random 26-letter text of 500,000 letters unless said otherwise.

| Operation | Time, ms (run 1 / run 2) |
|---|---|
| Suffix array, prefix doubling (`std::sort`), random | 121.8 / 130.0 |
| LCP array (Kasai) | 8.7 / 10.1 |
| Suffix automaton, build | 115.0 / 116.1 |
| Naive sort of the suffixes with `string::compare`, random | 108.6 / 104.8 |
| Suffix array by doubling, `a^500000` (19 rounds) | 278.9 / 252.3 |
| Naive sort of the suffixes, `a^50000` | 206.1 / 196.5 |
| Suffix array by doubling, `a^50000` | 16.3 / 17.3 |
| 100,000 occurrence counts of 8-letter patterns, suffix array | 43.8 / 45.8 |
| The same, automaton | 10.1 / 11.6 |
| Longest common substring of two 50,000-letter strings over `{a, b}`, suffix array | 21.8 / 20.3 |
| The same, automaton | 10.6 / 8.3 |

```
T10 100000 patterns of 8 letters cut from the text: 100000 occurrences by the suffix array, 100000 by the automaton
```

(The patterns were cut from the text, so each occurs at least once; all 100,000 counts are 1 in both structures, a check that they agree.)

What the numbers say, and what they do not:

- **On random text the naive sort is as fast as doubling** (about 105 to 109 ms against 122 to 130 ms for 500,000 letters, slightly faster): comparisons stop after a few letters and `memcmp` is fast. The naive method is not "slow", it is **unbounded**: on `a^50000` it takes about 200 ms against 17 ms for doubling, and the gap grows with `n` (a quadratic cost against a near-linear one). Doubling's time is nearly independent of the text.
- **The automaton answers queries about 4 times faster than the suffix array** (10 to 12 ms against 44 to 46 ms for 100,000 patterns), because it walks `m` edges and the array does `log n` comparisons of up to `m` letters.
- **The automaton builds as fast as the array** here (about 115 ms against 122 to 130 ms), but uses about 18 times the memory.
- For the **longest common substring** the automaton was about 2 to 2.5 times faster (8 to 11 ms against 20 to 22 ms), and it needs no separator.
- I did not measure a linear-time suffix-array algorithm (SA-IS) or a radix-sorted doubling; the figures above are for my `std::sort` version only.

## 8. Which tool

| Need | Use |
|---|---|
| One pattern in one text, once | `find` or KMP; no preprocessing pays off |
| Many patterns against one fixed text | suffix array (little memory) or suffix automaton (faster queries) |
| Many fixed patterns against many texts | Aho-Corasick (12.04) |
| Distinct substrings, longest repeated, k-th substring in order | suffix array + LCP |
| Occurrence **counts** for many queries | suffix automaton |
| Longest common substring of two strings | automaton (shortest code) or suffix array |
| Longest repeated substring of a **medium** text, once | rolling hash + binary search on the length (12.02), no structure to build |
| Compression and full-text indexes for large data | suffix array / BWT / FM-index (from memory: `bzip2` and read aligners such as BWA are built on these) |

A **suffix tree** is the third structure on the same idea (a compressed trie of all suffixes). Ukkonen's algorithm builds it online in `O(n)` (from memory); the suffix link tree of the automaton of the reversed string is the suffix tree of the string (also from memory). Everything in this tutorial can be done with a suffix tree; the array is chosen for its memory, the automaton for its short code.

## 9. Complexities

| Operation | Time | Space |
|---|---|---|
| Suffix array, prefix doubling with `std::sort` | `O(n log^2 n)` | `O(n)` |
| Suffix array, radix doubling / SA-IS / DC3 (from memory) | `O(n log n)` / `O(n)` | `O(n)` |
| LCP, Kasai | `O(n)` | `O(n)` |
| Search by binary search | `O(m log n)` | |
| Distinct substrings, longest repeated | `O(n)` after building | |
| Suffix automaton, build | `O(n)` (constant alphabet) | at most `2n - 1` states, `3n - 4` edges |
| Automaton walk / count | `O(m)` | |
| Longest common substring | `O(n + m)` automaton; `O((n + m) log^2)` array with `std::sort` | |

## 10. Common mistakes

1. **Forgetting the separator** when combining two strings; a "common" substring then runs across the boundary. The separator must be a letter that occurs in neither string, and it must sort consistently.
2. **Comparing the wrong ranks in doubling**: the second key is the rank of the suffix `k` positions later, with `-1` when it does not exist (the short suffix sorts first).
3. **Stopping the doubling too early.** Stop when all ranks differ, not after a fixed number of rounds.
4. **Resetting `h` in Kasai** (quadratic, and no test of the answers will notice).
5. **Using the LCP of non-adjacent rows without taking the minimum** over the rows between them.
6. **Off-by-one in the distinct-substring count**: `lcp[0]` must be 0, and the sum runs over all rows.
7. **Skipping the clone** in the automaton, or copying the edges but not redirecting the edges from `p` and its ancestors.
8. **Counting clones as occurrences.**
9. **Sorting the states by `len` with the wrong direction** when propagating counts; the longest must go first.
10. **Using a 26-entry table for a large alphabet**; use a map or sorted edges.
11. **Recursing over the suffix link tree** on a long text; the tree can be as deep as the text. Use a loop, as the count propagation does.
12. **Expecting a speedup from the structure on a single query.** Building costs `O(n log^2 n)` or `O(n)`; it only pays off over many queries.

## 11. Questions to answer out loud

1. Why is every substring a prefix of a suffix? How does that make a structure on `n` suffixes serve `n^2` substrings?
2. Define the suffix array and the LCP array. How do you get the LCP of two non-adjacent rows?
3. Describe prefix doubling. Why at most `log n` rounds, and what makes `a^n` the slowest input?
4. Why is Kasai's algorithm linear? What happens if you do not decrement `h`?
5. How do you find all occurrences of a pattern with a suffix array, and what is the cost?
6. Why is the number of distinct substrings `n(n+1)/2` minus the sum of the LCP values?
7. How do you find the longest common substring of two strings with a suffix array? Why the separator, and why the "different sides" test?
8. What does a state of the suffix automaton represent? What are `len` and `link`?
9. When does the construction clone a state, and why?
10. Give the bounds on states and edges, and a string that reaches the state bound.
11. How do you count the occurrences of every substring with the automaton?
12. When would you pick the array, the automaton, or a rolling hash?

## 12. Problems

### 12.1 On paper

Answer first, then compare with the answers at the bottom.

**A.** Write the suffix array of `mississippi`.

**B.** How many times do `ss` and `i` occur in `mississippi`?

**C.** How many distinct substrings do `abab` and `mississippi` have?

**D.** What is the longest repeated substring of `mississippi`?

**E.** What is the longest common substring of `abcde` and `xcdey`?

**F.** How many states does the suffix automaton of `abab` have?

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. "Premium" problems need a paid account.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Longest Duplicate Substring | 1044 | LeetCode (hard) | Binary search on the length with a rolling hash (12.02), or the largest LCP value of a suffix array |
| Longest Repeating Substring | 1062 | LeetCode (premium) | The same, with a small input; the LCP maximum is the answer |
| Maximum Length of Repeated Subarray | 718 | LeetCode | Dynamic programming `O(n m)`, or the longest common substring by automaton or array |
| Number of Distinct Substrings in a String | 1698 | LeetCode (premium) | `n(n+1)/2` minus the LCP sum, or the automaton's sum of `len - len[link]` |
| Longest Common Subpath | 1923 | LeetCode (hard) | Binary search on the length with hashing, or a generalised suffix automaton (from memory; the hard version of the same idea) |
| Find the Index of the First Occurrence in a String | 28 | LeetCode | KMP or `find`; a suffix array is only worth it for many queries |

### 12.3 In your editor

**A.** Replace `std::sort` in the doubling by a radix sort of the pairs and measure the gain on the 500,000-letter text and on `a^500000`.

**B.** Use the suffix array to print the `k`-th smallest distinct substring in dictionary order, in `O(n)` after building.

**C.** Answer "how many times does each substring of length `L` occur?" for all positions with the LCP array and a stack or a sparse table for range minimum queries.

**D.** Build a **generalised** suffix automaton for several strings (reset `last` to the root between strings, with the usual guard) and find the longest common substring of three strings.

**E.** Replace the 26-entry table by a sorted edge list or a hash and measure the memory and speed on a 256-letter alphabet.

### Answers

The program that checks the paper problems printed:

```
A 10 7 4 1 0 9 8 6 3 5 2
B 2 4
C 7 53
D issi
E cde
F 5
```

Reading it line by line:

**A.** `10 7 4 1 0 9 8 6 3 5 2`, starting with the suffix `i` (position 10) and ending with `ssissippi` (position 2).

**B.** `ss` occurs 2 times (positions 2 and 5) and `i` 4 times.

**C.** `abab` has 7 distinct substrings (10 counted with repetition, minus an LCP sum of 3), and `mississippi` has 53 (66 minus an LCP sum of 13).

**D.** `issi`, which occurs at positions 1 and 4 (the two occurrences overlap).

**E.** `cde`.

**F.** 5 states, including the root.

## Cheat sheet

- **Suffix array:** positions of the suffixes in sorted order. Doubling: sort by `(rank[i], rank[i + k])`, `k = 1, 2, 4, ...`, until all ranks differ; at most `ceil(log2 n)` rounds.
- **LCP (Kasai):** `h` drops by at most 1 per step, so `O(n)`. LCP of two rows = minimum of the adjacent values between them.
- **Search:** two binary searches give the block of rows that start with `p`.
- **Distinct substrings:** `n(n+1)/2 - sum(lcp)`. **Longest repeated:** `max(lcp)`. **Longest common substring:** `a + sep + b`, adjacent rows from different sides.
- **Suffix automaton:** state = set of substrings with the same end positions; `len`, `link`, edges; add a letter, walk the links, **clone** when `len[p] + 1 != len[q]`. At most `2n - 1` states, `3n - 4` edges.
- **Automaton uses:** distinct substrings = sum of `len - len[link]`; contains = walk; occurrences = 1 per non-clone state, summed up the links, longest first; longest common substring = run `b` through the automaton of `a`.
- **Cost of the automaton:** about 112 bytes per state here; 18 times the memory of an array plus LCP.

---

## Sources

- All code is tested here (3,765,694 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- Prefix doubling (Manber and Myers), Kasai et al.'s LCP algorithm, SA-IS, DC3, Ukkonen's suffix tree, the suffix automaton (Blumer et al.; the online construction is the standard competitive-programming presentation) and the bounds of `2n - 1` states and `3n - 4` edges are standard results, described here from memory. The statements about `bzip2`, BWA, the FM-index and the relation between the link tree and the suffix tree are also from memory and were not checked.
- The explanations and code are my own. LeetCode numbers and titles are from memory; check a number before you rely on it.
