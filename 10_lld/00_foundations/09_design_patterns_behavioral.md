# 09 — Behavioural patterns

> Prerequisite: `03`-`08`. Behavioural patterns solve one problem family:
> **how objects communicate and distribute responsibility for an algorithm
> or a flow of control** — as opposed to Creational (how objects are made)
> or Structural (how objects are composed). This is the largest catalog and
> the one used most directly across the machine-coding problem set in
> `01_machine_coding_problems/`.

Ten patterns. Given the count, each entry is tighter than `07`/`08` — problem,
minimal code, the one distinction interviewers actually probe.

## Strategy

**Problem**: a family of interchangeable algorithms for the same task
(pricing, sorting, route-finding), selected at runtime, swappable without
touching the client that uses them. The single most-used pattern in the
machine-coding problem set — parking pricing, fare calculation, split logic
all use it.

```cpp
class PricingStrategy { public: virtual double price(int hours) const = 0; virtual ~PricingStrategy()=default; };
class HourlyPricing : public PricingStrategy { double price(int hours) const override { return hours * 10.0; } };
class FlatPricing    : public PricingStrategy { double price(int) const override { return 25.0; } };

class ParkingTicket {
public:
    explicit ParkingTicket(std::shared_ptr<PricingStrategy> s) : strategy_(std::move(s)) {}
    double cost(int hours) const { return strategy_->price(hours); }
private:
    std::shared_ptr<PricingStrategy> strategy_;
};
```

**Interview tell**: "different ways to calculate/validate/sort X, chosen at
runtime or by config" → Strategy. Directly satisfies OCP (`05`): new pricing
tier = new class, zero edits to `ParkingTicket`.

**Modern C++ alternative — `std::function` instead of a class hierarchy**:
when a strategy is a single operation and callers would rather pass a
lambda inline than name and instantiate a class, `std::function` gives you
the same runtime-swappable behaviour with less boilerplate. Covered in full,
including exactly when to prefer each form, in
`02_stl_for_system_design.md` — the short version: class hierarchy for
multi-method or stateful strategies that are real domain vocabulary
(`CreditCardPayment`, `UpiPayment`); `std::function` for a single-method,
often ad hoc strategy where naming a class buys nothing.

## Observer

**Problem**: when one object's state changes, an open-ended set of other
objects need to be notified, without the subject knowing concretely who
they are.

```cpp
class Observer { public: virtual void onUpdate(double temp) = 0; virtual ~Observer()=default; };

class WeatherStation {
public:
    void subscribe(std::shared_ptr<Observer> o) { observers_.push_back(o); }
    void setTemperature(double t) {
        temp_ = t;
        for (auto& o : observers_) o->onUpdate(temp_);   // push notification
    }
private:
    double temp_ = 0;
    std::vector<std::shared_ptr<Observer>> observers_;
};
class PhoneDisplay : public Observer { void onUpdate(double t) override { /* redraw */ } };
```

**Interview tells**: "notify all subscribers when X happens" → Observer. This
is also the pattern underneath pub/sub systems and UI event handling — worth
naming that connection. **Pitfall to mention if asked**: holding raw
`Observer*` pointers risks dangling references if an observer is destroyed
without unsubscribing; `weak_ptr` or an explicit `unsubscribe()` is the fix,
and bringing this up unprompted is a strong signal.

**The `weak_ptr` fix, made concrete**:

```cpp
class WeatherStation {
public:
    void subscribe(std::weak_ptr<Observer> o) { observers_.push_back(std::move(o)); }
    void setTemperature(double t) {
        temp_ = t;
        for (auto it = observers_.begin(); it != observers_.end(); ) {
            if (auto sp = it->lock()) { sp->onUpdate(temp_); ++it; }   // still alive: notify
            else it = observers_.erase(it);                            // expired: prune it
        }
    }
private:
    double temp_ = 0;
    std::vector<std::weak_ptr<Observer>> observers_;   // non-owning: doesn't keep observers alive
};
```

`weak_ptr::lock()` atomically checks whether the observed object is still
alive and returns a `shared_ptr` to it if so (or an empty one if it was
already destroyed) — this is what makes the check-then-notify sequence safe
even if the observer is destroyed on another thread between the check and
the call, which a raw-pointer null-check cannot guarantee.

## Command

**Problem**: encapsulate a request (an action plus its parameters) as an
object, so it can be queued, logged, undone, or passed around independently
of whoever invoked it.

```cpp
class Command { public: virtual void execute() = 0; virtual void undo() = 0; virtual ~Command()=default; };

class AddTextCommand : public Command {
public:
    AddTextCommand(Document& d, std::string t) : doc_(d), text_(std::move(t)) {}
    void execute() override { doc_.append(text_); }
    void undo()    override { doc_.removeLast(text_.size()); }
private:
    Document& doc_;
    std::string text_;
};

class CommandHistory {
public:
    void execute(std::unique_ptr<Command> c) { c->execute(); history_.push_back(std::move(c)); }
    void undoLast() { if (!history_.empty()) { history_.back()->undo(); history_.pop_back(); } }
private:
    std::vector<std::unique_ptr<Command>> history_;
};
```

**Interview tell**: "undo/redo," "queue of operations," "macro/batch of
actions," "remote-control-style invoker decoupled from receiver" → Command.

## State

**Problem**: an object's behaviour changes based on its internal state, and
you want to avoid a large conditional (`if state == X ... else if state ==
Y ...`) scattered across every method. Each state becomes a class.

```cpp
class VendingMachineState { public: virtual void insertCoin(VendingMachine&) = 0; virtual void selectItem(VendingMachine&) = 0; virtual ~VendingMachineState()=default; };
class IdleState : public VendingMachineState {
    void insertCoin(VendingMachine& m) override;   // transitions to HasCoinState
    void selectItem(VendingMachine&) override { /* error: no coin inserted */ }
};
class HasCoinState : public VendingMachineState {
    void insertCoin(VendingMachine&) override { /* error: coin already inserted */ }
    void selectItem(VendingMachine& m) override;   // transitions to DispensingState
};
```

**Distinction from Strategy that's asked constantly, memorise it exactly**:
structurally near-identical (both hold a pointer to an interchangeable
object behind an interface). **Strategy** is chosen once by the *client* and
represents an *algorithm choice*; the object doesn't switch strategies on
its own. **State** is switched *internally by the object itself* as a
*consequence of its own behaviour*, modelling a state machine — the object
transitions itself from `IdleState` to `HasCoinState` as a side effect of
`insertCoin()`. Intent (algorithm choice vs state machine) is the whole
difference; the UML looks almost the same.

## Template Method

**Problem**: an algorithm has a fixed overall skeleton, but some steps vary
by subclass. Put the skeleton in the base class as a non-virtual method that
calls a sequence of virtual "hook" steps.

```cpp
class DataProcessor {
public:
    void process() {          // the template method: fixed skeleton, not overridden
        readData();
        transform();          // varies by subclass
        writeData();
    }
    virtual ~DataProcessor() = default;
protected:
    void readData();          // shared, not virtual
    virtual void transform() = 0;   // the hook subclasses fill in
    void writeData();         // shared, not virtual
};
class CsvProcessor : public DataProcessor { void transform() override; };
class JsonProcessor : public DataProcessor { void transform() override; };
```

**Interview tell**: "same overall steps every time, but one step differs by
type" → Template Method. **Distinction from Strategy**: Template Method
varies one *step* of an algorithm via inheritance (fixed skeleton, base
class controls the flow); Strategy swaps the *entire* algorithm via
composition (no shared skeleton, client controls which one runs). If you
find yourself wanting to vary more than one step independently, or wanting
runtime swapping, that's a signal to prefer Strategy or composition over
Template Method's inheritance-based hook mechanism.

## Chain of Responsibility

**Problem**: a request should be handled by one of several possible
handlers, tried in sequence, without the sender knowing which one will
actually handle it. Each handler either processes the request or passes it
to the next.

```cpp
class Handler {
public:
    void setNext(std::shared_ptr<Handler> next) { next_ = std::move(next); }
    virtual void handle(Request& r) {
        if (next_) next_->handle(r);   // default: pass along
    }
    virtual ~Handler() = default;
protected:
    std::shared_ptr<Handler> next_;
};
class AuthHandler : public Handler {
    void handle(Request& r) override {
        if (!r.isAuthenticated()) { r.reject("unauthenticated"); return; }
        Handler::handle(r);   // pass to next
    }
};
class RateLimitHandler : public Handler { /* similar */ };
```

**Interview tell**: "middleware pipeline," "sequence of validators/filters,
each of which can short-circuit," "escalation" (support ticket routed L1 →
L2 → L3) → Chain of Responsibility. Note this is exactly the shape of an
HTTP middleware stack or a Linux interrupt handler chain — you have a real
example to cite from `09_drivers/05_interrupt_handling.md`.

## Iterator

**Problem**: provide sequential access to elements of a collection without
exposing its underlying representation. In C++ this is largely *the
language's own iterator protocol* — you rarely hand-roll this pattern from
scratch in an interview, but you're expected to know it by name and explain
why `begin()`/`end()` + `operator++`/`operator*` is Iterator applied to
every STL container.

```cpp
class Playlist {
public:
    class Iterator {
    public:
        explicit Iterator(std::vector<Song>::iterator it) : it_(it) {}
        Song& operator*() { return *it_; }
        Iterator& operator++() { ++it_; return *this; }
        bool operator!=(const Iterator& o) const { return it_ != o.it_; }
    private:
        std::vector<Song>::iterator it_;
    };
    Iterator begin() { return Iterator(songs_.begin()); }
    Iterator end()   { return Iterator(songs_.end()); }
private:
    std::vector<Song> songs_;
};
```

**Interview tell**: mostly asked as a definition-and-recognition question
("what pattern is `begin()`/`end()`?") rather than something you implement
from scratch — know the answer, don't over-invest implementation time here.

## Mediator

**Problem**: many objects communicate with each other in a tangled
many-to-many web (each knows about several others directly). A Mediator
centralises that communication so objects only know about the mediator, not
each other.

```cpp
class ChatMediator { public: virtual void sendMessage(const std::string& msg, class User* from) = 0; virtual ~ChatMediator()=default; };
class ChatRoom : public ChatMediator {
public:
    void addUser(User* u) { users_.push_back(u); }
    void sendMessage(const std::string& msg, User* from) override {
        for (auto* u : users_) if (u != from) u->receive(msg);   // users never call each other directly
    }
private:
    std::vector<User*> users_;
};
```

**Interview tell**: "N objects need to talk to each other, and pairwise
coupling is getting out of hand" → Mediator. **Distinction from Facade**:
Facade simplifies calling *into* an existing subsystem from outside;
Mediator centralises communication *among peers inside* a group that would
otherwise reference each other directly.

## Memento

**Problem**: capture an object's internal state so it can be restored later
(undo, checkpoints, snapshots), **without violating encapsulation** — the
object exposes a snapshot but doesn't let outsiders directly poke its
internals.

```cpp
class EditorMemento {           // opaque to everyone except Editor
    friend class Editor;
    std::string content_;
    explicit EditorMemento(std::string c) : content_(std::move(c)) {}
};
class Editor {
public:
    void type(const std::string& s) { content_ += s; }
    EditorMemento save() const { return EditorMemento(content_); }
    void restore(const EditorMemento& m) { content_ = m.content_; }
private:
    std::string content_;
};
```

**Interview tell**: "undo/checkpoint that shouldn't expose internal fields
publicly" → Memento. **Distinction from Command's undo**: Command's `undo()`
reverses an *action* (often by computing the inverse operation); Memento
restores a full *snapshot of state*. They're frequently combined — a
`Command` that saves a `Memento` before executing, and restores it on
`undo()`, rather than computing an inverse.

## Visitor

**Problem**: you need to add new *operations* across a stable class
hierarchy, without modifying each class in the hierarchy every time you add
an operation — the inverse concern from Template Method/Strategy, where the
type hierarchy is what's stable and operations are what vary.

```cpp
class Circle; class Square;   // fwd decl
class ShapeVisitor {
public:
    virtual void visit(Circle&) = 0;
    virtual void visit(Square&) = 0;
    virtual ~ShapeVisitor() = default;
};
class Shape { public: virtual void accept(ShapeVisitor& v) = 0; virtual ~Shape()=default; };
class Circle : public Shape { public: void accept(ShapeVisitor& v) override { v.visit(*this); } };
class Square : public Shape { public: void accept(ShapeVisitor& v) override { v.visit(*this); } };

class AreaVisitor : public ShapeVisitor {
public:
    void visit(Circle& c) override { /* compute and accumulate */ }
    void visit(Square& s) override { /* compute and accumulate */ }
};
// new operation (e.g. ExportSvgVisitor) added later: zero edits to Circle/Square
```

**Honest framing**: Visitor is the pattern most likely to read as
over-engineering if the class hierarchy or the operation set isn't
genuinely both large and separately-evolving — it's real double-dispatch
machinery for a fairly narrow situation (stable types, growing operations).
Know it for recognition and for the rare case the requirement is literally
"add new operations without touching the existing type hierarchy," but don't
reach for it by default the way you might Strategy or Observer.

**The modern C++ alternative — `std::variant` + `std::visit` (C++17)**:
when the set of concrete types is **closed and known** (you're not
expecting third parties to add new shapes to your hierarchy), `variant` +
`visit` gives you the same "add a new operation without touching the
types" benefit as Visitor, with no heap allocation, no vtable, and the
compiler enforcing exhaustiveness at compile time — you cannot forget to
handle a type in the variant without a compile error.

```cpp
using Shape = std::variant<Circle, Square>;   // closed set: exactly these two

struct AreaVisitor {
    double operator()(const Circle& c) const { return M_PI * c.r * c.r; }
    double operator()(const Square& s) const { return s.side * s.side; }
};

std::vector<Shape> shapes = { Circle{2.0}, Square{3.0} };
for (auto& s : shapes) {
    double area = std::visit(AreaVisitor{}, s);   // dispatches to the right operator()
}
```

**This is exactly the closed-set-runtime-choice case named in
`10_lld/02_systems_lld/01_raii_and_modern_oop.md`'s polymorphism-cost discussion**:
GoF Visitor is the right tool when the type hierarchy is **open** (plugins,
types defined outside your control, an `accept()` method you can add to
every type) — `variant`/`visit` is strictly better when it's **closed**,
because you get the same extensibility-of-operations benefit for less
runtime cost and with compile-time exhaustiveness checking. In a modern
C++ interview, naming `variant`/`visit` as the default and classic Visitor
as the fallback for an open hierarchy is the stronger answer.

## Interpreter — named for completeness, rarely implemented live

The eleventh and last GoF Behavioural pattern. **Problem**: given a grammar
for a simple language, represent each rule as a class and interpret
sentences in that language by composing them — a `Context` plus an
`Expression` tree where each node's `interpret()` recurses into its
children (e.g. arithmetic expressions: `NumberExpression`,
`AddExpression(left, right)`, `AddExpression::interpret()` returns
`left->interpret() + right->interpret()`).

Genuinely useful for building rule engines, simple query languages, or
expression evaluators — but it's the one Behavioural pattern that almost
never appears as a full machine-coding prompt on its own, because it's a
narrow, specialised tool rather than a general OOD technique. Know the name
and the one-line description well enough to recognise it if a problem
statement says "evaluate expressions" or "parse a small rule language" — if
that's the actual ask, it's worth naming as the fit; otherwise it's not
worth rehearsing implementation for the way Strategy/Observer/State are.

## Null Object — a pattern beyond the 23, worth knowing anyway

Not one of the original GoF 23, but common enough in modern interview
prep lists that it's worth having ready. **Problem**: code is littered
with null checks before using an optional collaborator —

```cpp
if (logger_ != nullptr) {
    logger_->log("something happened");
}
```

— repeated at every call site, easy to forget once, and it clutters the
actual logic with defensive checks. **Fix**: instead of `nullptr`,
provide a real object that implements the same interface but does
nothing:

```cpp
class NullLogger : public LogSink {
public:
    void write(const LogMessage&) override { /* deliberately does nothing */ }
};

// construction: inject either a real sink or NullLogger, caller no longer cares
logger_->write(msg);   // always safe to call — no null check needed anywhere, ever
```

Now every call site can unconditionally call `logger_->write(...)` — if
no real logger was configured, `NullLogger` just silently absorbs the
call. This directly extends `10_lld/01_machine_coding_problems/12_logger_framework.md`'s `LogSink`
interface with zero changes to `Logger` itself — a `NullLogger` is just
another `LogSink` implementation, which is exactly the OCP payoff this
whole pattern trades on.

**When it's worth the extra class vs. just keeping the null check**: when
the same "is this null?" check is repeated at **many** call sites for the
**same** collaborator — one check in one place is often simpler than a
whole extra class; a dozen scattered checks are exactly what Null Object
exists to eliminate. Don't reach for it reflexively for a single,
one-off null check — that's the KISS discipline from
`17_more_principles_and_antipatterns.md` applied here too.

## Strategy vs State vs Template Method — the disambiguation interviewers actually want

This trio gets confused constantly because all three involve swapping
behaviour through an interface. One table, memorise it:

| | Who decides the behaviour | Changes at runtime by itself? | Mechanism |
| --- | --- | --- | --- |
| **Strategy** | The client, once, from outside | No — client swaps it explicitly | Composition, one interface, one active implementation |
| **State** | The object, as a side effect of its own methods | Yes — internal self-transition | Composition, one interface, object reassigns its own pointer |
| **Template Method** | The base class controls the skeleton; subclass fixes one step at compile time | No — fixed at the subclass's type | Inheritance, one fixed algorithm shape with virtual hooks |

## Quick reference

| Pattern | Tell in the problem statement |
| --- | --- |
| Strategy | "different algorithms/rules, chosen once, swappable" |
| Observer | "notify subscribers when X changes" |
| Command | "undo/redo, queue, or log of actions" |
| State | "behaviour changes based on internal state, object transitions itself" |
| Template Method | "same steps every time, one step differs by type" |
| Chain of Responsibility | "pipeline of handlers, each can short-circuit" |
| Iterator | "traverse a collection without exposing its structure" (usually just STL) |
| Mediator | "many peers, tangled pairwise communication" |
| Memento | "undo/checkpoint without exposing internals" |
| Visitor | "add new operations to a stable type hierarchy without editing it" |
| Interpreter | "evaluate expressions / parse a small rule language or grammar" |
| Null Object | "eliminate repeated null checks for an optional collaborator" |

## Interview angles

**"Strategy or State here?"** Ask yourself: does the object switch this
itself as a consequence of its own logic (State), or does a client pick it
once from outside (Strategy)? Answer with the mechanism, not a guess.

**"How would you add undo to this?"** Command (if it's action-based) or
Memento (if it's snapshot-based) — and know that combining them is normal.

## Exercises

1. Implement the `VendingMachine` State example fully — four states (Idle,
   HasCoin, Dispensing, OutOfStock), each transition explicit. Then explain
   in one sentence why this isn't Strategy.
2. Implement `Chain of Responsibility` for a request pipeline:
   `AuthHandler` → `RateLimitHandler` → `ValidationHandler` → business logic.
   Confirm any handler can short-circuit and the rest never run.
3. Implement `Observer` for a stock-price ticker with two observer types
   (a `ConsoleDisplay`, a `AlertObserver` that only fires above a threshold).
   Add `unsubscribe()` and demonstrate the dangling-pointer risk with a raw
   pointer, then fix it with `weak_ptr`.
4. From memory, fill in the Strategy/State/Template Method table above
   without looking, then check it against this file.
5. Implement `NullLogger` against `10_lld/01_machine_coding_problems/12_logger_framework.md`'s `LogSink`
   interface, wire it in as the default when no real sink is configured,
   and confirm every call site that calls `logger_->write(...)` needs zero
   null checks, before or after the change.
