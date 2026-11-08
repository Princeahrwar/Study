# DSA 12.04: Multi-Pattern Matching: Tries and Aho-Corasick

*Part of the [String algorithms plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Tries](../07_trees/17_tries.md) (the structure underneath), [Binary tries, radix trees and longest-prefix match](../07_trees/18_binary_tries_radix_trees_and_lpm.md) (its section 6 introduces Aho-Corasick and measures it; this tutorial goes deeper and does not repeat that table), and [KMP and the Z-function](01_kmp_and_the_z_function.md) (the failure link here is the prefix function, generalised)*

Searching a text for **one** pattern is KMP, or just `std::string::find`. Searching for **thousands** of patterns at once (every word of a dictionary, every signature in a virus database, every banned phrase in a chat filter) by running one search per pattern means one full pass over the text per pattern. **Aho-Corasick** (1975) does it in a single pass whose cost does not depend on the number of patterns: put the patterns in a trie, then add KMP-style *failure links* so that after a mismatch the search drops to the longest suffix that is still a prefix of some pattern, and never moves backwards in the text.

By the end you can:

- build the trie, the failure links and the filled-in transition table, and say what each node means;
- explain why, for a single pattern, the failure links are exactly the prefix function of KMP;
- report every occurrence in `O(text + occurrences)` using dictionary links, and say why the failure links alone would miss matches;
- count the occurrences of every pattern without listing them, by pushing counts down the failure links;
- solve the three classic uses: a **stream** matcher (LeetCode 1032), **bold tags** over matched words (LeetCode 758), and **counting strings that avoid every pattern** with a dynamic program over the automaton states;
- compare the full automaton (one lookup per letter) with the lazy one (walk the failure links), including the cost of the worst single letter;
- estimate the memory of an automaton and say when the plain table must be replaced.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 802,092 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran on 30,000 random pattern sets and texts (alphabets of 1 to 3 letters, up to 8 patterns of 1 to 5 letters, duplicates included): the full automaton's search, the lazy matcher's search, the occurrence counts, the yes/no check, the stream matcher (true at exactly the ends of matches) and the covered-letters mask, each against a brute force; the failure links against their *definition* (the longest proper suffix that is a node, found by rebuilding the strings of the nodes); for one pattern, the failure links against the prefix function from 12.01; and the avoiding-string counts against full enumeration. Six deliberate bugs (a wrong failure link, no dictionary links, the counting loop in the wrong order, a wrong `maxlen`, the lazy matcher without its fall-back loop, and the avoiding DP without the filter) were each caught by the tests.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours. Counts and traces are the same in both runs. Facts about real tools (Snort, `grep -F`, ClamAV, the Linux kernel) are from memory and marked as such.

---

## 1. The problem, and what one pattern at a time costs

You have `k` patterns with total length `m` and a text of length `n`. You want every place where any pattern occurs.

The baseline is a loop of single-pattern searches. With KMP that is `O(n + |p|)` per pattern, so `O(k * n + m)` in total; with `find` it is about the same in practice. Take a dictionary of 100,000 words and a 10 MB text: that is a hundred thousand passes over ten megabytes. The measurement in [Trees 18](../07_trees/18_binary_tries_radix_trees_and_lpm.md) showed the shape: the `find` loop grew from 24 ms to 1,532 ms as the patterns went from 100 to 5,000, while the automaton grew from 1.3 ms to 6.5 ms.

A better target is **`O(n + m + z)`**, where `z` is the number of occurrences you must report (you cannot report faster than you write). Aho-Corasick reaches it.

Two variants of the question appear in problems, and they are not the same:

- *Report* every occurrence (with its pattern and position);
- *Decide* or *count* something about occurrences (is there any? how many of each? which letters are covered?), where listing all of them might be far too slow, because `z` can be as large as `n * k`. Sections 5 and 6 are about these.

## 2. From the trie to the failure link

Put all the patterns in a trie ([Trees 17](../07_trees/17_tries.md)): one node per distinct prefix of any pattern, an edge per letter. Reading the text, imagine you keep the node for *the longest suffix of the text so far that is a prefix of some pattern*. Call that the **current node**. It is the generalisation of KMP's "length of the current match".

Now the next letter `c` arrives.

- If the current node has a child for `c`, go there. Done, one step.
- If not, the match cannot be extended. KMP would fall back to the longest border. Here, fall back to the **failure link**: the node for the **longest proper suffix of the current node's string that is also a node of the trie**. Try `c` again from there, and keep falling back until a node has a child for `c` or you reach the root.

Define, for a node `v` with string `s(v)`:

> `fail(v)` = the node whose string is the longest **proper** suffix of `s(v)` that is a node of the trie (the root, which is the empty string, if there is none).

Take the patterns `a`, `ab`, `bab`, `bc`, `bca`, `c`, `caa`. The program rebuilds each node's string and prints its failure node and whether the string ends with some pattern:

```
T1 patterns a ab bab bc bca c caa give 11 nodes (root included); node string -> fail node string, ends a pattern?: [a -> root, yes] [b -> root, no] [c -> root, yes] [ab -> b, yes] [ba -> a, yes] [bc -> c, yes] [ca -> a, yes] [bab -> ab, yes] [bca -> ca, yes] [caa -> a, yes]
```

Read two entries. The node `bab` fails to `ab`: the longest proper suffix of `bab` that is in the trie is `ab` (`ab` is a pattern, so it is a node; `b` is also a node but shorter). The node `ba` fails to `a` and ends a pattern even though `ba` is not a pattern, because its suffix `a` is. That is the point of the last column: **a node "ends a pattern" if its own string is a pattern or if any node along its failure chain is.** Keep this, it comes back in section 4.

**Why this is KMP.** For *one* pattern the trie is a single path, with nodes for the prefixes of lengths `1 .. m`. The failure link of the node of length `k` is the longest proper suffix of that prefix which is also a prefix: its longest *border*, which is `pi[k - 1]`, the prefix function of 12.01. The program checks that on `abacab`:

```
T3 single pattern abacab: fail length of nodes 1..6 = 0 0 1 0 1 2; prefix function = 0 0 1 0 1 2 (equal: yes)
```

(The tests check it for random patterns too.) So Aho-Corasick is KMP with the single line of nodes replaced by a tree. The same amortised argument that made KMP linear works here, and section 7 states it.

**How to compute the links.** Process the nodes **level by level** (breadth first). The failure link of a child `u = v --c--> u` comes from its parent: start at `fail(v)`, and move along failure links until some node has an edge `c`; that edge's end is `fail(u)`. In words: *to find the longest proper suffix of `s(v) c`, find the longest proper suffix of `s(v)` that can be extended by `c`.* This needs `fail(v)` to be known already, which is why a depth-first order breaks: the parent's failure link and everything on shorter levels must exist first. Children of the root fail to the root.

## 3. The full automaton: fill in the missing edges

Walking the failure links on every mismatch works, but there is a neater way. Since the BFS visits nodes in order of depth, when it processes `v`, every node on `v`'s failure chain has already been given a **complete** row of transitions. So for a letter `c` that `v` has no edge for, the answer "the node reached by falling back until `c` works" is exactly `go[fail(v)][c]`, already computed. Write that into the table:

```
go[v][c] = (edge exists) ? child : go[fail(v)][c]
```

Now the table is a **deterministic automaton**: from any node, any letter has exactly one next node, and the search needs one table lookup per text letter and no loops. The cost of this is a table of `sigma` entries per node (section 8 puts a number on it).

Besides the transitions, `build` fills four arrays that the later sections use:

- `hit[v]`: does the string of `v` end with some pattern? (Own pattern, or `hit` of the failure node.)
- `dict[v]`: the **nearest node along the failure chain that ends a pattern** (the dictionary link), or `-1`.
- `maxlen[v]`: the length of the longest pattern that is a suffix of `s(v)`.
- `order`: the nodes in breadth-first order, which section 5 reuses backwards.

Patterns can repeat, and several patterns can end at the same node; they are kept in a small chain per node (`head`, `nxt`), so a pattern id is just the order of `add`.

```cpp
// Aho-Corasick automaton for lowercase patterns (a..z).  A trie of the patterns, then:
//   fail[v] = the longest PROPER suffix of the string of node v that is also a node of the trie  (KMP's border, for many patterns);
//   go[v][c] = the node after reading letter c in node v  (a full automaton: missing edges borrow the fail node's, so a search needs
//              exactly one table lookup per text letter);
//   hit[v]   = does the string of v END with some pattern?  (v's own patterns, or any patterns along its fail chain);
//   maxlen[v]= the length of the longest pattern that ends with the string of v.
// Patterns may repeat; a pattern id is the order of add().  Patterns must be non-empty.
struct AhoCorasick {
    vector<std::array<int, 26>> go{std::array<int, 26>{}};
    vector<int> fail{0}, head{-1}, dict{-1}, order, node_of, nxt, maxlen{0}, len{0};
    vector<char> hit{0};
    bool built = false;
    int add(const string& p) {
        int v = 0;
        for (char ch : p) {
            int c = ch - 'a';
            if (!go[v][c]) {
                go[v][c] = (int)go.size(); go.emplace_back(); go.back().fill(0);
                fail.push_back(0); head.push_back(-1); dict.push_back(-1); maxlen.push_back(0); hit.push_back(0); len.push_back(len[v] + 1);
            }
            v = go[v][c];
        }
        int id = (int)node_of.size();
        node_of.push_back(v); nxt.push_back(head[v]); head[v] = id;          // patterns that end at the same node form a chain
        return id;
    }
    void build() {                                                           // breadth first: a node's fail link comes from its parent's
        order.clear(); order.push_back(0);
        for (size_t h = 0; h < order.size(); ++h) {
            int v = order[h];
            if (v) {
                dict[v] = (head[fail[v]] != -1) ? fail[v] : dict[fail[v]];   // nearest node along the fail chain that ends a pattern
                hit[v] = hit[v] || head[v] != -1 || hit[fail[v]];
                maxlen[v] = head[v] != -1 ? len[v] : maxlen[fail[v]];       // the node's own pattern is longer than any proper suffix's
            }
            for (int c = 0; c < 26; ++c) {
                int u = go[v][c];
                if (u) { fail[u] = v ? go[fail[v]][c] : 0; order.push_back(u); }
                else go[v][c] = v ? go[fail[v]][c] : 0;                      // borrow: no loop needed at search time
            }
        }
        built = true;
    }
    // Every occurrence, as report(end_index, pattern_id), through the dict links: O(n + number of occurrences).
    template <class F> void search(const string& t, F report) const {
        int v = 0;
        for (int i = 0; i < (int)t.size(); ++i) {
            v = go[v][t[i] - 'a'];
            for (int u = head[v] != -1 ? v : dict[v]; u != -1; u = dict[u])
                for (int id = head[u]; id != -1; id = nxt[id]) report(i, id);
        }
    }
    // The number of occurrences of EVERY pattern, without listing them: count the visits to each node, then push the counts down the
    // fail links from the deepest nodes up (a node's visits also count as visits to its fail node).  O(n + nodes).
    vector<long long> count_occurrences(const string& t) const {
        vector<long long> visits(go.size(), 0); int v = 0;
        for (char ch : t) { v = go[v][ch - 'a']; ++visits[v]; }
        for (int k = (int)order.size() - 1; k > 0; --k) visits[fail[order[k]]] += visits[order[k]];
        vector<long long> r(node_of.size());
        for (size_t id = 0; id < node_of.size(); ++id) r[id] = visits[node_of[id]];
        return r;
    }
    bool contains_any(const string& t) const {
        int v = 0; for (char ch : t) { v = go[v][ch - 'a']; if (hit[v]) return true; } return false;
    }
};
```

Check the shape on the example: `build` is one pass over the nodes with an inner loop over the alphabet, so `O(nodes * sigma)`, and the number of nodes is at most `m + 1`. For the seven patterns above the automaton has 11 nodes (the root and ten more; the program printed the ten).

## 4. Searching, and why failure links alone are not enough

`search` runs the automaton over the text: one lookup per letter, then report the patterns that end here. A first attempt is "report the patterns at the current node". That is wrong. The text `abccab` with the patterns above:

```
T2 text abccab, every occurrence as end index:pattern: 0:a 1:ab 2:bc 2:c 3:c 4:a 5:ab
```

At index 2 the current node is `bc`, which is a pattern, but the letter `c` is also a pattern (the suffix of `bc`), and it ends at the same place. Both must be reported, and `c` is not at the current node; it is at the failure node of `bc`. The same happens at every position where a pattern is a proper suffix of a longer one (`he` inside `she`, `a` inside `ba`).

The fix is the dictionary link. After moving to the node `v`, report `v`'s patterns if it has any, then jump along `dict[v]`, `dict[dict[v]]`, ... and report the patterns of each node reached. Every node on that chain ends a pattern, so each step reports something, and the work is `O(1 + number reported)` per letter. The loop in `search` is exactly that. Total: `O(n + z)`.

Why not follow ordinary failure links? Because most nodes on a failure chain end *no* pattern, and walking over them would cost the chain length for nothing. Take the pattern `a^1000` and a text of `a`'s: the chain of failure links from the deepest node has 1000 nodes, and every one of them ends a pattern only if it is the full pattern, which is rare; with other patterns the same chain holds nodes that end nothing. The dictionary links skip straight to the nodes that do end something. The test that removes the dictionary links (replacing them by `-1`) fails 8,181 of 802,092 checks.

If there is only a yes/no question, no chain is needed at all: `hit[v]` already says whether *some* pattern ends here, and `contains_any` returns at the first letter where it is true.

## 5. Counting every pattern's occurrences without listing them

How many times does each pattern occur? The text can contain `z` as large as `n * k`. For the patterns `a`, `aa`, `aaa` and a text of five `a`:

```
T4 patterns a aa aaa in aaaaa: counts 5 4 3; listing them one by one makes 12 events; the counts add up to 12
```

Counting by reporting costs one step per event (12 here). There is a way that costs `O(n + nodes)` whatever `z` is:

1. Run the text through the automaton and add 1 to `visits[v]` for the node `v` reached after each letter. Only `n` increments.
2. A visit to a node is also a visit, in the sense of "the text ends with this string", to every node on its failure chain. So push the counts down the chain: process the nodes in **reverse breadth-first order** (deepest first), and add `visits[v]` to `visits[fail(v)]`.
3. The count of a pattern is `visits` at its node.

Reverse order is required. A node must receive everything from the deeper nodes that fail to it *before* it passes its total on, and in breadth-first order a node's failure target is always shallower than the node. The test that walks the order forwards fails 11,821 checks. In the example, the five `a`'s reach nodes `a`, `aa`, `aaa`, `aaa`, `aaa`; pushing down gives 5, 4, 3.

This is the answer to "count occurrences of each word in a text" in a dictionary-sized setting: build once, one pass, counts for all patterns.

## 6. Three uses

### 6.1 A stream of letters (LeetCode 1032)

The problem: letters arrive one at a time, and after each you must say whether some suffix of the letters so far is one of the words. Build the automaton once. Keep one number, the current node. On each letter, move and return `hit[node]`. The state is a single integer, so the cost per letter is one lookup, whatever the number of words.

```cpp
// A stream matcher (LeetCode 1032): feed one letter at a time, learn at once whether the letters so far END with some pattern.
// The state is a single node number, so the cost per letter is one lookup, whatever the number of patterns.
struct StreamMatcher {
    const AhoCorasick& A; int v = 0;
    explicit StreamMatcher(const AhoCorasick& a) : A(a) {}
    bool feed(char ch) { v = A.go[v][ch - 'a']; return A.hit[v]; }
};
// Which letters of t lie inside at least one occurrence of a pattern (the bold tags of LeetCode 758)?  At each end position the longest
// pattern ending there covers [i - maxlen + 1, i]; mark the cover with a difference array.
inline vector<char> covered(const AhoCorasick& A, const string& t) {
    int n = (int)t.size(); vector<int> diff(n + 1, 0); int v = 0;
    for (int i = 0; i < n; ++i) {
        v = A.go[v][t[i] - 'a'];
        if (A.maxlen[v]) { ++diff[i - A.maxlen[v] + 1]; --diff[i + 1]; }
    }
    vector<char> r(n); int run = 0;
    for (int i = 0; i < n; ++i) { run += diff[i]; r[i] = run > 0; }
    return r;
}
// How many strings of length L over the first sigma letters contain NONE of the patterns?  A dynamic program over the automaton's
// states: dp[state] = number of strings that lead there without ever passing through a state that ends a pattern.
inline long long count_avoiding(const AhoCorasick& A, int L, int sigma, long long mod) {
    vector<long long> dp(A.go.size(), 0), nx(A.go.size()); dp[0] = 1;
    for (int step = 0; step < L; ++step) {
        std::fill(nx.begin(), nx.end(), 0);
        for (size_t v = 0; v < A.go.size(); ++v) if (dp[v])
            for (int c = 0; c < sigma; ++c) { int u = A.go[v][c]; if (!A.hit[u]) nx[u] = (nx[u] + dp[v]) % mod; }
        dp.swap(nx);
    }
    long long total = 0; for (long long x : dp) total = (total + x) % mod;
    return total;
}
```

(The block above contains the stream matcher and the two functions of sections 6.2 and 6.3; they share the automaton.) With the words `cd`, `f`, `kl`:

```
T5 words cd f kl, stream abcdefghijkl: true after the letters: d f l
```

The naive way to do this problem keeps the last `L` letters (`L` the longest word) and checks each word's suffix on every query, `O(k * L)` per letter. The automaton replaces it by `O(1)`.

### 6.2 Bold tags (LeetCode 758 and 616)

Given words and a string, wrap in bold every letter covered by at least one occurrence of a word, merging overlapping or touching stretches. The automaton gives, at each end position `i`, the length of the **longest** word that ends there (`maxlen`). Covering `[i - maxlen + 1, i]` is enough: any shorter word ending at `i` is inside it. Mark the cover with a difference array (`+1` at the start, `-1` after the end) and take a running sum. With the words `ab`, `bc` over `aabcd`:

```
T6 words ab bc in aabcd: covered mask 01110 -> a<b>abc</b>d
```

The covered stretch is `abc` (positions 1 to 3), so the answer is `a<b>abc</b>d`. Note the stretches `ab` and `bc` overlap; the mask merges them. Words that merely *touch* (one ends where the next starts) are also merged by the problem's rule, and you get that by joining adjacent marked runs when you print.

### 6.3 Counting strings that avoid every pattern

How many strings of length `L` over an alphabet of `sigma` letters contain **none** of the patterns? This is the "forbidden words" counting problem (LeetCode 1397 asks a version of it with bounds, and many contest problems ask it).

Run a dynamic program over the automaton states. Let `dp[v]` be the number of strings of the current length that end in state `v` and have never passed through a state with `hit` true. A step from `v` with letter `c` goes to `u = go[v][c]`; keep it only if `hit[u]` is false. The answer is the sum of `dp` after `L` steps. The cost is `O(L * nodes * sigma)`: there is no enumeration of the strings, so `L` can be in the thousands.

Three small cases:

```
T7 strings over {a,b} avoiding ab, lengths 1..8: 2 3 4 5 6 7 8 9
T7 avoiding aa and bb, lengths 1..8: 2 2 2 2 2 2 2 2
T7 avoiding aba, lengths 1..8: 2 4 7 12 21 37 65 114
```

(In order: avoiding `ab` over `{a, b}`, avoiding `aa` and `bb`, avoiding `aba`.) Read them. A string avoiding `ab` is some `b`'s followed by some `a`'s, so `L + 1` of them. Avoiding both `aa` and `bb` leaves the two alternating strings. Avoiding `aba` gives 2, 4, 7, 12, 21, 37, 65, 114 (I did not look for a closed form; the counts match full enumeration for lengths up to 9 in the tests). The test that drops the `hit` filter fails 3,354 checks, because then the DP counts every string.

The same DP, with an extra state for "how many positions of a second pattern have I matched", is the standard way to solve problems like LeetCode 1397, where the strings must lie between two bounds as well.

## 7. The lazy version, and what one letter can cost

The full automaton hides a cost model. The **lazy** matcher keeps only the trie edges and the failure links and, on a mismatch, walks the links exactly as KMP does:

```cpp
// The same automaton WITHOUT the filled-in transitions: only the trie edges and the fail links are stored, and the search follows fail
// links on a mismatch, exactly like KMP.  Less memory per node is not the point (the table is still there in this version); the point
// is the cost model: amortised O(1) per letter, but a single letter can cost as many steps as the current depth.
struct LazyMatcher {
    vector<std::array<int, 26>> child; vector<int> fail, head, dict, nxt; int pats = 0;
    LazyMatcher() : child(1), fail(1, 0), head(1, -1), dict(1, -1) { child[0].fill(0); }
    int add(const string& p) {
        int v = 0;
        for (char ch : p) {
            int c = ch - 'a';
            if (!child[v][c]) { child[v][c] = (int)child.size(); child.emplace_back(); child.back().fill(0); fail.push_back(0); head.push_back(-1); dict.push_back(-1); }
            v = child[v][c];
        }
        nxt.push_back(head[v]); head[v] = pats; return pats++;
    }
    void build() {
        vector<int> q;
        for (int c = 0; c < 26; ++c) if (child[0][c]) q.push_back(child[0][c]);
        for (size_t h = 0; h < q.size(); ++h) {
            int v = q[h]; dict[v] = (head[fail[v]] != -1) ? fail[v] : dict[fail[v]];
            for (int c = 0; c < 26; ++c) {
                int u = child[v][c]; if (!u) continue;
                int f = fail[v]; while (f && !child[f][c]) f = fail[f];       // KMP's fallback, on the trie
                fail[u] = child[f][c] ? child[f][c] : 0; q.push_back(u);
            }
        }
    }
    template <class F> void search(const string& t, F report) const {
        int v = 0;
        for (int i = 0; i < (int)t.size(); ++i) {
            int c = t[i] - 'a';
            while (v && !child[v][c]) v = fail[v];
            v = child[v][c];
            for (int u = head[v] != -1 ? v : dict[v]; u != -1; u = dict[u])
                for (int id = head[u]; id != -1; id = nxt[id]) report(i, id);
        }
    }
};
```

(My version still allocates a 26-entry table per node, so it does not save memory; the point is the cost model, and a version with sorted edge lists or a hash per node would save memory at the price of slower steps.)

**Amortised `O(1)` per letter.** The depth of the current node grows by at most one per text letter (an edge step) and every failure step makes it strictly smaller. So over a whole text the number of failure steps is at most the number of letters. That is KMP's argument, unchanged.

**Worst single letter.** The amortised bound says nothing about one letter. With the pattern `a^1000 c` and the text `a^1000 b`, the matcher walks 1000 letters into the pattern, and then `b` has no edge at any of the 1000 nodes on the way down:

```
T8 pattern a^1000 c, text a^1000 b (1001 letters): the lazy matcher took 1000 fail steps on the single letter b and 1000 in total (never more than the letters read so far)
```

The first 1000 letters cost nothing in failure steps, and the letter `b` costs 1000 of them: still within the bound (1000 steps in total for 1001 letters), but for a real-time system (a network device that must handle each packet within a fixed budget) a latency of `depth` steps on one letter may be unacceptable. The full automaton has no such spike: each letter is one lookup. That is the reason hardware and network engines build the full table, and the price is memory.

## 8. Measured costs and memory

The setting: 100,000 random patterns of 6 to 10 letters over 26 letters, and a random text of 10,000,000 letters. Few patterns occur in such a text (see the last line below), so this measures the automaton itself, not the reporting.

```
T9 100000 random patterns of 6 to 10 letters over 26: 507509 nodes, goto table 52.8 MB, whole automaton about 69.0 MB
T10 1000 random patterns, same 10 MB text: 10 occurrences found
```

| Operation | Time, ms (run 1 / run 2) |
|---|---|
| Build the full automaton (insert 100,000 patterns, then `build`) | 169.2 / 175.2 |
| Build the lazy matcher | 117.6 / 118.5 |
| Search 10 MB, full automaton | 804.3 / 786.6 |
| Search 10 MB, lazy matcher | 590.8 / 580.9 |
| Count every pattern's occurrences, 10 MB (`count_occurrences`) | 749.6 / 777.3 |
| 10 million letters `a`, pattern `a^1000 c`, full automaton | 35.9 / 37.3 |
| The same, lazy matcher | 51.8 / 56.5 |

What these show, and what they do not:

- **The cost per letter is not constant in practice.** 10 million letters in about 820 to 840 ms is about 83 ns per byte, against 2.5 to 13 ns per byte in the 500 KB tests of Trees 18 with at most 5,000 patterns. The algorithm is the same; the automaton there was 2 MB, here it is about 53 MB of table. I did not separate the cause, but the likely one is the memory system: once the table does not fit in the processor's caches, a random walk through it pays a cache miss on most steps. Treat the `O(n)` as the count of steps and the nanoseconds as a function of the table size.
- **The lazy matcher was faster than the full one on random text** (about 550 to 600 ms against 820 to 840 ms). I did not find out why. On random text the matcher spends most of its time at the shallow nodes, and both versions use the same table, so I would not read this as "lazy is better"; read it as "the per-step cost depends on how the memory is touched, and the full automaton's advantage is the bound on the worst letter, not average speed".
- **On the repetitive input** (a pattern that is a long run of `a` and a text of only `a`), the lazy matcher does one failure step per letter and the full one none, and the full one was faster (about 34 to 38 ms against 52 to 56 ms), as expected.
- **Counting** (a pass plus the push-down over 507,509 nodes) costs about as much as reporting, because the pass dominates.

**Memory.** A node of the plain table is `26 * 4 = 104` bytes of transitions plus the arrays (failure, head, dict, length, maxlen, hit, ...), about 136 bytes in this implementation. 507,509 nodes is 52.8 MB for the table alone and about 69 MB for everything. That is for a lowercase alphabet. For **bytes** (256 symbols) the table is 1 KB per node, which for half a million nodes is 500 MB: not acceptable. The standard remedies, which I describe from memory and did not implement here:

- store each node's edges in a **sorted array or a hash** and do the lazy walk (slower steps, memory proportional to the number of edges);
- a **double-array trie**, which packs the table into two integer arrays with a collision-free layout;
- **compressed** or **bitmapped** nodes, which keep a 256-bit mask per node and only the existing edges (this is the idea behind several network-security matchers);
- splitting the byte alphabet into **classes** that no pattern distinguishes (for a case-insensitive search `A` and `a` share a class), which can shrink `sigma` from 256 to a handful.

## 9. Where it is used (from memory)

I have not run any of these; I am describing what I remember of how they work.

- **Intrusion detection and deep packet inspection.** Snort and Suricata match thousands of content patterns against packets, and Aho-Corasick (and its variants) is the classic matcher in their pattern engines. Hardware and GPU versions exist because the full table has a bounded cost per byte.
- **`grep -F` / `fgrep`.** GNU grep, for several fixed strings, uses a matcher in the Aho-Corasick family (the Commentz-Walter algorithm, a Boyer-Moore-flavoured variant, is also in its history).
- **Antivirus signatures.** ClamAV is documented to use Aho-Corasick for part of its signature matching.
- **Linux.** The kernel's `textsearch` library (`lib/textsearch.c`) offers KMP, Boyer-Moore and Finite State Machine back-ends behind one interface, and `xt_string` (the iptables `string` match) uses it to search packet payloads. Those back-ends are single-pattern; I do not remember a multi-pattern one in the kernel.
- **Text processing.** Dictionary-based tokenisers, profanity filters, and keyword highlighters are the everyday uses.

## 10. Complexities

| Operation | Time | Space |
|---|---|---|
| Insert all patterns (trie) | `O(m)` | `O(m * sigma)` for the plain table |
| Build failure links, `hit`, `dict`, `maxlen`, fill the table | `O(m * sigma)` | the same |
| Search, report every occurrence | `O(n + z)` | |
| Yes/no, stream per letter | `O(n)`, `O(1)` per letter | |
| Count every pattern, `count_occurrences` | `O(n + m)` | `O(m)` |
| Lazy search | `O(n + z)` amortised, up to `depth` steps for one letter | |
| Avoiding strings, length `L` | `O(L * m * sigma)` | `O(m)` |

## 11. Which tool

- One pattern: `std::string::find` or KMP. For a handful of patterns, a loop of `find` is often fastest in practice.
- Many patterns, report all occurrences: Aho-Corasick.
- Many patterns, only a yes/no or the first match: Aho-Corasick with `hit` (or a hash set of the words plus a sliding window, if all words have the same length).
- Many patterns of **one length** in a text: a rolling hash and a hash set of the pattern hashes (12.02), which is simpler and uses less memory than the automaton.
- Matching the prefixes of **one query** against a dictionary (autocomplete): a plain trie, not the automaton.
- Patterns with wildcards or classes: the automaton is the wrong tool; look at regular expressions (12.05).
- Very large dictionaries over bytes: compressed or double-array automata; do not allocate 256 entries per node.

## 12. Common mistakes

1. **Depth-first construction of the failure links.** The parent's link and the shallower levels must exist first; use a queue.
2. **Reporting only the current node's patterns**, which misses every pattern that is a proper suffix of another (section 4).
3. **Following plain failure links to report**, which is correct but can cost the chain length per letter instead of the number of matches.
4. **A failure link of the root's children that is not the root.** The failure of a depth-1 node is the root; the general formula would otherwise read a row that does not exist yet.
5. **Pushing counts down in forward order** in the counting trick (section 5); it must be deepest first.
6. **Counting only the longest pattern** at a node for bold tags is right (`maxlen`); counting only the first pattern added is not.
7. **Forgetting duplicate patterns.** Two equal patterns end at the same node: keep both ids (the chain) if each needs a count.
8. **Allowing the empty pattern.** It matches everywhere and breaks the node-per-prefix idea; remove it or treat it separately.
9. **A 256-entry table per node** for a large dictionary, which exhausts memory.
10. **Building the automaton once per query** instead of once per dictionary.
11. **Assuming a uniform cost per letter** in a latency-critical system. With the lazy matcher one letter can cost `depth` steps (section 7).
12. **Thinking the occurrences are few.** The output size `z` can be `n * k`; a design that stores all matches can run out of memory where a count or a callback would not.

## 13. Questions to answer out loud

1. What does the current node mean while the text is being read?
2. What is the failure link of a node? Why is it the prefix function for one pattern?
3. Why is the failure link computed level by level?
4. How does the full automaton fill the missing edges, and why is the lookup `go[fail[v]][c]` already available?
5. Why is the current node's own pattern not enough to report all matches? What does the dictionary link fix?
6. Why is the total work `O(n + z)` and not `O(n * depth)`?
7. How do you count the occurrences of every pattern in `O(n + m)` without listing them? Why in reverse order?
8. How do you solve the stream-of-characters problem in `O(1)` per letter?
9. Why is the longest word ending at a position enough for the bold-tags problem?
10. How do you count the strings of length `L` that avoid a set of words?
11. What is the worst single-letter cost of the lazy matcher, and why does it not break the amortised bound?
12. What happens to the memory of the plain table for a 256-letter alphabet? Name two remedies.

## 14. Problems

### 14.1 On paper

Answer first, then compare with the answers at the bottom.

**A.** For the patterns `he`, `she`, `his`, `hers` and the text `ushers`, how many occurrences are there in total?

**B.** For the words `ab` and `abc` and the string `zabcabx`, how many letters are covered (would be inside bold tags)?

**C.** How many strings of length 1 to 6 over `{a, b}` do not contain `aa`? Give the six numbers, and say what sequence they are.

**D.** Words `a` and `b`, stream `xxaxb`: after how many of the five letters does the stream matcher answer true?

**E.** For the patterns of section 2 (`a ab bab bc bca c caa`), what is the failure node of `bab`, and of `caa`? Use the printed automaton.

### 14.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. "Premium" problems need a paid account.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Stream of Characters | 1032 | LeetCode | Build the automaton from the words, keep the node; answer `hit[node]` |
| Add Bold Tag / Bold Words in String | 616 / 758 | LeetCode (premium) | `maxlen` at each end position and a difference array; merge touching runs |
| Replace Words | 648 | LeetCode | A plain trie of the roots is enough (shortest prefix); the automaton is not needed |
| Word Search II | 212 | LeetCode (hard) | A trie of the words walked by DFS over the grid; this is the trie, not Aho-Corasick |
| String Matching in an Array | 1408 | LeetCode | Words that are substrings of others; the automaton over all words, or brute force with `find` for small input |
| Length of the Longest Valid Substring | 2781 | LeetCode (hard) | Forbidden words: for each end position the shortest forbidden suffix gives a left bound; a two-pointer scan (the automaton needs the *shortest* word at a node, a variant of `maxlen`) |
| Find All Good Strings | 1397 | LeetCode (hard) | Digit-DP over the automaton of one forbidden string, with a tight flag for each of the two bounds (as in 6.3) |

### 14.3 In your editor

**A.** Re-implement the automaton for **bytes** with the edges of each node stored in a sorted vector (binary search) and the lazy walk. Compare the memory and the speed with the 26-entry table on the 100,000-pattern setting.

**B.** Write an `fgrep -F`: read a file of patterns and a text file, print every line that contains at least one pattern. Compare with `grep -F` on a large file.

**C.** Make the matcher **case-insensitive** by mapping each byte through a fold table before the automaton, and compare with building two copies of the patterns.

**D.** For the dictionary of Trees 18's measurement, reproduce its table (100, 1,000, 5,000 patterns on 500,008 bytes) with this automaton and compare with the numbers there.

**E.** Add the *shortest* pattern at each node (`minlen`) and use it to solve LeetCode 2781.

### Answers

The program that checks the paper problems printed:

```
A 3
B 5
C 2 3 5 8 13 21
D 2
```

Reading it line by line:

**A.** `3`: `she` and `he` end at index 3 (both are found, `he` through the dictionary link) and `hers` at index 5.

**B.** `5`: `abc` at positions 1 to 3, covered by `abc` (and by `ab`), plus `ab` at positions 4 and 5. The `z` and `x` are not covered.

**C.** `2 3 5 8 13 21`: the Fibonacci numbers. After a string of length `L` that avoids `aa`, you can add `b` always, or `a` only if the last letter is `b`.

**D.** `2`: after the `a` and after the `b`. The `x`'s give false.

**E.** From the printed automaton: `bab` fails to `ab`, and `caa` fails to `a`.

## Cheat sheet

- **Idea:** trie of the patterns + failure links = KMP for many patterns; the current node is the longest suffix of the text read so far that is a prefix of some pattern.
- **Failure link** of `v`: the longest proper suffix of `s(v)` that is a node. Computed breadth first from the parent's link. One pattern: it equals the prefix function.
- **Full automaton:** `go[v][c] = go[fail[v]][c]` for missing edges; one lookup per letter. Memory `nodes * sigma`.
- **Reporting:** at each node report its patterns, then follow `dict` links. `O(n + z)`. Without dict links you miss patterns that are suffixes of others.
- **Counting all patterns:** `visits[node]++` per letter, then add `visits[v]` to `visits[fail[v]]` in reverse breadth-first order. `O(n + m)`.
- **Stream:** keep one node; answer `hit[node]`. **Bold tags:** `maxlen` + difference array. **Avoiding strings:** DP over states that skip `hit` states.
- **Lazy matcher:** amortised `O(1)` per letter, up to `depth` steps for one letter.
- **Memory:** 104 bytes of table per node for 26 letters (1 KB for bytes); compress for large byte dictionaries.

---

## Sources

- All code is tested here (802,092 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each. Times are this machine's.
- Aho and Corasick, "Efficient string matching: an aid to bibliographic search" (1975, from memory). The counting-by-pushing-down trick, the avoiding-strings DP and the bold-tags use of the longest word are standard competitive-programming techniques; the explanations and code are my own.
- Descriptions of Snort, Suricata, `grep -F`, ClamAV and the Linux `textsearch` library are from memory and were not checked against their sources.
- LeetCode numbers and titles are from memory; check a number before you rely on it.
