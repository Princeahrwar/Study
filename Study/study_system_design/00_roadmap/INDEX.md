# Index — every file in this study system

Start with `HOW_TO_USE.md`. Then `CURRICULUM.md` for the order, and
`CAPSTONES.md` for the three projects.

## Focus Area 0 — LLD foundations and machine coding (`lld/00_foundations/`, `lld/01_machine_coding_problems/`)

    00_foundations/00_how_to_study_this_track.md    orientation, why this order

    -- Phase 1: Modern C++ core mechanics (beginner) --
    00_foundations/01_raii_ownership_and_move_semantics.md  RAII, unique/shared/weak_ptr, move semantics, Rule of Zero/Three/Five
    00_foundations/02_stl_for_system_design.md      containers by complexity/iterator-stability, <algorithm>, std::function Strategy

    -- Phase 2: OOAD and UML (beginner) --
    00_foundations/03_oop_fundamentals.md           four pillars, vtable cost, composition over inheritance
    00_foundations/04_uml_and_modeling.md           class, sequence, use case and state diagram notation

    -- Phase 3: Structural principles and C++ idioms (intermediate) --
    00_foundations/05_solid_principles.md           SRP/OCP/LSP/ISP/DIP, each with a violation and a fix
    00_foundations/06_cpp_design_idioms.md          Pimpl, CRTP, Type Erasure, NVI

    -- Phase 4: Design patterns in modern C++ (intermediate) --
    00_foundations/07_design_patterns_creational.md  Singleton (Meyers'), Factory Method, Abstract Factory, Builder, Prototype, Object Pool
    00_foundations/08_design_patterns_structural.md  Adapter, Bridge, Composite, Decorator, Facade, Flyweight, Proxy
    00_foundations/09_design_patterns_behavioral.md  Strategy, Observer, Command, State, Template Method, CoR, Iterator, Mediator, Memento, Visitor, Interpreter

    -- Phase 5: Concurrency, threading and resiliency (advanced) --
    00_foundations/10_concurrency_for_lld.md        mutex/atomic/condition_variable, the transfer() deadlock, scoped_lock
    00_foundations/11_threads_futures_and_concurrency_patterns.md   thread/jthread, async/future/promise, Dining Philosophers
    00_foundations/12_lockfree_and_threadsafe_containers.md         CAS/memory-order fundamentals, thread-safe Stack, striped-lock HashMap

    -- Phase 6: Specialized data structures and algorithms (advanced) --
    00_foundations/13_specialized_data_structures_for_systems.md    Trie, interval structures, graph+topological sort, LFU

    -- Phase 7: Application architecture and framework design (advanced) --
    00_foundations/14_application_architecture_patterns.md          layered architecture, event bus, exception strategy, Result pattern

    -- Cross-cutting (Phase 9 content plus this track's own additions) --
    00_foundations/15_testing_gtest_and_gmock.md    unit testing, mocking, stress tests + TSan
    00_foundations/16_machine_coding_framework.md   the repeatable procedure for any LLD round, timeboxed
    00_foundations/17_more_principles_and_antipatterns.md   DRY, KISS, YAGNI, Law of Demeter, anti-pattern catalog
    00_foundations/18_cpp_oop_interview_rapid_fire.md       quick-revision Q&A: virtual destructors, Rule of Five, and more
    00_foundations/19_ai_and_lld.md                 AI in interviews vs on the job, where it helps and hurts
    00_foundations/20_api_design_interviews.md      the 4th of 5 LLD interview formats: signatures and contracts, no implementation
    00_foundations/21_schema_design_interviews.md   the 5th of 5 LLD interview formats: ER modeling, normalization to 3NF, indexing

    01_machine_coding_problems/00_problem_list_and_roadmap.md   36-problem roadmap, tiered, company-mapped, with a recommended 10-problem core sequence
    01_machine_coding_problems/01_tic_tac_toe.md
    01_machine_coding_problems/02_parking_lot.md
    01_machine_coding_problems/03_elevator_system.md
    01_machine_coding_problems/04_splitwise_expense_sharing.md
    01_machine_coding_problems/05_lru_cache.md
    01_machine_coding_problems/06_vending_machine.md
    01_machine_coding_problems/07_movie_ticket_booking.md
    01_machine_coding_problems/08_message_queue_pubsub.md
    01_machine_coding_problems/09_rate_limiter_library.md
    01_machine_coding_problems/10_task_scheduler_cron.md

## Focus Area 1 & 2 — Architecture, OS internals, memory (`os_and_arch/notes/`)

    01_cpu_pipelines_and_speculation.md   OoO execution, ILP, MLP, top-down method
    02_cache_hierarchy.md                 lines not bytes, the four Cs, aliasing, NT stores
    03_cache_coherence_mesi.md            MESI, RFO, false sharing, perf c2c
    04_branch_prediction.md               predictors, branchless arithmetic, virtual dispatch
    05_simd_and_vectorization.md          roofline, auto-vec blockers, NEON/AVX, dispatch
    06_arm_vs_x86.md                      weak ordering, LL/SC vs CAS, measured differences
    07_virtual_memory_and_tlb.md          4-level paging, TLB reach, hugepages, shootdown
    08_numa.md                            first-touch, interleave, shared-nothing
    09_process_scheduling.md              CFS/EEVDF, RT classes, isolation, tail latency
    10_memory_barriers.md                 happens-before, the six orders, SPSC ring
    11_ipc_mechanisms.md                  shared memory, eventfd, futex, SCM_RIGHTS, netlink
    12_user_kernel_boundary.md            syscall cost, vDSO, the four avoidance strategies

    benchmarks/                           cache.cpp, tlb2.cpp, Makefile, RESULTS_baseline.md
    diagrams/                             memory load path, MESI, CPU pipeline, memory map

## Focus Area 3 — Kernel and drivers (`kernel_drivers/notes/`)

    01_kernel_modules.md                  environment rules, Kbuild, allocation, lifetime
    02_char_driver_and_model.md           cdev, file_operations, ioctl ABI, platform/DT, devm
    03_interrupt_handling.md              top/bottom halves, threaded IRQ, NAPI, spinlock rules
    04_dma.md                             coherent vs streaming, barriers, IOMMU, stale data
    05_zero_copy_networking.md            the ladder: recvmmsg -> AF_PACKET -> XDP -> AF_XDP -> DPDK
    06_epoll_and_io_uring.md              readiness vs completion, edge triggering, SQPOLL
    07_sysfs_procfs_debugfs.md            choosing an interface, tracepoints, bpftrace
    08_rcu_and_kernel_sync.md             RCU, grace periods, seqlock, per-cpu, lockdep

    diagrams/                             IRQ and DMA arrival path

## Focus Area 4 — Systems LLD in modern C++ (`lld/notes/`, assumes Focus Area 0)

    01_raii_and_modern_oop.md             ownership vocabulary, rule of five, noexcept moves
    02_cpp_memory_model.md                happens-before in practice, refcount pattern, seqlock
    03_thread_safe_primitives.md          SPSC, MPSC, thread pool, timer wheel, object pool
    04_lockfree_data_structures.md        ABA, hazard pointers, epochs, the flow table
    05_hal_architecture.md                compile-time vs runtime, testability seams
    06_lld_interview_problems.md          rate limiter, LRU, bounded queue, logger, pool

    diagrams/                             flow-table class diagram, epoch reclamation

## Focus Area 5 — HLD (`hld/notes/`)

    01_estimation_and_budgets.md          the latency ladder, the estimation procedure, cost
    02_data_plane_vs_control_plane.md     the separation, table updates, fast/slow path attack
    03_rate_limiters.md                   algorithms, distributed designs, shedding, backpressure
    04_distributed_caching.md             patterns, stampede/penetration/avalanche, hashing
    05_storage_engines.md                 LSM vs B-tree, time-series encoding, durability
    06_telemetry_pipelines.md             the Capstone 3 design, end to end
    07_edge_to_cloud_fleet_management.md  identity, desired state, safe rollout, OTA

    diagrams/                             telemetry architecture, rollout state machine, LSM paths

## Focus Area 6 — Embedded and edge AI (`embedded_ai/notes/`)

    01_rtos_design_constraints.md         priority inversion, WCET, schedulability, latency
    02_edge_ai_acceleration.md            roofline, quantization, fusion, ORT, TensorRT, thermal
    03_hw_sw_codesign.md                  what belongs in silicon, interfaces, power, Amdahl

## Interviews (`interviews/`)

    company_playbooks/                    one per target company, with traps
    mocks/00_mock_bank.md                 12 scenarios with Staff-level rubrics
    behavioral/01_the_war_story.md        one story, three zoom levels
    flashcards/00_core_numbers.md         the numbers to know cold

## Capstones (`capstones/`)

    cap1_dma_chardev/                     rkdma      — after kernel_drivers/04
    cap2_lockfree_filter/                 pfilt      — after lld/04
    cap3_telemetry_ingest/                telemetrix — after hld/06

## Rendering the diagrams

    npm install -g @mermaid-js/mermaid-cli
    mmdc -i path/to/diagram.mmd -o diagram.svg

All ten diagram sources in this tree were rendered successfully before being
committed here.
