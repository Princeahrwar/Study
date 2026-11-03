# DSA 07.11: Red-Black Trees I: The Five Rules, the 2-3-4 Tree Behind Them, and Insertion

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (3.5 reading, 5.5 problems) · You need: [Rotations and AVL trees](10_rotations_and_avl_trees.md) (the rotation, the height arguments), [Inside `std::set` and `std::map`](09_inside_std_set_and_map.md) (the red-black tree you already use, and the checker idea) and [BST core operations](07_bst_core_operations.md). Erase is Topic 12, which also gives the complete class. There is no companion demo file: every listing is tested code in this file.*

By the end of this tutorial you can:

- state the red-black rules, prove that they keep the height below `2 log₂(n + 1)`, and say how that compares with AVL;
- read a red-black tree as a 2-3-4 tree and write a 2-3-4 tree as a red-black tree, so that every rule and every repair case has a meaning;
- write insertion with its three fix-up cases (red uncle, bend, line), trace it by hand and explain which cases can repeat and which end the repair;
- explain why a new node is red, why an insert does at most two rotations, and how many recolorings to expect;
- write Sedgewick's left-leaning variant in a few lines and say what it gains and what it costs;
- say, with evidence, that the insertion code of this tutorial builds the same trees as the libstdc++ `std::set` you measured in Topic 9.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 5,208,189 checks passed under AddressSanitizer with UBSan (GCC 13.3, libstdc++ 13). The tests built every insertion order of 1 to 9 keys (409,113 orders) and checked all red-black rules after every insert; compared the trees node for node and colour for colour with the libstdc++ `std::set` on 200 insertion sequences; compared the 2-3-4 reading with an independently written 2-3-4 tree on every insertion order of 1 to 8 keys (46,233 orders) and on 300 longer random sequences; ran 100 random sequences against `std::set`; tried four hostile orders up to 131,071 keys; checked the left-leaning variant the same way; and checked that the invariant checker fails when a colour, a parent link or a key is damaged by hand. A Python implementation written separately produced the traces and the worked answers, and agreed with the C++ on 300 random sequences. The sizes, counts, heights and shapes below are program output. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, glibc malloc, best of 3 on 1,000,000 distinct random `int` keys; read them as shapes, not benchmarks. Claims about history and other sources are marked "from memory" or point to a source.

---

## 1. The rules

A **red-black tree** is a binary search tree in which every node is coloured red or black and these rules hold:

1. every node is red or black;
2. the root is black;
3. the empty links (the "leaves" of CLRS) count as black;
4. a red node has no red child;
5. every path from a node down to an empty link passes the same number of black nodes (the **black height**).

The data layout is that of Topic 9: a key, a colour and three pointers (`l`, `r`, and a parent `p`, which the repair loop climbs). The tree below uses `int` keys and 32 bytes per node.

```cpp
struct Node {
    int key;
    bool red = true;                   // a new node is always red
    Node *l = nullptr, *r = nullptr, *p = nullptr;
    explicit Node(int k) : key(k) {}
};
inline bool isRed(const Node* n) { return n && n->red; }      // null links count as black

inline long recolors = 0, bends = 0, lines = 0;               // instrumentation: how often each case runs
```

The code below implements rules 2, 4 and 5 as a checker. It also checks that every parent link points back and that the key order is right, since a repair that rotates can break either without touching a colour. The black height here counts the black nodes on a path including the node itself (so a lone black root has black height 1).

```cpp
// Returns the black height of t (black nodes on any path down to a null link),
// or -1 if a rule is broken. Counts nodes into n. lo and hi bound the keys.
inline int check(const Node* t, const Node* parent, const int* lo, const int* hi, std::size_t& n, int& height, int depth = 1) {
    if (!t) { height = std::max(height, depth - 1); return 0; }
    if (t->p != parent) return -1;                            // parent links must agree
    if (lo && !(*lo < t->key)) return -1;                     // property: binary search tree order
    if (hi && !(t->key < *hi)) return -1;
    if (t->red && (isRed(t->l) || isRed(t->r))) return -1;    // property 4: no red node has a red child
    int a = check(t->l, t, lo, &t->key, n, height, depth + 1);  if (a < 0) return -1;
    int b = check(t->r, t, &t->key, hi, n, height, depth + 1);  if (b < 0) return -1;
    if (a != b) return -1;                                    // property 5: equal black height on both sides
    ++n;
    return a + (t->red ? 0 : 1);
}

struct Report { bool ok; int blackHeight; int height; std::size_t nodes; };
inline Report inspect(const Tree& T) {
    Report r{false, 0, 0, 0};
    if (!T.root()) { r.ok = T.size() == 0; return r; }
    if (T.root()->red) return r;                              // property 2: the root is black
    r.blackHeight = check(T.root(), nullptr, nullptr, nullptr, r.nodes, r.height);
    r.ok = r.blackHeight >= 0 && r.nodes == T.size();
    return r;
}
```

---

## 2. Why the rules bound the height

Let `bh(x)` be the black height of a node `x`, counting `x` if it is black, with `bh(null) = 0`.

**Claim 1: the subtree under `x` has at least `2^bh(x) - 1` nodes.** For an empty subtree this is 0. Otherwise each child has black height `bh(x)` (if `x` is red) or `bh(x) - 1` (if `x` is black), at least `bh(x) - 1` in both cases, and by induction each side has at least `2^(bh(x) - 1) - 1` nodes. Adding both sides and `x` gives at least `2 (2^(bh(x) - 1) - 1) + 1 = 2^bh(x) - 1`.

For the root this gives `n >= 2^b - 1`, so the black height is `b <= log₂(n + 1)`.

**Claim 2: the height is at most `2b`.** Along any path, a red node is followed by a black one (rule 4) and the root is black (rule 2), so at least half the nodes of every path are black, and the number of black nodes on a path is `b` (rule 5). So no path has more than `2b` nodes.

Together, **`height <= 2 log₂(n + 1)`**. For 1,000,000 keys that is 39.86, so at most 39. The measured heights are below it: 25 for random insertion and 37 for sorted insertion (section 8). The AVL limit was 28 for the same `n`, so the red-black rules are looser. How much looser? Ask for the fewest nodes a tree of height `h` can have, as in Topic 10. A Python search over all colourings and shapes (exhaustive by construction) gives:

| Height `h` | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| fewest nodes, red-black | 1 | 2 | 4 | 6 | 10 | 14 | 22 | 30 | 46 | 62 | 94 | 126 |
| fewest nodes, AVL (`N(h)`) | 1 | 2 | 4 | 7 | 12 | 20 | 33 | 54 | 88 | 143 | 232 | 376 |

The red-black row doubles every two heights (6, 14, 30, 62, 126, so `2^(h/2 + 1) - 2` for even `h`), the AVL row grows by the golden ratio each height. A tree of a given size can therefore be taller as a red-black tree. This is not only a bound: the smallest example is **6 keys inserted in ascending order**, which builds a red-black tree of height 4 where no AVL tree can exceed 3. Over all insertion orders of 1 to 9 keys, the greatest height reached is:

| n | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| perfect: `ceil(log₂(n + 1))` | 1 | 2 | 2 | 3 | 3 | 3 | 3 | 4 | 4 |
| AVL maximum | 1 | 2 | 2 | 3 | 3 | 3 | 4 | 4 | 4 |
| red-black, greatest over all insertion orders | 1 | 2 | 2 | 3 | 3 | 4 | 4 | 4 | 4 |

The tree for 1, 2, 3, 4, 5, 6 has preorder `2B 1B 4R 3B 5B 6R` (key and colour): the path 2, 4, 5, 6 has four nodes, with the red 4 and 6 sitting between black ones.

---

## 3. The 2-3-4 tree behind the rules

A **2-3-4 tree** is a search tree whose nodes hold 1, 2 or 3 keys (so 2, 3 or 4 children), with all leaves at the same depth. It is perfectly balanced by construction, and it grows by splitting: a node that would receive a fourth key splits around its middle key, and that key moves up to the parent (a split of the root grows the tree by one level, the only way the depth changes).

A red-black tree is a way to store a 2-3-4 tree in binary nodes. Glue each black node to its red children:

| 2-3-4 node | Red-black encoding |
|---|---|
| 2-node (1 key) | a black node |
| 3-node (2 keys) | a black node with **one** red child, on either side (two shapes) |
| 4-node (3 keys) | a black node with **two** red children |

The rules now read as sentences about the 2-3-4 tree:

- **Rule 5 (black height) is "all leaves are at the same depth"**: the black height of a node is the depth of its 2-3-4 node below it.
- **Rule 4 (no red-red) is "a node is one of the three shapes"**: a red child belongs to its black parent's node, so a red child of a red node would chain three binary nodes into one 2-3-4 node of a shape that is not allowed.
- **Rule 2 (black root)** only fixes the colour of the top key of the root's node.
- A **rotation** that swaps which key of a 3-node is on top, or turns a bend into a line, leaves the 2-3-4 tree unchanged.
- The height bound is now easy to see: a 2-3-4 tree of depth `d` has at least `2^d - 1` keys, and each 2-3-4 node costs at most 2 binary levels (a black node and one red child), so `height <= 2d`.

The function below prints a red-black tree as its 2-3-4 tree, keys first and children after, so `(10 20 (5) (15) (25))` is a 3-node with keys 10 and 20 and three one-key children:

```cpp
// Read the tree as a 2-3-4 tree: each black node together with its red children is one
// 2-, 3- or 4-node. Prints keys first, then the children, e.g. "(10 20 (5) (15) (25))".
inline std::string show234(const Node* b) {
    std::vector<int> keys;
    std::vector<const Node*> kids;
    if (isRed(b->l)) { kids.push_back(b->l->l); keys.push_back(b->l->key); kids.push_back(b->l->r); }
    else kids.push_back(b->l);
    keys.push_back(b->key);
    if (isRed(b->r)) { kids.push_back(b->r->l); keys.push_back(b->r->key); kids.push_back(b->r->r); }
    else kids.push_back(b->r);
    std::string s = "(";
    for (std::size_t i = 0; i < keys.size(); ++i) s += (i ? " " : "") + std::to_string(keys[i]);
    for (const Node* k : kids) if (k) s += " " + show234(k);
    return s + ")";
}
```

To test that the reading is not just a pretty story, the code also includes an independent 2-3-4 tree with the textbook insertion: put the key into a leaf; a node that now has four keys splits around the **middle of the three keys it had before**, that key is inserted into the parent, and so on up.

```cpp
// An independent 2-3-4 tree with bottom-up insertion. A node that would get a fourth key
// splits around the middle of the three keys it already had; that key moves up.
struct N234 {
    std::vector<int> k;
    std::vector<N234*> c;                                     // empty for a leaf
};
struct Up { bool split = false; int mid = 0; N234* right = nullptr; };

inline Up ins234(N234* n, int key) {
    std::size_t i = 0;
    while (i < n->k.size() && n->k[i] < key) ++i;
    if (i < n->k.size() && n->k[i] == key) return {};
    int up = key;
    N234* upRight = nullptr;
    if (!n->c.empty()) {
        Up u = ins234(n->c[i], key);
        if (!u.split) return {};
        up = u.mid;
        upRight = u.right;
    }
    bool full = n->k.size() == 3;
    int oldMid = full ? n->k[1] : 0;
    n->k.insert(n->k.begin() + i, up);
    if (upRight) n->c.insert(n->c.begin() + i + 1, upRight);
    if (!full) return {};
    std::size_t m = (i <= 1) ? 2 : 1;                         // where the old middle key sits among the four
    N234* right = new N234;
    right->k.assign(n->k.begin() + m + 1, n->k.end());
    n->k.resize(m);
    if (!n->c.empty()) {
        right->c.assign(n->c.begin() + m + 1, n->c.end());
        n->c.resize(m + 1);
    }
    return {true, oldMid, right};
}

inline N234* insert234(N234* root, int key) {
    if (!root) { root = new N234; root->k.push_back(key); return root; }
    Up u = ins234(root, key);
    if (!u.split) return root;
    N234* top = new N234;
    top->k.push_back(u.mid);
    top->c = {root, u.right};
    return top;
}

inline std::string show(const N234* n) {
    std::string s = "(";
    for (std::size_t i = 0; i < n->k.size(); ++i) s += (i ? " " : "") + std::to_string(n->k[i]);
    for (const N234* c : n->c) s += " " + show(c);
    return s + ")";
}
inline void destroy(N234* n) { if (n) { for (N234* c : n->c) destroy(c); delete n; } }
```

The tests inserted the same sequence into both. For all 46,233 orders of 1 to 8 keys and for 300 random sequences of up to 1,500 inserts, the printed 2-3-4 tree of the red-black code is identical to the string of the reference. So the red-black insertion of section 4 **is** bottom-up 2-3-4 insertion, key for key. It is also why it works: the cases of the repair are the three things that can happen to a node in that tree.

---

## 4. Insertion

The new node is inserted as in Topic 7 and coloured **red**. The reason: a red node changes no black height (rule 5 stays true everywhere), and it can break only rule 4, when its parent is also red. A black new node would add one black node to a single path, which breaks rule 5 for every other path and cannot be repaired locally. In 2-3-4 terms, a new red node is a new key added to an existing node.

If the parent is black, the key joined a 2-node or a 3-node and we are done. If the parent `p` is red, we have two reds in a row; `p` is not the root (rule 2), so the grandparent `g` exists and is black. Look at the uncle `u`, the other child of `g`:

| Case | The picture | In the 2-3-4 tree | Repair | Continues? |
|---|---|---|---|---|
| 1. `u` is red | `g` black with two red children `p`, `u`, and the red `z` below `p` | a 4-node `{p, g, u}` that just received a fourth key | recolor: `p` and `u` black, `g` red; set `z = g` | **yes**: `g` is red now, and its own parent may be red |
| 2. `u` black, `z` the inner grandchild | a bend: `g`, `p`, `z` do not lie in a line | a 3-node `{g, p}` that received a key between them | rotate `p` so that the bend becomes a line; now case 3 | no |
| 3. `u` black, `z` the outer grandchild | a line: `g`, `p`, `z` | a 3-node that received a third key | recolor `p` black and `g` red, rotate `g` toward `u`: `p` is now the top key with two red children | **no**, the black height and the colour of the subtree's top are as before |

Case 1 is the split. The middle key `g` moves up (turns red, which means "joins the parent's node"), and the problem moves with it, up two levels. If it reaches the root, the root is recoloured black at the end, which is the split of the root: the tree grew by one black level. Cases 2 and 3 do not split anything, they only put the middle key on top, and they end the repair. A repair is therefore any number of case 1 steps, followed by at most one case 2 and one case 3: **at most two rotations per insert**. Every case has a mirror image; the code uses one flag to handle both.

The rotation of Topic 10, with parent pointers, written once for both directions:

```cpp
// toLeft: left rotation at x (x->r moves up); otherwise a right rotation (x->l moves up).
inline void rotate(Node*& root, Node* x, bool toLeft) {
    Node* Node::* down = toLeft ? &Node::r : &Node::l;        // the child that moves up
    Node* Node::* up   = toLeft ? &Node::l : &Node::r;        // the side x moves to
    Node* y = x->*down;
    x->*down = y->*up;                                        // the middle subtree changes parent
    if (y->*up) (y->*up)->p = x;
    y->p = x->p;                                              // y takes x's place under x's parent
    if (!x->p) root = y;
    else if (x == x->p->l) x->p->l = y;
    else x->p->r = y;
    y->*up = x;
    x->p = y;
}
```

The repair loop:

```cpp
// z is a new red node. Repeat while its parent is red (two reds in a row).
inline void insertFixup(Node*& root, Node* z) {
    while (isRed(z->p)) {
        Node* p = z->p;
        Node* g = p->p;                                       // exists: the root is black, so a red p is not the root
        bool pLeft = (p == g->l);
        Node* u = pLeft ? g->r : g->l;                        // the uncle
        if (isRed(u)) {                                       // case 1: red uncle, recolor, continue upward
            p->red = false;
            u->red = false;
            g->red = true;
            z = g;
            ++recolors;
        } else {                                              // black uncle: at most two rotations, then done
            if (z == (pLeft ? p->r : p->l)) {                 // case 2: z is the inner grandchild, a bend
                z = p;
                rotate(root, z, pLeft);                       // straighten it into a line
                p = z->p;
                ++bends;
            }
            p->red = false;                                   // case 3: z is the outer grandchild, a line
            g->red = true;
            rotate(root, g, !pLeft);
            ++lines;
        }
    }
    root->red = false;
}
```

And the class around it, with the descent of Topic 7:

```cpp
class Tree {
    Node* root_ = nullptr;
    std::size_t n_ = 0;
    static void destroy(Node* t) { if (t) { destroy(t->l); destroy(t->r); delete t; } }
public:
    Tree() = default;
    Tree(const Tree&) = delete;
    Tree& operator=(const Tree&) = delete;
    ~Tree() { destroy(root_); }

    bool insert(int k) {
        Node *p = nullptr, *x = root_;
        while (x) {
            p = x;
            if (k < x->key) x = x->l;
            else if (x->key < k) x = x->r;
            else return false;                                // duplicate
        }
        Node* z = new Node(k);
        z->p = p;
        if (!p) root_ = z;
        else if (k < p->key) p->l = z;
        else p->r = z;
        ++n_;
        insertFixup(root_, z);
        return true;
    }
    bool contains(int k) const {
        for (const Node* t = root_; t; t = k < t->key ? t->l : t->r)
            if (!(k < t->key) && !(t->key < k)) return true;
        return false;
    }
    std::size_t size() const { return n_; }
    const Node* root() const { return root_; }
    Node*& rootRef() { return root_; }                        // for tests that damage or rotate the tree
};
```

Notes on the code:

- `isRed(z->p)` is false for a null parent, so the loop ends at the root; and a red `p` always has a parent, so `g` is never null.
- After the bend rotation, `z` and `p` swap roles: `z = p` means "the node that is now the lower one", and `p = z->p` is the node that moved up.
- In case 3, `rotate(root, g, !pLeft)`: when `p` is a left child, the line leans left and `g` rotates **right**.
- The loop ends with `root->red = false`, which is the only place the root rule needs enforcing.

---

## 5. A trace

Insert 1, 2, 3, 4, 5, 6, 7 into an empty tree. Colours are written after the key; the preorder lists the root first.

| Insert | Case | Preorder after | The 2-3-4 tree |
|---|---|---|---|
| 1 | root, coloured black | 1B | (1) |
| 2 | black parent, nothing | 1B 2R | (1 2) |
| 3 | red parent 2, black uncle: **line**, rotate left at 1 | 2B 1R 3R | (1 2 3) |
| 4 | red parent 3, red uncle 1: **recolor** | 2B 1B 3B 4R | (2 (1) (3 4)) |
| 5 | red parent 4, black uncle: **line**, rotate left at 3 | 2B 1B 4B 3R 5R | (2 (1) (3 4 5)) |
| 6 | red parent 5, red uncle 3: **recolor** (the 4-node splits, 4 goes up) | 2B 1B 4R 3B 5B 6R | (2 4 (1) (3) (5 6)) |
| 7 | red parent 6, black uncle: **line**, rotate left at 5 | 2B 1B 4R 3B 6B 5R 7R | (2 4 (1) (3) (5 6 7)) |

The right-hand column is exactly what you would draw if you inserted 1 to 7 into a 2-3-4 tree on paper: the node `(3 4 5)` after the fifth insert is a 4-node, the sixth key forces it to split with 4 moving up, and so on. Step 4 is the one to study: inserting 4 under the red 3 (whose sibling 1 is red) has a red uncle, so the 4-node `{1, 2, 3}` splits; 2 is the root, so it ends black, and the tree is now two levels of black deep.

A sorted insert does a rotation almost every time and a split about every other time; the counters of the next section say exactly how often.

---

## 6. What the repair costs

Counted by the instrumented code on 1,000,000 inserts:

| Insertion order | Recolorings (case 1) | Bends (case 2) | Lines (case 3) | Inserts that recolored | Inserts that rotated |
|---|---|---|---|---|---|
| random | 513,225 | 194,036 | 387,778 | 342,798 | 387,778 |
| ascending | 999,957 | 0 | 999,963 | 499,999 | 999,963 |

- **At most two rotations per insert**, as the case table says; a random insert did at most 2 and an ascending one at most 1 (the tests agree over all 409,113 orders of up to 9 keys).
- **Recolorings are not bounded by a constant per insert**: the longest chain of case 1 in one insert was 11 (random) and 18 (ascending), roughly one per level of black height. But they are cheap, and on average they are few: 0.51 per random insert and 1.0 per ascending insert. Over a long run the total is linear in the number of inserts (a known amortised result, from memory; the counts above are consistent with it).
- A random insert rotates about 39% of the time (387,778 of 1,000,000 inserts) and recolors about 34% of the time (342,798).

Compare with AVL (Topic 10): both do at most two rotations per insert, AVL does at most one repair, red-black can recolor up the tree. The real difference between them is the height limit (section 2) and the erase, which is Topic 12.

---

## 7. The left-leaning variant

Sedgewick's left-leaning red-black tree (2008; the paper is listed under Sources) restricts the encoding of 3-nodes to **one orientation**: a red link always leans left. A 3-node then has one shape, 4-nodes have two red children, and the insertion shrinks to three tests after the recursive call, applied on the way back up:

```cpp
// Sedgewick's left-leaning red-black tree: red links lean left, so a 3-node has one shape.
struct Node {
    int key;
    bool red = true;
    Node *l = nullptr, *r = nullptr;
};
inline bool isRed(const Node* h) { return h && h->red; }
inline Node* rotL(Node* h) { Node* x = h->r; h->r = x->l; x->l = h; x->red = h->red; h->red = true; return x; }
inline Node* rotR(Node* h) { Node* x = h->l; h->l = x->r; x->r = h; x->red = h->red; h->red = true; return x; }
inline void flip(Node* h) { h->red = !h->red; h->l->red = !h->l->red; h->r->red = !h->r->red; }

inline Node* insert(Node* h, int k, bool& added) {
    if (!h) { added = true; return new Node{k}; }
    if (k < h->key)      h->l = insert(h->l, k, added);
    else if (h->key < k) h->r = insert(h->r, k, added);
    else return h;
    if (isRed(h->r) && !isRed(h->l)) h = rotL(h);             // a red link leaning right: lean it left
    if (isRed(h->l) && isRed(h->l->l)) h = rotR(h);           // two reds in a row on the left: make it a 4-node
    if (isRed(h->l) && isRed(h->r)) flip(h);                  // a 4-node: split it, pass the middle key up
    return h;
}
```

1. a red right child with no red left sibling: rotate it left (lean it);
2. two reds in a row on the left: rotate right (this makes a 4-node);
3. two red children: flip the colours (split the 4-node, pass the middle key up).

The caller sets the root black after each insert. The version above splits a 4-node as soon as it forms, so no node is ever left with two red children (the tests checked this after random inserts), which means it encodes a **2-3 tree** rather than a 2-3-4 tree. The 2-3-4 version keeps 4-nodes; Wikipedia's summary says the two versions differ by one line (I recall that the line is the colour flip, moved before the descent; I did not test that version).

It is short, it needs no parent pointers and no mirror cases, and the tests confirmed all its rules. The measured cost:

| 1,000,000 keys | Height | Mean depth |
|---|---|---|
| left-leaning, random | 28 | 18.71 |
| left-leaning, ascending | 20 | 17.95 |
| left-leaning, descending | 26 | 18.27 |
| classic (section 4), random | 25 | 18.43 |
| classic, ascending or descending | 37 | 18.33 |

Both variants obey the `2 log₂(n + 1)` limit (39.86), and neither dominates: the left-leaning tree is taller for random input (28 against 25), the classic one is much taller for sorted input (37 against 20 for ascending; 26 for descending). The left-leaning tree is mostly valuable as the shortest way to see the 2-3 tree in code; the libraries use the classic form (Topic 9), and so does the Linux kernel's rbtree (Topic 13). Deletion for the left-leaning form is not covered in this tutorial.

---

## 8. It is the tree `std::set` builds

The insertion above is the textbook algorithm, and the libstdc++ `std::set` uses a red-black tree (Topic 9). The tests compared them directly: 200 sequences of up to 400 keys (ascending, descending, random with many duplicates, random over a wide range) were inserted into this `Tree` and into a `std::set<int>`, and the preorder with colours was compared after every sequence. **All 200 were identical, node for node and colour for colour.** Everything in this tutorial is therefore what happens when you call `std::set<int>::insert` (while only inserting; erase is Topic 12).

Measured on 1,000,000 distinct random keys:

| | This `Tree` | `std::set<int>` |
|---|---|---|
| node request, bytes | 32 | 40 |
| heap bytes per element (glibc) | 48.0 | 48.0 |
| height (random / ascending) | 25 / 37 | 25 / 37 |
| mean depth (random / ascending) | 18.43 / 18.33 | 18.43 / 18.33 |
| build time (insert all) | 874.3 ms | 815.1 ms |
| lookup, mean per key | 1040.2 ns | 1222.3 ns |

The shapes are the same, the heap use is the same, and the times differ by 7% (build, slower here) and 15% (lookup, faster here) in the saved run. In other runs the build gap changed sign (from 18% faster to 19% slower) and the lookup gap stayed between 9% and 27% in favour of this code, so I treat the build as a tie and I did not find the reason for the lookup gap. Since the trees are identical, it cannot come from the tree shape.

The libstdc++ numbers of Topic 9 and the AVL numbers of Topic 10 come together here:

| 1,000,000 keys, ascending insertion | Height | Mean depth |
|---|---|---|
| plain BST | 1,000,000 | about 500,000 |
| red-black (this tutorial, = `std::set`) | 37 | 18.33 |
| AVL | 20 | 17.95 |
| left-leaning red-black | 20 | 17.95 |

The mean depth, which is what an average lookup pays, differs by about 2% between the balanced trees. The height, the worst case, differs by almost a factor of two.

---

## 9. Common mistakes

1. **Colouring the new node black.** It breaks rule 5 on one path and there is no local repair. New nodes are red.
2. **Treating a black or null uncle as red.** Null links are black (rule 3): `isRed(u)` must accept null.
3. **Forgetting the bend.** If `z` is the inner grandchild and you rotate `g` directly, you move the red-red pair instead of removing it. Straighten first.
4. **Rotating the wrong way in case 3.** The rotation at `g` goes **away from the side `p` is on**: `p` left means rotate `g` right.
5. **Recoloring in the wrong order in case 3.** `p` becomes black and `g` red **before** the rotation; afterwards `p` is the top of the subtree. Recolor the other way round and the subtree top is red over a red child.
6. **Not continuing after case 1.** The recoloured `g` may have a red parent; the loop must restart at `g`, not stop.
7. **Forgetting `root->red = false`.** A case 1 that reaches the root colours it red.
8. **Updating the parent links incompletely in a rotation.** Three links change: the middle subtree's parent, the new top's parent, and the old top's parent; and the parent's child pointer, or the root. The checker verifies the parent links for this reason.
9. **Believing "red nodes are less important".** They are keys; the colour is only the shape of the 2-3-4 node they live in.

---

## 10. Questions to answer out loud

1. State the five rules. Which one is the hardest to keep, and why is a new node red?
2. Prove that a red-black tree with `n` nodes has height at most `2 log₂(n + 1)`.
3. How does the red-black tree encode a 3-node? How many shapes does a 3-node have, and what does the left-leaning variant do about it?
4. Which red-black rule corresponds to "all leaves of the 2-3-4 tree are at the same depth"?
5. What does case 1 do to the 2-3-4 tree? What do cases 2 and 3 do?
6. Why can the repair loop stop after case 3 and not after case 1?
7. What is the largest number of rotations one insert can do? Of recolorings?
8. The smallest red-black tree of height 4 has how many nodes, and how does that compare with AVL?
9. Is the tree that `std::set` builds from sorted input as flat as AVL's? Give the heights and say what the mean depths show.

---

## 11. Problems

**A. Trace.** Insert 41, 38, 31, 12, 19, 8 into an empty tree. For each insert give the case or cases, the preorder with colours and the 2-3-4 tree.

**B. The other direction.** Insert 10, 20, 30, 40, 50, 25 into a 2-3-4 tree by hand, splitting a node that would get a fourth key around the middle of its original three keys. Then write the red-black tree that corresponds to it and check it against section 4.

**C. Proof.** Prove that in any red-black tree the longest path from the root to a null link is at most twice as long as the shortest.

**D. The smallest tall tree.** What is the fewest nodes in a red-black tree of height 4? Draw one, and give the insertion order that builds it.

**E. Left-leaning.** Insert 1, 2, 3, 4, 5 into the left-leaning tree of section 7. Show the tree after each insert, naming which of the three tests fired.

**F. Code.** Write `bool isRedBlack(Node* root)` that checks rules 2, 4 and 5 and the key order in one recursive function that returns the black height (or -1). Say what it additionally has to check if nodes have parent pointers.

**G. Why red.** Suppose new nodes were inserted black. Give a small example in which rule 5 fails and say why no rotation or recoloring near the new node can repair it.

**H. Reading.** The 2-3-4 tree `(20 40 (10) (25 30) (50))` is given. Write the red-black tree (several are possible: list all). How many are there, and which one does the insertion of section 4 produce?

**More practice.** Red-black trees rarely appear as interview problems; the usual exercises are the ones above and the invariants checker in a coding round for a systems role. Topic 12 completes the class with erase.

---

## Answers

**A.** (Colours after each key; the 2-3-4 view in the last column.)

| Insert | Cases | Preorder | 2-3-4 tree |
|---|---|---|---|
| 41 | root | 41B | (41) |
| 38 | black parent | 41B 38R | (38 41) |
| 31 | line at 41 (rotate right) | 38B 31R 41R | (31 38 41) |
| 12 | recolor at 38 | 38B 31B 12R 41B | (38 (12 31) (41)) |
| 19 | bend at 12 (rotate left), line at 31 (rotate right) | 38B 19B 12R 31R 41B | (38 (12 19 31) (41)) |
| 8 | recolor at 19 | 38B 19R 12B 8R 31B 41B | (19 38 (8 12) (31) (41)) |

In total 2 recolorings, 1 bend and 2 lines. The last insert: 8 goes under the red 12, whose uncle 31 is red, so the 4-node `{12, 19, 31}` splits; 19 turns red (goes up into the root's node) and the root 38 stays black.

**B.** 10, 20: `(10 20)`. 30: `(10 20 30)`. 40: the node would get four keys; it had 10, 20, 30, the middle is 20, which becomes the root: `(20 (10) (30 40))`. 50: `(20 (10) (30 40 50))`. 25: it goes into `(30 40 50)` and makes four keys; the old middle is 40, which moves up: `(20 40 (10) (25 30) (50))`. The red-black tree has the preorder `20B 10B 40R 30B 25R 50B`, which is also what the Python model builds from the same inserts.

**C.** Let `b` be the black height. Every path from the root to a null link contains exactly `b` black nodes (rule 5), so the shortest path has at least `b` nodes. Every red node sits directly below a black node (the root is black and no red node has a red child), so a path with `b` black nodes has at most `b` red ones, and the longest path has at most `2b` nodes. So the longest is at most twice the shortest.

**D.** 6 nodes (table of section 2). The insertion order 1, 2, 3, 4, 5, 6 builds one: root 2 (black) with a black left child 1; its right child 4 is red with two black children 3 and 5; and 5 has a red right child 6 (preorder `2B 1B 4R 3B 5B 6R`). The longest path 2, 4, 5, 6 has height 4 and black height 2. Why not fewer nodes: height 4 with at most one red per black means the pattern black, red, black, red, so black height 2; the red node on the path needs two children of black height 1 (the second is one more node), and the root's other side also needs a black node to reach black height 2: root, red, black, red, one more black under the red, one black on the root's other side makes 6.

**E.** 1: `1B`. 2: the new red 2 leans right under 1, so test 1 fires at 1 (rotate left): `2B 1R`. 3: 3 is a red right child of 2 whose left child is red too, so test 1 does not fire; test 3 fires at 2 (flip), and the root is blackened: `2B 1B 3B`. 4: 4 is a red right child of 3, test 1 fires at 3: `2B 1B 4B 3R`. 5: 5 is a red right child of 4, whose left child 3 is red, so at 4 test 3 fires (flip: 4 red, 3 and 5 black); back at 2, the right child 4 is red and the left child 1 is black, so test 1 fires at 2 (rotate left): `4B 2R 1B 3B 5B`. The Python model and the C++ agree on every step.

**F.** `int h(Node* t, lo, hi)`: return 0 for null; fail on order violation or on a red node with a red child; compute the black heights `a` and `b` of the children; fail if `a != b`; return `a + (t is red ? 0 : 1)`. The caller also requires a black root. With parent pointers it must also check `t->l->p == t` and `t->r->p == t`, since a bad rotation can leave the colours and the keys correct and still corrupt the tree.

**G.** Take a tree whose root 1 is black with no children, and insert 2 as a black node on its right: the path through 2 has two black nodes, the empty link on the left of 1 has one, so rule 5 fails at the root. Here recolouring 2 red would repair it, which is the point: red is the only colour that cannot break rule 5. In a larger tree the black new node sits under a black parent with a black sibling subtree, and the short side is a whole subtree: no recolouring or rotation next to the new node adds a black node to the other side. The same shortage of one black node on one side is the "double black" problem of erase (Topic 12).

**H.** The node `(20 40 ...)` is a 3-node with keys 20 and 40, encoded as one of two shapes: black 20 with red right child 40, or black 40 with red left child 20. The node `(25 30)` is also a 3-node: black 25 with red 30, or black 30 with red 25. Together there are 2 x 2 = 4 red-black trees for this 2-3-4 tree. The insertion produces `20B 10B 40R 30B 25R 50B`: 40 is the red right child of 20, and 25 the red left child of 30.

---

## Cheat sheet

- Rules: red or black; black root; null links black; no red child of a red node; equal black height. Height `<= 2 log₂(n + 1)`.
- Proof: `n >= 2^bh - 1` by induction; at most half of a path is red.
- Fewest nodes for height `h`: red-black 1, 2, 4, 6, 10, 14, 22, 30 (about `2^(h/2)`), AVL 1, 2, 4, 7, 12, 20, 33, 54 (about `1.62^h`): red-black can be taller. 1 to 6 inserted ascending give height 4.
- 2-3-4 view: black node plus its red children is one node (2-, 3- or 4-node). Equal black height is equal leaf depth; no red-red is at most 3 keys per node. Tested identical to bottom-up 2-3-4 insertion.
- Insert: new node red. Black parent: done. Red parent: red uncle means recolor (split) and continue at the grandparent; black uncle means bend (rotate to a line) then line (recolor and rotate the grandparent), and stop. At most 2 rotations per insert.
- Costs (1,000,000 inserts): random 0.51 recolorings and 0.58 rotations per insert, at most 11 recolorings in one; ascending 1.0 and 1.0 (at most 18 recolorings in one).
- Sorted insert: height 37, black height 19 (AVL 20). Mean depth 18.33 against 17.95.
- Left-leaning: red links lean left; three tests; no parent pointers; 2-3 tree in the form shown; height 28 random, 20 ascending.
- This `Tree` equals `std::set<int>` node for node (200 sequences) while inserting.

---

## Sources

- The rules and the fix-up cases follow the usual textbook presentation (CLRS, from memory); the code was checked as described at the top, not against the book.
- Red-black trees come from the paper "A dichromatic framework for balanced trees" by Leo Guibas and Robert Sedgewick (FOCS 1978), listed on [a Princeton page](https://collaborate.princeton.edu/en/publications/a-dichromatic-framework-for-balanced-trees/) (the page did not mention the name "red-black"; that the paper introduced them is from memory).
- Left-leaning red-black trees: Sedgewick, 2008, via the [Wikipedia article](https://en.wikipedia.org/wiki/Left-leaning_red%E2%80%93black_tree) (the year, the 2-3 and 2-3-4 variants differing by one line, and the deletion cost); the paper itself is [available as a PDF](https://cspages.ucalgary.ca/~mgavrilo/335/LLRB.pdf) on a course page (I did not read it in full).
- The libstdc++ comparison (section 8) uses the header `bits/stl_tree.h` of the GCC 13 installation used for the tests, through the iterator's node pointer.
- Marked "from memory" in the text: the amortised bound on recolorings, the textbook source of the cases, and the paper's role.
- All other claims in this tutorial come from the tests and measurements described at the top.
