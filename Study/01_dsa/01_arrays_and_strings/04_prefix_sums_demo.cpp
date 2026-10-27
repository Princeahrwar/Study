// Companion to 04_prefix_sums_and_difference_arrays.md
// Every function the tutorial shows is here, checked against a brute force
// on hand-worked examples and on random inputs.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 04_prefix_sums_demo.cpp -o demo && ./demo

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (cond) ++g_passed;                                                    \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);               \
    } while (0)

// ---------------------------------------------------------------- section 1
std::vector<long long> buildPrefix(const std::vector<int>& a) {
    std::vector<long long> p(a.size() + 1, 0);       // p[0] = 0: the empty prefix
    for (size_t i = 0; i < a.size(); ++i) p[i + 1] = p[i] + a[i];
    return p;
}

// Sum of a[l..r], both ends included (0 <= l <= r < n).
long long rangeSum(const std::vector<long long>& p, int l, int r) {
    return p[r + 1] - p[l];
}

// ---------------------------------------------------------------- section 2
// Number of non-empty subarrays whose sum is exactly k. Negative numbers allowed.
long long countSubarraysWithSum(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, long long> seen;   // prefix value -> how many earlier prefixes had it
    seen[0] = 1;                                     // the empty prefix, so subarrays that start at index 0 are found
    long long prefix = 0, count = 0;
    for (int x : a) {
        prefix += x;
        auto it = seen.find(prefix - k);             // look up BEFORE inserting the current prefix
        if (it != seen.end()) count += it->second;
        ++seen[prefix];
    }
    return count;
}

// Length of the longest subarray whose sum is exactly k (0 if there is none).
int longestSubarrayWithSum(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, int> first;        // prefix value -> the FIRST index where it occurred
    first[0] = -1;                                   // the empty prefix sits just before index 0
    long long prefix = 0;
    int best = 0;
    for (int i = 0; i < (int)a.size(); ++i) {
        prefix += a[i];
        auto it = first.find(prefix - k);
        if (it != first.end()) best = std::max(best, i - it->second);
        first.emplace(prefix, i);                    // emplace keeps the earliest index if the key already exists
    }
    return best;
}

// Length of the shortest subarray whose sum is exactly k (0 if there is none).
int shortestSubarrayWithSum(const std::vector<int>& a, long long k) {
    std::unordered_map<long long, int> last;         // prefix value -> the LATEST index where it occurred
    last[0] = -1;
    long long prefix = 0;
    int best = 0;
    for (int i = 0; i < (int)a.size(); ++i) {
        prefix += a[i];
        auto it = last.find(prefix - k);
        if (it != last.end() && (best == 0 || i - it->second < best)) best = i - it->second;
        last[prefix] = i;                            // overwrite: a later start gives a shorter subarray
    }
    return best;
}

// ---------------------------------------------------------------- section 3
// LeetCode 525: longest subarray with equally many 0s and 1s.
int findMaxLength(const std::vector<int>& nums) {
    std::unordered_map<int, int> first{{0, -1}};     // balance (ones minus zeros) -> first index
    int balance = 0, best = 0;
    for (int i = 0; i < (int)nums.size(); ++i) {
        balance += nums[i] == 1 ? 1 : -1;
        auto it = first.find(balance);
        if (it == first.end()) first[balance] = i;
        else best = std::max(best, i - it->second);
    }
    return best;
}

// LeetCode 974: number of subarrays whose sum is divisible by k.
int subarraysDivByK(const std::vector<int>& nums, int k) {
    std::vector<int> count(k, 0);                    // count[r] = how many prefixes so far have remainder r
    count[0] = 1;                                    // the empty prefix
    long long prefix = 0;
    int result = 0;
    for (int x : nums) {
        prefix = ((prefix + x) % k + k) % k;         // C++ % can be negative: shift into 0..k-1
        result += count[prefix]++;                   // every earlier prefix with the same remainder pairs with this one
    }
    return result;
}

// XOR of a[l..r]: XOR undoes itself, so x[r + 1] ^ x[l] works like a subtraction.
std::vector<unsigned> buildPrefixXor(const std::vector<unsigned>& a) {
    std::vector<unsigned> x(a.size() + 1, 0);
    for (size_t i = 0; i < a.size(); ++i) x[i + 1] = x[i] ^ a[i];
    return x;
}

// Number of subarrays whose XOR is exactly k.
long long countSubarraysWithXor(const std::vector<unsigned>& a, unsigned k) {
    std::unordered_map<unsigned, long long> seen;
    seen[0] = 1;
    unsigned prefix = 0;
    long long count = 0;
    for (unsigned x : a) {
        prefix ^= x;
        auto it = seen.find(prefix ^ k);             // x[r + 1] ^ x[l] == k  <=>  x[l] == x[r + 1] ^ k
        if (it != seen.end()) count += it->second;
        ++seen[prefix];
    }
    return count;
}

// ---------------------------------------------------------------- section 4
struct Prefix2D {
    std::vector<std::vector<long long>> p;           // (m + 1) x (n + 1); p[i][j] = sum of the top-left i x j block
    explicit Prefix2D(const std::vector<std::vector<int>>& a) {
        int m = (int)a.size(), n = m ? (int)a[0].size() : 0;
        p.assign(m + 1, std::vector<long long>(n + 1, 0));
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < n; ++j)
                p[i + 1][j + 1] = a[i][j] + p[i][j + 1] + p[i + 1][j] - p[i][j];
    }
    // Sum of the rectangle with corners (r1, c1) and (r2, c2), both included.
    long long sum(int r1, int c1, int r2, int c2) const {
        return p[r2 + 1][c2 + 1] - p[r1][c2 + 1] - p[r2 + 1][c1] + p[r1][c1];
    }
};

// ---------------------------------------------------------------- section 5
// n slots start at 0. Each update {l, r, v} adds v to slots l..r (both included).
std::vector<long long> applyRangeAdds(int n, const std::vector<std::array<int, 3>>& updates) {
    std::vector<long long> d(n + 1, 0);              // one extra slot, so r + 1 never needs a bounds check
    for (const auto& [l, r, v] : updates) {
        d[l] += v;
        d[r + 1] -= v;
    }
    for (int i = 1; i < n; ++i) d[i] += d[i - 1];    // a prefix sum turns the differences back into values
    d.pop_back();
    return d;
}

// LeetCode 1094: can one car with this capacity do all trips {passengers, from, to}?
bool carPooling(const std::vector<std::vector<int>>& trips, int capacity) {
    std::vector<int> delta(1002, 0);                 // locations are 0..1000
    for (const auto& t : trips) {
        delta[t[1]] += t[0];                         // passengers get on at t[1] ...
        delta[t[2]] -= t[0];                         // ... and off at t[2]
    }
    int load = 0;
    for (int d : delta) {
        load += d;
        if (load > capacity) return false;
    }
    return true;
}

// Add v to every cell of the rectangle (r1, c1)..(r2, c2) on a difference grid.
void addRect(std::vector<std::vector<long long>>& d, int r1, int c1, int r2, int c2, long long v) {
    d[r1][c1] += v;
    d[r1][c2 + 1] -= v;
    d[r2 + 1][c1] -= v;
    d[r2 + 1][c2 + 1] += v;
}

// ---------------------------------------------------------------- section 6
// LeetCode 528: pick index i with probability w[i] / sum(w).
int indexForTarget(const std::vector<long long>& prefix, long long target) {
    // prefix[i] = w[0] + ... + w[i]; target is in 1..prefix.back()
    return int(std::lower_bound(prefix.begin(), prefix.end(), target) - prefix.begin());
}

class WeightedPicker {
    std::vector<long long> prefix_;
    std::mt19937_64 rng_;
public:
    WeightedPicker(const std::vector<int>& w, unsigned seed) : rng_(seed) {
        long long s = 0;
        for (int x : w) {
            s += x;
            prefix_.push_back(s);
        }
    }
    int pick() {
        long long target = std::uniform_int_distribution<long long>(1, prefix_.back())(rng_);
        return indexForTarget(prefix_, target);
    }
    const std::vector<long long>& prefix() const { return prefix_; }
};

// ---------------------------------------------------------------- section 7
// A cumulative 32-bit counter is a prefix sum. The difference of two readings is a range sum,
// and unsigned subtraction is correct across one wrap-around.
std::uint32_t bytesBetween(std::uint32_t before, std::uint32_t after) {
    return after - before;
}

// ---------------------------------------------------------------- answers
// LeetCode 303.
class NumArray {
    std::vector<long long> p_;
public:
    explicit NumArray(const std::vector<int>& nums) : p_(buildPrefix(nums)) {}
    int sumRange(int left, int right) const { return (int)(p_[right + 1] - p_[left]); }
};

// LeetCode 304.
class NumMatrix {
    Prefix2D t_;
public:
    explicit NumMatrix(const std::vector<std::vector<int>>& m) : t_(m) {}
    int sumRegion(int row1, int col1, int row2, int col2) const {
        return (int)t_.sum(row1, col1, row2, col2);
    }
};

int subarraySum(const std::vector<int>& nums, int k) {
    return (int)countSubarraysWithSum(nums, k);
}

// LeetCode 1109: seats booked on flights 1..n; each booking is {first, last, seats}.
std::vector<int> corpFlightBookings(const std::vector<std::vector<int>>& bookings, int n) {
    std::vector<int> diff(n + 2, 0);                 // flights are 1-indexed; slot n + 1 absorbs the "stop" marker
    for (const auto& b : bookings) {
        diff[b[0]] += b[2];
        diff[b[1] + 1] -= b[2];
    }
    std::vector<int> answer(n);
    int running = 0;
    for (int f = 1; f <= n; ++f) {
        running += diff[f];
        answer[f - 1] = running;
    }
    return answer;
}

// ---------------------------------------------------------------- brute forces
static long long bruteRange(const std::vector<int>& a, int l, int r) {
    long long s = 0;
    for (int i = l; i <= r; ++i) s += a[i];
    return s;
}
static long long bruteCountSum(const std::vector<int>& a, long long k) {
    long long c = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        long long s = 0;
        for (size_t j = i; j < a.size(); ++j) {
            s += a[j];
            if (s == k) ++c;
        }
    }
    return c;
}
static int bruteLongestSum(const std::vector<int>& a, long long k) {
    int best = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        long long s = 0;
        for (size_t j = i; j < a.size(); ++j) {
            s += a[j];
            if (s == k) best = std::max(best, (int)(j - i + 1));
        }
    }
    return best;
}
static int bruteBalanced(const std::vector<int>& a) {
    int best = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        int bal = 0;
        for (size_t j = i; j < a.size(); ++j) {
            bal += a[j] ? 1 : -1;
            if (bal == 0) best = std::max(best, (int)(j - i + 1));
        }
    }
    return best;
}
static int bruteDiv(const std::vector<int>& a, int k) {
    int c = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        long long s = 0;
        for (size_t j = i; j < a.size(); ++j) {
            s += a[j];
            if (s % k == 0) ++c;
        }
    }
    return c;
}

static int bruteShortestSum(const std::vector<int>& a, long long k) {
    int best = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        long long s = 0;
        for (size_t j = i; j < a.size(); ++j) {
            s += a[j];
            if (s == k && (best == 0 || (int)(j - i + 1) < best)) best = (int)(j - i + 1);
        }
    }
    return best;
}
static long long bruteCountXor(const std::vector<unsigned>& a, unsigned k) {
    long long c = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        unsigned x = 0;
        for (size_t j = i; j < a.size(); ++j) {
            x ^= a[j];
            if (x == k) ++c;
        }
    }
    return c;
}

int main() {
    std::mt19937 rng(20261026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // --- section 1: the worked example in the tutorial
    {
        std::vector<int> a{3, 1, 4, 1, 5, 9, 2, 6};
        auto p = buildPrefix(a);
        CHECK((p == std::vector<long long>{0, 3, 4, 8, 9, 14, 23, 25, 31}));
        CHECK(rangeSum(p, 2, 5) == 19);
        CHECK(rangeSum(p, 0, 7) == 31);
        CHECK(rangeSum(p, 0, 0) == 3);                      // l = 0 needs no special case
        std::vector<long long> viaStl(a.size());
        std::partial_sum(a.begin(), a.end(), viaStl.begin());
        CHECK(std::equal(viaStl.begin(), viaStl.end(), p.begin() + 1));
    }
    // random: every range of random arrays
    for (int t = 0; t < 200; ++t) {
        int n = rnd(1, 30);
        std::vector<int> a(n);
        for (int& x : a) x = rnd(-1000, 1000);
        auto p = buildPrefix(a);
        bool ok = true;
        for (int l = 0; l < n; ++l)
            for (int r = l; r < n; ++r) ok = ok && rangeSum(p, l, r) == bruteRange(a, l, r);
        CHECK(ok);
    }
    // overflow: 100000 values of 1e9 need 64 bits
    {
        std::vector<int> a(100000, 1000000000);
        auto p = buildPrefix(a);
        CHECK(p.back() == 100000LL * 1000000000LL);
    }

    // --- section 2: subarray sum equals k
    {
        std::vector<int> a{3, 4, 7, 2, -3, 1, 4, 2};
        CHECK(countSubarraysWithSum(a, 7) == 4);
        CHECK(longestSubarrayWithSum(a, 7) == 4);           // 7, 2, -3, 1
        CHECK(countSubarraysWithSum({1, 1, 1}, 2) == 2);
        CHECK(countSubarraysWithSum({1, 2, 3}, 3) == 2);
        CHECK(countSubarraysWithSum({0, 0, 0}, 0) == 6);    // k = 0 shows why we look up before inserting
        CHECK(countSubarraysWithSum({}, 0) == 0);
        CHECK(longestSubarrayWithSum({1, -1, 5, -2, 3}, 3) == 4);
        CHECK(longestSubarrayWithSum({-2, -1, 2, 1}, 1) == 2);
    }
    for (int t = 0; t < 500; ++t) {
        int n = rnd(0, 25);
        std::vector<int> a(n);
        for (int& x : a) x = rnd(-4, 4);
        long long k = rnd(-6, 6);
        CHECK(countSubarraysWithSum(a, k) == bruteCountSum(a, k));
        CHECK(longestSubarrayWithSum(a, k) == bruteLongestSum(a, k));
        CHECK(shortestSubarrayWithSum(a, k) == bruteShortestSum(a, k));
    }
    CHECK(shortestSubarrayWithSum({3, 4, 7, 2, -3, 1, 4, 2}, 7) == 1);          // the single element 7

    // --- section 3: other prefix functions
    {
        CHECK(findMaxLength({0, 1, 1, 0, 1, 1, 1, 0}) == 4);
        CHECK(findMaxLength({0, 1}) == 2);
        CHECK(findMaxLength({0, 1, 0}) == 2);
        CHECK(findMaxLength({1, 1, 1}) == 0);
        CHECK(subarraysDivByK({4, 5, 0, -2, -3, 1}, 5) == 7);
        CHECK(subarraysDivByK({5}, 9) == 0);
        CHECK(-1 % 5 == -1);                                // the C++ remainder really can be negative
    }
    for (int t = 0; t < 500; ++t) {
        int n = rnd(0, 25);
        std::vector<int> bits(n), a(n);
        for (int& x : bits) x = rnd(0, 1);
        for (int& x : a) x = rnd(-9, 9);
        int k = rnd(1, 7);
        CHECK(findMaxLength(bits) == bruteBalanced(bits));
        CHECK(subarraysDivByK(a, k) == bruteDiv(a, k));
    }
    {
        std::vector<unsigned> a{1, 3, 4, 8, 7};
        auto x = buildPrefixXor(a);
        CHECK((x[4] ^ x[1]) == (3u ^ 4u ^ 8u));             // xor of a[1..3]
        bool ok = true;
        for (size_t l = 0; l < a.size(); ++l) {
            unsigned acc = 0;
            for (size_t r = l; r < a.size(); ++r) {
                acc ^= a[r];
                ok = ok && (x[r + 1] ^ x[l]) == acc;
            }
        }
        CHECK(ok);
        CHECK(countSubarraysWithXor({4, 2, 2, 6, 4}, 6) == 4);               // hand-checked: [4,2], [2,2,6], [6] and the whole array
    }
    for (int tcase = 0; tcase < 300; ++tcase) {
        int n = rnd(0, 20);
        std::vector<unsigned> a(n);
        for (unsigned& x : a) x = (unsigned)rnd(0, 7);
        unsigned k = (unsigned)rnd(0, 7);
        CHECK(countSubarraysWithXor(a, k) == bruteCountXor(a, k));
    }

    // --- section 4: 2D
    {
        std::vector<std::vector<int>> a{{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
        Prefix2D t(a);
        CHECK((t.p[1] == std::vector<long long>{0, 1, 3, 6, 10}));
        CHECK((t.p[2] == std::vector<long long>{0, 6, 14, 24, 36}));
        CHECK((t.p[3] == std::vector<long long>{0, 15, 33, 54, 78}));
        CHECK(t.sum(1, 1, 2, 2) == 34);
        CHECK(t.sum(0, 0, 2, 3) == 78);
        CHECK(t.sum(2, 3, 2, 3) == 12);
    }
    for (int tcase = 0; tcase < 100; ++tcase) {
        int m = rnd(1, 7), n = rnd(1, 7);
        std::vector<std::vector<int>> a(m, std::vector<int>(n));
        for (auto& row : a)
            for (int& x : row) x = rnd(-9, 9);
        Prefix2D t(a);
        bool ok = true;
        for (int r1 = 0; r1 < m; ++r1)
            for (int c1 = 0; c1 < n; ++c1)
                for (int r2 = r1; r2 < m; ++r2)
                    for (int c2 = c1; c2 < n; ++c2) {
                        long long s = 0;
                        for (int i = r1; i <= r2; ++i)
                            for (int j = c1; j <= c2; ++j) s += a[i][j];
                        ok = ok && t.sum(r1, c1, r2, c2) == s;
                    }
        CHECK(ok);
    }

    // --- section 5: difference arrays
    {
        auto v = applyRangeAdds(6, {{1, 3, 2}, {2, 5, 3}, {0, 1, 1}});
        CHECK((v == std::vector<long long>{1, 3, 5, 5, 3, 3}));
        CHECK(applyRangeAdds(3, {}) == std::vector<long long>(3, 0));
        CHECK((applyRangeAdds(1, {{0, 0, 5}}) == std::vector<long long>{5}));
        // STL pair: partial_sum and adjacent_difference undo each other
        std::vector<int> a{5, 2, 9, 9, 1};
        std::vector<int> d(a.size());
        std::adjacent_difference(a.begin(), a.end(), d.begin());
        CHECK((d == std::vector<int>{5, -3, 7, 0, -8}));
        std::vector<int> back(a.size());
        std::partial_sum(d.begin(), d.end(), back.begin());
        CHECK(back == a);
    }
    for (int t = 0; t < 200; ++t) {
        int n = rnd(1, 20), q = rnd(0, 15);
        std::vector<std::array<int, 3>> ups;
        std::vector<long long> brute(n, 0);
        for (int i = 0; i < q; ++i) {
            int l = rnd(0, n - 1), r = rnd(l, n - 1), v = rnd(-5, 5);
            ups.push_back({l, r, v});
            for (int j = l; j <= r; ++j) brute[j] += v;
        }
        CHECK(applyRangeAdds(n, ups) == brute);
    }
    {
        CHECK((corpFlightBookings({{1, 2, 10}, {2, 3, 20}, {2, 5, 25}}, 5) == std::vector<int>{10, 55, 45, 25, 25}));
        CHECK((corpFlightBookings({{1, 2, 10}, {2, 2, 15}}, 2) == std::vector<int>{10, 25}));
        CHECK(!carPooling({{2, 1, 5}, {3, 3, 7}}, 4));
        CHECK(carPooling({{2, 1, 5}, {3, 3, 7}}, 5));
        CHECK(carPooling({{3, 2, 7}, {3, 7, 9}, {8, 3, 9}}, 11));   // drop-off and pick-up at the same stop
    }
    for (int t = 0; t < 100; ++t) {                                 // 2D difference grid
        int m = rnd(1, 6), n = rnd(1, 6), q = rnd(0, 8);
        std::vector<std::vector<long long>> d(m + 1, std::vector<long long>(n + 1, 0));
        std::vector<std::vector<long long>> brute(m, std::vector<long long>(n, 0));
        for (int i = 0; i < q; ++i) {
            int r1 = rnd(0, m - 1), r2 = rnd(r1, m - 1), c1 = rnd(0, n - 1), c2 = rnd(c1, n - 1), v = rnd(-5, 5);
            addRect(d, r1, c1, r2, c2, v);
            for (int a = r1; a <= r2; ++a)
                for (int b = c1; b <= c2; ++b) brute[a][b] += v;
        }
        // the final grid is the 2D prefix sum of d
        std::vector<std::vector<long long>> out(m, std::vector<long long>(n, 0));
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < n; ++j)
                out[i][j] = d[i][j] + (i ? out[i - 1][j] : 0) + (j ? out[i][j - 1] : 0) - (i && j ? out[i - 1][j - 1] : 0);
        CHECK(out == brute);
    }

    // --- section 6: weighted pick
    {
        std::vector<long long> prefix{1, 4, 6};              // weights 1, 3, 2
        std::vector<int> expect{0, 1, 1, 1, 2, 2};
        bool ok = true;
        for (int target = 1; target <= 6; ++target) ok = ok && indexForTarget(prefix, target) == expect[target - 1];
        CHECK(ok);
        WeightedPicker wp({1, 3, 2}, 42);
        std::array<int, 3> hits{0, 0, 0};
        const int draws = 60000;
        for (int i = 0; i < draws; ++i) ++hits[wp.pick()];
        // expected 10000, 30000, 20000; allow 3 percent
        CHECK(std::abs(hits[0] - 10000) < 1800 && std::abs(hits[1] - 30000) < 1800 && std::abs(hits[2] - 20000) < 1800);
    }

    // --- section 7: cumulative counters
    {
        CHECK(bytesBetween(1000, 1500) == 500);
        CHECK(bytesBetween(0xFFFFFF00u, 0x100u) == 0x200u);  // the counter wrapped once
        NumArray na({-2, 0, 3, -5, 2, -1});
        CHECK(na.sumRange(0, 2) == 1 && na.sumRange(2, 5) == -1 && na.sumRange(0, 5) == -3);
        NumMatrix nm({{3, 0, 1, 4, 2}, {5, 6, 3, 2, 1}, {1, 2, 0, 1, 5}, {4, 1, 0, 1, 7}, {1, 0, 3, 0, 5}});
        CHECK(nm.sumRegion(2, 1, 4, 3) == 8 && nm.sumRegion(1, 1, 2, 2) == 11 && nm.sumRegion(1, 2, 2, 4) == 12);
        CHECK(subarraySum({1, 1, 1}, 2) == 2 && subarraySum({1, 2, 3}, 3) == 2);
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
