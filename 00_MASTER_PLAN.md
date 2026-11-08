# Master Plan: Senior/Staff Systems Interviews

**Goal:** Senior/Staff software and systems roles across product, infrastructure, networking and hardware companies, built on real understanding rather than memorised answers.

**Depth:** every necessary topic in full detail: the mechanism underneath, worked examples, tested code, interview questions and problems, and projects that prove it.

**Pace:** about **25 hours a week** (2.5 h on weekdays, 6 h on each weekend day). The program runs **26 weeks, from Monday 12 October 2026 to Sunday 11 April 2027.** The schedule is a guide, not a contract: take the time a topic needs and shift the weeks.

**First applications:** from **week 14 (11 January 2027)**. Interview pipelines take five to ten weeks, so study continues while you interview.

| Your hours per week | Full program takes | Apply-ready milestone |
|---|---|---|
| 20 | ~34 weeks | ~week 18 |
| 25 (assumed) | 26–27 weeks | week 14 |
| 30 | ~22 weeks | ~week 12 |
| 35+ | ~19 weeks | ~week 10 |

The track budgets in the table in section 2 add up to about 745 hours (the DSA and computer-organization tracks grew after coverage reviews); 26 weeks at 25 hours is 650, so at that pace the full program is nearer 30 weeks than 26. The overflow rule in section 5 still applies: when a week runs over, drop a stretch item, never the practice. The pace table above and the week tables in section 5 are to be re-cut once you choose a pace.

This file is the order and the schedule. The method is in [`README.md`](README.md), and each track's `00_plan.md` has its topics and their status.

---

## Contents

1. [What interview loops test](#1-what-interview-loops-test)
2. [The tracks](#2-the-tracks)
3. [Projects](#3-projects)
4. [How to read the schedule](#4-how-to-read-the-schedule)
5. [The 26-week schedule](#5-the-26-week-schedule)
6. [Weekly rhythm](#6-weekly-rhythm)
7. [Checkpoints](#7-checkpoints)

---

## 1. What interview loops test

Formats change by company, team and year, so confirm the format with the recruiter each time. Across senior systems loops the weighting is stable:

| Round | What it tests | Tracks that prepare for it |
|---|---|---|
| Coding | Medium–hard algorithm problems, often with a follow-up (streaming, a memory limit, concurrency); sometimes an existing codebase to fix or extend | DSA, C, C++ |
| Low-level design | Classes, interfaces, extensibility, thread safety, working code in 45–60 minutes | LLD, C++ |
| System design | Scoping, data modelling, trade-offs and scale numbers; for infrastructure roles, storage, queues and pipelines | HLD |
| Systems depth | OS, Linux, networking, kernel, drivers, caches and performance; interviewers ask you to derive, not recall | CO, OS, LX, NET, K, DRV |
| Domain | GPU and CUDA, embedded and real-time, or network security, depending on the team | GPU, EMB, NET |
| Behavioural | Ownership, ambiguity, conflict and impact, told through stories from your own work | INT |

**What this means for the plan:**

- **DSA is non-negotiable** and gets the largest share of time.
- **System design** often decides the level, and you are starting from zero, so it gets a full track from fundamentals up.
- **C, C++, OS, kernel and drivers** are where your experience is a real advantage.
- **Networking and network security** matter for infrastructure, security and network-equipment roles.
- **GPU and CUDA** need hands-on practice; an afternoon of reading does not prepare you, so it gets a proper track and a project.

Notes on individual companies are in [`15_interviews/`](15_interviews/00_plan.md). They are optional and are not the frame of this plan.

---

## 2. The tracks

Each track has a `00_plan.md` with its topics, status, week, labs, interview questions and resources. "Exists" counts older notes that are rewritten in the same detailed style as the new tutorials before you study them.

| # | Track | Code | Topics | Weeks | Hours |
|---|---|---|---|---|---|
| 01 | [DSA](01_dsa/00_plan.md) | DSA | 15 areas, 104 tutorials (89 core, 85 written), ~310 problems | 1–23 | ~230 core |
| 02 | [C language](02_c_language/00_plan.md) | C | 8 (1 written) | 1–8 | ~18 |
| 03 | [C++](03_cpp/00_plan.md) | CPP | Primer (14, written) + 8 interview topics | 9–16 | ~20 |
| 04 | [Computer organization](04_computer_organization/00_plan.md) | CO | 31 (all written) | 1–6 | ~70 core, ~83 all |
| 05 | [Operating systems](05_operating_systems/00_plan.md) | OS | 15 (6 exist) | 5–13 | ~35 |
| 06 | [Linux](06_linux/00_plan.md) | LX | 6 | 3–9, 24 | ~12 |
| 07 | [Networking](07_networking/00_plan.md) | NET | 9 core (2 exist) + 1 optional | 10–15 | ~25 |
| 08 | [Kernel internals](08_kernel/00_plan.md) | K | 8 (1 exists) | 14–20 | ~30 |
| 09 | [Device drivers](09_drivers/00_plan.md) | DRV | 10 (5 exist) | 15–20 | ~30 |
| 10 | [LLD](10_lld/00_plan.md) | LLD | 21 foundations + 33 problems (written) + 6 systems notes (exist) | 1–14, 19–22, 24 | ~85 |
| 11 | [HLD](11_hld/00_plan.md) | HLD | 17 fundamentals (4 exist) + 19 problems + 3 domain notes (exist) | 11–23 | ~80 |
| 12 | [GPU and CUDA](12_gpu_cuda/00_plan.md) | GPU | 3 + 1 stretch | 21–22 | ~12 |
| 13 | [Embedded](13_embedded/00_plan.md) | EMB | 1 core (exists) + stretch | 23 | ~4 |
| 14 | [Projects](14_projects/00_plan.md) | PRJ | 6 projects | 12–23 | ~70 net |
| 15 | [Interviews](15_interviews/00_plan.md) | INT | Stories, resume, mocks, flashcards, optional company notes | 5–26 | ~25 |
| 99 | [Toolchain](99_toolchain/README.md) | — | Mac, Linux VM, QEMU, counters, cloud GPU | 3, 14, 20, 21 | — |

In total: about **170 new tutorials** and **40 rewrites** of older notes, written one at a time as each track reaches them.

"IPC and communication" is covered in two places: local IPC in OS 13–14, and network communication in the networking track. Intrusion prevention (IPS) is NET 09.

---

## 3. Projects

Each project ends in a number you measured against a baseline, and a section titled "What I tried that did not work". Full specs are in [`14_projects/`](14_projects/00_plan.md).

| # | Project | What you build | Weeks |
|---|---|---|---|
| 01 | [`rkv`](14_projects/01_rkv_redis_compatible_server/SPEC.md) | Redis-compatible server in C: event loop, incremental rehashing, skip lists, TTLs, AOF; benchmarked against real Redis | 12–15 |
| 02 | [`rkdma`](14_projects/02_rkdma_dma_driver/SPEC.md) | Zero-copy DMA ring driver on your own kernel in QEMU | 15–19 |
| 03 | [`pfilt`](14_projects/03_pfilt_packet_classifier/SPEC.md) | Lock-free multi-core packet classifier that survives hash flooding | 20–22 |
| 04 | [`nanoinfer`](14_projects/04_nanoinfer_cuda_kernels/SPEC.md) | Transformer CUDA kernels optimised step by step against cuBLAS | 21–22 |
| 05 | [`telemetrix`](14_projects/05_telemetrix_fleet_telemetry/SPEC.md) | Fleet telemetry design and ingest prototype for 50,000 access points | 23 |
| 06 | [`tinyos`](14_projects/06_tinyos_teaching_kernel/SPEC.md) | Your own Unix-like kernel on RISC-V, from boot to shell | Stretch |

Put every project and your DSA solutions in public GitHub repositories, linked from your resume.

---

## 4. How to read the schedule

Topics are written as **code + number**, the number being the file number inside the track folder:

| Written as | Means |
|---|---|
| `CO 03` | `04_computer_organization/03_*.md` (same pattern for C, CPP, OS, LX, NET, K, DRV, GPU, EMB) |
| `LL 03`, `Trees 05` | `01_dsa/04_linked_lists/03_*`, `01_dsa/07_trees/05_*` |
| `LLD 07` | `10_lld/00_foundations/07_*.md` |
| `LLD-P 01` | `10_lld/01_machine_coding_problems/01_*.md` |
| `LLD-S 01` | `10_lld/02_systems_lld/01_*.md` |
| `HLD 01` | `11_hld/01_fundamentals/01_*.md` |
| `HLD-P 01` | `11_hld/02_design_problems/01_*.md` |
| `HLD-D 01` | `11_hld/03_systems_domain/01_*.md` |
| `rkv M1` | Milestone 1 of Project 01 |

Each week is about 25 hours: roughly 8 h DSA, 8 h systems, 6 h design, and 3 h for C/C++, projects and interviews. Project-heavy weeks (15–22) borrow 3–4 hours from DSA and design.

---

## 5. The 26-week schedule

### Phase A — Foundations (weeks 1–6)

> The CO column in the week tables below uses the earlier 12-topic numbering. The CO plan now has 31 tutorials, so this column is to be redone once you choose a pace.

| Week | Starts | DSA | Systems | Design | C/C++ · projects · interviews |
|---|---|---|---|---|---|
| 1 | 12 Oct | Arrays & strings I; review LL 01–02 and Trees 01 | CO 01, CO 02 | LLD 01, 02 | C 01; create the GitHub repos |
| 2 | 19 Oct | Arrays & strings II; LL 03 | CO 03, CO 04 | LLD 03, 04 | C 02 |
| 3 | 26 Oct | Hashing; LL 04 | CO 05, CO 06; LX 01 | LLD 05, 06 | C 03; set up the Linux VM |
| 4 | 2 Nov | Binary search & sorting; LL 05 | CO 07, CO 08; LX 02 | LLD 07 | C 04 |
| 5 | 9 Nov | Stacks & queues; LL 06 | CO 09, CO 10; OS 01 | LLD 08 | C 05; start the story bank |
| 6 | 16 Nov | Recursion & backtracking; Trees 02; LL 07 | CO 11, CO 12; OS 02, OS 03 | LLD 09 | C 06; **Checkpoint 1** |

### Phase B — Core interview material (weeks 7–14)

| Week | Starts | DSA | Systems | Design | C/C++ · projects · interviews |
|---|---|---|---|---|---|
| 7 | 23 Nov | Trees 03–05 | OS 04, OS 05; LX 03 | LLD 10, 11 | C 07 |
| 8 | 30 Nov | Trees 06–08 | OS 06, OS 07; LX 04 | LLD 12, 13, 14 | C 08 |
| 9 | 7 Dec | Trees 09–11 | OS 08, OS 09; LX 05 | LLD 15, 16, 17 | CPP 01 |
| 10 | 14 Dec | Trees 12, 15, 16 (heaps) | OS 10, OS 11; NET 01 | LLD-P 01, 06, 02 | CPP 02 |
| 11 | 21 Dec | Trees 17–18 (tries); graphs I | OS 12, OS 13; NET 02 | LLD-P 03, 07, 04; HLD 01, 02 | CPP 03 (holiday week: use as buffer if needed) |
| 12 | 28 Dec | Graphs II; Trees 28 (union-find) | OS 14; NET 03, NET 04 | LLD-P 05, 08; HLD 03, 04 | CPP 04; **rkv M1** |
| 13 | 4 Jan | Greedy & intervals; DP I | OS 15; NET 05, NET 06 | LLD-P 09, 10; HLD 05, 06, 07 | CPP 05; rkv M2–M3; resume rewrite |
| 14 | 11 Jan | DP II | NET 07, NET 08; K 01 | LLD 18, 20, 21; HLD 08, 09, 10 | CPP 06; rkv M4; first mock loop; **Milestone: apply-ready** |

### Phase C — Systems depth and design breadth (weeks 15–20)

| Week | Starts | DSA | Systems | Design | C/C++ · projects · interviews |
|---|---|---|---|---|---|
| 15 | 18 Jan | DP III; bits & math | NET 09; K 02; DRV 01 | HLD 11, 12; HLD-P 01 | CPP 07; rkv M5; **rkdma M1** |
| 16 | 25 Jan | String algorithms; Trees 13 (kernel rbtree) | K 03; DRV 02, DRV 03 | HLD 13, 14, 15; HLD-P 02 | CPP 08; rkdma M2 |
| 17 | 1 Feb | Trees 19–21 (range queries) | K 04, K 05; DRV 04, DRV 05 | HLD 16, 17; HLD-P 03, 04 | rkdma M3 |
| 18 | 8 Feb | Trees 24, 25, 29; mixed medium–hard set | K 06; DRV 06, DRV 07 | HLD-P 05, 06, 07 | rkdma M4 |
| 19 | 15 Feb | Timed mixed set; AI-assisted coding practice (LLD 19) | DRV 08, DRV 09 | HLD-P 08, 09, 10 | **rkdma shipped** |
| 20 | 22 Feb | Design-style coding; Trees 31, 34; systems-flavoured set (bits, parsing, caches, rate limiters) | DRV 10; K 07, K 08 | LLD-S 01, 02; HLD-P 11, 12 | **pfilt M1**; **Checkpoint 3** |

### Phase D — Company specials and projects (weeks 21–23)

| Week | Starts | DSA | Systems | Design | C/C++ · projects · interviews |
|---|---|---|---|---|---|
| 21 | 1 Mar | Mixed timed sets | GPU 01, GPU 02 | LLD-S 03, 04; HLD-P 13, 14 | pfilt M2–M3; **nanoinfer M1–M2** |
| 22 | 8 Mar | Mixed timed sets | GPU 03 | LLD-S 05, 06; LLD-P 22, 23, 24; HLD-P 15, 16; HLD-D 01 | **pfilt shipped**; nanoinfer M3 |
| 23 | 15 Mar | Weak-area review | EMB 01 | HLD-D 02, 03; HLD-P 17, 18, 19 | **telemetrix** design document and prototype |

### Phase E — Interview mode (weeks 24–26)

| Week | Starts | Focus |
|---|---|---|
| 24 | 22 Mar | Mock loops (two or three this week); six extended LLD problems cold; LX 06 and C 08 question banks; concept flashcards |
| 25 | 29 Mar | More mock loops with different interviewers; behavioural polish; notes for any company you are interviewing with |
| 26 | 5 Apr | Buffer and revision: flashcards, your own summary files, weak areas |

**When a week overflows,** carry the unfinished topic into the next week and drop a stretch item, never the practice problems or a project's measurements. If you fall more than two weeks behind, say so in the main chat and the remaining weeks get re-cut.

---

## 6. Weekly rhythm

| Day | Hours | Plan |
|---|---|---|
| Mon, Wed, Fri | 2.5 each | 1 h DSA (one or two problems) + 1.5 h systems tutorial |
| Tue, Thu | 2.5 each | 1 h DSA + 1.5 h design (LLD or HLD) |
| Sat | 6 | 3 h systems (tutorial and lab), 2 h C/C++ or project, 1 h DSA |
| Sun | 6 | 2.5 h DSA tutorial, 2 h design problem or mock, 1.5 h project or interviews |

Every topic goes through the five steps in [`README.md`](README.md): read, rebuild, run, practise, explain.

---

## 7. Checkpoints

| When | You should be able to | If not |
|---|---|---|
| **Checkpoint 1**, end of week 6 | ~60 problems solved; explain two's complement, a 5-stage pipeline and cache arithmetic; read simple x86-64 and ARM64 assembly; name and sketch the creational and structural patterns | Spend week 7's slack on the gap |
| **Milestone**, end of week 14 | ~150 problems, mediums under 30 minutes; the ten core LLD problems cold in 45 minutes; HLD 01–10 including estimation; OS fundamentals cold; TCP and sockets; `rkv` running against `redis-benchmark`; first mock loop scored against the rubric | **Start applying** |
| **Checkpoint 3**, end of week 20 | ~210 problems; kernel and driver tracks done; `rkdma` shipped with measured numbers; 12 HLD problems | **Apply** to the systems-heavy roles (kernel, drivers, networking, GPU) |
| **End**, week 26 | ~250 problems; 19 HLD designs; five projects on GitHub with measured results; mock loops scored at Senior/Staff on the rubric | Keep a weekly mock until offers close |
