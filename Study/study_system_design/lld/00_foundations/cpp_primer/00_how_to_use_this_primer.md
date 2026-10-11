# C++ Primer — how to use this

## Who this is for

Everything else in this program assumes you can read and write C++
comfortably. If that's not true yet — if you're stronger in C than modern
C++, or you haven't used templates, smart pointers, lambdas, or the STL
much — **start here, before `00_foundations/01`.** This primer teaches the
actual C++ *syntax and mechanics* that every later tutorial uses without
re-explaining. The rest of the program teaches *design* (how to structure
a program); this teaches *language* (how to write the C++ that expresses
that design). You need both, and this comes first.

## Why this exists as a separate folder

`00_foundations/01` through `19` are about **design** — SOLID, patterns,
concurrency reasoning, architecture. They're written assuming you already
know what a template is, how a lambda captures a variable, why you'd use
`std::unique_ptr` instead of a raw pointer. This primer fills that gap —
purely mechanical, no design theory, just "here is the C++ syntax, here is
what it means, here is a tiny example." Once this feels comfortable, the
rest of the program will read far more smoothly.

## What's covered, in order

```
  01  Getting started & toolchain    compiling a file, basic syntax, I/O, auto
  02  Functions & references         parameters, overloading, pass-by-reference
  03  Pointers & memory              pointers, stack vs heap, new/delete, ASan
  04  Classes & objects              constructors, members, this, public/protected/
                                     private, static members, initialization order
  05  Inheritance & polymorphism     : public Base, virtual, override, pure virtual
                                     and abstract classes, virtual destructors
  06  const correctness              what const means and why it's everywhere
  07  Templates                      generic functions and classes
  08  STL essentials                 vector, string, map, iterators, range-for,
                                     optional, variant
  09  Exceptions                     try/catch/throw, writing your own exception type
  10  Lambdas & std::function        anonymous functions, captures
  11  Operator overloading           ==, <, (), <<, why they call your own code
  12  Smart pointers & move syntax   unique_ptr/shared_ptr, std::move — the mechanics
  13  Headers, build & project setup .h vs .cpp, #include, how compiling actually works
```

## How to study this, specifically

This is **reference material you'll return to**, not a novel to read
straight through and never open again. A realistic way to use it:

1. **Skim all 12 files once**, in order, over a day or two. Don't try to
   memorize anything — just get a first pass on every concept so none of
   it is a total surprise later.
2. **When a later tutorial uses something from here that feels shaky**,
   come back to the matching file and re-read it slowly, typing out its
   examples yourself.
3. **Type every example.** Not copy-paste — type it by hand, run it,
   change one thing, run it again. This is slower than reading and it's
   the only part of this that actually builds the muscle memory you're
   here to build.

## The one honest thing to say up front

You already know more than you think. If you've written C professionally
— and building WiFi platform / embedded software means you have — you
already understand pointers, memory, functions, and control flow at a
deep level most people learning C++ from scratch don't have. What's new
here is mostly **syntax for ideas you already understand** (a class is
mostly "a struct with functions attached and some rules about who can
touch what") plus a handful of genuinely new C++-only concepts (templates,
exceptions, RAII, smart pointers). That's a much smaller gap than "learn
programming" — treat it that way.
