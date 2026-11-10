# DSA 13.02: Iterators and Streams

*Part of the [Design-style coding plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Trees: binary search tree basics](../07_trees/00_plan.md) for in-order traversal, a heap (`std::priority_queue`), and [Cache designs](01_cache_designs.md) for the style of this area*

Two families of "design" questions look different but share one idea: **you are handed data piece by piece and must answer without keeping all of it.** An *iterator* question gives you a structure (nested lists, a tree, k sorted lists) and asks for `next()` and `has_next()` without copying it out first. A *stream* question gives you values one at a time (`add(x)`) and asks for a running answer (average, median, maximum of the last k, most frequent) in bounded memory. The skill is the same: decide what **small state** is enough, keep it correct after every call, and state the cost of each call.

By the end you can:

- state the iterator contract and its three traps (empty parts, `has_next` that consumes, `next` past the end);
- write flatten, zigzag, peeking, skip, nested-list, BST and k-way-merge iterators that do their work lazily, and say what each one stores;
- show why a lazy nested iterator examines 2 elements where an eager flatten examines a million, and why the BST iterator uses at most the height in memory;
- write moving average, k-th largest, running median, sliding-window maximum and first-unique in the right complexity, with the invariant that makes each correct;
- sample uniformly from a stream (reservoir sampling), find frequent items in one pass (Misra-Gries), and compute a running variance that does not collapse (Welford), including how two summaries merge;
- say what each structure costs, from measurements.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 10,287,838 checks passed under AddressSanitizer and UBSan. The tests compare every class against a model that is deliberately simple and slow: vectors that erase the front, sorted vectors, recomputed sums, `long double` two-pass variance, and exact counts. The sampling code is tested on its **distribution**: 240,000 runs of "3 of 10", with a chi-square check over all 120 possible samples and a check of each item's inclusion count. I tried 18 deliberate bugs and **all 18 were caught** by the first version of the tests. One of them (an off-by-one in the reservoir's random index) produces perfectly valid-looking samples and was caught **only by the distribution checks**; an ordinary "the sample has k distinct items" test passes it.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. Inputs are seeded random integers. Statements about other libraries and languages are from memory and marked as such.

---

## 1. The iterator contract

Every iterator in this tutorial has two methods: `has_next()` and `next()`. Write the contract down before any code, because every bug below is a break of one of its lines.

1. `has_next()` **does not consume** anything and can be called any number of times (including zero times before `next()`).
2. `next()` consumes one element and returns it. Calling it when `has_next()` is false is an error (here: `std::out_of_range`).
3. Work is **lazy**: nothing is read from the underlying data before it is needed, and the state is small (a few indices, or a stack as deep as the structure is).

The classic trap is line 1: an implementation whose `has_next()` advances to "find out" and a caller that calls it twice. The other trap is **empty parts** (empty rows, empty lists, empty children): any "is there more in the current part?" test must loop until it finds a real element or runs out of parts. The pattern that avoids both: **keep the cursor always parked on the next real element** (or past the end), and re-park it at the end of `next()`.

```cpp
// The contract: has_next() never consumes anything and may be called any number of times;
// next() consumes one element and throws std::out_of_range when there is none.
struct IntIter {
    virtual ~IntIter() = default;
    virtual bool has_next() = 0;
    virtual int next() = 0;
};

// Rows may be empty, so "the current row has more" is not the same as "there is more".
// The cursor is always parked on a real element (or past the end), and next() re-parks it.
class Flatten2D : public IntIter {
    const vector<vector<int>>& v;
    size_t r = 0, c = 0;
    void settle() { while (r < v.size() && c == v[r].size()) { ++r; c = 0; } }
public:
    explicit Flatten2D(const vector<vector<int>>& vv) : v(vv) { settle(); }
    bool has_next() override { return r < v.size(); }
    int next() override {
        if (!has_next()) throw std::out_of_range("next");
        int x = v[r][c++];
        settle();
        return x;
    }
};

// Zip / zigzag over any iterators: a queue of the sources that still have something.
class Zigzag : public IntIter {
    std::deque<IntIter*> q;
public:
    explicit Zigzag(const vector<IntIter*>& src) { for (IntIter* s : src) if (s->has_next()) q.push_back(s); }
    bool has_next() override { return !q.empty(); }
    int next() override {
        if (q.empty()) throw std::out_of_range("next");
        IntIter* s = q.front(); q.pop_front();
        int x = s->next();
        if (s->has_next()) q.push_back(s);
        return x;
    }
};

// Peeking: remember one element of look-ahead.
class Peeking : public IntIter {
    IntIter& in;
    bool have = false;
    int buf = 0;
    void fill() { if (!have && in.has_next()) { buf = in.next(); have = true; } }
public:
    explicit Peeking(IntIter& i) : in(i) {}
    bool has_next() override { fill(); return have; }
    int peek() { fill(); if (!have) throw std::out_of_range("peek"); return buf; }
    int next() override { int x = peek(); have = false; return x; }
};

// Skip: skip(v) removes the next occurrence of v from what is left, even if it is the element
// already looked at. Later occurrences are remembered in a count and dropped when they arrive.
class SkipIter : public IntIter {
    IntIter& in;
    std::unordered_map<int, int> pending;
    bool have = false;
    int buf = 0;
    void fill() {
        while (!have && in.has_next()) {
            int x = in.next();
            auto it = pending.find(x);
            if (it != pending.end()) { if (--it->second == 0) pending.erase(it); }
            else { buf = x; have = true; }
        }
    }
public:
    explicit SkipIter(IntIter& i) : in(i) {}
    bool has_next() override { fill(); return have; }
    int next() override { fill(); if (!have) throw std::out_of_range("next"); have = false; return buf; }
    void skip(int v) {
        fill();
        if (!have) return;                     // nothing left, so nothing to skip
        if (buf == v) have = false;            // the next element is the one to skip
        else ++pending[v];                     // a later occurrence
    }
};
```

## 2. Flatten a 2D vector

```
T1 flatten [[1,2],[],[3],[],[],[4,5,6]] -> 1 2 3 4 5 6
```

`settle()` is the whole idea: it moves `(r, c)` forward over finished and empty rows until it points at an element or past the last row. The constructor calls it (the first row may be empty); `next()` calls it after taking an element (the next row may be empty). `has_next()` then needs one comparison and is idempotent. The common wrong version tests `c < v[r].size()` in `has_next()` and fails on `[[], [1]]`. Cost: `O(1)` amortised per element, `O(1)` memory.

## 3. Zigzag: several sources, in turn

```
T2 zigzag [1,2,3] [4,5] [6,7,8,9] -> 1 4 6 2 5 7 3 8 9
```

`Zigzag` keeps a queue of the sources that still have something. `next()` takes the front source, takes one element from it, and puts it **back only if it still has more** (otherwise it drops out, and the others carry on in order). It works for k sources of any kind, because it only uses the contract: the sources here can be `Flatten2D`, another `Zigzag`, or a `BstIter`. Cost: `O(1)` per element, `O(k)` memory. The same shape (a queue of live sources) is round-robin scheduling.

## 4. Peeking and skipping

**Peeking** adds `peek()`, which looks at the next element without taking it. The base interface cannot undo a `next()`, so the wrapper **reads one element ahead** into a buffer and serves `peek()`, `next()` and `has_next()` from it.

```
T3 peeking over 1 2 3: peek 1, next 1, next 2, peek 3, next 3, has_next false
```

**Skip** (`skip(v)`: "drop the next occurrence of `v` from what is left") shows what lazy costs you. If `v` is the element already in the buffer, drop the buffer. Otherwise you cannot look ahead through the whole stream, so remember a count `pending[v]` and drop `v` **when it arrives**. The tests compare it with the obvious slow model (a vector, `erase` the first `v`).

```
T4 skip over 2 3 5 6 5 7 5 -1 5 10: has_next true; next 2; skip(5); next 3; next 6; next 5; skip(5) twice; next 7; next -1; has_next true; next 10; has_next false
```

(Two `skip(5)` calls in a row: the first dropped the 5 in the buffer's future, the second is remembered, and both 5s disappear when reached.) `skip` when nothing is left is a no-op. Without that check `pending` would grow with values that never come.

**Range-for.** C++ code expects `for (int x : range)`. `Range` is a small adapter around the contract (see the `merge` code below). It is a *single-pass input range*: it reads one element ahead of the loop body and can be traversed once. The price of the abstraction is measured in section 10.

## 5. Nested lists, lazily

A nested list is an integer or a list of nested lists. The recursive flatten is three lines, but it copies everything before the first `next()`. The lazy version keeps an **explicit stack** of `(list, position)` pairs, and its invariant is the same as `settle()` in section 2: the top of the stack is parked on an integer, or the stack is empty. Going into a list is a push; finishing a list is a pop; an empty list is pushed and popped without ever stopping.

```cpp
// A nested list: either an integer or a list of nested lists (LeetCode 341).
struct Nested {
    bool is_int = false;
    int val = 0;
    vector<Nested> list;
    static Nested I(int v) { Nested n; n.is_int = true; n.val = v; return n; }
    static Nested L(vector<Nested> l) { Nested n; n.list = std::move(l); return n; }
};

// Lazy: an explicit stack of (list, position). The stack is parked on an integer or is empty.
class NestedIter : public IntIter {
    struct Frame { const vector<Nested>* l; size_t i; };
    vector<Frame> st;
    long long examined = 0;                    // elements looked at so far (to show the laziness)
    void settle() {
        while (!st.empty()) {
            Frame& f = st.back();
            if (f.i == f.l->size()) { st.pop_back(); continue; }
            const Nested& n = (*f.l)[f.i];
            ++examined;
            if (n.is_int) return;
            ++f.i;                             // step past the list, then go into it
            st.push_back({&n.list, 0});        // (f is invalid after this line)
        }
    }
public:
    explicit NestedIter(const vector<Nested>& top) { st.push_back({&top, 0}); settle(); }
    bool has_next() override { return !st.empty(); }
    int next() override {
        if (st.empty()) throw std::out_of_range("next");
        int x = (*st.back().l)[st.back().i].val;
        ++st.back().i;
        settle();
        return x;
    }
    long long examined_so_far() const { return examined; }
};

// Eager, for comparison: everything is copied into a vector before the first next().
inline void flatten_eager(const vector<Nested>& l, vector<int>& out, long long& examined) {
    for (const Nested& n : l) { ++examined; if (n.is_int) out.push_back(n.val); else flatten_eager(n.list, out, examined); }
}
```

```
T5 nested [[1,1],2,[1,[1]]] -> 1 1 2 1 1
T5 two lists, 1000001 integers in all: elements examined before the first next(): lazy 2, eager 1000003
```

A million integers in the second list; the lazy iterator looked at **2** elements to produce its first answer, the eager one at 1,000,003. If the caller only ever wants the first few, or stops when a condition holds, the lazy one never touches the rest. The explicit stack also means the *iterator* uses no recursion, so its depth is limited by memory, not by the call stack. (The tests include a list nested 3,000 deep. The `Nested` data structure itself is destroyed recursively, so a depth of hundreds of thousands would crash in the destructor, not in the iterator.)

## 6. The BST iterator

An in-order walk of a binary search tree gives the keys sorted. The iterator version replaces the recursion by a stack of the **left spine**: push the node and all its left descendants; to take the next key, pop a node, push the left spine of its **right child**, and return the popped key.

```cpp
struct TreeNode { int val; TreeNode *l = nullptr, *r = nullptr; };

struct Bst {                                   // plain unbalanced search tree; nodes never move
    std::deque<TreeNode> pool;
    TreeNode* root = nullptr;
    void insert(int v) {
        pool.push_back({v});
        TreeNode* n = &pool.back();
        if (!root) { root = n; return; }
        TreeNode* p = root;
        for (;;) {
            TreeNode*& child = v < p->val ? p->l : p->r;
            if (!child) { child = n; return; }
            p = child;
        }
    }
};

// In-order iterator: the stack holds the path to the next node, so its size is at most the height.
class BstIter : public IntIter {
    vector<TreeNode*> st;
    size_t peak = 0;
    void push_left(TreeNode* n) { for (; n; n = n->l) st.push_back(n); peak = std::max(peak, st.size()); }
public:
    explicit BstIter(TreeNode* root) { push_left(root); }
    bool has_next() override { return !st.empty(); }
    int next() override {
        if (st.empty()) throw std::out_of_range("next");
        TreeNode* n = st.back(); st.pop_back();
        push_left(n->r);
        return n->val;
    }
    size_t peak_stack() const { return peak; }
};
```

```
T6 peak stack size: balanced tree of 1023 nodes 10, 1000 nodes inserted in increasing order 1, in decreasing order 1000
```

The stack holds a path from the root, so it never exceeds the **height** of the tree, not the number of nodes: 10 for the balanced tree of 1,023 nodes, 1,000 for a chain that leans left, and **1** for a chain that leans right (every popped node pushes only its right child). `next()` is `O(1)` amortised: each node is pushed once and popped once, so a full traversal does `2n` stack operations, though a single `next()` can push `O(h)`. Morris traversal gets `O(1)` extra memory by temporarily rewiring the tree, which is not allowed if other readers share it (from memory).

## 7. Merge k sorted sources

A heap with one pending element per source: `next()` pops the smallest, then pulls one more element from the same source. Memory `O(k)`, time `O(log k)` per element, and nothing past the heap's k elements is read, which is what you want when the sources are files, network streams or generators.

```cpp
// Merge k sorted iterators. The heap holds one pending element per source: O(k) memory,
// O(log k) per next(), and nothing is read before it is needed.
class MergeIter : public IntIter {
    std::vector<IntIter*> in;
    using E = std::tuple<int, size_t>;         // (value, source): ties go to the lower source
    std::priority_queue<E, std::vector<E>, std::greater<E>> pq;
    void refill(size_t i) { if (in[i]->has_next()) pq.push({in[i]->next(), i}); }
public:
    explicit MergeIter(std::vector<IntIter*> src) : in(std::move(src)) { for (size_t i = 0; i < in.size(); ++i) refill(i); }
    bool has_next() override { return !pq.empty(); }
    int next() override {
        if (pq.empty()) throw std::out_of_range("next");
        auto [v, i] = pq.top(); pq.pop();
        refill(i);
        return v;
    }
};

// Adapter so that `for (int x : Range(it))` works. It is a single-pass input range:
// the iterator is one element ahead of the loop body, and it can be traversed once.
struct Range {
    IntIter& in;
    struct End {};
    struct Cur {
        IntIter* in; int v; bool ok;
        explicit Cur(IntIter* i) : in(i), v(0), ok(i->has_next()) { if (ok) v = i->next(); }
        int operator*() const { return v; }
        Cur& operator++() { ok = in->has_next(); if (ok) v = in->next(); return *this; }
        bool operator!=(End) const { return ok; }
    };
    explicit Range(IntIter& i) : in(i) {}
    Cur begin() { return Cur(&in); }
    End end() { return {}; }
};
```

```
T7 merge [1,4,7] [2,5] [] [0,3,6,9] -> 0 1 2 3 4 5 6 7 9
```

Ties go to the lower source number because the heap compares `(value, source)` as a pair, which makes the merge stable.

---

## 8. Streams: one pass, bounded memory

Now the data arrives as `add(x)`. Each structure below keeps **less than the history**, and each has an invariant that is restated after every call.

```cpp
// Moving average of the last w values. The sum is a long long and is updated, never recomputed.
class MovingAverage {
    size_t w, head = 0, n = 0;
    vector<int> ring;
    long long sum = 0;
public:
    explicit MovingAverage(size_t window) : w(window), ring(window) { if (!window) throw std::invalid_argument("window"); }
    double add(int x) {
        if (n == w) sum -= ring[head]; else ++n;
        ring[head] = x; sum += x;
        head = (head + 1) % w;
        return (double)sum / n;
    }
};

// k-th largest so far: a min-heap that never holds more than k values.
class KthLargest {
    size_t k;
    std::priority_queue<int, vector<int>, std::greater<int>> h;
public:
    static constexpr long long NONE = LLONG_MIN;
    explicit KthLargest(size_t kk) : k(kk) { if (!kk) throw std::invalid_argument("k"); }
    long long add(int x) {
        if (h.size() < k) h.push(x);
        else if (x > h.top()) { h.pop(); h.push(x); }
        return h.size() == k ? h.top() : NONE;
    }
};

// Running median: a max-heap for the lower half, a min-heap for the upper half.
// Invariant: lo.size() == hi.size() or lo.size() == hi.size() + 1.
class MedianFinder {
    std::priority_queue<int> lo;
    std::priority_queue<int, vector<int>, std::greater<int>> hi;
public:
    void add(int x) {
        if (lo.empty() || x <= lo.top()) lo.push(x); else hi.push(x);
        if (lo.size() > hi.size() + 1) { hi.push(lo.top()); lo.pop(); }
        else if (hi.size() > lo.size()) { lo.push(hi.top()); hi.pop(); }
    }
    double median() const {
        if (lo.empty()) throw std::out_of_range("median");
        if (lo.size() > hi.size()) return lo.top();
        return ((long long)lo.top() + hi.top()) / 2.0;
    }
};

// Maximum of the last w values: a deque of indices whose values decrease from front to back.
class SlidingMax {
    long long w, i = 0;
    std::deque<std::pair<long long, int>> dq;  // (index, value)
public:
    explicit SlidingMax(size_t window) : w((long long)window) { if (!window) throw std::invalid_argument("window"); }
    int add(int x) {
        while (!dq.empty() && dq.back().second <= x) dq.pop_back();
        dq.push_back({i, x});
        if (dq.front().first <= i - w) dq.pop_front();
        ++i;
        return dq.front().second;
    }
};

// First value seen exactly once so far. Dead entries are dropped from the front when reached.
class FirstUnique {
    std::deque<int> q;
    std::unordered_map<int, int> cnt;
public:
    void add(int x) { if (++cnt[x] == 1) q.push_back(x); }
    std::optional<int> first() {
        while (!q.empty() && cnt[q.front()] > 1) q.pop_front();
        if (q.empty()) return std::nullopt;
        return q.front();
    }
};
```

**Moving average.** A ring buffer of the last `w` values and a running sum. The sum is a `long long` (a window of 32-bit values can overflow an `int`), and it is updated by adding the new value and subtracting the one that left, never recomputed.

```
T8 moving average (window 3) of 1 10 3 5 -> 1.0000 5.5000 4.6667 6.0000
```

**k-th largest.** A **min**-heap that never holds more than `k` values: its top is the k-th largest. A new value enters only if it beats the top. `O(log k)` per add; the sentinel `NONE` is returned until `k` values exist.

```
T9 3rd largest after each of 4 5 8 2 3 5 10 9 4 -> none none 4 4 4 5 5 8 8
```

**Running median.** Two heaps: a max-heap `lo` with the smaller half and a min-heap `hi` with the larger half. The invariant: every value in `lo` is at most every value in `hi`, and `lo.size()` equals `hi.size()` or exceeds it by one. The median is `lo.top()` or the mean of the two tops. The mean is computed in `long long` first: two `int` extremes would overflow in `int`.

```
T10 running median of 5 15 1 3 -> 5 10 5 4
```

The alternative that sounds fine, a sorted vector with insertion, is `O(n)` per add because of the shifting. For 100,000 random values, adding each one and reading the median after each add (ms, run 1 / run 2):

| Method | Time |
|---|---|
| Two heaps | 5.2 / 5.3 |
| Sorted vector, binary-search insert | 191.3 / 232.0 |

The vector is slower by a large factor, and the factor grows with `n` (it is `O(n^2)` against `O(n log n)`). `memmove` is fast, which is why 100,000 still finishes quickly. The heaps cannot **remove** an arbitrary value (the "sliding window median" problem needs an ordered multiset or a lazy-deletion trick; see the problems).

**Sliding-window maximum.** Keep a deque of `(index, value)` whose values **decrease from front to back**. A new value pops every smaller-or-equal value off the back (they can never be the maximum again: the new one is bigger and stays longer), then goes on the back. The front is the maximum; it is dropped when its index leaves the window. Every element enters and leaves the deque once: `O(1)` amortised.

```
T11 sliding maximum (window 3) after each of 1 3 -1 -3 5 3 6 7 -> 1 3 3 3 5 5 6 7
```

200,000 random values, window 1,000, maximum after each add (ms, run 1 / run 2). All three methods return the same sums.

| Method | Time |
|---|---|
| Monotone deque | 2.1 / 2.5 |
| `std::multiset` (insert, erase, take the last) | 29.1 / 35.3 |
| Rescan the window each time (`O(n w)`) | 124.2 / 135.8 |

```
T16 the three sliding-maximum methods agree: yes (sum 199811839820)
```

The deque is the fastest; the rescan costs `w` per step and is the slowest here, and it gets worse with a larger window while the deque does not.

**First unique.** A queue of values in arrival order and a count per value. A value joins the queue on its first arrival. `first()` drops from the front while the front's count exceeds 1, then reports the front. Dead entries are dropped **when reached**, so each value is dropped once: `O(1)` amortised. The count map grows with the number of distinct values, which is the honest memory bound of this exact version.

```
T12 first unique after 2 3 5, then add 5, add 2, add 3 -> 2 2 3 none
```

## 9. Sampling and summaries

When even `O(distinct)` memory is too much, you keep a **sample** or a **summary**.

```cpp
// Reservoir sampling (algorithm R): after n items, each item is in the k-sample with probability k/n.
template <class T>
class Reservoir {
    size_t k;
    vector<T> r;
    long long seen = 0;
    std::mt19937_64 g;
public:
    Reservoir(size_t kk, uint64_t seed) : k(kk), g(seed) {}
    void add(const T& x) {
        ++seen;
        if (r.size() < k) { r.push_back(x); return; }
        long long j = std::uniform_int_distribution<long long>(0, seen - 1)(g);   // 0 .. seen-1
        if (j < (long long)k) r[(size_t)j] = x;
    }
    const vector<T>& sample() const { return r; }
};

// Misra-Gries: at most k-1 counters. Every value that occurs more than n/k times is in the table,
// and each count is below the true count by at most n/k. (k = 2 is the Boyer-Moore majority vote.)
class MisraGries {
    size_t k;
    std::unordered_map<int, long long> c;
public:
    explicit MisraGries(size_t kk) : k(kk) { if (kk < 2) throw std::invalid_argument("k"); }
    void add(int x) {
        auto it = c.find(x);
        if (it != c.end()) { ++it->second; return; }
        if (c.size() < k - 1) { c[x] = 1; return; }
        for (auto i = c.begin(); i != c.end();) { if (--i->second == 0) i = c.erase(i); else ++i; }
    }
    long long estimate(int x) const { auto it = c.find(x); return it == c.end() ? 0 : it->second; }
    size_t size() const { return c.size(); }
    const std::unordered_map<int, long long>& table() const { return c; }
};

// Welford: running mean and variance without the cancellation of "sum of squares minus square of sum".
struct Running {
    long long n = 0;
    double mean = 0, m2 = 0;
    void add(double x) { ++n; double d = x - mean; mean += d / n; m2 += d * (x - mean); }
    double variance() const { return n > 1 ? m2 / (double)(n - 1) : 0.0; }
};
// Combine two summaries (Chan et al.), for example from two threads or two machines.
inline Running merge(const Running& a, const Running& b) {
    Running r; r.n = a.n + b.n;
    if (r.n == 0) return r;
    double d = b.mean - a.mean;
    r.mean = a.mean + d * (double)b.n / (double)r.n;
    r.m2 = a.m2 + b.m2 + d * d * (double)a.n * (double)b.n / (double)r.n;
    return r;
}
inline double naive_variance(double sum, double sumsq, long long n) { return n > 1 ? (sumsq - sum * sum / n) / (double)(n - 1) : 0.0; }
```

**Reservoir sampling.** Keep `k` items. The i-th item (1-based) replaces a random slot with probability `k / i`. Why every item ends with probability `k / n`: item `j` is kept at step `j` with probability `k / j` and then survives each later step `i` with probability `1 - (1/i)` (the new item picks one particular slot with probability `1/i`), so it survives with `(k/j) * (j/(j+1)) * ... * ((n-1)/n) = k/n`. The telescoping product is the proof (see problem G).

```
T13 reservoir, 3 of 10, 1200000 runs, times each item was kept (expected 360000 each): correct 359962 358963 359953 359677 360212 360157 359804 360950 360288 360034
T13 the same with the random index drawn from one slot too many: 436372 435919 435277 327371 327394 327180 327356 328119 327669 327343
```

Expected 360,000 each. The correct code is flat. The second line draws the random index from `0 .. seen` instead of `0 .. seen - 1`: one slot too many, so late items have a slightly lower chance of getting in, and the **first three items are kept 436,000 times against 327,000 for the others**. This is an off-by-one that a test of "k distinct items come out" does not see; only a distribution test does.

**Misra-Gries (frequent items).** With `k` you keep at most `k - 1` counters. A value in the table counts up; a new value enters if there is room; otherwise **every counter is decremented** (and a counter that reaches 0 leaves). The guarantee: any value that occurs more than `n / k` times is in the table at the end, and every estimate is lower than the truth by at most `n / k`. (Why: each decrement round removes `k` units of count in total from the history, one from the new value and one from each of `k - 1` counters, so there are at most `n / k` rounds.) For `k = 2` this is the Boyer-Moore majority vote.

```
T14 Misra-Gries with k=50 on 1000000 zipf(1.0) items over 100000 values (80786 distinct seen): table holds 25 counters, error bound n/k = 20000
T14 top five by true count: value 0: true 82997, estimate 65001; value 1: true 41113, estimate 23117; value 2: true 27365, estimate 9369; value 3: true 20684, estimate 2690; value 4: true 16426, estimate 0;
T14 values above n/k: 4, of them missing from the table: 0
```

Read it honestly. The **membership** guarantee held: every value above `n / k` is in the table. The **estimates** can be far too low (value 3: 2,690 against 20,684; the error is below the bound of 20,000, so the guarantee holds, but it is weak). The practical use is to get a **candidate list** in one pass and then count the candidates exactly in a second pass, if you can make one. The table cost 25 counters for a stream with 80,786 distinct values.

**Welford (running variance).** The textbook formula `(sum of squares - (sum)^2 / n) / (n - 1)` subtracts two huge, nearly equal numbers; that is *catastrophic cancellation*. Welford keeps the running mean and the running sum of squared deviations from it, which stays small.

```
T15 1,000,000 values near 1e9 with standard deviation 1: two-pass long double variance 1.000349, Welford 1.000349, sum-of-squares formula 76772.62
```

The sum-of-squares formula returns **76,772** where the variance is 1. It can also return a negative number. Welford agrees with a `long double` two-pass computation to all printed digits. Two summaries combine exactly (`merge`, Chan et al.): useful when two threads or two machines each saw part of the stream; the tests check the merge against the whole stream for random splits, including empty parts.

**Other summaries (from memory).** Count-distinct needs a sketch such as HyperLogLog, and approximate quantiles need a structure such as t-digest or Greenwald-Khanna; both are in the "randomised and approximate" tutorial of the next area and are not tested here. The count-min sketch of Cache designs (section 6) is another stream summary.

## 10. What abstraction costs

Summing 10,000,000 integers in 1,000 rows of 10,000 (ms, run 1 / run 2):

| Way of reading | Time |
|---|---|
| Two nested loops over the vectors | 8.8 / 6.8 |
| `Flatten2D` through the `IntIter` interface (virtual `has_next` and `next`) | 22.6 / 18.0 |
| The same through `Range` and range-for | 33.8 / 32.0 |

The virtual interface costs about 2 to 3 times here, and the range adapter about 3.5 to 5 times: two virtual calls per element plus the extra look-ahead. For a fast inner loop, a template over the concrete iterator type removes the virtual calls. For an interview class, the interface is what is asked for. It is worth knowing the cost exists.

## 11. How to answer a "design an iterator / stream" question

1. **Ask:** may the data change while iterating? Can `next()` be called without `has_next()`? What does `next()` do at the end (throw, return a sentinel)? What are the sizes, and is the order defined?
2. **Write the invariant** in one sentence ("the cursor is on an integer or past the end", "`lo` holds the smaller half, sizes differ by at most one").
3. **List the state** and say why it is enough. If the state grows with the input (the hash map in first-unique), say so.
4. **Give the cost per call** (worst and amortised) and the memory.
5. **Test the empty cases first:** empty input, empty parts, one element, and the call after the end.
6. **If asked about threads:** a shared iterator needs one lock around `has_next` plus `next` together (a check-then-act race), or give each thread its own iterator.
7. **If the data may change** (from memory: Java's iterators throw `ConcurrentModificationException`): keep a version number in the container, copy it into the iterator, compare in `next()`. This tutorial's iterators assume the data stays fixed.

## 12. Complexities

| Structure | Per call | Memory |
|---|---|---|
| Flatten 2D | `O(1)` amortised | `O(1)` |
| Zigzag over k | `O(1)` | `O(k)` |
| Peeking | `O(1)` | `O(1)` |
| Skip | `O(1)` expected | `O(#skips pending)` |
| Nested list (lazy) | `O(1)` amortised | `O(depth)` |
| BST iterator | `O(1)` amortised, `O(h)` worst | `O(h)` |
| Merge k | `O(log k)` | `O(k)` |
| Moving average | `O(1)` | `O(w)` |
| k-th largest | `O(log k)` | `O(k)` |
| Running median | `O(log n)` | `O(n)` |
| Sliding maximum | `O(1)` amortised | `O(w)` |
| First unique | `O(1)` amortised | `O(distinct)` |
| Reservoir | `O(1)` | `O(k)` |
| Misra-Gries | `O(1)` amortised | `O(k)` |
| Welford | `O(1)` | `O(1)` |

## 13. Common mistakes

1. A `has_next()` that consumes (or that is only correct when called exactly once).
2. Trusting "the current row has more" with empty rows, empty lists and empty children.
3. Forgetting that `peek()` and `has_next()` must both fill the buffer.
4. `skip(v)` that always remembers a count and never checks the front, so it skips a **later** `v` instead of the next one.
5. Using recursion for the nested or BST iterator, which flattens in advance and breaks the memory bound.
6. A BST iterator that pushes the whole tree, or forgets the **left spine of the right child**.
7. A moving average that recomputes the sum, or keeps it in an `int`.
8. A max-heap for "k-th largest" (it is a **min**-heap of size k), or letting the heap grow to `k + 1`.
9. Median heaps that are not rebalanced after **every** add.
10. A sliding-window deque that stores values and not indices, or checks expiry with `<` where `<=` is needed.
11. Reservoir sampling with an index range that is off by one (or `rand() % k`): valid-looking and biased.
12. Using the sum-of-squares formula for variance on data with a large mean.
13. Treating a Misra-Gries estimate as a count instead of a lower bound.

## 14. Questions to answer out loud

1. State the iterator contract. What goes wrong if `has_next()` consumes?
2. Why does `Flatten2D` park the cursor in the constructor and after every `next()`?
3. What is the state of `Zigzag`, and why does an exhausted source not return to the queue?
4. How does `skip` stay lazy? What does it store, and when does it use it?
5. Why did the lazy nested iterator examine 2 elements and the eager one a million?
6. Why is the BST iterator's memory at most the height? Why is `next()` amortised `O(1)`?
7. Why does the merge store one element per source, and what does the pair `(value, source)` give?
8. Why is the k-th largest a min-heap? What are the two heaps' roles in the median?
9. Why can the deque for the sliding maximum drop smaller values from the back?
10. Prove that reservoir sampling keeps each item with probability `k / n`.
11. What does Misra-Gries guarantee, and what does it not?
12. Why does the sum-of-squares variance fail, and how does Welford avoid it? How do you merge two summaries?

## 15. Problems

### 15.1 On paper

**A.** Over the stream `1 2 2 3 2 4`: `skip(2)`, `next`, `next`, `skip(2)`, `next`, `next`, `has_next`. What does each call return?

**B.** Sliding-window maximum with window 3 over `4 2 12 3 8 1 1 5`: the answer after each add.

**C.** The running median after each add of `7 1 9 3 5 2`.

**D.** Misra-Gries with `k = 3` (two counters) over `1 1 2 3 1 2 2 4`: the final estimate of each value. Check them against the true counts and the bound `n / k`.

**E.** Insert `8 3 10 1 6 14 4 7 13` into a search tree in that order. In what order does the BST iterator return keys, and what is the largest size its stack reaches?

**F.** For the nested list `[[], [1, [[2], []]], 3]`: the output order and the number of elements examined before the first `next()` and in total.

**G.** Prove that after `n` items, every item is in a reservoir of size `k` with probability `k / n`.

### 15.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. Several are premium problems.

| Problem | Number | How to approach |
|---|---|---|
| Flatten Nested List Iterator | 341 | Section 5: an explicit stack, parked on an integer in `has_next` |
| Peeking Iterator | 284 | Section 4: a one-element buffer |
| Binary Search Tree Iterator | 173 | Section 6: the stack of the left spine |
| Zigzag Iterator | 281 (premium) | Section 3: a queue of live sources |
| Kth Largest Element in a Stream | 703 | A min-heap of size k |
| Find Median from Data Stream | 295 | Two heaps with the sizes invariant |
| Sliding Window Maximum | 239 | Monotone deque of indices |
| Linked List Random Node / Random Pick Index | 382 / 398 | Reservoir sampling with k = 1 |
| Sliding Window Median (hard) | 480 | Two multisets, or two heaps with lazy deletion |

### 15.3 In your editor

**A.** Make the iterators **generic** (`template <class T>`) and add a `map`/`filter` adapter. Measure the virtual-call cost against a template version.

**B.** Add a **fail-fast** version number to `Flatten2D` and a test that mutates the vector in between.

**C.** Write the sliding-window **median** with two `std::multiset`s, and test it against the brute force.

**D.** Make the reservoir **weighted** (look up the A-Res algorithm first) and test the distribution.

**E.** Implement Morris in-order traversal and compare memory and speed with the stack iterator.

### Answers

The program that checks the paper problems printed:

```
A 1 2 3 4 false
B 4 4 12 12 12 8 8 5
C 7 4 7 5 5 4
D 1:1 2:1 3:0 4:0  size 2
E 1 3 4 6 7 8 10 13 14 peak 3
F 1 2 3 examined at start 3 total 8
```

**A.** `1`, `2`, `3`, `4`, then `has_next` is false. The first `skip(2)` removes the first 2, so what is left is `1 2 3 2 4` and the next two calls give 1 and 2. The second `skip(2)` removes the 2 that is still ahead in `3 2 4`, so the last two calls give 3 and 4.

**B.** `4 4 12 12 12 8 8 5`. The first two adds are windows of fewer than three values.

**C.** `7 4 7 5 5 4`.

**D.** The estimates are 1:1, 2:1, 3:0, 4:0, and the table holds two counters. True counts are 1:3, 2:3, 3:1, 4:1, and `n / k` = 8/3, about 2.67; the errors are 2, 2, 1, 1, all within the bound. Values 1 and 2 occur more than 2.67 times and both are in the table.

**E.** `1 3 4 6 7 8 10 13 14`, and the largest stack size is 3 (the tree has height 4). The first stack holds 8, 3, 1.

**F.** `1 2 3`. The iterator has examined 3 elements before the first `next` (the empty list, the second list, and the 1 in it), and 8 in total.

**G.** Item `j` is placed at step `j` with probability `min(1, k / j)`, which is `k / j` for `j > k`. At each later step `i` it is removed only if the new item is chosen (probability `k / i`) and picks this item's slot (probability `1 / k`), so it survives with `1 - 1/i = (i - 1) / i`. The product from `i = j + 1` to `n` telescopes to `j / n`, and `(k / j) * (j / n) = k / n`. For `j <= k` the item starts with probability 1 and survives with `k / n` by the same product from `i = k + 1`.

## Cheat sheet

- **Contract:** `has_next` does not consume; `next` throws at the end; lazy; small state. **Park the cursor** on the next real element and re-park after each `next`.
- **Flatten:** loop over empty parts. **Zigzag:** queue of live sources. **Peek:** one-element buffer. **Skip:** front check, else a count.
- **Nested:** explicit stack, lazy (2 examined against 1,000,003). **BST:** stack of the left spine, memory at most the height (1 to 1,000 here). **Merge k:** heap of `(value, source)`.
- **Stream invariants:** moving sum (long long); k-th largest = min-heap of size k; median = max-heap below, min-heap above, sizes differ by at most one; sliding max = decreasing deque of indices; first unique = queue plus counts.
- **Reservoir:** replace with probability `k / i`, index in `0 .. i - 1`; test by distribution. **Misra-Gries:** `k - 1` counters, finds everything above `n / k`, estimates are lower bounds. **Welford:** never sum squares of large values.
- **Cost of the abstraction:** about 2 to 3 times a plain loop through the interface, 3.5 to 5 times through `Range`, here.

---

## Sources

- All code is tested here (10,287,838 checks under ASan and UBSan; 18 deliberate bugs caught). All printed traces and timings are measured on one Xeon container, two runs each, with identical traces.
- Reservoir sampling (algorithm R, Vitter), Misra and Gries (1982), Welford (1962) and the merge of Chan, Golub and LeVeque are standard results, described here from memory and proved or tested in the text and the tests. Statements about Morris traversal, Java iterators, HyperLogLog, t-digest and Greenwald-Khanna are from memory and were not checked here.
- LeetCode numbers and titles are from memory; check a number before you rely on it. The explanations, traces and code are my own.
