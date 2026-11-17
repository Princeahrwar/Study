# LX 11: `ftrace`, eBPF and `bpftrace`

*Part of the [Linux plan](00_plan.md) · Time: about 7 hours · You need: [LX 05](05_performance_methodology_and_triage.md) (the triage that says which resource is the problem), [LX 10](10_perf_and_flame_graphs.md) (sampling profilers and `perf trace`), [LX 03](03_processes_signals_and_proc.md) (`/proc`, system calls) and the [kernel track](../08_kernel/00_plan.md) background on what a tracepoint and a scheduler event are. LX 10 answers "where does the CPU time go". This tutorial answers the questions a profiler cannot: **what exactly happened, in what order, how long did each one take, and who did it**, while the program keeps running.*

A profiler samples. A tracer **sees every event**: every `openat`, every context switch, every call of one function. That makes tracing the right tool for latency (one slow request among a million), for "who is opening this file", for "why did this process wake up" and for outliers that a sample would miss. It also costs per event, which is why the second half of the skill is knowing the price (section 9) and keeping the output small.

Linux has one event source and three ways to consume it. The **sources** are the **tracepoints** compiled into the kernel, the **kprobes** and **uprobes** that put a breakpoint on any kernel or user instruction, and the **USDT** markers that applications ship. The **consumers** are **`ftrace`** (the kernel's built in ring buffer, controlled through files), **`perf`** (LX 10) and **eBPF** (small verified programs that run in the kernel at the event and keep only a count or a histogram). `bpftrace` and the `bcc` and `libbpf` tools are front ends for eBPF. The tutorial goes in that order: the landscape (section 1), `ftrace` and `tracefs` (2), the probe types (3), eBPF itself and its safety checker (4), `bpftrace` (5), ready made tools (6), the method (7), a worked investigation (8), then costs, mistakes, questions and problems.

**Nine honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, an Intel Xeon at 2.10 GHz), as **root**, with **`bpftrace` 0.20.2**, **`libbpf` 1.3 and `bpftool` 7.4** (the Ubuntu 24.04 packages, newer kernel than userland), **`libbpf-tools` and `bcc` 0.29.1**, **`trace-cmd` 3.2** and **`clang` 18**. The tools were installed with `apt` for this tutorial.
2. **The kernel was built without `kprobes`** (`CONFIG_KPROBES` is not set, section 2), **without `fentry` and `fprobe` support, without the function graph tracer and without histogram triggers**. So **no probe on a kernel function** can be attached here: `kprobe:vfs_read` fails, `bpftrace -l 'kprobe:*'` lists nothing, and **23 of the 54 `libbpf-tools` programs fail for exactly this reason** (section 6). **Tracepoints, uprobes, software events and profile sampling work**, and that is what every lab uses.
3. **The `ftrace` function tracer is refused.** `function` is listed in `available_tracers`, but writing it to `current_tracer` returns **`Operation not permitted`**, as does `set_ftrace_filter`, and **`trace-cmd record` stops with the same error** (section 2). The function tracer, `function_graph`, `trace-cmd`'s main use and the latency tracers are therefore **explained from memory and marked so**. The event half of `ftrace` (events, filters, `trace_marker`, uprobes) was run.
4. **The `bcc` Python tools cannot run**: `bcc` compiles its C at start up against kernel headers, and this VM has none (`Unable to find kernel headers`). The **`libbpf-tools` rewrites** (CO-RE, compiled ahead of time) were used instead; the tool names are the same and section 6 explains the difference.
5. **There is no hardware performance counter (PMU)** (LX 10), so `bpftrace`'s `hardware:` probes and `perf_event` based tools such as `llcstat` do not run.
6. **Timing noise is large** (LX 10 measured up to about 60 percent). Costs are quoted as **ranges from five runs** and only large ratios are trusted.
7. The programs are small C programs (a request loop `target`, a syscall loop `sysload`, a function call loop `ucall`) chosen so that the answer is known. Production questions are messier; the method transfers, the numbers do not.
8. The `bpftrace` and `libbpf` programs in section 4 and 5 are mine and short; they are tutorial scale, not tested tools. Use the maintained tools of section 6 for real work.
9. Single machine, single day. The **numbers do not transfer** to your hardware; the **ratios and shapes** do.

By the end you can:

- say what a **tracepoint, a kprobe, a uprobe and a USDT probe** are and when each is the right one (measured here: **2,305 tracepoints**, **0 kprobes**, a **uprobe on a user function cost about 27 µs per call** against 2 to 3 ns for the function, and a **tracepoint counter added roughly 0 to 100 ns per system call**);
- drive **`ftrace` through `tracefs`** with nothing but `echo` and `cat`: enable events, filter them in the kernel, create a **uprobe at run time**, write your own markers (measured: a string filter on a user pointer silently matched **0 events** until it was written `filename.ustring`, which matched **2**);
- explain **eBPF** in one picture: program, **hook**, **map**, **helper**, **verifier**, **JIT** and **CO-RE**, and build one from source (measured: a **512 byte** program became **286 bytes** of x86, and the kernel counted its cost at **about 2 µs per run**);
- read a **verifier rejection** and fix the cause (measured: four messages: **`map_value_or_null`**, **`scalar`**, **`outside of the allowed memory range`** and **`1000001 insn`**, and one case that was accepted because the key was a constant);
- write **`bpftrace` one liners and short scripts** for counts, histograms, latency, stacks and user functions (measured: **42 `openat` calls** counted for a 40 request loop, a read latency histogram with **most reads under 1 µs**, a `do_lookup` latency of **43 µs on average**);
- use the **`libbpf-tools`** programs and know which work on a given kernel (measured: **28 of 54** ran, **23 failed for lack of kprobes**, **3 for other reasons**);
- find **scheduler delay**, **blocking time** and **system call latency** with a tool each (measured: run queue latency grew from **under 64 µs** to **2 to 8 ms** for most wake ups when four busy loops shared two CPUs; **938 ms of a 1 s window** was off CPU in `clock_nanosleep`);
- state the **cost and risk** of tracing and keep it safe in production (measured: **entry plus exit latency probes added 230 to 360 ns** to a 100 ns system call, `strace` made it **about 500 times slower**, and a `printf` per event **lost about two thirds of 300,000 events without any warning**).

**Where the numbers come from.** Twelve experiment scripts (`x1` to `x12`) and a problem answer script (`ans_prob`) were run in this session on the VM described above. Transcripts print `$ command` before each command's output when the command is shown. In transcripts, `target` is the request loop of section 1 (`target N` runs N requests, each one parses, opens `/etc/hostname`, reads it and sleeps 2 ms; `target N W` multiplies the compute work by W), `sysload` is a loop of N `getppid` calls and `ucall` a loop of N calls to one small function. Histogram bars were redrawn with `#` instead of `@` for the page. Numbers not from these runs are marked "from memory" or come from a source at the end.

**Data and timing.** No data sets: the programs generate their own work. Each experiment takes seconds to a few minutes (the tool sweep of section 6 takes about three). All timings are repeated runs on a noisy VM and carry the noise stated above.

---

## 1. The landscape: where events come from, and who reads them

### 1.1 Event sources

| Source | What it is | Stable? | Cost when off | Here |
| --- | --- | --- | --- | --- |
| **Tracepoint** | A named hook that kernel developers placed in the code (`sched:sched_switch`, `syscalls:sys_enter_openat`, `tcp:tcp_retransmit_skb`), with a fixed set of fields | Yes, treated as an interface | A patched out `nop`; near zero | **2,305** |
| **Raw tracepoint** | The same hook, delivered with the raw arguments and without the formatting step | Yes | same | **1,585** |
| **kprobe** and **kretprobe** | A breakpoint on almost any kernel function or instruction, at entry or return | No: function names and arguments change between kernels | none until placed | **0** (not built) |
| **fentry** and **fexit** | A faster, trampoline based way to hook a kernel function entry or exit (BPF only) | No | none | **not available** |
| **uprobe** and **uretprobe** | A breakpoint (`int3`) in a user program or library, at a function or an offset | No: tied to the binary | none until placed | works |
| **USDT** | A user space tracepoint: a `nop` that an application or a library (a database, the language runtime) ships, with documented arguments | Yes, by the application | a `nop` | needs a binary that has them (not used) |
| **Software and hardware events** | Timers, page faults, context switches, CPU counters | Yes | none | **14 software**, **12 hardware names listed but no PMU** |

The rule of thumb is **use a tracepoint if one exists, a USDT probe if the application has one, a uprobe for your own code, and a kprobe or `fentry` only when nothing else shows the thing you need**. Tracepoints are cheaper and survive kernel upgrades; kprobes break when a function is renamed or inlined. **This VM forces the first rule**: it has no kprobes, so everything in this tutorial uses tracepoints, uprobes and sampling.

### 1.2 Event consumers

| Consumer | How you use it | Strength | Weakness |
| --- | --- | --- | --- |
| **`ftrace` (`tracefs`)** | `echo` and `cat` on files under `/sys/kernel/tracing`; the front end is `trace-cmd` | Always there, no extra software, exact timestamps, good for function level tracing of the kernel and for short ordered sequences | Text buffer, little processing in the kernel, no aggregation (histogram triggers were not built here) |
| **`perf`** | `perf record`, `perf trace`, `perf probe` | Samples, counters, stacks, the same events | LX 10 |
| **eBPF** | `bpftrace`, `bcc` and `libbpf` tools, your own programs | **Aggregates in the kernel** (counts, histograms, latency), so only the answer crosses to user space; programmable | Needs privileges, a new enough kernel, and a program that the verifier accepts |

The reason eBPF replaced most uses of the other two is in the last column of the first line of this table: `ftrace` and `perf trace` **ship every event to user space** and let a tool count them there, while an eBPF program **counts inside the kernel** and ships a table. Section 9 measures the difference.

### 1.3 The test program

Every lab traces one small program, so the answers can be checked against the source:

```c
/* target.c: a tiny request loop with named functions, so that probes have something to attach to */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

__attribute__((noinline)) int do_lookup(int key) {
    char buf[256];
    int fd = open("/etc/hostname", O_RDONLY);
    if (fd < 0) return -1;
    int n = read(fd, buf, sizeof buf);
    close(fd);
    return n + key;
}
static int work = 1;
__attribute__((noinline)) int parse_request(int i) {
    volatile int x = 0;
    for (int k = 0; k < 20000 * work * (1 + i % 4); k++) x += k;
    return do_lookup(i) + (x & 1);
}
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 100;
    int sum = 0;
    if (argc > 2) work = atoi(argv[2]);
    for (int i = 0; i < n; i++) {
        sum += parse_request(i);
        usleep(2000);
    }
    printf("done %d\n", sum != 0);
    return 0;
}
```

Each request runs `parse_request`, which burns CPU for a time that depends on the request number, then calls `do_lookup`, which makes three system calls (`openat`, `read`, `close`) on `/etc/hostname`, then sleeps 2 ms. So a run of N requests must make **N `openat`, N `read`, N `close` and N `clock_nanosleep` calls**, plus a fixed number from the dynamic loader. Section 5 checks that.

---

## 2. `ftrace` and `tracefs`: tracing with `echo` and `cat`

`ftrace` is a tracing framework **inside the kernel**. Its control surface is a file system, `tracefs`, normally mounted at `/sys/kernel/tracing` (older systems: `/sys/kernel/debug/tracing`). Nothing to install, and every other tool (`trace-cmd`, `perf`) writes to the same files. Read the first transcript as a tour of the directory.

### 2.1 What is there, and what this kernel left out

First the build options that decide what is possible:

```text
--- 1. kernel build options that decide what can be traced (from /proc/config.gz)
CONFIG_FTRACE                  y
CONFIG_FUNCTION_TRACER         y
CONFIG_DYNAMIC_FTRACE          y
CONFIG_FUNCTION_GRAPH_TRACER   not set
CONFIG_FTRACE_SYSCALLS         y
CONFIG_KPROBES                 not set
CONFIG_UPROBES                 y
CONFIG_FPROBE                  not set
CONFIG_KPROBE_EVENTS           absent
CONFIG_UPROBE_EVENTS           y
CONFIG_HIST_TRIGGERS           not set
CONFIG_SYNTH_EVENTS            not set
CONFIG_USER_EVENTS             not set
CONFIG_BPF_SYSCALL             y
CONFIG_BPF_JIT                 y
CONFIG_BPF_EVENTS              y
CONFIG_DEBUG_INFO_BTF          y
CONFIG_CGROUP_BPF              y
CONFIG_BPF_LSM                 y
```

The file is the kernel's own configuration (`/proc/config.gz`). Read it as a capability list. **Event tracing, `uprobes`, the BPF system call, the JIT and kernel type information (BTF) are all on**. **`kprobes` and the function graph tracer are off**, and so are **histogram triggers**, **synthetic events** and **user events**. (From memory: a distribution kernel normally enables all of these; this one is a trimmed virtual machine kernel.)

Now the directory itself, and the first refusal:

```text
--- 1. tracefs: a directory of control files (no tool needed)
$ mount | grep -c 'tracefs'; ls /sys/kernel/tracing | wc -l; cat /sys/kernel/tracing/available_tracers; cat /sys/kernel/tracing/current_tracer
1
43
blk function nop
nop
$ ls /sys/kernel/tracing/events | wc -l; ls /sys/kernel/tracing/events/syscalls | wc -l; cat /sys/kernel/tracing/buffer_size_kb | head -1; cat /sys/kernel/tracing/tracing_on
105
722
1410
1
```

Four facts matter. `tracefs` is one mount and **43 control files** at the top. **105 event groups** are under `events/`; the `syscalls` group alone holds a pair of events (`sys_enter_*` and `sys_exit_*`) for every system call, **722 entries** counting its two control files. The **buffer is 1,410 KB per CPU** (so 2,820 KB on this 2 CPU VM) and `tracing_on` is a master switch. And `available_tracers` lists **`function`**, but the next transcript shows that it cannot be selected.

```text
--- 2. the function tracer: refused on this VM
$ echo function > /sys/kernel/tracing/current_tracer; cat /sys/kernel/tracing/current_tracer
bash: echo: write error: Operation not permitted
nop
$ cat /sys/kernel/tracing/set_ftrace_filter 2>&1 | head -2; cat /sys/kernel/tracing/available_filter_functions 2>&1 | head -2
cat: /sys/kernel/tracing/set_ftrace_filter: Operation not permitted
cat: /sys/kernel/tracing/available_filter_functions: Operation not permitted
```

**`Operation not permitted`** on `current_tracer`, `set_ftrace_filter` and `available_filter_functions`: the sandbox that runs this VM refuses the part of `ftrace` that patches kernel functions. Everything below uses the **event** interface, which is allowed.

**The function tracer, from memory** (not run here). With `function` selected, every kernel function that the filter allows writes one line to the buffer at its entry: task, CPU, timestamp, function and caller. `set_ftrace_filter` limits it to functions that match a pattern (`echo 'tcp_*' > set_ftrace_filter`), `set_ftrace_pid` to one process, and `function_graph` (needs the option this kernel lacks) adds the **return time of each function and indents callees**, which is the quickest way to answer "what does this system call do inside the kernel and where does it spend its time". It is also the tracer that can slow a busy machine to a crawl, because it records every call of every matched function. `trace-cmd record -p function_graph -g do_sys_openat2 CMD` wraps all of it.

### 2.2 One event: enable it, run something, read the buffer

An **event** is a tracepoint made visible in `tracefs`. Its `format` file says which fields it records, and writing 1 to `enable` turns it on:

```text
--- 3. one event: enable it, run something, read the buffer
$ cat /sys/kernel/tracing/events/sched/sched_process_exec/format | grep -E 'name:|field:' | head -6
name: sched_process_exec
        field:unsigned short common_type;       offset:0;       size:2; signed:0;
        field:unsigned char common_flags;       offset:2;       size:1; signed:0;
        field:unsigned char common_preempt_count;       offset:3;       size:1; signed:0;
        field:int common_pid;   offset:4;       size:4; signed:1;
        field:__data_loc char[] filename;       offset:8;       size:4; signed:0;
$ grep -v '^#' /sys/kernel/tracing/trace | head -4 | sed -E 's/^ +//; s/ +/ /g' | cut -c1-110
true-1106 [001] ..... 223.051163: sched_process_exec: filename=/bin/true pid=1106 old_pid=1106
ls-1107 [001] ..... 223.052346: sched_process_exec: filename=/bin/ls pid=1107 old_pid=1107
echo-1108 [001] ..... 223.053701: sched_process_exec: filename=/bin/echo pid=1108 old_pid=1108
```

`sched_process_exec` fires when a process calls `execve`. The transcript shows the common fields every event carries (`common_pid`, flags, preempt count) and then the event's own (`filename`, `pid`, `old_pid`), and the three `exec` calls that the script made, each with **task name, pid, CPU, timestamp and the event's fields**. The five flag characters after the CPU (`.....`) say whether interrupts were off, a reschedule was needed, and so on; the header of the `trace` file explains each column.

Two ways to read the buffer: `trace` (a snapshot, readable many times) and **`trace_pipe`** (consuming, blocks until data arrives, the right one for a live feed).

### 2.3 Filtering in the kernel, and a mistake that gives no error

A filter drops events **before they are written**, so it saves both buffer space and time:

```text
--- 4. a filter in the kernel: a plain string field fails, a user pointer needs .ustring
$ grep -v '^#' /sys/kernel/tracing/trace | wc -l
0
$ cat /sys/kernel/tracing/events/syscalls/sys_enter_openat/filter
filename.ustring ~ "*passwd*"
$ grep -v '^#' /sys/kernel/tracing/trace | sed -E 's/^ +//; s/ +/ /g' | cut -c1-100
cat-1116 [000] ..... 223.100232: sys_openat(dfd: 0xffffff9c, filename: 0x7ffcb0852cc3, flags: 0, mod
id-1118 [001] ..... 223.103569: sys_openat(dfd: 0xffffff9c, filename: 0x7f07adfd0339, flags: 0x80000
```

The first filter, `filename ~ "*passwd*"`, was **accepted without an error and matched 0 events** although `cat /etc/passwd` certainly ran. The `filename` of `sys_enter_openat` is a **pointer into user memory**, and a plain string comparison on it does not read that memory. Written **`filename.ustring`** the kernel copies the string from user space for the comparison, and **2 events** matched: the `cat`, and `id`, which opens `/etc/passwd` to look up your user name. The printed event shows the argument as a raw pointer (`filename: 0x7ffd...`) because the text format does not dereference it; that is why `bpftrace` has `str()` (section 5). The lesson is general: **a filter that matches nothing and a filter that is wrong look the same**. Run something that must match first.

### 2.4 A probe on your own code, created at run time

`uprobe_events` accepts a line `p:GROUP/NAME PATH:OFFSET` and creates an event that fires when the program counter reaches that offset in that file:

```text
--- 5. a user probe (uprobe) on a function of our own program, created at run time
$ cat /sys/kernel/tracing/uprobe_events | sed 's#/tmp/lx11a#W#'
p:lx11/lookup W/target:0x00000000000012a0
$ grep -c 'lookup:' /sys/kernel/tracing/trace
3
$ grep 'lookup:' /sys/kernel/tracing/trace | head -2 | sed -E 's/^ +//; s/ +/ /g; s/0x[0-9a-f]+$/ADDR/'
target-1128 [000] DBZff 223.215892: lookup: (0x55995eeab2a0)
target-1128 [000] DBZff 223.218146: lookup: (0x55995eeab2a0)
```

The offset used here is the symbol's address from `nm`, which equals the file offset for an ordinary gcc built executable (from memory: for other layouts subtract the load segment's virtual address and add its file offset). The probe fired **3 times for a run of 3 requests**, once per call of `do_lookup`, with **no change to the program and no restart**. The address printed (`0x558a...`) is the run time address of the PIE executable. Remove a probe by writing an empty line to the file (`echo > uprobe_events`).

### 2.5 Markers, the buffer, and `trace-cmd`

`trace_marker` is a file that **your program writes to** to put its own line into the same time ordered stream as the kernel events, which is how you see "request 17 started" between a context switch and a disk read:

```text
--- 6. your own marker in the same timeline
$ grep -v '^#' /sys/kernel/tracing/trace | sed -E 's/^ +//; s/ +/ /g; s/^[a-z]+-[0-9]+ \[[0-9]+\] [^ ]+ [0-9.]+: //'
tracing_mark_write: request 17 start
sched_process_exec: filename=/bin/true pid=1133 old_pid=1133
tracing_mark_write: request 17 end
```

The buffer behind all of this is **per CPU and fixed size**, and it overwrites the oldest events when full:

```text
--- 7. the ring buffer: per CPU, fixed size, overwrites the oldest
$ ls /sys/kernel/tracing/per_cpu; grep -E 'entries|overrun|bytes' /sys/kernel/tracing/per_cpu/cpu0/stats
cpu0
cpu1
entries: 1
overrun: 0
commit overrun: 0
bytes: 36
```

The `overrun` counter is how you know events were lost (0 here). Grow the buffer with `buffer_size_kb` before a long trace; **a trace that has wrapped answers a different question than the one you asked**.

`trace-cmd` automates the same files (enable events, start, stop, save a binary `trace.dat`, report it, and a `trace-cmd list` of events):

```text
--- 8. trace-cmd: a front end for the same files
$ trace-cmd list -e | wc -l; trace-cmd list -e | grep -c '^syscalls:'
2305
720
$ trace-cmd record -e sched:sched_switch -o t.dat true 2>&1 | head -2
trace-cmd: Operation not permitted
  Can't enable ftrace
$ cat /proc/sys/kernel/ftrace_enabled; echo 1 > /proc/sys/kernel/ftrace_enabled
1
bash: echo: write error: Operation not permitted
```

`trace-cmd list` works and finds the same **2,305 events**. `trace-cmd record` fails with `Can't enable ftrace`: it first writes `/proc/sys/kernel/ftrace_enabled`, which this sandbox refuses (the last transcript shows the read and the refused write). So on this VM the `echo` interface is the only one that works. (From memory: on an ordinary kernel `trace-cmd record -e sched:sched_switch CMD` then `trace-cmd report` is the normal way, and `kernelshark` draws the result.)

### 2.6 What `ftrace` is good for

- **Ordered sequences of a few kinds of event** with exact timestamps: scheduler wake ups, interrupts, system calls, your own markers. Nothing else shows "A woke B, B ran 40 µs later on another CPU" as plainly.
- **Kernel internals** with the function tracers (from memory): the path a system call takes, with times.
- **A system with nothing installed**: you need only a shell and root.
- **Not for aggregation**: counting, histograms and joins need a tool in user space (a flood of text) or, better, eBPF (the next sections).

---


## 3. The probe types in detail

### 3.1 Tracepoints: the stable interface

A tracepoint is a call to a tiny function that is **compiled out to a `nop`** until something attaches to it (a `static key` patches the `nop` into a jump; from memory). The kernel developer chose where it goes and what it reports, and the field list is treated as an interface that tools depend on. List them with `bpftrace -l` or read `events/` in `tracefs`:

```text
--- 1. what can be probed here
$ bpftrace --version
bpftrace v0.20.2
$ bpftrace -l 2>/dev/null | cut -d: -f1 | sort | uniq -c | sort -rn | tr -s ' ' | tr '\n' ';'; echo
 2305 tracepoint; 1585 rawtracepoint; 14 software; 12 hardware;
$ bpftrace -l 'kprobe:*' 2>/dev/null | wc -l; bpftrace -l 'tracepoint:sched:*' 2>/dev/null | wc -l
0
26
$ bpftrace -lv 'tracepoint:sched:sched_switch' | tr -s ' ' | tr '\n' ';'; echo
tracepoint:sched:sched_switch; char prev_comm[16]; pid_t prev_pid; int prev_prio; long prev_state; char next_comm[16]; pid_t next_pid; int next_prio;
```

Read it line by line. **2,305 tracepoints** and **1,585 raw tracepoints** are listed (the raw ones are the same hooks, delivered to BPF without the formatting step, so they are cheaper), **14 software** and **12 hardware** event names (the hardware ones exist as names only: there is no PMU), and **no `kprobe` entry at all**: `bpftrace -l 'kprobe:*'` printed **0**. The `sched` group has **26** tracepoints; `-lv` shows the fields of one of them, `sched_switch`, which carries the previous task's name, pid, priority and **state**, and the next task's name, pid and priority. That one event is enough to build a run queue latency tool, an off-CPU profiler and a context switch counter (sections 6 and 8).

Where to find the one you need: `perf list`, `bpftrace -l '*word*'`, or `grep` in `/sys/kernel/tracing/available_events` (**the group is the part before the colon**). The system call tracepoints come in `sys_enter_NAME` and `sys_exit_NAME` pairs, and `raw_syscalls:sys_enter` and `sys_exit` cover all calls at once.

### 3.2 uprobes: a breakpoint in a user program

A **uprobe** works by **replacing the first byte of the target instruction in the page cache copy of the file with `int3`** (from memory). When any process executes that address the CPU traps into the kernel, the kernel runs the attached handlers (an `ftrace` event, a perf event, a BPF program), executes the displaced instruction out of line (single step), and returns. A **uretprobe** also overwrites the function's **return address on the stack** with a trampoline so that it can catch the return, which is why a uretprobe costs about double and why it confuses stack unwinders and programs that inspect the stack.

Two consequences, both measured in section 9. **The cost per hit is that of two traps and a kernel round trip**: here **about 27 µs per call**, against 2 to 3 nanoseconds for the function itself, so a uprobe on a function called a million times a second is a self inflicted outage. (From memory: on bare metal a uprobe is closer to one to a few microseconds; a virtual machine makes traps more expensive, and this VM is the worst case.) And **a uprobe is tied to the file**: it needs the symbol to exist (the binary must not be stripped, or you give a raw offset), and it applies to every process that maps that file. Both `ftrace` (section 2.4) and `bpftrace` (section 5) create them from a path and a symbol or offset.

### 3.3 kprobes: any kernel instruction, and not here

A **kprobe** is the same idea in the kernel: put a breakpoint at the entry of a function (`vfs_read`) or at an offset, run a handler, step over the instruction. A **kretprobe** catches the return. They reach everything, with **no stability guarantee**: a function can be renamed, inlined (then it has no address to probe), or have its arguments changed between kernel versions, and **a handler runs in a context where it may not sleep or fault**. Many tools therefore carry two versions of a probe, one on a tracepoint and a fallback on a kprobe. The newer **`fentry` and `fexit`** (BPF trampolines, from memory: kernel 5.5 and later) are cheaper than kprobes and give typed access to arguments through BTF.

None of it is available here:

```text
--- 7. what goes wrong
$ bt -e 'kprobe:vfs_read { @ = count(); }' | grep -E 'WARNING|ERROR' | cut -c1-120
stdin:1:1-16: WARNING: vfs_read is not traceable (either non-existing, inlined, or marked as "notrace"); attaching to it
ERROR: Error attaching probe: 'kprobe:vfs_read'
$ bt -e 'fentry:vfs_read { @ = count(); }' | head -1 | cut -c1-100
stdin:1:1-16: ERROR: kfunc/kretfunc not available for your kernel version.
$ bt -e 'tracepoint:syscalls:sys_enter_nosuch { @ = count(); }' 2>&1 | head -2 | cut -c1-110
stdin:1:1-37: ERROR: tracepoint not found: syscalls:sys_enter_nosuch
tracepoint:syscalls:sys_enter_nosuch { @ = count(); }
$ bt -e 'tracepoint:syscalls:sys_enter_openat /comm == "targett"/ { @ = count(); }' -c '/tmp/lx11b/target 3' | tail -1
@: 0
```

Read the four failures. `kprobe:vfs_read` gets a **warning** that the function "is not traceable" (the cause is the missing option, not the function), followed by **`Error attaching probe`**. `fentry:vfs_read` is rejected with **`kfunc/kretfunc not available for your kernel version`**. A tracepoint with a misspelled name stops with **`tracepoint not found`** and shows where in the program. And the last one is the dangerous kind: **a filter that never matches** (`comm == "targett"` has a typo) is accepted and the program prints **`@: 0`**, which looks like a measurement. Zero events is an answer only after you have shown that the same probe sees events when it should.

### 3.4 USDT: probes an application ships

A **USDT** (user statically defined tracing) probe is a `nop` that the program or library author placed with the `sys/sdt.h` macros, together with an **ELF note** that tells a tracer the probe's name and the registers or memory that hold its arguments (from memory). They are the user space equivalent of tracepoints: **stable, cheap when idle**, and documented by the project. Databases, the JVM, the language runtimes (when built with the option) and **glibc** ship them.

```text
--- 1. USDT: probes that a library ships, listed from its ELF notes
$ bpftrace -l 'usdt:/lib/x86_64-linux-gnu/libc.so.6:*' 2>/dev/null | wc -l
57
$ bpftrace -l 'usdt:/lib/x86_64-linux-gnu/libc.so.6:libc:*' 2>/dev/null | sed 's#.*:##' | grep -E '^(memory_sbrk_more|mutex_entry|pthread_create|setjmp)$'
memory_sbrk_more
mutex_entry
pthread_create
setjmp
$ readelf -n /lib/x86_64-linux-gnu/libc.so.6 | grep -c stapsdt
82
$ readelf -n /lib/x86_64-linux-gnu/libc.so.6 | grep -A2 -m1 'Provider: libc' | tr -s ' ' | tr '\n' ';'; echo
 Provider: libc; Name: setjmp; Location: 0x0000000000044f15, Base: 0x00000000001d7760, Semaphore: 0x0000000000000000;
```

glibc here carries **57 probes** (the ELF notes show **82** `stapsdt` lines, which includes the notes' own headers): memory allocator events such as `memory_sbrk_more`, lock events such as `mutex_entry`, `pthread_create`, `setjmp`. The note for one probe gives its **name, the address of its `nop`, the base address and the semaphore** that a tracer may increment to enable it. Attaching is the other half:

```text
--- 2. attaching to one: refused here
$ bpftrace -e 'usdt:/lib/x86_64-linux-gnu/libc.so.6:libc:pthread_create { @ = count(); }' -c 'true' 2>&1 | grep -v RLIMIT_MEMLOCK | head -3
Attaching 1 probe...
ERROR: Error finding location for probe: usdt:/lib/x86_64-linux-gnu/libc.so.6:libc:pthread_create
```

**`Error finding location for probe`**: `bpftrace` could list the probe and not attach to it on this VM. I did not find the cause (a mismatch between the note's base address and how this system maps the library is the likely one, from memory) and report it as an observed failure. Plan for it: **a tool that lists a probe is not a proof that it can attach**, and every lab here was therefore checked end to end.

### 3.5 Which one, in a table

| You want | First choice | Then | Why |
| --- | --- | --- | --- |
| Every system call of a process, or one call's latency | `syscalls:sys_enter_*` and `sys_exit_*` | `raw_syscalls` for all of them | stable, cheap |
| Context switches, wake ups, run queue latency | `sched:sched_switch`, `sched_wakeup`, `sched_wakeup_new` | | stable |
| Block I/O requests | `block:block_rq_issue`, `block_rq_complete` | | stable |
| TCP events (retransmits, state changes, resets) | `tcp:tcp_retransmit_skb`, `tcp:tcp_receive_reset`, `sock:inet_sock_set_state` | kprobes on `tcp_*` only if missing | stable |
| A function in your own program | uprobe on the symbol | USDT if you can add one | no code change with a uprobe, stable with USDT |
| A library call (`malloc`, `SSL_read`) | uprobe on the library | USDT if shipped (glibc malloc) | |
| Allocation sizes and stacks for a leak | uprobes on `malloc` and `free` (heavy) | `memleak` tool, section 6 | expect a large cost |
| A kernel function that has no tracepoint | kprobe or `fentry` | | no stability, **not available here** |
| What a process is doing right now, with no setup | `perf top`, `strace -p` | | LX 10, section 9 below |

---

## 4. eBPF in one picture

**eBPF** (extended Berkeley Packet Filter, now just BPF) lets you load a **small program into the kernel**, attach it to a **hook** (a tracepoint, a probe, a network device, a cgroup, a scheduler decision), and have it run **every time the hook fires**, with the kernel guaranteeing in advance that it **terminates and cannot crash the system**. A program keeps its state in **maps** and talks to user space through them and through a **ring buffer**. That is the whole idea; the rest is detail.

```text
  your source (C, or a bpftrace script)
        |  clang -target bpf        (bpftrace does this step itself)
        v
  BPF bytecode in an ELF file  +  BTF (type information for CO-RE)
        |  bpf(BPF_PROG_LOAD)   <---- a libbpf based loader, or bpftool, or bpftrace
        v
  VERIFIER: reads every path through the program; rejects unsafe ones
        |  JIT compiler: bytecode -> native x86 machine code
        v
  attached to a HOOK ---- event fires ----> program runs in the kernel
        |                                          |  helpers: bpf_get_current_pid_tgid, bpf_ktime_get_ns,
        |                                          |  bpf_probe_read_user_str, bpf_map_lookup_elem, ...
        v                                          v
  MAPS (hash, array, per CPU, stack traces, ring buffer) <---- user space reads them (bpf() syscalls, mmap)
```

### 4.1 The parts

| Part | What it is |
| --- | --- |
| **Program type and hook** | The program type fixes where it attaches and what context it receives: `tracepoint`, `kprobe`, `uprobe`, `perf_event` (sampling), `raw_tracepoint`, `fentry`, `xdp` and `tc` (packets), `cgroup/*`, `lsm`, `sched_ext` and others. A tracing program receives a pointer to the event's fields |
| **Map** | Kernel memory shared between programs and user space: **hash**, **array**, **per CPU array and hash** (no locks, summed on read), **stack trace** (stack id to addresses), **LRU hash**, **ring buffer** (events to user space, shared by all CPUs) and more. A map has a fixed **maximum number of entries**, so it cannot grow without bound |
| **Helper** | A kernel function that programs may call, by number: read the current pid, the time, the task, a user string, a map entry, send a ring buffer record. A program **cannot call anything else** |
| **Verifier** | A static analyser run at load. It walks **every possible path** of the program, tracks the **type and range of every register**, and rejects: unbounded loops, reads of uninitialised registers, out of range map accesses, dereferences of unchecked pointers, calls of unknown helpers, programs over the instruction limit (1,000,000 verified instructions here) |
| **JIT** | Translates the verified bytecode into native code (`bpf_jit_enable` is 1 here), so a program runs at close to compiled speed. From memory, without it an interpreter runs the program and a probe costs several times more |
| **BTF** | **BPF Type Format**: compact debug information for the kernel's own types, exposed as `/sys/kernel/btf/vmlinux` (present here). It tells a loader the real layout of `struct task_struct` on **this** kernel |
| **CO-RE** | **Compile Once, Run Everywhere**: your program says "read field `real_parent` of `task_struct`" instead of "read 8 bytes at offset 2,264". The loader (**`libbpf`**) compares your compile time BTF with the running kernel's and **rewrites the offsets at load time**. This is what makes a BPF tool built once work on many kernels, without kernel headers on the target |
| **`libbpf` and skeletons** | The C loader library. `bpftool gen skeleton` turns a compiled program into a C header with `open`, `load`, `attach` and `destroy` functions and typed access to its maps |
| **`bcc` (older)** | A framework that **compiles the C program at run time** on the target with an embedded LLVM against kernel headers, so it needs the compiler and the headers on every machine (the reason its tools fail here). **`libbpf` + CO-RE** replaced that design |
| **`bpftrace`** | A high level language and tool: one line of awk like script is compiled to BPF at run time (needs BTF for some features, not the kernel headers) |

### 4.2 Build and run a program

This is the smallest realistic BPF tool: a program that runs on every `openat`, counts calls per pid in a hash map, and sends the file name and the parent pid to user space through a ring buffer.

```c
// count_open.bpf.c: a BPF program that runs on every openat(2); counts per pid and sends the path to user space
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

char LICENSE[] SEC("license") = "GPL";

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} opens SEC(".maps");

struct event {
    __u32 pid;
    __u32 ppid;
    char comm[16];
    char path[48];
};

struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 1 << 16);
} rb SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_openat")
int on_openat(struct trace_event_raw_sys_enter *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u64 one = 1, *cnt = bpf_map_lookup_elem(&opens, &pid);

    if (cnt)
        __sync_fetch_and_add(cnt, 1);
    else
        bpf_map_update_elem(&opens, &pid, &one, BPF_ANY);

    struct event *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e)
        return 0;
    struct task_struct *t = (struct task_struct *)bpf_get_current_task();
    e->pid = pid;
    e->ppid = BPF_CORE_READ(t, real_parent, tgid);
    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    bpf_probe_read_user_str(&e->path, sizeof(e->path), (void *)ctx->args[1]);
    bpf_ringbuf_submit(e, 0);
    return 0;
}
```

Walk through it. `vmlinux.h` is every kernel type, generated from BTF. `SEC(...)` puts each object in a named ELF section; the section name **`tracepoint/syscalls/sys_enter_openat`** tells `libbpf` which hook to attach to. The two maps are declared as static structures (type, size, key and value types). In the program: `bpf_get_current_pid_tgid() >> 32` is the process id; the map is looked up, **checked for NULL** (the verifier insists, section 4.4), and either incremented atomically or created. `bpf_ringbuf_reserve` gets space for an event, **which must be checked as well**; `BPF_CORE_READ(t, real_parent, tgid)` follows **two pointers in kernel memory safely and relocatably**; the file name is a **user pointer** so it is copied with `bpf_probe_read_user_str`, and the event is submitted.

The loader is ordinary user space C:

```c
// count_open.c: load count_open.bpf.o through its skeleton, run a command, print what the program saw
#include <bpf/libbpf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "count_open.skel.h"

struct event { unsigned pid, ppid; char comm[16]; char path[48]; };
static int child, shown, seen;

static int on_event(void *ctx, void *data, size_t len)
{
    struct event *e = data;
    if ((int)e->pid != child) return 0;
    seen++;
    if (shown++ < 3) printf("event: comm=%s ppid_is_loader=%d path=%s\n", e->comm, (int)e->ppid == getpid(), e->path);
    return 0;
}

int main(int argc, char **argv)
{
    struct count_open_bpf *skel = count_open_bpf__open_and_load();
    if (!skel) { fprintf(stderr, "load failed\n"); return 1; }
    if (count_open_bpf__attach(skel)) { fprintf(stderr, "attach failed\n"); return 1; }
    struct ring_buffer *rb = ring_buffer__new(bpf_map__fd(skel->maps.rb), on_event, NULL, NULL);
    child = fork();
    if (child == 0) { execvp(argv[1], argv + 1); _exit(127); }
    waitpid(child, NULL, 0);
    ring_buffer__consume(rb);
    unsigned key = child;
    unsigned long long cnt = 0;
    bpf_map__lookup_elem(skel->maps.opens, &key, sizeof key, &cnt, sizeof cnt, 0);
    printf("map opens[child] = %llu, ring buffer events for child = %d\n", cnt, seen);
    ring_buffer__free(rb);
    count_open_bpf__destroy(skel);
    return 0;
}
```

`count_open_bpf__open_and_load` runs the verifier and the JIT, `__attach` attaches the program to the tracepoint, a ring buffer consumer is set up, the command is forked, and after it exits the program prints the first three events and the map counter for the child's pid. The build chain and the run:

```text
--- 1. the build: BTF gives the kernel types, clang makes BPF bytecode, a skeleton wraps it
$ ls -l /sys/kernel/btf/vmlinux | awk '{print $5, "bytes of kernel type information"}'
5514989 bytes of kernel type information
$ /usr/lib/linux-tools-6.8.0-149/bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h; wc -l < vmlinux.h; grep -c '^struct ' vmlinux.h
134722
10908
$ grep -A5 '^struct trace_event_raw_sys_enter {' vmlinux.h
struct trace_event_raw_sys_enter {
        struct trace_entry ent;
        long int id;
        long unsigned int args[6];
        char __data[0];
};
$ clang -O2 -g -target bpf -D__TARGET_ARCH_x86 -I. -c count_open.bpf.c -o count_open.bpf.o && file -b count_open.bpf.o | cut -d, -f1-2
ELF 64-bit LSB relocatable, eBPF
$ /usr/lib/linux-tools-6.8.0-149/bpftool gen skeleton count_open.bpf.o > count_open.skel.h; grep -c 'count_open_bpf__' count_open.skel.h
26
$ gcc -O2 -o count_open count_open.c -lbpf && ./count_open ./target 3
done 1
event: comm=target ppid_is_loader=1 path=/etc/ld.so.cache
event: comm=target ppid_is_loader=1 path=/lib/x86_64-linux-gnu/libc.so.6
event: comm=target ppid_is_loader=1 path=
map opens[child] = 5, ring buffer events for child = 5
```

Four facts from it. The kernel exposes **5.5 MB of type information** (`btf`) from which `bpftool` generated a **134,722 line `vmlinux.h`** with **10,908 structs**, including the exact `trace_event_raw_sys_enter` that the program takes as its context (a `long id` and six `args`; the file name is `args[1]`). `clang -target bpf` produced an **ELF object for the `eBPF` machine**. The skeleton header is generated code (26 lines of it mention `count_open_bpf__`: `open`, `load`, `attach`, `destroy` and the structure that holds the maps and programs). And the run printed three events then the totals: **`ppid_is_loader=1`** proves that the CO-RE read of `real_parent->tgid` returned the right parent (the loader forked the child), and **`opens[child] = 5`, ring buffer events 5** is the dynamic loader's two opens plus the program's three requests.

Look at the third event: **`path=` is empty**. The first `/etc/hostname` open arrived with no string. This is not a bug in the program; it is a rule of the hook. The tracepoint fires **at system call entry, before the kernel has read the path**, and `bpf_probe_read_user_str` **may not fault a page in** (a tracepoint handler cannot sleep). If the page holding the string has not been touched yet by the process, the read fails and returns an empty string. Section 5 reproduces it with `bpftrace`, and shows that touching the string in user space first makes the problem disappear.

### 4.3 What the kernel holds after the program is loaded

`bpftool` is the inspector. Load the object, pin it, run the workload, then look:

```text
--- 2. what the kernel holds after load: programs, maps, links
$ /usr/lib/linux-tools-6.8.0-149/bpftool prog show name on_openat | sed -E 's/^[0-9]+: //; s/ tag [0-9a-f]+//; s/loaded_at [^ ]+  //; s/ +/ /g'
tracepoint name on_openat gpl run_time_ns 59808 run_cnt 35
        uid 0
        xlated 512B jited 286B memlock 4096B map_ids 1621,1622
        btf_id 3876
$ /usr/lib/linux-tools-6.8.0-149/bpftool map show | grep -E 'opens|rb' | sed -E 's/^[0-9]+: //; s/ +/ /g'
hash name opens flags 0x0
ringbuf name rb flags 0x0
$ /usr/lib/linux-tools-6.8.0-149/bpftool link show | sed -E 's/^[0-9]+: //; s/prog [0-9]+/prog N/; s/ +/ /g'
perf_event prog N
        tracepoint sys_enter_openat
$ /usr/lib/linux-tools-6.8.0-149/bpftool prog dump xlated name on_openat | grep -v '^;' | sed -n '2,9p'
   0: (bf) r6 = r1
   1: (85) call bpf_get_current_pid_tgid#269056
   2: (77) r0 >>= 32
   3: (63) *(u32 *)(r10 -4) = r0
   4: (b7) r7 = 1
   5: (7b) *(u64 *)(r10 -16) = r7
   6: (bf) r2 = r10
   7: (07) r2 += -4
```

The first line is a **program**: type `tracepoint`, name `on_openat`, license `gpl` (**needed for GPL only helpers**), and, because `bpf_stats_enabled` was on, **`run_time_ns` and `run_cnt`** (section 9 turns them into a cost). `xlated 512B jited 286B` says the verified bytecode is **512 bytes** and the native x86 translation **286 bytes**. `memlock 4096B` is the memory charged to the program, and `map_ids` lists the two maps it uses. `bpftool map show` lists the `hash` map `opens` and the `ringbuf` map `rb`. `link show` lists the **link**: a `perf_event` link on the tracepoint `sys_enter_openat`, which is what keeps the program attached. Then the **translated** program: eight instructions of the beginning (a register copy, the helper call `bpf_get_current_pid_tgid`, a shift, stores to the stack). This is the language the verifier reads. Remove the pin (and, with it, the link) and the program is unloaded.

### 4.4 The verifier

The verifier is why you may run a program written by a colleague in production without reading it: **it proves, before the first event, that the program cannot read or write outside what it was given, cannot loop forever, and cannot leave a pointer unchecked**. It also decides whether your program loads, so you will read its messages often. Four programs that it refuses (one is picked with `-DCASE=1` to `4`):

```c
// bad.bpf.c: four programs the verifier refuses; compile with -DCASE=1 .. 4 to pick one
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 16);
    __type(key, __u32);
    __type(value, __u64);
} m SEC(".maps");

#if CASE == 1
SEC("tracepoint/syscalls/sys_enter_openat")
int no_null_check(void *ctx)
{
    __u32 k = 0;
    __u64 *v = bpf_map_lookup_elem(&m, &k);
    *v += 1;
    return 0;
}
#endif

#if CASE == 2
SEC("tracepoint/syscalls/sys_enter_openat")
int bad_pointer(struct trace_event_raw_sys_enter *ctx)
{
    char *path = (char *)ctx->args[1];
    return path[0];
}
#endif

#if CASE == 3
SEC("tracepoint/syscalls/sys_enter_openat")
int out_of_range(void *ctx)
{
    __u32 k = 0;
    char *v = bpf_map_lookup_elem(&m, &k);
    if (!v) return 0;
    return v[bpf_get_prandom_u32() & 0xff];
}
#endif

#if CASE == 4
SEC("tracepoint/syscalls/sys_enter_openat")
int unbounded(void *ctx)
{
    __u64 sum = 0;
    while (bpf_get_prandom_u32() != 7)
        sum += bpf_get_prandom_u32();
    return sum & 1;
}
#endif
```

Each was compiled and loaded; the last lines of the log say why it was refused:

```text
--- 3. the verifier: it reads the program before it may run
case 1: no_null_check
7: (79) r1 = *(u64 *)(r0 +0)
R0 invalid mem access 'map_value_or_null'
processed 7 insns (limit 1000000) max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0
case 2: bad_pointer
1: (71) r0 = *(u8 *)(r1 +0)
R1 invalid mem access 'scalar'
processed 2 insns (limit 1000000) max_states_per_insn 0 total_states 0 peak_states 0 mark_read 0
case 3: out_of_range
invalid access to map value, value_size=8 off=255 size=1
R7 max value is outside of the allowed memory range
processed 12 insns (limit 1000000) max_states_per_insn 0 total_states 1 peak_states 1 mark_read 0
case 4: unbounded
8: (0f) r6 += r7
BPF program is too large. Processed 1000001 insn
processed 1000001 insns (limit 1000000) max_states_per_insn 4 total_states 15004 peak_states 15004 mark_read 0
case 1 again, with an array map and the constant key 0: accepted
loaded, exit status 0
```

Read each one.

- **Case 1, `R0 invalid mem access 'map_value_or_null'`.** `bpf_map_lookup_elem` returns a pointer **or NULL**. Register `R0` (the return value) is of type `map_value_or_null`, and the program dereferenced it (`r1 = *(u64 *)(r0 +0)`) without a check. Fix: `if (!v) return 0;`. This is the most common rejection.
- **Case 2, `R1 invalid mem access 'scalar'`.** `ctx->args[1]` is a **number** (a user space address held in an integer), not a pointer the verifier knows to be valid. Reading through it directly is refused; the program must use **`bpf_probe_read_user`** (or `_str`), which checks the address at run time and fails safely.
- **Case 3, `invalid access to map value, value_size=8 off=255 size=1`, `R7 max value is outside of the allowed memory range`.** The value is 8 bytes and the index is `rand & 0xff`, up to 255. The verifier tracks the **range** of the index (0 to 255) and sees that the largest value reads past the end. Fix: bound the index to the size (`& 7`), then it passes.
- **Case 4, `BPF program is too large. Processed 1000001 insn`.** The loop's exit depends on a random number, so no iteration bound can be proved, and the verifier explores paths until it hits the **limit of one million verified instructions**. (The line before the message shows where it was: the `r6 += r7` of the loop body.) Fix: give the loop a constant bound (`for (i = 0; i < 64 && ...; i++)`) or use `bpf_loop`, or move the work to user space. This one takes visibly longer to reject than the others, because the verifier burns through the whole limit first.

And one acceptance that surprises: the array map version of case 1.

The last two lines of the transcript show the same unchecked dereference **accepted** when the map is an **array with one entry** and the key is the constant 0: the verifier knows that a constant, in range key on an array map **always finds an entry**, so the pointer cannot be NULL. (I wrote case 1 that way first and was puzzled that it loaded; the hash map is what makes the example fail.) The general lesson: **the verifier reasons about values, not about intent**; a pass is not proof that you checked, and a failure is a statement about a path the verifier could not rule out.

Verifier habits that save time (from memory): compile with `-O2` (unoptimised code is refused for stack and instruction reasons), keep programs short, check every pointer you receive from a helper, **clamp every index with a mask**, use `__always_inline` for small helper functions, and read the log **from the bottom**: the last instruction and the register state at the failure are what matter.

### 4.5 What it costs the kernel

The kernel can count the time spent in every program (`sysctl kernel.bpf_stats_enabled=1`; it adds a small cost per run, which is why it is off by default):

```text
--- 4. cost: the kernel counts run time per program when bpf_stats_enabled=1
kernel.bpf_stats_enabled = 1
$ /usr/lib/linux-tools-6.8.0-149/bpftool prog show name on_openat | head -1 | grep -o 'run_time_ns [0-9]* run_cnt [0-9]*'
run_time_ns 414139 run_cnt 209
average: 1930 ns per run
```

The program ran **209 times** for a 200 request workload and used **0.41 ms** in total, so **about 1.9 µs per run**. For a program that does one map lookup, a ring buffer reservation and three helper calls, it is a reasonable price; the number to remember is that **a BPF program that does real work costs microseconds, not nanoseconds, and a cheap one costs tens of nanoseconds** (section 9 measures a counter at roughly 10 to 100 ns more than no probe).

---


## 5. `bpftrace`: one line programs and short scripts

`bpftrace` compiles a program in a small awk like language to BPF, loads it, attaches it, and prints the maps when you stop it. It is the right tool for **a question you have not asked before**: you write the probe, the filter and the aggregation in one line, run it, and read the answer in seconds.

### 5.1 The language in one table

A program is a list of **`probe /filter/ { action }`** blocks.

| Piece | Examples | Meaning |
| --- | --- | --- |
| **Probe** | `tracepoint:syscalls:sys_enter_openat`, `uprobe:/path:func`, `uretprobe:/path:func`, `profile:hz:99`, `interval:s:1`, `BEGIN`, `END`, `usdt:/path:provider:name` | where to run; wildcards allowed (`sys_enter_*`; the probe count is printed as `Attaching N probes`) |
| **Filter** | `/pid == 1234/`, `/comm == "nginx"/`, `/args.ret < 0/` | run the action only if true; **evaluated in the kernel** |
| **Built in variables** | `pid`, `tid`, `uid`, `comm`, `nsecs`, `cpu`, `cpid` (the pid of the `-c` command), `arg0` to `arg5` (function arguments), `retval` (in a return probe), `args.NAME` (tracepoint fields), `kstack`, `ustack`, `probe` | `args` is typed from the tracepoint's format |
| **Map** | `@count`, `@by_comm[comm]`, `@start[tid]` | a global (per program) BPF map; **keys can be any tuple** |
| **Scratch variable** | `$x = nsecs - @start[tid];` | exists inside one probe firing |
| **Aggregations** | `count()`, `sum(n)`, `avg(n)`, `min(n)`, `max(n)`, `stats(n)`, `hist(n)` (power of two buckets), `lhist(n, lo, hi, step)` (linear) | kept **in the kernel**, per CPU, merged when read |
| **Functions** | `printf`, `str(ptr)`, `print(@map, N)` (top N), `clear(@m)`, `delete(@m[k])`, `exit()`, `time()`, `strftime` | `str` copies a user string; maps are printed automatically when the program ends |
| **Command line** | `-e 'prog'`, a script file, `-c 'cmd'` (trace only until `cmd` exits and set `cpid`), `$1` (a positional argument), `-l 'pattern'` (list), `-lv` (with fields) | |

The first rule of the style: **aggregate in a map, print at the end** (or every few seconds with `interval`). The second: **filter in the probe**, not afterwards.

### 5.2 Count: what does a program do

The question: which system calls does `target` make, and how many? One command, no code to write:

```text
--- 2. count: which system calls does a program make (one command, no code)
$ bt -e 'tracepoint:syscalls:sys_enter_* /pid == cpid/ { @[probe] = count(); } END { print(@, 5); clear(@); }' -c '/tmp/lx11b/target 40' | sed 's/tracepoint:syscalls:sys_enter_//'
Attaching 361 probes...
done 1
@[mmap]: 8
@[clock_nanosleep]: 40
@[read]: 41
@[openat]: 42
@[close]: 43
```

The wildcard attached **361 probes** (every `sys_enter_*` in `syscalls`); the filter `/pid == cpid/` keeps only the traced command (without it, the answer is dominated by `bpftrace` itself and by everything else on the machine; the first version I wrote counted `bpf` and `ioctl` calls from the tool). `print(@, 5)` printed the **top five**, in ascending order. The counts match the source of `target.c` exactly: **`clock_nanosleep` 40** (one `usleep` per request), **`openat` 42, `read` 41, `close` 43** (the 40 requests plus the dynamic loader's own calls) and **`mmap` 8** (the loader mapping libc). This is the check that your probe is wired correctly before you trust a latency: **compare a count with a number you know**.

### 5.3 Trace events one by one, and the empty string

When you need the arguments and not just the count, print them, with a filter that keeps the volume low:

```text
--- 3. who opens a file: the argument is a user pointer, str() copies it
$ bt -e 'tracepoint:syscalls:sys_enter_openat /str(args.filename) == "/etc/hostname"/ { printf("%s pid %d opens %s\n", comm, pid, str(args.filename)); }' -c '/tmp/lx11b/target 3' | sed -E 's/pid [0-9]+/pid N/'
Attaching 1 probe...
target pid N opens /etc/hostname
target pid N opens /etc/hostname
done 1
```

The program printed **2 lines for a run that opens `/etc/hostname` 3 times**. The probe reads `args.filename` with `str()`, at system call entry, and the first read of the string failed for the reason found in section 4.2: the page of `target`'s own constant string had not been touched yet, and a probe may not fault it in. The proof, with a program that opens one path twice and optionally touches the string first:

```text
--- 1. the path string is read at syscall entry, before the kernel has touched it
$ bpftrace -e 'tracepoint:syscalls:sys_enter_openat /comm == "pf"/ { printf("[%s]\n", str(args.filename)); }' -c './pf' 2>&1 | grep '^\['
[/etc/ld.so.cache]
[/lib/x86_64-linux-gnu/libc.so.6]
[]
[/etc/hostname]
```

```text
--- 2. the same program, touching the string in user space first
$ bpftrace -e 'tracepoint:syscalls:sys_enter_openat /comm == "pf"/ { printf("[%s]\n", str(args.filename)); }' -c './pf touch' 2>&1 | grep '^\['
[/etc/ld.so.cache]
[/lib/x86_64-linux-gnu/libc.so.6]
[/etc/hostname]
[/etc/hostname]
```

Without the touch, the **third** open shows **`[]`** and the fourth works (the page is present by then). With the touch the third shows the path. This matters because "who opened this file" is the most common question asked of this probe, and **the first access by a process to a path it has not read before can be lost**. Counts can be trusted, strings at syscall entry cannot always. Three remedies (from memory): save the pointer at entry and read the string in a `sys_exit_openat` probe (by then the kernel has touched it), catch the call in a `uprobe` on `open` in libc, or accept the gap and say so. The maintained `opensnoop` printed **all four** `/etc/hostname` opens in section 6, which is consistent with the first remedy.

### 5.4 Latency: an entry probe and an exit probe

Latency is **the time between two events**, so store the timestamp at the first, subtract at the second, put the difference in a histogram. A script file for the latency of `read` in a `dd` that copies 2 MB in 4 KB blocks:

```bt
// lat.bt: latency of read(2) per process, from syscall entry to exit
tracepoint:syscalls:sys_enter_read
/comm == "dd"/
{
    @start[tid] = nsecs;
}

tracepoint:syscalls:sys_exit_read
/@start[tid]/
{
    @read_us = hist((nsecs - @start[tid]) / 1000);
    @bytes = sum(args.ret > 0 ? args.ret : 0);
    delete(@start[tid]);
}

END
{
    clear(@start);
}
```

The shape is the one pattern to learn: **`@start[tid] = nsecs` at entry, `@h = hist(nsecs - @start[tid])` at exit, `delete(@start[tid])` after** (so the map does not fill with threads that died between the two probes), and a filter on both probes that restricts it to the command of interest. `hist` takes any integer; dividing by 1000 gives microseconds. The `END` block clears `@start` so that the leftover entries are not printed. `args.ret` is the return value of the call: its sum counts the bytes that came back.

```text
--- 4. a latency histogram from an entry probe and an exit probe
$ cat lat.txt
Attaching 3 probes...
@bytes: 2048832
@read_us:
[0]                  484 |####################################################|
[1]                    3 |                                                    |
[2, 4)                 2 |                                                    |
[4, 8)                 2 |                                                    |
[8, 16)                1 |                                                    |
[16, 32)               3 |                                                    |
[32, 64)               1 |                                                    |
[64, 128)              2 |                                                    |
[128, 256)             1 |                                                    |
[256, 512)             2 |                                                    |
```

**501 reads** were measured. **484 took under a microsecond** (bucket `[0]`), the rest tail off to **256 to 512 µs**: the usual shape of a cache hit path with a few slow outliers (the first reads that missed the page cache or were interrupted). The mean would have hidden this; the histogram shows **a fast mode and a thin slow tail**, and the tail is what a user feels. `@bytes` is **2,048,832**: 500 reads of 4,096 bytes is 2,048,000, and the extra **832** is the one read of an ELF header by the dynamic loader (832 bytes is the usual first read of a shared library). A sum that comes out to the byte is a second proof that the probe sees what you think it sees.

### 5.5 A user function: `uprobe` and `uretprobe`

The same pattern on a function of our own program. The probe point is the symbol; the path is where the binary lives:

```bt
// lookup.bt: time spent inside do_lookup() of ./target, and who called it
uprobe:/tmp/lx11b/target:do_lookup
{
    @in[tid] = nsecs;
}

uretprobe:/tmp/lx11b/target:do_lookup
/@in[tid]/
{
    $us = (nsecs - @in[tid]) / 1000;
    @lookup_us = hist($us);
    @stats = stats($us);
    delete(@in[tid]);
}
```

```text
--- 5. uprobe and uretprobe: time inside one function of our own program
$ grep -v Attaching lookup.txt
done 1
@lookup_us:
[16, 32)               7 |#######                                             |
[32, 64)              48 |####################################################|
[64, 128)              5 |#####                                               |
@stats: count 60, average 43, total 2581
```

60 calls, all in the **16 to 128 µs** range, **average 43 µs**, and `stats` gave count, average and total in one line. **Do not believe the 43.** `do_lookup` makes three system calls, which `syscount` in section 6 measured at about **8 µs, 2.5 µs and 1.6 µs** (`openat`, `read`, `close`), roughly 12 µs together; the rest is **the probes themselves**: a uprobe costs about 27 µs per hit here (section 9) and the two of them straddle the function, so most of the measured interval is **the cost of measuring**. This is the standard hazard of a heavy probe on a short function: the latency you see is the latency you added. Use a uprobe on a function that takes milliseconds, or on a rare call; for a tight, hot function use sampling (`profile`) or a tracepoint instead.

### 5.6 Stacks: where is it, and why did it sleep

Two stack questions, one for each kind of time:

```text
--- 6. stacks: user stack by on-CPU sampling, kernel stack at a context switch
$ bt -e 'profile:hz:499 /pid == $1/ { @[ustack(3)] = count(); } interval:s:1 { print(@, 2); clear(@); exit(); }' $(pgrep -n target)
Attaching 2 probes...
@[
    parse_request+75
    main+42
    __libc_start_call_main+122
]: 134
@[
    parse_request+69
    main+42
    __libc_start_call_main+122
]: 224
$ bt -e 'tracepoint:sched:sched_switch /args.prev_comm == "target"/ { @[kstack(4)] = count(); } END { print(@, 1); clear(@); }' -c '/tmp/lx11b/target 40'
Attaching 2 probes...
done 1
@[
    __traceiter_sched_switch+65
    __schedule+1560
    schedule+35
    do_nanosleep+94
]: 40
```

The first program samples **499 times a second** on every CPU (`profile:hz:499`), keeps only samples whose pid is the program's (`$1` takes the pid from the command line, here found with `pgrep -n`), and counts them by **user stack, three frames deep** (`ustack(3)`). It ran for one second via `interval:s:1` and stopped itself. The two top stacks have the **same frames** (`parse_request`, `main`, `__libc_start_call_main`) and differ only in the **offset inside `parse_request`** (`+75` against `+69`), with **224 and 134 samples**: together **358 samples of the 499 possible in that second**, so the process was on a CPU about 72 percent of the time, and every sample was in the compute loop of `parse_request`. Two details to take away. **User stacks are resolved when printed, so the process must still be alive** (this is why the program ran in the background and the probe attached by pid, not with `-c`: with `-c`, the stacks print as hex after the process exits). And they need **frame pointers** in the traced code (`-fno-omit-frame-pointer`, as in LX 10): the program was built that way, and the sampled code runs in its own frames, so the three frames printed are the real ones.

The second program records the **kernel stack at every context switch away from `target`** (`sched_switch` with `prev_comm == "target"`, four frames deep) and printed the single most common one: **40 times** (once per request), always `__schedule` called from `schedule` called from **`do_nanosleep`**. It tells you why the program left the CPU, without any profiler: **it called `nanosleep`**. Extend it to every process and you have the core of the off-CPU profile of LX 10, in one line.

### 5.7 Rules for writing `bpftrace` that you can trust

1. **Check a probe with something that must happen** and compare to a known count (section 5.2) before measuring.
2. **Filter first, in the probe**. `/pid == $1/` or `/comm == "x"/`. Every event that passes costs the whole action.
3. **Aggregate, do not print per event.** A `printf` per event on a hot probe **loses events silently** (section 9).
4. **Delete map entries you create** (`delete(@start[tid])`), and clear scratch maps in `END`, or a map of dead threads grows to its limit (from memory: 4,096 keys by default, `BPFTRACE_MAP_KEYS_MAX`).
5. **A uprobe on a hot function changes what you measure.** Time the work that takes milliseconds.
6. **Read strings and stacks knowing their limits**: strings can be empty at syscall entry (section 5.3), user stacks need frame pointers and a live process (section 5.6).
7. **State the duration and the load** in the answer: "for 10 s, with 40 percent CPU", not "it is rare".
8. **Do not write a program longer than a screen** and put it in production. Past that, use a maintained tool (next section), or write a `libbpf` program with tests.

---

## 6. Ready made tools: `libbpf-tools` and `bcc`

The kernel developers and Brendan Gregg's collection supply **over a hundred tools** (from memory) that each answer one question, with the right probes, units and edge cases already handled. Learn the list; reach for a tool before you write a one liner. There are two generations with the same names:

- **`bcc`** (the `*-bpfcc` commands on Ubuntu, Python with embedded C). It **compiles the program when it starts**, so the target machine needs the compiler libraries **and kernel headers**. Here:

```text
--- 1. the bcc version of a tool compiles C at start up and needs kernel headers
$ ls /lib/modules/$(uname -r)/build 2>&1 | head -1
ls: cannot access '/lib/modules/6.18.44-fc-v553/build': No such file or directory
$ opensnoop-bpfcc -d 1 2>&1 | grep -E 'Unable to find kernel headers' | cut -c1-70
Unable to find kernel headers. Try rebuilding kernel with CONFIG_IKHEA
$ dpkg -s bpfcc-tools | grep '^Version'; ls /usr/sbin/*-bpfcc | wc -l
Version: 0.29.1+ds-1ubuntu7
128
```

The build directory for the running kernel does not exist, and the tool prints **`Unable to find kernel headers`** and stops. The package installs **128** `*-bpfcc` commands; I tried nine of them (this one, `execsnoop`, `runqlat`, `biolatency`, `tcpconnect`, `syscount`, `profile`, `offcputime`, `tcpretrans`) and all stopped with the same message (only the first is kept in the transcript). (From memory: on a stock distribution kernel, `linux-headers-$(uname -r)` is installed and they work.)

- **`libbpf-tools`** (plain names, `opensnoop`, `runqlat`, and so on). **Compiled ahead of time, CO-RE**, small static binaries that need only BTF in the kernel:

```text
--- 2. the libbpf version is compiled ahead of time and carries BTF relocations
$ dpkg -L libbpf-tools | grep -c '^/usr/sbin/[a-z]'; file -b /usr/sbin/opensnoop | cut -d, -f1-2; ls -l /usr/sbin/opensnoop | awk '{print $5, "bytes"}'
66
ELF 64-bit LSB pie executable, x86-64
379288 bytes
```

**66** programs and a **379 KB** executable for `opensnoop`. These work here, so the rest of this section uses them. They accept the same options as the `bcc` versions in most cases; check `--help`.

### 6.1 Five tools, five questions

**`opensnoop` and `execsnoop`: what files are opened, what programs are started.**

```text
--- 1. opensnoop and execsnoop: one line per event, filtered in the kernel by name
$ opensnoop -n target -d 1 2>&1 | q | sed -E 's/^ *[0-9]+ +/N /'
PID    COMM              FD ERR PATH
N target             3   0 /etc/ld.so.cache
N target             3   0 /lib/x86_64-linux-gnu/libc.so.6
N target             3   0 /etc/hostname
N target             3   0 /etc/hostname
N target             3   0 /etc/hostname
N target             3   0 /etc/hostname
$ timeout -s INT -k 2 1.5 execsnoop 2>&1 | q | awk '{print $1, $NF}' | sed -E 's/^[a-z]+ [0-9]+ //' | head -4
PCOMM ARGS
true /bin/true
ls /
echo hi
```

`opensnoop -n target` printed **every `open` of the command**, with pid, name, **file descriptor, errno and path**: the two loader opens and then all **four `/etc/hostname` opens**, including the first one that the `bpftrace` probe of section 5.3 lost. `execsnoop` printed **one line per process started**, with its arguments (`true`, `ls /`, `echo hi`): the first thing to run when a service "keeps restarting something" or a script spawns processes you did not expect. Both filter in the kernel and add little cost; the cost is in the events they print, so name a process (`-n`) or a user on a busy machine.

**`runqlat`: how long do runnable threads wait for a CPU?** The measure that separates "the CPU is busy" from "my thread is blocked". Idle machine against four busy loops on the two CPUs:

```text
--- 2. runqlat: how long runnable threads wait for a CPU, idle machine against 4 busy loops on 2 CPUs
$ runqlat 2 1 2>&1 | q | sed -n '2,12p'
     usecs               : count    distribution
         0 -> 1          : 26       |****************************************|
         2 -> 3          : 8        |************                            |
         4 -> 7          : 3        |****                                    |
         8 -> 15         : 11       |****************                        |
        16 -> 31         : 20       |******************************          |
        32 -> 63         : 22       |*********************************       |
        64 -> 127        : 1        |*                                       |
       128 -> 255        : 1        |*                                       |
$ runqlat 2 1 2>&1 | q | sed -n '2,16p'
     usecs               : count    distribution
         0 -> 1          : 28       |*                                       |
         2 -> 3          : 16       |                                        |
         4 -> 7          : 23       |                                        |
         8 -> 15         : 25       |*                                       |
        16 -> 31         : 8        |                                        |
        32 -> 63         : 6        |                                        |
        64 -> 127        : 4        |                                        |
       128 -> 255        : 2        |                                        |
       256 -> 511        : 0        |                                        |
       512 -> 1023       : 0        |                                        |
      1024 -> 2047       : 1        |                                        |
      2048 -> 4095       : 929      |****************************************|
      4096 -> 8191       : 39       |*                                       |
      8192 -> 16383      : 1        |                                        |
```

On the **idle** machine the histogram is concentrated under **64 µs**. With **four busy loops on two CPUs** a second population appears, and it is the big one: **929 waits in the 2,048 to 4,095 µs bucket and 39 in the next**, i.e. **2 to 8 milliseconds**, the length of a scheduler time slice. Every thread that wakes up while both CPUs run a loop waits for a slice to end. The tool measures the **delay between becoming runnable and running**, which no CPU utilisation number shows. (A run queue tail in the milliseconds is why a latency sensitive service sharing a CPU with batch work has a bad 99th percentile.) The tool is built on `sched_wakeup`, `sched_wakeup_new` and `sched_switch`, all tracepoints.

**`offcputime`: where does a process sleep?** The off-CPU analysis of LX 10 as one command: it sums **the time between switching out and switching back in**, per stack, for the process of interest:

```text
--- 3. offcputime: where a process sleeps, summed in microseconds
$ offcputime -p $(pgrep -n target) 1 2>&1 | q | sed -E 's/bpf_prog_[0-9a-f]+_/bpf_prog_HASH_/; s/\(([0-9]+)\)/(PID)/' | tail -19
    bpf_trace_run4
    __traceiter_sched_switch
    __schedule
    schedule
    do_nanosleep
    hrtimer_nanosleep
    common_nsleep
    __x64_sys_clock_nanosleep
    do_syscall_64
    entry_SYSCALL_64_after_hwframe
    clock_nanosleep
    nanosleep
    usleep
    main
    __libc_init_first
    __libc_start_main
    _start
    -                target (PID)
        938064
```

The stack is read from the **top** (the innermost frame first): the program called `usleep`, which called `nanosleep` and `clock_nanosleep`, the kernel went `do_nanosleep`, `schedule`, `__schedule`, and the BPF program's own frames sit on top. The number at the bottom is **microseconds spent in that stack in the one second window: 938,064**, so **94 percent of the second was spent asleep in `usleep`**. The tool prints one block per distinct stack, and the transcript keeps the last, which is the largest; the others (a `read`, a preemption) were far smaller. For a real service the largest stack tells you which lock, which disk read or which backend call the time went to. Run it for 5 to 30 seconds, not minutes.

**`syscount`: which system calls, how many, how slow.** `-L` adds the total time:

```text
--- 4. syscount: counts and latency per syscall for one process
$ syscount -p $(pgrep -n target) -L -d 1 2>&1 | q | head -8
Tracing syscalls, printing top 10... Ctrl+C to quit.
[21:01:08]
SYSCALL                   COUNT        TIME (us)
clock_nanosleep             438       938214.114
openat                      439         3614.149
read                        439         1101.346
close                       439          691.342
```

In one second **`clock_nanosleep` took 938,214 µs in 438 calls**, which is the sleeping again, and **`openat` 3,614 µs in 439 calls (8.2 µs each), `read` 2.5 µs and `close` 1.6 µs**. These are the numbers that gave the 12 µs of section 5.5. `syscount` is also the first thing to run on an unfamiliar process: **which kernel interface does it use, and which of those calls takes the time**.

### 6.2 Which tools run on this VM

The collection is large, and which tools work depends on the kernel's probes. A sweep ran every `libbpf-tools` program for two seconds and classified it by what `libbpf` printed:

```text
--- 1. which tools work here: each run for 2 seconds, classified by what libbpf printed
ran without a libbpf error: 28
failed because there are no kprobes: 23
failed for another reason: 3
ok: bashreadline biolatency biopattern bitesize cpudist drsnoop execsnoop exitsnoop fsdist fsslower
futexctn gethostlatency hardirqs memleak mountsnoop offcputime opensnoop readahead runqlat
runqslower sigsnoop softirqs statsnoop syscount tcplife tcppktlat tcpstates wakeuptime
no kprobes: bindsnoop biosnoop biostacks biotop cachestat capable filelife filetop funclatency
klockstat ksnoop mdflush numamove oomkill slabratetop solisten tcpconnect tcpconnlat tcprtt
tcpsynbl tcptop tcptracer vfsstat
other: cpufreq llcstat runqlen
```

**28 of 54 ran**, **23 failed because they attach a kprobe** (`failed to add legacy kprobe event`: `tcpconnect`, `biosnoop`, `vfsstat`, `filetop`, `funclatency`, `oomkill`, `tcptop` and others) and **3 failed for another reason**: `cpufreq` (no cpufreq interface in a VM), `llcstat` (needs a PMU event) and `runqlen` (**the BPF program failed to load with `Invalid argument`**; the cause was not investigated). "Ran without a libbpf error" means it loaded and attached for two seconds; it does not mean that anything was printed, because a quiet system has nothing to report. Two practical notes. The tools that work are **the ones built on tracepoints** (`syscount`, `opensnoop`, `execsnoop`, `runqlat`, `offcputime`, `tcplife`, `tcpstates`, `biolatency`) and software sampling; the tools that fail are **the ones whose question has no tracepoint** (a function inside the TCP stack, the VFS, the OOM killer). On a normal kernel the whole list works; on a cloud image or a minimal kernel, **check before you need it** and keep the tracepoint based tools as the fall back.

### 6.3 The catalogue worth knowing

The table lists, by question, the tool, the kernel facility it uses (from memory, apart from the sweep above) and whether it ran here.

| Question | Tool | Built on | Here |
| --- | --- | --- | --- |
| What programs start? | `execsnoop` | `sched_process_exec` tracepoint | ran |
| What programs exit, and with what status? | `exitsnoop` | `sched_process_exit` | ran |
| What files are opened (and which fail)? | `opensnoop`, `statsnoop` | `sys_enter` and `sys_exit` tracepoints | ran |
| Which system calls and how slow? | `syscount` | `raw_syscalls` | ran |
| Run queue latency | `runqlat`, `runqslower` | `sched` tracepoints | ran |
| Where does a process sleep? | `offcputime`, `wakeuptime` | `sched_switch` | ran |
| CPU time per scheduling episode | `cpudist` | `sched_switch` | ran |
| Interrupt time | `hardirqs`, `softirqs` | `irq` tracepoints | ran |
| Signals | `sigsnoop` | `signal_generate` | ran |
| Block I/O latency histogram | `biolatency` | `block_rq_*` tracepoints | ran |
| Block I/O pattern, sizes | `biopattern`, `bitesize` | block tracepoints | ran |
| Slow file system operations | `fsslower`, `fsdist` | `fentry` or kprobes (varies) | ran |
| TCP connection lifetimes, states, packet latency | `tcplife`, `tcpstates`, `tcppktlat` | `sock` and `tcp` tracepoints | ran |
| Who connects, how long it takes | `tcpconnect`, `tcpconnlat` | kprobes | **failed** (no kprobes) |
| Retransmits | `tcpretrans` (a `bcc` tool; not in the `libbpf-tools` package here) | kprobes, or the `tcp:tcp_retransmit_skb` tracepoint with `bpftrace` | not run |
| Top processes by I/O or by file | `biotop`, `filetop`, `vfsstat` | kprobes | **failed** |
| Functions called, how slow | `funccount`, `funclatency` | kprobes and uprobes | **failed** for kernel functions |
| Memory leaks | `memleak` | `malloc` uprobes and kernel allocation tracepoints | ran (loaded) |
| Who is killed by the OOM killer | `oomkill` | kprobe | **failed** |
| Page cache hits and misses | `cachestat` | kprobes | **failed** |

---

## 7. The method: from a symptom to the probe

Tracing answers **specific questions fast**; it does not tell you which question to ask. Use it after the triage of LX 05 has said which resource is the problem, and go **from cheap and coarse to expensive and detailed**:

1. **Count first.** `syscount`, `bpftrace -e '... @[comm] = count()'`, a `perf stat` software counter. A count costs almost nothing and tells you whether the thing happens at all, and how often. Compare it to a number you can predict.
2. **Then a histogram.** Latency distributions find the slow tail that averages hide: `biolatency`, `runqlat`, your own `hist()` (section 5.4). A bimodal distribution means two different paths.
3. **Then attribute.** Add the key that explains it: by process, by file, by stack, by CPU, by remote address (`@[comm, str(args.filename)] = count()`).
4. **Then look at individual events**, with a filter that keeps the volume low: the outliers of the histogram, one pid, one file. Print only here.
5. **Then the stack.** Where in the code, or why did it sleep: `ustack`, `kstack`, `offcputime`, a flame graph from LX 10.
6. **Prove the fix** by repeating step 2 and comparing the histogram.

### 7.1 Symptom to tool

| Symptom | First question | Tool |
| --- | --- | --- |
| Slow only sometimes | Is it on the CPU, waiting for the CPU, or blocked? | `runqlat`, `offcputime`, a `hist` of the request time with a uprobe |
| High `%sys` | Which system calls, whose? | `syscount -p`, `perf trace -s`, a `bpftrace` count by `comm` and `probe` |
| High `%iowait`, slow disk | Which processes, which sizes, what latency? | `biolatency`, `biosnoop` and `biotop` (kprobes), `bitesize` |
| "Something keeps spawning processes" | What is executed, by whom? | `execsnoop` |
| "Which process touches this file?" | Who opens it? | `opensnoop -n` / a tracepoint with a path filter, or `fatrace`, `inotifywait` |
| Connection failures or slow connects | Which peer, how long, what state? | `tcplife`, `tcpstates`, `tcpconnlat` (kprobes), `ss` |
| Packet loss or retransmits | Where, to whom? | `tcp:tcp_retransmit_skb` with `bpftrace`, `nstat`, `ss -ti` |
| A process stalls, low CPU | What is it waiting on? | `offcputime -p`, `strace -p` (briefly), `/proc/PID/stack`, `/proc/PID/wchan` |
| Memory grows | Who allocates and does not free? | `memleak`, uprobes on `malloc` and `free` (heavy) |
| Unexplained latency spikes | Scheduler, interrupts, or the app? | `runqlat`, `hardirqs`, `softirqs`, a timestamped trace with `ftrace` events |

### 7.2 Three rules

- **Prove the probe works before you trust a zero.** A typo in a filter gives a clean zero (section 3.3).
- **Keep the tracing duration short** and say what the load was while you traced. A 10 second sample of a service at 30 percent CPU is not a statement about peak load.
- **Know what the probe costs** before you attach it to a hot path (section 9). The cost is largest for per event output, uprobes and wide wildcards.

---

## 8. A worked investigation: a request that is slow only when the machine is busy

The symptom: **`parse_request` normally takes 1 to 4 ms and sometimes takes 8 to 16 ms**. The first move is the one in the method: is it computing, waiting for a CPU, or blocked? One `bpftrace` program answers all three at once, for the one process, and it runs twice, on the idle machine and while four busy loops share the two CPUs. The script keeps three things: the time inside `parse_request` (entry to return probe), the **number of times the process was preempted** (`sched_switch` with `prev_state == 0`: the task was still runnable when it was taken off the CPU), and the **scheduler wait** (from wake up or preemption to the next time it runs):

```bt
// reqrunq.bt: for the process named target: time in parse_request(), preemptions, and scheduler wait
uprobe:/tmp/lx11h/target:parse_request
{
    @in[tid] = nsecs;
}

uretprobe:/tmp/lx11h/target:parse_request
/@in[tid]/
{
    @req_us = hist((nsecs - @in[tid]) / 1000);
    delete(@in[tid]);
}

tracepoint:sched:sched_wakeup
/args.comm == "target"/
{
    @qtime[args.pid] = nsecs;
}

tracepoint:sched:sched_switch
/args.prev_comm == "target" && args.prev_state == 0/
{
    @preempted = count();
    @qtime[args.prev_pid] = nsecs;
}

tracepoint:sched:sched_switch
/args.next_comm == "target" && @qtime[args.next_pid]/
{
    @runq_us = hist((nsecs - @qtime[args.next_pid]) / 1000);
    delete(@qtime[args.next_pid]);
}

END
{
    clear(@in);
    clear(@qtime);
}
```

```text
--- 1. the same program, idle machine: request latency, preemptions, run queue wait
@preempted: 1
@req_us:
[512, 1K)             66 |#######################                             |
[1K, 2K)              84 |#############################                       |
[2K, 4K)             147 |####################################################|
[4K, 8K)               3 |#                                                   |
@runq_us:
[4, 8)                 5 |#                                                   |
[8, 16)              100 |###############################                     |
[16, 32)             167 |####################################################|
[32, 64)              29 |#########                                           |
```

On the idle machine: **300 requests, 1 preemption**, request times mostly from 512 µs to 4 ms (**3 slower than 4 ms**), and **scheduler waits of 4 to 64 µs, most of them 8 to 32 µs** (the wake up latency of an idle CPU). Under load:

```text
--- 2. four busy loops share the two CPUs
@preempted: 28
@req_us:
[512, 1K)             74 |#############################                       |
[1K, 2K)              71 |############################                        |
[2K, 4K)             131 |####################################################|
[4K, 8K)              17 |######                                              |
[8K, 16K)              7 |##                                                  |
@runq_us:
[4, 8)                43 |######################                              |
[8, 16)              101 |####################################################|
[16, 32)               1 |                                                    |
[32, 64)               1 |                                                    |
[64, 128)              0 |                                                    |
[128, 256)             0 |                                                    |
[256, 512)             2 |#                                                   |
[512, 1K)              1 |                                                    |
[1K, 2K)               5 |##                                                  |
[2K, 4K)              92 |###############################################     |
[4K, 8K)              73 |#####################################               |
[8K, 16K)              8 |####                                                |
[16K, 32K)             1 |                                                    |
```

**28 preemptions** against 1, the request tail moved from 3 requests above 4 ms to **24 above 4 ms and 7 above 8 ms**, and the **scheduler wait has a new population of 165 waits of 2 to 8 ms** (92 in the 2 to 4 ms bucket and 73 in the 4 to 8 ms bucket), the same shape that `runqlat` showed machine wide. The cause is shown by data, not by guessing: **the slow requests are the ones that were preempted and then waited a time slice for a CPU**; the program's own code did not change. (The 28 preemptions are fewer than the long waits because the wait histogram also counts the wake ups from the 2 ms sleep, which find both CPUs busy as well; I did not separate the two.)

What to do depends on the answer, which is why the investigation ends here and not with a code change: **give the service its own CPUs** (`cpuset` or `taskset`), **raise its priority** (`nice`, a cgroup `cpu.weight`, LX 08), **or move the batch work away** (the container and cgroup tools of LX 08 and LX 09). The number to watch afterwards is the run queue wait histogram, not CPU utilisation: the CPUs were 100 percent busy before and after.

---


## 9. What it costs

Tracing is not free, and the cost is **per event**, so it depends on how often the probe fires and what the program does each time. Two loops measured it: `sysload` makes a million `getppid` calls (the cheapest system call, so the probe's cost is the whole difference) and `ucall` calls one tiny function five million times. Each line is five runs, minimum to maximum:

```text
--- 1. kernel side: ns per getppid() call (5 runs, min to max), 1 million calls each
no tracing:                            99 106
tracefs event enabled (ring buffer):   138 197
bpftrace count() on the tracepoint:    107 199
bpftrace @[comm] = count():            172 295
bpftrace raw_syscalls (every syscall): 124 192
bpftrace entry+exit latency hist:      335 463
strace -f (ptrace):                    52384 53268
```

```text
--- 2. user side: ns per call of f(), 5 million calls
no tracing:                            2 3
bpftrace uprobe on f():                27388 27943
```

Reading it:

- **A system call here costs about 100 ns.** With **no tracing** the run was **99 to 106 ns** per call.
- **A tracepoint with a counter** (`@ = count()`) cost **107 to 199 ns**: roughly **zero to 100 ns more per event**. This is the cost of an eBPF program that does a map update. A **map keyed by `comm`** cost more (**172 to 295 ns**, a hash lookup per event), and the wildcard on **`raw_syscalls`** (every call) was in the same range (**124 to 192 ns**).
- **Enabling the same event in `ftrace`** (writing every event to the ring buffer) cost **138 to 197 ns**, similar to the counter: for a short run, writing a record is as cheap as updating a map, and the real difference arrives when someone must **read** those records (below).
- **Two probes and a map operation for latency** (entry stores the time, exit computes the difference and updates a histogram) cost **335 to 463 ns**: about **230 to 360 ns more than no tracing**, and two to four times the cost of the call itself. Latency tracing of a call that takes 100 ns doubles or triples it.
- **`strace -f` took 52,384 to 53,268 ns per call, about 500 times slower** (494 to 538). `strace` stops the process twice per system call and switches to the tracer each time (`ptrace`). Never attach it to a service you care about, as LX 10 said (330 to 370 times slower there, on a different loop).
- **A uprobe** (entry probe on a function that takes 2 to 3 ns) made each call take **27,388 to 27,943 ns**: **about 27 µs per hit, roughly 10,000 times the cost of the function**. It is the most expensive probe in this tutorial, and the reason section 5.5's measured latency was mostly probe cost. (From memory: on bare metal the cost is about one to a few microseconds, so on real hardware the ratio is still around a thousand.)

**The shape to carry into production:** a tracepoint counter is cheap enough to leave on for a few minutes on most workloads, a latency pair is worth thinking about on anything that makes more than about a hundred thousand calls a second, and a uprobe on a hot function is not an option.

### 9.1 Output volume: the cost nobody budgets

The counter above never leaves the kernel. The way to make tracing expensive and wrong at once is to **print every event**:

```text
--- 1. output volume: one printf per event, 300000 events, 3 runs
run 1: lines printed 110650, lines on stderr other than the timing and the memlock note: 0
run 2: lines printed 107253, lines on stderr other than the timing and the memlock note: 0
run 3: lines printed 92250, lines on stderr other than the timing and the memlock note: 0
```

A `printf` per event on the same tracepoint sent the events to user space through a **per CPU buffer**, and the buffer filled faster than `bpftrace` could drain it. Three runs of 300,000 events printed **110,650, 107,253 and 92,250 lines**: **63 to 69 percent of the events were lost**, and **nothing on standard error said so** (no line other than the timing and the harmless memory limit note). A tool that silently drops two thirds of its data and prints a plausible report is worse than no tool. Now the same count kept in a map:

```text
--- 2. the same count kept in a map instead of printed
@: 300000
```

**300,000, exactly**: the map holds a total updated in the kernel, and only the final number crossed to user space. This is the central design fact of eBPF tracing, and the section 1.2 table in one line: **count and aggregate in the kernel, print the summary**. `ftrace` and `perf trace` are on the wrong side of this line for high rate events, and `bpftrace` is on the right one only if you let the maps do the work. When you do need every event (a rare event, one pid, an error), the **ring buffer** map (`BPF_MAP_TYPE_RINGBUF`, section 4.2) has a documented drop counter; check it.

### 9.2 What the kernel limits

```text
--- 3. the limits the kernel puts around it
$ cat /proc/sys/kernel/unprivileged_bpf_disabled /proc/sys/net/core/bpf_jit_enable /proc/sys/kernel/bpf_stats_enabled | tr '\n' ' '; echo
2 1 0
$ grep -i 'max locked' /proc/self/limits | tr -s ' '
Max locked memory 8388608 8388608 bytes
$ cat /sys/kernel/tracing/buffer_size_kb | head -1; cat /sys/kernel/tracing/buffer_total_size_kb
1410
2820
```

`unprivileged_bpf_disabled = 2`: **ordinary users cannot load BPF programs**, and the setting cannot be loosened without a reboot (from memory: 0 allows all users, 1 disables until reboot, 2 disables but lets root re-enable). `bpf_jit_enable = 1`: programs are compiled to native code. `bpf_stats_enabled = 0`: the per program timing of section 4.5 is off unless you turn it on. **`Max locked memory 8,388,608`** (8 MiB): older kernels charged BPF maps and programs against this limit, which is why `bpftrace` prints `couldn't set RLIMIT_MEMLOCK` and why a large map can fail to load with `Operation not permitted` (from memory: kernels 5.11 and later account maps to the memory cgroup instead). And the `ftrace` buffer: **1,410 KB per CPU, 2,820 KB total**, which at about 100 bytes per event holds roughly **28,000 events** before it overwrites the oldest (from memory of the event size; section 2.5 shows the overrun counter).

### 9.3 Tracing in production: a checklist

1. **What can it break?** The verifier guarantees the program **terminates, does not crash the kernel and does not touch memory it should not**. It does not guarantee that the **cost is acceptable**. A correct program can still cost 10 percent of a busy server, as the table above shows.
2. **Start narrow and short.** Attach to one pid or one cgroup, keep the filter in the probe, run for 10 to 60 seconds with `timeout` or `interval:s:N { exit(); }`, not forever. On a fleet, one host first.
3. **Check the count** at the point of the probe (events per second) before you add the action: a probe that fires a million times a second needs a map and no `printf`.
4. **Measure the cost on the host.** `kernel.bpf_stats_enabled=1` for a minute gives `run_time_ns` and `run_cnt` per program (section 4.5). `perf stat` on the traced process, or the request latency of the service, shows the effect.
5. **Prefer a tracepoint to a kprobe**, a kprobe to a uprobe on a hot function, and a sampling probe (`profile:hz:99`) to a probe on every call, when the sampling answers the question.
6. **Know that tracing reads everything.** A program can read any process's memory and the contents of every system call argument (file names, buffers, in some cases passwords in a command line). Treat the **privilege to load BPF** (`CAP_BPF` and `CAP_PERFMON` on kernels 5.8 and later, from memory) as close to root, grant it to few people, and **do not ship the output** of a trace of a production system to a ticket without looking at what is in it.
7. **Confirm it is gone.** `bpftool prog show`, `bpftool link show` and the `tracefs` files (`events/*/enable`, `uprobe_events`) show what is still attached. A forgotten `ftrace` event or pinned BPF program keeps costing.
8. **Know the failure modes on your kernel before an incident.** Run the tools you will need once, on a staging host of the same image. On this VM a third of the standard tools failed (section 6.2); you want to learn that on a calm day.

### 9.4 The costs in one table

| Item | Measured here | Notes |
| --- | --- | --- |
| A system call, no tracing | 99 to 106 ns | the baseline |
| Tracepoint plus `count()` | 107 to 199 ns | roughly 0 to 100 ns more per event |
| Tracepoint plus `@[comm] = count()` | 172 to 295 ns | one hash lookup per event |
| `ftrace` event into the ring buffer | 138 to 197 ns | plus the cost of reading the text |
| Entry and exit probes with a histogram | 335 to 463 ns | 230 to 360 ns more |
| `strace -f` | 52,384 to 53,268 ns | about 500 times slower |
| A uprobe on a function | 27,388 to 27,943 ns per hit | about 27 µs |
| A BPF program that does a lookup, a ring buffer record and a few helpers | about 1.9 µs per run | from `bpf_stats` (section 4.5) |
| `printf` per event, 300,000 events from a tight loop | 63 to 69 percent of events lost, silently | use a map |
| A map update in the kernel | the count was exact (300,000 of 300,000) | |
| `perf record` software sampling | see LX 10 | cost is per sample, not per event |
| Memory | a few KB for a small program, per CPU maps scale with the CPU count | from memory |

---

## 10. Common mistakes

1. **Trusting a zero.** A probe with a misspelled filter, a wrong path or a probe that never fires prints `0` or nothing (section 3.3). Test the probe on something that must happen.
2. **A `printf` per event on a busy probe.** It lost 63 to 69 percent of 300,000 events here, with no warning. Aggregate in a map.
3. **Measuring the cost of the measurement.** A uprobe pair around a 3 system call function showed 43 µs, most of it the probes (section 5.5). Time work that takes milliseconds, or use sampling.
4. **Assuming a kernel function exists as a probe.** Functions are renamed and inlined, and some kernels have no kprobes at all (this one). Use a tracepoint first and check `bpftrace -l`.
5. **Believing a list of probes proves they attach.** The 57 glibc USDT probes were listed and the attach failed (section 3.4).
6. **Reading strings at system call entry.** The first access to a path can read as empty (sections 4.2 and 5.3). Count with the entry probe, read strings at the exit or with a library probe.
7. **A wildcard that attaches everything.** `sys_enter_*` attached 361 probes. It is fine for a 10 second count with a pid filter and a large cost on a busy machine without one.
8. **Forgetting the pid filter.** The first version of the count in section 5.2 counted the tool's own calls (`bpf`, `ioctl`) and everyone else's. Filter on `pid == cpid` or `$1`.
9. **Leaving map entries behind.** `@start[tid]` without a `delete` grows with every thread that exits between the two probes. Delete, and clear in `END`.
10. **User stacks without frame pointers, or after the process exited.** Hex addresses instead of names; the process must still exist when the stack is printed (section 5.6).
11. **Running `strace` on a live service.** About 500 times slower per call here (section 9). Use a tracepoint count, `perf trace` for a short time, or `syscount -p`.
12. **Treating a quiet tool as healthy.** "Ran without error for two seconds" meant only that it attached (section 6.2). Run the tool against something that provokes the event.
13. **Not knowing what the verifier rejected.** Read the log from the bottom: the instruction, the register type (`map_value_or_null`, `scalar`) and the range. Fix the check, mask or bound, not the symptom.
14. **Loading a BPF program or opening a trace in production without a cost estimate.** Count events per second first; budget the cost per event (section 9); stop after a minute.
15. **Forgetting to remove it.** A pinned program or an enabled `ftrace` event outlives the shell that started it. `bpftool prog show` and the `enable` files.

---

## 11. Questions out loud

1. **What is the difference between a tracepoint, a kprobe and a uprobe?** A tracepoint is a stable hook placed by kernel developers (2,305 here); a kprobe is a breakpoint on any kernel function, with no stability promise (none here); a uprobe is a breakpoint in a user program (about 27 µs per hit here). Prefer the stable one that shows what you need.
2. **What is `ftrace`, and how do you use it with no tools?** The kernel's built in tracer, controlled through files in `tracefs`: `echo` to `events/.../enable` and `filter`, `cat trace` or `trace_pipe`, write your own lines to `trace_marker`, create uprobes through `uprobe_events`. The function tracers (`function`, `function_graph`) trace kernel functions, from memory; they were refused on this VM.
3. **What is eBPF in a sentence, and why is it safe?** Small programs loaded into the kernel at hooks, with maps for state and helpers for services; safe because a **verifier** proves, before the program runs, that it terminates, stays in bounds and checks its pointers.
4. **What does the verifier reject, with an example?** An unchecked pointer from a map lookup (`map_value_or_null`), a dereference of a number as a pointer (`scalar`), an index that can leave the value (`off=255` for an 8 byte value), and a loop it cannot bound (1,000,001 instructions processed). It tracks types and ranges of every register on every path.
5. **What is CO-RE, and what problem does it solve?** Compile once, run everywhere: the program names fields (`task->real_parent->tgid`), and `libbpf` rewrites the offsets at load time using the running kernel's BTF, so a tool built once works on many kernels and needs no kernel headers. It is why `libbpf-tools` work here and the `bcc` Python tools, which compile at start up, do not.
6. **`bcc` against `libbpf-tools` against `bpftrace`?** `bcc`: Python plus C compiled at run time, needs headers and a compiler. `libbpf-tools`: precompiled CO-RE binaries, small and fast to start. `bpftrace`: a one line language for a new question. Use a maintained tool if one exists, `bpftrace` to explore.
7. **How do you measure the latency of a system call with `bpftrace`?** Store `nsecs` in a map keyed by `tid` at `sys_enter_X`; at `sys_exit_X` compute the difference into a `hist` and `delete` the key; filter both probes to the process. Check a count against a known number first.
8. **Why is a histogram better than an average?** A latency distribution is usually a fast mode plus a slow tail (484 of 501 reads under 1 µs here, a few at 256 to 512 µs); the mean hides the tail, which is what users feel.
9. **How do you find why a process is slow when the CPU is idle?** It is waiting: `offcputime -p` (the stacks where it sleeps and for how long), `runqlat` if it waits for a CPU, `syscount -L` for slow system calls, `biolatency` for disk. Here 938 ms of a 1 s window was in `usleep`.
10. **What does `runqlat` tell you that CPU utilisation does not?** The time between becoming runnable and running. With four busy loops on two CPUs, waits moved from under 64 µs to 2 to 8 ms, and utilisation was 100 percent in both cases.
11. **What does it cost to trace, and what do you do about it?** A tracepoint counter about 0 to 100 ns per event, latency pairs about 230 to 360 ns, a uprobe about 27 µs, `strace` about 500 times slower, a `printf` per event silently drops most events. Aggregate in the kernel, filter in the probe, start narrow and short, measure `bpf_stats`.
12. **A kernel without kprobes: what do you do?** Use tracepoints (`sched`, `syscalls`, `tcp`, `block`), uprobes on libraries, software sampling (`profile`, `perf`), and the tracepoint based tools: 28 of 54 `libbpf-tools` ran here. Say what you could not measure.
13. **Is tracing safe to run in production?** Safe from crashes by construction, not from cost or from information exposure: budget the cost per event, keep it short, limit who may load BPF, and check what the output contains.

---

## 12. Problems

**Problem A. The budget for tracing system calls.** A service makes 200,000 system calls per second per CPU, each costing about 100 ns. Using the measured costs (no tracing 99 to 106 ns, entry and exit latency probes 335 to 463 ns, `strace -f` 52,384 to 53,268 ns), how much extra does the latency program add per call, what share of one CPU does that take at this rate, and what would `strace` need? Which tracing method do you choose?

**Problem B. The budget for a uprobe.** A uprobe costs 27,388 to 27,943 ns per hit; the function takes 2 to 3 ns. How many calls per second fill one CPU with probe work, and what share of a CPU does a probe on a function called 100,000 times a second use? What do you do instead to find where that function's callers spend time?

**Problem C. Events lost.** Three runs of a `printf` per event on 300,000 events printed 110,650, 107,253 and 92,250 lines and gave no warning. What share was lost in each run, how would you have noticed without comparing to a known count, and what program do you write instead?

**Problem D. Read the histograms.** On the busy run, the run queue wait histogram has bucket counts 43, 101, 1, 1, 0, 0, 2, 1, 5, 92, 73, 8 and 1, where the buckets are 4 to 8 µs, 8 to 16, 16 to 32, 32 to 64, 64 to 128, 128 to 256, 256 to 512, 512 µs to 1 ms, 1 to 2, 2 to 4, 4 to 8, 8 to 16 and 16 to 32 ms. The idle run had 5, 100, 167 and 29 in the first four buckets. What share of the busy waits were 2 ms or longer, what does the idle run show, what share of the 300 requests exceeded 4 ms in each run (3 idle, 24 busy), and what is the most likely cause and the fix?

**Problem E. Pick the tool.** For each case give the tool, the output you read, and what you would do if the kernel has no kprobes. (1) A service restarts child processes and you do not know which. (2) Disk reads of one process are slow only at night. (3) You need the latency of every `accept` of a server without slowing it much. (4) A BPF program fails to load with `R0 invalid mem access 'map_value_or_null'`. (5) `opensnoop` shows a file name as empty for some opens. (6) You must find which function of your own program is slow, called 50,000 times a second.

---

## 13. Answers

```text
--- A. the budget for tracing a syscall heavy process (200,000 calls per second)
added per call by the histogram probes: 229 to 364 ns
CPU added at 200000 calls/s: 4.6 to 7.3 percent of one CPU
slowdown of the call under strace: 494 to 538 times
CPU seconds per second that strace would need at 200000 calls/s: 10.5 to 10.6 (the machine has 2 CPUs)
--- B. the budget for a uprobe
cost per hit: 27385 to 27941 ns
calls per second that fill one CPU: 35789 to 36516
CPU used by a uprobe on a function called 100000 times a second: 274 to 279 percent of one CPU
--- C. events lost by printing every event
delivered 110650 of 300000: lost 63.1 percent
delivered 107253 of 300000: lost 64.2 percent
delivered 92250 of 300000: lost 69.2 percent
--- D. the run queue wait histograms (busy run)
busy: 328 waits, 174 of 2 ms or more: 53.0 percent
idle: 301 waits, none above 64 us
requests above 4 ms: idle 3 of 300 (1.0 percent), busy 24 of 300 (8.0 percent)
preemptions per request: idle 0.003, busy 0.093
```

**Answer A.** The latency program adds **229 to 364 ns per call**: at 200,000 calls a second that is **4.6 to 7.3 percent of one CPU**, a cost you can accept for a diagnosis of a few minutes and should confirm with the service's own latency. `strace -f` makes each call **494 to 538 times slower**; to keep up with 200,000 calls it would need **about 10.5 CPU seconds per second**, which is five times the whole VM, so the traced process would run about 500 times slower. Choose the **tracepoint with a map** (a count, or the histogram if you need latency), `syscount -p`, or sampling; use `strace` only on a quiet process for a few seconds.

```text
--- B. the budget for a uprobe
cost per hit: 27385 to 27941 ns
calls per second that fill one CPU: 35789 to 36516
CPU used by a uprobe on a function called 100000 times a second: 274 to 279 percent of one CPU
--- C. events lost by printing every event
delivered 110650 of 300000: lost 63.1 percent
delivered 107253 of 300000: lost 64.2 percent
delivered 92250 of 300000: lost 69.2 percent
--- D. the run queue wait histograms (busy run)
busy: 328 waits, 174 of 2 ms or more: 53.0 percent
idle: 301 waits, none above 64 us
requests above 4 ms: idle 3 of 300 (1.0 percent), busy 24 of 300 (8.0 percent)
preemptions per request: idle 0.003, busy 0.093
```

**Answer B.** A uprobe costs **27,385 to 27,941 ns** net of the function, so one CPU is full of probe work at **35,789 to 36,516 calls a second**. At 100,000 calls a second the probe would need **274 to 279 percent of a CPU**: it cannot run: the traced process would slow down by more than three times. Instead sample the program (`profile:hz:99` with `ustack`, section 5.6, or `perf record`, LX 10) and look at the stacks, or probe the **caller** at a lower rate, or time a coarser unit of work. A uprobe is for rare calls and for functions that take milliseconds.

```text
--- C. events lost by printing every event
delivered 110650 of 300000: lost 63.1 percent
delivered 107253 of 300000: lost 64.2 percent
delivered 92250 of 300000: lost 69.2 percent
--- D. the run queue wait histograms (busy run)
busy: 328 waits, 174 of 2 ms or more: 53.0 percent
idle: 301 waits, none above 64 us
requests above 4 ms: idle 3 of 300 (1.0 percent), busy 24 of 300 (8.0 percent)
preemptions per request: idle 0.003, busy 0.093
```

**Answer C.** **63.1, 64.2 and 69.2 percent** were lost. Without a known count you would notice only by the symptom: the printed totals do not agree with another source (`syscount`, a map count), the line count is **not proportional to the load** or changes between identical runs (here **92,250 to 110,650** for the same 300,000 events), or the tool's own lost event counter reports it (this `bpftrace` printed none). The program to write is the **map version**: `@ = count()` or `@[comm] = count()` read at the end, which was **300,000 of 300,000**, and for rare events a ring buffer program that checks its drop counter.

```text
--- D. the run queue wait histograms (busy run)
busy: 328 waits, 174 of 2 ms or more: 53.0 percent
idle: 301 waits, none above 64 us
requests above 4 ms: idle 3 of 300 (1.0 percent), busy 24 of 300 (8.0 percent)
preemptions per request: idle 0.003, busy 0.093
```

**Answer D.** In the busy run **174 of 328 waits (53.0 percent)** were **2 ms or longer** (`92 + 73 + 8 + 1`); in the idle run **none of 301 exceeded 64 µs**. The share of requests above 4 ms rose from **1.0 percent to 8.0 percent**, and preemptions from **0.003 to 0.093 per request**. The most likely cause is **CPU contention**: the 2 to 8 ms waits are the length of a scheduler time slice, so a thread that wakes finds both CPUs running another task and waits for a slice to end. The program's code did not change. The fix is outside the program: **dedicated CPUs** (`cpuset`, `taskset`, isolation), a higher **priority or cgroup `cpu.weight`**, or moving the batch work off the machine. After the change, repeat the histogram and compare the tail.

**Answer E.**

1. **`execsnoop`** (`sched_process_exec`, a tracepoint, ran here): one line per `exec` with the arguments and the parent pid; add `-n` or a name filter. No kprobes needed.
2. **`biolatency`** (block tracepoints, ran here) for the latency histogram, `biosnoop` for individual requests (kprobes, failed here, so use `bpftrace` on `block:block_rq_issue` and `block_rq_complete` keyed by sector or request), plus `iostat -x`. Run it at night, for 60 seconds, and compare to the day histogram.
3. **`bpftrace`** with `tracepoint:syscalls:sys_enter_accept4` and `sys_exit_accept4` and the `@start[tid]` pattern (section 5.4), with a histogram; filter to the pid; about 230 to 360 ns per call, which is nothing at the rate of `accept`. `tcpconnlat` uses kprobes and fails here.
4. A **missing NULL check** after `bpf_map_lookup_elem`: add `if (!v) return 0;` before the dereference. Read the log from the bottom to find the instruction. (A constant key on an array map passes without the check, section 4.4.)
5. The **first access to the string** at syscall entry (section 5.3): the page had not been touched and a probe cannot fault it in. Use the exit probe with a saved pointer, or `opensnoop`, which printed all four opens in section 6; count with the entry probe.
6. **Sampling**, not a uprobe: `profile:hz:99 /pid == $1/ { @[ustack] = count(); }` or `perf record -g`; a uprobe at 50,000 calls a second would use about 137 percent of a CPU (50,000 times 27 µs). Probe a caller at a lower rate if you need the call counts.

---

## 14. Cheat sheet

```text
SOURCES     tracepoint (stable, 2,305 here) | raw tracepoint | kprobe/kretprobe (none here) | fentry (none)
            uprobe/uretprobe (~27 us/hit here) | USDT (stable; listed 57 in glibc, attach failed here)
            software events, profile:hz:N | hardware (PMU needed, none here)
FTRACE      mount -t tracefs nodev /sys/kernel/tracing ; cd there
            echo 1 > events/sched/sched_switch/enable ; cat trace | trace_pipe ; echo 0 > .../enable
            echo 'filename.ustring ~ "*passwd*"' > events/syscalls/sys_enter_openat/filter   (user strings need .ustring)
            echo 'p:grp/name /path/bin:0xOFFSET' >> uprobe_events ; echo "text" > trace_marker
            set_event_pid, buffer_size_kb, tracing_on, per_cpu/cpuN/stats (overrun)
            function, function_graph, trace-cmd record: refused here (from memory elsewhere)
EBPF        source -> clang -target bpf -> ELF + BTF -> bpf() load -> VERIFIER -> JIT -> hook -> maps
            map types: hash, array, per CPU, stack trace, ringbuf ; helpers: pid, ktime, probe_read_user_str
            CO-RE: BPF_CORE_READ(t, real_parent, tgid) ; vmlinux.h from /sys/kernel/btf/vmlinux
            bpftool prog show | map show | link show | prog dump xlated ; sysctl kernel.bpf_stats_enabled=1
VERIFIER    map_value_or_null = check NULL ; scalar = use bpf_probe_read ; off= size= = mask the index
            1000001 insn = bound the loop ; read the log from the bottom ; -O2 always
BPFTRACE    probe /filter/ { action }   -e 'prog' | file.bt | -c 'cmd' (cpid) | $1 | -l 'glob' | -lv
            count() sum() avg() min() max() stats() hist() lhist()   @m[key]   $scratch
            latency: @s[tid] = nsecs ... hist(nsecs - @s[tid]) ; delete(@s[tid])
            stacks: @[ustack] @[kstack] (process alive; frame pointers) ; profile:hz:99 ; interval:s:N { exit(); }
TOOLS       execsnoop opensnoop statsnoop syscount runqlat runqslower offcputime cpudist hardirqs softirqs
            biolatency tcplife tcpstates sigsnoop memleak   (ran here: 28 of 54; 23 need kprobes)
COST        count 0-100 ns ; hist pair 230-360 ns ; ftrace event ~40-100 ns ; uprobe ~27 us ; strace ~500x
            printf per event: 63-69 percent lost, no warning -> aggregate in a map
METHOD      count -> histogram -> attribute -> individual events (filtered) -> stack -> prove the fix
SAFETY      short, narrow, filtered, measured ; CAP_BPF is nearly root ; check what is in the output ; remove it
NOT RUN HERE: kprobes, fentry, function and function_graph tracers, trace-cmd record, histogram triggers,
              bcc Python tools, USDT attach, PMU based tools, tcpconnect and the other 22 kprobe tools
```

---

## 15. Sources

- Linux kernel documentation, *ftrace: Function Tracer* and *Event Tracing*, *Uprobe-tracer*, *Kprobe-based Event Tracing* and *Using the Linux Kernel Tracepoints*: `tracefs`, events, filters, `uprobe_events`, `trace_marker`. <https://docs.kernel.org/trace/ftrace.html>
- Linux kernel documentation, *BPF* (the verifier, `BPF_MAP_TYPE_*`, helpers, `bpf_stats`, BTF, `libbpf` overview). <https://docs.kernel.org/bpf/> and the `bpf(2)` and `bpf-helpers(7)` manual pages.
- *BPF CO-RE reference guide* and *BPF Portability and CO-RE*, Andrii Nakryiko: the meaning of `BPF_CORE_READ`, relocations and BTF. <https://nakryiko.com/posts/bpf-portability-and-co-re/>
- `libbpf` and `libbpf-bootstrap` (examples of skeleton based programs); `bpftool(8)`. <https://github.com/libbpf/libbpf-bootstrap>
- `bpftrace` reference guide and one-liner tutorial. <https://github.com/bpftrace/bpftrace/blob/master/docs/reference_guide.md> and <https://github.com/bpftrace/bpftrace/blob/master/docs/tutorial_one_liners.md>
- The `bcc` repository and the `libbpf-tools` directory: the list of tools, their options, and the notes on which probes each uses. <https://github.com/iovisor/bcc>
- Brendan Gregg, *BPF Performance Tools* (2019) and *Systems Performance*, 2nd edition (2020): the tool method, the one-liners, the observability diagrams; and his pages on `ftrace`, `bpftrace` and `perf`. <https://www.brendangregg.com/ebpf.html>
- Brendan Gregg, *Linux Performance Analysis: perf-tools* (the `ftrace` based `iosnoop`, `execsnoop`, `funccount`). <https://github.com/brendangregg/perf-tools>
- Sasha Goldshtein, *Linux tracing workshop*: labs on `ftrace`, `perf`, `bcc`. <https://github.com/goldshtn/linux-tracing-workshop>
- *Systemtap SDT / `sys/sdt.h`* and the glibc manual on its probes (the `libc` provider names listed in section 3.4). <https://sourceware.org/glibc/wiki/Tracing>
- Linux kernel documentation, *Linux Security: BPF and lockdown* and `sysctl` documentation for `kernel.unprivileged_bpf_disabled` and `kernel.bpf_stats_enabled`.
