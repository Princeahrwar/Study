# DSA 08.07: Minimum Spanning Trees

*Part of the [Graphs plan](00_plan.md) · Time: about 7 hours (3.5 reading, 3.5 problems) · You need: [Connectivity and cycles](02_connectivity_and_cycles.md) (union-find) and [Shortest paths I: Dijkstra](05_shortest_paths_dijkstra.md) (the heap loop with lazy deletion). There is no companion demo file: every structure is listed in this document.*

You have a set of towns and a price for laying a road, a cable or a pipe between some pairs. You want every town connected, and you want the total price as low as possible. The cheapest set of connections never contains a cycle (dropping any edge of a cycle keeps everything connected, and with non-negative weights it cannot make the total heavier), so it can be taken to be a **tree** on all the vertices: a **minimum spanning tree**, MST. This looks like a shortest-path question and is not one: the tree minimises the **total** weight of its edges, not the distance from any vertex to any other. The surprise is how short the solution is. A greedy choice, "take the cheapest edge that does not do harm", is correct, for two different reasons that give two algorithms: **Kruskal** (sort the edges, add one when it joins two different components) and **Prim** (grow one tree, add the cheapest edge that leaves it).

By the end you can:

- state the cut property and the cycle property, and prove the first in five lines;
- write Kruskal with union-find and Prim with a heap, and explain why each is correct from the cut property;
- say what happens when the graph is disconnected (a spanning forest) and why ties make the edge set non-unique but not the weight;
- say, with measurements, why Kruskal is dominated by its sort, when its early stop helps, and why Prim with a heap loses to Kruskal on sparse graphs and to an array on dense geometric ones;
- connect N points at minimum cost without ever storing the N^2 edges;
- find critical and pseudo-critical edges, and the smallest possible "heaviest edge" on a path between two vertices.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 36,469 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: Kruskal and Prim against a brute force over all acyclic edge subsets on thousands of tiny graphs (parallel edges, self loops, ties, disconnected ones, counted so that both connected and disconnected cases occur often); Kruskal and Prim choosing the same edges whenever the minimum tree is unique; critical and pseudo-critical edges against the brute-force count of how many minimum trees contain each edge; the array version of Prim on points against Kruskal on the complete graph; the minimax path against relaxation to a fixpoint; the **cycle property** as a certificate on a graph of 2,000 vertices (every non-tree edge is at least as heavy as the heaviest tree edge on the tree path between its ends); shuffled edge lists; and a graph of a million vertices and four million edges. Then 30 hand-made mutants of the listings were run against the tests: **25 were caught and 5 are equivalent changes** (section 8).

**Data and timing.** Graphs are **synthetic** and seeded: V = 1,000,000 vertices with 1, 2 or 4 million random edges, weights uniform in 1 to 10^6; and N = 3,000 random points in a square of side 10^5 with Manhattan distances. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Two facts that explain everything

A **spanning tree** of a connected graph is a set of V - 1 edges that connects all vertices without a cycle. Its weight is the sum of its edge weights, and an MST is one of minimum weight. If the graph is not connected there is no spanning tree; the same algorithms then give a **spanning forest**, a minimum tree for each component, with V minus (number of components) edges.

A **cut** splits the vertices into two non-empty sets `S` and the rest; an edge **crosses** the cut if its ends are on different sides.

> **Cut property.** For any cut, a lightest edge crossing it belongs to some minimum spanning tree.
> **Cycle property.** On any cycle, a heaviest edge can be removed without raising the weight of the best tree; if it is the unique heaviest, it is in no MST.

*Why the cut property.* Let `e` be a lightest crossing edge and `T` any MST. If `e` is in `T`, done. Otherwise `T + e` contains exactly one cycle, and that cycle must cross the cut again at some other edge `f` of `T`. Swap them: `T - f + e` is still a spanning tree, and `w(e) <= w(f)` because `e` was lightest across the cut, so it is no heavier than `T`, hence also minimum and contains `e`. The cycle property is the same argument turned round.

Both algorithms are this fact applied repeatedly. They differ only in which cut they look at: Kruskal at the cut between the component an edge joins and everything else, Prim at the cut between its one tree and everything else. Edges with equal weights make the tree non-unique (a triangle of three equal edges has three MSTs), but never the weight.

## 2. Kruskal

```cpp
struct WEdge { int a, b; long long w; };            // an undirected edge between a and b of weight w
const long long INF = LLONG_MAX / 4;
// Union-find (Topic 02): union by size, path halving.
struct Dsu {
    std::vector<int> p, sz; int comps;
    explicit Dsu(int n) : p(n), sz(n, 1), comps(n) { std::iota(p.begin(), p.end(), 0); }
    int find(int x) { while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; } return x; }
    bool unite(int a, int b) {
        a = find(a); b = find(b); if (a == b) return false;
        if (sz[a] < sz[b]) std::swap(a, b);
        p[b] = a; sz[a] += sz[b]; --comps; return true;
    }
};
// What both algorithms return: the total weight, the indices of the chosen edges, and how many connected components remain
// (1 for a connected graph; more means the answer is a minimum spanning FOREST, one tree per component). `looked` counts the work.
struct Mst { long long total = 0; std::vector<int> chosen; int comps = 0; long long looked = 0; };
```

```cpp
// Kruskal: look at the edges from the lightest to the heaviest and keep an edge when it joins two different components.
// Sorting costs O(E log E); the union-find work is almost linear. Stop as soon as n - 1 edges are kept (a spanning tree is complete);
// in a disconnected graph the loop simply runs out of edges. Ties are broken by input position so the result is repeatable.
Mst kruskal(int n, const std::vector<WEdge>& e) {
    std::vector<int> idx(e.size()); std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int x, int y) { return e[x].w < e[y].w; });
    Dsu d(n); Mst r;
    for (int i : idx) {
        if (d.comps == 1) break;                      // already spanning: the remaining edges cannot be needed
        ++r.looked;
        if (d.unite(e[i].a, e[i].b)) { r.total += e[i].w; r.chosen.push_back(i); }
    }
    r.comps = d.comps; return r;
}
```

Look at the edges from lightest to heaviest and keep an edge when its two ends are in different components (`unite` returns true). Why that is right: when an edge `e` is taken, it joins components `C1` and `C2`. Every lighter edge has already been considered, so any lighter edge leaving `C1` would have joined `C1` to something already (it was kept or its ends were already together), which means no lighter edge leaves `C1`: `e` is a lightest edge across the cut (`C1`, rest), and the cut property applies. An edge rejected because both ends are together would close a cycle, and it is the heaviest on that cycle, so the cycle property discards it.

The trace is on the standard nine-vertex example (14 edges). Each line is one step of a copy of the loop that prints its decisions; the final line is the real function.

```
 1: edge 6-7 w=1 take total=1 comps=8
 2: edge 2-8 w=2 take total=3 comps=7
 3: edge 5-6 w=2 take total=5 comps=6
 4: edge 0-1 w=4 take total=9 comps=5
 5: edge 2-5 w=4 take total=13 comps=4
 6: edge 6-8 w=6 skip total=13 comps=4
 7: edge 2-3 w=7 take total=20 comps=3
 8: edge 7-8 w=7 skip total=20 comps=3
 9: edge 0-7 w=8 take total=28 comps=2
10: edge 1-2 w=8 skip total=28 comps=2
11: edge 3-4 w=9 take total=37 comps=1
kruskal total=37 looked=11 chosen=8
```

Eight edges are kept (V - 1), total 37, and the loop stopped at the eleventh edge: the remaining three could not be needed once there is one component. That early stop (`d.comps == 1`) saves nothing if the graph is disconnected, because the loop never reaches one component.

**Cost.** Sorting is O(E log E); the union-find calls are O(E alpha(V)), almost linear. So Kruskal is a sort, plus a little.

## 3. Prim

```cpp
// Prim: grow ONE tree from a start vertex; always add the lightest edge leaving the tree. A binary heap holds candidate edges
// (weight, vertex, edge index); an entry whose vertex is already in the tree is stale and skipped (lazy deletion, as in Dijkstra).
// The only difference from Dijkstra's loop is the key: the weight of the connecting edge, not the distance from the source.
// If the graph is disconnected, the loop restarts from the next vertex not yet in a tree, which gives a spanning forest.
Mst prim(int n, const std::vector<WEdge>& e) {
    std::vector<int> off(n + 1, 0); for (auto& x : e) { ++off[x.a + 1]; ++off[x.b + 1]; }
    for (int u = 0; u < n; ++u) off[u + 1] += off[u];
    std::vector<int> at(off.begin(), off.end() - 1), to(off[n]), id(off[n]);
    for (int i = 0; i < (int)e.size(); ++i) { to[at[e[i].a]] = e[i].b; id[at[e[i].a]++] = i; to[at[e[i].b]] = e[i].a; id[at[e[i].b]++] = i; }
    using Item = std::tuple<long long, int, int>; std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    std::vector<char> in(n, 0); Mst r; r.comps = 0;
    for (int s = 0; s < n; ++s) {
        if (in[s]) continue;
        ++r.comps; in[s] = 1;
        for (int k = off[s]; k < off[s + 1]; ++k) pq.push({e[id[k]].w, to[k], id[k]});
        while (!pq.empty()) {
            auto [w, v, ei] = pq.top(); pq.pop(); ++r.looked;
            if (in[v]) continue;
            in[v] = 1; r.total += w; r.chosen.push_back(ei);
            for (int k = off[v]; k < off[v + 1]; ++k) if (!in[to[k]]) pq.push({e[id[k]].w, to[k], id[k]});
        }
    }
    return r;
}
```

Prim is Dijkstra with a different key. Dijkstra's heap holds "distance from the source through this edge"; Prim's holds "weight of this edge", and nothing accumulates along the path. The tree grows from a start vertex; the heap holds edges leaving it; the lightest one adds a new vertex (an entry whose vertex is already in the tree is stale and skipped, as in Topic 05). Correctness is the cut property for the cut (tree, rest). On a disconnected graph the outer loop starts a new tree at the next vertex not yet reached, giving a forest, and `comps` counts the trees.

```
prim total=37 pops=14 chosen: 0-1(4) 1-2(8) 2-8(2) 2-5(4) 5-6(2) 6-7(1) 2-3(7) 3-4(9)
kruskal chosen: 6-7(1) 2-8(2) 5-6(2) 0-1(4) 2-5(4) 2-3(7) 0-7(8) 3-4(9)
```

Same total, 37, but **not the same edges**: Kruskal kept `0-7` (weight 8) where Prim kept `1-2` (weight 8). Both are minimum trees, because several edges of weight 8 tie. The tests check that when the MST is unique (all the weights on the relevant cuts distinct) the two choose identical edges, and that the weight is always the same. A disconnected graph, with a self loop thrown in, gives a forest:

```
triangle kruskal total=2 edges: 0 1
triangle prim    total=2 edges: 0 1
forest: total=2 comps=3 edges=2
```

## 4. Which one, measured

The same random graphs, 10^6 vertices, with 1, 2 and 4 million edges. With few edges the graph is not connected, so the result is a forest (the table gives the number of trees), and Kruskal's early stop never triggers: it examines every edge.

| Edges | Trees in the result | Kruskal (ms) | Prim, heap (ms) | Sorting alone (ms) |
|---|---|---|---|---|
| 1,000,000 | 161,807 | 214 / 175 | 414 / 378 | 120 / 124 |
| 2,000,000 | 18,899 | 377 / 338 | 1,036 / 939 | 322 / 264 |
| 4,000,000 | 326 | 848 / 818 | 2,250 / 2,107 | 723 / 588 |

Both algorithms returned the same total weight in every row (checked in the benchmark and in the tests). Kruskal was **1.9 to 2.8 times faster** than heap Prim in every row of both runs, and the sort alone was 56 to 85% of Kruskal's time: after the sort, the whole union-find pass costs very little. Prim with a binary heap pays `log` for every pop and chases memory through the adjacency list; with 4 million edges it popped about as many entries as there are edges, so it has no way of skipping work.

When does Prim win? On a **dense** graph, where the heap can be replaced by an array. That is the next section. When does Kruskal's early stop help? When the graph is connected long before the heaviest edges are reached, as on the complete graph of the points below, where the 4,498,500 edges are all there but the tree was complete after 15,908 of them (0.35%). It saves the union-find work, not the sort.

## 5. Connecting points

"Connect N points so that every pair is joined by a path, the cost of a segment being the Manhattan distance, at minimum total cost." Every pair of points is a candidate edge, so the graph is complete: E is about N^2 / 2. Do not build it. Prim with an **array** needs only, for each point not yet in the tree, the cheapest edge to the tree so far:

```cpp
// Connecting points: every pair of points is an edge of weight |dx| + |dy|, so E is about N^2 / 2 and a heap is pointless. Use the
// array version of Prim: best[v] is the cheapest edge from the tree to v; each round take the smallest and update everyone.
// O(N^2) time, O(N) memory: the edges are never stored. (A complete graph on N = 3000 points would otherwise be 4.5 million edges.)
long long prim_points(const std::vector<std::pair<int, int>>& p) {
    int n = p.size(); if (n == 0) return 0;
    std::vector<long long> best(n, INF); std::vector<char> in(n, 0); best[0] = 0; long long total = 0;
    for (int it = 0; it < n; ++it) {
        int u = -1; for (int v = 0; v < n; ++v) if (!in[v] && (u < 0 || best[v] < best[u])) u = v;
        in[u] = 1; total += best[u];
        for (int v = 0; v < n; ++v) if (!in[v]) best[v] = std::min(best[v], (long long)std::abs(p[u].first - p[v].first) + std::abs(p[u].second - p[v].second));
    }
    return total;
}
```

Each round takes the smallest `best[v]` (a scan) and updates every other point against the new tree vertex: O(N^2) time, O(N) memory, and the edges are computed as needed. The two samples (the examples of the LeetCode problem 1584 as I remember them) give:

```
points A: 20
points B: 18
```

Measured with N = 3,000 points (4,498,500 edges):

| Method | Time (ms) |
|---|---|
| Array Prim (`prim_points`, no edges stored) | 13 / 13 |
| Kruskal on the 4,498,500 edges | 736 / 561 |
| Heap Prim on the 4,498,500 edges | 1,538 / 1,485 |

The array Prim finished in about 13 ms, Kruskal needed 43 to 57 times longer, and heap Prim 114 to 118 times longer. Kruskal must create 4.5 million edges and sort them; at N = 10^5 points that would be 5 * 10^9 edges, 80 GB at 16 bytes each (answer F), while the array version still needs only O(N) memory and 10^10 steps. When the weight is a geometric distance, there are also methods that never look at all pairs (Delaunay triangulation, or a k-d tree with Borůvka; from memory, not implemented here), but array Prim is the interview answer.

## 6. Two things the cut property gives for free

```cpp
// Two consequences of the cut property. (1) Critical and pseudo-critical edges: an edge is CRITICAL if every minimum spanning tree
// contains it (leaving it out makes the best tree heavier or impossible), PSEUDO-CRITICAL if some but not every one does. Test each
// edge by recomputing: once without it, once with it forced in. O(E^2 alpha), fine for the sizes where this question is asked.
// (2) The minimax path: the smallest possible "heaviest edge" on a path from s to t is found by Kruskal stopped the moment s and t
// join, because the MST contains a minimax path for every pair. Compare Topic 05, where the same answer needed a Dijkstra loop.
struct Crit { std::vector<int> critical, pseudo; };
static long long mst_weight_with(int n, const std::vector<WEdge>& e, const std::vector<int>& idx, int skip, int force) {
    Dsu d(n); long long total = 0;
    if (force >= 0) { d.unite(e[force].a, e[force].b); total += e[force].w; }
    for (int i : idx) if (i != skip && d.unite(e[i].a, e[i].b)) total += e[i].w;
    return d.comps == 1 ? total : INF;
}
Crit critical_edges(int n, const std::vector<WEdge>& e) {
    std::vector<int> idx(e.size()); std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int x, int y) { return e[x].w < e[y].w; });
    long long base = mst_weight_with(n, e, idx, -1, -1); Crit r;
    for (int i = 0; i < (int)e.size(); ++i) {
        if (e[i].a == e[i].b) continue;                                    // a self loop is in no tree
        if (mst_weight_with(n, e, idx, i, -1) > base) r.critical.push_back(i);
        else if (mst_weight_with(n, e, idx, -1, i) == base) r.pseudo.push_back(i);
    }
    return r;
}
long long bottleneck(int n, const std::vector<WEdge>& e, int s, int t) {
    if (s == t) return 0;
    std::vector<int> idx(e.size()); std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int x, int y) { return e[x].w < e[y].w; });
    Dsu d(n);
    for (int i : idx) { d.unite(e[i].a, e[i].b); if (d.find(s) == d.find(t)) return e[i].w; }
    return INF;
}
```

**Critical and pseudo-critical edges.** An edge is **critical** if every MST contains it, **pseudo-critical** if some but not every MST does. Recompute the MST without the edge: if the weight rises, or the graph disconnects, the edge is critical. Otherwise force the edge in (unite its ends first) and recompute: if the weight stays the same, there is an MST containing it. On the example graph:

```
critical: 0-1 2-3 2-8 2-5 3-4 5-6 6-7
pseudo: 0-7 1-2
triangle critical=0 pseudo=3
path+long critical=2 pseudo=0
```

The two pseudo-critical edges are exactly the two weight-8 edges, `0-7` and `1-2`, that Kruskal and Prim disagreed about in section 3. In an all-equal triangle all three edges are pseudo-critical; in a triangle with distinct weights the two light edges are critical and the heavy one is in no MST.

**The minimax path.** The smallest possible heaviest edge on a path from `s` to `t` is found by Kruskal stopped the moment `s` and `t` are in one component: the last edge added is the answer, because the edges added so far form every connection that uses only edges up to that weight. (The MST contains a minimax path between every pair; this is the same quantity as the "minimum effort" of Topic 05, found there with a Dijkstra loop.)

```
bottleneck 0->4: 9
bottleneck 0->8: 8
bottleneck 3->3: 0
unreachable: inf
```

**Not a shortest-path tree.** The MST minimises total weight; the shortest-path tree from a vertex minimises each distance from it. They are different trees, and the path between two vertices in an MST need not be the shortest path in the graph. It is, however, a path of minimum heaviest edge (above). One more trick: if vertices have an "own" cost as well (a well that can be drilled at a village), add one virtual vertex joined to each vertex by an edge of that cost; an MST of the extended graph decides which villages get a well and which are piped (LeetCode 1168, answer to problem D).

## 7. Choosing

| Situation | Use | Why |
|---|---|---|
| Edge list, sparse graph | Kruskal | a sort and a union-find; faster than heap Prim here |
| Adjacency list already built, or edges arrive on demand | Prim with a heap | no global sort |
| Complete graph, distances computed from coordinates | Prim with an array | O(N^2) time, O(N) memory, no edges stored |
| Graph may be disconnected | either; read `comps` | the result is a spanning forest |
| Largest total instead of smallest | Kruskal with `>` | the cut property holds for heaviest edges too |
| "Is this edge in every / some MST?" | recompute without, then with | section 6 |
| Path with the smallest heaviest edge | Kruskal until `s` and `t` join | section 6 |
| Distances from one vertex | Dijkstra (Topic 05), not an MST | different objective |

## 8. What the tests check, and what they cannot

Of the 30 mutants, 25 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Five survive, and each is an equivalent change as far as the answers go: the mutated program returns the same answers as the original on every input. Two of them change only speed, which the tests, looking at answers and at counters of examined edges, do not measure. The reasons:

- **Removing union by size.** The shape of the union-find trees changes, never which elements are together. Without it (and with path halving) the cost is still small in practice, but it is no longer the textbook bound.
- **Removing path halving.** The same: `find` returns the same root, only more slowly.
- **Pushing edges to vertices already in the tree.** Those entries are skipped when popped (the stale test), so the result is the same, with a larger heap.
- **`<=` instead of `<` in the array scan of `prim_points`.** Two vertices with equal `best` can be taken in either order; the total is the same.
- **`<=` instead of `==` when comparing the forced-in weight with the base weight.** Forcing an edge in cannot give a total below the minimum, so `<=` and `==` agree.

The graph with a million vertices has no brute-force reference, so it is checked by two independent algorithms agreeing on the weight and the number of trees, plus the structure of the answer (the chosen edges form a forest with exactly V minus components edges, summing to the reported total). The cycle property is checked as a certificate on a smaller graph, which proves minimality directly: a spanning tree in which no non-tree edge is lighter than the tree path it closes is a minimum one. What the tests cannot do is cover weights near the limit of a 64-bit total, or graphs too large to sort in memory.

Mutants are chosen where a bug is plausible, not at random, so "25 of 30" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Assuming the MST is unique.** Equal weights allow several; do not compare edge sets between algorithms, compare weights.
2. **Forgetting the disconnected case.** Check the number of components (`comps == 1`), or the answer "-1 / impossible" will be a forest.
3. **Using Dijkstra's key in Prim.** Pushing `dist[u] + w` instead of `w` builds a shortest-path tree, which is not minimal in total weight.
4. **Running Prim without the stale check.** Without `if (in[v]) continue;` a vertex is added twice and the total is too large.
5. **Building all N^2 edges for a geometric problem** when N is in the tens of thousands.
6. **Int overflow in the total.** One million edges of weight 10^6 need 64 bits.
7. **Comparing weights as the only sort key and expecting the same edge set every run.** Break ties by index if you need repeatable output, as the listing does.
8. **Reading an MST as a shortest-path structure.** The tree path between two vertices is not their shortest path.
9. **Counting self loops.** A self loop closes a cycle by itself; Kruskal rejects it (both ends already together), and the critical-edge test skips it.
10. **Recursive union-find `find` on a deep chain.** The listing is iterative with path halving.

## 10. Questions to answer out loud

1. State the cut property and prove it.
2. Why does Kruskal's rule "add an edge if it joins two components" never make a mistake?
3. Why does Prim's rule never make a mistake? What cut does it use?
4. How is Prim different from Dijkstra, line for line?
5. Can an MST have a different edge set under two correct algorithms? Can the weight differ?
6. What does Kruskal return on a disconnected graph? How do you detect it?
7. Why is Kruskal limited by its sort, and when does its early stop help?
8. For N points in the plane, why do we not build the edges? What is the cost of array Prim?
9. How do you tell whether an edge is in every MST? In some MST?
10. How does the MST give the smallest possible heaviest edge on a path from `s` to `t`?
11. Is the path between two vertices in the MST their shortest path? Give a small counterexample.
12. How would you find the maximum spanning tree? The second-smallest spanning tree?

## 11. Problems

### 11.1 On paper

**A.** Run Kruskal by hand on the edges `0-1:4, 0-2:1, 1-2:2, 1-3:5, 2-3:8, 2-4:10, 3-4:2` (five vertices). List the edges in the order examined, say which are kept, and give the total and the number of edges examined before the loop could stop.

**B.** Prove the cycle property: the unique heaviest edge of a cycle is in no MST.

**C.** How many spanning trees does the complete graph on 4 vertices have? If all six weights are equal, how many of them are minimum trees?

**D.** Wells and pipes: three villages with wells costing 1, 2, 2 and pipes `1-2` at 1 and `2-3` at 1. What is the cheapest way to supply water to all three? Model it as an MST.

**E.** Kruskal and Prim gave different edges on the example of section 3 and the same total. Which edges differ, and why does the tie not matter?

**F.** For N = 10^5 points, how many edges does the complete graph have, and how much memory do they take at 16 bytes each? What are the time and memory of array Prim?

**G.** For the graph `0-1:1, 1-2:1, 2-3:2, 0-3:2, 0-4:3, 3-4:3, 1-4:6`, which edges are critical and which pseudo-critical?

### 11.2 On LeetCode

For each: what are the vertices and edges, and which tool? Numbers and titles are from memory; check a number before you rely on it. Two are marked premium as far as I recall.

| Problem | Number | What to practise |
|---|---|---|
| Min Cost to Connect All Points | 1584 | Array Prim on points (section 5), or Kruskal if N is small |
| Connecting Cities With Minimum Cost (premium) | 1135 | Kruskal; return -1 if not connected |
| Find Critical and Pseudo-Critical Edges in Minimum Spanning Tree | 1489 | Section 6: recompute without and with each edge |
| Optimize Water Distribution in a Village (premium) | 1168 | Virtual vertex for the wells, then Kruskal |

### 11.3 In your editor

**A.** Implement Boruvka's algorithm (each component picks its lightest outgoing edge, all at once, repeat until one component remains); test against Kruskal on random graphs.

**B.** Second-best spanning tree: for each non-tree edge, replace the heaviest tree edge on the tree path between its ends; take the best replacement. Test against brute force.

**C.** Add `max_spanning_tree` and `minimum_bottleneck_tree` (the tree whose heaviest edge is smallest) and explain why Kruskal's tree is also a minimum bottleneck tree.

**D.** Replace the comparison sort in Kruskal with a counting sort for integer weights up to 1000, and time it against `stable_sort` on 4 million edges. Is the sort still the main cost?

**E.** Write a checker that, given a graph and a claimed MST, verifies the cycle property for every non-tree edge in O(E log V) or better; use it to test your own Prim.

### Answers

**A.** Sorted: `0-2:1`, `1-2:2`, `3-4:2`, `0-1:4`, `1-3:5`, ... Kept: `0-2`, `1-2`, `3-4`; `0-1` is skipped (0 and 1 are already joined through 2); `1-3` joins the two components and is kept. Total 1 + 2 + 2 + 5 = 10; five edges were examined before the tree was complete (checked by running the code).

**B.** Let `e` be the unique heaviest edge of a cycle and suppose an MST `T` contains it. Removing `e` splits `T` into two parts, and the rest of the cycle contains another edge `f` joining them. `T - e + f` is a spanning tree with `w(f) < w(e)`, so lighter than `T`, a contradiction.

**C.** 16 spanning trees (Cayley's formula n^(n-2), from memory; the number was also checked by counting subsets of edges). If all weights are equal, all 16 are minimum trees.

**D.** Add a virtual vertex 0 joined to villages 1, 2, 3 with edges of cost 1, 2, 2 (the wells), plus the pipes. The MST has total 3: the well at village 1 (cost 1), pipe `1-2` (1) and pipe `2-3` (1). Kruskal on this graph returns 3.

**E.** Kruskal kept `0-7` and Prim kept `1-2`, both of weight 8. In the example one of the two is enough to connect the same pair of regions, so either choice gives the same total; both edges are pseudo-critical (section 6).

**F.** N(N - 1) / 2 = 5 * 10^9 edges, about 8 * 10^10 bytes (80 GB). Array Prim: about N^2 = 10^10 steps and O(N) memory.

**G.** Critical: edges 0 and 1 (`0-1`, `1-2`, both weight 1, needed by every MST). Pseudo-critical: edges 2, 3, 4, 5 (`2-3`, `0-3`, `0-4`, `3-4`; each pair of equal weights can be swapped). Edge 6 (`1-4`, weight 6) is in no MST. This is the sample of the LeetCode problem as I remember it, and the code returns exactly these.

**11.2 hints.** 1584: the two samples give 20 and 18 (section 5). 1135: the sample with three cities and connections 5, 6 and 1 gives 6. 1168: sample total 3 as in problem D.

**11.3 hints.** C: any minimum spanning tree is also a minimum bottleneck tree, since the heaviest edge on its path between two vertices is a minimax path (section 6). E: the cycle property is the certificate used by the tests here.

---

## Cheat sheet

- **Cut property:** the lightest edge across any cut is in some MST. Cycle property: the heaviest edge of a cycle can be dropped.
- **Kruskal:** sort, `unite` when the ends are in different components; stop at V - 1 edges. Sort-dominated: the sort was 56 to 85% of the time; 1.9 to 2.8 times faster than heap Prim on a sparse graph.
- **Prim:** grow one tree with a heap of (edge weight, vertex); skip stale entries. Dijkstra's loop with the key `w` instead of `dist + w`.
- **Dense or geometric:** Prim with an array, O(N^2) time, O(N) memory; never store the N^2 edges.
- **Disconnected:** a spanning forest; check `comps`.
- **Ties:** the weight is unique, the edge set is not.
- **Critical / pseudo-critical:** recompute without the edge, then with it forced in.
- **Minimax path:** Kruskal until `s` and `t` join.
- **Not shortest paths:** use Dijkstra for distances.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Kruskal's and Prim's algorithms, the cut and cycle properties, Boruvka's algorithm and Cayley's formula are standard (named from memory; Kruskal 1956, Prim 1957). The remark about Delaunay and k-d tree methods for geometric MSTs is from memory and not implemented here.
- Problem numbers and titles in section 11, and the samples of problems 1584, 1135, 1168 and 1489, are LeetCode's, from memory; check a number before you rely on it.
