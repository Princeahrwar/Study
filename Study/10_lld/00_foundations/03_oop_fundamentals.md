# 03 — OOP fundamentals, from first principles

> No prerequisites beyond writing C++ day to day. This rebuilds OOP as
> vocabulary and mechanism, not as things you already do unconsciously — the
> gap between "I use classes" and "I can defend a design decision in these
> terms" is the whole point of this file.

## Why a systems engineer needs this framed explicitly

You already write structs, functions, and probably some classes. What you
likely haven't had to do is *argue* for a design in the specific vocabulary an
interviewer expects — "this violates encapsulation," "that's tight coupling,"
"prefer composition here." This file gives you that vocabulary attached to
real C++ mechanics, including the vtable cost, which you already have the
background for from `04_computer_organization/10_branch_prediction.md` (indirect branches) and `04_computer_organization/12_inside_a_modern_core.md`.

## Classes and objects

A **class** is a type definition: data layout plus the operations valid on
that data. An **object** is a value of that type living somewhere (stack,
heap, or static storage). This is identical to a `struct` plus free functions
that take a pointer to it — the difference is that a class bundles the two and
lets you restrict who can touch the data.

```cpp
// C-style: data and behaviour are separate, nothing stops misuse
struct Account { double balance; };
void withdraw(Account* a, double amt) { a->balance -= amt; }  // no check!

// OOP: behaviour is bundled with the data it protects
class Account {
public:
    explicit Account(double balance) : balance_(balance) {}
    void withdraw(double amt) {
        if (amt > balance_) throw std::runtime_error("insufficient funds");
        balance_ -= amt;
    }
    double balance() const { return balance_; }
private:
    double balance_;
};
```

Nothing here is new C++ to you. What's new is naming *why* the second version
is better: the invariant ("balance never goes negative via withdraw") is
enforced at a single point, because the data is not reachable except through
that point. That property has a name — **encapsulation** — and it is the
foundation the other three pillars build on.

## The four pillars

### 1. Encapsulation

**Bundling data with the operations that maintain its invariants, and hiding
the data so it can only change through those operations.**

Mechanism in C++: access specifiers (`private`, `protected`, `public`).
Default to `private` data, expose behaviour, not fields. A getter that just
returns a mutable reference to private data isn't encapsulation — it's public
data with extra steps.

```cpp
class RingBuffer {
public:
    void push(int v) { /* checks capacity, wraps index */ }
    int  pop();
private:
    std::vector<int> buf_;
    size_t head_ = 0, tail_ = 0, size_ = 0;   // invariant: size_ <= buf_.size()
};
```

The interview-relevant version of this idea: **invariants are only as strong
as the narrowest interface that can break them.** If `head_`/`tail_` were
public, any caller could violate `size_ <= buf_.size()` and every method that
trusts the invariant becomes a bug waiting to happen.

### 2. Abstraction

**Exposing what an object does without exposing how.** Encapsulation hides
data; abstraction hides implementation detail and complexity behind a simple
interface. They're related but distinct — you can have encapsulation
(private fields) without meaningful abstraction (a class that's still just a
thin wrapper with getters/setters for everything, exposing its internal shape
even if the fields themselves are private).

Mechanism in C++: abstract base classes with pure virtual functions define a
*contract* independent of implementation.

```cpp
class Shape {
public:
    virtual double area() const = 0;      // pure virtual: no implementation here
    virtual ~Shape() = default;
};

class Circle : public Shape {
public:
    explicit Circle(double r) : r_(r) {}
    double area() const override { return M_PI * r_ * r_; }
private:
    double r_;
};
```

Code that holds a `Shape*` doesn't know or care whether it's a `Circle` or a
`Rectangle`. That's abstraction: the caller programs against the contract,
not the concretion. This is the mechanism that every design pattern in
`07`-`09` is built from.

### 3. Inheritance

**A mechanism for one class to acquire the interface and (optionally)
implementation of another**, expressing an *is-a* relationship. `Circle`
*is a* `Shape`.

```cpp
class Base {
public:
    virtual void speak() const { std::cout << "Base\n"; }
    void greet() const { std::cout << "hello from "; speak(); }
    virtual ~Base() = default;
};
class Derived : public Base {
public:
    void speak() const override { std::cout << "Derived\n"; }
};
```

Three things every candidate should be able to state cold:

- **Object slicing.** `Base b = Derived();` copies only the `Base` part —
  polymorphism is lost. This is why you pass/store polymorphic types by
  pointer or reference, never by value.
- **The diamond problem.** Two base classes both inherit from a common
  ancestor, and a class inherits from both — the ancestor's state is
  duplicated unless you use `virtual` inheritance (`class B : public virtual
  A`). Virtual inheritance has a real cost (an extra indirection to find the
  shared base) and real complexity; most style guides, including this one's
  default stance, avoid multiple inheritance of *implementation* entirely and
  reserve inheritance for interfaces (pure abstract classes), where the
  diamond problem doesn't arise because there's no state to duplicate.
- **Public vs private vs protected inheritance.** `public` inheritance means
  *is-a* and should be the only kind you reach for in an interview — `private`
  inheritance (has-a with access to protected members, rarely the right
  choice) and `protected` inheritance are corners of the language, not tools
  you need in a 45-minute round.

### 4. Polymorphism

**The same interface producing different behaviour depending on the concrete
type.** Two independent mechanisms in C++, and interviewers expect you to
distinguish them:

**Compile-time (static) polymorphism** — function overloading and templates.
Resolved by the compiler; zero runtime cost; no `virtual` involved.

```cpp
template <typename T> T max_of(T a, T b) { return a > b ? a : b; }
```

**Runtime (dynamic) polymorphism** — `virtual` functions. Resolved through a
**vtable**: every polymorphic object carries a hidden pointer to a table of
function pointers, one per virtual method, filled in per concrete type. A
call through a base pointer is an extra pointer-chase plus an indirect call —
typically a few nanoseconds, and it also defeats inlining and can hurt branch
prediction on the indirect call if the concrete type varies unpredictably at
the call site. You already have the vocabulary for why that matters from
`04_computer_organization/10_branch_prediction` and `12_inside_a_modern_core` — this is the OOP-shaped instance of the same cost.

```cpp
Shape* s = make_shape();   // Circle or Rectangle, decided at runtime
s->area();                 // vtable lookup, then indirect call
```

**When this is asked as a follow-up**: "does virtual dispatch have a cost,
and when would you avoid it?" Answer: yes, an indirect call and a lost
inlining opportunity, real in a hot loop over millions of small objects (a
packet-processing pipeline, say) — there you'd reach for compile-time
polymorphism (templates, or `std::variant` + `std::visit`) or a hand-written
dispatch table instead. In a request handler called thousands of times a
second, the cost is noise. Naming the actual scale where it matters is the
Staff-level answer; refusing all virtual dispatch on principle is not.

## Composition over inheritance

The single most important design heuristic in this entire file, and the one
interviewers listen for hardest.

Inheritance couples a derived class to its base's implementation — a change
to the base can silently break every derived class (the "fragile base class"
problem), and C++ only allows single implementation inheritance cleanly, so
committing to one base forecloses others. **Composition** — a class holding a
pointer/reference to another and delegating to it — couples only through an
interface, can be swapped at runtime, and composes freely (a class can hold
five collaborators).

```cpp
// Inheritance: Car IS-A Engine? No. This is wrong and it's a classic mistake.
class Engine { public: void start(); };
class Car : public Engine { /* ... */ };   // wrong relationship

// Composition: Car HAS-A Engine.
class Car {
public:
    explicit Car(std::unique_ptr<Engine> engine) : engine_(std::move(engine)) {}
    void start() { engine_->start(); }
private:
    std::unique_ptr<Engine> engine_;
};
```

Rule of thumb used throughout the machine-coding problem set: **reach for
inheritance only to model a genuine is-a relationship against an abstract
interface** (a `PaymentStrategy` interface with `CardPayment`, `CashPayment`
implementations). Reach for **composition whenever you're tempted to inherit
just to reuse some code** — that's what most of the Structural and
Behavioural patterns in `08`/`09` formalise.

## Coupling and cohesion

Two words interviewers use constantly and expect you to use back:

- **Coupling** — how much one class knows about, or depends on, another's
  internals. Low coupling = classes interact only through narrow interfaces
  and can change independently. High coupling = a change in one class forces
  changes in others.
- **Cohesion** — how focused a single class's responsibilities are. High
  cohesion = a class does one clearly-scoped thing (this is SRP from `05`,
  stated as a design quality rather than a rule). Low cohesion = a class is a
  grab-bag ("God class" / "God object" — a common anti-pattern name to know).

**Goal: low coupling, high cohesion.** Every pattern in this track is a
named, well-tested way to get there for a specific recurring shape of
problem.

## Interview angles

**"What's the difference between abstraction and encapsulation?"**
Encapsulation hides *data* behind an interface to protect invariants.
Abstraction hides *implementation complexity* behind a simpler interface so
callers reason about *what*, not *how*. A class can have one without the
other — public getters/setters for every field is encapsulation in name only
and abstracts nothing.

**"When would you use inheritance vs composition?"**
Inheritance for a true is-a relationship against a stable abstract interface,
especially when you want polymorphic substitution (a `Shape*` that could be
any shape). Composition for everything else, especially code reuse and
runtime-swappable behaviour. Default to composition; justify inheritance.

**"What does virtual dispatch cost, and would you avoid it in hot code?"**
See the vtable discussion above. Name the mechanism, name the cost, name the
scale at which you'd actually change the design.

**"What's a God object and why is it bad?"**
Low cohesion taken to an extreme — one class owning too many unrelated
responsibilities, becoming the single point every change has to touch, and
the single class every test has to construct. It's the concrete failure mode
SRP (`05`) exists to prevent.

## Exercises

1. Take a class you've written recently at work. Identify: is its data truly
   encapsulated, or are there public setters that let callers violate an
   invariant you assumed? Fix one.
2. Write a small polymorphic hierarchy (`Shape` → `Circle`, `Rectangle`,
   `Triangle`) and a version using `std::variant<Circle, Rectangle,
   Triangle>` + `std::visit` instead. Compile both with `-O2` and look at the
   generated assembly for a loop calling `area()` on a vector of each — this
   is the compile-time vs runtime polymorphism tradeoff made concrete on your
   own machine.
3. Refactor a class-that-inherits-for-code-reuse (find one, or write a
   deliberately bad `SortedVector : public std::vector<int>` that tries to
   maintain sortedness by inheriting) into composition, and explain in one
   sentence what broke about the inheritance version (hint: `std::vector`
   has a non-virtual destructor and no invariant-protection hooks — this is
   also why you never publicly inherit from STL containers).
4. Write one paragraph, from memory, distinguishing coupling from cohesion,
   with one example of each from a system you've actually worked on.
