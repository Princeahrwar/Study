// Tutorial 02: two pointers: the code blocks below are copied verbatim from the tutorial.
// main() checks each one against a brute force on random inputs.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 02_two_pointers_demo.cpp -o demo && ./demo
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
bool isPalindrome(const std::string& s) {
    int l = 0, r = (int)s.size() - 1;   // int, so an empty string gives r = -1 and the loop is skipped
    while (l < r) {
        if (s[l] != s[r]) return false;
        ++l;
        --r;
    }
    return true;
}

// Sorted array: do two different elements add up to target? Returns their indices, or {-1, -1}.
std::pair<int, int> pairWithSum(const std::vector<int>& a, int target) {
    int l = 0, r = (int)a.size() - 1;
    while (l < r) {
        long long sum = (long long)a[l] + a[r];       // long long: two ints can overflow int
        if (sum == target) return {l, r};
        if (sum < target) ++l;                        // too small: a[l] cannot pair with anything left of r
        else              --r;                        // too big: a[r] cannot pair with anything right of l
    }
    return {-1, -1};
}

// Sorted array: how many pairs (i < j) have a[i] + a[j] < target?
long long countPairsBelow(const std::vector<int>& a, int target) {
    long long count = 0;
    int l = 0, r = (int)a.size() - 1;
    while (l < r) {
        if ((long long)a[l] + a[r] < target) {
            count += r - l;                           // a[l] pairs with every element from l+1 to r
            ++l;
        } else {
            --r;
        }
    }
    return count;
}

// Two sorted arrays: the values that appear in both (each value reported once).
std::vector<int> intersectSorted(const std::vector<int>& a, const std::vector<int>& b) {
    std::vector<int> out;
    size_t i = 0, j = 0;                              // one pointer per array, both only move forward
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) ++i;
        else if (a[i] > b[j]) ++j;
        else {
            if (out.empty() || out.back() != a[i]) out.push_back(a[i]);
            ++i;
            ++j;
        }
    }
    return out;
}

bool isSubsequence(const std::string& s, const std::string& t) {
    size_t i = 0;                                     // how much of s we have matched so far
    for (size_t j = 0; j < t.size() && i < s.size(); ++j)
        if (t[j] == s[i]) ++i;
    return i == s.size();
}

// Rearrange so all negative numbers come before all non-negative ones (order inside each group is not kept).
void partitionNegatives(std::vector<int>& a) {
    int l = 0, r = (int)a.size() - 1;
    while (l <= r) {
        if (a[l] < 0) ++l;                            // a[l] is already in the right zone
        else std::swap(a[l], a[r--]);                 // send it to the right zone; the value swapped in is unchecked
    }
}

bool isPalindromeAlnum(const std::string& s) {
    int l = 0, r = (int)s.size() - 1;
    while (l < r) {
        while (l < r && !std::isalnum((unsigned char)s[l])) ++l;   // skip spaces and punctuation
        while (l < r && !std::isalnum((unsigned char)s[r])) --r;
        if (std::tolower((unsigned char)s[l]) != std::tolower((unsigned char)s[r])) return false;
        ++l;
        --r;
    }
    return true;
}

std::vector<int> twoSumSorted(const std::vector<int>& numbers, int target) {
    int l = 0, r = (int)numbers.size() - 1;
    while (l < r) {
        int sum = numbers[l] + numbers[r];
        if (sum == target) return {l + 1, r + 1};     // the problem wants 1-based positions
        if (sum < target) ++l;
        else              --r;
    }
    return {};
}

int maxArea(const std::vector<int>& height) {
    int l = 0, r = (int)height.size() - 1, best = 0;
    while (l < r) {
        best = std::max(best, std::min(height[l], height[r]) * (r - l));
        if (height[l] < height[r]) ++l;               // the shorter line limits the area: keeping it can only get worse
        else                       --r;
    }
    return best;
}

std::vector<std::vector<int>> threeSum(std::vector<int> nums) {   // by value: we sort our own copy
    std::sort(nums.begin(), nums.end());
    std::vector<std::vector<int>> out;
    int n = (int)nums.size();
    for (int i = 0; i + 2 < n; ++i) {
        if (nums[i] > 0) break;                                    // smallest value is positive: no more triples
        if (i > 0 && nums[i] == nums[i - 1]) continue;             // same first value as before: skip
        int l = i + 1, r = n - 1;
        while (l < r) {
            long long sum = (long long)nums[i] + nums[l] + nums[r];
            if (sum < 0) ++l;
            else if (sum > 0) --r;
            else {
                out.push_back({nums[i], nums[l], nums[r]});
                ++l;
                --r;
                while (l < r && nums[l] == nums[l - 1]) ++l;       // skip duplicate second values
            }
        }
    }
    return out;
}

void sortColors(std::vector<int>& nums) {
    int lo = 0, mid = 0, hi = (int)nums.size() - 1;
    while (mid <= hi) {
        if (nums[mid] == 0)      std::swap(nums[lo++], nums[mid++]);
        else if (nums[mid] == 1) ++mid;
        else                     std::swap(nums[mid], nums[hi--]);   // do not advance mid: the value swapped in is unchecked
    }
}

int trap(const std::vector<int>& height) {
    int l = 0, r = (int)height.size() - 1;
    int leftMax = 0, rightMax = 0, water = 0;
    while (l < r) {
        if (height[l] < height[r]) {                  // the left side is the limiting side
            leftMax = std::max(leftMax, height[l]);
            water += leftMax - height[l];
            ++l;
        } else {
            rightMax = std::max(rightMax, height[r]);
            water += rightMax - height[r];
            --r;
        }
    }
    return water;
}

// ---------------------------------------------------------------- tests
static std::vector<int> randVec(std::mt19937& rng, int n, int lo, int hi) {
    std::vector<int> v(n);
    for (int& x : v) x = std::uniform_int_distribution<int>(lo, hi)(rng);
    return v;
}
static std::string randStr(std::mt19937& rng, int n, const std::string& alphabet) {
    std::string s;
    for (int i = 0; i < n; ++i) s += alphabet[std::uniform_int_distribution<size_t>(0, alphabet.size() - 1)(rng)];
    return s;
}

int main() {
    std::mt19937 rng(20261026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // palindromes
    CHECK(isPalindrome("") && isPalindrome("a") && isPalindrome("abba") && !isPalindrome("abca"));
    CHECK(isPalindromeAlnum("A man, a plan, a canal: Panama") && !isPalindromeAlnum("race a car") && isPalindromeAlnum(" "));
    for (int t = 0; t < 300; ++t) {
        auto s = randStr(rng, rnd(0, 8), "ab");
        std::string r(s.rbegin(), s.rend());
        CHECK(isPalindrome(s) == (s == r));
        auto u = randStr(rng, rnd(0, 8), "aA b,");
        std::string clean;
        for (char c : u) if (std::isalnum((unsigned char)c)) clean += (char)std::tolower((unsigned char)c);
        std::string rc(clean.rbegin(), clean.rend());
        CHECK(isPalindromeAlnum(u) == (clean == rc));
    }
    // pair with sum, count pairs below, two sum II
    for (int t = 0; t < 500; ++t) {
        auto a = randVec(rng, rnd(0, 9), -6, 6);
        std::sort(a.begin(), a.end());
        int target = rnd(-10, 10);
        bool exists = false;
        long long below = 0;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = i + 1; j < a.size(); ++j) {
                exists = exists || a[i] + a[j] == target;
                if (a[i] + a[j] < target) ++below;
            }
        auto p = pairWithSum(a, target);
        if (!exists) CHECK(p.first == -1 && p.second == -1);
        else CHECK(p.first < p.second && a[p.first] + a[p.second] == target);
        CHECK(countPairsBelow(a, target) == below);
        auto q = twoSumSorted(a, target);
        if (!exists) CHECK(q.empty());
        else CHECK(q.size() == 2 && q[0] < q[1] && a[q[0] - 1] + a[q[1] - 1] == target);
    }
    CHECK(pairWithSum({INT_MAX, INT_MAX}, -2).first == -1);          // the sum would overflow an int
    // intersection of sorted arrays
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 10), 0, 8), b = randVec(rng, rnd(0, 10), 0, 8);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        std::vector<int> expect;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(expect));
        expect.erase(std::unique(expect.begin(), expect.end()), expect.end());
        CHECK(intersectSorted(a, b) == expect);
    }
    // subsequence
    CHECK(isSubsequence("abc", "ahbgdc") && !isSubsequence("axc", "ahbgdc") && isSubsequence("", "abc") && !isSubsequence("a", ""));
    for (int t = 0; t < 300; ++t) {
        auto s = randStr(rng, rnd(0, 4), "ab"), u = randStr(rng, rnd(0, 8), "ab");
        bool brute = false;
        int n = (int)u.size();
        for (int mask = 0; mask < (1 << n); ++mask) {
            std::string sub;
            for (int i = 0; i < n; ++i) if (mask >> i & 1) sub += u[i];
            brute = brute || sub == s;
        }
        CHECK(isSubsequence(s, u) == brute);
    }
    // partition negatives and sort colors
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 10), -3, 3);
        auto before = a;
        partitionNegatives(a);
        bool seenNonNeg = false, okOrder = true;
        for (int x : a) {
            if (x >= 0) seenNonNeg = true;
            else if (seenNonNeg) okOrder = false;
        }
        std::sort(before.begin(), before.end());
        auto after = a;
        std::sort(after.begin(), after.end());
        CHECK(okOrder && before == after);
        auto c = randVec(rng, rnd(0, 12), 0, 2);
        auto expect = c;
        std::sort(expect.begin(), expect.end());
        sortColors(c);
        CHECK(c == expect);
    }
    // container with most water
    CHECK(maxArea({1, 8, 6, 2, 5, 4, 8, 3, 7}) == 49);
    for (int t = 0; t < 300; ++t) {
        auto h = randVec(rng, rnd(0, 9), 0, 9);
        int best = 0;
        for (size_t i = 0; i < h.size(); ++i)
            for (size_t j = i + 1; j < h.size(); ++j) best = std::max(best, std::min(h[i], h[j]) * (int)(j - i));
        CHECK(maxArea(h) == best);
    }
    // 3Sum: same set of triples as a brute force, no duplicates
    CHECK((threeSum({-1, 0, 1, 2, -1, -4}) == std::vector<std::vector<int>>{{-1, -1, 2}, {-1, 0, 1}}));
    CHECK(threeSum({0, 0, 0}).size() == 1 && threeSum({0, 1, 1}).empty() && threeSum({}).empty());
    for (int t = 0; t < 400; ++t) {
        auto a = randVec(rng, rnd(0, 10), -4, 4);
        std::set<std::vector<int>> expect;
        for (size_t i = 0; i < a.size(); ++i)
            for (size_t j = i + 1; j < a.size(); ++j)
                for (size_t k = j + 1; k < a.size(); ++k)
                    if (a[i] + a[j] + a[k] == 0) {
                        std::vector<int> tr{a[i], a[j], a[k]};
                        std::sort(tr.begin(), tr.end());
                        expect.insert(tr);
                    }
        auto got = threeSum(a);
        std::set<std::vector<int>> gotSet(got.begin(), got.end());
        CHECK(gotSet == expect && got.size() == gotSet.size());
    }
    // trapping rain water
    CHECK(trap({0, 1, 0, 2, 1, 0, 1, 3, 2, 1, 2, 1}) == 6);
    CHECK(trap({4, 2, 0, 3, 2, 5}) == 9);
    for (int t = 0; t < 300; ++t) {
        auto h = randVec(rng, rnd(0, 10), 0, 6);
        int water = 0;
        for (size_t i = 0; i < h.size(); ++i) {
            int lm = 0, rm = 0;
            for (size_t j = 0; j <= i; ++j) lm = std::max(lm, h[j]);
            for (size_t j = i; j < h.size(); ++j) rm = std::max(rm, h[j]);
            water += std::min(lm, rm) - h[i];
        }
        CHECK(trap(h) == water);
    }
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
