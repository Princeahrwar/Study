# OS 17: IPC fundamentals

*Part of the [Operating systems plan](00_plan.md) · Time: about 12 hours (8 reading and experiments, 4 problems) · You need: [OS 02](02_processes.md) (`fork`, `exec`, `wait`), [OS 06](06_locks.md) and [OS 07](07_condition_variables_and_semaphores.md) (mutexes, condition variables, semaphores), [OS 11](11_virtual_memory.md) (shared mappings) and [OS 15](15_file_systems.md) (file descriptors and open file descriptions)*

This is a new tutorial. Two processes have separate address spaces, so anything they share must go through something the kernel provides. The interview questions come in a fixed order. What are the mechanisms, and what does each one move, name and remember? How does a pipe behave when it is full, empty, or closed at one end, and what does `PIPE_BUF` promise? What can a signal carry, which signals queue, and what may a handler do? What are a process group, a session and a daemon? How do you share memory between processes, and what must you add to it (a lock that survives the death of its owner)? What is the difference between a POSIX and a System V semaphore, message queue and shared-memory segment? How do `flock`, `fcntl` record locks and open file description locks differ, and why is the `fcntl` rule about closing a descriptor a famous trap? What do Unix domain sockets add? And finally: how long does each mechanism take, and which one do you pick? The last tutorial in this group, [OS 18](18_ipc_mechanisms.md), goes deeper into `eventfd`, futexes, descriptor passing and netlink; this one is the foundation under it.

**Two honest limits, stated first.** (1) The machine is a Firecracker micro-VM with 2 virtual CPUs. A wake-up of a thread on **another** CPU costs an inter-processor interrupt, which a VM makes expensive: the cross-CPU round trips below (about 20 to 30 µs) are far slower than on bare metal (single-digit microseconds, from memory), while the same-CPU numbers (about 2 µs) are closer to a real machine. I report both and never mix them. (2) There is no terminal here, so job control with a real controlling terminal (`Ctrl-C`, `Ctrl-Z`, `SIGTTIN`) is described from the manual pages and not run; the process-group and session mechanics are run. Network sockets appear only as a comparison; the rest of that is [OS 18](18_ipc_mechanisms.md) and the networking track.

By the end you can:

- draw the map of IPC mechanisms by what each moves, whether it keeps message boundaries, what name it has and how long it lives, and pick one (measured: a pipe round trip takes 24,505 ns across CPUs and 2,112 ns on one CPU; a shared-memory flag with spinning takes 193 ns);
- predict what a pipe does when full, empty and closed, and what `PIPE_BUF` guarantees (measured: capacity 65,536 bytes; two writers of 4,096-byte records mixed none, of 4,097-byte records mixed thousands);
- say which signals queue and which do not, and write a safe handler (measured: 100 `SIGUSR1` sent while blocked ran the handler once; 100 real-time signals ran it 100 times, in order);
- explain process groups, sessions and the daemon idiom, and what `setsid` does (measured: a new session leader has no controlling terminal; `setsid` as a group leader fails with `EPERM`);
- share memory in five ways and say what each one is named by (measured: a private mapping stays private after `fork`, a shared one does not; a plain counter incremented by two processes ended at 2,167,745 instead of 4,000,000);
- use a process-shared robust mutex and recover from an owner that dies holding it (measured: a non-robust mutex timed out after 300 ms, a robust one returned `EOWNERDEAD` at once);
- compare POSIX and System V semaphores and message queues, including `SEM_UNDO` and message types (measured: a dead holder without `SEM_UNDO` leaves the semaphore at 0, with it restores 1);
- tell `flock`, `fcntl` locks and open file description (OFD) locks apart (measured: closing a second descriptor released an `fcntl` lock and did not release an OFD lock);
- choose between pipes, Unix sockets, queues and shared memory by measured cost, not habit (measured: a shared-memory ring moved 4 KiB messages at 7998.5 MB/s against 2376.0 MB/s for a pipe).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run as root on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory. The latency benchmark forks a child, pins the parent to CPU 0 and the child to CPU 1 (or both to CPU 0), and times 20,000 one-byte round trips seven times, reporting the median. The kernel behaviour was checked against the manual pages for `pipe(7)`, `signal(7)`, `signal-safety(7)`, `credentials(7)`, `setsid(2)`, `flock(2)`, the `fcntl` lock operations, `mq_overview(7)`, `shm_overview(7)`, `sem_overview(7)`, `unix(7)`, `signalfd(2)`, `sysvipc(7)` and `pthread_mutexattr_setrobust(3)`, all fetched; see Sources for what each did and did not say. Facts marked "from memory" were not fetched.

**Data and timing.** Latencies are medians of seven batches, and the minimum and maximum are printed beside them. Run-to-run differences of 10 to 20 percent are normal in a VM. Counts (bytes accepted, signals delivered) are deterministic, except the mixed-record counts in section 2.3, which vary with scheduling and are shown for two runs.

---

## 1. The map: what IPC is for and what each mechanism gives

Two models cover everything. **Message passing**: one process hands bytes to the kernel and another takes them out; the kernel copies twice (user to kernel, kernel to user), orders the bytes and blocks the sender or the receiver when it must. **Shared memory**: the kernel maps the same physical pages into both address spaces once, and afterwards the processes read and write them with ordinary loads and stores, with no kernel involvement and no copy, but also **no ordering, no blocking and no protection from each other's races** unless you add synchronisation. Everything below is one of the two, or a synchronisation tool that goes with them (semaphores, locks, signals).

| Mechanism | Moves | Boundaries kept | Named by | Lives until |
| --- | --- | --- | --- | --- |
| pipe | bytes | no | a pair of descriptors, inherited | the last descriptor is closed |
| FIFO (named pipe) | bytes | no | a path in a file system | the path is unlinked and all opens close |
| signal | a signal number (plus an integer for real-time signals) | no payload | a process id | delivered or discarded |
| Unix socket, stream | bytes | no | descriptors or a path or an abstract name | the last descriptor is closed (path: also unlink) |
| Unix socket, datagram or seqpacket | messages | yes | same | same |
| POSIX message queue | messages with priority | yes | `/name` | `mq_unlink` or shutdown |
| System V message queue | typed messages | yes | an id from a key | `msgctl(IPC_RMID)` (from memory) |
| shared memory (anonymous, POSIX, `memfd`, System V) | memory | not applicable | none, `/name`, descriptor, or id | depends on the kind (section 5.1) |
| POSIX or System V semaphore | a counter | not applicable | `/name`, or memory, or an id | `sem_unlink` or shutdown; System V until removed |
| `flock`, `fcntl` locks | a lock on a file or a byte range | not applicable | the file | the descriptor is closed, or the process exits |

Three questions sort them. **Does it carry data or only a signal?** (semaphores, signals, locks carry none.) **Does it keep message boundaries?** (a byte stream makes you frame your own messages.) **Who can find it and how long does it live?** (a pipe is private to a family of processes and dies with them; a POSIX queue has a name any process with permission can open and **outlives every process** until someone unlinks it, which is also how it leaks, section 5.1.)

**What an IPC call costs**, in four parts that the measurements in section 10 separate: the **system call** itself (a few hundred nanoseconds), the **copy** of the bytes (twice for message passing), the **wake-up** of a sleeping peer (the scheduler puts it on a run queue, possibly on another CPU, which in this VM costs an inter-processor interrupt), and the **context switch** to it. For small messages the wake-up dominates: that is why a pipe round trip of one byte takes about the same time as a semaphore post and wait, and why a design that never sleeps (spinning on shared memory) is a hundred times faster and burns a CPU to do it.

---

## 2. Pipes and FIFOs

### 2.1 What a pipe is

`pipe(fd)` returns two descriptors, `fd[0]` to read and `fd[1]` to write, over a **kernel buffer** with no name and no file on disk. It is the oldest Unix IPC and the one every shell uses. Because a child inherits descriptors through `fork` (OS 15, section 1), a pipe connects related processes: create it before `fork`, and parent and child each hold both ends. The manual page (`pipe(7)`, fetched) gives the capacity: **16 pages, 65,536 bytes with 4 KiB pages** since Linux 2.6.11, adjustable with `F_SETPIPE_SZ` up to `/proc/sys/fs/pipe-max-size` for unprivileged processes (default 1 MiB, which is the value here; the program runs as root, and a request of 1 MiB returned 1,048,576). The program fills pipes with non-blocking writes until `EAGAIN`:

```text
default capacity (F_GETPIPE_SZ): 65536 bytes
bytes accepted by 4096-byte writes before EAGAIN:  65536
bytes accepted by 1-byte writes before EAGAIN:     65536
packet mode (O_DIRECT), 1-byte writes accepted:    16
F_SETPIPE_SZ to 1 MiB returned 1048576; bytes accepted: 1048576
F_SETPIPE_SZ to 100 returned 4096 (rounded up to a page); F_GETPIPE_SZ now 4096
two 5-byte writes, one read with a 100-byte buffer returned 10 bytes: no message boundaries
after the last writer closed, read returned 0 (end of file)
write to a pipe with no reader, default SIGPIPE: child killed by SIGPIPE
with SIGPIPE ignored: write returned -1, errno EPIPE
non-blocking read of an empty pipe with a writer open: -1, errno EAGAIN
mkfifo made /tmp/h3_fifo, file type FIFO, link count 1, size 0
open for writing, non-blocking, with no reader: -1, errno ENXIO
open for reading with no writer yet blocked for 200 ms until the other side opened
read 2 bytes through the FIFO, and the data exists only in the kernel (file size stays 0)
```

(That output continues in section 2.2 and 2.5; the first six lines are the capacity experiment.) Reading it:

- **65,536 bytes** accepted with 4,096-byte writes **and** with 1-byte writes. A pipe is a byte buffer, not a queue of messages: small writes are merged into the same page.
- **16 bytes** in **packet mode** (`pipe2(O_DIRECT)`): each `write` becomes one packet that fills one of the 16 slots, so 16 one-byte writes fill the pipe, and the reader gets one packet per `read`. This mode is rarely used; I did not run it past capacity.
- `F_SETPIPE_SZ` to 1 MiB works and the pipe then accepted exactly 1,048,576 bytes. A request for 100 bytes is **rounded up to one page**, 4,096 (the page says the minimum is the page size).

### 2.2 Full, empty, closed

The rules are the ones the second half of the output shows:

- **Reading** returns what is there (up to the count asked) and does not wait for the count: two 5-byte writes and a read with a 100-byte buffer returned **10 bytes** in one call. **There are no message boundaries.** Reading an empty pipe **blocks** while any write end is open; **non-blocking** it returns `-1` with `EAGAIN`.
- **End of file.** When **every** write end is closed, `read` returns **0** once the buffered data is gone. This is why a pipe user must close the write ends it does not use: a reader whose own process still holds a write end open waits forever (section 2.6).
- **Writing** to a full pipe blocks, or returns `EAGAIN` if non-blocking. Writing when **no read end is open** raises **`SIGPIPE`**, which kills the process by default; with the signal ignored, `write` fails with **`EPIPE`**. The program shows both: the child writing to a closed pipe was killed by `SIGPIPE`, and with `SIG_IGN` the same write returned -1 with `EPIPE`. A server that writes to sockets or pipes should ignore `SIGPIPE` and handle `EPIPE`.

### 2.3 Atomic writes and `PIPE_BUF`

When several processes write to one pipe, can their bytes interleave? The manual page says: **a write of at most `PIPE_BUF` bytes (4,096 on Linux) is atomic**; larger writes **may be interleaved** with other writers. The program starts two writers that each send 20,000 records of one repeated letter (`A` or `B`), and a reader that takes whole records and counts the ones that contain both letters. Two runs:

```text
two writers,    100-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers,   4096-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers,   4097-byte writes: 40000 records read, 2950 mixed (a record containing both A and B)
two writers,   6000-byte writes: 40000 records read, 1215 mixed (a record containing both A and B)
two writers,  32768-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers,  65536-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers, 100000-byte writes: 40000 records read, 39240 mixed (a record containing both A and B)
```

```text
two writers,    100-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers,   4096-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers,   4097-byte writes: 40000 records read, 4723 mixed (a record containing both A and B)
two writers,   6000-byte writes: 40000 records read, 1491 mixed (a record containing both A and B)
two writers,  32768-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers,  65536-byte writes: 40000 records read, 0 mixed (a record containing both A and B)
two writers, 100000-byte writes: 40000 records read, 39014 mixed (a record containing both A and B)
```

- **100 and 4,096 bytes: no mixed records** in either run. That is the guarantee.
- **4,097 bytes: thousands mixed**, one byte over the limit. This is the cliff the manual page describes.
- **6,000 and 100,000 bytes: mixed.** The 100,000-byte writes mixed in almost every record.
- **32,768 and 65,536 bytes: no mixed records in these runs.** That is **not** a guarantee: the page promises atomicity only up to `PIPE_BUF`, I did not find out why these sizes happened to stay whole here, and a different kernel or a different mix of writers can break them. **Keep every message under 4,096 bytes if several processes write to one pipe, or give each writer its own pipe.**

### 2.4 FIFOs

A **FIFO** (named pipe) is a pipe with a **path** in a file system, made by `mkfifo`. The data is still only in the kernel; the file stays size 0. The program shows:

- `open` for **reading blocks until some process opens it for writing** (it blocked 200 ms until the other side arrived), and the reverse; a non-blocking open for writing with no reader fails with **`ENXIO`**.
- A FIFO is a rendezvous for **unrelated** processes: they need only the path. It has the same capacity, atomicity and `SIGPIPE` rules as an anonymous pipe.

### 2.5 Throughput: the write size and the pipe size

One writer and one reader on different CPUs move 256 MiB, with the default 64 KiB pipe and with a 1 MiB pipe:

```text
one-way pipe throughput, 256 MiB, writer on CPU 0, reader on CPU 1
write size   64 KiB pipe    1 MiB pipe
      64 B         56 MB/s         54 MB/s
     512 B        327 MB/s        400 MB/s
    4096 B       1746 MB/s       1733 MB/s
   16384 B       2315 MB/s       2288 MB/s
   65536 B       1557 MB/s       2869 MB/s
  262144 B       1363 MB/s       3278 MB/s
```

- **Small writes are call-bound.** 64-byte writes reach only about 55 MB/s: each `write` is a system call plus a wake-up check, about 1.1 µs here, so the bytes per second follow the **number** of calls. Batch your writes.
- **The sweet spot is a few pages**, 16 KiB writes reaching about 2,300 MB/s on both pipes.
- **A write as large as the pipe stalls the overlap.** At 64 KiB writes into a 64 KiB pipe the rate falls to about 1,560 MB/s; with a 1 MiB pipe the same writes run at about 2,870 and the 256 KiB writes at 3,280. My reading (not proved): when one write fills the whole pipe the writer must wait for the reader to empty it before continuing, so the two cannot work at the same time; a bigger pipe lets the writer fill one part while the reader drains another.

The shared-memory ring of section 10 moves 4 KiB messages about three times faster than any pipe row here. A pipe also has **two copies** per byte (user to kernel, kernel to user, from memory of the implementation), which is part of the gap. Linux has zero-copy variants for moving data between descriptors (`splice`, `vmsplice`, `tee`); I did not measure them.

### 2.6 A shell pipeline from first principles

`a | b | c` is three processes connected by two pipes, set up by the shell with `pipe`, `fork`, `dup2` and `exec`. The program builds `printf ... | sort | tr a-z A-Z`:

```c
static void run_pipeline(int close_unused)
{
    char *c1[] = { "printf", "pear\\napple\\nfig\\n", NULL };
    char *c2[] = { "sort", NULL };
    char *c3[] = { "tr", "a-z", "A-Z", NULL };
    char **cmds[] = { c1, c2, c3 };
    int p[2][2], prev = -1;
    pid_t pids[3];
    for (int i = 0; i < 3; i++) {
        if (i < 2) IGN(pipe(p[i]));
        pids[i] = fork();
        if (pids[i] == 0) {
            if (prev >= 0) { dup2(prev, 0); close(prev); }
            if (i < 2) { dup2(p[i][1], 1); close(p[i][1]); }
            if (close_unused && i < 2) close(p[i][0]);              /* the read end of the pipe we write to */
            execvp(cmds[i][0], cmds[i]);
            _exit(127);
        }
        if (prev >= 0) close(prev);
        if (i < 2) { if (close_unused) close(p[i][1]); prev = p[i][0]; }  /* the parent must drop its write end */
    }
    for (int i = 0; i < 3; i++) waitpid(pids[i], NULL, 0);
}
```

```text
pipeline  printf 'pear apple fig' | sort | tr a-z A-Z  built with pipe, fork, dup2, exec:
APPLE
FIG
PEAR
```

The three rules that make it work, and the three ways it hangs:

1. **Each child redirects its standard input and output** with `dup2` (descriptor 0 from the previous pipe's read end, descriptor 1 to the next pipe's write end) and then **closes the original pipe descriptors**, because `exec` keeps open descriptors.
2. **The parent closes its copies of the write ends** right after forking the writer, and each child closes the read end of the pipe it writes to. If **any** process keeps a write end open, the reader downstream **never sees end of file** and the pipeline hangs. This is the commonest pipe bug; the program's `close_unused` argument is the switch (I did not run the hanging variant, since it does not terminate).
3. **The parent waits for all children**, or they become zombies (OS 02).

---
## 3. Signals

### 3.1 What a signal is

A **signal** is a small asynchronous notification sent to a process (or a thread) and identified by a number. It carries almost no data: the number, plus a few fields in `siginfo_t` (who sent it, why, and for a real-time signal one integer or pointer). Sources are the **kernel** (`SIGSEGV` on a bad access, `SIGPIPE` on a broken pipe, `SIGCHLD` when a child changes state, `SIGALRM` from a timer), the **terminal driver** (`Ctrl-C` sends `SIGINT`) and **other processes** through `kill` and `sigqueue`. For each signal a process has a **disposition**: the default action (terminate, terminate and dump core, ignore, stop, continue), **ignore**, or a **handler** function. `SIGKILL` and `SIGSTOP` can be neither caught, blocked nor ignored.

| Signal | Number on x86 Linux | Default action | Typical use |
| --- | --- | --- | --- |
| `SIGHUP` | 1 | terminate | terminal closed; reload configuration by convention |
| `SIGINT` | 2 | terminate | `Ctrl-C` |
| `SIGKILL` | 9 | terminate, cannot be caught | force kill |
| `SIGUSR1`, `SIGUSR2` | 10, 12 | terminate | user-defined |
| `SIGSEGV` | 11 | core dump | invalid memory access |
| `SIGPIPE` | 13 | terminate | write to a pipe or socket with no reader |
| `SIGALRM` | 14 | terminate | timer expired |
| `SIGTERM` | 15 | terminate | polite request to exit |
| `SIGCHLD` | 17 | ignore | a child exited or stopped |
| `SIGSTOP`, `SIGTSTP` | 19, 20 | stop | stop a job; `Ctrl-Z` is `SIGTSTP` |
| `SIGCONT` | 18 | continue | resume a stopped job |
| `SIGRTMIN` to `SIGRTMAX` | 34 to 64 here | terminate | real-time, queued, ordered |

(The numbers are the x86 Linux ones, from memory; they differ on other architectures, so code uses the names. The kernel provides real-time signals 32 to 64 and the C library keeps the first two or three for its own threads, so `SIGRTMIN` is 34 here and the manual page says it may be 34 or 35. Programs should write `SIGRTMIN + n`.)

### 3.2 Pending, blocked, delivered: which signals queue

A signal is **generated**, then **pending** until the target is ready, then **delivered** (the handler runs or the default action happens). A thread can **block** a signal with a mask (`sigprocmask`, `pthread_sigmask`): a blocked signal stays pending and is delivered when the mask is lifted. The consequence the manual page states (`signal(7)`, fetched): **standard signals do not queue**, so several instances sent while one is pending collapse into one; **real-time signals do queue**, carry a value (`sigqueue`) and are delivered in the order sent. The program blocks the signal, sends 100 of each kind to itself, then unblocks:

```c
static void sect_coalesce(void)
{
    sigset_t b, old;
    struct sigaction sa = { .sa_handler = h_count };
    sigemptyset(&b); sigaddset(&b, SIGUSR1);
    sigaction(SIGUSR1, &sa, NULL);
    sigprocmask(SIG_BLOCK, &b, &old);
    hits = 0;
    for (int i = 0; i < 100; i++) kill(getpid(), SIGUSR1);
    sigprocmask(SIG_SETMASK, &old, NULL);
    printf("standard signal SIGUSR1: sent 100 times while blocked, handler ran %d time(s)\n", (int)hits);

    struct sigaction sr = { .sa_sigaction = h_rt, .sa_flags = SA_SIGINFO };
    sigaction(SIGRTMIN, &sr, NULL);
    sigemptyset(&b); sigaddset(&b, SIGRTMIN);
    sigprocmask(SIG_BLOCK, &b, &old);
    rt_n = 0;
    int sent = 0;
    for (int i = 0; i < 100; i++) {
        union sigval v = { .sival_int = i };
        if (sigqueue(getpid(), SIGRTMIN, v) == 0) sent++;
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
    int inorder = 1;
    for (int i = 0; i < rt_n; i++) if (rt_val[i] != i) inorder = 0;
    printf("real-time SIGRTMIN with sigqueue: %d accepted, handler ran %d times, values %d..%d, in order: %s\n", sent, rt_n, rt_n ? rt_val[0] : -1, rt_n ? rt_val[rt_n - 1] : -1, inorder ? "yes" : "no");
}
```

```text
standard signal SIGUSR1: sent 100 times while blocked, handler ran 1 time(s)
real-time SIGRTMIN with sigqueue: 100 accepted, handler ran 100 times, values 0..99, in order: yes
```

- **One delivery for 100 `SIGUSR1`.** A standard signal is a **flag**, not a counter. Two children that exit within one scheduling quantum may produce one `SIGCHLD`; a program must therefore loop on `waitpid(-1, ..., WNOHANG)` until it returns 0, not call `wait` once per signal.
- **100 deliveries, values 0 to 99 in order, for the real-time signal.** This is the only signal that can act as a counted event, and its payload (`si_value`) can carry one integer. The queue is limited (`RLIMIT_SIGPENDING`); I did not test the limit.

**Order among different pending signals.** The page says that for several pending standard signals the order is unspecified, real-time signals are delivered from the lowest number up, and **Linux delivers standard signals before real-time ones**. The program sends five signals while blocked (12, 35, 10, 34, 1 in that order) and records the order in which the handlers **start**, once with an empty `sa_mask` and once with a full one:

```text
handler mask empty; sent while blocked, in this order: 12 35 10 34 1
  handlers started in this order: 35 34 12 10 1   (SIGRTMIN is 34)
handler mask blocks all; sent while blocked, in this order: 12 35 10 34 1
  handlers started in this order: 1 10 12 34 35   (SIGRTMIN is 34)
```

The second pair is the delivery order: **1, 10, 12, 34, 35**, lowest number first, standard before real-time, regardless of the order of sending. The first pair is the **reverse**, and the reason is nesting: with an empty `sa_mask` the kernel sets up the frame for signal 1, finds more pending signals before the handler's first instruction runs, and stacks the frame for 10 on top, and so on, so the last one delivered runs first. This is my reading of the measurement and not a quotation from the manual; the lesson is solid either way: **do not rely on the order of signal handlers**, and set `sa_mask` if a handler must not be interrupted by the others.

### 3.3 `sigaction`, `SA_RESTART` and `EINTR`

`signal()` has historic differences between systems; use **`sigaction`**. Its fields: `sa_handler` or `sa_sigaction` (with `SA_SIGINFO`, to receive the `siginfo_t`), `sa_mask` (signals blocked **while** the handler runs; the signal being handled is blocked too unless `SA_NODEFER`), and flags: **`SA_RESTART`**, `SA_SIGINFO`, `SA_NOCLDWAIT`, `SA_NODEFER`, `SA_RESETHAND`.

What happens to a **blocked system call** when a handler runs? Without `SA_RESTART`, `read` returns **-1 with `EINTR`** and the program must retry; with it, the kernel restarts the call after the handler. The manual page lists what restarts (`read` and `write` on slow devices, `wait`, `flock`, `F_SETLKW`, `sem_wait`, POSIX `mq_send` and `mq_receive`, `accept` and other socket calls without a timeout) and, importantly, what **never** restarts whatever the flag: `pause`, `sigsuspend`, `sigwaitinfo`, `poll`, `select`, `epoll_wait`, **System V `msgsnd`, `msgrcv` and `semop`**, `nanosleep`. The program runs a blocking `read` on an empty pipe, with a timer signal after 100 ms and data arriving after 300 ms:

```c
static void sect_restart(int restart)
{
    int p[2];
    struct sigaction sa = { .sa_handler = h_count, .sa_flags = restart ? SA_RESTART : 0 };
    struct itimerval it = { .it_value = { 0, 100000 } };
    char c;
    IGN(pipe(p));
    sigaction(SIGALRM, &sa, NULL);
    hits = 0;
    if (fork() == 0) { usleep(300000); IGN(write(p[1], "x", 1)); _exit(0); }
    double t0 = now();
    setitimer(ITIMER_REAL, &it, NULL);
    ssize_t r = read(p[0], &c, 1);
    int e = errno;
    printf("SA_RESTART %-3s: read returned %2zd (%s) after %3.0f ms, handler ran %d time(s)\n", restart ? "on" : "off", r, r < 0 ? strerror(e) : "data", (now() - t0) * 1000, (int)hits);
    wait(NULL);
    close(p[0]); close(p[1]);
}
```

```text
SA_RESTART off: read returned -1 (Interrupted system call) after 100 ms, handler ran 1 time(s)
SA_RESTART on : read returned  1 (data) after 300 ms, handler ran 1 time(s)
```

Without the flag, `read` failed with `EINTR` at **100 ms**; with it, the call resumed and returned the data at **300 ms**, and in both cases the handler ran once. **Every blocking call in a program that installs handlers must either use `SA_RESTART` or loop on `EINTR`**, and the second option is the only one that works for `poll`, `epoll_wait` and the System V calls.

### 3.4 What a handler may do

A handler runs **between any two instructions** of the interrupted code, on top of its stack. If the main code was inside `malloc`, holding its internal lock, a handler that calls `malloc` deadlocks on that lock. The rule (`signal-safety(7)`, fetched) is: **only call async-signal-safe functions**, those that are either reentrant or atomic with respect to signals. The page's list includes `write`, `_exit`, `kill` and `sigaction`; it names the stdio functions (such as `printf`) as **not** safe, and `malloc` is not on the required list. **`errno`** is shared with the interrupted code: the page says touching it is safe only if the handler **saves it on entry and restores it before returning**. So a handler should do almost nothing:

1. set a `volatile sig_atomic_t` flag, or write one byte to a pipe, and return;
2. let the main loop notice and do the real work in normal context.

The two safe patterns for the main loop:

- **The self-pipe trick**: the handler writes a byte to a non-blocking pipe whose read end is in the main loop's `poll` set. Signals become ordinary readable events.
- **`signalfd`**: block the signals with `sigprocmask`, then read `struct signalfd_siginfo` records (128 bytes each) from a descriptor that works with `poll` and `epoll`. The page notes that the signals must be blocked first, that a read consumes them, and that the descriptor is inherited by `fork` (the child reads its own pending signals through it).

The program measures both, sending a signal to itself:

```text
kill(getpid(), SIGUSR1) to a handler and back: median 1268 ns
kill + read from a signalfd:                   median 555 ns
kill(getpid(), 0), no signal sent (baseline):  median 247 ns
signalfd: 3 SIGUSR1 sent while blocked, 1 record(s) read
```

`kill` to a handler and back costs about **1,268 ns**, a `kill` plus a `signalfd` read about **555 ns**, against about 247 ns for a `kill` that sends nothing (`sig 0` only checks that the target exists). So the **`signalfd` path is cheaper here than the handler**, because delivering to a handler builds a signal frame on the stack and returns through `sigreturn`, two extra kernel crossings. Three `SIGUSR1` sent while blocked produced **one** record: `signalfd` reads the same pending set, so coalescing is the same.

### 3.5 What `fork`, `exec` and threads do with signals

From the page (fetched) and the program:

- **`fork`**: the child inherits the dispositions and the signal mask; its pending set starts empty.
- **`exec`**: signals with handlers are **reset to default** (the handler code is gone); ignored signals **stay ignored**; the mask and the pending set are **preserved**. The program sets a handler for `SIGUSR2`, ignores `SIGUSR1`, blocks `SIGINT`, and `exec`s itself:

```text
before exec: SIGUSR2 has a handler, SIGUSR1 is ignored, SIGINT is blocked
after exec:  SIGUSR2 is default, SIGUSR1 is ignored, SIGINT is blocked
```

The consequence: a parent that ignores `SIGPIPE` or `SIGINT` before `exec` hands that to the new program silently, and a mask set before `exec` can leave `SIGINT` blocked in a program that expects it (a classic cause of "Ctrl-C does not work").
- **Threads**: the disposition is shared by all threads, the mask is per thread, and a **process-directed signal goes to any one thread that does not block it**, chosen arbitrarily; synchronous ones (`SIGSEGV`) and `pthread_kill` go to a specific thread. The usual design is to **block all signals in every thread** and dedicate one thread to `sigwait` or a `signalfd`.

### 3.6 `SIGCHLD` and zombies

A child that exits stays a **zombie** until the parent calls `wait` (OS 02). `SIGCHLD` tells the parent a child changed state; its `siginfo_t` carries the child's pid, the status and a reason (`si_code`). `SA_NOCLDWAIT`, or setting `SIGCHLD` to ignore, tells the kernel to **reap children automatically**, so no zombies appear but `wait` later fails with `ECHILD` and the exit status is lost. The program:

```text
waitid: si_pid matches child yes, si_code CLD_EXITED, si_status 7
child killed by a signal: si_code CLD_KILLED, si_status 9 (the signal number)
SA_NOCLDWAIT: waitpid returned -1, errno ECHILD (the child was reaped by the kernel)
```

`waitid` delivered the pid, `CLD_EXITED` with status 7 for a normal exit, and `CLD_KILLED` with status 9 (the signal number) for a child killed by a signal. With `SA_NOCLDWAIT` the parent's `waitpid` returned -1 with `ECHILD`: the child had been reaped by the kernel.

### 3.7 Signals as IPC

Technically a pair of processes can ping-pong with `kill` and `sigwaitinfo`, and the benchmark does:

```text
  signal (SIGUSR1/2)       round trip    26527 ns  (min  22383, max  30117)  one way about  13263 ns
```

About 26.5 µs per round trip across CPUs, the same as a pipe or a semaphore, since the wake-up dominates (section 10). But signals carry **no data** (an integer at most), standard ones **coalesce**, handlers are fragile and the order is unspecified. **Use signals for what they are for**: `SIGTERM` to ask for a shutdown, `SIGHUP` to reload, `SIGCHLD` for children, `SIGALRM` or better a timer descriptor, and **never** as a data channel.

---

## 4. Process groups, sessions and daemons

### 4.1 The hierarchy

Every process has a process id (pid), a **process group id (pgid)** and a **session id (sid)** (`credentials(7)`, fetched). A **process group** is a set of processes, usually one shell job (`a | b | c`); the process whose pid equals the pgid is the **group leader**. A **session** is a set of process groups, "a strict two-level hierarchy". A session has at most one **controlling terminal**, and at most one process group in it is the **foreground job**; the others are background jobs. Signals typed at the terminal go to the foreground group: `Ctrl-C` sends `SIGINT` to every process in it. A background process that reads the terminal gets `SIGTTIN`, which stops it; with the terminal's `TOSTOP` flag set, a background write gets `SIGTTOU` (both from the page). `kill(-pgid, sig)` sends a signal to every member of a group (the negative-pid form is from memory; the page mentions `kill` and `killpg`).

The program prints the ids at each step:

```text
inherited: fork keeps the process group and session
  this program               pid    532  ppid    518  pgid    532  sid    518
  child after fork           pid    533  ppid    532  pgid    532  sid    518

setpgid and setsid
  child after setpgid(0,0)   pid    534  ppid    532  pgid    534  sid    518
  setsid() as a group leader: -1, errno EPERM
  child after setsid()       pid    535  ppid    532  pgid    535  sid    535
  setsid returned 535 (its own pid: yes), pgid == sid == pid: yes
  open("/dev/tty") in the new session: ENXIO, no controlling terminal
```

- A child made by `fork` has the **same pgid and sid** as its parent.
- After **`setpgid(0, 0)`** the child is a group leader (its pgid equals its pid), still in the same session.
- **`setsid()`** makes a **new session and a new group**, both with the caller's pid as the id, and the caller has **no controlling terminal** (`open("/dev/tty")` failed with `ENXIO`). It fails with **`EPERM`** if the caller is already a group leader (the printed `-1, EPERM` case), which is why the idiom forks first: a freshly forked child is never a group leader.

### 4.2 Signalling a group

The program makes a leader with three children in one group and sends `SIGTERM` to the negative pgid:

```text
signalling a group: kill(-pgid, SIGTERM)
  processes in group 536 before: 4
  after kill(-536, SIGTERM): the leader was reaped, 0 live processes remain in the group
```

One call stopped all four processes. This is how a shell kills a whole job, and why a service manager puts each service in its own group (or, in modern Linux, its own control group, from memory), so that stopping the service stops its helpers too.

### 4.3 Daemons

A **daemon** is a background process with no controlling terminal that outlives the shell that started it. The classic recipe, which the program follows:

1. **`fork`** and let the parent exit, so the shell sees the command finish and the child is not a group leader;
2. **`setsid()`** in the child: a new session, no controlling terminal;
3. **fork again** and let the first child exit, so the daemon is **not a session leader** and can never acquire a controlling terminal by opening one (the `setsid(2)` page describes the first fork, and the second fork's purpose is from memory);
4. (in a full daemon) `chdir("/")`, reset the `umask`, close the standard descriptors and reopen them on `/dev/null`, and redirect logs.

```text
daemonize: fork, setsid, fork again, parent exits
  daemon: pid 541, ppid 1, pgid 540, sid 540 (sid != pid, so it is not a session leader)
  this program's pid 532: the daemon's parent is not this program (it was reparented to an init or subreaper process)
```

The daemon's `ppid` is **1**: after its parent exited it was **reparented** to the init process (or to a "subreaper"). Its sid differs from its pid, so it is not a session leader.

**Today** most daemons do not double-fork; they run in the foreground under a service manager such as systemd, which supplies the isolation and the logging (from memory). The double fork is still asked in interviews, and `nohup cmd &` is the shell's cheap version of it (from memory).

---
## 5. Shared memory

### 5.1 Five ways to get it

Shared memory is the one mechanism where the kernel is not in the data path: after the setup, a store by one process is a load by the other with no system call. The ways to set it up differ in **what names the region** and **how long it lives**. The program makes each kind, has the parent write 1, the child write 2, and reports what the parent reads:

```text
  mmap MAP_PRIVATE|MAP_ANONYMOUS:   parent sees 1 after the child wrote 2 (copy on write: not shared)
  mmap MAP_SHARED|MAP_ANONYMOUS:    parent sees 2 (shared with children created after the mmap; no name)
  memfd_create + MAP_SHARED:        parent sees 2 (an fd, no name in any file system; passed by fork or over a socket)
                                    /proc/self/fd/3 -> /memfd:h5 (deleted)
  shm_open + MAP_SHARED:            parent sees 2 (a file named /dev/shm/h5_demo; any process can open it by name)
  after close(fd): stat("/dev/shm/h5_demo") succeeds, size 4096 (it persists until shm_unlink)
  after shm_unlink: stat fails
  System V shmget + shmat:          parent sees 2 (attachments are inherited by fork)
  shmctl IPC_STAT: size 4096, attached 1, creator pid 543
  after shmdt: attached 0, but the segment still exists until IPC_RMID (removed; shmctl now fails)
  a segment created by a process that then exited (no IPC_RMID): shmctl IPC_STAT still finds it (a leak until removed)
```

| Kind | Setup | Name or handle | Joined by | Lives until |
| --- | --- | --- | --- | --- |
| private anonymous (`MAP_PRIVATE`) | `mmap` | none | nobody: copy on write after `fork` | the process exits |
| shared anonymous | `mmap(MAP_SHARED \| MAP_ANONYMOUS)` | none | **children created after the `mmap`** | the last mapping goes |
| `memfd_create` | `memfd_create`, `ftruncate`, `mmap` | a descriptor (the name shown in `/proc` is only a label) | whoever receives the descriptor (`fork`, or passed over a Unix socket) | the last descriptor and mapping go |
| POSIX `shm_open` | `shm_open`, `ftruncate`, `mmap` | `/name` in `/dev/shm` | **any process** that can open the name | `shm_unlink` (or shutdown) |
| System V `shmget` | `shmget`, `shmat` | an integer id from a key | any process that knows the id or key | `shmctl(IPC_RMID)` (from memory: until removed) |

What the measurement shows, line by line:

- **Private**: the parent still reads 1. A `MAP_PRIVATE` page is copied when written (OS 11), so it is **not** shared; this is the mapping type `fork` leaves for ordinary heap and stack.
- **Shared anonymous, `memfd`, `shm_open`, System V**: the parent reads 2. All four work **through `fork`**, because the child inherits the mapping.
- **`memfd`** has no name in any file system: `/proc/self/fd/3` shows `/memfd:h5 (deleted)`, a label with the word "deleted" because there is no directory entry. It is the clean choice when you hand the region to a peer over a socket (OS 18, descriptor passing) and want nothing left on the system if both die.
- **`shm_open`** creates a file in `/dev/shm`, a tmpfs, and the object **persists after `close`**: `stat` still found it, with size 4,096, until `shm_unlink` removed the name. A program that crashes before `shm_unlink` **leaks the object until reboot** (`shm_overview(7)`, fetched: "will exist until the system is shut down" unless unlinked). The same is true of POSIX queues and semaphores and of every System V object: a segment created by a child that then exited (the last line) was **still there**. Clean up in a `finally`-like path, and name objects uniquely so a leaked one does not break the next start (`O_EXCL`).
- **System V** `shmctl(IPC_STAT)` reports the attach count (`shm_nattch`) and creator: 1 while attached, 0 after `shmdt`, and the segment **remains** until `IPC_RMID`. `ipcs -m` lists them and `ipcrm` removes them.

The five steps for POSIX shared memory (`shm_overview(7)`): `shm_open`, then `ftruncate` (**a new object has size 0**), then `mmap`, `close` of the descriptor (the mapping stays), and `shm_unlink` when done. Forgetting `ftruncate` is the usual first bug.

### 5.2 The mapping and `read` agree; going past the end is `SIGBUS`

A `MAP_SHARED` file mapping and ordinary `read` and `write` on the same file are two views of **one page cache** (OS 16, section 1; OS 11, section 1.5). The program writes through the mapping and reads through `pread` on another descriptor, then the reverse:

```text
  store through MAP_SHARED, then pread on another fd, no msync: "written through the mapping"
  pwrite through the file, then read the mapping:                "WRITTEN through the mapping"
  (the mapping and read/write share the page cache, so they agree at once; msync only matters for reaching the disk)
```

Both see the change at once, without `msync`, because there is only one copy of the page. `msync` is for reaching the **disk**, not for visibility between processes.

The other trap, in the same program: mapping 8,192 bytes of a 4,096-byte object and storing into the second page:

```text
  mapping 8192 bytes of a 4096-byte shm object: store into byte 5000 -> child killed by SIGBUS
```

The pages beyond the end of the object do not exist, so the access raises **`SIGBUS`** (not `SIGSEGV`, which is for an address with no mapping at all). A reader process that maps a file or shared object that another process **truncates** gets the same signal on its next access, so code that reads shared memory written by a less-trusted peer must be ready for it, or must never let the peer shrink the object (a sealed `memfd`, from memory).

### 5.3 Why shared memory needs synchronisation: the numbers

Two processes increment one counter in shared memory 2,000,000 times each, on different CPUs, starting together:

```text
  plain increment (no synchronisation)   2 processes x 2000000: final  2167745 (expected 4000000)     2.7 ms    0.7 ns per increment
  atomic fetch_add                       2 processes x 2000000: final  4000000 (expected 4000000)    97.9 ms   24.5 ns per increment
  process-shared mutex                   2 processes x 2000000: final  4000000 (expected 4000000)   220.1 ms   55.0 ns per increment
  process-shared semaphore as a lock     2 processes x 2000000: final  4000000 (expected 4000000)   261.6 ms   65.4 ns per increment
```

- **Without synchronisation** the final value was 2,167,745, about **54 percent of the 4,000,000** that were done in this run (the exact loss varies from run to run). A `volatile` increment is a load, an add and a store; two processes interleave and **one overwrites the other's update**. This is the lost-update race of OS 06, between processes.
- **An atomic add** (`lock xadd`) was correct and cost about 24.5 ns per increment under contention (the cache line moves between the two CPUs).
- **A process-shared mutex** was correct and cost about 55.0 ns, **a process-shared semaphore** about 65.4 ns. Under contention the cost is dominated by the cache line moving and the occasional futex sleep; (from memory) the uncontended fast path of both is a user-space atomic with no system call.

(The plain row's time of a few milliseconds is not a cost of correct code: it did not do the work correctly.)

### 5.4 Process-shared mutexes, condition variables and robust mutexes

The synchronisation primitives of OS 06 and 07 work between processes if two conditions hold: the object lives **in shared memory**, and it was initialised with the **process-shared** attribute (`PTHREAD_PROCESS_SHARED` through `pthread_mutexattr_setpshared`, `pthread_condattr_setpshared`, or `sem_init(&s, 1, n)`). Forget the attribute and the object works within one process only. The condition-variable ping-pong of two processes took about 27,155 ns per round trip, the same as a pipe or a semaphore: the wake-up is the cost.

A new problem appears: **a process can die while holding the lock**. Threads die with their process, so a mutex inside one process never has this problem; a process-shared one does, and the default behaviour (`PTHREAD_MUTEX_STALLED`, `pthread_mutexattr_setrobust(3)`, fetched) is that the mutex "remains locked afterwards" and later `lock` calls block **forever**. The fix is a **robust mutex**: the next `lock` succeeds and returns **`EOWNERDEAD`**, telling you the previous owner died and the protected data may be inconsistent. You repair the data, call `pthread_mutex_consistent`, and unlock. If you unlock **without** calling `consistent`, the mutex becomes unusable and later locks fail with `ENOTRECOVERABLE`. Linux also gives `EOWNERDEAD` to a waiter if the owner calls `execve` while holding the lock (page). The program runs a child that locks the mutex and exits:

```c
static void sect_robust(void)
{
    for (int robust = 0; robust < 2; robust++) {
        pthread_mutex_t *m = mmap(NULL, sizeof *m, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        pthread_mutexattr_t a;
        pthread_mutexattr_init(&a);
        pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
        if (robust) pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(m, &a);
        if (fork() == 0) { pthread_mutex_lock(m); _exit(0); }   /* the owner dies holding the lock */
        wait(NULL);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 300 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        double t0 = now();
        int r = pthread_mutex_timedlock(m, &ts);
        printf("  %-11s mutex, owner exited holding it: timedlock returned %s after %.0f ms", robust ? "robust" : "non-robust", r == 0 ? "0" : r == EOWNERDEAD ? "EOWNERDEAD" : r == ETIMEDOUT ? "ETIMEDOUT" : strerror(r), (now() - t0) * 1000);
        if (r == EOWNERDEAD) {
            pthread_mutex_consistent(m);
            pthread_mutex_unlock(m);
            int r2 = pthread_mutex_lock(m);
            printf("; after pthread_mutex_consistent and unlock, the next lock returned %d", r2);
            pthread_mutex_unlock(m);
        }
        printf("\n");
    }
}
```

```text
  non-robust  mutex, owner exited holding it: timedlock returned ETIMEDOUT after 300 ms
  robust      mutex, owner exited holding it: timedlock returned EOWNERDEAD after 0 ms; after pthread_mutex_consistent and unlock, the next lock returned 0
```

The non-robust mutex was still locked: `pthread_mutex_timedlock` gave up after **300 ms** with `ETIMEDOUT` (a plain `lock` would have blocked forever). The robust one returned **`EOWNERDEAD` at once**, and after `pthread_mutex_consistent` and an unlock the next `lock` returned 0. **Every mutex that lives in memory shared between processes should be robust**, and the recovery code (rebuild or roll back the structure) must be written and tested.

### 5.5 Rules for designing shared-memory structures

1. **No pointers inside the region.** Each process may map the region at a **different address**, so a pointer valid in one is garbage in the other. Store **offsets from the base** (or indices), and add the base in each process.
2. **Put each independently written variable on its own cache line** (64 bytes on x86). The benchmark's structure aligns its flags and counters with `_Alignas(64)`; two flags on one line would make the two processes fight over the line even though they never touch the same variable (false sharing, OS 06). I did not measure the effect here.
3. **Use acquire and release ordering** for publishing data: write the payload, then store the "ready" flag with **release**; the reader loads the flag with **acquire** and then reads the payload (OS 08). A plain flag may be seen before the payload.
4. **Treat the contents as untrusted** if the peer is less trusted than you: copy a value out, then validate the copy, never validate in place and then use it (a peer can change it between the check and the use).
5. **Version the layout** (a magic number and a version at the start), because the two processes may be different builds.

The standard design is a **single-producer, single-consumer ring buffer**: the producer owns `head` and writes the slots, the consumer owns `tail` and reads them, and neither needs a lock because each index is written by one side only. The benchmark's ring, with spinning waits:

```c
static int ring_send(int d, const void *b, size_t n)
{
    if (n == 1 || d == 1) return spin_send(d, b, n);               /* the ack and 1-byte pings */
    const char *p = b;
    unsigned long h = atomic_load_explicit(&shm->head, memory_order_relaxed);
    while (n) {
        unsigned long t = atomic_load_explicit(&shm->tail, memory_order_acquire);
        size_t room = RING - (h - t);
        if (room == 0) { __builtin_ia32_pause(); continue; }
        size_t c = n < room ? n : room, off = h % RING;
        if (c > RING - off) c = RING - off;
        memcpy(shm->ring + off, p, c);
        h += c; p += c; n -= c;
        atomic_store_explicit(&shm->head, h, memory_order_release);
    }
    return 0;
}

static int ring_recv(int d, void *b, size_t n)
{
    if (n == 1 || d == 1) return spin_recv(d, b, n);
    char *p = b;
    unsigned long t = atomic_load_explicit(&shm->tail, memory_order_relaxed);
    while (n) {
        unsigned long h = atomic_load_explicit(&shm->head, memory_order_acquire);
        size_t avail = h - t;
        if (avail == 0) { __builtin_ia32_pause(); continue; }
        size_t c = n < avail ? n : avail, off = t % RING;
        if (c > RING - off) c = RING - off;
        memcpy(p, shm->ring + off, c);
        t += c; p += c; n -= c;
        atomic_store_explicit(&shm->tail, t, memory_order_release);
    }
    return 0;
}
```

(The functions take a `dir` argument for the benchmark's two directions; the ring itself is the `n > 1` path in the middle.) The ring moves bytes with a `memcpy` into and out of shared pages: **one copy in, one copy out, no system call**. Its numbers are in section 10: about 8 GB/s for 4 KiB messages. The price is that the **waiting side spins**, using a CPU while it waits. A production design waits with a `futex` or `eventfd` when the ring is empty and spins only briefly first (OS 18).

---

## 6. Semaphores

OS 07 defined the semaphore: an integer with an atomic **wait** (decrement, blocking at 0) and **post** (increment, waking a waiter). Two families exist between processes (`sem_overview(7)`, fetched, which calls the POSIX interface "simpler, and better designed" than System V).

**POSIX semaphores** come in two forms:

- **Named** (`sem_open("/name", ...)`): any process opens the same name; on Linux it appears as a file `sem.name` in `/dev/shm`; it persists until `sem_unlink` or shutdown.
- **Unnamed** (`sem_init(&s, pshared, value)`): lives in memory you provide; with `pshared = 1` it must be in **shared memory** and can then be used by several processes. No name, no leak.

**System V semaphores** are **sets** (an array of counters made by `semget`), changed by `semop` with an **array of operations applied atomically**: either all succeed or none does. The program shows both families:

```text
System V semaphores
  child took the semaphore (value 1 to 0) and exited without SEM_UNDO: value is now 0
  child took the semaphore (value 1 to 0) and exited with SEM_UNDO: value is now 1
  semop taking sem0 (value 1) and sem1 (value 0) in one call: -1, errno EAGAIN; sem0 is still 1 (all or nothing)

POSIX named semaphore: initial value 0
  it appears as /dev/shm/sem.h7_sem: yes
  sem_timedwait on value 0 with a 100 ms limit: -1, errno ETIMEDOUT after 100 ms
  a child that opened it by name and posted woke the parent: yes
  value after the wait: 0
  after sem_unlink: file gone
```

```c
static void sect_sysv_sem(void)
{
    int id = semget(IPC_PRIVATE, 2, IPC_CREAT | 0600);
    for (int undo = 0; undo < 2; undo++) {
        semctl(id, 0, SETVAL, 1);
        if (fork() == 0) {
            struct sembuf o = { .sem_num = 0, .sem_op = -1, .sem_flg = (short)(undo ? SEM_UNDO : 0) };
            semop(id, &o, 1);                         /* take the semaphore and die holding it */
            _exit(0);
        }
        wait(NULL);
        printf("  child took the semaphore (value 1 to 0) and exited %s SEM_UNDO: value is now %d\n", undo ? "with" : "without", semval(id, 0));
    }
    /* an atomic operation on two semaphores: all or nothing */
    semctl(id, 0, SETVAL, 1);
    semctl(id, 1, SETVAL, 0);
    struct sembuf both[2] = { { 0, -1, IPC_NOWAIT }, { 1, -1, IPC_NOWAIT } };
    errno = 0;
    int r = semop(id, both, 2);
    printf("  semop taking sem0 (value 1) and sem1 (value 0) in one call: %d, errno %s; sem0 is still %d (all or nothing)\n", r, errno == EAGAIN ? "EAGAIN" : strerror(errno), semval(id, 0));
    semctl(id, IPC_RMID, 0);
}
```

- **`SEM_UNDO`.** A child took the semaphore (1 to 0) and **exited**. Without `SEM_UNDO` the value stayed **0**: the resource is locked forever, the same death-while-holding problem as section 5.4. With `SEM_UNDO` the kernel recorded the adjustment and **restored it to 1** when the process exited. This is the System V answer to a crashed holder, and it is the main reason a System V semaphore is still used as a cross-process lock.
- **All or nothing.** One `semop` that takes semaphore 0 (value 1) and semaphore 1 (value 0) failed with `EAGAIN` and **left semaphore 0 at 1**: neither was taken. This is how a process takes two resources without deadlock (OS 09). POSIX semaphores have no multi-semaphore operation.
- **`sem_timedwait`** gave up after exactly 100 ms with `ETIMEDOUT` (POSIX); System V has `semtimedop`.
- The System V limits on this machine (`ipcs -l`): at most 32,767 as a semaphore value, 500 operations per `semop`, 32,000 semaphores per set.
- **Restarts**: `sem_wait` is restarted after a signal handler if `SA_RESTART` is set; `semop` is **never** restarted (section 3.3). A System V waiter must loop on `EINTR`.

**Cost.** In the benchmark a POSIX semaphore round trip (post, wait, in two directions) took 20,471 ns across CPUs and 1,684 ns on one CPU; System V took 27,387 and 1,923. The System V call always enters the kernel; the POSIX one is a **futex**-based user-space counter that enters the kernel only to sleep or wake (from memory of the glibc implementation), so it is cheaper when there is **no waiter**, which the ping-pong, where every call sleeps, does not show.

---

## 7. Message queues

### 7.1 POSIX message queues

A POSIX queue (`mq_open("/name", ...)`, `mq_overview(7)` fetched) holds **discrete messages** with a **priority**: **higher priorities are received first, equal priorities in first-in-first-out order**. Its limits are set at creation: `mq_maxmsg` (default and limit 10 here, `/proc/sys/fs/mqueue/msg_max`), `mq_msgsize` (default 8,192 bytes, `msgsize_max`), and 256 queues system-wide (`queues_max`, from the page); the space is accounted to the user through `RLIMIT_MSGQUEUE`. On Linux a queue descriptor **is a file descriptor**, so `poll`, `select` and `epoll` work on it, which the page notes is not portable. Queues persist until `mq_unlink` or shutdown, and are visible as files if the `mqueue` file system is mounted on `/dev/mqueue`. The program:

```text
POSIX queue created: maxmsg 10, msgsize 64, flags non-blocking
  a message descriptor is a file descriptor: fd 3 (poll and epoll work on it)
  poll on the empty queue: 0 ready
  sent (text, priority): (low-a,1) (mid-a,5) (high,9) (mid-b,5) (low-b,1)
  poll with messages queued: 1 ready
  received: (high,9) (mid-a,5) (mid-b,5) (low-a,1) (low-b,1)
  non-blocking sends accepted before the queue was full: 10, then errno EAGAIN
  sending 128 bytes into a queue with msgsize 64: -1, errno EMSGSIZE
  receiving into an 8-byte buffer: -1, errno EMSGSIZE (the buffer must be at least msgsize)
  after mq_close the queue still exists until mq_unlink: mq_open by name succeeds
```

- **Priority order**: sent low, mid, high, mid, low; received **high (9), mid-a, mid-b (5, in sending order), low-a, low-b (1)**.
- **Bounded**: the 11th non-blocking send failed with `EAGAIN`; a blocking sender would sleep. The queue limits memory and gives **backpressure**, unlike a signal.
- **Size rules**: a send larger than the queue's `msgsize` fails with `EMSGSIZE`, and so does a receive into a buffer **smaller than `msgsize`** (the receive buffer must hold the largest possible message, not the actual one).
- **`poll` works**: 0 ready on the empty queue, 1 ready after messages arrived.

### 7.2 System V message queues

A System V queue (`msgget`, `msgsnd`, `msgrcv`) tags each message with a **type** (a positive long) and lets the receiver **select by type**: the type is both a label and a way to give several readers their own mailbox on one queue (the benchmark uses types 1 and 2 as the two directions). The program sent types 3, 1, 2, 3:

```text
System V queue: sent types 3 1 2 3
  msgrcv(type 2) returned msg2 (type 2)
  msgrcv(type -2), the lowest type <= 2, returned msg1 (type 1)
  msgrcv(type 0), the oldest message, returned msg0 (type 3)
  msgrcv(type 0) again returned msg3 (type 3)
  queue size limit msg_qbytes: 16384 bytes
  32-byte messages accepted before EAGAIN: 512, which is 16384 bytes of payload, the whole msg_qbytes limit
```

- `msgrcv` with **type 2** took the message of type 2, skipping the older ones.
- **Type -2** takes the message with the **lowest type that is at most 2** (here type 1): a priority queue by type.
- **Type 0** takes the oldest message of any type.
- The queue's limit `msg_qbytes` is 16,384 bytes here; 32-byte messages stopped at **512**, exactly 16,384 bytes, so on this kernel the limit counted payload bytes. A System V message is limited by `msgmax`, 8,192 bytes here (`ipcs -l`).
- A System V queue is **not a file descriptor** (from memory): you cannot `poll` it, so it cannot sit in an event loop beside sockets, and `msgrcv` is never restarted after a signal (section 3.3). These are the reasons to prefer POSIX queues or sockets for new code.

### 7.3 Costs and when to use a queue

Round trip of one byte: a POSIX queue took 26,542 ns across CPUs and 1,971 ns on one CPU, System V 28,021 and 2,179, in line with a pipe. Throughput for 4 KiB messages, one writer and one reader on different CPUs: POSIX 1808.2 MB/s, System V 1487.6 MB/s, a pipe 2376.0 MB/s and a `SOCK_SEQPACKET` socket 2580.9 MB/s. Neither queue could carry a 64 KiB message with the default limits.

A message queue is justified by **priorities**, **message boundaries with backpressure**, or **persistence across a peer restart** (the queue and its messages survive a crashed consumer). Otherwise a `SOCK_SEQPACKET` Unix socket gives boundaries, ordering and flow control, works with `epoll`, supports descriptor passing, and **disappears with its processes** (section 9).

---
## 8. File locks

### 8.1 Why lock a file

Locks on files coordinate **cooperating processes** that share a file or a resource represented by one: a database file, a pid file ("only one instance of this daemon"), a configuration directory. They are **advisory**: the kernel does not stop a process that ignores them (the program's last test: a process that never asked for the lock wrote to the locked file and the write succeeded). Linux once had **mandatory locking**, which enforced locks on every `read` and `write`, and it is "no longer supported at all in Linux 5.15 and above" (the `fcntl` manual page, fetched). A great advantage of file locks over a "lock file" created with `O_EXCL`: **they are released automatically when the process dies**, so a crashed holder cannot leave the lock held (for `fcntl` locks, the page says "automatically released when the process terminates").

Linux has three kinds. Their rules differ in two ways that matter: **what the lock belongs to** and **what releases it**.

### 8.2 `flock`: tied to the open file description

`flock(fd, LOCK_EX | LOCK_SH | LOCK_UN, with LOCK_NB for non-blocking)` locks the **whole file** (`flock(2)`, fetched). The lock belongs to the **open file description** (OS 15, section 1.2), not to the process and not to the descriptor number. Therefore:

- A **separate `open`** of the same file, even in the same process, is an independent description and **conflicts** with the lock.
- A descriptor made by **`dup` or inherited through `fork`** shares the description and shares the lock: either side can unlock or convert it.
- The lock is released by an explicit `LOCK_UN` or when **every descriptor of the description is closed**. It survives `exec`.
- Converting a shared lock to exclusive or back is **not guaranteed atomic**: the old lock is dropped first, so a waiting process can slip in.
- Over NFS, since Linux 2.6.12, `flock` is emulated with whole-file byte-range locks.

The program checks each rule with a helper process that tries a non-blocking `flock`:

```text
flock (whole file, tied to the open file description)
  parent holds LOCK_EX; a separate open() in another process:   refused (would block)
  the child inherits the descriptor and calls flock on it:      got the lock
  (an inherited descriptor shares the description, so the lock is already 'its own')
  after close(fd) but dup'd descriptor still open:              refused (would block)
  after the last descriptor of the description closed:          got the lock
```

The parent's lock refused a separate `open`, the **inherited** descriptor could take the lock itself (it is the same description, so it is already the holder), the lock survived `close(fd)` while a `dup` was open, and was released only after the last one closed.

### 8.3 `fcntl` record locks: tied to the process, and the close trap

`fcntl(fd, F_SETLK, &struct flock)` locks **a byte range** (or the whole file with length 0), shared (`F_RDLCK`) or exclusive (`F_WRLCK`); `F_SETLKW` waits and `F_GETLK` asks who blocks you. These POSIX record locks belong to the **process**, with three consequences the manual page states (`F_SETLK(2const)`, fetched) and the program shows:

```c
static int fcntl_range(int fd, short type, off_t start, off_t len, int cmd)
{
    struct flock fl = { .l_type = type, .l_whence = SEEK_SET, .l_start = start, .l_len = len };
    return fcntl(fd, cmd, &fl);
}
```

```text
fcntl F_SETLK (POSIX record locks, tied to the process)
  parent holds a write lock on the whole file; another process: refused (would block)
  the forked child uses the inherited descriptor:               refused (would block)
  (locks are not inherited by fork: the child is a different process)
  parent opens the file a second time and closes that second fd:
  ... another process now:                                      got the lock
  (closing ANY descriptor of the file drops the process's locks on it)
```

1. **Not inherited by `fork`.** The child using the inherited descriptor was **refused**: it is another process, so the parent's lock excludes it. (Compare `flock`, where the same inherited descriptor shared the lock.)
2. **Preserved across `exec`**, and released when the process terminates (page).
3. **Closing ANY descriptor for the file releases ALL of the process's locks on it**, even locks taken through a different descriptor. The parent opened the file a second time and closed that second descriptor; the next process then **got the lock**. This is the famous trap: a library function that opens a file the program has locked, reads it and closes it **silently drops the program's lock**. The `F_OFD_SETLK` page, comparing the two, calls this behaviour "bad".

Ranges work as expected, and `F_GETLK` names the culprit:

```text
byte ranges and F_GETLK
  parent write-locks bytes 0..99. Another process locks 100..199: got the lock
  another process locks 50..149:                                  refused (would block)
  F_GETLK from another process reports: type write, bytes 0..99, owner pid 595 (parent is 595)
```

A lock on bytes 0 to 99 let another process lock 100 to 199 and **refused** 50 to 149 (overlap); `F_GETLK` reported the type, the range and the **owner's pid**, which makes it a diagnostic tool (`l_pid` is only given for process-associated locks).

**Deadlock detection.** Two processes that each hold one range and wait for the other's (`F_SETLKW`) would wait forever; the kernel detects the cycle and fails one request with **`EDEADLK`**, with a search depth limited to 10 steps (page). The program:

```c
static void sect_deadlock(void)
{
    int fa = open(path, O_RDWR), fb = open(path, O_RDWR);
    (void)fb;
    close(fb);
    int pp[2];
    IGN(pipe(pp));
    pid_t c = fork();
    if (c == 0) {
        int f = open(path, O_RDWR);
        fcntl_range(f, F_WRLCK, 200, 1, F_SETLK);              /* the child holds byte 200 */
        IGN(write(pp[1], "r", 1));
        usleep(200000);
        errno = 0;
        int r = fcntl_range(f, F_WRLCK, 100, 1, F_SETLKW);      /* and wants byte 100 */
        printf("  child  holds 200, waits for 100: F_SETLKW returned %d (%s)\n", r, r < 0 ? (errno == EDEADLK ? "EDEADLK" : strerror(errno)) : "granted");
        _exit(0);
    }
    char b;
    IGN(read(pp[0], &b, 1));
    fcntl_range(fa, F_WRLCK, 100, 1, F_SETLK);                 /* the parent holds byte 100 */
    usleep(100000);
    errno = 0;
    int r = fcntl_range(fa, F_WRLCK, 200, 1, F_SETLKW);        /* and wants byte 200 */
    printf("  parent holds 100, waits for 200: F_SETLKW returned %d (%s)\n", r, r < 0 ? (errno == EDEADLK ? "EDEADLK" : strerror(errno)) : "granted");
    waitpid(c, NULL, 0);
}
```

```text
  child  holds 200, waits for 100: F_SETLKW returned -1 (EDEADLK)
  parent holds 100, waits for 200: F_SETLKW returned 0 (granted)
```

The child, whose request closed the cycle, got `EDEADLK`; the parent's request was then granted when the child's lock vanished at its exit. Which process gets the error depends on timing.

### 8.4 Open file description locks

Linux 3.15 added **OFD locks** (`F_OFD_SETLK`, `F_OFD_SETLKW`, `F_OFD_GETLK`) to fix the close trap: they have the byte ranges of `fcntl` locks and the **ownership of `flock`**, the open file description (`F_OFD_SETLK(2const)`, fetched). They pass to a child through `fork`, they are released only when the **last descriptor of the description** is closed, locks taken through the same description never conflict with each other, and the `l_pid` field of `struct flock` **must be 0** or the call fails with `EINVAL`. The page adds that **no deadlock detection** is done for them. The program repeats the close experiment:

```text
open file description locks (F_OFD_SETLK, Linux 3.15 and later)
  parent holds an OFD lock, opens and closes a second fd:        refused (would block) for another process
  after the lock's own descriptor is closed:                     got the lock
```

Opening and closing a second descriptor **did not release** the OFD lock (another process was still refused); closing the lock's own descriptor released it.

### 8.5 Cost and choice

Uncontended lock plus unlock of the whole file:

```text
  flock                          lock + unlock, uncontended: median 326 ns
  fcntl F_SETLK                  lock + unlock, uncontended: median 484 ns
  fcntl F_OFD_SETLK              lock + unlock, uncontended: median 495 ns
```

All three are a few hundred nanoseconds (two system calls), with `flock` the cheapest. **Choosing**: use **OFD locks** for new Linux code that needs byte ranges or is multi-threaded; use **`flock`** for a whole-file lock such as a pid file; use **POSIX `fcntl` locks** only for portability, and then never open the same file twice in one process, in your code or in a library you call. A process-shared mutex in shared memory (section 5.4) is far faster for protecting a structure, because it needs no system call when uncontended (from memory), but only a file lock guards the **file** itself, including against processes that do not share your memory.

---

## 9. Unix domain sockets

A **Unix domain socket** is the socket interface (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`) used between processes on one machine, with the address a file-system path or an abstract name instead of an IP address and port (`unix(7)`, fetched). It is the most general local IPC and the one to reach for first. What it adds over a pipe:

- **Bidirectional**: one socket pair carries both directions (the benchmark's `socketpair` served both).
- **Three types**: `SOCK_STREAM` (byte stream), `SOCK_DGRAM` (messages, reliable and not reordered on Unix sockets, unlike UDP), and `SOCK_SEQPACKET` (connection-oriented messages that keep boundaries and order; Linux 2.6.4).
- **Many clients, one server**: `listen` and `accept` give each client its own connection; a pipe cannot do that.
- **Credentials and descriptors**: `SO_PEERCRED` reports the peer's pid, uid and gid; `SCM_RIGHTS` passes open file descriptors as ancillary data (the receiver gets a reference to the same open file description, possibly under a different number; the limit is 253 descriptors per message); `SCM_CREDENTIALS` sends the sender's credentials. Descriptor passing is how `memfd` regions and listening sockets are handed between processes ([OS 18](18_ipc_mechanisms.md)).

The program shows what the types do with three sends of 2, 3 and 1 bytes and one big receive, and what happens when the receiver does not read:

```text
three sends of 2, 3 and 1 bytes, then one receive with a 100-byte buffer
  SOCK_STREAM     first receive returned 6 bytes, the next -1
  SOCK_DGRAM      first receive returned 2 bytes, the next 3
  SOCK_SEQPACKET  first receive returned 2 bytes, the next 3

flow control: send 1,024-byte messages until the sender would block
  SOCK_STREAM     accepted 93 messages (93 KiB) before EAGAIN, nothing dropped
  SOCK_DGRAM      accepted 93 messages (93 KiB) before EAGAIN, nothing dropped
  SOCK_SEQPACKET  accepted 93 messages (93 KiB) before EAGAIN, nothing dropped
  SO_SNDBUF of a Unix stream socket here: 212992 bytes
```

- **`SOCK_STREAM` merged all six bytes** into one receive, like a pipe. **`SOCK_DGRAM` and `SOCK_SEQPACKET` returned 2, then 3**: boundaries kept.
- **None dropped data.** A sender that outruns the receiver is **blocked** (or gets `EAGAIN` non-blocking) after about 93 messages of 1,024 bytes, though `SO_SNDBUF` is 212,992 bytes. The difference is per-message kernel overhead charged against the buffer (my reading); the lesson is that **the capacity in small messages is far below the buffer size**, and that unlike UDP, a Unix datagram socket applies backpressure and never discards.

Names and access:

```text
SO_PEERCRED on a socketpair: pid 658, uid 0, gid 0 (this process is 658)
pathname socket: bind created /tmp/h8_sock on disk: yes
  after close(), the file is still there: yes (the program must unlink it)
  binding the same name again: -1, errno EADDRINUSE
abstract socket (name starts with a NUL byte): bind 0, a file on disk: no
  after close(), binding the same abstract name again: 0 (the name vanished with the last reference)
```

- **`SO_PEERCRED`** returned the process's own pid, uid and gid on a `socketpair` (the credentials in effect when the pair was created); on a connected socket it tells a server **who** connected, which is how a service decides whether to trust a request, without any password.
- A **pathname** socket creates a **file** that survives `close` and must be **`unlink`ed**; binding the same name again fails with `EADDRINUSE`. Connecting needs **write permission on the socket file** and search permission on the directories, so the file's mode is the access control (the page warns portable programs not to depend on it).
- An **abstract** socket (a name beginning with a NUL byte) has **no file** and disappears when the last reference closes, so nothing needs cleaning up after a crash; the program rebound the same name immediately after closing. It is not controlled by file permissions, so use `SO_PEERCRED` (from memory of the usual practice).

TCP over the loopback interface is the alternative for local communication that must also work across machines. In the benchmark (section 10) it was slower than a Unix socket for every message size above 64 bytes.

The call sequence for a server, for reference: `socket`, `bind` (the path), `listen`, then a loop of `accept` (one new descriptor per client) and `read` or `recv`. A client: `socket`, `connect`, then `send`. `socketpair` builds the connected pair in one call for related processes, and is the quickest way to make a bidirectional pipe.

---

## 10. The measurements side by side

### 10.1 Round-trip latency

One byte, parent to child and back, medians of seven batches of 20,000. First with the two processes on **different CPUs**:

```text
parent on CPU 0, child on CPU 1, ping-pong latency (1-byte messages)
  pipe                     round trip    24505 ns  (min  23296, max  27136)  one way about  12253 ns
  unix socket stream       round trip    28016 ns  (min  25428, max  31132)  one way about  14008 ns
  unix socket dgram        round trip    30049 ns  (min  28140, max  32713)  one way about  15024 ns
  unix socket seqpacket    round trip    28873 ns  (min  28462, max  31225)  one way about  14437 ns
  tcp loopback             round trip    32725 ns  (min  30574, max  52775)  one way about  16363 ns
  POSIX message queue      round trip    26542 ns  (min  24361, max  29633)  one way about  13271 ns
  System V message queue   round trip    28021 ns  (min  25696, max  28742)  one way about  14010 ns
  POSIX semaphore          round trip    20471 ns  (min  13375, max  21846)  one way about  10236 ns
  System V semaphore       round trip    27387 ns  (min  24545, max  29325)  one way about  13693 ns
  shared flag, spinning    round trip      193 ns  (min    184, max    197)  one way about     96 ns
  signal (SIGUSR1/2)       round trip    26527 ns  (min  22383, max  30117)  one way about  13263 ns
```

Then both on **one CPU**:

```text
parent and child on the same CPU, ping-pong latency (1-byte messages)
  pipe                     round trip     2112 ns  (min   2103, max   2184)  one way about   1056 ns
  unix socket stream       round trip     3393 ns  (min   3104, max   4069)  one way about   1697 ns
  unix socket dgram        round trip     2793 ns  (min   2638, max   4282)  one way about   1396 ns
  unix socket seqpacket    round trip     2669 ns  (min   2646, max   2776)  one way about   1335 ns
  tcp loopback             round trip     3857 ns  (min   3805, max   5651)  one way about   1928 ns
  POSIX message queue      round trip     1971 ns  (min   1948, max   2039)  one way about    986 ns
  System V message queue   round trip     2179 ns  (min   2148, max   2234)  one way about   1090 ns
  POSIX semaphore          round trip     1684 ns  (min   1670, max   1686)  one way about    842 ns
  System V semaphore       round trip     1923 ns  (min   1855, max   1986)  one way about    962 ns
  shared flag, spinning    skipped: a spinner on the same CPU as the thread it waits for burns its whole time slice
  signal (SIGUSR1/2)       round trip     3442 ns  (min   1966, max   3596)  one way about   1721 ns
```

- **Across CPUs every kernel mechanism costs 20 to 33 µs.** The data copy of one byte is negligible; what is left is **the wake-up of a sleeping thread on another CPU**, which in this VM needs an inter-processor interrupt and the host's help. Pipe, queue, socket, semaphore and signal are all within a factor of two of each other because they all pay the same wake-up. The POSIX semaphore, which carries no data, was the cheapest at about 20,471 ns; TCP loopback, with a protocol stack on top, the most expensive.
- **On one CPU the same calls cost about 1.7 to 3.9 µs**, roughly **ten times less** for the pipe (2,112 ns against 24,505 ns): when the peer is on the same CPU, waking it means moving it to the run queue of the CPU you are already on and switching, with no interrupt. This is also why **pinning a client and server to one core can beat spreading them**, for small request and reply messages.
- **Spinning on shared memory: 193 ns**, about **125 times faster** than a pipe across CPUs (24,505 / 193). Nothing sleeps: the waiter polls a cache line and sees the store within one cache-line transfer, about 100 ns each way. The price is two CPUs at 100 percent. On one CPU the spinning variants were **skipped**: a spinner waits for a thread that cannot run until the spinner's time slice ends, so the benchmark would measure the scheduler's tick, not the mechanism.

### 10.2 Throughput

One-way, a writer on CPU 0 and a reader on CPU 1, message sizes 64 B, 4 KiB and 64 KiB (counts chosen to move 64 MiB, 512 MiB and 512 MiB):

```text
parent on CPU 0, child on CPU 1, one-way throughput
  pipe                         64 B x 1048576:     90.7 MB/s    1416712 msgs/s    0.71 us per message
  pipe                       4096 B x  131072:   2376.0 MB/s     580089 msgs/s    1.72 us per message
  pipe                      65536 B x    8192:   1691.1 MB/s      25804 msgs/s   38.75 us per message
  unix socket stream           64 B x 1048576:     52.9 MB/s     826755 msgs/s    1.21 us per message
  unix socket stream         4096 B x  131072:   1968.7 MB/s     480634 msgs/s    2.08 us per message
  unix socket stream        65536 B x    8192:   5606.7 MB/s      85551 msgs/s   11.69 us per message
  unix socket dgram            64 B x 1048576:     63.7 MB/s     995017 msgs/s    1.01 us per message
  unix socket dgram          4096 B x  131072:   2220.7 MB/s     542174 msgs/s    1.84 us per message
  unix socket dgram         65536 B x    8192:   4371.7 MB/s      66707 msgs/s   14.99 us per message
  unix socket seqpacket        64 B x 1048576:     50.6 MB/s     790511 msgs/s    1.27 us per message
  unix socket seqpacket      4096 B x  131072:   2580.9 MB/s     630109 msgs/s    1.59 us per message
  unix socket seqpacket     65536 B x    8192:   6658.6 MB/s     101602 msgs/s    9.84 us per message
  tcp loopback                 64 B x 1048576:    169.1 MB/s    2641804 msgs/s    0.38 us per message
  tcp loopback               4096 B x  131072:    747.7 MB/s     182545 msgs/s    5.48 us per message
  tcp loopback              65536 B x    8192:   2084.6 MB/s      31808 msgs/s   31.44 us per message
  POSIX message queue          64 B x 1048576:     44.7 MB/s     698080 msgs/s    1.43 us per message
  POSIX message queue        4096 B x  131072:   1808.2 MB/s     441458 msgs/s    2.27 us per message
  POSIX message queue       65536 B: not possible (limit 8192 bytes)
  System V message queue       64 B x 1048576:     75.3 MB/s    1177152 msgs/s    0.85 us per message
  System V message queue     4096 B x  131072:   1487.6 MB/s     363181 msgs/s    2.75 us per message
  System V message queue    65536 B: not possible (limit 8192 bytes)
  shared ring, spinning        64 B x 1048576:   1179.5 MB/s   18429718 msgs/s    0.05 us per message
  shared ring, spinning      4096 B x  131072:   7998.5 MB/s    1952751 msgs/s    0.51 us per message
  shared ring, spinning     65536 B x    8192:  12969.1 MB/s     197892 msgs/s    5.05 us per message
```

And on one CPU:

```text
parent and child on the same CPU, one-way throughput
  pipe                         64 B x 1048576:    218.1 MB/s    3408145 msgs/s    0.29 us per message
  pipe                       4096 B x  131072:   4448.3 MB/s    1086011 msgs/s    0.92 us per message
  pipe                      65536 B x    8192:   7380.7 MB/s     112621 msgs/s    8.88 us per message
  unix socket stream           64 B x 1048576:     98.0 MB/s    1531620 msgs/s    0.65 us per message
  unix socket stream         4096 B x  131072:   3793.4 MB/s     926127 msgs/s    1.08 us per message
  unix socket stream        65536 B x    8192:  11056.8 MB/s     168713 msgs/s    5.93 us per message
  unix socket dgram            64 B x 1048576:     84.4 MB/s    1318906 msgs/s    0.76 us per message
  unix socket dgram          4096 B x  131072:   3812.9 MB/s     930890 msgs/s    1.07 us per message
  unix socket dgram         65536 B x    8192:  14092.2 MB/s     215030 msgs/s    4.65 us per message
  unix socket seqpacket        64 B x 1048576:     81.9 MB/s    1279984 msgs/s    0.78 us per message
  unix socket seqpacket      4096 B x  131072:   2958.8 MB/s     722354 msgs/s    1.38 us per message
  unix socket seqpacket     65536 B x    8192:  12748.4 MB/s     194525 msgs/s    5.14 us per message
  tcp loopback                 64 B x 1048576:    119.1 MB/s    1861510 msgs/s    0.54 us per message
  tcp loopback               4096 B x  131072:   2174.8 MB/s     530949 msgs/s    1.88 us per message
  tcp loopback              65536 B x    8192:   2836.6 MB/s      43283 msgs/s   23.10 us per message
  POSIX message queue          64 B x 1048576:    104.2 MB/s    1628325 msgs/s    0.61 us per message
  POSIX message queue        4096 B x  131072:   3581.2 MB/s     874306 msgs/s    1.14 us per message
  POSIX message queue       65536 B: not possible (limit 8192 bytes)
  System V message queue       64 B x 1048576:    101.2 MB/s    1581376 msgs/s    0.63 us per message
  System V message queue     4096 B x  131072:   3060.0 MB/s     747069 msgs/s    1.34 us per message
  System V message queue    65536 B: not possible (limit 8192 bytes)
  shared ring, spinning    skipped: a spinner on the same CPU as the thread it waits for burns its whole time slice
```

- **Tiny messages are limited by the call rate.** At 64 bytes the kernel mechanisms move 45 to 170 MB/s, that is 0.7 to 2.6 million messages a second; the shared ring moves **18 million messages a second** (1,180 MB/s) because it makes no system call. (The TCP row is the highest of the kernel rows at 64 bytes: it merges small writes into larger segments, from memory; a Unix stream socket did not do as well here and I did not find out why.) **Batch small messages.**
- **At 4 KiB the kernel mechanisms are within 1.8 times of each other**, TCP excepted: 1.5 to 2.6 GB/s across CPUs. The `SOCK_SEQPACKET` socket was fastest, the System V queue slowest, TCP loopback far behind at 748 MB/s.
- **At 64 KiB the Unix sockets lead**: `SOCK_SEQPACKET` 6.7 GB/s, stream 5.6 GB/s, datagram 4.4 GB/s, pipe 1.7 GB/s (default 64 KiB pipe; section 2.5 showed 2.9 GB/s with a 1 MiB pipe), TCP 2.1 GB/s. The pipe's small buffer hurts at this size.
- **The shared ring led everything**: about 8.0 GB/s at 4 KiB and 13.0 GB/s at 64 KiB, about 3 times the best kernel mechanism at 4 KiB and 1.9 times at 64 KiB. It is limited by `memcpy` speed and by the cache line moving between two cores, not by the kernel.
- **On one CPU the throughput roughly doubles** (pipe 4 KiB: 4,448 against 2,376 MB/s) because the two sides take turns on warm caches with no cross-CPU wake-ups.
- **Message queues were limited to 8 KiB** here (`msgsize_max`, `msgmax`), so the 64 KiB rows have no value.

### 10.3 What dies with the peer

IPC is also about **failure**. What each mechanism does when the other process dies:

| Mechanism | What the survivor sees |
| --- | --- |
| pipe, FIFO | reader: end of file; writer: `SIGPIPE` or `EPIPE` |
| Unix socket | reader: end of file; writer: `EPIPE` (and `SIGPIPE`), or `ECONNRESET` |
| POSIX message queue | nothing: the queue and its messages **persist**; the survivor waits forever for a reply |
| shared memory | **nothing**: stale data stays in place; the survivor must detect death another way (a heartbeat, `pidfd`, a robust mutex) |
| process-shared mutex, non-robust | blocked forever (measured: timed out after 300 ms) |
| robust mutex | `EOWNERDEAD` at the next lock |
| System V semaphore | locked forever, unless the holder used `SEM_UNDO` |
| `fcntl` or `flock` or OFD lock | released with the process |

The pattern: **descriptor-based mechanisms tell you about death; name-based and memory-based ones do not.** A shared-memory design needs its own liveness protocol.

---

## 11. Choosing

| Need | Use | Why |
| --- | --- | --- |
| a parent and children, a stream of bytes | pipe or `socketpair` | the simplest; ends close when processes exit |
| a local service with many unrelated clients | Unix `SOCK_STREAM` or `SOCK_SEQPACKET` socket | connections, credentials, descriptor passing, `epoll` |
| messages with boundaries and flow control | `SOCK_SEQPACKET` (or `SOCK_DGRAM`) | boundaries without the System V quirks |
| priorities, or messages that outlive the consumer | POSIX message queue | priority order, persistence, `poll`-able |
| a high-rate data plane between two processes | shared-memory ring plus a futex or `eventfd` to sleep | no copy by the kernel, no system call per message ([OS 18](18_ipc_mechanisms.md)) |
| a short event, no data | `eventfd`, or a pipe write | one descriptor in an event loop |
| protecting a shared structure | robust process-shared mutex in the shared memory | fast, and recoverable after a crash |
| protecting a **file**, or a one-instance pid file | `flock` or an OFD lock | released when the process dies |
| a lock that must free itself when the holder dies, with legacy code | System V semaphore with `SEM_UNDO` | the kernel undoes it |
| "tell it to stop" or "reload" | a signal | what signals are for |

Two rules beyond the table. **Do not use signals to carry data**, and **do not use TCP for local traffic** unless you also need it to work across machines. And whatever the mechanism, define **who creates and who removes** each named object (a path, a `/name`, a System V id) so that a crash does not leave one behind.

---
## What it costs

| Operation | Cost (measured here, unless marked) | Why |
| --- | --- | --- |
| pipe round trip, one byte, CPU 0 and CPU 1 | 24,505 ns | two wake-ups across CPUs (an inter-processor interrupt in a VM) |
| the same on one CPU | 2,112 ns | a switch on the local run queue |
| Unix `SOCK_SEQPACKET` / TCP loopback round trip, across CPUs | 28,873 / 32,725 ns | same wake-up, plus the TCP protocol work |
| POSIX semaphore, System V semaphore round trip, across CPUs | 20,471 / 27,387 ns | no data to copy, still a wake-up |
| signal ping-pong, across CPUs | 26,527 ns | `kill` plus `sigwaitinfo`, the same wake-up |
| shared-memory flag, spinning | 193 ns | two cache-line transfers, no system call; costs two busy CPUs |
| 4 KiB messages, pipe / seqpacket / shared ring | 2376.0 / 2580.9 / 7998.5 MB/s | copies and wake-ups, against one `memcpy` each side |
| pipe capacity | 65,536 bytes (16 pages); `F_SETPIPE_SZ` to 1 MiB works | `pipe(7)` |
| pipe write that stays atomic | up to 4,096 bytes; 4,097 mixed in thousands of records | `PIPE_BUF` |
| standard signals sent while blocked: 100 | 1 delivery; real-time: 100 deliveries | no queue against queue |
| `kill` to a handler / `kill` plus `signalfd` read | 1,268 / 555 ns | a signal frame and `sigreturn` against one read |
| one increment of a shared counter by two processes: plain / atomic / mutex / semaphore | lost updates / 24.5 / 55.0 / 65.4 ns per increment | one contended cache line, and the futex sleep |
| `flock` / `fcntl` / OFD lock plus unlock | 326 / 484 / 495 ns | two system calls |
| process-shared mutex plus condition variable round trip | 27,155 ns | a futex sleep and wake in each direction |
| non-robust mutex whose owner died | `timedlock` timed out after 300 ms; a plain `lock` blocks forever | the lock stays held |

---

## Common mistakes

1. **Forgetting to close the unused pipe ends.** A reader whose own process, or any sibling, keeps a write end open never sees end of file. After `fork`, each process closes the ends it does not use.
2. **Writing to a pipe or socket without handling `SIGPIPE`.** The default action kills the process. Ignore it and test for `EPIPE`.
3. **Assuming a pipe or a stream socket keeps message boundaries.** Two 5-byte writes arrived as one 10-byte read. Frame with a length prefix.
4. **Assuming large writes to a shared pipe are atomic.** Only up to `PIPE_BUF` (4,096): 4,097-byte records mixed thousands of times.
5. **Counting standard signals.** 100 sent, 1 delivered. Use a real-time signal, or better a different mechanism.
6. **Calling `printf` or `malloc` in a signal handler.** Only async-signal-safe functions, and save and restore `errno`.
7. **Not handling `EINTR`.** Without `SA_RESTART` a blocking `read` returned -1 after 100 ms. `poll`, `epoll_wait` and the System V calls are never restarted at all.
8. **Relying on the order of handlers.** With an empty `sa_mask` the handlers started in the reverse of the delivery order.
9. **Leaking IPC objects.** POSIX shared memory, queues and semaphores, and every System V object, outlive the process that made them. Unlink or remove them, and look with `ls /dev/shm`, `ipcs` and `ls /dev/mqueue`.
10. **Forgetting `ftruncate` after `shm_open`.** The object starts at size 0, and touching beyond its end is `SIGBUS`.
11. **Sharing memory with a plain integer and no atomic.** Two processes lost 46 percent of their increments in one run.
12. **Using a non-robust mutex in shared memory.** If its owner dies, every other process blocks forever. Make it robust and write the `EOWNERDEAD` recovery.
13. **Putting pointers in shared memory.** The region may be mapped at a different address in each process; use offsets.
14. **Using `fcntl` locks in a program that also uses libraries on the same file.** Any `close` of any descriptor of the file drops all the process's locks. Use OFD locks or `flock`.
15. **Choosing signals or TCP when a pipe, a Unix socket or shared memory fits.** The measured cost of the wrong choice is a factor of two to 125.

---

## Questions out loud

Answer each in two minutes, with a number or a diagram, before looking back (the section in brackets).

1. Name the IPC mechanisms and sort them by what they carry, whether they keep boundaries, who can find them and how long they live. [1]
2. What does `pipe` give you, and what happens when it is full, empty, or has had one end closed? [2.1, 2.2]
3. What does `PIPE_BUF` promise and what does it not? How would you let several processes write whole records into one channel? [2.3]
4. Build `a | b` with system calls. Which descriptors does each process close, and what hangs if you forget? [2.6]
5. Which signals queue? What does the kernel do with 100 `SIGUSR1` sent while the target blocks it? [3.2]
6. What may a signal handler call? What does it do with `errno`, and what pattern moves the work out of the handler? [3.4]
7. What does `SA_RESTART` do, and which calls ignore it? [3.3]
8. What do `fork` and `exec` do with dispositions, the mask and pending signals? [3.5]
9. What are a process group and a session? Which `setsid` error does the daemon idiom avoid? [4.1, 4.3]
10. Write the daemon recipe and say what each step is for. [4.3]
11. Name five ways to create shared memory and what each is named by. What happens to each when the creator dies? [5.1]
12. Why does a plain shared counter lose updates, and what does fixing it cost? [5.3]
13. What must you do to a mutex to use it between processes? What is a robust mutex? [5.4]
14. What rules make a shared-memory ring buffer correct? Where are acquire and release needed? [5.5]
15. POSIX against System V semaphores: names, atomicity, what happens if the holder dies. [6]
16. POSIX queues against System V queues: priority, selection by type, `poll`, limits. [7]
17. Compare `flock`, `fcntl` locks and OFD locks on ownership, inheritance, and `close`. [8]
18. What do Unix sockets add to pipes? Stream against datagram against seqpacket. [9]
19. Why is a cross-CPU round trip ten times a same-CPU one? When does pinning help? [10.1]
20. Pick a mechanism for each: a log pipeline, a local RPC service, a 10 Gb/s data plane, a one-instance guard, a shutdown request. [11]

---

## Problems

**Where to solve them.** On your own machine: compile the programs described above and use the tools that show the state: `ls -l /dev/shm`, `ipcs` and `ipcrm` for System V objects, `ls /dev/mqueue` after `mount -t mqueue none /dev/mqueue`, `ss -x` for Unix sockets, `lslocks` and `/proc/locks` for file locks, `ps -o pid,pgid,sid,tty,comm` for groups and sessions, `strace -f` to watch `pipe`, `dup2`, `fork` and `exec` in a shell pipeline, and `/proc/sys/fs/pipe-max-size`. For practice problems of the right shape, the concurrency questions on LeetCode 1114, 1115 and 1116 (print in order, print FooBar alternately, H2O) exercise semaphores and condition variables inside one process (numbers from memory), and "implement a shell that supports pipes" is a standard systems exercise. Beej's Guide to Unix IPC is a common free reading (from memory; not fetched).

**How to approach them.** State the mechanism's rule first (capacity, atomicity limit, queueing rule), then count. For latency problems, write the formula `time = fixed round trip + marginal cost * batch`. For ownership problems (locks), ask what the lock belongs to (process, description) and what releases it. For crash problems, ask what the survivor sees (section 10.3).

### Problem A (E, entry level): pipe rules

A parent creates a pipe with the default 64 KiB capacity. (a) With non-blocking 4,096-byte writes and no reader, how many writes succeed before `EAGAIN`? (b) The parent sets the capacity to 1 MiB first: how many? (c) The reader process then closes its end: what does the next `write` do, if `SIGPIPE` has its default action, and if it is ignored? (d) The writer closes its end after leaving 100 bytes in the pipe: what do the reader's next three `read`s with a 4,096-byte buffer return? (e) What does `F_SETPIPE_SZ` with 100 do?

### Problem B (E, entry level): pending signals

A program blocks `SIGUSR1` (10), `SIGUSR2` (12) and `SIGRTMIN` (34), installs handlers with a full `sa_mask`, and is sent `SIGUSR1` five times, `SIGRTMIN` five times with values 1 to 5, and `SIGUSR2` once, in a mixed order. Then it unblocks all three. How many handler calls happen, in what order, and with what values?

### Problem C (M, medium): batching a request stream

A client sends one-byte requests to a server on another CPU and waits for each reply. Take the measured pipe round trip across CPUs (24,505 ns), on one CPU (2,112 ns) and with spinning on shared memory (193 ns), and the measured pipe rate of 64-byte messages, 1,416,712 per second across CPUs. (a) What request rate does each of the three designs reach? (b) The client now sends B requests without waiting and then collects B replies; model the time of one batch as the round trip plus 0.706 µs per request (the inverse of the message rate). What is the smallest B that reaches 1,000,000 requests per second, and what does it do to the latency of one request?

### Problem D (M, medium): who owns the lock

A process holds a lock on a whole file. For each of the three lock kinds (`flock`, `fcntl` `F_SETLK`, `F_OFD_SETLK`), say whether the lock is still held and who is excluded after each event, applied one at a time to the starting state: (1) the process calls a library function that opens the same file in a new descriptor, reads, and closes it; (2) the process forks, and the child tries the lock on the inherited descriptor; (3) the process calls `dup` on its descriptor and closes the original; (4) the process exits without unlocking; (5) another, unrelated process opens the file itself and tries the lock.

### Problem E (H, hard): a shared-memory ring

A producer and a consumer share a ring of 1 MiB (1,048,576 bytes), with 4 KiB messages. The producer's `head` is 1,050,000 and the consumer's `tail` is 300,000 (byte counts that only grow; the position is the count modulo the ring size). (a) How many bytes are in the ring, how many are free, and how many whole messages can the producer add now? (b) At what offset will the next byte be written, and does a 4 KiB write there wrap? If `head` were 1,046,000 at an offset, would it wrap, and how is that write split? (c) Which stores need release ordering and which loads need acquire ordering, and what breaks without each? (d) How does the consumer sleep when the ring is empty without losing a wake-up? (e) The consumer crashes: how does the producer find out, and what state does a restarted consumer resume from? (f) At the measured 1,952,751 messages per second, how many MB/s is that?

---

## Answers

**A.** (a) 65,536 / 4,096 = **16** writes (measured 65,536 bytes accepted); the 17th returns `EAGAIN`. (b) 1,048,576 / 4,096 = **256**. (c) With the default action the process is **killed by `SIGPIPE`**; with the signal ignored, `write` returns -1 with **`EPIPE`**. (d) The first `read` returns **100** (all that is buffered, not 4,096), the second and the third return **0** (end of file, since no write end remains open). (e) The request is **rounded up to one page**, 4,096 bytes (the program printed 4,096).

**B.** A standard signal does not queue, so `SIGUSR1` is delivered once and `SIGUSR2` once; real-time `SIGRTMIN` queues, so it is delivered five times, with values 1 to 5 in the order sent. That is `1 + 1 + 5 =` **7 handler calls**. With a full `sa_mask` the handlers run one after another in delivery order: standard signals first, lowest number first (**`SIGUSR1`, then `SIGUSR2`**), then the real-time signal five times (**values 1, 2, 3, 4, 5**). The order in which they were sent does not matter. (With an empty `sa_mask` the handler frames nest and the handlers would **start** in the reverse order, section 3.2.)

**C.** (a) The rate is the inverse of the round trip: `1 / 24.505 µs =` **40,808** per second across CPUs; `1 / 2.112 µs =` **473,485** on one CPU; `1 / 193 ns =` **5,181,347** with spinning (and two busy CPUs). (b) A batch of `B` takes `24.505 + 0.706 B` µs, and the rate is `B / (24.505 + 0.706 B)` per µs. One million per second means one request per microsecond: `24.505 + 0.706 B <= B`, so `B >= 24.505 / 0.294 = 83.35`: **B = 84** (check: `24.505 + 59.3 = 83.8 µs` for 84 requests, `84 / 83.8 µs =` 1.002 million per second). The cost: the **first request of a batch waits for the whole batch**, so its latency is about **84 µs** against 24.5 µs for one request alone. Batching trades latency for throughput. The model reuses the one-way 64-byte rate as the marginal cost of a request and a reply together; real replies would add cost.

**D.**

| Event | `flock` | `fcntl` | OFD |
| --- | --- | --- | --- |
| 1. library opens and closes the same file | **held** (a separate description, closing it does not matter) | **released** (closing any descriptor drops the process's locks) | **held** |
| 2. child uses the inherited descriptor | **succeeds** (same description) | **refused** (a different process) | **succeeds** (same description, locks from one description never conflict) |
| 3. `dup`, then close the original | **held** (the description still has a descriptor) | **released** (closing any descriptor of the file) | **held** |
| 4. the process exits | released | released | released |
| 5. an unrelated process opens the file and tries | refused | refused | refused |

(The `dup` case for `fcntl` follows the rule in the page; the program tested the second-`open` form of it, which behaved the same way.)

**E.** (a) In the ring: `1,050,000 - 300,000 =` **750,000** bytes. Free: `1,048,576 - 750,000 =` **298,576**. Whole messages: `298,576 / 4,096 = 72.9`, so **72** (294,912 bytes), leaving 3,664 bytes unusable until the consumer advances. (b) The position is `1,050,000 mod 1,048,576 =` **1,424**; the write covers 1,424 to 5,519 and **does not wrap**. At `head` position 1,046,000 the write would end at `1,046,000 + 4,096 = 1,050,096`, past 1,048,576, so it **wraps**: the first `1,048,576 - 1,046,000 =` **2,576** bytes go to the end of the ring and the remaining **1,520** bytes go to offset 0, two `memcpy` calls (the code in section 5.5 splits at the end of the ring). (c) The producer stores `head` with **release** after the `memcpy`, and the consumer loads `head` with **acquire** before reading the data: without them the consumer could see the new `head` before the bytes. The consumer stores `tail` with **release** after it has copied the data out, and the producer loads `tail` with **acquire** before overwriting: without them the producer could overwrite bytes the consumer is still reading. Each side may read **its own** index with a relaxed load. (d) The consumer spins briefly, then sets a "sleeping" flag, **re-checks `head`**, and only then waits on a futex or an `eventfd`; the producer, after storing `head`, checks the flag and wakes the consumer. The re-check after setting the flag closes the window in which the producer stored `head` and looked at the flag just before it was set (a lost wake-up). (e) Nothing in the ring tells the producer: use a `pidfd` (becomes readable when the process exits), a heartbeat counter, or a robust mutex around the registration. The `tail` is **in the shared memory**, so a restarted consumer continues from it, and messages produced meanwhile are still there, until the ring fills and the producer must stop or drop. (f) `1,952,751 * 4,096 / 1,000,000 =` **7,999 MB/s** (the benchmark printed 7,998.5).

---

## Cheat sheet

- **Models**: message passing (kernel copies twice, orders, blocks) against shared memory (no kernel in the path, you add the synchronisation). Cost of small messages is the **wake-up**, not the copy.
- **Pipe**: 64 KiB (16 pages), no boundaries, `read` returns what is there, EOF when all write ends close, `SIGPIPE`/`EPIPE` with no reader, atomic writes up to `PIPE_BUF` 4,096, packet mode `O_DIRECT` 16 slots, FIFO opens block until both ends, `F_SETPIPE_SZ` up to `pipe-max-size`.
- **Pipeline**: `pipe`, `fork`, `dup2` onto 0 and 1, close every unused end in every process, `exec`, `wait`.
- **Signals**: standard do not queue (100 sent, 1 delivered), real-time queue with a value, delivered lowest first with standard before real-time; handlers only call async-signal-safe functions and save `errno`; `SA_RESTART` does not cover `poll`, `epoll_wait`, `select`, System V `msgrcv` and `semop`, `sigwait`; `exec` resets handlers but keeps ignored, mask and pending; `signalfd` or self-pipe moves handling into the loop; `SA_NOCLDWAIT` reaps automatically.
- **Groups and sessions**: `setpgid`, `setsid` (fails with `EPERM` for a group leader, drops the controlling terminal), `kill(-pgid, sig)`, foreground group gets `Ctrl-C`, background reads get `SIGTTIN`, daemon: fork, `setsid`, fork, `chdir`, close descriptors.
- **Shared memory**: anonymous (fork only), `memfd` (descriptor), `shm_open` (`/name` in `/dev/shm`, needs `ftruncate`, leaks until `shm_unlink`), System V (`shmget`, until `IPC_RMID`); beyond the end is `SIGBUS`; offsets not pointers; cache-line alignment; acquire and release; a plain counter loses updates; process-shared and **robust** mutex; `EOWNERDEAD` then `pthread_mutex_consistent`.
- **Semaphores**: POSIX named (`/dev/shm/sem.name`) or unnamed in shared memory; System V sets, `semop` atomic over several, `SEM_UNDO` restores on death; `semop` never restarts.
- **Queues**: POSIX (priority, boundaries, fd for `poll`, `msg_max` 10, `msgsize_max` 8,192, receive buffer at least `msgsize`); System V (type selection: positive exact, negative lowest at most, 0 oldest; 16 KiB default; not a descriptor).
- **File locks**: `flock` (description, whole file, survives `fork` and `dup`); `fcntl` (process, ranges, **close any descriptor drops all**, not inherited, `EDEADLK`); OFD (description, ranges, Linux 3.15, `l_pid` 0, no deadlock detection); advisory only; released on process exit.
- **Unix sockets**: stream, datagram (reliable, ordered), seqpacket (boundaries and order); `socketpair`; pathname (file, `unlink`, permissions), abstract (no file); `SO_PEERCRED`, `SCM_RIGHTS`; backpressure not loss.
- **Numbers**: pipe round trip 24.5 µs across CPUs, 2.1 µs on one; spinning on shared memory 0.19 µs; shared ring 8 GB/s at 4 KiB against 2.4 for a pipe; `flock` pair 326 ns; mutex under contention 55 ns per increment.
- **Peer death**: pipes and sockets say so; queues, shared memory, semaphores and non-robust mutexes do not.

---

## Sources

- [pipe(7)](https://man7.org/linux/man-pages/man7/pipe.7.html) (fetched): `PIPE_BUF` and atomicity, capacity, `pipe-max-size` and the user limits, EOF, `SIGPIPE`, `EPIPE`. The page did not describe packet mode or the FIFO open rules; those I measured.
- [signal(7)](https://man7.org/linux/man-pages/man7/signal.7.html) and [signal-safety(7)](https://man7.org/linux/man-pages/man7/signal-safety.7.html) (fetched): standard and real-time signals, queueing, delivery order, `fork` and `exec` inheritance, restart rules, the `errno` rule, unsafe stdio. The safe-function list was only sampled.
- [signalfd(2)](https://man7.org/linux/man-pages/man2/signalfd.2.html) (fetched): blocking first, `signalfd_siginfo`, `fork` and `exec` behaviour. It does not say outright that instances coalesce; the program shows it.
- [credentials(7)](https://man7.org/linux/man-pages/man7/credentials.7.html) and [setsid(2)](https://man7.org/linux/man-pages/man2/setsid.2.html) (fetched): process groups, sessions, controlling terminal, foreground jobs, `EPERM`. The negative-pid form of `kill` and the second fork's purpose are from memory.
- [shm_overview(7)](https://man7.org/linux/man-pages/man7/shm_overview.7.html), [sem_overview(7)](https://man7.org/linux/man-pages/man7/sem_overview.7.html), [mq_overview(7)](https://man7.org/linux/man-pages/man7/mq_overview.7.html) and [sysvipc(7)](https://man7.org/linux/man-pages/man7/sysvipc.7.html) (fetched): POSIX shared memory, semaphores and queues with their limits and persistence. The System V page does not say how long objects live; that and the `sem_post` details are from memory.
- [pthread_mutexattr_setrobust(3)](https://man7.org/linux/man-pages/man3/pthread_mutexattr_setrobust.3.html) (fetched): robust mutexes, `EOWNERDEAD`, `ENOTRECOVERABLE`.
- [flock(2)](https://man7.org/linux/man-pages/man2/flock.2.html), [F_SETLK(2const)](https://man7.org/linux/man-pages/man2/F_SETLK.2const.html) and [F_OFD_SETLK(2const)](https://man7.org/linux/man-pages/man2/F_OFD_SETLK.2const.html) (fetched; the main `fcntl(2)` page is only an index): the three lock kinds, inheritance, release rules, deadlock detection, the end of mandatory locking.
- [unix(7)](https://man7.org/linux/man-pages/man7/unix.7.html) (fetched): socket types, address kinds, `SO_PEERCRED`, `SCM_RIGHTS`, permissions.
- Measurements on this machine: Linux 6.18.44 in a Firecracker VM, 2 vCPUs, GCC 13.3, eight programs, as root. Cross-CPU numbers include the VM's cost of inter-processor interrupts and are not those of bare metal.
- Claims marked "from memory" were not fetched: the signal number table, double-fork rationale and systemd practice, `splice` and zero-copy variants, the two copies in a pipe, glibc's futex-based semaphores and the uncontended fast path of a mutex, the usual practice for abstract sockets, `pidfd`, the sealed `memfd`, the System V queue not being a descriptor, the LeetCode numbers and Beej's guide.
