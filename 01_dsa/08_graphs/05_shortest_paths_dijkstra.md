# DSA 08.05: Shortest Paths I, Dijkstra

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Graph representations and traversal](01_representations_and_traversal.md) (the compressed-row graph), [Topological sort](03_topological_sort.md) (processing vertices in an order) and [BFS on states and grids](04_bfs_on_states_and_grids.md) (0-1 BFS and A*, which this topic generalises). There is no companion demo file: every structure is listed in this document.*

BFS finds the fewest edges. The moment edges have different costs, "fewest edges" and "cheapest" are different questions, and BFS answers the wrong one. **Dijkstra's algorithm** answers the right one for every graph whose edge costs are not negative, and it is the basis of routing protocols, map software and a long list of interview problems. The loop is the BFS loop with one change: the queue is replaced by a **priority queue** that always hands back the vertex with the smallest distance found so far. This topic builds it, proves why it works, shows exactly where it stops working (a negative edge), and then uses the same loop for things that are not sums at all: the largest step on a path, the product of probabilities, and a state that includes how many flights you have taken.

By the end you can:

- write Dijkstra with a binary heap and lazy deletion, state its cost as a function of V and E, and explain what a "stale" heap entry is;
- prove it correct in five lines and point at the sentence that needs the weights to be non-negative;
- build a three-vertex example where a negative edge gives a wrong answer, and say what the lazy version does instead;
- rebuild a path from `parent[]` and count the shortest paths modulo a prime, and explain why zero-weight arcs break the count;
- say, with measurements, how the heap compares with an ordered set, with an array scan and with BFS on unit weights, and how much an early exit saves;
- recognise a cost that Dijkstra can handle (minimax effort, maximum probability) and a cost that needs a bigger state (cheapest trip with at most K stops).

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 91,714 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: distances against Bellman-Ford on thousands of random graphs (directed and undirected, with zero weights, several sources, unreachable vertices); every returned path against the arc weights; the number of shortest paths against a brute-force enumeration of simple paths, and against Pascal's triangle on a 60 by 70 grid; the array and set variants against the heap; early exit against the full run; minimax effort against repeated relaxation to a fixpoint; maximum probability against Bellman-style relaxation; "at most K stops" against a layered Bellman-Ford; and a random graph of a million vertices and four million edges checked by a certificate (section 9). Then 58 hand-made mutants of the listings were run against the tests: **50 were caught and 8 are equivalent changes** (section 9).

**Data and timing.** Graphs are **synthetic** and seeded: a random graph with V = 1,000,000 vertices and 4,000,000 undirected edges (8,000,000 arcs), weights uniform in `1..w_max`; a complete directed graph on 3,000 vertices; a 2000 by 2000 grid. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. From BFS to Dijkstra

**Vocabulary.** A **weight** is the cost of an arc. The **length** of a path is the sum of its weights. `dist[v]` is the length of the shortest path from the source, or infinity if there is none. To **relax** an arc `u -> v` of weight `w` is to ask whether `dist[u] + w` beats the current `dist[v]`, and if so to lower it. A **tentative** distance is the best length found so far; a **final** distance is one that can no longer change.

BFS can finalise a vertex the moment it sees it, because the first time a vertex is reached is by a path with the fewest edges. With weights that is false: the first path found to `v` may be an expensive one. What stays true is a different statement.

> **The rule.** Among the vertices not yet final, the one with the smallest tentative distance is final.

*Why, for non-negative weights.* Let `u` be that vertex, with tentative distance `d`, and suppose a cheaper path to `u` existed. Follow it from the source; it starts at a final vertex and ends at `u`, so it has a first vertex `y` that is not final, and its predecessor `x` is final, so `x`'s arcs were relaxed and `dist[y]` is at most the length of the path up to `y`. That prefix is at most the whole path, which is less than `d`, because the weights after `y` are not negative. So `dist[y] < d`, but `u` had the smallest tentative distance, a contradiction. The sentence that uses the assumption is "the prefix is at most the whole path". With a negative edge after `y` the prefix can be longer than the whole path, and the argument, and the algorithm, fail (section 3).

So the algorithm is: keep a priority queue of (tentative distance, vertex); take the smallest; relax its arcs; repeat. That is Dijkstra (1956; from memory).

## 2. The listing

The graph is the compressed-row form of Topic 01 with a weight beside every arc. An undirected edge becomes two arcs.

```cpp
struct WEdge { int a, b; long long w; };
// Compressed rows with a weight per arc (Topic 01). An undirected edge becomes two arcs.
struct WCsr { int n = 0; std::vector<int> off, to; std::vector<long long> w; };
WCsr build_wcsr(int n, const std::vector<WEdge>& e, bool directed) {
    WCsr g; g.n = n; g.off.assign(n + 1, 0);
    for (auto& x : e) { ++g.off[x.a + 1]; if (!directed) ++g.off[x.b + 1]; }
    for (int u = 0; u < n; ++u) g.off[u + 1] += g.off[u];
    g.to.resize(g.off[n]); g.w.resize(g.off[n]); std::vector<int> at(g.off.begin(), g.off.end() - 1);
    for (auto& x : e) {
        g.to[at[x.a]] = x.b; g.w[at[x.a]++] = x.w;
        if (!directed) { g.to[at[x.b]] = x.a; g.w[at[x.b]++] = x.w; }
    }
    return g;
}
const long long INF = LLONG_MAX / 4;
```

```cpp
// Dijkstra with a binary heap and "lazy deletion": instead of lowering a key inside the heap we push a new (distance, vertex) pair
// and ignore older pairs when they come out (their distance is larger than the best known). Every pair costs one push and one pop.
// Rule that makes it correct: the vertex with the smallest tentative distance can no longer be improved, because any other route to it
// goes through a vertex that is at least as far, and the weights are not negative.
// Outputs: dist (INF if unreachable), parent (the previous vertex on one shortest path), ways (the NUMBER of shortest paths, mod 1e9+7;
// only meaningful when all weights are positive). `target` >= 0 stops as soon as that vertex is taken from the heap.
// finalize = true is the textbook version that never revisits a vertex; with a negative edge it can return a wrong answer (section 3).
struct Shortest { std::vector<long long> dist, ways; std::vector<int> parent; long long pops = 0, pushes = 0; };
Shortest dijkstra(const WCsr& g, const std::vector<int>& sources, int target = -1, bool finalize = false) {
    const long long MOD = 1000000007LL; int n = g.n; Shortest r;
    r.dist.assign(n, INF); r.ways.assign(n, 0); r.parent.assign(n, -1); std::vector<char> done(n, 0);
    using Item = std::pair<long long, int>; std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (int s : sources) { r.dist[s] = 0; r.ways[s] = 1; pq.push({0, s}); ++r.pushes; }
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop(); ++r.pops;
        if (finalize ? done[u] : d > r.dist[u]) continue;                          // an older, worse pair for u
        done[u] = 1; if (u == target) break;
        for (int i = g.off[u]; i < g.off[u + 1]; ++i) {
            int v = g.to[i]; long long nd = d + g.w[i];
            if (nd < r.dist[v]) { r.dist[v] = nd; r.parent[v] = u; r.ways[v] = r.ways[u]; pq.push({nd, v}); ++r.pushes; }
            else if (nd == r.dist[v]) r.ways[v] = (r.ways[v] + r.ways[u]) % MOD;      // another shortest route
        }
    }
    return r;
}
// The path from a source to t as a list of vertices (empty if unreachable): follow parent[] backwards, then reverse.
std::vector<int> path_to(const Shortest& r, int t) {
    std::vector<int> p; if (r.dist[t] >= INF) return p;
    for (int v = t; v != -1; v = r.parent[v]) p.push_back(v);
    std::reverse(p.begin(), p.end()); return p;
}
```

**Lazy deletion.** The standard library heap has no "lower this key" operation. Instead of lowering a key we **push a new pair** and leave the old, worse pair in the heap. When an old pair comes out, its distance is larger than the best known for that vertex, so we recognise it as **stale** and skip it (`d > r.dist[u]`). Every push is popped exactly once (the tests assert `pops == pushes` for a full run), and a vertex is *expanded* only the first time it comes out with its best distance.

Here is the loop on the classic six-vertex example (undirected; edges 0-1:7, 0-2:9, 0-5:14, 1-2:10, 1-3:13, 2-3:11, 2-5:2, 3-4:6, 4-5:9). Every line is the output of an instrumented copy of the loop, which is compared with `dijkstra()` at the end.

```
pop (0,0) final
  push (7,1)
  push (9,2)
  push (14,5)
pop (7,1) final
  push (20,3)
pop (9,2) final
  push (11,5) improves
pop (11,5) final
  push (20,4)
pop (14,5) stale, skipped
pop (20,3) final
pop (20,4) final
agree=1
```

Look at what happened to vertex 5. It was reached first at distance 14 (straight from 0), then at 11 (through 2). The 14 stayed in the heap, came out after the 11 had already been used, and was skipped. That is the whole cost of lazy deletion: a few extra heap entries.

**Cost.** Each arc causes at most one push, so pushes are at most arcs plus the number of sources (a test pins this). A push or pop on a heap of at most `E` entries costs `O(log E)`, and `log E <= 2 log V`, so the total is `O((V + E) log V)`. With a Fibonacci heap the bound is `O(E + V log V)` (Fredman and Tarjan, 1984; from memory), which is rarely faster in practice. Memory is the arrays plus the heap, up to `E` entries.

**How many entries are really pushed?** Measured on the random graph (8,000,000 arcs):

| Weights | Reachable | Pushes | Stale pops | Heap (ms) | Ordered set (ms) |
|---|---|---|---|---|---|
| all 1 | 999,674 | 999,674 | 0 | 327 / 402 | 858 / 890 |
| 1 to 100 | 999,685 | 1,774,714 | 775,029 | 747 / 776 | 2,549 / 2,450 |
| 1 to 10^9 | 999,651 | 1,804,613 | 804,962 | 714 / 852 | 2,546 / 2,567 |

With all weights equal there are no stale entries at all: every vertex is first reached by its best path, as in BFS. With a spread of weights about 1.8 entries are pushed per vertex, so about 44% of pops are stale (775,029 of 1,774,714 pops at weights 1 to 100). That is far below the 8 million arcs: most relaxations do not improve anything.

**The early exit.** If you only need one target, stop when it comes **out** of the heap (`break` after `done[u] = 1`), not when it is first pushed: at the push its distance may still be improved, at the pop it is final. On this graph with a random target the search popped 301,531 of the 1,774,714 entries (17.0%) and ran 2.4 to 2.8 times faster. The saving depends on where the target is: on the 2000 by 2000 grid (weights 1..9), a target at the far corner needed 100.0% of the pops of the full run, a target a quarter of the way in needed 10.4%.

## 3. Why a negative edge breaks it

Take four vertices and arcs `0->1` weight 2, `0->2` weight 3, `2->1` weight -2, `1->3` weight 1. The shortest path to 3 is `0, 2, 1, 3`, length 2. Dijkstra pops 1 at distance 2 and expands it (3 gets 3), then pops 2 at distance 3 and relaxes `2->1` down to 1, but vertex 1 is already final in the textbook version and is never expanded again.

```
v=0 finalized=0 lazy=0
v=1 finalized=1 lazy=1
v=2 finalized=3 lazy=3
v=3 finalized=3 lazy=2
pops finalized=5 lazy=6
```

The `finalize` column is the textbook version (`finalize = true`): vertex 1 is correct at 1, but vertex 3 still says 3, because the improvement to 1 was never passed on. This is a wrong answer without any error: the output is a valid walk, just not the shortest.

The lazy version (default) gets this example right, because a vertex whose distance drops is pushed again and expanded again. That makes it a *correct* algorithm for negative edges without negative cycles, but at a price: a vertex can be expanded many times. A family of graphs I built to show this, `k` steps each with a direct arc and a detour that ends in a negative arc, makes the number of pops grow with the square of `k`:

```
k=2 V=5 pops=9
k=8 V=17 pops=81
k=14 V=29 pops=225
k=20 V=41 pops=441
```

That is 441 pops for 41 vertices, and it keeps growing faster than the vertex count. I have not constructed an exponential family; the literature, as far as I recall, contains graphs with negative edges on which re-inserting Dijkstra takes exponential time, which is why nobody uses it as the general method. And with a negative cycle reachable from the source, the lazy version never stops, because each trip around the cycle lowers a key (reasoning, not a measurement). For negative edges use Bellman-Ford, and for many sources Johnson's reweighting, both in the next topic.

**Why not add a constant to every weight?** A tempting repair for negative edges: add `c` to every arc so all weights are non-negative, run Dijkstra, subtract. It does not work, because a path with more arcs gains more. With arcs `s->t` weight 1, `s->a` weight 2, `a->t` weight -2, the path through `a` has length 0 and beats the direct arc (1). Add 2 to everything: the direct arc is 3, the path through `a` is 4 + 0 = 4, and the order flips (answer B in section 12).

## 4. Paths and counting paths

`parent[v]` records the vertex that last improved `v`, so following it back from the target gives a shortest path in reverse; `path_to` does that and reverses the list. Following `parent` always ends at a source: a vertex's parent was expanded before the vertex itself was, so the chain runs strictly backwards in pop order.

`ways[v]` counts the shortest paths to `v` modulo 1,000,000,007. When a strictly better distance is found, the count is *replaced* by the count of the predecessor; when an equal distance is found, the count is *added*. This is the usual "number of ways to arrive" problem.

```
v=0 dist=0 parent=-1 ways=1
v=1 dist=7 parent=0 ways=1
v=2 dist=9 parent=0 ways=1
v=3 dist=20 parent=1 ways=2
v=4 dist=20 parent=5 ways=1
v=5 dist=11 parent=2 ways=1
path to 4: 0 2 5 4
pushes=7 pops=7 arcs=18
target 3: pops=6 dist=20
```

Vertex 3 has two shortest paths (0-1-3 and 0-2-3, both 20), and its `parent` records only the first one found. The reconstruction therefore returns *a* shortest path, not all of them. The target-3 line shows the early exit: 6 pops instead of 7.

**Counting needs positive weights.** With a zero-weight arc the order in which equal-distance vertices come out matters, and a vertex can be given extra paths *after* it has passed its count on:

```
v=0 dist=0 ways=1
v=1 dist=1 ways=2
v=2 dist=1 ways=1
v=3 dist=2 ways=1
```

Vertex 3 has two shortest paths (0-1-3 and 0-2-1-3), yet `ways[3]` is 1: vertex 1 passed on its count of 1 before the zero-weight arc `2->1` raised it to 2. The distances are right; only the count is wrong. The tests check counts with positive weights only, and this one case is pinned as a known failure. If zero weights are possible, count in a second pass over the vertices sorted by distance, with zero-weight arcs ordered topologically (not implemented here).

## 5. Three ways to hold the frontier

```cpp
// Two other ways to hold the "frontier". First: no heap at all, scan an array for the smallest unfinished vertex each round. That is
// O(V^2) whatever the weights, which equals O(E) when E is about V^2 (section 6 measures it against the heap). w[u][v] < 0 means "no
// edge" here; real weights are >= 0.
std::vector<long long> dijkstra_n2(const std::vector<std::vector<long long>>& w, int src) {
    int n = w.size(); std::vector<long long> dist(n, INF); std::vector<char> done(n, 0); dist[src] = 0;
    for (int it = 0; it < n; ++it) {
        int u = -1; for (int v = 0; v < n; ++v) if (!done[v] && (u < 0 || dist[v] < dist[u])) u = v;
        if (u < 0 || dist[u] >= INF) break;
        done[u] = 1; for (int v = 0; v < n; ++v) if (w[u][v] >= 0 && dist[u] + w[u][v] < dist[v]) dist[v] = dist[u] + w[u][v];
    }
    return dist;
}
// "Decrease key" with an ordered set: erase the old pair, insert the new one. The set never holds a stale pair, at the price of a
// node allocation per insert.
std::vector<long long> dijkstra_set(const WCsr& g, int src) {
    std::vector<long long> dist(g.n, INF); std::set<std::pair<long long, int>> q; dist[src] = 0; q.insert({0, src});
    while (!q.empty()) {
        auto [d, u] = *q.begin(); q.erase(q.begin());
        for (int i = g.off[u]; i < g.off[u + 1]; ++i) {
            int v = g.to[i]; long long nd = d + g.w[i];
            if (nd < dist[v]) { if (dist[v] < INF) q.erase({dist[v], v}); dist[v] = nd; q.insert({nd, v}); }
        }
    }
    return dist;
}
```

The **array scan** replaces the heap by a linear search for the smallest unfinished vertex. It does `V` rounds of `O(V)` work, so `O(V^2)` whatever the weights, and it needs an adjacency matrix. The **ordered set** is "decrease key" done by erasing the old pair and inserting the new one; it never holds a stale entry but allocates a tree node per insert. The measurements:

| Case | Heap (ms) | Array scan (ms) | Ordered set (ms) |
|---|---|---|---|
| Random graph, weights 1 to 100 (V = 10^6, 8 x 10^6 arcs) | 747 / 776 | not run (needs a matrix) | 2,549 / 2,450 |
| Complete graph, V = 3,000, weights 1 to 1000 (8,997,000 arcs) | 22 / 24 | 23 / 21 | 27 / 27 |

On the sparse random graph the heap is faster than the set by 2.2 to 3.6 times in every row, in both runs, even though the set holds fewer entries: a binary heap in a vector is compact and the set is a tree of separate allocations. On the complete graph (3,000 vertices, 8,997,000 arcs) all three finish within a few milliseconds of each other: the work is dominated by reading the 9 million arcs, and the heap pushed only 20,030 entries in total. I expected the array scan to win there and it did not clearly, so do not choose it for speed alone; it is useful when V is small and the graph is a matrix anyway.

**Unit weights.** With every weight 1, Dijkstra reproduces BFS exactly (the tests compare them) and wastes only the heap: Dijkstra took 327 / 402 ms and BFS 205 / 179 ms (run 1 / run 2), a factor of 1.6 to 2.2. The rule of thumb from Topic 04 stands: use BFS for equal weights, 0-1 BFS for 0 and 1, Dijkstra beyond.

## 6. Other costs: minimax and probability

Dijkstra does not need the cost of a path to be a *sum*. It needs two things: extending a path never makes it better, and if one path is at least as good as another, it is still at least as good after both are extended by the same arc. Sums of non-negative weights satisfy both. So do these, and the loop is unchanged except for the combining operation.

```cpp
// Dijkstra only needs the cost of a path to be a function that never gets better as the path grows. "Sum" is one such function.
// The largest step on the path ("minimax") is another: the effort of a route over a height map is the biggest height difference
// between neighbouring cells on it, and we want the route with the smallest effort. Same loop, with max in place of +.
int min_effort(const std::vector<std::vector<int>>& h) {
    int H = h.size(), W = h[0].size(); std::vector<int> best(H * W, INT_MAX); using Item = std::pair<int, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq; best[0] = 0; pq.push({0, 0});
    static const int DR[4] = {-1, 1, 0, 0}, DC[4] = {0, 0, -1, 1};
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop(); if (d > best[u]) continue; if (u == H * W - 1) return d;
        for (int k = 0; k < 4; ++k) {
            int r = u / W + DR[k], c = u % W + DC[k]; if (r < 0 || r >= H || c < 0 || c >= W) continue;
            int nd = std::max(d, std::abs(h[r][c] - h[u / W][u % W])), v = r * W + c;
            if (nd < best[v]) { best[v] = nd; pq.push({nd, v}); }
        }
    }
    return best[H * W - 1];
}
// The most likely route: the probability of a path is the PRODUCT of its edge probabilities (each at most 1), so it never grows along
// a path; use a max-heap and "bigger is better". An undirected graph; returns 0 if t cannot be reached.
double max_probability(int n, const std::vector<std::tuple<int, int, double>>& e, int s, int t) {
    std::vector<std::vector<std::pair<int, double>>> adj(n); for (auto& [a, b, p] : e) { adj[a].push_back({b, p}); adj[b].push_back({a, p}); }
    std::vector<double> best(n, 0.0); std::priority_queue<std::pair<double, int>> pq; best[s] = 1.0; pq.push({1.0, s});
    while (!pq.empty()) {
        auto [p, u] = pq.top(); pq.pop(); if (p < best[u]) continue; if (u == t) return p;
        for (auto [v, q] : adj[u]) if (p * q > best[v]) { best[v] = p * q; pq.push({best[v], v}); }
    }
    return 0.0;
}
```

**Minimum effort** (a grid of heights; the effort of a route is the largest height difference between neighbouring cells on it; find the route with the smallest effort). The cost of a path is the *maximum* step. Replace `+` by `max` and the loop is Dijkstra's. This is the idea behind "Path With Minimum Effort" and, with cell values instead of differences, "Swim in Rising Water" (exercise C in section 12.3). On the 2000 by 2000 grid with random heights up to 10^6 the answer is 441,102 and the search took 727 / 700 ms for four million cells.

**Maximum probability.** The probability of a path is the product of its edge probabilities, each at most 1, so extending a path can only lower it. "Better" now means bigger, so use a max-heap. (An alternative is to minimise `-log p`, which turns the product into a sum; the direct product avoids the logarithm.)

On the samples from the problem statements the functions give:

```
min_effort=2
max_prob=0.2500
k=0 cheapest=500
k=1 cheapest=200
```

A cost that fails the test: *the average edge weight on the path.* Extending a path can lower the average, so a "final" vertex can later be improved. Whenever a problem asks for something like that, check the two conditions before reaching for Dijkstra.

## 7. State-augmented Dijkstra

"The cheapest trip from `src` to `dst` with at most `k` stops." The price alone is not a state, because a cheap route with many flights can be worse (it may use too many) than a dearer one with few. Plain Dijkstra on the cities marks a city final at its cheapest price and so can discard exactly the route that respects the limit:

```
plain dist to 3=3
k=0 cheapest=10
k=1 cheapest=10
k=2 cheapest=3
k=3 cheapest=3
```

With flights 0->1, 1->2, 2->3 at price 1 and a direct 0->3 at price 10, plain Dijkstra says 3 for city 3. With `k = 0` (one flight) the answer must be 10, and it stays 10 for `k = 1` (two flights are still not enough) until `k = 2`, when it becomes 3. As in Topic 04, the cure is to put the budget into the state.

```cpp
// State-augmented Dijkstra: "the cheapest trip from src to dst using at most k stops (so at most k + 1 flights)". The cost alone is not a
// state, because a cheap route with many flights can be worse than a dearer one with few: the state is (city, flights taken so far).
// n * (k + 2) states, Dijkstra by price. The first time dst is taken from the heap, in any layer, its price is the cheapest. -1 if none.
long long cheapest_within_k(int n, const std::vector<std::array<int, 3>>& flights, int src, int dst, int k) {
    int L = k + 2; std::vector<std::vector<std::pair<int, int>>> adj(n); for (auto& f : flights) adj[f[0]].push_back({f[1], f[2]});
    std::vector<long long> dist((size_t)n * L, INF); using Item = std::pair<long long, int>; std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    dist[(size_t)src * L] = 0; pq.push({0, src * L});
    while (!pq.empty()) {
        auto [d, id] = pq.top(); pq.pop(); if (d > dist[id]) continue;
        int city = id / L, used = id % L; if (city == dst) return d; if (used == L - 1) continue;
        for (auto [v, price] : adj[city]) { int nid = v * L + used + 1; if (d + price < dist[nid]) { dist[nid] = d + price; pq.push({dist[nid], nid}); } }
    }
    return -1;
}
```

The state is (city, flights used), `n * (k + 2)` states, and Dijkstra runs over them by price. The first time `dst` comes out of the heap, in any layer, its price is the cheapest, so the function returns immediately. The same answer comes from `k + 1` rounds of Bellman-Ford over copies of the distance array, in `O(k * E)`, and that is what the tests use as the reference. A common refinement, not implemented here, is to skip a state if the same city was reached before with at most as many flights and at most as high a price.

## 8. Choosing

| Situation | Use | Why |
|---|---|---|
| All weights equal | BFS | no heap; 1.6 to 2.2 times faster than Dijkstra here |
| Weights 0 or 1 | 0-1 BFS (Topic 04) | deque, no heap |
| Non-negative weights, one source | Dijkstra with a binary heap | the default |
| Many sources, nearest-source distance | Dijkstra with all sources pushed at 0 | one run, as in multi-source BFS |
| One target | Dijkstra with early exit; A* if coordinates exist (Topic 04) | stop at the pop |
| Complete or very dense graph | heap or array scan | measured the same here |
| Cost is max or a product | the same loop with that operation | section 6 |
| Limit on stops, tickets, fuel | Dijkstra on (vertex, budget) | section 7 |
| Negative edges, no negative cycle | Bellman-Ford (next topic) | Dijkstra can re-expand a lot |
| DAG with any weights | relax in topological order | `O(V + E)`, Topic 03 |
| All pairs | Floyd-Warshall, or Dijkstra from every vertex (next topic) | |

## 9. What the tests check, and what they cannot

Of the 58 mutants, 50 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Eight survive, and each is an equivalent change as far as the answers go: the mutated program returns the same answers as the original on every input. Five of them change only how much work is done, and the tests, which look at answers, pop counts and push counts but not at the number of arcs examined, cannot see work. The reasons:

- **`d < r.dist[u]` instead of `d > r.dist[u]` as the stale test.** Now stale entries are expanded instead of skipped. They cannot change anything: a stale entry has `d > dist[u]`, so `d + w > dist[u] + w >= dist[v]`, and neither the "better" nor the "equal" branch fires. It is the slowdown of mistake 4 in section 10, invisible to answer checks.
- **`<=` instead of `<` when the array scan picks the smallest vertex.** Two unfinished vertices at the same distance are both final; which goes first does not matter.
- **Dropping `|| dist[u] >= INF` from the array scan.** The vertices left are unreachable; relaxing from them tries `INF + w < INF`, which is false.
- **Not erasing the old pair in the ordered set.** The old pair stays and is expanded later with a larger distance, which improves nothing (the same argument as the first item). Only the set grows.
- **Removing the early return in `min_effort`.** The loop then runs until the heap is empty and returns the same `best` of the last cell. Time only.
- **`<=` instead of `<` when pushing a state in `cheapest_within_k`.** An equal pair is pushed twice and expanded twice with identical results.
- **Removing the stale test in `cheapest_within_k`.** The same argument as the first item.
- **Starting the source state at 1 instead of 0 in `dist`.** The heap still holds `(0, state)`, which is not stale against 1, and no arc leads back into layer 0, so the stored 1 is never compared with anything.

The million-vertex run has no independent reference to compare with (Bellman-Ford over four million edges would be too slow), so it is checked by a **certificate**: the heap and set versions agree; for every arc `dist[v] <= dist[u] + w` (so no vertex is too far); and for every reached vertex other than the source, the arc from `parent[v]` is **tight** (`dist[parent] + w == dist[v]`, so every distance is realised by a real path). Together these prove each distance is exactly right, whatever algorithm produced it, so the check does not share a misunderstanding with the code. What the tests cannot do is cover weights near `INF`, graphs with parallel arcs and self loops in the large runs (the small runs include both), or a negative cycle; and counts are verified only for positive weights.

Mutants are chosen where a bug is plausible, not at random, so "50 of 58" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 10. Common mistakes

1. **Using `INT_MAX` or `LLONG_MAX` as infinity and then adding.** `INF + w` overflows and wraps to a negative number, which then wins every comparison. Use an infinity such as `LLONG_MAX / 4`, or test `dist[u] < INF` before adding. Use 64-bit distances: path lengths over a million arcs of 10^4 overflow `int`.
2. **Marking a vertex done when it is pushed.** That is the BFS rule. In Dijkstra a vertex is final when it is *popped*; a vertex pushed at 14 may still be improved to 11.
3. **Stopping at the first push of the target** instead of the first pop.
4. **Forgetting the stale check.** Without `d > dist[u]` the answer is still right but every stale entry is expanded again; on a bad graph that is a large slowdown.
5. **Running it on negative edges** and trusting the answer (section 3).
6. **Counting paths with zero-weight arcs** (section 4).
7. **Plain Dijkstra with a budget.** If the question has a limit on stops or fuel, the vertex is not the state (section 7).
8. **Comparing doubles for equality** in the probability version; the tests use a tolerance.
9. **A max-heap by mistake.** `std::priority_queue` is a max-heap; for distances give it `std::greater` (or push negated keys).
10. **Rebuilding the adjacency list per query** in a many-query problem; build the compressed rows once.

## 11. Questions to answer out loud

1. What is a stale entry, and why can the heap hold more than V entries? What is the largest it can hold?
2. State the rule that makes Dijkstra correct, and say which word of the proof needs non-negative weights.
3. Why is the lazy-deletion version `O((V + E) log V)` and not `O(V * E)`?
4. Give a three-vertex graph with a negative edge where the textbook version is wrong.
5. Why does adding a constant to all weights not repair negative edges?
6. What changes in the loop to count shortest paths? Why does the count need positive weights?
7. How do you rebuild the path? Does it find all shortest paths?
8. When can you stop early, and why at the pop and not at the push?
9. On unit weights, what does Dijkstra cost compared with BFS, and what do you lose by using it?
10. Which of these can Dijkstra handle: minimise the largest edge; minimise the average edge; maximise the product of probabilities; minimise cost with at most K edges? What is the extra ingredient in each case?
11. Why is `(city, flights used)` a state but `city` alone is not?
12. Your graph has 10^5 vertices and 10^5 edges: array scan or heap? And 3,000 vertices with all pairs connected?
13. What does the lazy version do when the source can reach a negative cycle?

## 12. Problems

### 12.1 On paper

**A.** Run Dijkstra by hand on the directed graph `0->1:4, 0->2:1, 2->1:2, 2->3:7, 1->3:1, 3->4:3, 1->4:8` from vertex 0. List every pop in order, mark the stale ones with `*`, and give the final distances and the numbers of pushes and pops.

**B.** In section 3, after adding 2 to every weight, which path wins and by how much? What are the new lengths?

**C.** For the undirected graph `0-1, 0-2, 1-2, 1-3, 2-3` (all weight 1) plus `3-4` weight 2 and `0-4` weight 5, give `dist` and the number of shortest paths to every vertex.

**D.** Show that the heap never holds more than `E + 1` entries on a single-source run, and explain why the bound is far from tight in the table of section 2.

**E.** Prove that the vertices come out of the heap in non-decreasing order of final distance (with non-negative weights).

**F.** In the graph of section 3 the lazy version pops 6 entries and the textbook version 5. List the pops of the lazy version in order, mark the stale one, and say which vertex is expanded twice.

**G.** Show that "minimise the average edge weight" breaks the second condition of section 6: give two paths where the first is at least as good as the second, and one arc which, added to both, makes the second strictly better.

### 12.2 On LeetCode

For each: what is the state, what is the cost, and which part of this topic? Hints are in the answers; try first. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Network Delay Time | 743 | Dijkstra from one source; answer is the largest distance, or -1 |
| Path With Minimum Effort | 1631 | Minimax on a grid (section 6) |
| Cheapest Flights Within K Stops | 787 | State = (city, flights used) (section 7) |
| Path With Maximum Probability | 1514 | Max-heap, product of probabilities |
| Swim in Rising Water | 778 | Minimax over cell values |
| Number of Ways to Arrive at Destination | 1976 | Count shortest paths mod 1e9+7 (section 4) |

### 12.3 In your editor

**A.** Write `dijkstra` from memory without looking, then run it against a Bellman-Ford you also write, on 1,000 random graphs. Add the stale check last and see what changes in the pop count.

**B.** Add `std::vector<std::vector<int>> all_paths(...)` that returns every shortest path to a target (use the predecessor lists, not the single parent). Test it against `ways[]`.

**C.** Write "Swim in Rising Water": the cost of a path is the largest cell **value** on it, start and end included. Change two lines of `min_effort`.

**D.** Reconstruct the path in `cheapest_within_k` (store the parent of each (city, flights) state) and return it with the price.

**E.** Time heap vs set vs `std::priority_queue` with a pre-reserved vector on your machine, for `w_max = 1, 100, 10^9`. Do your rankings match the table?

### Answers

**A.** Pops in order: `(0,0) (1,2) (3,1) (4,1)* (4,3) (7,4) (8,3)* (11,4)*`. Final distances `0 3 1 4 7` for vertices 0 to 4. Pushes 8, pops 8, stale 3. Vertex 1 is first pushed at 4 (from 0), then improved to 3 through 2; vertex 3 is first pushed at 8 (from 2), then improved to 4 through 1; vertex 4 is first pushed at 11 (from 1), then improved to 7 through 3.

**B.** Before the shift: direct = 1, through `a` = 2 + (-2) = 0, so the path through `a` wins by 1. After adding 2 to every arc: direct = 3, through `a` = 4 + 0 = 4, so the direct arc wins by 1. The shift adds `2` per arc, and the path with two arcs gains 4 where the one-arc path gains 2.

**C.** Distances `0 1 1 2 4` for vertices 0 to 4; number of shortest paths `1 1 1 2 2`. Vertex 3 is reached by 0-1-3 and 0-2-3 (length 2; 0-1-2-3 has length 3). Vertex 4 by 0-1-3-4 and 0-2-3-4 (length 4; the direct arc costs 5).

**D.** Every push comes from an arc relaxation or from a source, and each arc is relaxed at most once (when its tail is expanded), so pushes, and hence heap size, are at most `E + 1`. In the table only 1.8 pushes per vertex occur, because most relaxations do not improve a distance; only strict improvements push.

**E.** Induction: when `u` is popped with distance `d`, every vertex still in the heap has tentative distance at least `d` (heap order), and every vertex pushed later is pushed with `d' = d_x + w >= d_x >= d` for the vertex `x` being expanded, using `w >= 0`. So the pop sequence never decreases.

**F.** `(0,0) (2,1) (3,2) (1,1) (2,3) (3,3)*`. Vertex 1 is expanded at distance 2 and again at distance 1; the stale entry is `(3,3)`, which was superseded by `(2,3)`. The textbook version skips the second expansion of 1 (that is why it is wrong), and so has one pop fewer.

**G.** Path P1 is the single arc 2 (average 2); path P2 is the arcs 1 and 3 (average 2): P1 is at least as good as P2. Add an arc of weight 10 to both: P1 becomes (2, 10) with average 6, P2 becomes (1, 3, 10) with average 4.67, so P2 is now strictly better. The order flipped, so a vertex reached "first" by the better average cannot be called final.

**12.2 hints.** 743: run from `k`, take the maximum over all distances; if any is infinite, return -1; the vertices are 1-based. 1631: state = cell, cost = maximum absolute difference so far (the table's `min_effort`; the examples give 2 and 1). 787: state = (city, flights); `k` stops means `k + 1` flights (examples: 700 for the first sample, 200 with `k = 1` and 500 with `k = 0` in the second). 1514: max-heap; the sample returns 0.25. 778: minimax over cell values: samples 3 and 16. 1976: `ways[]`; the sample has time 7 and 4 ways.

**12.3 hints.** C: the start value counts, so initialise `best[0] = h[0][0]` and combine with `max(d, h[r][c])`. E: expect the set to be slowest on sparse graphs.

---

## Cheat sheet

- **Rule:** the unfinished vertex with the smallest tentative distance is final (non-negative weights only).
- **Loop:** pop the smallest `(d, u)`; skip it if `d > dist[u]` (stale); relax every arc; push on strict improvement. `O((V + E) log V)`.
- **Early exit:** stop when the target is popped, not when it is pushed.
- **Heap vs set vs array:** the heap beat the ordered set by 2.2 to 3.6 times on sparse graphs; all three were within a few milliseconds on a complete graph of 3,000 vertices.
- **Unit weights:** use BFS (1.6 to 2.2 times faster than Dijkstra here).
- **Negative edge:** the textbook version can be silently wrong (section 3); use Bellman-Ford.
- **Paths:** `parent[]` and reverse; count paths with `+=` on a tie, `=` on an improvement, positive weights only.
- **Other costs:** any cost where extending never helps and order is preserved: max, products of numbers at most 1.
- **Budgets:** state = (vertex, budget used); `n * (k + 2)` states.
- **Infinity:** `LLONG_MAX / 4`; distances in 64 bits.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Dijkstra's algorithm (1956) and the Fibonacci-heap bound (Fredman and Tarjan, 1984) are named from memory. The statement that re-inserting Dijkstra can take exponential time on some graphs with negative edges is also from memory of the literature; the quadratic family in section 3 is my own and measured. The correctness argument in section 1 is the standard one.
- Problem numbers and titles in section 12 are LeetCode's, from memory; check a number before you rely on it.
