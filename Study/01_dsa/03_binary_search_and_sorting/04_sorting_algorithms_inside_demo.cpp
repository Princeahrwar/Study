// Tutorial: sorting algorithms inside. The sorts shown in the tutorial, checked against std::sort and std::stable_sort
// on every small input and on random ones, plus the few measurements the text quotes.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 04_sorting_algorithms_inside_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

using Vec = std::vector<int>;

// ---------------------------------------------------------------- 1. Insertion sort

// Sort a[lo..hi) by growing a sorted prefix: each new element moves left past the larger ones. Equal elements do not
// pass each other (strict <), so it is stable. It makes one move per inversion, so it is fast on nearly sorted input.
template <class T>
void insertionSort(std::vector<T>& a, int lo, int hi) {
    for (int i = lo + 1; i < hi; ++i) {
        T x = a[i];
        int j = i;
        while (j > lo && x < a[j - 1]) { a[j] = a[j - 1]; --j; }   // shift the larger elements right
        a[j] = x;
    }
}

// ---------------------------------------------------------------- 2. Merge sort

// Merge the sorted runs a[lo..mid) and a[mid..hi) through tmp. On a tie the left element goes first: stable.
template <class T>
void mergeRuns(std::vector<T>& a, std::vector<T>& tmp, int lo, int mid, int hi) {
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) tmp[k++] = (a[j] < a[i]) ? a[j++] : a[i++];
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    std::copy(tmp.begin() + lo, tmp.begin() + hi, a.begin() + lo);
}

// Top-down merge sort of a[lo..hi); tmp is a buffer as long as a. Stable, O(n log n) always, O(n) extra memory.
template <class T>
void mergeSort(std::vector<T>& a, std::vector<T>& tmp, int lo, int hi) {
    if (hi - lo < 2) return;
    int mid = lo + (hi - lo) / 2;
    mergeSort(a, tmp, lo, mid);
    mergeSort(a, tmp, mid, hi);
    mergeRuns(a, tmp, lo, mid, hi);
}

// The number of inversions (pairs i < j with a[i] > a[j]) of a[lo..hi), counted while merge-sorting it. When the next
// element comes from the right run, it is smaller than every element still waiting in the left run: mid - i inversions.
template <class T>
long long countInversions(std::vector<T>& a, std::vector<T>& tmp, int lo, int hi) {
    if (hi - lo < 2) return 0;
    int mid = lo + (hi - lo) / 2;
    long long count = countInversions(a, tmp, lo, mid) + countInversions(a, tmp, mid, hi);
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (a[j] < a[i]) { count += mid - i; tmp[k++] = a[j++]; }
        else tmp[k++] = a[i++];
    }
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi) tmp[k++] = a[j++];
    std::copy(tmp.begin() + lo, tmp.begin() + hi, a.begin() + lo);
    return count;
}

// ---------------------------------------------------------------- 3. Quicksort

// Lomuto partition of a[lo..hi), pivot = the last element. Returns the pivot's final index p:
// a[lo..p) < pivot, a[p] == pivot, a[p + 1..hi) >= pivot.
template <class T>
int partitionLomuto(std::vector<T>& a, int lo, int hi) {
    const T pivot = a[hi - 1];
    int i = lo;                                                 // a[lo..i) holds the elements smaller than the pivot
    for (int j = lo; j < hi - 1; ++j)
        if (a[j] < pivot) std::swap(a[i++], a[j]);
    std::swap(a[i], a[hi - 1]);                                 // the pivot goes between the two parts
    return i;
}

// Quicksort with the Lomuto partition. Recurse into the smaller part and loop on the larger: the recursion never goes more
// than log2(n) calls deep, even when the running time is quadratic. Not stable.
template <class T>
void quickSortLomuto(std::vector<T>& a, int lo, int hi) {
    while (hi - lo > 1) {
        int p = partitionLomuto(a, lo, hi);
        if (p - lo < hi - (p + 1)) { quickSortLomuto(a, lo, p); lo = p + 1; }
        else { quickSortLomuto(a, p + 1, hi); hi = p; }
    }
}

// Hoare partition of a[lo..hi) (at least two elements), pivot = the value of the middle element. Two indexes move toward
// each other and swap the pairs that are on the wrong side. Returns j with a[lo..j] <= pivot <= a[j + 1..hi), both non-empty.
template <class T>
int partitionHoare(std::vector<T>& a, int lo, int hi) {
    const T pivot = a[lo + (hi - lo - 1) / 2];
    int i = lo - 1, j = hi;
    while (true) {
        do ++i; while (a[i] < pivot);                           // stops at an element >= pivot
        do --j; while (pivot < a[j]);                           // stops at an element <= pivot
        if (i >= j) return j;
        std::swap(a[i], a[j]);
    }
}

// Quicksort with the Hoare partition. The parts are [lo, j] and [j + 1, hi); the pivot is not placed. Not stable.
template <class T>
void quickSortHoare(std::vector<T>& a, int lo, int hi) {
    while (hi - lo > 1) {
        int j = partitionHoare(a, lo, hi);
        if (j + 1 - lo < hi - (j + 1)) { quickSortHoare(a, lo, j + 1); lo = j + 1; }
        else { quickSortHoare(a, j + 1, hi); hi = j + 1; }
    }
}

// Three-way quicksort: a random pivot value splits a[lo..hi) into  < pivot | == pivot | > pivot  in one pass (the Dutch
// national flag of tutorial 01.02). The middle part is final, so many equal keys make the sort faster. Not stable.
template <class T>
void quickSort3(std::vector<T>& a, int lo, int hi, std::mt19937& rng) {
    while (hi - lo > 1) {
        const T pivot = a[lo + (int)(rng() % (hi - lo))];
        int lt = lo, i = lo, gt = hi;                           // a[lo..lt) < pivot, a[lt..i) == pivot, a[gt..hi) > pivot
        while (i < gt) {
            if (a[i] < pivot) std::swap(a[lt++], a[i++]);
            else if (pivot < a[i]) std::swap(a[i], a[--gt]);
            else ++i;
        }
        if (lt - lo < hi - gt) { quickSort3(a, lo, lt, rng); lo = gt; }
        else { quickSort3(a, gt, hi, rng); hi = lt; }
    }
}

// ---------------------------------------------------------------- 4. Heapsort

// Sift a[lo + i] down inside the max-heap a[lo..lo + n): node k has the children 2k + 1 and 2k + 2 (counted from lo).
template <class T>
void siftDown(std::vector<T>& a, int lo, int n, int i) {
    T x = a[lo + i];
    while (2 * i + 1 < n) {
        int c = 2 * i + 1;
        if (c + 1 < n && a[lo + c] < a[lo + c + 1]) ++c;        // the larger child
        if (!(x < a[lo + c])) break;                            // x is not smaller than its larger child: it stays
        a[lo + i] = a[lo + c];
        i = c;
    }
    a[lo + i] = x;
}

// Heapsort of a[lo..hi): build a max-heap bottom-up in O(n), then n - 1 times move the maximum behind the shrinking heap.
// O(n log n) always, O(1) extra memory, not stable.
template <class T>
void heapSort(std::vector<T>& a, int lo, int hi) {
    int n = hi - lo;
    for (int i = n / 2 - 1; i >= 0; --i) siftDown(a, lo, n, i);
    for (int end = n - 1; end > 0; --end) {
        std::swap(a[lo], a[lo + end]);
        siftDown(a, lo, end, 0);
    }
}

// ---------------------------------------------------------------- 5. Sorting without comparing

// Stable counting sort of a by key(x), where every key is in [0, k). O(n + k) time, O(n + k) extra memory.
template <class T, class Key>
void countingSortBy(std::vector<T>& a, int k, Key key) {
    std::vector<int> next(k + 1, 0);
    for (const T& x : a) ++next[key(x) + 1];                    // next[v + 1] = how many elements have key v
    for (int v = 0; v < k; ++v) next[v + 1] += next[v];         // next[v] = the first slot of key v
    std::vector<T> out(a.size());
    for (const T& x : a) out[next[key(x)]++] = x;               // left to right: equal keys keep their order
    a = out;
}

// LSD radix sort of non-negative ints in base 10: one stable counting sort per digit, least significant digit first.
void radixSort(Vec& a) {
    int mx = a.empty() ? 0 : *std::max_element(a.begin(), a.end());
    for (long long place = 1; mx / place > 0; place *= 10)
        countingSortBy(a, 10, [&](int x) { return (int)(x / place % 10); });
}

// ---------------------------------------------------------------- 6. Introsort

// Order a[lo], a[mid], a[hi - 1] so that a[mid] holds their median: a pivot that sorted or reversed input cannot ruin.
template <class T>
void medianOfThree(std::vector<T>& a, int lo, int hi) {
    int mid = lo + (hi - lo - 1) / 2;
    if (a[mid] < a[lo]) std::swap(a[mid], a[lo]);
    if (a[hi - 1] < a[mid]) std::swap(a[hi - 1], a[mid]);
    if (a[mid] < a[lo]) std::swap(a[mid], a[lo]);
}

// Quicksort rounds on a[lo..hi) until every part has at most 16 elements. When `depth` rounds have not been enough (a bad
// run of pivots), the part is finished by heapsort instead, so the worst case stays O(n log n).
template <class T>
void introSortLoop(std::vector<T>& a, int lo, int hi, int depth) {
    while (hi - lo > 16) {
        if (depth == 0) { heapSort(a, lo, hi); return; }
        --depth;
        medianOfThree(a, lo, hi);
        int j = partitionHoare(a, lo, hi);
        introSortLoop(a, j + 1, hi, depth);
        hi = j + 1;
    }
}

// Introsort: quicksort with median-of-three pivots and a depth limit of 2 * floor(log2 n), then one insertion sort over the
// whole array. Every element is within 16 places of its final one, so that last pass is linear. Not stable.
template <class T>
void introSort(std::vector<T>& a) {
    int n = (int)a.size();
    introSortLoop(a, 0, n, 2 * (std::bit_width((unsigned)n) - 1));
    insertionSort(a, 0, n);
}

// ---- for the tests and the experiments

struct Rec {                                                    // a record: sorted by key only; id says where it started
    int key, id;
    bool operator<(const Rec& o) const { return key < o.key; }
};
static long long g_less = 0;                                    // counts every comparison an algorithm makes
struct Counted {
    int v;
    bool operator<(const Counted& o) const { ++g_less; return v < o.v; }
};

static std::vector<Rec> toRecs(const Vec& keys) {
    std::vector<Rec> r;
    for (int i = 0; i < (int)keys.size(); ++i) r.push_back({keys[i], i});
    return r;
}
static Vec keysOf(const std::vector<Rec>& r) { Vec k; for (const Rec& x : r) k.push_back(x.key); return k; }
static Vec idsOf(const std::vector<Rec>& r) { Vec k; for (const Rec& x : r) k.push_back(x.id); return k; }

// Run `sorter` on records with the given keys. The keys must come out sorted and the ids must be a permutation;
// for a stable sorter the ids must also equal what std::stable_sort gives.
template <class Sorter>
static void checkSorter(bool stable, const Vec& keys, Sorter sorter) {
    std::vector<Rec> a = toRecs(keys), ref = a;
    std::stable_sort(ref.begin(), ref.end());
    sorter(a);
    CHECK(keysOf(a) == keysOf(ref));
    Vec ids = idsOf(a);
    if (stable) CHECK(ids == idsOf(ref));
    else { std::sort(ids.begin(), ids.end()); CHECK(ids == idsOf(toRecs(keys))); }   // still a permutation
}

static std::vector<Vec> inputs() {                              // every array of length 0..7 over 0..3, then bigger ones
    std::vector<Vec> all;
    for (int len = 0; len <= 7; ++len) {
        int total = 1; for (int i = 0; i < len; ++i) total *= 4;
        for (int code = 0; code < total; ++code) {
            Vec v(len); int c = code;
            for (int i = 0; i < len; ++i) { v[i] = c % 4; c /= 4; }
            all.push_back(v);
        }
    }
    std::mt19937 rng(12345);
    for (int len : {8, 15, 16, 17, 18, 31, 32, 33, 50, 100, 257})
        for (int range : {1, 2, 4, 50, 1000000})
            for (int shape = 0; shape < 4; ++shape) {           // random, sorted, reversed, nearly sorted
                Vec v(len);
                for (int& x : v) x = (int)(rng() % range);
                if (shape == 1 || shape == 2) std::sort(v.begin(), v.end());
                if (shape == 2) std::reverse(v.begin(), v.end());
                if (shape == 3) { std::sort(v.begin(), v.end()); if (len > 3) std::swap(v[1], v[len - 2]); }
                all.push_back(v);
            }
    return all;
}

static long long bruteInversions(const Vec& a) {
    long long c = 0;
    for (size_t i = 0; i < a.size(); ++i) for (size_t j = i + 1; j < a.size(); ++j) c += a[i] > a[j];
    return c;
}

// n values from mt19937(seed), each in [0, 10^6): the input of the comparison-count table
static Vec randomValues(int n, unsigned seed) {
    std::mt19937 rng(seed);
    Vec v(n);
    for (int& x : v) x = (int)(rng() % 1000000);
    return v;
}

template <class Sort>
static long long countLess(const Vec& values, Sort sorter) {
    std::vector<Counted> a;
    for (int v : values) a.push_back({v});
    g_less = 0;
    sorter(a);
    long long used = g_less;
    for (size_t i = 1; i < a.size(); ++i) CHECK(!(a[i].v < a[i - 1].v));
    return used;
}

// Time (best of 3, milliseconds) for 10^6 random ints below 10^9: run as  ./demo --time  (build with -O2, no sanitizer).
static void timings() {
    const int n = 1000000;
    std::mt19937 rng(1);
    Vec data(n);
    for (int& x : data) x = (int)(rng() % 1000000000);
    auto best = [&](const char* name, auto sorter) {
        double bestMs = 1e18;
        for (int rep = 0; rep < 3; ++rep) {
            Vec a = data;
            auto t0 = std::chrono::steady_clock::now();
            sorter(a);
            auto t1 = std::chrono::steady_clock::now();
            bestMs = std::min(bestMs, std::chrono::duration<double, std::milli>(t1 - t0).count());
            if (!std::is_sorted(a.begin(), a.end())) std::printf("NOT SORTED: %s\n", name);
        }
        std::printf("time n=10^6 %-12s %.0f ms\n", name, bestMs);
    };
    best("std::sort", [](Vec& a) { std::sort(a.begin(), a.end()); });
    best("introSort", [](Vec& a) { introSort(a); });
    best("quickSort3", [](Vec& a) { std::mt19937 r(7); quickSort3(a, 0, (int)a.size(), r); });
    best("mergeSort", [](Vec& a) { Vec t(a.size()); mergeSort(a, t, 0, (int)a.size()); });
    best("stable_sort", [](Vec& a) { std::stable_sort(a.begin(), a.end()); });
    best("heapSort", [](Vec& a) { heapSort(a, 0, (int)a.size()); });
    best("radixSort", [](Vec& a) { radixSort(a); });
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--time") == 0) { timings(); return 0; }
    std::vector<Vec> in = inputs();
    for (const Vec& keys : in) {
        int n = (int)keys.size();
        checkSorter(true, keys, [](std::vector<Rec>& a) { insertionSort(a, 0, (int)a.size()); });
        checkSorter(true, keys, [](std::vector<Rec>& a) { std::vector<Rec> t(a.size()); mergeSort(a, t, 0, (int)a.size()); });
        checkSorter(false, keys, [](std::vector<Rec>& a) { quickSortLomuto(a, 0, (int)a.size()); });
        checkSorter(false, keys, [](std::vector<Rec>& a) { quickSortHoare(a, 0, (int)a.size()); });
        checkSorter(false, keys, [](std::vector<Rec>& a) { std::mt19937 r(7); quickSort3(a, 0, (int)a.size(), r); });
        checkSorter(false, keys, [](std::vector<Rec>& a) { heapSort(a, 0, (int)a.size()); });
        checkSorter(false, keys, [](std::vector<Rec>& a) { introSort(a); });
        checkSorter(false, keys, [](std::vector<Rec>& a) {      // force the heapsort fallback after 0 or 1 rounds
            introSortLoop(a, 0, (int)a.size(), 0); insertionSort(a, 0, (int)a.size()); });
        checkSorter(false, keys, [](std::vector<Rec>& a) {
            introSortLoop(a, 0, (int)a.size(), 1); insertionSort(a, 0, (int)a.size()); });
        checkSorter(true, keys, [](std::vector<Rec>& a) {
            int mx = 0; for (const Rec& r : a) mx = std::max(mx, r.key);
            countingSortBy(a, mx + 1, [](const Rec& r) { return r.key; }); });
        {   // sorting a part only leaves the rest alone
            Vec v = keys; if (n >= 4) { heapSort(v, 1, n - 1); CHECK(v[0] == keys[0] && v[n - 1] == keys[n - 1]); CHECK(std::is_sorted(v.begin() + 1, v.end() - 1)); }
        }
        for (int range : {1, 150, 1500, 15000, 790000000}) {   // radix sort with 1 to 9 digits, also with a leading 1
            Vec v = keys; for (int i = 0; i < n; ++i) v[i] = (int)((keys[i] * 7919LL + i * 31LL) % range);
            Vec ref = v; std::sort(ref.begin(), ref.end());
            radixSort(v); CHECK(v == ref);
        }
        {   // inversions against the O(n^2) count
            Vec v = keys, t(keys.size());
            CHECK(countInversions(v, t, 0, n) == bruteInversions(keys));
            CHECK(std::is_sorted(v.begin(), v.end()));
        }
        if (n >= 2) {   // partition post-conditions
            Vec v = keys; int p = partitionLomuto(v, 0, n); int pivot = v[p];
            bool ok = pivot == keys[n - 1];
            for (int i = 0; i < p; ++i) ok = ok && v[i] < pivot;
            for (int i = p + 1; i < n; ++i) ok = ok && v[i] >= pivot;
            CHECK(ok);
            Vec w = keys; int piv = w[(n - 1) / 2]; int j = partitionHoare(w, 0, n);
            bool ok2 = j >= 0 && j <= n - 2;
            for (int i = 0; i <= j && ok2; ++i) ok2 = w[i] <= piv;
            for (int i = j + 1; i < n && ok2; ++i) ok2 = w[i] >= piv;
            CHECK(ok2);
            Vec sv = v, sw = w, sk = keys; std::sort(sv.begin(), sv.end()); std::sort(sw.begin(), sw.end()); std::sort(sk.begin(), sk.end());
            CHECK(sv == sk && sw == sk);
        }
        if (n > 16) {   // depth 0 sorts everything by heapsort; a large depth leaves every element within 15 places
            Vec v = keys; introSortLoop(v, 0, n, 0); CHECK(std::is_sorted(v.begin(), v.end()));
            Vec w = keys, ref = keys; std::sort(ref.begin(), ref.end()); introSortLoop(w, 0, n, 64);
            bool near = true;
            for (int i = 0; i < n; ++i) near = near && ref[std::max(0, i - 15)] <= w[i] && w[i] <= ref[std::min(n - 1, i + 15)];
            CHECK(near);
        }
        if (n >= 3) {
            Vec v = keys; medianOfThree(v, 0, n);
            int mid = (n - 1) / 2; Vec three = {keys[0], keys[mid], keys[n - 1]}; std::sort(three.begin(), three.end());
            CHECK(v[mid] == three[1]);
        }
    }
    // ---- the facts the tutorial quotes
    auto show = [](const Vec& v) { for (size_t i = 0; i < v.size(); ++i) std::printf(i ? " %d" : "%d", v[i]); };
    {
        Vec a = {2, 4, 1, 3, 5}, t(5);
        std::printf("inversions 2,4,1,3,5 = %lld\n", countInversions(a, t, 0, 5));
    }
    {
        Vec a = {3, 8, 2, 5, 1, 4}; int p = partitionLomuto(a, 0, 6);
        std::printf("lomuto 3,8,2,5,1,4 -> p=%d: ", p); show(a); std::printf("\n");
        Vec b = {3, 8, 2, 5, 1, 4}; int j = partitionHoare(b, 0, 6);
        std::printf("hoare 3,8,2,5,1,4 -> j=%d: ", j); show(b); std::printf("\n");
    }
    {
        Vec a = {4, 10, 3, 5, 1}; int n = 5;
        for (int i = n / 2 - 1; i >= 0; --i) siftDown(a, 0, n, i);
        std::printf("heap build 4,10,3,5,1: "); show(a); std::printf("\n");
        for (int end = n - 1; end > 0; --end) {
            std::swap(a[0], a[end]); siftDown(a, 0, end, 0);
            std::printf("heap end=%d: ", end); show(a); std::printf("\n");
        }
    }
    {
        std::vector<Rec> r = toRecs({2, 0, 2, 3, 0, 1});
        countingSortBy(r, 4, [](const Rec& x) { return x.key; });
        std::printf("counting keys 2,0,2,3,0,1 -> ids: "); show(idsOf(r)); std::printf("\n");
        Vec a = {170, 45, 75, 90, 802, 24, 2, 66};
        for (long long place = 1; place <= 100; place *= 10) {
            countingSortBy(a, 10, [&](int x) { return (int)(x / place % 10); });
            std::printf("radix pass %lld: ", place); show(a); std::printf("\n");
        }
    }
    {   // two records with equal keys, ids 0 and 1: the stable sorts keep them in order, the others swap them
        Vec keys = {0, 0};
        auto ids = [&](auto sorter) { std::vector<Rec> a = toRecs(keys); sorter(a); return idsOf(a); };
        std::printf("ids after sorting keys 0,0: insertion "); show(ids([](std::vector<Rec>& a) { insertionSort(a, 0, 2); }));
        std::printf(" | merge "); show(ids([](std::vector<Rec>& a) { std::vector<Rec> t(2); mergeSort(a, t, 0, 2); }));
        std::printf(" | counting "); show(ids([](std::vector<Rec>& a) { countingSortBy(a, 1, [](const Rec& r) { return r.key; }); }));
        std::printf(" | lomuto "); show(ids([](std::vector<Rec>& a) { quickSortLomuto(a, 0, 2); }));
        std::printf(" | hoare "); show(ids([](std::vector<Rec>& a) { quickSortHoare(a, 0, 2); }));
        std::printf(" | heap "); show(ids([](std::vector<Rec>& a) { heapSort(a, 0, 2); }));
        std::printf("\n");
    }
    {   // comparisons for n = 1000 values: random (mt19937 seed 2024, below 10^6), sorted, all equal
        Vec rnd = randomValues(1000, 2024), srt = rnd, eq(1000, 5);
        std::sort(srt.begin(), srt.end());
        auto row = [&](const char* name, auto sorter) {
            std::printf("compares n=1000 %-9s %lld %lld %lld\n", name, countLess(rnd, sorter), countLess(srt, sorter), countLess(eq, sorter));
        };
        row("insertion", [](std::vector<Counted>& a) { insertionSort(a, 0, (int)a.size()); });
        row("merge", [](std::vector<Counted>& a) { std::vector<Counted> t(a.size()); mergeSort(a, t, 0, (int)a.size()); });
        row("lomuto", [](std::vector<Counted>& a) { quickSortLomuto(a, 0, (int)a.size()); });
        row("hoare", [](std::vector<Counted>& a) { quickSortHoare(a, 0, (int)a.size()); });
        row("quick3", [](std::vector<Counted>& a) { std::mt19937 r(7); quickSort3(a, 0, (int)a.size(), r); });
        row("heap", [](std::vector<Counted>& a) { heapSort(a, 0, (int)a.size()); });
        row("intro", [](std::vector<Counted>& a) { introSort(a); });
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
