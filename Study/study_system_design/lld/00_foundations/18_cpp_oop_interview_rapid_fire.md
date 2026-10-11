# 18 — C++ OOP rapid-fire: the questions asked in almost every LLD round

> Prerequisite: `03`. This is a quick-revision Q&A file, not a tutorial —
> read `03_oop_fundamentals.md` and `lld/notes/01_raii_and_modern_oop.md`
> for the full depth behind each answer. Use this file the way you'd use
> flashcards: cover the answer, say it out loud, check. These are
> C++-specific gotchas that come up regardless of which machine-coding
> problem you're given, usually as a two-minute detour once your class
> diagram has a polymorphic hierarchy in it.

## Q: Why must a base class have a virtual destructor if you delete through a base pointer?

**A**: Without it, `delete basePtr;` where `basePtr` points to a derived
object only calls the base class's destructor — the derived part is never
destroyed. This is undefined behaviour, and in practice it leaks any
resources the derived class owns (heap memory, file handles, mutexes).

```cpp
class Base    { public: ~Base() {} };                 // NOT virtual — bug
class Derived : public Base { public: ~Derived() { delete[] data_; } private: int* data_ = new int[100]; };

Base* p = new Derived();
delete p;   // UB: only ~Base() runs, data_ leaks
```

Fix: `virtual ~Base() = default;` (or a real body). **Rule of thumb**: any
class with a virtual function should have a virtual destructor — if it's
meant to be used polymorphically at all, it needs one.

## Q: What's the difference between an abstract class and an interface in C++?

**A**: C++ has no `interface` keyword. An "interface" in C++ is a
convention: a class with **only pure virtual functions and no data
members** — a pure abstract class. An "abstract class" more generally is
any class with **at least one** pure virtual function (it may also have
data members and non-virtual concrete methods) — it can't be instantiated,
but it's not necessarily a pure interface.

```cpp
class Shape {                          // interface (pure abstract, no state)
public:
    virtual double area() const = 0;
    virtual ~Shape() = default;
};
class Vehicle {                        // abstract, but not a pure interface —
public:                                // it has state and a concrete method
    virtual void drive() = 0;
    int wheelCount() const { return wheels_; }   // concrete, shared
protected:
    int wheels_ = 4;
};
```

## Q: Overloading vs. overriding — what's actually different?

**A**: **Overloading** — multiple functions with the *same name, different
parameter list*, in the *same scope*, resolved at **compile time** based on
argument types. **Overriding** — a derived class redefining a *virtual*
function with the *same signature* as its base, resolved at **runtime**
based on the object's actual type. Different mechanisms entirely: one is
static polymorphism (`03_oop_fundamentals.md`), the other dynamic.

## Q: What does `= 0` on a virtual function actually mean, and can it still have a body?

**A**: `virtual void f() = 0;` marks `f` **pure virtual** — the class
becomes abstract (can't be instantiated) and every concrete derived class
must override it (or remain abstract itself). Counter-intuitively, a pure
virtual function **can** still have a body — `Base::f()` can be defined
out-of-line and called explicitly via `Base::f()` from a derived override,
useful for a "default implementation subclasses can opt into." **A pure
virtual destructor is a real, sometimes-used idiom** specifically to force a
class to be abstract when it otherwise has no other pure virtuals — but it
*must* still be defined (destructors are always called up the chain during
destruction, pure or not):

```cpp
class AbstractBase {
public:
    virtual ~AbstractBase() = 0;   // pure virtual, but still needs a definition
};
AbstractBase::~AbstractBase() {}   // required, even though it's pure virtual
```

## Q: Why doesn't calling a virtual function from a constructor do what you'd expect?

**A**: During a base class's constructor, the object's vtable pointer is
set to the **base class's** vtable — the derived part of the object doesn't
exist yet. So a virtual call made from inside a base constructor resolves
to the **base class's** implementation, never the derived override, even if
the object being constructed is ultimately a derived type. (Symmetric issue
in destructors, in reverse order, for the same reason.) This is a specific,
well-known gotcha — the takeaway to state if asked: don't rely on
polymorphic dispatch during construction or destruction.

## Q: What is object slicing?

**A**: Assigning or passing a derived object **by value** to something
expecting the base type copies only the base portion — the derived-specific
data and its vtable pointer are lost ("sliced off"). Covered in depth in
`03_oop_fundamentals.md`. The fix, always: pass/store polymorphic types by
pointer or reference, never by value.

## Q: What's the diamond problem and how do you fix it?

**A**: Two base classes both derive from a common ancestor; a class
inheriting from both ends up with two copies of the ancestor's state unless
you use `virtual` inheritance (`class B : public virtual A`), which makes
both paths share a single ancestor subobject. Covered in depth in
`03_oop_fundamentals.md`, including why this track's default stance is to
avoid multiple inheritance of implementation entirely rather than reach for
virtual inheritance.

## Q: Rule of Zero / Three / Five — what are they?

**A**: **Rule of Three** (pre-C++11): if you need a custom destructor,
copy constructor, or copy assignment operator, you almost certainly need
all three (they all typically exist because the class manages a resource
manually). **Rule of Five** (C++11+): add move constructor and move
assignment to that list — if you don't declare them, and you *have*
declared any of the other three, move operations won't be implicitly
generated and you silently fall back to copies. **Rule of Zero** (the
actual modern goal): design classes so they **own no raw resources
directly** — wrap resources in RAII types (`unique_ptr`, `vector`, etc.)
as members, and let the compiler generate all five special members for
free, correctly. Full depth: `lld/notes/01_raii_and_modern_oop.md`.

## Q: Can a constructor be virtual? If not, how do you get "virtual construction"?

**A**: No — virtual dispatch requires a vtable pointer that's only set up
*during* construction, so there's no mechanism to virtually dispatch *to* a
constructor before the object (and its type) exists. To get
construct-a-similar-object-of-the-right-derived-type behaviour, use the
**Prototype pattern's `clone()`** (`00_foundations/07_design_patterns_creational.md`)
— sometimes informally called the "virtual constructor idiom" for exactly
this reason: it's a regular virtual function that internally constructs a
new object of the correct derived type.

## Q: `const` methods — why mark a method `const`, and what does it actually enforce?

**A**: A `const` method promises it won't modify the object's observable
state (specifically: won't modify non-`mutable` members through `this`).
The compiler enforces this and, importantly, **only `const` methods can be
called on a `const` object or through a `const&`/`const*`** — so it's not
just documentation, it changes what code compiles. Mark every method
`const` that logically doesn't mutate state; it's a cheap, real signal of
careful API design, and it directly enables patterns like the
`shared_mutex` reader path in `10_concurrency_for_lld.md`, where read-only
methods need to be callable through a `const` reference.

## Q: Deep copy vs. shallow copy — where does this actually bite you?

**A**: A shallow copy (the compiler-generated default when a class holds a
raw pointer) copies the *pointer value*, not what it points to — two
objects now share one underlying resource, and when both destructors run,
you get a **double free**. A deep copy allocates a new resource and copies
the pointed-to data. This is exactly why the Rule of Three/Five exists:
any class managing a raw resource needs a hand-written copy constructor and
assignment operator that deep-copy, or (the Rule of Zero answer, and the
one to actually prefer) needs to not hold a raw pointer at all — wrap it in
something that manages this correctly for you (`unique_ptr` disables
copying by default and forces you to be explicit; `shared_ptr` gives you
correct shared ownership with reference counting).

## Exercises

1. Write the `Base`/`Derived` virtual-destructor-leak example. Leak
   detection specifically is platform-dependent — ASan's leak detector
   works on Linux but not macOS, and `valgrind` has little to no support
   on Apple Silicon Macs — so on a Mac, confirm the leak the more direct
   way: give `Derived` a destructor that prints "Derived destroyed" and
   confirm, with and without `virtual` on `~Base()`, whether that print
   happens when you `delete` through a `Base*`. On Linux, `-fsanitize=address`
   (leak detection included there) or `valgrind --leak-check=full` both
   work and give you a formal report instead of relying on the print.
2. Write a class demonstrating the constructor-virtual-call gotcha: a base
   constructor calling a virtual method, overridden in a derived class,
   and confirm at runtime which version actually executes.
3. Write a class holding a raw `int*` with a compiler-generated (not
   hand-written) copy constructor, copy it, let both copies go out of
   scope, and reproduce the double-free under a sanitizer. Then fix it two
   ways: a hand-written deep-copying copy constructor, and switching the
   member to `std::unique_ptr<int>` instead (noting the class becomes
   move-only unless you add explicit deep-copy semantics back).
4. Go through this file's ten questions from memory, out loud, without
   looking — the same rehearsal habit as everywhere else in this track.
