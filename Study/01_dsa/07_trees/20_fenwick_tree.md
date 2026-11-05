# DSA 07.20: The Fenwick Tree (Binary Indexed Tree): Prefix Sums That Can Change

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Prefix sums, sparse tables and RMQ](19_prefix_sums_sparse_tables_and_rmq.md) (prefix sums, difference arrays; this topic makes them dynamic). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- explain the **lowbit trick** and the implicit tree it defines, and write `add`, `prefix`, `range` and the O(n) build from memory;
- do **range add with point query**, and **range add with range sum** (two trees, with the algebra that explains why);
- find the **k-th element** of a changing multiset by **binary lifting** on the tree, in one descent;
- count **inversions** and "smaller elements after me", and say why a Fenwick tree for **maximum** exists but only half-works;
- build a **2D** Fenwick tree and say what it costs;
- say, with measurements, what the tree buys over the two obvious alternatives, and where a plain algorithm beats it.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 727,793 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: `lowbit` on its definition; the tree against naive sums for every length 0 to 70 (60 random updates each, with prefixes and ranges checked after every update, **every node compared with its definition** and the O(n) build compared with n separate adds); `kth` against a scan for 400 count arrays that include zeros; `RangeAdd` and `RangeSum` against naive arrays (300 runs, with and without an initial array); the 2D tree on **every rectangle** of 120 grids; the inversion counter, "smaller after" and the longest increasing subsequence against O(n^2) code (400 arrays with many equal values) and 100,000 reversed numbers for the 64-bit counter; `CountMultiset` against `std::multiset` (300 runs); and out-of-range arguments for every class. Finally 67 hand-made mutants of the code were run against the tests (a wrong lowbit step, a missing term in the range-sum algebra, a tie in the descent and so on): **66 were caught and 1 is an equivalent change** that cannot alter any output (listed after the common mistakes).

**Data and timing.** The array is **synthetic**: 1,000,000 random integers below 1,000 (a million `long long` counters in the tree, 8,000,008 bytes). Operations use random positions, generated beforehand. Times are nanoseconds per operation (or milliseconds per run) on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`; the same machine as Topic 19), best of 5 repetitions (best of 3 for the long ones), shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. Statements about systems practice are from general knowledge and say so.

---

## 1. The problem

Topic 19 answered "what is the sum of `a[l..r)`" in O(1) after a prefix-sum pass, for an array that **never changes**. Now let it change: `add(i, d)` and `prefix(i)` arrive in any order. The two obvious designs each make one operation slow:

| Design | `add` | `prefix` | Memory |
|---|---|---|---|
| The array itself, summed on demand | O(1) | O(n) | n |
| A prefix array, repaired on every change | O(n) | O(1) | n |
| **Fenwick tree** | **O(log n)** | **O(log n)** | **n** |

The measured cost of the two obvious designs, for a million elements, is in section 8; the point here is that at O(n) per operation, 100,000 operations already take more than 20 seconds.

---

## 2. The lowbit trick and the implicit tree

`lowbit(j)` is the lowest set bit of `j`: `lowbit(12) = 4` (`1100` becomes `0100`). In two's complement it is `j & -j`.

Store, for every `j` from 1 to `n`, one number `t[j]`: the **sum of the `lowbit(j)` elements that end at position `j`**, that is `a[j - lowbit(j)] .. a[j - 1]`. For `n = 8`:

| j | lowbit(j) | `t[j]` covers (0-based, half-open) | next node up (`j + lowbit(j)`) |
|---|---|---|---|
| 1 | 1 | [0, 1) | 2 |
| 2 | 2 | [0, 2) | 4 |
| 3 | 1 | [2, 3) | 4 |
| 4 | 4 | [0, 4) | 8 |
| 5 | 1 | [4, 5) | 6 |
| 6 | 2 | [4, 6) | 8 |
| 7 | 1 | [6, 7) | 8 |
| 8 | 8 | [0, 8) | 16 |

Odd positions cover one element, positions of the form 2 times odd cover two, multiples of 4 (not 8) cover four, and so on: the ranges nest like a binary counter. Two walks on this table do all the work:

- **Prefix query.** To sum `a[0..i)` start at `j = i` and repeat `j -= lowbit(j)`: each step removes the lowest set bit and jumps to the end of the next-earlier block, so the visited blocks tile `[0, i)` exactly. It visits `popcount(i)` nodes, at most `log2 n + 1`. For `i = 6` (`110`) the walk is `6, 4`: `t[6] + t[4] = [4,6) + [0,4)`.
- **Point update.** To change `a[i]` start at `j = i + 1` and repeat `j += lowbit(j)`: each step goes to the next node whose range contains `i`. The lowbit strictly grows at every step, so there are at most `log2 n + 1` steps. For `i = 2` the walk is `3, 4, 8`.

The one-based indexing is **not** a style choice: `lowbit(0) = 0`, so `j += lowbit(j)` from 0 never moves and the loop never ends. The public interface below is zero-based and does the shift inside.

```cpp
// lowbit(j) = the lowest set bit of j: 12 (1100) -> 4 (0100).  In two's complement j & -j; written with ~j + 1 to stay unsigned.
inline std::size_t lowbit(std::size_t j) { return j & (~j + 1); }

// Internally 1-based.  t_[j] holds the sum of the lowbit(j) elements that END at position j:  a[j - lowbit(j)] .. a[j - 1]  (0-based a).
// The public interface is 0-based: add(i, d) changes a[i]; prefix(i) = a[0] + ... + a[i-1]; range(l, r) = a[l] + ... + a[r-1].
template <class T>
class Fenwick {
    std::vector<T> t_;
    friend struct TestAccess;
public:
    explicit Fenwick(std::size_t n = 0) : t_(n + 1, T(0)) {}
    explicit Fenwick(const std::vector<T>& a) : t_(a.size() + 1, T(0)) {     // O(n) build: push each node's total to its parent
        for (std::size_t j = 1; j <= a.size(); ++j) {
            t_[j] += a[j - 1];
            std::size_t p = j + lowbit(j);                                   // the next node that covers j
            if (p < t_.size()) t_[p] += t_[j];
        }
    }
    std::size_t size() const { return t_.size() - 1; }
    void add(std::size_t i, T d) {                                           // a[i] += d, in O(log n)
        if (i >= size()) throw std::out_of_range("Fenwick::add");
        for (std::size_t j = i + 1; j < t_.size(); j += lowbit(j)) t_[j] += d;       // climb to every node whose range contains i
    }
    T prefix(std::size_t i) const {                                          // a[0] + ... + a[i-1]
        if (i > size()) throw std::out_of_range("Fenwick::prefix");
        T s = T(0);
        for (std::size_t j = i; j > 0; j -= lowbit(j)) s += t_[j];           // peel the lowest bit: each step jumps to the end of the next block
        return s;
    }
    T range(std::size_t l, std::size_t r) const {
        if (l > r || r > size()) throw std::out_of_range("Fenwick::range");
        return prefix(r) - prefix(l);
    }
    T at(std::size_t i) const { return range(i, i + 1); }
    // Smallest 0-based index i with prefix(i + 1) >= k, or size() if the total is below k.  Needs every element >= 0 and k >= 1.
    //    A binary descent over the implicit tree: try the biggest block first, take it if it still falls short of k.
    std::size_t kth(T k) const {
        if (!(T(0) < k)) throw std::out_of_range("Fenwick::kth");
        std::size_t n = size(), pos = 0, step = 1;
        while (step * 2 <= n) step *= 2;                                     // the highest power of two <= n
        for (; step > 0; step >>= 1)
            if (pos + step <= n && t_[pos + step] < k) { pos += step; k -= t_[pos]; }
        return pos;                                                          // pos elements are strictly before the answer
    }
};
```

For `a = 3 1 4 1 5 9 2 6` the tree is `t = 3 4 4 9 5 14 2 31` (`t[6] = 5 + 9 = 14`, `t[8]` is the whole sum). `prefix(6)` reads `t[6] + t[4] = 14 + 9 = 23`. After `add(2, +10)` the walk `3, 4, 8` changes `t` to `3 4 14 19 5 14 2 41`; now `prefix(6) = 14 + 19 = 33`, `range(2, 6) = 33 - 4 = 29` and `at(2) = 14`.

**The O(n) build.** Adding the n elements one at a time costs O(n log n). The second constructor instead sets `t[j] = a[j-1]` and then, in increasing `j`, pushes `t[j]` to its parent `j + lowbit(j)`: by the time `j` is pushed, every child of `j` (they all have smaller indices) has already added itself. For n = 8 the pushes are `1->2, 2->4, 3->4, 4->8, 5->6, 6->8, 7->8`. For a million elements the build took **5.3 / 6.0 ms** against **16.2 / 16.3 ms** for a million `add`s: **2.7 to 3.1 times faster**, and the two trees are identical (the program checks it).

**Cost.** Memory is `n + 1` words, the same as the prefix array. Measured on a million elements: a prefix query took **24.6 / 24.2 ns**, an update **49.6 / 59.4 ns**, an update costing **2.0 to 2.5 times** a query: it writes, and its walk goes up through nodes that the next query may need. (The update figure includes zeroing the 8 MB tree once, which is under 1 ns per operation.)

---

## 3. Which operations fit: invertible ones, and maximum with a catch

A range query is `prefix(r) - prefix(l)`, so the operation needs an **inverse**: sum, XOR, sum modulo a number, counts. `min`, `max` and `gcd` have no inverse, so there is no range query. What survives is a **prefix** query for such an operation, provided values only move in one direction: a node of a maximum tree stores the maximum of its block, and that stays valid only if values are never lowered (lowering the maximum of a block would need a recomputation from the children).

```cpp
// A Fenwick tree for prefix MAXIMUM.  max has no inverse, so there is no "range" query and a value can only be RAISED:
//    update(i, v) sets a[i] = max(a[i], v).  Both loops are the same as before with + replaced by max.
class PrefixMax {
    std::vector<int> t_;
public:
    explicit PrefixMax(std::size_t n) : t_(n + 1, 0) {}
    void update(std::size_t i, int v) {
        if (i + 1 >= t_.size()) throw std::out_of_range("PrefixMax::update");
        for (std::size_t j = i + 1; j < t_.size(); j += lowbit(j)) t_[j] = std::max(t_[j], v);
    }
    int query(std::size_t i) const {                                         // max of a[0..i), 0 if empty
        if (i + 1 > t_.size()) throw std::out_of_range("PrefixMax::query");
        int m = 0;
        for (std::size_t j = i; j > 0; j -= lowbit(j)) m = std::max(m, t_[j]);
        return m;
    }
};

// Length of the longest strictly increasing subsequence: best[x] = 1 + (the best length that ends in a smaller rank).
std::size_t lis_length(const std::vector<int>& a) {
    int m = 0;
    auto r = ranks(a, &m);
    PrefixMax f(m);
    int best = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        int len = f.query(r[i]) + 1;                                         // ranks strictly below r[i]
        f.update(r[i], len);
        best = std::max(best, len);
    }
    return (std::size_t)best;
}
```

This is exactly what the longest increasing subsequence needs. Process the elements in order; `best[x]` is one plus the best length ending in a **smaller** value, which is a prefix maximum over the ranks below `x`, and then `x`'s own length is **raised** in the tree. For `10 9 2 5 3 7 101 18` the answer is 4 (for example `2 5 7 101`). The `ranks` function (next section) shrinks values of any size to `0 .. m-1`.

An honest measurement: for a random permutation of a million the tree took **299.5 / 300.1 ms** and the standard patience-sorting method (a sorted array of tails and `lower_bound`) took **60.5 / 54.7 ms**, **5.0 to 5.5 times faster** and simpler; both gave the length 1,969. The tree is the right tool when the problem has an extra twist that patience sorting cannot absorb (weights, a bound on the previous value that is not simply smaller); for the plain problem it is the slower one.

---

## 4. Range add: the difference array again

**Range add, point query.** Keep a Fenwick tree over the **difference array** `d` of Topic 19 (`d[i] = a[i] - a[i-1]`): then `a[i]` is `prefix(i + 1)` of `d`, and "add `v` to `a[l..r)`" is two point updates, `d[l] += v` and `d[r] -= v`. The second is skipped when `r = n`, because that position does not exist and is never read.

**Range add, range sum.** The sum of the first `i` elements is a sum over the differences:

`sum_{j<i} a[j] = sum_{k<i} d[k] * (i - k) = i * (sum_{k<i} d[k]) - (sum_{k<i} k * d[k])`

so two trees are enough: one over `d[k]` and one over `k * d[k]`, and a prefix sum of `a` is `i * B1.prefix(i) - B2.prefix(i)`. (Starting from a non-zero array only adds the static prefix sums of the initial values, kept in a plain array.)

```cpp
// Range add, point query: a Fenwick tree over the DIFFERENCE array.  a[i] is the prefix sum of the differences.
class RangeAdd {
    Fenwick<long long> d_;
public:
    explicit RangeAdd(std::size_t n) : d_(n) {}                              // a starts as all zeros
    void add(std::size_t l, std::size_t r, long long v) {                    // a[l..r) += v
        if (l > r || r > d_.size()) throw std::out_of_range("RangeAdd::add");
        if (l == r) return;
        d_.add(l, v);
        if (r < d_.size()) d_.add(r, -v);                                    // the end marker r == n would fall off the tree and is never read
    }
    long long at(std::size_t i) const { return d_.prefix(i + 1); }
};

// Range add AND range sum: two Fenwick trees.  With d the difference array, the prefix sum of a is
//    sum_{j<i} a[j] = sum_{k<i} d[k] * (i - k) = i * (sum_{k<i} d[k]) - (sum_{k<i} k * d[k]),
//    so one tree keeps d[k] and the other keeps k * d[k].  base_ holds the prefix sums of the initial array.
class RangeSum {
    std::size_t n_;
    Fenwick<long long> b1_, b2_;
    std::vector<long long> base_;
    long long pre(std::size_t i) const { return base_[i] + (long long)i * b1_.prefix(i) - b2_.prefix(i); }
public:
    explicit RangeSum(std::size_t n) : n_(n), b1_(n), b2_(n), base_(n + 1, 0) {}
    explicit RangeSum(const std::vector<long long>& a) : RangeSum(a.size()) {
        for (std::size_t i = 0; i < a.size(); ++i) base_[i + 1] = base_[i] + a[i];
    }
    void add(std::size_t l, std::size_t r, long long v) {                    // a[l..r) += v
        if (l > r || r > n_) throw std::out_of_range("RangeSum::add");
        if (l == r) return;
        b1_.add(l, v);
        b2_.add(l, v * (long long)l);
        if (r < n_) { b1_.add(r, -v); b2_.add(r, -v * (long long)r); }
    }
    long long sum(std::size_t l, std::size_t r) const {                      // a[l] + ... + a[r-1]
        if (l > r || r > n_) throw std::out_of_range("RangeSum::sum");
        return pre(r) - pre(l);
    }
};
```

Take six zeros, `add(1, 4, 2)` then `add(3, 6, 1)`, which gives the array `0 2 2 3 1 1`. The differences are `d = 0 2 0 1 -2 0`. The prefix sum of the first 5 elements is `5 * (0 + 2 + 0 + 1 - 2) - (1*2 + 3*1 + 4*(-2)) = 5 - (-3) = 8` and the first 2 elements give `2 * 2 - 1*2 = 2`, so `sum(2, 5) = 8 - 2 = 6` (`2 + 3 + 1`); `sum(0, 6) = 9` and `sum(0, 2) = 2`. The program prints the same values.

**Cost.** Per operation measured on a million elements, with random ranges: `RangeAdd.add` **102.6 / 109.4 ns**, `RangeSum.add` **239.1 / 261.4 ns** (**2.3 to 2.4 times** more: four tree updates instead of two) and `RangeSum.sum` **201.9 / 197.6 ns** (four prefix queries). It is the price of arbitrary ranges on both sides; a lazy segment tree (Topic 22) handles operations other than sum.

---

## 5. The k-th element: binary lifting on the tree

If every `a[i]` is a **count** (non-negative), the prefix sums grow with `i`, and "the smallest `i` with `prefix(i + 1) >= k`" is the position of the k-th element of the multiset. Binary search on top of `prefix` costs O(log^2 n). The tree supports a **single descent**: try the biggest block first and take it if the total is still short of `k`; blocks are `t[pos + step]` for a power of two `step`, and they exist exactly because `pos` only ever has bits larger than `step` set.

For the multiset `{2, 2, 5, 7, 7, 7}` over the values `0 .. 9` the count tree is `t[1..10] = 0 0 2 2 0 1 0 6 0 0`. The descent for `k = 4`:

| Step | Looks at | Value | Decision |
|---|---|---|---|
| 8 | `t[8]` | 6 | not below 4: skip |
| 4 | `t[4]` | 2 | below 4: take (`pos = 4`, `k = 2`) |
| 2 | `t[6]` | 1 | below 2: take (`pos = 6`, `k = 1`) |
| 1 | `t[7]` | 0 | below 1: take (`pos = 7`) |

`pos = 7` elements lie strictly before the answer, so the 4th smallest is the value **7** (the program prints `2 2 5 7 7 7` for `k = 1 .. 6`). With zeros in the counts the rule `t < k` (strict) is what makes the descent land on the **first** index that reaches `k`, not the last.

```cpp
// A multiset of integers in [0, m) with insert, erase, rank and the k-th smallest, all in O(log m): a Fenwick tree of counts.
class CountMultiset {
    Fenwick<int> f_;
    int total_ = 0;
public:
    explicit CountMultiset(std::size_t m) : f_(m) {}
    int size() const { return total_; }
    void insert(std::size_t x) { f_.add(x, 1); ++total_; }
    bool erase(std::size_t x) {                                              // one copy of x
        if (x >= f_.size() || f_.at(x) == 0) return false;
        f_.add(x, -1);
        --total_;
        return true;
    }
    int count_less(std::size_t x) const { return f_.prefix(std::min(x, f_.size())); }
    std::size_t kth(int k) const {                                           // the k-th smallest, k = 1 .. size()
        if (k < 1 || k > total_) throw std::out_of_range("CountMultiset::kth");
        return f_.kth(k);
    }
};
```

The class is an order-statistic multiset for a bounded universe: insert, erase, rank (`count_less`) and select (`kth`), all O(log m), with no pointers and one array. Measured on a million counters (a total of 1,499,341 elements, random `k`): the descent took **237.9 / 236.3 ns** against **466.5 / 510.9 ns** for a binary search over `prefix`, **2.0 to 2.2 times faster** (not 20 times: the lower levels of the tree are cache misses either way).

**Systems use** (general knowledge, not measured here). A tree over the buckets of a latency histogram answers "which bucket holds the 99th percentile" in O(log m) while buckets keep changing; many histogram libraries simply scan the buckets, which is also fine for a few hundred buckets.

---

## 6. Counting inversions

An **inversion** is a pair `i < j` with `a[i] > a[j]`. Sweep left to right with a tree of counts over the **ranks** of the values (replace each value by its position among the distinct values; equal values share a rank, and the tree needs only `m` slots instead of the value range). At element `i`, the earlier elements that are not greater are those of rank at most `r[i]`, so the inversions that end here number `i - prefix(r[i] + 1)`. A sweep from the right with `prefix(r[i])` counts instead the **strictly smaller elements after** each position.

```cpp
// Replace each value by its rank among the distinct values (equal values share a rank), so that the tree needs only m slots.
inline std::vector<int> ranks(const std::vector<int>& a, int* distinct = nullptr) {
    std::vector<int> s(a);
    std::sort(s.begin(), s.end());
    s.erase(std::unique(s.begin(), s.end()), s.end());
    std::vector<int> r(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) r[i] = (int)(std::lower_bound(s.begin(), s.end(), a[i]) - s.begin());
    if (distinct) *distinct = (int)s.size();
    return r;
}

// Pairs i < j with a[i] > a[j].  Sweep left to right; the earlier elements that are NOT greater are those of rank <= r[i].
long long count_inversions(const std::vector<int>& a) {
    int m = 0;
    auto r = ranks(a, &m);
    Fenwick<int> seen(m);
    long long inv = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        inv += (long long)i - seen.prefix(r[i] + 1);                         // earlier elements minus the earlier ones that are <= a[i]
        seen.add(r[i], 1);
    }
    return inv;
}

// out[i] = the number of j > i with a[j] < a[i].  Sweep right to left; strictly smaller means rank < r[i].
std::vector<int> count_smaller_after(const std::vector<int>& a) {
    int m = 0;
    auto r = ranks(a, &m);
    Fenwick<int> seen(m);
    std::vector<int> out(a.size());
    for (std::size_t i = a.size(); i-- > 0;) {
        out[i] = seen.prefix(r[i]);
        seen.add(r[i], 1);
    }
    return out;
}
```

For `2 4 1 3 5` the contributions are `(2: 0) (4: 0) (1: 2) (3: 1) (5: 0)`, total **3** (the pairs `(2,1)`, `(4,1)`, `(4,3)`). `count_smaller_after(5 2 6 1)` is `2 1 1 0`. The counter must be 64-bit: a reversed array of 100,000 numbers has `4,999,950,000` inversions (a test checks it).

Honest measurement, again: on a random permutation of a million (**249,771,169,222** inversions, close to the expected `n(n-1)/4`) the tree took **318.2 / 287.3 ms** and the classic merge-sort counter took **105.5 / 103.2 ms**, **2.8 to 3.0 times faster**. The tree wins when the sweep has to answer more than the total (the per-element counts above, or queries interleaved with updates).

---

## 7. Two dimensions

Nest the loops: a Fenwick tree whose nodes are themselves Fenwick trees. An update walks `O(log R)` rows and, in each, `O(log C)` columns; a prefix rectangle query does the same with the other walk.

```cpp
// Two dimensions: the same loops, nested.  The tree is a Fenwick tree of Fenwick trees.
class Fenwick2D {
    std::size_t R_, C_;
    std::vector<long long> t_;                                               // (R + 1) x (C + 1), row-major, 1-based
    friend struct TestAccess;
public:
    Fenwick2D(std::size_t rows, std::size_t cols) : R_(rows), C_(cols), t_((rows + 1) * (cols + 1), 0) {}
    void add(std::size_t r, std::size_t c, long long d) {                    // g[r][c] += d
        if (r >= R_ || c >= C_) throw std::out_of_range("Fenwick2D::add");
        for (std::size_t j = r + 1; j <= R_; j += lowbit(j))
            for (std::size_t k = c + 1; k <= C_; k += lowbit(k)) t_[j * (C_ + 1) + k] += d;
    }
    long long prefix(std::size_t r, std::size_t c) const {                   // the sum of the rows < r and columns < c
        if (r > R_ || c > C_) throw std::out_of_range("Fenwick2D::prefix");
        long long s = 0;
        for (std::size_t j = r; j > 0; j -= lowbit(j))
            for (std::size_t k = c; k > 0; k -= lowbit(k)) s += t_[j * (C_ + 1) + k];
        return s;
    }
    long long rect(std::size_t r0, std::size_t c0, std::size_t r1, std::size_t c1) const {     // rows [r0, r1) x columns [c0, c1)
        if (r0 > r1 || c0 > c1) throw std::out_of_range("Fenwick2D::rect");
        return prefix(r1, c1) - prefix(r0, c1) - prefix(r1, c0) + prefix(r0, c0);
    }
};
```

For the grid `1 2 3 4 / 5 6 7 8 / 9 10 11 12`, `rect(1, 1, 3, 3) = 34` (the elements `6 7 10 11`, as in Topic 19), `prefix(3, 4) = 78`, and after `add(1, 1, +100)` the same rectangle reads **134**. Memory is `(R + 1)(C + 1)` words: for 2048 by 2048, **33,587,208 bytes**. A million-scale run took **304.9 / 266.1 ns** per update and **708.7 / 658.9 ns** per rectangle (four prefix queries, each walking up to 11 by 11 nodes). Use it when the grid changes; for a static grid the 2D prefix array of Topic 19 answers in four reads. Many 2D counting problems can be turned into 1D ones by sorting the points and sweeping (the inversion counter is that trick), which is both simpler and smaller.

---

## 8. What the tree buys, measured

For a million elements, random positions:

| Operation | Time per operation | Comparison |
|---|---|---|
| `prefix` | 24.6 / 24.2 ns | the plain scan of a prefix took **214,098 / 216,328 ns**: **8,700 to 8,900 times faster** |
| `add` | 49.6 / 59.4 ns | repairing a prefix array took **227,609 / 218,807 ns** per update: **3,700 to 4,600 times faster** |
| build | 5.3 / 6.0 ms | a million `add`s: 16.2 / 16.3 ms |

(The scan and the repair were timed on 2,000 random operations each, because a million would take minutes; all three structures returned the same sums.) Neither obvious design is ever the right one once both operations are frequent.

**Fenwick tree or segment tree?** The next topic's segment tree does the same job and more (any associative operation, range minimum with updates, lazy range updates), with about `2n` words and a few more lines. The Fenwick tree wins on code size (two short loops), on memory, and in my experience on constants (Topic 21 will measure it). It loses whenever the operation has no inverse or the updates are not point-wise. A reasonable rule: sums, counts and XOR with point updates: Fenwick; anything else: segment tree. Topic 21 will measure the two against each other.

---

## 9. Common mistakes

1. **Zero-based indices inside the loops.** `lowbit(0)` is 0, so `j += lowbit(j)` from 0 never ends. Shift by one inside, once.
2. **Mixing the two index conventions.** `prefix(i)` takes a count (`a[0..i)`), `add(i, d)` an index. Write both in half-open form and keep it.
3. **Moving the wrong way.** Queries subtract the lowbit (`j -= lowbit(j)`), updates add it (`j += lowbit(j)`).
4. **A tree of `n` slots** instead of `n + 1`.
5. **`int` sums.** The sum, and especially the inversion count, outgrows 32 bits; use `long long`.
6. **`kth` with negative values.** The descent needs monotone prefix sums; counts only. With a strict `<` it returns the first index that reaches `k`; with `<=` it overshoots.
7. **Range add + range sum with one tree**, a missing `k * d[k]` tree, or the wrong sign in `-v * r`. Guard the `r = n` update.
8. **Using the tree for a range minimum**, or lowering a value in the maximum tree.
9. **Strict against non-strict.** An inversion needs `a[i] > a[j]` (equal elements do not count); the longest strictly increasing subsequence needs the ranks **below** `x`, a non-decreasing one the ranks **up to** `x`.
10. **No coordinate compression**: a tree over values up to 10^9 does not fit in memory; use `ranks`.
11. **A fresh tree for every query set** instead of the O(n) build.
12. **A 2D tree for a static grid** (four reads from the prefix array are enough), or with the dimensions swapped.
13. **Picking the tree where the plain algorithm wins** (section 6 and section 3).

**The mutant that survives**, checked by hand: `k < 0` instead of `k < 1` in `CountMultiset::kth`. For `k = 0` the inner `Fenwick::kth` rejects the value with the same exception, so the outer check changes nothing that a caller can observe.

---

## 10. Questions to answer out loud

1. What does `t[j]` store? Which elements does `t[12]` cover?
2. Why does `prefix` visit at most `log n + 1` nodes? Why does `add`?
3. Why is the tree one-based inside? What would happen with `lowbit(0)`?
4. How does the O(n) build work, and why is the order of the pushes right?
5. Why can a Fenwick tree answer range sums but not range minimums?
6. What is the range-add / point-query design? What is stored?
7. Derive the prefix sum of an array under range adds. Why two trees?
8. How does `kth` work? Why does it need non-negative values, and why a strict `<`?
9. How do you count inversions with a tree? Where does `i - prefix(r[i] + 1)` come from?
10. What does a Fenwick tree for prefix maximum give up?
11. What does a 2D tree cost in time and memory?
12. When would you pick a segment tree instead?

---

## 11. Problems

**A. The tree.** Build the Fenwick tree of `3 1 4 1 5 9 2 6` (give `t[1..8]`) and say which nodes `prefix(6)` reads.

**B. The update path.** Which nodes does `add(2, d)` change, for `n = 8`? After `add(2, +10)`, what are `t` and `prefix(6)`?

**C. k-th.** For the multiset `{2, 2, 5, 7, 7, 7}` over `0 .. 9`, find the 4th smallest and the 6th, and say what `count_less(7)` is.

**D. Range add, range sum.** Start from six zeros, `add(1, 4, 2)`, then `add(3, 6, 1)`. What are `sum(0, 6)`, `sum(2, 5)` and `sum(0, 2)`?

**E. Inversions.** Count the inversions of `2 4 1 3 5`. Give `count_smaller_after` for `5 2 6 1`.

**F. LIS.** Find the length of the longest strictly increasing subsequence of `10 9 2 5 3 7 101 18`. Which operation of the maximum tree is used for each element?

**G. 2D.** For the grid `1 2 3 4 / 5 6 7 8 / 9 10 11 12`, give `rect(1, 1, 3, 3)` and `prefix(3, 4)`, and the rectangle after adding 100 at row 1, column 1.

**H. Choose.** Pick a structure for each: (a) 10^6 elements, 10^6 point updates mixed with 10^6 prefix sums; (b) the same array, never changing; (c) range minimum with point updates; (d) "add `v` to a range" and "sum a range", both 10^5 times; (e) the median of a stream of integers below 10^5.

---

## Answers

**A.** `t = 3 4 4 9 5 14 2 31` (`t[2] = 3 + 1`, `t[4] = 3 + 1 + 4 + 1`, `t[6] = 5 + 9`, `t[8]` = everything). `prefix(6)` starts at `j = 6` (`110`), then `j = 4` (`100`), then `0`: it reads `t[6] + t[4] = 14 + 9 = 23`. Program output: `3 4 4 9 5 14 2 31` and 23.

**B.** Index 2 is position `j = 3`; then `3 + 1 = 4`, then `4 + 4 = 8`, then `8 + 8 = 16 > 8`: the nodes **3, 4, 8**. After `add(2, +10)`: `t = 3 4 14 19 5 14 2 41`, and `prefix(6) = t[6] + t[4] = 14 + 19 = 33`. Program output: the same.

**C.** The 4th smallest is **7** and the 6th is **7** (the sorted multiset is `2 2 5 7 7 7`); `count_less(7)` is **3** (the elements 2, 2, 5). Program output: `7`, `7`, `3`.

**D.** The array is `0 2 2 3 1 1`: `sum(0, 6) = 9`, `sum(2, 5) = 2 + 3 + 1 = 6`, `sum(0, 2) = 0 + 2 = 2`. The derivation is in section 4. Program output: 9, 6 and 2.

**E.** The inversions of `2 4 1 3 5` are `(2,1)`, `(4,1)`, `(4,3)`: **3**. `count_smaller_after(5 2 6 1)` is `2 1 1 0` (after 5: 2 and 1; after 2: 1; after 6: 1; after 1: none). Program output: 3 and `2 1 1 0`.

**F.** **4** (for example `2 5 7 101`, or `2 3 7 18`). For each element: **query** the prefix maximum over the ranks below it, add 1, then **raise** its own rank to that length with `update`. Program output: 4.

**G.** `rect(1, 1, 3, 3) = 6 + 7 + 10 + 11 = 34`, `prefix(3, 4) = 78` (the sum of 1 to 12), and after `add(1, 1, +100)` the rectangle reads **134** (the element 6 became 106). Program output: 34, 78, 134.

**H.** (a) A Fenwick tree: 10^6 operations at O(log n) each. (b) A prefix array (Topic 19): no updates, so O(1) per query. (c) A segment tree (Topic 21): the minimum has no inverse. (d) Two Fenwick trees (`RangeSum`), or a lazy segment tree. (e) A `CountMultiset` over the values below 10^5 and `kth(size / 2 + 1)`, or two heaps (Topic 16) if the universe is unbounded.

---

## Cheat sheet

- **Node:** `t[j]` = sum of the `lowbit(j)` elements ending at `j`. **Query:** `j -= lowbit(j)`. **Update:** `j += lowbit(j)`. One-based inside, `lowbit(0) = 0` would loop forever.
- **Build in O(n):** `t[j] = a[j-1]`, then push `t[j]` to `j + lowbit(j)` in increasing order (2.7 to 3.1 times faster than n adds).
- **Fits:** operations with an inverse (sum, XOR, counts). **Maximum:** prefix query and raise-only updates. **Not minimum or gcd ranges.**
- **Range add, point query:** tree over the difference array. **Range add, range sum:** two trees, `i * B1.prefix(i) - B2.prefix(i)`.
- **k-th:** descent over powers of two, take a block while its total is `< k`; counts only. 2.0 to 2.2 times faster than binary search on `prefix`.
- **Inversions:** `i - prefix(rank + 1)` per element, 64-bit; with ranks. **LIS:** prefix maximum below the rank, then raise.
- **2D:** nested loops, `O(log R log C)`, `(R+1)(C+1)` words.
- **Measured (1M elements):** prefix 24 ns, update 50 to 59 ns; 8,700 to 8,900 and 3,700 to 4,600 times faster than the plain scan and the repaired prefix array. Plain merge sort and patience sorting beat the tree for the totals (2.8 to 3.0 and 5.0 to 5.5 times).

---

## Sources

- Peter M. Fenwick, "A new data structure for cumulative frequency tables", *Software: Practice and Experience* 24(3), 1994: the original description. Cited from memory; I did not re-read it.
- The range-add and range-sum construction, the O(n) build and the descent for the k-th element are standard in the competitive-programming literature (for example the cp-algorithms article "Fenwick Tree"); the code here is my own implementation of the described method, tested as above.
- Skiena, *The Algorithm Design Manual*, on binary indexed trees and cumulative frequencies, and CLRS on augmented data structures (both from memory).
- LeetCode, by name: Range Sum Query - Mutable, Range Sum Query 2D - Mutable, Count of Smaller Numbers After Self, Reverse Pairs, Count of Range Sum, Longest Increasing Subsequence, Find Median from Data Stream.
