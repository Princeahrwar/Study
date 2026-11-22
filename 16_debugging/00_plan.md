# Debugging: Track Plan

> **Track code:** DBG · **Weeks:** 8–14, then 17 and 24 · **Budget:** ~25 hours · **Prerequisites:** C 01 and C 02 (undefined behaviour, pointers), CO 07 (registers, stack frames, calling conventions), OS 02 (processes and signals)

Every systems interview has a debugging round in disguise: "this crashes once a day in production", "it works in the debugger but not outside", "memory grows by 1 MB an hour", "the service hangs and `top` shows nothing". The answer they want is a method and the right tool for each symptom, used fluently: `gdb` beyond `break` and `bt`, core dumps from stripped binaries, sanitizers and Valgrind, `strace` and `/proc` when you cannot stop the process, record and replay when the bug will not repeat, and `gdbserver`, QEMU and JTAG when the target is a board or a kernel. This track teaches each of them from the basics, with real sessions on real bugs.

It replaces the single Linux tutorial LX 04: one tutorial was not enough for something you will do every working day.

---

## 1. Why this track matters

- **Interviews.** Debugging scenarios appear in system-software, embedded, networking and infrastructure loops, and in behavioural rounds ("tell me about the hardest bug you fixed"). A structured answer, with the tools named in the right order, separates a senior candidate from a mid-level one.
- **Daily work.** On a Wi-Fi platform, the bugs that cost weeks are the intermittent ones: races, memory corruption, crashes on a device in the field. Knowing how to get a core dump off a box, symbolise it without the build tree, and replay a race is directly useful.
- **Other tracks depend on it.** The C, OS, kernel and driver tracks and the projects (`rkv`, `rkdma`, `pfilt`) all assume you can drive `gdb`, read a sanitizer report and decode a crash.

---

## 2. What you will be able to do after this track

1. Debug by method: reproduce, minimise, form a hypothesis, test it, and pick the tool from the symptom.
2. Drive `gdb` fluently: breakpoints with conditions and commands, hardware watchpoints, catchpoints, threads, forks, signals, memory and register inspection, disassembly, and Python scripting.
3. Turn a crash in production into a source line: capture a core dump, debug a stripped binary with separate debug information, `build-id` and `debuginfod`, and symbolise addresses with `addr2line`.
4. Find memory bugs with AddressSanitizer, UBSan, MemorySanitizer, LeakSanitizer and Valgrind, explain how each works and what it costs, and know which one catches which bug.
5. Find deadlocks and data races with `gdb`, ThreadSanitizer and Helgrind, and make an intermittent bug repeat with `rr` (record, replay, reverse execution, chaos mode).
6. Debug a process you cannot stop: `strace`, `ltrace`, `/proc/<pid>`, `lsof`, `LD_DEBUG`, `perf trace` and `bpftrace`.
7. Debug remotely: `gdbserver`, cross-architecture `gdb`, a kernel or bare-metal program in QEMU, and a board over JTAG or SWD with OpenOCD.
8. Answer the 30 standard debugging scenarios out loud, in order, with the evidence each step gives.

---

## 3. Topics

| # | File | Topic | Key subtopics | Priority | Status | Week |
|---|---|---|---|---|---|---|
| 01 | [`01_method_and_toolbox.md`](01_method_and_toolbox.md) | The debugging method and the toolbox | Reproduce, minimise, hypothesise, test; reading the symptom (crash, hang, wrong output, slow, leak, intermittent) and the first tool for each; building for debugging (`-g`, `-g3`, `-Og`, `-fno-omit-frame-pointer`, DWARF in outline); assertions and logging done well; print debugging and when it is the right tool; `git bisect` (including `git bisect run`); delta debugging with `cvise` in outline | Core | Written (Nov 2026) | 8 |
| 02 | `02_gdb_fundamentals.md` | `gdb` fundamentals | Starting, running and arguments; breakpoints (by line, function, address; temporary, conditional, `rbreak`), stepping (`step`, `next`, `finish`, `until`, `advance`), `print` with formats and `p *arr@n`, `display`, `x` to examine memory, `info locals`, `info frame`, `backtrace` and moving between frames, `ptype` and `whatis`, the TUI, `.gdbinit`; why variables are "optimized out" at `-O2` and what to do; the LLDB equivalents for the Mac | Core | To write | 8 |
| 03 | `03_gdb_advanced.md` | `gdb` in depth | Hardware against software watchpoints, `rwatch` and `awatch`, watching a heap address; breakpoint `commands` and `dprintf`; catchpoints (`syscall`, `signal`, `fork`, `exec`, `throw`); signals (`handle`); attaching to a running process; multi-process (`follow-fork-mode`, `detach-on-fork`, inferiors) and threads (`info threads`, `thread apply all bt`, `scheduler-locking`, non-stop mode); calling functions and changing state (`call`, `set var`, `jump`, `return`); assembly-level debugging (`layout asm`, `stepi`, `info registers`, `x/i $pc`); pretty printers for C++ containers; Python scripting (a custom command and a breakpoint that decides in Python); `checkpoint` | Core | To write | 9 |
| 04 | `04_crashes_core_dumps_and_symbols.md` | Crashes, core dumps and symbols | Signals that crash (`SIGSEGV`, `SIGBUS`, `SIGABRT`, `SIGFPE`, `SIGILL`) and what each usually means; enabling core dumps (`ulimit -c`, `core_pattern`, `systemd-coredump` and `coredumpctl`); a post-mortem session in `gdb`; stripped binaries, separate debug files, `build-id` and `debuginfod`; `addr2line`, `eu-stack` and `nm` to turn addresses into lines; position-independent executables and load addresses; unwinding with frame pointers against DWARF CFI, and why a backtrace comes out corrupt; in-process crash handlers (`backtrace(3)`, `sigaltstack`) and what is safe in them; minidumps (Breakpad, Crashpad) in outline; getting a core off an embedded device | Core | To write | 10 |
| 05 | `05_memory_bugs_sanitizers_and_valgrind.md` | Memory bugs: sanitizers and Valgrind | How AddressSanitizer works (shadow memory, red zones, quarantine) and what it catches; LeakSanitizer; UBSan checks worth enabling; MemorySanitizer for uninitialised reads; HWASan and Arm MTE in outline; Valgrind Memcheck, Massif (heap profiles) and Cachegrind; glibc's `MALLOC_CHECK_` and `mtrace`; hunting heap corruption with a watchpoint; the same eight bugs run under every tool, with the slowdown measured | Core | To write | 11 |
| 06 | `06_concurrency_bugs.md` | Concurrency bugs: deadlocks, races and heisenbugs | Diagnosing a deadlock from `thread apply all bt` and the lock graph; ThreadSanitizer (happens-before, how it reports) and Helgrind and DRD; data races that only show on Arm (link to CO 20); atomics misuse; lock contention (`perf lock`, `mutrace`); priority inversion symptoms; making a race repeat (stress, `taskset`, sleeps, `rr` chaos mode); why adding a `printf` makes the bug go away | Core | To write | 12 |
| 07 | `07_debugging_live_systems.md` | Debugging a live process without stopping it | `strace` (`-f`, `-T`, `-tt`, `-e`, `-k`, `-p`), `ltrace`; `/proc/<pid>` (`status`, `stack`, `wchan`, `fd`, `maps`, `limits`, `sched`); `lsof` and `ss` for file-descriptor and socket leaks; process states and the `D` state; `gdb -batch -ex "thread apply all bt"` as a stack sampler; `LD_DEBUG` for loader problems and `LD_PRELOAD` shims; `perf trace` and `bpftrace` one-liners for debugging (link to LX 05); triage of "hung", "100 % CPU" and "memory grows" | Core | To write | 13 |
| 08 | `08_record_replay_and_hard_bugs.md` | Record and replay, and the hardest bugs | `rr`: record, replay, `reverse-continue`, reverse watchpoints, chaos mode, and how it works (deterministic replay of one core, hardware performance counters); `gdb`'s own process record; bugs that vanish under the debugger (timing, uninitialised memory, environment, address-space layout); compiler bugs and how to prove one; minimising a failing input with `cvise` and fuzzers (link to C 11); intermittent failures in CI | Stretch | To write | 14 |
| 09 | `09_remote_embedded_and_kernel_debugging.md` | Remote, embedded and kernel debugging | `gdbserver` and the remote protocol; cross-architecture `gdb-multiarch` and a sysroot; debugging in QEMU (`-s -S`); a Cortex-M program over SWD with OpenOCD, reading a hard-fault frame; the kernel in QEMU with `vmlinux` and the `lx-*` helpers; `kgdb` in outline; where this hands over to K 08 (oops decoding, ftrace, KASAN, lockdep, kdump) and DRV 04 | Core for embedded and driver roles | To write | 17 |
| 10 | `10_debugging_interview_scenarios.md` | Debugging interview scenarios | 30 scenarios answered step by step: a segfault only at `-O2`, works under `gdb` but not outside, a crash with a corrupt backtrace, memory that grows slowly, 100 % CPU, a hang, a deadlock in production, file-descriptor exhaustion, slow after N days, a crash only on Arm, a bug that disappears with logging, a core dump with no symbols, and others; for each: the first question, the first tool, what each result tells you | Core | To write | 24 |

---

## Where related topics live

| Subject | Where |
|---|---|
| Kernel debugging: oops decoding, `printk`, dynamic debug, ftrace, kprobes, KASAN, KCSAN, lockdep, kdump and `crash` | [K 08](../08_kernel/00_plan.md); DBG 09 sets up the tools |
| Driver observability: debugfs, tracepoints | [DRV 04](../09_drivers/00_plan.md) |
| Performance debugging: `perf`, flame graphs, the USE method, eBPF | [LX 05, LX 10 and LX 11](../06_linux/00_plan.md) |
| JTAG and SWD hardware, hard-fault dumps on a microcontroller | [EMB 04](../13_embedded/00_plan.md); DBG 09 covers the `gdb` side |
| Testing, fuzzing and static analysis (finding bugs before they ship) | [C 11](../02_c_language/00_plan.md) |
| Undefined behaviour and why `-O0` against `-O2` differ | [C 01](../02_c_language/01_types_conversions_and_undefined_behavior.md) |
| Registers, stack frames and reading assembly | [CO 07](../04_computer_organization/00_plan.md) |
| GPU debugging (`cuda-gdb`, `compute-sanitizer`) | [GPU track](../12_gpu_cuda/00_plan.md) |

---

## 4. Labs

All labs run on Linux (the VM from [`99_toolchain`](../99_toolchain/README.md)); `rr` needs hardware performance counters, so it does not run on Apple Silicon or in many cloud VMs.

| Lab | Topics | What you build or find |
|---|---|---|
| `labs/gdb_drills/` | 02, 03 | Ten small programs, each with one bug that one `gdb` feature finds fastest (a watchpoint, a catchpoint, a conditional breakpoint, a thread switch, `finish` to see a return value) |
| `labs/debug_the_crash/` | 04, 05, 06, 07 | Five buggy programs (segfault, deadlock, leak, data race, file-descriptor leak); find each with the right tool (moved here from the Linux track) |
| `labs/stripped_core/` | 04 | Build with separate debug files, strip, crash, and debug the core with `build-id` and a local `debuginfod` |
| `labs/sanitizer_matrix/` | 05 | The same eight memory bugs under ASan, MSan, UBSan, Valgrind and `MALLOC_CHECK_`: which tool catches which, and the slowdown of each |
| `labs/race_hunt/` | 06, 08 | A race that fails once in a thousand runs; catch it with TSan, then record it with `rr` chaos mode and walk backwards to the bad write |
| `labs/qemu_gdb/` | 09 | `gdbserver` on an Arm user-mode program in QEMU, then a bare-metal Cortex-M program in QEMU with `gdb-multiarch` |

---

## 5. Representative interview questions

1. A program crashes once a day in production. How do you capture enough information to fix it?
2. A program segfaults at `-O2` but not at `-O0`. What do you suspect, and what do you run?
3. It works under `gdb` but crashes outside it. Name four reasons.
4. How does a hardware watchpoint work, and how many can you have on x86-64?
5. How does AddressSanitizer detect a heap overflow? What does it cost, and what can it not catch?
6. You have a core dump from a stripped binary. How do you get a backtrace with line numbers?
7. Why is a backtrace sometimes corrupt or truncated? How do frame pointers help?
8. A service is hung. What do you look at first, without restarting it?
9. Memory grows slowly over days. How do you tell a leak from fragmentation or a cache that never shrinks?
10. How do you find a deadlock in a running process? A data race that you cannot reproduce?
11. What does `rr` do, and why can it replay a race exactly?
12. How do you debug code on a board that has no screen and no network, only a JTAG header?
13. What is the difference between `strace` and `ltrace`, and what does each cost the traced process?
14. Tell me about the hardest bug you have fixed. (Structure: symptom, hypotheses, evidence, fix, what you changed so it cannot recur.)

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Type every `gdb` command yourself, on the lab programs; debugging is a skill of the fingers as much as of the head.
3. Keep a "bug diary" from your own work: symptom, tool, root cause, time spent. It becomes your answer to question 14.
4. Repeat each lab at the end of the track without notes, and time yourself.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| *Debugging with GDB* (the GDB manual, sourceware.org) | The reference for every command |
| *The Art of Debugging with GDB, DDD, and Eclipse*, Matloff and Salzman | A readable introduction |
| *Debugging: The 9 Indispensable Rules*, David Agans | The method, independent of tools |
| *Why Programs Fail*, Andreas Zeller | Systematic debugging, delta debugging |
| The `rr` project site and its paper (O'Callahan et al., 2017) | How record and replay works |
| AddressSanitizer and ThreadSanitizer papers and wikis (Google) | How the sanitizers work |
| Red Hat Developer "GDB for developers" series and the `debuginfod` articles | Debug information in practice |
| Brendan Gregg's site | Tracing and performance debugging |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01 | Method and toolbox | Written (Nov 2026) |
| 02–10 | Remaining topics | To write (next: DBG 02) |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
