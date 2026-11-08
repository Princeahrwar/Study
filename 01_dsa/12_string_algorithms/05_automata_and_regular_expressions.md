# DSA 12.05: Automata and Regular Expressions

*Part of the [String algorithms plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · Stretch topic · You need: [KMP and the Z-function](01_kmp_and_the_z_function.md) (section 9 builds the KMP automaton, a DFA) and [Multi-pattern matching: tries and Aho-Corasick](04_multi_pattern_matching_tries_and_aho_corasick.md) (a DFA with one lookup per letter; this tutorial generalises it from fixed words to patterns with choices and repetition)*

A **regular expression** such as `(a|b)*abb` describes a set of strings with three operations: choice (`|`), repetition (`*`) and concatenation. Everything you have seen in this area so far (KMP, Aho-Corasick) is a **finite automaton** in disguise: a machine with a fixed number of states that reads the text one letter at a time and never goes back. This tutorial connects the two. You compile a regular expression into an automaton, run it two ways that never backtrack, and see exactly why many library engines, which do backtrack, can take exponential time on innocent-looking input. That last point is a security topic too: it is the basis of the "regular expression denial of service" attacks.

By the end you can:

- define a DFA and an NFA, and explain what epsilon edges and nondeterminism mean;
- build the NFA of a regular expression with **Thompson's construction**, and say why it has at most about three states per character of the expression;
- match a string by tracking the **set of possible states**, in `O(n * m)` time with no backtracking;
- turn that into a **lazy DFA** that costs one table lookup per letter after warm-up, and explain when the number of DFA states explodes;
- show, with a count, how a backtracking matcher takes exponential time on `(a|aa)*c`, and name the fixes;
- write the two interview versions, **LeetCode 10** (`.` and `*`) and **LeetCode 44** (`?` and `*`), as dynamic programs;
- say what a finite automaton cannot do (balanced parentheses) and where that matters.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 1,220,018 checks passed under AddressSanitizer and UBSan (GCC 13.3). The tests ran on 20,000 random regular expressions over `a` and `b` (letters, `.`, `|`, `*`, `+`, `?`, groups nested two deep), each against 12 random strings, comparing the state-set matcher, the lazy DFA and the backtracking matcher with `std::regex` from the same compiler; on 5,000 more expressions with quantified groups inside quantified groups, where `std::regex` is too slow to serve as the referee, the three engines against each other; the DP of LeetCode 10 against the state-set matcher on 20,000 random patterns, and the DP of LeetCode 44 against it after translating `?` to `.` and `*` to `.*`; the known answers of both problems; parse errors; operator precedence; and the loop `(a*)*b`. Seven deliberate bugs (no repetition edge in `*`, `+` allowing zero, `.` not matching, no cycle guard, an inverted accept flag, a missing base case in the LeetCode 10 DP, and a missing edge in `|`) were each caught by the tests. One more finding from building them: `std::regex` took 3.9 seconds to reject the 7-letter string `bbbbbbb` against the 17-character expression `(a|b*(a?|..b)+)*a` (my backtracker needed 4,800 steps, the state-set matcher microseconds), and a longer random expression with groups nested three deep ran for over a minute on a short string, so I lowered the nesting depth of the expressions given to `std::regex` rather than wait.

**Data and timing.** Inputs are synthetic and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the **best of 3 repetitions, shown for run 1 / run 2** (one repetition for the three `std::regex` rows in section 7); they are this machine's, not yours. Counts and traces are the same in both runs. Statements about RE2, PCRE, Perl, grep and others are from memory and marked as such.

---

## 1. Automata: the machine behind the pattern

A **DFA** (deterministic finite automaton) has a fixed set of states, one **start** state, some **accepting** states, and for every state and every letter exactly one next state. To test a string you start at the start state, follow one edge per letter, and accept if you end in an accepting state. The cost is one table lookup per letter and the memory is just the state number: no stack, no backward moves.

You have already built DFAs. The KMP automaton of 12.01 has states `0 .. m` ("how many letters of the pattern match the end of the text") and accepts at `m`. The Aho-Corasick automaton of 12.04 has one state per trie node, and `hit` marks the accepting ones. Both recognise the language "strings that *end with* one of these words". A regular expression describes a larger family: any language you can build from single letters by **union** (`a|b`), **concatenation** (`ab`) and **repetition** (`a*`) is called **regular**, and a theorem of Kleene says that exactly the regular languages are the ones a finite automaton can recognise.

An **NFA** (nondeterministic finite automaton) relaxes one rule: a state may have several edges for the same letter, or **epsilon edges** that read no letter at all, and the machine accepts if *some* sequence of choices ends in an accepting state. "Nondeterministic" does not mean random; it means "all the choices are explored", conceptually in parallel. NFAs can be built directly from a regular expression (section 2) and DFAs are fast to run; the bridge between them is section 4.

## 2. Thompson's construction

Ken Thompson's 1968 construction (from memory) turns a regular expression into an NFA by gluing small pieces. Every piece is a **fragment** with one entry state and one exit state, and the rules are:

| Expression | Fragment |
|---|---|
| a letter `c` | `in --c--> out` |
| `.` | `in --any letter--> out` |
| `AB` (concatenation) | `A.out --eps--> B.in` |
| `A\|B` | a new `in` with epsilon edges to `A.in` and `B.in`; epsilon edges from `A.out` and `B.out` to a new `out` |
| `A*` | a new `in` and `out`; `in` to `A.in` and to `out`; `A.out` back to `A.in` and on to `out` |
| `A+` | as `A*` but with no edge from `in` straight to `out` (at least one pass) |
| `A?` | as `A*` but with no edge back from `A.out` to `A.in` (at most one pass) |

Each rule adds at most two new states, so the NFA is **linear in the size of the expression**. Operator precedence is the usual one: repetition binds tighter than concatenation, which binds tighter than `|`. The parser below is a recursive descent over exactly that grammar (`alt := cat ('|' cat)*`, `cat := rep*`, `rep := atom ('*'|'+'|'?')*`, `atom := letter | '.' | '(' alt ')'`), with an explicit error for a missing `)`, an unmatched `)` and a repetition with nothing to repeat. An empty concatenation (as in `a|` or `()`) matches the empty string.

```cpp
// Regular expressions over: a letter, '.', concatenation, '|', '*', '+', '?', and parentheses.
// Thompson's construction: every piece of the expression becomes a fragment with ONE start state and ONE end state,
// and the pieces are glued with epsilon edges (edges that read no letter).  A state has at most one letter edge and two epsilon edges.
struct Nfa {
    struct State { int ch = -2; int to = -1; int e1 = -1, e2 = -1; };   // ch: -2 no letter edge, -1 any letter ('.'), else the letter
    vector<State> s; int start = 0, accept = 0;
    int make() { s.emplace_back(); return (int)s.size() - 1; }
};
struct Frag { int in, out; };
struct Parser {                                  // alt := cat ('|' cat)* ; cat := rep* ; rep := atom ('*'|'+'|'?')* ; atom := letter | '.' | '(' alt ')'
    const string& p; size_t i = 0; Nfa& N;
    Parser(const string& pat, Nfa& n) : p(pat), N(n) {}
    void eps(int a, int b) { auto& x = N.s[a]; (x.e1 < 0 ? x.e1 : x.e2) = b; }
    Frag alt() {
        Frag f = cat();
        while (i < p.size() && p[i] == '|') {
            ++i; Frag g = cat();
            int in = N.make(), out = N.make();
            eps(in, f.in); eps(in, g.in); eps(f.out, out); eps(g.out, out);
            f = {in, out};
        }
        return f;
    }
    Frag cat() {
        int in = N.make(); Frag f = {in, in};             // an empty concatenation matches the empty string
        while (i < p.size() && p[i] != '|' && p[i] != ')') {
            Frag g = rep(); eps(f.out, g.in); f.out = g.out;
        }
        return f;
    }
    Frag rep() {
        Frag f = atom();
        while (i < p.size() && (p[i] == '*' || p[i] == '+' || p[i] == '?')) {
            char op = p[i++]; int in = N.make(), out = N.make();
            eps(in, f.in);
            if (op != '+') eps(in, out);                  // * and ? may skip the piece
            eps(f.out, out);
            if (op != '?') eps(f.out, f.in);              // * and + may repeat it
            f = {in, out};
        }
        return f;
    }
    Frag atom() {
        if (i >= p.size()) throw std::runtime_error("regex: unexpected end");
        char c = p[i++];
        if (c == '(') { Frag f = alt(); if (i >= p.size() || p[i] != ')') throw std::runtime_error("regex: missing )"); ++i; return f; }
        if (c == '*' || c == '+' || c == '?' || c == ')' || c == '|') throw std::runtime_error("regex: nothing to repeat");
        int in = N.make(), out = N.make();
        N.s[in].ch = c == '.' ? -1 : c - 'a'; N.s[in].to = out;
        return {in, out};
    }
};
inline Nfa compile(const string& pat) {
    Nfa N; Parser P(pat, N); Frag f = P.alt();
    if (P.i != pat.size()) throw std::runtime_error("regex: unmatched )");
    N.start = f.in; N.accept = f.out; return N;
}
```

The program prints the NFA of `a(b|c)*d`. A state with a letter edge shows `-letter->`; the others show their epsilon targets:

```
T1 a(b|c)*d: 15 states, start 0, accept 14: [0 eps 1] [1 -a-> 2] [2 eps 11] [3 eps 4] [4 -b-> 5] [5 eps 10] [6 eps 7] [7 -c-> 8] [8 eps 10] [9 eps 3,6] [10 eps 12,9] [11 eps 9,12] [12 eps 13] [13 -d-> 14]
```

Read it: state 1 reads `a`, state 4 reads `b`, state 7 reads `c`, state 13 reads `d`; the epsilon edges at 9, 10 and 11 build the loop for `*`, and state 14 is the accepting state. The sizes for a few expressions:

```
T8 states of the NFA against the length of the expression: [a: 3 vs 1] [ab: 5 vs 2] [a|b: 8 vs 3] [a*: 5 vs 2] [(a|b)*abb: 17 vs 9] [((a|b)*c)+d?: 20 vs 12]
```

My construction makes a fresh start state for every concatenation, so it uses a few more states than the textbook version; the tests check the bound of 3 states per character of the expression plus 2. The point is only that the size is linear. Precedence is a common source of bugs, so the program also checks `ab|c*` (it means "`ab`, or any number of `c`"):

```
T7 ab|c* means (ab) or (c*): ab 1, ccc 1, empty 1, abc 0, abcc 0
```

## 3. Matching without backtracking: keep the set of states

Here is the idea that avoids all exponential behaviour. At any moment the NFA could be in several states at once. Keep the **set** of all of them. To read a letter:

1. For every state in the set that has a letter edge matching the letter, add the target of that edge.
2. Add everything reachable from those targets through epsilon edges (the **closure**).

That gives the new set. The text is accepted if the final set contains the accepting state, and rejected early if the set becomes empty. Each set has at most `m` states (the NFA size), so a letter costs `O(m)` and the whole match costs **`O(n * m)`**, whatever the expression. No choice is ever undone, because no choice is ever made: all of them are followed together.

```cpp
// Matching WITHOUT backtracking: keep the SET of states the NFA could be in.  Each letter moves the whole set; cost O(states) per letter.
struct Sim {
    const Nfa& N; vector<int> mark; int stamp = 0;
    explicit Sim(const Nfa& n) : N(n), mark(n.s.size(), 0) {}
    void close(int v, vector<int>& set) {                 // add v and everything reachable by epsilon edges (an explicit stack, no recursion)
        vector<int> st{v};
        while (!st.empty()) {
            int u = st.back(); st.pop_back();
            if (mark[u] == stamp) continue;
            mark[u] = stamp; set.push_back(u);
            if (N.s[u].e1 >= 0) st.push_back(N.s[u].e1);
            if (N.s[u].e2 >= 0) st.push_back(N.s[u].e2);
        }
    }
    vector<int> start_set() { ++stamp; vector<int> r; close(N.start, r); return r; }
    vector<int> step(const vector<int>& cur, int c) {
        ++stamp; vector<int> nx;
        for (int u : cur) { const auto& x = N.s[u]; if (x.ch != -2 && (x.ch == -1 || x.ch == c)) close(x.to, nx); }
        return nx;
    }
    bool full_match(const string& t) {
        vector<int> cur = start_set();
        for (char ch : t) { cur = step(cur, ch - 'a'); if (cur.empty()) return false; }
        return std::find(cur.begin(), cur.end(), N.accept) != cur.end();
    }
};
```

The closure uses an explicit stack and a stamp array, so the cost of adding a state is constant and there is no deep recursion on a long expression. The program reads `aabb` against `(a|b)*abb`:

```
T2 (a|b)*abb has 17 NFA states; reading aabb, the set sizes are 9, 11, 11, 11, 10; accepting: yes
```

The set holds 9 states at the start (the closure of the start state), 11 after the first letter, and so on. The final set has the accepting state, so the string matches. The sizes are bounded by the 17 states of the NFA, and that bound, multiplied by the text length, is the cost.

*Searching* instead of matching the whole string is a one-line change: allow the match to start anywhere by adding the start state back into the set at every step (or compile `.*` in front of the expression). The state-set matcher then finds whether any substring matches in the same `O(n * m)`.

## 4. The lazy DFA: remember the sets

The state-set matcher recomputes the same set transitions again and again. Give each *set* a number, and remember the result of "set `S` on letter `c`": that is a DFA whose states are sets of NFA states, the **subset construction**. Building all of it up front can be huge (section 5), so build it **lazily**: compute a transition the first time the text needs it, store it in a table, and from then on it is a lookup.

```cpp
// The same matching with the sets turned into DFA states, built LAZILY: a DFA state is a sorted set of NFA states; a transition is
// computed the first time it is needed and then remembered.  After warm-up a letter costs one table lookup.
struct LazyDfa {
    const Nfa& N; Sim sim; std::map<vector<int>, int> id; vector<vector<int>> sets; vector<std::array<int, 26>> next; vector<char> acc;
    explicit LazyDfa(const Nfa& n) : N(n), sim(n) { state_of(sorted(sim.start_set())); }
    static vector<int> sorted(vector<int> v) { std::sort(v.begin(), v.end()); return v; }
    int state_of(const vector<int>& set) {
        auto it = id.find(set); if (it != id.end()) return it->second;
        int k = (int)sets.size(); id[set] = k; sets.push_back(set); next.emplace_back(); next.back().fill(-2);
        acc.push_back(std::binary_search(set.begin(), set.end(), N.accept)); return k;
    }
    int go(int d, int c) {
        if (next[d][c] == -2) { vector<int> nx = sim.step(sets[d], c); int k = state_of(sorted(nx)); next[d][c] = k; }
        return next[d][c];
    }
    size_t states() const { return sets.size(); }
    bool full_match(const string& t) {
        int d = 0;
        for (char ch : t) { d = go(d, ch - 'a'); if (sets[d].empty()) return false; }
        return acc[d];
    }
    // The number of DFA states reachable over the first sigma letters (eager exploration).
    size_t explore(int sigma) {
        for (size_t d = 0; d < sets.size(); ++d) for (int c = 0; c < sigma; ++c) go((int)d, c);
        return sets.size();
    }
};
```

On `(a|b)*abb`:

```
T3 lazy DFA of (a|b)*abb: 1 state after creation; after aabb (match) 4 states; after abababb (match) 4 states; after bbbb (no match) 5 states; all reachable over {a,b}: 5
```

The DFA starts with one state, creates three more while reading `aabb` (the table is filled only for the transitions the text used), and has 5 states when every transition has been explored. The textbook DFA for this language has 4 states; the lazy construction does not merge states that behave the same, which is what DFA **minimisation** (Hopcroft's algorithm, from memory) does, and I did not implement it.

**What it buys.** After warm-up a letter is one array lookup, like Aho-Corasick. Section 7 measures it: about 54 times faster than the state-set matcher on a 5-million-letter text.

## 5. When the DFA explodes

Subset construction can create up to `2^m` states from an NFA of `m` states. It really happens. Take the language "the `(k+1)`-th letter from the end is `a`", written `(a|b)*a(a|b)^k` with `(a|b)` repeated `k` times. To decide, a machine must remember the last `k + 1` letters, which is `2^(k+1)` possibilities. The program builds the NFA and explores every DFA state over the alphabet `{a, b}`:

```
T5 (a|b)*a(a|b)^k: k, NFA states, DFA states reachable: [k=1: 21, 5] [k=2: 29, 9] [k=3: 37, 17] [k=4: 45, 33] [k=5: 53, 65] [k=6: 61, 129] [k=7: 69, 257] [k=8: 77, 513] [k=9: 85, 1025] [k=10: 93, 2049] [k=11: 101, 4097] [k=12: 109, 8193]
```

The NFA grows by 8 states per step of `k`; the DFA doubles each time (`2^(k+1) + 1` states in every row; I did not check what the one extra state is, the start state being the likely candidate). At `k = 12`: 109 NFA states against 8,193 DFA states, and each extra step doubles it. No construction avoids this for this language: it is known that any DFA for it needs `2^(k+1)` states (from memory).

This is why the **lazy** construction matters in practice: real texts visit only a small fraction of the possible sets, and a lazy engine can also **cap the cache** and throw states away when it is full, falling back to the state-set matcher (this is how RE2 and similar engines work, from memory). An engine that builds the full DFA up front can run out of memory on an expression like the one above.

## 6. Backtracking, and where the exponential comes from

Most regular-expression libraries do not use the state sets. They explore one choice at a time: at a `|` try the left branch, and if the rest of the match fails, **go back** and try the right one; at a `*` try one more repetition, and on failure give it up. That is a depth-first search over the NFA, and it is natural to implement, it extends to features that no finite automaton can express (backreferences such as `(a*)\1`, and captured groups that must be reported), and on ordinary input it is fast. The matcher below is that search over the same NFA, with a step counter:

```cpp
// The way many library engines work: try one choice, and if the rest fails, go back and try the other.  `steps` counts the visits.
// `path` stops an epsilon cycle (such as (a*)*) from looping forever: a state already on the current path at the same text position is refused.
struct Backtrack {
    const Nfa& N; const string& t; vector<int> path; long long steps = 0;
    Backtrack(const Nfa& n, const string& text) : N(n), t(text), path(n.s.size(), -1) {}
    bool go(int v, size_t i) {
        ++steps;
        if (v == N.accept && i == t.size()) return true;
        if (path[v] == (int)i) return false;
        int old = path[v]; path[v] = (int)i; bool ok = false;
        const auto& x = N.s[v];
        if (x.e1 >= 0 && go(x.e1, i)) ok = true;
        else if (x.e2 >= 0 && go(x.e2, i)) ok = true;
        else if (x.ch != -2 && i < t.size() && (x.ch == -1 || x.ch == t[i] - 'a') && go(x.to, i + 1)) ok = true;
        path[v] = old; return ok;
    }
    bool run() { return go(N.start, 0); }
};
```

(The `path` array breaks epsilon cycles. Without it the expression `(a*)*b` loops forever, because the inner `*` can match the empty string again and again. The tests include that expression.)

Now the trouble. Take `(a|aa)*c` and a text of `n` letters `a`. The string cannot match (there is no `c`), but the matcher does not know that until it has tried every way of cutting the `a`'s into pieces of 1 and 2, and the number of ways is a Fibonacci number. The program counts the visits:

```
T4 (a|aa)*c against n letters a (no match is possible): backtracking visits n=10: 2374 n=20: 293324 n=30: 36077809 n=36: 647390201; the state-set method does at most 15 state visits per letter
```

Each ten extra letters multiply the work by about 120 (the ratio between consecutive Fibonacci numbers is 1.62 per letter). The state-set method does the same job in at most `n` times the 15 NFA states. On the clock (section 7): 36 million visits take 108 ms at `n = 30`, and 689 ms at `n = 34`; each four letters multiply the time by about six.

This is the vulnerability known as **ReDoS** (regular expression denial of service): if a server runs a user-supplied text through a backtracking engine with an expression like `(a+)+$`, a short crafted input can occupy a processor for minutes. I know it as a well-documented class of bugs in web applications and network security products (from memory). Remedies, in order of strength:

1. **Use a non-backtracking engine** (state sets or a lazy DFA) wherever the expression does not need backreferences. RE2 and Rust's `regex` crate are built this way, as I remember it; the price is that they do not support backreferences.
2. **Memoise** the pair (state, text position): a pair that failed once will fail again. With that, the backtracker visits each pair at most once, which is `O(n * m)`, and it has become the state-set method with another name.
3. **Limit** the number of steps, or the time, and fail the request.
4. **Rewrite** the expression so that no two parts can match the same letters (for example `a+` instead of `(a|aa)+`).

## 7. Measured costs

The expression `(a|b)*abb` on random text over `a` and `b`, and the expression `(a|aa)*c` as the bad case.

| Case | Time, ms (run 1 / run 2) |
|---|---|
| State-set matcher, 5,000,000 random letters | 905.5 / 980.5 |
| Lazy DFA, the same | 17.9 / 17.9 |
| State-set matcher, 2,000 letters | 0.4 / 0.4 |
| Lazy DFA, 2,000 letters (after the 5 million letters had filled the table) | 0.0 / 0.0 |
| `std::regex_match`, 2,000 letters (one repetition) | 0.1 / 0.1 |
| `(a|aa)*c`, backtracking matcher, 30 letters `a` | 116.3 / 122.0 |
| The same, 34 letters | 800.7 / 875.5 |
| `(a|aa)*c`, state-set matcher, 1,000,000 letters `a` | 202.0 / 202.4 |
| The same, lazy DFA | 2.8 / 3.6 |
| `(a|aa)*c`, `std::regex_match`, 18 letters `a` | 0.2 / 0.3 |
| The same, 22 letters | 1.9 / 2.1 |
| The same, 26 letters | 14.6 / 14.7 |

- The lazy DFA is about **54 times faster** than the state-set matcher on the large random text (about 3.6 ns against 194 ns a letter), and on the bad case a million letters take 3.6 ms against 171 ms. The DFA's cost per letter does not depend on the size of the expression after warm-up.
- On **ordinary short input a backtracking engine is not slow**: `std::regex` took 0.1 ms on 2,000 letters, less than my state-set matcher, because the greedy first choice succeeds or fails after a few steps. The danger of backtracking is not its average speed but that its worst case has no bound.
- `std::regex` is a backtracking engine and **grows exponentially** on the bad case: 0.3, 2.2 and 15.2 ms for 18, 22 and 26 letters, a factor of about 7 per four letters, the same rate as my counter. If that rate continued, 50 letters would take roughly half an hour (an extrapolation, not a measurement). This is the libstdc++ implementation; I did not test other libraries.
- `std::regex_match` is also **recursive**: my first version of this test used a 20,000-letter text and the program crashed with a segmentation fault, most likely stack exhaustion (I did not confirm it); I reduced the input to 2,000 letters. A matcher that recurses once per input letter is not safe on a long text.

## 8. The interview versions: LeetCode 10 and 44

Two problems ask you to match with a tiny pattern language. They are dynamic programs over (text position, pattern position), the same shape as edit distance.

**LeetCode 10, regular expression matching.** `.` is any letter and `x*` means "zero or more `x`" (a `*` always follows a letter or a dot). Let `dp[i][j]` mean "the first `i` letters of the text match the first `j` letters of the pattern". If `p[j-1]` is a letter or a dot: `dp[i][j] = dp[i-1][j-1]` and the letters agree. If it is a star: either use the starred item zero times, `dp[i][j-2]`, or once more, `dp[i-1][j]` when `p[j-2]` matches `s[i-1]`. The base row needs care: `dp[0][j]` is true for patterns like `a*b*` that can match the empty text, which is the line the deliberately broken version leaves out.

**LeetCode 44, wildcard matching.** `?` is any one letter and `*` is any string, also empty. The rows: `*` matches nothing (`dp[i][j-1]`) or swallows one more letter (`dp[i-1][j]`).

```cpp
// The two classic interview versions, by dynamic programming over (position in the text, position in the pattern).
// LeetCode 10: '.' is any letter, 'x*' is zero or more x (a '*' always follows a letter or '.').
inline bool lc10(const string& s, const string& p) {
    int n = (int)s.size(), m = (int)p.size();
    vector<vector<char>> dp(n + 1, vector<char>(m + 1, 0)); dp[0][0] = 1;
    for (int j = 2; j <= m; ++j) if (p[j - 1] == '*') dp[0][j] = dp[0][j - 2];
    for (int i = 1; i <= n; ++i)
        for (int j = 1; j <= m; ++j) {
            if (p[j - 1] == '*') dp[i][j] = dp[i][j - 2] || ((p[j - 2] == '.' || p[j - 2] == s[i - 1]) && dp[i - 1][j]);
            else dp[i][j] = (p[j - 1] == '.' || p[j - 1] == s[i - 1]) && dp[i - 1][j - 1];
        }
    return dp[n][m];
}
// LeetCode 44: '?' is any one letter, '*' is any string (also empty).
inline bool lc44(const string& s, const string& p) {
    int n = (int)s.size(), m = (int)p.size();
    vector<vector<char>> dp(n + 1, vector<char>(m + 1, 0)); dp[0][0] = 1;
    for (int j = 1; j <= m; ++j) dp[0][j] = dp[0][j - 1] && p[j - 1] == '*';
    for (int i = 1; i <= n; ++i)
        for (int j = 1; j <= m; ++j) {
            if (p[j - 1] == '*') dp[i][j] = dp[i][j - 1] || dp[i - 1][j];
            else dp[i][j] = (p[j - 1] == '?' || p[j - 1] == s[i - 1]) && dp[i - 1][j - 1];
        }
    return dp[n][m];
}
```

The known answers:

```
T6 LeetCode 10: (aa,a) 0; (aa,a*) 1; (ab,.*) 1; (aab,c*a*b) 1; (mississippi,mis*is*p*.) 0. LeetCode 44: (aa,*) 1; (cb,?a) 0; (adceb,*a*b) 1; (acdcb,a*c?b) 0
```

Both are `O(n * m)` time. They are also special cases of the NFA machinery: LeetCode 10 is the `.` and `*` fragment of Thompson's construction, and wildcard matching is the expression you get by replacing `?` with `.` and `*` with `.*`. The tests use exactly this: the DP and the state-set matcher agree on 20,000 random patterns of each kind. For LeetCode 44 there is also a greedy two-pointer solution that runs in `O(n)` space and, with backtracking only to the last star, `O(n * m)` time; I describe it from memory and did not write it here.

## 9. What a finite automaton cannot do

A DFA has a fixed number of states, so it cannot count without bound. The language of balanced parentheses, or `a^n b^n`, is **not regular**: to accept `a^n b^n` the machine must remember `n`, and no fixed number of states can remember all `n`. The pumping lemma (from memory) makes that precise. So "match balanced parentheses with a regular expression" is impossible in the pure sense, and the engines that seem to do it (recursive patterns in PCRE, balancing groups in .NET) are using features beyond regular expressions. Use a stack ([Stacks and queues 04](../05_stacks_and_queues/04_expression_evaluation_and_parsing.md)) for those.

What regular languages *do* have is closure: union, concatenation, repetition, **complement** (swap the accepting states of a complete DFA), and **intersection** (run two DFAs side by side on pairs of states). That is why a "match this *and* that" or "does not contain this" check can be built from DFAs, and why Aho-Corasick states can be reused in a counting DP, as in 12.04.

Regular expressions are also the engine inside **lexers**: a tool in the lex family (from memory) compiles all the token patterns into one DFA, and the longest match wins.

## 10. Complexities

| Operation | Time | Space |
|---|---|---|
| Compile an expression of length `m` to an NFA (Thompson) | `O(m)` | `O(m)` states |
| Match by sets of states, text of length `n` | `O(n * m)` | `O(m)` |
| Lazy DFA, per letter after warm-up | `O(1)` | up to `2^m` states, `O(sigma)` each |
| Lazy DFA, a new transition | `O(m)` (one set step) plus a map lookup | |
| Backtracking | exponential in the worst case, fast on typical input | `O(n)` stack |
| Backtracking with memoisation | `O(n * m)` | `O(n * m)` |
| LeetCode 10 and 44 by DP | `O(n * m)` | `O(n * m)`, or `O(m)` with two rows |

## 11. Which tool

- A **fixed set of words**: Aho-Corasick (12.04); a regular expression engine is overkill.
- A pattern with choices and repetition, **no backreferences**, input you do not control: a non-backtracking engine (state sets, a lazy DFA, or a library built on them).
- **Backreferences or lookaround** needed: a backtracking engine, with a step or time limit.
- **Interview** matching problems with `?`, `*`, `.`: the DP of section 8; know the NFA picture as the explanation.
- A **state machine for a protocol or a parser** (a number format, a command syntax): write the DFA by hand as a table; it is the shortest and safest code.
- A **nested structure** (parentheses, JSON, XML): not regular; use a stack or a parser.

## 12. Common mistakes

1. **Forgetting the epsilon closure**, or doing it before the letter step instead of after. The set of states must always be closed.
2. **Recursing in the closure**, which overflows the stack on a long expression; use a stack or a loop.
3. **No guard against epsilon cycles** in a backtracker: `(a*)*` loops forever.
4. **Wrong precedence**: `ab|c*` is `(ab)|(c*)`, not `a(b|c)*`.
5. **Treating the empty alternative or empty group as an error** when the language allows it (`a|` and `()` match the empty string).
6. **Building the whole DFA up front** for an expression that can need `2^m` states.
7. **Believing a backtracking engine is safe because it is fast on your tests.** The bad inputs are specific and short (section 6).
8. **Using a backtracking engine on user-supplied expressions or input** without a time limit.
9. **LeetCode 10: reading `*` as a wildcard** (as in LeetCode 44). In 10, `*` repeats the item before it.
10. **LeetCode 10: forgetting `dp[0][j]`** for patterns that match the empty string (`a*b*`).
11. **LeetCode 44: forgetting that `*` also matches the empty string.**
12. **Matching a substring when the problem asks for the whole string**, or the reverse. The state-set matcher here is full-match; for search, add the start state at each step.

## 13. Questions to answer out loud

1. What is the difference between a DFA and an NFA? What is an epsilon edge?
2. Which languages do finite automata recognise, and why can they not match balanced parentheses?
3. List the Thompson fragments for a letter, concatenation, `|`, `*`. Why is the NFA linear in the expression?
4. How do you match with sets of states? What is the cost, and why does no backtracking happen?
5. What is the subset construction? What does "lazy" change?
6. Give a language whose DFA has exponentially more states than its NFA.
7. Why does `(a|aa)*c` take exponential time in a backtracking engine? How many visits for `n` letters `a`?
8. Name four ways to defend against ReDoS.
9. Why are backreferences hard for a non-backtracking engine?
10. Write the recurrence for LeetCode 10, including the base row.
11. Write the recurrence for LeetCode 44. How does it differ from 10?
12. How do you turn a DFA into one that accepts the complement language?

## 14. Problems

### 14.1 On paper

Answer first, then compare with the answers at the bottom.

**A.** How many states does the NFA of `ab*` have in the construction used here?

**B.** Does `(a|b)*c` match `abbac`, `c`, and `abca`?

**C.** Does LeetCode 10 match the text `ab` with the pattern `a*b*`, and the text `aaa` with `ab*a*c*a`?

**D.** Does LeetCode 44 match `abcd` with `a*d`, and with `a?d`?

**E.** How many visits does the backtracking matcher make on `(a|aa)*c` with 12 letters `a`?

**F.** Draw a DFA, by hand, for binary strings with an even number of `1`s. How many states?

### 14.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| Regular Expression Matching | 10 | LeetCode (hard) | The DP of section 8; mind the `dp[0][j]` row |
| Wildcard Matching | 44 | LeetCode (hard) | The DP, or the greedy with a remembered star position |
| Valid Number | 65 | LeetCode (hard) | Draw the DFA (sign, digits, dot, exponent) and code it as a state table |
| String to Integer (atoi) | 8 | LeetCode | A four-state DFA: start, sign, number, end |
| Is Subsequence | 392 | LeetCode | Two pointers; a DFA of "next occurrence of each letter" when there are many queries |
| Number of Matching Subsequences | 792 | LeetCode | Bucket the words by the next letter they wait for and advance them as the text is read (many small NFAs run together) |
| Check if Binary String Has at Most One Segment of Ones | 1784 | LeetCode | A three-state DFA, or a check for the substring `01` followed by a `1` |

### 14.3 In your editor

**A.** Extend the parser with character classes `[abc]` and the repetition `{m,n}` (expand it into copies, and note the size growth).

**B.** Add the **unanchored** search (match anywhere) and compare the lazy DFA with `std::string::find` for a fixed pattern on a large text.

**C.** Implement **DFA minimisation** (partition refinement, or Hopcroft's algorithm) and check that the DFA for `(a|b)*abb` shrinks from 5 states to 4.

**D.** Make the backtracker **memoise** the pair (state, position) and show that the visit count for `(a|aa)*c` becomes linear in `n`.

**E.** Build the **complement** and the **intersection** of two lazy DFAs and use them to match "contains `ab` but not `ba`".

### Answers

The program that checks the paper problems printed:

```
A 7
B 1 1 0
C 1 1
D 1 0
E 6233
```

Reading it line by line:

**A.** `7` states in this construction (two for each letter fragment, two more for the `*`, and one start state for the concatenation). Other constructions give a slightly different number; the point is that it is a small multiple of the expression length.

**B.** `1 1 0`: `abbac` and `c` match, `abca` does not (it ends with `a`, not `c`).

**C.** `1 1`: `a*b*` matches `ab`; in `ab*a*c*a` the `b*` and `c*` repeat zero times, the first `a` takes one letter, the `a*` takes one more and the last `a` takes the third.

**D.** `1 0`: `a*d` matches (the star takes `bc`); `a?d` needs exactly three letters.

**E.** `6233`. For comparison, the count at 10 letters is 2,374 and at 20 letters 293,324.

**F.** Two states, "even" (start and accepting) and "odd"; each `1` swaps them and each `0` stays.

## Cheat sheet

- **DFA:** one next state per (state, letter); one lookup per letter. **NFA:** several choices and epsilon edges; accept if some path accepts.
- **Thompson:** letter, concatenation, `|`, `*`, `+`, `?` each add at most two states; the NFA is linear in the expression.
- **State-set matching:** keep the set of reachable states, close it under epsilon edges, `O(n * m)`, no backtracking.
- **Lazy DFA:** cache the set transitions; one lookup per letter after warm-up; the number of states can reach `2^m`.
- **Backtracking:** try, fail, go back. Fast on typical input, exponential on `(a|aa)*c`; fixes: a non-backtracking engine, memoisation, limits, rewriting.
- **LeetCode 10:** `dp[i][j]`, star = zero uses (`dp[i][j-2]`) or one more (`dp[i-1][j]`); base row for empty text. **LeetCode 44:** star = `dp[i][j-1]` or `dp[i-1][j]`.
- **Not regular:** balanced parentheses, `a^n b^n`. Use a stack.

---

## Sources

- All code is tested here (1,220,018 checks, GCC 13.3, sanitizers on); counts, traces and timings are measured on one Xeon container, two runs each (one run for the three `std::regex` rows). Times are this machine's.
- Thompson, "Regular expression search algorithm" (1968), the subset construction (Rabin and Scott, 1959), Hopcroft's minimisation, Kleene's theorem and the pumping lemma are standard textbook results, described here from memory. The description of RE2, Rust's `regex`, PCRE, lex, and ReDoS as a class of bugs is from memory and was not checked against their documentation. Russ Cox's article "Regular Expression Matching Can Be Simple And Fast" (2007, from memory) is the well-known explanation of section 6 and worth reading.
- The explanations and code are my own. LeetCode numbers and titles are from memory; check a number before you rely on it.
