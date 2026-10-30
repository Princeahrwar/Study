// Tutorial: merging and sorting linked lists. The functions shown in the tutorial, checked against std::merge,
// std::stable_sort and brute force on every small input.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 04_merging_and_sorting_lists_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <cstdio>
#include <functional>
#include <list>
#include <numeric>
#include <queue>
#include <random>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

using Vec = std::vector<int>;

struct ListNode {
    int val;
    ListNode* next;
    explicit ListNode(int v = 0, ListNode* n = nullptr) : val(v), next(n) {}
};

// ---------------------------------------------------------------- 1. Merging two sorted lists

// Merge two sorted lists by relinking their nodes. Stable: on a tie the node of a goes first, so the comparison is
// less(b, a). At most n + m - 1 comparisons, O(1) extra memory.
template <class Less = std::less<int>>
ListNode* mergeTwo(ListNode* a, ListNode* b, Less less = Less()) {
    ListNode dummy;
    ListNode* tail = &dummy;
    while (a && b) {
        if (less(b->val, a->val)) {
            tail->next = b;
            b = b->next;
        } else {
            tail->next = a;
            a = a->next;
        }
        tail = tail->next;
    }
    tail->next = a ? a : b;  // one list is empty: the other one is already in order
    return dummy.next;
}

// The same merge by recursion. The depth is n + m, so a long list can overflow the stack.
ListNode* mergeTwoRec(ListNode* a, ListNode* b) {
    if (!a) return b;
    if (!b) return a;
    if (b->val < a->val) {
        b->next = mergeTwoRec(a, b->next);
        return b;
    }
    a->next = mergeTwoRec(a->next, b);
    return a;
}

// ---------------------------------------------------------------- 2. Merging k sorted lists

// One at a time: the result grows, and every node of the early lists is compared again and again. O(N k).
template <class Less = std::less<int>>
ListNode* mergeKFold(const std::vector<ListNode*>& lists, Less less = Less()) {
    ListNode* result = nullptr;
    for (ListNode* l : lists) result = mergeTwo(result, l, less);
    return result;
}

// A min-heap that holds the current head of every list. O(N log k).
template <class Less = std::less<int>>
ListNode* mergeKHeap(const std::vector<ListNode*>& lists, Less less = Less()) {
    auto after = [&](const ListNode* x, const ListNode* y) { return less(y->val, x->val); };  // reversed: a min-heap
    std::priority_queue<ListNode*, std::vector<ListNode*>, decltype(after)> heap(after);
    for (ListNode* h : lists)
        if (h) heap.push(h);
    ListNode dummy;
    ListNode* tail = &dummy;
    while (!heap.empty()) {
        ListNode* n = heap.top();
        heap.pop();
        tail->next = n;
        tail = n;
        if (n->next) heap.push(n->next);
    }
    return dummy.next;
}

// The same heap, but stable across lists: an entry also knows its list, and on a tie the earlier list goes first.
template <class Less = std::less<int>>
ListNode* mergeKHeapStable(const std::vector<ListNode*>& lists, Less less = Less()) {
    using Entry = std::pair<ListNode*, int>;  // the current head of a list, and the index of that list
    auto after = [&](const Entry& x, const Entry& y) {
        if (less(y.first->val, x.first->val)) return true;   // x is larger than y
        if (less(x.first->val, y.first->val)) return false;  // x is smaller than y
        return x.second > y.second;                          // a tie: the later list comes later
    };
    std::priority_queue<Entry, std::vector<Entry>, decltype(after)> heap(after);
    for (int i = 0; i < (int)lists.size(); ++i)
        if (lists[i]) heap.push({lists[i], i});
    ListNode dummy;
    ListNode* tail = &dummy;
    while (!heap.empty()) {
        Entry e = heap.top();
        heap.pop();
        tail->next = e.first;
        tail = e.first;
        if (e.first->next) heap.push({e.first->next, e.second});
    }
    return dummy.next;
}

// Merge lists[lo..hi) by halves: the lists are merged in pairs, like a tournament. O(N log k), stable across lists.
template <class Less = std::less<int>>
ListNode* mergeKRange(const std::vector<ListNode*>& lists, int lo, int hi, Less less = Less()) {
    if (lo == hi) return nullptr;
    if (hi - lo == 1) return lists[lo];
    int mid = lo + (hi - lo) / 2;
    return mergeTwo(mergeKRange(lists, lo, mid, less), mergeKRange(lists, mid, hi, less), less);
}

// ---------------------------------------------------------------- 3. Sorting a list

// Cut the list after its first half and return the head of the second half. The list has at least two nodes.
ListNode* splitMiddle(ListNode* head) {
    ListNode* slow = head;
    ListNode* fast = head->next;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    ListNode* second = slow->next;
    slow->next = nullptr;
    return second;
}

// Top-down merge sort: split, sort both halves, merge. Stable, O(n log n), O(log n) stack.
template <class Less = std::less<int>>
ListNode* sortListTopDown(ListNode* head, Less less = Less()) {
    if (!head || !head->next) return head;
    ListNode* second = splitMiddle(head);
    ListNode* left = sortListTopDown(head, less);
    ListNode* right = sortListTopDown(second, less);
    return mergeTwo(left, right, less);
}

// Cut the list after n nodes (n >= 1) and return the head of the rest, or nullptr if there is no rest.
ListNode* cut(ListNode* head, int n) {
    for (int i = 1; head && i < n; ++i) head = head->next;
    if (!head) return nullptr;
    ListNode* rest = head->next;
    head->next = nullptr;
    return rest;
}

// Bottom-up merge sort: merge runs of 1, then 2, then 4 nodes, until one run is left. Stable, O(n log n), O(1) memory.
template <class Less = std::less<int>>
ListNode* sortListBottomUp(ListNode* head, Less less = Less()) {
    int len = 0;
    for (ListNode* p = head; p; p = p->next) ++len;
    ListNode dummy(0, head);
    for (int width = 1; width < len; width *= 2) {
        ListNode* prev = &dummy;  // the last node of the part that is already merged in this pass
        ListNode* cur = dummy.next;
        while (cur) {
            ListNode* left = cur;
            ListNode* right = cut(left, width);
            cur = cut(right, width);
            prev->next = mergeTwo(left, right, less);
            while (prev->next) prev = prev->next;  // walk to the end of the merged run
        }
    }
    return dummy.next;
}

// Insertion sort on a list: take each node and walk the sorted part to its place. Stable, O(n^2), O(1) memory.
ListNode* insertionSortList(ListNode* head) {
    ListNode dummy;
    while (head) {
        ListNode* next = head->next;
        ListNode* p = &dummy;
        while (p->next && !(head->val < p->next->val)) p = p->next;  // stop before the first greater value
        head->next = p->next;
        p->next = head;
        head = next;
    }
    return dummy.next;
}

// ---------------------------------------------------------------- 4. Where two lists meet

// The shared node of two lists that join into one (LeetCode 160), or nullptr. Each pointer walks its own list and then
// the other one, so both walk n + m nodes and arrive together. Neither list is changed.
ListNode* getIntersection(ListNode* a, ListNode* b) {
    ListNode* p = a;
    ListNode* q = b;
    while (p != q) {
        p = p ? p->next : b;
        q = q ? q->next : a;
    }
    return p;
}

// ---- for the tests and the experiments

static ListNode* build(const Vec& v) {
    ListNode dummy;
    ListNode* tail = &dummy;
    for (int x : v) {
        tail->next = new ListNode(x);
        tail = tail->next;
    }
    return dummy.next;
}

static Vec toVec(ListNode* h) {
    Vec out;
    for (; h && out.size() < 100000; h = h->next) out.push_back(h->val);
    return out;
}

static void freeList(ListNode* h) {
    while (h) {
        ListNode* n = h->next;
        delete h;
        h = n;
    }
}

struct Counting {  // compares like <, and counts the calls
    long* calls;
    bool operator()(int a, int b) const {
        ++*calls;
        return a < b;
    }
};

struct ByKey {  // compares only the key (val / 100) and counts: the rest of val is a tag that shows the original order
    long* calls;
    bool operator()(int a, int b) const {
        ++*calls;
        return a / 100 < b / 100;
    }
};

// Equal keys must appear in the order of their tags.
static bool stableByKey(const Vec& out) {
    for (std::size_t i = 1; i < out.size(); ++i) {
        if (out[i - 1] / 100 > out[i] / 100) return false;
        if (out[i - 1] / 100 == out[i] / 100 && out[i - 1] % 100 > out[i] % 100) return false;
    }
    return true;
}

static std::vector<ListNode*> nodesOf(ListNode* h) {
    std::vector<ListNode*> out;
    for (; h && out.size() < 100000; h = h->next) out.push_back(h);
    return out;
}

// For plain int values: equal values must come out in the order of the original nodes (checked by node address).
static bool stableByNode(const std::vector<ListNode*>& original, ListNode* sorted) {
    std::vector<ListNode*> out = nodesOf(sorted);
    if (out.size() != original.size()) return false;
    auto pos = [&](ListNode* n) { return std::find(original.begin(), original.end(), n) - original.begin(); };
    for (std::size_t i = 1; i < out.size(); ++i) {
        if (out[i - 1]->val > out[i]->val) return false;
        if (out[i - 1]->val == out[i]->val && pos(out[i - 1]) > pos(out[i])) return false;
    }
    return true;
}

// n ceil(lg n) - 2^ceil(lg n) + 1: the most comparisons the top-down merge sort of n items can need.
static long topDownWorst(long n) {
    long p = 1;
    int lg = 0;
    while (p < n) { p *= 2; ++lg; }
    return n * lg - p + 1;
}

// n ceil(lg n): every pass of the bottom-up sort makes fewer than n comparisons. (The bound above is not true for it:
// n = 5 can need 9 comparisons, and the bound is 8.)
static long bottomUpBound(long n) {
    long p = 1;
    int lg = 0;
    while (p < n) { p *= 2; ++lg; }
    return n * lg;
}

static std::vector<Vec> sortedVecs(int maxLen, int maxVal) {  // every non-decreasing vector
    std::vector<Vec> all;
    Vec cur;
    std::function<void(int)> rec = [&](int lo) {
        all.push_back(cur);
        if ((int)cur.size() == maxLen) return;
        for (int x = lo; x <= maxVal; ++x) { cur.push_back(x); rec(x); cur.pop_back(); }
    };
    rec(0);
    return all;
}

static void testMergeTwo() {
    std::vector<Vec> all = sortedVecs(4, 3);
    for (const Vec& x : all) {
        for (const Vec& y : all) {
            Vec want(x.size() + y.size());
            std::merge(x.begin(), x.end(), y.begin(), y.end(), want.begin());
            long calls = 0;
            ListNode* m = mergeTwo(build(x), build(y), Counting{&calls});
            CHECK(toVec(m) == want);
            CHECK(calls <= (long)(x.size() + y.size()) - 1 || x.empty() || y.empty());
            CHECK(calls >= (long)std::min(x.size(), y.size()));
            freeList(m);
            m = mergeTwoRec(build(x), build(y));
            CHECK(toVec(m) == want);
            freeList(m);
            ListNode* ha = build(x);
            ListNode* hb = build(y);
            std::vector<ListNode*> original = nodesOf(ha);
            for (ListNode* n : nodesOf(hb)) original.push_back(n);
            m = mergeTwo(ha, hb);
            CHECK(stableByNode(original, m));
            freeList(m);
            ha = build(x);
            hb = build(y);
            original = nodesOf(ha);
            for (ListNode* n : nodesOf(hb)) original.push_back(n);
            m = mergeTwoRec(ha, hb);
            CHECK(stableByNode(original, m));
            freeList(m);
        }
    }
    // stability: keys 0..2 in x, tags 0..9; the same keys in y with tags 10..; ties must keep x first
    std::mt19937 rng(5);
    for (int round = 0; round < 500; ++round) {
        Vec a, b;
        for (int i = 0, n = (int)(rng() % 6); i < n; ++i) a.push_back((int)(rng() % 3) * 100);
        for (int i = 0, n = (int)(rng() % 6); i < n; ++i) b.push_back((int)(rng() % 3) * 100);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        for (std::size_t i = 0; i < a.size(); ++i) a[i] += (int)i;        // tags 0.. in a
        for (std::size_t i = 0; i < b.size(); ++i) b[i] += 50 + (int)i;   // tags 50.. in b: later than any in a
        long calls = 0;
        ListNode* m = mergeTwo(build(a), build(b), ByKey{&calls});
        CHECK(stableByKey(toVec(m)));
        CHECK(toVec(m).size() == a.size() + b.size());
        freeList(m);
    }
}

static std::vector<ListNode*> buildAll(const std::vector<Vec>& lists) {
    std::vector<ListNode*> out;
    for (const Vec& v : lists) out.push_back(build(v));
    return out;
}

static void testMergeK() {
    std::vector<Vec> all = sortedVecs(3, 2);
    std::mt19937 rng(9);
    for (int round = 0; round < 4000; ++round) {
        int k = (int)(rng() % 6);
        std::vector<Vec> lists;
        Vec want;
        for (int i = 0; i < k; ++i) {
            lists.push_back(all[rng() % all.size()]);
            want.insert(want.end(), lists.back().begin(), lists.back().end());
        }
        std::sort(want.begin(), want.end());
        ListNode* r = mergeKFold(buildAll(lists));
        CHECK(toVec(r) == want);
        freeList(r);
        r = mergeKHeap(buildAll(lists));
        CHECK(toVec(r) == want);
        freeList(r);
        r = mergeKHeapStable(buildAll(lists));
        CHECK(toVec(r) == want);
        freeList(r);
        std::vector<ListNode*> heads = buildAll(lists);
        r = mergeKRange(heads, 0, (int)heads.size());
        CHECK(toVec(r) == want);
        freeList(r);
    }
    // stability across lists for the fold and the pairwise merge: tag = list index * 10 + position
    for (int round = 0; round < 500; ++round) {
        int k = 1 + (int)(rng() % 6);
        std::vector<Vec> lists(k);
        for (int i = 0; i < k; ++i) {
            for (int j = 0, n = (int)(rng() % 5); j < n; ++j) lists[i].push_back((int)(rng() % 3) * 100);
            std::sort(lists[i].begin(), lists[i].end());
            for (std::size_t j = 0; j < lists[i].size(); ++j) lists[i][j] += i * 10 + (int)j;
        }
        long calls = 0;
        ListNode* r = mergeKFold(buildAll(lists), ByKey{&calls});
        Vec byFold = toVec(r);
        CHECK(stableByKey(byFold));
        freeList(r);
        std::vector<ListNode*> heads = buildAll(lists);
        r = mergeKRange(heads, 0, (int)heads.size(), ByKey{&calls});
        CHECK(stableByKey(toVec(r)));
        CHECK(toVec(r) == byFold);  // two stable merges of the same input give the same sequence, tags included
        freeList(r);
        r = mergeKHeapStable(buildAll(lists), ByKey{&calls});
        CHECK(stableByKey(toVec(r)));
        CHECK(toVec(r) == byFold);
        freeList(r);
    }
}

static void checkSorted(const Vec& v) {
    Vec want = v;
    std::stable_sort(want.begin(), want.end());
    long c1 = 0, c2 = 0;
    ListNode* r = sortListTopDown(build(v), Counting{&c1});
    CHECK(toVec(r) == want);
    CHECK(c1 <= topDownWorst((long)v.size()) || v.size() < 2);
    freeList(r);
    r = sortListBottomUp(build(v), Counting{&c2});
    CHECK(toVec(r) == want);
    CHECK(c2 <= bottomUpBound((long)v.size()) || v.size() < 2);
    freeList(r);
    r = insertionSortList(build(v));
    CHECK(toVec(r) == want);
    freeList(r);
    std::list<int> lib(v.begin(), v.end());  // the library's sort makes the same comparisons as the bottom-up sort
    long c3 = 0;
    lib.sort(Counting{&c3});
    CHECK(Vec(lib.begin(), lib.end()) == want);
    CHECK(c3 == c2);
}

static void testSort() {
    // every sequence of length <= 7 over {0, 1, 2}, and every permutation of 1..7
    for (int len = 0; len <= 7; ++len) {
        int total = 1;
        for (int i = 0; i < len; ++i) total *= 3;
        for (int code = 0; code < total; ++code) {
            Vec v(len);
            for (int i = 0, c = code; i < len; ++i, c /= 3) v[i] = c % 3;
            checkSorted(v);
        }
    }
    Vec perm(7);
    std::iota(perm.begin(), perm.end(), 1);
    do { checkSorted(perm); } while (std::next_permutation(perm.begin(), perm.end()));
    // stability: key = val / 100, tag = the original position
    std::mt19937 rng(3);
    for (int round = 0; round < 1000; ++round) {
        Vec v;
        for (int i = 0, n = (int)(rng() % 40); i < n; ++i) v.push_back((int)(rng() % 4) * 100 + i);
        long calls = 0;
        ListNode* r = sortListTopDown(build(v), ByKey{&calls});
        CHECK(stableByKey(toVec(r)));
        CHECK(toVec(r).size() == v.size());
        freeList(r);
        r = sortListBottomUp(build(v), ByKey{&calls});
        CHECK(stableByKey(toVec(r)));
        CHECK(toVec(r).size() == v.size());
        freeList(r);
        std::list<int> lib(v.begin(), v.end());
        lib.sort(ByKey{&calls});
        CHECK(stableByKey(Vec(lib.begin(), lib.end())));
    }
    // the int versions: equal values must keep the order of the original nodes
    for (int round = 0; round < 500; ++round) {
        Vec v;
        for (int i = 0, n = (int)(rng() % 30); i < n; ++i) v.push_back((int)(rng() % 4));
        ListNode* h = build(v);
        std::vector<ListNode*> original = nodesOf(h);
        h = sortListTopDown(h);
        CHECK(stableByNode(original, h));
        freeList(h);
        h = build(v);
        original = nodesOf(h);
        h = sortListBottomUp(h);
        CHECK(stableByNode(original, h));
        freeList(h);
        h = build(v);
        original = nodesOf(h);
        h = insertionSortList(h);
        CHECK(stableByNode(original, h));
        freeList(h);
    }
    // longer random lists, and sorted and reverse sorted ones
    for (int n : {100, 1000, 1023, 1024, 1025}) {
        Vec v(n);
        std::iota(v.begin(), v.end(), 0);
        checkSorted(v);
        std::reverse(v.begin(), v.end());
        checkSorted(v);
        std::shuffle(v.begin(), v.end(), rng);
        if (n <= 1000) checkSorted(v);
    }
}

// Two lists with x and y private nodes that join into c shared nodes. Returns the first shared node.
static void testIntersection() {
    for (int x = 0; x <= 4; ++x) {
        for (int y = 0; y <= 4; ++y) {
            for (int c = 0; c <= 4; ++c) {
                ListNode* shared = build(Vec(c, 7));
                ListNode* a = shared;
                for (int i = 0; i < x; ++i) a = new ListNode(1, a);
                ListNode* b = shared;
                for (int i = 0; i < y; ++i) b = new ListNode(1, b);
                CHECK(getIntersection(a, b) == shared);  // nullptr when c == 0 (shared is nullptr)
                CHECK(getIntersection(b, a) == shared);
                // the lists are untouched
                int lenA = 0, lenB = 0;
                for (ListNode* p = a; p; p = p->next) ++lenA;
                for (ListNode* p = b; p; p = p->next) ++lenB;
                CHECK(lenA == x + c && lenB == y + c);
                // free the private parts, then the shared part
                for (int i = 0; i < x; ++i) { ListNode* n = a->next; delete a; a = n; }
                for (int i = 0; i < y; ++i) { ListNode* n = b->next; delete b; b = n; }
                freeList(shared);
            }
        }
    }
    // equal values but different nodes do not intersect
    ListNode* a = build({1, 2, 3});
    ListNode* b = build({1, 2, 3});
    CHECK(getIntersection(a, b) == nullptr);
    CHECK(getIntersection(a, a) == a);
    CHECK(getIntersection(nullptr, a) == nullptr);
    freeList(a);
    freeList(b);
}

int main() {
    testMergeTwo();
    testMergeK();
    testSort();
    testIntersection();
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
