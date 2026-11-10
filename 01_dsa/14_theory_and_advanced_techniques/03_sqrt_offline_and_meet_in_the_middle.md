# DSA 14.03: Square-Root Techniques, Offline Queries and Meet in the Middle

*Part of the [Theory and advanced techniques plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: prefix sums and binary search from [Arrays and strings](../01_arrays_and_strings/00_plan.md) and [Binary search and sorting](../03_binary_search_and_sorting/00_plan.md), BFS from [Graphs](../08_graphs/00_plan.md), and the subset DP from [P, NP and what to do with a hard problem](01_p_np_and_hard_problems.md)*

Three ideas that share one shape: **do not pay the full cost where a split costs far less.** Square-root techniques split the work into about `sqrt(n)` pieces of about `sqrt(n)` items each, so neither a long scan nor a long list of pieces is paid. Offline techniques notice that if all queries are known in advance, they can be reordered so that consecutive queries share almost all of their work. Meet in the middle splits an exponential search into two halves whose results are combined, turning `2^n` into `2^(n/2)`. They are stretch material: you will not meet them in every interview, but when the limits are `n = 10^5` with awkward queries, or `n = 40` with huge numbers, they are the intended answer.

By the end you can:

- split an array into blocks to support range add, range sum and "how many elements are at least `x`" in `O(sqrt(n))`, choose the block size by measuring, and say when a logarithmic structure is better;
- count triangles in `O(m sqrt(m))` with the heavy and light split on degrees, and show why a single hub vertex breaks the plain method (measured);
- write Mo's algorithm for offline range queries, derive its `O(n sqrt(q))` pointer moves, and measure the effect of block size and of the odd-even ordering;
- use coordinate compression and a sweep with a Fenwick tree to answer the same queries in `O((n + q) log n)`, and say which of the two to choose;
- solve subset sum counting and "closest sum" for `n` around 40 by meeting in the middle, and compare with trying all subsets;
- write bidirectional BFS and say when it helps a lot (branching graphs) and when it barely helps (grids).

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 4,834,041 checks passed under AddressSanitizer and UBSan. Every structure is compared with a plain oracle: a vector with loops for the blocks, a triple loop over an adjacency matrix for triangles, a `std::set` per query for distinct counts, trying all subsets for meet in the middle, one-sided BFS for the bidirectional search. The bound on Mo's pointer moves and the bound on out-degrees in the triangle count are tested too. I tried 22 deliberate bugs. Twenty-one were caught (one only by a time limit, because the loop never ended). One escaped and I did not count it: running Mo's shrink loops before the grow loops gives the right answer for a counting query, because the counts may dip below zero and come back; the order matters for queries that store more than counts, so keep the grow-first order.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. All data is random from a seeded generator, so every run gives the same counts. Treat the ratios as the shape of the effect, not as a prediction for your data. Statements about contest folklore, named variants and LeetCode are from memory and marked.

---

## 1. The square-root idea

Suppose one operation can be done in `O(n)` by a scan, or in `O(1)` if you keep a table that costs `O(n)` to update. Neither extreme is good for a mixed workload. Split the data into `n / B` blocks of size `B`. Keep a summary per block. An operation then scans at most two partial blocks (`O(B)`) and combines at most `n / B` whole-block summaries (`O(n / B)`). The cost `B + n / B` is smallest at `B = sqrt(n)`, giving `O(sqrt(n))`. For `n = 10^5` that is about 316 steps instead of 100,000.

When does this beat the alternatives? A Fenwick tree or a segment tree gives `O(log n)` per operation, which is better when the operation fits them. Square-root decomposition earns its place when the operation does **not** fit: the summary is awkward to merge (a sorted copy of each block), the update is a range update on a query that needs sorted data, or you need working code in 15 minutes. It is also the idea behind two later techniques in this tutorial.

## 2. Block decomposition: range add, range sum, count at least `x`

The structure holds the array in blocks. For each block it keeps the sum of its elements, a **lazy** value added to every element of the block but not yet written into the array, and a sorted copy of the elements (without the lazy part). A range add writes into the two partial blocks and rebuilds them, and adds to the lazy value of every whole block in between. A range sum scans the partial blocks and adds the stored block sums. A count of elements at least `x` scans the partial blocks and, for each whole block, does a binary search for `x - lazy` in the sorted copy.

```cpp
// Square-root decomposition: cut the array into blocks of size B. A range touches at most two partial blocks
// (scanned element by element) and many whole blocks (handled with one stored value each): O(B + n / B) per operation.
// Supports: add v to a range, sum of a range, and the number of elements >= x in a range.
struct SqrtArray {
    int n, B, nb;
    vector<long long> a, blk_sum, lazy;            // lazy[b] is added to every element of block b (not yet pushed into a)
    vector<vector<long long>> sorted;              // each block's elements (without the lazy part), sorted
    SqrtArray(const vector<long long>& init, int block) : n((int)init.size()), B(std::max(1, block)), nb((n + B - 1) / B),
        a(init), blk_sum(nb, 0), lazy(nb, 0), sorted(nb) {
        for (int b = 0; b < nb; ++b) rebuild(b);
    }
    int lo(int b) const { return b * B; }
    int hi(int b) const { return std::min(n, (b + 1) * B); }
    void rebuild(int b) {                          // O(B log B): recompute the sum and the sorted copy of block b
        long long s = 0;
        sorted[b].assign(a.begin() + lo(b), a.begin() + hi(b));
        for (int i = lo(b); i < hi(b); ++i) s += a[i];
        std::sort(sorted[b].begin(), sorted[b].end());
        blk_sum[b] = s + lazy[b] * (hi(b) - lo(b));
    }
    void add_partial(int b, int l, int r, long long v) {      // add v to a[l..r) inside block b
        for (int i = l; i < r; ++i) a[i] += v;
        rebuild(b);
    }
    void add(int l, int r, long long v) {          // [l, r)
        if (l >= r) return;
        int bl = l / B, br = (r - 1) / B;
        if (bl == br) { add_partial(bl, l, r, v); return; }
        add_partial(bl, l, hi(bl), v);
        add_partial(br, lo(br), r, v);
        for (int b = bl + 1; b < br; ++b) { lazy[b] += v; blk_sum[b] += v * (hi(b) - lo(b)); }
    }
    long long sum(int l, int r) const {
        if (l >= r) return 0;
        int bl = l / B, br = (r - 1) / B;
        long long s = 0;
        auto part = [&](int b, int x, int y) { for (int i = x; i < y; ++i) s += a[i] + lazy[b]; };
        if (bl == br) { part(bl, l, r); return s; }
        part(bl, l, hi(bl));
        part(br, lo(br), r);
        for (int b = bl + 1; b < br; ++b) s += blk_sum[b];
        return s;
    }
    int count_ge(int l, int r, long long x) const {
        if (l >= r) return 0;
        int bl = l / B, br = (r - 1) / B, c = 0;
        auto part = [&](int b, int p, int q) { for (int i = p; i < q; ++i) c += a[i] + lazy[b] >= x; };
        if (bl == br) { part(bl, l, r); return c; }
        part(bl, l, hi(bl));
        part(br, lo(br), r);
        for (int b = bl + 1; b < br; ++b)          // whole block: binary search for x - lazy in the sorted copy
            c += (int)(sorted[b].end() - std::lower_bound(sorted[b].begin(), sorted[b].end(), x - lazy[b]));
        return c;
    }
};
```

A hand trace on `n = 8`, `B = 3` (blocks `[0,3)`, `[3,6)`, `[6,8)`). `add(1, 7, 5)`: block 0 is partial (positions 1 and 2), block 2 is partial (position 6), so those three elements are written and the two blocks rebuilt; block 1 is whole, so `lazy[1] += 5` and its sum grows by `5 * 3`. A later `sum(2, 7)` scans position 2 (block 0), adds the stored sum of block 1, and scans position 6 (block 2).

The block size is a tuning parameter, and the textbook `sqrt(n)` is not the best value here. Each partial add **rebuilds** a block, which costs `B log B` because of the sort, so a smaller block is better. Measured with `n = 131,072` and 20,000 operations (a third each of range add, range sum and count at least `x`), with every block size giving the same checksum, and the same operations run with plain loops:

```
T1 n=131072, 20000 operations (a third each: add to a range, range sum, count of elements >= x), block sizes 4 to 4096: all give the same checksum yes; the plain loop over the whole array gives the same result
```

| Block size `B` | Time (ms), run 1 / run 2 |
|---|---|
| 4 | 329.9 / 349.8 |
| 16 | 102.7 / 111.7 |
| 64 | 55.0 / 59.1 |
| 256 | 115.8 / 118.4 |
| 1024 | 487.5 / 411.1 |
| 4096 | 2281.2 / 1971.1 |
| plain loops over the array | 341.9 / 260.3 |

The curve is a U: with `B = 4` there are 32,768 blocks to walk, with `B = 4096` every partial block costs a sort of 4,096 elements. The best value here is 64, about a fifth of `sqrt(n) = 362`, and it is about 4 to 6 times faster than plain loops, not hundreds of times, because a plain loop over a contiguous array is very fast (the compiler vectorises it). So: **measure** the block size; start from `sqrt(n)` and try a factor of 4 either side. The gain over plain loops grows with `n`; at `n = 10^6` it would be larger (not measured).

**Square root on the queries.** A different split: keep a list of the last `B` updates, apply them lazily to each query (a scan of the list), and rebuild the whole structure every `B` updates (`O(n)`). A query costs `O(base + B)` and the rebuilds amortise to `O(n / B)` per update. Use it when the static structure answers queries fast but has no update. An editor exercise below does this.

## 3. Square root on degrees: counting triangles

How many triangles does a graph with `m` edges contain? The method that intersects the neighbour lists of the two ends of each edge costs the sum over edges of `deg(u) + deg(v)`. A single vertex joined to everything makes that quadratic. Split the vertices by degree. A **heavy** vertex has degree above `sqrt(2m)`, and there are fewer than `sqrt(2m)` of them. A **light** vertex has degree at most `sqrt(2m)`. The clean way to write the split is to **orient** every edge from the endpoint with the smaller `(degree, id)` to the other. Then:

- a vertex with out-degree `d` has `d` out-neighbours, each of degree at least `d` (they come later in the order), so `2m >= d * d` and `d <= sqrt(2m)`;
- each triangle `u, v, w` (in this order) appears exactly once, as `u -> v`, `u -> w`, `v -> w`; to find them, mark the out-neighbours of `u`, then for each out-neighbour `v` look at the out-neighbours `w` of `v` and count the marked ones;
- the total work is the sum over edges `u -> v` of `out(v) <= sqrt(2m)`, so `O(m sqrt(m))`.

```cpp
// Square root on degrees. Orient every edge from the endpoint with the smaller (degree, id) to the larger.
// Then every vertex has at most sqrt(2m) out-neighbours: a vertex with out-degree d has d neighbours of degree >= d,
// so 2m >= d * d. Each triangle u < v < w (in this order) is found exactly once as u -> v, u -> w, v -> w.
// Cost O(m sqrt(m)), however skewed the degrees are.
struct TriangleStats { long long triangles = 0; int max_degree = 0, max_out = 0; };
inline TriangleStats count_triangles(int n, const vector<pair<int, int>>& edges) {
    vector<int> deg(n, 0);
    for (const auto& e : edges) { ++deg[e.first]; ++deg[e.second]; }
    auto before = [&](int u, int v) { return deg[u] != deg[v] ? deg[u] < deg[v] : u < v; };
    vector<vector<int>> out(n);
    for (const auto& e : edges) { if (before(e.first, e.second)) out[e.first].push_back(e.second); else out[e.second].push_back(e.first); }
    TriangleStats st;
    vector<int> mark(n, -1);
    for (int u = 0; u < n; ++u) {
        st.max_degree = std::max(st.max_degree, deg[u]);
        st.max_out = std::max(st.max_out, (int)out[u].size());
        for (int v : out[u]) mark[v] = u;
        for (int v : out[u]) for (int w : out[v]) if (mark[w] == u) ++st.triangles;
    }
    return st;
}
// The version without the orientation: for each edge, intersect the two full neighbour lists. Cost sum over edges of (deg u + deg v).
inline long long count_triangles_plain(int n, const vector<pair<int, int>>& edges) {
    vector<vector<int>> adj(n);
    for (const auto& e : edges) { adj[e.first].push_back(e.second); adj[e.second].push_back(e.first); }
    for (auto& l : adj) std::sort(l.begin(), l.end());
    long long t = 0;
    for (const auto& e : edges) {
        const auto &x = adj[e.first], &y = adj[e.second];
        size_t i = 0, j = 0;
        while (i < x.size() && j < y.size()) { if (x[i] < y[j]) ++i; else if (x[i] > y[j]) ++j; else { ++t; ++i; ++j; } }
    }
    return t / 3;                                  // every triangle is counted once per edge
}
```

A graph with 20,000 vertices, one hub joined to all of them, and 100,000 other random edges:

```
T2 20000 vertices, 120000 edges (one vertex joined to all others, the rest random): 100179 triangles by orientation, 100179 by intersecting full lists; largest degree 19999, largest out-degree 12, sqrt(2m) = 490
```

| Method | Time (ms), run 1 / run 2 |
|---|---|
| orientation by degree | 6.5 / 5.1 |
| intersect the full neighbour lists | 196.3 / 155.2 |

Both methods give 100,179. The orientation is about 30 times faster here, because the hub's 19,999 neighbours are handled by their small out-lists. The bound `sqrt(2m) = 490` is far from the measured largest out-degree, 12: the bound is a worst case, and real graphs are kinder. The hub itself has out-degree 0, since every edge to it points towards it. This heavy and light idea recurs in graph problems with degree skew (from memory: counting 4-cycles, queries about colours).

## 4. Mo's algorithm: reorder the queries

Given an array and `q` range queries `[l, r]`, you could answer each by a scan (`O(n)`). If you can **add** an element to a window and **remove** one in `O(1)` and read the answer from the window's state, you can slide one window from query to query. A random order makes the window jump around, but a good order makes it move little.

Mo's order: cut `l` into blocks of size `B`. Sort the queries by the block of `l`, then by `r`. Within one block the right end only moves right (at most `n` steps per block, `n / B` blocks), and the left end stays inside one block (at most `B` steps per query, `q` queries). Total moves `O(n * n / B + q * B)`, smallest at `B = n / sqrt(q)`: about `2 n sqrt(q)`. The **odd-even trick** sorts `r` descending in odd blocks, so the right end sweeps back instead of jumping to the start.

The query below counts distinct values in `a[l..r]`. The window state is an array of counts and the current number of distinct values.

```cpp
// Mo's algorithm: answer many range queries offline by sorting them so that a window [L, R] that moves from one query to the
// next moves only a little. Sort by (l / B, r); with B about n / sqrt(q) the window makes O(n sqrt(q)) steps in total.
// The "odd-even" trick sorts r descending in odd blocks, so the right end sweeps back instead of jumping.
// Here the query is: how many distinct values in a[l..r] (inclusive)? Values must lie in [0, V).
struct Query { int l, r, id; };
inline vector<int> mo_distinct(const vector<int>& a, int V, vector<Query> qs, int B, bool odd_even, long long& moves) {
    B = std::max(1, B);
    std::sort(qs.begin(), qs.end(), [&](const Query& x, const Query& y) {
        int bx = x.l / B, by = y.l / B;
        if (bx != by) return bx < by;
        return (odd_even && (bx & 1)) ? x.r > y.r : x.r < y.r;
    });
    vector<int> cnt(V, 0), ans(qs.size(), 0);
    int L = 0, R = -1, distinct = 0;
    moves = 0;
    auto add = [&](int i) { if (cnt[a[i]]++ == 0) ++distinct; ++moves; };
    auto del = [&](int i) { if (--cnt[a[i]] == 0) --distinct; ++moves; };
    for (const Query& q : qs) {
        while (L > q.l) add(--L);                  // grow first, then shrink: the window is never inverted
        while (R < q.r) add(++R);
        while (L < q.l) del(L++);
        while (R > q.r) del(R--);
        ans[q.id] = distinct;
    }
    return ans;
}
```

(The grow loops come first. With counts only, the other order also gives the right answer, but for a query that keeps a set or a maximum of the window, shrinking an empty window breaks it.) `n = q = 100,000` random ranges over 1,000 values; the reference count is `n sqrt(q) = 3.16e7`:

```
T3 Mo's algorithm, n = 100000, q = 100000 random ranges, distinct values (n * sqrt(q) = 3.16e+07):
T3 block size 31, plain order: 303843235 pointer moves
T3 block size 31, odd-even order: 157351663 pointer moves
T3 block size 316, plain order: 41914583 pointer moves
T3 block size 316, odd-even order: 26376381 pointer moves
T3 block size 3162, plain order: 107545823 pointer moves
T3 block size 3162, odd-even order: 106106882 pointer moves
```

With the block size from the analysis (316), the plain order makes 41.9 million moves and the odd-even order 26.4 million, about 1.6 times fewer. A block of 31 is too small (the right end sweeps the array 3,000 times) and a block of 3,162 is too large (the left end wanders). Time:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| Mo, block 316, odd-even | 30.0 / 32.6 |
| Mo, block 31, odd-even | 88.8 / 94.9 |
| a `std::set` per query, **300 queries only** | 451.7 / 404.3 |

The last row took about 1.4 ms per query, so 100,000 queries by this method would take around 140 seconds (extrapolated from the 300). Mo does all of them in about 30 ms.

What Mo needs: all queries known in advance, no updates to the array, and an `O(1)` add and remove. Variants (from memory): Mo with updates, which adds a third sort key and costs about `n^(5/3)`, and Mo on trees.

## 5. Offline processing, compression and a sweep

When the queries are known in advance, sometimes a cleverer reordering removes the square root entirely. For distinct values in a range: sweep `r` from left to right and keep a `1` in a Fenwick tree **only at the latest position of each value**. When the sweep reaches position `i` with value `v`, clear the old position of `v` and set `i`. The answer for a query `[l, r]`, asked when the sweep is at `r`, is the number of `1`s in `[l, r]`: every value present in the range has its latest position there, once.

**Coordinate compression** comes with it: replace values up to `10^9` by ranks `0..k-1`, so that arrays indexed by value fit in memory. (Mo's count array and the `last` array need it too.)

```cpp
// Offline processing: when all queries are known in advance, sort them and sweep. Coordinate compression first,
// so that values up to 10^9 become ranks 0..k-1 (equal values get equal ranks, order is kept).
inline vector<int> compress(const vector<long long>& a) {
    vector<long long> s(a);
    std::sort(s.begin(), s.end());
    s.erase(std::unique(s.begin(), s.end()), s.end());
    vector<int> r(a.size());
    for (size_t i = 0; i < a.size(); ++i) r[i] = (int)(std::lower_bound(s.begin(), s.end(), a[i]) - s.begin());
    return r;
}
struct Fenwick {
    vector<int> t;
    explicit Fenwick(int n) : t(n + 1, 0) {}
    void add(int i, int v) { for (++i; i < (int)t.size(); i += i & -i) t[i] += v; }
    int prefix(int i) const { int s = 0; for (++i; i > 0; i -= i & -i) s += t[i]; return s; }       // sum of positions 0..i
};
// Distinct values in a[l..r]: sweep r from left to right; keep a 1 only at the LATEST position of each value.
// Then the answer for [l, r] is the number of 1s in [l, r]. O((n + q) log n), no sqrt.
inline vector<int> distinct_offline(const vector<int>& a, int V, const vector<Query>& qs) {
    int n = (int)a.size();
    vector<vector<pair<int, int>>> at(n);          // queries ending at each r: (l, id)
    for (const Query& q : qs) at[q.r].push_back({q.l, q.id});
    Fenwick f(n);
    vector<int> last(V, -1), ans(qs.size(), 0);
    for (int i = 0; i < n; ++i) {
        if (last[a[i]] >= 0) f.add(last[a[i]], -1);
        f.add(i, 1);
        last[a[i]] = i;
        for (const auto& p : at[i]) ans[p.second] = f.prefix(i) - (p.first ? f.prefix(p.first - 1) : 0);
    }
    return ans;
}
```

```
T4 the offline sweep with a Fenwick tree gives the same 100000 answers as Mo's algorithm: yes
```

The sweep gives the same 100,000 answers as Mo. Time:

| Work | Time (ms), run 1 / run 2 |
|---|---|
| offline sweep with a Fenwick tree, `n = q = 100,000` | 14.2 / 12.0 |

That is more than twice as fast as Mo (30 ms) and `O((n + q) log n)` instead of `O(n sqrt(q))`. So: **when a sweep with a Fenwick tree exists, use it.** Mo's strength is that it needs only an `O(1)` add and remove, and many "count pairs in a range" or "mode of a range" queries have no sweep. Other offline ideas, from memory: sort queries by a threshold and insert elements in order (count of elements at most `x` in a range with a Fenwick tree), process deletions in reverse as insertions (union-find), and divide and conquer over time.

## 6. Meet in the middle

Subset sum with `n = 40` numbers up to `10^9` and a target: the table method of 14.01 would need `10^10` cells or more, and `2^40` subsets is about `10^12`. Split the numbers into two halves of 20. Each half has `2^20` subset sums, about a million. A subset of the whole is a subset of the left half plus a subset of the right half, so a subset has sum `T` exactly when `x + y = T` for some left sum `x` and right sum `y`. Sort the right sums and, for each left sum, binary search for `T - x`. The cost is `O(2^(n/2) * n)` time and `O(2^(n/2))` memory.

```cpp
// Meet in the middle: split the n items into two halves of n/2. Each half has 2^(n/2) subset sums. A subset of the whole is a
// subset of the left half plus a subset of the right half, so combine the two lists instead of listing 2^n subsets.
// 2^40 is out of reach; 2 * 2^20 sums, sorted, is not.
inline vector<long long> all_sums(const vector<long long>& a, size_t from, size_t to) {
    vector<long long> s{0};
    for (size_t i = from; i < to; ++i) {
        size_t k = s.size();
        for (size_t j = 0; j < k; ++j) s.push_back(s[j] + a[i]);      // each item is either left out or added
    }
    return s;
}
// How many subsets (the empty one included) have sum exactly `target`?  n <= 44.
inline long long count_subsets_mitm(const vector<long long>& a, long long target) {
    size_t h = a.size() / 2;
    vector<long long> left = all_sums(a, 0, h), right = all_sums(a, h, a.size());
    std::sort(right.begin(), right.end());
    long long total = 0;
    for (long long x : left) {
        auto p = std::equal_range(right.begin(), right.end(), target - x);
        total += p.second - p.first;
    }
    return total;
}
// The subset sum closest to `target` without exceeding it (the empty subset has sum 0). Needs sorted right and a search per left sum.
inline long long best_subset_sum_at_most(const vector<long long>& a, long long target) {
    size_t h = a.size() / 2;
    vector<long long> left = all_sums(a, 0, h), right = all_sums(a, h, a.size());
    std::sort(right.begin(), right.end());
    bool found = false;
    long long best = 0;
    for (long long x : left) {
        auto it = std::upper_bound(right.begin(), right.end(), target - x);
        if (it == right.begin()) continue;         // no right sum is small enough
        long long s = x + *(it - 1);
        if (!found || s > best) { best = s; found = true; }
    }
    return found ? best : target + 1;              // target + 1 means: not even the smallest total fits (possible with negatives)
}
// The same count by trying every subset, for comparison.
inline long long count_subsets_brute(const vector<long long>& a, size_t i, long long remaining) {
    if (i == a.size()) return remaining == 0;
    return count_subsets_brute(a, i + 1, remaining) + count_subsets_brute(a, i + 1, remaining - a[i]);
}
```

The closest sum not above `T`: for each left sum `x`, find the largest right sum at most `T - x` (`upper_bound` minus one) and keep the best total.

First, correctness against the brute force at sizes where brute force still finishes:

```
T5 n=20 numbers below 10^6, target = sum of every other one: 1 subsets by meet in the middle, 1 by trying all 1048576 subsets
T5 n=24 numbers below 10^6, target = sum of every other one: 6 subsets by meet in the middle, 6 by trying all 16777216 subsets
T5 n=26 numbers below 10^6, target = sum of every other one: 22 subsets by meet in the middle, 22 by trying all 67108864 subsets
```

```
T5 n=32 numbers below 10^9: 1 subsets reach the target 4022774889; the best sum below it is 4022774883 (target - 6)
T5 n=36 numbers below 10^9: 5 subsets reach the target 6173833860; the best sum below it is 6173833859 (target - 1)
T5 n=40 numbers below 10^9: 23 subsets reach the target 6560832564; the best sum below it is 6560832563 (target - 1)
T5 n=44 numbers below 10^9: 756 subsets reach the target 6671516589; the best sum below it is 6671516588 (target - 1)
```

| Method | Time (ms), run 1 / run 2 |
|---|---|
| brute force, 20 numbers | 1.1 / 1.1 |
| brute force, 24 numbers | 16.6 / 16.1 |
| brute force, 26 numbers | 64.2 / 62.5 |
| meet in the middle, 32 numbers | 9 / 9 |
| meet in the middle, 36 numbers | 44 / 44 |
| meet in the middle, 40 numbers | 166 / 153 |
| meet in the middle, 44 numbers | 1275 / 1304 |

Brute force multiplies by 4 for every two numbers added (63 ms at 26). Extrapolating, 40 numbers would take about `63 ms * 2^14`, around 17 minutes, and 44 numbers about 4.6 hours; meet in the middle does 44 numbers in about a second. The cost of the halves is memory: 44 numbers need two lists of `2^22` sums, 32 MB each (8 bytes per sum), and the limit is around 46 to 50 numbers. If the numbers are small, the table method of 14.01 is better; meet in the middle is the answer when the **numbers are large and `n` is at most about 45**.

The same two-halves idea (from memory) solves "four numbers summing to zero" with pair sums in a hash map (`O(n^2)`), and the shortest superstring or travelling salesman variants where half the state can be enumerated.

## 7. Bidirectional BFS

Shortest path in an unweighted graph, one source and one target. A one-sided BFS explores a ball of radius `d` around the source. If every vertex has about `b` neighbours, that ball has about `b^d` vertices. Two balls of radius `d/2` have about `2 b^(d/2)`: the square root. Run a BFS from each end, always expanding the smaller frontier by one whole level, and stop as soon as a vertex seen by one side is reached by the other.

Why the first meeting is optimal: when no meeting has happened, the two visited sets are disjoint, so the true distance `L` is at least `a + b + 1` where `a` and `b` are the depths already explored. Expanding one more level of the forward side to depth `a + 1`, any vertex the backward side has seen has backward distance at most `b`, so the total is at most `a + 1 + b <= L`; hence it equals `L`, and so does every other meeting found in that level.

```cpp
// Bidirectional BFS: search from both ends and stop when the two searches meet. If every vertex has about b neighbours and
// the distance is d, one-sided BFS reaches about b^d vertices; two searches of depth d/2 reach about 2 * b^(d/2).
// Each side expands a whole level at a time, the smaller frontier first. When a level finds a vertex the other side has seen,
// every such vertex gives the same total (a + 1 + b), so the first one found is the answer.
using Adj = vector<vector<int>>;
inline int bfs_dist(const Adj& g, int s, int t, long long& expanded) {
    vector<int> d(g.size(), -1);
    std::queue<int> q;
    d[s] = 0; q.push(s);
    expanded = 0;
    while (!q.empty()) {
        int u = q.front(); q.pop();
        ++expanded;
        if (u == t) return d[u];
        for (int v : g[u]) if (d[v] < 0) { d[v] = d[u] + 1; q.push(v); }
    }
    return -1;
}
inline int bidir_bfs(const Adj& g, int s, int t, long long& expanded) {
    expanded = 0;
    if (s == t) return 0;
    vector<int> ds(g.size(), -1), dt(g.size(), -1);
    vector<int> fs{s}, ft{t};
    ds[s] = 0; dt[t] = 0;
    while (!fs.empty() && !ft.empty()) {
        bool forward = fs.size() <= ft.size();
        vector<int>& fr = forward ? fs : ft;
        vector<int>& mine = forward ? ds : dt;
        vector<int>& other = forward ? dt : ds;
        vector<int> next;
        for (int u : fr) {
            ++expanded;
            for (int v : g[u]) {
                if (other[v] >= 0) return mine[u] + 1 + other[v];
                if (mine[v] < 0) { mine[v] = mine[u] + 1; next.push_back(v); }
            }
        }
        fr.swap(next);
    }
    return -1;
}
```

```
T6 random graph, 200000 vertices, about 6 neighbours each, 200 random pairs (0 not connected): distances agree in 200 of 200; average vertices expanded: one-sided 95491, bidirectional 176; average distance 7.2
T6 grid 300 x 300, corner to opposite corner: distance 598 and 598; vertices expanded: one-sided 90000, bidirectional 89402
T6 grid, two vertices 10 apart in the middle: distance 10 and 10; expanded: one-sided 211, bidirectional 74
```

| Work | Time (ms), run 1 / run 2 |
|---|---|
| one-sided BFS, 200 random pairs | 1091 / 1331 |
| bidirectional BFS, 200 random pairs | 12 / 12 |

On a random graph with 200,000 vertices the one-sided search expands about 95,000 vertices on average and the bidirectional one 176, and the time drops about 100 times. On a **grid** the gain is small: the ball of radius `r` has about `r^2` vertices (polynomial, not exponential), so two balls of radius `d/2` cover half the area of one ball of radius `d`. From corner to corner the saving is under 1 percent (89,402 against 90,000 vertices); for two vertices 10 apart in the middle, 74 against 211. The technique needs the graph to branch, and needs you to be able to walk edges backwards (in a directed graph or an implicit state space, you must be able to generate predecessors). Word ladder and the lock puzzle are the classic uses (from memory).

## 8. Choosing among them

| Situation | Technique |
|---|---|
| Range updates and queries with awkward summaries (sorted data, "at least `x`"), `n` around `10^5` | Blocks (section 2) |
| Graph problem with a few very high-degree vertices | Orient by degree, heavy and light (section 3) |
| Many static range queries, only `O(1)` add and remove known | Mo (section 4) |
| Same, but a sweep and a Fenwick tree exist | Offline sweep (section 5) |
| Large numbers, `n` up to about 45, subset or sum structure | Meet in the middle (section 6) |
| Unweighted shortest path in a branching graph or state space | Bidirectional BFS (section 7) |
| Values up to `10^9` need to index an array | Compression (section 5) |

## 9. Complexities

| Method | Time | Space |
|---|---|---|
| Blocks: add, sum, count at least `x` | `O(B log B + n / B + B)` per operation (the sort comes from rebuilding a partial block) | `O(n)` |
| Triangles, orientation | `O(m sqrt(m))` | `O(n + m)` |
| Triangles, intersect full lists | sum over edges of `deg(u) + deg(v)`, up to `O(n m)` | `O(n + m)` |
| Mo, `B = n / sqrt(q)` | `O(n sqrt(q))` moves plus sorting | `O(n + q)` |
| Offline sweep with a Fenwick tree | `O((n + q) log n)` | `O(n + q)` |
| Meet in the middle, subset sum | `O(2^(n/2) * n)` | `O(2^(n/2))` |
| Bidirectional BFS | about `2 b^(d/2)` against `b^d` on branching graphs | `O` of the vertices reached |

## 10. Common mistakes

1. Taking `B = sqrt(n)` without measuring when each partial operation costs more than `B` (a rebuild with a sort); forgetting that the last block is shorter.
2. Off-by-one at block edges: half-open or closed ranges mixed, an empty range (`l = r`) not handled.
3. Forgetting the lazy part when reading an element or a block sum, or when comparing with `x`.
4. Rebuilding a whole-block structure for a whole-block update, which makes the update `O(n)`.
5. **Mo:** sorting by `l` instead of by the block of `l`; moving the pointers in an order that makes the window negative for a non-counting query; using it with updates; choosing `B = sqrt(n)` instead of `n / sqrt(q)`; forgetting to compress values.
6. In the sweep: clearing the old position **after** setting the new one for the same value, or answering a query before position `r` is processed.
7. Counting triangles by orienting with only the vertex id (a hub with the smallest id keeps all its edges).
8. Meet in the middle: splitting unevenly, or sorting the wrong half; allocating `2^n` instead of `2^(n/2)`; overflow of the sums (use 64-bit).
9. Bidirectional BFS: stopping when a vertex is **popped** from both sides instead of when it is **discovered** by both (still correct if you take the minimum, but slower); expanding the larger frontier first; using it on a graph whose edges cannot be walked backwards.
10. Claiming `O(sqrt(n))` per operation when the block rebuild is `O(B log B)`.

## 11. Questions to answer out loud

1. Why does `B + n / B` have its minimum at `sqrt(n)`? What changes if a partial operation costs `B log B`?
2. How does a lazy value per block make a range add cost `O(n / B)` for the whole blocks?
3. When would you use a Fenwick tree or a segment tree instead of blocks? When blocks?
4. Why does orienting edges by degree bound the out-degree by `sqrt(2m)`? Why does a hub break the plain method?
5. Derive the number of pointer moves in Mo's algorithm. Why `B = n / sqrt(q)`?
6. What does the odd-even trick change, and by how much did it help here?
7. How does the offline sweep answer "distinct values in `[l, r]`"? Why must the old position be cleared?
8. Why compress coordinates? What stays the same after compression and what is lost?
9. Why is meet in the middle right for `n = 40` with large numbers, and the table method wrong? What limits it?
10. Why is the first meeting in bidirectional BFS already the shortest path?
11. Why does bidirectional BFS barely help on a grid?
12. Pick a technique: 200,000 range "number of distinct colours" queries on a static array.

## 12. Problems

### 12.1 On paper

**A.** For `n = 10^6`, an operation costs about `2B + n / B` steps. Evaluate it for `B` = 10, 100, 707, 1000 and 10,000. Where is the minimum, and why is it at `sqrt(n/2)`?

**B.** Mo's algorithm on `a = [1, 2, 1, 3, 2, 3]` with block size 2 and the queries `(0,2)`, `(1,4)`, `(2,5)`, `(0,5)`, `(3,4)`. Give the order the queries are processed in, the answers (distinct counts, in the order the queries were given), and the number of pointer moves (each add or remove counts 1; the window starts empty).

**C.** The graph with edges among `{0,1,2,3}` (all six), plus `3-4`, `4-5`, `3-5`. How many triangles? Give the degrees, and the out-degree of each vertex when edges are oriented from the smaller `(degree, id)` to the larger.

**D.** Meet in the middle on `a = [3, 34, 4, 12, 5, 2, 7, 9]` with target 14. Give the sums of each half (halves of four), the number of subsets with sum 14, and the best sum not above 13.

**E.** A path with 10 vertices, `0 - 1 - ... - 9`, from 0 to 9. How many vertices does one-sided BFS expand (pop) and how many does the bidirectional search expand, with the code above?

**F.** Choose the technique for each: (i) 40 numbers up to `10^12`, count subsets with a given sum; (ii) 200,000 queries, number of distinct values in a range, static array; (iii) range add and "how many elements are at least `x`" with updates, `n = 10^5`; (iv) shortest word ladder with a dictionary of 100,000 words; (v) count triangles in a social graph with a few celebrity vertices.

### 12.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. Mo's algorithm is rare on LeetCode; the standard source is competitive-programming sites (SPOJ DQUERY and Codeforces 86D are the classic ones, from memory).

| Problem | Number | How to approach |
|---|---|---|
| Closest Subsequence Sum | 1755 | Section 6: `n = 40`, values to `10^7`; two halves, sort, two pointers or binary search |
| Tallest Billboard | 956 | Meet in the middle on the difference, or a DP on the difference (either) |
| Split Array With Same Average | 805 | Meet in the middle on sums by number of elements, or a DP |
| Word Ladder | 127 | Section 7: bidirectional BFS over the words, with a set per side |
| Open the Lock | 752 | BFS over 10,000 states; bidirectional if you want the speedup |
| Range Sum Query - Mutable | 307 | A Fenwick tree is the usual answer; blocks are the alternative |
| Count of Smaller Numbers After Self | 315 | Offline: compress values, sweep with a Fenwick tree (section 5) |
| Count of Range Sum | 327 | Prefix sums, compress, Fenwick tree (or merge sort) |
| Reverse Pairs | 493 | The same offline idea with a Fenwick tree |

### 12.3 In your editor

**A.** Add a "set range to value" operation to the block structure (a second lazy kind) and test against the plain vector. Which lazy takes priority when both are pending?

**B.** Square root on queries: wrap a static prefix-sum array with a buffer of the last `B` point updates, rebuild after `B` updates, and choose `B` by measuring. Compare with the blocks of section 2 for range sum with point updates.

**C.** Implement Mo's algorithm for the **mode** of a range (most frequent value; keep `cnt[v]` and `freq[c]` = number of values with count `c`). Test against a scan.

**D.** Write the "four numbers summing to zero" count with pair sums in a hash map (four arrays `A, B, C, D`, LeetCode 454) and compare it with meet in the middle over sorted pair-sum vectors.

**E.** Make the bidirectional search work on a **directed** graph (keep a reverse adjacency list) and test it against one-sided BFS.

### Answers

The program that checks the paper problems printed:

```
A cost 2B + n/B for n = 1,000,000:  B=10 -> 100020;  B=100 -> 10200;  B=707 -> 2828;  B=1000 -> 3000;  B=10000 -> 20100;
B order (0,2) (1,4) (0,5) (3,4) (2,5) moves 14 answers by query id: 2 3 3 3 2
C triangles 5; degrees 3 3 3 5 2 2 ; out-degrees 3 2 1 0 2 1 ; edges 9
D left sums: 0 3 4 7 12 15 16 19 34 37 38 41 46 49 50 53
D right sums: 0 2 5 7 7 9 9 11 12 14 14 16 16 18 21 23
D subsets with sum 14: meet in the middle 6, brute 6; best sum at most 13: 13
E path of 10 vertices from 0 to 9: distances 9 and 9; vertices expanded 10 (one-sided) and 9 (bidirectional)
```

**A.** The costs are 100,020, 10,200, 2,828, 3,000 and 20,100. The minimum of `2B + n/B` is where the derivative `2 - n/B^2` is zero, `B = sqrt(n/2) = 707`, cost `2 sqrt(2n) = 2,828`. The cost grows quickly on both sides, but 1,000 is within 6 percent of the best.

**B.** The blocks of `l` with size 2 are `{0, 1}` (for `l` = 0 and 1), `{2, 3}` (for `l` = 2 and 3). Block 0 holds `(0,2)`, `(1,4)`, `(0,5)`, sorted by `r`: 2, 4, 5. Block 1 holds `(3,4)` and `(2,5)`, sorted by `r`: 4, 5. The order is `(0,2)`, `(1,4)`, `(0,5)`, `(3,4)`, `(2,5)`. The answers by query number are 2, 3, 3, 3, 2, and there are 14 moves: 3, then 3 more (one removal, two additions), 2 (one addition on each side), 4 (three left removals and one right), and 2.

**C.** Five triangles: the four inside the K4, and `3-4-5`. The degrees are 3, 3, 3, 5, 2, 2. Vertex 3 has the largest degree so it comes last and its out-degree is 0; the out-degrees are 3, 2, 1, 0, 2, 1 (their sum is 9, the number of edges). The largest out-degree, 3, is below `sqrt(2 * 9) = 4.2`.

**D.** The left half `[3, 34, 4, 12]` has the 16 sums 0, 3, 4, 7, 12, 15, 16, 19, 34, 37, 38, 41, 46, 49, 50, 53 and the right half `[5, 2, 7, 9]` has 0, 2, 5, 7, 7, 9, 9, 11, 12, 14, 14, 16, 16, 18, 21, 23. Six subsets have sum 14: `{12, 2}`, `{3, 4, 7}`, `{3, 2, 9}`, `{5, 9}`, `{5, 2, 7}` and `{3, 4, 5, 2}`. In terms of the halves: left sum 0 with right sum 14 (two ways, `{5, 9}` and `{5, 2, 7}`), left 3 with right 11 (`{2, 9}`), left 7 (`{3, 4}`) with right 7 (two ways, `{7}` and `{5, 2}`), and left 12 with right 2: 2 + 1 + 2 + 1 = 6. The best sum not above 13 is 13 itself (for example `{4, 9}`).

**E.** One-sided BFS pops all 10 vertices (it pops the target last). The bidirectional search, expanding the forward side first on equal frontiers, finds the meeting after 9 expansions: the saving on a path is tiny, because a path does not branch.

**F.** (i) Meet in the middle. (ii) The offline sweep with a Fenwick tree, or Mo as the alternative. (iii) Blocks with sorted copies. (iv) Bidirectional BFS. (v) Orient edges by degree, so the celebrities do not dominate.

## Cheat sheet

- **Blocks:** cost `B + n/B` (plus a sort per rebuild), so measure `B`: best was 64 for `n = 131,072`, about 4 to 6 times faster than plain loops. Lazy per block; sorted copy per block for "at least `x`".
- **Triangles:** orient by `(degree, id)`; out-degree at most `sqrt(2m)`; `O(m sqrt(m))`; 30 times faster than intersecting full lists with a hub (6 ms against 160 to 200 ms).
- **Mo:** sort by (block of `l`, `r`), `B = n / sqrt(q)`, `O(n sqrt(q))` moves; the odd-even trick gave 26.4 million moves against 41.9 million. Needs `O(1)` add and remove and no updates.
- **Offline sweep:** Fenwick tree with a 1 at the latest position of each value; faster than Mo (14 ms against 30 ms for 100,000 queries). Compress coordinates first.
- **Meet in the middle:** `2^(n/2)` per half, sort one, search from the other; 44 numbers in about a second, where brute force would need hours; memory is the limit (around 46 to 50 numbers).
- **Bidirectional BFS:** expand the smaller frontier by a whole level; 176 expansions against 95,491 on a random graph, but about equal on a grid corner to corner.
- **Say in an interview:** name the split, give the cost balance, say what the limits allow.

---

## Sources

- All code is tested here (4,834,041 checks under ASan and UBSan; 21 counted deliberate bugs all caught, one only by timeout, plus one that cannot change the answer). The traces and timings are measured on one Xeon container, two runs each, with identical traces. The data is random and seeded.
- The techniques (sqrt decomposition, heavy and light degrees for triangles, Mo's algorithm and its odd-even ordering, the Fenwick sweep for distinct values, meet in the middle for subset sum, bidirectional BFS) are standard; I describe them from memory and tested them here. The Mo-with-updates exponent, Mo on trees, the heavy-light uses for four-cycles, the four-sum reduction, the memory limit for meet in the middle and the remarks about contest sites and Word Ladder are from memory and were not checked.
- LeetCode numbers and titles are from memory; check a number before you rely on it. The explanations, traces and code are my own.
