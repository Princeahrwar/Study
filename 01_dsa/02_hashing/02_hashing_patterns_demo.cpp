// Tutorial: hashing patterns. Every function the tutorial shows is here, followed by brute-force comparisons
// on random inputs and the checks behind every number quoted in the text.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 02_hashing_patterns_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <numeric>
#include <queue>
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

// ---------------------------------------------------------------- from tutorial 01 (hash functions and the generator)
uint64_t mix64(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

size_t hashCombine(size_t seed, size_t value) {
    return seed ^ (mix64(value) + 0x9E3779B97F4A7C15ULL + (seed << 6) + (seed >> 2));
}

struct PairHash {
    size_t operator()(const std::pair<int, int>& p) const noexcept {
        return hashCombine(mix64((uint64_t)p.first), (size_t)p.second);
    }
};

// A small deterministic random number generator (splitmix64), so every run and every compiler sees the same numbers.
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t next() { return mix64(state += 0x9E3779B97F4A7C15ULL); }
    uint64_t below(uint64_t n) { return next() % n; }
};

// ---------------------------------------------------------------- section 2: seen-sets
// LeetCode 217: is any value present twice? insert() reports whether the value was new.
bool containsDuplicate(const std::vector<int>& nums) {
    std::unordered_set<int> seen;
    seen.reserve(nums.size());
    for (int x : nums)
        if (!seen.insert(x).second) return true;
    return false;
}

// LeetCode 219: are there equal values at two different indices at most k apart? Remember the latest index of each value.
bool containsNearbyDuplicate(const std::vector<int>& nums, int k) {
    std::unordered_map<int, int> lastIndex;
    for (int i = 0; i < (int)nums.size(); ++i) {
        auto it = lastIndex.find(nums[i]);
        if (it != lastIndex.end() && i - it->second <= k) return true;
        lastIndex[nums[i]] = i;                               // always overwrite: the latest position is the closest
    }
    return false;
}

// The same question with a set that holds exactly the last k values.
bool containsNearbyDuplicateWindow(const std::vector<int>& nums, int k) {
    std::unordered_set<int> window;
    for (int i = 0; i < (int)nums.size(); ++i) {
        if (i > k) window.erase(nums[i - k - 1]);             // the value k + 1 positions back is out of reach now
        if (!window.insert(nums[i]).second) return true;
    }
    return false;
}

int digitSquareSum(int n) {
    int sum = 0;
    for (; n > 0; n /= 10) sum += (n % 10) * (n % 10);
    return sum;
}

// LeetCode 202: replace n by the sum of the squares of its digits, again and again. Does it reach 1, or loop forever?
bool isHappy(int n) {
    std::unordered_set<int> seen;
    while (n != 1 && seen.insert(n).second) n = digitSquareSum(n);   // stop at 1, or when a value comes back
    return n == 1;
}

// The same with O(1) memory: a slow and a fast walker on the sequence (Floyd's cycle detection).
bool isHappyFloyd(int n) {
    int slow = n, fast = digitSquareSum(n);
    while (fast != 1 && slow != fast) {
        slow = digitSquareSum(slow);
        fast = digitSquareSum(digitSquareSum(fast));
    }
    return fast == 1;
}

// ---------------------------------------------------------------- section 3: counting
// Number of Good Pairs (LeetCode 1512): index pairs i < j with equal values.
long long countEqualPairs(const std::vector<int>& nums) {
    std::unordered_map<int, int> seen;
    long long pairs = 0;
    for (int x : nums) pairs += seen[x]++;                    // x pairs with every earlier copy of itself
    return pairs;
}

// LeetCode 347 with bucket sort: count, then file every value under its count. O(n).
std::vector<int> topKFrequent(const std::vector<int>& nums, int k) {
    std::unordered_map<int, int> count;
    for (int x : nums) ++count[x];
    std::vector<std::vector<int>> byCount(nums.size() + 1);   // byCount[c] = the values that occur exactly c times
    for (const auto& [value, c] : count) byCount[c].push_back(value);
    std::vector<int> result;
    for (int c = (int)nums.size(); c >= 1 && (int)result.size() < k; --c)
        for (int value : byCount[c])
            if ((int)result.size() < k) result.push_back(value);
    return result;
}

// LeetCode 347 with a min-heap of size k. O(n log k), and it also works on a stream of (value, count) pairs.
std::vector<int> topKFrequentHeap(const std::vector<int>& nums, int k) {
    std::unordered_map<int, int> count;
    for (int x : nums) ++count[x];
    using Entry = std::pair<int, int>;                        // (count, value)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
    for (const auto& [value, c] : count) {
        heap.push({c, value});
        if ((int)heap.size() > k) heap.pop();                 // drop the least frequent entry
    }
    std::vector<int> result;
    while (!heap.empty()) {
        result.push_back(heap.top().second);
        heap.pop();
    }
    return result;                                            // least frequent first
}

// ---------------------------------------------------------------- section 4: complement lookup
// LeetCode 1: indices i < j with nums[i] + nums[j] == target, or an empty vector.
std::vector<int> twoSum(const std::vector<int>& nums, int target) {
    std::unordered_map<int, int> index;                       // value -> index where it was seen
    for (int j = 0; j < (int)nums.size(); ++j) {
        auto it = index.find(target - nums[j]);               // ask first: the partner must come from an earlier index
        if (it != index.end()) return {it->second, j};
        index[nums[j]] = j;                                   // store afterwards: an element cannot pair with itself
    }
    return {};
}

// How many index pairs i < j have nums[i] + nums[j] == target?
long long countPairsWithSum(const std::vector<int>& nums, int target) {
    std::unordered_map<int, int> seen;
    long long pairs = 0;
    for (int x : nums) {
        auto it = seen.find(target - x);
        if (it != seen.end()) pairs += it->second;            // x pairs with every earlier copy of its partner
        ++seen[x];
    }
    return pairs;
}

// LeetCode 454: how many (a, b, c, d), one from each array, add up to 0? Meet in the middle: O(n^2) instead of O(n^4).
int fourSumCount(const std::vector<int>& A, const std::vector<int>& B, const std::vector<int>& C, const std::vector<int>& D) {
    std::unordered_map<int, int> pairSums;                    // a + b -> number of (a, b) pairs with that sum
    for (int a : A)
        for (int b : B) ++pairSums[a + b];
    int count = 0;
    for (int c : C)
        for (int d : D) {
            auto it = pairSums.find(-(c + d));
            if (it != pairSums.end()) count += it->second;
        }
    return count;
}

// ---------------------------------------------------------------- section 5: canonical forms and one-to-one mappings
// LeetCode 205: can the letters of s be replaced one-to-one to give t? Keep the mapping in both directions.
bool isIsomorphic(const std::string& s, const std::string& t) {
    if (s.size() != t.size()) return false;
    int forward[256], backward[256];                          // -1 = not mapped yet
    std::fill(forward, forward + 256, -1);
    std::fill(backward, backward + 256, -1);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char a = s[i], b = t[i];
        if (forward[a] == -1) forward[a] = b;                 // a is new: it must become b from now on
        else if (forward[a] != b) return false;               // a was already sent somewhere else
        if (backward[b] == -1) backward[b] = a;               // b is new: only a may become it
        else if (backward[b] != a) return false;              // b was already the image of another letter
    }
    return true;
}

// The shape of a string: every character is replaced by the index of its first occurrence ("paper" -> 0 1 0 3 4).
std::vector<int> shapeOf(const std::string& s) {
    int first[256];
    std::fill(first, first + 256, -1);
    std::vector<int> shape;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];
        if (first[c] == -1) first[c] = (int)i;
        shape.push_back(first[c]);
    }
    return shape;
}

// Two strings are isomorphic exactly when their shapes are equal.
bool isIsomorphicByShape(const std::string& s, const std::string& t) {
    return shapeOf(s) == shapeOf(t);
}

// LeetCode 290: does the sequence of words follow the pattern, letter for word, one-to-one?
// The same first-occurrence idea, with one map per side.
bool wordPattern(const std::string& pattern, const std::string& s) {
    std::vector<std::string> words;
    for (size_t i = 0; i < s.size();) {                       // words are separated by single spaces
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        words.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    if (words.size() != pattern.size()) return false;
    std::unordered_map<char, int> letterFirst;                // letter -> index of its first appearance
    std::unordered_map<std::string, int> wordFirst;           // word -> index of its first appearance
    for (int i = 0; i < (int)words.size(); ++i) {
        int a = letterFirst.emplace(pattern[i], i).first->second;     // emplace keeps the old value if the key exists
        int b = wordFirst.emplace(words[i], i).first->second;
        if (a != b) return false;
    }
    return true;
}

// LeetCode 36: no digit twice in a row, a column or a 3 x 3 box. Nine digits fit in a 9-bit mask per group.
bool isValidSudoku(const std::vector<std::string>& board) {
    int rows[9] = {}, cols[9] = {}, boxes[9] = {};            // bit d set: digit d + 1 is already used in this group
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) {
            if (board[r][c] == '.') continue;
            int bit = 1 << (board[r][c] - '1');
            int box = (r / 3) * 3 + c / 3;
            if ((rows[r] | cols[c] | boxes[box]) & bit) return false;
            rows[r] |= bit;
            cols[c] |= bit;
            boxes[box] |= bit;
        }
    return true;
}

// The same check with one hash set of encoded (group kind, group number, digit) triples.
bool isValidSudokuSet(const std::vector<std::string>& board) {
    std::unordered_set<int> seen;
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) {
            if (board[r][c] == '.') continue;
            int digit = board[r][c] - '0';
            int box = (r / 3) * 3 + c / 3;
            if (!seen.insert(0 * 1000 + r * 10 + digit).second) return false;      // kind 0: rows
            if (!seen.insert(1 * 1000 + c * 10 + digit).second) return false;      // kind 1: columns
            if (!seen.insert(2 * 1000 + box * 10 + digit).second) return false;    // kind 2: boxes
        }
    return true;
}

// ---------------------------------------------------------------- section 6: tuples and sequences as keys
// A hash for a whole vector: fold the elements with hashCombine (tutorial 01).
struct VectorHash {
    size_t operator()(const std::vector<int>& v) const noexcept {
        size_t h = v.size();
        for (int x : v) h = hashCombine(h, (size_t)x);
        return h;
    }
};

// LeetCode 2352: how many pairs (row r, column c) have exactly the same contents?
int equalPairs(const std::vector<std::vector<int>>& grid) {
    std::unordered_map<std::vector<int>, int, VectorHash> rowCount;
    for (const auto& row : grid) ++rowCount[row];
    int n = (int)grid.size(), pairs = 0;
    for (int c = 0; c < n; ++c) {
        std::vector<int> column(n);
        for (int r = 0; r < n; ++r) column[r] = grid[r][c];
        auto it = rowCount.find(column);
        if (it != rowCount.end()) pairs += it->second;
    }
    return pairs;
}

// LeetCode 149: the most points on one line (all points are distinct). From each point, count the other points
// by direction; a direction is the pair (dx, dy) divided by its gcd, so it is exact (no floating-point slopes).
int maxPointsOnLine(const std::vector<std::pair<int, int>>& points) {
    int n = (int)points.size(), best = std::min(n, 2);
    for (int i = 0; i < n; ++i) {
        std::unordered_map<std::pair<int, int>, int, PairHash> direction;
        for (int j = i + 1; j < n; ++j) {
            int dx = points[j].first - points[i].first, dy = points[j].second - points[i].second;
            int g = std::gcd(dx, dy);                         // never 0: the points are distinct
            dx /= g;
            dy /= g;
            if (dx < 0 || (dx == 0 && dy < 0)) { dx = -dx; dy = -dy; }   // a line has two directions: keep one
            best = std::max(best, ++direction[{dx, dy}] + 1); // + 1 for point i itself
        }
    }
    return best;
}

// ---------------------------------------------------------------- section 7: longest consecutive sequence
// LeetCode 128: the longest run of consecutive integers in an unsorted array, in O(n) expected time.
int longestConsecutive(const std::vector<int>& nums) {
    std::unordered_set<int> present(nums.begin(), nums.end());
    int best = 0;
    for (int x : present) {
        if (present.count(x - 1)) continue;                   // x is inside a run, not its start
        int length = 1;
        while (present.count(x + length)) ++length;
        best = std::max(best, length);
    }
    return best;
}

// ---------------------------------------------------------------- section 8: a duplicate filter for packet ids
// accept(id) is false if id is among the last `window` accepted ids. Otherwise it accepts id and forgets the oldest
// id when the window is full. A set answers "is it in the window?", a queue remembers the order.
class DuplicateFilter {
    size_t window_;
    std::unordered_set<uint32_t> recent_;
    std::deque<uint32_t> order_;
public:
    explicit DuplicateFilter(size_t window) : window_(window) {}
    bool accept(uint32_t id) {
        if (!recent_.insert(id).second) return false;         // already in the window
        order_.push_back(id);
        if (order_.size() > window_) {                        // forget the oldest
            recent_.erase(order_.front());
            order_.pop_front();
        }
        return true;
    }
};

// ================================================================ tests
static std::vector<int> randomArray(Rng& rng, int maxLength, int lo, int hi) {
    int length = (int)rng.below(maxLength + 1);
    std::vector<int> a(length);
    for (int i = 0; i < length; ++i) a[i] = lo + (int)rng.below(hi - lo + 1);
    return a;
}

static std::map<int, int> countsOf(const std::vector<int>& a) {
    std::map<int, int> counts;
    for (int x : a) ++counts[x];
    return counts;
}

// A copy of longestConsecutive that also counts its hash-set lookups, to check the O(n) argument.
static int longestConsecutiveLookups(const std::vector<int>& nums, long long& lookups) {
    std::unordered_set<int> present(nums.begin(), nums.end());
    int best = 0;
    for (int x : present) {
        ++lookups;
        if (present.count(x - 1)) continue;
        int length = 1;
        for (;;) {
            ++lookups;
            if (!present.count(x + length)) break;
            ++length;
        }
        best = std::max(best, length);
    }
    return best;
}

// The tempting wrong version of maxPointsOnLine: slopes as doubles.
static int maxPointsOnLineDoubleSlope(const std::vector<std::pair<int, int>>& points) {
    int n = (int)points.size(), best = std::min(n, 2);
    for (int i = 0; i < n; ++i) {
        std::map<double, int> slopes;
        for (int j = i + 1; j < n; ++j) {
            double dx = points[j].first - points[i].first, dy = points[j].second - points[i].second;
            double slope = dx == 0 ? 1e18 : dy / dx;
            best = std::max(best, ++slopes[slope] + 1);
        }
    }
    return best;
}

static bool sudokuBrute(const std::vector<std::string>& b) {
    for (int r1 = 0; r1 < 9; ++r1)
        for (int c1 = 0; c1 < 9; ++c1)
            for (int r2 = 0; r2 < 9; ++r2)
                for (int c2 = 0; c2 < 9; ++c2) {
                    if (r1 == r2 && c1 == c2) continue;
                    if (b[r1][c1] == '.' || b[r1][c1] != b[r2][c2]) continue;
                    if (r1 == r2 || c1 == c2 || (r1 / 3 == r2 / 3 && c1 / 3 == c2 / 3)) return false;
                }
    return true;
}

int main() {
    // ---- seen-sets
    CHECK(containsDuplicate({1, 2, 3, 1}) && !containsDuplicate({1, 2, 3, 4}) && !containsDuplicate({}));
    {
        Rng rng(1);
        for (int iter = 0; iter < 5000; ++iter) {
            std::vector<int> a = randomArray(rng, 12, 0, 9);
            bool brute = false;
            for (size_t i = 0; i < a.size(); ++i)
                for (size_t j = i + 1; j < a.size(); ++j) brute = brute || a[i] == a[j];
            CHECK(containsDuplicate(a) == brute);
        }
    }
    CHECK(containsNearbyDuplicate({1, 2, 3, 1}, 3) && containsNearbyDuplicate({1, 0, 1, 1}, 1) &&
          !containsNearbyDuplicate({1, 2, 3, 1, 2, 3}, 2));
    CHECK(containsNearbyDuplicateWindow({1, 2, 3, 1}, 3) && containsNearbyDuplicateWindow({1, 0, 1, 1}, 1) &&
          !containsNearbyDuplicateWindow({1, 2, 3, 1, 2, 3}, 2));
    {
        Rng rng(2);
        for (int iter = 0; iter < 5000; ++iter) {
            std::vector<int> a = randomArray(rng, 12, 0, 5);
            int k = (int)rng.below(7);
            bool brute = false;
            for (int i = 0; i < (int)a.size(); ++i)
                for (int j = i + 1; j < (int)a.size(); ++j) brute = brute || (a[i] == a[j] && j - i <= k);
            CHECK(containsNearbyDuplicate(a, k) == brute);
            CHECK(containsNearbyDuplicateWindow(a, k) == brute);
        }
    }
    {
        // Happy numbers
        CHECK(digitSquareSum(19) == 82 && digitSquareSum(82) == 68 && digitSquareSum(68) == 100 && digitSquareSum(100) == 1);
        auto happyReference = [](int n) {
            for (int step = 0; step < 1000; ++step) {
                if (n == 1) return true;
                n = digitSquareSum(n);
            }
            return false;
        };
        std::vector<int> happyUpTo100;
        for (int n = 1; n <= 100; ++n)
            if (happyReference(n)) happyUpTo100.push_back(n);
        CHECK((happyUpTo100 == std::vector<int>{1, 7, 10, 13, 19, 23, 28, 31, 32, 44, 49, 68, 70, 79, 82, 86, 91, 94, 97, 100}));
        int longestWalk = 0, longestStart = 0;
        for (int n = 1; n <= 5000; ++n) {
            CHECK(isHappy(n) == happyReference(n));
            CHECK(isHappyFloyd(n) == happyReference(n));
            // Every unhappy number falls into the cycle 4 -> 16 -> 37 -> 58 -> 89 -> 145 -> 42 -> 20 -> 4.
            bool reachesFour = false;
            int m = n, steps = 0;
            std::unordered_set<int> seen;
            while (m != 1 && seen.insert(m).second) {
                reachesFour = reachesFour || m == 4;
                m = digitSquareSum(m);
                ++steps;
            }
            CHECK(happyReference(n) || reachesFour);
            if (steps > longestWalk) { longestWalk = steps; longestStart = n; }
        }
        std::printf("happy numbers up to 5000: longest walk before 1 or a repeat is %d steps (start %d)\n", longestWalk, longestStart);
        int cycle[8] = {4, 16, 37, 58, 89, 145, 42, 20};
        for (int i = 0; i < 8; ++i) CHECK(digitSquareSum(cycle[i]) == cycle[(i + 1) % 8]);
        CHECK(digitSquareSum(1999999999) == 730 && digitSquareSum(999999999) == 729);   // largest first step for any int
        int maxBelow1000 = 0;
        for (int n = 1; n < 1000; ++n) maxBelow1000 = std::max(maxBelow1000, digitSquareSum(n));
        CHECK(maxBelow1000 == 243);
        CHECK(isHappy(1) && isHappy(7) && !isHappy(2) && isHappy(2147483646) == happyReference(2147483646));
    }

    // ---- counting
    CHECK(countEqualPairs({1, 2, 3, 1, 1, 3}) == 4 && countEqualPairs({1, 1, 1, 1}) == 6 && countEqualPairs({1, 2, 3}) == 0);
    {
        Rng rng(3);
        for (int iter = 0; iter < 3000; ++iter) {
            std::vector<int> a = randomArray(rng, 14, 0, 4);
            long long brute = 0;
            for (size_t i = 0; i < a.size(); ++i)
                for (size_t j = i + 1; j < a.size(); ++j) brute += a[i] == a[j];
            long long fromCounts = 0;
            for (const auto& [value, c] : countsOf(a)) fromCounts += (long long)c * (c - 1) / 2;
            CHECK(countEqualPairs(a) == brute && brute == fromCounts);
        }
        // top K: compare the multiset of frequencies of the chosen values with the k largest frequencies
        for (int iter = 0; iter < 3000; ++iter) {
            std::vector<int> a = randomArray(rng, 30, 0, 8);
            if (a.empty()) a.push_back(0);
            std::map<int, int> counts = countsOf(a);
            int k = 1 + (int)rng.below(counts.size());
            std::vector<int> frequencies;
            for (const auto& [value, c] : counts) frequencies.push_back(c);
            std::sort(frequencies.rbegin(), frequencies.rend());
            frequencies.resize(k);
            for (int variant = 0; variant < 2; ++variant) {
                std::vector<int> chosen = variant == 0 ? topKFrequent(a, k) : topKFrequentHeap(a, k);
                std::vector<int> chosenFrequencies;
                std::set<int> distinct(chosen.begin(), chosen.end());
                for (int v : chosen) chosenFrequencies.push_back(counts.count(v) ? counts[v] : -1);
                std::sort(chosenFrequencies.rbegin(), chosenFrequencies.rend());
                CHECK((int)chosen.size() == k && (int)distinct.size() == k && chosenFrequencies == frequencies);
            }
        }
        auto sorted = [](std::vector<int> v) { std::sort(v.begin(), v.end()); return v; };
        CHECK(sorted(topKFrequent({1, 1, 1, 2, 2, 3}, 2)) == (std::vector<int>{1, 2}));
        CHECK(sorted(topKFrequentHeap({1, 1, 1, 2, 2, 3}, 2)) == (std::vector<int>{1, 2}));
        CHECK(topKFrequent({4, 1, 1, 1, 2, 2, 3, 3, 3, 3}, 2) == (std::vector<int>{3, 1}));
        CHECK(topKFrequentHeap({4, 1, 1, 1, 2, 2, 3, 3, 3, 3}, 2) == (std::vector<int>{1, 3}));
        CHECK(topKFrequent({1}, 1) == (std::vector<int>{1}));
    }

    // ---- complement lookup
    CHECK(twoSum({2, 7, 11, 15}, 9) == (std::vector<int>{0, 1}) && twoSum({3, 2, 4}, 6) == (std::vector<int>{1, 2}));
    CHECK(twoSum({3, 3}, 6) == (std::vector<int>{0, 1}) && twoSum({3}, 6).empty() && twoSum({}, 0).empty());
    CHECK(countPairsWithSum({1, 5, 3, 3, 3}, 6) == 4);
    {
        Rng rng(4);
        for (int iter = 0; iter < 5000; ++iter) {
            std::vector<int> a = randomArray(rng, 10, -5, 5);
            int target = -6 + (int)rng.below(13);
            long long brute = 0;
            for (size_t i = 0; i < a.size(); ++i)
                for (size_t j = i + 1; j < a.size(); ++j) brute += a[i] + a[j] == target;
            std::vector<int> got = twoSum(a, target);
            bool valid = got.size() == 2 && got[0] < got[1] && got[1] < (int)a.size() && a[got[0]] + a[got[1]] == target;
            CHECK(got.empty() ? brute == 0 : valid);
            CHECK(countPairsWithSum(a, target) == brute);
        }
        for (int iter = 0; iter < 1500; ++iter) {
            std::vector<int> A = randomArray(rng, 5, -2, 2), B = randomArray(rng, 5, -2, 2);
            std::vector<int> C = randomArray(rng, 5, -2, 2), D = randomArray(rng, 5, -2, 2);
            int brute = 0;
            for (int a : A)
                for (int b : B)
                    for (int c : C)
                        for (int d : D) brute += a + b + c + d == 0;
            CHECK(fourSumCount(A, B, C, D) == brute);
        }
        CHECK(fourSumCount({1, 2}, {-2, -1}, {-1, 2}, {0, 2}) == 2 && fourSumCount({0}, {0}, {0}, {0}) == 1);
    }

    // ---- canonical forms and one-to-one mappings
    CHECK(isIsomorphic("egg", "add") && !isIsomorphic("foo", "bar") && isIsomorphic("paper", "title") &&
          !isIsomorphic("badc", "baba") && !isIsomorphic("ab", "a") && isIsomorphic("", ""));
    CHECK((shapeOf("paper") == std::vector<int>{0, 1, 0, 3, 4}) && (shapeOf("title") == std::vector<int>{0, 1, 0, 3, 4}));
    CHECK((shapeOf("badc") == std::vector<int>{0, 1, 2, 3}) && (shapeOf("baba") == std::vector<int>{0, 1, 0, 1}));
    {
        Rng rng(5);
        int isomorphicCount = 0;
        for (int iter = 0; iter < 6000; ++iter) {
            int length = (int)rng.below(8);
            std::string s(length, 'a');
            for (char& ch : s) ch = (char)('a' + rng.below(4));
            std::string t(length, 'a');
            int mode = (int)rng.below(3);
            char perm[4] = {'p', 'q', 'r', 's'};
            for (int i = 3; i > 0; --i) std::swap(perm[i], perm[rng.below(i + 1)]);
            for (int i = 0; i < length; ++i) {
                char random = (char)('p' + rng.below(4));
                t[i] = mode == 0 ? perm[s[i] - 'a'] : random;
            }
            if (mode == 2 && length > 0 && rng.below(2) == 0) t.pop_back();      // sometimes a different length
            bool brute = s.size() == t.size();
            std::map<char, char> forward, backward;
            for (size_t i = 0; brute && i < s.size(); ++i) {
                if (forward.count(s[i]) && forward[s[i]] != t[i]) brute = false;
                if (backward.count(t[i]) && backward[t[i]] != s[i]) brute = false;
                forward[s[i]] = t[i];
                backward[t[i]] = s[i];
            }
            isomorphicCount += brute;
            CHECK(isIsomorphic(s, t) == brute);
            CHECK(isIsomorphicByShape(s, t) == brute);
        }
        CHECK(isomorphicCount > 500 && isomorphicCount < 5500);
        // word pattern
        CHECK(wordPattern("abba", "dog cat cat dog") && !wordPattern("abba", "dog cat cat fish") &&
              !wordPattern("aaaa", "dog cat cat dog") && !wordPattern("abba", "dog dog dog dog") && wordPattern("", ""));
        const char* vocabulary[4] = {"x", "y", "z", "xx"};
        int patternMatches = 0;
        for (int iter = 0; iter < 4000; ++iter) {
            int length = (int)rng.below(6);
            std::string pattern(length, 'a');
            for (char& ch : pattern) ch = (char)('a' + rng.below(3));
            int wordCount = length + (rng.below(6) == 0 ? 1 : 0);
            std::vector<std::string> words;
            for (int i = 0; i < wordCount; ++i) words.push_back(vocabulary[rng.below(4)]);
            std::string s;
            for (int i = 0; i < wordCount; ++i) s += (i ? " " : "") + words[i];
            bool brute = (int)words.size() == length;
            std::map<char, std::string> letterToWord;
            std::map<std::string, char> wordToLetter;
            for (int i = 0; brute && i < length; ++i) {
                if (letterToWord.count(pattern[i]) && letterToWord[pattern[i]] != words[i]) brute = false;
                if (wordToLetter.count(words[i]) && wordToLetter[words[i]] != pattern[i]) brute = false;
                letterToWord[pattern[i]] = words[i];
                wordToLetter[words[i]] = pattern[i];
            }
            patternMatches += brute;
            CHECK(wordPattern(pattern, s) == brute);
        }
        CHECK(patternMatches > 100 && patternMatches < 3900);
        // Sudoku
        std::vector<std::string> valid = {"53..7....", "6..195...", ".98....6.", "8...6...3", "4..8.3..1",
                                          "7...2...6", ".6....28.", "...419..5", "....8..79"};
        std::vector<std::string> invalid = valid;
        invalid[0][0] = '8';
        CHECK(isValidSudoku(valid) && isValidSudokuSet(valid) && sudokuBrute(valid));
        CHECK(!isValidSudoku(invalid) && !isValidSudokuSet(invalid) && !sudokuBrute(invalid));
        std::vector<std::string> base(9, std::string(9, '.'));
        for (int r = 0; r < 9; ++r)
            for (int c = 0; c < 9; ++c) base[r][c] = (char)('1' + ((r % 3) * 3 + r / 3 + c) % 9);
        CHECK(sudokuBrute(base) && isValidSudoku(base) && isValidSudokuSet(base));
        int validBoards = 0;
        for (int iter = 0; iter < 3000; ++iter) {
            std::vector<std::string> board = base;
            for (int r = 0; r < 9; ++r)
                for (int c = 0; c < 9; ++c)
                    if (rng.below(2) == 0) board[r][c] = '.';
            int changes = (int)rng.below(3);
            for (int i = 0; i < changes; ++i) {
                int r = (int)rng.below(9), c = (int)rng.below(9);
                board[r][c] = (char)('1' + rng.below(9));
            }
            bool brute = sudokuBrute(board);
            validBoards += brute;
            CHECK(isValidSudoku(board) == brute);
            CHECK(isValidSudokuSet(board) == brute);
        }
        CHECK(validBoards > 300 && validBoards < 2900);
        // the box number formula
        CHECK((0 / 3) * 3 + 0 / 3 == 0 && (4 / 3) * 3 + 7 / 3 == 5 && (8 / 3) * 3 + 8 / 3 == 8);
    }

    // ---- tuples and sequences as keys
    CHECK(equalPairs({{3, 2, 1}, {1, 7, 6}, {2, 7, 7}}) == 1);
    CHECK(equalPairs({{3, 1, 2, 2}, {1, 4, 4, 5}, {2, 4, 2, 2}, {2, 4, 2, 2}}) == 3);
    {
        Rng rng(6);
        for (int iter = 0; iter < 3000; ++iter) {
            int n = 1 + (int)rng.below(5);
            std::vector<std::vector<int>> grid(n, std::vector<int>(n));
            for (auto& row : grid)
                for (int& x : row) x = (int)rng.below(3);
            int brute = 0;
            for (int r = 0; r < n; ++r)
                for (int c = 0; c < n; ++c) {
                    bool same = true;
                    for (int i = 0; i < n; ++i) same = same && grid[r][i] == grid[i][c];
                    brute += same;
                }
            CHECK(equalPairs(grid) == brute);
        }
        // hashing the 27 vectors of length 3 over {0, 1, 2}: the sum of the elements is a poor hash
        std::set<size_t> hashes;
        std::set<int> sums;
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b)
                for (int c = 0; c < 3; ++c) {
                    std::vector<int> v = {a, b, c};
                    hashes.insert(VectorHash()(v));
                    sums.insert(a + b + c);
                    CHECK(VectorHash()(v) == VectorHash()(std::vector<int>{a, b, c}));
                }
        std::printf("27 vectors over {0,1,2}: VectorHash gives %zu distinct hashes, the sum of the elements gives %zu\n", hashes.size(), sums.size());
        CHECK(hashes.size() == 27 && sums.size() == 7);
        // Points on a line
        CHECK(maxPointsOnLine({{1, 1}, {2, 2}, {3, 3}}) == 3);
        CHECK(maxPointsOnLine({{1, 1}, {3, 2}, {5, 3}, {4, 1}, {2, 3}, {1, 4}}) == 4);
        CHECK(maxPointsOnLine({{0, 0}}) == 1 && maxPointsOnLine({{0, 0}, {5, 7}}) == 2 && maxPointsOnLine({}) == 0);
        for (int iter = 0; iter < 4000; ++iter) {
            int range = iter < 3000 ? 5 : 7;                       // coordinates 0..4, or -3..3
            int lowest = iter < 3000 ? 0 : -3;
            int n = 1 + (int)rng.below(9);
            std::set<std::pair<int, int>> chosen;
            while ((int)chosen.size() < n) {
                int x = lowest + (int)rng.below(range);
                int y = lowest + (int)rng.below(range);
                chosen.insert({x, y});
            }
            std::vector<std::pair<int, int>> points(chosen.begin(), chosen.end());
            for (int i = n - 1; i > 0; --i) std::swap(points[i], points[rng.below(i + 1)]);   // the set is sorted; the order matters for the direction rule
            int brute = std::min(n, 2);
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j) {
                    int onLine = 0;
                    for (int k = 0; k < n; ++k) {
                        long long cross = (long long)(points[j].first - points[i].first) * (points[k].second - points[i].second) -
                                          (long long)(points[j].second - points[i].second) * (points[k].first - points[i].first);
                        onLine += cross == 0;
                    }
                    brute = std::max(brute, onLine);
                }
            CHECK(maxPointsOnLine(points) == brute);
        }
        // Floating-point slopes merge two different lines.
        std::vector<std::pair<int, int>> trap = {{0, 0}, {94911151, 94911150}, {94911152, 94911151}};
        std::printf("three points that are not on one line: exact answer %d, double-slope answer %d\n",
                    maxPointsOnLine(trap), maxPointsOnLineDoubleSlope(trap));
        CHECK(maxPointsOnLine(trap) == 2 && maxPointsOnLineDoubleSlope(trap) == 3);
    }

    // ---- longest consecutive sequence
    CHECK(longestConsecutive({100, 4, 200, 1, 3, 2}) == 4 && longestConsecutive({0, 3, 7, 2, 5, 8, 4, 6, 0, 1}) == 9);
    CHECK(longestConsecutive({}) == 0 && longestConsecutive({5}) == 1 && longestConsecutive({1, 1, 1}) == 1);
    {
        Rng rng(7);
        for (int iter = 0; iter < 5000; ++iter) {
            std::vector<int> a = randomArray(rng, 14, -6, 6);
            std::vector<int> sorted = a;
            std::sort(sorted.begin(), sorted.end());
            sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
            int brute = 0, run = 0;
            for (size_t i = 0; i < sorted.size(); ++i) {
                run = (i > 0 && sorted[i] == sorted[i - 1] + 1) ? run + 1 : 1;
                brute = std::max(brute, run);
            }
            long long lookups = 0;
            CHECK(longestConsecutive(a) == brute);
            CHECK(longestConsecutiveLookups(a, lookups) == brute && lookups == 2 * (long long)sorted.size());
        }
        // 50000 consecutive values: linear work takes milliseconds, the version without the skip rule would need
        // about 1.25 billion lookups. The limit is 100 times the usual time, so the check is not flaky.
        std::vector<int> run(50000);
        std::iota(run.begin(), run.end(), -7);
        for (size_t i = run.size() - 1; i > 0; --i) std::swap(run[i], run[rng.below(i + 1)]);
        auto start = std::chrono::steady_clock::now();
        int longest = longestConsecutive(run);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(longest == 50000 && seconds < 3.0);
        std::printf("longestConsecutive on 50000 shuffled consecutive values: %.4f s\n", seconds);
    }

    // ---- duplicate filter
    {
        DuplicateFilter f(2);
        CHECK(f.accept(1) && f.accept(2) && !f.accept(1) && f.accept(3) && f.accept(1) && !f.accept(1));
        Rng rng(8);
        for (int run = 0; run < 400; ++run) {
            size_t window = 1 + rng.below(8);
            DuplicateFilter filter(window);
            std::vector<uint32_t> last;                      // reference: the last `window` accepted ids, scanned
            for (int step = 0; step < 200; ++step) {
                uint32_t id = (uint32_t)rng.below(14);
                bool expected = std::find(last.begin(), last.end(), id) == last.end();
                if (expected) {
                    last.push_back(id);
                    if (last.size() > window) last.erase(last.begin());
                }
                CHECK(filter.accept(id) == expected);
            }
        }
    }
    CHECK(!containsNearbyDuplicate({1, 1}, 0) && !containsNearbyDuplicateWindow({1, 1}, 0));
    {
        long long n = 65536, m = 65537;
        CHECK(n * (n - 1) / 2 <= 2147483647LL && m * (m - 1) / 2 > 2147483647LL);   // pairs of equal items overflow int at 65537
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
