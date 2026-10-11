# Curriculum matrix

Seven focus areas, ordered by dependency. No fixed calendar — depth beats
speed. The "phase" column tells you what you can safely study in parallel.

## Phase 0 — LLD foundations and machine coding (no prerequisites)

Independent of every other phase — start here if you want the fastest path
to interview-ready LLD, or interleave it with Phase 1 if you'd rather build
the hardware model in parallel. Lives in `lld/00_foundations/` and
`lld/01_machine_coding_problems/`.

Internally organised into the same nine sub-phases as a dedicated modern-
C++-LLD curriculum, all inside `lld/00_foundations/` — numbering below is
the actual file order, always read sequentially:

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 00 | How to study this track | Why the phases are ordered this way |
| **Sub-phase 1 — Modern C++ core mechanics (beginner)** | | |
| 01 | RAII, ownership & move semantics | unique/shared/weak_ptr, std::move, Rule of Zero/Three/Five |
| 02 | STL for system design | Containers by complexity/iterator-stability, `<algorithm>`, `std::function` |
| **Sub-phase 2 — OOAD and UML (beginner)** | | |
| 03 | OOP fundamentals | The four pillars in C++, with vtable mechanics |
| 04 | UML and modelling | Class, sequence, use case and state diagram notation |
| **Sub-phase 3 — Structural principles and C++ idioms (intermediate)** | | |
| 05 | SOLID principles | Five rules, each with a violation and a fix |
| 06 | C++ design idioms | Pimpl, CRTP, Type Erasure, NVI |
| **Sub-phase 4 — Design patterns in modern C++ (intermediate)** | | |
| 07 | Creational patterns | Singleton (Meyers'), Factory Method, Abstract Factory, Builder, Prototype, Object Pool |
| 08 | Structural patterns | Adapter, Bridge, Composite, Decorator, Facade, Flyweight, Proxy |
| 09 | Behavioural patterns | Strategy, Observer, Command, State, Template Method, Chain of Responsibility, Iterator, Mediator, Memento, Visitor, Interpreter |
| **Sub-phase 5 — Concurrency, threading and resiliency (advanced)** | | |
| 10 | Concurrency for LLD | mutex/atomic/condition_variable, the classic `transfer()` deadlock |
| 11 | Threads & futures | `std::thread`/`jthread`, `async`/`future`/`promise`, Dining Philosophers |
| 12 | Lock-free & thread-safe containers | CAS/memory-order fundamentals, thread-safe Stack, striped-lock HashMap |
| **Sub-phase 6 — Specialized data structures & algorithms (advanced)** | | |
| 13 | Specialized data structures | Trie, interval structures, graph + topological sort, LFU |
| **Sub-phase 7 — Application architecture & framework design (advanced)** | | |
| 14 | Application architecture | Controller/Service/Repository, an in-memory event bus, exception strategy, Result pattern |
| **Cross-cutting (Phase 9 content plus this track's own additions)** | | |
| 15 | Testing | gtest, gmock, what makes a design testable, stress tests + ThreadSanitizer |
| 16 | Machine coding framework | The repeatable procedure for any LLD round, including the 45-minute plan |
| 17 | Beyond SOLID | DRY, KISS, YAGNI, Law of Demeter, and the anti-pattern catalog |
| 18 | C++ OOP rapid fire | Quick-revision Q&A: virtual destructors, Rule of Five, and more |
| 19 | AI and LLD | Where AI helps, where it hurts, what's asked about it |
| 20 | API design interviews | The 4th of 5 distinct LLD interview formats — signatures and contracts only |
| 21 | Schema design interviews | The 5th of 5 — ER modeling, cardinality, normalization to 3NF, denormalization, indexing |
| — | Machine coding problem set | Tic-Tac-Toe, Vending Machine, Parking Lot, Elevator, Movie Ticket Booking, Splitwise, LRU Cache, Message Queue/Pub-Sub, Rate Limiter, Task Scheduler, and a full roadmap of 36 problems in `lld/01_machine_coding_problems/00_problem_list_and_roadmap.md` |

## Phase 1 — The hardware model

Everything downstream assumes this. Do not skip it, and do not skim it.

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 01 | CPU pipelines and speculation | Why a stall costs what it costs; reading a top-down profile |
| 02 | Cache hierarchy | Lines not bytes; the stride/locality arithmetic |
| 03 | Cache coherence (MESI) | Why a contended atomic is expensive; false sharing |
| 04 | Branch prediction | Branchless idioms, and when they are a pessimisation |
| 05 | SIMD and vectorization | Auto-vec limits, intrinsics, NEON vs AVX |
| 06 | ARM vs x86 | Weak ordering, LL/SC, and why your APs behave differently |

## Phase 2 — OS internals and memory architecture

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 07 | Virtual memory, paging and the TLB | TLB reach; hugepage decisions with real arithmetic |
| 08 | NUMA | First-touch, migration, and why a socket boundary shows up as latency |
| 09 | Process scheduling | CFS/EEVDF, RT classes, PREEMPT_RT, the tail-latency story |
| 10 | Memory barriers | The hardware model under the C++ model |
| 11 | IPC mechanisms | Shared memory, eventfd, netlink, Unix sockets, and their costs |
| 12 | The user/kernel boundary | Syscall cost, vDSO, copy_to_user, and how to avoid crossing |

## Phase 3 — Kernel and device drivers

Capstone 1 lives here.

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 01 | Kernel modules and the build system | Getting a .ko to load without cargo-culting |
| 02 | Character drivers and the driver model | file_operations, cdev, sysfs, devicetree |
| 03 | Interrupt handling | Top/bottom halves, threaded IRQ, tasklets, workqueues, NAPI |
| 04 | DMA | Coherent vs streaming, IOMMU, the stale-data failure modes |
| 05 | Zero-copy networking | mmap rings, AF_XDP, DPDK, sendfile, splice |
| 06 | epoll and io_uring | Readiness vs completion, and when each wins |
| 07 | sysfs, procfs and debugfs | Exposing state the way a reviewer expects |
| 08 | RCU and kernel synchronisation | The read-mostly pattern that defines kernel concurrency |

## Phase 4 — Low-level design in modern C++ (systems LLD)

Capstone 2 lives here. Assumes Phase 0 — this is `lld/notes/`, the advanced
continuation of the foundations and machine-coding problems you already did.

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 01 | RAII, ownership and modern OOP | The design vocabulary a Staff reviewer expects |
| 02 | The C++ memory model | Every memory_order, with the assembly beside it |
| 03 | Thread-safe primitives | SPSC ring, seqlock, thread pool, timer wheel |
| 04 | Lock-free data structures | MPMC queues, ABA, hazard pointers, epoch reclamation |
| 05 | HAL architecture | Compile-time vs runtime polymorphism, and the cost of each |
| 06 | LLD interview problem set | Rate limiter, LRU, logger, parking lot, with full solutions |

## Phase 5 — High-level design

Capstone 3 lives here. Largely independent of phases 1-4; interleave freely.

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 01 | Estimation and latency budgets | The arithmetic that opens every design round |
| 02 | Data plane vs control plane | The separation that defines networking and infra design |
| 03 | Rate limiters | Token bucket through distributed quota |
| 04 | Distributed caching | Consistent hashing, invalidation, stampede control |
| 05 | Storage engines | LSM vs B-tree vs columnar, and time-series specifics |
| 06 | Telemetry pipelines | Ingest at a million samples/sec, backpressure, rollups |
| 07 | Edge-to-cloud device management | Fleet config, OTA, canary, rollback, multi-tenancy |

## Phase 6 — Advanced embedded and edge AI

| # | Tutorial | What it buys you |
| --- | --- | --- |
| 01 | RTOS design constraints | Priority inversion, WCET, static allocation, determinism |
| 02 | Edge AI acceleration | Quantization, ONNX Runtime EPs, TensorRT build vs runtime |
| 03 | Hardware-software co-design | What belongs in silicon and how to argue for it |

## Company mapping

Loop structure drifts by org and year — confirm format with your recruiter.
What does not drift is the weighting.

| Company | Centre of gravity | Read these first |
| --- | --- | --- |
| Nvidia | Fundamentals-first: memory hierarchy, DMA, PCIe, driver internals, C semantics. Derive, do not recall. | Phase 1 and 2 entire, kernel 03 and 04 |
| Cisco | Protocol depth, control vs data plane, embedded constraints, war stories. Your home turf — and the trap. | kernel 03-05, hld 02 and 07 |
| Palo Alto | Single-pass data plane, flow tables at line rate, zero-copy, adversarial traffic. | kernel 05, lld 03 and 04, hld 02 |
| Meta | Fast dense coding rounds; infra orgs add a systems deep dive. eBPF/XDP is high leverage. | lld 02-04, kernel 05, hld 03-06 |
| Google | Algorithmic rigour is real; design graded on scoping and data modelling before boxes and arrows. | hld entire, lld 06 |
| Microsoft | Varies most by org. Consistent thread: pragmatism, debugging, ownership. Ask which stack the team is on. | hld 07, kernel 07, interviews/behavioral |

The trap worth naming: Cisco is where you are most comfortable, which means the
default outcome is a lateral move at the same level. To be levelled up there you
must volunteer the architecture axis unprompted — fleet scale, cost, blast
radius — rather than settling into protocol detail where you are already fluent.
