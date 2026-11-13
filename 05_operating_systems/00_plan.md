# Operating Systems: Track Plan

> **Track code:** OS · **Weeks:** 5–13 · **Budget:** ~35 hours · **Prerequisites:** CO 02, 07, 09 and 13 (data representation, assembly, pipelining, caches)

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

---

## 3. Topics

Topics 05, 07, 10, 11, 14 and 15 are older, Staff-level notes; each is rewritten in the same detailed style as the new tutorials before you study it. The rest are new foundation tutorials.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | [`01_os_structure_and_system_calls.md`](01_os_structure_and_system_calls.md) | OS structure and system calls | User and kernel mode, privilege levels, traps, exceptions and interrupts, the system-call mechanism (`syscall`, `svc`), monolithic vs microkernel vs hybrid, how Linux boots | Written | 5 |
| 02 | [`02_processes.md`](02_processes.md) | Processes | The process abstraction, PCB (`task_struct`), process states, `fork`, `exec`, `wait`, zombies and orphans, copy-on-write, the address-space layout, what a context switch saves | Written | 6 |
| 03 | [`03_threads.md`](03_threads.md) | Threads | Threads vs processes, kernel vs user threads, the 1:1 model in Linux (`clone`), pthreads API, thread-local storage, thread pools, the cost of creating and switching threads | Written | 6 |
| 04 | [`04_cpu_scheduling_algorithms.md`](04_cpu_scheduling_algorithms.md) | CPU scheduling algorithms | Metrics (turnaround, response, throughput, fairness), FCFS, SJF and SRTF, round robin and quantum choice, priority and aging, MLFQ, lottery and stride, real-time (rate-monotonic, EDF), multiprocessor scheduling | Written | 7 |
| 05 | [`05_process_scheduling.md`](05_process_scheduling.md) | Scheduling in Linux | CFS and EEVDF, scheduling classes (`SCHED_OTHER`, `FIFO`, `RR`, `DEADLINE`), preemption models and PREEMPT_RT, where latency goes, measuring scheduling delay | Exists, rewrite due | 7 |
| 06 | `06_synchronization.md` | Synchronisation | Race conditions and critical sections, Peterson's algorithm, hardware atomics (test-and-set, CAS), spinlocks, mutexes, semaphores, condition variables, monitors, RW locks; producer–consumer, readers–writers, dining philosophers; priority inversion and inheritance | To write | 8 |
| 07 | [`07_memory_barriers.md`](07_memory_barriers.md) | Memory barriers and the memory model | Hardware vs language memory models, the canonical reordering example, the six C++ memory orders, StoreLoad, fences, the kernel's barrier vocabulary, a correct SPSC ring | Exists, rewrite due | 8 |
| 08 | `08_deadlocks.md` | Deadlocks | The four Coffman conditions, resource-allocation graphs, prevention (lock ordering), avoidance (banker's algorithm), detection and recovery, livelock and starvation, lockdep as a real-world detector | To write | 9 |
| 09 | `09_memory_management.md` | Memory management | Address spaces, segmentation vs paging, page tables and multi-level tables, demand paging, page faults, replacement (FIFO, OPT, LRU, Clock), Bélády's anomaly, working sets and thrashing, `mmap`, copy-on-write | To write | 9 |
| 10 | [`10_virtual_memory_and_tlb.md`](10_virtual_memory_and_tlb.md) | Virtual memory and the TLB in practice | The x86-64 and ARM64 page walk, TLB reach, huge pages (THP and hugetlbfs), the page-fault path, measured TLB costs | Exists, rewrite due | 10 |
| 11 | [`11_numa.md`](11_numa.md) | NUMA | What NUMA is, first-touch, explicit placement with `numactl` and `mbind`, diagnosing remote access, chiplets and sub-NUMA clustering | Exists, rewrite due | 10 |
| 12 | `12_file_systems_and_io.md` | File systems and I/O | Files, directories and inodes, allocation (contiguous, linked, indexed, extents), the VFS, journaling and crash consistency, the page cache, buffered vs direct I/O, `fsync`, block devices and I/O schedulers | To write | 11 |
| 13 | `13_ipc_fundamentals.md` | IPC fundamentals | Pipes and FIFOs, signals and their handlers (`sigaction`, async-signal safety, `signalfd`), System V and POSIX message queues, shared memory (shared anonymous and file mappings, POSIX `shm_open`, `memfd_create`, System V `shmget`), POSIX and System V semaphores, process-shared mutexes and condition variables, file and record locks (`flock`, `fcntl`), Unix domain sockets (`socketpair`, stream, datagram and `SOCK_SEQPACKET`), choosing between them | To write | 11 |
| 14 | [`14_ipc_mechanisms.md`](14_ipc_mechanisms.md) | IPC mechanisms and their real costs | Shared memory for a data plane, `eventfd`, futexes, fd passing over Unix sockets, netlink, measured costs | Exists, rewrite due | 12 |
| 15 | [`15_user_kernel_boundary.md`](15_user_kernel_boundary.md) | The user/kernel boundary | What a syscall really costs, the vDSO, `copy_to_user`, avoiding the boundary, `io_uring`, measuring | Exists, rewrite due | 13 |

Diagrams: [`diagrams/memory_load_path.mmd`](diagrams/memory_load_path.mmd), [`diagrams/process_memory_map.mmd`](diagrams/process_memory_map.mmd).

---

## 4. Labs

All labs run in a Linux VM or on a Linux machine (see `99_toolchain/README.md`).

| Lab | Topics | What you build or measure |
|---|---|---|
| `labs/syscall_cost/` | 01, 15 | Time `getpid` via the vDSO and via a raw `syscall`, and a context switch via a pipe ping-pong |
| `labs/mini_shell/` | 02, 13 | A small shell with `fork`, `exec`, `wait`, pipes, redirection and job control; handles `SIGCHLD` and leaves no zombies |
| `labs/scheduler_sim/` | 04 | Simulate FCFS, SJF, RR and MLFQ on the same workload and print a Gantt chart with average turnaround and response |
| `labs/sync_problems/` | 06, 08 | Producer–consumer, readers–writers and dining philosophers in C with pthreads; reproduce a deadlock, then fix it with lock ordering |
| `labs/page_table_walk/` | 09, 10 | Translate addresses by hand from `/proc/self/pagemap`; trigger and count page faults with `mmap` and `perf` |
| `labs/ipc_bench/` | 13, 14 | Measure latency and throughput of a pipe, a Unix socket, a POSIX queue and shared memory with `eventfd` |

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

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Work the numeric problems (scheduling, banker's algorithm, page replacement, address translation) on paper; they are asked as whiteboard exercises.
3. For every mechanism, find where Linux implements it: the `08_kernel` track goes into the source, but naming the structure (`task_struct`, `mm_struct`, `struct file`) already lifts an answer.
4. Link to C++: CPP 07 (week 15) builds on OS 06 and 07.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| *Operating Systems: Three Easy Pieces* (ostep.org, free) | The main companion; clear and full of exercises |
| *The Linux Programming Interface*, Michael Kerrisk | The definitive Linux system-call reference, with examples |
| *Operating System Concepts*, Silberschatz, Galvin & Gagne | The classic textbook treatment of scheduling, deadlocks and memory |
| *Linux Kernel Development*, Robert Love | How Linux implements these ideas |
| LWN.net | Current Linux design discussions (EEVDF, `io_uring`, folios) |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01 | OS structure and system calls | Written |
| 02 | Processes | Written |
| 03 | Threads | Written |
| 04 | CPU scheduling algorithms | Written |
| 05–15 | All other topics | 05 is next (week 7); 05, 07, 10, 11, 14, 15 are rewritten when reached |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
