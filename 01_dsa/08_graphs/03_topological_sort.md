# DSA 08.03: Topological Sort

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Graph representations and traversal](01_representations_and_traversal.md) (CSR, BFS, iterative DFS and finish times) and [Connectivity and cycles](02_connectivity_and_cycles.md) (the three colours, and returning a directed cycle). There is no companion demo file: every structure is listed in this document.*

A lot of real work is "do these things, but this one first": compile a file after the headers it includes, take a course after its prerequisites, run a migration after the one it depends on, install a package after its dependencies, evaluate a spreadsheet cell after the cells it reads. All of them are the same question on a directed graph: **find an order of the vertices in which every arc points forwards**, or report that none exists because the dependencies go in a circle. That order is a **topological order**, and there are two classic ways to get it. One peels off vertices that have nothing left to wait for (Kahn). The other reads a depth-first search backwards. They give different answers, they fail differently, and each has a variant the other lacks: Kahn gives you the *rounds* (what can run in parallel) and the *lexicographically smallest* order almost for free, while the DFS version is the one that falls out of a search you already wrote.

By the end you can:

- write Kahn's algorithm and the reverse-postorder DFS, prove both, and say which cycle information each one gives when no order exists;
- turn the same Kahn loop into: the lexicographically smallest order (a heap), the **number of rounds** needed when independent tasks run in parallel (the critical path), and a test for whether the order is **unique**;
- count topological orders by dynamic programming over subsets, and say how fast their number collapses as arcs are added (measured);
- get the arrow direction right in "course schedule", "alien dictionary" and build-system problems, which is where most wrong answers come from;
- compute the minimal rebuild after a file changes;
- say when recursion depth makes the DFS version a bug (measured).

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 31,402 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: both algorithms against **every permutation** for graphs of up to 7 vertices, with and without cycles (valid order iff no cycle, the number of valid permutations against the subset DP, the smallest-first order against the first valid permutation in lexicographic order, the unique flag against "exactly one valid permutation", the levels against longest-path relaxation); the stuck set of a cyclic graph against a transitive closure ("on a cycle or reachable from one"); every returned cycle walked arc by arc; the rebuild set against reachability and its order against the arcs; the alien dictionary against a search over all alphabets of up to 5 letters, on both sorted and random word lists, with prefix and cycle cases; and chains, rings, stars and a random acyclic graph of a million vertices, where nothing may recurse. Then 52 hand-made mutants of the listings were run against the tests: **49 were caught and 3 are equivalent changes** (section 9).

**Data and timing.** Graphs are **synthetic** and seeded: a random acyclic graph with V = 1,000,000 and 4 million arcs (a random permutation of the vertices fixes the order, and each arc goes from the earlier to the later endpoint of a random pair). Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and exit codes are identical in both runs.

---

## 1. Vocabulary

A **topological order** of a directed graph is a listing of all its vertices in which, for every arc `a -> b`, `a` comes before `b`. It exists if and only if the graph is a **DAG** (directed acyclic graph). The **in-degree** of a vertex is the number of arcs entering it; a **source** has in-degree 0, a **sink** has out-degree 0. The **level** of a vertex is the length of the longest path ending at it (sources are level 0). A DAG usually has many topological orders; how many is itself a question (section 4).

Before any code, fix the **direction convention**, because it is the commonest source of wrong answers: *an arc `a -> b` means `a` must come before `b`.* Problems state dependencies the other way round. LeetCode's `prerequisites[i] = [a, b]` says "to take `a` you must first take `b`", so the arc is `b -> a`. A build rule "`app` needs `lib`" is an arc `lib -> app`. If your output is the reverse of what you expected, you wrote the arcs backwards; reversing the final list is a fix that works by accident and breaks the first time the problem asks for something else (the smallest-first order, say, is *not* the reverse of the largest-first one in general).

Two facts used throughout. **Every finite DAG has a source:** start anywhere and walk backwards along arcs; if you never reached a vertex with no incoming arc you would walk forever in a finite graph, so a vertex repeats, and that is a cycle. By the same argument every finite DAG has a sink. **A cycle can never be ordered:** if `a` reaches `b` and `b` reaches `a`, each must come before the other.

## 2. Kahn's algorithm

The idea is to do what you would do by hand. Take a task with no unfinished prerequisite, do it, cross it off every other task's list, repeat. Keep, for each vertex, the number of arcs into it that have not yet been "used up" (`indeg`). A vertex whose count reaches 0 is released. That is all.

```cpp
using Edges = std::vector<std::pair<int, int>>;
// Directed graph in compressed rows (Topic 01): the arcs out of u are to[off[u]] .. to[off[u + 1] - 1].
// An arc a -> b reads "a must come before b".
struct Csr { int n = 0; std::vector<int> off, to; };
Csr build_csr(int n, const Edges& e) {
    Csr g; g.n = n; g.off.assign(n + 1, 0);
    for (auto [a, b] : e) ++g.off[a + 1];
    for (int u = 0; u < n; ++u) g.off[u + 1] += g.off[u];
    g.to.resize(e.size()); std::vector<int> at(g.off.begin(), g.off.end() - 1);
    for (auto [a, b] : e) g.to[at[a]++] = b;
    return g;
}
```

```cpp
// Kahn's algorithm: repeatedly take a vertex with no unfinished prerequisite. indeg[v] counts the arcs into v that are not yet "used up".
// ok = false means a cycle: the vertices never released (stuck) are exactly those ON a cycle or reachable from one.
// level[v] = the length of the longest path ending at v (0 for a source); only meaningful when ok.
// unique = at every step exactly one vertex was available, so the order is the only one.
// smallest_first replaces the queue by a min-heap, giving the lexicographically smallest order.
struct TopoResult { bool ok = true, unique = true; std::vector<int> order, level, stuck; };
TopoResult kahn(const Csr& g, bool smallest_first = false) {
    int n = g.n; std::vector<int> indeg(n, 0), q; std::priority_queue<int, std::vector<int>, std::greater<int>> heap;
    TopoResult r; r.level.assign(n, 0);
    for (int i = 0; i < g.off[n]; ++i) ++indeg[g.to[i]];
    auto release = [&](int v) { if (smallest_first) heap.push(v); else q.push_back(v); };
    for (int v = 0; v < n; ++v) if (indeg[v] == 0) release(v);
    size_t head = 0;
    auto waiting = [&] { return smallest_first ? heap.size() : q.size() - head; };
    while (waiting()) {
        if (waiting() > 1) r.unique = false;
        int u; if (smallest_first) { u = heap.top(); heap.pop(); } else u = q[head++];
        r.order.push_back(u);
        for (int i = g.off[u]; i < g.off[u + 1]; ++i) {
            int v = g.to[i]; r.level[v] = std::max(r.level[v], r.level[u] + 1);
            if (--indeg[v] == 0) release(v);
        }
    }
    if ((int)r.order.size() < n) { r.ok = false; r.unique = false; for (int v = 0; v < n; ++v) if (indeg[v] > 0) r.stuck.push_back(v); }
    return r;
}
// Parallel courses: the fewest rounds if everything available may run at once = (longest path in vertices). -1 on a cycle.
int semesters(const Csr& g) {
    TopoResult r = kahn(g); if (!r.ok) return -1;
    int best = 0; for (int l : r.level) best = std::max(best, l + 1); return best;
}
```

**Why it is correct.** Each vertex is released exactly when its last prerequisite has been output, so when it is output every arc into it already points backwards: the output is a valid order. If the graph is a DAG the loop cannot stall, because the vertices not yet output form a DAG, which has a source (section 1), and a source of that remainder has all its prerequisites output and so has been released. If there is a cycle, no vertex of it is ever released (each one waits for another on the cycle), so the output is shorter than V. That is the whole cycle test, with no colours.

**What a failure tells you.** When `ok` is false, `stuck` lists the vertices still waiting: exactly those **on a cycle or reachable from one** (a vertex downstream of a cycle waits for something that never finishes). It is not the cycle itself. To get the cycle use the DFS version of section 3, or walk backwards from any stuck vertex through arcs whose tail is also stuck until a vertex repeats.

**Levels and rounds.** Because a vertex is released after *all* its prerequisites, `level[v] = max over arcs u -> v of level[u] + 1` is the length of the longest chain that must precede it. All vertices of one level can run at the same time, and `semesters` returns the number of levels: the fewest rounds if unlimited tasks run in parallel. This is "Parallel Courses" and the critical path of a project plan. The same loop with weights on the vertices gives the earliest finish time of each task (problem I).

**Uniqueness.** If at some step two vertices are waiting in the queue, either may go first and the order is not unique. If at every step exactly one is waiting, the order is forced. `unique` records that, in O(1) extra work. It is the heart of "Sequence Reconstruction": a sequence is uniquely reconstructible iff the topological order of its constraints is unique and equals it.

**Smallest first.** Replace the queue by a min-heap and the vertex taken at each step is the smallest *available* one. This produces the lexicographically smallest topological order, and the proof is the usual exchange argument: any valid order that starts with a larger available vertex can be improved by moving the smaller one to the front, because nothing before it in the order is one of its prerequisites. It costs a factor log(available) per vertex; section 7 measures about 1.8 for the whole run. (The *largest* order is the same with a max-heap. It is not the reverse of the smallest one.)

**Trace.** The classic dressing example: vertices 0 shirt, 1 tie, 2 jacket, 3 belt, 4 trousers, 5 shoes, 6 socks, arcs `0->1 1->2 0->3 3->2 4->3 4->5 6->5`.

```text
kahn order [0 4 6 1 3 5 2]
kahn level [0 1 2 1 0 1 0]
kahn unique 0
heap order [0 1 4 3 2 6 5]
dfs order [6 4 5 0 3 1 2]
semesters 3
orders 91
```

Kahn starts with the three sources `0 4 6` in the queue. Taking 0 releases the tie (1); taking 4 releases the belt (3), whose other prerequisite (0) is already done; taking 6 releases the shoes (5). Then 1, 3, 5, and finally the jacket (2), which had been waiting for both the tie and the belt. So the order is `0 4 6 1 3 5 2`. The levels `0 1 2 1 0 1 0` give 3 rounds (`semesters`): trousers, shirt and socks first; then tie, belt and shoes; then the jacket. The DFS of section 3 gives `6 4 5 0 3 1 2`. The order is not unique (three vertices were waiting at the start), the heap gives `0 1 4 3 2 6 5`, and 91 topological orders exist.

## 3. Depth-first search: reverse postorder

Run a depth-first search over the whole graph and write each vertex down **when it finishes**. The list reversed is a topological order. This is the same search as the directed cycle test of Topic 02 with one extra line.

```cpp
// The other way: reverse postorder. Depth-first search, three colours (Topic 02); a vertex is appended when it FINISHES, and the
// finish order reversed is a topological order. An arc into a grey vertex is a cycle, returned as in Topic 02.
struct DfsTopo { bool ok = true; std::vector<int> order, cycle; };
DfsTopo topo_dfs(const Csr& g) {
    int n = g.n; std::vector<char> color(n, 0); std::vector<int> pos(n, -1), post; std::vector<std::pair<int, int>> st; DfsTopo r;
    for (int s = 0; s < n; ++s) {
        if (color[s]) continue;
        color[s] = 1; pos[s] = 0; st.push_back({s, g.off[s]});
        while (!st.empty()) {
            int u = st.back().first; int& i = st.back().second;
            if (i == g.off[u + 1]) { color[u] = 2; post.push_back(u); st.pop_back(); continue; }
            int v = g.to[i++];
            if (color[v] == 0) { color[v] = 1; pos[v] = st.size(); st.push_back({v, g.off[v]}); }
            else if (color[v] == 1) { r.ok = false; for (size_t k = pos[v]; k < st.size(); ++k) r.cycle.push_back(st[k].first); return r; }
        }
    }
    r.order.assign(post.rbegin(), post.rend()); return r;
}
```

**Why it is correct.** Consider an arc `u -> v`. When the search looks at it, `v` is either white (it becomes a descendant of `u` and finishes first), black (it has already finished, so before `u`), or grey (a cycle, which we report). In the first two cases `v` finishes before `u`, so `v` comes *later* in the reversed list: the arc points forwards. Nothing else can happen, which is the whole proof. Note what it does not say: the postorder is not unique (it depends on the vertex and arc order), and, unlike Kahn's, the first vertex of the reversed list is the root of the *last* search tree (necessarily a source, since every arc points forwards).

**Cycles.** Here you do get the cycle itself, as in Topic 02: on an arc into a grey vertex, the stack from that vertex to the top. In the cyclic example below it is `[1 2]`, found while Kahn only reports that `1 2 3` are stuck.

**Kahn or DFS?** Both are O(V + E), and the measurement of section 7 does not separate them in speed. Reach for **Kahn** when you need rounds, the smallest order, uniqueness, a streaming/online variant (you can release vertices as they become ready), or a cycle test with no recursion state. Reach for **DFS** when you already have a search (strongly connected components, Topic 08, builds on the same finish times) or when you want the cycle itself. They really do disagree: on the diamond `3->1 3->2 1->0 2->0` Kahn gives `3 1 2 0` and the DFS gives `3 2 1 0`, both valid.

**Cyclic example.** Arcs `0->1 1->2 2->1 2->3 4->0`: the cycle `1 2`, a head `4 0` before it, and a tail `3` after it.

```text
cyclic kahn ok 0
cyclic order [4 0]
cyclic stuck [1 2 3]
cyclic dfs ok 0
cyclic cycle [1 2]
```

Kahn outputs `4 0` and gets stuck: `1` waits for `2`, `2` waits for `1`, and `3` waits for `2`. The stuck set `[1 2 3]` contains the cycle and its downstream. The DFS from 0 reaches 1, 2 and then sees the arc `2 -> 1` into grey 1: the cycle is `[1 2]`, the stack from 1. Vertex 3 is not on it.

## 4. How many topological orders are there?

For small graphs you can count them exactly by dynamic programming over subsets: `dp[S]` is the number of ways to place exactly the vertices of `S` first, and a vertex `v` can be placed after `S` when all its prerequisites are in `S`.

```cpp
// The number of topological orders, by dynamic programming over subsets (n <= 20): dp[S] = the number of ways to place exactly the
// vertices of S first. A vertex v may be placed after S when all its prerequisites are in S. 20! still fits in a 64-bit integer.
long long count_orders(int n, const Edges& e) {
    std::vector<int> need(n, 0); for (auto [a, b] : e) need[b] |= 1 << a;        // a self-loop makes need[v] contain v itself: never satisfiable
    std::vector<long long> dp(size_t(1) << n, 0); dp[0] = 1;
    for (size_t S = 0; S < dp.size(); ++S) {
        if (!dp[S]) continue;
        for (int v = 0; v < n; ++v) if (!(S >> v & 1) && (need[v] & S) == (unsigned)need[v]) dp[S | size_t(1) << v] += dp[S];
    }
    return dp.back();
}
```

It runs in O(2^n n) time and O(2^n) space, so n up to about 20 (the 8-byte counters hold 20! = 2,432,902,008,176,640,000). Counting the orders of a general DAG is a #P-complete problem, which is why there is no polynomial formula to hope for; I state that from memory. The counting matters in practice for one reason: it tells you how much freedom a scheduler has. Section 7 shows how quickly it vanishes.

The dressing example of section 2 has 91 orders (the tests enumerate every permutation of graphs of this size and agree with the DP).

## 5. Applications

### 5.1 Course schedule (and "can finish")

Courses `0 .. n-1`, prerequisites `[a, b]` meaning "`b` before `a`". Build the arcs `b -> a`, run `kahn`, and *can finish* is `ok`; the order is the answer to "Course Schedule II"; `semesters` is "Parallel Courses". Three things to remember: duplicates in `prerequisites` produce parallel arcs, which the in-degree counting handles correctly; a course that requires itself is a self-loop, which is a cycle; and courses with no prerequisites and no dependants are sources and appear in the output (they are part of the order).

### 5.2 Alien dictionary

You are given words sorted in an unknown alphabet and must recover an order of the letters. Compare each pair of **neighbouring** words: the first position where they differ gives one fact, "this letter before that letter", and nothing else about the pair can be concluded (later letters of the pair say nothing). Collect those arcs and sort the letters topologically.

```cpp
// Alien dictionary: the words are sorted in an unknown alphabet. The first difference between neighbouring words is an arc
// "this letter before that letter". The answer is a topological order of the letters that occur (smallest letter first, so it is
// reproducible); there is none when the arcs form a cycle, or when a longer word comes before its own prefix ("abc" before "ab").
struct Alien { bool ok = false; std::string order; };
Alien alien_order(const std::vector<std::string>& w) {
    bool seen[26] = {}; for (auto& s : w) for (char c : s) seen[c - 'a'] = true;
    Edges e;
    for (size_t i = 0; i + 1 < w.size(); ++i) {
        const std::string &a = w[i], &b = w[i + 1]; size_t k = 0;
        while (k < a.size() && k < b.size() && a[k] == b[k]) ++k;
        if (k == std::min(a.size(), b.size())) { if (a.size() > b.size()) return {}; continue; }
        e.push_back({a[k] - 'a', b[k] - 'a'});
    }
    TopoResult r = kahn(build_csr(26, e), true); if (!r.ok) return {};
    Alien out; out.ok = true; for (int v : r.order) if (seen[v]) out.order += char('a' + v);
    return out;
}
```

Three traps are visible in the code. **A prefix comes first:** if `"abc"` precedes `"ab"` no alphabet can explain it, so the answer is "none"; `"ab"` before `"abc"` gives no arc at all and is fine. **Letters that appear but have no arcs** must still appear in the answer, which is why `seen` is separate from the arcs. **Repeated pairs** yield parallel arcs, which is harmless. The smallest-first heap makes the output reproducible (any topological order is a valid answer to the original problem). A cycle among the arcs, such as `"z", "x", "z"` (z before x before z), returns none.

```text
alien ok 1 order wertf
alien prefix ok 0
alien cycle ok 0
```

The standard example `wrt wrf er ett rftt` gives the arcs `t->f`, `w->e`, `r->t`, `e->r` and the order `wertf`.

### 5.3 Build systems: what must be rebuilt?

A build graph has an arc `a -> b` when `b` needs `a`. If the files in `changed` are touched, **everything reachable from them** is stale and must be rebuilt, and it must be rebuilt in an order that respects the arcs. Nothing else needs to be touched; that is the whole point of an incremental build. A cyclic dependency is a build error.

```cpp
// A build system: an arc a -> b means "b needs a built first". When the files in `changed` are touched, everything reachable from them
// must be rebuilt, in an order that respects the arcs. Returns ok = false if the dependencies are cyclic (nothing can be built).
struct Rebuild { bool ok = true; std::vector<int> order; };
Rebuild rebuild_order(const Csr& g, const std::vector<int>& changed) {
    Rebuild r; TopoResult t = kahn(g); if (!t.ok) { r.ok = false; return r; }
    std::vector<char> dirty(g.n, 0); std::vector<int> st;
    for (int c : changed) if (!dirty[c]) { dirty[c] = 1; st.push_back(c); }
    while (!st.empty()) {
        int u = st.back(); st.pop_back();
        for (int i = g.off[u]; i < g.off[u + 1]; ++i) if (!dirty[g.to[i]]) { dirty[g.to[i]] = 1; st.push_back(g.to[i]); }
    }
    for (int v : t.order) if (dirty[v]) r.order.push_back(v);
    return r;
}
```

Vertices here: 0 `util.h`, 1 `util.c`, 2 `net.c`, 3 `main.c`, 4 `libutil`, 5 `libnet`, 6 `app`, with arcs `0->1 0->2 1->4 2->5 4->6 5->6 3->6`.

```text
touch util.c [1 4 6]
touch util.h [0 1 2 4 5 6]
touch main.c [3 6]
```

Touching `util.c` rebuilds the library and the application (`1 4 6`), but not `net.c`. Touching the header rebuilds both libraries and everything above them. Touching `main.c` rebuilds only itself and the application. The function takes the global order and filters it; a production build tool does the sorting only inside the stale set (and uses file timestamps or content hashes to prune further), but the specification is this one.

## 6. Grids and implicit graphs

Nothing changes when the graph is implicit: the arcs between cells come from a rule (a cell's value is greater than its neighbour's, say), the in-degree of a cell is computed by counting its qualifying neighbours, and Kahn's loop runs on that. This is the standard way to do "Longest Increasing Path in a Matrix": the "greater than" relation on cells is acyclic by construction, so the levels give the answer directly, with no memoisation and no recursion (problem list below).

## 7. Measured

### 7.1 Random acyclic graph, a million vertices

V = 1,000,000, 3,999,996 arcs (self-pairs dropped). Times in ms (run 1 / run 2), on a prebuilt CSR.

| Algorithm | ms |
|---|---|
| Kahn, queue | 111.8 / 124.6 |
| Kahn, min-heap | 203.6 / 224.6 |
| DFS reverse postorder | 134.9 / 107.6 |

All three succeed. The graph has **29 levels**, so with unlimited parallelism the whole job needs 29 rounds, and the widest level holds 124,847 vertices (about 12 percent of all of them). The heap costs 1.8 times the queue in both runs, which is the log factor plus the cache misses of heap sifting. Kahn and DFS are within about 25 percent of each other and **swap places between the two runs**, so I make no claim about which is faster here.

### 7.2 Recursion

A chain of 10^6 vertices (the longest possible path), 8 MB stack, in a child process, DFS topological sort:

| Variant | Exit |
|---|---|
| Recursive, 1,000,000 vertices | killed by a signal |
| Iterative (section 3), 1,000,000 vertices | exit 0 |
| Recursive, 100,000 vertices | exit 0 |

The same fact as in Topics 01 and 02. Kahn needs no recursion at all, and the DFS of section 3 keeps its stack on the heap.

### 7.3 How fast the freedom disappears

Ten vertices and `m` distinct arcs, each going from an earlier to a later vertex of a hidden random order (so the graph is always acyclic); for each `m`, 300 seeded random graphs. The mean number of topological orders, and the percentage of graphs with exactly one:

| Arcs | Mean orders | Unique, % |
|---|---|---|
| 0 | 3,628,800 | 0.0 |
| 5 | 164,144 | 0.0 |
| 10 | 14,246 | 0.0 |
| 15 | 1,958 | 0.0 |
| 20 | 354 | 0.0 |
| 30 | 22 | 2.0 |
| 45 | 1 | 100.0 |

With no arcs there are 10! = 3,628,800 orders. Five arcs already cut that by a factor of 22; ten arcs by about 250; twenty by about 10,000. With 30 of the 45 possible pairs constrained, a typical graph has about 22 orders and only two percent have a single one. All 45 pairs constrained is a total order: exactly one. The practical reading: **a schedule with few constraints has enormous freedom, and a unique order needs almost a complete set of constraints**, so "the order is unique" is a strong statement about the input.

The subset DP for n = 20 with no arcs (the worst case, 20! orders) takes 75.0 / 72.5 ms.

## 8. Choosing

| Question | Use | Note |
|---|---|---|
| Any valid order, or "is there one" | Kahn | O(V + E), no recursion |
| The cycle itself | `topo_dfs` | Kahn gives only the stuck set |
| Smallest or largest order | Kahn with a heap | O((V + E) log V) |
| How many rounds in parallel | Kahn `level` / `semesters` | the critical path |
| Is the order unique | Kahn `unique` | one waiting vertex at every step |
| Earliest finish with durations | Kahn with weighted levels | problem I |
| How many orders | subset DP, n <= 20 | counting in general has no fast method |
| What to rebuild | reachability, then the global order | `rebuild_order` |
| Order inside a search you already have | reverse postorder | one extra line |

## 9. What the tests check, and what they cannot

Of the 52 mutants, 49 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). One of the 49, the alien dictionary run with a plain queue instead of the heap, survived the first run and is caught only since I added a test with a case where the two give different (both valid) answers. Three survive, and each is an equivalent change: the mutated program gives the same answers as the original on every input, so no test could tell them apart. The reasons:

- **`if (--indeg[v] <= 0)` instead of `== 0` in `kahn`.** The counter of a vertex is decremented once per arc into it and starts at the number of those arcs, so it reaches 0 exactly once and never goes below.
- **`need[a] |= 1 << b` instead of `need[b] |= 1 << a` in `count_orders`.** This counts the orders of the *reversed* graph, and reversing a topological order of a graph gives a topological order of its reverse, so the two numbers are always equal. (It is a nice fact to know, and it means this particular slip cannot be seen in the count.)
- **`if (!dp[S] && S) continue;` instead of `if (!dp[S]) continue;`.** The two differ only at `S = 0`, where `dp[0]` is 1, not 0.

I wrote these as plausible slips and classed them as equivalent by reading the code, not because the tests passed.

Mutants are chosen where a bug is plausible, not at random, so "49 of 52" is a statement about those edits and not a proof about the listings. The exhaustive tests cover graphs of up to 7 vertices (and the subset DP up to 20 on two special shapes); the million-vertex tests check validity of the order, not optimality of anything. Timings depend on the machine.

## 10. Common mistakes

- **Reversed arcs.** Reading `[a, b]` as `a -> b` when the problem means `b -> a`. Write the convention in a comment.
- Reversing the answer instead of the arcs, and then asking for the smallest order.
- Counting in-degrees from a deduplicated structure while releasing with a non-deduplicated one (or the reverse): with parallel arcs the counts never reach 0. Count and decrement over the **same** arcs.
- Stopping when the queue is empty and forgetting to compare the output size with V.
- Forgetting vertices that have no arcs at all: they are sources and belong in the order.
- In the alien dictionary: using every pair of words instead of neighbours; drawing arcs from later letters of a pair; missing the longer-before-its-prefix case; dropping letters that appear in no arc.
- Recursive DFS on a path of 10^6 vertices.
- Treating the DFS postorder as "the" order and relying on it being stable across runs or compilers.
- Using the stuck set as if it were the cycle.
- Assuming a unique order exists because the example has one.

## 11. Questions to answer out loud

1. **What is a topological order and when does it exist?** A listing with every arc forwards; exactly when the graph is acyclic.
2. **Why does every DAG have a source?** Walk backwards; a finite graph forces a repeat, which is a cycle.
3. **Describe Kahn's algorithm and its cycle test.** Release vertices with in-degree 0, decrement their out-neighbours; if fewer than V are output, the rest are on or behind a cycle.
4. **Why does reverse postorder work?** For every arc the head finishes before the tail, unless the arc is a back edge, which is a cycle.
5. **Which one gives the cycle?** The DFS; Kahn gives the vertices stuck on or behind it.
6. **How do you get the lexicographically smallest order?** Kahn with a min-heap.
7. **How do you get the number of parallel rounds?** The longest path, computed as `level` in Kahn's loop.
8. **How do you tell whether the order is unique?** At every step exactly one vertex was available.
9. **Alien dictionary: which pairs give arcs?** Neighbouring words, first differing letter only; the prefix case is invalid.
10. **How do you find what to rebuild?** Reachability from the changed files, in the global topological order.
11. **How many topological orders can there be?** From 1 up to V!; counting them exactly has no known fast method in general (subset DP for n up to about 20).
12. **When is the recursive version a bug?** When the longest path is unbounded: 10^6 on 8 MB crashed here.

## 12. Problems

### 12.1 On paper

**A.** Vertices 0 to 5, arcs `5->2 5->0 4->0 4->1 2->3 3->1`. Give Kahn's order (queue), the smallest-first order, the DFS order (roots in increasing order, arcs in the order listed), the levels, whether the order is unique, and the number of rounds.

**B.** Prove that if Kahn's algorithm outputs fewer than V vertices, the graph has a cycle.

**C.** A course catalogue has 4 courses and `prerequisites = [[1,0],[2,1],[3,1],[3,2]]`. Draw the arcs, give an order, and the minimum number of semesters if you may take any number of courses per semester.

**D.** Words `baa abcd abca cab cad`. Which arcs do the neighbouring pairs give, and what does `alien_order` return?

**E.** Arcs `0->1 1->2 2->0 3->0 2->4` on 5 vertices. What does Kahn output, what is the stuck set, and what cycle does the DFS return?

**F.** How many topological orders does the graph `0->2 1->2 2->3` have? Explain without enumerating.

### 12.2 On LeetCode

For each: what are the vertices and the arcs (and their direction), and which variant of Kahn or DFS? Hints in the answers. Numbers and titles from memory; premium ones are marked.

| Problem | Number | What to practise |
|---|---|---|
| Course Schedule | 207 | `ok` of Kahn |
| Course Schedule II | 210 | The order itself |
| Alien Dictionary (premium) | 269 | Section 5.2, with all traps |
| Parallel Courses (premium) | 1136 | `semesters`, -1 on a cycle |
| Sequence Reconstruction (premium) | 444 | `unique`, and the order equals the sequence |
| Find All Possible Recipes from Given Supplies | 2115 | Kahn from the supplies |
| Minimum Height Trees | 310 | Peel leaves (Kahn on degrees), last 1 or 2 are centres |
| Longest Increasing Path in a Matrix | 329 | Section 6: levels of the grid DAG |
| Sort Items by Groups Respecting Dependencies | 1203 | Two sorts: groups, then items inside groups |
| Largest Color Value in a Directed Graph | 1857 | Kahn plus a count table per vertex |

### 12.3 In your editor

**H.** Write Kahn from memory with the smallest-first heap, and test it against all permutations for n up to 7, as the tests above do.

**I.** Extend `kahn` to take a duration per vertex and return the earliest finish time of each (finish[v] = duration[v] + max finish of its prerequisites) and the length of the critical path. Test on the graph of problem A with durations equal to the vertex numbers plus one.

**J.** Implement `has_unique_order_equal_to(seq, constraints)` for Sequence Reconstruction and test it against brute force on random small inputs.

### Answers

**A.** Sources 4 and 5 give the queue `[4, 5]`. Taking 4 lowers the in-degree of 0 (from 2 to 1) and of 1 (2 to 1). Taking 5 releases 2 (arc `5->2` first) and then 0: queue `[2, 0]`. Taking 2 lowers 3, which is released; taking 0 releases nothing; taking 3 releases 1. Kahn: `4 5 2 0 3 1`. Smallest first: `4 5 0 2 3 1`. DFS with roots in increasing order: 0 and 1 have no out-arcs and finish at once; 2 goes to 3, which goes to the already finished 1, so the finish order continues 3, 2; then 4 and 5 finish. Postorder `0 1 3 2 4 5`, reversed `5 4 2 3 1 0`. Levels `1 3 1 2 0 0`. Not unique (two sources); 4 rounds (levels 0 to 3); 13 topological orders.

**B.** Let R be the vertices never output. Every vertex of R has in-degree at least 1 counting only arcs from R (an arc from an output vertex was already used up, and if all its prerequisites were output it would have been released). Start at any vertex of R and walk backwards along such an arc; the walk stays in R, which is finite, so it repeats a vertex: a cycle.

**C.** Arcs `0->1 1->2 1->3 2->3`. One order is `0 1 2 3`; it is the only one, because each step has a single available vertex. Levels are `0 1 2 3`, so 4 semesters: the prerequisites form a chain.

**D.** The pairs give `b->a` (from `baa`, `abcd`), `d->a` (`abcd`, `abca`; differ at position 3: `d` before `a`), `a->c` (`abca`, `cab`), `b->d` (`cab`, `cad`). The smallest-first topological order of the letters is `bdac`.

**E.** Kahn outputs `3` and gets stuck: the stuck set is `{0, 1, 2, 4}` (the cycle `0 1 2`, and `4` behind it). The DFS from 0 returns the cycle `[0 1 2]`.

**F.** Vertices 0 and 1 must both precede 2, which must precede 3, and 0 and 1 are not otherwise constrained: the two orders are `0 1 2 3` and `1 0 2 3`. In general the number is the product of the factorials of the sizes of the free groups here, 2! * 1! * 1!.

**LeetCode hints.** 207, 210: arcs `prerequisite -> course`. 269: section 5.2, return the empty string when there is none. 1136: `semesters`. 444: the order is unique iff at every step one vertex is waiting; compare with the sequence, and check that every element of the sequence appears. 2115: the supplies are sources with no arcs into them; a recipe is released when all its ingredients are available; build arcs `ingredient -> recipe`. 310: repeatedly remove all current leaves (degree 1) until at most two vertices remain. 329: the arc goes from a smaller cell to a larger neighbour; the answer is the number of levels. 1203: sort the groups by their cross-group arcs, sort the items inside each group, then concatenate. 1857: keep `count[v][colour]` along Kahn and take the maximum, or -1 on a cycle.

**H.** Compare with the first valid permutation in `next_permutation` order. **I.** With the durations `d[v] = v + 1` on the graph of A the finish times are `[7, 15, 9, 13, 5, 6]` for vertices 0 to 5 (for instance vertex 3: `4 + 9 = 13`, where 9 is the finish of 2) and the critical path has length 15, ending at vertex 1 through `5 -> 2 -> 3 -> 1`. **J.** Check that the unique topological order exists (every step has one waiting vertex) and equals the sequence.

---

## Cheat sheet

- **Direction:** arc `a -> b` = "a before b". Read the problem twice: `[a, b]` usually means `b -> a`.
- **Kahn:** in-degree counts, queue of zeros; output shorter than V means a cycle; stuck set is on or behind a cycle; O(V + E).
- **Kahn extras:** min-heap gives the smallest order (about 1.8 times the cost here); `level` gives the rounds and the critical path; one waiting vertex at every step means a unique order.
- **DFS:** reverse postorder; an arc into grey is the cycle, and you get it.
- **Counting:** subset DP, n up to 20; the number collapses fast as arcs are added (10 vertices, 30 arcs: about 22 orders).
- **Alien:** neighbouring words, first difference, prefix is invalid, keep letters with no arcs.
- **Build:** stale = reachable from the changed; rebuild in the global order.
- **Measured:** 10^6 vertices and 4 million arcs in about 110 to 125 ms with Kahn; recursion on a 10^6 chain died at 8 MB.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Kahn's algorithm and the reverse-postorder method are standard (Kahn's 1962 paper and CLRS, named from memory; chapter numbers not rechecked in this session). The statement that counting the topological orders of a general DAG is #P-complete is from memory.
- Problem numbers and titles in section 12 are LeetCode's, from memory, and four of them are marked premium; check a number before you rely on it.
