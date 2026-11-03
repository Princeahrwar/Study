# DSA 07.10: Rotations and AVL Trees: The One Primitive and the First Balanced Tree

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (3.5 reading, 5.5 problems) · You need: [BST core operations](07_bst_core_operations.md) (the invariant, pointer-to-pointer insert and erase, degeneration by insertion order), [BST problem patterns](08_bst_problem_patterns.md) (building a perfectly balanced tree from sorted input) and [Inside `std::set` and `std::map`](09_inside_std_set_and_map.md) (the heights real red-black trees reach). There is no companion demo file: every listing below is the complete, tested code of an AVL tree, and a separate demo would only repeat it.*

By the end of this tutorial you can:

- write left and right rotations from memory, say exactly which pointers and which heights change, and show that the in-order sequence cannot change;
- state the AVL rule, compute a balance factor, and name the four imbalance cases (LL, RR, LR, RL) from the signs of two balance factors;
- write insert and erase for an AVL tree that repair the tree on the way back up the recursion, and explain why an insert needs at most one repair while an erase can need many;
- derive the height bound of about 1.44 log₂ n from the sparsest possible AVL trees and Fibonacci numbers, and build those trees;
- compare AVL with red-black trees using measured heights, depths, repair counts and sizes, and say honestly where AVL wins and where it does not;
- recognise where a rotation is the answer to an interview problem and where it is not.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 5,794,916 checks passed under AddressSanitizer with UBSan (GCC 13.3, libstdc++ 13). The tests built every insertion order of 1 to 9 keys (409,113 orders) and checked the AVL rules after every insert; erased every key and some absent keys from every insertion order of 1 to 7 keys; ran 120 random sequences of 3,000 inserts and erases against `std::set`; tried four hostile insertion orders up to 131,071 keys against the exact height limit; rebuilt the sparsest AVL trees and compared their sizes with Fibonacci numbers and with an exhaustive search over tree shapes; and checked that the invariant checker fails when a stored height, the key order or the balance is damaged by hand. The sizes, counts, heights and shapes below are program output. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, glibc malloc, best of 3 on 1,000,000 distinct random `int` keys; read them as shapes, not benchmarks. The worked answers in the Answers section were computed by an independent Python implementation. Statements about other libraries and about history are marked "from memory" or point to a source.

---

## 1. What goes wrong, and what is allowed to fix it

[Topic 7](07_bst_core_operations.md) ended with a warning: a binary search tree is only as good as its insertion order, and sorted input turns it into a list. Measured on 1,000,000 distinct keys, a plain BST built from a random order reached height 49 with a mean node depth of 24.24 (the least possible for a million nodes is 17.95), and from sorted input it would be a million levels deep.

A balanced tree repairs the shape after every update, and the repair has three requirements:

1. it must not change the **in-order sequence** (the keys are still sorted left to right);
2. it must be **local**: a constant amount of work per level it touches, and it touches only the path the update came down;
3. it must keep some measurable **balance rule** true, one that implies height `O(log n)`.

Requirement 1 is the reason the tool exists. Moving a subtree changes the tree but not the order if, and only if, the subtree stays between the same two neighbours. That move is the rotation, and every balanced search tree that works by local shape repair (AVL, red-black, the splay tree and the treap) is built from it. What differs is only the rule that decides when and where to rotate.

---

## 2. The rotation

Take a node `x` whose left child is `y`. A **right rotation** at `x` makes `y` the parent and `x` its right child. The one subtree that has to move is `B`, the right subtree of `y`:

```text
        x                  y
       / \                / \
      y   C     ==>      A   x
     / \                    / \
    A   B                  B   C
```

Read the in-order sequence on both sides: `A y B x C`. Everything in `A` is below `y`, everything in `B` is between `y` and `x`, everything in `C` is above `x`. Before, `B` hung right of `y` (so it was above `y` and below `x`); after, it hangs left of `x` (still above `y`, still below `x`). No key passes another key, so the order is untouched for any `A`, `B`, `C`, empty or not. A **left rotation** is the mirror image and the exact inverse.

Here are the two functions, with the node and the height helpers they use:

```cpp
struct Node {
    int key;
    int h = 1;                         // height of the subtree rooted here: leaf = 1, null = 0
    Node *l = nullptr, *r = nullptr;
    explicit Node(int k) : key(k) {}
};

inline long rotations = 0;             // instrumentation: single rotations performed
inline long rebalances = 0;            // instrumentation: nodes that needed repair (single or double)

inline int ht(const Node* t) { return t ? t->h : 0; }
inline int bf(const Node* t) { return ht(t->l) - ht(t->r); }    // > 0: left side taller
inline void upd(Node* t) { t->h = 1 + std::max(ht(t->l), ht(t->r)); }
```

```cpp
/*       x                  y
        / \                / \
       y   C     ==>      A   x          rotR(x): the left child y becomes the root
      / \                    / \
     A   B                  B   C         rotL(y) is the exact inverse   */
inline Node* rotR(Node* x) {
    Node* y = x->l;
    x->l = y->r;                       // B changes parent: it stays between y and x in order
    y->r = x;
    upd(x);                            // x is now lower, so it must be updated first
    upd(y);
    ++rotations;
    return y;                          // the caller stores this where x used to hang
}

inline Node* rotL(Node* x) {
    Node* y = x->r;
    x->r = y->l;
    y->l = x;
    upd(x);
    upd(y);
    ++rotations;
    return y;
}
```

Details that are easy to get wrong:

- **Two heights change, no others.** After `rotR(x)`, only `x` and `y` have new subtrees. `A`, `B` and `C` are unchanged, and the ancestors are the caller's business. `x` must be updated before `y`, because `y` now has `x` below it.
- **The function returns the new root of the subtree.** The parent's link must be overwritten with it. In the recursive style below that is `t->l = rotL(t->l)`; with a pointer-to-pointer (Topic 7) it is `*link = rotR(*link)`. Forgetting the store is the classic bug: the rotated nodes become unreachable.
- **Cost.** Three pointer writes (`x->l`, `y->r` and the caller's link) and two height updates: `O(1)`, no allocation, no comparison of keys.
- **Parent pointers.** If nodes also store a parent, a rotation must also fix the parent of `B`, of `y` and of `x`: three more writes. The Linux kernel's trees do this (Topic 13). The recursive style here needs no parent pointers.

The tests rotated random plain BSTs at the root in both directions and checked three things each time: the in-order sequence is unchanged, the stored heights are still correct, and `rotL(rotR(t))` returns exactly the original shape.

---

## 3. The AVL rule

Adelson-Velsky and Landis proposed the rule in 1962 (names and year from memory). A tree is an **AVL tree** if for every node the heights of its two subtrees differ by at most 1. Define the **balance factor** `bf(t) = height(left) - height(right)`; the rule says `bf` is -1, 0 or +1 everywhere. This tutorial uses the convention that an empty tree has height 0 and a leaf has height 1, so `ht(nullptr) = 0` and `bf` of a leaf is 0.

Every node stores its own height `h`. That costs 4 bytes, which the node's alignment pays for anyway: the key (4 bytes) and `h` (4 bytes) fill one 8-byte word, the two pointers take 16, and `sizeof(Node)` is 24. Storing only a 2-bit balance factor would not shrink the node unless the key were smaller or the bits were hidden in a pointer (the kernel hides the red-black colour in the low bits of the parent pointer; Topic 13).

The rule is local, but it is a statement about **subtrees**, exactly like the BST invariant of Topic 7: checking that each node is balanced is not enough unless the heights it uses are right, so the checker below computes heights from the bottom and also compares them with the stored ones. This is the same bottom-up check as the LeetCode problem *Balanced Binary Tree* (110): "return the height, or -1 as soon as a subtree is unbalanced".

```cpp
// Returns the height of t, or -1 if anything is wrong: key order (strictly between
// lo and hi), the stored height, or the balance rule. Counts the nodes into n.
inline int check(const Node* t, const int* lo, const int* hi, std::size_t& n) {
    if (!t) return 0;
    if (lo && !(*lo < t->key)) return -1;
    if (hi && !(t->key < *hi)) return -1;
    int a = check(t->l, lo, &t->key, n);  if (a < 0) return -1;
    int b = check(t->r, &t->key, hi, n);  if (b < 0) return -1;
    if (std::abs(a - b) > 1) return -1;
    if (t->h != 1 + std::max(a, b)) return -1;
    ++n;
    return t->h;
}

inline bool isAvl(const Tree& T) {
    std::size_t n = 0;
    return check(T.root(), nullptr, nullptr, n) >= 0 && n == T.size();
}
```

---

## 4. The four imbalance cases

An insert or an erase changes the height of one subtree by at most 1. So at the lowest ancestor where the rule breaks, the two sides differ by exactly 2. Say the left side is the tall one (`bf(t) = +2`; the right side is the mirror image). Look at the tall child `y = t->left` and at its balance factor:

| `bf(t)` | `bf(y)` | Name | Picture of the heavy path | Repair |
|---|---|---|---|---|
| +2 | +1 (or 0 on erase) | **LL** | `t`, then left, then left: a straight line | one right rotation at `t` |
| +2 | -1 | **LR** | `t`, then left, then right: a bend | left rotation at `y`, then right rotation at `t` |
| -2 | -1 (or 0 on erase) | **RR** | straight line to the right | one left rotation at `t` |
| -2 | +1 | **RL** | a bend | right rotation at `y`, then left rotation at `t` |

The reason for the double rotation: in the LR case the extra height is under `y`'s right child `z`, and a single right rotation at `t` would only move the extra height to the other side and leave the tree still out of balance. Rotating `y` left first lifts `z` above `y`, which turns the bend into a straight line, and then the single rotation works. The three-key examples show it. Inserting 3, 2, 1 is LL, inserting 1, 2, 3 is RR (one rotation each); inserting 3, 1, 2 is LR and 1, 3, 2 is RL (two rotations each). All four end as the same perfect tree with 2 at the root; the tests check exactly this, and that the counters read 1, 1, 2 and 2 rotations.

The whole repair is one function. It recomputes the height of `t`, looks at the balance factor, and picks the case:

```cpp
// Restore the AVL rule at t, assuming both subtrees already obey it and
// differ in height by at most 2 (true after one insert or one erase below t).
inline Node* fix(Node* t) {
    upd(t);
    int b = bf(t);
    if (b > 1) {                                   // left side is two taller
        if (bf(t->l) < 0) t->l = rotL(t->l);       // LR: bend the zig-zag into a line
        ++rebalances;
        return rotR(t);                            // LL: one right rotation
    }
    if (b < -1) {                                  // right side is two taller
        if (bf(t->r) > 0) t->r = rotR(t->r);       // RL
        ++rebalances;
        return rotL(t);                            // RR
    }
    return t;
}
```

Two details deserve attention:

- **The test is `bf(t->l) < 0`, strictly.** When the tall child is perfectly balanced (`bf = 0`) the straight single rotation is the right repair. That situation never occurs after an insert, but it does after an erase (section 6), and writing `<= 0` there turns a correct single rotation into a double one that breaks the tree.
- **`fix` calls `upd(t)` first, always.** Even when no rotation happens, the node's height may have changed because something below it grew or shrank. A repair function that only runs when it sees an imbalance leaves stale heights above it.

---

## 5. Insert

Insert is the plain BST insert of Topic 7 plus one line on the way back up: after the recursive call returns, repair this node. The `added` flag lets a duplicate skip the repair, since nothing changed.

```cpp
inline Node* insert(Node* t, int k, bool& added) {
    if (!t) { added = true; return new Node(k); }
    if (k < t->key)      t->l = insert(t->l, k, added);
    else if (t->key < k) t->r = insert(t->r, k, added);
    else return t;                                 // duplicate: nothing changed
    return added ? fix(t) : t;
}
```

**Why one repair is enough.** Suppose the insert made `t`'s left side two taller than the right. Because `t` is the lowest broken node, `y` obeys the rule, and because `y`'s height just grew, one of its subtrees became strictly taller than the other: `bf(y)` is +1 or -1, never 0. Take the LL case with heights: `C` has height `k`, `y`'s subtrees `A` and `B` have heights `k+1` and `k`, so `y` has `k+2` and `t` was `k+3` after the insert. Before the insert `A` had height `k`, `y` had `k+1` and `t` had `k+2`. After `rotR(t)` the node `x` (old `t`) has `B` and `C`, both of height `k`, so `x` has `k+1`, and `y` has `A` (`k+1`) and `x` (`k+1`), so `y` has `k+2`. The repaired subtree is exactly as tall as `t` was **before** the insert, so no ancestor sees any change and every `fix` above only recomputes the same heights. The LR case ends at the same height by the same count.

The tests measured this: over all 409,113 insertion orders of 1 to 9 keys, no single insert ever repaired more than one node (`rebalances` was at most 1). A repair is one rotation (LL, RR) or two (LR, RL), so an insert does at most two rotations.

---

## 6. Erase

Erase is the BST erase of Topic 7: a node with at most one child is replaced by that child; a node with two children is replaced by its in-order successor, which is **relinked** (not key-copied, so addresses stay valid, as in Topic 7). Every node on the path back to the root is repaired. The successor is detached by `popMin`, which repairs the nodes along the left spine it walked down.

```cpp
// Detach the minimum node of t into m and return the repaired remainder.
inline Node* popMin(Node* t, Node*& m) {
    if (!t->l) { m = t; return t->r; }
    t->l = popMin(t->l, m);
    return fix(t);
}

inline Node* erase(Node* t, int k, bool& gone) {
    if (!t) return nullptr;
    if (k < t->key)      t->l = erase(t->l, k, gone);
    else if (t->key < k) t->r = erase(t->r, k, gone);
    else {
        gone = true;
        Node *a = t->l, *b = t->r;
        delete t;
        if (!b) return a;                          // zero or one child: relink
        Node* m;
        b = popMin(b, m);                          // successor, relinked (not copied)
        m->l = a;
        m->r = b;
        return fix(m);
    }
    return gone ? fix(t) : t;
}
```

An erase can be worse than an insert for two reasons. First, the repair does not always restore the old height, so the imbalance can travel upward. Second, the tall child may be perfectly balanced. With `bf(t) = +2` after an erase on the right, let the right side have height `k` and the left child `y` have height `k+2`:

| `bf(y)` | `y`'s subtrees `A`, `B` | After the single right rotation | Height of the subtree | What follows |
|---|---|---|---|---|
| +1 | `k+1`, `k` | `x` has `k+1`, `y` has `k+2` | `k+2`, one less than before the erase (`k+3`) | the shrink continues upward |
| 0 | `k+1`, `k+1` | `x` has `k+2`, `y` has `k+3` | `k+3`, the same as before | the repair stops here |
| -1 | `k`, `k+1` | LR double rotation | `k+2`, one less than before | the shrink continues upward |

So a repair that shrinks the subtree passes the problem to the parent, and an erase can repair up to one node per level. The measurements show how much that matters:

| Measurement | Result |
|---|---|
| Erase all 1,000,000 keys in random order | 265,646 repairs and 372,601 rotations in total (0.27 repairs and 0.37 rotations per erase); at most 6 repairs and 8 rotations in a single erase |
| Sparsest AVL trees (section 8), every key tried, worst key, heights 3 to 12 and 15 (4 to 376 and 1,596 nodes) | `(h - 1) / 2` rounded down repairs: 1, 1, 2, 2, 3, 3, 4, 4, 5, 5 for h = 3 to 12 and 7 for h = 15; every repair was a single rotation |

The sparsest trees are the adversary: nodes on the heavy path sit at the edge of the rule, so a repair there can shrink its subtree and hand the problem upward, and the number of repairs grows with the height (every key was tried for those heights; I measured the pattern and do not claim a proof here). For typical data the count stays tiny. The honest summary of the rotation cost: **insert at most 1 repair; erase up to `O(log n)` repairs, on average well under one**.

The class around the recursive functions keeps the root and the size, and its `contains` is a plain loop (Topic 7):

```cpp
class Tree {
    Node* root_ = nullptr;
    std::size_t n_ = 0;
    static void destroy(Node* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }
public:
    Tree() = default;
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    ~Tree() { destroy(root_); }                    // recursion depth <= 1.44 log2 n: safe

    bool insert(int k) { bool a = false; root_ = avl::insert(root_, k, a); n_ += a; return a; }
    bool erase(int k)  { bool g = false; root_ = avl::erase(root_, k, g);  n_ -= g; return g; }
    bool contains(int k) const {
        for (const Node* t = root_; t; t = k < t->key ? t->l : t->r)
            if (!(k < t->key) && !(t->key < k)) return true;
        return false;
    }
    std::size_t size() const { return n_; }
    int height() const { return ht(root_); }
    const Node* root() const { return root_; }
    Node*& rootRef() { return root_; }             // for tests that damage the tree on purpose
};
```

The destructor recurses, which is safe here for a reason a plain BST cannot offer: the depth is at most 1.44 log₂ n, about 28 for a million keys.

---

## 7. A hand trace

Insert 10, 20, 30, 40, 50, 25 into an empty tree. The nodes named below are the ones where `fix` found `bf = ±2`.

| Insert | Repair | Tree after (preorder) |
|---|---|---|
| 10, 20 | none | 10 20 |
| 30 | at 10, case RR: left rotation | 20 10 30 |
| 40 | none | 20 10 30 40 |
| 50 | at 30, case RR: left rotation | 20 10 40 30 50 |
| 25 | at 20, case RL: right rotation at 40, then left rotation at 20 | 30 20 10 25 40 50 |

The last step is the bend. Before the insert, node 20 has a left child 10 (height 1) and a right child 40 (height 2, with children 30 and 50). Inserting 25 puts it under 30, so the right side has height 3 against 1. The tall child 40 has `bf = +1` (its tall side is 30, on its left), which is the RL pattern. Rotate 40 right (30 becomes its parent), then rotate 20 left (30 becomes the root of that subtree, with 20 and 40 as children, and 25, which was below 30, goes to 20's right).

---

## 8. The height bound

How tall can an AVL tree with `n` nodes get? Ask the reverse question: what is the **fewest** nodes `N(h)` an AVL tree of height `h` can have? Its root has two subtrees; one must have height `h - 1`; the other can be as short as the rule allows, `h - 2`; and both should themselves be as sparse as possible:

```
N(0) = 0,  N(1) = 1,  N(h) = N(h - 1) + N(h - 2) + 1
```

The values are 0, 1, 2, 4, 7, 12, 20, 33, 54, 88, 143. Add 1 to each: 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, the Fibonacci numbers. By induction, `N(h) + 1 = (N(h-1) + 1) + (N(h-2) + 1)`, which is the Fibonacci recurrence, so

```
N(h) = F(h + 2) - 1,   with F(1) = F(2) = 1.
```

Now invert it. With `φ = (1 + √5) / 2 = 1.618...`, induction gives `F(k) >= φ^(k-2)`, so `n + 1 >= N(h) + 1 = F(h + 2) >= φ^h`, and therefore

```
h <= log_φ(n + 1) = 1.4404 log₂(n + 1).
```

A sharper form, `h < 1.4405 log₂(n + 2) - 0.3277`, is quoted in the literature (I recall it from Knuth; the tests below hold against it). In plain words: **an AVL tree is never more than about 44% taller than a perfect tree**, and the red-black bound from Topic 9 allows 100% (`2 log₂(n + 1)`).

The sparsest tree is real, and the code builds it: its root takes the sparsest tree of height `h - 1` on the left and `h - 2` on the right.

```cpp
// The AVL tree of height h with the fewest nodes: one subtree of height h-1 and
// one of height h-2, each again minimal. Keys 0, 1, 2, ... in order.
inline Node* minTree(int h, int& next) {
    if (h <= 0) return nullptr;
    Node* t = new Node(0);
    t->l = minTree(h - 1, next);
    t->key = next++;
    t->r = minTree(h - 2, next);
    t->h = h;
    return t;
}
```

The tests built it for every height from 1 to 22, checked that it passes the AVL checker, that its size is `F(h + 2) - 1`, that no tree of height `h` is smaller (an exhaustive search over height splits up to `h = 12` agreed), and that one node fewer allows only height `h - 1`.

How the bound looks next to measured heights, on 1,000,000 keys (`log₂(n + 1) = 19.93`, perfect height 20):

| Insertion order | AVL height | Plain BST height | libstdc++ `std::set` height |
|---|---|---|---|
| random | 24 | 49 | 25 |
| ascending | 20 | would be 1,000,000 | 37 |
| descending | 20 | would be 1,000,000 | not measured |
| alternating smallest and largest | 25 | would be 1,000,000 | not measured |
| **limit for this n** | **28** (exact maximum, from `N(h)`) | none | 39 (`2 log₂(n + 1)`, rounded down) |

Three things stand out. A sorted insert into an AVL tree builds a nearly perfect tree (height 20 is the minimum possible), while `std::set` reaches 37. A random insert gives AVL 24 and the red-black tree 25: the same. And the AVL tree never comes near its limit of 28 on these inputs; the sparse shape is rare, and you have to construct it on purpose.

The same story with the **mean** depth, which is what an average lookup pays (root = 0):

| 1,000,000 keys | Mean node depth |
|---|---|
| the least possible for 1,000,000 nodes (levels 0 to 18 full, the rest at depth 19) | 17.95 |
| AVL, random insertion | 18.41 |
| AVL, ascending insertion | 17.95 |
| `std::set`, random insertion | 18.43 |
| `std::set`, ascending insertion | 18.33 |
| plain BST, random insertion | 24.24 |

The mean depth of AVL and red-black is nearly the same (18.41 against 18.43 for random insertion; 17.95 against 18.33 for ascending, where the AVL tree is exactly the minimum). AVL's stricter rule buys a better **worst case** (28 against 39 levels), not a better **typical** lookup. A random plain BST pays 5.8 more levels on average at 1,000,000 keys (24.24 against 18.41) and 4.8 more at 100,000 (19.68 against 14.93).

---

## 9. What it costs in repairs

Rotations are cheap but not free; this is what the shape costs to maintain, counted by the instrumented code on 1,000,000 inserts:

| Insertion order | Nodes repaired | Rotations |
|---|---|---|
| random | 466,307 (0.47 per insert) | 698,889 (0.70 per insert) |
| ascending | 999,980 | 999,980 |
| descending | 999,980 | 999,980 |
| alternating smallest and largest | 999,975 | 1,624,957 (mostly double rotations) |

A random insert repairs about every other time. A sorted insert repairs almost every time, but always with a single rotation, since a straight line is the RR case. The alternating order bends the path every time, so double rotations dominate. None of this is a problem: it is `O(1)` extra per insert, and the result is a better tree than the random insert gives.

The measured memory and time against `std::set` on the same machine:

| 1,000,000 random keys | AVL (this code) | `std::set<int>` |
|---|---|---|
| node request, bytes | 24 | 40 |
| heap bytes per element (glibc) | 32.0 | 48.0 |
| build time (insert all) | 817.0 ms | 818.4 ms |
| lookup, mean per key | 821.0 ns | 1108.0 ns |

The build times tie (other runs of the same program differed by up to 10% in either direction). The AVL lookup took 26% less time in the saved run and 26% to 33% less in the other runs I made, but the mean depths above are nearly equal, so the tree shape is not the reason. The most likely cause is the node: 32 bytes of heap per element against 48 means the tree is two thirds the size and more of it stays in cache. I did not isolate that. Remember also that the comparison is unfair in the other direction: the library container carries the full standard interface (iterators, allocators, comparators, node handles), and this class has a recursive insert for one key type.

---

## 10. AVL, red-black and the others

| | AVL | Red-black (Topics 11 to 13) |
|---|---|---|
| Height limit | `1.44 log₂ n` | `2 log₂ n` |
| Rotations per insert | at most 2 (one repair) | at most 2 (from memory; Topic 11 tests it) |
| Rotations per erase | up to `O(log n)` | at most 3 (from memory; Topic 12 tests it) |
| Extra field | height (or 2 bits) | one colour bit |
| Best for | lookup-heavy workloads | update-heavy workloads, and what the libraries chose |

The standard libraries picked red-black trees (Topic 9). AVL trees have been used elsewhere. The Linux 2.4 kernel kept the address-space regions of a process in an AVL tree (the file `mm/mmap_avl.c`, which the patch for 2.4.10 deletes; the replacement was a red-black tree, from memory), the GNU libavl package implements AVL and other balanced trees, and I recall Boost.Intrusive offering AVL containers next to its red-black ones (from memory). The usual argument for red-black is the cheaper erase in the table (from memory; I did not measure red-black repairs here), which makes the choice a trade, not a verdict.

The rotation is shared by more than these two. A **splay tree** rotates the accessed node toward the root on every lookup, a **treap** rotates a node up while its random priority beats its parent's, and the **scapegoat tree** avoids rotations altogether by rebuilding a subtree when it gets too deep (that last one, the rebuild, is the sorted-array-to-balanced-tree function of Topic 8). These are named here for orientation and are not tested in this tutorial.

---

## 11. Common mistakes

1. **Not returning and storing the new root.** `rotR(t);` alone, without `t = rotR(t);`, leaves the parent pointing at the demoted node.
2. **Updating `y` before `x`** in a rotation: `y`'s height then uses the old height of `x`.
3. **Using the wrong child for the case test.** The case is decided by the balance factor of the **tall child** (`t->l` when `bf(t) > 0`), not of `t` itself and not of the other child.
4. **`<= 0` where the code needs `< 0`** (section 4): after an erase the tall child can be balanced, and then a single rotation is correct.
5. **Stopping an erase after the first repair.** Unlike an insert, an erase repair can leave the subtree shorter, so the loop or the recursion must continue to the root.
6. **Mixing the height convention.** Null as 0 with a leaf as 1, or null as -1 with a leaf as 0, both work, but mixing them gives a balance factor off by one at every leaf.
7. **Skipping `upd` when no rotation happens.** The heights above an insert or erase must be recomputed even when no node is out of balance.
8. **Copying a key instead of relinking the successor** and then keeping an iterator or pointer to the successor's old node: it now holds a different key. The code relinks (Topic 7).
9. **Believing the balance rule is "equal node counts" or "equal depth".** It is about heights, and a tree whose left side has many more nodes can still be perfectly legal.

---

## 12. Questions to answer out loud

1. What are the three requirements of a balance repair, and which one makes the rotation necessary?
2. Draw a right rotation and say where subtree `B` goes and why the in-order sequence is unchanged.
3. How many heights does a rotation change, and which ones?
4. How do you tell LL from LR? What is different about the repair?
5. Why is one repair enough after an insert, and why not after an erase?
6. Where does the Fibonacci sequence come from in the height bound, and what does `1.44` mean?
7. Your colleague says AVL trees are faster for lookups than red-black trees because they are better balanced. What do the mean-depth numbers say?
8. What does `std::set` use, and what would make you choose AVL instead?

---

## 13. Problems

**A. Trace.** Insert 10, 20, 30, 40, 50, 25 (section 7) by hand and then insert 5, 4. Give the preorder after each step and name the repair case when there is one.

**B. Erase, the balanced child.** Insert 1, 2, ..., 10 in order. Draw the tree. Erase 1, then erase 2. Which node needs a repair, which case is it, and how does the height change?

**C. Counting.** (a) What is the fewest nodes in an AVL tree of height 10? (b) What is the largest height an AVL tree with 100 nodes can have? (c) With 1,000,000 nodes?

**D. The cascade.** In the sparsest tree of height 7 (33 nodes), erasing the key 31 (the keys are 0 to 32) repairs three nodes. Draw the sparsest tree of height 5 (12 nodes, keys 0 to 11) and find the key whose erase repairs two nodes; explain why each repair shrinks the subtree.

**E. Proof.** Prove that an insert never needs more than one repair, using the heights from section 5 for the LR case (the case not shown there).

**F. Code.** (a) Write `bool isBalanced(Node*)` for the LeetCode problem *Balanced Binary Tree* (110) in `O(n)` using the idea of the checker. (b) The nodes of this tutorial store heights. Why is checking `|bf| <= 1` at every node still not a proof that the tree is an AVL tree?

**G. Code.** *Balance a Binary Search Tree* (1382): given a BST of any shape, return a balanced one with the same keys. Give the `O(n)`-time solution using Topic 8 and say what you would need to do it with only rotations and `O(1)` extra space.

**More practice** (titles checked against [a LeetCode BST problem list](https://walkccc.me/LeetCode/topics/data-structures/tree/binary-search-tree); I did not verify difficulty labels): 1382 *Balance a Binary Search Tree*, 108 *Convert Sorted Array to Binary Search Tree*, 701 *Insert into a Binary Search Tree*, 450 *Delete Node in a BST*, 96 *Unique Binary Search Trees*, 1373 *Maximum Sum BST in Binary Tree*; and 110 *Balanced Binary Tree* (not on that list; its number is from memory).

---

## Answers

**A.** After inserting 10, 20, 30, 40, 50, 25 the preorder is 30 20 10 25 40 50 (section 7). Insert 5: it goes left of 10; node 10 has bf +1, node 20 has a left subtree of height 2 and a right subtree of height 1 (bf +1), node 30 has 3 against 2 (bf +1); no node reaches ±2, so there is no repair: preorder 30 20 10 5 25 40 50. Insert 4: it goes left of 5; node 10 now has left subtree {5, 4} of height 2 and nothing on the right: bf +2, and the tall child 5 has bf +1: case LL, one right rotation at 10 (5 becomes the parent of 4 and 10). Preorder 30 20 5 4 10 25 40 50, height 4.

**B.** The tree for 1 to 10 has preorder 4 2 1 3 8 6 5 7 9 10, with six single left rotations during construction. Erasing 1 repairs nothing (preorder 4 2 3 8 6 5 7 9 10, height 4). Erasing 2 relinks 3 in its place and makes node 4 right-heavy by 2; its right child 8 has subtrees 6 (height 2) and 9 (height 2), so `bf(8) = 0`: the balanced-child case, one left rotation at 4. The preorder becomes 8 4 3 6 5 7 9 10, and the height stays 4 (the repair stopped there).

**C.** (a) `N(10) = F(12) - 1 = 143`. (b) `N(8) = 54` and `N(9) = 88` are at most 100, `N(10) = 143` is not, so the largest height is 9. (c) 28, found the same way (the exact maximum in the table of section 8).

**D.** The sparsest tree of height 5 has root 7, a left subtree of height 4 (keys 0 to 6) and a right subtree of height 3 (keys 8 to 11, with 10 at its root, 9 and 8 on the left, 11 on the right). Erasing 10 (or 11) repairs two nodes. For 10: the successor 11 is relinked into its place with the left subtree {9, 8} and nothing on the right, so 11 has `bf = +2` and its tall child 9 has `bf = +1`: LL, one right rotation, and the right subtree of 7 shrinks from height 3 to 2. Now the root 7 has height 4 on the left against 2 on the right, and its tall child 4 has `bf = +1`: LL again. Each repair shrank its subtree by one because the tall child leaned outward (the first row of the erase table), so the parent saw a one-level change and had to repair too. Keys 10 and 11 are the only keys of this tree with two repairs; keys 0 and 1 need none.

**E.** LR case: `t` has right subtree `C` of height `k`; its left child `y` has height `k+2` after the insert, with left subtree `A` of height `k` and right child `z` of height `k+1` (the one that grew). The two subtrees `P` and `Q` of `z` have heights `k` and `k-1` in some order (or are both empty when `z` is the new leaf and `k = 0`). Before the insert `z` had height `k`, so `y` had `k+1` and `t` had `k+2`. After the double rotation `z` is the root with `y` (holding `A` and `P`) and `t` (holding `Q` and `C`) as children; each of them has height `k+1`, so `z` has height `k+2`, the old height of `t`. Nothing above changes.

**F.** `int h(Node* t) { if (!t) return 0; int a = h(t->l), b = h(t->r); if (a < 0 || b < 0 || abs(a - b) > 1) return -1; return 1 + max(a, b); }` and `isBalanced = h(root) >= 0`; with stored heights the check is `abs(bf(t)) <= 1` per node, but it trusts the stored heights, which is why `check` also compares them.

**G.** Collect the keys with an in-order traversal and rebuild with `sortedToBst` of Topic 8: `O(n)` time and `O(n)` space, height `ceil(log2(n + 1))`. With rotations only, the Day-Stout-Warren method first turns the tree into a right-leaning list with right rotations at every node that has a left child, and then repeatedly rotates every second node of the list to the left; this is `O(n)` time and `O(1)` extra space. I describe it from memory and have not tested it here.

---

## Cheat sheet

- A rotation moves one subtree (`B`) from one side of the demoted node to the other. Order never changes, `O(1)`, two heights updated (demoted node first), the caller stores the returned root.
- AVL rule: `|height(left) - height(right)| <= 1` at every node; null = 0, leaf = 1; balance factor = left minus right.
- Case from two signs: `bf(t) = +2` with `bf(left) >= 0`: LL, one right rotation; with `bf(left) < 0`: LR, rotate the child left then `t` right. Mirror for -2. Use the strict `< 0`.
- Insert: BST insert, then `fix` on the way back; at most one repair (the subtree returns to its old height), at most 2 rotations.
- Erase: BST erase (relink the successor), `fix` on the way back; keep going to the root; can repair `O(log n)` nodes (worst case measured: `(h - 1) / 2` in the sparsest trees), under one per erase on average (0.27 measured).
- `N(h) = N(h-1) + N(h-2) + 1 = F(h+2) - 1`; `h <= log_φ(n + 1) = 1.44 log₂(n + 1)`; 1,000,000 keys: at most 28 levels (perfect 20; red-black limit 39).
- Measured: sorted insert gives height 20 (red-black `std::set`: 37); random insert gives 24 against 25; mean depths are equal (18.41 against 18.43); the AVL node is 32 bytes of heap against 48.
- The standard libraries chose red-black; AVL is the stricter tree, with the costlier erase.

---

## Sources

- Titles and numbers of 1382, 108, 701, 450, 96 and 1373 were checked against [a LeetCode BST problem list](https://walkccc.me/LeetCode/topics/data-structures/tree/binary-search-tree). Number 110 is from memory. Difficulty labels are not used because I could not verify them.
- The Linux 2.4 AVL tree: the page for [the 2.4.10 patch of `mm/mmap_avl.c`](https://ftp.riken.jp/Linux/kernel/v2.4/patch-html/patch-2.4.10/linux_mm_mmap_avl.c.html) shows the file, an AVL tree of memory areas, being deleted. That a red-black tree replaced it is from memory.
- GNU libavl: the [abstract of Ben Pfaff's libavl paper](https://benpfaff.org/papers/libavl-abstract.pdf) (I did not read the full text).
- Marked "from memory" in the text: the sharper height bound (Knuth), the red-black rotation limits (checked in Topics 11 and 12), Boost.Intrusive's AVL containers, Day-Stout-Warren, and the 1962 date and names.
- All other claims in this tutorial come from the tests and measurements described at the top.
