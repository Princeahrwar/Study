# DSA 07.21: Segment Tree I: Point Update, Range Query, Custom Merges and Descents

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (5 reading, 4 problems) · You need: [Fenwick tree](20_fenwick_tree.md) (what the segment tree generalises) and [Prefix sums, sparse tables and RMQ](19_prefix_sums_sparse_tables_and_rmq.md) (the static alternative). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- define a **monoid** (type, associative operation, identity) and build a segment tree over it, **recursively** and **bottom-up in 2n slots**;
- write point update and range query for both, and explain the three cases of the query and why it touches O(log n) nodes;
- write the merge for **maximum subarray sum**, **minimum with count** and **function composition**, and say why the last one needs the order of the pieces to be right;
- **descend** the tree to find the first index whose prefix aggregate satisfies a test, in O(log n);
- say, with measurements, what a segment tree costs against the Fenwick tree and the sparse table, and when each wins.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 1,539,437 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: both trees on **every range** of arrays of length 1 to 40, for seven monoids (sum, minimum, maximum, minimum with count, maximum subarray, affine maps, and **string concatenation**, which is not commutative), checked against independent references (sums by accumulation, maximum subarray by brute force over all subarrays, affine maps by applying the functions one at a time to four test inputs), with random point updates between rounds and a second tree built only by `set`; the layout (`4n + 4` and `2n` slots, the parent rule for every node, the leaf positions); `find_first` against a scan for every start and many thresholds, with updates; two 100,000-element trees against prefix sums and Kadane's algorithm; out-of-range arguments for every class; a descent over the non-commutative string monoid; minimum with count when the minimum is `INT_MAX`; and maximum subarray with values far below the stand-in for minus infinity. Finally 91 hand-made mutants of the code were run against the tests (a wrong child index, a swapped combine order, a lost identity, an off-by-one in the descent and so on): **88 were caught and 3 are equivalent changes** that no valid input can tell apart (listed after the common mistakes).

**Data and timing.** The array is **synthetic**: 1,000,000 random integers below 10^9 (sums are `long long`). Queries are random ranges `[l, r)` (average length about a third of the array), generated beforehand; 5,000,000 queries and 5,000,000 updates per measurement. Times are nanoseconds per operation (or milliseconds per build) on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 and 20), best of 5 repetitions (best of 3 for builds, updates and the descents), shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about systems practice are from general knowledge and say so.

---

## 1. What a segment tree is for

The Fenwick tree of Topic 20 handles sums with point updates. The sparse table of Topic 19 handles minimum, maximum and gcd with no updates. Neither can do **a range minimum with updates**, nor "the best subarray inside a range", nor "the product of a range of functions". The segment tree can do all three, because it only asks for one thing from the data: a **monoid**.

- a type `T`;
- an associative operation `op(a, b)` (the order of the two arguments matters, but the grouping does not);
- an identity `id()` with `op(id, x) = op(x, id) = x`.

`op` does **not** have to be commutative, and unlike the sparse table it does **not** have to be idempotent: the tree never combines overlapping pieces, so a sum or a product is fine.

Idea: store the answer for the whole array in a root, the answers for its two halves in its children, and so on down to single elements. A **point update** recomputes the O(log n) answers on one root-to-leaf path. A **range query** is covered by O(log n) stored answers, which are combined left to right.

| | Update | Range query | Memory | Operations |
|---|---|---|---|---|
| Fenwick tree (Topic 20) | O(log n) | O(log n), needs an inverse | n | sum, XOR |
| Sparse table (Topic 19) | none | O(1), needs idempotence | n log n | min, max, gcd |
| **Segment tree** | O(log n) | O(log n) | 2n to 4n | any monoid |

The monoids used in this tutorial:

```cpp
// A segment tree needs a MONOID: a type T, an associative operation op, and an identity id() with op(id, x) = op(x, id) = x.
// op need NOT be commutative (the tree always combines left to right) and need NOT be idempotent (no overlaps, unlike Topic 19).
struct SumM { using T = long long; static T id() { return 0; } static T op(T a, T b) { return a + b; } };
struct MinM { using T = int; static T id() { return std::numeric_limits<int>::max(); } static T op(T a, T b) { return std::min(a, b); } };
struct MaxM { using T = int; static T id() { return std::numeric_limits<int>::min(); } static T op(T a, T b) { return std::max(a, b); } };

// The minimum AND how many times it occurs.
struct MinCountM {
    struct T { int v; int c; };
    static T id() { return {std::numeric_limits<int>::max(), 0}; }
    static T op(T a, T b) {
        if (a.v < b.v) return a;
        if (b.v < a.v) return b;
        return {a.v, a.c + b.c};                                   // equal minima: the counts add
    }
};

// The maximum sum of a NON-EMPTY contiguous subarray.  A node keeps four numbers for its range:
//    sum = the total, pref = the best prefix, suf = the best suffix, best = the best subarray anywhere inside.
struct MaxSubM {
    struct T { long long sum, pref, suf, best; };
    static constexpr long long NEG = -(1LL << 60);                  // "minus infinity" that survives a few additions
    static T id() { return {0, NEG, NEG, NEG}; }
    static T leaf(long long x) { return {x, x, x, x}; }
    static T op(const T& a, const T& b) {
        return {a.sum + b.sum,
                std::max(a.pref, a.sum + b.pref),                   // the prefix stops in a, or runs through all of a into b
                std::max(b.suf, b.sum + a.suf),
                std::max({a.best, b.best, a.suf + b.pref})};        // inside a, inside b, or across the boundary
    }
};

// Function composition x -> a x + b (mod p): "apply f, then g".  NOT commutative: the order of the pieces is the whole point.
struct AffineM {
    struct T { long long a, b; };
    static constexpr long long MOD = 1000000007LL;
    static T id() { return {1, 0}; }
    static T op(T f, T g) { return {g.a * f.a % MOD, (g.a * f.b + g.b) % MOD}; }
};
```

---

## 2. The recursive tree

Node 1 is the root and covers `[0, n)`. A node that covers `[lo, hi)` has children `2v` and `2v + 1` covering `[lo, mid)` and `[mid, hi)`, with `mid = (lo + hi) / 2`; a node with `hi - lo = 1` is a leaf. The ranges are half-open throughout, which makes `mid` the end of one child and the start of the other with no `+1` or `-1`.

**Why `4n` slots.** The depth is `ceil(log2 n) + 1` levels, and node numbers on level `d` are below `2^(d+1)`. For `n` just above a power of two the last level needs numbers up to nearly `4n`. The code allocates `4n + 4`; the layout `2n` of section 4 is possible, but not with this numbering.

**The query has three cases** at every node, and these are the whole algorithm:

| Case | Test | Action |
|---|---|---|
| Disjoint | `r <= lo` or `hi <= l` | return `id()` |
| Fully inside | `l <= lo` and `hi <= r` | return the stored `t[v]` |
| Partial | otherwise | combine the answers of the two children |

**Why O(log n).** At each level at most two nodes are partial (the ones that contain `l` or `r`); every other visited node is either disjoint or fully inside and stops at once. So a query visits at most four nodes per level. A point update visits one node per level.

```cpp
// The classic recursive tree.  Node 1 is the root and covers [0, n); node v covers [lo, hi), its children 2v and 2v + 1 cover
//    [lo, mid) and [mid, hi).  The array has 4n slots, which is enough for any n (the depth is ceil(log2 n) + 1).
template <class M>
class SegTree {
public:
    using T = typename M::T;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
protected:
    std::size_t n_;
    std::vector<T> t_;
    friend struct TestAccess;
private:
    void build(std::size_t v, std::size_t lo, std::size_t hi, const std::vector<T>& a) {
        if (hi - lo == 1) { t_[v] = a[lo]; return; }
        std::size_t mid = (lo + hi) / 2;
        build(2 * v, lo, mid, a);
        build(2 * v + 1, mid, hi, a);
        t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);
    }
    void set(std::size_t v, std::size_t lo, std::size_t hi, std::size_t i, const T& x) {
        if (hi - lo == 1) { t_[v] = x; return; }
        std::size_t mid = (lo + hi) / 2;
        if (i < mid) set(2 * v, lo, mid, i, x); else set(2 * v + 1, mid, hi, i, x);
        t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);                    // recompute on the way back up
    }
    T query(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r) const {
        if (r <= lo || hi <= l) return M::id();                      // disjoint: contributes nothing
        if (l <= lo && hi <= r) return t_[v];                        // fully inside: take the stored answer
        std::size_t mid = (lo + hi) / 2;
        return M::op(query(2 * v, lo, mid, l, r), query(2 * v + 1, mid, hi, l, r));     // partial overlap: split
    }
public:
    explicit SegTree(std::size_t n = 0) : n_(n), t_(4 * n + 4, M::id()) {}
    explicit SegTree(const std::vector<T>& a) : n_(a.size()), t_(4 * a.size() + 4, M::id()) { if (n_) build(1, 0, n_, a); }
    std::size_t size() const { return n_; }
    void set(std::size_t i, const T& x) {                           // a[i] = x, in O(log n)
        if (i >= n_) throw std::out_of_range("SegTree::set");
        set(1, 0, n_, i, x);
    }
    T get(std::size_t i) const { return query(i, i + 1); }
    T query(std::size_t l, std::size_t r) const {                   // op over a[l..r), in O(log n); an empty range gives id()
        if (l > r || r > n_) throw std::out_of_range("SegTree::query");
        return n_ ? query(1, 0, n_, l, r) : M::id();
    }
};
```

For `a = 3 1 4 1 5 9 2 6` the sum tree in node order is `t[1..15] = 31 9 22 4 5 14 8 3 1 4 1 5 9 2 6` (root 31; its children 9 and 22; the leaves `t[8..15]` are the array) and the minimum tree is `1 1 2 1 1 5 2 3 1 4 1 5 9 2 6`. `set(2, 14)` changes the path from the leaf of position 2 to the root, nodes **1, 2, 5, 10** (`31 -> 41`, `9 -> 19`, `5 -> 15`, `4 -> 14`).

To see which stored answers a query uses, the program builds the tree over the monoid whose value is the interval it covers and whose `op` only joins adjacent intervals. The query `[1, 7)` joins, in the order the code performs them, `[4,6)+[6,7)`, `[1,2)+[2,4)` and `[1,4)+[4,7)`: it used the four stored answers `[1,2)`, `[2,4)`, `[4,6)` and `[6,7)`, and the final result is `[1,7)`.

---

## 3. Custom merges

Because the tree only needs a monoid, new problems are new `op` functions.

**Maximum subarray sum.** A node keeps four numbers for its range: the total `sum`, the best prefix `pref`, the best suffix `suf`, and the best subarray `best` anywhere inside. Merging a left piece `a` and a right piece `b`:

- `sum = a.sum + b.sum`;
- `pref = max(a.pref, a.sum + b.pref)` (the prefix stops in `a`, or covers `a` and continues);
- `suf = max(b.suf, b.sum + a.suf)`;
- `best = max(a.best, b.best, a.suf + b.pref)` (inside `a`, inside `b`, or across the boundary).

The identity uses a large negative number for the three "best" fields, so that it never wins; `NEG = -2^60` leaves room for the few additions that touch it. For `-2 1 -3 4 -1 2 1 -5 4`, the range `[0, 4)` is `(sum 0, pref 0, suf 4, best 4)`, the range `[4, 9)` is `(1, 2, 4, 4)`, and merging them gives `sum = 1`, `pref = max(0, 0 + 2) = 2`, `suf = max(4, 1 + 4) = 5`, `best = max(4, 4, 4 + 2) = 6`: the maximum subarray is `4 -1 2 1` with sum 6, found across the boundary. The program prints the same for the whole array `[0, 9)`.

**Minimum and how often it occurs.** `op` keeps the smaller value, and adds the counts when the values are equal. For `2 1 3 1 5` the range `[0, 5)` gives minimum 1 occurring 2 times, `[2, 5)` gives 1 once and `[0, 2)` gives 1 once.

**Function composition (not commutative).** A node holds a function `x -> a x + b (mod p)`; `op(f, g)` means "apply `f`, then `g`". For `f0 = (2, 3)`, `f1 = (5, 1)`, `f2 = (1, 7)` the composition `f0` then `f1` then `f2` is `x -> 10 x + 23`; the same three in the reverse order give `x -> 10 x + 75`. The slope is the same (multiplication commutes) but the offsets differ, which is exactly what a tree that combined pieces in the wrong order would get wrong. The tests apply the functions one at a time to four inputs and compare.

---

## 4. The bottom-up tree in 2n slots

The recursive tree spends four slots per element and a function call per node. The bottom-up design (Al.Cash's, widely known from the competitive-programming literature) is simpler: put the leaves at positions `n .. 2n-1` of an array of size `2n`, and define every other node by `t[v] = op(t[2v], t[2v+1])`. There is no recursion and no `lo`/`hi`.

- **Build:** copy the leaves, then compute `t[v]` for `v = n-1` down to 1. It is O(n).
- **Update:** change the leaf, then walk `v >>= 1` to the root, recomputing each parent.
- **Query `[l, r)`:** shift both ends to leaf positions, `l += n`, `r += n`, and loop while `l < r`. If `l` is odd it is a right child, so it cannot be merged with its sibling inside the range: take `t[l]` and move to `l + 1`. If `r` is odd, `r - 1` is a right child whose left sibling is inside the range, so take `t[r - 1]`. Then halve both. The pieces taken on the left are combined in order into `left`, the pieces taken on the right are put in front of `right`, and the answer is `op(left, right)`. The two accumulators are what keep it correct for a non-commutative `op`.

```cpp
// The bottom-up tree (Al.Cash's design).  2n slots, no recursion: the leaves are t_[n .. 2n-1], and t_[v] = op(t_[2v], t_[2v+1]).
//    For a power of two n this is the same heap layout as above.  For any other n it still answers every query correctly.
template <class M>
class IterSegTree {
public:
    using T = typename M::T;
private:
    std::size_t n_;
    std::vector<T> t_;
    friend struct TestAccess;
public:
    explicit IterSegTree(std::size_t n = 0) : n_(n), t_(2 * n, M::id()) {}
    explicit IterSegTree(const std::vector<T>& a) : n_(a.size()), t_(2 * a.size(), M::id()) {
        std::copy(a.begin(), a.end(), t_.begin() + n_);
        for (std::size_t v = n_; v-- > 1;) t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);          // O(n) build, from the leaves up
    }
    std::size_t size() const { return n_; }
    void set(std::size_t i, const T& x) {
        if (i >= n_) throw std::out_of_range("IterSegTree::set");
        std::size_t v = i + n_;
        t_[v] = x;
        for (v >>= 1; v >= 1; v >>= 1) t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);             // walk to the root
    }
    T get(std::size_t i) const { if (i >= n_) throw std::out_of_range("IterSegTree::get"); return t_[i + n_]; }
    T query(std::size_t l, std::size_t r) const {                   // op over a[l..r)
        if (l > r || r > n_) throw std::out_of_range("IterSegTree::query");
        T left = M::id(), right = M::id();                          // the pieces taken on the left edge and on the right edge
        for (l += n_, r += n_; l < r; l >>= 1, r >>= 1) {
            if (l & 1) left = M::op(left, t_[l++]);                  // l is a right child: take it, then move past it
            if (r & 1) right = M::op(t_[--r], right);                // r is a right child: its left sibling is inside the range
        }
        return M::op(left, right);                                   // left pieces come first, in order, then the right pieces
    }
};
```

For a **power of two** `n` the layout is exactly the heap layout of section 2 (the program shows the same sum tree, `31 9 22 4 5 14 8 3 1 4 1 5 9 2 6`, and the same changed nodes, 1, 2, 5 and 10, for `set(2, 14)`). For **any other `n`** the array still works: for `a = 3 1 4 1 5 9` (`n = 6`) the tree is `t[1..11] = 23 19 4 5 14 3 1 4 1 5 9` (leaves at `t[6..11]`), and `query(1, 5) = 11`, `query(0, 6) = 23`, `query(2, 3) = 4`. But some nodes are then **meaningless for an order-sensitive monoid**: `t[1]` of that tree joins the pieces `[2,6)` and `[0,2)`, in the wrong order. A query never uses such a node, which the interval monoid shows: `query(1, 5)` on six elements joins `[2,4)+[4,5)` and then `[1,2)+[2,5)` and returns `[1,5)`. Do not read `t[1]` as "the total" unless `n` is a power of two (or the operation is commutative).

**Measured, for a million elements** (a `long long` per node):

| | Recursive | Bottom-up | Fenwick (Topic 20) |
|---|---|---|---|
| Memory | 32,000,032 bytes | 16,000,000 bytes | 8,000,008 bytes |
| Build, ms | 11.9 / 10.6 | 4.0 / 5.3 | 4.0 / 3.6 |
| Sum query, ns | 524.2 / 539.6 | 175.0 / 186.5 | 67.9 / 65.7 |
| Point update, ns | 255.3 / 291.9 | 119.8 / 115.2 | 56.3 / 65.7 |

(The update for the trees is an assignment, for the Fenwick tree an addition; each update timing also includes creating an empty tree of a million elements, and the sum timings agree on every query.) The bottom-up tree is **half the memory**, builds **2.0 to 3.0 times faster**, answers **2.9 to 3.0 times faster** and updates **2.1 to 2.5 times faster** than the recursive one. I did not profile why; function-call overhead and the two-way visit of the recursive query are the likely causes. The Fenwick tree is in turn **2.6 to 2.8 times faster** at the sum query and **1.8 to 2.1 times faster** at the update than the bottom-up tree, at half its memory. When the operation is a plain sum with point updates, the Fenwick tree wins; the segment tree's argument is generality.

Against the sparse table, for the minimum (the same ranges, no updates): the bottom-up segment tree took **160.4 / 175.5 ns** per query, the recursive one **526.4 / 516.9 ns** and the sparse table **27.3 / 27.1 ns**, so the static table is **5.9 to 6.5 times faster** than the bottom-up tree. A segment tree is what you use *because the array changes*.

A fat node costs more: the maximum-subarray tree has 32-byte nodes (64,000,000 bytes for a million elements) and a query took **333.0 / 326.0 ns**, **1.7 to 1.9 times** the sum query of the same tree.

---

## 5. Descending the tree

A query asks about a range given from outside. A **descent** asks the tree where something happens. The question "what is the first index at or after `l` whose prefix aggregate `op(a[l], ..., a[r])` satisfies a test" can be answered by walking down once, because the stored answers say, subtree by subtree, whether the test would already hold.

The walk keeps `acc`, the aggregate of everything to the left of the current node that lies in `[l, n)`. For a node fully inside `[l, n)`: combine `acc` with the node's value. If the test is **not** satisfied, the whole subtree can be skipped, so absorb it into `acc` and move on. If it **is** satisfied, the answer is inside this subtree: descend into its children, left first (a leaf is the answer). For a node that straddles `l`, descend into both children. The test must be **monotone**: once it holds for `op(a[l..r])` it holds for every longer range.

```cpp
// Descending the tree.  find_first(l, pred) returns the smallest r >= l such that pred(op(a[l], ..., a[r])) holds, or npos.
//    pred must be MONOTONE: once it holds for a range it holds for every longer range that starts at the same l.
//    The walk skips whole subtrees whose combined value does not satisfy pred yet, and descends into the first one that does.
template <class M>
class SegTreeFind : public SegTree<M> {
    using Base = SegTree<M>;
    using T = typename M::T;
    friend struct TestAccess;
    template <class F>
    std::size_t go(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, const F& pred, T& acc) const {
        if (hi <= l) return Base::npos;                              // entirely left of l
        if (l <= lo) {                                               // entirely inside [l, n)
            T cand = M::op(acc, this->t_[v]);
            if (!pred(cand)) { acc = cand; return Base::npos; }      // not yet: absorb the whole subtree and move on
            if (hi - lo == 1) return lo;                             // a leaf that makes pred true: the answer
        }
        std::size_t mid = (lo + hi) / 2;
        std::size_t res = go(2 * v, lo, mid, l, pred, acc);
        return res != Base::npos ? res : go(2 * v + 1, mid, hi, l, pred, acc);
    }
public:
    using Base::Base;
    template <class F>
    std::size_t find_first(std::size_t l, const F& pred) const {
        if (l > this->n_) throw std::out_of_range("SegTreeFind::find_first");
        T acc = M::id();
        return this->n_ ? go(1, 0, this->n_, l, pred, acc) : Base::npos;
    }
};

// First index >= l whose value is at least x (a prefix maximum reaches x exactly when some element does).
inline std::size_t first_at_least(const SegTreeFind<MaxM>& t, std::size_t l, int x) {
    return t.find_first(l, [x](int m) { return m >= x; });
}
// The position of the k-th one (k >= 1) in a 0/1 array kept in a sum tree.
inline std::size_t kth_one(const SegTreeFind<SumM>& t, long long k) {
    return t.find_first(0, [k](long long s) { return s >= k; });
}
```

With the maximum monoid, "the prefix maximum reaches `x`" holds exactly when some element reaches `x`, which gives `first_at_least`. For `a = 3 1 4 1 5 9 2 6`: starting at `l = 0` with `x = 5` the answer is index 4; at `l = 5` it is 5; at `l = 6` it is 7 (the value 6); at `l = 0` with `x = 10` there is none; at `l = 2` with `x = 4` it is 2. The candidates tested for `(l = 0, x = 9)`, in order, are `9 4 9 9 5 9`: the root (9, satisfies), the left half (4, no: absorbed), the right half (9, yes), its left half (9, yes), the leaf 5 (5, no: absorbed), the leaf 9 (yes, index 5). For `(l = 3, x = 6)` they are `1 9 9 5 9` and the answer is 5.

With the sum monoid over a 0/1 array, "the sum reaches `k`" finds the **k-th one**: for `0 1 1 0 1 0 0 1 1` the first five ones are at positions `1 2 4 7 8`.

**Measured** (a million elements, 1,000,000 searches that each have an answer; the alternatives are a binary search over the sparse table of Topic 19 using O(1) range maxima, about 20 of them, and a plain scan from `l`, timed on the first 10,000 of them):

| Case | Average distance to the answer | Descent, ns | Sparse binary search, ns | Scan, ns |
|---|---|---|---|---|
| near (about one element in 1,000 qualifies) | 1,112 | 354.8 / 317.4 | 1,602.9 / 1,565.1 | 723 / 735 |
| far (about one in 100,000) | 123,508 | 310.1 / 264.8 | 838.1 / 785.9 | 80,663 / 82,908 |

The descent is **4.5 to 4.9 times** faster than the binary search over the sparse table in the near case and **2.7 to 3.0 times** in the far one, and its cost hardly depends on the distance. The scan is only **2.0 to 2.3 times** slower than the descent when the answer is about a thousand elements away, and **260 to 310 times** slower when it is a hundred thousand away: a scan is a perfectly good answer for nearby targets.

**Systems use** (general knowledge, not measured here). First-fit allocation over a bitmap or a table of free extents is a descent: store in each node the longest run of free blocks (the same four-field node as the maximum subarray, with runs of ones in place of sums) and descend to the first position with a run of at least the requested length. It is Problem H.

---

## 6. Choosing

| Situation | Use |
|---|---|
| Sums or XOR with point updates | Fenwick tree (Topic 20): about 2.6 to 2.8 times faster, half the memory |
| Minimum, maximum or gcd, array never changes | Sparse table (Topic 19): about 6 times faster than the bottom-up tree |
| Minimum or maximum with point updates | **Bottom-up segment tree** |
| A merge that is not a plain sum (maximum subarray, counts, composition) | Segment tree |
| "First index with ..." on changing data | Segment tree descent |
| Range updates (add or assign over a range) | Lazy segment tree (Topic 22), which needs the recursive form |

The recursive form earns its extra memory and time when you need things that depend on the range of each node (lazy tags, Topic 22; persistent versions, Topic 23). For everything in this topic the bottom-up tree is the default.

---

## 7. Common mistakes

1. **A recursive tree with `2n` slots.** Use `4n` (or a different numbering).
2. **Mixing closed and half-open ranges**, and `mid` computed for one while the ranges are the other.
3. **A wrong identity**: `0` for a minimum (it wins every comparison), or returning a default-constructed value for a disjoint node of a monoid whose identity is not zero.
4. **An `op` that is not associative**, or an identity that does not satisfy the law. The tree does not detect this; the answers depend on the shape of the tree.
5. **The wrong order of the pieces.** In the recursive query the left child comes first; in the bottom-up query the right edge pieces are put in front of `right`. A commutative test (sum, minimum) cannot reveal this; use string concatenation or function composition to test.
6. **Reading `t[1]` as the total** in a bottom-up tree whose size is not a power of two (section 4).
7. **Forgetting to recompute the path** after a leaf change, or adding the new value to the leaf instead of assigning it when the monoid is not a sum.
8. **A maximum-subarray node that allows the empty subarray in some places and not in others**, or an identity too close to the overflow limit. All-negative arrays are the test.
9. **A non-monotone predicate in a descent**, or `acc` passed by value (the absorbed prefix is then lost).
10. **`int` sums.** A million values of 10^9 do not fit in 32 bits.
11. **Building by `n` point updates** (O(n log n)) when the O(n) build exists.
12. **A segment tree where a Fenwick tree or a sparse table is enough** (section 6).
13. **An empty range.** `query(l, l)` is `id()`; code that tries to be clever about it returns garbage.

**The mutants that survive**, each checked by hand:

1. `NEG = -(1LL << 62)` instead of `-(1LL << 60)` in `MaxSubM`. The two stand-ins differ only if a partial sum falls below about -4.6 * 10^18, which is outside what the type is meant for (the sums must stay well inside 64 bits anyway).
2. `return query(1, 0, n_, l, r)` without the `n_ ?` test in `SegTree::query`. For `n_ = 0` the only legal call is `l = r = 0`, and the root test `r <= lo` already returns `id()`.
3. `go(1, 0, n_, l, pred, acc)` without the `n_ ?` test in `find_first`. For `n_ = 0` the only legal `l` is 0, and `hi <= l` returns `npos` at once.

---

## 8. Questions to answer out loud

1. What is a monoid? Which of the three requirements does the sparse table need and the segment tree not?
2. How are the nodes of the recursive tree numbered? Why `4n` slots?
3. State the three cases of the query. Why does it visit O(log n) nodes?
4. How does a point update work? Which nodes does `set(2, ...)` change on 8 elements?
5. Give the merge for the maximum subarray sum. Where does the cross-boundary case come from?
6. Why is function composition a good test for the combine order?
7. Explain the bottom-up query: why the odd tests, and why two accumulators?
8. What does the bottom-up layout look like when `n` is not a power of two? Which node is not the total?
9. What does a descent compute? Why must the predicate be monotone?
10. How do you find the k-th one in a 0/1 array with a tree?
11. For sums with point updates, which is better, Fenwick or segment tree? By how much (measured)?
12. When is the recursive form needed?

---

## 9. Problems

**A. The tree.** For `3 1 4 1 5 9 2 6` write the sum tree `t[1..15]` of the recursive form. Which nodes does `set(2, 14)` change, and to what?

**B. A query.** Which stored nodes does the query `[1, 7)` use? Give the intervals.

**C. Maximum subarray.** For `-2 1 -3 4 -1 2 1 -5 4` merge the nodes `[0, 4)` and `[4, 9)` and give the answer and the subarray.

**D. Order.** Compose `f0 = (2, 3)`, `f1 = (5, 1)`, `f2 = (1, 7)` in the order `f0, f1, f2` and in the order `f2, f1, f0`.

**E. Bottom-up, odd size.** For `3 1 4 1 5 9` (`n = 6`) give `t[1..11]` of the bottom-up tree and the pieces used by `query(1, 5)`.

**F. A descent.** For `3 1 4 1 5 9 2 6`, find `first_at_least(l = 3, x = 6)` and list the candidates tested.

**G. The k-th one.** In `0 1 1 0 1 0 0 1 1` where are the 1st to 5th ones?

**H. Design.** Define the node for "longest run of 1s" in a 0/1 array (what to store, how to merge, what the leaf and the identity are), and say how a descent finds the first position that starts a run of length at least `k`.

**I. Choose.** Pick a structure for each: (a) 10^6 numbers, 10^6 point updates mixed with 10^6 range sums; (b) the same with range minimums; (c) a static array and 10^7 range minimums; (d) the longest run of free blocks in a bitmap that changes; (e) the product of a range of 2 by 2 matrices modulo a prime, with point updates.

---

## Answers

**A.** `t[1..15] = 31 9 22 4 5 14 8 3 1 4 1 5 9 2 6`. `set(2, 14)` changes nodes **1, 2, 5, 10**: node 10 (the leaf of position 2) goes `4 -> 14`, node 5 (`[2,4)`) `5 -> 15`, node 2 (`[0,4)`) `9 -> 19`, node 1 `31 -> 41`. Program output: the same.

**B.** The four nodes `[1,2)`, `[2,4)`, `[4,6)` and `[6,7)`. The joins, in order, are `[4,6)+[6,7)`, `[1,2)+[2,4)` and `[1,4)+[4,7)`. Program output: the same.

**C.** The node `[0, 4)` is `(sum 0, pref 0, suf 4, best 4)` and `[4, 9)` is `(1, 2, 4, 4)`. The merge is `sum = 1`, `pref = 2`, `suf = max(4, 1 + 4) = 5`, `best = max(4, 4, 4 + 2) = 6`: the answer is **6**, the subarray `4 -1 2 1`. Program output: the same four numbers `(1, 2, 5, 6)`.

**D.** In the order `f0, f1, f2`: `f0` then `f1` is `(5 * 2, 5 * 3 + 1) = (10, 16)`, then `f2` gives `(10, 16 + 7) = (10, 23)`. In the order `f2, f1, f0`: `f2` then `f1` is `(5, 5 * 7 + 1) = (5, 36)`, then `f0` gives `(2 * 5, 2 * 36 + 3) = (10, 75)`. Program output: `(10, 23)` and `(10, 75)`.

**E.** `t[1..11] = 23 19 4 5 14 3 1 4 1 5 9` (leaves `3 1 4 1 5 9` at `t[6..11]`; `t[5] = 5 + 9`, `t[4] = 1 + 4`, `t[3] = 3 + 1`, `t[2] = t[4] + t[5] = 19`, `t[1] = t[2] + t[3] = 23`). `query(1, 5)`: `l = 7`, `r = 11`. `l` is odd: take `t[7]` (position 1) and `l` becomes 8; `r` is odd: take `t[10]` (position 4) and `r` becomes 10. Halve: `l = 4`, `r = 5`; `l` is even, `r` is odd: take `t[4]` (positions 2 to 3). Halve: `l = 2`, `r = 2`, stop. The pieces are positions 1, 2 to 3 and 4: `1 + 5 + 5`... with the values `1`, `4 + 1 = 5` and `5`: the sum **11**. With the interval monoid the joins are `[2,4)+[4,5)` (the right pieces) and then `[1,2)+[2,5)` (left, then right), giving `[1,5)`. Program output: 11 and those joins.

**F.** The answer is **5** (the value 9; positions 3 and 4 hold 1 and 5, both below 6). The candidates tested, in order: `1 9 9 5 9`: the leaf of position 3 (1, no: absorbed), the right half `[4,8)` (9, yes), its left half `[4,6)` (9, yes), the leaf 4 (5, no), the leaf 5 (9, yes). Program output: the same.

**G.** At positions **1, 2, 4, 7, 8** (the program prints `1 2 4 7 8`).

**H.** Store `len` (the length of the range), `pref` (the longest run of 1s at its left end), `suf` (at its right end) and `best` (the longest run anywhere). A leaf of value 1 is `(1, 1, 1, 1)`; a leaf of value 0 is `(1, 0, 0, 0)`; the identity is `(0, 0, 0, 0)`. Merge `a` then `b`: `len = a.len + b.len`; `pref = a.pref == a.len ? a.len + b.pref : a.pref`; `suf = b.suf == b.len ? b.len + a.suf : b.suf`; `best = max(a.best, b.best, a.suf + b.pref)`. A descent with the test `best >= k` finds the end of the first run of length at least `k`; its start is that index minus `k - 1`. (The test is monotone, because a longer range never has a shorter best run.)

**I.** (a) A Fenwick tree (Topic 20): sums with point updates, faster and smaller. (b) A bottom-up segment tree: the minimum has no inverse. (c) A sparse table (Topic 19): no updates, about 6 times faster. (d) A segment tree with the node of Problem H (descent for first fit). (e) A bottom-up segment tree with matrix product as `op` (associative, not commutative; the identity is the identity matrix).

---

## Cheat sheet

- **Monoid:** type, associative `op`, identity. No commutativity, no idempotence needed.
- **Recursive:** node `v` covers `[lo, hi)`, children `2v`, `2v+1`, `mid = (lo + hi) / 2`; `4n` slots; query cases: disjoint (`id`), inside (`t[v]`), partial (combine children); at most 4 nodes per level.
- **Bottom-up:** leaves at `n..2n-1`, `t[v] = op(t[2v], t[2v+1])`, 2n slots; query: `l += n, r += n`, take `t[l++]` if `l` odd into `left`, take `t[--r]` if `r` odd into `right`, halve; answer `op(left, right)`; correct for any `n`, but `t[1]` is the total only for a power of two.
- **Merges:** maximum subarray `(sum, pref, suf, best)` with `best = max(a.best, b.best, a.suf + b.pref)`; minimum with count; composition `(g.a f.a, g.a f.b + g.b)`.
- **Descent:** keep `acc`; skip a subtree whose combined value fails a monotone test, descend into the first that passes.
- **Measured (1M elements):** the bottom-up tree is half the memory and 2.9 to 3.0 times faster at queries than the recursive one; the Fenwick tree beats it by 2.6 to 2.8 times at sums; the sparse table beats it by 5.9 to 6.5 times at static minimums; the descent beats a binary search over the sparse table by 2.7 to 4.9 times.

---

## Sources

- Al.Cash, "Efficient and easy segment trees" (Codeforces blog, 2015): the bottom-up design with `left` and `right` accumulators and the discussion of non-power-of-two sizes: https://codeforces.com/blog/entry/18051. The code here is my own implementation of the method described, tested as above.
- The maximum-subarray merge, the descent for the first index and the first-fit use are standard in the competitive-programming literature (for example the cp-algorithms articles on segment trees); the first-fit remark is from general knowledge and was not measured.
- CLRS, *Introduction to Algorithms*, chapter 14 (augmenting data structures), and Skiena, *The Algorithm Design Manual*, on segment trees (both from memory).
- LeetCode, by name: Range Sum Query - Mutable, Range Minimum Query problems, Longest Increasing Subsequence II, My Calendar III, Maximum Subarray, Falling Squares, The Skyline Problem.
