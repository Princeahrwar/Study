# CO 26: Interrupts, Exceptions and DMA

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (3.5 reading, 1.5 problems) · You need: [tutorial 09](09_pipelining_and_hazards.md) (section 9: precise exceptions), [tutorial 15](15_virtual_memory_and_the_tlb.md) (section 7: page faults), [tutorial 19](19_cache_coherence.md) and [tutorial 20](20_memory_consistency_and_atomics.md) (non-coherent devices, memory ordering), [tutorial 07](07_assembly_in_practice.md) (the system-call registers) and [tutorial 25](25_buses_pcie_and_io_mapping.md) (MMIO, doorbells, MSI-X, DMA)*

By the end of this tutorial you can:

- tell an interrupt from a fault, a trap and an abort, say which are synchronous, and say where the saved program counter points in each case (at the faulting instruction or after it);
- say step by step what the hardware does when an event arrives on x86-64, AArch64 and RISC-V (what is saved and where, which stack, how the handler is found, which instruction returns), and read the vector number and error code that a fault produces;
- explain privilege levels (rings, EL0 to EL3, M/S/U mode), what a system call costs, and why a fault delivered to a signal handler costs ten times more;
- trace a device interrupt through the interrupt controller (8259, I/O APIC and local APIC, GIC, PLIC): priority, masking, level against edge triggering, end of interrupt, affinity, and inter-processor interrupts;
- compute interrupts per second for a packet rate, explain interrupt moderation, NAPI and receive livelock, and decide when polling beats interrupts;
- explain how timers and clocks work (tick, tickless, TSC, the vDSO, the Arm generic timer, RISC-V `mtime`) and compare timestamps without being bitten by counter wrap-around;
- describe DMA: scatter-gather, descriptor rings with producer and consumer indices, ownership bits and doorbells, the barriers that make them correct, and the cache maintenance that non-coherent systems need;
- follow a packet from the wire to your program and back (receive and transmit rings, receive-side scaling with the Toeplitz hash, offloads, DDIO), and an NVMe or virtio request through its queues;
- recognise the usual symptoms (jitter, one busy core, drops with idle CPUs, stale data after DMA) and say where to look.

Tutorial 25 ended at the device's side of the link: DMA is a device issuing memory requests, and an interrupt is a device writing a value to a special address. This tutorial picks up on the CPU side of that write. What does the core do when the interrupt arrives, how does it switch from your program to the kernel and back, what does that switch cost, and how do a device and a driver share a queue in memory so that they need to interrupt each other as rarely as possible? The same machinery explains system calls, page faults, timer ticks, the 100 GbE receive path and an NVMe drive, and it is the part of "how the hardware really works" that interviewers for systems, networking and embedded roles like best.

**Where the numbers come from.** The virtual machine used for tutorials 14 to 25 (a Firecracker micro-VM on a 2.1 GHz Intel Xeon with two virtual CPUs, Linux 6.18, `HZ=250`, tickless idle, the TSC as clock source) provides four kinds of evidence, and the text says which is which. (1) **Real kernel output**: `/proc/interrupts`, `/proc/softirqs`, `/proc/timer_list`, the virtio feature bits, `dmesg` lines about the APIC and the TSC. (2) **Measurements here**, from short C programs pinned to one CPU: the cost of a system call, of a fault delivered to a signal handler, of a read of the TSC, of a hand-off between two threads that spin and between two that sleep. They are *virtual-machine* numbers: this VM's exits to the hypervisor are slow (a `cpuid` takes 7.5 µs here, which suggests the host is itself virtualised, though I cannot see the host), so wake-ups and anything that exits look worse than on bare metal. Read them as ratios. (3) **Arithmetic** checked by scripts: packet budgets, interrupt rates, livelock, ring-overflow times, counter wrap times, and the receive-side-scaling hash, which reproduces all five IPv4 test vectors in Microsoft's [RSS verification table](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/verifying-the-rss-hash-calculation). (4) **Published work and specifications**, linked where used: Mogul and Ramakrishnan, ["Eliminating Receive Livelock in an Interrupt-driven Kernel"](https://www.usenix.org/legacy/publications/library/proceedings/sd96/mogul.html) (USENIX 1996); Farshin et al., ["Reexamining Direct Cache Access to Optimize I/O Intensive Applications for Multi-hundred-gigabit Networks"](https://www.usenix.org/system/files/atc20-farshin.pdf) (USENIX ATC 2020); Arm's [note on Cortex-M interrupt latency](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/beginner-guide-on-interrupt-latency-and-interrupt-latency-of-the-arm-cortex-m-processors); the Linux kernel's [FRED documentation](https://docs.kernel.org/6.9/arch/x86/x86_64/fred.html). **Nothing here was timed on a physical NIC, SSD or interrupt controller**, there is no ARM or RISC-V hardware in the VM (the two assembly sketches were assembled with cross tools, not run), and the cost of one *hardware* interrupt on bare metal is a figure I did not measure and do not quote.

> **The one-sentence model.** An interrupt or an exception makes the core stop at an instruction boundary, save the smallest possible state (where to resume, the status, the cause), raise its privilege, and jump to a handler found through a table indexed by the cause, with a special return instruction to undo it all; devices and drivers talk through **rings of descriptors in ordinary memory** (the producer writes descriptors then rings a doorbell, the consumer writes completions) so that an interrupt, which costs from a fraction of a microsecond to tens of microseconds depending on what it wakes, is needed once per *batch* and not once per item, or not at all when software polls; and almost every design in this tutorial (moderation, NAPI, DDIO-sized rings, DPDK) is a way to spend that cost only where it buys something.

---

## 1. Events: interrupts, exceptions and traps

### 1.1 Two sources, four kinds

Something can interrupt the normal flow of instructions for two different reasons, and the whole subject follows from keeping them apart.

- **Asynchronous: an interrupt.** It comes from *outside* the instruction stream (a NIC with a packet, a timer that expired, another core sending an inter-processor interrupt) and has nothing to do with the instruction that happens to be running. The core may take it at any instruction boundary.
- **Synchronous: an exception.** It is caused *by* an instruction: dividing by zero, touching an unmapped page, executing an undefined opcode, or deliberately asking for the kernel (`syscall`, `svc`, `ecall`, `int3`).

Synchronous events are sorted again by what happens next.

| Kind | Cause | The saved program counter points at | Resumable? | Examples |
|---|---|---|---|---|
| **Interrupt** | outside the instruction stream | the first instruction that has not completed | yes, exactly where it left off | NIC, timer tick, IPI |
| **Fault** | an instruction that could not complete | **the faulting instruction itself** | yes, once the handler fixes the cause (the instruction runs again) | page fault, `#GP`, divide error, `#UD` |
| **Trap** | an instruction that completed and asks for attention | **the next instruction** | yes | breakpoint (`int3`), single-step, `syscall` |
| **Abort** | the hardware cannot continue reliably | undefined or imprecise | no | machine check, double fault |

The "points at" column is the one to remember: it is why a page fault can be repaired and the load simply re-executed, and why a handler for a *trap* must not run the instruction again. Section 1.4 shows both with a real program.

**The vocabulary differs by vendor, and an interviewer may use any of it.** Intel and AMD say *exception* for synchronous events and *interrupt* for asynchronous ones (and "trap" is one kind of exception). Arm says *exception* for all of them, interrupt included. RISC-V says *trap* for everything that diverts the core and *exception* for the synchronous kind. Linux and many textbooks say "trap" loosely for any entry into the kernel. When someone asks "what happens on a trap?", say which meaning you take.

Two more distinctions you will need:

- **Maskable or not.** Most interrupts can be held off by clearing an enable flag (`IF` on x86, `PSTATE.I` on Arm, `SIE`/`MIE` on RISC-V). A **non-maskable interrupt (NMI)** ignores that flag; it is for events that cannot wait: a watchdog, an uncorrectable hardware error, the sampling interrupt of a profiler.
- **Precise.** All exceptions on a modern core are *precise* ([tutorial 09](09_pipelining_and_hazards.md) section 9 and [tutorial 11](11_superscalar_and_out_of_order_execution.md) section 6.1): every instruction before the one that caused the event has completed, none after it has had any visible effect. In an out-of-order core this is enforced at the head of the reorder buffer: the event is recorded when it is detected and *taken* only when its instruction is the oldest, the younger instructions are discarded, and the saved program counter is exact. That is also the first piece of the cost: an interrupt flushes the pipeline.

### 1.2 The x86 vector table

x86 numbers every event with a **vector** from 0 to 255. Vectors 0 to 31 are reserved by Intel for exceptions; 32 to 255 are free for external interrupts, inter-processor interrupts and the software instruction `int n`. The ones you will meet:

| Vector | Name | Kind | Error code pushed? | What a user program sees |
|---|---|---|---|---|
| 0 | `#DE` divide error | fault | no | `SIGFPE` |
| 1 | `#DB` debug | fault or trap | no | single-step, hardware watchpoints (a debugger) |
| 2 | NMI | interrupt | no | nothing: watchdog, profiler sampling, hardware error |
| 3 | `#BP` breakpoint | trap | no | `SIGTRAP` (a debugger's `int3`) |
| 6 | `#UD` invalid opcode | fault | no | `SIGILL` |
| 8 | `#DF` double fault | abort | yes (zero) | the kernel gives up |
| 13 | `#GP` general protection | fault | yes | `SIGSEGV` |
| 14 | `#PF` page fault | fault | yes | page-in, copy-on-write, or `SIGSEGV`; the address is in `CR2` |
| 17 | `#AC` alignment check | fault | yes | `SIGBUS` (only when enabled) |
| 18 | `#MC` machine check | abort | no | fatal hardware error |
| 32 to 255 | external interrupts, IPIs, `int n` | interrupt | no | nothing: the kernel handles it |

(The table follows the [OSDev exception list](https://wiki.osdev.org/Exceptions); the full set also has vectors 4, 5, 7, 9 to 12, 16, 19 to 21, 28 to 30. An error code is pushed for vectors 8, 10 to 14, 17, 21, 29 and 30.)

The **page-fault error code** tells the kernel what kind of access failed:

| Bit | Name | Set means |
|---|---|---|
| 0 | P | the page *was* present, so this is a protection violation (clear: not present) |
| 1 | W | the access was a write (clear: a read) |
| 2 | U | it happened in user mode |
| 3 | R | a page-table entry had reserved bits set |
| 4 | I | it was an instruction fetch (for example, executing a no-execute page) |

### 1.3 See the hardware's own report

Linux hands a signal handler the register state at the moment of the event, including the vector the CPU used and the error code it pushed. Here is the whole experiment, in 30 lines (x86-64 Linux; compiled with `gcc -O1`):

```c
#define _GNU_SOURCE
#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>

static sigjmp_buf env;

static void handler(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    printf("  %-24s si_code=%-3d vector=%-2lld error code=0x%llx\n", strsignal(sig), si->si_code,
           (long long)uc->uc_mcontext.gregs[REG_TRAPNO],          // the IDT vector the CPU used
           (unsigned long long)uc->uc_mcontext.gregs[REG_ERR]);   // the error code it pushed
    siglongjmp(env, 1);
}

int main(void) {
    struct sigaction sa = { .sa_sigaction = handler, .sa_flags = SA_SIGINFO | SA_NODEFER };
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGFPE, &sa, NULL); sigaction(SIGILL, &sa, NULL);
    volatile int zero = 0;

    if (!sigsetjmp(env, 1)) { volatile char c = *(volatile char *)0; (void)c; }   // read of address 0
    if (!sigsetjmp(env, 1)) { volatile int r = 10 / zero; (void)r; }               // divide by zero
    if (!sigsetjmp(env, 1)) { __asm__ volatile("ud2"); }                           // undefined instruction
    return 0;
}
```

Its output, and the result of five more one-line variants (a write to a read-only page, `cli`, `int3`, a non-canonical address, a misaligned `movaps`), all from this VM:

| Test | Signal | `si_code` | Vector | Error code |
|---|---|---|---|---|
| read of address 0 | `SIGSEGV` | 1 (`SEGV_MAPERR`) | **14** `#PF` | `0x4` (not present, read, user) |
| write to a read-only page never touched before | `SIGSEGV` | 2 (`SEGV_ACCERR`) | 14 | `0x6` (not present, **write**, user) |
| the same, after reading the page once | `SIGSEGV` | 2 | 14 | `0x7` (**present**, write, user) |
| `10 / 0` | `SIGFPE` | 1 (`FPE_INTDIV`) | **0** `#DE` | 0 |
| `ud2` | `SIGILL` | 2 | **6** `#UD` | 0 |
| `cli` (a privileged instruction) in user mode | `SIGSEGV` | 128 (`SI_KERNEL`) | **13** `#GP` | 0 |
| read of `0x8000000000000000` (not canonical) | `SIGSEGV` | 128 | 13 | 0 |
| `movaps` from an address ending in 1 | `SIGSEGV` | 128 | 13 | 0 |
| `int3` | `SIGTRAP` | 128 | **3** `#BP` | 0 |

Five things to take from it.

1. **The vector is exactly the number in the table of section 1.2.** There is no magic between the instruction and the signal: the CPU looked up gate 14, the kernel's page-fault handler ran, found no mapping, and sent `SIGSEGV`.
2. **The hardware does not know your memory regions.** The second and third rows are the same mistake (a write to a read-only mapping) with different hardware reports. A read-only page that has never been touched has no page-table entry yet, so the first write finds *no present page* (error code 6); only after a read has mapped it does a write find a present page without write permission (error code 7). In both cases the *kernel* looks at its own list of regions, sees that the region is read-only, and chooses `SEGV_ACCERR`. The CPU's job was only to say "this access cannot proceed; here is the address and the kind".
3. **`#GP` is the catch-all.** Three unrelated mistakes (a privileged instruction, a non-canonical address, a misaligned `movaps`) all arrive as vector 13 with error code 0 and all become the same "Segmentation fault". The clue is `si_code=128` (the kernel raised it, with no faulting data address) against 1 or 2 for a real page fault. This is why "segfault" tells you so little and `si_code` plus the faulting instruction tells you a lot.
4. **A fault re-runs the instruction; a trap does not.** For the divide, `si_addr` is the address of the `div` instruction itself. To get *past* it I had to add 3 (its length) to the saved `RIP` in the handler, and the cost program of section 2.5 does exactly that. For `int3` I added nothing: the saved `RIP` already points at the next instruction.
5. **Different exceptions cost the same here, with one exception** (section 2.5): in this VM `ud2` is ten times slower than a page fault, because the hypervisor intercepts `#UD`.

---

## 2. What the hardware does when an event arrives

### 2.1 The common shape

Whatever the architecture, an event goes through the same five steps.

1. **Pick a boundary.** The core finishes older instructions, discards younger ones (precise, section 1.1), and chooses the program counter to save.
2. **Save the minimum.** At least the resume address and the processor status. How much more the *hardware* saves is the main difference between architectures.
3. **Raise privilege and mask.** The core enters the more privileged mode (the kernel) and usually disables further interrupts, so the handler is not re-entered before it has saved state.
4. **Find the handler.** Through a table indexed by the cause, set up by the kernel (x86: the IDT; Arm: the vector table at `VBAR`; RISC-V: the `stvec` or `mtvec` register).
5. **Jump.** The handler's first job is to save the registers it will clobber, then work out what happened.

A special **return** instruction (`iretq`, `eret`, `sret`/`mret`) undoes steps 2 and 3 in one go: it restores the program counter and the status, which drops the privilege back to where it was.

The three architectures place the line between "hardware" and "software" differently, and that line is a classic comparison question.

| | **x86-64** | **AArch64** | **RISC-V** (S-mode) |
|---|---|---|---|
| Table | IDT: 256 gates of 16 bytes; base in `IDTR` | 16 entries of 0x80 bytes at `VBAR_ELn` (one table per exception level) | one address in `stvec`; for interrupts optionally `base + 4 × cause` |
| Which entry | the vector number | the *kind* of event × where it came from (section 2.4) | software reads `scause` |
| Return address saved in | pushed on the stack (`RIP`) | `ELR_ELn` | `sepc` |
| Status saved in | pushed (`RFLAGS`, `CS`, `SS`, `RSP`) | `SPSR_ELn` (the saved `PSTATE`) | bits in `sstatus` (`SPP`, `SPIE`) |
| Cause and extra information | the vector; an error code for some; `CR2` for a page fault | `ESR_ELn` (class and syndrome), `FAR_ELn` (fault address) | `scause`, `stval` |
| Stack switch | hardware loads `RSP` from the TSS (or an IST entry) when the privilege level changes | each level has its own stack-pointer register (`SP_EL1`), so none is needed | none; software swaps a register with `sscratch` |
| Interrupts masked on entry | `IF` cleared for interrupt gates | `PSTATE.DAIF` set | `sstatus.SIE` cleared |
| Memory written by the hardware | **40 or 48 bytes** | none | none |
| Return instruction | `iretq` | `eret` | `sret` |

The pattern: **x86 does the most in microcode** (a table walk, a stack switch, five or six pushes) and **Arm and RISC-V do almost nothing** (copy two or three registers, set a few bits, jump) and leave the rest to the first dozen instructions of the handler. Neither is cheaper overall; the work moves. Arm's own microcontroller profile goes the other way again, stacking eight registers in hardware (section 3.8).

### 2.2 x86-64 in detail

The IDT is an array of 256 **gate descriptors**, 16 bytes each in 64-bit mode (the OSDev [IDT page](https://wiki.osdev.org/Interrupt_Descriptor_Table) draws the layout): a 64-bit handler address split into three fields, a code-segment selector, a 3-bit **IST** index, a type (`0xE` interrupt gate, `0xF` trap gate), a privilege-level field (DPL) and a present bit. The `IDTR` register holds the table's base address and size.

When vector *v* arrives (a worked example with a user-mode page fault, vector 14):

1. The core finds the gate at `IDTR.base + 16 × 14`. For a hardware exception or interrupt no permission check is made on the gate; for the software instruction `int n` the gate's DPL must allow the caller (so a user program cannot fake a hardware vector).
2. The privilege changes from ring 3 to ring 0, so the core **switches stacks**: it loads `RSP` from the current task's **TSS** (`RSP0`, the kernel stack for this thread), or from a dedicated stack named by the gate's IST field (kernel designers use IST for `NMI`, `#DF` and `#MC`, which may arrive when the normal kernel stack is unusable). In 64-bit mode the stack pointer is also aligned down to 16 bytes.
3. It pushes, in this order, `SS`, `RSP` (the old user stack pointer), `RFLAGS`, `CS`, `RIP`, and then the error code if the vector has one:

```text
 higher addresses
     SS            pushed first (in 64-bit mode all five are always pushed)
     RSP           the interrupted stack pointer
     RFLAGS
     CS            its low two bits are the old privilege level
     RIP           where to resume (the faulting instruction for a fault)
     error code    only for vectors 8, 10 to 14, 17, 21, 29, 30
 lower addresses   <- RSP points here when the handler's first instruction runs
```

4. For an *interrupt gate* the core clears `IF` (no more maskable interrupts); a *trap gate* leaves it alone. Linux uses interrupt gates for nearly everything.
5. It loads `CS:RIP` from the gate and starts executing the handler in ring 0. The page-fault handler can read the address from `CR2` and the error code from the stack.

The return, `iretq`, pops `RIP`, `CS`, `RFLAGS`, `RSP`, `SS` (five words, 40 bytes; the handler must have removed the error code first) and, because `CS` carries the old privilege level, returns to ring 3. It is a microcoded instruction, and one with awkward corners (for example it unblocks NMIs even when it returns from an exception that happened *during* NMI handling). **FRED** (Flexible Return and Event Delivery) is Intel's replacement, aimed at "lower latency transitions" and at saving and restoring the whole context atomically; Linux has supported it since 6.9 ([kernel documentation](https://docs.kernel.org/6.9/arch/x86/x86_64/fred.html)). All software must still work with the IDT mechanism described here, and the CPU in this VM uses it.

Three instruction-level details worth knowing:

- `cli` and `sti` clear and set `IF`. `sti` takes effect after the *following* instruction, so `sti; hlt` cannot lose a wake-up between them: an interrupt that arrives right after `sti` is held until `hlt` has begun and then wakes it.
- A **NMI** blocks further NMIs until the next `iret`. Nesting NMIs would corrupt their own stack frames.
- The kernel runs with `-mno-red-zone` ([tutorial 07](07_assembly_in_practice.md) section 5) because an interrupt pushes onto the stack at any time, which would overwrite the 128 bytes below `RSP` that user-mode leaf functions use.

### 2.3 System calls: a faster door

A system call is a deliberate trap into the kernel. Going through the IDT for it was slow enough that x86-64 added a dedicated pair of instructions. Three registers set up in advance by the kernel describe the target: `IA32_LSTAR` (the entry address), `IA32_STAR` (the segment selectors) and `IA32_FMASK` (the `RFLAGS` bits to clear).

`syscall` then does, with no memory access at all:

```text
RCX    <- address of the next instruction
R11    <- RFLAGS
RFLAGS <- RFLAGS with the FMASK bits cleared (interrupts off)
CPL    <- 0, CS and SS from STAR
RIP    <- LSTAR
```

Note what it does *not* do: it does not switch stacks (the kernel entry code runs `swapgs` to reach its per-CPU data, saves the user stack pointer and loads the kernel stack itself) and it does not push anything. `sysretq` reverses it. This is why `rcx` and `r11` are clobbered by every system call ([tutorial 07](07_assembly_in_practice.md): the fourth argument goes in `r10` instead of `rcx`). I checked it: after a `getppid` the register `rcx` was exactly the address of the instruction after `syscall`, and `r11` held the old `RFLAGS` (`0x202`).

The other two architectures use their ordinary exception mechanism and differ in one telling detail.

| | x86-64 | AArch64 | RISC-V |
|---|---|---|---|
| Instruction | `syscall` | `svc #0` | `ecall` |
| Number and arguments (Linux) | `rax`; `rdi, rsi, rdx, r10, r8, r9` | `x8`; `x0` to `x5` | `a7`; `a0` to `a5` |
| Enters | the `LSTAR` fast path, not the IDT | the "lower EL, AArch64, synchronous" vector (offset `0x400`) | the trap vector with `scause` = 8 (from U-mode) |
| Saved return address | `rcx` = the **next** instruction | `ELR_EL1` = the **next** instruction | `sepc` = **the `ecall` itself** |
| Return | `sysretq` | `eret` | `sret` |

The last row's third column is a rite of passage for anyone writing a teaching kernel: on RISC-V **every** synchronous exception saves the address of the instruction that raised it, including `ecall`, so the handler must add 4 to `sepc` before `sret`, or the program executes the `ecall` forever. (Section 2.5 shows the line.)

### 2.4 AArch64: exception levels and the vector table

Arm's privilege levels are **exception levels**: EL0 (applications), EL1 (the operating system kernel), EL2 (the hypervisor, [tutorial 27](27_hardware_virtualization.md)) and EL3 (the secure monitor and firmware, the root of TrustZone; see [tutorial 30](30_hardware_security.md), section 7.2). The rule that makes the model clean: **you go up only by taking an exception and come down (or stay level) only by `eret`**. There is no instruction that raises the privilege level other than the exception-generating ones.

On an exception taken to EL*n* the hardware: saves the old `PSTATE` in `SPSR_ELn` and the return address in `ELR_ELn`; writes the cause into `ESR_ELn` (a 6-bit **exception class** `EC` plus syndrome bits) and, for aborts, the faulting address into `FAR_ELn`; sets the interrupt mask bits `PSTATE.DAIF` (Debug, SError, IRQ, FIQ); selects `SP_ELn` as the stack pointer; and sets `PC = VBAR_ELn + offset`. It writes nothing to memory. `eret` copies `ELR` and `SPSR` back.

The offset comes from the **vector table**: 16 entries of 0x80 bytes (32 instructions each), 2 KiB aligned, in four groups of four.

| Offset | Taken from | Type |
|---|---|---|
| `0x000` / `0x080` / `0x100` / `0x180` | current EL, using `SP_EL0` | Synchronous / IRQ / FIQ / SError |
| `0x200` / `0x280` / `0x300` / `0x380` | current EL, using `SP_ELx` (a kernel fault, a kernel interrupt) | Synchronous / IRQ / FIQ / SError |
| **`0x400` / `0x480`** / `0x500` / `0x580` | **a lower EL using AArch64** (user code) | **Synchronous** (`svc`, user page fault) **/ IRQ** (interrupt while in user mode) / FIQ / SError |
| `0x600` / `0x680` / `0x700` / `0x780` | a lower EL using AArch32 | Synchronous / IRQ / FIQ / SError |

(I assembled this 16-entry layout with the AArch64 cross-assembler to check the offsets; each entry is a `.balign 0x80` branch.) Two entries do nearly all the work in a Linux system: `0x400` for traps from user space and `0x480` for interrupts that hit user code. The `ESR_ELn.EC` field then names the reason: `0x15` is `svc` from AArch64, `0x20` and `0x21` are instruction aborts (from a lower or the same level), `0x24` and `0x25` are data aborts, `0x3C` is `brk`.

The **IRQ and FIQ** split is historical: two interrupt lines to the core, FIQ originally the "fast" one with banked registers. In modern Linux on a GIC, ordinary interrupts arrive as IRQ and FIQ is reserved for secure-world or special use. **SError** is the asynchronous system error (an external abort whose cause arrived late, such as a bus error on a posted write).

### 2.5 RISC-V: the shortest design

RISC-V has three privilege levels: **M** (machine, always present, runs the firmware), **S** (supervisor, the OS) and **U** (user). The hypervisor extension adds HS/VS/VU ([tutorial 27](27_hardware_virtualization.md)). Each level has its own control registers (CSRs): `mstatus`/`sstatus` (the enable bits `MIE`/`SIE`, the saved ones `MPIE`/`SPIE`, the previous privilege `MPP`/`SPP`), `mtvec`/`stvec` (the trap vector), `mepc`/`sepc` (the saved program counter), `mcause`/`scause` (the cause), `mtval`/`stval` (the faulting address or instruction), and `mscratch`/`sscratch` (one free register for the entry code). The registers `medeleg` and `mideleg` let M-mode *delegate* chosen exceptions and interrupts straight to S-mode, so the firmware does not have to forward them.

On a trap taken to S-mode the hardware does exactly this: `sepc ← pc`; `scause ← cause`; `stval ← the bad address or instruction`; `sstatus.SPIE ← SIE`, `SIE ← 0`, `SPP ← the previous privilege`; `pc ← stvec`. `sret` does the reverse: `pc ← sepc`, privilege `← SPP`, `SIE ← SPIE`. That is all. There is no stack switch, no table index (in *direct* mode every trap goes to one address and software reads `scause`), and no memory write.

`scause` has the top bit set for interrupts and clear for exceptions; the rest is the code:

| Exceptions (top bit 0) | | Interrupts (top bit 1) | |
|---|---|---|---|
| 2 | illegal instruction | 1 / 3 | supervisor / machine software (IPI) |
| 3 | breakpoint | 5 / 7 | supervisor / machine timer |
| 8 / 9 / 11 | `ecall` from U / S / M | 9 / 11 | supervisor / machine external |
| 12 / 13 / 15 | instruction / load / store page fault | | |

The entry code of a small kernel therefore looks like this (a sketch for a teaching kernel such as the `tinyos` project; **assembled with `riscv64-linux-gnu-as`, not run here**):

```asm
trap_entry:
        csrrw   sp, sscratch, sp        # swap: sp = kernel stack, sscratch = interrupted sp
        addi    sp, sp, -32
        sd      ra,  0(sp)              # save what the C handler may clobber (a sketch: 3 registers)
        sd      a0,  8(sp)
        sd      a1, 16(sp)

        csrr    a0, scause              # why? top bit 1: interrupt, 0: exception
        csrr    a1, sepc                # where? the trapping instruction
        bltz    a0, is_interrupt        # scause < 0 (top bit set): an interrupt
        li      t0, 8                   # 8 = ecall from U-mode
        bne     a0, t0, other_exception
        addi    a1, a1, 4               # ecall: sepc points AT the ecall; skip it or loop forever
        csrw    sepc, a1
        # ... dispatch the system call here ...
        j       restore
is_interrupt:
other_exception:
        # ... timer: program the next compare value; external: claim from the PLIC; fault: kill the process ...
restore:
        ld      ra,  0(sp)
        ld      a0,  8(sp)
        ld      a1, 16(sp)
        addi    sp, sp, 32
        csrrw   sp, sscratch, sp        # put the interrupted sp back and re-arm sscratch
        sret                            # pc = sepc, privilege = sstatus.SPP, SIE = SPIE
```

The first instruction is the clever one: `csrrw sp, sscratch, sp` exchanges `sp` with `sscratch` in one step, so that the handler gets a known-good kernel stack without needing a free register to hold anything first.

In *vectored* mode (`stvec` bit 0 set) interrupts jump to `base + 4 × cause` while exceptions still go to `base`, which saves the dispatch for the timer interrupt.

### 2.6 Privilege levels, in one paragraph

All of this exists to let the kernel protect itself from user code. What changes with privilege: the ability to execute privileged instructions (`cli`, `hlt`, `lgdt`, `wrmsr`, writes to control registers; on Arm, writes to most system registers; on RISC-V, CSR access above one's level), access to pages marked supervisor-only (the `U/S` bit; the kernel can additionally forbid *itself* from executing or reading user pages unless it means to, with SMEP and SMAP on x86 and PXN/PAN on Arm), and access to I/O ports. User code can raise its privilege **only** through the instructions and events that jump into the entry points the kernel installed in its own tables (the IDT and `LSTAR`, the `VBAR` vectors, `stvec`), and those jump to code the kernel wrote. That is the entire argument for why a user program cannot "just call kernel code". x86 defines four rings, but rings 1 and 2 are unused by mainstream operating systems; the virtualisation extensions add a second axis (root and non-root mode, [tutorial 27](27_hardware_virtualization.md)). Arm and RISC-V have the same idea with the names above.

### 2.7 What the crossing costs: a measured ladder

All of these were measured here with a program pinned to CPU 0 (the median of seven runs of 0.1 to 20 million iterations; the ranges are the spread over two sessions). The CPU reports "Meltdown: not affected", so Linux does not switch page tables on kernel entry in this VM; on a CPU that needs page-table isolation a system call costs noticeably more ([tutorial 15](15_virtual_memory_and_the_tlb.md) section 11.4).

| Operation | Time | TSC ticks (time × 2.1 GHz) |
|---|---|---|
| an empty loop iteration (the baseline) | 0.4 to 0.6 ns | 1 |
| `rdtsc` (read the time-stamp counter) | 16 ns | 34 to 40 |
| `clock_gettime` through the **vDSO** (no kernel entry) | 29 to 33 ns | about 65 |
| a raw `syscall` (`getppid`, which does nothing) | 115 to 126 ns | **about 262** |
| `clock_gettime` forced through a real system call | 175 to 205 ns | about 400 |
| `#DE`, `#BP`, `#GP` or `#PF` **delivered to a signal handler and returned from** | **1.5 to 1.7 µs** | 3,100 to 3,600 |
| a futex hand-off between two threads **on one CPU** (a context switch) | 1.6 to 1.8 µs | about 3,400 |
| `cpuid` (always exits to the hypervisor in a VM) | 6.9 to 7.8 µs | about 16,000 |
| `ud2` to a signal handler and back | 9.5 to 10.9 µs | about 21,000 |
| a futex hand-off between two threads **on two CPUs**, the waiter asleep | 10.0 to 10.5 µs | about 21,600 |
| the same hand-off with the waiter **spinning** | 56 to 60 ns | about 122 |

The last column counts **ticks of the time-stamp counter**, which runs at the 2.1 GHz label. The core itself ran at about 3 GHz (measured in [tutorial 28](28_performance_and_power.md) section 2.2), so multiply by about 1.4 for core cycles: a system call is about 260 ticks, about 350 core cycles. Ratios between rows are unaffected.

Read the ladder like this.

- **A system call is about 260 TSC ticks** (about 350 core cycles) of pure round trip here, and that figure already contains the hardware entry (`syscall`, `swapgs`, stack switch), the kernel's dispatch and `sysret`. The hardware part of a *system-call* entry is therefore bounded by a few hundred cycles. Ten million system calls a second is over a second of CPU time per second, which is why batching interfaces exist (`readv`, `sendmmsg`, `io_uring`).
- **The vDSO is the proof that the crossing is the cost.** `clock_gettime` through the vDSO reads the TSC and a few scaling constants from a page the kernel keeps mapped into every process: no mode change, 31 ns. Forced through the kernel it takes about 190 ns. Same answer; the mode switch is six times the work.
- **A fault delivered to user space costs ten system calls**, and most of the 1.5 µs is Linux building a *signal frame* on the user stack (including the floating-point and vector state, which can be kilobytes), running your handler, and returning through `rt_sigreturn`. The hardware's own share of the exception cannot be separated out in this measurement. So: never use faults as a fast path (`SIGSEGV`-based tricks, `userfaultfd` in a hot loop, touching guard pages on purpose) unless microseconds are acceptable.
- **A wake-up that crosses CPUs costs 10 µs here** against 60 ns for a spinning waiter. Section 5 explains the gap; the point for now is that the interrupt-driven path is *two orders of magnitude* slower than the polled one for the same hand-off.
- **`cpuid` and `ud2` expose the hypervisor.** `cpuid` is guaranteed to exit; at 7.5 µs this VM's exits are slow (the host is probably itself virtualised). A `ud2` costs 8 to 9 µs more than a page fault, about one extra exit, which fits a hypervisor intercepting `#UD` (KVM does so to emulate instructions); I did not look inside the hypervisor to confirm. Page faults, divides and breakpoints do not exit. The practical lesson, which the next tutorials rely on: **in a virtual machine a few operations cost microseconds instead of nanoseconds, and interrupts and inter-processor interrupts are among them.**

---

## 3. Interrupt controllers: from a wire (or a message) to a vector

### 3.1 The problem

A CPU core has one interrupt input. A system has dozens to thousands of sources (every device queue, the timers, other cores). Something must: remember which sources are asserting, **choose the highest-priority one**, hide the others until it is done, deliver it to the *right core*, translate the source into a vector or ID the core can use, and learn when software has finished so the next one can come. The priority encoder of [tutorial 03](03_boolean_logic_and_combinational_circuits.md) is the heart of it; around it sit mask bits, pending bits, and routing tables. That block is the **interrupt controller**.

How a source signals matters, because it decides who has to clear what:

- **Level-triggered.** The line stays asserted as long as the device needs service. The handler must clear the cause *at the device* (usually by reading or writing a status register), or the interrupt fires again the moment the handler returns: an **interrupt storm**. Level lines can be shared by several devices (wired-OR), at the price of calling every handler on the line to ask "was it you?". If every handler declines for long enough, Linux masks the line and prints "nobody cared".
- **Edge-triggered.** The *transition* is the event. The controller must latch it (a pending bit) or a transition that arrives while the source is masked is lost. A handler that forgets to re-check the device after clearing the cause can miss a second event that arrived in between.
- **Message-signalled (MSI and MSI-X).** The device performs a memory write ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 5.2). Edge-like, never shared, no wire, one vector per queue, and the write itself says which core.

### 3.2 x86: PIC, I/O APIC, local APIC

The original PC used two cascaded **8259A programmable interrupt controllers**: eight inputs each, one used for the cascade, so 15 usable IRQs (IRQ 0 timer, 1 keyboard, 4 first serial port, ...), fixed priority by pin number. The chip delivers a vector from a programmable base; the BIOS left that base at 8, which collides with the CPU's own exception vectors (8 is the double fault), so every operating system immediately re-programs the PICs to use 0x20 or higher and then disables them in favour of the APIC. It is still emulated everywhere.

The **APIC** architecture splits the job in two and scales to many cores:

- The **I/O APIC** receives the interrupt pins of devices. It has a *redirection table* of (typically) 24 entries, one per pin; each entry holds a vector, the destination core, the delivery mode, edge or level, and a mask bit. This VM's `dmesg` shows it: `IOAPIC[0]: apic_id 0, version 17, address 0xfec00000, GSI 0-23`.
- A **local APIC** sits on every core. It takes interrupts from the I/O APIC, its own timer, other cores (IPIs), message-signalled writes (MSI), the performance counters and the thermal sensor. Three 256-bit registers track each vector: *request* (pending), *in service*, and *trigger mode*. A **priority** mechanism decides delivery: the priority *class* of a vector is `vector >> 4` (16 classes), and an interrupt is delivered only if its class is higher than the core's current **processor priority** (the larger of the task-priority register and the highest in-service class). So **a higher vector number is a higher priority**, which is why the timer and IPI vectors live near the top of the range. Software signals completion by writing the **EOI** (end of interrupt) register, which lets the next vector of equal or lower class through.
- **x2APIC** is the same architecture with register access through MSRs instead of a memory-mapped page and 32-bit core IDs (this VM: `x2apic enabled`). MSI writes go to the address range starting at `0xFEE00000`, with the destination core in the address bits and the vector in the low byte of the data.

The VM has one more detail worth reading. Sending an IPI means writing the APIC's command register, and EOI is another register write; in a virtual machine each can exit to the hypervisor. `dmesg` says `kvm-guest: APIC: send_IPI_mask() replaced with kvm_send_ipi_mask()` and `kvm-guest: APIC: eoi() replaced with kvm_guest_apic_eoi_write()`: the guest kernel detected the hypervisor and swapped in **paravirtual** versions (a hypercall instead of a register write) to make those two operations cheaper. [Tutorial 27](27_hardware_virtualization.md) explains the hardware that goes further.

**Inter-processor interrupts (IPIs)** deserve a line of their own, because they are how cores talk to each other through the kernel. This VM, one hundred seconds after boot:

```text
         CPU0   CPU1
RES:      269    335   Rescheduling interrupts   (wake a task that should run on the other CPU)
CAL:     4280   3693   Function call interrupts  (run this function on the other CPU)
TLB:      593    416   TLB shootdowns            (tutorial 15 section 10.3: invalidate stale translations)
```

An IPI is also why `mprotect` or `munmap` in a multi-threaded process slows down the *other* threads: they are interrupted ([tutorial 15](15_virtual_memory_and_the_tlb.md) section 10.3).

### 3.3 Arm: the GIC

The Generic Interrupt Controller has the same jobs with different names. In GICv3 (the common one in servers and recent phones):

- the **Distributor** (`GICD`) holds the shared configuration: enable, priority, trigger type and routing for shared interrupts;
- a **Redistributor** (`GICR`) per core holds that core's private interrupts and its LPI tables;
- the **CPU interface** is a set of system registers (`ICC_*`): the core *acknowledges* an interrupt by reading `ICC_IAR1_EL1`, which returns the interrupt ID and marks it active, sets a priority mask in `ICC_PMR_EL1`, and signals completion by writing `ICC_EOIR1_EL1`;
- the **ITS** (Interrupt Translation Service) turns an MSI write, identified by (device ID, event ID), into a **LPI** and a target core.

Interrupt IDs fall in ranges you should be able to recite: **0 to 15 SGIs** (software-generated: the Arm IPIs), **16 to 31 PPIs** (private to a core: its timer, its performance counters), **32 to 1019 SPIs** (shared peripherals on wires), **8192 and up LPIs** (message-based). Priorities are 8-bit and **a lower number means higher priority**, the opposite of x86's vector classes, a classic trip-up.

### 3.4 RISC-V: CLINT/ACLINT and PLIC (and AIA)

The simple RISC-V platforms (the QEMU `virt` machine used by teaching kernels) have two blocks. The **CLINT** (core-local interruptor) gives each hart a software-interrupt bit (`msip`, the IPI) and a timer compare register (`mtimecmp`) against a shared counter (`mtime`). The **PLIC** (platform-level interrupt controller) handles external devices: each source has a priority; each *context* (a hart in a given privilege mode) has per-source enable bits and a priority threshold; software reads the **claim** register to get the highest-priority pending ID (which also clears its pending bit) and writes the same ID to the **complete** register when done. The newer **AIA** (Advanced Interrupt Architecture) adds message-signalled interrupts through a per-hart IMSIC, closing the gap with the APIC and the GIC's ITS.

### 3.5 Side by side

| | x86 | Arm (GICv3) | RISC-V (PLIC + CLINT) |
|---|---|---|---|
| Wired devices | I/O APIC pins | SPIs (32 to 1019) | PLIC sources |
| Message-signalled | MSI/MSI-X to the local APIC | ITS, to LPIs | AIA (IMSIC) |
| Per-core local block | local APIC (timer, IPIs, ...) | redistributor, PPIs and SGIs | CLINT (`msip`, `mtimecmp`) |
| Priority | vector class `vector >> 4`; **higher is higher** | 8-bit; **lower is higher** | PLIC priority per source and threshold per context |
| Software acknowledges by | taking the vector, then **EOI** write | **read `IAR`**, then write `EOIR` | **read claim**, write complete |
| IPIs | write the command register (ICR) | SGIs | write `msip` |

### 3.6 This VM's `/proc/interrupts`

Real output, trimmed to the instructive lines (one hundred seconds after boot; the columns are the two CPUs):

```text
           CPU0       CPU1
 26:          5          0  IO-APIC   4-edge      ttyS0
 36:          0          1  PCI-MSIX-0000:00:02.0   0-edge      virtio1-config
 37:          0       1305  PCI-MSIX-0000:00:02.0   1-edge      virtio1-req.0
 48:          0          0  PCI-MSIX-0000:00:08.0   0-edge      virtio7-config
 49:       1440          0  PCI-MSIX-0000:00:08.0   1-edge      virtio7-input.0
 50:          0       1459  PCI-MSIX-0000:00:08.0   2-edge      virtio7-output.0
NMI:          0          0   Non-maskable interrupts
LOC:       5875       5758   Local timer interrupts
RES:        269        335   Rescheduling interrupts
CAL:       4280       3693   Function call interrupts
TLB:        593        416   TLB shootdowns
```

How to read it:

- Line 26 is a **legacy wired interrupt**: the first serial port, IRQ 4 on the I/O APIC, edge-triggered. Every other device line is **`PCI-MSIX`**: a PCI function (`00:02.0`) with its own vector per purpose, named by the driver. `virtio7` is the virtio network device with a **separate vector for its receive queue and for its transmit queue** (and one for configuration changes); `virtio1-req.0` is the block device's request queue 0. One vector per queue is exactly what MSI-X is for ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 5.2), and it is what lets a driver send each queue's interrupt to a different core.
- The rows under the numbers are the **per-core, non-device** sources: `LOC` is the local timer (the scheduler tick), `NMI` the non-maskable one, and the three IPI types of section 3.2.
- The **affinity** files say where a vector may go. `/proc/irq/49/smp_affinity_list` shows `0-1` (allowed on both), yet `effective_affinity_list` shows a single CPU: the hardware delivers each vector to *one* core even when several are allowed.

You can move one. Writing `1` into `/proc/irq/49/smp_affinity_list` (the network receive vector), then generating traffic, counted on the two CPUs:

| Action | CPU0 count | CPU1 count | Effective |
|---|---|---|---|
| before | 10,554 | 0 | CPU 0 |
| allowed only on CPU 1, read after 1, 2, 3 s of traffic | 10,555 · 10,555 · 10,555 | 21 · 42 · 62 | CPU 1 |
| allowed only on CPU 0 again, read after 1, 2 s | 10,574 · 10,595 | 63 · 63 | CPU 0 |

The count on CPU 1 climbed by about 20 a second while it was the target, and CPU 0's stopped; reversing the mask reversed the flow. (The block-device vectors refused the same write with "Operation not permitted"; I did not look into why.) This is how irqbalance and network tuning scripts steer interrupts: **put the interrupt on the core that will consume the data**, which for a NIC means the core running the thread that reads that queue, on the NIC's NUMA node ([tutorial 16](16_dram_memory_controllers_and_numa.md)).

### 3.7 Masking, priority and nesting

Three levels of masking exist and interviewers mix them up.

1. **At the source**: the device's own interrupt-enable bit or the MSI-X vector's mask bit.
2. **At the controller**: the I/O APIC's mask bit, the GIC's enable bits, the PLIC's enables and thresholds; or the local APIC's priority.
3. **At the core**: the flag (`IF`, `PSTATE.I`, `SIE`). Clearing it holds off *all* maskable interrupts on that core, which is how a handler protects itself and also how a long critical section creates latency for everyone else.

Because an interrupt gate clears `IF`, a Linux handler normally runs with interrupts off and is not interrupted by another device. Real-time systems often want the opposite: a high-priority interrupt that preempts a low-priority handler (**nesting**), which the controller's priority and the core's mask make possible.

### 3.8 The embedded counter-example: Cortex-M's NVIC

Arm's microcontroller profile makes the other trade. Its **NVIC** (nested vectored interrupt controller) is part of the core, every interrupt has a programmable priority, and, importantly, **the hardware stacks the caller-saved registers itself**: R0 to R3, R12, LR, PC and xPSR are pushed in parallel with fetching the handler address, and the handler is an ordinary C function (the return uses a special `EXC_RETURN` value in LR). Arm's [documentation](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/beginner-guide-on-interrupt-latency-and-interrupt-latency-of-the-arm-cortex-m-processors) states an interrupt latency of **12 cycles** for the Cortex-M3 and M4 (16 for the M0, 15 for the M0+), and that **tail-chaining** (jumping from one handler straight into the next pending one without unstacking and restacking) "can be just six cycles". At 100 MHz, 12 cycles is 120 ns, a deterministic latency that a desktop-class core, with its caches, TLB and deep pipeline, cannot promise. This is the number to quote when an embedded interviewer asks why a hard-real-time system uses a Cortex-M rather than a Cortex-A.

---

## 4. Handling an interrupt: cost, moderation, NAPI and livelock

### 4.1 One interrupt, end to end

Take the receive interrupt of the virtio network device above (vector 49) as the example, and follow it.

1. The device performs its MSI-X write ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 5.2). The CPU knows nothing yet.
2. The write reaches the target core's local APIC and sets the vector's bit in its request register.
3. When `IF` is set and the priority allows, the core takes the interrupt **at an instruction boundary**: it flushes the younger instructions and does the hardware entry of section 2.2.
4. The kernel's assembly entry code saves the registers, switches to an interrupt stack, and finds the handler registered for the vector.
5. The **top half**, the driver's handler: for a NIC, normally tiny. It masks that queue's interrupt at the device and schedules the polling routine.
6. It writes **EOI** to the APIC, so equal-or-lower-priority vectors can come through again.
7. It returns (`iretq`). The interrupted program resumes, or, if the handler woke a task, the scheduler may run that task instead.
8. Soon afterwards the **bottom half** (a softirq on Linux; section 4.5) does the real work: reads the descriptors, builds packets, runs the protocol stack.

Where the time goes, with the honest caveat that I measured only the pieces I can name:

| Cost | Size | Note |
|---|---|---|
| hardware entry and `iretq` | a few hundred cycles at most (a whole `syscall` round trip is about 350 core cycles here) | flush plus microcode; bounded, not zero |
| any MMIO *read* of the device in the handler | **about 1 µs on hardware** ([tutorial 25](25_buses_pcie_and_io_mapping.md): a PCIe round trip) | reading an "interrupt status" register is the classic avoidable cost; modern drivers read a completion entry in RAM instead |
| the handler's own work | tens to hundreds of ns for a tiny top half | the reason for the top/bottom split |
| **indirect**: cache, TLB and branch-predictor pollution | lasts after the handler has returned | the interrupted program runs slower for a while; hard to measure, real |
| in a VM: EOI, IPIs, timer reprogramming | each may be a VM exit (7.5 µs for a `cpuid` here) | paravirtual shortcuts (section 3.2) exist for this reason |
| waking a core from a deep idle state | microseconds to hundreds of microseconds | the CPU's C-state exit latency |

The models below use a single parameter **c, the all-in CPU cost of one interrupt with its softirq entry and exit**. I **assume c = 2 µs**, which is in the range commonly quoted for a server-class machine, and I did not measure it. The conclusions do not depend on the exact value; Problem A makes you vary it.

### 4.2 The split every system makes: top half and bottom half

A handler runs with the interrupt's own line masked and often with *all* maskable interrupts off on that core. Anything slow in it delays every other interrupt and every task on that core. And it cannot sleep: it runs in the context of whatever was running when the interrupt hit, with no thread of its own to block, so it may not take a lock that can sleep, wait for I/O or call anything that might. The remedy is a universal pattern:

- **Top half** (hardirq, ISR): acknowledge the device, grab what must be grabbed now (a status, a timestamp), and **defer**.
- **Bottom half**: the deferred work, run soon, with interrupts enabled and in a context that is easier to schedule. Linux has several (softirqs, tasklets, threaded interrupt handlers, work queues; the operating-systems track covers them); the network receive path runs in the `NET_RX` softirq, whose counters you saw in `/proc/softirqs` (`NET_RX: 1441 1298` here).
- **An RTOS does the same thing with different words**: the ISR gives a semaphore or posts to a queue and returns, and a task does the work. A long ISR is a priority-inversion bug waiting to happen.

### 4.3 How often is too often

Suppose every received packet raised an interrupt. With c = 2 µs:

| Packet rate | Interrupts per second | CPU spent on interrupts alone |
|---|---|---|
| 1 kpps | 1,000 | 0.2 % of a core |
| 100 kpps | 100,000 | 20 % of a core |
| 1 Mpps | 1,000,000 | **2 cores** |
| 14.88 Mpps (10 GbE, 64-byte frames) | 14,880,000 | **29.8 cores** |

(14.88 Mpps is `10 Gb/s ÷ (84 B × 8)`; the 20 extra bytes per frame are the preamble and the inter-frame gap, [tutorial 24](24_accelerators_and_fpgas.md) section 6.3.) One core can take at most `1 ÷ c = 500,000` interrupts a second even if it does nothing else. Per-packet interrupts are therefore impossible at any serious rate, and the whole of this section is about avoiding them.

### 4.4 Interrupt moderation (coalescing)

The NIC delays the interrupt so that one interrupt reports several packets. The two knobs (`ethtool -C`) are a **frame count** F (interrupt after F packets) and a **timer** T (interrupt at most T after the first unreported packet). With a steady arrival rate the batch is `max(1, min(F, pps × T))`, the interrupt rate is `pps ÷ batch`, and the first packet of a batch waits `min((F − 1) ÷ pps, T)`. With F = 32 and T = 50 µs and c = 2 µs:

| Packet rate | Batch | Interrupts per second | CPU for interrupts | The oldest packet waits |
|---|---|---|---|---|
| 1 kpps | 1 | 1,000 | 0.2 % | **50 µs** |
| 10 kpps | 1 | 10,000 | 2 % | **50 µs** |
| 100 kpps | 5 | 20,000 | 4 % | 50 µs |
| 1 Mpps | 32 | 31,250 | 6.25 % | 31 µs |
| 14.88 Mpps | 32 | 465,000 | **93 %** | 2.1 µs |

Three lessons from one table.

- **Moderation trades latency for CPU, and the trade is worst at low load.** At 1 kpps there is nothing to batch, so a fixed timer just adds up to 50 µs to every packet. That is why **adaptive moderation** exists (Linux "DIM"): short timer when idle, long when busy. A latency-sensitive service with a bursty low-rate workload should check what its NIC defaults to.
- **At high rates the frame count matters more than the timer.** At 14.88 Mpps even a batch of 32 leaves 93 % of a core in interrupts; a batch of 256 would cost about 12 %. The next step beyond moderation is not to take interrupts at all (section 4.5).
- **The same idea has other names.** NVMe defines an *interrupt coalescing* feature with an aggregation threshold (entries) and an aggregation time (in 100 µs units). Virtio has the **event index**: the driver tells the device "interrupt me only when you pass entry *k*" and the device tells the driver "notify me only when you pass entry *m*". It is negotiated in this VM's network device (feature bit 29). It is interrupt moderation and doorbell batching in one mechanism.

### 4.5 NAPI: switch the interrupt off while there is work

Linux's answer for networking is **NAPI**: use interrupts at low load and polling at high load, and switch automatically.

1. The first packet raises an interrupt. The handler **masks that queue's interrupt** and schedules the poll.
2. The `NET_RX` softirq calls the driver's **poll** function, which handles up to a **weight** of packets from the ring (64 here: `net.core.dev_weight`).
3. If the ring still has packets when the weight is used up, the poll function stays on the list and runs again, **with the interrupt still masked**: the system is polling, at no interrupt cost at all.
4. If the ring is empty, the driver unmasks the interrupt and goes back to sleep. The next packet interrupts again.

There are global limits so one busy queue cannot starve the rest of the system: a softirq pass handles at most **300 packets** (`net.core.netdev_budget`) or **8,000 µs** (`netdev_budget_usecs`), values read from this VM. When a pass is cut short by the budget, `/proc/net/softnet_stat` counts it in the third column (**time_squeeze**), the number to look at when a busy receive path loses packets. On this idle VM, the first row (CPU 0) reads: processed 5,710 packets, dropped 0, squeezed 0.

NAPI is the same trade as moderation but driven by load instead of a fixed timer, which is why every modern Linux NIC driver is built on it.

### 4.6 Receive livelock

Mogul and Ramakrishnan's 1996 paper names a failure that any interrupt-driven system has: if interrupts have *strict priority over the work they feed*, then as the offered load grows, the CPU spends more and more of its time in the interrupt handlers and less on the processing that turns packets into results, until **the delivered throughput falls to zero while the CPU is 100 % busy**. The system is alive and delivering nothing: livelock.

A model with my own parameters shows the shape. Let the interrupt-level cost per packet be a = 3 µs (strict priority) and the protocol-and-application cost b = 5 µs, with offered load R. The interrupts take `min(1, R × a)` of the CPU, the rest goes to processing, and the delivered rate is `min(R, (1 − R a) ÷ b)`:

| Offered load | CPU in interrupts | Left for processing | **Delivered** |
|---|---|---|---|
| 50 kpps | 15 % | 85 % | 50 kpps |
| 125 kpps | 37.5 % | 62.5 % | **125 kpps (the peak, `1 ÷ (a + b)`)** |
| 150 kpps | 45 % | 55 % | 110 kpps |
| 200 kpps | 60 % | 40 % | 80 kpps |
| 250 kpps | 75 % | 25 % | 50 kpps |
| 300 kpps | 90 % | 10 % | 20 kpps |
| 334 kpps and above | 100 % | 0 % | **0** (`R ≥ 1 ÷ a`) |

The remedies, in the spirit of the paper and of modern kernels: **Poll under overload** (NAPI: no interrupt, so no interrupt-level cost). **Limit the interrupt rate** (moderation). **Drop early**, at the cheapest point: if the ring is full the NIC drops the packet before the CPU has spent anything on it. And give the processing at least some guaranteed CPU (the budget limits above). The deeper lesson, which applies to any queueing system: **work that is cheap per item but unbounded in rate must be bounded at the front door.**

### 4.7 Where the interrupt goes: affinity and locality

An interrupt that lands on the wrong core is a hidden cost. The data the device just wrote is in the cache of the socket where the buffer lives (section 7.7), and the thread that will use it runs on some core. If the interrupt (and so the softirq and the protocol processing) runs on a third core, possibly on another socket, every packet's descriptors and headers cross cores twice. Practical rules, all consequences of tutorials 16 and 19:

- One queue per core, its interrupt pinned to that core, the application thread on the same core or its hyperthread sibling.
- Keep everything on the NIC's NUMA node (`numa_node` in sysfs).
- Do not let `irqbalance` move the vectors of a latency-critical queue.

---

## 5. Polling versus interrupts

### 5.1 Two ways to learn that something happened

- **Interrupts**: the CPU does something else (or sleeps) until the device tells it. The cost is paid *per event*: the entry, the handler, and, if the CPU was idle, the wake-up.
- **Polling**: the CPU reads a status location in a loop. The cost is paid *all the time*: a core is busy whether or not anything arrives, but the cost *per event* is only the time to notice it.

### 5.2 A measurement

Two threads on the two CPUs pass a token back and forth. In one version the waiting thread **spins** on a shared variable; in the other it **sleeps** in the kernel (a futex wait) and the other thread wakes it. Time per one-way hand-off, the median of three runs:

| Waiter | CPUs | Time per hand-off |
|---|---|---|
| **spins** (polling) | 0 and 1 | **56 to 60 ns** |
| sleeps (futex), waiter on the other CPU | 0 and 1 | **10.0 to 10.5 µs** |
| sleeps (futex), both threads on one CPU | 0 and 0 | 1.6 to 1.8 µs |

The spinning hand-off is a cache line moving between two cores ([tutorial 19](19_cache_coherence.md): about 60 ns here). The sleeping hand-off across CPUs is **175 times slower**. What it contains: a system call to wake the waiter, the scheduler deciding where to run it, an **inter-processor interrupt** to the other CPU (an exit in this VM), the other CPU leaving its idle state, and the return to user mode. The same-CPU case has no IPI and no idle exit, so it is six times cheaper than the cross-CPU one: **most of the 10 µs is the cost of waking a sleeping core**. On bare metal the gap is smaller but of the same shape; the wake-up from a deep C-state alone can take tens to hundreds of microseconds, which I did not measure here.

### 5.3 When polling wins

The break-even is simple. One interrupt costs c of CPU, and polling costs a whole core. At c = 2 µs an interrupt-driven design uses less CPU than polling only while the event rate is below `1 ÷ c`:

| Cost of one interrupt, c | Break-even event rate |
|---|---|
| 0.5 µs | 2,000,000 per second |
| 1 µs | 1,000,000 |
| **2 µs** | **500,000** |
| 5 µs | 200,000 |
| 10 µs (a cross-CPU wake-up in this VM) | 100,000 |

(With batching the interrupt rate is lower than the event rate, so polling wins later; this is the "no batching" bound.) Beyond CPU cost there are two more reasons to poll:

- **Latency and jitter.** Polling removes the entry cost, the wake-up of an idle core, and the variability of both. A dedicated spinning core reacts in the time of one cache-line transfer.
- **Predictability.** An interrupt can arrive at any time and perturb whatever is running; a polling design runs the same loop at the same pace.

And two reasons not to:

- **Power.** A polling core never sleeps. A battery device, or an access point at 3 a.m. with no clients, wants interrupts so the SoC can idle.
- **Scale.** Dedicating a core per queue works for a few queues per machine; a server with hundreds of devices or containers cannot afford it.

### 5.4 The designs in between

Most practical systems are hybrids.

- **NAPI** (section 4.5): interrupt to start, poll while busy.
- **Adaptive moderation**: raise the timer when busy.
- **Busy polling for sockets** (`SO_BUSY_POLL`, `net.core.busy_poll`): a thread that would otherwise sleep in `recv` polls the NIC's queue for a few microseconds first, trading CPU for latency.
- **`io_uring`**: `SQPOLL` makes a kernel thread poll the submission ring, so the application issues I/O with no system call; `IOPOLL` polls for storage completions instead of taking interrupts.
- **NVMe polled queues** and **hybrid polling**: sleep for about half the expected latency, then poll for the rest.
- **Poll-mode drivers (DPDK, SPDK)**: user-space drivers on cores that do nothing but run this loop, on memory pinned in huge pages, with the device mapped into the process. The packet loop is essentially:

```c
for (;;) {
    n = rx_burst(port, queue, pkts, 32);       // read up to 32 descriptors from the RX ring in RAM
    for (i = 0; i < n; i++) process(pkts[i]);  // no interrupt, no system call, no copy to the kernel
    tx_burst(port, queue, out, n_out);         // fill TX descriptors, one doorbell for the burst
}
```

  `rx_burst` looks at the *descriptor's* done flag in ordinary memory, which sits in this core's cache until the NIC writes it, so an empty poll costs a few nanoseconds and a full burst amortises the one doorbell and the cache misses over 32 packets. It never touches a device *register* on the fast path, for the reason in [tutorial 25](25_buses_pcie_and_io_mapping.md): a register read is a round trip.
- **Waiting without wasting power**: instructions that let a core wait on a memory location in a low-power state: `WFE` on Arm, `MWAIT` and (newer cores) `UMWAIT`/`TPAUSE` on x86. The core sleeps until the line changes or a timeout expires, which gives most of polling's latency at lower power. A plain spin loop should use `pause` (x86) or `yield`/`wfe` (Arm) so it does not hog the pipeline or hammer the line ([tutorial 19](19_cache_coherence.md): spin-lock behaviour).

In an interview the strong answer to "polling or interrupts?" is never one word: it names the event rate, the latency requirement, whether a core can be dedicated, and the power budget, then proposes the hybrid.

---

## 6. Timers and clocks

### 6.1 Two jobs, one idea

Hardware time does two different jobs, and mixing them up confuses most explanations.

1. **Telling the time**: a free-running counter that software reads (`rdtsc`, the Arm `CNTVCT_EL0`, RISC-V `time`).
2. **Doing something at a time**: a comparator. Software writes a deadline; when the counter reaches it, the timer raises an interrupt.

Everything below, from the 4 ms scheduler tick to a 5 µs `nanosleep`, is built from those two pieces.

### 6.2 x86

The history is a stack of layers, all still present:

- **PIT** (the 8253/8254, 1.193182 MHz): the original PC timer; slow to program, obsolete.
- **HPET** and the **ACPI power-management timer**: memory- or port-mapped counters. Reading one is a bus access, typically hundreds of nanoseconds to a microsecond or more, so they are used as references, not read in hot paths.
- **The local-APIC timer**: one per core, the source of the scheduler tick. Modes: one-shot, periodic, and **TSC-deadline**, in which software writes an absolute TSC value to a model-specific register and the core interrupts when the TSC reaches it. This VM uses it: `/proc/timer_list` shows `Clock Event Device: lapic-deadline` on both CPUs, and `dmesg` says `TSC deadline timer available`.
- **The TSC** (time-stamp counter): a 64-bit counter that counts at a constant rate on any CPU with the **invariant TSC** feature (flags `constant_tsc` and `nonstop_tsc` here, plus `tsc_known_freq`; `dmesg`: `tsc: Detected 2100.000 MHz processor`). `rdtsc` costs about 16 ns (34 to 40 TSC ticks), the cheapest clock there is. The kernel uses the TSC as the clock source when it can (`current_clocksource: tsc`; this VM also offers `kvm-clock`). Pitfalls: on old CPUs the TSC stopped in deep sleep or changed with the frequency; on multi-socket machines the TSCs were once not synchronised; after a VM migration to a host with a different frequency it must be rescaled; and `rdtsc` is not serialising, so it can execute earlier than the code you meant to time (use `rdtscp` or an `lfence`, [tutorial 20](20_memory_consistency_and_atomics.md)).

On Linux, `clock_gettime(CLOCK_MONOTONIC)` does not enter the kernel: the **vDSO** (section 2.7) reads the TSC and a scale factor from a shared page. `CLOCK_MONOTONIC` never jumps; `CLOCK_REALTIME` is stepped by NTP, so a timeout computed from it can fire early or never.

### 6.3 Arm and RISC-V

**Arm's generic timer** separates the two jobs cleanly. A system counter (64 bits) is readable at every level through `CNTPCT_EL0` (physical) and `CNTVCT_EL0` (virtual: the physical count minus an offset the hypervisor sets, so each guest has its own clock). Each core has timers, one per level (physical, virtual, hypervisor), each with a **compare value** (`CNTV_CVAL_EL0`, an absolute count), a **timer value** (`CNTV_TVAL_EL0`, a down-counter that fires at zero), and a control register (enable, mask, a status bit). The timer's interrupt is a **PPI** (section 3.3). The counter frequency, `CNTFRQ_EL0`, was implementation-defined, typically a few tens of MHz, until **Armv8.6, which fixed it at 1 GHz** (as QEMU's change to its default for newer CPUs notes). A firmware or kernel that assumes a particular frequency is a classic porting bug; read `CNTFRQ_EL0`.

**RISC-V** puts a 64-bit `mtime` counter and a per-hart `mtimecmp` compare register in the CLINT. The `time` CSR gives read access to `mtime` from lower modes. A **machine timer interrupt fires when `mtime ≥ mtimecmp` and stays asserted until you write a larger `mtimecmp`**, which is why a timer handler's first act is to program the next deadline. S-mode cannot write `mtimecmp`; it asks M-mode firmware through the SBI call `sbi_set_timer`, or on cores with the `Sstc` extension writes its own `stimecmp` CSR. A teaching kernel's tick is exactly this: handle the interrupt, set `mtimecmp = mtime + interval`, call the scheduler.

### 6.4 The tick, tickless idle, and the VM's reading

The kernel's periodic **tick** runs at `HZ` times a second on each busy core (this kernel: `CONFIG_HZ=250`, a 4 ms period, 8.4 million TSC ticks or about 12 million core cycles). It updates the time, charges CPU time to the running task, expires timers, drives RCU, and, when the running task has used its slice, requests preemption. It is the reason a `while(1);` loop cannot hold a core forever: the tick interrupt takes the core away, and the return path calls the scheduler.

The measured tick: over a 4-second window with one thread spinning on CPU 0, `/proc/interrupts` counted **1,004** local-timer interrupts on that CPU, **251 a second**, exactly `HZ`.

Two variants stop the tick from disturbing idle or dedicated cores:

- **Tickless idle** (`NO_HZ_IDLE`, which this kernel has): an idle core turns the tick off and sleeps until the next timer is due, so it can reach deep C-states and idle servers draw little power.
- **Full tickless** (`NO_HZ_FULL`; this kernel is built without it): a core running a single task gets no tick either. Combined with `isolcpus`, moving all interrupts away (`irqaffinity`), and a polling thread, it is how DPDK and real-time deployments reach microsecond-level jitter.

**Measuring jitter, and a trap.** The standard method is a loop that reads a fast clock continuously and records any gap that is much bigger than one loop iteration; whatever took the core away shows up as a gap.

```c
uint64_t prev = __rdtsc();
for (;;) {
    uint64_t t = __rdtsc();
    if (t - prev > THRESHOLD) record(prev, t - prev);   // something held the CPU for (t - prev) cycles
    prev = t;
}
```

Run on CPU 0 here for 4 seconds, it recorded **13,163 gaps over 0.5 µs, about 3,300 a second**, while the kernel's own counter showed only 251 timer interrupts a second and a few dozen device interrupts. Of the gaps, about 4,600 were 10 to 20 µs long and about four in five of those fell within the same 50 µs of a 1 ms cycle: a **1 kHz train that the guest kernel cannot have produced** (its tick is 250 Hz). Together the gaps took 3 to 4 % of the CPU. The guest's own "steal time" counter accounted for only a fraction of it (10 to 30 ms against 95 to 120 ms inside the gaps), so I cannot say where all of it went. **What I can say is that more than nine in ten of the interruptions were not guest interrupts.** They were the host and the virtualisation layer taking the physical CPU: invisible to `/proc/interrupts` and indistinguishable, from inside, from a real interrupt.

That is the trap: **a hiccup test inside a VM measures the hypervisor as much as your own kernel.** On bare metal the same test (Linux ships it as the `hwlat` tracer) shows the timer tick, other interrupts, and, on x86, **system-management interrupts (SMIs)**, which firmware handles invisibly to the operating system for tens of microseconds to milliseconds. Remedies are `isolcpus`, `nohz_full`, moving device interrupts off those cores, and switching off the firmware features that raise SMIs.

### 6.5 Preemption by the timer, and the RTOS version

The scheduler tick preempts a task like this: the timer interrupt arrives, the kernel's handler finds the task has used its slice and sets a flag, and on the way back to user mode the kernel sees the flag, saves this task's registers, loads another's, and returns *into the other task*. From the first task's point of view nothing happened except that time passed.

The Cortex-M version, which is also the shape of most RTOS ports: the **SysTick** timer (a 24-bit down-counter built into the core) generates the periodic interrupt; its handler advances the tick count and *pends* a **PendSV** exception, which has the lowest priority; PendSV runs only after every other interrupt has finished and does the context switch. The deferral guarantees that a context switch never happens in the middle of another interrupt handler.

### 6.6 Counter widths and wrap-around

Counters wrap. How soon depends on the width and the rate:

| Counter | Wraps after |
|---|---|
| 16-bit timer at 1 MHz | 65.5 ms |
| 32-bit counter at 1 GHz | 4.29 s |
| 32-bit timer at 1 MHz | 1.19 hours |
| 32-bit `jiffies` at `HZ = 1000` | 49.7 days |
| 32-bit `jiffies` at `HZ = 250` | 198.8 days |
| 64-bit counter at 2.1 GHz (the TSC here) | 278 years |
| 64-bit counter at 1 GHz (Armv8.6) | 584 years |
| 64-bit counter at 24 MHz | 24,000 years |

A 64-bit counter never wraps in practice; a 32-bit one wraps during the life of your product, and the bug appears on day 49.7 of continuous operation. The fix is one rule: **compare by subtracting, in unsigned arithmetic, never by comparing absolute values.**

```c
uint32_t start = now();
/* ... */
if ((uint32_t)(now() - start) >= timeout)   // right: the subtraction wraps correctly
if (now() >= start + timeout)               // wrong: start + timeout may wrap past zero
```

With `start = 0xFFFFFFF6` (10 ticks before the wrap) and `timeout = 100`, the sum `start + timeout` wraps to 90. Two ticks later (`now = 0xFFFFFFF8`) the wrong test says `0xFFFFFFF8 ≥ 90`, **true: the timeout "expires" after 2 ticks**, while the right test sees `elapsed = 2`, false. (Run it: the output is `elapsed 2, right check 0, wrong check 1`.) Linux's `time_after(a, b)` is `(long)(b - a) < 0` for the same reason, and it starts `jiffies` at −300 seconds so that a wrap bug shows up five minutes after boot and not 49 days.

---

## 7. DMA: devices that move data themselves

### 7.1 Why

The CPU could copy data from a device itself ("programmed I/O"), but each read of a device register is an uncached round trip of about a microsecond ([tutorial 25](25_buses_pcie_and_io_mapping.md)). Reading a 1,518-byte frame 4 bytes at a time is 380 reads, **roughly 380 µs, about 300 times the 1.23 µs the frame takes on a 10 GbE wire.** So the device must move the data itself: **direct memory access** (DMA). The device is a bus master and reads or writes memory with ordinary memory requests; the CPU only tells it where.

Two kinds of engine exist. Almost every device has its own (NIC, NVMe, GPU copy engines, the Wi-Fi MAC). Microcontrollers and SoCs also have a shared **DMA controller** that copies memory to memory or between memory and a peripheral. Textbooks describe its modes: *burst* (hold the bus until the block is moved), *cycle stealing* (one word at a time, interleaved with the CPU's accesses) and *transparent* (only when the CPU is not using the bus). On a modern packet-switched interconnect everything is arbitrated per request, so those differences reappear as **burst length** and **quality-of-service settings**.

### 7.2 Descriptors and scatter-gather

A transfer is described by a **descriptor**: at least a DMA address and a length, with flags and status. Memory that a program sees as one buffer is physically scattered over 4 KB pages ([tutorial 15](15_virtual_memory_and_the_tlb.md)); the device cannot use virtual addresses. **Scatter-gather** DMA solves it: the descriptor names one piece, and a *list* of descriptors, chained or in a ring, describes the whole buffer, so the data are never copied into a contiguous staging area. Without it the driver must either copy, or allocate physically contiguous memory (which is scarce after the system has been running for a while).

### 7.3 The descriptor ring

The data structure that connects almost every high-speed device to its driver is a **circular array of descriptors in ordinary RAM**, with a producer and a consumer walking around it. Take an 8-slot receive ring. Three regions follow each other around the circle, separated by three indices:

```text
              next_to_clean            head                         tail
                   v                     v                            v
 slot:      |   0   |   1   |   2   |   3   |   4   |   5   |   6   |   7   |
 owner:        driver  driver  driver  NIC     NIC     NIC     NIC    (free)
 meaning:      done, waiting to be     empty buffers the NIC          slot 7 is not
               processed by software   may fill, in order             yet refilled
```

- The **driver** posts *empty buffers* (it writes each descriptor with a DMA address) and then tells the NIC by writing the new **tail** to a device register: the **doorbell** (a posted MMIO write, [tutorial 25](25_buses_pcie_and_io_mapping.md)). The NIC may use the descriptors from its `head` up to, but not including, `tail`.
- The **NIC** fetches the descriptor at `head`, writes the packet into the buffer it names, then **overwrites that descriptor with a completion**: length, status bits (**DD**, "descriptor done", and **EOP**, end of packet), checksum result, RSS hash. It advances `head`.
- The driver, polling or after an interrupt, looks at the descriptor at `next_to_clean`; if DD is set, it processes the packet, then refills the slot with a new buffer.
- **Empty** (for the driver): `next_to_clean == head`: nothing is done. **Out of buffers** (for the NIC): `head == tail`: it has nowhere to put the next packet, and drops it. Real NICs count this (`rx_missed_errors`, `rx_no_buffer_count`). One slot is always left unused so "full" and "empty" are distinguishable.

Four properties make this pattern so widely reused.

1. **Batching at every step.** The doorbell is written once per batch of refills; the NIC fetches several descriptors per PCIe request; the completions are written back in groups; the interrupt is moderated. [Tutorial 25](25_buses_pcie_and_io_mapping.md) section 3.7 computed what each of these saves.
2. **No locks.** Each ring has exactly one producer and one consumer for each index, so neither side waits for the other.
3. **The completion is in RAM.** The driver learns that a packet arrived by reading a descriptor that sits in its cache until the NIC writes it, not by reading a device register.
4. **The interrupt becomes optional.** If software polls the DD bits, no interrupt is needed at all (section 5).

The ring also tells you how long you have: if nobody serves it, a ring of N descriptors overflows after `N × (the time per packet)`:

| Link and frame | Time per packet | Ring of 256 | Ring of 1,024 | Ring of 4,096 |
|---|---|---|---|---|
| 10 Gb/s, 64 B | 67.2 ns | **17.2 µs** | 68.8 µs | 275 µs |
| 10 Gb/s, 1,518 B | 1,230 ns | 315 µs | 1.26 ms | 5.0 ms |
| 25 Gb/s, 64 B | 26.9 ns | 6.9 µs | 27.5 µs | 110 µs |
| 100 Gb/s, 64 B | 6.7 ns | **1.7 µs** | 6.9 µs | 27.5 µs |

A cross-CPU wake-up cost 10 µs in this VM. On a 10 GbE port with minimum-size frames and 256 descriptors, a single such delay loses packets, which is why rings are sized in the hundreds to thousands and why interrupt latency matters for small packets. But a bigger ring has a price too (section 7.7).

### 7.4 The ordering rule: the most common DMA bug

The descriptor ring is shared memory between two agents that do not coordinate except through it, and the ordering of their stores and loads is exactly the problem of [tutorial 20](20_memory_consistency_and_atomics.md). Two rules cover almost everything.

**CPU to device** (transmit, or posting a receive buffer): the device may read a descriptor the instant it sees the "go" signal, so everything the descriptor says must be visible *before* the signal. There are two signals, the descriptor's ownership bit and the doorbell, so two barriers:

```c
ring[idx].dma_addr = dma_addr;           // 1. fill in the descriptor
ring[idx].len      = len;
dma_wmb();                               // 2. address and length visible before the OWN bit
ring[idx].flags    = OWN;                // 3. hand the descriptor to the device
wmb();                                   // 4. every descriptor write visible before the doorbell
writel_relaxed(idx + 1, doorbell);       // 5. tell the device (posted MMIO write)
```

(`writel()`, without `_relaxed`, already contains the barrier of step 4. This sketch uses stand-ins for the kernel macros and **compiles; it is not a working driver**.)

**Device to CPU** (a completion): the device writes the data and then the status; the CPU must read the status first, and only then the rest.

```c
if (!(READ_ONCE(ring[idx].flags) & DONE)) return 0;   // look at the status first...
dma_rmb();                                            // ...and only then at the rest
len = ring[idx].len;                                  // valid because DONE was seen
```

Why this holds on the PCIe side: posted writes from one device on the same traffic class are delivered in order ([tutorial 25](25_buses_pcie_and_io_mapping.md): a read cannot pass a posted write, and writes do not pass each other), so a NIC that writes the data and then the descriptor guarantees that the CPU, seeing the descriptor, will find the data (unless the Relaxed Ordering attribute is set, which some drivers enable for data buffers and which removes that guarantee).

Why this matters per architecture: **on x86 (TSO) stores are not reordered with other stores and loads are not reordered with loads, so `dma_wmb()` and `dma_rmb()` compile to nothing but a compiler barrier**. A driver that is missing them works perfectly on a PC. On **Arm and RISC-V they are real instructions (`dmb oshst`, `dmb oshld`, or a `fence`)**, and the missing barrier shows up as a descriptor that the device reads half-written, once in a million times. This is the single most common reason that a driver ported from x86 to an Arm SoC (an access point, say) fails intermittently.

### 7.5 Coherence: does the device see what the CPU wrote?

[Tutorial 19](19_cache_coherence.md) and [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) put the rule in one line each; here is the whole picture.

- **x86 PCIe DMA is cache-coherent.** Inbound reads snoop the CPU caches, and inbound writes invalidate or update the cached copy. A driver does not flush caches for DMA.
- **Many Arm SoCs are not coherent for every master.** A DMA master wired through a coherent interface (AXI ACE-Lite or CHI) is "I/O-coherent"; one that is not snoops nothing. The device tree marks coherent devices with `dma-coherent`, and the driver or its platform must know which.

For a **non-coherent** device the software rules are:

| Direction | Before the DMA | After the DMA, before the CPU reads |
|---|---|---|
| **Device reads memory** (transmit) | **clean** the CPU cache lines of the buffer (write dirty data back to memory) | nothing |
| **Device writes memory** (receive) | **invalidate** the lines (so a stale or dirty line cannot later be evicted *over* the new data) | **invalidate** again (a speculative load or prefetch may have brought stale lines back in during the DMA) |

Linux wraps these in the **DMA API**: `dma_map_single()` / `dma_unmap_single()` for the whole lifetime of a streaming buffer and `dma_sync_single_for_cpu()` / `dma_sync_single_for_device()` for handing it back and forth, with a direction argument (`DMA_TO_DEVICE`, `DMA_FROM_DEVICE`, `DMA_BIDIRECTIONAL`). On a coherent platform they cost almost nothing; on a non-coherent one they run cache-maintenance instructions over the buffer. Use the wrong direction, or touch the buffer between map and sync, and you get stale data on Arm and a perfect run on x86.

Two traps for non-coherent systems:

- **Cache-line sharing.** Invalidation works on whole lines. If a receive buffer shares a line with a neighbour (a spinlock, a counter), invalidating it **destroys the neighbour's recent writes**. DMA buffers must be cache-line-aligned and padded to a whole line (Linux's `ARCH_DMA_MINALIGN` is 128 on arm64).
- **Descriptor rings need another approach.** They are read and written by both sides constantly. `dma_alloc_coherent()` gives a region mapped uncached or write-combined on non-coherent systems: CPU accesses are slow, but no per-access maintenance is needed, and ring memory is small.

Also: the descriptor holds a **DMA address** ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 5.1), never a virtual pointer and, with an IOMMU, not a physical address either. A device that can address only 32 bits (a DMA mask) cannot reach RAM above 4 GiB; the kernel then **bounces** the data through a low buffer (`swiotlb`), a copy that costs time. This VM boots with `swiotlb=noforce`, which means the kernel is told not to bounce unless it must.

### 7.6 The Wi-Fi access-point version

On an AP system-on-chip (the kind of platform this track's owner builds software for) the same machinery appears under different names. The radio's MAC is a bus master with its own **receive ring** and several **transmit rings**, typically one per queue or access category so that a voice frame is not stuck behind bulk data. Frames are described by **chains of descriptors** (scatter-gather: a header buffer and a payload buffer, or several aggregated subframes), the driver rings a **doorbell** per batch, and completions arrive by interrupt on a **GIC** line or are polled. Two points decide whether such a driver is correct: the barriers of section 7.4, and, on an Arm SoC, whether the MAC's DMA is coherent (section 7.5). The throughput limit is nearly always the per-packet CPU budget of section 4.3, not the radio.

### 7.7 DDIO and why a bigger ring can be slower

On recent Intel server cores the NIC's inbound writes do not go to DRAM: **Data Direct I/O (DDIO)** places them straight into the last-level cache, so the CPU that polls the ring reads the packet from L3 instead of DRAM (about 70 ns less per access in the measurements quoted in [tutorial 25](25_buses_pcie_and_io_mapping.md) section 3.6). The catch ([Farshin et al., ATC 2020](https://www.usenix.org/system/files/atc20-farshin.pdf)): by default DDIO may use only **two ways of the LLC** (the number is set by a model-specific register, `IIO LLC WAYS`, and can be raised).

The buffers in flight can easily exceed that. A ring of 4,096 descriptors with 2 KiB buffers is `4096 × (2048 + 16)` bytes `= 8.06 MiB` per queue; sixteen queues are 129 MiB. If the DDIO share of the cache is a few MiB (with a hypothetical 32 MiB, 16-way cache, two ways are 4 MiB), newly arriving packets **evict packets that have been DMA'd but not yet processed**, and the CPU then reads them from DRAM after all, while the NIC later has to fetch the processed ones back from DRAM to transmit them. The paper calls this **leaky DMA**. Its measurements show the effect: with 1,500-byte packets, growing the receive ring to 4,096 descriptors dropped the DDIO hit rate to **about 40 %**, and even 128 descriptors left about 10 % missing.

So ring size is a trade between two failures: too small and bursts overflow it (section 7.3); too large and the working set falls out of the cache that DDIO feeds. The remedies: size rings for the burst you must absorb, no more; process packets promptly (a faster poll loop keeps the in-flight data small); give DDIO more cache ways where the platform allows; and spread flows over more queues only if the cores can keep up (more queues means more buffers in flight).

---

## 8. A packet's journey: the NIC

Everything so far (interrupts, moderation, rings, barriers, coherence) comes together in one path: a packet from the wire to your program and back. The NIC here is a generic multi-queue Ethernet controller; real parts differ in detail, not in shape.

### 8.1 Setup, once

The driver allocates the receive rings and their buffers, maps them for DMA (section 7.5), writes each ring's base address and length into the NIC (a few MMIO writes), fills the descriptors with buffer addresses and rings the doorbell, programs the RSS key and table (section 8.5), and enables one MSI-X vector per queue with its affinity (section 3.6).

### 8.2 Receive, one packet

1. **On the wire.** The MAC checks the frame length and the Ethernet CRC and may filter on MAC address and VLAN. The NIC computes the RSS hash over the header fields and picks a queue (section 8.5); it can also verify the TCP/UDP checksum.
2. **Descriptor fetch.** The NIC reads the next descriptor of that queue (a DMA read, normally prefetched several at a time) to learn the buffer's DMA address.
3. **Data write.** It writes the frame into that buffer (posted PCIe writes; with DDIO they land in the last-level cache, section 7.7).
4. **Completion write.** It overwrites the descriptor with length, the DD bit, checksum status and the RSS hash, *after* the data (the ordering of section 7.4).
5. **Interrupt.** It writes the queue's MSI-X vector, subject to moderation (section 4.4).
6. **Top half.** The core takes the interrupt, masks that queue, and schedules NAPI (section 4.5).
7. **Poll.** The driver loops: descriptor at `next_to_clean` has DD? Wrap the buffer in the kernel's packet structure (no copy), pass it up, refill the slot with a fresh buffer, and write the tail doorbell every few packets.
8. **Protocol processing.** IP, then TCP or UDP, then the socket's receive queue; the thread waiting in `recv` is woken, which costs what section 5.2 measured.
9. **The copy.** `recv` copies the payload into the application's buffer: the one copy of the conventional path. Interfaces that skip it (AF_XDP, DPDK) let the application read the ring buffers directly.

This VM's receive counters (packets, interrupts and `NET_RX` softirqs for a download) and the per-packet budget at 10, 25 and 100 Gb/s are in [tutorial 31](31_arm_vs_x86_and_putting_it_together.md) section 9 and Problem D; its section 12 follows one system call on x86-64 and AArch64.

### 8.3 Transmit, one packet

The application's `send` copies the data into a kernel buffer, the stack adds the headers, and the driver builds the **descriptors**: typically one for the headers and one per payload fragment (scatter-gather). It rings the doorbell, batching when more packets are queued behind it. The NIC then fetches the descriptors and the data by **DMA reads**, which are non-posted round trips (547 ns each in the published measurements, [tutorial 25](25_buses_pcie_and_io_mapping.md) section 3.6), so a NIC keeps many reads in flight, or transmit throughput suffers (Little's law, the same arithmetic as Problem A(d) there). It computes checksums, cuts large segments (below), sends the frames, writes a completion, and (moderated) interrupts; the driver then frees the buffers, often lazily at the next transmit or in the same poll as receive.

Receive is *writes* to memory, transmit is *reads*. Posted writes need no answer; reads are round trips, so the transmit side needs many requests outstanding to keep up.

### 8.4 Offloads: work moved off the CPU

At 100 Gb/s a core has 369 cycles per 1,518-byte frame and 20 per 64-byte frame ([tutorial 24](24_accelerators_and_fpgas.md) section 6.3). Touching every byte or running the stack once per frame does not fit, so NICs take over the cheap, regular parts.

| Offload | What the hardware does | What it saves |
|---|---|---|
| **Checksum** | computes (transmit) or verifies (receive) the IP/TCP/UDP checksum | reading every byte of every packet in software |
| **TSO / GSO** (segmentation offload) | the stack hands over one large segment (up to 64 KiB) with one header; the NIC cuts it into MSS-sized frames and fills in sequence numbers and checksums | about 45 passes through the stack: one 64 KiB segment is about 45 frames (`65,536 ÷ 1,448`), or 5.5 µs of wire time at 100 Gb/s, handled in *one* pass |
| **GRO / LRO** (receive aggregation) | merges consecutive segments of one flow into one large packet before the stack sees them (GRO in the kernel, LRO in hardware; routers turn LRO off because it changes the packets) | the same amortisation, on the receive side |
| **RSS hash and VLAN** | the hash and the VLAN tag arrive in the descriptor; the NIC strips or inserts tags | parsing and hashing in software |
| **Hardware timestamps** | stamps the packet at the MAC (PTP) | the scheduling jitter that a software timestamp would include |

### 8.5 Receive-side scaling (RSS)

One queue is one ring, one interrupt and one core's worth of processing. To use *N* cores the NIC needs *N* queues and a rule for which packet goes where. Two requirements pull in opposite directions: every packet of one **flow** (the same two IP addresses and two ports) must go to the same queue, so order is kept and the flow's state (TCP, connection tracking) stays in one core's cache without locks; and different flows must spread evenly.

The standard rule is the **Toeplitz hash**. The input is the flow's header fields (for IPv4 TCP or UDP, 12 bytes: source address, destination address, source port, destination port). The key is a 40-byte secret. For every input bit that is 1, XOR in the 32-bit window of the key that *starts at that bit's position*. The low bits of the 32-bit result index an **indirection table** (typically 128 entries) whose entries are queue numbers. In Python:

```python
KEY = bytes.fromhex("6d5a56da255b0ec24167253d43a38fb0d0ca2bcbae7b30b477cb2da38030f20c6a42b73bbeac01fa")

def toeplitz(data: bytes, key: bytes = KEY) -> int:
    keybits, klen, h = int.from_bytes(key, "big"), len(key) * 8, 0
    for i in range(len(data) * 8):
        if (data[i // 8] >> (7 - i % 8)) & 1:                  # input bit i is set ...
            h ^= (keybits >> (klen - 32 - i)) & 0xFFFFFFFF      # ... XOR in the 32 key bits that start at bit i
    return h

src, dst = bytes([66, 9, 149, 187]), bytes([161, 142, 100, 80])
ports = (2794).to_bytes(2, "big") + (1766).to_bytes(2, "big")
print(hex(toeplitz(src + dst + ports)), hex(toeplitz(src + dst)))      # 0x51ccc178 0x323e8fc2
```

**Checked**: this reproduces all five IPv4 rows of Microsoft's published [verification table](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/verifying-the-rss-hash-calculation) (the first is 66.9.149.187:2794 to 161.142.100.80:1766, hash `0x51ccc178` with the ports and `0x323e8fc2` with the addresses only), including the input order (source address, destination address, source port, destination port, in network byte order).

What it does with traffic, computed with the same function and the indirection table filled `entry % queues`:

- **Spread.** 10,000 random clients connecting to one server spread over 8 queues as 1,206, 1,231, 1,286, 1,253, 1,221, 1,268, 1,262 and 1,273: every queue within 3.5 % of the ideal 1,250.
- **A few flows spread badly.** Eight connections from one client to one server (source ports 40000 to 40007, destination 10.0.0.9:443), 4 queues: they landed on queues 3, 2, 3, 2, 3, 2, 3, 2, so **two of the four queues got everything** and two sat idle. Hashing balances *many* flows; with a handful, chance decides.
- **One flow always uses one queue.** The same tuple gives the same hash for every packet. A single TCP connection, one tunnel, or one `iperf` stream is therefore limited to **one core**, however many queues the NIC has. This is the "elephant flow" problem, and it is why a single-stream benchmark on a 100 GbE NIC often tops out far below line rate.
- **The default key is asymmetric.** The two directions of one connection hash differently (`0x7cc63cb3` forward, `0x12d9c17f` reverse for 10.0.0.7:40000 to 10.0.0.9:443; with 4 queues they happened to share queue 3, with 8 queues they went to queues 3 and 7). A stateful firewall, NAT or IDS that needs both directions on one core cannot rely on it. Repeating the pair `6d5a` through the whole key makes the hash symmetric: **both directions gave `0xfb3afb3a`**, as verified here.

Related mechanisms you should be able to name: **RPS** (receive packet steering, RSS done in software on one core, for NICs without hardware queues), **RFS** and **aRFS** (steer a flow to the core where the *application thread* last read it, aRFS using hardware filters), and **Flow Director** (exact-match rules in hardware). On a real Linux NIC, the commands are `ethtool -l` (queues), `-x` (key and indirection table; `-X` sets them), `-g` (ring sizes), `-C` (coalescing), `-k` (offloads) and `-S` (counters).

### 8.6 This VM's NIC

The virtio network device (`virtio7`) is a small, real example. Its negotiated feature bits, read from `/sys/bus/virtio/devices/virtio7/features`:

| Bit | Feature | Meaning |
|---|---|---|
| 0, 1 | `CSUM`, `GUEST_CSUM` | checksum offload both ways |
| 7, 8, 11, 12 | `GUEST_TSO4/6`, `HOST_TSO4/6` | TCP segmentation offload both ways |
| 10, 14 | `GUEST_UFO`, `HOST_UFO` | UDP fragmentation offload |
| 15 | `MRG_RXBUF` | one packet may span several receive buffers (merged) |
| 29 | `RING_F_EVENT_IDX` | notification suppression (section 4.4) |
| 32 | `VERSION_1` | the modern interface |

Not offered: `MQ` (multiqueue), so `/sys/class/net/eth0/queues/` lists exactly `rx-0` and `tx-0`. With a single queue pair **RSS has nothing to spread across**, one interrupt vector handles all receive work (section 3.6: `virtio7-input.0`), and the whole path is single-core. The softirq numbers of section 4.5 (`dev_weight` 64, budget 300) are real kernel settings here. (`ethtool` is not installed in this VM, so queue counts come from `/sys/class/net/eth0/queues/`; on a real machine use `ethtool -l`.)

### 8.7 "Where did my packet go?"

A dropped packet is lost at one of several places, each with its own counter (the names below are typical; they vary by driver), and the right fix depends on which. This is also an interview question that separates people who have debugged a data plane from those who have not.

| Layer | Counter | What it means | Fix |
|---|---|---|---|
| NIC, bad frame | `rx_crc_errors`, length errors (`ethtool -S`) | corrupted on the wire | cable, optics, duplex |
| NIC, no free buffer in the ring | `rx_missed_errors`, `rx_no_buffer_count` | the driver did not refill fast enough, or the ring is too small, or the interrupt came too late | larger ring (`ethtool -G`), more queues, faster poll, check moderation |
| NIC, internal FIFO overflow | `rx_fifo_errors` | the NIC's internal buffer overflowed because DMA could not drain it fast enough (link, IOMMU or memory bandwidth are the usual suspects) | [tutorial 25](25_buses_pcie_and_io_mapping.md): link width and speed, IOMMU mode |
| Softirq cut short | `/proc/net/softnet_stat`, third column (`time_squeeze`) | the poll ran out of budget and is behind | more queues, raise `netdev_budget`, busy polling |
| Per-CPU backlog queue full | `softnet_stat`, second column (`dropped`) | `netdev_max_backlog` exceeded (RPS, loopback, tunnels) | spread the work, raise the limit |
| Socket buffer full | `netstat -s` / `nstat`: UDP "receive buffer errors", `ss -m` | the application reads slower than packets arrive | larger `SO_RCVBUF`, faster application, more threads |

Read it top to bottom: the further down the packet travelled before it died, the more CPU it already cost, which is the livelock lesson again.

---

## 9. Other rings: NVMe and virtio

### 9.1 An NVMe command

[Tutorial 17](17_storage_and_reliability.md) section 4 introduced the queues. With the machinery above the whole life of a read is short.

1. The host writes a **64-byte submission entry** (opcode, namespace, the DMA address of a buffer, the starting block and length, a command ID) into the next slot of a **submission queue** (SQ) in RAM.
2. It writes the SQ's new **tail** to the controller's **doorbell** register (one posted MMIO write; several commands can share one doorbell write).
3. The controller fetches the entry by DMA, executes the read, and DMA-writes the data into the buffer.
4. It writes a **16-byte completion entry** to the matching **completion queue** (CQ): the command ID, the controller's current SQ head (so the host knows which SQ slots are free again), a status, and a **phase tag** bit.
5. It raises the CQ's MSI-X vector (optionally coalesced: an *aggregation threshold* in entries and an *aggregation time* in 100 µs units).
6. The host reads CQ entries while their phase tag matches what it expects, completes the requests, and writes the **CQ head doorbell**.

The **phase tag** is the neat part: the controller flips the bit it writes each time the CQ wraps, so the host recognises a *new* entry by comparing the bit with its own expectation, with no need to read a head register (a slow MMIO read) and no need to clear entries after use. It is also what makes **polling** natural: spin on the next entry's phase bit in RAM.

For the arithmetic, take 4 KiB random reads at 1 million IOPS and the assumed c = 2 µs: one interrupt per completion would cost **2 cores**; coalescing 8 completions per interrupt, **0.25 cores**; polling, one core spinning (and finishing sooner, since there is no interrupt latency). That is why the fastest storage stacks (SPDK, `io_uring` with polling) poll, and why NVMe gives one queue pair to each core so that the submission and the completion interrupt both stay local.

### 9.2 The virtio ring (what every device in this VM uses)

Virtio is the standard interface between a guest operating system and a (usually virtual) device. Its *split ring* is three arrays in guest memory:

```c
struct vring_desc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; };   // 16 bytes
// flags: NEXT = 1 (chained to desc[next]), WRITE = 2 (the device writes into this buffer), INDIRECT = 4
// available ring (driver -> device): flags, idx, ring[N] of descriptor indices, used_event
// used ring      (device -> driver): flags, idx, ring[N] of { id, len },       avail_event
```

The driver fills a chain of descriptors (a request header, then data buffers: scatter-gather), puts the index of the first in the **available ring**, advances `avail.idx`, and **notifies** the device (a write to a notification register, the "kick"; in a VM this is an exit; hypervisors keep it short by having the kernel signal an event file descriptor, `ioeventfd`, so the vCPU resumes while another thread handles the request). The device processes the chain, writes `{id, len}` into the **used ring**, advances `used.idx`, and interrupts the driver. Both sides can *suppress* the other's signal: with `EVENT_IDX` (feature bit 29, present in this VM's NIC) the driver writes `used_event` ("interrupt me only after you pass entry *k*") and the device writes `avail_event` ("kick me only after entry *m*"). A newer *packed ring* (feature bit 34, not negotiated here) merges the three arrays into one with wrap-counter flags, for fewer cache lines touched per request.

### 9.3 One pattern, three dialects

| | NIC (Intel-style) | NVMe | virtio |
|---|---|---|---|
| Work goes in | receive: empty-buffer descriptors; transmit: data descriptors | 64-byte entry in the SQ | descriptor chain, index in the available ring |
| Producer signals with | tail register (doorbell) | SQ tail doorbell | notification ("kick") |
| Consumer reports with | write-back into the descriptor (DD bit) | 16-byte entry in the CQ with a phase tag | `{id, len}` in the used ring |
| Freshness test | DD flag | phase tag | `used.idx` |
| Interrupt | per-queue MSI-X, moderated (ITR) | per-CQ MSI-X, coalescing | per-queue vector, `EVENT_IDX` |
| Entry size | 16 bytes | 64 / 16 bytes | 16 bytes |

Everything in the table is the same four decisions made slightly differently: *where are the indices, who owns each slot, how does the consumer know a slot is ready, and how often do we interrupt.* A new device's driver, a GPU's command ring, a Wi-Fi MAC's queues, an accelerator's descriptor engine: learn the pattern once and read the data sheet for the four answers.

---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Latency spikes of tens of microseconds at regular intervals on a "quiet" core | the timer tick, device interrupts, IPIs (TLB shootdowns, other threads' `munmap`), firmware SMIs; in a VM, the hypervisor | `isolcpus`, `nohz_full`, move interrupts away (`irqaffinity`), `hwlat`, watch `/proc/interrupts` (`LOC`, `CAL`, `TLB`) before and after |
| One core at 100 % softirq on a many-core server with a fast NIC | all flows hash to one queue (an elephant flow, or a single queue configured), or all vectors pinned to CPU 0 | `ethtool -l/-x`, check `/proc/interrupts` per queue, raise queue count, RFS/aRFS; test with many flows |
| Drops with `rx_missed_errors` while the CPU is idle | ring too small for the latency (wake-ups, moderation), or interrupts delayed | larger ring (but see DDIO), shorter moderation, busy polling, check `time_squeeze` |
| High interrupt rate, low throughput, CPU busy | livelock-like: per-packet interrupts and early work that is later dropped | NAPI is on? moderation on? drop earlier; see section 4.6 |
| A low-rate request/response service has 50 to 100 µs added latency | fixed interrupt-moderation timers, or deep C-states on wake-up | adaptive moderation or `rx-usecs 0`, disable deep C-states for the service (`/dev/cpu_dma_latency`), busy polling |
| Corrupted or stale data after DMA, only on the Arm board | missing cache maintenance or missing barriers (the x86 box hides both) | use the DMA API with the right direction, `dma_wmb`/`dma_rmb`, align buffers to the cache line |
| The device "ignores" a doorbell, or reads half a descriptor, one time in a million | store ordering: OWN bit before the fields, or doorbell before the descriptor | barriers of section 7.4; `writel` rather than `writel_relaxed` unless you ordered it yourself |
| `irq NN: nobody cared` and the line goes dead | a level-triggered line nobody cleared, or a shared line with no owner | clear the source in the handler, check trigger type and sharing |
| An interrupt handler sleeps or hangs | it called something that blocks, or took a lock the interrupted code holds | defer to a bottom half or a thread; use `spin_lock_irqsave` for data shared with the handler |
| System-call-heavy code is slow | 100 to 200 ns per call, more with mitigations and in VMs | batch (`readv`, `sendmmsg`, `io_uring`), use the vDSO for time, avoid per-byte calls |
| Timestamps go backwards or timeouts fire early | non-monotonic clock, unsynchronised TSC, or `start + timeout` overflow | `CLOCK_MONOTONIC`, compare by subtracting (section 6.6) |
| A `SIGSEGV`/`SIGILL`-based trick is slow | each fault delivered to a signal handler costs about 1.5 µs measured here (about 10 µs for `#UD`, which this VM's hypervisor intercepts) | do not put faults on a hot path; use explicit checks |
| A VM is much slower than the host for the same code | sensitive operations exit: `cpuid`, IPIs, EOI, timer programming | check what the code does; paravirtual interfaces; pin and dedicate cores |
| A single TCP stream cannot exceed a fraction of the link | one flow, one queue, one core | more parallel streams; multi-queue-aware transport; offloads on; larger MTU |

---

## 11. Mistakes that cost points

1. **Calling every kernel entry a "trap" or an "interrupt".** Say which: asynchronous interrupt, fault (saved PC at the instruction), trap (after it), or abort.
2. **Saying a fault's handler resumes at the next instruction.** For a fault it re-executes the same instruction (that is why a page fault can be repaired). For a trap it continues after it.
3. **Forgetting that RISC-V's `sepc` points at the `ecall`** (add 4), while x86's `rcx` and Arm's `ELR` already point past the call.
4. **Believing the hardware pushes the registers.** x86 pushes five words and an error code; Arm and RISC-V save only the PC and status into registers; the handler saves the rest (the Cortex-M NVIC is the exception, stacking eight).
5. **Mixing up priority directions.** On x86 a higher vector is a higher priority; on a GIC a lower number is.
6. **Treating a level-triggered interrupt like an edge.** If the handler does not clear the cause at the device, it fires again at once.
7. **Putting slow or blocking work in the top half.** It runs with interrupts masked and cannot sleep; defer.
8. **Quoting "an interrupt costs X" as one number.** It is a hardware entry, a handler, usually an MMIO read, cache pollution, and sometimes a wake-up from deep idle or a VM exit. Say which you mean.
9. **Reading a device register inside the interrupt handler on the fast path.** One PCIe read is about a microsecond on bare metal and tens of microseconds in this VM ([tutorial 25](25_buses_pcie_and_io_mapping.md)).
10. **Assuming polling is always faster or always wasteful.** Polling wins above roughly `1 ÷ c` events a second, or when latency and jitter matter and a core can be spared; it loses on power and when cores are scarce.
11. **Treating interrupt moderation as free.** A fixed timer adds its full length to the latency at low load.
12. **Forgetting barriers because it works on your x86 laptop.** `dma_wmb` and `dma_rmb` are no-ops on x86 and real instructions on Arm and RISC-V.
13. **Forgetting cache maintenance on non-coherent Arm DMA**, or sharing a cache line between a DMA buffer and other data.
14. **Handing the device a virtual address.** It needs a DMA address from the DMA API.
15. **Comparing timestamps with `now >= start + timeout`.** Subtract in unsigned arithmetic.
16. **Reading every gap in a jitter test as an interrupt.** In a VM most are the hypervisor.
17. **Expecting RSS to speed up a single flow.** One flow, one queue, one core.
18. **Making the receive ring huge "to be safe".** It can push the working set out of the cache DDIO writes into.

---

## 12. Questions you will be asked

**Q1. What is the difference between an interrupt, an exception, a trap, a fault and an abort?** Both interrupts and exceptions make the CPU stop what it is doing and run a handler found through a table. An **interrupt** is asynchronous: it comes from a device, a timer or another CPU and is unrelated to the current instruction. An **exception** is synchronous: the current instruction caused it. Exceptions split by where the saved program counter points. A **fault** saves the address of the *faulting* instruction, so the handler can fix the cause and the instruction runs again (a page fault, after the page is loaded). A **trap** saves the address of the *next* instruction (`int3`, a system call). An **abort** is unrecoverable (a machine check, a double fault). Say which one you mean; "trap" is often used loosely for all of them.

**Q2. What does the hardware do when an interrupt arrives?** It finishes (or squashes) instructions so that state is precise (tutorial 09), then, in one atomic step: saves the old program counter and status, raises the privilege level, masks further interrupts, switches to the kernel stack if needed, and jumps to an address found from the event's number. On **x86-64** it indexes the IDT (16 bytes per vector), pushes SS, RSP, RFLAGS, CS and RIP (plus an error code for some vectors) and clears IF for an interrupt gate; `iretq` pops them. On **AArch64** it writes the return address to `ELR_ELx` and the old state to `SPSR_ELx`, masks the interrupts in PSTATE, puts the cause in `ESR_ELx` (and a fault address in `FAR_ELx`), and jumps to `VBAR_ELx` plus an offset chosen by type and origin; `eret` returns. On **RISC-V** it writes `sepc`, `scause` and `stval`, saves and clears the interrupt-enable bit in `sstatus`, and jumps to `stvec`; `sret` returns. Arm and RISC-V push nothing: the handler saves the registers it needs.

**Q3. Why does a system call cost about 120 ns when a fault delivered to a signal handler costs about 1.5 µs?** `syscall` is built to be cheap: it saves only the return address (`rcx`) and flags (`r11`), loads a fixed entry point from an MSR, and does no table lookup or stack switch in hardware (the kernel stub does that). A fault goes through the full exception path, the kernel's fault handling, building a signal frame on the user stack, the handler, `sigreturn` and a second kernel exit: several kernel entries and exits and several hundred more instructions. Measured here: `syscall` 115 to 126 ns, a fault to a handler and back 1.5 to 1.7 µs, and 10 µs for `#UD` because this VM's hypervisor intercepts it. The ratio, not the absolute numbers, is the point.

**Q4. On a system call, where does the return address live, and what is different about RISC-V?** x86-64 `syscall` puts the address of the *next* instruction in `rcx`; AArch64 `svc` leaves it in `ELR_EL1`, also the next instruction. RISC-V `ecall` is treated as an exception whose `sepc` points *at the `ecall`* itself, so the kernel must add 4 before `sret` or the program would execute `ecall` again forever. The same rule is the difference between a fault (re-execute) and a trap (continue).

**Q5. What are privilege levels, and how does a CPU change them?** They decide which instructions and which memory a piece of code may use. x86 has rings 0 to 3 (Linux uses 0 and 3; VT-x adds root and non-root modes); Arm has exception levels EL0 (applications), EL1 (kernel), EL2 (hypervisor), EL3 (secure monitor); RISC-V has U, S, M (plus an H extension). You go *up* only by taking an exception, an interrupt or a call instruction such as `syscall`, `svc`, `ecall`, through an entry point the kernel chose; you go *down* with `iretq`/`sysret`, `eret` or `sret`. User code cannot jump into the kernel at an address of its choosing, which is what makes the boundary enforceable.

**Q6. Compare the x86 APIC, the Arm GIC and the RISC-V PLIC.** All three turn a wire or message from a device into "CPU *n* should run vector *v*", with priorities, masking and acknowledge/complete steps. x86: an **I/O APIC** routes wired pins to **local APICs**, one per core, which also deliver timers and **IPIs**; MSI-X messages go straight to a local APIC. Arm **GIC**: a *distributor* routes shared peripheral interrupts (SPI, IDs 32 to 1019), per-core *redistributors* handle private ones (PPI 16 to 31, SGI 0 to 15 for IPIs), *LPIs* carry MSIs. RISC-V **PLIC**: per-source priorities, per-hart-and-mode enable bits and thresholds, a claim/complete register pair; the newer AIA adds MSIs. Priority direction differs: on x86 a higher vector class wins, on a GIC a *lower* number wins, on a PLIC a *higher* priority number wins (0 means disabled).

**Q7. Level-triggered, edge-triggered, MSI: what is the difference, and what goes wrong?** A **level** line stays asserted until the device is cleared, so the handler must clear the cause or the interrupt fires again at once (the "nobody cared" storm); sharing a line between devices is possible because the line stays high until all are serviced. An **edge** is a pulse and can be lost if the CPU masks it or two arrive close together. **MSI/MSI-X** is a memory write to an address that the interrupt controller decodes, with a data word that selects the vector: no shared wire, up to 2,048 vectors per device, one per queue, each with its own CPU affinity. A NIC with 16 queues uses 16 MSI-X vectors so that each core handles its own queue.

**Q8. What is a non-maskable interrupt for?** For events that must be handled even when the kernel has interrupts disabled: hardware errors, watchdogs ("hard lockup" detection) and sampling profilers (`perf`). Because it can arrive inside any critical section, an NMI handler must be re-entrant-safe against everything, cannot take ordinary locks, and on x86 a further NMI is blocked until the handler returns with `iretq`.

**Q9. Why do kernels split an interrupt into a top half and a bottom half, and why can a handler not sleep?** The top half runs with interrupts masked (at least that line, usually the core's) on the interrupted task's stack; it must acknowledge the device, grab what is essential, and return quickly, or latency for everything else suffers. It cannot sleep because it has no process context to put to sleep: the interrupted task is innocent, and sleeping with interrupts masked would hang the core. The rest runs later as a **softirq**, tasklet, **threaded handler** or workqueue item, with interrupts enabled; threaded and workqueue code can sleep. Data shared with a handler must be protected with `spin_lock_irqsave`, or the handler can interrupt the lock holder on the same core and deadlock.

**Q10. What is interrupt moderation, what is NAPI, and what is receive livelock?** Moderation makes the NIC raise one interrupt per `F` frames or per `T` microseconds, whichever first: at 1 Mpps with `T` = 20 µs that is 50,000 interrupts a second instead of a million, at the price of up to `T` added latency at low load. **NAPI** switches the receive interrupt *off* when it fires, polls the ring in a softirq with a budget (300 packets or 8 ms here), and re-enables the interrupt only when the ring is empty: interrupts at low load (low latency), polling at high load (efficiency). **Livelock** (Mogul and Ramakrishnan 1996) is what happens without that: with per-packet interrupts at higher priority than the code that processes packets, a rate beyond about `1 ÷ (a + b)` spends all time on interrupts and delivers *less* and eventually *nothing*, since packets are accepted and then dropped at the end of the pipeline. The cure is to drop early and to poll when busy.

**Q11. When is polling better than interrupts?** When events are frequent enough that the per-event cost of an interrupt (hardware entry, handler, a PCIe register read, cache pollution, sometimes a wake-up from a deep C-state or a VM exit) exceeds the cost of checking: the break-even is about `1 ÷ c` events a second, 500,000 a second for c = 2 µs. Polling also gives lower and steadier latency (a spin hand-off between cores measured 56 to 60 ns against 10 µs for a sleeping wake-up in this VM). It loses when events are rare (a core burns power looking at nothing), when cores are scarce, or when latency does not matter. Hybrids win in practice: NAPI, adaptive moderation, busy-poll with a timeout, `io_uring` with `SQPOLL`, NVMe polling for fast devices only.

**Q12. How does RSS work, and why can one TCP flow not use a 100 GbE link?** The NIC hashes each packet's flow tuple (source and destination address and port) with a Toeplitz hash, takes the low bits as an index into an indirection table of queue numbers, and DMAs the packet into that queue's ring, interrupting that queue's core. All packets of a flow have the same hash, so they stay in order on one core with its state in cache. By the same rule **one flow is one queue is one core**: an `iperf` with a single stream tops out at what one core can process however many queues exist. Fixes: more flows, a transport that opens several, or hardware that spreads within a flow at the cost of reordering. The default key is asymmetric (forward and reverse hashes differ), so firewalls that need both directions on one core use a symmetric key (`6d5a` repeated).

**Q13. Walk through how a device and a driver share a descriptor ring, and where the barriers go.** The driver fills descriptors (buffer address, length, flags) in consecutive slots, executes a **write barrier** (`dma_wmb`, then `wmb` or a plain `writel` that includes one before the MMIO write), and writes the new **tail** to the device's **doorbell** register (batching several descriptors per doorbell). The device fetches the descriptors by DMA, moves the data, then writes status back into the descriptor (the **DD** bit), *after* the data. The driver polls or takes an interrupt, reads the status bit, executes a **read barrier** (`dma_rmb`), then reads the other fields. Without them, an Arm or RISC-V core (or a compiler on any machine) may reorder the stores so that the device sees the doorbell before the descriptor, or the driver sees the flag before the length: a bug that appears once in a million packets and never on an x86 laptop, where the barriers cost nothing.

**Q14. Does DMA see what the CPU wrote? Compare x86 and Arm.** On x86 the DMA path is cache-coherent: a device's reads and writes snoop the CPU caches, so a driver needs only ordering. On many Arm and RISC-V SoCs it is **not**: the CPU may hold dirty lines the device does not see (so the driver must *clean* before the device reads), and the device writes memory behind stale cached copies (so it must *invalidate* before the CPU reads). Linux hides this behind the **DMA API**: `dma_map_single(..., direction)` and `dma_sync_*_for_cpu/_device` do the maintenance, `dma_alloc_coherent` gives uncached or coherent memory, and buffers must not share cache lines with other data (`ARCH_DMA_MINALIGN` is 128 on arm64). A driver that "works on x86" and fails on Arm usually skipped the API.

**Q15. What are DDIO and "leaky DMA"?** Data Direct I/O lets a PCIe device write straight into the last-level cache instead of DRAM, so the CPU's first read of a packet is an LLC hit rather than a ~150 ns miss. By default it uses only **2 ways** of the LLC. If the receive ring is large and packets sit unprocessed, new DMA writes evict earlier packets that have not been read yet, which then go to DRAM and are fetched back: "leaky DMA". Farshin et al. (2020) saw the hit rate fall to about 40 % with a 4,096-entry ring of 1,500-byte packets. So a bigger ring is not safer: size it for the burst, process promptly, and give DDIO more ways where possible.

**Q16. How do you read the time cheaply, and how do you compare timestamps safely?** On x86 the **TSC** is a per-core counter that on modern CPUs ticks at a constant rate regardless of frequency (`constant_tsc`, `nonstop_tsc`); `rdtsc` takes about 16 ns here, and the **vDSO** `clock_gettime` (about 30 ns) reads it plus a kernel-maintained scale in user space with no kernel entry, while a forced system call costs 175 to 205 ns. Arm has a **generic timer**: a system counter (architecturally 1 GHz since Armv8.6, earlier 1 to 50 MHz) readable at EL0, plus per-core compare registers; RISC-V has `mtime`/`mtimecmp` (and `Sstc` so S mode needs no SBI call). For intervals use `CLOCK_MONOTONIC` and compare by subtraction in unsigned arithmetic: `(now - start) >= timeout` stays correct across wrap, while `now >= start + timeout` fails when `start + timeout` wraps. A 32-bit counter at 1 GHz wraps in 4.29 s; a 64-bit one at 2.1 GHz in 278 years.

**Q17. Why are some things slower in a VM, and how fast can a microcontroller respond?** Operations that the hypervisor must intercept cause a **VM exit**: `cpuid` (7.5 µs here), programming a timer, sending an IPI, sometimes the end-of-interrupt, `#UD`. Hardware assists (posted interrupts, APIC virtualization, paravirtual interfaces such as `kvm-clock` and virtio) remove many exits ([tutorial 27](27_hardware_virtualization.md)). At the other extreme a **Cortex-M3/M4** takes an interrupt in **12 cycles** (hardware stacks eight registers; **tail-chaining** to a pending interrupt takes about 6), because the design is deterministic: no cache, no virtual memory, vector table in fast memory. That is why hard real-time work stays on microcontrollers and tuned cores.

---

## 13. Problems

**How to use this section.** Three are paper problems, one uses a real machine; each says *Where* to check the answer and *How*. The checks need a calculator or a few lines of Python. Do the paper part first, then check, and fix the paper answer if they disagree. Time: about 20 to 30 minutes each.

### Problem A (paper, then a script): the packet budget and interrupt moderation

Use the formulas of sections 4.3 and 4.4: wire bytes per frame = frame + 20; `batch = max(1, min(F, pps × T))`; `interrupts per second = pps ÷ batch`; `oldest packet waits min((F − 1) ÷ pps, T)`. Assume c = 2 µs of CPU per interrupt and a 3 GHz core.

(a) For 25 GbE give the packet rate, the time per packet and the cycles per packet at 3 GHz for 64-byte and 1,518-byte frames.
(b) With one interrupt per packet, what share of a core is spent at 100 kpps and at 1 Mpps? At what rate is one core saturated? How many cores for 64-byte frames at 25 GbE? What is the break-even polling rate if c were 0.5 µs, or 5 µs?
(c) Choose `F` and `T` for a service that receives 1 Mpps, must stay at or below 50,000 interrupts a second, and may add at most 20 µs of moderation delay.
(d) With `F` = 32 and `T` = 20 µs, give interrupts per second, CPU share and delay at 10 kpps, at 100 kpps and at the 64-byte 25 GbE rate. What does the last result tell you about moderation alone?

*Where:* paper; check with a calculator or twenty lines of Python.
*How:* (a) `rate = bits per second ÷ ((frame + 20) × 8)`; cycles = 3e9 ÷ rate. (b) `CPU share = pps × c`; break-even `1 ÷ c`. (c) You need batch ≥ `1e6 ÷ 50,000`, and the timer bounds the batch at `pps × T`. (d) apply the formulas.

### Problem B (paper): an eight-slot descriptor ring and a toy hash

A transmit ring has 8 slots (0 to 7) and the usual convention: `head` is where the NIC reads next, `tail` is where the driver writes next, both start at 0, the ring is *full* when `(tail + 1) mod 8 = head` (so one slot is always unused), and the driver writes `tail` to the doorbell.

(a) Fill in `head`, `tail`, packets in flight and free slots after each event: (1) start; (2) the driver queues 5 packets with one doorbell write; (3) the NIC finishes 3 of them; (4) the driver wants to queue 6 more; (5) the NIC finishes everything outstanding. How many doorbell writes happened, and how many would a doorbell per packet need?
(b) For event (2) list the writes the driver makes, in program order, and mark where a `dma_wmb()`/`wmb()` is needed and why. Then list the reads on the completion side and mark where a `dma_rmb()` is needed.
(c) Instead of a write-back flag, a driver learns about completions by reading the NIC's `head` register after each packet. Why is that slow? What do the descriptors' write-back replace it with, and what does that read cost?
(d) Toeplitz on a toy scale: key `0b11010110`, input `0b1011`, 4-bit windows. Compute the hash. The indirection table has 4 entries, `[2, 0, 3, 1]`, indexed by the low 2 bits of the hash: which queue?

*Where:* paper; (a) and (d) can be checked in a few lines of Python.
*How:* (a) `in flight = (tail − head) mod 8`, `free = 7 − in flight`. (b) section 7.4. (c) tutorial 25 section 3.6 and section 5.2 above. (d) for every 1 bit of the input at position `i` (counting from the left, from 0), XOR in key bits `i` to `i + 3`.

### Problem C (paper): decode the event

(a) x86 page-fault error codes. Decode `0x4`, `0x6`, `0x7` and `0x15` using bits P (0), W (1), U (2), R (3), I (4): what was the access, from which mode, and what did the page look like?
(b) The IDT starts at address `0x1000` and entries are 16 bytes. Where are the entries for vector 14 (`#PF`), vector `0x21` and vector `0x80`? How big is a full IDT? How many bytes does the CPU push on entry for a page fault from user mode, and for `int 0x80` from user mode?
(c) AArch64 with `VBAR_EL1 = 0xFFFF000008010000`. At which address does execution start for: an `svc #0` from a user program; a timer interrupt while user code runs; a data abort in kernel code; an interrupt in kernel code (using `SP_EL1`)? What exception class is `ESR_EL1 = 0x56000000`? Decode `0x96000045`: class, instruction length and fault status (this value is made up for practice).
(d) RISC-V. `scause = 0x8000000000000005`, `scause = 13`, `scause = 8`: what is each? With `stvec = 0x80200001`, where does the CPU jump for a supervisor timer interrupt (cause 5) and a supervisor external interrupt (cause 9)? Where for a page fault? An `ecall` is at `0x80001000` and a faulting load at `0x80002000`: what does the kernel put in `sepc` before `sret` in each case?

*Where:* paper; the real behaviour is in the `traps` results of section 1.3.
*How:* (a) error code layout of section 1.2. (b) base + 16 × vector; the frame is five words, plus one for an error code. (c) section 2.4: sync 0x200/0x400, IRQ 0x280/0x480; `EC = ESR >> 26`, `IL` is bit 25, `DFSC` the low 6 bits. (d) section 2.5: interrupt flag is the top bit; vectored mode adds `4 × cause` for interrupts only.

### Problem D (on your machine): read the interrupts and time a call

Use any Linux machine or VM (root needed for part (c)). No NIC offload or `ethtool` is required for (a), (b), (d) and (e).

(a) `grep -E 'LOC|CAL|RES|TLB' /proc/interrupts`, then pin a busy loop to CPU 0 for 4 seconds (`taskset -c 0 timeout 4 sh -c 'while :; do :; done'`) and read it again. How many local timer interrupts per second did CPU 0 get, and what does that say about the kernel's `HZ`? What does `getconf CLK_TCK` print, and why is it not the same thing?
(b) Which devices have vectors? `grep -E 'virtio|nvme|eth|ens|mlx|iwl|xhci' /proc/interrupts`. How many vectors does each use, which CPUs are taking them, and what do `/proc/irq/<n>/smp_affinity_list` and `effective_affinity_list` show?
(c) Pick a busy network interrupt and move it: `echo 1 > /proc/irq/<n>/smp_affinity_list`, generate traffic (`ping -f` to a neighbour, or a download), and watch the counters in `/proc/interrupts`. Move it back. Which CPU's count grew each time?
(d) Time a call. Save the tiny program of the Answers section as `timeit.c`, build with `gcc -O2`, and run it pinned: `taskset -c 0 ./timeit`. How many nanoseconds is a vDSO `clock_gettime`, a `getppid` system call, and the forced `clock_gettime` system call? Compute cycles from your CPU frequency.
(e) Read the receive path's settings: `cat /proc/sys/net/core/{netdev_budget,netdev_budget_usecs,dev_weight,netdev_max_backlog}`, `cat /proc/net/softnet_stat`, `ls /sys/class/net/*/queues`, and if installed `ethtool -l/-g/-c/-k <dev>`. How many queues, ring entries and what coalescing does your NIC use? Is any `time_squeeze` or `dropped` count non-zero?

*Where:* your own machine.
*How:* `softnet_stat` has one line per CPU in hexadecimal; the first three columns are processed, dropped and `time_squeeze`. Compare with the VM's answers in the Answers section. A hypervisor or container may hide some files.

---

## Answers

### Problem A

(a) Wire bytes are frame + 20, so at 25 Gb/s:

| Frame | Packets per second | Time per packet | Cycles at 3 GHz |
|---|---|---|---|
| 64 B | 37.20 M | 26.9 ns | 80.6 |
| 1,518 B | 2.03 M | 492 ns | 1,476 |

(b) `share = pps × c`: **20 %** of a core at 100 kpps, **200 %** (two cores) at 1 Mpps. One core is saturated at `1 ÷ 2 µs` = **500 kpps**. At 37.2 Mpps of 64-byte frames: `37.2 M × 2 µs` = **74 cores**: per-packet interrupts are impossible at this rate. The break-even polling rate is `1 ÷ c`: **2 M per second** for c = 0.5 µs, **200 k per second** for c = 5 µs. (Cheaper interrupts raise the rate at which interrupts are still the better choice.)

(c) You need a batch of at least `1,000,000 ÷ 50,000 = 20`. The timer limits the batch to `pps × T`, so `T` ≥ 20 µs; the delay limit says `T` ≤ 20 µs; so **`T` = 20 µs**, and `F` must allow a batch of 20: **`F` ≥ 20**. Any of `F` = 20, 21 or 32 works (with `F` = 20 the oldest packet waits `19 ÷ 1e6` = 19 µs). Smaller `F` (16) fails: 62,500 interrupts a second. A longer `T` (25 µs) breaks the delay budget even though it lowers the rate to 40,000.

(d) `F` = 32, `T` = 20 µs:

| Rate | Batch | Interrupts/s | CPU at c = 2 µs | Delay |
|---|---|---|---|---|
| 10 kpps | 1 | 10,000 | 2 % | 20 µs (the timer) |
| 100 kpps | 2 | 50,000 | 10 % | 20 µs |
| 1 Mpps | 20 | 50,000 | 10 % | 20 µs |
| 37.2 Mpps | 32 | 1,162,574 | 232 % | 0.8 µs |

At 25 GbE with small packets moderation alone still needs more than two cores for interrupts, before a single packet is processed, so such a system must **poll** (NAPI, or a DPDK-style loop) and spread over queues. Note also that moderation costs a flat 20 µs of latency even at 10 kpps, where it saves almost nothing; adaptive moderation exists for that reason.

```python
def rate(g, f): return g * 1e9 / ((f + 20) * 8)
def mod(pps, F, T): b = max(1, min(F, pps * T)); return b, pps / b, min((F - 1) / pps, T)
print(rate(25, 64), rate(25, 1518))
for pps in (1e4, 1e5, 1e6, rate(25, 64)): print(pps, mod(pps, 32, 20e-6))
```

### Problem B

(a)

| Event | head | tail | in flight | free |
|---|---|---|---|---|
| (1) start | 0 | 0 | 0 | 7 |
| (2) driver queues 5, one doorbell write of `tail = 5` | 0 | 5 | 5 | 2 |
| (3) NIC finishes 3 | 3 | 5 | 2 | 5 |
| (4) driver wants 6, only **5** fit; it posts them with one doorbell write (`tail = 2`), the sixth waits and the transmit queue is stopped | 3 | 2 | 7 | 0 |
| (5) NIC finishes all 7 | 2 | 2 | 0 | 7 |

Two doorbell writes against ten for one per packet. Each doorbell is an uncached MMIO write that the CPU must issue in order, so batching matters at high rates ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 3.7). The ring is *full* at event (4) with 7 of 8 slots used: the unused slot is what makes `head = tail` mean empty, not full. The driver stops the transmit queue and restarts it from the completion path when slots free up.

(b) In program order for event (2): write the fields of descriptors 0 to 4 (buffer address, length, flags); **barrier** (`dma_wmb()` for the descriptor stores against each other if a per-descriptor ownership bit is used, and a `wmb()` or the barrier inside `writel` between the last descriptor store and the doorbell); write `tail = 5` to the doorbell register. The barrier is needed because on Arm and RISC-V the doorbell store could become visible to the device before the descriptor stores, and the NIC would fetch slot 4 half-written or stale. On the completion side: read the write-back status of slot 0 (the DD bit); **`dma_rmb()`**; only then read length and checksum status. Without it the CPU may satisfy the later loads early with stale data.

(c) Reading the NIC's `head` register is a PCIe read: a round trip to the device that stalls the core, about a microsecond or less on bare metal and tens of microseconds in this VM ([tutorial 25](25_buses_pcie_and_io_mapping.md)); five packets would pay it five times. With write-back the NIC writes the completion status into the descriptor in memory, and the driver reads it with an ordinary load: a cache hit if DDIO or the coherence protocol put it there, else a DRAM access of 146 to 149 ns (measured in this VM).

(d) Input bits are `1 0 1 1`; set bits at positions 0, 2 and 3. The key is `1 1 0 1 0 1 1 0`. Windows: position 0 → bits 0 to 3 = `1101`; position 2 → bits 2 to 5 = `0101`; position 3 → bits 3 to 6 = `1011`. `1101 ^ 0101 = 1000`; `1000 ^ 1011 =` **`0011`**, which is 3. The low 2 bits are `11` = index 3, and the table entry is **queue 1**.

### Problem C

(a) Bits: P = present (the page was mapped), W = write, U = user mode, R = reserved bit set, I = instruction fetch.
- `0x4` (U): a user-mode **read** of a page that is **not present**. This is the NULL-pointer dereference in the `traps` run (error code 4).
- `0x6` (W, U): a user-mode **write** to a page that is not present (the write to the never-touched read-only page gave 6 in the `traps` run).
- `0x7` (P, W, U): a user-mode write to a page that **is present** but not writable: a protection violation, such as the write after a read in the `traps` run, or a write into a copy-on-write page (which the kernel then resolves instead of killing the process).
- `0x15` (P, U, I): a user-mode **instruction fetch** from a present page: the page is not executable, so this is an NX violation, for example a jump into the stack or data.
The faulting address is in `CR2`. These are all vector 14; Arm reports the same cases as data and instruction aborts with a fault status code, RISC-V as scause 12, 13 and 15.

(b) Entry address = `0x1000 + 16 × vector`: vector 14 at **`0x10E0`**, vector `0x21` at **`0x1210`**, vector `0x80` at **`0x1800`**. A full IDT is `256 × 16` = **4,096 bytes**. On entry from user mode the CPU pushes SS, RSP, RFLAGS, CS and RIP: 5 × 8 = **40 bytes** for `int 0x80`; for a page fault it also pushes the error code, **48 bytes**.

(c) `svc #0` from user space: lower EL, AArch64, synchronous: offset 0x400 → **`0xFFFF000008010400`**. Timer interrupt while in user code: lower EL IRQ, 0x480 → **`0xFFFF000008010480`**. Data abort in kernel code: current EL with `SP_ELx`, synchronous: 0x200 → **`0xFFFF000008010200`**. Interrupt in kernel code: 0x280 → **`0xFFFF000008010280`**. `0x56000000 >> 26` = `0x15` = **SVC from AArch64**. For `0x96000045`: `>> 26` = `0x25` = **data abort without a change of exception level** (the fault came from the kernel); bit 25 (IL) = 1, a 32-bit instruction; the low six bits are `0x05` = **translation fault, level 1** (the page-table entry at level 1 was invalid).

(d) `0x8000000000000005`: the top bit marks an **interrupt**, cause 5 = **supervisor timer interrupt**. `13` = **load page fault** (an exception, since the top bit is clear). `8` = **`ecall` from U-mode**. With `stvec = 0x80200001` the low two bits are `01`, so **vectored mode** and the base is `0x80200000`. Interrupts jump to base + 4 × cause: cause 5 at **`0x80200014`**, cause 9 at **`0x80200024`**. All exceptions, including the page fault, go to the base **`0x80200000`**. For the `ecall` at `0x80001000`, `sepc` holds `0x80001000` and the kernel must write **`0x80001004`** before `sret`. For the faulting load at `0x80002000`, after it fixes the mapping it leaves `sepc` alone, so `sret` **re-executes** `0x80002000`.

### Problem D

The numbers below are from the Firecracker micro-VM (2 vCPUs, 2.1 GHz Xeon, Linux 6.18). Yours will differ; the shapes should match.

(a) CPU 0's `LOC` grew by **1,004** in 4 s: **251 per second**, so `HZ = 250`. `getconf CLK_TCK` prints **100**: it is `USER_HZ`, the unit in which the kernel reports times to user space (`/proc/stat`, `times()`), a fixed ABI constant, not the kernel's tick rate. A mostly idle CPU on a `NO_HZ_IDLE` kernel takes fewer ticks; a busy one takes the full `HZ`. Also look at `CAL` (function-call IPIs), `RES` (reschedule IPIs) and `TLB` (shootdowns): all three are inter-processor interrupts, and `CAL` is busy in this VM.

(b) Each virtio device uses a *config* vector plus one vector per queue: the block devices two each (`virtio1-config`, `virtio1-req.0`), the network device three (`virtio7-config`, `virtio7-input.0`, `virtio7-output.0`), the balloon device five. Each vector is delivered to a single CPU (a vector's `effective_affinity_list` shows one CPU even when `smp_affinity_list` allows `0-1`). The network input vector was by far the busiest.

(c) In this VM the virtio-net receive vector (IRQ 49) moved as follows: CPU 0 had 10,554 and CPU 1 had 0; after allowing only CPU 1, CPU 1's count rose 21, 42, 62 while CPU 0 stayed at 10,555; after allowing only CPU 0 again, CPU 0 rose to 10,574 and 10,595 while CPU 1 stayed at 63. The block-device vector refused the write with `EPERM`; I did not look into why (kernel-managed interrupt affinity is a likely reason, but I did not check).

(d) The program (compile with `gcc -O2 timeit.c -o timeit`):

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

int main(void) {
    enum { N = 5000000 };
    struct timespec ts;
    volatile long sink = 0;
    double t0 = now_ns();
    for (int i = 0; i < N; i++) sink += i;                           // baseline: the loop alone
    double t1 = now_ns();
    for (int i = 0; i < N; i++) clock_gettime(CLOCK_MONOTONIC, &ts); // vDSO: no kernel entry
    double t2 = now_ns();
    for (int i = 0; i < N; i++) syscall(SYS_getppid);                // a real kernel entry and exit
    double t3 = now_ns();
    for (int i = 0; i < N; i++) syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts); // same clock, forced through the kernel
    double t4 = now_ns();
    printf("loop %.1f ns | vDSO clock_gettime %.1f ns | syscall getppid %.1f ns | syscall clock_gettime %.1f ns\n",
           (t1 - t0) / N, (t2 - t1) / N, (t3 - t2) / N, (t4 - t3) / N);
    return 0;
}
```

Three runs here gave a loop of 0.5 to 2.2 ns, the vDSO call **29 to 34 ns** (about 65 ticks), `getppid` **127 to 136 ns** (about 270 to 285 ticks) and the forced `clock_gettime` **171 to 178 ns** (about 360 to 375 ticks; the tick column of section 2.7 is time × 2.1 GHz, and core cycles are about 1.4 times that). The vDSO is about five times cheaper than the same service through the kernel, and the entry-and-exit alone (`getppid`) is roughly 130 ns, 270 cycles.

(e) Here: `netdev_budget` 300, `netdev_budget_usecs` 8,000 (µs), `dev_weight` 64; `softnet_stat` for CPU 0 showed 5,710 processed packets and zero for both `dropped` and `time_squeeze`; `/sys/class/net/eth0/queues` lists one receive and one transmit queue (the device did not offer multiqueue), so RSS has nothing to spread; and `ethtool` is not installed. On a physical NIC expect several queues (often one per core up to 16 or 64), ring sizes of 256 to 4,096 entries, and non-zero `rx-usecs` coalescing with an adaptive mode.

---

## Cheat sheet

- **Events:** interrupts are asynchronous (devices, timers, IPIs); exceptions are synchronous. Fault = saved PC at the instruction (re-executed after the fix); trap = after it; abort = no recovery. `#PF` = vector 14, error code bits P, W, U, R, I, address in `CR2`.
- **x86-64 entry:** IDT, 16 bytes per vector (gate type 0xE interrupt, 0xF trap); pushes SS, RSP, RFLAGS, CS, RIP (+ error code for vectors 8, 10 to 14, 17, 21, 29, 30), 40 or 48 bytes; `iretq` returns; FRED (Linux 6.9) replaces this on newer CPUs. Higher vector = higher priority.
- **Arm entry:** return address in `ELR_ELx`, state in `SPSR_ELx`, cause in `ESR_ELx` (`EC = ESR >> 26`: 0x15 svc, 0x20/0x21 instruction abort, 0x24/0x25 data abort), address in `FAR_ELx`; vector table `VBAR + 0x000/0x200/0x400/0x600` by origin and `+0x000/0x080/0x100/0x180` by type (sync, IRQ, FIQ, SError); user sync at 0x400, user IRQ at 0x480. Lower GIC priority number wins.
- **RISC-V entry:** `sepc`, `scause` (top bit = interrupt; 2 illegal, 8/9/11 ecall U/S/M, 12/13/15 page faults, 5 timer, 9 external), `stval`, `stvec` (vectored: base + 4 × cause for interrupts); `sepc` of an `ecall` is the `ecall`: add 4.
- **System calls:** `syscall` (rcx = next instruction, r11 = flags), `svc`, `ecall`. Measured: `syscall` 115 to 136 ns (about 270 TSC ticks, 350 core cycles), vDSO `clock_gettime` 29 to 34 ns, `rdtsc` 16 ns, fault to a signal handler and back 1.5 to 1.7 µs, `cpuid` (VM exit) 7 µs, `#UD` 10 µs here.
- **Privilege:** rings 0 to 3 (+ VMX root); EL0 to EL3; U/S/M. Up only through an exception or call gate, down with `iretq`/`sysret`, `eret`, `sret`.
- **Controllers:** x86 I/O APIC + local APIC (x2APIC), MSI-X as memory writes; Arm GIC (SGI 0 to 15, PPI 16 to 31, SPI 32 to 1019, LPI 8192+); RISC-V PLIC/CLINT/AIA. Level needs clearing at the device; edge can be lost; MSI-X gives a vector per queue. Affinity in `/proc/irq/N/smp_affinity_list`.
- **Handling:** top half short, no sleeping; softirq/threaded/workqueue for the rest; `spin_lock_irqsave` for data shared with a handler. Interrupt cost `c` (entry, handler, MMIO read, cache, wake-up) is about 1 to 5 µs; CPU share = `rate × c`.
- **Rates:** packets per second = rate ÷ ((frame + 20) × 8): 14.88 M at 10G/64 B (201 cycles at 3 GHz), 148.8 M at 100G (20 cycles), 8.13 M at 100G/1518 B (369 cycles).
- **Moderation and NAPI:** `batch = max(1, min(F, pps × T))`, interrupts = pps ÷ batch, wait = min((F − 1) ÷ pps, T). NAPI: mask on interrupt, poll with budget (300 / 8 ms / weight 64), unmask when empty. Livelock: delivered rate peaks at `1 ÷ (a + b)` and falls to zero at `1 ÷ a`; drop early.
- **Polling:** wins above about `1 ÷ c` events a second (500 k/s for 2 µs) and for latency; costs power and a core. Measured: spin hand-off 56 to 60 ns; sleeping cross-CPU hand-off 10 µs; same-CPU futex 1.7 µs.
- **Timers:** tick (`HZ` 250 here), tickless idle, TSC (`constant_tsc`, `nonstop_tsc`), vDSO, Arm generic timer (1 GHz architecturally from Armv8.6), RISC-V `mtime`/`mtimecmp`, SysTick/PendSV on Cortex-M. Compare with `(now − start) >= timeout` in unsigned arithmetic. 32-bit at 1 GHz wraps in 4.29 s; 64-bit at 2.1 GHz in 278 years.
- **DMA:** device moves data itself; descriptors (address, length, flags), scatter-gather, rings with producer/consumer indices, a doorbell and a write-back flag. Order: fields, `dma_wmb`, flag or doorbell; read flag, `dma_rmb`, fields. Coherent on x86; clean and invalidate (DMA API) on non-coherent Arm; never give a device a virtual address.
- **NIC path:** frame → hash → descriptor fetch → DMA write (DDIO into 2 LLC ways) → write-back → MSI-X → NAPI poll → refill + tail doorbell → stack → socket → one copy. Offloads: checksum, TSO/GSO (64 KiB ≈ 45 frames), GRO, VLAN, timestamps.
- **RSS:** Toeplitz hash of the 4-tuple, low bits index a 128-entry table; one flow = one queue; default key asymmetric, repeating `6d5a` makes it symmetric; test vector 66.9.149.187:2794 → 161.142.100.80:1766 = `0x51ccc178`.
- **Rings elsewhere:** NVMe 64 B SQE / 16 B CQE, doorbells, phase tag, a queue pair per core; virtio descriptor 16 B (addr, len, flags, next), available and used rings, `EVENT_IDX`.
- **Drops:** NIC ring (`rx_missed_errors`) → softirq budget (`time_squeeze`) → backlog (`dropped`) → socket buffer; the later it dies, the more CPU it wasted.
- **Everything here came from real output of this VM's kernel, short programs timed in this VM (numbers that include slow hypervisor exits, so read them as ratios), published papers and specifications, and scripts for the arithmetic and the hash (checked against Microsoft's five IPv4 test vectors); nothing was timed on a physical NIC, SSD or interrupt controller, and the Arm and RISC-V sketches were assembled but never run.**

**Next:** tutorial 27, Hardware support for virtualization (why x86 needed help to virtualise, VT-x and AMD-V, the VMCS and VM exits and their cost, Arm EL2, nested paging and the two-dimensional page walk, interrupt virtualization, device assignment with the IOMMU and SR-IOV, virtio and vhost, containers against VMs, KVM and Firecracker). Say "next" when you are ready.
