# DSA 07.13: The Linux Kernel rbtree: Intrusive Nodes, `rb_root_cached` and Augmented Trees

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (4 reading, 5 problems) · You need: [Red-black trees II](12_red_black_trees_deletion_and_full_class.md) (the cases and the invariants; this tutorial only changes how they are stored), [Lists Inside Real Systems](../04_linked_lists/06_systems_level_lists.md) (`container_of`) and [Inside `std::set` and `std::map`](09_inside_std_set_and_map.md) (the node-based baseline). There is no companion demo file: the port is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- explain why the kernel's tree is **intrusive**, use `rb_entry`/`container_of`, and say what that buys over `std::set` of pointers (with a measurement);
- write the kernel-style search and insert loop yourself, and say why the library does not do it for you;
- decode the packed `__rb_parent_color` word, and say why a red node's word is just its parent pointer;
- read the kernel's `rb_insert_color` and `rb_erase` cases against the cases of Topic 11 and Topic 12 (they are the same cases);
- use `rb_root_cached`, and say honestly how much it saves;
- build an **augmented** tree with the three callbacks (`propagate`, `copy`, `rotate`), and explain how the EEVDF scheduler uses one;
- name the usual mistakes that corrupt a kernel tree.

**Where the numbers come from.** I cannot compile the kernel here, so the code in this tutorial is a **userspace port written for this tutorial** from the documented behaviour of the kernel API and the structure of its `lib/rbtree.c` (read through public source mirrors and the kernel documentation, see Sources). It is not kernel source, and it keeps the kernel's names. What I tested is the port: 18,371,490 checks passed under AddressSanitizer with UBSan (GCC 13.3, `-Wall -Wextra`). They include every insertion order followed by every erase order of 1 to 6 keys (533,417 runs, rules checked through the packed word after each step, the cached leftmost compared with `rb_first` after each erase), every insertion order of 7 and 8 keys with every victim and two absent keys (448,560 cases), 150 random sequences against `std::map`, hostile orders up to 8,191 keys, a checker that must fail on damaged colours, parents and unused bits, and 300 random sequences in which this port and Topic 12's `RBTree` produced **the same tree, node for node and colour for colour** (and Topic 12 matched the libstdc++ `std::set` the same way). The augmented tree was checked after every sampled operation, and 72,000 picks were compared with brute force. Where I describe what the kernel does and the port does not show it, the text says "from memory" or names the source. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, glibc malloc, best of 3; read them as shapes, not benchmarks.

---

## 1. Why the kernel's tree is intrusive

`std::set<Task*>` stores, per element, a tree node that holds a *pointer* to the task. Finding a task means walking the nodes, and at every level following the pointer into the task to read its key: two dependent loads per level. The kernel avoids it by putting the tree node **inside the object**:

```
struct Task { long vruntime; int id; rb_node run; };   // 40 bytes: 8 + 4 (+4 padding) + 24
```

The same object can carry several nodes, one per structure it lives in (a run queue, a list of all tasks, a hash chain), exactly as with the `list_head` of Topic 04.06. A pointer to the embedded node is turned back into the task with `rb_entry(ptr, Task, run)`, which is `container_of`: subtract `offsetof(Task, run)`. Three consequences:

- **No allocation at insert time.** The memory for the node is part of the object that already exists, so inserting into a tree cannot fail with an out-of-memory error, which matters in code that runs with locks held or in interrupt context.
- **One cache line fewer per level.** The key sits next to the node.
- **The caller owns everything.** The library never allocates, frees or compares anything; it only rewires pointers and colours.

The measurements (1,000,000 distinct keys, the same keys as in Topic 12, 2,000,000 lookups, best of 3):

| | lookup, mean | heap bytes per element |
|---|---|---|
| intrusive tree of `Task` (this port) | 1061.3 ns | 48.0 (the task itself) |
| `std::set<long>` | 1173.7 ns | 48.0 |
| `std::set<Task*>` | 1925.9 ns | 48.0 (set node) + 48.0 (the task) |
| Topic 12 `RBTree<long,int>` | 1296.0 ns | 48.0 |

A second run gave 1015.8 / 1128.1 / 1880.5 / 1217.0 ns, the same order. The point is the third row: with the same tree shape (the port's height on these keys is 24, as in Topic 12) the set of pointers costs about 1.8 times as much per lookup as the intrusive tree, and twice the memory, because the task is a separate allocation. Against `std::set<long>` the intrusive tree is only about 10% faster, and that margin is within what I would not trust on one machine.

---

## 2. The interface, and who does what

| Name | What it does |
|---|---|
| `struct rb_node` | the link: parent and colour in one word, plus two child pointers (24 bytes on a 64-bit machine) |
| `struct rb_root` | one pointer to the top node |
| `struct rb_root_cached` | a root plus a pointer to the leftmost node |
| `rb_link_node(node, parent, link)` | hang a new red leaf at the place the caller found |
| `rb_insert_color(node, root)` | repair the colours after `rb_link_node` |
| `rb_erase(node, root)` | unlink a node and repair; the node is not freed |
| `rb_first`, `rb_last`, `rb_next`, `rb_prev` | in-order walk; `rb_next` and `rb_prev` need no stack and no root |
| `rb_replace_node(victim, new, root)` | put `new` where `victim` is, same colour, no rebalancing |
| `rb_insert_color_cached`, `rb_erase_cached`, `rb_first_cached` | the same for a cached root |
| `rb_insert_augmented`, `rb_erase_augmented` | the same with the three callbacks for augmented data |

The notable absence is **search and insert**. There is no `rb_find` and no `rb_insert(key)`: the caller writes the descent, compares its own keys with its own code, and then calls `rb_link_node` and `rb_insert_color` (listing in section 5). The kernel documentation gives the reason as speed and generality: a library function would need a comparison callback, which the compiler cannot inline, and the tree cannot know how the caller wants duplicates handled. (From memory: later kernels added `rb_add()` and `rb_find()` helpers that take inlined `less` and compare callbacks, so the loop is no longer always hand-written.) Here, keys are unique and a duplicate is rejected by the caller's loop, which is a policy only the caller can choose.

The library also has **no locking**. Every user serialises access itself (a spinlock, a mutex, or a seqlock-based variant; from memory).

---

## 3. The node: parent and colour in one word

```cpp
// A userspace port in the style of the Linux kernel rbtree (written for this tutorial,
// from the documented behaviour; not kernel source). The node is intrusive: it lives inside
// the user's own struct and the user recovers that struct with container_of.
struct rb_node {
    unsigned long pc;                  // parent pointer | colour in the lowest bit (0 red, 1 black)
    rb_node* right;
    rb_node* left;
} __attribute__((aligned(sizeof(long))));

struct rb_root { rb_node* node = nullptr; };
struct rb_root_cached { rb_root root; rb_node* leftmost = nullptr; };

enum : unsigned long { RB_RED = 0, RB_BLACK = 1 };

#define container_of(ptr, T, member) ((T*)((char*)(ptr) - offsetof(T, member)))
#define rb_entry(ptr, T, member) container_of(ptr, T, member)

inline rb_node* rb_parent(const rb_node* n) { return (rb_node*)(n->pc & ~3UL); }
inline bool rb_is_red(const rb_node* n) { return !(n->pc & 1); }
inline bool rb_is_black(const rb_node* n) { return n->pc & 1; }
inline void rb_set_parent_color(rb_node* n, rb_node* p, unsigned long c) { n->pc = (unsigned long)p | c; }
inline void rb_set_parent(rb_node* n, rb_node* p) { n->pc = (unsigned long)p | (n->pc & 1); }
inline void rb_set_black(rb_node* n) { n->pc |= RB_BLACK; }
inline void rb_set_red(rb_node* n) { n->pc &= ~1UL; }
inline void rb_copy_color(rb_node* to, const rb_node* from) { to->pc = (to->pc & ~1UL) | (from->pc & 1); }

#define RB_EMPTY_NODE(n) ((n)->pc == (unsigned long)(n))   // a node known not to be in a tree
#define RB_CLEAR_NODE(n) ((n)->pc = (unsigned long)(n))

// The caller finds the place with its own loop, then links the new (red) leaf there.
inline void rb_link_node(rb_node* node, rb_node* parent, rb_node** link) {
    node->pc = (unsigned long)parent;          // red, so the word is just the parent
    node->left = node->right = nullptr;
    *link = node;
}

inline void rb_change_child(rb_node* old, rb_node* neu, rb_node* parent, rb_root* root) {
    if (parent) { if (parent->left == old) parent->left = neu; else parent->right = neu; }
    else root->node = neu;
}

// Hooks for augmented trees; the plain tree passes empty ones.
struct rb_augment_callbacks {
    void (*propagate)(rb_node* node, rb_node* stop);   // refresh values from node upward, until stop or no change
    void (*copy)(rb_node* old, rb_node* neu);           // neu takes over old's position: copy the value
    void (*rotate)(rb_node* old, rb_node* neu);         // after a rotation: neu takes old's value, old recomputes
};
inline void rb_nop2(rb_node*, rb_node*) {}
static const rb_augment_callbacks rb_dummy = {rb_nop2, rb_nop2, rb_nop2};

using rb_link_t = rb_node* rb_node::*;                  // &rb_node::left or &rb_node::right

// Rotate at x: the child on side `down` moves up and x becomes its child on side `up`.
// Colours are left alone; the callers recolour.
inline void rb_rotate(rb_node* x, rb_link_t down, rb_link_t up, rb_root* root, void (*cb)(rb_node*, rb_node*)) {
    rb_node* y = x->*down;
    x->*down = y->*up;
    if (y->*up) rb_set_parent(y->*up, x);
    rb_node* p = rb_parent(x);
    y->pc = (unsigned long)p | (y->pc & 1);
    rb_change_child(x, y, p, root);
    y->*up = x;
    rb_set_parent(x, y);
    cb(x, y);
}
```

Reading the listing:

- `__attribute__((aligned(sizeof(long))))` guarantees that the address of every `rb_node` is a multiple of 8 (or of 4 on a 32-bit machine), so the **lowest bits of a parent pointer are zero**. The port asserts it (a test checks `alignof(rb_node)` and that a real node's address has its two low bits clear).
- The colour lives in bit 0 of `pc`: 0 is red, 1 is black. The parent is `pc & ~3` (bit 1 is also masked off, as in the kernel header: it is kept free; the checker requires it to be 0).
- Because **red is 0**, the word of a red node *is* its parent pointer. `rb_link_node` stores just the parent. Insertion creates red nodes, and the repair loop reads "my parent" from a red node without masking.
- The saving is real but small: Topic 12's node holds `key`, `val`, a `bool` colour and three pointers (40 bytes with an `int` value); the kernel node holds only the three words (24 bytes), and the key and value are the user's. Colour costs no extra byte.
- `RB_CLEAR_NODE` sets the word to the node's own address, a value no real parent can have. `RB_EMPTY_NODE` tests for it, and `rb_next`/`rb_prev` return null for such a node, so "is this task in the tree?" is a one-word test.
- `rb_rotate` is the rotation of Topic 10 with the same mirrored pointer-to-member trick as Topic 12; it fixes the parent words and the parent's child link, and then calls the augmentation hook `cb(old, new)`. A plain tree passes a no-op.

`struct rb_augment_callbacks` and `rb_dummy` are the hooks. The next sections only add the repair loops.

---

## 4. Insert: the cases of Topic 11 in the packed form

```cpp
// Repair after rb_link_node. Invariant: `node` is red.
inline void rb_insert_color(rb_node* node, rb_root* root, void (*cb)(rb_node*, rb_node*) = rb_nop2) {
    rb_node* parent = rb_parent(node);
    for (;;) {
        if (!parent) { rb_set_black(node); return; }              // node is the root
        if (rb_is_black(parent)) return;                          // nothing is violated
        rb_node* gp = rb_parent(parent);                          // a red parent is not the root
        bool pLeft = (parent == gp->left);
        rb_link_t dn = pLeft ? &rb_node::left : &rb_node::right;  // from gp to the parent
        rb_link_t in = pLeft ? &rb_node::right : &rb_node::left;
        rb_node* uncle = gp->*in;
        if (uncle && rb_is_red(uncle)) {                          // recolour, continue at the grandparent
            rb_set_black(uncle); rb_set_black(parent); rb_set_red(gp);
            node = gp; parent = rb_parent(node);
            continue;
        }
        if (node == parent->*in) {                                // bend: rotate it onto the outer side
            rb_rotate(parent, in, dn, root, cb);
            parent = node;
        }
        rb_rotate(gp, dn, in, root, cb);                          // line: rotate the grandparent
        rb_set_black(parent); rb_set_red(gp);
        return;
    }
}
```

The loop is the one of Topic 11, with the kernel's early exits:

| Situation | Kernel/port step | Topic 11 name |
|---|---|---|
| no parent | the node is the root: make it black | rule 2 |
| parent black | stop, nothing violated | no violation |
| parent red, uncle red | recolour parent, uncle, grandparent; continue from the grandparent | case 1 (recolour) |
| parent red, uncle black, node on the inner side | rotate at the parent so the node is on the outer side | case 2 (bend) |
| node on the outer side | rotate at the grandparent, recolour, stop | case 3 (line) |

Because the code uses pointers to members (`dn` is the side from the grandparent to the parent, `in` the other side), the left and right mirror images are one body. The kernel's C has two copies instead. A red parent always has a grandparent (the root is black), which is why `gp` needs no null test.

---

## 5. What the caller writes, and `rb_root_cached`

```cpp
// What a user of the tree writes. The search and the insert loop belong to the caller, so the
// comparison is ordinary inlined code on the caller's own key.
struct Task {
    long vruntime;                     // the sort key
    int id;
    rb_node run;                       // the tree node lives inside the object
};

inline Task* task_find(const rb_root* root, long key) {
    rb_node* n = root->node;
    while (n) {
        Task* t = rb_entry(n, Task, run);
        if (key < t->vruntime) n = n->left;
        else if (key > t->vruntime) n = n->right;
        else return t;
    }
    return nullptr;
}

inline bool task_insert(rb_root_cached* rc, Task* t) {      // false if the key exists
    rb_node** link = &rc->root.node;
    rb_node* parent = nullptr;
    bool leftmost = true;
    while (*link) {
        parent = *link;
        Task* cur = rb_entry(parent, Task, run);
        if (t->vruntime < cur->vruntime) link = &parent->left;
        else if (t->vruntime > cur->vruntime) { link = &parent->right; leftmost = false; }
        else return false;
    }
    rb_link_node(&t->run, parent, link);
    rb_insert_color_cached(&t->run, rc, leftmost);
    return true;
}
```

- The descent goes through `link`, a pointer *to the pointer* that will hold the new node: the root pointer for the first node, or a child pointer of `parent`. After the loop, `rb_link_node(node, parent, link)` is a three-line store. There is no special case for an empty tree.
- `leftmost` stays true only if every step went left. For a plain tree it is ignored.
- Keeping the whole thing in the caller's loop is what lets the compiler inline `t->vruntime < cur->vruntime`.

For a tree that is mostly asked for its smallest element (a scheduler picks the task with the smallest virtual runtime), the root can be **cached**:

```cpp
// rb_root_cached: the leftmost node is kept, so "the smallest" costs one load.
inline void rb_insert_color_cached(rb_node* node, rb_root_cached* rc, bool leftmost) {
    if (leftmost) rc->leftmost = node;
    rb_insert_color(node, &rc->root);
}
inline void rb_erase_cached(rb_node* node, rb_root_cached* rc) {
    if (rc->leftmost == node) rc->leftmost = rb_next(node);   // before the unlink
    rb_erase(node, &rc->root);
}
inline rb_node* rb_first_cached(const rb_root_cached* rc) { return rc->leftmost; }
```

`rb_erase_cached` moves the pointer to the successor *before* unlinking: once the node is out of the tree its links are no longer part of the contract. (In this port an erased node keeps its old words, so moving the call after the erase happens to give the same answer, and the mutation test that does it survives; the order is still the right one.) The port tests that after every single erase of the 533,417 small runs `rc.leftmost == rb_first(&rc.root)`.

What does the pointer save? In isolation a lot, in a real loop little:

| | uncached | cached |
|---|---|---|
| `rb_first` alone, tree of 1,000,000, hot path | 9.47 ns | 0.40 ns |
| pop the smallest, advance its key, reinsert (2,000,000 steps) | 496.3 ns | 475.1 ns |

The second run gave 8.83 / 0.42 ns and 438.7 / 434.3 ns. In the loop the saving was 4% in one run and 1% in the other, because the reinsertion (a descent through cold memory, a rebalancing) costs a few hundred nanoseconds and the walk to the leftmost node is only a small part of it. A `std::set::begin()` version of the same loop took 561.1 and 567.7 ns. So the cached root is a free, correct, O(1) shortcut, and its value grows when `rb_first` is called often *without* a modification in between (peeking at the next timer, say); do not expect a big win from the pop-and-reinsert pattern. Both loops produced the same sequence of popped keys (a checksum of 82,963,158,803,895 for the cached, the uncached and the `std::set` version).

---

## 6. Erase, in the kernel's shape

Topic 12 passed `x` and its parent `xp` separately. The kernel splits the work differently: `rb_erase_node` does the unlink and returns the node that is the **parent of the hole** (or null), and `rb_erase_color` then repairs from there with `node` starting as null.

```cpp
// Unlink `node`. The successor is relinked, never copied. Returns the node whose child
// position is one black short (the parent of the hole), or null if the colours still hold.
inline rb_node* rb_erase_node(rb_node* node, rb_root* root, const rb_augment_callbacks& cb) {
    rb_node *child = node->right, *tmp = node->left, *parent, *rebalance;
    unsigned long pc;
    if (!tmp) {                                        // no left child: the right child (maybe null) moves up
        pc = node->pc; parent = rb_parent(node);
        rb_change_child(node, child, parent, root);
        if (child) { child->pc = pc; rebalance = nullptr; }   // a lone child is red; it takes the colour
        else rebalance = (pc & 1) ? parent : nullptr;         // a black leaf leaves a hole
        tmp = parent;
    } else if (!child) {                               // only a left child, which is red
        tmp->pc = pc = node->pc; parent = rb_parent(node);
        rb_change_child(node, tmp, parent, root);
        rebalance = nullptr; tmp = parent;
    } else {                                           // two children: relink the successor
        rb_node *succ = child, *child2;
        tmp = child->left;
        if (!tmp) { parent = succ; child2 = succ->right; cb.copy(node, succ); }
        else {
            do { parent = succ; succ = tmp; tmp = tmp->left; } while (tmp);
            child2 = succ->right;
            parent->left = child2;
            succ->right = child;
            rb_set_parent(child, succ);
            cb.copy(node, succ);
            cb.propagate(parent, succ);                // the path the successor left behind
        }
        tmp = node->left; succ->left = tmp; rb_set_parent(tmp, succ);
        pc = node->pc; tmp = rb_parent(node);
        rb_change_child(node, succ, tmp, root);
        if (child2) {                                  // the successor was black with a red child
            succ->pc = pc; rb_set_parent_color(child2, parent, RB_BLACK); rebalance = nullptr;
        } else {
            unsigned long was = succ->pc; succ->pc = pc;
            rebalance = (was & 1) ? parent : nullptr;
        }
        tmp = succ;
    }
    cb.propagate(tmp, nullptr);                        // refresh the values above the change
    return rebalance;
}

// The hole is at `node` (null at first) below `parent`; the sibling is never null.
inline void rb_erase_color(rb_node* parent, rb_root* root, void (*cb)(rb_node*, rb_node*)) {
    rb_node* node = nullptr;
    for (;;) {
        bool nodeLeft = (parent->right != node);
        rb_link_t S = nodeLeft ? &rb_node::right : &rb_node::left;     // side of the sibling
        rb_link_t T = nodeLeft ? &rb_node::left : &rb_node::right;     // side of the hole
        rb_node* w = parent->*S;
        if (rb_is_red(w)) {                                            // case 1: make the sibling black
            rb_rotate(parent, S, T, root, cb);
            rb_set_black(w); rb_set_red(parent);
            w = parent->*S;
        }
        rb_node* far = w->*S;
        rb_node* near = w->*T;
        bool farRed = far && rb_is_red(far), nearRed = near && rb_is_red(near);
        if (!farRed && !nearRed) {                                     // case 2: push the hole up
            rb_set_red(w);
            if (rb_is_red(parent)) { rb_set_black(parent); return; }
            node = parent; parent = rb_parent(node);
            if (!parent) return;
            continue;
        }
        if (!farRed) {                                                 // case 3: bend into case 4
            rb_rotate(w, T, S, root, cb);
            w = near; far = w->*S;
        }
        rb_copy_color(w, parent);                                      // case 4: borrow, done
        rb_set_black(parent); rb_set_black(far);
        rb_rotate(parent, S, T, root, cb);
        return;
    }
}

inline void rb_erase(rb_node* node, rb_root* root) {
    if (rb_node* r = rb_erase_node(node, root, rb_dummy)) rb_erase_color(r, root, rb_nop2);
}
inline void rb_insert_augmented(rb_node* node, rb_root* root, const rb_augment_callbacks& cb) { rb_insert_color(node, root, cb.rotate); }
inline void rb_erase_augmented(rb_node* node, rb_root* root, const rb_augment_callbacks& cb) {
    if (rb_node* r = rb_erase_node(node, root, cb)) rb_erase_color(r, root, cb.rotate);
}
```

How it maps to Topic 12:

- **One child.** A node with one child must be black with a red child (Topic 12, problem D). The child takes the whole word of the erased node, parent and colour: `child->pc = pc`. One store replaces "recolour the child black".
- **No child.** A red leaf leaves no hole; a black leaf does, and the hole is below `parent`: `rebalance = parent`.
- **Two children.** The successor is relinked, not copied. If the successor has a right child, that child is red and the successor black: the child is blackened and takes the successor's old place, and no hole remains. Otherwise the hole exists exactly when the successor was black. The successor takes the erased node's word.
- **`rb_erase_color` cases**, in the kernel's numbering (from the comments in `lib/rbtree.c` as I read them): case 1 is the red sibling, case 2 the sibling with two black nephews, case 3 the near nephew red, case 4 the far nephew red. They are Topic 12's cases one to one. The kernel's case 2 has one extra line, the early exit: if the parent is red, make it black and stop, instead of moving the hole to the parent and looping once more (the loop would end there anyway; Topic 12's loop does it).
- **Case 3's recolouring is omitted** here. Topic 12 showed that its two assignments are overwritten by case 4's, so the port goes straight from the rotation to case 4. Whether the kernel keeps them I did not check; they cost nothing either way.
- **Callbacks.** `copy(node, succ)` gives the successor the augmented value of the position it takes over. `propagate(parent, succ)` refreshes the path the successor left. The final `propagate(tmp, nullptr)` refreshes everything above the change. Plain trees pass `rb_dummy`, whose hooks do nothing.
- `rb_erase` does not free the node. The caller owns the memory, and a freed task must first be erased.

The test result that matters: after any mix of inserts and erases, this tree is **identical to Topic 12's `RBTree`** (300 sequences, compared every fifth operation and at the end, with colours). Topic 12 matched `std::set`. The kernel algorithm and the textbook algorithm therefore build the same trees, at least for the code that I wrote from the kernel's description.

Two worked examples, from the port (the unlink and the repair printed separately). The tree built by inserting 1..7 is `2B 1B 4R 3B 6B 5R 7R`.

| Erase | After the unlink | Hole | After the repair |
|---|---|---|---|
| 4 (a red node with two children; its successor 5 is a red leaf) | `2B 1B 5R 3B 6B 7R` | none | unchanged: the successor took 4's colour and a red leaf left |
| 1, in the tree of 1..8 (`4B 2R 1B 3B 6R 5B 7B 8R`) | `4B 2R 3B 6R 5B 7B 8R` | below 2 | `4B 2B 3R 6R 5B 7B 8R`: case 2 (the sibling 3 turns red), and the parent 2 is red so it turns black |

---

## 7. Walking the tree

```cpp
inline rb_node* rb_first(const rb_root* root) { rb_node* n = root->node; if (!n) return nullptr; while (n->left) n = n->left; return n; }
inline rb_node* rb_last(const rb_root* root) { rb_node* n = root->node; if (!n) return nullptr; while (n->right) n = n->right; return n; }
inline rb_node* rb_next(const rb_node* node) {
    if (RB_EMPTY_NODE(node)) return nullptr;
    if (node->right) { node = node->right; while (node->left) node = node->left; return (rb_node*)node; }
    rb_node* p;
    while ((p = rb_parent(node)) && node == p->right) node = p;
    return p;
}
inline rb_node* rb_prev(const rb_node* node) {
    if (RB_EMPTY_NODE(node)) return nullptr;
    if (node->left) { node = node->left; while (node->right) node = node->right; return (rb_node*)node; }
    rb_node* p;
    while ((p = rb_parent(node)) && node == p->left) node = p;
    return p;
}
// Put `neu` where `victim` is, with the same colour; the caller guarantees the same sort position.
inline void rb_replace_node(rb_node* victim, rb_node* neu, rb_root* root) {
    rb_node* p = rb_parent(victim);
    rb_change_child(victim, neu, p, root);
    if (victim->left) rb_set_parent(victim->left, neu);
    if (victim->right) rb_set_parent(victim->right, neu);
    *neu = *victim;
}
```

`rb_next` is the successor rule of Topic 07: the leftmost node of the right subtree, or else the first ancestor reached from a left child. It uses the parent pointers and therefore no stack and no root. That is also why a tree with parent pointers can be walked and modified in a loop: take `rb_next(node)` *before* erasing `node`.

`rb_replace_node` copies the whole word, so colour and parent come with it, and the children's parent words are updated. It requires that `neu` sorts exactly where `victim` did, and it does not touch augmented values: that is how a task is swapped for an updated copy without any rebalancing.

---

## 8. Augmented trees, and where the kernel runs them

An **augmented** tree stores in each node a value computed from its subtree (a size, a maximum, a minimum), so that a query can skip whole subtrees. The value must be computable from the node and its two children alone. Three events can make a stored value stale, and the three callbacks cover them:

| Callback | When it runs | What it does |
|---|---|---|
| `propagate(node, stop)` | after a change below `node` (insert, erase) | recompute upward until `stop`, or until a recomputed value equals the stored one (then nothing above can change) |
| `copy(old, new)` | the successor takes over an erased node's place | `new` gets `old`'s stored value |
| `rotate(old, new)` | after every rotation | `new` (now on top) takes `old`'s value, since it covers the same set of nodes; `old` recomputes |

The kernel generates the three functions with the macro `RB_DECLARE_CALLBACKS` from one "compute" function and the field's name (from the header as I read it). The port writes them by hand for one concrete use, shaped like the EEVDF run queue: sorted by a key, with the smallest **deadline** of each subtree stored in the node.

```cpp
// An augmented tree in the shape of the EEVDF run queue: sorted by `key` (the virtual runtime),
// every node also stores the smallest `deadline` in its subtree.
struct Ent {
    long key;
    long deadline;
    long min_deadline;                 // augmented: min of deadline over this node's subtree
    rb_node node;
};

inline long ent_min(const Ent* e) {    // computed from the node and its children only
    long m = e->deadline;
    if (e->node.left) { long c = rb_entry(e->node.left, Ent, node)->min_deadline; if (c < m) m = c; }
    if (e->node.right) { long c = rb_entry(e->node.right, Ent, node)->min_deadline; if (c < m) m = c; }
    return m;
}
inline void ent_propagate(rb_node* n, rb_node* stop) {
    while (n != stop) {
        Ent* e = rb_entry(n, Ent, node);
        long m = ent_min(e);
        if (e->min_deadline == m) break;                 // nothing above can change either
        e->min_deadline = m;
        n = rb_parent(n);
        if (!n) break;
    }
}
inline void ent_copy(rb_node* old, rb_node* neu) { rb_entry(neu, Ent, node)->min_deadline = rb_entry(old, Ent, node)->min_deadline; }
inline void ent_rotate(rb_node* old, rb_node* neu) {
    Ent* o = rb_entry(old, Ent, node);
    rb_entry(neu, Ent, node)->min_deadline = o->min_deadline;   // the new top covers the same set
    o->min_deadline = ent_min(o);
}
static const rb_augment_callbacks ent_cb = {ent_propagate, ent_copy, ent_rotate};

inline void ent_insert(rb_root* root, Ent* e) {
    rb_node** link = &root->node;
    rb_node* parent = nullptr;
    while (*link) {
        parent = *link;
        link = (e->key < rb_entry(parent, Ent, node)->key) ? &parent->left : &parent->right;
    }
    e->min_deadline = e->deadline;
    rb_link_node(&e->node, parent, link);
    if (parent) ent_propagate(parent, nullptr);                  // fold the new leaf into the ancestors
    rb_insert_augmented(&e->node, root, ent_cb);
}
inline void ent_erase(rb_root* root, Ent* e) { rb_erase_augmented(&e->node, root, ent_cb); }

// The entity with the smallest deadline among those with key <= V (the "eligible" ones):
// one descent, and a second one inside the winning left subtree.
inline Ent* ent_pick(const rb_root* root, long V) {
    Ent* best = nullptr;
    rb_node* sub = nullptr; long subMin = 0;
    for (rb_node* n = root->node; n;) {
        Ent* e = rb_entry(n, Ent, node);
        if (e->key > V) { n = n->left; continue; }      // not eligible: only smaller keys can be
        if (!best || e->deadline < best->deadline) best = e;
        if (n->left) {                                  // the whole left subtree is eligible
            long m = rb_entry(n->left, Ent, node)->min_deadline;
            if (!sub || m < subMin) { sub = n->left; subMin = m; }
        }
        n = n->right;
    }
    if (sub && (!best || subMin < best->deadline)) {
        for (rb_node* n = sub;;) {
            Ent* e = rb_entry(n, Ent, node);
            if (e->deadline == subMin) return e;
            n = (n->left && rb_entry(n->left, Ent, node)->min_deadline == subMin) ? n->left : n->right;
        }
    }
    return best;
}
```

How the pick works: keys up to `V` are **eligible**. Walking down from the root, a node whose key exceeds `V` is skipped together with its right subtree. An eligible node is a candidate, and its *entire left subtree* is eligible too, so its stored `min_deadline` says whether a better deadline hides there without looking. At the end, if a left subtree beat the best single candidate, one more descent inside it follows the child whose stored minimum equals the target. Two descents, so `O(log n)`.

The checks: after each sampled operation of 120 random sequences (with key ranges 30, 400 and 100,000) the stored value of every node equalled the value recomputed from its children, the tree obeyed the rules, and 10 picks per sample matched a brute-force scan (72,000 picks in all).

The cost on 1,000,000 entries, per erase-and-reinsert pair: **1796.8 ns plain against 1862.3 ns augmented** (a second run: 1698.8 and 1760.8), so the augmentation added about 3.6% in both runs. A pick took 1323.7 ns (a second run: 1403.3 ns), about the cost of one lookup. (The measured plain tree here also uses `rb_erase_cached`, so the two are not identical in anything but the augmentation.)

**Where the kernel does this** (the first is from the patches I read; the others are from memory and marked):

- **The scheduler.** In Linux 6.6 the fair scheduler became EEVDF. Each runnable entity is in an rbtree (`cfs_rq->tasks_timeline`, a cached root) ordered by virtual runtime, with `min_deadline` stored per node and updated by `RB_DECLARE_CALLBACKS`-generated callbacks; the pick descends as above, with eligibility meaning non-negative lag. A November 2023 patch series (Abel Wu) re-sorted the tree by deadline and stored `min_vruntime` instead, so the pick prunes by eligibility in one pass and the cached leftmost serves as a fast path. Both designs are augmented rbtrees. The exact release that carries the second one I did not verify.
- **Interval trees** (from memory): the augmented value is the largest interval end in the subtree, so an overlap query skips subtrees that end before the query starts. Topic 30 builds one.
- **Timers** (from memory): the high-resolution timer queue keeps timers in a cached rbtree ordered by expiry, so the next timer is the cached leftmost node.
- **Address spaces** (the plan's source list): a process's memory areas lived in an rbtree plus a list until the maple tree replaced them in Linux 6.1; Topic 30 returns to the maple tree.

---

## 9. The checker, and what the tests cover

```cpp
// The rules, read back through the packed word. `lt(a, b)` says a sorts before b.
// Returns the black height, or -1 on any violation. `count` receives the number of nodes.
struct rb_report { bool ok; int black_height; std::size_t count; };
inline int rb_walk(const rb_node* t, const rb_node* parent, bool (*lt)(const rb_node*, const rb_node*),
                   const rb_node* lo, const rb_node* hi, std::size_t& count) {
    if (!t) return 0;
    if (rb_parent(t) != parent || (t->pc & 2)) return -1;           // parent link right, bit 1 unused
    if (lo && !lt(lo, t)) return -1;
    if (hi && !lt(t, hi)) return -1;
    if (rb_is_red(t) && ((t->left && rb_is_red(t->left)) || (t->right && rb_is_red(t->right)))) return -1;
    int a = rb_walk(t->left, t, lt, lo, t, count);   if (a < 0) return -1;
    int b = rb_walk(t->right, t, lt, t, hi, count);  if (b < 0) return -1;
    if (a != b) return -1;
    ++count;
    return a + (rb_is_black(t) ? 1 : 0);
}
inline rb_report rb_check(const rb_root* root, bool (*lt)(const rb_node*, const rb_node*)) {
    rb_report r{false, 0, 0};
    if (!root->node) { r.ok = true; return r; }
    if (rb_is_red(root->node)) return r;
    int bh = rb_walk(root->node, nullptr, lt, nullptr, nullptr, r.count);
    r.black_height = bh; r.ok = bh >= 0;
    return r;
}
```

The checker reads colours and parents through the packed word and verifies rules 2, 4 and 5, key order (through the caller's `lt`), every parent link, the unused bit and the node count. The tests include damage that only a packed-word tree can have: a wrong parent in the high bits, bit 1 set, and a flipped colour bit.

Beyond that: the exhaustive runs of section 0, equality with `RBTree` and `std::map`, the cached pointer after every erase, `rb_replace_node` (same preorder with colours before and after, and `RB_EMPTY_NODE` after `RB_CLEAR_NODE`), and the augmented tree after every sampled operation.

---

## 10. Common mistakes

1. **Forgetting that the library never frees.** `rb_erase` unlinks; freeing is yours, and only after the erase.
2. **Erasing a node that is not in the tree**, or twice. There is no check; the parent pointers are garbage. `RB_CLEAR_NODE` on removal and `RB_EMPTY_NODE` before a second erase makes the mistake detectable.
3. **Changing the key of a node while it is in the tree.** The position stays; the order breaks. Erase, change, reinsert (the scheduler loop of section 5).
4. **Using plain `rb_erase` on a cached root.** The leftmost pointer then dangles. Use `rb_erase_cached` for every erase on that tree.
5. **Linking with a stale `parent` or `link`.** The descent and `rb_link_node` must be under one lock with no modification between them.
6. **Wrong `rb_entry` member.** A node embedded twice needs the right member name; the conversion only subtracts an offset.
7. **Augmented value not refreshed before `rb_insert_color`.** The new leaf's own value and its ancestors must be right before the rotations start, or `rotate(old, new)` copies a stale number. The port calls `propagate` first.
8. **Using `rb_replace_node` with a key that sorts differently.** There is no rebalancing and no check.
9. **Assuming a thread-safe tree.** There is none; concurrent readers need the caller's own scheme (RCU variants exist; from memory).
10. **Calling `rb_next` after erasing the node.** Take the successor first.

---

## 11. Questions to answer out loud

1. What is `container_of`, and why does an intrusive tree need it?
2. What did the measurement of the set of pointers show, and why?
3. Why does the kernel make you write the search loop? Name one cost and one benefit.
4. What is in `__rb_parent_color`, and why is a red node's word equal to its parent pointer?
5. Which bits are free, and what guarantees it?
6. Match the kernel's four erase cases to Topic 12's cases and say what the extra early exit does.
7. What does `rb_erase_node` return, and what is the hole?
8. When does `rb_root_cached` help, and when is the gain small? Quote the measurement.
9. Which three events make an augmented value stale, and what does each callback do?
10. In the EEVDF pick, why is the whole left subtree of an eligible node eligible?

---

## 12. Problems

**A. The word.** A node at address `0x7f00` has parent `0x7e00`. What is `pc` if the node is red, and what if it is black? Why can the same trick not be used with a one-byte alignment?

**B. Trace.** In the tree `2B 1B 4R 3B 6B 5R 7R` (insert 1..7), erase 4 with the port and give the tree. Which node is the successor, what colour does it take, and why is there no hole?

**C. Trace with a hole.** In the tree `4B 2R 1B 3B 6R 5B 7B 8R`, erase 1. What does `rb_erase_node` return, which case runs, and why does the repair stop at once?

**D. The sibling.** Why can `rb_erase_color` read `parent->*S` without a null test?

**E. A size augmentation.** The augmented value is the number of nodes in the subtree (for rank queries). Say what `propagate`, `copy` and `rotate` do, and whether `propagate` can stop early.

**F. Choosing.** Choose for each case and defend it with the measurements here or in earlier topics: (a) a run queue where the code mostly takes the smallest and puts it back; (b) a table of 1,000,000 keys built once and read many times; (c) objects that must be in three different indexes at once.

**G. The pick.** Entities (key, deadline) are (10, 50), (20, 30), (30, 70), (40, 20), (50, 40) in an augmented tree. Which entity does `ent_pick` return for V = 5, 15, 35, 45 and 55?

**H. Mistakes.** The scheduler code calls `rb_erase` (not `_cached`) on the leftmost task and later `rb_first_cached`. What happens, and what is the fix?

---

## Answers

**A.** Red: `pc = 0x7e00` (parent, colour bit 0). Black: `pc = 0x7e01`. With one-byte alignment the low bit of an address can be 1, so the colour bit would corrupt the parent pointer. The trick needs a guarantee that the low bits are zero, which `aligned(sizeof(long))` gives.

**B.** The node 4 is red, with children 3 and 6. Its successor is 5, the leftmost node of 6's subtree. 5 is a red leaf and 4 is red; the successor takes 4's word, so it becomes red and 4's place. No black node left any path, so there is no hole and `rb_erase_node` returns null. The tree is `2B 1B 5R 3B 6B 7R`. (This is the first row of the table in section 6.)

**C.** 1 is a black leaf, so the function returns its parent, 2. The sibling 3 is black with two null nephews: case 2, which turns 3 red. The parent 2 is red, so it is turned black and the repair ends: `4B 2B 3R 6R 5B 7B 8R`. The black height of the left side is unchanged because the black on 2 replaces the black that left with 1.

**D.** The side of the hole is one black short. The other side must contain at least one black node on every path, so the sibling exists. (Topic 12, section 3.)

**E.** The value of a node is `1 + size(left) + size(right)`. `rotate(old, new)`: `new` takes `old`'s size, `old` recomputes. `copy(old, new)`: `new` takes `old`'s size. `propagate`: recompute upward. It can never stop early on an insert or an erase, because the size of every ancestor changes by one, so the "unchanged" test never fires until the root; the early stop is a speed-up for values like a minimum or a maximum, where a change may not reach the ancestors.

**F.** (a) The cached tree: `rb_first_cached` is a load. But measure the whole loop: it saved only 1% to 4% in section 5, since reinsertion dominates. A binary heap or a timing wheel may serve better when only the minimum matters (not measured here). (b) A sorted array with binary search: Topic 09 measured 184.2 ns against 1122.6 ns for `std::set`; nothing changes after the build. (c) The intrusive tree, with one `rb_node` per index in the object: no allocation, no duplicate objects, and the measured lookup was 1.8 times faster than a set of pointers.

**G.** V = 5: none (no key is at most 5). V = 15: key 10, deadline 50 (only one eligible). V = 35: eligible keys 10, 20, 30 with deadlines 50, 30, 70: key 20, deadline 30. V = 45: eligible 10 to 40, deadlines 50, 30, 70, 20: key 40, deadline 20. V = 55: all eligible; the smallest deadline is 20 at key 40. (Program output; the root's `min_deadline` is 20.)

**H.** `rb_erase` does not touch `leftmost`, so `rc.leftmost` still points at the erased task. The next `rb_first_cached` returns a task that is not in the tree (or freed memory). The fix is `rb_erase_cached`, which moves the pointer to `rb_next(node)` first, for every erase on that tree.

---

## Cheat sheet

- Intrusive: the `rb_node` is a member; `rb_entry`/`container_of` gets the object back. No allocation, caller owns memory, one object can sit in several trees. Measured: lookups in the intrusive tree 1061.3 ns against 1925.9 ns for `std::set<Task*>`.
- `rb_node` = parent|colour word + two children, aligned to `sizeof(long)`; colour in bit 0 (red 0, black 1); `pc & ~3` is the parent; a red node's word is its parent.
- The caller writes search and insert; then `rb_link_node` and `rb_insert_color`. Erase: `rb_erase` (unlink and repair, never frees).
- Same cases as Topic 11 and Topic 12; the port's trees are identical to `RBTree`'s (300 sequences) and so to `std::set`'s.
- `rb_root_cached`: leftmost kept; `rb_first` 9.47 ns to 0.40 ns, but in a pop-and-reinsert loop only 1% to 4%. Always erase with the `_cached` form.
- Augmented: `propagate`, `copy`, `rotate`; the value is computed from a node and its children. Overhead measured at about 3.6% per churn pair.
- EEVDF (Linux 6.6): an augmented, cached rbtree (`min_deadline`; later patches: sorted by deadline with `min_vruntime`).
- No locking; free after erase; never change a key in place.

---

## Sources

- Linux kernel documentation, "Red-black Trees (rbtree) in Linux" (`docs.kernel.org/core-api/rbtree.html`): the interface and the caller-written search loops. I could not open the page in this environment, so the interface description follows the header and source mirrors below and my memory of the documentation.
- Public mirrors of `include/linux/rbtree.h`, `include/linux/rbtree_augmented.h` and `lib/rbtree.c`, read for the node layout (3 words, aligned to `sizeof(long)`, `RB_EMPTY_NODE`), the colour encoding (red 0, black 1, low bits masked), the insert and erase case structure, and the callback contract: [rbtree.h (Android common kernel)](https://android.googlesource.com/kernel/common/+/android-trusty-3.10/include/linux/rbtree.h), [lib/rbtree.c (linux-imx mirror)](https://rtime.felk.cvut.cz/gitweb/linux-imx.git/blob/1b1c7409b75a8b62906b78b84c8469002072b738:/lib/rbtree.c), [rbtree_augmented.h (CentOS kernel mirror)](https://git.stg.centos.org/source-git/kernel/blob/c8s/f/include/linux/rbtree_augmented.h). The mirrors are older trees; newer kernels changed details I did not check.
- EEVDF: [Peter Zijlstra's "Implement an EEVDF like policy" patch](https://patchew.org/linux/20230531115839.089944915@infradead.org/20230531124603.931005524@infradead.org) (rbtree by vruntime, `min_deadline`, `pick_eevdf`), and [Abel Wu, "Sort the rbtree by virtual deadline"](https://lkml.iu.edu/hypermail/linux/kernel/2311.0/05395.html) (the later change to `min_vruntime`).
- CLRS for the cases (from memory), and Topics 07, 09, 10, 11 and 12 of this series.
- Marked "from memory" in the text: `rb_add`/`rb_find`, locking schemes, interval trees, timers.
- All other claims in this tutorial come from the tests and measurements described at the top.
