# 03 — Pointers, stack vs. heap, and why C++ moves away from raw `new`/`delete`

> If you've written embedded C, most of this file will feel familiar —
> it's mostly naming things you already do. The important new idea is at
> the end: **why C++ code in this program almost never calls `new` or
> `delete` directly.**

## Stack vs. heap, quickly

```cpp
void f() {
    int x = 5;              // stack: exists only inside f(), destroyed when f() returns
    int* p = new int(10);   // heap: exists until you explicitly delete it
    delete p;                // without this line, that memory leaks -- never freed
}
```

- **Stack** memory (local variables) is automatic — created when you enter
  a scope (a function, a `{ }` block), destroyed when you leave it. Fast,
  and you can never forget to clean it up because the compiler does it
  for you.
- **Heap** memory (`new`) is manual — it exists until *you* say `delete`,
  no matter when the function that created it returns. This is exactly
  what lets you build a data structure that outlives the function that
  created it (a linked list node, an object a `Game` needs to keep around
  after `setup()` returns).

## Pointers — same as C

```cpp
int x = 5;
int* p = &x;      // p holds the address of x
std::cout << *p;  // *p dereferences p: "the value at the address p holds" -> 5
*p = 10;          // changes x itself, through the pointer -> x is now 10
```

`&x` = "address of x." `*p` = "the value pointed to by p." If this is
already second nature from C, you're already most of the way there — the
only genuinely new things are `nullptr` (C++'s spelling of a null pointer,
prefer it over C's `NULL` or a bare `0`) and everything below.

## The problem with raw `new`/`delete`

```cpp
void processOrder() {
    Order* order = new Order();
    if (somethingGoesWrong()) {
        return;              // BUG: never reached `delete order` -- memory leak
    }
    order->process();
    delete order;
}
```

Every early `return`, every exception thrown partway through, every extra
code path added six months later by someone else is a place a matching
`delete` can be forgotten. This isn't a hypothetical — it's the single
most common category of real bug in manually-memory-managed C++ (and C).

## The fix: let an object's lifetime manage the memory for you

```cpp
void processOrder() {
    auto order = std::make_unique<Order>();   // heap-allocates an Order
    if (somethingGoesWrong()) {
        return;    // order's memory is freed automatically here -- guaranteed
    }
    order->process();
    // order's memory is freed automatically here too, whichever path was taken
}
```

`std::unique_ptr<Order>` is a small wrapper object that **owns** a heap
allocation and automatically calls `delete` on it when the wrapper itself
goes out of scope — the same automatic-cleanup guarantee your stack
variables already get, extended to heap memory. This is called **RAII**
(Resource Acquisition Is Initialization), and it's arguably the single
most important idea in modern C++ — `10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`
covers it in full depth once you're through this primer; this file is
just giving you the syntax so that discussion isn't your first exposure
to any of it.

**Practical rule for this entire program**: you will see `new` and
`delete` used directly almost nowhere in the actual tutorial solutions.
Instead you'll see `std::make_unique<T>(...)` (file `12` covers this in
full) constantly. If you find yourself wanting to write `new`, that's
usually a sign you want a smart pointer instead — flag it and check file
`12` or `10_lld/00_foundations/01`.

## `nullptr`

```cpp
int* p = nullptr;      // explicitly "points at nothing"
if (p == nullptr) { /* ... */ }
if (!p) { /* same check, common shorthand */ }
```

Use `nullptr`, not `NULL` or `0`, when you mean "no pointer" in C++ —
it's type-safe in ways the old C spellings aren't (a detail that matters
more in edge cases than day to day, but the habit is worth building now).

## AddressSanitizer (ASan) — catching these bugs automatically

Memory bugs (use-after-free, reading past the end of an array, and on
some platforms, leaks) are exactly the kind of thing that can silently
"seem to work" for months and then fail unpredictably. **AddressSanitizer
(ASan)** is a compiler mode — turned on with `-fsanitize=address` — that
inserts extra checking around every memory operation, so these bugs are
caught immediately, with an exact report, instead of causing mysterious
behaviour later:

```bash
clang++ -std=c++20 -fsanitize=address -g program.cpp -o program
./program
```

If `program` touches memory incorrectly, ASan stops it immediately and
prints exactly which line did the bad access, and (for a use-after-free)
exactly which earlier lines allocated and freed that memory. This is the
single most useful debugging tool for the class of bug C++ makes possible
— you'll use `-fsanitize=address` constantly throughout this program, and
it's already on by default in the practice project's `CMakeLists.txt`.

**One honest platform note**: ASan's *leak-detection* feature specifically
is not supported on macOS (it works on Linux) — everything else about
ASan (use-after-free, buffer overflows) works identically on your Mac. If
you want to specifically hunt leaks on macOS, the built-in `leaks`
command-line tool (already installed) is the native alternative, though
it's less beginner-friendly to use than ASan's automatic reports.

## The four cast operators — and why C++ wants you to stop writing `(Type)x`

C has one cast syntax, `(int)x`, that means five different things depending
on what `x` and `int` are — which is exactly the problem. C++ splits that
one dangerous syntax into four named casts, each doing *one* specific,
searchable thing:

```cpp
double d = 3.9;
int i = static_cast<int>(d);          // 1. ordinary, checked-at-compile-time
                                        //    conversion between related types
                                        //    (numeric, or up/down a class
                                        //    hierarchy you're sure about)

const std::string& cref = someString;
std::string& ref = const_cast<std::string&>(cref);   // 2. adds or removes
                                        // const -- the only cast that touches
                                        // const/volatile, nothing else. Rare,
                                        // and a signal something upstream is
                                        // designed wrong if you need it often.

Base* b = getBase();
if (Derived* d2 = dynamic_cast<Derived*>(b)) {   // 3. checked downcast through
    d2->derivedOnlyMethod();                      //    a polymorphic hierarchy --
}                                                  //    returns nullptr (for a
                                                    //    pointer) if b isn't
                                                    //    actually a Derived*,
                                                    //    instead of corrupting
                                                    //    memory

// reinterpret_cast<T*>(rawBytes)   // 4. reinterprets raw bits as a different,
                                     //    UNRELATED type -- low-level/embedded
                                     //    territory (parsing a byte buffer as
                                     //    a struct), essentially never needed
                                     //    in the LLD problems this program covers
```

**Why not just `(int)d` like C?** That one syntax silently picks whichever
of these four (or a combination) the compiler thinks you meant, with no way
to grep for "every place this codebase reinterprets memory" or "every place
it strips `const`" — both of which are exactly the lines you want to find
fast when something's gone wrong. Naming the cast names the risk.

**The one you'll actually reach for constantly**: `static_cast` — for
ordinary numeric conversions (`static_cast<double>(intValue)` to avoid
integer division, e.g.) and for the CRTP pattern's compile-time downcast
you saw in `10_lld/00_foundations/06_cpp_design_idioms.md`. **The one you'll reach
for rarely, and only in tests**: `dynamic_cast`, to check *which* concrete
type a factory actually produced —
`10_lld/00_foundations/15_testing_gtest_and_gmock.md` names this as one of the few
legitimate uses; production code that finds itself needing `dynamic_cast`
usually has a design smell (a missing virtual method, an interface that
isn't abstract enough) rather than a genuine need to ask an object what it
really is. **`const_cast`**: reach for it only to satisfy a legacy API that
takes a non-`const` pointer/reference but you know it won't actually
mutate — never to legitimately bypass a `const` contract you wrote
yourself. **`reinterpret_cast`**: essentially never in this program;
flagged here so the name isn't a total surprise if you see it in someone
else's embedded/systems code.

## Arrays, briefly, and why you'll rarely see raw C-style arrays here

```cpp
int arr[5] = {1, 2, 3, 4, 5};   // C-style array: works, but size is fixed and separate from the data
```

You'll see this occasionally (e.g. a fixed 3x3 Tic-Tac-Toe board using
`std::array`, a safer typed wrapper around exactly this). For anything
that needs to grow, shrink, or whose size isn't known at compile time,
C++ code uses `std::vector` instead — covered in file `08`, and it's what
you'll actually reach for almost everywhere a C programmer would reach
for `malloc` + a raw array.

## Exercises

1. Write a program that allocates an `int` with `new`, `delete`s it, then
   reads through the now-dangling pointer anyway (a **use-after-free** —
   works identically on macOS and Linux, unlike leak detection). Compile
   and run it under ASan:
   ```bash
   clang++ -std=c++20 -fsanitize=address -g program.cpp -o program && ./program
   ```
   Read the report: find the three line numbers it gives you (the bad
   read, the `delete`, and the original `new`) in your own source file.
   Then write a second version of the same idea using a leak instead
   (allocate with `new`, return early without `delete`) — on macOS this
   one will *not* be caught by ASan (the platform gap explained above);
   confirm that for yourself by noticing the program exits cleanly with no
   report, which is itself worth seeing once. Finally, fix **both**
   versions using `std::make_unique` instead of `new`/`delete`, and
   confirm each still behaves correctly with no manual cleanup code at all.
2. Write a small function taking an `int*` parameter that might be
   `nullptr`, check for it explicitly before dereferencing, and write a
   test calling it both with a valid pointer and with `nullptr`.
3. Declare a `std::array<int, 5>` (you'll need `#include <array>`),
   fill it with values, and print them in a loop — this is the exact type
   used for the Tic-Tac-Toe board in this program's first machine-coding
   problem.
4. Write a small polymorphic `Animal` base class with a `Dog` and `Cat`
   derived from it, each with one method the other doesn't have
   (`Dog::fetch()`, `Cat::scratch()`). Store an `Animal*` that actually
   points at a `Dog`, and use `dynamic_cast` to check whether it's a `Dog`
   before calling `fetch()` on it. Then confirm what `dynamic_cast` returns
   when you try to cast that same pointer as if it were a `Cat`.
