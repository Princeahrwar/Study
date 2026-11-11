# C Language: Track Plan

> **Track code:** C · **Weeks:** 1–8, one topic a week · **Budget:** ~18 hours · **Prerequisites:** none beyond writing C day to day

You write C every day. This track is about the layer underneath daily use: what the standard actually promises, where undefined behaviour hides, how memory, alignment and linking really work, and how to answer the C questions that platform, driver and data-plane interviews ask.

---

## 1. Why this track matters

C is the language of kernels, drivers, firmware, network data planes and language runtimes, and fluency in it is what makes OS, kernel and performance answers credible even in C++ shops. Interviewers probe it with "what does this code print": pointer semantics, `volatile`, alignment and padding, memory-mapped registers, structs laid over packet headers, endianness, bit-fields, ring buffers, and sometimes writing a small socket server in C.
---

## 2. What you will be able to do after this track

1. Spot undefined behaviour in a code snippet and explain what the compiler is allowed to do with it.
2. Predict the size and layout of any struct on x86-64 and ARM64, and control it.
3. Explain how `malloc` and `free` work inside, and implement a simple allocator.
4. Trace a program from `.c` to a running process: preprocessing, compiling, assembling, linking, loading.
5. Write `memcpy`, `memmove`, `atoi`, `strtok` and a lock-free SPSC ring buffer in C, correctly, under interview time.
6. Answer the standard "what does this print" and embedded-C questions with the reason, not a guess.

---

## 3. Topics

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_types_conversions_and_undefined_behavior.md` | Types, conversions and undefined behaviour | Integer types and their guaranteed ranges, two's complement, integer promotion and the usual arithmetic conversions, signed overflow vs unsigned wraparound, the UB catalogue (overflow, out-of-bounds, use-after-free, unsequenced modification, strict aliasing), what optimisers do with UB | Written | 1 |
| 02 | [`02_pointers_and_arrays.md`](02_pointers_and_arrays.md) | Pointers and arrays in depth | Array-to-pointer decay and its exceptions (`a` against `&a`, with `_Generic` type printouts), pointer arithmetic and the one-past-the-end rule, half-open pointer ranges (rotate by three reversals, `lower_bound`), `T[]` against `T*` in parameters, `char *p` against `char a[]` (write to a literal: `SIGSEGV`), multi-dimensional arrays and three heap layouts (column-wise access 3 to 13 times slower, measured), `Node **` list removal without a head case, reading declarations and function-pointer dispatch tables, a `qsort` comparator that overflows (UBSan), `void *` generic code (byte-wise swap 5 to 6 times slower), dangling pointers and out-of-bounds access under ASan | Written | 2 |
| 03 | `03_memory_stack_heap_and_malloc.md` | Memory: stack, heap and `malloc` | Storage durations, stack frames, how `malloc` and `free` work (free lists, bins, `brk` and `mmap`), fragmentation, leaks, double frees and corruption, AddressSanitizer and Valgrind; lab: write a small allocator | To write | 3 |
| 04 | `04_structs_unions_bitfields_and_alignment.md` | Structs, unions, bit-fields and alignment | Padding and alignment rules, `offsetof`, packing and its cost on ARM, endianness and network byte order, bit-fields and why drivers avoid them, unions and type punning, flexible array members | To write | 4 |
| 05 | `05_qualifiers_storage_classes_and_linkage.md` | Qualifiers, storage classes and linkage | `const` (and `const` pointers), `volatile` (what it does and does not guarantee), `restrict`, `static` at file and function scope, `extern`, internal and external linkage, `inline` in C99 | To write | 5 |
| 06 | `06_preprocessor_compilation_and_linking.md` | Preprocessor, compilation and linking | Macro pitfalls and `do { } while (0)`, X-macros, include guards, the build pipeline, object files and ELF sections, symbol resolution, static vs dynamic libraries, PIC, the loader; weak symbols, `LD_PRELOAD` interposition, `dlopen`, symbol visibility, thread-local storage and how it is addressed | To write | 6 |
| 07 | `07_strings_stdlib_and_bit_manipulation.md` | Strings, the standard library and bit manipulation | `strncpy` and `strncat` traps, `snprintf`, `memcpy` vs `memmove`, writing `memcpy`, `memmove`, `atoi`, `strtok`, `itoa`; bit idioms (set, clear, toggle, test, count, reverse), register-style bit masks | To write | 7 |
| 08 | `08_c_interview_question_bank.md` | C interview question bank | 40+ "what does this print" and "what is wrong here" questions with explanations; implement a ring buffer, a linked list and a fixed-block allocator; embedded C (ISRs, `volatile`, MMIO, watchdogs) | To write | 8 |

Every tutorial with code has a companion `NN_<topic>_demo.c`, built with `-std=c11 -Wall -Wextra -fsanitize=address,undefined` and ending in `N/N checks passed`.

---

## 4. Labs

| Lab | Topic | What you build or observe |
|---|---|---|
| `labs/ub_explorer/` | 01 | Small UB programs compiled at `-O0` and `-O2`; compare the assembly on godbolt.org and see the optimiser delete your overflow check |
| `labs/allocator/` | 03 | A first-fit allocator over a static arena with block splitting and coalescing, tested with random alloc/free sequences |
| `labs/struct_layout/` | 04 | Print `sizeof` and `offsetof` for a set of structs on your Mac (ARM64) and in a Linux VM (x86-64 via QEMU), and explain every byte |
| `labs/link_lab/` | 06 | Build a static and a shared library, inspect them with `nm`, `objdump` and `readelf`, break symbol resolution on purpose |

ELF tools need Linux; the Mac produces Mach-O binaries. `99_toolchain/README.md` sets up a Linux VM for these labs.

---

## 5. Representative interview questions

1. What is the difference between `char *p = "abc";` and `char a[] = "abc";`? What happens if you write `p[0] = 'x'`?
2. Why is `i = i++ + 1;` undefined, and what does "unsequenced" mean?
3. What is `sizeof(struct { char c; int i; char d; })` and how would you make it smaller?
4. What does `volatile` guarantee? Is it enough to share a flag between two threads? Between an ISR and the main loop?
5. Explain `memcpy` vs `memmove`, then write `memmove`.
6. A function returns a pointer to a local array. Why does it sometimes "work"?
7. How does `free` know how many bytes to release?
8. What is the difference between `static` on a global variable and on a local variable?
9. Write a macro that swaps two values. What breaks it? Why is `do { } while (0)` used?
10. How do you check endianness at run time? Convert a 32-bit value from host to network order without `htonl`.
11. What happens between `gcc main.c` and `./a.out` printing output?
12. Implement a lock-free single-producer, single-consumer ring buffer. Which barriers does it need on ARM?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. For every "what does this print" question, predict first, then compile at `-O0` and `-O2` and compare. Disagreements between the two are where UB lives.
3. Keep godbolt.org open: when a tutorial says the compiler does something, look at the assembly.
4. Topic 08 is a question bank; revisit it in week 24 before interviews.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| *Modern C*, Jens Gustedt (free online) | A modern, standard-based treatment of C |
| *The C Programming Language*, Kernighan & Ritchie | The classic, short and precise |
| *Expert C Programming*, Peter van der Linden | Declarations, arrays vs pointers, linking stories |
| cppreference.com C section | The standard, readable |
| SEI CERT C Coding Standard | Undefined behaviour and secure coding rules |
| godbolt.org | Seeing what the compiler did |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01 | Types, conversions and undefined behaviour | Written |
| 02 | Pointers and arrays | Written |
| 03–08 | Remaining topics | To write |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
