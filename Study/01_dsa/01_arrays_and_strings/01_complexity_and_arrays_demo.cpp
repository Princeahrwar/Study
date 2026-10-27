// Tutorial 01: complexity and arrays in memory: the code blocks below are copied verbatim from the tutorial.
// main() checks each one against a brute force on random inputs.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 01_complexity_and_arrays_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <iterator>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

// ---------------------------------------------------------------- tutorial code
bool hasDuplicateBrute(const std::vector<int>& a) {
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = i + 1; j < a.size(); ++j)
            if (a[i] == a[j]) return true;
    return false;
}

bool hasDuplicateSorted(std::vector<int> a) {   // by value: we sort our own copy
    std::sort(a.begin(), a.end());
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i] == a[i - 1]) return true;
    return false;
}

long long sumRowMajor(const std::vector<int>& grid, int N) {
    long long sum = 0;
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c) sum += grid[r * N + c];   // next element is next in memory
    return sum;
}

long long sumColumnMajor(const std::vector<int>& grid, int N) {
    long long sum = 0;
    for (int c = 0; c < N; ++c)
        for (int r = 0; r < N; ++r) sum += grid[r * N + c];   // next element is N ints away
    return sum;
}

int longestRun(const std::vector<int>& a) {
    if (a.empty()) return 0;
    int best = 1, cur = 1;                 // cur is the running state: length of the current run
    for (size_t i = 1; i < a.size(); ++i) {
        cur = (a[i] == a[i - 1]) ? cur + 1 : 1;
        best = std::max(best, cur);
    }
    return best;
}

// Keep only the even numbers, preserving order. Returns the new length.
size_t keepEven(std::vector<int>& a) {
    size_t w = 0;                          // write index: next slot to fill
    for (size_t r = 0; r < a.size(); ++r)  // read index: visits every element once
        if (a[r] % 2 == 0) a[w++] = a[r];
    a.resize(w);
    return w;
}

// Remove a[i] in O(1) when order does not matter: overwrite it with the last element.
void eraseUnordered(std::vector<int>& a, size_t i) {
    a[i] = a.back();
    a.pop_back();
}

void reverseRange(std::vector<int>& a, size_t l, size_t r) {   // reverses a[l, r): r is one past the end
    while (l + 1 < r) std::swap(a[l++], a[--r]);
}

bool containsDuplicate(const std::vector<int>& nums) {
    std::unordered_set<int> seen;
    for (int x : nums)
        if (!seen.insert(x).second) return true;   // insert fails if x is already there
    return false;
}

int removeElement(std::vector<int>& nums, int val) {
    int w = 0;
    for (int r = 0; r < (int)nums.size(); ++r)
        if (nums[r] != val) nums[w++] = nums[r];
    return w;
}

int removeDuplicates(std::vector<int>& nums) {
    if (nums.empty()) return 0;
    int w = 1;                                  // nums[0] is always kept
    for (int r = 1; r < (int)nums.size(); ++r)
        if (nums[r] != nums[w - 1]) nums[w++] = nums[r];
    return w;
}

void moveZeroes(std::vector<int>& nums) {
    int w = 0;
    for (int r = 0; r < (int)nums.size(); ++r)
        if (nums[r] != 0) std::swap(nums[w++], nums[r]);
}

int maxProfit(const std::vector<int>& prices) {
    int minPrice = INT_MAX, best = 0;
    for (int p : prices) {
        minPrice = std::min(minPrice, p);         // cheapest day so far
        best = std::max(best, p - minPrice);      // sell today
    }
    return best;
}

void merge(std::vector<int>& nums1, int m, const std::vector<int>& nums2, int n) {
    int i = m - 1, j = n - 1, w = m + n - 1;     // three fingers, all moving left
    while (j >= 0) {                             // when nums2 is used up, the rest of nums1 is already in place
        if (i >= 0 && nums1[i] > nums2[j]) nums1[w--] = nums1[i--];
        else                               nums1[w--] = nums2[j--];
    }
}

void rotate(std::vector<int>& nums, int k) {
    int n = nums.size();
    if (n == 0) return;
    k %= n;                                      // k can be larger than n
    reverseRange(nums, 0, n);                    // 1 2 3 4 5 6 7  ->  7 6 5 4 3 2 1
    reverseRange(nums, 0, k);                    // first k:           5 6 7 4 3 2 1
    reverseRange(nums, k, n);                    // the rest:          5 6 7 1 2 3 4
}

std::vector<int> productExceptSelf(const std::vector<int>& nums) {
    int n = nums.size();
    std::vector<int> out(n, 1);
    int prefix = 1;                              // product of everything left of i
    for (int i = 0; i < n; ++i) { out[i] = prefix; prefix *= nums[i]; }
    int suffix = 1;                              // product of everything right of i
    for (int i = n - 1; i >= 0; --i) { out[i] *= suffix; suffix *= nums[i]; }
    return out;
}

std::vector<int> twoSum(const std::vector<int>& nums, int target) {
    std::unordered_map<int, int> seenAt;         // value -> index where we saw it
    for (int i = 0; i < (int)nums.size(); ++i) {
        auto it = seenAt.find(target - nums[i]);
        if (it != seenAt.end()) return {it->second, i};
        seenAt[nums[i]] = i;
    }
    return {};
}

// ---------------------------------------------------------------- tests
static std::vector<int> randVec(std::mt19937& rng, int n, int lo, int hi) {
    std::vector<int> v(n);
    for (int& x : v) x = std::uniform_int_distribution<int>(lo, hi)(rng);
    return v;
}

int main() {
    std::mt19937 rng(20261026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // section 1.1: three ways to find a duplicate agree
    for (int t = 0; t < 500; ++t) {
        auto a = randVec(rng, rnd(0, 12), 0, 15);
        bool brute = false;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = i + 1; j < a.size(); ++j) brute = brute || a[i] == a[j];
        CHECK(hasDuplicateBrute(a) == brute);
        CHECK(hasDuplicateSorted(a) == brute);
        CHECK(containsDuplicate(a) == brute);
    }
    // section 2.4: both orders add up the same numbers
    {
        int N = 6;
        std::vector<int> g(N * N);
        std::iota(g.begin(), g.end(), 1);
        CHECK(sumRowMajor(g, N) == 36 * 37 / 2);
        CHECK(sumColumnMajor(g, N) == 36 * 37 / 2);
    }
    // section 3.1: longest run
    CHECK(longestRun({1, 1, 2, 2, 2, 3}) == 3);
    CHECK(longestRun({}) == 0);
    CHECK(longestRun({7}) == 1);
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 14), 0, 2);
        int best = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            size_t j = i;
            while (j < a.size() && a[j] == a[i]) ++j;
            best = std::max(best, (int)(j - i));
        }
        CHECK(longestRun(a) == best);
    }
    // section 3.3 and 3.4
    {
        std::vector<int> a{10, 20, 30, 40, 50};
        eraseUnordered(a, 1);
        CHECK((a == std::vector<int>{10, 50, 30, 40}));
        std::vector<int> b{1, 2, 3, 4, 5};
        reverseRange(b, 1, 4);
        CHECK((b == std::vector<int>{1, 4, 3, 2, 5}));
        std::vector<int> c{1, 2, 3};
        reverseRange(c, 0, 0);
        reverseRange(c, 2, 3);
        CHECK((c == std::vector<int>{1, 2, 3}));
    }
    // problem 7: remove element
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 12), 0, 4);
        int val = rnd(0, 4);
        std::vector<int> expect;
        for (int x : a) if (x != val) expect.push_back(x);
        int w = removeElement(a, val);
        CHECK(w == (int)expect.size() && std::equal(expect.begin(), expect.end(), a.begin()));
    }
    // problem 8: remove duplicates from a sorted array
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 12), 0, 6);
        std::sort(a.begin(), a.end());
        std::vector<int> expect = a;
        expect.erase(std::unique(expect.begin(), expect.end()), expect.end());
        int w = removeDuplicates(a);
        CHECK(w == (int)expect.size() && std::equal(expect.begin(), expect.end(), a.begin()));
    }
    // problem 9: move zeroes keeps the order of the non-zeros
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 12), 0, 3);
        std::vector<int> expect;
        for (int x : a) if (x) expect.push_back(x);
        expect.resize(a.size(), 0);
        moveZeroes(a);
        CHECK(a == expect);
    }
    // problem 10: best time to buy and sell
    CHECK(maxProfit({7, 1, 5, 3, 6, 4}) == 5);
    CHECK(maxProfit({7, 6, 4, 3, 1}) == 0);
    CHECK(maxProfit({}) == 0);
    for (int t = 0; t < 300; ++t) {
        auto p = randVec(rng, rnd(0, 10), 0, 20);
        int best = 0;
        for (size_t i = 0; i < p.size(); ++i)
            for (size_t j = i + 1; j < p.size(); ++j) best = std::max(best, p[j] - p[i]);
        CHECK(maxProfit(p) == best);
    }
    // problem 11: merge into nums1
    for (int t = 0; t < 300; ++t) {
        int m = rnd(0, 8), n = rnd(0, 8);
        auto x = randVec(rng, m, 0, 20), y = randVec(rng, n, 0, 20);
        std::sort(x.begin(), x.end());
        std::sort(y.begin(), y.end());
        std::vector<int> expect = x;
        expect.insert(expect.end(), y.begin(), y.end());
        std::sort(expect.begin(), expect.end());
        std::vector<int> nums1 = x;
        nums1.resize(m + n, 0);
        merge(nums1, m, y, n);
        CHECK(nums1 == expect);
    }
    // problem 12: rotate right by k
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 10), 0, 9);
        int k = rnd(0, 25);
        std::vector<int> expect = a;
        if (!a.empty()) std::rotate(expect.begin(), expect.end() - (k % (int)a.size()), expect.end());
        rotate(a, k);
        CHECK(a == expect);
    }
    // problem 13: product except self
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(1, 7), -3, 3);
        std::vector<int> expect(a.size(), 1);
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = 0; j < a.size(); ++j) if (i != j) expect[i] *= a[j];
        CHECK(productExceptSelf(a) == expect);
    }
    // problem 14: two sum
    for (int t = 0; t < 400; ++t) {
        auto a = randVec(rng, rnd(0, 9), -5, 5);
        int target = rnd(-8, 8);
        bool exists = false;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = i + 1; j < a.size(); ++j) exists = exists || a[i] + a[j] == target;
        auto r = twoSum(a, target);
        if (!exists) CHECK(r.empty());
        else CHECK(r.size() == 2 && r[0] != r[1] && a[r[0]] + a[r[1]] == target);
    }
    // numbers quoted in the tutorial text (capacity listing is what libstdc++ prints)
    {
        std::vector<int> v;
        std::vector<std::pair<size_t, size_t>> changes;
        size_t last = v.capacity();
        for (int i = 0; i < 100; ++i) {
            v.push_back(i);
            if (v.capacity() != last) { last = v.capacity(); changes.push_back({v.size(), last}); }
        }
        std::vector<std::pair<size_t, size_t>> expect{{1, 1}, {2, 2}, {3, 4}, {5, 8}, {9, 16}, {17, 32}, {33, 64}, {65, 128}};
        CHECK(changes == expect);
        // problem 2: element copies while pushing 1000 items
        long long dbl = 0, plus10 = 0;
        for (long long cap = 1, size = 0; size < 1000; ) { if (size == cap) { dbl += size; cap *= 2; } ++size; }
        for (long long cap = 10, size = 0; size < 1000; ) { if (size == cap) { plus10 += size; cap += 10; } ++size; }
        CHECK(dbl == 1023 && plus10 == 49500);
        // problem 1 loops: count iterations for n = 1024 (and 3^10 for loop b)
        long long a = 0, b = 0, c = 0, d = 0;
        int n = 1024;
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) if (i == j) ++a;
        for (long long i = 1; i < 59049; i *= 3) ++b;
        for (int i = 0; i < n; ++i) for (int j = 0; j < 100; ++j) ++c;
        for (int i = n; i > 0; i /= 2) for (int j = 0; j < i; ++j) ++d;
        CHECK(a == n && b == 10 && c == 100LL * n && d == 2 * n - 1);   // (a) is n increments but n*n tests
        long long calls = 0, calls2 = 0;
        std::function<void(int)> f = [&](int m) { ++calls; if (m <= 1) return; f(m / 2); f(m / 2); };
        std::function<void(int)> g = [&](int m) { ++calls2; if (m <= 1) return; g(m - 1); g(m - 1); };
        f(1024);
        g(10);
        CHECK(calls == 2047 && calls2 == 1023);
        // problem 4: addresses
        CHECK(0x1000 + 10 * 4 == 0x1028 && 0x1000 + 10 * 8 == 0x1050 && 0x1000 + 10 == 0x100A);
        CHECK(0x2000 + (2 * 4 + 1) * 4 == 0x2024 && 0x1000 + 100 * 4 == 0x1190);
    }
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
