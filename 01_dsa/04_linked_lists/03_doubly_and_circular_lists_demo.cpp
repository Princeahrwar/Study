// Tutorial: doubly and circular linked lists. The code shown in the tutorial, checked against std::list, std::deque and
// brute-force simulations on every small case.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 03_doubly_and_circular_lists_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <cstdio>
#include <deque>
#include <list>
#include <random>
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

// ---------------------------------------------------------------- 1. Doubly linked list with a sentinel

struct DNode {
    int val;
    DNode* prev;
    DNode* next;
};

// Without a sentinel the list needs head and tail pointers, and every edit has to patch them at the two ends.
void plainErase(DNode*& head, DNode*& tail, DNode* x) {
    if (x->prev) x->prev->next = x->next; else head = x->next;  // x was the first node
    if (x->next) x->next->prev = x->prev; else tail = x->prev;  // x was the last node
    delete x;
}

// Insert v before pos; pos == nullptr means "at the end".
DNode* plainInsertBefore(DNode*& head, DNode*& tail, DNode* pos, int v) {
    DNode* x = new DNode{v, pos ? pos->prev : tail, pos};
    if (x->prev) x->prev->next = x; else head = x;
    if (pos) pos->prev = x; else tail = x;
    return x;
}

// A ring of nodes with one extra node, the sentinel, that holds no data. begin() is the node after the sentinel,
// end() is the sentinel itself, and an empty list is a sentinel that points to itself.
class DList {
public:
    DList() { s_.prev = s_.next = &s_; }
    DList(const DList&) = delete;
    DList& operator=(const DList&) = delete;
    DList& operator=(DList&&) = delete;
    DList(DList&& o) noexcept {
        s_.prev = s_.next = &s_;
        if (!o.empty()) {
            s_.next = o.s_.next;
            s_.prev = o.s_.prev;
            s_.next->prev = &s_;  // the first and last nodes still point at o's sentinel
            s_.prev->next = &s_;  // so point them at ours
            n_ = o.n_;
            o.s_.prev = o.s_.next = &o.s_;
            o.n_ = 0;
        }
    }
    ~DList() { clear(); }

    DNode* begin() { return s_.next; }
    DNode* end() { return &s_; }
    bool empty() const { return s_.next == &s_; }
    std::size_t size() const { return n_; }

    // Insert v before pos (pos may be end()). Returns the new node. Four pointer writes, no branches.
    DNode* insertBefore(DNode* pos, int v) {
        DNode* x = new DNode{v, pos->prev, pos};
        pos->prev->next = x;
        pos->prev = x;
        ++n_;
        return x;
    }

    // Remove the data node x (never end()). Returns the node that followed it.
    DNode* erase(DNode* x) {
        DNode* after = x->next;
        x->prev->next = x->next;
        x->next->prev = x->prev;
        delete x;
        --n_;
        return after;
    }

    void pushFront(int v) { insertBefore(begin(), v); }
    void pushBack(int v) { insertBefore(end(), v); }
    void popFront() { erase(begin()); }  // the list must not be empty
    void popBack() { erase(s_.prev); }   // the list must not be empty

    // Move the data node x to the front without allocating: unlink it, then link it after the sentinel.
    void moveToFront(DNode* x) {
        x->prev->next = x->next;
        x->next->prev = x->prev;
        x->next = s_.next;
        x->prev = &s_;
        s_.next->prev = x;
        s_.next = x;
    }

    // Reverse in place by swapping prev and next in every node, the sentinel included.
    void reverse() {
        DNode* p = &s_;
        do {
            std::swap(p->prev, p->next);
            p = p->prev;  // the old next
        } while (p != &s_);
    }

    void clear() {
        while (!empty()) popFront();
    }

private:
    DNode s_{0, nullptr, nullptr};
    std::size_t n_ = 0;
};

// ---------------------------------------------------------------- 2. Circular singly linked list

struct CNode {
    int val;
    CNode* next;
};

// A circular list is held as a pointer to its last node, tail. The first node is tail->next. nullptr is the empty list.
// Every function returns the new tail.
CNode* cPushFront(CNode* tail, int v) {
    CNode* x = new CNode{v, nullptr};
    if (!tail) {
        x->next = x;  // a single node points to itself
        return x;
    }
    x->next = tail->next;
    tail->next = x;
    return tail;
}

CNode* cPushBack(CNode* tail, int v) {
    tail = cPushFront(tail, v);
    return tail->next;  // the new node is now the first one: let it be the last one
}

CNode* cPopFront(CNode* tail) {
    if (!tail) return nullptr;
    CNode* first = tail->next;
    if (first == tail) {
        delete first;
        return nullptr;
    }
    tail->next = first->next;
    delete first;
    return tail;
}

// The first node becomes the last one: one pointer move, no node is touched.
CNode* cRotate(CNode* tail) { return tail ? tail->next : nullptr; }

// Visit every value once, starting at the first node. A do-while: the first test comes after the first visit.
template <class F>
void cForEach(CNode* tail, F visit) {
    if (!tail) return;
    CNode* p = tail->next;
    do {
        visit(p->val);
        p = p->next;
    } while (p != tail->next);
}

void cFree(CNode* tail) {
    while (tail) tail = cPopFront(tail);
}

// Insert v into a sorted circular list (LeetCode 708). head may be any node of the ring. Returns head, or the new node
// when the list was empty.
CNode* insertSortedCircular(CNode* head, int v) {
    CNode* x = new CNode{v, nullptr};
    if (!head) {
        x->next = x;
        return x;
    }
    CNode* p = head;
    do {
        CNode* q = p->next;
        bool between = p->val <= v && v <= q->val;
        bool atSeam = p->val > q->val && (v >= p->val || v <= q->val);  // p is the largest, q the smallest
        if (between || atSeam) {
            x->next = q;
            p->next = x;
            return head;
        }
        p = q;
    } while (p != head);
    x->next = head->next;  // no slot found: all values are equal, so any place is correct
    head->next = x;
    return head;
}

// ---------------------------------------------------------------- 3. Josephus

// n people stand in a circle, numbered 1..n. Count k people starting from person 1, remove the k-th, and start the next
// count at the person after it. Returns the number of the last person.
// J(1) = 0 and J(m) = (J(m - 1) + k) mod m, counting from 0.
int josephus(int n, int k) {
    int r = 0;
    for (int m = 2; m <= n; ++m) r = (r + k) % m;
    return r + 1;
}

// For k = 2: write n = 2^a + l with 0 <= l < 2^a. The survivor is 2l + 1.
int josephusK2(int n) {
    int p = 1;
    while (p * 2 <= n) p *= 2;
    return 2 * (n - p) + 1;
}

// The order in which people leave, by playing the game on a circular list. O(n * k).
Vec josephusOrder(int n, int k) {
    CNode* tail = nullptr;
    for (int i = 1; i <= n; ++i) tail = cPushBack(tail, i);
    Vec order;
    CNode* before = tail;  // the node just before the one the count starts at
    for (int left = n; left > 0; --left) {
        for (int step = 1; step < k; ++step) before = before->next;
        CNode* gone = before->next;
        order.push_back(gone->val);
        if (gone == before) {  // the last person: the ring is empty after this
            delete gone;
            break;
        }
        before->next = gone->next;
        delete gone;
    }
    return order;
}

// ---- for the tests and the experiments

// The list as a vector, walking forward from the sentinel; also checks the links on the way.
static Vec forward(DList& l) {
    Vec out;
    DNode* p = l.begin();
    DNode* before = l.end();
    while (p != l.end()) {
        CHECK(p->prev == before);
        CHECK(before->next == p);
        out.push_back(p->val);
        before = p;
        p = p->next;
        if (out.size() > 10000) break;  // a broken ring would loop forever
    }
    CHECK(l.end()->prev == before);
    return out;
}

static Vec backward(DList& l) {
    Vec out;
    for (DNode* p = l.end()->prev; p != l.end() && out.size() <= 10000; p = p->prev) out.push_back(p->val);
    std::reverse(out.begin(), out.end());
    return out;
}

static DNode* nodeAt(DList& l, std::size_t i) {
    DNode* p = l.begin();
    while (i--) p = p->next;
    return p;
}

static void testPlain() {
    for (int len = 0; len <= 5; ++len) {
        for (int pos = 0; pos <= len; ++pos) {  // insert before position pos (pos == len: at the end)
            DNode *head = nullptr, *tail = nullptr;
            Vec model;
            for (int i = 0; i < len; ++i) {
                plainInsertBefore(head, tail, nullptr, 10 + i);
                model.push_back(10 + i);
            }
            DNode* at = head;
            for (int i = 0; i < pos; ++i) at = at->next;
            plainInsertBefore(head, tail, at, 99);
            model.insert(model.begin() + pos, 99);
            Vec fwd, bwd;
            for (DNode* p = head; p; p = p->next) fwd.push_back(p->val);
            for (DNode* p = tail; p; p = p->prev) bwd.push_back(p->val);
            std::reverse(bwd.begin(), bwd.end());
            CHECK(fwd == model);
            CHECK(bwd == model);
            CHECK((head->prev == nullptr) && (tail->next == nullptr));
            while (head) plainErase(head, tail, head);
            CHECK(tail == nullptr);
        }
        for (int pos = 0; pos < len; ++pos) {  // erase the node at position pos
            DNode *head = nullptr, *tail = nullptr;
            Vec model;
            for (int i = 0; i < len; ++i) {
                plainInsertBefore(head, tail, nullptr, 10 + i);
                model.push_back(10 + i);
            }
            DNode* at = head;
            for (int i = 0; i < pos; ++i) at = at->next;
            plainErase(head, tail, at);
            model.erase(model.begin() + pos);
            Vec fwd, bwd;
            for (DNode* p = head; p; p = p->next) fwd.push_back(p->val);
            for (DNode* p = tail; p; p = p->prev) bwd.push_back(p->val);
            std::reverse(bwd.begin(), bwd.end());
            CHECK(fwd == model);
            CHECK(bwd == model);
            CHECK((head == nullptr) == (tail == nullptr));
            while (head) plainErase(head, tail, head);
        }
    }
}

static void testDList() {
    // every insert position and every erase position on every small list
    for (int len = 0; len <= 5; ++len) {
        for (int pos = 0; pos <= len; ++pos) {
            DList l;
            Vec model;
            for (int i = 0; i < len; ++i) {
                l.pushBack(10 + i);
                model.push_back(10 + i);
            }
            l.insertBefore(nodeAt(l, pos), 99);
            model.insert(model.begin() + pos, 99);
            CHECK(forward(l) == model);
            CHECK(backward(l) == model);
            CHECK(l.size() == model.size());
        }
        for (int pos = 0; pos < len; ++pos) {
            DList l;
            Vec model;
            for (int i = 0; i < len; ++i) {
                l.pushBack(10 + i);
                model.push_back(10 + i);
            }
            DNode* after = l.erase(nodeAt(l, pos));
            model.erase(model.begin() + pos);
            CHECK(forward(l) == model);
            CHECK(backward(l) == model);
            CHECK(after == nodeAt(l, pos));
        }
    }
    // random operations against std::list
    std::mt19937 rng(7);
    for (int round = 0; round < 300; ++round) {
        DList l;
        std::list<int> model;
        for (int op = 0; op < 120; ++op) {
            int kind = (int)(rng() % 8);
            int v = (int)(rng() % 1000);
            std::size_t n = model.size();
            if (kind == 0) { l.pushFront(v); model.push_front(v); }
            else if (kind == 1) { l.pushBack(v); model.push_back(v); }
            else if (kind == 2 && n) { l.popFront(); model.pop_front(); }
            else if (kind == 3 && n) { l.popBack(); model.pop_back(); }
            else if (kind == 4) {
                std::size_t i = rng() % (n + 1);
                l.insertBefore(nodeAt(l, i), v);
                model.insert(std::next(model.begin(), (long)i), v);
            } else if (kind == 5 && n) {
                std::size_t i = rng() % n;
                l.erase(nodeAt(l, i));
                model.erase(std::next(model.begin(), (long)i));
            } else if (kind == 6 && n) {
                std::size_t i = rng() % n;
                l.moveToFront(nodeAt(l, i));
                model.splice(model.begin(), model, std::next(model.begin(), (long)i));
            } else if (kind == 7) {
                l.reverse();
                model.reverse();
            }
            Vec want(model.begin(), model.end());
            CHECK(forward(l) == want);
            CHECK(backward(l) == want);
            CHECK(l.size() == want.size());
            CHECK(l.empty() == want.empty());
        }
        // moving the list must repair the links that pointed at the old sentinel
        Vec want(model.begin(), model.end());
        DList moved(std::move(l));
        CHECK(forward(moved) == want);
        CHECK(backward(moved) == want);
        CHECK(l.empty() && l.size() == 0 && forward(l).empty());
        CHECK(moved.size() == want.size());
        moved.pushBack(5);
        want.push_back(5);
        CHECK(forward(moved) == want);
    }
}

static Vec ring(CNode* tail, std::size_t count) {
    Vec out;
    cForEach(tail, [&](int v) { if (out.size() <= count) out.push_back(v); });
    return out;  // a ring that does not close would give more than `count` values
}

static void testCircular() {
    std::mt19937 rng(11);
    for (int round = 0; round < 300; ++round) {
        CNode* tail = nullptr;
        std::deque<int> model;
        for (int op = 0; op < 100; ++op) {
            int kind = (int)(rng() % 5);
            int v = (int)(rng() % 1000);
            if (kind == 0) { tail = cPushFront(tail, v); model.push_front(v); }
            else if (kind == 1) { tail = cPushBack(tail, v); model.push_back(v); }
            else if (kind == 2) { tail = cPopFront(tail); if (!model.empty()) model.pop_front(); }
            else if (kind == 3) {
                tail = cRotate(tail);
                if (!model.empty()) { model.push_back(model.front()); model.pop_front(); }
            } else if (kind == 4 && !model.empty()) {
                CHECK(tail->next->val == model.front());
                CHECK(tail->val == model.back());
            }
            CHECK((tail == nullptr) == model.empty());
            CHECK(ring(tail, model.size()) == Vec(model.begin(), model.end()));
        }
        cFree(tail);
    }
    // sorted insert: every sorted ring of up to 5 values from 0..3, every node as head, every value from -1 to 4
    for (int len = 0; len <= 5; ++len) {
        Vec sorted(len, 0);
        std::vector<Vec> all;
        std::vector<int> idx(len, 0);
        // enumerate non-decreasing sequences
        auto rec = [&](auto&& self, int i, int lo) -> void {
            if (i == len) { all.push_back(sorted); return; }
            for (int x = lo; x <= 3; ++x) { sorted[i] = x; self(self, i + 1, x); }
        };
        rec(rec, 0, 0);
        for (const Vec& s : all) {
            for (int headIdx = 0; headIdx < std::max(len, 1); ++headIdx) {
                for (int v = -1; v <= 4; ++v) {
                    CNode* tail = nullptr;
                    for (int x : s) tail = cPushBack(tail, x);
                    CNode* head = tail ? tail->next : nullptr;
                    for (int i = 0; i < headIdx; ++i) head = head->next;
                    CNode* got = insertSortedCircular(head, v);
                    // read the ring once, starting from got; it must be a rotation of the sorted sequence
                    Vec read;
                    CNode* p = got;
                    do { read.push_back(p->val); p = p->next; } while (p != got && read.size() < 20);
                    Vec want = s;
                    want.insert(std::upper_bound(want.begin(), want.end(), v), v);
                    bool rotation = false;
                    for (std::size_t r = 0; r < read.size(); ++r) {
                        Vec rot(read.begin() + (long)r, read.end());
                        rot.insert(rot.end(), read.begin(), read.begin() + (long)r);
                        if (rot == want) rotation = true;
                    }
                    CHECK(read.size() == want.size());
                    CHECK(rotation);
                    // free the ring
                    CNode* last = got;
                    while (last->next != got) last = last->next;
                    cFree(last);
                }
            }
        }
    }
}

static int josephusBrute(int n, int k) {
    Vec people(n);
    for (int i = 0; i < n; ++i) people[i] = i + 1;
    std::size_t at = 0;
    while (people.size() > 1) {
        at = (at + (std::size_t)k - 1) % people.size();
        people.erase(people.begin() + (long)at);
        if (at == people.size()) at = 0;
    }
    return people[0];
}

static void testJosephus() {
    CHECK(josephus(5, 2) == 3);                                   // LeetCode 1823, example 1
    CHECK(josephus(6, 5) == 1);                                   // example 2
    CHECK(josephusOrder(6, 5) == (Vec{5, 4, 6, 2, 3, 1}));        // the removal order given in the statement, then the winner
    CHECK(josephusOrder(7, 3) == (Vec{3, 6, 2, 7, 5, 1, 4}));     // CSES Josephus Problem II, "7 2": skip 2, remove the next
    CHECK(josephusOrder(6, 2) == (Vec{2, 4, 6, 3, 1, 5}));        // CSES "6 1": skip 1, remove the next
    CHECK(josephusOrder(7, 2) == (Vec{2, 4, 6, 1, 5, 3, 7}));     // CSES Josephus Problem I, n = 7: every second child
    CHECK(josephusOrder(5, 2) == (Vec{2, 4, 1, 5, 3}));           // the order used in the tutorial
    for (int n = 1; n <= 40; ++n) {
        for (int k = 1; k <= 60; ++k) {
            Vec order = josephusOrder(n, k);
            CHECK((int)order.size() == n);
            CHECK(order.back() == josephus(n, k));
            CHECK(josephusBrute(n, k) == josephus(n, k));
        }
    }
    for (int n = 1; n <= 3000; ++n) CHECK(josephusK2(n) == josephus(n, 2));
    CHECK(josephus(41, 2) == 19);
}

int main() {
    testPlain();
    testDList();
    testCircular();
    testJosephus();
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
