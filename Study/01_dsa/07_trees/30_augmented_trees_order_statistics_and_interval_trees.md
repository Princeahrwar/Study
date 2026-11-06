# DSA 07.30: Augmented Trees, Order Statistics and Interval Trees

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (5 reading, 3 problems) · You need: [Randomized and amortized trees](14_randomized_and_amortized_trees.md) (the treap that carries the augmentation here), [Inside std::set and std::map](09_inside_std_set_and_map.md) (the GNU order-statistics tree it is compared with) and [The Linux kernel rbtree](13_linux_kernel_rbtree.md) (section 8 shows the kernel's augmentation callbacks, which this tutorial replaces by a single `pull`). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

A balanced search tree answers "is x in the set?" and "what is next to x?" in O(log n). Many problems ask more: "what is the 5,000th smallest element?", "how many elements are below x?", "does any stored interval overlap this one?". A plain tree cannot do these faster than a scan, because the answer depends on whole subtrees. The fix is to **augment** the tree: store in each node one extra value that summarises its subtree, keep it up to date, and let a query skip a subtree by looking at its summary. The two classic cases are **subtree sizes** (rank and select, the *order-statistic tree*) and the **largest interval end in a subtree** (overlap queries, the *interval tree*). The Linux kernel has an interval tree built exactly this way.

By the end you can:

- say when a field can be maintained by a tree (it is computable from the node and its two children alone) and when it cannot, and write the `pull` that keeps it right;
- build an **order-statistic tree** (select the k-th, rank of a key, count in a range) and use it for sliding-window medians and "count of smaller numbers";
- build an **interval tree**, prove the one-line descent that finds an overlap, report all k overlaps in O(k log n), and count overlaps with two order-statistic trees;
- say what the kernel's interval tree is and where it is used, marking what is from memory;
- choose between this, `std::map` neighbour checks, the GNU tree, a sorted vector, and offline methods, with measurements.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 367,455 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: `AugTreap` with each of the three monoids (sum, max, min) against `std::map` over random mixes of insert, erase, find, `kth`, `rank`, `lower_bound` and range `fold`, with duplicate first keys and **every invariant checked after every operation** (key order, heap order of the priorities, and the stored `size` and `agg` of every node equal to what `pull` recomputes); `OrderMultiset` against a sorted vector; `IntervalTree` against a brute-force list over random inserts, erases and queries (the set reported by `all_overlaps`, in order, and the answer of `any_overlap`), with equal starts, single-point intervals and ends that touch; the exact results of the textbook example below; the empty tree, bad arguments, and a tree of 200,000 ascending keys (the worst case for a plain BST) and 200,000 random intervals. Finally 84 hand-made mutants of the code were run against the tests (a wrong comparison in the descent, a missing `pull`, a swapped merge order and so on): **78 were caught and 6 are equivalent changes** that do not alter any output (listed after the common mistakes).

**Data and timing.** The data is **synthetic**: random 30-bit values for the order statistics, and intervals whose start is uniform below 10^9 and whose length is uniform up to a limit, which sets how many intervals a query meets. Times are on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 29), best of 3 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about the kernel are from memory and say so.

---

## 1. The idea, and the rule that limits it

Choose a base tree that stays balanced (here a treap, Topic 14), choose a field, and prove it is maintainable. The rule (CLRS, named from memory) is: **a field is maintainable if it can be computed from the node itself and the fields of its two children.** Then any change to the tree's shape needs the field recomputed only on the nodes whose children changed, which is a path of O(log n) nodes (or O(1) per rotation).

| Field | Computed as | Lets you |
|---|---|---|
| `size` | 1 + size(left) + size(right) | select the k-th, rank a key |
| `agg` with sum | agg(left) + val + agg(right) | sum of values over a key range |
| `agg` with max of interval ends | max(agg(left), end, agg(right)) | find an interval that meets a query |
| `agg` with min of deadlines | min of the three | the earliest deadline among the eligible (Topic 13, EEVDF) |

And two things that are **not** maintainable: the **depth** of a node (a rotation changes the depth of a whole subtree, not O(1) nodes) and its **rank in the whole tree** (it depends on nodes outside its subtree). What a node can store is what its subtree looks like; what is above it must be found by walking down from the root and adding up as you go, which is how `kth` and `rank` below work.

With a treap, the "shape changes" are only `split` and `merge`, and each ends with one call, `pull(node)`. That single function replaces the three callbacks (`propagate`, `copy`, `rotate`) that the kernel's red-black tree needs, because a split or merge never rotates.

```cpp
// A treap (a binary search tree with random priorities, expected depth O(log n)) whose nodes also keep, for the subtree below them,
//   size : the number of nodes, and
//   agg  : the fold of `val` over the subtree in key order, for a monoid M (an associative op with an identity).
// pull() recomputes both from a node and its two children. Every change to the shape (split, merge, insert, erase) ends by calling
// pull on the nodes it touched, bottom-up, so the fields are always right and the rest of the code never has to think about them.
using Key = std::pair<long long, int>;         // (value, serial number): equal values are told apart by the serial

struct SumM { using T = long long; static T id() { return 0; }         static T op(T a, T b) { return a + b; } };
struct MaxM { using T = long long; static T id() { return LLONG_MIN; } static T op(T a, T b) { return std::max(a, b); } };
struct MinM { using T = long long; static T id() { return LLONG_MAX; } static T op(T a, T b) { return std::min(a, b); } };

template <class M>
struct AugTreap {
    using T = typename M::T;
    struct Node {
        Key key; T val, agg; int size = 1; unsigned long long prio; Node *l = nullptr, *r = nullptr;
    };
    Node* root = nullptr;
    unsigned long long seed = 0x9E3779B97F4A7C15ull;

    AugTreap() = default;
    AugTreap(const AugTreap&) = delete;
    AugTreap& operator=(const AugTreap&) = delete;
    ~AugTreap() { destroy(root); }
    static void destroy(Node* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }
    unsigned long long next_prio() {           // splitmix64: a fixed stream, so every run builds the same trees
        unsigned long long z = (seed += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    static int size(const Node* t) { return t ? t->size : 0; }
    static T agg(const Node* t) { return t ? t->agg : M::id(); }
    static void pull(Node* t) {
        t->size = 1 + size(t->l) + size(t->r);
        t->agg = M::op(M::op(agg(t->l), t->val), agg(t->r));      // key order: left subtree, this node, right subtree
    }
    static void split(Node* t, const Key& k, Node*& a, Node*& b) {   // a gets the keys < k, b the keys >= k
        if (!t) { a = b = nullptr; return; }
        if (t->key < k) { split(t->r, k, t->r, b); a = t; pull(a); }
        else { split(t->l, k, a, t->l); b = t; pull(b); }
    }
    static Node* merge(Node* a, Node* b) {     // every key of a is below every key of b
        if (!a) return b;
        if (!b) return a;
        if (a->prio > b->prio) { a->r = merge(a->r, b); pull(a); return a; }
        b->l = merge(a, b->l); pull(b); return b;
    }

    int n() const { return size(root); }
    const Node* find(const Key& k) const {
        const Node* x = root;
        while (x && x->key != k) x = k < x->key ? x->l : x->r;
        return x;
    }
    static Node* insert_at(Node* t, Node* x) { // x goes above the first node of lower priority, splitting the subtree there
        if (!t) return x;
        if (x->prio > t->prio) { split(t, x->key, x->l, x->r); pull(x); return x; }
        if (x->key < t->key) t->l = insert_at(t->l, x); else t->r = insert_at(t->r, x);
        pull(t);
        return t;
    }
    bool insert(const Key& k, T v) {           // false if the key is already there
        if (find(k)) return false;
        Node* x = new Node{k, v, v, 1, next_prio()};
        pull(x);
        root = insert_at(root, x);
        return true;
    }
    static Node* erase_at(Node* t, const Key& k, bool& found) {
        if (!t) return t;
        if (k < t->key) t->l = erase_at(t->l, k, found);
        else if (t->key < k) t->r = erase_at(t->r, k, found);
        else { found = true; Node* m = merge(t->l, t->r); delete t; return m; }
        pull(t);
        return t;
    }
    bool erase(const Key& k) { bool found = false; root = erase_at(root, k, found); return found; }

    const Node* kth(int k) const {             // the node with k keys below it (k from 0), or null
        if (k < 0 || k >= n()) return nullptr;
        const Node* x = root;
        for (;;) {
            int ls = size(x->l);
            if (k < ls) x = x->l;
            else if (k == ls) return x;
            else { k -= ls + 1; x = x->r; }
        }
    }
    int rank(const Key& k) const {             // how many keys are below k
        int r = 0;
        for (const Node* x = root; x;) {
            if (x->key < k) { r += size(x->l) + 1; x = x->r; } else x = x->l;
        }
        return r;
    }
    const Node* lower_bound(const Key& k) const {   // the first node with key >= k
        const Node* best = nullptr;
        for (const Node* x = root; x;) {
            if (x->key < k) x = x->r; else { best = x; x = x->l; }
        }
        return best;
    }
    T fold(const Key& lo, const Key& hi) {     // the fold of val over the keys in [lo, hi): cut the range out, read its agg, put it back
        Node *a, *b, *m, *c;
        split(root, lo, a, b);
        split(b, hi, m, c);
        T r = agg(m);
        root = merge(a, merge(m, c));
        return r;
    }

    // Every rule at once: key order, heap order of the priorities, and size and agg equal to what pull would compute.
    bool valid() const { return valid(root, nullptr, nullptr); }
    static bool valid(const Node* t, const Key* lo, const Key* hi) {
        if (!t) return true;
        if ((lo && !(*lo < t->key)) || (hi && !(t->key < *hi))) return false;
        if ((t->l && t->l->prio > t->prio) || (t->r && t->r->prio > t->prio)) return false;
        if (t->size != 1 + size(t->l) + size(t->r)) return false;
        if (!(t->agg == M::op(M::op(agg(t->l), t->val), agg(t->r)))) return false;
        return valid(t->l, lo, &t->key) && valid(t->r, &t->key, hi);
    }
};

// Order statistics on a multiset of numbers: the serial number breaks ties, so equal values are different keys.
struct OrderMultiset {
    AugTreap<SumM> t; int serial = 0;
    void insert(long long x) { t.insert({x, serial++}, 1); }
    bool erase_one(long long x) {              // removes one copy of x, if there is one
        auto* p = t.lower_bound({x, INT_MIN});
        return p && p->key.first == x && t.erase(p->key);
    }
    int size() const { return t.n(); }
    std::optional<long long> kth(int k) const { auto* p = t.kth(k); return p ? std::optional<long long>(p->key.first) : std::nullopt; }
    int count_less(long long x) const { return t.rank({x, INT_MIN}); }
    int count_at_most(long long x) const { return t.rank({x, INT_MAX}); }
};

// Closed intervals [lo, hi]. Key = (lo, id), val = hi, agg = the largest hi in the subtree.
struct IntervalTree {
    using Tree = AugTreap<MaxM>;
    using Node = Tree::Node;
    Tree t;
    bool insert(long long lo, long long hi, int id) {
        if (lo > hi) throw std::invalid_argument("IntervalTree: lo > hi");
        return t.insert({lo, id}, hi);
    }
    bool erase(long long lo, int id) { return t.erase({lo, id}); }
    // Some interval that meets [a, b], or null. Go left whenever the left subtree holds an interval that ends at or after a:
    // if none of those meets [a, b], they all start after b, and so does everything to the right.
    const Node* any_overlap(long long a, long long b) const {
        if (a > b) throw std::invalid_argument("IntervalTree: a > b");
        const Node* x = t.root;
        while (x && !(x->key.first <= b && x->val >= a))
            x = (x->l && x->l->agg >= a) ? x->l : x->r;
        return x;
    }
    // Calls f(lo, hi, id) for every interval that meets [a, b], in order of lo: O(k log n) for k results.
    template <class F> void all_overlaps(long long a, long long b, F f) const {
        if (a > b) throw std::invalid_argument("IntervalTree: a > b");
        walk(t.root, a, b, f);
    }
    template <class F> static void walk(const Node* x, long long a, long long b, F& f) {
        if (!x || x->agg < a) return;          // nothing in this subtree ends at or after a
        walk(x->l, a, b, f);
        if (x->key.first > b) return;          // this node and everything to its right start after b
        if (x->val >= a) f(x->key.first, x->val, x->key.second);
        walk(x->r, a, b, f);
    }
};
```

Read the code in this order: `pull` (the whole idea), `split` and `merge` (each recomputes the nodes it touched on the way back up), `insert_at` and `erase_at` (built from them), then the queries.

## 2. Order statistics: select and rank

With `size` in every node, two descents answer the main questions.

**`kth(k)`** (the node with exactly k keys below it): at a node with `ls` keys in its left subtree, go left if `k < ls`, stop if `k == ls`, else subtract `ls + 1` and go right. **`rank(key)`** (how many keys are below `key`): descend by key; every time you go right, add the left subtree's size plus one.

Trace: the keys 10, 20, ..., 70 inserted in order give this treap (each node is `key:size`, written `(left key:size right)`):

```
((10:2 (20:1)) 30:7 (((40:1) 50:3 (60:1)) 70:4))
```

To find `kth(3)` (the fourth smallest): at the root 30 the left subtree has size 2, and 3 > 2, so subtract 3 and go right with k = 0; at 70 the left subtree (rooted at 50) has size 3, and 0 < 3, so go left; at 50 the left subtree has size 1 and 0 < 1, so go left; at 40 the left subtree is empty and k == 0: the answer is **40**. The path touches 30, 70, 50, 40. `rank(35)` goes 30 (add 2 + 1), then 70 (go left), 50 (go left), 40 (key 40 is not below 35, go left, empty): 3. After erasing 40 the sizes above it are recomputed on the way back up and `kth(3)` is 50:

```
((10:2 (20:1)) 30:6 ((50:2 (60:1)) 70:3))
```

The same tree gives **range sums** (`fold` over a key range, by cutting the range out with two splits, reading its `agg`, and merging back) and **counts in a range**. For a multiset of numbers the key is `(value, serial number)`, so equal values are different keys; `OrderMultiset` wraps that, with `count_less`, `count_at_most` and `kth` (so the **median of a sliding window** is `kth(W / 2)` after erasing the oldest and inserting the newest).

The GNU order-statistics tree of Topic 9 does the same on a red-black tree. The treap is shorter to write and works anywhere; the GNU one is a GCC extension and faster (below).

## 3. Interval trees

Store closed intervals `[lo, hi]`. The tree is ordered by `lo` (ties by an id), each node's `val` is its `hi`, and `agg` is the **largest `hi` in the subtree**. Two intervals **overlap** when `lo1 <= hi2 && lo2 <= hi1`; a query `[a, b]` meets `[lo, hi]` when `lo <= b && hi >= a` (so touching ends count).

### Finding one overlap

```
x = root
while x and x does not meet [a, b]:
    if x.left exists and x.left.agg >= a: x = x.left else x = x.right
```

Why it never misses. Suppose the left subtree holds an interval ending at or after `a` (that is what `left.agg >= a` says). If one of the left intervals meets `[a, b]`, going left finds it. If **none** does, the interval with `hi >= a` must fail the other condition, so it starts after `b`; but every interval on the right has `lo` at least as large as the left ones, hence also starts after `b`, so nothing on the right meets the query either, and going left to find nothing is the correct answer. If the left has no interval ending at or after `a`, no left interval can meet the query, so go right. One path, O(log n) steps.

### Finding all of them

`all_overlaps` visits the left subtree unless `agg < a` prunes it, reports the node if it meets the query, and visits the right subtree only if the node's `lo <= b` (otherwise everything to the right starts too late). The cost is O(k log n) for k results.

The classic textbook intervals `[15,20] [10,30] [17,19] [5,20] [12,15] [30,40]` (ids 0 to 5) give this treap (each node is `lo-hi:agg`):

```
((((5-20:30 (10-30:30)) 12-15:30) 15-20:30) 17-19:40 (30-40:40))
```

| Query | All overlaps (in order of lo) | `any_overlap` | Nodes visited by `all_overlaps` |
|---|---|---|---|
| [14, 16] | 5-20, 10-30, 12-15, 15-20 | 15-20 | 5 |
| [21, 23] | 10-30 | 10-30 | 6 |
| [31, 39] | 30-40 | 30-40 | 3 |
| [41, 50] | none | none | 1 |
| [20, 20] | 5-20, 10-30, 15-20 | 15-20 | 6 |

The query [41, 50] costs one node: the root's `agg` is 40, which is below 41, so the whole tree is pruned. `any_overlap(21, 23)` walks 17-19 (no overlap; the left subtree's agg 30 is at least 21, go left), 15-20 (no; go left), 12-15 (no; go left), 5-20 (no; no left child, go right) and 10-30 (yes).

### Counting overlaps

To **count** the intervals that meet `[a, b]` you do not need to visit them. An interval misses the query exactly when it ends before `a` or starts after `b`, and it cannot do both (since `lo <= hi` and `a <= b`). So

count = n - (number with `hi < a`) - (number with `lo > b`),

and each of the two counts is a `count_less` on an `OrderMultiset` (one keyed by `hi`, one by `lo`). For the six intervals and [14, 16]: no end is below 14, two starts (17 and 30) are above 16: 6 - 0 - 2 = **4**, as in the table. For [21, 23]: four ends are below 21 (20, 19, 20, 15), one start is above 23: 6 - 4 - 1 = **1**.

### The kernel's interval tree

From memory, not re-read for this file. Linux has a generic interval tree (`include/linux/interval_tree.h`, `lib/interval_tree.c`, with a generator macro `INTERVAL_TREE_DEFINE` in `interval_tree_generic.h`). A node holds `start`, `last` (a **closed** interval, like here) and `__subtree_last`, the largest `last` in its subtree, in an augmented rbtree (Topic 13); `interval_tree_iter_first` and `interval_tree_iter_next` return the overlaps one at a time, pruned by `__subtree_last` as above. It is used where an address range must be mapped back to the things that cover it, for example the reverse mapping of file pages (`i_mmap`) and anonymous memory (`anon_vma`), and in some drivers. Topic 31 returns to the maple tree that replaced the VMA rbtree.

## 4. What it costs

Numbers are **run 1 / run 2**.

**Shape.** After 1,000,000 random-key inserts the deepest node of the treap is at depth 49 and the mean depth is 26.86; after 1,000,000 ascending inserts they are 51 and 26.31 (2 ln n is 27.6); both trees pass the validator.

**Sliding window**: erase the oldest value, insert the newest, read the median, in a window of W values (steps per row as shown; ns per step; the three structures gave the same sum of medians).

| W | Steps | `AugTreap` (`OrderMultiset`) | Sorted `std::vector` | GNU order-statistics tree |
|---|---|---|---|---|
| 100 | 1,000,000 | 361.5 / 368.0 | 110.7 / 115.5 | 222.6 / 257.6 |
| 10,000 | 1,000,000 | 768.8 / 861.5 | 453.9 / 456.0 | 456.7 / 456.0 |
| 100,000 | 300,000 | 1,753.2 / 1,799.8 | 8,440.1 / 8,485.3 | 986.7 / 1,174.4 |

`kth` on a static set of 10^6 keys (10^6 queries): treap 1,662.2 / 1,687.5 ns, GNU tree 1,223.9 / 1,291.3 ns (run 1 / run 2).

**Interval tree**, 10^6 intervals, 10^6 point queries `[q, q]`. "Limit" is the largest interval length.

| Limit | Results per query | Nodes visited per query | Build (ms) | `any_overlap` (ns) | `all_overlaps` (ns) | Scan of all intervals (ns) | Erase all (ms) |
|---|---|---|---|---|---|---|---|
| 100 | 0.051 | 37.85 | 1,687.9 / 1,767.0 | 1,517.4 / 1,497.2 | 1,323.9 / 1,317.9 | 3,297,241 / 3,332,328 | 1,163.1 / 1,080.6 |
| 20,000 | 9.994 | 52.37 | 1,705.3 / 1,719.1 | 731.1 / 765.1 | 3,099.2 / 3,290.0 | 3,296,448 / 3,601,229 | 1,156.5 / 1,166.0 |
| 2,000,000 | 998.799 | 1713.13 | 1,879.2 / 1,749.2 | 374.4 / 369.0 | 143,435.1 / 145,331.3 | 3,678,288 / 3,300,724 | 1,625.1 / 1,172.9 |

What both runs agree on:

- **The vector is the fastest at small windows and the worst at large ones.** At W = 100 the sorted vector costs 110.7 / 115.5 ns a step against 361.5 / 368.0 for the treap; at W = 10,000 the vector (453.9 / 456.0) and the GNU tree (456.7 / 456.0) are about equal; at W = 100,000 the vector (8,440.1 / 8,485.3) is 4.7 to 4.8 times slower than the treap. The vector's insert and erase are O(W) moves of contiguous memory, which is cheap until the window is long.
- **The GNU tree is faster than this treap** at every window (about 1.4 to 1.9 times) and for `kth` on a static set; the treap pays for a `new` and a `delete` per key and a recursive `insert_at`. It is the shorter and more general structure, not the faster one.
- **The treap's shape does not depend on the insertion order:** the depth numbers for random and ascending keys are alike, near 2 ln n, which is what the random priorities buy.
- **An overlap query visits about k + 2 log2 n nodes**, and `all_overlaps` costs about 130 to 150 ns per reported interval when k is large; a scan of all intervals costs about 3.3 to 3.7 **milliseconds** a query, thousands of times more than `all_overlaps` for few results.
- `any_overlap` is the cheaper the more overlaps there are, because the descent stops at the first hit; when nothing overlaps it goes down to a leaf.

## 5. Choosing

- **Dynamic k-th, rank, or count below x:** this tree, or the GNU one on GCC. For a fixed set of values known in advance, a Fenwick tree over compressed coordinates (Topic 20) is smaller and faster, and a window of a few thousand elements is better served by a sorted vector.
- **Intervals that never overlap each other** (a calendar with no double booking): an ordered map is enough. Look at the neighbours of the new interval (the one that starts at or before it and the next one); that is exact only because stored intervals are disjoint. If stored intervals may overlap, a long early interval can hide behind a short one and the neighbour check misses it; use the interval tree.
- **A fixed set of intervals, many stabbing queries:** sort and sweep, or a static structure (a segment tree over the endpoints); no dynamic tree is needed.
- **Many overlapping intervals, inserts and erases mixed with queries:** the interval tree.
- **Another summary** (sum, min, max, gcd, count of a property): the same `AugTreap` with another monoid; the monoid must be associative, and `pull` applies it in key order.
- **Updates that change a whole range** (add 5 to every key in a range): the treap needs lazy tags as well (Topic 22 does this for a segment tree); this tree does not.

## 6. Common mistakes

- **Forgetting `pull` on one path.** Any change to a child pointer without a `pull` of its parent leaves a stale `size` or `agg` that shows up much later and far away. In this code each of `split`, `merge`, `insert_at` and `erase_at` ends by calling it; the validator recomputes every node to catch a miss at once.
- **Pull order for a non-commutative operation.** `agg` is left subtree, then the node, then the right subtree. For sum, min and max the order does not matter; for concatenation or matrix product it does.
- **Using a field that is not local.** Depth, or "my rank in the tree", cannot be kept (section 1). Store the subtree summary and compute the rest on the way down.
- **Equal keys.** A tree keyed by `lo` alone cannot hold two intervals with the same start, and erasing "the interval starting at 5" is ambiguous. Break ties with an id, and erase by `(lo, id)`.
- **Open against closed intervals.** The code treats `[a, b]` as closed, so `[1, 3]` and `[3, 5]` overlap. Problems that use half-open intervals `[a, b)` need `lo < b` and `hi > a`, not the tests here.
- **Pruning with the wrong end.** The left subtree is skipped when its maximum end is **below `a`**, not when it is below `b`.
- **The serial number running out.** `OrderMultiset` numbers inserts with an `int`, so it can take about 2 billion inserts over its life, not at once.
- **Recursion depth.** The recursive treap is safe because its depth is O(log n) with high probability (about 50 here for 10^6 keys); a degenerate tree (a fixed priority) would overflow the stack.

The equivalent mutants (changes that do not alter any output, so no test can catch them): (1) an extra `if (t->size == 0) return;` in `split`, since a stored size is never 0; (2) skipping `pull` on a brand new node, whose `size` and `agg` start out right; (3) merging as `merge(merge(a, m), c)` instead of `merge(a, merge(m, c))`, because a treap's shape is fixed by its keys and priorities, so both orders build the same tree; (4) `OrderMultiset` storing the value 0 instead of 1 in each node, since it only reads `size`; (5) `lower_bound({x, 0})` instead of `{x, INT_MIN}` in `erase_one`, since serial numbers start at 0; (6) removing the `x->agg < a` prune from `all_overlaps`, which keeps every answer but visits whole subtrees, so only the cost (the visited counts in section 3) changes, and no test of the output can see it. Every other change was caught by a test.

## 7. Questions to answer out loud

1. **What is an augmented tree?** A balanced tree whose nodes store a summary of their subtree, kept up to date by recomputing from the children, so queries can skip subtrees.
2. **What can be stored?** Anything computable from the node and its two children: size, sum, min, max, an interval-end maximum. Not depth, not global rank.
3. **How do you find the k-th smallest?** Compare k with the size of the left subtree: left if smaller, here if equal, else subtract and go right.
4. **How do you find the rank of a key?** Descend by key; add left size plus one each time you go right.
5. **What does an interval tree store, and what is the descent?** Intervals ordered by start; the largest end in each subtree; go left if the left subtree's maximum end is at least the query's start, else right.
6. **Why is that descent correct?** If the left side has an end at least `a` but nothing meets the query, that interval starts after `b`, and so does everything to the right.
7. **How do you list all overlaps?** Prune a subtree when its maximum end is below `a`, and stop at a node whose start is above `b`; O(k log n).
8. **How do you count overlaps without listing them?** n minus the number of ends below `a` minus the number of starts above `b`, with two order-statistic trees.
9. **Where does the kernel use an interval tree?** An augmented rbtree of closed ranges (`start`, `last`, `__subtree_last`) for reverse mapping of memory (from memory).

## 8. Problems

**A.** For the six intervals of section 3, say which are returned for the queries [14, 16], [21, 23] and [20, 20], and why [20, 20] returns three.

**B.** Walk `any_overlap(21, 23)` on the printed tree and list the nodes visited in order.

**C.** Count the intervals meeting [14, 16] and [21, 23] with the formula of section 3, and show the two counts for each.

**D.** In the order tree of section 2 compute `kth(3)` by hand, using only the sizes, and `rank(35)`. What are the sizes of the nodes on the path after erasing 40?

**E.** The stream 1, 3, -1, -3, 5, 3, 6, 7 with a window of 3: give the median of every window. How is each computed with `OrderMultiset`?

**F.** Why can a node not store its own depth as an augmented field?

**G.** Prove that `any_overlap` is correct: if the loop ends with null, no stored interval meets the query.

**H.** Choose a structure: (a) an online stream with "how many earlier elements are larger than this one" for each new element; (b) a calendar where a new event is refused if it overlaps any stored one, and stored events never overlap; (c) intervals that may overlap each other, with inserts, erases and "does anything cover this time" queries; (d) a million fixed intervals and a million stabbing queries; (e) the maximum value in a key range of a changing set.

### Answers

**A.** [14, 16]: 5-20, 10-30, 12-15, 15-20 (each has `lo <= 16` and `hi >= 14`; 17-19 starts after 16 and 30-40 later still). [21, 23]: only 10-30 (the others end at 20 or below or start at 30). [20, 20]: 5-20, 10-30 and 15-20, because the ends are closed: an interval ending at 20 contains the point 20 (17-19 ends at 19 and does not; 30-40 starts at 30).

**B.** 17-19 (does not meet; the left subtree's agg is 30, at least 21, so go left), 15-20 (no; left agg 30, go left), 12-15 (no; left agg 30, go left), 5-20 (no; it has no left child, so go right), 10-30 (meets). Five nodes, answer 10-30.

**C.** For [14, 16]: ends below 14: none; starts above 16: 17 and 30, two; 6 - 0 - 2 = 4. For [21, 23]: ends below 21: 20, 19, 20, 15, four; starts above 23: 30, one; 6 - 4 - 1 = 1. Both agree with the lists.

**D.** `kth(3)`: root 30:7 has left size 2; 3 > 2, so k = 3 - 3 = 0 and go right to 70:4, whose left subtree 50:3 has size 3, so go left (0 < 3); 50's left subtree 40:1 has size 1, go left (0 < 1); at 40 the left subtree is empty and k = 0: answer 40. `rank(35)`: at 30, 30 < 35, add 2 + 1 = 3 and go right; at 70, go left; at 50, go left; at 40, 40 >= 35, go left, empty: 3. After erasing 40 the sizes on the path are 60-side 50:2, 70:3, 30:6, as in `((10:2 (20:1)) 30:6 ((50:2 (60:1)) 70:3))`, and `kth(3)` is 50.

**E.** The windows are [1, 3, -1], [3, -1, -3], [-1, -3, 5], [-3, 5, 3], [5, 3, 6], [3, 6, 7]; the medians are 1, -1, -1, 3, 5, 6. With `OrderMultiset`: insert the first three values, then for each step read `kth(1)` (W / 2 for W = 3), `erase_one` the oldest and `insert` the next.

**F.** A rotation (or a split or merge) lifts a whole subtree one level or drops it, so the depth of every node in that subtree changes, which is not O(1) nodes. The height of a subtree, in contrast, is computable from its children (1 + max of the two) and can be kept.

**G.** Claim: if some stored interval meets `[a, b]`, the loop finds one. Induction on the subtree: the loop is at a node `x` whose subtree contains a meeting interval (true at the root). If `x` itself meets, stop. Otherwise, if `x.left.agg >= a`, some left interval `I` has `hi >= a`. Either a left interval meets the query, and it is in `x.left`'s subtree, or none does; in the second case `I` has `hi >= a` but does not meet, so `lo(I) > b`, and then `x` and everything to its right have `lo >= lo(I) > b` and cannot meet either, contradicting that the subtree contains a meeting interval. So going left is safe. If `x.left.agg < a`, no left interval meets (all end before `a`), so the meeting interval is on the right.

**H.** (a) An order-statistic tree (`count_at_most` on the elements so far), or a Fenwick tree if values are compressed in advance. (b) An ordered map with a neighbour check (disjoint intervals). (c) The interval tree. (d) Sort and sweep, or a static structure; no dynamic tree. (e) The same augmented treap with the max monoid (`fold` over the key range).

---

## Cheat sheet

- **Rule:** a field is maintainable iff it is computed from the node and its children; `pull` recomputes it; call it after every change to a child pointer.
- **Order statistics:** `size` per node; `kth`: compare k with the left size; `rank`: add left size + 1 on each right step.
- **Interval tree:** key = start, val = end, `agg` = largest end; find one: go left if `left.agg >= a`; find all: prune `agg < a`, stop at `lo > b`; O(k log n).
- **Count overlaps:** n - (ends below `a`) - (starts above `b`).
- **Closed intervals** meet when `lo <= b` and `hi >= a`; half-open need strict tests.
- **Kernel:** augmented rbtree with `start`, `last`, `__subtree_last` (from memory).
- **Measured (treap, 10^6 keys):** depth near 2 ln n whatever the insertion order; the GNU tree is about 1.4 to 1.9 times faster; a sorted vector wins up to a window of about 10^4 and loses badly at 10^5; an overlap query visits about k + 2 log2 n nodes against a scan of 3.3 to 3.7 ms.

---

## Sources

- The augmentation rule, dynamic order statistics and interval trees are CLRS (the chapter on augmenting data structures; named from memory, not re-read for this file). The treap is Seidel and Aragon (1989; from memory). The code is my own implementation, tested as above; the correctness of the descent is proved in Problem G.
- The description of the kernel's `interval_tree` and its users is from memory and was not checked against the source; the GNU order-statistics tree is the GCC `pb_ds` extension used in Topic 9. All timings are measured here.
- Problems by name, from memory: LeetCode Sliding Window Median (480), Count of Smaller Numbers After Self (315), My Calendar I, II and III (729, 731, 732), Range Module (715), The Skyline Problem (218), Data Stream as Disjoint Intervals (352).
