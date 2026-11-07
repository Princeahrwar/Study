// Tutorial: binary search templates. Every function the tutorial shows is here, followed by exhaustive checks against
// the standard library and a brute-force scan, and the experiments behind every number quoted in the text.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 01_binary_search_templates_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

constexpr size_t kNone = SIZE_MAX;                              // "there is no such index"

// The first index in [lo, hi) where pred is true, or hi if there is none.
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

// The two bounds of a sorted array, and what you can build from them.
size_t lowerBound(const std::vector<int>& a, int x) { return firstTrue<size_t>(0, a.size(), [&](size_t i) { return a[i] >= x; }); }
size_t upperBound(const std::vector<int>& a, int x) { return firstTrue<size_t>(0, a.size(), [&](size_t i) { return a[i] > x; }); }

bool contains(const std::vector<int>& a, int x) { size_t i = lowerBound(a, x); return i < a.size() && a[i] == x; }

size_t countOf(const std::vector<int>& a, int x) { return upperBound(a, x) - lowerBound(a, x); }

size_t largestAtMost(const std::vector<int>& a, int x) { size_t u = upperBound(a, x); return u == 0 ? kNone : u - 1; }

size_t smallestAtLeast(const std::vector<int>& a, int x) { size_t l = lowerBound(a, x); return l == a.size() ? kNone : l; }

// A search over values, not over an array: the largest r with r * r <= x. The predicate is "r * r > x".
// 46340 * 46340 <= INT_MAX < 46341 * 46341, so the candidates are 0 .. 46340 and 46341 means "none is too big".
int isqrt(int x) { return (int)firstTrue<int64_t>(0, 46341, [&](int64_t r) { return r * r > x; }) - 1; }

// The classic version: look for an exact match in [lo, hi]. It returns some matching index, not necessarily the first.
int binarySearchClosed(const std::vector<int>& a, int x) {
    int lo = 0, hi = (int)a.size() - 1;
    while (lo <= hi) {                                          // invariant: if x is in the array, it is in [lo, hi]
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == x) return mid;
        if (a[mid] < x) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

// The last index in [0, n) where pred is true, or -1. pred must be true, ..., true, false, ..., false.
// The middle must round UP here, or the loop can stall.
template <class Pred>
int lastTrue(int n, Pred pred) {
    int lo = -1, hi = n - 1;                                    // invariant: the answer is in [lo, hi]
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;                       // rounds up: lo < mid <= hi
        if (pred(mid)) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

// What (lo + hi) / 2 does with 32-bit ints once the sum passes 2^31 - 1. The sum is done in unsigned arithmetic here
// so that the wrap-around is defined; with plain int it is undefined behaviour, and in practice it wraps the same way.
int32_t midWrapped(int32_t lo, int32_t hi) { return (int32_t)((uint32_t)lo + (uint32_t)hi) / 2; }
int32_t midSafe(int32_t lo, int32_t hi) { return lo + (hi - lo) / 2; }

// The same answer as lowerBound, written so that the loop runs ceil(log2 n) times whatever the data is and needs no branch.
size_t lowerBoundBranchless(const int* a, size_t n, int x) {
    if (n == 0) return 0;
    const int* base = a;                                        // invariant: a[0 .. base - 1] are < x, and the answer is in [base, base + n]
    while (n > 1) {
        size_t half = n / 2;
        base = (base[half] < x) ? base + half : base;           // a select: the compiler may emit a conditional move or a branch
        n -= half;
    }
    return (size_t)(base - a) + (*base < x);
}

// ================================================================ tests and experiments
static size_t floorLog2(size_t n) {
    size_t r = 0;
    while (n >>= 1) ++r;
    return r;
}

// Test-only: a tiny deterministic random generator (xorshift64*), the same on every compiler.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }
    uint64_t below(uint64_t n) { return next() % n; }
};

int main() {
    // ---------------------------------------------------------------- firstTrue on an index range, against std::partition_point
    {
        size_t arrays = 0;
        for (size_t n = 0; n <= 40; ++n) {
            std::vector<size_t> idx(n);
            std::iota(idx.begin(), idx.end(), size_t(0));
            for (size_t b = 0; b <= n; ++b) {
                size_t calls = 0;
                bool inRange = true;
                auto pred = [&](size_t i) { ++calls; if (i >= n) inRange = false; return i >= b; };
                size_t r = firstTrue<size_t>(0, n, pred);
                CHECK(r == b);
                CHECK(inRange);                                   // pred is only ever asked about indices inside the range
                auto it = std::partition_point(idx.begin(), idx.end(), [&](size_t i) { return i < b; });
                CHECK((size_t)(it - idx.begin()) == r);           // firstTrue(p) is partition_point(not p)
                CHECK(n == 0 ? calls == 0 : calls <= floorLog2(n) + 1);
                ++arrays;
            }
        }
        // sub-ranges: the answer is the boundary pulled into [lo, hi]
        for (size_t n = 0; n <= 12; ++n)
            for (size_t lo = 0; lo <= n; ++lo)
                for (size_t hi = lo; hi <= n; ++hi)
                    for (size_t b = 0; b <= n; ++b)
                        CHECK(firstTrue<size_t>(lo, hi, [&](size_t i) { return i >= b; }) == std::min(std::max(b, lo), hi));
        std::printf("firstTrue boundaries tested=%zu\n", arrays);
    }

    // ---------------------------------------------------------------- how many probes
    {
        const size_t sizes[] = {1, 2, 3, 4, 7, 8, 9, 1000, 1000000};
        for (size_t n = 1; n <= 2000; ++n) {
            size_t worst = 0, best = SIZE_MAX;
            for (size_t b = 0; b <= n; ++b) {
                size_t calls = 0;
                firstTrue<size_t>(0, n, [&](size_t i) { ++calls; return i >= b; });
                worst = std::max(worst, calls);
                best = std::min(best, calls);
            }
            CHECK(worst == floorLog2(n) + 1);
            CHECK(best >= 1);
        }
        for (size_t n : sizes) {
            size_t worst = 0;
            const size_t step = n > 5000 ? 7 : 1;                 // every boundary for small n, every 7th for the big one
            for (size_t b = 0; b <= n; b += step) {
                size_t calls = 0;
                firstTrue<size_t>(0, n, [&](size_t i) { ++calls; return i >= b; });
                worst = std::max(worst, calls);
            }
            CHECK(worst == floorLog2(n) + 1);
            std::printf("probes n=%zu worst=%zu\n", n, worst);
        }
    }

    // ---------------------------------------------------------------- the invariant holds even when pred is not monotone
    {
        size_t predicates = 0;
        for (int n = 0; n <= 10; ++n)
            for (unsigned mask = 0; mask < (1u << n); ++mask) {
                auto p = [&](size_t i) { return (mask >> i & 1) != 0; };
                size_t r = firstTrue<size_t>(0, (size_t)n, p);
                CHECK(r == 0 || !p(r - 1));                       // false just below the result ...
                CHECK(r == (size_t)n || p(r));                    // ... and true at it: a boundary, though not necessarily the first
                ++predicates;
            }
        std::printf("non-monotone predicates tested=%zu\n", predicates);
    }

    // ---------------------------------------------------------------- every sorted array of length <= 6 over 0..3, every query
    {
        size_t arraysTested = 0, queries = 0;
        for (int n = 0; n <= 6; ++n) {
            int total = 1;
            for (int i = 0; i < n; ++i) total *= 4;
            for (int code = 0; code < total; ++code) {
                std::vector<int> a(n);
                int c = code;
                for (int i = 0; i < n; ++i) { a[i] = c % 4; c /= 4; }
                if (!std::is_sorted(a.begin(), a.end())) continue;
                ++arraysTested;
                for (int x = -1; x <= 4; ++x) {
                    ++queries;
                    size_t lo = (size_t)(std::lower_bound(a.begin(), a.end(), x) - a.begin());
                    size_t hi = (size_t)(std::upper_bound(a.begin(), a.end(), x) - a.begin());
                    size_t refFirstGE = kNone, refLastLE = kNone;                 // brute force: scan
                    for (size_t i = 0; i < a.size(); ++i) {
                        if (refFirstGE == kNone && a[i] >= x) refFirstGE = i;
                        if (a[i] <= x) refLastLE = i;
                    }
                    CHECK(lowerBound(a, x) == lo);
                    CHECK(upperBound(a, x) == hi);
                    CHECK(contains(a, x) == std::binary_search(a.begin(), a.end(), x));
                    CHECK(countOf(a, x) == (size_t)std::count(a.begin(), a.end(), x));
                    CHECK(largestAtMost(a, x) == refLastLE);
                    CHECK(smallestAtLeast(a, x) == refFirstGE);
                    auto er = std::equal_range(a.begin(), a.end(), x);
                    CHECK((size_t)(er.first - a.begin()) == lo && (size_t)(er.second - a.begin()) == hi);
                    int idx = binarySearchClosed(a, x);
                    if (contains(a, x)) CHECK(idx >= 0 && a[idx] == x);
                    else CHECK(idx == -1);
                    CHECK(lastTrue((int)a.size(), [&](int i) { return a[i] <= x; }) == (int)hi - 1);
                    CHECK(lowerBoundBranchless(a.data(), a.size(), x) == lo);
                }
            }
        }
        std::printf("exhaustive arrays=%zu queries=%zu\n", arraysTested, queries);
    }

    // ---------------------------------------------------------------- random arrays with many duplicates, against the library
    {
        Rng rng(2024);
        for (int round = 0; round < 300; ++round) {
            size_t n = rng.below(2000);
            int range = 1 + (int)rng.below(round % 2 ? 50 : 100000);
            std::vector<int> a(n);
            for (int& v : a) v = (int)rng.below(range);
            std::sort(a.begin(), a.end());
            for (int q = 0; q < 100; ++q) {
                int x = (int)rng.below(range + 2) - 1;
                CHECK(lowerBound(a, x) == (size_t)(std::lower_bound(a.begin(), a.end(), x) - a.begin()));
                CHECK(upperBound(a, x) == (size_t)(std::upper_bound(a.begin(), a.end(), x) - a.begin()));
                CHECK(lowerBoundBranchless(a.data(), a.size(), x) == lowerBound(a, x));
            }
        }
    }

    // ---------------------------------------------------------------- the array of the text
    {
        std::vector<int> a = {1, 3, 3, 5, 8, 8, 8, 10};
        auto trace = [&](const char* name, int x, bool upper) {
            std::printf("trace %s x=%d probes", name, x);
            size_t r = firstTrue<size_t>(0, a.size(), [&](size_t i) {
                bool t = upper ? a[i] > x : a[i] >= x;
                std::printf(" %zu:%c", i, t ? 'T' : 'F');
                return t;
            });
            std::printf(" -> %zu\n", r);
            CHECK(r == (upper ? upperBound(a, x) : lowerBound(a, x)));
        };
        trace("lower", 8, false);
        trace("upper", 8, true);
        trace("lower", 4, false);
        trace("lower", 11, false);
        const int xs[] = {0, 1, 3, 4, 8, 10, 11};
        for (int x : xs) {
            size_t fl = largestAtMost(a, x), ce = smallestAtLeast(a, x);
            std::printf("query x=%d lower=%zu upper=%zu count=%zu contains=%d floor=%s ceil=%s\n", x, lowerBound(a, x), upperBound(a, x), countOf(a, x), (int)contains(a, x),
                        fl == kNone ? "none" : std::to_string(a[fl]).c_str(), ce == kNone ? "none" : std::to_string(a[ce]).c_str());
        }
        std::printf("closed x=8 index=%d first=%zu last=%zu\n", binarySearchClosed(a, 8), lowerBound(a, 8), upperBound(a, 8) - 1);
        CHECK(binarySearchClosed(a, 8) == 5);
        std::printf("closed x=3 index=%d first=%zu last=%zu\n", binarySearchClosed(a, 3), lowerBound(a, 3), upperBound(a, 3) - 1);
        std::vector<int> five = {2, 3, 3, 3, 3, 9};
        std::printf("closed five x=3 index=%d first=%zu last=%zu\n", binarySearchClosed(five, 3), lowerBound(five, 3), upperBound(five, 3) - 1);
        CHECK(binarySearchClosed(a, 3) == 1);
    }

    // ---------------------------------------------------------------- empty and one-element arrays
    {
        std::vector<int> e, one = {5};
        CHECK(lowerBound(e, 7) == 0 && upperBound(e, 7) == 0 && !contains(e, 7) && countOf(e, 7) == 0);
        CHECK(largestAtMost(e, 7) == kNone && smallestAtLeast(e, 7) == kNone && binarySearchClosed(e, 7) == -1);
        CHECK(lowerBoundBranchless(e.data(), 0, 7) == 0);
        CHECK(lastTrue(0, [](int) { return true; }) == -1);
        CHECK(lowerBound(one, 5) == 0 && upperBound(one, 5) == 1 && contains(one, 5) && !contains(one, 4));
        CHECK(lowerBoundBranchless(one.data(), 1, 6) == 1 && lowerBoundBranchless(one.data(), 1, 5) == 0);
    }

    // ---------------------------------------------------------------- the middle: overflow and stalls
    {
        std::printf("mid lo=1500000000 hi=2000000000 wrapped=%d safe=%d\n", midWrapped(1500000000, 2000000000), midSafe(1500000000, 2000000000));
        CHECK(midWrapped(1500000000, 2000000000) == -397483648);
        CHECK(midSafe(1500000000, 2000000000) == 1750000000);
        CHECK(midWrapped(10, 20) == 15 && midSafe(10, 20) == 15);
        // the safe middle never leaves [lo, hi): check it on the extremes of int32
        const int32_t edges[] = {0, 1, 2, 1073741823, 1073741824, 1500000000, 2147483646, INT32_MAX};
        for (int32_t lo : edges)
            for (int32_t hi : edges)
                if (lo < hi) { int32_t m = midSafe(lo, hi); CHECK(m >= lo && m < hi); CHECK(m == std::midpoint(lo, hi)); }   // C++20's std::midpoint agrees
        // firstTrue over the whole positive int32 range
        int calls = 0;
        bool ok = true;
        int32_t r = firstTrue<int32_t>(0, INT32_MAX, [&](int32_t i) { ++calls; if (i < 0) ok = false; return i >= 2000000000; });
        CHECK(r == 2000000000 && ok && calls <= 31);
        std::printf("int32 range threshold=2000000000 result=%d probes=%d\n", r, calls);
        calls = 0;
        int64_t big = firstTrue<int64_t>(0, int64_t(1) << 62, [&](int64_t i) { ++calls; return i >= 4000000000000000000LL; });
        CHECK(big == 4000000000000000000LL && calls <= 63);
        CHECK(firstTrue<int32_t>(0, INT32_MAX, [](int32_t) { return false; }) == INT32_MAX);
        // a round-down middle with "lo = mid" stalls: lo = 2, hi = 3 gives mid = 2 again, forever
        {
            int lo = 2, hi = 3, steps = 0;
            while (lo < hi && steps < 20) {
                int mid = lo + (hi - lo) / 2;
                bool p = true;                                    // pred(2) is true
                if (p) lo = mid; else hi = mid - 1;
                ++steps;
            }
            CHECK(steps == 20 && lo == 2 && hi == 3);
            std::printf("roundDown stalls lo=%d hi=%d after %d steps\n", lo, hi, steps);
        }
    }

    // ---------------------------------------------------------------- square roots by searching values
    {
        for (int x = 0; x <= 20000; ++x) {
            long long r = isqrt(x);
            CHECK(r * r <= x && (r + 1) * (r + 1) > x);
        }
        const int big[] = {INT_MAX, INT_MAX - 1, 2147395600, 2147395599, 1000000007};
        for (int x : big) { long long r = isqrt(x); CHECK(r * r <= x && (r + 1) * (r + 1) > x); }
        std::printf("isqrt 8=%d 15=%d 16=%d INT_MAX=%d\n", isqrt(8), isqrt(15), isqrt(16), isqrt(INT_MAX));
        CHECK(isqrt(INT_MAX) == 46340);
    }

    // ---------------------------------------------------------------- the branchless loop does the same work every time
    {
        Rng rng(7);
        for (size_t n = 0; n <= 70; ++n) {
            std::vector<int> a(n);
            for (int& v : a) v = (int)rng.below(20);
            std::sort(a.begin(), a.end());
            for (int x = -1; x <= 21; ++x) CHECK(lowerBoundBranchless(a.data(), n, x) == lowerBound(a, x));
        }
        std::vector<int> same(100, 4);
        CHECK(lowerBoundBranchless(same.data(), 100, 4) == 0 && lowerBoundBranchless(same.data(), 100, 5) == 100 && lowerBoundBranchless(same.data(), 100, 3) == 0);
    }

    // ---------------------------------------------------------------- timing (only when asked; the numbers vary from run to run)
    if (std::getenv("BS01_TIMING")) {
        const size_t sizes[] = {size_t(1) << 10, size_t(1) << 16, size_t(1) << 22, size_t(1) << 25};
        for (size_t n : sizes) {
            std::vector<int> a(n);
            for (size_t i = 0; i < n; ++i) a[i] = (int)(2 * i);        // distinct even numbers: queries hit and miss
            const size_t Q = 1 << 20;
            std::vector<int> qs(Q);
            Rng rng(99);
            for (int& q : qs) q = (int)rng.below(2 * n);
            auto nsPer = [&](auto fn) {
                size_t sink = 0;
                double best = 1e18;
                for (int rep = 0; rep < 3; ++rep) {
                    auto t0 = std::chrono::steady_clock::now();
                    for (int q : qs) sink += fn(q);
                    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                    best = std::min(best, s);
                }
                if (sink == 12345) std::printf(" ");
                return best / (double)Q * 1e9;
            };
            double tOwn = nsPer([&](int q) { return lowerBound(a, q); });
            double tStd = nsPer([&](int q) { return (size_t)(std::lower_bound(a.begin(), a.end(), q) - a.begin()); });
            double tBl = nsPer([&](int q) { return lowerBoundBranchless(a.data(), n, q); });
            std::printf("timing n=%zu firstTrue %.1f ns, std::lower_bound %.1f ns, branchless %.1f ns\n", n, tOwn, tStd, tBl);
        }
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
