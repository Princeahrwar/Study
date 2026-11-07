# 08 — STL essentials: the containers you'll use constantly

The STL (Standard Template Library) is a set of ready-made, templated
(file `07`) data structures and algorithms that ship with C++. You will
use these in almost every single tutorial in this program — this file is
the minimum fluency you need before that stops feeling unfamiliar.

## `std::string`

```cpp
#include <string>
std::string name = "Alice";
name += " Smith";               // concatenation
std::cout << name.size();       // length: 11
std::cout << name[0];           // 'A' -- indexing, like an array
if (name == "Alice Smith") { }  // comparison works as you'd expect
```

C++'s string type — like a `char*` in C, but it manages its own memory
(no manual `malloc`/`free`), knows its own length, and supports `+`, `==`,
and indexing directly.

## `std::vector<T>` — a dynamically-growing array

```cpp
#include <vector>
std::vector<int> nums;
nums.push_back(10);
nums.push_back(20);
nums.push_back(30);

std::cout << nums.size();     // 3
std::cout << nums[0];         // 10 -- indexing like an array
std::cout << nums.at(0);      // 10 -- same, but throws if the index is out of range

for (int n : nums) {          // range-based for loop -- "for each n in nums"
    std::cout << n << " ";
}
```

This is your default go-to container for "a list of things," used
constantly throughout this program (a `Level`'s list of `Slot`s, an
`Elevator`'s list of requests, almost everything). `push_back` adds to the
end; `size()` tells you how many elements; `[]` and `.at()` both index in
(`.at()` is safer — it throws an exception, file `09`, instead of silently
reading invalid memory, if the index is wrong).

**The range-based for loop** (`for (int n : nums)`) is how you'll see
almost every loop over a container written in this program — much less
error-prone than the classic `for (int i = 0; i < nums.size(); i++)`
index-counting loop, though that form still works too.

## `std::pair<A, B>` — two values bundled together

```cpp
#include <utility>
std::pair<std::string, int> entry = {"Alice", 30};
std::cout << entry.first << " is " << entry.second;   // "Alice is 30"
```

## `std::map<Key, Value>` and `std::unordered_map<Key, Value>`

```cpp
#include <map>
std::map<std::string, int> ages;
ages["Alice"] = 30;
ages["Bob"] = 25;

std::cout << ages["Alice"];          // 30
if (ages.find("Carol") == ages.end()) {
    std::cout << "Carol not found";  // this prints -- Carol was never added
}
```

A **key-value lookup table** — like a hash table you'd build yourself in
C, but built in. `std::map` keeps keys in sorted order (backed by a
balanced tree, `O(log n)` operations); `std::unordered_map` doesn't
guarantee any order but is faster on average (`O(1)` operations, backed by
a hash table). **Default to `std::unordered_map`** unless you specifically
need sorted iteration — this exact tradeoff is covered in depth in
`10_lld/00_foundations/02_stl_for_system_design.md`, once you're through this
primer.

`ages.find("Carol")` returns a special "iterator" (see below) pointing at
the entry if found, or `ages.end()` (a sentinel meaning "not found") if
not — this `find(...) == container.end()` pattern is how you'll check "is
this key present?" throughout this program, since indexing with `[]` on a
missing key silently *creates* it with a default value, which is usually
not what you want when checking existence.

## Structured bindings — unpacking a pair (or map entry) into named variables

```cpp
std::pair<std::string, int> entry = {"Alice", 30};
auto [name, age] = entry;         // unpacks entry.first -> name, entry.second -> age
std::cout << name << " is " << age;   // "Alice is 30" -- no .first/.second needed
```

`auto [a, b] = something;` — this exact bracket syntax — declares two (or
more) new names in one line, each bound to one piece of `something`, for
any type whose pieces C++ knows how to unpack (a `pair`, a fixed-size
`array`, or your own struct's public members in declaration order). You
already have every ingredient for this (`auto` from file `01`, references
from file `02`) — the only new thing is the `[a, b]` bracket syntax
itself. **Where you'll see this constantly**: looping over a `std::map`
without writing `.first`/`.second` at every use:

```cpp
std::map<std::string, int> ages = {{"Alice", 30}, {"Bob", 25}};
for (const auto& [name, age] : ages) {          // unpacks each pair as you iterate
    std::cout << name << " -> " << age << "\n";
}
```

This is the single most common shape you'll see structured bindings in
throughout this program — `for (auto& [key, value] : someMap)` reads far
more clearly than the equivalent `for (const auto& entry : someMap) { ...
entry.first ... entry.second ... }`, and you'll find it in several
machine-coding problems' solutions (`10_lld/01_machine_coding_problems/04_splitwise_expense_sharing.md`'s
balance-settling logic, among others) exactly this way. `auto [it,
inserted] = someMap.try_emplace(key, value);` is the other common shape —
`try_emplace` returns a `pair<iterator, bool>`, and structured bindings
name both halves at once instead of leaving you to write `result.first`/
`result.second`.

## `std::set<T>` and `std::unordered_set<T>`

Like a `map`, but with no separate value — just "is this item present or
not," with duplicates automatically ignored:

```cpp
#include <set>
std::set<int> seen;
seen.insert(5);
seen.insert(5);     // no-op -- already present
std::cout << seen.size();   // 1
```

## Iterators, briefly

```cpp
std::vector<int> nums = {1, 2, 3};
std::vector<int>::iterator it = nums.begin();   // points at the first element
std::cout << *it;                                // 1 -- dereference, like a pointer
++it;                                             // move to the next element
std::cout << *it;                                // 2
```

An **iterator** is an object that behaves like a pointer into a
container — `*it` reads the current element, `++it` advances it,
`container.begin()`/`container.end()` mark the start and one-past-the-end.
You rarely write raw iterator loops by hand in this program (the
range-based `for` loop above does it for you under the hood), but you'll
see iterators explicitly in a few important places — most notably the LRU
Cache problem, which stores `std::list<...>::iterator`s directly inside a
`std::unordered_map` as the key trick that makes it fast. When you see
`auto it = someMap.find(key);` — that's an iterator, and `it->second` (or
`it->first`) accesses the found entry's value (or key).

## `std::optional<T>` — "a value, or nothing"

```cpp
#include <optional>
std::optional<int> maybeFind(bool found) {
    if (found) return 42;
    return std::nullopt;   // explicitly "no value"
}

auto result = maybeFind(false);
if (result.has_value()) {
    std::cout << *result;   // dereference, like a pointer, to get the value
} else {
    std::cout << "nothing found";
}
```

Used constantly throughout this program for "this operation might
legitimately not produce a value" (a cache miss, a parking lot with no
free slot) — covered more in `10_lld/00_foundations/14_application_architecture_patterns.md`'s
Result pattern discussion, once you're through this primer.

## `std::variant<Types...>` — "exactly one of these specific types, at a time"

```cpp
#include <variant>
std::variant<int, std::string> value;

value = 42;                    // value now holds an int
value = std::string("hello");  // value now holds a std::string instead -- not both

if (std::holds_alternative<std::string>(value)) {
    std::cout << std::get<std::string>(value);   // "hello"
}
```

A `std::variant<int, std::string>` can hold **either** an `int` **or** a
`std::string` at any given moment — never both, and never anything else.
Think of it as a type-safe version of a C `union`: unlike a `union`, the
variant always knows which type it's currently holding, so
`std::get<std::string>(value)` when it's actually holding an `int` throws
an exception rather than silently reading garbage.

**Where this shows up directly in the tutorials**: `10_lld/00_foundations/09_design_patterns_behavioral.md`'s
Visitor pattern section uses exactly this shape —
`std::variant<Circle, Square>` to represent "a shape that's definitely
one of these two closed, known types" — paired with `std::visit`:

```cpp
struct AreaVisitor {
    double operator()(const Circle& c) const { return 3.14159 * c.r * c.r; }
    double operator()(const Square& s) const { return s.side * s.side; }
};

std::variant<Circle, Square> shape = Circle{2.0};
double area = std::visit(AreaVisitor{}, shape);   // calls the right operator() automatically
```

`std::visit` looks at which type `shape` currently holds and calls the
matching `operator()` overload on `AreaVisitor` (the functor concept from
file `11`) — no `if`/`switch` on a type tag needed anywhere. You don't
need to fully understand *why* this is a good design yet (that's
`10_lld/00_foundations/09`'s job) — just recognize the syntax when you see it.

## Exercises

1. Build a `std::vector<std::string>` of five names, print each with a
   range-based for loop, then print just the third one using `[]`.
2. Build a `std::unordered_map<std::string, int>` of three people's ages,
   look up one that exists and one that doesn't using the
   `find(...) == map.end()` pattern, and print the right message for each.
3. Write a function `std::optional<int> findFirstEven(const std::vector<int>& nums)`
   that returns the first even number, or `std::nullopt` if there isn't
   one. Test it with a vector that has an even number and one that
   doesn't.
4. Build a `std::set<int>` by inserting the same handful of numbers twice
   each, and confirm `size()` only counts each once.
5. Rewrite exercise 2's lookup loop using
   `for (const auto& [name, age] : agesMap)` instead of manual
   `.first`/`.second` access, printing every entry. Then call
   `agesMap.try_emplace("Dave", 40)` twice in a row, structured-bind the
   result each time as `auto [it, inserted] = ...`, and print `inserted`
   both times — confirm it's `true` the first call and `false` the second
   (the key already existed).
6. Build a `std::variant<int, std::string, bool>`, assign each of the
   three types to it in turn, and after each assignment use
   `std::holds_alternative<T>` to check which type it currently holds
   before printing it.
7. Reproduce the `AreaVisitor`/`std::visit` example above with a third
   shape type added (a `Triangle`, say) — add it to both the `variant<...>`
   type list and as a new `operator()` overload, and confirm the compiler
   forces you to handle it (try removing the new overload and read the
   error) — this compile-time exhaustiveness check is `std::variant`'s
   real advantage over an `if`/`switch` on a type tag.
