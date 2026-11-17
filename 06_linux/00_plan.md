# Linux: Track Plan

> **Track code:** LX · **Weeks:** 3, 4, 6, 7, 9, 11, 12, 15 to 19, and 24 for the question bank · **Budget:** ~42 hours · **Prerequisites:** you use Linux daily; OS 02 before topic 03, OS 10 to 12 before topic 06, OS 15 and 16 before topic 07, NET 06 before topic 08, OS 20 before topic 09

You already know Linux as a user and developer. This track is revision with a purpose: turn daily familiarity into precise answers about how the system works, and into fluency with the shell, the diagnostic tools and the performance tools that interviewers expect a senior systems engineer to reach for without thinking.

The track was widened in November 2026 after a coverage audit (section 9): six topics became fourteen, because one tutorial on "processes, signals, `/proc` and containers" and one on "performance, tracing and network tools" cannot hold what the interview questions actually ask.

---

## 1. Why this track matters

"A process is hung, slow or leaking: how do you debug it?" is asked in system-software, infrastructure and networking loops alike, and the answer is a toolkit and a method: the 60-second triage, `/proc`, `strace`, `perf`, `tcpdump`, `ss`, `ip`, eBPF tracing, and how containers and namespaces isolate processes. SRE-flavoured questions add load average, memory pressure, file descriptors and "the disk is full but `du` disagrees". Network-appliance and GPU-infrastructure roles (Cisco, Palo Alto Networks, Nvidia) add CPU pinning, IRQ affinity, NUMA, huge pages and tuning for latency. Shell fluency also shows in every live environment: the "write a one-liner that..." question is a standard opener.

---

## 2. What you will be able to do after this track

1. Write robust shell scripts and explain how the shell parses, expands and executes a line (quoting, redirection, pipes, subshells, job control, traps).
2. Answer "what does this command do" and "write a one-liner that..." questions with `grep`, `sed`, `awk`, `find`, `xargs`, `sort` and `jq`.
3. Explain process states, signals, `/proc`, resource limits and load average, and find what a running process is doing without restarting it.
4. Describe the boot sequence from firmware to a login prompt, write and diagnose systemd units and timers, and read the journal.
5. Run the 60-second triage and the USE method on CPU, memory, disk and network, and name the saturated resource.
6. Read `free`, `/proc/meminfo`, `smaps` and the OOM killer's log; explain page cache, swap, overcommit and cgroup memory limits; use pressure stall information (PSI).
7. Diagnose full disks and inode exhaustion, deleted-but-open files, mount problems, and read `iostat -x`; know when to choose ext4, XFS, btrfs or tmpfs.
8. Use the network toolbox (`ip`, `ss`, `tcpdump`, `ethtool`, `nstat`, `iperf3`, `nft`, `tc`) to localise packet loss, retransmits and MTU problems, and build a small network from namespaces and `veth` pairs.
9. Build a container by hand (`unshare`, `pivot_root`, `overlayfs`, cgroups v2, `veth`), and explain how Docker, `runc` and Kubernetes limits map onto kernel features.
10. Find where CPU time goes with `perf` and flame graphs, and trace kernel and user events with `ftrace`, `bcc` tools and `bpftrace`.
11. Manage users, `sudo`, SSH (keys, agent, `ProxyJump`, tunnels, hardening), basic SELinux and AppArmor use, and firewall rules.
12. Tune a Linux host for latency or throughput (CPU affinity, `isolcpus`, IRQ affinity, NUMA, huge pages, governors) and say what each change costs.

Crash dumps and `gdb` are taught in the [debugging track](../16_debugging/00_plan.md); this track gives the system-level view and points to it.

---

## 3. Topics

Tutorials are numbered in study order. A tutorial that depends on an OS or networking tutorial comes after it in the schedule.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | [`01_shell_and_scripting.md`](01_shell_and_scripting.md) | The shell and robust scripting | How a line is processed (tokenising, the order of expansions, quoting rules, word splitting and globbing); redirection and file-descriptor duplication (`2>&1`, `>&3`, here-documents); pipes, subshells and what runs in which process; exit codes, `$?`, `PIPESTATUS`; job control (`&`, `fg`, `bg`, `disown`, `nohup`, `setsid`); functions, arrays, associative arrays, parameter expansion, `getopts`; `set -euo pipefail` and the cases where it misleads; `trap` and cleanup, `mktemp`, `flock`; `[[ ]]` against `[ ]`, POSIX `sh` against bash; `shellcheck` and the classic gotchas (unquoted variables, `cd` failures, `read` and backslashes, `for f in $(ls)`) | Written | 3 |
| 02 | [`02_text_processing_and_one_liners.md`](02_text_processing_and_one_liners.md) | Text processing and one-liners | Regular expressions (BRE, ERE, PCRE with `grep -P`); `grep`, `sed` (addresses, `s///`, hold space), `awk` as a language (fields, patterns, associative arrays, `BEGIN`/`END`); `cut`, `tr`, `sort` (`-k`, `-n`, `-u`, `-s`), `uniq -c`, `join`, `comm`, `paste`, `column`, `tee`; `find` (`-exec +`, `-print0` with `xargs -0`, `-newer`, `-size`), `xargs -P`; `jq` for JSON; process substitution; `LC_ALL=C` and pipeline speed; a worked bank of 30 one-liners over web-server, auth and syslog files (top IPs, errors per minute, slowest endpoints, session durations) | Written | 4 |
| 03 | [`03_processes_signals_and_proc.md`](03_processes_signals_and_proc.md) | Processes, signals and `/proc` | Process tree and `pstree`; states (`R`, `S`, `D`, `Z`, `T`) and what causes each; fields of `ps` and `top`; signals, default actions and the ones that cannot be caught; `kill`, `pkill`, `pgrep`, `killall`; Ctrl-C, Ctrl-Z, Ctrl-\ and process groups; `/proc/<pid>/` (`status`, `stat`, `maps`, `smaps`, `fd`, `limits`, `wchan`, `stack`, `cgroup`, `ns`), `/proc/sys` and `/sys`; `ulimit` and `prlimit`; load average explained and why it can be high with idle CPUs; `nice`, `ionice`, `taskset`, `chrt`; daemons, `nohup`, zombies and `SIGCHLD`; `lsof`, `fuser`, `ss -p` for "who has this port or file"; added in the review (November 2026): standard signals are not queued while real-time signals are, `EINTR` and `SA_RESTART`, what `fork` and `execve` keep (dispositions, mask, pending), `kill -0`, `SIGSTOP`/`SIGCONT`, orphans, reparenting and subreapers, PID 1 and signals (why a container's first process ignores `SIGTERM`), threads in `/proc/<pid>/task` and `ps -L`, `/proc/<pid>/io`, context-switch counters and `schedstat`, core dumps (`ulimit -c`, `core_pattern`), and a process in state `D` that you can create and inspect | Written | 6 |
| 04 | [`04_boot_systemd_and_logs.md`](04_boot_systemd_and_logs.md) | Boot, systemd and logs | UEFI, the bootloader, the kernel, the initramfs, systemd as PID 1; units (service, socket, timer, mount, path), targets and dependencies (`Wants`, `Requires`, `After`); `systemctl`, `systemd-analyze blame` and `critical-chain`; timers against cron; restart policy and watchdogs; resource control (`systemd-run`, slices, `MemoryMax`); sandboxing options in a unit file; `journalctl` filters, `dmesg`, `rsyslog`, `logrotate`; kernel command line, modules (`modprobe`, `udev`), `sysctl` persistence; recovery (rescue and emergency targets, a broken `fstab`, a lost root password) | Written | 7 |
| 05 | [`05_performance_methodology_and_triage.md`](05_performance_methodology_and_triage.md) | Performance methodology and the 60-second triage | The USE method with the Linux checklist for CPU, memory, storage and network; the RED method; the first ten commands (`uptime`, `dmesg -T`, `vmstat 1`, `mpstat -P ALL 1`, `pidstat 1`, `iostat -xz 1`, `free -m`, `sar -n DEV 1`, `sar -n TCP,ETCP 1`, `top`) and how to read each; run queue and saturation; PSI (`/proc/pressure/*`); latency against throughput, percentiles, coordinated omission; active benchmarking and the common ways a benchmark lies; `sar` history; a decision tree from symptom to resource | Written | 9 |
| 06 | [`06_memory_swap_and_the_oom_killer.md`](06_memory_swap_and_the_oom_killer.md) | Memory, swap and the OOM killer in practice | `free` and every field of `/proc/meminfo`; page cache, buffers, anonymous, slab and shared memory; RSS, PSS, USS and VSZ (`pmap`, `smem`, `smaps_rollup`); swap and `swappiness`; overcommit modes; the OOM killer (`oom_score`, `oom_score_adj`, reading the log, cgroup OOM); `vm.*` sysctls and why `drop_caches` is not a fix; huge pages and THP; NUMA (`numactl`, `numastat`); cgroup v2 `memory.max`, `memory.high`, `memory.events`, PSI for memory; finding a leak from `/proc/<pid>/smaps` and `pmap` diffs, and `slabtop` for kernel leaks | Written | 11 |
| 07 | [`07_files_permissions_and_storage.md`](07_files_permissions_and_storage.md) | Files, permissions and storage in practice | "The disk is full but `du` disagrees": `df -i`, deleted files held open (`lsof +L1`), sparse files, bind mounts hiding data; `mount`, `findmnt`, `fstab`, `lsblk`, `blkid`, loop devices; ext4, XFS, btrfs, tmpfs and overlayfs and when to pick each; `fsck`, `tune2fs`, `xfs_repair`; LVM and `mdadm` basics; permissions, `umask`, ACLs, `chattr`, extended attributes; `iostat -x` fields (`await`, `aqu-sz`, `%util` and why it misleads on SSDs); `fio` for measuring; `smartctl`, `blktrace`; quotas | Written | 12 |
| 08 | [`08_linux_networking_toolbox.md`](08_linux_networking_toolbox.md) | The Linux networking toolbox | `ip` (`addr`, `link`, `route`, `rule`, `neigh`, `netns`), `ss` filters and fields, `tcpdump` and BPF filters, `ethtool` (`-S`, `-k`, `-g`), `nstat` and `/proc/net/*`, `mtr`, `iperf3`, `dig` and `resolvectl`; `nft` and `iptables`, `conntrack`; `veth`, bridges, VLANs, bonding; `tc` and `netem` for injecting loss and delay; `sysctl net.*`; a packet-loss triage order (NIC counters, driver, qdisc, socket queues, retransmits, MTU and PMTUD); network namespaces for building a lab on one machine | Written | 15 |
| 09 | [`09_containers_namespaces_and_cgroups.md`](09_containers_namespaces_and_cgroups.md) | Containers from first principles | The eight namespaces (`mount`, `UTS`, `IPC`, `PID`, `network`, `user`, `cgroup`, `time`) and `clone`, `unshare`, `setns`; cgroups v2 (the unified hierarchy, controllers `cpu`, `memory`, `io`, `pids`, `cpuset`, the no-internal-process rule, delegation, `cpu.max` throttling); `pivot_root` and a root filesystem; `overlayfs` layers; images, OCI, `runc` and `containerd`; container networking (`veth` and a bridge, port mapping); capabilities, `seccomp`, user namespaces and rootless containers; containers against VMs; how Kubernetes requests and limits become cgroup settings (CPU throttling, `OOMKilled`); `nsenter`, `lsns`, `systemd-run` | Written | 15 |
| 10 | [`10_perf_and_flame_graphs.md`](10_perf_and_flame_graphs.md) | `perf` and flame graphs | `perf stat` and the PMU (cycles, instructions, IPC, cache and branch misses); `perf record`, `report`, `annotate`, `top`; sampling frequency and overhead; stacks (frame pointers, DWARF, LBR) and symbol resolution; reading a flame graph and a differential flame graph; off-CPU analysis; `perf trace`, `perf sched`, `perf mem` and `perf c2c` for false sharing; what works inside a VM and what does not; turning a profile into a fix and measuring the improvement | Written | 16 |
| 11 | [`11_ftrace_ebpf_and_bpftrace.md`](11_ftrace_ebpf_and_bpftrace.md) | `ftrace`, eBPF and `bpftrace` | Tracepoints, kprobes, uprobes and USDT; `ftrace` (`function`, `function_graph`, `trace-cmd`, `tracefs`); eBPF in one picture (programs, maps, the verifier, CO-RE and `libbpf`); the `bcc` tools worth knowing (`execsnoop`, `opensnoop`, `biolatency`, `runqlat`, `tcpconnect`, `tcpretrans`, `ext4slower`, `profile`, `offcputime`); `bpftrace` one-liners and short scripts (counts, histograms, stacks); cost and safety of tracing in production | Written | 17 |
| 12 | [`12_users_ssh_and_hardening.md`](12_users_ssh_and_hardening.md) | Users, SSH and hardening | Users, groups, NSS, `/etc/passwd` and `shadow`; `sudo` and `sudoers`; PAM in outline; SSH keys, the agent, `~/.ssh/config`, `ProxyJump`, local, remote and dynamic forwarding, `ControlMaster`, host keys, and a hardened `sshd_config`; SELinux and AppArmor from the administrator's side (contexts, booleans, reading an AVC denial); `setcap` and file capabilities; basic firewall policy with `nft`; `auditd`; package management (`apt`, `dnf`, `rpm`) and signed repositories; time sync | Written | 18 |
| 13 | `13_tuning_for_latency_and_throughput.md` | Tuning for latency and throughput | CPU affinity and `cpuset`; `isolcpus` and `nohz_full`; IRQ affinity, RSS, RPS and XPS; NUMA placement; frequency governors and C-states; huge pages against TLB misses; real-time priorities (`SCHED_FIFO`) and an introduction to `PREEMPT_RT`; `tuned` profiles; `sysctl` for the network and VM; NIC ring sizes and offloads; measuring jitter; what each change costs and how to prove it helped | To write | 19 |
| 14 | `14_linux_interview_question_bank.md` | Linux interview question bank | 80 questions with answers across shell, processes, memory, storage, networking, containers, performance and security; 15 troubleshooting scenarios worked end to end ("the server is slow", "the disk is full but `du` disagrees", "a process hangs", "packets are dropped", "a container is `OOMKilled`"); a one-page triage decision tree | To write | 24 |

Debugging (`gdb`, core dumps, sanitizers, `rr`) is the separate [`16_debugging`](../16_debugging/00_plan.md) track (DBG 01 to 10); it replaced the old LX 04.

---

## Where related topics live

| Subject | Where |
|---|---|
| `gdb`, core dumps and symbols, sanitizers, Valgrind, `rr`, `strace` as a debugging technique | [Debugging track](../16_debugging/00_plan.md), especially DBG 07 for live processes |
| Scheduler, virtual memory, file systems, page cache, IPC, protection (the theory behind topics 03, 06, 07 and 09) | [OS track](../05_operating_systems/00_plan.md) |
| The network stack, TCP, `epoll`, firewalls and IPS (the theory behind topic 08) | [NET 03, NET 06, NET 09](../07_networking/00_plan.md) |
| Kernel debugging (`kprobes`, `lockdep`, `kdump`, `crash`), kernel modules | [K 08](../08_kernel/00_plan.md) |
| ELF, linking, `ldd` and `LD_PRELOAD`, `nm`, `readelf` | [C 06](../02_c_language/00_plan.md) |
| Hardware support for virtualisation, VMs against containers against microVMs | [CO 27](../04_computer_organization/00_plan.md) |
| Disks, SSDs and the FTL, RAID, ECC | [CO 17](../04_computer_organization/00_plan.md) |
| Side channels, Spectre and Meltdown, secure boot | [CO 30](../04_computer_organization/00_plan.md) |
| Embedded Linux (device tree, bootloaders, Yocto and Buildroot) | [EMB track](../13_embedded/00_plan.md) |
| Out of scope on purpose | Configuration management (Ansible, Puppet), Terraform, Kubernetes internals beyond the cgroup mapping, distribution certification syllabi, editors |

---

## 4. Labs

All labs run in the Linux VM from [`../99_toolchain/README.md`](../99_toolchain/README.md); the Mac has BSD userland, no `/proc` and no `perf`.

| Lab | Topic | What you do |
|---|---|---|
| `labs/shell_toolkit/` | 01, 02 | Answer ten questions about a web-server log using only shell one-liners (top IPs, error rate per minute, slowest endpoints); then take a fragile script, run `shellcheck`, and make it safe with quoting, `trap` and `flock` |
| `labs/proc_explorer/` | 03 | Write a small tool that prints state, threads, open files and memory of a PID from `/proc` and compare it with `ps` and `top`; create a zombie and a process in `D` state on purpose |
| `labs/broken_service/` | 04 | Diagnose and fix a deliberately broken systemd unit and a broken timer from the journal |
| `labs/triage_drill/` | 05 | Three prepared faults (CPU hog, disk saturation with `fio`, packet loss with `netem`); run the 60-second checklist and name the saturated resource before looking at the cause |
| `labs/oom_lab/` | 06 | Put a leaking program in a cgroup with `memory.max` and then `memory.high`; watch `memory.events` and PSI; read the OOM log; change `oom_score_adj` and see who dies |
| `labs/disk_full/` | 07 | Reproduce "full but `du` disagrees" (a deleted open file), inode exhaustion, and a mount that hides data; build an ext4 and an XFS file system on loop devices and compare behaviour |
| `labs/netns_lab/` | 08 | Two namespaces joined by a `veth` pair and a bridge; add NAT with `nft`, inject loss with `netem`, find it with `tcpdump`, `ss` and `nstat` |
| `labs/container_by_hand/` | 09 | Build a container with `unshare`, a new root with `pivot_root` and `overlayfs`, a cgroup memory and CPU limit, and a `veth` network; then drop capabilities and add a `seccomp` filter |
| `labs/flame_graph/` | 10 | Profile a slow program with `perf`, build a flame graph, fix the hot spot, and measure the improvement; find false sharing with `perf c2c` |
| `labs/bpftrace_oneliners/` | 11 | Count syscalls per process, histogram `read` latency, trace TCP retransmits, find who opens a file |
| `labs/ssh_tunnels/` | 12 | `ProxyJump` through two hosts, local and remote forwarding, a `ControlMaster` socket, and a hardened `sshd` in a VM |
| `labs/latency_tuning/` | 13 | Pin a busy loop to an isolated CPU, move IRQs away, measure jitter before and after, and test huge pages against TLB misses |

---

## 5. Representative interview questions

1. A process is using 100% CPU. How do you find out what it is doing, without restarting it?
2. The disk is full according to `df`, but `du` finds far less. What is going on? What if `df -i` shows 100%?
3. What is load average, and why can it be high when CPU usage is low?
4. What happens when you press Ctrl-C in a terminal running a program? Ctrl-Z?
5. What is the difference between `SIGTERM`, `SIGKILL` and `SIGSTOP`? Which can be caught?
6. How does a container differ from a virtual machine? Which kernel features make containers work?
7. How would you find which process is listening on port 8080, and which files it has open?
8. Write a one-liner that prints the ten most common IP addresses in an access log.
9. A program crashes once a day in production. How do you capture enough information to fix it?
10. How would you measure the latency of a specific system call across the whole machine?
11. What does the OOM killer do, and how does it choose a victim? How does a cgroup OOM differ from a system OOM?
12. A server has packet loss. Which tools and counters do you check, in what order?
13. You log in to a slow server. What are the first ten commands you run, and what does each tell you?
14. What is the difference between RSS, PSS and VSZ? Why can the sum of RSS over all processes exceed physical memory?
15. A container keeps being `OOMKilled` although the node has free memory. Why?
16. What does `iostat -x` show, and why can `%util` mislead on an SSD?
17. Explain what happens between pressing the power button and a login prompt.
18. How do you securely reach a database that is only visible from a bastion host? What does `ProxyJump` do?
19. A process is in state `D` and will not die. What is it doing, and what can you do?
20. How would you make a latency-sensitive thread run without interruption on a server?
21. What does `set -euo pipefail` do, and when does it not protect you?
22. A systemd service restarts in a loop. How do you find out why?
23. Two containers on one host cannot reach each other. How do you debug it?
24. Why is a CPU flame graph not enough for a program that is slow but not busy?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md), with one change: the "practice" step is the lab, done at a terminal.
2. Type every command; do not copy-paste. Fluency is the point.
3. For every tool, learn the question it answers and the number that tells you the answer, not its flags. "What is the saturation of this resource?" is the interview question; `-xz` is the detail.
4. Keep a personal cheat sheet in this folder of every command you had to look up; it becomes your revision file.
5. Practise the 60-second triage on the lab faults until you can say the resource before you say the cause.
6. Topic 14 is a question bank for revision in week 24, before interviews.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| [*The Linux Programming Interface*, Michael Kerrisk](https://man7.org/tlpi/) | Processes, signals, `/proc`, and the system calls behind every tool (1,552 pages; the page offers the chapter list, sample chapters and the example source code) |
| [Brendan Gregg's Linux performance page](https://www.brendangregg.com/linuxperf.html) | The tool diagrams, the USE method, checklists, flame graphs, `perf`, eBPF, and the list of his talks (the page links the videos) |
| [Linux USE method checklist](https://www.brendangregg.com/USEmethod/use-linux.html) | Utilisation, saturation and errors for CPU, memory, storage and network, with the command for each |
| [Performance checklists for SREs (SREcon 2016)](https://www.brendangregg.com/blog/2016-05-04/srecon2016-perf-checklists-for-sres.html) | The ten-command 60-second triage and the disk, network and CPU checklists |
| *Systems Performance* (2nd ed.) and *BPF Performance Tools*, Brendan Gregg | Methods and tools in depth; the second has over 100 eBPF tools |
| [cgroup v2 documentation](https://docs.kernel.org/admin-guide/cgroup-v2.html) and [`cgroups(7)`](https://man7.org/linux/man-pages/man7/cgroups.7.html) | Controllers, `memory.max`, `memory.high`, `cpu.max`, delegation, the no-internal-process rule |
| [`namespaces(7)`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | The eight namespace types, `clone`, `unshare`, `setns` |
| [PSI: pressure stall information](https://docs.kernel.org/accounting/psi.html) | `/proc/pressure/*`, `some` and `full`, triggers, per-cgroup pressure |
| [ftrace documentation](https://docs.kernel.org/trace/ftrace.html) | The tracers, `tracefs`, filters and events |
| [`bcc` tools](https://github.com/iovisor/bcc) and [`bpftrace`](https://github.com/bpftrace/bpftrace) | The tool catalogue (`execsnoop`, `biolatency`, `runqlat`, `tcpretrans`, and many more), the reference guide, and `bpftrace` tutorials and labs |
| [Linux tracing workshop (Sasha Goldshtein)](https://github.com/goldshtn/linux-tracing-workshop) | 25 hands-on labs: `ftrace`, `perf`, `bcc`, writing BPF tools (the Node.js labs can be skipped) |
| [The Missing Semester of Your CS Education (MIT)](https://missing.csail.mit.edu/) | Shell, command-line environment, debugging and profiling; lectures on YouTube from the page |
| [Containers from Scratch, Liz Rice (GOTO Amsterdam 2018)](https://gotopia.tech/sessions/429/containers-from-scratch) | Namespaces, `chroot` and cgroups built live in Go; the talk to watch before the container lab |
| [Container learning path (iximiuz)](https://iximiuz.com/en/posts/container-learning-path/) | The order: namespaces and cgroups, images, runtimes, orchestration, OCI |
| [Julia Evans's zines](https://wizardzines.com/) | Short, accurate pictures: *Bite Size Linux*, *How Containers Work*, and the free *strace*, `tcpdump` and `perf` zines |
| *How Linux Works*, Brian Ward | Boot, systemd, devices, networking, at the right level for revision |
| `man 7 signal`, `man 5 proc`, `man 7 capabilities`, `man 8 ip`, `man 8 ss` | The primary sources |
| [`signal(7)`](https://man7.org/linux/man-pages/man7/signal.7.html) and [`pid_namespaces(7)`](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html) | Uncatchable signals, standard against real-time queueing, `EINTR` and `SA_RESTART`, inheritance across `fork` and `execve`; signals to PID 1, orphan adoption, subreapers |
| [Linux load averages: solving the mystery (Brendan Gregg)](https://www.brendangregg.com/blog/2017-08-08/linux-load-averages.html) | Why Linux load counts uninterruptible tasks (since 1993), the damped 1, 5, 15 minute averages, and better saturation metrics |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01 | Shell and scripting | Written |
| 02 | Text processing and one-liners | Written |
| 03 | Processes, signals and `/proc` | Written |
| 04 | Boot, systemd and logs | Written |
| 05 | Performance methodology and triage | Written |
| 06 | Memory, swap and the OOM killer | Written |
| 07 | Files, permissions and storage | Written |
| 08 | The Linux networking toolbox | Written |
| 09 | Containers from first principles | Written |
| 10 | `perf` and flame graphs | Written |
| 11 | `ftrace`, eBPF and `bpftrace` | Written |
| 12 | Users, SSH and hardening | Written |
| 13 | Tuning for latency and throughput | To write |
| 14 | Question bank | To write (week 24) |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).

---

## 9. Coverage audit (November 2026)

The plan was checked against: Brendan Gregg's Linux performance page, USE-method Linux checklist and SREcon checklists, the Linux kernel documentation for cgroup v2, PSI and `ftrace`, the `cgroups(7)` and `namespaces(7)` manual pages, the `bcc` and `bpftrace` repositories, Goldshtn's Linux tracing workshop, the table of contents of *The Linux Programming Interface*, MIT's Missing Semester syllabus, the iximiuz container learning path, and published Linux and SRE interview-question lists (the Linux Foundation's article and a Linux troubleshooting article for SRE interviews). I read the pages named here; other interview-question sites that appeared in search results were not read. What changed:

| Gap found | Fix |
|---|---|
| One tutorial held shell mechanics, scripting and the text tools; the "one-liner" question deserves its own drill | Split into LX 01 (shell and scripting) and LX 02 (text processing, with a bank of 30 one-liners) |
| One tutorial held processes, signals, `/proc`, limits, load average, cgroups, namespaces, capabilities and a container build | Split into LX 03 (processes, signals, `/proc`) and LX 09 (containers); cgroup v2 detail, `cpu.max` throttling, delegation and the Kubernetes mapping added |
| Memory in practice (`/proc/meminfo`, RSS against PSS, swap, the OOM log, cgroup OOM, PSI) had no home, though "OOM killer" and "free memory vs `free`" are standard questions | New LX 06 |
| Storage in practice ("full but `du` disagrees", inodes, deleted open files, mounts, LVM, `iostat -x`, file-system choice) appeared only as a question | New LX 07 |
| The network tools were a tail on the performance tutorial; `netns`, `veth`, `nft`, `conntrack`, `tc netem` and a packet-loss triage order were missing | New LX 08 |
| Performance, `perf`, flame graphs, `ftrace`, eBPF and `bpftrace` were one tutorial | Split into LX 05 (method and 60-second triage, with PSI), LX 10 (`perf`) and LX 11 (`ftrace`, eBPF, `bpftrace`) |
| Users, `sudo`, SSH tunnelling and hardening, SELinux and AppArmor use, firewalls and package management were missing | New LX 12 |
| Tuning for latency (affinity, `isolcpus`, IRQ affinity, NUMA, governors, huge pages, `PREEMPT_RT`) was missing, though it matters for network-appliance and GPU-infrastructure roles | New LX 13 |
| `systemd` timers, resource control and sandboxing, logrotate, `auditd` and recovery procedures were missing from boot | Added to LX 04 and LX 12 |
| The question bank had 50 questions and no worked scenarios | LX 14: 80 questions and 15 end-to-end scenarios |
| Debugging, ELF tools, kernel debugging, VMs and storage hardware | Already covered elsewhere (table above) |

Cross-track references updated at the same time: the debugging plan (performance debugging now points to LX 05, LX 10 and LX 11), the OS plan (containers moved to LX 09), the C plan (signals and `/proc` is LX 03), the project specs (`perf` and `bpftrace` are LX 10 and LX 11), the interviews plan (the Linux bank is LX 14) and the master plan (topic count, hours, weeks).
