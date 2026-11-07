# DSA 07.22: Segment Tree II: Lazy Propagation, Range Updates and Tag Algebra

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (5 reading, 4 problems) · You need: [Segment tree I](21_segment_tree_point_update_range_query.md) (the monoid, the recursive tree and the bottom-up tree) and [Fenwick tree](20_fenwick_tree.md) (the range-add, range-sum alternative). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- explain why a range update on a plain segment tree costs O(n), and how a **tag** that says "this is still owed to my children" brings it to O(log n);
- write the **recursive lazy tree** and the **bottom-up lazy tree** over a general *action* (a tag type, an identity, `apply` and `compose`), and say what each of the two conventions means;
- write the tag algebra for **range add**, **range assign**, **assign and add together** and **affine maps**, and state the one law that every tag type must satisfy;
- find the classic bugs from their symptoms: a missing push, a push in the wrong place, a tag composed in the wrong order;
- use the **no-push** trick when it is valid, and say when it is not;
- say, with measurements, what the lazy trees cost against the Fenwick range-sum tree.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 2,662,055 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: both lazy trees on six combinations of values and actions (add and sum, add and maximum, assign and sum, assign and maximum, assign-or-add and maximum, affine maps and sum modulo a prime) for every size from 1 to 40, with 300 random operations each (range updates, range queries, single reads, point sets), compared with a plain array updated **one element at a time by independent code**, a second tree built only by point sets, **every range queried** every 20 operations, and the structural invariant of the tags checked after every operation; the action law for every action on random inputs; the layout (`4n + 4` slots and `n` rounded up to a power of two); the order tests of section 3; the no-push tree against a plain array; two 20,000-element trees over 3,000 random operations; and out-of-range arguments for every class. Finally 107 hand-made mutants of the code were run against the tests (a missing push, a swapped composition, a wrong length, a lost identity and so on): **99 were caught and 8 are equivalent changes** that cannot alter any output (listed after the common mistakes).

**Data and timing.** The array is **synthetic**: 1,000,000 random integers below 10^9. The operations are random ranges `[l, r)` (average length about a third of the array) with random values in `[-1000, 1000]`, generated beforehand: 2,000,000 operations, half updates and half queries in random order (the "mixed" rows), or 1,000,000 updates (the "update only" rows). Times are nanoseconds per operation on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 21), best of 3 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Each timing includes **building the tree**. Checksums of all queries agree across the structures that compute the same function. Statements about systems practice are from general knowledge and say so.

---

## 1. The problem laziness solves

Topic 21 made point updates cheap. A **range update** ("add 5 to every element of `a[l..r)`", "set every element of `a[l..r)` to 7") on that tree means one point update per element: O(n log n) for a long range. A Fenwick tree (Topic 20) can do range add with range sum through the difference array, but not range assign and not range maximum.

The idea is to be lazy. A range update touches the O(log n) nodes that exactly cover the range (the same nodes a query would use), and at each of them it does two things: it fixes that node's own answer **right now**, and it leaves a **tag** saying "every element below this node is owed this update". Nothing below is touched. The tag is paid out ("pushed down" to the two children) only when something needs to look below the node.

Two conventions, which every line of code below depends on:

1. `t[v]` is **already correct** for the whole range of `v`, including every update that reached `v` or any ancestor that has already been pushed.
2. `z[v]` is the update still **owed to the children** of `v`. If it is the identity, the children are up to date.

From these two follow the whole algorithm: before going below a node, push its tag; before returning from a node, recompute its answer from its children.

**What a tag must be.** For the node to fix its own answer, the tag must say what it does to a summary, not only to an element: adding 5 to every element of a range of length `len` adds `5 * len` to the sum, but only 5 to the maximum. So an **action** has:

| Piece | Meaning |
|---|---|
| `G` | the type of a tag |
| `id()`, `is_id(g)` | the tag that does nothing, and a test for it |
| `apply(g, x, len)` | the new summary of a node that covers `len` elements and holds `x`, after the update `g` |
| `compose(g, old)` | one tag meaning "do `old` first, then `g`" |

and one law: **`apply(compose(g, h), x, len) = apply(g, apply(h, x, len), len)`**, with `apply(id, x, len) = x`. The tests check this law for every action. The monoid of the values is the one of Topic 21 (`SumM` is listed there; the two new ones are below).

```cpp
// A lazy tree needs a MONOID M for the values (as in Topic 21) and an ACTION F for the pending updates:
//    G                   the type of a tag ("add 5", "assign 7", "x -> 3x + 1")
//    F::id()             the tag that does nothing; F::is_id(g) tests for it
//    F::apply(g, x, len) the new value of a node that covers len elements and holds x, after the update g
//    F::compose(g, old)  one tag that means "do old first, then g".  The ORDER of the two arguments is the whole point.
// The laws: apply(compose(g, h), x, len) = apply(g, apply(h, x, len), len), and apply(id, x, len) = x.
struct LMaxM { using T = long long; static constexpr T NEG = -(1LL << 60); static T id() { return NEG; } static T op(T a, T b) { return std::max(a, b); } };
struct ModSumM { using T = long long; static constexpr long long P = 1000000007LL; static T id() { return 0; } static T op(T a, T b) { return (a + b) % P; } };

struct AddSumF {                                       // add g to every element; the node holds a sum
    using G = long long;
    static G id() { return 0; }
    static bool is_id(G g) { return g == 0; }
    static long long apply(G g, long long x, std::size_t len) { return x + g * static_cast<long long>(len); }
    static G compose(G g, G old) { return g + old; }
};
struct AddMaxF {                                       // add g to every element; the node holds a maximum (len is not needed)
    using G = long long;
    static G id() { return 0; }
    static bool is_id(G g) { return g == 0; }
    static long long apply(G g, long long x, std::size_t) { return x + g; }
    static G compose(G g, G old) { return g + old; }
};
struct AssignSumF {                                    // set every element to v; the node holds a sum
    struct G { bool set; long long v; };
    static G id() { return {false, 0}; }
    static bool is_id(const G& g) { return !g.set; }
    static long long apply(const G& g, long long x, std::size_t len) { return g.set ? g.v * static_cast<long long>(len) : x; }
    static G compose(const G& g, const G& old) { return g.set ? g : old; }          // the newer assignment wins; an empty tag keeps the old one
};
struct AssignMaxF {                                    // set every element to v; the node holds a maximum
    using G = AssignSumF::G;
    static G id() { return {false, 0}; }
    static bool is_id(const G& g) { return !g.set; }
    static long long apply(const G& g, long long x, std::size_t) { return g.set ? g.v : x; }
    static G compose(const G& g, const G& old) { return g.set ? g : old; }
};
struct AssignAddMaxF {                                 // x -> keep * x + c with keep in {0, 1}: "assign c" is (0, c), "add c" is (1, c)
    struct G { int keep; long long c; };
    static G id() { return {1, 0}; }
    static bool is_id(const G& g) { return g.keep == 1 && g.c == 0; }
    static long long apply(const G& g, long long x, std::size_t) { return g.keep ? x + g.c : g.c; }
    static G compose(const G& g, const G& old) { return {g.keep & old.keep, g.keep ? old.c + g.c : g.c}; }   // g after old
};
struct AffineSumF {                                    // x -> b x + c (mod P) on every element; the node holds a sum mod P
    struct G { long long b, c; };
    static constexpr long long P = ModSumM::P;
    static G id() { return {1, 0}; }
    static bool is_id(const G& g) { return g.b == 1 && g.c == 0; }
    static long long apply(const G& g, long long x, std::size_t len) { return (g.b * x + g.c * static_cast<long long>(len % P)) % P; }
    static G compose(const G& g, const G& old) { return {g.b * old.b % P, (g.b * old.c + g.c) % P}; }     // g after old
};
```

`AssignAddMaxF` deserves a second look. A tag `(keep, c)` means `x -> keep * x + c`, so "add `c`" is `(1, c)` and "assign `c`" is `(0, c)`. Composing "`g` after `old`" gives `keep = g.keep AND old.keep` (if either one assigns, the result no longer depends on `x`) and the constant `old.c + g.c` if `g` keeps the value, or just `g.c` if `g` assigns. This is how one tree supports both range assign and range add.

---

## 2. The recursive lazy tree

```cpp
// The recursive lazy tree.  Convention: t_[v] is ALREADY correct for its whole range, including every update that reached v;
//    z_[v] is the update still owed to the CHILDREN of v.  Before going below v, push z_[v] down.
template <class M, class F>
class LazyTree {
public:
    using T = typename M::T;
    using G = typename F::G;
protected:
    std::size_t n_;
    std::vector<T> t_;
    std::vector<G> z_;
    friend struct TestAccess;
private:
    void take(std::size_t v, std::size_t len, const G& g) {         // v receives the update g
        t_[v] = F::apply(g, t_[v], len);
        z_[v] = F::compose(g, z_[v]);                                // g comes after whatever v already owed
    }
    void push(std::size_t v, std::size_t lo, std::size_t hi) {
        if (F::is_id(z_[v])) return;
        std::size_t mid = (lo + hi) / 2;
        take(2 * v, mid - lo, z_[v]);
        take(2 * v + 1, hi - mid, z_[v]);
        z_[v] = F::id();                                             // the debt is paid
    }
    void build(std::size_t v, std::size_t lo, std::size_t hi, const std::vector<T>& a) {
        if (hi - lo == 1) { t_[v] = a[lo]; return; }
        std::size_t mid = (lo + hi) / 2;
        build(2 * v, lo, mid, a);
        build(2 * v + 1, mid, hi, a);
        t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);
    }
    void update(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r, const G& g) {
        if (r <= lo || hi <= l) return;
        if (l <= lo && hi <= r) { take(v, hi - lo, g); return; }     // fully inside: record it here and stop
        push(v, lo, hi);                                             // partial: the children must be up to date before they change
        std::size_t mid = (lo + hi) / 2;
        update(2 * v, lo, mid, l, r, g);
        update(2 * v + 1, mid, hi, l, r, g);
        t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);
    }
    T query(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r) {
        if (r <= lo || hi <= l) return M::id();
        if (l <= lo && hi <= r) return t_[v];
        push(v, lo, hi);                                             // queries push too: the children are stale until then
        std::size_t mid = (lo + hi) / 2;
        return M::op(query(2 * v, lo, mid, l, r), query(2 * v + 1, mid, hi, l, r));
    }
    void assign_at(std::size_t v, std::size_t lo, std::size_t hi, std::size_t i, const T& x) {
        if (hi - lo == 1) { t_[v] = x; z_[v] = F::id(); return; }
        push(v, lo, hi);                                             // a point set must also see the pending updates above it
        std::size_t mid = (lo + hi) / 2;
        if (i < mid) assign_at(2 * v, lo, mid, i, x); else assign_at(2 * v + 1, mid, hi, i, x);
        t_[v] = M::op(t_[2 * v], t_[2 * v + 1]);
    }
public:
    explicit LazyTree(std::size_t n = 0) : n_(n), t_(4 * n + 4, M::id()), z_(4 * n + 4, F::id()) {}
    explicit LazyTree(const std::vector<T>& a) : n_(a.size()), t_(4 * a.size() + 4, M::id()), z_(4 * a.size() + 4, F::id()) { if (n_) build(1, 0, n_, a); }
    std::size_t size() const { return n_; }
    void apply(std::size_t l, std::size_t r, const G& g) {           // apply the update g to a[l..r)
        if (l > r || r > n_) throw std::out_of_range("LazyTree::apply");
        if (l < r) update(1, 0, n_, l, r, g);
    }
    void set(std::size_t i, const T& x) {
        if (i >= n_) throw std::out_of_range("LazyTree::set");
        assign_at(1, 0, n_, i, x);
    }
    T query(std::size_t l, std::size_t r) {                          // NOT const: a query pushes tags
        if (l > r || r > n_) throw std::out_of_range("LazyTree::query");
        return n_ ? query(1, 0, n_, l, r) : M::id();
    }
    T get(std::size_t i) { if (i >= n_) throw std::out_of_range("LazyTree::get"); return query(i, i + 1); }
};
```

Three things to notice. `take(v, len, g)` is the only place a tag is recorded: it updates the node's answer with `apply` and merges the new tag into the old one with `compose(g, z)`, in that order (the new update comes **after** whatever the node already owed). `push` pays the debt to both children, each with its own length, and clears the tag. And **a query pushes too**: `query` is not `const`, because the children of a partially covered node are stale until the tag is paid. (The leaves receive tags as well; those are never read.)

For `a = 3 1 4 1 5 9 2 6` and the sum action, the tree starts as `t[1..15] = 31 9 22 4 5 14 8 3 1 4 1 5 9 2 6` with every tag zero. After `apply(2, 7, +10)` the range `[2, 7)` is covered by three nodes: node 5 (`[2,4)`), node 6 (`[4,6)`) and node 14 (`[6,7)`). Each is fixed at once (`5 -> 25`, `14 -> 34`, `2 -> 12`) and receives the tag 10; the ancestors 2, 3, 7 and 1 are recomputed on the way back:

`t = 81 29 52 4 25 34 18 3 1 4 1 5 9 12 6` and the tags are `z[5] = z[6] = z[14] = 10`.

Nothing below those three nodes moved: leaves 10 to 13 still hold `4 1 5 9`. Now `query(3, 5)` needs positions 3 and 4, which live under nodes 5 and 6. It pushes node 5, so that leaves 10 and 11 become `14` and `11`, and node 6, so that leaves 12 and 13 become `15` and `19`, and returns `11 + 15 =` **26**. Node 14 was never visited, so its tag is still waiting. Then `query(0, 8)` is the root's `t[1] = 81` at once.

**The update visits at most four nodes per level** (like the query of Topic 21), and the pushes cost O(1) each, so the update and the query are both O(log n).

**A point set** (`set`) must also push along its path. Otherwise a pending tag above the leaf would later be applied **on top of** the new value and overwrite it.

---

## 3. Composing tags: the order is the algorithm

Take four elements, all 5, and the maximum. Do two updates: assign 7 to everything, then add 3 to the first two. The answer must be 10, 10, 7, 7.

The root receives "assign 7" as a tag. The second update is partial at the root, so the root must **push first**: both children receive "assign 7" (their maxima become 7), and only then does node 2 receive "add 3" (`7 + 3 = 10`). The tags after the second update are `(0, 10)` on node 2 and `(0, 7)` on node 3: `t[1..7] = 10 10 7 5 5 5 5`, and the queries give `query(0, 2) = 10`, `query(2, 4) = 7`.

**Bug 1: no push before a partial update.** Remove the push in `update`. The root still owes "assign 7" when node 2 receives "add 3", so node 2 becomes `5 + 3 = 8`, with the tag `(1, 3)`, and the root's debt is still there. The tree now reads `t[1..7] = 8 8 5 5 5 5 5`. On the next query the root pays its debt: node 2 receives "assign 7" **after** its own "add 3", and `compose(assign 7, add 3) = (0, 7)`: the add is gone. `query(0, 2)` returns **7 instead of 10**. (This is the bug the mutants without a push in `update` produce, and the tests catch it.)

**Bug 2: the right pushes, the wrong composition.** `compose(add 3 after assign 7)` is `(0, 10)`: the result is an assignment of 10. Written the other way round (`compose(assign 7 after add 3)`) it is `(0, 7)`, which is the same wrong answer 7. The two bugs look alike from outside; only the trace of the tags tells them apart. A commutative action (two adds) hides both: the order of two adds does not matter, so the bug survives until the first assignment arrives. **Test with a non-commutative pair.**

**Affine maps** are the cleanest non-commutative example. The tag `(b, c)` means `x -> b x + c`. With `f = (2, 3)` and `g = (5, 1)`, "`g` after `f`" is `x -> 5 (2 x + 3) + 1 = 10 x + 16`, that is `(10, 16)`; "`f` after `g`" is `x -> 2 (5 x + 1) + 3 = 10 x + 5`, that is `(10, 5)`. The general rule in the code, `compose(g, old) = (g.b * old.b, g.b * old.c + g.c)`, is exactly this expansion. On a sum, `apply` is `b * x + c * len`: the constant is added to every one of the `len` elements, but the scaling applies once to the sum.

---

## 4. The bottom-up lazy tree

The bottom-up tree of Topic 21 stored the leaves at `n .. 2n-1` for any `n`. A lazy tree cannot do that: a tag at node `v` must mean "all of the node's range", so every node has to cover a proper contiguous range, and the tree must be a perfect binary tree. The bottom-up lazy tree therefore rounds `n` **up to a power of two** (`size_`) and leaves the padding at the identity value. It costs up to twice the leaves, but the padding is never read: a query only ever uses nodes entirely inside `[0, n)`.

The algorithm (the design of the AtCoder Library's lazy segment tree, here my own implementation) works along the two edges of the range. For `[l, r)`, shifted to leaf positions `l + size_` and `r + size_`:

1. **Push** the tags of all ancestors of the two edge leaves, top down. The nodes in the middle of the range need no push: they are fully inside, and receive the new tag directly.
2. Visit the covering nodes exactly as the query loop of Topic 21 does (take `l` if odd, take `r - 1` if `r` is odd, halve).
3. For an update, **recompute** the ancestors of the two edges, bottom up.

An edge ancestor is pushed or recomputed only if it is not aligned with the edge (`((l >> i) << i) != l`), because an aligned ancestor is entirely inside the range or entirely outside it.

```cpp
// The bottom-up lazy tree (the design of the AtCoder Library).  The leaves are at size_ .. 2 size_ - 1 where size_ is n rounded UP to a
// power of two: unlike the plain tree of Topic 21, a lazy tree needs every node to cover a proper range.  Before touching the nodes
// along the two edges of [l, r), push the tags of all their ancestors, top down; afterwards recompute those ancestors, bottom up.
template <class M, class F>
class IterLazyTree {
public:
    using T = typename M::T;
    using G = typename F::G;
protected:
    std::size_t n_, size_ = 1, log_ = 0;
    std::vector<T> t_;
    std::vector<G> z_;                                               // z_[k] for the internal nodes k < size_
    friend struct TestAccess;
private:
    void init() { while (size_ < n_) { size_ <<= 1; ++log_; } t_.assign(2 * size_, M::id()); z_.assign(size_, F::id()); }
    void pull(std::size_t k) { t_[k] = M::op(t_[2 * k], t_[2 * k + 1]); }
    void take(std::size_t k, const G& g) {
        std::size_t len = size_ >> (63 - __builtin_clzll(k));         // the node k covers size_ / 2^depth elements
        t_[k] = F::apply(g, t_[k], len);
        if (k < size_) z_[k] = F::compose(g, z_[k]);
    }
    void push(std::size_t k) {
        if (F::is_id(z_[k])) return;
        take(2 * k, z_[k]); take(2 * k + 1, z_[k]); z_[k] = F::id();
    }
    void push_edges(std::size_t l, std::size_t r) {                  // l, r already shifted by size_
        for (std::size_t i = log_; i >= 1; --i) {
            if (((l >> i) << i) != l) push(l >> i);
            if (((r >> i) << i) != r) push((r - 1) >> i);
        }
    }
public:
    explicit IterLazyTree(std::size_t n = 0) : n_(n) { init(); }
    explicit IterLazyTree(const std::vector<T>& a) : n_(a.size()) {
        init();
        std::copy(a.begin(), a.end(), t_.begin() + size_);
        for (std::size_t k = size_; k-- > 1;) pull(k);
    }
    std::size_t size() const { return n_; }
    void apply(std::size_t l, std::size_t r, const G& g) {
        if (l > r || r > n_) throw std::out_of_range("IterLazyTree::apply");
        if (l == r) return;
        l += size_; r += size_;
        push_edges(l, r);
        for (std::size_t a = l, b = r; a < b; a >>= 1, b >>= 1) {
            if (a & 1) take(a++, g);
            if (b & 1) take(--b, g);
        }
        for (std::size_t i = 1; i <= log_; ++i) {
            if (((l >> i) << i) != l) pull(l >> i);
            if (((r >> i) << i) != r) pull((r - 1) >> i);
        }
    }
    void set(std::size_t i, const T& x) {
        if (i >= n_) throw std::out_of_range("IterLazyTree::set");
        std::size_t p = i + size_;
        for (std::size_t j = log_; j >= 1; --j) push(p >> j);
        t_[p] = x;
        for (std::size_t j = 1; j <= log_; ++j) pull(p >> j);
    }
    T query(std::size_t l, std::size_t r) {
        if (l > r || r > n_) throw std::out_of_range("IterLazyTree::query");
        if (l == r) return M::id();
        l += size_; r += size_;
        push_edges(l, r);
        T left = M::id(), right = M::id();
        for (; l < r; l >>= 1, r >>= 1) {
            if (l & 1) left = M::op(left, t_[l++]);
            if (r & 1) right = M::op(t_[--r], right);
        }
        return M::op(left, right);
    }
    T get(std::size_t i) { if (i >= n_) throw std::out_of_range("IterLazyTree::get"); return query(i, i + 1); }
};
```

The length of a node, needed by `apply`, is `size_` divided by two to the depth of the node, and the depth is the position of the highest set bit of its index (`63 - __builtin_clzll(k)`).

A trace on six elements `3 1 4 1 5 9` (so `size_ = 8`, leaves at `t[8..13]`, and `t[14]` is padding): initially `t[1..14] = 23 9 14 4 5 14 0 3 1 4 1 5 9 0`. `apply(1, 5, +10)` covers positions 1 to 4 with three nodes: node 9 (position 1), node 12 (position 4) and node 5 (positions 2 and 3). After it the array is `t[1..14] = 63 39 24 14 25 24 0 3 11 4 1 15 9 0` and the only tag is `z[5] = 10`. Then `query(2, 3)` reads position 2, which is under node 5, so the push gives leaves 10 and 11 the values `14` and `11`, and the answer is **14**. The total, `query(0, 6)`, is 63.

---

## 5. The no-push trick

If the updates commute with each other **and** with the way a query merges, the tag never needs to be pushed: it can stay where it is forever, and the query adds the tags of the nodes it passes through. Range add with range maximum is the standard case: adding `x` to every element of a range adds `x` to its maximum.

Each node keeps `mx_[v]`, the maximum of its range counting every add at `v` and below, and `ad_[v]`, the total ever added to **all** of `v`'s range. The answer of an inner node is `max(children) + ad_[v]`, and a query adds `ad_[v]` of every partially covered node on the way back up.

```cpp
// A trick that needs no push at all, for updates that COMMUTE with each other and with the query: range add with range maximum.
//    mx_[v] = the maximum of v's range counting every add at v or below;  ad_[v] = the total added to v's whole range and never pushed.
//    A query adds the ad_ of every node it walks through on the way back up.
class AddMaxNoPush {
    std::size_t n_;
    std::vector<long long> mx_, ad_;
    friend struct TestAccess;
    void build(std::size_t v, std::size_t lo, std::size_t hi, const std::vector<long long>& a) {
        if (hi - lo == 1) { mx_[v] = a[lo]; return; }
        std::size_t mid = (lo + hi) / 2;
        build(2 * v, lo, mid, a); build(2 * v + 1, mid, hi, a);
        mx_[v] = std::max(mx_[2 * v], mx_[2 * v + 1]);
    }
    void add(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r, long long x) {
        if (r <= lo || hi <= l) return;
        if (l <= lo && hi <= r) { mx_[v] += x; ad_[v] += x; return; }
        std::size_t mid = (lo + hi) / 2;
        add(2 * v, lo, mid, l, r, x); add(2 * v + 1, mid, hi, l, r, x);
        mx_[v] = std::max(mx_[2 * v], mx_[2 * v + 1]) + ad_[v];      // children plus what was added to all of v
    }
    long long query(std::size_t v, std::size_t lo, std::size_t hi, std::size_t l, std::size_t r) const {
        if (r <= lo || hi <= l) return LMaxM::NEG;
        if (l <= lo && hi <= r) return mx_[v];
        std::size_t mid = (lo + hi) / 2;
        return std::max(query(2 * v, lo, mid, l, r), query(2 * v + 1, mid, hi, l, r)) + ad_[v];
    }
public:
    explicit AddMaxNoPush(const std::vector<long long>& a) : n_(a.size()), mx_(4 * a.size() + 4, LMaxM::NEG), ad_(4 * a.size() + 4, 0) { if (n_) build(1, 0, n_, a); }
    std::size_t size() const { return n_; }
    void add(std::size_t l, std::size_t r, long long x) {
        if (l > r || r > n_) throw std::out_of_range("AddMaxNoPush::add");
        if (l < r) add(1, 0, n_, l, r, x);
    }
    long long query(std::size_t l, std::size_t r) const {            // the maximum of a[l..r); NEG for an empty range
        if (l > r || r > n_) throw std::out_of_range("AddMaxNoPush::query");
        return (l < r) ? query(1, 0, n_, l, r) : LMaxM::NEG;
    }
};
```

On `3 1 4 1 5 9 2 6`, `add(1, 6, +10)` covers positions 1 to 5 with the nodes 9 (position 1), 5 (`[2,4)`) and 6 (`[4,6)`); each gets `ad = 10`. The maxima become `mx[1..15] = 19 14 19 11 14 19 6 3 11 4 1 5 9 2 6`. `query(0, 3)` is `max(mx[4], leaf 10 + ad[5]) = max(11, 4 + 10) = 14`, and `query(5, 6)` is `9 + ad[6] = 19`.

It does **not** work for assignment, nor for a sum with range add: the sum of a node depends on how many elements are below it (the length), and an assignment erases tags below it. Whenever an update does not commute with its neighbours, you need the push.

---

## 6. What it costs

**Memory** (a million elements of 8 bytes): the recursive tree keeps `4n + 4` values and `4n + 4` tags, **64,000,064 bytes**; the bottom-up tree keeps `2 * 1,048,576` values and `1,048,576` tags, **25,165,824 bytes** (**2.5 times** less); the Fenwick range-sum tree of Topic 20 keeps three arrays of `n + 1` numbers, **24,000,024 bytes** (computed, not measured). A 16-byte tag (assign or affine) takes the bottom-up tree to **33,554,432 bytes**.

**Range add with range sum.** The three structures compute the same function and gave the same checksum.

| | Recursive lazy | Bottom-up lazy | Fenwick (Topic 20) |
|---|---|---|---|
| Mixed 50/50, ns per operation | 596.4 / 595.8 | 395.7 / 399.1 | 116.4 / 109.1 |
| Updates only, ns per update | 717.6 / 578.5 | 379.5 / 519.2 | 141.6 / 159.3 |

The bottom-up tree is **1.5 times faster** than the recursive one on the mixed workload, and the Fenwick tree is **3.4 to 3.7 times faster** than the bottom-up tree. On the updates-only row the two runs disagree on the gap between the two lazy trees (1.9 times in the first run, 1.1 times in the second), so I draw no conclusion there beyond the Fenwick tree being **2.7 to 3.3 times faster** than the bottom-up tree. **When the updates are adds and the queries are sums, use the Fenwick tree.** The lazy tree is for what the Fenwick tree cannot do.

**Range add with range maximum** (mixed 50/50):

| | Recursive lazy | Bottom-up lazy | No-push |
|---|---|---|---|
| ns per operation | 676.9 / 661.0 | 323.5 / 353.3 | 468.5 / 520.2 |

All three agree. The bottom-up lazy tree is **1.9 to 2.1 times faster** than the recursive one. The no-push tree is **1.3 to 1.4 times faster** than the recursive lazy tree (no pushes, no tag writes), but **1.4 to 1.5 times slower** than the bottom-up lazy tree: it is still a recursive tree. Not pushing helps only if you compare equals.

**The cost of a fatter tag**, on the bottom-up tree with the same operations (the functions differ, so only the timings are compared): add `372.4 / 460.1`, assign `373.6 / 348.2`, affine map `1,025.0 / 1,005.5`. The assign tag is not consistently different from the add tag (it is slower in one run and faster in the other). The affine tree is **2.2 to 2.8 times** slower than the add tree; I did not separate the cost of the two modular reductions per `apply` from the cost of the 16-byte tag.

---

## 7. Choosing

| Situation | Use |
|---|---|
| Range add, range sum | Fenwick range-sum tree (Topic 20): 3.4 to 3.7 times faster |
| Range add, point query | Fenwick difference array (Topic 20) |
| Range add, range maximum or minimum | Bottom-up lazy tree (or the no-push tree if you want 25 lines) |
| Range assign, anything | Lazy tree: assignment erases history, only a tag can express it |
| Range add and range assign together | Lazy tree with the `(keep, c)` tag |
| Range affine map, sum | Lazy tree with the affine tag |
| An update with no `apply` on a summary (for example "count the elements at least `x` after a range add") | None of these: the summary cannot be updated in O(1), so it needs another idea (Topic 23 mentions segment tree beats) |

Use the recursive lazy tree when the node must know its **range** (for example for a descent that pushes on the way down), and the bottom-up tree for speed.

---

## 8. Common mistakes

1. **Forgetting to push** in `query`, in `update` before a partial overlap, or in the point `set`. The result is right until the first assignment arrives, so tests with only adds pass.
2. **Pushing after recursing** instead of before.
3. **Composing the new tag in the wrong order** (`compose(old, g)` for `compose(g, old)`), or making the tag overwrite the old one instead of composing.
4. **Not clearing the tag after the push.** The debt is then paid again at the next visit.
5. **Using `len` wrongly or not at all.** A sum tag needs the length of the node; the two children of a node can have different lengths (an odd range splits unevenly), so each child must use its own.
6. **A "no update" sentinel that is also a real value**, such as `0` for "no assignment" when 0 is a valid value to assign, or `c == 0` for the identity of a tag that also has a scale. Use a flag.
7. **A tag whose identity test is incomplete** (`keep == 1` without `c == 0`). A pending "add 5" is then skipped at every push.
8. **A lazy tree for a commutative add on sums** where a Fenwick tree is three times faster (section 6).
9. **Making `query` `const`.** It pushes, so it changes the tree.
10. **A point set that does not push**, or that does not recompute its ancestors.
11. **Using the plain `2n` layout for a lazy tree** (section 4) or reading the padding.
12. **Overflow in tags.** Adds accumulate: a million adds of 10^9 do not fit in 32 bits, and the product of two modular numbers needs 64 bits.
13. **Testing with commutative updates only** (section 3).

**The mutants that survive**, each checked by hand:

1. `z_[v] = F::id()` dropped when a point set reaches a leaf in `LazyTree`. The tags of the leaves are never read.
2. The `l < r` guard removed in `LazyTree::apply` and in `AddMaxNoPush::add`. An empty range reaches only disjoint nodes (at worst it pushes or recomputes without changing a value).
3. The `n_ ?` guard removed in `LazyTree::query`. For `n_ = 0` the only legal call is `l = r = 0`, and the root test returns the identity.
4. The `l == r` early return removed in `IterLazyTree::apply` and `IterLazyTree::query`. The edge loops do not run, and the pushes and recomputations do nothing.
5. `(r - 1) >> i` replaced by `r >> i` in the push loop and in the recompute loop of the bottom-up tree. They sit inside the test `((r >> i) << i) != r`, under which the two indices are the same node.

---

## 9. Questions to answer out loud

1. Why does a range update on a plain segment tree cost O(n), and how does a tag fix it?
2. What do `t[v]` and `z[v]` mean in the convention of this topic?
3. What does an action consist of? State the law that `compose` must satisfy.
4. Why does `apply` get the length of the node? For which actions is it unused?
5. Why does a query push? Why is the query not `const`?
6. Why must a point set push along its path?
7. Which node receives which tag after `apply(2, 7, +10)` on eight elements?
8. Compose "assign 7" then "add 3" in the `(keep, c)` tag. What is the result in the other order?
9. Describe the lost-update bug and what it returns on the example of section 3.
10. Why does the bottom-up lazy tree need a power-of-two size, when the plain bottom-up tree did not?
11. Why is it enough to push the ancestors of the two edge leaves only?
12. When does the no-push trick work? Why does it fail for assignment?
13. For range add and range sum, which structure wins, and by how much (measured)?

---

## 10. Problems

**A. A range update.** For `3 1 4 1 5 9 2 6` and the sum action, write `t[1..15]` and the nonzero tags after `apply(2, 7, +10)`.

**B. A query.** Continue from A with `query(3, 5)`. Which tags are pushed, what are the four leaves 10 to 13 afterwards, and what does the query return?

**C. Composing a sequence.** In the `(keep, c)` tag compose, in this order: add 4, assign 9, add -2, add 6. What is the single tag?

**D. The lost update.** Four elements, all 5, the maximum. Apply assign 7 to `[0, 4)` and then add 3 to `[0, 2)`. Give `t[1..7]` and the tags after a correct run and after a run that skips the push in `update`. What does `query(0, 2)` return in each?

**E. Affine.** Compose `f = (2, 3)` and `g = (5, 1)` in both orders and apply them to `x = 4` in each order.

**F. The bottom-up tree.** On `3 1 4 1 5 9` (`size_ = 8`), which nodes receive the tag in `apply(1, 5, +10)`?

**G. Design: sum of squares.** Support range add and the query "sum of squares of a range". What does a node store, what is `apply(add c, x, len)`, and what is `compose`? Check on `1 2 3` with `add 2`.

**H. Design: flip.** Support "flip every bit in `[l, r)`" and "count the ones in `[l, r)`" on a 0/1 array. What is the node, the tag, `apply` and `compose`? Check on `1 0 1 1` with `flip [1, 3)`.

**I. Choose.** Pick a structure for each: (a) a million numbers, range add, range sum; (b) range assign, range sum; (c) range add, range minimum; (d) point update, range sum; (e) range add, "how many elements are at least `x`".

---

## Answers

**A.** `t[1..15] = 81 29 52 4 25 34 18 3 1 4 1 5 9 12 6`. The nonzero tags are `z[5] = z[6] = z[14] = 10`: the nodes `[2,4)`, `[4,6)` and `[6,7)`. Program output: the same.

**B.** The query visits nodes 5 and 6 as partial overlaps, so their tags are pushed: leaves 10 to 13 become `14 11 15 19`, and `z[5] = z[6] = 0`. The answer is `11 + 15 =` **26**. Node 14 is not visited and keeps its tag. Program output: the same.

**C.** Add 4 is `(1, 4)`; assign 9 gives `(0, 9)`; add -2 gives `(0, 7)`; add 6 gives `(0, 13)`: the single tag is **assign 13**.

**D.** Correct: `t[1..7] = 10 10 7 5 5 5 5`, tags (constants of `(keep, c)`) `0 10 7` on nodes 1 to 3 (the root's tag is the identity `(1, 0)`), `query(0, 2) = 10`, `query(2, 4) = 7`. Without the push in `update`: `t[1..7] = 8 8 5 5 5 5 5`, node 1 still owes `(0, 7)`, node 2 holds `(1, 3)`, and after the root pays its debt `query(0, 2)` returns **7**. A model of both runs written independently of the C++ gives the same numbers.

**E.** `g` after `f` is `(10, 16)`; `f` after `g` is `(10, 5)`. On `x = 4`: `f(4) = 11`, `g(11) = 56`, and `10 * 4 + 16 = 56` agrees; `g(4) = 21`, `f(21) = 45`, and `10 * 4 + 5 = 45` agrees.

**F.** Nodes **9** (position 1), **12** (position 4) and **5** (positions 2 and 3): the loop takes `l = 9` (odd), then `r = 13` (odd, so node 12), and at the next level `l = 5` (odd). Program output: `z[5] = 10` is the only tag left.

**G.** A node stores the pair `(s1, s2)`, the sum and the sum of squares of its range, and the length is known from the node. Adding `c` to every element: `s2' = s2 + 2 c s1 + len c^2` and `s1' = s1 + c len`. The tags compose by adding, as for any add. For `1 2 3`: `s1 = 6`, `s2 = 14`; after `add 2`, `s1' = 6 + 6 = 12` and `s2' = 14 + 2 * 2 * 6 + 3 * 4 = 50`, which is `3^2 + 4^2 + 5^2`.

**H.** A node stores `ones` (and knows `len`); the tag is a boolean `flip`; the identity is `false`. `apply(flip, ones, len) = len - ones` if `flip`, else `ones`. `compose(g, old) = g XOR old`: two flips cancel. The merge is the sum of the two `ones`. For `1 0 1 1` and `flip [1, 3)`: the array becomes `1 1 0 1`, and the count of ones in `[0, 4)` goes from 3 to **3**, in `[1, 3)` from 1 to 1, and in `[0, 2)` from 1 to 2.

**I.** (a) The Fenwick range-sum tree (Topic 20): 3.4 to 3.7 times faster than the bottom-up lazy tree. (b) A lazy tree with the assign tag: an assignment cannot be undone, so no difference structure can express it. (c) The bottom-up lazy tree with the add tag on the minimum (or the no-push tree). (d) A Fenwick tree (Topic 20). (e) None of the above directly: the count of elements at least `x` cannot be maintained in O(1) per node under a range add; this needs another idea (offline processing, a different decomposition, or the beats technique of Topic 23).

---

## Cheat sheet

- **Lazy tree:** `t[v]` is correct for its whole range; `z[v]` is owed to the children. Update: fully inside, `take`; partial, **push, recurse, recompute**. Query: **push** before going below.
- **Action:** `G`, `id`, `is_id`, `apply(g, x, len)`, `compose(g, old)`; the law `apply(compose(g, h)) = apply(g) after apply(h)`.
- **Tags:** add `g + old`; assign `g if set else old`; assign-or-add `(g.keep & old.keep, g.keep ? old.c + g.c : g.c)`; affine `(g.b old.b, g.b old.c + g.c)`; flip XOR.
- **Bottom-up lazy:** `size_` = next power of two; push the ancestors of the two edge leaves top down, take the covering nodes, recompute the ancestors bottom up.
- **No-push:** `mx[v] = max(children) + ad[v]`; works only when updates commute with each other and with the query.
- **Measured (1M elements):** the bottom-up lazy tree is 2.5 times smaller and about 1.5 times faster (sum) or 1.9 to 2.1 times faster (maximum) than the recursive one; the Fenwick range-sum tree is 3.4 to 3.7 times faster than the bottom-up lazy tree.

---

## Sources

- The AtCoder Library, `lazy_segtree` (https://github.com/atcoder/ac-library): the bottom-up design with `push` along the edges and the `op`, `mapping`, `composition` interface. The code here is my own implementation of the method, tested as above; the details of the library itself are from memory.
- cp-algorithms, "Segment Tree" (lazy propagation, assignment and add): the standard presentation of the recursive tree and of the assign-or-add composition.
- The no-push (permanent tag) technique and the observation that it needs commuting updates are standard in the competitive-programming literature; the remark on allocators and bitmaps is from general knowledge and was not measured.
- LeetCode, by name: My Calendar III, Falling Squares, Range Module, The Skyline Problem, Rectangle Area II.
