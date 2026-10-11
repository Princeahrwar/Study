# Study — Senior/Staff Systems Interview Program

Goal: move from Senior SWE (Ruckus, Wi-Fi platform, C/C++, embedded Linux) to
a Senior/Staff Systems offer at Google, Nvidia, Meta, Microsoft, Cisco, or
Palo Alto Networks. Everything lives under `study_system_design/` — this
file is the entry point and the honest status board for what's built versus
what's still planned.

## Start here

Read `study_system_design/00_roadmap/HOW_TO_USE.md` first — it explains how
the whole program fits together and how to actually retain the material
(three rules: exercises before moving on, write summaries from memory, say
answers out loud on a timer). Then `study_system_design/00_roadmap/CURRICULUM.md`
for the full dependency-ordered topic list.

For the LLD track specifically — the reading order and the buildable coding
environment (Mac toolchain fully installed and verified: cmake, googletest,
sanitizers, mermaid-cli, pandoc/LibreOffice) — go to
`study_system_design/lld/GETTING_STARTED.md`.

## Program structure

```
  study_system_design/
    00_roadmap/                the curriculum, capstone specs, this index's source
    lld/00_foundations/        22 files (00-21), numbered to match the
                                user's 9-phase syllabus exactly: C++
                                mechanics -> OOAD/UML -> SOLID/idioms ->
                                patterns -> concurrency -> data structures ->
                                architecture -> testing/framework/AI [DONE]
    lld/01_machine_coding_problems/  worked interview problems [36 of 36 written (37 with optional Chess) — COMPLETE]
    lld/notes/                 LLD Phase C — systems LLD, modern C++, concurrency [DONE — Staff level]
    lld/include,src,tests/     buildable CMake project, verified: 10/10 tests
                                pass under ASan+UBSan, 8 workspace stubs
                                compile clean out of the box [DONE]
    lld/docs/                  all 70 LLD files as .docx, diagrams embedded
                                as rendered images [DONE]
    os_and_arch/                CPU/cache/memory architecture                 [DONE — Staff level]
    kernel_drivers/             Linux kernel modules, interrupts, DMA         [DONE — Staff level]
    hld/                        distributed systems: caching, storage, telemetry [DONE — Staff level; classic HLD fundamentals still planned, see below]
    embedded_ai/                RTOS constraints, edge AI acceleration        [DONE — advanced level; ground-up embedded fundamentals still planned]
    interviews/                 company playbooks, mock bank, behavioral prep [DONE]
    capstones/                  3 portfolio projects tying it together        [specs written, implementation pending]
```

## What's genuinely new-to-this-program vs already existed

This program started from a solid but *advanced-only* base — strong on CPU
architecture, kernel internals, lock-free C++, and distributed-systems
topics, but written for someone who already has that foundation. It didn't
cover classic OOD/design patterns, classic HLD fundamentals (CAP theorem,
load balancers, DNS/CDN, DB scaling basics), true embedded-systems-from-zero
(microcontrollers, GPIO, RTOS concepts), or AI's role in any of this. Status:

- **LLD — COMPLETE.** (`lld/00_foundations/`, `lld/01_machine_coding_problems/`,
  `lld/notes/`, `lld/include,src,tests/`, `lld/docs/`). Built and reviewed
  five times: once against external LLD roadmaps/checklists, once against a
  full 9-phase modern-C++-LLD syllabus supplied directly, once to physically
  reorder and renumber every file in `00_foundations/` (`00`-`19`) to match
  that syllabus's exact phase sequence, once to finish every remaining gap
  in the machine-coding roadmap, and once to add a full C++ language primer
  (`00_foundations/cpp_primer/`, 14 files — inheritance/polymorphism, templates, smart pointers,
  lambdas, STL, exceptions, headers/build — from scratch) once it became
  clear the intended reader is stronger in C than in modern C++ specifically.
  Foundations: Phase 1 Modern C++ mechanics (RAII/ownership/move semantics,
  STL), Phase 2 OOAD & UML,
  Phase 3 SOLID & C++ idioms (Pimpl/CRTP/Type Erasure/NVI), Phase 4 all 12
  GoF patterns (with modern alternatives — `std::function` Strategy,
  `weak_ptr` Observer, `std::variant`/`std::visit` Visitor), Phase 5
  concurrency (mutex/atomic/condition_variable through threads/futures/
  Dining Philosophers through CAS/memory-order fundamentals and a
  striped-lock thread-safe HashMap), Phase 6 specialized data structures
  (Trie, interval structures, graph+topological sort, LFU), Phase 7
  application architecture (layered design, event bus, exception strategy,
  Result pattern), plus cross-cutting testing (gtest/gmock/TSan), the
  machine-coding framework (now naming all 5 distinct LLD interview formats —
  OOD/whiteboard, Machine Coding, Concurrency Design, API Design, Schema
  Design — not just the machine-coding one), a DRY/KISS/YAGNI/Law-of-Demeter
  and anti-pattern catalog, a C++ OOP rapid-fire Q&A, AI scope, and
  dedicated API Design and Schema Design interview tutorials
  (signatures/contracts/naming with no implementation for the former;
  ER modeling, cardinality, normalization to 3NF, denormalization and
  indexing, worked against the Splitwise/Library problems, for the
  latter — the 4th and 5th of the 5 named LLD interview formats, both now
  written in full). The `cpp_primer/` C++ syntax primer was also reviewed
  a second time and closed six real gaps found by grepping the whole track
  for concepts that were *used* in code constantly but never actually
  *taught*: the `explicit` keyword, the four cast operators
  (`static_cast`/`dynamic_cast`/`const_cast`/`reinterpret_cast`), `friend`
  classes, `enum class`, the subscript (`operator[]`) and copy-assignment
  (`operator=`, with the self-assignment-check idiom) operators, and the
  five named shapes of inheritance (single/hierarchical/multilevel/
  multiple/hybrid). **All 36 roadmap problems
  written** (37 with optional Chess), spanning Tier 1 through Tier 6
  (embedded/HAL-flavoured differentiators in Tier 4 — Traffic Light
  Controller, Pluggable Sensor/HAL Framework, OTA Update Manager; Tier 5
  added after cross-checking against public LLD curricula — URL Shortener,
  Ride-Sharing, In-Memory File System; Tier 6 added after cross-checking
  against a popular YouTube LLD course — Document Editor, Music Player,
  Payment Gateway, Discount Coupon Engine, Chat Room, Dating App,
  including this program's only dedicated Chain-of-Responsibility and
  Mediator implementations). A verified,
  buildable CMake practice project and 76 `.docx` conversions with embedded
  diagrams round it out. Start here if design patterns aren't yet reflexive.
- **HLD fundamentals** (CAP theorem, consistency models, load balancing,
  DNS/CDN, database replication/sharding basics, message queues, API
  design) — **planned, not yet written.** The existing `hld/notes/` assumes
  this layer and starts from estimation/rate-limiters/caching internals.
- **Embedded systems from zero** (MCU vs MPU, memory maps, GPIO/ADC/DAC,
  UART/SPI/I2C, bare-metal interrupts, RTOS task/scheduling/queue/semaphore
  concepts) — **planned, not yet written.** The existing `embedded_ai/` and
  `kernel_drivers/` assume embedded Linux and kernel-driver experience
  already in place.
- **AI usage and scope**, applied to each domain — **started.**
  `lld/00_foundations/19_ai_and_lld.md` covers it for LLD specifically. A
  program-wide `ai_scope/` view (how AI changes HLD design work, embedded
  firmware development, and what each target company is asking about AI
  competency in loops) is planned next.

## Next up

LLD is done. In priority order for what's next (by explicit agreement —
HLD deferred until the user starts studying LLD):

1. Classic HLD fundamentals track, positioned before the existing `hld/notes/`.
2. Embedded systems from zero, positioned before `kernel_drivers/` and
   `embedded_ai/`.
3. Program-wide AI-scope guide.
4. `.docx` conversion for HLD/embedded tracks once they're written
   (mechanism already built and proven for LLD — `lld/docs/`).

## How to navigate day to day

Everything is plain Markdown with inline Mermaid diagrams — viewable as-is
in any Markdown-aware editor (VS Code, GitHub, Claude Code) with diagrams
rendering natively. No build step required for reading. `.mmd` diagram
source files that predate this reorganization (in `hld/diagrams/`,
`lld/notes/diagrams/`, `kernel_drivers/notes/diagrams/`) still need
`mmdc` to render to an image — see
`study_system_design/00_roadmap/INDEX.md` for the command.
