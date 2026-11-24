# Study

A self-study program for senior systems-engineering work and interviews: fifteen tracks, six projects, one numbered tree. The same tree is mirrored in the Study-Plan project.

## Start here

1. Skim [`00_MASTER_PLAN.md`](00_MASTER_PLAN.md) once for the order of the tracks and the week-by-week schedule.
2. Open the `00_plan.md` of the track you are on. It lists the topics and which are written.
3. Take the topics one at a time, in order, and finish each one before starting the next.

---

## The tree

| Folder | Track | What is inside |
|---|---|---|
| [`01_dsa/`](01_dsa/00_plan.md) | Data structures and algorithms | 13 areas, one folder each; [practice log](01_dsa/practice_log.md) |
| [`02_c_language/`](02_c_language/00_plan.md) | C in depth | Types and UB, pointers, memory, layout, linking, advanced pointers and pointer bugs, struct design and OOP in C, advanced features, testing, fuzzing and secure C, question bank |
| [`03_cpp/`](03_cpp/00_plan.md) | C++ | `01_primer/` (from scratch) and `02_interview/` (internals) |
| [`04_computer_organization/`](04_computer_organization/00_plan.md) | Computer organization | Bits to out-of-order cores, caches, DRAM, coherence, GPUs, PCIe, virtualization |
| [`05_operating_systems/`](05_operating_systems/00_plan.md) | Operating systems | System calls to file systems, IPC, protection and a numericals bank (21 topics) |
| [`06_linux/`](06_linux/00_plan.md) | Linux | Shell and text tools, `/proc`, memory and storage in practice, networking tools, containers, `perf` and eBPF, tuning (debugging has its own track) |
| [`07_networking/`](07_networking/00_plan.md) | Networking | L2 to TLS, sockets, the Linux stack, firewalls and IPS, datacenter fabrics, RDMA, load balancing, QoS |
| [`08_kernel/`](08_kernel/00_plan.md) | Kernel internals | Your own kernel in QEMU: syscalls, memory, locking, RCU, debugging |
| [`09_drivers/`](09_drivers/00_plan.md) | Device drivers | Modules to PCIe and NAPI |
| [`10_lld/`](10_lld/00_plan.md) | Low-level design | Foundations, 33 problems, systems LLD, a buildable CMake project |
| [`11_hld/`](11_hld/00_plan.md) | System design | Fundamentals, 19 design problems, domain designs |
| [`12_gpu_cuda/`](12_gpu_cuda/00_plan.md) | GPU and CUDA | Architecture, CUDA, the system view |
| [`13_embedded/`](13_embedded/00_plan.md) | Embedded and edge AI | RTOS constraints; stretch topics |
| [`16_debugging/`](16_debugging/00_plan.md) | Debugging | The method, `gdb` in depth, core dumps, sanitizers, concurrency bugs, live processes, `rr`, remote and embedded |
| [`14_projects/`](14_projects/00_plan.md) | Projects | Six specs: `rkv`, `rkdma`, `pfilt`, `nanoinfer`, `telemetrix`, `tinyos` |
| [`15_interviews/`](15_interviews/00_plan.md) | Interviews | Stories, mocks, flashcards, optional company notes |
| [`99_toolchain/`](99_toolchain/README.md) | Toolchain | Mac, Linux VM, QEMU kernels, cloud GPU |

Inside a track: numbered tutorials in study order (`NN_topic.md`), the code that goes with a tutorial beside it (`NN_topic_demo.cpp`), and `diagrams/` and `labs/` where needed. A gap in the numbers is a topic that is planned but not written yet.

---

## Studying one topic

1. **Read** it once, carefully, and trace the worked examples on paper.
2. **Rebuild** the main code or diagram from memory with the file closed, then compare.
3. **Run** the companion code. Change something and predict the result before you run it.
4. **Practise**: the problems and exercises at the end. This is where most of the learning happens.
5. **Explain** it out loud as if an interviewer had asked, and write a short summary from memory as `NN_summary.md` beside the tutorial. What you cannot reconstruct is what to re-read tomorrow.

Ask doubts in the track's chat and say "next" when you are ready for the following topic. Measure on your own machine whenever a tutorial gives a number; your ARM Mac will disagree with x86 figures in places, and those differences are good material for an interview.

---

## How the tutorials are written

There is no fixed template; each one follows its topic. What every tutorial aims for:

- **Understanding from the mechanism up.** An intuition or a picture first, then how it really works, with diagrams.
- **Worked examples with real values**, traced step by step.
- **Tested code** beside the tutorial, compiled with warnings and sanitizers and ending in `N/N checks passed`. Code shown in the text is copied from the tested file.
- **Interview questions with answer outlines**, and **problems worked in full** with exercises, using LeetCode numbers where they exist.
- **General knowledge, not history or company lore.** The content applies across companies; company-specific notes live only in `15_interviews/`, and are optional.

---

## Status words

Used in the topic tables of the track plans.

| Status | Meaning |
|---|---|
| **Done** | Studied, with exercises and a summary |
| **Written** | Ready to study |
| **Exists, rewrite due** | An older, dense note; it is rewritten before you study it |
| **To write** | Planned; written when the track reaches it |
| **Stretch** | Worth doing only after the schedule |

---

## Progress

| Track | Status | Next |
|---|---|---|
| DSA | Arrays and strings 01 to 07, Hashing 01 to 04, Binary search 01 to 05, LL 01 to 07, Stacks and queues 01 to 04, Recursion and backtracking 01 to 04 (area complete), Trees 01 to 34 written (area complete), Graphs 01 to 09 written (area complete), Greedy and intervals 01 to 03 written (area complete), Dynamic programming 01 to 08 written (area complete); C language 01 written, Bits and math 01 to 04 written (area complete), String algorithms 01 to 06 written (area complete), Design-style coding 01 to 04 written (area complete), Theory and advanced techniques 01 to 04 written (area complete), Pattern map and drills 01 written (DSA tutorials complete) | C 02 pointers and arrays, then C 02 pointers and arrays |
| C | C 01 to C 12 written; track complete | The next track |
| C++ | Primer written | CPP 01 |
| Computer organization | CO 01 to 31 written; track complete | The next track |
| Operating systems | OS 01 to 21 written; track complete; 2 older notes | The next track |
| Linux | Plan widened to 14 tutorials (audited Nov 2026); LX 01 to LX 14 written; track complete | none |
| Networking | Plan widened to 15 topics plus a Wi-Fi sub-track of 15 (audited Nov 2026); NET 01 to NET 09 and NET 11 to NET 15 written; WF 01 to WF 13 written, WF 14 and WF 15 in progress (priority raised 23 Nov 2026) | WF 14 |
| Debugging | DBG 01 written (method and toolbox) | DBG 02 |
| Kernel | 1 older note | K 01 |
| Drivers | 5 older notes | DRV 01 |
| LLD | Fully written | LLD 01–02 |
| HLD | 7 older notes | HLD 01 |
| GPU and CUDA | Planned | GPU 01 |
| Embedded | 3 older notes | EMB 01 |
| Projects | 6 specs | `rkv` M1 |
| Interviews | Mock bank and core numbers exist | Story bank |

---

## Where things are saved

- **Mac:** `Desktop/My Projects/Study/`
- **Study-Plan project:** the same paths, starting with `Study/`

Every new or changed file is saved to both.
