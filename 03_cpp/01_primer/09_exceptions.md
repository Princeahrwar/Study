# 09 — Exceptions: `try`, `catch`, `throw`

## The problem exceptions solve

In C, when a function fails, it usually signals that through a return
value (`-1`, `NULL`, an error code) — and it's entirely up to the caller
to remember to check it. Forget one check, and the error silently
propagates as garbage data instead of stopping the program where the
actual problem occurred.

C++ adds a second mechanism: **throwing an exception**, which
immediately stops normal execution and unwinds up the call stack until
something explicitly handles it — you genuinely cannot "forget" to
notice an uncaught exception the way you can forget to check a return
code, because an uncaught one crashes the program with a clear message
rather than silently continuing with bad data.

## The basic shape

```cpp
#include <stdexcept>

double divide(double a, double b) {
    if (b == 0) {
        throw std::invalid_argument("division by zero");
    }
    return a / b;
}

int main() {
    try {
        double result = divide(10, 0);
        std::cout << result;          // never reached
    } catch (const std::invalid_argument& e) {
        std::cout << "Error: " << e.what() << "\n";   // this runs instead
    }
}
```

- **`throw`** creates and "raises" an exception object — here,
  `std::invalid_argument`, a standard exception type carrying a message.
- **`try { }`** marks a block of code where you're watching for
  exceptions to occur.
- **`catch (const std::invalid_argument& e)`** catches that specific
  exception type if one is thrown inside the `try` block, and `e.what()`
  gives you back the message string.

## Why you'll see `catch (const std::exception& e)` specifically

```cpp
try {
    riskyOperation();
} catch (const std::exception& e) {
    std::cout << "Something went wrong: " << e.what() << "\n";
}
```

`std::exception` is the **base class** every standard exception type
(`std::invalid_argument`, `std::runtime_error`, `std::out_of_range`, ...)
inherits from — this is the same inheritance idea `10_lld/00_foundations/03_oop_fundamentals.md`
covers at the design level, applied here to error types. Catching
`const std::exception&` catches *any* of them, which is why you'll see
this exact pattern as the "catch anything reasonable" default throughout
this program — always **`const &`**, never by value (which would slice the
exception the same way object slicing works for any other class, losing
information about the specific error type) and never by raw pointer
(ownership of a caught-by-pointer exception is ambiguous).

## Common standard exception types you'll see

```cpp
throw std::invalid_argument("bad input");   // caller passed something invalid
throw std::runtime_error("operation failed");   // a generic runtime failure
throw std::out_of_range("index too large");   // classic container-bounds error
throw std::logic_error("not implemented");    // this exact one shows up throughout
                                                 // this program's workspace stub files
```

## Writing your own exception type

You're not limited to the standard types above — you can define your own,
using inheritance (file `05`) from `std::runtime_error` or another
standard exception:

```cpp
#include <stdexcept>

class InsufficientFundsError : public std::runtime_error {
public:
    explicit InsufficientFundsError(double shortfall)
        : std::runtime_error("insufficient funds, short by " + std::to_string(shortfall)),
          shortfall_(shortfall) {}

    double shortfall() const { return shortfall_; }

private:
    double shortfall_;
};
```

```cpp
try {
    throw InsufficientFundsError(25.50);
} catch (const InsufficientFundsError& e) {
    std::cout << e.what() << "\n";              // inherited from std::runtime_error
    std::cout << "short by: " << e.shortfall();  // your own extra data
} catch (const std::exception& e) {
    // a catch block for the general case still catches your custom type too,
    // since InsufficientFundsError IS-A std::runtime_error IS-A std::exception
}
```

Two things worth noticing: `: std::runtime_error("...")` in the
initializer list calls the base class's constructor (exactly the pattern
from file `05`) to set the message `.what()` returns; and your custom
type can carry **extra data** beyond just a message (`shortfall_` here) —
something a plain `throw std::runtime_error(...)` can't do. This is
exactly the pattern `10_lld/00_foundations/14_application_architecture_patterns.md`
uses for a whole custom exception *hierarchy* (a base error type with
several specific subtypes) — worth returning to that file once this
shape feels comfortable.

## What happens if nothing catches it

```cpp
void f() { throw std::runtime_error("oops"); }
int main() { f(); }   // no try/catch anywhere
```

The program terminates immediately, printing something like
`terminate called after throwing an instance of 'std::runtime_error'
what():  oops` and exits. This is intentional — a loud, immediate failure
is far easier to debug than a silent one.

## Exception safety and RAII — the connection worth previewing

```cpp
void process() {
    std::unique_ptr<Resource> r = std::make_unique<Resource>();
    riskyOperation();   // if this throws...
    // ...this line never runs
} // ...but r's destructor STILL runs here, cleaning up automatically
```

This is the payoff of RAII (previewed in file `03`, covered fully in
`10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`): even when an
exception skips the rest of a function, C++ guarantees every stack-local
object's destructor still runs on the way out. This is *why* smart
pointers and RAII wrappers are the default in this program instead of
manual `new`/`delete` — manual cleanup code would simply be skipped if an
exception fired in between, but a destructor never is.

## The three exception-safety guarantee levels

A question worth being able to answer precisely, since it comes up
directly whenever you write an `operator=` by hand (`11_operator_overloading.md`'s
`Buffer::operator=` example) or design a container-like class: **if a
function throws partway through, what state is the program left in?**
C++ names three answers, weakest to strongest:

- **No guarantee** — the object can be left in a broken, inconsistent
  state (missing invariants, a resource leaked). Never acceptable in code
  you write for this program.
- **Basic guarantee** — if an exception is thrown, no resources leak and
  every object involved is left in *some* valid state (invariants still
  hold), but not necessarily the state it was in before the call. This is
  the minimum every function in this program should offer, and RAII gives
  it to you almost for free — the earlier `process()` example's `unique_ptr`
  cleanup is the basic guarantee in action.
- **Strong guarantee** — if an exception is thrown, the operation has *no
  effect at all*: the object is exactly as it was before the call, as if
  it had never been attempted ("commit-or-rollback" semantics). This is
  the one worth explicitly aiming for on anything that mutates shared
  state, and it's what an interviewer means by "is your `operator=`
  exception-safe?"

**The idiom that gets you the strong guarantee for free: copy-and-swap.**
Look back at `11_operator_overloading.md`'s hand-written
`Buffer::operator=` — it mutates `*this` directly (`delete[] data_;` then
reassigns), so if `new int[size_]` throws partway through (out of memory),
`*this` is left with `data_` already deleted and no valid replacement —
only the *basic* guarantee, not the strong one. Copy-and-swap fixes this
by doing every risky step on a **temporary copy first**, and only
touching `*this` with a `swap` — an operation documented to never throw:

```cpp
class Buffer {
public:
    explicit Buffer(size_t n) : size_(n), data_(new int[n]) {}
    ~Buffer() { delete[] data_; }
    Buffer(const Buffer& other) : size_(other.size_), data_(new int[other.size_]) {
        std::copy(other.data_, other.data_ + size_, data_);
    }
    void swap(Buffer& other) noexcept {          // never throws -- just swaps pointers
        std::swap(size_, other.size_);
        std::swap(data_, other.data_);
    }
    Buffer& operator=(Buffer other) {             // note: takes its argument BY VALUE --
        swap(other);                              // this is where the copy (and any throw)
        return *this;                              // happens, BEFORE *this is touched at all
    }
private:
    size_t size_;
    int* data_;
};
```

**Why this is strongly exception-safe where the hand-written version
wasn't**: `operator=`'s parameter `other` is taken *by value*, so the
copy constructor runs to build it — if `new int[...]` throws there, it
throws while building a temporary, before `*this` has been touched at
all. `*this` is only ever modified by `swap`, which is `noexcept` and
therefore cannot fail partway through. Either the whole assignment
succeeds, or `*this` is completely untouched — the strong guarantee, and
notice the self-assignment check from the original version isn't even
needed anymore: `x = x` just copies `x` into `other` and swaps it back,
harmlessly.

## Exercises

1. Write a function that throws `std::invalid_argument` if given a
   negative number, and a `main()` that calls it inside a `try`/`catch`
   both with a valid and an invalid input — confirm the catch block only
   runs for the invalid case.
2. Write a function that throws `std::out_of_range`, and a caller that
   catches `const std::exception&` (the base class) instead of the
   specific type — confirm it still catches it, and print `e.what()`.
3. Reproduce the RAII-and-exceptions example above for real: a small
   class that prints "destroyed" in its destructor, held in a
   `std::unique_ptr`, with a function that throws partway through using
   it — confirm "destroyed" still prints even though the throw skipped
   the rest of the function.
4. Write your own exception type deriving from `std::invalid_argument`,
   carrying one piece of extra data beyond the message (an error code, a
   field name — your choice). Throw it, catch it once by its specific
   type (and use the extra data), and separately confirm a
   `catch (const std::exception&)` block also catches it.
5. Write the copy-and-swap `Buffer` class above in full, plus a fourth
   member function `void forceThrow()` that always throws (simulating
   `new` failing). Temporarily call `forceThrow()` inside `swap()` (after
   the pointer swap, to simulate a mid-operation failure) and observe that
   `*this` is now corrupted — this demonstrates *why* `swap()` must be
   `noexcept` and must be the very last, simplest step: it's the one
   operation in the whole idiom that's never allowed to fail.
