# DSA 07.09: Inside `std::set` and `std::map`: Layout, Guarantees, Comparators and Node Handles

*Part of the [Trees plan](00_plan.md) · Time: about 9 hours (3.5 reading, 5.5 problems) · You need: [BST core operations](07_bst_core_operations.md) (the invariant, floor and ceiling, why shape matters) and [BST problem patterns](08_bst_problem_patterns.md) (the order-statistic idea). The balanced-tree mechanics themselves come in Topics 10 to 13; here the tree is a black box that we open and measure. There is no companion demo file: nothing here is a new data structure, and every listing is tested code in this file.*

By the end of this tutorial you can:

- say what the C++ standard promises for `std::set` and `std::map`, and what the three major libraries actually build to keep those promises;
- compute what a node costs in bytes and explain why a million-element `std::set<int>` is about twelve times the size of a `std::vector<int>` and much slower to scan;
- look inside a real `std::set` and check the red-black invariants and the height of the tree you are running;
- use `lower_bound`, `upper_bound` and `equal_range` for floor, ceiling and range queries, and avoid the `multiset` traps;
- write a comparator that is a strict weak ordering, and describe what the library does when it is not;
- use C++17 node handles (`extract`, `merge`) to re-key or move elements without allocation;
- use the GNU order-statistics tree for k-th element and rank queries, and say when not to.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 176,418 checks passed under AddressSanitizer with UBSan (GCC 13.3, libstdc++ 13). The tests compared the helpers with sorted-vector references on random sets; ran 40 random insert/erase sequences on `set`, `multiset` and `map`, checking the red-black invariants of the real trees every 250 operations; confirmed that held iterators and addresses survived 60,000 random updates of other elements; and checked that the invariant checker really fails when each rule is broken by hand. The sizes, counts and shapes below are program output. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, glibc malloc, and each is the best of 3 runs on about a million random `int` keys; read them as shapes, not benchmarks. Everything about `_Rb_tree` was read in the header `bits/stl_tree.h` of this GCC. The statements about libc++ and the Microsoft STL come from their sources and a blog post (see Sources), and the places where I rely on my memory of the standard are marked as such.

---

## 1. What is promised and what is built

The standard does not say "red-black tree". It says what operations cost and what stays valid: lookup, insert and erase are logarithmic in the size, iterators are bidirectional and walk the keys in the order of the comparator, inserting never invalidates iterators or references, and erasing invalidates only those of the erased element (stated from memory of the standard; the experiments in section 4 test the behaviour). Those rules **force** a node-based balanced search tree: a sorted array cannot insert in logarithmic time, a hash table has no order, and a tree that moved nodes would break the references. Every major library chose the red-black tree.

| Library | Type | What I checked |
|---|---|---|
| libstdc++ (GCC) | `_Rb_tree` | read in `stl_tree.h` on this machine |
| libc++ (LLVM) | `__tree` | read in the source: red-black rebalancing functions, a colour flag in each node, the begin node and the size stored in the tree |
| Microsoft STL | `_Tree` | from a post on Microsoft's Old New Thing blog (August 7, 2023): a red-black tree with an external header node and an `_Isnil` flag in every node |

The layout in libstdc++, as the header declares it:

- a node starts with a **base** of four fields: the colour (an enum, so 4 bytes, padded to 8), and three pointers `_M_parent`, `_M_left`, `_M_right`; the base is 32 bytes, and the value follows it;
- the container object holds a **header node** of the same base type, plus the node count; `end()` is the header;
- the header's parent is the **root** (and the root's parent is the header), the header's left link is the **leftmost** node and its right link the **rightmost** one, so `begin()` and the last element cost `O(1)`;
- an empty tree is a header whose left and right links point at itself;
- the colour is `false` for red and `true` for black.

libc++ stores the begin node and the size in the tree object, uses a sentinel end node whose left link is the root, and keeps the colour as a `bool`. The Microsoft STL allocates the header separately, stores a pointer to it where the others store null, and tags each node with `_Isnil`. All three are the same idea with different bookkeeping: a red-black tree with a sentinel, an `O(1)` `begin()`, a stored size and a node per element.

---

## 2. What a node costs

An allocator that records the size of its requests shows what one element costs:

```cpp
// An allocator that records the size of the last request: every node of a tree is one request.
struct AllocLog {
    std::size_t calls = 0;
    std::size_t lastBytes = 0;
};
inline AllocLog g_log;

template <class T>
struct CountingAlloc {
    using value_type = T;
    CountingAlloc() = default;
    template <class U> CountingAlloc(const CountingAlloc<U>&) {}
    T* allocate(std::size_t n) {
        ++g_log.calls;
        g_log.lastBytes = n * sizeof(T);
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }
    void deallocate(T* p, std::size_t) { ::operator delete(p); }
    template <class U> bool operator==(const CountingAlloc<U>&) const { return true; }
    template <class U> bool operator!=(const CountingAlloc<U>&) const { return false; }
};

// bytes requested from the allocator for the first node of an empty container
template <class Container>
std::size_t firstNodeBytes(typename Container::value_type v) {
    Container c;
    c.insert(std::move(v));
    return g_log.lastBytes;
}
```

Program output on GCC 13.3, x86-64:

| Container | `sizeof` the container | Bytes requested per node |
|---|---|---|
| `std::set<int>`, `std::multiset<int>` | 48 | 40 |
| `std::map<int, int>` | 48 | 40 |
| `std::set<char>`, `std::set<long long>` | | 40 |
| `std::set<std::string>` | | 64 (32 + `sizeof(std::string)` = 32) |
| `std::map<int, std::string>` | | 72 |

A `set<int>` node holds a 32-byte base and 4 bytes of payload padded to 8, so the request is 40, **ten times the payload**. The container object itself is 48 bytes: the header node (32), the node count (8) and the comparator (padded to 8). The allocator then adds its own overhead. Measured with `mallinfo2` on about a million random keys (glibc malloc, which rounds a 40-byte request to a 48-byte chunk):

| Container | Heap bytes per element |
|---|---|
| `std::vector<int>` | 4.0 |
| `std::unordered_set<int>` | 43.6 |
| `std::set<int>` | 48.0 |
| `std::map<int, int>` | 48.0 |
| `std::set<std::string>`, 100,000 short strings | 80.0 |
| GNU order-statistics tree (section 8) | 48.0 |

So a million-element `std::set<int>` occupies about 48 MB against 4 MB for the vector. The cost is not only memory. Each node is a separate allocation, and a lookup follows about `log2 n` pointers, each of which can miss the cache. With 999,768 distinct random keys (1M draws):

| Operation | `std::set<int>` | sorted `std::vector<int>` | `std::unordered_set<int>` |
|---|---|---|---|
| lookup of a random key | 1122.6 ns | 184.2 ns | 40.9 ns |
| full scan, per element | 182.82 ns (built from shuffled keys) | 0.37 ns | |
| full scan, per element, built from sorted keys | 8.97 ns | | |

The last two scan rows are the lesson of this section. A set built from shuffled keys scatters its nodes in memory, so walking it in order hops between distant addresses at **about 183 ns per element**; the same set built by inserting sorted keys gets consecutive allocations and scans at about 9 ns, still 24 times slower than the vector. The number of comparisons is what the theory predicts: 21.40 per `count()` on random misses (a `lower_bound` plus one more comparison), 20.40 per `lower_bound`, 20.94 per insert, against `log2 n` = 19.93. The tree does its logarithmic work faithfully; the constant is the cache.

When is that acceptable? When the container changes all the time and must stay ordered, when stable addresses matter, when the size is small, or when the program is not lookup-bound. When the data is built once and queried many times, a sorted vector with `std::lower_bound` is usually the better ordered container (in this run 6 times faster to look up and 12 times smaller), and a hash table wins if order is not needed.

---

## 3. Looking inside a real `std::set`

In libstdc++ the iterator of every tree container exposes its node pointer as `_M_node`, and `end()` is the header. That is enough to write the invariant checker that Topic 12 will test an implementation with, and run it on the library's own trees:

```cpp
#if defined(__GLIBCXX__)
// Peeking into libstdc++ (GCC). The iterator of a set, map, multiset or multimap exposes its node
// pointer as `_M_node`, and `end()` points at the header node, whose parent is the root.
// This is an implementation detail: it compiles with libstdc++ only.
struct RbInfo {
    bool ok;                       // every red-black rule holds and all links agree
    int height;                    // levels
    int blackHeight;               // black nodes on any path from the root down to a null link
    std::size_t nodes;
};

inline bool rbWalk(const std::_Rb_tree_node_base* n, const std::_Rb_tree_node_base* parent,
                   int& height, int& black, std::size_t& count) {
    if (!n) { height = 0; black = 0; return true; }
    if (n->_M_parent != parent) return false;                  // the parent link must point back
    bool red = n->_M_color == std::_S_red;
    if (red && ((n->_M_left && n->_M_left->_M_color == std::_S_red) ||
                (n->_M_right && n->_M_right->_M_color == std::_S_red)))
        return false;                                          // no red node has a red child
    int hl, hr, bl, br;
    if (!rbWalk(n->_M_left, n, hl, bl, count) || !rbWalk(n->_M_right, n, hr, br, count)) return false;
    if (bl != br) return false;                                // equal black count on all paths
    ++count;
    height = 1 + std::max(hl, hr);
    black = bl + (red ? 0 : 1);
    return true;
}

template <class Container>
RbInfo inspect(const Container& c) {
    const std::_Rb_tree_node_base* header = c.end()._M_node;
    const std::_Rb_tree_node_base* root = header->_M_parent;
    RbInfo info{true, 0, 0, 0};
    if (!root) {                                               // empty: the header links to itself
        info.ok = c.empty() && header->_M_left == header && header->_M_right == header;
        return info;
    }
    const std::_Rb_tree_node_base* lo = root;                  // the real leftmost and rightmost nodes,
    while (lo->_M_left) lo = lo->_M_left;                      // found by walking down from the root
    const std::_Rb_tree_node_base* hi = root;
    while (hi->_M_right) hi = hi->_M_right;
    info.ok = root->_M_color == std::_S_black &&               // the root is black
              rbWalk(root, header, info.height, info.blackHeight, info.nodes) &&
              info.nodes == c.size() &&
              header->_M_left == lo &&                         // the header caches the leftmost node ...
              header->_M_right == hi;                          // ... and the rightmost one
    return info;
}
#endif
```

The checker verifies the red-black rules (the root is black; no red node has a red child; every path to a null link has the same number of black nodes), the parent links, the node count against `size()`, and that the header's cached leftmost and rightmost links agree with the nodes found by walking down from the root. The tests also break each rule on purpose (root red, a leaf's colour flipped, a parent link cut, the header's links swapped, a red child under a red node) and require the checker to fail.

Results on the real trees (program output; `ok` was true everywhere):

| n | Random inserts: height, black height | Sorted inserts: height, black height | Sorted, then every other key erased: height, black height | `log2(n+1)` | Bound `2 log2(n+1)` |
|---|---|---|---|---|---|
| 1000 | 12, 6 | 17, 9 | 9, 8 | 9.97 | 19.93 |
| 100,000 | 20, 10 | 31, 16 | 16, 15 | 16.61 | 33.22 |
| 1,000,000 | 24, 12 | 37, 19 | 19, 18 | 19.93 | 39.86 |

Compare with Topic 07, where sorted insertion into a plain BST gave a chain of height `n`. Here the worst of the three cases, sorted insertion of a million keys, gives height 37, below the bound of 39.86. Two facts show in the table. The height is never more than twice the black height (12 against 12, 37 against 38): red nodes cannot be adjacent, so at most every other node on a path is red, and the black height `b` forces at least `2^b - 1` nodes, which gives `height <= 2 log2(n + 1)`. And the tree is not perfectly balanced: sorted insertion gives a height 1.9 times `log2 n`, random insertion 1.2 times. That is the price of the cheap rebalancing of Topics 11 and 12 (AVL trees in Topic 10 are stricter).

**Consequences.** `*s.begin()` and `*s.rbegin()` (the minimum and maximum) are `O(1)`, because the header caches both ends. A full traversal visits every node a bounded number of times; a single `++` can climb `O(log n)` links, so the cost per step is amortised `O(1)`, not worst-case `O(1)`.

The `#if` is not decoration: this peeking compiles only with libstdc++. It is a measurement tool for learning, never something to ship.

---

## 4. Iterators, references and erasing while iterating

The tests keep 286 iterators and addresses of elements, run 60,000 random inserts and erases of *other* keys, and then check that every held iterator still finds its element and every address is unchanged. They are, because nodes never move. Two consequences follow.

- **Erase while iterating.** `erase(it)` returns the next iterator (C++11), so the loop is `it = s.erase(it)` when erasing and `++it` otherwise. The older form `s.erase(it++)` works too, because the increment happens before the node is freed. Both are tested.
- **The elements are const.** `*it` of a `set` is `const int&`; a `map` stores `pair<const K, V>`. The reason is the invariant: change a key behind the tree's back and the tree is wrong. The program below does it with a cast (undefined behaviour) and reports what happened on this toolchain.

```cpp
// The ordering must be a strict weak ordering: comp(x, x) is false, and "neither is smaller" is
// transitive. Violations are undefined behaviour; this is what GCC 13's libstdc++ did in these runs.
struct LessEqual { bool operator()(int a, int b) const { return a <= b; } };   // wrong: comp(x, x) is true

struct LessEqualResult {
    std::size_t size;              // after inserting 5, 5, 3, 5
    std::size_t countOf5;
    bool foundFive;
    std::size_t erasedFive;
};
inline LessEqualResult lessEqualDemo() {
    std::set<int, LessEqual> s;
    for (int k : {5, 5, 3, 5}) s.insert(k);
    LessEqualResult r{s.size(), s.count(5), s.find(5) != s.end(), 0};
    r.erasedFive = s.erase(5);
    return r;
}

// NaN is unordered with every number, so it is "equivalent" to whatever it is compared with first.
inline std::size_t nanFirstSize() {
    std::set<double> s;
    for (double d : {std::nan(""), 1.0, 2.0, 3.0}) s.insert(d);
    return s.size();
}

// Keys are const for a reason: changing one through a cast leaves the tree in the old order.
struct MutateResult {
    bool foundNew, foundOld;
    bool stillSorted;
};
inline MutateResult mutateKeyDemo() {
    std::set<int> s{1, 2, 3, 4, 5};
    const_cast<int&>(*s.find(2)) = 10;                         // undefined behaviour
    MutateResult r{s.find(10) != s.end(), s.find(2) != s.end(), std::is_sorted(s.begin(), s.end())};
    return r;
}

// Heterogeneous lookup (C++14): with a transparent comparator, find() takes any type the
// comparator accepts, so no temporary key has to be built.
struct Key {
    std::string text;
    static inline int made = 0;                                // constructions, for counting
    Key(const char* c) : text(c) { ++made; }
    Key(std::string_view v) : text(v) { ++made; }
    Key(const Key& o) : text(o.text) { ++made; }
    bool operator<(const Key& o) const { return text < o.text; }
};
struct KeyLess {
    using is_transparent = void;                               // switches on the extra find() overloads
    bool operator()(const Key& a, const Key& b) const { return a.text < b.text; }
    bool operator()(const Key& a, std::string_view b) const { return a.text < b; }
    bool operator()(std::string_view a, const Key& b) const { return a < b.text; }
};
```

The first three functions of this listing are the "what if" experiments. Their output on libstdc++ 13 (observed behaviour of undefined behaviour, not a guarantee):

| Experiment | Observed |
|---|---|
| set with `<=`, insert 5, 5, 3, 5 | size 4; `count(5)` = 0; `find(5)` fails; `erase(5)` removes 0 |
| `set<double>`, insert NaN, 1, 2, 3 | size 1 (everything is "equivalent" to NaN) |
| `set<int>` of 1..5, key 2 overwritten with 10 | `find(10)` succeeds, `find(2)` fails, iteration is no longer sorted |

Why the `<=` set loses its fives: the insert path calls `comp(5, node)`, which is true for an equal key, so it goes left and inserts a *second* 5 instead of recognising the first. The lookup then asks "is the node's key not less than 5", which is `!(5 <= 5)` = false for every 5, so `lower_bound` runs past all of them. The three 5s are in the tree and unreachable. Section 6 states the rules that prevent this.

---

## 5. `lower_bound`, `upper_bound`, `equal_range` and the `multiset` traps

`lower_bound(k)` returns the first element not less than `k`; `upper_bound(k)` the first element greater than `k`; `equal_range(k)` is the pair of them. They are exactly the floor and ceiling of Topic 07, with iterators instead of the pointer chase:

```cpp
// floor, ceiling, lower and higher of a key in a std::set: one lower_bound or upper_bound each.
inline std::optional<int> floorOf(const std::set<int>& s, int k) {      // largest key <= k
    auto it = s.upper_bound(k);                                // first key > k
    if (it == s.begin()) return std::nullopt;
    return *--it;
}
inline std::optional<int> ceilOf(const std::set<int>& s, int k) {       // smallest key >= k
    auto it = s.lower_bound(k);                                // first key >= k
    if (it == s.end()) return std::nullopt;
    return *it;
}
inline std::optional<int> lowerOf(const std::set<int>& s, int k) {      // largest key < k
    auto it = s.lower_bound(k);
    if (it == s.begin()) return std::nullopt;
    return *--it;
}
inline std::optional<int> higherOf(const std::set<int>& s, int k) {     // smallest key > k
    auto it = s.upper_bound(k);
    if (it == s.end()) return std::nullopt;
    return *it;
}

// multiset: count() walks the whole equal range, erase(key) removes all of it
inline std::size_t countEqual(const std::multiset<int>& s, int k) {
    auto r = s.equal_range(k);                                 // [lower_bound(k), upper_bound(k))
    return (std::size_t)std::distance(r.first, r.second);
}
inline bool eraseOne(std::multiset<int>& s, int k) {           // erase(k) would remove every copy
    auto it = s.find(k);
    if (it == s.end()) return false;
    s.erase(it);
    return true;
}
```

On `{10, 20, 30, 40}` (program output):

| Key | floor | ceil | lower | higher |
|---|---|---|---|---|
| 5 | none | 10 | none | 10 |
| 10 | 10 | 10 | none | 20 |
| 25 | 20 | 30 | 20 | 30 |
| 40 | 40 | 40 | 30 | none |
| 45 | 40 | none | 40 | none |

Note the order of operations in `floorOf`: the element at `upper_bound(k)` is the first one greater than `k`, so the one before it is the largest not greater; the check `it == begin()` must come before the decrement. The tests compared all four with a sorted vector for keys from -60 to 60 on 300 random sets, and with `INT_MIN` and `INT_MAX`.

**`multiset` traps.** On `{10, 20, 20, 20, 30}`: `lower_bound(20)` is at index 1, `upper_bound(20)` at index 4, `equal_range(20)` is the range of three elements, and `lower_bound(25)` is at index 4. Then:

- `erase(20)` by key removes **all three**, while `erase(find(20))` removes one (`eraseOne` leaves two). Many bugs are an `erase(key)` where one copy was meant.
- `count(k)` walks the equal range, so it costs `O(log n + count)`, not `O(log n)` (stated from memory of the standard).
- A new element goes at the **upper bound** of its equal range, after the existing equal elements, so equal keys keep their insertion order (this is documented on cppreference, and the tests inserted 500 pairs with only five distinct first components and compared with a stable sort).
- Comparing only part of the data (the `first` of a pair) is legal if it is a strict weak ordering; elements that compare equal are then *different* objects, which is what makes `multiset<pair>` useful and `set<pair>` lossy.

For a `map`, the traps are the same family: `m[k]` inserts a default value when `k` is absent, so use `find`, `at` or `contains` for reading; `insert` does not overwrite; the value type is `pair<const K, V>`.

---

## 6. Comparators: the rules and the transparent form

The ordering `comp` passed to a set or map must be a **strict weak ordering**: `comp(x, x)` is false; `comp(a, b)` implies not `comp(b, a)`; `comp` is transitive; and "incomparable" (neither `comp(a, b)` nor `comp(b, a)`) is itself transitive. The library treats incomparable elements as the same key. `<=` breaks the first rule (section 4's lost fives); NaN breaks the last (it is incomparable with everything, but 1 and 2 are not incomparable with each other, so the tree degenerates into inconsistent answers). Practical consequences:

- compare several fields with `std::tie(a.x, a.y) < std::tie(b.x, b.y)` instead of hand-written chains of `if`;
- never use `<=` or `>=` as a comparator, and never subtract integers to compare (overflow);
- keep floating-point keys free of NaN, or give NaN an explicit place in the order;
- the comparator must not change while elements are inside, and the key must not change either.

**Transparent comparators (C++14).** `set<std::string>::find("gamma")` builds a temporary `std::string` for each call. With a comparator that declares `is_transparent`, `find`, `lower_bound`, `count` and the other lookups accept any type the comparator can compare. The `Key` and `KeyLess` pair at the end of the listing of section 4 counts constructions: 1000 finds with a temporary `Key` made **1000** constructions; the same 1000 finds with `std::string_view` and the transparent comparator made **0**. The standard spelling for strings is `std::set<std::string, std::less<>>`.

---

## 7. Node handles: re-key, move and merge without allocating

Since C++17 a node can be detached from a tree as a handle that owns it: `extract(key)` or `extract(iterator)`, then `insert(std::move(handle))`. The handle gives non-const access to the element, **including the key** of a map or set. The only way to re-key a map entry is therefore:

```cpp
// Change the key of an element without allocating, copying or moving the value (C++17).
template <class Map>
bool rekey(Map& m, const typename Map::key_type& from, const typename Map::key_type& to) {
    auto nh = m.extract(from);                                 // unlinks the node: O(log n), no allocation
    if (nh.empty()) return false;                              // no such key
    nh.key() = to;                                             // the only way to modify a key
    auto r = m.insert(std::move(nh));
    if (r.inserted) return true;
    r.node.key() = from;                                       // `to` was taken: the handle comes back, restore it
    m.insert(std::move(r.node));
    return false;
}

// The elements of a set are const, so a string cannot be moved out of it; through a node handle it can.
inline std::optional<std::string> takeOut(std::set<std::string>& s, const std::string& key) {
    auto nh = s.extract(key);
    if (nh.empty()) return std::nullopt;
    return std::move(nh.value());                              // moves the string out; the node is freed with the handle
}

// merge() moves the nodes whose keys are not yet in the target; clashing ones stay in the source.
inline std::size_t mergeCount(std::set<int>& into, std::set<int>& from) {
    std::size_t before = from.size();
    into.merge(from);
    return before - from.size();
}
```

`rekey` was tested on a map of strings: the element keeps its address (`&m["z"]` after equals `&m["a"]` before), a missing source or an occupied target leaves the map unchanged (the failed insert hands the node back in `r.node`, which the function puts back under the old key), and `rekey(m, k, k)` works. On 100 integer keys with the counting allocator of section 2, re-keying every element made **0** allocations, while the erase-and-emplace form made 100. On 200,000 entries of `map<int, std::string>` (40-character values), re-keying all of them took 55.2 ms with `extract` and 58.4 ms with erase and emplace: the saving is real in allocation count but small in time here, because the tree walks dominate. Where it pays is when the value is expensive or impossible to move, or when stable addresses must survive.

`takeOut` shows the other use: the elements of a set are const, so `std::move(*it)` copies; the handle's `value()` is non-const, and the test confirms the 100-character string's heap buffer was moved, not copied. `merge` moves every node whose key is absent from the target and leaves the clashing ones in the source (the test merges `{1,2,3}` and `{3,4,5}`: two nodes move, `3` stays behind, and the address of 4 is unchanged). `extract` by key costs `O(log n)`, and by iterator amortised `O(1)`, and invalidates only iterators to the extracted element (cppreference).

---

## 8. The GNU order-statistics tree

A standard `std::set` cannot answer "what is the k-th smallest element" or "how many elements are smaller than x" better than `O(n)`: `std::next(s.begin(), k)` walks. GCC ships an extension, policy-based data structures, in which a tree can maintain subtree sizes in its nodes. That is the `select` and `rank` of the Extra of Topic 07, ready made:

```cpp
// GNU policy-based data structures (GCC only): a red-black tree whose nodes also keep subtree sizes.
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>
using OrderedSet = __gnu_pbds::tree<int, __gnu_pbds::null_type, std::less<int>, __gnu_pbds::rb_tree_tag,
                                    __gnu_pbds::tree_order_statistics_node_update>;

inline std::optional<int> kthKey(const OrderedSet& s, std::size_t k) {      // k from 0
    auto it = s.find_by_order(k);
    if (it == s.end()) return std::nullopt;
    return *it;
}
inline std::size_t rankOf(const OrderedSet& s, int key) {      // how many keys are smaller than `key`
    return s.order_of_key(key);
}

// A multiset with ranks: make the keys unique by pairing each with a serial number.
using OrderedMulti = __gnu_pbds::tree<std::pair<int, int>, __gnu_pbds::null_type, std::less<std::pair<int, int>>,
                                      __gnu_pbds::rb_tree_tag, __gnu_pbds::tree_order_statistics_node_update>;
inline std::size_t countLess(const OrderedMulti& s, int key) { return s.order_of_key({key, INT_MIN}); }
inline std::size_t countAtMost(const OrderedMulti& s, int key) { return s.order_of_key({key, INT_MAX}); }
```

`find_by_order(k)` counts from 0 and returns `end()` past the last element; `order_of_key(x)` is the number of keys strictly less than `x`. The tests compared both with a sorted vector on 100 random sets of up to 150 keys (every index and every key from -2 to 151), and the multiset variant with counts on 300 inserted keys. For `OrderedSet` after inserting 50, 10, 40, 20, 30: `kthKey(2)` is 30, `rankOf(35)` is 3, `rankOf(40)` is 3 (the keys 10, 20, 30 are smaller) and `kthKey(5)` is none.

Measured on 99,995 distinct keys, 200 random k-th queries: **0.19 microseconds** per query with the order-statistics tree against **4701.7 microseconds** with `std::next` on a `std::set`, a factor of about 25,000. The node costs the same 48 heap bytes per element as the standard set in my measurement (section 2).

Caveats, because an interviewer will ask: it is a GCC extension (libstdc++ only; not available with libc++ or MSVC); it has no `multiset` form, hence the (key, serial number) pairs; the pairs make `count of keys <= x` need a serial bound (`INT_MAX` above, so no real serial may equal it); and the extra bookkeeping makes inserts and erases slightly more expensive. Where portability matters, write the augmented tree yourself (Topic 30) or rank over compressed coordinates with a Fenwick tree.

---

## 9. Choosing the container

| Need | Choice | Why |
|---|---|---|
| Ordered, changing, range queries, stable addresses | `std::set` / `std::map` | the guarantees of section 1 |
| Build once, query many times, ordered | sorted `std::vector` + `std::lower_bound` | 184.2 ns against 1122.6 ns per lookup, 4 against 48 bytes per key |
| Lookup only, no order | `std::unordered_set` / `unordered_map` | 40.9 ns per lookup |
| k-th or rank on a changing set | order-statistics tree (GCC) or your own augmented tree | section 8 |
| Re-key or move elements between containers | node handles | section 7 |
| Min or max only, repeatedly | `priority_queue` (the heap topics later in this plan) | no order maintenance needed |

A C++23 standard library adds `std::flat_map` and `std::flat_set`, which are sorted vectors behind the map interface (from memory; GCC 13 does not have them). Benchmarking `std::map` against a sorted vector and a B-tree style container is the subject of Topic 33.

---

## 10. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Comparator with `<=` or `>=` | Duplicates accepted, lookups and erases fail (the lost fives) | Strict `<` |
| NaN in a floating-point key | Keys silently dropped or lookups wrong | Exclude NaN or order it explicitly |
| Changing a key through a cast | Tree order broken, `find` fails | Erase and insert, or `extract` and re-key |
| `m[k]` to test presence | Inserts a default element | `find`, `count` or `contains` |
| `multiset::erase(key)` meaning "one" | Removes every equal element | `erase(find(key))` |
| `multiset::count` in a hot loop | `O(log n + count)` each time | Keep the iterators from `equal_range` |
| `std::next(s.begin(), k)` for k-th | `O(k)`, 4701.7 microseconds at 100,000 keys | Order-statistics tree |
| Decrementing `lower_bound` before checking `begin()` | Undefined behaviour | Test `it == begin()` first |
| Erasing in a loop with `erase(it); ++it;` | Use of an invalid iterator | `it = erase(it)` |
| Building a temporary key for each `find` | Allocation per lookup | Transparent comparator |
| Expecting cache-friendly scans | 183 ns per element on a shuffled build | Sorted vector, or reserve in a pool |
| Peeking at `_M_node` in portable code | Does not compile elsewhere | Only for learning and tests |

---

## 11. Questions to answer out loud

1. What does the standard require of `std::set`, and why does that force a balanced node-based tree?
2. Draw the libstdc++ layout: node, header, root, leftmost, rightmost. Why does the root's parent point at the header?
3. How many bytes does one `std::set<int>` element cost, in the request and on the heap? Where do the 40 come from?
4. Why is a scan of a `std::set` much slower than of a vector even though both are `O(n)`?
5. State the red-black rules and derive `height <= 2 log2(n + 1)`.
6. Why are the elements of a set const? What happens if you change one?
7. How do you compute floor, ceiling, lower and higher with `lower_bound` and `upper_bound`?
8. What do `erase(key)`, `erase(iterator)` and `count` cost and do on a `multiset`?
9. State the strict weak ordering rules and give two comparators that break them. What did the `<=` set do?
10. What is a transparent comparator, and what does it save?
11. How do node handles let you re-key a map element? What is guaranteed about its address?
12. How would you get the k-th element of a changing set in `O(log n)`, and what are the portability limits?

---

## 12. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 12.1 On paper

**Problem A.** A program stores 50 million `int` keys in a `std::set<int>` on libstdc++ with glibc malloc. Estimate the memory for the elements in megabytes, and the same for a `std::vector<int>`.

*Where:* paper. *Goal:* two numbers and a ratio. *How:* use the heap bytes per element of section 2.

**Problem B.** Trace `std::set<int, LessEqual>` inserting 5, 5, 3, 5 with the insert rule "go left if `comp(k, node)`, else right". Draw the tree before any rebalancing and explain why `lower_bound(5)` returns `end()`.

*Where:* paper. *Goal:* a tree and a reason. *How:* the insert descent asks `comp(k, node)`; `lower_bound` goes left only when `!comp(node, k)`.

**Problem C.** Insert 5, 3, 5, 3, 5 into a `multiset<int>` and then run `erase(5)`; separately run `erase(find(5))`. Give the contents after each.

*Where:* paper. *Goal:* two sequences. *How:* `erase(key)` removes the whole equal range.

**Problem D.** For the set `{4, 8, 15, 16, 23, 42}` give floor, ceil, lower and higher of 15, 16, 20 and 50.

*Where:* paper. *Goal:* a table of 16 entries. *How:* `lower_bound` and `upper_bound`, then one step back.

**Problem E.** A red-black tree has black height 5. What are the smallest and largest possible numbers of nodes and the largest possible height? Check the height bound against 37 for a million sorted keys.

*Where:* paper. *Goal:* three numbers. *How:* a path alternates at most one red between blacks; the smallest tree is perfect and all black.

**Problem F.** For `std::map<std::string, int> m{{"a",1},{"b",2},{"c",3}}`, give the result and the map after `rekey(m, "a", "c")`, `rekey(m, "a", "d")` and then `rekey(m, "a", "e")`.

*Where:* paper. *Goal:* three results and a map. *How:* a failed insert returns the node, which is put back.

**Problem G.** For an `OrderedSet` that received 50, 10, 40, 20, 30, give `kthKey` for 0, 2 and 5 and `rankOf` for 5, 30, 35 and 100.

*Where:* paper. *Goal:* seven values. *How:* `rankOf` counts strictly smaller keys.

### 12.2 In your editor

For each, write the straightforward version first, then the one that uses the tool of this tutorial, and compare on random inputs.

1. **My Calendar I (729).** Where: LeetCode. Goal: reject overlapping bookings. How: `lower_bound` on the start times, check one neighbour on each side.
2. **Contains Duplicate III (220).** Where: LeetCode. Goal: two values within a distance and an index window. How: a `set` of the window; `lower_bound(x - t)` and compare.
3. **The Skyline Problem (218).** Where: LeetCode. Goal: the outline of buildings. How: a sweep with a `multiset` of heights; erase one copy, not all.
4. **Data Stream as Disjoint Intervals (352).** Where: LeetCode. Goal: merged intervals after each added number. How: a `map` start to end, with `upper_bound` and one step back.
5. **Stock Price Fluctuation (2034).** Where: LeetCode. Goal: latest, highest and lowest price with corrections. How: a map timestamp to price and a `multiset` of prices.
6. **Smallest Number in Infinite Set (2336).** Where: LeetCode. Goal: pop the smallest, add back. How: a `set` of re-added numbers and a counter.
7. **Exam Room (855).** Where: LeetCode. Goal: seat the student farthest from others. How: an ordered set of seats and a scan of gaps; then a faster design.
8. **Count of Smaller Numbers After Self (315).** Where: LeetCode. Goal: for each element, how many smaller ones follow. How: the order-statistics tree from the right, `rankOf`; then the merge-sort version.
9. **Create Sorted Array through Instructions (1649).** Where: LeetCode. Goal: total cost of inserting values with strictly-smaller and strictly-larger counts. How: `OrderedMulti` with `countLess` and the complement of `countAtMost`.
10. **Closest Nodes Queries in a Binary Search Tree (2476).** Where: LeetCode. Goal: floor and ceiling of each query in a BST. How: inorder into a vector and `lower_bound`, or `floorOf` and `ceilOf` on a set.

**Extra.** Write a `std::set` of one million random integers, then measure `begin()`-to-`end()` iteration twice: once as built, and once after copying it (`std::set<int> t = s;`). The copy allocates its nodes in sorted order. Explain what you see with the numbers of section 2.

---

## Answers

**Problem A.** At 48.0 heap bytes per element, the set needs 50,000,000 x 48 = 2400 MB (2.4 GB); the vector needs 50,000,000 x 4 = 200 MB. The ratio is 12.

**Problem B.** Inserting 5: the root. Second 5: `comp(5, 5)` is true, go left, so it becomes the left child of the first 5. Then 3: `comp(3, 5)` is true, go left to the second 5, `comp(3, 5)` true again, left child of it. The third 5: `comp(5, 5)` true, go left to the second 5, `comp(5, 5)` true, go left to 3, `comp(5, 3)` false, go right: the right child of 3. So the tree is a left chain 5, 5, 3 with 5 under 3, size 4. `lower_bound(5)` walks down asking whether the node's key is not less than 5; with `<=` the answer for a key of 5 is "less", so it goes right at every 5, ends up at null and returns `end()`. The tests observed `size` 4, `count(5)` 0, `find(5)` fails and `erase(5)` removes 0.

**Problem C.** After the inserts: 3, 3, 5, 5, 5. `erase(5)` leaves 3, 3 (it removed three). `erase(find(5))` leaves 3, 3, 5, 5.

**Problem D.**

| Key | floor | ceil | lower | higher |
|---|---|---|---|---|
| 15 | 15 | 15 | 8 | 16 |
| 16 | 16 | 16 | 15 | 23 |
| 20 | 16 | 23 | 16 | 23 |
| 50 | 42 | none | 42 | none |

**Problem E.** The smallest tree has all nodes black and perfect: `2^5 - 1 = 31` nodes. The largest height is `2 x 5 = 10` (alternating red and black on the longest path); the largest tree with black height 5 has height 10 and `2^10 - 1 = 1023` nodes when it is perfect (all levels full, with the red levels in between). For a million keys, `2 log2(1,000,001)` is 39.86, so the height is at most 39, and the measured 37 is within the bound.

**Problem F.** `rekey(m, "a", "c")`: false (c is taken), map unchanged. `rekey(m, "a", "d")`: true, map is `b:2, c:3, d:1`. `rekey(m, "a", "e")`: false, because the key `a` no longer exists, and the map is unchanged.

**Problem G.** The sorted keys are 10, 20, 30, 40, 50. `kthKey(0)` is 10, `kthKey(2)` is 30, `kthKey(5)` is none. `rankOf(5)` is 0, `rankOf(30)` is 2, `rankOf(35)` is 3 and `rankOf(100)` is 5.

**Problems 12.2.** All follow the tools of the tutorial. For 855 and 220 write a brute force over small inputs and compare; for 315 and 1649 compare the order-statistics tree with a quadratic count.

---

## Cheat sheet

- The standard gives complexity and stability, which forces a node-based balanced tree; all three major libraries use a red-black tree with a header node and a stored size.
- libstdc++ node: 32-byte base (colour, parent, left, right) plus the value; `set<int>` requests 40 bytes per element and uses 48 on glibc; the container is 48 bytes.
- Header: parent = root, left = leftmost, right = rightmost: `begin()` and the maximum are `O(1)`.
- Real heights stay below `2 log2(n + 1)` (37 for a million sorted keys); the cost is the cache, not the comparisons.
- Iterators and addresses survive other inserts and erases; elements are const; erase in a loop with `it = erase(it)`.
- `lower_bound` = first not less; `upper_bound` = first greater; floor = step back from `upper_bound`; `multiset::erase(key)` removes all copies.
- Comparator: strict weak ordering, never `<=`, no NaN; transparent comparators avoid temporary keys.
- `extract` + `insert` re-keys without allocation and keeps the address; `merge` moves non-clashing nodes.
- k-th and rank: GNU order-statistics tree (`find_by_order`, `order_of_key`), 0.19 microseconds against 4701.7 for `std::next`; GCC only.

---

## Sources

- Titles and numbers of 220, 218, 315, 352, 729, 855, 1649, 2034, 2336 and 2476 were checked against [a list of ordered-set problems](https://lyte-code.pages.dev/topics/Ordered%20Set) and [a LeetCode BST problem list](https://walkccc.me/LeetCode/topics/data-structures/tree/binary-search-tree). Difficulty labels are not used because I could not verify them.
- libstdc++: the header `bits/stl_tree.h` of the GCC 13 installation used for the tests.
- libc++: [the `__tree` header](https://llvm.googlesource.com/libcxx/+/master/include/__tree) (red-black rebalancing functions, colour flag, begin node and size stored in the tree).
- Microsoft STL: [a post of August 7, 2023 on the Old New Thing blog](https://devblogs.microsoft.com/oldnewthing/20230807-00/?p=108562) for the red-black tree, the external header node and `_Isnil` (I did not note the post's title).
- Node handles and `multiset::insert`: cppreference pages for [`set::extract`](https://en.cppreference.com/cpp/container/set/extract) and [`multiset::insert`](https://cppreference.com/cpp/container/multiset/insert).
- Marked "from memory" in the text: the standard's complexity and invalidation wording, the cost of `count` on a `multiset`, and `std::flat_map`.
- All other claims in this tutorial come from the tests and measurements described at the top.
