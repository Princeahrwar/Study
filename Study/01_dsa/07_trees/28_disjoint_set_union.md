# DSA 07.28: Disjoint Set Union (Union-Find)

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (5 reading, 3 problems) · You need: [General trees and the Euler tour](24_general_trees_euler_tour_and_subtree_queries.md) (the `Edge` struct of its `trees.hpp` is used for Kruskal) and [LCA at scale](25_lca_at_scale.md) (its Tarjan offline LCA was the first place this structure appeared; here it gets its full treatment). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

A **disjoint set union** (DSU, also called union-find) keeps a collection of disjoint sets of the numbers 0 to n - 1 and supports two operations: **find** (which set is this element in?) and **unite** (merge two sets). It is a forest of parent pointers in one array. It is short enough to write from memory in an interview, and it is behind connected components, cycle detection, Kruskal's minimum spanning tree, equations of the form "a / b = 2", grid problems where land appears over time, and, with one change, offline problems where edges are added **and removed**.

By the end you can:

- write the structure in a dozen lines and say what each of the two speed-ups buys: **union by size** (depth at most log2 n) and **path halving** (flatter trees as a side effect of finds), with measurements of each alone and together;
- state the inverse-Ackermann bound for both together and say what it means in practice;
- use it for **connectivity, cycle detection and Kruskal**, and say why Kruskal is right (the cut property);
- carry **extra information along the path to the root**: a weighted DSU that answers "what is val[a] - val[b]?" and detects contradictions;
- build a **rollback DSU** (no compression, a stack of unions) and use it for **offline dynamic connectivity** with a segment tree over time;
- avoid the usual bugs: recursive `find` overflowing the stack, reading `sz` at a non-root, mixing compression with rollback, and the quadratic version with no heuristic.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 2,880,695 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: all four variants (neither speed-up, size only, halving only, both) against a brute-force labelling over random mixes of `unite`, `same`, `size_of` and `components`, and the exact parent arrays of the traces below; a union by size tree never deeper than log2 n, and exactly 17 deep on 2^17 nodes merged in equal pairs; `kruskal` against the best of all edge subsets that form a spanning forest (weights from -3 to 5), with the picked edges checked to be in order of weight and then of input position, and an all-equal-weights case for the tie rule; `WeightedDsu` against a search over the accepted relations, with deliberately contradicting values mixed in; `RollbackDsu` against recomputing the sets from the surviving unions after every step; `offline_connectivity` against recomputing connectivity at every query; chains and random unions of 10^6 nodes; and bad arguments for every function that checks its arguments, including `n = 0` (which must work). Finally 104 hand-made mutants of the code were run against the tests (a flipped comparison, a lost size update, a missing swap, an off-by-one interval end and so on): **100 were caught and 4 are equivalent changes** that do not alter any output (listed after the common mistakes).

**Data and timing.** The data is **synthetic**: random unions of two uniformly random nodes (so the sets are not tidy), and the chain `unite(i, i + 1)`, which is the worst case for a structure with no size rule. Times are on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 27), best of 3 repetitions unless a row says 1, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about results I did not test are from general knowledge and say so.

---

## 1. A forest of parent pointers

Every element has a parent; a root is its own parent. The root is the **name** of the set. `find(x)` walks up from `x` to the root; `unite(a, b)` finds the two roots and hangs one under the other. Two elements are in the same set exactly when their roots are equal.

With nothing else, this is correct but slow. The tree can become a path: uniting `(0, 1), (1, 2), (2, 3), ...` and always hanging the first root under the second gives `parent = 1 2 3 4 5 5` for six elements, and `find(0)` then walks the whole chain. At 10^6 elements that chain costs 10^6 steps per find. The two fixes below attack the two ways a tree gets deep.

Here is the structure with both fixes as switches (so that the cost of leaving either out can be measured):

```cpp
// A forest of parent pointers: the root of a tree is the name of its set.
// BySize: attach the smaller tree under the larger one (depth stays at most log2 n).
// Halve : while walking up in find, point every visited node at its grandparent (path halving).
// The two switches are separate so that the cost of leaving either out can be measured.
template <bool BySize, bool Halve>
struct DsuT {
    std::vector<int> parent, sz;               // sz[r] is the number of nodes in the set whose root is r
    int comps;
    explicit DsuT(int n) : parent(n < 0 ? throw std::invalid_argument("Dsu: n") : n), sz(n, 1), comps(n) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int x) {
        if (x < 0 || x >= static_cast<int>(parent.size())) throw std::out_of_range("Dsu: node");
        while (parent[x] != x) {
            if (Halve) parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    bool unite(int a, int b) {                 // false if they were already in one set
        int ra = find(a), rb = find(b);
        if (ra == rb) return false;
        if (BySize && sz[ra] > sz[rb]) std::swap(ra, rb);   // ra is the smaller root and goes under rb
        parent[ra] = rb; sz[rb] += sz[ra]; --comps;
        return true;
    }
    bool same(int a, int b) { return find(a) == find(b); }
    int size_of(int x) { return sz[find(x)]; }
    int components() const { return comps; }
};
using Dsu = DsuT<true, true>;                  // the one to use: union by size + path halving
```

Three details matter:

- **`find` is a loop, not a recursion.** A tree of 10^6 elements without the size rule can be 10^6 deep, and a recursive `find` would overflow the stack. The loop is also faster.
- **`sz[r]` is only meaningful at a root.** `size_of` goes through `find` first. Reading `sz[x]` at an arbitrary node gives a stale number.
- **`unite` returns whether it merged.** That `bool` is what cycle detection and Kruskal use.

### A trace

Eight elements, `DsuT<true, false>` (size rule, no halving), the unions `(0,1), (2,3), (0,2), (4,5), (6,7), (4,6), (0,4)`. The parent array after each union (a root is its own parent; ties hang the first root under the second):

```
1 1 2 3 4 5 6 7     after (0,1)
1 1 3 3 4 5 6 7     after (2,3)
1 3 3 3 4 5 6 7     after (0,2)
1 3 3 3 5 5 6 7     after (4,5)
1 3 3 3 5 5 7 7     after (6,7)
1 3 3 3 5 7 7 7     after (4,6)
1 3 3 7 5 7 7 7     after (0,4)
```

The sizes are `1 2 1 4 1 2 1 8` (only the roots 1, 3, 5, 7 matter), there is one component, and `unite(3, 1)` now returns false. Node 0 sits at depth 3 (0, 1, 3, 7): exactly log2 8. Equal-sized sets are the only case where the depth of a tree grows, and then only by one, which is the whole proof in the next section.

## 2. The two speed-ups and the bound

### Union by size

Always hang the **smaller** tree under the larger one. A node's depth grows by one only when its root is hung under another root, which happens only when the set it is in is no larger than the other, so the set at least **doubles**. A set cannot double more than log2 n times, so no node is ever deeper than log2 n. That alone makes `find` O(log n). (A variant, union by *rank* or height, gives the same bound and is the version in many textbooks; it is general knowledge and is not implemented here, size is easier to reason about and gives the set size for free.)

### Path halving

While walking up in `find`, point each visited node at its grandparent: `parent[x] = parent[parent[x]]`. This needs no second pass and no stack, and it flattens the tree for later finds. The older form, **path compression**, points every node on the path at the root (in two passes or recursively). Halving and compression have the same guarantees; halving is the one that fits in a loop.

On the chain `unite(i, i + 1)` with halving only, `find(0)` gives `parent = 2 2 4 4 5 5` for six elements (the path 0, 1, 2, 3, 4, 5 was visited as 0 to 2, 2 to 4, and the last hops are already at the root), and a second `find(0)` gives `4 2 4 4 5 5`.

### Both together

With union by size **and** path compression (or halving), the amortized cost per operation is O(alpha(n)), where alpha is the **inverse Ackermann function**. This is Tarjan's result (1975; named from memory, not re-read). alpha grows so slowly that it is at most 4 for any n that fits in the universe (also from memory); in practice it is "constant". Two things worth saying out loud: the bound is **amortized** (one `find` can still take log n steps), and either heuristic alone gives O(log n) amortized, not the inverse-Ackermann bound.

### Measured

Random unions of random pairs: 2n unions, then n finds, on n nodes. "Mean depth" and "max depth" are measured on the final forest, before the finds. Number of components at the end: 1826 for n = 10^5; 19,117 for n = 10^6 (a breadth-first search over the same edges agrees; the biggest set has 980,172 elements).

| n | Variant | Time (ms) run 1 / run 2 | Mean depth | Max depth |
|---|---|---|---|---|
| 10^5 | none (1 repetition) | 15,629.8 / 16,420.1 | 11,388.213 | 16,546 |
| 10^5 | size only | 5.8 / 5.9 | 2.028 | 7 |
| 10^5 | halving only | 5.9 / 6.1 | 3.490 | 8 |
| 10^5 | both | 2.2 / 2.3 | 1.046 | 3 |
| 10^6 | size only | 111.0 / 107.7 | 2.047 | 8 |
| 10^6 | halving only | 180.9 / 170.5 | 4.232 | 9 |
| 10^6 | both | 55.8 / 54.8 | 1.044 | 3 |

What both runs agree on:

- **No heuristic is catastrophic.** At n = 10^5 the plain version took 15 to 16 **seconds**, more than 6,000 times the 2.2 ms of "both" (the ratio is 7,100 in run 1). It is quadratic: the mean depth is about 0.11 n. That is why it was not run at 10^6.
- **Both together is the fastest**, at 10^6 about 2 times faster than size only and 3 times faster than halving only. At 10^5 size only and halving only are about equal (5.8 against 5.9 ms), so no ranking is claimed there.
- **The depth numbers show why.** The maximum depth with the size rule is 7 and 8 against log2 n of 16.6 and 19.9: the bound is far from tight on random data. With both, the mean depth is about 1.04: nearly every node points straight at its root.

The chain `unite(i, i + 1)` at n = 10^6 is where each heuristic earns its keep. "Build" is the time of all the unions; "100 finds" is 100 calls of `find(0)` afterwards:

| Variant | Build (ms) run 1 / run 2 | Max depth after build | 100 finds (ms) run 1 / run 2 |
|---|---|---|---|
| none | 2.5 / 2.6 | 999,999 | 186.486 / 187.364 |
| size only | 4.6 / 4.7 | 1 | 0.001 / 0.000 |
| halving only | 2.1 / 2.3 | 999,999 | 6.599 / 6.956 |
| both | 4.0 / 3.8 | 1 | 0.000 / 0.000 |

The build is cheap in every case (the unions touch only roots, which are always at hand). The damage is in the finds: with no heuristic each of the 100 finds walks 10^6 nodes. The size rule makes the tree a star (depth 1) because the single large set always absorbs the new singleton. Halving alone cannot repair the shape at once, but the first find halves the whole path and the next ones are cheaper, so 100 finds cost 27 to 28 times less than with no heuristic.

## 3. Using it

### Connectivity, cycles and counting

Process the edges one at a time with `unite`. If `unite(u, v)` returns **false**, the edge closes a cycle (u and v were already connected). `components()` gives the number of sets at any moment, which is `n` minus the number of successful unions. In the standard "Redundant Connection" problem, the answer is the first edge for which `unite` returns false.

Compared with a graph search: a DSU answers "are they connected now?" while edges are **added** in any order, with no graph stored. It cannot enumerate neighbours or paths, and it cannot delete an edge (see section 5).

### Kruskal's minimum spanning tree

Sort the edges by weight; scan them in that order; keep an edge if it joins two different sets. The answer is a minimum spanning tree (a **forest** with one tree per component if the graph is disconnected). It is correct by the **cut property**: for any way of splitting the nodes into two groups, the cheapest edge between the groups can be in some minimum tree. When Kruskal looks at an edge that joins two different sets, that edge is the cheapest remaining edge leaving one of those sets (all cheaper ones were already looked at), so it is safe. Negative weights are fine. Ties are broken by input order here (`stable_sort`), so the result is reproducible.

```cpp
// Minimum spanning forest: scan the edges by increasing weight (ties by input order) and keep an edge
// if it joins two different sets. Works with negative weights; on a disconnected graph it gives a spanning forest.
struct Mst { long long weight; std::vector<int> picked; int components; };   // picked: indices into the edge list

inline Mst kruskal(int n, const std::vector<Edge>& edges) {
    std::vector<int> idx(edges.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return edges[a].w < edges[b].w; });
    Dsu d(n);
    Mst r{0, {}, n};
    for (int i : idx) {
        const Edge& e = edges[i];
        if (d.unite(e.u, e.v)) { r.weight += e.w; r.picked.push_back(i); }
    }
    r.components = d.components();
    return r;
}
```

The 7-node example: edges `(0,1,4), (0,2,3), (1,2,2), (1,3,5), (2,3,7), (3,4,6), (4,5,1), (3,5,8)` (as `(u, v, weight)`, numbered 0 to 7). Sorted by weight: edge 6 (weight 1), 2 (2), 1 (3), 0 (4, rejected: 0 and 1 are already joined through 2), 3 (5), 5 (6), 4 (7, rejected), 7 (8, rejected). The tree is edges `6 2 1 3 5` with total weight 1 + 2 + 3 + 5 + 6 = **17**, and there are **2 components** because node 6 has no edge. The sort costs O(E log E) and dominates; the unions are almost free. A forest of 10^6 nodes and 3 * 10^6 random edges (weights below 10^6) took 544.9 / 612.9 ms (run 1 / run 2), of which most is the sort; the result (weight 197,720,173,564, 2,500 components, 997,500 edges) was checked on this input to be acyclic, to have as many components as the whole edge set and a weight equal to the sum of its edges (that it is a *minimum* is what the small brute-force tests show). When the graph is dense, Prim's algorithm with a heap is the usual alternative (general knowledge, not implemented here).

### A weighted DSU: "a - b = d"

Many problems give relations between pairs ("a is 3 more than b", "a / b = 2") and ask about other pairs or whether the relations contradict each other. Store with each node the **difference to its parent**: `diff[x] = val[x] - val[parent[x]]`, for values we never see. Then `val[x] - val[root]` is the sum of the `diff` values on the path to the root, and `find` can add those up while it flattens the path.

```cpp
// Union-find that also remembers differences: diff[x] = val[x] - val[parent[x]] for unknown values val.
// add(a, b, d) states val[a] - val[b] = d. It returns false if that contradicts what is already known.
struct WeightedDsu {
    std::vector<int> parent, sz; std::vector<long long> diff; std::vector<int> path;
    explicit WeightedDsu(int n) : parent(n < 0 ? throw std::invalid_argument("WeightedDsu: n") : n), sz(n, 1), diff(n, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int x) {                          // afterwards diff[x] = val[x] - val[root]
        if (x < 0 || x >= static_cast<int>(parent.size())) throw std::out_of_range("WeightedDsu: node");
        path.clear();
        int r = x;
        while (parent[r] != r) { path.push_back(r); r = parent[r]; }
        for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i) {     // from the node nearest the root downwards
            int v = path[i], p = parent[v];
            diff[v] += diff[p];                // p already points at r (diff[p] is relative to r), or p is r itself and diff[r] = 0
            parent[v] = r;
        }
        return r;
    }
    bool add(int a, int b, long long d) {
        int ra = find(a), rb = find(b);
        if (ra == rb) return diff[a] - diff[b] == d;
        long long dr = d - diff[a] + diff[b];  // val[ra] - val[rb]
        if (sz[ra] > sz[rb]) { std::swap(ra, rb); dr = -dr; }
        parent[ra] = rb; diff[ra] = dr; sz[rb] += sz[ra];
        return true;
    }
    std::optional<long long> query(int a, int b) {     // val[a] - val[b], if the two are connected
        if (find(a) != find(b)) return std::nullopt;
        return diff[a] - diff[b];
    }
};
```

How it works:

- **`find`** walks to the root, remembers the path, then goes through it **from the node nearest the root downwards**: when it reaches `v`, its parent `p` already points at the root and `diff[p]` is already relative to the root, so `diff[v] += diff[p]` makes `diff[v]` relative to the root, and `parent[v] = r`. (For the child of the root, `p` is the root and `diff[root]` is 0.) Doing this in the other order uses `diff[p]` before it has been fixed.
- **`add(a, b, d)`** finds both roots. If they are equal, the answer is a check: is `val[a] - val[b]` as stored equal to `d`? If not, the new fact **contradicts** the old ones and `add` returns false. Otherwise it hangs one root under the other with `dr = val[ra] - val[rb] = d - diff[a] + diff[b]` (from `val[a] = diff[a] + val[ra]`). When the roots are swapped to keep the larger one on top, `dr` changes sign.
- **`query(a, b)`** is `val[a] - val[b]` if the two are in one set, nothing otherwise.

Trace: `add(0,1,3)` and `add(1,2,4)` succeed, `add(0,2,8)` fails (the known difference is 3 + 4 = 7) and `add(0,2,7)` succeeds (it repeats what is known). `query(0,2)` is 7, `query(2,0)` is -7, and `query(0,4)` has no value. After `add(4,0,1)`, `query(4,2)` is 1 + 7 = 8.

Multiplicative relations ("a / b = 2") use the same structure with products or ratios in place of sums (with care about zero and about floating point; a common trick is to store logarithms or exact fractions). The "Evaluate Division" problem is this.

Cost: a chain of 10^6 `add(i, i + 1, 1)` took 4.8 / 5.4 ms, and 10^6 random queries on a 10^6-node structure took 19.5 / 25.0 ms.

## 4. Undo: the rollback DSU

Path compression changes many parents at once, and undoing it means remembering all of them. Giving it up is cheaper: use **union by size only** (depth at most log2 n, so find is O(log n)) and a **stack** that records each successful union as "root `c` was hung under root `p`". Undoing it is three assignments: `sz[p] -= sz[c]`, `parent[c] = c`, `++comps`. `snapshot()` is the stack height; `rollback(s)` pops until the stack has height `s`.

```cpp
// Union-find that can undo its unions, for offline problems. No path compression (it would be hard to undo);
// union by size alone keeps find at O(log n). Every successful union is pushed on a stack.
struct RollbackDsu {
    std::vector<int> parent, sz; std::vector<std::pair<int, int>> stack;    // (root attached, root it went under)
    int comps;
    explicit RollbackDsu(int n) : parent(n < 0 ? throw std::invalid_argument("RollbackDsu: n") : n), sz(n, 1), comps(n) {
        std::iota(parent.begin(), parent.end(), 0);
    }
    int find(int x) const {
        if (x < 0 || x >= static_cast<int>(parent.size())) throw std::out_of_range("RollbackDsu: node");
        while (parent[x] != x) x = parent[x];
        return x;
    }
    bool unite(int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra == rb) return false;
        if (sz[ra] > sz[rb]) std::swap(ra, rb);
        parent[ra] = rb; sz[rb] += sz[ra]; --comps; stack.push_back({ra, rb});
        return true;
    }
    int snapshot() const { return static_cast<int>(stack.size()); }
    void rollback(int snap) {                  // undo every union made after snapshot() returned snap
        if (snap < 0 || snap > static_cast<int>(stack.size())) throw std::invalid_argument("RollbackDsu: snapshot");
        while (static_cast<int>(stack.size()) > snap) {
            auto [c, p] = stack.back(); stack.pop_back();
            sz[p] -= sz[c]; parent[c] = c; ++comps;
        }
    }
};

// Offline dynamic connectivity. ops: type 0 = add edge (u, v), 1 = remove edge (u, v), 2 = ask whether u and v are connected.
// An edge lives for an interval of operation numbers; each interval is stored in O(log T) nodes of a segment tree over time,
// and a depth-first walk over that tree adds the edges of a node, recurses, and rolls back.
struct ConnOp { int type, u, v; };

inline std::vector<char> offline_connectivity(int n, const std::vector<ConnOp>& ops) {
    const int T = static_cast<int>(ops.size());
    std::vector<std::vector<std::pair<int, int>>> seg(4 * static_cast<std::size_t>(T) + 4);
    std::map<std::pair<int, int>, int> open;                              // edge -> the time it was added, for the edges present now
    auto put = [&](auto&& self, int node, int lo, int hi, int l, int r, std::pair<int, int> e) -> void {   // add e to [l, r)
        if (l >= hi || r <= lo) return;
        if (l <= lo && hi <= r) { seg[node].push_back(e); return; }
        int mid = (lo + hi) / 2;
        self(self, 2 * node, lo, mid, l, r, e); self(self, 2 * node + 1, mid, hi, l, r, e);
    };
    for (int t = 0; t < T; ++t) {
        const ConnOp& o = ops[t];
        if (o.u < 0 || o.u >= n || o.v < 0 || o.v >= n || o.type < 0 || o.type > 2) throw std::invalid_argument("offline_connectivity: op");
        std::pair<int, int> e{std::min(o.u, o.v), std::max(o.u, o.v)};
        if (o.type == 0) {
            if (!open.emplace(e, t).second) throw std::invalid_argument("offline_connectivity: edge already present");
        } else if (o.type == 1) {
            auto it = open.find(e);
            if (it == open.end()) throw std::invalid_argument("offline_connectivity: edge not present");
            put(put, 1, 0, T, it->second, t, e); open.erase(it);
        }
    }
    for (auto& x : open) put(put, 1, 0, T, x.second, T, x.first);        // still present at the end
    RollbackDsu d(n);
    std::vector<char> answer;
    auto walk = [&](auto&& self, int node, int lo, int hi) -> void {
        int snap = d.snapshot();
        for (auto& e : seg[node]) d.unite(e.first, e.second);
        if (hi - lo == 1) { if (ops[lo].type == 2) answer.push_back(d.find(ops[lo].u) == d.find(ops[lo].v)); }
        else { int mid = (lo + hi) / 2; self(self, 2 * node, lo, mid); self(self, 2 * node + 1, mid, hi); }
        d.rollback(snap);
    };
    if (T > 0) walk(walk, 1, 0, T);
    return answer;
}
```

Trace: `unite(0,1)`; `snapshot()` is 1; `unite(2,3)`, `unite(1,2)` give 3 components and `same(0,3)` true; `rollback(1)` leaves 5 components, `same(0,3)` false, `same(0,1)` true, stack of size 1. 2 * 10^6 random unions on 10^6 nodes took 70.5 / 83.8 ms, undoing all of them 11.2 / 15.3 ms (and the depth stayed small: mean 2.047, max 8, as in the table above; this workload has no finds, so its time is not comparable with the variant timings).

### Offline dynamic connectivity

The problem: a sequence of operations "add edge", "remove edge", "are `u` and `v` connected now?". DSU cannot delete an edge, but if all the operations are known in advance (offline), there is a classic trick:

1. Every edge is present during an **interval of operation numbers** `[added, removed)` (until the end if never removed).
2. Build a segment tree over time. Store each interval in the O(log T) nodes that cover it.
3. Walk the tree depth first. On entering a node, `unite` all of its edges; at a leaf, answer the query (if that operation is one); on leaving, **roll back** to the snapshot taken on entry.

At a leaf, exactly the edges whose interval contains that time have been united, because every ancestor of the leaf contributed the intervals that cover it. The cost is O(E log T) unions of O(log n) each. On n = 10^5 nodes and 300,000 operations it took 204.6 / 183.9 ms, and the answers matched the brute force.

Trace on 4 nodes: the operations are `add(0,1), add(1,2), query(0,2), remove(1,2), query(0,2), add(2,3), query(0,3), add(1,2), query(0,3)`, numbered 0 to 8. Edge `(0,1)` lives in `[0, 9)`, edge `(1,2)` in `[1, 3)` and `[7, 9)`, edge `(2,3)` in `[5, 9)`. The four answers are `1 0 0 1`: at time 2 the path 0, 1, 2 exists; at time 4 `(1,2)` is gone; at time 6 it is still gone (so 3 is unreachable); at time 8 all three edges are present.

## 5. What it costs, and choosing

| Operation | Time |
|---|---|
| `find`, `unite`, `same` with both heuristics | O(alpha(n)) amortized (from memory), about 1 to 2 in practice |
| `find` with the size rule only | O(log n) worst case |
| Kruskal | O(E log E) for the sort, plus E unions |
| Weighted `find` / `add` / `query` | same as the plain structure |
| Rollback `unite` / `find` / `rollback` | O(log n) / O(log n) / O(1) per undone union |
| Offline connectivity | O(E log T) unions, each O(log n) |
| Memory | one or two int arrays of size n (plus a stack for rollback) |

Choosing:

- **Edges only added, queries "connected?" or "how many components?":** the plain DSU with both heuristics.
- **Minimum spanning tree of a sparse graph:** Kruskal. For a dense graph consider Prim with a heap.
- **Relations of the form a - b = d (or a / b = r):** weighted DSU.
- **Edges added and removed, all known in advance:** the rollback DSU with a segment tree over time. If the edges are not known in advance this does not apply (the online problem needs link-cut trees or Euler tour trees, general knowledge, not implemented here).
- **Only deletions:** run the process in reverse and the deletions become additions.
- **You also need the actual path or the neighbours:** a graph search, not a DSU.

## 6. Common mistakes

- **Recursive `find`.** On a deep tree (a chain of 10^6 without the size rule) it overflows the stack. Use the loop.
- **No heuristic at all.** The code works and passes small tests, then takes 15 seconds on 10^5 random unions (and 186 ms for 100 finds on a chain of a million).
- **Reading `sz` or `parent` at a non-root.** `sz[x]` is meaningful only where `x` is a root; always go through `find` first.
- **Path compression together with rollback.** Compression rewrites parents far from the union being undone, so the stack no longer describes the changes. Use size only, as above.
- **Wrong order in the weighted `find`.** Fixing `diff[v]` before its parent's `diff` is already relative to the root gives wrong differences on paths of length 3 or more; the tests that use long chains are what catch this.
- **Sign and direction in the weighted `add`.** `add(a, b, d)` means `val[a] - val[b] = d`; when the roots are swapped to keep the bigger one on top the difference changes sign.
- **Kruskal on a disconnected graph.** It returns a spanning **forest**; check `components` if you need a tree. Ties in the weights give different trees with the same weight; if the exact edges matter, fix the tie rule (here, input order).
- **Offline connectivity, interval end.** An edge removed at time `t` is present for times strictly before `t`: the interval is `[added, t)`. An edge never removed lasts until `T`.
- **Adding an edge that is already present.** The offline routine treats it as an error (and a removal of an absent edge too); decide what your problem means before passing such input.

The equivalent mutants (changes that do not alter any output, so no test can catch them): starting the write-back loop of the weighted `find` one node short (the skipped node is the child of the root, for which the loop only repeats `parent[v] = r` and adds `diff[root] = 0`); initialising the component count of `kruskal` to 0 instead of `n` (it is overwritten from `d.components()` after the loop); ending an edge's interval one time later (`t + 1`): time `t` is the removal operation itself, which is not a query, so no answer sees the edge; starting it one time later (`added + 1`): time `added` is the addition itself, also not a query. Every other change was caught by a test.

## 7. Questions to answer out loud

1. **What does a DSU do and how is it stored?** Maintains disjoint sets under `find` and `unite`; one parent array, the root names the set.
2. **Why union by size?** A node's depth grows only when its set at least doubles, so depth is at most log2 n.
3. **What is path halving?** In `find`, point each visited node at its grandparent; it flattens the tree with no stack and no second pass.
4. **What is the cost with both?** O(alpha(n)) amortized, alpha the inverse Ackermann function (Tarjan, from memory), at most 4 for any real n.
5. **How do you detect a cycle?** `unite` returns false when the two ends were already connected.
6. **Why is Kruskal right?** The cheapest edge leaving a set is safe (cut property); scanning by weight, an edge between two sets is the cheapest such edge remaining.
7. **How do you answer "a - b = d" queries?** Store `diff` to the parent; `find` adds the differences up while flattening; `add` on connected nodes is a consistency check.
8. **Why does the rollback version drop compression?** Compression changes many parents; with size only, each union is one assignment on one root, undone by three assignments.
9. **How does offline dynamic connectivity work?** Each edge lives in an interval of time; put it in O(log T) nodes of a segment tree over time; depth-first walk with unite on entry and rollback on exit.

## 8. Problems

**A.** Run `DsuT<true, false>` on 8 elements with the unions `(0,1), (2,3), (0,2), (4,5), (6,7), (4,6), (0,4)` (ties hang the first root under the second). Give the final parent array and the depth of node 0.

**B.** On 10 elements apply `(0,1), (2,3), (1,3), (0,2), (4,5), (6,6)` with `Dsu`. Which calls return false and how many components remain?

**C.** Run Kruskal by hand on the 7-node example of section 3. Give the order in which the edges are examined, which are kept, the weight, and the number of components.

**D.** `add(0,1,3)` and `add(1,2,4)` have been applied to a weighted DSU. What is `query(0,2)`? Is `add(0,2,8)` consistent? What is `query(2,0)`?

**E.** Why can a node's depth grow at most log2 n times under union by size? Why does this fail for "always hang the first root under the second"?

**F.** On 4 nodes the operations are `add(0,1), add(1,2), query(0,2), remove(1,2), query(0,2), add(2,3), query(0,3), add(1,2), query(0,3)`. Give the interval of every edge and the four answers.

**G.** A friend adds path compression to the rollback DSU "to make it faster". What goes wrong?

**H.** Choose a method: (a) the number of connected components after a stream of edges; (b) the first edge that makes a cycle; (c) a minimum spanning tree of a sparse graph; (d) equations `a / b = r` and queries; (e) connectivity queries while edges are added and deleted, all given in advance; (f) the shortest path between two nodes.

### Answers

**A.** The final parent array is `1 3 3 7 5 7 7 7` (see the trace in section 1). Node 0 is at depth 3: 0 to 1 to 3 to 7.

**B.** `(0,1)` merges, `(2,3)` merges, `(1,3)` merges (sets {0,1} and {2,3}), `(0,2)` returns **false** (already joined), `(4,5)` merges, `(6,6)` returns **false** (an element is always in its own set). Four successful unions, so 10 - 4 = **6** components.

**C.** Order: edge 6 (4-5, weight 1), 2 (1-2, weight 2), 1 (0-2, weight 3), 0 (0-1, weight 4), 3 (1-3, weight 5), 5 (3-4, weight 6), 4 (2-3, weight 7), 7 (3-5, weight 8). Kept: 6, 2, 1, 3, 5. Edges 0, 4 and 7 each join two nodes that are already connected. Weight 17, **2** components (node 6 is alone).

**D.** `query(0,2)` is 3 + 4 = 7. `add(0,2,8)` contradicts it (7 is known) and returns false; `query(2,0)` is -7.

**E.** Count how often one node's depth can grow: it grows by one only when its root is hung under another root, which happens only if its set is not larger than the other one, so after the merge the set has at least twice as many elements. The size is at most n, so the depth grows at most log2 n times. With "always hang the first root under the second" there is no size comparison: `unite(0,1), unite(1,2), ...` makes the chain `1 2 3 4 5 5` and a find from 0 walks the whole chain.

**F.** Edge `(0,1)`: `[0, 9)`. Edge `(1,2)`: `[1, 3)` and `[7, 9)`. Edge `(2,3)`: `[5, 9)`. The answers at times 2, 4, 6, 8 are `1 0 0 1`: at time 2 the path 0, 1, 2 exists; at time 4 the edge `(1,2)` is gone, so 0 and 2 are apart; at time 6 it is still gone, so 0 and 3 are apart; at time 8 all edges are present.

**G.** A compressed `find` rewrites the parents of nodes that are not roots, and none of that is recorded on the stack. After `rollback` the forest no longer matches the state at the snapshot: the node that was undone may still be reachable through rewritten pointers, so `same` returns wrong answers. (This is a design reason, which the tests do not need to show.)

**H.** (a) Plain DSU, `components()`. (b) Plain DSU, the first `unite` that returns false. (c) Kruskal. (d) Weighted DSU, with products or logarithms. (e) Rollback DSU plus a segment tree over time. (f) Not a DSU: a graph search (breadth first, or Dijkstra with weights).

---

## Cheat sheet

- **Structure:** one `parent` array; a root is its own parent and names the set; `sz` valid only at roots.
- **Union by size:** hang the smaller root under the larger; depth at most log2 n.
- **Path halving:** in `find`, `parent[x] = parent[parent[x]]`; no recursion.
- **Both:** O(alpha(n)) amortized (Tarjan, from memory). Neither: quadratic on random unions (15 s at 10^5).
- **Cycle:** `unite` returned false. **Components:** `comps`.
- **Kruskal:** sort edges, `unite`, keep the ones that merge; cut property; forest if disconnected.
- **Weighted:** `diff[x] = val[x] - val[parent[x]]`; fix `diff` from the root side down; contradiction = connected and different.
- **Rollback:** size only, stack of (child root, parent root), undo by three assignments; offline connectivity = intervals in a segment tree over time + DFS with rollback.
- **Measured (10^6 nodes, random unions):** both heuristics 55 ms against 111 (size only) and 171 to 181 (halving only); the chain build is 2 to 5 ms in every variant but 100 finds cost 186 ms with no heuristic and 0.001 ms with the size rule.

---

## Sources

- The inverse-Ackermann analysis is Tarjan's (1975, named from memory, not re-read); the cut property and Kruskal's algorithm are in CLRS (from memory) and in the cp-algorithms articles on DSU and on the minimum spanning tree (named from memory, not re-read for this file). The code is my own implementation, tested as above. The offline connectivity trick (segment tree over time with rollback) is a standard competitive-programming technique, described in cp-algorithms (from memory).
- The claims about union by rank, Prim's algorithm, link-cut trees and Euler tour trees, and the size of alpha are from general knowledge and were not measured or implemented here. All timings are measured here.
- Problems by name, from memory: LeetCode Number of Provinces, Accounts Merge, Evaluate Division, Redundant Connection, Satisfiability of Equality Equations, Number of Islands II, Min Cost to Connect All Points.
