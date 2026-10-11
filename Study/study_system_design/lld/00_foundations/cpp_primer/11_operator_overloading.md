# 11 — Operator overloading: why `a == b` can run your own code

## What this is

C++ lets you define what `==`, `<`, `+`, and other operators *mean* for
your own classes — so `account1 == account2` or `seatA < seatB` can call
code you wrote, the same way `+` already means something specific for
`int` and something different for `std::string` (concatenation).

## A simple example

```cpp
struct Point {
    int x, y;

    bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

Point p1{1, 2};
Point p2{1, 2};
std::cout << (p1 == p2);   // true -- calls your operator==
```

`operator==` is just a specially-named member function — `p1 == p2` is
shorthand the compiler translates into `p1.operator==(p2)`. Once you see
that, operator overloading stops looking magic: it's a function like any
other, with a special name and special call syntax.

## `operator<` — needed for sorting and ordered containers

```cpp
struct Task {
    int priority;
    bool operator<(const Task& other) const {
        return priority < other.priority;
    }
};

std::vector<Task> tasks = { {3}, {1}, {2} };
std::sort(tasks.begin(), tasks.end());   // uses your operator< automatically
```

**This is exactly why `std::priority_queue` and `std::sort` work on your
own types** — they call `operator<` internally to decide ordering.
You'll see this precise pattern in
`01_machine_coding_problems/10_task_scheduler_cron.md`'s `ScheduledTask`:

```cpp
struct ScheduledTask {
    Clock::time_point nextRun;
    bool operator<(const ScheduledTask& other) const { return nextRun > other.nextRun; }
    // ^ inverted on purpose: std::priority_queue is a max-heap by default,
    //   so "less than" here is defined backward to get the earliest time on top
};
```

If you ever see a class define `operator<` with what looks like the
"wrong" direction, that's usually this exact trick — worth recognizing on
sight now rather than being confused by it later.

## `= default` — auto-generating an operator

```cpp
struct Square { int row, col; bool operator==(const Square&) const = default; };
```

For simple structs, C++ can generate a member-by-member `==` for you —
`= default` instead of writing the comparison logic yourself. You'll see
this exact line in `01_machine_coding_problems/14_chess.md`'s `Square`
struct.

## `operator<<` — teaching `std::cout` how to print your own type

```cpp
struct Point {
    int x, y;
};

std::ostream& operator<<(std::ostream& os, const Point& p) {
    os << "(" << p.x << ", " << p.y << ")";
    return os;
}
```

```cpp
Point p{3, 4};
std::cout << p << "\n";   // (3, 4) -- calls your operator<< automatically
```

This is the one overload on this page that **isn't** a member function —
notice it's written as a standalone function, not inside `Point`'s body,
and it takes *two* parameters (the stream, and the thing to print) rather
than one. That's because `std::cout << p` is really asking "is there an
`operator<<` that takes a stream on the left and a `Point` on the right?"
— and that function needs to be free-standing so the stream can be on the
left. `os << ...` returns `os` again (by reference) specifically so
chained calls like `std::cout << p << "\n"` keep working, the same
chaining idea as `return *this` from `00_foundations/cpp_primer/04_classes_and_objects.md`.

**Why this is worth knowing early**: without it, `std::cout << myObject`
is a compile error — "no operator<< for this type" — which you will hit
constantly the moment you try to debug a class by printing it. Add this
overload once per class you're actively debugging, and `std::cout <<`
just works from then on.

## `operator()`  — making an object callable like a function

```cpp
struct AreaVisitor {
    double operator()(const Circle& c) const { return 3.14159 * c.r * c.r; }
    double operator()(const Square& s) const { return s.side * s.side; }
};

AreaVisitor visitor;
double a = visitor(myCircle);   // calls operator()(const Circle&)
```

An object with `operator()` defined is called a **functor** — you use it
with function-call syntax (`visitor(x)`) even though it's really an
object. This is exactly how `00_foundations/09_design_patterns_behavioral.md`'s
`std::variant`/`std::visit` section works, and it's the mechanism
underneath every lambda from file `10` (a lambda is, under the hood, the
compiler generating an anonymous class with an `operator()` for you —
recognizing that connection is a nice "click" moment once file `10`
is comfortable).

## `operator[]` — making your type indexable like an array

```cpp
class Board {
public:
    Board() : cells_(9, ' ') {}
    char& operator[](int index) { return cells_[index]; }               // read/write
    const char& operator[](int index) const { return cells_[index]; }   // read-only
private:
    std::vector<char> cells_;
};

Board b;
b[0] = 'X';               // calls the non-const overload -- assigns through the reference
std::cout << b[0];        // 'X'
```

Two overloads, not one, is the pattern to remember: the non-`const` version
returns a plain reference so `b[0] = 'X'` can assign through it; the
`const` version (called when `b` itself is `const`, or accessed through a
`const Board&`) returns a `const` reference, so it can be read but not
written — the same read/write-vs-read-only distinction `06_const_correctness.md`
covers for regular member functions, applied to indexing specifically.
You'll see close variants of this exact pattern in board/grid-based
problems in this program (`01_tic_tac_toe.md`, `14_chess.md`,
`15_snake_and_ladder.md`) wherever a `Board` or `Grid` class wraps an
underlying `std::vector`/`std::array` and wants callers to write
`board[row][col]` instead of `board.at(row, col)`.

## `operator=` — copy assignment, written out in full

`00_foundations/01_raii_ownership_and_move_semantics.md`'s Rule of
Three/Five names copy assignment as one of the "big five" special member
functions; here's what actually writing one by hand looks like, and the
one bug almost everyone writes into their first attempt:

```cpp
class Buffer {
public:
    explicit Buffer(size_t n) : size_(n), data_(new int[n]) {}
    ~Buffer() { delete[] data_; }

    Buffer& operator=(const Buffer& other) {
        if (this == &other) return *this;   // <-- self-assignment check: without
                                              //     this, `buf = buf;` would delete
                                              //     data_ and then immediately try
                                              //     to copy from the now-freed memory
        delete[] data_;                      // free what we currently own
        size_ = other.size_;
        data_ = new int[size_];
        std::copy(other.data_, other.data_ + size_, data_);
        return *this;                        // enables chaining: a = b = c
    }
private:
    size_t size_;
    int* data_;
};
```

**The bug this guards against**: without the `if (this == &other)` check,
`someBuffer = someBuffer;` (rare directly, but easy to hit indirectly
through a reference or pointer alias) would `delete[]` `data_`, and then
try to read from `other.data_` — which is the *same*, now-freed memory.
Every hand-written `operator=` in this program's style needs that check
first, unconditionally.

**Why you'll almost never actually write this in practice**: this is
exactly the Rule of Three/Five territory `00_foundations/01` warns you
away from — a class built from RAII members (`std::vector`, `unique_ptr`)
instead of a raw `int* data_` gets a *correct* compiler-generated
`operator=` for free, with no self-assignment bug possible, because each
member's own `operator=` already handles it. Seeing the hand-written
version once, here, is what makes "Rule of Zero" land as an actual
simplification rather than an abstract recommendation.

## What you actually need to remember

You will **read** overloaded operators constantly in this program's code
(`operator<`, `operator==`, `operator()`, `operator[]`). You'll **write**
them occasionally — mostly `operator<` for anything going into a
`std::priority_queue` or `std::set`, `operator==` for anything compared or
used as a map/set key, and `operator[]` for any board/grid-shaped class.
You'll almost never hand-write `operator=` (see above — Rule of Zero makes
it unnecessary), but you should be able to write it correctly, self-
assignment check included, if asked to in an interview. You don't need the
full list of overloadable operators memorized — recognizing the shape
(`operatorX(...) const { ... }` defines what `X` means for this type) is
what matters.

## Exercises

1. Write a `Money` struct with cents as an `int` member, and overload
   `operator<` and `operator==` for it. Put several `Money` values in a
   `std::vector` and sort them.
2. Write a small functor (a struct with `operator()`) that adds a fixed
   offset to whatever number it's called with, construct one with offset
   `10`, and call it like a function: `myFunctor(5)` should give `15`.
3. Take the `ScheduledTask` example above, explain in one sentence (in a
   comment) why its `operator<` is inverted, and confirm you understand
   it by writing a tiny `std::priority_queue<ScheduledTask>` that pops
   items in ascending `nextRun` order despite the inverted comparator.
4. Add an `operator<<` for your `Money` struct from exercise 1 (print it
   as dollars and cents, e.g. `$4.20`), then put several `Money` values in
   a `std::vector` and print all of them in a range-based for loop using
   `std::cout << m << "\n"` — confirm you never had to write a separate
   "print this" function once `operator<<` existed.
5. Write a small `Grid` class wrapping a `std::vector<int>` sized `rows *
   cols`, with `int& operator[](int index)` and a `const` overload. Use it
   to set and read a few cells. Then add a second, 2D-feeling accessor
   `int& at(int row, int col)` that computes `row * cols + col` internally
   — compare the two call sites (`grid[5]` vs `grid.at(1, 2)`) and note
   which one is harder to misuse.
6. Write the `Buffer` class from the `operator=` example above in full
   (constructor, destructor, and the hand-written `operator=`), then
   deliberately write `Buffer b(5); b = b;` to exercise the self-assignment
   path. Comment out the `if (this == &other) return *this;` line, compile
   and run it under ASan (`-fsanitize=address`), and read the
   use-after-free report it produces — then restore the check and confirm
   the same test passes cleanly.
