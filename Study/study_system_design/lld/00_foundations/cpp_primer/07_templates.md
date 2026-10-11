# 07 — Templates: writing code that works for any type

## The problem templates solve

```cpp
int maxInt(int a, int b) { return a > b ? a : b; }
double maxDouble(double a, double b) { return a > b ? a : b; }
std::string maxString(std::string a, std::string b) { return a > b ? a : b; }
```

Same logic, three times, once per type. Overloading (file `02`) helps
you *call* `max` the same way for each, but you're still writing (and
maintaining, and fixing bugs in) the same body three times.

## A function template — write it once, for any type

```cpp
template <typename T>
T maxOf(T a, T b) {
    return a > b ? a : b;
}

maxOf(3, 7);              // T becomes int
maxOf(2.5, 1.1);           // T becomes double
maxOf(std::string("a"), std::string("b"));   // T becomes std::string
```

`template <typename T>` says "this function is generic over some type
`T`, to be filled in later." When you call `maxOf(3, 7)`, the compiler
looks at the arguments, figures out `T` must be `int`, and generates (at
compile time — this costs nothing at runtime) a version of the function
with `T` replaced by `int` everywhere. Call it with `double`s, and it
generates a separate `double` version. This is why templates are called
"zero-cost abstraction" — you write it once, the compiler does the
repetitive work, and the generated code is exactly as fast as if you'd
hand-written each version yourself.

## A class template — the same idea, for a whole class

```cpp
template <typename T>
class Box {
public:
    explicit Box(T value) : value_(value) {}
    T get() const { return value_; }
private:
    T value_;
};

Box<int> intBox(42);
Box<std::string> stringBox("hello");
```

`Box<int>` and `Box<std::string>` are, to the compiler, two completely
different, independently-generated classes — the template is a blueprint,
`<int>` and `<std::string>` are what fill in the blueprint. **This is
exactly how `std::vector`, `std::unique_ptr`, and every other generic
type in this program's code actually works under the hood** — `std::vector<int>`
and `std::vector<std::string>` are the standard library's own `vector`
class template, instantiated for two different types.

## Where you'll actually see this in the tutorials

The LRU Cache problem (`01_machine_coding_problems/05_lru_cache.md`) is
templated exactly like `Box` above:

```cpp
template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}
    void put(const Key& key, Value value) { /* ... */ }
    // ...
};

LRUCache<int, std::string> cache(100);   // a cache from int keys to string values
```

Two template parameters this time (`Key` and `Value`) — same idea, just
two blanks to fill in instead of one. When you see `<Key, Value>` or
`<T>` after a class name throughout this program, that's a template being
instantiated for specific types.

## Multiple template parameters and a default

```cpp
template <typename Key, typename Value, size_t StripeCount = 16>
class StripedHashMap { /* ... */ };

StripedHashMap<std::string, int> map1;          // StripeCount defaults to 16
StripedHashMap<std::string, int, 32> map2;       // explicitly 32
```

Same idea as default function arguments (file `02`) — a template
parameter can have a default, used when the caller doesn't specify it.
You'll see this exact shape in
`00_foundations/12_lockfree_and_threadsafe_containers.md`.

## You don't need to write complex templates yet

Reading `template <typename T>` and instantiations like `Box<int>` fluently
is the actual goal of this file — you'll write simple ones (like the
exercises below), but most of this program's *design* content uses
templates you'll be filling in or calling, not inventing from scratch.
Don't feel behind if template metaprogramming (a much deeper, more
advanced corner of C++) is unfamiliar — it's genuinely not needed for
anything in this program.

## Exercises

1. Write `template <typename T> T minOf(T a, T b)`, and call it with
   `int`, `double`, and `std::string` arguments.
2. Write a class template `Pair<T>` holding two values of the same type
   `T`, with a method `swap()` that exchanges them. Instantiate it as
   `Pair<int>` and `Pair<std::string>`.
3. Take your `Box<T>` (or the one from this file) and add a second
   template parameter for a default value, mirroring the
   `StripedHashMap` example's `StripeCount` default.
