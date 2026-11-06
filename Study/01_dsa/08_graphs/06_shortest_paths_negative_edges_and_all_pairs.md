# DSA 08.06: Shortest Paths II, Negative Edges and All Pairs

*Part of the [Graphs plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Topological sort](03_topological_sort.md) (Kahn's order) and [Shortest paths I: Dijkstra](05_shortest_paths_dijkstra.md) (relaxation, why a negative edge breaks the rule). There is no companion demo file: every structure is listed in this document.*

Dijkstra needs every weight to be non-negative. Real problems do not always oblige: a price difference, a refund, a gain on a currency exchange, a potential difference. This topic is what you reach for then. **Bellman-Ford** handles negative edges from one source and tells you when the question has no answer (a negative cycle). A **DAG** needs neither: relax in topological order and any weights work, even for the longest path. **Floyd-Warshall** answers every pair at once in three nested loops and also gives the transitive closure. **Johnson's algorithm** repairs the weights so that Dijkstra can run from every vertex. The topic ends with the budget version ("at most k arcs"), where Bellman-Ford's rounds are exactly the budget.

By the end you can:

- explain why a shortest path has at most V - 1 arcs when there is no negative cycle, and why V - 1 rounds of relaxing every arc therefore suffice;
- write Bellman-Ford with early exit, detect a negative cycle, extract one, and mark the vertices whose distance is minus infinity;
- say, with measurements, how Bellman-Ford, the queue variant and Dijkstra compare, and what arc order and a negative cycle cost;
- compute shortest and longest paths in a DAG in O(V + E);
- write Floyd-Warshall with path reconstruction, say why `k` must be the outer loop, and write transitive closure with bit rows;
- reweight with potentials (Johnson), prove the new weights are non-negative and that shortest paths survive, and choose between Floyd and Johnson with numbers;
- solve "at most k arcs" with k rounds of Bellman-Ford.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 1,453,908 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: Bellman-Ford, the cycle flag, the minus-infinity set and the cycle finder against a brute-force enumeration of simple paths and simple cycles on thousands of small graphs (including many with negative cycles, counted so that both outcomes occur often); the queue variant against Bellman-Ford; DAG paths, shortest and longest, against Bellman-Ford; Floyd-Warshall distances, paths (each re-summed along the arcs) and cycle flag against the same brute force; Johnson against Floyd; the closure against a DFS for sizes across 64-bit word boundaries; "at most k arcs" against brute-force walks; and a graph of 100,000 vertices and 400,000 arcs with 100,000-odd negative edges and no negative cycle (section 9). Then 45 hand-made mutants of the listings were run against the tests: **36 were caught and 9 are equivalent changes** (section 9).

**Data and timing.** Graphs are **synthetic** and seeded: random directed graphs with V = 1,000,000 and E = 4,000,000 (weights 1 to 100, or with negative edges made by a potential, see section 2); a path of 20,000 vertices; a graph of 1,000 vertices and 4,000 arcs; a complete graph on 500 vertices. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. What goes wrong, and what is true

A **negative cycle** is a directed cycle whose weights sum to less than zero. If one is reachable from the source and can reach the target, there is no shortest walk: going round once more makes it shorter, so the distance is minus infinity. A cycle of weight exactly zero is harmless. Two facts organise everything below.

> **Fact 1.** If no negative cycle is reachable, every vertex has a shortest path with no repeated vertex, so with **at most V - 1 arcs**.
> **Fact 2.** A shortest path is made of shortest paths: if `s ... u ... t` is shortest, so are `s ... u` and `u ... t`.

*Why Fact 1.* A shortest walk with a repeated vertex contains a cycle; removing the cycle gives a walk that is no longer (the cycle's weight is zero or more). Repeat until no vertex repeats.

Dijkstra's rule ("the closest unfinished vertex is final") needed non-negative weights (Topic 05). Bellman-Ford's rule needs only Fact 1: after round `i`, every vertex whose shortest path has `i` arcs or fewer holds its true distance. It is a different kind of algorithm: it does not choose which vertex to finish, it just keeps relaxing everything until nothing changes.

## 2. Bellman-Ford

```cpp
struct WEdge { int a, b; long long w; };            // a directed arc a -> b of weight w (negative allowed)
const long long INF = LLONG_MAX / 4;                 // "no path"; INF + w never overflows for |w| below 2^61
```

```cpp
// Bellman-Ford. Idea: a shortest path has at most n - 1 arcs, so after round i every vertex whose shortest path has i arcs or fewer
// is correct. Do n - 1 rounds of "relax every arc"; a round that changes nothing ends the work early. If an n-th round STILL
// changes something, a negative cycle is reachable from the source. dist is then not a set of distances; neg_inf marks the vertices
// whose distance is minus infinity (reachable from a negative cycle), the others are exact.
struct BF { std::vector<long long> dist; std::vector<int> parent; std::vector<char> neg_inf; bool neg_cycle = false; int rounds = 0; };
BF bellman_ford(int n, const std::vector<WEdge>& e, int src) {
    BF r; r.dist.assign(n, INF); r.parent.assign(n, -1); r.neg_inf.assign(n, 0); r.dist[src] = 0;
    bool changed = true; int round = 0;
    while (changed && round < n) {                    // rounds 1..n; round n changing something means a negative cycle
        changed = false; ++round;
        for (auto& x : e)
            if (r.dist[x.a] < INF && r.dist[x.a] + x.w < r.dist[x.b]) { r.dist[x.b] = r.dist[x.a] + x.w; r.parent[x.b] = x.a; changed = true; }
        if (changed && round < n) r.rounds = round;
    }
    if (changed) {                                    // the n-th round changed something
        r.neg_cycle = true;
        for (auto& x : e) if (r.dist[x.a] < INF && r.dist[x.a] + x.w < r.dist[x.b]) r.neg_inf[x.b] = 1;
        for (bool again = true; again;) {             // everything reachable from such a vertex is minus infinity too
            again = false; for (auto& x : e) if (r.neg_inf[x.a] && !r.neg_inf[x.b]) { r.neg_inf[x.b] = 1; again = true; }
        }
    }
    return r;
}
// A negative cycle anywhere in the graph (not just reachable from one source), as a list of vertices; empty if there is none.
// Start every distance at 0, which is the same as a virtual source with a 0-weight arc to every vertex.
std::vector<int> find_negative_cycle(int n, const std::vector<WEdge>& e) {
    std::vector<long long> d(n, 0); std::vector<int> par(n, -1); int last = -1;
    for (int round = 0; round < n; ++round) {
        last = -1;
        for (auto& x : e) if (d[x.a] + x.w < d[x.b]) { d[x.b] = d[x.a] + x.w; par[x.b] = x.a; last = x.b; }
        if (last < 0) return {};
    }
    for (int i = 0; i < n; ++i) last = par[last];     // n steps back along parents lands inside the cycle
    std::vector<int> cyc{last};
    for (int v = par[last]; v != last; v = par[v]) cyc.push_back(v);
    std::reverse(cyc.begin(), cyc.end()); return cyc;
}
```

The arcs are a plain list, not compressed rows, because each round touches all of them. Round `i` makes correct every vertex whose shortest path has at most `i` arcs: an induction on `i`, since a path with `i` arcs is a path with `i - 1` arcs (already correct) plus one arc, and that arc is relaxed in round `i`. Fact 1 says `V - 1` rounds are enough. The cost is **O(V * E)** in the worst case and O(E) per round. If a round changes nothing, the next ones cannot either, so we stop (`changed`).

Here it is on a ten-arc graph with negative arcs (the classic textbook example, arcs relaxed in the order listed). Each line is the distance array after a round.

```
start  0 inf inf inf inf
round 1 0 2 7 4 2
round 2 0 2 7 4 -2
round 3 0 2 7 4 -2
round 4 0 2 7 4 -2
agree=1 rounds=2 neg_cycle=0
```

Round 1 already reached the answer for four of the five vertices, because the arc list happened to be in a helpful order, and round 2 fixed the last one (vertex 4 goes from 2 to -2 through the negative arc `1->4`). `rounds` reports the last round that changed anything.

**Arc order changes the number of rounds, not the answer.** On a path `0 -> 1 -> ... -> n-1` the arcs listed from the source outward finish in one round; listed backwards, each round advances one vertex:

```
n=6 against the grain rounds=5 with the grain rounds=1
n=40 against the grain rounds=39 with the grain rounds=1
```

On a path of 20,000 vertices the backwards listing took 181 / 183 ms (19,999 rounds over 19,999 arcs) and the helpful one under a millisecond. The worst case is real, not a theoretical curiosity: an unlucky arc order costs V rounds. On random graphs the number of rounds is small: **18 rounds** for the graph of a million vertices and four million arcs in section 3.

**Negative cycles.** If round `V` still changes something, a negative cycle is reachable from the source (Fact 1: with no such cycle, round `V - 1` was the last that could change anything). The code then marks `neg_inf`: the arcs that can still be relaxed lead into the cycle, and everything reachable from there is minus infinity; the other distances are exact. Here is a small case, where `1 -> 2 -> 1` has weight -2, vertex 4 is not reachable from the cycle, and vertex 3 is:

```
neg_cycle=1
v=0 neg_inf=0
v=1 neg_inf=1
v=2 neg_inf=1
v=3 neg_inf=1
v=4 neg_inf=0
cycle: 1 2
spfa neg_cycle=1
```

`find_negative_cycle` answers a different question, "is there a negative cycle anywhere?", by starting every distance at 0 (a virtual source joined to everything) and, after `V` rounds, walking `parent` back `V` times to be certain to land inside the cycle before reading it off. The `spfa` line shows the queue variant also detecting it (section 3).

**Cost of a negative cycle.** Without early exit the algorithm needs all `V` rounds to notice: on a graph of 5,000 vertices and 25,002 arcs it took 423 / 399 ms for the one answer "there is a negative cycle", which is 5,000 rounds over 25,002 arcs.

## 3. The queue variant (SPFA)

Most arcs are relaxed to no effect after the first few rounds. The queue variant keeps only the vertices whose distance just dropped and re-examines their arcs.

```cpp
// The queue-based variant (often called SPFA): only re-examine the arcs out of a vertex whose distance just dropped. Same answers as
// Bellman-Ford when there is no negative cycle; if a vertex is taken from the queue n times, there is one. `pops` counts queue removals.
struct Spfa { std::vector<long long> dist; bool neg_cycle = false; long long pops = 0; };
Spfa spfa(int n, const std::vector<WEdge>& e, int src) {
    std::vector<std::vector<std::pair<int, long long>>> adj(n); for (auto& x : e) adj[x.a].push_back({x.b, x.w});
    Spfa r; r.dist.assign(n, INF); std::vector<char> in(n, 0); std::vector<int> times(n, 0); std::deque<int> q{src}; r.dist[src] = 0; in[src] = 1;
    while (!q.empty()) {
        int u = q.front(); q.pop_front(); in[u] = 0; ++r.pops;
        if (++times[u] > n) { r.neg_cycle = true; return r; }
        for (auto [v, w] : adj[u]) if (r.dist[u] + w < r.dist[v]) { r.dist[v] = r.dist[u] + w; if (!in[v]) { in[v] = 1; q.push_back(v); } }
    }
    return r;
}
```

If a vertex leaves the queue more than `V` times, there is a negative cycle. The worst case is still O(V * E); it is a heuristic, and graphs on which it is slow are known (from memory). Measured on the random graph of a million vertices and four million arcs, weights 1 to 100 (no negative edges), then the same size with negative edges made by a potential (each weight is `pot[b] - pot[a]` plus a number from 0 to 50, so there is no negative cycle, and 1,899,087 arcs are negative):

| Graph | Bellman-Ford (ms) | Queue variant (ms) | Dijkstra (ms) | BF rounds | Queue pops |
|---|---|---|---|---|---|
| Random, weights 1 to 100 | 597 / 646 | 1,096 / 1,057 | 657 / 725 | 18 | 2,497,847 |
| Random, with negative edges | 683 / 599 | 908 / 847 | not applicable | 19 | 2,466,940 |

On both graphs the queue variant was **slower** than plain Bellman-Ford in both runs (1.3 to 1.8 times), because it makes about 2.5 removals per vertex through a queue with a flag array, while Bellman-Ford with 18 or 19 rounds is a sequential sweep over an array. Bellman-Ford even matched Dijkstra on the non-negative graph (a random graph has a small diameter, so few rounds). Do not assume the queue variant is the faster one; on this machine it was not. Measure before preferring it.

## 4. A DAG needs none of this

If the graph has no cycles, there is an order in which every arc goes forward (Topic 03). Relax the arcs in that order: when `u` is reached, all of its predecessors are done, so `dist[u]` is final. This is **O(V + E)**, and any weights, negative included, are fine.

```cpp
// Shortest (or, with longest = true, LONGEST) paths in a directed acyclic graph, any weights, in O(V + E): relax the arcs in
// topological order (Kahn, Topic 03). Each vertex is final when it is reached in that order. ok = false if the graph has a cycle.
struct Dag { std::vector<long long> dist; bool ok = true; };
Dag dag_paths(int n, const std::vector<WEdge>& e, int src, bool longest = false) {
    std::vector<std::vector<std::pair<int, long long>>> adj(n); std::vector<int> indeg(n, 0);
    for (auto& x : e) { adj[x.a].push_back({x.b, longest ? -x.w : x.w}); ++indeg[x.b]; }
    Dag r; r.dist.assign(n, INF); r.dist[src] = 0; std::vector<int> q; for (int v = 0; v < n; ++v) if (!indeg[v]) q.push_back(v);
    for (size_t h = 0; h < q.size(); ++h) {
        int u = q[h];
        for (auto [v, w] : adj[u]) { if (r.dist[u] < INF && r.dist[u] + w < r.dist[v]) r.dist[v] = r.dist[u] + w; if (--indeg[v] == 0) q.push_back(v); }
    }
    if ((int)q.size() != n) r.ok = false;
    if (longest) for (auto& d : r.dist) if (d < INF) d = -d;
    return r;
}
```

Negating the weights turns "shortest" into "longest", which is why the same function does both. Example (arcs `0->1:3, 0->2:2, 1->3:4, 2->3:1, 2->4:6, 3->4:-2`):

```
shortest 0 3 2 3 1
longest  0 3 2 7 8
cycle ok=0
```

The longest path to vertex 4 is 8 (`0, 2, 4`); the shortest is 1 (`0, 2, 3, 4`). In a general graph the longest simple path is believed to need exponential time in the worst case (it is NP-complete as a decision problem; from memory), and the absence of cycles is exactly what removes the difficulty. Scheduling and "critical path" problems are longest paths in a DAG.

## 5. Floyd-Warshall: every pair

```cpp
// Floyd-Warshall, all pairs. d[i][j] after the loop on k is the shortest i -> j path that uses only vertices 0..k as middles.
// Three nested loops, O(V^3), and k MUST be the outer loop. nxt[i][j] is the first step on a shortest i -> j path (-1 if none).
// A negative cycle shows up as d[i][i] < 0. Values on such a cycle can keep halving downward, so they are clamped at -INF.
struct Apsp { std::vector<std::vector<long long>> d; std::vector<std::vector<int>> nxt; bool neg_cycle = false; };
Apsp floyd(int n, const std::vector<WEdge>& e) {
    Apsp r; r.d.assign(n, std::vector<long long>(n, INF)); r.nxt.assign(n, std::vector<int>(n, -1));
    for (int i = 0; i < n; ++i) { r.d[i][i] = 0; r.nxt[i][i] = i; }
    for (auto& x : e) if (x.w < r.d[x.a][x.b]) { r.d[x.a][x.b] = x.w; r.nxt[x.a][x.b] = x.b; }
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i) {
            if (r.d[i][k] >= INF) continue;
            for (int j = 0; j < n; ++j) {
                if (r.d[k][j] >= INF) continue;
                long long nd = std::max(-INF, r.d[i][k] + r.d[k][j]);
                if (nd < r.d[i][j]) { r.d[i][j] = nd; r.nxt[i][j] = r.nxt[i][k]; }
            }
        }
    for (int i = 0; i < n; ++i) if (r.d[i][i] < 0) r.neg_cycle = true;
    return r;
}
// The path i -> j as vertices (empty if none). Only meaningful when there is no negative cycle.
std::vector<int> floyd_path(const Apsp& r, int i, int j) {
    std::vector<int> p; if (r.nxt[i][j] < 0) return p;
    for (int v = i; v != j; v = r.nxt[v][j]) p.push_back(v);
    p.push_back(j); return p;
}
// Transitive closure (Warshall): reach[i] is a bit row; the same triple loop with OR instead of min, 64 columns per word.
std::vector<std::vector<uint64_t>> closure(int n, const std::vector<std::pair<int, int>>& arcs) {
    int W = (n + 63) / 64; std::vector<std::vector<uint64_t>> reach(n, std::vector<uint64_t>(W, 0));
    for (int i = 0; i < n; ++i) reach[i][i / 64] |= 1ULL << (i % 64);
    for (auto [a, b] : arcs) reach[a][b / 64] |= 1ULL << (b % 64);
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i)
            if (reach[i][k / 64] >> (k % 64) & 1) for (int w = 0; w < W; ++w) reach[i][w] |= reach[k][w];
    return reach;
}
```

After the loop on `k`, `d[i][j]` is the shortest `i -> j` path whose **middle vertices** all lie in `0..k`. Going from `k - 1` to `k` there are two cases: the best path avoids `k` (keep `d[i][j]`) or passes through `k` once, in which case it is a shortest `i -> k` path plus a shortest `k -> j` path, both with middles in `0..k-1`. Three loops, **O(V^3)** time, **O(V^2)** memory.

**`k` must be the outer loop.** If it is the inner or middle one, `d[i][k]` and `d[k][j]` have not yet been finalised when they are used. Take arcs `0->2:1, 2->1:1, 1->3:1`. With the loops in the order `i, k, j`, row 0 meets `k = 1` before `d[0][1]` is known and ends with `d[0][3] = inf`; the correct value is 3 (problem E in section 12 shows the run). This is the most common bug in written solutions.

The `nxt` matrix stores the first step of each shortest path, so a path is rebuilt by following it: `floyd_path`. On the four-vertex example (arcs `0->1:3, 1->2:-2, 2->3:2, 0->3:4, 3->1:1`):

```
row 0 0 3 1 3
row 1 inf 0 -2 0
row 2 inf 3 0 2
row 3 inf 1 -1 0
path 0->3: 0 1 2 3
neg_cycle=0
two-cycle: neg_cycle=1 d00=-1 d11=-2
reach 0:11100
reach 1:01100
reach 2:00100
reach 3:00011
reach 4:00001
```

The path `0, 1, 2, 3` has length 3 and beats the direct arc (4) because of the negative arc `1->2`. The two-cycle `0->1:1, 1->0:-2` has weight -1; the diagonal shows it (`d[0][0] = -1`, `d[1][1] = -2`), which is the Floyd test for a negative cycle: some `d[i][i] < 0`. On a negative cycle the values can keep getting more negative (they can double with every `k`), so the code clamps at `-INF`. A complete graph of 40 vertices with every weight -1 overflows 64 bits without the clamp; a test uses it and checks every entry stays in range.

**Transitive closure** (`closure`) is the same triple loop with "or" in place of "min": `reach[i][j]` becomes true if `reach[i][k]` and `reach[k][j]`. Storing each row as bits handles 64 columns per word operation, so it is about V^3 / 64. On a graph of 4,000 vertices and 8,000 arcs it found 10,061,024 reachable pairs (the diagonal included) in 102 / 96 ms.

## 6. Johnson: Dijkstra for every source

Running Dijkstra from every vertex costs `V * E log V`, much less than V^3 when the graph is sparse, but needs non-negative weights. Johnson repairs the weights.

```cpp
// Johnson's algorithm: all pairs with negative edges but no negative cycle, in O(V * E log V). Idea: find potentials h[v] with
// Bellman-Ford from a virtual source (a 0-weight arc to every vertex), then give each arc the new weight w + h[u] - h[v]. Every
// new weight is >= 0 (that is exactly the triangle inequality h[v] <= h[u] + w), and every path from s to t changes by the SAME
// amount h[s] - h[t], so shortest paths stay shortest. Run Dijkstra from every vertex on the new weights and undo the shift.
struct Johnson { std::vector<std::vector<long long>> d; bool ok = true; };
Johnson johnson(int n, const std::vector<WEdge>& e) {
    Johnson r; std::vector<WEdge> ext = e; for (int v = 0; v < n; ++v) ext.push_back({n, v, 0});
    BF bf = bellman_ford(n + 1, ext, n); if (bf.neg_cycle) { r.ok = false; return r; }
    const std::vector<long long>& h = bf.dist;
    std::vector<std::vector<std::pair<int, long long>>> adj(n); for (auto& x : e) adj[x.a].push_back({x.b, x.w + h[x.a] - h[x.b]});
    r.d.assign(n, std::vector<long long>(n, INF));
    for (int s = 0; s < n; ++s) {
        std::vector<long long>& dist = r.d[s]; std::priority_queue<std::pair<long long, int>, std::vector<std::pair<long long, int>>, std::greater<>> pq;
        dist[s] = 0; pq.push({0, s});
        while (!pq.empty()) {
            auto [du, u] = pq.top(); pq.pop(); if (du > dist[u]) continue;
            for (auto [v, w] : adj[u]) if (du + w < dist[v]) { dist[v] = du + w; pq.push({dist[v], v}); }
        }
        for (int t = 0; t < n; ++t) if (dist[t] < INF) dist[t] += h[t] - h[s];
    }
    return r;
}
```

**Potentials.** Add a virtual vertex joined to every vertex by an arc of weight 0 and run Bellman-Ford from it; call the distances `h`. The triangle inequality for every arc says `h[v] <= h[u] + w`, i.e. **`w + h[u] - h[v] >= 0`**: the new weights are non-negative. And any path from `s` to `t` changes by the same amount, `h[s] - h[t]` (the middle terms cancel), so the shortest path is the same path; after Dijkstra, add `h[t] - h[s]` back. If the virtual source finds a negative cycle, the answer is that there is none to give (`ok = false`). The trace shows the numbers on the example above:

```
h 0 0 -2 0
arc 0->1: 3 becomes 3
arc 1->2: -2 becomes 0
arc 2->3: 2 becomes 0
arc 0->3: 4 becomes 4
arc 3->1: 1 becomes 1
johnson equals floyd=1
row 0 0 3 1 3
```

The negative arc `1->2` became 0, the others kept or grew, and the result equals Floyd's. Measured:

| Graph | Floyd-Warshall (ms) | Johnson (ms) |
|---|---|---|
| Sparse: V = 1,000, 4,000 arcs, negative edges | 509 / 600 | 145 / 161 |
| Complete: V = 500, 249,500 arcs, weights 1 to 100 | 97 / 93 | 241 / 223 |

On the sparse graph Johnson was 3.5 to 3.7 times faster. On the complete graph it was **2.4 to 2.5 times slower** (V Dijkstras of V^2 arcs each cannot beat three tight loops). So: sparse and large, Johnson; dense or small, Floyd. Both need V^2 memory for the answer: 10^4 vertices already means 800 MB of 64-bit entries.

## 7. At most k arcs

"Cheapest from `src` to `dst` with at most `k` arcs." Bellman-Ford's round `i` is exactly "the best using at most `i` arcs", so stop after `k` rounds. The one trap: each round must read the previous round's array, not the one being updated, or a single round could chain several arcs.

```cpp
// At most k arcs (so k - 1 stops for k flights): Bellman-Ford's rounds ARE the arc count, so stop after k rounds. Each round reads the
// previous round's array (a copy), otherwise one round could use several arcs. Compare with Topic 05, where the same question
// needed a state (city, flights) inside Dijkstra.
std::vector<long long> bellman_k(int n, const std::vector<WEdge>& e, int src, int k) {
    std::vector<long long> d(n, INF); d[src] = 0;
    for (int round = 0; round < k; ++round) {
        std::vector<long long> nx = d;
        for (auto& x : e) if (d[x.a] < INF && d[x.a] + x.w < nx[x.b]) nx[x.b] = d[x.a] + x.w;
        d = nx;
    }
    return d;
}
```

```
at most 1 arcs 0 100 inf inf
at most 2 arcs 0 100 200 700
at most 3 arcs 0 100 200 400
```

For vertex 3 the answer is 400 with three arcs (`0->1->2->3` costs 100 + 100 + 200), 700 with two (`0->1->3`) and none with one. This is the Topic 05 flight example: there, "at most `k` stops" was a state (city, flights used) inside Dijkstra, `n * (k + 2)` states; here it is `k` rounds of `O(E)`, i.e. `O(k * E)`, and negative prices would be allowed.

## 8. Choosing

| Situation | Use | Cost |
|---|---|---|
| Non-negative weights, one source | Dijkstra (Topic 05) | O((V + E) log V) |
| Negative edges, one source | Bellman-Ford | O(V * E), O(E) per round |
| Might contain a negative cycle | Bellman-Ford, check round V | O(V * E) always, to be sure |
| Any cycle anywhere? | `find_negative_cycle` | O(V * E) |
| DAG, any weights, shortest or longest | topological order | O(V + E) |
| At most k arcs | k rounds of Bellman-Ford | O(k * E) |
| All pairs, dense or small V | Floyd-Warshall | O(V^3), O(V^2) memory |
| All pairs, sparse, no negative cycle | Johnson | O(V * E log V) |
| All pairs, no negative edges | Dijkstra from each vertex | O(V * E log V) |
| Reachability for all pairs | bit-row closure | about V^3 / 64 |

## 9. What the tests check, and what they cannot

Of the 45 mutants, 36 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). One of the 36 survived the first run and is caught only because of a test I then added: removing the `-INF` clamp in Floyd-Warshall, which needs a complete graph of 40 vertices with every weight -1 to overflow (a cycle with chords of 100 vertices did not; the magnitudes there grow only polynomially). Nine survive, and each is an equivalent change as far as the answers go: the mutated program returns the same answers as the original on every input. The reasons:

- **`round <= n` instead of `round < n` in Bellman-Ford's loop.** One extra round only happens if round `n` changed something, and then the negative cycle is already known.
- **Marking the tail instead of the head of a still-relaxable arc.** After round `n`, an arc that can still be relaxed has a tail whose distance changed in that very round, so the tail is reachable from the cycle too, and the propagation marks the rest.
- **Starting `find_negative_cycle` at infinity instead of 0.** Every vertex starts equally far, which is the same as a virtual source with equal arcs; whether a cycle exists does not depend on the common offset.
- **Walking `n - 1` parent steps instead of `n`.** The part of the chain before the cycle has at most `n - 1` vertices, so `n - 1` steps already land on the cycle.
- **`<= 0` instead of `== 0` on the in-degree in the DAG code.** An in-degree reaches 0 exactly once, and never goes below it.
- **`<=` instead of `<` when loading parallel arcs in Floyd.** Equal weights to the same head give the same `nxt`.
- **Skipping self loops in `closure`.** The diagonal is already set, so a self loop adds nothing.
- **Weight 1 instead of 0 on the virtual source's arcs in Johnson.** The potentials change, but they are still a valid set of potentials (the triangle inequality still holds), so the reweighted graph and the undone distances are the same.
- **Removing the stale test in Johnson's Dijkstra.** Stale entries are expanded and improve nothing (Topic 05).

The graph of 100,000 vertices has no independent reference, so it is checked by a certificate: Bellman-Ford and the queue variant agree, and for every arc `dist[v] <= dist[u] + w` (so no distance is too large); distances realised by actual paths are the job of the small tests, which re-sum every parent chain and every Floyd path along real arcs. The graphs with negative edges but no negative cycle are built with a potential (each weight is `pot[b] - pot[a]` plus a non-negative number), which guarantees no negative cycle by construction. What the tests cannot do is cover weights of magnitude near `INF`, multi-gigabyte matrices, or adversarial inputs for the queue variant.

Mutants are chosen where a bug is plausible, not at random, so "36 of 45" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 10. Common mistakes

1. **Running Dijkstra on negative edges** (Topic 05). Use Bellman-Ford, or Johnson to reweight.
2. **`INF + w` with a negative `w`.** If `dist[u]` is the infinity value and `w` is negative, `INF + w < INF` and an unreachable vertex looks reachable. Guard every relaxation with `dist[u] < INF`, as the listings do.
3. **Treating an undirected edge with a negative weight as one arc.** It is two arcs, and `a -> b -> a` is a negative cycle. Negative edges only make sense in a directed graph.
4. **Stopping at V - 1 rounds** and never checking round V, so a negative cycle goes unnoticed.
5. **Trusting `dist` after a negative cycle was found.** Distances of vertices reachable from the cycle are meaningless; `neg_inf` says which.
6. **Updating in place in the k-arcs version.** Without the copy, one round can use several arcs.
7. **Floyd with the wrong loop order**, `k` not outermost.
8. **Floyd on a graph with a negative cycle** without a clamp: values can grow in magnitude until they overflow.
9. **Forgetting the diagonal.** `d[i][i]` must start at 0 (and at the weight of a negative self loop, if any); a transitive closure usually wants the diagonal true.
10. **Johnson's undo step.** Distances must be corrected by `h[t] - h[s]` after Dijkstra; forgetting leaves answers that are shifted by potentials.

## 11. Questions to answer out loud

1. Why does a shortest path have at most V - 1 arcs when there is no negative cycle?
2. Why do V - 1 rounds of Bellman-Ford suffice? What does the V-th round tell you?
3. Which vertices get minus infinity when a negative cycle exists?
4. Why can the number of rounds depend on the arc order, and what is the worst case?
5. What does a negative cycle cost in time, and can you stop earlier?
6. Why can the longest path be found quickly in a DAG but not, as far as anyone knows, in a general graph?
7. State the invariant of Floyd-Warshall after the loop on `k`. Why must `k` be outermost?
8. How do you detect a negative cycle from the Floyd matrix?
9. Why are the reweighted arcs in Johnson non-negative? Why do shortest paths survive?
10. When do you prefer Floyd to Johnson? Give V and E.
11. How would you answer "at most k arcs" with Dijkstra? with Bellman-Ford? What does each cost?
12. How do you find an arbitrage cycle in currency rates?

## 12. Problems

### 12.1 On paper

**A.** Run Bellman-Ford on the arcs `2->3:1, 1->2:1, 0->1:1, 0->3:10` (in this order) from vertex 0. Give the array after every pass, the number of passes, and the last pass that changed anything.

**B.** For arcs `0->1:2, 1->2:-4, 2->1:1, 2->3:0, 0->3:5, 4->3:-1` and source 0: is there a negative cycle? Which vertices are minus infinity, and what is `dist[0]`? What does `find_negative_cycle` return?

**C.** Prove that with no negative cycle a shortest path has at most V - 1 arcs.

**D.** For arcs `0->1:2, 1->2:-3, 0->2:1`, compute the potentials `h` from a virtual source, the reweighted arcs, and the distance from 0 to 2 after undoing the shift.

**E.** Run Floyd-Warshall with the loops in the order `i, k, j` on `0->2:1, 2->1:1, 1->3:1`. What is `d[0][3]`? What should it be?

**F.** Prove that the reweighted arcs of Johnson are non-negative, and that two paths from `s` to `t` change by the same amount.

**G.** A graph has 10^4 vertices and 5 * 10^4 arcs. Estimate the work of Floyd and of Johnson as counts of basic steps, and the memory of the answer.

### 12.2 On LeetCode

For each: which tool of this topic, and what is the graph? Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Find the City With the Smallest Number of Neighbors at a Threshold Distance | 1334 | Floyd-Warshall, then count per row; ties go to the largest index |
| Evaluate Division | 399 | Floyd on ratios (products), or a weighted union-find |
| Cheapest Flights Within K Stops | 787 | k + 1 rounds of Bellman-Ford with a copy (section 7); compare with Topic 05 |
| Minimum Cost to Convert String I | 2976 | Floyd on 26 letters, then sum per position |

### 12.3 In your editor

**A.** Detect a negative cycle in a directed graph and print it, without looking at the listing; test on random graphs against a brute force.

**B.** Currency arbitrage: rates `r(a, b)` for every pair; find a cycle whose product of rates exceeds 1. (Hint: weights `-log r`; what does a negative cycle mean?)

**C.** Add `Apsp` path reconstruction for Johnson (it computes distances only), then test against `floyd_path`.

**D.** Make Bellman-Ford visit the arcs in a random order and measure the number of rounds on the path graph. What is the expected number?

**E.** Measure Bellman-Ford, the queue variant and Dijkstra on your machine for a graph with 10^5 vertices. Do your rankings match section 3?

### Answers

**A.** Passes: `0 1 inf 10`, then `0 1 2 10`, then `0 1 2 3`, then `0 1 2 3` (no change). Four passes, last change in pass 3. The arc list runs against the grain, so each pass advances one vertex.

**B.** Yes: `1 -> 2 -> 1` has weight -4 + 1 = -3. Minus infinity: vertices 1, 2 and 3 (vertex 3 is reachable from the cycle, so its direct arc 5 does not help). `dist[0] = 0`, and vertex 4 is unreachable from 0, so it keeps the infinity value. `find_negative_cycle` returns the cycle `1 2`.

**C.** Suppose a shortest walk repeats a vertex. The part between the two visits is a cycle of weight zero or more (no negative cycle); remove it and the walk is no longer. Repeat until no vertex repeats. A walk with no repeated vertex has at most V vertices, so at most V - 1 arcs.

**D.** `h = (0, 0, -3)`. New weights: `0->1: 2 + 0 - 0 = 2`, `1->2: -3 + 0 + 3 = 0`, `0->2: 1 + 0 + 3 = 4`. The shortest reweighted path from 0 to 2 is `0, 1, 2` with length 2; undo the shift by adding `h[2] - h[0] = -3`, giving -1, which equals the original `2 - 3`.

**E.** `d[0][3] = inf` (row 0 meets `k = 1` before `d[0][1]` is known). The correct value is 3 (`0, 2, 1, 3`).

**F.** From Bellman-Ford, `h[v] <= h[u] + w` for every arc, so `w + h[u] - h[v] >= 0`. For a path `s = v0, v1, ..., vm = t` the new length is the sum of `w_i + h[v_i] - h[v_{i+1}]`; the `h` terms telescope to `h[s] - h[t]`, the same for every path with those endpoints, so the order of paths is preserved.

**G.** Floyd: (10^4)^3 = 10^12 steps. Johnson: about V * E * log2 V = 10^4 * 5 * 10^4 * 13.3 = 6.6 * 10^9 steps, about 150 times fewer, plus one Bellman-Ford. Memory of the answer: 10^8 entries of 8 bytes = 800 MB for either.

**12.2 hints.** 1334: the sample (4 cities, threshold 4) gives city 3 with 2 neighbours. 399: take the product of ratios along the path; a variable that is not in the graph gives -1.0. 787: for the sample, 700 with one stop (two arcs), as in section 7. 2976: the cost to change a letter is the shortest path in the letter graph; unreachable pairs make the answer -1.

**12.3 hints.** B: a product above 1 is a sum of `-log r` below 0, so arbitrage is a negative cycle; compare the cost of trusting floating point with a small epsilon. E: expect the queue variant to be slowest on random graphs.

---

## Cheat sheet

- **Fact:** with no negative cycle a shortest path has at most V - 1 arcs; round `i` of Bellman-Ford fixes all paths of `i` arcs.
- **Bellman-Ford:** relax all arcs up to V - 1 times, stop early if nothing changed; a change in round V means a negative cycle. O(V * E).
- **Rounds depend on arc order** (19,999 rounds on a backwards path, 18 on a random graph of a million vertices); a negative cycle always costs V rounds.
- **Queue variant:** 1.3 to 1.8 times slower than plain Bellman-Ford here; no better worst case.
- **DAG:** relax in topological order, any weights, O(V + E); negate the weights for the longest path.
- **Floyd-Warshall:** `k` outermost; `d[i][i] < 0` means a negative cycle; clamp so values cannot overflow; O(V^3), O(V^2) memory.
- **Closure:** the same loops with OR on bit rows.
- **Johnson:** potentials from a virtual source, `w + h[u] - h[v] >= 0`, Dijkstra from each vertex, add `h[t] - h[s]` back. 3.5 to 3.7 times faster than Floyd on a sparse graph, 2.4 to 2.5 times slower on a dense one.
- **At most k arcs:** k rounds of Bellman-Ford on a copy.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Bellman-Ford, Floyd-Warshall, Warshall's closure and Johnson's algorithm are the standard algorithms (named from memory; their original papers are 1950s to 1970s). The statements that the queue variant has bad worst cases and that the longest simple path is NP-complete as a decision problem are from memory of the literature and not measured here.
- Problem numbers and titles in section 12 are LeetCode's, from memory; check a number before you rely on it.
