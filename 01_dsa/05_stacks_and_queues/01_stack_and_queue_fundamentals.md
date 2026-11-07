# DSA 05.01: Stack and Queue Fundamentals

*Part of the [Stacks and queues plan](00_plan.md) · Time: about 6 hours (3.5 reading, 2.5 problems) · You need: [Complexity and arrays in memory](../01_arrays_and_strings/01_complexity_and_arrays_in_memory.md) (why a growing array costs O(1) amortised) · Related: [Memory consistency and atomics](../../04_computer_organization/20_memory_consistency_and_atomics.md) for section 4 · Companion code: [`01_stack_and_queue_fundamentals_demo.cpp`](01_stack_and_queue_fundamentals_demo.cpp) (every listing below plus checks)*

By the end of this tutorial you can:

- build a stack and a queue on an array, and explain why a queue that pops with `erase(begin())` is a trap;
- write a ring buffer three ways (a stored size, one slot left empty, free-running indexes) and say which one the Linux kernel's `kfifo` uses and why its size must be a power of two;
- share a ring between one producer thread and one consumer thread with no lock, and say which loads and stores need acquire and release;
- build a queue from two stacks, a stack from one queue, a min-stack and a min-queue, and prove the costs;
- recognise these shapes in `io_uring` and in the `rkdma` project.

**Where the numbers come from.** Timings were measured on one machine: a 2-core Intel Xeon VM (Ubuntu 24.04, GCC 13.3 with `-O2`, 64-byte cache lines). Your numbers will differ; the shapes should not. Every listing was compiled with `-Wall -Wextra` and tested against the standard library before it went into this file. 12,184,052 checks passed under AddressSanitizer with UBSan and under ThreadSanitizer, and the same program passed at `-O2` with GCC and with clang. The companion file contains every listing and a shorter set of checks (459,031 of them).

All listings need these headers, which the companion file includes: `<algorithm>`, `<atomic>`, `<cassert>`, `<cstddef>`, `<cstdint>`, `<queue>`, `<utility>` and `<vector>`.

---

## 1. The two contracts

### 1.1 What a stack and a queue promise

A **stack** gives elements back in the reverse of the order they went in: last in, first out (LIFO). A **queue** gives them back in the order they went in: first in, first out (FIFO). A **deque** (say "deck", a double-ended queue) can push and pop at both ends.

| | Add | Remove | Look | Order |
|---|---|---|---|---|
| Stack | `push` at the top | `pop` from the top | `top` | LIFO |
| Queue | `push` at the back | `pop` from the front | `front` | FIFO |
| Deque | at either end | from either end | `front`, `back` | either |

Every operation should cost O(1). Pushing `a`, `b`, `c` and popping three times gives `c b a` from a stack and `a b c` from a queue. That is the whole contract; the rest of this tutorial is about building it without hidden costs.

### 1.2 Where you meet them

- **Stack:** the call stack (a call pushes a frame, a return pops it), undo, the browser's back button, depth-first search, matching brackets and evaluating expressions (tutorial 04), and the monotonic stack (tutorial 02).
- **Queue:** breadth-first search, round-robin scheduling, and any buffer between something that produces data and something that consumes it (keystrokes, network packets, log lines).
- **Deque:** the sliding-window maximum (tutorial 03).

### 1.3 What the C++ library gives you

`std::stack` and `std::queue` are *adaptors*: thin wrappers around another container. In libstdc++ 13 (the headers `bits/stl_stack.h` and `bits/stl_queue.h`) the default container of both is `std::deque`, and `std::stack<int, std::vector<int>>` swaps in a vector. Three details trip people up:

- `pop()` returns `void`. To get the value, call `top()` (or `front()`) first, then `pop()`. The usual explanation is exception safety: a `pop` that returns the element by value must copy or move it out after removing it, and if that copy throws, the element is lost.
- `top()`, `front()` and `pop()` on an empty container are undefined behaviour. The library does not check; you do.
- `std::priority_queue` is not a queue. It returns the largest element, not the oldest.

LeetCode's design problems (`MyStack`, `MyQueue`) do return the value from `pop`; follow the problem statement there.

---

## 2. Stacks

### 2.1 A stack on an array

The top of the stack is the end of an array, so `push` and `pop` touch only the last element. A growing array (`std::vector`) gives O(1) amortised pushes, because it doubles when full and the copies are spread over the pushes that caused them (tutorial 01 of arrays).

```cpp
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
```

The `assert`s document that an empty stack must not be popped. In an interview, say which behaviour you chose (assert, throw, or return a flag) and move on.

A stack on a linked list also works (push and pop at the head), but each push allocates one node and the nodes are scattered in memory. The array version is the default; tutorial 06 of linked lists has the measurements.

### 2.2 A stack with no heap

Kernels, interrupt handlers and small microcontrollers often cannot allocate. A stack of fixed capacity lives in the object itself and returns a flag instead of growing:

```cpp
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
```

The caller decides what a full stack means: drop the item, report an error or stop. `T data_[N]` needs `T` to be default constructible and assignable; that is fine for the integers and small structs this is used with.

---

## 3. Queues

### 3.1 The trap: popping from the front of an array

A queue wants to remove the first element, and the first element of an array is its most expensive place to remove. Two natural attempts both go wrong:

```cpp
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
```

`FrontEraseQueue` is correct, and every `pop` moves all remaining elements one place to the left: O(n). `HeadIndexQueue` is O(1) per operation but never gives anything back. After a million `push` and `pop` pairs with at most one element alive, it still holds 1,000,000 slots. Here is how long it takes to push `n` numbers and then pop them all:

| `n` | `FrontEraseQueue` (ms) | `HeadIndexQueue` (ms) | `std::queue` (ms) |
|---|---|---|---|
| 1,000 | 0.019 | 0.001 | 0.003 |
| 10,000 | 1.35 | 0.013 | 0.027 |
| 100,000 | 402 | 0.35 | 0.20 |

Ten times more elements made `FrontEraseQueue` about 300 times slower (a straight quadratic would be 100 times; at 100,000 elements the 400 KB array no longer fits in this machine's 48 KB L1 data cache, which probably adds to it). The fix is to stop moving the elements and move the *start* instead, and to reuse the slots the start has left behind. That is the ring buffer.

### 3.2 The ring buffer

Treat the array as a circle. Keep `head_`, the slot of the oldest element, and `size_`, the number of elements. The next free slot is `head_ + size_`, taken modulo the capacity. `pop` moves `head_` forward by one; `push` writes after the last element. When an index runs off the end it wraps to slot 0.

```cpp
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
```

`wrap(i)` is `i % capacity` for `i` below twice the capacity, written as a compare and a subtract. `grow` is explained in section 3.5. Here is a run (the output of the class above; slots that were never written show 0, and a popped slot keeps its old value until it is overwritten):

```text
push 1   head=0 size=1 cap=4 slots=[1 0 0 0]
push 2   head=0 size=2 cap=4 slots=[1 2 0 0]
push 3   head=0 size=3 cap=4 slots=[1 2 3 0]
push 4   head=0 size=4 cap=4 slots=[1 2 3 4]
pop      head=1 size=3 cap=4 slots=[1 2 3 4]
pop      head=2 size=2 cap=4 slots=[1 2 3 4]
push 5   head=2 size=3 cap=4 slots=[5 2 3 4]
push 6   head=2 size=4 cap=4 slots=[5 6 3 4]
push 7   head=0 size=5 cap=8 slots=[3 4 5 6 7 0 0 0]
```

Read the last five lines. After `pop`, `pop`, `push 5` and `push 6` the queue is `3 4 5 6`, but in memory it is `5 6 3 4`: the elements wrapped around the end of the array. Pushing 7 finds the array full, so `grow` copies the elements *in queue order* into a new array of twice the size, starting at slot 0, and resets `head_`.

### 3.3 Full or empty?

With only `head` and `tail` indexes, a full ring and an empty ring both have `head == tail`. There are three ways to tell them apart:

| Way | How | Cost |
|---|---|---|
| Store the size | `empty` is `size == 0`, `full` is `size == capacity` | one extra field to keep up to date (`RingQueue` above) |
| Leave one slot unused | `empty` is `head == tail`, `full` is `(tail + 1) % cap == head` | one slot of capacity wasted |
| Free-running indexes | never wrap the indexes; the size is `in - out` | nothing, but the capacity must be a power of two |

The third is the one used in production, so it gets its own section.

### 3.4 Free-running indexes

Let two unsigned counters, `in_` and `out_`, only ever go up. `in_` counts all elements ever pushed and `out_` all elements ever popped. Then:

- the number of elements is `in_ - out_`;
- the ring is empty when `in_ == out_` and full when `in_ - out_ == N`;
- the slot for an index is the index modulo `N`, computed as `index & (N - 1)` when `N` is a power of two.

```cpp
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
```

The counters are 32-bit and will wrap past 2^32 after four billion operations. That is harmless, for two reasons. Unsigned subtraction in C++ is arithmetic modulo 2^32, so `in_ - out_` is still the right count after `in_` has wrapped, as long as the count itself is at most `N`. And because `N` divides 2^32 (it is a power of two), the slot sequence carries on correctly across the wrap. The constructor takes a starting index so that you can watch this happen:

```text
start       in=4294967294 out=4294967294 size=0
push 10: slot 2 -> in=4294967295 out=4294967294 size=1
push 11: slot 3 -> in=0 out=4294967294 size=2
push 12: slot 0 -> in=1 out=4294967294 size=3
push 13: slot 1 -> in=2 out=4294967294 size=4
full=1
pop -> 10: slot 2 -> in=2 out=4294967295 size=3
pop -> 11: slot 3 -> in=2 out=0 size=2
push 14: slot 2 -> in=3 out=0 size=3
push 15: slot 3 -> in=4 out=0 size=4
```

`in` wrapped from 4294967295 to 0 at the second push, `size` stayed correct, and the slots went 2, 3, 0, 1, 2, 3 without a hiccup. Had `N` been 6, the slot of index 4294967295 would be `4294967295 % 6 = 3` and the next one `0 % 6 = 0`, so slots 4 and 5 would be skipped at the wrap, and two live elements would end up in one slot (paper problem B).

**This is how the Linux kernel does it.** `struct __kfifo` in `include/linux/kfifo.h` holds `in`, `out` and `mask` (all `unsigned int`), `kfifo_len` is `in - out`, and the unused space is `(mask + 1) - (in - out)`. `__kfifo_alloc_node` in `lib/kfifo.c` rounds the size up to a power of two with a comment that says the "let the indices wrap" technique only works in that case. The header also states that no locking is needed while there is only one reader and one writer. (Source checked on the kernel `master` branch as fetched on 2026-11-01.)

### 3.5 Growing a ring

A ring that must grow allocates a bigger array and copies. The copy must walk the elements **in queue order** (`head_`, `head_ + 1`, and so on, wrapping), not copy the raw array: a raw copy would leave the wrapped part in the wrong place. That is the line `bigger[i] = std::move(buf_[wrap(head_ + i)])` in `grow`, and it is the bug interviewers hope to see you avoid. Doubling keeps `push` at O(1) amortised, the same argument as for `std::vector`. After `grow`, references to elements are invalid, as with a vector.

### 3.6 The deque

A ring buffer already knows where both ends are, so a deque needs only the mirror images of `push` and `pop`:

```cpp
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
```

`push` and `pop` of the base class are `pushBack` and `popFront`. `pushFront` steps `head_` left, wrapping below slot 0. Every operation is O(1) amortised.

`std::deque` is built differently. libstdc++ keeps a table of pointers to blocks of 512 bytes (128 `int`s per block), so growing at either end allocates a new block and never moves the old elements. That is why the standard guarantees that `push_back` and `push_front` on a `std::deque` keep references to the existing elements valid (iterators are invalidated), which a growing ring cannot promise. The test program checks this: it takes a reference to an element, pushes 10,000 elements at each end, and reads through the reference. The price is one more pointer hop per access.

---

## 4. A ring between two threads

### 4.1 Why one writer per counter needs no lock

Give each counter exactly one writer. The **producer** thread writes `in_` and the **consumer** thread writes `out_`, and each only reads the other's counter. Then no two threads ever write the same variable, and the only problem left is *ordering*: the consumer must not see the new `in_` before it can see the element written into the slot, and the producer must not reuse a slot before the consumer has finished reading it.

That is exactly what release and acquire give (tutorial 07 of linked lists, section 2.2, and the Computer organization tutorial linked at the top):

```cpp
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
```

- `push` writes the element, then **publishes** it with a release store of `in_`. A consumer whose acquire load of `in_` sees the new value is guaranteed to see the element.
- `pop` reads the element, then **hands the slot back** with a release store of `out_`. A producer whose acquire load of `out_` sees the new value is guaranteed that the read is finished.
- Each thread reads its *own* counter with `relaxed`, because nobody else writes it.
- `in_`, `out_` and the data sit on separate 64-byte lines (`alignas(64)`) so that the producer's stores to `in_` do not keep invalidating the line the consumer is reading `out_` from (false sharing; 64 is the line size of the machine used here, and I did not measure the effect).

Both operations finish in a bounded number of steps whatever the other thread does (they return `false` instead of waiting), so this is *wait-free*, the strongest class in tutorial 07 of linked lists. It is correct for **one** producer and **one** consumer only; two producers would both write `in_`.

### 4.2 How it was tested

One test pushes numbers through a 64-slot ring (so the indexes wrap thousands of times) with the consumer checking that it receives 0, 1, 2 and so on in order: 2,000,000 numbers at `-O2` and 300,000 under ThreadSanitizer, which is slower. ThreadSanitizer reports nothing. Then I changed each of the four ordered operations (`in_.store`, `out_.store`, and the two acquire loads) to `relaxed`, one at a time. ThreadSanitizer reported a data race in every one of the four mutants. **The value checks still passed in all four** on this x86 machine, because x86 hardware does not reorder these particular accesses; a program that "works" proves nothing about missing ordering, which is why the race detector is part of the test. A fifth mutant, a wrong full test (`N + 1`), was caught by the value checks.

### 4.3 The same shape in real systems

- **`io_uring`.** The submission queue and the completion queue are rings shared between a program and the kernel. In `include/uapi/linux/io_uring.h` the structures `io_sqring_offsets` and `io_cqring_offsets` list `head`, `tail`, `ring_mask` and `ring_entries` for each ring. The `liburing` helper that finishes a completion advances the ring's head with a release store (`io_uring_cq_advance`), and the code that counts waiting completions reads the tail with an acquire load, the pattern of `SpscRing`. (`liburing` master, fetched 2026-11-01.)
- **`kfifo`**, section 3.4.
- **Your `rkdma` project** (milestone M2 in [its spec](../../14_projects/02_rkdma_dma_driver/SPEC.md)) maps a ring into user space with the producer and consumer indexes on separate cache lines. This section is that design in 30 lines; the project adds DMA and the barriers a device needs.

To go beyond one producer and one consumer, put a mutex around an ordinary queue first. A lock-free multi-producer queue (Michael and Scott's, in words in tutorial 07 of linked lists, section 2.8) is for when measurement says the lock is the bottleneck.

---

## 5. A queue from two stacks

### 5.1 The idea

LeetCode 232 asks for a queue built from stacks only. Keep two. New elements are pushed on the **in** stack. To pop, take from the **out** stack. When **out** is empty, pour the whole **in** stack into it, one element at a time; the pouring reverses the order, so the oldest element ends up on top.

```cpp
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
```

Run: push 1, 2, 3, pop, push 4, pop, pop, pop (stacks are listed bottom to top):

```text
push 1             in=[1]       out=[]        transfers=0
push 2             in=[1 2]     out=[]        transfers=0
push 3             in=[1 2 3]   out=[]        transfers=0
pop      -> 1      in=[]        out=[3 2]     transfers=3
push 4             in=[4]       out=[3 2]     transfers=3
pop      -> 2      in=[4]       out=[3]       transfers=3
pop      -> 3      in=[4]       out=[]        transfers=3
pop      -> 4      in=[]        out=[]        transfers=4
```

The first `pop` pours three elements, reversing `[1 2 3]` into `[3 2]` with 1 handed out. Later pops are free until `out` is empty again. Note that 4 stayed on `in` until `out` ran dry: pouring while `out` still holds elements would put 4 under 3, and 4 would come out before them. The test is `if (!out_.empty()) return;`, and getting that line wrong is the common bug.

### 5.2 Why it is O(1) amortised

One `pop` can cost O(n): the first pop above poured every element. Yet any run of operations is cheap, because **each element is poured at most once**. Put it as coins: charge every `push` four coins, one for each stack operation the element will ever need (push on `in`, pop from `in`, push on `out`, pop from `out`). When an element is poured or popped, its coins pay for it. No one ever runs out, so `m` operations cost at most `4m` stack operations, which is O(1) each on average. The test checks the key fact directly, `transfers() <= pushes` after every operation, over 320,000 random operations.

What amortised does not give you is a bound on a *single* call. If one `pop` must never take long (a real-time loop), use the ring buffer.

### 5.3 The other way: a stack from a queue

LeetCode 225 asks for the reverse. A queue gives the oldest first, so after each `push` rotate the older elements behind the new one; the new element is now at the front, where a stack's top should be:

```cpp
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
```

`pop` and `top` are O(1), but the `i`-th push does `i - 1` rotations, so `n` pushes cost `n(n - 1) / 2` queue moves. Unlike the two-stack queue, this cost is not spread out. That is the answer to the usual follow-up "can you use one queue?": yes, and push pays.

---

## 6. The min-stack and its relatives

### 6.1 The problem

Support `push`, `pop`, `top` and `getMin`, all in O(1). A scan for the minimum is O(n). The trick: the minimum of the stack *at the moment an element was pushed* never changes while that element is on the stack, so store it with the element.

```cpp
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
```

Pushing 5, 3, 3, 7, 2 gives the pairs (5,5), (3,3), (3,3), (7,3), (2,2). Pop the 2 and the minimum is 3 again, because the pair below it says so. Every operation is O(1) and the extra space is one `int` per element.

### 6.2 One stack of differences

The pair version stores the minimum many times. A well-known interview trick stores one number per element: the difference between the element and the minimum *before* it was pushed. A negative difference marks a new minimum, and the old minimum can be recovered from it.

```cpp
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
```

The same sequence, with the stored differences:

```text
push 5   stored=0   min=5  top=5
push 3   stored=-2  min=3  top=3
push 3   stored=0   min=3  top=3
push 7   stored=4   min=3  top=7
push 2   stored=-1  min=2  top=2
pop      stored=4   min=3  top=7
pop      stored=0   min=3  top=3
pop      stored=-2  min=3  top=3
pop      stored=0   min=5  top=5
pop      (empty)
```

When 3 is pushed on top of 5 the stored value is -2 (negative: a new minimum, and `min_` becomes 3). On the pop, `min_ -= d` gives `3 - (-2) = 5`, the old minimum. Equal values store 0, so a repeated minimum is handled. Two cautions: the differences need a wider type than the values (here `long long` for `int` values), so on `int` values this uses the same eight bytes per element as the pair version and its only advantage is that it needs no second field; and with 64-bit values the difference itself can overflow. Treat it as a puzzle to be able to explain, not as a better design.

A third form keeps a second stack of minima, pushing `x` there only when `x` is at most the current minimum, and popping from it when the popped value *equals* its top. The comparison must be `<=`: with `<`, a repeated minimum is pushed once but popped twice (paper problem E).

### 6.3 A queue with a minimum

Put the min-stack to work inside the two-stack queue of section 5: use two min-stacks. The minimum of the queue is the smaller of the two stacks' minima.

```cpp
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
```

Every operation is O(1) amortised, with the same coin argument as before. Replace `std::min` with `std::max` and you have a queue with a maximum. This gives the *sliding window minimum* in O(n) (push the new element, pop the oldest, read `min()`), the problem that tutorial 03 solves with a monotonic deque instead; compare the two when you get there. The cp-algorithms article "Minimum Stack / Minimum Queue" covers both.

LeetCode 716, Max Stack, adds `popMax`, which removes the largest element wherever it is. The pair trick cannot do that; see problem 7.

---

## 7. How fast are they?

Push 100,000 numbers, then pop them all (best of five runs, milliseconds), and a steady state where 1,000 elements are waiting and we push one and pop one 10,000,000 times (nanoseconds per push and pop pair):

| Queue | Push 100,000, then pop all (ms) | Steady state (ns per push and pop) |
|---|---|---|
| `FrontEraseQueue` | 402 | not run |
| `HeadIndexQueue` | 0.35 | not run (memory grows without bound) |
| `std::queue` | 0.20 | 1.5 |
| `RingQueue` | 0.54 | 1.1 |
| `FixedRing` | 0.13 | 1.1 |
| `TwoStackQueue` | 0.85 | 3.1 |

Three readings, each from the table. First, `FrontEraseQueue` is hopeless, as section 3.1 said. Second, in the batch test `std::queue` beats the growing `RingQueue`: the ring copies its elements at every doubling (about one extra move per element in total) and a `std::deque` never copies its elements. `FixedRing`, with no growth at all, is fastest. Third, in the steady state, when nothing grows, the rings are slightly ahead of the deque (one array, no block table), and the two-stack queue is about three times slower because each element is touched three times. All of these are a few nanoseconds per operation, so for most programs the standard `std::queue` is the right choice; a ring wins when you need no allocation, bounded memory, or a fixed layout shared with hardware or another thread. The program was run three times; each figure is the median of the three runs, and each run takes the best of five repetitions. The spread between runs was small: `FrontEraseQueue` at 100,000 took 391 to 415 ms and `RingQueue` 0.48 to 0.56 ms.

---

## 8. Which one to use

| You need | Use |
|---|---|
| A stack | `std::vector` with `push_back`, `back`, `pop_back`, or `std::stack<T, std::vector<T>>` |
| A queue | `std::queue<T>` |
| Both ends, or references that stay valid | `std::deque<T>` |
| No allocation, or memory bounded in advance | `FixedStack`, `FixedRing` |
| One thread writing, one reading | `SpscRing` |
| Many writers or many readers | a mutex around a queue; a lock-free queue only after measuring |
| "Make a queue from stacks" (232) | two stacks, pour when `out` is empty |
| "Make a stack from a queue" (225) | one queue, rotate after each push |
| "Minimum in O(1)" (155) | pairs; or two min-stacks for a queue |

---

## 9. Common mistakes

1. **Reading an empty container.** `top()` or `front()` on empty is undefined behaviour. Check `empty()` first, or assert.
2. **Dequeuing with `erase(begin())`.** O(n) per pop (section 3.1).
3. **Full and empty look the same.** With `head` and `tail` only, both are `head == tail`; use a size, a spare slot or free-running indexes.
4. **Copying the raw array when a ring grows.** Copy in queue order from `head_` (section 3.5).
5. **A free-running index with a capacity that is not a power of two.** The slots skip at the wrap (section 3.4).
6. **Pouring when `out` is not empty** in the two-stack queue. The new elements end up in front of older ones.
7. **A strict `<` in the second-stack min-stack.** Duplicates of the minimum break it.
8. **Forgetting the `int` overflow** in the difference trick (`x - min` for values near the ends of the range).
9. **Two producers on an `SpscRing`.** It is for one producer and one consumer.
10. **Trusting a passing test for atomics.** A relaxed store passes every value check on x86. Run ThreadSanitizer.

---

## 10. Questions to answer out loud

1. Why does `std::stack::pop` return `void`? (section 1.3)
2. Build a queue on an array so that every operation is O(1). How do you tell full from empty? (3.2, 3.3)
3. With free-running indexes, why must the capacity be a power of two, and why is `in - out` right after `in` wraps? (3.4)
4. What does a ring do when it must grow, and what is the classic bug? (3.5)
5. Prove that the queue from two stacks is O(1) amortised. What is the worst single `pop`? When would you refuse to use it? (5.2)
6. How do you make a stack from queues? What does each `push` cost? (5.3)
7. Make `getMin` O(1) on a stack. Now do it with one number of extra space. What can go wrong? (6.1, 6.2)
8. Two threads share a ring and there is no lock. Which loads and stores need acquire and release, and what goes wrong with relaxed? (4.1, 4.2)
9. When would you pick a ring buffer over `std::queue`? (7, 8)
10. Where does this ring show up in the Linux kernel? (3.4, 4.3)

---

## 11. Problems

### 11.1 On paper

**Problem A. Trace a fixed ring.**
*Where:* paper.
*Goal:* see `in`, `out` and the slots move.
*How:* start with an empty `FixedRing<int, 4>` (`in` = `out` = 0). Run: push 1, 2, 3, 4, then push 5, pop, pop, push 5, push 6, pop, push 7. After each operation write `in`, `out`, the size and the four slots. Which operation fails, and why does the failed push change nothing?

**Problem B. A capacity that is not a power of two.**
*Where:* paper.
*Goal:* see why the kernel rounds the size up.
*How:* imagine `FixedRing` with `N` = 6 and the slot computed as `index % 6`, with 32-bit free-running indexes. (a) Find the slots of the indexes 4294967291 to 4294967295 and then 0. (Use `2^32 mod 6 = 4`.) (b) Six elements are pushed with exactly these indexes and none is popped. Which two share a slot, and what happens to the older one? (c) What does `N` = 8 with `& 7` give for the same indexes?

**Problem C. Count the pours.**
*Where:* paper, using `TwoStackQueue`.
*Goal:* count the work and find the worst call.
*How:* push 1, 2, 3, 4, pop, push 5, pop, pop, pop, pop. After each operation write both stacks (bottom to top). How many elements were poured in total? How many stack operations in all? Which single call was the most expensive, and how many stack operations did it perform?

**Problem D. Decode the differences.**
*Where:* paper, using `MinStackEncoded`.
*Goal:* trace the encoding both ways.
*How:* push 4, 6, 2, 2, 9, 1. After each push write the stored difference, the minimum and the top. Then pop all six, writing the minimum and the top (if any) after each pop.

**Problem E. The strict comparison.**
*Where:* paper.
*Goal:* find the bug in a plausible design.
*How:* a min-stack keeps a second stack of minima. `push(x)` also pushes `x` on the second stack when it is empty or `x` is *less than* its top. `pop()` pops the second stack when the popped value *equals* its top. Find a sequence of three operations after which `getMin` is wrong, and say what the fix is.

### 11.2 In your editor

**Problem 1. LeetCode 232, Implement Queue using Stacks.**
*Where:* LeetCode (Easy on the mirror sites I looked at), written in your editor first.
*Goal:* `push`, `pop`, `peek` and `empty` using only stack operations, each O(1) amortised.
*How:* two stacks and the pouring rule of section 5.1. Test against `std::queue` with random operations, and count the pours to confirm that they never exceed the pushes.

**Problem 2. LeetCode 225, Implement Stack using Queues.**
*Where:* LeetCode (Easy on the mirror sites).
*Goal:* a stack from queue operations only, then the follow-up: with one queue.
*How:* rotate after each push (section 5.3). Then make `push` O(1) and `pop` the costly one: how would you do it? Which is better for a program that pushes a million times and pops once?

**Problem 3. LeetCode 155, Min Stack.**
*Where:* LeetCode (Medium on the mirror sites).
*Goal:* `push`, `pop`, `top` and `getMin` in O(1).
*How:* the pair version first. Then the difference version: LeetCode's values run over the whole `int` range, so which type must the differences be? Test both against a brute-force minimum, with many duplicates and with `INT_MIN` and `INT_MAX`.

**Problem 4. LeetCode 622, Design Circular Queue.**
*Where:* LeetCode (Medium on the mirror sites; capacity at most 1000).
*Goal:* a fixed-capacity queue with `enQueue` that fails when full.
*How:* `head` plus a size is the simplest. Read the statement for what `Front` and `Rear` return when the queue is empty. Then redo it with `head` and `tail` and one wasted slot, and compare the two on the same tests.

**Problem 5. LeetCode 641, Design Circular Deque.**
*Where:* LeetCode (Medium on the mirror sites).
*Goal:* insert and delete at both ends, in a fixed capacity.
*How:* the ring of problem 4 with `insertFront` stepping `head` left (section 3.6). The easy mistake is the wrap below slot 0. Test against `std::deque` limited to `k` elements.

**Problem 6. A ring between two threads.**
*Where:* your editor, compiled with `-fsanitize=thread`.
*Goal:* feel what ThreadSanitizer finds.
*How:* write `SpscRing` from memory and run one producer and one consumer through a 64-slot ring, checking the order. Then change one `release` or `acquire` to `relaxed` and run again. Does the value check still pass? What does the sanitizer say? Then add a second producer and see what breaks.

**Problem 7. LeetCode 716, Max Stack (stretch).**
*Where:* LeetCode (marked premium and Hard on the mirror site I looked at), or your editor against a brute force.
*Goal:* `push`, `pop`, `top`, `peekMax`, `popMax` with `top` in O(1) and the others in O(log n).
*How:* the pair trick fails because `popMax` removes an element from the middle. Think about a structure that removes an arbitrary element in O(1) (a doubly linked list from linked lists 03) together with a structure that finds the maximum in O(log n) (an ordered map from value to the list nodes that hold it). Ties go to the top-most element.

**Problem 8. A queue with a minimum.**
*Where:* your editor.
*Goal:* `MinQueue` of section 6.3, then use it.
*How:* write it from the two-stack queue and the min-stack. Use it for the minimum of every window of size `k` over an array and check it against a version that uses `std::multiset`. Which of the two does fewer comparisons?

---

## Answers

**Problem A.** The ring from the program:

```text
push 1   ok    in=1 out=0 size=1 slots=[1 . . .]
push 2   ok    in=2 out=0 size=2 slots=[1 2 . .]
push 3   ok    in=3 out=0 size=3 slots=[1 2 3 .]
push 4   ok    in=4 out=0 size=4 slots=[1 2 3 4]
push 5   full  in=4 out=0 size=4 slots=[1 2 3 4]
pop      1     in=4 out=1 size=3 slots=[1 2 3 4]
pop      2     in=4 out=2 size=2 slots=[1 2 3 4]
push 5   ok    in=5 out=2 size=3 slots=[5 2 3 4]
push 6   ok    in=6 out=2 size=4 slots=[5 6 3 4]
pop      3     in=6 out=3 size=3 slots=[5 6 3 4]
push 7   ok    in=7 out=3 size=4 slots=[5 6 7 4]
```

The fifth push fails because `in - out` is 4 = `N`: the ring is full, and the function returns before it touches `data_` or `in_`, so nothing changes. After the two pops, the next push (the second 5) writes slot `4 & 3 = 0`, the slot of the already-popped 1.

**Problem B.** (a) `2^32 mod 6 = 4`, so an index of the form `2^32 - k` has the slot `(4 - k) mod 6`. The indexes 4294967291, ...92, ...93, ...94, ...95 are `2^32 - 5` to `2^32 - 1` and land on slots 5, 0, 1, 2, 3. The next index, 0, lands on slot 0. (b) Six pushes with the indexes 4294967291 to 4294967295 and 0 use the slots 5, 0, 1, 2, 3, 0. The last one overwrites slot 0, which holds the unread element of index 4294967292, and slot 4 is never used. (c) With `N` = 8 and `& 7`, since 8 divides `2^32`, `2^32 - k` has the slot `(8 - k) mod 8`: the indexes land on slots 3, 4, 5, 6, 7 and then 0, which continues the sequence correctly.

**Problem C.**

```text
push 1..4  in=[1 2 3 4]  out=[]
pop -> 1   in=[]         out=[4 3 2]     (poured 4)
push 5     in=[5]        out=[4 3 2]
pop -> 2   in=[5]        out=[4 3]
pop -> 3   in=[5]        out=[4]
pop -> 4   in=[5]        out=[]
pop -> 5   in=[]         out=[]          (poured 1: the 5)
```

Five elements were poured, one for each push. Each element costs four stack operations (push on `in`, pop from `in`, push on `out`, pop from `out`), so 20 in all. The most expensive call is the first `pop`: it pops four elements from `in`, pushes four on `out`, and then pops one from `out`, which is 9 stack operations.

**Problem D.**

```text
push 4   stored=0    min=4  top=4
push 6   stored=2    min=4  top=6
push 2   stored=-2   min=2  top=2
push 2   stored=0    min=2  top=2
push 9   stored=7    min=2  top=9
push 1   stored=-1   min=1  top=1
pop      min=2  top=9      (popped -1: min = 1 - (-1) = 2)
pop      min=2  top=2      (popped 7)
pop      min=2  top=2      (popped 0; the next stored value is -2, so top = min)
pop      min=4  top=6      (popped -2: min = 2 + 2 = 4)
pop      min=4  top=4      (popped 2; the next stored value is 0)
pop      empty             (popped 0)
```

**Problem E.** Push 2, push 2, pop, then `getMin`. The second push does not go on the second stack (`2 < 2` is false), so the second stack holds `[2]` while the main stack holds `[2, 2]`. The pop removes a 2 from the main stack, sees that it equals the top of the second stack and pops that too. The main stack still holds a 2, but the second stack is empty, so `getMin` has nothing to answer with. Fix: push when `x <= top`.

**Problem 1.** Both `push` and the pouring loop use only stack operations. The test compares every `front` and `pop` with `std::queue`; the pour count stays at most the push count, and one `pop` after `n` pushes pours `n` elements.

**Problem 2.** After `push(x)` put `x` on the queue and rotate the older `size - 1` elements to the back. Alternative: `push` is a plain enqueue and `pop` rotates `size - 1` elements, then dequeues; `top` costs O(n) too. For a million pushes and one pop, the second is far better: `n` pushes cost `n` instead of `n(n - 1) / 2` moves, and the one pop costs `n`.

**Problem 3.** The pair version uses `std::pair<int, int>`. For the difference version, `x - min` can be as large as `2^32 - 1` in magnitude, which does not fit in `int`, so use `long long`. Pop restores the minimum with `min -= d` when `d < 0`. The tests need duplicates (the 0 case) and the extreme values.

**Problem 4.** With `head` and `size`: `enQueue` writes `(head + size) % k` and fails when `size == k`; `deQueue` moves `head`. With `head`, `tail` and a spare slot, the array has `k + 1` slots, empty is `head == tail` and full is `(tail + 1) % (k + 1) == head`. Both agree on every operation; the second wastes one slot and needs no `size`.

**Problem 5.** `insertFront`: `head = (head + k - 1) % k` (with the size version), then write. `deleteLast`: decrease the size. `getRear`: slot `(head + size - 1) % k`. The wrap below zero is the line that goes wrong, as the section 3.6 code does it with the `head == 0` case.

**Problem 6.** With one producer and one consumer the value check passes in every variant on x86, even with `relaxed`, and ThreadSanitizer reports a data race on the slot in each mutant (section 4.2). With two producers, both read the same `in_`, both write the same slot, and elements are lost or overwritten; ThreadSanitizer reports a race even with the correct orderings, because two threads write `in_`.

**Problem 7.** Keep a doubly linked list of the elements in stack order and a `std::map<int, std::vector<list iterator>>` from value to the nodes holding it. `push` appends to the list and to the vector of its value; `top` is the list's back (O(1)); `pop` erases the back node and the last entry of its value's vector; `peekMax` is the last key of the map and the last node in its vector; `popMax` erases that node from the list and the vector (the iterator makes it O(1)) and removes the key when the vector empties. The map operations are O(log n). The last node in the vector is the top-most one with that value, which breaks ties as the statement asks.

**Problem 8.** `MinQueue::min()` combines the two stacks. For the sliding window: push `a[i]`; once the queue holds `k` elements read `min()`, then call `pop()` to drop the oldest element before the next step. Each element is pushed, poured and popped once, so the total is O(n); the multiset version is O(n log k). Count comparisons with a counting comparator to see the difference.

---

## Cheat sheet

- **Stack:** LIFO; `push`, `pop`, `top` in O(1); a `std::vector` is the default. `std::stack` and `std::queue` wrap a `deque`; `pop()` returns `void`.
- **Queue trap:** `erase(begin())` is O(n); a moving head index never frees space; a ring does both right.
- **Ring:** `head` and `size`, or free-running unsigned `in` and `out` with capacity a power of two (size `in - out`, slot `index & (N - 1)`, the Linux `kfifo` design). When growing, copy in queue order.
- **Two threads:** one producer writes `in`, one consumer writes `out`; release store to publish, acquire load to read the other side; relaxed for your own counter; wait-free; test with ThreadSanitizer, because the values still look right.
- **Queue from two stacks:** push on `in`; pour into `out` only when `out` is empty; O(1) amortised, O(n) for one call.
- **Stack from a queue:** rotate after each push; push is O(n), `n` pushes cost `n(n - 1) / 2` moves.
- **Min-stack:** store the minimum with each element; or a difference with `long long`; with a second stack of minima use `<=`.
- **Min-queue:** two min-stacks; the minimum is the smaller of the two.
- **Default:** `std::vector` for a stack, `std::queue` for a queue, a ring when you need no allocation or a fixed layout.

---

## Sources

- libstdc++ (GCC 13.3) headers `bits/stl_stack.h`, `bits/stl_queue.h` (default container `deque<_Tp>`, `pop` returns `void`) and `bits/stl_deque.h` (`_GLIBCXX_DEQUE_BUF_SIZE` is 512).
- Linux kernel, `master` branch as fetched on 2026-11-01: [`include/linux/kfifo.h`](https://github.com/torvalds/linux/blob/master/include/linux/kfifo.h) (`struct __kfifo`, `kfifo_len`, the locking note) and [`lib/kfifo.c`](https://github.com/torvalds/linux/blob/master/lib/kfifo.c) (`roundup_pow_of_two`, `kfifo_unused`, `smp_wmb` before `in` is advanced).
- `io_uring`: [`include/uapi/linux/io_uring.h`](https://github.com/torvalds/linux/blob/master/include/uapi/linux/io_uring.h) (`io_sqring_offsets`, `io_cqring_offsets`) and `liburing` [`src/include/liburing.h`](https://github.com/axboe/liburing/blob/master/src/include/liburing.h) and [`src/include/liburing/barrier.h`](https://github.com/axboe/liburing/blob/master/src/include/liburing/barrier.h) (`io_uring_cq_advance`, `io_uring_smp_store_release`, `io_uring_smp_load_acquire`).
- cp-algorithms, [Minimum Stack / Minimum Queue](https://cp-algorithms.com/data_structures/stack_queue_modification.html).
- LeetCode problems, with labels taken from mirror sites because LeetCode itself could not be opened from here: [155](https://brain-leetcode.pages.dev/01xx/155/) (Medium; values over the whole `int` range, calls on non-empty stacks only), [232](https://brain-leetcode.pages.dev/02xx/232/) (Easy; the follow-up asks for amortised O(1)), [225](https://ycj.codeberg.page/problems/0225-implement-stack-using-queues.html) (Easy; the follow-up asks for one queue), [622](https://brain-leetcode.pages.dev/06xx/622/) (Medium; `k` at most 1000, at most 3000 calls), [641](https://leetcode.doocs.org/en/lc/641/) (Medium; `k` from 1 to 1000), [716](https://leetcode.doocs.org/en/lc/716/) (Hard, shown with a lock mark, which means premium; values within 10^7 in magnitude, at most 10^5 calls, `top` in O(1), the rest in O(log n)).
- Sedgewick and Wayne, *Algorithms*, section 1.3, and CLRS section 10.1, for the array-based stack and queue.

---

**Next:** tutorial 02, the monotonic stack (next greater element, daily temperatures, the largest rectangle in a histogram, trapping rain water). Say "next" when you are ready.
