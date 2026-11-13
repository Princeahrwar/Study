# C++: Track Plan

> **Track code:** CPP · **Weeks:** primer as needed from week 1; interview topics weeks 9–16, one a week · **Budget:** ~20 hours for the interview topics · **Prerequisites:** the primer files you need, and [`../10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`](../10_lld/00_foundations/01_raii_ownership_and_move_semantics.md)

C++ is the language of your coding rounds and of most systems teams you are targeting. This track has two parts: a **primer** that teaches the language mechanics from scratch (read only what you need), and an **interview** part that goes underneath: how objects are laid out, what a move really costs, how the STL containers work inside, and how the memory model maps to hardware.

---

## 1. Why this track matters

C++ is the default language for performance-sensitive systems code, and follow-up questions on any C++ solution go to the same places: who owns this memory, what does this copy cost, would `std::move` help, what happens if this throws, what does virtual dispatch cost. For infrastructure and low-latency roles the questions extend to concurrency and the memory model, allocation on the hot path, and cache-friendly containers.
---

## 2. What you will be able to do after this track

1. Draw the memory layout of an object with virtual functions and multiple inheritance, and explain what a virtual call costs.
2. Explain value categories, when a move happens, when the compiler elides a copy, and why `std::move` does not move anything.
3. Implement `unique_ptr`, a simplified `shared_ptr`, and `vector` with correct exception safety.
4. Explain how `vector`, `deque`, `map`, `unordered_map` and `string` work inside, including iterator invalidation rules.
5. Write correct concurrent code with mutexes, condition variables and atomics, and choose a memory order with a reason.
6. Answer the C++ rapid-fire round without hesitation.

---

## 3. Part 1: the primer (`01_primer/`)

A from-scratch introduction to C++ mechanics, written for LLD: syntax and what it means, with a tiny example for each, and no design theory. It is reference material, not a novel. Skim all of it once over a day or two so nothing is a total surprise, return to a file when a later tutorial uses something shaky, and type every example by hand, run it and change one thing. You already understand pointers, memory and control flow from C; what is new is mostly syntax for ideas you know, plus templates, exceptions, RAII and smart pointers.

| # | File | Topic | Status |
|---|---|---|---|
| 01 | [`01_getting_started_and_toolchain.md`](01_primer/01_getting_started_and_toolchain.md) | Compilers, flags, the first program | Written |
| 02 | [`02_functions_and_references.md`](01_primer/02_functions_and_references.md) | Functions, overloading, references | Written |
| 03 | [`03_pointers_and_memory.md`](01_primer/03_pointers_and_memory.md) | Pointers, the stack and the heap | Written |
| 04 | [`04_classes_and_objects.md`](01_primer/04_classes_and_objects.md) | Classes, constructors, destructors | Written |
| 05 | [`05_inheritance_and_polymorphism_syntax.md`](01_primer/05_inheritance_and_polymorphism_syntax.md) | Inheritance and virtual functions | Written |
| 06 | [`06_const_correctness.md`](01_primer/06_const_correctness.md) | `const` everywhere | Written |
| 07 | [`07_templates.md`](01_primer/07_templates.md) | Function and class templates | Written |
| 08 | [`08_stl_essentials.md`](01_primer/08_stl_essentials.md) | Containers, iterators, algorithms | Written |
| 09 | [`09_exceptions.md`](01_primer/09_exceptions.md) | Exceptions | Written |
| 10 | [`10_lambdas_and_std_function.md`](01_primer/10_lambdas_and_std_function.md) | Lambdas and `std::function` | Written |
| 11 | [`11_operator_overloading.md`](01_primer/11_operator_overloading.md) | Operator overloading | Written |
| 12 | [`12_smart_pointers_and_move_syntax.md`](01_primer/12_smart_pointers_and_move_syntax.md) | Smart pointers and move syntax | Written |
| 13 | [`13_headers_build_and_project_structure.md`](01_primer/13_headers_build_and_project_structure.md) | Headers, builds, project layout | Written |

---

## 4. Part 2: C++ for interviews (`02_interview/`)

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_object_model_and_virtual_dispatch.md` | The object model and virtual dispatch | Object layout and padding, vptr and vtables, the cost of a virtual call (indirect branch, no inlining), multiple and virtual inheritance layouts, RTTI and `dynamic_cast`, object slicing, empty base optimisation | To write | 9 |
| 02 | `02_value_categories_and_move_semantics.md` | Value categories and move semantics | lvalue, prvalue, xvalue; rvalue references; what `std::move` really is; perfect forwarding and `std::forward`; copy elision and guaranteed RVO in C++17; Rule of 0, 3 and 5; moved-from states | To write | 10 |
| 03 | `03_templates_and_generic_programming.md` | Templates and generic programming | Deduction rules, full and partial specialisation, SFINAE and `enable_if`, type traits, `constexpr` and `if constexpr`, C++20 concepts, CRTP, variadic templates | To write | 11 |
| 04 | `04_memory_ownership_and_allocators.md` | Memory, ownership and allocators | `unique_ptr` with custom deleters, `shared_ptr` control block and `make_shared`, `weak_ptr` and cycles, placement new, allocators and `std::pmr`, arenas for hot paths | To write | 12 |
| 05 | `05_stl_internals.md` | STL internals | `vector` growth and iterator invalidation, `deque` block map, `list`, `map` and `set` as red-black trees, `unordered_map` buckets, load factor and rehash, `string` small-string optimisation, choosing a container by access pattern | To write | 13 |
| 06 | `06_exceptions_and_error_handling.md` | Exceptions and error handling | How exceptions unwind, zero-cost model and its real costs, `noexcept` and why `vector` cares, the basic, strong and no-throw guarantees, copy-and-swap, `std::optional`, `std::expected` (C++23) and error codes | To write | 14 |
| 07 | `07_concurrency_and_memory_model.md` | Concurrency and the memory model | `std::thread` and `jthread`, mutexes and lock guards, condition variables and spurious wakeups, atomics, memory orders (relaxed, acquire/release, seq_cst) and what they compile to on x86 and ARM, futures and `async`, classic bugs | To write | 15 |
| 08 | `08_modern_cpp_and_question_bank.md` | Modern C++ and the question bank | What changed in C++11, 14, 17, 20 and 23 that interviews ask about; implement `unique_ptr`, `shared_ptr` and `vector` under time pressure; 50 rapid-fire questions with answers | To write | 16 |

Each topic has a companion `NN_<topic>_demo.cpp`, built with `-std=c++20 -Wall -Wextra -fsanitize=address,undefined` (and `-fsanitize=thread` for 07), ending in `N/N checks passed`.

### Related files in other tracks

| File | Read it with |
|---|---|
| [`../10_lld/00_foundations/01_raii_ownership_and_move_semantics.md`](../10_lld/00_foundations/01_raii_ownership_and_move_semantics.md) | CPP 02, 04 |
| [`../10_lld/00_foundations/02_stl_for_system_design.md`](../10_lld/00_foundations/02_stl_for_system_design.md) | CPP 05 |
| [`../10_lld/00_foundations/18_cpp_oop_interview_rapid_fire.md`](../10_lld/00_foundations/18_cpp_oop_interview_rapid_fire.md) | CPP 01, 08 |
| [`../10_lld/02_systems_lld/02_cpp_memory_model.md`](../10_lld/02_systems_lld/02_cpp_memory_model.md) | CPP 07 |
| [`../05_operating_systems/08_memory_barriers.md`](../05_operating_systems/08_memory_barriers.md) | CPP 07 |

---

## 5. Labs

| Lab | Topic | What you build |
|---|---|---|
| `labs/vtable_explorer/` | 01 | Print object sizes and inspect vtables with `-fdump-lang-class` (GCC) and godbolt; measure a virtual call against a direct call |
| `labs/my_vector/` | 02, 04, 06 | Your own `vector<T>` with the strong exception guarantee on `push_back`, tested with a type that throws on the Nth copy |
| `labs/my_shared_ptr/` | 04, 07 | A `shared_ptr` with an atomic reference count and `weak_ptr` support, tested under ThreadSanitizer |
| `labs/container_bench/` | 05 | Benchmark `vector`, `list`, `deque`, `map` and `unordered_map` for insert, lookup and iteration; explain every result with cache behaviour |

---

## 6. Representative interview questions

1. What happens in memory when you call a virtual function? Why can a virtual call not be inlined, and when can it?
2. Why should a base class destructor be virtual? What happens if it is not?
3. What does `std::move` do? What state is a moved-from `std::string` in?
4. When does the compiler elide a copy, and when is that guaranteed?
5. What is inside a `shared_ptr`? What does `make_shared` save, and what does it cost with `weak_ptr`?
6. When are `vector` iterators invalidated? Why does `push_back` sometimes copy instead of move?
7. How does `unordered_map` handle collisions? What happens on rehash?
8. Explain the strong exception guarantee and write an assignment operator that gives it.
9. What is the difference between `memory_order_acquire` and `memory_order_seq_cst`? What does each compile to on ARM?
10. Write a thread-safe bounded queue with a mutex and two condition variables. Why a `while` loop around `wait`?
11. What is CRTP and when would you use it instead of virtual functions?
12. Implement `unique_ptr<T>` with move-only semantics and a custom deleter.

---

## 7. How to study this track

1. Use the primer as a reference: when an LLD or DSA file uses a feature you are unsure about, read that primer file first.
2. For the interview topics, follow the five-step method in [`../README.md`](../README.md).
3. For every claim about cost (virtual call, `shared_ptr` copy, atomic), measure it on your machine in the lab.
4. CPP 08 is the pre-interview revision file; revisit it in weeks 24–25.

---

## 8. Resources

| Resource | Use it for |
|---|---|
| cppreference.com | The reference for every library and language rule |
| *Effective Modern C++*, Scott Meyers | Move semantics, smart pointers, lambdas, the classic interview material |
| *C++ Concurrency in Action* (2nd ed.), Anthony Williams | Threads, atomics, the memory model |
| CppCon "Back to Basics" talks (YouTube) | Clear one-hour explanations of each topic |
| *Inside the C++ Object Model*, Stanley Lippman | Object layout and vtables (old, but the mechanism has not changed) |
| godbolt.org | What the compiler generated |

---

## 9. Progress

| Part | Done | Next |
|---|---|---|
| Primer | Written, read as needed | — |
| Interview 01–08 | — | 01 object model and virtual dispatch (week 9) |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
