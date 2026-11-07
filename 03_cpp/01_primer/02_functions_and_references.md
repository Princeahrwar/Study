# 02 — Functions, parameters, and references

## Functions look like C, mostly

```cpp
int add(int a, int b) {
    return a + b;
}
```

Same as C. What's new in C++ is **how you can pass parameters**, and
**overloading** (multiple functions with the same name).

## The three ways to pass a parameter

This is one of the most important things to get comfortable with — you'll
see all three constantly.

### 1. Pass by value — the function gets a copy

```cpp
void increment(int x) {
    x = x + 1;   // only changes the local copy
}
int n = 5;
increment(n);
// n is still 5 -- increment() never touched the caller's variable
```

Fine for small things (`int`, `double`, `bool`) where copying is cheap.
Wasteful for big things (a `std::string`, a `std::vector` with a million
elements) because the whole thing gets copied.

### 2. Pass by pointer — the function gets an address (this you already know from C)

```cpp
void increment(int* x) {
    *x = *x + 1;   // *x means "the thing x points to"
}
int n = 5;
increment(&n);   // &n means "the address of n"
// n is now 6
```

### 3. Pass by reference — C++'s alternative to pointers for this exact case

```cpp
void increment(int& x) {   // & here means "reference to int", not "address of"
    x = x + 1;             // no * needed -- x IS the caller's variable, just aliased
}
int n = 5;
increment(n);   // no & needed at the call site either
// n is now 6
```

**A reference (`int&`) is an alias for an existing variable** — a second
name for the same memory, not a separate pointer variable you have to
dereference. Three practical differences from a pointer worth knowing:

- A reference **can't be null** and **can't be reassigned** to refer to
  something else after it's created — it's bound once, at creation.
  A pointer can be null and can be reassigned.
- You use it exactly like the original variable — no `*` needed to read or
  write through it.
- This is why C++ code prefers references over pointers for "just let this
  function modify my variable" — less syntax, and the "can't be null"
  guarantee is a real correctness win.

**`const` reference — the single most common parameter type in this whole
program:**

```cpp
void printName(const std::string& name) {
    std::cout << name << "\n";
    // name = "changed";   // compile error -- const means read-only
}
```

`const std::string&` means: "give me a reference to your string (no
copy!), and I promise not to modify it." This is the default way you'll
see almost every function in this program take a parameter that's
expensive to copy (strings, vectors, any class) but doesn't need to be
modified. You'll see it written as `const T&` for any type `T` — get used
to reading that shape.

## Overloading — same function name, different parameter types

```cpp
int add(int a, int b) { return a + b; }
double add(double a, double b) { return a + b; }

add(2, 3);       // calls the int version
add(2.5, 3.1);   // calls the double version
```

The compiler picks which one to call based on the argument types you pass.
This is different from C, where every function name must be unique.

## Default arguments

```cpp
void greet(std::string name, std::string greeting = "Hello") {
    std::cout << greeting << ", " << name << "\n";
}
greet("Alice");              // uses "Hello"
greet("Bob", "Hey there");   // uses "Hey there"
```

A parameter with `= value` becomes optional — callers can omit it and get
the default.

## Return by value, and why it's usually fine

```cpp
std::vector<int> makeNumbers() {
    std::vector<int> result = {1, 2, 3};
    return result;   // looks like a copy, but the compiler avoids it
}
```

You might worry this copies the whole vector out. In modern C++, the
compiler is very good at avoiding that copy (a guarantee called "copy
elision," and when it can't fully avoid it, it *moves* instead of copies —
you'll learn exactly what that means in file `12`). For now: **returning
by value is the normal, correct thing to do** in C++, unlike some older
advice you might have heard about "always return via an output
parameter." Trust it.

## Exercises

1. Write a function `void doubleIt(int& x)` that doubles its argument
   in place, and confirm it actually modifies the caller's variable.
2. Write two overloads of a function `bigger` — one for two `int`s, one
   for two `std::string`s (comparing by length) — and call both.
3. Write a function that takes a `const std::string&` parameter and try to
   modify it inside the function body — confirm you get a compile error,
   and read the error message carefully; you'll see this exact error
   again in real code and want to recognize it instantly.
4. Write a function with two default arguments and call it three ways:
   with all arguments, with only the required one, and with one default
   overridden.
