# 01 — Getting started: compiling, basic syntax, I/O

## Your very first C++ program

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, world\n";
    return 0;
}
```

Save this as `hello.cpp`. Compile and run it:

```bash
clang++ -std=c++20 hello.cpp -o hello
./hello
```

`clang++` is the compiler (you already have it — this is the same one
your Mac setup verified). `-std=c++20` tells it which version of the C++
language rules to use (this whole program uses C++20). `-o hello` names
the output program `hello`; `./hello` runs it.

## What's different from C, right away

- **`#include <iostream>`** instead of `<stdio.h>`. C++ has its own I/O
  library. `std::cout` is "console output" — think of it as C++'s
  replacement for `printf`.
- **`<<`** is the "stream insertion" operator — you "push" things into
  `std::cout` with it. `std::cout << "text" << 42 << "\n";` prints the
  string, then the number, then a newline, left to right.
- **`std::`** is a *namespace* — a naming prefix that groups the standard
  library's names together so they don't collide with your own. Read
  `std::cout` as "the `cout` that belongs to the `std` namespace." You'll
  see `std::` in front of almost everything from the standard library
  throughout this program: `std::vector`, `std::string`, `std::unique_ptr`.
- **`int main()`** is still your entry point, same as C.
- Everything else about basic syntax — `if`, `for`, `while`, `{}`, `;`,
  `int`/`double`/`char`/`bool` — is **identical to C**. This is genuinely
  good news: you're not learning a new language from zero, you're learning
  what C++ *adds* on top of C.

## `auto` — let the compiler figure out the type

```cpp
auto x = 42;         // x is an int, deduced from 42
auto name = std::string("Alice");   // name is a std::string
auto pi = 3.14;       // pi is a double
```

`auto` tells the compiler "figure out this variable's type from what I'm
assigning to it." It's not "untyped" or dynamic like Python — the type is
still fixed at compile time, you're just not typing it out yourself. This
matters enormously once types get long and ugly (you'll see
`std::unique_ptr<Elevator>`, `std::vector<std::pair<int,int>>::iterator` —
nobody wants to type or read that twice), so `auto` shows up constantly in
every tutorial in this program.

**Rule of thumb for when you're new to this**: use `auto` when the type is
obvious from the right-hand side (`auto count = items.size();` — obviously
some kind of integer) or when it's long and ugly. Write the type out
explicitly when it genuinely helps a reader understand what's happening
(`double price = 9.99;` is clearer than `auto price = 9.99;` to a reader
who doesn't already know `price` should be a `double`).

## Comments

```cpp
// a single-line comment
/* a
   multi-line
   comment */
```

Identical to C.

## Namespaces, briefly

```cpp
namespace lld::tic_tac_toe {
    class Board { /* ... */ };
}
```

A namespace is a named box you put your code in, so `Board` here doesn't
collide with some other `Board` class elsewhere in a big program. You'll
see every machine-coding problem's solution wrapped in its own namespace
like this (`lld::tic_tac_toe`, `lld::parking_lot`, ...) — it's purely
organizational, ignore it as "just a label" until it clicks.

Inside a `main()` or a small test file, you'll often see:
```cpp
using namespace lld::tic_tac_toe;
```
This says "let me refer to things in this namespace without typing the
full `lld::tic_tac_toe::` prefix every time." Convenient in a small file,
generally avoided in headers/library code (a detail you don't need to
worry about yet).

## Reading input, briefly

```cpp
int age;
std::cin >> age;   // reads a number typed by the user into `age`
```

`std::cin` ("console in") is the input counterpart to `std::cout`. You
won't use this much in this program — the tutorials mostly build things
that get *tested* (called from code) rather than typed into by a human —
but it's good to recognize.

## Exercises

1. Compile and run the `hello.cpp` example above exactly as shown.
2. Change it to print your name and a number, using two separate `<<`
   operators in one statement.
3. Write a variable using `auto` for an `int`, a `double`, and a
   `std::string`, then print all three with labels (e.g. `"age: " <<
   age << "\n"`).
4. Wrap your whole program's logic in a namespace called `practice`, and
   call it from `main()` using the full `practice::` prefix (don't use
   `using namespace` yet) — this is exactly the pattern you'll see in every
   machine-coding problem's solution file.
