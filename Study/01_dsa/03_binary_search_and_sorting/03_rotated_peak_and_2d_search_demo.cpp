// Tutorial: rotated, peak and 2D search. The functions shown in the tutorial, checked against brute force on every
// small input, plus the few measurements the text quotes.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 03_rotated_peak_and_2d_search_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

using Vec = std::vector<int>;
using Matrix = std::vector<std::vector<int>>;

// The first value in [lo, hi) where pred is true, or hi if there is none (tutorial 01).
// pred must be false, false, ..., false, true, ..., true on the range.
template <class Index, class Pred>
Index firstTrue(Index lo, Index hi, Pred pred) {
    while (lo < hi) {                                           // invariant: pred(lo - 1) is false and pred(hi) is true, where those exist
        Index mid = lo + (hi - lo) / 2;                         // lo <= mid < hi
        if (pred(mid)) hi = mid;                                // mid may be the answer: keep it
        else lo = mid + 1;                                      // mid is false: the answer is after it
    }
    return lo;                                                  // lo == hi
}

// ---------------------------------------------------------------- 1. Rotated arrays

// The index of the smallest element of a rotated sorted array with distinct values: where the sorted order starts.
// Before it every element is larger than the last one, from it on every element is at most the last one.
int rotationStart(const Vec& a) {
    return firstTrue<int>(0, (int)a.size(), [&](int i) { return a[i] <= a.back(); });
}

// The index of x in a rotated sorted array with distinct values, or -1. Search the sorted order
// b[k] = a[(start + k) % n] without building it.
int searchRotated(const Vec& a, int x) {
    int n = (int)a.size(), start = rotationStart(a);
    int k = firstTrue<int>(0, n, [&](int j) { return a[(start + j) % n] >= x; });
    if (k < n && a[(start + k) % n] == x) return (start + k) % n;
    return -1;
}

// The smallest value of a rotated sorted array that may contain duplicates. Compare a[mid] with a[hi].
int minWithDuplicates(const Vec& a) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] > a[hi]) lo = mid + 1;                       // mid is in the left part, the minimum is right of it
        else if (a[mid] < a[hi]) hi = mid;                      // mid is in the right part: it may be the minimum
        else --hi;                                              // equal: a[mid] has the same value, so dropping a[hi] loses nothing
    }
    return a[lo];
}

// Is x in a rotated sorted array that may contain duplicates? Closed interval. One half of [lo, hi] is sorted, unless
// a[lo] == a[mid] == a[hi]: then nothing can be said and both ends are trimmed.
bool containsRotatedDup(const Vec& a, int x) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == x) return true;
        if (a[lo] == a[mid] && a[mid] == a[hi]) { ++lo; --hi; }
        else if (a[lo] <= a[mid]) {                             // a[lo..mid] is sorted
            if (a[lo] <= x && x < a[mid]) hi = mid - 1;
            else lo = mid + 1;
        } else {                                                // a[mid..hi] is sorted
            if (a[mid] < x && x <= a[hi]) lo = mid + 1;
            else hi = mid - 1;
        }
    }
    return false;
}

// ---------------------------------------------------------------- 2. Peaks and pairs

// The index of a peak: an element larger than both neighbours, where outside the array counts as minus infinity.
// Neighbouring elements differ. a[i] > a[i + 1] is only asked for i < n - 1; if every answer is no, a[n - 1] is the peak.
int findPeak(const Vec& a) {
    return firstTrue<int>(0, (int)a.size() - 1, [&](int i) { return a[i] > a[i + 1]; });
}

// The value that appears once in a sorted array (odd length) where every other value appears twice. Pair q is
// a[2q], a[2q + 1]: before the single value every pair matches, from its position on no pair does. If every pair
// matches, the single value is the last element, which firstTrue reports as "none" (q = n / 2).
int singleElement(const Vec& a) {
    int q = firstTrue<int>(0, (int)a.size() / 2, [&](int p) { return a[2 * p] != a[2 * p + 1]; });
    return a[2 * q];
}

// ---------------------------------------------------------------- 3. Matrices

// Each row sorted and the first entry of a row above the last entry of the row before: the matrix is one sorted array
// of rows * cols entries, read by row.
bool containsFlat(const Matrix& m, int x) {
    int rows = (int)m.size(), cols = (int)m[0].size();
    int k = firstTrue<int>(0, rows * cols, [&](int j) { return m[j / cols][j % cols] >= x; });
    return k < rows * cols && m[k / cols][k % cols] == x;
}

// Rows sorted and columns sorted. Start at the top-right corner. An entry larger than x: everything below it is larger
// too, so drop its column. An entry smaller than x: everything left of it is smaller, so drop its row.
bool containsStaircase(const Matrix& m, int x) {
    int r = 0, c = (int)m[0].size() - 1;
    while (r < (int)m.size() && c >= 0) {
        if (m[r][c] == x) return true;
        if (m[r][c] > x) --c;
        else ++r;
    }
    return false;
}

// ---------------------------------------------------------------- 4. Two sorted arrays

// The k-th smallest (k from 1) of two sorted arrays. Take t elements from a and k - t from b. The split is right when
// a[t - 1] <= b[k - t] and b[k - t - 1] <= a[t]. As t grows, a[t] grows and b[k - t - 1] shrinks, so
// "a[t] >= b[k - t - 1]" goes false, ..., false, true, ..., true. Outside the arrays: -infinity left, +infinity right.
int kthOfTwo(const Vec& a, const Vec& b, int k) {
    int m = (int)a.size(), n = (int)b.size();
    auto A = [&](int i) { return i < 0 ? INT_MIN : i >= m ? INT_MAX : a[i]; };
    auto B = [&](int j) { return j < 0 ? INT_MIN : j >= n ? INT_MAX : b[j]; };
    int lo = std::max(0, k - n), hi = std::min(k, m);           // t is in [lo, hi], and the test is true at hi
    int t = firstTrue<int>(lo, hi, [&](int s) { return A(s) >= B(k - s - 1); });   // no smaller t works: t = hi
    return std::max(A(t - 1), B(k - t - 1));
}

// The median of two sorted arrays (not both empty): the middle value, or the mean of the two middle values.
double medianOfTwo(const Vec& a, const Vec& b) {
    int total = (int)(a.size() + b.size());
    int low = kthOfTwo(a, b, (total + 1) / 2);
    if (total % 2 == 1) return low;
    return (low + (double)kthOfTwo(a, b, total / 2 + 1)) / 2;
}

// ---------------------------------------------------------------- 5. Unimodal functions and unknown sizes

// The point in [lo, hi] where f is largest, for an f that strictly rises and then strictly falls. Each round compares
// f at the two thirds and drops the outer third that cannot hold the top.
template <class F>
double ternaryMax(double lo, double hi, int rounds, F f) {
    for (int i = 0; i < rounds; ++i) {
        double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
        if (f(m1) < f(m2)) lo = m1;                             // the top is right of m1
        else hi = m2;                                           // the top is left of m2
    }
    return lo + (hi - lo) / 2;
}

// The first index in [0, n] whose element is >= x, for a sorted sequence read through get(i). Doubling brackets the
// answer p in (bound / 2, bound] with about log2(p) reads and a binary search inside needs about log2(p) more: the cost
// follows the position of the answer, not n. (Use 64-bit indexes when n is near 2^31.)
template <class Get>
int gallopLowerBound(int n, Get get, int x) {
    if (n == 0 || get(0) >= x) return 0;
    int bound = 1;                                              // get(bound / 2) < x is known (get(0) when bound = 1)
    while (bound < n && get(bound) < x) bound *= 2;
    return firstTrue<int>(bound / 2 + 1, std::min(bound, n), [&](int i) { return get(i) >= x; });
}

// ---- for the tests and the experiments

template <class Pred>
std::string probes(int lo, int hi, Pred pred) {                 // "mid:T mid:F ... -> result"
    std::string s;
    int r = firstTrue<int>(lo, hi, [&](int i) {
        bool v = pred(i);
        s += std::to_string(i) + (v ? ":T " : ":F ");
        return v;
    });
    return s + "-> " + std::to_string(r);
}

static std::vector<Vec> sortedSeqs(int len, int maxVal) {       // all non-decreasing sequences of the given length over 0..maxVal
    std::vector<Vec> out;
    Vec cur(len, 0);
    for (;;) {
        out.push_back(cur);
        int i = len - 1;
        while (i >= 0 && cur[i] == maxVal) --i;
        if (i < 0) break;
        ++cur[i];
        for (int j = i + 1; j < len; ++j) cur[j] = cur[i];
    }
    return out;
}

static Vec rotated(const Vec& a, int r) {                       // move the first r elements to the back
    Vec b(a.size());
    for (size_t i = 0; i < a.size(); ++i) b[i] = a[(i + r) % a.size()];
    return b;
}

static bool isPeak(const Vec& a, int i) {
    return (i == 0 || a[i - 1] < a[i]) && (i == (int)a.size() - 1 || a[i + 1] < a[i]);
}

static Matrix reshape(const Vec& flat, int rows, int cols) {
    Matrix m(rows, Vec(cols));
    for (int i = 0; i < rows * cols; ++i) m[i / cols][i % cols] = flat[i];
    return m;
}

static bool hasValue(const Matrix& m, int x) {
    for (auto& row : m) if (std::find(row.begin(), row.end(), x) != row.end()) return true;
    return false;
}

int main() {
    // --- rotated, distinct values: every subset of 0..9 of size 1..7, every rotation, every target
    for (int mask = 1; mask < (1 << 10); ++mask) {
        Vec base;
        for (int v = 0; v < 10; ++v) if (mask >> v & 1) base.push_back(v);
        if (base.size() > 7) continue;
        for (size_t r = 0; r < base.size(); ++r) {
            Vec a = rotated(base, (int)r);
            int mn = (int)(std::min_element(a.begin(), a.end()) - a.begin());
            CHECK(rotationStart(a) == mn);
            for (int x = -1; x <= 10; ++x) {
                auto it = std::find(a.begin(), a.end(), x);
                CHECK(searchRotated(a, x) == (it == a.end() ? -1 : (int)(it - a.begin())));
            }
        }
    }
    // --- rotated with duplicates: every non-decreasing sequence of length 1..8 over 0..3, every rotation, every target
    for (int len = 1; len <= 8; ++len)
        for (const Vec& base : sortedSeqs(len, 3))
            for (int r = 0; r < len; ++r) {
                Vec a = rotated(base, r);
                CHECK(minWithDuplicates(a) == *std::min_element(a.begin(), a.end()));
                for (int x = -1; x <= 4; ++x)
                    CHECK(containsRotatedDup(a, x) == (std::find(a.begin(), a.end(), x) != a.end()));
            }
    // --- peaks: every array of length 1..8 over 0..3 with different neighbours
    for (int len = 1; len <= 8; ++len) {
        int total = 1;
        for (int i = 0; i < len; ++i) total *= 4;
        for (int code = 0; code < total; ++code) {
            Vec a(len);
            for (int i = 0, c = code; i < len; ++i, c /= 4) a[i] = c % 4;
            bool ok = true;
            for (int i = 0; i + 1 < len; ++i) ok = ok && a[i] != a[i + 1];
            if (ok) CHECK(isPeak(a, findPeak(a)));
        }
    }
    // --- the single element: n odd up to 13, the single value at every position
    for (int n = 1; n <= 13; n += 2)
        for (int s = 0; s < (n + 1) / 2; ++s) {
            Vec a;
            for (int v = 0; v < (n + 1) / 2; ++v) {
                a.push_back(v * 3);
                if (v != s) a.push_back(v * 3);
            }
            CHECK(singleElement(a) == s * 3);
        }
    // --- matrices: every sorted flat array over 0..4 as 1x1 to 3x3; every sorted-rows-and-columns matrix over 0..3 up to 3x3
    for (int rows = 1; rows <= 3; ++rows)
        for (int cols = 1; cols <= 3; ++cols) {
            for (const Vec& flat : sortedSeqs(rows * cols, 4)) {
                Matrix m = reshape(flat, rows, cols);
                for (int x = -1; x <= 5; ++x) CHECK(containsFlat(m, x) == hasValue(m, x));
            }
            int cells = rows * cols, total = 1;
            for (int i = 0; i < cells; ++i) total *= 4;
            for (int code = 0; code < total; ++code) {
                Vec flat(cells);
                for (int i = 0, c = code; i < cells; ++i, c /= 4) flat[i] = c % 4;
                Matrix m = reshape(flat, rows, cols);
                bool sorted = true;
                for (int r = 0; r < rows; ++r)
                    for (int c = 0; c < cols; ++c) {
                        if (c + 1 < cols && m[r][c] > m[r][c + 1]) sorted = false;
                        if (r + 1 < rows && m[r][c] > m[r + 1][c]) sorted = false;
                    }
                if (!sorted) continue;
                for (int x = -1; x <= 4; ++x) CHECK(containsStaircase(m, x) == hasValue(m, x));
            }
        }
    // --- two sorted arrays: every pair of non-decreasing arrays of length 0..4 over 0..4, every k, and the median
    {
        std::vector<Vec> all;
        for (int len = 0; len <= 4; ++len) for (const Vec& s : sortedSeqs(len, 4)) all.push_back(s);
        for (const Vec& a : all)
            for (const Vec& b : all) {
                if (a.empty() && b.empty()) continue;
                Vec merged = a;
                merged.insert(merged.end(), b.begin(), b.end());
                std::sort(merged.begin(), merged.end());
                int total = (int)merged.size();
                for (int k = 1; k <= total; ++k) CHECK(kthOfTwo(a, b, k) == merged[k - 1]);
                double want = total % 2 ? merged[total / 2] : (merged[total / 2 - 1] + merged[total / 2]) / 2.0;
                CHECK(medianOfTwo(a, b) == want);
            }
    }
    // --- ternary search on a few parabolas and on the decreasing-then-increasing mirror
    for (double c : {-3.0, 0.0, 0.7, 2.5, 9.99}) {
        double x = ternaryMax(-10, 10, 100, [&](double t) { return -(t - c) * (t - c); });
        CHECK(std::fabs(x - c) < 1e-9);
    }
    // --- exponential search against lower_bound, on every non-decreasing array of length 0..8 over 0..4
    for (int len = 0; len <= 8; ++len)
        for (const Vec& a : sortedSeqs(len, 4))
            for (int x = -1; x <= 5; ++x) {
                int want = (int)(std::lower_bound(a.begin(), a.end(), x) - a.begin());
                CHECK(gallopLowerBound(len, [&](int i) { return a[i]; }, x) == want);
            }

    // --- the numbers quoted in the text
    {
        Vec a = {4, 5, 6, 7, 0, 1, 2};
        std::printf("rotated 4,5,6,7,0,1,2 start probes %s\n", probes(0, 7, [&](int i) { return a[i] <= a.back(); }).c_str());
        std::printf("rotated search x=5 -> %d, x=3 -> %d\n", searchRotated(a, 5), searchRotated(a, 3));
        Vec d = {1, 0, 1, 1, 1};
        std::printf("duplicates 1,0,1,1,1: a[i] <= a.back() starts at %d, the minimum is at %d, minWithDuplicates = %d\n",
                    rotationStart(d), (int)(std::min_element(d.begin(), d.end()) - d.begin()), minWithDuplicates(d));
        Vec p = {1, 2, 1, 3, 5, 6, 4};
        std::printf("peak 1,2,1,3,5,6,4 probes %s\n", probes(0, 6, [&](int i) { return p[i] > p[i + 1]; }).c_str());
        Vec s = {1, 1, 2, 3, 3, 4, 4, 8, 8};
        std::printf("single 1,1,2,3,3,4,4,8,8 -> %d\n", singleElement(s));
        Vec A = {1, 3, 8, 9, 15}, B = {7, 11, 18, 19, 21, 25};
        auto Af = [&](int i) { return i < 0 ? INT_MIN : i >= 5 ? INT_MAX : A[i]; };
        auto Bf = [&](int j) { return j < 0 ? INT_MIN : j >= 6 ? INT_MAX : B[j]; };
        std::printf("two arrays k=6 probes %s; median = %g\n", probes(0, 5, [&](int t) { return Af(t) >= Bf(6 - t - 1); }).c_str(), medianOfTwo(A, B));
        Vec lo1 = {2, 4, 6}, hi1 = {1, 3, 5, 7, 9};
        auto Cf = [&](int i) { return i < 0 ? INT_MIN : i >= 3 ? INT_MAX : lo1[i]; };
        auto Df = [&](int j) { return j < 0 ? INT_MIN : j >= 5 ? INT_MAX : hi1[j]; };
        std::printf("two arrays 2,4,6 and 1,3,5,7,9 k=4 probes %s; kth = %d\n", probes(0, 3, [&](int t) { return Cf(t) >= Df(4 - t - 1); }).c_str(), kthOfTwo(lo1, hi1, 4));
    }
    {
        for (int n : {1000000, 1000000000}) {
            for (int pos : {10, 1000, 100000, 999999}) {
                if (pos >= n) continue;
                long probesUsed = 0;
                int x = pos;                                     // a[i] = i, so the answer is x
                int r = gallopLowerBound(n, [&](int i) { ++probesUsed; return i; }, x);
                CHECK(r == x);
                std::printf("gallop n=%d answer at %d: %ld reads (plain binary search: %d)\n", n, pos, probesUsed, (int)std::floor(std::log2(n)) + 1);
            }
        }
    }
    {
        // ternary search: rounds needed for a width of 20 to reach 1e-9, and what a large constant does to the precision
        int need = (int)std::ceil(std::log(20 / 1e-9) / std::log(1.5));
        double x0 = ternaryMax(-10, 10, need, [](double t) { return -(t - 0.3) * (t - 0.3); });
        double x1 = ternaryMax(-10, 10, 200, [](double t) { return 1000 - (t - 0.3) * (t - 0.3); });
        std::printf("ternary: %d rounds for width 20 -> 1e-9; error with f = -(x-0.3)^2: %.1e; with f = 1000 - (x-0.3)^2 after 200 rounds: %.1e\n",
                    need, std::fabs(x0 - 0.3), std::fabs(x1 - 0.3));
        CHECK(std::fabs(x0 - 0.3) < 1e-9);
    }
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
