# OS 14: I/O systems and disk scheduling

*Part of the [Operating systems plan](00_plan.md) · Time: about 8 hours (5 reading and experiments, 3 problems) · You need: [OS 11](11_virtual_memory.md) (the page cache is the file-backed half of demand paging), [OS 03](03_threads.md) and [OS 07](07_condition_variables_and_semaphores.md) (blocking and waking), and, for the hardware side, [CO 26](../04_computer_organization/26_interrupts_exceptions_and_dma.md) (interrupts and DMA) and [CO 17](../04_computer_organization/17_storage_and_reliability.md) (storage devices)*

This is a new tutorial. Every program a computer runs ends in an I/O device: a disk, a network card, a keyboard, a GPU. The operating system's job is to make devices that differ by six orders of magnitude in speed look like a handful of uniform interfaces, to keep the CPU busy while a slow device works, and to decide, when several requests are waiting, which to serve first. The interview questions come in a fixed set. How does the CPU talk to a device (polling, interrupts, DMA), and when is each right? What is a driver, and what is the difference between a block and a character device? What are the I/O models (blocking, non-blocking, multiplexed, signal-driven, asynchronous), and why does `epoll` scale where `select` does not? What does a disk access cost, and what does that make of sequential against random? Work an FCFS, SSTF, SCAN, C-SCAN, LOOK and C-LOOK schedule by hand. Why does seek-based scheduling stop mattering on an SSD, and what takes its place? And what do the Linux I/O schedulers (`none`, `mq-deadline`, BFQ, Kyber) do, and how do you choose?

**Two honest limits, stated first.** (1) The disk under these programs is a **virtual** block device (virtio) inside a micro-VM. Its latencies are those of the host's storage stack plus the guest's, not those of a spinning disk or of a particular SSD, and the host probably caches. So the disk-geometry and scheduling-algorithm sections are **arithmetic and simulation**, run exactly, and the measured sections show what a real kernel does with a real block device, whose absolute numbers belong to this machine and not to a catalogue. (2) Opening `/dev/vda` itself is refused in this sandbox, so every measurement goes through a file on the ext4 file system that lives on it, using `O_DIRECT` where the page cache must be bypassed.

By the end you can:

- explain the device model (registers for status, command and data; port-mapped against memory-mapped access), polling against interrupts, why a fast device can favour polling, hybrid polling and interrupt coalescing, and what DMA removes from the CPU (measured: 2,000 direct 4 KiB reads at queue depth 1 caused 2,001 interrupts, one per read, and 0.02 per read at queue depth 64);
- say what a device driver is, why it is the largest and buggiest part of an operating system, and tell block devices from character devices by `stat` (measured: `/dev/vda` and `/dev/loop0` are block devices; `/dev/null`, `/dev/zero`, `/dev/urandom` and `/dev/tty` are character devices);
- explain buffering, caching and spooling and quantify them (measured: writing 4 MiB one byte at a time cost 221.9 ns per byte, in 4 KiB pieces 0.4; a cold random 4 KiB read about 33.7 / 33.0 µs, the same read from the page cache about 1.1 / 1.2 µs; readahead took a sequential read from about 79.3 / 97.2 to about 2998.8 / 3363.6 MB/s);
- classify an I/O call into the five models, say who waits where, and show the cost of each (measured: a 100 ms wait cost 0.06 ms of CPU blocked and about 100 ms spinning; one `epoll_wait` over 10,000 descriptors cost about 200 ns where `poll` cost 233,920 ns);
- compute the access time of a disk from rotation, seek and transfer, and say why random reads on a hard disk are about 300 times slower than sequential (computed: 339 times for the OSTEP drive);
- run FCFS, SSTF, SCAN, C-SCAN, LOOK and C-LOOK by hand on a queue, give the head movement of each, and say which one starves (simulated: SSTF never served a far request in 100,000 services; the sweep algorithms served it after about 6 or 7);
- say why head scheduling does not apply to flash and what does matter (queue depth, request size, merging, write amplification), with the measured queue-depth curve (25,254 IOPS at depth 1, 209,298 at depth 64) and the measured merging (16,384 sequential 4 KiB reads became 1,254 requests);
- describe the Linux multi-queue block layer, state what `none`, `mq-deadline`, BFQ and Kyber do, and choose between them with a measured example (a reader against a bulk writer: BFQ held the reader's median at about 40 µs while `mq-deadline` gave 11 to 23 ms, and lowering `fifo_batch` to 1 cut that to 1.5 to 1.9 ms).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory, as root. The root file system is ext4 on `/dev/vda`, a 256 GiB virtio block device that reports `rotational` 1, uses the `mq-deadline` scheduler and has a write-back cache; the other block devices (`vdb` to `vdf`) are small read-only images, one of which (`vdb`) is used only to read scheduler tunables. The test file is 1 GiB in `/tmp`, on that file system. `fio`, `iostat` and `blktrace` are not installed, so their output is described, not shown; the kernel's own counters (`/proc/diskstats`, `/proc/interrupts`, sysfs) are read directly. The device-model, drive-geometry, disk-scheduling and flash material comes from the OSTEP chapters on I/O devices, hard disk drives and solid-state drives (fetched), the kernel documents on the multi-queue block layer, the sysfs queue files, the deadline scheduler and BFQ (fetched) and the manual pages for `epoll` and `io_uring` (fetched; see Sources, where the limits of the fetched pages are stated). The Kyber design, the NVMe and `io_uring` internals beyond what the manual says, and general facts marked "from memory" were not fetched.

**Data and timing.** The scheduling algorithms and the access-time formulas are deterministic. The I/O model programs, the multiplexing costs, the page-cache and readahead results and the block-layer results are timings and counters from this machine. Where a program was run twice the two results are shown as "first / second", and they differ by 10 percent or more in places: a virtual disk has the host's noise in it. The scheduler comparison is the noisiest, and the text says which of its results held in every run and which did not.

---

## 1. The operating system's view of a device

### 1.1 The device model

The operating system sees a device through an **interface** of three kinds of register and nothing else (OSTEP, fetched): a **status** register to read the device's state, a **command** register to tell it what to do, and a **data** register to move data. Behind the interface is the **internals**, which may be a microcontroller, memory and custom chips; a RAID controller runs a great deal of firmware there. The protocol for a simple device is four steps: wait until the status says the device is not busy, write the data, write the command, then wait for it to finish.

There are two ways for the CPU to reach those registers. **Port-mapped I/O** uses special instructions (`in` and `out` on x86), which are privileged, so only the kernel can touch a device. **Memory-mapped I/O** makes the registers appear at physical addresses, so ordinary loads and stores reach the device, with no new instructions, and the kernel controls access by controlling the page tables (OSTEP says neither is clearly better, and both are in use; PCIe devices use memory-mapped registers, from memory).

### 1.2 Polling, interrupts, hybrid and DMA

**Polling** means the CPU spins on the status register until the device is done. It wastes the CPU while a slow device works, because the CPU could run another process. **Interrupts** fix that: the kernel issues the request, puts the caller to sleep, runs something else, and the device raises an interrupt when it finishes, so that a handler completes the request and wakes the caller (this is the sleep and wake of [OS 07](07_condition_variables_and_semaphores.md)). Interrupts are not always better. If the device is fast, the interrupt and the context switches cost more than the wait, and **polling wins**; if the device's speed is unknown, a **hybrid** polls briefly and then switches to interrupts. A flood of interrupts, as from a busy network card, can leave the kernel doing nothing but handling interrupts, with no time for user processes: **livelock**. Occasional polling gives the kernel control (this is the idea of NAPI in Linux networking, from memory). **Interrupt coalescing** has the device wait briefly so that several completions share one interrupt, which lowers the overhead and adds latency.

The CPU copying data to a device one word at a time is **programmed I/O**, and it keeps the CPU busy for the whole copy. **Direct memory access (DMA)** fixes that: a DMA engine moves data between memory and the device. The kernel tells it the memory address, the amount and the device, and goes to other work; the engine raises an interrupt at the end. The shape of one disk read in a modern kernel is therefore: build a request that names a **memory buffer**, hand it to the device, sleep; the device DMAs the data into the buffer; one interrupt wakes the thread. The CPU never touches the data on its way in.

**Measured: the interrupt per I/O, and how it falls.** The counters are `/proc/interrupts` (the line named `virtio1-req.0` is the request queue of this disk) and `/proc/diskstats`. 2,000 random 4 KiB direct reads, issued one at a time, produced these deltas:

```text
2000 random 4 KiB O_DIRECT reads of a 1 GiB file, one at a time:
  elapsed 56.3 ms, 28.2 us per read
  diskstats: reads completed +2001, merged +0, sectors +16008 (4098 bytes per read)
  interrupts on virtio1-req.0: +2001 (1.00 per read)
```

One read, one request at the disk, one interrupt. Section 6 raises the number of requests in flight and the interrupts per I/O fall to 0.02, because one interrupt can complete many requests (batching on the device side, a form of coalescing; the mechanism inside virtio is from memory).

### 1.3 Device drivers

A **driver** contains all the device-specific knowledge, and the layers above (the file system, the page cache) issue generic block requests and stay unaware of the device type. That is the abstraction, and it has two costs. The generic interface can hide features: rich error detail from a SCSI disk can be reduced to a generic I/O error before the file system sees it (OSTEP). And drivers are the largest and weakest part of an operating system: OSTEP cites a study in which more than 70 percent of the code of Linux was in drivers, and another in which drivers had many more bugs than the core kernel and were a leading cause of crashes (the figures are as the chapter reports them; I did not check the studies). For an embedded or drivers role this is the point to hold on to: **the driver is where the kernel meets an untrusted, imperfect, concurrent piece of hardware**, and its bugs are the kernel's bugs.

### 1.4 Block devices and character devices

A **block device** is addressed in fixed-size blocks that can be read or written at any position, and the kernel caches it and schedules requests to it (a disk). A **character device** is a stream of bytes with no cache or reordering by the kernel (a terminal, a serial port, a random-number source). The kernel marks each device node with a type, a **major number** that names the driver and a **minor number** that names the device within it:

```text
/dev/null  character device  major   1 minor   3
/dev/zero  character device  major   1 minor   5
/dev/urandom character device  major   1 minor   9
/dev/tty   character device  major   5 minor   0
/dev/vda   block device      major 254 minor   0
/dev/loop0 block device      major   7 minor   0
```

`/dev/vda` (major 254, the virtio block driver on this machine) and `/dev/loop0` are block devices; the rest are character devices. The same code shows another fact: calling `open("/dev/vda")` here failed with `Operation not permitted`, even as root. Device nodes are files, so the usual permission model applies, and a sandbox can deny the open (a container's device cgroup does this).

### 1.5 What the kernel says about the disk

The block layer exposes a device's parameters in `/sys/block/<dev>/queue/`. The program prints the ones used in this tutorial; the meanings are from the kernel's sysfs document (fetched):

```text
/sys/block/vda/queue:
  rotational           1
  scheduler            none [mq-deadline] kyber bfq
  nr_requests          256
  read_ahead_kb        128
  max_sectors_kb       4096
  max_hw_sectors_kb    2147483647
  logical_block_size   512
  physical_block_size  4096
  minimum_io_size      4096
  optimal_io_size      0
  write_cache          write back
  nomerges             0
  rq_affinity          1
  io_poll              0
  add_random           0
  iostats              1
  wbt_lat_usec         75000
  discard_granularity  4096
```

- `rotational` is 1. The meaning is "rotational or not", and the setting can be changed; here it is a **default** for a virtual disk, not a measurement, and section 4.3 shows that this disk has no seek penalty at all. Never read `rotational` as proof of what the hardware is.
- `scheduler` lists the available I/O schedulers with the active one in brackets (`mq-deadline`), and writing a name switches it (section 7).
- `nr_requests` (256) limits how many requests the block layer can allocate. `max_sectors_kb` (4096) is the largest request the layer will build, bounded by `max_hw_sectors_kb`. `read_ahead_kb` (128) is the readahead window for file systems on it (section 2.3).
- `logical_block_size` 512 and `physical_block_size` 4096: the device accepts 512-byte addressing but its natural unit is 4 KiB, so smaller or misaligned writes cost a read-modify-write. `minimum_io_size` is 4096.
- `write_cache` is `write back`, meaning the device may acknowledge a write before it is durable; durability then needs a flush (the subject of OS 16).
- `nomerges` 0 enables all merging; `rq_affinity` 1 completes requests on the CPU group that submitted them; `io_poll` 0 means completion polling is off; `iostats` 1 means the counters of section 8 are collected; `wbt_lat_usec` 75000 is the write-back throttling target (the delay in microseconds at which the kernel starts to slow writers to protect reads).

---

## 2. Buffering, caching and spooling

### 2.1 Buffering

A **buffer** is memory that holds data while it moves between two parties that differ in speed, in transfer size or in timing. There are three reasons for one: a **speed mismatch** (a slow device against a fast producer), a **size mismatch** (a network packet, a disk block and an application's three-byte `write` do not line up), and **copy semantics** (after `write` returns, the application may reuse its buffer, so the kernel must hold a copy until the device has taken it). The cost it removes is the per-call cost of crossing into the kernel, which is what the next program measures:

```c
static void raw_writes(const char *path, size_t chunk)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    char *buf = malloc(chunk);
    memset(buf, 'x', chunk);
    double t0 = now();
    long calls = 0;
    for (size_t done = 0; done < TOTAL; done += chunk) { IGN(write(fd, buf, chunk)); calls++; }
    double dt = now() - t0;
    printf("write() of %5zu bytes  %8ld calls  %9.1f ms  %8.1f ns per byte\n", chunk, calls, dt * 1e3, dt / TOTAL * 1e9);
    close(fd); free(buf);
}
```

It writes 4 MiB to a file with `write` calls of 1, 64, 4096 and 65536 bytes, and then through `stdio` (`fwrite`, which keeps its own buffer in user space and calls `write` once per 4 KiB or so, from memory):

```text
write() of     1 bytes   4194304 calls      930.8 ms     221.9 ns per byte
write() of    64 bytes     65536 calls       15.2 ms       3.6 ns per byte
write() of  4096 bytes      1024 calls        1.8 ms       0.4 ns per byte
write() of 65536 bytes        64 calls        2.0 ms       0.5 ns per byte
fwrite() of    1 bytes  stdio-buffered            55.6 ms      13.3 ns per byte
fwrite() of   64 bytes  stdio-buffered             3.0 ms       0.7 ns per byte
```

Writing one byte at a time costs about 221.9 ns per byte, and 4 KiB at a time about 0.4: a factor of more than 500, all of it the system call (the page cache absorbs the data in both cases). The `stdio` buffer brings the same one-byte-at-a-time program to 13 ns per byte. **A buffer converts N small operations into one large one**, and the same arithmetic reappears at every level from here: the page cache, the block layer's merging and the disk's own cache.

### 2.2 Caching: the page cache and `O_DIRECT`

A **cache** holds a second copy of data to make the next access faster, which is a different purpose from buffering (it keeps data it has already delivered). Linux caches file data in the **page cache** (OS 11, section 1.5): a `read` first looks there and only goes to the device on a miss. The measurement uses 20,000 random 4 KiB reads of a 1 GiB file:

```text
random 4 KiB reads, 20000 per row (microseconds per read)
  buffered, cold cache         33.7
  buffered, same offsets        1.1
  O_DIRECT                     45.9
```

(microseconds per read, first run; the text gives both runs as "first / second"). A cold buffered read costs about 33.7 / 33.0 µs, which is the device; the same offsets again cost about 1.1 / 1.2 µs, which is the cost of the system call and a copy from memory, roughly 30 times cheaper. `O_DIRECT` bypasses the page cache and goes to the device every time (45.9 / 32.5 µs), which is the right choice for a program that has its own cache (a database buffer pool) and would otherwise cache everything twice. It requires buffers, offsets and lengths aligned to the device's block size (the programs use 4096). The cache-hit number is the argument for **doing less I/O**, and the miss number is the argument for **doing more of it at once**.

### 2.3 Readahead

When a program reads a file sequentially, the kernel notices and reads **ahead** of the program, so that the next blocks are already in the cache when they are asked for. The window is `read_ahead_kb`. The program sets it to 0, 128 (the machine's default) and 4096, drops the page cache each time, and reads the first 256 MiB in 4 KiB pieces (MB/s, first run shown; the text gives both runs):

```text
sequential 4 KiB buffered reads of the first 256 MiB, cold cache (MB/s)
  read_ahead_kb 0         79.3
  read_ahead_kb 128      686.3
  read_ahead_kb 4096    2998.8
```

With readahead off every 4 KiB `read` is a separate trip to the device and the program gets about 79.3 / 97.2 MB/s; with a 128 KiB window about 686.3 / 1385.7; with 4 MiB about 2998.8 / 3363.6. Readahead is **prefetching sized to the workload**: too small and the device idles between requests, too large and it wastes bandwidth and cache on data that is never read (for a random workload it should be small). The 128 KiB runs differ by a factor of two between the two runs, which is the virtual disk's host-side variation, not a kernel effect.

### 2.4 The request size, and a two-parameter model

The next table reads the first 256 MiB of the file with `O_DIRECT` in different request sizes, with no readahead and no cache involved (MB/s, first run shown):

```text
sequential O_DIRECT reads of the first 256 MiB by request size (MB/s)
      4096 bytes     106.0
     16384 bytes     375.1
     65536 bytes    1110.9
    262144 bytes    2393.8
   1048576 bytes    2579.0
   4194304 bytes    2805.3
```

Throughput climbs 20 to 25 times from 4 KiB to 1 MiB and then flattens. A rough two-parameter model explains it: each request costs a **fixed overhead** plus a **per-byte time**, `t = a + s / B`. From the 1 MiB and 4 MiB rows of the first run (407 and 1,495 microseconds per request) the line gives `B` of about 2.9 GB/s and `a` of about 44 µs, which predicts 4 KiB at 45 µs against 39 observed, and 64 KiB at 66 against 59. It is a fit to a noisy virtual disk and is about 15 percent off at the ends, so use it for the shape and not for the digits: **the fixed per-request cost, not the transfer, is what small requests pay**, and large requests are the only way to reach the device's bandwidth. This is the SSD-era form of the disk's seek, and it is why merging (section 7.1) matters.

### 2.5 Spooling

A **spool** (simultaneous peripheral operations on-line) is a buffer on disk for a device that cannot be shared and is slow: the classic case is a printer. Each job is written to a queue directory and a daemon feeds the printer one job at a time, so that many programs can "print" at once and none waits for the paper. The same structure appears in mail queues, batch-job systems, and the journal that a logging daemon drains to disk (all from memory). The idea is the buffer of section 2.1 enlarged to the size of a whole job and made durable, and the price is that the operation completes later than the call that requested it, so the requester needs a way to be told (a job id, a status, a bounce message).

---
## 3. The five I/O models

### 3.1 Two phases, five ways to handle them

Getting data from a device to a program has **two phases**: waiting for the data to be ready (a packet to arrive, a disk block to be read into the kernel's buffer), and **copying** it from the kernel to the program's buffer. The five classical I/O models (W. Richard Stevens' classification, from memory) differ in what the caller does during each phase:

| Model | Waiting phase | Copy phase | Linux call |
| --- | --- | --- | --- |
| Blocking | the thread sleeps in the call | in the call | `read` on a default descriptor |
| Non-blocking | the call returns `EAGAIN` at once; the caller must ask again | in the call, when data is there | `read` with `O_NONBLOCK` |
| Multiplexed | one thread sleeps in a call that watches many descriptors | the caller then calls `read`, which cannot block | `select`, `poll`, `epoll_wait` |
| Signal-driven | the caller runs on; the kernel sends `SIGIO` when ready | the caller calls `read` from the handler or later | `fcntl` with `O_ASYNC` |
| Asynchronous | the caller submits and runs on | **the kernel does it** and reports completion | kernel AIO, `io_uring` |

The first four are all **synchronous** in the POSIX sense: the caller itself performs the copy. Only the last is **asynchronous**: the kernel performs both phases and tells the caller when the whole operation is done. The word "asynchronous" is used loosely in practice for any of the middle three, and an interview answer should say which is meant.

### 3.2 The first four, run

The program makes a child process write one byte to a pipe after 100 ms, and has the parent wait for it under each model (a pipe stands for any descriptor that becomes ready later; the model, not the device, is the subject). The three that take the most code:

```c
static void model_blocking(void)
{
    char c;
    start_writer(0.100);
    double t0 = now(), c0 = cpu_now();
    long v0 = voluntary_switches();
    IGN(read(pfd[0], &c, 1));                  /* sleeps in the kernel until the byte arrives */
    report("blocking read", t0, c0, 1, voluntary_switches() - v0, "sleeps");
}

static void model_nonblocking(void)
{
    char c;
    long calls = 0, eagain = 0;
    start_writer(0.100);
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);
    double t0 = now(), c0 = cpu_now();
    for (;;) {
        calls++;
        if (read(pfd[0], &c, 1) == 1) break;
        if (errno == EAGAIN) eagain++;         /* nothing yet: ask again, burning the CPU */
    }
    report("non-blocking, spinning", t0, c0, calls, eagain, "EAGAIN");
}

static void model_signal(void)
{
    char c;
    start_writer(0.100);
    struct sigaction sa = { .sa_handler = on_sigio };
    sigaction(SIGIO, &sa, NULL);
    fcntl(pfd[0], F_SETOWN, getpid());
    fcntl(pfd[0], F_SETFL, O_ASYNC | O_NONBLOCK);
    double t0 = now(), c0 = cpu_now();
    long work = 0;
    while (!got_sigio) { for (volatile int i = 0; i < 1000; i++) { } work++; }   /* useful work meanwhile */
    IGN(read(pfd[0], &c, 1));
    report("signal-driven (SIGIO)", t0, c0, 1, work, "work units done meanwhile");
}
```

The output (elapsed wall time, CPU time of the waiting thread, the number of system calls it made, and the model's own count):

```text
blocking read          elapsed  100.4 ms  cpu   0.064 ms  syscalls        1  sleeps=1
non-blocking, spinning elapsed  100.3 ms  cpu 100.331 ms  syscalls   814422  EAGAIN=814421
select                 elapsed  100.4 ms  cpu   0.046 ms  syscalls        2  spins=0
poll                   elapsed  100.4 ms  cpu   0.049 ms  syscalls        2  spins=0
epoll                  elapsed  100.5 ms  cpu   0.082 ms  syscalls        3  spins=0
signal-driven (SIGIO)  elapsed  100.4 ms  cpu  99.495 ms  syscalls        1  work units done meanwhile=273982
```

- **Blocking.** One system call, 0.06 ms of CPU, one voluntary sleep. The thread was off the CPU for 100 ms, free for other work. This is the right default: simple, and the cost of waiting is zero.
- **Non-blocking, spinning.** Same 100 ms of wall time, but **100.331 ms of CPU** and 814,422 system calls, all of them `EAGAIN` but the last. The CPU did nothing for 100 ms but ask. Non-blocking mode alone is almost never the answer; it is a **building block** for the multiplexed models, where the caller is told when to ask.
- **Multiplexed** (`select`, `poll`, `epoll`). Two or three system calls and well under 0.1 ms of CPU, exactly like blocking: the thread sleeps in the wait call. The difference from blocking only shows with more than one descriptor, which is the next section.
- **Signal-driven.** The thread ran 273,982 units of other work while it waited (the loop counts them), so it kept the CPU doing something useful; its CPU time is 99 ms because that is the work, not waste. The model works, and is rarely used: signals do not queue (several arrivals can produce one `SIGIO`), carry little information, interrupt the program at arbitrary points where only async-signal-safe calls are allowed, and a descriptor still has to be found and read (all from memory). Modern servers use multiplexing, or true asynchronous I/O, instead.

### 3.3 Multiplexing at scale: `select`, `poll`, `epoll`

A server with 10,000 connections cannot have 10,000 threads blocked in `read`, nor spin over 10,000 non-blocking descriptors. It uses **one wait call over all of them**. The three Linux calls differ in how they hold the set:

- `select` takes **bitmaps** of descriptors (limited to `FD_SETSIZE`, 1024 descriptor numbers, from memory), **modifies them in place**, so the caller must rebuild the sets before each call, and the kernel scans every descriptor up to the highest.
- `poll` takes an **array** of `struct pollfd`, with no fixed limit, copied into the kernel on each call, which scans all of it.
- `epoll` keeps the interest list **in the kernel**: you register a descriptor once with `epoll_ctl`, and the kernel adds it to a ready list when it becomes ready, so `epoll_wait` returns only the ready ones. The manual page (fetched) says it "scales well" to large numbers of descriptors without giving a figure; the measurement below supplies one.

The program creates N `eventfd` descriptors, makes **only the last** one ready (the worst case for a scan), and times one non-blocking wait call (timeout 0). The three loops:

```c
            for (int r = 0; r < reps; r++) {
                fd_set rs = base;                             /* select destroys its sets: rebuild every call */
                select(fds[n - 1] + 1, &rs, NULL, NULL, &(struct timeval){ 0, 0 });
            }
```

```c
        for (int r = 0; r < reps; r++) poll(pf, n, 0);
```

```c
        int ep = epoll_create1(0);
        for (int i = 0; i < n; i++) {
            struct epoll_event ev = { .events = EPOLLIN, .data.fd = fds[i] };
            epoll_ctl(ep, EPOLL_CTL_ADD, fds[i], &ev);
        }
```

```c
        for (int r = 0; r < reps; r++) epoll_wait(ep, out, 4, 0);
```

The first rebuilds `rs` from `base` on every call because `select` clobbers it. The result:

```text
      N       select         poll        epoll   (ns per call; one ready descriptor, placed last)
     10          269          191          209
    100          785          713          213
    500         4077         2670          209
   1000         6472         7787          282
   5000 n/a (fd>=1024)        52757          270
  10000 n/a (fd>=1024)       233920          203
```

(nanoseconds per call). `epoll_wait` is **flat at about 200 to 280 ns** from 10 descriptors to 10,000; `poll` and `select` grow linearly with N, to about 6,472 ns for `select` at 1,000 and about 233,920 ns for `poll` at 10,000 (`select` cannot watch those descriptors at all, because their numbers exceed 1024). At 10,000 connections an event loop that waits once per event on `poll` spends 150 to 230 µs per wait, a thousand times `epoll`'s cost, which is the **C10K problem** (handling ten thousand concurrent connections, from memory) in one table.

Two properties of `epoll` need care (the manual page, fetched). **Level-triggered**, the default, behaves like `poll`: a descriptor is reported for as long as data remains. **Edge-triggered** (`EPOLLET`) reports only a change of state, so if the reader consumes part of the data a later `epoll_wait` may block with data still waiting; to use it safely the descriptors must be non-blocking and each must be read until `read` returns `EAGAIN`. Edge-triggered servers can also **starve** other descriptors while draining one busy one, so a ready list handled round-robin is needed. `EPOLLONESHOT` disables a descriptor after one event and makes the caller re-arm it, which prevents two threads from handling the same one. And a descriptor leaves the interest list only when **every descriptor referring to the same open file description** is closed, so a closed descriptor can still deliver events unless it was removed with `EPOLL_CTL_DEL` first.

### 3.4 Readiness is not completion: asynchronous I/O

`epoll` answers "can I read without blocking?", which suits sockets and pipes. It does not help with **regular files** on a disk: a regular file is always reported ready (from memory), and the `read` then blocks on the disk. For files and for a design that wants the kernel to do the whole job, Linux offers true asynchronous I/O.

**Kernel AIO** (`io_setup`, `io_submit`, `io_getevents`) submits a batch of requests and collects completions; in practice it is genuinely asynchronous only for files opened with `O_DIRECT` (from memory), which is how the programs here use it. libaio is not installed, so they call the raw system calls:

```c
static void aio_random(int fd, int qd, int total, double *iops, long *calls)
{
    aio_context_t ctx = 0;
    struct iocb cb[MAXQD], *p[MAXQD];
    struct io_event ev[MAXQD];
    if (io_setup(MAXQD, &ctx) != 0) { perror("io_setup"); exit(1); }
    long submitted = 0, done = 0;
    *calls = 0;
    double t0 = now();
    for (int i = 0; i < qd; i++) {                               /* fill the queue */
        memset(&cb[i], 0, sizeof cb[i]);
        cb[i].aio_fildes = fd; cb[i].aio_lio_opcode = IOCB_CMD_PREAD;
        cb[i].aio_buf = (uintptr_t)bufs[i]; cb[i].aio_nbytes = BS;
        cb[i].aio_offset = (rnd() % (FSIZE / BS)) * BS; cb[i].aio_data = i;
        p[i] = &cb[i];
    }
    io_submit(ctx, qd, p); (*calls)++; submitted = qd;
    while (done < total) {
        long n = io_getevents(ctx, 1, qd, ev); (*calls)++;
        int k = 0;
        for (long i = 0; i < n; i++) {                           /* refill one slot per completion */
            done++;
            if (submitted < total) {
                struct iocb *c = (struct iocb *)(uintptr_t)ev[i].obj;
                c->aio_offset = (rnd() % (FSIZE / BS)) * BS;
                p[k++] = c; submitted++;
            }
        }
        if (k) { io_submit(ctx, k, p); (*calls)++; }
    }
    *iops = total / (now() - t0);
    syscall(SYS_io_destroy, ctx);
}
```

**`io_uring`** replaces the per-call crossing with two **rings shared between the application and the kernel** (the manual page, fetched): a **submission queue** into which the application puts entries describing operations, and a **completion queue** from which it reads results, both mapped into the application's memory. One `io_uring_enter` submits any number of entries and can wait for completions, and with a kernel polling thread (`SQPOLL`) even that call can be skipped. Each entry's result carries the equivalent system call's return value or a negated errno, requests can complete in any order, so a `user_data` field identifies them. Setting up the rings, and submitting one read:

```c
static inline int ring_open(struct ring *r, unsigned entries)
{
    struct io_uring_params p;
    memset(&p, 0, sizeof p);
    r->fd = syscall(SYS_io_uring_setup, entries, &p);
    if (r->fd < 0) return -1;
    size_t sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned);
    size_t cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
    if (cq_sz > sq_sz) sq_sz = cq_sz;
    char *m = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQ_RING);
    r->sqes = mmap(NULL, p.sq_entries * sizeof(struct io_uring_sqe), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQES);
    if (m == MAP_FAILED || r->sqes == MAP_FAILED) return -1;
    r->sq_tail = (unsigned *)(m + p.sq_off.tail);
    r->sq_mask = (unsigned *)(m + p.sq_off.ring_mask);
    r->sq_array = (unsigned *)(m + p.sq_off.array);
    r->cq_head = (unsigned *)(m + p.cq_off.head);
    r->cq_tail = (unsigned *)(m + p.cq_off.tail);
    r->cq_mask = (unsigned *)(m + p.cq_off.ring_mask);
    r->cqes = (struct io_uring_cqe *)(m + p.cq_off.cqes);
    return 0;
}

static inline void ring_read(struct ring *r, int fd, void *buf, unsigned len, uint64_t off, uint64_t tag)
{
    unsigned tail = *r->sq_tail, idx = tail & *r->sq_mask;
    struct io_uring_sqe *s = &r->sqes[idx];
    memset(s, 0, sizeof *s);
    s->opcode = IORING_OP_READ; s->fd = fd; s->addr = (uintptr_t)buf; s->len = len;
    s->off = off; s->user_data = tag;
    r->sq_array[idx] = idx;
    __atomic_store_n(r->sq_tail, tail + 1, __ATOMIC_RELEASE);    /* publish the entry to the kernel */
}
```

and the loop that keeps 32 reads in flight, reaping all the completions each time it is woken:

```c
static void uring_random(int fd, int qd, int total, double *iops, long *calls)
{
    struct ring r;
    if (ring_open(&r, MAXQD) != 0) { perror("io_uring_setup"); exit(1); }
    long done = 0, submitted = 0;
    unsigned pending = 0;
    *calls = 0;
    double t0 = now();
    for (int i = 0; i < qd; i++) { ring_read(&r, fd, bufs[i], BS, (rnd() % (FSIZE / BS)) * BS, i); pending++; submitted++; }
    while (done < total) {
        syscall(SYS_io_uring_enter, r.fd, pending, 1, IORING_ENTER_GETEVENTS, NULL, 0); (*calls)++;
        pending = 0;
        unsigned head = *r.cq_head, tail = __atomic_load_n(r.cq_tail, __ATOMIC_ACQUIRE);
        while (head != tail) {                                   /* reap every finished request */
            struct io_uring_cqe *c = &r.cqes[head & *r.cq_mask];
            if (c->res != BS) { fprintf(stderr, "cqe res %d\n", c->res); exit(1); }
            int slot = (int)c->user_data;
            head++; done++;
            if (submitted < total) {
                ring_read(&r, fd, bufs[slot], BS, (rnd() % (FSIZE / BS)) * BS, slot);
                pending++; submitted++;
            }
        }
        __atomic_store_n(r.cq_head, head, __ATOMIC_RELEASE);
    }
    *iops = total / (now() - t0);
    close(r.fd);
}
```

The comparison at queue depth 32, forty thousand 4 KiB direct reads each, on this machine (two runs of each):

```text
the same at queue depth 32, AIO against io_uring (40000 reads)
  kernel AIO    179613 IOPS,   5767 system calls
  io_uring      154068 IOPS,   2960 system calls
  kernel AIO    185605 IOPS,   6108 system calls
  io_uring      175942 IOPS,   1489 system calls
```

The throughput is about the same (the virtual disk, not the interface, is the limit, and the runs vary by 15 percent), but the **system calls** are not: kernel AIO made about 5,800 to 6,100 calls (a `io_getevents` and an `io_submit` per batch of completions) where `io_uring` made 1,500 to 3,000 (one `io_uring_enter` per batch). When each I/O is a few microseconds, as on a fast NVMe device, that difference is the one that matters (the claim about fast NVMe is from memory; this disk is too slow to show it).

---

## 4. The disk

### 4.1 Geometry

A hard disk stores data on one or more spinning **platters**, each surface divided into concentric **tracks**, each track into **sectors** (512 bytes or 4 KiB). The tracks at the same radius on all surfaces form a **cylinder**; one arm carries a head per surface, and the heads move together. The operating system does not see this: a modern disk presents a flat array of **logical blocks** (LBA), numbered from 0, and hides the geometry (zones with more sectors per track at the outer edge, remapped bad sectors). That matters for scheduling, because the kernel has to guess distance from block numbers, on the assumption that blocks with nearby numbers are physically near. OSTEP calls the variant of SSTF that uses block addresses **nearest-block-first**.

### 4.2 Access time

One I/O to a hard disk costs three things (OSTEP, fetched): `T = T_seek + T_rotation + T_transfer`.

- **Seek**: move the arm to the right cylinder. It has an acceleration, coasting, deceleration and settling phase; the quoted figure is an average (4 ms for the high-end drive, 9 ms for the capacity drive of the chapter).
- **Rotation**: wait for the right sector to come under the head. The average is **half a revolution**: at 7,200 rpm one revolution takes 60 / 7200 = 8.33 ms, so 4.17 ms on average.
- **Transfer**: the data passes under the head at the media rate (about 100 to 125 MB/s for these drives), so 4 KiB takes about 0.03 to 0.04 ms.

The program applies the formulas to the two OSTEP drives and to an assumed laptop drive:

```text
access time of one random 4 KiB read (the OSTEP formulas)
Cheetah 15K.5          rpm  15000  seek  4.0 ms  rotation  2.00 ms  transfer 0.0328 ms  total  6.033 ms  rate 0.679 MB/s
Barracuda 7200         rpm   7200  seek  9.0 ms  rotation  4.17 ms  transfer 0.0390 ms  total 13.206 ms  rate 0.310 MB/s
laptop 5400 (assumed)  rpm   5400  seek 12.0 ms  rotation  5.56 ms  transfer 0.0512 ms  total 17.607 ms  rate 0.233 MB/s
Barracuda sequential 100 MB: 966 ms, 103.6 MB/s; random/sequential gap 339x
```

Three consequences. (1) A random 4 KiB read is **99.7 percent seek and rotation**: for the 7,200 rpm drive 13.2 ms of which 0.04 ms is moving data. (2) The disk delivers **0.31 MB/s** on random 4 KiB reads and **104 MB/s** on a sequential 100 MB read: a gap of about **340 times** (OSTEP's own figures, with the transfer time rounded differently, are 0.31 and 105 MB/s). (3) Faster rotation helps rotation and seek, not the gap: the 15,000 rpm drive reaches 0.68 MB/s. Everything in file system design for disks follows from this: **lay data out so it is read sequentially, and issue large requests**. And it explains the whole subject of the next section: when each random request costs 6 to 13 ms, choosing the order in which to serve a queue of them can matter more than anything else the kernel does.

### 4.3 What a drive does for itself, and what this disk does

A real drive has its own cache and, in newer drives, a command queue that lets the **drive** reorder up to dozens of outstanding requests (NCQ on SATA, from memory). That is why OSTEP says SPTF (shortest positioning time first, which accounts for rotation as well as seek) is usually implemented inside the drive: the operating system does not know the head's angular position, but the drive does. The kernel's scheduler still matters, because it decides what the drive is offered, and because it merges and prioritises.

**This machine's disk has no seek at all, and the measurement shows it.** A random 4 KiB direct read took about 45.9 / 32.5 µs (section 2.2), which is 90 to 125 MB/s at queue depth 1; the **sequential** 4 KiB direct read of section 2.4 delivered 106.0 / 110.2 MB/s, and the two are the same within the noise. A rotating disk would show a ratio of about 340. The kernel nevertheless reports `rotational` as 1 (section 1.5): that flag is a default for the virtual device, and the benchmark, not the flag, is the evidence about the hardware. Every conclusion about seek-based scheduling in the next section comes from simulation for this reason.

---
## 5. Disk scheduling by hand

### 5.1 The problem and the six algorithms

Requests arrive faster than a disk can serve them, so a queue forms, and each request names a cylinder. The order of service decides the total **head movement**, and so the total time, since seek dominates. The usual teaching example, which the program also runs: a disk of 200 cylinders (0 to 199), the head at **53**, and a queue arriving in the order **98, 183, 37, 122, 14, 124, 65, 67**. The measure is cylinders travelled. The algorithms:

- **FCFS** serves in arrival order. Fair, simple, and the head swings back and forth.
- **SSTF** (shortest seek time first) serves the nearest request to the head next. Least movement per step; a greedy choice.
- **SCAN** (the elevator) moves the head in one direction, serving requests on the way, **to the end of the disk**, then reverses.
- **C-SCAN** (circular SCAN) sweeps in one direction only: at the end it **jumps back to the start** without serving, and sweeps again.
- **LOOK** is SCAN that reverses at the **last request** in the direction of travel, not at the disk's edge.
- **C-LOOK** is C-SCAN that jumps back to the **lowest outstanding request**, not the edge.

All four sweep algorithms are worked below going **upward first**; going downward first gives the mirror image.

### 5.2 Worked by hand

**FCFS.** 53 to 98 is 45; to 183 is 85; to 37 is 146; to 122 is 85; to 14 is 108; to 124 is 110; to 65 is 59; to 67 is 2. Total `45 + 85 + 146 + 85 + 108 + 110 + 59 + 2 =` **640**.

**SSTF.** From 53 the nearest is 65 (12). From 65 it is 67 (2). From 67 the choices are 37 (30 away) and 98 (31 away): 37 (30). From 37 it is 14 (23). From 14 the nearest remaining is 98 (84), then 122 (24), 124 (2), 183 (59). Total `12 + 2 + 30 + 23 + 84 + 24 + 2 + 59 =` **236**, in the order 65, 67, 37, 14, 98, 122, 124, 183.

**SCAN.** Up from 53 to the edge at 199 (146 cylinders), serving 65, 67, 98, 122, 124, 183 on the way; then down from 199 to 14 (185 cylinders), serving 37 and 14. Total `146 + 185 =` **331**.

**C-SCAN.** Up to 199 (146), serving the same six; jump to 0 (199 cylinders, no service); up from 0 to 37 (37), serving 14 and 37. Total `146 + 199 + 37 =` **382**, or **183** if the empty return trip is not counted (textbooks differ, so say which).

**LOOK.** Up to the last request, 183 (130), then down to the lowest, 14 (169). Total `130 + 169 =` **299**.

**C-LOOK.** Up to 183 (130); jump to 14 (169); up to 37 (23). Total `130 + 169 + 23 =` **322**, or **153** without the return jump.

The program computes all six, and the figures agree:

```text
queue: 98 183 37 122 14 124 65 67   head at 53, cylinders 0..199, sweeping upward first
FCFS    total 640                                 order: 98 183 37 122 14 124 65 67
SSTF    total 236                                 order: 65 67 37 14 98 122 124 183
SCAN    total 331                                 order: 65 67 98 122 124 183 37 14
C-SCAN  total 382 (183 without the 199 return trip)  order: 65 67 98 122 124 183 14 37
LOOK    total 299                                 order: 65 67 98 122 124 183 37 14
C-LOOK  total 322 (153 without the 169 return trip)  order: 65 67 98 122 124 183 14 37
```

The function that does it, with FCFS and SSTF at the top and the four sweeps below:

```c
static int schedule(int alg, const int *q, int n, int head, int *order, int *jump)
{
    int left[MAXQ], used[MAXQ] = { 0 }, k = 0, pos = head, total = 0;
    *jump = 0;
    if (alg == FCFS) {
        for (int i = 0; i < n; i++) { total += abs(q[i] - pos); pos = q[i]; order[k++] = q[i]; }
        return total;
    }
    if (alg == SSTF) {
        for (int step = 0; step < n; step++) {
            int best = -1;
            for (int i = 0; i < n; i++)
                if (!used[i] && (best < 0 || abs(q[i] - pos) < abs(q[best] - pos))) best = i;
            used[best] = 1; total += abs(q[best] - pos); pos = q[best]; order[k++] = q[best];
        }
        return total;
    }
    memcpy(left, q, n * sizeof(int));
    qsort(left, n, sizeof(int), cmp_int);
    int first = 0;
    while (first < n && left[first] < head) first++;     /* first request at or above the head */
    if (alg == SCAN || alg == LOOK) {
        for (int i = first; i < n; i++) order[k++] = left[i];
        int top = first < n ? left[n - 1] : head;        /* LOOK turns at the last request */
        if (alg == SCAN && first > 0) top = CYL - 1;     /* SCAN goes to the edge first */
        if (alg == SCAN && first == n) top = head;
        total = abs(top - head);
        for (int i = first - 1; i >= 0; i--) order[k++] = left[i];
        if (first > 0) total += abs(top - left[0]);
        return total;
    }
    /* C-SCAN and C-LOOK: sweep up, jump back to the bottom, sweep up again */
    for (int i = first; i < n; i++) order[k++] = left[i];
    int top = first < n ? left[n - 1] : head;
    if (alg == CSCAN && first > 0) top = CYL - 1;
    total = top - head;
    if (first > 0) {
        int bottom = alg == CSCAN ? 0 : left[0];
        *jump = top - bottom;
        total += *jump;
        for (int i = 0; i < first; i++) order[k++] = left[i];
        total += left[first - 1] - bottom;
    }
    return total;
}
```

### 5.3 What the numbers mean

One queue proves little, so the program also draws random queues (20,000 per row, uniform cylinders, head at a random cylinder) and reports the **mean cylinders moved per request**:

```text
random queues, 20000 trials each, head at a random cylinder; mean cylinders moved per request
     n     FCFS     SSTF     SCAN   C-SCAN     LOOK   C-LOOK
     4    66.78    39.78    53.25    79.53    43.43    53.32
    16    66.59    15.42    17.22    23.37    16.04    20.86
    64    66.74     4.46     4.57     6.11     4.49     5.95
```

Four facts. (1) FCFS is flat at about 67 cylinders per request whatever the queue length, because it never exploits the queue. (2) Every other algorithm gets **better as the queue gets longer**, because there is more choice: at 64 requests the five reordering algorithms are all near 4.5 to 6 cylinders. This is why the elevator is a **throughput** device: the deeper the queue, the less each request costs. (3) SSTF has the lowest total, as expected of a greedy method, and LOOK is close behind. (4) The circular versions cost more in total than their non-circular cousins, because of the return trip; what they buy is in the next paragraph.

**Starvation.** SSTF can leave a request waiting forever, and the next experiment shows it. Eight requests are always pending near cylinder 100 (a new one arrives each time one is served), and one request waits at cylinder 199. How many requests are served before the far one?

```text
starvation (the four sweep algorithms are simulated as turning at the last request): 8 requests always pending in 80..120, one waiting at cylinder 199 (cap 100000 served)
FCFS    mean served before the far request:      9.0   runs that hit the cap:   0 of 200
SSTF    mean served before the far request: 100000.0   runs that hit the cap: 200 of 200
SCAN    mean served before the far request:      6.4   runs that hit the cap:   0 of 200
C-SCAN  mean served before the far request:      6.4   runs that hit the cap:   0 of 200
LOOK    mean served before the far request:      6.8   runs that hit the cap:   0 of 200
C-LOOK  mean served before the far request:      6.5   runs that hit the cap:   0 of 200
```

(the four sweep algorithms are simulated as turning at the last request, which is why SCAN and LOOK, and C-SCAN and C-LOOK, agree to within noise). SSTF **never** served it: in all 200 runs it was still waiting after 100,000 services, because something nearer always arrived. FCFS served it as the ninth, by definition. The sweep algorithms served it after about 6 or 7, bounded by one sweep. **Greedy locality starves the far request: the fix is a rule that guarantees every request a bounded wait.** The cost of the rule is more head movement, which is the whole trade of the table below.

| Algorithm | Total movement | Starvation | Waiting time | Remark |
| --- | --- | --- | --- | --- |
| FCFS | highest | none | fair, high average | no reordering |
| SSTF | lowest | **possible** | unfair to the far cylinders | greedy |
| SCAN | low | none | uneven: a request just behind the head waits about two sweeps | edge to edge |
| C-SCAN | low, plus the return | none | **more uniform**: every request waits at most about one sweep plus the return | favoured when load is heavy |
| LOOK | lower than SCAN | none | as SCAN | turns at the last request |
| C-LOOK | lower than C-SCAN | none | as C-SCAN | turns at the lowest request |

Variants worth naming (OSTEP, fetched): **F-SCAN** freezes the queue during a sweep, so requests arriving mid-sweep wait for the next one and cannot keep a sweep going forever near the head; **nearest-block-first** is SSTF on block addresses; and **SPTF** accounts for rotation as well as seek, which only the drive can do (section 4.3). **I/O merging** combines adjacent requests (blocks 33 and 34) into one, saving a request's fixed cost (section 7.1).

**Work-conserving or anticipatory.** A **work-conserving** scheduler sends the next request the moment the disk is free. An **anticipatory** (non-work-conserving) scheduler may **wait briefly** when a process has just finished a read, on the bet that it will immediately ask for the next nearby block, which is cheaper than seeking away and back (OSTEP). Linux's BFQ does exactly this: its `slice_idle` tunable on this machine reads 8 ms (section 7.2).

---

## 6. SSDs, and why seek scheduling stops applying

### 6.1 How flash works

An SSD is built from flash chips, organised into **blocks** (erase blocks, typically 128 to 256 KB) which hold **pages** (a few KB, often 4 KB). It has three operations (OSTEP, fetched): **read** a page (fast, and the same wherever the page is), **erase** a whole block (slow; it resets every page in it), and **program** a page (slower than a read and faster than an erase, and possible only on a page in an erased block). The consequence is the **write rule**: overwriting a page means erasing its block first, so the live pages in that block must be moved somewhere else beforehand. Raw latencies, as the chapter gives them:

| Type | Read (µs) | Program (µs) | Erase (µs) |
| --- | --- | --- | --- |
| SLC | 25 | 200 to 300 | 1,500 to 2,000 |
| MLC | 50 | 600 to 900 | about 3,000 |
| TLC | about 75 | about 900 to 1,350 | about 4,500 |

Blocks also wear out: the chapter gives about 10,000 program and erase cycles for MLC and about 100,000 for SLC.

The **flash translation layer (FTL)** inside the drive hides all this behind the ordinary block interface. Most FTLs are **log-structured**: a write is appended to the next free page, and an in-memory **mapping table** records where each logical block now lives. Overwritten pages become dead, and **garbage collection** reads the live pages of a block, rewrites them to the log and erases the block. **Wear levelling** spreads erases evenly, including by moving data that never changes. **Write amplification** is the bytes written to flash divided by the bytes the host wrote, and it is what a bad FTL or a nearly full drive makes large. **Trim** lets the host tell the drive which blocks are free, so collection does not copy dead data, and **overprovisioning** (spare capacity) lets the drive postpone and hide the cleaning. The chapter's comparison of SSDs and a 15,000 rpm disk (MB/s) shows the effect:

| Device | Random read | Random write | Sequential read | Sequential write |
| --- | --- | --- | --- | --- |
| Samsung 840 Pro SSD | 103 | 287 | 421 | 384 |
| Seagate 600 SSD | 84 | 252 | 424 | 374 |
| Intel 335 SSD | 39 | 222 | 344 | 354 |
| Seagate Savvio 15K.3 disk | 2 | 2 | 223 | 223 |

The disk loses a factor of 100 between its sequential and random reads; the SSDs lose a factor of 4 to 9 between theirs (the chapter's figures, from drives of its time, so read the shape and not the digits). SSD random **writes** beat random reads because the log structure turns them into sequential appends.

### 6.2 Why seek-based scheduling no longer applies

There is no head, no arm and no rotation. A read costs about the same wherever it lands, so an algorithm whose entire purpose is to shorten head travel has **nothing to optimise**, and the elevator's sort becomes pure overhead. The chapter does not address scheduling directly; this follows from its description of flash as a random-access device. What replaces head scheduling:

1. **Parallelism.** An SSD has many flash chips on several channels, and can serve many requests **at the same time**. A queue that holds one request wastes most of the device; the **queue depth** is the lever, not the order.
2. **Request size and merging.** The fixed cost per request (section 2.4) is still there, so large and merged requests still win.
3. **Writes.** Sequential and aligned writes cost the FTL less, and garbage collection causes **latency spikes** when it runs behind a write burst.
4. **Fairness and latency.** The scheduler's job becomes to share the device between processes and to keep one process's bulk traffic from delaying another's small reads. That is the subject of section 7.3.

### 6.3 The measured queue-depth curve

The program issues 40,000 random 4 KiB direct reads through kernel AIO, with a fixed number in flight (the queue depth, QD), against a cold cache. **Mean latency** is computed from **Little's law** (the number in the system equals the throughput times the time each spends there), `latency = QD / IOPS`, so it is a derived figure and not a per-request measurement:

```text
random 4 KiB O_DIRECT reads through kernel AIO, 40000 per row
  QD       IOPS  mean lat (us)   syscalls   irqs per I/O
   1      25254           39.6      80000           1.00
   2      49101           40.7      60453           0.75
   4      86148           46.4      20026           0.25
   8      85408           93.7      15258           0.13
  16     147397          108.6      10457           0.08
  32     181573          176.2       5392           0.04
  64     209298          305.8       2950           0.02
```

The shape is the one an SSD's designers describe. From QD 1 to about QD 4 the **IOPS rise in proportion** (from 25,254 to about 90,000) while the latency stays at **about 40 µs**: the device serves several requests in parallel at no extra cost per request. Beyond QD 4 to 8 the device is saturated, the IOPS rise only slowly to 209,298 at QD 64, and the **latency climbs** (to about 300 µs at QD 64), because the extra requests are now **waiting in a queue**, not being served. The knee, where parallelism runs out, is the right queue depth for a latency-sensitive service; the plateau is the right one for a bulk job. The other columns tell a second story: the number of system calls falls from 80,000 at QD 1 (two per I/O: submit and wait) to about 3,000 at QD 64, and the **interrupts per I/O fall from 1.00 to 0.02**, because one interrupt now completes many requests. **Deeper queues amortise the per-I/O costs of the whole stack.**

The caveat for all of this is that the device is virtual, and the host's own queues and cache are in the path, so the numbers (the knee at 4 to 8, the saturation near 200,000 IOPS) belong to this VM. The shape and the method, a sweep of the queue depth with a counter for interrupts and system calls, belong to any machine, and `fio` (not installed here) does the same sweep with `--iodepth` (from memory).

---
## 7. The Linux block layer and its I/O schedulers

### 7.1 The path of one request, and merging

A `read` or `write` on a file goes through the **VFS**, then the **page cache** (or straight past it with `O_DIRECT`), then the **file system**, which turns a file offset into a block number on the device, then the **block layer**, then the **driver**, then the device; the completion comes back as an interrupt (section 1.2). The block layer's current design is **multi-queue (blk-mq)**, introduced because the older design had one request queue guarded by one lock, which scaled poorly on many cores through cache and lock contention (the kernel document, fetched). Its structure:

- **Software staging queues**, one per CPU or per NUMA node, each with its own lock, hold the requests that are not sent straight to the driver. This is where **adjacent requests are merged** and where an I/O scheduler gets to reorder them (the document notes that scheduling happens among requests in the same software queue).
- **Hardware dispatch queues** are the last stage before the driver takes ownership. Their number is bounded by the core count and by what the device offers, and a request that cannot be sent for lack of device resources waits on a dispatch list and is sent first when capacity returns.
- A **tag set** describes the driver's setup (its operations, the number of hardware queues and their depth). Each request gets an integer **tag** and the driver returns it on completion, which avoids a search to find the finished request.

**Merging, measured.** The program reads the first 64 MiB of the file as 16,384 sequential 4 KiB direct reads, keeping 32 in flight, and counts what the disk sees in `/proc/diskstats`. Then it sets `nomerges` to 2 (no merge lookups at all) and repeats:

```text
sequential 4 KiB reads at queue depth 32 over 64 MiB: does the block layer merge them?
  nomerges=0 (default)   I/Os issued 16384, requests the disk saw 1254, merged 15130, interrupts 832
  nomerges=2 (no merging) I/Os issued 16384, requests the disk saw 16603, merged 0, interrupts 569
```

With merging on, **16,384 small reads reached the disk as about 1,254 requests** (the block layer merged about 15,100 of them: about 13 pages per request); with merging off, the disk saw one request per read (16,603, which includes a few file-system metadata reads). That is the fixed per-request cost of section 2.4 being paid 13 times less often. The interrupt counts are in the output but are not a clean comparison, because how many completions share an interrupt depends on timing.

### 7.2 The schedulers

`/sys/block/<dev>/queue/scheduler` lists them: `none`, `mq-deadline`, `kyber` and `bfq` on this machine. The older single-queue schedulers (noop, deadline, CFQ) were replaced when the block layer became multi-queue (from memory). The tunables that follow were read on this machine from the sysfs files of a read-only disk switched to each scheduler in turn:

```text
== kyber
read_lat_nsec 2000000
write_lat_nsec 10000000
== bfq
back_seek_max 16384
back_seek_penalty 2
fifo_expire_async 250
fifo_expire_sync 125
low_latency 1
max_budget 0
slice_idle 8
slice_idle_us 8000
strict_guarantees 0
timeout_sync 124
== mq-deadline
async_depth 256
fifo_batch 16
front_merges 1
prio_aging_expire 10000
read_expire 500
write_expire 5000
writes_starved 2
```

- **`none`** does no reordering: requests go to the hardware queue as they arrive (the kernel document, fetched). The right choice when the device does its own scheduling and is fast enough that the kernel's sort would only add overhead, as on NVMe (the NVMe advice is from memory).
- **`mq-deadline`** keeps requests sorted by sector within each direction and gives each a **deadline** when it arrives: reads `read_expire` (here 500 ms) and writes `write_expire` (5,000 ms). It prefers reads, and dispatches writes after `writes_starved` (2) read preferences; requests go out in **batches** of up to `fifo_batch` (16) in sector order, and deadlines are checked **only between batches**; a value of 1 gives first-come-first-served and larger values raise throughput at the cost of more variable latency; `front_merges` (1) allows merging a request in front of an existing one. The deadline guarantee is the starvation fix of section 5.3: the sector-sorted order is the elevator, and the deadlines bound the wait. (These semantics are from the kernel's deadline-scheduler document, fetched; the document gives no default values, so the defaults above are measured.)
- **Kyber** (from memory) is a light scheduler for fast multi-queue devices. It does not sort. It limits how many requests of each kind (reads, writes) may be in flight, and adapts the limits so that observed completion latencies meet **targets**, which are the two tunables here: reads 2,000,000 ns (2 ms) and writes 10,000,000 ns (10 ms).
- **BFQ** (budget fair queueing; the kernel document, fetched) is a **proportional-share** scheduler with low-latency features. Each process gets a queue and a **weight** (from its I/O priority, or from the cgroup file `io.bfq.weight`, range 1 to 1000, default 100, so a group of weight 200 gets twice the bandwidth of one of weight 100). The device serves **one queue at a time**, and the queue gives up the device when its **budget** (in sectors) runs out, when it empties or when a timeout fires. Budgets adapt: sequential, I/O-bound queues get large ones for throughput, and sporadic latency-sensitive ones get small ones so they are served soon. By default it detects interactive and soft-real-time applications and **raises their weight** (`low_latency` 1 here). It also **idles on purpose**: `slice_idle` reads 8 ms, which is the anticipatory wait of section 5.3 (the tunable's meaning is from memory; the doc describes the idea). The document states the cost: about 1.9 µs of processing per request against 0.7 µs for `mq-deadline` on the machine it measured, about 10,500 lines of code against about 800, and a default that favours latency over throughput; setting `low_latency` to 0 gives maximum throughput. It reports up to 30 percent higher throughput than the old CFQ and up to 150 percent higher than deadline and `noop` for sequential hard-disk workloads, and about the same on random and flash workloads (the figures are the document's; I did not reproduce them).

### 7.3 An experiment: a reader against a bulk writer

The test that separates the schedulers is not throughput but **interference**. A bulk job writes 1 MiB direct writes, 8 in flight, sequentially and in a loop; at the same time a latency-sensitive reader issues random 4 KiB direct reads **one at a time** for 4 seconds and records every latency. The program does it under each scheduler in turn (switching `scheduler` in sysfs between runs, three rounds), and reports the reader's reads per second and its 50th, 99th and 99.9th percentile and maximum latency, and the writer's throughput (computed from the sectors written in `/proc/diskstats` over the window, so approximate). The first of the three rounds:

```text
none         reader:     122 reads/s  p50  1280.5  p99   2143.5  p99.9 6320267.1  max 6320267.1 us   writer:   1381 MB/s
mq-deadline  reader:      58 reads/s  p50 16909.7  p99  53536.5  p99.9  57560.5  max   57560.5 us   writer:    946 MB/s
kyber        reader:     111 reads/s  p50  9009.3  p99  16959.6  p99.9  27697.5  max   27697.5 us   writer:    934 MB/s
bfq          reader:    4449 reads/s  p50    42.2  p99    138.3  p99.9  32480.4  max  124723.9 us   writer:    717 MB/s
none         reader:       4 reads/s  p50  2647.0  p99 13739436.1  p99.9 13739436.1  max 13739436.1 us   writer:   2373 MB/s
mq-deadline  reader:      90 reads/s  p50 11414.3  p99  15332.7  p99.9  33453.0  max   33453.0 us   writer:    749 MB/s
kyber        reader:     149 reads/s  p50  6309.3  p99  14964.9  p99.9  17293.9  max   17293.9 us   writer:    716 MB/s
bfq          reader:    5160 reads/s  p50    37.3  p99    119.1  p99.9  26614.1  max  107433.4 us   writer:    703 MB/s
none         reader:       5 reads/s  p50  2355.8  p99 4961798.8  p99.9 4961798.8  max 4961798.8 us   writer:   1174 MB/s
mq-deadline  reader:      95 reads/s  p50 10948.3  p99  15168.2  p99.9  29368.8  max   29368.8 us   writer:    794 MB/s
kyber        reader:     157 reads/s  p50  5736.8  p99  13859.1  p99.9  14557.2  max   14557.2 us   writer:    762 MB/s
bfq          reader:    3981 reads/s  p50    39.2  p99   2486.0  p99.9  37930.7  max  102781.4 us   writer:    657 MB/s
```

(the program runs this order three times; only the first round is shown, and the ranges below cover all of them). Across the three rounds of the final run and two earlier development runs of the same program (eight rounds per scheduler in all), the ranges were:

| Scheduler | Reader's median latency | Reader's reads per second | What happened |
| --- | --- | --- | --- |
| `bfq` | **34 to 42 µs** | 3,800 to 7,300 | the reader runs almost as if the writer were not there; p99 about 120 to 160 µs in 5 of 8 rounds and 2.5 to 3 ms in the other 3 |
| `kyber` | 5.7 to 11 ms | about 80 to 160 | the reader waits behind the writer, bounded |
| `mq-deadline` | 11 to 23 ms | about 45 to 95 | the reader waits behind the writer's batches |
| `none` | 1.2 to 3.0 ms | 4 to 830 | see below |

The effect is a factor of **about 1,000** between the best and worst median latency, while the writer's throughput, outside the stalled `none` rounds, stayed within about 650 to 1,400 MB/s (a rough figure). In the stalled rounds the writer had the disk to itself and reached 2,400 MB/s. A scheduler's job on fast storage is to decide **who waits**, and this is what that decision looks like.

Two results need caveats. **`none` is bimodal.** Its median was 1.2 to 3.0 ms, but in **4 of the 8 `none` rounds** (all three of the final run's) the reader had at least one read that took **4 to 14 seconds**, and the reader's rate collapsed to 4 or 5 reads per second. I cannot see the cause from inside the guest: it is probably the host side of the virtual disk (a queue or a rate limit filling up when eight megabyte writes are in flight), and it belongs to this virtual disk with eight megabytes of writes in flight, not to `none` on a local NVMe drive. **The explanation for `mq-deadline` is a hypothesis.** The kernel document says dispatch goes in batches of `fifo_batch` requests and that deadlines are checked only between batches, so a read that arrives while a batch of 16 one-megabyte writes is being sent waits for it. The tuning run tests that:

```text
tuning, one run each
fifo_batch=1:  mq-deadline  reader:     419 reads/s  p50  1486.6  p99  12215.6  p99.9  13198.5  max   15326.6 us   writer:    737 MB/s
nr_requests=8: none         reader:      32 reads/s  p50  2721.5  p99 766046.1  p99.9 3377455.2  max 3377455.2 us   writer:    798 MB/s
```

Setting `fifo_batch` to 1 took the reader's median from about 11 to 23 ms to **1.5 ms in this session (1.9 ms in the one before)**, with the writer's throughput unchanged, which is what the batch explanation predicts. It is consistent with it and not a proof. The second line, `nr_requests` lowered to 8 under `none`, did not help in this session (the reader hit a multi-second stall, the same bimodal behaviour as above) and showed no gain in the previous one either; I did not find a setting of that knob that mattered.

### 7.4 Choosing a scheduler

The decision follows from the device and the workload, and the shape is stable even if the numbers move:

| Situation | Choose | Why |
| --- | --- | --- |
| Fast NVMe, one main workload | `none` | the device schedules itself, the kernel's sort and per-request cost buy nothing (from memory) |
| Rotating disk, server mix | `mq-deadline` | sector-sorted batches (the elevator) with a deadline against starvation; low overhead (from memory and the doc) |
| Desktop, mixed or interactive load, VMs, shares between users | `bfq` | per-process fairness and low latency under a bulk writer (measured above; the use cases are the document's) |
| Fast multi-queue device with latency targets | `kyber` | throttles by observed latency, low overhead (from memory) |

Changing it is one write to the sysfs file, and a persistent choice is made with a udev rule or a kernel parameter (from memory). Some surrounding controls: the **I/O priority** of a process (`ionice`) and the cgroup I/O controller feed BFQ's weights (the document describes the weight formula `(IOPRIO_BE_NR - ioprio) * 10` and the cgroup files); `read_ahead_kb` (section 2.3); `max_sectors_kb`; and **write-back throttling** (`wbt_lat_usec`, 75,000 µs here), which slows writers when read latency exceeds its target. The write side of the cache is governed by the dirty-page limits, which on this machine are `vm.dirty_background_ratio` 10 (start background write-back above 10 percent of memory), `vm.dirty_ratio` 20 (writers are forced to wait above 20 percent) and `vm.dirty_expire_centisecs` 3,000 (a dirty page older than 30 seconds is written; the meanings are from memory). They are the subject of OS 16.

---

## 8. Reading the counters, and diagnosing I/O

### 8.1 `/proc/diskstats` and what `iostat` computes from it

The kernel counts I/O per device in `/proc/diskstats`, and `iostat -x` (not installed here; from memory) turns the counters into rates over an interval. The fields the programs read, and the derived quantities:

| Counter | Derived | Meaning |
| --- | --- | --- |
| reads (and writes) completed | `r/s`, `w/s` (IOPS) | requests the **device** saw, after merging |
| reads (and writes) merged | `rrqm/s` | requests that were merged into others |
| sectors read (and written), of 512 bytes | `rkB/s`; sectors per request | throughput and average request size |
| milliseconds spent reading (writing) | `r_await = ms / completed` | average time per request, queue plus service |
| weighted milliseconds in the queue | `aqu-sz = weighted ms / interval` | average queue depth (Little's law again) |
| milliseconds the device was busy | `%util = busy ms / interval` | **share of time with at least one request in flight** |

The last row is the trap: `%util` of 100 percent means the device was never idle, which for a hard disk (one request at a time) means saturated and for an SSD or a virtual disk serving many requests in parallel (section 6.3) means **nothing about spare capacity**. A device at QD 1 and a device at QD 64 both read 100 percent.

### 8.2 Symptoms, causes and instruments

| Symptom | Likely cause | How to check |
| --- | --- | --- |
| high `r_await`, low `aqu-sz` | the device or the host behind it is slow per request | compare with a direct-read latency test; check the host |
| `aqu-sz` high, IOPS flat | the device is saturated: the queue is the delay | the QD sweep of section 6.3; reduce load or add devices |
| many small requests, few merged (low sectors per request) | small, scattered I/O | batch writes, raise readahead, use larger blocks, check `nomerges` |
| read latency spikes during large writes | no scheduler, wrong scheduler, or dirty-page flush storm | the experiment of section 7.3; `bfq` or `fifo_batch`; lower `vm.dirty_ratio` |
| processes in state `D` (uninterruptible sleep), CPU idle | waiting on I/O that is not finishing | `ps`, `/proc/PID/stack`, the counters above |
| high interrupt rate on one CPU | completions all land on one CPU | `/proc/interrupts`; `rq_affinity`; the interrupt's affinity mask |
| sequential reads far below the device's rate | readahead too small, or requests too small | section 2.3 and 2.4 |

The honest instrument list for a real investigation is `iostat -x`, `/proc/diskstats`, `/proc/interrupts`, `blktrace` or the block tracepoints for per-request timing, and `fio` for a controlled test (from memory; none of the four tools is installed here, so these are described, not run).

---
## What it costs

| Mechanism | What it costs | What it buys | Evidence |
| --- | --- | --- | --- |
| Polling | a CPU spinning for the whole wait | no interrupt or context-switch cost, lowest latency on a fast device | measured: a 100 ms wait cost 100 ms of CPU and about 800,000 system calls |
| Interrupts | one interrupt and a wake-up per completion (at queue depth 1) | the CPU free during the wait | measured: 1.00 interrupt per direct read at depth 1, 0.02 at depth 64 |
| DMA | a setup and one interrupt | the CPU out of the data path | OSTEP (fetched); the device-side copy is not visible to the counters |
| One-byte `write` calls | a system call per byte | nothing | measured: 222 to 302 ns per byte against 0.4 at 4 KiB, more than 500 times |
| Page cache | memory, and a second copy for programs with their own cache | hits at about 1 µs instead of about 33 µs | measured: 1.1 against 33.7 µs per random 4 KiB read |
| Readahead (128 KiB to 4 MiB) | wasted bandwidth when access is random | sequential reads at the device's rate | measured: 79 to 97, 686 to 1,386, 2,999 to 3,364 MB/s for 0, 128 KiB, 4 MiB |
| Small requests | a fixed cost of about 40 µs per request here | none | measured: 106 MB/s at 4 KiB against 2,579 at 1 MiB |
| `select` or `poll` over N descriptors | linear in N per call | works everywhere | measured: 233,920 ns per `poll` at N = 10,000 |
| `epoll_wait` | an `epoll_ctl` per descriptor, once | flat cost, about 200 ns, at any N | measured: 203 to 282 ns from N = 10 to 10,000 |
| Non-blocking spin | CPU | nothing alone | measured as above |
| Kernel AIO against `io_uring` | 2 system calls per batch against 1 | fewer crossings | measured: about 5,800 to 6,100 against 1,500 to 3,000 calls per 40,000 reads |
| A hard disk's random access | seek plus rotation, 6 to 13 ms | none | computed: 99.7 percent of a random 4 KiB read |
| FCFS head scheduling | about 67 cylinders per request | fairness | simulated |
| SSTF | starvation of far requests | the least movement | simulated: never served the far request in 100,000 services |
| SCAN family | the return trip, uneven waits | bounded wait | simulated: far request served after 6 to 7 |
| Deep queues (QD 32 to 64) | latency 170 to 305 µs against 40 | the device's full IOPS | measured: 25,254 to 209,298 IOPS |
| Merging | a lookup per request | 16,384 reads became about 1,250 requests | measured |
| BFQ | about 1.9 µs per request (document), deliberate idling | a reader's median at about 40 µs under a bulk writer | measured; overhead figure from the kernel document |
| `mq-deadline` with `fifo_batch` 16 | batches that delay reads behind writes | high throughput | measured: reader median 11 to 23 ms; 1.5 to 1.9 ms at `fifo_batch` 1 |

## Common mistakes

1. **Spinning on a non-blocking descriptor.** It burns a core to learn nothing; use `epoll`, `poll` or a blocking call.
2. **Using `select` for a large server.** It rebuilds its sets on every call, scans linearly, and cannot watch descriptor numbers of 1024 or more.
3. **Edge-triggered `epoll` without non-blocking descriptors and a drain-to-`EAGAIN` loop.** It can stall with data waiting.
4. **Expecting `epoll` to make disk reads asynchronous.** Regular files are always "ready"; use kernel AIO with `O_DIRECT`, `io_uring` or a thread pool.
5. **Calling any of the middle three models "asynchronous".** In the strict sense only the kernel doing both phases and reporting completion is asynchronous.
6. **Trusting `rotational`.** It is a default; a virtual disk reports 1 and has no seek penalty. Benchmark.
7. **Reading `%util` as "headroom".** 100 percent means the device was never idle, and on a device that serves many requests at once it says nothing about spare capacity.
8. **Optimising head movement on an SSD.** There is no head. The levers are queue depth, request size, merging and write patterns.
9. **Issuing small unbuffered I/O.** One byte per system call costs more than 500 times as much per byte as a 4 KiB write.
10. **Using `O_DIRECT` with unaligned buffers, offsets or lengths.** It fails with `EINVAL`; align to the device's block size.
11. **Choosing a scheduler by name and not by workload.** The same bulk writer gave a reader 40 µs under BFQ and 11 to 23 ms under `mq-deadline`.
12. **Forgetting the starvation of greedy policies.** SSTF is optimal per step and unbounded in waiting time; any policy needs a bound on the wait.

## Questions out loud

1. *Polling, interrupts and DMA: how does the CPU deal with a device?* Polling spins on a status register and wastes the CPU while a slow device works; interrupts let the CPU run something else and be told when the device finishes; DMA lets the device move the data itself, so the CPU only sets up the transfer and takes one completion interrupt.
2. *When is polling better than interrupts?* When the device is so fast that an interrupt and its context switches cost more than the wait, or when an interrupt flood would starve user code (livelock); a hybrid polls briefly and then sleeps.
3. *What is a device driver and why are drivers the buggiest part?* The device-specific code behind a generic interface; it runs in the kernel against imperfect, concurrent hardware, it is most of the kernel's code (OSTEP cites over 70 percent), and its bugs are kernel bugs.
4. *Block device against character device?* A block device is addressed in blocks, randomly, with the kernel caching and scheduling requests (a disk); a character device is a byte stream with no such layer (a terminal, `/dev/urandom`). `stat` shows the type and the major and minor numbers.
5. *Name the five I/O models and say which are asynchronous.* Blocking, non-blocking, multiplexed, signal-driven, asynchronous. In the first four the caller does the copy itself; only the last (AIO, `io_uring`) has the kernel do the whole operation and report completion.
6. *Why does `epoll` scale and `select` not?* `select` and `poll` pass the whole set on each call and the kernel scans it, so the cost is linear in the number of descriptors (measured: 233,920 ns for `poll` at 10,000); `epoll` keeps the interest list in the kernel and returns only the ready ones (about 200 ns at any size).
7. *Level-triggered against edge-triggered?* Level reports a descriptor for as long as it is ready; edge reports a change, so you must use non-blocking descriptors and read until `EAGAIN`, and guard against starving the others.
8. *Why can `epoll` not do asynchronous disk I/O, and what can?* It reports readiness, and regular files are always ready. Kernel AIO with `O_DIRECT`, or `io_uring`, submits the operation and delivers a completion; `io_uring` does it with shared rings and one system call per batch.
9. *Compute the access time of a disk.* Seek plus half a rotation plus the transfer; for 7,200 rpm, 9 ms seek and 105 MB/s, a random 4 KiB read is about 13.2 ms, 0.31 MB/s, against 105 MB/s sequential: about 340 times.
10. *Run SCAN, C-SCAN and LOOK on a queue.* Sweep in one direction serving on the way; SCAN goes to the edge and back, C-SCAN goes to the edge and jumps to the start, LOOK turns at the last request; on the classic queue (head 53) the totals are 331, 382 and 299.
11. *Why does SSTF starve, and what is the fix?* It always takes the nearest request, so a steady stream near the head can postpone a far one forever; use a sweep (SCAN family) or deadlines to bound the wait.
12. *Why does seek scheduling not apply to an SSD, and what matters?* There is no head; random and sequential reads cost about the same per request. What matters is queue depth (parallel flash channels), request size and merging, and write patterns (erase-before-program, garbage collection, write amplification).
13. *Which Linux I/O scheduler for which device?* `none` for fast NVMe, `mq-deadline` for rotating disks and general servers (sorted batches plus deadlines), BFQ for interactive or shared workloads that need fairness and low latency, Kyber for fast multi-queue devices with latency targets (the NVMe, HDD and Kyber advice is from memory).
14. *A disk shows 100 percent `%util`: is it saturated?* Not necessarily. `%util` is the share of time with at least one request in flight; look at the queue depth and whether the IOPS still rise when the depth does.

---

## Problems

**Where to solve them.** Paper for all five. To check B and C, change the queue and head in `d2_sched` (the array `q` and the head argument in `main`) and run it; to check D, `/proc/diskstats` on any Linux machine has the fields of section 8.1, and `iostat -x` prints the derived columns. E needs only arithmetic.

**How to approach them.** For A, write seek, half a rotation and the transfer in milliseconds before dividing. For B and C, list the head's path as a sequence of positions and add the absolute differences; for a sweep, write where it turns. For D, convert each counter to a per-second figure first, then use the definitions of section 8.1. For E, find the smaller of the queue depth and the number of parallel units, then use `latency = QD / IOPS`.

### Problem A (E, entry level): access time

A 10,000 rpm drive has an average seek of 6 ms and a transfer rate of 150 MB/s. (a) The average rotational delay. (b) The time and the rate (MB/s) of one random 4 KiB read. (c) The time to read 100 MB sequentially, counting one seek and one half rotation. (d) The ratio of the sequential rate to the random rate.

### Problem B (E, entry level): FCFS and SSTF

A disk has 200 cylinders (0 to 199), the head is at **50**, and the queue, in arrival order, is **82, 170, 43, 140, 24, 16, 190**. Give the total head movement and the order of service for (a) FCFS and (b) SSTF.

### Problem C (M, medium): the sweeps

For the queue and head of Problem B, with the head moving **upward first**, give the total movement for (a) SCAN, (b) LOOK, (c) C-SCAN and (d) C-LOOK, counting the return trip of the circular ones, and also without it. (e) Which of the four gives the most uniform waiting time, and which would you choose for a disk under heavy load?

### Problem D (M, medium): read the counters

Over a 10-second interval a disk's counters changed by: reads completed 50,000; reads merged 150,000; sectors read (512 bytes each) 3,200,000; milliseconds spent reading 400,000. Compute (a) the read IOPS, (b) the number of read requests the file system submitted before merging, and the fraction merged, (c) the average request size in KiB, (d) the read throughput, (e) the average read time per request, (f) the average queue depth, and (g) say whether these numbers show that the device is saturated.

### Problem E (H, hard): the queue-depth model

A flash device has 8 independent units, each of which serves one 4 KiB read in 80 µs. Model it as `IOPS(QD) = min(QD, 8) / 80 µs` with the mean latency from Little's law. (a) Fill in the IOPS and the mean latency for QD of 1, 4, 8, 16 and 64. (b) A service needs 60,000 IOPS with a mean latency below 100 µs: what is the smallest queue depth that works, and what do you get? (c) Why is a queue depth of 32 a poor choice for that service? (d) The device measured in section 6.3 had a latency of about 40 µs below QD 4 and saturated near 200,000 IOPS: what units and service time does the model need to fit that, and does it fit the measured curve?

---

## Answers

**A.** (a) One revolution takes `60 / 10,000 = 6` ms, so the average delay is half of it: **3 ms**. (b) The transfer is `4096 / 150,000,000 = 0.0273` ms, so `T = 6 + 3 + 0.0273 =` **9.03 ms**, and the rate is `4096 / 0.00903 s =` **0.454 MB/s**. (c) `100 MB / 150 MB/s = 667` ms of transfer, plus 6 and 3 ms: **676 ms**, a rate of **148 MB/s**. (d) `150 / 0.454 =` **about 330 times**.

**B.** (a) FCFS: `50 to 82 = 32`, `82 to 170 = 88`, `170 to 43 = 127`, `43 to 140 = 97`, `140 to 24 = 116`, `24 to 16 = 8`, `16 to 190 = 174`; total **642**, in arrival order. (b) SSTF: from 50 the nearest is 43 (7); from 43, 24 (19); from 24, 16 (8); from 16 the nearest remaining is 82 (66); then 140 (58), 170 (30), 190 (20). Total `7 + 19 + 8 + 66 + 58 + 30 + 20 =` **208**, in the order 43, 24, 16, 82, 140, 170, 190. SSTF moves a third as far as FCFS here.

**C.** Requests above the head: 82, 140, 170, 190; below: 43, 24, 16. (a) SCAN: up to the edge, `199 - 50 = 149`; down to 16, `199 - 16 = 183`; total **332**. (b) LOOK: up to 190, `140`; down to 16, `174`; total **314**. (c) C-SCAN: up to 199 (149), jump to 0 (199), up to 43 (43): **391** with the return and **192** without. (d) C-LOOK: up to 190 (140), jump to 16 (174), up to 43 (27): **341** with the return and **167** without. (e) The circular ones give the most uniform waiting, since every request is reached by a sweep in the same direction, so a request just behind the head waits for the return and the next sweep and not for a double pass; under heavy load **C-LOOK** (or C-SCAN) is the usual choice, accepting the return trip for the uniform wait. The totals rank LOOK, SCAN, C-LOOK, C-SCAN, and SSTF's 208 is the lowest of all and is the one that can starve.

**D.** (a) `50,000 / 10 =` **5,000 IOPS**. (b) The file system submitted `50,000 + 150,000 = 200,000` requests, so **75 percent** were merged (20,000 per second before merging). (c) `3,200,000 x 512 / 50,000 = 32,768` bytes, **32 KiB** per request. (d) `3,200,000 x 512 / 10 s =` **163.8 MB/s**. (e) `400,000 ms / 50,000 =` **8 ms** per request. (f) The time spent by all requests, 400,000 ms, over the 10,000 ms interval: **40** requests in flight on average (Little's law: `5,000 x 0.008 = 40`). (g) **Not from these numbers alone.** A queue depth of 40 and 8 ms per request is what a saturated hard disk looks like, but it is also what a flash array looks like when the application keeps 40 requests in flight; to decide, compare with a depth sweep (does the IOPS still rise when the depth does?) and look at the device type. The 75 percent merge rate says the workload is sequential and the block layer is doing its work.

**E.** (a) QD 1: IOPS `1 / 80 µs =` **12,500**, latency **80 µs**. QD 4: **50,000**, 80 µs. QD 8: **100,000**, 80 µs. QD 16: IOPS capped at **100,000**, latency `16 / 100,000 =` **160 µs**. QD 64: **100,000**, **640 µs**. (b) The IOPS needed is 60,000, so `QD / 80 µs >= 60,000` gives `QD >= 4.8`, so **QD 5**: 62,500 IOPS, latency 80 µs, under the 100 µs limit. QD 4 gives only 50,000. (c) At QD 32 the device is already saturated at 100,000 IOPS, which the service does not need, so the extra requests only wait: the latency is `32 / 100,000 = 320` µs, over three times the limit, for no added throughput. (d) A saturation of 200,000 IOPS with a latency of 40 µs per request needs `units = 200,000 x 40 µs =` **8 units** (the same 8, with a service time of 40 µs). The model then predicts flat 40 µs up to QD 8 and IOPS rising in proportion (25,000 at QD 1, 100,000 at QD 4, 200,000 at QD 8). The measured curve has a similar knee at QD 4 to 8 but is softer: 25,254 at QD 1 and 86,148 at QD 4 (a little below the model's 100,000), 181,573 at QD 32 and 209,298 at QD 64, with the latency starting to rise at QD 4 (46 µs). So the model gets the **shape** and the knee right and is about 15 percent off in the middle: a real device has queueing inside it before every unit is busy.

---

## Cheat sheet

- Device model: status, command and data registers; port-mapped (`in` and `out`, privileged) or memory-mapped (loads and stores). Polling spins; interrupts sleep and wake; hybrid polls then sleeps; livelock under an interrupt flood; coalescing trades latency for fewer interrupts. DMA moves the data, one interrupt at the end.
- Driver: device-specific code behind a generic interface; most of the kernel's code and many of its bugs. Block devices (disks: cached, scheduled, random access) against character devices (byte streams). `stat`: type, major (driver), minor (device).
- Buffer: data in transit between mismatched parties (speed, size, copy semantics); cache: a second copy for faster reuse; spool: a durable queue for a slow, non-sharable device. Measured: one-byte writes 222 to 302 ns per byte against 0.4 at 4 KiB; page-cache hit 1.1 µs against 33.7; readahead took 79 to 97 to 2,999 to 3,364 MB/s.
- Request cost: fixed (about 40 µs here) plus size over bandwidth; large and merged requests reach the device's rate (106 MB/s at 4 KiB, about 2.6 GB/s at 1 MiB).
- Five models: blocking, non-blocking (poll it yourself), multiplexed (`select`, `poll`, `epoll`), signal-driven (`SIGIO`), asynchronous (kernel does both phases: AIO, `io_uring`). Only the last is asynchronous in the strict sense.
- `select` bitmaps (limit 1024), rebuilt per call, linear; `poll` array, linear; `epoll` kernel interest list, flat about 200 ns (measured at N from 10 to 10,000 against 233,920 ns for `poll`). Level against edge (`EPOLLET`: non-blocking and read to `EAGAIN`); `EPOLLONESHOT`; `EPOLL_CTL_DEL` before closing a duplicated descriptor. `epoll` cannot make regular-file reads asynchronous.
- `io_uring`: shared submission and completion rings, one `io_uring_enter` per batch, `SQPOLL` to skip it, `user_data` to match out-of-order completions. Measured 1,500 to 3,000 calls against 5,800 to 6,100 for kernel AIO per 40,000 reads.
- Disk access time: `T = seek + half a rotation + transfer`; 7,200 rpm: 9 + 4.17 + 0.04 = 13.2 ms, 0.31 MB/s random against 105 MB/s sequential (about 340 times). Sequential and large requests are the only fast way.
- Scheduling on the classic queue (head 53; 98 183 37 122 14 124 65 67; 200 cylinders, upward first): FCFS 640, SSTF 236, SCAN 331, C-SCAN 382 (183 without the return), LOOK 299, C-LOOK 322 (153 without). SSTF starves; the sweeps bound the wait; C versions give uniform waits; LOOK versions skip the unused edge. F-SCAN freezes the queue; SPTF needs the drive; anticipatory scheduling idles on purpose (BFQ `slice_idle` 8 ms).
- Flash: read a page (25 to 75 µs, OSTEP), program a page, erase a block (ms); overwrite needs erase; FTL is log-structured with a mapping table, garbage collection, wear levelling, write amplification, trim, overprovisioning. No seek: what matters is queue depth, request size, merging and write patterns. Measured on the virtual disk: QD 1 to 64: 25,254 to 209,298 IOPS, latency about 40 µs flat to QD 4 then 305 µs at QD 64, interrupts per I/O 1.00 to 0.02, no sequential against random gap (the `rotational` flag is a default).
- Block layer (blk-mq): software staging queues per CPU (merge, schedule), hardware dispatch queues, tag sets; merging measured 16,384 sequential 4 KiB reads to about 1,250 requests. `nomerges` 0, 1, 2.
- Schedulers: `none` (no reordering; fast NVMe), `mq-deadline` (sector-sorted batches, deadlines `read_expire` 500 ms, `write_expire` 5,000 ms, `writes_starved` 2, `fifo_batch` 16), Kyber (latency targets 2 ms and 10 ms), BFQ (weights, budgets, `slice_idle`, low-latency mode, `io.bfq.weight`). Measured reader under a bulk writer: BFQ 34 to 42 µs, Kyber 5.7 to 11 ms, `mq-deadline` 11 to 23 ms (1.5 to 1.9 ms at `fifo_batch` 1), `none` 1.2 to 3.0 ms with multi-second stalls in 4 of 8 rounds.
- Counters: `/proc/diskstats` gives requests (after merging), merged, sectors, time; derive IOPS, size, `await`, queue depth (Little). `%util` is not headroom. Write-back limits here: background 10 percent, hard 20 percent, expiry 30 s.

## Sources

Measured on the VM described above (a virtual disk; the host's behaviour is in every number). Fetched and used: the three OSTEP chapters, the four kernel documents and the two manual pages below. The pages were summarised by the fetch tool, not read in full by me, so the figures from them (OSTEP's latencies, drive figures and table of SSD against disk performance; the kernel documents' tunables, BFQ overhead and throughput claims) are as that summary reported them, and I did not check them independently. The first address I tried for the sysfs queue document failed, and the second worked. The kernel's deadline document does not give default values, so the defaults used here were read from the machine. The Kyber design, the NVMe scheduler advice, the NCQ, SQPOLL-on-fast-NVMe and NAPI remarks, the Stevens classification, and the `fio`, `iostat` and `blktrace` descriptions are from memory and were not fetched or run; the `epoll` and `io_uring` internals beyond the manual pages are from memory. OS 15 (file systems) and OS 16 (crash consistency and the page cache) continue from here and are not yet written.

- [OSTEP: I/O Devices](https://pages.cs.wisc.edu/~remzi/OSTEP/file-devices.pdf), fetched: the canonical device model, polling, interrupts, DMA, PIO and memory-mapped I/O, drivers
- [OSTEP: Hard Disk Drives](https://pages.cs.wisc.edu/~remzi/OSTEP/file-disks.pdf), fetched: the access-time formula and drive figures, SSTF, elevator, F-SCAN, C-SCAN, SPTF, merging, work-conserving scheduling
- [OSTEP: Flash-based SSDs](https://pages.cs.wisc.edu/~remzi/OSTEP/file-ssd.pdf), fetched: flash structure and latencies, the FTL, garbage collection, wear levelling, the SSD against disk table
- [Linux kernel: Multi-Queue Block IO Queueing Mechanism (blk-mq)](https://docs.kernel.org/block/blk-mq.html), fetched: software and hardware queues, tag sets, why it replaced the single queue
- [Linux kernel: Deadline IO scheduler tunables](https://docs.kernel.org/block/deadline-iosched.html), fetched: batches, expiry, `writes_starved`, `fifo_batch`, `front_merges`
- [Linux kernel: BFQ (Budget Fair Queueing)](https://docs.kernel.org/block/bfq-iosched.html), fetched: budgets, weights, low-latency mode, overhead, throughput claims, cgroup weights
- [Linux kernel: Queue sysfs files](https://www.kernel.org/doc/html/latest/block/queue-sysfs.html), fetched: meanings of `rotational`, `scheduler`, `nr_requests`, `read_ahead_kb`, `nomerges` and the others
- [epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html), fetched: the interest and ready lists, level and edge triggering, `EPOLLONESHOT`, the pitfalls
- [io_uring(7)](https://man7.org/linux/man-pages/man7/io_uring.7.html), fetched: the shared rings, `io_uring_setup` and `io_uring_enter`, `SQPOLL`
- [OS 11: Virtual memory](11_virtual_memory.md), [OS 07: Condition variables and semaphores](07_condition_variables_and_semaphores.md), [OS 03: Threads](03_threads.md), [CO 26: Interrupts, exceptions and DMA](../04_computer_organization/26_interrupts_exceptions_and_dma.md) and [CO 17: Storage and reliability](../04_computer_organization/17_storage_and_reliability.md), the neighbouring material
