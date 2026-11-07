# 01 — Memory management and ownership: RAII, smart pointers, move semantics

> No prerequisites beyond writing C++ day to day. This is Phase 1 of the
> LLD track for a reason the syllabus states explicitly: **how C++ manages
> memory, ownership, and object lifecycles dictates your architectural
> choices** — before you can reason about whether a relationship in a class
> diagram should be composition or aggregation (`04_uml_and_modeling.md`),
> you need the ownership vocabulary that makes those two words mean
> something concrete in code, not just on a whiteboard.
>
> This file covers the ground at the depth this *design* track needs. For
> the deeper, assembly-level treatment (exact `noexcept`-move optimisation
> behaviour, the full ownership-cost table, `std::atomic_ref`, and more).
> `10_lld/02_systems_lld/01_raii_and_modern_oop.md` and `10_lld/02_systems_lld/02_cpp_memory_model.md`
> pick up from here at Staff depth — read them once this file is solid,
> not instead of it.

## RAII: the idea everything else in this file is a special case of

**Resource Acquisition Is Initialization**: tie a resource's lifetime to an
object's lifetime. Acquire the resource in the constructor; release it in
the destructor. The compiler then guarantees release happens — on every
exit path, including an exception unwinding through the function — because
destructors for stack-allocated objects always run when they go out of
scope, no matter how control leaves the scope.

```cpp
class FileHandle {
public:
    explicit FileHandle(const char* path) : fp_(std::fopen(path, "r")) {
        if (!fp_) throw std::runtime_error("failed to open file");
    }
    ~FileHandle() { if (fp_) std::fclose(fp_); }   // guaranteed to run

    FileHandle(const FileHandle&) = delete;             // see "Rule of
    FileHandle& operator=(const FileHandle&) = delete;  // Three/Five" below

    FILE* get() const { return fp_; }
private:
    FILE* fp_;
};

void process(const char* path) {
    FileHandle f(path);         // resource acquired
    // ... do work, maybe throw ...
}                                // ~FileHandle() runs here, always —
                                 // whether this line is reached normally
                                 // or via an exception unwinding through it
```

Compare this to manual `fopen`/`fclose`: every early `return`, every
exception path, every new code path added six months later by someone else
is a place the matching `fclose` can be forgotten. RAII doesn't make that
discipline optional — it makes the compiler enforce it. This is the single
idea that "resource" in C++ generalizes to: heap memory, file handles,
network sockets, mutex locks, database connections — anything with an
acquire/release pair becomes leak-proof the same way, via the same
mechanism. `std::lock_guard` (mutex), `std::unique_ptr` (heap memory), and
`std::fstream` (a file, already RAII-wrapped for you) are all this same
pattern, applied by the standard library so you rarely write the raw
`FileHandle` example above yourself in practice — but understanding that
they're all instances of one idea is what lets you apply it to your *own*
resource types.

## `std::unique_ptr<T>` — exclusive ownership, the default

**One owner. Zero runtime overhead over a raw pointer** (no reference
counting — it's just a raw pointer with compiler-enforced move-only
semantics and an automatic `delete` in its destructor). This is your
**default choice** whenever an object needs to own a heap-allocated
resource exclusively — composition relationships in `04_uml_and_modeling.md`,
and factory functions returning a newly-created object.

```cpp
std::unique_ptr<Engine> engine = std::make_unique<Engine>();  // always prefer make_unique
                                                                 // over `new` + unique_ptr construction —
                                                                 // exception-safety, see below

class Car {
public:
    explicit Car(std::unique_ptr<Engine> engine) : engine_(std::move(engine)) {}
    // engine_ is destroyed automatically when Car is destroyed — composition,
    // exactly the UML relationship from 04_uml_and_modeling.md
private:
    std::unique_ptr<Engine> engine_;
};
```

**Why `unique_ptr` cannot be copied, only moved**: if it could be copied,
two pointers would both believe they owned the same object, and both
destructors would call `delete` on it — a double free. The compiler
enforces single ownership by deleting the copy constructor/assignment and
providing only *move* versions, which transfer ownership (the source
becomes null) rather than duplicate it.

**Why `make_unique` over `new` + a `unique_ptr` constructor**: consider
`f(std::unique_ptr<T>(new T()), mayThrow())`. C++ does not guarantee
argument evaluation order between `new T()` and `mayThrow()` — if
`mayThrow()` runs and throws *after* `new T()` succeeded but *before* the
raw pointer is wrapped in a `unique_ptr`, that memory leaks, because
nothing owns it yet at the moment of the exception. `std::make_unique<T>()`
does the allocation and the wrapping as a single, non-interruptible
operation, closing that gap entirely. Prefer it unconditionally.

## `std::shared_ptr<T>` — shared ownership, and its real cost

**Multiple owners, reference-counted.** The object is destroyed when the
*last* `shared_ptr` to it is destroyed. Reach for it only when lifetime is
genuinely shared and dynamic — a cache holding entries that outlive any
single request, a DAG where a node can have multiple parents, an object
shared across multiple subsystems where no single one is the "real" owner.

```cpp
std::shared_ptr<CacheEntry> entry = std::make_shared<CacheEntry>(...);  // prefer make_shared,
                                                                           // same reasoning as make_unique,
                                                                           // plus one fewer allocation
                                                                           // (control block + object combined)
```

**The cost, worth stating precisely because it's a real interview
question**: the reference count is updated with **atomic** increment/
decrement on every copy/destruction — because a `shared_ptr` might be
copied across threads, the standard requires this even in single-threaded
code. That's a real, measurable cost in a hot path — copying a `shared_ptr`
per packet across threads in a data-plane loop is a cache-coherence event
on a shared cache line, not a free operation. **`shared_ptr` is not a
"safer default" to reach for out of caution** — `unique_ptr` is the
default; `shared_ptr` is a deliberate choice justified by genuine shared,
dynamic lifetime, the same way reaching for a lock in
`10_concurrency_for_lld.md` is justified by genuine shared mutable state,
not applied everywhere "to be safe."

## `std::weak_ptr<T>` — breaking cycles, observing without owning

**A non-owning reference to an object managed by `shared_ptr`, that
doesn't keep it alive**, and can be checked ("is it still alive?") safely
even across threads via `.lock()`.

**The problem it solves**: two objects holding `shared_ptr`s to each other
form a reference cycle — each keeps the other's count above zero forever,
so neither is ever destroyed, even when nothing external references either
one. Classic case: a parent-child tree where the child also needs a
back-pointer to its parent.

```cpp
class Child;
class Parent {
public:
    std::vector<std::shared_ptr<Child>> children_;   // parent owns children: shared_ptr
};
class Child {
public:
    std::weak_ptr<Parent> parent_;   // child does NOT own the parent: weak_ptr breaks the cycle
};
```

If `Child::parent_` were also a `shared_ptr`, `Parent` and each `Child`
would keep each other alive forever — a memory leak neither destructor ever
runs to fix. `weak_ptr` lets the child *reach* its parent
(`parent_.lock()`, which returns a `shared_ptr` if the parent is still
alive, or an empty one if not) without *owning* it.

**The other major use — already seen in the Observer pattern**
(`09_design_patterns_behavioral.md`): a subject holding `weak_ptr<Observer>`
in its subscriber list, so a destroyed observer doesn't leave a dangling
pointer the subject might call into — `lock()` safely detects "this
observer no longer exists" instead of crashing on a stale raw pointer.

## Move semantics and rvalue references — transferring, not copying

**An rvalue reference (`T&&`) binds to a temporary — something about to be
destroyed anyway** — which means it's safe to *steal* its internals rather
than copy them, since nobody will use the original afterward.

```cpp
class Buffer {
public:
    // Move constructor: steal the other buffer's pointer, leave it empty.
    Buffer(Buffer&& other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // the moved-from object must be left in a
        other.size_ = 0;         // valid (destructible, assignable) but
    }                             // unspecified state — this is that state

    // Copy constructor, for contrast: allocates and duplicates.
    Buffer(const Buffer& other) : size_(other.size_) {
        data_ = new int[size_];
        std::copy(other.data_, other.data_ + size_, data_);
    }
private:
    int* data_;
    size_t size_;
};

Buffer makeBuffer() {
    Buffer b(1000);
    return b;   // moved out, not copied — modern compilers usually elide
}               // this entirely (RVO/NRVO), but when they can't, move
                // semantics make it O(1) instead of O(n)
```

**`std::move` does not move anything** — worth stating exactly this way if
asked, because it's a precise, common gotcha. `std::move(x)` is purely a
**cast**: it tells the compiler "treat `x` as an rvalue," making it
eligible to bind to a `T&&` overload (the move constructor/assignment)
instead of the `const T&` overload (the copy constructor/assignment). The
actual "moving" — stealing the pointer, zeroing the source — happens inside
whatever move constructor/assignment gets selected as a result of that
cast. If a type has no move constructor, `std::move(x)` still compiles; it
just silently falls back to a copy.

**Why this matters for API design, and comes up constantly**: a function
signature like `void consume(std::unique_ptr<Widget> w)` (by value) forces
the caller to `std::move` their `unique_ptr` in — the signature itself
documents "this function takes ownership," which is exactly the ownership-
signalling idea from `10_lld/02_systems_lld/01_raii_and_modern_oop.md`'s signature-
convention table.

## The Rule of Three, Rule of Five, Rule of Zero

**Rule of Three** (pre-C++11): if a class needs a custom **destructor**,
that's usually because it manages a resource manually (a raw pointer, a
file handle) — and if it needs a custom destructor, it almost certainly
also needs a custom **copy constructor** and **copy assignment operator**,
because the compiler-generated defaults would do a shallow copy of that
resource (two objects pointing at the same heap allocation, both
destructors freeing it — a double free, exactly the bug RAII was supposed
to prevent).

**Rule of Five** (C++11+): add **move constructor** and **move assignment
operator** to that list. If you declare *any* of the "big five" yourself
(destructor, copy ctor, copy assign, move ctor, move assign), the compiler
stops auto-generating the ones you didn't declare — miss the move pair and
your type silently falls back to copying everywhere a move would have been
possible, quietly slower than it should be with no compile error to warn
you.

**Rule of Zero** — the actual goal to design toward, not just the third
rule in a list: **a well-designed class manages no raw resources directly
at all**. Every member is either a value type, or an RAII wrapper
(`unique_ptr`, `shared_ptr`, `std::vector`, `std::string`, `std::fstream`)
that already correctly implements the Rule of Five for the resource it
wraps. A class built entirely from such members needs **zero** custom
destructor, copy, or move operations — the compiler-generated defaults are
already correct, because each member's own defaults are already correct,
recursively.

```cpp
// Rule of Three/Five territory: manages a raw resource directly
class BadWidget {
    int* data_;   // needs custom ~, copy ctor, copy assign, move ctor, move assign
public:
    BadWidget(size_t n) : data_(new int[n]) {}
    ~BadWidget() { delete[] data_; }
    // ...and four more special members to get right by hand...
};

// Rule of Zero: delegate resource management to a member that already does it
class GoodWidget {
    std::vector<int> data_;   // vector is already Rule-of-Five-correct;
public:                        // GoodWidget needs none of the five itself
    GoodWidget(size_t n) : data_(n) {}
    // compiler-generated destructor, copies, and moves are all correct, for free
};
```

**This is the design implication the syllabus flags explicitly**: aim
every class you design in this program at Rule of Zero. If you find
yourself writing a destructor, ask first whether wrapping the resource in
an existing RAII type (or `Pimpl`, `06_cpp_design_idioms.md`, if the
resource is really "a pointer to an implementation") would eliminate the
need for one — reaching for Rule of Three/Five by hand should be rare, and
mostly reserved for the RAII wrapper types *themselves* (like the
`FileHandle` example at the top of this file) rather than for ordinary
application classes built on top of them.

## Interview angles

**"Walk me through unique_ptr vs shared_ptr vs weak_ptr."** Ownership
model first (exclusive / shared-refcounted / non-owning), cost second
(zero-overhead / atomic refcounting / atomic `.lock()`), when to reach for
each third — this ordering (model, then cost, then use case) is a stronger
answer than reciting definitions.

**"What does `std::move` actually do?"** It's a cast, not an action — say
this precisely, then name what actually happens (selecting the move
overload) as a separate step.

**"Why does this class need a custom destructor — could you avoid it?"** A
genuinely good question to ask yourself about your own code constantly in
this program: is there an RAII wrapper that makes the Rule of Three/Five
unnecessary here?

## Exercises

1. Write the `FileHandle` RAII wrapper from this file, then write a
   function that opens one, throws partway through using it, and confirm
   (via a print in the destructor, or a leak checker) that the file is
   still closed correctly despite the exception.
2. Write `BadWidget` exactly as shown (raw `int*`, no copy/move declared),
   copy an instance, let both go out of scope, and reproduce the double
   free under ASan. Then fix it two ways: hand-writing the full Rule of
   Five correctly, and rewriting it as `GoodWidget` (Rule of Zero via
   `std::vector`) instead — compare the two fixes' line counts.
3. Construct a genuine `shared_ptr` reference cycle (two classes each
   holding a `shared_ptr` to the other), confirm via a destructor print
   that neither is ever destroyed, then fix it by changing one side to
   `weak_ptr` and confirm both are destroyed correctly once external
   references go away.
4. Write a class with a move constructor that prints when it runs, put
   instances in a `std::vector` and trigger reallocation (push past
   capacity), and observe whether moves or copies actually happen — then
   remove `noexcept` from the move constructor and observe the difference
   (cross-reference `10_lld/02_systems_lld/01_raii_and_modern_oop.md`'s explanation of
   why `std::vector` only moves on reallocation when the move is
   `noexcept`).
