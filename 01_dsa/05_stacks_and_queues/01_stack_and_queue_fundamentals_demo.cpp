// DSA 05.01 companion: every listing of the tutorial "Stack and Queue Fundamentals", plus checks.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

template <class T>
class ArrayStack {
public:
    bool empty() const { return a_.empty(); }
    std::size_t size() const { return a_.size(); }
    void push(T x) { a_.push_back(std::move(x)); }
    T& top() { assert(!a_.empty()); return a_.back(); }
    void pop() { assert(!a_.empty()); a_.pop_back(); }
private:
    std::vector<T> a_;
};

template <class T, std::size_t N>
class FixedStack {
public:
    bool empty() const { return n_ == 0; }
    bool full() const { return n_ == N; }
    std::size_t size() const { return n_; }
    bool push(const T& x) {            // false when full: the caller decides what to do
        if (n_ == N) return false;
        data_[n_++] = x;
        return true;
    }
    bool pop(T& out) {                 // false when empty
        if (n_ == 0) return false;
        out = data_[--n_];
        return true;
    }
private:
    T data_[N];
    std::size_t n_ = 0;
};

class FrontEraseQueue {                // correct, and O(n) per pop
public:
    bool empty() const { return a_.empty(); }
    void push(int x) { a_.push_back(x); }
    int pop() {
        int x = a_.front();
        a_.erase(a_.begin());          // moves every remaining element one place left
        return x;
    }
private:
    std::vector<int> a_;
};

class HeadIndexQueue {                 // O(1) per operation, but never gives memory back
public:
    bool empty() const { return head_ == a_.size(); }
    void push(int x) { a_.push_back(x); }
    int pop() { return a_[head_++]; }
    std::size_t slotsHeld() const { return a_.size(); }
private:
    std::vector<int> a_;
    std::size_t head_ = 0;
};

template <class T>
class RingQueue {
public:
    bool empty() const { return size_ == 0; }
    std::size_t size() const { return size_; }
    std::size_t capacity() const { return buf_.size(); }
    T& front() { assert(size_ > 0); return buf_[head_]; }
    void push(T x) {
        if (size_ == buf_.size()) grow();
        buf_[wrap(head_ + size_)] = std::move(x);
        ++size_;
    }
    void pop() {
        assert(size_ > 0);
        head_ = wrap(head_ + 1);
        --size_;
    }
protected:
    std::size_t wrap(std::size_t i) const { return i >= buf_.size() ? i - buf_.size() : i; }
    void grow() {
        std::vector<T> bigger(buf_.empty() ? 4 : 2 * buf_.size());
        for (std::size_t i = 0; i < size_; ++i)           // copy in queue order, starting at slot 0
            bigger[i] = std::move(buf_[wrap(head_ + i)]);
        buf_ = std::move(bigger);
        head_ = 0;
    }
    std::vector<T> buf_;
    std::size_t head_ = 0, size_ = 0;
};

template <class T, std::uint32_t N>
class FixedRing {
    static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
public:
    explicit FixedRing(std::uint32_t firstIndex = 0) : in_(firstIndex), out_(firstIndex) {}
    std::uint32_t size() const { return in_ - out_; }     // unsigned wrap-around keeps this right
    bool empty() const { return in_ == out_; }
    bool full() const { return size() == N; }
    bool push(const T& x) {
        if (full()) return false;
        data_[in_ & (N - 1)] = x;
        ++in_;
        return true;
    }
    bool pop(T& out) {
        if (empty()) return false;
        out = data_[out_ & (N - 1)];
        ++out_;
        return true;
    }
    std::uint32_t in() const { return in_; }
    std::uint32_t out() const { return out_; }
private:
    T data_[N];
    std::uint32_t in_, out_;
};

template <class T>
class RingDeque : public RingQueue<T> {
    using Q = RingQueue<T>;
    using Q::buf_;
    using Q::head_;
    using Q::size_;
public:
    T& back() { assert(size_ > 0); return buf_[Q::wrap(head_ + size_ - 1)]; }
    void pushFront(T x) {
        if (size_ == buf_.size()) Q::grow();
        head_ = (head_ == 0 ? buf_.size() : head_) - 1;   // step left, wrapping below slot 0
        buf_[head_] = std::move(x);
        ++size_;
    }
    void popBack() { assert(size_ > 0); --size_; }
};

template <class T, std::uint32_t N>
class SpscRing {
    static_assert(N != 0 && (N & (N - 1)) == 0, "N must be a power of two");
public:
    bool push(const T& x) {                                   // producer thread only
        std::uint32_t in = in_.load(std::memory_order_relaxed);
        if (in - out_.load(std::memory_order_acquire) == N) return false;     // full
        data_[in & (N - 1)] = x;
        in_.store(in + 1, std::memory_order_release);         // publish the element
        return true;
    }
    bool pop(T& out) {                                        // consumer thread only
        std::uint32_t o = out_.load(std::memory_order_relaxed);
        if (in_.load(std::memory_order_acquire) == o) return false;           // empty
        out = data_[o & (N - 1)];
        out_.store(o + 1, std::memory_order_release);         // hand the slot back
        return true;
    }
private:
    alignas(64) std::atomic<std::uint32_t> in_{0};            // written by the producer only
    alignas(64) std::atomic<std::uint32_t> out_{0};           // written by the consumer only
    alignas(64) T data_[N];
};

template <class T>
class TwoStackQueue {
public:
    bool empty() const { return in_.empty() && out_.empty(); }
    std::size_t size() const { return in_.size() + out_.size(); }
    void push(T x) { in_.push(std::move(x)); }
    T& front() { refill(); return out_.top(); }
    void pop() { refill(); out_.pop(); }
    std::size_t transfers() const { return transfers_; }
private:
    void refill() {
        assert(!empty());
        if (!out_.empty()) return;                 // only when "out" has run dry
        while (!in_.empty()) {
            out_.push(std::move(in_.top()));
            in_.pop();
            ++transfers_;
        }
    }
    ArrayStack<T> in_, out_;
    std::size_t transfers_ = 0;
};

template <class T>
class StackFromQueue {
public:
    bool empty() const { return q_.empty(); }
    std::size_t size() const { return q_.size(); }
    void push(T x) {
        q_.push(std::move(x));
        for (std::size_t i = q_.size() - 1; i > 0; --i) {     // rotate the older elements behind it
            q_.push(std::move(q_.front()));
            q_.pop();
        }
    }
    T& top() { return q_.front(); }
    void pop() { q_.pop(); }
private:
    std::queue<T> q_;
};

class MinStack {
public:
    bool empty() const { return s_.empty(); }
    void push(int x) { s_.push_back({x, s_.empty() ? x : std::min(x, s_.back().second)}); }
    void pop() { s_.pop_back(); }
    int top() const { return s_.back().first; }
    int min() const { return s_.back().second; }
private:
    std::vector<std::pair<int, int>> s_;       // (value, smallest value at or below it)
};

class MinStackEncoded {
public:
    bool empty() const { return d_.empty(); }
    void push(int x) {
        if (d_.empty()) { d_.push_back(0); min_ = x; return; }
        d_.push_back(static_cast<long long>(x) - min_);   // negative exactly when x is a new minimum
        if (x < min_) min_ = x;
    }
    void pop() {
        long long d = d_.back();
        d_.pop_back();
        if (d < 0) min_ -= d;                  // the popped value was the minimum: restore the old one
    }
    long long top() const { long long d = d_.back(); return d < 0 ? min_ : min_ + d; }
    long long min() const { return min_; }
    long long stored() const { return d_.back(); }
private:
    std::vector<long long> d_;
    long long min_ = 0;
};

class MinQueue {
public:
    bool empty() const { return in_.empty() && out_.empty(); }
    void push(int x) { in_.push(x); }
    void pop() { refill(); out_.pop(); }
    int front() { refill(); return out_.top(); }
    int min() const {
        if (in_.empty()) return out_.min();
        if (out_.empty()) return in_.min();
        return std::min(in_.min(), out_.min());
    }
private:
    void refill() {
        if (!out_.empty()) return;
        while (!in_.empty()) { out_.push(in_.top()); in_.pop(); }
    }
    MinStack in_, out_;
};

// ---- Checks (the companion to DSA 05.01) --------------------------------------
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined -pthread demo.cpp -o demo && ./demo
// For the two-thread ring also try -fsanitize=thread (instead of address,undefined).
#include <climits>
#include <cstdio>
#include <deque>
#include <random>
#include <stack>
#include <string>
#include <thread>

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

int main() {
    std::mt19937 rng(1);

    // 1. Stacks against std::stack, and the fixed stack against a bounded model.
    ArrayStack<int> as; std::stack<int> ms; FixedStack<int, 8> fs; std::vector<int> fm;
    for (int i = 0; i < 5000; ++i) {
        int x = (int)(rng() % 100);
        if (rng() % 3 != 0) { as.push(x); ms.push(x); }
        else if (!ms.empty()) { CHECK(as.top() == ms.top()); as.pop(); ms.pop(); }
        CHECK(as.size() == ms.size());
        if (rng() % 2) { bool ok = fs.push(x); CHECK(ok == (fm.size() < 8)); if (ok) fm.push_back(x); }
        else { int o = 0; bool ok = fs.pop(o); CHECK(ok == !fm.empty()); if (ok) { CHECK(o == fm.back()); fm.pop_back(); } }
    }

    // 2. The growing ring and the ring deque against the standard containers (wrapped content, growth).
    RingQueue<std::string> rq; std::queue<std::string> mq; RingDeque<int> rd; std::deque<int> md;
    for (int i = 0; i < 20000; ++i) {
        int pushPct = (i / 2000 % 2 == 0) ? 75 : 25;
        std::string s = "s" + std::to_string(rng() % 1000);
        if ((int)(rng() % 100) < pushPct) { rq.push(s); mq.push(s); } else if (!mq.empty()) { CHECK(rq.front() == mq.front()); rq.pop(); mq.pop(); }
        CHECK(rq.size() == mq.size());
        int x = (int)(rng() % 1000), op = (int)(rng() % 6);
        if (op == 0) { rd.push(x); md.push_back(x); } else if (op == 1) { rd.pushFront(x); md.push_front(x); }
        else if (op == 2 && !md.empty()) { rd.pop(); md.pop_front(); } else if (op == 3 && !md.empty()) { rd.popBack(); md.pop_back(); }
        CHECK(rd.size() == md.size());
        if (!md.empty()) { CHECK(rd.front() == md.front()); CHECK(rd.back() == md.back()); }
    }

    // 3. FixedRing: every push/pop sequence of length 12 on 4 slots, from index 0 and from just below 2^32.
    for (std::uint32_t start : {0u, 0xFFFFFFFEu}) {
        for (unsigned bits = 0; bits < (1u << 12); ++bits) {
            FixedRing<int, 4> r(start); std::queue<int> m; int next = 0;
            for (int k = 0; k < 12; ++k) {
                if (bits >> k & 1) { bool ok = r.push(next); CHECK(ok == (m.size() < 4)); if (ok) m.push(next); ++next; }
                else { int o = -1; bool ok = r.pop(o); CHECK(ok == !m.empty()); if (ok) { CHECK(o == m.front()); m.pop(); } }
                CHECK(r.size() == m.size());
            }
        }
    }
    FixedRing<int, 4> w(0xFFFFFFFEu);
    for (int v = 10; v < 14; ++v) w.push(v);
    std::printf("in=%u out=%u size=%u full=%d   (in wrapped past 2^32; the size is still right)\n", w.in(), w.out(), w.size(), (int)w.full());

    // 4. One producer and one consumer thread sharing a ring.
    SpscRing<int, 64> sp; int bad = 0; const int M = 200000;
    std::thread producer([&] { for (int i = 0; i < M; ++i) while (!sp.push(i)) std::this_thread::yield(); });
    std::thread consumer([&] { for (int i = 0; i < M; ++i) { int v; while (!sp.pop(v)) std::this_thread::yield(); if (v != i) ++bad; } });
    producer.join(); consumer.join();
    CHECK(bad == 0);

    // 5. The queue from two stacks (pours never exceed pushes) and the stack from a queue.
    TwoStackQueue<int> tq; std::queue<int> tm; std::size_t pushes = 0; StackFromQueue<int> sq; std::stack<int> sm;
    for (int i = 0; i < 20000; ++i) {
        int x = (int)(rng() % 100);
        if (rng() % 100 < (i / 2500 % 2 ? 30u : 70u)) { tq.push(x); tm.push(x); ++pushes; sq.push(x); sm.push(x); }
        else if (!tm.empty()) { CHECK(tq.front() == tm.front()); tq.pop(); tm.pop(); CHECK(sq.top() == sm.top()); sq.pop(); sm.pop(); }
        CHECK(tq.size() == tm.size()); CHECK(tq.transfers() <= pushes);
    }

    // 6. Min-stack (pairs), min-stack (differences) and min-queue against a plain scan.
    MinStack a; MinStackEncoded b; MinQueue q; std::vector<int> v; std::deque<int> d;
    for (int i = 0; i < 20000; ++i) {
        int x = (rng() % 4 == 0) ? (rng() % 2 ? INT_MIN : INT_MAX) : (int)(rng() % 5);
        if (rng() % 100 < (i / 2500 % 2 ? 35u : 65u)) { a.push(x); b.push(x); v.push_back(x); q.push(x); d.push_back(x); }
        else if (!v.empty()) { a.pop(); b.pop(); v.pop_back(); CHECK(q.front() == d.front()); q.pop(); d.pop_front(); }
        if (!v.empty()) { int mn = *std::min_element(v.begin(), v.end()); CHECK(a.top() == v.back() && a.min() == mn); CHECK(b.top() == v.back() && b.min() == mn); }
        if (!d.empty()) CHECK(q.min() == *std::min_element(d.begin(), d.end()));
    }

    std::printf("%d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
