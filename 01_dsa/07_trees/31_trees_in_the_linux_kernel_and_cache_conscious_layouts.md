# DSA 07.31: Trees Inside the Linux Kernel, and Cache-Conscious Layouts

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (5 reading, 3 problems) · You need: [B-trees and B+ trees](29_b_trees_and_b_plus_trees.md) (wide nodes), [Binary tries, radix trees and longest-prefix match](18_binary_tries_radix_trees_and_lpm.md) (a radix tree is the XArray's shape), [The Linux kernel rbtree](13_linux_kernel_rbtree.md) (what the maple tree replaced) and [Augmented trees](30_augmented_trees_order_statistics_and_interval_trees.md) (the gap bookkeeping the VMA tree needed). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

The red-black tree of Topic 13 stores one key per node and reaches it through about 2 log2 n pointers, each a likely cache miss, and a writer rotates several nodes that a lockless reader may be standing on. The kernel's newer trees answer both problems the same way: **wide nodes** (16 to 64 slots in a node, so a lookup touches a handful of cache lines) and **replace, do not edit** (so a reader under RCU sees either the old node or the new one). This topic builds a small model of each of the two kernel structures, the **XArray** (index to pointer, a 64-ary radix tree) and the **maple tree** (ranges to pointers, a copy-on-write B-tree), and then looks at the same idea for **static** data: the **Eytzinger**, **van Emde Boas** and **S-tree** layouts, compared with a cache model and a stopwatch.

By the end you can:

- explain why wide nodes and replace-on-write beat a binary tree for the kernel's workloads (page cache, memory-map lookups) and say what each costs;
- write an XArray-style radix tree with marks and a `next` that skips empty ranges 64 slots at a time, and give its height for any index;
- write a range tree in which gaps are entries, a store overwrites a range, and an old root stays valid as a snapshot;
- build the three static layouts, explain the cache-line counts of each, and say what each is good for;
- mark honestly which statements about the kernel are from memory.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 1,963,565 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: `XArray` against `std::map` and `std::set` over random mixes of every operation, with indexes from 0 to 2^64 - 1 and at every level boundary, validated after every operation; `RangeMap` (fan-out 4 and 16) against per-point arrays over random overwrites, with every old root re-checked at the end; `Eytzinger` and `STree` against `std::lower_bound` for every size from 0 to 300 and around 4,096 and 65,536, with duplicates and queries on, below and above every end; and the exact vEB orders of heights 4 and 5. Finally 70 hand-made mutants of the three listings were run against the tests: **67 were caught and 3 are equivalent changes** (listed in section 7).

**Data and timing.** All data is **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, 2 cores, GCC 13.3.0, `-O2`, no `-march`. Times are shown as **run 1 / run 2** and a claim is made only when it holds in both; they are this machine's, not yours. The **cache-line counts are not timings**: a model (section 4) produces them, identically in both runs.

---

## 1. Why wide nodes, and why replace instead of edit

A binary node holds one key and two pointers, so a million keys are 20 to 40 pointer hops deep, most of them cache misses. A node of 16 slots (256 bytes in the maple tree; from memory) or 64 slots (the XArray) holds the same keys in 4 to 5 levels, and a search inside a node scans memory that is already loaded. This is Topic 29's argument with the page replaced by a few cache lines.

The second argument is concurrency. The kernel's hot lookups (the page cache, finding the VMA for a faulting address) must run without taking a lock. A reader walking a red-black tree can be standing on a node while a writer rotates it away. If the writer instead **builds a replacement node, publishes it with one pointer store, and frees the old node only after every reader that could hold it has finished** (an RCU grace period), a reader sees the old node or the new one, never a half-edited one. Wide nodes make this affordable: replacing one node of 16 slots is one allocation, not a cascade.

Both structures below are **models**: they have the shape and the update rule of the kernel structure, not its code, its locking, or its full feature set. Statements about the kernel itself are marked *(from memory)*, not re-read for this file.

## 2. The XArray: a 64-ary radix tree

An XArray maps an `unsigned long` index to an entry. The index is cut into 6-bit digits; a node has 64 slots; the root's height is the least that covers the largest index stored, so index 0 to 63 needs one level, 64 to 4,095 two, up to 2^64 - 1 eleven (6 x 11 = 66 bits). A lookup is one shift, mask and load per level and no comparison. Empty ranges cost nothing: a slot is empty or points at a node.

```cpp
// A 64-ary radix tree over 64-bit indexes (the shape of the kernel's XArray; this is a model, not kernel code).
// A node with shift s covers 64^(s/6+1) indexes; its slot i covers the indexes whose bits s..s+5 equal i.
// Per node: `used` has bit i when slot i holds something, `mark` has bit i when that slot (or the subtree under it) holds a marked entry.
struct XArray {
    static constexpr int FAN = 64;
    struct Node {
        int shift; uint64_t used = 0, mark = 0; uint64_t val[FAN] = {}; Node* kid[FAN] = {};
        explicit Node(int sh) : shift(sh) {}
    };
    Node* head = nullptr; size_t count = 0;
    ~XArray() { drop(head); }
    static void drop(Node* n) { if (!n) return; if (n->shift) for (uint64_t b = n->used; b; b &= b - 1) drop(n->kid[__builtin_ctzll(b)]); delete n; }
    static uint64_t span(int shift) { return shift + 6 >= 64 ? ~0ull : (1ull << (shift + 6)) - 1; }   // largest index a node of this shift covers
    static int slot(const Node* n, uint64_t i) { return (int)((i >> n->shift) & 63); }

    void grow(uint64_t i) {                        // make the tree tall enough for index i
        if (!head) { int sh = 0; while (i > span(sh)) sh += 6; head = new Node(sh); return; }
        while (i > span(head->shift)) { Node* t = new Node(head->shift + 6); t->kid[0] = head; t->used = 1; t->mark = head->mark ? 1 : 0; head = t; }
    }
    void store(uint64_t i, uint64_t v) {
        grow(i); Node* n = head;
        for (;;) {
            int s = slot(n, i); uint64_t b = 1ull << s;
            if (!n->shift) { if (!(n->used & b)) ++count; n->used |= b; n->val[s] = v; return; }
            if (!(n->used & b)) { n->kid[s] = new Node(n->shift - 6); n->used |= b; }
            n = n->kid[s];
        }
    }
    std::optional<uint64_t> load(uint64_t i) const {
        if (!head || i > span(head->shift)) return std::nullopt;
        const Node* n = head;
        for (;;) { int s = slot(n, i); if (!((n->used >> s) & 1)) return std::nullopt; if (!n->shift) return n->val[s]; n = n->kid[s]; }
    }
    bool erase(uint64_t i) {
        if (!head || i > span(head->shift) || !erase_in(head, i)) return false;
        --count;
        if (!head->used) { delete head; head = nullptr; }
        else while (head->shift && head->used == 1) { Node* k = head->kid[0]; delete head; head = k; }   // the root has only slot 0: drop a level
        return true;
    }
    static bool erase_in(Node* n, uint64_t i) {
        int s = slot(n, i); uint64_t b = 1ull << s;
        if (!(n->used & b)) return false;
        if (!n->shift) { n->used &= ~b; n->mark &= ~b; return true; }
        Node* c = n->kid[s]; bool had = erase_in(c, i);
        if (had) { if (!c->used) { delete c; n->used &= ~b; n->mark &= ~b; } else if (!c->mark) n->mark &= ~b; }
        return had;
    }
    bool set_mark(uint64_t i) {
        if (!load(i)) return false;
        for (Node* n = head;; n = n->kid[slot(n, i)]) { n->mark |= 1ull << slot(n, i); if (!n->shift) return true; }
    }
    bool clear_mark(uint64_t i) { return head && i <= span(head->shift) && clear_in(head, i); }
    static bool clear_in(Node* n, uint64_t i) {      // true if the entry had been marked; clears the summary bits above it that become empty
        int s = slot(n, i); uint64_t b = 1ull << s;
        if (!(n->used & b)) return false;
        if (!n->shift) { bool was = n->mark & b; n->mark &= ~b; return was; }
        Node* c = n->kid[s]; bool was = clear_in(c, i); if (!c->mark) n->mark &= ~b; return was;
    }
    bool test_mark(uint64_t i) const {
        if (!head || i > span(head->shift)) return false;
        const Node* n = head;
        for (;;) { int s = slot(n, i); if (!((n->used >> s) & 1)) return false; if (!n->shift) return (n->mark >> s) & 1; n = n->kid[s]; }
    }
    // the smallest index >= from that holds an entry (or a marked entry); uses ctz on the bitmaps, so it skips empty ranges 64 slots at a time
    bool next(uint64_t from, bool marked, uint64_t& out) const { return head && from <= span(head->shift) && find_from(head, 0, from, marked, out); }
    static bool find_from(const Node* n, uint64_t base, uint64_t from, bool marked, uint64_t& out) {
        uint64_t bits = marked ? n->mark : n->used;
        uint64_t s0 = from <= base ? 0 : (from - base) >> n->shift;
        if (s0 >= 64) return false;
        bits &= ~0ull << s0;
        for (; bits; bits &= bits - 1) {
            int s = __builtin_ctzll(bits); uint64_t b = base + ((uint64_t)s << n->shift);
            if (!n->shift) { out = b; return true; }
            if (find_from(n->kid[s], b, from, marked, out)) return true;
        }
        return false;
    }
    int height() const { return head ? head->shift / 6 + 1 : 0; }
    static size_t nodes_in(const Node* n) { size_t c = 1; if (n->shift) for (uint64_t b = n->used; b; b &= b - 1) c += nodes_in(n->kid[__builtin_ctzll(b)]); return c; }
    size_t nodes() const { return head ? nodes_in(head) : 0; }
    bool valid() const { size_t c = 0; if (!head) return count == 0; return head->shift == 0 || head->used != 1 ? chk(head, c) && c == count : false; }
    static bool chk(const Node* n, size_t& c) {
        if (!n->used || (n->mark & ~n->used)) return false;                 // no empty nodes; marks only where something is
        if (!n->shift) { c += __builtin_popcountll(n->used); return true; }
        for (uint64_t b = n->used; b; b &= b - 1) {
            int s = __builtin_ctzll(b); const Node* k = n->kid[s];
            if (!k || k->shift != n->shift - 6 || !chk(k, c) || (k->mark != 0) != (bool)((n->mark >> s) & 1)) return false;
        }
        return true;
    }
};
```

Read it in this order: `span` and `grow` (height follows the largest index), `store` (create nodes on the way down), `load`, `erase` (prune empty nodes and drop root levels that have only slot 0), then the marks and `next`.

**Marks.** Each node keeps, in one 64-bit word, a bit per slot saying that the slot or the subtree under it holds a marked entry. Setting a mark sets one bit per level on the path. Clearing it clears the bit and, if the node's word is now zero, clears the bit in the parent too (`clear_in` does this on the way back up). Then `next(from, marked = true)` can skip a whole subtree with one test of one bit, which is how the page cache finds the dirty pages of a file without visiting the clean ones.

**A run** (`trace31.cpp`; each node is `s<shift>[slot->child or slot=value]`, a `*` after the slot means "marked at or below"):

```
xa after 0 1 5 : height 1 nodes 1 : s0[0=100 1=101 5=105]
xa after 64 : height 2 nodes 3 : s6[0->s0[0=100 1=101 5=105] 1->s0[0=164]]
xa after 4100 : height 3 nodes 6 : s12[0->s6[0->s0[0=100 1=101 5=105] 1->s0[0=164]] 1->s6[0->s0[4=4200]]]
xa marks 5 and 4100 : s12[0*->s6[0*->s0[0=100 1=101 5*=105] 1->s0[0=164]] 1*->s6[0*->s0[4*=4200]]]
xa next_marked(0)=5 next_marked(6)=4100 next(65)=4100 found 111
xa after clear_mark(5) : next_marked(0)=4100 : s12[0->s6[0->s0[0=100 1=101 5=105] 1->s0[0=164]] 1*->s6[0*->s0[4*=4200]]]
xa after erase 4100 : height 2 nodes 3 : s6[0->s0[0=100 1=101 5=105] 1->s0[0=164]]
```

Index 64 is the first that does not fit in one node, so `grow` puts a new root (shift 6) above the old one. Index 4,100 = 64 x 64 + 4 needs a third level: slot 1 of the new root, slot 0 of the middle node, slot 4 of the leaf. After the two marks the path to each marked entry shows `*` at every level. `next_marked(6)` tries the leaf that holds 5 (nothing marked at or after 6), backs out, and takes the root's marked slot 1; it never enters the unmarked leaf that holds index 64. Clearing the mark on 5 removes the `*` along its path up to the top, because the whole left subtree has no marks left, and the mark on 4,100 stays. Erasing 4,100 prunes the two empty nodes and the root drops back to height 2.

**What it costs.** Nodes per entry (a node here is 1,048 bytes because the model keeps values and child pointers in separate arrays) and lookup time for 10^6 keys:

| Case | Height | Nodes | Nodes per entry |
|---|---|---|---|
| Dense: keys 0 to 999,999 | 4 | 15,875 | 0.0159 |
| Sparse: 50,000 random keys below 2^30 | 5 | 99,598 | 1.992 |
| Clustered: 127,560 keys in 4,096 clusters of up to 50 neighbours | 7 | 11,863 | 0.093 |

Lookup of one key among the dense 10^6 (ns per lookup, run 1 / run 2): XArray 35.6 / 27.2, `std::map` 1,021.4 / 1,026.1, `unordered_map` 29.1 / 30.2.

Two things to read off. A **dense** index range costs about one node per 63 entries and a lookup is a handful of loads; it is as fast as a hash table and about 28 to 38 times faster than `std::map`. A **sparse** range is the XArray's weak spot: 50,000 random keys in a space of 2^30 use about two nodes per entry, because most keys sit alone in a leaf and share only the top of the tree. That is why the kernel uses it for things that are dense by construction (file offsets in pages, small IDs) and not for arbitrary 64-bit keys.

**The kernel's XArray** *(from memory)*: it replaced the radix tree API in Linux 4.20 and underlies the page cache (`address_space` keeps its pages in an XArray indexed by page offset) and the IDR/IDA allocators. A node has 64 slots (`XA_CHUNK_SHIFT` is 6; 4 on `CONFIG_BASE_SMALL` builds) and is about 576 bytes. There are three marks (`XA_MARK_0` to `XA_MARK_2`), kept as a bitmap per mark per node, as the model keeps one. Entries are pointers whose low bits are used to tag small integers and internal pointers. `xa_load` is lockless under `rcu_read_lock`; writers take the array's own spinlock (`xa_lock`); the advanced interface (`xas_*` with an `xa_state`) keeps a position so a loop does not re-descend from the root. Entries can also span a power-of-two range of indexes (used for large folios). The model has none of: RCU, locking, tagged entries, multi-index entries.

## 3. The maple tree: ranges in a copy-on-write B-tree

A process's address space is a set of **non-overlapping ranges**, each with an owner (a VMA), and the common question is "which range holds address a?". The model stores the whole key space `[0, MAXK]`: every address is in some slot, and unmapped address ranges are **NULL entries (gaps)**. A node has up to `FAN` slots, each with a **pivot** (the last index of the slot) and either a value (leaf) or a child (inner node). Slot i covers `(pivot[i-1], pivot[i]]`. Searching a node is a scan for the first pivot at or above the index.

```cpp
// A copy-on-write B-tree of ranges (the ideas of the kernel's maple tree; a model, not kernel code).
// The whole key space [0, MAXK] is always covered. A node holds up to FAN slots; slot i covers (pivot[i-1], pivot[i]].
// A leaf slot holds a value (0 means NULL, a gap); an inner slot holds a child. Nothing is changed in place: store() builds new nodes
// along the path it touches and returns a new root, so a reader holding the old root keeps seeing a consistent old tree.
template <int FAN = 16>
struct RangeMap {
    using U = unsigned long long; static constexpr U MAXK = ~0ull;
    struct Node;
    using P = std::shared_ptr<const Node>;
    struct Ent { U piv; U val; P kid; };
    struct Node { bool leaf; int n = 0; U piv[FAN]; U val[FAN]; P kid[FAN]; };
    static inline long built = 0;                       // nodes built so far, to show the cost of a store
    P root;
    RangeMap() { auto* l = new Node; ++built; l->leaf = true; l->n = 1; l->piv[0] = MAXK; l->val[0] = 0; root = P(l); }

    static std::vector<P> chunk(const std::vector<Ent>& e, bool leaf) {        // cut a list of slots into balanced nodes of at most FAN
        size_t m = e.size(), k = (m + FAN - 1) / FAN; std::vector<P> out; size_t at = 0;
        for (size_t j = 0; j < k; ++j) {
            size_t take = (m - at + (k - j) - 1) / (k - j); auto* nd = new Node; ++built; nd->leaf = leaf; nd->n = (int)take;
            for (size_t t = 0; t < take; ++t, ++at) { nd->piv[t] = e[at].piv; nd->val[t] = e[at].val; nd->kid[t] = e[at].kid; }
            out.push_back(P(nd));
        }
        return out;
    }
    // Rebuild node nd (which starts at nlo) so that [lo, hi] holds v. Returns one node, or several if it overflowed.
    static std::vector<P> put(const P& nd, U nlo, U lo, U hi, U v) {
        std::vector<Ent> out; U s = nlo; bool placed = false; U nhi = nd->piv[nd->n - 1];
        for (int i = 0; i < nd->n; ++i) {
            U e = nd->piv[i];                                                  // slot i covers [s, e]
            if (e < lo || s > hi) out.push_back({e, nd->val[i], nd->kid[i]});    // untouched
            else if (nd->leaf) {
                if (s < lo) out.push_back({lo - 1, nd->val[i], nullptr});        // the part left of the new range survives
                if (!placed) { out.push_back({std::min(hi, nhi), v, nullptr}); placed = true; }
                if (e > hi) out.push_back({e, nd->val[i], nullptr});             // the part right of it survives
            } else for (const P& p : put(nd->kid[i], s, lo, hi, v)) out.push_back({p->piv[p->n - 1], 0, p});
            if (e != MAXK) s = e + 1;
        }
        if (nd->leaf) {                                                          // merge neighbouring gaps
            std::vector<Ent> c;
            for (auto& x : out) if (!c.empty() && c.back().val == 0 && x.val == 0) c.back().piv = x.piv; else c.push_back(x);
            out.swap(c);
        }
        return chunk(out, nd->leaf);
    }
    // Overwrite [lo, hi] with v (v == 0 makes it a gap). Returns the new root; *this is updated to it.
    void store(U lo, U hi, U v) {
        std::vector<P> ps = put(root, 0, lo, hi, v);
        while (ps.size() > 1) { std::vector<Ent> e; for (auto& p : ps) e.push_back({p->piv[p->n - 1], 0, p}); ps = chunk(e, false); }
        root = ps[0];
    }
    static U find(const P& r, U idx) {
        const Node* n = r.get();
        for (;;) { int i = 0; while (n->piv[i] < idx) ++i; if (n->leaf) return n->val[i]; n = n->kid[i].get(); }
    }
    U find(U idx) const { return find(root, idx); }
    template <class F> static void each(const P& r, F&& f) {                     // visit the non-gap ranges in order: f(lo, hi, value)
        walk(r.get(), 0, f);
    }
    template <class F> static void walk(const Node* n, U s, F& f) {
        for (int i = 0; i < n->n; ++i) {
            if (n->leaf) { if (n->val[i]) f(s, n->piv[i], n->val[i]); } else walk(n->kid[i].get(), s, f);
            if (n->piv[i] != MAXK) s = n->piv[i] + 1;
        }
    }
    static int depth(const P& r) { int d = 1; for (const Node* n = r.get(); !n->leaf; n = n->kid[0].get()) ++d; return d; }
    static size_t nodes(const P& r) { size_t c = 1; if (!r->leaf) for (int i = 0; i < r->n; ++i) c += nodes(r->kid[i]); return c; }
    static bool valid(const P& r) { int leafdepth = -1; return r->piv[r->n - 1] == MAXK && chk(r.get(), 0, 1, leafdepth); }
    static bool chk(const Node* n, U lo, int d, int& leafdepth) {
        if (n->n < 1 || n->n > FAN) return false;
        for (int i = 0; i < n->n; ++i) {
            if (i && n->piv[i] <= n->piv[i - 1]) return false;                   // pivots strictly increase
            if (n->leaf) { if (i && n->val[i] == 0 && n->val[i - 1] == 0) return false; }
            else if (!n->kid[i] || n->kid[i]->piv[n->kid[i]->n - 1] != n->piv[i]) return false;   // a pivot is the last pivot of its child
        }
        if (n->leaf) { if (leafdepth < 0) leafdepth = d; return leafdepth == d; }
        U s = lo;
        for (int i = 0; i < n->n; ++i) { if (!chk(n->kid[i].get(), s, d + 1, leafdepth)) return false; if (n->piv[i] != MAXK) s = n->piv[i] + 1; }
        return true;
    }
};
```

Read `store` and `put` together. `put` rebuilds one node: it walks the old slots, keeps the ones the new range does not touch, keeps the left and right remainders of the ones it cuts, drops the ones it covers, puts the new entry in, merges neighbouring gaps, and then `chunk` cuts the result into nodes of at most `FAN` slots. An inner node does the same with the **replacement nodes** its children return. So one `store` allocates a new node at each level of the path (plus a sibling when something overflows) and returns a new root; nothing already published is touched.

**A run** with `FAN = 4` so that splits show (leaf `[pivot=value ...]`, `.` is a gap, inner `(pivot:child ...)`; "built" counts nodes allocated by that store):

```
rm store [10,19]=1 : built 1 depth 1 : [9=. 19=1 max=.]
rm store [30,39]=2 : built 3 depth 2 : (29:[9=. 19=1 29=.] max:[39=2 max=.])
rm store [50,59]=3 : built 2 depth 2 : (29:[9=. 19=1 29=.] max:[39=2 49=. 59=3 max=.])
rm store [70,79]=4 : built 3 depth 2 : (29:[9=. 19=1 29=.] 59:[39=2 49=. 59=3] max:[69=. 79=4 max=.])
rm store [15,34]=9 : built 3 depth 2 : (29:[9=. 14=1 29=9] 59:[34=9 39=2 49=. 59=3] max:[69=. 79=4 max=.])
rm store [50,59]=gap : built 2 depth 2 : (29:[9=. 14=1 29=9] 59:[34=9 39=2 59=.] max:[69=. 79=4 max=.])
rm find now: 12=1 15=9 33=9 35=2 55=0 75=4 100=0
rm find in the root saved after the second store: 12=1 15=1 33=2 35=2 55=0 75=0
```

The first store cuts the single gap `[0, max]` into three slots. The second would make five slots, more than `FAN = 4`, so `chunk` cuts them 3 and 2 and `store` adds a root above: 3 nodes built. The fifth store, `[15,34]`, overwrites the tail of `[10,19]`, the gap after it and the head of `[30,39]`: the left remainder `[10,14]=1` survives, and the new range is written as `[15,29]=9` in the first leaf and `[30,34]=9` in the second (a range that crosses a node boundary is stored in pieces). Erasing `[50,59]` stores a gap, which is just a store of value 0. The last two lines are the point of copy-on-write: the root saved after the second store still answers `15=1` and `33=2`, while the current root says 9 and 9. A reader that started before the store finishes sees a consistent old tree.

**What it costs** (100,000 stores of random ranges of up to 40,000 in a space of 2^32, `FAN = 16`): the tree has depth 5 and 16,045 nodes for 100,425 stored ranges, and a store builds 5.08 nodes on average. The depth fits a 16-way tree (log16 of 100,000 is 4.2; nodes split in halves are partly empty), and a store builds about one node per level. The model has no merge of underfull nodes, so a tree that has had many deletions stays large; the real one rebalances.

**The kernel's maple tree** *(from memory)*: merged in Linux 6.1; its first big user is the process address space, where it replaced both the VMA red-black tree and the `vm_next` linked list (a VMA iterator walks it). A node is 256 bytes. A leaf or range node has 16 slots and 15 pivots; the **allocation-range** node used for the VMA tree has only 10 slots and a **gap array** that records, per slot, the largest free run beneath it; that is how `mmap` finds a hole of a given size, and it is the same job as the `rb_subtree_gap` augmentation in the old VMA rbtree (Topics 13 and 30). Readers use `mas_*` operations under RCU; the tree has an embedded spinlock (or uses the lock of its owner); a writer builds replacement nodes and the old ones are freed after a grace period, and a reader that lands on a node that has been replaced notices (it is marked dead) and restarts. Linux 6.4 added per-VMA locks, so a page fault looks up its VMA under RCU and takes only that VMA's lock instead of the whole `mmap_lock`; that rests on the lockless maple-tree lookup. **Differences from the model**: the kernel does not copy the whole path to the root for every store (it replaces the node it changes and fixes the parent slot), it has dense and 64-bit node kinds, gap tracking, and rebalancing.

## 4. Static data: three layouts for a sorted array

For data that does not change (a routing table built once, a sorted table of keys) the layout is free to choose. Binary search on a sorted array reads `log2 n` keys that are far apart: the first reads are in the same lines every time, the last four levels share one line, and everything between costs a line each. The **external-memory model** counts line transfers; three layouts reduce them.

```cpp
// Three ways to lay out a sorted set of 32-bit keys (all below INT_MAX) for fast search. All return the first key >= x.
using Key32 = int32_t;
// (1) Eytzinger: the keys in breadth-first order of a complete binary tree, so the top levels sit together and children are at 2k, 2k+1.
struct Eytzinger {
    std::vector<Key32> a; size_t n = 0;                       // a[1..n]
    void build(const std::vector<Key32>& s) { n = s.size(); a.assign(n + 1, 0); size_t i = 0; fill(1, s, i); }
    void fill(size_t k, const std::vector<Key32>& s, size_t& i) { if (k > n) return; fill(2 * k, s, i); a[k] = s[i++]; fill(2 * k + 1, s, i); }
    Key32 lower_bound(Key32 x) const {
        size_t k = 1;
        while (k <= n) { __builtin_prefetch((const void*)((uintptr_t)a.data() + k * 64)); k = 2 * k + (a[k] < x); }   // no branch on the comparison
        k >>= __builtin_ffsll(~(long long)k);                   // undo the trailing right turns plus the last left turn
        return k ? a[k] : INT_MAX;
    }
};
// (2) S-tree: a static B-tree whose nodes are 16 keys (one 64-byte line). Level 0 is the sorted keys; level h+1 holds the maximum of each block of level h.
struct STree {
    static constexpr int B = 16;
    std::vector<std::vector<Key32>> lev; size_t n = 0;
    void build(const std::vector<Key32>& s) {
        n = s.size(); lev.clear(); std::vector<Key32> cur(s); cur.resize(std::max<size_t>(B, (n + B - 1) / B * B), INT_MAX); lev.push_back(cur);
        while (lev.back().size() > B) {
            const auto& d = lev.back(); std::vector<Key32> up;
            for (size_t j = B - 1; j < d.size(); j += B) up.push_back(d[j]);
            up.resize((up.size() + B - 1) / B * B, INT_MAX); lev.push_back(up);
        }
    }
    static int count_less(const Key32* p, Key32 x) { int c = 0; for (int i = 0; i < B; ++i) c += p[i] < x; return c; }   // compares all 16, no early exit
    size_t lower_bound_index(Key32 x) const {                  // position of the first key >= x in sorted order, or n
        if (n == 0 || x > lev[0][n - 1]) return n;
        size_t node = 0;
        for (size_t h = lev.size() - 1; h > 0; --h) node = node * B + count_less(&lev[h][node * B], x);
        return node * B + count_less(&lev[0][node * B], x);
    }
    Key32 lower_bound(Key32 x) const { size_t i = lower_bound_index(x); return i < n ? lev[0][i] : INT_MAX; }
};
// (3) van Emde Boas order of a perfect tree of height h: BFS indices, top half first, then each bottom subtree whole.
inline void veb_order(int h, uint64_t r, std::vector<uint64_t>& out) {
    if (h == 1) { out.push_back(r); return; }
    int t = h / 2, b = h - t;
    veb_order(t, r, out);
    for (uint64_t j = 0; j < (1ull << t); ++j) veb_order(b, (r << t) + j, out);
}
```

- **Eytzinger.** Put the keys in the order of a breadth-first walk of a complete binary tree, so node k has children 2k and 2k+1. The search has no branch (the comparison result is added to the index), and the 16 great-grandchildren of k are 16 consecutive keys starting at 16k, one line, so the code prefetches `a + 16k` four levels ahead. The final shift undoes the trailing right turns and the last left turn to land on the answer.
- **S-tree.** A static B-tree with 16 keys (one 64-byte line) per node, stored level by level; no pointers, since the children of block j are blocks 16j to 16j+15 of the level below. One line per level and about log16 n levels. The node search compares all 16 keys with no early exit, which a compiler may turn into vector compares (I did not inspect the assembly).
- **van Emde Boas.** Cut the tree at half its height; store the top half, then each bottom subtree whole, each laid out the same way. Every recursive block is contiguous, so for any line size B some level of the recursion fits a block in a line, **without knowing B**: it is cache-oblivious. The listing makes the order and nothing else; navigating it needs a position per node, which the cache model below supplies by table.

### A cache model

To compare line counts without a stopwatch, a small program (`bench31.cpp`, not listed here) replays each search and records which 64-byte line every read touches. It reports lines per query when nothing is cached, and misses per query in a fully associative cache with least-recently-used eviction (512 lines = 32 KiB and 4,096 lines = 256 KiB). Each layout searches the keys 1, 3, 5, ... and every answer is checked against the true rank. It is a **model**: no associativity limit, no hardware prefetch, no TLB, and the vEB order is looked up from a table rather than navigated.

Results (n = 2^h - 1 keys; 20,000 warm-up and 20,000 measured random queries; the same in both runs; every layout returned the right rank for every query):

| Layout | h | n | Lines per query | Misses, 512 lines | Misses, 4,096 lines |
|---|---|---|---|---|---|
| Sorted array | 16 | 65,535 | 12.001 | 4.198 | 0.000 |
| Eytzinger | 16 | 65,535 | 13.000 | 4.202 | 0.000 |
| van Emde Boas | 16 | 65,535 | 5.604 | 2.153 | 0.000 |
| S-tree | 16 | 65,535 | 4.000 | 1.199 | 0.066 |
| Sorted array | 22 | 4,194,303 | 18.000 | 11.331 | 7.881 |
| Eytzinger | 22 | 4,194,303 | 19.000 | 11.333 | 7.882 |
| van Emde Boas | 22 | 4,194,303 | 8.331 | 4.682 | 3.682 |
| S-tree | 22 | 4,194,303 | 6.000 | 2.940 | 2.100 |

- **Binary search reads about h - 4 lines** (the last four levels share one) and **Eytzinger h - 3**: no fewer. Under this model Eytzinger and the sorted array have the **same miss counts to within 0.1%**: packing the top of the tree helps less than it seems, because the sorted array's top levels are few lines anyway. Eytzinger's advantage is not fewer lines: it is **no branch mispredictions and prefetching four levels ahead**, which hide the latency of the lines it does touch. The model has no prefetch, so it cannot show that.
- **The S-tree reads one line per level**: 4 at h = 16 and 6 at h = 22 lines, the number of levels. It has the fewest lines and the fewest misses: about 3.7 to 3.9 times fewer misses than binary search at 2^22 keys.
- **vEB reads about 1.4 times the S-tree's lines** at both sizes, with no parameter tuned to the line size.

### The stopwatch

Time per query (ns) for random queries over n random keys, queries independent of each other:

| n | `std::lower_bound` | Eytzinger | S-tree |
|---|---|---|---|
| 2^14 | 89.0 / 89.0 | 16.1 / 17.0 | 16.0 / 16.4 |
| 2^18 | 137.8 / 141.5 | 43.3 / 43.3 | 24.7 / 24.8 |
| 2^22 | 477.8 / 426.9 | 98.1 / 104.5 | 78.2 / 71.1 |
| 2^24 | 626.5 / 579.7 | 178.9 / 165.8 | 114.7 / 112.0 |

What both runs agree on: **both layouts are 3 to 7 times faster than `std::lower_bound`** at every size, and **the S-tree is 1.2 to 1.8 times faster than Eytzinger from 2^18 keys up** (a tie at 2^14, where everything is in cache). The cost is that both are **static**: one insert rebuilds or shifts the array. For dynamic data, B-trees (Topic 29) are the same idea with slack in the nodes. These timings come from a mix of effects (branches, prefetch, line counts, the vector compare) that the cache model separates only partly, so take the order of the three, not the exact ratios.

## 5. Choosing

- **Index to pointer, dense or clustered, with lockless reads** (page cache, file offsets, small IDs): an XArray-like radix tree. Not for sparse 64-bit keys (about two nodes per entry here); use a hash table or an ordered map.
- **Non-overlapping ranges to owners, with fast lookups of "which range holds a"** (address spaces, memory maps, IP ranges): a range tree in the maple-tree style. If a simple `std::map` keyed by range start is enough (one thread, small), use it: `upper_bound` and a step back finds the range.
- **Search a sorted table that never changes**: S-tree if lookups dominate and the table does not fit in cache; Eytzinger when you want a simple array and branch-free search; plain `std::lower_bound` below a few thousand keys.
- **The same table, dynamic**: a B-tree or B+ tree (Topic 29), or a skip list or hash table.
- **You cannot tune for the line size**: vEB order is within a small factor of the best without it.

## 6. Common mistakes

- **Using an XArray for sparse keys.** Memory grows by about two nodes per key (section 2), each many times the size of the key. The structure is for dense indexes.
- **Forgetting that marks live above the entry.** A mark bit set in a leaf but not in its ancestors makes `next_marked` miss the entry; clearing a leaf mark without the upward clear leaves stale bits that send searches into empty subtrees. `valid()` checks both.
- **Shrinking the root wrongly.** The root can drop a level only when it has just slot 0; a root whose only child sits in slot 3 covers different indexes and must stay.
- **Editing a published node.** A reader holding the old root must never see a change; one write into a shared node breaks every snapshot (the tests re-check old roots for this).
- **Gaps that do not merge.** Two neighbouring NULL slots waste slots and make "the largest gap" wrong; `put` merges them and the validator rejects a leaf with two in a row.
- **Layout off-by-ones.** Eytzinger is 1-based (index 0 unused) and the final `k >>= ffs(~k)` is the easy line to get wrong; the S-tree pads with `INT_MAX`, so it cannot store `INT_MAX`, and must not search past the last real key. Test every size.
- **Judging layouts on a small array.** At 2^14 keys everything is in cache and the layouts tie; they differ when the table is larger than the cache.

The equivalent mutants (changes that do not alter any output, so no test can catch them): (1) in `put`, `s = e + 1` without the `e != MAXK` guard, because `s` is not read after the last slot; (2) in `STree::build`, sizing level 0 to `(n + B - 1) / B * B` without the `max` with `B`, which differs only for an empty tree, and `lower_bound` returns before reading anything when `n == 0`; (3) `h <= 1` for `h == 1` in `veb_order`, since a height below 1 is never asked for. The validators (`valid`, `chk`) were not mutated: weakening a checker cannot change what correct code does, so such a change would always survive. Every other change was caught by a test.

## 7. Questions to answer out loud

1. **Why wide nodes?** Fewer levels and a node search over memory that is already loaded: a lookup touches a few cache lines, not 20 to 40 scattered nodes.
2. **What does "replace, do not edit" buy?** Lockless readers: they see the old node or the new one, and the old one is freed only after a grace period.
3. **How is the XArray indexed?** The index is cut into 6-bit digits, one per level; the height is the least that covers the largest index.
4. **How do marks work?** One bit per slot per node meaning "something marked at or under this slot", set along the whole path and cleared upward when a word becomes zero; `next_marked` skips subtrees with one bit test.
5. **When is an XArray the wrong choice?** Sparse 64-bit keys: most entries sit alone in a leaf.
6. **What is a pivot, and why store gaps as entries?** A pivot is the last index of a slot. With gaps as NULL entries every address belongs to exactly one slot, so a lookup is a pivot scan with no "not found" case inside the tree.
7. **What did the maple tree replace, and why?** The VMA red-black tree and linked list (Linux 6.1): fewer cache misses, RCU-safe lookups (which per-VMA locks in 6.4 rely on), and gap tracking for `mmap` (from memory).
8. **Why does Eytzinger beat binary search if it reads about the same number of lines?** No branch mispredictions and prefetching of the next four levels, so the lines arrive early.
9. **What is cache-oblivious?** An algorithm or layout that is good for every line and cache size without being told them; vEB order is the example.

## 8. Problems

**A.** What height does an XArray need for the indexes 63, 64, 4,095, 4,100, 70,000 and 2^64 - 1? How many of the 64 slots of the top node can the last one use?

**B.** In the XArray trace, walk `next_marked(6)` after both marks were set: which node does it visit at each level, and which slots does it skip without looking?

**C.** In the maple trace, why did the second store build 3 nodes, and why does the root saved after it still answer `15=1`?

**D.** An S-tree with 16 keys per node holds 10^6 keys. How many levels does it have, and how many lines does one search read?

**E.** Build the Eytzinger array for the sorted keys 1 to 7 and trace `lower_bound(5)`, including the final shift.

**F.** Write the van Emde Boas order (BFS indices) of a perfect tree of height 3.

**G.** A thread must find which memory range holds an address while another thread maps and unmaps ranges, and the reader may not take a lock. Why does a red-black tree make this hard, and what do the maple tree's choices change?

**H.** Choose a structure: (a) the page cache of a file with pages indexed by offset; (b) a read-only table of 10^7 sorted 32-bit keys with random lookups; (c) a set of non-overlapping IP ranges, each with a policy, looked up per packet; (d) a map from random 64-bit connection IDs to sessions; (e) finding the dirty pages of a file in offset order.

### Answers

**A.** 63: one level; 64: two; 4,095: two; 4,100: three (it is at least 64 x 64); 70,000: three (it is below 64^3 = 262,144); 2^64 - 1: eleven. The top node of the eleventh level holds the top 4 bits (64 - 60), so only 16 of its 64 slots can be used.

**B.** The root (shift 12) starts at slot `6 >> 12 = 0`. Its mark word has slots 0 and 1 set. Slot 0 leads to the middle node (shift 6), which starts at slot `6 >> 6 = 0`; its mark word has only slot 0 (the leaf holding 5), because the leaf holding 64 has no mark, so slot 1 is never entered. The leaf (shift 0) starts at slot 6, and its only mark is slot 5, so nothing is left: false. Back at the root, slot 1 (base 4,096) leads to a middle node whose slot 0 leads to a leaf whose marked slot is 4: 4,096 + 4 = 4,100.

**C.** The second store makes five slots (gap, 1, gap, 2, gap) in a leaf of capacity 4; `chunk` splits them 3 and 2 (two nodes) and `store` adds a root above (one node): 3. The old root was not modified: the store built new nodes and only swapped the `root` pointer of the map, so a saved root still points at the old nodes, where 15 is in `[10,19]=1`.

**D.** Level 0: 10^6 / 16 = 62,500 blocks. Level 1 holds 62,500 separators: 3,907 blocks. Level 2: 3,907 separators, 245 blocks. Level 3: 245 separators, 16 blocks. Level 4: 16 separators, 1 block. Five levels, so one search reads five lines.

**E.** The array is `a[1..7] = 4 2 6 1 3 5 7`. For x = 5: k = 1, `a[1] = 4 < 5`, so k = 3; `a[3] = 6` is not below 5, so k = 6; `a[6] = 5` is not below 5, so k = 12, which is above n = 7, stop. k = 12 is 1100 in binary; `~k` has its lowest set bit at position 0, so `ffs` is 1 and k becomes 6: `a[6] = 5`.

**F.** Height 3: t = 1, b = 2. Top: 1. Bottoms: the height-2 subtrees rooted at 2 and 3, each in order root then its two children: 2, 4, 5 and 3, 6, 7. Order: 1, 2, 4, 5, 3, 6, 7.

**G.** A rotation changes several parent and child pointers; a reader on a node that is rotated away can follow a pointer to a place that no longer matches its key range, and the writer cannot know which readers are where. The maple tree's writer builds a replacement node and publishes it with one pointer store, frees the old one after a grace period, and a reader that meets a replaced node detects it and retries (from memory); a model that copies the whole path gives an even simpler guarantee, a complete snapshot.

**H.** (a) An XArray (dense offsets, marks for dirty). (b) An S-tree or an Eytzinger array. (c) A range tree like the maple tree if updates are frequent, or a sorted array of ranges rebuilt on change if they are rare. (d) A hash table; an XArray would use about two nodes per entry. (e) The XArray's `next(from, marked)` loop over the dirty mark.

---

## Cheat sheet

- **XArray:** 64-ary radix tree over the index; height = least that covers the largest index (11 for 64 bits); sparse keys are expensive (about 2 nodes per key); one mark bit per slot per node, set on the whole path and cleared upward.
- **Maple-style range tree:** B-tree of pivots; gaps are NULL entries; store = rebuild the touched nodes and return a new root; old roots remain valid snapshots; kernel: 256-byte nodes, 16 slots, an allocation-range node with a gap array, RCU (all from memory).
- **Static layouts:** sorted array ~ h - 4 lines per search; Eytzinger ~ h - 3 lines but branch-free with prefetch; S-tree = one line per level (log16 n); vEB about 1.4 x the S-tree without knowing the line size.
- **Measured:** XArray dense lookup 28 to 38 times faster than `std::map`; S-tree and Eytzinger 3 to 7 times faster than `std::lower_bound`, S-tree 1.2 to 1.8 times faster than Eytzinger from 2^18 keys.

---

## Sources

- The kernel facts (XArray in 4.20 and the page cache; the maple tree in 6.1, its node kinds, sizes and gap array; per-VMA locks in 6.4; RCU replacement of nodes) are from memory of the kernel documentation (`Documentation/core-api/xarray.rst` and `maple_tree.rst`) and of LWN's articles on both structures; I did not re-read them for this file.
- The comparison of sorted, Eytzinger, B-tree and vEB layouts follows Khuong and Morin, "Array Layouts for Comparison-Based Searching" (named from memory), and the prefetching Eytzinger search and the S-tree follow the "Static B-trees" article on Algorithmica (from memory); the code is my own implementation, tested as above. The vEB layout is from the cache-oblivious literature (Prokop; Frigo, Leiserson, Prokop, Ramachandran), named from memory.
- All timings and the cache model's numbers are measured or computed here.
- Problems by name, from memory: Design a Hit Counter, Range Module (715), My Calendar I (729), Snapshot Array (1146), Time Based Key-Value Store (981), Design Memory Allocator (2502).
