# 05 — Inheritance and polymorphism: the actual syntax

> `00_foundations/03_oop_fundamentals.md` teaches you *why* and *when* to
> use inheritance as a design tool (is-a relationships, the vtable cost,
> composition-over-inheritance). This file teaches the *syntax* first — by
> the time you reach that discussion, none of the code in it should be
> unfamiliar, even if the design reasoning is new.

## The basic syntax: `: public Base`

```cpp
class Animal {
public:
    void breathe() const { std::cout << "breathing\n"; }
};

class Dog : public Animal {   // Dog inherits from Animal
public:
    void bark() const { std::cout << "woof\n"; }
};
```

```cpp
Dog d;
d.breathe();   // inherited from Animal -- Dog gets this for free
d.bark();      // Dog's own method
```

`class Dog : public Animal` means "`Dog` is-a `Animal`" — every `Dog`
automatically has everything `public` (and `protected`, from file `04`)
that `Animal` has, plus whatever `Dog` adds itself. `Animal` here is
called the **base class** (or "parent" / "superclass"); `Dog` is the
**derived class** (or "child" / "subclass"). You'll see this exact
`: public Base` shape constantly — every `Vehicle`/`Car`, every
`Shape`/`Circle`, every pattern in `00_foundations/07`-`09` that has a
class hierarchy at all.

## Calling the base class's constructor

```cpp
class Animal {
public:
    explicit Animal(std::string name) : name_(std::move(name)) {}
    const std::string& name() const { return name_; }
private:
    std::string name_;
};

class Dog : public Animal {
public:
    Dog(std::string name, std::string breed)
        : Animal(std::move(name)), breed_(std::move(breed)) {}   // call Animal's constructor explicitly
    const std::string& breed() const { return breed_; }
private:
    std::string breed_;
};
```

`Animal(std::move(name))` in `Dog`'s initializer list calls `Animal`'s
constructor with `name` — this is the same member-initializer-list syntax
from file `04`, just naming the base class instead of a member variable.
**If you don't write this explicitly, C++ tries to call `Animal`'s
no-argument constructor automatically** — if `Animal` doesn't have one
(exactly the case here, since it only has a constructor that takes a
`std::string`), that's a compile error. This is a very common first
mistake when learning inheritance: forgetting to call the base
constructor explicitly when the base has no default constructor.

## Where `protected` (from file `04`) actually matters

```cpp
class Animal {
protected:
    int age_ = 0;   // Dog can touch this; code outside the hierarchy can't
};

class Dog : public Animal {
public:
    void haveBirthday() {
        ++age_;   // OK: age_ is protected, Dog inherits access to it
    }
};

Dog d;
// d.age_;        // still a compile error -- protected is not public
d.haveBirthday();  // OK
```

This is the concrete example file `04` promised — `protected` exists
specifically so a base class can expose something to its *own family
tree* (derived classes) without exposing it to *everyone*.

## Virtual functions and `override` — letting a derived class customize behavior

```cpp
class Shape {
public:
    virtual double area() const {   // virtual: derived classes CAN replace this
        return 0.0;
    }
};

class Circle : public Shape {
public:
    explicit Circle(double r) : r_(r) {}
    double area() const override {   // override: this REPLACES Shape's version
        return 3.14159 * r_ * r_;
    }
private:
    double r_;
};
```

```cpp
Shape* s = new Circle(2.0);
std::cout << s->area();   // prints the Circle's area (12.566...), NOT 0.0
```

This last line is the entire point of `virtual`. Without it, calling
`area()` through a `Shape*` would always run `Shape`'s own version (`0.0`),
even though the object underneath is actually a `Circle` — with `virtual`,
C++ looks at the object's *actual* type at runtime and calls the right
version. This mechanism is called **runtime polymorphism**, and it's
covered at the design/cost level (the "vtable," what it actually costs)
in `00_foundations/03_oop_fundamentals.md` — this file is just giving you
the syntax so that discussion isn't also new vocabulary.

**Always write `override` on a function meant to replace a base class's
virtual function.** It's not required by the compiler, but it should be
treated as if it were: if you misspell the function name or get the
parameter types slightly wrong, `override` turns that into a compile
error ("this doesn't actually override anything") instead of silently
creating an unrelated new function that never gets called — a very easy,
very common mistake without it.

## Pure virtual functions and abstract classes

```cpp
class Shape {
public:
    virtual double area() const = 0;   // "= 0": no implementation, MUST be overridden
};
```

`= 0` makes `area()` a **pure virtual function** — `Shape` now has no
implementation for it at all, and as a consequence, **you cannot create a
plain `Shape` object anymore**:

```cpp
Shape s;         // compile error: Shape is abstract (has a pure virtual function)
Shape* s = new Circle(2.0);   // fine -- Circle provides an actual area()
```

A class with at least one pure virtual function is called an **abstract
class** — it exists purely to define a contract ("anything claiming to be
a `Shape` must provide an `area()`"), never to be instantiated directly.
This is the actual mechanism behind every "interface" you'll see
throughout this program (`Dice`, `LogSink`, `PaymentStrategy`, dozens
more) — in C++, an "interface" is just the convention of an abstract
class with *only* pure virtual functions and no data. `00_foundations/04_uml_and_modeling.md`
and `00_foundations/06_cpp_design_idioms.md`'s NVI section build directly
on this mechanism.

## Virtual destructors — the one you must not forget

```cpp
class Animal {
public:
    virtual ~Animal() = default;   // <-- the word "virtual" here matters enormously
};
class Dog : public Animal {
public:
    ~Dog() { std::cout << "Dog destroyed\n"; }
};

Animal* a = new Dog();
delete a;   // WITHOUT virtual on ~Animal(), only ~Animal() would run --
            // Dog's own cleanup would be silently skipped, a real bug
```

**Rule to just follow, every time**: any class with at least one
`virtual` function should also have a `virtual` destructor. The full
mechanical explanation of exactly what goes wrong without it — and why —
is in `00_foundations/18_cpp_oop_interview_rapid_fire.md`'s first
question; for now, treat this as a fixed rule: if you write `virtual`
anywhere in a class, write `virtual ~ClassName() = default;` too.

## The five named shapes of inheritance

A vocabulary list worth having cold, even though you've already written
most of these without the names — every shape below is just "which
classes inherit from which," drawn as a small tree:

```
Single:         Hierarchical:      Multilevel:        Multiple:          Hybrid:
  Animal          Shape              Animal              A   B            Shape
    |            /     \               |                 \ /             /    \
  Dog          Circle Rectangle       Dog                  D          Drawable Movable
                                        |                                  \    /
                                      Puppy                                Sprite
```

- **Single** — one base, one derived. `Dog : public Animal` from this
  file's very first example.
- **Hierarchical** — one base, *several* independent derived classes.
  `Circle : public Shape` and `Rectangle : public Shape` together — the
  shape every pattern in `00_foundations/07`-`09` with more than one
  concrete implementation of an interface uses.
- **Multilevel** — a chain: `Puppy : public Dog : public Animal`. Each
  level adds its own layer; `Puppy` gets everything `Dog` has, which
  already includes everything `Animal` has. Perfectly fine and common —
  nothing in this program avoids this one.
- **Multiple** — one derived class, two or more *direct* bases
  (`class D : public B, public C`) — covered in detail just below,
  including why this program avoids it.
- **Hybrid** — any combination of the above in one hierarchy (e.g.
  hierarchical + multiple at once). Mentioned here only so the term
  isn't a surprise if an interviewer uses it; it inherits (no pun
  intended) all of multiple inheritance's problems, for the same reason.

**The practical takeaway**: single, hierarchical, and multilevel are all
completely ordinary and used constantly throughout this program's code.
Multiple and hybrid are where the diamond problem lives — read on.

## Multiple inheritance and the diamond problem — know it exists, mostly avoid it

```cpp
class A { public: int x; };
class B : public A {};
class C : public A {};
class D : public B, public C {};   // D inherits from BOTH B and C, which both inherit from A

D d;
// d.x;   // AMBIGUOUS -- compile error: is this B's copy of A::x, or C's copy?
```

This is the **diamond problem** — `D` ends up with two separate copies of
`A`'s data, and the compiler can't guess which one you mean. C++ has a
fix (`virtual` inheritance), but this whole program's convention — stated
in `00_foundations/03_oop_fundamentals.md` — is to **avoid multiple
inheritance of implementation entirely** and reserve inheritance for
single, clean is-a relationships against abstract interfaces (where this
problem doesn't arise, because there's no data to duplicate). Know the
term and recognize the shape if you see it; you're not expected to reach
for it.

**Where multiple inheritance of *interfaces* (as opposed to
implementation) is genuinely fine**: a class implementing two unrelated
abstract interfaces (`class Sprite : public Drawable, public Movable`,
both pure abstract, no data) — no diamond problem arises because there's
nothing to duplicate, only contracts to satisfy. This is the "hybrid"
shape from above used safely, and it's a fine thing to reach for.

## Exercises

1. Write `Animal` (with a `name_` member and a `speak()` virtual function
   returning a generic string) and two derived classes, `Dog` and `Cat`,
   each overriding `speak()`. Put pointers to one of each in a
   `std::vector<std::unique_ptr<Animal>>` and call `speak()` on each
   through the base pointer, in a loop, confirming each prints its own
   version.
2. Deliberately misspell an `override` (e.g. `Area()` instead of `area()`)
   and confirm the compiler catches it. Then remove `override` entirely
   from the same misspelled version and confirm it *silently compiles* as
   an unrelated new function instead — this is the exact bug `override`
   exists to prevent, made concrete.
3. Write an abstract class `Shape` with a pure virtual `area()`, try to
   instantiate a bare `Shape` directly, and read the compiler error.
4. Reproduce the virtual-destructor bug on purpose: a `Base`/`Derived`
   pair, `Derived`'s destructor printing something, deleting through a
   `Base*` with a *non-virtual* `~Base()` — confirm `Derived`'s message
   never prints. Then add `virtual` and confirm it does.
5. Write `Animal` with `protected int age_`, a `Dog : public Animal` with
   a method that modifies `age_`, and confirm from a `main()` outside both
   classes that `dog.age_` is still inaccessible directly.
6. Build a three-level chain, `Vehicle` -> `Car` -> `SportsCar`, each
   adding one new method, and confirm from `main()` that a `SportsCar`
   object can call all three levels' methods — this is multilevel
   inheritance made concrete. Then write `class Sprite : public Drawable,
   public Movable` from two pure-abstract interfaces (no data members in
   either) and confirm it compiles with no diamond-problem ambiguity,
   unlike the `A`/`B`/`C`/`D` example above.
