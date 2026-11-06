# DSA 07.29: B-Trees and B+ Trees

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (6 reading, 3 problems) · You need: [Rotations and AVL trees](10_rotations_and_avl_trees.md) and [Red-black trees](11_red_black_trees_invariants_and_insertion.md) (the binary balanced trees these replace when a node is a disk page or a cache line) and [Inside std::set and std::map](09_inside_std_set_and_map.md) (the baseline in the measurements). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

A binary tree spends one comparison per node and one memory access per node. When the cost of an access is large (a disk page, or a cache miss that costs 100 times a comparison), that is the wrong trade. A **B-tree** puts many keys in one node, sorted, so that one access narrows the search by a factor of the node's fanout instead of 2. Every database index, most file systems' metadata, and every ordered key-value store built on pages uses a B-tree or its variant, the **B+ tree**, which keeps all values in the leaves and chains the leaves together.

By the end you can:

- explain **why fanout matters** with numbers (10^9 keys are 4 to 5 levels deep with a fanout of 256, against 30 for a binary tree);
- write a **B-tree** (split full nodes on the way down, borrow and merge on the way down) and a **B+ tree** (the separator is **copied** up from a leaf split, the middle key **moves** up from an internal split; the **leaf chain** serves range scans), and say which invariants hold at every step;
- state the height bounds and the space utilisation (about 69 percent for random inserts, 50 percent for ascending ones) and see them in measurements;
- say where these trees run, how they are made concurrent, and when an LSM tree is the better choice, marking what is general knowledge.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 1,659,000 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran, for `BTree` with T = 2, 3, 4, 5, 16 and `BPlusTree` with B = 3, 4, 5, 6, 7, 8, 16, 33: random insert, erase and find against `std::map` on key spaces of 20, 150 and 3,000 keys, with **the whole structure validated after every operation** on the two small ones (key order and separator bounds, node sizes, equal leaf depth, the key count, and the leaf chain: the leaves in tree order, linked, holding every key once); ascending, descending and shuffled insertion followed by erasure in three orders; the height against its proven bounds; range scans against `std::map`; the exact shapes of the traces below; the empty tree, `INT_MIN` and `INT_MAX`, and duplicate inserts; and 300,000 shuffled keys with half of them erased. Finally 163 hand-made mutants of the code were run against the tests (a wrong split point, a lost value, a borrow from the wrong side, a missing chain link and so on): **157 were caught and 6 are equivalent changes** that do not alter any output (listed after the common mistakes).

**Data and timing.** The data is **synthetic**: 1,000,000 distinct random keys below 2^30 (so many nodes share no locality), inserted in random order, looked up in a different random order; values are the keys. A range query reads 100 consecutive keys, starting at a random key. Times are on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topics 19 to 28), best of 3 repetitions, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about databases and file systems are from general knowledge and say so.

---

## 1. Why fanout

The cost model is: **the number of nodes touched**, each one a page read or a cache miss. A search in a tree of fanout f touches about log_f(n) nodes. For n = 10^9 keys:

| Fanout f | 2 | 4 | 16 | 64 | 256 | 512 |
|---|---|---|---|---|---|---|
| Levels, ceil(log_f 10^9) | 30 | 15 | 8 | 5 | 4 | 4 |

A page of 4096 bytes with 8-byte keys and 8-byte child pointers holds about 4096 / 16 = 256 entries, so f = 256 is a real design point, not a made-up one. Nodes are about 69 percent full on average (section 4), so the effective fanout is nearer 180, which still gives 4 to 5 levels. The upper levels are few: the first two hold 1 + 256 pages, about a megabyte, so they stay in memory and a lookup in a billion-key index costs about two disk reads (general knowledge of how databases are tuned, not measured here).

Inside a node the keys are sorted and searched by binary search; the work per node is more than for a binary tree, but it is work on one contiguous block already in cache, and it is the **memory access** that dominates.

## 2. The B-tree

A **B-tree of minimum degree T** (T >= 2, the form used in CLRS, named from memory): every node except the root holds **T - 1 to 2T - 1 keys**; an internal node with m keys has m + 1 children; the root has at least one key (unless the tree is empty); **all leaves are at the same depth**. Keys and values live in every node. With T = 2 this is a 2-3-4 tree, which is how red-black trees are explained (Topic 11).

Two bounds follow. A tree of height h has at most (2T)^h - 1 keys (every node full). It has at least 2 T^(h-1) - 1 keys (the root has one key and two children, every other node the minimum T - 1 keys and T children). So **h <= 1 + log_T((n + 1) / 2)**. The tests check both bounds on random trees.

```cpp
// B-tree of minimum degree T (CLRS): every node except the root holds T-1 to 2T-1 keys, an internal node with m keys has m+1 children,
// and all leaves are at the same depth. Keys AND values live in every node. Insert splits full nodes on the way DOWN, so it never
// has to climb back; erase makes sure the child it enters has at least T keys (borrow from a sibling, or merge), also on the way down.
template <int T>
struct BTree {
    static_assert(T >= 2, "BTree: T must be at least 2");
    struct Node {
        std::vector<int> key, val;
        std::vector<std::unique_ptr<Node>> kid;                    // empty in a leaf
        bool leaf() const { return kid.empty(); }
        std::size_t slot(int k) const { return std::lower_bound(key.begin(), key.end(), k) - key.begin(); }
    };
    std::unique_ptr<Node> root = std::make_unique<Node>();
    std::size_t n = 0;

    const int* find(int k) const {
        const Node* x = root.get();
        for (;;) {
            std::size_t i = x->slot(k);
            if (i < x->key.size() && x->key[i] == k) return &x->val[i];
            if (x->leaf()) return nullptr;
            x = x->kid[i].get();
        }
    }
    static void split(Node& x, std::size_t i) {                    // x.kid[i] is full (2T-1 keys): its median moves up into x
        Node& y = *x.kid[i];
        auto z = std::make_unique<Node>();
        z->key.assign(y.key.begin() + T, y.key.end());
        z->val.assign(y.val.begin() + T, y.val.end());
        if (!y.leaf()) {
            for (std::size_t j = T; j < y.kid.size(); ++j) z->kid.push_back(std::move(y.kid[j]));
            y.kid.resize(T);
        }
        int mk = y.key[T - 1], mv = y.val[T - 1];
        y.key.resize(T - 1); y.val.resize(T - 1);
        x.key.insert(x.key.begin() + i, mk); x.val.insert(x.val.begin() + i, mv);
        x.kid.insert(x.kid.begin() + i + 1, std::move(z));
    }
    bool insert(int k, int v) {                                    // false if k was present (its value is replaced)
        if (const int* p = find(k)) { *const_cast<int*>(p) = v; return false; }
        if (root->key.size() == 2 * T - 1) {                       // the root is full: the tree grows in height here, and only here
            auto r = std::make_unique<Node>();
            r->kid.push_back(std::move(root)); root = std::move(r);
            split(*root, 0);
        }
        Node* x = root.get();
        while (!x->leaf()) {
            std::size_t i = x->slot(k);
            if (x->kid[i]->key.size() == 2 * T - 1) { split(*x, i); if (k > x->key[i]) ++i; }
            x = x->kid[i].get();
        }
        std::size_t i = x->slot(k);
        x->key.insert(x->key.begin() + i, k); x->val.insert(x->val.begin() + i, v);
        ++n;
        return true;
    }

    static void merge(Node& x, std::size_t i) {                    // kid[i] + key[i] + kid[i+1] become kid[i]
        Node& a = *x.kid[i];
        std::unique_ptr<Node> b = std::move(x.kid[i + 1]);
        a.key.push_back(x.key[i]); a.val.push_back(x.val[i]);
        a.key.insert(a.key.end(), b->key.begin(), b->key.end());
        a.val.insert(a.val.end(), b->val.begin(), b->val.end());
        for (auto& c : b->kid) a.kid.push_back(std::move(c));
        x.key.erase(x.key.begin() + i); x.val.erase(x.val.begin() + i); x.kid.erase(x.kid.begin() + i + 1);
    }
    static std::size_t fix(Node& x, std::size_t i) {               // make kid[i] hold at least T keys; return where to descend afterwards
        Node& c = *x.kid[i];
        if (i > 0 && x.kid[i - 1]->key.size() >= T) {              // borrow through the parent from the left sibling
            Node& l = *x.kid[i - 1];
            c.key.insert(c.key.begin(), x.key[i - 1]); c.val.insert(c.val.begin(), x.val[i - 1]);
            x.key[i - 1] = l.key.back(); x.val[i - 1] = l.val.back(); l.key.pop_back(); l.val.pop_back();
            if (!l.leaf()) { c.kid.insert(c.kid.begin(), std::move(l.kid.back())); l.kid.pop_back(); }
            return i;
        }
        if (i < x.key.size() && x.kid[i + 1]->key.size() >= T) {   // borrow from the right sibling
            Node& r = *x.kid[i + 1];
            c.key.push_back(x.key[i]); c.val.push_back(x.val[i]);
            x.key[i] = r.key.front(); x.val[i] = r.val.front(); r.key.erase(r.key.begin()); r.val.erase(r.val.begin());
            if (!r.leaf()) { c.kid.push_back(std::move(r.kid.front())); r.kid.erase(r.kid.begin()); }
            return i;
        }
        if (i < x.key.size()) { merge(x, i); return i; }           // both siblings are at the minimum: merge
        merge(x, i - 1); return i - 1;
    }
    bool erase_from(Node& x, int k) {
        std::size_t i = x.slot(k);
        bool here = i < x.key.size() && x.key[i] == k;
        if (x.leaf()) {
            if (!here) return false;
            x.key.erase(x.key.begin() + i); x.val.erase(x.val.begin() + i);
            return true;
        }
        if (here) {
            if (x.kid[i]->key.size() >= T) {                       // replace by the predecessor (largest key of the left subtree)
                const Node* m = x.kid[i].get();
                while (!m->leaf()) m = m->kid.back().get();
                x.key[i] = m->key.back(); x.val[i] = m->val.back();
                return erase_from(*x.kid[i], x.key[i]);
            }
            if (x.kid[i + 1]->key.size() >= T) {                   // or by the successor
                const Node* m = x.kid[i + 1].get();
                while (!m->leaf()) m = m->kid.front().get();
                x.key[i] = m->key.front(); x.val[i] = m->val.front();
                return erase_from(*x.kid[i + 1], x.key[i]);
            }
            merge(x, i);                                           // both neighbours are at the minimum: k comes down into the merged node
            return erase_from(*x.kid[i], k);
        }
        if (x.kid[i]->key.size() < T) i = fix(x, i);
        return erase_from(*x.kid[i], k);
    }
    bool erase(int k) {
        bool r = erase_from(*root, k);
        if (root->key.empty() && !root->leaf()) { auto c = std::move(root->kid[0]); root = std::move(c); }   // the tree shrinks here, and only here
        if (r) --n;
        return r;
    }

    int height() const { int h = 1; for (const Node* x = root.get(); !x->leaf(); x = x->kid[0].get()) ++h; return h; }
    std::string dump() const { std::string s; dump(*root, s); return s; }
    static void dump(const Node& x, std::string& s) {              // [child key child key child]
        s += '[';
        for (std::size_t i = 0; i <= x.key.size(); ++i) {
            if (!x.leaf()) { if (i) s += ' '; dump(*x.kid[i], s); }
            if (i < x.key.size()) { if (i || !x.leaf()) s += ' '; s += std::to_string(x.key[i]); }
        }
        s += ']';
    }
    // Every invariant: order, node sizes, equal leaf depth, key count. Returns the depth of the subtree, or -1 on a violation.
    int check(const Node& x, const int* lo, const int* hi, bool is_root, std::size_t& count) const {
        if (!is_root && (x.key.size() < T - 1 || x.key.size() > 2 * T - 1)) return -1;
        if (is_root && x.key.size() > 2 * T - 1) return -1;
        if (!x.leaf() && (x.key.empty() || x.kid.size() != x.key.size() + 1)) return -1;
        if (x.val.size() != x.key.size()) return -1;
        for (std::size_t i = 0; i < x.key.size(); ++i) {
            if (i && x.key[i - 1] >= x.key[i]) return -1;
            if ((lo && x.key[i] <= *lo) || (hi && x.key[i] >= *hi)) return -1;
        }
        count += x.key.size();
        if (x.leaf()) return 1;
        int d = -2;
        for (std::size_t i = 0; i < x.kid.size(); ++i) {
            int c = check(*x.kid[i], i ? &x.key[i - 1] : lo, i < x.key.size() ? &x.key[i] : hi, false, count);
            if (c < 0 || (d != -2 && c != d)) return -1;
            d = c;
        }
        return d + 1;
    }
    bool valid() const { std::size_t c = 0; return check(*root, nullptr, nullptr, true, c) > 0 && c == n; }
};
```

### Insert: split on the way down

A node with 2T - 1 keys is **full**. Insert looks for the leaf and, **before entering any full node, splits it**: its median key moves up into the parent (which is known not to be full, because it was split first if it was), and the node becomes two nodes of T - 1 keys. So an insert never has to go back up. Only a full root grows the tree, by creating a new root above it. Trace for T = 2, inserting 1, 2, 3, ... (a node is `[child key child key child]`, a leaf is `[key key]`):

```
after insert 3:   [1 2 3]
after insert 4:   [[1] 2 [3 4]]
after insert 5:   [[1] 2 [3 4 5]]
after insert 7:   [[1] 2 [3] 4 [5 6 7]]
after insert 10:  [[[1] 2 [3]] 4 [[5] 6 [7] 8 [9 10]]]
```

Inserting 4 finds the root `[1 2 3]` full, so it splits before looking further: the median 2 goes up into a new root. Insert 6 (not shown) finds the leaf `[3 4 5]` full and splits it first, moving 4 up; 7 then fills the new leaf `[5 6]`. Ascending insertion leaves every node but the last at the minimum, about half full.

### Delete: make sure the child has T keys before entering it

Erase also works on the way down, with the mirror rule: **never enter a child that has only T - 1 keys**. For the key `k` and the current node `x`:

- **`k` is in a leaf:** remove it. (The node has at least T keys because we ensured it on the way in, or is the root.)
- **`k` is in an internal node** at position i: if the left child has at least T keys, replace `k` by its **predecessor** (the largest key of the left subtree) and erase that from the left child; else if the right child has at least T keys, use the **successor**; else both neighbours have T - 1 keys: **merge** them with `k` between into one node of 2T - 1 keys, and erase `k` from it.
- **`k` is not in `x`:** descend into child i, but first fix it if it has only T - 1 keys: **borrow** a key from a sibling that has at least T (rotate through the parent: the sibling's end key goes up, the parent's separator comes down), or, if both siblings are at the minimum, **merge** with one of them.

Only when the root ends up with no key does the tree shrink. Trace for T = 2, continuing from the 10 keys above:

```
erase 6:  [[[1] 2 [3]] 4 [[5 7] 8 [9 10]]]       6 was in [5 6 7], a leaf with 3 keys
erase 3:  [[1] 2 [4] 5 [7] 8 [9 10]]             the root and its children were minimal: they merge, the tree loses a level
erase 8:  [[1] 2 [4] 5 [7] 9 [10]]               8 is replaced by its successor 9
erase 1:  [[2 4] 5 [7] 9 [10]]                   [1] is minimal and so is its sibling [4]: merge them with 2
erase 5:  [[2] 4 [7] 9 [10]]                     5 is replaced by its predecessor 4
```

Look at the second line: erasing 3 had to enter the child `[[1] 2 [3]]`, which has the minimum, and its sibling is minimal too, so the root and both children merged into one node: the tree went from three levels to two.

## 3. The B+ tree

The B+ tree changes where things live:

- **Values (the data) only in the leaves.** Internal nodes hold **separators** only, which are copies of keys used to route the search. So an internal node of the same size has more room: more keys per node means a higher fanout, so a lower tree.
- **Leaves are chained** left to right. A range scan descends once to the first key and then walks the chain, with no climbing back up and no recursion.
- **Every lookup ends at a leaf**, so every lookup costs the same number of nodes; there is no early exit for a key found in an upper node.

In this implementation the search rule is: `kid[i]` holds keys **below** `key[i]` and `kid[i + 1]` holds keys **at or above** it, so the child to descend into is the number of separators that are `<= k` (`upper_bound`). Each node holds at most B keys and, except for the root, at least B / 2. Nodes are fixed arrays with one spare slot, so a node may hold B + 1 keys for a moment before it is split.

```cpp
// B+ tree with at most B keys per node (B >= 3); every node except the root keeps at least B/2.
// Internal nodes hold only separators: kid[i] has keys below key[i], kid[i+1] has keys at or above it. All values sit in the leaves,
// and the leaves are chained left to right, so a range scan is one descent plus a walk along the chain.
// Nodes have fixed arrays with one spare slot: a node is allowed to hold B+1 keys for an instant before it splits.
template <int B>
struct BPlusTree {
    static_assert(B >= 3, "BPlusTree: B must be at least 3");
    static constexpr int MIN = B / 2;
    struct Node { bool leaf; int n = 0; int key[B + 1]; explicit Node(bool l) : leaf(l) {} virtual ~Node() = default; };
    struct Leaf : Node { int val[B + 1]; Leaf* next = nullptr; Leaf() : Node(true) {} };
    struct Inner : Node { std::unique_ptr<Node> kid[B + 2]; Inner() : Node(false) {} };
    static Leaf* L(Node* x) { return static_cast<Leaf*>(x); }
    static const Leaf* L(const Node* x) { return static_cast<const Leaf*>(x); }
    static Inner* I(Node* x) { return static_cast<Inner*>(x); }
    static const Inner* I(const Node* x) { return static_cast<const Inner*>(x); }

    std::unique_ptr<Node> root = std::make_unique<Leaf>();
    std::size_t n = 0;

    static int ub(const Node& x, int k) { return static_cast<int>(std::upper_bound(x.key, x.key + x.n, k) - x.key); }   // keys <= k
    static int lb(const Node& x, int k) { return static_cast<int>(std::lower_bound(x.key, x.key + x.n, k) - x.key); }   // keys <  k
    const Leaf* leaf_for(int k) const {                            // the one leaf that would hold k
        const Node* x = root.get();
        while (!x->leaf) x = I(x)->kid[ub(*x, k)].get();
        return L(x);
    }
    const int* find(int k) const {
        const Leaf* l = leaf_for(k);
        int i = lb(*l, k);
        return i < l->n && l->key[i] == k ? &l->val[i] : nullptr;
    }
    // Calls f(key, value) for every key in [lo, hi), in order, by walking the leaf chain. Returns how many.
    template <class F> std::size_t range(int lo, int hi, F f) const {
        std::size_t c = 0;
        const Leaf* l = leaf_for(lo);
        for (int i = lb(*l, lo); l; l = l->next, i = 0)
            for (; i < l->n; ++i) { if (l->key[i] >= hi) return c; f(l->key[i], l->val[i]); ++c; }
        return c;
    }

    struct Split { bool did = false; int sep = 0; std::unique_ptr<Node> right; };
    Split put(Node* x, int k, int v, bool& added) {
        if (x->leaf) {
            Leaf* l = L(x);
            int i = lb(*l, k);
            if (i < l->n && l->key[i] == k) { l->val[i] = v; added = false; return {}; }
            for (int j = l->n; j > i; --j) { l->key[j] = l->key[j - 1]; l->val[j] = l->val[j - 1]; }
            l->key[i] = k; l->val[i] = v; ++l->n; added = true;
            if (l->n <= B) return {};
            auto r = std::make_unique<Leaf>();                     // B+1 keys: the left leaf keeps the first half, the right gets the rest
            int keep = (B + 1) / 2;
            for (int j = keep; j < l->n; ++j) { r->key[j - keep] = l->key[j]; r->val[j - keep] = l->val[j]; }
            r->n = l->n - keep; l->n = keep;
            r->next = l->next; l->next = r.get();
            Split s; s.did = true; s.sep = r->key[0]; s.right = std::move(r);   // the separator is COPIED up: the key stays in the leaf
            return s;
        }
        Inner* p = I(x);
        int i = ub(*p, k);
        Split s = put(p->kid[i].get(), k, v, added);
        if (!s.did) return {};
        for (int j = p->n; j > i; --j) { p->key[j] = p->key[j - 1]; p->kid[j + 1] = std::move(p->kid[j]); }
        p->key[i] = s.sep; p->kid[i + 1] = std::move(s.right); ++p->n;
        if (p->n <= B) return {};
        auto r = std::make_unique<Inner>();                        // B+1 keys: the middle one MOVES up, it is not kept below
        int mid = p->n / 2;
        Split t; t.did = true; t.sep = p->key[mid];
        r->n = p->n - mid - 1;
        for (int j = 0; j < r->n; ++j) r->key[j] = p->key[mid + 1 + j];
        for (int j = 0; j <= r->n; ++j) r->kid[j] = std::move(p->kid[mid + 1 + j]);
        p->n = mid;
        t.right = std::move(r);
        return t;
    }
    bool insert(int k, int v) {                                    // false if k was present (its value is replaced)
        bool added = false;
        Split s = put(root.get(), k, v, added);
        if (s.did) {                                               // the root split: the tree grows in height
            auto r = std::make_unique<Inner>();
            r->key[0] = s.sep; r->kid[0] = std::move(root); r->kid[1] = std::move(s.right); r->n = 1;
            root = std::move(r);
        }
        if (added) ++n;
        return added;
    }

    void merge(Inner* p, int j) {                                  // kid[j+1] is appended to kid[j] and removed
        Node* a = p->kid[j].get(); Node* b = p->kid[j + 1].get();
        if (a->leaf) {
            for (int t = 0; t < b->n; ++t) { L(a)->key[a->n + t] = b->key[t]; L(a)->val[a->n + t] = L(b)->val[t]; }
            a->n += b->n; L(a)->next = L(b)->next;
        } else {
            a->key[a->n] = p->key[j];                              // the separator comes down between the two
            for (int t = 0; t < b->n; ++t) a->key[a->n + 1 + t] = b->key[t];
            for (int t = 0; t <= b->n; ++t) I(a)->kid[a->n + 1 + t] = std::move(I(b)->kid[t]);
            a->n += b->n + 1;
        }
        for (int t = j; t + 1 < p->n; ++t) { p->key[t] = p->key[t + 1]; p->kid[t + 1] = std::move(p->kid[t + 2]); }
        p->kid[p->n].reset(); --p->n;
    }
    void rebalance(Inner* p, int i) {                              // kid[i] has fewer than MIN keys
        Node* c = p->kid[i].get();
        Node* l = i > 0 ? p->kid[i - 1].get() : nullptr;
        Node* r = i < p->n ? p->kid[i + 1].get() : nullptr;
        if (l && l->n > MIN) {
            if (c->leaf) {
                for (int j = c->n; j > 0; --j) { c->key[j] = c->key[j - 1]; L(c)->val[j] = L(c)->val[j - 1]; }
                c->key[0] = l->key[l->n - 1]; L(c)->val[0] = L(l)->val[l->n - 1];
                p->key[i - 1] = c->key[0];
            } else {
                for (int j = c->n; j > 0; --j) c->key[j] = c->key[j - 1];
                for (int j = c->n + 1; j > 0; --j) I(c)->kid[j] = std::move(I(c)->kid[j - 1]);
                c->key[0] = p->key[i - 1]; I(c)->kid[0] = std::move(I(l)->kid[l->n]);
                p->key[i - 1] = l->key[l->n - 1];
            }
            --l->n; ++c->n;
        } else if (r && r->n > MIN) {
            if (c->leaf) {
                c->key[c->n] = r->key[0]; L(c)->val[c->n] = L(r)->val[0];
                for (int j = 0; j + 1 < r->n; ++j) { r->key[j] = r->key[j + 1]; L(r)->val[j] = L(r)->val[j + 1]; }
                p->key[i] = r->key[0];
            } else {
                c->key[c->n] = p->key[i]; I(c)->kid[c->n + 1] = std::move(I(r)->kid[0]);
                p->key[i] = r->key[0];
                for (int j = 0; j + 1 < r->n; ++j) r->key[j] = r->key[j + 1];
                for (int j = 0; j < r->n; ++j) I(r)->kid[j] = std::move(I(r)->kid[j + 1]);
            }
            --r->n; ++c->n;
        } else if (l) merge(p, i - 1);
        else merge(p, i);
    }
    bool take(Node* x, int k) {
        if (x->leaf) {
            Leaf* l = L(x);
            int i = lb(*l, k);
            if (i >= l->n || l->key[i] != k) return false;
            for (int j = i; j + 1 < l->n; ++j) { l->key[j] = l->key[j + 1]; l->val[j] = l->val[j + 1]; }
            --l->n;
            return true;
        }
        Inner* p = I(x);
        int i = ub(*p, k);
        bool r = take(p->kid[i].get(), k);
        if (r && p->kid[i]->n < MIN) rebalance(p, i);
        return r;
    }
    bool erase(int k) {
        bool r = take(root.get(), k);
        if (!root->leaf && root->n == 0) { auto c = std::move(I(root.get())->kid[0]); root = std::move(c); }   // the tree shrinks
        if (r) --n;
        return r;
    }

    int height() const { int h = 1; for (const Node* x = root.get(); !x->leaf; x = I(x)->kid[0].get()) ++h; return h; }
    std::string dump() const { std::string s; dump(*root, s); return s; }
    static void dump(const Node& x, std::string& s) {
        s += '[';
        for (int i = 0; i <= x.n; ++i) {
            if (!x.leaf) { if (i) s += ' '; dump(*I(&x)->kid[i], s); }
            if (i < x.n) { if (i || !x.leaf) s += ' '; s += std::to_string(x.key[i]); }
        }
        s += ']';
    }
    // Order, separator bounds, node sizes, equal depth, and the leaf chain (the leaves in tree order, linked, holding n keys in all).
    int check(const Node& x, const int* lo, const int* hi, bool is_root, std::vector<const Leaf*>& leaves) const {
        if (x.n > B || (!is_root && x.n < MIN) || (!x.leaf && x.n < 1)) return -1;
        for (int i = 0; i < x.n; ++i) {
            if (i && x.key[i - 1] >= x.key[i]) return -1;
            if (x.leaf ? ((lo && x.key[i] < *lo) || (hi && x.key[i] >= *hi)) : ((lo && x.key[i] <= *lo) || (hi && x.key[i] >= *hi))) return -1;
        }
        if (x.leaf) { leaves.push_back(L(&x)); return 1; }
        int d = -2;
        for (int i = 0; i <= x.n; ++i) {
            if (!I(&x)->kid[i]) return -1;
            int c = check(*I(&x)->kid[i], i ? &x.key[i - 1] : lo, i < x.n ? &x.key[i] : hi, false, leaves);
            if (c < 0 || (d != -2 && c != d)) return -1;
            d = c;
        }
        return d + 1;
    }
    bool valid() const {
        std::vector<const Leaf*> leaves;
        if (check(*root, nullptr, nullptr, true, leaves) < 1) return false;
        std::size_t total = 0;
        for (std::size_t i = 0; i < leaves.size(); ++i) {
            total += leaves[i]->n;
            if (leaves[i]->next != (i + 1 < leaves.size() ? leaves[i + 1] : nullptr)) return false;
        }
        return total == n;
    }
    // Counts for the benchmark: nodes, and keys stored in leaves.
    void stats(std::size_t& leaves, std::size_t& inner) const { leaves = inner = 0; stats(*root, leaves, inner); }
    static void stats(const Node& x, std::size_t& l, std::size_t& in) {
        if (x.leaf) { ++l; return; }
        ++in;
        for (int i = 0; i <= x.n; ++i) stats(*I(&x)->kid[i], l, in);
    }
};
```

### Insert: split when a node overflows

Insert recurses to the leaf, puts the key in, and if the leaf has B + 1 keys splits it into two, and **returns the split upward** (the new right node and the separator); the parent inserts it and may split in turn. If the root splits, a new root is made.

The two splits differ, and mixing them up is the commonest bug:

- **Leaf split:** the separator is the **first key of the new right leaf**, and it is **copied** up: the key stays in the leaf, because the value lives there.
- **Internal split:** the **middle key moves up**: it is removed from both halves, because separators in an internal node are only signposts.

Trace for B = 3 (at most 3 keys per node), inserting 1, 2, 3, ... (the root is printed as `[child separator child ...]`):

```
after insert 3:   [1 2 3]
after insert 4:   [[1 2] 3 [3 4]]                     leaf split, 3 copied up
after insert 5:   [[1 2] 3 [3 4 5]]
after insert 7:   [[1 2] 3 [3 4] 5 [5 6 7]]
after insert 10:  [[[1 2] 3 [3 4] 5 [5 6]] 7 [[7 8] 9 [9 10]]]   the root split: 7 moved up
```

Notice that 3, 5, 7 and 9 each appear twice: once as a separator and once as a key in a leaf. The 7 in the root is the one that **moved** (an internal split); the 7 in the leaf `[7 8]` is the key itself.

### Delete: fix an underflow on the way back up

Erase recurses to the leaf, removes the key, and when the child it came out of has fewer than B / 2 keys, the parent repairs it: **borrow** from the left sibling if that has more than the minimum, else from the right sibling, else **merge** with a sibling (the two nodes together fit in B). A merge removes a separator from the parent, which may then underflow in turn. When the root is left with no key and one child, the child becomes the root.

The last line shows an internal merge: the node `[[6]]` has no separator left, its sibling is minimal, so the two merge and the root's key 7 comes **down** between them; the root is left with no key and its only child replaces it, so the tree loses a level. A leaf borrow moves one key and updates the parent's separator to the new first key of the right-hand node. An internal borrow **rotates through the parent**: the parent's separator comes down, the sibling's end key goes up.

```
erase 7:  [[[1 2] 3 [3 4] 5 [5 6]] 7 [[8] 9 [9 10]]]            the leaf [7 8] shrinks; the separator 7 stays
erase 8:  [[[1 2] 3 [3 4] 5 [5 6]] 7 [[9] 10 [10]]]            [8] is empty: borrow 9 from the right leaf, the separator becomes 10
erase 1:  [[[2] 3 [3 4] 5 [5 6]] 7 [[9] 10 [10]]]
erase 2:  [[[3] 4 [4] 5 [5 6]] 7 [[9] 10 [10]]]                 [2] is empty: borrow 3 from [3 4]
erase 3:  [[[4] 5 [5 6]] 7 [[9] 10 [10]]]                       [3] is empty and [4] is minimal: merge, a separator leaves the parent
erase 4:  [[[5] 6 [6]] 7 [[9] 10 [10]]]                         [4] is empty: borrow 5 from [5 6]
erase 5:  [[6] 7 [9] 10 [10]]                                   [5] and [6] merge; the node above is left with no key, merges with its sibling, the root empties
```

The first line shows a fact that surprises people: after `erase 7` the separator **7 is still in the root** though the key is gone. That is correct. A separator only has to satisfy "everything on the left is smaller, everything on the right is at least this", and 7 still does. The tests check exactly this: the bounds are checked against separators, not against present keys.

### Range scan

`range(lo, hi, f)` finds the leaf for `lo`, starts at its first key `>= lo`, and follows `next` until a key `>= hi`. For `B = 4` and the keys 1 to 12 inserted in the order 5, 1, 9, 3, 7, 2, 8, 4, 6, 10, 11, 12 the tree is `[[1 2 3 4] 5 [5 6] 7 [7 8] 9 [9 10 11 12]]` and `range(4, 9)` visits 4 | 5 6 | 7 8, five keys with sum 30, touching three leaves after the descent. The cost is one descent plus the number of leaves the answer spans.

## 4. What it costs

Numbers are **run 1 / run 2**; 10^6 keys. "Fill" is keys divided by leaf capacity.

| B (keys per node) | Node bytes: leaf / inner | Height | Fill, random order | Fill, ascending order | Bytes per key |
|---|---|---|---|---|---|
| 7 | 88 / 120 | 8 | 0.725 | 0.571 | 22.31 |
| 15 | 152 / 216 | 6 | 0.707 | 0.533 | 16.30 |
| 31 | 280 / 408 | 5 | 0.700 | 0.516 | 13.78 |
| 63 | 536 / 792 | 4 | 0.696 | 0.508 | 12.63 |
| 127 | 1048 / 1560 | 3 | 0.695 | 0.504 | 12.08 |
| 255 | 2072 / 3096 | 3 | 0.699 | 0.502 | 11.72 |

| B | Build (ms) | Lookup (ns) | Range scan (ns per key) |
|---|---|---|---|
| 7 | 421.6 / 435.1 | 436.7 / 470.2 | 15.2 / 15.0 |
| 15 | 300.4 / 308.4 | 341.5 / 367.0 | 8.7 / 8.7 |
| 31 | 272.2 / 260.3 | 288.1 / 284.7 | 5.6 / 5.9 |
| 63 | 232.1 / 235.1 | 250.1 / 248.1 | 4.7 / 5.1 |
| 127 | 220.2 / 217.4 | 233.8 / 229.9 | 3.8 / 3.7 |
| 255 | 220.7 / 222.6 | 226.2 / 225.2 | 3.2 / 3.5 |
| `std::map` | 796.5 / 772.0 | 1,035.3 / 1,057.9 | 132.8 / 152.3 |
| sorted array | not applicable | 186.2 / 196.1 | not measured |

The same keys in the B-tree with vectors as nodes (T = 4, 16, 64 gives at most 7, 31, 127 keys per node):

| T | Height | Build (ms) | Lookup (ns) |
|---|---|---|---|
| 4 | 9 | 875.1 / 863.2 | 824.2 / 790.2 |
| 16 | 5 | 571.6 / 541.2 | 421.9 / 441.0 |
| 64 | 4 | 395.2 / 389.2 | 265.4 / 264.7 |

What both runs agree on:

- **Space.** Random-order insertion fills the leaves 69.5 to 72.5 percent, near ln 2 = 69.3 percent (the classic result for random insertion, from memory); ascending insertion leaves them at 50.2 to 57.1 percent, because each split leaves the left half and no key ever goes there again. Databases avoid the waste with a special case: when the insert is at the right end of the tree, split so that the old node stays full (general knowledge, not implemented here).
- **Lookup falls as the fanout rises, with diminishing returns.** The B+ tree is 2.2 to 4.7 times faster than `std::map` at every fanout measured, and the step from 127 to 255 keys is under 4 percent. The **sorted array** with binary search (186 to 196 ns) is still faster than every B+ tree row: a B+ tree pays for being updatable.
- **Range scans.** At B = 255 a scan costs 3.2 to 3.5 ns per key against 133 to 152 for `std::map`: about 40 times less. The leaves are contiguous arrays, a tree walk is a pointer chase.
- **Build** with the largest nodes (B = 255) takes 221 to 223 ms against 772 to 797 for `std::map`.
- **B-tree against B+ tree is not a clean comparison here.** At comparable node sizes the vector-based B-tree (790 to 824 ns at T = 4, at most 7 keys) is slower than the array-based B+ tree (437 to 470 ns at B = 7), but the two differ in more than one thing (vectors on the heap against arrays inside the node, values in internal nodes, an extra indirection), so this says little about the shapes themselves.
- Erasing all 10^6 keys in random order from a B = 63 tree took 254.8 / 257.0 ms and left an empty, valid tree.

## 5. Where they run

All of this section is general knowledge from memory, not measured or re-read for this file.

- **SQLite** stores every table and index as a B-tree of pages; **InnoDB** (MySQL) stores each table as a B+ tree clustered on the primary key; **XFS** uses B+ trees for free space and extents; **Btrfs** is a copy-on-write B-tree file system; PostgreSQL, SQL Server and Oracle use B+ tree variants for ordinary indexes.
- **Real pages hold variable-length keys,** so a node is a slotted page, internal keys are often prefix-compressed, and a split happens when the page runs out of bytes, not at B + 1 keys.
- **Concurrency.** **Latch crabbing** locks the child before releasing the parent, and releases the parent once the child is "safe" (will not split or merge). The **B-link tree** (Lehman and Yao, 1981) adds a right-sibling link and a high key to every node, so a reader that meets a node that just split follows the link instead of restarting.
- **Crash safety** is a separate layer: a write-ahead log, or copy-on-write of the path to the root.
- **B-tree against LSM tree.** A B-tree updates pages in place (a small random write rewrites a page; reads are cheap). An LSM tree buffers writes and merges sorted files in the background (cheap writes, reads that may check several files, compaction work). Read-heavy and range-heavy workloads suit a B+ tree; sustained write ingest suits an LSM.
- **Deletion in practice.** Many engines do not merge on delete; they leave sparse pages and merge lazily. The merge code here is the textbook version.

## 6. Common mistakes

- **Copying the wrong key on a split.** In a B+ tree, a leaf split copies the separator up, an internal split moves it up. Moving up from a leaf loses a value; copying up from an internal node leaves a duplicate signpost and an over-full node.
- **Search tie rule.** With separators that are the first key of the right subtree, a key equal to a separator goes **right**. Using `lower_bound` for the child choice sends it left and misses the key.
- **Underflow threshold.** The minimum is B / 2 (integer division) for the B+ tree here; with that, a merge of an at-minimum node and an underflowed one always fits in B. A minimum that is too high makes merges overflow.
- **Updating the separator after a borrow.** A leaf borrow must set the parent's separator to the new first key of the right-hand node; skipping this leaves a stale bound that tests of order will catch later, not at once.
- **Dropping the leaf chain.** When two leaves merge, the left leaf must take over the right one's `next`. A split must link the new leaf in. Scans then silently skip or stop early; only a test that scans a mutated tree sees it.

The equivalent mutants (changes that do not alter any output, so no test can catch them): adding the new root in `insert` without splitting the old root at once (the descent loop splits it on its first step anyway); testing `k >= x->key[i]` instead of `k > x->key[i]` after a split (the key is known to be absent, so the two are never equal); `break` instead of `return` when a range scan reaches `hi` (the scan then walks the rest of the chain and finds nothing: same output, slower); not clearing the last child slot of a parent after a merge (the pointer is never read again and the next insert overwrites it; the merged-away node merely lives until the parent dies); taking the new separator after a left leaf borrow from the sibling's last key instead of the borrowed key (it is the same key); and testing a child for underflow even when the key was not found (the child did not change, so it cannot be below the minimum). Every other change was caught by a test.

## 7. Questions to answer out loud

1. **Why B-trees?** The cost is nodes touched; many keys per node gives a fanout of hundreds and a height of 4 or 5 for a billion keys.
2. **What are the invariants?** Every non-root node has T - 1 to 2T - 1 keys; m keys means m + 1 children; all leaves are at the same depth; keys are ordered.
3. **How does insert avoid going back up?** It splits every full node on the way down, so the parent always has room.
4. **How does delete avoid going back up?** It makes sure the child it enters has at least T keys, by borrowing or merging.
5. **How does the height change?** Only at the root: split of a full root grows it, merge that empties the root shrinks it.
6. **What is a B+ tree and why prefer it?** Values only in leaves, chained; separators in internal nodes; higher fanout, uniform lookup cost, range scans by walking the chain.
7. **Copy up or move up?** A leaf split copies the separator up; an internal split moves the middle key up.
8. **B-tree or LSM?** Reads and ranges against sustained write ingest; write amplification against read amplification.

## 8. Problems

**A.** With T = 2 insert 1 to 7. Give the tree after 3, 4, 5 and 7 keys. Which inserts caused a split, and of what?

**B.** How many levels does a tree of fanout 2, 16 and 256 need for 10^9 keys? Why is a fanout of 256 realistic for a 4096-byte page?

**C.** With B = 3 insert 1 to 5 into a B+ tree. Give the tree. Why does 3 appear twice?

**D.** Continue from the 10-key B+ tree of section 3 and erase 7, then 8. Explain what happens to the leaf `[8]` and why the separator in the root is still 7.

**E.** For `B = 4`, the keys 1 to 12 inserted in the order 5, 1, 9, 3, 7, 2, 8, 4, 6, 10, 11, 12 and `range(4, 9)`: give the tree, the keys visited and which leaves are touched.

**F.** What are the smallest and largest numbers of keys in a B-tree of height 3 with T = 3?

**G.** Take the T = 2 tree of the keys 1 to 10 from section 2 after 6 has been erased. Erase 3 and say why the height drops. Which rule made the merge necessary?

**H.** Choose a structure: (a) an ordered map of 10^5 integers held in memory, rarely changed; (b) an on-disk index that must support "all keys between a and b"; (c) an index fed by a stream of millions of writes per second; (d) a read-mostly in-memory table of 10^6 sorted ids with no updates; (e) a database index with many concurrent writers.

### Answers

**A.** After 1, 2, 3: `[1 2 3]`. After 4: `[[1] 2 [3 4]]` (the root was full, so it split before 4 was placed: the median 2 became the root). After 5: `[[1] 2 [3 4 5]]`. After 7: `[[1] 2 [3] 4 [5 6 7]]`. The splits: the full root when 4 arrived (the median 2 became the root); the full leaf `[3 4 5]` when 6 arrived (it moved 4 up and 6 went into the new leaf `[5 6]`); insert 7 then filled that leaf.

**B.** 30, 8 and 4 levels (the table in section 1). A 4096-byte page holds 4096 / 16 = 256 pairs of an 8-byte key and an 8-byte pointer, ignoring the page header.

**C.** `[[1 2] 3 [3 4 5]]`. Inserting 4 made the leaf `[1 2 3 4]` overflow; it split into `[1 2]` and `[3 4]` and the first key of the right leaf, 3, was **copied** up. The 3 in the root is a signpost, the 3 in the leaf is the key and its value.

**D.** Erasing 7 from `[7 8]` leaves `[8]`, which is not below the minimum of 1 for B = 3, so nothing else happens, and the root keeps the separator 7 (still true: everything to its left is below 7, everything to its right is at least 7). Erasing 8 empties the leaf; its left neighbour in that parent does not exist (it is the first child), the right sibling `[9 10]` has 2 keys, more than the minimum, so the leaf **borrows** 9 and the separator becomes the new first key of the right sibling, 10: `[[9] 10 [10]]`.

**E.** The tree is `[[1 2 3 4] 5 [5 6] 7 [7 8] 9 [9 10 11 12]]`. The range [4, 9) visits 4, 5, 6, 7, 8: five keys, sum 30. The descent finds the leaf `[1 2 3 4]`, so the scan reads 4 there, then follows the chain through `[5 6]` and `[7 8]` and stops at 9 in `[9 10 11 12]`: three leaves read for keys, and a fourth leaf is entered only to see 9 and stop.

**F.** The fewest: 2 T^(h-1) - 1 = 2 * 9 - 1 = 17 keys (a root with 1 key, two children with 2 keys each, six leaves with 2 keys each: 1 + 4 + 12). The most: (2T)^h - 1 = 6^3 - 1 = 215 (all 43 nodes full with 5 keys).

**G.** The tree before the erase (after 6 was erased) is `[[[1] 2 [3]] 4 [[5 7] 8 [9 10]]]`: the root has one key and each child has one key, the minimum for T = 2. Erasing 3 must enter the left child, but the rule is never to enter a child with only T - 1 keys; its only sibling is minimal too, so there is nothing to borrow and the two merge with the root's key 4 between them into one node of 3 keys. The root is left with no key, so the merged node replaces it and the height drops from 3 to 2. (After that, entering the leaf `[3]` needs a borrow of 4 through the parent from `[5 7]`, and then 3 is removed.)

**H.** (a) `std::map` or a B+ tree in memory; either works at this size. (b) A B+ tree: range scans by the chain. (c) An LSM tree (cheap sequential writes). (d) A sorted array with binary search (it was the fastest lookup measured). (e) A B+ tree with latch crabbing or a B-link tree.

---

## Cheat sheet

- **B-tree (T):** T - 1 to 2T - 1 keys per non-root node; m keys, m + 1 children; equal leaf depth; h <= 1 + log_T((n + 1) / 2).
- **Insert (B-tree):** split every full node on the way down; only a full root grows the tree.
- **Erase (B-tree):** enter only children with at least T keys (borrow from a sibling, or merge); a key in an internal node is replaced by its predecessor or successor, or the neighbours are merged.
- **B+ tree:** values only in leaves, leaves chained, internal nodes are separators; child = number of separators `<= k`.
- **Splits:** leaf split **copies** the first key of the right half up; internal split **moves** the middle key up. Overflow is handled bottom-up.
- **Erase (B+):** repair an underflowing child from the parent: borrow left, borrow right, else merge; stale separators are fine; the root shrinks when it is left with no key.
- **Range scan:** one descent plus a walk along the chain.
- **Space:** 69 percent full for random inserts, 50 percent for ascending ones.
- **Measured (10^6 keys):** B+ lookup 225 to 470 ns against 1,035 to 1,058 for `std::map`; range scan 3.2 to 15.2 ns per key against 133 to 152.

---

## Sources

- B-trees were introduced by Bayer and McCreight (1970); the minimum-degree formulation and the on-the-way-down insert and delete are CLRS; the B-link tree is Lehman and Yao (1981). All named from memory, not re-read for this file. The code is my own implementation, tested as above; the invariants and bounds are derived in the text.
- The descriptions of SQLite, InnoDB, XFS, Btrfs, PostgreSQL, page layouts, latch crabbing, write-ahead logging, copy-on-write and LSM trees are from general knowledge and were not measured or implemented here. The 69 percent figure for random insertion is a classic result (from memory) that the measurements agree with. All timings and node counts are measured here.
- Interview forms, from memory: "design an on-disk index", "why do databases use B+ trees and not red-black trees?", "implement a range scan"; LeetCode has no direct B-tree problem.
