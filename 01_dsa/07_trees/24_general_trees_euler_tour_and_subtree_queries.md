# DSA 07.24: General Trees, Euler Tour and Subtree Queries

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (5 reading, 3 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md), [Iterative traversals](02_iterative_traversals_and_morris.md), [Fenwick tree](20_fenwick_tree.md) and [Segment tree I](21_segment_tree_point_update_range_query.md). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

Binary trees have two child pointers. Most trees you meet in systems and in contests do not: a process tree, a directory tree, a spanning tree of a network, an org chart, a dependency tree. They arrive as **n - 1 edges on n nodes**, with any number of children per node and sometimes no designated root. This tutorial is how to store them, walk them without recursion at 10^6 nodes, turn "subtree" questions into **range** questions with the Euler tour, and find the **diameter, centers and centroids**.

By the end of this tutorial you can:

- store a tree as compressed adjacency (CSR) arrays and say why that beats a vector of vectors;
- run a **non-recursive DFS** that fills parent, depth, entry time `tin`, exit time `tout` and the visit order, and say at what depth a recursive one dies;
- state the Euler tour property (a subtree is the contiguous range `[tin, tout)`) and use it for ancestor tests, subtree sizes, subtree sums with updates, and path-from-root sums;
- answer "sum of the subtree of `v` when the tree is rooted at `r`" without rebuilding;
- find the diameter by **two passes**, say when the argument fails, and find the **center(s)** from it;
- find the **centroid(s)** from subtree sizes and say how they differ from the centers;
- say, with measurements, what each of these costs.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 2,479,121 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: tree construction on every size from 1 to 40 with seven shapes (random, path, star, caterpillar, binary heap, broom, deep random), random labels, random roots and both unit and random weights, with `tin`, `tout`, depth, the order, subtree sizes, every subtree as a set, the children lists and **every pair of nodes** for the ancestor test checked against a walk up the parents; subtree sums, subtree adds, path sums and the re-rooted sum against brute force after every one of 120 random operations; the diameter against **all pairs** of distances, the path by walking its edges, and the centers against **minimum eccentricity**; the centroids against **removing each node and flood filling**, from two different roots; a path and a star of 300,000 nodes (deeper than a recursive DFS survives under ASan); invalid input (wrong edge count, self loops, out of range endpoints, cycles, disconnected input, multi-edges, bad roots); and a fixed tree with a negative weight where the two-pass method fails. Finally 88 hand-made mutants of the code were run against the tests (a dropped parent check, an exclusive bound turned inclusive, a swapped child, a lost reverse and so on): **82 were caught and 6 are equivalent changes** that alter neither the result nor the type of error (listed after the common mistakes).

**Data and timing.** The trees are **synthetic**: 1,000,000 nodes, labels shuffled so that neighbours are not near each other in memory. "Random" means each node attaches to a uniformly random earlier node, "path" is a single chain, "star" is one centre with 999,999 leaves. Times are milliseconds for a whole run or nanoseconds per operation on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 23), best of 3 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about systems practice are from general knowledge and say so.

---

## 1. What is a tree, as input

A **tree** on `n` nodes is a connected graph with `n - 1` edges, equivalently a graph with no cycle and `n - 1` edges, equivalently a graph where every pair of nodes has exactly one path. Two of the three properties imply the third, so a validator needs two of them: the code below checks the edge count and then checks that a traversal reaches all `n` nodes (which also catches cycles, multi-edges and isolated nodes). In fact the traversal check alone rejects every bad input, since too few edges leave nodes unreached and too many close a cycle; the count test is kept as an early, clearly worded rejection. Checking the edge count alone is not enough: a triangle plus an isolated node has `n - 1 = 3` edges.

A **rooted** tree adds a designated root; then every node except the root has one parent and zero or more children. Most tree algorithms want a rooted tree, and the input usually does not say which node is the root. Pick any (node 0, or node 1 in 1-based input) unless the problem names one.

Three representations:

| Representation | Memory per node | Notes |
|---|---|---|
| Child pointers (`Node{vector<Node*> kids}`) | pointer + vector header | allocation per node; slow to build; fine for small trees |
| `vector<vector<int>>` adjacency | 24 bytes of header + a heap block per node | the default in contest code; 10^6 small allocations |
| **CSR** (compressed sparse row): one `start` array and one `adj` array | 4 + 8 bytes per node, no allocations per node | `adj[start[v] .. start[v+1])` lists the neighbours of `v` |

CSR is built by **counting sort**: count each node's degree, prefix-sum the counts into `start`, then drop each edge into its slot. Neighbours of a node come out in input order. The cost is two passes over the edges and three arrays.

---

## 2. The rooted tree, with a DFS that cannot overflow

A recursive DFS uses one stack frame per level. On a path of 10^6 nodes that is 10^6 frames. The default stack of a Linux process is 8 MiB (`ulimit -s`), and I measured where a small recursive DFS dies: with the 8,388,608-byte limit it **survived a path of 173,000 nodes and died before 175,000** (about 48 bytes of stack per level, compiled with `-O2`; a frame with more locals dies earlier). Running a deeper DFS needs a bigger stack (a thread with a stack you size yourself, or raising the limit; from general knowledge, not measured here) or an **explicit stack**, which is what the listing below does.

The explicit stack holds the nodes of the current root-to-node path. Each node also has a **next-edge pointer** (`next[v]`, starting at `start[v]`), so the loop can resume a node's neighbour list after finishing a child. When a node has no neighbour left, it is **finished**: its exit time is recorded and it is popped.

```cpp
struct Edge { int u, v; long long w = 1; };

// A tree on nodes 0..n-1, rooted at `root`, stored as compressed adjacency (CSR).
// Everything is built with explicit stacks: no recursion, so a path of 10^6 nodes is fine.
struct RootedTree {
    int n = 0, root = 0;
    bool unit = true;                         // every edge weight is 1
    std::vector<int> start, adj;              // neighbours of v: adj[start[v] .. start[v+1])
    std::vector<long long> wt;                // wt[e] is the weight of the edge adj[e]
    std::vector<int> parent, depth;           // parent[root] = -1
    std::vector<int> tin, tout, order;        // subtree of v = order[tin[v] .. tout[v])
    std::vector<int> cstart, clist;           // children of v in increasing tin order

    RootedTree(int n_, const std::vector<Edge>& edges, int root_ = 0) : n(n_), root(root_) {
        if (n < 1) throw std::invalid_argument("RootedTree: need at least one node");
        if (root < 0 || root >= n) throw std::out_of_range("RootedTree: root");
        if (edges.size() != static_cast<std::size_t>(n - 1))
            throw std::invalid_argument("RootedTree: a tree on n nodes has n-1 edges");
        start.assign(n + 1, 0);
        for (const Edge& e : edges) {
            if (e.u < 0 || e.u >= n || e.v < 0 || e.v >= n) throw std::out_of_range("RootedTree: edge endpoint");
            if (e.u == e.v) throw std::invalid_argument("RootedTree: self loop");
            ++start[e.u + 1]; ++start[e.v + 1];
            if (e.w != 1) unit = false;
        }
        for (int v = 0; v < n; ++v) start[v + 1] += start[v];
        adj.resize(2 * edges.size()); wt.resize(2 * edges.size());
        std::vector<int> fill(start.begin(), start.end() - 1);
        for (const Edge& e : edges) {
            adj[fill[e.u]] = e.v; wt[fill[e.u]++] = e.w;
            adj[fill[e.v]] = e.u; wt[fill[e.v]++] = e.w;
        }
        parent.assign(n, -2); depth.assign(n, 0); tin.assign(n, 0); tout.assign(n, 0); order.assign(n, 0);
        std::vector<int> next(start.begin(), start.end() - 1), stack{root};
        parent[root] = -1; order[0] = root;
        int timer = 1;
        while (!stack.empty()) {
            int v = stack.back();
            if (next[v] < start[v + 1]) {
                int w = adj[next[v]++];
                if (w == parent[v]) continue;
                if (parent[w] != -2) throw std::invalid_argument("RootedTree: cycle");
                parent[w] = v; depth[w] = depth[v] + 1;
                tin[w] = timer; order[timer++] = w;
                stack.push_back(w);
            } else {
                tout[v] = timer;                  // first time stamp after the subtree
                stack.pop_back();
            }
        }
        if (timer != n) throw std::invalid_argument("RootedTree: not connected");
        cstart.assign(n + 1, 0);
        for (int t = 1; t < n; ++t) ++cstart[parent[order[t]] + 1];
        for (int v = 0; v < n; ++v) cstart[v + 1] += cstart[v];
        clist.resize(n > 0 ? n - 1 : 0);
        std::vector<int> cf(cstart.begin(), cstart.end() - 1);
        for (int t = 1; t < n; ++t) { int v = order[t]; clist[cf[parent[v]]++] = v; }
    }
    int size(int v) const { return tout[v] - tin[v]; }
    bool is_ancestor(int u, int v) const { return tin[u] <= tin[v] && tin[v] < tout[u]; }   // u == v counts
    // The child of v whose subtree contains r (r must lie strictly below v).
    int child_toward(int v, int r) const {
        if (v == r || !is_ancestor(v, r)) throw std::invalid_argument("child_toward: r is not below v");
        auto b = clist.begin() + cstart[v], e = clist.begin() + cstart[v + 1];
        auto it = std::upper_bound(b, e, tin[r], [&](int t, int c) { return t < tin[c]; });
        return *(it - 1);
    }
};
```

The two time stamps are the whole point. `tin[v]` is the number of nodes entered before `v`; `tout[v]` is the number entered when `v` finishes, so **the subtree of `v` is exactly the nodes with time stamps in `[tin[v], tout[v])`**, and `order[t]` is the node entered at time `t`. Subtree size is `tout - tin`. Every field costs 4 bytes per node, plus 16 bytes for the weights; section 8 gives the total.

For this tree (edges listed in the order `(0,1) (0,2) (1,3) (1,4) (2,5) (4,6) (4,7) (5,8) (8,9)`):

```
0
├── 1
│   ├── 3
│   └── 4
│       ├── 6
│       └── 7
└── 2
    └── 5
        └── 8
            └── 9
```

the DFS from node 0 gives (children are visited in input order):

| node | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| parent | - | 0 | 0 | 1 | 1 | 2 | 4 | 4 | 5 | 8 |
| depth | 0 | 1 | 1 | 2 | 2 | 2 | 3 | 3 | 3 | 4 |
| tin | 0 | 1 | 6 | 2 | 3 | 7 | 4 | 5 | 8 | 9 |
| tout | 10 | 6 | 10 | 3 | 6 | 10 | 5 | 6 | 10 | 10 |
| size | 10 | 5 | 4 | 1 | 3 | 3 | 1 | 1 | 2 | 1 |

and `order = 0 1 3 4 6 7 2 5 8 9`. The subtree of node 4 is `order[3..6) = 4 6 7`, and the subtree of node 2 is `order[6..10) = 2 5 8 9`.

The **children** lists (`cstart`, `clist`) hold each node's children in increasing `tin` order: node 1's children are `3 4`, node 4's are `6 7`. They are built from `order` in one pass. Having them sorted by `tin` is what makes `child_toward` (section 3) a binary search.

---

## 3. The Euler tour: subtree queries become range queries

Because the subtree of `v` is a contiguous block of the visit order, anything you can ask about a **range of an array** you can ask about a **subtree**:

- **Is `u` an ancestor of `v`?** `tin[u] <= tin[v] < tout[u]`. O(1), no walking up. (`u == v` counts.)
- **Subtree size**: `tout[v] - tin[v]`.
- **Sum (or min, max, count) over a subtree**: put the node values in an array indexed by `tin` and query the range `[tin[v], tout[v])` with a Fenwick tree (Topic 20) or a segment tree (Topics 21 and 22).
- **Add to every node of a subtree**: a range update on the same array.

This is called the **Euler tour technique** (the name is also used for a longer tour that records a node every time the walk passes it, 2n - 1 entries; that version, used for LCA by range minimum, is Topic 25).

```cpp
// Fenwick tree over positions 0..n-1 (Topic 20): point add, prefix sum.
class Fen {
    std::vector<long long> t_;
public:
    explicit Fen(int n = 0) : t_(n + 1, 0) {}
    void add(int i, long long d) { for (++i; i < static_cast<int>(t_.size()); i += i & -i) t_[i] += d; }
    long long prefix(int i) const { long long s = 0; for (; i > 0; i -= i & -i) s += t_[i]; return s; }   // sum of [0, i)
    long long range(int l, int r) const { return prefix(r) - prefix(l); }
};

// Node values with point updates and subtree sums: the subtree of v is the Euler range [tin, tout).
class SubtreeSums {
    const RootedTree& t_;
    Fen f_;
    long long total_ = 0;
public:
    explicit SubtreeSums(const RootedTree& t) : t_(t), f_(t.n) {}
    void add(int v, long long d) { f_.add(t_.tin[v], d); total_ += d; }
    long long subtree(int v) const { return f_.range(t_.tin[v], t_.tout[v]); }
    // Sum over the subtree of v when the tree is re-rooted at r (without rebuilding anything).
    long long subtree_rooted_at(int v, int r) const {
        if (v == r) return total_;
        if (!t_.is_ancestor(v, r)) return subtree(v);          // r is outside: v's subtree is unchanged
        int c = t_.child_toward(v, r);                          // r is below v: lose the branch towards r
        return total_ - subtree(c);
    }
};

// Subtree range add with point query, which is also "path sum from the root" with point updates:
// adding d to the value of node a changes the root-to-v sum of every v in a's subtree by d.
class SubtreeAdd {
    const RootedTree& t_;
    Fen f_;                                                     // difference array over Euler positions
public:
    explicit SubtreeAdd(const RootedTree& t) : t_(t), f_(t.n + 1) {}
    void add_subtree(int v, long long d) { f_.add(t_.tin[v], d); f_.add(t_.tout[v], -d); }
    long long value(int v) const { return f_.prefix(t_.tin[v] + 1); }
};
```

`SubtreeSums` is "node values, point update, subtree sum". `SubtreeAdd` is the dual: a **difference** Fenwick (add `d` at `tin`, subtract it at `tout`) gives "add `d` to the whole subtree, read one node's value". Reading a node's value then equals "the sum of the additions made at its ancestors and itself", which is also how you answer **"the sum of the values on the path from the root to `v`" with point updates**: changing the value of node `a` by `d` changes the root-to-`v` sum of every `v` in the subtree of `a` by exactly `d`, so it is `add_subtree(a, d)`.

**Subtree sum under a different root.** Suppose the questions say "the subtree of `v` when the tree is rooted at `r`", with `r` changing every query. Do not rebuild; use the tree rooted at the original root and look at where `r` is:

| Where `r` is | The subtree of `v` rooted at `r` |
|---|---|
| `r == v` | the whole tree |
| `r` is not in the original subtree of `v` | the original subtree of `v`, unchanged |
| `r` is below `v` | the whole tree minus the original subtree of the child `c` of `v` that contains `r` |

Finding `c` is the `child_toward(v, r)` call: among the children of `v` (sorted by `tin`), the last one with `tin <= tin[r]`, by binary search.

On the tree above give node `v` the value `v + 1`. The subtree sums are `55 26 28 4 20 25 7 8 19 10` for the nodes 0 to 9 (node 1: `2 + 4 + 5 + 7 + 8 = 26`). Rooted at node 6 (the path from 6 is `6 4 1 0 2`): the subtree of node 1 is the whole tree minus the subtree of its child 4 toward 6, `55 - 20 = 35`; of node 0 it is `55 - 26 = 29` (child 1 is toward 6); of node 4 it is `55 - 7 = 48`; of node 2 it is unchanged, **28**, because 6 is not below 2; of node 6 it is the whole tree, 55.

With `add_subtree(1, 10)` and then `add_subtree(4, 100)`, the node values read `0 10 0 10 110 0 110 110 0 0`: node 4 and its subtree 6 and 7 got both additions, node 3 only the first.

**What if the update is "assign" or the query is "max"?** Then a Fenwick tree is not enough: use a segment tree with lazy propagation (Topic 22) over the same `tin` order. The Euler tour does not change; only the structure over the array does.

---

## 4. The diameter

The **diameter** of a tree is the length of the longest path between two nodes. In an unweighted tree, length counts edges. The classic algorithm:

1. Start anywhere (`s`). Find the node `a` farthest from `s`.
2. Find the node `b` farthest from `a`. The path `a` to `b` is a diameter.

```cpp
struct Sweep { std::vector<long long> dist; std::vector<int> par; int far; };

// One pass from src over the tree: distances, parents and a farthest node.
// In a tree the path between two nodes is unique, so any traversal order works (no BFS queue needed).
inline Sweep sweep(const RootedTree& t, int src) {
    if (src < 0 || src >= t.n) throw std::out_of_range("sweep: src");
    Sweep s{std::vector<long long>(t.n, 0), std::vector<int>(t.n, -1), src};
    std::vector<int> stack{src};
    while (!stack.empty()) {
        int v = stack.back(); stack.pop_back();
        for (int e = t.start[v]; e < t.start[v + 1]; ++e) {
            int w = t.adj[e];
            if (w == s.par[v]) continue;
            s.par[w] = v; s.dist[w] = s.dist[v] + t.wt[e];
            if (s.dist[w] > s.dist[s.far]) s.far = w;
            stack.push_back(w);
        }
    }
    return s;
}

struct Diameter { int a, b; long long length; std::vector<int> path; };   // path runs from a to b

// Two passes: the farthest node a from anywhere, then the farthest node b from a.
// Needs every weight >= 0 (the argument breaks with negative weights, see the problems).
inline Diameter diameter(const RootedTree& t) {
    for (long long w : t.wt) if (w < 0) throw std::invalid_argument("diameter: negative weight");
    int a = sweep(t, t.root).far;
    Sweep s = sweep(t, a);
    Diameter d{a, s.far, s.dist[s.far], {}};
    for (int v = d.b; v != -1; v = s.par[v]) d.path.push_back(v);      // b ... a
    std::reverse(d.path.begin(), d.path.end());                         // a ... b
    return d;
}
```

Two things to notice. First, **a tree has a unique path between any two nodes**, so the distance from the source can be computed by any traversal order, a plain stack suffices (the BFS queue the textbook uses is needed for graphs, where there can be several paths). Second, the code checks that all weights are non-negative.

On the 10-node tree, the first pass from node 0 gives distances `0 1 1 2 2 2 3 3 3 4` and the farthest node is 9 (distance 4). The second pass from node 9 gives distances `4 5 3 6 6 2 7 7 1 0`; the farthest nodes are 6 and 7 (distance 7) and the code returns 6, with the path `9 8 5 2 0 1 4 6`. The diameter is **7 edges**.

**Why it works, and where it stops.** The statement is the standard lemma "a farthest node from any node is an end of some longest path" (I am quoting it, I did not prove it here; the exchange argument is in the usual texts). What I did check: the tests compare the returned length with the maximum over **all pairs** for every tree of 1 to 40 nodes in the seven shapes, with random weights from 0 to 9 including zero. The lemma needs **non-negative weights**. Counter-example (verified): nodes 0 to 3 with edges `0-1` weight -3, `1-2` weight 2, `0-3` weight 1. From node 0 the distances are `0 -3 -1 1`, the farthest is node 3, and from node 3 the farthest distance is 1; the best path is `1-2` with weight 2. With negative weights use a tree DP (Topic 26).

**One-pass alternative.** With the DFS order already available you can compute the diameter in one reverse pass: for each node keep the two longest downward chains among its children, and the best diameter through a node is their sum. It does not need the traversal from a second source. Section 8 shows it is much faster when the tree is already built, but it is a different algorithm: it needs a rooted structure, and the two-pass method works from any representation that can enumerate neighbours.

---

## 5. Centers

The **eccentricity** of a node is its distance to the farthest node. The **center** is the node (or nodes) with the smallest eccentricity. In an unweighted tree there are one or two centers, and they are the middle of any diameter path: if the diameter has an even number of edges, its middle node is the single center; if odd, the two middle nodes are the centers (adjacent). The radius is `ceil(diameter / 2)`.

```cpp
// Centers: the one or two nodes of smallest eccentricity (unit weights only): the middle of a diameter path.
inline std::vector<int> centers(const RootedTree& t) {
    if (!t.unit) throw std::invalid_argument("centers: unit weights only");
    Diameter d = diameter(t);
    int len = static_cast<int>(d.length);                               // edges on the path
    std::vector<int> c{d.path[len / 2]};
    if (len % 2 == 1) c.push_back(d.path[len / 2 + 1]);
    std::sort(c.begin(), c.end());
    return c;
}

// Centroids: nodes whose removal leaves no component larger than n/2 (one or two of them).
// The components of v are its child subtrees and the rest of the tree, n - size(v).
inline std::vector<int> centroids(const RootedTree& t) {
    std::vector<int> big(t.n, 0);                                       // largest child subtree of v
    for (int i = t.n - 1; i >= 1; --i) {                                // children before parents
        int v = t.order[i], p = t.parent[v];
        big[p] = std::max(big[p], t.size(v));
    }
    std::vector<int> c;
    for (int v = 0; v < t.n; ++v)
        if (2 * std::max(big[v], t.n - t.size(v)) <= t.n) c.push_back(v);
    return c;
}
```

On the 10-node tree the diameter path `9 8 5 2 0 1 4 6` has 7 edges, the middle edge is `2-0`, so the centers are **{0, 2}**. On a path of six nodes the centers are `{2, 3}`; on a path of five nodes the single center is the middle node.

The tests check the answer against the minimum eccentricity over all nodes, and that the radius is `(diameter + 1) / 2`. Weighted trees have a weighted centre that can fall in the middle of an edge, so `centers` refuses non-unit weights.

Another way to find the centers (from general knowledge, not tested here) is to **peel leaves**: remove all leaves, repeat until at most two nodes remain. It is the usual solution of the "minimum height trees" problem (LeetCode 310) and needs no diameter.

---

## 6. Centroids

The **centroid** of a tree is a node whose removal leaves no component larger than `n / 2`. Every tree has one or two centroids (two only when `n` is even and an edge splits the tree into two halves of `n / 2`). The components after removing `v` are its **child subtrees** and the **rest of the tree**, of size `n - size(v)`, so the test needs only subtree sizes.

The code in section 5 does this: the largest child subtree of each node is found by a reverse pass over `order` (children are entered after parents, so going backwards sees all children first), and `v` is a centroid when `2 * max(largest child, n - size(v)) <= n`.

For the 10-node tree the table of "largest child subtree / rest of tree" is:

| node | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| largest child | 5 | 3 | 3 | 0 | 1 | 2 | 0 | 0 | 1 | 0 |
| rest | 0 | 5 | 6 | 9 | 7 | 7 | 9 | 9 | 8 | 9 |

Only nodes 0 and 1 have both numbers at most 5, so the centroids are **{0, 1}** (the edge `0-1` splits the tree into two parts of 5 and 5). The centers of the same tree are `{0, 2}`: the two ideas differ. The centers minimise the **farthest distance**; the centroids minimise the **largest component**. On a path they coincide. On a "broom" they do not: take the path `0-1-...-9` and hang 20 leaves from node 9 (30 nodes). The diameter is 10 edges (node 0 to a leaf), so the center is node 5, the middle of the path; the only centroid is node 9, because removing it leaves 20 single leaves and one component of 9, while removing node 8 leaves 21 nodes, more than half.

The centroid matters because splitting a tree at its centroid leaves pieces of at most `n / 2` nodes, so repeating this gives a decomposition of depth `log2 n`: **centroid decomposition**, Topic 27. The answer does not depend on which node was the root (the tests build the tree from two different roots and compare).

---

## 7. Choosing

| Question | Use |
|---|---|
| Store a tree of 10^5 to 10^6 nodes | CSR adjacency; a vector of vectors only for small trees |
| Walk it | DFS with an explicit stack and a next-edge pointer per node |
| Is `u` an ancestor of `v`? Subtree size? | `tin` and `tout`, O(1) |
| Sum over a subtree with point updates | Fenwick over `tin` |
| Add to a subtree, read a node; path-from-root sum with point updates | Difference Fenwick over `tin` |
| Add to a subtree, sum over a subtree; assign; max | Lazy segment tree over `tin` (Topic 22) |
| Subtree queries with the root changing | Original root plus `child_toward` |
| Longest path, non-negative weights | Two passes, or the one-pass DP when the tree is already built |
| Longest path, possibly negative weights | Tree DP (Topic 26) |
| The middle of the tree by distance | Centers (middle of the diameter) |
| The middle of the tree by size | Centroid |
| Lowest common ancestor | Topic 25 |

---

## 8. What it costs

All timings are 1,000,000 nodes.

**Building.** I compared the CSR tree of section 2 (which builds the CSR arrays, parent, depth, `tin`, `tout`, `order` and the children lists) with a baseline that builds a `vector<vector<int>>` and computes only `tin` and `tout` with the same explicit-stack DFS. The two gave identical `tin` and `tout`.

| Tree | CSR | `vector<vector<int>>` | Ratio |
|---|---|---|---|
| Random | 183.5 / 164.3 ms | 429.1 / 374.5 ms | 2.3 |
| Path | 302.6 / 263.5 ms | 604.5 / 568.8 ms | 2.0 to 2.2 |
| Star | 90.0 / 91.3 ms | 211.7 / 212.7 ms | 2.3 to 2.4 |

CSR is **2.0 to 2.4 times faster** although it does more work. The whole `RootedTree` of a random tree takes **55,999,980 bytes** (56 bytes per node; 16 of them are the weights, which you can drop for unweighted problems). The path build is slower than the random one in both layouts; I did not isolate why.

**Recursion.** A recursive DFS on a path survived 173,000 nodes and died before 175,000 with an 8 MiB stack (section 2). The explicit stack built the 10^6 path in the table above.

**Subtree queries** (random tree, 1,000,000 operations of each kind):

| Operation | ns per operation |
|---|---|
| `SubtreeSums.add` | 96.2 / 67.1 |
| `SubtreeSums.subtree` | 83.9 / 80.8 |
| `subtree_rooted_at` | 93.6 / 88.9 |
| `SubtreeAdd.add_subtree` | 118.9 / 97.2 |
| `SubtreeAdd.value` | 42.7 / 41.5 |

Against scanning the Euler range directly (same answers on 200 queries): on the random tree the **scan is faster**, **11.4 / 12.3 ns** per query against 83.9 / 80.8, because the average subtree has only 9.1 nodes. On the **path**, where the average subtree has 364,396 nodes, the Fenwick query takes **72.5 / 72.1 ns** and the scan **901,333 / 823,427 ns**: about **11,000 to 12,500 times** slower. Moral: a Fenwick tree pays off when subtrees are large or queries are many and mixed with updates; for shallow random trees a scan is hard to beat.

**Ancestor test** on the path: the O(1) test took **13.4 / 13.0 ns**, walking up the parents **10,116,671 / 9,329,219 ns** (1000 queries), more than 700,000 times slower.

**Diameter, centers and centroids** (milliseconds):

| Tree | Diameter (2 sweeps) | Centers | Centroids | One-pass DP |
|---|---|---|---|---|
| Random | 115.1 / 126.1 | 105.5 / 126.8 | 13.9 / 15.3 | 15.3 / 17.7 |
| Path | 350.0 / 338.8 | 357.2 / 308.6 | 18.4 / 16.2 | 16.1 / 11.3 |
| Star | 70.9 / 62.8 | 65.2 / 50.0 | 11.6 / 9.8 | 12.4 / 9.5 |

Two sweeps together took **5.7 to 30 times as long** as the one-pass DP over the existing visit order (random: 7.1 to 7.5; path: 22 to 30; star: 5.7 to 6.6). The two methods agreed on the length (61 on the random tree, 999,999 on the path, 2 on the star). A likely reason (not isolated here) is memory access: the sweeps follow neighbour lists across shuffled labels, while the DP reads `order` and `parent`, which the DFS already laid out. The centers cost one diameter plus a little; the centroids are cheap because they reuse the sizes.

---

## 9. Common mistakes

1. **Recursive DFS on a tree that can be a path.** It works on the small tests and crashes on the big one; the limit I measured was 173,000 levels at 8 MiB.
2. **Forgetting the parent check** in an undirected adjacency list: the walk returns to the node it came from.
3. **Believing `n - 1` edges makes a tree.** It must also be connected.
4. **Mixing inclusive and exclusive exits.** Here `tout` is the first time stamp after the subtree: the range is `[tin, tout)`, the size is `tout - tin`.
5. **Fenwick off-by-one** between the 0-based Euler position and the 1-based tree; the difference array also needs a slot at `tout`, which can equal `n`.
6. **Using 0 as "no parent"** when node 0 exists. Use -1.
7. **1-based input** read into 0-based arrays.
8. **Using one Fenwick for add-subtree plus sum-subtree.** A single difference Fenwick supports "add to a range, read a point". Range add with range sum needs two Fenwick trees (a formula of the form `prefix * x - correction`) or a lazy segment tree.
9. **The two-pass diameter with a negative weight.**
10. **Counting the diameter in nodes** when the problem wants edges (or the other way round).
11. **Mixing up the center and the centroid.** One minimises the farthest distance, the other the largest component.
12. **Rooted-at-`r` queries**: forgetting the case `r == v`, or the case where `r` is outside the subtree.
13. **Building an adjacency of vectors** inside a loop that runs for every test case, with 10^6 small allocations each time.

**The mutants that survive:**

1. The edge-count test `!=` changed to `>` or to `<` (two mutants). The traversal check rejects the same inputs: too few edges leave nodes unreached, too many close a cycle. Only the wording of the error differs. The count check stays as a cheap early rejection with a clear message.
2. The self-loop test removed. The walk meets the loop as a cycle and rejects it.
3. The prefix-sum loop of `start` beginning at 1 instead of 0: `start[0]` is 0, so adding it changes nothing.
4. `SubtreeAdd` using a Fenwick tree of size `n` instead of `n + 1`: the extra slot is only written at position `n` and never read.
5. `2 * max <= n` rewritten as `max <= n / 2`: the same test in integer arithmetic.

---

## 10. Questions to answer out loud

1. A graph has `n` nodes and `n - 1` edges. Is it a tree? What else must you check?
2. How is a CSR adjacency built? Why does it beat a vector of vectors at 10^6 nodes?
3. Why does a recursive DFS fail on a path of 10^6 nodes, and what does the explicit stack replace?
4. What do `tin` and `tout` mean, and why is the subtree of `v` the range `[tin, tout)`?
5. How do you test "is `u` an ancestor of `v`" in O(1)?
6. How do you answer "sum of the subtree of `v`" with point updates? With "add to a subtree"?
7. How does "path sum from the root with point updates" become a subtree add?
8. For a re-rooted subtree sum, what are the three cases? How do you find the child toward `r`?
9. What are the two passes of the diameter algorithm? Why is a queue not needed in a tree? When does it fail?
10. How do the centers follow from the diameter? Can there be two?
11. What is a centroid? Why is the maximum component at most `n / 2`? Can there be two?
12. Give a tree where the center and the centroid differ (the broom of section 6 is one).

---

## 11. Problems

**A. The tour.** In the 10-node tree of section 2, give `tin`, `tout` and the size of node 4. List the subtree of node 2 from `order`. Is 2 an ancestor of 9? Is 1 an ancestor of 9?

**B. Subtree sums.** With node values `v + 1`, give the sum of the subtree of node 2, and the sum of the subtree of node 1 when the tree is rooted at node 6.

**C. Path sums.** Starting from all zeros, do `add_subtree(1, 10)` then `add_subtree(4, 100)`. What are the values of nodes 3, 4 and 6?

**D. Diameter and middles.** Give the diameter (in edges), one diameter path, the centers and the centroids of the 10-node tree. Why do the centers differ from the centroids here?

**E. A path.** For the path `0-1-2-3-4-5` give the diameter, the centers and the centroids.

**F. A weighted tree.** Edges `(0,1,3) (1,2,4) (1,3,2) (3,4,6)`. Give the diameter and a path.

**G. A failure.** Edges `0-1` weight -3, `1-2` weight 2, `0-3` weight 1. Run the two passes starting from node 0 and show what goes wrong.

**H. Design.** A tree of 10^6 nodes receives 10^6 operations, each either "add `x` to node `v`" or "ask for the sum over the subtree of `v`". Which structures do you build and what is the cost per operation? How does it change if some operations are "set every node in the subtree of `v` to `x`"?

---

## Answers

**A.** For node 4: `tin = 3`, `tout = 6`, size 3. The subtree of node 2 is `order[6..10) = 2 5 8 9`. Node 2 is an ancestor of 9 (`tin[2] = 6 <= 9 < 10`); node 1 is not (`tout[1] = 6 <= 9`).

**B.** The subtree of node 2 (`2 5 8 9`) has sum `3 + 6 + 9 + 10 = 28`. Rooted at 6, the subtree of node 1 is the whole tree (55) minus the subtree of its child toward 6, which is node 4 (sum 20): **35**.

**C.** The values are node 3: **10**, node 4: **110**, node 6: **110**. (Node 3 is in the subtree of 1 only; node 4 and its child 6 are in both.)

**D.** The diameter is **7** edges; the path `9 8 5 2 0 1 4 6` (or ending at 7). The centers are `{0, 2}` (the middle edge of the 7-edge path). The centroids are `{0, 1}`: the edge `0-1` splits the tree into two parts of 5 nodes. They differ because the centers care about distance (the long tail `2 5 8 9` pulls the center toward node 2) and the centroids about size (removing node 2 leaves a component of 6 nodes, more than half).

**E.** Diameter **5**; the centers are `{2, 3}`; the centroids are `{2, 3}` (removing node 2 leaves components of 2 and 3 nodes, and the other half symmetrically).

**F.** The diameter is **12**: the path `4 3 1 2` with weights `6 + 2 + 4`. (The other candidates: `4 3 1 0` is 11, `0 1 2` is 7.)

**G.** From node 0 the distances are `0, -3, -1, 1` so the farthest is node 3. From node 3 the distances are `1 (to 0), -2 (to 1), 0 (to 2)`, so the second pass reports 1. The real best path is `1-2` with weight 2. The lemma needs non-negative weights: node 3 is the farthest from node 0 only because the edge to node 1 is negative, and that hides the pair 1, 2 that actually forms the best path.

**H.** Build the `RootedTree` once (O(n)), then a Fenwick tree over `tin` (`SubtreeSums`): each add and each subtree sum is O(log n), about 70 to 95 ns per operation on the random tree of section 8. With "assign on a subtree" the sum is not a Fenwick quantity any more: use a lazy segment tree with an assign-sum action (Topic 22) over the same `tin` order, O(log n) per operation, with larger constants.

---

## Cheat sheet

- **Tree** = connected + `n - 1` edges. Validate both.
- **CSR**: `start` (prefix sums of degrees) + `adj`; built by counting sort; about 2.0 to 2.4 times faster than a vector of vectors at 10^6 nodes in these runs.
- **DFS without recursion**: stack + `next[v]` per node; `tin[v]` on entry, `tout[v]` = timer on exit. Recursive DFS died near 173,000 levels at 8 MiB.
- **Euler tour**: subtree of `v` = `order[tin[v] .. tout[v])`; `size = tout - tin`; ancestor test `tin[u] <= tin[v] < tout[u]`.
- **Subtree sum + point update**: Fenwick over `tin`. **Subtree add + point read, root-path sums**: difference Fenwick (`+d` at `tin`, `-d` at `tout`). Anything richer: Topic 22 over `tin`.
- **Re-rooted subtree**: `r == v` whole tree; `r` outside: unchanged; `r` below: whole minus child toward `r`.
- **Diameter**: two passes (farthest from anywhere, then farthest from that), non-negative weights only; a plain stack is enough in a tree.
- **Centers** = middle of the diameter (1 or 2). **Centroids** = largest component at most `n / 2` (1 or 2); from subtree sizes.
- **Measured (10^6 nodes):** Fenwick subtree query about 80 ns; on a path 11,000 to 12,500 times faster than scanning; ancestor test about 13 ns.

---

## Sources

- CLRS, appendix B.5 (trees) and the standard texts on the diameter lemma; the lemma is quoted, not proved here.
- cp-algorithms: "Euler tour" style subtree-to-range reductions are described in its tree and Fenwick articles; centroid decomposition and the "minimum height trees" leaf-peeling idea are standard (from general knowledge).
- The code in this file is my own implementation, tested as above. The claims about the default 8 MiB Linux stack and about running a DFS on a bigger stack are from general knowledge; the 173,000-level limit is measured here.
- LeetCode, by name: Maximum Depth of N-ary Tree, N-ary Tree Preorder Traversal, N-ary Tree Level Order Traversal, Serialize and Deserialize N-ary Tree, Diameter of Binary Tree, Tree Diameter, Minimum Height Trees, Number of Nodes in the Sub-Tree With the Same Label, Minimum Time to Collect All Apples in a Tree, Time Needed to Inform All Employees, Minimum Fuel Cost to Report to the Capital, Sum of Distances in Tree (Topic 26).
