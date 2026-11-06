# DSA 07.32: Hash and Spatial Trees

*Part of the [Trees plan](00_plan.md) · Time: about 6 hours (4 reading, 2 problems; this topic is at awareness level: know the idea, the cost and where each is used) · You need: [Binary trees](01_binary_tree_fundamentals.md) (recursion on a tree), [Segment tree](21_segment_tree_point_update_range_query.md) (a tree that summarises a range, as a Merkle tree summarises its leaves) and [B-trees and B+ trees](29_b_trees_and_b_plus_trees.md) (wide nodes, which the R-tree and the kernel's hash tree both use). There is no companion demo file: every structure is listed in this file (SHA-256 excepted, see below) and every listing is tested code.*

Two kinds of tree that are not ordered maps. A **Merkle tree** puts a hash in every node, so that one 32-byte root commits to a whole data set, a **proof** of one item is a few hashes, and two copies of a data set can find where they differ without sending the data. **Spatial trees** (k-d tree, quadtree, R-tree) split space instead of keys, so that "what is near this point" and "what is inside this rectangle" do not scan everything.

By the end you can:

- build a Merkle tree in the Certificate Transparency shape, make and check an audit path, update a leaf in O(log n), and find the differing leaves of two replicas;
- say why leaves and inner nodes are hashed with different prefixes, and where Merkle trees are used (git, Certificate Transparency, replica repair, the kernel's dm-verity);
- build a static k-d tree, answer nearest-neighbour and box queries, and say why it stops working in high dimension (measured);
- build a point quadtree and an R-tree packed by Sort-Tile-Recursive, and say which suits which data;
- mark honestly what is from memory.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 267,914 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: SHA-256 against four standard vectors (empty, "abc", the 56-byte one, a million `a`); the Merkle tree against a separate recursive definition for every size from 1 to 130, with every audit path checked and then rejected when the leaf, the position, a hash or the length was wrong; leaf updates against a rebuild; the diff against random sets of changed leaves; the k-d tree in 1, 2, 3 and 5 dimensions, the quadtree and the R-tree against a brute-force scan, including grids with many equal coordinates and points on box edges. Finally 77 hand-made mutants of the four listings and the SHA-256 routine were run against the tests: **72 were caught and 5 are equivalent changes** (listed in section 8).

**Data and timing.** All data is **synthetic** and seeded: random 32-byte-ish strings for the Merkle tree; 100,000 random points in the unit square (uniform) or around 16 Gaussian clusters of spread 0.02 (clustered); box queries are centred on a random stored point. Machine: a cloud container, Intel Xeon at 2.10 GHz, 2 cores, GCC 13.3.0, `-O2`. Times are **run 1 / run 2** and a claim is made only when it holds in both; they are this machine's, not yours. Counts (nodes visited, hashes, comparisons) are identical in both runs.

---

## 1. Merkle trees

**The idea.** Hash every leaf; hash every pair of children into their parent; the root hash then depends on every leaf and on their order. Three consequences follow. **Tamper evidence**: change one byte anywhere and the root changes. **Proofs**: to show that item i is in the set whose root you trust, send the item and the **sibling hashes** on its way to the root (an *audit path*, about log2 n hashes, 32 bytes each); the verifier recomputes the root. **Cheap comparison**: two replicas compare root hashes; if equal, they are identical; if not, compare children, and descend only into the subtrees that differ.

The tree below has the shape of RFC 6962 (Certificate Transparency): a list of n > 1 leaves is cut at the largest power of two below n, so the left part is always a full tree. Leaves are hashed as `SHA-256(0x00 || data)` and inner nodes as `SHA-256(0x01 || left || right)`. The **two prefixes** matter: without them, the 64 bytes `left || right` of an inner node could be presented as the data of a leaf, and the same root would "prove" a leaf that never existed (a second-preimage attack; the test checks that the two hashes differ).

```cpp
// A Merkle tree over a list of byte strings, shaped as in RFC 6962 (Certificate Transparency): a list of n > 1 leaves is cut at the
// largest power of two below n. Leaf hash = SHA-256(0x00 || data); node hash = SHA-256(0x01 || left || right). The two different
// prefixes stop a node from being passed off as a leaf.
inline Hash hash_leaf(std::string_view d) { std::vector<uint8_t> v{0x00}; v.insert(v.end(), d.begin(), d.end()); return sha256(v.data(), v.size()); }
inline Hash hash_node(const Hash& a, const Hash& b) { std::vector<uint8_t> v{0x01}; v.insert(v.end(), a.begin(), a.end()); v.insert(v.end(), b.begin(), b.end()); return sha256(v.data(), v.size()); }

struct Merkle {
    struct Node { Hash h; size_t lo, hi; int l = -1, r = -1; };     // covers leaves [lo, hi)
    std::vector<Node> nd; int root = -1; size_t n = 0;
    static size_t split(size_t m) { size_t k = 1; while (k * 2 < m) k *= 2; return k; }   // largest power of two below m (m >= 2)
    int make(size_t lo, size_t hi, const std::vector<std::string>& data) {
        if (hi - lo == 1) { nd.push_back({hash_leaf(data[lo]), lo, hi}); return (int)nd.size() - 1; }
        size_t k = split(hi - lo); int l = make(lo, lo + k, data), r = make(lo + k, hi, data);
        nd.push_back({hash_node(nd[l].h, nd[r].h), lo, hi, l, r}); return (int)nd.size() - 1;
    }
    void build(const std::vector<std::string>& data) { n = data.size(); nd.clear(); root = n ? make(0, n, data) : -1; }
    Hash root_hash() const { return n ? nd[root].h : sha256(nullptr, 0); }
    // Replace leaf i and recompute the hashes above it: one hash per level, so O(log n). `hashes` counts them.
    void set_leaf(size_t i, std::string_view d, size_t* hashes = nullptr) {
        std::vector<int> path; int x = root;
        while (nd[x].l >= 0) { path.push_back(x); x = i < nd[nd[x].l].hi ? nd[x].l : nd[x].r; }
        nd[x].h = hash_leaf(d); size_t c = 1;
        for (size_t j = path.size(); j-- > 0;) { nd[path[j]].h = hash_node(nd[nd[path[j]].l].h, nd[nd[path[j]].r].h); ++c; }
        if (hashes) *hashes = c;
    }
    // The audit path of leaf m: the sibling subtree hashes on the way from the leaf up to the root (bottom first).
    std::vector<Hash> audit_path(size_t m) const {
        std::vector<Hash> p; int x = root;
        while (nd[x].l >= 0) { bool left = m < nd[nd[x].l].hi; p.push_back(nd[left ? nd[x].r : nd[x].l].h); x = left ? nd[x].l : nd[x].r; }
        std::reverse(p.begin(), p.end()); return p;
    }
    // Does leaf hash `leaf` at position `index` of a tree of `size` leaves, with this path, give `root`? (The iterative check of RFC 9162.)
    static bool verify(const Hash& leaf, size_t index, size_t size, const std::vector<Hash>& path, const Hash& root) {
        if (index >= size) return false;
        size_t fn = index, sn = size - 1; Hash r = leaf;
        for (const Hash& p : path) {
            if (sn == 0) return false;
            if ((fn & 1) || fn == sn) { r = hash_node(p, r); if (!(fn & 1)) while (!(fn & 1) && fn != 0) { fn >>= 1; sn >>= 1; } }
            else r = hash_node(r, p);
            fn >>= 1; sn >>= 1;
        }
        return sn == 0 && r == root;
    }
    // Anti-entropy: the leaves in which two trees of the same size differ, found by comparing hashes top-down; subtrees with equal hashes are skipped.
    static void diff(const Merkle& a, const Merkle& b, int x, int y, std::vector<size_t>& out, size_t& compares) {
        ++compares;
        if (a.nd[x].h == b.nd[y].h) return;
        if (a.nd[x].l < 0) { out.push_back(a.nd[x].lo); return; }
        diff(a, b, a.nd[x].l, b.nd[y].l, out, compares); diff(a, b, a.nd[x].r, b.nd[y].r, out, compares);
    }
};
```

SHA-256 itself (`sha256`, about 40 lines, FIPS 180-4) is in the tested header and is not printed here: it is the standard algorithm, checked against the four standard vectors. Read the listing in this order: `make` (the shape), `audit_path` (the sibling at every level, bottom first), `verify` (the iterative check of RFC 9162, named from memory but tested against the recursive definition for every size and index up to 130), `set_leaf` (one hash per level) and `diff`.

**A run** (five leaves a to e; each node is `[lo,hi)` and the first four bytes of its hash, in hex):

```
merkle node [0,1) 022a6979 (leaf)
merkle node [1,2) 57eb3561 (leaf)
merkle node [0,2) b137985f
merkle node [2,3) 597fcb31 (leaf)
merkle node [3,4) d070dc5b (leaf)
merkle node [2,4) dbbd68c3
merkle node [0,4) 33376a3b
merkle node [4,5) 2824a7cc (leaf)
merkle node [0,5) fe14a542
merkle root fe14a542
merkle audit path of leaf 2 : d070dc5b b137985f 2824a7cc
merkle verify c at 2 of 5 : 1 ; wrong data : 0 ; wrong index : 0 ; last hash flipped : 0
merkle set_leaf(2, C) : root 34e592ce ; hashes recomputed 4 ; leaf 4 and node [0,2) unchanged : 1
merkle diff of 8 leaves with leaf 5 changed : found 5 ; hash comparisons 7
```

Five leaves split as 4 and 1: the left part is the full tree over a to d, and e hangs directly off the root. The audit path of leaf 2 (`c`) is the three siblings met on the way up: leaf 3 (`d070dc5b`), the node over leaves 0 and 1 (`b137985f`), and leaf 4 (`2824a7cc`). The verifier hashes `c`, combines it with `d` on the right, with `[0,2)` on the left, and with `e` on the right, and compares with the root. The same path with the wrong data, the wrong index, or one flipped bit fails. Replacing leaf 2 recomputes four hashes (the leaf and the three above it) and leaves leaf 4 and `[0,2)` alone. For the diff, two trees of eight leaves that differ in leaf 5 compare the roots (differ), then the two children of the root, then the two below the differing one, then the two leaves: **7 comparisons**, 1 + 2 x 3, and the answer is leaf 5.

**What it costs** (n = 1,000,000 leaves of about 14 bytes; 20,000 random updates and proofs; **my own SHA-256**, plain C++, so the times say how this code behaves, not how fast SHA-256 can be):

| Measure | Value |
|---|---|
| Build the tree (ms), run 1 / run 2 | 1,512.9 / 1,377.8 |
| Update one leaf (ns) | 17,274 / 17,688 |
| Hashes recomputed per update, max and mean | 21 and 20.98 |
| Audit path length, maximum (hashes) | 20 |
| Verify one proof (ns) | 15,858 / 18,968 |
| Diff against a copy with 999 changed leaves: hash comparisons | 20,185 (a full comparison of the nodes needs 1,999,999) |

Reading the table: an update recomputes **one hash per level** (21 including the leaf, of the 1,999,999 nodes in the tree), and a proof is **at most 20 hashes**, 640 bytes. The diff is the anti-entropy step: of 999 changed leaves among a million, the descent made about 20,000 hash comparisons instead of the 1,999,999 a node-by-node comparison would need, and it found exactly the changed leaves. It is O(k log n) comparisons for k changes; if most leaves differ, it is no better than a full scan.

**Where it is used** *(from memory; I did not re-read these sources)*. **Git** names every object by its hash, and a tree object lists the hashes of its entries, so a commit hash commits to the whole tree of files (a Merkle DAG, not a binary tree). **Certificate Transparency** (RFC 6962 and 9162) keeps a public append-only log as exactly the tree above, with inclusion proofs and **consistency proofs** (that a later tree extends an earlier one); the consistency proof is not implemented here. **Dynamo-style stores** (Cassandra's repair, for example) build a Merkle tree over key ranges on each replica and compare them to find ranges to repair. Bitcoin's block tree duplicates the last node of an odd level instead of splitting at a power of two, which is a different shape with a known ambiguity (two different transaction lists can give the same root; CVE-2012-2459). In the Linux kernel, **dm-verity** (and, per file, **fs-verity**) checks every block read from a read-only device against a hash tree whose root is trusted: with 4 KiB blocks and 32-byte digests, one hash block holds 128 hashes, so the tree is **128-ary and only a few levels deep**; a read costs one hash block per level plus the data block. That is the wide-node idea of Topic 29 applied to a Merkle tree.

## 2. The k-d tree

**The idea.** A binary search tree over points in k dimensions: at depth d split on coordinate d mod k, at the median. The tree below is the array itself (`a[lo, hi)` has its median at `mid` and the halves on either side), so it has no pointers and is balanced by construction. To find the **nearest neighbour** of q: go down the side of the splitting plane that holds q, remember the best point seen, and on the way back visit the far side only if the plane is **closer than the best distance so far** (the far side cannot hold anything nearer if even the plane is farther). To count the points in a **box**, skip a half when the box lies wholly on the other side of the median.

```cpp
// A static k-d tree: at each level the points are split at the median of one coordinate, the axis cycling with depth.
// The tree is the array itself: a[lo, hi) has its median at mid = (lo + hi) / 2, the left half below it and the right half above it.
template <int D>
struct KdTree {
    using P = std::array<double, D>;
    std::vector<P> a;
    void build(std::vector<P> pts) { a = std::move(pts); rec(0, a.size(), 0); }
    void rec(size_t lo, size_t hi, int axis) {
        if (hi - lo <= 1) return;
        size_t mid = (lo + hi) / 2;
        std::nth_element(a.begin() + lo, a.begin() + mid, a.begin() + hi, [axis](const P& x, const P& y) { return x[axis] < y[axis]; });
        rec(lo, mid, (axis + 1) % D); rec(mid + 1, hi, (axis + 1) % D);
    }
    static double dist2(const P& x, const P& y) { double s = 0; for (int i = 0; i < D; ++i) s += (x[i] - y[i]) * (x[i] - y[i]); return s; }
    // The nearest point to q: go down the near side first, and visit the far side only if the splitting plane is closer than the best so far.
    void nn(size_t lo, size_t hi, int axis, const P& q, double& best, size_t& at, size_t& visited) const {
        if (lo >= hi) return;
        size_t mid = (lo + hi) / 2; ++visited;
        double d = dist2(a[mid], q); if (d < best) { best = d; at = mid; }
        double diff = q[axis] - a[mid][axis]; int next = (axis + 1) % D;
        if (diff < 0) { nn(lo, mid, next, q, best, at, visited); if (diff * diff < best) nn(mid + 1, hi, next, q, best, at, visited); }
        else          { nn(mid + 1, hi, next, q, best, at, visited); if (diff * diff < best) nn(lo, mid, next, q, best, at, visited); }
    }
    double nearest(const P& q, P& out, size_t* visited = nullptr) const {     // squared distance; the tree must not be empty
        double best = 1e300; size_t at = 0, v = 0; nn(0, a.size(), 0, q, best, at, v); out = a[at]; if (visited) *visited = v; return best;
    }
    // Count the points in the closed box [lo, hi]; a subtree is skipped when the box lies wholly on the other side of the median.
    size_t count(size_t l, size_t h, int axis, const P& lo, const P& hi, size_t& visited) const {
        if (l >= h) return 0;
        size_t mid = (l + h) / 2, c = 0; ++visited; bool in = true;
        for (int i = 0; i < D; ++i) if (a[mid][i] < lo[i] || a[mid][i] > hi[i]) in = false;
        c += in; int next = (axis + 1) % D;
        if (lo[axis] <= a[mid][axis]) c += count(l, mid, next, lo, hi, visited);
        if (hi[axis] >= a[mid][axis]) c += count(mid + 1, h, next, lo, hi, visited);
        return c;
    }
    size_t count_box(const P& lo, const P& hi, size_t* visited = nullptr) const { size_t v = 0, c = count(0, a.size(), 0, lo, hi, v); if (visited) *visited = v; return c; }
};
```

**A run** (the six points of the usual example, k = 2; `nearest` and `count_box` also report the nodes visited):

```
kd order : (2,3) (5,4) (4,7) (7,2) (8,1) (9,6)
kd nearest to (9,2) : (8,1) dist2 2 ; nodes visited 3 of 6
kd nearest to (4,6) : (4,7) dist2 1 ; nodes visited 3 of 6
kd box [3,8] x [1,5] : 3 points ; nodes visited 6
```

The median of x is 7, so the root is (7,2); the left three points are split on y (median (5,4)) and the right two on y as well (median (9,6)). For q = (9,2) the search goes right of the root, then to (9,6), then to (8,1) with distance squared 2; the plane y = 6 is at distance 4 and x = 7 at 2, neither below 2, so the far sides are never visited: 3 of 6 nodes.

**It stops working in high dimension.** 100,000 uniform random points in D dimensions, 2,000 nearest-neighbour queries, each answer checked against a scan:

| D | Nodes visited per query | Fraction of n | k-d tree (ns) | Scan (ns) |
|---|---|---|---|---|
| 2 | 22.9 | 0.0002 | 368 / 382 | 155,740 / 164,306 |
| 4 | 75.7 | 0.0008 | 1,431 / 1,415 | 225,269 / 206,286 |
| 8 | 1,520.2 | 0.0152 | 25,511 / 30,505 | 429,798 / 358,394 |
| 16 | 92,010.5 | 0.9201 | 1,787,331 / 1,752,559 | 867,614 / 870,612 |

The number of nodes visited grows from 23 to 92,000: with 10^5 points the tree has only about 17 levels, so in 16 dimensions each coordinate is split about once, the planes are rarely closer than the best distance, and the search visits **92% of the points** and costs about **twice a scan** in both runs. The rule of thumb is that a k-d tree wants n much larger than 2^D. For high-dimensional vectors (embeddings) practice uses approximate methods *(from memory: graph and clustering indexes)*, not an exact tree.

## 3. The quadtree

**The idea.** Cut the square into four equal quadrants; a leaf holds up to CAP points and splits into four children when it overflows. It is a trie on the bits of the coordinates (Topic 18 with 2 bits per level), so the cuts are at fixed positions, not at medians: insertion needs no rebalancing, but the depth follows the data. Many equal points would split for ever, so there is a depth limit.

```cpp
// A point quadtree on [0,1) x [0,1): a leaf holds up to CAP points; a fuller leaf splits into four equal quadrants.
// Splitting stops at depth MAXD, so many equal points cannot recurse for ever.
struct QuadTree {
    using P = std::array<double, 2>; static constexpr int CAP = 4, MAXD = 24;
    struct Node {
        double x0, y0, x1, y1; int kid[4] = {-1, -1, -1, -1}; std::vector<P> pts;
        Node(double a, double b, double c, double d) : x0(a), y0(b), x1(c), y1(d) {}
    };
    std::vector<Node> nd; size_t n = 0;
    QuadTree() { nd.push_back({0, 0, 1, 1}); }
    int quad(int i, const P& p) const { return (p[0] >= (nd[i].x0 + nd[i].x1) / 2) + 2 * (p[1] >= (nd[i].y0 + nd[i].y1) / 2); }
    void split(int i) {
        double x0 = nd[i].x0, y0 = nd[i].y0, x1 = nd[i].x1, y1 = nd[i].y1, mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
        const double bx[4][4] = {{x0, y0, mx, my}, {mx, y0, x1, my}, {x0, my, mx, y1}, {mx, my, x1, y1}};
        for (int q = 0; q < 4; ++q) { nd.push_back({bx[q][0], bx[q][1], bx[q][2], bx[q][3]}); nd[i].kid[q] = (int)nd.size() - 1; }
        std::vector<P> old; old.swap(nd[i].pts);
        for (const P& p : old) nd[nd[i].kid[quad(i, p)]].pts.push_back(p);
    }
    void insert(const P& p) {
        int i = 0, depth = 0;
        for (;;) {
            if (nd[i].kid[0] < 0) { if ((int)nd[i].pts.size() < CAP || depth >= MAXD) { nd[i].pts.push_back(p); ++n; return; } split(i); }
            i = nd[i].kid[quad(i, p)]; ++depth;
        }
    }
    size_t count(int i, const P& lo, const P& hi, size_t& visited) const {       // points in the closed box [lo, hi]
        const Node& v = nd[i]; ++visited;
        if (v.x0 > hi[0] || v.x1 <= lo[0] || v.y0 > hi[1] || v.y1 <= lo[1]) return 0;
        size_t c = 0;
        if (v.kid[0] < 0) { for (const P& p : v.pts) c += p[0] >= lo[0] && p[0] <= hi[0] && p[1] >= lo[1] && p[1] <= hi[1]; return c; }
        for (int q = 0; q < 4; ++q) c += count(v.kid[q], lo, hi, visited);
        return c;
    }
    size_t count_box(const P& lo, const P& hi, size_t* visited = nullptr) const { size_t v = 0, c = count(0, lo, hi, v); if (visited) *visited = v; return c; }
};
```

**A run** (CAP = 4; four points in the lower left quadrant, one in the upper right, then a sixth, (0.12, 0.12), again lower left):

```
quad nodes 9 : [0.5,1)x[0.5,1){(0.6,0.6)} [0,0.25)x[0,0.25){(0.1,0.1)(0.12,0.12)} [0.25,0.5)x[0,0.25){(0.3,0.2)} [0,0.25)x[0.25,0.5){(0.2,0.3)} [0.25,0.5)x[0.25,0.5){(0.4,0.4)}
quad box [0.05,0.15]^2 : 2 points ; nodes visited 9 of 9
```

The first four points fill one leaf. The fifth, in the other half, splits the root into four (5 nodes); the sixth overflows the lower-left leaf, which splits again (9 nodes in all). The box query enters all 9 nodes, counting the ones it rejects at once: the root, its four children (three are rejected on entry) and the four under the lower-left quadrant. On a large tree the same rejection removes whole quadrants of thousands of points.

## 4. The R-tree

**The idea.** A B-tree whose keys are **rectangles**. Each node holds up to M children and the smallest rectangle that contains all of them; a query descends into every child whose rectangle meets the query. Rectangles of siblings may **overlap**, so a query may go down several branches, but the nodes are wide (M = 16 here, hundreds on disk) and the tree is shallow. The data decides the shape, not a fixed grid. The tree below is **static** and packed by Sort-Tile-Recursive: sort by x, cut into vertical slices, sort each slice by y, cut into groups of M; each group is a leaf; pack the leaves the same way, level by level.

```cpp
// A static R-tree for points, packed by Sort-Tile-Recursive: sort by x, cut into vertical slices, sort each slice by y, cut into groups of M;
// each group is a node with its bounding rectangle; then pack the nodes the same way, level by level, until one is left.
struct RTree {
    using P = std::array<double, 2>; static constexpr int M = 16;
    struct Node { double lo[2], hi[2]; int first, cnt; bool leaf; };           // children: ch[first, first + cnt), or points pts[first, first + cnt)
    std::vector<P> pts; std::vector<Node> nd; std::vector<int> ch; int root = -1;
    template <class V, class Key>
    static void tile(V& items, size_t m, Key key, std::vector<std::pair<size_t, size_t>>& groups) {      // order items by STR, give the groups of <= M
        size_t leaves = (items.size() + m - 1) / m, S = (size_t)std::ceil(std::sqrt((double)leaves)), per = S * m;
        std::sort(items.begin(), items.end(), [&](const auto& x, const auto& y) { return key(x, 0) < key(y, 0); });
        for (size_t s = 0; s < items.size(); s += per) {
            size_t e = std::min(items.size(), s + per);
            std::sort(items.begin() + s, items.begin() + e, [&](const auto& x, const auto& y) { return key(x, 1) < key(y, 1); });
            for (size_t g = s; g < e; g += m) groups.push_back({g, std::min(e, g + m)});
        }
    }
    void build(std::vector<P> p) {
        pts = std::move(p); nd.clear(); ch.clear(); root = -1; if (pts.empty()) return;
        std::vector<std::pair<size_t, size_t>> groups; tile(pts, M, [](const P& x, int a) { return x[a]; }, groups);
        std::vector<int> level;
        for (auto& g : groups) {
            Node v{{1e300, 1e300}, {-1e300, -1e300}, (int)g.first, (int)(g.second - g.first), true};
            for (size_t i = g.first; i < g.second; ++i) for (int a = 0; a < 2; ++a) { v.lo[a] = std::min(v.lo[a], pts[i][a]); v.hi[a] = std::max(v.hi[a], pts[i][a]); }
            nd.push_back(v); level.push_back((int)nd.size() - 1);
        }
        while (level.size() > 1) {
            groups.clear(); tile(level, M, [&](int x, int a) { return nd[x].lo[a] + nd[x].hi[a]; }, groups); std::vector<int> up;
            for (auto& g : groups) {
                Node v{{1e300, 1e300}, {-1e300, -1e300}, (int)ch.size(), (int)(g.second - g.first), false};
                for (size_t i = g.first; i < g.second; ++i) { ch.push_back(level[i]); for (int a = 0; a < 2; ++a) { v.lo[a] = std::min(v.lo[a], nd[level[i]].lo[a]); v.hi[a] = std::max(v.hi[a], nd[level[i]].hi[a]); } }
                nd.push_back(v); up.push_back((int)nd.size() - 1);
            }
            level.swap(up);
        }
        root = level[0];
    }
    size_t count(int i, const P& lo, const P& hi, size_t& visited) const {     // points in the closed box [lo, hi]
        const Node& v = nd[i]; ++visited; size_t c = 0;
        if (v.lo[0] > hi[0] || v.hi[0] < lo[0] || v.lo[1] > hi[1] || v.hi[1] < lo[1]) return 0;
        if (v.leaf) { for (int k = 0; k < v.cnt; ++k) { const P& p = pts[v.first + k]; c += p[0] >= lo[0] && p[0] <= hi[0] && p[1] >= lo[1] && p[1] <= hi[1]; } return c; }
        for (int k = 0; k < v.cnt; ++k) c += count(ch[v.first + k], lo, hi, visited);
        return c;
    }
    size_t count_box(const P& lo, const P& hi, size_t* visited = nullptr) const { size_t v = 0, c = root < 0 ? 0 : count(root, lo, hi, v); if (visited) *visited = v; return c; }
};
```

The test pins one case: 17 collinear points make two leaves (16 and 1 points) and a root, 3 nodes. Dynamic R-trees (insert and delete with node splits and reinsertion; the R*-tree is the usual variant) are more code and are not implemented here *(from memory)*; database engines use them for geometry columns (PostGIS indexes through PostgreSQL's GiST, and SQLite has an `rtree` module).

## 5. Comparing the three on box queries

100,000 points, 10,000 box queries centred on stored points, results checked against each other and against a scan on 1,000 of them. **Visited** is nodes touched per query; the units differ: a k-d "node" is one point, a quadtree node is a quadrant holding up to 4 points, an R-tree node holds up to 16 children.

Nodes: for the uniform data the quadtree has 74,801 nodes and the R-tree 6,669; for the clustered data 72,929 and 6,669. Build time (ms, run 1 / run 2, uniform): k-d tree 16.9 / 17.3, quadtree 27.4 / 28.6, R-tree 11.8 / 11.9.

| Data | Box side | Results per query | k-d visited | Quadtree visited | R-tree visited |
|---|---|---|---|---|---|
| Uniform | 0.003 | 1.89 | 23.4 | 38.7 | 58.5 |
| Uniform | 0.03 | 89.50 | 151.9 | 149.9 | 85.4 |
| Clustered | 0.003 | 12.41 | 46.6 | 65.3 | 79.1 |
| Clustered | 0.03 | 1047.83 | 1221.9 | 956.6 | 219.5 |

Time per query (ns, run 1 / run 2); the last column is a scan of all 100,000 points:

| Data | Box side | k-d tree | Quadtree | R-tree | Scan |
|---|---|---|---|---|---|
| Uniform | 0.003 | 397 / 382 | 740 / 747 | 664 / 675 | 351,908 / 346,253 |
| Uniform | 0.03 | 1,519 / 1,494 | 5,806 / 4,516 | 1,988 / 1,632 | 373,716 / 371,046 |
| Clustered | 0.003 | 933 / 655 | 2,003 / 1,566 | 1,150 / 1,171 | 338,139 / 354,011 |
| Clustered | 0.03 | 5,438 / 6,745 | 22,690 / 32,185 | 4,633 / 6,036 | 323,950 / 405,244 |

What both runs agree on:

- **Every tree is hundreds of times faster than a scan** when the box holds a dozen points or fewer (a scan is about 350,000 ns).
- **The k-d tree is the fastest on the uniform data and on small boxes**; the quadtree is the slowest in every row, 1.1 to 5.4 times the R-tree's time: it has 11 times as many nodes as the R-tree (74,801 against 6,669), each its own allocation, and a per-leaf `std::vector`.
- **The R-tree wins where a box holds many points in dense data**: for the large box on clustered data it visits 219 nodes against 1,222 for the k-d tree and is faster in time in both runs, because its leaves hold 16 points that are tested in one pass and whole subtrees are accepted or rejected at once.
- The quadtree's node count depends on the data (74,801 nodes for uniform points, 72,929 for clustered); the R-tree has the same count for both (6,669) because it packs by count, not by position.

## 6. Choosing

- **Prove membership or detect tampering in a large set; compare replicas:** a Merkle tree. Cut at a power of two (or use a wide fan-out for storage), and prefix leaves differently from nodes.
- **Static points, nearest neighbour or box, up to about 10 dimensions:** a k-d tree.
- **Points inserted over time in 2D or 3D with a fixed extent (a game world, a map tile index):** a quadtree (octree in 3D); simple, no rebalancing, but depth follows the data.
- **Rectangles or shapes, large data, disk-resident, overlap allowed:** an R-tree (or a database's spatial index built on it).
- **High-dimensional vectors:** not an exact tree; scan, or an approximate index.
- **Only a handful of points:** a scan; the tree's overhead is not paid back.

## 7. Common mistakes

- **No domain separation in a Merkle tree.** Hashing leaves and inner nodes the same way lets one be mistaken for the other.
- **Odd sizes.** Duplicating the last node of an odd level (Bitcoin's choice) makes two different lists share a root; the power-of-two split does not.
- **Trusting a path without the position.** An audit path means something only together with the leaf's index and the tree size; `verify` takes both and rejects a path of the wrong length.
- **Hashing to compare big data when the data is already trusted.** A Merkle tree helps when the comparison crosses a slow link or must be authenticated; for two files on one disk, compare the files.
- **k-d tree without a median split.** Splitting at the midpoint of the range, or at the first point inserted, gives an unbalanced tree on clustered data; `nth_element` on the median keeps it balanced.
- **Pruning with the wrong distance.** The far side is skipped only when the **squared** plane distance is not below the **squared** best distance; mixing a squared and a plain distance prunes wrongly.
- **Half-open quadrants against closed boxes.** A quadrant owns its low edge and not its high edge; a box edge exactly on a boundary must still find the points on it (the tests put points on boundaries).
- **Unbounded quadtree depth.** Equal points never separate; without a depth limit insertion does not end.
- **Expecting the R-tree's rectangles not to overlap.** They do; a query may visit several branches.

The equivalent mutants (changes that do not alter any output, so no test can catch them): (1) `root = 0` instead of `-1` for an empty Merkle tree, since `root` is never read when `n == 0`; (2) removing the early `sn == 0` exit in `verify`, because a path that is too long hashes to a different value and fails the final comparison anyway; (3) dropping `sn == 0` from the final test of `verify`, for the same reason (these two checks only matter if SHA-256 collides); (4) `v.y1 < lo[1]` instead of `<=` in the quadtree box test, which visits one more quadrant and rejects it later, so only the cost changes; (5) `(items.size() + m) / m` instead of a ceiling for the number of leaves in `tile`, which changes how many slices are cut, not any answer. Every other change was caught by a test.

## 8. Questions to answer out loud

1. **What does the root of a Merkle tree commit to?** Every leaf and their order: change any leaf and the root changes.
2. **How big is a proof and how is it checked?** About log2 n sibling hashes; the verifier hashes the item, combines with each sibling on the correct side (the index says which), and compares with the trusted root.
3. **Why different prefixes for leaves and nodes?** So that an inner node's bytes cannot be passed off as a leaf's data (second preimage).
4. **How do two replicas find their differences?** Compare roots; if they differ compare children; descend only into differing subtrees: O(k log n) comparisons for k changed leaves.
5. **Where does the kernel use a Merkle tree?** dm-verity and fs-verity (from memory): a hash tree over the blocks, 128-ary for 4 KiB blocks and SHA-256, root trusted.
6. **How does a k-d tree find a nearest neighbour?** Descend to q's side, then on the way up visit the other side only if the splitting plane is nearer than the best so far.
7. **Why does it fail in 16 dimensions?** With n about 2^17 each coordinate is split about once, the planes rarely prune, and the search visits most of the points.
8. **What is the difference between a quadtree and a k-d tree?** The quadtree cuts at fixed positions into four, inserts without rebalancing and follows the data in depth; the k-d tree cuts at the median, is balanced, and is built once.
9. **What is an R-tree?** A B-tree of rectangles; siblings may overlap; wide nodes; used for geometry in databases.

## 9. Problems

**A.** In the Merkle trace, list the three steps a verifier takes to check leaf `c` at index 2 of 5, saying for each whether the path hash goes on the left or the right.

**B.** Why can the root of a Merkle tree that hashes leaves and inner nodes the same way be forged, and what does the `0x00` / `0x01` prefix change?

**C.** Two Merkle trees of 8 leaves differ in leaves 0 and 7. How many hash comparisons does the diff make? (The trace shows 7 for one changed leaf.)

**D.** A 1 GiB device is protected by a hash tree with 4,096-byte blocks and 32-byte SHA-256 digests, one hash block per 128 hashes. How many data blocks are there, how many hash blocks on each level, how many levels, and how many hash blocks does one verified read touch? Compare with a binary Merkle tree over the blocks.

**E.** Build the k-d tree of (2,3), (5,4), (9,6), (4,7), (8,1), (7,2) by hand and check it against the trace.

**F.** Walk the nearest-neighbour search for (9,2) and say why the left half of the root is never visited.

**G.** Why does the k-d tree visit 92% of 100,000 points in 16 dimensions, and what would you use instead?

**H.** Choose a structure: (a) verifying every block of a read-only partition as it is read; (b) finding which key ranges differ between two replicas of a store; (c) nearest charging station among 10^5 fixed points on a map; (d) a database of building footprints (rectangles, overlapping, on disk) queried by window; (e) exact nearest neighbours among 10^5 vectors of 16 numbers.

### Answers

**A.** Hash `c` as a leaf, r = H(0x00 || c). Step 1: the path hash `d070dc5b` is leaf 3, which is on the **right** of `c` (index 2 is even): r = H(0x01 || r || d). Step 2: `b137985f` is the node over leaves 0 and 1, on the **left** (the pair is the right child of the node over 0 to 3): r = H(0x01 || [0,2) || r). Step 3: `2824a7cc` is leaf 4, on the **right** (the left part is the full tree over a to d): r = H(0x01 || r || e). The result must equal `fe14a542`.

**B.** If both are `H(left || right)` or `H(data)` with no prefix, a leaf whose data is the 64 bytes `left || right` of an inner node hashes to that node's hash, so the tree of the leaves `left` and `right` and the tree with one leaf `left || right` have the same root. With the prefixes the two hashes differ (the test checks it), so a leaf cannot stand for a node.

**C.** Root pair: 1 comparison (differ). Its two child pairs: 2 (both differ). Under `[0,4)`: pairs `[0,2)` (differ) and `[2,4)` (same): 2; under `[4,8)`: `[4,6)` (same) and `[6,8)` (differ): 2. Then under `[0,2)`: leaves 0 (differ) and 1 (same): 2; under `[6,8)`: leaves 6 (same) and 7 (differ): 2. Total 1 + 2 + 4 + 4 = **11**.

**D.** 1 GiB / 4,096 = 262,144 data blocks. Each hash block covers 128 of them: 2,048 blocks on the first level; 2,048 / 128 = 16 on the second; 16 / 128 rounds up to 1 on the third. **Three levels**, and a verified read touches one hash block per level (3) plus the data block. A binary tree over the 262,144 blocks has 18 levels and a proof of 18 hashes; the 128-ary tree trades longer nodes for fewer, cache-line- or block-sized reads (Topic 29's argument).

**E.** x-coordinates 2, 4, 5, 7, 8, 9: the median at position 3 is 7, so the root is (7,2). Left: (2,3), (5,4), (4,7) split on y: median y is 4, so (5,4) with (2,3) below and (4,7) above. Right: (8,1) and (9,6) on y: the position `mid = (4 + 6) / 2 = 5` is (9,6), with (8,1) on its left. The array order (2,3) (5,4) (4,7) (7,2) (8,1) (9,6) is exactly the trace.

**F.** q = (9,2). Root (7,2): distance squared 4; q's x is 9, right of 7, so go right. Node (9,6): distance squared 16; q's y is 2, below 6, so go left to (8,1): distance squared 2, the best. Back at (9,6): the plane y = 6 is 4 away, squared 16, not below 2, so the other side is skipped. Back at the root: the plane x = 7 is 2 away, squared 4, not below 2, so the left half (three points) is never visited. Three nodes visited of six.

**G.** With n = 100,000 the tree has about 17 levels, so with 16 coordinates each is split about once; the splitting planes are rarely farther than the best distance found, which in 16 dimensions is large, so almost nothing is pruned and the traversal overhead makes it about twice a scan. For exact answers scan (with vectorised distance); for large collections use an approximate nearest-neighbour index (from memory: graph-based or cluster-based).

**H.** (a) A Merkle hash tree (dm-verity style). (b) A Merkle tree per replica over key ranges and the top-down diff. (c) A k-d tree (static, 2D). (d) An R-tree. (e) A scan, or an approximate index; a k-d tree would visit most of the points.

---

## Cheat sheet

- **Merkle:** leaf = H(0x00 || data), node = H(0x01 || l || r); split at the largest power of two below n; proof = about log2 n sibling hashes; update = one hash per level; diff = descend only where hashes differ, O(k log n).
- **dm-verity / fs-verity:** hash tree over 4 KiB blocks, 128 hashes per block (SHA-256), root trusted (from memory); 1 GiB is 3 levels.
- **k-d tree:** split at the median, axis by depth; nearest: near side first, far side only if plane distance squared < best; works to about 10 dimensions; 92% of points visited and 2x a scan at D = 16, n = 10^5.
- **Quadtree:** fixed quadrants, capacity per leaf, depth limit for equal points; follows the data.
- **R-tree:** B-tree of rectangles, overlap allowed, STR packing for static data; wins on large windows over dense data.
- **Measured:** a million-leaf Merkle tree updates with 21 hashes and proves with at most 20; the diff of 999 changes made 20,185 comparisons against 1,999,999.

---

## Sources

- Merkle trees: the CT shape, the two prefixes and the audit-path idea follow RFC 6962 and RFC 9162 (named from memory; the iterative `verify` is my recollection of RFC 9162 and is tested against the recursive definition for every size and index up to 130, not compared with the RFC text). Git, Cassandra repair, Bitcoin's duplication and CVE-2012-2459, dm-verity and fs-verity are from memory and were not re-read for this file.
- Spatial trees: the k-d tree is Bentley (1975), the R-tree Guttman (1984), Sort-Tile-Recursive packing Leutenegger, Lopez and Edgington (1997), and the R*-tree Beckmann et al. (1990); all named from memory. The code is my own implementation, tested as above.
- All timings and counts are measured here.
- Problems by name, from memory: LeetCode K Closest Points to Origin (973), The Skyline Problem (218), Rectangle Area (223), Design Search Autocomplete System (642), Count of Range Sum (327); and the 2D range-query problems on a Fenwick tree (Topic 20) for contrast.
