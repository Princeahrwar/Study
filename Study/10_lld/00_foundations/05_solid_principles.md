# 05 — SOLID principles

> Prerequisite: `03_oop_fundamentals.md`. SOLID is the four pillars turned
> into five checkable rules. Every design pattern in `07`-`09` exists to
> satisfy one or more of these.

## Why this is the highest-leverage file in the foundations track

If an interviewer asks exactly one theory question in an LLD round, it's a
SOLID question — usually disguised as "why did you structure it this way?"
or "what would you change if we added a new payment type?" Knowing the five
letters is table stakes; being able to spot a violation in *someone else's*
code on sight, and fix it with the right pattern, is the actual signal.

## S — Single Responsibility Principle

**A class should have one reason to change.** Not "one method" — one
*responsibility*, one *axis along which requirements change*.

```cpp
// Violation: two reasons to change — pricing rules AND persistence format
class Invoice {
public:
    double calculateTotal() const;          // changes when pricing rules change
    void saveToDatabase() const;             // changes when storage changes
    void printReceipt() const;               // changes when receipt format changes
};
```

Three unrelated stakeholders (finance, DBA, UX) can each demand a change to
`Invoice`, and each change risks breaking the other two responsibilities.

```cpp
// Fixed: split along the axes of change
class Invoice {
public:
    double calculateTotal() const;
};
class InvoiceRepository {
public:
    void save(const Invoice&) const;
};
class ReceiptPrinter {
public:
    void print(const Invoice&) const;
};
```

**Common misreading**: SRP does not mean "one method per class." A class can
have ten methods and still have a single responsibility if all ten serve the
same axis of change. Interviewers penalize both directions — a bloated
god-class *and* pointless one-method micro-classes with no cohesive purpose.

## O — Open/Closed Principle

**Open for extension, closed for modification.** Adding a new behaviour
should mean *adding* code, not *editing* existing, tested code.

```cpp
// Violation: every new shape means editing this function
double area(const Shape& s) {
    if (s.type == CIRCLE)    return M_PI * s.radius * s.radius;
    if (s.type == RECTANGLE) return s.w * s.h;
    // adding Triangle means editing this function and retesting it
}
```

```cpp
// Fixed: polymorphism gives you the extension point
class Shape { public: virtual double area() const = 0; virtual ~Shape()=default; };
class Circle    : public Shape { double area() const override; };
class Rectangle : public Shape { double area() const override; };
class Triangle  : public Shape { double area() const override; };  // added, nothing else touched
```

This is the principle that most directly *is* "use polymorphism (or a
Strategy) instead of a switch/if-chain on a type tag." Every time you see a
`switch` on an enum that's likely to grow, OCP is the name for why that's a
smell, and Strategy/Factory (`07`/`09`) are the fix.

**Caveat interviewers respect**: OCP is not "never edit code." A fixed,
small, truly closed set (say, exactly 4 HTTP verbs) doesn't need a
polymorphic hierarchy — that would be speculative complexity. Apply OCP where
the set of variants is genuinely expected to grow.

## L — Liskov Substitution Principle

**A subtype must be usable anywhere its base type is expected, without the
caller noticing the difference or breaking.** Formally: if `S` is a subtype
of `T`, objects of type `T` may be replaced with objects of type `S` without
altering the correctness of the program.

The canonical violation, worth having memorised because it's asked constantly:

```cpp
class Rectangle {
public:
    virtual void setWidth(double w)  { w_ = w; }
    virtual void setHeight(double h) { h_ = h; }
    double area() const { return w_ * h_; }
protected:
    double w_ = 0, h_ = 0;
};

class Square : public Rectangle {
public:
    void setWidth(double w) override  { w_ = h_ = w; }   // must keep w==h
    void setHeight(double h) override { w_ = h_ = h; }
};

void test(Rectangle& r) {
    r.setWidth(5);
    r.setHeight(4);
    assert(r.area() == 20);   // PASSES for Rectangle, FAILS for Square (area == 16)
}
```

`Square` is-a `Rectangle` mathematically, but not *behaviourally* — it
breaks an invariant callers of `Rectangle` are entitled to assume
(independent width/height). This is LSP's real content: inheritance must
preserve the *base class's contract* (preconditions no stronger,
postconditions no weaker, invariants preserved), not just the shape of the
API. The fix is usually to not model `Square` as a `Rectangle` at all —
both implement a `Shape` interface instead, with no inheritance between them.

Other LSP tells: a derived class that throws `NotImplementedError` from an
inherited method (`FlightlessBird : public Bird { void fly() override { throw
...; } }`) is an LSP violation — the base contract promised flight. The fix
is usually to narrow the base interface (ISP, next) so `fly()` isn't part of
the contract every `Bird` must honour.

## I — Interface Segregation Principle

**Clients shouldn't be forced to depend on methods they don't use.** Many
small, focused interfaces beat one large one.

```cpp
// Violation: a fat interface forces irrelevant implementations
class Worker {
public:
    virtual void work() = 0;
    virtual void eat() = 0;    // a RobotWorker has no business implementing this
};

// Fixed: segregated interfaces
class Workable { public: virtual void work() = 0; };
class Eatable  { public: virtual void eat()  = 0; };
class HumanWorker : public Workable, public Eatable { /* both */ };
class RobotWorker  : public Workable              { /* only work */ };
```

This is directly why abstract interfaces in the machine-coding problem set
tend to be small (`Payable`, `Printable`, `Comparable`-style single-method
interfaces) rather than one big `IEverything`. It also composes naturally
with multiple inheritance of *interfaces* (pure abstract classes), which is
safe in C++ in a way multiple inheritance of *implementation* is not (no
diamond-of-state problem, see `03`).

## D — Dependency Inversion Principle

**High-level modules should not depend on low-level modules; both should
depend on abstractions.** Not to be confused with Dependency *Injection*
(a technique that's often used to satisfy DIP, but is a separate concept).

```cpp
// Violation: high-level NotificationService depends directly on a concrete,
// low-level EmailSender — swapping to SMS means editing NotificationService.
class EmailSender { public: void send(const std::string& msg); };
class NotificationService {
    EmailSender sender_;
public:
    void notify(const std::string& msg) { sender_.send(msg); }
};

// Fixed: both depend on an abstraction
class MessageSender {
public:
    virtual void send(const std::string& msg) = 0;
    virtual ~MessageSender() = default;
};
class EmailSender : public MessageSender { void send(const std::string&) override; };
class SmsSender    : public MessageSender { void send(const std::string&) override; };

class NotificationService {
    std::shared_ptr<MessageSender> sender_;   // injected, not owned by concrete choice
public:
    explicit NotificationService(std::shared_ptr<MessageSender> s) : sender_(std::move(s)) {}
    void notify(const std::string& msg) { sender_->send(msg); }
};
```

`NotificationService` (high-level policy) and `EmailSender` (low-level
detail) now both depend on `MessageSender` (the abstraction). This is also
what makes the class **unit-testable** — inject a `MockMessageSender` and
assert on what was sent, with no real network call. If you take one thing
from this section for real-world use: **DIP is the reason your code is
testable**, and "how would you test this?" is one of the most common LLD
follow-ups.

## How SOLID and the patterns connect

| Principle | Pattern(s) that typically enforce it |
| --- | --- |
| SRP | Facade (isolates one responsibility behind a seam), extracting collaborators |
| OCP | Strategy, Factory Method, Decorator, Observer |
| LSP | Discipline in how you design base classes, not a single pattern — verify substitutability by inspection |
| ISP | Small abstract interfaces used throughout Structural/Behavioural patterns |
| DIP | Every pattern that programs against an interface (Strategy, Observer, Bridge, Factory) |

You'll see this table cash out concretely once you're in `07`-`09`.

## Interview angles

**"Walk me through SOLID."** Don't recite definitions — for each letter,
give the one-line violation-and-fix you'd actually say out loud, the way
they're written above. That's the difference between having read this file
and having internalised it.

**"Here's a class (shown fat interface / god class / switch-on-type). What's
wrong with it?"** Identify the specific letter(s) violated, not "it's not
very clean." Then propose the specific pattern that fixes it.

**"How would you test this class?"** If the honest answer involves a real
database, real network call, or real clock, you have a DIP violation — say
so, and describe the abstraction you'd inject instead.

## Exercises

1. Find a `switch` or long `if/else if` on a type/category field in code
   you've written. Identify which principle it violates (usually OCP) and
   rewrite it with polymorphism.
2. Take the `Rectangle`/`Square` example, and instead of inheritance, model
   both as implementations of a `Shape` interface with `area()`. Write the
   test from the LSP section again and confirm it now passes for both,
   because neither claims to *be* the other.
3. Write a `Logger` class that directly opens a file in its constructor
   (a DIP violation — untestable). Refactor it to depend on an abstract
   `LogSink` interface, then write a `InMemoryLogSink` purely for tests.
4. From memory, write one sentence per letter connecting it to a concrete
   code smell you'd recognise on sight.
