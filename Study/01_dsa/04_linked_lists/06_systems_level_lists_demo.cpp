// Tutorial: lists inside real systems. The classes shown in the tutorial (an intrusive list, an hlist, a fixed-size
// pool with an in-place free list, an XOR list, an unrolled list) and the two experiments of section 3.
// The classes are checked against simple models (vectors, deques) on random operations.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 06_systems_level_lists_demo.cpp -o demo && ./demo
//   g++ -std=c++20 -O2 06_systems_level_lists_demo.cpp -o bench && ./bench bench      (the timings of section 3)
//
// Expected last line of the first command: "<N>/<N> checks passed".

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>
#include <list>
#include <random>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(...)                                                           \
    do {                                                                     \
        ++g_checks;                                                          \
        if (__VA_ARGS__) ++g_passed;                                         \
        else std::printf("FAILED line %d: %s\n", __LINE__, #__VA_ARGS__);    \
    } while (0)

// ---------------------------------------------------------------- 1. An intrusive list (the idea of the Linux list_head)

// The links live inside the object that is on the list. A ListHead alone holds no data.
struct ListHead {
    ListHead* next;
    ListHead* prev;
};

// Written into a removed node, so that a second removal or a stray read fails fast.
// (The kernel writes the values 0x100 and 0x122 for the same purpose.)
constexpr std::uintptr_t kPoisonNext = 0x100;
constexpr std::uintptr_t kPoisonPrev = 0x122;

inline void listInit(ListHead* head) {
    head->next = head;
    head->prev = head;
}

inline bool listEmpty(const ListHead* head) { return head->next == head; }

// Put 'node' between 'before' and 'after', which must be neighbours.
inline void linkBetween(ListHead* node, ListHead* before, ListHead* after) {
    after->prev = node;
    node->next = after;
    node->prev = before;
    before->next = node;
}

inline void listAddFront(ListHead* node, ListHead* head) { linkBetween(node, head, head->next); }
inline void listAddBack(ListHead* node, ListHead* head) { linkBetween(node, head->prev, head); }

// O(1). It needs only the node: no search and no pointer to the head.
inline void listRemove(ListHead* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = reinterpret_cast<ListHead*>(kPoisonNext);
    node->prev = reinterpret_cast<ListHead*>(kPoisonPrev);
}

// From a pointer to a member back to the object that contains it.
#define OWNER_OF(ptr, Type, member) \
    reinterpret_cast<Type*>(reinterpret_cast<char*>(ptr) - offsetof(Type, member))

// One object on two lists at the same time: it holds two ListHead members.
struct Job {
    int id;
    int owner;
    ListHead byArrival;  // this job's place in the global queue
    ListHead byOwner;    // this job's place in the list of its owner
};

// Take a job off both of its lists. It needs only the Job*.
void cancel(Job* job) {
    listRemove(&job->byArrival);
    listRemove(&job->byOwner);
}

// Cancel every job of 'owner' while walking the global queue.
// 'next' is saved before the body runs, because the body may remove the current node.
int cancelOwner(ListHead* queue, int owner) {
    int removed = 0;
    for (ListHead *p = queue->next, *next = p->next; p != queue; p = next, next = p->next) {
        Job* job = OWNER_OF(p, Job, byArrival);
        if (job->owner == owner) {
            cancel(job);
            ++removed;
        }
    }
    return removed;
}

// hlist: a head with ONE pointer, for the many buckets of a hash table. A node keeps 'pprev', the address of
// whatever points at it (the head's 'first' or the previous node's 'next'), so removal needs no head.
struct HNode {
    HNode* next;
    HNode** pprev;
};
struct HHead {
    HNode* first;
};

inline void hAddFront(HNode* n, HHead* h) {
    n->next = h->first;
    if (h->first) h->first->pprev = &n->next;
    h->first = n;
    n->pprev = &h->first;
}

inline void hRemove(HNode* n) {
    *n->pprev = n->next;
    if (n->next) n->next->pprev = n->pprev;
}

// ---------------------------------------------------------------- 2. A fixed-size pool with an in-place free list

// Count slots of SlotSize bytes each. A free slot stores the pointer to the next free slot in its own first bytes,
// so the free list costs no extra memory. allocate and deallocate are O(1) and never search.
template <std::size_t SlotSize, std::size_t Count>
class Pool {
    static_assert(SlotSize >= sizeof(void*), "a free slot must have room for the next pointer");
    union Slot {
        Slot* next;                     // while the slot is free
        unsigned char bytes[SlotSize];  // while it is in use
    };
    Slot slots_[Count];
    Slot* free_ = nullptr;
    std::size_t used_ = 0;

public:
    Pool() {
        for (std::size_t i = Count; i-- > 0;) {  // so that slot 0 is the first one handed out
            slots_[i].next = free_;
            free_ = &slots_[i];
        }
    }

    void* allocate() {
        if (!free_) return nullptr;  // the pool is empty
        Slot* slot = free_;
        free_ = slot->next;
        ++used_;
        return slot;
    }

    void deallocate(void* p) {
        Slot* slot = static_cast<Slot*>(p);
        slot->next = free_;
        free_ = slot;
        --used_;
    }

    std::size_t used() const { return used_; }

    // ---- for the tests and the experiments
    const void* base() const { return slots_; }
    static constexpr std::size_t slotBytes() { return sizeof(Slot); }
};

// ---------------------------------------------------------------- 4. An XOR linked list

// One link field instead of two: link = (address of the previous node) XOR (address of the next node),
// where a missing neighbour counts as address 0.
template <class T>
class XorList {
    struct Node {
        T value;
        std::uintptr_t link;
    };
    static std::uintptr_t bits(const Node* n) { return reinterpret_cast<std::uintptr_t>(n); }
    static Node* node(std::uintptr_t b) { return reinterpret_cast<Node*>(b); }

    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;

public:
    XorList() = default;
    XorList(const XorList&) = delete;
    XorList& operator=(const XorList&) = delete;
    ~XorList() {
        while (head_) popFront();
    }

    void pushBack(const T& v) {
        Node* n = new Node{v, bits(tail_)};  // previous = the old tail, next = none (0)
        if (tail_) tail_->link ^= bits(n);   // the old tail's next changes from none to n
        else head_ = n;
        tail_ = n;
        ++size_;
    }

    void popFront() {  // the list must not be empty
        Node* old = head_;
        Node* next = node(old->link);        // the previous is none (0), so link == next
        if (next) next->link ^= bits(old);   // next's previous changes from old to none
        else tail_ = nullptr;
        head_ = next;
        delete old;
        --size_;
    }

    // Swapping the two ends reverses the list, because the links read the same in both directions.
    void reverse() { std::swap(head_, tail_); }

    template <class F>
    void forEach(F f) const {
        const Node* prev = nullptr;
        const Node* cur = head_;
        while (cur) {
            f(cur->value);
            const Node* next = node(cur->link ^ bits(prev));  // the link XOR where we came from
            prev = cur;
            cur = next;
        }
    }

    std::size_t size() const { return size_; }
};

// ---------------------------------------------------------------- 5. An unrolled linked list

// Each node holds up to B elements in an array, so one pointer is followed per B elements.
// (T must be cheap to copy and default-construct; this is a teaching version.)
template <class T, unsigned B>
class UnrolledList {
    static_assert(B >= 2, "a node must hold at least two elements to be split");

public:
    struct Node {
        Node* next;
        unsigned count;
        T items[B];
    };

private:
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;

public:
    UnrolledList() = default;
    UnrolledList(const UnrolledList&) = delete;
    UnrolledList& operator=(const UnrolledList&) = delete;
    ~UnrolledList() {
        for (Node* n = head_; n;) {
            Node* next = n->next;
            delete n;
            n = next;
        }
    }

    std::size_t size() const { return size_; }

    void pushBack(const T& v) {
        if (!tail_ || tail_->count == B) {
            Node* n = new Node{nullptr, 0, {}};
            (tail_ ? tail_->next : head_) = n;
            tail_ = n;
        }
        tail_->items[tail_->count++] = v;
        ++size_;
    }

    // Skip whole nodes by their counts, then index inside one node: O(n / B + B).
    const T& at(std::size_t i) const {  // i < size()
        const Node* n = head_;
        while (i >= n->count) {
            i -= n->count;
            n = n->next;
        }
        return n->items[i];
    }

    void insert(std::size_t pos, const T& v) {  // pos <= size()
        if (!head_) {
            pushBack(v);
            return;
        }
        Node* n = head_;
        while (pos > n->count) {  // pos == n->count means: at the end of this node
            pos -= n->count;
            n = n->next;
        }
        if (n->count == B) {  // full: the upper half moves into a new node
            Node* half = new Node{n->next, B - B / 2, {}};
            std::copy(n->items + B / 2, n->items + B, half->items);
            n->count = B / 2;
            n->next = half;
            if (tail_ == n) tail_ = half;
            if (pos > n->count) {
                pos -= n->count;
                n = half;
            }
        }
        std::copy_backward(n->items + pos, n->items + n->count, n->items + n->count + 1);
        n->items[pos] = v;
        ++n->count;
        ++size_;
    }

    void erase(std::size_t pos) {  // pos < size()
        Node* prev = nullptr;
        Node* n = head_;
        while (pos >= n->count) {
            pos -= n->count;
            prev = n;
            n = n->next;
        }
        std::copy(n->items + pos + 1, n->items + n->count, n->items + pos);
        --n->count;
        --size_;
        if (n->count == 0) {  // an empty node is unlinked
            (prev ? prev->next : head_) = n->next;
            if (tail_ == n) tail_ = prev;
            delete n;
        }
    }

    template <class F>
    void forEach(F f) const {
        for (const Node* n = head_; n; n = n->next)
            for (unsigned i = 0; i < n->count; ++i) f(n->items[i]);
    }

    // ---- for the tests and the experiments
    std::size_t nodeCount() const {
        std::size_t c = 0;
        for (const Node* n = head_; n; n = n->next) ++c;
        return c;
    }
    bool nodesAreSane() const {  // every node is non-empty and not over-full; the counts add up to size()
        std::size_t total = 0;
        const Node* last = nullptr;
        for (const Node* n = head_; n; n = n->next) {
            if (n->count == 0 || n->count > B) return false;
            total += n->count;
            last = n;
        }
        return total == size_ && last == tail_;
    }
};

// ---------------------------------------------------------------- 3. The experiments of section 3 (./bench bench)

using Clock = std::chrono::steady_clock;
static volatile long long g_sink;

// Experiment 1: add up all the elements. Only the memory layout differs.
long long sumVector(const std::vector<int>& v) {
    long long s = 0;
    for (int x : v) s += x;
    return s;
}

long long sumList(const std::list<int>& l) {
    long long s = 0;
    for (int x : l) s += x;
    return s;
}

// An element of Bytes bytes whose first four bytes are the sort key.
template <std::size_t Bytes>
struct Item {
    int key;
    char pad[Bytes - sizeof(int)];
};
// A bare key, for the 4-byte case.
struct Small {
    int key;
};

template <class T>
T makeItem(int key) {
    T t;
    std::memset(&t, 0, sizeof t);
    t.key = key;
    return t;
}

// Experiment 2: insert n random keys so that the sequence stays sorted (walk to the place, then insert),
// then erase n elements at random positions. The return value is a checksum of the erased keys.
template <class T>
unsigned long long insertEraseVector(std::vector<T>& v, std::size_t n) {
    std::mt19937 rng(1);
    for (std::size_t i = 0; i < n; ++i) {
        int key = int(rng() % 1000000);
        auto it = v.begin();
        while (it != v.end() && it->key < key) ++it;
        v.insert(it, makeItem<T>(key));
    }
    unsigned long long check = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t k = rng() % v.size();
        check = check * 31 + (unsigned long long)v[k].key;
        v.erase(v.begin() + (std::ptrdiff_t)k);
    }
    return check;
}

template <class T>
unsigned long long insertEraseList(std::list<T>& l, std::size_t n) {
    std::mt19937 rng(1);
    for (std::size_t i = 0; i < n; ++i) {
        int key = int(rng() % 1000000);
        auto it = l.begin();
        while (it != l.end() && it->key < key) ++it;
        l.insert(it, makeItem<T>(key));
    }
    unsigned long long check = 0;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t k = rng() % l.size();
        auto it = std::next(l.begin(), (std::ptrdiff_t)k);
        check = check * 31 + (unsigned long long)it->key;
        l.erase(it);
    }
    return check;
}

// ---- for the tests and the experiments

template <class F>
double seconds(F f) {
    auto t0 = Clock::now();
    f();
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Nanoseconds per element for one pass of 'sumAll': the best of 5 trials, each doing enough passes to take a while.
template <class F>
double nsPerElement(std::size_t n, F sumAll) {
    std::size_t reps = std::max<std::size_t>(1, 20000000 / n);
    double best = 1e30;
    for (int t = 0; t < 5; ++t) {
        long long s = 0;
        double sec = seconds([&] {
            for (std::size_t r = 0; r < reps; ++r) s += sumAll();
        });
        g_sink = s;
        best = std::min(best, sec * 1e9 / (double(reps) * double(n)));
    }
    return best;
}

void benchTraverse(std::size_t n) {
    std::mt19937 rng(7);
    std::vector<int> values(n);
    for (int& x : values) x = int(rng() % 100);
    std::vector<int> v = values;
    std::list<int> ordered(values.begin(), values.end());  // nodes allocated one after another
    std::list<int> scrambled(values.begin(), values.end());
    scrambled.sort();  // relinks the nodes: the list order is now unrelated to the memory order
    UnrolledList<int, 13> unrolled;  // 13 ints + count + next = 64 bytes per node
    for (int x : values) unrolled.pushBack(x);
    long long total = sumVector(v);
    double a = nsPerElement(n, [&] { return sumVector(v); });
    double b = nsPerElement(n, [&] { return sumList(ordered); });
    double c = nsPerElement(n, [&] { return sumList(scrambled); });
    double d = nsPerElement(n, [&] {
        long long s = 0;
        unrolled.forEach([&](int x) { s += x; });
        return s;
    });
    std::printf("T1 %zu %.3f %.3f %.3f %.3f %lld\n", n, a, b, c, d, total);
    std::fflush(stdout);
}

template <class T, std::size_t Bytes>
void benchInsertErase(std::size_t n) {
    static_assert(sizeof(T) == Bytes, "the label must match the element size");
    int repsVector = 5, repsList = n <= 10000 ? 5 : (n <= 30000 ? 3 : 1);  // the best of several runs
    double sv = 1e30, sl = 1e30;
    unsigned long long cv = 0, cl = 0;
    for (int r = 0; r < repsVector; ++r) {
        std::vector<T> v;
        sv = std::min(sv, seconds([&] { cv = insertEraseVector(v, n); }));
    }
    for (int r = 0; r < repsList; ++r) {
        std::list<T> l;
        sl = std::min(sl, seconds([&] { cl = insertEraseList(l, n); }));
    }
    std::printf("T2 %zu %zu %.6f %.6f %d\n", Bytes, n, sv, sl, cv == cl ? 1 : 0);
    std::fflush(stdout);
}

void runBenchmarks() {
    std::printf("T0 sizeof(Node of UnrolledList<int,13>) = %zu\n", sizeof(UnrolledList<int, 13>::Node));
    for (std::size_t n : {1000u, 10000u, 100000u, 1000000u, 4000000u}) benchTraverse(n);
    benchInsertErase<Small, 4>(1000);
    benchInsertErase<Small, 4>(10000);
    benchInsertErase<Small, 4>(30000);
    benchInsertErase<Small, 4>(100000);
    benchInsertErase<Item<64>, 64>(10000);
    benchInsertErase<Item<256>, 256>(10000);
    benchInsertErase<Item<1024>, 1024>(10000);
}

// ---- 1. intrusive list and hlist

std::vector<int> arrivalForward(ListHead* queue) {
    std::vector<int> ids;
    for (ListHead* p = queue->next; p != queue; p = p->next) ids.push_back(OWNER_OF(p, Job, byArrival)->id);
    return ids;
}

std::vector<int> arrivalBackward(ListHead* queue) {
    std::vector<int> ids;
    for (ListHead* p = queue->prev; p != queue; p = p->prev) ids.push_back(OWNER_OF(p, Job, byArrival)->id);
    std::reverse(ids.begin(), ids.end());
    return ids;
}

std::vector<int> ownerForward(ListHead* head) {
    std::vector<int> ids;
    for (ListHead* p = head->next; p != head; p = p->next) ids.push_back(OWNER_OF(p, Job, byOwner)->id);
    return ids;
}

void testIntrusive() {
    {  // an empty list, and the layout facts the tutorial states
        ListHead h;
        listInit(&h);
        CHECK(listEmpty(&h) && h.next == &h && h.prev == &h);
        CHECK(sizeof(ListHead) == 2 * sizeof(void*));
        Job j{};
        CHECK(reinterpret_cast<char*>(&j.byArrival) - reinterpret_cast<char*>(&j) == (std::ptrdiff_t)offsetof(Job, byArrival));
        CHECK(OWNER_OF(&j.byArrival, Job, byArrival) == &j && OWNER_OF(&j.byOwner, Job, byOwner) == &j);
        CHECK(offsetof(Job, byArrival) == 8 && offsetof(Job, byOwner) == 24);  // 4 + 4 bytes of ints, then 16 per ListHead
    }
    {  // the stack and queue orders
        ListHead h;
        listInit(&h);
        Job a{1, 0, {}, {}}, b{2, 0, {}, {}}, c{3, 0, {}, {}};
        listAddFront(&a.byArrival, &h);
        listAddFront(&b.byArrival, &h);
        listAddBack(&c.byArrival, &h);
        CHECK((arrivalForward(&h) == std::vector<int>{2, 1, 3}));
        CHECK((arrivalBackward(&h) == std::vector<int>{2, 1, 3}));
        listRemove(&a.byArrival);
        CHECK((arrivalForward(&h) == std::vector<int>{2, 3}));
        CHECK(a.byArrival.next == reinterpret_cast<ListHead*>(kPoisonNext));
        CHECK(a.byArrival.prev == reinterpret_cast<ListHead*>(kPoisonPrev));
    }
    std::mt19937 rng(5);
    for (int round = 0; round < 300; ++round) {
        const int jobsN = 1 + int(rng() % 40);
        const int owners = 1 + int(rng() % 4);
        std::vector<Job> jobs(jobsN);
        ListHead queue;
        listInit(&queue);
        std::vector<ListHead> ownerHeads(owners);
        for (ListHead& h : ownerHeads) listInit(&h);
        std::deque<int> queueModel;
        std::vector<std::deque<int>> ownerModel(owners);
        std::vector<char> alive(jobsN, 1);
        for (int i = 0; i < jobsN; ++i) {
            jobs[i].id = i;
            jobs[i].owner = int(rng() % owners);
            if (rng() % 2) {
                listAddFront(&jobs[i].byArrival, &queue);
                queueModel.push_front(i);
            } else {
                listAddBack(&jobs[i].byArrival, &queue);
                queueModel.push_back(i);
            }
            listAddBack(&jobs[i].byOwner, &ownerHeads[jobs[i].owner]);
            ownerModel[jobs[i].owner].push_back(i);
        }
        for (int step = 0; step < 6; ++step) {
            CHECK((arrivalForward(&queue) == std::vector<int>(queueModel.begin(), queueModel.end())));
            CHECK((arrivalBackward(&queue) == std::vector<int>(queueModel.begin(), queueModel.end())));
            for (int o = 0; o < owners; ++o)
                CHECK((ownerForward(&ownerHeads[o]) == std::vector<int>(ownerModel[o].begin(), ownerModel[o].end())));
            if (rng() % 2) {  // cancel one job given only its pointer
                int id = int(rng() % jobsN);
                if (alive[id]) {
                    cancel(&jobs[id]);
                    alive[id] = 0;
                    queueModel.erase(std::find(queueModel.begin(), queueModel.end(), id));
                    auto& om = ownerModel[jobs[id].owner];
                    om.erase(std::find(om.begin(), om.end(), id));
                    CHECK(jobs[id].byOwner.next == reinterpret_cast<ListHead*>(kPoisonNext));
                }
            } else {  // cancel a whole owner while walking the queue
                int o = int(rng() % owners);
                int expect = int(ownerModel[o].size());
                for (int id : ownerModel[o]) alive[id] = 0;
                queueModel.erase(std::remove_if(queueModel.begin(), queueModel.end(),
                                                [&](int id) { return jobs[id].owner == o && !alive[id]; }),
                                 queueModel.end());
                ownerModel[o].clear();
                CHECK(cancelOwner(&queue, o) == expect);
            }
        }
    }
}

void testHlist() {
    std::mt19937 rng(6);
    for (int round = 0; round < 200; ++round) {
        HHead buckets[4] = {};
        std::vector<HNode> nodes(30);
        std::vector<int> bucketOf(30, -1);
        std::vector<std::deque<int>> model(4);
        for (int step = 0; step < 100; ++step) {
            int i = int(rng() % 30);
            if (bucketOf[i] < 0) {
                int b = int(rng() % 4);
                hAddFront(&nodes[i], &buckets[b]);
                bucketOf[i] = b;
                model[b].push_front(i);
            } else {
                hRemove(&nodes[i]);
                auto& m = model[bucketOf[i]];
                m.erase(std::find(m.begin(), m.end(), i));
                bucketOf[i] = -1;
            }
            for (int b = 0; b < 4; ++b) {
                std::vector<int> got;
                for (HNode* p = buckets[b].first; p; p = p->next) got.push_back(int(p - nodes.data()));
                CHECK((got == std::vector<int>(model[b].begin(), model[b].end())));
            }
        }
    }
    CHECK(sizeof(HHead) * 2 == sizeof(ListHead));
}

// ---- 2. pool

void testPool() {
    {  // the slot layout and the order of the first allocations
        Pool<24, 4> pool;
        CHECK(Pool<24, 4>::slotBytes() == 24);
        const char* base = static_cast<const char*>(pool.base());
        for (int i = 0; i < 4; ++i) CHECK(static_cast<char*>(pool.allocate()) == base + 24 * i);
        CHECK(pool.allocate() == nullptr && pool.used() == 4);
        void* p = const_cast<char*>(base) + 24 * 2;
        pool.deallocate(p);
        CHECK(pool.used() == 3);
        CHECK(pool.allocate() == p);  // the slot freed last is handed out first
        CHECK(pool.allocate() == nullptr);
    }
    std::mt19937 rng(8);
    for (int round = 0; round < 200; ++round) {
        Pool<16, 10> pool;
        const char* base = static_cast<const char*>(pool.base());
        std::vector<void*> live;
        for (int step = 0; step < 200; ++step) {
            if (!live.empty() && (rng() % 2 || live.size() == 10)) {
                std::size_t k = rng() % live.size();
                void* p = live[k];
                live.erase(live.begin() + (std::ptrdiff_t)k);
                pool.deallocate(p);
                CHECK(pool.allocate() == p);  // LIFO: it comes straight back
                live.push_back(p);
            } else {
                void* p = pool.allocate();
                CHECK(p != nullptr);
                CHECK(std::find(live.begin(), live.end(), p) == live.end());  // never handed out twice
                CHECK(static_cast<char*>(p) >= base && static_cast<char*>(p) < base + 160);
                CHECK((static_cast<char*>(p) - base) % 16 == 0);
                std::memset(p, 0xAB, 16);  // the caller may use the whole slot
                live.push_back(p);
            }
            CHECK(pool.used() == live.size());
        }
    }
}

// ---- 4. XOR list

void testXor() {
    {
        XorList<int> l;
        std::vector<int> got;
        l.forEach([&](int x) { got.push_back(x); });
        CHECK(got.empty() && l.size() == 0);
    }
    std::mt19937 rng(9);
    for (int round = 0; round < 300; ++round) {
        XorList<int> l;
        std::deque<int> model;
        for (int step = 0; step < 60; ++step) {
            int op = int(rng() % 4);
            if (op <= 1) {
                int v = int(rng() % 1000);
                l.pushBack(v);  // after reverse() this adds at the new tail, which the model keeps last
                model.push_back(v);
            } else if (op == 2 && !model.empty()) {
                l.popFront();
                model.pop_front();
            } else {
                l.reverse();
                std::reverse(model.begin(), model.end());
            }
            std::vector<int> got;
            l.forEach([&](int x) { got.push_back(x); });
            CHECK((got == std::vector<int>(model.begin(), model.end())));
            CHECK(l.size() == model.size());
        }
    }
}

// ---- 5. unrolled list

template <unsigned B>
void testUnrolledWith(unsigned seed) {
    std::mt19937 rng(seed);
    for (int round = 0; round < 120; ++round) {
        UnrolledList<int, B> l;
        std::vector<int> model;
        for (int step = 0; step < 150; ++step) {
            int op = int(rng() % 5);
            if (op == 0) {
                int v = int(rng() % 1000);
                l.pushBack(v);
                model.push_back(v);
            } else if (op <= 2 || model.empty()) {
                int v = int(rng() % 1000);
                std::size_t pos = rng() % (model.size() + 1);
                l.insert(pos, v);
                model.insert(model.begin() + (std::ptrdiff_t)pos, v);
            } else {
                std::size_t pos = rng() % model.size();
                l.erase(pos);
                model.erase(model.begin() + (std::ptrdiff_t)pos);
            }
            std::vector<int> got;
            l.forEach([&](int x) { got.push_back(x); });
            CHECK(got == model);
            CHECK(l.size() == model.size() && l.nodesAreSane());
            if (!model.empty()) {
                std::size_t i = rng() % model.size();
                CHECK(l.at(i) == model[i]);
            }
        }
    }
}

void testUnrolled() {
    testUnrolledWith<2>(1);
    testUnrolledWith<3>(2);
    testUnrolledWith<4>(3);
    testUnrolledWith<13>(4);
    {  // appending fills nodes completely: n elements need ceil(n / B) nodes
        UnrolledList<int, 4> l;
        for (int i = 0; i < 10; ++i) l.pushBack(i);
        CHECK(l.nodeCount() == 3 && l.size() == 10);
        CHECK(sizeof(UnrolledList<int, 13>::Node) == 64);
    }
    {  // a full node splits into two halves: 4 elements, insert in the middle -> nodes of 2 and 3
        UnrolledList<int, 4> l;
        for (int i = 0; i < 4; ++i) l.pushBack(i);
        l.insert(1, 99);
        CHECK(l.nodeCount() == 2 && l.at(0) == 0 && l.at(1) == 99 && l.at(2) == 1 && l.at(4) == 3);
    }
}

// ---- 3. experiments: the structures agree

void testExperiments() {
    for (std::size_t n : {1u, 2u, 17u, 1000u}) {
        std::mt19937 rng(7);
        std::vector<int> values(n);
        for (int& x : values) x = int(rng() % 100);
        std::list<int> ordered(values.begin(), values.end());
        std::list<int> scrambled(values.begin(), values.end());
        scrambled.sort();
        UnrolledList<int, 13> unrolled;
        for (int x : values) unrolled.pushBack(x);
        long long s = 0;
        unrolled.forEach([&](int x) { s += x; });
        CHECK(sumVector(values) == sumList(ordered) && sumList(ordered) == sumList(scrambled) && s == sumVector(values));
    }
    auto both = [](auto tag, std::size_t n) {
        using T = decltype(tag);
        std::vector<T> v;
        std::list<T> l;
        unsigned long long cv = insertEraseVector(v, n);
        unsigned long long cl = insertEraseList(l, n);
        CHECK(cv == cl && v.empty() && l.empty());
    };
    for (std::size_t n : {1u, 5u, 300u}) {
        both(Small{}, n);
        both(Item<8>{}, n);
        both(Item<64>{}, n);
        both(Item<1024>{}, n);
    }
    CHECK(sizeof(Small) == 4 && sizeof(Item<64>) == 64 && sizeof(Item<1024>) == 1024);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "bench") == 0) {
        runBenchmarks();
        return 0;
    }
    testIntrusive();
    testHlist();
    testPool();
    testXor();
    testUnrolled();
    testExperiments();
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
