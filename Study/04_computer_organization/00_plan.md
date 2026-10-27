# Computer Organization and Architecture: Plan

Every performance question, OS question, driver question and GPU question sits on top of how a CPU executes instructions and how memory feeds it. This track starts from "what is a computer" and builds up, one tutorial at a time, from logic gates to out-of-order cores, caches, virtual memory, DRAM, multicore, GPUs, accelerators and virtualization, so that "why is this slow?" becomes arithmetic instead of guessing.

**Language:** C (and some assembly) · **Size:** 31 tutorials in 5 parts · **Time:** about 3 hours per Core tutorial, 1 to 2 hours per Skim or Optional one (about 70 hours for the Core path, about 83 for everything) · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain.
3. Ends with about four problems. Each says **where to solve it** (paper, godbolt.org, your own terminal) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.

Nothing to install beyond a C compiler and Python. Where a tutorial measured something, the short program that produced the number is listed in the tutorial with its output, and the problems tell you which parts are paper-only and which you can run yourself.

---

## Tutorials

*Priority* tells you what to cut if time is short. **Core** is asked often in systems interviews. **Skim** is good background you can read quickly. **Optional** is for completeness (exam-style or specialised material).

### Part 1: Foundations (logic and numbers)

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 01 | [`01_how_a_computer_works.md`](01_how_a_computer_works.md) | CPU, memory and I/O; fetch-decode-execute on a toy machine; real assembly for the same ideas; privilege modes and interrupts (preview); the iron law; the latency ladder; ISA vs microarchitecture | Core | Written |
| 02 | [`02_data_representation.md`](02_data_representation.md) | Binary, octal, hex, BCD, Gray code; unsigned, sign-magnitude, 1's and 2's complement; sign extension, overflow vs carry, shifts; endianness; fixed-point; IEEE-754 (half, single, double; normalisation, subnormals, NaN, infinity, rounding modes); ASCII and UTF-8; alignment and struct padding | Core | Written |
| 03 | [`03_boolean_logic_and_combinational_circuits.md`](03_boolean_logic_and_combinational_circuits.md) | Gates (AND, OR, NOT, NAND, NOR, XOR), universal gates, Boolean algebra, De Morgan; SOP and POS; Karnaugh maps (4 to 5 variables), don't-cares, Quine-McCluskey in a paragraph; multiplexers, demultiplexers, decoders, encoders, priority encoders, comparators; half and full adders, ripple-carry adder; a first ALU; transistors (CMOS) and gate delay in a page | Skim | Written |
| 04 | [`04_sequential_logic_and_state_machines.md`](04_sequential_logic_and_state_machines.md) | Clocks, level vs edge triggering; SR, D, JK and T latches and flip-flops; setup and hold time, clock skew, metastability; registers and shift registers (SISO, SIPO, PISO, PIPO); synchronous and asynchronous counters; Mealy and Moore finite state machines; a read-only taste of Verilog; crossing clock domains (the failure rate of a synchroniser, why a bus cannot be synchronised bit by bit, Gray-coded pointers, asynchronous FIFOs and their sizing, handshakes, reset synchronisers, a torn read of a 64-bit counter) | Skim | Written |
| 05 | [`05_computer_arithmetic_and_the_alu.md`](05_computer_arithmetic_and_the_alu.md) | What every arithmetic operation costs (measured latency and throughput); fast adders (generate and propagate, parallel-prefix networks: Kogge-Stone, Sklansky, Brent-Kung; carry-skip, carry-select, carry-save); shift-and-add and Booth (radix-2 and radix-4) multiplication, Wallace and Dadda trees, a pipelined multiplier; restoring, non-restoring, SRT and Newton-Raphson division, square root, and the compiler's divide-by-constant (checked for all 2^32 inputs); floating-point add and multiply hardware, guard, round and sticky bits, ties to even, subnormal slowdowns (measured), fused multiply-add, NaN and signed-zero differences in `max` between x86 and Arm, Q15 fixed point; the flags Z, N, C and V and the condition codes on x86, Arm and RISC-V; bit-manipulation instructions | Core | Written |

### Part 2: The instruction set and the processor

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 06 | [`06_isa_design.md`](06_isa_design.md) | ISA classes (accumulator, stack, memory-memory, register-memory, load/store); 0-, 1-, 2- and 3-address formats; fixed vs variable-length encoding and opcode fields; all the addressing modes (immediate, direct, indirect, register, register indirect, displacement, indexed, base, PC-relative, auto-increment); RISC vs CISC and micro-ops; instruction types; RISC-V as the teaching ISA | Core | Written |
| 07 | [`07_assembly_in_practice.md`](07_assembly_in_practice.md) | Registers, flags and condition codes; calling conventions (System V, AAPCS64); stack frames, alignment, red zone; reading `-O0`, `-O1` and `-O2` compiler output; inline assembly; assembler, linker and loader, object files and ELF, relocation, static vs dynamic linking (short; the C track goes deeper) | Core | Written |
| 08 | [`08_datapath_and_control_unit.md`](08_datapath_and_control_unit.md) | Single-cycle datapath, critical path and clock calculation; multi-cycle datapath and shared units; hardwired control (FSM and gates); microprogrammed control (control store, microinstructions, horizontal vs vertical, nano-programming); how x86 uses micro-ops | Skim | Written |
| 09 | [`09_pipelining_and_hazards.md`](09_pipelining_and_hazards.md) | The 5-stage RISC pipeline and pipeline registers; speedup, throughput, ideal vs actual CPI; the pipelined clock (slowest stage plus register overhead) and why five stages is not 5x faster; structural hazards; RAW, WAR and WAW data hazards; forwarding, load-use stalls, compiler scheduling, long-latency operations (multi-cycle FP multiply and divide); control hazards and branch delay slots; CPI with stalls; exceptions and interrupts in a pipeline (precise vs imprecise, EPC) and how a pipelined core takes an interrupt; pipeline-depth trade-offs | Core | Written |
| 10 | [`10_branch_prediction.md`](10_branch_prediction.md) | Why prediction is needed and what a miss costs in CPI (deeper and wider cores lose more); static (always taken, BTFNT); dynamic: 1-bit and 2-bit counters, aliasing, branch target buffer (direction and target as separate problems), correlating and global history (gshare, history length needed), tournament, TAGE and perceptron predictors, return address stack and its overflow, indirect branches and virtual calls; branchless code (conditional moves, masks, predication) and its break-even; `__builtin_expect`, `[[likely]]` and profile-guided optimisation; measuring with `perf stat` (branch-misses, MPKI); Spectre v1 and v2 as a pointer to 30 | Core | Written |
| 11 | [`11_superscalar_and_out_of_order_execution.md`](11_superscalar_and_out_of_order_execution.md) | Instruction-level parallelism, the dataflow limit and the critical path; in-order superscalar vs out-of-order; the structure of an out-of-order core; one program traced on an in-order machine, a scoreboard and Tomasulo's algorithm (reservation stations, tags, common data bus); register renaming with a physical register file (RAT, free list); reorder buffer, in-order commit, precise exceptions and misprediction recovery; load/store queues, store-to-load forwarding and memory disambiguation; MSHRs and memory-level parallelism; how far a core runs past a miss; limits of wider cores; compiler techniques for ILP (unrolling and multiple accumulators, software pipelining, trace scheduling); VLIW and EPIC compared; trace caches, runahead, value prediction and run-time translation as ideas not found in every core; why a pointer chase is slow | Core | Written |
| 12 | [`12_inside_a_modern_core.md`](12_inside_a_modern_core.md) | Putting 09 to 11 together in a real core: the front end (fetch, instruction cache, decode, micro-op cache, loop buffer, macro-fusion); rename and allocation width; schedulers, issue ports and execution units (integer, FP, SIMD, load/store, address generation); the load/store unit and the L1 data cache; retirement; latency vs throughput, dependency chains and multiple accumulators; recent Intel, AMD, Apple and Arm cores side by side (widths, window sizes, caches, with sources); reading instruction tables (Agner Fog, uops.info); top-down analysis and `perf`, step by step; why a core's IPC is what it is | Core | Written |

### Part 3: The memory system

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 13 | [`13_cache_basics.md`](13_cache_basics.md) | Locality; the memory hierarchy; SRAM vs DRAM at a glance; lines, direct-mapped, fully associative and set-associative caches; tag, index and offset arithmetic; replacement (LRU, pseudo-LRU, FIFO, random; Belady's optimal policy as the yardstick, RRIP in a sentence); write-through vs write-back, write-allocate, write buffers; AMAT; CAM and TCAM (TLBs, routers, ACLs); worked cache problems | Core | Written |
| 14 | [`14_cache_optimisation_and_the_real_hierarchy.md`](14_cache_optimisation_and_the_real_hierarchy.md) | The 3 Cs (and coherence misses); inclusive vs exclusive caches; victim caches; non-blocking caches and MSHRs; hardware and software prefetching; virtually-indexed caches (VIPT) and aliasing; loop interchange, blocking and tiling, cache-oblivious algorithms; data layout (array of structs vs struct of arrays, cache-line-sized nodes) and what it means for DSA choices; memory types (write-back, write-through, write-combining, uncacheable) and why device registers are uncached; non-temporal stores and cache-line flush; cache partitioning (Intel CAT, Arm MPAM) and noisy neighbours; finding misses with `perf` | Core | Written |
| 15 | [`15_virtual_memory_and_the_tlb.md`](15_virtual_memory_and_the_tlb.md) | Paging and segmentation, fragmentation; page tables (multi-level) and PTE bits; x86-64 4- and 5-level paging and Arm translation tables; the TLB, page walks and page-walk caches; page faults and demand paging; page replacement and Belady's anomaly (brief, the OS track goes deeper); huge pages, ASIDs/PCIDs, TLB shootdowns; protection and memory protection keys; process address-space layout and the user/kernel split; copy-on-write and `fork`; context-switch cost (registers, TLB, caches); the cost of KPTI; nested paging is in 27 | Core | Written |
| 16 | [`16_dram_memory_controllers_and_numa.md`](16_dram_memory_controllers_and_numa.md) | SRAM (6T) vs DRAM (1T1C) cells and refresh; rows, banks, channels and row-buffer hits; DRAM timing (tCL, tRCD, tRP) and computing an access latency; DDR4, DDR5 and HBM; bandwidth vs latency, loaded latency and Little's law for memory; measuring bandwidth STREAM-style; interleaving and address mapping (which address bits pick channel, bank, row and column; XOR hashing); memory controller scheduling (FR-FCFS) and interference between cores; reliability (ECC, scrubbing, chipkill, machine-check exceptions); Rowhammer; flash, persistent memory and CXL in a paragraph; NUMA placement, first-touch and `numactl`; processing-in-memory in a paragraph | Core | Written |
| 17 | [`17_storage_and_reliability.md`](17_storage_and_reliability.md) | HDD geometry (briefly); SSD and NAND, the flash translation layer, wear levelling, write amplification; NVMe queues; RAID 0, 1, 5, 6 and 10; MTTF and availability; error-correcting codes (parity, Hamming, SEC-DED); CRC and LFSRs (the frame check sequence of Ethernet and Wi-Fi) and checksums | Skim | Written |

### Part 4: Parallel hardware

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 18 | [`18_multicore_and_multithreading.md`](18_multicore_and_multithreading.md) | Why multicore (the power wall, end of Dennard scaling); Flynn's taxonomy (SISD, SIMD, MISD, MIMD); coarse-grained, fine-grained and simultaneous multithreading (SMT, hyper-threading); SMP vs NUMA; shared vs distributed memory; the three parallel programming models (shared address space, message passing, data-parallel) and work distribution and load balancing; scaling limits; heterogeneous cores (big.LITTLE, performance and efficiency cores); chiplets and the uncore; the SMT security trade-off in a sentence | Core | Written |
| 19 | [`19_cache_coherence.md`](19_cache_coherence.md) | The coherence problem; snooping protocols (MSI, MESI, MOESI, MESIF); write-invalidate vs write-update; directory-based protocols; coherence misses; false sharing and cache-line bouncing; the true cost of an atomic; `perf c2c`; lock scalability on a coherent machine (test-and-set vs test-and-test-and-set, ticket and MCS locks, backoff); barrier implementations (sense-reversing, tree); per-core sharding and `alignas(64)` padding; why a contended atomic counter collapses | Core | Written |
| 20 | [`20_memory_consistency_and_atomics.md`](20_memory_consistency_and_atomics.md) | Sequential consistency; TSO (x86) and weak ordering (ARM, POWER, RISC-V); store buffers and reordering examples; fences and barriers; acquire/release; LL/SC vs locked instructions; compare-and-swap and the ABA problem; x86 `lock`/`xchg`/`mfence` vs Arm `dmb`/`ldar`/`stlr` and the Arm LSE atomics (`cas`, `ldadd`); litmus tests (store buffering, message passing); hardware transactional memory (Intel TSX, Arm TME, IBM POWER) and lock elision, with its limits (capacity aborts, a fallback path, security issues); why `volatile` is not atomic; link to the C++ memory model | Core | Written |
| 21 | [`21_simd_and_vector_processing.md`](21_simd_and_vector_processing.md) | Data-level parallelism; classic vector processors (vector registers, chaining, strip-mining) and the RISC-V vector extension; SSE, AVX, AVX-512, NEON, SVE; vector length, masks, gather and scatter; auto-vectorisation and its limits; intrinsics, alignment, runtime dispatch with `cpuid` and ifunc; floating-point reductions and `-ffast-math` (addition is not associative); saturating arithmetic; denormals and flush-to-zero; AVX-512 frequency effects; SIMD in practice (`memchr`, packet parsing, UTF-8 validation) | Core | Written |
| 22 | [`22_gpu_execution_model.md`](22_gpu_execution_model.md) | Why GPUs exist (throughput design vs latency design); the SIMT model: threads, warps (NVIDIA) and wavefronts (AMD), thread blocks and the grid mapped onto streaming multiprocessors and compute units; the warp scheduler and issue (one instruction per scheduler per cycle; instruction-level parallelism inside a thread also hides latency); divergence, reconvergence and predication; independent thread scheduling (Volta onward) and warp-level synchronisation; latency hiding by multithreading instead of big caches; registers, shared memory and the occupancy limits they create; SIMT vs SIMD on a CPU; NVIDIA and AMD vocabulary side by side (SM/warp/shared memory vs CU/wavefront/LDS); reading PTX and SASS (the virtual and the real GPU instruction sets); the graphics pipeline in one page | Core | Written |
| 23 | [`23_gpu_memory_tensor_cores_and_scaling.md`](23_gpu_memory_tensor_cores_and_scaling.md) | Global memory, HBM and the L2; coalescing and how many memory transactions an access pattern makes; shared-memory banks and conflicts; constant memory and read-only caches; local memory and register spills; tensor cores and matrix engines; number formats for AI (FP32, TF32, FP16, BF16, FP8, INT8) and sparsity; asynchronous copies (`cp.async`, the Tensor Memory Accelerator) and thread-block clusters; the GPU roofline and telling memory-bound from compute-bound, including why LLM decoding at batch size one is memory-bound and large batches are not; kernel fusion and FlashAttention-style tiling as ways to cut HBM traffic; host-device transfers (PCIe, NVLink, pinned and unified memory), kernel-launch overhead and when a GPU loses to a CPU; multi-GPU scaling (NVLink and NVSwitch, collectives such as all-reduce at a glance; data, tensor and pipeline parallelism and why NVLink domains shape their placement); sharing one GPU (MIG, time-slicing, vGPU); links to the CUDA track | Core | Written |
| 24 | [`24_accelerators_and_fpgas.md`](24_accelerators_and_fpgas.md) | Why accelerators exist; the TPU and systolic arrays; NPUs for edge AI; why convolutions and attention become matrix multiplies, and layer fusion; dataflow and wafer-scale designs as the contrast to GPUs; fixed-function engines (crypto, packet processing); SmartNICs and DPUs; FPGAs (LUTs, CPU plus FPGA); heterogeneous SoCs and what goes wrong when blocks share one DRAM; matrix engines inside a CPU (Intel AMX); the fast-path and slow-path packet pattern (Linux flowtable offload) and packet budgets; the offload break-even (when an accelerator loses); real FPGA synthesis output (LUTs, carry chains, DSP blocks); a one-page glimpse of neuromorphic and quantum | Skim | Written |

### Part 5: The system, performance and security

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 25 | [`25_buses_pcie_and_io_mapping.md`](25_buses_pcie_and_io_mapping.md) | System buses (shared bus, synchronous vs asynchronous vs split-transaction, why parallel buses gave way to serial links); bus arbitration (daisy chain, centralised, distributed; fixed priority vs round-robin and starvation); tri-state and open-drain signalling, the I2C pull-up window and wired-AND arbitration; memory-mapped vs port-mapped I/O, address decoding, device registers, posted writes vs round-trip reads, the read-back flush and doorbells; PCIe (lanes, generations and the real per-lane rate with encoding, topology, TLPs and header cost, payload efficiency, latency, link power states, retimers and error reporting, configuration space, ECAM, enumeration and BAR sizing, bridge windows); a NIC receive-rate model showing why descriptor batching matters; DMA, MSI-X and peer-to-peer; the IOMMU (what it protects, the cost of translation and invalidation, ATS); SR-IOV and device passthrough; CXL in a paragraph; USB, SPI, UART and AXI at a glance; real config-space output read from a virtio-pci VM | Core | Written |
| 26 | [`26_interrupts_exceptions_and_dma.md`](26_interrupts_exceptions_and_dma.md) | Events (interrupts vs faults, traps and aborts; the x86 vector table and page-fault error codes read from a real run; precise exceptions and where the saved PC points); what the hardware does on x86-64 (IDT, interrupt frame, `iretq`, FRED), AArch64 (exception levels, `VBAR` vector table, `ESR`) and RISC-V (`sepc`, `scause`, `stvec`); system calls (`syscall`, `svc`, `ecall`) and a measured cost ladder of syscall, vDSO, fault and VM exit; privilege levels (rings, EL0 to EL3, U/S/M); interrupt controllers (8259, I/O APIC and local APIC, GIC, PLIC and AIA), level vs edge vs MSI-X, masking, priority, affinity and IPIs, with this VM's `/proc/interrupts` and an affinity experiment; Cortex-M NVIC; interrupt cost, top and bottom halves, moderation arithmetic, NAPI and receive livelock; polling vs interrupts (break-even, hybrids, DPDK-style loops, `io_uring` polling, WFE/MWAIT); timers and clocks (tick and tickless, TSC and the vDSO, Arm generic timer, RISC-V `mtime`, SysTick, counter wrap-around); DMA (descriptors, scatter-gather, rings, the ordering rule with `dma_wmb`/`dma_rmb`, coherence on x86 vs Arm and the DMA API, DDIO and leaky DMA); a packet's journey through a NIC (rx and tx paths, offloads, RSS with a Toeplitz hash verified against Microsoft's vectors, where packets are dropped); NVMe and virtio rings as the same pattern | Core | Written |
| 27 | [`27_hardware_virtualization.md`](27_hardware_virtualization.md) | What a hypervisor must guarantee (Popek and Goldberg) and why trap-and-emulate failed on x86 (the seventeen sensitive-but-unprivileged instructions, `POPF` run on a real machine); binary translation, paravirtualization and hardware assist; Intel VT-x and AMD-V (root and non-root mode, the VMCS, exit reasons, a measured ladder of which instructions exit in a real guest, published exit costs in cycles); Arm EL2, stage 2, VHE and the GIC virtual interface; the RISC-V H extension; memory virtualization (shadow paging, EPT/NPT, the two-dimensional walk and its `(n + 1)(m + 1) - 1` cost, huge pages in both dimensions, VPID/ASID/VMID, first touch, ballooning, dirty tracking); interrupts, timers and idle in a guest (APICv, posted interrupts, GIC list registers, TSC offsetting, `kvm-clock`, steal time, the paravirtual features this guest advertises); devices (emulation, virtio, `vhost`, vhost-user, SR-IOV and passthrough, `ioeventfd`) and this guest's virtio devices against Firecracker's design; containers, gVisor, microVMs and full VMs; nested virtualization and PVM; confidential VMs (SEV-SNP, TDX) as a pointer to 30; a symptom table for application code. Promote to Core if you target cloud or virtualization teams | Skim | Written |
| 28 | [`28_performance_and_power.md`](28_performance_and_power.md) | Why performance stopped being free (Moore's law, Dennard scaling and its end, dark silicon, die yield and chiplet cost with a yield table); the iron law with the real clock found by dependent-instruction chains (TSC ticks against core cycles) and CPI measured by experiment; why MIPS and peak FLOPS mislead; benchmarks (SPEC CPU2017 method, geometric, arithmetic and harmonic means, MLPerf Inference scenarios, reading a vendor's claim); microbenchmarking traps measured (dead code, merged chains, loop interchange, timer cost, warm-up, noise, a small harness); Amdahl, the Universal Scalability Law and offload; latency against throughput, percentiles, the measured interruption floor of a loop that does nothing, the tail at scale, Little's law, M/M/1 queues and coordinated omission; the roofline built from a measured peak and bandwidth, with kernels and matrix multiply placed on it; CPI stacks, top-down, `perf` and flame graphs, a measurement method; dynamic and static power, a DVFS energy model, P-states, C-states, turbo, RAPL and thermal limits, race to idle, clock and power gating, performance per watt; a symptom table for application code. This VM has no performance counters or power interfaces, so those parts give the method and published behaviour, not output from the machine | Core | Written |
| 29 | [`29_interconnection_networks_and_warehouse_scale_computing.md`](29_interconnection_networks_and_warehouse_scale_computing.md) | The cost of a message (`α + n/β`, the half-bandwidth size) measured with a TCP ping-pong on loopback in the VM, and a latency ladder from an L1 hit to light in fibre; topologies with degree, diameter, average distance, bisection width and link count computed by script (ring, mesh, torus, hypercube, crossbar, butterfly, tree, fat tree, Clos, dragonfly, the TPU v4 torus), k-ary fat-tree and leaf-spine sizes built and counted, oversubscription; routing (dimension order, Valiant, adaptive, ECMP) with the channel loads of three traffic patterns on a mesh and the birthday-problem collisions of ECMP; store-and-forward, cut-through and wormhole latency, credits and buffer sizing, virtual channels, head-of-line blocking and its 58.6 % limit; deadlock (channel dependency graphs checked by script, dateline, turn model, escape channels, protocol deadlock, PFC deadlock); networks on chip (the Skylake-SP ring to mesh); the data-centre network (Jupiter, incast, RDMA and DCQCN, accelerator networks and collectives); warehouse-scale computing (PUE arithmetic, failure rates and availability, the tail, one request end to end); a symptom table. Nothing about real switches or data centres was measured: those parts are published figures and arithmetic | Optional | Written |
| 30 | [`30_hardware_security.md`](30_hardware_security.md) | Threat model and what the VM reports about itself (`/sys/devices/system/cpu/vulnerabilities/`, the `mitigations=` attack vectors); side channels measured: a cache hit against a flushed miss (79 to 95 ns apart), a byte sent through 256 cache lines and read back (about 99 %), Flush+Reload, and Prime+Probe (my in-VM attempt was too noisy to use, and the tutorial says so); constant-time code (an early-exit compare rises 0.34 ns per matching byte, an OR-of-XORs compare is flat at 22 ns; guess and sample arithmetic); transient execution: Meltdown and KPTI, the Spectre v1 gadget with the index mask (1.9 times) and `lfence` (19 times) measured, v2 with retpoline measured (5 to 8 times on a predicted call) against eIBRS, BHI, v4 and SSBD measured (about 2 times), and the sysfs family table; Rowhammer in brief; NX tested on a real page, the ASLR spread over 300 runs, canary, RELRO, CET, Arm PAC and BTI, MTE; trusted execution (SGX and its attack history, TrustZone, confidential VMs); secure boot, measured boot with a PCR model, TPM sealing and attestation; a symptom table. Meltdown, MDS, Rowhammer, SGX, TrustZone, TDX, SEV, CET, MTE, PAC and the TPM were not measured: the VM cannot show them. No exploit is included | Skim | Written |
| 31 | [`31_arm_vs_x86_and_putting_it_together.md`](31_arm_vs_x86_and_putting_it_together.md) | ARM against x86 without "RISC against CISC": a map of what differs; code density measured on 53,692 lines of zstd compiled for both ISAs (AArch64 10 to 15 % smaller, 14 to 15 % fewer instructions, under half the stack accesses: the reverse of tutorial 06's one loop); memory ordering and atomics as a porting problem (`release`/`acquire` are the same `mov`s on x86 and `stlr`/`ldar` on Arm, and the message-passing test passes on x86 and under QEMU); porting pitfalls run on both ISAs (`char` sign, `long double`, divide by zero, float to integer, fused multiply-add, misaligned atomics); x86 specifics (rings and MSRs, `cpuid` at 7.5 µs, the 11,008-byte `XSAVE` area against Arm SVE state); how to choose. Then a cost ledger and eight "what happens when" walkthroughs with this VM's measurements: running a program (about 1 ms, 75 page faults linked dynamically against 46 statically), a packet arriving (5.9 to 7.3 KB packets, 1.7 per interrupt), a pointer dereference, power-on to `main` (the `dmesg` timeline of a micro-VM, the x86 reset vector and Arm trusted firmware), a system call on both ISAs (two programs with no C library), a context switch (1.2 to 1.5 µs), a page fault (cost by kind), and `malloc` down to a physical frame. Arm code was only compiled and run under QEMU: there is no Arm hardware, timing or ordering result | Core | Written |

---

## Coverage check

I compared this plan with the list you pasted and with the syllabi of the standard references (Hennessy & Patterson, Stallings, Berkeley CS61C, MIT 6.004, ETH Zürich's computer architecture courses) and a published 123-question interview guide. Every topic on your list has a home:

| Your list | Tutorial |
|---|---|
| Number systems, BCD, Gray code, ASCII and Unicode, fixed point, IEEE-754 | 02 |
| Boolean algebra, gates, K-maps, mux, decoders, encoders, adders | 03 |
| Latches, flip-flops, timing, registers, counters, FSMs, a taste of HDL | 04 |
| Fast adders, Booth, Wallace, division algorithms, ALU and flags | 05 |
| ISA classes, formats, addressing modes, RISC vs CISC | 06 |
| Calling conventions, stack frames, assembler, linker, loader, ELF | 07 |
| Single-cycle and multi-cycle datapath, hardwired and microprogrammed control | 08 |
| Pipelining, hazards, forwarding, exceptions in pipelines | 09 |
| Static and dynamic branch prediction | 10 |
| Superscalar, Tomasulo, renaming, ROB, speculation, VLIW and EPIC | 11 (and 12 for how a real core is organised) |
| Cache mapping, policies, replacement, 3 Cs, MSHR, prefetch, victim, inclusive and exclusive | 13 and 14 |
| Paging, segmentation, TLB, page faults, VIPT, TLB shootdowns | 15 (VIPT in 14) |
| SRAM, DRAM, DDR, HBM, flash, interleaving | 16 |
| HDD, SSD and FTL, RAID, MTTF, Hamming and SEC-DED | 17 |
| Flynn's taxonomy, multithreading and SMT, SMP vs NUMA | 18 |
| MSI, MESI, MOESI, directory protocols, false sharing | 19 |
| Consistency models and fences | 20 |
| Vector architecture and SIMD | 21 |
| GPU architecture | 22 and 23 |
| TPU, systolic arrays, FPGAs, neuromorphic and quantum glimpse | 24 |
| Buses, arbitration, memory-mapped vs port-mapped I/O, PCIe | 25 |
| Polling, interrupts, DMA | 26 |
| Interconnection networks, warehouse-scale computing | 29 |
| Side channels, Spectre and Meltdown, TEEs | 30 (Spectre introduced in 11) |

**Added from the references, not in your list:** performance and power (iron law, roofline, Little's law, DVFS) in 28; hardware support for virtual machines (VT-x, nested paging, SR-IOV) in 27; memory controllers, Rowhammer and processing-in-memory in 16; top-down analysis and `perf` counters in 12, 14 and 28; the privilege and context-switch material that interview guides ask about, in 01, 15 and 26; ARM vs x86 porting pitfalls in 31; RISC-V as the teaching ISA in 06; TAGE and perceptron predictors in 10.

**Added after reviewing tutorials 01 to 08** (so nothing is lost): privileged instructions and context switches (in 01 and 06, details in 26); the system-call ABI (07, 26); tail calls, optimisation levels and debug info (07); UTF-16, float non-associativity and bit-field warnings (02); tri-state buffers, barrel shifter and `slt` (03, 08); LFSRs, CRC and scramblers (04, 17); and, in the rows above for later tutorials, lock scalability (19), CAS and the ABA problem (20), SIMD reductions and denormals (21), benchmarking pitfalls (28) and the power-on-to-`main` walkthrough (31).

**Added after the full-scope review of CPU, memory and GPU:** three new tutorials, *Inside a modern core* (12), *GPU architecture II: memory, tensor cores and scaling out* (23) and *Hardware support for virtualization* (27), because the out-of-order tutorial, the single GPU tutorial and a few words in the virtual-memory row could not hold that material at interview depth. Smaller additions: CAM/TCAM and write buffers (13); memory types, cache partitioning and data-layout choices (14); page-walk caches and protection keys (15); DRAM timing arithmetic, loaded latency, RAS features and CXL (16); NIC rings, RSS and DDIO, NVMe queues, and timers (26); SmartNICs and DPUs (24).

**Added after comparing with NPTEL (IIT Kharagpur, IIT Guwahati, IISc), MIT 6.5900, ETH Zürich, Stanford CS149, Georgia Tech and the NVIDIA interview write-ups:** hardware transactional memory (20); compiler ILP techniques and predication (10, 11); classic vector processors (21); long-latency pipeline operations (09); DRAM address mapping and memory scheduling (16); parallel programming models and barriers (18, 19); GPU independent thread scheduling, PTX/SASS, constant memory, the Tensor Memory Accelerator, LLM decode arithmetic intensity and multi-GPU parallelism (22, 23); dataflow and wafer-scale accelerators (24); technology trends, yield and SPEC (28). Everything else in those outlines was already in the plan. Two things are left out on purpose: the history and generations of computers, and architectural simulators such as gem5 (the plan keeps to paper work and a few lines you run yourself).

**Added in the final review** (all 31 tutorials re-read against this plan, the syllabi above and published interview question lists): tutorial 05 is now written and is Core, because divide-by-constant, rounding, subnormals, fused multiply-add, `max` and the flags are interview material in their own right; crossing clock domains (synchroniser failure rate, Gray-coded pointers, asynchronous FIFOs, handshakes, reset synchronisers, torn counter reads) in 04; clock gating and power gating in 28; PCIe link power states, retimers and error reporting in 25; cache-oblivious algorithms in 14; trace caches, runahead and value prediction in 11; square root and the functions that are not in hardware in 05; and a note in 13 that reconciles the 60 to 100 ns textbook DRAM latency with the 95 to 135 ns measured here. Embedded-only material (the memory protection unit, bit-banding, JTAG and SWD, watchdogs, ADC, PWM, CAN) sits in the planned stretch topics 04 and 05 of `13_embedded`.

**Kept to a paragraph on purpose** (exam material with little interview value): Quine-McCluskey by hand, K-maps beyond 5 variables, EBCDIC, the Itanium story, quad-precision floats, nano-programming.

**Covered in other tracks instead:** operating-system memory management and page-replacement policy (`05_operating_systems`), embedded systems and RTOS (`13_embedded`), linking in depth (`02_c_language`), CUDA programming and GPU parallel patterns such as reduction, scan and histogram, plus FlashAttention and KV-cache sizing (`12_gpu_cuda`), storage engines and distributed storage (`11_hld`).

**Not planned:** chip-design and verification work (writing synthesizable RTL in SystemVerilog, UVM testbenches, timing closure). Tutorials 03 and 04 give the vocabulary only (tutorial 04 also covers clock-domain crossing, the part of RTL design that firmware and hardware interviews touch). If you target design or verification roles, that needs a separate track.

---

## Problems (about 4 per tutorial)

A mix of paper problems and hands-on ones. Here is the kind of problem to expect:

| # | Typical problems |
|---|---|
| 01 | Trace a program on the toy machine; iron-law arithmetic; read compiler output on godbolt; look up your own machine's cache sizes |
| 02 | Two's-complement and hex conversions by hand; spot the overflow; decode a float's bits; struct size with padding |
| 03 | Simplify a function with a K-map; build a 4-bit adder on paper; design a decoder |
| 04 | Trace a D flip-flop circuit; design a small counter; draw a state machine for a pattern detector |
| 05 | Booth recoding and the multiplier tree; a Kogge-Stone addition and carry-save sums; guard, round and sticky bits by hand and a divide-by-constant multiplier; write a floating-point multiply with integers and test it against the hardware |
| 06 | Encode an instruction by hand; pick an addressing mode for an array access; RISC vs CISC code size |
| 07 | Read a compiled function; work out registers and the stack frame; write a short function in assembly on paper |
| 08 | Follow one instruction through the datapath; fill in control signals; clock period from component delays |
| 09 | Draw a pipeline diagram with stalls and forwarding; compute CPI; find the hazards in a snippet |
| 10 | Predict a branch pattern with a 2-bit counter; measure sorted vs random data in a few lines of C |
| 11 | Rename registers on paper; step through a small Tomasulo example; reason about why a pointer chase is slow |
| 12 | Find the bottleneck of a loop from its instruction mix and port usage; latency-bound vs throughput-bound time of a reduction; read a `perf stat` top-down breakdown |
| 13 | Cache geometry arithmetic; hits and misses for an access pattern; AMAT |
| 14 | Loop interchange and blocking on a matrix; classify misses (compulsory, capacity, conflict); effect of prefetching |
| 15 | Split an address into page number and offset; walk a two-level page table; TLB reach |
| 16 | DRAM bandwidth arithmetic; row-buffer hit rate; access latency from DRAM timings; NUMA placement |
| 17 | RAID capacity and failure arithmetic; write amplification; Hamming code and a CRC by hand |
| 18 | Classify machines with Flynn's taxonomy; SMT benefit; Amdahl on 16 cores |
| 19 | MESI state transitions for a sequence of accesses; find and fix false sharing |
| 20 | Which outcomes are allowed under SC, TSO and ARM; place the fences in a lock-free snippet |
| 21 | Vectorise a loop by hand on paper; why a loop does not auto-vectorise |
| 22 | Warp divergence cost; occupancy arithmetic from register and shared-memory limits; map a thread index to a warp and an address |
| 23 | Count the memory transactions of an access pattern; spot a bank conflict; place a kernel on the roofline (including decode at batch 1 vs 64); when PCIe transfer time cancels a GPU speedup |
| 24 | Size a systolic array for a matrix multiply; place the TPU's workloads on its roofline; decide whether an offload pays (break-even, residency); budget cycles per packet and the effect of the fast-path hit rate |
| 25 | Bandwidth of a PCIe link (lane rate, encoding and TLP overhead); arbitration order under fixed priority and round-robin; decode a BAR size and an ECAM address; the I2C pull-up window and bus arbitration; how a doorbell write and a read-back flush behave |
| 26 | Packet and interrupt budgets with moderation; an eight-slot descriptor ring and a toy RSS hash on paper; decode page-fault error codes, Arm vector offsets and RISC-V `scause`; time a system call against the vDSO on your machine and read `/proc/interrupts` |
| 27 | Count the memory references of a nested page walk and what huge pages in each dimension do; an exit budget for a packet rate with emulated, virtio and SR-IOV NICs; classify instructions as exit or no exit; detect a hypervisor from inside a machine and time `cpuid` against a system call |
| 28 | Iron-law, means and clock questions; Amdahl, scalability, Little's law, queue and fan-out arithmetic; read a `perf stat` output, build a CPI stack and place kernels on a roofline; an energy model with deadlines and race to idle; on your machine: find the real clock, measure the interruption floor, the memory roofs and a matrix-multiply peak |
| 29 | The cost of a message and a ping-pong on your machine; topology metrics, bisection bandwidth, routing a message and channel load; a channel dependency graph, dateline, turn model, protocol deadlock and head-of-line blocking; fat-tree sizing, oversubscription, ECMP, incast, PUE and availability arithmetic |
| 30 | How a cache timing channel works (thresholds, why the receiver needs a page stride, a scrambled order and a written array, the channel rate, a program for your machine); spot the Spectre gadget in four functions and read the compiler output, and what retpoline adds to a path; constant-time code (rewrite a compare, guess and sample arithmetic, measure on your machine); what SGX, TrustZone, confidential VMs, NX, shadow stacks, KPTI, secure boot and a TPM protect against nine threats, a signing key on a rooted phone, and a confidential database |
| 31 | Port a lock-free ring buffer from x86 to Arm (the right memory orders, the compiled `ldar`/`stlr`, why x86 and QEMU pass, what ThreadSanitizer says); predict the porting pitfalls, measure code density and saved state on your own code; the cost ledger (page faults, process creation, `malloc`) as arithmetic; the packet budget at 10, 25 and 100 Gb/s |

---

## Questions you should be able to answer at the end

Each question points to the tutorial that answers it (the second column of numbers is where the supporting detail is). If you can answer all of them out loud, with numbers, you are ready for the computer-organization part of a systems interview.

| # | Question | Tutorial |
|---|---|---|
| 1 | What is -1 as a 32-bit two's-complement value in hex? What does `(unsigned)-1 > 0` evaluate to? | 02 |
| 2 | Walk through what a 5-stage pipeline does with a load followed by a dependent add. | 09 |
| 3 | A 32 KiB, 8-way set-associative cache with 64-byte lines: how many sets, and which address bits are tag, index and offset? | 13 |
| 4 | Why is a linked list slower than an array of the same size, even when both fit in L2? | 11, 13, 14 |
| 5 | What is false sharing, and how would you find and fix it? | 19 |
| 6 | How does a NIC deliver a packet into memory without the CPU copying it? | 25, 26 |
| 7 | A program spends 80 % of its time in a loop you can parallelise perfectly. What is the best speedup on 16 cores? | 18, 28 |
| 8 | Why does code that works on x86 sometimes break on ARM? | 20, 31 |
| 9 | What happens, step by step, on a TLB miss? On a page fault? | 15 |
| 10 | What do register renaming and the reorder buffer each do? | 11 |
| 11 | What happens, step by step, from power-on until `main` runs? | 31 |
| 12 | What does a system call cost, and what does the hardware do when it executes one? | 26, 31 |
| 13 | Why is a contended atomic counter slow, and how would you fix it? | 19 |
| 14 | Why can the same sum of floats give different answers after vectorising or going multithreaded? | 21, 05 |
| 15 | A GPU version of a kernel is slower than the CPU version. Name three reasons. | 23 |
| 16 | Walk through what an out-of-order core does with a load that misses the L2 cache and the instructions behind it. | 11, 12 |
| 17 | How does a GPU hide memory latency without large caches, and how do you tell whether a kernel is memory-bound? | 22, 23 |
| 18 | What is a VM exit, and why does nested paging make a TLB miss more expensive? | 27 |
| 19 | How can a CPU add two 64-bit numbers in one clock cycle? How does it multiply, and why is a divide so much slower? | 05 |
| 20 | How does the compiler divide by 10 without a divide instruction, and why is a signed `x / 2` more than a shift? | 05 |
| 21 | Walk through a floating-point addition. What are the guard, round and sticky bits, and what does a subnormal result cost? | 05 |
| 22 | How are the flags computed, and why can `max` of a NaN differ between x86 and Arm? | 05 |
| 23 | What decides the maximum clock frequency? Explain setup and hold time. | 04, 09 |
| 24 | How do you pass a multi-bit value between two clock domains, and how does an asynchronous FIFO tell full from empty? | 04 |
| 25 | Von Neumann against Harvard; hardwired against microprogrammed control; RISC against CISC. | 01, 06, 08 |
| 26 | How does a branch predictor work and what does a misprediction cost? | 10 |
| 27 | Why does DRAM latency not improve with each generation, and what is a row-buffer hit? | 16 |
| 28 | Why did clock speeds stop rising? What are dark silicon, clock gating and race to idle? | 28 |
| 29 | What is the difference between x86 TSO and Arm's memory model, and where do the fences go? | 20 |
| 30 | What does the IOMMU protect against and what does it cost? | 25, 26 |
| 31 | How does a cache timing channel work, and what does Spectre v1 exploit? | 30 |
| 32 | How does a flash translation layer cause write amplification? How does RAID 5 survive a disk failure? | 17 |
| 33 | Is this loop memory-bound or compute-bound? Place it on a roofline. | 12, 28 |

---

## Resources

| Resource | Use it for |
|---|---|
| *Computer Systems: A Programmer's Perspective* (CS:APP), Bryant & O'Hallaron | The main companion: data representation, assembly, caches, virtual memory |
| *Computer Organization and Design* (RISC-V or ARM edition), Patterson & Hennessy | Logic, datapath and pipelining (tutorials 03 to 09) |
| *Computer Architecture: A Quantitative Approach*, Hennessy & Patterson | Depth on out-of-order execution, memory, parallelism, GPUs and accelerators. Chapter 1 and Appendix A are 28 and 06; Appendix C is 09; chapter 3 is 10 to 12; chapter 2 and Appendix B are 13 to 16; chapter 4 is 21 to 23; chapter 5 is 18 to 20; chapter 6 is 29; chapter 7 is 24 |
| *Computer Organization and Architecture*, William Stallings | Fuller treatment of number systems, arithmetic, control units and I/O (02, 05, 08, 25, 26) |
| "What Every Programmer Should Know About Memory", Ulrich Drepper | Caches and DRAM, still the reference (13, 14, 16) |
| MIT 6.004 (OpenCourseWare), Berkeley CS61C, ETH Zürich Digital Design and Computer Architecture (lecture videos) | A second explanation of any topic that does not click |
| godbolt.org (Compiler Explorer) | Reading compiler output for any language and architecture |
| Ben Eater's 8-bit computer videos, or nand2tetris.org | Seeing logic and a CPU built from scratch (tutorials 03, 04 and 08) |
| Agner Fog's optimisation manuals, uops.info, Chips and Cheese and WikiChip | Instruction latencies, and real-core microarchitecture details (12) |

### Free courses and lectures, matched to the tutorials

Watch one of these next to a tutorial when you want a second explanation or more practice. You do not need all of them.

| Course | Best for | Tutorials |
|---|---|---|
| NPTEL 106105163, *Computer Architecture and Organization* (IIT Kharagpur, Indranil Sengupta and Kamalika Datta) | A slow, complete first pass: ISA, control unit, arithmetic, memory, cache, I/O | 05 to 09, 13, 14, 25, 26 |
| NPTEL 106105033, *High Performance Computer Architecture* (IIT Kharagpur, Ajit Pal) | ILP, memory hierarchy, thread-level parallelism, and reviews of Pentium and ARM cores | 09 to 20 |
| NPTEL 106103206, *Advanced Computer Architecture*, and 106103183, *Multi-Core Computer Architecture* (IIT Guwahati, John Jose) | Pipeline hazards, Tomasulo, cache optimisation, DRAM controllers, coherence, GPUs, network-on-chip | 09 to 16, 19, 22, 29 |
| Georgia Tech / Udacity *High Performance Computer Architecture* (Milos Prvulovic) | The clearest short lessons on branch prediction, Tomasulo, the reorder buffer and memory ordering | 09 to 11 |
| ETH Zürich *Computer Architecture* (Onur Mutlu; lecture videos on his YouTube channel) | DRAM, memory controllers, processing-in-memory, GPUs, coherence. Deep and research-flavoured | 10 to 16, 19, 22 |
| MIT 6.5900 (formerly 6.823) *Computer System Architecture* | Out-of-order execution, memory models, GPUs and accelerators, modern virtual memory | 11, 15, 20, 22, 24 |
| Stanford CS149 *Parallel Computing* | A modern multicore and GPU view; coherence, consistency, lock-free code, transactional memory | 18 to 23 |
| Berkeley CS61C *Great Ideas in Computer Architecture* | Pipelining, caches, virtual memory and SIMD taught through projects | 06 to 09, 13, 15, 21 |
| Udacity CS344 *Intro to Parallel Programming* (Owens and Luebke) and NVIDIA's architecture whitepapers (Volta, Ampere, Hopper) | The GPU hardware lesson and real numbers for 22 and 23 | 22, 23 |

---

## Progress

| Part | Tutorials | Status |
|---|---|---|
| 1 Foundations | 01 to 05 | 01 to 05 written, Part 1 is complete |
| 2 Instruction set and processor | 06 to 12 | 06 to 12 written, Part 2 is complete |
| 3 Memory system | 13 to 17 | 13 to 17 written, Part 3 is complete |
| 4 Parallel hardware | 18 to 24 | 18 to 24 written, Part 4 is complete |
| 5 System, performance and security | 25 to 31 | 25 to 31 written, Part 5 is complete |

Track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
