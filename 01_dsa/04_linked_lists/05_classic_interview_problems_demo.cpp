// Tutorial: classic linked-list interview problems. The functions shown in the tutorial, checked against brute-force
// models (a vector, a number, std::rotate, a slow list-of-pairs cache) on every small input and on random ones.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 05_classic_interview_problems_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <cstdio>
#include <list>
#include <numeric>
#include <random>
#include <unordered_map>
#include <unordered_set>
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

// ---------------------------------------------------------------- 1. Palindrome list

// Reverse a list in place and return the new head (tutorial 02).
ListNode* reverseList(ListNode* head) {
    ListNode* prev = nullptr;
    while (head) {
        ListNode* next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}

// The last node of the first half: node number ceil(n / 2). The list must not be empty.
ListNode* endOfFirstHalf(ListNode* head) {
    ListNode* slow = head;
    ListNode* fast = head;
    while (fast->next && fast->next->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    return slow;
}

// O(n) time, O(1) extra memory. The list is the same when the function returns.
bool isPalindrome(ListNode* head) {
    if (!head) return true;
    ListNode* firstEnd = endOfFirstHalf(head);
    ListNode* second = reverseList(firstEnd->next);  // the second half, backwards
    bool ok = true;
    for (ListNode *p = head, *q = second; q; p = p->next, q = q->next) {
        if (p->val != q->val) {
            ok = false;
            break;
        }
    }
    reverseList(second);  // put the second half back: firstEnd->next still points at its first node
    return ok;
}

// The recursive version: the call stack walks back from the end while `front` walks forward. Depth n.
static bool matchesFront(ListNode*& front, ListNode* cur) {
    if (!cur) return true;
    if (!matchesFront(front, cur->next)) return false;
    if (front->val != cur->val) return false;
    front = front->next;
    return true;
}

bool isPalindromeRec(ListNode* head) {
    ListNode* front = head;
    return matchesFront(front, head);
}

// ---------------------------------------------------------------- 2. Add two numbers

// LeetCode 2: the digits are stored with the least significant digit first, so the lists already start where the
// addition starts. The result is a new list; the inputs are not changed.
ListNode* addTwoNumbers(const ListNode* a, const ListNode* b) {
    ListNode dummy;
    ListNode* tail = &dummy;
    int carry = 0;
    while (a || b || carry) {
        int sum = carry;
        if (a) {
            sum += a->val;
            a = a->next;
        }
        if (b) {
            sum += b->val;
            b = b->next;
        }
        tail->next = new ListNode(sum % 10);
        tail = tail->next;
        carry = sum / 10;
    }
    return dummy.next;
}

// LeetCode 445: the most significant digit comes first. Two stacks hold the digits; popping gives the least
// significant digit first, and each new node goes in FRONT of the result, so no reversal is needed.
ListNode* addForward(const ListNode* a, const ListNode* b) {
    std::vector<int> sa, sb;
    for (; a; a = a->next) sa.push_back(a->val);
    for (; b; b = b->next) sb.push_back(b->val);
    ListNode* head = nullptr;
    int carry = 0;
    while (!sa.empty() || !sb.empty() || carry) {
        int sum = carry;
        if (!sa.empty()) {
            sum += sa.back();
            sa.pop_back();
        }
        if (!sb.empty()) {
            sum += sb.back();
            sb.pop_back();
        }
        head = new ListNode(sum % 10, head);
        carry = sum / 10;
    }
    return head;
}

// The same without stacks: reverse both inputs, use addTwoNumbers, reverse the sum, and put the inputs back.
ListNode* addForwardByReversing(ListNode* a, ListNode* b) {
    a = reverseList(a);
    b = reverseList(b);
    ListNode* sum = reverseList(addTwoNumbers(a, b));
    reverseList(a);  // the inputs are as they were; the heads are known to the caller
    reverseList(b);
    return sum;
}

// ---------------------------------------------------------------- 3. Copy a list with random pointers

struct RNode {
    int val;
    RNode* next;
    RNode* random;  // any node of the same list, or nullptr
    explicit RNode(int v = 0) : val(v), next(nullptr), random(nullptr) {}
};

// A hash map from every old node to its copy. O(n) time, O(n) extra memory.
RNode* copyRandomMap(RNode* head) {
    std::unordered_map<const RNode*, RNode*> copy;
    for (RNode* p = head; p; p = p->next) copy[p] = new RNode(p->val);
    for (RNode* p = head; p; p = p->next) {
        copy[p]->next = copy[p->next];      // copy[nullptr] is created as nullptr, so a null needs no special case
        copy[p]->random = copy[p->random];
    }
    return copy[head];
}

// No map: weave each copy in after its original (A, A', B, B', ...), so that the copy of X is X->next. O(n) time,
// O(1) extra memory. The original list is the same when the function returns.
RNode* copyRandomWeave(RNode* head) {
    if (!head) return nullptr;
    for (RNode* p = head; p; p = p->next->next) {  // 1. insert a copy after every node
        RNode* c = new RNode(p->val);
        c->next = p->next;
        p->next = c;
    }
    for (RNode* p = head; p; p = p->next->next)  // 2. the copy's random is the copy of the original's random
        p->next->random = p->random ? p->random->next : nullptr;
    RNode* newHead = head->next;
    for (RNode* p = head; p; p = p->next) {  // 3. unweave: restore the original, link the copies together
        RNode* c = p->next;
        p->next = c->next;
        c->next = c->next ? c->next->next : nullptr;
    }
    return newHead;
}

// ---------------------------------------------------------------- 4. Flatten a multilevel list

struct MNode {
    int val;
    MNode* prev;
    MNode* next;
    MNode* child;  // the head of another doubly linked list, or nullptr
    explicit MNode(int v = 0) : val(v), prev(nullptr), next(nullptr), child(nullptr) {}
};

// Walk the list; at a node with a child, splice the whole child list in between the node and its next node and keep
// walking: the next node visited is the child's first node. O(n) time, O(1) extra memory, no recursion.
MNode* flatten(MNode* head) {
    for (MNode* cur = head; cur; cur = cur->next) {
        if (!cur->child) continue;
        MNode* child = cur->child;
        MNode* tail = child;
        while (tail->next) tail = tail->next;  // the last node of the child list
        tail->next = cur->next;
        if (cur->next) cur->next->prev = tail;
        cur->next = child;
        child->prev = cur;
        cur->child = nullptr;
    }
    return head;
}

// The same by recursion: flatten the list that starts at `head` and return its last node. Depth = nesting depth.
static MNode* flattenToTail(MNode* head) {
    MNode* last = head;
    for (MNode* cur = head; cur;) {
        MNode* next = cur->next;
        last = cur;
        if (cur->child) {
            MNode* childTail = flattenToTail(cur->child);
            cur->next = cur->child;
            cur->child->prev = cur;
            childTail->next = next;
            if (next) next->prev = childTail;
            cur->child = nullptr;
            last = childTail;
        }
        cur = next;
    }
    return last;
}

MNode* flattenRec(MNode* head) {
    if (head) flattenToTail(head);
    return head;
}

// ---------------------------------------------------------------- 5. Rotate a list

// Move the last k nodes to the front. Join the tail to the head (a ring), walk to the new tail, cut the ring there.
ListNode* rotateRight(ListNode* head, int k) {
    if (!head || !head->next || k == 0) return head;
    int n = 1;
    ListNode* tail = head;
    while (tail->next) {
        tail = tail->next;
        ++n;
    }
    k %= n;
    if (k == 0) return head;
    tail->next = head;  // close the ring
    ListNode* newTail = head;
    for (int i = 0; i < n - k - 1; ++i) newTail = newTail->next;
    ListNode* newHead = newTail->next;
    newTail->next = nullptr;  // open the ring
    return newHead;
}

// ---------------------------------------------------------------- 6. LRU cache

// A doubly linked list kept in order of use (front = most recent, back = least recent) and a hash map from a key to
// its node. get and put are O(1) on average. A node stores its key: to evict the back node we must erase it from the map.
class LRUCache {
    struct Node {
        int key, value;
        Node *prev, *next;
    };
    int capacity_;
    Node head_{};  // the sentinel of a ring: head_.next is the most recently used node, head_.prev the least
    std::unordered_map<int, Node*> index_;

    void unlink(Node* x) {
        x->prev->next = x->next;
        x->next->prev = x->prev;
    }
    void pushFront(Node* x) {
        x->prev = &head_;
        x->next = head_.next;
        head_.next->prev = x;
        head_.next = x;
    }

public:
    explicit LRUCache(int capacity) : capacity_(capacity) { head_.prev = head_.next = &head_; }
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;
    ~LRUCache() {
        for (Node* x = head_.next; x != &head_;) {
            Node* next = x->next;
            delete x;
            x = next;
        }
    }

    int get(int key) {
        auto it = index_.find(key);
        if (it == index_.end()) return -1;
        Node* x = it->second;
        unlink(x);
        pushFront(x);  // a hit makes it the most recently used
        return x->value;
    }

    void put(int key, int value) {
        if (capacity_ <= 0) return;
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->value = value;
            unlink(it->second);
            pushFront(it->second);
            return;
        }
        if ((int)index_.size() == capacity_) {
            Node* oldest = head_.prev;
            index_.erase(oldest->key);
            unlink(oldest);
            delete oldest;
        }
        Node* x = new Node{key, value, nullptr, nullptr};
        pushFront(x);
        index_[key] = x;
    }

    int size() const { return (int)index_.size(); }
};

// ---------------------------------------------------------------- 7. LFU cache

// A list of entries for every use count (front = most recent), a hash map from a key to its entry, and the smallest
// use count in the cache. The entry to evict is the back of the list of the smallest count: the least recently used
// among the least frequently used. get and put are O(1) on average.
class LFUCache {
    struct Entry {
        int key, value, freq;
    };
    using List = std::list<Entry>;
    int capacity_;
    int minFreq_ = 0;
    std::unordered_map<int, List::iterator> index_;
    std::unordered_map<int, List> byFreq_;

    // One more use of the entry: splice it to the front of the list of the next count (no allocation, and the
    // iterator in index_ stays valid).
    void touch(List::iterator it) {
        List& from = byFreq_[it->freq];
        List& to = byFreq_[it->freq + 1];
        to.splice(to.begin(), from, it);
        if (from.empty()) {
            if (minFreq_ == it->freq) ++minFreq_;
            byFreq_.erase(it->freq);
        }
        ++it->freq;
    }

public:
    explicit LFUCache(int capacity) : capacity_(capacity) {}

    int get(int key) {
        auto f = index_.find(key);
        if (f == index_.end()) return -1;
        touch(f->second);
        return f->second->value;
    }

    void put(int key, int value) {
        if (capacity_ <= 0) return;
        auto f = index_.find(key);
        if (f != index_.end()) {
            f->second->value = value;
            touch(f->second);
            return;
        }
        if ((int)index_.size() == capacity_) {
            List& victims = byFreq_[minFreq_];
            index_.erase(victims.back().key);
            victims.pop_back();
            if (victims.empty()) byFreq_.erase(minFreq_);
        }
        List& ones = byFreq_[1];
        ones.push_front({key, value, 1});
        index_[key] = ones.begin();
        minFreq_ = 1;  // a new entry has count 1, the smallest possible
    }

    int size() const { return (int)index_.size(); }
    int buckets() const { return (int)byFreq_.size(); }  // how many use-count lists exist
};

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

static Vec toVec(const ListNode* h) {
    Vec out;
    for (; h && out.size() < 100000; h = h->next) out.push_back(h->val);
    return out;
}

static std::vector<ListNode*> nodesOf(ListNode* h) {
    std::vector<ListNode*> out;
    for (; h && out.size() < 100000; h = h->next) out.push_back(h);
    return out;
}

static void freeList(ListNode* h) {
    while (h) {
        ListNode* n = h->next;
        delete h;
        h = n;
    }
}

// ---- 1. palindrome

static void testPalindrome() {
    // every list over {0, 1, 2} of length 0 to 9
    for (int len = 0; len <= 9; ++len) {
        int total = 1;
        for (int i = 0; i < len; ++i) total *= 3;
        for (int code = 0; code < total; ++code) {
            Vec v(len);
            for (int i = 0, c = code; i < len; ++i, c /= 3) v[i] = c % 3;
            bool expected = std::equal(v.begin(), v.end(), v.rbegin());
            ListNode* head = build(v);
            std::vector<ListNode*> before = nodesOf(head);
            CHECK(isPalindrome(head) == expected);
            CHECK(nodesOf(head) == before);  // the same nodes in the same order
            CHECK(toVec(head) == v);         // and the same values
            CHECK(isPalindromeRec(head) == expected);
            CHECK(nodesOf(head) == before);
            freeList(head);
        }
    }
    // the halves: ceil(n / 2) nodes in the first one
    for (int n = 1; n <= 8; ++n) {
        Vec v(n);
        std::iota(v.begin(), v.end(), 1);
        ListNode* head = build(v);
        CHECK(endOfFirstHalf(head)->val == (n + 1) / 2);
        freeList(head);
    }
    // long lists: a palindrome, and the same list with one value changed in the second half
    for (int n : {1000, 1001}) {
        Vec v(n);
        for (int i = 0; i < n; ++i) v[i] = std::min(i, n - 1 - i) % 7;
        ListNode* head = build(v);
        CHECK(isPalindrome(head));
        CHECK(isPalindromeRec(head));
        freeList(head);
        v[n - 3] += 1;
        head = build(v);
        CHECK(!isPalindrome(head));
        CHECK(!isPalindromeRec(head));
        freeList(head);
    }
}

// ---- 2. add two numbers

// The decimal digits of x, most significant first ("0" for 0).
static Vec digitsOf(unsigned long long x) {
    Vec d;
    do {
        d.push_back((int)(x % 10));
        x /= 10;
    } while (x);
    std::reverse(d.begin(), d.end());
    return d;
}

static ListNode* buildReverse(const Vec& msdFirst) { return build(Vec(msdFirst.rbegin(), msdFirst.rend())); }

static void checkAdd(unsigned long long x, unsigned long long y) {
    Vec dx = digitsOf(x), dy = digitsOf(y), expected = digitsOf(x + y);
    Vec expectedRev(expected.rbegin(), expected.rend());

    // least significant digit first (LeetCode 2)
    ListNode* a = buildReverse(dx);
    ListNode* b = buildReverse(dy);
    ListNode* s = addTwoNumbers(a, b);
    CHECK(toVec(s) == expectedRev);
    CHECK(toVec(a) == Vec(dx.rbegin(), dx.rend()));  // the inputs are not changed
    CHECK(toVec(b) == Vec(dy.rbegin(), dy.rend()));
    freeList(s);
    freeList(a);
    freeList(b);

    // most significant digit first (LeetCode 445), with stacks and by reversing
    a = build(dx);
    b = build(dy);
    s = addForward(a, b);
    CHECK(toVec(s) == expected);
    CHECK(toVec(a) == dx && toVec(b) == dy);
    freeList(s);
    ListNode* ha = a;
    ListNode* hb = b;
    s = addForwardByReversing(ha, hb);
    CHECK(toVec(s) == expected);
    CHECK(toVec(a) == dx && toVec(b) == dy);  // the heads did not move: the nodes are in the original order again
    freeList(s);
    freeList(a);
    freeList(b);
}

static void testAdd() {
    for (unsigned long long x = 0; x < 300; ++x)
        for (unsigned long long y = 0; y < 300; ++y) checkAdd(x, y);
    std::mt19937_64 rng(5);
    for (int t = 0; t < 4000; ++t) {
        int lx = 1 + (int)(rng() % 18), ly = 1 + (int)(rng() % 18);
        unsigned long long px = 1, py = 1;
        for (int i = 1; i < lx; ++i) px *= 10;
        for (int i = 1; i < ly; ++i) py *= 10;
        checkAdd(px + rng() % (9 * px), py + rng() % (9 * py));  // lx and ly digits
    }
    // the carry runs through the whole list: 999...9 + 1
    Vec nines(60, 9);
    ListNode* a = build(nines);
    ListNode* b = build({1});
    ListNode* s = addForward(a, b);
    Vec expected(61, 0);
    expected[0] = 1;
    CHECK(toVec(s) == expected);
    freeList(s);
    freeList(a);
    freeList(b);
    // an empty list is the number "nothing"; two of them give an empty list
    CHECK(addTwoNumbers(nullptr, nullptr) == nullptr);
    CHECK(addForward(nullptr, nullptr) == nullptr);
}

// ---- 3. copy with random pointers

// Nodes 0..n-1 with next to the following node and random[i] as the index of the random target (-1 for nullptr).
static RNode* buildR(const Vec& vals, const Vec& random, std::vector<RNode*>& nodes) {
    nodes.clear();
    for (int v : vals) nodes.push_back(new RNode(v));
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i + 1 < nodes.size()) nodes[i]->next = nodes[i + 1];
        nodes[i]->random = random[i] < 0 ? nullptr : nodes[random[i]];
    }
    return nodes.empty() ? nullptr : nodes[0];
}

static void freeR(RNode* h) {
    while (h) {
        RNode* n = h->next;
        delete h;
        h = n;
    }
}

// The list as (val, index of random) pairs; -1 for a null random. Fails if a random points outside the list.
static bool describe(RNode* head, std::vector<std::pair<int, int>>& out) {
    std::unordered_map<const RNode*, int> index;
    int n = 0;
    for (RNode* p = head; p; p = p->next) index[p] = n++;
    out.clear();
    for (RNode* p = head; p; p = p->next) {
        if (!p->random) {
            out.push_back({p->val, -1});
        } else {
            auto it = index.find(p->random);
            if (it == index.end()) return false;
            out.push_back({p->val, it->second});
        }
    }
    return true;
}

static void testCopyRandom() {
    std::mt19937 rng(7);
    for (int t = 0; t < 3000; ++t) {
        int n = (int)(rng() % 13);
        Vec vals(n), random(n);
        for (int i = 0; i < n; ++i) {
            vals[i] = (int)(rng() % 100);
            random[i] = (int)(rng() % (n + 1)) - 1;  // -1 .. n-1
        }
        for (int variant = 0; variant < 2; ++variant) {
            std::vector<RNode*> nodes;
            RNode* head = buildR(vals, random, nodes);
            std::vector<std::pair<int, int>> shape;
            CHECK(describe(head, shape));
            RNode* copy = variant == 0 ? copyRandomMap(head) : copyRandomWeave(head);
            // the copy has the same shape, and the original is unchanged
            std::vector<std::pair<int, int>> copyShape, afterShape;
            CHECK(describe(copy, copyShape));  // every random of the copy points inside the copy
            CHECK(copyShape == shape);
            CHECK(describe(head, afterShape) && afterShape == shape);
            std::vector<RNode*> now;
            for (RNode* p = head; p; p = p->next) now.push_back(p);
            CHECK(now == nodes);
            // and shares no node with the original
            std::unordered_set<const RNode*> old(nodes.begin(), nodes.end());
            bool shared = false;
            for (RNode* p = copy; p; p = p->next) shared = shared || old.count(p) || (p->random && old.count(p->random));
            CHECK(!shared);
            freeR(copy);
            freeR(head);
        }
    }
    CHECK(copyRandomMap(nullptr) == nullptr);
    CHECK(copyRandomWeave(nullptr) == nullptr);
}

// ---- 4. flatten

// A random multilevel list with `budget` nodes in all; `pre` gets the values in the order the flat list must have
// (a node, then everything below its child, then the next node).
static MNode* genLevel(std::mt19937& rng, int& budget, int& nextVal, Vec& pre, int depth) {
    int len = 1 + (int)(rng() % 4);
    MNode *head = nullptr, *tail = nullptr;
    for (int i = 0; i < len && budget > 0; ++i) {
        --budget;
        MNode* n = new MNode(nextVal++);
        pre.push_back(n->val);
        n->prev = tail;
        if (tail) tail->next = n;
        else head = n;
        tail = n;
        if (budget > 0 && depth < 6 && rng() % 3 == 0) n->child = genLevel(rng, budget, nextVal, pre, depth + 1);
    }
    return head;
}

static void checkFlat(MNode* head, const Vec& pre) {
    Vec vals;
    bool linksOk = head == nullptr || head->prev == nullptr;
    bool noChild = true;
    for (MNode* p = head; p; p = p->next) {
        vals.push_back(p->val);
        noChild = noChild && p->child == nullptr;
        if (p->next) linksOk = linksOk && p->next->prev == p;
    }
    CHECK(vals == pre);
    CHECK(linksOk);
    CHECK(noChild);
}

static void freeFlat(MNode* h) {
    while (h) {
        MNode* n = h->next;
        delete h;
        h = n;
    }
}

static void testFlatten() {
    for (int seed = 0; seed < 3000; ++seed) {
        int total = 1 + seed % 40;
        for (int variant = 0; variant < 2; ++variant) {
            std::mt19937 rng(seed);
            int budget = total, nextVal = 1;
            Vec pre;
            MNode* head = genLevel(rng, budget, nextVal, pre, 0);
            MNode* flat = variant == 0 ? flatten(head) : flattenRec(head);
            CHECK(flat == head);
            checkFlat(flat, pre);
            freeFlat(flat);
        }
    }
    CHECK(flatten(nullptr) == nullptr);
    CHECK(flattenRec(nullptr) == nullptr);
    // a chain of children, 3000 levels deep, one node per level
    MNode* top = new MNode(0);
    MNode* cur = top;
    Vec pre{0};
    for (int i = 1; i < 3000; ++i) {
        cur->child = new MNode(i);
        cur = cur->child;
        pre.push_back(i);
    }
    top = flatten(top);
    checkFlat(top, pre);
    freeFlat(top);
}

// ---- 5. rotate

static void testRotate() {
    for (int n = 0; n <= 8; ++n) {
        for (int k : {0, 1, 2, 3, 5, 7, 8, 9, 15, 16, 17, 100, 2000000000, 2147483647}) {
            Vec v(n);
            std::iota(v.begin(), v.end(), 1);
            Vec expected = v;
            if (n > 0) std::rotate(expected.begin(), expected.end() - k % n, expected.end());
            ListNode* head = rotateRight(build(v), k);
            CHECK(toVec(head) == expected);
            CHECK((int)nodesOf(head).size() == n);  // no ring is left behind
            freeList(head);
        }
    }
}

// ---- 6. LRU

// The slow model: a vector of (key, value), most recently used first.
struct LruModel {
    int cap;
    std::vector<std::pair<int, int>> items;
    int get(int key) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].first == key) {
                std::pair<int, int> kv = items[i];
                items.erase(items.begin() + (long)i);
                items.insert(items.begin(), kv);
                return kv.second;
            }
        }
        return -1;
    }
    void put(int key, int value) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (items[i].first == key) {
                items.erase(items.begin() + (long)i);
                break;
            }
        }
        if ((int)items.size() == cap) items.pop_back();
        items.insert(items.begin(), {key, value});
    }
};

static void testLru() {
    {  // a small trace
        LRUCache c(2);
        c.put(1, 10);
        c.put(2, 20);
        CHECK(c.get(1) == 10);  // order, most recent first: 1 2
        c.put(3, 30);           // full: evicts 2
        CHECK(c.get(2) == -1);
        c.put(1, 11);  // an update is a use
        c.put(4, 40);  // evicts 3
        CHECK(c.get(3) == -1);
        CHECK(c.get(1) == 11);
        CHECK(c.get(4) == 40);
        CHECK(c.size() == 2);
    }
    {  // capacity 1 and capacity 0
        LRUCache c(1);
        c.put(1, 1);
        c.put(2, 2);
        CHECK(c.get(1) == -1 && c.get(2) == 2);
        LRUCache z(0);
        z.put(1, 1);
        CHECK(z.get(1) == -1 && z.size() == 0);
    }
    std::mt19937 rng(11);
    for (int t = 0; t < 600; ++t) {
        int cap = 1 + (int)(rng() % 5);
        int keys = 1 + (int)(rng() % 9);
        LRUCache c(cap);
        LruModel m{cap, {}};
        for (int step = 0; step < 200; ++step) {
            int key = (int)(rng() % keys);
            if (rng() % 2) {
                int value = (int)(rng() % 1000);
                c.put(key, value);
                m.put(key, value);
            } else {
                CHECK(c.get(key) == m.get(key));
            }
            CHECK(c.size() == (int)m.items.size());
        }
    }
}

// ---- 7. LFU

// The slow model: every entry has a use count and the time of its last use; evict the smallest (count, time).
struct LfuModel {
    struct E {
        int key, value, freq;
        long stamp;
    };
    int cap;
    long clock = 0;
    std::vector<E> items;
    E* find(int key) {
        for (E& e : items)
            if (e.key == key) return &e;
        return nullptr;
    }
    int get(int key) {
        E* e = find(key);
        if (!e) return -1;
        ++e->freq;
        e->stamp = ++clock;
        return e->value;
    }
    void put(int key, int value) {
        if (cap <= 0) return;
        if (E* e = find(key)) {
            e->value = value;
            ++e->freq;
            e->stamp = ++clock;
            return;
        }
        if ((int)items.size() == cap) {
            auto victim = std::min_element(items.begin(), items.end(), [](const E& a, const E& b) {
                return a.freq != b.freq ? a.freq < b.freq : a.stamp < b.stamp;
            });
            items.erase(victim);
        }
        items.push_back({key, value, 1, ++clock});
    }
};

static void testLfu() {
    {  // a small trace
        LFUCache c(2);
        c.put(1, 10);
        c.put(2, 20);
        CHECK(c.get(1) == 10);  // counts: key 1 = 2, key 2 = 1
        c.put(3, 30);           // full: evicts key 2 (count 1)
        CHECK(c.get(2) == -1);
        CHECK(c.get(3) == 30);  // counts: key 1 = 2, key 3 = 2 (key 3 used later)
        c.put(4, 40);           // a tie at count 2: evicts key 1, the less recently used
        CHECK(c.get(1) == -1);
        CHECK(c.get(3) == 30);
        CHECK(c.get(4) == 40);
        CHECK(c.size() == 2);
    }
    {
        LFUCache c(1);
        c.put(1, 1);
        c.put(2, 2);
        CHECK(c.get(1) == -1 && c.get(2) == 2);
        LFUCache z(0);
        z.put(1, 1);
        CHECK(z.get(1) == -1 && z.size() == 0);
    }
    std::mt19937 rng(13);
    for (int t = 0; t < 800; ++t) {
        int cap = 1 + (int)(rng() % 5);
        int keys = 1 + (int)(rng() % 9);
        LFUCache c(cap);
        LfuModel m{cap, 0, {}};
        for (int step = 0; step < 250; ++step) {
            int key = (int)(rng() % keys);
            if (rng() % 2) {
                int value = (int)(rng() % 1000);
                c.put(key, value);
                m.put(key, value);
            } else {
                CHECK(c.get(key) == m.get(key));
            }
            CHECK(c.size() == (int)m.items.size());
            CHECK(c.buckets() <= c.size());  // empty lists are erased: at most one list per entry
        }
    }
}

int main() {
    testPalindrome();
    testAdd();
    testCopyRandom();
    testFlatten();
    testRotate();
    testLru();
    testLfu();
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
