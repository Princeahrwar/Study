# DSA 07.33: Production-Grade Tree Code in Modern C++

*Part of the [Trees plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems and experiments) · You need: [BST core operations](07_bst_core_operations.md), [Iterative traversals](02_iterative_traversals_and_morris.md), [Inside std::set and std::map](09_inside_std_set_and_map.md) and [B-trees and B+ trees](29_b_trees_and_b_plus_trees.md). There is no companion demo file: the three listings are in this file and are tested code.*

You can write a binary search tree in 20 lines. This topic is about the other 200: who owns the nodes, what happens when a million of them are freed, what an iterator is, which allocator the nodes come from, what is left behind when `new` throws, and what `const` should mean. Then the question that decides whether any of it matters: is a node-per-key tree even the right container?

By the end you can:

- choose between `unique_ptr` children and raw pointers owned by a class, and show with a crash why the first needs a hand-written destructor;
- write a bidirectional iterator on parent pointers, write the stack alternative, and say what each costs;
- make a container allocator-aware (`std::allocator`, `std::pmr`) and say when an arena or a pool is the right resource;
- state the exception guarantee of each operation and test it by making allocations and key copies fail;
- keep `const` honest, and check part of it at compile time;
- read a benchmark of `std::set`, a sorted `vector`, a two-level block container and this tree, and say what it does and does not show.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 33,077 checks passed under AddressSanitizer with UBSan (GCC 13.3, C++17). The tests ran: the tree against `std::set` through 6,000 random operations with `valid()` after each; empty trees; erase with other iterators and addresses held; copy, move and self-assignment; a 20,000-node chain through destroy, copy, validate and iterate; the stack iterator against the in-order walk; two allocators and string keys; moves between equal and unequal arenas; a memory resource that fails after K allocations (every K up to 11) and a key type whose copy throws, each checked for an unchanged tree, no leaked node and `valid()`; compile-time `static_assert`s (section 6); and the block container against `std::set`. Then 75 hand-made mutants of the three listings were run against the tests: **69 were caught and 6 are equivalent changes** (section 8).

**Data and timing.** Machine: a cloud container, Intel Xeon at 2.10 GHz, 2 cores, GCC 13.3.0, `-O2`, one thread. Keys are 32-bit integers from `i * 2654435761` (distinct), shuffled, so the unbalanced tree built from them is random and its depth is about 2 ln n. Times are **run 1 / run 2** and a claim is made only when it holds in both; they are this machine's, not yours. Counts (bytes, allocations, exit codes) are identical in both runs.

---

## 1. The decisions

| Decision | Choice made here | Why, and what it costs |
|---|---|---|
| Child ownership | Raw pointers, one owner class | `unique_ptr` children are safe but their destructor recurses (section 2) |
| Node links | Parent pointer in every node | Iterators and erase need no stack; costs 8 bytes a node |
| Iterator | Node pointer plus tree pointer, 16 bytes | `--end()` needs the tree; stays valid across other inserts and erases |
| Allocation | `Alloc` template parameter, rebound to the node | One tree class for `new`, an arena or a pool |
| Insert | Search, then build the node, then link | The only step that can throw is in the middle, before anything changes |
| Copy | Iterative clone; on a throw free what was built | No recursion, strong guarantee |
| Constness | `iterator` is `const_iterator` | A set's keys must not change through an iterator |
| Balance | None | This topic is about the engineering; balancing is Topics 10 to 14 |

## 2. Ownership: `unique_ptr` children, and the recursion trap

The modern reflex is `std::unique_ptr<Node>` for each child: no `delete`, no leak, move-only by construction. It is the right choice for shallow, bounded trees (a parse tree, a config tree). Its hidden cost is that destruction is **recursive**: the destructor of a node runs the destructors of its two children, and so on, so freeing a chain of n nodes needs n nested calls. The listing builds a right chain, in O(n) with a tail pointer, and frees it in a child process with an 8 MB stack, once with the compiler's destructor and once with an explicit stack.

```cpp
// Ownership choice 1: unique_ptr children. Nothing leaks, nothing to write... until the tree is deep.
// With IterativeDtor == false the destructor is the compiler's: each node's destructor destroys its children, so freeing a chain of
// n nodes recurses n deep and overflows the stack. With true, the destructor takes the nodes apart with an explicit stack.
struct UNode { int key; std::unique_ptr<UNode> l, r; explicit UNode(int k) : key(k) {} };
template <bool IterativeDtor>
struct UTree {
    std::unique_ptr<UNode> root;
    bool insert(int k) {                                              // iterative, so building a deep tree is fine
        std::unique_ptr<UNode>* at = &root;
        while (*at) { if (k == (*at)->key) return false; at = k < (*at)->key ? &(*at)->l : &(*at)->r; }
        *at = std::make_unique<UNode>(k); return true;
    }
    ~UTree() {
        if (!IterativeDtor) return;                                  // root's own destructor then runs: recursive
        std::vector<std::unique_ptr<UNode>> st; if (root) st.push_back(std::move(root));
        while (!st.empty()) { auto n = std::move(st.back()); st.pop_back(); if (n->l) st.push_back(std::move(n->l)); if (n->r) st.push_back(std::move(n->r)); }
    }
};
```

Exit codes of the child process (`-11` is a kill by signal 11, a stack overflow):

| Nodes in the chain | Compiler's recursive destructor | Iterative destructor |
|---|---|---|
| 10,000 | exit 0 | exit 0 |
| 100,000 | exit 0 | exit 0 |
| 1,000,000 | killed by signal 11 | exit 0 |

A chain is the worst case an unbalanced BST can reach on sorted input, so "my tree is balanced on average" is not an answer for code that takes data from outside (an attacker can send sorted keys). The frame size and the stack limit are this machine's; the shape of the result, that some n crashes, is not. The raw-pointer design in section 3 never recurses: `destroy_all`, `copy_all` and `valid` all walk with parent pointers.

When is `unique_ptr` still right? When depth is bounded by construction (a red-black tree is at most 2 log2 n deep, about 80 levels for 2^40 keys), or when you give the type the explicit-stack destructor above. The cost of that destructor is that you now own the teardown code, which is exactly what the smart pointer was meant to save you.

## 3. The tree class

```cpp
// Ownership choice 2: raw node pointers and a class that owns them all. An (unbalanced) binary search tree set, written the way a library would:
//   - allocator-aware (std::allocator, or std::pmr::polymorphic_allocator for arenas and pools);
//   - nodes carry a parent pointer, so an iterator is just a node and erase keeps other nodes where they are;
//   - destruction, copying and iteration never recurse, so a chain of a million nodes is no problem;
//   - insert and copy have the strong exception guarantee: allocate and construct first, change the tree last.
template <class K, class Cmp = std::less<K>, class Alloc = std::allocator<K>>
class Bst {
    struct Node {
        K key; Node *l = nullptr, *r = nullptr, *p = nullptr;
        template <class... A> explicit Node(A&&... a) : key(std::forward<A>(a)...) {}
    };
    using NodeAlloc = typename std::allocator_traits<Alloc>::template rebind_alloc<Node>;
    using NT = std::allocator_traits<NodeAlloc>;
    Node* root_ = nullptr; size_t size_ = 0; Cmp cmp_; NodeAlloc al_;

    template <class... A> Node* make(A&&... a) {                      // allocate, then construct; if construction throws, give the memory back
        Node* n = NT::allocate(al_, 1);
        try { NT::construct(al_, n, std::forward<A>(a)...); } catch (...) { NT::deallocate(al_, n, 1); throw; }
        return n;
    }
    void drop(Node* n) { NT::destroy(al_, n); NT::deallocate(al_, n, 1); }
    void destroy_all(Node* n) {                                       // post-order by walking parent pointers: no recursion, no stack
        while (n) {
            if (n->l) n = n->l; else if (n->r) n = n->r;
            else { Node* p = n->p; if (p) (p->l == n ? p->l : p->r) = nullptr; drop(n); n = p; }
        }
    }
    Node* copy_all(const Node* s) {                                   // same shape, iteratively; on an exception frees what it built
        if (!s) return nullptr;
        Node* r = make(s->key); Node* d = r;
        try {
            for (;;) {
                if (s->l && !d->l) { d->l = make(s->l->key); d->l->p = d; s = s->l; d = d->l; }
                else if (s->r && !d->r) { d->r = make(s->r->key); d->r->p = d; s = s->r; d = d->r; }
                else { if (!s->p) break; s = s->p; d = d->p; }
            }
        } catch (...) { destroy_all(r); throw; }
        return r;
    }
    static Node* leftmost(Node* n) { while (n->l) n = n->l; return n; }
    static Node* rightmost(Node* n) { while (n->r) n = n->r; return n; }
    void transplant(Node* u, Node* v) { if (!u->p) root_ = v; else (u->p->l == u ? u->p->l : u->p->r) = v; if (v) v->p = u->p; }

public:
    class const_iterator {                                            // bidirectional; holds a node and the tree (the tree is needed for --end())
        const Node* n_ = nullptr; const Bst* t_ = nullptr; friend class Bst;
        const_iterator(const Node* n, const Bst* t) : n_(n), t_(t) {}
    public:
        using iterator_category = std::bidirectional_iterator_tag; using value_type = K; using difference_type = std::ptrdiff_t; using pointer = const K*; using reference = const K&;
        const_iterator() = default;
        reference operator*() const { return n_->key; }
        pointer operator->() const { return std::addressof(n_->key); }
        const_iterator& operator++() {                                // successor: leftmost of the right subtree, else the first ancestor we are left of
            if (n_->r) n_ = leftmost(const_cast<Node*>(n_->r));
            else { const Node* c = n_; while (c->p && c == c->p->r) c = c->p; n_ = c->p; }
            return *this;
        }
        const_iterator& operator--() {
            if (!n_) n_ = t_->root_ ? rightmost(t_->root_) : nullptr;     // from end(): the maximum
            else if (n_->l) n_ = rightmost(const_cast<Node*>(n_->l));
            else { const Node* c = n_; while (c->p && c == c->p->l) c = c->p; n_ = c->p; }
            return *this;
        }
        const_iterator operator++(int) { auto c = *this; ++*this; return c; }
        const_iterator operator--(int) { auto c = *this; --*this; return c; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.n_ == b.n_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.n_ != b.n_; }
    };
    using iterator = const_iterator;                                  // a set's elements are not to be changed through an iterator, so both are the same type
    using key_type = K; using value_type = K; using allocator_type = Alloc;

    Bst() = default;
    explicit Bst(const Alloc& a, const Cmp& c = Cmp()) : cmp_(c), al_(a) {}
    Bst(const Bst& o, const Alloc& a) : size_(o.size_), cmp_(o.cmp_), al_(a) { root_ = copy_all(o.root_); }
    Bst(const Bst& o) : Bst(o, NT::select_on_container_copy_construction(o.al_)) {}
    Bst(Bst&& o) noexcept : root_(std::exchange(o.root_, nullptr)), size_(std::exchange(o.size_, 0)), cmp_(std::move(o.cmp_)), al_(std::move(o.al_)) {}
    Bst& operator=(const Bst& o) { if (this != &o) { Bst t(o, get_allocator()); swap_contents(t); } return *this; }     // copy, then swap: strong guarantee
    Bst& operator=(Bst&& o) noexcept(NT::is_always_equal::value || NT::propagate_on_container_move_assignment::value) {
        if (this == &o) return *this;
        if constexpr (NT::is_always_equal::value || NT::propagate_on_container_move_assignment::value) {
            clear(); if constexpr (NT::propagate_on_container_move_assignment::value) al_ = std::move(o.al_);
            root_ = std::exchange(o.root_, nullptr); size_ = std::exchange(o.size_, 0);
        } else if (al_ == o.al_) { clear(); root_ = std::exchange(o.root_, nullptr); size_ = std::exchange(o.size_, 0); }
        else { Bst t(o, get_allocator()); swap_contents(t); o.clear(); }   // different arenas: the nodes cannot be handed over, so copy
        return *this;
    }
    ~Bst() { destroy_all(root_); }
    void swap_contents(Bst& o) noexcept { std::swap(root_, o.root_); std::swap(size_, o.size_); }   // only for equal allocators
    Alloc get_allocator() const { return Alloc(al_); }
    void clear() noexcept { destroy_all(root_); root_ = nullptr; size_ = 0; }
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    const_iterator begin() const { return {root_ ? leftmost(root_) : nullptr, this}; }
    const_iterator end() const { return {nullptr, this}; }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    const_iterator find(const K& k) const {
        for (const Node* n = root_; n;) { if (cmp_(k, n->key)) n = n->l; else if (cmp_(n->key, k)) n = n->r; else return {n, this}; }
        return end();
    }
    bool contains(const K& k) const { return find(k) != end(); }
    const_iterator lower_bound(const K& k) const {                    // first element not less than k
        const Node* best = nullptr;
        for (const Node* n = root_; n;) { if (cmp_(n->key, k)) n = n->r; else { best = n; n = n->l; } }
        return {best, this};
    }
    std::pair<const_iterator, bool> insert(const K& k) { return put(k); }
    std::pair<const_iterator, bool> insert(K&& k) { return put(std::move(k)); }
    template <class U> std::pair<const_iterator, bool> put(U&& k) {
        Node* parent = nullptr; Node** at = &root_;                   // 1. search; nothing is changed yet (the comparator may throw)
        while (*at) { parent = *at; if (cmp_(k, parent->key)) at = &parent->l; else if (cmp_(parent->key, k)) at = &parent->r; else return {{parent, this}, false}; }
        Node* n = make(std::forward<U>(k));                           // 2. build the node (may throw: the tree is untouched)
        n->p = parent; *at = n; ++size_;                              // 3. link it in (cannot throw)
        return {{n, this}, true};
    }
    const_iterator erase(const_iterator it) {                         // other iterators and pointers stay valid: nodes are relinked, never copied
        Node* z = const_cast<Node*>(it.n_); const_iterator next = it; ++next;
        if (!z->l) transplant(z, z->r);
        else if (!z->r) transplant(z, z->l);
        else {
            Node* y = leftmost(z->r);
            if (y->p != z) { transplant(y, y->r); y->r = z->r; y->r->p = y; }
            transplant(z, y); y->l = z->l; y->l->p = y;
        }
        drop(z); --size_; return next;
    }
    size_t erase(const K& k) { auto it = find(k); if (it == end()) return 0; erase(it); return 1; }

    // Stack iterator: the alternative to parent pointers. It needs no parent field in the node but holds the path from the root,
    // so it is bigger, allocates, and is tied to the tree shape: any insert or erase may invalidate it.
    class StackIter {
        std::vector<const Node*> st_;
        void push_left(const Node* n) { for (; n; n = n->l) st_.push_back(n); }
        friend class Bst;
    public:
        bool done() const { return st_.empty(); }
        const K& operator*() const { return st_.back()->key; }
        StackIter& operator++() { const Node* n = st_.back(); st_.pop_back(); push_left(n->r); return *this; }
    };
    StackIter stack_begin() const { StackIter s; s.push_left(root_); return s; }

    bool valid() const {                                              // order, parent links, count (iterative, so it also works on a chain)
        size_t c = 0; const Node* prev = nullptr; const Node* n = root_; if (n && n->p) return false;
        if (n) n = leftmost(const_cast<Node*>(n));
        while (n) {
            ++c; if (n->l && n->l->p != n) return false; if (n->r && n->r->p != n) return false;
            if (prev && !cmp_(prev->key, n->key)) return false;
            prev = n;
            if (n->r) n = leftmost(const_cast<Node*>(n->r)); else { const Node* x = n; while (x->p && x == x->p->r) x = x->p; n = x->p; }
        }
        return c == size_;
    }
};
```

Read it in this order: `make` and `drop` (the only places that allocate and free), `put` (search, build, link), `destroy_all` (post-order with no stack: go down to a leaf, free it, clear the parent's pointer, step up), `copy_all` (the same walk over the source and the copy together), `erase` (relink with `transplant`, then free), and the iterator. A run on a small tree (`50 30 70 20 40 60 80 45`):

```
built      : 20 30 40 45 50 60 70 80   (size 8, valid 1)
walk from 40 with ++ : 40 45 50
--end() is 80, then -- gives 70
erase(30) returns 40; iterator to 45 still reads 45 at the same address: yes
after erase: 20 40 45 50 60 70 80   (size 7, valid 1)
lower_bound(55) = 60, lower_bound(81) is end: yes
stack iterator: 20 40 45 50 60 70 80
sizeof(const_iterator) = 16
insert(9) threw bad_alloc; size still 3, live nodes 3, valid 1, contains 9: 0
copy of 3 nodes threw after 2 allocations; live nodes now 3 (only u's 3)
```

Things to notice: the iterator held on 45 still reads 45 at the same address after `erase(30)` removed a node with two children, because erase moves **nodes** (relinks them), never keys; and the failed `insert(9)` left the size, the live-node count and `valid()` exactly as before.

## 4. Two iterators

**Parent-pointer iterator** (`const_iterator`). `++` is "leftmost of the right subtree, else climb while you are a right child, then one more". It holds a node pointer and a tree pointer: 16 bytes, checked by a `static_assert`. The tree pointer exists only for `--end()`, which must find the maximum; the standard library's red-black tree avoids it by keeping a header node that `end()` points to (from memory; I did not read the library source here), which makes its iterator one pointer.

**Stack iterator** (`StackIter`). Keep the path from the root to the current node in a `vector`; `++` pops and pushes the left spine of the right child. It needs no parent field (smaller nodes), it is the shape LeetCode 173 asks for, but it allocates, it is bigger, and **any insert or erase can invalidate it**: it remembers the path, not the node.

| | Parent pointer | Stack |
|---|---|---|
| Node size | +8 bytes | unchanged |
| Iterator size | 16 bytes | a vector (24 bytes) plus its heap array |
| Extra memory while iterating | none | O(height) |
| Valid after other inserts and erases | yes | no |
| Bidirectional | yes | forward only here |
| `++` cost | amortised O(1), but climbs through old nodes | amortised O(1), nodes stay in the vector |

Measured, n = 1,000,000 random keys, best of 5, ns per element (the sums of all four walks agreed):

| Walk | Run 1 / run 2 |
|---|---|
| Parent-pointer iterator | 178.2 / 197.0 |
| Stack iterator | 57.3 / 62.0 |
| `std::set` | 162.9 / 176.9 |
| Sorted `vector` | 0.6 / 0.7 |

The stack walk was about three times faster than the parent-pointer walk in both runs, and the parent-pointer walk was within about 12% of `std::set`'s. My explanation, not something I measured with a profiler: when `++` climbs, it reads ancestors last touched long ago, which at a million random nodes are out of cache, while the stack iterator holds them in a small hot array. Both tree walks are far from the `vector` (about 0.6 ns), which is section 7's point.

## 5. Allocators: `new`, an arena, a pool

`Bst` takes an `Alloc` and rebinds it to its node type, so `std::allocator<K>` gives ordinary `new` and `std::pmr::polymorphic_allocator<K>` lets the **caller** pick the memory resource at run time. What the choices do is easiest to see by counting what reaches the upstream allocator. The workload: keep 1,000 keys live and replace a random one 1,000,000 times (erase one, insert a new one).

| Resource | Upstream allocations | Upstream bytes |
|---|---|---|
| Direct (like `new` and `delete`) | 1,001,000 | not applicable (peak live 32,000 bytes) |
| `monotonic_buffer_resource` | 24 | 34,475,072 |
| `unsynchronized_pool_resource` | 8 | 63,472 |

(The node is 32 bytes for a 4-byte key: key, padding, three pointers; that is the request size the counting resource saw.)

- **Direct** frees every node and reuses nothing: a million allocations, but the peak is the live set, 32,000 bytes.
- **`monotonic_buffer_resource`** never frees. 1,000,000 insertions took 34 MB for a tree that holds 1,000 keys: the arena is the right tool for "build, use, throw it all away", and the wrong one for a tree with erase in a long-lived loop.
- **`unsynchronized_pool_resource`** keeps freed nodes on free lists, so the same churn used 63,472 bytes in 8 upstream calls.

Three `pmr` rules worth knowing, each tested: a `polymorphic_allocator` does **not** propagate on copy construction (the copy gets the default resource, unless you pass `get_allocator()` explicitly, which is why the class has the two-argument copy constructor); it does not propagate on move assignment, so assigning between trees on **different** arenas cannot hand nodes over and must copy (the test checks that the target keeps its own arena and the source ends empty); and for equal arenas it steals in O(1). The class encodes this: its move assignment is `noexcept` only when the allocator is always-equal or propagates, and a `static_assert` checks that it is `noexcept` for `std::allocator` and not for `pmr`.

## 6. Exceptions and `const`

Which operations can throw, and what the caller can rely on afterwards:

| Operation | Can throw from | Guarantee | How |
|---|---|---|---|
| `insert` / `put` | comparator, allocation, key copy | strong | search, then build the node, then link (links and `++size_` cannot throw) |
| `erase(iterator)` | nothing | no-throw | relink, destroy, free |
| Copy constructor | allocation, key copy | strong | `copy_all` frees what it built and rethrows |
| Copy assignment | the same | strong | copy into a temporary, then `swap_contents` |
| Move constructor | nothing | no-throw (`noexcept`) | steal root and size |
| Move assignment | only the copying branch | conditionally `noexcept` | steal when allocators allow, else copy |
| Destructor | nothing | no-throw | key destructors must not throw |

The tests make these true or false in practice: a memory resource that throws after K allocations, for every K from 0 to 11, during a run of inserts, then during `operator=`, and a copy of a 50-node tree failing at each of its first 50 allocations; after every failure the tree is `valid()`, the **live node count equals the size** (nothing leaked) and, for copy construction, the live count returned to what it was. A key whose copy constructor throws on demand covers `make`'s catch block (the memory is returned) and copy assignment (the target keeps its old contents).

**Const-correctness.** `begin()`, `end()`, `find` and `lower_bound` are `const` and return `const_iterator`; `*it` is `const K&`, because changing a key in place would break the ordering. For a set this makes `iterator` and `const_iterator` the same type. (A map needs two types, since its mapped values may change and its keys may not.) Part of this is checked by the compiler, not by a run, with `static_assert`:

- the iterator category is `bidirectional_iterator_tag` and `*it` has type `const int&`;
- a `const Bst&` has no `insert` (a detection-idiom trait is false for the const type and true for the non-const one);
- the iterator is exactly two pointers; the move constructor is `noexcept`.

One honest smell: the iterator stores `const Node*` and `leftmost` takes `Node*`, so `++` uses a `const_cast`. It is safe (nothing is written through it), and storing `Node*` inside a type that only exposes `const K&` would remove it. The standard containers do the second.

## 7. Is a node-per-key tree the right container?

The comparison needs a competitor with a different shape. `BlockSet` is a B-tree-style container in two levels: a vector of sorted blocks of at most 2B = 512 keys and a vector of each block's maximum; find is two binary searches, insert is a binary search plus a shift inside one block, and a full block splits in two.

```cpp
// A B-tree-style container in two levels: a vector of sorted blocks of up to 2B keys, and a vector of each block's largest key.
// find and insert binary-search the block maxima, then the block; a full block splits in two. (No erase: this is for the comparison.)
template <class K, int B = 256>
struct BlockSet {
    std::vector<std::vector<K>> blocks; std::vector<K> maxes; size_t n = 0;
    size_t locate(const K& k) const { size_t b = std::lower_bound(maxes.begin(), maxes.end(), k) - maxes.begin(); return b == maxes.size() ? b - 1 : b; }
    bool contains(const K& k) const {
        if (blocks.empty()) return false;
        const auto& v = blocks[locate(k)]; auto it = std::lower_bound(v.begin(), v.end(), k); return it != v.end() && !(k < *it);
    }
    bool insert(const K& k) {
        if (blocks.empty()) { blocks.push_back({k}); maxes.push_back(k); n = 1; return true; }
        size_t b = locate(k); auto& v = blocks[b]; auto it = std::lower_bound(v.begin(), v.end(), k);
        if (it != v.end() && !(k < *it)) return false;
        v.insert(it, k); ++n; maxes[b] = v.back();
        if (v.size() > (size_t)2 * B) {                                // split: the upper half becomes a new block after this one
            std::vector<K> hi(v.begin() + B, v.end()); v.resize(B); maxes[b] = v.back();
            blocks.insert(blocks.begin() + b + 1, std::move(hi)); maxes.insert(maxes.begin() + b + 1, blocks[b + 1].back());
        }
        return true;
    }
    template <class F> void each(F f) const { for (const auto& v : blocks) for (const K& x : v) f(x); }
    size_t bytes() const { size_t s = blocks.capacity() * sizeof(blocks[0]) + maxes.capacity() * sizeof(K); for (auto& v : blocks) s += v.capacity() * sizeof(K); return s; }
};
```

Measured on random distinct keys. Insert and find are ns per operation (find: 1,000,000 random hits, best of 3), iterate is ns per element (best of 3). **Bytes per key**: for the tree types the bytes requested per node (what the counting resource saw, so malloc headers are not included); for the vector and `BlockSet` the capacity. `std::set` here is `std::pmr::set` over the counting resource so its bytes can be counted: its **insert** includes one virtual call per node, so do not compare its insert with the other trees' (find and iterate are not affected).

| n | Container | Insert | Find | Iterate | Bytes per key |
|---|---|---|---|---|---|
| 10,000 | `std::set` (pmr) | 99.5 / 99.4 | 95.1 / 100.1 | 10.8 / 10.6 | 40.0 |
|  | sorted `vector` | 149.0 / 227.3 | 70.7 / 73.5 | 0.3 / 0.3 | 6.6 |
|  | `BlockSet<uint32_t, 256>` | 85.1 / 81.5 | 74.9 / 76.6 | 0.3 / 0.3 | 6.9 |
|  | `Bst`, `std::allocator` | 85.4 / 116.5 | 79.9 / 100.1 | 11.3 / 13.1 | 32.0 |
|  | `Bst`, monotonic arena | 87.0 / 123.6 | 74.2 / 82.9 | 10.3 / 15.9 | same as `Bst`, plus arena slack |
| 100,000 | `std::set` (pmr) | 344.4 / 186.0 | 305.2 / 257.1 | 53.5 / 48.5 | 40.0 |
|  | sorted `vector` | 1,886.0 / 1,959.2 | 89.4 / 106.9 | 0.3 / 0.3 | 5.2 |
|  | `BlockSet<uint32_t, 256>` | 118.0 / 117.9 | 113.4 / 113.0 | 0.3 / 0.3 | 5.4 |
|  | `Bst`, `std::allocator` | 247.8 / 232.4 | 240.5 / 265.6 | 61.8 / 60.2 | 32.0 |
|  | `Bst`, monotonic arena | 217.2 / 198.6 | 200.2 / 209.7 | 51.2 / 41.3 | same as `Bst`, plus arena slack |
| 1,000,000 | `std::set` (pmr) | 528.9 / 541.4 | 874.2 / 868.7 | 113.7 / 114.3 | 40.0 |
|  | sorted `vector` | skipped (O(n) each); sort builds it at 62.2 / 70.7 per key | 157.9 / 194.1 | 0.3 / 0.4 | 4.0 |
|  | `BlockSet<uint32_t, 256>` | 151.8 / 169.4 | 174.3 / 197.8 | 0.4 / 0.5 | 5.8 |
|  | `Bst`, `std::allocator` | 704.4 / 699.1 | 871.4 / 884.9 | 150.6 / 121.4 | 32.0 |
|  | `Bst`, monotonic arena | 770.6 / 741.6 | 720.5 / 690.7 | 100.4 / 96.0 | same as `Bst`, plus arena slack |

What holds in both runs:

- **Sorted array search beats pointer chasing from 10^5 up.** At 10^6 the `vector` and `BlockSet` finds (about 160 to 200 ns) were more than four times faster than the `std::set` and `Bst` finds (about 870 ns): a random tree of 10^6 nodes is about 28 levels deep on average (2 ln n) and I expect most of those levels to be cache misses (not measured with a counter).
- **The block container is the practical middle.** At 10^6 its insert was more than three times faster than `std::set`'s (with the virtual-call caveat) and at 10^5 more than fifteen times faster than inserting into a plain `vector`, whose cost is the O(n) shift. Its find stayed within about 30% of the `vector`'s at every size.
- **Iteration is where nodes lose most.** Every tree walk was at least 30 times slower per element than the two array containers at 10^4 and worse after.
- **Memory.** 40 bytes a key for `std::set` and 32 for this tree against about 4 to 7 for the array containers, a factor of four or more.
- **This tree and `std::set` are in the same band.** Find was within a factor of 1.3 and insert within a factor of 2 either way at every size; neither is consistently ahead on find: both are one pointer-chase per level.

What it does **not** show: the tree is unbalanced and the keys random, so its depth is the friendly case (sorted input would make it a chain); I measured no erase for `BlockSet` (it has none); the keys are 4 bytes (larger keys move the crossover); and an arena versus `new` made no consistent difference in find here, so I draw no conclusion about it.

## 8. What the tests check, and what they cannot

Of the 75 mutants, the survivors that are genuinely equivalent are these, each with the reason:

- **Inverted destructor flag, and the teardown that skips pushing the right child** (two mutants): with a 20,000-node chain the compiler's recursive destruction does not overflow, so the test cannot tell. The fork test of section 2 does.
- **Freeing the left child before the right in `destroy_all`:** the order in which a post-order walk frees nodes is not observable.
- **The self-assignment guard replaced by `if (true)`:** copy-then-swap already handles `a = a`; the guard is an optimisation.
- **The root-has-no-parent check removed from `valid()`:** no public operation can give the root a parent.
- **`insert(K&&)` forwarding as a copy:** for `int` and for a key with a `noexcept` move the two cannot be told apart by the tests.

The two destructor mutants are worth a second look: the stack-overflow property is a property of **depth and the process stack**, which the unit test deliberately keeps small (20,000 nodes) so that it runs under AddressSanitizer; the crash is demonstrated, with its exit code, in the fork test of section 2 instead.

## 9. Choosing

- **Ordered iteration, erase and insert in the middle, stable addresses:** a node-based tree, balanced. Use `std::set` or `std::map` unless you need an intrusive node (Topic 13) or your own allocator policy.
- **Mostly reads, bulk loading, scans:** a sorted `vector`. Insertion only if n is small or the data arrives in batches.
- **Large ordered data with inserts:** a block or B-tree container (Topic 29); this is why databases and `absl::btree_map` exist.
- **A tree that lives for one phase and is freed whole:** nodes from a `monotonic_buffer_resource`; nothing goes back to the system until the arena dies (this class still walks the tree in its destructor; for trivially destructible keys that walk could be skipped).
- **A long-lived tree with churn:** a pool resource, or the default allocator.
- **Untrusted keys:** never a recursive destructor, copy, print or validator, and a balanced tree.

## 10. Common mistakes

- Recursive destructor, copy or comparison on a tree that can be a chain.
- Allocating the node, then changing the tree, then constructing the key: a throw in the middle leaves a half-changed tree. Build first, link last.
- `new Node(...)` in `put` without a catch around the key constructor, which leaks on throw (the test for it is a throwing key).
- Writing `operator=` as "clear, then copy": a throw loses the old contents. Copy first, then swap.
- Forgetting that `pmr` allocators do not propagate: `Bst b = a;` silently moves the copy to the default resource.
- An arena with erase in a loop: the memory never comes back.
- Holding a `StackIter`, or an iterator of any tree whose rebalancing moves keys, across a modification.
- Checking an iterator-validity claim only for insert: erase of the node you stand on invalidates it.

## 11. Questions to answer out loud

1. **Why does destroying a `unique_ptr` tree overflow the stack?** Each node's destructor runs its children's destructors, so the recursion depth is the tree height; a chain of 10^6 nodes overflowed an 8 MB stack here.
2. **How do you free a tree without recursion?** With parent pointers: go to a leaf, free it, clear the parent's link, step up; or with an explicit stack of owned nodes.
3. **What does a parent-pointer iterator cost and give?** 8 bytes a node and a 16-byte iterator; it is bidirectional, valid across other modifications, and needs no memory while walking.
4. **Why does `--end()` need the tree?** `end()` holds no node, so the maximum must be found from the root; a header node (as the standard library has, from memory) removes the need.
5. **What is the exception guarantee of insert, and how is it achieved?** Strong: search, then allocate and construct the node, then link; the link steps cannot throw.
6. **Why is copy assignment "copy then swap"?** The copy can fail and the target must keep its contents; the swap cannot fail.
7. **What does `pmr` change about copying and moving?** The allocator is not propagated on copy or move assignment, so a copy lands on the default resource unless told otherwise, and assignment across different arenas must copy.
8. **When do you use a monotonic arena and when a pool?** Arena: build and discard, no erase. Pool: churn, because freed nodes are reused (63 KB against 34 MB in the measured churn).
9. **Why is `iterator` the same type as `const_iterator` for a set?** The keys are immutable through any iterator; a map needs two because its values are mutable.
10. **When would you not use a binary tree at all?** For large mostly-read data (a sorted vector) or large ordered data with inserts (a B-tree style container): the node-per-key tree loses on cache misses and bytes per key.

## 12. Problems

**A.** In the trace, why is the iterator held on 45 still valid after `erase(30)`, and what would break it?

**B.** Write the iterative destructor for the `unique_ptr` tree in your own words and say why the explicit stack holds `unique_ptr`s and not raw pointers.

**C.** `put` is changed to `n = make(k); ++size_; link`, with `make` possibly throwing. Which of the tree's invariants is broken by a throw, and which test in this topic would catch it?

**D.** A tree of 10^6 4-byte keys: how many bytes do the nodes take here (32 per node), in `std::set` (40), and in a sorted vector? What are the ratios?

**E.** Choose the resource: (a) parse 50 MB of input into a tree, query it, drop it; (b) a connection table with about 1,000 live entries and constant churn; (c) a tree built on one thread and read on many.

**F.** Implement LeetCode 173, BST Iterator, with O(h) memory, then with O(1) memory, and say which of this topic's iterators each resembles.

**G.** Why can't you write `*it = 5` for this tree's iterator, and what would a `Bst` of pairs (a map) need that this class does not have?

**H.** Interview narration: you are asked to "implement a BST with an iterator". Give the ten-sentence answer: structure, iterator, complexity, exception safety, what you would add for production.

### Answers

**A.** `erase` relinks nodes: 30's successor, 40, is moved into 30's place and 30 itself is freed, so the node holding 45 is never touched and its address is unchanged. An iterator would break if the node it stands on is the one erased, or if the tree copied keys into other nodes (the "replace the key by the successor's and delete the successor" erase does exactly that, and invalidates the successor's iterator).

**B.** Pop a node from the stack, push its two children (moved out of the node), and let the popped node's `unique_ptr` die at the end of the iteration with no children left, so its destructor recurses nowhere. The stack holds `unique_ptr`s because they are the owners: moving a child out and then letting the stack element die is what frees it; a raw pointer would need a manual `delete`.

**C.** The size says one more node than the tree holds when `make` throws, so `size() == count of nodes` (checked by `valid()`) fails; the failing-resource test, which checks `valid()` and the live count after each failed insert, catches it.

**D.** 32,000,000 bytes here, 40,000,000 in `std::set` (1.25 times more), and 4,000,000 in a vector (8 times less than this tree).

**E.** (a) A monotonic arena (no erase, freed in one step). (b) A pool resource (free lists reuse the nodes; the monotonic arena would grow without bound). (c) Build in one arena, then share it read-only: no allocation happens during reads, and `Bst` const operations modify nothing, so concurrent readers are safe if nobody writes (this is a design argument, not tested here).

**F.** O(h): push the left spine; `next` pops, then pushes the left spine of its right child (this topic's `StackIter`). O(1) memory: climb with parent pointers (this topic's `const_iterator`), or Morris traversal (Topic 02), which temporarily rewires the tree and so cannot be used when readers share it.

**G.** `const_iterator::operator*` returns `const K&` and the tree is a set: changing a key could break the order. A map would store `pair<const K, V>` in the node and hand out a non-const iterator whose `->second` is mutable, which means two iterator types, a node type holding the pair, and `value_type` distinct from `key_type`.

**H.** Sample: "A node holds a key, left, right and parent. Insert walks down and links a new leaf; erase relinks (leaf, one child, or the successor); find and lower_bound walk down. The iterator is a node pointer: next is the leftmost of the right subtree or the first ancestor I am a left child of; `--end()` needs the root. All operations are O(h), which is O(log n) if the tree is balanced and O(n) on sorted input, so I would use a red-black or AVL tree in production. Destroy, copy and validate use parent pointers so a chain does not overflow the stack. Insert builds the node before linking so a throwing allocation or key copy leaves the tree unchanged; copy assignment is copy then swap. Take the allocator as a template parameter so callers can use an arena or a pool. Keys are const through iterators. I would test against `std::set` with a validator after every operation and inject allocation failures."

---

## Cheat sheet

- **Ownership:** `unique_ptr` children recurse on destruction: a 10^6 chain crashed with an 8 MB stack. Raw pointers plus an owner class and parent-pointer walks never recurse.
- **Iterator:** node plus tree pointer, 16 bytes, bidirectional, valid across other modifications; the stack iterator is O(h) memory, forward, invalidated by any change. Measured at 10^6: stack about 3 times faster than parent-pointer; parent-pointer about equal to `std::set`.
- **Allocators:** direct 1,001,000 allocations for the churn (peak 32 KB); monotonic 34 MB; pool 63 KB. `pmr` does not propagate on copy or move assignment.
- **Exceptions:** insert = search, build, link (strong); copy = clone and free on throw; copy assign = copy then swap; erase of an iterator does not throw.
- **Const:** set iterators are const iterators; check with `static_assert` (category, `const K&`, no `insert` on const, 16-byte iterator, `noexcept` move).
- **Containers at n = 10^6 (4-byte keys):** node trees 32 to 40 bytes a key and about 870 ns a find; sorted vector or block container about 4 to 7 bytes and about 160 to 200 ns; trees are at least 30 times slower to iterate.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- From memory and not checked against the library source in this session: the standard library's red-black tree keeping a header node that `end()` points to; `pmr::polymorphic_allocator` not propagating on copy, move assignment or swap (the copy and move behaviour is tested here); the `static_assert`-style detection idiom as a standard technique.
- The container comparison idea (a wide-node ordered container beating a node-per-key tree) is the B-tree argument of Topic 29; `absl::btree_map` is named from memory.
- Problems by name, from memory: LeetCode Binary Search Tree Iterator (173), Kth Smallest Element in a BST (230), Delete Node in a BST (450), Design HashMap (706) for the allocation and ownership questions.
