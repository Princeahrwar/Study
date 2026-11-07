# DSA 07.15: Binary Heaps: Layout, Sift, Build-Heap in O(n), Heapsort and the C++ Library

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md) (complete trees and height), [Rotations and AVL trees](10_rotations_and_avl_trees.md) (for the comparison with a search tree), and [Sorting algorithms inside](../03_binary_search_and_sorting/04_sorting_algorithms_inside.md) (heapsort in the sorting family). There is no companion demo file: every listing is tested code in this file.*

By the end of this tutorial you can:

- lay a complete binary tree out in an array, and move between parent and children with index arithmetic in both 0-based and 1-based form;
- write `sift_up` and `sift_down`, say why each costs `O(log n)`, and turn them into `push`, `pop`, `top` and `replace_top`;
- **prove that build-heap is `O(n)`**, say what its exact worst-case comparison count is, and say why `n` pushes cost more;
- explain Floyd's bottom-up sift-down, which halves the comparisons of a pop, and recognize it in the standard library;
- write heapsort, state its costs, and say why it is not stable and why `std::sort` still beats it;
- use `std::priority_queue`, `std::make_heap`, `push_heap`, `pop_heap`, `sort_heap` and `is_heap_until` with the right comparator, including for move-only elements.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 1,150,040 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: **every permutation** of 1 to 8 distinct values (46,233 permutations) through build-heap, `n` pushes, both pop variants, a min-heap and both heapsorts; 150 random sequences of push, pop, Floyd pop and `replace_top` against `std::priority_queue`; a min-heap of pairs and a heap of `std::unique_ptr` against reference containers; a brute-force search for the **worst-case number of comparisons of build-heap for every `n` from 1 to 9** (362,880 permutations at `n = 9`), checked against a closed formula; comparison bounds for inputs that are sorted, reversed and random up to 100,000 elements; a damage test that puts a violation at every position of a 63-element heap and requires the checker to see it; and the usage snippets of section 6. Finally the tests were attacked with 48 hand-made mutants of the code (wrong index formulas, a flipped comparison, a skipped sift, a wrong bound in heapsort and so on), and all 48 were caught. Comparison counts are program output and are the same on every run. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, best of 3, with `int` elements; read them as shapes, not benchmarks. Claims from other sources are marked "from memory" or name the source.

---

## 1. What a heap is, and where it lives

A **max-heap** is a binary tree in which every node is at least as large as each of its children. A min-heap reverses the order. Two things are *not* required, and they are the whole difference from a search tree:

- The two children of a node are **not ordered** with respect to each other.
- The only fast query is the **extreme element**, at the root: `top()` is `O(1)`. Finding an arbitrary key is `O(n)`.

What a heap gives up in queries it gains in shape: it is always a **complete binary tree** (every level full except possibly the last, which is filled from the left), so it needs no pointers and no balancing. It is stored level by level in an array. With 0-based indices:

```
node i      parent (i - 1) / 2         left child 2i + 1       right child 2i + 2
```

```
index:   0   1   2   3   4   5   6
value:   9   5   8   1   3   2   7        9
                                         ├── 5
                                         │    ├── 1
                                         │    └── 3
                                         └── 8
                                              ├── 2
                                              └── 7
```

With 1-based indices (common in textbooks and CLRS) the formulas are `parent(i) = i / 2`, `left = 2i`, `right = 2i + 1`: the same tree, shifted by one. A complete tree with `n` nodes has height `floor(log2 n)` edges, so every path from the root is that short; that single fact makes everything below `O(log n)`.

```cpp
// A binary heap is a complete binary tree stored level by level in an array (0-based).
// Cmp(a, b) is true when a has LOWER priority than b, as in std::priority_queue,
// so std::less gives a max-heap and std::greater a min-heap.
inline std::size_t hparent(std::size_t i) { return (i - 1) / 2; }
inline std::size_t hleft(std::size_t i) { return 2 * i + 1; }
inline std::size_t hright(std::size_t i) { return 2 * i + 2; }
inline int hheight(std::size_t n) { int h = 0; while (n > 1) { n >>= 1; ++h; } return h; }   // edges on the longest path, floor(log2 n)

template <class T, class Cmp> bool heap_ok(const std::vector<T>& a, Cmp c) {
    for (std::size_t i = 1; i < a.size(); ++i) if (c(a[hparent(i)], a[i])) return false;   // no child may beat its parent
    return true;
}
```

`heap_ok` is the checker: it walks the array once and requires that no child beats its parent, with the comparator convention of the standard library: `Cmp(a, b)` is true when `a` has **lower** priority than `b`. So `std::less` gives a max-heap and `std::greater` a min-heap. Equal keys are allowed anywhere.

Why an array and not nodes? There is no per-node overhead (a `Heap<int>` is a `std::vector<int>` plus a comparator: 4 bytes per element, against the 48 bytes per element measured for `std::set<long>` in [the Linux rbtree tutorial](13_linux_kernel_rbtree.md)), the whole thing is one contiguous block, and the shape stays complete with no work: growing by one element appends at index `n`, and shrinking removes index `n - 1`.

Index arithmetic checks, used in the tests for the first 100,000 indices: `parent(left(i)) = i`, `parent(right(i)) = i`, `right(i) = left(i) + 1`, and `hheight(n) = floor(log2 n)` for every `n` up to 70,000. For instance `parent(10) = 4`, the children of 7 are 15 and 16, `parent(6) = 2`, and `hheight` is 2 for 7 nodes, 3 for 8 nodes and 19 for 1,000,000 nodes (program output).

---

## 2. Sift up, sift down, push, pop

Every heap operation puts the array back into shape by moving **one value along one root-to-leaf path**.

```cpp
// Both sifts carry the moving value in a "hole" and shift the others, so each level costs
// one move instead of the three moves of a swap.
template <class T, class Cmp> void sift_up(std::vector<T>& a, std::size_t i, Cmp& c) {
    T v = std::move(a[i]);
    while (i > 0) {
        std::size_t p = hparent(i);
        if (!c(a[p], v)) break;
        a[i] = std::move(a[p]); i = p;
    }
    a[i] = std::move(v);
}
// Textbook sift-down within a[0, n): two comparisons per level (child against child, winner against v).
template <class T, class Cmp> void sift_down(std::vector<T>& a, std::size_t i, std::size_t n, Cmp& c) {
    T v = std::move(a[i]);
    for (;;) {
        std::size_t k = hleft(i);
        if (k >= n) break;
        if (k + 1 < n && c(a[k], a[k + 1])) ++k;       // the better child
        if (!c(v, a[k])) break;                         // v already beats it
        a[i] = std::move(a[k]); i = k;
    }
    a[i] = std::move(v);
}
// Floyd's variant (what libstdc++ does): one comparison per level on the way down to a leaf,
// then climb back up from there. The moved-down value usually belongs near the bottom.
template <class T, class Cmp> void sift_down_floyd(std::vector<T>& a, std::size_t i, std::size_t n, Cmp& c) {
    T v = std::move(a[i]);
    const std::size_t top = i;
    for (;;) {
        std::size_t k = hleft(i);
        if (k >= n) break;
        if (k + 1 < n && c(a[k], a[k + 1])) ++k;
        a[i] = std::move(a[k]); i = k;                  // always take the better child, no test against v
    }
    while (i > top) {
        std::size_t p = hparent(i);
        if (!c(a[p], v)) break;
        a[i] = std::move(a[p]); i = p;
    }
    a[i] = std::move(v);
}
```

- **`sift_up(i)`** repairs a heap whose only bad node is `i`, which may now beat its parent: swap upward while it does. At most `hheight(n)` steps, one comparison each.
- **`sift_down(i, n)`** repairs a heap whose only bad node is `i`, which may now be worse than a child: swap downward with the **better child** while it is beaten. At most `hheight(n)` steps, and **two comparisons per step**: child against child, then the winner against the value.
- Both carry the moving value in a **hole** and shift the others one level, so a step costs one move instead of the three moves of a swap.

```cpp
template <class T, class Cmp = std::less<T>> class Heap {
    std::vector<T> a_; Cmp c_;
public:
    explicit Heap(Cmp c = Cmp()) : c_(c) {}
    explicit Heap(std::vector<T> v, Cmp c = Cmp()) : a_(std::move(v)), c_(c) { heapify(); }
    // Build in O(n): sift down every internal node, last to first.
    void heapify() { for (std::size_t i = a_.size() / 2; i-- > 0;) sift_down(a_, i, a_.size(), c_); }
    bool empty() const { return a_.empty(); }
    std::size_t size() const { return a_.size(); }
    const T& top() const { return a_[0]; }
    const std::vector<T>& data() const { return a_; }
    void push(T v) { a_.push_back(std::move(v)); sift_up(a_, a_.size() - 1, c_); }
    T pop() {                                           // move the last element to the root and sift it down
        T r = std::move(a_[0]);
        T last = std::move(a_.back()); a_.pop_back();
        if (!a_.empty()) { a_[0] = std::move(last); sift_down(a_, 0, a_.size(), c_); }
        return r;
    }
    T pop_floyd() {
        T r = std::move(a_[0]);
        T last = std::move(a_.back()); a_.pop_back();
        if (!a_.empty()) { a_[0] = std::move(last); sift_down_floyd(a_, 0, a_.size(), c_); }
        return r;
    }
    // Replace the root and restore the heap with one sift: cheaper than pop() followed by push().
    T replace_top(T v) { T r = std::move(a_[0]); a_[0] = std::move(v); sift_down(a_, 0, a_.size(), c_); return r; }
};
```

- **push** appends at the end (the next free slot of the complete tree) and sifts up. **pop** takes the root, moves the **last** element into the root (so the shape stays complete) and sifts it down. **top** is `a[0]`.
- `replace_top(v)` swaps in a new root and sifts down once. It is the same result as `pop()` followed by `push(v)`, with one sift instead of two (section 7 measures it).
- The constructor from a vector calls `heapify`, the build-heap of section 3.

A trace (program output). Pushing 5, 3, 8, 1, 9, 2 one by one gives, after each push:

```
5 | 5 3 | 8 3 5 | 8 3 5 1 | 9 8 5 1 3 | 9 8 5 1 3 2
```

(the 8 swaps with the 5 at the root, and the 9 rises past the 3 and the 8). Then three pops: `9` leaves `8 3 5 1 2`, `8` leaves `5 3 2 1`, `5` leaves `3 1 2`. Each pop moved the last element to the root and sifted it down.

Complexity: `push` and `pop` are `O(log n)` in the worst case. A pop that moves a leaf value to the root almost always sinks it to near the bottom, so a pop costs close to its worst case, while a push is usually cheap, because a new value is typically small and stops near the bottom. Measured on a random heap of 1,000,000 elements (see section 3 for the build side), a **pop costs 34.91 comparisons on average** with this `sift_down` against a worst case of `2 log2 n = 39.86`.

---

## 3. Build-heap in O(n)

Given an unsorted array, make it a heap: for `i` from the last internal node down to the root, call `sift_down(i)`. Nodes at the bottom level are already heaps, so the loop starts at `n / 2 - 1` (the code's `heapify`).

It looks like `n / 2` calls of an `O(log n)` operation, so `O(n log n)`. It is `O(n)`, because **most nodes are near the bottom, where a sift-down is short**. A node at height `h` (the longest path down to a leaf) can sink at most `h` levels, and there are at most `ceil(n / 2^(h+1))` nodes at height `h`. So the total number of levels descended is at most

```
sum over h of  ceil(n / 2^(h+1)) * h   <=   n * sum over h>=0 of h / 2^(h+1)   =   n * 1   =   n
```

(because `sum of h / 2^h` over `h >= 0` is 2; the `+1` that each ceiling can add contributes at most `sum of h` over the `log2 n` heights, which is `O(log^2 n)` and does not matter). At two comparisons per level the build uses **at most about `2n` comparisons**; the tests check `<= 2n` for sorted, reversed and random input at ten sizes up to 100,000.

The **exact worst case** of this build is `2n - 2 s2(n) - e2(n)` comparisons, where `s2(n)` is the number of 1 bits of `n` and `e2(n)` is the number of trailing zero bits (the formula is the known result of Suchenek, from memory). I did not trust the memory: the tests find the worst case **by brute force over all permutations** for `n = 1` to `9` and the formula matches every time. For example `n = 7` gives 8 comparisons (average 7.714 over the 5,040 permutations) and `n = 9` gives 14 (average 11.644 over 362,880).

A trace of build-heap on `3 9 2 1 4 5` (program output). The last internal node is index 2: its only child is the 5, so `2` and `5` swap, giving `3 9 5 1 4 2`. Index 1 holds 9, which beats both children 1 and 4: no change. Index 0 holds 3: the better child is 9, so they swap, then 3 meets children 1 and 4 and swaps with the 4: `9 4 5 1 3 2`.

**Why `n` pushes are worse.** Building by `n` successive pushes is `O(n log n)` in the worst case, because a push sifts *up* and most nodes are near the bottom, where the path to the root is long: if the input arrives in ascending order into a max-heap, every push climbs to the root. Measured on 1,000,000 elements, comparisons per element:

| Input | sift-down build | `n` pushes | `std::make_heap` |
|---|---|---|---|
| ascending | 2.000 | 17.951 | 1.500 |
| descending | 1.000 | 1.000 | 2.000 |
| random | 1.881 | 2.281 | 1.649 |

For random input the push-based build is only about 20% more comparisons, because a random push stops after a constant number of steps on average; the damage is on adversarial order. In time on random input (best of 3) the sift-down build took **7.3 ns per element** against 13.2 ns for `n` pushes and 9.3 ns for `std::make_heap` (a second run: 8.7, 15.5 and 11.0). Build-heap is the right way to turn a vector into a heap: `std::priority_queue` has a constructor from a container for exactly that reason.

---

## 4. Floyd's sift-down: half the comparisons of a pop

The pop in section 2 spends two comparisons per level: which child is better, then whether the moved value beats it. But the value we moved to the root came from the **last** level, so it is small, and it almost always sinks all the way down. Floyd's idea: **skip the second comparison on the way down.** Follow the better child to a leaf with one comparison per level, then climb back up from the leaf to find where the value really belongs (usually one or two steps).

That is `sift_down_floyd` in the listing of section 2. On a hand example, `2 9 8 7 6 5 4 3 1` with the root's value 2 to be sifted down in a heap of the other eight (the replaced root): the textbook sift-down ends with `9 7 8 3 6 5 4 2 1` after **6 comparisons** and Floyd's after **4**, to the same result (program output).

Draining a random heap of 1,000,000 elements, average comparisons per pop:

| Pop | comparisons |
|---|---|
| textbook `sift_down` | 34.91 |
| `sift_down_floyd` | 18.65 |
| `std::pop_heap` | 18.65 |
| for reference: `2 log2 n`, `log2 n` | 39.86, 19.93 |

Floyd's pop takes about one comparison per level (18.65 against `log2 n = 19.93`; the heap shrinks while it drains, so its average height is a little less than that of the full heap), and the library matches it to the second decimal, which is consistent with what I remember of libstdc++ (its `__adjust_heap` goes to the leaf and then pushes up; from memory, not re-read). Two cautions. Floyd's version moves *more* elements when the value stops high, so it wins when comparisons are expensive (strings, or a comparator that follows pointers) and not necessarily for plain `int`: popping everything from a random heap of 1,000,000 ints took 112.2 ns per pop for the textbook and 112.5 ns for Floyd's (second run: 125.3 and 124.6), and 121.0 ns for `std::priority_queue` (135.8). Memory traffic, not comparisons, dominates for ints. And Floyd's version is not a better *push*: only the pop and the heapsort use it.

---

## 5. Heapsort

Build a max-heap in place, then repeatedly swap the root with the last element of the heap, shrink the heap by one, and sift the new root down. The sorted tail grows from the end.

```cpp
// In-place heapsort: build a max-heap (Cmp = "less"), then swap the root to the end and shrink.
template <class T, class Cmp = std::less<T>> void heapsort(std::vector<T>& a, Cmp c = Cmp()) {
    const std::size_t n = a.size();
    for (std::size_t i = n / 2; i-- > 0;) sift_down(a, i, n, c);
    for (std::size_t end = n; end-- > 1;) { std::swap(a[0], a[end]); sift_down(a, 0, end, c); }
}
template <class T, class Cmp = std::less<T>> void heapsort_floyd(std::vector<T>& a, Cmp c = Cmp()) {
    const std::size_t n = a.size();
    for (std::size_t i = n / 2; i-- > 0;) sift_down_floyd(a, i, n, c);
    for (std::size_t end = n; end-- > 1;) { std::swap(a[0], a[end]); sift_down_floyd(a, 0, end, c); }
}
```

A trace of the second phase (program output) on the heap `9 5 8 1 3 2`: swap the 9 to the end and sift: `8 5 2 1 3 | 9`; then `5 3 2 1 | 8 9`; then `3 1 2 | 5 8 9`; then `2 1 | 3 5 8 9`; then `1 | 2 3 5 8 9`. The bar separates the heap from the sorted tail.

- **Time `O(n log n)` in the worst case**, with no bad inputs: the build is `O(n)` and the `n - 1` sifts are `O(log n)` each.
- **Space `O(1)`**: it sorts in place. That, together with the worst-case guarantee, is why introsort-style hybrids fall back to it when quicksort recurses too deep (from memory of how `std::sort` is implemented).
- **Not stable.** The tests search all arrangements of the keys `0 0 0 1` tagged with their original positions and find a case where equal keys come out swapped, so there is no hidden stability to rely on.
- Comparisons per element on 1,000,000 elements:

| Input | heapsort | Floyd heapsort | `make_heap` + `sort_heap` | `std::sort` |
|---|---|---|---|---|
| ascending | 37.69 | 20.40 | 20.40 | 25.60 |
| descending | 36.00 | 20.75 | 20.75 | 18.13 |
| random | 36.80 | 20.29 | 20.29 | 24.33 |

(`log2 n` is 19.93; the information-theoretic minimum for sorting is about `log2 n - 1.44` comparisons per element.) Floyd's variant brings heapsort to about `n log2 n` comparisons, fewer than `std::sort` makes on random input.

- **Time: heapsort loses anyway.** On random `int` input of 1,000,000 elements: heapsort 124.9 ns per element, Floyd 128.4, the library's `make_heap` + `sort_heap` 117.6, and `std::sort` **58.2** (second run: 134.9, 145.4, 122.5 and 69.1). Heapsort's accesses jump between a node and its children far apart in the array at every level, with no locality to exploit, while quicksort scans memory sequentially. Fewer comparisons did not make Floyd's version faster here.

---

## 6. The C++ library

`std::priority_queue<T, Container, Compare>` is an adaptor over a container (a `std::vector` by default) kept as a max-heap with respect to `Compare`. The interface is deliberately small: `push`, `emplace`, `top` (a **const** reference), `pop` (returns `void`), `size`, `empty`. There is no iteration, no `erase`, and no way to change an element's priority.

The same machinery is exposed on any random-access range in `<algorithm>`:

| Function | Does | Cost (standard, from memory) |
|---|---|---|
| `make_heap(first, last, cmp)` | build-heap | at most 3n comparisons |
| `push_heap` | the last element is the new one; sift it up | at most log n comparisons |
| `pop_heap` | move the best to the **back**, repair the rest | at most 2 log n comparisons |
| `sort_heap` | turn a heap into a sorted range | at most 2n log n comparisons |
| `is_heap`, `is_heap_until` | check; first position that breaks the property | linear |

Measured: `make_heap` took 1.65 comparisons per element on random input (at most 2.0 in the worst case seen), and `pop_heap` 18.65 per pop, both well below the bounds.

```cpp
// 1. A min-heap with std::priority_queue: greater<> flips the default max-heap.
//    Pairs compare lexicographically, so equal distances are broken by the node number.
inline std::vector<int> nodes_by_distance(const std::vector<std::pair<int, int>>& dist_node) {
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>> q;
    for (auto& p : dist_node) q.push(p);
    std::vector<int> out;
    while (!q.empty()) { out.push_back(q.top().second); q.pop(); }
    return out;
}

// 2. Your own type: the comparator answers "is a LESS urgent than b?" (true puts a below b).
struct Job { int prio; std::string name; };                       // a lower prio number is more urgent
struct LessUrgent { bool operator()(const Job& a, const Job& b) const { return a.prio > b.prio; } };
inline std::vector<std::string> run_order(const std::vector<Job>& jobs) {
    std::priority_queue<Job, std::vector<Job>, LessUrgent> q(LessUrgent(), jobs);   // builds in O(n) from the container
    std::vector<std::string> out;
    while (!q.empty()) { out.push_back(q.top().name); q.pop(); }
    return out;
}

// 3. A move-only element: priority_queue::top() is const, so it cannot be moved out.
//    Keep the heap in a vector and use pop_heap, which moves the best element to the back.
inline std::unique_ptr<int> pop_move_only(std::vector<std::unique_ptr<int>>& v) {
    auto cmp = [](const std::unique_ptr<int>& a, const std::unique_ptr<int>& b) { return *a < *b; };
    std::pop_heap(v.begin(), v.end(), cmp);
    std::unique_ptr<int> r = std::move(v.back());
    v.pop_back();
    return r;
}

// 4. Where does the heap property first fail? (is_heap_until returns the first bad position.)
inline std::size_t first_violation(const std::vector<int>& a) { return (std::size_t)(std::is_heap_until(a.begin(), a.end()) - a.begin()); }
```

Four points from the snippets:

1. **Min-heap:** pass `std::greater<T>` as the comparator (and the container type before it). `std::pair` compares lexicographically, so `(distance, node)` pairs come out by distance and then by node. This is the idiom for Dijkstra's priority queue (Topic 16 and the graphs area).
2. **Your own type:** the comparator answers "is `a` less urgent than `b`?". It is the reverse of how you would say "a goes first". The `Job` example puts the *lowest* priority number on top, so its comparator uses `>`.
3. **Move-only elements:** `top()` is const, so `std::priority_queue<std::unique_ptr<T>>` cannot give the element up. Keep a vector and use `pop_heap`, which moves the best element to the back where you may move it out (`pop_move_only`).
4. **Constructing from a container** (`q(cmp, container)`) calls `make_heap`: `O(n)`, not `n` pushes.

The comparator must be a **strict weak ordering**, as for `std::sort`. A comparator that returns true for equal elements (`<=`) is undefined behaviour, even though it often appears to work on small tests.

---

## 7. How fast is it?

Measured on this machine, steady state: a heap of `m` ints, and 2,000,000 operations that each remove the top and add a random value, in nanoseconds per operation (best of 3; first run, then second run):

| `m` | heap data | textbook pop + push | Floyd pop + push | `replace_top` | `std::priority_queue` |
|---|---|---|---|---|---|
| 2^10 | 4 KB | 12.0 / 12.0 | 19.0 / 19.4 | 2.0 / 2.1 | 19.0 / 20.1 |
| 2^14 | 64 KB | 37.7 / 34.7 | 34.9 / 32.4 | 5.7 / 5.6 | 33.5 / 37.3 |
| 2^18 | 1 MB | 54.0 / 75.7 | 54.8 / 66.6 | 29.0 / 36.9 | 54.4 / 69.4 |
| 2^22 | 16 MB | 206.7 / 246.4 | 236.0 / 231.3 | 200.6 / 184.4 | 231.6 / 221.9 |

Three things to read from it. First, the cost per operation grows much faster than `log m` as the heap outgrows the caches: from 2^10 to 2^22 the depth grows from 10 to 22 levels (2.2 times) while the textbook version's time grows 17 to 21 times, which points at the memory hierarchy rather than the algorithm (I did not read the cache sizes of this VM, so I cannot say where the steps are). A heap is cache-friendly near the root and hostile at the bottom (a node and its children are `i` and `2i + 1` apart), a problem that `d`-ary heaps and van Emde Boas layouts address (Topic 16). Second, the textbook and Floyd versions and the library trade places between runs; for ints there is no stable winner. Third, `replace_top` is much faster in this test, but be careful what it measures: it puts the *new random value* at the root, and a random value tends to stop within a few levels of the root, whereas `pop` + `push` moves the *last leaf* to the root, which sinks all the way. The two forms produce the same heap contents; their costs differ because the sifts start from different values. So read the table as: `replace_top` needs one sift instead of two and is the right call when you are replacing the top anyway, but the 6 to 10 times in this test comes from the value distribution and is not a general speedup. I did not test other distributions.

---

## 8. When to use a heap

| Need | Use |
|---|---|
| repeatedly take the smallest (or largest) of a changing set | binary heap (`std::priority_queue`) |
| the `k` best of a stream, in `O(n log k)` and `O(k)` space | a heap of size `k` of the *worst* kept so far (Topic 16) |
| merge `k` sorted sequences | a heap of the current heads (Topic 16) |
| sort in place with a worst-case guarantee and no extra memory | heapsort (but expect `std::sort` to be about twice as fast) |
| change the priority of, or delete, an arbitrary element | not a plain heap: an indexed heap (Topic 16), or `std::set` / `std::map` (this series, Topics 09 to 13) |
| iterate in sorted order, or find the next larger key | a search tree, not a heap |

---

## 9. Common mistakes

1. **Wrong direction of the comparator.** `std::priority_queue<int>` is a max-heap; the min-heap needs `std::greater<int>` *and* the container argument (`priority_queue<int, vector<int>, greater<int>>`). A custom comparator that says "a goes first" produces the opposite of what you want.
2. **A non-strict comparator** (`<=` or `>=`): undefined behaviour in the library. Use strict `<` and `>`.
3. **Using `top()` after `pop()`.** `top()` returns a reference into the heap; the next `pop()` or `push()` invalidates it. Copy first if you need the value.
4. **Expecting `pop()` to return the element.** It returns `void` in the standard adaptor; read `top()` first. (The `Heap` class above returns the value because it can.)
5. **Building by `n` pushes when you already have the data.** Use the container constructor or `make_heap`: `O(n)`.
6. **Mutating a key inside the heap.** Changing the priority of an element in place breaks the invariant silently. Pop and re-push, or use an indexed heap.
7. **Index off-by-one between 0-based and 1-based formulas.** In a 0-based array the root's children are 1 and 2, not 2 and 3; and the last internal node is `n / 2 - 1`, not `n / 2`.
8. **Sifting down with the wrong bound.** In heapsort the heap shrinks: `sift_down(0, end)` must stop at `end`, or it pulls the sorted tail back in.
9. **Assuming heapsort is stable**, or that equal keys keep their order in a priority queue. They do not.
10. **Comparing the heap's order with sorted order.** The array of a heap is not sorted: `9 8 5 1 3 2` is a valid heap, and the 3 comes after the 1.

---

## 10. Questions to answer out loud

1. Why is a heap always a complete tree, and why does that allow an array without pointers?
2. Give the parent and child index formulas in both 0-based and 1-based form.
3. Why does `sift_down` use two comparisons per level, and what does Floyd's variant change?
4. Prove that build-heap is `O(n)`. Why is `n` pushes `O(n log n)` in the worst case?
5. What input makes `n` pushes into a max-heap slowest?
6. Why is heapsort `O(n log n)` in the worst case, in place, and still slower than `std::sort`?
7. How do you get a min-heap from `std::priority_queue`? From `std::make_heap`?
8. Why can a `std::priority_queue<std::unique_ptr<T>>` not give up its top, and what do you do instead?
9. What does `replace_top` do, and when would you use it?
10. Why can a heap not support `find(key)` or `erase(key)` efficiently, and what do you use when you need them?

---

## 11. Problems

**A. Build.** Build a max-heap from `3 9 2 1 4 5` with the bottom-up algorithm. Show the array after each sift-down.

**B. Push and pop.** Push 5, 3, 8, 1, 9, 2 into an empty max-heap, then pop three times. Show the array after each operation.

**C. Heapsort.** Starting from the heap `9 5 8 1 3 2`, show the array and the heap size after each extraction of heapsort.

**D. Indices.** In a 0-based heap: the parent of index 10; the children of index 7; the index of the last internal node of a heap of 6 elements. Then the same three questions in the 1-based layout.

**E. Worst case.** What is the largest number of comparisons the bottom-up build can use on 7 elements? Why can it not use more?

**F. The sum.** Show that `sum over h >= 0 of h / 2^h = 2`, and use it to bound the total sift-down work of build-heap by `n` levels.

**G. Choose.** For each, name the structure: (a) the 100 largest values of a stream of a billion; (b) a task scheduler where tasks can be cancelled by id; (c) the median of a growing set (think about it, Topic 16 solves it); (d) the next event in a discrete-event simulation.

**H. Comparators.** Write the type of a min-heap of `std::pair<int, int>` in `std::priority_queue`. A colleague writes `return a.prio <= b.prio;` in a comparator; what is wrong? A `Job` comparator returns `a.prio < b.prio` and the job with the *highest* number runs first, but you want the lowest number first; what do you change?

---

## Answers

**A.** The array is `3 9 2 1 4 5`. Index 2 (value 2, one child, 5): swap, `3 9 5 1 4 2`. Index 1 (value 9, children 1 and 4): no change, `3 9 5 1 4 2`. Index 0 (value 3, children 9 and 5): swap with 9, then with 4 (children 1 and 4), `9 4 5 1 3 2`. (Program output.)

**B.** Pushes: `5` / `5 3` / `8 3 5` / `8 3 5 1` / `9 8 5 1 3` / `9 8 5 1 3 2`. Pops: `9` leaves `8 3 5 1 2`; `8` leaves `5 3 2 1`; `5` leaves `3 1 2`. (Program output.)

**C.** After the first extraction: the heap `8 5 2 1 3` (size 5) and the tail `9`. Then `5 3 2 1` (size 4) with tail `8 9`; `3 1 2` (size 3) with tail `5 8 9`; `2 1` (size 2) with tail `3 5 8 9`; `1` (size 1) with tail `2 3 5 8 9`; the array is then `1 2 3 5 8 9`. (Program output.)

**D.** 0-based: the parent of 10 is 4; the children of 7 are 15 and 16; the last internal node of 6 elements is `6 / 2 - 1 = 2`. 1-based (nodes numbered 1 to 6): the parent of 10 is 5; the children of 7 are 14 and 15; the last internal node is `6 / 2 = 3`. The 0-based index `i` corresponds to the 1-based index `i + 1`.

**E.** 8 comparisons, which is `2n - 2 s2(n) - e2(n) = 14 - 6 - 0` for `n = 7` (`s2(7) = 3` ones, no trailing zeros), found by brute force over all 5,040 permutations. The internal nodes are 0, 1 and 2. Nodes 1 and 2 each have two leaf children, so each costs at most 2 comparisons (a leaf has no children to compare with). The root can sink two levels, at 2 comparisons per level, so at most 4. The total is `2 + 2 + 4 = 8`, and the search shows it is reached.

**F.** The standard argument: with `x = 1/2`, `sum of h x^h = x / (1 - x)^2 = (1/2) / (1/4) = 2`. The nodes of height `h` number at most `ceil(n / 2^(h+1))`, each sinks at most `h` levels, so the levels are at most `sum of (n / 2^(h+1) + 1) h`. The main term is `(n / 2) * 2 = n`; the extra `+1` terms add `sum of h` over the `log2 n` heights, which is `O(log^2 n)`, negligible. At two comparisons a level, about `2n` comparisons.

**G.** (a) A min-heap of size 100: push each value; once it holds more than 100, drop the smallest (or compare with the top and use `replace_top`): `O(n log 100)`, and the memory is 100 values. (b) Not a plain heap: cancellation by id needs a search or an indexed heap, or lazy deletion (mark cancelled and skip on pop); a `std::set` keyed by (time, id) also works. (c) Two heaps, a max-heap for the lower half and a min-heap for the upper half (Topic 16). (d) A min-heap of events by time (a priority queue), with pairs `(time, sequence)` so that events at the same time keep a deterministic order.

**H.** `std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>>`. The comparator `<=` is not a strict weak ordering (`a <= a` is true), so the library's behaviour is undefined; it may appear to work on small inputs and fail on others. A comparator answers "is `a` below `b`?", and the element that is below nothing comes out on top. With `a.prio < b.prio` the job with the larger number is *above*, so it runs first. To run the lowest number first, say that a *larger* number is below: return `a.prio > b.prio`, which is what `LessUrgent` does in section 6.

---

## Cheat sheet

- **Layout:** complete tree in an array; 0-based `parent = (i - 1) / 2`, `left = 2i + 1`, `right = 2i + 2`; height `floor(log2 n)`; 1-based: `i / 2`, `2i`, `2i + 1`.
- **Comparator:** `Cmp(a, b)` true means `a` is below `b`; `less` gives a max-heap, `greater` a min-heap; strict weak ordering only.
- **push:** append and sift up. **pop:** move the last element to the root and sift down. **top:** `O(1)`. **replace_top:** one sift.
- **Build-heap:** sift down from `n / 2 - 1` to 0; `O(n)`; at most `2n` comparisons; exact worst case `2n - 2 s2(n) - e2(n)` (1.881 per element measured on random input; `n` pushes: 2.281 random, 17.951 ascending).
- **Floyd's sift-down:** go to a leaf (one comparison per level), then climb: 18.65 against 34.91 comparisons per pop at 1M; `std::pop_heap` matches it.
- **Heapsort:** build, then extract; `O(n log n)` worst case, `O(1)` space, not stable; about twice as slow as `std::sort` on random ints (124.9 against 58.2 ns per element).
- **Library:** `priority_queue` (top is const, `pop` returns void), `make_heap`, `push_heap`, `pop_heap`, `sort_heap`, `is_heap_until`; build from a container, not by pushes; move-only elements via `pop_heap`.
- A heap finds the extreme fast and nothing else; use a search tree if you need find, erase or order.

---

## Sources

- CLRS, *Introduction to Algorithms*, chapter 6 (heaps, build-heap analysis, heapsort, priority queues); the `O(n)` argument above is the standard one.
- Floyd, "Algorithm 245: Treesort 3" (1964), the bottom-up heap construction; the variant of sift-down that goes to the leaf first is usually attributed to Floyd's treesort and refined by Wegener as "bottom-up heapsort" (from memory).
- The exact worst-case count `2n - 2 s2(n) - e2(n)` for build-heap is a result of Suchenek (from memory); it was verified here by brute force for `n = 1` to `9`, not proved.
- The standard-library complexity bounds in the table of section 6 and the remark about libstdc++'s `__adjust_heap` and `std::sort`'s heapsort fallback are from memory; the matching of `std::pop_heap`'s comparison count to Floyd's was measured.
- [cppreference: std::priority_queue](https://en.cppreference.com/w/cpp/container/priority_queue) and [std::make_heap](https://en.cppreference.com/w/cpp/algorithm/make_heap) for the interface (I did not re-read them for this tutorial).
- Topics 01, 09 and 10 of this series for complete trees, node overhead, and the search-tree comparison; Topic 16 for the patterns, indexed heaps and `d`-ary heaps promised above.
- All other numbers in this tutorial come from the tests and measurements described at the top.
