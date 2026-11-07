# Linux: Track Plan

> **Track code:** LX · **Weeks:** 3, 4, 7, 8, 9, and 24 for the question bank · **Budget:** ~12 hours · **Prerequisites:** you use Linux daily; OS 01–02 help for topic 02

You already know Linux as a user and developer. This track is revision with a purpose: turn daily familiarity into precise answers about how the system works, and into fluency with the debugging and performance tools that interviewers expect a senior systems engineer to reach for without thinking.

---

## 1. Why this track matters

"A process is hung, slow or leaking: how do you debug it?" is asked in system-software, infrastructure and networking loops alike, and the answer is a toolkit: `gdb`, `strace`, `perf`, `/proc`, core dumps, `tcpdump`, `ss`, `ip`, eBPF tracing, and how containers and namespaces isolate processes. SRE-flavoured questions add load average, memory pressure and file descriptors. Shell fluency also shows in every live environment.
---

## 2. What you will be able to do after this track

1. Answer "what does this command do" and "write a one-liner that…" questions with `grep`, `sed`, `awk`, `find`, `xargs` and `sort`.
2. Explain signals, `/proc`, resource limits, cgroups, namespaces and capabilities, and build a minimal container by hand.
3. Describe the boot sequence from firmware to a login prompt, and diagnose a failing systemd unit.
4. Debug a crash from a core dump, a hang with `gdb` and `strace`, and a leak with sanitizers and Valgrind.
5. Find where CPU time goes with `perf` and a flame graph, and trace kernel events with `ftrace` and `bpftrace`.
6. Use the network toolbox (`ip`, `ss`, `tcpdump`, `ethtool`, `iperf3`) to diagnose a connectivity or throughput problem.

---

## 3. Topics

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_shell_and_core_tools.md` | The shell and core tools | Pipes and redirection, quoting, globbing, exit codes, job control; `grep`, `sed`, `awk`, `find`, `xargs`, `sort`, `uniq`, `cut`, `tr`; permissions, umask, setuid; robust bash scripts (`set -euo pipefail`, traps) | To write | 3 |
| 02 | `02_processes_signals_proc_and_containers.md` | Processes, signals, `/proc` and containers | Process tree, `ps` and `top` fields, signals and default actions, `/proc/<pid>/` (maps, status, fd, stack), `ulimit` and rlimits, load average explained, cgroups v2, namespaces, capabilities, building a container with `unshare` | To write | 4 |
| 03 | `03_boot_systemd_and_logs.md` | Boot, systemd and logs | Firmware (UEFI) → bootloader → kernel → initramfs → systemd; units, targets, dependencies; `journalctl`, `dmesg`; kernel command line; module loading with `modprobe` and `udev` | To write | 7 |
| 04 | `04_debugging_gdb_sanitizers_strace.md` | Debugging | `gdb` (breakpoints, watchpoints, backtraces, threads, attaching), core dumps and `coredumpctl`, ASan, UBSan, TSan, Valgrind, `strace` and `ltrace`, `lsof`, debugging a hung process | To write | 8 |
| 05 | `05_performance_and_tracing_perf_ebpf.md` | Performance and tracing | The USE method, `vmstat`, `iostat`, `mpstat`, `pidstat`; `perf stat`, `perf record` and flame graphs; `ftrace` and `trace-cmd`; eBPF and `bpftrace` one-liners; network tools: `ip`, `ss`, `tcpdump`, `ethtool`, `iperf3` | To write | 9 |
| 06 | `06_linux_interview_question_bank.md` | Linux interview question bank | 50 questions with answers: commands, internals, debugging scenarios ("the disk is full but `du` disagrees", "the server is slow"), and performance triage | To write | 24 |

---

## 4. Labs

| Lab | Topic | What you do |
|---|---|---|
| `labs/log_analysis/` | 01 | Answer ten questions about a web-server log using only shell one-liners (top IPs, error rates per minute, slowest endpoints) |
| `labs/container_by_hand/` | 02 | Build a container with `unshare`, a new root filesystem with `pivot_root`, and a cgroup memory limit; watch the OOM killer act |
| `labs/broken_service/` | 03 | Diagnose and fix a deliberately broken systemd unit from its journal |
| `labs/debug_the_crash/` | 04 | Five small buggy programs (segfault, deadlock, leak, data race, fd leak); find each bug with the right tool |
| `labs/flame_graph/` | 05 | Profile a slow program with `perf`, build a flame graph, fix the hot spot, and measure the improvement |
| `labs/bpftrace_oneliners/` | 05 | Count syscalls per process, histogram `read` latency, trace TCP retransmits |

The Mac's own tools differ (BSD userland, no `/proc`, no `perf`); do every lab in the Linux VM from `99_toolchain/README.md`.

---

## 5. Representative interview questions

1. A process is using 100% CPU. How do you find out what it is doing, without restarting it?
2. The disk is full according to `df`, but `du` finds far less. What is going on?
3. What is load average, and why can it be high when CPU usage is low?
4. What happens when you press Ctrl-C in a terminal running a program? Ctrl-Z?
5. What is the difference between `SIGTERM`, `SIGKILL` and `SIGSTOP`? Which can be caught?
6. How does a container differ from a virtual machine? Which kernel features make containers work?
7. How would you find which process is listening on port 8080, and which files it has open?
8. Write a one-liner that prints the ten most common IP addresses in an access log.
9. A program crashes once a day in production. How do you capture enough information to fix it?
10. How would you measure the latency of a specific system call across the whole machine?
11. What does the OOM killer do, and how does it choose a victim?
12. A server has packet loss. Which tools and counters do you check, in what order?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md), with one change: the "practice" step is the lab, done at a terminal.
2. Type every command; do not copy-paste. Fluency is the point.
3. Keep a personal cheat sheet in this folder of every command you had to look up; it becomes your revision file.
4. Topic 06 is a question bank for revision in week 24, before interviews.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| *The Linux Programming Interface*, Michael Kerrisk | Processes, signals, `/proc`, and the system calls behind every tool |
| *Systems Performance* (2nd ed.) and brendangregg.com, Brendan Gregg | The USE method, `perf`, flame graphs, eBPF |
| *BPF Performance Tools*, Brendan Gregg | `bpftrace` and BCC in depth |
| *How Linux Works*, Brian Ward | Boot, systemd, devices, at the right level for revision |
| `man 7 signal`, `man 5 proc`, `man 7 namespaces`, `man 7 cgroups` | The primary sources |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–06 | All topics | 01 is next (week 3) |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
