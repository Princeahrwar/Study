# DSA 07.23: Segment Tree III: Persistence, Dynamic Trees, Merge Sort Trees, Beats and Offline Queries

*Part of the [Trees plan](00_plan.md) · Time: about 10 hours (6 reading, 4 problems) · You need: [Segment tree I](21_segment_tree_point_update_range_query.md), [Segment tree II](22_segment_tree_lazy_propagation.md) and [Fenwick tree](20_fenwick_tree.md) (used by the offline sweep). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- **compress coordinates** and say when it is enough, and when the universe is too big (or unknown) for it;
- build a **persistent** segment tree by path copying and answer "the k-th smallest in `a[l..r)`" online in O(log n);
- build a **dynamic (sparse)** segment tree over a universe of 2^62 positions and answer range sums on it;
- build a **merge sort tree** and count the elements of a range below a value in O(log² n);
- explain **segment tree beats** (range `chmin` with range sum) well enough to recognise a problem that needs it;
- answer queries **offline** by sorting them and sweeping with a Fenwick tree (distinct values in a range), and compare that with the online persistent answer;
- say, with measurements, what each of these costs.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 4,433,753 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the compressor against a set on 410 arrays; the persistent k-th tree on **every range and every k** of arrays of length 0 to 60 with three value patterns (many duplicates, negative numbers, large numbers), plus `count_less` for values around every range's minimum and maximum, the exact node count (one fresh path per insert), and a test that building a later version, or a branch off an old version, leaves the old versions unchanged; the dynamic tree against a plain array on **every range** of universes of size 1 to 33 and against a `std::map` on a universe of 2^62 with positions near both ends, with `find_prefix` against a scan for every threshold; the merge sort tree on every range and many thresholds, with **every node compared with a sorted copy of its range**; the beats tree against a plain array on 200 arrays with 300 mixed operations each, **the root checked after every operation** (maximum, strict second maximum, count of maxima and sum) and exact work counters; the offline sweep on every range of arrays of length up to 40; and out-of-range arguments for every class. Finally 106 hand-made mutants of the code were run against the tests (a lost copy in the path copy, a swapped child, an off-by-one in a descent, a lost second maximum and so on): **98 were caught and 8 are equivalent changes** that cannot alter any output (listed after the common mistakes).

**Data and timing.** The arrays are **synthetic**: 500,000 random integers below 10^9 (for the beats tree, 200,000 integers below 10^9). Queries are random ranges `[l, r)` (average length about a third of the array) with random thresholds and ranks, generated beforehand. Times are nanoseconds per operation (or milliseconds for a whole run) on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 22), best of 3 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about systems practice are from general knowledge and say so.

---

## 1. Four walls of the plain segment tree

Topics 21 and 22 gave a tree that supports point updates, range updates and range queries over a **monoid**. Four kinds of question still do not fit:

| Wall | Example | Idea |
|---|---|---|
| The universe is huge | "add to position 10^18, ask for the sum of `[a, b)`" | create nodes only where something happened (dynamic tree) |
| The question is about **order** inside a range | "the k-th smallest in `a[l..r)`" | keep every prefix as a version (persistence), or sorted lists in the nodes (merge sort tree) |
| The question is about the **past** | "what did the array look like after update 7?" | persistence |
| The update is not a tag | "`a[i] = min(a[i], x)` on a range, then a sum" | store extra facts in the node (beats) |

Many of these also yield to a simpler tool if **all the questions are known in advance**: sort them, sweep, and let a Fenwick tree do the work (section 6).

---

## 2. Coordinate compression

When the values (or positions) come from a huge range but there are at most `n` distinct ones, replace each by its **rank** among the distinct values. A segment tree over `[0, 10^9)` becomes a tree over `[0, n)`, and order is preserved, so every comparison question still has the same answer.

```cpp
// Coordinate compression: replace every value by its RANK among the distinct values, so a universe of 10^9 becomes at most n slots.
struct Compressor {
    std::vector<long long> vals;                                   // sorted, distinct
    explicit Compressor(std::vector<long long> a) {
        std::sort(a.begin(), a.end());
        a.erase(std::unique(a.begin(), a.end()), a.end());
        vals = std::move(a);
    }
    std::size_t size() const { return vals.size(); }
    std::size_t rank(long long x) const {                          // the number of distinct values smaller than x
        return static_cast<std::size_t>(std::lower_bound(vals.begin(), vals.end(), x) - vals.begin());
    }
    long long value(std::size_t r) const { return vals.at(r); }    // the inverse of rank for a value that is present
};
```

`rank(x)` is the number of distinct values below `x`, whether `x` is present or not. For `a = 3 1 4 1 5 9 2 6` the distinct values are `1 2 3 4 5 6 9` and the ranks of the elements are `2 0 3 0 4 6 1 5`.

Compression needs the values **in advance**. If they arrive online, or you do not want to sort first, section 4 builds the tree over the whole universe without allocating it.

---

## 3. Persistence: the k-th smallest in a range

A **persistent** structure keeps every older version usable after an update. For a segment tree this is cheap, because one update changes only one root-to-leaf path: **copy those O(log m) nodes, point the new path at the old children it did not change, and share everything else**. The new root describes the new version; the old root still describes the old one, untouched.

**The k-th smallest in `a[l..r)`.** Build a counting tree over the compressed values. Version `i` contains the first `i` elements of the array (one `insert` from version `i - 1`). The multiset `a[l..r)` is then "version `r` minus version `l`", and a node's count in it is the **difference of the two nodes' counts**. To find the k-th smallest, descend both versions together: if `k` is at most the difference of the left children's counts, go left; otherwise subtract that difference from `k` and go right.

```cpp
// A PERSISTENT counting tree over the universe [0, m).  An insert never changes a node: it copies the O(log m) nodes on one
// root-to-leaf path ("path copying") and shares everything else, so every older root still describes the older tree.
// Node 0 is the shared empty tree: its children are 0 and its count is 0.
class PersistentCounts {
    struct Node { int l = 0, r = 0, cnt = 0; };
    std::vector<Node> pool_;
    std::size_t m_;
    friend struct TestAccess;
    int insert(int prev, std::size_t lo, std::size_t hi, std::size_t pos) {
        Node nd = pool_[prev];                                      // copy, never modify
        ++nd.cnt;
        if (hi - lo > 1) {
            std::size_t mid = (lo + hi) / 2;
            if (pos < mid) nd.l = insert(nd.l, lo, mid, pos); else nd.r = insert(nd.r, mid, hi, pos);
        }
        pool_.push_back(nd);
        return static_cast<int>(pool_.size()) - 1;
    }
public:
    explicit PersistentCounts(std::size_t m) : pool_(1), m_(m) { if (m == 0) throw std::invalid_argument("PersistentCounts: empty universe"); }
    std::size_t universe() const { return m_; }
    std::size_t nodes() const { return pool_.size() - 1; }          // not counting the shared empty node
    int insert(int root, std::size_t pos) {                         // a NEW root: the old one is untouched
        if (pos >= m_) throw std::out_of_range("PersistentCounts::insert");
        return insert(root, 0, m_, pos);
    }
    std::size_t count(int root) const { return static_cast<std::size_t>(pool_[root].cnt); }
    // In the multiset (version hi) minus (version lo): the position of the k-th smallest element, k = 1, 2, ...
    std::size_t kth(int hi_root, int lo_root, std::size_t k) const {
        if (k < 1 || k > count(hi_root) - count(lo_root)) throw std::out_of_range("PersistentCounts::kth");
        std::size_t lo = 0, hi = m_;
        int a = hi_root, b = lo_root;
        while (hi - lo > 1) {
            std::size_t mid = (lo + hi) / 2;
            std::size_t left = static_cast<std::size_t>(pool_[pool_[a].l].cnt - pool_[pool_[b].l].cnt);
            if (k <= left) { a = pool_[a].l; b = pool_[b].l; hi = mid; }
            else { k -= left; a = pool_[a].r; b = pool_[b].r; lo = mid; }
        }
        return lo;
    }
    // In the difference of the two versions: how many elements have a position below pos.
    std::size_t count_less(int hi_root, int lo_root, std::size_t pos) const {
        if (pos >= m_) return count(hi_root) - count(lo_root);
        std::size_t lo = 0, hi = m_, total = 0;
        int a = hi_root, b = lo_root;
        while (hi - lo > 1) {
            std::size_t mid = (lo + hi) / 2;
            if (pos < mid) { a = pool_[a].l; b = pool_[b].l; hi = mid; }
            else { total += static_cast<std::size_t>(pool_[pool_[a].l].cnt - pool_[pool_[b].l].cnt); a = pool_[a].r; b = pool_[b].r; lo = mid; }
        }
        return total;
    }
};

// The k-th smallest value in a[l..r), online, in O(log n) per query: version i holds the first i elements, so the
// difference of versions r and l is exactly the multiset a[l..r).
class KthInRange {
    Compressor c_;
    PersistentCounts p_;
    std::vector<int> root_;
public:
    explicit KthInRange(const std::vector<long long>& a) : c_(a), p_(std::max<std::size_t>(1, c_.size())), root_(a.size() + 1, 0) {
        for (std::size_t i = 0; i < a.size(); ++i) root_[i + 1] = p_.insert(root_[i], c_.rank(a[i]));
    }
    std::size_t size() const { return root_.size() - 1; }
    std::size_t nodes() const { return p_.nodes(); }
    long long kth(std::size_t l, std::size_t r, std::size_t k) const {          // k = 1 is the minimum
        if (l > r || r > size()) throw std::out_of_range("KthInRange::kth");
        return c_.value(p_.kth(root_[r], root_[l], k));
    }
    std::size_t count_less(std::size_t l, std::size_t r, long long x) const {    // how many of a[l..r) are below x
        if (l > r || r > size()) throw std::out_of_range("KthInRange::count_less");
        return p_.count_less(root_[r], root_[l], c_.rank(x));
    }
};
```

For `a = 3 1 4 1 5 9 2 6` (seven distinct values, so ranks in `[0, 7)`) the roots are `0 4 7 11 14 18 22 26 30` and the number of nodes after each insert is `4 7 11 14 18 22 26 30`. An insert creates one node per level of its path. The tree over seven ranks has four levels except at rank 0 (a leaf at depth 2 in the left subtree of three), so the 8 inserts create `8 * 4 - 2 = 30` nodes: the two inserts of the value 1 (rank 0) have shorter paths.

The query `kth(1, 6, 3)` asks for the 3rd smallest of `a[1..6) = 1 4 1 5 9`, which sorted is `1 1 4 5 9`. It descends versions 6 and 1:

| Ranks | `k` | Left difference | Move |
|---|---|---|---|
| `[0, 7)` | 3 | 2 | right (`k` stays above 2) |
| `[3, 7)` | 1 | 2 | left |
| `[3, 5)` | 1 | 1 | left |

It ends at rank 3, which is the value **4**. Other answers on this array: `kth(0, 8, k)` for `k = 1 .. 8` is `1 1 2 3 4 5 6 9`, `kth(2, 7, 3) = 4`, and `count_less(1, 7, 5) = 4`.

`count_less` descends the same way, adding the left difference whenever it goes right, and answers "how many elements of `a[l..r)` are below `x`" in O(log n).

**Memory is the price.** Each insert creates about `log2 m` nodes, so `n` inserts keep `n log m` nodes alive forever (section 8 gives the byte count).

---

## 4. The dynamic (sparse) tree

A tree over `[0, 2^62)` cannot be allocated. But it does not have to be: **create a node only when something is added below it**. The root exists from the start; an update walks down and creates the missing nodes of its path (at most one per level); a node that was never created represents a range where nothing was ever added, so its sum is 0 and the query can stop there.

```cpp
// A DYNAMIC (sparse) sum tree over the universe [0, u) with u as large as 2^62: a node exists only if something was added below
// it, so n updates create at most n * (depth + 1) nodes however big u is.  Index 0 is "no node": its sum is 0.
class DynamicSumTree {
    struct Node { int l = 0, r = 0; long long sum = 0; };
    std::vector<Node> pool_;
    unsigned long long u_;
    friend struct TestAccess;
    long long query(int v, unsigned long long lo, unsigned long long hi, unsigned long long l, unsigned long long r) const {
        if (v == 0 || r <= lo || hi <= l) return 0;                  // nothing was ever added below a missing node
        if (l <= lo && hi <= r) return pool_[v].sum;
        unsigned long long mid = lo + (hi - lo) / 2;
        return query(pool_[v].l, lo, mid, l, r) + query(pool_[v].r, mid, hi, l, r);
    }
public:
    static constexpr unsigned long long npos = ~0ULL;
    explicit DynamicSumTree(unsigned long long u) : pool_(2), u_(u) { if (u == 0) throw std::invalid_argument("DynamicSumTree: empty universe"); }
    std::size_t nodes() const { return pool_.size() - 2; }          // not counting the null node and the root
    void add(unsigned long long pos, long long d) {                  // a[pos] += d, creating the missing nodes of one path
        if (pos >= u_) throw std::out_of_range("DynamicSumTree::add");
        int cur = 1;
        unsigned long long lo = 0, hi = u_;
        for (;;) {
            pool_[cur].sum += d;
            if (hi - lo == 1) return;
            unsigned long long mid = lo + (hi - lo) / 2;
            bool left = pos < mid;
            int child = left ? pool_[cur].l : pool_[cur].r;
            if (child == 0) {
                child = static_cast<int>(pool_.size());
                pool_.push_back(Node{});
                if (left) pool_[cur].l = child; else pool_[cur].r = child;
            }
            cur = child;
            if (left) hi = mid; else lo = mid;
        }
    }
    long long sum(unsigned long long l, unsigned long long r) const {    // a[l] + ... + a[r-1]
        if (l > r || r > u_) throw std::out_of_range("DynamicSumTree::sum");
        return query(1, 0, u_, l, r);
    }
    // The smallest position p whose prefix a[0..p] sums to at least k (all adds must be non-negative), or npos.
    unsigned long long find_prefix(long long k) const {
        if (k <= 0) return 0;
        if (pool_[1].sum < k) return npos;
        int cur = 1;
        unsigned long long lo = 0, hi = u_;
        while (hi - lo > 1) {
            unsigned long long mid = lo + (hi - lo) / 2;
            long long left = pool_[pool_[cur].l].sum;
            if (k <= left) { cur = pool_[cur].l; hi = mid; } else { k -= left; cur = pool_[cur].r; lo = mid; }
        }
        return lo;
    }
};
```

Index 0 is "no node" (sum 0, children 0), so a missing child is a valid index everywhere. `find_prefix` is the descent of Topic 21: the first position whose prefix sum reaches `k`, valid when all adds are non-negative.

On a universe of 16: `add(5, 3)` creates 4 nodes (one per level below the root), the second `add(5, 2)` creates none (the path exists), and `add(12, 4)` creates 4 more, since it leaves the root's left subtree: **8 nodes** in all. Then `sum(0, 16) = 9`, `sum(0, 6) = 5`, `sum(6, 16) = 4`, `find_prefix(5) = 5` (position 5 holds 5), `find_prefix(6) = 12` and `find_prefix(10)` is `npos` (the total is 9). On a universe of 2^60, two adds at positions 5 and 2^59 create **120 nodes**, 60 per path, and `find_prefix(5)` is 2^59 = 576460752303423488 (the sum up to 5 is 3, so the 4 at position 2^59 is the one that reaches 5).

**Dynamic against compression.** If every update position is known in advance, compressing them and using a Fenwick tree is simpler and faster (section 8). The dynamic tree is for **online** positions, and for trees that also need a lazy tag over a huge range (the nodes then carry the tags of Topic 22, created the same way).

---

## 5. The merge sort tree

A segment tree whose every node stores the **sorted values** of its range. The build merges the two children's lists (n log n time and memory). A query binary-searches the O(log n) covering nodes, so counting the elements of `a[l..r)` below `x` costs O(log² n).

```cpp
// A MERGE SORT TREE: every node keeps the SORTED values of its range.  count_less(l, r, x) binary-searches the O(log n) covering
// nodes: O(log^2 n) per query, n log n memory, and it works for any "how many elements of a[l..r) satisfy a comparison".
class MergeSortTree {
    std::size_t n_;
    std::vector<std::vector<long long>> t_;
    friend struct TestAccess;
    void build(std::size_t v, std::size_t lo, std::size_t hi, const std::vector<long long>& a) {
        if (hi - lo == 1) { t_[v] = {a[lo]}; return; }
        std::size_t mid = (lo + hi) / 2;
        build(2 * v, lo, mid, a);
        build(2 * v + 1, mid, hi, a);
        t_[v].resize(hi - lo);
        std::merge(t_[2 * v].begin(), t_[2 * v].end(), t_[2 * v + 1].begin(), t_[2 * v + 1].end(), t_[v].begin());
    }
    std::size_t count_less(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r, long long x) const {
        if (r <= lo || hi <= l) return 0;
        if (l <= lo && hi <= r) return static_cast<std::size_t>(std::lower_bound(t_[v].begin(), t_[v].end(), x) - t_[v].begin());
        std::size_t mid = (lo + hi) / 2;
        return count_less(2 * v, lo, mid, l, r, x) + count_less(2 * v + 1, mid, hi, l, r, x);
    }
public:
    explicit MergeSortTree(const std::vector<long long>& a) : n_(a.size()), t_(4 * a.size() + 4) { if (n_) build(1, 0, n_, a); }
    std::size_t size() const { return n_; }
    std::size_t count_less(std::size_t l, std::size_t r, long long x) const {          // how many of a[l..r) are below x
        if (l > r || r > n_) throw std::out_of_range("MergeSortTree::count_less");
        return n_ ? count_less(1, 0, n_, l, r, x) : 0;
    }
    std::size_t count_in(std::size_t l, std::size_t r, long long lo, long long hi) const {   // how many of a[l..r) lie in [lo, hi)
        if (lo >= hi) return 0;
        return count_less(l, r, hi) - count_less(l, r, lo);
    }
};
```

For `a = 3 1 4 1 5 9 2 6` the root holds `1 1 2 3 4 5 6 9`, node 2 (`[0,4)`) holds `1 1 3 4`, node 3 (`[4,8)`) holds `2 5 6 9`, and the nodes 4 to 7 hold `1 3`, `1 4`, `5 9` and `2 6`. Then `count_less(1, 7, 5) = 4` (the range is `1 4 1 5 9 2`, and `1`, `4`, `1`, `2` are below 5) and `count_in(1, 7, 2, 6) = 3` (`4`, `5`, `2`).

The merge sort tree is **slower and bigger than the persistent tree** for `count_less` (section 8) but has one advantage: the node lists can hold anything you can sort and search, so it also answers questions the counting tree cannot (for example the number of elements of a range closest to `x`, or the sum of the elements below `x` if each node also keeps prefix sums). Its listing is static: an update would mean re-sorting O(n) lists.

---

## 6. Offline queries

When every question is known before you start, you can reorder them. The standard trick is to **sort the queries by right endpoint**, sweep the array from left to right, and keep a Fenwick tree that is correct for the prefix seen so far; a query that ends at `r` is answered when the sweep reaches `r`.

**Distinct values in `a[l..r)`.** Keep a 1 only at the **last occurrence** of each value seen so far. Then the answer for a query that ends at `r` is the number of 1s in `[l, r)`: each distinct value in the range contributes exactly its last occurrence inside the prefix, and that occurrence is at or after `l` exactly when the value occurs in the range.

```cpp
// OFFLINE QUERIES: when all the questions are known in advance, sort them and sweep, and a Fenwick tree replaces a heavy structure.
// "How many DISTINCT values in a[l..r)": sweep r from left to right and keep a 1 only at the LAST occurrence of each value seen so
// far; the answer to a query that ends at r is the number of 1s in [l, r).
struct Bit {                                                        // a Fenwick tree over [0, n), 0-based
    std::vector<int> t;
    explicit Bit(std::size_t n) : t(n + 1, 0) {}
    void add(std::size_t i, int d) { for (++i; i < t.size(); i += i & (~i + 1)) t[i] += d; }
    int prefix(std::size_t i) const { int s = 0; for (; i > 0; i -= i & (~i + 1)) s += t[i]; return s; }     // sum of [0, i)
};
inline std::vector<std::size_t> distinct_in_ranges(const std::vector<long long>& a, const std::vector<std::pair<std::size_t, std::size_t>>& q) {
    Compressor c(a);
    std::vector<std::size_t> order(q.size()), ans(q.size());
    for (std::size_t i = 0; i < q.size(); ++i) {
        if (q[i].first > q[i].second || q[i].second > a.size()) throw std::out_of_range("distinct_in_ranges");
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return q[x].second < q[y].second; });
    std::vector<long long> last(c.size(), -1);
    Bit bit(a.size());
    std::size_t next = 0;
    for (std::size_t r = 0; r <= a.size(); ++r) {                    // the prefix a[0..r) has been processed
        if (r > 0) {
            std::size_t v = c.rank(a[r - 1]);
            if (last[v] >= 0) bit.add(static_cast<std::size_t>(last[v]), -1);
            bit.add(r - 1, 1);
            last[v] = static_cast<long long>(r - 1);
        }
        for (; next < order.size() && q[order[next]].second == r; ++next) {
            auto [l, rr] = q[order[next]];
            ans[order[next]] = static_cast<std::size_t>(bit.prefix(rr) - bit.prefix(l));
        }
    }
    return ans;
}
```

For `a = 1 2 1 3 2 1` the marked positions after each prefix are `{0}`, `{0,1}`, `{1,2}`, `{1,2,3}`, `{2,3,4}` and `{3,4,5}`: when the second 1 arrives at position 2, the mark moves from 0 to 2. The queries `[0,3)`, `[1,5)`, `[0,6)`, `[2,2)` and `[3,4)` give **2, 3, 3, 0, 1**.

**The same question online.** Define `prev[i]` as the index of the previous occurrence of `a[i]` (or -1). An element of `a[l..r)` is the **first** occurrence of its value within the range exactly when `prev[i] < l`, so `distinct(l, r) = #{ i in [l, r) : prev[i] < l }`, which is `count_less(l, r, l)` on the persistent tree built over `prev`. For the array above, `prev = -1 -1 0 -1 1 2` and `distinct(1, 5)` counts the positions 1, 2 and 3 (their `prev` are -1, 0 and -1) but not 4 (`prev = 1`): **3**, as before. This needs no sorting of the queries, so it works for queries that arrive one at a time. Section 8 shows the price.

---

## 7. Segment tree beats (awareness)

Consider "set `a[i] = min(a[i], x)` for every `i` in `[l, r)`" together with range sum. It is not a tag in the sense of Topic 22: after a `chmin`, the sum of a node depends on **how many elements exceeded `x`**, which the sum alone does not say.

The idea (known as *segment tree beats* or the Ji driver segment tree) is to store, at every node, the **maximum** `mx`, the **strict second maximum** `se`, **how many elements equal the maximum** `cnt`, and the sum. A `chmin(x)` that fully covers a node has three cases:

- `x >= mx`: nothing changes; stop.
- `se < x < mx`: only the elements equal to the maximum are above `x`, and they all become `x`; the sum drops by `(mx - x) * cnt`, the maximum becomes `x`, and the node needs no recursion.
- `x <= se`: at least two different values would collapse into `x`; recurse into both children. This is the **breaking** step.

There is no stored tag: a child whose maximum exceeds its parent's maximum has not yet heard the news, and `push` caps it.

```cpp
// SEGMENT TREE BEATS (for awareness): range chmin, a[i] = min(a[i], x) on [l, r), with range sum and range maximum.
// A node keeps the maximum mx, the STRICT second maximum se, how many elements equal mx (cnt) and the sum.
//    If x >= mx the update does nothing.  If se < x < mx only the maxima change, by the same amount: sum -= (mx - x) * cnt.
//    Otherwise (x <= se) two different values would collapse into one, so we recurse: that is the "breaking" step.
// There is no stored tag: a child whose maximum exceeds its parent's maximum simply has not heard the news yet.
class ChminBeats {
    struct Node { long long mx, se, sum; int cnt; };
    static constexpr long long NONE = std::numeric_limits<long long>::min();      // "no second maximum"
    std::size_t n_;
    std::vector<Node> t_;
    mutable std::size_t visits_ = 0;
    friend struct TestAccess;
    void pull(std::size_t v) {
        const Node &a = t_[2 * v], &b = t_[2 * v + 1];
        Node r;
        r.sum = a.sum + b.sum;
        if (a.mx > b.mx) { r.mx = a.mx; r.cnt = a.cnt; r.se = std::max(a.se, b.mx); }
        else if (a.mx < b.mx) { r.mx = b.mx; r.cnt = b.cnt; r.se = std::max(b.se, a.mx); }
        else { r.mx = a.mx; r.cnt = a.cnt + b.cnt; r.se = std::max(a.se, b.se); }
        t_[v] = r;
    }
    void cap(std::size_t v, long long x) {                          // requires se < x; lowers the maxima of v to x
        if (x >= t_[v].mx) return;
        t_[v].sum -= (t_[v].mx - x) * t_[v].cnt;
        t_[v].mx = x;
    }
    void push(std::size_t v) { cap(2 * v, t_[v].mx); cap(2 * v + 1, t_[v].mx); }
    void build(std::size_t v, std::size_t lo, std::size_t hi, const std::vector<long long>& a) {
        if (hi - lo == 1) { t_[v] = {a[lo], NONE, a[lo], 1}; return; }
        std::size_t mid = (lo + hi) / 2;
        build(2 * v, lo, mid, a); build(2 * v + 1, mid, hi, a);
        pull(v);
    }
    void chmin(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r, long long x) {
        ++visits_;
        if (r <= lo || hi <= l || t_[v].mx <= x) return;             // outside, or nothing exceeds x
        if (l <= lo && hi <= r && t_[v].se < x) { cap(v, x); return; }   // only the maxima are above x: lower them in O(1)
        push(v);
        std::size_t mid = (lo + hi) / 2;
        chmin(2 * v, lo, mid, l, r, x); chmin(2 * v + 1, mid, hi, l, r, x);
        pull(v);
    }
    long long sum(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r) {
        if (r <= lo || hi <= l) return 0;
        if (l <= lo && hi <= r) return t_[v].sum;
        push(v);
        std::size_t mid = (lo + hi) / 2;
        return sum(2 * v, lo, mid, l, r) + sum(2 * v + 1, mid, hi, l, r);
    }
    long long maximum(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r) {
        if (r <= lo || hi <= l) return NONE;
        if (l <= lo && hi <= r) return t_[v].mx;
        push(v);
        std::size_t mid = (lo + hi) / 2;
        return std::max(maximum(2 * v, lo, mid, l, r), maximum(2 * v + 1, mid, hi, l, r));
    }
public:
    explicit ChminBeats(const std::vector<long long>& a) : n_(a.size()), t_(4 * a.size() + 4) { if (n_) build(1, 0, n_, a); }
    std::size_t size() const { return n_; }
    std::size_t visits() const { return visits_; }
    void chmin(std::size_t l, std::size_t r, long long x) {
        if (l > r || r > n_) throw std::out_of_range("ChminBeats::chmin");
        if (l < r) chmin(1, 0, n_, l, r, x);
    }
    long long sum(std::size_t l, std::size_t r) {
        if (l > r || r > n_) throw std::out_of_range("ChminBeats::sum");
        return n_ ? sum(1, 0, n_, l, r) : 0;
    }
    long long maximum(std::size_t l, std::size_t r) {                // NONE (the smallest long long) for an empty range
        if (l > r || r > n_) throw std::out_of_range("ChminBeats::maximum");
        return n_ ? maximum(1, 0, n_, l, r) : NONE;
    }
};
```

On `5 3 8 6 2 7` (maximum 8, second maximum 7, sum 31): `chmin(0, 6, 7)` breaks at the root (7 is not above the second maximum) and leaves `5 3 7 6 2 7`: the root is `(mx 7, se 6, cnt 2, sum 30)` after **3 visited nodes**. `chmin(0, 6, 6)` gives `5 3 6 6 2 6`: `(mx 6, se 5, cnt 3, sum 28)`, **8 visits** in all. `chmin(0, 6, 4)` gives `4 3 4 4 2 4`: `(mx 4, se 3, cnt 4, sum 21)`, **13 visits** in all. Then `sum(0, 6) = 21`, `maximum(0, 6) = 4` and `sum(1, 4) = 11`.

**Cost.** The known result for `chmin` together with range sum is O((n + q) log n) amortised: each break reduces the number of distinct values in the node (from the literature, from memory; I did not prove it here). Mixing in range **add** weakens it to O(log² n) amortised per operation (also from memory). I measured only the common case: on random operations the tree visited 8.1 nodes per `chmin` on average (section 8).

---

## 8. What it costs

All timings are 500,000 elements unless said otherwise.

**Memory.** The persistent tree built over 500,000 random values has **9,975,574 nodes**, 19.95 per insert, of 12 bytes: **119,706,888 bytes**, thirty times the 4,000,000 bytes of the array. The merge sort tree stores 20 levels of 500,000 numbers: **80,000,000 bytes** of values (plus a vector header for each node, not counted). The dynamic tree over a universe of 2^40 with 200,000 random updates has **4,699,796 nodes**, 23.5 per update, of 16 bytes: **75,196,736 bytes**.

**Counting the elements of a range below a value** (`count_less`, 200,000 random queries): the persistent tree took **1,079.0 / 960.3 ns** and the merge sort tree **2,980.1 / 2,753.5 ns**: the persistent tree is **2.8 to 2.9 times faster**. The build: persistent **554.5 / 577.0 ms**, merge sort tree **133.5 / 126.1 ms**: the persistent tree is **4.2 to 4.6 times slower** to build. Both gave the same answers.

**The k-th smallest in a range** (random ranges and ranks):

| Method | ns per query |
|---|---|
| Persistent tree (1,000,000 queries) | 1,309.8 / 1,234.0 |
| Merge sort tree with a binary search on the value (20,000 queries) | 17,323.4 / 16,637.9 |

The persistent descent is **13.2 to 13.5 times faster** than the binary search over the merge sort tree (which costs O(log³ n)). And against copying the range and calling `nth_element`, for the **median** of a window of fixed length:

| Window length | `nth_element` on a copy | Persistent tree |
|---|---|---|
| 100 | 891 / 1,053 | 888.8 / 859.8 |
| 10,000 | 66,528 / 74,232 | 691.0 / 640.0 |
| 250,000 | 1,710,006 / 1,698,858 | 135.8 / 153.3 |

For 100 elements the two are level (equal in the first run, the persistent tree 1.2 times faster in the second; no conclusion). For 10,000 the persistent tree is about **100 to 120 times faster**, and for 250,000 about **11,000 to 13,000 times faster**. The persistent query also ran faster on these fixed-length windows than on random ranges (1,309.8 ns above); I did not isolate the reason, so treat the table as the cost trend of the two methods and not as a constant.

**Distinct values in 500,000 ranges** over 500,000 elements drawn from 100,000 values: the offline sweep took **208.8 / 198.0 ms** (sorting the queries, one Fenwick tree) and the online persistent route (build the tree over `prev`, then 500,000 `count_less`) **864.1 / 800.4 ms**: the sweep is **4.0 to 4.1 times faster**, and the persistent route needs no knowledge of the queries. The answers agreed.

**The dynamic tree against offline compression** (200,000 updates at random positions in `[0, 2^40)`, then 500,000 range sums): the dynamic tree took **883.0 / 836.2 ns** per update and **858.2 / 819.2 ns** per query, **605.7 / 576.9 ms** in all; compressing the update positions and using a Fenwick tree took **163.1 / 181.2 ms** in all: the offline route is **3.2 to 3.7 times faster**, and it needs every update position in advance. The answers agreed.

**Beats** (200,000 elements, 400,000 random operations, half `chmin` with a random value, half range sums): **227.0 / 185.4 ns per operation**, against **20,139.4 / 22,454.0 ns** for a plain loop over the range (measured on the first 2,000 operations only): about **90 to 120 times faster**. The tree visited **8.1 nodes** per `chmin` on average. The sums of the first 2,000 operations agreed with the plain loop.

---

## 9. Choosing

| Situation | Use |
|---|---|
| k-th smallest (or rank) in a range, static array, queries online | Persistent tree: O(log n) per query, `n log n` memory |
| Count of elements below `x` in a range, static array | Persistent tree (2.8 to 2.9 times faster than the merge sort tree) |
| The same, but the node must hold something that is not a count | Merge sort tree |
| Point updates at positions up to 10^18, all positions unknown | Dynamic tree |
| Point updates, all positions known in advance | Compress + Fenwick tree (3.2 to 3.7 times faster) |
| Distinct values in a range, queries known in advance | Offline sweep with a Fenwick tree (4.0 to 4.1 times faster) |
| Distinct values in a range, queries online | Persistent tree over `prev` |
| `chmin` or `chmax` on a range with sums | Segment tree beats |
| Range add with range maximum | A lazy tree (Topic 22) |
| Counts of elements below `x` **with point updates** | None of these directly: it needs another idea (an order-statistic structure inside each node, or offline divide and conquer) |

---

## 10. Common mistakes

1. **Modifying a node in a persistent tree** instead of copying it. One missed copy corrupts every older version, and the symptom appears in a query about the past.
2. **Using the wrong pair of versions**: the range `[l, r)` is "version `r` minus version `l`", not `l + 1`.
3. **Forgetting that a persistent tree never frees**: `n` updates keep `n log m` nodes. For a million elements that is hundreds of megabytes.
4. **Compressing and then comparing against an uncompressed value**: use `rank(x)`, which also works for absent values, and not an equality lookup.
5. **A compressed universe of size 0** (an empty array): the tree needs at least one slot.
6. **A dynamic tree with `mid = (lo + hi) / 2` on a universe near 2^64**, which overflows; `lo + (hi - lo) / 2` does not.
7. **Using an index 0 for the root** in an arena where 0 means "no node".
8. **Pointers into a growing arena**: `push_back` can move the vector, so hold indices, not references, while creating nodes.
9. **Applying `find_prefix` to negative adds**: the descent needs non-negative values (it is a monotone test).
10. **Counting duplicates twice in a merge sort tree** by using `<=` where `<` was meant: `count_less` and `count_in` take half-open intervals.
11. **Beats with a non-strict second maximum.** If `se` may equal `mx`, the case "only the maxima are above `x`" is wrong. Keep `se` strictly smaller.
12. **Sorting the offline queries by the left endpoint** instead of the right one, or answering a query before the sweep reached its end.
13. **Reaching for beats, persistence or a dynamic tree** when a Fenwick tree and a sort would do (sections 6 and 9).

**The mutants that survive**, each checked by hand:

1. `v == 0 ||` removed in `DynamicSumTree::query`. A missing node's children are the null node, whose sum is 0, so the recursion ends in the same answer (more slowly).
2. `k <= 0` replaced by `k < 0` in `find_prefix`. For `k = 0` the descent goes left to position 0, the value the early return gives.
3. The `n_ ?` guard removed in `MergeSortTree::count_less`, `ChminBeats::sum` and `ChminBeats::maximum` (three mutants). For `n_ = 0` the only legal call is `l = r = 0`, and the root test `r <= lo` returns at once.
4. `lo >= hi` replaced by `lo > hi` in `count_in`. For `lo == hi` the two counts are equal and the difference is 0.
5. `x >= mx` replaced by `x > mx` in `cap`. For `x == mx` the sum changes by `(mx - x) * cnt = 0`.
6. `second == r` replaced by `second <= r` in `distinct_in_ranges`. The queries are sorted by their end, so none with a smaller end is left when the sweep reaches `r`.

---

## 11. Questions to answer out loud

1. What does coordinate compression preserve, and what does it need to know in advance?
2. How does an insert in a persistent segment tree avoid damaging the old versions? How many nodes does it create?
3. Why is "version `r` minus version `l`" the multiset `a[l..r)`? How does the k-th smallest descent use it?
4. Why are 30 nodes created for the 8 inserts of section 3, and not 32?
5. How does the dynamic tree represent a range where nothing was ever added?
6. When do you compress and use a Fenwick tree instead of a dynamic tree?
7. What does a merge sort tree store, and why is `count_less` O(log² n)?
8. Why is the persistent tree faster than the merge sort tree for `count_less`, and what does the merge sort tree do that it cannot?
9. In beats, what are the three cases of `chmin` at a fully covered node? Which one costs extra, and why does the tree not store a tag?
10. How does the offline sweep count distinct values? What is stored in the Fenwick tree after each prefix?
11. How can the same question be answered online with a persistent tree?
12. Which structure would you pick for "count of elements below `x` in a range, with point updates"?

---

## 12. Problems

**A. Compression.** Compress `30 10 20 10 40`. Give the distinct values, the ranks of the elements and `rank(25)`.

**B. Node counts.** For `3 1 4 1 5 9 2 6`, the persistent tree over the ranks `[0, 7)` has 30 nodes after 8 inserts. Which inserts create three nodes instead of four, and why?

**C. A descent.** For the same array, find `kth(2, 8, 4)` and write the descent as in the table of section 3.

**D. A dynamic tree.** On a universe of 16 do `add(5, 3)`, `add(5, 2)`, `add(12, 4)`. How many nodes exist after each? Give `find_prefix(5)`, `find_prefix(6)`, `find_prefix(10)`.

**E. A merge sort tree.** For `3 1 4 1 5 9 2 6` give the lists of the nodes `[0,4)` and `[4,8)`, then `count_less(2, 8, 4)` and `count_in(0, 8, 2, 5)`.

**F. Beats.** On `5 3 8 6 2 7` do `chmin(0, 3, 4)` then `chmin(0, 6, 5)`. Give the array and the root `(mx, se, cnt, sum)` after each.

**G. Offline distinct.** For `4 4 5 4 6 5` and the queries `[0,3)`, `[1,6)`, `[2,5)`, `[0,6)`, give the marked positions after each prefix and the four answers.

**H. Choose.** Pick a structure for each: (a) the k-th smallest in a range of a static array, queries arriving online; (b) a million point updates at positions up to 10^18 whose positions are all known before you start, then range sums; (c) the same, with positions that arrive online; (d) the number of distinct values in each of 10^6 ranges, all given in advance; (e) "set every element of a range to the minimum of itself and `x`" followed by sums; (f) the number of elements of a range below `x`, with point updates between queries.

---

## Answers

**A.** Distinct values `10 20 30 40`; ranks `2 0 1 0 3`; `rank(25) = 2` (two distinct values, 10 and 20, are below 25).

**B.** In the tree over `[0, 7)` the left subtree `[0, 3)` splits into `[0, 1)` and `[1, 3)`, so rank 0 is a leaf at depth 2 and its path has 3 nodes; every other rank is at depth 3 with a path of 4 nodes. Rank 0 is the value 1, at positions 1 and 3, so the **second and fourth** inserts create three nodes. Total `8 * 4 - 2 = 30`. The counts after each insert, `4 7 11 14 18 22 26 30`, show steps of 4, 3, 4, 3, 4, 4, 4, 4.

**C.** `a[2..8) = 4 1 5 9 2 6`, sorted `1 2 4 5 6 9`, so the 4th smallest is **5**. The descent over versions 8 and 2:

| Ranks | `k` | Left difference | Move |
|---|---|---|---|
| `[0, 7)` | 4 | 2 (values 1, 2) | right, `k = 2` |
| `[3, 7)` | 2 | 2 (values 4, 5) | left |
| `[3, 5)` | 2 | 1 (value 4) | right, `k = 1` |

It ends at rank 4, the value 5.

**D.** After `add(5, 3)`: 4 nodes; after `add(5, 2)`: 4 (the path exists); after `add(12, 4)`: 8. `find_prefix(5) = 5` (the sum up to position 5 is 5), `find_prefix(6) = 12` (the sum up to 12 reaches 9), `find_prefix(10)` is `npos` (the total is 9). Program output: the same.

**E.** Node `[0,4)` holds `1 1 3 4`, node `[4,8)` holds `2 5 6 9`. For `count_less(2, 8, 4)` the range is `4 1 5 9 2 6`, and the elements below 4 are `1` and `2`: **2**. For `count_in(0, 8, 2, 5)` the elements in `[2, 5)` are `3`, `4`, `2`: **3**.

**F.** After `chmin(0, 3, 4)`: the array is `4 3 4 6 2 7`, the root is `(mx 7, se 6, cnt 1, sum 26)`. After `chmin(0, 6, 5)`: `4 3 4 5 2 5`, the root is `(mx 5, se 4, cnt 2, sum 23)`.

**G.** The marked positions (last occurrences) after each prefix: `{0}`, `{1}`, `{1,2}`, `{2,3}`, `{2,3,4}`, `{3,4,5}`. The answers: `[0,3)` has `4, 4, 5`: **2**; `[1,6)` has `4 5 4 6 5`: **3**; `[2,5)` has `5 4 6`: **3**; `[0,6)`: **3**.

**H.** (a) The persistent tree. (b) Compress the positions and use a Fenwick tree: faster and smaller. (c) A dynamic tree: nothing is known in advance. (d) The offline sweep with a Fenwick tree. (e) Segment tree beats. (f) None of the structures of this topic: the counts change under updates, so it needs an order-statistic structure per node or an offline divide and conquer.

---

## Cheat sheet

- **Compression:** `rank(x)` = number of distinct values below `x`; the universe shrinks to at most `n`.
- **Persistent tree:** insert copies one path (about `log m` nodes); version `i` = first `i` elements; `a[l..r)` = version `r` minus version `l`; the k-th smallest descends both versions at once. `n log m` nodes, O(log n) per query.
- **Dynamic tree:** index 0 = no node; create nodes on the path of an update; missing node = sum 0. `n (depth + 1)` nodes at most.
- **Merge sort tree:** sorted list per node, binary search per covering node: O(log² n) per count, `n log n` memory.
- **Beats:** per node `mx`, strict `se`, `cnt`, `sum`; `chmin(x)`: stop if `x >= mx`, cap if `se < x`, else recurse.
- **Offline:** sort queries by right end, sweep, Fenwick tree; distinct = marks at last occurrences. Online: `count_less(l, r, l)` over `prev`.
- **Measured (500,000 elements):** persistent `count_less` is 2.8 to 2.9 times faster than the merge sort tree; the offline distinct sweep is 4.0 to 4.1 times faster than the persistent route; compress + Fenwick is 3.2 to 3.7 times faster than the dynamic tree; beats is about 90 to 120 times faster than a loop.

---

## Sources

- The persistent segment tree for the k-th smallest in a range, the merge sort tree and the offline distinct-count sweep are standard in the competitive-programming literature (for example the cp-algorithms articles on segment trees and on Fenwick trees); the code here is my own implementation, tested as above.
- Segment tree beats: Ruyi Ji's Chinese national team training paper (2016) is the original source of the technique; the amortised bounds quoted in section 7 are from memory and were not proved or checked here.
- The AtCoder Library and the cp-algorithms "Segment Tree" article, for the conventions of Topics 21 and 22 that this topic builds on.
- The remark that dynamic trees back sparse-address bookkeeping is from general knowledge and was not measured here.
- LeetCode, by name: Range Sum Query - Mutable, Count of Smaller Numbers After Self, Count of Range Sum, My Calendar III, Range Module, The Skyline Problem.
