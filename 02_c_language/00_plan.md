# C Language: Track Plan

> **Track code:** C · **Weeks:** 1–10, one topic a week, with the question bank revisited in week 24 · **Budget:** ~26 hours · **Prerequisites:** none beyond writing C day to day

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
7. Read any declaration, name every class of pointer bug from its symptom, and write callback, plugin and state-machine code with function pointers.
8. Design C modules the way the Linux kernel does: opaque handles, tagged unions, inheritance by embedding, `container_of`, intrusive lists and vtables of function pointers.
9. Use the less common parts of the language correctly: variadic functions, `setjmp`/`longjmp`, `goto cleanup`, `_Generic`, C11 atomics and threads, and what C23 adds.

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
| 08 | `08_advanced_pointers_and_pointer_bugs.md` | Advanced pointers and pointer bugs | Reading any declaration (the right-left rule, `cdecl`, 20 drills including functions that return pointers to functions and arrays of them); `*p++`, `(*p)++`, `*++p` and `++*p`; the pointer bug catalogue, one compiled program each: wild, null, dangling (stack, heap, after `realloc`), use-after-free, double free, leaks, freeing a pointer that did not come from `malloc`, `free` in a callee and the `T **` fix, `malloc(0)`, aliasing through two pointers, misaligned access, segmentation fault against bus error, stack overflow from deep recursion and large VLAs; `restrict` in practice; pointers as integers (`uintptr_t`, tagged pointers using the alignment bits); function pointers in depth: returning and storing them, callbacks with a `void *` context (`qsort_r` style), jump tables, table-driven state machines, plugins loaded with `dlopen` and `dlsym` | To write | 8 |
| 09 | `09_struct_design_and_oop_in_c.md` | Struct design and object-oriented C | Struct semantics (assignment, passing and returning by value, why `==` does not exist and `memcmp` is wrong because of padding), designated initialisers and compound literals, anonymous structs and unions (C11), opaque types and handles (information hiding behind a header), tagged unions as variants, inheritance by embedding and `container_of`, intrusive lists as in the Linux kernel (`list_head`), vtables of function pointers and polymorphism (how `file_operations` works), constructors, destructors and ownership rules, arrays of structs against structs of arrays (measured) | To write | 9 |
| 10 | `10_advanced_c_features_and_error_handling.md` | Advanced C features and error handling | Variadic functions (`stdarg.h`, `va_copy`, format checking with `__attribute__((format))`), `setjmp` and `longjmp` and why modified locals need `volatile`, the `goto cleanup` idiom, `errno` and error-return conventions, `_Generic` for type-generic macros, `_Static_assert`, `_Alignas` and `_Alignof`, variable-length arrays and their risks, `<stdatomic.h>` and `<threads.h>` (the bridge to OS 06 and 07), async-signal-safe code, what C23 adds (`nullptr`, `typeof`, `constexpr`, `#embed`, attributes) | To write | 10 |
| 11 | `11_c_interview_question_bank.md` | C interview question bank | 40+ "what does this print" and "what is wrong here" questions with explanations; implement a ring buffer, a linked list and a fixed-block allocator; embedded C (ISRs, `volatile`, MMIO, watchdogs) | To write | 10, revisited 24 |

Every tutorial with code has a companion `NN_<topic>_demo.c`, built with `-std=c11 -Wall -Wextra -fsanitize=address,undefined` and ending in `N/N checks passed`.

---

### Where related topics live

Some C-adjacent subjects belong to other tracks, because the interview questions about them are about the operating system or the network, not the language:

| Subject | Where |
|---|---|
| Local IPC: pipes, FIFOs, signals, message queues, shared memory (`shm_open`, `mmap`), semaphores, Unix domain sockets | [OS 13 and OS 14](../05_operating_systems/00_plan.md) |
| `mmap`, copy-on-write, page faults | [OS 09 and OS 10](../05_operating_systems/00_plan.md) |
| Threads, mutexes, condition variables, atomics, memory barriers | [OS 06 and OS 07](../05_operating_systems/00_plan.md); C 10 covers the C11 interfaces |
| Socket programming in C, `select`, `poll`, `epoll`, `io_uring` | [NET 04 and NET 05](../07_networking/00_plan.md) |
| Signals from the shell, `/proc`, debugging with `gdb`, `strace` and sanitizers | [LX 02 and LX 04](../06_linux/00_plan.md) |
| Kernel C: `list_head`, `container_of`, `kmalloc`, locking in the kernel | [Kernel track](../08_kernel/00_plan.md); C 09 introduces the idioms |
| MMIO, `volatile` registers, ISRs | C 05 and C 11 here, then [Embedded](../13_embedded/00_plan.md) and [Drivers](../09_drivers/00_plan.md) |

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
13. Read `void (*(*f[3])(int))(void)` aloud. Write a `typedef` that makes it readable.
14. A function frees a pointer it was given, and the caller crashes later. Why, and how do you change the signature?
15. How does `container_of` work, and why does the Linux kernel embed list nodes inside structures instead of the reverse?
16. How do you write object-oriented code (encapsulation, inheritance, polymorphism) in C?
17. Write a variadic `log_printf` that the compiler checks like `printf`. What does `va_copy` fix?
18. When is `setjmp`/`longjmp` the right tool, and what goes wrong with local variables?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. For every "what does this print" question, predict first, then compile at `-O0` and `-O2` and compare. Disagreements between the two are where UB lives.
3. Keep godbolt.org open: when a tutorial says the compiler does something, look at the assembly.
4. Topic 11 is a question bank; revisit it in week 24 before interviews.

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
| 03–11 | Remaining topics | To write |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
