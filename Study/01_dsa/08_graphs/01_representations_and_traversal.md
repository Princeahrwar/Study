# DSA 08.01: Graph Representations and Traversal

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Stacks and queues](../05_stacks_and_queues/01_stack_and_queue_fundamentals.md) (the two containers every traversal is built on), [Recursion fundamentals](../06_recursion_and_backtracking/01_recursion_fundamentals.md) (the call stack and its depth limit) and [Binary tree fundamentals](../07_trees/01_binary_tree_fundamentals.md) (a tree is the simplest graph, and its traversals are the graph traversals with the visited set removed). There is no companion demo file: every structure is listed in this file and is tested code.*

A **graph** is a set of vertices and a set of edges between them. Almost everything in this area, and a large share of interview problems, is a graph in disguise: a grid of cells, a word list where words differing in one letter are linked, a build system's dependencies, a map of roads, the states of a puzzle. The two things you must be able to do before anything else are **store** a graph in a way that fits the question, and **walk** it, visiting every reachable vertex once. Everything later (components, ordering, shortest paths, spanning trees) is one of those two walks with a little extra bookkeeping.

By the end you can:

- name the four ways to store a graph and say what each costs in memory, in time per operation, and in practice (measured);
- write breadth-first search with distances, parents and path reconstruction, and say why it finds shortest paths when the edges are unweighted;
- write depth-first search without recursion, with discovery and finish times, and use them to tell tree, forward, back and cross edges apart;
- explain why "mark when pushed" is right for BFS and wrong for DFS, with a counterexample and a measured stack size;
- treat a grid as a graph nobody stores, and avoid the usual indexing mistakes;
- say when recursion depth makes a recursive DFS a bug, with a number.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 121,105 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the four representations against each other on random directed and undirected graphs with self-loops and parallel edges; BFS distances against Floyd-Warshall for every source and target, and every reconstructed path walked edge by edge; the iterative DFS against a separate recursive one (same discovery and finish times and parents); the timestamps for the nesting property and for using each of the numbers 1 to 2n exactly once; every edge's kind against the definition through ancestors; the stack variants against a recursive DFS with reversed neighbour order; the counterexample of section 5; random grids (square and not, with and without diagonal moves) against Floyd-Warshall on the explicit graph; and a path of a million vertices, which must not recurse. Then 35 hand-made mutants of the listings were run against the tests: **34 were caught and 1 is an equivalent change** (section 9).

**Data and timing.** Graphs are **synthetic** and seeded: random directed edges with uniformly chosen endpoints, and a 4000 by 4000 grid with each cell a wall with probability one in four. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, best of 3, and a claim is made only when it holds in both; they are this machine's, not yours. Byte counts and exit codes are identical in both runs.

---

## 1. Vocabulary

A graph has **V** vertices (here `0 .. V-1`) and **E** edges. An edge is **directed** (a to b only) or **undirected** (both ways; stored, if it is stored as two arcs, as a to b and b to a). **Weighted** edges carry a number; this topic is unweighted, weights come in Topics 05 to 07. A **self-loop** joins a vertex to itself and **parallel edges** repeat a pair; interview inputs usually promise neither, real inputs often have both, and every listing here survives them. The **degree** of a vertex is its number of outgoing edges. A **path** is a sequence of edges, a **cycle** a path that returns to its start, and a graph is **connected** if every vertex can reach every other (for directed graphs the word is subtler, Topic 08). A graph is **sparse** if E is about V (a road map, a social network) and **dense** if E is about V squared (a complete table of distances). A tree is a connected graph without cycles, so a tree is a graph with E = V - 1.

Two facts about cost organise everything: a traversal that visits each vertex once and looks at each edge once costs **O(V + E)**; and **E can be anywhere from 0 to V squared**, so "O(E)" and "O(V squared)" are not the same bound and the representation decides which you get.

## 2. Four ways to store a graph

```cpp
using Edges = std::vector<std::pair<int, int>>;                    // the input: pairs (a, b), vertices 0..n-1

// 1. Adjacency list: one vector per vertex. Neighbour order = input order. Simple to grow; n+1 allocations.
std::vector<std::vector<int>> build_list(int n, const Edges& e, bool directed) {
    std::vector<std::vector<int>> g(n);
    for (auto [a, b] : e) { g[a].push_back(b); if (!directed) g[b].push_back(a); }
    return g;
}
// 2. Compressed rows (CSR): two arrays. The neighbours of u are to[off[u] .. off[u+1]). Built by counting; fixed once built.
struct Csr {
    int n = 0; std::vector<int> off, to;
    int degree(int u) const { return off[u + 1] - off[u]; }
};
Csr build_csr(int n, const Edges& e, bool directed) {
    Csr g; g.n = n; g.off.assign(n + 1, 0);
    for (auto [a, b] : e) { ++g.off[a + 1]; if (!directed) ++g.off[b + 1]; }       // count out-degrees
    for (int u = 0; u < n; ++u) g.off[u + 1] += g.off[u];                          // prefix sums = start of each row
    g.to.resize(g.off[n]); std::vector<int> at(g.off.begin(), g.off.end() - 1);    // next free slot of each row
    for (auto [a, b] : e) { g.to[at[a]++] = b; if (!directed) g.to[at[b]++] = a; }
    return g;
}
// 3. Adjacency matrix: n*n bytes, m[a*n+b] = 1 if an edge a->b exists (parallel edges collapse). O(1) edge test, O(n) to list neighbours.
std::vector<unsigned char> build_matrix(int n, const Edges& e, bool directed) {
    std::vector<unsigned char> m((size_t)n * n, 0);
    for (auto [a, b] : e) { m[(size_t)a * n + b] = 1; if (!directed) m[(size_t)b * n + a] = 1; }
    return m;
}
// The edge list itself (Edges) is the fourth representation: smallest, no structure, what Kruskal and Bellman-Ford want.
```

| | Edge list | Adjacency list | CSR (compressed rows) | Matrix |
|---|---|---|---|---|
| Memory | 8E bytes | about 24V + 4E, plus slack and one allocation per vertex | 4(V + 1) + 4E, two allocations | V squared bytes (V squared / 8 as bits) |
| Is there an edge a to b? | O(E) | O(degree) | O(degree) | **O(1)** |
| List the neighbours of u | O(E) | **O(degree)** | **O(degree)**, contiguous | O(V) |
| Add an edge | O(1) | O(1) amortised | rebuild | O(1) |
| Remove an edge | O(E) | O(degree) | rebuild | O(1) |
| Natural for | Kruskal, Bellman-Ford, sorting edges | most interview problems; graphs that grow | big fixed graphs that are traversed a lot | dense graphs; Floyd-Warshall; "is a to b adjacent" in a loop |

CSR is the adjacency list with its vectors glued into one array: `off[u]` says where u's neighbours start, `off[u + 1]` where they end. It is built in three passes (count the degrees, prefix-sum them into starts, drop each edge into its slot) and costs no per-vertex allocation. The trade is that it cannot grow.

**Measured**, V = 100,000 and E = 800,000 random directed edges, bytes requested (malloc's own headers are not counted):

| Representation | Bytes |
|---|---|
| Edge list | 6,400,000 |
| Adjacency list (`vector<vector<int>>`, capacities) | 6,755,844 |
| CSR | 3,600,004 |
| Matrix, one byte per cell | 10,000,000,000 |
| Matrix, one bit per cell | 1,250,000,000 |

The list needed 1.88 times the memory of CSR, mostly vector capacity slack and the 24-byte vector headers. The matrix is out of the question at this size (10 GB), which is the whole argument for the other three: **choose the matrix only when V is a few thousand or the graph is dense**.

On a graph ten times larger (V = 10^6, E = 8 * 10^6, ns per edge, best of 3, run 1 / run 2):

| | Adjacency list | CSR |
|---|---|---|
| Build | 97.1 / 88.8 | 21.4 / 22.2 |
| BFS | 20.9 / 19.6 | 13.0 / 14.3 |

Building CSR was about four times faster than building the list in both runs, and a BFS over it about 1.3 to 1.6 times faster, because the neighbours of a vertex are next to each other and no vector header is chased. Neither number changes the order of growth; they are the reason that libraries and judges' reference solutions for big graphs use CSR-like layouts.

**What to write in an interview.** The adjacency list: `vector<vector<int>> g(n)`. It is the shortest to write and nobody asks for more. Say CSR when the interviewer asks about memory or speed, and the matrix when E is near V squared.

## 3. The graph as a function

Every traversal below takes the graph as a function `nbr(u, f)` that calls `f(v)` for each neighbour v of u. A list, a CSR, a scan of a matrix row and the four moves on a grid are all different `nbr`s, so the algorithm is written once. This matters in interviews: when the statement gives you a grid, a word list or a puzzle state, you do not build a graph, you write the `nbr` (section 7, and Topic 04).

## 4. Breadth-first search

```cpp
// Every traversal below takes the graph as a function: nbr(u, f) calls f(v) for each neighbour v of u.
// A list, a CSR, a matrix row scan or a grid's four directions are all just different nbr.
struct BfsResult { std::vector<int> dist, parent; };                // -1 = not reached
template <class Nbr> BfsResult bfs(int n, int src, Nbr nbr) {
    BfsResult r{std::vector<int>(n, -1), std::vector<int>(n, -1)};
    std::vector<int> q{src}; r.dist[src] = 0;                       // the queue is a vector with a head index: nothing is ever popped
    for (size_t h = 0; h < q.size(); ++h) {
        int u = q[h];
        nbr(u, [&](int v) { if (r.dist[v] < 0) { r.dist[v] = r.dist[u] + 1; r.parent[v] = u; q.push_back(v); } });   // mark when PUSHED
    }
    return r;
}
std::vector<int> path_to(const BfsResult& r, int t) {               // src ... t, or empty if t was not reached
    std::vector<int> p; if (r.dist[t] < 0) return p;
    for (int v = t; v >= 0; v = r.parent[v]) p.push_back(v);
    std::reverse(p.begin(), p.end()); return p;
}
```

BFS visits the source, then everything one edge away, then everything two edges away, and so on, using a queue (here a vector and a head index, since nothing is ever popped from the front). It does so in **O(V + E)**: each vertex enters the queue once and each edge is looked at once from its tail.

**Why it finds shortest paths in an unweighted graph.** The queue always holds vertices in nondecreasing order of distance, and its contents are at two consecutive distances d and d + 1 at most. A vertex first reached from u (at distance d) gets d + 1, and no shorter way can exist, because any vertex at distance d - 1 or less has already been taken off the queue and has already offered its neighbours. The `parent` array records who reached each vertex first, so the parents form a **shortest-path tree** and `path_to` walks it backwards.

**Mark when pushed.** `dist[v]` is set when v enters the queue, not when it leaves. If you mark on leaving, a vertex reachable from several vertices of the same level is pushed once per edge, which is still correct but the queue can grow to E and the distance may be assigned late. Marking on push keeps the queue at most V long.

A run on a small undirected graph (edges 0-1, 0-2, 1-3, 2-3, 3-4, 5-6):

```
undirected  0: 1 2
undirected  1: 0 3
undirected  2: 0 3
undirected  3: 1 2 4
undirected  4: 3
undirected  5: 6
undirected  6: 5
off: 0 2 4 6 9 10 11 12
to : 1 2 0 3 0 3 1 2 4 3 6 5
bfs from 0: dist 0 1 1 2 3 -1 -1 ; parent -1 0 0 1 3 -1 -1
path to 4: 0 1 3 4 ; path to 6 has 0 vertices
```

Reading it: in CSR form `off[u]` to `off[u + 1]` holds the neighbours of u; BFS from 0 gives distances `0 1 1 2 3` and `-1` for the unreachable 5 and 6; the path to 4 is `0 1 3 4`, and 3's parent is 1, not 2, because 1 was taken off the queue first.

## 5. Depth-first search

```cpp
// Depth-first search with timestamps, no recursion: the stack holds (vertex, next edge to look at), i.e. the call stack made visible.
// disc[u] = time u is first reached, fin[u] = time all its edges are done; the clock ticks at both events (1..2n).
struct DfsResult { std::vector<int> disc, fin, parent, preorder, postorder; };
DfsResult dfs_all(const Csr& g) {
    int n = g.n; DfsResult r{std::vector<int>(n, 0), std::vector<int>(n, 0), std::vector<int>(n, -1), {}, {}};
    std::vector<std::pair<int, int>> st; int clock = 0;
    for (int s = 0; s < n; ++s) {                                   // every vertex as a root, in id order: a forest
        if (r.disc[s]) continue;
        r.disc[s] = ++clock; r.preorder.push_back(s); st.push_back({s, g.off[s]});
        while (!st.empty()) {
            int u = st.back().first; int& i = st.back().second;
            if (i < g.off[u + 1]) {
                int v = g.to[i++];                                  // advance first: push_back below may move the vector
                if (!r.disc[v]) { r.disc[v] = ++clock; r.parent[v] = u; r.preorder.push_back(v); st.push_back({v, g.off[v]}); }
            } else { r.fin[u] = ++clock; r.postorder.push_back(u); st.pop_back(); }
        }
    }
    return r;
}
// Where does edge u->v sit in the search? 'D' descendant (a tree edge or a forward edge), 'B' back (v is an ancestor of u, or u itself), 'C' cross.
char edge_kind(const DfsResult& r, int u, int v) {
    if (r.disc[u] < r.disc[v] && r.fin[v] < r.fin[u]) return 'D';
    if (r.disc[v] <= r.disc[u] && r.fin[u] <= r.fin[v]) return 'B';
    return 'C';
}
// The shortcut people write: push every unvisited neighbour, mark when POPPED. It does visit exactly the reachable set, but may push a vertex
// many times (up to one entry per edge), and the stack grows with the edges, not the vertices.
struct StackDfs { std::vector<int> order, parent; size_t peak = 0; };
StackDfs dfs_mark_on_pop(const Csr& g, int s) {
    StackDfs r; r.parent.assign(g.n, -1); std::vector<char> seen(g.n, 0); std::vector<std::pair<int, int>> st{{s, -1}};   // (vertex, who pushed it)
    while (!st.empty()) {
        auto [u, from] = st.back(); st.pop_back();
        if (seen[u]) continue;
        seen[u] = 1; r.parent[u] = from; r.order.push_back(u);
        for (int i = g.off[u]; i < g.off[u + 1]; ++i) if (!seen[g.to[i]]) st.push_back({g.to[i], u});
        r.peak = std::max(r.peak, st.size());
    }
    return r;
}
// The bug to avoid: marking when PUSHED, as BFS does. The order can look plausible, but the tree is not a depth-first tree.
StackDfs dfs_mark_on_push_wrong(const Csr& g, int s) {
    StackDfs r; r.parent.assign(g.n, -1); std::vector<char> seen(g.n, 0); std::vector<int> st{s}; seen[s] = 1;
    while (!st.empty()) {
        int u = st.back(); st.pop_back(); r.order.push_back(u);
        for (int i = g.off[u]; i < g.off[u + 1]; ++i) if (!seen[g.to[i]]) { seen[g.to[i]] = 1; r.parent[g.to[i]] = u; st.push_back(g.to[i]); }
        r.peak = std::max(r.peak, st.size());
    }
    return r;
}
```

DFS goes as deep as it can, then backs up. The recursive version is four lines; the listing is the same program with the call stack made visible: the stack holds `(vertex, next edge to look at)`, one entry per vertex on the current path. Two numbers per vertex come out of it: `disc` when the vertex is first reached and `fin` when all of its edges are done, from one clock that ticks at both events, so the numbers 1 to 2V are each used once.

The **parenthesis property**: for any two vertices the intervals `[disc, fin]` either nest or are disjoint, never partly overlap. A vertex v is a descendant of u in the DFS forest exactly when `disc[u] < disc[v]` and `fin[v] < fin[u]`. That gives the four kinds of edge u to v:

| Kind | v is | Meaning |
|---|---|---|
| Tree | discovered through this edge | part of the DFS forest |
| Forward | a descendant of u, reached earlier another way | skips down the tree |
| Back | u or an ancestor of u (still open) | **closes a cycle** |
| Cross | in a subtree that is already finished, or a different tree | none of the above |

`edge_kind` returns `'D'` for tree and forward together (a tree edge is the one whose tail is `parent[v]`), `'B'` or `'C'`. The facts to remember: a directed graph has a cycle **if and only if** DFS finds a back edge (Topic 02), reverse finishing order is a topological order of an acyclic graph (Topic 03), and in an **undirected** graph every edge is a tree or back edge, never forward or cross (the tests check it). Back edges, finish times and `low` values are the machinery of Topic 08.

A run on a directed graph with edges 0 to 1, 0 to 2, 0 to 3, 1 to 3, 2 to 1 and 3 to 0:

```
vertex 0: disc 1 fin 8 parent -1
vertex 1: disc 2 fin 5 parent 0
vertex 2: disc 6 fin 7 parent 0
vertex 3: disc 3 fin 4 parent 1
edge 0->1: tree
edge 0->2: tree
edge 0->3: forward
edge 1->3: tree
edge 2->1: cross
edge 3->0: back
preorder: 0 1 3 2 ; postorder: 3 1 2 0
```

DFS from 0 takes 1 first (neighbour order is the input order), then 3, and 3's edge back to 0 is a back edge. Back at 0, the edge to 3 now finds it already discovered inside 1's subtree: a forward edge. 2 is reached from 0, and its edge to 1 lands in a finished subtree: a cross edge.

**The shortcut everyone writes, and where it goes wrong.** Many people write DFS as "push the start; pop a vertex; if unvisited, visit it and push its neighbours". That is `dfs_mark_on_pop`, and it is a correct DFS, but it **visits neighbours in reverse order** (the last pushed is popped first) and it **pushes every unvisited neighbour of every visited vertex**, so a vertex can sit on the stack many times. On the complete graph with 1,000 vertices, one search from vertex 0:

| Variant | Vertices visited | Peak stack entries |
|---|---|---|
| Mark on pop (`dfs_mark_on_pop`) | 1,000 | 498,502 |
| Mark on push (`dfs_mark_on_push_wrong`) | 1,000 | 999 |
| The real DFS stack of `dfs_all` | 1,000 | at most 1,000 (one per vertex on the path) |

The stack of the shortcut grew with the number of **edges** (about (V - 1)(V - 2) / 2 + 1 entries here), not the vertices. The tempting repair is to mark vertices when they are pushed, as BFS does: that keeps the stack at most V, but it is **not depth-first search**. On the graph with edges 0 to 1, 0 to 2 and 2 to 1:

```
mark on pop : order 0 2 1 ; parent -1 2 0
mark on push: order 0 2 1 ; parent -1 0 0
```

Both print the same visiting order, but mark-on-push says 1's parent is 0. In a real depth-first search, once 2 is open its edge to 1 must be followed before 2 can finish, so 1's parent is 2; the tree from mark-on-push has 2 finishing with an unvisited neighbour, which is impossible in DFS. The order looks right, the tree is wrong, and everything built on the tree (cycle detection, topological order, bridges) breaks on some input. The rule: **BFS marks on push; DFS marks when it arrives (pop, or the stack-of-iterators of `dfs_all`)**.

## 6. Recursion depth, with a number

A recursive DFS uses one stack frame per vertex on the current path, and the longest path can be V. Depth-first search of a path graph of n vertices, in a child process with an 8 MB stack:

| Vertices | Recursive DFS | `dfs_all` |
|---|---|---|
| 10,000 | exit 0 | exit 0 |
| 100,000 | exit 0 | exit 0 |
| 1,000,000 | killed by signal 11 | exit 0 |

(`-11` is a kill by signal 11, a stack overflow.) At 10^5 the recursion survived and at 10^6 it did not; the limit is this machine's, and the frame of that function is a few tens of bytes. The shape of a **random** graph is no help: its DFS paths are long, typically a large fraction of V. **BFS never has this problem** (its queue is a heap vector). So: a recursive DFS is fine when V is small and known, as in most interview statements (state it), and wrong when V comes from the input. The same lesson as [Trees 34](../07_trees/34_pattern_indexed_problem_set_and_interview_narration.md), with a graph in place of a tree.

## 7. Grids are graphs nobody stores

```cpp
// A grid is a graph nobody stores: cell (r, c) is vertex r*W + c and its neighbours are computed. '#' is a wall.
// Returns the number of steps from (sr, sc) to (tr, tc), or -1. diag = allow the four diagonal moves too.
int grid_shortest(const std::vector<std::string>& g, int sr, int sc, int tr, int tc, bool diag = false) {
    int H = g.size(), W = H ? g[0].size() : 0;
    if (!H || g[sr][sc] == '#' || g[tr][tc] == '#') return -1;
    static const int DR[8] = {-1, 1, 0, 0, -1, -1, 1, 1}, DC[8] = {0, 0, -1, 1, -1, 1, -1, 1};
    int moves = diag ? 8 : 4;
    auto nbr = [&](int u, auto f) {
        int r = u / W, c = u % W;
        for (int k = 0; k < moves; ++k) {
            int nr = r + DR[k], nc = c + DC[k];
            if (nr >= 0 && nr < H && nc >= 0 && nc < W && g[nr][nc] != '#') f(nr * W + nc);     // bounds first, then the wall test
        }
    };
    return bfs(H * W, sr * W + sc, nbr).dist[tr * W + tc];
}
```

A cell `(r, c)` is vertex `r * W + c` and its neighbours are the four (or eight) cells around it that are in bounds and not walls. Nothing is stored: `nbr` computes them. For the 4000 by 4000 grid used below, an explicit CSR graph would need 320,000,004 bytes against 16,000,000 for the grid itself, a factor of twenty.

Three mistakes account for most grid bugs, and the tests are built to catch each (the grids are not square):

- **Bounds before content.** `g[nr][nc]` is read only after `nr` and `nc` are known to be in range; the other order reads outside the grid.
- **Row and column swapped.** `u / W` is the row and `u % W` the column; `W` is the number of columns, `H` the number of rows. On a square grid the swap is invisible, which is why the tests use non-square ones.
- **The start or target is a wall.** Without the start check, a search that starts inside a wall finds paths; the target check is redundant here (a wall is never entered).

The example grid in the trace (`.` open, `#` wall):

```
grid 4x4 shortest (0,0)->(3,3): 6 ; with diagonals: 4 ; (0,0)->(0,3): 5
```

Reading it: the walls are at (0,2), (1,0) and (2,2); the top-left to bottom-right route takes 6 steps, or 4 with diagonal moves, and the top-right corner is 5 steps away because the wall at (0,2) forces a detour.

**Measured**, BFS on the 4000 by 4000 grid (a shortest path of 7,998 steps, 11,998,140 open cells), ns per open cell, best of 3: 51.6 / 43.1. It is more than the CSR figures above because the grid BFS recomputes `u / W` and `u % W` (two divisions) and tests four neighbours per cell, three quarters of the memory touched is not needed by the answer, and each cell's neighbours are in other rows; it is the price of not storing the graph.

## 8. Choosing

| The input is | Store it as | Walk with |
|---|---|---|
| A list of pairs, then queries about neighbours | adjacency list (CSR if huge) | BFS or DFS |
| A list of pairs, then sort or union edges | edge list | Kruskal, Bellman-Ford (Topics 06, 07) |
| A small graph and many "is a adjacent to b" | matrix | any, or Floyd-Warshall |
| A grid | the grid itself | BFS for fewest steps, DFS or BFS for regions |
| A rule that generates neighbours (words, puzzle states) | nothing: a function | BFS (Topic 04) |
| A tree with parent pointers | the tree | BFS from a node reaches "k steps away" (Trees 34) |

**BFS or DFS?** Fewest edges, or level by level: BFS. Reachability, components, cycles, ordering, structure of the graph (back edges, finishing times): DFS. If either would do, BFS has no depth limit.

## 9. What the tests check, and what they cannot

The one survivor of the 35 mutants is an equivalent change, with the reason:

- **Dropping the target-wall test in `grid_shortest`:** a wall is never entered by the search, so its distance stays `-1` anyway; the test only saves the search. (The start-wall test, in contrast, matters, and removing it is caught.)

## 10. Common mistakes

- Building an `n by n` matrix for a sparse graph of 10^5 vertices (10 GB here).
- Forgetting to add the reverse edge for an undirected graph, or adding both and then counting E twice.
- Marking visited when a vertex is **taken off** the queue in BFS (the queue grows to E), or when it is **pushed** in DFS (the tree is not depth-first).
- Recursive DFS on input whose V is unbounded.
- Assuming no self-loops or parallel edges because the examples have none.
- Using vertex labels 1 to n as indices into arrays of size n.
- Searching a grid with `H` and `W` swapped, or testing the wall before the bounds.
- Reporting the BFS distance as the answer when the problem wants the path (keep `parent`), or the reverse.
- Using BFS for shortest paths when the edges have different weights (Topic 05).

## 11. Questions to answer out loud

1. **What does the choice of representation change?** The memory (8E, about 24V + 4E, 4V + 4E, or V squared), the cost of "is there an edge" and of "list the neighbours", and whether the graph can grow.
2. **Why is CSR faster than a list of vectors here?** The neighbours of a vertex are contiguous, there is no per-vertex allocation, and construction is three linear passes (about four times faster to build and 1.3 to 1.6 times faster to traverse, measured).
3. **Why does BFS give shortest paths?** The queue holds vertices in order of distance and a vertex is first reached from the nearest vertex that has already been processed.
4. **What is the complexity of BFS and DFS?** O(V + E) with a list or CSR; O(V squared) with a matrix, because listing neighbours scans a row.
5. **What are `disc` and `fin`, and what is the parenthesis property?** The times a vertex is first reached and finished; intervals nest or are disjoint.
6. **How do you tell the edge kinds apart?** By the intervals of the endpoints: descendant inside, ancestor (or itself) outside and open, otherwise cross.
7. **Why does "mark on push" break DFS?** It commits to a parent before the vertex is actually reached; the resulting tree is not a depth-first tree.
8. **What is wrong with the pop-time DFS shortcut?** Nothing for correctness, but its stack holds one entry per edge instead of one per vertex (about 500,000 entries for 1,000 vertices in a complete graph).
9. **When is recursive DFS a bug?** When the depth is not bounded by something you control: a path of 10^6 vertices overflowed an 8 MB stack here.
10. **How do you do BFS on a grid?** Vertex `r * W + c`, four direction offsets, bounds first and then the wall test; the neighbours are computed, not stored.

## 12. Problems

### 12.1 On paper

**A.** Write the adjacency list, the CSR arrays (`off` and `to`) and the matrix for the directed graph with V = 4 and edges 0 to 1, 0 to 2, 2 to 3, 3 to 1, 1 to 1.

**B.** For the graph of A, run BFS from 0 and give the distances, parents and the path to 3.

**C.** For the same graph run DFS from 0 (all roots, neighbour order as in the list) and give `disc`, `fin` and the kind of each edge.

**D.** A graph has V = 10^6 and E = 3 * 10^6 undirected edges. Estimate the bytes of the edge list, the adjacency list and CSR (4-byte ints, 24-byte vector headers, ignore slack), and say whether a matrix is possible.

**E.** Give a directed graph on 4 vertices where marking visited on **push** gives a different parent array than marking on **pop**, and say which is the depth-first tree.

**F.** On the complete graph K_5, how many entries does the mark-on-pop DFS stack hold at its peak? (Use the formula in section 5 and check by hand.)

### 12.2 On LeetCode

For each: what is the graph (vertices, edges), which traversal, and what do you store? Approach hints are given in the answers section; try first.

| Problem | Number | What to practise |
|---|---|---|
| Find if Path Exists in Graph | 1971 | Build the adjacency list, BFS or DFS from the source |
| Clone Graph | 133 | A map from old node to new node doubles as the visited set |
| Number of Provinces | 547 | Given as a matrix: DFS or BFS over rows, count the starts |
| Find the Town Judge | 997 | Not a traversal: in-degree and out-degree arrays |
| Find Center of Star Graph | 1791 | Degree counting on an edge list |
| All Paths From Source to Target | 797 | DFS with a path, on an acyclic graph |
| Keys and Rooms | 841 | Reachability from room 0 |
| Shortest Path in Binary Matrix | 1091 | Grid BFS with 8 directions |
| Flood Fill | 733 | Grid DFS or BFS, recolour on visit |
| Number of Islands | 200 | Grid components: one search per unvisited land cell |
| Max Area of Island | 695 | Same, count cells per search |

### 12.3 In your editor

**H.** Write the iterative DFS (stack of vertex and edge index) from memory on an adjacency list, with discovery and finish times, and test it on a path of a million vertices.

**I.** Write a BFS that returns one shortest path as a list of vertices, and a second that returns **the number** of distinct shortest paths to every vertex (hint: add the count of the parent when a vertex is reached again at the same distance), checked against brute force on small random graphs.

### Answers

**A.** List: `0: [1, 2]`, `1: [1]`, `2: [3]`, `3: [1]`. CSR: `off = [0, 2, 3, 4, 5]`, `to = [1, 2, 1, 3, 1]`. Matrix (row = tail): row 0 is `0 1 1 0`, row 1 `0 1 0 0`, row 2 `0 0 0 1`, row 3 `0 1 0 0`. (The edge 1 to 1 is a self-loop: a 1 on the diagonal.)

**B.** From 0: distances `0 1 1 2` (vertex 3 is reached from 2), parents `-1 0 0 2`, path to 3 is `0 2 3`.

**C.** Visit 0 (`disc` 1), then 1 (`disc` 2): its self-loop 1 to 1 is a back edge, so 1 finishes (`fin` 3). Back to 0, then 2 (`disc` 4), then 3 (`disc` 5): 3's edge to 1 goes to a finished vertex in another subtree: cross. 3 finishes (6), 2 (7), 0 (8). Edge kinds: 0 to 1 tree, 0 to 2 tree, 1 to 1 back, 2 to 3 tree, 3 to 1 cross.

**D.** Edge list: 3,000,000 pairs of two ints = 24 MB. Adjacency list: 24 * 10^6 + 4 * 6 * 10^6 = 48 MB (each undirected edge is stored twice). CSR: 4 * (10^6 + 1) + 4 * 6 * 10^6 = about 28 MB. The matrix would be 10^12 bytes, a terabyte: impossible.

**E.** The graph of the trace in section 5: edges 0 to 1, 0 to 2, 2 to 1. Mark on pop gives parents `-1 2 0` (1 hangs from 2); mark on push gives `-1 0 0`. The first is the depth-first tree: when 2 is open, its edge to 1 must be followed before 2 finishes.

**F.** The formula (V - 1)(V - 2) / 2 + 1 gives 7 for V = 5. By hand: the start pushes 4 entries (peak 4); the first vertex popped pushes its 3 unvisited neighbours (stack 3 + 3 = 6); the next pushes 2 (5 + 2 = 7); the next pushes 1 (6 + 1 = 7); the next finds no unvisited neighbour (stack 5). So the sizes after each visited vertex are 4, 6, 7, 7 and 5, and the peak is 7.

**LeetCode hints.** 1971: the path exists iff BFS from the source reaches the target; an edge list is the input, so build the list first. 133: copy a node when first met, then connect copies of its neighbours; the map prevents infinite loops on cycles. 547: the matrix is the graph; count the number of times you start a new search. 997: the judge has in-degree V - 1 and out-degree 0; no traversal needed. 1791: the centre appears in every edge, so it is the common vertex of the first two edges. 797: the graph is a DAG, so no visited set is needed; carry the current path and copy it at the target. 841: reachability from vertex 0; compare the visited count with V. 1091: 8-direction BFS from (0,0), answer is the distance plus one in cells; handle a wall at the start or the end. 733: one search from the start cell; remember to stop if the new colour equals the old (otherwise an unmarked recolour can loop). 200: iterate over cells, start a search at each unvisited land cell and count the starts. 695: as 200, return the largest size.

**H.** Compare your times with the reference in the listing: if your recursive version crashes at 10^6, the iterative one is not optional. **I.** Counting: when a neighbour v is first reached set `ways[v] = ways[u]`; when it is reached again with `dist[v] == dist[u] + 1`, add `ways[u]`.

---

## Cheat sheet

- **Store:** list `vector<vector<int>>` by default; CSR for big and fixed (list was 1.9 times the bytes of CSR here, and about four times slower to build); edge list for Kruskal; matrix only for small or dense V.
- **BFS:** queue (vector + head), `dist = -1` means unseen, **mark on push**, O(V + E); `parent` gives the shortest-path tree.
- **DFS:** stack of (vertex, next edge), `disc` and `fin` from one clock, mark when arriving; back edge = cycle; reverse finish order = topological order (Topic 03).
- **Edge kinds:** tree, forward (descendant), back (ancestor or self), cross; undirected graphs have only tree and back edges.
- **Traps:** mark on push in DFS gives a non-DFS tree; the pop-time shortcut's stack is O(E) (498,502 entries for K_1000); recursive DFS died at 10^6 vertices on 8 MB.
- **Grids:** vertex `r * W + c`, directions array, bounds before walls, test non-square grids.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The DFS edge classification and the parenthesis property follow the standard treatment (CLRS chapter 20, named from memory; I did not recheck the chapter in this session).
- Problem numbers and titles in section 12 are LeetCode's, from memory, and the plan marks the premium ones; check a number before you rely on it.
