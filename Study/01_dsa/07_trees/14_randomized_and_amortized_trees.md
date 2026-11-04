# DSA 07.14: Randomized and Amortized Trees: Treaps, Splay Trees, Scapegoat and AA Trees

*Part of the [Trees plan](00_plan.md) · Time: about 10 hours (5 reading, 5 problems) · You need: [Rotations and AVL trees](10_rotations_and_avl_trees.md) and [Red-black trees II](12_red_black_trees_deletion_and_full_class.md) (the balanced baselines, whose measured shapes this tutorial compares with), [BST core operations](07_bst_core_operations.md), and [Skip Lists, Lock-Free Lists and RCU](../04_linked_lists/07_skip_lists_lock_free_and_rcu.md) (the other randomized ordered structure). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- explain why a **treap** has the shape of a random binary search tree whatever the insertion order, derive its expected depth, and write `split` and `merge`;
- use the **implicit treap** as a sequence with `O(log n)` insert, erase and range reversal, and say when it beats `std::vector`;
- state the **splay** rule, the potential function and the access lemma, prove the zig-zig case, and say which access patterns make a splay tree faster or slower than a balanced one;
- write a **scapegoat** tree, explain why an over-deep insert always finds a scapegoat, and read its rebuild cost;
- write an **AA tree** as a restricted red-black tree and name its two repair operations;
- choose between these, AVL, red-black trees and a skip list from measurements, not folklore.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 13,911,028 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: every insertion order of 1 to 7 keys into a treap with fixed priorities (5,913 builds, each compared with the Cartesian tree of the keys and priorities, then every victim erased); an implicit treap against `std::vector` over 200 random sequences of insert, erase, reverse-range and access; the splay tree against `std::set` over 150 sequences, **plus the access lemma checked exactly on 30,000 accesses** (rotations plus the change of potential against `3 (log2 n - log2 s(x)) + 1`); the scapegoat tree against `std::set` with the height bound checked after every operation and for 20,000 ascending, descending and random inserts and erases; and the AA tree for every insertion order of 1 to 8 keys with every victim and two absent keys, every insertion order followed by every erase order up to 6 keys (533,417 runs), and 150 random sequences, all with a checker for each rule. A separately written Python splay tree agreed with the C++ on 300 random sequences (preorder and rotation count). Every checker also had to fail on damaged trees, one rule at a time. Finally the tests were attacked with 104 hand-made mutants of the code (99 were caught; the 5 that were not are listed at the end of section 6). Counts, heights and traces are program output. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, glibc malloc, best of 3; read them as shapes, not benchmarks. Claims from other sources are marked "from memory" or name the source.

---

## 1. Three ways to avoid storing balance rules

AVL and red-black trees keep a **rule** in every node and repair it on every update (Topics 10 to 12). This tutorial shows four other bargains:

| Structure | Stores per node | Guarantee | What pays for it |
|---|---|---|---|
| **Treap** | a random priority (and a size, for sequences) | expected `O(log n)` for every operation, any input order | randomness |
| **Splay tree** | nothing | amortized `O(log n)` per operation; one operation can cost `O(n)` | every access reshapes the tree |
| **Scapegoat tree** | a size (or nothing, with parent pointers) | worst-case `O(log n)` search; amortized `O(log n)` update | occasional rebuilds of a whole subtree |
| **AA tree** | a level | worst-case `O(log n)` | a red-black tree with half the cases |

"Expected" is over the coin flips of the structure; no input can make it bad (an adversary who does not see the priorities). "Amortized" is a bound on the *average* over any sequence of operations: individual operations can be slow.

---

## 2. Treaps

A **treap** is a binary search tree on the key and a max-heap on a random priority drawn when the key is inserted. Given the keys and priorities, **exactly one** tree satisfies both orders: the root must be the key of highest priority, and then the same rule applies to the keys on each side (a *Cartesian tree*). So the shape depends only on the set of (key, priority) pairs, and never on the order of insertions and erasures that produced it. The tests confirm it: all 5,040 insertion orders of the keys 1 to 7 gave the same preorder, and after random mixes of inserts and erases the tree was always equal to the Cartesian tree of the surviving pairs.

Since the priorities are random, the treap has the shape of a **random binary search tree** (the shape you get by inserting the keys in a random order), however the real inserts were ordered. Sorted input is not a bad case.

### 2.1 Expected depth

Take the keys in sorted order, ranked 1 to n. Rank `j` is an ancestor of rank `i` exactly when `j` has the highest priority among the ranks between `i` and `j`, which happens with probability `1 / (|i - j| + 1)`. So the expected number of ancestors of rank `i` is

```
sum over j != i of 1/(|i-j|+1)  =  (H_i - 1) + (H_{n-i+1} - 1)
```

where `H_m` is the m-th harmonic number. Averaged over `i` (root at depth 0), the mean depth is `2 (1 + 1/n) H_n - 4`, about `2 ln n`, which is `1.39 log2 n`. For 1,000,000 keys that is **24.79**, against 19.93 for a perfectly balanced tree. Measured on 1,000,000 keys with seeded priorities:

| | height | mean depth |
|---|---|---|
| treap, random-order keys | 47 | 24.52 |
| treap, ascending keys | 49 | 24.39 |
| formula (random BST) | | 24.79 |
| for comparison: AVL (Topic 10) | 24 (random) / 20 (ascending) | 18.41 / 17.95 |
| for comparison: red-black (Topic 11) | 25 / 37 | 18.43 / 18.33 |

The mean depth is about 6 levels more than a balanced tree's, and the height is about twice the AVL height. What you buy is that nothing depends on the input order: the ascending row is as good as the random one, where the red-black tree reaches 37.

### 2.2 Split and merge

```cpp
// Binary search tree on key, max-heap on a random priority; split and merge do all the work.
struct TNode {
    int key; unsigned pri; int size = 1; bool rev = false;
    TNode *l = nullptr, *r = nullptr;
};
inline int tsz(const TNode* t) { return t ? t->size : 0; }
inline void tpull(TNode* t) { t->size = 1 + tsz(t->l) + tsz(t->r); }
inline void tpush(TNode* t) {                       // lazy "reverse this subtree"; key-ordered trees never set it
    if (t->rev) { std::swap(t->l, t->r); if (t->l) t->l->rev ^= 1; if (t->r) t->r->rev ^= 1; t->rev = false; }
}
// Keys < k go to l, keys >= k to r.
inline void tsplit(TNode* t, int k, TNode*& l, TNode*& r) {
    if (!t) { l = r = nullptr; return; }
    tpush(t);
    if (t->key < k) { tsplit(t->r, k, t->r, r); l = t; }
    else { tsplit(t->l, k, l, t->l); r = t; }
    tpull(t);
}
// Every key of a is below every key of b (or: a's elements come first in the sequence).
inline TNode* tmerge(TNode* a, TNode* b) {
    if (!a) return b;
    if (!b) return a;
    if (a->pri > b->pri) { tpush(a); a->r = tmerge(a->r, b); tpull(a); return a; }
    tpush(b); b->l = tmerge(a, b->l); tpull(b); return b;
}

class Treap {
    TNode* root_ = nullptr;
    uint64_t s_;
    static void destroy(TNode* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }
    unsigned nextPri() {                            // splitmix64: deterministic, so runs are repeatable
        uint64_t z = (s_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return (unsigned)((z ^ (z >> 31)) >> 32);
    }
    static TNode* erase(TNode* t, int k, bool& found) {
        if (!t) return t;
        if (k < t->key) t->l = erase(t->l, k, found);
        else if (k > t->key) t->r = erase(t->r, k, found);
        else { found = true; TNode* m = tmerge(t->l, t->r); delete t; return m; }
        tpull(t); return t;
    }
public:
    mutable long probes = 0;                        // nodes visited by contains(), for the measurements
    explicit Treap(uint64_t seed = 1) : s_(seed) {}
    Treap(const Treap&) = delete; Treap& operator=(const Treap&) = delete;
    ~Treap() { destroy(root_); }
    std::size_t size() const { return tsz(root_); }
    const TNode* root() const { return root_; }
    bool contains(int k) const {
        for (const TNode* t = root_; t;) { ++probes; if (k < t->key) t = t->l; else if (k > t->key) t = t->r; else return true; }
        return false;
    }
    bool insert(int k, unsigned pri) {
        for (const TNode* t = root_; t;) { if (k == t->key) return false; t = k < t->key ? t->l : t->r; }
        TNode *a, *b; tsplit(root_, k, a, b);
        root_ = tmerge(tmerge(a, new TNode{k, pri}), b);
        return true;
    }
    bool insert(int k) { return insert(k, nextPri()); }
    bool erase(int k) { bool f = false; root_ = erase(root_, k, f); return f; }
};
```

- `tsplit(t, k, l, r)` cuts the tree into the keys below `k` and the rest, along one root-to-leaf path: `O(depth)`.
- `tmerge(a, b)` joins two trees whose keys are already separated: the root is the one with the larger priority, and the other tree is merged into the appropriate child. `O(depth)` again.
- **Insert** = split at `k`, merge the left part, the new node, and the right part. **Erase** = replace the node by `tmerge(left, right)`. No rotation appears anywhere: the heap order is restored by the way `merge` picks roots.

A trace on the keys 1 to 7 with the priorities 5300, 2100, 7700, 900, 6400, 3800, 8800 (inserted in the order 4, 2, 6, 1, 3, 5, 7): the preorder is `7 3 1 2 5 4 6`, because the largest priority (8800) belongs to the key 7, and the best of keys 1 to 6 is 7700 for key 3, and so on. Splitting at 4 gives `[3 1 2]` and `[7 5 4 6]`, and merging them gives back `7 3 1 2 5 4 6`.

Two practical points. First, with 32-bit random priorities and a million keys, ties happen (about 100 pairs are expected, by the birthday bound: from memory); they do not break the structure, because the checker only forbids a child with a *strictly larger* priority, but the shape is then not unique. Use 64-bit priorities if you need the shape to be a function of the set. Second, the code above costs a search, a split and two merges per insert; the usual faster insert descends until the new priority beats a node and splits only below that point. I did not write it, and the build times below are for the simple version.

### 2.3 The implicit treap: a sequence

Throw the key away and let the **position** be the order. A node's position is the size of its left subtree plus the positions above it, so `split` and `merge` work by index and the tree is a sequence with `O(log n)` expected insert, erase and access anywhere. Add a lazy flag `rev` and a whole range can be **reversed** by splitting out the range, flipping the flag and merging back.

```cpp
// The same node as a sequence: no key, the position is the order. Split and merge by index.
inline void tsplit_at(TNode* t, int k, TNode*& l, TNode*& r) {     // the first k elements go to l
    if (!t) { l = r = nullptr; return; }
    tpush(t);
    if (tsz(t->l) < k) { tsplit_at(t->r, k - tsz(t->l) - 1, t->r, r); l = t; }
    else { tsplit_at(t->l, k, l, t->l); r = t; }
    tpull(t);
}
inline void tinsert_at(TNode*& root, int i, TNode* n) { TNode *a, *b; tsplit_at(root, i, a, b); root = tmerge(tmerge(a, n), b); }
inline TNode* terase_at(TNode*& root, int i) {
    TNode *a, *b, *m, *c; tsplit_at(root, i, a, b); tsplit_at(b, 1, m, c); root = tmerge(a, c); return m;
}
inline void treverse(TNode*& root, int i, int j) {                  // reverse the elements [i, j)
    TNode *a, *b, *m, *c; tsplit_at(root, i, a, b); tsplit_at(b, j - i, m, c);
    if (m) m->rev ^= 1;
    root = tmerge(tmerge(a, m), c);
}
inline TNode* tat(TNode* t, int i) {                                // the i-th element
    for (;;) {
        tpush(t); int ls = tsz(t->l);
        if (i < ls) t = t->l; else if (i == ls) return t; else { i -= ls + 1; t = t->r; }
    }
}
inline void tlist(TNode* t, std::vector<int>& out) { if (t) { tpush(t); tlist(t->l, out); out.push_back(t->key); tlist(t->r, out); } }
```

`tpush` is the lazy step: it swaps a node's children, passes the flag down to them, and clears it; every function that looks inside a node calls it first. A test sequence: start with 0 to 9, reverse the whole range to get `9 8 7 6 5 4 3 2 1 0`, then reverse positions 2 to 4 (the range `[2, 5)`) to get `9 8 5 6 7 4 3 2 1 0`; empty ranges change nothing. The 200 random sequences also compared every state with a `std::vector`.

The cost against the obvious alternative: 200,000 mixed operations (insert, erase, reverse a random range) on a sequence of about 100,000 elements took **1961 ns per operation** with the treap and **6535 ns** with `std::vector` (second run: 2040 and 6229 ns), where the vector pays for moving elements. The ratio is about 3 here and grows with the sequence length, because the vector's cost is linear and the treap's is logarithmic. For short sequences the vector wins.

---

## 3. Splay trees

A splay tree stores **nothing but keys and links**. After every access (find, insert, erase) the node that was reached is rotated to the root by a fixed rule, and the amortized analysis does the rest.

```cpp
// Bottom-up splay tree. Every operation ends by splaying a node to the root; the tree keeps
// no balance information at all. `rotations` is the cost the amortized analysis counts.
struct SNode { int key; SNode *l = nullptr, *r = nullptr, *p = nullptr; };
class Splay {
    SNode* root_ = nullptr; std::size_t n_ = 0;
    void rot(SNode* x) {                            // lift x over its parent
        SNode *p = x->p, *g = p->p;
        if (x == p->l) { p->l = x->r; if (x->r) x->r->p = p; x->r = p; }
        else { p->r = x->l; if (x->l) x->l->p = p; x->l = p; }
        p->p = x; x->p = g;
        if (g) { if (g->l == p) g->l = x; else g->r = x; } else root_ = x;
        ++rotations;
    }
    void splay(SNode* x) {
        while (x->p) {
            SNode *p = x->p, *g = p->p;
            if (!g) rot(x);                                         // zig
            else if ((g->l == p) == (p->l == x)) { rot(p); rot(x); }// zig-zig: the parent first
            else { rot(x); rot(x); }                                // zig-zag
        }
    }
public:
    long rotations = 0, probes = 0;
    Splay() = default;
    Splay(const Splay&) = delete; Splay& operator=(const Splay&) = delete;
    ~Splay() {
        std::vector<SNode*> st; if (root_) st.push_back(root_);
        while (!st.empty()) { SNode* t = st.back(); st.pop_back(); if (t->l) st.push_back(t->l); if (t->r) st.push_back(t->r); delete t; }
    }
    std::size_t size() const { return n_; }
    const SNode* root() const { return root_; }
    bool find(int k) {                              // also splays: the last node visited if k is absent
        SNode *t = root_, *last = nullptr;
        while (t) { ++probes; last = t; if (k < t->key) t = t->l; else if (k > t->key) t = t->r; else break; }
        if (!last) return false;
        splay(last);
        return last->key == k;
    }
    bool insert(int k) {
        SNode *t = root_, *p = nullptr;
        while (t) { p = t; if (k == t->key) { splay(t); return false; } t = k < t->key ? t->l : t->r; }
        SNode* n = new SNode{k}; n->p = p;
        if (!p) root_ = n; else if (k < p->key) p->l = n; else p->r = n;
        ++n_; splay(n); return true;
    }
    bool erase(int k) {
        if (!find(k)) return false;
        SNode *x = root_, *L = x->l, *R = x->r;
        delete x; --n_;
        if (L) L->p = nullptr;
        if (R) R->p = nullptr;
        if (!L) { root_ = R; return true; }
        root_ = L;
        SNode* m = L; while (m->r) m = m->r;        // the largest key of the left part
        splay(m);                                   // now it is the root and has no right child
        m->r = R; if (R) R->p = m;
        return true;
    }
};
```

The three cases, with `x` the node being lifted, `p` its parent, `g` its grandparent:

| Case | Shape | Moves |
|---|---|---|
| zig | `p` is the root | one rotation of `x` over `p` |
| zig-zig | `x` and `p` are both left (or both right) children | rotate **`p` over `g` first**, then `x` over `p` |
| zig-zag | `x` and `p` are on opposite sides | rotate `x` over `p`, then `x` over `g` |

The order in zig-zig is the whole point: rotating `x` twice instead would also bring `x` to the top but would not shorten the path it came along; a path that is traversed gets about **half** as long.

A trace, inserting 1 to 5 in ascending order (every insert splays the new key to the root, so the tree is a left path with preorder `5 4 3 2 1`, after 4 rotations in all). Then `find(1)` moves the deepest node up in 4 rotations and leaves `1 4 2 3 5`; `find(3)` in that tree costs 3 rotations and leaves `3 1 2 4 5`. Each find costs exactly its depth in rotations, since every rotation lifts the node by one level.

### 3.1 The access lemma

Give each node `v` the size `s(v)` of its subtree and the rank `r(v) = log2 s(v)`, and let the potential of the tree be `Φ = Σ r(v)`. A balanced tree has a small potential (most subtrees are small), and a path has a large one (`log2 n!`, about `n log2 n`). Count one unit of cost per rotation.

**Lemma (Sleator and Tarjan, 1985; from memory).** Splaying `x` in a tree with root `t` has amortized cost `rotations + ΔΦ ≤ 3 (r(t) - r(x)) + 1`.

The proof is a telescoping sum over the steps. For a zig-zig step, with `y` the parent and `z` the grandparent of `x`, and primes for the values after the step: the step costs 2, and `s'(x) = s(z)`, so

```
amortized = 2 + r'(y) + r'(z) - r(x) - r(y)                (the terms r'(x) = r(z) cancel)
          ≤ 2 + r'(x) + r'(z) - 2 r(x)                    (r'(y) ≤ r'(x) and r(y) ≥ r(x))
```

Now `s(x) + s'(z) ≤ s'(x)`, and since `log` is concave, `r(x) + r'(z) ≤ 2 r'(x) - 2`. Substituting `r'(z) ≤ 2 r'(x) - 2 - r(x)` gives `amortized ≤ 3 (r'(x) - r(x))`: the extra 2 is paid by the concavity. The zig-zag step gives the same bound, and the single final zig costs one more, which is the `+1`. The sum over all steps telescopes to `3 (r(t) - r(x)) + 1`. (Zig-zag and zig I state from memory.)

Consequences: one access costs at most `3 log2 n + 1` amortized, because `r(t) = log2 n` and `r(x) ≥ 0`; and over `m` accesses starting from any tree of `n` nodes the total is `O((m + n) log n)`. The same lemma, with different weights, gives the **static optimality** and **working set** theorems (from memory): the splay tree is within a constant of the best fixed tree for a given access distribution, and an access costs `O(log k)` if only `k` distinct keys were touched since the last access to this one.

The tests check the lemma itself, not just the consequence: on 30,000 accesses (100 starting trees, half of them paths, with keys that are present and absent) the quantity `rotations + Φ_after - Φ_before` never exceeded `3 (log2 n - log2 s(x)) + 1`, where `x` is the node actually splayed. A worked case: a path of 1,023 nodes, then `find(1)`: the potential falls from 8759.01 to 4387.16, the 1,022 rotations are paid by it, and the amortized cost is **-3349.84**, far below the bound 31.00. One slow access buys a tree about half as deep: the height goes from 1,023 to 513 and the mean depth from 511.00 to 256.25. Doing it again for every key in order cost 3,450 rotations for the 1,023 finds, 3.4 per find, within the sequential-access bound of `4.5 n` that Tarjan proved (from memory).

### 3.2 When a splay tree wins and when it loses

Measured on 1,000,000 keys in random insertion order, 2,000,000 finds per row:

| Access pattern | rotations per find | nodes visited per find |
|---|---|---|
| uniform random | 25.88 | 26.88 |
| sequential scan | 3.49 | 4.49 |
| working set of 16 keys | 3.27 | 4.27 |
| working set of 1,024 keys | 11.47 | 12.47 |
| skewed (key = N u^4, u uniform) | 22.67 | 23.67 |

So uniform access costs the full depth, while a repeated small working set costs close to `log2 k` (16 keys: 3.27; 1,024 keys: 11.47 against `log2 1024 = 10`) however large the tree is. The times show it: random finds in the same 1,000,000-key tree took **3060.1 ns** for the splay tree against 1760.5 ns for Topic 12's red-black tree and 1667.2 ns for `std::set`, and the working set of 1,024 keys took **171.0 ns** against 383.9 and 363.5 ns (second run: 3210.4 / 1743.5 / 1688.6 ns, and 166.1 / 341.1 / 346.2 ns). The splay tree is about 1.7 to 1.8 times slower on random access and about 2 times faster on a hot set. The hot-set figure for the balanced trees is itself helped by the CPU cache; the splay tree has the same help plus a path of length 3.

Costs that the amortized bound hides:

- **A single operation can cost `O(n)`**. The first `find(1)` on the sorted-insert path cost 1,022 rotations. A real-time system cannot use it.
- **Reads write.** `find` rotates, so two threads cannot read the same tree, and a "const" lookup is a lie. A splay tree cannot be shared among readers without a lock.
- **Ascending insertion makes a path** (1,000,000 nodes tall in the measurement, with only 1.00 rotation per insert), which is cheap to build and expensive to query once. Recursive code (a destructor, a height function) overflows the stack on it; the code above is iterative where it matters.
- **Memory**: 32 bytes of node (key, three pointers, no padding wasted) and 48 bytes of heap per element on this allocator, the same as `std::set<int>`.
- It is a good fit for caches and for data with locality (a splay tree is used, from memory, in some memory allocators and network routing tables) and a poor fit for uniform access or concurrent reads.

---

## 4. Scapegoat trees

The scapegoat tree keeps **no colour and no priority**. It lets the tree go unbalanced, and when an insert lands too deep it finds an ancestor that is too lopsided and **rebuilds that subtree into a perfectly balanced one**.

```cpp
// Weight-balanced by rebuilding: no colour, no priority, only the subtree sizes.
// An insert that lands deeper than log_{1/A}(n) finds the "scapegoat" ancestor and rebuilds it perfectly.
class Scapegoat {
public:
    struct N { int key; int size = 1; N *l = nullptr, *r = nullptr; };
    static constexpr double A = 0.7;
    long rebuilt = 0; mutable long probes = 0;                 // nodes touched by rebuilds; nodes visited by contains()
private:
    N* root_ = nullptr; std::size_t n_ = 0, max_ = 0;
    static int sz(const N* t) { return t ? t->size : 0; }
    static void destroy(N* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }
    static int limit(std::size_t n) { return (int)std::floor(std::log((double)n) / std::log(1.0 / A)); }
    static void flatten(N* t, std::vector<N*>& v) { if (t) { flatten(t->l, v); v.push_back(t); flatten(t->r, v); } }
    static N* build(std::vector<N*>& v, int lo, int hi) {           // v[lo, hi)
        if (lo >= hi) return nullptr;
        int mid = (lo + hi) / 2; N* t = v[mid];
        t->l = build(v, lo, mid); t->r = build(v, mid + 1, hi);
        t->size = hi - lo; return t;
    }
    N* rebuild(N* t) { std::vector<N*> v; flatten(t, v); rebuilt += (long)v.size(); return build(v, 0, (int)v.size()); }
    N* erase(N* t, int k, bool& f) {
        if (!t) return t;
        if (k < t->key) t->l = erase(t->l, k, f);
        else if (k > t->key) t->r = erase(t->r, k, f);
        else {
            f = true;
            if (!t->l || !t->r) { N* c = t->l ? t->l : t->r; delete t; return c; }
            N* s = t->r; while (s->l) s = s->l;
            int sk = s->key; bool d = false; t->r = erase(t->r, sk, d); t->key = sk;
        }
        t->size = 1 + sz(t->l) + sz(t->r); return t;
    }
public:
    Scapegoat() = default;
    Scapegoat(const Scapegoat&) = delete; Scapegoat& operator=(const Scapegoat&) = delete;
    ~Scapegoat() { destroy(root_); }
    std::size_t size() const { return n_; }
    std::size_t peak() const { return max_; }
    const N* root() const { return root_; }
    bool contains(int k) const {
        for (const N* t = root_; t;) { ++probes; if (k < t->key) t = t->l; else if (k > t->key) t = t->r; else return true; }
        return false;
    }
    bool insert(int k) {
        for (const N* t = root_; t;) { if (k == t->key) return false; t = k < t->key ? t->l : t->r; }
        std::vector<N*> path;
        for (N* t = root_; t; t = k < t->key ? t->l : t->r) { path.push_back(t); ++t->size; }
        N* n = new N{k};
        if (path.empty()) root_ = n; else (k < path.back()->key ? path.back()->l : path.back()->r) = n;
        ++n_; if (n_ > max_) max_ = n_;
        if ((int)path.size() > limit(n_)) {                        // too deep: climb to the scapegoat
            N* child = n;
            for (std::size_t i = path.size(); i-- > 0;) {
                N* par = path[i];
                if (child->size > A * par->size) {
                    N* sub = rebuild(par);
                    if (i == 0) root_ = sub; else (path[i - 1]->l == par ? path[i - 1]->l : path[i - 1]->r) = sub;
                    break;
                }
                child = par;
            }
        }
        return true;
    }
    bool erase(int k) {
        bool f = false; root_ = erase(root_, k, f);
        if (!f) return false;
        --n_;
        if (n_ < A * max_) { root_ = rebuild(root_); max_ = n_; }   // too many deletions: rebuild everything
        return true;
    }
    int heightLimit() const { return max_ ? limit(max_) + 1 : 0; }
};
```

- `A = 0.7` is the weight-balance parameter: a node is *balanced* if neither child holds more than `A` of its size. The depth limit for `n` nodes is `floor(log_{1/A} n)`: 38 for a million keys and 5 for 8.
- **Why a scapegoat always exists.** Suppose every node on the path to the new node, from the root, were balanced. Each step down would shrink the subtree size by at least the factor `A`, so after `d` steps the size would be at most `A^d n`, which must be at least 1: `d ≤ log_{1/A} n`. A node deeper than the limit therefore has an unbalanced ancestor: climb from the new node and stop at the first parent whose child is heavier than `A` times it. The code finds it by comparing sizes, which the descent has already incremented.
- **Erase** is the plain binary search tree erase. The tree counts `max_`, the peak size since the last full rebuild, and when `n` falls below `A max_` it rebuilds everything. That keeps the height under `limit(max_) + 1` (the tests check the bound after every operation).
- **Amortized cost.** A rebuild of size `m` can only be triggered after a lot of unbalanced inserts below it (the argument is from memory: about `Ω(m)` of them since it was last balanced), so the cost per insert is `O(log n)` amortized. Measured: on **random-order** keys the 1,000,000 inserts rebuilt only 5,546 nodes in all (0.01 per insert), since a random binary search tree is rarely deeper than 38. On **ascending** keys they rebuilt 23,033,992 nodes (23.03 per insert), about log2 n of them, which is the amortized `O(log n)` showing up.

A trace: ascending inserts of 1, 2, 3, ...; the first rebuild happens at the 7th insert (depth limit for 7 nodes: 5, the new node is at depth 6), and it rebuilds 4 nodes.

Shapes on 1,000,000 keys: height **39** and mean depth 24.18 for random keys, height 39 and mean depth 27.45 for ascending ones. The height is exactly the limit plus one: the tree uses all the slack it is given, so with `A = 0.7` it is **taller than a red-black tree** (25 random) and about as flat as a treap on average. Smaller `A` means a flatter tree and more rebuilding. The node is 24 bytes (key, size, two pointers), which the allocator rounds to a 32-byte chunk: **32 bytes per element against 48 for `std::set`**.

---

## 5. AA trees

An AA tree is a red-black tree with one extra rule that removes half the cases: **only right children may be red**. Instead of a colour, each node stores a **level** (its black height): a left child is exactly one level lower, a right child is on the same level (a "red" right child) or one lower, and there are never two right links in a row on one level.

```cpp
// AA tree: a red-black tree in which only right children may be red, stored as levels.
// A node's level is its black height; a right child on the same level is the "red" one.
struct ANode { int key; int lvl = 1; ANode *l = nullptr, *r = nullptr; };
inline int alvl(const ANode* t) { return t ? t->lvl : 0; }
inline ANode* aa_skew(ANode* t) {                   // a left child on the same level: rotate it up
    if (t && t->l && t->l->lvl == t->lvl) { ANode* x = t->l; t->l = x->r; x->r = t; return x; }
    return t;
}
inline ANode* aa_split(ANode* t) {                  // two right links in a row on one level: rotate, level up
    if (t && t->r && t->r->r && t->r->r->lvl == t->lvl) { ANode* x = t->r; t->r = x->l; x->l = t; ++x->lvl; return x; }
    return t;
}
inline ANode* aa_insert(ANode* t, int k, bool& added) {
    if (!t) { added = true; return new ANode{k}; }
    if (k < t->key) t->l = aa_insert(t->l, k, added);
    else if (k > t->key) t->r = aa_insert(t->r, k, added);
    else return t;
    return aa_split(aa_skew(t));
}
inline ANode* aa_erase(ANode* t, int k, bool& removed) {
    if (!t) return t;
    if (k < t->key) t->l = aa_erase(t->l, k, removed);
    else if (k > t->key) t->r = aa_erase(t->r, k, removed);
    else {
        removed = true;
        if (!t->l && !t->r) { delete t; return nullptr; }
        bool d = false;
        if (!t->l) { ANode* s = t->r; while (s->l) s = s->l; int sk = s->key; t->r = aa_erase(t->r, sk, d); t->key = sk; }
        else { ANode* p = t->l; while (p->r) p = p->r; int pk = p->key; t->l = aa_erase(t->l, pk, d); t->key = pk; }
    }
    int should = std::min(alvl(t->l), alvl(t->r)) + 1;              // a level may have lost its support below
    if (should < t->lvl) { t->lvl = should; if (t->r && should < t->r->lvl) t->r->lvl = should; }
    t = aa_skew(t); t->r = aa_skew(t->r); if (t->r) t->r->r = aa_skew(t->r->r);
    t = aa_split(t); t->r = aa_split(t->r);
    return t;
}
class AATree {
    ANode* root_ = nullptr; std::size_t n_ = 0;
    static void destroy(ANode* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }
public:
    mutable long probes = 0;
    AATree() = default;
    AATree(const AATree&) = delete; AATree& operator=(const AATree&) = delete;
    ~AATree() { destroy(root_); }
    std::size_t size() const { return n_; }
    const ANode* root() const { return root_; }
    ANode*& rootRef() { return root_; }
    bool contains(int k) const {
        for (const ANode* t = root_; t;) { ++probes; if (k < t->key) t = t->l; else if (k > t->key) t = t->r; else return true; }
        return false;
    }
    bool insert(int k) { bool a = false; root_ = aa_insert(root_, k, a); n_ += a; return a; }
    bool erase(int k) { bool r = false; root_ = aa_erase(root_, k, r); n_ -= r; return r; }
};
```

Two operations restore the rules, and they are the whole algorithm:

- **skew** removes a left child on the same level (a red left child) with a right rotation;
- **split** removes two right links in a row on one level (two red nodes in a row) with a left rotation, and raises the middle node one level.

Insert is "insert, then skew, then split" on the way back up. Erase replaces a two-child node by its successor's (or predecessor's) key, then repairs on the way up: lower a level that has lost support, then skew and split a fixed number of times. The 2-3 tree reading of Topic 11 applies: a level is a node of the 2-3 tree and a right child on the same level is the second key of a 3-node. Erase copies keys, so unlike Topic 12 it does not keep node addresses stable.

A trace: inserting 1 to 7 gives the preorder (key/level) `4/3 2/2 1/1 3/1 6/2 5/1 7/1`, a perfect tree; erasing 4 then gives `3/2 1/1 2/1 6/2 5/1 7/1`, in which the node 1 has a right child 2 on its own level: a 3-node.

Shapes on 1,000,000 keys: height **27** (random) and **26** (ascending), mean depth 18.61 and 18.27: as flat as the red-black tree on average (18.43, 18.33) and **much flatter on sorted input** (26 against the classic red-black tree's 37), because the extra rule forbids the skinny shapes. The tests check height `≤ 2 log2(n + 1)` after every sampled operation. The node is 24 bytes (32 per element), and AA trees were the **fastest lookup** of the new structures: 1440.7 ns against 1760.5 ns for Topic 12's red-black tree (1522.4 and 1743.5 in the second run). I did not isolate why; a flatter tree and smaller nodes are the obvious suspects.

---

## 6. The checkers

```cpp
// Each checker returns the node count of a valid tree, or -1. Order is checked with bounds.
inline long treap_check(const TNode* t, long lo = -(1L << 40), long hi = (1L << 40)) {
    if (!t) return 0;
    if (t->key <= lo || t->key >= hi) return -1;
    if ((t->l && t->l->pri > t->pri) || (t->r && t->r->pri > t->pri)) return -1;   // heap order
    long a = treap_check(t->l, lo, t->key), b = treap_check(t->r, t->key, hi);
    if (a < 0 || b < 0 || t->size != 1 + a + b) return -1;
    return 1 + a + b;
}
inline long splay_check(const SNode* t, const SNode* parent, long lo = -(1L << 40), long hi = (1L << 40)) {
    if (!t) return 0;
    if (t->p != parent || t->key <= lo || t->key >= hi) return -1;
    long a = splay_check(t->l, t, lo, t->key), b = splay_check(t->r, t, t->key, hi);
    return (a < 0 || b < 0) ? -1 : 1 + a + b;
}
inline long sg_check(const Scapegoat::N* t, long lo = -(1L << 40), long hi = (1L << 40)) {
    if (!t) return 0;
    if (t->key <= lo || t->key >= hi) return -1;
    long a = sg_check(t->l, lo, t->key), b = sg_check(t->r, t->key, hi);
    if (a < 0 || b < 0 || t->size != 1 + a + b) return -1;
    return 1 + a + b;
}
inline long aa_check(const ANode* t, long lo = -(1L << 40), long hi = (1L << 40)) {
    if (!t) return 0;
    if (t->key <= lo || t->key >= hi) return -1;
    if (!t->l && !t->r && t->lvl != 1) return -1;                                 // a leaf is on level 1
    if (t->l && t->l->lvl != t->lvl - 1) return -1;                               // a left child is one lower
    if (t->r && t->r->lvl != t->lvl && t->r->lvl != t->lvl - 1) return -1;       // a right child: same or one lower
    if (t->r && t->r->r && t->r->r->lvl >= t->lvl) return -1;                     // no two same-level rights in a row
    if (t->lvl > 1 && (!t->l || !t->r)) return -1;                                // above level 1 both children exist
    long a = aa_check(t->l, lo, t->key), b = aa_check(t->r, t->key, hi);
    return (a < 0 || b < 0) ? -1 : 1 + a + b;
}
```

All four checkers verify the order of keys with bounds (strictly increasing), then the structure's own invariants: the heap order and the size for the treap, the parent links for the splay tree, the size for the scapegoat tree, and the five level rules for the AA tree. `shape_of` (listed after the checkers) measures height and mean depth iteratively.

```cpp
// Height (in nodes) and mean depth (root = 0) of any binary tree with l and r members;
// iterative, because a splay tree can be a path of a million nodes.
struct Shape { int height = 0; double meanDepth = 0; std::size_t n = 0; };
template <class Node> Shape shape_of(const Node* root) {
    Shape s; if (!root) return s;
    std::vector<std::pair<const Node*, int>> st{{root, 0}};
    double sum = 0;
    while (!st.empty()) {
        auto [t, d] = st.back(); st.pop_back();
        ++s.n; sum += d; if (d + 1 > s.height) s.height = d + 1;
        if (t->l) st.push_back({t->l, d + 1});
        if (t->r) st.push_back({t->r, d + 1});
    }
    s.meanDepth = sum / s.n;
    return s;
}
```

Every rule has a test that breaks it alone and requires the checker to fail: a child with a higher priority, a wrong size, a key out of order on either side, a wrong parent link, and for the AA tree eleven separate hand-built trees (left child on the same level or two levels down, right child two levels down or above its parent, two right links in a row, a level-2 node with one child, a leaf below level 1, keys out of order on either side, a duplicate key on either side) plus the valid ones that must pass. `shape_of` is checked on hand-built trees (a right path, a left path, a mixed tree, the empty tree).

**What the mutants did not catch.** Five of the 104 changes survive, and I looked at each:

- Dropping `else g->r = x` in the splay rotation (the grandparent's right-child update) changes nothing observable: in a bottom-up splay the node always rotates over that grandparent next, and that rotation overwrites the stale pointer. The line is dead code in this algorithm; I kept it because `rot` is meant to be correct on its own.
- `m->rev = true` instead of `m->rev ^= 1` in `treverse`: the root of the extracted range has always been pushed by the split just before, so its flag is clear and both forms agree.
- `>=` instead of `==` in `aa_skew`: a left child is never on a higher level than its parent, so the two tests agree.
- `lvl < 1` instead of `lvl != 1` for a leaf in `aa_check`: a leaf above level 1 is rejected by the next rule anyway (above level 1 both children must exist), so that rule is redundant.
- Replacing the root when the scapegoat is the root itself (`i == 0` in `Scapegoat::insert`): **a test gap.** It needs a tree whose every subtree is close to the 0.7 limit while the root's child is heavy, and neither the sorted, zig-zag and random inserts up to 3,000 keys nor 200,000 random insert/erase sequences ever reached it. Read the scapegoat insert as tested except for that one branch.

---

## 7. All of them together

Measured on 1,000,000 distinct keys, `int` keys, no values (the `shape` rows of the sections above, then lookups and builds):

| | height (random / ascending) | node bytes | heap bytes per element | random find, mean | build, random / ascending (ns per insert) |
|---|---|---|---|---|---|
| treap | 47 / 49 | 32 | 48.0 | 1970.0 ns | 2387.0 / 115.9 |
| splay | 52 / 1,000,000 | 32 | 48.0 | 3060.1 ns | 1460.8 / 352.8 |
| scapegoat | 39 / 39 | 24 | 32.0 | 1735.9 ns | 1194.7 / 566.2 |
| AA | 27 / 26 | 24 | 32.0 | 1440.7 ns | 1293.0 / 95.6 |
| Topic 12 `RBTree` | 25 / 37 (Topic 11) | | | 1760.5 ns | |
| `std::set<int>` | | | 48.0 | 1667.2 ns | |

A second run of the timings gave the same order for lookups (treap 2058.4, AA 1522.4, scapegoat 1811.6, splay 3210.4, `RBTree` 1743.5, `std::set` 1688.6 ns) and the same pattern for builds: random 2271.7, 1279.0, 1393.7, 1542.3 ns (treap, AA, scapegoat, splay) and ascending 135.2, 108.7, 625.6, 363.6 ns. Read them for three things only. The treap is the slowest to build from random keys, because my insert is the plain split-and-merge version. The scapegoat tree builds ascending keys the slowest of the balanced ones because of its rebuilds (23 node visits per insert). And the lookup differences between the new trees mostly follow the mean depth and the node size.

**Against the skip list.** Topic 04.07 measured the skip list, the other randomized ordered structure: at a million keys `std::map` was about 1.5 times faster than the inline-link skip list, and the skip list needed 37.1 comparisons per find against 21.4. A treap or a skip list gives expected `O(log n)` from random choices, and both are slower than a balanced tree on one thread. The skip list wins elsewhere: updates touch only the predecessors, so lock-free and concurrent versions are practical, and range scans are a linked-list walk. A treap has no such property: a split or merge changes the tree along a whole path, and a rotation-based treap changes an ancestor chain.

**Choosing.**

| Need | Choice |
|---|---|
| ordered set or map, no special pattern | red-black (what `std::set` is) or AVL |
| ordered set with no balance bits to store, or a very small node | scapegoat (24 bytes here), AA |
| a sequence with insert, erase, split, join and reverse at arbitrary positions | implicit treap |
| a split or a merge of two ordered sets as an operation | treap (`O(log n)` expected, against `O(n)` for rebuilding a red-black tree) |
| accesses with strong locality or a small hot set, single thread | splay |
| concurrent readers | not a splay tree (reads write); a skip list or a locked balanced tree |
| hard real-time per operation | not a splay tree, not a scapegoat tree (a rebuild is `O(n)`): a balanced tree with bounded repair |
| insertion in sorted order | any of them except a plain binary search tree; the treap and AA tree handled it as well as random input |

---

## 8. Common mistakes

1. **Priorities from a bad generator**, or the same priority reused on reinsertion. The expected bound assumes independent uniform priorities; a priority derived from the key gives a degenerate tree.
2. **Ties in 32-bit priorities** with millions of keys. They are harmless to correctness, and a checker that forbids equal priorities would fail on a valid tree.
3. **`merge` called with overlapping key ranges.** `merge(a, b)` requires every key of `a` below every key of `b`; otherwise the result is not a search tree and nothing complains.
4. **Forgetting to `tpush` before looking inside an implicit-treap node**, or before reading its children. The reversal flag is then applied late or twice.
5. **Splaying in a `const` method.** `find` changes the tree. The code above has no const `find`.
6. **Assuming a splay tree is `O(log n)` per operation.** The bound is amortized: the first access after a sorted build costs `O(n)`.
7. **Recursion on a splay tree.** Its height can be `n`. Destructors, shape functions and printing must be iterative.
8. **Scapegoat depth counted off by one.** The new node's depth is the length of the path to it; compare it with `floor(log_{1/A} n)` where `n` already counts the new node, as the code does.
9. **Rebuilding after erase with the wrong peak.** The full rebuild must reset `max_` to the new size, or the tree is rebuilt on every later erase.
10. **AA erase without the level repair.** Skipping the "lower a level that lost support" step leaves levels too high, and the checker finds a node of level above 1 with a missing child.

---

## 9. Questions to answer out loud

1. Why does the treap's shape not depend on the insertion order? What is the Cartesian tree?
2. Derive the expected depth of a treap node. What is the mean depth for a million keys?
3. How do `split` and `merge` give insert and erase?
4. What changes between a treap and an implicit treap? What does `rev` do, and when is it pushed?
5. State the three splay cases. Why is the zig-zig order (parent first) important?
6. State the potential function and the access lemma. Where does concavity of the logarithm enter?
7. When is a splay tree faster than a red-black tree, and when slower? Give a measurement.
8. Why does an insert deeper than `floor(log_{1/A} n)` always find a scapegoat?
9. What are skew and split in an AA tree, and what do they correspond to in a red-black tree?
10. Why can a skip list be made concurrent when a treap cannot easily?

---

## 10. Problems

**A. Treap trace.** With the priorities `pri(1..7) = 5300, 2100, 7700, 900, 6400, 3800, 8800`, give the treap for the keys 1 to 7 and the two trees after `split(root, 4)`. Does the insertion order matter?

**B. Cartesian tree.** Prove that for distinct priorities exactly one treap exists for a set of keys.

**C. Implicit treap.** Start from the sequence 0 to 9. Give the sequence after `reverse(0, 10)` and then `reverse(2, 5)`. Why can several `rev` flags be pending at once without making the contents wrong?

**D. Splay trace.** Insert 1, 2, 3, 4, 5 into a splay tree and give the preorder and the rotation count. Then `find(1)` and `find(3)`.

**E. Potential.** For a path of 3 nodes (sizes 3, 2, 1) compute `Φ`. A path of 1,023 nodes has `Φ = 8759.01`; after `find(1)` it is 4387.16 and the access took 1,022 rotations. What is the amortized cost, and how does it compare with the bound?

**F. Scapegoat.** With `A = 0.7`, what is the depth limit for `n = 8` and `n = 1,000,000`? In the ascending run, at which insert does the first rebuild happen, and how many nodes does it rebuild?

**G. AA.** Insert 1 to 7 into an AA tree and give the levels. Then erase 4. Which node ends up with a right child on its own level?

**H. Choosing.** Choose and defend: (a) a text editor buffer with cut, paste and reverse; (b) a cache of recently used objects looked up by key, accessed with strong locality; (c) an ordered map read by many threads; (d) an ordered set in a device with very little memory per node.

---

## Answers

**A.** The key 7 has the highest priority (8800), so it is the root. Of the keys 1 to 6 the highest is key 3 (7700), which becomes the left child; its left part is `1 2` (priorities 5300, 2100, so 1 is the root with 2 as its right child) and its right part is `4 5 6` (900, 6400, 3800: root 5, children 4 and 6). The preorder is `7 3 1 2 5 4 6`. Splitting at 4 gives `[3 1 2]` (keys below 4: root 3, then 1 with child 2) and `[7 5 4 6]`. The order of insertion does not matter: all 5,040 orders of 1 to 7 give this tree.

**B.** By induction on the number of keys. The root of the heap order must be the key with the highest priority: any other key at the root would have a child with a higher priority, which is forbidden. Fixing the root fixes which keys are in the left subtree and which in the right (the search order). Each part is again a set of keys with distinct priorities, so by induction its tree is unique.

**C.** After `reverse(0, 10)`: `9 8 7 6 5 4 3 2 1 0`. After `reverse(2, 5)`: `9 8 5 6 7 4 3 2 1 0`. The flag is set on the root of each extracted range and pushed to its children only when some function next looks inside that node, so several flags can be pending at once (for example on subtrees the second call's splits never entered); the contents are still always right, because every function calls `tpush` before it looks inside a node.

**D.** Insertion 1 to 5: preorder `5 4 3 2 1` after 4 rotations in all (one per insert from the second). `find(1)`: 4 rotations, preorder `1 4 2 3 5`. `find(3)`: 3 rotations, preorder `3 1 2 4 5`. (Program output.)

**E.** A path of 3 has subtree sizes 3, 2, 1: `Φ = log2 3 + log2 2 + log2 1 = 1.585 + 1 + 0 = 2.585`. For the path of 1,023: amortized cost = 1,022 + 4387.16 - 8759.01 = **-3349.84**, and the lemma's bound is `3 log2 1023 + 1 = 31.00`; the cost is negative, so the bound holds with a huge margin: the potential released by flattening the path pays for the slow access.

**F.** For `n = 8`: `floor(log 8 / log(1/0.7)) = floor(2.079 / 0.3567) = 5`. For 1,000,000: `floor(13.816 / 0.3567) = 38`. In the ascending run the first rebuild is at the **7th insert** (limit for 7 nodes is 5, the new node is at depth 6) and it rebuilds **4 nodes**; the tree then has height 6. (Program output.)

**G.** After 1 to 7: `4/3 2/2 1/1 3/1 6/2 5/1 7/1`. After erasing 4: `3/2 1/1 2/1 6/2 5/1 7/1`. The node 1 (level 1) has the right child 2 on level 1: they form a 3-node in the 2-3 tree.

**H.** (a) The implicit treap: cut, paste and reverse are splits and merges, `O(log n)`, where a vector costs `O(n)` per operation (1961 ns against 6535 ns at 100,000 elements). (b) A splay tree: a hot set of 1,024 keys took 171.0 ns against 383.9 ns for a red-black tree; but a hash table may serve better for a cache and is not measured here. (c) Not a splay tree, because reads write. A skip list, which supports lock-free readers (Topic 04.07), or a balanced tree under a read-write lock. (d) The scapegoat tree (24-byte node, 32 bytes per element, no balance bits) or an AA tree (same sizes with a level), accepting a rebuild pause for the scapegoat tree.

---

## Cheat sheet

- **Treap**: BST on key, heap on random priority; shape = Cartesian tree, independent of history. Expected depth `2 (1 + 1/n) H_n - 4` (24.79 for 1M keys; measured 24.52 and 24.39 for random and ascending input). `split` and `merge` give everything; an implicit treap is a sequence with range reverse (1961 ns per op against 6535 for `std::vector`).
- **Splay**: no balance data; splay the accessed node (zig, zig-zig with the parent first, zig-zag). Potential `Σ log2 s(v)`; amortized `≤ 3 (r(t) - r(x)) + 1`, so `O(log n)` amortized and `O(n)` worst for one operation. Fast on locality (171.0 ns against 383.9 ns on a hot set), slow on uniform access (3060.1 against 1760.5 ns), reads write.
- **Scapegoat**: `A = 0.7`; if an insert is deeper than `floor(log_{1/A} n)`, rebuild the first lopsided ancestor perfectly; rebuild all when `n < A max`. Height 39 on 1M keys; 0.01 and 23.03 rebuilt nodes per insert (random, ascending); 32 bytes per element.
- **AA**: red-black with red only on the right; skew and split; height 27 and 26 on 1M keys; fastest lookup of the new structures (1440.7 ns).
- Randomized or amortized trees trade the worst case for simplicity; for a library set or map, red-black or AVL; for sequences, splitting and joining: treap; for locality: splay.

---

## Sources

- The treap: Aragon and Seidel, "Randomized search trees" (1989/1996), and the Cartesian tree (Vuillemin, 1980); the expected-depth derivation is the standard one (from memory), and the formula was checked against the measured mean depths here.
- The splay tree and the access lemma: Sleator and Tarjan, "Self-adjusting binary search trees" (1985); the sequential access bound `4.5 n` is Tarjan's (from memory). The zig-zig step is proved above; the lemma itself was checked on 30,000 accesses by the tests.
- The scapegoat tree: Galperin and Rivest, "Scapegoat trees" (1993), and Andersson's weight-balanced rebuilding (from memory); the existence argument is written out above.
- The AA tree: Arne Andersson, "Balanced search trees made simple" (1993), as I remember the skew/split formulation; the code was verified by the checker, not against the paper.
- [cp-algorithms: Treap (Cartesian tree)](https://cp-algorithms.com/data_structures/treap.html) for the split/merge and implicit treap operations (listed in the plan's sources; I did not re-read it for this tutorial).
- Topics 04.07, 10, 11 and 12 of this series for the skip list, AVL and red-black measurements used as baselines.
- Marked "from memory" in the text: the tie estimate for 32-bit priorities, the static optimality and working set theorems, the splay tree's uses in allocators and routing tables, the `Ω(m)` argument for scapegoat amortization, the zig-zag case of the lemma.
- All other claims in this tutorial come from the tests and measurements described at the top.
