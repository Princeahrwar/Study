# 10 — Lambdas and `std::function`: passing behavior as a value

## The problem: you want to pass "what to do," not just data

```cpp
std::vector<int> nums = {5, 2, 8, 1, 9};
std::sort(nums.begin(), nums.end());   // sorts ascending by default
```

What if you wanted descending order, or to sort by some custom rule? You
need a way to hand the sort algorithm *a piece of behavior* — a
comparison function — not just data. A **lambda** is C++'s way to write a
small, unnamed function right where you need it.

## Lambda syntax

```cpp
auto add = [](int a, int b) {
    return a + b;
};
std::cout << add(3, 4);   // 7
```

Breaking down `[](int a, int b) { return a + b; }`:
- **`[]`** — the "capture list" (see below) — empty here, meaning it uses
  nothing from outside itself.
- **`(int a, int b)`** — the parameter list, exactly like a normal
  function.
- **`{ return a + b; }`** — the body, exactly like a normal function.

This whole expression creates a small, anonymous, callable object on the
spot. `auto add = ...` gives it a name so you can call it, but you don't
have to — you can write the lambda directly where it's needed:

```cpp
std::sort(nums.begin(), nums.end(), [](int a, int b) {
    return a > b;   // "a should come before b if a > b" -- descending order
});
```

## Captures — using variables from outside the lambda

```cpp
int threshold = 5;

auto isAboveThreshold = [threshold](int x) {
    return x > threshold;   // "captured" threshold from the surrounding scope
};

std::cout << isAboveThreshold(10);   // true
```

`[threshold]` says "copy `threshold` from the enclosing scope into this
lambda, so I can use it inside." Three common capture forms:

```cpp
[threshold](int x) { ... }    // capture threshold by value (a copy)
[&threshold](int x) { ... }   // capture threshold by reference (the real variable)
[=](int x) { ... }            // capture everything used, by value
[&](int x) { ... }            // capture everything used, by reference
```

**Rule of thumb for this program**: capture by value (`[x]` or `[=]`)
unless you specifically need the lambda to see changes made to the
variable after the lambda was created, or the lambda outlives the
variable's scope (capturing by reference to something that's since been
destroyed is a real, common bug — a dangling reference).

## Where you'll see this constantly in the tutorials

**As a comparison function**, exactly like the sort example above — you'll
see this in `10_lld/01_machine_coding_problems/21_airline_seat_reservation.md`'s
seat-sorting logic.

**As a task to run later**, especially with the Task Scheduler and Thread
Pool:

```cpp
scheduler.schedule([]() {
    std::cout << "This runs later, not right now\n";
}, std::chrono::seconds(5));
```

Here the whole lambda — the `[]() { ... }` — is the "what to do," handed
to `schedule()` as a value, to be called at some point in the future.
This is the exact mechanic behind
`10_lld/01_machine_coding_problems/10_task_scheduler_cron.md`.

**As a lightweight Strategy**, instead of a whole class hierarchy —
`10_lld/00_foundations/09_design_patterns_behavioral.md`'s Strategy section and
`10_lld/00_foundations/02_stl_for_system_design.md` both cover this directly.

## `std::function` — a variable that can hold "any callable thing"

```cpp
#include <functional>

std::function<int(int, int)> operation;

operation = [](int a, int b) { return a + b; };
std::cout << operation(3, 4);   // 7

operation = [](int a, int b) { return a * b; };
std::cout << operation(3, 4);   // 12
```

`std::function<int(int, int)>` means "a variable that can hold any
callable thing taking two `int`s and returning an `int`" — a lambda, a
regular function, anything with that shape. This is what lets you store a
lambda in a class member, pass it around, or reassign it later — a lambda
by itself (the `auto add = [...]` form) has a type the compiler generates
for you that you generally can't write out yourself; `std::function` is
the general-purpose "container" type you use when you need to actually
name that type (a class member, a function parameter that should accept
any callable).

```cpp
class Button {
public:
    void setOnClick(std::function<void()> callback) {
        onClick_ = callback;
    }
    void click() {
        if (onClick_) onClick_();   // calls whatever was set
    }
private:
    std::function<void()> onClick_;
};
```

You'll see exactly this shape — a class member typed `std::function<...>`,
set once, called later — in the Logger, Task Scheduler, and Rate Limiter
problems.

## Exercises

1. Write a lambda that squares its argument, store it in a variable with
   `auto`, and call it.
2. Write a lambda that captures a local `int` variable by value, and a
   second one that captures the same variable by reference. Change the
   variable after creating both lambdas, then call both — confirm they
   give different results, and understand why.
3. Use `std::sort` with a lambda comparator to sort a
   `std::vector<std::pair<std::string, int>>` (name, age pairs) by age,
   descending.
4. Write a class with a `std::function<void(int)>` member, a setter for
   it, and a method that calls it with a value — set it to two different
   lambdas at different points and confirm the behavior changes each time.
