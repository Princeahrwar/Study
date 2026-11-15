# LX 03: Processes, signals and `/proc`

*Part of the [Linux plan](00_plan.md) · Time: about 5 hours · You need: [OS 02](../05_operating_systems/02_processes.md) (the process abstraction, `fork`, `exec`, `wait`), [OS 17](../05_operating_systems/17_ipc_fundamentals.md) (signals and handlers) and [LX 01](01_shell_and_scripting.md) (job control, exit statuses, `trap`). The OS tutorials say what a process **is**; this one is about what you can **see, send and change** on a running Linux machine.*

"A process is hung, slow, leaking or will not die: what do you do?" is asked in system-software, infrastructure and networking loops alike. The answer is not a tool name. It is a short chain of facts: what **state** the process is in and why, what it is **waiting on**, which **files and sockets** it holds, what **limits** it runs under, what **signals** it has blocked or caught, and who its **parent** is. Every one of those facts is a file under `/proc` or a column of `ps`, and this tutorial makes you produce each of them on a live process and read it.

The sequence is: what a process is to the kernel and the seven states (section 1), how processes are created, end, become zombies and orphans (2), `ps` and `top` and the numbers they print (3), signals from generation to delivery (4), terminals, sessions and job control (5), `/proc/PID` file by file (6), resource limits (7), the load average and pressure stall information (8), scheduling knobs (9), daemons, subreapers and PID 1 in a container (10), "who has this port, who has this file" (11), a triage playbook (12), and then the costs, mistakes, questions and five problems with checked answers.

**Five honest limits, stated first.** (1) Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, about 8 GiB of memory, 7.8 GiB reported), running as **root**, with procps-ng 4.0.4, util-linux, lsof, psmisc, gdb and bash 5.2.21. (2) **PID 1 here is not `systemd`**: it is a small program (`process_api`) that reaps orphans but does not supervise services, so section 10 reports what *this* PID 1 did and says what `systemd` does only from memory; boot and `systemd` are [LX 04](00_plan.md). (3) These tools are **not installed** in the VM: `htop`, `ss`, `pidstat`, `mpstat`, `ltrace`, `stress-ng`, `netstat`. Where the answer would normally come from `ss` or `pidstat`, I read `/proc` by hand, which is also what those tools do. (4) Timings and CPU shares are **single runs** and vary by 10 to 30 percent between runs; timings of 0.1 s steps are taken from a VM whose clock source is the hypervisor's. (5) Being root changes some results (resource limits are not enforced on root the same way; section 7 shows a non-root run for that reason).

By the end you can:

- name the process states and say what each means (measured: a busy loop is `R`; `sleep` is `S` with `wchan` `hrtimer_nanosleep`; the parent of a `vfork` is **`D`** with `wchan` `kernel_clone`; a child that exited unreaped is `Z`; `SIGSTOP` gives `T`; a debugger gives **`t` (tracing stop)** and a `TracerPid`);
- explain why `kill -9` cannot remove a zombie and what does (measured: `kill -9` returned status 0 and the zombie stayed; it vanished when its parent exited and PID 1 reaped it);
- read `ps` and `top` correctly (measured: a process that burned the CPU for 2 s and then slept showed `%CPU` **21.6 in `ps`** and **0.0 in `top`**, because `ps` divides CPU time by lifetime and `top` by the last interval);
- state the rules that decide whether a signal is lost (measured: five `SIGUSR1` sent while blocked ran the handler **once**, five `SIGRTMIN` ran it **five times**, in order);
- predict `EINTR` against `SA_RESTART` (measured: a blocking `read` returned `EINTR` after 1.0 s without `SA_RESTART` and completed after 2.0 s with it, while `sleep(3)` with `SA_RESTART` still returned early with 1 s left);
- say what `fork` and `exec` do to the signal mask, dispositions and pending set (measured with the `SigBlk`, `SigIgn`, `SigCgt`, `ShdPnd` masks of `/proc`);
- translate exit statuses (measured: `SIGTERM` is 143, `SIGKILL` 137, `SIGPIPE` 141, `abort()` 134 with a core file of 356,352 bytes only when `ulimit -c` allowed it);
- show how a terminal turns keys into signals (measured on a pseudo-terminal: `^C` gave `INT`, `^\` gave `QUIT`, `^Z` gave `TSTP`, closing the master gave `HUP`, and `stty -isig` delivered the same bytes as data);
- find a process's files, memory, threads, limits, cgroup and namespaces in `/proc/PID` (measured, for a process with 64 MiB mapped and 4 MiB touched: **VSZ 76,456 kB, RSS 5,984 kB**);
- explain the load average (measured: with three busy loops the 1-minute value followed `L = L*e + n*(1-e)`, `e = exp(-5/60)`, to within 0.01 at every 5 s sample) and why `D`-state tasks count in it;
- use `nice`, `chrt`, `taskset` and `ionice` and know what each does not do (measured: on one CPU, nice 0 against nice 10 split **542 against 58 ticks, 90% and 10%**, and on two CPUs nice made no difference);
- say what PID 1 of a PID namespace must do (measured: `SIGKILL` sent to PID 1 from inside its own namespace was ignored; an orphan stayed a zombie because that PID 1 never waited);
- find the owner of a port or a file and the process holding a deleted file (measured: a deleted 64 MiB file left `df` unchanged until it was truncated through `/proc/PID/fd/N`).

**Where the numbers come from.** Twelve experiment scripts plus a problem-answer script were run in this session on Linux 6.18.44 (2 vCPUs, about 8 GiB). Transcripts print `$ command` before each command's output. Numbers that are not from these runs are marked "from memory" or come from a source listed at the end.

**Data and timing.** Nothing here needs a data set. The test programs are small C and Python files (`vforkd`, `zombie`, `burst`, `sigdemo`, `procdemo`, `crash`, `mini_init.py` and others), printed where they are used. Each experiment names its timing (for example "after 3 s"); treat CPU-time ratios as a range.

---

## 1. What a process is to the kernel, and the states it can be in

To the kernel, a **task** is the unit that is scheduled: a `task_struct` (see [OS 02](../05_operating_systems/02_processes.md)). A *process* is a task plus the things it does not share with other processes: an address space (`mm`), a table of open files, signal handlers, credentials, limits. A **thread** is a task that *shares* some of these with others by `clone`, so every thread has its own **TID** and all threads of a process share the same **TGID**, which is what `getpid()` and the PID you see in `ps` mean. `/proc/PID/task/TID` has one directory per thread (measured in section 6: the leader has TID equal to PID, the other thread had a different one).

The identifiers `ps` can print:

| Field | Meaning | `ps` column |
| --- | --- | --- |
| PID / TGID | process id (the thread group id) | `pid` |
| TID | task id of one thread | `lwp` |
| PPID | parent; **changes** when the parent dies (adoption, section 10) | `ppid` |
| PGID | process group, the target of `kill -SIG -PGID` and of terminal signals | `pgid` |
| SID | session, the set of groups under one controlling terminal | `sid` |
| TPGID | the foreground process group of the controlling terminal | `tpgid` |

### 1.1 The states, each produced on purpose

`ps` prints one letter of the state in the `STAT` column, from `/proc/PID/stat`. I made a process in each state and read it with `ps -o ...`, `/proc` and `gdb`. The `D` state needed a trick, described below the transcript.

```text
--- R: running or runnable (a busy loop), S: interruptible sleep (sleep)
PID  PPID  PGID   SID STAT WCHAN                  COMMAND
465   458   455   453 R    -                      bash -c while :; do :; done
466   458   455   453 S    hrtimer_nanosleep      sleep 30

--- D: the parent of a vfork() waits uninterruptibly (killable) for its child
PID  PPID  PGID   SID STAT WCHAN                  COMMAND
470   458   455   453 D    kernel_clone           vforkd 5
472   470   455   453 S    hrtimer_nanosleep      vforkd 5
State:  D (disk sleep)
kernel_clone
[<0>] kernel_clone+0x1af/0x3e0
[<0>] __do_sys_vfork+0x43/0x70
[<0>] do_syscall_64+0x71/0x280

--- Z: a child that exited and has not been waited for
PID  PPID  PGID   SID STAT WCHAN                  COMMAND
481   479   455   453 Z    -                      [zombie] <defunct>
State:  Z (zombie)
PPid:   479
kill -9 on the zombie: status 0
481 Z    [zombie] <defunct>
and /proc/PID/cmdline of a zombie: []
after the parent exited: zombie still in the process table = 0 (checked every 0.5 s, up to 3 times); PID 1 adopted and reaped it

--- T: stopped by SIGSTOP, then continued
PID STAT COMMAND
504 T    sleep 30
State:  T (stopped)
504 S    sleep 30

--- t: a tracer (gdb) has stopped the process
State:  t (tracing stop)
TracerPid: (the gdb process)
514 t    sleep 30
after gdb detached: State:      S (sleeping)

--- the state letters and flags in ps STAT
ps -o stat for this shell:  S
(s = session leader, + = foreground process group, l = multithreaded, < = high priority, N = low priority, L = pages locked)
```

What each state means, with the evidence above:

| State | Name in `/proc/PID/status` | What it means | How it was produced |
| --- | --- | --- | --- |
| `R` | running | on a CPU **or** in a run queue waiting for one | an endless `bash` loop |
| `S` | sleeping | waiting for an event, **can** be woken by a signal | `sleep 30`, `wchan` `hrtimer_nanosleep` |
| `D` | disk sleep | waiting, **not** woken by an ordinary signal (uninterruptible) | the parent of a `vfork` |
| `T` | stopped | stopped by a job-control signal | `kill -STOP`, back to `S` after `kill -CONT` |
| `t` | tracing stop | stopped under a debugger or tracer | `gdb -p PID -batch`, with `TracerPid` set |
| `Z` | zombie | exited, **not yet waited for** by its parent | a child that called `_exit(7)` while the parent slept |
| `X` | dead | being removed; you almost never see it | not produced |

**The `D` state is not only disk.** The name in `status` is "disk sleep" and the common story is "waiting for I/O", but the kernel uses uninterruptible sleep for any wait that must not be broken halfway. My experiment used none of the disk: the **parent of a `vfork` sleeps until its child execs or exits**, and it showed `D` with `wchan` `kernel_clone` and a kernel stack of `kernel_clone`, `__do_sys_vfork`, `do_syscall_64`. The program is two lines of logic:

```c
int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    pid_t pid = vfork();
    if (pid == 0) {          /* child: shares the parent's memory; the parent is suspended */
        sleep(secs);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    return 0;
}
```

Three things to take away. **One**: a `D` process ignores `SIGTERM`; it acts on it when the wait ends. (From memory: a `D` wait that uses the "killable" variant can be ended by `SIGKILL`; the `vfork` wait is such a case. A wait on a hung NFS server or a failing disk may not be, and then only fixing the cause or rebooting helps.) **Two**: the `wchan` column and `/proc/PID/stack` tell you **what it is stuck in**; `stack` needs root. **Three**: `D` counts in the load average (section 8), which is why a machine with idle CPUs can show a high load.

**`t` is not `T`.** A stop by `SIGSTOP`/`SIGTSTP` is `T`. A stop *by a tracer* is `t`, and `TracerPid` in `/proc/PID/status` names the tracer. I first tried `strace -p` for this and it did not leave the process in state `t` while attached (I did not investigate why); `gdb -p` stops it, which is what the transcript shows. A process stuck in `t` with no live tracer is rare and means the tracer died while stopped (from memory).

**Where to read the state without `ps`:** `/proc/PID/stat` field 3 (one letter), `/proc/PID/status` line `State:`. The `STAT` column adds flags: `s` session leader, `+` foreground process group of its terminal, `l` multithreaded, `<` high priority, `N` low priority, `L` locked pages.

---

## 2. Creating and ending processes: wait, zombies and orphans

The mechanics (`fork` copies the task, `exec` replaces the program, `exit` ends it, the parent collects the status with `wait`) are in [OS 02](../05_operating_systems/02_processes.md). What a Linux operator must know on top of that:

**A process that exits becomes a zombie until its parent calls `wait`.** The kernel keeps the exit status and the accounting (a small `task_struct`, no memory, no open files) so that the parent can read them. A zombie cannot be killed because it is already dead: in the experiment `kill -9` on the zombie returned **status 0** (the signal was accepted) and the zombie stayed in state `Z`, with `ps` showing `[zombie] <defunct>` and an **empty** `/proc/PID/cmdline`. What removes it is the parent calling `wait`, or **the parent exiting**, after which the zombie is **adopted** (reparented) and its new parent reaps it; in the run PID 1 did this within the first 0.5 s poll. The zombie program:

```c
int main(int argc, char **argv) {
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    pid_t pid = fork();
    if (pid == 0) _exit(7);
    printf("parent %d child %d\n", (int)getpid(), (int)pid);
    fflush(stdout);
    sleep(secs);             /* the child is a zombie for this long */
    return 0;
}
```

**The fix for a zombie leak is in the parent, not the zombie.** The parent must `wait` (or ignore `SIGCHLD` explicitly with `SIG_IGN`, or set `SA_NOCLDWAIT`, both from memory, which tell the kernel not to keep status at all). A handful of zombies is harmless; the cost is the **PID** (and the process-table slot). A parent that leaks one zombie per request will eventually hit the PID limit (`/proc/sys/kernel/pid_max`, from memory) or `RLIMIT_NPROC` and `fork` fails with `EAGAIN` (section 7).

**An orphan is a live child whose parent died.** It is not a problem by itself: the kernel reparents it to PID 1 or to the nearest ancestor that declared itself a **subreaper** (section 10), and that process reaps it when it exits. It matters in two cases: a daemon is *made* by orphaning on purpose (section 10), and inside a container PID 1 might not know how to reap (section 10).

**The exit status.** `wait` returns one word that holds either an exit code (0 to 255, from `exit(n)`) or a terminating signal number and a core-dumped flag. The shell shows a death by signal `N` as `128+N` in `$?`, which is a **convention of the shell**, not of the kernel: `exit 143` and a death by `SIGTERM` look identical in `$?` and differ in `waitpid` (section 4.5 and Problem B).

**Reaping many.** One `wait` returns one child; there may be zombies behind it. The reaper must loop on `waitpid(-1, ..., WNOHANG)` until it returns 0 or `ECHILD`, because `SIGCHLD` can be coalesced (section 4.4 shows five exits raising the handler once).

---

## 3. `ps` and `top`: reading the numbers correctly

`ps` takes a snapshot. It has two option families that give different default columns, which is a standing interview question ("`ps aux` against `ps -ef`").

```text
--- ps aux (BSD style) against ps -ef (System V style): the same processes, different columns
USER       PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME COMMAND
root         1  0.0  0.0  35148  7508 ?        SLl  14:26   0:00 /process_api --firecracker-init --a
UID        PID  PPID  C STIME TTY          TIME CMD
root         1     0  0 14:26 ?        00:00:00 /process_api --firecracker-init --addr 0.0.0.0:2024

--- columns chosen by name, for the one process we start
started 5.2 s ago, burned the CPU for 2 s, then slept for the rest of the time:
PID NLWP ELAPSED     TIME %CPU %MEM    VSZ   RSS STAT PSR  NI PRI COMMAND
650    2       5 00:00:01 21.6  0.0  10920  1940 Sl     0   0  19 burst
top, one batch iteration with a 1 s delay (the process is asleep now):
PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND
650 root      20   0   10920   1940   1800 S   0.0   0.0   0:01.13 burst

--- ps -L shows threads (LWP = thread id, NLWP = how many)
PID   LWP NLWP STAT %CPU COMMAND
659   659    2 Rl    100 burst
659   661    2 Sl    0.0 burst
entries in /proc/PID/task: 2

--- the header of top, line by line, next to the files it reads
top - 14:34:45 up 8 min,  0 user,  load average: 0.20, 0.09, 0.03
Tasks:  71 total,   1 running,  70 sleeping,   0 stopped,   0 zombie
%Cpu(s):  0.0 us,  0.0 sy,  0.0 ni,100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
MiB Mem :   8024.1 total,   7472.4 free,    510.1 used,    259.8 buff/cache
MiB Swap:      0.0 total,      0.0 free,      0.0 used.   7514.0 avail Mem
/proc/loadavg: 0.20 0.09 0.03 1/104 670
/proc/stat (cpu line, jiffies: user nice system idle iowait irq softirq steal): cpu  1370 0 225 95794 650 0 23 41 0 0
CLK_TCK: 100 per second
MemTotal:        8216668 kB
MemFree:         7653308 kB
MemAvailable:    7695880 kB
Buffers:            7712 kB
Cached:           250920 kB
SwapTotal:             0 kB
SwapFree:              0 kB
```

- **BSD style** (`ps aux`, no dash) prints `USER PID %CPU %MEM VSZ RSS TTY STAT START TIME COMMAND`; **System V style** (`ps -ef`) prints `UID PID PPID C STIME TTY TIME CMD`. `-ef` has the parent (`PPID`), `aux` does not; `aux` has memory and `STAT`. For scripts, ignore both and pick columns by name: `ps -o pid,ppid,stat,%cpu,rss,args -p PID`. A trailing `=` on a column name (`-o pid=`) removes its header.
- **`%CPU` in `ps` is a lifetime average**: CPU time divided by elapsed time since the process started. The test process (`burst 2 6`) burned CPU for 2 s of wall time, then slept; 5 s after its start `ps` showed **21.6**, and `top` in batch mode, which measures the **last interval**, showed **0.0**. Use `top` (or two reads of `/proc/PID/stat` utime plus stime, in clock ticks of 10 ms: `CLK_TCK` is 100 here) to see what a process is doing now.
- **The CPU burn did not give 2 s of CPU.** `TIME+` in `top` was `0:01.13` for a loop that ran for 2 s of wall-clock time. I did not isolate the cause; the likely one is that the hypervisor did not give the vCPU a full 2 s (the `steal` column of `/proc/stat` is exactly that), so **in a VM, wall time and CPU time can differ even for a spinning loop**.
- **`VSZ` against `RSS`**: `VSZ` is the size of the address space (mapped, not necessarily used), `RSS` is the part resident in RAM. For the test process they were 10,920 and 1,940 kB. Section 6 builds one with a known 64 MiB mapping to show the gap, and shows that `RSS` also counts file-backed pages shared with other processes, so **summing `RSS` over processes overcounts**; `PSS` (proportional set size) divides shared pages among their users.
- **Threads**: `ps -L` lists one line per thread (`LWP`), `NLWP` is the count. The test process showed two lines (LWP 659 at `Rl` and 100% CPU for the spinning thread, LWP 661 at `Sl` and 0.0), and `ps` without `-L` shows only the group leader with the **sum**. `top -H` does the same.
- **The header of `top` is `/proc` formatted.** The load averages come from `/proc/loadavg` (`0.20 0.09 0.03 1/104 670`: three averages, `running/total` tasks, last PID used); the `%Cpu(s)` line from the `cpu` line of `/proc/stat` (jiffies of user, nice, system, idle, iowait, irq, softirq, steal, differenced between two reads); the memory lines from `/proc/meminfo`. `MemAvailable`, not `MemFree`, is the field that answers "how much memory can a new process get" (from memory: it adds reclaimable page cache to free memory).

Finding things without scanning `ps` output: `pgrep -af PATTERN` (matches the full command line with `-f`, prints it with `-a`), `pidof NAME`, `pkill`, `ps -o pid,ppid,stat,args --forest`, `pstree -sp PID` (the ancestors and children of one process). Always check what `pgrep -f` matches before feeding it to `pkill`: the pattern also matches the shell running your own command line.

---

## 4. Signals: from generation to delivery

A signal is a small integer sent to a process (or a thread) to tell it something happened. [OS 17](../05_operating_systems/17_ipc_fundamentals.md) covers `sigaction`, async-signal safety and `signalfd`; this section covers what an operator and an interviewer need: **what happens to a signal on its way**, and the measured behaviours that surprise people.

### 4.1 The model: generate, pend, block, deliver, dispose

1. **Generated** by the kernel (a fault: `SIGSEGV`; a terminal key: `SIGINT`; a child exits: `SIGCHLD`; a timer: `SIGALRM`) or by a process (`kill`, `tgkill`, `sigqueue`, `raise`).
2. **Pending** on the target: recorded in a set of pending signals. There are two sets, per thread and per process. `/proc/PID/status` shows them as hexadecimal masks: `SigPnd` (this thread) and `ShdPnd` (the whole process), where bit `N-1` is signal `N`.
3. **Blocked** or not: each thread has a **mask** (`SigBlk`). A blocked signal stays pending and is **not** delivered until unblocked.
4. **Delivered** when the target next returns to user mode (from a system call, an interrupt or a wake-up), and its **disposition** applies: the default action, ignore (`SigIgn`), or a handler (`SigCgt`).
5. **Default actions** (from the `signal(7)` manual page): *terminate* for `HUP INT PIPE TERM USR1`, *terminate and dump core* for `QUIT SEGV`, *ignore* for `CHLD`, *stop* for `STOP TSTP`, *continue* for `CONT`. (From memory, the same classes also hold for `USR2 ALRM` terminate, `ABRT ILL FPE BUS` core, `URG WINCH` ignore, `TTIN TTOU` stop.)

Two signals are special: **`SIGKILL` and `SIGSTOP` cannot be caught, blocked or ignored**. Everything else can. In a bash test, `trap "" KILL` is silently accepted and has no effect: the script that then sent itself `SIGKILL` died with status 137.

### 4.2 Standard signals are not queued; real-time signals are

This is the most useful fact about signals and the most often missed. A **standard** signal (1 to 31) has **one bit** of pending state: if it arrives again while pending, the second is **lost**. A **real-time** signal (`SIGRTMIN` to `SIGRTMAX`, 34 to 64 on Linux) is **queued**, each delivery carries its own value, and they are delivered in order. The test blocks both kinds, sends five of each, and then unblocks:

```c
static void queueing(void) {
    struct sigaction sa;
    sigset_t block, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_usr1;
    sigaction(SIGUSR1, &sa, NULL);
    sa.sa_sigaction = on_rt; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGRTMIN, &sa, NULL);
    sigemptyset(&block); sigaddset(&block, SIGUSR1); sigaddset(&block, SIGRTMIN);
    sigprocmask(SIG_BLOCK, &block, &old);
    for (int i = 0; i < 5; i++) kill(getpid(), SIGUSR1);
    for (int i = 0; i < 5; i++) { union sigval v; v.sival_int = 100 + i; sigqueue(getpid(), SIGRTMIN, v); }
    show_mask("10 sent, blocked:");
    sigprocmask(SIG_SETMASK, &old, NULL);
    printf("after unblocking: SIGUSR1 handler ran %d time(s), SIGRTMIN handler ran %d time(s), values in order:", (int)usr1_count, (int)rt_count);
    for (int i = 0; i < rt_count && i < 16; i++) printf(" %d", rt_values[i]);
    printf("\n");
}
```

```text
--- 5 x SIGUSR1 and 5 x SIGRTMIN (with a value each) sent while both are blocked
$ sigdemo queue
10 sent, blocked:         SigPnd:       0000000000000000  ShdPnd:       0000000200000200  SigBlk:       0000000200000200  SigIgn:       0000000000000000  SigCgt:       0000000200000200
after unblocking: SIGUSR1 handler ran 1 time(s), SIGRTMIN handler ran 5 time(s), values in order: 100 101 102 103 104

--- the same blocking read() on a pipe, SIGALRM after 1 s, data after 2 s
$ sigdemo restart
read() without SA_RESTART: returned -1 (Interrupted system call) after 1.0 s, handler ran 1 time(s)
read() with SA_RESTART: returned 1 (data) after 2.0 s, handler ran 1 time(s)

--- sleep() is not restarted even with SA_RESTART
$ sigdemo sleep
sleep(3) with SA_RESTART, alarm after 1 s: returned early, 1 s left

--- fork clears pending, exec keeps blocked, ignored and pending, resets handlers
$ sigdemo inherit
parent:                   SigPnd:       0000000000000000  ShdPnd:       0000000000000001  SigBlk:       0000000000004001  SigIgn:       0000000000000800  SigCgt:       0000000000000200
child after fork:         SigPnd:       0000000000000000  ShdPnd:       0000000000000000  SigBlk:       0000000000004001  SigIgn:       0000000000000800  SigCgt:       0000000000000200
child, own HUP sent:      SigPnd:       0000000000000000  ShdPnd:       0000000000000001  SigBlk:       0000000000004001  SigIgn:       0000000000000800  SigCgt:       0000000000000200
child now execs sed on /proc/self/status:
SigPnd: 0000000000000000
ShdPnd: 0000000000000001
SigBlk: 0000000000004001
SigIgn: 0000000000000800
SigCgt: 0000000000000000
```

Read the first block: with `SIGUSR1` and `SIGRTMIN` both blocked, `ShdPnd` (process-wide pending) showed one bit for each (`0x200` is bit 9, signal 10, `SIGUSR1`; `0x200000000` is bit 33, signal 34, `SIGRTMIN`). After unblocking, **the `SIGUSR1` handler ran once, the `SIGRTMIN` handler five times, with the values 100 to 104 in order**. The consequence for design: **a signal is a notification that "something happened", never a count**. A `SIGCHLD` handler must loop on `waitpid` (section 4.4), and a program that needs every event uses a queue (a pipe, `signalfd`, a real-time signal with `sigqueue`) and not a standard signal.

### 4.3 `EINTR` and `SA_RESTART`

When a signal handler runs while the process is blocked in a system call, the call either **fails with `EINTR`** or is **restarted** after the handler returns. Which one depends on the call and on the `SA_RESTART` flag of the handler. From the `signal(7)` page: with `SA_RESTART`, `read` and `write` on slow devices, the `wait` family, `flock` and `futex` are restarted; **never restarted** (always `EINTR`): `poll`, `select`, `epoll_wait`, the sleep calls (`sleep` returns the **remaining seconds**, `nanosleep` the remaining time), `pause` and `sigsuspend`, and socket calls with a timeout set. The test blocks in `read` on a pipe that gets data after 2 s, with `SIGALRM` after 1 s, once with and once without the flag, and then calls `sleep(3)` with `SA_RESTART` set:

```c
static void interrupted(int restart) {   /* a blocking read() on a pipe, interrupted by SIGALRM */
    int p[2]; char c; struct sigaction sa;
    if (pipe(p)) return;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sa.sa_flags = restart ? SA_RESTART : 0;
    sigaction(SIGALRM, &sa, NULL);
    alarms = 0;
    struct timespec s, e; clock_gettime(CLOCK_MONOTONIC, &s);
    alarm(1);
    if (!fork()) { sleep(2); if (write(p[1], "x", 1) < 0) _exit(1); _exit(0); }   /* data arrives after 2 s */
    ssize_t n = read(p[0], &c, 1);
    int err = errno;
    clock_gettime(CLOCK_MONOTONIC, &e);
    printf("read() with%s SA_RESTART: returned %zd (%s) after %.1f s, handler ran %d time(s)\n", restart ? "" : "out", n, n < 0 ? strerror(err) : "data", (e.tv_sec - s.tv_sec) + (e.tv_nsec - s.tv_nsec) / 1e9, (int)alarms);
    wait(NULL); close(p[0]); close(p[1]);
}

static void sleeper(void) {   /* sleep() and nanosleep() are never restarted */
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm; sa.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa, NULL);
    alarm(1);
    unsigned left = sleep(3);
    printf("sleep(3) with SA_RESTART, alarm after 1 s: returned early, %u s left\n", left);
}
```

The output is in the transcript above (second and third blocks): **without `SA_RESTART`, `read` returned -1 `EINTR` after 1.0 s; with it, the read was restarted and returned the data after 2.0 s; `sleep(3)` returned early with 1 s left even with `SA_RESTART`**. The rule for application code: every blocking call in a program that installs handlers is either wrapped in `do { r = call(); } while (r < 0 && errno == EINTR);` or the handlers set `SA_RESTART`, and the calls on the never-restart list are always wrapped (or use the `EINTR`-free variants such as `epoll_pwait`, from memory). This is also why `strace` of a program that handles signals shows `ERESTARTSYS` or `EINTR` lines (from memory).

### 4.4 `SIGCHLD`: one signal, many children

`SIGCHLD` is a standard signal, so it **coalesces** like `SIGUSR1`. The test creates five children that exit at once while `SIGCHLD` is blocked, then unblocks it:

```python
#!/usr/bin/env python3
"""sigchld.py: SIGCHLD is a standard signal, so five exits can raise it once."""
import os, signal, subprocess, time
calls = 0
def on_chld(sig, frame):
    global calls
    calls += 1
def zombies(label):
    out = subprocess.run(["ps", "--ppid", str(os.getpid()), "-o", "stat="], capture_output=True, text=True).stdout.split()
    print(f"{label}: {sum(1 for s in out if s.startswith('Z'))} zombie children")
signal.signal(signal.SIGCHLD, on_chld)
signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGCHLD})
for _ in range(5):
    if os.fork() == 0:
        os._exit(0)
time.sleep(0.4)
zombies("5 children exited, SIGCHLD blocked")
signal.pthread_sigmask(signal.SIG_UNBLOCK, {signal.SIGCHLD})
time.sleep(0.1)
print("handler calls after unblocking:", calls)
os.waitpid(-1, 0)                       # the wrong pattern: one wait per signal
zombies("after ONE waitpid per SIGCHLD")
reaped = 1
while True:                             # the right pattern: loop until nothing is left
    try:
        pid, _ = os.waitpid(-1, os.WNOHANG)
    except ChildProcessError:
        break
    if pid == 0:
        break
    reaped += 1
zombies("after the WNOHANG loop")
print("children reaped in total:", reaped)
```

```text
$ sigchld.py
5 children exited, SIGCHLD blocked: 5 zombie children
handler calls after unblocking: 1
after ONE waitpid per SIGCHLD: 4 zombie children
after the WNOHANG loop: 0 zombie children
children reaped in total: 5
```

**Five zombies, one handler call**. A reaper that calls `waitpid` once per signal, which is the natural reading of "wait when the child signal arrives", left **4 zombies**; the loop `waitpid(-1, &st, WNOHANG)` until it returns 0 or `ECHILD` left 0. The same loop is correct in C and in every language's runtime.

### 4.5 `kill`, exit statuses and what a default action does

`kill PID` sends `SIGTERM` (15). `kill -s NAME PID` or `kill -NAME PID` chooses a signal; `kill -l` prints the table; `kill -0 PID` sends nothing and only tests whether the process exists and may be signalled; a **negative PID** (`kill -TERM -1234`) signals the whole **process group** 1234. The call `kill(2)` returns success when the signal is accepted, **not** when the process dies, as the zombie experiment showed.

The default actions and the statuses the shell reports, measured by sending each signal to `sleep 30` and reading `$?` (and, first, by showing why the first attempt gave nothing):

```text
--- a background job of a non-interactive shell ignores SIGINT and SIGQUIT
SigIgn: 0000000000000006
PID STAT COMMAND
3892 S    sleep 30

--- send a signal to sleep 30 (default dispositions restored), read the exit status the shell reports
SIGHUP   ( 1) -> status 129 = 128+1
SIGINT   ( 2) -> status 130 = 128+2
SIGQUIT  ( 3) -> status 131 = 128+3
SIGTERM  (15) -> status 143 = 128+15
SIGKILL  ( 9) -> status 137 = 128+9
SIGUSR1  (10) -> status 138 = 128+10
SIGPIPE  (13) -> status 141 = 128+13
SIGSEGV  (11) -> status 139 = 128+11

--- SIGCHLD and SIGURG are ignored by default, SIGWINCH too: sleep survives
PID STAT COMMAND
3922 S    sleep 30

--- SIGSTOP and SIGCONT
State:  T (stopped)
State:  S (sleeping)

--- a program that cannot be told to ignore SIGKILL
bash:  3933 Killed                  bash -c 'trap "echo got TERM" TERM; trap "" KILL 2>&1; kill -KILL $$'
status 137

--- SIGPIPE: the writer dies when the reader has gone
PIPESTATUS of yes | head -1: 141 0

--- core dumps: ulimit -c, core_pattern
core
ulimit -c here: 0
bash: line 1:  3940 Aborted                 crash
status 134
core files with ulimit -c 0: 0
bash: line 1:  3945 Aborted                 (core dumped) crash
status 134
-rw------- 356352 core
core: ELF 64-bit LSB core file, x86-64
#0  __pthread_kill_implementation (no_tid=0, signo=6, threadid=<optimized out>) at ./nptl/
#0  __pthread_kill_implementation (no_tid=0, signo=6, threadid=<optimized out>) at ./nptl/
#1  __pthread_kill_internal (signo=6, threadid=<optimized out>) at ./nptl/pthread_kill.c:7
#2  __GI___pthread_kill (threadid=<optimized out>, signo=signo@entry=6) at ./nptl/pthread_
```

- **A background job of a non-interactive shell ignores `SIGINT` and `SIGQUIT`** (`SigIgn` was `0x6`, bits for signals 2 and 3), so `kill -INT` did nothing and the process stayed `S`. This is the rule from [LX 01](01_shell_and_scripting.md) section 8 (`cmd &` in a script). I used `env --default-signal=INT,QUIT` to restore the defaults for the measurement.
- **Statuses**: `HUP` 129, `INT` 130, `QUIT` 131, `KILL` 137, `SEGV` 139, `USR1` 138, `PIPE` 141, `TERM` 143. This is **128 plus the signal number**, which is the shell's convention (see section 2); `bash` also prints `Killed`, `Terminated` or `Aborted (core dumped)` to the terminal when a foreground job dies this way.
- **`SIGSTOP` and `SIGCONT`**: `T (stopped)`, then back to `S (sleeping)`.
- **Ignored by default**: `SIGCHLD`, `SIGURG` and `SIGWINCH` sent to `sleep` left it running.
- **`SIGPIPE`**: `yes | head -1` gave `PIPESTATUS` `141 0`. `head` exits after one line, the next `write` by `yes` has no reader, and the kernel sends `SIGPIPE`, whose default action ends `yes` with 141. In a program the usual choices are to ignore `SIGPIPE` and handle `EPIPE`, or to let it die. Under `set -o pipefail` (LX 01) the 141 makes the pipeline fail.
- **Core dumps** need two things: a core size limit above 0 (`ulimit -c`, here **0**, so none by default) and a place to write: `core_pattern` is `core` here, so the file is `./core`. `crash` calls `abort()`:

```c
#include <stdlib.h>

/* crash.c: dies from SIGABRT, the way a failed assert() or a double free does */
int main(void) { abort(); }
```

With `ulimit -c 0` the process died with status 134 (`128+6`, `SIGABRT`) and no file; with `ulimit -c unlimited` the message gained `(core dumped)` and `./core` was an ELF core file of **356,352 bytes** (mode `-rw-------`). `gdb -batch -ex bt PROGRAM core` printed the stack of the dying thread (`__pthread_kill_implementation`, `signo=6`, from `abort`). (From memory: on a distribution with `systemd`, `core_pattern` usually pipes to `systemd-coredump`, and the dumps are listed with `coredumpctl`; that is [LX 04](00_plan.md) material. The [debugging track](../16_debugging/00_plan.md) covers reading cores.)

### 4.6 What `fork` and `exec` do to signals

The `signal(7)` page states the rules; the test reads the masks from `/proc/self/status` at each step. A parent blocks `SIGTERM` and `SIGHUP`, handles `SIGUSR1`, ignores `SIGUSR2`, and has a `SIGHUP` pending, then forks, the child sends itself a `SIGHUP`, and then execs `sed` on `/proc/self/status`:

```c
static void inheritance(void) {
    struct sigaction sa; sigset_t m;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_usr1; sigaction(SIGUSR1, &sa, NULL);     /* handled */
    sa.sa_handler = SIG_IGN; sigaction(SIGUSR2, &sa, NULL);     /* ignored */
    sigemptyset(&m); sigaddset(&m, SIGTERM); sigaddset(&m, SIGHUP);
    sigprocmask(SIG_BLOCK, &m, NULL);                           /* blocked */
    kill(getpid(), SIGHUP);                                     /* pending (process-wide) */
    show_mask("parent:");
    pid_t c = fork();
    if (c == 0) {
        show_mask("child after fork:");
        kill(getpid(), SIGHUP);                                 /* make the child's own pending signal */
        show_mask("child, own HUP sent:");
        printf("child now execs sed on /proc/self/status:\n");
        execl("/bin/sed", "sed", "-n", "/^\\(SigBlk\\|SigIgn\\|SigCgt\\|SigPnd\\|ShdPnd\\)/p", "/proc/self/status", (char *)NULL);
        _exit(1);
    }
    waitpid(c, NULL, 0);
}
```

The transcript is the last block of `e3_signals` above. The parent has `ShdPnd 1` (bit 0, `SIGHUP`), `SigBlk 0x4001` (`HUP` and `TERM`), `SigIgn 0x800` (`USR2`), `SigCgt 0x200` (`USR1`). Then:

| | blocked mask | ignored | handled | pending |
| --- | --- | --- | --- | --- |
| **after `fork`** (child) | inherited | inherited | inherited | **cleared** (`ShdPnd 0`) |
| **after `exec`** | **kept** | **kept** | **reset to default** (`SigCgt 0`) | **kept** (`ShdPnd 1`) |

The rules in words: `fork` copies the mask and the dispositions and **empties** the pending set; `exec` keeps the mask, **keeps ignored signals ignored**, resets caught signals to default (the handler's code is gone) and keeps what is pending. The practical consequences: **a program started with a signal blocked or ignored stays that way**, which is how `nohup` (ignore `SIGHUP`) and a shell's background jobs (ignore `SIGINT`) work, and why a job started from a script that ignores `SIGPIPE` or blocks signals can behave differently from the same job started by hand. A library that must run a child with a clean mask resets it between `fork` and `exec`.

### 4.7 Handlers: what they may do

A handler runs asynchronously, between any two instructions of the interrupted code, so it can only use **async-signal-safe** functions (`write`, `_exit`, `kill`, `sigaction` and the other calls listed in `signal-safety(7)`; **not** `printf`, `malloc` or anything that takes a lock the interrupted code may hold). The standard safe pattern is to set a `volatile sig_atomic_t` flag, or write one byte to a pipe, and do the work in the main loop; `signalfd` or `sigtimedwait` with the signals blocked turns signals into ordinary events. (The measured programs in this tutorial use `printf` only outside handlers.) For a multithreaded program, a process-directed signal is delivered to **one** thread that does not block it; the usual design is to block all signals in every thread but one, or use `signalfd`, from memory.

---

## 5. Terminals, sessions and job control: who gets the signal

[LX 01](01_shell_and_scripting.md) section 8 and [OS 21](../05_operating_systems/21_os_interview_question_bank.md) section 8.3 cover this from the shell side. Here is the kernel side, measured on a **pseudo-terminal** that a Python program creates and controls (the VM has no real terminal for the test).

The structure: a **session** (SID) is created by `setsid`; the session leader may acquire a **controlling terminal**; the terminal has one **foreground process group** (`TPGID`), and the **terminal driver**, not the shell, turns special characters into signals **to that foreground group**:

| Key (default) | Signal | Setting name in `stty` |
| --- | --- | --- |
| `^C` | `SIGINT` | `intr` |
| `^\` | `SIGQUIT` | `quit` |
| `^Z` | `SIGTSTP` | `susp` |
| (terminal closed) | `SIGHUP` to the session leader, which then passes it to its jobs | |
| (background job reads or writes the tty) | `SIGTTIN`, `SIGTTOU` | `tostop` |

```python
#!/usr/bin/env python3
"""pty_signals.py: the terminal driver turns keys into signals; closing the master sends SIGHUP."""
import os, pty, sys, time, tempfile

log = tempfile.mktemp(prefix="pty.")
script = r'''
L=%s
trap 'echo INT >> $L' INT
trap 'echo QUIT >> $L' QUIT
trap 'echo TSTP >> $L' TSTP
trap 'echo HUP >> $L; exit 0' HUP
ps -o pid,pgid,sid,tpgid,tty,stat,comm -p $$ >> $L
stty -a | grep -o 'intr = [^;]*; quit = [^;]*' >> $L
stty -a | grep -o 'susp = [^;]*' >> $L
while :; do sleep 0.1; done
''' % log

def spawn(cmd):
    pid, master = pty.fork()          # child: new session, the pty slave is its controlling terminal
    if pid == 0:
        os.execvp("sh", ["sh", "-c", cmd])
    return pid, master

pid, master = spawn(script)
time.sleep(0.5)
for key, name in ((b"\x03", "Ctrl-C"), (b"\x1c", "Ctrl-\\"), (b"\x1a", "Ctrl-Z")):
    os.write(master, key); time.sleep(0.3)
os.close(master)                      # the last holder of the master goes away: the kernel sends SIGHUP
time.sleep(0.5)
os.waitpid(pid, 0)
print(open(log).read(), end="")
os.unlink(log)

print("--- with stty -isig the same bytes arrive as data and no signal is sent")
log2 = tempfile.mktemp(prefix="pty.")
pid, master = spawn("trap 'echo INT >> %s' INT; stty -isig -icanon; head -c3 | od -An -tx1 >> %s" % (log2, log2))
time.sleep(0.5)
os.write(master, b"\x03\x1a\x1c"); time.sleep(0.5)
os.waitpid(pid, 0)
print(open(log2).read(), end="")
os.unlink(log2)
```

```text
$ pty_signals.py
  PID  PGID   SID TPGID TT       STAT COMMAND
 3968  3968  3968  3968 pts/0    Ss+  sh
intr = ^C; quit = ^\
susp = ^Z
INT
QUIT
TSTP
HUP
--- with stty -isig the same bytes arrive as data and no signal is sent
 03 1a 1c
```

- The child was a session leader with its own process group: `PID 3968 PGID 3968 SID 3968 TPGID 3968` and `STAT Ss+`. `stty -a` inside it showed `intr = ^C; quit = ^\` and `susp = ^Z`.
- Writing the bytes `0x03`, `0x1c`, `0x1a` into the master side made the shell's traps log **`INT`, `QUIT`, `TSTP`**: the signals came from the terminal driver, not from any program.
- **Closing the master** (the terminal window or the SSH connection going away) made the kernel send **`SIGHUP`**, logged as `HUP`. That is how a closed terminal kills its jobs and why `nohup`, `setsid`, `disown` or `tmux` are used for work that must outlive a login.
- With `stty -isig` (and `-icanon`, so the bytes are not line-buffered) the **same three bytes arrived as data**, `03 1a 1c`, and no signal was raised. That is how `ssh`, `vim` and password prompts take control of `^C` and `^Z`, and why a program that left the terminal in raw mode makes the shell look dead until `stty sane`.

**What the shell adds**: for each pipeline it creates a **process group** and, in an interactive shell, makes it foreground with `tcsetpgrp`, so `^C` reaches exactly the processes of the pipeline and not the shell. The `jobs`, `fg`, `bg` and `disown` built-ins manage the groups (LX 01). `kill -TERM -PGID` is the way to signal a whole job; `kill PID` signals one process, and a shell script that was killed does not stop the child it was waiting for unless a `trap` forwards the signal.

**Which signal to send, as a rule.** `SIGTERM` first (the program may clean up); wait; then `SIGKILL`, which gives no chance to clean up (no `atexit`, no buffers flushed, locks files and shared memory left behind). `SIGINT` is what a human at a terminal sends. `SIGHUP` conventionally means "the controlling terminal went away" and, for daemons, "re-read your configuration" (from memory: many daemons, for example `sshd` and `nginx`, do this; check the manual of each). `SIGUSR1` and `SIGUSR2` are application-defined. Section 10 shows that the same `kill` has a different effect on PID 1 of a container.

---

## 6. `/proc/PID`: one process, file by file

`/proc` is a virtual file system: the "files" are produced by the kernel when you read them (their size is 0 and the content is current at the read). Everything `ps`, `top`, `lsof` and `pidstat` print comes from here. The test process `procdemo` has a file open, a pipe, a **64 MiB anonymous mapping of which 4 MiB is touched**, a second thread, and an environment variable; it is started as `procdemo a1 "b 2"`:

```c
int main(void) {
    int fd = open("/etc/hostname", O_RDONLY);
    int p[2];
    if (fd < 0 || pipe(p)) return 1;
    size_t len = 64u << 20;                            /* map 64 MiB, touch 4 MiB */
    char *m = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(m, 1, 4u << 20);
    setenv("DEMO_KEY", "demo-value", 1);
    pthread_t t;
    pthread_create(&t, NULL, worker, NULL);
    printf("ready\n");
    fflush(stdout);
    sleep(60);
    return 0;
}
```

```text
--- procdemo runs as PID $p; every file below is /proc/$p/NAME
$ tr "\0" "|" < /proc/$p/cmdline; echo
procdemo|a1|b 2|
$ tr "\0" "\n" < /proc/$p/environ | grep -E "^DEMO"
DEMO_ENV=hello
$ readlink /proc/$p/exe /proc/$p/cwd
/tmp/lx03/procdemo
/tmp

--- fd: the open files, with the position and flags in fdinfo
$ ls -l /proc/$p/fd | awk "NR>1{print \$9, \$10, \$11}"
0 -> /dev/null
1 -> /dev/null
2 -> pipe:[12569]
3 -> /etc/hostname
4 -> pipe:[12573]
5 -> pipe:[12573]
$ cat /proc/$p/fdinfo/3
pos:    0
flags:  0100000
mnt_id: 27
ino:    319552

--- status: the human-readable summary
$ grep -E "^(Name|State|Tgid|Pid|PPid|TracerPid|Uid|Threads|VmPeak|VmSize|VmRSS|RssAnon|RssFile|VoluntaryCtxt|NonVoluntaryCtxt|Cpus_allowed_list)" /proc/$p/status
Name:   procdemo
State:  S (sleeping)
Tgid:   4540
Pid:    4540
PPid:   4531
TracerPid:      0
Uid:    0       0       0       0
VmPeak:    76456 kB
VmSize:    76456 kB
VmRSS:      5984 kB
RssAnon:            4240 kB
RssFile:            1744 kB
Threads:        2
Cpus_allowed_list:      0-1

--- the same fields as numbers in /proc/PID/stat (fields 3-6, 14-15, 20, 22-24 of the file)
$ awk "{ sub(/^[0-9]+ \\([^)]*\\) /, \"\"); print \"state=\" \$1, \"ppid=\" \$2, \"pgrp=\" \$3, \"session=\" \$4, \"utime=\" \$12, \"stime=\" \$13, \"threads=\" \$18, \"starttime=\" \$20, \"vsize=\" \$21, \"rss_pages=\" \$22 }" /proc/$p/stat
state=S ppid=4531 pgrp=4529 session=4527 utime=0 stime=0 threads=2 starttime=181142 vsize=78290944 rss_pages=1476

--- maps: the address space, one line per mapping
$ wc -l < /proc/$p/maps
27
$ grep -E "procdemo|heap|stack|libc.so|vdso" /proc/$p/maps | awk "{print \$1, \$2, \$6}" | cut -c1-80 | head -12
55fb0d956000-55fb0d957000 r--p /tmp/lx03/procdemo
55fb0d957000-55fb0d958000 r-xp /tmp/lx03/procdemo
55fb0d958000-55fb0d959000 r--p /tmp/lx03/procdemo
55fb0d959000-55fb0d95a000 r--p /tmp/lx03/procdemo
55fb0d95a000-55fb0d95b000 rw-p /tmp/lx03/procdemo
55fb1c7e7000-55fb1c808000 rw-p [heap]
7fd983000000-7fd983028000 r--p /usr/lib/x86_64-linux-gnu/libc.so.6
7fd983028000-7fd9831b1000 r-xp /usr/lib/x86_64-linux-gnu/libc.so.6
7fd9831b1000-7fd983200000 r--p /usr/lib/x86_64-linux-gnu/libc.so.6
7fd983200000-7fd983204000 r--p /usr/lib/x86_64-linux-gnu/libc.so.6
7fd983204000-7fd983206000 rw-p /usr/lib/x86_64-linux-gnu/libc.so.6
7fd98328e000-7fd983290000 r-xp [vdso]
$ gawk "{split(\$1, a, \"-\"); n = strtonum(\"0x\" a[2]) - strtonum(\"0x\" a[1]); if (n >= 64 * 1024 * 1024) print \$1, \$2, n / 1048576 \" MiB, anonymous\"}" /proc/$p/maps
7fd97f000000-7fd983000000 rw-p 64 MiB, anonymous

--- smaps_rollup: VSZ is address space, RSS is what is resident
$ cat /proc/$p/smaps_rollup | grep -E "^(Rss|Pss|Private_Dirty|Shared_Clean|Anonymous)"
Rss:                5984 kB
Pss:                4414 kB
Pss_Dirty:          4252 kB
Pss_Anon:           4240 kB
Pss_File:            174 kB
Pss_Shmem:             0 kB
Shared_Clean:       1732 kB
Private_Dirty:      4252 kB
Anonymous:          4240 kB
$ ps -o vsz,rss,cmd -p $p | sed "s/^ *//"
VSZ   RSS CMD
76456  5984 procdemo a1 b 2

--- threads are tasks: one directory each, the thread group leader has TID == PID
$ for t in /proc/$p/task/*; do echo "$(basename $t) $(cat $t/comm) $(awk "{print \$3}" $t/stat)"; done
4540 procdemo S
4542 procdemo S

--- limits, cgroup, namespaces, wchan, syscall
$ grep -E "^(Max (open files|processes|core file size|stack size|address space))" /proc/$p/limits
Max stack size            8388608              unlimited            bytes
Max core file size        0                    unlimited            bytes
Max processes             32003                32003                processes
Max open files            20000                20000                files
Max address space         unlimited            unlimited            bytes
$ cat /proc/$p/cgroup
9:name=systemd:/
8:pids:/process_api
7:blkio:/
6:freezer:/
5:devices:/
4:memory:/process_api/01a1df9a-5385-7262-9350-25fca4270575/claude-code-bash
3:cpuset:/
2:cpuacct:/
1:cpu:/
0::/
$ ls /proc/$p/ns | tr "\n" " "; echo; readlink /proc/$p/ns/pid
cgroup ipc mnt net pid pid_for_children time time_for_children user uts
pid:[4026531836]
$ cat /proc/$p/wchan; echo; cat /proc/$p/syscall | cut -d" " -f1
hrtimer_nanosleep
230

--- the name of /proc/self is whoever reads it
$ readlink /proc/self; bash -c "readlink /proc/self; echo \$\$"
4576
4578
4577
```

What each file answers, in the order of the transcript:

| File | Content | Question it answers |
| --- | --- | --- |
| `cmdline` | arguments separated by **NUL** (`procdemo|a1|b 2|` with `tr`) | "what exactly was it started with" (the argument `b 2` keeps its space) |
| `environ` | the **initial** environment, NUL-separated | "which `LD_LIBRARY_PATH`, which proxy"; readable by the owner and root; it does **not** show later `setenv` calls |
| `exe`, `cwd`, `root` | symbolic links: the executable, the working directory, the root directory | "which binary, which directory"; `exe` of a deleted binary reads `(deleted)` |
| `fd/`, `fdinfo/` | one entry per open descriptor; `fdinfo/N` has `pos`, `flags`, `mnt_id`, `ino` | "which files and sockets, and how far has it read" |
| `status` | readable summary: state, ids, `SigBlk`, `SigCgt`, `Vm*`, `Threads`, context switches | the first file to read |
| `stat` | the same as **numbers**: about 52 space-separated fields (from memory; field 3 state, 4 ppid, 5 pgrp, 6 session, 14 and 15 user and system CPU ticks, 20 threads, 22 start time, 23 virtual size, 24 resident pages) | what `ps` parses |
| `maps`, `smaps`, `smaps_rollup` | address-space mappings, with the details per mapping, with totals | "where is the memory" |
| `task/TID/` | a directory per **thread** (the same files again) | per-thread state and CPU |
| `limits` | the resource limits in force (section 7) | "why did `open` fail" |
| `cgroup`, `ns/` | control group membership; the namespaces (links such as `pid:[4026531836]`) | "which container, which limits" |
| `wchan`, `syscall`, `stack` | the kernel function it sleeps in; the system call number and arguments; the kernel stack (root) | "what is it blocked on" |
| `io`, `sched`, `schedstat` | bytes read and written, scheduler statistics (from memory; needs the right permissions) | "is it doing I/O or waiting for CPU" |

Points the transcript proves:

- **`cmdline` and `environ` are NUL-separated**, so `cat` runs them together; use `tr '\0' ' '` (or `\n`). A process can overwrite its own `argv`, so `cmdline` is what the process **says**, not a proof of what was exec'd; `exe` is the proof.
- **`stat` has a trap in field 2**: the command name is in parentheses and **may contain spaces and parentheses** (Problem A runs a program named `a b) c`). The correct parse removes everything up to the **last** `) `: `sed 's/^.*) //'`. The numbers above came from that: `ppid`, `pgrp`, `session`, `threads=2`, `vsize=78,290,944` bytes, `rss_pages=1476`.
- **Virtual size against resident size.** `VmSize` is **76,456 kB** (the 64 MiB mapping plus the program, libraries and stacks); `VmRSS` is only **5,984 kB**, because only the 4 MiB written by `memset` has pages (`RssAnon` 4,240 kB), plus 1,744 kB of file-backed pages (`RssFile`: the program, the C library). The 64 MiB mapping is the `rw-p` region `7fd97f000000-7fd983000000` found by size in `maps`. **Reading `VSZ` as memory use is a classic mistake**; a process may map hundreds of GiB and use little (a JVM or a Go program reserves address space up front).
- **`RSS` double counts shared memory.** `smaps_rollup` splits it: `Pss` (proportional set size) is **4,414 kB** against `Rss` 5,984 kB, because the 1,732 kB of `Shared_Clean` pages (the C library) are shared with every other process and each user is charged a share. For "how much memory would I free by killing this process" the answers are `Private_Dirty` plus the private part of the rest, which `Pss` approximates.
- **`task/` has one directory per thread**: `4540` (the leader, TID equal to PID) and `4542`, both named `procdemo`, both `S`. `status` showed `Threads: 2`. Thread names are set with `prctl(PR_SET_NAME)` or `pthread_setname_np` and are visible in `comm`, up to **15 characters** (from memory).
- **`limits`**: `Max open files 20000`, `Max processes 32003`, `Max core file size 0`, `Max stack size 8388608` soft (8 MiB), hard unlimited (section 7).
- **`cgroup`**: this VM runs the process in cgroup v1 hierarchies (`memory:/process_api/.../claude-code-bash`, `pids:/process_api`) plus a unified `0::/` entry. On a modern `systemd` host with cgroup v2 it is a single line such as `0::/system.slice/nginx.service` (from memory). The memory and pids limits that apply to the process are found by walking from that path in `/sys/fs/cgroup`; this is the container part of the track ([LX 09](00_plan.md)).
- **`ns/`**: ten namespace links; two processes are in the same namespace if the numbers in the links are equal. `pid:[4026531836]` is the initial PID namespace of this VM. Section 10 shows a process with `NSpid: 3696 1`.
- **`wchan` and `syscall`**: the process sleeps in `hrtimer_nanosleep`, and `syscall` starts with `230`, which is `clock_nanosleep` on x86-64 (from memory; `ausyscall` or `/usr/include/asm/unistd_64.h` maps numbers to names). For a process stuck in `D`, `wchan` and `/proc/PID/stack` are the two lines to paste into a bug report.
- **`/proc/self`** is a link to the PID of **whoever reads it**: `readlink /proc/self` printed the PID of the `readlink` process, and the nested `bash -c` printed its own child's PID (4578) and then its own (`$$` 4577).

**System-wide files** you will use alongside: `/proc/loadavg` (section 8), `/proc/stat` (the `cpu` line in jiffies and `procs_running`, `procs_blocked`, `ctxt`, `btime`), `/proc/meminfo` (`MemAvailable`, `Dirty`, `Slab`, `HugePages_*`), `/proc/pressure/{cpu,io,memory}` (section 8), `/proc/sys/` (the tunables read and written by `sysctl`), `/proc/net/` (section 11), `/proc/interrupts`, `/proc/cpuinfo`, `/proc/mounts`. Permissions: `environ`, `fd`, `mem` and `syscall` of other users' processes need root or `CAP_SYS_PTRACE`; the `hidepid` mount option can hide other users' directories entirely (from memory).

---

## 7. Resource limits: what stops a process from asking for more

Each process has a set of **resource limits** (`rlimit`s), each a **soft** value (the one enforced) and a **hard** value (the ceiling the soft one may be raised to by an unprivileged process). They are **inherited across `fork` and `exec`**, set with `setrlimit`, the shell's `ulimit`, or `prlimit` (which can also change a *running* process), and listed in `/proc/PID/limits`.

| Limit | `ulimit` flag | What happens at the limit |
| --- | --- | --- |
| `RLIMIT_NOFILE` | `-n` | `open`, `socket`, `accept`, `pipe` fail with `EMFILE` |
| `RLIMIT_NPROC` | `-u` | `fork` and `clone` fail with `EAGAIN`; counts **all** processes (and threads) of the real user |
| `RLIMIT_AS` | `-v` | `mmap`, `brk` fail with `ENOMEM`; the allocation fails, the process is not killed |
| `RLIMIT_CORE` | `-c` | a core file larger than this is cut or not written (0 means none) |
| `RLIMIT_STACK` | `-s` | the main-thread stack grows to this size; beyond it the process gets `SIGSEGV` |
| `RLIMIT_CPU` | `-t` | `SIGXCPU`, then `SIGKILL` (from memory) |
| `RLIMIT_MEMLOCK` | `-l` | the amount that can be locked in RAM with `mlock` (from memory) |

```text
--- soft and hard limits of this shell, and the inheritance across fork and exec
$ ulimit -Sn; ulimit -Hn
20000
20000
$ bash -c "ulimit -n 50; bash -c ulimit\ -n"
50
$ prlimit --nofile --nproc --core --stack --pid $$
RESOURCE DESCRIPTION                 SOFT      HARD UNITS
NOFILE   max number of open files   20000     20000 files
NPROC    max number of processes    32003     32003 processes
CORE     max core file size             0 unlimited bytes
STACK    max stack size           8388608 unlimited bytes

--- RLIMIT_NOFILE: open files until the kernel says no (EMFILE)
$ bash -c "ulimit -n 20; python3 /tmp/_nofile.py"
opened 17 files, then EMFILE: Too many open files; lowest new fd would have been 20

--- change the limit of a RUNNING process with prlimit (soft then hard)
$ prlimit --pid $p --nofile
RESOURCE DESCRIPTION               SOFT  HARD UNITS
NOFILE   max number of open files 20000 20000 files
$ prlimit --pid $p --nofile=100:200; prlimit --pid $p --nofile
RESOURCE DESCRIPTION              SOFT HARD UNITS
NOFILE   max number of open files  100  200 files
$ grep "Max open files" /proc/$p/limits
Max open files            100                  200                  files

--- RLIMIT_NPROC counts all processes of the user; root is exempt, uid 65534 is not
$ setpriv --reuid=65534 --regid=65534 --clear-groups bash -c "ulimit -u 8; python3 /tmp/_nproc.py"
forked 7 children, then EAGAIN: Resource temporarily unavailable

--- RLIMIT_AS: the allocation fails, the process is not killed
$ bash -c "ulimit -v 200000; python3 -c \"b = bytearray(300 * 1024 * 1024)\"" 2>&1 | tail -1
MemoryError
```

- **Inheritance**: `bash -c "ulimit -n 50; bash -c ulimit -n"` printed 50, so the child got the parent's new limit.
- **`EMFILE`**: with `ulimit -n 20`, a process opened **17** files and then the 18th `open` failed with `EMFILE`; the lowest descriptor it would have got was 20, because 0, 1, 2 were already open. A server that runs out of descriptors under load fails every new `accept`; the usual fix is a higher `LimitNOFILE` for the service (LX 04) and closing leaked descriptors, and `ls /proc/PID/fd | wc -l` counts what it holds now.
- **`prlimit` on a running process** changed `sleep`'s soft and hard `NOFILE` to 100 and 200, and `/proc/PID/limits` showed the change at once. This is the way to raise a limit for a daemon you cannot restart. (Restarting through the service manager is the durable fix, because a limit set by hand is lost at the next start.)
- **`EAGAIN` on `fork`**: with `ulimit -u 8` and **user 65534** (not root), a program forked **7** children and the eighth `fork` failed with `EAGAIN` (the program itself is the eighth process). **The same test as root is not limited** (from memory: root, or a process with `CAP_SYS_RESOURCE`, bypasses `RLIMIT_NPROC`), which is why I used `setpriv` to run as an unprivileged user. A "fork bomb" containment is exactly this limit or the cgroup `pids.max`.
- **`RLIMIT_AS`**: allocating 300 MiB under `ulimit -v 200000` (kB) made `bytearray` raise `MemoryError`: `malloc` returned `NULL` (`mmap` failed with `ENOMEM`) and the program was **not** killed. This is different from the **out-of-memory killer**, which picks a victim when the whole machine or a cgroup runs out of memory, and which is [LX 06](00_plan.md).
- **Where limits are set persistently** (from memory): `/etc/security/limits.conf` and `limits.d/` apply at login through PAM (and do **not** apply to `systemd` services); a service gets `LimitNOFILE=` and friends in its unit file; a container gets them from the runtime (`docker run --ulimit`) and from cgroups.

---

## 8. The load average, `D` state and pressure stall information

`uptime`, `top` and `/proc/loadavg` print **three numbers**, the "load average" over about 1, 5 and 15 minutes (`0.20 0.09 0.03` above). Interviewers ask what it counts, and the common answer ("the number of processes using the CPU") is incomplete.

What the load average is (from Brendan Gregg's analysis of the Linux kernel source, fetched for this tutorial): **an exponentially damped moving average of the number of tasks that are running or runnable, plus those in uninterruptible sleep (`D`)**. Linux added the `D` tasks in 0.99.14 (November 1993), so the number reflects demand for **system resources**, not only for CPU. The kernel updates it about every 5 seconds, and the three averages differ only in their time constants.

The formula, which I tested rather than quoted: at each 5 s sample, `L = L * e + n * (1 - e)` with `e = exp(-5/60)` for the 1-minute value, where `n` is the number of such tasks at that moment. The experiment adds three busy loops on 2 CPUs (so `n` is 3 and there are more tasks than CPUs), then, separately, three tasks in **`D` state with the CPUs idle** (three `vforkd` parents from section 1, which count as `D`):

```python
#!/usr/bin/env python3
"""loadavg.py: watch /proc/loadavg climb while R tasks, then D tasks, are added; compare with the damping formula."""
import math, os, subprocess, sys, time

def load():
    return [float(x) for x in open("/proc/loadavg").read().split()[:3]]

def states():
    out = subprocess.run(["ps", "-eo", "stat="], capture_output=True, text=True).stdout.split()
    return sum(s[0] == "R" for s in out), sum(s[0] == "D" for s in out)

def phase(title, seconds, n):
    print(f"--- {title}")
    print(f"{'t(s)':>4} {'R':>2} {'D':>2} {'load1':>6} {'model1':>7} {'implied n':>9}   (model: L = L*e + n*(1-e), e = exp(-5/60), n = {n})")
    e = math.exp(-5 / 60)
    model = prev = load()[0]
    t0 = time.time()
    for i in range(seconds // 5 + 1):
        r, d = states()
        cur = load()[0]
        implied = "-" if i == 0 else f"{(cur - prev * e) / (1 - e):.1f}"   # the n that explains this step
        print(f"{i*5:>4} {r:>2} {d:>2} {cur:>6.2f} {model:>7.2f} {implied:>9}")
        prev = cur
        model = model * e + n * (1 - e)
        time.sleep(max(0, t0 + (i + 1) * 5 - time.time()))

mode = sys.argv[1]
if mode == "cpu":
    procs = [subprocess.Popen(["bash", "-c", "while :; do :; done"]) for _ in range(3)]
    phase("3 busy loops on 2 CPUs", 40, 3)
    print("pressure/cpu:", open("/proc/pressure/cpu").read().split("\n")[0])
    for p in procs: p.kill()
    for p in procs: p.wait()
elif mode == "d":
    procs = [subprocess.Popen(["vforkd", "45"]) for _ in range(3)]
    time.sleep(1)
    phase("3 parents asleep in vfork (state D), the CPUs idle", 25, 3)
    out = subprocess.run(["vmstat", "1", "2"], capture_output=True, text=True).stdout.rstrip().split("\n")
    print("vmstat: r = runnable, b = uninterruptible (D); id = idle CPU")
    print("\n".join(out[1:2] + out[-1:]))
    for p in procs: p.kill()
    for p in procs: p.wait()
```

```text
CPUs: 2
$ cat /proc/loadavg
0.55 0.97 0.65 1/102 3791
--- 3 busy loops on 2 CPUs
t(s)  R  D  load1  model1 implied n   (model: L = L*e + n*(1-e), e = exp(-5/60), n = 3)
   0  4  0   0.55    0.55         -
   5  4  0   0.75    0.75       3.1
  10  4  0   0.93    0.93       3.0
  15  4  0   1.09    1.09       2.9
  20  4  0   1.25    1.24       3.1
  25  4  0   1.39    1.38       3.0
  30  5  0   1.52    1.51       3.0
  35  4  0   1.64    1.63       3.0
  40  4  0   1.75    1.74       3.0
pressure/cpu: some avg10=49.36 avg60=27.73 avg300=14.48 total=120528256

--- 3 parents asleep in vfork (state D), the CPUs idle
t(s)  R  D  load1  model1 implied n   (model: L = L*e + n*(1-e), e = exp(-5/60), n = 3)
   0  1  3   1.85    1.85         -
   5  1  3   1.94    1.94       3.0
  10  1  3   2.02    2.03       2.9
  15  1  3   2.10    2.10       3.0
  20  1  3   2.17    2.18       3.0
  25  2  3   2.24    2.24       3.0
vmstat: r = runnable, b = uninterruptible (D); id = idle CPU
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 0  0      0 7713024    844 200900    0    0     0     0   51   67  0  0 100  0  0  0
```

- **The curve follows the formula**: at every sample the model column equals the measured value to within 0.01, and the "implied n" column (the `n` that explains each step) is 2.9 to 3.1 throughout. The 1-minute value after 40 s of three busy loops is **1.75, not 3**: it takes a few time constants (about 3 minutes) to converge, and it reaches `1 - 1/e`, about 63 percent of the way, after one minute. Gregg's post gives the same result for one busy thread (about 0.62 after one minute).
- **`D` tasks count**: with three tasks asleep in `vfork`, **the CPUs were 100 percent idle** (the `id` column of `vmstat` is 100) and the load average still rose from 1.85 to 2.24 in 25 s, heading for 3. A high load with an idle CPU therefore means **tasks waiting on something other than the CPU**: the usual causes are slow or hung storage (NFS, a failing disk, a lock in the kernel) and, as here, any uninterruptible wait. The check is `ps -eo state,pid,wchan:30,args | awk '$1=="D"'`.
- **`vmstat` does not show these tasks in its `b` column** (blocked): it stayed 0 here. (From memory: `b` and `procs_blocked` in `/proc/stat` count tasks waiting for **I/O**, which is narrower than "in `D` state".) So `vmstat` may show no blocked tasks while the load average and `ps` show `D`.
- **The reading rule** of Gregg's post: compare the three values. **1-minute above 15-minute: load is rising**; below: it is falling. Compare the number with the **CPU count** (`nproc`, here 2) only after separating CPU demand from `D` waiting. A load of 4 on 2 CPUs where every task is `R` means each task gets half a CPU; the same 4 with idle CPUs means 4 tasks are waiting for something else.
- **Better metrics** (Gregg): per-CPU utilisation (`mpstat -P ALL`), per-process CPU (`pidstat`), **run-queue latency** (how long runnable tasks wait: `/proc/PID/schedstat`, `perf sched`, the `runqlat` tool; `vmstat`'s `r` column is the count), and the USE method (utilisation, saturation, errors for each resource). `mpstat` and `pidstat` were not installed in this VM, so per-CPU numbers here come from `/proc/stat`, `top` and `ps`.
- **Pressure stall information (PSI)** answers "what share of time were tasks stalled" directly, per resource: `/proc/pressure/cpu`, `io`, `memory`. In the busy-loop run `/proc/pressure/cpu` printed `some avg10=49.36 avg60=27.73 avg300=14.48` at the end of the 40 s phase: the 10 s value is about half. My reading, **from memory of the kernel documentation and not tested here**, is that three loops on two CPUs always leave one CPU with a waiting task, and the system-wide figure averages the stall over the CPUs (one stalled CPU of two is about 50 percent); the 60 s and 300 s values are lower because the loops had run for less than a minute. `some` means at least one task stalled, `full` means all non-idle tasks stalled; for `cpu` only `some` is defined at the system level (from memory). PSI is the metric to alert on instead of the load average; the memory file is the early warning of thrashing, and it is how container runtimes and `oomd` decide to act ([LX 05](00_plan.md) and [LX 06](00_plan.md)). Gregg's post, which I fetched, does **not** mention PSI; this is from the kernel documentation as I remember it and from the file's contents here.

**What to say in an interview**: "load average is a damped count of runnable plus uninterruptible tasks, so it measures demand, not CPU use; a high load with idle CPUs means tasks stuck in `D`; I look at `vmstat`, per-CPU utilisation, `ps` for `D` tasks and PSI to separate CPU saturation from I/O or lock waits."

---

## 9. Scheduling knobs: `nice`, `chrt`, `taskset`, `ionice`

[OS 05](../05_operating_systems/05_process_scheduling.md) explains the scheduler (weights, virtual runtime). Here is what the commands do, measured.

```text
--- nice: two busy loops pinned to the same CPU for 6 s, one at nice 0 and one at nice 10
PID  NI PRI PSR STAT     TIME COMMAND
3842   0  19   0 R    00:00:05 bash -c while :; do :; done
3843  10   9   0 RN   00:00:00 bash -c while :; do :; done
ticks (1 tick = 10 ms): nice 0 got 542, nice 10 got 58; shares 90% and 10%
weights 1024 and 110 (from the kernel weight table, from memory) predict 90% and 10%

--- the same two loops on different CPUs: nice does not matter when nobody competes
ticks: nice 0 on cpu 0 got 400, nice 10 on cpu 1 got 398

--- scheduling policies and real-time priorities
$ chrt -m | head -6
SCHED_OTHER min/max priority    : 0/0
SCHED_FIFO min/max priority     : 1/99
SCHED_RR min/max priority       : 1/99
SCHED_BATCH min/max priority    : 0/0
SCHED_IDLE min/max priority     : 0/0
SCHED_DEADLINE min/max priority : 0/0
$ chrt -p $$
pid 3835's current scheduling policy: SCHED_OTHER
pid 3835's current scheduling priority: 0
$ chrt -f -p 10 $s; ps -o pid,cls,rtprio,ni,pri,args -p $s | sed "s/^ *//"
PID CLS RTPRIO  NI PRI COMMAND
3860  FF     10   -  50 sleep 5
$ chrt -p $s
pid 3860's current scheduling policy: SCHED_FIFO
pid 3860's current scheduling priority: 10
sched_rt_runtime_us / sched_rt_period_us: 950000 / 1000000

--- affinity
$ taskset -p $$
pid 3835's current affinity mask: 3
$ taskset -cp 1 $s; grep Cpus_allowed_list /proc/$s/status
pid 3868's current affinity list: 0,1
pid 3868's new affinity list: 1
Cpus_allowed_list:      1

--- I/O priority (only a scheduler such as bfq or mq-deadline acts on it)
$ ionice -p $$
none: prio 0
$ ionice -c 3 -p $$; ionice -p $$
idle
$ ionice -c 2 -n 4 -p $$; ionice -p $$
best-effort: prio 4
$ cat /sys/block/vda/queue/scheduler 2>/dev/null || ls /sys/block
none [mq-deadline] kyber bfq
```

- **`nice`** changes the **weight** of a normal (`SCHED_OTHER`) task: range -20 to 19; a higher value means a lower weight. It matters **only when tasks compete for the same CPU**. Two busy loops pinned to CPU 0 for 6 s, one at nice 0 and one at nice 10, got **542 and 58 ticks** of CPU time (of 10 ms each): **90 and 10 percent**, which matches the weights 1024 and 110 that the kernel's table gives for nice 0 and 10 (the table values are from memory; the match is the evidence). **On two different CPUs the same pair got 400 and 398 ticks**, so `nice` did nothing: nobody competed. `ps -o ni,pri` shows the nice and the priority number (`PRI` 19 and 9 in this run; do not compare `PRI` across tools, because they invert or offset it differently). `renice -n 5 -p PID` changes a running process; an unprivileged user may raise the nice value but not lower it.
- **Real-time policies**: `chrt -m` lists the policies and their priority ranges: `SCHED_OTHER`, `SCHED_BATCH`, `SCHED_IDLE` have priority 0 only; **`SCHED_FIFO` and `SCHED_RR` have 1 to 99**; `SCHED_DEADLINE` takes a runtime, deadline and period instead. `chrt -f -p 10 PID` made `sleep` `SCHED_FIFO` priority 10 (`ps` showed `CLS FF`, `RTPRIO 10`). A FIFO task runs until it blocks or a higher real-time task appears, so **a real-time task that spins starves every normal task on its CPU**. The safety net is **real-time throttling**: `sched_rt_runtime_us` / `sched_rt_period_us` was **950000 / 1000000**, so real-time tasks may use 95 percent of each second (a value of -1 turns the limit off; from memory). In a container the real-time budget is often 0 and `chrt` fails.
- **`taskset`** sets the **CPU affinity** (the set of CPUs a task may run on): `taskset -p $$` printed the mask `3` (CPUs 0 and 1), and `taskset -cp 1 PID` changed it to CPU 1 (`Cpus_allowed_list: 1` in `status`). Affinity is inherited by children. It is used to keep a latency-critical thread on a dedicated core, to keep a benchmark off a noisy core, and to stay on one NUMA node ([OS 13](../05_operating_systems/13_numa.md)); the cost is that the scheduler cannot balance load away from a busy core. `isolcpus` and IRQ affinity are in [LX 13](00_plan.md).
- **`ionice`** sets the **I/O scheduling class**: `1` real-time, `2` best-effort (levels 0 to 7), `3` idle, none. In this run it set and read back: the default was `none: prio 0`, `-c 3` gave `idle`, `-c 2 -n 4` gave `best-effort: prio 4`. **It only has an effect with an I/O scheduler that implements priorities** (from memory: `bfq`, and `mq-deadline` for some cases; `none` ignores it). The block device here (`vda`) used `none [mq-deadline] kyber bfq`, with `mq-deadline` selected, so a change here is not a statement about performance. On NVMe, whose default scheduler is usually `none`, `ionice` does not work (from memory).
- **cgroups are the modern way to limit and weight**: `cpu.weight`, `cpu.max`, `io.weight`, `memory.max` (cgroup v2). `nice` is per task and advisory; a cgroup limit applies to a group and is enforced.

---

## 10. Daemons, subreapers and PID 1 (and why containers need an init)

### 10.1 A daemon is an orphan made on purpose

A **daemon** is a process with no controlling terminal that outlives its starter. The classic recipe (from the Unix tradition, and what `daemon(3)` does, from memory) is **fork, `setsid`, fork again, `chdir("/")`, reset the `umask`, point descriptors 0, 1 and 2 at `/dev/null`**. The test program does exactly that and reports what the daemon looks like:

```python
#!/usr/bin/env python3
"""daemonize.py: the classic double-fork daemon, then report what it looks like."""
import os, sys, time

def daemonize(report):
    if os.fork():                     # 1: the parent returns to the shell
        return False
    os.setsid()                       # 2: new session, no controlling terminal
    if os.fork():                     # 3: the session leader exits, so the daemon can never reacquire a tty
        os._exit(0)
    os.chdir("/")                     # 4: do not pin a mount point
    os.umask(0)
    fd = os.open("/dev/null", os.O_RDWR)
    for n in (0, 1, 2):
        os.dup2(fd, n)                # 5: no stale tty on stdin, stdout, stderr
    os.close(fd)
    with open(report, "w") as f:
        f.write("pid=%d ppid=%d sid=%d pgid=%d\n" % (os.getpid(), os.getppid(), os.getsid(0), os.getpgrp()))
    time.sleep(3)
    os._exit(0)

if __name__ == "__main__":
    if daemonize(sys.argv[1]):
        pass
    time.sleep(0.5)
    print(open(sys.argv[1]).read(), end="")
```

```text
--- a double-fork daemon: parent PID 1, session id is not its own pid, so it cannot get a terminal
$ daemonize.py $r
pid=3677 ppid=1 sid=3676 pgid=3676
$ setsid --fork sleep 2; sleep 0.3; ps -o pid,ppid,pgid,sid,tty,stat,args -C sleep | sed "s/^ *//"
PID  PPID  PGID   SID TT       STAT COMMAND
3679     1  3679  3679 ?        Ss   sleep 2

--- a subreaper adopts orphans instead of PID 1
$ subreaper.py
subreaper pid 3686
grandchild pid, parent pid after A died: 3688 3686
wait() returned the adopted grandchild: 3688

--- PID 1 of a PID namespace
$ unshare --pid --fork --mount-proc pid1.py
my pid: 1  my ppid: 0
    PID    PPID STAT COMMAND
      1       0 S    python3
      3       1 Z    python3
      4       1 R    ps
after kill(1, SIGTERM): still here
after kill(1, SIGKILL): still here
SIGTERM handler ran

--- the same namespace seen from outside: NSpid lists the pid in each namespace
$ grep -E "^NSpid" /proc/$n/status
NSpid:  3696    1
```

- **Parent PID 1**: the first child called `setsid` and then forked and exited, so the daemon was orphaned and **adopted by PID 1** (`ppid=1`).
- **Session and group ids are those of the first child, not the daemon's own PID** (`sid=3676`, `pgid=3676`, own PID 3677): the daemon is **not** a session leader. Only a session leader can acquire a controlling terminal, so the second fork guarantees that the daemon can never get one by opening a terminal device. The one-step alternative, `setsid --fork sleep 2`, made a session leader (`Ss`) with PPID 1 and no terminal (`?`).
- **Why `cwd` is `/`**: a daemon that stays in a directory keeps that file system busy and `umount` fails.
- **Modern practice** (from memory): under `systemd` a service **does not daemonize**. It runs in the foreground (`Type=simple`), logs to standard output or the journal, and `systemd` keeps it in a cgroup, restarts it and kills the whole cgroup on stop. A service that double-forks needs `Type=forking` and a PID file, which is more fragile. This is [LX 04](00_plan.md).

### 10.2 Who adopts the orphans: PID 1, or a subreaper

When a process dies, its children are **reparented**. By default the new parent is PID 1; a process that has called `prctl(PR_SET_CHILD_SUBREAPER, 1)` becomes the adopter for **all its descendants**, which is what `systemd --user`, container runtimes and process supervisors use, and what the `pid_namespaces(7)` page describes (orphans go to the namespace's init, or to a nearer subreaper). The test sets the flag, creates a child A that forks a grandchild B and exits at once, and reports B's parent:

```python
#!/usr/bin/env python3
"""subreaper.py: PR_SET_CHILD_SUBREAPER makes this process the adopter of its orphaned descendants."""
import ctypes, os, time
libc = ctypes.CDLL(None, use_errno=True)
PR_SET_CHILD_SUBREAPER = 36
libc.prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0)
me = os.getpid()
r, w = os.pipe()
if os.fork() == 0:                      # child A
    if os.fork() == 0:                  # grandchild B outlives A
        time.sleep(0.4)
        os.write(w, b"%d %d\n" % (os.getpid(), os.getppid()))
        os._exit(0)
    os._exit(0)                         # A exits at once: B is an orphan
os.wait()                               # reap A
print("subreaper pid", me)
print("grandchild pid, parent pid after A died:", os.read(r, 100).decode().strip())
print("wait() returned the adopted grandchild:", os.wait()[0])
```

The transcript is the second section above: the subreaper (3686) was **B's parent after A died** (`3688 3686`, not 1), and its own `wait()` returned B. Without the flag the same program would have shown parent 1, and the subreaper would not have received the status.

### 10.3 PID 1 of a PID namespace

A **PID namespace** gives its first process the PID **1** inside, while the host sees a different number: `NSpid: 3696 1` in the transcript is "3696 on the host, 1 inside". `unshare --pid --fork --mount-proc` made one for the test, and the program running as its PID 1 (`pid1.py`) printed the following, which sums up the rules of the `pid_namespaces(7)` page that I fetched and what I measured:

```python
#!/usr/bin/env python3
"""pid1.py: runs as PID 1 of a PID namespace, does not reap, and shows what that means."""
import os, signal, subprocess, time
print("my pid:", os.getpid(), " my ppid:", os.getppid())
pid = os.fork()
if pid == 0:                            # child A makes an orphan B that dies at once
    if os.fork() == 0:
        os._exit(0)
    time.sleep(0.2)
    os._exit(0)
os.waitpid(pid, 0)                      # reap A only, never B
time.sleep(0.2)
print(subprocess.run(["ps", "-e", "-o", "pid,ppid,stat,comm"], capture_output=True, text=True).stdout, end="")
for sig in ("SIGTERM", "SIGKILL"):
    os.kill(1, getattr(signal, sig))    # signal ourselves from inside the namespace
    time.sleep(0.1)
    print("after kill(1,", sig + "): still here")
signal.signal(signal.SIGTERM, lambda s, f: print("SIGTERM handler ran"))
os.kill(1, signal.SIGTERM)
time.sleep(0.1)
```

- **It has `ppid` 0** (its parent is outside the namespace, invisible).
- **It is the reaper for the whole namespace** and nothing reaps for it: the test makes an orphan that dies at once; the orphan became a child of PID 1 and, because PID 1 never waited, **stayed a zombie** (`3 1 Z python3` in the `ps` listing). A container whose main process is an application that does not reap children (a shell script, a Python or Node server that spawns subprocesses) **leaks zombies**, and a long-running one runs out of PIDs.
- **Signals it has no handler for are not delivered by `kill` from inside the namespace**: `kill(1, SIGTERM)` and then **`kill(1, SIGKILL)`** both returned and the program printed "still here" after each. Only after it installed a `SIGTERM` handler did `kill(1, SIGTERM)` run the handler. From the `pid_namespaces(7)` page: signals sent **from an ancestor namespace** are delivered, and `SIGKILL` and `SIGSTOP` from the ancestor always are. So an application running as PID 1 that has no `SIGTERM` handler **ignores `docker stop`'s `SIGTERM`**, and the runtime waits for its timeout (10 s by default, from memory) and then sends `SIGKILL` from outside. This is the cause of "my container takes 10 seconds to stop".
- **When PID 1 of a namespace exits, the kernel kills every other process in it** (`pid_namespaces(7)`: `SIGKILL` to all, and `fork` in that namespace then fails with `ENOMEM`).

The fix is a **real init as PID 1**: `docker run --init` (which injects `tini`, from memory), `tini`, `dumb-init`, or `systemd`; it does exactly three things: **forward signals to the child, reap every adopted child in a `waitpid(-1)` loop, and exit with the main child's status**. Problem E has you write one and runs it in a namespace.

---

## 11. Who has this port, who has this file

Two questions that every on-call engineer gets (the file-system side is [LX 07](00_plan.md), the network side [LX 08](00_plan.md)): "what is listening on 8080?" and "who is using this file or mount?". `ss` is the normal tool for sockets but is not installed in this VM, so the experiment shows `lsof`, `fuser` **and the `/proc` route that all of them use**. The test program holds a listening socket on 127.0.0.1:18080, an established connection to it, and a 64 MiB file that it later deletes:

```python
#!/usr/bin/env python3
"""holder.py: holds a listening socket, a connection, and a big file that it then deletes."""
import os, socket, sys, time
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", 18080)); srv.listen(5)
cli = socket.create_connection(("127.0.0.1", 18080)); conn, _ = srv.accept()
path = sys.argv[1]
f = open(path, "wb"); f.write(os.urandom(1 << 20) * 64); f.flush(); os.fsync(f.fileno())   # 64 MiB
print("ready", flush=True)
sys.stdin.readline()                    # wait for the driver to say 'delete'
os.unlink(path)
print("deleted", flush=True)
sys.stdin.readline()                    # wait for the driver to say 'exit'
```

```text
--- who has port 18080?
$ lsof -nP -i :18080 | awk "{print \$1, \$2, \$4, \$8, \$9, \$10}" | column -t
COMMAND  PID   FD  NODE  NAME
python3  4078  3u  TCP   127.0.0.1:18080                   (LISTEN)
python3  4078  4u  TCP   127.0.0.1:52484->127.0.0.1:18080  (ESTABLISHED)
python3  4078  5u  TCP   127.0.0.1:18080->127.0.0.1:52484  (ESTABLISHED)
$ fuser -v -n tcp 18080 2>&1 | cut -c1-70
                     USER        PID ACCESS COMMAND
18080/tcp:           root       4078 F.... python3

--- the same answer from /proc/net/tcp by hand (no ss in this VM)
$ printf "port 18080 = 0x%X\n" 18080
port 18080 = 0x46A0
$ awk "NR>1 && \$2 ~ /:46A0\$/ {print \$2, \$3, \"state=\" \$4, \"inode=\" \$10}" /proc/net/tcp
0100007F:46A0 00000000:0000 state=0A inode=12131
0100007F:46A0 0100007F:CD04 state=01 inode=12133
$ for x in /proc/[0-9]*/fd/*; do [ "$(readlink $x 2>/dev/null)" = "socket:[$ino]" ] && echo "$x" | cut -d/ -f3 | sed "s/^/owner pid: /"; done | sort -u
owner pid: 4078

--- who has this file open?
$ lsof $f | awk "{print \$1, \$2, \$4, \$5, \$7, \$9}" | column -t
COMMAND  PID   FD  TYPE  SIZE/OFF  NAME
python3  4078  6w  REG   67108864  /var/tmp/hold.YSKZFR/big.dat
$ fuser -v $f 2>&1 | cut -c1-70
                     USER        PID ACCESS COMMAND
/var/tmp/hold.YSKZFR/big.dat:
                     root       4078 F.... python3

--- deleted but still open: du and ls forget it, df does not
df used before delete: 14220440 kB, du of the directory: 65540 kB
df used after delete:  14220440 kB (change 0 kB), du of the directory: 4 kB, ls: 0 files
$ ls -l /proc/$p/fd | grep deleted | awk "{print \$9, \$10, \$11, \$12}"
6 -> /var/tmp/hold.YSKZFR/big.dat (deleted)
$ lsof +L1 2>/dev/null | awk "NR==1||/big.dat/ {print \$1, \$2, \$5, \$7, \$8, \$9, \$10}" | column -t
COMMAND  PID   TYPE  SIZE/OFF  NLINK  NODE    NAME
python3  4078  REG   67108864  0      885411  /var/tmp/hold.YSKZFR/big.dat

--- recover the data, or free the space without killing the process
recovered file size: 67108864 bytes
after truncating through /proc/PID/fd/6: df used 14154904 kB (was 14220440)
```

- **`lsof -nP -i :18080`** lists the listener (`3u`, `LISTEN`) and both ends of the connection (`4u`, `5u`, `ESTABLISHED`) with the command and PID. `-n` and `-P` stop name and service lookups, which can hang a command on a broken DNS. `lsof -iTCP -sTCP:LISTEN -nP` lists all listeners.
- **`fuser -v -n tcp 18080`** names the owner (the `F` in `ACCESS` means the process has the object open). `fuser -k` **kills** whatever has it open: use `-v` first and never `-k` on a mount point you do not understand.
- **By hand from `/proc/net/tcp`** (the data `ss` reads, in another form, from memory): the port is **hexadecimal** (`18080` is `0x46A0`) and the address bytes are in **host byte order** (`0100007F` is 127.0.0.1 on a little-endian machine); state `0A` is `LISTEN` and `01` is `ESTABLISHED`; the last useful field is the **socket inode**. The socket inode appears in `/proc/PID/fd` as `socket:[12131]`, so scanning the descriptors found **owner pid 4078**, the same process `lsof` named. This is the route when no tool is installed in a minimal container.
- **`lsof FILE`** and `fuser -v FILE` list the processes that have the file open (here `6w`, open for writing, `SIZE/OFF 67108864`); `fuser -vm /mnt` lists users of a **mount**, which is the first question when `umount` says "target is busy".

**A file that is deleted but still open** keeps its blocks until the last descriptor closes. The experiment deletes the 64 MiB file while the process holds it, and reads three things:

1. **`df` did not change** (the used figure was identical before and after), **`du` of the directory fell from 65,540 kB to 4 kB**, and `ls` listed nothing: the three tools answer different questions (`df` asks the file system, `du` walks the names).
2. **The holder is findable**: `ls -l /proc/PID/fd` shows `6 -> /var/tmp/hold.XXXXXX/big.dat (deleted)`, and `lsof +L1` lists open files with a **link count below 1** (`NLINK 0`).
3. **Two ways out without restarting**: **copy the data back** with `cp /proc/PID/fd/6 recovered` (the full 67,108,864 bytes came back), or **truncate it through the descriptor** (`: > /proc/PID/fd/6`), after which `df` fell by **65,536 kB**, exactly the file's size (14,220,440 to 14,154,904 kB). This is the standard answer to "the disk is full but `du` finds nothing": a log file was rotated by deleting it while the daemon still writes to it (from memory: the proper fix is a rotation that signals the daemon to reopen, or `copytruncate`).

---

## 12. A triage playbook

Each row is a symptom, the first two commands, and what the answer means. Everything here was produced in the experiments above.

| Symptom | First commands | How to read the answer |
| --- | --- | --- |
| "it is hung" | `ps -o pid,stat,wchan:30,args -p PID`; `cat /proc/PID/{wchan,syscall,stack}` | `R` at 100 percent: spinning (`top -H` shows the thread); `S` in `futex`, `poll`, `read`: waiting for something (find what with `ls -l /proc/PID/fd`); `D`: kernel wait, the stack names it; `T`: stopped, `kill -CONT`; `t`: a debugger holds it |
| "I cannot kill it" | `grep -E 'State|Sig(Blk|Ign|Cgt)|NSpid' /proc/PID/status` | `Z`: kill the parent or fix its `wait`; `D`: wait for the cause; `SigIgn`/`SigBlk` has bit 14 (`TERM`): it ignores or blocks `SIGTERM`, use `SIGKILL`; PID 1 of its namespace with no handler: signal from outside |
| "high load, CPUs idle" | `uptime`; `ps -eo state,pid,wchan:30,args | awk '$1=="D"'`; `cat /proc/pressure/{cpu,io,memory}` | load counts `D` tasks (section 8); PSI `io` or `memory` shows which resource stalls |
| "too many open files" | `ls /proc/PID/fd | wc -l`; `grep 'open files' /proc/PID/limits` | the count against the soft limit; `ls -l /proc/PID/fd` shows what is leaking (thousands of sockets, one deleted log); raise it with `prlimit` for now and in the unit file for good |
| "disk full, `du` finds nothing" | `df -h`; `lsof +L1` | a deleted file still open; truncate through `/proc/PID/fd/N` or restart the holder |
| "address already in use" | `lsof -nP -iTCP:PORT`; `fuser -v -n tcp PORT` | the process that listens; **`TIME_WAIT` sockets are not an owner** (they have no process) |
| "what is this process" | `readlink /proc/PID/{exe,cwd}`; `tr '\0' ' ' < /proc/PID/cmdline`; `pstree -sp PID` | the binary, the directory, the arguments and the chain of parents; `cat /proc/PID/cgroup` names the service or container |
| "memory grows" | `grep -E 'VmRSS|RssAnon|RssFile' /proc/PID/status`; `cat /proc/PID/smaps_rollup` | `RssAnon` growing: heap or mappings; `RssFile` growing: page cache of mapped files; compare `Pss` across processes |
| "zombies pile up" | `ps -eo stat,ppid,pid,args | awk '$1 ~ /^Z/'` | group by `ppid`: that parent never calls `wait`; restarting it clears them |
| "exit status 137, 139, 143" | `kill -l $((status-128))` | 137 is `SIGKILL` (the OOM killer or an operator, [LX 06](00_plan.md)); 139 `SIGSEGV`; 143 `SIGTERM`; 134 `SIGABRT` with a core file |

---

## 13. What it costs: the numbers worth remembering

Measured on one VM; quote them as ranges.

| Quantity | Value |
| --- | --- |
| `ps` `%CPU` against `top` `%CPU` for a process that burned 2 s and slept | 21.6 against 0.0 |
| `VmSize` against `VmRSS` for 64 MiB mapped, 4 MiB touched | 76,456 against 5,984 kB |
| `Pss` against `Rss` for the same process | 4,414 against 5,984 kB |
| standard signals pending: 5 sent, delivered | 1 (real-time: 5 of 5, in order) |
| blocking `read` interrupted by a signal at 1 s, data at 2 s | `EINTR` at 1.0 s, or the data at 2.0 s with `SA_RESTART` |
| `sleep(3)` interrupted at 1 s with `SA_RESTART` | returns early, 1 s left |
| five children exit together | 1 `SIGCHLD` handler call, 5 zombies |
| exit status after death by signal `N` | `128 + N` in the shell (143, 137, 141, 134) |
| core file of the `abort()` test | 356,352 bytes, only with `ulimit -c` above 0 |
| open files before `EMFILE` with `ulimit -n 20` | 17 (3 descriptors were already open) |
| `fork` failures with `ulimit -u 8` as a non-root user | the 8th process (7 children) |
| 1-minute load after 40 s of 3 busy tasks, from 0.55 | 1.75 (formula: 1.74); 63 percent of the way after 60 s |
| CPU share, nice 0 against nice 10 on one CPU | 542 against 58 ticks (90 and 10 percent) |
| same pair on two CPUs | 400 and 398 ticks |
| real-time budget | 950,000 of every 1,000,000 microseconds |
| deleted but open file | `df` unchanged; freed 65,536 kB after truncating through `/proc/PID/fd/N` |

---

## 14. Common mistakes

1. **Reading `VSZ` as memory use** (sections 3 and 6); use `RSS`, and `PSS` when comparing processes.
2. **Reading `ps` `%CPU` as the current rate** (section 3); it is a lifetime average.
3. **`kill -9` as the first signal**: no cleanup; try `SIGTERM`, wait, then `SIGKILL` (section 5).
4. **Expecting `kill -9` to remove a zombie or end a `D` wait** (sections 1 and 2).
5. **Counting on standard signals to be queued** (section 4.2); two `SIGUSR1` can be one.
6. **A `SIGCHLD` handler that reaps one child** (section 4.4); loop on `waitpid(-1, WNOHANG)`.
7. **Not handling `EINTR`** in a program with handlers, especially around `poll`, `epoll_wait`, `sleep` and `accept` (section 4.3).
8. **Doing real work in a signal handler**: `printf`, `malloc`, locks (section 4.7).
9. **Parsing `/proc/PID/stat` by splitting on spaces** (section 6): the name may contain spaces and parentheses; cut after the last `) `.
10. **Reading `cmdline` or `environ` with `cat`**: they are NUL-separated; and `environ` shows the **initial** environment.
11. **Treating load average as CPU use** (section 8); it counts `D` tasks.
12. **Restarting a daemon to raise a limit** you can change with `prlimit`, and setting a limit by hand that vanishes at the next start (section 7).
13. **`nice` on tasks that do not compete**, and **`chrt -f` on a task that can spin** (section 9).
14. **An application as PID 1 of a container** without a `SIGTERM` handler and without reaping (section 10).
15. **`fuser -k` or `pkill -f` with a pattern you did not check**: `pkill -f` matches the shell command line that contains the pattern, and `-k` on a mount kills every user of it.
16. **Deleting a big log that a daemon holds open** and expecting the space back (section 11).
17. **`kill PID` of a shell script** and expecting its children to stop: the signal goes to one process (sections 4 and 5); signal the group (`kill -- -PGID`) or forward in a `trap`.
18. **Believing `nohup` or `disown` fixes a lost session** for a job that still reads the terminal: `nohup` ignores `SIGHUP` only; use `setsid`, `tmux`, or a service (section 5, LX 01).

---

## 15. Questions out loud

Say each answer in 30 to 60 seconds, with one command or one measured number.

1. **Name the process states and what `D` means.** `R` running or runnable, `S` interruptible sleep, `D` uninterruptible sleep, `T` stopped, `t` tracing stop, `Z` zombie. `D` ignores ordinary signals until the wait ends; I made one with a `vfork` parent, and it counts in the load average.
2. **What is a zombie and how do you remove it?** A child that exited and has not been waited for; it holds only its exit status and a PID. `kill -9` did nothing (status 0, still `Z`); the parent must `wait`, or exit so that PID 1 adopts and reaps it.
3. **What is an orphan?** A live process whose parent died. It is adopted by PID 1 or the nearest subreaper (`PR_SET_CHILD_SUBREAPER`); daemons are made this way on purpose.
4. **Why does `ps` show 21.6 percent and `top` 0.0 for the same idle process?** `ps` divides CPU time by lifetime; `top` uses the last interval.
5. **Can a signal be lost?** Yes: standard signals are not queued, so five `SIGUSR1` while blocked ran the handler once; real-time signals queue (five of five, in order). `SIGCHLD` coalesces, so reap in a loop.
6. **Which signals cannot be caught?** `SIGKILL` and `SIGSTOP`; even `trap "" KILL` in `bash` has no effect.
7. **What does `EINTR` mean and how does `SA_RESTART` change it?** A blocking call was interrupted by a handler. With the flag, `read` and `wait` restart; `poll`, `select`, `epoll_wait` and the sleeps never do (measured: `read` returned at 2.0 s with the flag, `EINTR` at 1.0 s without; `sleep` returned early either way).
8. **What do `fork` and `exec` do to signals?** `fork` copies the mask and dispositions and clears pending; `exec` keeps the mask, ignored signals and pending signals, and resets handlers to default (checked through `SigBlk`, `SigIgn`, `SigCgt` and `ShdPnd`).
9. **What is exit status 143, 137, 139?** `128 + N` in the shell: `SIGTERM`, `SIGKILL`, `SIGSEGV`. The shell's convention: `exit 143` looks the same; `waitpid` or Python's `returncode` (-15) tells them apart.
10. **What happens when you press Ctrl-C?** The terminal driver sends `SIGINT` to the **foreground process group** of the terminal (set by the shell with `tcsetpgrp`); with `stty -isig` it arrives as a byte instead. Closing the terminal sends `SIGHUP`.
11. **How do you find which process holds port 8080?** `lsof -nP -iTCP:8080`, or `fuser -v -n tcp 8080`, or hex port in `/proc/net/tcp`, take the inode, and find `socket:[inode]` under `/proc/*/fd`.
12. **`df` says full and `du` says small: why?** A deleted file still held open; `lsof +L1` finds it; truncate through `/proc/PID/fd/N` to free the space (measured: 65,536 kB back).
13. **What is in `/proc/PID`?** `cmdline`, `environ`, `exe`, `cwd`, `fd`, `status`, `stat`, `maps` and `smaps_rollup`, `task`, `limits`, `cgroup`, `ns`, `wchan`, `syscall`, `stack`, `io`.
14. **What does the load average measure?** A damped count of runnable plus uninterruptible tasks, over about 1, 5 and 15 minutes (`L = L*e + n*(1-e)`, checked to 0.01). High with idle CPUs means tasks in `D`; use PSI and per-CPU views.
15. **What does `nice` do and when does it not?** It changes the CFS weight; on one CPU nice 10 against 0 gave 10 and 90 percent; on separate CPUs, 398 and 400 ticks: no effect.
16. **What breaks when an app is PID 1 in a container?** Signals without a handler are ignored (even `SIGKILL` from inside), orphans stay zombies, and its exit kills the namespace. Use `--init`/`tini`.
17. **How do you raise a limit for a running process?** `prlimit --pid PID --nofile=SOFT:HARD`; persistently in the service unit or the container runtime.
18. **How does a daemon detach?** Fork, `setsid`, fork again, `chdir /`, `umask`, descriptors to `/dev/null`; under `systemd` it should not detach at all.

---

## Problems

**Where to solve them.** On any Linux machine with `bash`, `ps`, `procps`, `util-linux` (`unshare`, `setsid`), `coreutils` (`timeout`) and Python 3, as root for the namespace in Problem E (an unprivileged user can use `unshare --user --pid --fork --mount-proc` where user namespaces are enabled). **Run your solution and compare with the stated result** before reading the answer.

**How to approach them.** Start with the question: what fact do I need (a state, a signal mask, a parent), and which `/proc` file or command gives it? Print it for a process you made yourself, then write the program.

### Problem A (E, entry level): parse `/proc/PID/stat` correctly

A copy of `sleep` is installed as `/tmp/a b) c` and started as `"/tmp/a b) c" 100 &`. Print its PID, state letter and parent PID, using **only `/proc`** (no `ps`), in a way that works for this name.

### Problem B (E, entry level): killed or exited?

The shell reports `143` for `sh -c 'kill -TERM $$'` and for `sh -c 'exit 143'`. Show how a program can tell the two apart.

### Problem C (M, medium): a command that ignores SIGTERM

The command `sh -c 'trap "" TERM; while :; do sleep 0.1; done'` ignores `SIGTERM`. (a) Show what `timeout 1 COMMAND` does. (b) Make it stop within about 2 seconds and report the status. (c) What status does `timeout 1 sleep 5` give?

### Problem D (M, medium): the ancestors of a process

Write a shell fragment that prints a process's name and PID, then its parent's, and so on up to the root, using `/proc`. Run it three levels deep and compare with `pstree -s`.

### Problem E (H, hard): a minimal PID 1

Write a program to run as **PID 1** of a PID namespace. It starts two jobs, one of which leaves an orphan; on `SIGTERM` it forwards the signal to every process in the namespace; it reaps **every** child including adopted ones; it exits with status 0 when none is left and prints how many children ended by which cause and how many zombies remain.

---

## Answers

The answers were run, and the checks are in the output.

```bash
#!/usr/bin/env bash
# ans_prob.sh: the answers to the five problems, each run and checked
. "$(dirname "$0")/h.sh"
P=$(dirname "$(readlink -f "$0")"); PATH=$P:$PATH
echo '=== Problem A: pid, state and parent from /proc only, for a process whose name has spaces and a parenthesis'
cp "$(command -v sleep)" "/tmp/a b) c"
"/tmp/a b) c" 100 & p=$!
sleep 0.2
show 'cat /proc/$p/stat | cut -d" " -f1-5'
show 'awk "{print \$1, \$3, \$4}" /proc/$p/stat'
show 'sed "s/^.*) //" /proc/$p/stat | awk -v p=$p "{print p, \$1, \$2}"'
show 'ps -o pid=,stat=,ppid= -p $p'
kill $p; wait $p 2>/dev/null; rm -f "/tmp/a b) c"
echo
echo '=== Problem B: exit 143 or killed by SIGTERM?'
show 'sh -c "kill -TERM \$\$"; echo "shell sees $?"; sh -c "exit 143"; echo "shell sees $?"'
show 'python3 -c "import subprocess as s; print(s.run([\"sh\", \"-c\", \"kill -TERM \$\$\"]).returncode, s.run([\"sh\", \"-c\", \"exit 143\"]).returncode)"'
echo
echo '=== Problem C: a command that ignores SIGTERM and a timeout that still works'
loop='trap "" TERM; while :; do sleep 0.1; done'
setsid bash -c "echo \$\$ > /tmp/_sid; exec timeout 1 sh -c '$loop'" & sleep 3
sid=$(cat /tmp/_sid); echo "plain timeout 1, looked at after 3 s:"
ps -o pid=,stat=,etimes=,args= --sid $sid | sed -E 's/^ *[0-9]+ /PID /' | cut -c1-80
pkill -KILL -s $sid; wait 2>/dev/null; rm -f /tmp/_sid
SECONDS=0; timeout -k 1 1 sh -c "$loop"; echo "timeout -k 1 1:       status $? after $SECONDS s"
SECONDS=0; timeout 1 sleep 5; echo "well-behaved command: status $? after $SECONDS s"
echo
echo '=== Problem D: the ancestors of a process, walked through /proc'
cat > /tmp/_anc.sh <<'S'
p=$$
while [ "$p" -gt 0 ]; do
    printf '%s(%s) ' "$(cat /proc/$p/comm)" "$p"
    p=$(awk '/^PPid:/ {print $2}' /proc/$p/status)
done
echo
S
echo 'pstree -s $$' >> /tmp/_anc.sh
unshare --pid --fork --mount-proc sh -c 'sh -c "bash /tmp/_anc.sh; true"; true' | sed -E 's/\([0-9]+\)/(N)/g'
rm -f /tmp/_anc.sh
echo
echo '=== Problem E: a minimal PID 1 that forwards SIGTERM and reaps'
unshare --pid --fork --mount-proc mini_init.py & u=$!
sleep 1
init=$(pgrep -f 'python3.*mini_init.py' | tail -1)
echo "host pid of the namespace's PID 1 is not 1: NSpid: $(awk '/^NSpid/ {print $2, $3}' /proc/$init/status)"
kill -TERM $init
wait $u; echo "init exited with status $?"
```

```text
=== Problem A: pid, state and parent from /proc only, for a process whose name has spaces and a parenthesis
$ cat /proc/$p/stat | cut -d" " -f1-5
4634 (a b) c) S
$ awk "{print \$1, \$3, \$4}" /proc/$p/stat
4634 b) c)
$ sed "s/^.*) //" /proc/$p/stat | awk -v p=$p "{print p, \$1, \$2}"
4634 S 4625
$ ps -o pid=,stat=,ppid= -p $p
 4634 S     4625

=== Problem B: exit 143 or killed by SIGTERM?
$ sh -c "kill -TERM \$\$"; echo "shell sees $?"; sh -c "exit 143"; echo "shell sees $?"
Terminated
shell sees 143
shell sees 143
$ python3 -c "import subprocess as s; print(s.run([\"sh\", \"-c\", \"kill -TERM \$\$\"]).returncode, s.run([\"sh\", \"-c\", \"exit 143\"]).returncode)"
-15 143

=== Problem C: a command that ignores SIGTERM and a timeout that still works
plain timeout 1, looked at after 3 s:
PID Ss         3 timeout 1 sh -c trap "" TERM; while :; do sleep 0.1; done
PID S          3 sh -c trap "" TERM; while :; do sleep 0.1; done
PID S          0 sleep 0.1
bash:  4648 Killed                  setsid bash -c "echo \$\$ > /tmp/_sid; exec timeout 1 sh -c '$loop'"
bash:  4687 Killed                  timeout -k 1 1 sh -c "$loop"
timeout -k 1 1:       status 137 after 2 s
well-behaved command: status 124 after 1 s

=== Problem D: the ancestors of a process, walked through /proc
bash(N) sh(N) sh(N)
sh---sh---bash---pstree

=== Problem E: a minimal PID 1 that forwards SIGTERM and reaps
pid 1 started 2 jobs
host pid of the namespace's PID 1 is not 1: NSpid: 4727 1
reaped: SIGTERM x2, exit 0 x1
zombies left: 0
init exited with status 0
```

**Problem A.** The naive `awk '{print $1, $3, $4}'` printed `b) c)` as the "state" and the "parent" because the name `a b) c` has two spaces and a parenthesis. The correct parse removes everything up to the **last `) `** (`sed 's/^.*) //'`), after which the first field is the state and the second the parent: `S` and the shell's PID, the same as `ps -o stat=,ppid=` printed. (Reading `/proc/PID/status` with `awk '/^State:|^PPid:/'` avoids the problem altogether, and is the safer choice when speed does not matter.)

**Problem B.** In the shell, both print 143. A program that uses `waitpid` sees a different **kind** of status: `WIFSIGNALED` with `WTERMSIG` 15 against `WIFEXITED` with `WEXITSTATUS` 143. Python's `subprocess` reports a death by signal as a **negative** return code: `-15` for the first command and `143` for the second. `bash` itself printed `Terminated` for the first (job-control message), which the second did not print. The 128 offset is a shell convention to make the two spaces share one number; it is ambiguous by design, and the one real ambiguity is a program that calls `exit(143)` for its own reasons.

**Problem C.** (a) After 3 s the plain `timeout 1` was **still running** (`timeout` in `Ss`, the `sh` and its `sleep 0.1`, all with an elapsed time of 3 s): `timeout` sent `SIGTERM` to the child and to its process group, the `sh` ignored it, the `sleep` children died and were restarted by the loop. A plain `timeout` never gets rid of such a command. I had to kill the session by hand. (b) **`timeout -k 1 1 COMMAND`** sends `SIGTERM` after 1 s and **`SIGKILL` 1 s later**: it finished after **2 s** with status **137**, and `bash` printed `Killed` because `timeout` re-raises the signal on itself (the first `Killed` line in the transcript is my cleanup of the stuck job from (a)). (c) `timeout 1 sleep 5` finished after 1 s with status **124**, the status `timeout` itself uses for "the command timed out" (from `timeout`'s manual page, as I know it; the transcript shows 124). The lesson: **always give a kill-after when the command might ignore `SIGTERM`**, and check for 124 against 137.

**Problem D.** The loop prints `comm` and PID, then reads `PPid:` from `/proc/PID/status` and repeats until the parent is 0. The output was `bash(N) sh(N) sh(N)` (PIDs masked), from the script upwards, and `pstree -s` printed the same chain from the top down: `sh---sh---bash---pstree`. The last `sh` is PID 1 of the namespace used for a short, repeatable result. Reading `status` rather than `stat` avoids the name-with-parenthesis trap of Problem A.

**Problem E.** The program:

```python
#!/usr/bin/env python3
"""mini_init.py: a 30-line PID 1: start the jobs, forward SIGTERM, reap every child, exit when none is left."""
import collections, os, signal, subprocess, sys, time

def on_term(sig, frame):
    os.kill(-1, signal.SIGTERM)           # in a PID namespace: every process except ourselves

signal.signal(signal.SIGTERM, on_term)
subprocess.Popen(["sleep", "30"])
subprocess.Popen(["sh", "-c", "(sleep 30 &); exit 0"])    # leaves an orphan that PID 1 must adopt
print("pid", os.getpid(), "started 2 jobs", flush=True)
seen = collections.Counter()
while True:
    try:
        pid, status = os.wait()           # blocks until any child, adopted orphans included, changes state
    except ChildProcessError:
        break                             # no children left
    except InterruptedError:
        continue
    seen["SIG" + signal.Signals(os.WTERMSIG(status)).name[3:] if os.WIFSIGNALED(status) else "exit %d" % os.WEXITSTATUS(status)] += 1
print("reaped:", ", ".join("%s x%d" % kv for kv in sorted(seen.items())), flush=True)
zombies = [p for p in os.listdir("/proc") if p.isdigit() and open("/proc/%s/stat" % p).read().rsplit(")", 1)[1].split()[0] == "Z"]
print("zombies left:", len(zombies))
```

It printed `reaped: SIGTERM x2, exit 0 x1`, `zombies left: 0`, and the namespace exited with status 0. The two `SIGTERM` deaths are the direct `sleep 30` and the **orphan `sleep 30`** left by the shell that exited (the third child, the shell itself, ended with exit 0); the orphan could only be reaped because PID 1 loops on `os.wait()` and receives **adopted** children too. The pieces that matter are the three of section 10.3: `os.kill(-1, SIGTERM)` is the forwarding (in a PID namespace `-1` means every process except the caller), the loop is the reaping, and the exit condition `ChildProcessError` (`ECHILD`) is "no children left". The host saw the program as `NSpid: 4727 1`. A production init also forwards other signals, handles `SIGCHLD` without losing exits (here a blocking `wait` is enough because it sleeps in the call), forwards `SIGINT`, and starts the child in its own process group; `tini` is about 500 lines of C for that reason.

---

## Cheat sheet

```text
States          R run/runnable  S sleep (signals wake it)  D uninterruptible (counts in load)  T stopped  t tracer stop  Z zombie
ps              ps -o pid,ppid,pgid,sid,stat,wchan:30,args -p PID   -L threads   aux = BSD style   -ef = System V style
                %CPU in ps = CPU time / lifetime     top = last interval     RSS counts shared pages  ->  use PSS
Signals         uncatchable: KILL STOP    standard signals coalesce (1 pending), RT signals queue (in order)    kill -0 PID tests existence
                kill -SIG -PGID signals a group     128+N in $? = killed by N (shell convention)     143 TERM  137 KILL  139 SEGV  134 ABRT  141 PIPE
Handlers        async-signal-safe only: set a flag or write to a pipe;  EINTR: poll/select/epoll/sleep never restart;  SA_RESTART restarts read/wait
fork / exec     fork: mask + dispositions copied, pending cleared    exec: mask, ignored and pending kept, handlers reset
Terminal        ^C INT  ^\ QUIT  ^Z TSTP to the foreground group    close terminal -> HUP    stty -isig: keys become data    setsid / nohup / tmux
/proc/PID       status stat cmdline(NUL) environ(NUL) exe cwd fd/ fdinfo/ maps smaps_rollup task/ limits cgroup ns/ wchan syscall stack io
                stat field 2 holds the name: cut after the LAST ") "     SigPnd ShdPnd SigBlk SigIgn SigCgt: bit N-1 = signal N
Limits          ulimit -n -u -v -c -s    prlimit --pid PID --nofile=S:H    EMFILE / EAGAIN (root exempt from NPROC) / ENOMEM    /proc/PID/limits
Load            L = L*e + n*(1-e), e = exp(-5/60), n = R + D tasks     1 min > 15 min: rising     idle CPUs + high load = D     /proc/pressure/{cpu,io,memory}
Scheduling      nice -n 10 / renice (only matters when tasks compete)    chrt -f -p 10 PID (starves normal tasks; rt throttle 95%)
                taskset -cp 1 PID    ionice -c3 (needs bfq or similar)    cgroups cpu.weight cpu.max memory.max
Daemons         fork, setsid, fork, chdir /, umask, fds to /dev/null     systemd: do not daemonize     subreaper: prctl(PR_SET_CHILD_SUBREAPER)
PID 1 in ns     ignores signals without a handler (even KILL from inside)    must reap with waitpid(-1) loop    exit kills the namespace    docker run --init / tini
Who has it      lsof -nP -iTCP:PORT   fuser -v -n tcp PORT   lsof FILE   fuser -vm /mnt   /proc/net/tcp (hex port, state 0A listen, inode) -> /proc/*/fd
Disk full       df vs du differ: deleted but open;  lsof +L1;  cp /proc/PID/fd/N out  or  : > /proc/PID/fd/N
Triage          state + wchan -> limits -> fds -> signals mask -> parent -> cgroup;  never start with kill -9
```

---

## Sources

Fetched in this session (what each confirmed):

- [signal(7)](https://man7.org/linux/man-pages/man7/signal.7.html): `SIGKILL` and `SIGSTOP` cannot be caught, blocked or ignored; standard signals are not queued, real-time signals are queued in order; the default actions (terminate for `HUP INT PIPE TERM USR1`, terminate with core for `QUIT SEGV`, ignore for `CHLD`, stop for `STOP TSTP`, continue for `CONT`); calls restarted with `SA_RESTART` and those that always fail with `EINTR` (`poll`, `select`, `epoll_wait`, the sleeps, `pause`, `sigsuspend`); `fork` copies dispositions and mask and clears pending; `execve` resets handled signals, keeps ignored ones, and preserves the mask and the pending set.
- [Linux load averages: solving the mystery](https://www.brendangregg.com/blog/2017-08-08/linux-load-averages.html) (Brendan Gregg): the load counts running, runnable and uninterruptible tasks; the `D` state was added in Linux 0.99.14 (1993); the averages are exponentially damped sums sampled about every 5 seconds, about 0.62 after one minute for one busy thread; a rising 1-minute value against 5 and 15 minutes means rising load; better metrics are per-CPU utilisation, per-process CPU, run-queue latency and the USE method. It does **not** mention pressure stall information.
- [pid_namespaces(7)](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html): signals from inside a namespace to its PID 1 are delivered only if it has a handler; `SIGKILL` and `SIGSTOP` from an ancestor namespace always are; orphans are reparented to the namespace's init or a nearer subreaper; when PID 1 exits all other processes in the namespace are killed and later `fork` fails with `ENOMEM`.

Run in this session: procps-ng 4.0.4 (`ps`, `top`, `pgrep`, `pkill`), util-linux (`unshare`, `setsid`, `chrt`, `taskset`, `ionice`, `prlimit`, `setpriv`), psmisc (`fuser`, `pstree`), lsof, coreutils `timeout`, gdb, GCC and Python 3, on Linux 6.18.44 (2 vCPUs, about 8 GiB), as root, with PID 1 `process_api`. Not installed, so not used: `htop`, `ss`, `pidstat`, `mpstat`, `ltrace`, `stress-ng`, `netstat`.

Not retrieved: the `proc(5)` manual page and the kernel's PSI documentation (the `/proc/pressure` explanation and the claim that the CPU `some` figure is averaged over CPUs are **from memory** and the number itself, 49.36, is from the run).

From memory, not fetched or not tested in this session: the CFS weight table values 1024 and 110 (the 90 and 10 percent result is the evidence), that `D` waits of the "killable" kind end on `SIGKILL`, that `SA_NOCLDWAIT` and `SIG_IGN` for `SIGCHLD` make the kernel discard child status, that `daemon(3)` and `systemd` behave as described in section 10.1, `docker stop`'s default 10 second timeout and `--init` using `tini`, the `/proc/net/tcp` byte order and state codes, `RLIMIT_NPROC` being bypassed by `CAP_SYS_RESOURCE`, real-time throttling being switched off by -1, `ionice` needing `bfq` or similar, that `vmstat`'s `b` counts I/O waiters, `limits.conf` not applying to `systemd` services, the `hidepid` mount option, `systemd-coredump` and `coredumpctl`, the `syscall` number 230 being `clock_nanosleep`, `SIGHUP` as "reload" for common daemons, and the cgroup v2 path format.

Cross-references: [LX 01](01_shell_and_scripting.md) (job control, `trap`, exit statuses, `setsid`, `nohup`), [LX 02](02_text_processing_and_one_liners.md) (the pipelines used to read `ps` and `/proc`), [OS 02](../05_operating_systems/02_processes.md), [OS 03](../05_operating_systems/03_threads.md), [OS 05](../05_operating_systems/05_process_scheduling.md), [OS 13](../05_operating_systems/13_numa.md), [OS 17](../05_operating_systems/17_ipc_fundamentals.md), [OS 21](../05_operating_systems/21_os_interview_question_bank.md), the [debugging track](../16_debugging/00_plan.md).
