# Low-Level Design (LLD): Track Plan

> **Track code:** LLD · **Weeks:** 1–14 core, 19–22 systems LLD, 24 for cold practice · **Budget:** ~85 hours · **Prerequisites:** C++ comfortable enough to read; use the C++ primer in [`../03_cpp/`](../03_cpp/00_plan.md) when you hit something unfamiliar

Low-level design rounds ask you to turn a vague problem ("design a parking lot", "design a rate limiter") into classes, interfaces and working code in 45–60 minutes. You have never studied LLD formally, so this track starts at the beginning: object-oriented design, UML, SOLID and design patterns, then concurrency, then a large set of interview problems worked in full, and finally the systems-level LLD (lock-free structures, the memory model, hardware abstraction layers) where your background becomes an advantage.

This track is already fully written. Your work here is studying it, attempting every problem cold before reading the solution, and getting your solutions reviewed.

---

## 1. Why this track matters

Low-level design (machine-coding) rounds come in several forms: classic object-oriented design (classes, interfaces, extensibility); object-oriented design folded into coding rounds ("design an LRU cache", "design a file system"); rounds on an existing multi-file codebase where you fix, extend and optimise, sometimes with an AI assistant whose output you must verify; security-flavoured systems (a rate limiter, a NAT table, a policy engine); and systems LLD (thread-safe and lock-free structures, hardware abstraction layers, memory ownership in C++).
---

## 2. What you will be able to do after this track

1. Run any LLD round with a repeatable procedure: clarify, identify entities, define interfaces, draw a class diagram, code the core, extend live.
2. Apply SOLID and the right design pattern for a reason you can state, and recognise when a pattern is overkill.
3. Draw class, sequence and state diagrams in UML quickly and correctly.
4. Make a design thread-safe, and explain the locking strategy and its cost.
5. Solve the ten core machine-coding problems cold in 45 minutes, with working, tested C++.
6. Design API contracts and database schemas when the round is an API or schema interview.
7. Design and defend lock-free and wait-free structures with memory-ordering arguments, and hardware abstraction layers with the cost of each kind of polymorphism.

---

## 3. How the track is organised

| Part | Folder | What it is | Status |
|---|---|---|---|
| Phase 0 | [`../03_cpp/01_primer/`](../03_cpp/00_plan.md) | C++ language mechanics from scratch (optional) | Written |
| Phase A | [`00_foundations/`](00_foundations/) | 21 tutorials: C++ for design, OOP, UML, SOLID, idioms, design patterns, concurrency, architecture, testing, the machine-coding framework, API and schema interviews | Written |
| Phase B | [`01_machine_coding_problems/`](01_machine_coding_problems/00_problem_list_and_roadmap.md) | 33 interview problems, each with requirements, class diagram, full C++ and live-extension scenarios | Written |
| Phase C | `02_systems_lld/` | 6 Staff-depth notes: ownership, the memory model, thread-safe primitives, lock-free structures, HAL design, a systems problem set | Exists, rewrite due |
| Code | `include/`, `src/`, `tests/`, `CMakeLists.txt` | A buildable CMake project with reference solutions (Tic-Tac-Toe, LRU cache), 10 passing gtest cases under sanitizers, and skeletons for you to implement the other core problems | Built and verified |

Build the project first (section 8), then work through the phases in order.

---

## 4. Phase A — Foundations (weeks 1–9, and 14)

| # | File | Topic | Week |
|---|---|---|---|
| 01 | [`01_raii_ownership_and_move_semantics.md`](00_foundations/01_raii_ownership_and_move_semantics.md) | RAII, ownership and move semantics | 1 |
| 02 | [`02_stl_for_system_design.md`](00_foundations/02_stl_for_system_design.md) | The STL for system design | 1 |
| 03 | [`03_oop_fundamentals.md`](00_foundations/03_oop_fundamentals.md) | OOP fundamentals | 2 |
| 04 | [`04_uml_and_modeling.md`](00_foundations/04_uml_and_modeling.md) | UML and modelling | 2 |
| 05 | [`05_solid_principles.md`](00_foundations/05_solid_principles.md) | SOLID principles | 3 |
| 06 | [`06_cpp_design_idioms.md`](00_foundations/06_cpp_design_idioms.md) | C++ design idioms (Pimpl, CRTP, type erasure, NVI) | 3 |
| 07 | [`07_design_patterns_creational.md`](00_foundations/07_design_patterns_creational.md) | Creational patterns | 4 |
| 08 | [`08_design_patterns_structural.md`](00_foundations/08_design_patterns_structural.md) | Structural patterns | 5 |
| 09 | [`09_design_patterns_behavioral.md`](00_foundations/09_design_patterns_behavioral.md) | Behavioural patterns | 6 |
| 10 | [`10_concurrency_for_lld.md`](00_foundations/10_concurrency_for_lld.md) | Concurrency for LLD | 7 |
| 11 | [`11_threads_futures_and_concurrency_patterns.md`](00_foundations/11_threads_futures_and_concurrency_patterns.md) | Threads, futures and concurrency patterns | 7 |
| 12 | [`12_lockfree_and_threadsafe_containers.md`](00_foundations/12_lockfree_and_threadsafe_containers.md) | Lock-free and thread-safe containers | 8 |
| 13 | [`13_specialized_data_structures_for_systems.md`](00_foundations/13_specialized_data_structures_for_systems.md) | Specialised data structures for systems | 8 |
| 14 | [`14_application_architecture_patterns.md`](00_foundations/14_application_architecture_patterns.md) | Application architecture patterns | 8 |
| 15 | [`15_testing_gtest_and_gmock.md`](00_foundations/15_testing_gtest_and_gmock.md) | Testing with gtest and gmock | 9 |
| 16 | [`16_machine_coding_framework.md`](00_foundations/16_machine_coding_framework.md) | The machine-coding framework | 9 |
| 17 | [`17_more_principles_and_antipatterns.md`](00_foundations/17_more_principles_and_antipatterns.md) | More principles and anti-patterns | 9 |
| 18 | [`18_cpp_oop_interview_rapid_fire.md`](00_foundations/18_cpp_oop_interview_rapid_fire.md) | C++ and OOP rapid fire | 14 |
| 19 | [`19_ai_and_lld.md`](00_foundations/19_ai_and_lld.md) | AI and LLD (working with an AI assistant in a coding round) | 19 |
| 20 | [`20_api_design_interviews.md`](00_foundations/20_api_design_interviews.md) | API design interviews | 14 |
| 21 | [`21_schema_design_interviews.md`](00_foundations/21_schema_design_interviews.md) | Schema design interviews | 14 |

**Why the order is what it is.** 16 (the machine-coding framework) comes after every theory phase on purpose: the procedure only makes sense once you hold the whole toolbox. 17 and 18 are reference and quick-revision material to revisit, not chapters to read once. 19 is cross-cutting commentary on everything before it. 20 and 21 are the dedicated treatments of the API-design and schema-design interview formats and assume the patterns and principles they draw on.

**A warning about pattern-itis.** The most common way senior candidates lose points in LLD rounds is applying a pattern because they just learned it, not because the problem needs it. A `Singleton` for a class that is instantiated once anyway buys only a testability problem. A `Strategy` for a method that will only ever have one implementation is speculative complexity. Files 07–09 build the vocabulary; the skill in 16 and the problem set is knowing when *not* to reach for a pattern.

---

## 5. Phase B — Machine-coding problems (weeks 10–13, 22, 24)

**How to work a problem:** read only the requirements, close the file, and solve it cold on a 45-minute timer using the framework from foundations 16. Implement it in the `src/workspace/` skeleton where one exists, with tests. Only then read the worked solution and compare.

### The ten core problems, in study order

| Order | # | Problem | Workspace skeleton | Week |
|---|---|---|---|---|
| 1 | 01 | [Tic-Tac-Toe](01_machine_coding_problems/01_tic_tac_toe.md) | Reference solution in `include/reference/` | 10 |
| 2 | 06 | [Vending machine](01_machine_coding_problems/06_vending_machine.md) | `src/workspace/vending_machine.cpp` | 10 |
| 3 | 02 | [Parking lot](01_machine_coding_problems/02_parking_lot.md) | `src/workspace/parking_lot.cpp` | 10 |
| 4 | 03 | [Elevator system](01_machine_coding_problems/03_elevator_system.md) | `src/workspace/elevator_system.cpp` | 11 |
| 5 | 07 | [Movie ticket booking](01_machine_coding_problems/07_movie_ticket_booking.md) | `src/workspace/movie_ticket_booking.cpp` | 11 |
| 6 | 04 | [Splitwise](01_machine_coding_problems/04_splitwise_expense_sharing.md) | `src/workspace/splitwise.cpp` | 11 |
| 7 | 05 | [LRU cache](01_machine_coding_problems/05_lru_cache.md) | Reference solution in `include/reference/` | 12 |
| 8 | 08 | [Message queue / pub-sub](01_machine_coding_problems/08_message_queue_pubsub.md) | `src/workspace/message_queue_pubsub.cpp` | 12 |
| 9 | 09 | [Rate limiter library](01_machine_coding_problems/09_rate_limiter_library.md) | `src/workspace/rate_limiter.cpp` | 13 |
| 10 | 10 | [Task scheduler (cron)](01_machine_coding_problems/10_task_scheduler_cron.md) | `src/workspace/task_scheduler.cpp` | 13 |

### Embedded and systems differentiators (week 22)

These are where your background stands out; use them as stories in systems and embedded loops.

| # | Problem |
|---|---|
| 22 | [Traffic light controller](01_machine_coding_problems/22_traffic_light_controller.md) |
| 23 | [Pluggable sensor HAL framework](01_machine_coding_problems/23_pluggable_sensor_hal_framework.md) |
| 24 | [OTA update manager](01_machine_coding_problems/24_ota_update_manager.md) |

### Extended problems (six of them cold in week 24)

Highest value first: 11 library management, 17 bounded blocking queue, 20 ATM, 19 in-memory KV store with TTL, 21 airline seat reservation, 30 payment gateway. The rest (12 logger, 13 notification system, 14 chess, 15 snake and ladder, 16 food delivery, 18 distributed ID generator, 25 URL shortener, 26 ride sharing, 27 in-memory file system, 28 document editor, 29 music player, 31 coupon engine, 32 chat room, 33 dating app) are for extra practice or a company that is known to ask them. The full roadmap is in [`00_problem_list_and_roadmap.md`](01_machine_coding_problems/00_problem_list_and_roadmap.md).

---

## 6. Phase C — Systems LLD (weeks 20–22)

These six notes are dense and assume Phases A and B. Each is rewritten in the detailed style before you study it. Note that `06_lld_interview_problems.md` predates the rest of the track: four of its six problems are superseded by longer versions in Phase B, so skim it and read its Connection Pool section, the one genuinely new thing there.

| # | File | Topic | Week |
|---|---|---|---|
| 01 | [`01_raii_and_modern_oop.md`](02_systems_lld/01_raii_and_modern_oop.md) | RAII, ownership and modern C++ design | 20 |
| 02 | [`02_cpp_memory_model.md`](02_systems_lld/02_cpp_memory_model.md) | The C++ memory model in practice | 20 |
| 03 | [`03_thread_safe_primitives.md`](02_systems_lld/03_thread_safe_primitives.md) | Thread-safe primitives: SPSC ring, seqlock, thread pool, timer wheel | 21 |
| 04 | [`04_lockfree_data_structures.md`](02_systems_lld/04_lockfree_data_structures.md) | Lock-free structures, ABA, hazard pointers, epoch reclamation | 21 |
| 05 | [`05_hal_architecture.md`](02_systems_lld/05_hal_architecture.md) | Hardware abstraction layers | 22 |
| 06 | [`06_lld_interview_problems.md`](02_systems_lld/06_lld_interview_problems.md) | The systems LLD problem set | 22 |

Diagrams: [`02_systems_lld/diagrams/lld_flow_table_classes.mmd`](02_systems_lld/diagrams/lld_flow_table_classes.mmd), [`02_systems_lld/diagrams/epoch_reclamation.mmd`](02_systems_lld/diagrams/epoch_reclamation.mmd). Phase C feeds Project 03, [`pfilt`](../14_projects/03_pfilt_packet_classifier/SPEC.md).

---

## 7. Representative interview questions

1. Design a parking lot that supports several vehicle sizes and pricing strategies. Now add reservations.
2. Design an elevator system. How do you change the scheduling algorithm without touching the elevator class?
3. Design a rate limiter library supporting token bucket and sliding window. Make it thread-safe. Make it work across 64 cores without a global lock.
4. Design an LRU cache with O(1) operations. Add TTL. Make it concurrent.
5. Design a pub/sub message queue with at-least-once delivery and consumer groups.
6. Which design pattern would you use for undo/redo in a document editor, and why?
7. Explain the Open/Closed principle with an example where following it blindly makes the code worse.
8. Design a NAT table for a firewall: the data structures, the concurrency, the timeouts.
9. Design a sensor HAL that supports new sensor types without recompiling the core.
10. Design the API for a file-upload service: endpoints, errors, idempotency, pagination.

---

## 8. Build the project, and how to work a problem

**Setup (once).** The Mac toolchain (Xcode command-line tools, Homebrew, `cmake`, `ninja`, `googletest`) is covered in [`../99_toolchain/README.md`](../99_toolchain/README.md). Then, from this folder:

```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

What is in the project:

- `include/reference/`: Tic-Tac-Toe and the LRU cache, fully implemented, as examples of a finished solution.
- `tests/reference/`: 10 gtest cases for them. ASan and UBSan are on by default, so a memory bug or undefined behaviour fails the build, not just the test.
- `src/workspace/`: a compiling skeleton for each of the other eight core problems. Every method throws `std::logic_error("TODO")`; replacing the throws is the exercise.
- `tests/workspace/`: empty until you fill in a skeleton.

**Working a problem.**

1. Read the problem's tutorial up to "Step 3: Class diagram", then stop.
2. Implement it cold and timed (45 minutes, using the framework in foundations 16). For the eight problems with a skeleton, work in `src/workspace/<problem>.cpp`; for the others, in a scratch file: `clang++ -std=c++20 -fsanitize=address,undefined -g -o solution solution.cpp && ./solution`.
3. Write a gtest file `tests/workspace/<problem>_test.cpp` (create the folder; `#include "workspace/<problem>.cpp"` directly, or split your skeleton into a header and a source file first), uncomment and adapt the example block at the bottom of `tests/CMakeLists.txt`, and assert the cases from the tutorial's "Step 6: Edge cases". Run the commands above.
4. Only now read the tutorial's Step 4–5 code and diff yours against it. Write down where you diverged and why; the diff is the lesson.

| Skeleton | Tutorial |
|---|---|
| `parking_lot.cpp` | `01_machine_coding_problems/02_parking_lot.md` |
| `elevator_system.cpp` | `01_machine_coding_problems/03_elevator_system.md` |
| `splitwise.cpp` | `01_machine_coding_problems/04_splitwise_expense_sharing.md` |
| `vending_machine.cpp` | `01_machine_coding_problems/06_vending_machine.md` |
| `movie_ticket_booking.cpp` | `01_machine_coding_problems/07_movie_ticket_booking.md` |
| `message_queue_pubsub.cpp` | `01_machine_coding_problems/08_message_queue_pubsub.md` |
| `rate_limiter.cpp` | `01_machine_coding_problems/09_rate_limiter_library.md` |
| `task_scheduler.cpp` | `01_machine_coding_problems/10_task_scheduler_cron.md` |

**What a machine-coding round tests**, in order of importance: turning a fuzzy requirement into classes, responsibilities and relationships; using the vocabulary (SOLID, patterns, composition over inheritance) as compressed arguments for a decision; writing working code in real time; and extending it live when the requirements change. At Senior level expect a clean working design, one live change, and a concurrency or scale discussion inside 45–60 minutes.

**Three habits.** Redraw each UML diagram from memory after reading. Implement without looking, then diff. Say the trade-off out loud: "unbounded conditionals in the pricing method" is a better reason for a Strategy than "it is a best practice". Draw every class diagram by hand before coding; interviewers judge the diagram before the code. Ask for a code review of each solution in the LLD chat.

**Diagrams and paper copies.** Mermaid renders in VS Code, GitHub and Claude's file viewer; `mmdc -i file.md -o diagram.svg` exports one. `pandoc file.md -o file.docx` makes a Word copy.

---

## 9. Resources

| Resource | Use it for |
|---|---|
| refactoring.guru (design patterns and refactoring) | A second explanation of each pattern, with diagrams |
| *Head First Design Patterns* (2nd ed.) | Patterns explained with intuition first |
| *Design Patterns*, Gamma, Helm, Johnson & Vlissides | The original catalogue |
| *Clean Architecture*, Robert C. Martin | SOLID and architecture boundaries |
| *C++ Concurrency in Action* (2nd ed.), Anthony Williams | Concurrency for Phases A and C |

---

## 10. Progress

| Part | Done | Next |
|---|---|---|
| Phase A (01–21) | — | 01, 02 (week 1) |
| Phase B (core 10) | — | Tic-Tac-Toe (week 10) |
| Phase C (01–06) | — | Rewrite before week 20 |

