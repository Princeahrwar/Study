# 04 — Classes and objects: the actual syntax

> `00_foundations/03_oop_fundamentals.md` teaches you *why* and *when* to
> use these ideas (encapsulation, inheritance, polymorphism) as design
> tools. This file teaches the *syntax* first, so that discussion isn't
> also your first time seeing a `class` keyword.

## A class is a struct with functions attached, plus access rules

If you've used C `struct`s to bundle related data, you already understand
80% of what a class is:

```cpp
class Account {
public:
    Account(double startingBalance) {   // constructor: runs when an Account is created
        balance_ = startingBalance;
    }

    void deposit(double amount) {        // a member function ("method")
        balance_ = balance_ + amount;
    }
    double getBalance() {
        return balance_;
    }

private:
    double balance_;   // member variable -- trailing underscore is a common convention
                        // for "this is private data," used throughout this program
};
```

Usage:

```cpp
Account acc(100.0);   // calls the constructor, creates an object
acc.deposit(50.0);
std::cout << acc.getBalance();   // 150
```

## `public`, `private`, and `protected` — the three access levels

```cpp
class Account {
public:
    double getBalance() const { return balance_; }   // anyone can call this

protected:
    void applyInterest(double rate) { balance_ += balance_ * rate; }   // only Account and its subclasses

private:
    double balance_;   // only Account itself
};
```

Everything under `public:` can be used by anyone with an `Account` object.
Everything under `private:` can only be used by `Account`'s own member
functions — `acc.balance_` from outside the class would be a compile
error. **`protected:` is the middle ground**: also hidden from the outside
world, but — unlike `private` — visible to any class that *inherits* from
this one (covered in full in file `05`, right after this file). Until you
have inheritance, `protected` behaves exactly like `private`; its whole
purpose only becomes visible once a derived class exists that needs that
access. That's why this file mentions it here but the real, working
example is in the very next file.

**This is the actual mechanism behind "encapsulation"** that
`03_oop_fundamentals.md` talks about at the design level — none of these
three are a suggestion, the compiler enforces all of them. You can have as
many `public:`/`protected:`/`private:` sections as you want in one class,
in any order; each one applies to everything until the next label.
A quick way to decide which of the three a member should be: **can code
outside this whole class hierarchy need it? `public`. Only this exact
class? `private`. This class and anything that inherits from it?
`protected`.**

## `friend` — a deliberate, narrow hole in encapsulation

You'll see this word appear inside a class body in a few of this
program's design-pattern examples, and it's worth knowing exactly what it
does the first time you see it rather than skimming past it:

```cpp
class Engine {
private:
    int rpm_;
    friend class Car;   // Car (and only Car) can touch Engine's private members
};

class Car {
public:
    void redline(Engine& e) { e.rpm_ = 9000; }   // legal, because of the friend declaration above
};
```

Without the `friend class Car;` line, `e.rpm_` from inside `Car` would be a
compile error — `rpm_` is private to `Engine`. `friend` grants one specific
other class (or, with `friend void someFunction(...);`, one specific
free function) an exception to that rule, and *only* that one.

**Why this isn't a contradiction of encapsulation, if used narrowly**: it's
a deliberate, explicit, compiler-checked exception — grep-able (`friend
class Car` names exactly who gets the access), unlike making `rpm_` fully
public, which would let *anyone* touch it. You'll see this exact pattern
in `00_foundations/07_design_patterns_creational.md`'s Builder (`friend
class HttpRequest;` — the nested `Builder` only gives `HttpRequest` itself
access to the fields it collected) and in `09_design_patterns_behavioral.md`'s
Memento (`friend class Editor;` — the memento is opaque to everyone except
the one class allowed to restore from it). **The habit to build**: reach
for `friend` only when two classes are genuinely two halves of one
design (a class and the one builder/originator that constructs it) —
using it to avoid writing a proper public method is the anti-pattern
version of this feature.

## `explicit` — blocking conversions you didn't ask for

You've already seen `explicit` in front of almost every one-argument
constructor in this program's code (`explicit Account(double balance)`).
Here's why it's there, concretely — without it, C++ allows a surprising
implicit conversion:

```cpp
class Account {
public:
    Account(double balance) : balance_(balance) {}   // NOT explicit
    // ...
};

void processPayment(Account acc);

processPayment(500.0);   // compiles! 500.0 is silently converted to a
                          // temporary Account -- did you mean to do that,
                          // or was this a bug (passing a raw amount where
                          // an Account was expected)?
```

Any constructor callable with exactly one argument can be used by the
compiler as an *implicit conversion* from that argument's type to your
class — even when you never asked for that and never call it that way on
purpose. `explicit` turns that off:

```cpp
class Account {
public:
    explicit Account(double balance) : balance_(balance) {}   // explicit
};

processPayment(500.0);          // now a COMPILE ERROR -- good, this was
                                 // probably a bug
processPayment(Account(500.0)); // still fine -- an explicit, intentional conversion
```

**The practical rule this program follows everywhere**: mark every
constructor callable with one argument `explicit`, unless you're
deliberately designing that type to be implicitly convertible from that
argument (a genuinely rare, deliberate choice — not the default). It costs
nothing and catches a real class of bug at compile time instead of at
runtime, which is exactly the kind of free safety C++ rewards you for
using.

## Constructors

```cpp
class Account {
public:
    Account(double startingBalance) : balance_(startingBalance) {}
    // ...
private:
    double balance_;
};
```

That `: balance_(startingBalance)` before the `{}` is a **member
initializer list** — it's the preferred way to set a member variable's
initial value (rather than assigning inside the constructor body). You'll
see this syntax constantly in every tutorial's code. Read
`Account(double startingBalance) : balance_(startingBalance) {}` as:
"when constructing an `Account`, initialize `balance_` with whatever
`startingBalance` was passed in."

You can have multiple constructors (this is just overloading, from
file `02`, applied to constructors):

```cpp
class Account {
public:
    Account() : balance_(0.0) {}                      // no-argument constructor
    Account(double startingBalance) : balance_(startingBalance) {}
    // ...
};
Account a;        // uses the first constructor, balance_ = 0.0
Account b(50.0);   // uses the second, balance_ = 50.0
```

## Default member initializers — a value written right where the member is declared

```cpp
class Account {
public:
    Account() {}                                       // balance_ already 0.0, no work needed here
    Account(double startingBalance) : balance_(startingBalance) {}
private:
    double balance_ = 0.0;   // <-- the default, used by ANY constructor that
                              //     doesn't override it in its own init list
};
```

`= 0.0` directly after the member's declaration (not inside any specific
constructor) sets that member's value whenever a constructor's own
initializer list doesn't mention it — a default every constructor shares,
written once instead of repeated in each one. You'll see this constantly
for a member that has one obvious sensible default (a `count_ = 0`, a
`std::string method_ = "GET";` for an HTTP request builder) while still
letting a specific constructor override it explicitly if it needs to. This
is different from, and works alongside, the member initializer list from
the Constructors section above — a constructor's own `: balance_(x)` always
wins over the in-class default if both are present.

## Delegating constructors — one constructor calling another

```cpp
class Account {
public:
    Account() : Account(0.0) {}                          // delegates to the constructor below
    Account(double startingBalance) : balance_(startingBalance) {}
private:
    double balance_;
};
```

`Account() : Account(0.0) {}` — a constructor's initializer list naming
*its own class* instead of a member is a **delegating constructor**: it
calls the other constructor (`Account(double)`) to do the actual
initialization work, instead of repeating that logic. Useful the moment
two or more constructors would otherwise share real setup logic (not just
a default value, which the section above already handles) — validation, a
computed initial value, anything more than "set this field to a
constant." A constructor that delegates **can't also have its own member
initializer list entries** — it's one or the other, delegate everything or
initialize everything yourself.

## Destructors

```cpp
class Account {
public:
    ~Account() {   // the destructor -- note the ~
        std::cout << "Account destroyed\n";
    }
};
```

The destructor runs automatically when the object's lifetime ends — when
a stack-local object goes out of scope, or when a `unique_ptr` holding it
is destroyed (file `03`). You rarely need to write one yourself in modern
C++ (this is the "Rule of Zero" idea `00_foundations/01` covers in full) —
but you'll see `virtual ~ClassName() = default;` constantly, and
`00_foundations/03_oop_fundamentals.md` explains exactly why the
`virtual` matters there.

## `struct` vs `class` — the one real difference

```cpp
struct Point { int x, y; };     // members are public by default
class Point  { int x, y; };     // members are private by default (same otherwise)
```

That's genuinely the *only* difference in C++ — `struct` and `class` are
otherwise identical (both can have constructors, member functions, be
inherited from, etc.). Convention in this program: `struct` for simple
data bundles with no invariants to protect (a `Point`, a `Seat`), `class`
for anything with behavior and private state to protect.

## `enum class` — a small, safe, named set of values

You'll see this constantly for anything with a fixed set of states or
options — an eviction policy, a traffic light's phase, an order's status:

```cpp
enum class EvictionPolicy { LRU, LFU, FIFO };

EvictionPolicy p = EvictionPolicy::LRU;   // must be qualified: EvictionPolicy::LRU
```

**Why `enum class` and not plain `enum`** (the older C-style version):
plain `enum` dumps its values into the surrounding scope (`LRU` alone,
colliding with any other `LRU` anywhere else in the program) and silently
converts to `int`, so `EvictionPolicy(999)` — a value that was never
listed — compiles without complaint. `enum class` fixes both: values are
scoped (must write `EvictionPolicy::LRU`, so two different enums can each
have an `LRU` with no collision) and there's no silent conversion to or
from `int` — you'd need an explicit `static_cast` to do that, which makes
it a deliberate, visible choice rather than an accident.

```cpp
switch (policy) {
    case EvictionPolicy::LRU:  /* ... */ break;
    case EvictionPolicy::LFU:  /* ... */ break;
    case EvictionPolicy::FIFO: /* ... */ break;
}
```

A `switch` over every value of an `enum class`, with no `default:`, is
also how compilers warn you (`-Wswitch`) if you add a new enum value later
and forget to handle it somewhere — a small, free safety net you'd lose
with a plain `bool`/`int` flag doing the same job.

**Rule of thumb for this whole program**: any time you're tempted to add a
`bool` parameter or field that really means "which of several named
options," reach for `enum class` instead — it self-documents at every call
site (`Cache(capacity, EvictionPolicy::LFU)` reads clearly; `Cache(capacity,
true)` does not), which is the exact same call-site-readability argument
`00_foundations/20_api_design_interviews.md` makes about avoiding raw
boolean parameters.

## `this`

```cpp
class Account {
public:
    Account& deposit(double amount) {
        balance_ += amount;
        return *this;   // "myself" -- lets you chain calls: acc.deposit(10).deposit(20)
    }
private:
    double balance_;
};
```

`this` is a pointer to the current object, available inside any member
function. `*this` dereferences it to get the object itself. You'll mostly
see this in **fluent builder** patterns (chaining `.deposit(x).deposit(y)`)
— `00_foundations/07_design_patterns_creational.md`'s Builder section
uses this exact technique.

## Getters and setters — and why this program is wary of them

```cpp
class Account {
public:
    double getBalance() const { return balance_; }   // read-only accessor
    // no setBalance() -- callers use deposit()/withdraw() instead, which
    // enforce rules (never negative); a raw setter would let anyone bypass them
private:
    double balance_;
};
```

Notice `const` after the parameter list on `getBalance()` — that means
"this function doesn't modify the object." File `06` covers `const` in
depth. Notice also there's deliberately no `setBalance()` — this is a
design choice, not an omission, and `00_foundations/03_oop_fundamentals.md`
explains why plain getter/setter pairs for every field often defeat the
whole point of making data private in the first place.

## `static` members — belonging to the class, not to any one object

Every example so far had data that belongs to *one specific object* —
`acc.balance_` is different from `otherAcc.balance_`. Sometimes you want
data or a function that belongs to the **class itself**, shared by every
object of that type:

```cpp
class Account {
public:
    Account(double startingBalance) : balance_(startingBalance) { ++accountCount_; }
    ~Account() { --accountCount_; }

    static int accountCount() { return accountCount_; }   // a static member function

private:
    double balance_;
    static int accountCount_;   // ONE shared counter for every Account that exists
};

int Account::accountCount_ = 0;   // static data members need exactly one definition
                                    // like this, outside the class, usually in a .cpp file
```

```cpp
Account a(100.0);
Account b(50.0);
std::cout << Account::accountCount();   // 2 -- called on the CLASS, not an object
```

Two things to notice: **`static` data** (`accountCount_`) is one single
shared value, not a per-object copy — every `Account` object reads and
writes the *same* `accountCount_`. **`static` member functions**
(`accountCount()`) can be called through the class name directly
(`Account::accountCount()`), without needing any actual `Account` object
to exist, and — because of that — they have no `this` and can only touch
`static` data, never a particular object's `balance_`.

**Where you'll see this in the tutorials**: the Meyers' Singleton pattern
(`00_foundations/07_design_patterns_creational.md`) is built almost
entirely out of a `static` local variable inside a `static` member
function — recognizing `static` here is a direct prerequisite for that
pattern making sense.

## Nested classes — a class declared inside another class

```cpp
class HttpRequest {
public:
    class Builder {                 // Builder only makes sense in the context of HttpRequest
    public:
        Builder& url(std::string u) { url_ = std::move(u); return *this; }
        HttpRequest build() const { return HttpRequest(*this); }
    private:
        std::string url_;
    };
private:
    explicit HttpRequest(const Builder& b) : url_(b.url_) {}
    std::string url_;
};

HttpRequest::Builder b;              // note the :: -- Builder's full name includes its outer class
HttpRequest req = b.url("...").build();
```

A class declared inside another class's body is a **nested class** —
`Builder` here is a completely ordinary class (its own constructors, its
own members, everything covered in this whole file applies to it too),
the only difference is where its name lives: from outside, you refer to
it as `HttpRequest::Builder`, not just `Builder`. **Why nest it at all**,
rather than declaring `Builder` as a normal top-level class next to
`HttpRequest`: it communicates "this type has no reason to exist except in
service of `HttpRequest`" — you'll never construct a bare `Builder` for
anything else, so its name scoped under `HttpRequest::` says that
directly instead of leaving two same-level, seemingly-independent class
names for a reader to figure out the relationship between. **Where
you'll see this constantly**: every fluent Builder in
`00_foundations/07_design_patterns_creational.md` nests its `Builder`
exactly this way, usually paired with the `friend class` technique from
earlier in this file so the outer class's private constructor can read
the nested builder's collected fields directly.

## A gotcha worth knowing: member initialization order

```cpp
class Weird {
public:
    Weird(int x) : b_(x), a_(b_) {}   // looks like: set b_ first, then a_ from b_...
private:
    int a_;   // ...but a_ is declared FIRST, so it's actually initialized first!
    int b_;   // b_ is declared second, so despite being listed first in the
              // initializer list, it initializes AFTER a_ -- a_(b_) reads
              // b_ before b_ has been set, which is a real bug
};
```

**Members are always initialized in the order they're *declared* in the
class, not the order they appear in the initializer list** — the
initializer list's order is purely cosmetic to the compiler; only the
declaration order (top to bottom inside the `class { ... }` body) is
real. Most compilers will even warn you about this (`-Wreorder`) if the
two orders don't match. The practical habit: **write your initializer
list in the same order as your member declarations**, and this gotcha
never bites you.

## Exercises

1. Write a `Rectangle` class with private `width_` and `height_`, a
   constructor taking both, and `area()`/`perimeter()` methods.
2. Add a second constructor taking no arguments that defaults both to 1.
3. Add a destructor that prints "Rectangle destroyed" and confirm (by
   creating one inside a small nested `{ }` block) that it fires exactly
   when the block ends, not before.
4. Convert your `Rectangle` from a `class` to a `struct`, remove the
   `public:`/`private:` labels, and confirm it still compiles — this
   should make the "only difference is the default" claim concrete rather
   than abstract.
5. Add a `static int instanceCount_` to your `Rectangle`, incremented in
   the constructor and decremented in the destructor, with a
   `static int instanceCount()` accessor. Create three `Rectangle`s in a
   nested `{ }` block, print the count inside the block, let them all go
   out of scope, and print it again from outside — confirm it goes back
   to zero automatically.
6. Deliberately reproduce the member-initialization-order gotcha above
   (declare `a_` before `b_`, but try to initialize `a_` from `b_` in the
   list) — compile with `-Wall` and read the reorder warning, then fix it
   by reordering the *declarations* to match the dependency, not the
   initializer list.
7. Add a single-argument constructor to your `Rectangle` that isn't
   `explicit` (e.g. `Rectangle(double side)` building a square), write a
   function `void printArea(Rectangle r)`, and call it with a bare
   `printArea(5.0)` to see the implicit conversion happen. Then mark the
   constructor `explicit` and confirm the same call is now a compile
   error.
8. Add an `enum class Shape { Rectangle, Circle, Triangle }` and a
   `switch` statement over it with no `default:` case. Remove one of the
   `case` labels, compile with `-Wall`, and read the `-Wswitch` warning —
   this is the safety net `enum class` gives you for free.
9. Write two small classes, `Logger` and `LogWriter`, where `LogWriter`
   needs to call one private method on `Logger` (e.g. `flushInternal()`)
   that no one else should call. Make `LogWriter` a `friend class` of
   `Logger` and confirm it compiles; then remove the `friend` declaration
   and confirm you get the private-access compile error you'd expect.
10. Give your `Rectangle` a default member initializer for `width_` and
    `height_` (both `= 1.0`), then write a no-argument constructor with an
    empty body (`Rectangle() {}`) and confirm it produces a 1x1 rectangle
    with no initializer list at all. Then rewrite your two-argument
    constructor as a delegating constructor that calls a private
    `Rectangle(double side)` (a square) with `side = (width + height) / 2`
    — a slightly contrived use, just to see the `: Rectangle(...)` syntax
    compile and run.
11. Give `Rectangle` a nested class `Builder` (following the `HttpRequest`
    shape above) with a fluent `width(double)`/`height(double)` and a
    `build()` returning a `Rectangle`. Construct one from outside as
    `Rectangle::Builder{}.width(3).height(4).build()`, and confirm
    `Rectangle::Builder` is the name you need — not just `Builder` — from
    outside `Rectangle`'s own body.
