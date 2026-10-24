# 06 — `const`: what it means and why it's everywhere

`const` shows up in roughly four different-looking places in this
program's code, and they all mean the same underlying thing: **"this
cannot be changed."** Once you can recognize all four, a huge fraction of
every tutorial's code stops looking like unfamiliar noise.

## 1. A `const` variable

```cpp
const int MAX_PLAYERS = 4;
MAX_PLAYERS = 5;   // compile error -- can't reassign a const
```

Straightforward: the value is fixed after initialization.

## 2. A `const` reference parameter — you saw this in file `02`

```cpp
void printName(const std::string& name) {
    // name can be read, but not modified -- and no copy was made to get here
}
```

This is the single most common use of `const` in this program: "give me
read-only, no-copy access to your data."

## 3. A `const` member function

```cpp
class Account {
public:
    double getBalance() const {   // <-- const here
        return balance_;
        // balance_ = 0;   // would be a compile error -- const methods can't modify members
    }
private:
    double balance_;
};
```

`const` after the parameter list means "this method promises not to
modify the object it's called on." This isn't just documentation — the
compiler checks it, and it changes what compiles:

```cpp
void printBalance(const Account& acc) {
    std::cout << acc.getBalance();   // OK: getBalance() is const, callable on a const Account
    // acc.deposit(10);              // compile error if deposit() isn't marked const
}
```

**A `const Account&` can only call `const` member functions.** This is
why you'll see `const` on almost every "read-only" method throughout this
program's code (`isAvailable() const`, `size() const`, `at() const`) — it's
what makes passing objects around as `const&` (rule #2) actually usable.

## 4. A pointer to `const` data

```cpp
const int* p = &someInt;   // p points to data that can't be modified through p
int* const q = &someInt;   // q itself can't be reassigned, but *q can be modified
```

This one's rarer in this program's own code (references cover most of the
same need more simply), but you'll see it occasionally, especially in
older-style or lower-level code. Read `const int*` right-to-left: "pointer
to const int."

## The practical habit to build

**Default to `const` wherever you can, and only drop it when you
genuinely need to modify something.** Concretely:

- Take parameters as `const T&` unless the function needs to modify the
  caller's object.
- Mark every member function `const` unless it genuinely changes the
  object's state.

This isn't pedantry — it's the compiler helping you catch a real class of
bug (accidentally modifying something you didn't mean to) before you ever
run the program, and it's a habit every tutorial's code in this program
already follows, so reading `const` fluently will make the actual design
tutorials much easier to follow.

## Exercises

1. Write a class with one `const` member function and one non-`const`
   member function. Write a free function taking `const YourClass&` and
   confirm it can call the `const` method but not the non-`const` one
   (read the compiler error).
2. Take the `Rectangle` class from the previous file's exercises and mark
   every method that doesn't modify state as `const`.
3. Declare a `const int*` and a `int* const`, and for each, try to do the
   operation it should forbid (reassign the pointer vs. modify the
   pointed-to value) — confirm which one errors in which case.
