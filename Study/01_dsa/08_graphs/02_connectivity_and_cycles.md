# DSA 08.02: Connectivity and Cycles

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Graph representations and traversal](01_representations_and_traversal.md) (CSR, BFS, iterative DFS, edge kinds, the grid as a graph) and [Recursion fundamentals](../06_recursion_and_backtracking/01_recursion_fundamentals.md) (the call-stack limit is measured again here). There is no companion demo file: every structure is listed in this document.*

Topic 01 gave you two walks. This topic uses them to answer the first four questions anyone asks about a graph: **is it in one piece** (and if not, how many pieces), **does it contain a cycle**, **can its vertices be split into two sides with every edge crossing** (bipartite), and, on a grid, **how big is the blob I am standing in** (flood fill). They look like four topics. They are one: a traversal with a different thing written down at each step. The interesting parts are the places where the obvious code is wrong in a way small examples do not show: skipping the edge you arrived by, treating a finished vertex like a vertex on the current path, climbing a tree to turn "not bipartite" into a proof, and recolouring a region with its own colour.

By the end you can:

- label the connected components of an undirected graph by traversal and by union-find, and say when each is the right tool;
- find a cycle in an undirected graph (including self-loops and parallel edges) and in a directed one, and return the cycle itself, not just a yes or no;
- explain why the undirected rule "a seen vertex means a cycle" is wrong for directed graphs, with the smallest counterexample;
- two-colour a graph, and when that fails return an **odd cycle** as the proof;
- count regions of a grid and flood-fill one, in four or eight directions, without recursion;
- say how the structure of a random graph changes at an average degree of one (measured), and when recursion depth makes a cycle check a bug (measured).

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 71,241 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: component labels from both methods against a transitive closure on random graphs with self-loops and parallel edges (every pair of vertices, and the labels numbered in order of the smallest vertex); the undirected cycle finder against the counting rule of section 3, with every returned cycle checked edge by edge for distinct vertices and distinct edges; the directed cycle finder against "some vertex reaches itself in one or more steps" computed by Warshall's closure, with every returned cycle walked arc by arc; the bipartite test against all 2^n colourings for n up to 9, with every returned odd cycle checked for odd length and for being a cycle; random grids against a separate recursive count (four and eight directions) and flood fill against a recursive reference, including the same-colour case; and rings and paths of a million vertices and grids of nine million cells, none of which may recurse. Then 67 hand-made mutants of the listings were run against the tests: **62 were caught and 5 are equivalent changes** (section 9).

**Data and timing.** Graphs are **synthetic** and seeded: V = 1,000,000 vertices with E = (E/V) * V edges whose endpoints are uniform random (so self-loops and parallel edges occur), and 2000 by 2000 grids in which each cell is open with a given probability. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run (not best of several), and a claim is made only when it holds in both runs; they are this machine's, not yours. Component counts, sizes and exit codes are identical in both runs.

---

## 1. Vocabulary

Two vertices are **connected** if a path joins them (in an undirected graph; direction is dealt with in section 4). Being connected is an equivalence relation, and its classes are the **connected components**. A graph is **connected** if it has one component. A **cycle** is a path that returns to its start, using no edge twice and no vertex twice (except the start). Two special cases matter in practice: a **self-loop** `(v, v)` is a cycle of length 1, and two **parallel edges** between a and b are a cycle of length 2, even though no *third* vertex is involved. A **forest** is a graph with no cycle and a **tree** is a connected forest. A graph is **bipartite** if its vertices can be split into two sets so that every edge has one end in each (equivalently: it can be coloured with two colours, no edge joining two vertices of the same colour). An **odd cycle** is a cycle of odd length.

Three counting facts you will use constantly. A forest on V vertices with c components has exactly **V - c** edges. A tree therefore has **V - 1** edges, and a connected graph with V - 1 edges is a tree. Any graph with more than V - c edges has a cycle. These are not tricks; section 3 proves the second and uses it as a test oracle.

## 2. Connected components

### 2.1 By traversal

The idea is the outer loop of every traversal from Topic 01: for each vertex in order, if it has no label yet, start a search from it, give everything the search reaches the next label, and add one to the count. The search itself can be BFS or DFS; nothing in the answer depends on the order.

```cpp
using Edges = std::vector<std::pair<int, int>>;
// Compressed rows as in Graphs 01, plus eid[i] = the index in the input of the edge behind arc i.
// An undirected edge is two arcs with the same eid, which is how a search tells "the edge I came by" from "another edge to the same vertex".
struct Csr { int n = 0; std::vector<int> off, to, eid; };
Csr build_csr(int n, const Edges& e, bool directed) {
    Csr g; g.n = n; g.off.assign(n + 1, 0);
    for (auto [a, b] : e) { ++g.off[a + 1]; if (!directed) ++g.off[b + 1]; }
    for (int u = 0; u < n; ++u) g.off[u + 1] += g.off[u];
    g.to.resize(g.off[n]); g.eid.resize(g.off[n]); std::vector<int> at(g.off.begin(), g.off.end() - 1);
    for (size_t id = 0; id < e.size(); ++id) {
        auto [a, b] = e[id]; g.to[at[a]] = b; g.eid[at[a]++] = id;
        if (!directed) { g.to[at[b]] = a; g.eid[at[b]++] = id; }
    }
    return g;
}
```

`Csr` is Topic 01's compressed-row graph with one addition, `eid`: the index, in the input list, of the edge behind each arc. An undirected edge is two arcs with the **same** `eid`. Section 3 needs it; this section does not.

```cpp
// Connected components of an UNDIRECTED graph. label[v] = 0, 1, 2, ... in order of the smallest vertex of each component.
// Marking a vertex when it is pushed is fine here: we need the set that is reachable, not the shape of a depth-first tree.
struct Components { std::vector<int> label; int count = 0; };
Components components(const Csr& g) {
    Components r; r.label.assign(g.n, -1); std::vector<int> st;
    for (int s = 0; s < g.n; ++s) {
        if (r.label[s] >= 0) continue;
        r.label[s] = r.count; st.push_back(s);
        while (!st.empty()) {
            int u = st.back(); st.pop_back();
            for (int i = g.off[u]; i < g.off[u + 1]; ++i) if (r.label[g.to[i]] < 0) { r.label[g.to[i]] = r.count; st.push_back(g.to[i]); }
        }
        ++r.count;
    }
    return r;
}
// The same answer from the edge list, with union-find: no adjacency structure at all, and edges can arrive one at a time.
struct Dsu {
    std::vector<int> p, sz; int sets;
    explicit Dsu(int n) : p(n), sz(n, 1), sets(n) { for (int i = 0; i < n; ++i) p[i] = i; }
    int find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }          // path halving
    bool unite(int a, int b) {                                                             // false if a and b were already together: that edge closes a cycle
        a = find(a); b = find(b); if (a == b) return false;
        if (sz[a] < sz[b]) std::swap(a, b);
        p[b] = a; sz[a] += sz[b]; --sets; return true;
    }
};
Components components_dsu(int n, const Edges& e) {
    Dsu d(n); for (auto [a, b] : e) d.unite(a, b);
    Components r; r.label.assign(n, -1); std::vector<int> id(n, -1);
    for (int v = 0; v < n; ++v) { int root = d.find(v); if (id[root] < 0) id[root] = r.count++; r.label[v] = id[root]; }
    return r;
}
bool is_tree(int n, const Edges& e) { return (int)e.size() == n - 1 && components_dsu(n, e).count == 1; }   // connected with V-1 edges (so n = 0 never qualifies)
```

Two details. First, the vertices are marked when they are **pushed**, which Topic 01 section 5 showed is wrong for DFS if you want the depth-first *tree*. Here you do not: you want the *set* reachable from the start, and any order of visiting gives the same set. The stack never holds a vertex twice, so its size is at most V (not E, as in the pop-time variant). Second, because the loop scans `s` upward and labels in order, the label of a component is the rank of its smallest vertex, which makes the output canonical: two correct methods give *equal* vectors, and the tests compare them with `==`.

Cost: O(V + E) time, O(V) extra space beyond the graph.

### 2.2 By union-find

The second method never builds a graph. It starts with V singleton sets and, for each edge, joins the sets of its two ends. `Dsu::unite` returns **false** when both ends were already in one set, and that is exactly an edge that closes a cycle: the first useful by-product, used in section 3 and again for the minimum spanning tree in Topic 07. `find` uses path halving (each step on the way up points the vertex at its grandparent), and `unite` hangs the smaller set under the larger, which together make a sequence of m operations cost O(m alpha(V)), where alpha is the inverse Ackermann function (below 5 for any size that fits in memory).

The traversal is preferable when you already have the adjacency structure or need paths, distances or the colouring; the union-find is preferable when edges **arrive one at a time** and you want the component count after each (a traversal would have to start over), or when the input is an edge list you would otherwise have to convert. The tests check both give the same labels, and section 7 times them.

`is_tree` is the counting fact of section 1 in code: V - 1 edges and one component. Both conditions are needed. V - 1 edges with a cycle leave a component unreached (a triangle plus an isolated vertex has V = 4, E = 3), and a connected graph with more edges has a cycle. A single vertex with no edges is a tree; zero vertices is not, because it would need -1 edges.

### 2.3 Trace

The graph has nine vertices and the edges `(0,1) (1,2) (3,4) (5,5) (6,7) (7,8) (8,6)`: a path of three, an edge, a vertex with a self-loop, and a triangle.

```text
labels [0 0 0 1 1 2 3 3 3]
count 4 dsu_count 4 same 1
edge 0 (0,1) merged sets=8
edge 1 (1,2) merged sets=7
edge 2 (3,4) merged sets=6
edge 3 (5,5) CYCLE sets=6
edge 4 (6,7) merged sets=5
edge 5 (7,8) merged sets=4
edge 6 (8,6) CYCLE sets=4
```

The labels are `0 0 0 1 1 2 3 3 3`: four components, numbered by smallest vertex. In the union-find run the number of sets starts at 9 and falls with each successful `unite`: after edge 0 it is 8, and so on to 4. Edges 3 and 6 print `CYCLE` and leave it unchanged. Edge 3 is the self-loop (a vertex is always in its own set already) and edge 6, `(8,6)`, closes the triangle because 6 and 8 were joined through 7. Without looking at a drawing, the output says: 9 vertices, 4 components, so a forest would have 5 edges; there are 7, so exactly 2 edges too many, and exactly 2 `CYCLE` lines.

## 3. Cycles in an undirected graph

### 3.1 The counting test, and why a search is still needed

By section 1 a graph has a cycle exactly when E > V - c. That answers "is there one" in O(V + E), by counting components. It does not say *where*, and interviews usually want the cycle (or want you to stop at the first one without exploring the rest). The search below returns the cycle itself.

*Proof that more than V - c edges force a cycle:* take a spanning forest of the graph (one search tree per component). It has V - c edges. Any further edge joins two vertices already connected by a path in the forest, and the path plus that edge is a cycle. The proof is also the algorithm, and it is what `Dsu::unite` returning false means.

### 3.2 The search

Run a depth-first search. When it looks at an arc to a vertex that is already discovered, that is a non-tree edge, and in an undirected graph every non-tree edge is a **back edge** (Topic 01, section 5): its far end is an ancestor of the current vertex. The cycle is the tree path from the ancestor down to the current vertex, closed by that edge.

There is one trap, and it is the reason this is more than a one-liner. Every tree edge is also an arc **back to the parent**, because the graph is undirected. If "already discovered" counted that arc, every graph with an edge would "have a cycle". The usual fix is to skip the parent *vertex*. That is wrong for **parallel edges**: two edges between a and b make a cycle of length 2, and skipping every arc that goes to the parent vertex skips both. The correct fix skips the **edge** by which we arrived, identified by its `eid`.

```cpp
// A cycle in an undirected graph, as vertices v0 .. vk-1 (consecutive ones, and the last with the first, are joined by distinct edges),
// or empty if the graph is a forest. A self-loop is the cycle [v]; two parallel edges are the cycle [a, b].
// The edge we arrived by is skipped by its id, not by its far end: skipping "the parent vertex" would miss parallel edges.
std::vector<int> find_cycle_undirected(const Csr& g) {
    int n = g.n; std::vector<int> par(n, -1), pare(n, -1); std::vector<char> seen(n, 0); std::vector<std::pair<int, int>> st;
    for (int s = 0; s < n; ++s) {
        if (seen[s]) continue;
        seen[s] = 1; st.push_back({s, g.off[s]});
        while (!st.empty()) {
            int u = st.back().first; int& i = st.back().second;
            if (i == g.off[u + 1]) { st.pop_back(); continue; }
            int v = g.to[i], id = g.eid[i]; ++i;
            if (id == pare[u]) continue;
            if (!seen[v]) { seen[v] = 1; par[v] = u; pare[v] = id; st.push_back({v, g.off[v]}); }
            else { std::vector<int> cyc; for (int x = u; ; x = par[x]) { cyc.push_back(x); if (x == v) break; } return cyc; }   // v is an ancestor of u (or u): walk up to it
        }
    }
    return {};
}
```

Each stack entry is a vertex and the index of the next arc to look at, so the search is iterative: it uses no recursion and the stack depth is bounded by the length of the current path, in a heap-allocated vector. `seen[v]` is the visited flag. When a seen vertex `v` is found, `x` climbs from `u` through `par` until it reaches `v`; the guarantee that `v` is on that climb is the back-edge fact above. One more point makes the guarantee hold: the first time a non-tree arc is looked at, it is looked at from the *descendant* end, because when the ancestor reaches its arc to a descendant that was already discovered, that descendant has examined the same edge from its side and the function has already returned.

The function returns at the first cycle, so on a graph with a cycle near the start it does a tiny fraction of the work of the counting test (section 7 has the numbers).

### 3.3 Trace

```text
undirected cycle [5]
parallel edges cycle [1 0]
path cycle []
```

Reading the first three lines. The nine-vertex graph of section 2 gives `[5]`: the search runs through the path `{0,1,2}` and the edge `{3,4}` without finding anything, then starts at 5 and sees its own self-loop: `v == u`, so the climb stops at once. The graph with edges `(0,1) (1,0) (1,2)` gives `[1 0]`, the parallel pair. And the path `0-1-2-3` gives an empty vector.

## 4. Cycles in a directed graph

### 4.1 Why the undirected rule fails

Take the four arcs `0 to 1`, `0 to 2`, `1 to 3`, `2 to 3` (a diamond). A depth-first search from 0 goes 0, 1, 3, then back to 0 and on to 2, and at 2 it sees the arc to 3, **already discovered**. If "seen means cycle" were the rule, this would report one. But there is no cycle: no path leads from 3 back to 0, 1 or 2. In a directed graph an arc to a discovered vertex can be a **forward** or **cross** edge (harmless) as well as a **back** edge (a cycle), and the rule must tell them apart.

### 4.2 Three colours

Topic 01's intervals tell them apart, and the simplest implementation is three states per vertex. **White**: not yet reached. **Grey**: reached, still on the stack, so on the *current path*. **Black**: finished, all its arcs examined. An arc to a white vertex is a tree edge; to a **black** vertex, a forward or cross edge, harmless (that vertex cannot reach the current path, or it would not be finished without having found it); to a **grey** vertex, a **back edge**, which closes a cycle. A directed graph has a cycle if and only if some search finds a grey target (sketch: if it has a cycle, the first vertex of the cycle to be discovered is grey when the search reaches the cycle's last arc, because every other vertex of the cycle is a descendant of it).

To return the cycle and not only a yes, remember each grey vertex's position on the stack in `pos[]`. When the arc `u to v` reaches a grey `v`, the cycle is exactly the stack from `pos[v]` to the top.

```cpp
// A cycle in a DIRECTED graph, as vertices v0 -> v1 -> ... -> vk-1 -> v0, or empty if the graph is acyclic. Three colours:
// white = not seen, grey = on the current path (on the stack), black = finished. An arc to a grey vertex closes a cycle.
std::vector<int> find_cycle_directed(const Csr& g) {
    int n = g.n; std::vector<char> color(n, 0); std::vector<int> pos(n, -1); std::vector<std::pair<int, int>> st;
    for (int s = 0; s < n; ++s) {
        if (color[s]) continue;
        color[s] = 1; pos[s] = 0; st.push_back({s, g.off[s]});
        while (!st.empty()) {
            int u = st.back().first; int& i = st.back().second;
            if (i == g.off[u + 1]) { color[u] = 2; st.pop_back(); continue; }
            int v = g.to[i++];
            if (color[v] == 0) { color[v] = 1; pos[v] = st.size(); st.push_back({v, g.off[v]}); }
            else if (color[v] == 1) { std::vector<int> cyc; for (size_t k = pos[v]; k < st.size(); ++k) cyc.push_back(st[k].first); return cyc; }
        }
    }
    return {};
}
bool is_dag(const Csr& g) { return find_cycle_directed(g).empty(); }
```

`is_dag` is the same function read as a question. Topic 03 reuses the black/grey/white machinery and adds the order in which vertices turn black, which is a topological order when there is no cycle.

### 4.3 Trace

```text
directed cycle [1 2 3]
diamond is_dag 1
```

The six arcs `0 to 1`, `1 to 2`, `2 to 3`, `3 to 1`, `0 to 4`, `4 to 5` have the cycle `1 to 2 to 3 to 1` reached from 0 through a tail. The search finds it when the arc `3 to 1` sees vertex 1 still grey, and reports exactly the stack from 1: `[1 2 3]`. Vertex 0 is *not* in the answer, although it is on the stack: `pos[1]` is 1, not 0. And the diamond is acyclic, as computed above.

## 5. Bipartite graphs

### 5.1 Two-colouring by BFS

Give the start vertex colour 0 and every neighbour the opposite colour; keep going. If the whole graph can be coloured without an edge whose ends match, it is bipartite. If an edge joins two vertices of the same colour, it cannot be, and we want the *reason*.

**Theorem.** A graph is bipartite if and only if it has no odd cycle.

*(Only if.)* Around a cycle the colours must alternate, so an odd cycle would need colour 0 next to colour 0 somewhere. *(If.)* Suppose the BFS finds an edge `(u, v)` with the same colour. Colour is the parity of the BFS depth, so `depth[u]` and `depth[v]` have the same parity. In an undirected graph an edge never joins vertices more than one level apart, so the depths are in fact **equal**. Let `w` be the lowest common ancestor of `u` and `v` in the BFS tree, at depth `d`. The path from `w` down to `u`, the edge `(u, v)`, and the path from `v` up to `w` form a cycle of length `(depth[u] - d) + 1 + (depth[v] - d)` = `2 (depth[u] - d) + 1`, which is odd. So a same-colour edge always comes with an odd cycle, and the code builds it.

```cpp
// Two-colouring of an UNDIRECTED graph by BFS: neighbours get opposite colours. If an edge joins two vertices of the SAME colour the graph is
// not bipartite, and the proof is an odd cycle: the two tree paths up to their meeting point plus that edge.
struct BipResult { bool ok = true; std::vector<int> color, odd_cycle; };
BipResult bipartite(const Csr& g) {
    int n = g.n; BipResult r; r.color.assign(n, -1); std::vector<int> par(n, -1), depth(n, 0), q;
    for (int s = 0; s < n; ++s) {
        if (r.color[s] >= 0) continue;
        r.color[s] = 0; q.assign(1, s);
        for (size_t h = 0; h < q.size(); ++h) {
            int u = q[h];
            for (int i = g.off[u]; i < g.off[u + 1]; ++i) {
                int v = g.to[i];
                if (r.color[v] < 0) { r.color[v] = 1 - r.color[u]; par[v] = u; depth[v] = depth[u] + 1; q.push_back(v); }
                else if (r.color[v] == r.color[u]) {
                    std::vector<int> a, b; int x = u, y = v;                                // climb the deeper one until they meet
                    while (x != y) { if (depth[x] >= depth[y]) { a.push_back(x); x = par[x]; } else { b.push_back(y); y = par[y]; } }
                    a.push_back(x); a.insert(a.end(), b.rbegin(), b.rend());
                    r.ok = false; r.odd_cycle = a; return r;
                }
            }
        }
    }
    return r;
}
```

To build the cycle, climb from `u` and `v` towards the root one step at a time, always from the deeper one (ties: from `u`), until they meet at `w`. The climbed vertices of `u`, then `w`, then the climbed vertices of `v` in reverse, make the cycle. Each vertex appears once because the two climbs are in different subtrees below `w`. The outer loop over `s` handles disconnected graphs: each component is coloured on its own, and **one odd component makes the whole graph non-bipartite** (the tests include a graph whose only odd cycle is in the second component).

A graph with a **self-loop** is not bipartite (the loop joins a vertex to itself, same colour): `v == u` gives the cycle `[u]`, length 1, odd.

### 5.2 Trace

```text
C5 ok 0
C5 odd cycle [2 1 0 4 3]
C6 ok 1
C6 color [0 1 0 1 0 1]
tail ok 0
tail odd cycle [2 1 3]
```

On the 5-cycle `0 1 2 3 4`, BFS from 0 gives 1 and 4 colour 1, and 2 and 3 colour 0; the edge `(2, 3)` is the clash. Both are at depth 2, so the climb goes `2, 1` on one side and `3, 4` on the other until both reach 0: the odd cycle is `[2 1 0 4 3]`, length 5 (the other end of the clash edge, `3`, closes it back to 2). The 6-cycle colours cleanly as `0 1 0 1 0 1`. The third graph has a triangle `1 2 3` with a tail `0-4-5-6` and an edge `0-1`; the cycle returned is `[2 1 3]`, found without ever touching the tail's vertices because the climb stops at 1, the common ancestor.

## 6. Grids: regions and flood fill

A grid of cells is a graph whose edges are "adjacent and both open" (Topic 01, section 7). Counting the connected components of that graph is the classic **number of islands**, and the size of each is the area. Nothing is stored: the neighbour offsets are computed. Both listings use an explicit stack for the reason the next section measures.

```cpp
// Regions of a grid: cells equal to `land`, joined by 4 moves (8 if diag). Depth-first with an explicit stack; the copy of the grid doubles as the visited set.
struct Regions { int count = 0, largest = 0; };
Regions grid_regions(std::vector<std::string> g, char land = '1', bool diag = false) {
    int H = g.size(), W = H ? g[0].size() : 0; Regions r; std::vector<int> st;
    for (int s = 0; s < H * W; ++s) {
        if (g[s / W][s % W] != land) continue;
        ++r.count; int size = 0; g[s / W][s % W] = 0; st.push_back(s);                    // mark when pushed
        while (!st.empty()) {
            int u = st.back(); st.pop_back(); ++size;
            for (int dr = -1; dr <= 1; ++dr) for (int dc = -1; dc <= 1; ++dc) {
                if ((!dr && !dc) || (!diag && dr && dc)) continue;
                int nr = u / W + dr, nc = u % W + dc;
                if (nr >= 0 && nr < H && nc >= 0 && nc < W && g[nr][nc] == land) { g[nr][nc] = 0; st.push_back(nr * W + nc); }
            }
        }
        r.largest = std::max(r.largest, size);
    }
    return r;
}
// Flood fill: recolour the region of (r, c). Returns the number of cells changed. If the new colour equals the old one there is nothing to do,
// and the early return is what stops a "recolour, then look for cells of the old colour" loop from running forever.
int flood_fill(std::vector<std::string>& g, int r, int c, char to) {
    char from = g[r][c]; if (from == to) return 0;
    int H = g.size(), W = g[0].size(), changed = 0; std::vector<int> st{r * W + c}; g[r][c] = to;
    while (!st.empty()) {
        int u = st.back(); st.pop_back(); ++changed;
        static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; ++k) {
            int nr = u / W + DR[k], nc = u % W + DC[k];
            if (nr >= 0 && nr < H && nc >= 0 && nc < W && g[nr][nc] == from) { g[nr][nc] = to; st.push_back(nr * W + nc); }
        }
    }
    return changed;
}
```

`grid_regions` copies the grid (it is taken by value) and uses the copy as its visited set: writing `0` into a cell removes it from the land. Marking on push is correct for the same reason as in section 2: only the set matters. `diag = true` adds the four diagonal moves, which is what "islands connected diagonally" asks for.

`flood_fill` is the paint-bucket tool: recolour the region of `(r, c)` with colour `to`. The `if (from == to) return 0` line is the trap. Without it, recolouring a cell to the colour it already has leaves the cell looking like a not-yet-visited cell of the old colour, and a search that tests "is this cell still the old colour" never stops: the stack refills forever. The visited set *is* the recolouring, so when the two colours are equal the visited set is empty. It is the single most common bug in this problem.

```text
4-dir regions 3 largest 4
8-dir regions 1 largest 7
fill changed 3
zzb
zbb
bbb
fill same colour changed 0
```

The grid `11000 / 11000 / 00100 / 00011` has three regions with four moves (a square of four, one cell, two cells; the largest is 4) and **one** region with eight moves (the cell at row 2 touches the square diagonally and touches the pair diagonally; the largest is 7, the whole land). The fill of `aab / abb / bbb` from the top-left corner changes three cells (the `a` region), and filling `aa` with `a` changes none.

## 7. Measured

### 7.1 When does a random graph fall apart?

V = 1,000,000 and E chosen as a multiple of V, endpoints uniform. The table gives the component count, the size of the largest component, and the time of three algorithms (ms, run 1 / run 2): the stack labelling of section 2.1 (on a prebuilt CSR), the union-find of section 2.2 (on the edge list), and the undirected cycle finder of section 3.

| E / V | Components | Largest | Has a cycle | Stack labelling | Union-find | Cycle finder |
|---|---|---|---|---|---|---|
| 0.25 | 750,000 | 30 | no | 18.6 / 16.4 | 21.7 / 19.3 | 22.0 / 18.9 |
| 0.50 | 500,005 | 21,629 | yes | 42.3 / 42.7 | 32.1 / 36.8 | 0.9 / 1.0 |
| 1.00 | 161,713 | 797,051 | yes | 106.3 / 94.9 | 49.7 / 45.8 | 0.8 / 0.9 |
| 2.00 | 18,995 | 980,285 | yes | 113.7 / 109.8 | 63.9 / 60.1 | 1.6 / 1.7 |

Three things stand out. **The graph is a forest below one half and has a giant component above it.** At E / V = 0.25 the count is exactly V - E = 750,000: no edge closed a cycle, and the largest component is tiny (30 vertices). At E / V = 0.5, which is an *average degree* of 1, the largest component is 21,629 vertices, about two percent. At E / V = 1 it holds about four fifths of the vertices (797,051) and at 2 about 98 percent (980,285). The jump near E / V = 1/2 is the classical threshold of random graphs; the sizes match the textbook formulas, and I state that comparison from memory rather than from a source I checked here. **The cycle finder is nearly free once cycles are plentiful.** With a cycle somewhere early it stops after a millisecond or two, while with none (E / V = 0.25) it must see everything and costs the full 22.0 / 18.9 ms. **Union-find beats the stack labelling** from E / V = 0.5 up, in both runs, by a factor of 1.8 to 2.1 at E / V = 1 and 2: it touches each edge once, in input order, rather than hopping through a CSR. The stack version is faster at 0.25 in both runs. The stack version's times exclude building the CSR, which the union-find does not need, so the real gap is larger than the table shows.

### 7.2 The same recursion bug, again

A directed ring of 10^6 vertices (the longest possible search path) with the stack limit set to 8 MB, in a child process:

| Variant | Exit |
|---|---|
| Recursive, 1,000,000 vertices | killed by a signal |
| Iterative (section 4), 1,000,000 vertices | exit 0 |
| Recursive, 100,000 vertices | exit 0 |

The recursive three-colour check is the textbook one (about ten lines). It works at 100,000 vertices and dies at a million, killed by a signal. The listing of section 4 does not. This is the same fact as Topic 01 section 6, and the same advice: when the depth is not bounded by something you control, the stack of the search belongs in the heap.

### 7.3 Percolation on a grid

A 2000 by 2000 grid in which each cell is open with probability p. Regions are counted with `grid_regions` (four moves):

| p | Regions | Largest region as % of open cells | ns per open cell |
|---|---|---|---|
| 0.50 | 263,085 | under 0.05 | 29.1 / 28.5 |
| 0.55 | 175,552 | 0.2 | 27.7 / 28.0 |
| 0.60 | 101,971 | 73.1 | 27.5 / 27.2 |
| 0.65 | 56,555 | 94.8 | 33.7 / 25.1 |
| 0.70 | 29,251 | 98.3 | 29.7 / 23.4 |

Between p = 0.55 and p = 0.60 the picture changes from dust to a continent: the largest region goes from 0.2 percent of the open cells to 73.1. The known critical probability for site percolation on the square lattice is about 0.5927, which lies in that interval; I give the value from memory and did not check it here. Below it the largest region is under half a percent of the open cells, above it the largest region holds most of them. The cost is between 20 and 35 ns per open cell in both runs, and it does not depend on p: the work is one visit per open cell and four looks at neighbours.

## 8. Choosing

| Question | Use | Cost |
|---|---|---|
| How many pieces, offline | Stack labelling on CSR, or union-find on the edge list | O(V + E) |
| Pieces while edges are added | Union-find (`sets` after each `unite`) | O(alpha(V)) per edge |
| Is it a tree | `is_tree` (V - 1 edges and one component) | O(V + E) |
| Is there an undirected cycle, only yes or no | Count: E > V - c | O(V + E) |
| Undirected cycle, as a list | `find_cycle_undirected` (skip the arrival *edge*) | up to O(V + E), stops early |
| Directed cycle or "is it a DAG" | Three colours, `pos[]` to extract it | O(V + E) |
| Can I split into two sides | BFS two-colouring; on failure the odd cycle | O(V + E) |
| Regions or areas of a grid | `grid_regions` | O(cells) |
| Recolour a region | `flood_fill`, with the equal-colour early return | O(cells in region) |

## 9. What the tests check, and what they cannot

Of the 67 mutants, 62 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Five survived, and each is an equivalent change: the mutated program gives the same answers as the original on every input, so no test could tell them apart. The reasons:

- **`is_tree` with `<= n - 1` edges instead of `== n - 1`.** A connected graph has at least V - 1 edges, so once the component count is 1 the two conditions agree.
- **The extra guard `|| par[x] < 0` in the cycle climb of `find_cycle_undirected`.** The climb always meets `v` before it runs out of ancestors (section 3.2), so the guard never fires. It is a defensive check, not a behaviour.
- **`color[v] == 0 && v != u` in the directed search.** When `v == u` (a self-loop) the vertex is grey, not white, so the extra test is already false for the only case it excludes.
- **Climbing from `v` instead of `u` on equal depths in `bipartite`.** Both orders return a valid odd cycle of the same length through the same vertices; only the order in which the list starts differs, and the specification promises a cycle, not a starting point.
- **`nc <= W` instead of `nc < W` in `grid_regions`.** The cell one past the end of a `std::string` row is the terminating `'\0'`, which is a legal read and is never the land character `'1'`. (It would matter if `land` were `'\0'`; the function is not meant to be called that way.)

I wrote these five as plausible slips or defensive checks, and classed them as equivalent by reading the code, not because the tests passed.

Mutants are chosen where a bug is plausible, not at random, so "62 of 67" is a statement about those edits and not a proof about the listings. What the tests cannot check: behaviour on graphs of more than a few thousand vertices *except* the million-vertex rings, paths and nine-million-cell grids, where only one structure (a single big cycle or region) is exercised; and the timing claims, which depend on the machine.

## 10. Common mistakes

- Skipping the parent **vertex** instead of the arrival **edge** in an undirected cycle search: parallel edges disappear.
- Using the undirected rule ("seen means cycle") on a directed graph: every diamond is reported as a cycle. Use grey, not "visited".
- Forgetting that a self-loop is a cycle of length 1, in both the directed and undirected cases, and that it makes a graph non-bipartite.
- Colouring only the component of vertex 0 in a bipartite test: every component needs its own start.
- Saying "no odd cycle" and "bipartite" are different statements; they are the same theorem, and the failure case gives you the cycle for free.
- Flood-filling to the colour the cell already has, and looping forever.
- Counting components by "number of searches" but starting the search from the wrong set (for a grid, starting from every cell instead of every *unvisited open* cell).
- Using recursion for the search on an input whose path can have 10^6 vertices.
- Checking `E == V - 1` for a tree and forgetting connectivity (or the reverse).
- Reporting "edges closed" from a union-find as the *number of cycles*: it counts the independent cycles (E - V + c), not every cycle in the graph.

## 11. Questions to answer out loud

1. **How do you count connected components?** One search per unlabelled vertex, adding one each time; or union-find, counting successful merges and subtracting from V. O(V + E).
2. **Why can labelling mark on push when DFS cannot?** It needs the set, not the tree; any order yields the same set.
3. **What does `unite` returning false mean?** The two ends were already connected: this edge closes a cycle.
4. **How many edges can a forest on V vertices with c components have?** Exactly V - c. A tree has V - 1.
5. **How do you find a cycle in an undirected graph?** Depth-first search, skipping the edge you came by (by id). A seen vertex is an ancestor, so walk up to it.
6. **Why skip the edge and not the parent vertex?** Parallel edges. Two edges to the same parent are a 2-cycle.
7. **Why does "seen means cycle" fail for directed graphs?** Forward and cross edges also reach seen vertices; only an arc to a vertex on the current path (grey) closes a cycle.
8. **How do you return the directed cycle?** Remember each vertex's stack position; on an arc to a grey vertex, the cycle is the stack from that position to the top.
9. **What is the relation between bipartite and odd cycles?** Bipartite if and only if no odd cycle; BFS depth parity colours, and a same-colour edge plus the two tree paths to the meeting point is an odd cycle.
10. **Why is one odd component enough to fail a disconnected graph?** Bipartite is a property of the whole vertex set; one component that cannot be split ruins it.
11. **Why does flood fill need `from == to` handled?** Otherwise the new colour looks like unvisited territory and the search never ends.
12. **When is union-find better than a traversal?** Edges arriving online, or only an edge list available; when you need paths or colours, use a traversal.

## 12. Problems

### 12.1 On paper

**A.** The undirected graph has V = 8 and the edges `(0,1) (1,2) (2,0) (3,4) (5,6) (6,7) (7,5) (5,0)`. Give the components, and say how many edges a forest on the same vertices and components would have.

**B.** Run the union-find on the edges of A in the order given and mark which `unite` calls return false.

**C.** For the directed graph with arcs `0 to 1`, `1 to 2`, `2 to 0`, `2 to 3`, `3 to 4`, `4 to 3`: list all cycles and say which one a DFS from 0 reports (neighbour order as given).

**D.** Two-colour the graph with edges `(0,1) (0,3) (1,2) (2,3) (3,4) (4,5) (5,6) (6,3)` by BFS from 0 and give either the colouring or an odd cycle.

**E.** On a 1 by n strip of open cells, how many regions does `grid_regions` count with four moves, and with eight? What changes for a 2 by n strip in which the cells alternate open and closed in a chequerboard?

### 12.2 On LeetCode

For each: what is the graph, which of this topic's tools, and what do you return? Hints are in the answers; try first. Numbers and titles are from memory.

| Problem | Number | What to practise |
|---|---|---|
| Number of Connected Components in an Undirected Graph (premium) | 323 | Components by union-find or traversal |
| Graph Valid Tree (premium) | 261 | `is_tree`: V - 1 edges and connected |
| Redundant Connection | 684 | The edge for which `unite` returns false |
| Course Schedule | 207 | Directed cycle detection on prerequisites |
| Find Eventual Safe States | 802 | Vertices that cannot reach a directed cycle |
| Is Graph Bipartite? | 785 | BFS two-colouring on every component |
| Possible Bipartition | 886 | Dislikes as edges, then bipartite |
| Number of Islands | 200 | `grid_regions`, count |
| Max Area of Island | 695 | `grid_regions`, largest |
| Flood Fill | 733 | `flood_fill`, with `from == to` |
| Surrounded Regions | 130 | Regions that do not touch the border |
| Pacific Atlantic Water Flow | 417 | Two searches from the borders, intersect |
| Number of Enclaves | 1020 | Land cells in regions not touching the border |

### 12.3 In your editor

**H.** Write the undirected cycle finder with the arrival-**vertex** skip, and find an input for which it misses a cycle. Then fix it with edge ids.

**I.** Add a method to the union-find that reports, after each edge, the number of components and the size of the largest, and run it on the random graph of section 7.1 to see the giant component appear as edges arrive.

### Answers

**A.** Components: `{0,1,2,5,6,7}` (the edge `(5,0)` joins the two triangles), `{3,4}`. That is 2 components on 8 vertices, so a forest would have 6 edges; there are 8, which means 2 independent cycles.

**B.** `(0,1)` merges, `(1,2)` merges, `(2,0)` returns **false** (cycle), `(3,4)` merges, `(5,6)` merges, `(6,7)` merges, `(7,5)` returns **false**, `(5,0)` merges the two triangles. Two false returns, matching E - (V - c) = 8 - 6 = 2.

**C.** Cycles: `0 to 1 to 2 to 0` and `3 to 4 to 3`. DFS from 0 goes 0, 1, 2 and at the arc `2 to 0` sees the grey 0 first, because 2's arcs are in the order `0`, `3`: it reports `[0 1 2]`. If the arc to 3 had been listed first, it would have found `[3 4]` first.

**D.** BFS from 0: colour 0 for 0; 1 and 3 get colour 1; 2 (from 1) gets colour 0; 4 and 6 (from 3) get colour 0; 5 (from 4) gets colour 1. The edge `(2, 3)` joins colour 0 and colour 1: fine. The edge `(5,6)`: 5 has colour 1, 6 has colour 0: fine. The triangle `3, 4, 5, 6` is the cycle 3-4-5-6-3, of length 4, even. So the graph is bipartite with sides `{0,2,4,6}` and `{1,3,5}`. (If you found a clash, recheck: every cycle here has length 4.)

**E.** A strip of n open cells is one region with four moves and one with eight. A chequerboard of open cells, in a 2 by n grid, is n isolated cells with four moves and **one** region with eight (the diagonals link neighbours along the strip).

**LeetCode hints.** 323: components of an edge list; union-find, answer `sets`. 261: `n - 1` edges and `sets == 1`. 684: process the edges in order; the first `unite` that returns false is the answer (it is the last edge, in input order, of the only cycle). 207: a cycle in the prerequisite digraph means false. 802: reverse the graph and search from the vertices with no outgoing arcs, or colour with the three states and take the vertices that end black without being on a cycle. 785: the BFS of section 5, started from every uncoloured vertex. 886: build the graph from the dislikes and run the same test. 200, 695: `grid_regions`. 733: `flood_fill`, and return the image. 130: search from the border cells, then flip every remaining open cell. 417: one search from each ocean's border, in reverse (uphill), then intersect. 1020: border search, count the rest.

**H.** Two vertices and the parallel edges `(0,1) (1,0)`: the search goes from 0 to 1 and at 1 skips both arcs back to 0 (the parent vertex), so it returns empty although `[1 0]` is a cycle. **I.** Track `max(size)` in `unite`; for `E / V` near 0.5 you will see the largest size jump from hundreds to tens of thousands in a few percent of the edges.

---

## Cheat sheet

- **Components:** one search per unlabelled vertex, or union-find; both O(V + E) up to alpha; labels by smallest vertex are canonical.
- **Counting facts:** forest has V - c edges; tree has V - 1; E > V - c means a cycle; `unite` false means a cycle edge.
- **Undirected cycle:** DFS, skip the arrival **edge** (`eid`), a seen vertex is an ancestor, walk `par` up to it. Self-loop `[v]`, parallel pair `[a, b]`.
- **Directed cycle:** white, grey, black; an arc to grey closes a cycle; `pos[]` gives the stack slice; diamonds are not cycles.
- **Bipartite:** BFS parity colouring on every component; a same-colour edge plus the two tree paths to the meeting point is an odd cycle (bipartite iff no odd cycle).
- **Grids:** `grid_regions` for count and area, 4 or 8 moves; `flood_fill` returns early when the colours are equal.
- **Measured:** random graphs: giant component near E / V = 0.5; union-find 1.8 to 2.1 times as fast as the stack labelling at E / V = 1 and 2; recursion on a ring of 10^6 vertices dies at 8 MB.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The statements that bipartite means no odd cycle, that a forest has V - c edges, and the three-colour characterisation of directed cycles are standard results (CLRS and most graph texts; chapter references not rechecked in this session).
- The random-graph threshold and the percolation threshold of about 0.5927 are recalled from memory and compared, not verified against a source.
- Problem numbers and titles in section 12 are LeetCode's, from memory, and two of them are marked premium; check a number before you rely on it.
