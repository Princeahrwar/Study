# DSA 08.08: Advanced Graphs

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Connectivity and cycles](02_connectivity_and_cycles.md) (DFS and components) and [Topological sort](03_topological_sort.md) (DAGs and finishing order). There is no companion demo file: every structure is listed in this document.*

Four questions that sound unrelated turn out to share one tool, the **low-link value** of a depth-first search. Which parts of a directed graph can reach each other, and in what order do they depend on each other (**strongly connected components**)? Which single edge, or which single vertex, can you remove to split an undirected network (**bridges** and **articulation points**)? Can you draw a figure, visit a list of flights, or lay out dominoes using every edge exactly once (**Euler trails**)? And can a set of "this or that" constraints all be satisfied at once (**2-SAT**)? The first two are the same DFS with the same bookkeeping. The fourth is the first one in disguise: build a graph out of the constraints, find its components, and read the answer off them. The third is a different, simpler idea, a walk that splices itself together.

By the end you can:

- define a strongly connected component (SCC), draw the **condensation** and say why it is a DAG;
- write Tarjan's algorithm (one pass, low-links, a stack) and Kosaraju's (two passes, the reverse graph), both iteratively, and say what order each numbers the components in;
- find bridges and articulation points, including the cases that break naive code: the DFS root, parallel edges, and a graph with several components;
- decide whether an Euler trail exists from the degrees, and build it with Hierholzer's algorithm, directed or undirected, even the lexicographically smallest one;
- turn a 2-SAT formula into an implication graph, decide it with SCCs, and print a satisfying assignment;
- say, with measurements, where the giant component appears in a random digraph, how bridges thin out as a graph gets denser, and where random 2-SAT stops being satisfiable.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 173,356 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: both SCC algorithms against the definition (two vertices are together exactly when each reaches the other) on thousands of small random digraphs, plus the component numbering property of each and the uniqueness of the condensation; bridges and articulation points against recomputation (remove the edge or vertex and count components) on small multigraphs with self loops; the Euler test against an exhaustive search over all orderings of the edges, and every trail returned checked to use each edge once; 2-SAT against brute force over all assignments (both satisfiable and unsatisfiable cases occur often); and graphs of a million vertices (a ring, a chain, a random digraph with three million arcs). Then 41 hand-made mutants of the listings were run against the tests: **36 were caught and 5 are equivalent changes** (section 8).

**Data and timing.** Graphs are **synthetic** and seeded: V = 1,000,000 vertices with random edges (uniform random ends), and a 2-SAT formula with 1,000,000 variables. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs. Every DFS in this document is **iterative** (an explicit stack): earlier topics found that a recursive DFS crashes at the 8 MB default stack on a chain of a million vertices, and a chain of a million vertices is one of the tests here.

---

## 1. Strongly connected components

In an undirected graph, "connected" is symmetric. In a directed graph it is not: `u` may reach `v` without `v` reaching `u`. Two vertices are **strongly connected** when each reaches the other. That relation is reflexive, symmetric and transitive, so it splits the vertices into classes, the **strongly connected components**: maximal sets in which every vertex reaches every other. A vertex on no cycle is a component by itself.

The **condensation** replaces each component by one vertex and keeps an arc `A -> B` when some arc of the graph goes from a vertex of `A` to a vertex of `B`. **It is always a DAG.** If it had a cycle through `A` and `B`, every vertex of `A` could reach every vertex of `B` and back, and they would be one component. That is the useful picture: inside a component everything is mutually reachable, so a question about reachability, dependencies or "can this ever happen" becomes a question about a DAG of components, and [topological sort](03_topological_sort.md) applies.

Example used below (seven vertices): `0->1, 1->2, 2->0` is a cycle, `2->3` leaves it, `3->4, 4->5, 5->3` is a second cycle, `5->6` leaves that. Components: `{0,1,2}`, `{3,4,5}`, `{6}`; the condensation is a path of three vertices.

All the code in this document uses compressed rows (see [Representations](01_representations_and_traversal.md)) with an **edge id** on every arc. An undirected edge becomes two arcs with the same id; that id is what lets the bridge search tell parallel edges apart. `sort_nbrs` orders each row by target, which the Euler code uses to get lexicographic answers.

```cpp
struct Edge { int a, b; };
// Compressed rows (Topic 01) with an edge id on every arc, so a DFS can tell WHICH edge it arrived by (needed for parallel edges).
// An undirected edge becomes two arcs with the same id. sort_nbrs = true orders each row by target (for lexicographic answers).
struct Csr { int n = 0; std::vector<int> off, to, eid; };
Csr build_csr(int n, const std::vector<Edge>& e, bool directed, bool sort_nbrs = false) {
    Csr g; g.n = n; g.off.assign(n + 1, 0);
    for (auto& x : e) { ++g.off[x.a + 1]; if (!directed) ++g.off[x.b + 1]; }
    for (int u = 0; u < n; ++u) g.off[u + 1] += g.off[u];
    g.to.resize(g.off[n]); g.eid.resize(g.off[n]); std::vector<int> at(g.off.begin(), g.off.end() - 1);
    for (int i = 0; i < (int)e.size(); ++i) {
        g.to[at[e[i].a]] = e[i].b; g.eid[at[e[i].a]++] = i;
        if (!directed) { g.to[at[e[i].b]] = e[i].a; g.eid[at[e[i].b]++] = i; }
    }
    if (sort_nbrs) for (int u = 0; u < n; ++u) {
        std::vector<std::pair<int, int>> row; for (int k = g.off[u]; k < g.off[u + 1]; ++k) row.push_back({g.to[k], g.eid[k]});
        std::sort(row.begin(), row.end()); for (int k = g.off[u]; k < g.off[u + 1]; ++k) { g.to[k] = row[k - g.off[u]].first; g.eid[k] = row[k - g.off[u]].second; }
    }
    return g;
}
```

## 2. Tarjan's algorithm

Run a DFS. Give each vertex a discovery number `num[v]`. Define `low[v]` as the smallest `num` that `v`'s DFS subtree can reach using tree arcs and at most one arc back to a vertex that is **still on a stack of open candidates**. Two facts make it work:

- A vertex whose `low` equals its own `num` cannot reach anything discovered earlier that is still open, so everything above it on the stack, together with it, is exactly one component. Pop down to it.
- An arc to a vertex that is already in a finished component must be ignored: that component is closed, and `v` is not part of it. This is the one place where the directed version differs from the undirected one, and the test `comp[w] < 0` ("not yet assigned") is the whole check.

```cpp
// Tarjan's strongly connected components. num[v] = DFS discovery time; low[v] = the smallest discovery time reachable from v's DFS
// subtree using tree arcs and at most one arc back to a vertex still on the stack. If low[v] == num[v], v is the root of a component:
// pop the stack down to v. "On the stack" is the same as "discovered but not yet assigned" (comp < 0). One pass, O(V + E).
// Components are numbered in the order they are completed, which is a REVERSE topological order of the condensation:
// every arc u -> v satisfies comp[u] >= comp[v] (component 0 is a sink).
struct Scc { std::vector<int> comp; int count = 0; };
Scc scc_tarjan(const Csr& g) {
    int n = g.n, timer = 0; Scc r; r.comp.assign(n, -1);
    std::vector<int> num(n, -1), low(n, 0), it(n, 0), st, call;
    for (int s = 0; s < n; ++s) {
        if (num[s] >= 0) continue;
        num[s] = low[s] = timer++; it[s] = g.off[s]; st.push_back(s); call.push_back(s);
        while (!call.empty()) {
            int v = call.back();
            if (it[v] < g.off[v + 1]) {
                int w = g.to[it[v]++];
                if (num[w] < 0) { num[w] = low[w] = timer++; it[w] = g.off[w]; st.push_back(w); call.push_back(w); }
                else if (r.comp[w] < 0) low[v] = std::min(low[v], num[w]);          // w is on the stack
            } else {
                if (low[v] == num[v]) { int w; do { w = st.back(); st.pop_back(); r.comp[w] = r.count; } while (w != v); ++r.count; }
                call.pop_back(); if (!call.empty()) low[call.back()] = std::min(low[call.back()], low[v]);
            }
        }
    }
    return r;
}
```

Trace on the example (checked by running the code). DFS from 0 numbers 0, 1, 2; the arc `2->0` goes to an open vertex so `low[2] = 0`. Then 3, 4, 5 get numbers 3, 4, 5; the arc `5->3` gives `low[5] = 3`; vertex 6 gets number 6, has no arcs, and `low[6] = num[6]`, so it is popped as component 0. Back at 5, 4 and 3 the low value 3 travels up; at 3 `low = num = 3`, so the stack is popped down to 3 and `{5,4,3}` becomes component 1. Back at 2, 1, 0 the low value 0 travels up and `{2,1,0}` becomes component 2.

```
tarjan comp   2 2 2 1 1 1 0
count=3
kosaraju comp 0 0 0 1 1 1 2
count=3
condensation (tarjan numbers): 1->0 2->1
condensation (kosaraju numbers): 0->1 1->2
```

The first two lines are Tarjan's: `comp` is `2 2 2 1 1 1 0`. Note the numbering: the sink component `{6}` is number 0, the source component is the last. **Tarjan numbers components in reverse topological order**, so every arc `u -> v` has `comp[u] >= comp[v]`. This is checked by the tests, and it is what makes 2-SAT (section 6) cheap.

One subtle line: after a vertex `v` is popped as a component root, the code still does `low[parent] = min(low[parent], low[v])`. That is harmless, because `low[v] = num[v]` is larger than `num[parent]`, which is at least `low[parent]`.

## 3. Kosaraju's algorithm and the condensation

Kosaraju needs the **reverse graph** (every arc flipped) and two passes.

1. DFS on `G`; record vertices in the order they **finish**.
2. Take vertices in **decreasing** finish order. For each one not yet assigned, search the **reverse** graph from it; everything newly reached is one component.

Why it works: the vertex that finishes last belongs to a source component of `G` (nothing outside it reaches in). A source of `G` is a sink of the reverse graph, so a search in the reverse graph from it cannot leave its component. Remove that component and repeat. The same argument covers each later vertex in the order.

```cpp
// Kosaraju: two passes. Pass 1: DFS on G, record vertices in order of FINISHING. Pass 2: DFS on the REVERSED graph, starting
// from vertices in decreasing finish order; each tree of pass 2 is one component. Why: the vertex that finishes last is in a source
// component of G, which is a sink component of the reverse graph, so its pass-2 search cannot leak into another component.
// Components come out in TOPOLOGICAL order (every arc u -> v has comp[u] <= comp[v]), the opposite of Tarjan's numbering.
Scc scc_kosaraju(const Csr& g, const Csr& rev) {
    int n = g.n; Scc r; r.comp.assign(n, -1);
    std::vector<int> order, it(n, 0), call; std::vector<char> seen(n, 0);
    for (int s = 0; s < n; ++s) {
        if (seen[s]) continue;
        seen[s] = 1; it[s] = g.off[s]; call.push_back(s);
        while (!call.empty()) {
            int v = call.back();
            if (it[v] < g.off[v + 1]) { int w = g.to[it[v]++]; if (!seen[w]) { seen[w] = 1; it[w] = g.off[w]; call.push_back(w); } }
            else { order.push_back(v); call.pop_back(); }
        }
    }
    for (int i = n - 1; i >= 0; --i) {
        int s = order[i]; if (r.comp[s] >= 0) continue;
        r.comp[s] = r.count; call.push_back(s);
        while (!call.empty()) {
            int v = call.back(); call.pop_back();
            for (int k = rev.off[v]; k < rev.off[v + 1]; ++k) if (r.comp[rev.to[k]] < 0) { r.comp[rev.to[k]] = r.count; call.push_back(rev.to[k]); }
        }
        ++r.count;
    }
    return r;
}
// The condensation: one vertex per component, an arc between components when some arc of G joins them (duplicates removed).
// It is always a DAG, so Topic 03 applies to it.
std::vector<Edge> condensation(const std::vector<Edge>& e, const Scc& s) {
    std::vector<Edge> out; for (auto& x : e) if (s.comp[x.a] != s.comp[x.b]) out.push_back({s.comp[x.a], s.comp[x.b]});
    std::sort(out.begin(), out.end(), [](const Edge& p, const Edge& q) { return p.a != q.a ? p.a < q.a : p.b < q.b; });
    out.erase(std::unique(out.begin(), out.end(), [](const Edge& p, const Edge& q) { return p.a == q.a && p.b == q.b; }), out.end());
    return out;
}
```

On the example, pass 1 finishes the vertices in the order 6, 5, 4, 3, 2, 1, 0. Pass 2 starts at 0, reaches 2 and 1 through reversed arcs (component 0), then the next unassigned vertex is 3, which reaches 5 and 4 (component 1), then 6 (component 2). This is the third line of the trace above: `0 0 0 1 1 1 2`. **Kosaraju numbers components in topological order**, the opposite of Tarjan, so the condensation arcs read `0->1 1->2` there and `1->0 2->1` with Tarjan's numbers (last two lines of the trace). Both are valid; know which one you hold before you use the order.

The condensation function sorts the component-to-component arcs and removes duplicates, so it has at most one arc for each pair.

### Which one, measured

Random digraphs with V = 1,000,000 vertices and E random arcs. `largest` is the size of the biggest component. Times in ms, run 1 / run 2 (the reverse graph for Kosaraju was built beforehand and is not in its time; Tarjan does not need it).

| E | SCCs | largest | Tarjan | Kosaraju |
|---|---|---|---|---|
| 500,000 | 1,000,000 | 1 | 44 / 42 | 48 / 45 |
| 1,000,000 | 999,917 | 51 | 92 / 90 | 99 / 94 |
| 2,000,000 | 366,392 | 633,609 | 195 / 194 | 230 / 231 |
| 4,000,000 | 39,250 | 960,751 | 300 / 304 | 303 / 301 |

- **The giant component appears between E/V = 1 and E/V = 2.** At E = 1 million the largest component has 51 vertices; at 2 million it has 633,609; at 4 million, 960,751. A random digraph with average degree below 1 is almost all singletons; above it, most vertices fall into one component. (That the threshold is at average degree 1 is the standard result for random digraphs, from memory; the measurement shows it lies between 1 and 2.)
- **The two algorithms agree** on the component count in every row, and the tests check that they give the same partition on thousands of small graphs.
- **Neither wins clearly.** Kosaraju took 0.99 to 1.19 times as long as Tarjan across the eight measurements: a little slower on the sparse graphs, about equal on the densest. Tarjan is one pass and needs no reverse graph, so it is the default; Kosaraju is easier to reason about and to remember, and the reverse graph is sometimes needed anyway.
- **Both are linear**, O(V + E): the times grow with E and not faster.

## 4. Bridges and articulation points

In an undirected graph, a **bridge** is an edge whose removal increases the number of components. An **articulation point** (cut vertex) is a vertex whose removal does. These are the weak spots of a network: one cable, one router.

The DFS tree of an undirected graph has a special property: every non-tree edge connects a vertex to one of its **ancestors** (there are no cross edges). So `low[v]`, the smallest `num` reachable from `v`'s subtree by tree edges plus one back edge, is enough to decide:

- A tree edge `(p, w)` is a **bridge** if `low[w] > num[p]`: nothing in `w`'s subtree reaches `p` or above except through that edge.
- A non-root vertex `p` is an **articulation point** if some child `w` has `low[w] >= num[p]`: the subtree of `w` can get no higher than `p`, so removing `p` cuts it off. (Equal is enough, a back edge to `p` itself does not help once `p` is gone.)
- The **root** is an articulation point if and only if it has **two or more DFS children**. For the root `low >= num` always holds, so the rule above would wrongly mark every root that has one child.

Two details that break naive code. First, **skip the arrival edge by its id, not by the parent vertex**: if two parallel edges join `p` and `w`, the second is a way back, so neither is a bridge, and skipping by vertex would call both bridges. That is why every arc in the listing carries an edge id. Second, a graph can have several components, so the DFS is started from every unvisited vertex and the root rule is applied to each root.

```cpp
// Bridges and articulation points of an undirected graph. Same low-link idea on the DFS tree of an undirected graph, where every
// non-tree edge goes to an ancestor. A tree edge (v, w) is a BRIDGE if low[w] > num[v]: nothing below w reaches v or above, so removing
// the edge disconnects w's subtree. A non-root v is an ARTICULATION POINT if some child has low[w] >= num[v]; the root is one
// if it has two or more DFS children. Arrival is skipped by EDGE id, not by parent vertex, so a parallel edge is a way back (not a bridge).
struct Cuts { std::vector<char> is_bridge, is_art; int bridges = 0, arts = 0; };
Cuts bridges_and_cuts(const Csr& g, int m) {
    int n = g.n, timer = 0; Cuts r; r.is_bridge.assign(m, 0); r.is_art.assign(n, 0);
    std::vector<int> num(n, -1), low(n, 0), it(n, 0), pe(n, -1), call, kids(n, 0);
    for (int s = 0; s < n; ++s) {
        if (num[s] >= 0) continue;
        num[s] = low[s] = timer++; it[s] = g.off[s]; call.push_back(s);
        while (!call.empty()) {
            int v = call.back();
            if (it[v] < g.off[v + 1]) {
                int k = it[v]++, w = g.to[k];
                if (g.eid[k] == pe[v]) continue;                                         // the edge we arrived by
                if (num[w] < 0) { pe[w] = g.eid[k]; num[w] = low[w] = timer++; it[w] = g.off[w]; call.push_back(w); ++kids[v]; }
                else low[v] = std::min(low[v], num[w]);                                  // back edge (or a parallel edge to the parent)
            } else {
                call.pop_back();
                if (call.empty()) { if (kids[v] >= 2) r.is_art[v] = 1; break; }
                int p = call.back(); low[p] = std::min(low[p], low[v]);
                if (low[v] > num[p]) r.is_bridge[pe[v]] = 1;
                if (low[v] >= num[p] && call.size() > 1) r.is_art[p] = 1;                      // p is not the root
            }
        }
    }
    for (char c : r.is_bridge) r.bridges += c;
    for (char c : r.is_art) r.arts += c;
    return r;
}
```

Trace (checked by running the code). The graph has two triangles `{0,1,2}` and `{3,4,5}`, a link `1-3`, a link `5-6`, and two parallel edges `6-7`:

```
bridges: 1-3 5-6
articulation: 1 3 5 6
two parallel edges: bridges=0
single edge: bridges=1 articulation=0
star: bridges=3 articulation=1
```

The bridges are the two links, and the doubled edge `6-7` is not a bridge, because the second copy is a way back. The articulation points are 1, 3, 5 and 6: the ends of the two links that have other vertices on both sides. Vertex 6 is one because 7 hangs only from it; 7 itself is not (removing it leaves the rest connected), and neither are 0, 2 and 4. The three small cases show the edge conditions: a single edge is a bridge but neither end is a cut vertex (removing an end leaves one vertex, which is one component); a star has three bridges and its centre as the one articulation point.

Measured on random undirected graphs with V = 1,000,000 (times in ms, run 1 / run 2):

| E | bridges | articulation points | time |
|---|---|---|---|
| 500,000 | 499,415 | 264,093 | 65 / 59 |
| 1,000,000 | 366,052 | 279,648 | 158 / 155 |
| 2,000,000 | 78,518 | 74,819 | 285 / 294 |

As the graph gets denser, bridges disappear: of 500,000 edges almost all (499,415) are bridges, because the graph is mostly small trees; with 1 million edges 366,052 are; with 2 million only 78,518, as more and more edges lie on cycles. The time grows in proportion to E.

## 5. Euler trails

An **Euler trail** uses every edge exactly once; an **Euler circuit** is one that returns to its start. (A Hamiltonian path, visiting every vertex once, is a different question with no known fast algorithm; Euler's is decided by degrees alone.)

**When it exists.**

- *Undirected:* all edges lie in one connected piece, and the number of odd-degree vertices is 0 (a circuit, start anywhere) or 2 (a trail from one odd vertex to the other).
- *Directed:* all edges in one piece, and every vertex has in-degree equal to out-degree (a circuit), except possibly one vertex with out minus in equal to 1 (the start) and one with in minus out equal to 1 (the end).

"One piece" is not tested separately: the construction below produces a trail with fewer than m + 1 vertices when the edges lie in several pieces, and the code rejects that.

**Hierholzer's algorithm.** Keep a stack of vertices holding the current walk. Repeat: look at the top vertex; if it has an unused edge, take it and push the other end; if not, pop it and write it to the output. The output is the trail **in reverse**. Why: the first vertex that gets stuck is the end of the trail (the degree rule says a walk can only get stuck at the end vertex); every later stuck vertex closes a sub-circuit that is spliced into the walk at the point where the vertex was left. Each edge is used once and each vertex is popped once, so the time is O(V + E).

```cpp
// Euler trail (a walk using every edge exactly once) by Hierholzer's algorithm. First the degree test: undirected, 0 or 2 odd
// vertices; directed, every vertex balanced except possibly one with out - in = 1 (the start) and one with in - out = 1 (the end).
// Then walk until stuck, and splice: keep a stack, always extend along an unused edge; when a vertex has none left, it is the NEXT
// vertex of the trail in reverse. The trail is valid only if it used all m edges (otherwise the edges are in several pieces).
// Returns the vertices in order, or an empty list if no Euler trail exists (or m == 0). With sorted rows the trail is the
// lexicographically smallest, because we always take the smallest unused neighbour first.
std::vector<int> euler_trail(const Csr& g, int m, bool directed) {
    int n = g.n; if (m == 0) return {};
    std::vector<int> deg(n, 0), indeg(n, 0); for (int u = 0; u < n; ++u) { deg[u] = g.off[u + 1] - g.off[u]; if (directed) for (int k = g.off[u]; k < g.off[u + 1]; ++k) ++indeg[g.to[k]]; }
    int start = -1, plus = 0, minus = 0;
    for (int u = 0; u < n; ++u) {
        if (directed) { if (deg[u] - indeg[u] == 1) { ++plus; start = u; } else if (indeg[u] - deg[u] == 1) ++minus; else if (deg[u] != indeg[u]) return {}; }
        else if (deg[u] & 1) { ++plus; if (start < 0) start = u; }
    }
    if (directed ? (plus > 1 || minus > 1 || plus != minus) : (plus != 0 && plus != 2)) return {};
    if (start < 0) for (int u = 0; u < n; ++u) if (deg[u] > 0) { start = u; break; }
    std::vector<int> ptr(g.off.begin(), g.off.end() - 1), stk{start}, res; std::vector<char> used(m, 0);
    while (!stk.empty()) {
        int v = stk.back(); while (ptr[v] < g.off[v + 1] && used[g.eid[ptr[v]]]) ++ptr[v];
        if (ptr[v] == g.off[v + 1]) { res.push_back(v); stk.pop_back(); }
        else { int k = ptr[v]++; used[g.eid[k]] = 1; stk.push_back(g.to[k]); }
    }
    std::reverse(res.begin(), res.end());
    if ((int)res.size() != m + 1) return {};
    return res;
}
```

Trace (checked by running the code). Directed arcs `0->1, 1->2, 2->0, 0->3`: vertex 0 has out 2 and in 1, so 0 is the start; vertex 3 has in 1 and out 0, so it is the end. The walk goes 0, 1, 2, 0, 3 and gets stuck at 3, then 0, 2, 1, 0 are popped as they have nothing left; reversed, the trail is `0 1 2 0 3`. For the undirected square `0-1, 1-2, 2-3, 3-0` with diagonal `0-2`, vertices 0 and 2 are odd; the smallest start is 0 and the trail is `0 1 2 0 3 2`. Two edges in separate pieces and K4 (four odd vertices) have no trail; parallel arcs are fine.

```
directed trail 0 1 2 0 3
undirected trail 0 1 2 0 3 2
two pieces: trail size=0
K4 (four odd vertices): trail size=0
directed multi trail 0 1 0 1 2 0
```

**Lexicographically smallest trail.** Sort every row by target when building the graph (`sort_nbrs = true`). Taking the smallest unused neighbour first then gives the smallest trail, because the walk is extended greedily and the reverse output is the final order. This is exactly what "Reconstruct Itinerary" asks.

Measured: a random closed walk of 1,000,000 arcs on 1,000,000 vertices (so an Euler circuit exists by construction) returns a trail of 1,000,001 vertices that starts and ends at the same vertex, in 152 / 164 ms (run 1 / run 2).

## 6. 2-SAT

A **2-SAT** formula is an AND of clauses, each an OR of two literals, such as `(x or y) and (not x or z)`. The general satisfiability problem is the standard example of an intractable one; with two literals per clause it is solvable in linear time, thanks to implication.

A clause `(a or b)` says: if `a` is false then `b` must be true, and if `b` is false then `a` must be true. So add the arcs `not a -> b` and `not b -> a`. Number the literals so that variable `v` is literal `2v` (true) and `2v + 1` (false); then the negation of `l` is `l ^ 1`. In the resulting **implication graph**:

- If a variable and its negation are in the **same** SCC, each implies the other: contradiction, the formula is unsatisfiable.
- If no variable shares an SCC with its negation, a satisfying assignment exists, and we can read it off: for each variable choose the literal whose component comes **later** in topological order. Everything a later literal implies comes later still, so choosing it forces nothing false. With Tarjan's numbering (sinks first, the reverse of topological order) the later literal has the smaller number, so `v` is true when `comp[2v] < comp[2v + 1]`.

```cpp
// 2-SAT: a formula that is an AND of clauses (a OR b), each with two literals. Literal 2v means "variable v is true", 2v + 1 "v is
// false"; l ^ 1 is the negation. A clause (a OR b) says: if a is false then b must be true, and if b is false then a must be true,
// i.e. the arcs (not a) -> b and (not b) -> a. The formula is satisfiable exactly when no variable shares a strongly connected
// component with its own negation. To read off an assignment: v is true if comp[2v] comes AFTER comp[2v + 1] in topological order,
// which with Tarjan's numbering (sinks first) means comp[2v] < comp[2v + 1].
struct Sat { bool ok = false; std::vector<char> val; };
Sat two_sat(int nvars, const std::vector<std::pair<int, int>>& clauses) {
    std::vector<Edge> arcs; for (auto [a, b] : clauses) { arcs.push_back({a ^ 1, b}); arcs.push_back({b ^ 1, a}); }
    Scc s = scc_tarjan(build_csr(2 * nvars, arcs, true)); Sat r; r.val.assign(nvars, 0); r.ok = true;
    for (int v = 0; v < nvars; ++v) { if (s.comp[2 * v] == s.comp[2 * v + 1]) { r.ok = false; return r; } r.val[v] = s.comp[2 * v] < s.comp[2 * v + 1]; }
    return r;
}
```

Trace (checked by running the code). The formula `(x0 or x1) and (not x0 or x1) and (not x1 or x2)` is satisfiable; the code returns all true. The formula with clauses `(x or x)` and `(not x or not x)` (forcing `x` and `not x`) is unsatisfiable, and so is the formula with all four combinations of two variables forbidden:

```
satisfiable=1 values: 1 1 1
x and not x: satisfiable=0
all four combinations forbidden: satisfiable=0
```

A clause of one literal is written as `(a or a)`. Random formulas with 1,000,000 variables and a given number of clauses per variable (run 1 / run 2, ms; the whole decision including building the graph):

| clauses per variable | satisfiable | time |
|---|---|---|
| 0.6 | yes | 128 / 135 |
| 1.0 | no | 236 / 250 |
| 1.4 | no | 395 / 411 |

At 0.6 clauses per variable the random formula was satisfiable, at 1.0 and 1.4 it was not. That agrees with the known result that random 2-SAT has a sharp threshold at one clause per variable (from memory; this experiment only shows one sample on each side). The time grows with the number of clauses, since the graph has two arcs per clause.

## 7. Choosing

| Question | Tool | Cost |
|---|---|---|
| Groups that reach each other, directed | Tarjan (default) or Kosaraju | O(V + E) |
| Order of dependencies when there are cycles | SCC, then topological order of the condensation | O(V + E) |
| Single points of failure, undirected | Low-link bridges and articulation points | O(V + E) |
| Use every edge once | Hierholzer after the degree test | O(V + E) |
| Smallest itinerary using every ticket | Hierholzer with sorted rows | O(V + E) plus the sort |
| Pairwise "this or that" constraints | 2-SAT through SCC | O(V + C), C clauses |
| Three literals per clause | Not this tool: the problem is the general one | |

## 8. What the tests check, and what they cannot

Of the 41 mutants, 36 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Five survive, and each is an equivalent change as far as the answers go: the mutated program returns the same answers as the original on every input, or differs only where the result is documented as unspecified. The reasons:

- **`low[w]` instead of `num[w]` for an arc to a vertex on the stack, in Tarjan's SCC.** Both give the same components: `low[w]` is never smaller than the discovery number of the root of `w`'s component. (In the bridge DFS the same edit is wrong and was caught.)
- **Starting the undirected trail at the last odd vertex instead of the first.** With exactly two odd vertices a trail exists from either one, so both starts are correct; only which valid trail is printed changes.
- **Accepting any odd-vertex count up to 2 instead of exactly 0 or 2.** The number of odd-degree vertices in an undirected graph is always even, so 1 cannot occur.
- **Dropping the check that the start and end counts are equal in the directed degree test.** The out minus in sums to zero over all vertices, so if every other vertex is balanced the counts are equal anyway.
- **Not returning at once when a variable shares a component with its negation in 2-SAT.** The loop continues, but `ok` is already false and the values are documented as meaningless for an unsatisfiable formula.

The graphs with a million vertices have no brute-force reference, so they are checked structurally: the ring is one SCC and has an Euler circuit that uses all arcs, the chain has a million singleton components, 999,999 bridges and 999,998 articulation points, the ring has neither bridges nor cut vertices, and Tarjan and Kosaraju agree on a random digraph with three million arcs. What the tests cannot do is show that the random-graph statistics (the threshold values) hold for other seeds; they are one sample per row.

Mutants are chosen where a bug is plausible, not at random, so "36 of 41" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Following an arc into a finished component in Tarjan.** Without the `comp[w] < 0` test, `low` can pick up a number from a closed component and merge two components that should stay apart.
2. **Using `low[w]` instead of `num[w]` for a back arc in the bridge search.** In the SCC code either works (the tests accept both), but in the undirected search it lowers `low` too far and the mutant using it was caught. Use `num[w]` in both and the question never arises.
3. **Mixing up the two numbering orders.** Tarjan numbers components sinks first, Kosaraju sources first. A topological order needs a reversal for one of them.
4. **Skipping the parent by vertex in the undirected DFS.** Parallel edges then look like bridges. Skip by edge id.
5. **Treating the DFS root like any other vertex.** It is an articulation point only with two or more children.
6. **Using `>=` for bridges or `>` for articulation points.** A bridge needs `low[w] > num[p]`; an articulation point needs `low[w] >= num[p]`. The two conditions differ by one character and the tests catch either swap.
7. **Starting the Euler walk anywhere.** With two odd vertices (or an out minus in of 1) the start is forced.
8. **Forgetting the "one piece" check.** The degrees can be right while the edges lie in two components; compare the trail length with m + 1.
9. **Assuming the output of Hierholzer is in walking order.** It is reversed.
10. **Reading the 2-SAT assignment with the wrong comparison.** Which side is "true" depends on the component numbering; with Tarjan it is `comp[2v] < comp[2v + 1]`.
11. **Recursive DFS on large input.** A chain of a million vertices overflows the default stack; the listings use an explicit stack.

## 10. Questions to answer out loud

1. Define a strongly connected component. Why is the condensation always a DAG?
2. In Tarjan's algorithm, what does `low[v]` mean, and when is `v` the root of a component?
3. Why must an arc to an already-assigned vertex be ignored?
4. Why does Kosaraju's second pass use the reverse graph, and why in decreasing finish order?
5. Which numbering do Tarjan and Kosaraju give the components? How do you get a topological order from each?
6. What is the difference between a bridge and an articulation point? State both low-link conditions.
7. Why is the DFS root special for articulation points but not for bridges?
8. Why do parallel edges need edge ids in the bridge search?
9. State the conditions for an Euler circuit and an Euler trail, undirected and directed.
10. Explain Hierholzer's algorithm. Why is the output reversed?
11. How would you get the lexicographically smallest Euler trail?
12. How does a 2-SAT clause become arcs? Why does "x and not x in one SCC" mean unsatisfiable, and how do you read an assignment?
13. Why does 3-SAT not fall to the same method?

## 11. Problems

### 11.1 On paper

**A.** Run Tarjan by hand on the arcs `0->1, 1->2, 2->0, 1->3, 3->4, 4->3, 2->4` (five vertices). Give the components with Tarjan's numbering.

**B.** Find the bridges and articulation points of the undirected graph with edges `0-1, 1-2, 2-0, 2-3, 3-4, 4-5, 5-3`.

**C.** Does the undirected graph with edges `0-1, 0-2, 1-2, 1-3, 2-3, 2-4, 3-4` (a "house") have an Euler trail? Where must it start and end?

**D.** Show with the implication graph that the formula `(x or y) and (not x or y) and (x or not y) and (not x or not y)` is unsatisfiable.

**E.** Why does every vertex of a bridge-free connected graph lie on a cycle? (Hint: take an edge on the vertex.)

**F.** In a directed graph, is a vertex with in-degree 0 always a component by itself? Is a vertex with in-degree 1 and out-degree 1 always on a cycle?

**G.** Prove that a graph with an Euler circuit has every vertex of even degree (undirected case).

### 11.2 On LeetCode

For each: what are the vertices and edges, and which tool? Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Critical Connections in a Network | 1192 | Bridges (section 4) |
| Reconstruct Itinerary | 332 | Euler trail with sorted rows (section 5) |
| Valid Arrangement of Pairs | 2097 | Directed Euler trail; the start is the vertex with out minus in = 1 |
| Minimum Number of Days to Disconnect Island | 1568 | Count islands; if not 1, answer 0; else look for an articulation point in the grid; the answer is at most 2 |
| Maximum Employees to Be Invited to a Meeting | 2127 | Functional graph (each vertex has one outgoing arc): cycles and chains hanging on them |

### 11.3 In your editor

**A.** Write a recursive Tarjan, show that it crashes on a chain of 10^6 vertices with the default stack, and time it against the iterative listing on a graph where it does not crash.

**B.** Given the condensation, compute the largest number of vertices of the original graph on any path of components (a longest path in a DAG with vertex weights).

**C.** Write biconnected components (the blocks between articulation points) by pushing edges on a stack in the bridge DFS.

**D.** Write a checker that, given a graph and a claimed set of components, verifies them by two BFS runs per pair of sample vertices; use it on your own Tarjan.

**E.** Solve a small scheduling puzzle with 2-SAT: each of 10 meetings gets one of two time slots, and some pairs may not share a slot.

### Answers

**A.** Components (Tarjan numbering): `{3,4}` is component 0 (a sink: nothing leaves it), `{0,1,2}` is component 1. The code prints `1 1 1 0 0` with count 2.

**B.** The only bridge is `2-3`; the articulation points are 2 and 3. The code prints exactly that.

**C.** Degrees 2, 3, 4, 3, 2: vertices 1 and 3 are odd, so a trail exists and must start at one of them and end at the other. The code finds a trail with 8 vertices starting at 1 and ending at 3.

**D.** The four clauses give the arcs `not x -> y` and `not y -> x`; `x -> y` and `not y -> not x`; `not x -> not y` and `y -> x`; `x -> not y` and `y -> not x`. Then `x -> y -> not x` and `not x -> y -> x`, so `x` and `not x` are in one SCC (and likewise `y` and `not y`). The code reports unsatisfiable.

**E.** Take an edge on the vertex; since it is not a bridge, removing it leaves its two ends connected, so a path joins them, and with the edge it forms a cycle through the vertex.

**F.** Yes for the first: nothing reaches a vertex of in-degree 0, so no other vertex can reach it back and it is a singleton component. No for the second: the arc into it may come from a vertex that is never reached again, for example a path `a -> v -> b` with nothing else.

**G.** Each time the circuit passes through a vertex it uses one edge to enter and one to leave, so each visit uses two edges; the start uses one to leave and one to return. The edges at a vertex are therefore used in pairs.

**11.2 hints.** 1192: the sample with four servers and connections `[[0,1],[1,2],[2,0],[1,3]]` has the one critical connection `[1,3]` (checked by running the code). 332: the sample `JFK->SFO, JFK->ATL, SFO->ATL, ATL->JFK, ATL->SFO` gives `JFK ATL JFK SFO ATL SFO` (checked). 2097: the sample pairs `[5,1], [4,5], [11,9], [9,4]` give `11 9 4 5 1` (checked).

**11.3 hints.** C: push each tree and back edge on a stack; when `low[w] >= num[p]`, pop to that edge to get one block.

---

## Cheat sheet

- **SCC:** Tarjan, one pass, low-link plus a stack, pop when `low == num`; components come out sinks first. Kosaraju: DFS, then the reverse graph in decreasing finish order; sources first.
- **Condensation:** always a DAG; topological order applies.
- **Bridge:** tree edge `(p, w)` with `low[w] > num[p]`. **Articulation point:** `low[w] >= num[p]` for a non-root; the root needs two children.
- **Parallel edges:** skip the arrival by edge id.
- **Euler:** undirected, 0 or 2 odd vertices; directed, balanced with at most one start and one end; Hierholzer with a stack, output reversed; compare the length with m + 1.
- **Smallest itinerary:** sort the rows.
- **2-SAT:** clause `(a or b)` gives `not a -> b` and `not b -> a`; unsatisfiable iff some variable shares an SCC with its negation; true iff `comp[2v] < comp[2v + 1]` (Tarjan).
- **Giant component:** appears in a random digraph between E/V = 1 and 2.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Tarjan's algorithm (1972), Kosaraju's algorithm, Hierholzer's algorithm and Euler's degree condition, and the linear-time 2-SAT method through implication graphs (Aspvall, Plass and Tarjan, 1979) are standard (named and dated from memory). The thresholds for the giant component and for random 2-SAT are from memory; this document only measures one sample on each side.
- Problem numbers and titles in section 11, and the samples of problems 1192, 332 and 2097, are LeetCode's, from memory; check a number before you rely on it.
