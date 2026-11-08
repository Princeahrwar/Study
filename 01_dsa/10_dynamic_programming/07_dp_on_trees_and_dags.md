# DSA 10.07: DP on Trees and DAGs

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [DP fundamentals](01_dp_fundamentals.md) (state, transition, the fill order, recursion depth) and the trees area ([tree traversals](../07_trees/02_iterative_traversals_and_morris.md) and [bottom-up recursion](../07_trees/04_bottom_up_recursion.md)). There is no companion demo file: every structure is listed in this doc

Every DP so far had a state that was a number, a prefix, a range or a set. The state can also be a **node of a graph**. If the graph is a tree, the subproblem for a node is its **subtree**, and the subproblems depend on each other exactly as the tree says: a node needs its children. If the graph is a **DAG** (a directed graph with no cycle), the subproblem for a node needs the nodes that point to it, and the fill order is a **topological order**. Both are the same idea as before, "fill the table so that every cell's inputs are ready"; the structure now comes from the input instead of from loop counters.

Three things to learn:

1. **Tree DP.** Root the tree, let `f(v)` describe the subtree of `v`, and build `f(v)` from the `f` of its children. The difficult step is choosing what `f(v)` stores: usually a *few* numbers, one per situation the parent might need ("v is taken", "v is not taken"; "v has a camera", "v is watched by a child", "v is not watched yet").
2. **Rerooting.** When the answer is wanted for *every* node as the root, do not run the tree DP `n` times. Compute it for one root, then move the root along the edges, fixing the answer in constant time per move.
3. **DP over a topological order.** Longest path, counting paths, and the longest increasing path in a matrix are all one pass over a DAG in the right order.

By the end you can:

- compute a tree DP without recursion, from an explicit order, and say why that matters on a path-shaped tree;
- design the per-node states for house robber III, tree cameras and the diameter, and justify why each state exists;
- derive a rerooting formula such as `ans[child] = ans[parent] - size + (n - size)` and the "best of the others" trick with the two largest child values;
- order a DAG with Kahn's method, run a longest-path or path-counting DP on it, detect a cycle, and see a matrix as an implicit DAG;
- recognise which of these a problem is.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 59,613 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: house robber against every set of non-adjacent nodes; cameras against every set of nodes; the diameter, the sum of distances and the farthest-node distances against a breadth-first search from every node; each tree answer again after the node labels are shuffled (the code roots the tree at node 0, so the answer must not depend on which node that is); trees of five shapes (random, path, star, caterpillar, binary), including a path of 300,001 nodes where a recursive version would need that many stack frames; unique search trees against the plain recursion, the Catalan recurrence and the closed form; DAGs built on a hidden random order, so the node numbers are not already topological, against an exhaustive search of paths, with the returned order checked edge by edge and a cycle added to check that it is detected; counting paths on a lattice against the binomial coefficient; and the longest increasing path against a plain memoised search. Then 66 hand-made mutants of the listings were run against the tests: **61 were caught and 5 are equivalent changes**; section 7 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Tree DP: robber, cameras and the diameter

**Rooting without recursion.** A tree has no direction until you pick a root. Node 0 is the root here. A recursive solution calls itself on each child and combines the answers on the way back, which needs one stack frame per level of the tree. A tree shaped like a path has `n` levels, and a million frames is more than a default stack holds. So compute an **order** once with an explicit stack: `order` lists the nodes so that each node comes after its parent (a preorder), and `parent[v]` is remembered. Walking `order` **backwards** visits every node after all of its children, which is what a bottom-up DP needs. The tests include a path of 300,001 nodes.

**House robber III.** Each node holds money; you cannot take two nodes that share an edge. Find the most you can take. For a node `v` the parent only needs to know two things about `v`'s subtree: the best total if `v` is taken, and the best total if it is not.

```
in[v]  = money[v] + sum over children c of out[c]            (v taken, so no child is)
out[v] = sum over children c of max(in[c], out[c])           (v free, each child decides)
answer = max(in[root], out[root])
```

**Binary tree cameras.** A camera on a node watches the node, its parent and its children. Place the fewest cameras so that every node is watched. Two states are not enough: whether `v` is watched depends on a child, and whether the *parent* must act depends on `v`. Three states per node:

```
cam[v]  = a camera on v:                  1 + sum over children of min(cam, seen, bare)
seen[v] = no camera on v, v watched by a child:   at least one child has a camera:
          sum over children of min(cam, seen)  +  the smallest extra cost to force one child to cam
bare[v] = no camera on v and v not watched yet:   every child is watched without v:
          sum over children of seen[c]            (the parent of v must have a camera)
answer  = min(cam[root], seen[root])           (a bare root would be unwatched)
```

A leaf has `cam = 1`, `bare = 0` and no way to be `seen` (infinity). In the `cam` line a child may be in any state, because `v`'s own camera watches it.

**Diameter.** The longest path between two nodes, in edges. Every path has a highest node `v`, and it goes down through two different children (or one, or none). Let `down[v]` be the longest path that goes straight down from `v`. Then the best path with `v` highest is the sum of the two largest values of `down[c] + 1` over the children `c`, and the diameter is the maximum over all `v`. The same pass gives `down`, so the time is `O(n)`.

**Unique binary search trees.** The number of binary search trees on keys 1 to `n`: the root is key `r`, the left subtree uses the `r - 1` smaller keys and the right the `n - r` larger ones, and the two sides are chosen independently: `f(n) = sum over r of f(r-1) * f(n-r)`. These are the Catalan numbers. It is a DP over the *shape* of a tree rather than over a given tree.

```cpp
// DP on a tree: root the tree at node 0 and let f(v) describe the SUBTREE of v.  A node's value is built from its children's values, so
// children are processed before parents.  Recursion would be n frames deep on a path-shaped tree, so the order is computed once with an
// explicit stack: `order` lists every node with its parent before it (a preorder), and walking `order` backwards visits children first.
void root_tree(const Adj& adj, std::vector<int>& parent, std::vector<int>& order) {
    int n = (int)adj.size(); parent.assign(n, -1); order.clear(); if (n == 0) return;
    std::vector<int> st; st.push_back(0);
    while (!st.empty()) {
        int v = st.back(); st.pop_back(); order.push_back(v);
        for (int c : adj[v]) if (c != parent[v]) { parent[c] = v; st.push_back(c); }
    }
}
// House robber III: nodes hold money, you cannot take two nodes joined by an edge; the most you can take.  Two numbers per node:
// in[v] = best in v's subtree when v IS taken (so its children are not), out[v] = best when v is NOT taken (each child free to be either).
ll rob_tree(const Adj& adj, const std::vector<ll>& val) {
    int n = (int)adj.size(); if (n == 0) return 0; std::vector<int> par, order; root_tree(adj, par, order);
    std::vector<ll> in(n), out(n);
    for (int i = n - 1; i >= 0; --i) {
        int v = order[i]; in[v] = val[v]; out[v] = 0;
        for (int c : adj[v]) if (c != par[v]) { in[v] += out[c]; out[v] += std::max(in[c], out[c]); }
    }
    return std::max(in[0], out[0]);
}
// Binary tree cameras: a camera on a node watches the node, its parent and its children.  The fewest cameras so that every node is
// watched.  Three states per node: cam = a camera on v; seen = no camera on v, but a child has one; bare = no camera on v and v is not
// yet watched (every child is watched without help from v, so v's PARENT must have a camera).  seen needs at least one child with a camera.
ll min_cameras(const Adj& adj) {
    int n = (int)adj.size(); if (n == 0) return 0; const ll INF = 1000000000000000LL; std::vector<int> par, order; root_tree(adj, par, order);
    std::vector<ll> cam(n), seen(n), bare(n);
    for (int i = n - 1; i >= 0; --i) {
        int v = order[i]; ll c = 1, sumFree = 0, bareSum = 0, extra = INF; bool leaf = true;
        for (int u : adj[v]) if (u != par[v]) {
            leaf = false; c += std::min({cam[u], seen[u], bare[u]});
            ll free = std::min(cam[u], seen[u]); sumFree += free; extra = std::min(extra, cam[u] - free); bareSum = std::min(INF, bareSum + seen[u]);
        }
        cam[v] = c; seen[v] = leaf ? INF : sumFree + extra; bare[v] = leaf ? 0 : bareSum;
    }
    return std::min(cam[0], seen[0]);
}
// Diameter (the longest path, counted in edges).  down[v] = the longest path going straight down from v.  A longest path has a highest
// node v; it goes down two different children, so through v the best is the sum of the two largest values of down[child] + 1.
int tree_diameter(const Adj& adj) {
    int n = (int)adj.size(); if (n <= 1) return 0; std::vector<int> par, order; root_tree(adj, par, order);
    std::vector<int> down(n, 0); int best = 0;
    for (int i = n - 1; i >= 0; --i) {
        int v = order[i], m1 = 0, m2 = 0;
        for (int c : adj[v]) if (c != par[v]) { int h = down[c] + 1; if (h > m1) { m2 = m1; m1 = h; } else if (h > m2) m2 = h; }
        down[v] = m1; best = std::max(best, m1 + m2);
    }
    return best;
}
// Unique binary search trees on keys 1..n (Catalan numbers): the root is key r, the left part has r-1 keys and the right n-r.
ll unique_bsts(int n) {
    std::vector<ll> f(n + 2, 0); f[0] = 1;
    for (int m = 1; m <= n; ++m) for (int r = 1; r <= m; ++r) f[m] += f[r - 1] * f[m - r];
    return f[n];
}
```

```
house robber III on the tree 0:{1,2} 1:{3,4} 2:{5,6} with money 3 4 5 1 3 1 1; in = v taken, out = v not taken; children first
  node 3 (money 1): in=1 out=0
  node 4 (money 3): in=3 out=0
  node 1 (money 4): in=4 out=4
  node 5 (money 1): in=1 out=0
  node 6 (money 1): in=1 out=0
  node 2 (money 5): in=5 out=2
  node 0 (money 3): in=9 out=9
best=9 (code gives 9)
```

```
cameras on the same tree; cam = camera here, seen = a child has one, bare = not watched yet (the parent must have one)
  node 3: cam=1 seen=inf bare=0
  node 4: cam=1 seen=inf bare=0
  node 1: cam=1 seen=2 bare=inf
  node 5: cam=1 seen=inf bare=0
  node 6: cam=1 seen=inf bare=0
  node 2: cam=1 seen=2 bare=inf
  node 0: cam=3 seen=2 bare=4
best=2 (code gives 2)
```

```
diameter of the same tree; down = longest path straight down, through = the two best child paths joined at v
  node 3: down=0 through=0
  node 4: down=0 through=0
  node 1: down=1 through=2
  node 5: down=0 through=0
  node 6: down=0 through=0
  node 2: down=1 through=2
  node 0: down=2 through=4
diameter=4 edges (code gives 4)
```

In the robber trace the root is a tie, 9 either way: taking the root (3) plus what is left below it (4 from `out` of node 1 and 2 from `out` of node 2), or leaving it and taking the better of each child (4 from node 1, 5 from node 2). The camera trace shows the three states clearly: a leaf has `seen = inf`, a node with two leaf children has `cam = 1` and `seen = 2` (one camera on a child), and its `bare` is infinite because a bare node needs all its children `seen`, which leaves cannot be. The root needs 2 cameras: one on each of nodes 1 and 2. In the diameter trace, node 0 joins two paths of length 2 into a path of 4 edges.

Measured. On trees of 1,000,000 nodes (random shape, a path, a star), house robber took 109 / 123 ms on the random tree, 30 / 29 on the path and 26 / 24 on the star; the cameras 99 / 132, 19 / 16 and 18 / 15 ms; the diameter 71 / 99, 10 / 10 and 11 / 11 ms. The path needed 333,334 cameras (one per three nodes, rounded up), had diameter 999,999, and the star needed 1 camera and had diameter 2. The random tree's diameter was 66. The number of unique search trees on 5, 10, 15 and 19 keys is 42, 16,796, 9,694,845 and 1,767,263,190, from a table of 190 products for 19 keys.

## 2. Rerooting: the answer for every node as the root

**Sum of distances.** For every node `v`, find the sum of the distances from `v` to all the other nodes. Doing a search from each node costs `O(n^2)`. Rerooting does it in `O(n)`.

First compute, for root 0, `cnt[v]` (the size of the subtree of `v`) and `down[v]` (the sum of the distances from `v` to the nodes below it): `down[p] += down[v] + cnt[v]`, since every node below `v` is one step further from `p`. Then `ans[0] = down[0]`. Now move the root from a node `v` to its child `c`. The `cnt[c]` nodes in `c`'s subtree are each one step *closer*; the other `n - cnt[c]` nodes are each one step *further*:

```
ans[c] = ans[v] - cnt[c] + (n - cnt[c])
```

This needs only the answer of the parent, so a single pass in `order` (parents first) fills everything.

**Farthest node from each node.** For each node, the length of the longest path starting there. The longest path from `v` goes either down into the subtree, which `down[v]` gives, or **up** through the parent and then anywhere else. Define `up[c]` as the longest path from `c` that starts by stepping to its parent `v`. From `v` it can continue up (`up[v]`) or go down into a *different* child of `v` than `c`. The second option is the longest downward path of `v` not through `c`: keep the two largest child heights `m1 >= m2` of `v`, and use `m2` when `c` is the child that gave `m1`, and `m1` otherwise:

```
up[c]  = 1 + max( up[v],  (down[c] + 1 == m1[v]) ? m2[v] : m1[v] )
answer = max( down[v], up[v] )
```

When two children tie for the largest height, `m2 == m1`, so the comparison still works.

```cpp
// Rerooting: the answer is wanted for EVERY node as the root.  Computing each from scratch costs n times as much.  Instead compute the
// answer for root 0 bottom-up, then derive each child's answer from its parent's in one top-down pass.
// Sum of distances from each node to all the others.  cnt[v] = size of v's subtree; down[v] = sum of distances from v to the nodes below
// it.  Moving the root from v to its child c brings the cnt[c] nodes of c's subtree one step closer and the other n - cnt[c] one step
// further: ans[c] = ans[v] - cnt[c] + (n - cnt[c]).
std::vector<ll> sum_distances(const Adj& adj) {
    int n = (int)adj.size(); std::vector<ll> ans(n, 0); if (n == 0) return ans; std::vector<int> par, order; root_tree(adj, par, order);
    std::vector<ll> cnt(n, 1), down(n, 0);
    for (int i = n - 1; i > 0; --i) { int v = order[i], p = par[v]; cnt[p] += cnt[v]; down[p] += down[v] + cnt[v]; }
    ans[0] = down[0];
    for (int i = 1; i < n; ++i) { int v = order[i]; ans[v] = ans[par[v]] - cnt[v] + (n - cnt[v]); }
    return ans;
}
// The farthest node from each node (its eccentricity).  down[v] = height below v.  up[c] = the longest path from c that STARTS by going to
// its parent v; it is 1 + max(up[v], the longest downward path of v through a child other than c).  Keeping v's two best child heights
// answers "other than c" in O(1).
std::vector<int> eccentricities(const Adj& adj) {
    int n = (int)adj.size(); std::vector<int> res(n, 0); if (n <= 1) return res; std::vector<int> par, order; root_tree(adj, par, order);
    std::vector<int> down(n, 0), up(n, 0), m1(n, 0), m2(n, 0);
    for (int i = n - 1; i > 0; --i) {
        int v = order[i], p = par[v], h = down[v] + 1;
        if (h > m1[p]) { m2[p] = m1[p]; m1[p] = h; } else if (h > m2[p]) m2[p] = h;
        down[p] = m1[p];
    }
    for (int i = 1; i < n; ++i) { int c = order[i], v = par[c]; int other = (down[c] + 1 == m1[v]) ? m2[v] : m1[v]; up[c] = 1 + std::max(up[v], other); }
    for (int v = 0; v < n; ++v) res[v] = std::max(down[v], up[v]);
    return res;
}
```

```
sum of distances on the tree 0-1 0-2 2-3 2-4 2-5, rooted at 0
  node 0: subtree size=6 distances below=8
  node 1: subtree size=1 distances below=0
  node 2: subtree size=4 distances below=3
  node 3: subtree size=1 distances below=0
  node 4: subtree size=1 distances below=0
  node 5: subtree size=1 distances below=0
  root 0: answer=8
  node 2 from its parent 0: 8 - 4 + 2 = 6
  node 5 from its parent 2: 6 - 1 + 5 = 10
  node 4 from its parent 2: 6 - 1 + 5 = 10
  node 3 from its parent 2: 6 - 1 + 5 = 10
  node 1 from its parent 0: 8 - 1 + 5 = 12
answers by node: 8 12 6 10 10 10 (code gives 8 12 6 10 10 10)
```

The trace moves the root across the tree `0-1, 0-2, 2-3, 2-4, 2-5`. From node 0 (answer 8) to node 2: the 4 nodes in node 2's subtree get closer (minus 4) and the 2 others further (plus 2), giving 6. From node 2 to the leaf 5: minus 1 plus 5 gives 10, the same for leaves 3 and 4. The answers `8 12 6 10 10 10` are the known sample for this tree.

Measured. On a random tree of 5000 nodes, rerooting took 0.10 / 0.12 ms and 5000 breadth-first searches took 250 / 253 ms, and the sums were identical (the sum for node 0 was 38,823). The farthest-node distances for all 5000 nodes took 0.17 / 0.15 ms and matched the searches. On a path of 1,000,000 nodes the sum of distances from the end is 499,999,500,000, which is `n (n - 1) / 2`; for the star it is 999,999.

## 3. DP over a DAG

**The topological order.** In a DAG, the answer for a node depends on the nodes that have edges into it, and a cycle would make the dependence circular, so the DP is only defined on a DAG. **Kahn's method** finds an order: keep, for each node, the count of incoming edges not yet processed; start with the nodes of count 0; take one, and for each outgoing edge reduce its target's count, adding the target when it reaches 0. If the order ends with fewer than `n` nodes, the rest lie on or behind a cycle. The time is `O(n + m)`.

**Longest path.** The heaviest total of edge weights along a path. `best[v]` = the heaviest path *ending* at `v`. In topological order, when `v` is reached its `best[v]` is final (all predecessors came first), so push it along each outgoing edge: `best[w] = max(best[w], best[v] + weight)`. The answer is the maximum `best`. On a graph with a cycle there is no longest path in general, so the code reports it as an error.

**Counting paths.** The number of different paths from `s` to `t`, modulo `10^9 + 7`. `ways[s] = 1` and `ways[w] += ways[v]` along each edge, in topological order. Two parallel edges between the same nodes count as two paths.

**A matrix is a DAG.** In the longest increasing path of a matrix each step goes to a neighbour with a strictly larger value, so the "graph" on the cells has an edge from each cell to every larger neighbour; it cannot have a cycle because the values strictly increase. A topological order is simply the cells **sorted by value**. Then `best[c] = 1 + max best[neighbour with a smaller value]`, and the smaller neighbours come earlier in the sorted order. This avoids the memoised recursion (and its depth) altogether. It is the same problem as [BFS on states and grids](../08_graphs/04_bfs_on_states_and_grids.md) from the graph side, solved by a DP order instead.

```cpp
// DP on a DAG: the subproblems are the nodes and the edges say which subproblem needs which, so the fill order is a TOPOLOGICAL order.
// Kahn's method: repeatedly take a node with no unprocessed predecessors.  It finishes only if there is no cycle.
struct Edge { int u, v; ll w; };
bool topo_order(int n, const std::vector<Edge>& e, std::vector<int>& order) {
    std::vector<int> indeg(n, 0); std::vector<std::vector<int>> out(n);
    for (size_t i = 0; i < e.size(); ++i) { out[e[i].u].push_back((int)i); ++indeg[e[i].v]; }
    order.clear(); std::queue<int> q; for (int v = 0; v < n; ++v) if (!indeg[v]) q.push(v);
    while (!q.empty()) { int v = q.front(); q.pop(); order.push_back(v); for (int i : out[v]) if (--indeg[e[i].v] == 0) q.push(e[i].v); }
    return (int)order.size() == n;
}
// Longest path in a DAG (the largest total edge weight over all paths, including the empty path at one node).  best[v] = the heaviest
// path ENDING at v; in topological order, best[v] is final before it is pushed along v's out-edges.  False if there is a cycle.
bool longest_path_dag(int n, const std::vector<Edge>& e, ll& result) {
    std::vector<int> order; if (!topo_order(n, e, order)) return false;
    std::vector<std::vector<int>> out(n); for (size_t i = 0; i < e.size(); ++i) out[e[i].u].push_back((int)i);
    std::vector<ll> best(n, 0); result = 0;
    for (int v : order) { result = std::max(result, best[v]); for (int i : out[v]) best[e[i].v] = std::max(best[e[i].v], best[v] + e[i].w); }
    return true;
}
// Number of paths from s to t in a DAG, modulo MOD.  ways[v] = paths from s to v; a node is final once all its predecessors are done.
// Returns -1 when the graph has a cycle.
ll count_paths_dag(int n, const std::vector<Edge>& e, int s, int t) {
    std::vector<int> order; if (!topo_order(n, e, order)) return -1;
    std::vector<std::vector<int>> out(n); for (size_t i = 0; i < e.size(); ++i) out[e[i].u].push_back((int)i);
    std::vector<ll> ways(n, 0); ways[s] = 1;
    for (int v : order) if (ways[v]) for (int i : out[v]) ways[e[i].v] = (ways[e[i].v] + ways[v]) % MOD;
    return ways[t];
}
// Longest increasing path in a matrix (each step to a neighbour with a strictly larger value), counted in cells.  The "graph" has an edge
// from every cell to each larger neighbour, so it is a DAG whose topological order is simply the cells sorted by value: best[c] =
// 1 + the best of the smaller neighbours, and the smaller neighbours are earlier in the sorted order.
int longest_increasing_path(const std::vector<std::vector<int>>& g) {
    int R = (int)g.size(); if (R == 0) return 0; int C = (int)g[0].size(); if (C == 0) return 0;
    std::vector<int> cells(R * C); for (int i = 0; i < R * C; ++i) cells[i] = i;
    std::sort(cells.begin(), cells.end(), [&](int a, int b) { return g[a / C][a % C] < g[b / C][b % C]; });
    std::vector<int> best(R * C, 1); int ans = 1; const int dr[4] = {1, -1, 0, 0}, dc[4] = {0, 0, 1, -1};
    for (int id : cells) {
        int r = id / C, c = id % C;
        for (int d = 0; d < 4; ++d) { int nr = r + dr[d], nc = c + dc[d]; if (nr >= 0 && nr < R && nc >= 0 && nc < C && g[nr][nc] < g[r][c]) best[id] = std::max(best[id], best[nr * C + nc] + 1); }
        ans = std::max(ans, best[id]);
    }
    return ans;
}
```

```
longest path in a DAG with edges 0>1 (3) 0>2 (2) 1>3 (4) 2>3 (6) 3>4 (1) 2>4 (5) 5>2 (1)
topological order: 0 5 1 2 3 4
  after node 0: best ending at each node = 0 3 2 0 0 0
  after node 5: best ending at each node = 0 3 2 0 0 0
  after node 1: best ending at each node = 0 3 2 7 0 0
  after node 2: best ending at each node = 0 3 2 8 7 0
  after node 3: best ending at each node = 0 3 2 8 9 0
  after node 4: best ending at each node = 0 3 2 8 9 0
longest=9 (code gives 9)
```

```
longest increasing path in 9 9 4 / 6 6 8 / 2 1 1: cells in increasing order of value, best = longest path ending there
  cell (2,1) value 1: best=1
  cell (2,2) value 1: best=1
  cell (2,0) value 2: best=2
  cell (0,2) value 4: best=1
  cell (1,0) value 6: best=3
  cell (1,1) value 6: best=2
  cell (1,2) value 8: best=3
  cell (0,0) value 9: best=4
  cell (0,1) value 9: best=3
longest=4 (code gives 4)
```

In the first trace the order `0 5 1 2 3 4` is one of several valid ones; node 5 has no incoming edge, so it can appear anywhere before node 2. The longest path is `0 > 2 > 3 > 4` with 2 + 6 + 1 = 9; the path `5 > 2 > 3 > 4` is one short at 1 + 6 + 1 = 8, and the final row of the trace shows 9 ending at node 4. The second trace sorts the cells; the cell `(0,0)` of value 9 has best 4 through `1 > 2 > 6 > 9`, the path `(2,1) > (2,0) > (1,0) > (0,0)`. A strictly increasing path cannot repeat a value, so the two 9s do not chain.

Measured. A random DAG of 1,000,000 nodes and 3,000,000 edges (built on a hidden random order) was sorted and its longest path of 43,165 found in 1117 / 1087 ms. The longest increasing path of a random 1000 by 1000 matrix (1,000,000 cells) took 268 / 288 ms, and its length was 14. On the lattice DAG of right and down moves over a 1,000 by 1,000 grid (a million nodes), counting the paths from one corner to the other took 191 / 181 ms and gave 965,601,742 modulo the prime, equal to the binomial `C(1998, 999)` modulo the prime (checked by the verification script). On a 30 by 30 lattice the count is 289,070,178, equal to `C(58, 29)` modulo the prime.

## 4. Recognising the shape

| Signal in the statement | State | Order | Time |
|---|---|---|---|
| Choose nodes of a tree with a rule about neighbours | per node: taken or not (or a few more modes) | children before parents | O(n) |
| Cover or watch every node with the fewest items | per node: item here, watched from below, not yet watched | children before parents | O(n) |
| Longest or best path in a tree | `down[v]` plus the two best children at the top | children before parents | O(n) |
| The same answer for every root | one root's answer, then edge-by-edge fixes with subtree sizes or the top two child values | parents first | O(n) |
| Longest, shortest or counted paths in a directed acyclic graph | per node: best or count ending here | topological order | O(n + m) |
| Steps only to a strictly larger value | the matrix as an implicit DAG | cells sorted by value | O(RC log RC) |
| Count the shapes of trees | per size: choose the root, split the rest | increasing size | O(n^2) |

## 5. Why shortcuts fail here

Each problem has a plausible shortcut. Measured on 100,000 random small instances (trees or DAGs of 2 to 11 nodes), a rule is "wrong" when its answer differs from the DP's:

- **House robber: take the richer of the even-depth and the odd-depth levels:** wrong on 37,541 (37.5%). A level rule ignores that a poor node can sit between rich ones.
- **Cameras: put one on every internal node:** wrong on 48,817 (48.8%). It over-counts whenever one camera could watch two neighbours.
- **Diameter: join the two tallest branches of the root only:** wrong on 3,631 (3.6%). The longest path need not pass through the root. Node 0 of these random trees is often near the middle, so the rate is low, but it is not zero.
- **Longest path in a DAG: from each node follow the heaviest outgoing edge:** wrong on 51,023 (51.0%). A heavy edge can lead to a dead end.

## 6. Testing a tree or DAG DP

1. **A different method for every table.** All sets of nodes for the robber and the cameras (so the brute force never sees a state), a search from every node for distances, and an exhaustive walk of the paths for the DAG.
2. **Relabel the nodes.** The code roots the tree at node 0. Shuffling the labels changes the root and the child order, and every answer must move with the labels. This catches a DP that depends on the root or on the order of children.
3. **Shapes.** A path, a star, a caterpillar and a binary tree behave very differently: a star has one node with `n - 1` children (it stresses the "top two" logic and overflow of infinite sums), a path has depth `n`.
4. **Known counts.** Cameras on a path of `n` nodes need `ceil(n/3)`; the sum of distances on a path is `n (n - 1) / 2` from the end; lattice paths are binomials; unique search trees are the Catalan numbers.
5. **A cycle on purpose.** Add two edges that close a cycle and check that the order fails and the DP reports it.
6. **Pins:** an empty tree, one node, a star whose centre is worth exactly the sum of its leaves, parallel edges, and a DAG with no edges.

## 7. What the tests check, and what they cannot

Of the 66 mutants, 61 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Five survive, and each leaves every output the tests can see unchanged. No pin was needed to kill any of the others. The reasons the five stay:

- **Setting `parent[0] = 0` for the root.** The code only compares a neighbour with the parent to skip the way back; no node is its own neighbour, so a root whose "parent" is itself skips nothing extra.
- **In the `bare` state, letting a child be `cam` as well as `seen` (`min(seen, cam)`).** Such a node is in fact watched by its child, which is the `seen` case, and `seen` is already in the minimum. When no child prefers a camera the two sums are the same; when one does, the sum equals `seen[v]` exactly, so the answer cannot change.
- **Letting an in-degree go to zero or below (`<= 0` instead of `== 0`).** Each edge lowers its target's in-degree once, so the count reaches 0 exactly once and never goes below it.
- **Not pushing the path count along the out-edges of the target `t`.** A path from `s` to `t` that continued past `t` could never return to `t` in a DAG, so those edges cannot add to `ways[t]`.
- **Starting the best length of the matrix path at 0 instead of 1.** The first cell processed sets it to at least 1 anyway.

The large inputs have no brute-force reference; they are checked against independent computations (the formula for a path, a star, the binomial for the lattice). What the tests cannot do is prove that the states model the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass.

Mutants are chosen where a bug is plausible, not at random, so "61 of 66" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 8. Common mistakes

1. **Recursing on a deep tree** and overflowing the stack; compute an order and walk it backwards.
2. **Visiting the parent again:** in an undirected adjacency list every edge appears twice, so skip the parent (`c != parent[v]`).
3. **Storing too few states:** robber needs "taken" and "not taken", cameras need three.
4. **Mixing up the camera states:** `seen` needs a child with a camera, `bare` forbids it; they answer different questions for the parent.
5. **Adding infinity to infinity** in the camera sums (a leaf cannot be `seen`); cap the sums.
6. **Accepting `bare` at the root:** the root has no parent to watch it, so only `cam` and `seen` are allowed.
7. **The diameter through the wrong node:** the best path has a highest node; take the maximum over all nodes, not only the root.
8. **Using only the best child for the farthest node** and missing the second best when the first child is the one you came from.
9. **Rerooting in the wrong order:** the parent's answer must be ready, so process `order` forwards for the top-down pass and backwards for the bottom-up pass.
10. **Computing the top-down term with `n - cnt`** but `cnt` from a different root.
11. **Running a DAG DP in node-number order** instead of topological order; the numbers are only a topological order if the edges happen to go upward.
12. **Ignoring a cycle:** Kahn's method returns fewer than `n` nodes; check it.
13. **Counting paths without a modulus** on a large DAG: the count grows like a binomial coefficient and overflows 64 bits.

## 9. Questions to answer out loud

1. Why compute an explicit order instead of recursing, and which direction do you walk it?
2. State `in` and `out` for the robber. Why are two numbers enough?
3. Why do the cameras need three states? What does each mean, and which one is invalid at the root?
4. How does the diameter use `down`, and why must you take the maximum over all nodes?
5. How many binary search trees have `n` keys, and what is the recurrence?
6. Derive `ans[c] = ans[v] - cnt[c] + (n - cnt[c])`.
7. Why do you need the two largest child heights to find the farthest node from every node?
8. When do two children tie, and why does the formula still work?
9. What is a topological order, and how does Kahn's method detect a cycle?
10. State the longest-path DP. Why is it final when the node is reached?
11. How do you count the paths from `s` to `t`, and what happens with parallel edges?
12. Why can the longest increasing path in a matrix be solved by sorting the cells?

## 10. Problems

### 10.1 On paper

**A.** House robber III on the tree with edges 0-1, 0-2, 1-3, 1-4, 2-5, 4-6, 4-7 and money `2 7 9 3 4 5 8 6` (node 0 first): the most you can take.

**B.** Cameras for the tree with edges 0-1, 1-2, 2-3, 2-4, 4-5, 4-6: the fewest cameras.

**C.** For the tree with edges 0-1, 0-2, 1-3, 3-4, 4-5, 2-6, 6-7: the diameter, and the farthest distance from each node.

**D.** The sum of distances from each node, for the tree with edges 0-1, 1-2, 1-3, 3-4.

**E.** The longest path in the DAG with edges (0,1,4), (0,2,1), (2,1,2), (1,3,1), (2,3,7), (3,4,2), written (from, to, weight).

**F.** The number of paths from 0 to 4 in the DAG with edges 0>1, 0>2, 1>2, 1>3, 2>3, 3>4, 2>4; and from 0 to 5 in the DAG with edges 0>1, 0>2, 1>3, 2>3, 3>4, 3>5, 4>5.

**G.** The longest increasing path of `3 4 5 / 3 2 6 / 2 2 1` and of `1 2 / 4 3`; the number of binary search trees on 5 keys.

### 10.2 On LeetCode

For each: state what `f(v)` is, which direction the order runs, and what each state means before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| House Robber III | 337 | Section 1: taken and not taken |
| Binary Tree Cameras | 968 | The three states |
| Diameter of Binary Tree | 543 | `down` and the two best children |
| Binary Tree Maximum Path Sum | 124 | The same, with values and negatives |
| Longest Path With Different Adjacent Characters | 2246 | A tree DP with a condition on the edge |
| Sum of Distances in Tree | 834 | Section 2: rerooting |
| Unique Binary Search Trees | 96 | Catalan |
| Longest Increasing Path in a Matrix | 329 | A matrix as a DAG |
| Parallel Courses III | 2050 | Longest path in a DAG |

### 10.3 In your editor

**A.** Extend the robber to return **which** nodes to take, and test the total against `rob_tree`.

**B.** Write the maximum-weight path in a tree where node values can be negative (the best path may be a single node).

**C.** Write the tree centre: the nodes with the smallest farthest distance, using the code for all farthest distances. Show there are one or two.

**D.** Write the count of **longest** paths in a DAG (how many paths reach the maximum weight).

**E.** Write the number of ways to colour a tree with `k` colours so that adjacent nodes differ, modulo the prime, and check it against `k (k - 1)^(n - 1)`.

**F.** Rewrite the longest increasing path with a memoised recursion and compare the maximum recursion depth with the sorted-cell version on a 1000 by 1000 staircase.

### Answers

**A.** The most is 30 (nodes 1, 2, 6 and 7: 7 + 9 + 8 + 6).

**B.** 3 cameras (on nodes 1, 2 and 4).

**C.** The diameter is 7 (the path `5 4 3 1 0 2 6 7`). The farthest distances for nodes 0 to 7 are `4 4 5 5 6 7 6 7`.

**D.** The sums are `8 5 8 6 9` for nodes 0 to 4.

**E.** The longest path has weight 10 (`0 > 2 > 3 > 4`: 1 + 7 + 2).

**F.** The first DAG has 5 paths and the second has 4.

**G.** The first matrix has a longest increasing path of 4 cells and the second also 4; there are 42 trees on 5 keys.

## Cheat sheet

- **Tree DP:** root at 0, compute `order` and `parent` with a stack, walk `order` **backwards**; skip the parent in the adjacency list.
- **Robber:** `in = money + sum out[c]`, `out = sum max(in[c], out[c])`.
- **Cameras:** `cam`, `seen` (needs a child with a camera), `bare` (all children `seen`); answer `min(cam, seen)` at the root; cap infinity.
- **Diameter:** `down[v]`; at each node add the two largest `down[c] + 1`; maximum over all nodes.
- **Rerooting:** sum of distances `ans[c] = ans[v] - cnt[c] + (n - cnt[c])`; farthest node `up[c] = 1 + max(up[v], best other child)` with the two largest child heights.
- **DAG:** Kahn's order (fewer than `n` nodes means a cycle); `best[w] = max(best[v] + weight)`; `ways[w] += ways[v]`.
- **Matrix as a DAG:** sort the cells by value; `best = 1 + max over smaller neighbours`.
- **Catalan:** `f(n) = sum f(r-1) f(n-r)`.
- **Shortcuts fail:** see section 5 for how often.
- **Test** by brute force over sets or paths, by relabelling, on several shapes, and with known counts.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The tree DPs, rerooting, Kahn's method and the DAG DPs are standard; the statements here are my own wording. The binomial `C(1998, 999)` and `C(58, 29)` used to check the lattice counts are computed by the verification script, not taken from a table. The claim that a deep recursion can overflow the stack is the standard one from [DP fundamentals](01_dp_fundamentals.md) and is not measured here.
- Problem numbers and titles in section 10, and the samples of problems 337, 968, 834, 329 and 96 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
