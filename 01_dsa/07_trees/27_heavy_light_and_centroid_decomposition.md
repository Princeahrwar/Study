# DSA 07.27: Heavy-Light and Centroid Decomposition

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (6 reading, 3 problems) · You need: [General trees and the Euler tour](24_general_trees_euler_tour_and_subtree_queries.md) (this file uses its `RootedTree`: `parent`, `depth`, `order`, `size`, the children lists and the neighbour lists `start` / `adj`), [Fenwick tree](20_fenwick_tree.md) and [segment tree](21_segment_tree_point_update_range_query.md) (the range structures that sit on top), and [LCA at scale](25_lca_at_scale.md) for the comparisons. There is no companion demo file: every structure is listed in this file and every listing is tested code.*

Topic 24 turned **subtree** questions into range questions: a subtree is one range of the DFS order. A **path** is different. A path from `u` to `v` goes up to their lowest common ancestor and down again, and in DFS order it is scattered over many ranges. This tutorial gives two ways to tame paths, and they are the two classical tools for "queries on a tree with updates":

- **Heavy-light decomposition** (HLD) cuts the tree into vertical chains so that every path is a union of O(log n) ranges of one array. Then any range structure (Fenwick, segment tree) answers path sum, path maximum and so on, with point updates.
- **Centroid decomposition** repeatedly removes a node whose removal splits the tree into pieces of at most half the size. Every path then passes through one "top" centroid, which turns distance questions (nearest marked node, number of pairs within distance K) into O(log n) work per node.

By the end you can:

- build the decomposition into heavy chains **without recursion**, prove that a path crosses at most log2(n) light edges, and answer **LCA, path sum, path maximum, subtree sum, edge queries and k-th ancestor** from it;
- explain why every chain **and** every subtree is a contiguous range of positions, and use that for subtree queries too;
- build the **centroid tree** (height at most floor(log2 n) + 1), keep the distance from every node to each of its centroid ancestors, and answer **nearest marked node** with online marks and **count pairs within distance K**;
- choose between HLD, centroid decomposition, the Euler tour and LCA tables, with measurements.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 4,428,507 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: on every tree of 1 to 40 nodes in seven shapes (random, path, star, caterpillar, binary heap, broom, deep random), with random labels and random roots, the whole HLD structure (the heavy child is a largest child, chains are consecutive positions, the head of every node, every subtree is a contiguous range of the right length, no root path crosses more than floor(log2 n) light edges); `lca` against a brute-force walk, and the ranges returned for a path against the brute-force node set (and, for edge queries, the brute-force edge set) for **every pair of nodes**, with at most `2 floor(log2 n) + 2` ranges per query; `kth_ancestor` for every node and every `k` up to depth + 2; `PathStore` against walking the path, over random mixes of updates and queries with negative values; the centroid tree (exactly one top, levels increase by one, every stored distance equals a breadth-first-search distance, each piece at level `l` has at most `n / 2^l` nodes, and each centroid is a true centroid of its piece); `NearestMarked` against a scan over all marked nodes, with marks interleaved with queries; `count_pairs_within` against all-pairs distances for every K from -1 to n; a path and a star of 300,000 nodes (where answers are known by formula); a heap-shaped tree of 262,143 nodes; and bad arguments for every function that checks its arguments. Finally 120 hand-made mutants of the code were run against the tests (a flipped comparison, an off-by-one range end, a lost update, a swapped chain jump and so on): **111 were caught and 9 are equivalent changes** that do not alter any final output (listed after the common mistakes).

**Data and timing.** The trees are **synthetic**, 1,000,000 nodes, labels shuffled, rooted at the node labelled 0 (a random position in the tree): "random" attaches each node to a uniformly random earlier node, "path" is one chain (so the root sits somewhere along it), "binary" is the heap shape. Queries are 1,000,000 random pairs and node values are random in -1000 to 1000. Times are on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 26), best of 3 repetitions (the pairs count: 1), shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Building the `RootedTree` is not included. Statements about systems practice and about results I did not test are from general knowledge and say so.

---

## 1. The problem, and the two ideas

Take a tree with a number at every node. We want, with the numbers changing over time:

- the **sum** or **maximum** over the nodes of the path from `u` to `v`;
- the same over the **edges** of the path;
- the sum of a whole **subtree**.

A DFS order makes the subtree a range, but a path is a range only when `u` is an ancestor of `v` *and* the path runs straight down. Walking the path costs O(depth), which is 10^6 steps on a long path. The two ideas:

1. **HLD:** arrange the nodes in an array so that long vertical stretches of the tree are contiguous. A path is then a few stretches.
2. **Centroid decomposition:** pick a node `c` so that removing it leaves pieces of at most n / 2 nodes. Any path between two nodes of different pieces goes through `c`. Recurse on the pieces. The recursion is only log n deep.

## 2. Heavy-light decomposition

For every non-leaf node call the child with the largest subtree the **heavy child** (the first one on a tie). The edge to it is a **heavy edge**; every other edge to a child is **light**. Heavy edges join into vertical **chains**, and every node belongs to exactly one chain (a leaf that is a light child is a chain of length one). The top of a chain is its **head**.

**The key fact.** A path from any node to the root crosses at most floor(log2 n) light edges. Proof: let `c` be a light child of `p`, and let `h` be the heavy child of `p`. Since `h` was chosen as the largest, `size(c) <= size(h)`, so `size(p) >= 1 + size(c) + size(h) > 2 size(c)`. Every light edge you climb at least doubles the size of the subtree you stand in, and sizes never exceed n. A path between `u` and `v` climbs from each end towards their LCA, so it touches at most 2 floor(log2 n) + 1 chains.

**Positions.** Number the nodes so that each chain gets consecutive numbers, top to bottom. Then a stretch of one chain is a range `[pos[a], pos[b]]` in an array indexed by position, and the range structures work on that array. The code lays out one chain at a time, walking down the heavy children, and pushes the light children it passes onto a stack; each is popped later and starts its own chain. A stack gives **no recursion**.

This order also makes every **subtree a contiguous range**: when a chain is laid out, every light child hanging off it is pushed, and the stack hands them back (last pushed first) before anything that was pushed earlier. So the nodes below `u` (the rest of its chain, and all the light subtrees hanging off that rest) are placed right after `u` and before anything outside `u`'s subtree. The subtree of `v` is `[pos[v], pos[v] + size(v) - 1]`. The tests check this for every node of every small tree.

```cpp
// Heavy-light decomposition. Every node picks its largest child as the heavy child; heavy edges form chains.
// Any path from a node to the root crosses at most log2(n) light edges, so it touches O(log n) chains.
// Positions are assigned chain by chain, so that every chain AND every subtree is one contiguous range of positions.
struct Hld {
    const RootedTree* t;                       // must outlive this object
    std::vector<int> heavy, head, pos, at;     // heavy child (-1 at a leaf), top of the chain, position, inverse of pos

    explicit Hld(const RootedTree& tr) : t(&tr), heavy(tr.n, -1), head(tr.n), pos(tr.n), at(tr.n) {
        for (int v = 0; v < tr.n; ++v) {                           // sizes are known, so any order will do
            for (int e = tr.cstart[v]; e < tr.cstart[v + 1]; ++e) {
                int c = tr.clist[e];
                if (heavy[v] < 0 || tr.size(c) > tr.size(heavy[v])) heavy[v] = c;   // first largest wins
            }
        }
        std::vector<int> stack{tr.root};                           // chain tops still to be laid out
        int timer = 0;
        while (!stack.empty()) {
            int h = stack.back(); stack.pop_back();
            for (int v = h; v >= 0; v = heavy[v]) {                // walk down the chain
                head[v] = h; pos[v] = timer; at[timer++] = v;
                for (int e = tr.cstart[v]; e < tr.cstart[v + 1]; ++e)
                    if (tr.clist[e] != heavy[v]) stack.push_back(tr.clist[e]);   // light children start new chains
            }
        }
    }
    void check(int v) const { if (v < 0 || v >= t->n) throw std::invalid_argument("hld: node out of range"); }

    // Lowest common ancestor in O(log n), with no table: jump the deeper chain top to its parent.
    int lca(int u, int v) const {
        check(u); check(v);
        while (head[u] != head[v]) {
            if (t->depth[head[u]] < t->depth[head[v]]) std::swap(u, v);
            u = t->parent[head[u]];
        }
        return t->depth[u] < t->depth[v] ? u : v;
    }
    // Calls f(l, r) (inclusive positions) for O(log n) ranges that together cover the path u..v.
    // edges = false: the nodes of the path. edges = true: the edges, where the edge to a node's parent is stored at the node.
    template <class F> void path(int u, int v, bool edges, F f) const {
        check(u); check(v);
        while (head[u] != head[v]) {
            if (t->depth[head[u]] < t->depth[head[v]]) std::swap(u, v);
            f(pos[head[u]], pos[u]);
            u = t->parent[head[u]];
        }
        if (t->depth[u] > t->depth[v]) std::swap(u, v);            // u is now the upper end, on the same chain
        if (pos[u] + (edges ? 1 : 0) <= pos[v]) f(pos[u] + (edges ? 1 : 0), pos[v]);
    }
    // The range of positions of the whole subtree of v: [pos[v], pos[v] + size - 1].
    int subtree_end(int v) const { check(v); return pos[v] + t->size(v) - 1; }
    // The k-th ancestor (k = 0 is v itself), or -1 if there is none: skip whole chains.
    int kth_ancestor(int v, long long k) const {
        check(v);
        if (k < 0) throw std::invalid_argument("hld: negative k");
        while (v >= 0) {
            int h = head[v], up = t->depth[v] - t->depth[h];       // steps from v to the top of its chain
            if (k <= up) return at[pos[v] - static_cast<int>(k)];
            k -= up + 1; v = t->parent[h];
        }
        return -1;
    }
};

// Path sum, path maximum and subtree sum over node values, with point updates, on top of an Hld:
// a Fenwick tree for sums and an iterative segment tree for maxima, both indexed by position.
struct PathStore {
    const Hld& h; int n; std::vector<long long> val, bit, seg;
    PathStore(const Hld& hl, const std::vector<long long>& v) : h(hl), n(hl.t->n), val(v), bit(hl.t->n + 1, 0), seg(2 * static_cast<std::size_t>(hl.t->n), LLONG_MIN) {
        if (v.size() != static_cast<std::size_t>(n)) throw std::invalid_argument("PathStore: values");
        for (int i = 0; i < n; ++i) { bit[i + 1] = val[h.at[i]]; seg[n + i] = val[h.at[i]]; }
        for (int i = 1; i <= n; ++i) { int j = i + (i & -i); if (j <= n) bit[j] += bit[i]; }   // linear Fenwick build
        for (int i = n - 1; i >= 1; --i) seg[i] = std::max(seg[2 * i], seg[2 * i + 1]);
    }
    void set(int v, long long x) {
        h.check(v);
        long long d = x - val[v]; val[v] = x;
        int p = h.pos[v];
        for (int i = p + 1; i <= n; i += i & -i) bit[i] += d;
        for (seg[p += n] = x, p >>= 1; p >= 1; p >>= 1) seg[p] = std::max(seg[2 * p], seg[2 * p + 1]);
    }
    long long prefix(int i) const { long long s = 0; for (; i > 0; i -= i & -i) s += bit[i]; return s; }      // sum of positions [0, i)
    long long range_sum(int l, int r) const { return prefix(r + 1) - prefix(l); }
    long long range_max(int l, int r) const {
        long long m = LLONG_MIN;
        for (l += n, r += n + 1; l < r; l >>= 1, r >>= 1) { if (l & 1) m = std::max(m, seg[l++]); if (r & 1) m = std::max(m, seg[--r]); }
        return m;
    }
    long long path_sum(int u, int v) const { long long s = 0; h.path(u, v, false, [&](int l, int r) { s += range_sum(l, r); }); return s; }
    long long path_max(int u, int v) const { long long m = LLONG_MIN; h.path(u, v, false, [&](int l, int r) { m = std::max(m, range_max(l, r)); }); return m; }
    long long subtree_sum(int v) const { return range_sum(h.pos[v], h.subtree_end(v)); }
};
```

**Walk-through.**

- *Constructor.* The first loop picks the heavy child of every node (any order will do, sizes are known); the strict `>` keeps the first largest child on a tie. The second loop is the chain layout described above.
- *`lca`.* While the two nodes are on different chains, take the one whose chain head is **deeper** and jump to the parent of its head. When they share a chain, the shallower one is the LCA. It needs no table and takes O(log n) time (a rebuild is needed if the tree changes shape).
- *`path`.* The same loop, but at every jump it hands the stretch `[pos[head], pos[u]]` to the caller. On the shared chain the last stretch runs from the upper node to the lower one. With `edges = true` the value of an edge is stored at its **lower** endpoint (the child), so the last stretch starts one position later, and a path with no edges (`u == v`) gives no stretch.
- *`kth_ancestor`.* Skip whole chains: if `k` fits inside the chain (at most `depth[v] - depth[head]` steps), the answer is a position lookup; otherwise subtract the chain's length and jump to the parent of the head.
- *`PathStore`.* A Fenwick tree for sums and an iterative segment tree for maxima, both indexed by position. A path query is O(log n) stretches times O(log n) per stretch, so **O(log^2 n)**; a point update is O(log n); a subtree sum is a single range.

**Trace.** The 10-node tree of Topic 24 has edges `(0,1) (0,2) (1,3) (1,4) (2,5) (4,6) (4,7) (5,8) (8,9)`, rooted at 0.

```text
node  :  0  1  2  3  4  5  6  7  8  9
size  : 10  5  4  1  3  3  1  1  2  1
heavy :  1  4  5 -1  6  8 -1 -1  9 -1
head  :  0  0  2  3  0  2  0  7  2  2
pos   :  0  1  6  5  2  7  3  4  8  9
```

The four chains are `0 1 4 6`, `2 5 8 9`, `3` and `7`. Reading the array by position gives `at = 0 1 4 6 7 3 2 5 8 9`: the first chain, then the light leaves 7 and 3 (popped last pushed first), then the chain headed by 2. Node 4 has two children 6 and 7 of equal size; the first, 6, is heavy. The subtree of 1 is positions 1 to 5 (`1 4 6 7 3`), size 5.

`lca(7, 9) = 0`, `lca(6, 3) = 1`, `lca(6, 7) = 4`, `lca(8, 5) = 5`.

The path from 6 to 9 (`6 4 1 0 2 5 8 9`): the chain tops are 0 (depth 0) and 2 (depth 1), so the 9 side jumps first and yields `[6, 9]`; then 0 and 6 share a chain and the last stretch is `[0, 3]`. Two stretches for eight nodes. For edges the last stretch starts one later, `[1, 3]`, and the seven edges are stored at the nodes `2 5 8 9` and `1 4 6` (every path node except the top, 0).

The path from 3 to 7 (`3 1 4 7`): the chain top of 7 (node 7, depth 3) is deeper than that of 3 (node 3, depth 2), so the first stretch is `[4, 4]` (node 7) and we move to 4; then 3 jumps: `[5, 5]`, and we move to 1; then 1 and 4 share a chain: `[1, 2]`. As edges the last stretch starts at position 2: `[2, 2]` (the edge `4-1`; node 1's own edge to its parent is not on the path).

`kth_ancestor(9, 3) = 2`, `kth_ancestor(9, 4) = 0`, `kth_ancestor(9, 5) = -1`, `kth_ancestor(6, 2) = 1`.

With the values `4 2 6 3 5 1 7 2 8 3` on the nodes 0 to 9: `path_sum(6, 9) = 36` (7 + 5 + 2 + 4 + 6 + 1 + 8 + 3), `path_max(6, 9) = 8`, `subtree_sum(1) = 19`, `subtree_sum(2) = 18`. After `set(5, 10)`: `path_sum(6, 9) = 45`, `path_max(6, 9) = 10`, `subtree_sum(2) = 27`.

**What HLD does not do.** If the values never change, a path sum needs no decomposition: store each node's sum from the root and combine with an LCA (Topic 25): `S[u] + S[v] - 2 S[lca] + val[lca]`, O(1) after a table. HLD is for **changing** values and for operations that are not invertible (the maximum). A combine that is not commutative (for example a path-ordered string or matrix product) also works but the two halves of the path must be combined in the right order and direction; that variant is from general knowledge and is not implemented here.

## 3. Centroid decomposition

A **centroid** of a tree is a node whose removal leaves pieces of at most n / 2 nodes each (Topic 24 shows every tree has one or two). Centroid decomposition removes a centroid `c`, then does the same inside every remaining piece. The chosen centroids form the **centroid tree**: the centroid of a piece is a child of the centroid that split it off.

- **Height.** A piece at level `l` has at most n / 2^l nodes, so there are at most floor(log2 n) + 1 levels.
- **Path property.** Let `c` be the lowest common ancestor of `u` and `v` in the centroid tree. Both nodes lie in the piece of `c`, and removing `c` puts them in different pieces (or one of them is `c`), so the tree path from `u` to `v` goes through `c` and `dist(u, v) = dist(u, c) + dist(c, v)`. For every other common ancestor `c'` the walk `u` to `c'` to `v` is a walk between the same ends, so `dist(u, c') + dist(c', v) >= dist(u, v)`.

The code stores `dist[l][v]`: the number of edges from `v` to its centroid ancestor at level `l`. A node has `level[v] + 1` of them, one per ancestor (including itself at distance 0), and the table has `height` rows of `n` entries. The build is a loop over pieces with an explicit stack: search the piece, compute subtree sizes in reverse search order, slide from any node towards a too-large neighbour until none is more than half, then search again from the centroid for the distances. Each level costs O(n) in total, so the build is **O(n log n)**.

```cpp
// Centroid decomposition. Pick a centroid c of the tree (every piece left after removing c has at most half the nodes),
// remove it, and repeat on each piece. The centroid chosen for a piece becomes a child of c in the "centroid tree",
// whose height is at most floor(log2 n) + 1. Every path u..v passes through the top centroid of the smallest piece that holds both.
// dist[l][v] is the number of edges from v to its centroid ancestor at level l (valid for l <= level[v]).
struct CentroidTree {
    std::vector<int> parent, level;            // parent in the centroid tree (-1 at the top), depth in it (top = 0)
    std::vector<std::vector<int>> dist;

    explicit CentroidTree(const RootedTree& t) : parent(t.n, -1), level(t.n, 0) {
        const int n = t.n;
        std::vector<char> removed(n, 0);
        std::vector<int> bp(n), sz(n), comp;
        struct Job { int s, pc, lv; };
        std::vector<Job> jobs{{t.root, -1, 0}};
        while (!jobs.empty()) {
            Job j = jobs.back(); jobs.pop_back();
            comp.assign(1, j.s); bp[j.s] = -1;                     // collect the piece by a search from j.s
            for (std::size_t i = 0; i < comp.size(); ++i) {
                int v = comp[i]; sz[v] = 1;
                for (int e = t.start[v]; e < t.start[v + 1]; ++e) {
                    int w = t.adj[e];
                    if (!removed[w] && w != bp[v]) { bp[w] = v; comp.push_back(w); }
                }
            }
            const int total = static_cast<int>(comp.size());
            for (int i = total - 1; i >= 1; --i) sz[bp[comp[i]]] += sz[comp[i]];
            int c = j.s;                                           // slide towards the heavy side until no piece is over half
            for (;;) {
                int next = -1;
                for (int e = t.start[c]; e < t.start[c + 1]; ++e) {
                    int w = t.adj[e];
                    if (!removed[w] && w != bp[c] && 2 * sz[w] > total) { next = w; break; }
                }
                if (next < 0) break;
                c = next;
            }
            if (static_cast<int>(dist.size()) <= j.lv) dist.emplace_back(n, 0);
            std::vector<int>& d = dist[j.lv];
            comp.assign(1, c); bp[c] = -1; d[c] = 0;               // distances from the centroid to its whole piece
            for (std::size_t i = 0; i < comp.size(); ++i) {
                int v = comp[i];
                for (int e = t.start[v]; e < t.start[v + 1]; ++e) {
                    int w = t.adj[e];
                    if (!removed[w] && w != bp[v]) { bp[w] = v; d[w] = d[v] + 1; comp.push_back(w); }
                }
            }
            removed[c] = 1; parent[c] = j.pc; level[c] = j.lv;
            for (int e = t.start[c]; e < t.start[c + 1]; ++e)
                if (!removed[t.adj[e]]) jobs.push_back({t.adj[e], c, j.lv + 1});
        }
    }
    int height() const { return static_cast<int>(dist.size()); }
};

// "Distance to the nearest marked node" with marks added over time: O(log n) per mark and per query.
struct NearestMarked {
    static constexpr int NONE = INT_MAX / 2;
    const CentroidTree& c; std::vector<int> best;                  // best[a]: nearest marked node inside a's piece, from a
    explicit NearestMarked(const CentroidTree& ct) : c(ct), best(ct.parent.size(), NONE) {}
    void check(int v) const { if (v < 0 || v >= static_cast<int>(best.size())) throw std::invalid_argument("NearestMarked: node out of range"); }
    void mark(int v) {
        check(v);
        for (int a = v; a >= 0; a = c.parent[a]) best[a] = std::min(best[a], c.dist[c.level[a]][v]);
    }
    int query(int v) const {                                       // -1 if nothing is marked
        check(v);
        int r = NONE;
        for (int a = v; a >= 0; a = c.parent[a]) r = std::min(r, best[a] + c.dist[c.level[a]][v]);
        return r >= NONE ? -1 : r;
    }
};

// The number of unordered pairs of different nodes at distance at most K. Count through each centroid, then subtract
// the pairs that lie inside one child piece (they were counted with the wrong, longer, route through the centroid).
inline long long count_pairs_within(const CentroidTree& c, int K) {
    const int n = static_cast<int>(c.parent.size());
    if (K < 0) return 0;
    std::vector<std::vector<int>> A(n), B(n);                      // A[a]: distances to a from its piece; B[a]: distances to a's parent
    for (int x = 0; x < n; ++x)
        for (int a = x; a >= 0; a = c.parent[a]) {
            A[a].push_back(c.dist[c.level[a]][x]);
            if (c.parent[a] >= 0) B[a].push_back(c.dist[c.level[c.parent[a]]][x]);
        }
    auto pairs = [&](std::vector<int>& v) {                        // pairs i < j with v[i] + v[j] <= K, by two pointers
        std::sort(v.begin(), v.end());
        long long cnt = 0; int i = 0, j = static_cast<int>(v.size()) - 1;
        while (i < j) { if (v[i] + v[j] <= K) { cnt += j - i; ++i; } else --j; }
        return cnt;
    };
    long long total = 0;
    for (int a = 0; a < n; ++a) total += pairs(A[a]) - pairs(B[a]);
    return total;
}
```

**Trace.** On the same 10-node tree:

```text
node    :  0  1  2  3  4  5  6  7  8  9
cparent : -1  4  5  1  0  0  4  4  5  8
clevel  :  0  2  2  3  1  1  2  2  2  3
```

Node 0 is the top: removing it leaves the pieces `{1, 3, 4, 6, 7}` and `{2, 5, 8, 9}` (the largest has 5 = n / 2 nodes, so 0 qualifies; node 1 would too). The first piece has the centroid 4 (removing it leaves `{1, 3}`, `{6}`, `{7}`), and the second has 5 (leaves `{2}` and `{8, 9}`). The height is 4 levels. The distance rows (a node shows `-1` where it is not in a piece of that level):

```text
level 0 (centroid 0)    : 0  1  1  2  2  2  3  3  3  4
level 1 (centroids 4, 5): -1  1  1  2  0  0  1  1  1  2
level 2                 : -1  0  0  1 -1 -1  0  0  0  1
level 3                 : -1 -1 -1  0 -1 -1 -1 -1 -1  0
```

For node 9 the chain of centroid ancestors is 9 (level 3), 8 (level 2), 5 (level 1), 0 (level 0) with distances 0, 1, 2 and 4: the entries in column 9, bottom to top.

### Nearest marked node

Keep `best[a]` = the distance from `a` to the nearest marked node **inside a's piece**. Marking `v` updates `best[a]` for each centroid ancestor `a` of `v`, with `dist(a, v)`. A query for `v` takes the minimum over its centroid ancestors of `best[a] + dist(a, v)`. Both are O(log n). It is exact because the true nearest marked node `m` has a centroid `a` that is a common ancestor of `v` and `m` and lies on the path between them, where `best[a] + dist(a, v) = dist(m, v)`; every other term is an upper bound.

After marking 6 and 9, the nearest-marked distances for nodes 0 to 9 are `3 2 3 3 1 2 0 2 1 0` (node 0: three edges to 6). Marks can only be added: removing a mark needs a multiset per centroid (general knowledge; not implemented).

### Pairs within distance K

For one centroid `c`, the pairs whose path goes through `c` are the pairs of nodes in its piece with `dist(u, c) + dist(c, v) <= K`, **minus** the pairs inside the same child piece (their real path avoids `c`, so the sum overcounts). Sort the distances and count with two pointers. The code collects, for every centroid `a`, the list `A[a]` of distances to `a` from its piece and the list `B[a]` of distances from the same nodes to `a`'s parent, and the answer is the sum over `a` of `pairs(A[a]) - pairs(B[a])`. Total list length is O(n log n), sorting makes it **O(n log^2 n)**.

On the 10-node tree the numbers of pairs within distance 1, 2, 3 and 4 are **9, 19, 28 and 34** (9 is the edge count).

## 4. What it costs

Numbers are **run 1 / run 2**; 10^6 nodes, 10^6 queries.

| Tree | HLD build (ms) | Centroid build (ms) | Centroid levels | Centroid table (bytes) |
|---|---|---|---|---|
| Random | 60.5 / 58.5 | 859.7 / 892.9 | 17 | 68,000,000 |
| Path | 59.8 / 63.6 | 3,165.3 / 3,179.0 | 20 | 80,000,000 |
| Binary | 37.8 / 34.0 | 1,150.7 / 1,134.1 | 19 | 76,000,000 |

| Tree | Chains | Max light edges to the root | Ranges per path (mean, max) |
|---|---|---|---|
| Random | 499,436 | 10 | 10.22, 18 |
| Path | 2 | 1 | 1.49, 2 |
| Binary | 500,000 | 18 | 17.40, 31 |

| Tree | `lca` (ns) | `path_sum` (ns) | `path_max` (ns) | `set` (ns) |
|---|---|---|---|---|
| Random | 315.3 / 315.8 | 1,032.3 / 1,108.8 | 572.5 / 601.4 | 195.1 / 203.8 |
| Path | 25.1 / 26.5 | 131.9 / 132.0 | 279.1 / 234.2 | 197.6 / 187.5 |
| Binary | 646.8 / 722.8 | 1,444.7 / 1,382.0 | 1,075.3 / 992.8 | 189.8 / 191.2 |

| Tree | `mark` (ns) | `query` (ns) | Pairs within 10 (ms) |
|---|---|---|---|
| Random | 398.9 / 396.8 | 207.3 / 231.0 | 1,404.8 / 1,585.4 |
| Path | 699.3 / 617.8 | 601.2 / 553.7 | 3,169.8 / 3,000.9 |
| Binary | 649.9 / 667.7 | 510.2 / 493.6 | not run |

What holds in both runs:

- The **HLD build** is cheap, 34.0 to 63.6 ms, and the **centroid build** costs at least 14 times as much (860 to 3,179 ms); the path is the slowest case for the centroid build (20 levels, each a full pass over the tree). The centroid table takes 68 to 80 MB.
- The **light-edge bound holds with room to spare on random trees** (at most 10 light edges, the bound being 19) and is **nearly reached by the heap-shaped tree** (18). The path has 2 chains because the root sits in the middle of it.
- A **path query touches few ranges on a random tree** (about 10, at most 18) and the most on the heap-shaped tree (about 17, at most 31, the bound being 2 * 19 + 1 = 39).
- `lca` is fastest on the path and slowest on the heap-shaped tree, with the random tree between (25.1 to 26.5, 315 to 316 and 647 to 723 ns), which follows the number of chain jumps. Every `path_sum` and `path_max` costs more than the `lca` on the same tree, and a `set` costs 188 to 204 ns on every tree.
- The **nearest-marked** query is cheaper on the random tree (207 to 231 ns) than on the path or the heap-shaped tree (494 to 601 ns), and so is the mark (397 to 399 ns against 618 to 699 ns).

Also in both runs: `path_sum` costs more than `path_max` on the random and the heap-shaped tree and less than `path_max` on the path; I did not investigate why. The pairs count within 10 is slower on the path (3,000.9 to 3,169.8 ms) than on the random tree (1,404.8 to 1,585.4 ms); it was not run on the heap-shaped tree.

**Against walking up.** On the first 2,000 queries, walking up from both ends took **1,175.3 / 1,084.9 ns** per query on the random tree, **15,041,482.8 / 15,027,035.4 ns** on the path, and **1,383.7 / 1,334.5 ns** on the heap-shaped tree; HLD took **1,070.9 / 1,034.6 ns**, **161.6 / 84.4 ns** and **1,501.4 / 1,553.7 ns**. On the path HLD is **more than 90,000 times faster**. On the random tree HLD was faster in both runs, but only by a factor of 1.05 to 1.10 (depth about 25), and on the heap-shaped tree it was slower in both. An earlier pair of runs of the same program (before a one-line change to the build loop) gave a different order on the heap-shaped tree, so the small differences on these two trees are noise-level and I claim no winner there. HLD pays off when the depth is large; its answers always agreed with the walk.

**Correctness at this size.** The nearest-marked answers for all 10^6 queries match a multi-source breadth-first search on all three trees (100,000 random marks). The number of pairs within distance 10 on the path equals the formula `sum over d = 1..10 of (n - d)` = 9,999,945. On a random tree of 3000 nodes `count_pairs_within` matches a breadth-first search from every node for K = 1, 3, 6, 10 (2,999, 21,293, 172,868 and 1,246,936 pairs). The random tree of 10^6 nodes has 983,347,358 pairs within 10.

## 5. Choosing

| Question | Use |
|---|---|
| Subtree sum or max with updates | Euler tour + Fenwick or segment tree (Topic 24); HLD also gives the subtree range |
| Path sum or max with updates | HLD + Fenwick or segment tree |
| Path sum, **no** updates, many queries | Prefix sums from the root + LCA (Topic 25) |
| Just LCA or distance | The tables of Topic 25 if memory allows; HLD `lca` is O(log n) with no table |
| k-th ancestor | HLD (chain skipping) or binary lifting |
| Nearest marked node, marks added over time | Centroid decomposition |
| Number of pairs at distance at most K | Centroid decomposition |
| Distance-based question about every node | Centroid decomposition, or rerooting (Topic 26) if the answer is one function of the whole tree |
| Insert or delete edges while the tree changes | Link-cut trees (general knowledge; not covered here) |

In interviews HLD and centroid decomposition appear rarely (general knowledge), but the *ideas* come up: "decompose into chains so that a path is few ranges" and "split at the centroid so the recursion is log n deep" are the answers to two classic "queries on a tree" questions.

## 6. Common mistakes

- **Recursive DFS for sizes and chains.** A path of 10^6 nodes overflows the stack. Use `order` backwards for sizes and a stack for chains.
- **Picking the heavy child by height instead of size.** The doubling argument needs *size*. A height-based choice loses the log bound.
- **Edge values on the wrong node.** An edge value belongs to the child end; the top node of the path then contributes no edge, which is why the last stretch starts at `pos[u] + 1`.
- **Forgetting the last stretch.** After the loop the two nodes are on one chain and the stretch between them must still be reported (and it must be ordered by depth, not by label).
- **Subtree range off by one.** The end is `pos[v] + size - 1`, inclusive.
- **Comparing heads by depth of the node, not of the head.** The jump must move the node whose *chain head* is deeper; moving the deeper *node* can skip past the LCA.
- **Forgetting the piece above.** The slide uses sizes counted from the search start. It is right because it moves into a child exactly when that child holds more than half the piece, which leaves the part above (`total - size`) at less than half.
- **Counting pairs without subtracting the same-child-piece pairs.** The path through the centroid is longer than the true path for those pairs, so the count is wrong in both directions.
- **Unmarking.** `best[a]` is a minimum; it cannot be undone. Use a multiset per centroid or work offline in reverse.
- **Distance table size.** `dist` has `height` rows of `n` entries (68 to 80 MB at 10^6 nodes); the `A` and `B` lists of the pair count add O(n log n) more.

The equivalent mutants (changes that do not alter any output, so no test can catch them): `<=` instead of `<` when `lca` compares the depths of the two chain heads (when they are equal either node may jump); `parent[u]` instead of `parent[head[u]]` in `lca` (the same answer, one step at a time, so slower); comparing positions instead of depths on the shared chain in `path` (on one chain the two orders agree); a Fenwick build loop that stops one short (the last index has no parent to update); a size accumulation that skips the first child of the search start (the size of the start is never read); leaving out `d[c] = 0` (each table entry is written by one piece only, so it is still 0); dropping the early return for a negative K, and returning early for K = 0 (neither changes the count, since distinct nodes are at least 1 apart); and `i <= j` in the two-pointer loop (the extra step adds 0)..

## 7. Questions to answer out loud

1. **What is the heavy-light decomposition and why is a path O(log n) chains?** Each node's heavy child is its largest; a light edge at least doubles the subtree size going up, so at most log2 n light edges lie on a root path.
2. **How do you lay out the array so a subtree is also a range?** Lay out one chain at a time and keep the light children on a stack: everything below a node is placed right after it.
3. **Path query cost?** O(log n) ranges, O(log n) per range: O(log^2 n); update O(log n).
4. **How do edge weights work?** Store each at its child node and leave out the top node of the path (start the last stretch one later).
5. **What is a centroid and why is the recursion log n deep?** A node whose removal leaves pieces of at most n / 2 nodes; pieces halve at every level.
6. **How do you answer nearest marked node?** `best[a]` at each centroid; mark and query walk the centroid ancestors; exact because the true nearest mark is reached through a common centroid.
7. **How do you count pairs within K?** Per centroid, sort the distances, two pointers, subtract the pairs inside one child piece.
8. **When is HLD unnecessary?** When values never change: root prefix sums plus LCA.

## 8. Problems

**A.** For the 10-node tree list the heavy child of every node, the four chains, and `pos` of every node. What is the subtree range of node 2?

**B.** How many light edges lie on the root path of node 6? Of node 7? Of node 3? For the heap-shaped tree of 7 nodes (node `i` has children `2i + 1` and `2i + 2`) what is the largest number of light edges on a root path?

**C.** Give the ranges for the **edges** of the path from 3 to 7 and say which tree edges they are.

**D.** Decompose the 10-node tree into centroids by hand. What are the top centroid, its two pieces, the centroids of the pieces, and the height of the centroid tree?

**E.** Mark nodes 6 and 9. Give the nearest-marked distance for every node and explain the value for node 3 using its chain of centroid ancestors.

**F.** Count the pairs within distance 2 on a path of 5 nodes, on a star with 6 leaves, and on the 10-node tree.

**G.** Show by an example that counting pairs through a centroid without subtracting the same-piece pairs gives the wrong answer.

**H.** Choose a method: (a) path sums of a tree whose values never change, 10^6 queries; (b) node values change, query the maximum on a path; (c) how many pairs of nodes are at most K apart; (d) the sum over a subtree with updates; (e) the closest "server" for any node while servers are added.

### Answers

**A.** Heavy children: 0 to 1, 1 to 4, 2 to 5, 4 to 6, 5 to 8, 8 to 9 (nodes 3, 6, 7, 9 are leaves). Chains `0 1 4 6`, `2 5 8 9`, `3`, `7`. Positions: node 0 at 0, 1 at 1, 4 at 2, 6 at 3, 7 at 4, 3 at 5, 2 at 6, 5 at 7, 8 at 8, 9 at 9. The subtree of 2 is positions 6 to 9 (size 4).

**B.** Node 6 is on the root chain: 0 light edges. Node 7: its edge to 4 is light and 4 is on the root chain: 1. Node 3: 1. In the 7-node heap shape the heavy child of every node is its first child (equal sizes), so node 6 (second child of 2, itself the second child of 0) has 2 light edges, which is the most: floor(log2 7) = 2.

**C.** `[2, 2]` for the edge 4-1, `[4, 4]` for the edge 7-4 and `[5, 5]` for the edge 3-1. The jump from 3 and from 7 each stores an edge at the chain head (positions 5 and 4), and the shared chain contributes the edge stored at node 4, position 2.

**D.** The top centroid is 0 (node 1 would also work). Its pieces are `{1, 3, 4, 6, 7}` with centroid 4 and `{2, 5, 8, 9}` with centroid 5. The deepest chain is 0, 4, 1, 3 (levels 0 to 3), so the height is 4 levels.

**E.** `3 2 3 3 1 2 0 2 1 0`. For node 3 the centroid ancestors are 3, 1, 4, 0. Marks at 6 and 9 are recorded at 4 (distance 1 to node 6; 9 is not in 4's piece) and at 0 (distances 3 and 4, so `best[0] = 3`). The query at node 3 takes the best of `best[4] + dist(4, 3) = 1 + 2 = 3` and `best[0] + dist(0, 3) = 3 + 2 = 5`, giving 3 (the path 3, 1, 4, 6).

**F.** Path of 5 nodes: 4 + 3 = 7. Star with 6 leaves: every pair is within 2, so 7 * 6 / 2 = 21. The 10-node tree: 19.

**G.** Take the path `a - b - c - d` and K = 3. Its centroid `b` has the pieces `{a}` and `{c, d}`. The distances to `b` are 0 (b), 1 (a), 1 (c), 2 (d); every pair has a sum of at most 3, so all 6 pairs are counted at `b`, which is the right answer. The pair (c, d) is also counted again at the centroid of its own piece `{c, d}` (distances 0 and 1), so adding the pieces gives 7 and not 6. Subtracting the pairs of the child piece measured **from `b`** (the distances 1 and 2, sum 3 <= 3, one pair) removes the double count: 6 + 1 - 1 = 6.

**H.** (a) Root prefix sums + LCA. (b) HLD + segment tree. (c) Centroid decomposition. (d) Euler tour + Fenwick (Topic 24); HLD also works. (e) Centroid decomposition, `NearestMarked`.

---

## Cheat sheet

- **HLD:** heavy child = largest subtree; chains are consecutive positions; a root path has at most log2 n light edges; subtree = `[pos, pos + size - 1]`.
- **Path loop:** while heads differ, take the node whose head is deeper, emit `[pos[head], pos[u]]`, go to `parent[head]`; then emit the stretch on the shared chain (start one later for edges).
- **Costs:** build O(n); `lca` O(log n); path query O(log^2 n); update O(log n); k-th ancestor O(log n).
- **Centroid:** remove a node that leaves pieces of at most n / 2; height at most floor(log2 n) + 1; keep `dist[level][v]` to every centroid ancestor; build O(n log n).
- **Nearest marked:** `best[a]` per centroid; mark and query walk the ancestors, O(log n) each. **Pairs within K:** per centroid, sorted distances and two pointers, minus the same-child-piece pairs.
- **Measured (10^6 nodes):** HLD build 41 to 84 ms, centroid build 0.9 to 3.3 s, path sum about 120 ns to 1.4 microseconds per query, nearest-marked about 0.2 to 0.6 microseconds, HLD more than 100,000 times faster than walking on a path.

---

## Sources

- Heavy-light decomposition and centroid decomposition are standard: see the cp-algorithms articles on both (named from memory, not re-read for this file); the idea of heavy-light paths goes back to Sleator and Tarjan's work on dynamic trees (from memory). The code is my own implementation, tested as above.
- The claims about link-cut trees, about how often these topics appear in interviews, and about non-commutative combines are from general knowledge and were not measured or implemented here. All timings are measured here.
- Problems by name, from memory: SPOJ QTREE (path maximum with updates), Codeforces 342E "Xenia and Tree" (nearest red node), POJ 1741 "Tree" (pairs within distance K), Codeforces 321C "Ciel the Commander" (centroid decomposition); LeetCode: Kth Ancestor of a Tree Node.
