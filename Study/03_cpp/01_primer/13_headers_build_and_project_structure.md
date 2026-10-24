# 13 — Headers, `#include`, and how compiling actually works

> This file connects everything else in this primer to the actual
> `clang++` commands and the `CMakeLists.txt` project you'll use
> throughout this program — worth reading once you're comfortable with
> files `01`-`12`, right before you start the machine-coding problems for
> real.

## Why C++ projects split into `.h` and `.cpp` files

A single-file program (everything you've written so far in this primer)
works fine for small examples. A real project splits code into:

- **Header files (`.h` or `.hpp`)** — declare *what exists*: class
  definitions, function signatures. Other files `#include` these to know
  what's available, without seeing (or needing) the actual implementation.
- **Source files (`.cpp`)** — contain the actual *implementation*: the
  bodies of functions declared in the headers.

```cpp
// account.h
#pragma once
class Account {
public:
    Account(double startingBalance);
    void deposit(double amount);
    double getBalance() const;
private:
    double balance_;
};
```

```cpp
// account.cpp
#include "account.h"

Account::Account(double startingBalance) : balance_(startingBalance) {}
void Account::deposit(double amount) { balance_ += amount; }
double Account::getBalance() const { return balance_; }
```

`Account::deposit` means "the `deposit` function, defined as a member of
`Account`" — the `::` (scope resolution operator) links a function body
back to the class it belongs to, when you're defining it outside the
class body itself.

## `#pragma once`

```cpp
#pragma once   // at the very top of a header file
```

This tells the compiler "only actually process this file once, even if
it gets `#include`d multiple times (directly and indirectly) while
building one program." Without it, including the same header twice (easy
to do accidentally in a larger project) causes "this class is defined
twice" errors. You'll see `#pragma once` at the top of essentially every
header in this program's `include/` folder — it's the modern, simpler
replacement for the older `#ifndef`/`#define`/`#endif` include-guard
pattern, which you may also encounter in older code.

## How this program's tutorials are actually written

You'll notice most machine-coding problem tutorials show everything — the
class definitions *and* their implementations — in one code block, rather
than splitting into `.h`/`.cpp`. This is deliberate: it's easier to read
and copy for learning purposes. **In the actual practice project**
(`10_lld/include/reference/`, `10_lld/src/workspace/`), you'll see the real
split: `.hpp` files in `include/reference/` for the two fully-solved
problems, plain `.cpp` files (with everything inline, for simplicity) for
the workspace stubs. Both are valid; which one a real project uses is a
judgment call, covered a little more in
`10_lld/00_foundations/14_application_architecture_patterns.md`.

## What `clang++` actually does when you compile

```bash
clang++ -std=c++20 hello.cpp -o hello
```

Two stages happen, even though it looks like one command:

1. **Compilation** — turns your `.cpp` source into machine code (an
   "object file"), one `.cpp` at a time, independently. This is the stage
   where syntax errors, type errors, and missing-declaration errors get
   caught.
2. **Linking** — combines all the object files (and any libraries you're
   using) into one final executable, resolving cross-references (e.g.
   "this file calls a function defined in that file").

For a single-file program, `clang++` does both automatically in one
command. For a multi-file project (multiple `.cpp` files), you'd compile
each separately and then link them together — this is exactly what
`CMakeLists.txt` and the `cmake --build build` command do for you
automatically, without you needing to type out each step. Once this two-
stage idea makes sense, `cmake --build` stops feeling like a mysterious
black box and starts feeling like "the same two things I already
understand, just automated."

## Reading a `#include` line

```cpp
#include <vector>      // angle brackets: a standard library / system header
#include "account.h"    // quotes: a header from your own project, found relative to this file
```

Angle brackets vs. quotes is the one small syntax distinction worth
knowing: standard library and other "system" headers use `< >`; your own
project's headers use `" "`.

## Exercises

1. Split your `Rectangle` class from `04_classes_and_objects.md`'s
   exercises into `rectangle.h` (the class definition with `#pragma once`)
   and `rectangle.cpp` (the member function bodies, using `::`). Write a
   separate `main.cpp` that `#include`s `rectangle.h` and uses the class.
2. Compile your three files together by hand:
   ```bash
   clang++ -std=c++20 main.cpp rectangle.cpp -o program
   ```
   Confirm it links and runs correctly.
3. Deliberately remove `#pragma once` from your header, `#include` it
   twice from the same `.cpp` file (directly and once more explicitly),
   and observe the "redefinition" error — then restore it and confirm the
   error goes away.
4. Open `10_lld/CMakeLists.txt` and `10_lld/tests/CMakeLists.txt` in this
   project and find the line that lists the reference test `.cpp` files —
   confirm you can now read it as "here are the source files being
   compiled and linked into one program," the same two-stage idea from
   this file, just expressed in CMake's syntax instead of raw `clang++`
   flags.
