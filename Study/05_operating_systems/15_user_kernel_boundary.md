# OS 15 — The user/kernel boundary

> Prerequisite: `10_virtual_memory_and_tlb.md`.
> This tutorial is the bridge into the kernel track.

## What a syscall actually costs

A syscall is not a function call. It is a privilege transition:

```
  user code
    -> SYSCALL instruction (x86-64) / SVC (AArch64)
    -> CPU switches to ring 0 / EL1, loads the kernel stack
    -> entry_SYSCALL_64: save registers, swapgs, [KPTI: switch CR3]
    -> syscall table dispatch -> the handler runs
    -> [KPTI: switch CR3 back], restore registers
    -> SYSRET / ERET back to user
```

Cost: roughly **50-100 ns** on a modern machine without mitigations, and
**150-400 ns** with KPTI and Spectre mitigations enabled. Check yours:

```bash
cat /sys/devices/system/cpu/vulnerabilities/*
```

That range is the whole reason for `io_uring`, `AF_XDP`, DPDK and vDSO. Frame it
as arithmetic: at 10 Mpps you have 100 ns per packet, so **one syscall per packet
is the entire budget**. Any design with a syscall in the per-packet path is dead
before you discuss anything else.

## vDSO: the syscall that isn't

Some "syscalls" never enter the kernel. The kernel maps a shared page into every
process containing code and data for a few read-only queries:

```bash
cat /proc/self/maps | grep vdso
```

`clock_gettime`, `gettimeofday`, `time` and `getcpu` are served from there by
reading a kernel-maintained page and doing the arithmetic in userspace. Cost
drops from ~100 ns to ~20 ns.

This matters in practice: timestamping every packet with `clock_gettime` is
viable *because* of the vDSO. It also explains the classic puzzle "why is
`gettimeofday` so much faster than `getpid`?" — one is vDSO, the other is a real
syscall. That is a nice question to be able to answer instantly.

## Copying across the boundary

The kernel cannot simply dereference a user pointer — the address may be invalid,
unmapped, or a deliberate attack pointing at kernel memory. Hence:

```c
/* In kernel code. These can fault and they can sleep, so they must never be
 * called from atomic context. They return the number of bytes NOT copied. */
if (copy_from_user(&kbuf, user_ptr, len))
    return -EFAULT;

if (copy_to_user(user_ptr, &kbuf, len))
    return -EFAULT;

/* Hardware enforcement: SMAP (x86) / PAN (ARM) fault if the kernel touches user
 * memory outside these accessors. The accessors toggle the bit around the copy.
 * This is why a stray user-pointer dereference in a driver oopses immediately
 * rather than silently working. */
```

Three rules that come up in driver review, and are worth stating as rules:

1. **Validate before you copy**, and validate *after* copying into kernel memory
   — never validate a user buffer in place, because userspace can change it
   between your check and your use (a TOCTOU race).
2. **Never hold a spinlock across `copy_*_user`**; it can sleep.
3. **Watch integer overflow** in size arithmetic. `offset + len` can wrap.

## Avoiding the boundary: the four strategies

This is the frame that makes the whole kernel track coherent. Every high-
performance I/O technology is one of these four:

### 1. Batch — amortise the crossing

`readv`/`writev`, `sendmmsg`/`recvmmsg`, `epoll_wait` returning many events.
One crossing, N units of work. Simplest and often sufficient: at 64 packets per
`recvmmsg`, a 200 ns syscall becomes 3 ns per packet.

### 2. Share memory — cross once at setup

`mmap` a ring the kernel also writes. Used by `AF_PACKET` v3 (TPACKET), `AF_XDP`
UMEM, `perf` ring buffers, and your Project 02 (`rkdma`) driver. After setup the data path
is pure loads and stores.

### 3. Move the work into the kernel

eBPF and XDP: instead of copying packets out to decide, run a verified program
where the packets already are. Filtering, load balancing and DDoS drop at
millions of packets per second with no crossing at all.

### 4. Take the device out of the kernel

DPDK and friends: unbind the NIC from the kernel driver, map its registers and
DMA rings into userspace via VFIO, poll in a busy loop. No syscalls, no
interrupts, and no kernel networking stack — which also means no `tcpdump`, no
`iptables`, and a dedicated core burning 100%.

```
  strategy          crossings per packet     complexity     loses
  ----------------------------------------------------------------------
  naive read()             1                  none          -
  batching                 1/N                low           some latency
  shared ring              ~0                 medium        ABI coupling
  eBPF/XDP                 0                  medium        expressiveness (verifier)
  kernel bypass            0                  high          the entire kernel stack
```

**The interview move** is to present this ladder and then say which rung the
problem needs, rather than jumping straight to DPDK. Reaching for kernel bypass
when batching would do is a genuine architecture smell, and interviewers are
testing for exactly that judgement.

## io_uring: completion instead of readiness

`epoll` tells you a fd is *ready*; you then syscall to do the work. `io_uring`
gives you two shared ring buffers — submission and completion — so you post
operations and reap results without a syscall per operation.

```
  SQ (submission queue)   userspace writes entries, kernel consumes
  CQ (completion queue)   kernel writes results, userspace consumes
  io_uring_enter()        only needed to kick the kernel; with SQPOLL, not even that
```

With `IORING_SETUP_SQPOLL` a kernel thread polls the submission queue, so a
steady-state workload issues **zero syscalls**. It also works for storage and
file I/O, which `epoll` never did properly — that is arguably its bigger
contribution. Full treatment in `07_networking/05_epoll_and_io_uring.md`.

## Measuring boundary cost

```bash
# Count and time syscalls:
strace -c -f ./app
perf trace -s ./app

# Where is time going, user vs kernel?
perf stat ./app                    # task-clock split
perf record -g ./app && perf report --sort=dso   # kernel vs user symbols

# Raw syscall cost on your machine:
perf bench syscall basic
```

Writing a micro-benchmark for this is worth doing once: time a million
`getppid()` calls (a trivial syscall that cannot be vDSO-accelerated) and divide.
That gives you *your* machine's boundary cost, mitigations included, which is a
far better number to quote than a remembered one.

## Interview angles

**"How expensive is a syscall and why does it matter?"**
50-400 ns depending on mitigations. Then immediately do the packet-rate
arithmetic. The arithmetic is what makes it a Staff answer.

**"How do you get to 10 Mpps?"**
Walk the ladder. Say which rung and why, and what each rung gives up.

**"Why is `clock_gettime` fast but `getpid` not?"**
vDSO.

**"What is `copy_from_user` protecting against?"**
Invalid or malicious user pointers, plus SMAP/PAN enforcement. Mention TOCTOU.

**"epoll vs io_uring?"**
Readiness versus completion; io_uring removes the per-operation syscall and
handles file I/O properly. Mention SQPOLL for the zero-syscall steady state.

## Exercises

1. Measure your machine's syscall cost with a `getppid` loop. Then boot with
   `mitigations=off` in a VM and measure again. Record the delta — that number is
   the price of Spectre/Meltdown mitigation on your hardware.
2. Write a UDP receiver three ways: `recvfrom` per packet, `recvmmsg` with a
   batch of 64, and `AF_PACKET` with an mmap'd TPACKET_V3 ring. Measure packets
   per second for each and produce the ladder from your own data.
3. Time `clock_gettime(CLOCK_MONOTONIC)` and confirm it does not appear in
   `strace`. Then time `clock_gettime(CLOCK_MONOTONIC_RAW)`, which is not vDSO
   accelerated on some kernels, and compare.
4. Write a trivial character device with an `ioctl` that copies a struct in and
   out. Deliberately omit the size validation and demonstrate to yourself what
   goes wrong. Then fix it properly.
5. Build a minimal `io_uring` echo server and compare it against an `epoll` one
   at 1, 10 and 100 concurrent connections. Note where the crossover is — it is
   not where most people assume.
