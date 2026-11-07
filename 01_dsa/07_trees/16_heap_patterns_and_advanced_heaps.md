# DSA 07.16: Heap Patterns and Advanced Heaps: Top-K, Merging, Median, Scheduling, Indexed and d-ary Heaps, Pairing Heaps

*Part of the [Trees plan](00_plan.md) · Time: about 10 hours (5 reading, 5 problems) · You need: [Binary heaps](15_binary_heaps.md) (sift, build-heap, `std::priority_queue`), and [Randomized and amortized trees](14_randomized_and_amortized_trees.md) (for the amortized-bound vocabulary). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- recognize the four heap patterns (a bounded heap for **top-K**, a heap of heads for **k-way merge**, two heaps for the **running median**, a heap of end times for **scheduling**) and write each in a few lines;
- explain why the expected number of replacements in a top-K scan is `K (H_n - H_K)`, and see it in a measurement;
- build an **indexed heap** with `decrease-key`, use it in Dijkstra's algorithm, and say when it beats the lazy-deletion version with a plain `std::priority_queue`;
- write a **d-ary heap**, and say from comparison counts and timings when 4-ary is worth it;
- write a **pairing heap**, state the theory of binomial, Fibonacci and pairing heaps, and explain from a measurement why the binary heap still wins in practice.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 568,445 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: top-K against a sort, with the number of replacements against an independent `std::multiset` simulation (300 random streams, five values of K each); k-way merge against a sort (300 random sets of lists, some empty); the running median against a sorted vector after every insertion (200 streams); the meeting-room count against a brute-force count of the busiest moment (400 sets); the d-ary heap for `D` = 2, 3, 4 and 8 and the indexed heap for the same four values against `std::priority_queue` and a `std::map` model, with the heap property and the position table checked after every operation; the pairing heap against a `std::multiset` with random pushes, pops and decrease-keys and a structural checker (parent and sibling links, order, count) after every step, plus a chain 200,000 nodes deep to prove that nothing recurses; and **Dijkstra in six variants** (lazy, indexed with `D` = 2, 3, 4 and 8, and pairing) against Bellman-Ford on 400 random graphs with zero weights, self-loops, parallel edges and unreachable vertices. The structural checkers were attacked too: through a test hook (the `friend struct TestAccess` line in the classes) the tests damage a heap on purpose, one rule at a time (a child above its parent at every position, a stale position table, a broken sibling link, a wrong count), and require `valid()` to say so. Finally 82 hand-made mutants of the code were run against the tests (wrong index formulas, flipped comparisons, a dropped sift, a missing cut, a lost stale check and so on): **78 were caught and 4 are equivalent changes**, listed at the end of section 10. An independently written Python model reproduced the traces. Counts of pushes, pops, replacements and comparisons are program output and identical on every run. The timings come from one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, best of 3, two runs; read them as shapes, not benchmarks. Claims from other sources are marked "from memory" or name the source.

---

## 1. When a heap is the answer

A heap answers one question fast: **what is the best element of a changing set?** (`O(1)` to look, `O(log n)` to remove or add.) The patterns below are all disguises of that question:

| The question in the problem | The heap |
|---|---|
| "the K largest (smallest)" of something too big to sort | a heap of size K holding the K best so far |
| "the next smallest across K sorted sources" | a heap holding the current head of each source |
| "the median of everything so far" | two heaps, one for each half |
| "which resource frees up first" | a heap of finishing times |
| "the closest unfinished vertex" (Dijkstra, Prim) | a heap of tentative distances, with decrease-key |

If you also need to find, delete or reprioritize arbitrary elements, a plain heap cannot do it, and sections 6 and 7 are about the two ways around that.

---

## 2. Top-K

Find the K largest of `n` values in `O(n log K)` time and `O(K)` space, without sorting. Keep a **min-heap of the K best seen so far**: its top is the weakest of them, the one a newcomer must beat. (A max-heap would be the wrong tool: you need quick access to the element you might throw away.)

```cpp
// The k largest values of a stream, in O(n log k) time and O(k) space: keep a MIN-heap of the
//    k best seen so far; its top is the weakest of them, the one a newcomer has to beat.
inline std::vector<int> top_k_largest(const std::vector<int>& xs, std::size_t k, long* replaced = nullptr) {
    std::priority_queue<int, std::vector<int>, std::greater<int>> q;
    for (int x : xs) {
        if (q.size() < k) q.push(x);
        else if (k > 0 && x > q.top()) { q.pop(); q.push(x); if (replaced) ++*replaced; }
    }
    std::vector<int> out;
    while (!q.empty()) { out.push_back(q.top()); q.pop(); }
    std::reverse(out.begin(), out.end());                      // largest first
    return out;
}
```

A trace on the stream `3 1 4 1 5 9 2 6 5 3 5` with K = 3 (program output): the heap fills with 3, 1, 4. Then 5 beats the 1 (replacement 1), 9 beats the 3 (replacement 2), and 6 beats the 4 (replacement 3); the rest do not beat the current weakest, 5. The result is `9 6 5` with **3 replacements**.

**How many replacements?** Each replacement costs `O(log K)`; all other elements cost one comparison. If the stream is in random order, element number `i` is among the K largest of the first `i` with probability `K / i`, so the expected number of replacements is

```
sum over i = K+1 .. n of  K / i   =   K (H_n - H_K)   ~   K ln(n / K)
```

Measured on 1,000,000 random values: K = 100 gave **960** replacements against 920.5 expected, and K = 10,000 gave **45,985** against 46,051.2. The scan is therefore `n` cheap comparisons plus about `K ln(n/K)` heap operations: for K = 100 and a million elements, about 0.1% of the elements ever enter the heap. In time, the heap scan took **1.0 ns per element** for K = 100 and 5.9 ns for K = 10,000, against 7.3 and 7.5 ns for copying the array and running `std::nth_element` (the `O(n)` selection algorithm). The adversarial case is an input in ascending order: every element is a replacement and the cost is `n log K`.

Variants: the K smallest uses a max-heap of size K; the K-th largest is the top of the heap after the scan; "K most frequent" counts first (a hash map) and then runs the same scan over the counts. With `K = 0` the code returns an empty result (the `k > 0` guard keeps `q.top()` from being read on an empty heap), and with `K >= n` it sorts.

---

## 3. K-way merge

Merging `k` sorted lists with a total of `N` elements: put the head of every list in a min-heap; repeatedly pop the smallest, append it to the output and push the next element of the same list. That is `N` pops and at most `N` pushes on a heap of at most `k` elements: `O(N log k)`, against `O(N k)` for scanning all heads each time.

```cpp
// Merge k sorted lists: a min-heap holds the current head of each list.
inline std::vector<int> merge_k(const std::vector<std::vector<int>>& lists) {
    struct Head { int v; std::size_t list, idx; };
    auto later = [](const Head& a, const Head& b) { return a.v > b.v; };
    std::priority_queue<Head, std::vector<Head>, decltype(later)> q(later);
    for (std::size_t i = 0; i < lists.size(); ++i) if (!lists[i].empty()) q.push({lists[i][0], i, 0});
    std::vector<int> out;
    while (!q.empty()) {
        Head h = q.top(); q.pop();
        out.push_back(h.v);
        if (h.idx + 1 < lists[h.list].size()) q.push({lists[h.list][h.idx + 1], h.list, h.idx + 1});
    }
    return out;
}
```

Trace (program output): merging `1 4 7`, `2 5` and `3 6 8` gives `1 2 3 4 5 6 7 8`. This is the engine of an **external merge sort** (sort chunks that fit in memory, then `k`-way merge the sorted runs from disk) and of merging log files by timestamp. The same shape solves "smallest range covering one element from each list" and "K-th smallest in a sorted matrix".

---

## 4. The running median

Two heaps split the data at the median: a **max-heap `lo`** holds the smaller half and a **min-heap `hi`** the larger half, so the median sits at one or both tops. Keep their sizes equal, or `lo` one larger.

```cpp
// Running median with two heaps: `lo` is a max-heap of the smaller half, `hi` a min-heap of the
//    larger half. Invariant: lo.size() == hi.size() or lo.size() == hi.size() + 1.
class RunningMedian {
    std::priority_queue<int> lo_;
    std::priority_queue<int, std::vector<int>, std::greater<int>> hi_;
public:
    void add(int x) {
        if (lo_.empty() || x <= lo_.top()) lo_.push(x); else hi_.push(x);
        if (lo_.size() > hi_.size() + 1) { hi_.push(lo_.top()); lo_.pop(); }
        else if (hi_.size() > lo_.size()) { lo_.push(hi_.top()); hi_.pop(); }
    }
    std::size_t size() const { return lo_.size() + hi_.size(); }
    double median() const { return lo_.size() > hi_.size() ? lo_.top() : (lo_.top() + (double)hi_.top()) / 2.0; }
};
```

Insert into the proper half, then rebalance by moving one top across if the sizes differ by more than allowed. `add` is `O(log n)` and `median` is `O(1)`. Trace (program output) for the stream `5 15 1 3 8 7 9 10`: the medians after each insertion are `5, 10, 5, 4, 5, 6, 7, 7.5`. The tests compare the median with a sorted vector after every one of the insertions of 200 random streams, with duplicates and negative values.

The limit of this structure is **deletion**: two heaps cannot remove an arbitrary element. A sliding-window median needs deletions, and the usual answers are lazy deletion with a count of the pending removals in each heap, or an ordered multiset with an iterator that follows the median (Topic 09's `std::multiset`).

---

## 5. Scheduling

The fewest rooms for a set of meetings, each `[start, end)`: sort by start time and keep a min-heap of the end times of the rooms in use. A new meeting reuses the room that frees up first if that room is already free (`end <= start`), and otherwise opens a room. The answer is the size of the heap at the end.

```cpp
// Scheduling: the fewest rooms for a set of [start, end) meetings, each with start < end. Sort by start; a min-heap
//    holds the end times of the rooms in use; reuse the room that frees up first if it is free.
inline int min_rooms(std::vector<std::pair<int, int>> meetings) {
    std::sort(meetings.begin(), meetings.end());
    std::priority_queue<int, std::vector<int>, std::greater<int>> ends;
    for (auto& m : meetings) {
        if (!ends.empty() && ends.top() <= m.first) ends.pop();
        ends.push(m.second);
    }
    return (int)ends.size();
}
```

Trace (program output): `{0,30} {5,10} {15,20}` needs **2** rooms (the 5 to 10 meeting overlaps the first; the 15 to 20 one reuses the room that freed at 10), and `{1,2} {2,3}` needs **1** (the comparison is `<=`, so a meeting may start the moment another ends). The same pattern, "a heap of the earliest finishing resource", gives the minimum number of platforms, the minimum number of CPUs for a set of jobs, and (with the event time as the key) a **discrete-event simulation**: pop the earliest event, handle it, push the events it causes.

---

## 6. Indexed heaps and decrease-key

The heap of Topic 15 cannot change the priority of an element that is already inside, because it does not know where the element is. Dijkstra's algorithm and Prim's need exactly that: when a shorter path to a vertex is found, its key in the heap must go down. There are two answers.

**Lazy deletion.** Leave the old entry and push a new one with the smaller key; when an outdated entry surfaces later, recognize it (its key is larger than the vertex's current distance) and skip it. No new structure is needed.

**An indexed heap.** Keep, next to the heap array, a table `pos_[id]` giving the position of every id. Every move updates the table, so `update(id, key)` finds the element in `O(1)`, changes its key and sifts up (or down, if the key rose) in `O(log n)`.

```cpp
// An indexed min-heap over ids 0..n-1 with keys: it knows where every id sits (pos_), so the key of
// an id already in the heap can be changed in O(log n). This is what Dijkstra and Prim need.
template <class K, int D = 2> class IndexedMinHeap {
    friend struct TestAccess;
    std::vector<int> heap_, pos_;                       // heap_[p] = id at position p; pos_[id] = p, or -1 if absent
    std::vector<K> key_;
    void place(std::size_t p, int id) { heap_[p] = id; pos_[id] = (int)p; }
    void up(std::size_t p) {
        int id = heap_[p]; const K k = key_[id];
        while (p > 0) { std::size_t q = (p - 1) / D; if (!(k < key_[heap_[q]])) break; place(p, heap_[q]); p = q; }
        place(p, id);
    }
    void down(std::size_t p) {
        int id = heap_[p]; const K k = key_[id]; const std::size_t n = heap_.size();
        for (;;) {
            std::size_t first = D * p + 1;
            if (first >= n) break;
            std::size_t best = first, end = std::min(n, first + D);
            for (std::size_t c = first + 1; c < end; ++c) if (key_[heap_[c]] < key_[heap_[best]]) best = c;
            if (!(key_[heap_[best]] < k)) break;
            place(p, heap_[best]); p = best;
        }
        place(p, id);
    }
public:
    explicit IndexedMinHeap(int n) : pos_(n, -1), key_(n) {}
    bool empty() const { return heap_.empty(); }
    std::size_t size() const { return heap_.size(); }
    bool contains(int id) const { return pos_[id] >= 0; }
    K key(int id) const { return key_[id]; }
    int top() const { return heap_[0]; }
    void push(int id, K k) { key_[id] = k; heap_.push_back(id); up(heap_.size() - 1); }
    void update(int id, K k) { key_[id] = k; up(pos_[id]); down(pos_[id]); }     // lower or raise
    int pop() {
        int id = heap_[0]; pos_[id] = -1;
        int last = heap_.back(); heap_.pop_back();
        if (!heap_.empty()) { place(0, last); down(0); }
        return id;
    }
    bool valid() const {
        for (std::size_t p = 0; p < heap_.size(); ++p) {
            if (pos_[heap_[p]] != (int)p) return false;
            if (p > 0 && key_[heap_[p]] < key_[heap_[(p - 1) / D]]) return false;
        }
        return true;
    }
};
```

Trace (program output): push A with key 5, B 3, C 8, D 6: the top is B (key 3). Update C to key 1: the top becomes C, and the pop order is `C B A D`.

Both versions of Dijkstra, and a third with a pairing heap (section 8), follow:

```cpp
// Shortest paths from src on a graph in compressed-row form (edges of u are off[u]..off[u+1]-1).
struct Graph { std::vector<int> off, to, w; int n() const { return (int)off.size() - 1; } };
struct Stats { long pushes = 0, pops = 0, stale = 0, updates = 0, improved = 0, scans = 0; };    // improved: times a distance got smaller; scans: edges examined
constexpr long long INF = std::numeric_limits<long long>::max();

// Lazy deletion: push a new entry on every improvement and skip the outdated ones when they surface.
inline std::vector<long long> dijkstra_lazy(const Graph& g, int src, Stats& st) {
    std::vector<long long> dist(g.n(), INF); dist[src] = 0;
    using E = std::pair<long long, int>;
    std::priority_queue<E, std::vector<E>, std::greater<E>> q; q.push({0, src}); ++st.pushes;
    while (!q.empty()) {
        auto [d, u] = q.top(); q.pop(); ++st.pops;
        if (d > dist[u]) { ++st.stale; continue; }
        for (int e = g.off[u]; e < g.off[u + 1]; ++e) {
            ++st.scans;
            long long nd = d + g.w[e];
            if (nd < dist[g.to[e]]) { dist[g.to[e]] = nd; q.push({nd, g.to[e]}); ++st.pushes; ++st.improved; }
        }
    }
    return dist;
}
// Indexed heap: one entry per vertex, and an improvement changes its key in place.
template <int D = 2> std::vector<long long> dijkstra_indexed(const Graph& g, int src, Stats& st) {
    std::vector<long long> dist(g.n(), INF); dist[src] = 0;
    IndexedMinHeap<long long, D> h(g.n()); h.push(src, 0); ++st.pushes;
    while (!h.empty()) {
        int u = h.pop(); ++st.pops;
        for (int e = g.off[u]; e < g.off[u + 1]; ++e) {
            ++st.scans;
            int v = g.to[e]; long long nd = dist[u] + g.w[e];
            if (nd >= dist[v]) continue;
            dist[v] = nd; ++st.improved;
            if (h.contains(v)) { h.update(v, nd); ++st.updates; } else { h.push(v, nd); ++st.pushes; }
        }
    }
    return dist;
}
// Pairing heap with decrease-key through node handles.
inline std::vector<long long> dijkstra_pairing(const Graph& g, int src, Stats& st) {
    std::vector<long long> dist(g.n(), INF); dist[src] = 0;
    PairingHeap<long long> h; std::vector<PairingHeap<long long>::N*> hd(g.n(), nullptr);
    hd[src] = h.push(0, src); ++st.pushes;
    while (!h.empty()) {
        int u = h.pop().second; ++st.pops; hd[u] = nullptr;
        for (int e = g.off[u]; e < g.off[u + 1]; ++e) {
            ++st.scans;
            int v = g.to[e]; long long nd = dist[u] + g.w[e];
            if (nd >= dist[v]) continue;
            dist[v] = nd; ++st.improved;
            if (hd[v]) { h.decrease_key(hd[v], nd); ++st.updates; } else { hd[v] = h.push(nd, v); ++st.pushes; }
        }
    }
    return dist;
}
```

A trace on a graph of four vertices with edges `0->1 (4)`, `0->2 (1)`, `2->1 (2)`, `1->3 (1)`, `2->3 (5)` (program output): the distances are `0 3 1 4`. The lazy version makes **6 pushes**, 6 pops and **2 stale pops** (vertex 1 is first reached with 4 and then improved to 3; vertex 3 is first reached with 6 and improved to 4). The indexed version makes 4 pushes (one per vertex), **2 updates** and 4 pops.

Measured on random graphs with weights 1 to 1000 (source 0; pushes, pops and so on are counted by the code; all variants return the same distances):

| Graph | reachable | lazy pushes | lazy stale pops | indexed pushes | indexed updates |
|---|---|---|---|---|---|
| 200,000 vertices, 1,000,000 edges (degree 5) | 198,605 | 288,560 | 89,955 | 198,605 | 89,933 |
| 200,000 vertices, 4,000,000 edges (degree 20) | 200,000 | 523,690 | 323,690 | 200,000 | 323,706 |
| 20,000 vertices, 2,000,000 edges (degree 100) | 20,000 | 83,151 | 63,151 | 20,000 | 63,115 |

The lazy version makes 1.45, 2.6 and 4.2 pushes per vertex in the three graphs, and its heap can grow to `O(E)` entries in the worst case instead of the `O(V)` of the indexed one. (The update counts of the indexed version differ slightly from the stale counts of the lazy one because ties are broken in a different order.) The times, in milliseconds for one full run (best of 3; a second run in brackets):

| Graph | lazy | indexed binary | indexed 4-ary | indexed 8-ary | pairing |
|---|---|---|---|---|---|
| degree 5 | 79.3 (82.6) | 75.9 (81.9) | 81.6 (83.0) | 83.4 (87.3) | 143.4 (161.2) |
| degree 20 | 187.3 (184.2) | 158.8 (161.3) | 140.4 (144.4) | 133.1 (129.8) | 260.8 (279.4) |
| degree 100 | 21.2 (22.5) | 14.5 (15.4) | 13.2 (12.5) | 12.5 (13.0) | 17.0 (18.7) |

On a sparse graph the lazy heap and the indexed binary heap are the same speed, since few entries go stale, so the simplest code wins. As the graph gets denser, updates dominate, and the indexed heap pulls ahead (about 15% at degree 20 and 30% at degree 100 over the lazy one), most of all with a wider `D`: an update is a sift-up, whose cost falls with the number of levels, while the extra comparisons of a wide sift-down are paid only once per vertex. Prim's algorithm has the same shape with the key "lightest edge from the tree to this vertex".

---

## 7. d-ary heaps

A `D`-ary heap gives every node `D` children: `children(i) = D i + 1 ... D i + D`, `parent(i) = (i - 1) / D`. The tree has `log_D n` levels instead of `log2 n`, so sift-up is shorter, while sift-down inspects `D` children per level.

```cpp
// A d-ary MIN-heap: node i has children D*i+1 ... D*i+D. Cmp(a, b) is true when a belongs ABOVE b
// (note: the opposite convention to Topic 15, where true meant "below").
// More children per node means a shallower tree (log_D n levels) but D-1 comparisons per level in
// sift-down; the children of a node are adjacent in memory, so they share cache lines.
template <class T, int D, class Cmp = std::less<T>> class DaryHeap {
    friend struct TestAccess;                           // lets the tests damage the heap on purpose
    std::vector<T> a_; Cmp c_;
public:
    explicit DaryHeap(Cmp c = Cmp()) : c_(c) {}
    bool empty() const { return a_.empty(); }
    std::size_t size() const { return a_.size(); }
    const T& top() const { return a_[0]; }
    void push(T v) {
        a_.push_back(T());
        std::size_t i = a_.size() - 1;
        while (i > 0) { std::size_t p = (i - 1) / D; if (!c_(v, a_[p])) break; a_[i] = std::move(a_[p]); i = p; }
        a_[i] = std::move(v);
    }
    T pop() {
        T r = std::move(a_[0]);
        T v = std::move(a_.back()); a_.pop_back();
        const std::size_t n = a_.size();
        if (n > 0) {
            std::size_t i = 0;
            for (;;) {
                std::size_t first = D * i + 1;
                if (first >= n) break;
                std::size_t best = first, end = std::min(n, first + D);
                for (std::size_t k = first + 1; k < end; ++k) if (c_(a_[k], a_[best])) best = k;
                if (!c_(a_[best], v)) break;
                a_[i] = std::move(a_[best]); i = best;
            }
            a_[i] = std::move(v);
        }
        return r;
    }
    bool valid() const { for (std::size_t i = 1; i < a_.size(); ++i) if (c_(a_[i], a_[(i - 1) / D])) return false; return true; }
};
```

(Note the comparator convention of this class: `Cmp(a, b)` true means `a` belongs **above** `b`, the opposite of Topic 15's, because here a min-heap is the default.)

Comparisons per pop when draining a random heap of 1,000,000 elements (program output; `D = 2` is the textbook sift-down of Topic 15, whose 34.91 it reproduces):

| `D` | 2 | 3 | 4 | 8 | 16 |
|---|---|---|---|---|---|
| comparisons per pop | 34.91 | 33.83 | 36.23 | 49.77 | 75.68 |
| levels `log_D n` | 19.93 | 12.58 | 9.97 | 6.64 | 4.98 |

So a wide node costs more comparisons per pop (`D - 1` to find the best child plus one against the moved value, per level); the benefit must come from somewhere else. The candidate is memory: the `D` children of a node are adjacent in the array, and with 4-byte ints a 4-ary node's children fit in 16 bytes, within one cache line, so each level costs one cache miss instead of one per child; the levels are also fewer. (This is the reasoning; I did not read hardware counters.) Time per pop plus push in the steady state, from a heap of `2^m` ints built by pushes (best of 3; each cell is the first run / the second run):

| Heap size | binary | 4-ary | 8-ary | 16-ary | `std::priority_queue` |
|---|---|---|---|---|---|
| 2^10 | 22.6 / 23.0 | 19.5 / 20.0 | 32.5 / 32.7 | 39.3 / 38.9 | 17.9 / 17.9 |
| 2^18 | 72.7 / 74.0 | 66.2 / 64.8 | 91.4 / 96.4 | 106.2 / 110.9 | 63.1 / 62.3 |
| 2^22 | 261.9 / 265.6 | 243.6 / 259.1 | 291.7 / 276.0 | 315.9 / 326.1 | 227.6 / 227.1 |

The 4-ary heap beats the binary one by 2 to 14% at every size here, and 8-ary and 16-ary lose. For `int` keys the gain is modest, and **the library's binary heap, which uses Floyd's sift-down from Topic 15 (half the comparisons), beats all of them.** A 4-ary heap would also use Floyd's trick; I did not combine them. The indexed heap in Dijkstra gains more from wider nodes (table above) because its workload is update-heavy.

---

## 8. Binomial, Fibonacci and pairing heaps

The binary heap does `decrease-key` and `delete-min` in `O(log n)`. Several structures improve some operations in theory. The amortized bounds below are from memory (CLRS and the papers cited at the end), not checked here:

| | insert | find-min | delete-min | decrease-key | meld |
|---|---|---|---|---|---|
| binary heap | `O(log n)` | `O(1)` | `O(log n)` | `O(log n)` | `O(n)` |
| binomial heap | `O(1)` amortized | `O(log n)` | `O(log n)` | `O(log n)` | `O(log n)` |
| Fibonacci heap | `O(1)` | `O(1)` | `O(log n)` amortized | `O(1)` amortized | `O(1)` |
| pairing heap | `O(1)` | `O(1)` | `O(log n)` amortized | between `Ω(log log n)` and `O(log n)`, exact bound open | `O(1)` |

The prize is Dijkstra in `O(E + V log V)` with a Fibonacci heap instead of `O(E log V)`: `decrease-key` is called up to `E` times. In practice the constants (several pointers per node, a mark bit, cascading cuts, a root list consolidation) and the pointer chasing make Fibonacci heaps slower than a binary heap on real graphs. I did **not** implement a Fibonacci or binomial heap here; that statement is the folklore, supported by the literature I remember (Larkin, Sen and Tarjan's "back-to-basics" study found implicit heaps and pairing heaps ahead; from memory), and by the pairing-heap measurement below.

The **pairing heap** is the simple member of the family: a heap-ordered tree with any number of children per node, stored as a first-child / next-sibling list, with a `prev` pointer that is the parent for a first child and the left sibling otherwise (that is what makes a cut `O(1)`).

```cpp
// A pairing heap: a heap-ordered tree with any number of children per node, kept as a first-child /
// next-sibling list. Insert and meld are O(1); delete-min melds the root's children in two passes.
template <class K> class PairingHeap {
public:
    struct N { K key; int id; N *child = nullptr, *sib = nullptr, *prev = nullptr; };   // prev: parent if first child, else left sibling
private:
    friend struct TestAccess;
    N* root_ = nullptr; std::size_t n_ = 0; std::vector<N*> scratch_;
    static N* meld(N* a, N* b) {                         // both are roots (prev == sib == nullptr)
        if (!a) return b;
        if (!b) return a;
        if (b->key < a->key) std::swap(a, b);            // a keeps the smaller key
        b->sib = a->child; if (a->child) a->child->prev = b;
        b->prev = a; a->child = b;
        return a;
    }
public:
    PairingHeap() = default;
    PairingHeap(const PairingHeap&) = delete; PairingHeap& operator=(const PairingHeap&) = delete;
    ~PairingHeap() {
        std::vector<N*> st; if (root_) st.push_back(root_);
        while (!st.empty()) { N* t = st.back(); st.pop_back(); if (t->child) st.push_back(t->child); if (t->sib) st.push_back(t->sib); delete t; }
    }
    bool empty() const { return !root_; }
    std::size_t size() const { return n_; }
    const N* top() const { return root_; }
    N* push(K k, int id) { N* x = new N{k, id}; root_ = meld(root_, x); ++n_; return x; }
    std::pair<K, int> pop() {
        N* r = root_; std::pair<K, int> out{r->key, r->id};
        scratch_.clear();
        for (N* c = r->child; c;) { N* nx = c->sib; c->prev = c->sib = nullptr; scratch_.push_back(c); c = nx; }
        delete r; --n_;
        std::size_t m = 0;
        for (std::size_t i = 0; i + 1 < scratch_.size(); i += 2) scratch_[m++] = meld(scratch_[i], scratch_[i + 1]);   // first pass: pairs
        if (scratch_.size() % 2) scratch_[m++] = scratch_.back();
        N* res = nullptr;
        for (std::size_t i = m; i-- > 0;) res = meld(res, scratch_[i]);                                            // second pass: right to left
        root_ = res;
        return out;
    }
    void decrease_key(N* x, K k) {                       // cut x out of its place and meld it with the root
        x->key = k;
        if (x == root_) return;
        if (x->prev->child == x) x->prev->child = x->sib; else x->prev->sib = x->sib;
        if (x->sib) x->sib->prev = x->prev;
        x->sib = x->prev = nullptr;
        root_ = meld(root_, x);
    }
    bool valid() const {
        if (!root_) return n_ == 0;
        if (root_->prev || root_->sib) return false;
        std::size_t cnt = 0; std::vector<const N*> st{root_};
        while (!st.empty()) {
            const N* t = st.back(); st.pop_back(); ++cnt;
            const N* left = t;                            // walk the children: first child's prev is t, the others' prev is the sibling to their left
            for (const N* c = t->child; c; c = c->sib) {
                if (c->prev != left) return false;
                if (c->key < t->key) return false;
                st.push_back(c); left = c;
            }
        }
        return cnt == n_;
    }
};
```

- **meld** compares two roots and hangs the larger under the smaller: `O(1)`. **push** is a meld with a one-node heap.
- **pop** removes the root and melds its children in **two passes**: left to right in pairs, then right to left on the results. This is where the `O(log n)` amortized cost comes from.
- **decrease_key** cuts the node out of its sibling list and melds it with the root: `O(1)` plus the meld.
- The destructor and `valid()` use an explicit stack, because a heap built from descending keys is a path (the test builds one 200,000 deep).

A trace (program output): pushing 5, 3, 8, 1, 9, 2 gives the tree `(1 (2) (9) (3 (8) (5)))` in the notation `(key child child ...)`, so the root 1 has the children 2, 9 and 3 and the node 3 has children 8 and 5. After one pop, the children 2, 9, 3 are paired (2 beats 9; 3 stays alone), then melded from the right: `(2 (3 (8) (5)) (9))`.

**The measurement.** In Dijkstra the pairing heap was the slowest variant: 143.4 ms against 75.9 ms for the indexed binary heap on the sparse graph, 260.8 against 158.8 at degree 20, and 17.0 against 14.5 at degree 100 (second run: 161.2, 279.4, 18.7). In the steady-state test it was the fastest at 2^10 elements (15.4 ns against 22.6 and 17.9 for the library) and then fell behind: 294.2 ns at 2^18 against 72.7 and 63.1, and **2,678 ns at 2^22 against 261.9 and 227.6** (second run: 2,731). The node is 32 bytes (40 with a 64-bit key), allocated individually, and a pop touches the children of the root, which are scattered over the heap; once the heap does not fit in cache every step is a cache miss. The `O(1)` insert and meld that make the structure attractive are not what these workloads need.

Where a pairing heap, or a mergeable heap in general, is the right tool: when you need `meld` of two whole queues, as in some mergeable-priority-queue problems (for instance merging the task queues of two schedulers), which no array heap does in better than `O(n)`.

---

## 9. Choosing

| Need | Use |
|---|---|
| top-K of a stream, or the K-th largest | bounded heap of size K (`std::priority_queue`), or `nth_element` if all the data is in memory and you only need the set |
| merge sorted streams | heap of heads |
| median of a growing set | two heaps |
| median with deletions (sliding window) | two heaps with lazy deletion, or a `std::multiset` with a median iterator |
| earliest-finishing resource, event simulation | min-heap of times |
| Dijkstra or Prim, sparse graph | lazy `std::priority_queue`: simplest, and as fast as the alternatives |
| Dijkstra or Prim, dense or update-heavy | indexed heap, 4-ary or 8-ary |
| meld two whole priority queues | pairing heap (or a leftist / skew heap, not covered here) |
| a theory exam | know the Fibonacci heap's bounds; in a program, use the binary heap |

---

## 10. Common mistakes

1. **Forgetting the stale check** in lazy Dijkstra (`if (d > dist[u]) continue;`). Without it every outdated entry is expanded, which can turn a near-linear run into a quadratic one, and with it removed the answer is still right, so the bug hides.
2. **A max-heap for "K largest".** The heap of size K must be a *min*-heap, so that the weakest of the K is the one at the top.
3. **`K = 0` or `K > n`.** Guard the `top()` of an empty heap; the K-largest of fewer than K elements is all of them.
4. **Rebalancing the two median heaps incorrectly.** After each insert, move at most one element, and enforce `lo.size() == hi.size()` or `lo.size() == hi.size() + 1`. The median formula depends on that exact invariant.
5. **Meeting rooms with `<` instead of `<=`.** Whether a meeting may start when another ends is part of the problem statement; the code above treats `[start, end)` as half-open.
6. **Calling `update` on an id that is not in the heap** (use `contains` first), or `push` on one that is. The position table holds -1 for absent ids and a stale position for present ones.
7. **Integer overflow in distances.** Sums of `int` weights overflow; use 64-bit distances and never add to `INF`.
8. **Negative weights.** Dijkstra's argument breaks, and both versions can return wrong answers or loop. (Zero weights are fine.)
9. **Using `decrease_key` to raise a key** in the pairing heap: the cut-and-meld logic assumes the new key is not larger.
10. **Comparators with the wrong direction** in `d`-ary or pairing heaps written by hand: this series' `DaryHeap` takes "above" comparators (true means the first goes on top), while `std::priority_queue` takes "below" comparators. Mixing the two conventions is the classic way to get a max-heap when you wanted a min-heap.

**The four mutants that survive**, each checked by hand: (1) `x < lo.top()` instead of `x <= lo.top()` in the median sends an element equal to the boundary to the other heap, which changes no median; (2) `k <= key` instead of `k < key` in the indexed heap's sift-up moves an element past an equal parent, which keeps the heap valid; (3) deleting the `if (!b) return a;` line of `meld`: no caller ever passes a null second argument, so the line is defensive; (4) not clearing the handle `hd[u]` of a popped vertex in the pairing Dijkstra: a popped vertex never improves again when weights are non-negative, so the stale handle is never used (negative weights are outside the algorithm anyway).

---

## 11. Questions to answer out loud

1. Why is a *min*-heap the right tool for the K *largest* values? What is the expected number of replacements in a random stream?
2. How many heap operations does a k-way merge of `N` elements make, and what is the size of the heap?
3. State the invariant of the two-heap median and how `add` keeps it. Why can it not handle deletions?
4. How does the min-rooms algorithm use the heap? Why `<=` in the comparison?
5. What does an indexed heap store besides the heap array, and what does `update` cost?
6. How many entries can the lazy Dijkstra heap hold, and what makes it hold more? When is it as fast as the indexed version?
7. What does a `D`-ary heap trade? What did the comparison counts and the timings show?
8. State the amortized bounds of the binary, Fibonacci and pairing heaps. Why does the Fibonacci heap lose in practice?
9. How does `delete-min` work in a pairing heap, and what does `prev` store?
10. When would you choose a pairing heap?

---

## 12. Problems

**A. Top-K.** Run `top_k_largest` on `3 1 4 1 5 9 2 6 5 3 5` with K = 3. How many replacements, and what is the result? For a random stream of 1,000,000 values and K = 100, how many replacements do you expect?

**B. Median.** Give the running median after each element of `5 15 1 3 8 7 9 10`, and the contents of the two heaps after the fifth.

**C. Merge.** Merge `1 4 7`, `2 5`, `3 6 8`. How many pops and pushes in total? What is the heap size at most?

**D. Rooms.** The fewest rooms for `{0,30} {5,10} {15,20}`, and for `{1,2} {2,3} {3,4}`. What changes if a meeting may not start when another ends?

**E. Indexed.** Push A:5, B:3, C:8, D:6 into an indexed min-heap, update C to 1, then pop everything. In what order do they come out? What would happen if the code pushed C again instead of updating it?

**F. Dijkstra.** On the graph `0->1 (4)`, `0->2 (1)`, `2->1 (2)`, `1->3 (1)`, `2->3 (5)` give the distances from 0, the number of pushes and stale pops of the lazy version, and the number of updates of the indexed version.

**G. Pairing.** Build a pairing heap by pushing 5, 3, 8, 1, 9, 2 and draw it. Pop once and draw the result.

**H. Choose.** Pick a structure for each: (a) the 10 closest points to the origin out of 50 million, one pass; (b) merging 1,000 sorted log files of 1 GB each; (c) a route planner on a road graph with 10 million vertices and 25 million edges; (d) a leaderboard that must show the median score and let players leave.

---

## Answers

**A.** The heap starts as 3, 1, 4. The next 1 does not beat the weakest (1); 5 replaces 1 (replacement 1); 9 replaces 3 (replacement 2); 2 does not beat 4; 6 replaces 4 (replacement 3); the later 5, 3, 5 do not beat 5. The result is `9 6 5` with **3 replacements** (program output). For a random stream of 1,000,000 values and K = 100, expect `K (H_n - H_K) = 920.5`; the measured run gave 960.

**B.** `5, 10, 5, 4, 5, 6, 7, 7.5` (program output). After the fifth element (`5 15 1 3 8`) the sorted data is `1 3 5 8 15`: `lo` holds the smaller half plus the median, `{1, 3, 5}`, as a max-heap with 5 on top, and `hi` holds `{8, 15}` as a min-heap with 8 on top; the median is 5.

**C.** `1 2 3 4 5 6 7 8`. Total pops: 8. Pushes: 3 initial heads plus 5 successors, 8 in all (the last element of each list has no successor). The heap never holds more than `k = 3` entries.

**D.** 2 rooms and 1 room (program output for the first; the second is the half-open reading: each meeting starts when the previous ends). If a meeting may not start when another ends, change `<=` to `<`: `{1,2} {2,3} {3,4}` then needs 2 rooms, since the room that frees at 2 is not yet reusable at 2, while the second meeting can use a new room and the third reuses the first.

**E.** `C B A D` (program output): C has the smallest key (1), then B (3), A (5), D (6). If the code pushed C again, the heap would hold two entries for C and the table would describe only one of the two entries, and `valid()` would fail. This is why the lazy version needs a stale check and the indexed one needs `contains`.

**F.** The distances are `0 3 1 4`. The lazy version makes 6 pushes and 6 pops, **2 of them stale**; the indexed version makes 4 pushes and **2 updates** (vertex 1: 4 improved to 3; vertex 3: 6 improved to 4). (Program output.)

**G.** After the pushes: `(1 (2) (9) (3 (8) (5)))`. Popping the 1 leaves its children 2, 9, 3: the first pass melds the pair (2, 9) into `(2 (9))` and leaves 3 alone; the second pass melds from the right, 3 with `(2 (9))`, and the 2 wins: `(2 (3 (8) (5)) (9))`. (Program output.)

**H.** (a) A bounded heap of size 10, a max-heap of the *distances* of the 10 closest so far, so that the farthest of the ten is at the top (here the K smallest, the mirror of section 2): `O(n log 10)` and no storage of the 50 million points. (b) A 1,000-way merge with a heap of 1,000 heads, reading each file sequentially: `O(N log 1000)`. (c) Dijkstra, or better A* with a good heuristic, with a lazy `std::priority_queue` (sparse graph: degree 2.5) or an indexed 4-ary heap; the measurements say they are equal at that density, so choose the lazy one for simplicity. (d) The median with deletions: two heaps with lazy deletion, or a `std::multiset` with an iterator on the median, or an order-statistic tree (Topic 30 of the plan).

---

## Cheat sheet

- **Top-K largest:** min-heap of size K; replace the top when a newcomer beats it; `O(n log K)`; expected replacements `K (H_n - H_K)` (960 against 920.5 for n = 1M, K = 100).
- **K-way merge:** heap of the current heads; `O(N log k)`.
- **Running median:** max-heap `lo` (smaller half, size equal or one larger) and min-heap `hi`; `O(log n)` add, `O(1)` median; no deletion.
- **Rooms / resources:** sort by start, min-heap of end times; reuse the top if `end <= start`.
- **Indexed heap:** a position table makes `update(id, key)` `O(log n)`; Dijkstra and Prim; equal to lazy deletion on sparse graphs, 15 to 30% faster on dense ones, and `O(V)` memory instead of `O(E)`.
- **d-ary:** fewer levels, more comparisons per level (34.91, 33.83, 36.23, 49.77, 75.68 per pop for D = 2, 3, 4, 8, 16); 4-ary was 2 to 14% faster than binary; the library's binary heap with Floyd's sift beat all of them.
- **Pairing / Fibonacci / binomial:** better bounds in theory (`O(1)` insert, meld, and amortized decrease-key for the Fibonacci heap); slower than a binary heap in every measurement here (pairing: 2,678 ns against 262 ns per operation at 2^22).
- Use a plain heap for "the best of a changing set"; reach for the others only when a measurement says so.

---

## Sources

- CLRS, *Introduction to Algorithms*: chapter 6 (heaps), chapter 19 in older editions (Fibonacci heaps), chapter 22 (single-source shortest paths) for the amortized bounds quoted in section 8 (from memory).
- Fredman, Sedgewick, Sleator and Tarjan, "The pairing heap: a new form of self-adjusting heap" (1986); Pettie, "Towards a final analysis of pairing heaps" (2005), for the decrease-key bounds (from memory, including the `Ω(log log n)` lower bound of Fredman).
- Larkin, Sen and Tarjan, "A back-to-basics empirical study of priority queues" (2014) for the observation that simple heaps beat Fibonacci heaps in practice (from memory; I did not re-read it).
- Topics 15 and 14 of this series for the binary heap, Floyd's sift-down and the amortized vocabulary; Topic 09 for `std::multiset` as an alternative to two heaps.
- Not implemented here, stated from memory: binomial and Fibonacci heaps, leftist and skew heaps.
- All other numbers in this tutorial come from the tests and measurements described at the top.
