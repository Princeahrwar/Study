# DSA 07.19: Prefix Sums, Sparse Tables and RMQ: Static Range Queries in O(1)

*Part of the [Trees plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Binary tree fundamentals](01_binary_tree_fundamentals.md) (the Cartesian tree of section 8 is a binary tree) and the [monotonic stack](../05_stacks_and_queues/02_monotonic_stack.md) (the same stack builds it). There is no companion demo file: every structure is listed in this file and every listing is tested code.*

By the end of this tutorial you can:

- answer range sums in O(1) with a **prefix-sum array** (1D and 2D), apply many range updates with a **difference array**, and use prefix sums with a hash map to count subarrays with a given sum or a sum divisible by `m`;
- build a **sparse table** and answer **range minimum, maximum, gcd, AND and OR** in O(1), and explain exactly why the same table gives **wrong** sums;
- return the **position** of the minimum with a defined tie rule;
- build a **disjoint sparse table**, which does O(1) queries for operations that are not idempotent (sums, products, string concatenation), and say when prefix sums make it unnecessary;
- cut the memory of a sparse table about five-fold with **block decomposition**, and say what the cut costs;
- build a **Cartesian tree** in O(n), and state the equivalence "range minimum = lowest common ancestor".

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 1,342,999 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: prefix sums, 2D prefix sums and difference arrays against naive loops (every range of 300 arrays, every rectangle of 200 grids, 300 sequences of random range updates); the three hash-map counters against brute force (400 arrays with negative numbers and zeros); the sparse table with five operations (min, max, gcd, AND, OR) on every range of every array of length 1 to 70, and on 3,000 random ranges of arrays up to 4,096 long; `ArgMinTable` with many ties; the disjoint sparse table with sum, min and **string concatenation** (an operation that is not commutative); `BlockMin` for block sizes 1, 2, 4, 16 and 64; the Cartesian tree (shape, heap order, depths, and every range against `ArgMinTable`); `longest_window` against brute force, including a difference that does not fit in an `int`; and out-of-range arguments for every class. Finally 87 hand-made mutants of the code were run against the tests (a wrong level, a shifted block, a lost tie rule, a missing correction term and so on): **85 were caught and 2 are equivalent changes** that cannot alter any output (listed after the common mistakes).

**Data and timing.** The array is **synthetic**: 1,000,000 random integers below 10^9 (below 1,000 for the sum comparison, so that no sum passes 2^31). The queries are generated first and are random: 10,000,000 **long** ones (a random start and a random length, 249,876 on average) and 10,000,000 **short** ones (length 1 to 64). Times are nanoseconds per query (or milliseconds per build) on a 2-core Intel Xeon VM at 2.80 GHz (Ubuntu 24.04, GCC 13.3, `-O2`), best of 5 repetitions for queries and best of 3 for builds, shown as **run 1 / run 2** (two complete runs of the program); only conclusions that hold in both runs are drawn. It is a **different VM from the one of Topic 18**, so the absolute numbers of the two tutorials must not be compared. Statements about systems practice are from general knowledge and say so.

---

## 1. Prefix sums, in one and two dimensions

Given an array `a`, define `P[0] = 0` and `P[i] = a[0] + ... + a[i-1]`. Then the sum of any range is a **difference of two prefixes**:

`a[l] + ... + a[r-1] = P[r] - P[l]`

The range is **half-open**, `[l, r)`, and `P` has `n + 1` entries. Both choices remove special cases: with the leading zero, a range that starts at 0 needs no `if`, and an empty range (`l == r`) is simply 0. Building `P` costs O(n), a query costs one subtraction, and the array is **immutable**: a single changed `a[i]` changes every `P[j]` with `j > i`. (When updates are mixed with queries the tool is a Fenwick tree, Topic 20.)

For `a = 3 1 4 1 5 9 2 6` the program prints `P = 0 3 4 8 9 14 23 25 31`, and `sum(2, 6) = P[6] - P[2] = 23 - 4 = 19`, which is `4 + 1 + 5 + 9`.

**Overflow.** The prefix of a million `int`s can pass 2^31, and signed overflow is undefined behaviour in C++, so `P` must be a wider type (`long long`). Unsigned counters are the pleasant exception: they wrap by definition, and a difference of two wrapped prefixes is still right as long as the true range sum fits (section 10).

**Two dimensions.** Let `P[i][j]` be the sum of the rectangle of all rows `< i` and columns `< j`. Each entry is built from its neighbours by inclusion and exclusion, `P[i+1][j+1] = g[i][j] + P[i][j+1] + P[i+1][j] - P[i][j]` (the two neighbours both contain the rectangle `P[i][j]`, so it is removed once). A query reverses the picture: the rectangle rows `[r0, r1)` by columns `[c0, c1)` is the whole prefix rectangle, minus the strip above it, minus the strip to its left, plus the corner that both strips removed.

**The difference array** is the inverse tool. To add `v` to every element of `[l, r)` store the change only at the ends, `d[l] += v` and `d[r] -= v`, in O(1); when all updates are in, one prefix-sum pass over `d` yields the final array. Many range updates followed by a single read is the pattern; if reads are interleaved with the updates it is not enough (Fenwick, Topic 20).

```cpp
// P[i] = a[0] + ... + a[i-1].  P has n + 1 entries, so a range is the half-open [l, r) and its sum is P[r] - P[l].
template <class T>
class PrefixSum {
    std::vector<T> p_;
public:
    explicit PrefixSum(const std::vector<T>& a) : p_(a.size() + 1, T(0)) {
        for (std::size_t i = 0; i < a.size(); ++i) p_[i + 1] = p_[i] + a[i];
    }
    std::size_t size() const { return p_.size() - 1; }
    T sum(std::size_t l, std::size_t r) const {                    // a[l] + ... + a[r-1]; an empty range is allowed
        if (l > r || r >= p_.size()) throw std::out_of_range("PrefixSum::sum");
        return p_[r] - p_[l];
    }
};

// The same idea in two dimensions: P[i][j] = the sum of the rectangle of rows < i and columns < j.
class Prefix2D {
    std::size_t rows_, cols_;
    std::vector<long long> p_;                                     // (rows + 1) x (cols + 1), row-major
    long long& at(std::size_t i, std::size_t j) { return p_[i * (cols_ + 1) + j]; }
    long long at(std::size_t i, std::size_t j) const { return p_[i * (cols_ + 1) + j]; }
public:
    explicit Prefix2D(const std::vector<std::vector<int>>& g)
        : rows_(g.size()), cols_(g.empty() ? 0 : g[0].size()), p_((rows_ + 1) * (cols_ + 1), 0) {
        for (std::size_t i = 0; i < rows_; ++i)
            for (std::size_t j = 0; j < cols_; ++j)
                at(i + 1, j + 1) = g[i][j] + at(i, j + 1) + at(i + 1, j) - at(i, j);       // add the two neighbours, remove the overlap
    }
    long long sum(std::size_t r0, std::size_t c0, std::size_t r1, std::size_t c1) const {   // rows [r0, r1) x columns [c0, c1)
        if (r0 > r1 || c0 > c1 || r1 > rows_ || c1 > cols_) throw std::out_of_range("Prefix2D::sum");
        return at(r1, c1) - at(r0, c1) - at(r1, c0) + at(r0, c0);                           // whole, minus two strips, plus the corner removed twice
    }
};

// The inverse tool.  Many range updates "add v to a[l..r)", then one pass that reads the final array.
class DiffArray {
    std::size_t n_;
    std::vector<long long> d_;                                     // d[i] = a[i] - a[i-1]; one extra slot so that r == n is legal
public:
    explicit DiffArray(std::size_t n) : n_(n), d_(n + 1, 0) {}
    void add(std::size_t l, std::size_t r, long long v) {          // a[l..r) += v, in O(1)
        if (l > r || r > n_) throw std::out_of_range("DiffArray::add");
        d_[l] += v;
        d_[r] -= v;
    }
    std::vector<long long> finish() const {                        // the prefix sum of d is the array itself
        std::vector<long long> a(n_);
        long long run = 0;
        for (std::size_t i = 0; i < n_; ++i) { run += d_[i]; a[i] = run; }
        return a;
    }
};
```

The program prints the 2D table of `1 2 3 4 / 5 6 7 8 / 9 10 11 12`, and the rectangle of rows 1 to 2 and columns 1 to 2 (the elements `6 7 10 11`) is read from four of its entries:

| i | P[i][0] | P[i][1] | P[i][2] | P[i][3] | P[i][4] |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 0 | 0 |
| 1 | 0 | 1 | 3 | 6 | 10 |
| 2 | 0 | 6 | 14 | 24 | 36 |
| 3 | 0 | 15 | 33 | 54 | 78 |

`sum(1, 1, 3, 3) = P[3][3] - P[1][3] - P[3][1] + P[1][1] = 54 - 6 - 15 + 1 = 34`, and `6 + 7 + 10 + 11 = 34`. For the difference array of length 6 with `add(1, 4, 2)` and `add(3, 6, 1)` (note that `r = n = 6` is legal, which is why `d` has `n + 1` slots) the final array is `0 2 2 3 1 1`.

---

## 2. Prefix sums with a hash map: counting subarrays

A subarray `(i, j]` has sum `P[j] - P[i]`. So "how many subarrays have sum `k`" asks, for each `j`, how many **earlier** prefixes equal `P[j] - k`: keep a hash map from prefix value to count, and look up before inserting. Nothing here needs the values to be positive, which is why it replaces the two-pointer method when negatives are allowed.

```cpp
// Number of subarrays with sum exactly k.  A subarray (i, j] has sum P[j] - P[i], so for each j we need the number of
//    earlier prefixes equal to P[j] - k.  P[0] = 0 is "seen" once before the loop.  Values may be negative.
long long count_sum_k(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, long long> seen{{0, 1}};
    long long run = 0, count = 0;
    for (int x : a) {
        run += x;
        auto it = seen.find(run - k);
        if (it != seen.end()) count += it->second;
        ++seen[run];
    }
    return count;
}

// Number of subarrays whose sum is divisible by m (m > 0): two prefixes with the same remainder.
long long count_div_by(const std::vector<int>& a, int m) {
    std::vector<long long> freq(m, 0);
    freq[0] = 1;
    long long run = 0, count = 0;
    for (int x : a) {
        run = ((run + x) % m + m) % m;                             // C++ % keeps the sign of the left operand: fix it
        count += freq[run]++;                                      // pairs with every earlier prefix of the same remainder
    }
    return count;
}

// Length of the longest subarray with sum exactly k (0 if none): keep the FIRST index at which each prefix appeared.
std::size_t longest_sum_k(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, std::size_t> first{{0, 0}};      // prefix value -> number of elements consumed when first seen
    long long run = 0;
    std::size_t best = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        run += a[i];
        auto it = first.find(run - k);
        if (it != first.end()) best = std::max(best, i + 1 - it->second);
        first.emplace(run, i + 1);                                 // emplace does not overwrite: the first occurrence stays
    }
    return best;
}
```

Three details decide whether this is right:

- **The seed `{0: 1}`** stands for the empty prefix `P[0]`. Without it every subarray that starts at index 0 is missed.
- **Look up first, insert after.** With `k = 0` the key looked up is `run` itself; if the current prefix were inserted first, every position would pair with itself and count an empty subarray.
- **The negative remainder.** In C++ `-3 % 5` is `-3`, not 2, so `(run + x) % m` can be negative and would index `freq` out of range. `((run + x) % m + m) % m` fixes it.

For the longest subarray the opposite rule holds: store the **first** index at which a prefix value appeared (`emplace` does not overwrite), because the earliest start gives the longest subarray.

The program gives `count_sum_k([1,2,1,2,1], 3) = 4`: the subarrays `[1,2]`, `[2,1]`, `[1,2]` and `[2,1]`. For `count_div_by([4,5,0,-2,-3,1], 5) = 7` the prefix remainders (with the leading 0) are `0 4 4 4 2 4 0`: the value 4 occurs 4 times, which makes `C(4,2) = 6` pairs, the value 0 occurs twice, which makes 1 pair, and the value 2 occurs once, which makes none: `6 + 1 = 7`. That is what the line `count += freq[run]++` computes, one pair at a time.

---

## 3. The range-minimum problem

**Static RMQ:** given an array `a` that never changes, answer many queries "what is the minimum of `a[l..r)`?". Two kinds of cost matter, the cost to build and the cost per query; the design space is:

| Structure | Build | Query | Memory | Section |
|---|---|---|---|---|
| Scan the range | none | O(length) | none | 4 |
| Sparse table | O(n log n) | O(1) | n log n | 4 |
| Sparse table over blocks | O(n) | O(B) in the worst case | about 3n + (n / B) log n | 7 |
| Cartesian tree + LCA walk | O(n) | O(depth) | 4n | 8 |
| Segment tree (Topic 21) | O(n) | O(log n) | 2n | later; also supports updates |

Prefix sums are the special case of this problem for an operation that has an **inverse** (subtract the prefix you do not want). `min` has no inverse, which is why the range-minimum problem needs its own structures.

---

## 4. The sparse table

The idea is to precompute the answer for every range whose length is a **power of two**. Level `j` holds, for every start `i`, the minimum of the `2^j` elements `a[i .. i + 2^j - 1]`; level `j` is built from level `j - 1` by combining two halves, so the whole table costs O(n log n) time and space.

A query `[l, r)` of length `L` takes `j = floor(log2 L)`, so that `2^j <= L < 2^(j+1)`. The two blocks `[l, l + 2^j)` and `[r - 2^j, r)` each fit inside the range, and since `2 * 2^j > L` together they **cover** it; they overlap in the middle. The answer is the `min` of the two table entries: two reads and no loop.

```cpp
inline int floor_log2(std::size_t x) { return 63 - __builtin_clzll(x); }          // x > 0

struct MinOp { template <class T> T operator()(const T& a, const T& b) const { return b < a ? b : a; } };
struct MaxOp { template <class T> T operator()(const T& a, const T& b) const { return a < b ? b : a; } };
struct GcdOp { template <class T> T operator()(const T& a, const T& b) const { return std::gcd(a, b); } };
struct SumOp { template <class T> T operator()(const T& a, const T& b) const { return a + b; } };   // NOT idempotent: see below

// t_[j][i] = op over the 2^j elements a[i .. i + 2^j - 1].  Level j has n - 2^j + 1 entries.
// Op must be associative, commutative and IDEMPOTENT (op(x, x) == x), because a query covers its range with two
//    overlapping blocks and the overlap is counted twice.
template <class T, class Op>
class SparseTable {
    std::vector<std::vector<T>> t_;
    Op op_;
    friend struct TestAccess;                                      // lets the trace program print the table
public:
    explicit SparseTable(const std::vector<T>& a = {}, Op op = Op()) : op_(op) {
        std::size_t n = a.size();
        if (n == 0) return;
        t_.push_back(a);
        for (std::size_t j = 1; (std::size_t(1) << j) <= n; ++j) {
            std::size_t half = std::size_t(1) << (j - 1), len = n - (std::size_t(1) << j) + 1;
            t_.emplace_back(len);
            for (std::size_t i = 0; i < len; ++i) t_[j][i] = op_(t_[j - 1][i], t_[j - 1][i + half]);
        }
    }
    std::size_t size() const { return t_.empty() ? 0 : t_[0].size(); }
    std::size_t levels() const { return t_.size(); }
    std::size_t bytes() const { std::size_t b = 0; for (auto& v : t_) b += v.size() * sizeof(T); return b; }
    T query(std::size_t l, std::size_t r) const {                  // the range [l, r), l < r
        if (l >= r || r > size()) throw std::out_of_range("SparseTable::query");
        int j = floor_log2(r - l);                                 // the largest power of two that fits
        return op_(t_[j][l], t_[j][r - (std::size_t(1) << j)]);    // [l, l + 2^j) and [r - 2^j, r) cover [l, r) and may overlap
    }
};
```

The overlap is why the operation must be **idempotent**, `op(x, x) = x`, besides associative and commutative. `min`, `max`, `gcd`, bitwise AND and OR qualify (an element counted twice changes nothing). **Sum, XOR and product do not**, and the table does not fail loudly, it answers wrongly.

For `a = 3 1 4 1 5 9 2 6` the minimum table is:

| Level | Entries |
|---|---|
| 0 (length 1) | 3 1 4 1 5 9 2 6 |
| 1 (length 2) | 1 1 1 1 5 2 2 |
| 2 (length 4) | 1 1 1 1 2 |
| 3 (length 8) | 1 |

`min[1, 7)` has length 6, `j = 2`, and reads level 2 at `1` (covers `a[1..4]`, value 1) and at `7 - 4 = 3` (covers `a[3..6]`, value 1): the answer is 1. `min[4, 8)` has length 4, so the two blocks coincide and it reads `2` twice. `min[2, 5)` has length 3, `j = 1`, and reads level 1 at 2 and at 3.

The same table built with `SumOp` is:

| Level | Entries |
|---|---|
| 0 | 3 1 4 1 5 9 2 6 |
| 1 | 4 5 5 6 14 11 8 |
| 2 | 9 11 19 17 22 |
| 3 | 31 |

and `query(1, 7)` reads `11 + 17 = 28`, while the true sum is `22`: the elements `a[3..4] = 1 + 5 = 6` were counted twice. Worse, **every** query is wrong: even a single element, `query(0, 1)`, returns `op(a[0], a[0]) = 6` instead of 3, because both blocks are the same block. (The tests check these three wrong values, so the claim is not just a sentence.)

**Cost.** For the million integers the table has **20 levels and 75,805,780 bytes (75.8 MB, 19.0 times the array)** and takes **64.2 / 70.9 ms** to build; level `j` has `n - 2^j + 1` entries, which the code uses to save a little over a full `n log n` rectangle. A query is two reads from far apart in a 76 MB table, so it is dominated by cache misses rather than by arithmetic: **28.8 / 38.6 ns** for long ranges and **25.8 / 29.9 ns** for short ones. The plain scan, for comparison, takes **153,218 / 152,998 ns** for a long range (a quarter of a million elements), so the table is **4,000 to 5,300 times faster** there. For short ranges the picture is different: the scan of 1 to 64 elements takes **35.9 / 36.2 ns**, only **1.2 to 1.4 times slower** than the table. A sparse table pays off when queries are long or very many, not for short windows.

**Details that go wrong.** The level is `floor_log2(L)`, written with a count-leading-zeros instruction; computing it with a floating-point `log2` invites an off-by-one at exact powers of two. An array of `n = 2^k` elements needs `k + 1` levels, not `k`. The second block starts at `r - 2^j`, not `r - 2^j + 1` (the range is half-open).

**Any update breaks it.** Changing `a[i]` invalidates up to `2^j` entries on level `j`, O(n) entries in total; with updates use a segment tree (Topic 21).

---

## 5. The position of the minimum

Interviews usually want the **index** of the minimum, not its value. Store indices in the table and compare the values they point to. The tie rule matters: if two positions hold the same minimum, "the" answer is ambiguous, and a table that picks arbitrarily returns different indices from different implementations. Choosing **the smaller index** is still associative, commutative and idempotent (it is the minimum under the order "by value, then by index"), so the two-block query stays valid.

```cpp
// RMQ usually wants the POSITION of the minimum.  Store indices and compare the values they point to; ties go to the
//    smaller index, which is still associative, commutative and idempotent, so the same two-block query works.
template <class T>
class ArgMinTable {
    std::vector<T> a_;
    std::vector<std::vector<int>> t_;
    int better(int i, int j) const { return (a_[j] < a_[i] || (a_[j] == a_[i] && j < i)) ? j : i; }
public:
    explicit ArgMinTable(std::vector<T> a) : a_(std::move(a)) {
        std::size_t n = a_.size();
        if (n == 0) return;
        t_.emplace_back(n);
        std::iota(t_[0].begin(), t_[0].end(), 0);
        for (std::size_t j = 1; (std::size_t(1) << j) <= n; ++j) {
            std::size_t half = std::size_t(1) << (j - 1), len = n - (std::size_t(1) << j) + 1;
            t_.emplace_back(len);
            for (std::size_t i = 0; i < len; ++i) t_[j][i] = better(t_[j - 1][i], t_[j - 1][i + half]);
        }
    }
    std::size_t argmin(std::size_t l, std::size_t r) const {      // index of the leftmost minimum in [l, r)
        if (l >= r || r > a_.size()) throw std::out_of_range("ArgMinTable::argmin");
        int j = floor_log2(r - l);
        return better(t_[j][l], t_[j][r - (std::size_t(1) << j)]);
    }
};
```

For the array above, `argmin[0, 8) = 1`, although the value 1 also sits at index 3: the leftmost wins. `argmin[2, 8) = 3` and `argmin[4, 8) = 6`.

---

## 6. The disjoint sparse table: sums, products and other non-idempotent operations

If the two blocks of a query must **not** overlap, they have to be the two halves of the range, split at a point both queries agree on. The **disjoint sparse table** arranges that. Pad the array to a power of two `N`. At level `k` cut it into blocks of `2^(k+1)` elements, with a middle point `mid` in each. For every position left of `mid` store the combined value from that position **up to `mid - 1`**, and for every position from `mid` on store the combined value from `mid` **up to that position**.

A query `[l, r]` (inclusive here) with `l != r`: the highest bit in which `l` and `r` differ is `k = floor_log2(l ^ r)`. Above that bit they agree, so they are in the same block of size `2^(k+1)`; at bit `k` `l` has 0 and `r` has 1, so `l` is in the left half and `r` in the right half. The range is therefore `[l, mid-1]` followed by `[mid, r]`, the two pieces **touch** and do not overlap, and the answer is `op(t[k][l], t[k][r])`, in that order. `l == r` is a single element. Because the pieces are combined left first, the operation only has to be associative, not commutative.

```cpp
// Sums (and any associative but NOT idempotent operation) get O(1) queries from a different table.  Pad the array to a power of
//    two N.  At level k the array is cut into blocks of 2^(k+1); from the middle of each block the table stores the running
//    result going LEFT (t[k][i] = op over a[i..mid-1]) and going RIGHT (t[k][i] = op over a[mid..i]).  A query [l, r] whose two
//    ends are in different halves of one block at level k = floor_log2(l ^ r) is op(t[k][l], t[k][r]): the pieces TOUCH, never overlap.
template <class T, class Op>
class DisjointSparse {
    std::size_t n_;
    std::vector<T> a_;
    std::vector<std::vector<T>> t_;
    Op op_;
    friend struct TestAccess;
public:
    explicit DisjointSparse(const std::vector<T>& a, Op op = Op()) : n_(a.size()), op_(op) {
        if (n_ == 0) return;
        std::size_t N = 1;
        int L = 0;
        while (N < n_) { N <<= 1; ++L; }
        a_ = a;
        a_.resize(N);                                              // padding is only ever read by blocks that no valid query uses
        t_.assign(L, std::vector<T>(N));
        for (int k = 0; k < L; ++k) {
            std::size_t half = std::size_t(1) << k;
            for (std::size_t s = 0; s < N; s += 2 * half) {
                std::size_t mid = s + half;
                t_[k][mid - 1] = a_[mid - 1];
                for (std::size_t i = mid - 1; i-- > s;) t_[k][i] = op_(a_[i], t_[k][i + 1]);              // leftwards from the middle
                t_[k][mid] = a_[mid];
                for (std::size_t i = mid + 1; i < s + 2 * half; ++i) t_[k][i] = op_(t_[k][i - 1], a_[i]); // rightwards from the middle
            }
        }
    }
    std::size_t bytes() const { std::size_t b = a_.size() * sizeof(T); for (auto& v : t_) b += v.size() * sizeof(T); return b; }
    T query(std::size_t l, std::size_t r) const {                  // the range [l, r), l < r
        if (l >= r || r > n_) throw std::out_of_range("DisjointSparse::query");
        --r;                                                       // now inclusive
        if (l == r) return a_[l];                                  // l ^ r == 0 has no highest bit
        int k = floor_log2(l ^ r);                                 // the highest bit where l and r differ = the level whose middle separates them
        return op_(t_[k][l], t_[k][r]);                            // order matters for a non-commutative op: left piece first
    }
};
```

The tables for `a = 3 1 4 1 5 9 2 6` and the sum are:

| Level | Entries | Block size |
|---|---|---|
| 0 | 3 1 4 1 5 9 2 6 | 2 |
| 1 | 4 1 4 5 14 9 2 8 | 4 |
| 2 | 9 6 5 1 5 14 16 22 | 8 |

Take `[1, 7)`, that is `l = 1`, `r = 6` inclusive. `1 ^ 6 = 7`, so `k = 2` (block of 8, middle at 4). `t[2][1] = 6` is `1 + 4 + 1` (positions 1 to 3) and `t[2][6] = 16` is `5 + 9 + 2` (positions 4 to 6): `6 + 16 = 22`, the true sum, where the overlapping sparse table said 28. `[2, 6)` gives `l = 2`, `r = 5`, `2 ^ 5 = 7`, `k = 2`: `t[2][2] + t[2][5] = 5 + 14 = 19`. `[5, 6)` is one element: 9.

The tests use the table with **string concatenation**, which is associative but not commutative, to prove that the order of the two pieces is right.

**Is it worth it for sums?** No, and the measurements say so. For a million values the disjoint table takes **88,080,384 bytes (88.1 MB)** and **66.6 / 66.9 ms** to build, against **8,000,008 bytes** and **2.1 / 1.8 ms** for the prefix array (that array holds `long long`, the table `int`, so the gap is if anything understated). A query takes **19.3 / 29.0 ns** (long) and **17.2 / 21.3 ns** (short) against **8.1 / 9.5** and **7.9 / 9.4 ns** for the prefix array: **2.2 to 3.1 times slower** and 11 times the memory. The table earns its place only for an associative operation that has **no inverse and is not idempotent**: a product of matrices, a composition of functions, a product modulo a number where some elements are not invertible, concatenation of strings or hashes.

---

## 7. Cutting the memory: block decomposition

The sparse table spends `log n` words per element because it stores answers for ranges of every power-of-two length at every start. Most of that is redundant for a query that spans many blocks. The usual fix is to cut the array into blocks of `B` elements and keep three things: the minimum of every **prefix of a block**, the minimum of every **suffix of a block**, and a sparse table over the **block minima** (only `n / B` entries, so the `log n` factor now applies to a much smaller `n`).

A query `[l, r)` that touches two or more blocks is the suffix of the first block, the prefix of the last, and the blocks in between: three reads. A query inside one block falls back to scanning at most `B` elements.

```cpp
// Cut the array into blocks of B.  Keep, per element, the minimum from the start of its block (pre) and to the end of its block
//    (suf), and a sparse table over the block minima.  A query is a suffix of one block, whole blocks in between, and a prefix of
//    another; inside a single block it is a plain scan of at most B elements.
template <int B>
class BlockMin {
    static_assert(B >= 1, "block size");
    std::vector<int> a_, pre_, suf_;
    SparseTable<int, MinOp> top_;
public:
    explicit BlockMin(std::vector<int> a) : a_(std::move(a)), pre_(a_.size()), suf_(a_.size()) {
        std::size_t n = a_.size();
        std::vector<int> mins;
        for (std::size_t s = 0; s < n; s += B) {
            std::size_t e = std::min(n, s + B);
            pre_[s] = a_[s];
            for (std::size_t i = s + 1; i < e; ++i) pre_[i] = std::min(pre_[i - 1], a_[i]);
            suf_[e - 1] = a_[e - 1];
            for (std::size_t i = e - 1; i-- > s;) suf_[i] = std::min(suf_[i + 1], a_[i]);
            mins.push_back(pre_[e - 1]);                           // the block's minimum is its last prefix minimum
        }
        top_ = SparseTable<int, MinOp>(mins);
    }
    std::size_t bytes() const { return 3 * a_.size() * sizeof(int) + top_.bytes(); }
    int query(std::size_t l, std::size_t r) const {                // the range [l, r), l < r
        if (l >= r || r > a_.size()) throw std::out_of_range("BlockMin::query");
        std::size_t last = r - 1, bl = l / B, br = last / B;
        if (bl == br) return *std::min_element(a_.begin() + l, a_.begin() + r);
        int m = std::min(suf_[l], pre_[last]);
        if (br - bl > 1) m = std::min(m, top_.query(bl + 1, br));
        return m;
    }
};
```

For a million integers (the sparse table was 75.8 MB, 19.0 times the array):

| Block size | Bytes | Times the array | Build ms | Query ns, long | Query ns, short |
|---|---|---|---|---|---|
| 16 | 15,737,924 | 3.93 | 3.6 / 5.4 | 32.4 / 32.4 | 37.8 / 35.5 |
| 32 | 13,743,992 | 3.44 | 3.5 / 3.4 | 23.5 / 42.1 | 37.2 / 36.5 |
| 64 | 12,809,524 | 3.20 | 2.5 / 3.3 | 19.2 / 28.0 | 41.3 / 38.1 |
| sparse table | 75,805,780 | 19.0 | 64.2 / 70.9 | 28.8 / 38.6 | 25.8 / 29.9 |

The blocked version uses **4.8 to 5.9 times less memory** and builds **13 to 26 times faster**; its long queries are in the same range as the sparse table's (neither is consistently faster, because both are dominated by cache misses and the smaller structure misses less), and its short queries are **1.2 to 1.6 times slower**, because they scan inside a block. All three agree with the sparse table on all 20,000,000 queries (the program checks the sums of the answers).

This is also the first half of the classic **O(n) memory, O(1) query** RMQ: handle the in-block queries with lookup tables indexed by the *shape* of the block (the Fischer and Heun construction, from memory) instead of a scan. The tutorial stops at the scan.

---

## 8. The Cartesian tree: range minimum is a lowest common ancestor

The **min-Cartesian tree** of an array has the minimum as its root; the part of the array to its left forms the left subtree and the part to its right forms the right subtree, recursively. Two facts make it the structure behind RMQ:

1. The in-order traversal of the tree is the array order, `0, 1, ..., n-1`.
2. The minimum of `a[l..r]` is the **lowest common ancestor** of the nodes `l` and `r`: the LCA is the first node, from the root, that separates `l` from `r`, so it is the smallest element between them.

It is built in O(n) with the stack of "previous smaller element" (Topic 05): keep the rightmost path on a stack; a new element pops everything greater than itself, adopts the last popped node as its left child, and becomes the right child of the node left on top.

```cpp
// The min-Cartesian tree of an array: the root is the minimum, its left subtree is the Cartesian tree of everything to its left,
//    its right subtree the one of everything to its right.  Built in O(n) with a stack, the way "previous smaller element" is.
//    Equal values: the earlier one is the ancestor, so the LCA of two positions is the LEFTMOST minimum between them.
class CartesianTree {
    std::vector<int> l_, r_, par_, depth_;
    int root_ = -1;
public:
    explicit CartesianTree(const std::vector<int>& a)
        : l_(a.size(), -1), r_(a.size(), -1), par_(a.size(), -1), depth_(a.size(), 0) {
        std::vector<int> st;                                       // indices on the rightmost path, values increasing
        for (int i = 0; i < (int)a.size(); ++i) {
            int last = -1;
            while (!st.empty() && a[st.back()] > a[i]) { last = st.back(); st.pop_back(); }   // strictly greater: equal values stay
            if (last >= 0) { l_[i] = last; par_[last] = i; }       // the popped chain hangs under i on the left
            if (!st.empty()) { r_[st.back()] = i; par_[i] = st.back(); }
            st.push_back(i);
        }
        if (!st.empty()) root_ = st[0];
        std::vector<int> todo;
        if (root_ >= 0) todo.push_back(root_);
        while (!todo.empty()) {                                    // depths by an explicit stack: the tree can be n deep
            int u = todo.back();
            todo.pop_back();
            for (int c : {l_[u], r_[u]})
                if (c >= 0) { depth_[c] = depth_[u] + 1; todo.push_back(c); }
        }
    }
    int root() const { return root_; }
    int left(int i) const { return l_[i]; }
    int right(int i) const { return r_[i]; }
    int parent(int i) const { return par_[i]; }
    int depth(int i) const { return depth_[i]; }
    int lca(int u, int v, long* steps = nullptr) const {           // walk the deeper one up until they meet
        while (u != v) {
            if (depth_[u] >= depth_[v]) u = par_[u]; else v = par_[v];
            if (steps) ++*steps;
        }
        return u;
    }
    int argmin(std::size_t l, std::size_t r, long* steps = nullptr) const {   // RMQ = LCA of the two end positions
        if (l >= r || r > l_.size()) throw std::out_of_range("CartesianTree::argmin");
        return lca((int)l, (int)(r - 1), steps);
    }
};
```

Equal values: the loop pops only **strictly** greater values, so an equal earlier element stays on the stack and becomes an ancestor of the later one. The LCA therefore reports the **leftmost** minimum, the same rule as in section 5, and the tests compare the two on every range.

For `a = 3 1 4 1 5 9 2 6` the tree is:

| Node | Value | Left | Right | Parent | Depth |
|---|---|---|---|---|---|
| 0 | 3 | none | none | 1 | 1 |
| 1 | 1 | 0 | 3 | none | 0 |
| 2 | 4 | none | none | 3 | 2 |
| 3 | 1 | 2 | 6 | 1 | 1 |
| 4 | 5 | none | 5 | 6 | 3 |
| 5 | 9 | none | none | 4 | 4 |
| 6 | 2 | 4 | 7 | 3 | 2 |
| 7 | 6 | none | none | 6 | 3 |

The root is node 1, the first minimum. `argmin[1, 7)` is the LCA of nodes 1 and 6: node 6 has depth 2, node 1 depth 0, so the walk moves 6 up to 3 and then to 1: 2 steps, answer 1. `argmin[4, 8)` is the LCA of 4 and 7: it takes 2 steps and gives 6 (value 2). `argmin[5, 8)` takes 3 steps and gives 6 as well.

**The equivalence runs both ways.** A Cartesian tree turns RMQ into LCA, and the Euler tour of a tree (Topic 24) turns LCA into RMQ over the depths. They are one problem, which is why "LCA at scale" (Topic 25) will reuse the sparse table of section 4. Its depths change by exactly 1 between neighbours (the "plus or minus one" RMQ), which is what allows the O(n) memory and O(1) query solutions (Farach-Colton and Bender, from memory).

**Cost of the walk.** The query is as slow as the tree is deep. On a random array of a million elements the tree is shallow, **maximum depth 49**, and a long query took **43.49 steps** on average (11.28 for short ones), **332.1 / 363.7 ns**: **9.4 to 11.5 times slower** than the sparse table. On a **sorted** array the tree is a path, and 20,000 random queries on 20,000 elements took **4,999 steps** on average. The build is cheap (16,000,000 bytes, **28.7 / 34.2 ms**), and this is the structure to know for its *shape* (a treap is the Cartesian tree of its keys ordered by random priorities, Topic 14), and to read next to the monotonic stack.

---

## 9. An application: the longest window with a bounded spread

*Longest subarray in which the maximum minus the minimum is at most `k`.* If `[l, r)` is valid, every sub-range is valid too (the spread of a part cannot exceed the spread of the whole), so as `r` grows the left end `l` only moves right: two pointers. Each test needs the maximum and the minimum of `[l, r)`, which two sparse tables give in O(1).

```cpp
// Longest subarray whose maximum minus minimum is at most k.  If [l, r) is valid then so is every sub-range, so the left end
//    only moves right: two pointers, and each test is two O(1) sparse-table queries.
std::size_t longest_window(const std::vector<int>& a, long long k) {
    SparseTable<int, MinOp> lo(a);
    SparseTable<int, MaxOp> hi(a);
    std::size_t best = 0, l = 0;
    for (std::size_t r = 1; r <= a.size(); ++r) {
        while ((long long)hi.query(l, r) - lo.query(l, r) > k) ++l;
        best = std::max(best, r - l);
    }
    return best;
}
```

The subtraction is done in `long long`: `2,000,000,000 - (-2,000,000,000)` does not fit in an `int`, and a test checks this. For `3 1 4 1 5 9 2 6` the lengths for `k = 0, 2, 3, 8` are `1, 2, 4, 8` (for `k = 3` the window is `3 1 4 1`).

An honest comparison: this problem is also solved in O(n) time and O(window) memory by two **monotonic deques** (Topic 05), which hold the running maximum and minimum. The sparse-table version is simpler to get right and also answers *arbitrary* range queries afterwards; the deque version is leaner. Choose by whether you need the table for anything else.

---

## 10. Choosing

| Situation | Use |
|---|---|
| Range sums, no updates | Prefix sums (section 1) |
| Many range additions, read once | Difference array |
| Range sums with point updates | Fenwick tree (Topic 20) |
| Range min, max, gcd, AND, OR, static, many queries | Sparse table (section 4), or the blocked version if memory matters (section 7) |
| Range min with updates | Segment tree (Topic 21) |
| Associative, no inverse, not idempotent (products, composition), static | Disjoint sparse table (section 6) |
| Index of the minimum, static | `ArgMinTable` with a stated tie rule, or the Cartesian tree |
| Few queries, or short ranges | A plain scan: the measurement shows it within 1.2 to 1.4 times of the table for ranges up to 64 |

**Where this appears in systems work** (general knowledge, not measured here). Cumulative hardware and kernel counters (bytes received on an interface, CPU time in `/proc/stat`) are prefix sums of an unseen stream: a rate is `(P[t2] - P[t1]) / dt`, and because the counters are unsigned and wrap modulo a power of two, the unsigned difference is correct across one wraparound. A difference array is the usual way to answer "how many intervals overlap at each time" (reservations, airtime or bandwidth allocations): add `+1` at the start and `-1` at the end of each interval, then sweep.

---

## 11. Common mistakes

1. **Mixing closed and half-open ranges.** This tutorial uses `[l, r)` throughout, so `sum(l, r) = P[r] - P[l]`. Many online judges use inclusive ranges; convert at the boundary and nowhere else.
2. **A prefix array without the leading zero**, which needs a special case for `l = 0`, and gets it wrong.
3. **Prefix sums in `int`.** Signed overflow is undefined behaviour; use `long long`, and check the bound (`n * max |a|`).
4. **`count_sum_k` without the seed `{0: 1}`**, or inserting the current prefix before the lookup (counts empty subarrays when `k = 0`).
5. **A negative remainder.** `-3 % 5` is `-3` in C++; add `m` and take the remainder again.
6. **Overwriting the first occurrence** in the longest-subarray map: the longest subarray starts at the earliest index.
7. **A sparse table over sum, XOR or product.** It returns wrong answers, silently, for every query, even single elements (section 4).
8. **A floating-point `log2` for the level**, or `k` levels for `2^k` elements: off by one.
9. **The second block at `r - 2^j + 1`** in a half-open range (it must be `r - 2^j`).
10. **Using a sparse table when the array changes.** Rebuilding costs O(n log n) per update; use a segment tree.
11. **An undefined tie rule for argmin.** Say which index wins, and make the combine function respect it (section 5).
12. **A 2D prefix without the corner term**, or with rows and columns swapped.
13. **A difference array with `n` slots**, so that `add(l, n, v)` writes out of range.
14. **Walking a Cartesian tree on sorted input.** The tree is a path; the walk is O(n) per query, and a recursive build or traversal would overflow the stack. The code here uses explicit stacks.
15. **Believing the table is always the fast choice.** For ranges of up to 64 elements a plain scan was within 1.2 to 1.4 times of it (section 4).

**The mutants that survive**, each checked by hand: (1) `depth_[u] > depth_[v]` instead of `>=` in `CartesianTree::lca`: when the depths are equal and the nodes differ, it makes no difference which one moves up first, and the walk meets at the same node; (2) dropping the tie clause `(a_[j] == a_[i] && j < i)` from `ArgMinTable::better`: both callers (the build and the query) always pass the candidate of the **left** block first, and the leftmost minimum of the left block is never to the right of the leftmost minimum of the right block, so the clause never fires. It is kept because it makes `better` a true commutative operation, which is what the argument for the two-block query assumes.

---

## 12. Questions to answer out loud

1. Why does `P` have `n + 1` entries? What is `sum(l, l)`?
2. How do you answer a rectangle sum in O(1) after O(rows * columns) preprocessing? Which term is added back, and why?
3. What does a difference array do, and why does it have `n + 1` slots?
4. How do prefix sums with a hash map count the subarrays with sum `k`? Why look up before inserting? What if the array has negatives?
5. Why are two equal prefix remainders a subarray divisible by `m`? How many subarrays do `f` equal remainders give?
6. How is a sparse table built, and what does level `j` hold? What is its memory?
7. Why do two overlapping blocks cover every query? What does the operation have to satisfy, and which of min, max, gcd, sum, XOR, AND qualify?
8. Why is a sparse table over sums wrong for **every** query, including a single element?
9. How does a disjoint sparse table avoid the overlap? What does it cost, and when is it better than prefix sums?
10. What does block decomposition keep, and what does it give up compared with the full table?
11. How is a Cartesian tree built in O(n)? Why is range minimum the LCA? What does an equal value do to the tree?
12. Why is the LCA walk slow on a sorted array? What would fix it?

---

## 13. Problems

**A. Prefix sums.** For `3 1 4 1 5 9 2 6` find the sum of the elements at positions 2 to 5 (inclusive) and say which two entries of `P` give it.

**B. A rectangle.** For the grid `1 2 3 4 / 5 6 7 8 / 9 10 11 12` find the sum of the sub-grid of rows 1 to 2 and columns 1 to 2 from the table of section 1.

**C. A difference array.** Start from six zeros, add 2 to positions 1 to 3, then add 1 to positions 3 to 5 (inclusive). What is the array, and what does `d` hold before the final pass?

**D. Counting.** How many subarrays of `1 2 1 2 1` have sum 3? How many subarrays of `4 5 0 -2 -3 1` have a sum divisible by 5?

**E. A sparse table.** Build the minimum table of `3 1 4 1 5 9 2 6`. Which two entries answer `[2, 5)`?

**F. Why not sums.** On the same array, what does the sum table return for `[1, 7)`, and what is the true sum? Which elements were counted twice?

**G. A disjoint table.** Using the table of section 6, answer the sums of `[2, 6)` and `[0, 8)`.

**H. A tree.** Draw the min-Cartesian tree of `2 1 3 1 2`. Where does the second 1 go, and which index does `argmin[0, 5)` return?

**I. Choose.** Pick a structure for each: (a) a static array of 10^6 integers and 10^7 range-minimum queries with 100 MB of memory; (b) the same array with 10^5 point updates mixed with the queries; (c) the product of a range of 2x2 matrices, static; (d) "add `v` to a range" 10^6 times, then print the array once; (e) the sum of a rectangle of a 5000 by 5000 image, 10^6 queries.

---

## Answers

**A.** Positions 2 to 5 are `4 + 1 + 5 + 9 = 19`: in half-open form `[2, 6)`, which is `P[6] - P[2] = 23 - 4 = 19`. Program output: 19.

**B.** `P[3][3] - P[1][3] - P[3][1] + P[1][1] = 54 - 6 - 15 + 1 = 34`, which is `6 + 7 + 10 + 11`. Program output: 34.

**C.** Positions 1 to 3 get +2 and positions 3 to 5 get +1, so the array is `0 2 2 3 1 1` (position 3 gets both). Before the pass `d = 0 2 0 1 -2 0 -1`: `+2` at 1, `-2` at 4, `+1` at 3, `-1` at 6. Program output: `0 2 2 3 1 1`.

**D.** The array `1 2 1 2 1` has **4** subarrays with sum 3 (`[1,2]`, `[2,1]`, `[1,2]`, `[2,1]`). The array `4 5 0 -2 -3 1` has **7** subarrays with a sum divisible by 5 (section 2: remainders `0 4 4 4 2 4 0` give `C(4,2) + C(2,2) = 7`). Program output: 4 and 7.

**E.** The levels are `3 1 4 1 5 9 2 6`, `1 1 1 1 5 2 2`, `1 1 1 1 2` and `1`. `[2, 5)` has length 3, so `j = 1`; it reads level 1 at position 2 (`min(4, 1) = 1`) and at `5 - 2 = 3` (`min(1, 5) = 1`): the answer is 1. Program output: 1.

**F.** The sum table returns **28** (`11 + 17`, from the blocks `[1, 5)` and `[3, 7)`); the true sum is **22**. The elements at positions 3 and 4, `1 + 5 = 6`, lie in both blocks and were counted twice. Program output: 28 and 22.

**G.** `[2, 6)` is `l = 2`, `r = 5`; `2 ^ 5 = 7`, `k = 2`; `t[2][2] + t[2][5] = 5 + 14 = 19`. `[0, 8)` is `l = 0`, `r = 7`; `0 ^ 7 = 7`, `k = 2`; `t[2][0] + t[2][7] = 9 + 22 = 31`. Program output: 19 and 31.

**H.** The root is index 1 (the first 1, the leftmost minimum). Its left child is index 0 (value 2). Its right child is index 3 (the second 1, which does not displace the first because it is not strictly smaller), whose left child is index 2 (value 3) and right child index 4 (value 2). So `argmin[0, 5) = 1`, the leftmost minimum. (The verifier builds this tree with an independent Python model.)

**I.** (a) A sparse table: 10^6 integers take 75.8 MB (section 4), inside the budget, and each query is two reads; the blocked version (12.8 to 15.7 MB) if the budget were tighter. (b) A segment tree (Topic 21): a sparse table cannot absorb updates. (c) A disjoint sparse table: matrix product is associative, not idempotent, not commutative and has no cheap inverse. (d) A difference array: one pass at the end. (e) A 2D prefix sum, `long long` entries: 25 million entries, 200 MB, so check the budget; the image values bound the sums, which decides whether `int` would do.

---

## Cheat sheet

- **Prefix sums:** `P[0] = 0`, `sum[l, r) = P[r] - P[l]`; use `long long`; 2D: `P[r1][c1] - P[r0][c1] - P[r1][c0] + P[r0][c0]`; **difference array:** `d[l] += v`, `d[r] -= v`, then one prefix pass.
- **Hash-map counting:** seed `{0: 1}`, look up `run - k`, then insert; fix negative remainders; for the longest subarray keep the first index.
- **Sparse table:** level `j` = answers for length `2^j`; query = two blocks `[l, l + 2^j)` and `[r - 2^j, r)`, `j = floor_log2(r - l)`; operation must be **idempotent** (min, max, gcd, AND, OR); 75.8 MB for a million integers (19.0 times), 64 ms to build, 26 to 39 ns per query.
- **Argmin:** store indices, break ties to the smaller index.
- **Disjoint sparse table:** halves around a middle point per level, `k = floor_log2(l ^ r)`, `op(t[k][l], t[k][r])`; for non-idempotent associative operations; 88.1 MB; 2.2 to 3.1 times slower than prefix sums for sums.
- **Blocks:** prefix and suffix minima per block plus a sparse table over block minima; 3.2 to 3.9 times the array; scans inside one block.
- **Cartesian tree:** O(n) with a stack; RMQ = LCA; leftmost minimum on ties; query O(depth), 43.5 steps on random data, 4,999 on sorted.
- **Plain scan** is within 1.2 to 1.4 times of the table up to 64 elements.

---

## Sources

- Bender and Farach-Colton, "The LCA problem revisited" (2000), and Fischer and Heun, "Theoretical and practical improvements on the RMQ problem, with applications to LCA and LCE" (2006): the O(n) memory, O(1) query constructions. Cited from memory; I did not re-read them.
- The disjoint sparse table is described in the competitive-programming literature (for example the cp-algorithms and Codeforces write-ups under that name); the construction here is my own implementation from the description, and was tested as above.
- CLRS, *Introduction to Algorithms*, the chapter on augmenting data structures (for the contrast with updatable structures), and Skiena, *The Algorithm Design Manual*, on static range queries (both from memory).
- LeetCode, by name: Range Sum Query - Immutable, Range Sum Query 2D - Immutable, Subarray Sum Equals K, Subarray Sums Divisible by K, Corporate Flight Bookings, Car Pooling, Longest Continuous Subarray With Absolute Diff Less Than or Equal to Limit, Largest Rectangle in Histogram, Maximum Binary Tree.
