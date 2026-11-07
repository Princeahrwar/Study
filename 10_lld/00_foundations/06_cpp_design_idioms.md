# 06 — C++-specific design idioms: Pimpl, CRTP, Type Erasure, NVI

> Prerequisite: `02`, `03`, `05`. These four idioms are what separates "I
> know GoF patterns" from "I know how they're actually built in idiomatic
> modern C++." They're asked less often as a *whole machine-coding problem*
> and more often as a **follow-up question about a design you've already
> proposed** — "how would you hide this class's implementation from the
> header?" or "how would you avoid the vtable cost here?" Know all four by
> name and shape; you'll use at least one of them in almost any Staff-level
> LLD or systems discussion.

## Pimpl (Pointer to Implementation)

**Problem**: a class's private members are visible in its header, which
means (1) every private member's type must be `#include`d by every
translation unit that includes the header, creating compilation
dependencies that don't need to exist, and (2) changing a private member
(even one nobody outside the class touches) forces a rebuild of every file
that includes the header. Pimpl breaks this by hiding all private state
behind one opaque pointer.

```cpp
// widget.h — the public header. Notice: no <expensive_dependency.h>,
// no private data members visible, no rebuild triggered by changing them.
class Widget {
public:
    Widget();
    ~Widget();                          // must be declared here, defined in .cpp
    Widget(Widget&&) noexcept;          // and these too — see below
    Widget& operator=(Widget&&) noexcept;
    void doSomething();
private:
    class Impl;                         // incomplete type in the header
    std::unique_ptr<Impl> impl_;
};

// widget.cpp — the implementation, and the only file that needs the
// expensive dependency or knows the real member types.
#include "widget.h"
#include <expensive_dependency.h>

class Widget::Impl {
public:
    void doSomething() { /* uses ExpensiveDependency internally */ }
private:
    ExpensiveDependency dep_;
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}
Widget::~Widget() = default;                                    // Impl is complete here
Widget::Widget(Widget&&) noexcept = default;
Widget& Widget::operator=(Widget&&) noexcept = default;
void Widget::doSomething() { impl_->doSomething(); }
```

**The gotcha worth having ready**: the destructor (and move operations)
**must** be defined in the `.cpp`, not defaulted implicitly in the header —
`unique_ptr<Impl>`'s destructor needs `Impl` to be a complete type to call
`delete` on it, and `Impl` is only complete in the `.cpp`. Defaulting
`~Widget()` implicitly in the header (by omitting a declaration entirely)
tries to instantiate `unique_ptr<Impl>`'s destructor where `Impl` is still
incomplete — a compile error the first time you actually try it, and a
very standard "gotcha" follow-up question.

**When to reach for it**: a stable public API over a volatile
implementation (an SDK, a library header consumed by many translation
units), or breaking a genuine circular/heavy header dependency. **Cost**:
one extra heap allocation and one extra indirection per `Widget` instance,
plus the boilerplate above — not free, so it's a build-time/API-stability
tradeoff, not a default.

**Relationship to Bridge** (`08_design_patterns_structural.md`): Pimpl is
essentially Bridge with exactly one implementation, applied for
compilation-firewall reasons rather than to let an abstraction vary across
multiple implementations. If asked "isn't this just Bridge?" — yes,
structurally; the *intent* here is build hygiene and ABI stability, not
runtime-swappable implementations.

## CRTP (Curiously Recurring Template Pattern)

**Problem**: you want polymorphic-shaped code (a base class defining
behaviour in terms of operations a derived class implements) **without**
the vtable cost of virtual dispatch — static polymorphism instead of
dynamic. The "curious" part: a class inherits from a template
instantiation of itself.

```cpp
template <typename Derived>
class Shape {
public:
    double area() const { return static_cast<const Derived*>(this)->areaImpl(); }
    // static_cast, not dynamic_cast or virtual dispatch: resolved entirely
    // at compile time, fully inlinable.
};

class Circle : public Shape<Circle> {
public:
    explicit Circle(double r) : r_(r) {}
    double areaImpl() const { return M_PI * r_ * r_; }   // no `override`, not virtual
private:
    double r_;
};
```

Calling `circle.area()` compiles down to a direct call to `Circle::areaImpl()`
— no vtable, no indirect call, fully eligible for inlining. This is the
compile-time-polymorphism mechanism `03_oop_fundamentals.md` named as the
alternative when the type set is closed and the call is hot; CRTP is *how*
you actually write that alternative when you want base-class-shaped shared
code (not just a bare template function) enforcing an interface contract.

**The real cost**: **no runtime polymorphism** — you cannot hold a
`std::vector<Shape<?>*>` of mixed concrete types, because `Shape<Circle>`
and `Shape<Square>` are genuinely different, unrelated types. If you need a
heterogeneous collection processed through one interface, CRTP is the wrong
tool — that's exactly the runtime-polymorphism use case from
`03_oop_fundamentals.md`. CRTP also causes template instantiation bloat (one
full code copy per `Derived` type) and materially worse compile-error
messages than a plain virtual interface — a real cost to name honestly.

**Where it's genuinely used in practice** (good to cite): `std::enable_shared_from_this`
is CRTP. Many high-performance libraries (Eigen, parts of Boost) use it
for exactly this "polymorphic interface, zero runtime cost" tradeoff in
code paths that are both extremely hot and have a closed, known type set.

## Type Erasure

**Problem**: you want a unified interface over types that **don't share a
common base class** and weren't designed to — without forcing every
concrete type to inherit from something. This is how `std::function` works
internally, and it's worth understanding the mechanism because it's a
recurring "how would you implement `std::function` yourself" interview
question at the Staff level.

```cpp
class AnyDrawable {
public:
    template <typename T>
    AnyDrawable(T obj) : self_(std::make_unique<Model<T>>(std::move(obj))) {}

    void draw() const { self_->draw(); }

private:
    struct Concept {                              // the type-erased interface
        virtual void draw() const = 0;
        virtual ~Concept() = default;
    };
    template <typename T>
    struct Model : Concept {                      // one instantiation per T used
        Model(T obj) : obj_(std::move(obj)) {}
        void draw() const override { obj_.draw(); }   // requires T to have .draw(), nothing more
        T obj_;
    };
    std::unique_ptr<Concept> self_;
};

// usage: ThirdPartyCircle and LegacySquare need NOT share any base class —
// they only need a compatible draw() method, checked at the call site above.
struct ThirdPartyCircle { void draw() const; };
struct LegacySquare     { void draw() const; };

std::vector<AnyDrawable> shapes;
shapes.emplace_back(ThirdPartyCircle{});
shapes.emplace_back(LegacySquare{});
for (auto& s : shapes) s.draw();     // uniform call, no common base class anywhere
```

The mechanism: an inner `Concept`/`Model` pair does the actual virtual
dispatch (so there *is* a vtable under the hood — type erasure isn't free,
it typically costs the same as a small heap allocation plus an indirect
call, similar in profile to `std::function`), but the **outer type**
(`AnyDrawable`) is not part of any inheritance hierarchy the wrapped types
need to know about. This is the crucial distinction from a plain `Shape*`
runtime-polymorphism design: **the wrapped types don't need to inherit from
anything**, which matters enormously when wrapping third-party or legacy
types you can't modify (a cleaner alternative to `Adapter`,
`08_design_patterns_structural.md`, when you'd otherwise need one adapter
subclass per third-party type but they all already satisfy the same
implicit interface via structural typing, i.e. duck typing).

**When to reach for it in an interview**: almost never build the full
`Concept`/`Model` machinery from scratch in a 45-minute round unless
explicitly asked to explain `std::function`'s internals — but know the name,
the mechanism, and that `std::function`, `std::any`, and `std::move_only_function`
(C++23) are all type erasure in the standard library, so you can reach for
them instead of hand-rolling this pattern in ordinary application code.

## Non-Virtual Interface (NVI)

**Problem**: a plain `virtual` public method gives derived classes total
control over what happens when it's called — they can skip validation,
logging, or invariant-checking the base class wanted guaranteed on *every*
call. NVI fixes this by making the public method **non-virtual** (so it
can't be overridden or bypassed) and having it call a **private virtual**
method that derived classes *do* override — the base class's pre/post-logic
around that call is then guaranteed to run every time, for every subtype.

```cpp
class Shape {
public:
    double area() const {                 // public, NOT virtual — the contract
        double a = areaImpl();            // delegates to the customization point
        assert(a >= 0.0 && "area must be non-negative — implementation bug");
        return a;
    }
    virtual ~Shape() = default;
private:
    virtual double areaImpl() const = 0;  // private virtual — the customization point
};

class Circle : public Shape {
private:
    double areaImpl() const override { return M_PI * r_ * r_; }   // can't be called directly by clients
    double r_;
};
```

**What this buys you, precisely**: callers can only ever invoke
`Shape::area()`, which means the base class's invariant check
(`assert(a >= 0.0)`) runs unconditionally on every call site, for every
subtype, forever — a derived class *cannot* accidentally or deliberately
bypass it, because it never has access to a callable public virtual to
override-and-skip-the-wrapper on. This is a direct, mechanical enforcement
of the LSP contract-preservation idea from `05_solid_principles.md`: the
base class's promise ("area is never negative") is now structurally
guaranteed, not just documented.

**When to reach for it**: any interface where the base class needs to
guarantee some invariant, logging, or pre/post-condition around every
concrete implementation's behaviour — a `Component::render()` that must
always push/pop a rendering context around a subclass's actual drawing
code is a classic real-world example. **Don't reach for it reflexively**:
plain public virtuals are simpler and are the right default when there's
genuinely nothing the base class needs to guarantee around the call — NVI
without a real invariant to enforce is unnecessary indirection (the KISS
principle from `17_more_principles_and_antipatterns.md`, applied here).

## Quick reference: which idiom answers which question

| Idiom | The question it answers |
| --- | --- |
| Pimpl | "How do I hide implementation details from the header and stop rebuilds cascading?" |
| CRTP | "How do I get polymorphic-shaped code with zero vtable cost, for a closed, hot-path type set?" |
| Type Erasure | "How do I unify unrelated types behind one interface without making them inherit from anything?" |
| NVI | "How do I guarantee a base class's pre/post-conditions run on every call, even from subclasses I don't control?" |

## Interview angles

**"This class's private members are pulling in a huge dependency into every
translation unit that includes it — what would you do?"** Pimpl, and be
ready to state the destructor-in-the-.cpp gotcha unprompted.

**"How would you get polymorphism here without the vtable indirection?"**
CRTP if the type set is closed and known at compile time; `std::variant` +
`std::visit` (per `09`) if it's closed but needs runtime selection; plain
virtual dispatch if the set is genuinely open (plugins, ABI boundaries).

**"How does `std::function` actually work internally?"** Type erasure —
sketch the `Concept`/`Model` shape from memory.

## Exercises

1. Implement the `Widget`/Pimpl example from scratch, deliberately omit the
   destructor declaration in the header first, and confirm you get a
   compile error referencing an incomplete type — then fix it and note
   exactly which line the fix touched.
2. Implement the CRTP `Shape<Derived>` example with `Circle` and `Square`,
   then try (and fail) to put both in a single `std::vector` — write down
   the compiler error and explain in one sentence why it's fundamental to
   the technique, not a bug.
3. Implement `AnyDrawable` from scratch, wrap two structurally-unrelated
   types with a `draw()` method (no shared base class), and confirm both
   work uniformly through the wrapper.
4. Add NVI to any `virtual` method in a solution from
   `01_machine_coding_problems/` where a base class invariant is currently
   only enforced by convention (e.g. `Slot::fits()` in `10_lld/01_machine_coding_problems/02_parking_lot.md`)
   and state, in one sentence, what guarantee you gained.
