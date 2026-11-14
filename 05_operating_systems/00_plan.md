# Operating Systems: Track Plan

> **Track code:** OS · **Weeks:** 5–14 · **Budget:** ~50 hours · **Prerequisites:** CO 02, 07, 09 and 13 (data representation, assembly, pipelining, caches)

An operating system is a set of answers to a few hard questions: how to share one CPU among many programs, how to give every program its own memory, how to let programs cooperate without corrupting each other, and how to talk to devices safely. This track teaches each answer from first principles, then shows how Linux actually implements it, with the numbers that make the trade-offs real.

---

## 1. Why this track matters

Operating-system concepts show up in system-software rounds (virtual memory, the user/kernel boundary, mutex versus semaphore versus spinlock, scheduling and context switches), in infrastructure and SRE rounds, underneath concurrency questions in coding rounds, and in networking and embedded roles ("how does a packet get from the NIC to my process?", shared memory between processes, real-time scheduling). The kernel, driver and system-design tracks reuse these ideas constantly.
---

## 2. What you will be able to do after this track

1. Explain exactly what happens on a system call, an interrupt and a context switch, at the instruction level.
2. Explain `fork`, `exec` and `wait`, zombies and orphans, and copy-on-write, and use them correctly in C.
3. Compare scheduling algorithms on a worked example, and explain how Linux's CFS and EEVDF schedulers decide what runs.
4. Solve the classic synchronisation problems (producer–consumer, readers–writers, dining philosophers) with the right primitive, and explain priority inversion.
5. Prevent, avoid and detect deadlocks, and run the banker's algorithm by hand.
6. Translate a virtual address through a multi-level page table, compute TLB reach, and explain a page fault end to end.
7. Explain inodes, the page cache, journaling, and every IPC mechanism with its cost.
8. Explain how a mutex is built from atomics and a futex, and pick the right spinlock for a contended multicore lock.
9. Work disk-scheduling and file-allocation numericals by hand, and explain how Linux schedules block I/O on an SSD.
10. Explain the OS protection model from Unix permissions to capabilities, SELinux and `seccomp`.

---

## 3. Topics

Topics 08, 12, 13, 18 and 19 are older, Staff-level notes; each is rewritten in the same detailed style as the new tutorials before you study it. The rest are new foundation tutorials. The plan grew from 15 to 21 topics after the November 2026 coverage audit (section 9): synchronisation, memory management, and file systems and I/O were each split in two, and protection and security, I/O systems with disk scheduling, and a question bank were added.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | [`01_os_structure_and_system_calls.md`](01_os_structure_and_system_calls.md) | OS structure and system calls | User and kernel mode, privilege levels, traps, exceptions and interrupts, the system-call mechanism (`syscall`, `svc`), monolithic vs microkernel vs hybrid, how Linux boots | Written | 5 |
| 02 | [`02_processes.md`](02_processes.md) | Processes | The process abstraction, PCB (`task_struct`), process states, `fork`, `exec`, `wait`, zombies and orphans, copy-on-write, the address-space layout, what a context switch saves | Written | 6 |
| 03 | [`03_threads.md`](03_threads.md) | Threads | Threads vs processes, kernel vs user threads, the 1:1 model in Linux (`clone`), pthreads API, thread-local storage, thread pools, the cost of creating and switching threads | Written | 6 |
| 04 | [`04_cpu_scheduling_algorithms.md`](04_cpu_scheduling_algorithms.md) | CPU scheduling algorithms | Metrics (turnaround, response, throughput, fairness), FCFS, SJF and SRTF, HRRN, round robin and quantum choice, priority and aging, multilevel queue, MLFQ, lottery and stride, real-time (rate-monotonic, EDF), multiprocessor scheduling | Written | 7 |
| 05 | [`05_process_scheduling.md`](05_process_scheduling.md) | Scheduling in Linux | CFS and EEVDF (virtual runtime, lag, weights from nice), run queues and load balancing, scheduling classes (`SCHED_OTHER`, `FIFO`, `RR`, `DEADLINE`, `BATCH`, `IDLE`), `sched_ext` in outline, preemption models and PREEMPT_RT, CPU isolation, where latency goes, measuring scheduling delay | Written | 7 |
| 06 | [`06_locks.md`](06_locks.md) | Locks: from Peterson to the futex | The critical-section problem and its three requirements, why disabling interrupts fails on multicore, Peterson's and Lamport's bakery algorithms, test-and-set, CAS and fetch-and-add, spinlocks (TAS, TTAS, ticket, MCS), spin vs yield vs sleep, two-phase locks, the futex and how a pthread mutex is built on it, lock contention and scalability, locked data structures (approximate counter, hand-over-hand list, concurrent hash table) | Written | 8 |
| 07 | [`07_condition_variables_and_semaphores.md`](07_condition_variables_and_semaphores.md) | Condition variables, semaphores and classic problems | Condition variables and the `while` rule, Mesa vs Hoare semantics, monitors, counting and binary semaphores, mutex vs semaphore, producer-consumer (bounded buffer), readers-writers (both preferences), dining philosophers, sleeping barber, barriers, the thundering herd; concurrency bug patterns (atomicity and order violations); priority inversion and inheritance | Written | 8 |
| 08 | [`08_memory_barriers.md`](08_memory_barriers.md) | Memory barriers and the memory model | The compiler as reorderer, release and acquire as a conditional, publication patterns (flag, pointer, seqlock), x86-64 vs arm64 assembly, a measured SPSC ring, ThreadSanitizer, the kernel barrier vocabulary, the driver descriptor and doorbell | Written | 9 |
| 09 | [`09_deadlocks.md`](09_deadlocks.md) | Deadlocks | The four Coffman conditions, resource-allocation and wait-for graphs, prevention (lock ordering, try-lock and back-off), avoidance (safe states, banker's algorithm), detection and recovery, livelock and starvation, lockdep as a real-world detector | Written | 9 |
| 10 | [`10_memory_management.md`](10_memory_management.md) | Memory management: address spaces, allocation and paging | Logical vs physical addresses, base and bounds, contiguous allocation (first, best, worst fit), internal and external fragmentation, compaction, free-space management (free lists, coalescing, segregated lists, buddy, slab), segmentation, paging and address translation by hand, page-table entry bits, multi-level, hashed and inverted page tables, TLB and effective access time | Written | 10 |
| 11 | [`11_virtual_memory.md`](11_virtual_memory.md) | Virtual memory: demand paging and replacement | Demand paging, the page-fault sequence (minor, major, invalid), effective access time with faults, page replacement (FIFO, OPT, LRU, Clock, second chance, NRU, LFU), Bélády's anomaly, frame allocation (global vs local), working sets, page-fault frequency and thrashing, memory-mapped files, copy-on-write, the swap space | Written | 10 |
| 12 | [`12_virtual_memory_and_tlb.md`](12_virtual_memory_and_tlb.md) | Virtual memory and the TLB in practice | The x86-64 and ARM64 page walk, TLB reach, huge pages (THP and hugetlbfs), the page-fault path, overcommit, swap and the OOM killer as a user sees them, measured TLB costs | Written | 11 |
| 13 | [`13_numa.md`](13_numa.md) | NUMA | What NUMA is, first-touch, explicit placement with `numactl` and `mbind`, diagnosing remote access, chiplets and sub-NUMA clustering | Written | 11 |
| 14 | [`14_io_systems_and_disk_scheduling.md`](14_io_systems_and_disk_scheduling.md) | I/O systems and disk scheduling | The OS view of devices (polling, interrupts, DMA; device drivers as the abstraction), block vs character devices, buffering, caching and spooling, the I/O models (blocking, non-blocking, multiplexed, signal-driven, asynchronous), disk geometry and access time (seek, rotation, transfer), disk scheduling (FCFS, SSTF, SCAN, C-SCAN, LOOK, C-LOOK) worked by hand, SSDs and why seek-based scheduling no longer applies, Linux block layer and I/O schedulers (`mq-deadline`, BFQ, Kyber, `none`) | Written | 11 |
| 15 | [`15_file_systems.md`](15_file_systems.md) | File systems: interface and implementation | Files, directories and paths, the file-descriptor table, open-file table and inode, hard and symbolic links, permissions, allocation methods (contiguous, linked and FAT, indexed, multi-level index, extents) with maximum-file-size arithmetic, free-space management (bitmaps), directory implementation, FFS and cylinder groups, the VFS layer, path lookup and the dentry cache, mounting | Written | 12 |
| 16 | [`16_crash_consistency_and_the_page_cache.md`](16_crash_consistency_and_the_page_cache.md) | Crash consistency, journaling and the page cache | The crash-consistency problem, `fsck`, journaling (data, ordered and writeback modes), log-structured and copy-on-write file systems (LFS, btrfs, ZFS), checksums and data integrity, the page cache and write-back, buffered vs direct I/O, `fsync`, `fdatasync` and the write-then-rename pattern, what ext4, XFS and F2FS choose | Written | 12 |
| 17 | [`17_ipc_fundamentals.md`](17_ipc_fundamentals.md) | IPC fundamentals | Pipes and FIFOs, signals and their handlers (`sigaction`, async-signal safety, `signalfd`), process groups, sessions, job control and daemons, System V and POSIX message queues, shared memory (shared anonymous and file mappings, POSIX `shm_open`, `memfd_create`, System V `shmget`), POSIX and System V semaphores, process-shared mutexes and condition variables, file and record locks (`flock`, `fcntl`), Unix domain sockets (`socketpair`, stream, datagram and `SOCK_SEQPACKET`), choosing between them | Written | 12 |
| 18 | [`18_ipc_mechanisms.md`](18_ipc_mechanisms.md) | IPC mechanisms and their real costs | Shared memory for a data plane, `eventfd`, futexes, fd passing over Unix sockets, netlink, measured costs | Exists, rewrite due | 13 |
| 19 | [`19_user_kernel_boundary.md`](19_user_kernel_boundary.md) | The user/kernel boundary | What a syscall really costs, the vDSO, `copy_to_user`, avoiding the boundary, `io_uring`, measuring | Exists, rewrite due | 13 |
| 20 | `20_protection_and_security.md` | Protection and security | Protection domains and the access matrix, ACLs vs capability lists, Unix permissions, `setuid` and the principle of least privilege, POSIX ACLs, Linux capabilities, mandatory access control (LSM, SELinux, AppArmor), `seccomp` and sandboxing, authentication and password storage in outline, kernel hardening (ASLR and KASLR, SMEP and SMAP, stack protector) with pointers to CO 30 | To write | 13 |
| 21 | `21_os_interview_question_bank.md` | OS interview question bank and numericals | Timed drills on every numeric type (scheduling, banker's algorithm, page replacement, address translation and effective access time, disk scheduling, file-size arithmetic, semaphore traces), end-to-end walkthroughs ("what happens when you run `./a.out`", "a `read()` that misses the page cache", "Ctrl-C in a shell"), and 60+ conceptual questions with answers | To write | 14 |

Diagrams: [`diagrams/memory_load_path.mmd`](diagrams/memory_load_path.mmd), [`diagrams/process_memory_map.mmd`](diagrams/process_memory_map.mmd).

### Covered in other tracks (not repeated here)

| Topic | Where |
|---|---|
| Hardware support for virtualization, VMs vs containers vs microVMs | CO 27 |
| Namespaces, cgroups v2, capabilities in use, building a container by hand | LX 02 |
| Disks, SSDs and the FTL, RAID, ECC and CRCs (the hardware side of storage) | CO 17 |
| Side channels, Spectre and Meltdown, KPTI, NX, ASLR, secure boot | CO 30 |
| `malloc` internals, arenas and pools (user-space allocators) | C 03 |
| ELF, linking and loading, dynamic libraries | C 06 |
| Kernel implementations: syscalls, buddy and SLUB, kernel locks, RCU, VFS objects, the block layer | K 02, K 03, K 04, K 05, K 07 |
| `epoll`, `io_uring`, the network stack | NET 05, NET 06 |
| Interrupt handling and DMA in drivers | DRV 05, DRV 07 |
| RTOS design, priority-inversion protocols, WCET | EMB 01 |
| Distributed file systems and consensus | HLD track |

---

## 4. Labs

All labs run in a Linux VM or on a Linux machine (see `99_toolchain/README.md`).

| Lab | Topics | What you build or measure |
|---|---|---|
| `labs/syscall_cost/` | 01, 19 | Time `getpid` via the vDSO and via a raw `syscall`, and a context switch via a pipe ping-pong |
| `labs/mini_shell/` | 02, 17 | A small shell with `fork`, `exec`, `wait`, pipes, redirection and job control; handles `SIGCHLD` and leaves no zombies |
| `labs/scheduler_sim/` | 04 | Simulate FCFS, SJF, RR and MLFQ on the same workload and print a Gantt chart with average turnaround and response |
| `labs/sync_problems/` | 06, 07, 09 | Producer-consumer, readers-writers and dining philosophers in C with pthreads; reproduce a deadlock, then fix it with lock ordering |
| `labs/page_table_walk/` | 10, 11, 12 | Translate addresses by hand from `/proc/self/pagemap`; trigger and count page faults with `mmap` and `perf` |
| `labs/ipc_bench/` | 17, 18 | Measure latency and throughput of a pipe, a Unix socket, a POSIX queue and shared memory with `eventfd` |

---

## 5. Representative interview questions

1. What happens, step by step, when a program calls `read()` on a file?
2. What is the difference between a process and a thread? What does `fork` copy, and what does copy-on-write defer?
3. What is a zombie process? How do you prevent zombies in a long-running server?
4. What exactly is saved and restored on a context switch? Why is a thread switch cheaper than a process switch?
5. Mutex vs semaphore vs spinlock: when is each the right choice? Can you take a mutex in an interrupt handler?
6. Explain priority inversion. How did it affect the Mars Pathfinder, and how does priority inheritance fix it?
7. What are the four conditions for deadlock? How do you design a system so deadlock cannot happen?
8. Translate a virtual address through a 4-level page table. How many memory accesses does a TLB miss cost?
9. What happens on a page fault? Distinguish a minor fault, a major fault and a segmentation fault.
10. Why does `write()` return before data reaches the disk? What does `fsync` guarantee?
11. You need two processes on the same machine to exchange 1 million messages a second. Which IPC do you choose and why?
12. How does Linux decide which thread runs next? What does a real-time priority change?
13. Why does a mutex built on a futex not enter the kernel when it is uncontended? When is a spinlock better than a mutex?
14. Why must a condition-variable wait sit in a `while` loop?
15. A disk head is at cylinder 53 with requests 98, 183, 37, 122, 14, 124, 65, 67. What total head movement do SSTF and C-LOOK give?
16. Hard link vs symbolic link: what survives deleting the original, and why can a hard link not cross file systems?
17. How do you update a file so that a crash leaves either the old or the new version, never a mix?
18. What does `setuid` do, and why is a capability set better than running as root?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Work the numeric problems (scheduling, banker's algorithm, page replacement, address translation) on paper; they are asked as whiteboard exercises.
3. For every mechanism, find where Linux implements it: the `08_kernel` track goes into the source, but naming the structure (`task_struct`, `mm_struct`, `struct file`) already lifts an answer.
4. Link to C++: CPP 07 (week 15) builds on OS 06 to 08.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| *Operating Systems: Three Easy Pieces* ([ostep.org](https://pages.cs.wisc.edu/~remzi/OSTEP/), free) | The main companion; clear and full of exercises and simulators |
| *Operating System Concepts*, Silberschatz, Galvin and Gagne | The classic textbook treatment; its chapter list is the checklist for numericals (scheduling, deadlocks, memory, disk scheduling) |
| *Operating Systems: Principles and Practice*, Anderson and Dahlin | The CMU 15-410 reading list follows it; strong on synchronisation |
| *The Linux Programming Interface*, Michael Kerrisk | The definitive Linux system-call reference, with examples |
| *Linux Kernel Development*, Robert Love | How Linux implements these ideas |
| [MIT 6.1810 (xv6)](https://pdos.csail.mit.edu/6.1810/) | A whole small kernel to read; labs on page tables, traps, copy-on-write, locking, file systems and `mmap` |
| Berkeley CS162 lectures (YouTube) | Lecture series used by the Coding Interview University list for processes, threads and synchronisation |
| Prof. Mythili Vutukuru, IIT Bombay (YouTube, NPTEL) | Concept-first OS lectures with practice problems and xv6 walkthroughs |
| Neso Academy, Gate Smashers (YouTube) | Drilling the numericals: scheduling, banker's algorithm, page replacement, disk scheduling |
| LWN.net | Current Linux design discussions (EEVDF, `sched_ext`, `io_uring`, folios) |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01 | OS structure and system calls | Written |
| 02 | Processes | Written |
| 03 | Threads | Written |
| 04 | CPU scheduling algorithms | Written (HRRN and multilevel queue added after the audit) |
| 05 | Scheduling in Linux | Written (replaces the older note) |
| 06 to 21 | All other topics | 06 is next (week 8); 08, 12, 13, 18, 19 are rewritten when reached |

---

## 9. Coverage audit (November 2026)

The plan was checked against these sources: OSTEP's chapter list, Galvin's chapter list (10th edition), the MIT 6.1810 lecture and lab schedule, Stanford CS111's lecture calendar, CMU 15-410's reading list, Coding Interview University, OS interview-question lists, and the YouTube channels most recommended for OS numericals. What changed:

| Gap found | Fix |
|---|---|
| Disk scheduling algorithms and the OS side of I/O (I/O models, buffering, block layer) were missing | New OS 14 |
| Protection and access control (access matrix, ACLs, capabilities, MAC, `seccomp`) were missing | New OS 20 |
| No drill bank for numericals and "what happens when" walkthroughs | New OS 21 |
| One synchronisation tutorial could not hold locks, futexes, scalable spinlocks and locked data structures as well as condition variables, semaphores and the classic problems | Split into OS 06 and OS 07 |
| Allocation, fragmentation and the buddy and slab allocators had no home; memory management was one tutorial | Split into OS 10 (allocation and paging) and OS 11 (demand paging and replacement) |
| File systems, crash consistency and the page cache were one tutorial | Split into OS 15 and OS 16 |
| HRRN and the multilevel queue were missing from scheduling | Added to OS 04 (section 4.5) |
| Process groups, sessions and daemons; overcommit, swap and the OOM killer; `sched_ext` | Added to OS 17, OS 12 and OS 05 |
| VMs and containers, hardware security, storage hardware | Already covered elsewhere (table in section 3) |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
