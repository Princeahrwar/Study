# DSA 08.09: Network Flow and Bipartite Matching

*Part of the [Graphs plan](00_plan.md) · Stretch topic · Time: about 8 hours (4 reading, 4 problems) · You need: [BFS on states and grids](04_bfs_on_states_and_grids.md) (levels and shortest paths in unweighted graphs) and [Connectivity and cycles](02_connectivity_and_cycles.md). There is no companion demo file: every structure is listed in this document.*

Pipes have capacities and you want to know how much water can flow from a source to a sink. Applicants have jobs they could do and you want to give as many of them a job as possible. A network has links that can fail and you want to know how many failures it takes to cut two sites apart. These are one problem. A **flow network** is a directed graph whose arcs have capacities; a **flow** sends stuff along arcs without exceeding any capacity and without creating or losing any at the vertices in between; the **maximum flow** is the most that can go from source `s` to sink `t`. Its twin is the **minimum cut**, the cheapest set of arcs whose removal separates `s` from `t`, and the central theorem says the two numbers are equal. Matching is the special case in which every capacity is 1.

The surprise is how little machinery is needed. One data structure (every arc paired with a reverse twin), one idea (keep pushing flow along a path of unused capacity, and let later pushes take back earlier ones), and three ways to pick the paths, which differ enormously in speed.

By the end you can:

- state the flow constraints, build the **residual graph**, and say why the reverse arcs are what makes a greedy method correct;
- write Ford-Fulkerson, Edmonds-Karp and Dinic, and say what bounds the running time of each (the flow value, V*E^2, V^2*E);
- state and use the **max-flow min-cut theorem**, and read a minimum cut off a finished flow;
- model vertex capacities, several sources or sinks, and undirected edges as plain flow problems;
- reduce **bipartite matching** to flow, say why Dinic on a unit network is fast, and produce a **Hall's theorem** witness when no perfect matching exists;
- say what changes when the assignment has weights (a different tool: not covered by these listings).

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 132,314 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the three flow algorithms against the brute-force minimum cut (try every set of vertices containing `s` and not `t`) on thousands of tiny random networks with self loops, parallel and antiparallel arcs and zero capacities; every flow checked to respect capacities and conservation; the cut read off each result checked to have capacity equal to the flow; the scripted worst case for Ford-Fulkerson (below); matching against brute force over all partial matchings, Hall's theorem against all subsets of the left side, the independent-set formula against all subsets of vertices; and larger inputs (a graph with a planted perfect matching of 300,000 pairs, grids, random networks) where the three algorithms must agree. Then 36 hand-made mutants of the listings were run against the tests: **29 were caught and 7 are equivalent changes** (section 8).

**Data and timing.** Graphs are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs. Every search in the listings is iterative.

---

## 1. Flow, residual graph, and the pairing trick

A **flow network**: directed arcs `u -> v`, each with a capacity `c(u, v) >= 0`, a source `s` and a sink `t`. A **flow** `f` assigns each arc a number with

- **capacity:** `0 <= f(u, v) <= c(u, v)`;
- **conservation:** at every vertex other than `s` and `t`, the flow in equals the flow out.

Its **value** is the net flow leaving `s`. The goal: a flow of maximum value.

Greedy fails if you only ever add flow. Take `s -> a`, `s -> b`, `a -> b`, `a -> t`, `b -> t`, all of capacity 1. Pushing one unit along `s -> a -> b -> t` uses up `s -> a` and `b -> t`, and nothing else can pass: the flow is 1, but the maximum is 2 (`s -> a -> t` and `s -> b -> t`). The fix is to allow a later path to **undo** part of an earlier one. The path `s -> b -> a -> t` goes backwards along `a -> b`, cancelling the earlier unit and re-routing it.

The **residual graph** records this. For every arc `u -> v` with capacity `c` and flow `f` it holds a forward arc with remaining capacity `c - f` and a backward arc `v -> u` with capacity `f` (the amount that could be cancelled). A path from `s` to `t` in the residual graph that has positive capacity everywhere is an **augmenting path**; pushing its bottleneck (the smallest residual capacity on it) increases the flow. The central fact, proved in section 4, is: **a flow is maximum if and only if no augmenting path exists.**

The code stores each arc together with its twin in adjacent array slots (ids `e` and `e ^ 1`) and keeps only the residual capacities; pushing `f` along `e` is `cap[e] -= f; cap[e ^ 1] += f`, which both uses forward capacity and creates backward capacity. The flow on an original arc is the residual capacity of its twin.

```cpp
// A flow network as linked arc lists. add(u, v, c) creates TWO arcs: id e from u to v with capacity c, and id e ^ 1 from v to u
// with capacity 0 (the residual twin). cap[] always holds the RESIDUAL capacity; the flow on arc e is therefore cap[e ^ 1] for an
// arc created with add(). Pushing f along e does cap[e] -= f, cap[e ^ 1] += f; that single rule also cancels flow later.
typedef long long ll;
const ll INF = LLONG_MAX / 4;
struct Net {
    int n; std::vector<int> head, nxt, to; std::vector<ll> cap;
    explicit Net(int n_) : n(n_), head(n_, -1) {}
    int add(int u, int v, ll c) {
        to.push_back(v); cap.push_back(c); nxt.push_back(head[u]); head[u] = (int)to.size() - 1;
        to.push_back(u); cap.push_back(0); nxt.push_back(head[v]); head[v] = (int)to.size() - 1;
        return (int)to.size() - 2;
    }
};
```

## 2. Ford-Fulkerson and Edmonds-Karp

Ford-Fulkerson is the loop: find any augmenting path, push its bottleneck, repeat until there is none. With integer capacities every push is at least 1, and the flow never exceeds the capacity out of `s`, so it stops. How long it takes depends only on how the path is chosen.

- **Ford-Fulkerson with a depth-first search.** Each round costs O(E) and there can be up to `|f|` rounds, so the bound is O(E * |f|): it depends on the **capacities**, not only on the size of the graph. The standard bad case is two routes of capacity `C` joined by a cross arc of capacity 1: if the paths are chosen to alternate between going through the cross arc forwards and backwards, each push is 1 and it takes `2C` rounds.
- **Edmonds-Karp** chooses a **shortest** path (fewest arcs) with a breadth-first search. Then the shortest distance from `s` to any vertex never decreases, and each push saturates an arc that is a bottleneck; an arc can be that bottleneck at most V/2 times before the distance to its tail grows. That gives at most V*E/2 augmentations, O(V*E^2) in total, **whatever the capacities are**.

```cpp
// Ford-Fulkerson: while the residual graph has an s-t path, push the bottleneck along it. The path here comes from a depth-first
// search. Returns the max-flow value; *augs counts the augmenting paths. Correct for integer capacities, but the number of
// rounds is bounded only by the flow value, so the running time is O(E * |f|) in the worst case.
ll ford_fulkerson(Net& g, int s, int t, long long* augs = nullptr) {
    ll total = 0; std::vector<int> pe(g.n), it(g.n), st;
    for (;;) {
        std::fill(pe.begin(), pe.end(), -2); it = g.head; pe[s] = -1; st.assign(1, s);
        while (!st.empty() && pe[t] == -2) {
            int v = st.back(); int& e = it[v];
            while (e != -1 && (g.cap[e] <= 0 || pe[g.to[e]] != -2)) e = g.nxt[e];
            if (e == -1) st.pop_back(); else { pe[g.to[e]] = e; st.push_back(g.to[e]); }
        }
        if (pe[t] == -2) return total;
        ll f = INF; for (int v = t; v != s; v = g.to[pe[v] ^ 1]) f = std::min(f, g.cap[pe[v]]);
        for (int v = t; v != s; v = g.to[pe[v] ^ 1]) { g.cap[pe[v]] -= f; g.cap[pe[v] ^ 1] += f; }
        total += f; if (augs) ++*augs;
    }
}
// Edmonds-Karp: the same loop, but the path is a SHORTEST one (breadth-first search). Each augmentation saturates an arc, and an
// arc can be the bottleneck at most V/2 times before the shortest distance grows, so there are at most V*E/2 rounds: O(V E^2) overall,
// whatever the capacities are.
ll edmonds_karp(Net& g, int s, int t, long long* augs = nullptr) {
    ll total = 0; std::vector<int> pe(g.n), q;
    for (;;) {
        std::fill(pe.begin(), pe.end(), -2); pe[s] = -1; q.assign(1, s);
        for (size_t h = 0; h < q.size() && pe[t] == -2; ++h)
            for (int e = g.head[q[h]]; e != -1; e = g.nxt[e]) if (g.cap[e] > 0 && pe[g.to[e]] == -2) { pe[g.to[e]] = e; q.push_back(g.to[e]); }
        if (pe[t] == -2) return total;
        ll f = INF; for (int v = t; v != s; v = g.to[pe[v] ^ 1]) f = std::min(f, g.cap[pe[v]]);
        for (int v = t; v != s; v = g.to[pe[v] ^ 1]) { g.cap[pe[v]] -= f; g.cap[pe[v] ^ 1] += f; }
        total += f; if (augs) ++*augs;
    }
}
```

**The bad case, scripted.** The depth-first search above does not happen to fall into the trap on the two-route network (at `C` = 1, 1,000 and 1,000,000 it used 2 augmentations, as did Edmonds-Karp, and Dinic needed 1 phase). The worst case depends on the choice of path, so the test applies it directly: for `C` = 400 it pushes one unit along `s -> a -> b -> t`, then one along `s -> b -> a -> t`, and so on, 800 times. Every one of the 800 paths is valid in the residual graph at its turn (checked), and the final flow is 800, the maximum. So 2C rounds are possible for a rule that picks badly, and 2 suffice for one that picks shortest paths. That is the whole argument for breadth-first search.

Where the choice matters in practice, measured on a random network (5,000 vertices, 25,000 arcs, capacities 1 to 1000, plus a source joined to 20 random vertices and a sink fed from 20 others):

| Network | Max flow | FF augmentations | EK augmentations | Dinic phases | FF ms | EK ms | Dinic ms |
|---|---|---|---|---|---|---|---|
| Random, 5,000 vertices, 25,000 arcs | 43,433 | 42,938 | 340 | 8 | 6,842 / 7,158 | 188 / 157 | 10 / 9 |
| Grid 300 by 300, left column to right column | 9,342 | 9,341 | 9,172 | 581 | 25,678 / 25,986 | 33,947 / 33,238 | 4,427 / 4,049 |

On the random network Ford-Fulkerson with a depth-first search needed 42,938 augmentations against 340 for Edmonds-Karp: depth-first paths wander, so their bottlenecks are small. It took 36 to 46 times as long. On the grid the two were of the same order (Edmonds-Karp was slower, 1.28 to 1.32 times as long as Ford-Fulkerson): both need one augmentation for nearly every unit of flow, so they pay a full search for each. The depth-first choice is not safe, and the breadth-first choice is not fast; section 3 fixes that.

## 3. Dinic

Edmonds-Karp repeats a whole breadth-first search for every single path. Dinic does one search and then uses it for many paths:

1. A breadth-first search from `s` labels every vertex with its **level**, its distance in the residual graph. If `t` is not reached, the flow is maximum.
2. Find paths that use only arcs going from level `d` to level `d + 1`, by depth-first search, until none is left. The set of arcs used is a **blocking flow**.
3. Repeat from step 1.

After a blocking flow the distance from `s` to `t` has strictly grown (every shortest path has lost an arc), so there are at most V phases. Inside a phase, the **current-arc pointer** `it[v]` remembers how far along `v`'s arc list the search has got: an arc that failed once in this phase can never succeed later in it, so each arc is skipped at most once, and a phase costs O(V*E). Total O(V^2*E) in the worst case, and much faster in practice.

```cpp
// Dinic: in each PHASE, a breadth-first search labels every vertex with its distance level[] from s in the residual graph; then
// a depth-first search finds augmenting paths that only use arcs from level d to level d + 1 (a BLOCKING flow: afterwards no such path is
// left). it[v] is the current-arc pointer: an arc that failed once in this phase is never tried again, so a phase costs O(V E) at
// worst. The distance to t strictly grows each phase, so there are at most V phases: O(V^2 E). On unit-capacity networks it is far
// better (section 6). Returns the max-flow value; *phases counts the BFS rounds that reached t.
ll dinic(Net& g, int s, int t, int* phases = nullptr) {
    ll total = 0; int n = g.n; std::vector<int> level(n), it(n), q, path;
    for (;;) {
        std::fill(level.begin(), level.end(), -1); level[s] = 0; q.assign(1, s);
        for (size_t h = 0; h < q.size(); ++h)
            for (int e = g.head[q[h]]; e != -1; e = g.nxt[e]) if (g.cap[e] > 0 && level[g.to[e]] < 0) { level[g.to[e]] = level[q[h]] + 1; q.push_back(g.to[e]); }
        if (level[t] < 0) return total;
        if (phases) ++*phases;
        it = g.head; path.clear(); int v = s;
        for (;;) {
            if (v == t) {                                                     // push the bottleneck, then retreat to the first saturated arc
                ll f = INF; for (int e : path) f = std::min(f, g.cap[e]);
                for (int e : path) { g.cap[e] -= f; g.cap[e ^ 1] += f; }
                total += f; size_t k = 0; while (g.cap[path[k]] > 0) ++k;
                v = g.to[path[k] ^ 1]; path.resize(k); continue;
            }
            int& e = it[v]; while (e != -1 && !(g.cap[e] > 0 && level[g.to[e]] == level[v] + 1)) e = g.nxt[e];
            if (e != -1) { path.push_back(e); v = g.to[e]; }
            else { if (v == s) break; level[v] = -1; int pe = path.back(); path.pop_back(); v = g.to[pe ^ 1]; }   // dead end: cut it off
        }
    }
}
```

The search is iterative: `path` holds the arcs from `s` to the current vertex. At the sink the code pushes the bottleneck and then **retreats only to the first saturated arc**, so the part of the path before it is reused. At a dead end the vertex is cut off for the rest of the phase by setting its level to -1.

Trace (checked by running the code): the network `s -> a: 3, s -> b: 2, a -> b: 1, a -> t: 2, b -> t: 3`. Phase 1 sends 2 along `s -> a -> t` and 2 along `s -> b -> t`; phase 2 finds `s -> a -> b -> t` for the last unit.

```
max flow=5 phases=2
arc s->a cap=3 flow=3
arc s->b cap=2 flow=2
arc a->b cap=1 flow=1
arc a->t cap=2 flow=2
arc b->t cap=3 flow=3
source side: s
cut arcs: s->a s->b
cut capacity=5
edmonds-karp=5 augmentations=3
ford-fulkerson=5 augmentations=3
```

The last two lines are the other two algorithms on the same network (they reach 5 with 3 augmentations each). The textbook network (the one in Cormen, Leiserson, Rivest and Stein's book, from memory) has maximum flow 23, which all three algorithms return; Dinic needs 2 phases:

```
textbook network: max flow=23 phases=2
```

On the large inputs of the table above, Dinic was the fastest: on the random network it was 17 to 19 times faster than Edmonds-Karp, and on the 300 by 300 grid (source joined to the whole left column, sink to the whole right column, capacities 1 to 100) it was at least 5.8 times faster than the better of the other two. It also needed far fewer rounds than augmentations: 8 phases for 340 augmentations on the random network, 581 for 9,172 on the grid.

## 4. Max-flow min-cut

A **cut** is a partition of the vertices into `S` containing `s` and `T` containing `t`. Its **capacity** is the sum of capacities of the arcs from `S` to `T` (arcs the other way do not count).

**Weak duality.** The value of any flow is at most the capacity of any cut. The flow out of `s` is the flow across the cut minus the flow coming back, because conservation makes everything that enters `S` from inside cancel; and flow across is at most capacity, flow back is at least 0.

**Strong duality (max-flow min-cut).** When no augmenting path exists, let `S` be the vertices reachable from `s` in the residual graph. Then `t` is not in `S`. Every arc from `S` to outside is **saturated** (otherwise its head would be reachable) and every arc from outside into `S` carries **no flow** (otherwise its reverse would be a residual arc into the outside from `S`). So the flow across the cut equals the capacity, the flow equals the cut, and by weak duality both are optimal. This proves two things at once: a flow with no augmenting path is maximum, and **max flow = min cut**.

The proof is also the algorithm: after any of the three solvers, one search finds a minimum cut.

```cpp
// The minimum cut, read off a finished max flow: the vertices still reachable from s in the residual graph form the source side S.
// Every arc leaving S is saturated and every arc entering S carries no flow, so the capacity crossing the cut equals the flow.
std::vector<char> source_side(const Net& g, int s) {
    std::vector<char> in(g.n, 0); std::vector<int> st{s}; in[s] = 1;
    while (!st.empty()) { int v = st.back(); st.pop_back(); for (int e = g.head[v]; e != -1; e = g.nxt[e]) if (g.cap[e] > 0 && !in[g.to[e]]) { in[g.to[e]] = 1; st.push_back(g.to[e]); } }
    return in;
}
```

In the trace above, the source side is `{s}` and the cut arcs `s -> a` and `s -> b` have total capacity 5, equal to the flow. The tests check this for every random network: the capacity of the set found equals the flow value, and `t` is outside it.

**Modelling tricks.** All of these are one-line reductions (they are standard; the first two are used in the problems below).

- *Vertex capacities:* split `v` into `v_in -> v_out` with the vertex's capacity, send all incoming arcs to `v_in` and all outgoing arcs from `v_out`.
- *Several sources or sinks:* add a super source with arcs of infinite capacity to each source, and likewise a super sink. (The benchmark networks above are built this way.)
- *Undirected edge of capacity c:* two arcs, one each way.
- *Edge-disjoint paths:* all capacities 1; the maximum flow is the largest number of edge-disjoint `s`-`t` paths, and by max-flow min-cut it is the smallest number of edges whose removal disconnects them.

## 5. Bipartite matching

A **matching** in a bipartite graph (left side `L`, right side `R`) is a set of edges no two of which share a vertex. We want one of maximum size.

**As a flow.** Add a source with a capacity-1 arc to each left vertex, a capacity-1 arc from each right vertex to a sink, and give every allowed pair a capacity-1 arc from left to right. An integer flow in this network chooses a set of pairs in which every left and every right vertex is used at most once, so the maximum flow is exactly the maximum matching. Because the construction is just a flow, the three algorithms above all work; Dinic is the right one.

```cpp
// Maximum bipartite matching as a flow: source -> every left vertex (capacity 1), left -> right for every allowed pair (1),
// every right vertex -> sink (1). Integer capacities give an integer flow, and a unit of flow on a left-right arc is a matched pair.
// match_left[l] = the right vertex matched to l, or -1.
struct Matching { int size = 0; std::vector<int> match_left, match_right; };
Matching bipartite_matching(int nl, int nr, const std::vector<std::pair<int, int>>& pairs, int* phases = nullptr) {
    int s = nl + nr, t = s + 1; Net g(nl + nr + 2); std::vector<int> id;
    for (int l = 0; l < nl; ++l) g.add(s, l, 1);
    for (auto& p : pairs) id.push_back(g.add(p.first, nl + p.second, 1));
    for (int r = 0; r < nr; ++r) g.add(nl + r, t, 1);
    Matching m; m.match_left.assign(nl, -1); m.match_right.assign(nr, -1); m.size = (int)dinic(g, s, t, phases);
    for (size_t i = 0; i < pairs.size(); ++i) if (g.cap[id[i]] == 0) { m.match_left[pairs[i].first] = pairs[i].second; m.match_right[pairs[i].second] = pairs[i].first; }
    return m;
}
// Hall's theorem: every set S of left vertices has |N(S)| >= |S| if and only if some matching covers all of the left side.
// If the matching m leaves a left vertex u free, search from u by alternating paths (any edge left -> right, then the matching edge
// right -> left). The left vertices reached form a set S with |N(S)| = |S| - 1: a witness that no perfect matching of the left exists.
// Returns an empty list when the matching covers the left side.
std::vector<int> hall_violator(int nl, int nr, const std::vector<std::pair<int, int>>& pairs, const Matching& m) {
    int u = -1; for (int l = 0; l < nl; ++l) if (m.match_left[l] < 0) { u = l; break; }
    if (u < 0) return {};
    std::vector<std::vector<int>> adj(nl); for (auto& p : pairs) adj[p.first].push_back(p.second);
    std::vector<char> seenl(nl, 0), seenr(nr, 0); std::vector<int> st{u}, S; seenl[u] = 1;
    while (!st.empty()) {
        int l = st.back(); st.pop_back(); S.push_back(l);
        for (int r : adj[l]) if (!seenr[r]) { seenr[r] = 1; int l2 = m.match_right[r]; if (l2 >= 0 && !seenl[l2]) { seenl[l2] = 1; st.push_back(l2); } }
    }
    std::sort(S.begin(), S.end()); return S;
}
```

**Why Dinic is fast here.** In a network where every vertex other than `s` and `t` has a single incoming or single outgoing arc of capacity 1, Dinic needs only O(sqrt(V)) phases and runs in O(E*sqrt(V)): this is the Hopcroft-Karp bound, from memory (the matching network is such a network). Measured: random bipartite graphs with 200,000 vertices on each side and random edges, number of phases against the bound sqrt(400,000), about 632:

| Edges | Matching size | Phases | Time ms |
|---|---|---|---|
| 400,000 | 156,749 | 19 | 1,011 / 971 |
| 800,000 | 195,513 | 19 | 2,599 / 2,573 |
| 1,600,000 | 199,936 | 9 | 3,322 / 3,254 |

The phases (9 to 19) are far below 632. With 2 edges per vertex on average only about 78% of the left side can be matched; at 8 per vertex nearly all (199,936 of 200,000). The time grows with the number of edges; the linked-list layout of the network is probably not cache friendly (I did not measure that), and a specialised Hopcroft-Karp over arrays might be faster, but I did not build one.

### Hall's theorem and a witness

**Hall's theorem.** Some matching covers every left vertex if and only if every set `S` of left vertices has at least `|S|` neighbours. The "only if" is clear (the vertices of `S` need distinct partners). For "if", the code gives the proof: if a maximum matching leaves a left vertex `u` free, search from `u` along alternating paths (any edge from left to right, then the matching edge back). Every right vertex reached must be matched (otherwise the path would be augmenting), so the left vertices reached, `S`, are `u` plus the partners of the right vertices reached, and the neighbourhood of `S` is exactly those right vertices: `|N(S)| = |S| - 1`. `hall_violator` returns this `S`.

Trace (checked by running the code). Left vertices 0 to 3, right vertices 0 to 2, pairs `0-0, 0-1, 1-0, 2-0, 3-2`: the matching has size 3, left vertex 1 is free, and the violator is `{1, 2}` whose only neighbour is right vertex 0. A second graph with a perfect left matching returns an empty violator:

```
matching size=3 phases=1
match_left: 1 -1 0 2
Hall violator S: 1 2
neighbours of S: right 0
second graph: size=3 violator empty=1
```

**König's theorem and independent sets** (from memory; the formula is tested). In a bipartite graph the largest set of vertices with no edge between any two (an **independent set**) has `|V| - (maximum matching)` vertices, and the smallest vertex cover has the size of the maximum matching. That turns "choose the most seats such that no two conflict" into a matching problem when the conflict graph is bipartite (seats of even and odd columns, say). The test checks `|V| - matching` against brute force over all vertex subsets of 6000 random small graphs.

**Weighted assignment.** If the pairs have weights and you want the best total, plain matching is not enough: use **min-cost flow** (successive shortest augmenting paths in a residual graph with costs) or the Hungarian algorithm. Neither is in the listings; I only mention them (from memory) so that you do not try to solve a weighted assignment with the unweighted one. When the sides have at most about 8 to 10 vertices, trying all permutations is also fine.

## 6. Choosing

| Question | Tool | Cost (worst case) |
|---|---|---|
| Max flow, small integer capacities, a first version | Ford-Fulkerson (depth-first) | O(E * flow), can be slow even when the graph is small |
| Max flow, safe and short to write | Edmonds-Karp | O(V E^2) |
| Max flow in practice, large networks | Dinic | O(V^2 E); O(E sqrt(V)) for unit-capacity matching networks |
| Minimum cut | Any max flow, then reachability from `s` | + O(V + E) |
| Maximum bipartite matching | Dinic on the unit network | O(E sqrt(V)) |
| Is there a perfect matching of the left side? | Matching size, Hall violator if not | |
| Largest conflict-free set in a bipartite conflict graph | `|V|` minus the maximum matching | |
| Best total weight assignment | Min-cost flow or Hungarian (not covered here) | |

## 7. Common mistakes

1. **Forgetting the reverse arcs.** Without them the greedy method stops at a flow that is not maximum (the 4-vertex example in section 1).
2. **Using the original capacity where the residual is needed.** All three algorithms must test the *remaining* capacity `cap[e] > 0`.
3. **Setting the twin's capacity to anything but 0** (for a directed arc). A twin with positive capacity invents capacity.
4. **Counting flow on an arc by its remaining capacity.** The flow is the capacity of the twin, not the capacity left on the arc.
5. **Depth-first Ford-Fulkerson with large capacities and an adversarial structure.** The number of rounds depends on the capacities; use shortest paths or Dinic.
6. **Dinic without the current-arc pointer.** It is still correct and can be dramatically slower: every phase re-examines dead arcs.
7. **Forgetting to cut off a dead end** (the level of a vertex that cannot reach `t`). Same effect.
8. **Retreating to the start of the path after each push in Dinic.** Correct but wasteful; retreat to the first saturated arc.
9. **Reading a matching from the left-to-right arcs that still have capacity.** A matched pair is an arc whose capacity is **used up** (0), not one that has capacity.
10. **Vertex capacities ignored.** If a vertex can only carry c units, split it.
11. **Solving an assignment with weights using unweighted matching.** The matching ignores the weights.
12. **Assuming the minimum cut is unique.** There may be many cuts of the same capacity; the reachability set is the one closest to `s`.

## 8. What the tests check, and what they cannot

Of the 36 mutants, 29 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Seven survive, and each leaves every answer unchanged: the mutated program returns the same flow, cut and matching on every input. Most of them change only speed or the number of rounds, which the tests, looking at answers and a few pinned counts, do not measure. The reasons:

- **Resetting the arc pointer of a newly reached vertex in the depth-first Ford-Fulkerson.** The pointer already starts at the head of the list and each vertex is reached once per search, so the reset does nothing.
- **Inserting new vertices at the front of the Edmonds-Karp queue.** The search is no longer breadth-first, so the path found need not be a shortest one. Any augmenting path still raises the flow, so the value is right; what is lost is the O(V E^2) bound, which a value check cannot see.
- **Dropping the early stop when `t` is reached in Edmonds-Karp.** The search simply continues to the end of the queue; the path to `t` is the same.
- **Letting Dinic use arcs that skip levels (`>=` instead of `==`).** Such an arc still lies on an augmenting path, so answers stay right; the number of phases and the work per phase are what change.
- **Retreating to the last saturated arc instead of the first in Dinic.** The path then keeps some saturated arcs, so the next push moves zero units and the loop retreats again; the flow value is the same, with extra steps.
- **Restarting from `s` after each push in Dinic.** Correct but wasteful: the part of the path before the saturated arc is searched again.
- **Choosing the last free left vertex instead of the first as the start of the Hall search.** Any free left vertex gives a valid witness set.

The large inputs have no brute-force reference; they are checked by agreement: the three flow algorithms return the same value on random networks and on a grid, the matching of the graph with a planted perfect matching has size 300,000 with every pair consistent and no Hall violator, and a smaller random matching is the same size from Dinic as from Edmonds-Karp. What the tests cannot do is show that the running times hold on other inputs: the benchmark rows are one sample each, and adversarial networks for Dinic (which exist) were not tried.

Mutants are chosen where a bug is plausible, not at random, so "29 of 36" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Questions to answer out loud

1. State the two constraints of a flow. What is the value of a flow?
2. What is the residual graph? Why does it need backward arcs? Give a small example where greedy without them fails.
3. What is an augmenting path? State the theorem that links it to maximum flow.
4. Why can Ford-Fulkerson be slow even on a four-vertex graph? Why is Edmonds-Karp immune to that?
5. How many augmentations can Edmonds-Karp make, and why?
6. What is a level graph, a blocking flow, and a current-arc pointer? What do they buy?
7. State max-flow min-cut. Prove weak duality and describe how to find a minimum cut from a maximum flow.
8. How do you model a capacity on a vertex? Several sources? An undirected edge?
9. Reduce bipartite matching to flow. Why does an integer flow give a matching?
10. State Hall's theorem and describe how the alternating search proves it.
11. How does the maximum matching give the largest independent set of a bipartite graph?
12. When is matching not enough for an assignment problem?

## 10. Problems

### 10.1 On paper

**A.** Run Edmonds-Karp by hand on `s -> a: 4, s -> b: 3, a -> b: 2, a -> t: 2, b -> t: 3`. How many augmentations, and what is the flow?

**B.** Find a minimum cut of the network in A and verify that its capacity equals the flow. Is it the only one?

**C.** Prove that the value of any flow is at most the capacity of any cut.

**D.** Left vertices `a, b, c, d` with allowed right vertices `a: {x, y}`, `b: {x}`, `c: {x, y}`, `d: {z}`. What is the maximum matching? Give a set that violates Hall's condition.

**E.** A 2 by 3 board has its top-left cell removed. What is the largest number of dominoes (each covering two adjacent cells) that can be placed? Model it as a matching.

**F.** Show on the network `s -> a, s -> b, a -> b, a -> t, b -> t` (all capacities 1) that a greedy method without backward arcs can stop at flow 1, and that the maximum is 2.

**G.** The path `s -> a -> t` has capacities 5 and 5, but vertex `a` can pass at most 3 units. How do you model that, and what is the flow?

### 10.2 On LeetCode

For each: what are the two sides or the network, and which tool? Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Maximum Students Taking Exam | 1349 | Seats a student can see are conflicts between columns of different parity: independent set = good seats minus maximum matching (section 5); a bitmask DP over rows is the other solution |
| Maximum Number of Accepted Invitations | 1820 | Plain bipartite matching: boys against girls |
| Maximum Compatibility Score Sum | 1947 | A weighted assignment: with the small limits a permutation or bitmask search works; for large inputs it needs min-cost flow or the Hungarian algorithm |

### 10.3 In your editor

**A.** Dinic on a unit-capacity bipartite graph (the matching code), then count the phases on random graphs of growing size and compare with sqrt(V).

**B.** Write Hopcroft-Karp over plain arrays (a BFS from the free left vertices, then a DFS along level-increasing alternating paths) and compare with `bipartite_matching` for speed and for the matching size.

**C.** Add capacity scaling to Ford-Fulkerson (only use arcs with residual capacity at least `Delta`, halve `Delta`) and compare the augmentation count with the listings.

**D.** Write the minimum-cost version: successive shortest augmenting paths with Bellman-Ford on a residual graph with costs; test it against brute force on assignments of up to 7 workers.

**E.** The **project selection** problem: projects with profits (some negative), each requiring a set of other projects. Reduce it to a minimum cut and test it against brute force.

**F.** Compute the number of edge-disjoint paths between two vertices of a random undirected graph and verify it against the smallest edge cut found by brute force on small graphs.

### Answers

**A.** The shortest paths have two arcs: `s -> a -> t` (bottleneck 2) and `s -> b -> t` (bottleneck 3). After those two augmentations the arcs into `t` are full, so the flow is 5 (the code gives 2 augmentations, flow 5).

**B.** Take `S = {s, a, b}`: the arcs leaving it are `a -> t` (2) and `b -> t` (3), capacity 5 = the flow. It is the only minimum cut here: `{s}` has capacity 7, `{s, a}` has 7 and `{s, b}` has 7 (the code finds `{s, a, b}`).

**C.** For a flow `f` and a cut `(S, T)`, add the conservation equations for all vertices in `S` other than `s`. Arcs with both ends in `S` cancel, so value(f) = (flow on arcs from `S` to `T`) minus (flow on arcs from `T` to `S`). The first term is at most the capacity of the cut and the second is at least 0.

**D.** Maximum matching 3 (`a-y`, `b-x`, `d-z` for instance; `c` cannot be matched). The set `{a, b, c}` has neighbours `{x, y}` only, 2 < 3. The code returns the violator `{a, b, c}`.

**E.** Five cells remain, so at most 2 dominoes; two can be placed. Colour the cells like a chessboard: dominoes join a black cell to a white one, so they are matchings in the bipartite graph of adjacent cells. The code returns 2.

**F.** The path `s -> a -> b -> t` uses `s -> a` and `b -> t` and leaves no other path in the original graph. In the residual graph `s -> b -> a -> t` exists (through the backward arc `b -> a`), giving 2. The code returns max flow 2.

**G.** Replace `a` by `a_in -> a_out` with capacity 3. The flow is 3 (the code returns 3).

**10.2 hints.** 1820: the sample grid `[[1,1,1],[1,0,1],[0,0,1]]` has maximum matching 3 (checked by running the code). 1349: in the sample 3 by 6 room (as I remember it) there are 6 good seats, the maximum matching of the conflict graph is 2, so 4 students (checked); the match between the even and odd columns uses the left, right, upper-left, upper-right, lower-left and lower-right neighbours.

**10.3 hints.** E: add a source arc for each profitable project (capacity = profit), a sink arc for each costly one (capacity = cost), and an infinite arc from a project to each one it requires; the answer is the total profit minus the minimum cut.

---

## Cheat sheet

- **Flow:** capacity and conservation; value = net out of `s`. **Residual graph:** forward `c - f`, backward `f`; store arcs in pairs `e`, `e ^ 1`.
- **Augmenting path** exists iff the flow is not maximum.
- **Ford-Fulkerson:** any path, O(E * flow). **Edmonds-Karp:** shortest path, O(V E^2). **Dinic:** levels plus blocking flow plus current arcs, O(V^2 E).
- **Min cut:** after max flow, the vertices reachable from `s` in the residual graph; its capacity equals the flow.
- **Tricks:** split a vertex for a vertex capacity; super source and sink; two arcs for an undirected edge.
- **Matching:** unit network, Dinic, O(E sqrt(V)). Matched pair = a left-right arc with capacity 0 left.
- **Hall:** alternating search from a free left vertex gives `S` with `|N(S)| = |S| - 1`.
- **König:** maximum independent set = `|V|` minus the maximum matching (bipartite).
- **Weights:** min-cost flow or Hungarian, not this.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Ford and Fulkerson's method and the max-flow min-cut theorem (1956), Edmonds and Karp's shortest-path rule (1972), Dinic's algorithm (1970), the Hopcroft-Karp bound O(E sqrt(V)) for unit networks (1973), Hall's marriage theorem and König's theorem are standard (named from memory; years approximate). The textbook network with value 23 is the one in Cormen, Leiserson, Rivest and Stein (from memory). The min-cost flow and Hungarian remarks, and the project selection reduction, are from memory and not implemented here.
- Problem numbers and titles in section 10, and the samples of 1820 and 1349, are LeetCode's, from memory; check a number before you rely on it.
