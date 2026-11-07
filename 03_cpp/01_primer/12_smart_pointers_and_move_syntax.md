# 12 — Smart pointers and `std::move`: the syntax

> This file is the mechanical "here's the syntax" companion to
> `10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`, which explains
> *why* these exist and when to use each. Read this first if the syntax
> itself — not just the design reasoning — still feels unfamiliar.

## `std::unique_ptr<T>` — one owner

```cpp
#include <memory>

std::unique_ptr<Account> acc = std::make_unique<Account>(100.0);
acc->deposit(50.0);          // -> works on a smart pointer just like a raw pointer
std::cout << acc->getBalance();

// acc automatically deletes the Account when it goes out of scope --
// no explicit delete anywhere
```

`std::make_unique<Account>(100.0)` heap-allocates an `Account`, calling
its constructor with `100.0`, and wraps it in a `unique_ptr`. `->` works
exactly like it does on a raw pointer — access a member through it. The
object is automatically destroyed when `acc` goes out of scope (this is
the RAII idea from files `03` and `09`, made concrete).

**A `unique_ptr` cannot be copied**:

```cpp
std::unique_ptr<Account> a = std::make_unique<Account>(100.0);
std::unique_ptr<Account> b = a;   // compile error!
```

This is on purpose (file `03`'s "one owner" idea, enforced by the
compiler) — if it could be copied, two `unique_ptr`s would both think
they own the same memory, and both would try to delete it. To transfer
ownership instead of copying, you use `std::move` (below).

## `std::move` — transferring ownership

```cpp
std::unique_ptr<Account> a = std::make_unique<Account>(100.0);
std::unique_ptr<Account> b = std::move(a);   // OK: transfers ownership from a to b

// a is now empty (holds nullptr) -- it gave up ownership
// b now owns the Account
```

**`std::move` does not move anything by itself — it's a cast.** It says
"treat `a` as something whose contents can be stolen rather than
copied." What actually happens — `b` taking `a`'s internals, `a` being
left empty — happens inside `unique_ptr`'s own move logic, triggered by
that cast. You'll see `std::move` constantly anywhere ownership is being
handed off:

```cpp
class Car {
public:
    explicit Car(std::unique_ptr<Engine> engine) : engine_(std::move(engine)) {}
    // ^ the parameter `engine` is moved into the member `engine_` --
    //   without std::move here, this wouldn't compile, since unique_ptr can't be copied
private:
    std::unique_ptr<Engine> engine_;
};

auto myEngine = std::make_unique<Engine>();
Car car(std::move(myEngine));   // hand ownership to Car's constructor
// myEngine is now empty -- car owns the Engine now
```

**The practical rule**: whenever you're passing or storing a
`unique_ptr` (or anything else that can't be copied, or that's expensive
to copy and you're done with the original), and the compiler complains
that it can't be copied — reach for `std::move` at that exact call site.

## `std::shared_ptr<T>` — multiple owners

```cpp
std::shared_ptr<CacheEntry> entry1 = std::make_shared<CacheEntry>(...);
std::shared_ptr<CacheEntry> entry2 = entry1;   // OK! shared_ptr CAN be copied

// the CacheEntry is only actually destroyed once BOTH entry1 and entry2
// (and any other copies) go out of scope -- it keeps an internal count
```

Unlike `unique_ptr`, `shared_ptr` **can** be copied — every copy
increments an internal reference count, and the object is destroyed only
when the count reaches zero (the last owner goes away). Use this when
something genuinely needs multiple simultaneous owners — the design
tradeoffs (and the real performance cost of that reference count) are
covered in `10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`.

## `std::weak_ptr<T>` — looking without owning

```cpp
std::shared_ptr<Parent> parent = std::make_shared<Parent>();
std::weak_ptr<Parent> weakRef = parent;   // doesn't keep Parent alive by itself

if (auto locked = weakRef.lock()) {   // lock() gives you a real shared_ptr, if still alive
    std::cout << locked->getName();
} else {
    std::cout << "Parent no longer exists";
}
```

A `weak_ptr` can observe an object managed by `shared_ptr` without
counting as an owner — `.lock()` gives you a temporary `shared_ptr` you
can actually use, or an empty one if the object's already gone. You'll
see this exact pattern in the Observer pattern
(`10_lld/00_foundations/09_design_patterns_behavioral.md`), where a subject
holds `weak_ptr`s to its observers so it doesn't accidentally keep a
destroyed observer "alive" from its own perspective.

## Rvalue references (`&&`) — what `std::move` actually enables

```cpp
void take(std::string&& s) {   // && means "an rvalue reference to string"
    std::cout << "took: " << s;
}

std::string name = "Alice";
take(std::move(name));   // OK -- std::move casts name into an rvalue
// take(name);           // would NOT match this overload -- name is an lvalue
```

You don't need to fully internalize the lvalue/rvalue distinction to work
productively in this program — the practical takeaway is: `T&&` in a
function signature (most often a move constructor or move-assignment
operator, which you'll see but rarely need to write by hand thanks to the
Rule of Zero in `10_lld/00_foundations/01`) means "this specifically accepts
something being moved from," and it's what `std::move` targets.

## Exercises

1. Create a `std::unique_ptr<int>` with `std::make_unique<int>(42)`, then
   try to copy it into a second variable — read the compiler error, then
   fix it using `std::move` and confirm the original is now empty
   (compare it to `nullptr`).
2. Write a small class that prints "moved" when its move constructor runs
   (the compiler generates one for you automatically if you follow the
   Rule of Zero — put it in a `std::vector` and trigger a reallocation by
   pushing past initial capacity to observe it, similar to the exercise
   in `10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`).
3. Create two `std::shared_ptr`s pointing at the same object, print how
   many owners exist with `.use_count()`, let one go out of scope (a
   nested `{ }` block), and print `.use_count()` again to watch it drop.
4. Create a `shared_ptr<int>`, a `weak_ptr<int>` observing it, then let
   the `shared_ptr` go out of scope — confirm `weakPtr.lock()` returns an
   empty (falsy) result afterward.
