# CO 27: Hardware Support for Virtualization

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 3.5 hours (2.5 reading, 1 problems) · You need: [tutorial 15](15_virtual_memory_and_the_tlb.md) (section 6: page walks), [tutorial 25](25_buses_pcie_and_io_mapping.md) (sections 6 and 7: the IOMMU, SR-IOV and passthrough), [tutorial 26](26_interrupts_exceptions_and_dma.md) (sections 1 to 3 and 5: events, interrupt controllers, polling)*

By the end of this tutorial you can:

- state what a hypervisor must guarantee (equivalence, resource control, efficiency), say why classic trap-and-emulate failed on x86, and name the three answers (binary translation, paravirtualization, hardware assist);
- describe Intel VT-x and AMD-V: root and non-root operation, the VMCS, VM entry and exit, which events exit and which do not, and what one exit costs in cycles (published and measured);
- say what Arm EL2 with stage-2 translation and the RISC-V H extension add, in the same terms;
- do the arithmetic of a nested page walk, `(n + 1)(m + 1) - 1`, and explain what tagged TLBs, walk caches and huge pages in *both* dimensions do about it;
- explain how interrupts, timers and idle are virtualised (virtual APIC and posted interrupts, GIC list registers, `kvm-clock`, paravirtual EOI and IPIs, steal time) and read the evidence in a real guest;
- choose between emulated devices, virtio, vhost, passthrough and SR-IOV for a scenario, and compare containers, VMs and microVMs from the hardware's point of view;
- outline what changes with nested virtualization and with confidential VMs;
- find out, from inside a machine, whether it is a guest and what its hypervisor offers.

Almost every server you will deploy to is a guest. The cloud instance, the CI runner, the sandbox this tutorial was written in: all run on a hypervisor, and so does a growing share of embedded and automotive software. Virtualization is also where several earlier tutorials meet: the privileged instructions of [tutorial 26](26_interrupts_exceptions_and_dma.md), the page walks of [tutorial 15](15_virtual_memory_and_the_tlb.md), the IOMMU of [tutorial 25](25_buses_pcie_and_io_mapping.md). For an interview the useful skill is not the name of every VMCS field but a clear answer to *what costs what*: why a guest runs at nearly native speed until it does one particular thing, and why that thing is so much slower than it looks.

**Where the numbers come from.** The virtual machine used for tutorials 14 to 26 (a Firecracker-style micro-VM on a 2.1 GHz Intel Xeon with two virtual CPUs, Linux 6.18) is a *guest*, which makes it unusually good evidence for this topic and unusually poor for one half of it. Four kinds of evidence are used, and the text says which is which. (1) **Real guest output**: `dmesg` lines about the hypervisor, the CPUID leaves the hypervisor exposes, the virtio devices in `/sys`, the steal counter in `/proc/stat`. (2) **Measurements in this guest** from short C programs pinned to one CPU: which unprivileged instructions exit to the hypervisor, what the five instructions that broke classic virtualization do today, and what touching fresh memory costs. (3) **Arithmetic** checked by script: nested page-walk reference counts and exit budgets. (4) **Published work**, linked where used: Robin and Irvine, ["Analysis of the Intel Pentium's Ability to Support a Secure Virtual Machine Monitor"](https://static.usenix.org/events/sec00/full_papers/robin/robin_html/index.html) (USENIX Security 2000); Adams and Agesen, ["A Comparison of Software and Hardware Techniques for x86 Virtualization"](https://pdos.csail.mit.edu/6.1810/2019/readings/adams06vmware.pdf) (ASPLOS 2006); Ben-Yehuda et al., ["The Turtles Project: Design and Implementation of Nested Virtualization"](https://www.usenix.org/legacy/events/osdi10/tech/full_papers/Ben-Yehuda.pdf) (OSDI 2010); Gandhi et al., ["Efficient Memory Virtualization"](https://scail.cs.wisc.edu/papers/micro14-virtualization.pdf) (MICRO 2014); Dall et al., ["Optimizing the Design and Implementation of the Linux ARM Hypervisor"](https://www.usenix.org/system/files/conference/atc17/atc17-dall.pdf) (USENIX ATC 2017); Agache et al., ["Firecracker: Lightweight Virtualization for Serverless Applications"](https://css.csail.mit.edu/6.566/2023/readings/firecracker.pdf) (NSDI 2020); and the Linux kernel's documentation for [KVM CPUID bits](https://docs.kernel.org/virt/kvm/x86/cpuid.html) and [TDX](https://docs.kernel.org/arch/x86/tdx.html). **I could not run a hypervisor here**: the guest has no `/dev/kvm` and no `vmx` flag, so every number about the *host* side of an exit (the cost of a VM exit in cycles, nested walks, vhost) is a published figure, not mine, and I say so each time. The hypervisor under this guest is not visible to me beyond what it chooses to expose.

> **The one-sentence model.** A hypervisor lets a guest believe it owns the machine by running guest code *directly* on the CPU at full speed and taking control back, in a **VM exit**, only for the few events that would reveal or damage the real machine; hardware support made "few" small and the exit cheaper, and every remaining cost of a virtual machine is either an exit or an extra level of address translation.

---

## 1. What virtualization has to do

### 1.1 The job, and the vocabulary

In 1974 Popek and Goldberg ("Formal Requirements for Virtualizable Third Generation Architectures") set out what a virtual machine monitor must provide, and the three properties are still the right checklist:

- **Equivalence (fidelity).** A program in the VM behaves as it would on the real machine, apart from timing and the resources available.
- **Resource control (safety).** The monitor stays in control of all resources: a guest cannot touch memory, devices or CPU time that it was not given.
- **Efficiency.** A statistically dominant share of the guest's instructions run directly on the hardware, with no monitor involved.

The third property is why this tutorial is about *hardware*. Emulating every instruction in software (a simulator, QEMU without KVM) gives the first two and fails the third by a large factor.

Words you need, used consistently below:

| Term | Meaning |
|---|---|
| **Hypervisor / VMM** | the software that creates VMs and gives them resources: KVM with a user-space part such as QEMU or Firecracker, Xen, VMware ESXi, Microsoft Hyper-V |
| **Host** | the real machine and whatever runs directly on it (the hypervisor, or a Linux kernel that contains one) |
| **Guest** | an operating system and its applications running inside a VM |
| **vCPU** | a virtual CPU; the hypervisor schedules it onto a physical core. In KVM each vCPU is an ordinary host thread |
| **Guest-virtual, guest-physical, host-physical** | the three address kinds: what a guest application uses, what the guest kernel believes is RAM, and the real DRAM address (section 5) |
| **VM exit / VM entry** | the guest stops and the hypervisor runs / the hypervisor resumes the guest |
| **Hypercall** | a deliberate call from the guest to the hypervisor, like a system call one level down (`vmcall` on Intel, `vmmcall` on AMD, `hvc` on Arm) |

A hypervisor that runs directly on the hardware (Xen, ESXi) is called **Type 1**; one that runs as a program inside a general-purpose operating system (VirtualBox, VMware Workstation) is **Type 2**. KVM turns the Linux kernel itself into a Type 1 hypervisor by adding a module, which is why the distinction blurs. For hardware questions it does not matter: the CPU only knows "guest" and "not guest".

### 1.2 What has to be virtualised

A machine is more than a CPU. Each part has its own danger if handed to a guest unchanged, and its own mechanism:

| Resource | What the guest expects | Danger if given directly | Mechanism | Where |
|---|---|---|---|---|
| **CPU privilege** | to run at the highest privilege level and execute `cli`, `hlt`, `mov cr3`, `wrmsr` | it could disable interrupts for the whole machine, halt the core, remap any memory | a second set of privilege levels (VMX non-root, Arm EL2, RISC-V HS-mode) whose controls say which instructions exit | sections 3, 4 |
| **Memory** | page tables that map to "physical" RAM it owns from address 0 | it could map any host page | a second translation (EPT, NPT, stage 2) owned by the hypervisor | section 5 |
| **Interrupts and timers** | an interrupt controller and a timer it programs | it could mask the host's timer interrupt or receive another VM's interrupts | virtual interrupt controller, injection, posted interrupts | section 6 |
| **Devices and DMA** | a NIC and a disk with registers and DMA | DMA to any address; two VMs fighting over one device | emulation, virtio, IOMMU, SR-IOV | section 7 |
| **Time** | a steady TSC and clocks | clocks that jump when the VM is paused or migrated | TSC offset and scale, `kvm-clock` | section 6 |

Everything below is a variation on one idea: *let the guest run, and make the dangerous operations land in the hypervisor*.

---

## 2. Why x86 needed help

### 2.1 Trap and emulate

The classic technique works on any CPU with two privilege levels. Run the guest operating system at the *lower* level (the hypervisor keeps the higher one); every instruction the guest kernel is not allowed to execute then **traps** to the hypervisor, which emulates it against the *virtual* CPU state and resumes the guest.

Example: the guest kernel executes `cli` to disable interrupts. The CPU, running it at the lower privilege, raises a general-protection fault. The hypervisor catches it, sees that it was a `cli`, clears the *virtual* interrupt-enable flag it keeps for that vCPU (so it will not inject an interrupt until the guest re-enables it), moves the program counter past the instruction and resumes. The guest never knew. Cost: one fault and some code, a microsecond or two, paid only on privileged instructions, which are rare in normal code. Ordinary arithmetic, loads and stores run at full speed.

Popek and Goldberg proved when this is enough: **every sensitive instruction must also be privileged.** A *sensitive* instruction is one whose behaviour depends on, or changes, the machine's privilege state or configuration (reading the current privilege level, changing the interrupt flag, loading the page-table base). A *privileged* one is one that traps when run at the lower privilege. If every sensitive instruction traps, the hypervisor sees all of them and the technique is sound. If even one does not, a guest can run it and either see the real machine or silently get the wrong result.

### 2.2 Seventeen holes

The x86 instruction set was designed before anyone expected this. In 2000 Robin and Irvine analysed the Pentium and found **seventeen** instructions that are sensitive but not privileged, so a monitor never gets a chance to intercept them. The families:

- **Reading privileged state.** `SGDT`, `SIDT`, `SLDT`, `STR` and `SMSW` store the descriptor-table registers, the task register and the machine status word to memory with no privilege check. `mov` from a segment register such as `CS` exposes the current privilege level in its low two bits.
- **Changing it silently.** `POPF` pops the flags register; at user privilege it does not fault but quietly *ignores* the changes to the interrupt flag.
- **Checking privilege against the wrong level.** `LAR`, `LSL`, `VERR` and `VERW` test segment access against the current privilege, so a guest kernel deprivileged to ring 3 gets different answers than it would in ring 0.

A short program runs the interesting ones from user mode on this machine, and the result is the whole problem in a few lines:

```c
unsigned long f0, f1;
__asm__ volatile("pushfq; popq %0" : "=r"(f0));                       // read the flags
unsigned long want = f0 ^ 0x200 ^ 0x400;                              // flip IF (bit 9) and DF (bit 10)
__asm__ volatile("pushq %1; popfq; pushfq; popq %0" : "=r"(f1) : "r"(want) : "cc");
__asm__ volatile("cld");                                              // put DF back: the C ABI needs it clear
```

*Measured* output (print `f0`, `want` and `f1` afterwards, in hexadecimal):

```
popf:  flags before 0x246, asked for 0x446, got 0x646  | IF UNCHANGED (silently ignored), DF changed, no fault
mov %cs: 0x33, CPL = 3
```

`DF` changed because any program may set it. `IF` did not change, and nothing told the program. A deprivileged guest kernel that executes `popf` to re-enable interrupts would believe it had done so, no trap would reach the hypervisor, and the virtual interrupt flag would stay clear forever: the guest would stop receiving interrupts. And `mov %cs` gave `0x33`, whose low two bits say ring 3. A kernel that checks "am I in ring 0?" before doing something privileged would discover it is not.

The other family is visible too, but the answer is different on a modern machine:

```
sgdt:  limit 0x0 base 0xfffffffffffe0000
sidt:  limit 0x0 base 0xffffffffffff0000
smsw:  0x80050033
str:   0x40
sldt:  0x0
```

Those bases are not real addresses; they are constants. Recent CPUs have **UMIP** (user-mode instruction prevention; the `umip` flag is present here), which makes exactly these five instructions fault outside ring 0, and Linux handles the fault by *emulating* them with made-up values (`0xfffffffffffe0000` and `0xffffffffffff0000` are the dummy descriptor-table bases in the kernel's [`umip.c`](https://github.com/torvalds/linux/blob/master/arch/x86/kernel/umip.c), the `smsw` result is a fixed image of the control register, and `str` and `sldt` return the task-register and LDT selectors), because some programs (Wine, DOSEMU) still use them. So UMIP closes five of the seventeen holes, and it did so long after virtualization had been fixed another way.

### 2.3 Three answers

By the late 1990s people had virtual machines on x86 anyway. They worked around the holes in three ways, and the third is what you use today.

1. **Binary translation (VMware).** Before running guest *kernel* code, scan it and rewrite each problem instruction into a safe sequence or a call into the monitor; run the translated code. User code runs untouched. Fast where traps would have been frequent, but complex, and the translator must be exactly right.
2. **Paravirtualization (Xen).** Change the *guest* operating system so that it never executes the awkward instructions: it calls the hypervisor with hypercalls instead. Fast and simple for the hypervisor, but it needs a modified guest kernel. The idea survives in a lighter form: a modern Linux guest replaces a few hot paths with hypercall-based versions (section 6.3 shows the lines in this guest's boot log).
3. **Hardware assistance (Intel VT-x, 2005; AMD-V, 2006).** Add a new *dimension* of privilege, so that the guest kernel runs at its own ring 0 and the hypervisor stays above it. The hardware, not software tricks, decides what traps (section 3).

Hardware was not an instant win. Adams and Agesen measured the first generation in 2006 and concluded that it "fails to provide an unambiguous performance advantage" over the best software VMM, for two reasons: the cost of switching between guest and monitor was high (a VM entry took **2,409 cycles** on a Pentium 4, and 937 on the next-generation Core), and the hardware gave no help with *memory*, so the monitor still had to keep shadow page tables and trap every page-table write. Their `forkwait` benchmark (which creates and destroys processes, so it rewrites page tables constantly) took 6.02 s natively, 36.95 s on the binary-translating VMM and **106.4 s** on the first hardware VMM. Two things then had to improve, and both did: the exit had to get cheaper, and the second level of address translation had to move into hardware (EPT in 2008, section 5).

### 2.4 A real example: virtualization without the hardware, in 2024

Look at the boot log of the guest this tutorial was written in:

```
kvm: not registering kvm_pvm, so there is no /dev/kvm: the kernel command line does not carry nested_pvm (add nested_pvm=1 to enable)
```

This kernel contains **PVM** ("pagetable-based virtual machine"), a recent proposal by Alibaba Cloud and Ant Group that is described in [LWN](https://lwn.net/Articles/963718/). It runs a guest on x86 *without* VT-x: the guest kernel is deprivileged to ring 3, a small "switcher" in the host's entry code handles transitions, paravirtual interfaces replace the awkward instructions, and shadow page tables stand in for EPT. It exists because public clouds often disable nested hardware virtualization, so a customer who wants to run VM-based containers (Kata) inside a cloud VM has no `vmx` to use; that is the situation of this guest (no `vmx` flag, no `/dev/kvm`). The same article states the price: shadow paging "performs very poorly" when the guest changes page tables often, such as during heavy process creation. Which is, almost exactly, the 2006 `forkwait` result. The ideas of this section are not history; they are what you reach for when the hardware is not available. (I did not boot the PVM mode here: the log says it needs a command-line option I cannot set.)

---

## 3. Hardware-assisted CPU virtualization (x86)

### 3.1 Two modes, orthogonal to the rings

VT-x does not remove the rings; it adds a second axis. The CPU is either in **VMX root operation** (where the hypervisor runs) or **VMX non-root operation** (where a guest runs). Each mode has the usual four rings, so the guest kernel runs in *its own* ring 0 and guest applications in ring 3, exactly as they would natively. The difference is in what the hardware does when the guest tries something that must stay under hypervisor control.

```
        VMX root operation                      VMX non-root operation
   (the hypervisor, e.g. Linux+KVM)                   (a guest)
   ring 0: KVM, the host kernel                  ring 0: the guest kernel
   ring 3: the VMM process (QEMU, Firecracker)   ring 3: guest applications
                  |                                        ^
                  |   VMLAUNCH / VMRESUME  ("VM entry")    |
                  +----------------------------------------+
                  |                                        |
                  +<---------  VM exit  -------------------+
                    (reason and details written to the VMCS)
```

AMD's version has the same shape with different names: `VMRUN` enters the guest, and a `#VMEXIT` returns to the host; the control structure is the **VMCB**, and guests are tagged with an **ASID**.

### 3.2 The VMCS: where the state lives

For each vCPU the hypervisor keeps a **VMCS** (virtual-machine control structure), a block of memory the CPU reads and writes at entry and exit through special instructions (`VMPTRLD` to make one current, `VMREAD` and `VMWRITE` to access fields, `VMLAUNCH` and `VMRESUME` to enter). Its areas:

| Area | Holds |
|---|---|
| **Guest-state** | the guest's `RIP`, `RSP`, `RFLAGS`, control registers (`CR0`, `CR3`, `CR4`), segment registers, descriptor tables, key MSRs; loaded at entry, saved at exit |
| **Host-state** | where to resume when the guest exits: the host's `RIP`, `RSP`, `CR3`, segment state |
| **VM-execution controls** | *the* configuration: which events cause exits (next section) |
| **VM-entry and VM-exit controls** | what to load or save at the transition |
| **Exit information** | filled by hardware at an exit: the **exit reason**, an *exit qualification* (e.g. the address of an access), the faulting guest-physical address, the instruction length |

Hardware saves and loads the registers above on its own. Everything else (the other 15 general-purpose registers, the floating-point and vector state, and MSRs outside the VMCS) is saved by the hypervisor's software. That is a large part of why an exit costs thousands of cycles rather than dozens, and why hypervisors work hard to avoid touching vector state on short exits.

### 3.3 What exits, and who decides

**The hypervisor chooses.** The VM-execution controls hold bitmaps and flags: exit on `HLT`? on `INVLPG`? on access to which MSRs? on which exceptions? on external interrupts? A few instructions exit unconditionally, among them `CPUID`, `XSETBV`, `INVD` and the VMX instructions themselves. These are the exit reasons you will meet in every hypervisor, with their numbers from Linux's [`vmx.h`](https://github.com/torvalds/linux/blob/master/arch/x86/include/uapi/asm/vmx.h):

| # | Exit reason | Typical cause | Who normally handles it |
|---|---|---|---|
| 0 | exception or NMI | a guest fault the hypervisor intercepts (page faults under shadow paging) | KVM |
| 1 | external interrupt | a host interrupt arrived while the guest ran | host kernel, then re-enter |
| 10 | `CPUID` | the guest asks what CPU it has (always exits) | KVM |
| 12 | `HLT` | the guest is idle | KVM (may poll, then schedule another task) |
| 14 | `INVLPG` | flush one TLB entry (only needed under shadow paging) | KVM |
| 16 | `RDTSC` | read the time-stamp counter (exits only if the hypervisor asks) | KVM |
| 18 | `VMCALL` | a hypercall | KVM |
| 28 | control-register access | `mov cr3` and friends (needed under shadow paging) | KVM |
| 30 | I/O instruction | `in`/`out` to a port | KVM or the user-space VMM |
| 31, 32 | `RDMSR`, `WRMSR` | read or write a model-specific register | KVM; some go to user space |
| 40 | `PAUSE` | a guest spin loop ("pause-loop exiting") | KVM yields the vCPU |
| 44, 45, 56 | APIC access, EOI-induced, APIC write | the guest touched the (virtual) interrupt controller | KVM |
| 48 | **EPT violation** | the guest touched a guest-physical address with no valid second-level mapping | KVM; may reach the user-space VMM (device registers) |
| 49 | EPT misconfiguration | a deliberately invalid mapping, used to catch device-register accesses fast | KVM |
| 52 | VMX-preemption timer | a hypervisor-programmed timer expired | KVM |

Read the table as a design guide. Everything *not* listed (ordinary arithmetic, loads and stores that hit mapped memory, `syscall`, guest page faults under nested paging, `rdtsc` with offsetting, `pause` in a short loop, most vector instructions) runs at native speed, and that is what "near-native performance" means.

### 3.4 Which instructions exit? Measure it

You can see the difference from inside a guest by timing unprivileged instructions in a loop. *Measured* in this guest, ns per instruction, pinned to CPU 0, three runs of a loop around each instruction (a compact version is in Problem D):

| Instruction | Time | Exit? |
|---|---|---|
| empty loop | 0.2 to 0.3 ns | no |
| `rdpid` | 2.2 to 2.3 ns | no |
| `lfence` | 4.4 to 5.1 ns | no |
| `xgetbv` | 5.1 to 5.9 ns | no |
| `pause` | 12.3 to 12.7 ns | no |
| `mfence` | 12.8 to 13.9 ns | no |
| `rdtsc` | 15.4 to 19.3 ns | no (the time-stamp counter is offset in hardware, section 6.4) |
| `rdtscp` | 22.6 to 25.5 ns | no |
| `rdrand` | 33.6 to 34.1 ns | no |
| `syscall` (`getppid`) | 108 to 127 ns | no (a guest system call never involves the hypervisor) |
| `rdseed` | 807 to 844 ns | unclear |
| `cpuid`, leaf 0 | **6.9 to 7.6 µs** | **yes** |
| `cpuid`, leaf 7 | 7.4 to 8.8 µs | yes |
| `cpuid`, leaf `0x40000000` | 8.3 to 9.5 µs | yes |

Everything unprivileged costs nanoseconds, and `cpuid` alone costs microseconds: about **15,000 TSC ticks (21,000 core cycles at the 3 GHz the core actually ran at, [tutorial 28](28_performance_and_power.md) section 2.2), sixty times a system call**. That is the price of one exit in this guest. (`rdseed` is slow too, but it is a slow instruction on any machine, since it waits for hardware entropy; I cannot tell from here whether an exit is involved and did not investigate. `cpuid` on leaf `0x40000000`, the hypervisor's own leaf, was a little slower than leaf 0; I did not look into why.)

### 3.5 What an exit costs, from the literature

A VM exit is not one thing. It is the hardware transition (save guest state, load host state), then the hypervisor's software prologue (save the rest, find the reason), then the handler, then the epilogue and the entry. Published measurements, all on the same Xeon E5-2450 (2.1 GHz base clock; the paper reports cycles) with KVM, from Dall et al. (2017):

| Operation | x86 cycles | At 2.1 GHz | What it is |
|---|---|---|---|
| Hypercall | 1,437 | 0.68 µs | guest to the host kernel's KVM code and back, doing nothing |
| I/O Kernel | 2,565 | 1.22 µs | a trap to an emulated device handled *inside the host kernel* |
| Virtual IPI | 3,102 | 1.48 µs | the guest sends an interrupt to another vCPU |
| I/O User | 6,732 | 3.21 µs | a trap to an emulated device handled *in the user-space VMM* (QEMU) |

Two lessons. First, an exit that stays in the kernel costs about 0.7 to 1.5 µs; one that has to go out to a user-space process and back costs roughly **three to five times** more, which is why hypervisors push hot paths (virtqueue notifications, interrupt controllers) into the kernel (section 7). Second, the base cost has fallen: Adams and Agesen found the VM *entry* alone took 2,409 cycles on a Pentium 4 and 937 on a Core processor; a whole hypercall round trip is now about 1,400 cycles on the Xeon above.

Then the nesting factor. Ben-Yehuda et al. (the Turtles project, 2010) timed `cpuid` at about **100 cycles** on bare metal, about **2,600** in an ordinary VM and about **58,000** in a *nested* VM (a guest running its own hypervisor), because every instruction the inner hypervisor executes to handle one exit (`vmread`, `vmwrite`, `vmresume`) is itself an exit to the outer one: "exit multiplication". This guest measured about 15,000 cycles, between the two published figures. I cannot see the host: it may simply be a slow path, or the host may be virtualised too. The point to carry away is the *ratio*: here an exit is about 60 system calls.

### 3.6 What a VMM does with an exit

KVM is a kernel module; `/dev/kvm` gives a user-space program an ioctl interface. A VMM such as QEMU or Firecracker runs one thread per vCPU, and each thread is essentially this loop:

```c
for (;;) {
    ioctl(vcpu_fd, KVM_RUN, 0);               // kernel: VM entry. Returns only for an exit KVM cannot finish itself.
    switch (run->exit_reason) {
    case KVM_EXIT_IO:       emulate_port_io(run);   break;   // the guest executed in/out
    case KVM_EXIT_MMIO:     emulate_mmio(run);      break;   // the guest touched a device register
    case KVM_EXIT_SHUTDOWN: return;
    }                                         // then loop: the next KVM_RUN is the VM entry
}
```

Most exits never come back to this loop. KVM handles `cpuid`, MSR accesses, EPT violations on ordinary memory, timers and (with its in-kernel interrupt controller) halts inside the kernel, and returns to the guest at once; only exits that need a *device model* (a port or a memory-mapped register that belongs to emulated hardware) are handed up to the process. That split is the "I/O Kernel" and "I/O User" rows above.

---

## 4. Arm and RISC-V: the same idea, different clothes

### 4.1 Arm: EL2 and stage-2 translation

Arm built the second privilege axis into the exception-level model. A guest kernel runs at **EL1** and applications at **EL0**, as natively; the hypervisor sits at **EL2**, one level above ([tutorial 26](26_interrupts_exceptions_and_dma.md) section 2.4). There is no separate "guest mode": a guest is simply whatever runs at EL0 or EL1 *while the EL2 controls are configured to trap it*. The controls are registers, not a memory structure:

- **`HCR_EL2`** (hypervisor configuration register). `VM` turns on stage-2 translation; `IMO`/`FMO`/`AMO` route physical interrupts and aborts to EL2 and make *virtual* ones available to EL1; `TWI`/`TWE` trap the guest's `wfi`/`wfe` (so an idle guest reaches the hypervisor, like x86's `HLT`); `TGE` and `E2H` are used by the host kernel running at EL2 (below).
- **`VTTBR_EL2`** holds the base of the **stage-2** page tables together with an 8- or 16-bit **VMID**. Arm calls the guest-physical address an **IPA** (intermediate physical address): the guest's own tables (stage 1) turn a virtual address into an IPA, and stage 2 turns the IPA into a real physical address.
- **Entering and leaving.** The hypervisor enters the guest with `eret`. A trap arrives as an ordinary exception at EL2 (class `0x16` in `ESR_EL2` for `hvc`, the hypercall; `0x24` for a data abort from a lower level, including stage-2 faults, whose faulting IPA is in `HPFAR_EL2`).

A subtle point decided the cost of KVM on Arm. Armv8.0 Linux expects to run at EL1, but the hypervisor must run at EL2. So the first KVM/ARM ran the host kernel at EL1 and put a small piece of code at EL2 ("split mode"): every exit meant guest at EL1, to EL2, then switching the whole EL1 register context to the *host's* kernel, then back. **Armv8.1's Virtualization Host Extensions (VHE)** let the host kernel itself run at EL2: with `HCR_EL2.E2H` set, EL2 gets register encodings and a page-table format that look like EL1's, so a kernel written for EL1 runs unchanged, and `_EL12` aliases let it touch the guest's EL1 state. Dall et al. measured the effect (cycles, as printed in their Table 2; no VHE silicon existed then, so their "EL2 OPT" column is a redesign on current hardware that approximates it):

| Operation | Arm, split mode | Arm, host at EL2 (optimised) | x86 (same paper) |
|---|---|---|---|
| Hypercall | 6,413 | **752** | 1,437 |
| I/O Kernel | 8,034 | **1,604** | 2,565 |
| Virtual IPI | 13,121 | **2,526** | 3,102 |
| I/O User | 10,012 | 7,630 | 6,732 |

Where the host kernel sits changed a kernel-handled exit by a factor of **8.5** (hypercall), while the exit that must go out to a user-space process barely moved (10,012 to 7,630): the cost there is the host's own context switches, not the transition. This is the best single illustration in the field that *where the code sits* matters as much as what the hardware does.

Virtual interrupts and time are in hardware too. The GIC's **virtual CPU interface** lets the hypervisor place a pending virtual interrupt into one of a few **list registers** (`ICH_LR<n>_EL2`; the number is implementation-defined, up to 16); the guest then acknowledges and ends it with its *normal* `ICC_IAR1_EL1` and `ICC_EOIR1_EL1` accesses, which the hardware redirects to the virtual interface, with no trap. **GICv4** goes further and delivers virtual MSIs from a device straight to the right vCPU. For time, the guest reads a *virtual* counter equal to the physical one minus `CNTVOFF_EL2` and arms the *virtual* timer directly, with no trap.

### 4.2 RISC-V: the H extension

RISC-V adds the second axis as a **hypervisor extension** ("H"). The privilege modes become **HS** (hypervisor-extended supervisor: where the hypervisor, or a host OS with one, runs) plus **VS** and **VU** (virtualised supervisor and user), selected by a **V bit** in addition to the privilege level. A guest kernel runs in VS-mode and believes it is in S-mode.

- **Two-stage translation.** The guest's own `satp` is replaced in VS-mode by `vsatp` (guest-virtual to guest-physical); the hypervisor's **`hgatp`** (CSR `0x680`) holds the **G-stage** tables (guest-physical to real) and a VMID field (14 bits on RV64, per Linux's `csr.h`). The G-stage formats are `Sv39x4`, `Sv48x4` and `Sv57x4`; the "x4" means the root table is four times larger, two extra address bits for the guest-physical space.
- **Who handles what.** `hedeleg` and `hideleg` delegate exceptions and interrupts straight to VS-mode, so a guest page fault or timer interrupt does not trap to the hypervisor. `hvip` lets the hypervisor inject a virtual interrupt.
- **Traps to the hypervisor.** New causes: **guest page faults** (20 for instruction fetch, 21 for load, 23 for store or atomic) when the G-stage fails, and a **virtual instruction exception** (22) when the guest runs something it must not. `ecall` from VS-mode is cause 10 (the hypercall, in effect). `htval` holds the faulting guest-physical address and `htinst` a decoded copy of the trapping instruction, so the hypervisor can emulate a device-register access without re-reading guest memory.

### 4.3 Side by side

| | Intel VT-x | AMD-V | Arm | RISC-V H |
|---|---|---|---|---|
| Hypervisor runs in | VMX root | host mode | EL2 | HS-mode |
| Guest kernel runs in | non-root ring 0 | guest mode ring 0 | EL1 | VS-mode |
| Control state | VMCS in memory | VMCB in memory | registers (`HCR_EL2`, `VTTBR_EL2`, ...) | CSRs (`hstatus`, `hgatp`, ...) |
| Enter the guest | `VMLAUNCH`/`VMRESUME` | `VMRUN` | `eret` | `sret` with V = 1 |
| Back to the hypervisor | VM exit (hardware) | `#VMEXIT` | exception to EL2 | trap to HS-mode |
| Second-stage translation | EPT | NPT (RVI) | stage 2 | G-stage |
| TLB tag | VPID | ASID | VMID | VMID |
| Hypercall | `vmcall` | `vmmcall` | `hvc` | `ecall` (cause 10) |
| Virtual interrupts | virtual-APIC page, posted interrupts | AVIC | GIC list registers, GICv4 | `hvip`, AIA guest files |

The shape is the same in all four: a place for the hypervisor, a place for the guest, a table of "which events trap", a second translation, a tag that keeps TLB entries of different guests apart, and a way to inject a virtual interrupt. Learn the shape once; the names are in the data sheet.

---

## 5. Memory virtualization

### 5.1 Three addresses

A guest has two layers of addresses, and the machine has a third:

```
guest application ──► guest-virtual address (gVA)
                           │  guest page tables (the guest's CR3)
                           ▼
                      guest-physical address (gPA)  ← what the guest kernel believes is RAM
                           │  second level: EPT (Intel), NPT (AMD), stage 2 (Arm), G-stage (RISC-V)
                           ▼
                      host-physical address (hPA)   ← the real DRAM location
```

The guest kernel manages the first translation exactly as it would on bare metal. The hypervisor owns the second. The question is who combines them.

### 5.2 Shadow page tables: software does it

Before hardware support, the hypervisor built a **shadow page table** that maps gVA directly to hPA, and pointed the real `CR3` at it. For this to stay correct it must know whenever the guest changes its own tables: it write-protects the pages that hold guest page tables, so that every guest store to a page-table entry faults into the hypervisor, and it intercepts guest `CR3` loads and `INVLPG`. The benefit is that a TLB miss walks only one tree (4 reads). The cost is an exit for every page-table update, which is why Adams and Agesen's `forkwait` took 106.4 s against 6.02 s native (section 2.3), and why PVM, which still uses shadow paging, says it suffers when guests create processes heavily. Workloads that change page tables rarely (a long-running Java service) do fine.

### 5.3 Nested paging: hardware does it

**EPT** (Intel, from the Nehalem generation in 2008), **NPT/RVI** (AMD, Barcelona), **stage 2** (Arm) and the **G-stage** (RISC-V) put both translations in hardware. The guest's page faults and `CR3` writes no longer exit at all; the hypervisor sees only exits when the *second* level has no valid entry (an **EPT violation**, exit reason 48). The price is on a TLB miss, where the page walker must do a two-dimensional walk:

1. The guest's `CR3` holds a *guest-physical* address. Before the walker can read the top-level guest table it must translate that address through the host's tree: `m` reads, then one read of the guest table entry.
2. The entry holds the *guest-physical* address of the next-level guest table: again `m` reads to translate, then one read.
3. After all `n` guest levels it has the data's guest-physical address, which needs `m` more reads.

The total is `n × (m + 1) + m = (n + 1)(m + 1) - 1`. With four levels each: **24 references**, against 4 on bare metal. A simulation of exactly that walk, counted by script, for the page-size combinations that occur:

| Guest pages | Host pages | n | m | References per full walk |
|---|---|---|---|---|
| (bare metal) | none | 4 | 0 | 4 |
| 4 KB | 4 KB | 4 | 4 | **24** |
| 4 KB | 2 MB | 4 | 3 | 19 |
| 4 KB | 1 GB | 4 | 2 | 14 |
| 2 MB | 4 KB | 3 | 4 | 19 |
| 2 MB | 2 MB | 3 | 3 | 15 |
| 1 GB | 1 GB | 2 | 2 | 8 |
| 5-level | 5-level | 5 | 5 | 35 |
| 5-level | 4-level | 5 | 4 | 29 |

(The formula and the simulation agree on every row.) Those are worst cases. Three things make real walks much cheaper:

- **The TLB caches the combined translation** (gVA to hPA), so a hit costs the same as on bare metal. Only misses pay.
- **Walk caches** (partial-translation caches for both dimensions) skip the upper levels of either tree, as in [tutorial 15](15_virtual_memory_and_the_tlb.md) section 6.
- **The page-table lines themselves are cached** as ordinary data. If each reference hits the L2 cache at an assumed 4 ns, a 24-reference walk costs 96 ns against 16 ns native; if each is an L3 hit at an assumed 15 ns, 360 ns against 60 ns. Both are assumptions for the arithmetic; the ratio, six times, is the point.

For measured cost: Gandhi et al. (MICRO 2014) report that on a big-memory workload (graph500, 4 KB pages) the address-translation overhead was **28 %** natively and **113 %** when virtualised (4 KB in both dimensions), about four times as much. And in this guest [tutorial 15](15_virtual_memory_and_the_tlb.md) measured a TLB miss adding **45 to 75 ns** to a DRAM access with 4 KB pages against about **10 ns** with 2 MB pages (section 6.3 there), a gap the second dimension widens.

### 5.4 Huge pages must exist in both dimensions

The table has a lesson for anyone tuning a cloud workload. Turning on transparent huge pages in the *guest* gives a 3-level guest tree, but if the host backs guest memory with 4 KB pages the walk is still 19 references, and there is a second effect: a TLB entry caches a translation of the *smaller* of the two page sizes (the combined mapping can only be as large as the piece both levels agree on, called "splintering"), so a 2 MB guest page on 4 KB host pages is cached as 4 KB entries and the TLB reach does not grow. Huge pages in the guest help only to the extent the host uses them too, which is why high-performance cloud and HPC setups ask the hypervisor to back guest memory with 2 MB or 1 GB pages.

### 5.5 Tagged TLBs: no flush on every switch

A VM entry and exit changes the address space (the hypervisor's tables, the guest's tables), and a plain TLB would have to be emptied each time. Instead each entry is **tagged** with the identity of the VM that created it: Intel's **VPID** (16 bits), AMD's **ASID**, Arm's **VMID**, RISC-V's **VMID**. An exit and re-entry then keeps the guest's translations alive, and the hypervisor flushes selectively (`INVEPT` and `INVVPID` on Intel). Inside the guest, the guest's own process tags (PCID/ASID) still work, which is why the numbers above can be as good as they are. Without tags, a 1,400-cycle exit would be followed by thousands of cycles of TLB refill.

### 5.6 The hypervisor as a memory manager

From the host's point of view a guest's RAM is just a range of virtual memory in the VMM process. KVM calls it a *memory slot*; the VMM `mmap`s anonymous memory and registers it as, say, guest-physical addresses 0 to 8 GB. Pages are allocated **on first touch**: when the guest touches a guest-physical page that has no host page yet, the second-level walk fails, an EPT violation exits, KVM takes an ordinary host page fault (allocating and zeroing a page), installs the mapping and resumes. That is demand paging twice. [Tutorial 15](15_virtual_memory_and_the_tlb.md) saw it in this guest: touching fresh memory cost 1.5 to 2.5 µs per page in steady state and **18 µs the first time this VM used that memory**, "when the host also had to supply it". Re-measured for this tutorial (touching 1 GiB with 4 KB pages, four rounds), the first round cost 2.5 µs per page and the later rounds 1.95 to 1.97 µs, about 0.55 µs (28 %) more for the first touch; a later run over 2 GiB showed no first-round penalty, because by then this VM's memory had been backed. What the host did and when, I cannot see.

The hypervisor then has several ways to use RAM more cleverly than a one-to-one split, and the first is visible here:

- **Ballooning.** A driver in the guest (`virtio-balloon`) allocates pages on request and tells the host which, so the host can reclaim them for another VM. This guest has one: `virtio0` in `/proc/interrupts` has queues named `inflate`, `deflate`, `stats` and `reporting`. The `stats` queue was serviced 154 times in 776 s of uptime, **once every 5 s**: the host is polling the guest for memory statistics.
- **Free-page reporting** (the `reporting` queue, 29 events in 776 s): the guest tells the host which pages are free, so the host can unmap them from the second level and give the memory back.
- **Same-page merging** (KSM): the host finds identical pages across VMs and shares one copy.
- **Dirty tracking for live migration.** To move a running VM, the hypervisor copies its memory while it runs and must know which pages changed: it write-protects second-level entries (a write then exits) or uses **page-modification logging** (a hardware log with its own exit reason, 62, when the log fills).

The interaction between the memory-management layers matters: the guest also reclaims, swaps and uses huge pages, without knowing what the host is doing. A guest that "has" 8 GB may really have far less backed by DRAM, and the guest cannot tell.

---

## 6. Interrupts, timers and idle in a guest

### 6.1 The slow path: inject on entry

A physical device interrupts the host; the virtual device of a guest must interrupt the *guest*. Without hardware help the sequence is: the host takes the physical interrupt (an *external-interrupt exit*, reason 1, if a guest was running), a backend thread or the VMM handles the device's data, the hypervisor records a pending virtual interrupt for the target vCPU, and on its next VM entry writes it into the VMCS's **entry interruption-information field**, so the CPU delivers it to the guest as if it had just arrived. The guest handler runs, ends with a write to the (virtual) APIC's end-of-interrupt register, and that write is an exit too. One interrupt costs two to three exits, which at the rates of [tutorial 26](26_interrupts_exceptions_and_dma.md) section 4.3 is a large tax: at 100,000 interrupts a second and three exits of about a microsecond each, 30 % of a core.

### 6.2 Hardware that removes the exits

- **Virtual APIC (Intel APICv; AMD AVIC).** The guest's local APIC registers live in a **virtual-APIC page** in memory that the CPU reads and writes directly: most register reads no longer exit. **Virtual-interrupt delivery** lets the hardware itself decide which pending virtual interrupt to take, and *virtualises EOI*: the guest ends an interrupt without a trap. (APICv arrived with the Ivy Bridge-EP generation of Xeons, late 2013 to early 2014.)
- **Posted interrupts.** A 256-bit **posted-interrupt descriptor** per vCPU lives in memory. To interrupt a *running* vCPU, anybody (the hypervisor on another core, or an assigned device through the IOMMU's interrupt remapping) sets a bit in the descriptor and sends a single **notification vector** to the core running the vCPU; the hardware merges the bit into the virtual-APIC page and delivers the interrupt to the guest **without a VM exit**. If the vCPU is not running, a wake-up notification goes to the hypervisor instead. With this, a device assigned to a guest (section 7) can interrupt it with no hypervisor code on the path.
- **Arm** has the equivalent in the GIC's list registers and GICv4 direct injection (section 4.1); **RISC-V** with the AIA gives each guest a dedicated interrupt file.

### 6.3 Paravirtual shortcuts: the evidence in this guest

Even with hardware help, a guest that *knows* it is virtualised can skip work. Linux's KVM guest code does this and prints it at boot. These lines are from this VM's `dmesg`:

```
Hypervisor detected: KVM
kvm-clock: Using msrs 4b564d01 and 4b564d00
kvm-guest: APIC: eoi() replaced with kvm_guest_apic_eoi_write()
kvm-guest: KVM setup pv remote TLB flush
kvm-guest: setup PV sched yield
kvm-guest: PV spinlocks enabled
kvm-guest: APIC: send_IPI_mask() replaced with kvm_send_ipi_mask()
kvm-guest: setup PV IPIs
```

The guest finds out what the hypervisor offers by reading **CPUID leaves in a range the hypervisor owns**: leaf `0x40000000` returns a vendor string (here `KVMKVMKVM`, read by a small program, below) and leaf `0x40000001` a bit mask of features. This guest got `0x01007efb`:

| Bit | KVM_FEATURE | What it lets the guest skip |
|---|---|---|
| 0, 3 | CLOCKSOURCE, CLOCKSOURCE2 | `kvm-clock`: read time from a page the host updates, no exit |
| 1 | NOP_IO_DELAY | the old "write to port 0x80 to delay" no longer needed (it exits) |
| 4, 10, 14 | ASYNC_PF (and variants) | when a guest page is not backed by the host, tell the guest so it can run another task instead of freezing the whole vCPU |
| 5 | STEAL_TIME | the host records how long this vCPU was runnable but not running (below) |
| 6 | PV_EOI | end an interrupt by clearing a flag in shared memory, no APIC write |
| 7 | PV_UNHALT | paravirtual spinlocks: a waiter halts and the lock holder wakes it |
| 9 | PV_TLB_FLUSH | flush a descheduled vCPU's TLB lazily at its next entry, not with an IPI it cannot answer |
| 11 | PV_SEND_IPI | send an IPI to many vCPUs with one hypercall |
| 12 | POLL_CONTROL | the guest can switch off the host's halt polling (section 6.5) |
| 13 | PV_SCHED_YIELD | a vCPU hands its time to an IPI target that is preempted |
| 24 | CLOCKSOURCE_STABLE_BIT | `kvm-clock` will not jump between vCPUs |

This is paravirtualization as it survives today: not a modified kernel, but a menu of optional hypercalls and shared-memory flags, discovered through CPUID and used where they help. The reason each one exists is the same: an exit is expensive, and a guest that can avoid one by *telling* the host something instead of *doing* something privileged saves thousands of cycles.

The shortest program that shows all this from inside a guest:

```c
#include <stdio.h>
#include <string.h>
#include <cpuid.h>
int main(void) {
    unsigned a, b, c, d;
    __cpuid(1, a, b, c, d);   printf("hypervisor bit: %u\n", (c >> 31) & 1);        // CPUID.1:ECX[31]
    __cpuid(0x40000000, a, b, c, d);
    char v[13]; memcpy(v, &b, 4); memcpy(v + 4, &c, 4); memcpy(v + 8, &d, 4); v[12] = 0;
    printf("vendor \"%s\", max hypervisor leaf 0x%x\n", v, a);                       // "KVMKVMKVM"
    __cpuid(0x40000001, a, b, c, d); printf("KVM features 0x%08x\n", a);              // 0x01007efb here
    return 0;
}
```

*Measured* here: hypervisor bit 1, vendor `KVMKVMKVM`, maximum hypervisor leaf `0x40000001`, features `0x01007efb`. On a physical machine the first line prints 0.

### 6.4 Time, timers and "steal"

**The time-stamp counter.** A guest executes `rdtsc` directly: the CPU adds a per-vCPU **TSC offset** (and, on newer CPUs, multiplies by a scaling ratio) held in the VMCS, so the guest sees a counter that stays continuous when the VM is migrated to a machine with a different frequency. That is why `rdtsc` cost 15 to 19 ns here, the same as native, with no exit. This guest even uses the TSC as its clock source (`/sys/devices/system/clocksource/clocksource0/current_clocksource` says `tsc`; `kvm-clock` is also available) because the CPU reports an invariant, known-frequency TSC.

**Timers.** Programming a timer is different. The guest writes the TSC-deadline MSR or an APIC register, which exits; the hypervisor arms a host timer (or the VMX-preemption timer, exit reason 52) to fire at that time and injects an interrupt when it does. A guest that takes a 250 Hz tick on every busy vCPU ([tutorial 26](26_interrupts_exceptions_and_dma.md) section 6.4) therefore pays exits for every tick; this is one of the reasons tickless idle (`NO_HZ`) matters even more in a VM, and why a "hiccup" test in a VM mostly measures the hypervisor, as that tutorial found.

**Steal time.** A vCPU is a thread; when the host has more runnable threads than cores, a vCPU waits. The guest cannot see that, except through the **steal time** the host publishes in shared memory (feature bit 5): the number of nanoseconds this vCPU was ready to run but not running. It appears as the 8th field of the `cpu` line in `/proc/stat` and as `st` in `top`. In this guest, `/proc/stat` showed **134 steal ticks out of 155,113**, 0.09 % over 776 s of mostly idle time; with both vCPUs spinning for 5 s the steal counter rose by **12 ticks out of 1,000**, 1.2 %. That is a lightly loaded host. On an oversold host it can be far higher, and it shows up as unexplained latency: the first number to check when a cloud benchmark gets noisy.

### 6.5 Idle and wake-up

When the guest has nothing to run it executes `HLT` (or `MWAIT`), and that exits: the host scheduler gives the physical core to something else. Waking the vCPU needs the host to notice (an interrupt for that vCPU, an IPI from another vCPU, a timer) and schedule the thread again; that is much longer than waking a core that merely clock-gated. KVM softens this with **halt polling**: after a `HLT` exit the host spins for a short, tunable while (`halt_poll_ns`) in case the wake-up arrives at once, trading CPU for latency, and `POLL_CONTROL` (bit 12 above) lets the guest ask the host not to. The effect is visible in [tutorial 26](26_interrupts_exceptions_and_dma.md) section 5.2, where in this guest a hand-off to a *sleeping* thread on the other CPU took **10.0 to 10.5 µs** and to a *spinning* one **56 to 60 ns**: most of the 10 µs is the IPI, the `HLT` exit and the host's wake-up, not the kernel's own work.

---

## 7. Devices and I/O

### 7.1 Five ways to give a guest a NIC

[Tutorial 25](25_buses_pcie_and_io_mapping.md) section 7 listed the four standard answers; with what this tutorial added about exits, here they are again with the cost column filled in, plus the two refinements of virtio that matter in production.

| Approach | What the guest sees | Exits on the data path | Host CPU | Live migration | Needs |
|---|---|---|---|---|---|
| **Full emulation** (an Intel e1000 NIC, an IDE disk) | a real old device | several per packet: every register access exits to the **user-space** VMM ("I/O User", about 6,700 cycles each) | highest | easy | nothing; any guest OS has a driver |
| **virtio** with a user-space backend | shared queues ([tutorial 26](26_interrupts_exceptions_and_dma.md) section 9.2) | one "kick" per batch of packets | moderate | easy | virtio driver in the guest |
| **virtio with a kernel backend** (`vhost`) | the same | one kick per batch, handled in the **host kernel** with no trip to user space | lower | easy | `vhost` module |
| **vhost-user** / **DPDK-style** polling backend | the same | none while the backend polls the queue; the price is a dedicated host core | low per packet, one core fixed | easy | a polling process on the host |
| **Passthrough** (whole device, via VFIO) or an **SR-IOV virtual function** | the real device or a slice of it | none: the guest drives the hardware and DMA is confined by the IOMMU | lowest | hard (state is inside the device) | an IOMMU ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 6) |

A sixth deserves a sentence: **vDPA** (virtio data path acceleration) has the *hardware* implement the virtqueue format, so the guest keeps its ordinary virtio driver and the data path bypasses the host, trying to get the portability of virtio and the speed of passthrough.

### 7.2 Why a virtio kick is cheap

A guest tells a virtio device there is work by writing a "queue notify" register in its PCI BAR. That write is an access to a guest-physical address no one has mapped, so it exits (an EPT misconfiguration or violation, reasons 49 and 48). The question is where it is handled. If the exit went up to the user-space VMM it would cost the "I/O User" figure, **6,732 cycles (3.2 µs)** in Dall et al.'s measurement; handled inside the kernel, "I/O Kernel", **2,565 cycles (1.2 µs)**. KVM supports the second with an **`ioeventfd`**: the VMM registers an event file descriptor against the notification address, and when the guest writes there, KVM itself signals the descriptor (the KVM documentation: a guest write in the registered address "will signal the provided event instead of triggering an exit" to user space) while the vCPU carries on. A `vhost` thread in the kernel waits on that descriptor, drains the queue, and signals the guest's completion interrupt through the mirror mechanism, an **`irqfd`**: writing to a descriptor makes KVM inject a chosen interrupt into the guest. The user-space VMM is out of the data path entirely; it only sets things up. Combined with the batching and notification suppression of virtqueues (`EVENT_IDX`, [tutorial 26](26_interrupts_exceptions_and_dma.md) section 9.2), the number of exits per packet falls to a small fraction.

### 7.3 This guest's device model

`/sys/bus/virtio/devices` lists exactly ten devices; the virtio device ID is in each device's `device` file (0x0005, 0x0002, 0x0001, 0x0013, 0x0004, shown here in decimal) and the driver name is the target of its `driver` link (*measured*):

| Device | virtio device ID | Driver | Vectors in `/proc/interrupts` |
|---|---|---|---|
| `virtio0` | 5 | `virtio_balloon` | config, inflate, deflate, stats, reporting |
| `virtio1` to `virtio6` | 2 | `virtio_blk` | config, `req.0` (each) |
| `virtio7` | 1 | `virtio_net` | config, `input.0`, `output.0` |
| `virtio8` | 19 | `vmw_vsock_virtio_transport` | config, rx, tx, event |
| `virtio9` | 4 | `virtio_rng` | config, input |

Every one is a PCI function with MSI-X ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 4), and the rest of `/proc/interrupts` is the serial port on I/O APIC pin 4 (`ttyS0`) and two ACPI general-event lines. That is the entire machine: **no graphics adapter, no USB, no sound, no legacy storage controller, no keyboard controller**. Each missing device is an emulation that would have had to be written, maintained, and defended against a hostile guest. This is the design of a **microVM**, and the Firecracker paper states its consequences:

- the guest kernel boots to application code in **under 125 ms** (with a minimal kernel configuration);
- memory overhead is **under 5 MB** per microVM (measured around **3 MB** against about 131 MB for QEMU in the same paper);
- a host can create up to **150 microVMs per second**;
- the VMM is about **50,000 lines of Rust**, against more than 1.4 million lines for QEMU (96 % fewer); the virtio-block implementation alone is about 1,400 lines.

The security argument follows directly from the exit table of section 3.3: the guest can only hurt the host through the exits it causes, so **the hypervisor's attack surface is its device models**, and the cheapest bug is the one in code that was never written. (This guest also has a balloon, a vsock device and a random-number device that the 2020 paper's list did not include, a reminder that the model keeps growing.)

### 7.4 DMA, the IOMMU and GPUs

A device given to a guest does DMA with *guest-physical* addresses, which are meaningless to the real memory system; the **IOMMU** translates them to host-physical addresses and confines the device to the memory it was assigned, exactly as in [tutorial 25](25_buses_pcie_and_io_mapping.md) section 6. Without an IOMMU, passthrough is unsafe: the device could read any memory. For interrupts, the interrupt-remapping hardware can post a device's MSI-X straight to the guest's vCPU (section 6.2). The same ideas apply to GPUs: [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) section 11 covers vGPU (a virtual GPU per VM, time-sliced or backed by MIG instances) and MIG, which partitions an NVIDIA GPU in hardware into up to seven isolated instances, each with its own SMs, L2 slices and memory controllers, and says what each isolates.

---

## 8. Containers, VMs and microVMs, as the hardware sees them

### 8.1 A container is not a small VM

A **container** is an ordinary process (or tree of processes) on the host kernel with three limits applied by that kernel: **namespaces** (what it can see: its own process IDs, network stack, mount points), **cgroups** (what it can use: CPU, memory, I/O), and filters on what it can call (capabilities, seccomp). There is *one kernel*, and the CPU does not know a container exists. No VM exit, no second-level translation, no virtual device: a system call costs what it always costs (about 110 to 125 ns here), a memory access walks the ordinary 4-level tree, and the container starts in milliseconds because starting a process is all it takes.

The price is the boundary. A VM is separated from its neighbours by the hardware's privilege and translation mechanisms, and its attack surface is the exit handlers and device models. A container is separated by the **kernel's system-call interface**: a bug in any system call, filesystem or driver reachable from the container is a path to the host. Neither is "more secure" in the abstract; they have different surfaces.

### 8.2 The spectrum

| | Process | Container | gVisor-style sandbox | microVM (Firecracker, Kata) | Full VM |
|---|---|---|---|---|---|
| Kernel the code runs on | host | host | a user-space kernel that re-implements system calls on top of the host | its **own** guest kernel | its own guest kernel |
| Boundary enforced by | user/kernel privilege | namespaces, cgroups, seccomp | the user-space kernel plus a very small host-facing interface | **hardware virtualization** and a tiny device model | hardware virtualization and a large device model |
| Exits and 2D walks | none | none | none (but syscalls are intercepted) | yes | yes |
| Start-up | ms | ms | fast (no guest kernel to boot) | about 125 ms (Firecracker) | seconds |
| Overhead per instance | small | small | moderate | a few MB (Firecracker: about 3, under 5) | tens to hundreds of MB (QEMU: about 131 in the Firecracker paper) |

The middle two columns exist because of the outer ones' weaknesses: people wanted container density and speed with a stronger boundary than the shared kernel, and the microVM (one VM per function invocation or per pod) is the answer cloud providers chose. Hardware virtualization is what makes a "VM per request" affordable: the exit cost and the translation cost of this tutorial are the entire reason it is not a hundred times slower.

### 8.3 Virtualization inside virtualization

A **nested** guest is a hypervisor running as a guest, itself hosting guests. The inner (L1) hypervisor executes `vmread`, `vmwrite`, `vmresume` and the rest; they cannot run natively, so each traps to the outer (L0) hypervisor, which emulates VMX for it. One exit of the inner guest (L2) therefore becomes: exit to L0, which forwards it to L1; L1 handles it with a stream of VMX instructions, each its own exit to L0; then a `vmresume` that L0 turns into the actual entry. Ben-Yehuda et al. measured the multiplication: one `cpuid` in the inner guest caused **13 switches between guest mode and root mode**, **11** of them exits taken by L1 itself while it read and wrote the VMCS to handle the one original exit, and the cycle counts quoted in section 3.5 (about 100, 2,600 and 58,000) show the cost. Their headline is that common workloads ran within **6 to 8 %** of single-level virtualization (individual benchmarks were further off: kernbench 14.5 % slower, SPECjbb 7.8 % lower), because most of the time is spent in the guest, not exiting; the exit-heavy cases (I/O, `cpuid`, page-table updates) pay many times over. Later hardware helps: **VMCS shadowing** (introduced with Haswell) lets L1's `vmread`/`vmwrite` run without exits. This guest shows the practical situation: the virtualization extensions are not exposed to it, so it has no `/dev/kvm` and cannot host guests of its own with KVM, which is the gap PVM (section 2.4) was built to fill.

### 8.4 Confidential virtual machines

Everything so far assumes the hypervisor is trusted: it can read and change guest memory and registers by design. In a public cloud the customer may not want to trust the provider's operators or the hypervisor. **Confidential computing** removes the hypervisor from the guest's trust base using the memory controller and the CPU itself. It is a topic for [tutorial 30](30_hardware_security.md) (section 7, where it is compared with SGX and TrustZone); the hardware shape in brief, from the Linux kernel's documentation:

- **AMD SEV** encrypts each VM's memory with a key unique to that VM, so the hypervisor sees only ciphertext. **SEV-ES** extends this to the register state at exits, and **SEV-SNP** adds integrity protection: typed guest pages that the hypervisor cannot silently remap or replay, and attestation reports the guest can show a remote party.
- **Intel TDX** runs the guest as a **trust domain (TD)** managed by a CPU-attested **TDX module** in a new CPU mode (SEAM), entered with the `SEAMCALL` instruction. Private memory is encrypted with keys (KeyIDs) that the host cannot use; the guest decides, with one bit in each page-table entry, whether a page is **private** or **shared** with the host. Shared memory is how the guest talks to the hypervisor, so the guest must treat what it finds there as untrusted input.
- **Arm CCA** gives the same idea a name of its own (realms).

The cost shows up exactly where this tutorial said exits matter. In a TD, MMIO is not allowed to fault to the host with the instruction exposed: an access to a device register raises a virtualization exception (`#VE`) in the guest, whose handler emulates the instruction and issues a controlled call to the host, and port I/O, `HLT`, some MSR and CPUID accesses go the same way. DMA cannot target private memory (the device is not trusted with the keys), so the kernel routes it through a **shared bounce buffer** (SWIOTLB, converted to shared at boot): one extra copy per transfer. So confidential VMs make the exit-heavy paths of this tutorial more expensive and push designs toward virtio with large, batched transfers.

---

## 9. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Memory-heavy code with a large working set is slower in a VM than on bare metal, and the gap grows with the miss rate | two-dimensional walks on TLB misses; 4 KB pages in the guest or in the host | huge pages in the guest **and** host-side backing (ask for it: dedicated hosts, hugepage-backed VMs); check `AnonHugePages` in `/proc/meminfo` |
| Latency spikes of milliseconds on idle vCPUs, no explanation inside the guest | **steal time**: the host ran something else | `st` in `top`, 8th field of the `cpu` line in `/proc/stat`; pin vCPUs, avoid overcommit, use dedicated instances |
| Code that calls `cpuid`, `rdmsr`/`wrmsr` or `in`/`out` in a hot path is 100 times slower | each is an exit (microseconds here, section 3.4) | cache the `cpuid` result at start-up; never poll an MSR |
| Timer-driven or `sleep`-based benchmarks are noisy | timer programming exits; host scheduling of the vCPU | use `CLOCK_MONOTONIC` and many repetitions; read [tutorial 26](26_interrupts_exceptions_and_dma.md) section 6.4 |
| Spin-lock-heavy code scales badly when vCPUs outnumber physical cores | **lock-holder preemption**: the vCPU holding the lock was descheduled and everyone spins | PV spinlocks (check the boot log), fewer vCPUs than cores, sleeping locks |
| `munmap`/`mprotect`-heavy multi-threaded code is slow in a guest | TLB shootdown IPIs exit, and the target vCPU may be preempted | PV TLB flush (bit 9), fewer threads, avoid per-request `mprotect` |
| A VM's network throughput is low and host CPU is high | emulated NIC, or virtio without `vhost`, one queue, no batching | virtio-net with `vhost` and multiple queues, larger rings; for line rate, SR-IOV or vDPA |
| A VM's disk latency is worse than the SSD's | single virtio-blk queue, user-space backend, image-format overhead | multiple queues, an `io_uring` backend, a raw device, polling |
| `perf` shows no hardware counters in a VM | the virtual PMU is not exposed | enable it in the hypervisor, or use software events |
| "Free" memory in the guest but the host is swapping | overcommit and ballooning | reserve memory for latency-critical VMs; check the balloon driver |
| `/dev/kvm` is missing in a cloud VM that must run VMs | no nested virtualization | a nested-enabled or bare-metal instance type, or software virtualization (QEMU emulation, PVM) |
| Wall-clock jumps or timestamps differ after migration or pause | the clock source | `CLOCK_MONOTONIC` for intervals, `chrony`/PTP for wall time, not the raw TSC across hosts |
| A container sandbox is judged "not isolated enough" | it shares the host kernel | a microVM (Kata, Firecracker) or a user-space kernel (gVisor) |
| A confidential VM has poor I/O | `#VE`-emulated MMIO and bounce buffers | virtio with large batched transfers; fewer, bigger I/Os |

---

## 10. Mistakes that cost points

1. **Equating virtualization with emulation.** A guest runs real instructions on the real CPU; only a few events exit.
2. **Saying "every privileged instruction traps to the hypervisor".** In hardware-assisted virtualization most of them do not: the guest kernel runs its own ring 0, and the controls choose what exits.
3. **Forgetting why x86 was hard.** The answer is "sensitive but unprivileged instructions" (seventeen, `POPF` as the example), not "x86 is old".
4. **Calling a VM exit a context switch.** It saves and loads a large part of the CPU state, enters the hypervisor's code, and disturbs caches, branch predictors and sometimes the TLB; and "cost" depends on whether the exit is handled in the kernel or goes out to user space.
5. **Quoting one number for an exit.** About 1,400 cycles for a lightweight round trip, more than 6,000 through a user-space device model, 15,000 TSC ticks (21,000 core cycles) for `cpuid` in this guest, 58,000 nested. Name which.
6. **Saying nested paging slows down all memory accesses.** Only TLB misses pay for the longer walk; a TLB hit costs the same as on bare metal.
7. **Reciting "24 references" with no derivation.** It is `(n + 1)(m + 1) - 1` for `n` guest levels and `m` host levels; give the formula, say it is the worst case, and say walk caches and cached page-table lines shorten it.
8. **Enabling huge pages in one dimension only.** The walk is still long and the TLB entries are still small if the other level uses 4 KB pages.
9. **Treating a container as a lightweight VM.** There is one kernel and no hardware boundary.
10. **Mixing up virtio and SR-IOV, or forgetting the IOMMU.** virtio is shared-memory queues served by the host; an SR-IOV VF is hardware with its own queues; passthrough is only safe with an IOMMU.
11. **Calling paravirtualization obsolete.** `kvm-clock`, PV EOI, PV IPIs, PV spinlocks and virtio are paravirtualization, present in every modern Linux guest.
12. **Forgetting the quiet exits.** `HLT`, timer programming, IPIs and EOIs are what an *idle* VM spends its time on.
13. **Trusting timings and benchmarks taken in a VM without looking at steal time.**
14. **Saying a hypervisor can read a confidential VM's memory.** That is exactly what SEV and TDX prevent, at the price of bounce buffers and emulated MMIO.
15. **Claiming hardware virtualization was faster from day one.** The first generation lost to binary translation on page-table-heavy workloads; two changes (cheaper exits, nested paging) fixed that.

---

## 11. Questions you will be asked

**Q1. Why is virtualizing a CPU hard, and what are the requirements?** The monitor must give a guest operating system the illusion that it owns the machine while keeping control of the real one. Popek and Goldberg's conditions are **equivalence** (the guest behaves as it would on real hardware, apart from timing), **resource control** (the monitor stays in charge of every resource) and **efficiency** (most guest instructions run directly on the CPU). The classic method is trap-and-emulate: run the guest kernel de-privileged, let it execute normally, and trap on anything sensitive. It works only if every *sensitive* instruction (one that reads or changes the machine's privileged state) is also *privileged* (it faults when run in user mode). On the Pentium-era x86 that was false.

**Q2. Why couldn't the original x86 be virtualized by trap-and-emulate?** Robin and Irvine counted **seventeen** instructions that are sensitive but not privileged, among them `SGDT`, `SIDT`, `SLDT`, `SMSW`, `STR`, `PUSHF`/`POPF`, `LAR`, `LSL`, `VERR`, `VERW`, `MOV` from a segment register and some far control transfers. The worst is `POPF`: run in user mode it does not fault, it silently ignores an attempt to change the interrupt flag, so a de-privileged guest kernel that executes `cli`-like logic with `popf` believes it changed something that did not change. This tutorial measured it: asked for flags `0x446` (IF cleared), a user-mode `popf` left IF set and raised no fault. Three workarounds followed: **binary translation** (rewrite the guest's kernel code on the fly, VMware), **paravirtualization** (change the guest to call the monitor, Xen), and **hardware assist** (Intel VT-x, 2005, and AMD-V, 2006).

**Q3. What does VT-x actually add?** A second dimension of privilege: **VMX root** operation for the hypervisor and **VMX non-root** for the guest, each with its own rings, so the guest kernel runs in its own ring 0. The state of a guest and the rules for leaving non-root mode live in a per-vCPU memory structure, the **VMCS** (guest-state area, host-state area, execution controls, exit and entry controls, exit information). `VMLAUNCH`/`VMRESUME` enter the guest; the hardware loads guest state, runs until something the controls say to intercept happens, then performs a **VM exit**: it saves guest state, records the reason, loads host state and resumes the hypervisor at a fixed address. AMD-V is the same idea with a memory block (the VMCB) and `VMRUN`. The hypervisor loop is: enter, run until exit, read the reason, emulate or fix, enter again.

**Q4. What causes a VM exit, and how do you reduce them?** Unconditional causes include `cpuid`, `vmcall`, `xsetbv` and EPT violations. Configurable ones include I/O instructions, MSR accesses (a bitmap chooses), `hlt`, external interrupts, control-register accesses (not with EPT), `pause` loops and APIC accesses. The reduction is by design: nested paging so page faults and `CR3` writes do not exit; TSC offsetting so `rdtsc` does not exit; APIC virtualization and posted interrupts so interrupts and EOIs do not exit; paravirtual interfaces (`kvm-clock`, PV EOI, PV IPIs); handling exits in the kernel instead of user space (`ioeventfd`, `vhost`); batching through virtqueues; and passthrough or SR-IOV so I/O needs none at all. The question to ask of any slow VM is "what is it exiting for, and how often?" (`perf kvm stat` on the host counts exits by reason).

**Q5. How expensive is an exit?** Name the tier. A minimal round trip (hypercall that does nothing) is about **1,400 cycles** (0.7 µs) on a Xeon of that era; an exit handled entirely inside the kernel (a virtio kick through `ioeventfd`) about **2,600**; a virtual IPI about **3,100**; an exit that must go out to a user-space device model and back about **6,700** (3.2 µs). Those are Dall et al.'s measurements on x86; on Arm without VHE the same hypercall cost 6,413 cycles and with the EL2 optimisation 752. A nested `cpuid` was about 58,000 cycles in the Turtles paper. And in this tutorial's guest, a `cpuid` took 7 µs, about 15,000 TSC ticks (21,000 core cycles), which is about 60 system calls. The cost is not only the cycles: the exit pollutes caches and predictors and changes what the next instructions find.

**Q6. Shadow page tables versus nested paging?** With **shadow paging** the hypervisor builds a table mapping guest-virtual straight to host-physical addresses and runs the guest on it. A TLB miss walks one tree (4 reads), but the hypervisor must see every guest page-table update (it write-protects those pages, so each update exits) and every `CR3` load and `INVLPG`. Process creation, `fork` and `mmap` heavy workloads suffer badly (Adams and Agesen's `forkwait`: 6 s native, 106 s on the first hardware VMM). With **nested paging** (EPT, NPT, stage 2) the guest manages its own tables with no exits and the hardware walks both trees, at the price of a longer TLB miss. Everything since about 2008 uses nested paging; shadow paging survives in PVM, in software hypervisors and in some nested cases.

**Q7. Why is a nested page walk 24 memory references, and how do you shorten it?** Each of the guest's 4 levels needs its table's *guest-physical* address translated through the host's 4 levels (4 reads) and then the entry itself read (1): `4 × 5 = 20`. The final data address needs its own 4 reads: 24. In general `(n + 1)(m + 1) - 1` for `n` and `m` levels. To shorten it: **huge pages in both dimensions** (4 KB guest on 2 MB host: 19; 2 MB on 2 MB: 15; 1 GB on 1 GB: 8), **walk caches** that skip the upper levels, **big TLBs** and a hit that holds the combined translation, and a cache hierarchy that keeps the page-table lines close. Only TLB misses pay; hits cost what they cost natively.

**Q8. What are VPID, ASID and VMID for?** Tags on TLB entries that name the owner (VPID on Intel, ASID on AMD, VMID on Arm and RISC-V), so that a VM entry or exit does not have to flush the TLB. Without them every switch between guest and host, or between two guests, would throw away translations that are still valid. They are the virtual-machine analogue of the PCID/ASID that lets the OS skip flushes on a process switch ([tutorial 15](15_virtual_memory_and_the_tlb.md)).

**Q9. How are interrupts virtualized?** The slow path is an exit on the physical interrupt, the hypervisor deciding which vCPU it belongs to, injecting a virtual interrupt on the next entry, and then trapping the guest's EOI write. Hardware removes most of those exits. **APICv** (Intel) and **AVIC** (AMD) give the guest a virtual APIC page the CPU updates itself: virtual interrupt delivery and EOI virtualization run without an exit. **Posted interrupts** let a sender (another core, or an assigned device through the IOMMU's interrupt remapping) set a bit in a per-vCPU descriptor and send one notification vector; the CPU merges it into the running guest with no exit. On **Arm** the GIC's virtual CPU interface and **list registers** (`ICH_LR<n>_EL2`) hold interrupts waiting for the guest, which acknowledges and ends them without trapping, and GICv4 can inject some interrupts directly. Still exiting: IPIs on older hardware (writing the ICR), timer programming, and `HLT`.

**Q10. What is paravirtualization today?** Not a modified OS but a set of optional shortcuts the guest discovers through the hypervisor's CPUID leaves: `kvm-clock` (time read from shared memory), PV EOI, PV IPIs (one hypercall for many targets), PV TLB flush, PV spinlocks and sched-yield (for lock-holder preemption), steal time, and above all **virtio** devices. This guest's `dmesg` shows them being enabled and CPUID leaf `0x40000001` returns the bit mask `0x01007efb`. The principle is that telling the host something beats doing something privileged that has to be intercepted.

**Q11. virtio, vhost, SR-IOV or passthrough for a VM's NIC?** **Emulation** of a real chip: universal and slow (many user-space exits per packet). **virtio**: a shared-queue protocol with the host doing the work; one notification per batch; easy migration. **vhost** (kernel backend) or **vhost-user** (a polling user-space backend such as a DPDK vswitch): fewer exits and copies, at the cost of host CPU. **SR-IOV VF or passthrough**: the guest drives hardware directly, the IOMMU confines DMA; near-native speed, but live migration is hard and VF counts are limited. **vDPA**: hardware that speaks virtio. Choose by what must be true: migration and density (virtio), line rate and low latency (VF or vDPA), compatibility with an unknown guest (emulation).

**Q12. Containers, VMs and microVMs?** A container is a process with namespaces, cgroups and seccomp on the host's kernel: no exits, no second translation, start-up in milliseconds, but a shared-kernel attack surface. A VM adds its own kernel behind the hardware boundary, at the cost of exits, nested walks and memory for a second kernel. A **microVM** (Firecracker, Kata) is a VM stripped to a few virtio devices and a minimal VMM: about 125 ms to boot, a few MB overhead, about 50,000 lines of VMM against more than 1.4 million for QEMU, which is how serverless platforms get a VM per tenant function. gVisor sits between, with a user-space kernel in front of the host's.

**Q13. How does Arm's virtualization differ from x86's?** Arm has a separate exception level for the hypervisor, **EL2**, above the guest kernel's EL1, with trap controls in `HCR_EL2` and stage 2 translation set by `VTTBR_EL2`; there is no `VMCS` and state switching is done by software, which is why a hypercall cost 6,413 cycles on Arm in Dall et al. before **VHE** (the Virtualization Host Extensions) let the host kernel run in EL2 itself, removing most of the switch (their EL2 optimisation measured 752). On x86 the host kernel runs in VMX root with full rings, so no equivalent problem arose, and the VMCS makes the hardware save and restore state. RISC-V's hypervisor extension adds a virtualization bit V and HS, VS and VU modes with `vsatp` and `hgatp` registers, delegating traps with `hedeleg` and `hideleg`.

**Q14. What is nested virtualization and why is it slow?** A hypervisor running as a guest. Its `vmread`/`vmwrite`/`vmresume` instructions cannot run natively, so the outer hypervisor (L0) traps and emulates them, and one exit of the inner guest (L2) becomes many (13 mode switches for one `cpuid` in the Turtles paper, 11 of them from L1's VMCS accesses). The cost is concentrated in exit-heavy work; compute-bound code runs near single-level speed. VMCS shadowing, in hardware since Haswell, removes the vmread/vmwrite exits. Alibaba and Ant Group's PVM avoids the need for nested hardware support altogether, with a guest kernel in ring 3 and shadow page tables (this guest's boot log mentions it, and says `/dev/kvm` is not registered).

**Q15. What is a confidential VM?** One whose memory and registers are protected from the hypervisor that runs it. **AMD SEV** gives each VM its own memory-encryption key; SEV-ES encrypts register state at exits and SEV-SNP adds integrity (the hypervisor cannot remap or replay guest pages unnoticed) and attestation. **Intel TDX** runs the guest as a trust domain under a module entered by `SEAMCALL`, with private memory encrypted under a key the host cannot use. The costs follow from the model: no host-visible device registers (MMIO and port I/O become `#VE` in the guest, which asks the host explicitly), DMA through shared bounce buffers (an extra copy), and no host inspection of guest memory.

**Q16. How are vCPUs scheduled, and what goes wrong?** A vCPU is a host thread. If the host has more runnable threads than cores, a vCPU waits, and the guest sees only **steal time** (the 8th field of the `cpu` line in `/proc/stat`). The guest also cannot know which of its vCPUs is descheduled: if the one holding a spin lock was preempted, the others spin ("**lock-holder preemption**"), and a TLB-shootdown IPI waits for a vCPU that is not running. The mitigations are paravirtual: PV spinlocks (halt instead of spin), PV TLB flush, PV sched-yield, pause-loop exiting on the host, and on the operator's side, pinning and avoiding overcommit for latency-critical VMs.

**Q17. A service is 20 % slower after moving to a VM. How do you investigate?** First is it real: repeat, and read steal time (`top`'s `st`). Then walk the list: **memory** (are huge pages used in the guest *and* backed by huge pages on the host? check `AnonHugePages` in the guest, ask the operator about the host; compare TLB misses with `perf stat` if the PMU is exposed); **exits** (`perf kvm stat` on the host: are `cpuid`, MSR, HLT, EPT-violation or I/O exits high? did the application call `cpuid` or read an MSR in a loop?); **I/O** (emulated device, virtio without `vhost`, a single queue?); **CPU model** (are the instruction-set flags the host has exposed to the guest? a "generic" CPU model can hide instruction-set extensions such as AVX-512); **NUMA** (are vCPUs and memory on the same node?); **time** (is the clock source the cheap one?); **neighbours**. Then change one thing at a time.

---

## 12. Problems

**How to use this section.** Three are paper problems, one uses a real machine; each says *Where* to check the answer and *How*. The checks need a calculator or a few lines of Python. Do the paper part first, then check, and fix the paper answer if they disagree. Time: about 20 to 30 minutes each.

### Problem A (paper, then a script): the two-dimensional walk

Use the formula of section 5.3: `n` guest levels and `m` host levels cost `(n + 1)(m + 1) - 1` references.

(a) Derive the formula by counting for `n = 4`, `m = 4`, then give the number of references for: bare metal, 4 KB guest pages on 4 KB host pages, 4 KB guest on 2 MB host, 2 MB guest on 4 KB host, 2 MB on 2 MB, 1 GB on 1 GB (assume a huge page ends the walk one level early).
(b) A program takes **1 million TLB misses a second**. Assume each page-table reference costs 4 ns (an L2 hit). How much time per second goes to walks on bare metal and under 4 KB/4 KB nested paging? By what percentage does a 1-second run get longer? Repeat for 15 ns per reference (an L3 hit).
(c) A guest turns on transparent huge pages, but the host backs the VM with 4 KB pages. How many references does a walk need? How much memory does a 1,536-entry TLB cover when entries are 4 KB, and when the guest uses 2 MB pages *and* the host does too? Why does the first case not improve?
(d) Gandhi et al. report address-translation overhead of 28 % natively and 113 % virtualized on a graph workload. What is the ratio? Which of the changes in (a) would you ask the cloud provider for first?

*Where:* paper; check with a calculator or fifteen lines of Python.
*How:* (a) per guest level you translate one address (`m` reads) and read one entry (1); at the end one more translation (`m`). (b) `time = misses × references × ns`. (c) the smaller page size wins in the combined TLB entry. (d) `113 ÷ 28`.

### Problem B (paper): the exit budget

Use these exit costs at 2.1 GHz (Dall et al., x86): hypercall 1,437 cycles, I/O handled in kernel 2,565, virtual IPI 3,102, I/O handled in user space 6,732. A VM receives and sends **200,000 packets a second**.

(a) Convert the four costs to microseconds.
(b) An emulated NIC needs 5 user-space I/O exits per packet. How many cores' worth of time does the hypervisor spend on exits?
(c) A virtio NIC without batching costs one kick (kernel-handled I/O) and one interrupt delivery (cost it as a virtual IPI) per packet. How many cores? And with batches of 16 packets per kick and per interrupt?
(d) An SR-IOV VF with posted interrupts needs no exits on the data path. What does the guest still pay for?
(e) Repeat (b) and (c) if an exit costs 7 µs, as the `cpuid` did in this guest. At what packet rate does each option saturate one core?

*Where:* paper; check with the ten-line script in the Answers.
*How:* `cores = packets per second × microseconds per packet × 1e-6`. The interrupt-as-IPI costing is an assumption; state it.

### Problem C (paper): who exits?

For each instruction say what happens (1) in a guest kernel de-privileged under pre-VT trap-and-emulate, and (2) in a VT-x guest with EPT and typical KVM settings: `hlt`, `popf` (clearing IF), `sgdt`, `cpuid`, `mov cr3`, `rdtsc`, `in`/`out`, `syscall`, `invlpg`, `wrmsr`, `pause`, `rdrand`. Then say which of these you could time from inside a guest to find the exits, and what you expect.

*Where:* paper; check against section 3.4's table, Problem D, and the exit-reason list of section 3.3.
*How:* ask two questions of each: "is it privileged (faults in ring 3)?" and "does a VM-execution control make it exit?" Remember which exits are unconditional.

### Problem D (on your machine): is this a VM, and what does it cost?

Use any Linux machine, physical or virtual. Root is needed only for `dmesg`.

(a) Detect: `systemd-detect-virt --vm`, `lscpu | grep -i -E 'hypervisor|virtualization'`, `grep -c hypervisor /proc/cpuinfo`, `ls -l /dev/kvm`, `dmesg | grep -i -E 'hypervisor|kvm'`. Which hypervisor, and does the kernel say it is using paravirtual features?
(b) Compile the CPUID program of section 6.3. What vendor string and feature mask does it print? Decode the mask with the table there.
(c) Compile the timing program in the Answers (`vmtime.c`) and run it pinned (`taskset -c 0 ./vmtime`). How many nanoseconds and cycles is `cpuid` compared with `getppid` and `rdtsc`? What does the ratio say about the cost of one exit?
(d) Steal time: read the `cpu` line of `/proc/stat` (the 8th number is steal, in 10 ms ticks), compute its share of the total, then run `taskset -c 0 timeout 5 sh -c 'while :; do :; done'` and `taskset -c 1 ...` together and read it again. Compare with `top`'s `st`.
(e) Which virtio devices does this machine have? `for d in /sys/bus/virtio/devices/*; do echo $(basename $d) $(cat $d/device) $(basename $(readlink $d/driver)); done` and `grep virtio /proc/interrupts`. How many vectors does each use?
(f) On a KVM *host* (skip otherwise): `cat /sys/module/kvm_intel/parameters/{ept,vpid,enable_apicv,nested}` (or `kvm_amd`: `npt`, `avic`, `nested`), and `grep -o -w -E 'ept|vpid|vnmi|flexpriority|apicv' /proc/cpuinfo | sort | uniq -c`.

*Where:* your own machine or VM.
*How:* compare with the VM's answers in the Answers section. A container may hide `dmesg`, and some clouds hide parts of CPUID.

---

## Answers

### Problem A

(a) Four guest levels. To read the top-level entry the walker must first translate the guest-physical address of that table through the host's four levels (4 reads) and then read the entry (1): 5 per level, so 20 for four levels, and the data address needs 4 more: **24**. Per guest level it is `m + 1`, plus `m` at the end, so `n(m + 1) + m = (n + 1)(m + 1) - 1`. A huge page ends a walk one level early, so a 4 KB page uses 4 levels, a 2 MB page 3, a 1 GB page 2.

| Case | n | m | References |
|---|---|---|---|
| bare metal | 4 | none | 4 |
| 4 KB guest, 4 KB host | 4 | 4 | **24** |
| 4 KB guest, 2 MB host | 4 | 3 | 19 |
| 2 MB guest, 4 KB host | 3 | 4 | 19 |
| 2 MB guest, 2 MB host | 3 | 3 | 15 |
| 1 GB guest, 1 GB host | 2 | 2 | 8 |

(b) At 4 ns per reference a walk takes 16 ns natively and 96 ns nested. At 1 million misses a second: **16 ms** against **96 ms** per second, so a 1-second run grows by 80 ms, **8 %**. At 15 ns per reference: 60 ns against 360 ns, 60 ms against 360 ms, an extra 300 ms: **30 %**. The walk is six times longer in both cases; how much that matters depends on how much the cache absorbs and how many misses the program takes.

(c) A guest that uses 2 MB pages on a 4 KB-backed host sees `n = 3`, `m = 4`: **19 references**, no better than 4 KB guest pages on 2 MB host pages. More important is the **TLB**: it caches the combined translation, and a combined entry can only be as large as the smaller of the two page sizes, so every entry covers 4 KB: reach `1,536 × 4 KB = 6 MiB`. With 2 MB pages in both dimensions the reach is `1,536 × 2 MB = 3 GiB`. The guest's huge pages are *splintered* by the host's small ones; the benefit appears only when both levels are large (and the host memory is contiguous enough to allow it).

(d) `113 ÷ 28 ≈ 4.0`: the virtualized overhead was about four times as large. Ask first for **host-side huge page backing** (and large pages in the guest for the big data structures): it cuts the host dimension from `m = 4` to `m = 3` or `2` and keeps TLB entries large; the 1 GB case would reduce 24 to 8 references.

```python
def refs(n, m): return (n + 1) * (m + 1) - 1
for name, n, m in [("4K/4K", 4, 4), ("4K on 2M", 4, 3), ("2M on 4K", 3, 4), ("2M/2M", 3, 3), ("1G/1G", 2, 2)]:
    print(name, refs(n, m))
for t in (4, 15):
    nat, nes = 4 * t, 24 * t
    print(t, "ns/ref:", nat, nes, "ns; extra", 1e6 * (nes - nat) * 1e-9 * 100, "% of a second")
print(1536 * 4 / 1024, "MiB;", 1536 * 2 / 1024, "GiB;", 113 / 28)
```

### Problem B

(a) `cycles ÷ 2,100` ns: hypercall **0.68 µs**, kernel I/O **1.22 µs**, virtual IPI **1.48 µs**, user-space I/O **3.21 µs**.

(b) `200,000 × 5 × 3.21 µs = 3.21` seconds of exit time per second: **3.2 cores**, before the guest does any work.

(c) Unbatched virtio: `1.22 + 1.48 = 2.70 µs` per packet, `200,000 × 2.70 µs =` **0.54 core**. With batches of 16: `2.70 ÷ 16 = 0.169 µs` per packet, **0.034 core** (about 3 %).

(d) Almost nothing on the exit side: no kicks (the guest writes a doorbell in device memory mapped straight into it), no interrupt exits (posted). It still pays the device driver's own work, the guest's interrupt handling, the **two-dimensional walks** on TLB misses and the IOMMU's translation of the device's DMA ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 6). Also paid in flexibility: no easy migration.

(e) At 7 µs per exit: emulated `200,000 × 5 × 7 µs =` **7 cores**; unbatched virtio `2 × 7 µs = 14 µs` per packet, **2.8 cores**; batched by 16, **0.175 core**. Saturation of one core at the published costs: emulated `1 ÷ (5 × 3.21 µs) =` **62 kpps**, unbatched virtio `1 ÷ 2.70 µs =` **371 kpps**, batched by 16 about **5.9 Mpps**. The lesson in both columns is the same: the number of exits per packet matters more than the cost of each, and batching changes it by a factor of the batch size. The 7 µs case also shows why you cannot import a cycle count from a paper without knowing the machine.

```python
us = {k: c / 2100 for k, c in dict(hyper=1437, io_kernel=2565, ipi=3102, io_user=6732).items()}
pps = 200_000
print({k: round(v, 3) for k, v in us.items()})
for e in (None, 7.0):
    f = lambda k: e if e else us[k]
    print(pps * 5 * f("io_user") * 1e-6,
          pps * (f("io_kernel") + f("ipi")) * 1e-6,
          pps * (f("io_kernel") + f("ipi")) / 16 * 1e-6)
```

### Problem C

| Instruction | Pre-VT trap-and-emulate (guest kernel in ring 3) | VT-x + EPT, typical KVM |
|---|---|---|
| `hlt` | privileged: faults, the monitor emulates and runs something else | exits (HLT exit, reason 12); the host deschedules the vCPU, perhaps after polling |
| `popf` (clear IF) | **sensitive but unprivileged**: no fault, IF silently unchanged: the hole | no exit: the guest kernel runs in its own ring 0 and changes its own virtual IF |
| `sgdt` | **sensitive but unprivileged**: no fault, returns the real GDT register | no exit: the GDTR is part of the guest state in the VMCS (UMIP, where present, hides it from user mode) |
| `cpuid` | unprivileged: runs natively and returns the host's answer | **always exits** (unconditional): the hypervisor decides what the guest learns |
| `mov cr3` | privileged: faults (the shadow table must be switched) | no exit with EPT (the CR3-load control is off) |
| `rdtsc` | runs natively (unless `CR4.TSD`), returns the host counter | no exit: TSC offsetting (and scaling) in hardware; measured 16 to 19 ns |
| `in` / `out` | privileged (the I/O permission check faults): emulated | exits (I/O instruction exit, reason 30); handled in kernel or user space |
| `syscall` | enters the monitor's entry point, not the guest kernel's; the monitor has to catch it and forward it | no exit: the guest's own entry point; measured 108 to 127 ns |
| `invlpg` | privileged: faults (shadow table update) | no exit with EPT |
| `wrmsr` | privileged: faults | exits unless the MSR bitmap passes that MSR through; many do exit, a few hot ones (e.g. `FS_BASE`) do not; x2APIC ICR writes exit unless IPI virtualization exists |
| `pause` | unprivileged: runs | no exit normally (measured 12 ns); exits only if pause-loop exiting sees a tight spin |
| `rdrand` | does not exist yet | no exit unless the hypervisor asks for it (measured 34 ns) |

To find the exits from inside a guest, time the unprivileged ones: `cpuid` should be microseconds, while `rdtsc`, `syscall`, `pause`, `rdrand` should be nanoseconds, as in section 3.4. Privileged ones (`hlt`, `in`, `wrmsr`) cannot be timed from user mode. You must be careful about `rdseed`: it was slow here (about 800 ns) but I cannot tell whether that is an exit or the hardware waiting for entropy.

### Problem D

Results from this tutorial's guest:

(a) `systemd-detect-virt --vm` prints `kvm` (without `--vm` it printed `docker`: the shell here is inside a container, and the plain command reports the container first). `lscpu` says `Hypervisor vendor: KVM` and `Virtualization type: full`. `/proc/cpuinfo` has the `hypervisor` flag on both CPUs and also `umip`, and no `vmx` or `svm`. `/dev/kvm` does not exist. `dmesg` shows the lines of section 6.3 (`Hypervisor detected: KVM`, `kvm-clock`, PV EOI, PV IPIs, PV spinlocks, PV TLB flush) and the PVM message that `/dev/kvm` was not registered because the kernel command line lacks `nested_pvm`.

(b) Hypervisor bit 1, vendor `KVMKVMKVM`, maximum leaf `0x40000001`, features `0x01007efb`. Decoded: bits 0, 1, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14 and 24 (clock source, no-op I/O delay, async page faults, steal time, PV EOI, PV unhalt, PV TLB flush, PV IPIs, poll control, sched yield, clock-source-stable).

(c) The program (it prints ns per operation; run it three times):

```c
// vmtime.c: gcc -O2 vmtime.c -o vmtime && taskset -c 0 ./vmtime
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <x86intrin.h>
#include <cpuid.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
#define BENCH(name, N, body) do { double t0 = now(); for (long i = 0; i < (N); i++) { body; } \
    printf("%-14s %9.1f ns\n", name, (now() - t0) / (N)); } while (0)
int main(void) {
    volatile unsigned sink = 0; unsigned a, b, c, d;
    BENCH("empty loop", 50000000, __asm__ volatile(""));
    BENCH("rdtsc",      10000000, sink += (unsigned)__rdtsc());
    BENCH("getppid",     5000000, syscall(SYS_getppid));
    BENCH("cpuid(0)",     200000, __cpuid(0, a, b, c, d));
    return 0;
}
```

Three runs here: empty loop 0.2 to 0.3 ns, `rdtsc` 15.7 to 18.9 ns, `getppid` 109 to 118 ns, `cpuid` **7.2 to 8.0 µs**, so about 15,000 to 17,000 TSC ticks (time × 2.1 GHz; about 21,000 to 24,000 core cycles at 3 GHz, [tutorial 28](28_performance_and_power.md) section 2.2) and about 65 system calls. The Turtles paper measured about 100 cycles for `cpuid` on bare metal and 2,600 in a VM, so this guest's exit is slower than that VM's by a large factor; the point is the ratio to `getppid`, and the fact that `rdtsc` does not budge.

(d) At the time of measurement `/proc/stat` showed `cpu 5432 0 1262 245751 695 0 224 225 ...` (user, nice, system, idle, iowait, irq, softirq, **steal**), 225 of 253,589 ticks, **0.09 %**, and `top` showed 0.0 `st`. Earlier, with both vCPUs spinning for 5 s, the steal counter rose by 12 ticks of 1,000, 1.2 %. Both are the numbers of an unloaded host.

(e) Ten virtio devices: `virtio0` balloon (ID 0x0005, 5 vectors), `virtio1` to `virtio6` block (0x0002, 2 vectors each), `virtio7` net (0x0001, 3 vectors), `virtio8` vsock (0x0013, 4 vectors) and `virtio9` rng (0x0004, 2 vectors). Each queue has its own MSI-X vector ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 4), so completions can be steered to different CPUs.

(f) Not possible here: there is no `kvm_intel` or `kvm_amd` module and no `vmx`. On a modern Intel host `ept`, `vpid` and `enable_apicv` normally read `Y`; `nested` depends on how the host is configured; if `ept` read `N`, KVM would be using shadow paging (section 5.2).

---

## Cheat sheet

- **Why virtualize:** isolation, consolidation, mobility (snapshot, migration), a clean unit of deployment. Popek and Goldberg: equivalence, resource control, efficiency; trap-and-emulate needs every sensitive instruction to be privileged.
- **x86 problem:** 17 sensitive-but-unprivileged instructions (`SGDT`, `SIDT`, `SLDT`, `SMSW`, `STR`, `POPF`, `LAR`, `LSL`, `VERR`, `VERW`, segment moves, far control transfers); `POPF` fails silently. UMIP (a bit in CR4) now closes five of them for user mode. Remedies: binary translation, paravirtualization, hardware assist (VT-x 2005, AMD-V 2006).
- **VT-x / AMD-V:** VMX root and non-root; VMCS (guest state, host state, controls, exit information) per vCPU, `VMLAUNCH`/`VMRESUME`; AMD VMCB and `VMRUN`; exit reasons: 10 CPUID, 12 HLT, 30 I/O, 31/32 RDMSR/WRMSR, 48 EPT violation, 49 EPT misconfiguration, 1 external interrupt, 18 VMCALL.
- **Exit cost:** about 1,400 cycles (hypercall), 2,600 (kernel-handled I/O), 3,100 (virtual IPI), 6,700 (user-space I/O) on x86; Arm without VHE 6,400 (hypercall), with EL2 optimisation 750; nested `cpuid` 58,000; this guest 15,000 TSC ticks (21,000 core cycles) for `cpuid`. Entry alone: 2,409 cycles (Pentium 4), 937 (Core).
- **First generation lost:** `forkwait` 6.02 s native, 36.95 s binary translation, 106.4 s first hardware VMM; fixed by cheaper exits and nested paging.
- **Nested paging:** EPT (Intel 2008), NPT/RVI (AMD), stage 2 (Arm), G-stage (RISC-V). Walk = `(n + 1)(m + 1) - 1` references: 24 for 4 and 4 levels, 19 (4 KB on 2 MB), 15 (2 MB on 2 MB), 8 (1 GB on 1 GB), 35 (5 and 5). Only TLB misses pay. Huge pages in both dimensions; the TLB entry is the smaller page size.
- **Tags:** VPID (Intel), ASID (AMD), VMID (Arm, RISC-V) so a switch need not flush the TLB.
- **Arm:** EL2 hypervisor, `HCR_EL2` traps, `VTTBR_EL2` stage 2 with VMID, `HPFAR_EL2` for the faulting guest-physical address, VHE (`E2H`) lets the host kernel live in EL2; GIC virtual CPU interface with list registers; `CNTVOFF_EL2` offsets the virtual counter. **RISC-V:** H extension, V bit, HS/VS/VU, `hgatp`/`vsatp`, `hedeleg`/`hideleg`/`hvip`, guest page faults (causes 20, 21, 23), virtual instruction 22.
- **Interrupts and time:** APICv/AVIC (virtual APIC page, virtual interrupt delivery, EOI virtualization), posted interrupts, GIC list registers, GICv4 direct injection; TSC offset and scaling; `kvm-clock`; steal time (field 8 of `/proc/stat`, `st` in `top`); lock-holder preemption and PV spinlocks; halt polling and POLL_CONTROL.
- **Paravirtual features (CPUID `0x40000001`):** clock source, async page fault, steal time, PV EOI, PV unhalt, PV TLB flush, PV send IPI, poll control, PV sched yield. This guest: `0x01007efb`.
- **Devices:** emulation (many user-space exits), virtio (shared queues, a kick per batch), `vhost` (kernel backend; `ioeventfd`/`irqfd`), vhost-user (polling), SR-IOV VF or passthrough (IOMMU, hard to migrate), vDPA (hardware virtio). A virtio kick: 1.2 µs in kernel, 3.2 µs through user space.
- **microVM:** Firecracker: under 125 ms boot, about 3 MB overhead per VM (under 5 MB), 150 per second per host, about 50,000 lines of Rust against QEMU's 1.4 million, a handful of devices (this guest: balloon, block, net, vsock, rng, serial).
- **Containers:** one kernel, namespaces plus cgroups plus seccomp, no exits, no second walk, shared-kernel attack surface. VM: its own kernel behind the hardware boundary. gVisor: user-space kernel.
- **Nested and confidential:** nesting multiplies exits (13 mode switches for one `cpuid`), within 6 to 8 % overall in the Turtles abstract; VMCS shadowing helps. SEV (per-VM key), SEV-ES (registers), SEV-SNP (integrity, attestation); TDX (trust domain, `SEAMCALL`, private/shared bit, `#VE` for MMIO, bounce buffers).
- **Diagnose a slow VM:** steal time, huge pages in guest and host, `perf kvm stat` exit counts, device model and queues, CPU model flags, NUMA, clock source, neighbours.
- **Everything here came from real output of this VM (`dmesg`, CPUID leaves, `/sys`, `/proc`), short programs timed in this VM (numbers that include slow hypervisor exits, so read them as ratios), published papers and kernel documentation for the host-side costs, and scripts for the arithmetic; nothing was run on a hypervisor host, because this guest has no `vmx` and no `/dev/kvm`, PVM was not booted, and the Arm and RISC-V material comes from the architecture documents, the Linux headers and the papers, not from running those machines.**

**Next:** tutorial 28, Performance and power (the iron law and IPC, why MIPS and FLOPS mislead, benchmarks and the geometric mean, microbenchmarking pitfalls, Amdahl and Gustafson, latency against throughput and tail latency, Little's law, roofline, CPI stacks and top-down analysis, `perf`, dynamic and static power, DVFS and C-states). Say "next" when you are ready.
