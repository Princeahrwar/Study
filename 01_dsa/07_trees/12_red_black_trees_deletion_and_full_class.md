# DSA 07.12: Red-Black Trees II: Erase, the Double Black, and a Complete `RBTree<K, V>`

*Part of the [Trees plan](00_plan.md) · Time: about 10 hours (4 reading, 6 problems) · You need: [Red-black trees I](11_red_black_trees_invariants_and_insertion.md) (the rules, the 2-3-4 reading and the insertion repair, which this tutorial reuses unchanged), [Rotations and AVL trees](10_rotations_and_avl_trees.md) (for the comparison) and [BST core operations](07_bst_core_operations.md) (erase by relinking the successor). There is no companion demo file: the complete class is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- say why erasing a red node costs nothing, why erasing a black node leaves one path a black node short (the **double black**), and what that means in the 2-3-4 tree (an empty node that must borrow or merge);
- name the four repair cases, say which ones repeat and which end the repair, and prove that an erase does at most three rotations;
- trace an erase by hand, in the binary tree and in the 2-3-4 tree at the same time;
- write a complete, tested `RBTree<K, V, Compare>` with iterators, bounds, insert, erase by key and by iterator, move support and an invariant checker;
- explain why this class and the libstdc++ `std::set` build the same trees under inserts and erases;
- compare AVL and red-black trees with measured erase costs, worst cases and workloads, and decide between them without folklore.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 16,442,992 checks passed under AddressSanitizer with UBSan (GCC 13.3, libstdc++ 13). The tests ran **every insertion order followed by every erase order** of 1 to 6 keys (533,417 runs, rules checked after each step); every insertion order of 7 and 8 keys with every single victim and two absent keys (448,560 cases); 150 random sequences of 3,000 operations against `std::map`, comparing lookups, bounds and forward and backward iteration; 200 sequences against the libstdc++ `std::set` comparing the whole tree after inserts and erases; pointer stability of held iterators over 20,000 random updates of other keys; erase-in-a-loop; `std::string` keys, move-only values and a reversed comparator; hostile insertion orders (ascending, descending, alternating, random) up to 32,767 keys followed by mass erases; and a checker that must fail when a colour, a parent link, a key or the size is damaged and must report the true height and black height. A Python implementation written separately agreed with the C++ on 300 random sequences of inserts and erases (tree, case counts and rotations) and produced the traces. The counts, heights and shapes below are program output. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, glibc malloc, best of 3; read them as shapes, not benchmarks. Claims about other sources are marked "from memory" or point to a source.

---

## 1. What erase has to repair

Erase starts as in Topic 7. To remove a node `z`:

- with at most one child, replace `z` by that child;
- with two children, take the in-order successor `y` (the minimum of the right subtree), **relink** it into `z`'s place and give it `z`'s colour. No key is copied, so every other node keeps its address (a test holds iterators across 20,000 updates of other keys and checks it).

In both cases one node leaves the tree for good from some position: `z` itself in the first case, `y` from its old position in the second. Call its original colour `c`, and call `x` the node that now hangs where it was (possibly a null link). The rules react as follows:

| `c` | Effect | Repair |
|---|---|---|
| **red** | none: no black node left any path; its parent and child were black, so no red pair appears | nothing to do |
| **black**, `x` red | every path through the position lost one black node, but `x` is red | recolour `x` black, which restores the count |
| **black**, `x` black or null | every path through the position is **one black node short**: rule 5 fails | the double-black repair below |

The third row is the whole difficulty. `x` is said to carry an **extra black** (a "double black"): it counts as two black nodes when we count black heights, which is exactly what the tree needs and the colours do not show. The repair moves that extra black up the tree, or absorbs it with a rotation and a recolouring.

---

## 2. The same problem in the 2-3-4 tree

Topic 11 showed that a black node and its red children form one node of a 2-3-4 tree. In that tree:

- removing a red node removes a key from a 3-node or a 4-node: the node survives with fewer keys;
- removing a black node whose node had only that key (a 2-node) leaves an **empty node**, one level shorter than all the others: that is the double black.

An empty node is repaired the way a B-tree repairs an underflow:

- **borrow**: if the sibling node has a spare key (it is a 3-node or a 4-node), rotate one key through the parent: the sibling's nearest key goes up, the parent's key comes down into the empty node;
- **fuse**: if the sibling is a 2-node, there is nothing to borrow; pull the parent's key down and merge it with the sibling into one node. The parent has lost a key, so it may underflow in turn: the problem moves up one level.

If the problem moves past the root, the whole tree becomes one level shorter, the one way that the black height decreases. The four cases below are these two moves, plus two bookkeeping steps for the binary encoding.

---

## 3. The four cases

Let `x` be the node with the extra black, `xp` its parent (kept in a variable because `x` can be a null link), `w` its sibling, and call the children of `w` the **near** nephew (on `x`'s side) and the **far** nephew. The sibling `w` is never null: `x`'s side is one black short, so the other side has at least one black node. Everything is mirrored when `x` is a right child; the code uses one flag.

| Case | The situation | Repair | What happens next |
|---|---|---|---|
| **1** | `w` is red (so `xp` is black and `w`'s children are black) | recolour `w` black and `xp` red, rotate `xp` toward `x`; the new sibling of `x` is black | **continue with case 2, 3 or 4**; the black height has not changed yet |
| **2** | `w` is black with two black nephews | recolour `w` red; the extra black moves to `xp` (`x = xp`) | if `xp` is red the loop ends (it becomes black), else **repeat** one level up |
| **3** | `w` is black, the **near** nephew is red, the far one black | recolour the near nephew black and `w` red, rotate `w` away from `x` | **continue with case 4** (the far nephew is now red) |
| **4** | `w` is black, the **far** nephew is red | `w` takes `xp`'s colour, `xp` and the far nephew become black, rotate `xp` toward `x` | **done**: the extra black is gone |

In 2-3-4 terms: case 2 is the **fuse**; cases 3 and 4 are the **borrow** (3 reshapes a sibling 3-node whose key is on the wrong side, 4 does the borrow and handles a 4-node sibling as well); case 1 is bookkeeping. When `w` is red, it is a key of the *parent's* node, not a sibling node; rotating moves it out so that the real sibling, one of its black children, becomes `x`'s sibling.

**Only case 2 repeats.** Case 4 ends the loop. Case 3 flows into case 4. Case 1 flows into case 2 (and then `xp` is red, so that ends too), 3 or 4. Therefore an erase performs at most one rotation in case 1, one in case 3 and one in case 4: **at most three rotations**, and any number of case 2 recolourings up the tree (at most one per level). The tests confirm it exhaustively: across all 533,417 runs of up to 6 keys the largest number of rotations in one erase was 3, and every one of the four cases occurred. Compare AVL (Topic 10), where an erase can repair a node at every level.

---

## 4. Traces

Insert 1, 2, ..., 8 into a red-black tree. The preorder is `4B 2R 1B 3B 6R 5B 7B 8R` and the 2-3-4 tree is `(2 4 6 (1) (3) (5) (7 8))`. Now erase 1, 2, 3 in turn:

| Erase | What happens | Preorder after | The 2-3-4 tree |
|---|---|---|---|
| 1 | 1 is a black leaf, `x` is null, its sibling 3 is black with two null nephews: **case 2**, 3 turns red, the extra black moves to the red parent 2, which becomes black | 4B 2B 3R 6R 5B 7B 8R | (4 6 (2 3) (5) (7 8)) |
| 2 | 2 is black with a single red child 3: the child is promoted and recoloured black, no case runs | 4B 3B 6R 5B 7B 8R | (4 6 (3) (5) (7 8)) |
| 3 | 3 is a black leaf whose sibling 6 is red: **case 1** (rotate left at the root 4) makes the new sibling 5 black; then **case 2** (5 has no children): 5 turns red and the extra black moves to the parent 4, which is red after case 1 and so becomes black | 6B 4B 5R 7B 8R | (6 (4 5) (7 8)) |

The 2-3-4 column shows the reading: erasing 1 fuses the empty node with its sibling `(3)` and the parent key 2 into `(2 3)`; erasing 3 fuses with the sibling `(5)` and the key 4 into `(4 5)`; the root node `(4 6)` loses the key 4 and keeps 6.

Every case also occurs in small trees. These five (insertion order, then the key erased) come from the independent Python model, and the C++ class gives the same trees:

| Insert order | Erase | Cases | Before | After (2-3-4 tree) |
|---|---|---|---|---|
| 1, 2, 3, 4 | 1 | 4 | `2B 1B 3B 4R` | `3B 2B 4B`, `(3 (2) (4))` |
| 1, 2, 4, 3 | 1 | 3, 4 | `2B 1B 4B 3R` | `3B 2B 4B`, `(3 (2) (4))` |
| 1, 2, 3, 4, 5, 6 | 1 | 1, 2 | `2B 1B 4R 3B 5B 6R` | `4B 2B 3R 5B 6R`, `(4 (2 3) (5 6))` |
| 1, 2, 3, 5, 6, 4 | 1 | 1, 4 | `2B 1B 5R 3B 4R 6B` | `5B 3R 2B 4B 6B`, `(3 5 (2) (4) (6))` |
| 1, 2, 4, 5, 6, 3 | 1 | 1, 3, 4 | `2B 1B 5R 4B 3R 6B` | `5B 3R 2B 4B 6B`, `(3 5 (2) (4) (6))` |

The first two rows end with the same tree from two different shapes: the 3-node `(3 4)` is stored with the red node on the far side in one tree and on the near side in the other, and case 3 is exactly the step that turns the second into the first.

---

## 5. The complete class

Everything below is one class, shown in five pieces. The node holds a key, a value, a colour and three pointers; the iterator is bidirectional through parent pointers; `stats` counts rotations and cases for the experiments (it costs one increment each).

```cpp
    struct Node {
        K key;
        V val;
        bool red = true;                                      // a new node is always red
        Node *l = nullptr, *r = nullptr, *p = nullptr;
        Node(const K& k, V v) : key(k), val(std::move(v)) {}
    };

    struct Stats {                                            // instrumentation, reset by the caller
        long rotations = 0;
        long insertCase[4] = {0, 0, 0, 0};                    // [1] recolor, [2] bend, [3] line
        long eraseCase[5] = {0, 0, 0, 0, 0};                  // [1] red sibling ... [4] far red nephew
    } stats;

    class iterator {                                          // bidirectional, in key order
        const RBTree* t_ = nullptr;
        Node* n_ = nullptr;                                   // null means end()
    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = Node;
        using difference_type = std::ptrdiff_t;
        using pointer = Node*;
        using reference = Node&;
        iterator() = default;
        iterator(const RBTree* t, Node* n) : t_(t), n_(n) {}
        Node& operator*() const { return *n_; }
        Node* operator->() const { return n_; }
        iterator& operator++() { n_ = successor(n_); return *this; }
        iterator& operator--() { n_ = n_ ? predecessor(n_) : maximum(t_->root_); return *this; }
        iterator operator++(int) { iterator o = *this; ++*this; return o; }
        iterator operator--(int) { iterator o = *this; --*this; return o; }
        bool operator==(const iterator& o) const { return n_ == o.n_; }
        bool operator!=(const iterator& o) const { return n_ != o.n_; }
    };
```

The private core: the helpers, the rotation of Topic 10 and Topic 11, and the insertion repair of Topic 11 (unchanged apart from the counters).

```cpp
    Node* root_ = nullptr;
    std::size_t n_ = 0;
    Compare cmp_;

    static bool isRed(const Node* n) { return n && n->red; }
    static Node* minimum(Node* n) { while (n->l) n = n->l; return n; }
    static Node* maximum(Node* n) { while (n->r) n = n->r; return n; }
    static Node* successor(Node* n) {
        if (n->r) return minimum(n->r);
        while (n->p && n == n->p->r) n = n->p;
        return n->p;
    }
    static Node* predecessor(Node* n) {
        if (n->l) return maximum(n->l);
        while (n->p && n == n->p->l) n = n->p;
        return n->p;
    }
    static void destroy(Node* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }

    // toLeft: left rotation at x (x->r moves up); otherwise a right rotation.
    void rotate(Node* x, bool toLeft) {
        Node* Node::* down = toLeft ? &Node::r : &Node::l;
        Node* Node::* up   = toLeft ? &Node::l : &Node::r;
        Node* y = x->*down;
        x->*down = y->*up;
        if (y->*up) (y->*up)->p = x;
        y->p = x->p;
        if (!x->p) root_ = y;
        else if (x == x->p->l) x->p->l = y;
        else x->p->r = y;
        y->*up = x;
        x->p = y;
        ++stats.rotations;
    }

    void insertFixup(Node* z) {                               // as in Topic 11
        while (isRed(z->p)) {
            Node* p = z->p;
            Node* g = p->p;
            bool pLeft = (p == g->l);
            Node* u = pLeft ? g->r : g->l;
            if (isRed(u)) {
                p->red = false; u->red = false; g->red = true;
                z = g;
                ++stats.insertCase[1];
            } else {
                if (z == (pLeft ? p->r : p->l)) {
                    z = p;
                    rotate(z, pLeft);
                    p = z->p;
                    ++stats.insertCase[2];
                }
                p->red = false; g->red = true;
                rotate(g, !pLeft);
                ++stats.insertCase[3];
            }
        }
        root_->red = false;
    }
```

Erasing: `transplant` is the standard "put this subtree where that one hung" helper, `eraseFixup` is the table of section 3, and `eraseNode` is the relinking of section 1.

```cpp
    // Put the subtree v where the subtree u hangs (v may be null).
    void transplant(Node* u, Node* v) {
        if (!u->p) root_ = v;
        else if (u == u->p->l) u->p->l = v;
        else u->p->r = v;
        if (v) v->p = u->p;
    }

    // x carries an extra "black" (a double black). xp is its parent, kept separately
    // because x may be a null link. Move the extra black up or get rid of it.
    void eraseFixup(Node* x, Node* xp) {
        while (x != root_ && !isRed(x)) {
            bool xLeft = (x == xp->l);
            Node* w = xLeft ? xp->r : xp->l;                  // the sibling; never null, x's side is one black short
            if (isRed(w)) {                                   // case 1: red sibling, make it black
                w->red = false;
                xp->red = true;
                rotate(xp, xLeft);
                w = xLeft ? xp->r : xp->l;
                ++stats.eraseCase[1];
            }
            Node* near = xLeft ? w->l : w->r;                 // the nephews
            Node* far  = xLeft ? w->r : w->l;
            if (!isRed(near) && !isRed(far)) {                // case 2: both nephews black, push the extra black up
                w->red = true;
                x = xp;
                xp = x->p;
                ++stats.eraseCase[2];
            } else {
                if (!isRed(far)) {                            // case 3: only the near nephew is red, bend into case 4
                    near->red = false;
                    w->red = true;
                    rotate(w, !xLeft);
                    w = xLeft ? xp->r : xp->l;
                    far = xLeft ? w->r : w->l;
                    ++stats.eraseCase[3];
                }
                w->red = xp->red;                             // case 4: far nephew red, one rotation ends it
                xp->red = false;
                far->red = false;
                rotate(xp, xLeft);
                ++stats.eraseCase[4];
                x = root_;
            }
        }
        if (x) x->red = false;
    }

    // Unlink z, relinking the successor when z has two children (no key is copied),
    // and return the node that followed z in key order.
    Node* eraseNode(Node* z) {
        Node* next = successor(z);
        Node* y = z;
        bool yWasRed = y->red;
        Node *x, *xp;
        if (!z->l) { x = z->r; xp = z->p; transplant(z, z->r); }
        else if (!z->r) { x = z->l; xp = z->p; transplant(z, z->l); }
        else {
            y = minimum(z->r);
            yWasRed = y->red;
            x = y->r;
            if (y->p == z) xp = y;
            else {
                xp = y->p;
                transplant(y, y->r);
                y->r = z->r;
                y->r->p = y;
            }
            transplant(z, y);
            y->l = z->l;
            y->l->p = y;
            y->red = z->red;                                  // y takes z's colour: z's place keeps its black height
        }
        delete z;
        --n_;
        if (!yWasRed) eraseFixup(x, xp);                      // a black node left: one path is a black short
        return next;
    }
```

The public interface. `find` goes through `lower_bound`, `insert` keeps the old value on a duplicate (like `std::map::insert`), and erasing by iterator returns the iterator of the following element.

```cpp
    RBTree() = default;
    explicit RBTree(Compare c) : cmp_(std::move(c)) {}
    RBTree(const RBTree&) = delete;
    RBTree& operator=(const RBTree&) = delete;
    RBTree(RBTree&& o) noexcept : root_(o.root_), n_(o.n_), cmp_(std::move(o.cmp_)) { o.root_ = nullptr; o.n_ = 0; }
    RBTree& operator=(RBTree&& o) noexcept {
        if (this != &o) { clear(); root_ = o.root_; n_ = o.n_; cmp_ = std::move(o.cmp_); o.root_ = nullptr; o.n_ = 0; }
        return *this;
    }
    ~RBTree() { destroy(root_); }

    std::size_t size() const { return n_; }
    bool empty() const { return n_ == 0; }
    void clear() { destroy(root_); root_ = nullptr; n_ = 0; }
    iterator begin() const { return iterator(this, root_ ? minimum(root_) : nullptr); }
    iterator end() const { return iterator(this, nullptr); }

    iterator lower_bound(const K& k) const {                  // first element not less than k
        Node* best = nullptr;
        for (Node* t = root_; t;) {
            if (!cmp_(t->key, k)) { best = t; t = t->l; }
            else t = t->r;
        }
        return iterator(this, best);
    }
    iterator upper_bound(const K& k) const {                  // first element greater than k
        Node* best = nullptr;
        for (Node* t = root_; t;) {
            if (cmp_(k, t->key)) { best = t; t = t->l; }
            else t = t->r;
        }
        return iterator(this, best);
    }
    iterator find(const K& k) const {
        iterator it = lower_bound(k);
        return (it != end() && !cmp_(k, it->key)) ? it : end();
    }
    bool contains(const K& k) const { return find(k) != end(); }

    std::pair<iterator, bool> insert(const K& k, V v) {
        Node *p = nullptr, *x = root_;
        bool left = false;
        while (x) {
            p = x;
            if (cmp_(k, x->key)) { x = x->l; left = true; }
            else if (cmp_(x->key, k)) { x = x->r; left = false; }
            else return {iterator(this, x), false};           // the key exists; keep the old value
        }
        Node* z = new Node(k, std::move(v));
        z->p = p;
        if (!p) root_ = z;
        else if (left) p->l = z;
        else p->r = z;
        ++n_;
        insertFixup(z);
        return {iterator(this, z), true};
    }
    V& operator[](const K& k) { return insert(k, V()).first->val; }

    iterator erase(iterator it) { return iterator(this, eraseNode(it.operator->())); }
    std::size_t erase(const K& k) {
        iterator it = find(k);
        if (it == end()) return 0;
        eraseNode(it.operator->());
        return 1;
    }
```

The checker verifies rules 2, 4 and 5, the key order (with the tree's own comparator), every parent link and the node count. A repair that rotates can break a parent link or the order without touching a colour, which is why they are in the checker.

```cpp
    struct Report { bool ok = false; int blackHeight = 0; int height = 0; std::size_t nodes = 0; };

    // Rules 2, 4, 5, the key order, the parent links and the node count.
    Report check() const {
        Report r;
        if (!root_) { r.ok = (n_ == 0); return r; }
        if (root_->red || root_->p) return r;
        int bh = walk(root_, nullptr, nullptr, nullptr, 1, r);
        r.blackHeight = bh;
        r.ok = bh >= 0 && r.nodes == n_;
        return r;
    }
private:
    int walk(const Node* t, const Node* parent, const K* lo, const K* hi, int depth, Report& r) const {
        if (!t) { if (depth - 1 > r.height) r.height = depth - 1; return 0; }
        if (t->p != parent) return -1;
        if (lo && !cmp_(*lo, t->key)) return -1;
        if (hi && !cmp_(t->key, *hi)) return -1;
        if (t->red && (isRed(t->l) || isRed(t->r))) return -1;
        int a = walk(t->l, t, lo, &t->key, depth + 1, r);  if (a < 0) return -1;
        int b = walk(t->r, t, &t->key, hi, depth + 1, r);  if (b < 0) return -1;
        if (a != b) return -1;
        ++r.nodes;
        return a + (t->red ? 0 : 1);
    }
public:
    Node*& rootRef() { return root_; }                        // for tests that damage the tree on purpose
```

Notes on the code:

- **`xp` is passed separately.** When `x` is a null link there is no node to ask for its parent. In `eraseNode`, the cases of `xp` are: the old parent of `z` when `z` has at most one child, `y` when `y` was `z`'s right child, and `y`'s old parent otherwise.
- **`yWasRed` is read before `y` takes `z`'s colour.** The colour that decides whether the black height changed is the original colour of the node that physically left its position.
- **`next` is computed before the unlink.** Because nodes are relinked and not copied, `next` stays valid, and `erase(iterator)` can return it.
- **In case 3, `w` and `far` are re-read after the rotation**, since the rotation changes who the sibling and the far nephew are.
- **The test `x == xp->l`** is used instead of asking `x` which side it is on, so it works for a null `x`; this needs the sibling to be non-null, as shown in section 3.
- **Two lines of case 3 are dead stores.** `near->red = false` is overwritten by case 4's `w->red = xp->red`, and `w->red = true` is undone by `far->red = false`, because `far` is the old `w`. Removing either line, or both, leaves every test passing (`verify.py` runs these three variants and requires that), so they are equivalent mutants. They stay because they put the tree in the state that case 4 is stated for (a black sibling with a red far nephew), which is how the textbooks and the 2-3-4 reading describe it.
- The destructor and `clear` recurse, which is safe: the depth is at most `2 log₂(n + 1)`.

---

## 6. It is the same tree as `std::set`

The tests ran 200 random sequences of inserts and erases (some with ascending runs, some with a small key range that forces duplicates and misses) on this class and on the libstdc++ `std::set<int>`, comparing the preorder with colours of the two trees every seventh operation and at the end. **All 200 sequences stayed identical, node for node and colour for colour.** The libstdc++ erase is therefore the same algorithm as the one in this tutorial (successor relinked, original colour of the departing node decides, the four cases), at least for these inputs and this library version, and this class can be read as a model of what `std::set::erase` does.

Other results of the tests:

- **Stability.** 50 held iterators (every 40th key of 2,000 even keys) still pointed at the same nodes, at the same addresses, after 20,000 random inserts and erases of odd keys and the erasing of all other even keys.
- **Erasing while iterating** with `it = tree.erase(it)` removed every multiple of 3 from 500 keys and left 333.
- **Move-only values** (`std::unique_ptr`) and a reversed comparator (`std::greater`) behave as expected; moving leaves the source empty and valid.
- **Hostile orders.** After ascending, descending, alternating and random inserts of up to 32,767 keys (sizes `2^e - 1`), followed by erasing every other key and then the rest, every height stayed within `2 log₂(n + 1)` and the rules held (checked in full up to 2,047 keys, height checked at every size).

---

## 7. What erase costs

Measured on 1,000,000 distinct random keys, erasing all of them in random order (the keys and the order are the same as in Topic 10's AVL measurement):

| | Red-black (this class) | AVL (Topic 10) |
|---|---|---|
| rotations in total | 377,524 (0.38 per erase) | 372,601 (0.37 per erase) |
| most rotations in one erase | 3 | 8 |
| erases that needed a repair | 498,125 | 265,646 repairs (at most 6 in one) |
| case 1 / 2 / 3 / 4 | 58,688 / 513,347 / 106,400 / 212,436 | |

- In about half of the erases (501,875 of 1,000,000) no case ran at all: the node that left was red, or black with a red child.
- Total rotations are almost the same for the two trees on random data. The difference is in the **worst case**, not the average.
- The rotation histogram of the red-black erases was 742,221 with none, 143,436 with one, 108,941 with two, and 5,402 with three.
- **Case 2 dominates** (513,347 recolourings), as expected: most repairs fuse nodes and move the extra black up, which is only a colour change.
- Erasing in **sorted order** is harder: after the same random build, erasing in ascending order took 681,207 rotations and in descending order 681,325 (0.68 per erase), still with at most 3 in one erase. The number of case 2 repairs was 513,347 in all three orders; I did not look for the reason.

**The worst case.** In Topic 10 the sparsest AVL tree of height 15 (1,596 keys) needed 7 rotations to erase its worst key (the program of this tutorial re-measured the same figure over all its keys). I built red-black trees over the same 1,596 keys with 40 different insertion orders and erased every key from each: the most rotations in any single erase was **3**. This is the real advantage of red-black erase: a bounded number of rotations, whatever the shape, which matters when a rotation is expensive (for example when it happens under a lock or with a lot of augmented data to update, as in the interval trees of Topic 30).

The cost of the bound is the shape: the height limit is `2 log₂(n + 1)` instead of `1.44 log₂ n` (Topics 10 and 11).

---

## 8. AVL or red-black?

All three trees below held 1,000,000 `int` keys. The timings come from this machine, best of 3 runs of 2,000,000 operations. The red-black tree here carries an `int` value as well, so its node is larger (40 bytes against the AVL node's 24; 48 against 32 bytes of heap per element), which favours AVL in the lookup column.

| | AVL | red-black (this class) | `std::set<int>` |
|---|---|---|---|
| lookups, mean | 1035.7 ns | 1247.9 ns | 1437.8 ns |
| churn: erase a random key, insert a random one | 3428.2 ns | 3011.9 ns | 2201.7 ns |
| rotations per erase+insert pair | 0.93 | 0.78 | |

A second run gave the same order in both rows (lookups 1097.3 / 1334.8 / 1578.5 ns, churn 3687.2 / 3430.6 / 2295.4 ns). What the numbers say, and do not say:

- The AVL tree looked up faster than the red-black tree in every run, by about 17 to 18%, with the caveat above about node size; in Topic 10 the *mean depth* of the two trees was equal, so this is not the "better balance" of folklore.
- The red-black tree churned faster than the AVL tree, by 7 to 12%, and with 16% fewer rotations per pair (0.78 against 0.93), consistent with the bounded-erase argument.
- `std::set` was clearly the fastest in churn (27% to 33% less time than this class in the two runs) and the slowest in lookups. It runs the same algorithm (section 6) in the same tree shape, so the difference lies elsewhere. My class computes the successor before every unlink, compares twice per level on insert, and `find` always descends to a leaf; I did not test which of these matters, so I cannot tell you why.
- Margins of 10% to 30% on one machine are not a basis for a decision on their own.

A summary that holds up against the measurements:

| Situation | Choice |
|---|---|
| a static set, built once and queried a lot | a sorted array with binary search, or a B-tree; a binary tree loses on cache behaviour (Topic 9: `std::set` lookup 6 times slower than a sorted vector) |
| a library container, ordered iteration, stable iterators | red-black: what `std::set` and `std::map` are, node for node |
| bounded work per erase (a lock held, augmented data), constant updates | red-black: at most 3 rotations, whatever the shape |
| an unbalanced build order such as sorted input, with few erases | either; AVL gives the flatter tree (height 20 against 37 for a million sorted keys, Topics 10 and 11), red-black costs the same per insert |
| average lookup cost | no measurable difference in mean depth; look at node size and layout first |

---

## 9. Common mistakes

1. **Reading the colour of the wrong node.** The decision to repair depends on the colour of the node that physically left (`z`, or the successor `y` in its *old* place), read before `y` is recoloured.
2. **Forgetting to give the successor `z`'s colour.** The position of `z` keeps its black height only if `y` takes the colour.
3. **Copying the successor's key into `z`** instead of relinking: iterators and pointers to the successor now point to a different key, and a held iterator to `z` would silently show another key. The stability test of section 6 exists for this.
4. **Reading `x->p` for a null `x`.** Pass the parent separately.
5. **Using the sibling's old pointer after a rotation.** After case 1 or case 3, `w` and `far` must be read again.
6. **Case 3 recolouring in the wrong order**, or rotating `w` toward `x` instead of away from it.
7. **Stopping after case 1.** Case 1 only makes the sibling black; the extra black is still there.
8. **Forgetting `if (x) x->red = false`** at the end: a red `x` (including `x == root` after case 2 climbs to it) must become black.
9. **Computing the next iterator after the unlink** (or, with key copying, not at all): `erase(iterator)` must return the successor, which is determined before the structure changes.
10. **Believing erase is amortised constant.** The recolourings (case 2) are at most one per level; the rotations are at most three. Both statements are about one erase.

---

## 10. Questions to answer out loud

1. When does an erase need no repair at all? Which node's colour decides?
2. What does "double black" mean? Where does the extra black sit, and what is it in the 2-3-4 tree?
3. Why is the sibling never null in the repair?
4. Which cases repeat, which end the repair, and why does that give at most three rotations?
5. What are cases 2, 3 and 4 in the 2-3-4 tree?
6. Why does `eraseNode` pass `xp` separately?
7. Why relink the successor and not copy its key?
8. Name one measured difference between AVL and red-black erase, and one that is not a difference.
9. Why do the measurements not support "AVL is better for lookups"?

---

## 11. Problems

**A. Trace.** Insert 1, 2, ..., 8, then erase 1, 2, 3, 4 (section 4) and continue with 5, 6, 7, 8. Give the cases at each step and the total number of rotations of the whole sequence.

**B. Cases.** For each of the five trees of the second table in section 4, name the node that is erased and describe its repair in the 2-3-4 tree.

**C. Proof.** Prove that an erase performs at most three rotations.

**D. Red nodes.** Prove that erasing a red node, and erasing a black node with a red child, never needs a rotation.

**E. Code.** Add `iterator erase(iterator first, iterator last)` to the class and say what it returns. Then add `std::size_t eraseRange(const K& lo, const K& hi)` that removes all keys in `[lo, hi)`. What is its cost in terms of the number of keys removed?

**F. Choosing.** Pick a container for each case and defend it with a measurement from this tutorial or the earlier ones: (a) a dictionary of 1,000,000 words built once and queried; (b) a timer queue with constant insertion and removal of events; (c) a structure inside a kernel path where the work of one update must be bounded.

**G. Why `xp`.** Give the smallest tree in which `x` is null and the code would crash if it read `x->p`.

**H. Why re-read.** In case 3, what would go wrong if `far` were not re-read after the rotation?

---

## Answers

**A.**

| Erase | Cases | Rotations | Preorder after |
|---|---|---|---|
| 1 | 2 | 0 | 4B 2B 3R 6R 5B 7B 8R |
| 2 | none (black node with a red child) | 0 | 4B 3B 6R 5B 7B 8R |
| 3 | 1, 2 | 1 | 6B 4B 5R 7B 8R |
| 4 | none (black 4 has a red child 5) | 0 | 6B 5B 7B 8R |
| 5 | 4 | 1 | 7B 6B 8B |
| 6 | 2 | 0 | 7B 8R |
| 7 | none (black root with a red child) | 0 | 8B |
| 8 | none (the last node) | 0 | empty |

The erase phase used 2 rotations; the 8 inserts before it had used 4.

**B.** The erased key is 1 in all five.

1. `2B 1B 3B 4R`, 2-3-4 tree `(2 (1) (3 4))`: the sibling 3 is black with a red far nephew 4: case 4. The empty node borrows through the parent: 3 goes up, 2 comes down, giving `(3 (2) (4))`.
2. `2B 1B 4B 3R`: the same 2-3-4 tree, but the 3-node `(3 4)` is stored with the red key on the near side: case 3 rotates it into the shape of row 1, then case 4 finishes.
3. `2B 1B 4R 3B 5B 6R`, `(2 4 (1) (3) (5 6))`: the sibling 4 is red, a key of the parent node `(2 4)`: case 1 rotates it out, and the new sibling is 3, a 2-node with no spare key. Case 2 fuses `(1)`, the key 2 and `(3)` into `(2 3)`; the parent `xp` (the key 2's node) is red after case 1, so the repair ends. Result `(4 (2 3) (5 6))`.
4. `2B 1B 5R 3B 4R 6B`, `(2 5 (1) (3 4) (6))`: the sibling 5 is red, case 1; the new sibling is the 3-node `(3 4)` with a red far nephew 4: case 4 borrows. Result `(3 5 (2) (4) (6))`.
5. `2B 1B 5R 4B 3R 6B`, the same 2-3-4 tree with the 3-node stored the other way: case 1, then 3, then 4, with 3 rotations in all, the maximum.

**C.** Case 2 never rotates and is the only case that repeats. Case 4 rotates once and ends the loop. Case 3 rotates once and is followed by case 4. Case 1 rotates once and is followed by one of cases 2, 3 or 4: after case 1 the parent `xp` is red, so case 2 ends the loop immediately (it makes `xp` black), and 3 and 4 behave as before. The rotations in one erase are therefore at most case 1 (one), case 3 (one) and case 4 (one): three. Together with the relinking of the successor (no rotation), an erase does at most 3.

**D.** *Red node.* By rule 4 a red node `z` has black children and a black parent. Cut it out and put the child that replaces it (black or null) in its place: the parent is black, so no red node has a red child, and the black count on every path is unchanged, since `z` was not counted. Rules 4 and 5 hold, and no rotation runs. With two children the successor takes `z`'s colour and the node that physically leaves is the successor's old position: the argument applies to the colour of the node that left.

*Black node with a red child `c`.* Every path through the position loses one black node, and `c` is red, so recolouring `c` black gives each of those paths back exactly one. Rule 4 holds because `c`'s children are black (it was red) and its new parent, the old parent of the removed node, is either black or red, and a black `c` under either is fine. No rotation.

**E.** `erase(first, last)` loops `while (first != last) first = erase(first);` and returns `last` (still valid, since only the erased elements are relinked or freed; `last` is never erased). `eraseRange(lo, hi)` is `erase(lower_bound(lo), lower_bound(hi))` and returns the old size minus the new. Each erase costs `O(log n)` at worst, so removing `m` keys costs `O(m log n)`; one descent for the start is `O(log n)`.

**F.** (a) A sorted vector: Topic 9 measured the sorted vector at 184.2 ns per lookup against 1122.6 ns for `std::set`; a hash table was 40.9 ns if order is not needed. (b) A red-black tree (or a heap, if only the minimum is needed): in the churn measurements it was 7% to 12% faster than AVL, with fewer rotations per pair; a binary heap or a timing wheel is the usual better answer, not measured here. (c) Red-black: at most three rotations per erase whatever the shape, against 7 in the sparsest AVL tree of 1,596 keys; this is the argument for the kernel's choice (Topic 13).

**G.** The tree `2B 1B 3B 4R` of section 4: erase 1, a black leaf. The node that takes its place is the null left link of 2, so `x` is null and `xp` is the node 2. The code compares `x == xp->l`, which is true because both are null, and then uses `xp->r` as the sibling; reading `x->p` would dereference a null pointer.

**H.** Case 3 rotates `w` away from `x`. The old near nephew becomes the sibling and the old `w` becomes its far child. A stale `far` still names the node that was the old far nephew, which is now black or null (the situation of case 3 was that it was black) and no longer a child of the sibling. Colouring it black and rotating `xp` would leave the new far nephew red and the black count short; the checker fails on it, and one of the mutants in `verify.py` removes the re-read to prove the tests notice.

---

## Cheat sheet

- Erase relinks the successor (no key copy), which takes the colour of the node it replaces. If the node that left was red: done. If black with a red child: recolour the child black. Otherwise the child `x` carries a double black.
- Double black = an empty node in the 2-3-4 tree. Borrow from a sibling with a spare key (cases 3 and 4) or fuse with it and the parent key (case 2); case 1 only turns a red sibling into a black one.
- Case 2 is the only one that repeats; at most 3 rotations per erase (1 + 1 + 1), any number of recolourings (at most one per level). Measured: at most 3 in all 533,417 small runs and in every large run.
- On 1,000,000 random erases: 0.38 rotations per erase for red-black, 0.37 for AVL; worst single erase 3 against 8; the sparsest AVL tree needs 7 where red-black trees of the same keys need at most 3.
- The class matches the libstdc++ `std::set` node for node under inserts and erases (200 sequences).
- Lookups: AVL 1035.7 ns, this class 1247.9 ns, `std::set` 1437.8 ns; churn: 3428.2, 3011.9, 2201.7 ns. Margins are small and I did not find the cause of `std::set`'s churn speed.
- Choose by erase bound and library support, not by mean depth: it is equal.

---

## Sources

- The cases and the structure of the code follow the usual textbook presentation of red-black deletion (CLRS, from memory); the code was checked as described at the top, not against the book.
- libstdc++: the header `bits/stl_tree.h` of the GCC 13 installation used for the tests, through the iterator's node pointer, for the comparison of section 6.
- The AVL numbers (rotations per erase, the sparsest tree) are from Topic 10, re-measured here on the same keys and order.
- Marked "from memory" in the text: the source of the textbook cases, and the claim about interval trees in Topic 30, which is written ahead of that topic.
- All other claims in this tutorial come from the tests and measurements described at the top.
