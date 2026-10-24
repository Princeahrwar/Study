# Linux Kernel Internals: Track Plan

> **Track code:** K · **Weeks:** 14–18 and 20 · **Budget:** ~30 hours · **Prerequisites:** the whole `05_operating_systems` track; C 03–06

The OS track teaches what an operating system must do. This track opens the Linux source and shows how it actually does it: the system-call path, `task_struct`, the buddy and slab allocators, spinlocks and RCU, softirqs and workqueues, the VFS, and the tools kernel engineers use to debug all of it. Every topic has a lab on a kernel you build yourself and boot in QEMU.

The `09_drivers` track runs interleaved with this one from week 15; drivers use everything here.

---

## 1. Why this track matters

Kernel knowledge decides system-software and embedded-Linux roles: kernel memory allocation, locking in interrupt context, DMA, `mmap` of device memory, debugging oopses, the scheduler and memory management, kernel configuration and boot, and data-plane performance (softirqs, per-CPU data, RCU-protected tables, XDP). Infrastructure teams add eBPF and the debugging tools.
---

## 2. What you will be able to do after this track

1. Configure, build and boot your own kernel in QEMU, and attach `gdb` to it.
2. Trace a system call from the user-space instruction to the kernel function and back.
3. Explain `task_struct`, how `fork` and `clone` create tasks, kernel threads, and how the scheduler classes plug in.
4. Explain the buddy allocator, SLUB, `kmalloc` vs `vmalloc`, GFP flags, and what happens under memory pressure up to the OOM killer.
5. Choose the right kernel lock for a given context (process, softirq, hard IRQ), and explain RCU well enough to use it correctly.
6. Choose between softirqs, tasklets, workqueues and threaded IRQs, and use timers and hrtimers.
7. Use `list_head`, rbtrees and the XArray, and explain the VFS objects (superblock, inode, dentry, file).
8. Decode an oops, and use `printk`, dynamic debug, ftrace, kprobes, KASAN, lockdep and kdump.

---

## 3. Topics

Topic 05 is an older, Staff-level note; it is rewritten in the same detailed style as the new tutorials before you study it.

| # | File | Topic | Key subtopics | Status | Week |
|---|---|---|---|---|---|
| 01 | `01_kernel_architecture_and_building_in_qemu.md` | Kernel architecture, and building it in QEMU | Source tree tour, subsystems, Kconfig and `make menuconfig`, building `bzImage`/`Image`, a BusyBox initramfs, booting in QEMU for x86-64 and arm64, attaching `gdb` to the running kernel; fills `99_toolchain/` | To write | 14 |
| 02 | `02_system_calls_and_process_management.md` | System calls and process management | The syscall entry path and table, `SYSCALL_DEFINE`, `task_struct` and `thread_info`, `current`, `fork`/`clone`/`exec` inside the kernel, kernel threads, scheduler classes and `schedule()`, wait queues; lab: add a system call | To write | 15 |
| 03 | `03_kernel_memory_management.md` | Kernel memory management | Physical memory, zones and nodes, `struct page` and folios, the buddy allocator, SLUB caches, `kmalloc` vs `vmalloc` vs `alloc_pages`, GFP flags and atomic context, the page cache, reclaim, kswapd and the OOM killer | To write | 16 |
| 04 | `04_kernel_synchronization.md` | Kernel synchronisation | Atomics and bitops, spinlocks and the `irqsave` variants, mutexes, semaphores, RW locks, completions, memory barriers in the kernel, preemption and its interaction with locks, which lock in which context | To write | 17 |
| 05 | [`05_rcu_and_kernel_sync.md`](05_rcu_and_kernel_sync.md) | RCU, seqlocks, per-CPU data and lockdep | The primitive menu, RCU's idea and grace periods, seqlocks, per-CPU variables, lockdep | Exists, rewrite due | 17 |
| 06 | `06_deferred_work_and_time.md` | Deferred work and time | Interrupt vs process context, softirqs and `ksoftirqd`, tasklets (and why they are being phased out), workqueues and `cmwq`, threaded IRQs, jiffies, timers and hrtimers, `ktime` | To write | 18 |
| 07 | `07_kernel_data_structures_vfs_and_block.md` | Kernel data structures, VFS and the block layer | `list_head` and `container_of`, `hlist`, rbtree, XArray and the maple tree, kfifo; VFS objects (superblock, inode, dentry, file) and the dentry cache; the block layer and `blk-mq` at a glance | To write | 20 |
| 08 | `08_kernel_debugging.md` | Kernel debugging | `printk` and log levels, dynamic debug, decoding an oops and `addr2line`, ftrace and function graphs, kprobes and eBPF, KASAN, KCSAN, lockdep reports, kgdb, kdump and `crash` | To write | 20 |

Related: DSA trees topics 13 (the kernel rbtree) and 31 (trees inside the kernel) in `01_dsa/07_trees/`.

---

## 4. Labs

Every lab runs on the kernel you build in K 01, inside QEMU, so a crash costs nothing.

| Lab | Topic | What you build or observe |
|---|---|---|
| `labs/qemu_kernel/` | 01 | Script that builds a kernel and BusyBox initramfs and boots it in QEMU in under a minute; `gdb` attached with `lx-symbols` |
| `labs/new_syscall/` | 02 | Add a system call that returns per-task statistics, and call it from user space with `syscall()` |
| `labs/slab_explorer/` | 03 | A module that creates a slab cache, allocates under memory pressure, and reports through `/proc/slabinfo` and `slabtop` |
| `labs/lock_contexts/` | 04, 05 | A module with a deliberate lock bug (sleeping in atomic context, wrong IRQ variant); let lockdep and `CONFIG_DEBUG_ATOMIC_SLEEP` catch it; then an RCU-protected list with concurrent readers |
| `labs/deferred_work/` | 06 | Schedule work from an hrtimer callback through a tasklet, a workqueue and a threaded handler; measure latency of each |
| `labs/oops_decoding/` | 08 | Trigger a NULL dereference in a module, decode the oops to the source line, then find the same bug with KASAN |

---

## 5. Representative interview questions

1. Walk through what happens when user space calls `write()` until the kernel function runs.
2. What is `current`, and how does the kernel find it on x86-64 and arm64?
3. What is the difference between `kmalloc` and `vmalloc`? When must you use `GFP_ATOMIC`?
4. Why can you not sleep while holding a spinlock? What happens if you do?
5. When do you need `spin_lock_irqsave` instead of `spin_lock`?
6. Explain RCU. Why are readers almost free, and what does a writer have to wait for?
7. Softirq vs tasklet vs workqueue vs threaded IRQ: which would you choose for a network driver's RX processing, and why?
8. What is `container_of` and how does it work?
9. What do the inode, dentry and file objects each represent? What does the dentry cache save?
10. Your module crashes the kernel. Walk through how you find the faulting line.
11. What does lockdep detect, and how does it detect a deadlock that has not happened yet?
12. How does the kernel decide which process the OOM killer kills?

---

## 6. How to study this track

1. Follow the five-step method in [`../README.md`](../README.md).
2. Read the actual source for every structure named in a tutorial. Use elixir.bootlin.com to navigate it in the browser; tutorials name the kernel version they describe.
3. Do every lab inside QEMU. Kernel bugs on your real machine cost a reboot; in QEMU they cost a second.
4. This track feeds Project 02 (`rkdma`), which starts in week 15 alongside it.

---

## 7. Resources

| Resource | Use it for |
|---|---|
| Bootlin "Linux kernel and driver development" training slides (free) | Up-to-date, practical coverage of the whole track |
| kernel.org documentation (`Documentation/`) | The primary source for every subsystem |
| elixir.bootlin.com | Navigating the source with cross-references |
| Linux Kernel Labs (linux-kernel-labs.github.io) | Exercises with QEMU, close to this track's labs |
| *Linux Kernel Development* (3rd ed.), Robert Love | Clear explanations of the core; dated APIs, so check against the source |
| *Understanding the Linux Kernel* (3rd ed.), Bovet & Cesati | Depth on memory and processes; also dated |
| LWN.net kernel articles | Current design (folios, maple tree, EEVDF, RCU series by Paul McKenney) |

---

## 8. Progress

| # | Topic | Status |
|---|---|---|
| 01–08 | All topics | 01 is next (week 14); 05 is rewritten when reached |

Cross-track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
