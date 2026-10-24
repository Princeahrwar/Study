# Computer Organization and Architecture: Plan

Every performance question, OS question, driver question and GPU question sits on top of how a CPU executes instructions and how memory feeds it. This track starts from "what is a computer" and builds up, one tutorial at a time, from logic gates to out-of-order cores, caches, virtual memory, DRAM, multicore, GPUs, accelerators and virtualization, so that "why is this slow?" becomes arithmetic instead of guessing.

**Language:** C (and some assembly) · **Size:** 31 tutorials in 5 parts · **Time:** about 3 hours per Core tutorial, 1 to 2 hours per Skim or Optional one (about 66 hours for the Core path, about 80 for everything) · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain.
3. Ends with about four problems. Each says **where to solve it** (paper, godbolt.org, your own terminal) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.

No simulators, no companion programs. Anything you run is a few lines you type yourself.

---

## Tutorials

*Priority* tells you what to cut if time is short. **Core** is asked often in systems interviews. **Skim** is good background you can read quickly. **Optional** is for completeness (exam-style or specialised material).

### Part 1: Foundations (logic and numbers)

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 01 | [`01_how_a_computer_works.md`](01_how_a_computer_works.md) | CPU, memory and I/O; fetch-decode-execute on a toy machine; real assembly for the same ideas; privilege modes and interrupts (preview); the iron law; the latency ladder; ISA vs microarchitecture | Core | Written |
| 02 | [`02_data_representation.md`](02_data_representation.md) | Binary, octal, hex, BCD, Gray code; unsigned, sign-magnitude, 1's and 2's complement; sign extension, overflow vs carry, shifts; endianness; fixed-point; IEEE-754 (half, single, double; normalisation, subnormals, NaN, infinity, rounding modes); ASCII and UTF-8; alignment and struct padding | Core | Written |
| 03 | [`03_boolean_logic_and_combinational_circuits.md`](03_boolean_logic_and_combinational_circuits.md) | Gates (AND, OR, NOT, NAND, NOR, XOR), universal gates, Boolean algebra, De Morgan; SOP and POS; Karnaugh maps (4 to 5 variables), don't-cares, Quine-McCluskey in a paragraph; multiplexers, demultiplexers, decoders, encoders, priority encoders, comparators; half and full adders, ripple-carry adder; a first ALU; transistors (CMOS) and gate delay in a page | Skim | Written |
| 04 | [`04_sequential_logic_and_state_machines.md`](04_sequential_logic_and_state_machines.md) | Clocks, level vs edge triggering; SR, D, JK and T latches and flip-flops; setup and hold time, clock skew, metastability; registers and shift registers (SISO, SIPO, PISO, PIPO); synchronous and asynchronous counters; Mealy and Moore finite state machines; a read-only taste of Verilog | Skim | Written |
| 05 | Computer arithmetic and the ALU | Fast adders (carry-lookahead, carry-skip, carry-select, carry-save); shift-and-add and Booth multiplication, Wallace and Dadda trees; restoring, non-restoring, SRT and Newton-Raphson division; floating-point add and multiply hardware and rounding; ALU design and the flags (zero, sign, carry, overflow) | Optional | Optional, not written yet |

### Part 2: The instruction set and the processor

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 06 | [`06_isa_design.md`](06_isa_design.md) | ISA classes (accumulator, stack, memory-memory, register-memory, load/store); 0-, 1-, 2- and 3-address formats; fixed vs variable-length encoding and opcode fields; all the addressing modes (immediate, direct, indirect, register, register indirect, displacement, indexed, base, PC-relative, auto-increment); RISC vs CISC and micro-ops; instruction types; RISC-V as the teaching ISA | Core | Written |
| 07 | [`07_assembly_in_practice.md`](07_assembly_in_practice.md) | Registers, flags and condition codes; calling conventions (System V, AAPCS64); stack frames, alignment, red zone; reading `-O0`, `-O1` and `-O2` compiler output; inline assembly; assembler, linker and loader, object files and ELF, relocation, static vs dynamic linking (short; the C track goes deeper) | Core | Written |
| 08 | [`08_datapath_and_control_unit.md`](08_datapath_and_control_unit.md) | Single-cycle datapath, critical path and clock calculation; multi-cycle datapath and shared units; hardwired control (FSM and gates); microprogrammed control (control store, microinstructions, horizontal vs vertical, nano-programming); how x86 uses micro-ops | Skim | Written |
| 09 | [`09_pipelining_and_hazards.md`](09_pipelining_and_hazards.md) | The 5-stage RISC pipeline and pipeline registers; speedup, throughput, ideal vs actual CPI; the pipelined clock (slowest stage plus register overhead) and why five stages is not 5x faster; structural hazards; RAW, WAR and WAW data hazards; forwarding, load-use stalls, compiler scheduling, long-latency operations (multi-cycle FP multiply and divide); control hazards and branch delay slots; CPI with stalls; exceptions and interrupts in a pipeline (precise vs imprecise, EPC) and how a pipelined core takes an interrupt; pipeline-depth trade-offs | Core | Written |
| 10 | [`10_branch_prediction.md`](10_branch_prediction.md) | Why prediction is needed and what a miss costs in CPI (deeper and wider cores lose more); static (always taken, BTFNT); dynamic: 1-bit and 2-bit counters, aliasing, branch target buffer (direction and target as separate problems), correlating and global history (gshare, history length needed), tournament, TAGE and perceptron predictors, return address stack and its overflow, indirect branches and virtual calls; branchless code (conditional moves, masks, predication) and its break-even; `__builtin_expect`, `[[likely]]` and profile-guided optimisation; measuring with `perf stat` (branch-misses, MPKI); Spectre v1 and v2 as a pointer to 30 | Core | Written |
| 11 | [`11_superscalar_and_out_of_order_execution.md`](11_superscalar_and_out_of_order_execution.md) | Instruction-level parallelism, the dataflow limit and the critical path; in-order superscalar vs out-of-order; the structure of an out-of-order core; one program traced on an in-order machine, a scoreboard and Tomasulo's algorithm (reservation stations, tags, common data bus); register renaming with a physical register file (RAT, free list); reorder buffer, in-order commit, precise exceptions and misprediction recovery; load/store queues, store-to-load forwarding and memory disambiguation; MSHRs and memory-level parallelism; how far a core runs past a miss; limits of wider cores; compiler techniques for ILP (unrolling and multiple accumulators, software pipelining, trace scheduling); VLIW and EPIC compared; why a pointer chase is slow | Core | Written |
| 12 | [`12_inside_a_modern_core.md`](12_inside_a_modern_core.md) | Putting 09 to 11 together in a real core: the front end (fetch, instruction cache, decode, micro-op cache, loop buffer, macro-fusion); rename and allocation width; schedulers, issue ports and execution units (integer, FP, SIMD, load/store, address generation); the load/store unit and the L1 data cache; retirement; latency vs throughput, dependency chains and multiple accumulators; recent Intel, AMD, Apple and Arm cores side by side (widths, window sizes, caches, with sources); reading instruction tables (Agner Fog, uops.info); top-down analysis and `perf`, step by step; why a core's IPC is what it is | Core | Written |

### Part 3: The memory system

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 13 | [`13_cache_basics.md`](13_cache_basics.md) | Locality; the memory hierarchy; SRAM vs DRAM at a glance; lines, direct-mapped, fully associative and set-associative caches; tag, index and offset arithmetic; replacement (LRU, pseudo-LRU, FIFO, random; Belady's optimal policy as the yardstick, RRIP in a sentence); write-through vs write-back, write-allocate, write buffers; AMAT; CAM and TCAM (TLBs, routers, ACLs); worked cache problems | Core | Written |
| 14 | [`14_cache_optimisation_and_the_real_hierarchy.md`](14_cache_optimisation_and_the_real_hierarchy.md) | The 3 Cs (and coherence misses); inclusive vs exclusive caches; victim caches; non-blocking caches and MSHRs; hardware and software prefetching; virtually-indexed caches (VIPT) and aliasing; loop interchange, blocking and tiling; data layout (array of structs vs struct of arrays, cache-line-sized nodes) and what it means for DSA choices; memory types (write-back, write-through, write-combining, uncacheable) and why device registers are uncached; non-temporal stores and cache-line flush; cache partitioning (Intel CAT, Arm MPAM) and noisy neighbours; finding misses with `perf` | Core | Written |
| 15 | [`15_virtual_memory_and_the_tlb.md`](15_virtual_memory_and_the_tlb.md) | Paging and segmentation, fragmentation; page tables (multi-level) and PTE bits; x86-64 4- and 5-level paging and Arm translation tables; the TLB, page walks and page-walk caches; page faults and demand paging; page replacement and Belady's anomaly (brief, the OS track goes deeper); huge pages, ASIDs/PCIDs, TLB shootdowns; protection and memory protection keys; process address-space layout and the user/kernel split; copy-on-write and `fork`; context-switch cost (registers, TLB, caches); the cost of KPTI; nested paging is in 27 | Core | Written |
| 16 | DRAM, memory controllers and NUMA | SRAM (6T) vs DRAM (1T1C) cells and refresh; rows, banks, channels and row-buffer hits; DRAM timing (tCL, tRCD, tRP) and computing an access latency; DDR4, DDR5 and HBM; bandwidth vs latency, loaded latency and Little's law for memory; measuring bandwidth STREAM-style; interleaving and address mapping (which address bits pick channel, bank, row and column; XOR hashing); memory controller scheduling (FR-FCFS) and interference between cores; reliability (ECC, scrubbing, chipkill, machine-check exceptions); Rowhammer; flash, persistent memory and CXL in a paragraph; NUMA placement, first-touch and `numactl`; processing-in-memory in a paragraph | Core |  |
| 17 | Storage and reliability | HDD geometry (briefly); SSD and NAND, the flash translation layer, wear levelling, write amplification; NVMe queues; RAID 0, 1, 5, 6 and 10; MTTF and availability; error-correcting codes (parity, Hamming, SEC-DED); CRC and LFSRs (the frame check sequence of Ethernet and Wi-Fi) and checksums | Skim |  |

### Part 4: Parallel hardware

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 18 | Multicore and multithreading | Why multicore (the power wall, end of Dennard scaling); Flynn's taxonomy (SISD, SIMD, MISD, MIMD); coarse-grained, fine-grained and simultaneous multithreading (SMT, hyper-threading); SMP vs NUMA; shared vs distributed memory; the three parallel programming models (shared address space, message passing, data-parallel) and work distribution and load balancing; scaling limits; heterogeneous cores (big.LITTLE, performance and efficiency cores); chiplets and the uncore; the SMT security trade-off in a sentence | Core |  |
| 19 | Cache coherence | The coherence problem; snooping protocols (MSI, MESI, MOESI, MESIF); write-invalidate vs write-update; directory-based protocols; coherence misses; false sharing and cache-line bouncing; the true cost of an atomic; `perf c2c`; lock scalability on a coherent machine (test-and-set vs test-and-test-and-set, ticket and MCS locks, backoff); barrier implementations (sense-reversing, tree); per-core sharding and `alignas(64)` padding; why a contended atomic counter collapses | Core | Older note exists |
| 20 | Memory consistency and atomics | Sequential consistency; TSO (x86) and weak ordering (ARM, POWER, RISC-V); store buffers and reordering examples; fences and barriers; acquire/release; LL/SC vs locked instructions; compare-and-swap and the ABA problem; x86 `lock`/`xchg`/`mfence` vs Arm `dmb`/`ldar`/`stlr` and the Arm LSE atomics (`cas`, `ldadd`); litmus tests (store buffering, message passing); hardware transactional memory (Intel TSX, Arm TME, IBM POWER) and lock elision, with its limits (capacity aborts, a fallback path, security issues); why `volatile` is not atomic; link to the C++ memory model | Core |  |
| 21 | SIMD and vector processing | Data-level parallelism; classic vector processors (vector registers, chaining, strip-mining) and the RISC-V vector extension; SSE, AVX, AVX-512, NEON, SVE; vector length, masks, gather and scatter; auto-vectorisation and its limits; intrinsics, alignment, runtime dispatch with `cpuid` and ifunc; floating-point reductions and `-ffast-math` (addition is not associative); saturating arithmetic; denormals and flush-to-zero; AVX-512 frequency effects; SIMD in practice (`memchr`, packet parsing, UTF-8 validation) | Core | Older note exists |
| 22 | GPU architecture I: the execution model | Why GPUs exist (throughput design vs latency design); the SIMT model: threads, warps (NVIDIA) and wavefronts (AMD), thread blocks and the grid mapped onto streaming multiprocessors and compute units; the warp scheduler and issue (dual issue; instruction-level parallelism inside a thread also hides latency); divergence, reconvergence and predication; independent thread scheduling (Volta onward) and warp-level synchronisation; latency hiding by multithreading instead of big caches; registers, shared memory and the occupancy limits they create; SIMT vs SIMD on a CPU; NVIDIA and AMD vocabulary side by side (SM/warp/shared memory vs CU/wavefront/LDS); reading PTX and SASS (the virtual and the real GPU instruction sets); the graphics pipeline in one page | Core |  |
| 23 | GPU architecture II: memory, tensor cores and scaling out | Global memory, HBM and the L2; coalescing and how many memory transactions an access pattern makes; shared-memory banks and conflicts; constant memory and read-only caches; local memory and register spills; tensor cores and matrix engines; number formats for AI (FP32, TF32, FP16, BF16, FP8, INT8) and sparsity; asynchronous copies (`cp.async`, the Tensor Memory Accelerator) and thread-block clusters; the GPU roofline and telling memory-bound from compute-bound, including why LLM decoding at batch size one is memory-bound and large batches are not; kernel fusion and FlashAttention-style tiling as ways to cut HBM traffic; host-device transfers (PCIe, NVLink, pinned and unified memory), kernel-launch overhead and when a GPU loses to a CPU; multi-GPU scaling (NVLink and NVSwitch, collectives such as all-reduce at a glance; data, tensor and pipeline parallelism and why NVLink domains shape their placement); sharing one GPU (MIG, time-slicing, vGPU); links to the CUDA track | Core |  |
| 24 | Accelerators and FPGAs | Why accelerators exist; the TPU and systolic arrays; NPUs for edge AI; why convolutions and attention become matrix multiplies, and layer fusion; dataflow and wafer-scale designs as the contrast to GPUs; fixed-function engines (crypto, packet processing); SmartNICs and DPUs; FPGAs (LUTs, CPU plus FPGA); heterogeneous SoCs; a one-page glimpse of neuromorphic and quantum | Skim |  |

### Part 5: The system, performance and security

| # | Tutorial | What it covers | Priority | Status |
|---|---|---|---|---|
| 25 | Buses, PCIe and I/O mapping | System buses (address, data, control); synchronous vs asynchronous; bus arbitration (daisy chain, centralised, distributed); tri-state and open-drain signalling (I2C wired-AND); memory-mapped vs port-mapped I/O; device registers; PCIe (lanes, topology, TLPs, BARs, configuration space, enumeration); the IOMMU; SR-IOV and device passthrough; CXL in a paragraph; USB, SPI and I2C at a glance | Core |  |
| 26 | Interrupts, exceptions and DMA | Polling vs interrupt-driven I/O; interrupt vectors, priority and daisy-chaining; interrupt controllers (APIC, GIC) and MSI-X; privilege levels (x86 rings, Arm EL0 to EL3, RISC-V M/S/U); what the hardware does on a system call, an exception and an interrupt (save state, switch stack, vector), with the Linux system-call ABI as a pointer back to 07; timers and clocks (TSC, Arm generic timer); interrupt moderation and IPIs; DMA (burst vs cycle stealing, scatter-gather, descriptor rings); DMA and cache coherence; how a NIC delivers a packet (rx and tx rings, RSS and multiple queues, checksum and segmentation offloads, DDIO into the last-level cache); NVMe submission and completion queues; polling (DPDK-style) vs interrupts | Core |  |
| 27 | Hardware support for virtualization | Why virtualise; trap-and-emulate and why early x86 needed help; Intel VT-x and AMD-V (root and non-root mode, the VMCS, VM exits and their cost); Arm EL2; nested paging (EPT/NPT) and the two-dimensional page walk; VPID/ASID tagging; interrupt virtualization (APICv, GIC virtualization, posted interrupts); the IOMMU for devices, device assignment, SR-IOV and VFIO; paravirtual devices (virtio, vhost); sharing a GPU (vGPU, MIG); containers vs VMs from the hardware's view; lightweight VMs (KVM, Firecracker); confidential computing (SEV-SNP, TDX) as a pointer to 30. Promote to Core if you target cloud or virtualization teams | Skim |  |
| 28 | Performance and power | Technology trends (Moore's law, the end of Dennard scaling, dark silicon, die yield and cost); the iron law in depth; IPC and CPI; why MIPS and FLOPS mislead; benchmark suites (SPEC CPU, MLPerf) and summarising results with the geometric mean; microbenchmarking pitfalls (warm-up, turbo, the compiler removing your code, choice of timer); Amdahl and Gustafson; latency vs throughput; percentiles and tail latency; Little's law; roofline; CPI stacks and top-down; `perf stat` and flame graphs; dynamic vs static power, DVFS, C-states and P-states, thermal limits, energy efficiency | Core |  |
| 29 | Interconnection networks and warehouse-scale computing | Topologies (bus, ring, mesh, torus, hypercube, fat-tree, butterfly); routing and deadlock avoidance; flow control (store-and-forward, cut-through, wormhole, virtual channels); network-on-chip; racks, top-of-rack switches, PUE, tail latency | Optional |  |
| 30 | Hardware security | Side channels (timing, cache attacks such as Flush+Reload and Prime+Probe); transient execution (Spectre, Meltdown) and its mitigations and their performance cost (KPTI, retpolines); Rowhammer; constant-time code; NX, ASLR, pointer authentication, memory tagging; trusted execution (Intel SGX, ARM TrustZone) and confidential VMs; secure boot | Skim |  |
| 31 | ARM vs x86, and putting it together | Encoding and micro-ops; memory-model and atomics differences; SIMD families; power and performance positioning; porting pitfalls; x86 specifics (rings, MSRs, `cpuid`, SSE/AVX state); then end-to-end interview walkthroughs: running a program, a packet arriving, a pointer dereference, power-on to `main` (reset vector, firmware, bootloader, kernel), a system call, a context switch, a page fault, and `malloc` to DRAM | Core | Older note exists |

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

**Kept to a paragraph on purpose** (exam material with little interview value): Quine-McCluskey by hand, K-maps beyond 5 variables, EBCDIC, the Itanium story, quad-precision floats, nano-programming.

**Covered in other tracks instead:** operating-system memory management and page-replacement policy (`05_operating_systems`), embedded systems and RTOS (`13_embedded`), linking in depth (`02_c_language`), CUDA programming and GPU parallel patterns such as reduction, scan and histogram, plus FlashAttention and KV-cache sizing (`12_gpu_cuda`), storage engines and distributed storage (`11_hld`).

**Not planned:** chip-design and verification work (writing synthesizable RTL in SystemVerilog, UVM testbenches, timing closure). Tutorials 03 and 04 give the vocabulary only. If you target design or verification roles, that needs a separate track.

---

## Problems (about 4 per tutorial)

A mix of paper problems and hands-on ones. Here is the kind of problem to expect:

| # | Typical problems |
|---|---|
| 01 | Trace a program on the toy machine; iron-law arithmetic; read compiler output on godbolt; look up your own machine's cache sizes |
| 02 | Two's-complement and hex conversions by hand; spot the overflow; decode a float's bits; struct size with padding |
| 03 | Simplify a function with a K-map; build a 4-bit adder on paper; design a decoder |
| 04 | Trace a D flip-flop circuit; design a small counter; draw a state machine for a pattern detector |
| 05 | Multiply two numbers by Booth's method on paper; carry-lookahead delay; where the flags come from |
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
| 24 | Size a systolic array for a matrix multiply; what to offload to an accelerator and what not |
| 25 | Bandwidth of a PCIe link; arbitration order; address decoding for memory-mapped devices |
| 26 | Follow a packet from the NIC to your program; polling vs interrupt cost; DMA descriptor ring on paper |
| 27 | Count the memory references of a TLB miss under nested paging; what causes a VM exit; choose between virtio, SR-IOV and passthrough for a scenario |
| 28 | Amdahl and Little's law questions; read `perf stat` output; roofline placement |
| 29 | Bisection bandwidth of a topology; route a message; PUE arithmetic |
| 30 | Spot the Spectre gadget; how a cache timing channel works; what SGX and TrustZone protect |
| 31 | Port an x86 lock-free snippet to ARM on paper; the "what happens when..." interview answers |

---

## Questions you should be able to answer at the end

1. What is -1 as a 32-bit two's-complement value in hex? What does `(unsigned)-1 > 0` evaluate to?
2. Walk through what a 5-stage pipeline does with a load followed by a dependent add.
3. A 32 KiB, 8-way set-associative cache with 64-byte lines: how many sets, and which address bits are tag, index and offset?
4. Why is a linked list slower than an array of the same size, even when both fit in L2?
5. What is false sharing, and how would you find and fix it?
6. How does a NIC deliver a packet into memory without the CPU copying it?
7. A program spends 80% of its time in a loop you can parallelise perfectly. What is the best speedup on 16 cores?
8. Why does code that works on x86 sometimes break on ARM?
9. What happens, step by step, on a TLB miss? On a page fault?
10. What do register renaming and the reorder buffer each do?
11. What happens, step by step, from power-on until `main` runs?
12. What does a system call cost, and what does the hardware do when it executes one?
13. Why is a contended atomic counter slow, and how would you fix it?
14. Why can the same sum of floats give different answers after vectorising or going multithreaded?
15. A GPU version of a kernel is slower than the CPU version. Name three reasons.
16. Walk through what an out-of-order core does with a load that misses the L2 cache and the instructions behind it.
17. How does a GPU hide memory latency without large caches, and how do you tell whether a kernel is memory-bound?
18. What is a VM exit, and why does nested paging make a TLB miss more expensive?

---

## Older notes (still to be rewritten)

Three notes written before this plan sit in the `older_notes/` folder, so the numbers in this folder are only the new tutorials 01 to 31. The titles inside the older notes carry the old numbers. Each is rewritten in the new simple style when we reach its tutorial, and the old file is removed then.

| Older note | Becomes tutorial |
|---|---|
| [`older_notes/07_cache_coherence_mesi.md`](older_notes/07_cache_coherence_mesi.md) | 19 |
| [`older_notes/11_simd_and_vectorization.md`](older_notes/11_simd_and_vectorization.md) | 21 |
| [`older_notes/12_arm_vs_x86.md`](older_notes/12_arm_vs_x86.md) | 31 |

Other tracks link to the older notes at their `older_notes/` paths. When a note is rewritten, its old file is removed and the links in other tracks are updated to the new tutorial.

Already replaced and deleted: the old data-representation note (now tutorial 02), the old ISA-and-assembly note (now tutorials 06 and 07), the old branch-prediction note (now tutorial 10) and the old pipelines-and-speculation note (now tutorials 09, 11 and 12) and the old cache-hierarchy note (now tutorials 13 and 14).

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
| 1 Foundations | 01 to 05 | 01 to 04 written; 05 is optional and not written yet |
| 2 Instruction set and processor | 06 to 12 | 06 to 12 written, Part 2 is complete |
| 3 Memory system | 13 to 17 | 13 to 15 written; 16 is next; the rest are written one at a time when you say "next" |
| 4 Parallel hardware | 18 to 24 | |
| 5 System, performance and security | 25 to 31 | |

Track view: [`../00_MASTER_PLAN.md`](../00_MASTER_PLAN.md).
