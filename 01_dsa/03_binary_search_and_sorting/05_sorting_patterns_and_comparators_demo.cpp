// Tutorial: sorting patterns and comparators. The functions shown in the tutorial, checked against brute force on every
// small input, plus the few counts the text quotes.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 05_sorting_patterns_and_comparators_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

using Vec = std::vector<int>;

// ---------------------------------------------------------------- 1. Comparators

// Does `less` behave as a strict weak ordering on these sample values? Tries every pair and every triple (for tests only:
// O(n^3)). The four rules: never less than itself; never both a < b and b < a; a < b < c gives a < c; "neither is less"
// (equivalent) is transitive.
template <class T, class Less>
bool isStrictWeakOrder(const std::vector<T>& sample, Less less) {
    auto equiv = [&](const T& a, const T& b) { return !less(a, b) && !less(b, a); };
    for (const T& a : sample) {
        if (less(a, a)) return false;
        for (const T& b : sample) {
            if (less(a, b) && less(b, a)) return false;
            for (const T& c : sample) {
                if (less(a, b) && less(b, c) && !less(a, c)) return false;
                if (equiv(a, b) && equiv(b, c) && !equiv(a, c)) return false;
            }
        }
    }
    return true;
}

struct Student {
    std::string name;
    int grade;
};

// Highest grade first; equal grades by name. Compare the first key, and only when it ties the next one.
bool byGradeThenName(const Student& a, const Student& b) {
    if (a.grade != b.grade) return a.grade > b.grade;
    return a.name < b.name;
}

// The same order with std::tie: lists compare key by key. Descending order = swap the two sides of that key.
bool byGradeThenNameTie(const Student& a, const Student& b) {
    return std::tie(b.grade, a.name) < std::tie(a.grade, b.name);
}

// The indexes of v in sorted order (v itself is not changed). Stable: equal values keep the smaller index first.
std::vector<int> argsort(const std::vector<int>& v) {
    std::vector<int> idx(v.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int i, int j) { return v[i] < v[j]; });
    return idx;
}

// ---------------------------------------------------------------- 2. Orders from a rule

// LeetCode 179: arrange the numbers so that their concatenation is the largest. a goes before b when "ab" is larger than "ba".
std::string largestNumber(const std::vector<int>& nums) {
    std::vector<std::string> s;
    for (int x : nums) s.push_back(std::to_string(x));
    std::sort(s.begin(), s.end(), [](const std::string& a, const std::string& b) { return a + b > b + a; });
    if (s[0] == "0") return "0";                                // the biggest first piece is 0: all pieces are 0
    std::string result;
    for (const std::string& x : s) result += x;
    return result;
}

// LeetCode 791: the letters of s in the order given by `order`; letters that `order` lacks go last (rank 26), in their old order.
std::string customSortString(const std::string& order, std::string s) {
    int rank[26];
    std::fill(rank, rank + 26, 26);
    for (int i = 0; i < (int)order.size(); ++i) rank[order[i] - 'a'] = i;
    std::stable_sort(s.begin(), s.end(), [&](char a, char b) { return rank[a - 'a'] < rank[b - 'a']; });
    return s;
}

// ---------------------------------------------------------------- 3. Selection

// Insertion sort of a[lo..hi), from tutorial 04: used here for groups of at most 5 elements.
template <class T>
void insertionSort(std::vector<T>& a, int lo, int hi) {
    for (int i = lo + 1; i < hi; ++i) {
        T x = a[i];
        int j = i;
        while (j > lo && x < a[j - 1]) { a[j] = a[j - 1]; --j; }
        a[j] = x;
    }
}

// Three-way partition of a[lo..hi) around the value `pivot`: a[lo..lt) < pivot, a[lt..gt) == pivot, a[gt..hi) > pivot.
// Returns {lt, gt}.
template <class T>
std::pair<int, int> partition3(std::vector<T>& a, int lo, int hi, const T& pivot) {
    int lt = lo, i = lo, gt = hi;
    while (i < gt) {
        if (a[i] < pivot) std::swap(a[lt++], a[i++]);
        else if (pivot < a[i]) std::swap(a[i], a[--gt]);
        else ++i;
    }
    return {lt, gt};
}

// Quickselect: the element that would be at index k if a[lo..hi) were sorted (lo <= k < hi). Partition around a random
// element, then keep only the side that holds index k. Expected O(n); rearranges a.
template <class T>
T quickSelect(std::vector<T>& a, int lo, int hi, int k, std::mt19937& rng) {
    while (true) {
        const T pivot = a[lo + (int)(rng() % (hi - lo))];
        auto [lt, gt] = partition3(a, lo, hi, pivot);
        if (k < lt) hi = lt;                                    // index k is among the smaller elements
        else if (k >= gt) lo = gt;                              // ... among the larger ones
        else return pivot;                                      // ... or it is in the block of elements equal to the pivot
    }
}

// The same result in O(n) in the worst case. The pivot is the median of the medians of the groups of 5, so every round
// removes at least about 3/10 of the elements. Rearranges a.
template <class T>
T selectMoM(std::vector<T>& a, int lo, int hi, int k) {
    while (hi - lo > 5) {
        std::vector<T> medians;
        for (int g = lo; g < hi; g += 5) {                      // sort each group of 5 and keep its median
            int end = std::min(g + 5, hi);
            insertionSort(a, g, end);
            medians.push_back(a[g + (end - g - 1) / 2]);
        }
        const int m = (int)medians.size();
        const T pivot = selectMoM(medians, 0, m, (m - 1) / 2);  // the median of the medians, found by the same function
        auto [lt, gt] = partition3(a, lo, hi, pivot);
        if (k < lt) hi = lt;
        else if (k >= gt) lo = gt;
        else return pivot;
    }
    insertionSort(a, lo, hi);                                   // at most 5 elements left
    return a[k];
}

// ---------------------------------------------------------------- 4. Top K

// The k largest elements of a (1 <= k <= n), largest first. Sort everything: O(n log n).
template <class T>
std::vector<T> topKBySort(std::vector<T> a, int k) {
    std::sort(a.begin(), a.end(), [](const T& x, const T& y) { return y < x; });
    a.resize(k);
    return a;
}

// A min-heap that holds the k largest elements seen so far: O(n log k), and it works on a stream. The heap's own order is
// reversed (its top is the SMALLEST kept element), so the test "is x better than the worst kept?" is  heap.top() < x.
template <class T>
std::vector<T> topKByHeap(const std::vector<T>& a, int k) {
    auto later = [](const T& x, const T& y) { return y < x; };
    std::priority_queue<T, std::vector<T>, decltype(later)> heap(later);
    for (const T& x : a) {
        if ((int)heap.size() < k) heap.push(x);
        else if (heap.top() < x) { heap.pop(); heap.push(x); }
    }
    std::vector<T> out(heap.size());
    for (int i = (int)out.size() - 1; i >= 0; --i) { out[i] = heap.top(); heap.pop(); }   // smallest first, so fill from the back
    return out;
}

// Partition around the k-th largest with nth_element, then sort only the k winners: O(n + k log k) on average.
template <class T>
std::vector<T> topKBySelect(std::vector<T> a, int k) {
    auto bigger = [](const T& x, const T& y) { return y < x; };
    std::nth_element(a.begin(), a.begin() + (k - 1), a.end(), bigger);
    a.resize(k);
    std::sort(a.begin(), a.end(), bigger);
    return a;
}

// The library's heap method: partial_sort puts the k largest first, in order, and leaves the rest unspecified.
template <class T>
std::vector<T> topKByPartialSort(std::vector<T> a, int k) {
    std::partial_sort(a.begin(), a.begin() + k, a.end(), [](const T& x, const T& y) { return y < x; });
    a.resize(k);
    return a;
}

// ---------------------------------------------------------------- 5. Intervals

struct Interval {
    int start, end;
};

// Merge overlapping closed intervals [start, end]; [1, 4] and [4, 5] share the point 4, so they merge. Sort by start, then
// sweep: an interval that starts inside the last merged one extends it, any other starts a new one.
std::vector<Interval> mergeIntervals(std::vector<Interval> v) {
    std::sort(v.begin(), v.end(), [](const Interval& a, const Interval& b) { return a.start < b.start; });
    std::vector<Interval> out;
    for (const Interval& iv : v) {
        if (!out.empty() && iv.start <= out.back().end) out.back().end = std::max(out.back().end, iv.end);
        else out.push_back(iv);
    }
    return out;
}

// The fewest rooms for meetings [start, end) (half-open: a meeting may start at the moment another one ends). Sort the
// starts and the ends separately; each start either reuses a room that is already free or opens a new one.
int minRooms(const std::vector<Interval>& v) {
    std::vector<int> starts, ends;
    for (const Interval& iv : v) { starts.push_back(iv.start); ends.push_back(iv.end); }
    std::sort(starts.begin(), starts.end());
    std::sort(ends.begin(), ends.end());
    int rooms = 0, freed = 0;                                   // freed: how many meetings, in order of ending, have been reused
    for (int s : starts) {
        if (s < ends[freed]) ++rooms;                           // even the earliest-ending meeting is still running: new room
        else ++freed;                                           // it is over: reuse its room
    }
    return rooms;
}

// ---- for the tests and the experiments

struct Counted {                                                // counts every comparison an algorithm makes
    int v;
    bool operator<(const Counted& o) const { ++g_less; return v < o.v; }
    static long long g_less;
};
long long Counted::g_less = 0;

static std::vector<Counted> asCounted(const Vec& v) {
    std::vector<Counted> c;
    for (int x : v) c.push_back({x});
    return c;
}

// n values from mt19937(seed), each in [0, 10^6)
static Vec randomValues(int n, unsigned seed) {
    std::mt19937 rng(seed);
    Vec v(n);
    for (int& x : v) x = (int)(rng() % 1000000);
    return v;
}

static std::string show(const Vec& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + std::to_string(v[i]);
    return s;
}
static std::string showIv(const std::vector<Interval>& v) {
    std::string s;
    for (const Interval& iv : v) s += "[" + std::to_string(iv.start) + "," + std::to_string(iv.end) + "]";
    return s;
}

// all arrays of length 0..maxLen over 0..range-1
static std::vector<Vec> allArrays(int maxLen, int range) {
    std::vector<Vec> all;
    for (int len = 0; len <= maxLen; ++len) {
        int total = 1; for (int i = 0; i < len; ++i) total *= range;
        for (int code = 0; code < total; ++code) {
            Vec v(len); int c = code;
            for (int i = 0; i < len; ++i) { v[i] = c % range; c /= range; }
            all.push_back(v);
        }
    }
    return all;
}

static std::vector<Interval> bruteMerge(std::vector<Interval> v) {   // merge any two that share a point, until nothing changes
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < v.size() && !changed; ++i)
            for (size_t j = i + 1; j < v.size() && !changed; ++j)
                if (v[i].start <= v[j].end && v[j].start <= v[i].end) {
                    v[i] = {std::min(v[i].start, v[j].start), std::max(v[i].end, v[j].end)};
                    v.erase(v.begin() + j);
                    changed = true;
                }
    }
    std::sort(v.begin(), v.end(), [](const Interval& a, const Interval& b) { return a.start < b.start; });
    return v;
}

static int bruteRooms(const std::vector<Interval>& v) {              // the most meetings running at one integer time
    int best = 0;
    for (int t = 0; t <= 12; ++t) {
        int c = 0;
        for (const Interval& iv : v) c += iv.start <= t && t < iv.end;
        best = std::max(best, c);
    }
    return best;
}

// Counts of `<` for one run of `work` on a copy of the values.
template <class Work>
static long long countLess(const Vec& values, Work work) {
    std::vector<Counted> a = asCounted(values);
    Counted::g_less = 0;
    work(a);
    return Counted::g_less;
}

int main() {
    // ---- comparators
    {
        Vec ints; for (int i = 0; i < 30; ++i) ints.push_back(i);
        struct P { int x, y; };
        std::vector<P> grid; for (int x = 0; x < 3; ++x) for (int y = 0; y < 3; ++y) grid.push_back({x, y});
        std::vector<std::string> words = {"", "a", "b", "ab", "ba", "abc", "b", "zz"};
        std::vector<double> reals = {1.0, 2.0, 3.0, std::nan("")};
        auto yn = [](bool b) { return b ? "yes" : "no"; };
        bool r1 = isStrictWeakOrder(ints, [](int a, int b) { return a < b; });
        bool r2 = isStrictWeakOrder(ints, [](int a, int b) { return a % 10 < b % 10; });
        bool r3 = isStrictWeakOrder(ints, [](int a, int b) { return a <= b; });
        bool r4 = isStrictWeakOrder(grid, [](const P& a, const P& b) { return a.x < b.x || a.y < b.y; });
        bool r5 = isStrictWeakOrder(grid, [](const P& a, const P& b) { return std::tie(a.x, a.y) < std::tie(b.x, b.y); });
        bool r6 = isStrictWeakOrder(ints, [](int a, int b) { return a + 3 < b; });
        bool r7 = isStrictWeakOrder(words, [](const std::string& a, const std::string& b) { return a.size() < b.size() || (a.size() == b.size() && a < b); });
        bool r8 = isStrictWeakOrder(ints, [](int a, int b) { return (bool)(a - b); });
        bool r9 = isStrictWeakOrder(reals, [](double a, double b) { return a < b; });
        bool r10 = isStrictWeakOrder(reals, [](double a, double b) { return std::isnan(b) ? !std::isnan(a) : a < b; });
        bool r11 = isStrictWeakOrder(ints, [](int a, int b) { return a > b; });
        bool r12 = isStrictWeakOrder(ints, [](int a, int b) { return (a % 2) < (b % 2) || (a % 2 == b % 2 && a < b); });
        Vec cycle = {0, 1, 2};
        bool r13 = isStrictWeakOrder(cycle, [](int a, int b) { return (b - a + 3) % 3 == 1; });
        std::printf("strict weak order: a<b %s | a%%10<b%%10 %s | a<=b %s | x<x||y<y %s | tie(x,y) %s | a+3<b %s | length then text %s | (bool)(a-b) %s | double < with NaN %s | NaN last %s | a>b %s | parity then value %s | cycle 0<1<2<0 %s\n",
                    yn(r1), yn(r2), yn(r3), yn(r4), yn(r5), yn(r6), yn(r7), yn(r8), yn(r9), yn(r10), yn(r11), yn(r12), yn(r13));
        CHECK(r1 && r2 && !r3 && !r4 && r5 && !r6 && r7 && !r8 && !r9 && r10 && r11 && r12 && !r13);

        std::vector<Student> st = {{"Cy", 80}, {"Ann", 90}, {"Bob", 80}, {"Di", 90}, {"Ed", 70}};
        auto w1 = st, w2 = st;
        std::sort(w1.begin(), w1.end(), byGradeThenName);
        std::sort(w2.begin(), w2.end(), byGradeThenNameTie);
        std::string order;
        for (const Student& s : w1) order += s.name + " ";
        std::printf("by grade then name: %s\n", order.c_str());
        bool same = true;
        for (const Student& a : st) for (const Student& b : st) same = same && byGradeThenName(a, b) == byGradeThenNameTie(a, b);
        CHECK(same);
        for (size_t i = 0; i < w1.size(); ++i) CHECK(w1[i].name == w2[i].name);
        CHECK(isStrictWeakOrder(st, byGradeThenName));

        Vec v = {30, 10, 20, 10};
        std::printf("argsort 30,10,20,10: %s\n", show(argsort(v)).c_str());
        for (const Vec& a : allArrays(6, 3)) {
            std::vector<int> idx = argsort(a);
            std::vector<int> sorted = idx; std::sort(sorted.begin(), sorted.end());
            bool ok = (int)idx.size() == (int)a.size();
            for (size_t i = 0; i < sorted.size(); ++i) ok = ok && sorted[i] == (int)i;            // a permutation
            for (size_t i = 1; i < idx.size(); ++i)
                ok = ok && (a[idx[i - 1]] < a[idx[i]] || (a[idx[i - 1]] == a[idx[i]] && idx[i - 1] < idx[i]));   // sorted, stable
            CHECK(ok);
        }
        std::mt19937 rng(41);                                   // a larger input: std::sort would not keep the order of ties
        Vec big(200); for (int& x : big) x = (int)(rng() % 3);
        std::vector<int> idx = argsort(big);
        bool ok = true;
        for (size_t i = 1; i < idx.size(); ++i)
            ok = ok && (big[idx[i - 1]] < big[idx[i]] || (big[idx[i - 1]] == big[idx[i]] && idx[i - 1] < idx[i]));
        CHECK(ok);
    }
    // ---- orders from a rule
    {
        std::printf("largest number 3,30,34,5,9: %s | 10,2: %s | 0,0: %s | 824,8247: %s\n", largestNumber({3, 30, 34, 5, 9}).c_str(),
                    largestNumber({10, 2}).c_str(), largestNumber({0, 0}).c_str(), largestNumber({824, 8247}).c_str());
        CHECK(largestNumber({3, 30, 34, 5, 9}) == "9534330" && largestNumber({10, 2}) == "210" && largestNumber({0, 0}) == "0");
        const Vec pool = {0, 1, 3, 9, 10, 12, 30, 34, 121, 12, 120, 999, 98, 9, 90};
        std::mt19937 rng(5);
        for (int round = 0; round < 3000; ++round) {
            int n = 1 + (int)(rng() % 6);
            Vec nums; for (int i = 0; i < n; ++i) nums.push_back(pool[rng() % pool.size()]);
            Vec perm = nums; std::sort(perm.begin(), perm.end());
            std::string best;
            do {
                std::string s; for (int x : perm) s += std::to_string(x);
                best = std::max(best, s);
            } while (std::next_permutation(perm.begin(), perm.end()));
            if (best[0] == '0') best = "0";
            CHECK(largestNumber(nums) == best);
        }
        std::printf("custom sort string cba,abcd: %s\n", customSortString("cba", "abcd").c_str());
        CHECK(customSortString("cba", "abcd") == "cbad");
        for (int round = 0; round < 2000; ++round) {
            std::string letters = "abcdef", order, s;
            std::shuffle(letters.begin(), letters.end(), rng);
            order = letters.substr(0, rng() % 7);
            for (int i = 0, len = (int)(rng() % 9); i < len; ++i) s += (char)('a' + rng() % 6);
            std::string r = customSortString(order, s);
            std::string a = r, b = s; std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end());
            bool ok = a == b;                                   // same letters
            for (size_t i = 0; i < r.size(); ++i)
                for (size_t j = i + 1; j < r.size(); ++j) {
                    size_t pi = order.find(r[i]), pj = order.find(r[j]);
                    if (pi != std::string::npos && pj != std::string::npos) ok = ok && pi <= pj;       // the order is respected
                    if (pi == std::string::npos && pj != std::string::npos) ok = false;                // letters outside the order are last
                }
            CHECK(ok);
        }
        for (int round = 0; round < 200; ++round) {              // letters that `order` lacks keep their old order
            std::string s, order = "qwerty", lacking, lackingAfter;
            for (int i = 0; i < 100; ++i) s += (char)('a' + rng() % 26);
            std::string r = customSortString(order, s);
            for (char c : s) if (order.find(c) == std::string::npos) lacking += c;
            for (char c : r) if (order.find(c) == std::string::npos) lackingAfter += c;
            CHECK(lacking == lackingAfter);
        }
    }
    // ---- selection
    {
        Vec a = {7, 2, 9, 4, 6, 1, 8};
        auto [lt, gt] = partition3(a, 0, 7, 7);
        std::printf("partition3 7,2,9,4,6,1,8 pivot 7 -> lt=%d gt=%d: %s\n", lt, gt, show(a).c_str());
        Vec left(a.begin(), a.begin() + lt);
        auto [lt2, gt2] = partition3(left, 0, lt, 4);
        std::printf("partition3 of the first %d with pivot 4 -> lt=%d gt=%d: %s\n", lt, lt2, gt2, show(left).c_str());
        for (const Vec& arr : allArrays(7, 4)) {
            int n = (int)arr.size();
            Vec sorted = arr; std::sort(sorted.begin(), sorted.end());
            for (int k = 0; k < n; ++k) {
                Vec b = arr; std::mt19937 rng(k + 1);
                CHECK(quickSelect(b, 0, n, k, rng) == sorted[k]);
                Vec c = arr;
                CHECK(selectMoM(c, 0, n, k) == sorted[k]);
                std::sort(c.begin(), c.end()); CHECK(c == sorted);                 // only rearranged
                if (n > 2 && k > 0 && k < n - 1) {                                 // a part of the array only
                    Vec d = arr; int r = selectMoM(d, 1, n - 1, k);
                    Vec mid(arr.begin() + 1, arr.end() - 1); std::sort(mid.begin(), mid.end());
                    CHECK(r == mid[k - 1] && d[0] == arr[0] && d[n - 1] == arr[n - 1]);
                }
            }
        }
        std::mt19937 rng(99);
        for (int round = 0; round < 400; ++round) {
            static const int ranges[] = {1, 2, 5, 50, 100000};
            int n = 1 + (int)(rng() % 400), range = ranges[rng() % 5];
            Vec arr(n); for (int& x : arr) x = (int)(rng() % range);
            int shape = (int)(rng() % 4);
            if (shape == 1) std::sort(arr.begin(), arr.end());
            if (shape == 2) { std::sort(arr.begin(), arr.end()); std::reverse(arr.begin(), arr.end()); }
            Vec sorted = arr; std::sort(sorted.begin(), sorted.end());
            int k = (int)(rng() % n);
            Vec b = arr, c = arr; std::mt19937 r2(round);
            CHECK(quickSelect(b, 0, n, k, r2) == sorted[k]);
            CHECK(selectMoM(c, 0, n, k) == sorted[k]);
        }
    }
    // ---- top K
    {
        std::mt19937 rng(17);
        for (int round = 0; round < 600; ++round) {
            static const int ranges[] = {2, 5, 100, 1000000};
            int n = 1 + (int)(rng() % 40), range = ranges[rng() % 4];
            Vec a(n); for (int& x : a) x = (int)(rng() % range);
            Vec sorted = a; std::sort(sorted.begin(), sorted.end(), std::greater<int>());
            for (int k = 1; k <= n; ++k) {
                Vec want(sorted.begin(), sorted.begin() + k);
                CHECK(topKBySort(a, k) == want);
                CHECK(topKByHeap(a, k) == want);
                CHECK(topKBySelect(a, k) == want);
                CHECK(topKByPartialSort(a, k) == want);
            }
        }
    }
    // ---- intervals
    {
        std::printf("merge [1,3][2,6][8,10][15,18]: %s | [1,4][4,5]: %s | [1,4][5,6]: %s\n",
                    showIv(mergeIntervals({{1, 3}, {2, 6}, {8, 10}, {15, 18}})).c_str(), showIv(mergeIntervals({{1, 4}, {4, 5}})).c_str(),
                    showIv(mergeIntervals({{1, 4}, {5, 6}})).c_str());
        std::printf("rooms [0,30][5,10][15,20]: %d | [7,10][2,4]: %d | [0,30][30,60]: %d | [1,5][2,6][3,7][8,9]: %d\n", minRooms({{0, 30}, {5, 10}, {15, 20}}),
                    minRooms({{7, 10}, {2, 4}}), minRooms({{0, 30}, {30, 60}}), minRooms({{1, 5}, {2, 6}, {3, 7}, {8, 9}}));
        CHECK(minRooms({{0, 30}, {5, 10}, {15, 20}}) == 2 && minRooms({{7, 10}, {2, 4}}) == 1 && minRooms({{0, 30}, {30, 60}}) == 1);
        CHECK(minRooms({}) == 0 && mergeIntervals({}).empty());
        std::mt19937 rng(23);
        for (int round = 0; round < 20000; ++round) {
            int n = (int)(rng() % 7);
            std::vector<Interval> v;
            for (int i = 0; i < n; ++i) { int s = (int)(rng() % 10), len = 1 + (int)(rng() % 3); v.push_back({s, s + len}); }   // start < end, all within 0..12
            std::vector<Interval> got = mergeIntervals(v), want = bruteMerge(v);
            bool same = got.size() == want.size();
            for (size_t i = 0; same && i < got.size(); ++i) same = got[i].start == want[i].start && got[i].end == want[i].end;
            CHECK(same);
            CHECK(minRooms(v) == bruteRooms(v));
        }
    }
    // ---- the counts the tutorial quotes
    {
        const int n = 1000, k = n / 2 - 1;                      // k = 499 is the index of the lower median
        long long qs = 0, mom = 0, nth = 0, srt = 0;
        const int runs = 200;
        for (int seed = 1; seed <= runs; ++seed) {
            Vec data = randomValues(n, seed);
            std::mt19937 rng(seed);
            qs += countLess(data, [&](std::vector<Counted>& a) { quickSelect(a, 0, n, k, rng); });
            mom += countLess(data, [&](std::vector<Counted>& a) { selectMoM(a, 0, n, k); });
            nth += countLess(data, [&](std::vector<Counted>& a) { std::nth_element(a.begin(), a.begin() + k, a.end()); });
            srt += countLess(data, [&](std::vector<Counted>& a) { std::sort(a.begin(), a.end()); });
        }
        Vec sortedData = randomValues(n, 2024); std::sort(sortedData.begin(), sortedData.end());
        Vec equalData(n, 5);
        auto one = [&](const Vec& d, const char* name) {
            std::mt19937 rng(7);
            std::printf("select counts n=1000 k=499 %-6s quickselect %lld | median of medians %lld | nth_element %lld | sort %lld\n", name,
                        countLess(d, [&](std::vector<Counted>& a) { quickSelect(a, 0, n, k, rng); }),
                        countLess(d, [&](std::vector<Counted>& a) { selectMoM(a, 0, n, k); }),
                        countLess(d, [&](std::vector<Counted>& a) { std::nth_element(a.begin(), a.begin() + k, a.end()); }),
                        countLess(d, [&](std::vector<Counted>& a) { std::sort(a.begin(), a.end()); }));
        };
        std::printf("select counts n=1000 k=499 average of %d random arrays: quickselect %lld | median of medians %lld | nth_element %lld | sort %lld\n",
                    runs, qs / runs, mom / runs, nth / runs, srt / runs);
        one(sortedData, "sorted");
        one(equalData, "equal");
        const int N = 100000;
        Vec big = randomValues(N, 2024);
        for (int kk : {10, 1000, 50000}) {
            std::printf("top-k counts n=100000 k=%-5d sort %lld | heap %lld | nth_element+sort %lld | partial_sort %lld\n", kk,
                        countLess(big, [&](std::vector<Counted>& a) { topKBySort(a, kk); }),
                        countLess(big, [&](std::vector<Counted>& a) { topKByHeap(a, kk); }),
                        countLess(big, [&](std::vector<Counted>& a) { topKBySelect(a, kk); }),
                        countLess(big, [&](std::vector<Counted>& a) { topKByPartialSort(a, kk); }));
        }
    }

    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
