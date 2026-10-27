// Tutorial 03: sliding window: the code blocks below are copied verbatim from the tutorial.
// main() checks each one against a brute force on random inputs.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 03_sliding_window_demo.cpp -o demo && ./demo
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
// Largest sum of any k consecutive elements (requires 1 <= k <= a.size()).
long long maxSumOfK(const std::vector<int>& a, int k) {
    long long window = 0;
    for (int i = 0; i < k; ++i) window += a[i];      // the first window
    long long best = window;
    for (int r = k; r < (int)a.size(); ++r) {
        window += a[r];                              // slide: add the new element ...
        window -= a[r - k];                          // ... and drop the oldest one
        best = std::max(best, window);
    }
    return best;
}

// How many windows of size k have k different values?
int countDistinctWindows(const std::vector<int>& a, int k) {
    std::unordered_map<int, int> freq;               // value -> how many times it is in the window
    int count = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        ++freq[a[r]];                                // add the new element
        if (r >= k) {                                // the window now has k+1 elements: drop the oldest
            if (--freq[a[r - k]] == 0) freq.erase(a[r - k]);
        }
        if (r >= k - 1 && (int)freq.size() == k) ++count;
    }
    return count;
}

// Non-negative numbers: length of the longest subarray whose sum is at most limit (limit >= 0).
int longestSumAtMost(const std::vector<int>& a, long long limit) {
    long long sum = 0;                               // window state: the sum of a[l..r]
    int best = 0, l = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        sum += a[r];                                 // grow: take a[r] into the window
        while (sum > limit) sum -= a[l++];           // invalid: shrink from the left until valid again
        best = std::max(best, r - l + 1);            // the window is valid here: record its length
    }
    return best;
}

// Length of the shortest substring of s that contains at least one 'a', one 'b' and one 'c' (0 if none).
int shortestWithABC(const std::string& s) {
    int count[3] = {0, 0, 0};
    int have = 0;                                    // how many of a, b, c are in the window
    int best = INT_MAX, l = 0;
    for (int r = 0; r < (int)s.size(); ++r) {
        if (s[r] >= 'a' && s[r] <= 'c' && ++count[s[r] - 'a'] == 1) ++have;
        while (have == 3) {                          // valid: record it, then shrink
            best = std::min(best, r - l + 1);
            if (s[l] >= 'a' && s[l] <= 'c' && --count[s[l] - 'a'] == 0) --have;
            ++l;
        }
    }
    return best == INT_MAX ? 0 : best;
}

// Number of subarrays with at most k distinct values.
long long atMostKDistinct(const std::vector<int>& a, int k) {
    std::unordered_map<int, int> freq;
    long long count = 0;
    int l = 0;
    for (int r = 0; r < (int)a.size(); ++r) {
        ++freq[a[r]];
        while ((int)freq.size() > k) {               // too many different values: shrink
            if (--freq[a[l]] == 0) freq.erase(a[l]);
            ++l;
        }
        count += r - l + 1;                          // every subarray that ends at r and starts in [l, r] is valid
    }
    return count;
}

// Number of subarrays with exactly k distinct values.
long long exactlyKDistinct(const std::vector<int>& a, int k) {
    return atMostKDistinct(a, k) - atMostKDistinct(a, k - 1);
}

double findMaxAverage(const std::vector<int>& nums, int k) {
    long long window = 0;
    for (int i = 0; i < k; ++i) window += nums[i];
    long long best = window;
    for (int r = k; r < (int)nums.size(); ++r) {
        window += nums[r];
        window -= nums[r - k];
        best = std::max(best, window);
    }
    return (double)best / k;
}

int lengthOfLongestSubstring(const std::string& s) {
    int count[256] = {0};                            // how many times each character is in the window
    int best = 0, l = 0;
    for (int r = 0; r < (int)s.size(); ++r) {
        ++count[(unsigned char)s[r]];
        while (count[(unsigned char)s[r]] > 1)       // s[r] now appears twice: shrink from the left
            --count[(unsigned char)s[l++]];
        best = std::max(best, r - l + 1);
    }
    return best;
}

int minSubArrayLen(int target, const std::vector<int>& nums) {
    int best = INT_MAX, l = 0;
    long long sum = 0;
    for (int r = 0; r < (int)nums.size(); ++r) {
        sum += nums[r];
        while (sum >= target) {                      // valid: record it, then try to shrink
            best = std::min(best, r - l + 1);
            sum -= nums[l++];
        }
    }
    return best == INT_MAX ? 0 : best;
}

bool checkInclusion(const std::string& s1, const std::string& s2) {
    int k = (int)s1.size();
    if (k == 0 || k > (int)s2.size()) return k == 0;
    int need[26] = {0}, have[26] = {0};
    for (char c : s1) ++need[c - 'a'];
    for (int r = 0; r < (int)s2.size(); ++r) {
        ++have[s2[r] - 'a'];
        if (r >= k) --have[s2[r - k] - 'a'];         // keep the window at exactly k characters
        if (r >= k - 1 && std::equal(need, need + 26, have)) return true;
    }
    return false;
}

int characterReplacement(const std::string& s, int k) {
    int count[26] = {0};
    int best = 0, l = 0;
    auto maxFreq = [&] { return *std::max_element(count, count + 26); };
    for (int r = 0; r < (int)s.size(); ++r) {
        ++count[s[r] - 'A'];
        while ((r - l + 1) - maxFreq() > k)          // more than k characters would need replacing: shrink
            --count[s[l++] - 'A'];
        best = std::max(best, r - l + 1);
    }
    return best;
}

std::string minWindow(const std::string& s, const std::string& t) {
    if (t.empty()) return "";
    int need[256] = {0};
    for (char c : t) ++need[(unsigned char)c];
    int missing = (int)t.size();                     // how many characters of t the window still lacks
    int bestStart = 0, bestLen = INT_MAX, l = 0;
    for (int r = 0; r < (int)s.size(); ++r) {
        if (need[(unsigned char)s[r]]-- > 0) --missing;          // this character was still needed
        while (missing == 0) {                                   // the window covers t: record it, then shrink
            if (r - l + 1 < bestLen) { bestLen = r - l + 1; bestStart = l; }
            if (++need[(unsigned char)s[l++]] > 0) ++missing;    // we dropped a character that was needed
        }
    }
    return bestLen == INT_MAX ? "" : s.substr(bestStart, bestLen);
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

    // fixed windows
    CHECK(maxSumOfK({2, 1, 5, 1, 3, 2}, 3) == 9);
    CHECK(countDistinctWindows({1, 2, 1, 3, 4}, 3) == 2);
    CHECK(std::abs(findMaxAverage({1, 12, -5, -6, 50, 3}, 4) - 12.75) < 1e-9);
    for (int t = 0; t < 300; ++t) {
        int n = rnd(1, 12), k = rnd(1, n);
        auto a = randVec(rng, n, -5, 5);
        long long best = LLONG_MIN;
        int distinct = 0;
        for (int i = 0; i + k <= n; ++i) {
            long long s = 0;
            std::set<int> st;
            for (int j = i; j < i + k; ++j) { s += a[j]; st.insert(a[j]); }
            best = std::max(best, s);
            if ((int)st.size() == k) ++distinct;
        }
        CHECK(maxSumOfK(a, k) == best);
        CHECK(countDistinctWindows(a, k) == distinct);
    }
    // longest sum at most (non-negative)
    CHECK(longestSumAtMost({2, 1, 3, 1, 1, 5}, 5) == 3);
    CHECK(longestSumAtMost({3, 1, 2, 7, 4, 2, 1, 1, 5}, 8) == 4);
    for (int t = 0; t < 400; ++t) {
        auto a = randVec(rng, rnd(0, 12), 0, 6);
        int limit = rnd(0, 15), best = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            int s = 0;
            for (size_t j = i; j < a.size(); ++j) {
                s += a[j];
                if (s <= limit) best = std::max(best, (int)(j - i + 1));
            }
        }
        CHECK(longestSumAtMost(a, limit) == best);
        // shortest with sum at least target (positive numbers), problem 3
        auto b = randVec(rng, rnd(0, 12), 1, 6);
        int target = rnd(1, 20), shortest = INT_MAX;
        for (size_t i = 0; i < b.size(); ++i) {
            int s = 0;
            for (size_t j = i; j < b.size(); ++j) {
                s += b[j];
                if (s >= target) { shortest = std::min(shortest, (int)(j - i + 1)); break; }
            }
        }
        CHECK(minSubArrayLen(target, b) == (shortest == INT_MAX ? 0 : shortest));
    }
    // shortest substring with a, b and c
    CHECK(shortestWithABC("abcbca") == 3);
    CHECK(shortestWithABC("aabb") == 0);
    for (int t = 0; t < 400; ++t) {
        auto s = randStr(rng, rnd(0, 12), "abcd");
        int best = INT_MAX;
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t j = i; j < s.size(); ++j) {
                std::string w = s.substr(i, j - i + 1);
                if (w.find('a') != std::string::npos && w.find('b') != std::string::npos && w.find('c') != std::string::npos)
                    best = std::min(best, (int)w.size());
            }
        CHECK(shortestWithABC(s) == (best == INT_MAX ? 0 : best));
    }
    // at most K / exactly K distinct
    CHECK(atMostKDistinct({1, 2, 1, 2, 3}, 2) == 12);
    CHECK(atMostKDistinct({1, 2, 1, 2, 3}, 1) == 5);
    CHECK(exactlyKDistinct({1, 2, 1, 2, 3}, 2) == 7);
    for (int t = 0; t < 300; ++t) {
        auto a = randVec(rng, rnd(0, 11), 1, 4);
        int k = rnd(1, 4);
        long long atMost = 0, exactly = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            std::set<int> st;
            for (size_t j = i; j < a.size(); ++j) {
                st.insert(a[j]);
                if ((int)st.size() <= k) ++atMost;
                if ((int)st.size() == k) ++exactly;
            }
        }
        CHECK(atMostKDistinct(a, k) == atMost);
        CHECK(exactlyKDistinct(a, k) == exactly);
    }
    // longest substring without repeats
    CHECK(lengthOfLongestSubstring("abcabcbb") == 3 && lengthOfLongestSubstring("bbbbb") == 1 && lengthOfLongestSubstring("pwwkew") == 3 && lengthOfLongestSubstring("") == 0);
    for (int t = 0; t < 300; ++t) {
        auto s = randStr(rng, rnd(0, 12), "abc");
        int best = 0;
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t j = i; j < s.size(); ++j) {
                std::set<char> st(s.begin() + i, s.begin() + j + 1);
                if (st.size() == j - i + 1) best = std::max(best, (int)(j - i + 1));
            }
        CHECK(lengthOfLongestSubstring(s) == best);
    }
    // permutation in string
    CHECK(checkInclusion("ab", "eidbaooo") && !checkInclusion("ab", "eidboaoo") && checkInclusion("", "abc") && !checkInclusion("abc", "ab"));
    for (int t = 0; t < 300; ++t) {
        auto s1 = randStr(rng, rnd(0, 3), "abc"), s2 = randStr(rng, rnd(0, 9), "abc");
        bool brute = false;
        if (s1.empty()) brute = true;
        else {
            std::string sorted1 = s1;
            std::sort(sorted1.begin(), sorted1.end());
            for (size_t i = 0; i + s1.size() <= s2.size(); ++i) {
                std::string w = s2.substr(i, s1.size());
                std::sort(w.begin(), w.end());
                brute = brute || w == sorted1;
            }
        }
        CHECK(checkInclusion(s1, s2) == brute);
    }
    // character replacement
    CHECK(characterReplacement("AABABBA", 1) == 4 && characterReplacement("ABAB", 2) == 4 && characterReplacement("", 3) == 0);
    for (int t = 0; t < 300; ++t) {
        auto s = randStr(rng, rnd(0, 11), "ABC");
        int k = rnd(0, 3), best = 0;
        for (size_t i = 0; i < s.size(); ++i)
            for (size_t j = i; j < s.size(); ++j) {
                int cnt[3] = {0, 0, 0};
                for (size_t x = i; x <= j; ++x) ++cnt[s[x] - 'A'];
                int mx = *std::max_element(cnt, cnt + 3);
                if ((int)(j - i + 1) - mx <= k) best = std::max(best, (int)(j - i + 1));
            }
        CHECK(characterReplacement(s, k) == best);
    }
    // minimum window substring
    CHECK(minWindow("ADOBECODEBANC", "ABC") == "BANC" && minWindow("a", "a") == "a" && minWindow("a", "aa").empty() && minWindow("abc", "").empty());
    for (int t = 0; t < 400; ++t) {
        auto s = randStr(rng, rnd(0, 12), "abc"), tt = randStr(rng, rnd(0, 4), "abc");
        std::string best;
        bool found = false;
        if (!tt.empty())
            for (size_t i = 0; i < s.size(); ++i)
                for (size_t j = i; j < s.size(); ++j) {
                    int need[3] = {0, 0, 0};
                    for (char c : tt) ++need[c - 'a'];
                    for (size_t x = i; x <= j; ++x) --need[s[x] - 'a'];
                    bool ok = need[0] <= 0 && need[1] <= 0 && need[2] <= 0;
                    if (ok && (!found || j - i + 1 < best.size())) { best = s.substr(i, j - i + 1); found = true; }
                }
        auto got = minWindow(s, tt);
        CHECK(got.size() == best.size());                 // several windows can tie; only the length must match
        if (found) {
            int need[3] = {0, 0, 0};
            for (char c : tt) ++need[c - 'a'];
            for (char c : got) --need[c - 'a'];
            CHECK(need[0] <= 0 && need[1] <= 0 && need[2] <= 0 && s.find(got) != std::string::npos);
        }
    }
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
