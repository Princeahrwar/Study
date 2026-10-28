// Tutorial: binary search on the answer. Every function the tutorial shows is here, followed by checks against brute
// force and independent dynamic programs, and the experiments behind every number quoted in the text.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 02_binary_search_on_the_answer_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
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

// ---------------------------------------------------------------- 1. Koko eating bananas (LeetCode 875)

// Hours needed to eat every pile at k bananas per hour. 64-bit: 10^4 piles of 10^9 at speed 1 take 10^13 hours.
int64_t hoursNeeded(const std::vector<int>& piles, int64_t k) {
    int64_t hours = 0;
    for (int p : piles) hours += (p + k - 1) / k;               // ceil(p / k)
    return hours;
}

// The smallest speed that finishes within h hours. Speed max(piles) always works if h >= number of piles
// (one hour per pile), so the answer is in [1, max]. If h is too small, the result is max + 1: "none".
int64_t minEatingSpeed(const std::vector<int>& piles, int64_t h) {
    int64_t top = *std::max_element(piles.begin(), piles.end());
    return firstTrue<int64_t>(1, top + 1, [&](int64_t k) { return hoursNeeded(piles, k) <= h; });
}

// ---------------------------------------------------------------- 2. Ship packages within D days (LeetCode 1011)

// Days needed to ship the packages in the given order with a ship of capacity c: load each day as much as fits.
// Only meaningful when c is at least the heaviest package.
int daysNeeded(const std::vector<int>& w, int64_t c) {
    int days = 1;
    int64_t load = 0;
    for (int x : w) {
        if (load + x > c) { ++days; load = 0; }                 // x does not fit today: start the next day
        load += x;
    }
    return days;
}

// The smallest capacity that ships everything within `days` days. It cannot be below the heaviest package
// and never needs to be above the total weight (one day).
int64_t shipCapacity(const std::vector<int>& w, int days) {
    int64_t lo = *std::max_element(w.begin(), w.end());
    int64_t hi = std::accumulate(w.begin(), w.end(), int64_t(0));
    return firstTrue<int64_t>(lo, hi + 1, [&](int64_t c) { return daysNeeded(w, c) <= days; });
}

// ---------------------------------------------------------------- 3. Split array largest sum (LeetCode 410)

// Split a into k non-empty contiguous parts so that the largest part sum is as small as possible. This is the ship
// problem: the capacity is the largest part sum and the days are the parts. (At most k parts is the same as exactly k
// when k <= n, because cutting a part in two never raises the largest sum.)
int64_t splitLargestSum(const std::vector<int>& a, int k) { return shipCapacity(a, k); }

// ---------------------------------------------------------------- 4. Magnetic force between two balls (LeetCode 1552): a maximum

// Can m balls go into the sorted positions with every two at least d apart? Take the first position, then every
// position that is at least d beyond the last one taken.
bool canPlace(const std::vector<int>& pos, int m, int d) {
    int placed = 1, last = pos[0];
    for (size_t i = 1; i < pos.size(); ++i)
        if (pos[i] - last >= d) { ++placed; last = pos[i]; }
    return placed >= m;
}

// The largest minimum distance. canPlace is true for small d and false for large d, so search for the first d
// that is too large and step back one. m balls need m - 1 gaps, so d is at most span / (m - 1).
int maxMinDistance(std::vector<int> pos, int m) {
    std::sort(pos.begin(), pos.end());
    int hi = (pos.back() - pos.front()) / (m - 1);
    return firstTrue<int>(1, hi + 1, [&](int d) { return !canPlace(pos, m, d); }) - 1;
}

// ---------------------------------------------------------------- 5. Bouquets (LeetCode 1482): the answer may not exist

// Are there m bouquets of k adjacent flowers by day d? A flower is open when bloomDay <= d.
bool canMakeBouquets(const std::vector<int>& bloomDay, int64_t m, int k, int d) {
    int64_t made = 0;
    int run = 0;                                                // adjacent open flowers in the current run
    for (int b : bloomDay) {
        if (b > d) run = 0;
        else if (++run == k) { ++made; run = 0; }
    }
    return made >= m;
}

// The first day with m bouquets, or -1. Searching [first bloom, last bloom + 1) returns last bloom + 1 when even
// the last day is not enough, and that is exactly "none".
int minDays(const std::vector<int>& bloomDay, int64_t m, int k) {
    int lo = *std::min_element(bloomDay.begin(), bloomDay.end());
    int hi = *std::max_element(bloomDay.begin(), bloomDay.end()) + 1;
    int day = firstTrue<int>(lo, hi, [&](int d) { return canMakeBouquets(bloomDay, m, k, d); });
    return day == hi ? -1 : day;
}

// ---------------------------------------------------------------- 6. A count as the predicate: k-th smallest in a sorted matrix (LeetCode 378)

// How many entries are <= x? Rows and columns are sorted. Start at the bottom-left corner: if the entry is <= x, the
// whole column above it is too, so add them and step right; otherwise step up. At most 2n steps.
int64_t countAtMost(const std::vector<std::vector<int>>& mat, int x) {
    int n = (int)mat.size();
    int64_t count = 0;
    int r = n - 1, c = 0;
    while (r >= 0 && c < n) {
        if (mat[r][c] <= x) { count += r + 1; ++c; }
        else --r;
    }
    return count;
}

// The k-th smallest entry, k from 1. The smallest x with countAtMost(x) >= k is always an entry of the matrix:
// if x were not an entry, countAtMost(x - 1) would equal countAtMost(x), and x - 1 would already qualify.
// The values may be +-10^9, so hi - lo stays below 2^31 - 1 (it is at most 2 * 10^9 + 1).
int kthSmallest(const std::vector<std::vector<int>>& mat, int64_t k) {
    int lo = mat[0][0], hi = mat.back().back() + 1;
    return firstTrue<int>(lo, hi, [&](int x) { return countAtMost(mat, x) >= k; });
}

// ---------------------------------------------------------------- 7. Real numbers

// Square root by halving a fixed number of times. Real numbers have no "mid + 1", so the loop runs a set number
// of rounds: the interval [lo, hi] shrinks to (hi - lo) / 2^rounds. The predicate is "mid * mid >= x".
double sqrtReal(double x, int rounds) {
    double lo = 0, hi = std::max(1.0, x);
    for (int i = 0; i < rounds; ++i) {
        double mid = lo + (hi - lo) / 2;
        if (mid * mid >= x) hi = mid;
        else lo = mid;
    }
    return hi;
}

// The tempting version: stop when hi - lo <= eps. The cap exists only so that the demo can stop it; without it
// this loop can run forever.
int roundsUntil(double x, double eps, int cap, double& width) {
    double lo = 0, hi = std::max(1.0, x);
    int rounds = 0;
    while (hi - lo > eps && rounds < cap) {
        double mid = lo + (hi - lo) / 2;
        if (mid * mid >= x) hi = mid;
        else lo = mid;
        ++rounds;
    }
    width = hi - lo;
    return rounds;
}

// ---------------------------------------------------------------- 8. No upper bound known

// The first x >= 1 with pred(x), when pred is false, ..., false, true, ... and nothing says how far the "true" part
// starts. Double until pred holds, then search the last doubling step. (Loops forever if pred is never true.)
template <class Pred>
int64_t firstTrueFrom1(Pred pred) {
    int64_t hi = 1;
    while (!pred(hi)) hi *= 2;                                  // the answer is in (hi / 2, hi]
    return firstTrue<int64_t>(hi / 2 + 1, hi + 1, pred);
}

// ================================================================ tests and experiments
static size_t floorLog2(uint64_t n) {
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

// Runs firstTrue and records every probe as "value:T" or "value:F".
template <class Pred>
static std::string traced(int64_t lo, int64_t hi, Pred pred, int64_t& result, int& calls) {
    std::string log;
    calls = 0;
    result = firstTrue<int64_t>(lo, hi, [&](int64_t v) {
        bool t = pred(v);
        ++calls;
        log += (log.empty() ? "" : " ") + std::to_string(v) + (t ? ":T" : ":F");
        return t;
    });
    return log;
}

// ---- independent references (no binary search anywhere in them)
static int64_t hoursSim(const std::vector<int>& piles, int k) {          // eat hour by hour
    int64_t h = 0;
    for (int p : piles) for (int left = p; left > 0; left -= k) ++h;
    return h;
}
static int64_t kokoBrute(const std::vector<int>& piles, int64_t h) {     // try every speed
    int top = *std::max_element(piles.begin(), piles.end());
    for (int k = 1; k <= top; ++k) if (hoursSim(piles, k) <= h) return k;
    return top + 1;
}
// Minimum over all ways to cut a into exactly k non-empty parts of the largest part sum (needs k <= n).
static int64_t dpSplit(const std::vector<int>& a, int k) {
    int n = (int)a.size();
    std::vector<int64_t> pre(n + 1, 0);
    for (int i = 0; i < n; ++i) pre[i + 1] = pre[i] + a[i];
    const int64_t INF = INT64_MAX;
    std::vector<std::vector<int64_t>> dp(k + 1, std::vector<int64_t>(n + 1, INF));
    dp[0][0] = 0;
    for (int j = 1; j <= k; ++j)
        for (int i = j; i <= n; ++i)
            for (int t = j - 1; t < i; ++t)
                if (dp[j - 1][t] != INF) dp[j][i] = std::min(dp[j][i], std::max(dp[j - 1][t], pre[i] - pre[t]));
    return dp[k][n];
}
// Most disjoint windows of k adjacent flowers that are all open on day d.
static int bouquetsDP(const std::vector<int>& bloom, int k, int d) {
    int n = (int)bloom.size();
    std::vector<int> best(n + 1, 0);
    for (int i = 1; i <= n; ++i) {
        best[i] = best[i - 1];
        if (i >= k) {
            bool allOpen = true;
            for (int j = i - k; j < i; ++j) if (bloom[j] > d) allOpen = false;
            if (allOpen) best[i] = std::max(best[i], best[i - k] + 1);
        }
    }
    return best[n];
}
static int minDaysBrute(const std::vector<int>& bloom, int m, int k) {
    int mx = *std::max_element(bloom.begin(), bloom.end());
    for (int d = 1; d <= mx; ++d) if (bouquetsDP(bloom, k, d) >= m) return d;
    return -1;
}
// Try every choice of m positions.
static int maxMinBrute(const std::vector<int>& pos, int m) {
    std::vector<int> p = pos;
    std::sort(p.begin(), p.end());
    int n = (int)p.size(), best = 0;
    for (unsigned mask = 0; mask < (1u << n); ++mask) {
        if (__builtin_popcount(mask) != m) continue;
        int prev = -1, gap = INT_MAX;
        for (int i = 0; i < n; ++i)
            if (mask >> i & 1) { if (prev >= 0) gap = std::min(gap, p[i] - prev); prev = p[i]; }
        best = std::max(best, gap);
    }
    return best;
}
static std::vector<std::vector<int>> randomMatrix(Rng& rng, int n) {
    std::vector<std::vector<int>> m(n, std::vector<int>(n));
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            int up = i ? m[i - 1][j] : -5, left = j ? m[i][j - 1] : -5;
            m[i][j] = std::max(up, left) + (int)rng.below(4);
        }
    return m;
}

int main() {
    // ---------------------------------------------------------------- firstTrue itself (tutorial 01 tested it in full)
    for (int n = 0; n <= 30; ++n)
        for (int b = 0; b <= n; ++b)
            CHECK(firstTrue<int>(0, n, [&](int i) { return i >= b; }) == b);

    // ---------------------------------------------------------------- Koko
    {
        std::vector<int> piles = {3, 6, 7, 11};
        std::printf("hours k=1..12:");
        for (int k = 1; k <= 12; ++k) std::printf(" %lld", (long long)hoursNeeded(piles, k));
        std::printf("\n");
        int64_t r; int calls;
        std::string log = traced(1, 12, [&](int64_t k) { return hoursNeeded(piles, k) <= 8; }, r, calls);
        std::printf("trace koko piles=3,6,7,11 h=8 probes %s -> %lld\n", log.c_str(), (long long)r);
        CHECK(r == 4 && minEatingSpeed(piles, 8) == 4);
        // the "exactly h hours" predicate is not monotone: it is true only for speeds 4 and 5
        int64_t e = firstTrue<int64_t>(1, 12, [&](int64_t k) { return hoursNeeded(piles, k) == 8; });
        std::printf("exact hours==8 search returns %lld (answer is 4)\n", (long long)e);
        CHECK(e == 12);
        std::vector<int> p2 = {30, 11, 23, 4, 20};
        for (int h : {5, 6}) {
            std::string l2 = traced(1, 31, [&](int64_t k) { return hoursNeeded(p2, k) <= h; }, r, calls);
            std::printf("trace koko piles=30,11,23,4,20 h=%d probes %s -> %lld\n", h, l2.c_str(), (long long)r);
            CHECK(r == minEatingSpeed(p2, h) && r == kokoBrute(p2, h));
        }
        // exhaustive: every pile list of length 1..4 over 1..6, every h from 1 to sum + 1
        size_t cases = 0;
        for (int n = 1; n <= 4; ++n) {
            int total = 1;
            for (int i = 0; i < n; ++i) total *= 6;
            for (int code = 0; code < total; ++code) {
                std::vector<int> v(n);
                int c = code, sum = 0;
                for (int i = 0; i < n; ++i) { v[i] = c % 6 + 1; c /= 6; sum += v[i]; }
                for (int k = 1; k <= 7; ++k) CHECK(hoursNeeded(v, k) == hoursSim(v, k));
                for (int h = 1; h <= sum + 1; ++h) { CHECK(minEatingSpeed(v, h) == kokoBrute(v, h)); ++cases; }
            }
        }
        std::printf("koko exhaustive cases=%zu\n", cases);
        Rng rng(7);
        for (int t = 0; t < 300; ++t) {
            int n = 1 + (int)rng.below(8);
            std::vector<int> v(n);
            for (int& x : v) x = 1 + (int)rng.below(50);
            int sum = std::accumulate(v.begin(), v.end(), 0);
            int h = n + (int)rng.below(sum);
            CHECK(minEatingSpeed(v, h) == kokoBrute(v, h));
            for (int k = 1; k < 60; ++k) CHECK(hoursNeeded(v, k) >= hoursNeeded(v, k + 1));     // never slower with a faster speed
        }
        // the largest inputs: 10^4 piles of 10^9
        std::vector<int> big(10000, 1000000000);
        CHECK(hoursNeeded(big, 1) == 10000000000000LL);
        int64_t wrapped = (int32_t)(uint32_t)((uint64_t)10000000000000ULL);
        std::printf("koko big hours at speed 1 = %lld, in 32 bits = %lld\n", (long long)hoursNeeded(big, 1), (long long)wrapped);
        traced(1, 1000000001, [&](int64_t k) { return hoursNeeded(big, k) <= 1000000000; }, r, calls);
        std::printf("koko big piles=10000x1e9 h=1e9 speed=%lld calls=%d\n", (long long)r, calls);
        CHECK(r == 10000 && minEatingSpeed(big, 1000000000) == 10000);
        CHECK((size_t)calls <= floorLog2(1000000000) + 1);
        // fewer hours than piles: nothing works, the result is max + 1
        CHECK(minEatingSpeed(piles, 3) == 12);
        std::printf("koko h=3 (too few hours) -> %lld; hours at speed 11 = %lld\n", (long long)minEatingSpeed(piles, 3), (long long)hoursNeeded(piles, 11));
    }

    // ---------------------------------------------------------------- Ship packages
    {
        std::vector<int> w = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        int64_t r; int calls;
        std::string log = traced(10, 56, [&](int64_t c) { return daysNeeded(w, c) <= 5; }, r, calls);
        std::printf("trace ship weights=1..10 days=5 probes %s -> %lld\n", log.c_str(), (long long)r);
        CHECK(r == 15 && shipCapacity(w, 5) == 15);
        std::printf("ship 1..10 capacity 15 loads:");
        {
            int64_t load = 0;
            for (int x : w) {
                if (load + x > 15) { std::printf(" |"); load = 0; }
                load += x;
                std::printf(" %d", x);
            }
            std::printf("\n");
        }
        // exhaustive against an independent DP
        size_t cases = 0;
        for (int n = 1; n <= 5; ++n) {
            int total = 1;
            for (int i = 0; i < n; ++i) total *= 4;
            for (int code = 0; code < total; ++code) {
                std::vector<int> v(n);
                int c = code;
                for (int i = 0; i < n; ++i) { v[i] = c % 4 + 1; c /= 4; }
                for (int d = 1; d <= n; ++d) { CHECK(shipCapacity(v, d) == dpSplit(v, d)); ++cases; }
                CHECK(shipCapacity(v, n + 2) == *std::max_element(v.begin(), v.end()));
            }
        }
        std::printf("ship exhaustive cases=%zu\n", cases);
        // the bug of starting at 1: a package heavier than the ship is carried anyway
        {
            std::vector<int> b = {1, 5};
            int64_t bad = firstTrue<int64_t>(1, 7, [&](int64_t c) { return daysNeeded(b, c) <= 2; });
            std::printf("bug ship lo=1 on [1,5] days=2 -> %lld (correct %lld); daysNeeded at capacity 4 = %d\n",
                        (long long)bad, (long long)shipCapacity(b, 2), daysNeeded(b, 4));
            CHECK(bad == 1 && shipCapacity(b, 2) == 5 && daysNeeded(b, 4) == 2);
        }
        // the largest inputs: 5 * 10^4 packages of weight 500
        std::vector<int> big(50000, 500);
        traced(500, 25000001, [&](int64_t c) { return daysNeeded(big, c) <= 1; }, r, calls);
        std::printf("big ship n=50000 weight=500 days=1 capacity=%lld calls=%d\n", (long long)r, calls);
        CHECK(r == 25000000 && shipCapacity(big, 1) == 25000000);
        CHECK((size_t)calls <= floorLog2(25000001 - 500) + 1);
        std::printf("big ship range size=%d worst probes=%zu\n", 25000001 - 500, floorLog2(25000001 - 500) + 1);
    }

    // ---------------------------------------------------------------- Split array
    {
        std::vector<int> a = {7, 2, 5, 10, 8};
        int64_t r; int calls;
        std::string log = traced(10, 33, [&](int64_t c) { return daysNeeded(a, c) <= 2; }, r, calls);
        std::printf("trace split nums=7,2,5,10,8 k=2 probes %s -> %lld\n", log.c_str(), (long long)r);
        CHECK(r == 18 && splitLargestSum(a, 2) == 18 && dpSplit(a, 2) == 18);
        for (int k = 1; k <= 5; ++k) { CHECK(splitLargestSum(a, k) == dpSplit(a, k)); }
        std::printf("split 7,2,5,10,8 for k=1..5:");
        for (int k = 1; k <= 5; ++k) std::printf(" %lld", (long long)splitLargestSum(a, k));
        std::printf("\n");
        size_t cases = 0;
        for (int n = 1; n <= 6; ++n) {
            int total = 1;
            for (int i = 0; i < n; ++i) total *= 5;
            for (int code = 0; code < total; ++code) {
                std::vector<int> v(n);
                int c = code;
                for (int i = 0; i < n; ++i) { v[i] = c % 5; c /= 5; }
                for (int k = 1; k <= n; ++k) { CHECK(splitLargestSum(v, k) == dpSplit(v, k)); ++cases; }
            }
        }
        std::printf("split exhaustive cases=%zu\n", cases);
        Rng rng(11);
        for (int t = 0; t < 300; ++t) {
            int n = 1 + (int)rng.below(25);
            std::vector<int> v(n);
            for (int& x : v) x = (int)rng.below(1001);
            int k = 1 + (int)rng.below(n);
            CHECK(splitLargestSum(v, k) == dpSplit(v, k));
        }
        // 1000 numbers of 10^6 into 1 part: the sum is 10^9
        std::vector<int> big(1000, 1000000);
        traced(1000000, 1000000001, [&](int64_t c) { return daysNeeded(big, c) <= 1; }, r, calls);
        std::printf("big split n=1000 values=1e6 k=1 -> %lld calls=%d\n", (long long)r, calls);
        CHECK(r == 1000000000 && (size_t)calls <= floorLog2(1000000001 - 1000000) + 1);
        CHECK(splitLargestSum(std::vector<int>(4, 0), 2) == 0);
        // a total above 2^31: four numbers of 10^9 into two parts (outside the LeetCode limits, but legal for the function)
        std::vector<int> huge(4, 1000000000);
        std::printf("huge split 4x1e9 k=2 -> %lld\n", (long long)splitLargestSum(huge, 2));
        CHECK(splitLargestSum(huge, 2) == 2000000000 && shipCapacity(huge, 1) == 4000000000LL && shipCapacity(huge, 4) == 1000000000);
    }

    // ---------------------------------------------------------------- Magnetic force (a maximum)
    {
        std::vector<int> pos = {1, 2, 3, 4, 7};
        int hi = (7 - 1) / 2;
        int64_t r; int calls;
        std::string log = traced(1, hi + 1, [&](int64_t d) { return !canPlace(pos, 3, (int)d); }, r, calls);
        std::printf("trace magnetic pos=1,2,3,4,7 m=3 (too far?) probes %s -> %lld so answer %lld\n", log.c_str(), (long long)r, (long long)r - 1);
        CHECK(maxMinDistance(pos, 3) == 3 && maxMinDistance({5, 4, 3, 2, 1, 1000000000}, 2) == 999999999);
        // exhaustive: every subset of {1..10} with 2..10 elements, every m from 2 to its size
        size_t cases = 0;
        for (unsigned mask = 0; mask < (1u << 10); ++mask) {
            if (__builtin_popcount(mask) < 2) continue;
            std::vector<int> p;
            for (int i = 0; i < 10; ++i) if (mask >> i & 1) p.push_back(i + 1);
            std::reverse(p.begin(), p.end());                                       // unsorted on purpose
            for (int m = 2; m <= (int)p.size(); ++m) { CHECK(maxMinDistance(p, m) == maxMinBrute(p, m)); ++cases; }
        }
        std::printf("magnetic exhaustive cases=%zu\n", cases);
        Rng rng(5);
        for (int t = 0; t < 200; ++t) {
            int n = 2 + (int)rng.below(9);
            std::vector<int> p;
            int cur = 1;
            for (int i = 0; i < n; ++i) { cur += 1 + (int)rng.below(30); p.push_back(cur); }
            int m = 2 + (int)rng.below(n - 1);
            CHECK(maxMinDistance(p, m) == maxMinBrute(p, m));
        }
        // the stall of the "last true" loop with a rounded-down middle: lo = 2, hi = 3, canPlace(2) is true
        CHECK(canPlace({1, 2, 3, 4, 7}, 3, 2) && !canPlace({1, 2, 3, 4, 7}, 3, 4));
    }

    // ---------------------------------------------------------------- Bouquets
    {
        std::vector<int> b = {1, 10, 3, 10, 2};
        std::printf("bouquets [1,10,3,10,2] m=3 k=1 -> %d; m=3 k=2 -> %d; [7,7,7,7,12,7,7] m=2 k=3 -> %d\n",
                    minDays(b, 3, 1), minDays(b, 3, 2), minDays({7, 7, 7, 7, 12, 7, 7}, 2, 3));
        CHECK(minDays(b, 3, 1) == 3 && minDays(b, 3, 2) == -1 && minDays({7, 7, 7, 7, 12, 7, 7}, 2, 3) == 12);
        int64_t r; int calls;
        std::string log = traced(1, 11, [&](int64_t d) { return canMakeBouquets(b, 3, 1, (int)d); }, r, calls);
        std::printf("trace bouquets [1,10,3,10,2] m=3 k=1 probes %s -> %lld\n", log.c_str(), (long long)r);
        log = traced(1, 11, [&](int64_t d) { return canMakeBouquets(b, 3, 2, (int)d); }, r, calls);
        std::printf("trace bouquets [1,10,3,10,2] m=3 k=2 probes %s -> %lld (hi is 11: none)\n", log.c_str(), (long long)r);
        size_t cases = 0;
        for (int n = 1; n <= 6; ++n) {
            int total = 1;
            for (int i = 0; i < n; ++i) total *= 4;
            for (int code = 0; code < total; ++code) {
                std::vector<int> v(n);
                int c = code;
                for (int i = 0; i < n; ++i) { v[i] = c % 4 + 1; c /= 4; }
                for (int m = 1; m <= 4; ++m)
                    for (int k = 1; k <= n; ++k) {
                        int got = minDays(v, m, k);
                        CHECK(got == minDaysBrute(v, m, k));
                        CHECK((got == -1) == ((int64_t)m * k > n));                // impossible exactly when m * k > n
                        ++cases;
                    }
            }
        }
        std::printf("bouquets exhaustive cases=%zu\n", cases);
        CHECK(minDays({1, 1, 1}, 1000000, 3) == -1);
        std::printf("bouquets m*k with m=1e6 k=1e5 = %lld\n", (long long)1000000 * 100000);
    }

    // ---------------------------------------------------------------- k-th smallest in a sorted matrix
    {
        std::vector<std::vector<int>> mat = {{1, 5, 9}, {10, 11, 13}, {12, 13, 15}};
        std::printf("kth example count<=12: %lld, count<=13: %lld, count<=14: %lld, k=8 -> %d\n",
                    (long long)countAtMost(mat, 12), (long long)countAtMost(mat, 13), (long long)countAtMost(mat, 14), kthSmallest(mat, 8));
        CHECK(kthSmallest(mat, 8) == 13 && countAtMost(mat, 12) == 6 && countAtMost(mat, 13) == 8);
        int64_t r; int calls;
        std::string log = traced(1, 16, [&](int64_t x) { return countAtMost(mat, (int)x) >= 8; }, r, calls);
        std::printf("trace kth k=8 probes %s -> %lld\n", log.c_str(), (long long)r);
        Rng rng(3);
        size_t cases = 0;
        for (int t = 0; t < 400; ++t) {
            int n = 1 + (int)rng.below(6);
            auto m = randomMatrix(rng, n);
            std::vector<int> flat;
            for (auto& row : m) for (int v : row) flat.push_back(v);
            std::sort(flat.begin(), flat.end());
            for (int x = -6; x <= flat.back() + 1; ++x) {
                int64_t brute = std::upper_bound(flat.begin(), flat.end(), x) - flat.begin();
                CHECK(countAtMost(m, x) == brute);
            }
            for (int k = 1; k <= n * n; ++k) { CHECK(kthSmallest(m, k) == flat[k - 1]); ++cases; }
        }
        std::printf("kth cases=%zu\n", cases);
        std::vector<std::vector<int>> ext = {{-1000000000, -1000000000}, {-1000000000, 1000000000}};
        CHECK(kthSmallest(ext, 1) == -1000000000 && kthSmallest(ext, 3) == -1000000000 && kthSmallest(ext, 4) == 1000000000);
        std::vector<std::vector<int>> ext2 = {{-1000000000, 0}, {0, 1000000000}};
        CHECK(kthSmallest(ext2, 4) == 1000000000 && kthSmallest(ext2, 1) == -1000000000);
        std::printf("kth range hi-lo = %lld, INT_MAX = %d\n", (long long)(1000000001LL + 1000000000LL), INT_MAX);
        CHECK(1000000001LL + 1000000000LL < INT_MAX);
        CHECK(kthSmallest({{42}}, 1) == 42);
    }

    // ---------------------------------------------------------------- real numbers
    {
        double w;
        int rounds = roundsUntil(2.0, 1e-9, 1000, w);
        std::printf("sqrt eps=1e-9 x=2 rounds=%d width=%.3g\n", rounds, w);
        CHECK(rounds == 31 && w <= 1e-9);
        double r31 = sqrtReal(2.0, 31);
        std::printf("sqrtReal(2, 31)=%.12f error=%.3g\n", r31, std::fabs(r31 - std::sqrt(2.0)));
        CHECK(std::fabs(r31 - std::sqrt(2.0)) <= 2.0 / std::pow(2.0, 31));
        for (double x : {0.0, 0.25, 1.0, 2.0, 10.0, 12345.678, 1e6, 1e18})
            CHECK(std::fabs(sqrtReal(x, 100) - std::sqrt(x)) <= 4e-16 * std::sqrt(x) + std::max(1.0, x) * std::pow(2.0, -100));
        rounds = roundsUntil(2.0, 1e-12, 1000, w);
        std::printf("sqrt eps=1e-12 x=2 rounds=%d width=%.3g\n", rounds, w);
        CHECK(rounds == 41 && w <= 1e-12);
        CHECK(roundsUntil(2.0, std::ldexp(2.0, -31), 1000, w) == 31);              // stops when the width equals eps
        CHECK(sqrtReal(4.0, 100) == 2.0 && sqrtReal(0.25, 100) == 0.5 && sqrtReal(0.0, 100) < 1e-29);
        // an epsilon that is too small for the size of the numbers: the interval stops shrinking at one step of the double
        rounds = roundsUntil(1e18, 1e-9, 1000, w);
        std::printf("eps trap x=1e18 eps=1e-9 cap=1000 rounds=%d width=%.3g\n", rounds, w);
        CHECK(rounds == 1000 && w > 1e-9 && w < 1e-6);
        rounds = roundsUntil(1e18, 1e-6, 1000, w);
        std::printf("eps trap x=1e18 eps=1e-6 rounds=%d width=%.3g\n", rounds, w);
        CHECK(rounds < 1000);
        // a halving takes exactly one bit of precision: 100 rounds are far more than a double has
        CHECK(sqrtReal(2.0, 100) == sqrtReal(2.0, 200));
        // the first rounds by hand: [0, 2] -> mid 1 -> [1, 2] -> 1.5 -> [1, 1.5] -> 1.25 -> [1.25, 1.5] -> 1.375 -> 1.4375 ...
        std::printf("sqrtReal(2, r) for r = 1..7:");
        for (int r = 1; r <= 7; ++r) std::printf(" %.6g", sqrtReal(2.0, r));
        std::printf("\n");
        CHECK(sqrtReal(2.0, 1) == 2.0 && sqrtReal(2.0, 2) == 1.5 && sqrtReal(2.0, 3) == 1.5 && sqrtReal(2.0, 4) == 1.5);
        CHECK(sqrtReal(2.0, 5) == 1.4375 && sqrtReal(2.0, 6) == 1.4375 && sqrtReal(2.0, 7) == 1.421875);
    }

    // ---------------------------------------------------------------- no upper bound known
    {
        size_t worstCalls = 0;
        for (int64_t b = 1; b <= 1000; ++b) {
            size_t calls = 0;
            int64_t r = firstTrueFrom1([&](int64_t x) { ++calls; return x >= b; });
            CHECK(r == b);
            worstCalls = std::max(worstCalls, calls);
            CHECK(calls <= 2 * (floorLog2(b) + 1) + 1);
        }
        size_t calls = 0;
        int64_t r = firstTrueFrom1([&](int64_t x) { ++calls; return x >= 1000; });
        std::printf("doubling b=1000 -> %lld calls=%zu (doubling %zu, then search)\n", (long long)r, calls, floorLog2(1024) + 1);
        CHECK(r == 1000);
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
