// Tutorial 05: subarray patterns and Kadane. Every function the tutorial shows is here,
// followed by brute-force versions and randomized checks.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 05_subarray_patterns_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <climits>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

// ---------------------------------------------------------------- section 2: Kadane
// Best sum of a non-empty subarray. Requires a.size() >= 1.
long long maxSubarraySum(const std::vector<int>& a) {
    long long cur = a[0];                              // best sum of a subarray that ends at the current element
    long long best = a[0];                             // best over all ends seen so far
    for (size_t i = 1; i < a.size(); ++i) {
        cur = std::max<long long>(a[i], cur + a[i]);   // extend the run, or start a new one here
        best = std::max(best, cur);
    }
    return best;
}

// Best sum of a possibly empty subarray: 0 if every element is negative.
long long maxSubarraySumOrEmpty(const std::vector<int>& a) {
    long long cur = 0, best = 0;
    for (int x : a) {
        cur = std::max<long long>(0, cur + x);         // a negative running sum is dropped
        best = std::max(best, cur);
    }
    return best;
}

struct Best {
    long long sum;
    int l, r;                                          // the subarray a[l..r], both ends included
};

Best maxSubarrayRange(const std::vector<int>& a) {
    Best best{a[0], 0, 0};
    long long cur = a[0];
    int start = 0;                                     // where the current run began
    for (int i = 1; i < (int)a.size(); ++i) {
        if (cur < 0) { cur = a[i]; start = i; }        // the run so far only hurts: start again at i
        else cur += a[i];
        if (cur > best.sum) best = {cur, start, i};
    }
    return best;
}

// The same answer from prefix sums (tutorial 04): best P[r + 1] - P[l] with l <= r.
long long maxSubarrayByPrefix(const std::vector<int>& a) {
    long long prefix = 0, minBefore = 0, best = LLONG_MIN;   // minBefore = smallest P[l] allowed for the current end
    for (int x : a) {
        prefix += x;                                   // this is P[r + 1]
        best = std::max(best, prefix - minBefore);
        minBefore = std::min(minBefore, prefix);       // P[r + 1] may now start a later subarray
    }
    return best;
}

// ---------------------------------------------------------------- section 2.3: divide and conquer
struct Node {
    long long sum, prefix, suffix, best;               // best prefix, best suffix and best subarray are all non-empty
};

Node leaf(int x) { return {x, x, x, x}; }

Node combine(const Node& L, const Node& R) {
    return {L.sum + R.sum,
            std::max(L.prefix, L.sum + R.prefix),
            std::max(R.suffix, R.sum + L.suffix),
            std::max({L.best, R.best, L.suffix + R.prefix})};
}

Node maxSubarrayDC(const std::vector<int>& a, int lo, int hi) {   // the half-open range [lo, hi), non-empty
    if (hi - lo == 1) return leaf(a[lo]);
    int mid = lo + (hi - lo) / 2;
    return combine(maxSubarrayDC(a, lo, mid), maxSubarrayDC(a, mid, hi));
}

// ---------------------------------------------------------------- section 3: Kadane with more state
// LeetCode 1186: best sum of a non-empty subarray after deleting at most one element of it.
long long maxSumOneDeletion(const std::vector<int>& a) {
    const long long NEG = LLONG_MIN / 4;               // "impossible", but safe to add to
    long long noDel = a[0], oneDel = NEG, best = a[0]; // best sums of a subarray ending here
    for (size_t i = 1; i < a.size(); ++i) {
        oneDel = std::max(noDel, oneDel + a[i]);       // delete a[i] itself, or extend a run that already lost one
        noDel = std::max<long long>(a[i], noDel + a[i]);
        best = std::max({best, noDel, oneDel});
    }
    return best;
}

// Largest sum of a rectangle of cells, with O(rows^2 * cols) work. Requires a non-empty grid.
long long maxRectangleSum(const std::vector<std::vector<int>>& g) {
    int m = (int)g.size(), n = (int)g[0].size();
    long long best = LLONG_MIN;
    for (int top = 0; top < m; ++top) {
        std::vector<long long> col(n, 0);              // col[j] = g[top][j] + ... + g[bottom][j]
        for (int bottom = top; bottom < m; ++bottom) {
            long long cur = 0;
            for (int j = 0; j < n; ++j) {
                col[j] += g[bottom][j];
                cur = std::max(col[j], cur + col[j]);  // Kadane along the columns
                best = std::max(best, cur);
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------- section 4: maximum product
long long maxProductSubarray(const std::vector<int>& a) {
    long long hi = a[0], lo = a[0], best = a[0];       // largest and smallest product of a subarray ending here
    for (size_t i = 1; i < a.size(); ++i) {
        long long x = a[i];
        if (x < 0) std::swap(hi, lo);                  // a negative factor turns the largest product into the smallest
        hi = std::max(x, hi * x);
        lo = std::min(x, lo * x);
        best = std::max(best, hi);
    }
    return best;
}

// The same answer from the two ends: the best product is a prefix or a suffix of a zero-free stretch.
long long maxProductScan(const std::vector<int>& a) {
    int n = (int)a.size();
    long long best = a[0], front = 1, back = 1;
    for (int i = 0; i < n; ++i) {
        front = (front == 0 ? 1 : front) * a[i];       // product from the last zero (or the start) up to i
        back = (back == 0 ? 1 : back) * a[n - 1 - i];  // product from the end down to n - 1 - i
        best = std::max({best, front, back});
    }
    return best;
}

// ---------------------------------------------------------------- section 5: circular array
// LeetCode 918: best sum of a non-empty subarray of the array viewed as a circle.
long long maxCircularSubarray(const std::vector<int>& a) {
    long long total = 0, curMax = 0, curMin = 0, bestMax = a[0], bestMin = a[0];
    for (int x : a) {
        curMax = std::max<long long>(x, curMax + x);
        bestMax = std::max(bestMax, curMax);
        curMin = std::min<long long>(x, curMin + x);
        bestMin = std::min(bestMin, curMin);
        total += x;
    }
    if (bestMax < 0) return bestMax;                   // every element is negative: total - bestMin would be the empty subarray
    return std::max(bestMax, total - bestMin);         // a middle block, or everything except the worst middle block
}

// ---------------------------------------------------------------- section 6: majority vote
// The only value that can be a majority element. Meaningless if there is none.
int majorityCandidate(const std::vector<int>& a) {
    int candidate = 0, count = 0;
    for (int x : a) {
        if (count == 0) candidate = x;                 // the votes so far cancelled out: nominate x
        count += (x == candidate) ? 1 : -1;
    }
    return candidate;
}

// The value that occurs more than n / 2 times, if there is one.
std::optional<int> majorityElement(const std::vector<int>& a) {
    if (a.empty()) return std::nullopt;
    int c = majorityCandidate(a);
    long long occurrences = std::count(a.begin(), a.end(), c);   // the second pass: confirm it
    if (occurrences * 2 > (long long)a.size()) return c;
    return std::nullopt;
}

// Misra-Gries: at most k - 1 counters. Every value occurring more than n / k times is left in the map.
std::unordered_map<int, int> misraGries(const std::vector<int>& a, int k) {
    std::unordered_map<int, int> counters;
    for (int x : a) {
        auto it = counters.find(x);
        if (it != counters.end()) {
            ++it->second;
        } else if ((int)counters.size() < k - 1) {
            counters[x] = 1;
        } else {                                       // k different values now: cancel one of each
            for (auto i = counters.begin(); i != counters.end();) {
                if (--i->second == 0) i = counters.erase(i);
                else ++i;
            }
        }
    }
    return counters;
}

// All values that occur more than n / k times (LeetCode 229 is k = 3), in increasing order.
std::vector<int> heavyHitters(const std::vector<int>& a, int k) {
    std::vector<int> result;
    for (const auto& [value, estimate] : misraGries(a, k)) {
        long long occurrences = std::count(a.begin(), a.end(), value);   // confirm with a second pass
        if (occurrences * k > (long long)a.size()) result.push_back(value);
    }
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------- section 7: stock trading
// One transaction: Kadane on the daily changes, with the empty answer (0) allowed.
long long maxProfitOne(const std::vector<int>& prices) {
    long long cur = 0, best = 0;
    for (size_t i = 1; i < prices.size(); ++i) {
        cur = std::max<long long>(0, cur + prices[i] - prices[i - 1]);   // the best gain of a run that ends today
        best = std::max(best, cur);
    }
    return best;
}

// Any number of transactions (LeetCode 122): collect every daily rise.
long long maxProfitUnlimited(const std::vector<int>& prices) {
    long long profit = 0;
    for (size_t i = 1; i < prices.size(); ++i)
        if (prices[i] > prices[i - 1]) profit += prices[i] - prices[i - 1];
    return profit;
}

// Any number of transactions with a fee per transaction (LeetCode 714).
long long maxProfitFee(const std::vector<int>& prices, int fee) {
    long long hold = -(long long)prices[0];            // best balance while holding a share
    long long cash = 0;                                // best balance while holding nothing
    for (size_t i = 1; i < prices.size(); ++i) {
        cash = std::max(cash, hold + prices[i] - fee); // sell today (pay the fee), or stay out
        hold = std::max(hold, cash - prices[i]);       // buy today, or keep holding
    }
    return cash;
}

// Any number of transactions, but no buying on the day after a sale (LeetCode 309).
long long maxProfitCooldown(const std::vector<int>& prices) {
    long long hold = -(long long)prices[0];            // holding a share
    long long sold = LLONG_MIN / 4;                    // sold today (impossible on day 0)
    long long idle = 0;                                // holding nothing and free to buy tomorrow
    for (size_t i = 1; i < prices.size(); ++i) {
        long long newHold = std::max(hold, idle - prices[i]);
        long long newSold = hold + prices[i];
        long long newIdle = std::max(idle, sold);      // yesterday's sale becomes tradable again today
        hold = newHold;
        sold = newSold;
        idle = newIdle;
    }
    return std::max(idle, sold);
}

// At most k transactions (LeetCode 188; LeetCode 123 is k = 2).
long long maxProfitAtMostK(int k, const std::vector<int>& prices) {
    if (2 * (long long)k >= (long long)prices.size()) return maxProfitUnlimited(prices);   // k no longer limits anything
    std::vector<long long> buy(k + 1, LLONG_MIN / 4);  // buy[j]: best balance while holding the share of transaction j
    std::vector<long long> sell(k + 1, 0);             // sell[j]: best balance with j transactions completed
    for (int price : prices)
        for (int j = 1; j <= k; ++j) {
            buy[j] = std::max(buy[j], sell[j - 1] - price);
            sell[j] = std::max(sell[j], buy[j] + price);
        }
    return sell[k];
}

// ---------------------------------------------------------------- answers
int maxSubArray(const std::vector<int>& nums) { return (int)maxSubarraySum(nums); }                 // LeetCode 53
int maxProduct(const std::vector<int>& nums) { return (int)maxProductSubarray(nums); }             // LeetCode 152
int maxSubarraySumCircular(const std::vector<int>& nums) { return (int)maxCircularSubarray(nums); } // LeetCode 918
int majorityElementLC(const std::vector<int>& nums) { return majorityCandidate(nums); }             // LeetCode 169
int maxProfitII(const std::vector<int>& prices) { return (int)maxProfitUnlimited(prices); }        // LeetCode 122

// ---------------------------------------------------------------- brute forces
static std::vector<int> randVec(std::mt19937& rng, int n, int lo, int hi) {
    std::vector<int> v(n);
    for (int& x : v) x = std::uniform_int_distribution<int>(lo, hi)(rng);
    return v;
}

static long long bruteMaxSub(const std::vector<int>& a) {            // non-empty
    long long best = LLONG_MIN;
    for (size_t i = 0; i < a.size(); ++i) {
        long long s = 0;
        for (size_t j = i; j < a.size(); ++j) { s += a[j]; best = std::max(best, s); }
    }
    return best;
}

static long long bruteMaxProduct(const std::vector<int>& a) {
    long long best = LLONG_MIN;
    for (size_t i = 0; i < a.size(); ++i) {
        long long p = 1;
        for (size_t j = i; j < a.size(); ++j) { p *= a[j]; best = std::max(best, p); }
    }
    return best;
}

static long long bruteCircular(const std::vector<int>& a) {
    int n = (int)a.size();
    long long best = LLONG_MIN;
    for (int s = 0; s < n; ++s) {
        long long sum = 0;
        for (int len = 1; len <= n; ++len) { sum += a[(s + len - 1) % n]; best = std::max(best, sum); }
    }
    return best;
}

static long long bruteOneDeletion(const std::vector<int>& a) {
    long long best = LLONG_MIN;
    int n = (int)a.size();
    for (int l = 0; l < n; ++l) {
        long long s = 0;
        for (int r = l; r < n; ++r) {
            s += a[r];
            best = std::max(best, s);
            if (r > l)
                for (int d = l; d <= r; ++d) best = std::max(best, s - a[d]);   // delete one element, the rest is not empty
        }
    }
    return best;
}

static long long bruteRectangle(const std::vector<std::vector<int>>& g) {
    int m = (int)g.size(), n = (int)g[0].size();
    long long best = LLONG_MIN;
    for (int r1 = 0; r1 < m; ++r1)
        for (int r2 = r1; r2 < m; ++r2)
            for (int c1 = 0; c1 < n; ++c1)
                for (int c2 = c1; c2 < n; ++c2) {
                    long long s = 0;
                    for (int i = r1; i <= r2; ++i)
                        for (int j = c1; j <= c2; ++j) s += g[i][j];
                    best = std::max(best, s);
                }
    return best;
}

// Exhaustive search over every buy/sell decision. blockedUntil = first day on which buying is allowed again.
static long long bruteStock(const std::vector<int>& p, int day, bool holding, int txLeft, int blockedUntil, int fee, int cooldown) {
    int n = (int)p.size();
    if (day == n) return holding ? LLONG_MIN / 4 : 0;                 // a share still held at the end is not allowed
    long long best = bruteStock(p, day + 1, holding, txLeft, blockedUntil, fee, cooldown);   // do nothing today
    if (!holding && txLeft > 0 && day >= blockedUntil)
        best = std::max(best, -p[day] + bruteStock(p, day + 1, true, txLeft - 1, blockedUntil, fee, cooldown));
    if (holding)
        best = std::max(best, p[day] - fee + bruteStock(p, day + 1, false, txLeft, day + 1 + cooldown, fee, cooldown));
    return best;
}

int main() {
    std::mt19937 rng(20261026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // examples quoted in the tutorial
    CHECK(maxSubarraySum({-2, 1, -3, 4, -1, 2, 1, -5, 4}) == 6);
    CHECK(maxSubarraySum({-3, -1, -2}) == -1 && maxSubarraySumOrEmpty({-3, -1, -2}) == 0);
    {
        Best b = maxSubarrayRange({-2, 1, -3, 4, -1, 2, 1, -5, 4});
        CHECK(b.sum == 6 && b.l == 3 && b.r == 6);
        Best c = maxSubarrayRange({5, -9, 6, -2, 3, -7, 4});
        CHECK(c.sum == 7 && c.l == 2 && c.r == 4);
    }
    CHECK(maxProductSubarray({2, 3, -2, 4}) == 6 && maxProductSubarray({-2, 0, -1}) == 0 && maxProductSubarray({-2, 3, -4}) == 24);
    CHECK(maxCircularSubarray({5, -3, 5}) == 10 && maxCircularSubarray({-3, -2, -3}) == -2 && maxCircularSubarray({3, -1, 2, -1}) == 4);
    CHECK(majorityCandidate({2, 2, 1, 1, 1, 2, 2}) == 2 && majorityCandidate({1, 2, 3}) == 3 && !majorityElement({1, 2, 3}));
    {
        auto m = misraGries({1, 2, 1, 3, 1, 2, 1, 4, 1}, 3);
        CHECK(m.size() == 1 && m.at(1) == 3);
        CHECK((heavyHitters({1, 2, 1, 3, 1, 2, 1, 4, 1}, 3) == std::vector<int>{1}));
        CHECK((heavyHitters({3, 2, 3}, 3) == std::vector<int>{3}));
        CHECK((heavyHitters({1, 2}, 3) == std::vector<int>{1, 2}));
    }
    CHECK(maxProfitOne({7, 1, 5, 3, 6, 4}) == 5 && maxProfitUnlimited({7, 1, 5, 3, 6, 4}) == 7);
    CHECK(maxProfitCooldown({1, 2, 3, 0, 2}) == 3 && maxProfitFee({1, 3, 2, 8, 4, 9}, 2) == 8);
    CHECK(maxProfitAtMostK(2, {3, 3, 5, 0, 0, 3, 1, 4}) == 6 && maxProfitAtMostK(1, {3, 3, 5, 0, 0, 3, 1, 4}) == 4);
    CHECK(maxProfitAtMostK(2, {1, 2, 3, 4, 5}) == 4 && maxProfitAtMostK(3, {7, 6, 4, 3, 1}) == 0);
    CHECK(maxSubArray({1}) == 1 && maxSubArray({5, 4, -1, 7, 8}) == 23);
    CHECK(maxProduct({2, 3, -2, 4}) == 6 && maxSubarraySumCircular({1, -2, 3, -2}) == 3 && majorityElementLC({3, 2, 3}) == 3 && maxProfitII({1, 2, 3, 4, 5}) == 4);

    // Kadane family against brute force
    for (int t = 0; t < 1500; ++t) {
        auto a = randVec(rng, rnd(1, 12), -6, 6);
        long long brute = bruteMaxSub(a);
        CHECK(maxSubarraySum(a) == brute);
        CHECK(maxSubarrayByPrefix(a) == brute);
        CHECK(maxSubarrayDC(a, 0, (int)a.size()).best == brute);
        CHECK(maxSubarraySumOrEmpty(a) == std::max(0LL, brute));
        Best b = maxSubarrayRange(a);
        long long s = 0;
        for (int i = b.l; i <= b.r; ++i) s += a[i];
        CHECK(b.sum == brute && s == brute && 0 <= b.l && b.l <= b.r && b.r < (int)a.size());
        CHECK(maxSumOneDeletion(a) == bruteOneDeletion(a));
        CHECK(maxCircularSubarray(a) == bruteCircular(a));
    }
    // divide and conquer: the other three fields of the node
    for (int t = 0; t < 500; ++t) {
        auto a = randVec(rng, rnd(1, 10), -5, 5);
        Node nd = maxSubarrayDC(a, 0, (int)a.size());
        long long total = 0, bestPre = LLONG_MIN, bestSuf = LLONG_MIN, s = 0;
        for (int x : a) { s += x; bestPre = std::max(bestPre, s); }
        total = s;
        s = 0;
        for (int i = (int)a.size() - 1; i >= 0; --i) { s += a[i]; bestSuf = std::max(bestSuf, s); }
        CHECK(nd.sum == total && nd.prefix == bestPre && nd.suffix == bestSuf);
    }
    // 2D
    for (int t = 0; t < 400; ++t) {
        int m = rnd(1, 5), n = rnd(1, 5);
        std::vector<std::vector<int>> g(m);
        for (auto& row : g) row = randVec(rng, n, -5, 5);
        CHECK(maxRectangleSum(g) == bruteRectangle(g));
    }
    // products (zeros and negatives included)
    for (int t = 0; t < 2000; ++t) {
        auto a = randVec(rng, rnd(1, 10), -3, 3);
        long long brute = bruteMaxProduct(a);
        CHECK(maxProductSubarray(a) == brute);
        CHECK(maxProductScan(a) == brute);
    }
    // majority and heavy hitters
    for (int t = 0; t < 1500; ++t) {
        auto a = randVec(rng, rnd(0, 14), 0, rnd(1, 4));
        std::map<int, int> freq;
        for (int x : a) ++freq[x];
        std::optional<int> expect;
        for (auto [v, c] : freq) if (c * 2 > (int)a.size()) expect = v;
        CHECK(majorityElement(a) == expect);
        if (expect) CHECK(majorityCandidate(a) == *expect);
        for (int k = 2; k <= 5; ++k) {
            std::vector<int> exact;
            for (auto [v, c] : freq) if (c * k > (int)a.size()) exact.push_back(v);
            CHECK(heavyHitters(a, k) == exact);
            auto mg = misraGries(a, k);
            CHECK((int)mg.size() <= k - 1);
            for (auto [v, c] : freq) if (c * k > (int)a.size()) CHECK(mg.count(v) == 1);   // no heavy hitter is ever lost
            for (auto [v, est] : mg) CHECK(est <= freq[v] && (freq[v] - est) * k <= (int)a.size());   // 0 <= error <= n / k
        }
    }
    // stock family
    for (int t = 0; t < 1200; ++t) {
        auto p = randVec(rng, rnd(1, 9), 1, 9);
        CHECK(maxProfitOne(p) == bruteStock(p, 0, false, 1, 0, 0, 0));
        CHECK(maxProfitUnlimited(p) == bruteStock(p, 0, false, 99, 0, 0, 0));
        CHECK(maxProfitCooldown(p) == bruteStock(p, 0, false, 99, 0, 0, 1));
        int fee = rnd(0, 3);
        CHECK(maxProfitFee(p, fee) == bruteStock(p, 0, false, 99, 0, fee, 0));
        for (int k = 1; k <= 3; ++k) CHECK(maxProfitAtMostK(k, p) == bruteStock(p, 0, false, k, 0, 0, 0));
        CHECK(maxProfitAtMostK(1000000000, p) == maxProfitUnlimited(p));
    }
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
