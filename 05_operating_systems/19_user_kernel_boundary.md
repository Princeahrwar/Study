# OS 19: The user/kernel boundary

*Part of the [Operating systems plan](00_plan.md) · Time: about 10 hours (7 reading and experiments, 3 problems) · You need: [OS 01](01_os_structure_and_system_calls.md) (system calls), [OS 11](11_virtual_memory.md) and [OS 12](12_virtual_memory_and_tlb.md) (address spaces, page faults, the TLB), and [OS 17](17_ipc_fundamentals.md) and [OS 18](18_ipc_mechanisms.md) (the shared-memory ring and its wake-ups, which are the "share memory" rung of the ladder in section 4)*

This is a rewrite. The earlier version was a short note whose costs ("50 to 100 ns, or 150 to 400 ns with mitigations") were remembered ranges, and one of its exercises assumed a clock that is not accelerated; this version measures each claim and corrects the ones that did not hold. It is the bridge into the kernel track, and the questions it answers are the ones a senior systems interview uses to test whether you think in costs. What exactly happens when a program makes a system call, and what does it cost? Which "system calls" never enter the kernel, and why is `clock_gettime` cheap and `getpid` not? What does the kernel do with a pointer that user space hands it, how can that go wrong (bad pointers, short copies, a double fetch), and what rules follow? How do you cut the number of crossings (batch, share memory, move the work into the kernel, take the device out), and where does each rung stop paying? What does `io_uring` change, and what does it not? How do you measure all of this on a machine you do not own? The tutorial ends with the arithmetic that turns "10 million packets a second" into a design choice.

**Three honest limits, stated first.** (1) The machine is a Firecracker micro-VM with 2 virtual CPUs on a CPU that is **not affected by Meltdown**, so its kernel does not switch page tables on every system call (no KPTI). The "150 to 400 ns with mitigations" of the old note describes machines that do; I cannot reboot this one with different boot parameters, so **the price of those mitigations is not measured here**, and what I report is the cost with the mitigations this CPU needs (section 1.3). (2) There is no `perf`, so I time with the clock, `getrusage` and `strace`. (3) System calls do not exit to the hypervisor, so their cost here should be close to bare metal for this CPU (from memory, not verified), but anything that wakes a thread on another CPU (the `io_uring` poller in section 5) pays the virtual inter-processor interrupt cost described in [OS 18](18_ipc_mechanisms.md). Anything I did not run or fetch is marked "from memory".

By the end you can:

- describe what a system call does, in order, and quote its cost on a machine you measured (measured: `getppid` through `syscall(2)` took 96.6 / 106.3 ns against 0.7 to 1.0 ns for an ordinary function call, about 100 times more);
- say which time and CPU queries never enter the kernel and prove it (measured: `clock_gettime(CLOCK_MONOTONIC)` took 24.4 / 25.5 ns, `time` and `sched_getcpu` 1.4 ns, and the same clock forced through `syscall(2)` took 137.2 / 143.0 ns; `strace` showed only the forced calls);
- split the cost of a read into a fixed part and a per-byte part (measured: about 130 ns fixed, and above 64 KiB about 47 GB/s);
- say what the kernel returns for a bad user pointer, for a pointer that is partly bad, and for a pointer it never touches (measured: `EFAULT`, a short count of 100, and success);
- explain the double-fetch bug and the copy-then-validate rule (measured: 3,155 of 5,000,000 checks passed and then used an illegal length when the length was read twice, and none when it was read once);
- climb the ladder, batch then share then move the work then bypass, and say which rung a problem needs (measured: `writev` cut the cost of a 1-byte piece from 114.2 ns to 4.7 ns, while `recvmmsg` on UDP changed the per-packet cost by about 12 percent because the network stack dominates);
- explain what `io_uring` saves and what it does not (measured: 176.7 ns per operation with one `io_uring_enter` each, 23.9 ns at 64 per call, and zero `io_uring_enter` calls with `SQPOLL`, which was slower per operation in the test);
- do the packet-rate arithmetic that a design review starts from (10 million packets a second is 100 ns each, and one bare system call is about 97 to 106 ns).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run as root on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory (glibc 2.39, x86_64, an Intel Xeon at a nominal 2.10 GHz, clock source `tsc`). Four programs were written for this tutorial; the functions are printed in the text and their output is pasted unedited. Kernel behaviour was checked against the manual pages `vdso(7)`, `syscall(2)`, `io_uring(7)` and `io_uring_setup(2)`, listed under Sources with what each page did and did not confirm.

**Data and timing.** Latencies are medians of five to seven batches of 300,000 to 2,000,000 operations, pinned to CPU 0 unless a test needs both CPUs. Run-to-run differences of 10 percent are normal in a VM: the `getppid` cost was 96.6 ns in one run and 106.3 ns in a second, and I quote both where it matters. Counts (which calls appear in `strace`, which pointers fail) are deterministic.

---

## 1. What a system call is, and what it costs

### 1.1 The path

A system call is a **controlled privilege transition**, not a function call. On x86-64 the sequence is the following (the register convention is from `syscall(2)`, fetched; the hardware steps are from memory of the architecture manuals, since the page does not list them):

```text
  user code:  put the call number in rax, the arguments in rdi, rsi, rdx, r10, r8, r9
  user code:  SYSCALL instruction
  CPU:        save the return address (in rcx) and flags (in r11), switch to ring 0,
              load the kernel entry address from a model-specific register
  kernel:     entry code: switch to the kernel stack, save the user registers,
              (swapgs to reach per-CPU data), [on CPUs that need it: switch page tables]
  kernel:     index the system call table with rax and run the handler
  kernel:     restore registers, [switch page tables back], SYSRET to user code
  user code:  the result is in rax; the C library turns a small negative value into -1 and errno
```

The convention the page confirms: the number goes in `eax`, the return value comes back in `rax`, and the six arguments use `rdi`, `rsi`, `rdx`, `r10`, `r8` and `r9` (note `r10`, where the ordinary C calling convention has `rcx`, which the instruction itself overwrites; that part is from memory). The page also says the `syscall` instruction "might not be the fastest or best way to transition to the kernel" and refers to `vdso(7)`, which is the subject of section 2.

Every part of the path is overhead that a handler's work sits on top of. Costs that are not in the diagram but are real: the instruction pipeline is serialised at the transition, the entry code adds mitigation barriers on some CPUs, and the kernel's code and data compete with the program's for the caches and the TLB ([OS 12](12_virtual_memory_and_tlb.md)).

### 1.2 The measurement

The program `m1_syscall.c` times each call in a tight loop (a million calls per batch, seven batches, the median):

```c
static void one(int which, long n)
{
    struct timespec ts;
    struct timeval tv;
    char b;
    for (long i = 0; i < n; i++) {
        switch (which) {
        case N_FUNC: sink = plain_call(i); break;
        case N_GETPPID: sink = syscall(SYS_getppid); break;
        case N_GETPID: sink = getpid(); break;
        case N_CGT_MONO: clock_gettime(CLOCK_MONOTONIC, &ts); sink = ts.tv_nsec; break;
        case N_CGT_RAW: clock_gettime(CLOCK_MONOTONIC_RAW, &ts); sink = ts.tv_nsec; break;
        case N_CGT_REAL: clock_gettime(CLOCK_REALTIME, &ts); sink = ts.tv_nsec; break;
        case N_CGT_COARSE: clock_gettime(CLOCK_MONOTONIC_COARSE, &ts); sink = ts.tv_nsec; break;
        case N_TIME: sink = time(NULL); break;
        case N_GTOD: gettimeofday(&tv, NULL); sink = tv.tv_usec; break;
        case N_CPU: sink = sched_getcpu(); break;
        case N_CGT_SYS: syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts); sink = ts.tv_nsec; break;
        case N_READ0: IGN(read(fd_zero, &b, 1)); break;
        case N_WRITE0: IGN(write(fd_null, &b, 1)); break;
        }
    }
}

static void sect_cost(void)
{
    enum { B = 7, N = 1000000 };
    printf("one call, median of %d batches of %d (CPU 0)\n", B, N);
    for (int w = 0; w < N_COUNT; w++) {
        double r[B];
        one(w, 20000);
        for (int b = 0; b < B; b++) {
            double t0 = now();
            one(w, N);
            r[b] = (now() - t0) / N * 1e9;
        }
        qsort(r, B, sizeof r[0], cmp);
        printf("  %-42s %7.1f ns\n", names[w], r[B / 2]);
    }
}
```

```text
one call, median of 7 batches of 1000000 (CPU 0)
  ordinary function call (not inlined)           0.7 ns
  getppid via syscall(2)                        96.6 ns
  getpid (glibc)                                93.1 ns
  clock_gettime(CLOCK_MONOTONIC)                24.4 ns
  clock_gettime(CLOCK_MONOTONIC_RAW)            24.7 ns
  clock_gettime(CLOCK_REALTIME)                 24.1 ns
  clock_gettime(CLOCK_MONOTONIC_COARSE)          4.7 ns
  time(NULL)                                     1.4 ns
  gettimeofday                                  23.8 ns
  sched_getcpu                                   1.4 ns
  clock_gettime forced through syscall(2)      137.2 ns
  read 1 byte from /dev/zero                   130.2 ns
  write 1 byte to /dev/null                    108.5 ns
```

How to read it:

- **An ordinary function call costs 0.7 ns; a system call that does nothing costs 96.6 / 106.3 ns** (96.6 ns in this run, 106.3 ns in a second run). That is about 100 times more, and about 200 cycles at the nominal 2.10 GHz. This is the number to quote: the cost of crossing, on this machine, with its mitigations (section 1.3).
- **`getpid` is not cached.** It costs the same as `getppid` (93.1 / 99.0 ns), so this glibc (2.39) makes a real system call for it. Older glibc versions cached the pid in user space, which is why the "classic puzzle" answer in old notes is dated: today the contrast is with `clock_gettime`, not with a cached `getpid`.
- **A call that does work costs the crossing plus the work.** `read` of one byte from `/dev/zero` took 130.2 / 130.7 ns, about 33 ns more than the empty call (descriptor lookup, the file operation, the security hooks), and a one-byte `write` to `/dev/null` took 108.5 / 107.5 ns. When you estimate a design, the crossing is the floor, and the real handler is above it.
- **The account of user and system time is not a clean split.** Five million `getppid` calls took 0.534 s of wall time (107 ns each), of which the kernel's accounting put 0.300 s in system time and 0.228 s in user time, even though the loop does almost nothing in user space:

```c
static void sect_split(void)
{
    struct rusage a, b;
    getrusage(RUSAGE_SELF, &a);
    double t0 = now();
    one(N_GETPPID, 5000000);
    double dt = now() - t0;
    getrusage(RUSAGE_SELF, &b);
    double u = (b.ru_utime.tv_sec - a.ru_utime.tv_sec) + (b.ru_utime.tv_usec - a.ru_utime.tv_usec) / 1e6;
    double s = (b.ru_stime.tv_sec - a.ru_stime.tv_sec) + (b.ru_stime.tv_usec - a.ru_stime.tv_usec) / 1e6;
    printf("5,000,000 getppid calls took %.3f s wall; the kernel accounted %.3f s as system time and %.3f s as user time\n", dt, s, u);
}
```

```text
5,000,000 getppid calls took 0.534 s wall; the kernel accounted 0.300 s as system time and 0.228 s as user time
```

The 43 percent attributed to user time is far more than a one-line loop can use, and I did not establish why (a plausible cause, from memory, is that this kernel accounts time by sampling at timer ticks, which attributes a tick to whichever mode it interrupts, so the proportions are approximate). Do not read the split of `time(1)` or `getrusage` as the physical fraction spent in the kernel for loops this short; use it for large differences.

### 1.3 Mitigations, and what this machine does

The old note's "150 to 400 ns with KPTI and Spectre mitigations" is a statement about the CPU. The kernel reports what it applies in `/sys/devices/system/cpu/vulnerabilities`:

```text
== vulnerabilities
gather_data_sampling: Not affected
ghostwrite: Not affected
indirect_target_selection: Not affected
itlb_multihit: Not affected
l1tf: Not affected
mds: Not affected
meltdown: Not affected
mmio_stale_data: Not affected
old_microcode: Not affected
reg_file_data_sampling: Not affected
retbleed: Not affected
spec_rstack_overflow: Not affected
spec_store_bypass: Mitigation: Speculative Store Bypass disabled via prctl
spectre_v1: Mitigation: usercopy/swapgs barriers and __user pointer sanitization
spectre_v2: Mitigation: Enhanced / Automatic IBRS; IBPB: conditional; PBRSB-eIBRS: SW sequence; BHI: Vulnerable
srbds: Not affected
tsa: Not affected
tsx_async_abort: Not affected
vmscape: Not affected
```

On this CPU: `meltdown: Not affected`, so there is **no page-table switch on the system call path** (the largest single cost in the "with mitigations" range on affected CPUs, from memory). Spectre variant 2 is handled with **enhanced IBRS**, a hardware setting that costs far less than the retpoline and IBRS-on-entry schemes of older CPUs, and spectre v1 with pointer sanitisation and `swapgs` barriers on the entry path. The report also says `BHI: Vulnerable`; I did not investigate why, and it means this guest does not run with the branch-history mitigation that some CPUs need. So **96.6 / 106.3 ns is the cost on a CPU that needs few mitigations**, and a CPU affected by Meltdown, with the page-table switch, would be slower. Exercise 1 of the old note (boot with `mitigations=off` and subtract) is the right experiment and I could not run it here; do it on your own machine, and keep the boot line in the notes next to the number, because a syscall cost without its mitigation set is not a comparable number.

### 1.4 The arithmetic that makes it matter

At 10 million packets a second you have **100 ns per packet**. One bare system call per packet costs 97 to 106 ns here, so the crossing alone consumes the whole budget before any work is done, and a real receive (`recvfrom` on UDP measured at about 240 ns per packet in section 4.1) is more than twice the budget. The conclusion does not depend on the exact number. A design with a system call in the per-packet path cannot reach 10 Mpps on one core, and the rest of this tutorial is about the four ways out.

---

## 2. The vDSO: calls that never cross

### 2.1 What it is

Some questions have an answer the kernel already keeps in memory: the current time, the current CPU. The kernel maps a small shared library, the **vDSO** ("virtual dynamic shared object"), into every process, and exports a few functions from it that read the kernel's data **in user mode**, with no transition. The manual page gives the reason: system calls that are used often "can dominate performance due to the context-switch overhead that results from exiting user space and entering the kernel" (fetched). On x86-64 it lists four exported functions, `__vdso_clock_gettime`, `__vdso_gettimeofday`, `__vdso_time` and `__vdso_getcpu` (fetched), and the kernel hands the base address to the program in the auxiliary vector (`AT_SYSINFO_EHDR`), usually randomised at each `execve` (fetched). The mapping in this process:

```text
== vdso mapping
7fce53eb9000-7fce53ebb000 r-xp 00000000 00:00 0 [vdso]
```

(`r-xp`: readable, executable, private, 8 KiB, no file behind it.) The C library resolves `clock_gettime`, `gettimeofday`, `time` and `getcpu` to these functions at start-up and falls back to a real system call when the vDSO cannot serve the request.

### 2.2 The measurement

The same table again, now read for the time calls:

```text
  clock_gettime(CLOCK_MONOTONIC)                24.4 ns
  clock_gettime(CLOCK_MONOTONIC_RAW)            24.7 ns
  clock_gettime(CLOCK_REALTIME)                 24.1 ns
  clock_gettime(CLOCK_MONOTONIC_COARSE)          4.7 ns
  time(NULL)                                     1.4 ns
  gettimeofday                                  23.8 ns
  sched_getcpu                                   1.4 ns
  clock_gettime forced through syscall(2)      137.2 ns
```

- **`clock_gettime` costs about 24 ns for all of `CLOCK_MONOTONIC` (24.4 / 25.5 ns), `CLOCK_MONOTONIC_RAW` (24.7 / 25.7 ns) and `CLOCK_REALTIME` (24.1 / 25.5 ns), and `gettimeofday` the same.** Forcing the same clock through `syscall(2)` took 137.2 / 143.0 ns, about 5 to 6 times more. The 24 ns is the cost of reading the CPU's time-stamp counter, applying the kernel's scale factor and checking a sequence counter so that a concurrent update is detected (from memory; I did not decompose it).
- **`CLOCK_MONOTONIC_RAW` is accelerated on this kernel.** The old note's exercise 3 suggested it "is not vDSO accelerated on some kernels". Here it is, to the nanosecond. Whether yours is is a property of the kernel version, so measure it; do not assume.
- **`CLOCK_MONOTONIC_COARSE` costs 4.7 ns** and **`time` 1.4 ns**: these read a value the kernel updates once per timer tick, so they need no counter read and no scaling, at the price of a resolution of one tick (a few milliseconds, from memory).
- **`sched_getcpu` costs 1.4 ns.** On this glibc it does not even need the vDSO function: from memory, glibc 2.35 and later read the CPU number from a per-thread area shared with the kernel (restartable sequences), which is why it is as cheap as a memory read. I measured the cost, not the mechanism.

**Proof that these calls do not enter the kernel.** The program has a `mini` mode that makes 1,000 `clock_gettime` calls through the library, 1,000 `time` calls, 3 `getppid` calls and 1,000 `clock_gettime` calls forced through `syscall(2)`. `strace -c` counts what reaches the kernel:

```c
static void sect_mini(void)
{
    struct timespec ts;
    for (int i = 0; i < 1000; i++) clock_gettime(CLOCK_MONOTONIC, &ts);
    for (int i = 0; i < 1000; i++) sink = time(NULL);
    for (int i = 0; i < 3; i++) sink = syscall(SYS_getppid);
    for (int i = 0; i < 1000; i++) syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
}
```

```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
100.00    0.012589          12      1000           clock_gettime
  0.00    0.000000           0         3           getppid
```

The count of `clock_gettime` is **1,000**, not 2,000, and `time` does not appear at all: the 1,000 library calls and the 1,000 `time` calls were served by the vDSO and are invisible, and only the 1,000 forced ones were traced. The manual page says the same: "symbols (system calls) that are exported by the vDSO will not appear in the trace output", and they are equally invisible to `seccomp` filters (fetched). Two consequences. A profile that counts system calls will not see your hot timestamping, and, from memory, a sandbox policy cannot restrict these calls.

### 2.3 When the vDSO falls back

The fast path needs a clock the CPU can read from user mode. On this machine the clock source is `tsc`:

```text
== clocksource
tsc
```

When the kernel's clock source is one that user space cannot read (an old hypervisor clock, `hpet`, `acpi_pm`), `clock_gettime` quietly becomes a real system call, and a program that timestamps every packet loses its budget (from memory; the manual page I fetched does not describe this fallback). Check `current_clocksource` on a new platform before trusting a timestamp-heavy design, and do the `strace -c` test above.

**The classic question, answered with numbers:** "why is `clock_gettime` fast and `getpid` slow?" Because one is served from a page the kernel maps into the process (24 ns, or 1.4 ns for `time`), and the other is a real transition (93 to 99 ns).

---
## 3. Moving bytes and pointers across the boundary

### 3.1 Why the kernel cannot trust a pointer

A system call that takes a buffer receives an **address**, valid in the caller's address space, and the kernel is not allowed to believe it. The address may be unmapped, may point into the kernel's own half of the address space (a deliberate attack: "read my buffer" where the buffer is a kernel secret), may be mapped but read-only, or may be valid when checked and invalid a microsecond later. Touching a bad user address while running in the kernel must produce an error for the caller and not a crash of the system, so the kernel never dereferences a user pointer directly. It uses **accessors** that know how to fail (the declarations and rules below are from memory of the kernel source; the user-visible behaviour is measured):

```text
/* In kernel code. All of these can fault, and a fault can sleep (the page may
 * have to be read in), so none of them may be called from atomic context.
 * copy_from_user and copy_to_user return the number of bytes NOT copied. */
if (copy_from_user(&kbuf, user_ptr, len))
    return -EFAULT;
if (copy_to_user(user_ptr, &kbuf, len))
    return -EFAULT;
/* get_user / put_user move one scalar; strncpy_from_user moves a string and
 * bounds its length; access_ok(ptr, len) only checks that the range lies in
 * the user half, and does not check that the pages exist. */
```

The hardware helps. This CPU reports the flags `smep` and `smap` (the relevant flags from `/proc/cpuinfo`):

```text
== cpu flags
pcid ssbd ibrs stibp smep invpcid smap
```

That means **SMEP** stops the kernel executing code on a user page and **SMAP** stops it reading or writing user memory except where it has explicitly allowed it (ARM's equivalent is PAN; from memory). The accessors open that permission for the length of one copy and close it again, so a driver that dereferences a user pointer by mistake faults at once instead of silently working on one machine and failing on another.

### 3.2 What the caller sees

The program feeds the kernel bad pointers and reports what comes back:

```c
static void sect_efault(void)
{
    int z = open("/dev/zero", O_RDONLY), nul = open("/dev/null", O_WRONLY);
    errno = 0;
    ssize_t r = read(z, (void *)16, 10);
    printf("read into address 16:                       returned %zd, errno %s\n", r, errno == EFAULT ? "EFAULT" : strerror(errno));
    errno = 0;
    r = write(nul, (void *)16, 10);
    printf("write from address 16 to /dev/null:         returned %zd, errno %s\n", r, errno == EFAULT ? "EFAULT" : (errno ? strerror(errno) : "none"));
    errno = 0;
    r = write(nul, (void *)0xffff800000000000UL, 10);
    printf("write from a kernel-half address:           returned %zd, errno %s\n", r, errno == EFAULT ? "EFAULT" : (errno ? strerror(errno) : "none"));

    char *p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    munmap(p + 4096, 4096);
    errno = 0;
    r = read(z, p + 4096 - 100, 300);
    printf("read of 300 bytes, 100 mapped then a hole:  returned %zd, errno %s (a partial copy is a short count, not a failure)\n", r, errno ? strerror(errno) : "none");
    errno = 0;
    r = read(z, p + 4096, 300);
    printf("read of 300 bytes entirely in the hole:     returned %zd, errno %s\n", r, errno == EFAULT ? "EFAULT" : strerror(errno));

    int f = open("/tmp/m2_efault", O_RDWR | O_CREAT | O_TRUNC, 0600);
    unlink("/tmp/m2_efault");
    char *q = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(q, 'x', 4096);
    munmap(q + 4096, 4096);
    errno = 0;
    r = write(f, q + 4096 - 100, 300);
    off_t size = lseek(f, 0, SEEK_END);
    printf("write of 300 bytes, 100 mapped then a hole: returned %zd, file size %lld\n", r, (long long)size);
}
```

```text
read into address 16:                       returned -1, errno EFAULT
write from address 16 to /dev/null:         returned 10, errno none
write from a kernel-half address:           returned -1, errno EFAULT
read of 300 bytes, 100 mapped then a hole:  returned 100, errno none (a partial copy is a short count, not a failure)
read of 300 bytes entirely in the hole:     returned -1, errno EFAULT
write of 300 bytes, 100 mapped then a hole: returned 100, file size 100
```

Read the six lines as three rules.

1. **A bad pointer the kernel touches is `EFAULT`.** A `read` into address 16 failed with `EFAULT`, and so did a `write` from a kernel-half address, and a `read` into a hole in the mapping.
2. **A pointer the kernel never touches is not an error.** `write(/dev/null, (void *)16, 10)` **succeeded and returned 10**: the null device discards the data without reading it, so nothing ever dereferenced address 16. The check is a side effect of the copy, not a separate validation step. A consequence for testing: a driver's bad-pointer handling is only exercised by tests that make the driver read the buffer.
3. **A copy that fails partway is a short count, not an error.** A 300-byte `read` into a buffer with 100 mapped bytes before a hole returned **100**, and the 300-byte `write` from the same shape returned 100 and left a 100-byte file. The kernel copied what it could, stopped at the fault, and returned the progress. Only a fault at the very first byte gives `EFAULT`. Callers that ignore short counts, which are legal for every `read` and `write`, pass the bad-pointer tests and fail under unusual memory layouts.

### 3.3 What a copy costs

The same boundary carries the bytes, so the cost of a call has a fixed part (the crossing and the file layer) and a part that grows with the size. The test reads from `/dev/zero`, where the kernel writes zeros into the caller's buffer through its user-access routines, and compares with a `memcpy` of the same size in user space:

```c
static void sect_size(void)
{
    enum { B = 5 };
    size_t sizes[] = { 1, 64, 512, 4096, 65536, 1 << 20 };
    int z = open("/dev/zero", O_RDONLY);
    char *buf = aligned_alloc(4096, 1 << 20), *src = aligned_alloc(4096, 1 << 20);
    memset(buf, 1, 1 << 20); memset(src, 2, 1 << 20);
    printf("%-9s %12s %12s %12s\n", "bytes", "read ns", "memcpy ns", "read GB/s");
    for (int k = 0; k < 6; k++) {
        size_t n = sizes[k];
        long iters = n >= 65536 ? 20000 : 300000;
        double r[B], m[B];
        for (int b = 0; b < B; b++) {
            double t0 = now();
            for (long i = 0; i < iters; i++) IGN(read(z, buf, n));
            r[b] = (now() - t0) / iters * 1e9;
            t0 = now();
            for (long i = 0; i < iters; i++) { memcpy(buf, src, n); __asm__ volatile("" ::: "memory"); }
            m[b] = (now() - t0) / iters * 1e9;
        }
        qsort(r, B, sizeof r[0], cmp); qsort(m, B, sizeof m[0], cmp);
        printf("%-9zu %12.1f %12.1f %12.2f\n", n, r[B / 2], m[B / 2], n / r[B / 2]);
    }
    printf("(read from /dev/zero makes the kernel store zeros into the user buffer through its user-access routines; copy_to_user itself was not timed)\n");
}
```

```text
bytes          read ns    memcpy ns    read GB/s
1                131.1          1.7         0.01
64               129.5          1.4         0.49
512              138.1          2.8         3.71
4096             158.2         24.7        25.89
65536           1357.2       1428.1        48.29
1048576        22504.4      37550.7        46.59
(read from /dev/zero makes the kernel store zeros into the user buffer through its user-access routines; copy_to_user itself was not timed)
```

- **For small reads the time is the crossing.** 1 byte, 64 bytes and 512 bytes cost 131 to 138 ns, the same as the empty system call of section 1 plus the file layer. A `memcpy` of those sizes costs 1.4 to 2.8 ns, about 50 to 100 times less. A design that moves small items one call at a time is paying for the crossing, not for the bytes.
- **At 4 KiB the copy is a minor part.** 158 ns against the 130 ns floor: about 28 ns is the copy, 18 percent of the call.
- **Large transfers are bandwidth.** 64 KiB took 1.36 to 1.44 µs and 1 MiB took 22.5 µs: about **46 to 48 GB/s**, so for sizes of 64 KiB and above `time = 130 ns + bytes / 47 bytes per ns` matches the table (for 1 MiB: 22.3 µs plus 0.13 µs; for 64 KiB the model gives 1.52 µs against 1.36 to 1.44 measured). Below that the model is too pessimistic, because a 4 KiB buffer stays in the cache and the copy runs at roughly 150 bytes per ns. Past a few hundred kilobytes the copy runs at memory speed, and the crossing is noise.
- **The kernel's store is not faster or slower than yours** here: for 64 KiB the kernel's fill and the user `memcpy` took about the same time (1.36 and 1.43 µs), and the `memcpy` of 1 MiB was slower (37.6 µs) because it reads a second buffer as well.

This table tells you when to remove the copy and when not to. For messages of a few hundred bytes the crossing is the cost and the copy is noise, which is why the high-rate designs in section 4 remove per-message calls first and large copies second. The tools that remove the copy itself (`mmap` of a file, `sendfile`, `splice`, and `MSG_ZEROCOPY` for sockets) are from memory and not measured here; each trades the copy for page-table work or for pinning memory, and pays only for large transfers.

### 3.4 The double fetch

The rule of the old note, "validate after copying into kernel memory, never in place", has a mechanism worth showing, because it is the classic way a correct-looking check becomes an exploit. User memory can be changed by another thread **while the kernel is running on behalf of the first one**. If the kernel reads a length from user memory, checks it, and then reads it **again** to use it, the second read can return a different value:

```text
   thread A (inside the system call)           thread B (user space, same memory)
   len = *user_len        reads 16              ...
   if (len > LIMIT) fail  passes
                                                 *user_len = 1 MiB
   memcpy(kbuf, src, *user_len)   reads 1 MiB, writes far past the end of kbuf
```

The program reproduces the shape in user space (an analogue, not a kernel run): one thread keeps flipping a shared length between 16 and 1 MiB; the checker either reads it twice (check, then use) or reads it once into a local and works on the local:

```c
static void *flipper(void *arg)
{
    (void)arg;
    while (!atomic_load(&stop)) { atomic_store(&shared_len, 16); atomic_store(&shared_len, 1 << 20); }
    return NULL;
}

static void sect_double_fetch(void)
{
    enum { N = 5000000, LIMIT = 4096 };
    pthread_t t;
    cpu_set_t both;
    CPU_ZERO(&both); CPU_SET(0, &both); CPU_SET(1, &both);
    sched_setaffinity(0, sizeof both, &both);
    atomic_store(&shared_len, 16);
    pthread_create(&t, NULL, flipper, NULL);
    long bad_double = 0, bad_single = 0;
    for (long i = 0; i < N; i++) {
        if (atomic_load(&shared_len) <= LIMIT) {        /* check */
            long use = atomic_load(&shared_len);        /* second fetch: use */
            if (use > LIMIT) bad_double++;
        }
        long once = atomic_load(&shared_len);           /* one fetch into a local, check the local */
        if (once <= LIMIT) { long use = once; if (use > LIMIT) bad_single++; }
    }
    atomic_store(&stop, 1);
    pthread_join(t, NULL);
    printf("double fetch: %ld of %d checks passed and then used an illegal length (limit %d)\n", bad_double, N, LIMIT);
    printf("single fetch into a local, then check and use the local: %ld illegal uses\n", bad_single);
}
```

```text
double fetch: 3155 of 5000000 checks passed and then used an illegal length (limit 4096)
single fetch into a local, then check and use the local: 0 illegal uses
```

With two fetches, **3,155 of 5,000,000 checks** passed the test and then used an illegal length (1,907 in an earlier run, so the count is a property of the timing, not a constant); with one fetch into a local the count was **zero**, because the value that was checked is the value that is used. The kernel version of the one-fetch rule is: **copy the argument into kernel memory first, with one `copy_from_user`, and validate and use only the kernel copy.** Never validate the user buffer in place, and never pass a user pointer on to a second layer that will read it again.

The remaining rules from the old note stand, with their reasons:

1. **Never hold a spinlock across a `copy_*_user`.** The copy can fault, the fault can sleep to read a page, and sleeping with a spinlock held deadlocks or panics the kernel ([OS 06](06_locks.md)). Copy first, then take the lock, or copy after releasing it.
2. **Watch integer overflow in size arithmetic.** With 32-bit values, `offset + len` for `offset = 0xFFFFFFF0` and `len = 0x20` is `0x10`, which passes a check against a buffer size. Write the check as `len > size || offset > size - len`, or use the compiler's overflow-checked addition, and prefer 64-bit types for sizes.
3. **Design the user ABI so that the kernel copies once.** From memory, the usual practice for `ioctl` structures is a fixed-size header with a size or version field, zero-filled padding that the kernel checks, and pointers to variable parts that are themselves copied once.

---
## 4. Crossing less: the four-rung ladder

Every high-performance I/O technology is one of four ways to make fewer crossings per unit of work. They form a ladder, each rung removing more crossings and costing more in complexity and in what you give up. Two of the rungs are measured below, the third and fourth rest on the cost arithmetic and on memory, and the order of the section is the order of the interview answer.

### 4.1 Rung 1: batch

One crossing, many units of work: `readv` and `writev`, `sendmmsg` and `recvmmsg`, `epoll_wait` returning many events. The test first isolates the crossing, with 1-byte pieces written to `/dev/null` (which does no work with them), 1 to 64 pieces per call:

```c
static void sect_writev(void)
{
    enum { B = 7, TOTAL = 2000000 };
    int nul = open("/dev/null", O_WRONLY);
    char byte[64];
    memset(byte, 'x', sizeof byte);
    struct iovec iov[64];
    for (int i = 0; i < 64; i++) { iov[i].iov_base = byte + i; iov[i].iov_len = 1; }
    printf("1-byte pieces written to /dev/null, cost per piece (median of %d batches of %d pieces)\n", B, TOTAL);
    int sizes[] = { 1, 2, 4, 8, 16, 32, 64 };
    for (int k = 0; k < 7; k++) {
        int n = sizes[k];
        double r[B];
        for (int b = 0; b < B; b++) {
            double t0 = now();
            for (long i = 0; i < TOTAL / n; i++) {
                if (n == 1) IGN(write(nul, byte, 1));
                else IGN(writev(nul, iov, n));
            }
            r[b] = (now() - t0) / TOTAL * 1e9;
        }
        qsort(r, B, sizeof r[0], cmp);
        printf("  %2d per call (%s): %6.1f ns per piece\n", n, n == 1 ? "write " : "writev", r[B / 2]);
    }
}
```

```text
1-byte pieces written to /dev/null, cost per piece (median of 7 batches of 2000000 pieces)
   1 per call (write ):  114.2 ns per piece
   2 per call (writev):   74.0 ns per piece
   4 per call (writev):   34.4 ns per piece
   8 per call (writev):   17.5 ns per piece
  16 per call (writev):   10.8 ns per piece
  32 per call (writev):    6.8 ns per piece
  64 per call (writev):    4.7 ns per piece
```

The cost per piece falls from **114.2 ns to 4.7 ns**, a factor of 24. The numbers fit a simple model: one call costs about **115 ns plus about 2.9 ns per piece** (a 64-piece `writev` took 64 times 4.7 ns, about 300 ns, and 115 + 64 times 2.9 is 301; an 8-piece one took 8 times 17.5 = 140 ns against 115 + 23 = 138). The fixed term is the crossing (it matches section 1), and the per-piece term is the kernel walking one more `iovec` entry. This is the old note's arithmetic, now measured: with 64 pieces per call a roughly 100 ns crossing becomes under 2 ns per piece, and the rest is the work.

Now the same idea on a real protocol. The test sends a batch of UDP datagrams over the loopback interface and receives them, one call per packet and then one call per batch (`sendmmsg`, `recvmmsg`), for batches of 1, 4, 16 and 64:

```c
static void sect_udp(void)
{
    enum { B = 5, ROUNDS = 400, MAXB = 64, PAY = 64 };
    int rx = socket(AF_INET, SOCK_DGRAM, 0), tx = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (bind(rx, (struct sockaddr *)&a, sizeof a) < 0) { perror("bind"); exit(1); }
    socklen_t al = sizeof a;
    getsockname(rx, (struct sockaddr *)&a, &al);
    int big = 4 << 20;
    setsockopt(rx, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);
    if (connect(tx, (struct sockaddr *)&a, sizeof a) < 0) { perror("connect"); exit(1); }

    static char payload[MAXB][PAY], rbuf[MAXB][PAY];
    struct mmsghdr sm[MAXB], rm[MAXB];
    struct iovec siov[MAXB], riov[MAXB];
    for (int i = 0; i < MAXB; i++) {
        memset(payload[i], 'p', PAY);
        siov[i] = (struct iovec){ payload[i], PAY };
        sm[i].msg_hdr = (struct msghdr){ .msg_iov = &siov[i], .msg_iovlen = 1 };
        riov[i] = (struct iovec){ rbuf[i], PAY };
        rm[i].msg_hdr = (struct msghdr){ .msg_iov = &riov[i], .msg_iovlen = 1 };
    }
    int sizes[] = { 1, 4, 16, 64 };
    printf("UDP over loopback, %d-byte datagrams, send a batch then receive it (median of %d, %d rounds each)\n", PAY, B, ROUNDS);
    printf("%-6s %18s %18s %18s %18s\n", "batch", "sendto ns/pkt", "recvfrom ns/pkt", "sendmmsg ns/pkt", "recvmmsg ns/pkt");
    for (int k = 0; k < 4; k++) {
        int n = sizes[k];
        double s1[B], r1[B], s2[B], r2[B];
        for (int b = 0; b < B; b++) {
            double ts = 0, tr = 0, t0;
            for (int i = 0; i < ROUNDS; i++) {
                t0 = now();
                for (int j = 0; j < n; j++) IGN(send(tx, payload[j], PAY, 0));
                ts += now() - t0;
                t0 = now();
                for (int j = 0; j < n; j++) IGN(recv(rx, rbuf[j], PAY, 0));
                tr += now() - t0;
            }
            s1[b] = ts / (ROUNDS * n) * 1e9; r1[b] = tr / (ROUNDS * n) * 1e9;
            ts = tr = 0;
            for (int i = 0; i < ROUNDS; i++) {
                t0 = now();
                int sent = sendmmsg(tx, sm, n, 0);
                ts += now() - t0;
                t0 = now();
                int got = recvmmsg(rx, rm, n, MSG_DONTWAIT, NULL);
                tr += now() - t0;
                if (sent != n || got != n) { printf("short batch: sent %d got %d of %d\n", sent, got, n); exit(1); }
            }
            s2[b] = ts / (ROUNDS * n) * 1e9; r2[b] = tr / (ROUNDS * n) * 1e9;
        }
        qsort(s1, B, sizeof(double), cmp); qsort(r1, B, sizeof(double), cmp);
        qsort(s2, B, sizeof(double), cmp); qsort(r2, B, sizeof(double), cmp);
        printf("%-6d %18.1f %18.1f %18.1f %18.1f\n", n, s1[B / 2], r1[B / 2], s2[B / 2], r2[B / 2]);
    }
}
```

```text
UDP over loopback, 64-byte datagrams, send a batch then receive it (median of 5, 400 rounds each)
batch       sendto ns/pkt    recvfrom ns/pkt    sendmmsg ns/pkt    recvmmsg ns/pkt
1                   606.6              243.0              715.6              332.9
4                   792.9              251.8              622.9              234.1
16                  613.0              213.2              580.6              206.5
64                  664.8              236.7              626.0              217.4
```

**Batching barely helped.** On the receive side the cost per packet fell from about 243 ns (`recvfrom`, batch 1) to 207 to 217 ns (`recvmmsg`, batches 16 and 64), roughly 30 ns or 12 percent. On the send side the numbers are noisy and show no gain (581 to 793 ns per packet, with `sendmmsg` at batch 64 about the same as `send`). And `recvmmsg` with a batch of one (333 ns) was **slower** than a plain `recvfrom`, because the multi-message call has setup of its own. The lesson is the other half of the batching rule: **batching amortises the crossing, and only the crossing.** Each UDP packet costs a fixed amount of kernel work (building a socket buffer, routing, the UDP and IP layers, delivery to the socket queue), which here is about 210 to 240 ns on receive and about 600 to 800 ns on send, because on loopback the sender also does the delivery. The 100 ns crossing is a small share of it. A pure crossing, as in the `writev` test, is cheap to amortise; a call that does substantial work per item is not. At about 215 ns per received packet the best this core can do on UDP is about 4.6 million packets a second, and the 10 Mpps target of section 1.4 needs a different rung.

### 4.2 Rung 2: share memory, cross once at setup

`mmap` a region that both sides use, and move the data with loads and stores. This is the ring of [OS 17](17_ipc_fundamentals.md) and [OS 18](18_ipc_mechanisms.md), and the numbers measured there are the numbers of this rung: a flag round trip at 193 ns, a 64-byte ring at 18.4 million messages a second (54 ns each) and a ring with a sleeping consumer at about 4 million a second in the lockstep regime, against 2.4 GB/s for a pipe at 4 KiB. After setup there is no crossing, and the cost moves to **cache-coherence traffic** between the two cores, which is why the ring's design (separate cache lines, batched index updates) is where the work goes.

The kernel offers this rung in several places, from memory and not run here: `mmap`ed packet rings on `AF_PACKET` (TPACKET), the user-memory rings of `AF_XDP`, the `perf` event ring buffers, and `io_uring` (section 5); and the same pattern appears in the DMA descriptor ring of a character device, such as the driver in the study plan's own project. The price is **coupling to a layout**: the ring format becomes part of the interface between the kernel and the program, and changing it breaks users.

### 4.3 Rung 3: move the work into the kernel

If the decision "what to do with this packet" needs only the packet and a little state, run the decision where the packet already is, and never copy it out. This is what **eBPF** and **XDP** do: a small program, checked by an in-kernel verifier, is attached to a hook (the network driver's receive path, a trace point) and runs there. Dropping, redirecting or counting packets at millions per second needs no crossing at all. This rung is from memory and I did not run it, so I state its trade as the one I trust from the literature: it gives up expressiveness (the verifier rejects unbounded loops and unchecked memory access) and portability across kernel versions, and in return keeps the whole stack and the normal tools.

### 4.4 Rung 4: take the device out of the kernel

DPDK and similar frameworks unbind the network card from its kernel driver, map its registers and DMA rings into user space, and **poll** them in a busy loop on a dedicated core. There are no system calls, no interrupts and no copies, and also no kernel networking stack (so no `tcpdump` on that interface and no `iptables` rules on it), and a core at 100 percent regardless of load (from memory; I did not run it). It is the right rung for a forwarding appliance at line rate, and a very expensive way to solve a problem that batching would solve.

### 4.5 The ladder, with what was measured

| Rung | Crossings per item | What this machine measured | What you give up |
| --- | --- | --- | --- |
| naive call per item | 1 | 97 to 106 ns crossing; `read` 130 ns; UDP `recvfrom` about 240 ns | nothing |
| batch | 1/N | `writev`: 114.2 to 4.7 ns per piece at 64; UDP receive: about 12 percent better | some latency (you wait for N); per-item work is unchanged |
| shared ring | about 0 | 54 ns per 64-byte message (OS 17), about 4 M msgs/s with sleeping (OS 18) | a layout shared with the other side; the wake-up problem of OS 18 |
| eBPF and XDP | 0 | not run | expressiveness (the verifier) and version portability |
| kernel bypass | 0 | not run | the whole kernel stack, and a core at 100 percent |

**The interview move** is to put this ladder on the board, say which rung the stated rate needs, and say what that rung costs. "10 Mpps on one core" is 100 ns a packet, and the table shows that rung 1 cannot reach it on UDP (the per-packet work alone is above it), rung 2 can for a ring of small records if the other end is user space, and rungs 3 and 4 are for the case where the packets must come out of the NIC at that rate. Reaching for kernel bypass when batching would do is the architecture mistake the question is testing for, and so is the opposite one, batching a workload whose per-item work dominates.

---

## 5. `io_uring`: completion queues instead of readiness

### 5.1 The idea

`epoll` tells you a descriptor is **ready**, and you then make a system call to do the work: two crossings per operation at best. `io_uring` gives you two shared rings, a **submission queue** (SQ) that user space writes and the kernel reads, and a **completion queue** (CQ) that the kernel writes and user space reads. You queue many operations, make one `io_uring_enter` to hand them over, and read the results from the CQ. The manual page puts it as: the ring buffers "are shared between user space and kernel space", and queueing several requests followed by one call to `io_uring_enter` is how it saves system calls (fetched). There is no `glibc` wrapper (the page's own example defines the calls itself), so the program below uses raw `syscall(2)` and no `liburing`.

A detail of the layout that the page states and the code follows: the submission ring holds **indices into a separate array** of submission entries (SQEs), so the program writes the entry at `sqes[idx]` and then stores `idx` into the `array` slot (fetched: "the submission side ring buffer is an index into this array, which in turn contains the index into the SQEs"). The memory ordering is the one of [OS 08](08_memory_barriers.md): the user side publishes the SQ tail with a **release** store and reads the CQ tail with an **acquire** load, which the page's example also uses (fetched).

```c
static int ur_open(struct ur *u, unsigned entries, unsigned flags, int sq_cpu)
{
    struct io_uring_params p;
    memset(&p, 0, sizeof p);
    p.flags = flags;
    p.sq_thread_cpu = sq_cpu;
    p.sq_thread_idle = 5000;
    int fd = (int)syscall(__NR_io_uring_setup, entries, &p);
    if (fd < 0) return -1;
    size_t sqsz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    size_t cqsz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    size_t sz = sqsz > cqsz ? sqsz : cqsz;
    char *ring = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
    u->sqes = mmap(NULL, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
    if (ring == MAP_FAILED || u->sqes == MAP_FAILED) return -1;
    u->fd = fd;
    u->sq_head = (unsigned *)(ring + p.sq_off.head);
    u->sq_tail = (unsigned *)(ring + p.sq_off.tail);
    u->sq_mask = (unsigned *)(ring + p.sq_off.ring_mask);
    u->sq_flags = (unsigned *)(ring + p.sq_off.flags);
    u->sq_array = (unsigned *)(ring + p.sq_off.array);
    u->cq_head = (unsigned *)(ring + p.cq_off.head);
    u->cq_tail = (unsigned *)(ring + p.cq_off.tail);
    u->cq_mask = (unsigned *)(ring + p.cq_off.ring_mask);
    u->cqes = (struct io_uring_cqe *)(ring + p.cq_off.cqes);
    u->enters = 0;
    return 0;
}

static void ur_queue(struct ur *u, int opcode, int fd, void *addr, unsigned len)
{
    unsigned tail = *u->sq_tail, idx = tail & *u->sq_mask;
    struct io_uring_sqe *s = &u->sqes[idx];
    memset(s, 0, sizeof *s);
    s->opcode = opcode;
    s->fd = fd;
    s->addr = (unsigned long)addr;
    s->len = len;
    u->sq_array[idx] = idx;
    __atomic_store_n(u->sq_tail, tail + 1, __ATOMIC_RELEASE);
}

static double ur_run(struct ur *u, int opcode, int fd, char *buf, unsigned len, int k, long n, int sqpoll)
{
    double t0 = now();
    long done = 0;
    while (done < n) {
        for (int i = 0; i < k; i++) ur_queue(u, opcode, fd, buf, len);
        if (!sqpoll) {
            sys_enter(u->fd, k, k, IORING_ENTER_GETEVENTS);
            u->enters++;
        } else if (__atomic_load_n(u->sq_flags, __ATOMIC_ACQUIRE) & IORING_SQ_NEED_WAKEUP) {
            sys_enter(u->fd, 0, 0, IORING_ENTER_SQ_WAKEUP);
            u->enters++;
        }
        unsigned got = 0;
        while (got < (unsigned)k) {
            unsigned h = *u->cq_head, t = __atomic_load_n(u->cq_tail, __ATOMIC_ACQUIRE);
            while (h != t) { h++; got++; }
            __atomic_store_n(u->cq_head, h, __ATOMIC_RELEASE);
        }
        done += k;
    }
    return (now() - t0) / n * 1e9;
}
```

### 5.2 What it costs

The program queues batches of NOP operations (the machinery with no work in it) and 4096-byte reads from `/dev/zero`, and compares one `io_uring_enter` per batch with a ring in `SQPOLL` mode, where a kernel thread polls the SQ so that user space makes no `io_uring_enter` call at all (the page: "there is no need for you to call `io_uring_enter(2)`", fetched):

```text
NOP operations (the cost of the machinery with no work in it)
  one io_uring_enter per batch of  1:          176.7 ns per op
  one io_uring_enter per batch of  8:           39.6 ns per op
  one io_uring_enter per batch of 64:           23.9 ns per op
  SQPOLL (kernel thread on CPU 1), batch  1:   832.1 ns per op, io_uring_enter calls 0 in 3020000 ops
  SQPOLL (kernel thread on CPU 1), batch  8:   178.8 ns per op, io_uring_enter calls 0 in 3020000 ops
  SQPOLL (kernel thread on CPU 1), batch 64:    67.4 ns per op, io_uring_enter calls 0 in 3020000 ops

READ of 4096 bytes from /dev/zero
  one io_uring_enter per batch of  1:          283.1 ns per op
  one io_uring_enter per batch of  8:          139.5 ns per op
  one io_uring_enter per batch of 64:          117.1 ns per op
  SQPOLL (kernel thread on CPU 1), batch  1:  1590.9 ns per op, io_uring_enter calls 0 in 3020000 ops
  SQPOLL (kernel thread on CPU 1), batch  8:   283.6 ns per op, io_uring_enter calls 0 in 3020000 ops
  SQPOLL (kernel thread on CPU 1), batch 64:   169.2 ns per op, io_uring_enter calls 0 in 3020000 ops
  for comparison, read(2) of 4096 bytes:       167.0 ns per op
```

- **One operation per `io_uring_enter` is no bargain.** A NOP costs 176.7 ns, more than the bare system call of section 1 (97 to 106 ns), and a 4 KiB `read` through the ring at batch 1 costs 283 ns against 167 ns for `read(2)`. The ring adds work (writing an SQE, publishing a tail, reading a CQE) on top of the same crossing.
- **Batching is where it pays.** At 8 per call a NOP costs 39.6 ns and at 64 it costs 23.9 ns, a factor of 7 down from batch 1. A 4 KiB read at 64 per call costs 117.1 ns, about 30 percent less than the 167 ns of `read(2)`. (The same batching idea as rung 1, with the work, here a copy of 4 KiB, still counted.)
- **`SQPOLL` removed every system call and was slower.** The counts say it: **0 `io_uring_enter` calls in 3,020,000 operations**. But the cost per operation was 832 ns at batch 1 and 67.4 ns at batch 64, against 176.7 and 23.9 for the plain ring. The reason is in how the test waits: it submits a batch and then waits for its completions, so every batch is a **round trip to the poller thread on the other CPU** (the SQ tail line moves to the poller, the poller runs the operations, and the CQ tail line moves back), which costs hundreds of nanoseconds of cache-line transfers in this VM. `SQPOLL` helps a submitter that keeps many operations in flight and never waits, which this test does not do, so **I measured its latency per batch, not its throughput ceiling**, and this result must not be read as "SQPOLL is slow". It also keeps a kernel thread spinning for `sq_thread_idle` milliseconds after the last submission (the test used 5,000), which on a 2-vCPU machine is half the machine. Without `CAP_SYS_NICE`-level rights it is also restricted on older kernels (the setup page: privileged before 5.11, allowed with `CAP_SYS_NICE` from 5.11, relaxed further in 5.13; fetched).

Three limits of the measurement: it covers NOP and a read from `/dev/zero`, **not storage or sockets**, where `io_uring`'s larger contribution lies (from memory: asynchronous file I/O that `epoll` never handled for regular files, and linked operations); it uses one thread; and the kernel's `io_uring_disabled` setting reads 0 here, meaning the interface is enabled (`/proc/sys/kernel/io_uring_disabled`), whereas some hardened environments turn it off (from memory, because of the attack surface).

### 5.3 `epoll` or `io_uring`

| | `epoll` | `io_uring` |
| --- | --- | --- |
| model | readiness: "you can read now" | completion: "the read is done, here are the bytes" |
| crossings per operation | at least 2 (wait, then the call) | about 1/N with batching, 0 with `SQPOLL` |
| regular files | not useful (always "ready") | supported (from memory) |
| measured here | not measured | 24 ns per NOP at batch 64; 117 ns per 4 KiB read against 167 ns |
| cost of adopting | low | a new ring-handling discipline; a larger kernel attack surface |

The old note's last exercise, an `io_uring` echo server against an `epoll` one at 1, 10 and 100 connections, is the right experiment and I have not run it; the crossover is where batching starts to fill, so expect it near the connection count at which several operations are ready per wake-up.

---

## 6. Measuring the boundary on a machine you do not own

What worked here, in the order I would do it on a new system:

1. **Record the platform with the number.** The mitigation list, the clock source and the CPU line (section 1.3 and 2.3) change the answer. A cost without them is not comparable.
2. **A micro-benchmark with discipline** (the program of section 1): pin to one CPU, warm up, run batches of a million calls, take the median of several batches, and keep the result in a `volatile` so the compiler cannot delete the loop. Time the **empty** call (`getppid` through `syscall(2)`; `getpid` is also uncached on this glibc), and then the call you care about, and subtract in your head.
3. **Count with `strace -c -f`** to see how many crossings a program makes and of what kind, which found the 1,000 forced `clock_gettime` calls and showed the vDSO ones absent (section 2.2). **Never time under `strace`**: the same 1,000 forced calls that cost 137 ns each untraced show as 12 µs per call in the `strace` summary, about 90 times more, because every call is stopped twice by the tracer.
4. **`getrusage` and `time`** for the user and system split, with the caution of section 1.2 about tick-based accounting.
5. **`perf`** (not installed here, so from memory): `perf stat` for the user and kernel split of a whole run, `perf trace -s` for a summary of system calls with their times and `perf bench syscall basic` for a ready-made `getppid` loop, and `perf record -g` with `perf report --sort=dso` to see where kernel time goes. Use these on your own machine, and compare their `getppid` figure with your own loop's.

---
## 7. A design review in numbers

The pieces combine into one calculation, and it is the calculation to practise out loud. Take a service that must process **10 million 64-byte packets a second on one core**, with 40 ns of its own work per packet. The budget is 1 s / 10 M = **100 ns per packet**, so the I/O path may use at most 60 ns. Each way of getting a packet to the program, with the measured cost of its I/O path:

| Design | I/O path per packet | Plus 40 ns of work | Fits 100 ns? |
| --- | --- | --- | --- |
| one `recvfrom` per packet | about 243 ns (section 4.1) | 283 ns | no: 3.5 Mpps at best |
| `recvmmsg`, 64 per call | about 207 to 217 ns | about 250 ns | no: the per-packet stack work is above the budget |
| one bare crossing per packet (any call) | 97 to 106 ns | about 140 ns | no, before doing any real kernel work |
| a ring in shared memory, a batched design like the 64-byte ring of OS 17 | about 54 ns | about 94 ns | yes, with 6 ns to spare |
| the same ring in the lockstep regime of OS 18 | about 250 ns (4 M msgs/s) | about 290 ns | no: the ring needs batched index updates |
| XDP or kernel bypass | not measured here | not measured | by design, the usual answer for a NIC at line rate |

Two more terms commonly forgotten:

- **Timestamping every packet** with `clock_gettime(CLOCK_MONOTONIC)` costs about 24 ns (section 2), which makes the shared-ring design 94 + 24 = 118 ns and **no longer fits**. Take one timestamp per batch of 32 packets (under 1 ns per packet), or use the coarse clock at 4.7 ns if tick resolution is enough.
- **A silent fallback** matters as much: if the clock source is not readable from user mode, that timestamp becomes a real system call at about 137 ns, and the design collapses to about 230 ns, 4.3 Mpps. This is the reason to run `strace -c` on the real system and see that the time calls are absent (section 2.2).

The structure of the argument is what an interviewer is listening for: state the budget, price each term from a measurement, and say which rung of the ladder the total forces. A remembered range ("50 to 400 ns") cannot do that.

---

## What it costs

| Operation | Cost (measured here, unless marked) | Why |
| --- | --- | --- |
| ordinary function call | 0.7 to 1.0 ns | a call and a return |
| `getppid` through `syscall(2)` / `getpid` | 96.6 / 106.3 / 93.1 / 99.0 ns | the crossing itself (two runs; no KPTI on this CPU) |
| `read` 1 byte from `/dev/zero` / `write` 1 byte to `/dev/null` | 130.2 / 130.7 / 108.5 / 107.5 ns | crossing plus the file layer |
| `clock_gettime` (monotonic, raw, real) / `gettimeofday` | about 24 ns | vDSO: counter read, scale, sequence check |
| `clock_gettime` coarse / `time` / `sched_getcpu` | 4.7 / 1.4 / 1.4 ns | a read of a value the kernel keeps |
| `clock_gettime` forced through `syscall(2)` | 137.2 / 143.0 ns | a real crossing |
| `read` of 512 B / 4 KiB / 64 KiB / 1 MiB | 138 / 158 / 1,357 / 22,504 ns (second run) | fixed 130 ns, then memory bandwidth at about 47 GB/s |
| bad user pointer the kernel touches / never touches | `EFAULT` / success | the check is a side effect of the copy |
| `writev`, 1 piece per call / 64 pieces | 114.2 / 4.7 ns per 1-byte piece | about 115 ns per call plus 2.9 ns per piece |
| UDP loopback `recvfrom` / `recvmmsg` (64) | about 243 / 217 ns per packet | per-packet stack work dominates the crossing |
| `io_uring` NOP, one `enter` per op / per 64 ops | 176.7 / 23.9 ns | one crossing per batch plus the ring's own work |
| `io_uring` 4 KiB read, batch 1 / batch 64 / `read(2)` | 283 / 117 / 167 ns | the same, plus a 4 KiB copy |
| `io_uring` with `SQPOLL`, `enter` calls in 3.02 M operations | 0, at 832 ns per op (batch 1) | a round trip to a polling thread on another CPU |
| double fetch of a user length, 5 M checks | 3,155 unsafe uses; 0 with a single fetch | the value changes between the check and the use |
| `strace` per traced call (the forced `clock_gettime`) | about 12 µs | the tracer stops the process twice |

---

## Common mistakes

1. **Quoting a syscall cost without the platform.** The cost depends on whether the CPU needs page-table switching and which Spectre mitigations apply. Quote the number with the mitigation list and the clock source.
2. **Assuming `getpid` is cached.** On this glibc it costs a full crossing (93 to 99 ns). The classic contrast is now `clock_gettime`.
3. **Assuming every `clock_gettime` is a system call, or none is.** The monotonic, real and raw clocks were served by the vDSO here; forcing the call, or a clock source that user space cannot read, makes it a crossing. Check with `strace -c`.
4. **Timing under `strace`.** It adds about 12 µs per call. Count with it; time without it.
5. **Reading the user and system split of `getrusage` as exact.** A loop of 5 million `getppid` calls was accounted 43 percent to user time. Use it for large differences only.
6. **Expecting a bad pointer to always fail.** The kernel reports `EFAULT` only if it touches the page; a write to `/dev/null` from address 16 succeeded. Tests of driver error paths must make the driver read the buffer.
7. **Ignoring short counts.** A fault midway gives a count, not `EFAULT`. Handle partial `read` and `write`.
8. **Validating a user buffer in place.** Another thread can change it between the check and the use; 2,000 to 3,200 unsafe uses in 5 million checks appeared in the analogue. Copy once into kernel memory, then validate the copy.
9. **Holding a spinlock across a user copy.** It can fault and sleep.
10. **Size arithmetic that wraps.** `offset + len` in 32 bits can pass a bounds check; test `len > size || offset > size - len`.
11. **Batching a call whose cost is per item.** `recvmmsg` on UDP improved the packet cost by about 12 percent because the stack work per packet dominates; `writev` of tiny pieces improved it 24 times because there was no per-piece work. Price the per-item work first.
12. **Batching a batch of one.** `recvmmsg` with one message was slower than `recvfrom`; `io_uring` with one operation per `enter` was slower than the bare call.
13. **Concluding from one `SQPOLL` number.** The test that waits per batch measures latency to a polling thread, not throughput; 832 ns per operation at batch 1 does not say the interface is slow.
14. **Jumping to kernel bypass.** If batching or a shared ring meets the budget, they keep the tools, the firewall and the debugging that bypass gives up.
15. **Believing a remembered range.** The old note's "150 to 400 ns with mitigations" does not apply to this CPU (97 to 106 ns). Measure your machine.

---

## Questions out loud

Answer each in two minutes, with a number or a diagram, before looking back (the section in brackets).

1. Walk through what happens between a `syscall` instruction and the return to user mode. Which register carries the call number and which the fourth argument? [1.1]
2. How long does a system call take, and how do you measure it honestly on a machine you do not own? What must you record with the number? [1.2, 1.3, 6]
3. What do KPTI and the Spectre mitigations add to that cost, and how did you check whether this CPU needs them? [1.3]
4. At 10 Mpps, what is the per-packet budget, and what does one system call per packet do to it? [1.4, 7]
5. What is the vDSO? Which calls does it serve, and how can you prove from outside that a call does not enter the kernel? [2.1, 2.2]
6. Why is `clock_gettime` cheap and `getpid` not? When does `clock_gettime` stop being cheap? [2.2, 2.3]
7. What does `copy_from_user` protect against? What do SMAP and SMEP add? [3.1]
8. A `read` is given a buffer that is half mapped. What does it return? What if the first byte is unmapped, and what if the kernel never touches the buffer? [3.2]
9. What is a double fetch? State the rule that prevents it and why a check on the user's copy in place is wrong. [3.4]
10. A read of 512 bytes and a read of 1 MiB: where does the time go in each? [3.3]
11. Draw the four-rung ladder. Which rung does a 1 Mpps UDP service need? A 10 Mpps one? What does each give up? [4]
12. Why did `writev` improve 24 times and UDP `recvmmsg` only about 12 percent? [4.1]
13. What does `io_uring` change compared to `epoll`? When is it slower than a plain system call? What does `SQPOLL` trade? [5]
14. You are told a service "spends 30 percent of its time in the kernel". What do you measure next, and with which tools? [6]

---

## Problems

**Where to solve them.** On your own machine, with your own numbers: write the `getppid` loop of section 1 and compare it with `perf bench syscall basic` and `perf trace -s`, compile the programs described above, and use `strace -c -f` to count crossings and `cat /sys/devices/system/cpu/vulnerabilities/*` and `cat /sys/devices/system/clocksource/clocksource0/current_clocksource` to record the platform. For practice problems of this kind: the `man7.org` pages `vdso(7)`, `syscall(2)`, `io_uring(7)` and `io_uring_setup(2)`, the kernel documentation of the user-access functions and of the speculation mitigations, and the `liburing` examples (from memory), then reproduce the tables here and compare.

**How to approach them.** Write the budget first (`1 s / rate`), then price each term from a measurement or an explicitly stated assumption, then add and compare. For a pointer question, ask whether the kernel **touches** the memory and at which byte it first faults. For a batching question, split the cost into a per-call and a per-item part.

### Problem A (E, entry level): which calls cross?

For each call, say whether it enters the kernel on this machine and about how long it takes: `getppid()` through `syscall(2)`, `time(NULL)`, `clock_gettime(CLOCK_MONOTONIC)`, `clock_gettime(CLOCK_MONOTONIC)` through `syscall(SYS_clock_gettime, ...)`, `sched_getcpu()`, `getpid()`, `read` of 1 byte from `/dev/zero`. How would you confirm each answer without a timer?

### Problem B (E, entry level): what comes back

A program calls `read` on `/dev/zero` into each of these buffers, and `write` to `/dev/null` from the last one. What is returned each time? (a) 300 bytes, where the first 100 are mapped and the next 200 are unmapped; (b) 300 bytes, all unmapped; (c) 300 bytes at a kernel-half address; (d) `write` of 10 bytes from address 16 to `/dev/null`.

### Problem C (M, medium): the batching model

A call costs 115 ns plus 2.9 ns per piece. (a) What is the cost per piece for 1, 4, 16 and 64 pieces per call? (b) What is the smallest batch that brings the cost per piece under 10 ns? (c) What piece rate does a single core reach with 1 piece per call, and with 64? (d) Receiving a UDP packet costs 243 ns with one call per packet, of which about 100 ns is the crossing: what is the best speedup if batching removed the crossing completely, and what did `recvmmsg` at 64 per call achieve (207 to 217 ns)?

### Problem D (M, medium): reading a file in pieces

Use the measured costs of a `read` of 4 KiB (158 ns) and of 1 MiB (22,504 ns) from section 3.3. (a) How long do 256 reads of 4 KiB take, against one read of 1 MiB, and how many times slower is the first? (b) What fraction of each 4 KiB read, and of the 1 MiB read, is the 130 ns fixed cost? (c) Using `time = 130 ns + bytes / 47 per ns` for large sizes, at what size does the copy cost as much as the fixed part, and is the model valid there? (d) What does this say about reading a 1 GiB file with a 4 KiB buffer?

### Problem E (H, hard): the 10 Mpps design

A single core must handle 10 million 64-byte packets a second, with 40 ns of processing per packet, and every packet must be timestamped. (a) What is the per-packet budget and the I/O allowance after the processing? (b) For each of these, say whether it fits: one `recvfrom` per packet (243 ns); `recvmmsg` with a batch of 64 (212 ns); a batched shared ring (54 ns). (c) Add a `clock_gettime(CLOCK_MONOTONIC)` per packet (24 ns) to the shared-ring design: does it fit, and how would you fix it? (d) The platform turns out to have a clock source that user space cannot read, so each `clock_gettime` costs 137 ns: what is the rate now, with the timestamp per packet and with one per batch of 32? (e) Which rung of the ladder does the design need, and what does it give up compared with the one below?

---

## Answers

**A.** `getppid()` through `syscall(2)`: **crosses**, 97 to 106 ns. `time(NULL)`: does **not** cross, 1.4 ns (vDSO). `clock_gettime(CLOCK_MONOTONIC)`: does **not** cross, about 24 ns (vDSO). The same through `syscall(SYS_clock_gettime, ...)`: **crosses**, about 137 ns. `sched_getcpu()`: does **not** cross, 1.4 ns (from memory, a read of a per-thread area on this glibc). `getpid()`: **crosses**, 93 to 99 ns (glibc 2.39 does not cache it). `read` of 1 byte from `/dev/zero`: **crosses**, 130 ns. Without a timer: `strace -c -f` lists the calls that reach the kernel, and the vDSO ones are absent (section 2.2); the program's `mini` mode made 1,000 library `clock_gettime` calls and 1,000 forced ones and `strace` counted 1,000.

**B.** (a) **100**: the kernel copies up to the first bad byte and returns the progress (section 3.2: a 300-byte read with 100 mapped bytes returned 100). (b) **-1 with `EFAULT`**: the first byte faults. (c) **-1 with `EFAULT`**. (d) **10, success**: `/dev/null` discards the data without reading it, so address 16 is never touched.

**C.** (a) (115 + 2.9 n) / n: n = 1: **117.9** ns; n = 4: **31.7**; n = 16: **10.1**; n = 64: **4.7** ns (measured 114.2, 34.4, 10.8, 4.7). (b) 115 / n + 2.9 < 10 gives n > 16.2, so **n = 17** (at 17: 9.66 ns; at 16 it is 10.09, just over). (c) 1 piece per call: 1 / 117.9 ns = about **8.5 million pieces a second**; 64 per call: 1 / 4.7 ns = about **213 million a second**. (d) Removing a 100 ns crossing from 243 ns leaves 143 ns, a best speedup of 243 / 143 = **1.7 times**. `recvmmsg` at 64 per call reached 207 to 217 ns, 1.12 to 1.17 times, about a third of the possible gain, because the call has setup of its own and the per-packet work is untouched.

**D.** (a) 256 reads times 158 ns = **40.4 µs**, against **22.5 µs** for one read of 1 MiB: the pieces are about **1.8 times slower**. (b) For 4 KiB: 130 / 158 = **82 percent** is the fixed cost. For 1 MiB: 130 / 22,504 = **0.6 percent**. (c) The copy equals the fixed cost where bytes / 47 = 130, which is **6,110 bytes**, about 6 KB. The model was fitted to sizes of 64 KiB and above, so at 6 KB it is not valid: the measured 4 KiB copy took only 28 ns, much less than 4,096 / 47 = 87 ns, because the buffer stays in cache. The crossover is therefore higher than 6 KB, and the exact size depends on the cache. (d) A 1 GiB file with a 4 KiB buffer is 262,144 reads at about 158 ns, **41 ms** of which 34 ms is the fixed cost (82 percent), whereas a 1 MiB buffer needs 1,024 reads at 22.5 µs, **23 ms**. Use buffers of tens of kilobytes or more for streaming, or `mmap`.

**E.** (a) 1 s / 10 M = **100 ns per packet**; after the 40 ns of processing, **60 ns** for I/O. (b) `recvfrom` at 243 ns: **no** (283 ns total, 3.5 Mpps). `recvmmsg` at 212 ns: **no** (252 ns, 4 Mpps), because the per-packet stack work alone exceeds the budget. The batched shared ring at 54 ns: **yes**, 94 ns total with 6 ns to spare. (c) 94 + 24 = **118 ns**: it **does not fit** (8.5 Mpps). Fix: take one timestamp per batch of 32 (24 / 32 = 0.75 ns per packet, giving 94.75 ns), or use the coarse clock at 4.7 ns if tick resolution suffices (98.7 ns, barely), and check that no per-packet system call remains. (d) With a 137 ns timestamp per packet: 94 + 137 = **231 ns**, about **4.3 Mpps**. With one per batch of 32: 137 / 32 = 4.3 ns per packet, 98.3 ns, which **just fits** (about 10.2 Mpps). Also run `strace -c` to catch the fallback, and check `current_clocksource`. (e) The shared ring (rung 2), fed by something that already puts packets in memory. Compared with the rung below it (batching system calls) it gives up the simple interface: both sides share a layout, you must solve the sleep and wake problem of OS 18, and the kernel's network stack and its tools are not in the path unless something else (XDP, `AF_XDP`, a driver) writes the ring.

---

## Cheat sheet

- **The crossing**: `syscall` instruction, number in `rax`, arguments `rdi rsi rdx r10 r8 r9`, entry code, handler, `sysret`. About **97 to 106 ns** here (no KPTI), about 200 cycles; a function call is under 1 ns. Always quote the mitigation list and the clock source with it.
- **A call with work costs crossing plus work**: `read` 1 byte 130 ns, `write` to `/dev/null` 108 ns.
- **vDSO** (x86-64: `clock_gettime`, `gettimeofday`, `time`, `getcpu`): user-mode read of a kernel page; `clock_gettime` about 24 ns (monotonic, raw and real), coarse 4.7 ns, `time` 1.4 ns, `sched_getcpu` 1.4 ns; forced through `syscall` 137 ns; invisible to `strace` and `seccomp`; needs a readable clock source (`tsc` here), else it falls back to a real call.
- **User pointers**: use `copy_from_user`, `copy_to_user`; they return bytes not copied; `EFAULT` only if the kernel touches a bad byte; a fault midway gives a **short count**; SMEP and SMAP; no copy under a spinlock; check size arithmetic for wrap.
- **Double fetch**: copy once into kernel memory, validate and use the copy; reading twice lost the race 1,907 to 3,155 times in 5 million tries here.
- **Copy cost**: 130 ns fixed plus about 47 B/ns for large sizes (46 to 48 GB/s); small copies are crossing only; 4 KiB is 82 percent crossing.
- **Ladder**: batch (`writev` 114 to 4.7 ns per piece; UDP only about 12 percent), share memory (about 54 ns per message, OS 17), move work into the kernel (eBPF/XDP, not run), bypass (DPDK, not run). Batching amortises only the crossing.
- **`io_uring`**: SQ and CQ shared rings, raw syscalls, SQ tail release and CQ tail acquire; 176.7 ns per op at 1 per `enter`, 23.9 at 64; 4 KiB read 283 / 117 against 167 ns; `SQPOLL` makes zero `enter` calls but measured 832 ns per op at batch 1 (latency to the poller, not a throughput ceiling).
- **Measure**: pinned `getppid` loop with a `volatile` sink; `strace -c -f` to count (never to time: about 12 µs per call); `getrusage` is approximate; `perf` from memory.
- **10 Mpps**: 100 ns per packet; one crossing is the whole budget; only a shared ring (54 ns) or XDP and bypass fit with 40 ns of work; timestamps once per batch.

---

## Sources

- [vdso(7)](https://man7.org/linux/man-pages/man7/vdso.7.html) (fetched): what the vDSO is and why, the four x86-64 functions, `AT_SYSINFO_EHDR` and its randomisation, that `strace` and `seccomp` do not see vDSO calls. The page does not describe the fallback when the clock source is unusable; that is from memory.
- [syscall(2)](https://man7.org/linux/man-pages/man2/syscall.2.html) (fetched): the x86-64 register convention (the number in `eax`, the result in `rax`, arguments in `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`), and that `syscall` may not be the fastest entry (referring to `vdso(7)`). It says nothing of the cost, the clobbered `rcx` and `r11`, or the error-return range; those are from memory.
- [io_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html) and [io_uring_setup(2)](https://man7.org/linux/man-pages/man2/io_uring_setup.2.html) (fetched): the shared submission and completion rings, batching with one `io_uring_enter`, `SQPOLL` and `sq_thread_idle`, `IORING_SQ_NEED_WAKEUP` and the privilege history (before 5.11 privileged, `CAP_SYS_NICE` from 5.11, relaxed in 5.13), `IORING_SETUP_SQ_AFF`, `IORING_FEAT_SINGLE_MMAP` (5.4), the mmap offsets, the index array, release and acquire ordering, no `glibc` wrappers. The first page did not mention `IORING_SQ_NEED_WAKEUP` and the second did not state that the entry count rounds to a power of two; the program relies on neither.
- Not fetched, so from memory: the kernel's `copy_from_user` family, SMEP, SMAP and PAN, KPTI and the Spectre mitigations and their costs, restartable sequences behind `sched_getcpu`, `mmap`, `sendfile`, `splice` and `MSG_ZEROCOPY`, `AF_PACKET`, `AF_XDP`, eBPF and XDP, DPDK, `perf` subcommands, the `io_uring` asynchronous file I/O and its security posture, and tick-based CPU-time accounting.
- Measurements on this machine: Linux 6.18.44 in a Firecracker VM, 2 vCPUs, Intel Xeon at a nominal 2.10 GHz, GCC 13.3, glibc 2.39, four programs, as root. The CPU is not affected by Meltdown, so the costs here exclude page-table switching on entry. The `SQPOLL` result measures latency per batch and not throughput. The cost of mitigations was not measured.
