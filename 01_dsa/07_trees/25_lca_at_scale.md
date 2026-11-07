# DSA 07.25: Lowest Common Ancestor at Scale

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (5 reading, 3 problems) · You need: [General trees and the Euler tour](24_general_trees_euler_tour_and_subtree_queries.md) (this file uses its `RootedTree`: `parent`, `depth`, `tin`, `order` and the children lists), [Prefix sums, sparse tables and RMQ](19_prefix_sums_sparse_tables_and_rmq.md) and, for Tarjan's method, the union-find idea (Topic 28 treats it fully; here you need only "find the root of a set, attach one root under another"). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

The **lowest common ancestor** (LCA) of two nodes is the deepest node that is an ancestor of both (a node counts as its own ancestor). It is the question behind distances in a tree, "is `x` on the path from `u` to `v`", the k-th node on a path, merging in heavy-light decomposition (Topic 27), and every question of the form "where do these two branches meet". On a tree of 10^6 nodes with 10^6 queries the choice of method decides whether you wait milliseconds or minutes.

By the end of this tutorial you can:

- answer LCA by walking up the parents and say exactly when that is too slow;
- build **binary lifting** (a table of 2^k-th ancestors), answer LCA, **k-th ancestor**, distance and the **k-th node on a path** with it, and size the table by depth and not by `n`;
- reduce LCA to a **range minimum** with the Euler tour and a sparse table (O(1) per query), and use the lighter variant that needs only the plain DFS order;
- run **Tarjan's offline LCA** with one DFS and a union-find, with no recursion;
- choose between them by build time, query time and memory, with measurements.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 4,496,404 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: all four structures against a brute-force LCA (mark all ancestors of `u`, take the deepest that is also an ancestor of `v`) on **every pair of nodes** of trees with 1 to 40 nodes in seven shapes (random, path, star, caterpillar, binary heap, broom, deep random), with random labels and random roots; the Euler tour itself (length `2n - 1`, starts and ends at the root, neighbouring entries differ by one level, each node appears once more than it has children); `kth_ancestor` for **every node and every `k`** up to depth + 2; `dist` against the length of the path walked by hand; `kth_on_path` for every `k` on random paths; Tarjan's method on random query lists up to 60 nodes with duplicates and `u == v`, against binary lifting; a path of 300,000 nodes (where LCA of `u` and `v` is simply the node nearer the root, so the expected answer is known without any LCA code) and a star of 300,000; out-of-range arguments for every entry point; and the exact table sizes on a small tree. Finally 89 hand-made mutants of the code were run against the tests (a shifted range end, a swapped comparison, a lost jump level, a union in the wrong direction and so on): **81 were caught and 8 are equivalent changes** that do not alter any final output (listed after the common mistakes).

**Data and timing.** The trees are **synthetic**, 1,000,000 nodes, labels shuffled, rooted at the node labelled 0 (a random position in the tree): "random" attaches each node to a uniformly random earlier node (maximum depth 42, mean depth 25.6 from that root), "path" is one chain (maximum depth 911,199, mean depth 419,085, because the root sits somewhere along the chain), "binary" is the heap shape (maximum depth 38, mean depth 35.0). Queries are 1,000,000 random pairs, generated beforehand. Times are milliseconds for a whole run or nanoseconds per query on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 24), best of 3 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about systems practice are from general knowledge and say so.

---

## 1. The baseline: walk up

Lift the deeper node until both are at the same depth, then lift both together until they meet.

```cpp
// Walk up from the deeper node, then from both: O(depth) per query, no preprocessing.
inline int lca_walk(const RootedTree& t, int u, int v) {
    if (u < 0 || u >= t.n || v < 0 || v >= t.n) throw std::out_of_range("lca_walk");
    while (t.depth[u] > t.depth[v]) u = t.parent[u];
    while (t.depth[v] > t.depth[u]) v = t.parent[v];
    while (u != v) { u = t.parent[u]; v = t.parent[v]; }
    return u;
}
```

It needs no preprocessing and O(depth) time per query. On the 10-node tree of Topic 24 (edges `(0,1) (0,2) (1,3) (1,4) (2,5) (4,6) (4,7) (5,8) (8,9)`, rooted at 0, depths `0 1 1 2 2 2 3 3 3 4`) it is fine. On a path it is catastrophic: section 8 measured **15.6 / 15.2 milliseconds per query** on the path tree, where binary lifting needs a few hundred nanoseconds.

---

## 2. Binary lifting

Any distance up the tree is a sum of powers of two. Store, for every node `v` and every `k`, the `2^k`-th ancestor `up[k][v]` (or -1 if there is none). Level 0 is the parent array; level `k` is level `k - 1` applied twice:

`up[k][v] = up[k-1][ up[k-1][v] ]`.

The table needs `levels` rows where `2^levels` exceeds the **maximum depth**, not `n`: on a random tree of 10^6 nodes the depth is 42 and six rows suffice; only on a path do you need 20.

```cpp
// up_[k * n + v] is the 2^k-th ancestor of v, or -1 if there is none.
class BinaryLifting {
    const RootedTree& t_;
    int levels_ = 1;
    std::vector<int> up_;
    int up(int k, int v) const { return up_[static_cast<std::size_t>(k) * t_.n + v]; }
    void check(int v) const { if (v < 0 || v >= t_.n) throw std::out_of_range("BinaryLifting: node"); }
public:
    explicit BinaryLifting(const RootedTree& t) : t_(t) {
        int maxd = *std::max_element(t.depth.begin(), t.depth.end());
        while ((1 << levels_) <= maxd) ++levels_;                 // 2^levels > every depth
        up_.assign(static_cast<std::size_t>(levels_) * t.n, -1);
        for (int v = 0; v < t.n; ++v) up_[v] = t.parent[v];
        for (int k = 1; k < levels_; ++k)                         // level k from level k-1, any node order
            for (int v = 0; v < t.n; ++v) {
                int mid = up(k - 1, v);
                up_[static_cast<std::size_t>(k) * t.n + v] = mid < 0 ? -1 : up(k - 1, mid);
            }
    }
    std::size_t bytes() const { return up_.size() * sizeof(int); }
    int levels() const { return levels_; }
    // The k-th ancestor of v (k = 0 is v itself), or -1 if k exceeds the depth of v.
    int kth_ancestor(int v, long long k) const {
        check(v);
        if (k < 0) throw std::out_of_range("BinaryLifting: negative k");
        if (k > t_.depth[v]) return -1;
        for (int b = 0; k > 0; ++b, k >>= 1) if (k & 1) v = up(b, v);
        return v;
    }
    int lca(int u, int v) const {
        check(u); check(v);
        if (t_.depth[u] < t_.depth[v]) std::swap(u, v);
        u = kth_ancestor(u, t_.depth[u] - t_.depth[v]);           // same depth now
        if (u == v) return u;
        for (int k = levels_ - 1; k >= 0; --k)                    // jump while the ancestors differ
            if (up(k, u) != up(k, v)) { u = up(k, u); v = up(k, v); }
        return t_.parent[u];
    }
    int dist(int u, int v) const { int a = lca(u, v); return t_.depth[u] + t_.depth[v] - 2 * t_.depth[a]; }
    // The k-th node on the path from u to v (k = 0 is u, k = dist is v), or -1 if k > dist.
    int kth_on_path(int u, int v, long long k) const {
        int a = lca(u, v);
        long long up_len = t_.depth[u] - t_.depth[a], total = up_len + t_.depth[v] - t_.depth[a];
        if (k < 0) throw std::out_of_range("BinaryLifting: negative k");
        if (k > total) return -1;
        if (k <= up_len) return kth_ancestor(u, k);
        return kth_ancestor(v, total - k);
    }
};
```

For the 10-node tree the maximum depth is 4, so there are 3 levels (`2^3 = 8 > 4`). The rows are:

| node | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| `up[0]` (parent) | -1 | 0 | 0 | 1 | 1 | 2 | 4 | 4 | 5 | 8 |
| `up[1]` (2nd ancestor) | -1 | -1 | -1 | 0 | 0 | 0 | 1 | 1 | 2 | 5 |
| `up[2]` (4th ancestor) | -1 | -1 | -1 | -1 | -1 | -1 | -1 | -1 | -1 | 0 |

**The k-th ancestor** is read from the bits of `k`: to go up 3 = `11` in binary, take the `2^0` jump and then the `2^1` jump. `kth_ancestor(9, 3)`: 3 is `11` in binary, so jump 1 from 9 to 8 and then jump 2 from 8 to 2: the answer is **2**; `kth_ancestor(9, 4) = 0`; `kth_ancestor(9, 5) = -1` (the depth of 9 is 4); `kth_ancestor(6, 2) = 1`. Asking beyond the depth returns -1 and never reads outside the table.

**The LCA.** Lift the deeper node to the depth of the other; if they are equal that node is the answer. Otherwise, going from the largest jump to the smallest, take every jump that leaves the two ancestors **different**; afterwards both are children of the answer. Take `lca(6, 9)`: the depths are 3 and 4, so first lift 9 by 1 to node 8. Now compare 6 and 8 level by level:

| `k` | `up[k][6]` | `up[k][8]` | equal? | action |
|---|---|---|---|---|
| 2 | -1 | -1 | yes | stay |
| 1 | 1 | 2 | no | `u = 1`, `v = 2` |
| 0 | 0 | 0 | yes | stay |

The nodes are now 1 and 2, both children of `parent[1] = 0`, so **the answer is 0**. Other answers on this tree: `lca(6, 7) = 4`, `lca(9, 3) = 0`, `lca(3, 6) = 1`, `lca(8, 5) = 5`.

Three things come for free. `dist(u, v) = depth[u] + depth[v] - 2 * depth[lca]` (`dist(6, 9) = 3 + 4 - 0 = 7`). **The k-th node on the path from `u` to `v`**: with `a` the LCA, if `k` is at most `depth[u] - depth[a]` it is the k-th ancestor of `u`; otherwise it is an ancestor of `v` at distance `dist - k` (the path from 9 to 3 is `9 8 5 2 0 1 3`, `dist = 6`, and asking for `k = 7` gives -1). And a membership test: `x` is on the path from `u` to `v` exactly when `dist(u, x) + dist(x, v) == dist(u, v)`.

---

## 3. Euler tour and a sparse table: O(1) queries

Record the tour of the DFS **with a node written every time the walk is at it**: on entry, and again after returning from each child. This is the Euler tour of `2n - 1` entries (Topic 24 used the shorter `tin/tout` version). For the 10-node tree:

`0 1 3 1 4 6 4 7 4 1 0 2 5 8 9 8 5 2 0` (19 entries),

with first occurrences `first = 0 1 11 2 4 12 5 7 13 14` for the nodes 0 to 9.

**Why LCA is a range minimum.** Between the first visits of `u` and `v` the tour walks the path from `u` up to `lca(u, v)` and down to `v` (possibly wandering into side subtrees below the path, which are deeper). The shallowest node in that stretch is the LCA. For `lca(3, 6)`: `first[3] = 2`, `first[6] = 5`, the stretch is `3 1 4 6` with depths `2 1 2 3`, and the minimum is node **1**.

A **sparse table** (Topic 19) answers "which position in `[l, r]` has the least depth" in O(1) by taking two overlapping blocks of length `2^floor(log2(r - l + 1))`:

```cpp
// Sparse table of nodes, the minimum being the node of smaller depth (Topic 19).
class DepthSparse {
    const std::vector<int>* depth_;
    int m_ = 0;
    std::vector<int> lg_, tab_;                                   // tab_[k * m + i]: best of [i, i + 2^k)
    int better(int a, int b) const { return (*depth_)[a] <= (*depth_)[b] ? a : b; }
public:
    DepthSparse(const std::vector<int>& seq, const std::vector<int>& depth) : depth_(&depth), m_(static_cast<int>(seq.size())) {
        lg_.assign(m_ + 1, 0);
        for (int i = 2; i <= m_; ++i) lg_[i] = lg_[i / 2] + 1;
        int levels = lg_[m_] + 1;
        tab_.resize(static_cast<std::size_t>(levels) * m_);
        std::copy(seq.begin(), seq.end(), tab_.begin());
        for (int k = 1; k < levels; ++k)
            for (int i = 0; i + (1 << k) <= m_; ++i)
                tab_[static_cast<std::size_t>(k) * m_ + i] = better(tab_[static_cast<std::size_t>(k - 1) * m_ + i], tab_[static_cast<std::size_t>(k - 1) * m_ + i + (1 << (k - 1))]);
    }
    int query(int l, int r) const {                               // best node among positions l..r inclusive
        int k = lg_[r - l + 1];
        return better(tab_[static_cast<std::size_t>(k) * m_ + l], tab_[static_cast<std::size_t>(k) * m_ + r - (1 << k) + 1]);
    }
    std::size_t bytes() const { return (tab_.size() + lg_.size()) * sizeof(int); }
};
```

and the Euler version just builds the tour with an explicit stack (no recursion) and asks it:

```cpp
// Euler tour of 2n - 1 entries (a node is recorded on entry and after each child) + sparse table: O(1) queries.
class EulerLca {
    const RootedTree& t_;
    std::vector<int> euler_, first_;
    std::unique_ptr<DepthSparse> sp_;
public:
    explicit EulerLca(const RootedTree& t) : t_(t), first_(t.n, 0) {
        euler_.reserve(2 * static_cast<std::size_t>(t.n) - 1);
        std::vector<int> next(t.cstart.begin(), t.cstart.end() - 1), stack{t.root};
        first_[t.root] = 0; euler_.push_back(t.root);
        while (!stack.empty()) {
            int v = stack.back();
            if (next[v] < t.cstart[v + 1]) {
                int c = t.clist[next[v]++];
                first_[c] = static_cast<int>(euler_.size()); euler_.push_back(c);
                stack.push_back(c);
            } else {
                stack.pop_back();
                if (!stack.empty()) euler_.push_back(stack.back());   // back at the parent
            }
        }
        sp_ = std::make_unique<DepthSparse>(euler_, t.depth);
    }
    std::size_t bytes() const { return (euler_.size() + first_.size()) * sizeof(int) + sp_->bytes(); }
    const std::vector<int>& tour() const { return euler_; }
    int lca(int u, int v) const {
        if (u < 0 || u >= t_.n || v < 0 || v >= t_.n) throw std::out_of_range("EulerLca: node");
        int l = first_[u], r = first_[v];
        if (l > r) std::swap(l, r);
        return sp_->query(l, r);
    }
};
```

The table holds `levels * (2n - 1)` entries: **188 MB** for a million nodes (section 8). The next variant halves it.

---

## 4. The same idea on the plain DFS order

The tour with `2n - 1` entries is not needed. Take the plain DFS order of Topic 24 (`order`, `tin`), and for two different nodes `u`, `v` with `tin[u] < tin[v]` look at the stretch of positions `(tin[u], tin[v]]`, **excluding the first**. The shallowest node in that stretch is a child of `lca(u, v)` on the way to `v` (every node in the stretch lies strictly inside the subtree of the LCA, and the first node of the LCA's child subtree that contains `v` is in the stretch and has depth one more than the LCA). So the answer is the **parent** of the shallowest node in the stretch, and the table has `n` entries per level.

```cpp
// The same idea on the plain DFS order of n entries: for tin[u] < tin[v], the shallowest node in
// order(tin[u], tin[v]] is a child of the answer, so the answer is its parent. Half the table of the Euler version.
class OrderLca {
    const RootedTree& t_;
    DepthSparse sp_;
public:
    explicit OrderLca(const RootedTree& t) : t_(t), sp_(t.order, t.depth) {}
    std::size_t bytes() const { return sp_.bytes(); }
    int lca(int u, int v) const {
        if (u < 0 || u >= t_.n || v < 0 || v >= t_.n) throw std::out_of_range("OrderLca: node");
        if (u == v) return u;
        int l = t_.tin[u], r = t_.tin[v];
        if (l > r) std::swap(l, r);
        return t_.parent[sp_.query(l + 1, r)];
    }
};
```

On the 10-node tree, `order = 0 1 3 4 6 7 2 5 8 9`. For `lca(3, 6)`: `tin[3] = 2`, `tin[6] = 4`, the stretch is the positions 3 and 4, nodes `4 6` with depths `2 3`; the shallowest is node 4, and its parent is **1**, the answer. For `lca(7, 9)`: `tin = 5` and `9`, the stretch is `2 5 8 9` (depths 1 2 3 4), the shallowest is node 2, its parent is **0**.

---

## 5. Tarjan's offline LCA

When all the queries are known in advance, one DFS plus a union-find answers them with **no table**. Give each query to both of its endpoints. Walk the tree depth first; when a node **finishes**, it joins the set of its parent. Then, at any moment, the root of the set of a finished node is the **deepest node of the currently open path** that is above it. Now take a query `(u, w)` and the moment the *second* of the two finishes, say `w`, with `u` already finished: the open path is exactly the path from the root to `w`, and the root of `u`'s set is the deepest open node above `u`: the deepest ancestor of `u` that lies on the root-to-`w` path. That is `lca(u, w)`.

```cpp
// Tarjan's offline LCA: one DFS with a union-find. When a node finishes it is attached to its parent, so the
// root of its set is always the deepest node of the open path above it. Iterative: no recursion.
inline std::vector<int> tarjan_lca(const RootedTree& t, const std::vector<std::pair<int, int>>& queries) {
    const int n = t.n, qn = static_cast<int>(queries.size());
    std::vector<int> qstart(n + 1, 0), qother(2 * static_cast<std::size_t>(qn)), qid(2 * static_cast<std::size_t>(qn));
    for (const auto& q : queries) {
        if (q.first < 0 || q.first >= n || q.second < 0 || q.second >= n) throw std::out_of_range("tarjan_lca: node");
        ++qstart[q.first + 1]; if (q.second != q.first) ++qstart[q.second + 1];
    }
    for (int v = 0; v < n; ++v) qstart[v + 1] += qstart[v];
    {
        std::vector<int> fill(qstart.begin(), qstart.end() - 1);
        for (int i = 0; i < qn; ++i) {
            auto [a, b] = queries[i];
            qother[fill[a]] = b; qid[fill[a]++] = i;
            if (b != a) { qother[fill[b]] = a; qid[fill[b]++] = i; }
        }
    }
    std::vector<int> dsu(n), ans(qn, -1), next(t.cstart.begin(), t.cstart.end() - 1), stack{t.root};
    std::vector<char> done(n, 0);
    std::iota(dsu.begin(), dsu.end(), 0);
    auto find = [&](int x) { while (dsu[x] != x) { dsu[x] = dsu[dsu[x]]; x = dsu[x]; } return x; };
    while (!stack.empty()) {
        int v = stack.back();
        if (next[v] < t.cstart[v + 1]) { stack.push_back(t.clist[next[v]++]); continue; }
        done[v] = 1;                                              // v's subtree is finished
        for (int e = qstart[v]; e < qstart[v + 1]; ++e)
            if (done[qother[e]]) ans[qid[e]] = find(qother[e]);
        stack.pop_back();
        if (!stack.empty()) dsu[v] = stack.back();                // v's set joins its parent's set
    }
    return ans;
}
```

Take the queries `(8, 5)`, `(7, 4)`, `(6, 9)` on the 10-node tree. The nodes finish in the order `3 6 7 4 1 9 8 5 2 0`.

| Query | Answered when | Set root at that moment | Answer |
|---|---|---|---|
| `(8, 5)` | 5 finishes (8 finished earlier and joined 5) | `find(8) = 5` | 5 |
| `(7, 4)` | 4 finishes (7 joined 4) | `find(7) = 4` | 4 |
| `(6, 9)` | 9 finishes (6's chain was `6, 4, 1`, and 1 already joined 0) | `find(6) = 0` | 0 |

The union-find here is the plainest version, with path halving and no union by rank; each finished node is attached directly under its parent, which is the root of its set at that moment. The cost is a DFS plus a near-linear number of `find` steps. The method needs the queries **before** the DFS and gives every answer at the end; it cannot answer one query on demand.

---

## 6. Beyond (awareness)

There are methods with O(n) preprocessing and O(1) queries using O(n) memory: the Farach-Colton and Bender reduction of LCA to a range minimum over a sequence whose neighbouring values differ by one, solved with blocks of size about `log n` and a sparse table over the block minima. I know this from general knowledge and did not implement or measure it here; the sparse-table variants above are what most contest and interview solutions use. Heavy-light decomposition (Topic 27) also answers LCA, in O(log n) per query and O(n) memory, as a by-product.

---

## 7. Choosing

| Situation | Use |
|---|---|
| A few queries, or a shallow tree | Walk up (no preprocessing) |
| Online queries, k-th ancestor, distances, k-th node on a path | Binary lifting |
| Online LCA only, enough memory, queries dominate | DFS-order sparse table |
| The same, with the Euler tour | Only if you need the tour for something else |
| All queries known in advance, memory is tight | Tarjan offline |
| Online LCA with a tight memory limit and many nodes | Binary lifting on a shallow tree, or a block method (section 6) |
| The tree grows by adding leaves online | Binary lifting: a new leaf's table column depends only on its ancestors (an idea, not implemented here) |

---

## 8. What it costs

All timings are 1,000,000 nodes and 1,000,000 random queries.

**Build** (milliseconds; the `RootedTree` is built once and excluded):

| Tree | Binary lifting | Euler + sparse | DFS order + sparse |
|---|---|---|---|
| Random | 19.8 / 22.9 | 220.5 / 198.9 | 62.6 / 60.6 |
| Path | 133.8 / 130.0 | 381.2 / 358.2 | 83.6 / 97.3 |
| Binary | 21.2 / 22.7 | 222.6 / 207.6 | 60.0 / 60.3 |

**Memory** (bytes): binary lifting **24,000,000** on the random and binary trees (6 levels) and **80,000,000** on the path (20 levels); the Euler version **187,999,912** (the tour, `first` and a 21-level table); the DFS-order version **84,000,004** (a 20-level table). The Euler version needs 2.2 times the memory of the DFS-order version. Tarjan's method keeps only the queries, a union-find and the answers (not measured).

**Query** (nanoseconds per query):

| Tree | Binary lifting | Euler + sparse | DFS order + sparse |
|---|---|---|---|
| Random | 130.0 / 121.0 | 41.5 / 43.8 | 43.9 / 46.2 |
| Path | 441.0 / 513.5 | 45.9 / 45.0 | 69.3 / 64.7 |
| Binary | 101.9 / 129.1 | 37.8 / 45.3 | 39.0 / 43.0 |

The Euler version is **2.8 to 3.1 times** (random), **9.6 to 11.4 times** (path) and **2.7 to 2.8 times** (binary) faster than binary lifting per query; the DFS-order version is **2.6 to 3.0**, **6.4 to 7.9** and **2.6 to 3.0 times** faster. The two sparse-table variants are close: on the path the DFS-order one is **1.4 to 1.5 times slower** per query, and on the random and binary trees the runs do not agree on a direction. All four methods gave the same answers on every query.

**Totals for build plus 1,000,000 queries** (milliseconds, from the figures above; Tarjan's figure is the measured whole run):

| Tree | Binary lifting | Euler + sparse | DFS order + sparse | Tarjan offline |
|---|---|---|---|---|
| Random | 149.8 / 143.9 | 262.0 / 242.7 | 106.5 / 106.8 | 238.3 / 245.7 |
| Path | 574.8 / 643.5 | 427.1 / 403.2 | 152.9 / 162.0 | 349.4 / 358.6 |
| Binary | 123.1 / 151.8 | 260.4 / 252.9 | 99.0 / 103.3 | 223.6 / 254.5 |

The **DFS-order variant has the lowest total on all three trees in both runs**. Tarjan offline is not faster than the DFS-order table here (it took about 240 ns per query on the random tree); its advantage is that it builds no table (its memory was not measured here). The Euler version loses to binary lifting on the shallow trees because its build is 8.7 to 11.1 times slower.

**Walking up** (first 100,000 queries; 100 on the path): **200.1 / 314.3 ns** per query on the random tree, **211.6 / 210.7** on the binary tree and **15,612,782 / 15,167,363 ns** on the path. The two runs of the random tree differ a lot, so read that figure as "a few hundred nanoseconds". On the shallow trees walking is 1.5 to 2.6 times slower than binary lifting; on the path it is more than 29,000 times slower.

**The k-th ancestor** by binary lifting took **25.3 / 26.0 ns** (random), **265.6 / 207.5 ns** (path) and **31.0 / 30.1 ns** (binary) for random `k` up to the depth of the node.

---

## 9. Common mistakes

1. **Sizing the table by `n`** instead of by the maximum depth: 20 rows instead of 6 on a random tree, more than three times the memory.
2. **Forgetting to return when the lifted node equals the other one.** The jump loop then climbs past the answer.
3. **Jumping while the ancestors are equal.** The rule is: jump while they are *different*; the answer is then the parent.
4. **A table with the root as its own ancestor** and a k-th ancestor that wraps silently. Use a sentinel (here -1) and test `k > depth` first.
5. **An Euler tour of `n` entries**, or a table sized `n`: the tour has `2n - 1`.
6. **Not swapping the endpoints** so that `l <= r` before the range query.
7. **Including `tin[u]` in the range of the DFS-order version.** The range is `(tin[u], tin[v]]` and the result is the *parent* of the minimum.
8. **Recursive DFS** in the build or in Tarjan at depth 10^6 (Topic 24 measured where it dies).
9. **Answering in Tarjan when a node is entered** instead of when it finishes, or attaching a node to its parent before its queries are answered.
10. **Using LCA on a forest.** Two nodes in different trees have no common ancestor; the `RootedTree` of Topic 24 rejects disconnected input.
11. **Mixing node labels and positions** when the sparse table stores nodes (it does here) or tour indices.
12. **Spending a 188 MB table on a problem with 10^3 queries**: a walk up would have been enough.

**The mutants that survive:**

1. A table entry of 0 instead of -1 for a missing ancestor at levels 1 and up. Two nodes at the same depth either both have that ancestor or both lack it, so the comparison in the LCA loop is unchanged, and `kth_ancestor` tests the depth first.
2. `k < up_len` instead of `k <= up_len` in `kth_on_path`. At `k == up_len` both branches reach the LCA.
3. The negative-`k` test removed from `kth_on_path`. The next call that uses `k` throws the same exception.
4. `lg_[r - l]` instead of `lg_[r - l + 1]`. It differs only when the length is a power of two, where two blocks of half the length tile the range exactly.
5. The prefix-sum loop of `qstart` beginning at 1: `qstart[0]` is 0, so adding it changes nothing.
6. `ans` initialised to 0 instead of -1: every query is answered before the function returns.
7. Path halving removed from `find`: the same roots are found, only more slowly.
8. A query answered every time one endpoint finishes (`if (true)`): the provisional answers are overwritten when the second endpoint finishes, and that last write is the correct one.

---

## 10. Questions to answer out loud

1. What is the LCA of two nodes when one is an ancestor of the other?
2. How is `up[k][v]` computed from the previous row? Why must the table have `2^levels > max depth` and not `> n`?
3. Walk through `kth_ancestor(v, 13)` using the bits of 13.
4. In the LCA loop, why do you jump while the ancestors are different? Why is the answer then the parent?
5. How do you get `dist(u, v)` and "is `x` on the path from `u` to `v`" from the LCA?
6. What is the Euler tour of a tree, and why is it `2n - 1` entries long?
7. Why is the shallowest node between the first visits of `u` and `v` the LCA?
8. What does a sparse table store, and how does it answer a range minimum in O(1)?
9. In the DFS-order variant, why do you exclude `tin[u]` and take the parent of the minimum?
10. In Tarjan's method, what is the root of a finished node's set at any time? When is a query answered?
11. When would you choose binary lifting over a sparse table, and the reverse?
12. The tree grows by adding leaves online and you need LCA queries between additions. What structure fits?

---

## 11. Problems

**A. K-th ancestors.** On the 10-node tree give `kth_ancestor(9, 3)`, `kth_ancestor(6, 2)`, `kth_ancestor(9, 5)`.

**B. A lifting run.** Trace binary lifting for `lca(6, 9)` as in section 2: which node is lifted first, and which jumps are taken?

**C. The tour.** Write the Euler tour of the 10-node tree, the `first` array, and find `lca(3, 6)` and `lca(2, 7)` as the minimum-depth node of a stretch.

**D. The DFS-order variant.** Find `lca(7, 9)` and `lca(6, 7)` with the plain order.

**E. Distances.** Give `dist(6, 9)`, `dist(9, 3)`, the nodes of the path from 9 to 3, and say whether node 4 and node 2 lie on it.

**F. Tarjan.** For the queries `(8, 5)`, `(7, 4)`, `(6, 9)` say when each is answered and by which `find`.

**G. Memory.** Compute the size of the binary lifting table for a tree of 10^6 nodes and depth 42, and for a path of 10^6 nodes. Then the size of the Euler sparse table with 21 levels over `2 * 10^6 - 1` entries.

**H. Choose.** (a) 10^6 online LCA queries on a static tree, speed first; (b) k-th ancestor queries; (c) 5 * 10^5 queries all known in advance, memory tight; (d) the tree gains a leaf between queries; (e) three queries on a tree of 10^6 nodes.

---

## Answers

**A.** `kth_ancestor(9, 3) = 2` (9 to 8 to 5 to 2). `kth_ancestor(6, 2) = 1` (6 to 4 to 1). `kth_ancestor(9, 5) = -1`: the depth of 9 is 4.

**B.** The depths are 3 (node 6) and 4 (node 9), so 9 is lifted by 1 to node 8. At `k = 2` both ancestors are -1, equal, stay. At `k = 1` the 2nd ancestors are 1 and 2, different: move to 1 and 2. At `k = 0` both parents are 0, equal, stay. The answer is `parent[1] = 0`.

**C.** The tour is `0 1 3 1 4 6 4 7 4 1 0 2 5 8 9 8 5 2 0`; `first = 0 1 11 2 4 12 5 7 13 14`. For `lca(3, 6)`: positions 2 to 5, nodes `3 1 4 6`, shallowest is 1. For `lca(2, 7)`: `first[2] = 11`, `first[7] = 7`, so positions 7 to 11: nodes `7 4 1 0 2` with depths `3 2 1 0 1`; the shallowest is 0.

**D.** `lca(7, 9)`: `tin = 5, 9`, positions 6 to 9, nodes `2 5 8 9`, shallowest is 2, parent 0. `lca(6, 7)`: `tin = 4, 5`, position 5 only, node 7, its parent is 4.

**E.** `dist(6, 9) = 7`, `dist(9, 3) = 6`. The path from 9 to 3 is `9 8 5 2 0 1 3`. Node 2 is on it (`dist(9, 2) + dist(2, 3) = 3 + 3 = 6`); node 4 is not (`dist(9, 4) + dist(4, 3) = 6 + 2 = 8`).

**F.** `(8, 5)` is answered when 5 finishes: `find(8) = 5`. `(7, 4)` when 4 finishes: `find(7) = 4`. `(6, 9)` when 9 finishes: `find(6) = 0`, because 6 joined 4, 4 joined 1 and 1 joined 0 before 9 finished.

**G.** Six rows (`2^6 = 64 > 42`): `6 * 10^6 * 4 = 24,000,000` bytes. A path: 20 rows (`2^20 = 1,048,576 > 10^6 - 1`; the depth can be up to 999,999): 80,000,000 bytes. The Euler tour table: 21 levels times 1,999,999 entries times 4 bytes is 167,999,916 bytes, and the whole structure measured 187,999,912 with the tour, `first` and the log array.

**H.** (a) The DFS-order sparse table: lowest total time in the runs above. (b) Binary lifting; the sparse tables do not answer k-th ancestor. (c) Tarjan offline (no table) or binary lifting if the tree is shallow. (d) Binary lifting: the table column of a new leaf depends only on its ancestors. (e) Walk up; preprocessing costs more than the queries.

---

## Cheat sheet

- **Walk up:** O(depth) per query, no memory. 15 ms per query on a path of 10^6.
- **Binary lifting:** `up[k][v] = up[k-1][up[k-1][v]]`, `levels` with `2^levels > max depth`. LCA: equalise depths, jump while different, take the parent. Also k-th ancestor, distance, k-th node on a path.
- **Euler + sparse table:** tour of `2n - 1` entries; LCA = shallowest node between the first visits. O(1).
- **DFS order + sparse table:** range `(tin[u], tin[v]]`, answer is the parent of the shallowest node. `n` entries per level.
- **Tarjan offline:** DFS, a finished node joins its parent; answer a query when its second endpoint finishes: `find(first endpoint)`.
- **Measured (10^6 nodes, 10^6 queries):** O(1) queries 38 to 70 ns against 100 to 510 ns for lifting; DFS-order sparse table has the lowest build plus query total on all three trees; table memory 24 to 80 MB (lifting), 84 MB (order), 188 MB (Euler).

---

## Sources

- The LCA methods here are the standard ones: see the cp-algorithms articles "Lowest Common Ancestor" (binary lifting, Euler tour with a sparse table, Tarjan's offline algorithm) and "Sparse Table"; Bender and Farach-Colton, "The LCA Problem Revisited" (2000), for the O(n) memory, O(1) query method of section 6 (from memory, not implemented here).
- The DFS-order variant of section 4 is a common contest trick; the code is my own implementation, tested as above.
- The remark that Tarjan's method needs the queries in advance and the claims about the default 8 MiB Linux stack are from general knowledge; all timings are measured here.
- LeetCode, by name: Lowest Common Ancestor of a Binary Tree, Lowest Common Ancestor of a Binary Search Tree, Kth Ancestor of a Tree Node, Lowest Common Ancestor of Deepest Leaves, Maximum Difference Between Node and Ancestor, Distance Between Nodes in a Tree (a classic LCA-and-depth problem).
