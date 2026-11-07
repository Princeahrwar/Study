# CO 22: GPU Architecture I, the Execution Model

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 11](11_superscalar_and_out_of_order_execution.md), section 10.1 (latency chains and multiple accumulators), [tutorial 18](18_multicore_and_multithreading.md), sections 2 and 3 (Flynn's classes and hardware threads) and [tutorial 21](21_simd_and_vector_processing.md), sections 1 and 9.2 (lanes, masks instead of branches)*

By the end of this tutorial you can:

- explain why a GPU is a throughput machine and a CPU core a latency machine, and which resources each spends its silicon on;
- describe the SIMT model: threads grouped into warps (NVIDIA, 32) or wavefronts (AMD, 64 or 32), warps into thread blocks, blocks into a grid;
- map a thread index to its block, warp and lane, and say what the hardware does with a block (it goes whole to one streaming multiprocessor);
- describe a streaming multiprocessor and an AMD compute unit: sub-partitions, register file, lanes, shared memory (LDS), schedulers;
- explain how a warp scheduler issues one instruction per cycle from whichever warp is ready, and why switching warps costs nothing;
- compute the cost of divergence, and say what predication, reconvergence and the execution mask do;
- explain independent thread scheduling (Volta and later), why old "warp-synchronous" code broke, and use `__syncwarp`, `__shfl_down_sync` and `__ballot_sync`;
- use Little's law to say how many warps a GPU needs in flight, and why GPUs hide latency with threads instead of large caches;
- compute occupancy from register, shared-memory, block and warp limits, and explain why maximum occupancy is not the goal;
- translate between NVIDIA, AMD and OpenCL vocabulary, and read a small kernel in PTX and in AMD assembly;
- describe the graphics pipeline in one page and say which parts run on the programmable cores.

Tutorials 9 to 14 built a core that tries to finish one thread as early as possible: out-of-order execution, branch prediction, big caches; tutorial 21 added a vector unit with 8 or 16 lanes. A GPU starts from the opposite question: *what if there are a million independent pieces of work, and only the total time matters?* Then no single piece needs to be fast. The chip can drop the machinery that makes one thread fast (prediction, renaming, reordering, a deep cache hierarchy) and spend the silicon on arithmetic lanes and on a very large register file that holds thousands of threads at once. When one thread waits for memory, another is already running. This tutorial is how that works: the programming model, the hardware that matches it, the cost of branches, and the arithmetic that decides how many threads fit. Tutorial 23 continues with the GPU memory system, tensor cores and scaling out.

**Where the numbers come from.** **Nothing in this tutorial about GPU speed was measured by me.** The virtual machine of tutorials 14 to 21 has no GPU, and no CUDA toolkit. What it does have is clang 18, whose NVIDIA (NVPTX) and AMD (AMDGPU) back ends compile device code without hardware. So the **PTX and AMD assembly listings are real compiler output** (clang 18.1, targets `sm_80`, `gfx90a` and `gfx1100`), and the small host-side snippets, shown only for context, are marked as not run. Every hardware figure comes from a vendor document or a published measurement, and each source is linked where the number first appears: NVIDIA's [compute-capability tables](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html), the [Hopper tuning guide](https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html), the [CUDA programming guide](https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html), the Volta microbenchmarking paper by [Jia et al.](https://arxiv.org/pdf/1804.06826), a [Chips and Cheese article on H100](https://chipsandcheese.com/p/nvidias-h100-funny-l2-and-tons-of-bandwidth), and AMD's ROCm pages on [GPU specifications](https://rocm.docs.amd.com/en/docs-6.4.0/reference/gpu-arch-specs.html) and [hardware implementation](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html). The arithmetic (occupancy, divergence, Little's law) was checked with small Python scripts. Architectures change every generation; the *rules* in this tutorial are stable, the numbers belong to the generation named next to them.

> **The one-sentence model.** A GPU runs thousands of scalar-looking threads, grouped by the hardware into warps of 32 (or wavefronts of 64) that share one instruction stream and one program counter's worth of control, with an execution mask to let the lanes disagree; it hides memory latency by switching to another ready warp every cycle at no cost, which is possible only because a huge register file keeps every resident warp's state on chip, so performance depends on having enough independent warps (occupancy, limited by registers and shared memory), on branches that agree within a warp, and on data that the warp's lanes can fetch together.

---

## 1. Why GPUs exist

### 1.1 Two ways to be fast

A CPU core is built to make *one* instruction stream finish sooner. Everything in tutorials 9 to 14 serves that: it predicts branches so the front end never waits, it reorders instructions to find independent work, it keeps recent data in a cache of a few megabytes so that a load takes 5 cycles rather than 300. These tricks are expensive in silicon and power, and they pay only when a single thread has work that can be made faster.

A GPU is built to finish *a lot of threads* sooner. Suppose a program has a million independent pixels to shade. Make each thread 3 times faster and the program is 3 times faster; add 3 times as many lanes and it is 3 times faster as well, and lanes are much cheaper than the machinery for a faster thread. So the GPU spends its silicon on lanes, registers and memory bandwidth, and lets each thread run slowly.

| | CPU core (latency design) | GPU streaming multiprocessor (throughput design) |
|---|---|---|
| Goal | finish this thread soon | finish all the threads soon |
| Hides memory latency with | caches, prefetching, out-of-order execution | running another warp |
| Threads resident per core | 1 or 2 (SMT, tutorial 18) | up to 2048 (H100) |
| Lanes per instruction | 8 or 16 (AVX2, AVX-512) | 32 (a warp), or 64 on AMD CDNA |
| Branches | predict, speculate, flush on a miss (tutorial 10) | run both sides under a mask; no speculation |
| Registers | 16 to 32 architectural, a few hundred physical, per thread | 65,536 per SM shared by every resident thread, at most 255 per thread |
| Cache next to the core | 48 KB L1 data and 2 MB L2 on the VM of tutorials 14 to 21 | 256 KB per SM, shared between L1 and programmer-managed shared memory (H100) |
| Order of execution | out of order inside a thread | in order inside a warp |

The H100 figures are from NVIDIA's [Hopper tuning guide](https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html) (64K 32-bit registers per SM; a combined 256 KB of L1, texture and shared memory; at most 64 warps, i.e. 2048 threads, per SM).

### 1.2 What one thread gets

Divide an H100 SM's resources by its maximum number of resident threads:

```
registers per thread:        65,536 / 2,048  =  32
L1 + shared memory per thread: 256 KB / 2,048 =  128 bytes
```

A CPU thread has the whole 48 KB L1 and 2 MB L2 of a core of that VM to itself. The GPU thread has 128 bytes of nearby storage and 32 registers. It cannot depend on a cache to save it from a slow load, so it needs a different tactic: while its load is out, the SM runs other threads. Section 7 puts numbers on this.

A second consequence: the GPU wins only if the work really is parallel. A million independent threads is a good fit. A loop where each step needs the previous result (a linked-list walk, a recurrence, a parser state machine) gives the SM nothing to switch to, and then one slow GPU thread is much worse than one fast CPU thread.

### 1.3 Which jobs suit a GPU

| Fits well | Fits badly |
|---|---|
| tens of thousands of independent items: pixels, particles, matrix tiles, tokens | a serial dependence chain, one item at a time |
| the same operations on every item, few data-dependent branches | each item takes a different path (a parser, a tree search with irregular depth) |
| many arithmetic operations per byte, or a streaming job that needs the terabytes per second of HBM ([tutorial 16](16_dram_memory_controllers_and_numa.md)) | small data, or random access with no reuse |
| the data stay on the device across many kernels | the data are copied across PCIe, used once, copied back |

The last row is a classic interview question, so work it once with explicit assumptions. Add two 1 GB float arrays into a third, on a GPU across PCIe. Assume PCIe moves about 25 GB/s in practice in each direction (a PCIe 4.0 x16 link; [tutorial 25](25_buses_pcie_and_io_mapping.md) covers the real figure) and the GPU's memory delivers 2 TB/s:

```
copy in 2 GB + copy out 1 GB     = 3 GB / 25 GB/s   = 120 ms
kernel: reads 2 GB, writes 1 GB   = 3 GB / 2 TB/s    = 1.5 ms
CPU, memory bound at 24 GB/s      = 3 GB / 24 GB/s   = 125 ms     (24 GB/s: the one-core DRAM figure of tutorial 21)
```

The kernel is about 80 times faster than the CPU loop, and the whole job is no faster at all (121.5 ms against 125 ms), because the copy over the link costs 120 ms and the kernel costs 1.5. Moving data to the GPU pays only if the data are reused: many kernels on the same data, or an operation with many arithmetic steps per byte (a matrix multiply does n of them per element). Do not "port the hot loop" without counting the bytes that cross the link.

---

## 2. The SIMT model

### 2.1 Write the code for one thread

The programmer writes a function for **one** thread and launches it for many. Here is saxpy, the loop of [tutorial 21](21_simd_and_vector_processing.md), as a CUDA kernel:

```cuda
__global__ void saxpy(float *y, const float *x, float a, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;   // which element am I?
    if (i < n)                                       // the last block may run past the end
        y[i] = a * x[i] + y[i];
}
```

and, on the host (shown for context, not run here):

```cuda
int block = 256;                          // threads per block
int grid  = (n + block - 1) / block;      // round up so every element is covered
saxpy<<<grid, block>>>(d_y, d_x, 2.0f, n);
```

There is no loop over elements. The loop of tutorial 21 (strip-mine by 8, handle a tail) has become "launch `grid × block` threads, each does one element", and the tail is the `if (i < n)`. Compare with the intrinsic version of tutorial 21: there the programmer managed 8 lanes explicitly; here the programmer writes scalar code and the hardware groups 32 of the threads into one instruction stream. That grouping is the whole idea of **SIMT** (single instruction, multiple threads): the code looks like many independent threads, and the hardware runs it like SIMD.

AMD's HIP language is nearly identical (`__global__`, `blockIdx`, `threadIdx`, `<<<grid, block>>>`), and OpenCL and SYCL say the same with other words (section 10).

### 2.2 The hierarchy

| Level | NVIDIA (CUDA) | AMD (HIP) | OpenCL | Size |
|---|---|---|---|---|
| one piece of work | thread | work-item (thread) | work-item | |
| the lock-step group | **warp** | **wavefront** (wave) | sub-group | 32 threads on NVIDIA; 64 on AMD CDNA, 32 or 64 on RDNA |
| the cooperating group | **thread block** | workgroup / block | work-group | up to 1024 threads on NVIDIA |
| group of blocks (Hopper) | thread block cluster | | | up to 8 blocks portably |
| everything launched | **grid** | grid | NDRange | up to billions of threads |

AMD's wavefront size is 64 on its data-centre CDNA parts (MI100, MI250X, MI300X) and 32 on its RDNA graphics parts such as the RX 7900 XTX, according to the [ROCm specification table](https://rocm.docs.amd.com/en/docs-6.4.0/reference/gpu-arch-specs.html). The cluster level is from the [Hopper tuning guide](https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html) (maximum portable cluster size 8).

Three levels, three different guarantees. Keep these straight; they decide which code is correct:

| Level | What the hardware guarantees |
|---|---|
| **Warp** | its 32 lanes issue together; data can be exchanged between lanes by register shuffles and votes (section 6) |
| **Block** | all its threads run on **one** SM at the same time, can share on-chip shared memory, and can wait for each other at a barrier (`__syncthreads()`) |
| **Grid** | **nothing.** Blocks run in any order, some at once and some later, on any SM. A block must never wait for another block, because that block may not be running yet, and if no SM is free it never will be (a deadlock) |

That third row is why GPU code scales: the same grid runs on a small laptop GPU that holds 2 blocks at a time, and on a big one that holds 1000.

### 2.3 What the compiler makes of it

The PTX that clang 18 produces for `saxpy` (target `sm_80`, the A100's architecture; the build line is in section 11). I removed the directives, the register declarations and the parameter loads (`ld.param`, `cvta`) that only fetch the kernel arguments:

```
    mov.u32       %r3, %ctaid.x;               // blockIdx.x
    mov.u32       %r4, %ntid.x;                // blockDim.x
    mov.u32       %r5, %tid.x;                 // threadIdx.x
    mad.lo.s32    %r1, %r3, %r4, %r5;          // i = blockIdx.x * blockDim.x + threadIdx.x
    setp.ge.s32   %p1, %r1, %r2;               // p1 = (i >= n)
    @%p1 bra      $L__BB0_2;                   // if p1, jump to the end
    mul.wide.s32  %rd5, %r1, 4;                // byte offset of element i
    ...                                        // add the offset to the x and y pointers
    ld.global.f32 %f2, [%rd6];                 // x[i]
    ld.global.f32 %f3, [%rd7];                 // y[i]
    fma.rn.f32    %f4, %f2, %f1, %f3;          // x[i] * a + y[i], rounded once
    st.global.f32 [%rd7], %f4;                 // y[i] = ...
$L__BB0_2:
    ret;
```

Three things to see. The thread's identity is just three reads of **special registers** (`%tid.x`, `%ntid.x`, `%ctaid.x`). The index is one multiply-add. And the `if (i < n)` is an ordinary conditional branch, `@%p1 bra`: the thread-level view of the program is a normal sequential program, and the grouping into warps is not visible in the code at all.

### 2.4 Which warp, which lane

The hardware numbers the threads of a block with one linear index, with `x` varying fastest, then `y`, then `z`:

```
linear = threadIdx.z * (blockDim.x * blockDim.y) + threadIdx.y * blockDim.x + threadIdx.x
warp   = linear / 32
lane   = linear % 32
```

Worked example, a 16 × 16 block (256 threads, 8 warps):

| thread (x, y) | linear | warp | lane |
|---|---|---|---|
| (0, 0) | 0 | 0 | 0 |
| (15, 0) | 15 | 0 | 15 |
| (0, 1) | 16 | 0 | 16 |
| (5, 2) | 37 | 1 | 5 |
| (15, 15) | 255 | 7 | 31 |

So warp 0 holds rows 0 and 1 of the block, 16 threads wide each. Whether the 32 lanes of a warp then read neighbouring addresses depends on which array index each thread computes, and that decides memory speed ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)).

**A block size that is not a multiple of 32 wastes lanes.** Warps are made whole; the unused lanes of the last warp are masked off but still occupy the slots.

| Block size | Warps | Idle lanes in the last warp | Wasted lanes |
|---|---|---|---|
| 100 | 4 | 28 | 21.9% |
| 33 | 2 | 31 | 48.4% |
| 96 | 3 | 0 | 0% |
| 1000 | 32 | 24 | 2.3% |

Use multiples of 32 (typically 128 to 256).

---

## 3. The hardware: streaming multiprocessors and compute units

### 3.1 Inside an NVIDIA SM

A GPU is a few dozen to a few hundred identical **streaming multiprocessors** (SMs), a shared L2 cache and the memory controllers. The H100 SXM5 card enables 132 SMs of the full chip's 144, according to [Chips and Cheese](https://chipsandcheese.com/p/nvidias-h100-funny-l2-and-tons-of-bandwidth) (the PCIe card they tested enables 114). One SM is built from four copies of a smaller unit:

```
                        one SM (Hopper-style)
+------------------+------------------+------------------+------------------+
| sub-partition 0  | sub-partition 1  | sub-partition 2  | sub-partition 3  |
| warp scheduler   | warp scheduler   | warp scheduler   | warp scheduler   |
| (1 issue/cycle)  | (1 issue/cycle)  | (1 issue/cycle)  | (1 issue/cycle)  |
| register file    | register file    | register file    | register file    |
| 32 FP32 lanes    | 32 FP32 lanes    | 32 FP32 lanes    | 32 FP32 lanes    |
| INT32, FP64      | INT32, FP64      | INT32, FP64      | INT32, FP64      |
| tensor core      | tensor core      | tensor core      | tensor core      |
| load/store, SFU  | load/store, SFU  | load/store, SFU  | load/store, SFU  |
+------------------+------------------+------------------+------------------+
| 256 KB: L1 data cache and shared memory, split chosen per kernel          |
+---------------------------------------------------------------------------+
```

The four **sub-partitions** (NVIDIA calls them SMSPs) each have their own warp scheduler, their own slice of the register file, their own lanes and their own tensor core. They share the L1 and shared memory. According to Chips and Cheese, each H100 sub-partition has 32 FP32 units and can issue one FP32 warp instruction per clock, so one SM has 128 FP32 lanes. The [Volta paper](https://arxiv.org/pdf/1804.06826) found the same four-way split on V100 (four processing blocks, one dispatcher each), with warps assigned to a sub-partition by their warp number modulo 4. Tutorial 12 showed a CPU core with 4 to 6 wide issue; an SM is four *independent* narrow in-order machines, each issuing one warp-wide instruction per cycle.

### 3.2 Inside an AMD compute unit

AMD's equivalent is the **compute unit** (CU). On the data-centre CDNA parts, [AMD's documentation](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html) describes four SIMD units per CU, each with 16 single-precision lanes, so a 64-wide wavefront instruction takes **four cycles** to pass through a SIMD, one per 16 lanes. The CU has a sequencer that organises wavefronts into four pools of up to ten (eight on the MI200 generation), so up to 40 wavefronts can be resident, a vector register file (VGPRs) of 256 to 512 KiB per CU, a smaller scalar register file (SGPRs), and the **LDS** (local data share), the equivalent of shared memory: 64 KiB per CU on MI100, MI250X and MI300X.

RDNA, the graphics family, groups two CUs into a **work-group processor** (WGP), gives each CU two 32-wide SIMDs, and runs a 32-wide wavefront in one cycle.

The AMD CU has one thing the NVIDIA SM exposes less visibly: a **scalar unit** with its own registers, which runs the parts of the program that are the same for the whole wavefront (loop counters, base addresses, branch conditions that are uniform). You will see it in the assembly in section 5.1: the loop counters live in `s` registers, the data in `v` registers.

### 3.3 From a grid to the SMs

When you launch a grid, the GPU's block scheduler hands out **whole blocks** to SMs that have room:

- a block never splits across SMs, and never migrates;
- an SM can hold several blocks at once, as many as its resources allow (section 8);
- when a block finishes, its resources are freed and the next waiting block takes its place.

If every block takes the same time, the grid behaves like waves of blocks. With 132 SMs and room for one block per SM, a grid of 132 blocks finishes in one wave, but a grid of **133 blocks needs two waves, and the second wave has one block** while 131 SMs sit idle:

| Blocks in the grid | Waves (132 SMs, 1 block each) | Utilisation over the whole run |
|---|---|---|
| 132 | 1 | 100% |
| 133 | 2 | 50.4% |
| 265 | 3 | 66.9% |
| 1,000 | 8 | 94.7% |
| 10,000 | 76 | 99.7% |

This is **wave quantisation** (or the tail effect). It is an idealisation, because real blocks finish at different times and the scheduler refills SMs as they empty, but it explains why a kernel can slow down when the problem grows by 1%. Cures: launch many more blocks than the GPU can hold (thousands), so the last wave is small compared with the rest, or choose a block count that matches the SM count if the kernel is persistent.

### 3.4 What an SM can hold

NVIDIA publishes the per-SM limits by **compute capability** (the architecture version: 8.0 is the A100, 8.6 the GA10x chips of consumer cards and the A40, 9.0 the H100). From the [compute-capability tables](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html):

| Compute capability | Max resident blocks per SM | Max resident warps (threads) per SM | Shared memory per SM | Shared memory per block |
|---|---|---|---|---|
| 7.5 (Turing) | 16 | 32 (1024) | 64 KB | 64 KB |
| 8.0 (A100) | 32 | 64 (2048) | 164 KB | 163 KB |
| 8.6 (GA10x) | 16 | 48 (1536) | 100 KB | 99 KB |
| 9.0 (H100) | 32 | 64 (2048) | 228 KB | 227 KB |

For all of them: 64K 32-bit registers per SM, at most 64K per block and at most 255 per thread. Shared memory and L1 share one data array: the page lists a unified cache of 96 KB (7.5), 192 KB (8.0), 128 KB (8.6) and 256 KB (9.0), and the shared-memory column above is the largest carve-out from it; whatever is not shared memory acts as L1. Newer architectures (10.0 and 12.x) are in the same appendix. These limits are what section 8 turns into occupancy.

---

## 4. Issuing instructions: the warp scheduler

### 4.1 One instruction per scheduler per cycle

Each sub-partition holds many **resident warps**: an SM with room for 64 warps gives each of its four schedulers up to 16. Every cycle the scheduler of a sub-partition looks at its warps and picks **one warp that is eligible**, then issues that warp's next instruction to all 32 lanes. A warp is eligible when

- its next instruction has been fetched;
- the registers it reads are ready (a **scoreboard** tracks the outstanding loads and long-latency results);
- the functional unit it needs is free;
- it is not waiting at a barrier.

The policy that chooses among eligible warps is not published (the research literature describes round-robin, oldest-first and mixtures; do not rely on any of them). The point is what happens when a warp is *not* eligible: its slot is simply given to another warp, in the next cycle, for free.

Compare with tutorials [11](11_superscalar_and_out_of_order_execution.md) and [12](12_inside_a_modern_core.md). A CPU core issues from one instruction stream and needs reordering hardware to find independent instructions inside it. A GPU sub-partition issues in order within a warp, and finds independence **between** warps, which are independent by construction. That is why the scheduler can be so small.

### 4.2 Hiding a short latency with warps

Suppose a dependent chain of fused multiply-adds, where each needs the previous result. The [Volta microbenchmarks](https://arxiv.org/pdf/1804.06826) measured a dependent-issue latency of **4 cycles** for `FFMA` and `FADD` on V100; I use 4 cycles as the illustration for this tutorial (newer chips differ in detail). One warp alone can issue one FMA, then must wait:

```
cycle:        0    1    2    3    4    5    6    7    8
warp A:      FMA   .    .    .   FMA   .    .    .   FMA
issue slot:  busy idle idle idle busy idle idle idle busy      -> the FMA unit is 25% used
```

With four warps resident in the sub-partition, the scheduler fills the gaps:

```
cycle:        0    1    2    3    4    5    6    7    8
warp A:      FMA   .    .    .   FMA   .    .    .   FMA
warp B:       .   FMA   .    .    .   FMA   .    .    .
warp C:       .    .   FMA   .    .    .   FMA   .    .
warp D:       .    .    .   FMA   .    .    .   FMA   .
issue slot:   A    B    C    D    A    B    C    D    A      -> 100% used
```

No warp ran faster. Each still takes 4 cycles per dependent step. The sub-partition as a whole issues one instruction every cycle. This is **latency hiding by multithreading**, and it is the same law as the accumulators of tutorials [11](11_superscalar_and_out_of_order_execution.md) and [21](21_simd_and_vector_processing.md): *independent operations needed = latency × throughput*. There the independent operations were accumulators inside one thread; here they are warps.

### 4.3 Two ways to supply the independence

Independence can come from more warps (thread-level parallelism, TLP) or from more independent instructions inside each thread (instruction-level parallelism, ILP). They trade off:

| Independent instructions per thread (ILP) | Warps needed per sub-partition | Warps needed per SM |
|---|---|---|
| 1 (a dependent chain) | 4 | 16 |
| 2 | 2 | 8 |
| 4 | 1 | 4 |

(Latency 4 cycles, one warp instruction issued per cycle per sub-partition, four sub-partitions.) A thread that has four independent FMAs in flight needs only a quarter of the warps. This matters because warps cost registers (section 8): a kernel that uses many registers per thread to hold independent work can run at low occupancy and still keep the pipelines full. High-performance matrix-multiply kernels are written this way, as far as I know.

The same applies to loads, where the latencies are much longer. A thread that loads four independent values before using any of them has four loads in flight, not one. Wider loads do the same with one instruction. This kernel loads 16 bytes per thread:

```cuda
typedef float float4_t __attribute__((ext_vector_type(4)));   // CUDA has the type float4
__global__ void scale4(float4_t *y, const float4_t *x, float a)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    y[i] = a * x[i];
}
```

and compiles to a single 16-byte load and store (clang 18, `sm_80`):

```
    ld.global.v4.f32  {%f2, %f3, %f4, %f5}, [%rd6];     // four floats, one instruction
    mul.f32           %f6, %f1, %f5;                    // four multiplies
    ...
    st.global.v4.f32  [%rd7], {%f9, %f8, %f7, %f6};     // one 16-byte store
```

Four times the bytes in flight per thread, for one load instruction. Section 7 shows why this matters.

### 4.4 Issue rates

The issue slot is one limit; the functional unit's rate is another. From [Chips and Cheese's H100 measurements](https://chipsandcheese.com/p/nvidias-h100-funny-l2-and-tons-of-bandwidth): each sub-partition issues one FP32 warp instruction per clock, and an FP64 warp instruction every two clocks (every four on A100). A warp-wide instruction is a unit of 32 operations, so an SM with four sub-partitions peaks at 4 × 32 = 128 FP32 operations per cycle, or 256 flop per cycle with fused multiply-add.

On the AMD side the ROCm documentation describes a different split: a wavefront of 64 occupies a 16-lane SIMD for four cycles, and the issue stage can send **up to five instructions per cycle** from different wavefronts, one each of: vector ALU, vector memory, scalar ALU (or scalar memory), LDS, and branch. So one CU can be running a vector add, a memory load and a scalar loop-counter update in the same cycle, each for a different wavefront.

### 4.5 Why switching is free

On a CPU, a context switch saves registers to memory and restores another thread's: thousands of cycles for the operating system. Even SMT ([tutorial 18](18_multicore_and_multithreading.md)) can hold only two or so threads. On a GPU, **every resident warp's registers stay in the register file the whole time**: when a block is placed on an SM, the SM reserves its registers and shared memory for the block's lifetime. Switching warps is only a matter of the scheduler reading different register addresses on the next cycle. The cost is the size of the register file: 256 KB per SM on Hopper (64K × 4 bytes), 33 MB in total across the 132 SMs of an H100 SXM5. It is the register file, not the number of lanes, that sets how many threads can be resident.

---

## 5. Divergence, predication and reconvergence

### 5.1 One instruction, one path

All 32 lanes of a warp receive the same instruction. If a branch sends some lanes one way and some the other, the warp cannot go both ways at once. So it goes **one way after the other**, with the lanes that did not take that path switched off by a mask:

```
if (lane is odd) { A } else { B }

time:   [ A for odd lanes (even lanes masked off) ] [ B for even lanes (odd lanes masked off) ]
mask:    0101 0101 ... 0101                          1010 1010 ... 1010
```

**The warp's time is the sum of the paths that at least one lane takes**, not the longest and not the average. When the path lengths are 40 and 10 instructions and lanes disagree, the warp spends 50 instruction slots, though each lane did either 40 or 10 slots of useful work. The masked-off lanes are idle but still occupy the slots.

Here is the real code. The kernel has a branch on lane parity with a loop of run-time length on each side:

```cuda
__global__ void two_way(float *y, const float *x, int na, int nb)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    float v = x[i];
    if (threadIdx.x & 1) { for (int k = 0; k < na; ++k) v = v * 1.0001f + 0.5f; }
    else                 { for (int k = 0; k < nb; ++k) v = v * 0.9999f - 0.5f; }
    y[i] = v;
}
```

In PTX (clang 18, `sm_80`) it is two ordinary loops guarded by branches; the hardware, not the code, deals with the disagreement:

```
    and.b32       %r19, %r17, 1;                    // threadIdx.x & 1
    setp.eq.b32   %p1, %r19, 1;
    ...
    @%p4 bra      $L__BB0_7;                        // even lanes go to the "else" loop
    bra.uni       $L__BB0_1;                        // odd lanes fall into the "then" loop
$L__BB0_4:                                          // the "then" loop body (unrolled by 8)
    fma.rn.f32    %f16, %f39, 0f3F800347, 0f3F000000;   // v = v * 1.0001f + 0.5f
    ...
$L__BB0_10:                                         // the "else" loop body
    fma.rn.f32    %f24, %f39, 0f3F7FF972, 0fBF000000;   // v = v * 0.9999f - 0.5f
    ...
$L__BB0_13:                                         // both paths meet here
    st.global.f32 [%rd9], %f39;
```

If `na = nb = 100`, a warp whose lanes all took the same side would run 100 iterations. This warp, with odd and even lanes mixed, runs **200**: 100 for the odd lanes while the even ones wait, then 100 for the even lanes.

The AMD compiler shows the mechanism in the instruction stream, because the AMD ISA exposes the mask. This is the same kernel for `gfx90a` (MI250X, wave64), trimmed and annotated:

```
    v_and_b32_e32        v0, 1, v0              ; lane parity, per lane
    v_cmp_eq_u32_e32     vcc, 1, v0             ; vcc = a 64-bit mask: one bit per lane that is odd
    s_and_saveexec_b64   s[2:3], vcc            ; s[2:3] = old exec; exec = exec & vcc  (odd lanes only)
    s_xor_b64            s[2:3], exec, s[2:3]   ; s[2:3] = the lanes that did not take this side
    s_cbranch_execz      .LBB1_4                ; if no lane is active, skip this side entirely
    ...                                         ; "then" loop: v_fma_f32 v1, v1, s4, 0.5
.LBB1_3:
    s_add_i32            s8, s8, -1             ; loop counter in a SCALAR register (same for all lanes)
    s_cmp_eq_u32         s8, 0
    v_fma_f32            v1, v1, s4, 0.5        ; the vector arithmetic: 64 lanes, only odd ones enabled
    s_cbranch_scc0       .LBB1_3                ; scalar branch on the scalar condition code
.LBB1_4:
    s_andn2_saveexec_b64 s[2:3], s[2:3]         ; exec = the other lanes; remember the ones just done
    s_cbranch_execz      .LBB1_8
    ...                                         ; "else" loop: v_fma_f32 v1, v1, s4, -0.5
.LBB1_8:
    s_or_b64             exec, exec, s[2:3]     ; reconverge: every lane is active again
```

Read it as three kinds of instruction. Registers with `v` are per-lane (vector). Registers with `s` are one value for the whole wavefront (scalar): the loop counters `s8` and `s9` live there because `na` and `nb` are the same for every lane, so the loop branch `s_cbranch_scc0` is a plain scalar branch. And `exec` is the 64-bit execution mask: the first half of the code sets it to the odd lanes, `s_andn2_saveexec_b64` switches it to the even lanes, and the final `s_or_b64` restores it. The RDNA build (`gfx1100`, wave32) is the same program with 32-bit masks (`exec_lo`, `s_and_not1_saveexec_b32`).

### 5.2 Predication: no jump at all

When both sides are short, the compiler does not branch. It computes both and selects, or it marks instructions with a **predicate** so that lanes whose predicate is false do nothing. This is the vector-mask idea of [tutorial 21](21_simd_and_vector_processing.md) (section 9.2) done per lane. Real compiler output for

```cuda
if (i < n) { float v = x[i]; y[i] = (v > 0.0f) ? v * 2.0f : v + 1.0f; }
```

is (`sm_80`):

```
    ld.global.f32 %f1, [%rd6];                 // v = x[i]
    setp.gt.f32   %p2, %f1, 0f00000000;        // p2 = (v > 0)
    selp.f32      %f2, %f1, 0f3F800000, %p2;   // f2 = p2 ? v : 1.0f
    add.f32       %f3, %f1, %f2;               // v + f2  ==  p2 ? 2v : v + 1
    st.global.f32 [%rd7], %f3;
```

There is no branch for the body, and the two arms were merged into one add (`v * 2` is `v + v`, and `v + 1` is `v + 1`, so `v + (p ? v : 1)` covers both). Every lane executes the same four instructions, so there is no divergence, at the price that both arms' work is always done. The compiler's choice follows the same rule as [tutorial 10](10_branch_prediction.md): predication wins when the bodies are short; a real branch wins when one side is long and rarely taken. On the GPU the rule is stronger, because a branch costs both sides whenever the warp disagrees.

(The only branch left in `saxpy` and in this kernel is the `i < n` guard. It diverges only in the last warp of the grid, and then only for that one warp.)

### 5.3 How rare is "rare"?

A branch taken by 1% of the threads sounds harmless. But divergence is a property of the **warp**, not of the thread. With probability *p* that a thread takes the expensive path A (40 slots) and the rest take B (10 slots), a warp of 32 independent threads is divergent with probability 1 − *p*³² − (1 − *p*)³², and then costs 50 slots:

| p(A) | Warps that diverge | Warp cost (slots) | Average useful work per thread (slots) | Lane efficiency |
|---|---|---|---|---|
| 0 | 0% | 10 | 10 | 100% |
| 1% | 27.5% | 21.0 | 10.3 | 49% |
| 5% | 80.6% | 42.3 | 11.5 | 27% |
| 10% | 96.6% | 48.6 | 13.0 | 27% |
| 50% | 100% | 50.0 | 25.0 | 50% |
| 100% | 0% | 40 | 40 | 100% |

(Computed by a short script; the efficiency is the average useful work per thread divided by the warp cost.) A path that only 1% of the threads take halves the efficiency, because more than a quarter of the warps contain at least one such thread, and each of them pays for the expensive path. At 5% to 10%, most warps pay in full. This is the opposite of the CPU intuition (tutorial 10): a CPU predictor handles a 99%-taken branch for free; a GPU warp does not, since it only needs one dissenter.

The condition that matters is whether lanes **in the same warp** disagree:

| Branch condition | Warp behaviour |
|---|---|
| depends on a kernel argument, `blockIdx` or a constant | all lanes agree: free |
| `threadIdx.x / 32` or `(threadIdx.x >> 5) & 1`, i.e. constant within each warp | all lanes of one warp agree: free (different warps may differ, which is fine) |
| `threadIdx.x % 2`, `threadIdx.x < 16` | lanes disagree inside every warp: divergent |
| data-dependent: `x[i] > 0`, a sparse-matrix row length, a tree depth | divergent unless the data are sorted or clustered so that neighbours agree |
| the tail guard `if (i < n)` | divergent in the last warp only |

### 5.4 Loops: the warp runs as long as its slowest lane

A data-dependent trip count is divergence in time. If lane *i* loops `trips[i]` times, the warp keeps issuing until the **maximum** is reached, and lanes that finished are masked. Take trip counts drawn uniformly from 1 to 64. The mean is 32.5, but the expected maximum of 32 draws is 62.5, so a lane does on average 32.5 slots of useful work for every 62.5 slots the warp spends, an efficiency of **52%**. A simulation of 65,536 threads in random order gave 52.0%. After **sorting** the work items by trip count, so that each warp holds neighbours, the same simulation gave 100%.

That is the standard cure for irregular loops, and it is worth remembering the shape: the cost of divergence is decided by how work items are assigned to lanes, so the fix is often a data layout or a sort, not a code change. Sparse-matrix kernels use row length binning; ray tracers sort rays by direction; particle codes sort by cell.

### 5.5 Reconvergence

After the branches, the lanes must come together again so the warp can run full-width. The point where they rejoin is chosen by the compiler (normally the end of the `if`, the post-dominator in compiler terms). Two generations of hardware do it differently:

- **Before Volta (compute capability below 7.0).** One program counter for the warp plus an active mask, and a hardware **stack** of (mask, reconvergence address) entries. A divergent branch pushes entries; reaching the reconvergence point pops one. The PTX for `two_way` above contains no instruction marking the reconvergence: the assembler `ptxas` works it out when it turns PTX into the machine code.
- **Volta and later.** Each thread has its own program counter (section 6), and the hardware keeps **convergence barriers**. In disassembled machine code (SASS) you see `BSSY` (set a barrier before a divergent region) and `BSYNC` (wait there until the participating threads arrive); I have not generated SASS on this VM, so this is from published disassemblies. The effect for performance is the same as before: sequential paths, then rejoining.

For performance, it is enough to know that the warp pays for every path with a live lane, and rejoins at the end of the region. Code placed after the reconvergence point runs full width again.

### 5.6 Compared with a CPU

| | CPU (tutorial 10) | GPU |
|---|---|---|
| A branch the lanes or the predictor get wrong | a flush: roughly 12 to 20 cycles lost for that thread | no flush: both paths issue in turn |
| Speculation | yes, the front end runs ahead on a guess | none: a warp waits for its branch, and other warps run meanwhile (as far as the public documentation says; there is no predictor to tune) |
| Cost of a branch that is 99% one way | almost nothing | up to a halving of efficiency if the lanes disagree 1% of the time per thread (section 5.3) |
| Cost of a data-independent branch | almost nothing | nothing if warp-uniform |

So the GPU has no misprediction penalty to lose, and in exchange it has a divergence penalty that depends on the data. Control flow that is regular in space (neighbouring threads agree) is cheap on a GPU; control flow that is irregular is expensive however well a CPU would predict it.

### 5.7 Ways to avoid divergence

1. **Make the condition warp-uniform.** Organise the data so that all threads of a warp take the same side (process "interior" and "boundary" elements in separate kernels or separate blocks).
2. **Sort or bin work** by the quantity that decides the branch or the trip count (section 5.4).
3. **Compute both and select** when the arms are short (the compiler does it for you).
4. **Move the branch up** to the block or kernel level: the grid is scheduled per block, and a block that all agree is free.
5. **Do not hide long work in a rare branch.** If 1% of the items need 100 times the work, give them their own kernel or queue, so that the other 99% do not wait.

---

## 6. Independent thread scheduling and warp-level synchronisation

### 6.1 What Volta changed

Before compute capability 7.0 a warp had **one** program counter and an active mask, as in section 5.5. All 32 threads shared one position in the program. A thread on the side of a branch that was not currently running could not make progress at all, however long the other side took.

Since compute capability 7.0 (Volta), says the [CUDA programming guide](https://docs.nvidia.com/cuda/cuda-programming-guide/03-advanced/advanced-kernel-programming.html), **each thread has its own program counter and call stack**, and threads can diverge and reconverge at sub-warp granularity, with full concurrency between threads regardless of warp. The hardware still issues one instruction for a group of threads that are at the same place (it executes the lock-step groups of section 5), but it may switch between divergent groups instead of finishing one first.

Two consequences matter in practice.

**1. A thread can wait for another thread of its own warp.** This lock is the standard example (illustrative; not compiled or run here):

```cuda
while (atomicCAS(&lock, 0, 1) != 0) { }   // spin until we own the lock
/* critical section */
atomicExch(&lock, 0);                      // release
```

If two lanes of the same warp contend, one wins the compare-and-swap and the other spins. On a pre-Volta GPU the hardware could keep executing the spinning side, because the winner is on the other side of the divergence and is not scheduled until the spinners finish, so the warp could hang. On Volta and later the scheduler may interleave the groups and the winner proceeds. (It is still a poor idea on a GPU, since 32 lanes contending for one lock serialise, but it no longer deadlocks.)

**2. Code that assumed lock-step execution can break.** The old way to write the last steps of a reduction in a block was to run 32 threads on a shared-memory array with no synchronisation, because "a warp is lock-step":

```cuda
// WRONG on Volta and later: no guarantee that the lanes are converged between the steps
if (t < 32) { s[t] += s[t + 16]; s[t] += s[t + 8]; s[t] += s[t + 4]; s[t] += s[t + 2]; s[t] += s[t + 1]; }
```

The guide's advice is that such code may no longer work, and that you should synchronise explicitly with `__syncwarp()`. The right fix is either `__syncwarp()` between the steps, or the shuffle version of section 6.3, which needs no shared memory at all.

### 6.2 The `_sync` intrinsics and the mask

The warp-level functions take a 32-bit **mask** that names the lanes taking part. All the named lanes must execute the same call with the same mask; the mask is the programmer's statement of who is supposed to be there, which is exactly what independent scheduling no longer guarantees by itself.

| Function | Meaning | PTX |
|---|---|---|
| `__syncwarp(mask)` | the named lanes wait for each other; memory writes before it are visible after it | `bar.warp.sync` |
| `__shfl_sync(mask, v, src)`, `__shfl_down_sync(mask, v, d)`, `__shfl_xor_sync`, `__shfl_up_sync` | read register `v` from another lane | `shfl.sync.*` |
| `__ballot_sync(mask, pred)` | a 32-bit word: bit *k* is the predicate of lane *k* | `vote.sync.ballot.b32` |
| `__any_sync`, `__all_sync` | is the predicate true for any/all of the lanes | `vote.sync.any/all` |
| `__activemask()` | which lanes happen to be executing right now (a snapshot; do not use it to decide who participates) | |

The compiler output for the three used below (clang 18, `sm_80`; `-1` is `0xffffffff`, all lanes):

```
    bar.warp.sync         -1;                     // __syncwarp(0xffffffff)
    shfl.sync.down.b32    %f3, %f2, 16, 31, -1;   // value from lane (my lane + 16), 31 = the width mask
    vote.sync.ballot.b32  %r1, %p1, -1;           // one bit per lane's predicate
```

### 6.3 A warp reduction, by hand

Sum the 32 values held one per lane. The shuffle version halves the distance each step: lane *i* adds the value of lane *i + 16*, then *i + 8*, and so on:

```cuda
v += __shfl_down_sync(0xffffffff, v, 16);
v += __shfl_down_sync(0xffffffff, v,  8);
v += __shfl_down_sync(0xffffffff, v,  4);
v += __shfl_down_sync(0xffffffff, v,  2);
v += __shfl_down_sync(0xffffffff, v,  1);
// lane 0 now holds the sum of all 32 values
```

Trace it with 8 lanes (three steps) and the values 3, 1, 4, 1, 5, 9, 2, 6:

```
lane:             0   1   2   3   4   5   6   7
start:            3   1   4   1   5   9   2   6
after delta=4:    8  10   6   7   .   .   .   .      (lane i + lane i+4; lanes 4 to 7 hold unused partial values)
after delta=2:   14  17   .   .   .   .   .   .
after delta=1:   31   .   .   .   .   .   .   .      (3+1+4+1+5+9+2+6 = 31)
```

The compiler turned the five lines for 32 lanes into five `shfl.sync.down` and five `add.f32` instructions, with no shared-memory traffic and no barrier. The shared-memory version needs a store, a load and a synchronisation per step. A block-wide sum is then: each warp reduces with shuffles, lane 0 writes its result to a small shared array (one value per warp), one `__syncthreads()`, and the first warp reduces the 8 or 32 partial sums with shuffles again.

`__ballot_sync` is the other workhorse. If every lane asks "do I have an element to emit?", then `__popc(ballot)` is the number of elements the warp emits, and `__popc(ballot & lanemask_lt)` is each lane's output position within the warp. One lane can then do one `atomicAdd` of that count for the whole warp instead of 32 atomics (warp-aggregated atomics, the standard way to append to a global queue).

### 6.4 The block barrier

`__syncthreads()` waits until **every thread of the block** has reached it, and compiles to `bar.sync 0`. Here is the smallest useful case: each thread writes one element into an array in **shared memory** (memory private to the block, [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)), and then reads its neighbour's element, which another warp may not have written yet:

```cuda
__global__ void neighbour(float *y, const float *x)
{
    __shared__ float tile[256];
    int t = threadIdx.x;
    tile[t] = x[blockIdx.x * 256 + t];
    __syncthreads();                                   // wait until every thread has written
    y[blockIdx.x * 256 + t] = tile[(t + 1) & 255];     // now it is safe to read a neighbour's element
}
```

The compiler output (`sm_80`):

```
    st.shared.f32  [%rd9], %f1;       // tile[t] = x[...]
    bar.sync       0;                 // wait for the whole block
    ld.shared.f32  %f2, [%rd11];      // tile[(t + 1) & 255]
```

The rule: **every thread of the block must reach the same barrier.** A `__syncthreads()` inside an `if` is allowed only when the condition is the same for the whole block; otherwise some threads wait for ones that will never arrive and the kernel hangs or misbehaves. While a warp waits at a barrier it is not eligible, so if all the warps of the SM's only block wait on each other and on memory, nothing is left to hide the latency. That is one reason to have two or more blocks resident per SM: when one block is at its barrier, the other's warps still run.

### 6.5 On AMD

A wavefront executes in lock step under its `exec` mask (section 5.1); cross-lane exchange is done by dedicated instructions (data-parallel primitives, swizzles and permutes), and HIP offers `__shfl`, `__shfl_down` and `__ballot` as the equivalents. Older ROCm releases had no mask argument; check your ROCm version for the `_sync` forms. The block barrier is `s_barrier`. As on NVIDIA, a barrier must be reached by every wavefront of the workgroup.

---

## 7. Hiding latency with threads instead of caches

### 7.1 The latencies

Jia et al. measured an NVIDIA V100 with microbenchmarks. Their results, in core clock cycles, with the equivalent at the V100's 1.455 GHz boost clock (the clock is from [Tom's Hardware's launch article](https://tomshardware.com/news/nvidia-tesla-v100-volta-gpu,34379.html)):

| Access | Latency (cycles) | About |
|---|---|---|
| dependent `FFMA` | 4 | 3 ns |
| shared memory, no bank conflict | 19 | 13 ns |
| L1 data cache hit | 28 | 19 ns |
| L2 hit | about 193 | 133 ns |
| global memory (mean, PCIe card) | 391 | 269 ns |
| global memory, L2 and TLB miss (first touch) | 1029 | 707 ns |

A global load costs about a hundred times a floating-point add, and a thread that issues one must not stop the SM. The CPU of tutorial 16 sees 100 to 133 ns for a DRAM access, and covers it with caches and out-of-order execution; the GPU's global latency is two to three times longer and it covers it with other warps.

### 7.2 Little's law, per SM

[Tutorial 16](16_dram_memory_controllers_and_numa.md) (section 7) gave the law: bytes in flight = bandwidth × latency. For a V100 with 80 SMs and 900 GB/s ([Tom's Hardware](https://tomshardware.com/news/nvidia-tesla-v100-volta-gpu,34379.html)):

```
latency            = 391 cycles / 1.455 GHz   = 269 ns
bytes in flight    = 900 GB/s * 269 ns        = 236 KiB for the whole chip
per SM             = 236 KiB / 80             = 3,023 bytes
as warp loads      = 3,023 B / 128 B          = 23.6 loads of one warp, 4 bytes per lane
```

So each SM needs about **24 warps with a load outstanding at all times** to keep the memory system busy, out of the 64 it can hold. Fewer than that and the bandwidth falls in proportion; the arithmetic units wait on memory whatever their own latency.

Now the data-centre card of tutorial 16: 3.35 TB/s at 500 ns loaded needs 1.7 MB in flight. A full H100 SXM5 holds 132 SMs × 2048 threads = **270,336 threads**. If each has one 4-byte load outstanding, that is 1.08 MB, **not enough**. Each thread needs about two independent loads in flight, or one 8 or 16-byte load (`float2`, `float4`: section 4.3). So memory-bound GPU code needs both full occupancy *and* several loads in flight per thread, or it leaves bandwidth unused.

The same chip's arithmetic gives the balance point. V100: 80 SMs × 64 FP32 lanes × 2 flop (FMA) × 1.455 GHz = 14.9 Tflop/s, over 900 GB/s is **16.6 flop per byte**, or 66 floating-point operations for every 4-byte float loaded. A kernel with fewer operations per byte than that is limited by memory, not arithmetic (the roofline of [tutorial 12](12_inside_a_modern_core.md), section 9). Saxpy does 2 flop per 12 bytes moved: very far below.

### 7.3 Why the GPU does not rely on large caches

Section 1.2 gave 128 bytes of L1 and shared memory per resident thread. A cache that small cannot capture a thread's working set, and it does not need to: the design assumes reuse comes from **explicit shared memory** staged by the programmer ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)) and from **latency tolerance** through other warps. The consequences for your code:

- do not count on temporal locality inside one thread the way you would on a CPU;
- put data that a whole block reuses into shared memory yourself;
- keep enough warps resident, which is the next section.

---

## 8. Registers, shared memory and occupancy

### 8.1 What decides how many warps fit

Section 7 said an SM needs enough warps resident. How many it gets is set by four limits, all per SM:

| Limit | Where it comes from |
|---|---|
| **warps (threads)** | a fixed number of warp slots: 64 warps (2048 threads) on A100 and H100, 48 on GA10x |
| **blocks** | a fixed number of block slots: 32 on A100 and H100, 16 on GA10x |
| **registers** | 65,536 32-bit registers per SM, shared by all resident threads; a thread uses what the compiler assigned (at most 255) |
| **shared memory** | a block asks for some bytes; the SM has 164 KB (A100), 228 KB (H100), 100 KB (GA10x) to give, and CUDA reserves 1 KB per block |

Limits from the [compute-capability tables](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/compute-capabilities.html) and the [Hopper tuning guide](https://docs.nvidia.com/cuda/hopper-tuning-guide/index.html) (1 KB reserved per block). **Occupancy** is the number of resident warps divided by the maximum for the SM. The block scheduler places a block on an SM only if *all four* limits allow it, so the number of resident blocks is the minimum of four quotients:

```
blocks by warps     = floor( max_warps / warps_per_block )
blocks by blocks    = max_blocks
blocks by registers = floor( 65536 / (registers_per_warp * warps_per_block) )     registers_per_warp = threads (32) * regs per thread, rounded up to a multiple of 256
blocks by shared    = floor( shared_per_SM / (shared_per_block + 1 KB) )
resident blocks     = minimum of the four          occupancy = resident blocks * warps_per_block / max_warps
```

I round registers per warp up to 256 and shared memory per block up to 128 bytes, the allocation units of recent architectures. Real hardware has more detail (granularities differ between architectures), so for a real kernel use the occupancy calculator or API that NVIDIA ships (`cudaOccupancyMaxActiveBlocksPerMultiprocessor`) or Nsight Compute, which reports theoretical and achieved occupancy. The model here is exact enough for estimates and for interviews.

### 8.2 Registers

An A100 SM (compute capability 8.0), 256-thread blocks (8 warps), no shared memory. The only thing that changes is the registers per thread:

| Registers per thread | Registers per warp | Registers per block | Blocks that fit | Resident warps | Occupancy |
|---|---|---|---|---|---|
| 32 | 1,024 | 8,192 | 8 | 64 | 100% |
| 40 | 1,280 | 10,240 | 6 | 48 | 75% |
| 64 | 2,048 | 16,384 | 4 | 32 | 50% |
| 96 | 3,072 | 24,576 | 2 | 16 | 25% |
| 128 | 4,096 | 32,768 | 2 | 16 | 25% |
| 168 | 5,376 | 43,008 | 1 | 8 | 12.5% |
| 255 | 8,160 → 8,192 | 65,536 | 1 | 8 | 12.5% |

(Computed with the formula above by a script.) Work one row: 64 registers × 32 threads = 2048 per warp; 8 warps per block = 16,384; 65,536 / 16,384 = 4 blocks; 4 × 8 = 32 warps, half of the 64.

Two things stand out. Occupancy falls in **steps**: 32 registers give 100% and 33 give 75%; 64 give 50% and 65 give 37.5%; 96 and 128 both give 25%, and 129 gives 12.5%. So a kernel at 97 registers can grow to 128 for free, and a kernel just above a step (33, 41, 49, 65 or 129 here) is the one to look at, since one register fewer buys back a whole step. The compiler's register count is the thing to watch, and it is not in the PTX (PTX has unlimited virtual registers; `ptxas` assigns the real ones). Print it with `nvcc -Xptxas -v` or `--resource-usage`.

If a kernel needs too many registers, the compiler **spills** the extra values to *local memory*, which despite the name is private per-thread space in ordinary global memory, cached in L1 and L2. Spills turn register accesses into memory accesses. Two controls exist: `-maxrregcount=N` for a whole file, and `__launch_bounds__(maxThreadsPerBlock, minBlocksPerSM)` on a kernel, which tells the compiler how many blocks you want resident so that it keeps registers low enough. Whether the extra occupancy beats the spills is for measurement to decide.

### 8.3 Block size and shared memory

The same SM, 32 registers per thread, only the block size changes:

| Threads per block | Warps | Blocks that fit | Resident warps | Occupancy | Limit |
|---|---|---|---|---|---|
| 32 | 1 | 32 | 32 | 50% | block slots (32) |
| 64 | 2 | 32 | 64 | 100% | |
| 96 | 3 | 21 | 63 | 98.4% | warps (64 / 3) |
| 128 to 1024 | 4 to 32 | 16 down to 2 | 64 | 100% | |

A one-warp block is capped by the block limit at half occupancy. And a very large block has a different cost: it must fit whole, and a barrier makes all its warps wait for the slowest.

Now shared memory. 256-thread blocks, 32 registers, increasing shared memory per block, A100 against H100:

| Shared memory per block | A100: blocks, occupancy | H100: blocks, occupancy |
|---|---|---|
| 0 to 16 KB | 8, 100% | 8, 100% |
| 32 KB | 4, 50% | 6, 75% |
| 48 KB | 3, 37.5% | 4, 50% |
| 64 KB | 2, 25% | 3, 37.5% |
| 100 KB | 1, 12.5% | 2, 25% |

The jump from 16 to 32 KB takes an A100 from 100% to 50%. A kernel that stages a big tile in shared memory pays for it in resident warps: the shared-memory design (large tiles, high reuse) and the latency-hiding design (many warps) compete for the same SM. On Hopper the carve-out can be set between 0 and 228 KB per SM; a block can use at most 227 KB, and a static allocation is limited to 48 KB (larger needs a dynamic allocation with an explicit opt-in), as the Hopper tuning guide states.

### 8.4 Occupancy is a means, not the goal

More warps are better only while they are needed to hide latency. Section 4.3 showed a thread with four independent FMAs needs only four warps per SM (6% occupancy) to keep the FMA pipe full. Section 7 showed that a memory-bound kernel on a V100 needs about 24 loads in flight per SM, i.e. 37% of the warp slots with one 4-byte load each, and a quarter of that with 16-byte loads. After that point extra warps buy nothing, and they cost registers per thread, which can mean more spills or less ILP. Vasily Volkov's well-known talk "Better Performance at Lower Occupancy" (GTC 2010) made this point, and, as far as I know, high-performance matrix-multiply kernels are typically written for low occupancy (well under half) with 128 or more registers per thread.

A sensible order:

1. Choose a block size that is a multiple of 32, usually 128 or 256.
2. Read the register count, the shared-memory use and the theoretical occupancy; see which of the four limits binds.
3. If the kernel is memory-bound and occupancy is below about half, raise it (fewer registers, smaller tiles) or put more loads in flight per thread (unroll, vector loads).
4. If the kernel is compute-bound with plenty of ILP, low occupancy is fine. Look at the achieved occupancy too: it is lower than the theoretical when blocks are imbalanced or the grid has a tail (section 3.3).

### 8.5 The same logic on AMD

AMD's documentation lists the same four limits for a CU: VGPR (and SGPR) use, LDS per workgroup, the fixed number of wavefront slots, and the workgroup size. The [ROCm specification table](https://rocm.docs.amd.com/en/docs-6.4.0/reference/gpu-arch-specs.html) lists a 512 KiB vector register file for the MI300X (it does not name the unit; I read it as per CU, which matches four SIMDs of 128 KiB); per SIMD that is 128 KiB, i.e. 512 registers per lane (128 KiB / (64 lanes × 4 bytes)). If a wavefront uses *V* registers per lane, a SIMD holds `min(10, floor(512 / V))` wavefronts, taking the documented maximum of ten per pool from the [hardware implementation page](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html) (eight on MI200):

| VGPRs per wavefront | Wavefronts per SIMD | Per CU (4 SIMDs) | Share of the maximum (40) |
|---|---|---|---|
| 32 | 10 (capped; 512/32 = 16) | 40 | 100% |
| 64 | 8 | 32 | 80% |
| 96 | 5 | 20 | 50% |
| 128 | 4 | 16 | 40% |
| 256 | 2 | 8 | 20% |
| 512 | 1 | 4 | 10% |

(Allocation granularity and the split of the registers between the architectural and accumulation files are ignored.) LDS works as shared memory does: with 64 KiB per CU, a workgroup that uses 16 KiB lets four workgroups be resident.

---

## 9. SIMT against SIMD

[Tutorial 21](21_simd_and_vector_processing.md)'s SIMD and the GPU's SIMT look similar in the machine, so it helps to separate what the **hardware** does from what the **programmer** writes.

| | CPU SIMD (tutorial 21) | GPU SIMT |
|---|---|---|
| What the programmer writes | explicit vectors (intrinsics) or a loop for the auto-vectoriser | scalar code for one thread |
| Width | 4 to 16 lanes; a fixed ISA property (or length-agnostic: SVE, RISC-V V) | 32 lanes (NVIDIA, RDNA) or 64 (CDNA); visible but chosen by the hardware |
| A branch | the programmer writes a mask and a blend | the programmer writes `if`; the hardware masks and runs both sides |
| Memory access | a vector load reads contiguous bytes; a gather is a slow special case | each lane has its own address; contiguous lanes are fast, scattered lanes are slow ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)) |
| Tail of the loop | explicit (scalar tail, mask) | an `if (i < n)` guard |
| Cross-lane data | shuffles and permutes | shuffle intrinsics, or shared memory |
| Where the state lives | lanes of one vector register of one thread | each thread has its own registers; the warp is just 32 of them executing together |
| Latency hiding | out-of-order execution and caches | other warps |

One way to say it: **SIMT = SIMD lanes + hardware multithreading + per-lane masking and per-lane addresses**. The lane-wise arithmetic is identical. What changed is who deals with the awkward cases (branches, tails, different addresses): on a CPU the programmer or compiler, on a GPU the hardware.

That convenience has a price. A SIMD programmer sees a masked blend and knows it computes both sides; a SIMT programmer writes an `if` that looks cheap and may cost both sides (section 5.3). A SIMD programmer sees a gather instruction and expects it to be slow; a SIMT programmer writes `a[idx[i]]` and the same cost is hidden ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)). SIMT code is easy to write and easy to write slowly. The question to ask when reading a kernel is the SIMD one: *what do the 32 lanes do at this instruction, and do their addresses and conditions agree?*

AMD's ISA keeps a SIMD flavour in plain view. A wavefront instruction is `v_fma_f32` on 64 lanes, the mask is an ordinary register, and **scalar instructions** and registers carry the values that are the same for all lanes: loop counters, base pointers, kernel arguments (in the saxpy of section 11, the constant `a` sits in a scalar register `s0` and is used directly by the vector instruction). NVIDIA hardware has a similar idea (from Turing on, as far as I know, a uniform datapath and registers for values shared by the whole warp), but the programming interface hides it.

---

## 10. NVIDIA and AMD side by side

| Concept | NVIDIA (CUDA) | AMD (HIP / ROCm) | OpenCL / SYCL |
|---|---|---|---|
| processor core | streaming multiprocessor (SM) | compute unit (CU); RDNA pairs two into a work-group processor | compute unit |
| thread | thread | work-item / thread | work-item |
| lock-step group | warp, 32 | wavefront, 64 (CDNA), 32 or 64 (RDNA) | sub-group |
| cooperating group | thread block | workgroup / block | work-group |
| whole launch | grid | grid | NDRange |
| on-chip memory shared by a block | **shared memory** | **LDS** (local data share) | **local memory** |
| per-thread storage | registers; spills to "**local memory**" | VGPRs; spills to scratch | private memory |
| device memory | global memory | global memory | global memory |
| register file | 64K × 32-bit per SM | VGPRs (per lane) and scalar SGPRs, 256 to 512 KiB of VGPR per CU | |
| execution mask | hidden (active mask) | the visible `EXEC` register | hidden |
| block barrier | `__syncthreads()`, `bar.sync` | `__syncthreads()`, `s_barrier` | `barrier()` |
| matrix unit | tensor core | matrix core (MFMA instructions) | |
| virtual ISA | PTX | none comparable: LLVM IR, then native | SPIR-V |
| native ISA | SASS (not fully documented) | GCN / CDNA / RDNA (documented in AMD's ISA manuals) | |
| compiler | `nvcc`, clang | `hipcc`, clang | clang, vendor compilers |

The trap in the table: **"local memory" means opposite things.** In OpenCL it is the fast shared on-chip memory; in CUDA it is per-thread spill space in slow global memory. Say "shared memory" or "LDS" and "spill" to avoid confusion in an interview.

Other differences that matter when porting: AMD's wavefronts are twice as wide on CDNA, so one dissenting lane costs 64 lanes rather than 32 (section 5.3's probability becomes 1 − *p*⁶⁴ − (1 − *p*)⁶⁴), and the LDS has 32 banks on CDNA and 64 on RDNA2 and later, against 32 on NVIDIA (the bank rules are [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)'s).

---

## 11. Reading PTX and SASS

### 11.1 Two levels on NVIDIA

NVIDIA's compilers produce two kinds of code:

- **PTX** (parallel thread execution) is a *virtual* instruction set, written as text. It has typed virtual registers in unlimited number (`%r` 32-bit integers, `%rd` 64-bit, `%f` floats, `%p` predicates), explicit memory spaces (`ld.global`, `ld.shared`, `ld.param`) and no machine details. It stays stable across hardware generations.
- **SASS** is the real machine code of one architecture (`sm_80`, `sm_90`, ...). `ptxas` turns PTX into SASS. The instruction set changes with every generation and is only partly documented, which is why PTX exists: a binary can carry PTX, and the driver compiles it for whatever GPU it finds at load time (JIT).

AMD has no equivalent virtual ISA: the compiler goes through LLVM IR to the native code of a chosen target (`gfx90a`, MI250X; `gfx942`, MI300; `gfx1100`, RDNA3), and AMD documents the native ISA in public manuals.

### 11.2 Getting PTX without a GPU

The listings in this tutorial came from clang, using this stand-in for CUDA's header. It defines the few names the examples need (`mini_cuda.h`):

```cpp
// mini_cuda.h: just enough of the CUDA runtime header for clang to compile device code
// without the CUDA toolkit installed. Not for running anything; only for reading the PTX.
#define __global__ __attribute__((global))
#define __device__ __attribute__((device))
#define __shared__ __attribute__((shared))

struct __tid_x  { __device__ operator unsigned() const { return __nvvm_read_ptx_sreg_tid_x(); } };
struct __bid_x  { __device__ operator unsigned() const { return __nvvm_read_ptx_sreg_ctaid_x(); } };
struct __bdim_x { __device__ operator unsigned() const { return __nvvm_read_ptx_sreg_ntid_x(); } };
struct __threadIdx_t { __tid_x x; };
struct __blockIdx_t  { __bid_x x; };
struct __blockDim_t  { __bdim_x x; };
static __device__ const __threadIdx_t threadIdx = {};
static __device__ const __blockIdx_t  blockIdx  = {};
static __device__ const __blockDim_t  blockDim  = {};

#define FULL_MASK 0xffffffffu
__device__ inline float __shfl_down_sync(unsigned m, float v, int d) { return __nvvm_shfl_sync_down_f32(m, v, d, 0x1f); }
__device__ inline unsigned __ballot_sync(unsigned m, int p)          { return __nvvm_vote_ballot_sync(m, p); }
__device__ inline void __syncwarp(unsigned m)                        { __nvvm_bar_warp_sync(m); }
```

Put the kernel in a `.cu` file, include the header, wrap the kernels in `extern "C" { ... }` to get clean names, and compile with:

```
clang++ -x cuda --cuda-device-only --cuda-gpu-arch=sm_80 -nocudainc -nocudalib --cuda-path=/nonexistent \
        -Xclang -target-feature -Xclang +ptx70 -O2 -S -o saxpy.ptx saxpy.cu
```

(`__syncthreads()` is a clang built-in and needs no definition.) With a real CUDA toolkit the equivalent is `nvcc -ptx -arch=sm_80 saxpy.cu`. Change `sm_80` to `sm_70` or `sm_90` and the same source compiles for another generation (the PTX version number changes: 7.0 for `sm_80`, 7.8 for `sm_90`). For AMD, this header (`mini_hip.h`) plays the same role:

```cpp
// mini_hip.h: just enough for clang to compile HIP-style device code without ROCm installed.
// blockDim.x is fixed at 256 here (the real value comes from the dispatch packet).
#define __global__ __attribute__((global))
#define __device__ __attribute__((device))

struct __tid_x  { __device__ operator unsigned() const { return __builtin_amdgcn_workitem_id_x(); } };
struct __bid_x  { __device__ operator unsigned() const { return __builtin_amdgcn_workgroup_id_x(); } };
struct __bdim_x { __device__ operator unsigned() const { return 256; } };
struct __threadIdx_t { __tid_x x; };
struct __blockIdx_t  { __bid_x x; };
struct __blockDim_t  { __bdim_x x; };
static __device__ const __threadIdx_t threadIdx = {};
static __device__ const __blockIdx_t  blockIdx  = {};
static __device__ const __blockDim_t  blockDim  = {};
```

and

```
clang++ -x hip --cuda-device-only --offload-arch=gfx90a -nogpulib -nogpuinc -O2 -S -o saxpy_gfx90a.s saxpy.hip
```

emits the assembly. Change `gfx90a` to `gfx1100` for RDNA's wave32 code.

### 11.3 A PTX vocabulary

| PTX | Meaning |
|---|---|
| `mov.u32 %r5, %tid.x` | read a special register: `%tid.x` thread, `%ntid.x` block size, `%ctaid.x` block, `%nctaid.x` grid size (`cta` = cooperative thread array = thread block) |
| `mad.lo.s32 d, a, b, c` | d = a × b + c, low 32 bits (the index computation) |
| `setp.ge.s32 %p1, a, b` | set a predicate register from a comparison |
| `@%p1 bra L` | branch to L for the lanes whose predicate is true |
| `selp.f32 d, a, b, %p` | d = p ? a : b (a select: predication in data form) |
| `ld.global`, `st.global` | load, store in global memory (DRAM through L2 and L1) |
| `ld.shared`, `st.shared` | shared memory of the block |
| `ld.param` | read a kernel argument |
| `fma.rn.f32` | fused multiply-add, round to nearest even |
| `bar.sync 0` | block barrier (`__syncthreads`) |
| `bar.warp.sync m` | warp barrier (`__syncwarp`) |
| `shfl.sync.down.b32` | read a register of another lane |
| `vote.sync.ballot.b32` | collect a predicate from the lanes into one word |
| `ret` | end of the thread |

### 11.4 SASS, in a paragraph

You will meet SASS in profilers and disassemblers (`cuobjdump -sass`, `nvdisasm`, Nsight Compute's source view). I did not generate any on this VM (it needs NVIDIA's `ptxas`), so this table is from published disassemblies and NVIDIA's binary-utilities documentation, as a guide to what you will see:

| SASS mnemonic | Role |
|---|---|
| `S2R` | read a special register (thread and block indices) |
| `IMAD`, `IADD3`, `LEA` | integer arithmetic and address computation (`IMAD` is also used for moves) |
| `ISETP`, `FSETP` | set predicates; an `@P0` prefix predicates any instruction |
| `BRA`, `EXIT`; `BSSY`, `BSYNC` | branch, thread exit; convergence barrier set and wait (section 5.5) |
| `LDG`, `STG`; `LDS`, `STS` | global and shared memory loads and stores |
| `FFMA`, `FADD`, `FMUL` | floating point |
| `SHFL`, `VOTE`, `WARPSYNC`, `BAR` | warp shuffles and votes, warp and block barriers |
| `HMMA`, `IMMA` | tensor core matrix instructions ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)) |

Things PTX cannot tell you: the register count and spills (decided by `ptxas`), whether `ptxas` turned your branch into predicated instructions, and the real instruction schedule. For those, use `-Xptxas -v` and the SASS.

### 11.5 Reading AMD assembly

The AMD code of saxpy (`gfx90a`, MI250X, wave64), trimmed and annotated; the `s_` prefix means scalar (one value per wavefront), `v_` vector (one per lane):

```
    s_load_dwordx2       s[0:1], s[4:5], 0x10      ; scalar load of the kernel arguments (a and n)
    v_lshl_add_u32       v0, s6, 8, v0             ; i = (workgroup id << 8) + lane's thread id   (s6 = workgroup id)
    s_waitcnt            lgkmcnt(0)                ; wait for the scalar load
    v_cmp_gt_i32_e32     vcc, s1, v0               ; vcc = per-lane mask (n > i)
    s_and_saveexec_b64   s[2:3], vcc               ; exec = lanes with i < n
    s_cbranch_execz      .LBB0_2                   ; if no lane is active, skip
    ...                                            ; address computation
    global_load_dword    v2, v[2:3], off           ; x[i]
    global_load_dword    v3, v[0:1], off           ; y[i]
    s_waitcnt            vmcnt(0)                  ; wait for both vector loads
    v_fmac_f32_e32       v3, s0, v2                ; y = a * x + y   (a is in a SCALAR register)
    global_store_dword   v[0:1], v3, off           ; y[i] = ...
.LBB0_2:
    s_endpgm
```

Note the `s_waitcnt` instructions: the compiler inserts explicit waits for outstanding loads (`vmcnt` for vector memory, `lgkmcnt` for scalar loads and LDS), which is the programmer-visible form of the scoreboard of section 4.1.

---

## 12. The graphics pipeline on one page

GPUs were built for this, and several features of the SM (texture units, special-function units, 2 × 2 groups of lanes) exist because of it. The path of a triangle from the application to the screen:

| Stage | Fixed or programmable | What it does |
|---|---|---|
| Vertex fetch | fixed | reads vertices (position, normal, texture coordinates) from buffers |
| **Vertex shader** | programmable | runs once per vertex; transforms positions to clip space; runs as warps of 32 vertices |
| Tessellation, geometry or mesh shaders | programmable (optional) | create or amplify geometry |
| Primitive assembly, clipping, culling | fixed | groups vertices into triangles, discards what is off-screen or facing away |
| **Rasterisation** | fixed | finds which pixels each triangle covers and emits **fragments**, in 2 × 2 **quads** |
| **Fragment (pixel) shader** | programmable | runs once per fragment: lighting, texture lookups; runs as warps of 32 fragments |
| Texture sampling | fixed (in each SM) | fetches and filters texels (bilinear, mip-mapped) |
| Raster operations (ROP) | fixed | depth and stencil tests, blending, writes to the frame buffer, in API order per pixel |
| Display | fixed | scans the frame buffer out |

How this meets the SIMT model:

- **Shaders are kernels.** A vertex shader is "a thread per vertex", a fragment shader "a thread per pixel", and both run as warps on the SMs. A compute shader, or CUDA, is the same machinery without the fixed stages.
- **Quads.** The fragment shader needs derivatives (to choose the mip level of a texture), obtained by differencing neighbouring lanes in a 2 × 2 quad. A quad on the edge of a triangle may contain pixels outside it; those lanes still execute as **helper lanes** so the derivatives exist, and their results are discarded. Small triangles waste lanes this way.
- **Divergence in shading** appears at material boundaries and `if`s inside shaders; neighbouring pixels usually agree, so it is mild, and the hardware packs fragments of one triangle into the same warps.
- **Fixed functions stay fixed** where specialised hardware beats programmable cores by a large factor: rasterisation, texture filtering, ROP blending, and (since NVIDIA's Turing and AMD's RDNA 2) **ray-tracing units** that walk the bounding-volume tree and intersect triangles.

GPGPU began by treating a fragment shader as a general kernel and the frame buffer as the output array. CUDA and HIP removed the graphics detour; the SM's texture units, special-function units (reciprocal, square root, sine) and quad-based lane groups are what is left of that history.

---

## 13. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| A kernel runs several times slower than the arithmetic suggests; the profiler reports low warp execution efficiency | divergence: lanes of a warp take different paths or loop counts | make the condition warp-uniform; sort or bin the work; compute both sides and select; give rare long paths their own kernel |
| Run time jumps when the problem grows by a few percent | wave quantisation: the last wave of blocks is nearly empty | many more blocks than the GPU holds at once; balance the work per block |
| Theoretical occupancy is 25% or lower | registers per thread or shared memory per block | check `-Xptxas -v`; `__launch_bounds__`; smaller tiles; look for a threshold just above the current count |
| Registers cut by the compiler, then the kernel slows | spills to local memory | raise the limit back; restructure to hold fewer live values |
| A kernel hangs or gives wrong results only occasionally | `__syncthreads()` in a branch some threads skip, or a block waiting for another block | barriers outside divergent code; no inter-block waiting (use separate kernels or cooperative launch) |
| Correct on Pascal, wrong on Volta or later | warp-synchronous code that relied on lock step | `__syncwarp`, the `_sync` intrinsics with explicit masks, shuffles instead of shared-memory tricks |
| A reduction or scan is slow | shared memory and barriers for every step | warp shuffles inside a warp, one shared-memory step across warps |
| A memory-bound kernel reaches a fraction of the bandwidth | too few bytes in flight (low occupancy, one 4-byte load per thread) | raise occupancy, unroll for independent loads, 8 or 16-byte vector loads (and coalescing, [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)) |
| Many tiny kernels, little work in each | launch overhead and data dependence between launches dominate | fuse kernels; batch work; keep data resident on the device |
| The GPU version is no faster than the CPU's although the kernel is 50 times faster | copies over PCIe dominate | keep data on the device; count bytes across the link before porting |
| Block of 100 threads, or a grid of 20 blocks | idle lanes in the last warp; idle SMs | multiples of 32; at least several blocks per SM |
| Contended atomics are slow | all lanes hit one address | warp-aggregated atomics with `__ballot_sync` and `__popc`; per-block partial results |

---

## 14. Mistakes that cost points

1. **Calling a GPU thread a "core" or comparing it with a CPU thread.** It is one lane of a warp. A GPU advertised with 16,384 "CUDA cores" has, for example, 128 SMs of 128 FP32 lanes each; the number counts lanes, not independent processors.
2. **Treating divergence as a per-thread cost.** It is a per-warp cost: one dissenting lane in 32 makes the whole warp run both paths. A path taken by 1% of the threads can halve the efficiency (section 5.3).
3. **Putting `__syncthreads()` where only some threads of the block reach it.** The kernel hangs or misbehaves.
4. **Assuming a warp runs in lock step on Volta and later.** Use `__syncwarp()` and the `_sync` functions with correct masks; old "warp-synchronous" reductions are wrong.
5. **Using `__activemask()` to decide who participates in a shuffle.** It reports who happens to be converged right now, not who should be. Write the mask from the program's logic.
6. **Maximising occupancy.** It is needed to hide latency and no more; fast kernels often run at low occupancy with many registers and much ILP.
7. **Block sizes that are not a multiple of 32,** or a block of one warp (the block-slot limit halves the occupancy).
8. **Launching too few blocks.** A grid of 20 blocks keeps 20 of 132 SMs busy.
9. **Assuming blocks run at the same time or in order,** or making one block wait for another's result. If the second block is not resident, the first waits forever.
10. **Timing a launch without synchronising.** Kernel launches are asynchronous: the call returns when the work is queued. Time with events or synchronise, and discard the first launch (context creation, JIT).
11. **Quoting the speed-up of the kernel and forgetting the copies over PCIe** (section 1.3).
12. **Confusing "local memory".** In OpenCL it is shared on-chip memory; in CUDA it is per-thread spill space in global memory.
13. **Reading the register count from PTX.** PTX has virtual registers; `ptxas` decides the real count.
14. **Quoting an H100 number for every GPU.** Always say the generation and compute capability: 64 warps per SM is true of A100 and H100 and false of GA10x (48).

---

## 15. Questions you will be asked

**What is a warp, and how does it differ from a thread block?** A warp is the hardware's unit of execution: 32 threads that are fetched and issued together, each lane with its own registers (AMD's wavefront is 64, or 32 on RDNA). A thread block is the programmer's unit of cooperation: up to 1024 threads that run on one SM at the same time, share shared memory and can meet at a barrier. A block consists of warps; the warps of a block are scheduled independently, so they need `__syncthreads()` to coordinate.

**Why does a GPU have such a large register file and small caches?** Because it hides latency by switching between resident warps every cycle at no cost, which needs every resident thread's registers on chip: 64K registers per SM and 2048 threads. With that much parallelism a cache is less necessary, and its area is better spent on lanes. The per-thread share is small (32 registers and about 128 bytes of L1 and shared memory at full occupancy), so reuse is made explicit through shared memory.

**What is warp divergence and what does it cost?** When lanes of one warp take different sides of a branch, the warp executes each side in turn with the other lanes masked off, so the cost is the sum of the paths with a live lane. A branch that is uniform across the warp is free. The probability that a warp contains at least one dissenter grows quickly: with 5% of the threads taking a rare path, over 80% of the warps pay for it. Fixes are to make the condition warp-uniform, to sort or bin the work, to let the compiler predicate short bodies, and to move rare long paths into their own kernel.

**What is occupancy, what limits it, and is higher always better?** Occupancy is resident warps over the maximum for the SM. Four limits apply per SM: warp slots, block slots, registers (65,536 per SM) and shared memory; the number of resident blocks is the minimum of the four quotients. It is needed to hide latency (Little's law: bytes or instructions in flight equal latency times throughput), but beyond that it costs registers and shared memory, and instruction-level parallelism can replace warps. Aim for enough, check which limit binds, and measure.

**How does a GPU hide memory latency?** By having many warps resident and issuing from whichever is ready, so when one waits for memory another runs, with no context-switch cost because all the registers stay in the register file. Sustaining the memory bandwidth needs bandwidth times latency bytes in flight (about 3 KB per SM on a V100 at its 391-cycle latency, 24 warp-wide loads), which is why the threads should each have several loads outstanding.

**Explain SIMT and how it differs from SIMD.** SIMT is the programming model of one scalar thread, executed by hardware that groups 32 threads into one instruction stream. The lane arithmetic is SIMD, but the hardware handles branches by masking, lets each lane have its own memory address, and runs many warps to hide latency. In SIMD on a CPU the programmer or compiler manages the masks, the tail and the gathers. SIMT is simpler to write and equally able to be slow: divergence and scattered addresses are legal but not free.

**What changed with independent thread scheduling in Volta?** Before, a warp had one program counter and an active mask, so a lane on the not-running side of a branch made no progress, and intra-warp spin locks could deadlock. From compute capability 7.0 each thread has its own program counter and call stack, and the hardware may interleave divergent groups. Programs may no longer assume lock step: use `__syncwarp()` and the `_sync` warp functions with explicit masks.

**How are blocks assigned to SMs, and what may you assume?** The block scheduler gives whole blocks to SMs with enough free registers, shared memory and slots, as many per SM as fit, and fills freed slots with waiting blocks. A block never splits or migrates. You may assume nothing about the order in which blocks run, or about two blocks running together, so blocks must not wait for each other; you may assume that the threads of one block are co-resident and can synchronise.

**Write a warp-level sum.** Five steps of `v += __shfl_down_sync(0xffffffff, v, d)` with *d* = 16, 8, 4, 2, 1; lane 0 then holds the total. It needs no shared memory and no barrier. A block sum reduces each warp this way, lets lane 0 of each warp write its partial to a small shared array, synchronises once, and has the first warp reduce those partials the same way.

**NVIDIA warp of 32 against AMD wavefront of 64: what changes?** On a wavefront-64 machine one dissenting lane costs 64 lanes rather than 32, so divergence is more expensive; AMD's graphics parts (RDNA) use 32. Wavefront-64 hardware issues the instruction over four cycles on a 16-lane SIMD (CDNA's classic model). AMD exposes a scalar unit and the `EXEC` mask in the ISA, so uniform values and loop counters live in scalar registers. Code that assumes 32 (hard-coded masks, shuffle widths) must be written against the device's warp size.

**When would you not use a GPU?** When the work is serial or irregular (a dependence chain, a parser, deep data-dependent recursion), when the data are small, or when the data must cross PCIe for little work per byte: at 25 GB/s a gigabyte costs 40 ms in each direction, which a kernel at terabytes per second cannot win back unless the data are reused many times on the device.

---

## 16. Problems

**How to use this section.** Try each problem before you read the answers. A, B and C are paper problems (30 to 45 minutes each; B uses a short script you write); D is a compile-and-read exercise that needs a clang with the NVIDIA and AMD back ends but **no GPU**. Sections 5.1 to 5.4 give A; sections 8.1 to 8.5 give B; sections 2.4, 3.3 and 5.3 give C; sections 6, 11.2 and 11.3 give D.

### Problem A (paper): the cost of divergence

Every thread of a kernel executes 8 common instructions, then `if (x[i] > 0)` takes path A (30 instructions), otherwise path B (12 instructions). The warp issues one instruction per slot.

(a) How many slots does a warp need when all 32 lanes take A? All take B? Lanes are mixed? In the mixed case with exactly 16 lanes on each side, what fraction of the lane-slots do useful work?

(b) Now 3% of the values are positive, scattered at random (each thread independently takes A with probability 0.03). What is the probability that a warp is divergent, the expected warp cost in slots, and the lane efficiency (average useful slots per thread divided by the warp cost)? You will need 0.97³² ≈ 0.377.

(c) The array has 1,000,000 elements. Suppose you first sort it (or partition it) so that all positive values come first. About how many slots do the 31,250 warps need in total now, and how does it compare with the random layout?

(d) A second kernel loops `for (k = 0; k < len[i]; k++)` where `len[i]` is uniform on 1 to 64 (mean 32.5). The expected maximum of 32 such values is about 62.5. What is the lane efficiency? If you split the items into four bins by length (1 to 16, 17 to 32, 33 to 48, 49 to 64) and give each bin its own launch, what is it, using "expected maximum ≈ top of the range − 0.5" within a bin? What if the items are fully sorted?

*Where:* paper; sections 5.1 to 5.4. *How:* write the cost of each case first; then weight by probability. Use a calculator or a few lines of Python.

### Problem B (paper plus a short script): occupancy

A kernel is launched with 192 threads per block, uses 40 registers per thread and 20 KB of shared memory per block.

(a) For an A100 (compute capability 8.0: 64 warps, 32 blocks, 65,536 registers, 164 KB shared memory per SM), an H100 (9.0: 64 warps, 32 blocks, 228 KB shared) and a GA10x chip (8.6: 48 warps, 16 blocks, 100 KB shared), compute the number of resident blocks allowed by each of the four limits, the resident blocks, the resident warps, the occupancy and the limiting resource. Use the simplified model of section 8.1 (registers per warp rounded up to a multiple of 256; shared memory rounded up to 128 bytes plus 1 KB per block).

(b) On the A100, which single change gets the highest occupancy: shared memory down to 12 KB, registers down to 32, or both? Compute the three cases.

(c) Write the model as a short function (Python or C) and reproduce the 40-register row of the table in section 8.2 and the 32 KB row of the shared-memory table in section 8.3.

(d) An MI300X compute unit has 512 VGPRs per lane per SIMD, a documented maximum of 10 wavefronts per SIMD, and 64 KiB of LDS. A kernel with 96 VGPRs per wavefront and a workgroup of 256 threads (four wavefronts) uses 24 KiB of LDS per workgroup. How many workgroups fit per CU, which limit binds, and what fraction of the 40 wavefront slots is that?

*Where:* paper and a script; sections 8.1 to 8.5. *How:* compute each limit separately as a quotient, take the minimum, and always say which limit it was.

### Problem C (paper): from a thread index to a warp and an address

(a) A 1D kernel handles `n = 1000` floats with 128 threads per block. How many blocks must be launched? For `blockIdx.x = 3`, `threadIdx.x = 45`: what are the global index `i`, the warp number within the block, the lane, the warp number counted over the whole grid, and the byte offset of `x[i]` from the array base? Which warp contains the tail guard's divergence, and how many of its lanes are active?

(b) An image kernel uses a 2D block of (32, 8) threads on a float image `W = 1920` pixels wide, row-major, with `col = blockIdx.x * 32 + threadIdx.x` and `row = blockIdx.y * 8 + threadIdx.y`. For `threadIdx = (5, 3)` in block `(2, 1)`: what are `col` and `row`, the linear thread number in the block, the warp and lane, and the byte offset of the pixel? Which pixels does one warp cover? Repeat the question about the warp's coverage for a (16, 16) block, and give the distance in bytes between the two runs of consecutive addresses.

(c) For a block of 128 threads, say which of these branch conditions diverge, and in which warps: (i) `threadIdx.x % 32 < 8`; (ii) `(threadIdx.x / 32) % 2 == 1`; (iii) `threadIdx.x < 64`; (iv) `threadIdx.x < 48`; (v) `blockIdx.x % 2 == 0`.

(d) A grid has 1,500 blocks of 256 threads on a GPU with 132 SMs where each SM can hold 8 of these blocks at once. How many waves, and what is the utilisation over the run, under the idealised model of section 3.3? How many blocks would give 100% in two waves?

*Where:* paper; sections 2.4, 3.3 and 5.3. *How:* use the formulas `linear`, `warp = linear / 32`, `lane = linear % 32`; keep byte offsets as index × 4.

### Problem D (your machine, no GPU): read the compiler output

You need a clang with the NVPTX and AMDGPU back ends: check that `clang --print-targets` lists `nvptx64` and `amdgcn` (Linux distribution packages usually do; Apple's clang does not, but an LLVM from Homebrew may). If you cannot get one, use the listings in this tutorial, or Compiler Explorer with a CUDA compiler, and answer from them.

Save `mini_cuda.h` from section 11.2 and these kernels in a `.cu` file inside `extern "C" { ... }`:

```cuda
__global__ void saxpy(float *y, const float *x, float a, int n)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; if (i < n) y[i] = a * x[i] + y[i]; }

__global__ void ternary(float *y, const float *x, int n)
{ int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) { float v = x[i]; y[i] = (v > 0.0f) ? v * 2.0f : v + 1.0f; } }

__global__ void loops(float *y, const float *x, int na, int nb)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; float v = x[i];
  if (threadIdx.x & 1) { for (int k = 0; k < na; ++k) v = v * 1.0001f + 0.5f; }
  else                 { for (int k = 0; k < nb; ++k) v = v * 0.9999f - 0.5f; }
  y[i] = v; }

__global__ void wsum(float *out, const float *x)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; float v = x[i];
  v += __shfl_down_sync(FULL_MASK, v, 1);  v += __shfl_down_sync(FULL_MASK, v, 2);
  v += __shfl_down_sync(FULL_MASK, v, 4);  v += __shfl_down_sync(FULL_MASK, v, 8);
  v += __shfl_down_sync(FULL_MASK, v, 16);
  if ((threadIdx.x & 31) == 0) out[i >> 5] = v; }
```

(a) **Predict, then compile.** Which of `ternary` and `loops` contains a real conditional branch on the data, and which uses `selp`? Compile with the command of section 11.2 and check. Count the instructions an in-range thread executes in `saxpy` and an out-of-range thread.

(b) In `wsum` the shuffle distances are in increasing order, the reverse of the usual 16, 8, 4, 2, 1. Does lane 0 still end up with the sum of all 32 values? Does the result change in floating point? Count the `shfl.sync` instructions in the PTX.

(c) Compile `saxpy` for `sm_70`, `sm_80` and `sm_90` and compare the outputs (`diff`). What differs?

(d) Compile `saxpy` and `loops` for AMD (`gfx90a` and `gfx1100`, with `mini_hip.h` of section 11.2 in place of `mini_cuda.h`). Find the instructions that write `exec`, the scalar registers that hold the loop counters, and the wavefront size in the metadata at the end of the file.

*Where:* your machine; sections 5, 6 and 11. *How:* read only the lines you need (`grep -n 'bra\|selp\|shfl' file.ptx`); the point is to recognise predication, branches and warp primitives in real output.

---

## Answers

### Problem A

(a) All lanes take A: 8 + 30 = **38** slots. All take B: 8 + 12 = **20**. Mixed: 8 + 30 + 12 = **50**. With 16 lanes on each side the useful lane-slots are 16 × (8 + 30) + 16 × (8 + 12) = 608 + 320 = 928 out of 32 × 50 = 1,600: **58%**.

(b) P(warp has lanes on both sides) = 1 − 0.03³² − 0.97³² ≈ 1 − 0.377 = **0.623** (the 0.03³² term is negligible). P(all lanes B) = 0.377. Expected warp cost = 0.377 × 20 + 0.623 × 50 = 7.5 + 31.2 = **38.7** slots. Average useful work per thread = 8 + 0.03 × 30 + 0.97 × 12 = 20.54 slots. Efficiency = 20.54 / 38.7 = **53%**. So 3% of the data take 62% of the warps down both paths, and the warp costs nearly as much as if everything were A (38 slots).

(c) Sorted: 30,000 positive values fill 937 whole warps (A, 38 slots), one boundary warp is mixed (50), the other 30,312 are all B (20): 937 × 38 + 50 + 30,312 × 20 = 35,606 + 50 + 606,240 ≈ **641,900** slots. Random layout: 31,250 × 38.7 ≈ **1,208,800**. Sorting makes it **1.88 times** cheaper, with efficiency ~100%. (Sorting costs something too; the gain is in the downstream kernels that use the order, or from sorting once and reusing.)

(d) Random order: mean 32.5 / expected max 62.5 = **52%** (a simulation with 65,536 items gave 52.2%). Four bins: expected max ≈ 15.5, 31.5, 47.5, 63.5 against means 8.5, 24.5, 40.5, 56.5. Total useful 130 against total issued 158: **82%** (simulation: 81.5%). Fully sorted: neighbouring lanes have nearly equal lengths, so **~100%**. Binning gets most of the benefit with a cheap pass; sorting gets the rest.

### Problem B

(a) With the script in (c) below. A100: by warps 64 / 6 = 10; by blocks 32; by registers: 40 × 32 = 1,280 per warp, × 6 warps = 7,680 per block, 65,536 / 7,680 = 8; by shared: 20 KB + 1 KB = 21 KB, 164 / 21 = 7. Minimum **7 blocks**, 42 warps, **65.6%**, limited by **shared memory**. H100: shared 228 / 21 = 10, registers 8, warps 10, blocks 32: **8 blocks**, 48 warps, **75%**, limited by **registers**. GA10x: warps 48 / 6 = 8, blocks 16, registers 8, shared 100 / 21 = 4: **4 blocks**, 24 warps, **50%**, limited by **shared memory**. The same kernel, three different bottlenecks.

(b) A100: shared down to 12 KB: shared allows 164 / 13 = 12, but registers allow 8: **8 blocks, 75%** (now limited by registers). Registers down to 32 alone: registers allow 65,536 / (1,024 × 6) = 10, shared still 7: **7 blocks, 65.6%** (unchanged: shared binds). Both: min(10, 12, 10) = **10 blocks, 60 warps, 93.8%**. Each change alone leaves the other limit binding, and only the combination gets near the top: a classic case of the second limit appearing as soon as the first is fixed.

(c) The script (Python); run it and compare with the tables:

```python
from math import ceil

def round_up(x, unit):
    return ceil(x / unit) * unit

def occupancy(max_warps, max_blocks, smem_per_sm, threads, regs, smem):
    warps_per_block = ceil(threads / 32)
    regs_per_warp = round_up(regs * 32, 256)
    smem_block = round_up(smem, 128) + 1024 if smem else 0
    limits = {
        "warps":  max_warps // warps_per_block,
        "blocks": max_blocks,
        "regs":   65536 // (regs_per_warp * warps_per_block),
        "smem":   smem_per_sm // smem_block if smem_block else max_blocks,
    }
    blocks = min(limits.values())
    return limits, blocks, blocks * warps_per_block, blocks * warps_per_block / max_warps

# A100: 64 warps, 32 blocks, 164 KB shared
print(occupancy(64, 32, 164 * 1024, 256, 40, 0))            # 6 blocks, 48 warps, 0.75
print(occupancy(64, 32, 164 * 1024, 256, 32, 32 * 1024))    # 4 blocks, 32 warps, 0.5
print(occupancy(64, 32, 164 * 1024, 192, 40, 20 * 1024))    # 7 blocks, 42 warps, 0.65625
```

The first line reproduces the 40-register row of section 8.2 (6 blocks, 48 warps, 75%), the second the 32 KB row of section 8.3 (4 blocks, 50%): 164 KB / 33 KB = 4.97 → 4.

(d) Waves per SIMD = min(10, 512 / 96 = 5.33 → 5) = 5, so **20 wavefronts per CU** = 5 workgroups by registers. LDS: 64 KiB / 24 KiB = 2.67 → **2 workgroups**. The minimum is **2 workgroups = 8 wavefronts**, limited by **LDS**, i.e. 8 / 40 = **20%**. Trimming the LDS use to 16 KiB would allow 4 workgroups (16 wavefronts, 40%), then registers would allow 5.

### Problem C

(a) Blocks: ceil(1000 / 128) = **8** (1,024 threads). `i` = 3 × 128 + 45 = **429**; warp within the block = 45 / 32 = **1**; lane = 45 % 32 = **13**; warp number over the grid = 3 × 4 + 1 = **13** (4 warps per block); `x[i]` is at byte offset 429 × 4 = **1,716**. The last block covers `i` = 896 to 1023; its warp 3 holds `i` = 992 to 1023, of which only 992 to 999 pass `i < n`: **8 active lanes, 24 masked off**. It is the only warp of the 32 that diverges at the guard.

(b) `col` = 2 × 32 + 5 = **69**, `row` = 1 × 8 + 3 = **11**. Linear thread number = 3 × 32 + 5 = **101** → warp **3**, lane **5**. Element index = 11 × 1,920 + 69 = 21,189; byte offset = **84,756**. A warp is 32 consecutive threads in x: **one row segment of 32 pixels = 128 contiguous bytes**. For a (16, 16) block the same thread has linear number 3 × 16 + 5 = 53 (warp 1, lane 21), and a warp covers **two rows of 16 pixels**: two runs of 64 contiguous bytes whose starts are **1,920 × 4 = 7,680 bytes apart**. Both shapes are legal; which one reads memory efficiently is [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)'s subject.

(c) (i) divergent in **every warp** (lanes 0 to 7 of each warp differ from lanes 8 to 31). (ii) warp-uniform: warps 1 and 3 take the branch, 0 and 2 do not, no divergence. (iii) warp-uniform: warps 0 and 1 against 2 and 3; the boundary is a multiple of 32. (iv) divergent in **warp 1 only** (threads 32 to 63 are split at 48). (v) block-uniform: free.

(d) Slots: 132 × 8 = 1,056 blocks per wave. 1,500 / 1,056 = 1.42 → **2 waves**, utilisation 1,500 / 2,112 = **71%**. Exactly **2,112 blocks** would fill two waves (100%); 1,057 blocks would need two waves at 50%. The model assumes equal block times; real runs smear the effect, but a grid size just above a multiple of the resident capacity is still the typical bad case.

### Problem D

(a) `ternary` has no data-dependent branch: the compiler emits `setp.gt.f32` and `selp.f32` (and merges the two arms into one add), as in section 5.2. `loops` has real branches (`@%p4 bra`, `bra.uni`, loop back-edges) because each side has a run-time loop. In `saxpy` an in-range thread executes **20** instructions (7 up to and including the guard, 12 for the body and parameter loads, 1 `ret`); an out-of-range thread executes **8** (7 plus `ret`). Your count may differ by a few if the compiler version differs; what matters is that the guarded threads exit early.

(b) Yes: a butterfly or tree reduction over 32 lanes sums all values whichever order the distances come in; after the five steps lane 0 holds the total either way (with distances 1, 2, 4, 8, 16 each step adds blocks of increasing size). In floating point the **grouping of the additions differs**, so the last bits can differ from the 16, 8, 4, 2, 1 order, which is the same reassociation point as tutorial 21, section 6.1. The PTX contains **5** `shfl.sync.down.b32` instructions.

(c) Only the `.target` line changes between `sm_70` and `sm_80`; between `sm_80` and `sm_90` the `.target` and the PTX `.version` (7.0 against 7.8). The instruction stream is identical: PTX is a virtual ISA, and the differences between the generations appear when `ptxas` produces SASS.

(d) In `gfx90a` code: `s_and_saveexec_b64`, `s_xor_b64 s[2:3], exec, s[2:3]`, `s_andn2_saveexec_b64` and `s_or_b64 exec, exec, s[2:3]` write `exec`; the loop counters are `s8` and `s9` (scalar registers, since `na` and `nb` are the same for every lane) and the loops end in `s_cbranch_scc0`; the metadata says `.wavefront_size: 64`. In `gfx1100` the masks are 32-bit (`exec_lo`, `s_and_not1_saveexec_b32`, and a `v_cmpx_eq_u32` that compares and writes `exec` in one instruction) and the metadata says `.wavefront_size: 32`. (Label numbers such as `.LBB2_4` depend on the order of the kernels in your file.)

---

## Cheat sheet

- **GPU = throughput machine.** Thousands of resident threads, in-order issue, no speculation, a huge register file instead of big caches. Per-thread share at full occupancy on H100: 32 registers, 128 bytes of L1 and shared memory. The CPU is the latency machine (tutorials 9 to 14).
- **SIMT model:** write scalar code for one thread; thread → **warp** (32; AMD wavefront 64, RDNA 32) → **block** (up to 1024 threads, on one SM, shared memory and barrier) → **grid** (no guarantees between blocks: never wait for another block). Index: `i = blockIdx.x * blockDim.x + threadIdx.x`; `linear = z*dx*dy + y*dx + x`, `warp = linear / 32`, `lane = linear % 32`. Block sizes: multiples of 32 (128 to 256 typical).
- **SM (Hopper):** 4 sub-partitions, each with a warp scheduler issuing one warp instruction per cycle, a register file slice, 32 FP32 lanes, a tensor core; 64K registers, 256 KB L1/shared (up to 228 KB shared), 64 warps, 32 blocks; 132 SMs on an H100 SXM5. **AMD CU:** 4 SIMD16 units, a wave64 instruction takes 4 cycles, scalar unit, VGPR file 256 to 512 KiB, LDS 64 KiB, up to 10 waves per SIMD.
- **Blocks → SMs:** whole blocks, as many as fit; waves of blocks; a grid just above a multiple of the capacity wastes the last wave (133 blocks on 132 SMs: 50%).
- **Latency hiding:** another ready warp issues next cycle, at zero switching cost. Independent work needed = latency × throughput: with 4-cycle FMA latency, 4 warps per sub-partition with no ILP, 1 with ILP 4. Memory: V100 391 cycles (269 ns) × 900 GB/s ≈ 3 KB per SM ≈ 24 warp-wide loads in flight; H100 needs ~1.7 MB in flight, more than one 4-byte load per resident thread (1.08 MB), so use wider loads and unrolling. Measured latencies on V100 (Jia et al.): shared 19, L1 28, L2 ~193, global ~391 cycles.
- **Divergence:** a warp runs every path that any lane takes, in turn, with an execution mask; cost = **sum** of the paths. Uniform within the warp = free. Per-warp probability matters: 1% of threads on a 40-slot path against 10 halves the efficiency (27% of warps diverge); 3% → 62% of warps. Loops: the warp runs the **max** trip count (52% efficiency for uniform 1 to 64; bins 82%; sorted ~100%). Fixes: warp-uniform conditions, sort/bin, predication for short bodies, separate kernels for rare long paths. No misprediction flush on a GPU, but no prediction either.
- **Predication:** short `if`/`else` becomes `setp` + `selp` or `@p` instructions, no jump. **Reconvergence:** pre-Volta stack of masks; Volta+ convergence barriers (SASS `BSSY`/`BSYNC`); PTX does not show it.
- **Independent thread scheduling (CC ≥ 7.0):** per-thread PC and call stack; no lock-step assumption; use `__syncwarp(mask)`, `__shfl_*_sync`, `__ballot_sync` (explicit masks; not `__activemask()`); warp sum = 5 × `__shfl_down_sync` (16, 8, 4, 2, 1). `__syncthreads()` (`bar.sync 0`) must be reached by all threads of the block.
- **Occupancy** = resident warps / max warps = min over four limits of blocks (warps, block slots, registers: 65,536 / (regs × 32 rounded to 256 × warps per block), shared memory + 1 KB per block). A100, 256-thread blocks: 32 regs 100%, 64 regs 50%, 128 regs 25%, 255 regs 12.5%. 32 KB shared per 256-thread block: A100 50%, H100 75%. Registers: `-Xptxas -v`; `__launch_bounds__`; spills go to "local memory". Occupancy is a means: enough to hide latency, then use ILP.
- **SIMT vs SIMD:** SIMT = SIMD lanes + hardware multithreading + per-lane masks and per-lane addresses; the hardware handles branches, tails and gathers, so code is easier to write and as easy to write slowly. AMD exposes the scalar unit and `EXEC`.
- **Vocabulary:** SM/CU, warp/wavefront/sub-group, block/workgroup/work-group, shared memory/LDS/local memory, CUDA "local memory" = spill space. **PTX** = virtual ISA (`%tid.x`, `mad.lo`, `setp`, `@p bra`, `selp`, `ld.global`, `fma.rn.f32`, `bar.sync`, `shfl.sync`); **SASS** = native (`S2R`, `IMAD`, `LDG`, `FFMA`, `BAR`, `HMMA`); AMD: `s_` scalar, `v_` vector, `exec`, `s_waitcnt`.
- **Graphics:** vertex shader → rasteriser (fixed) → fragment shader (2 × 2 quads, helper lanes) → ROP (fixed); shaders are kernels run as warps.
- **When not to use a GPU:** serial or irregular code, small data, PCIe copies without reuse (1 GB at 25 GB/s = 40 ms each way).
- **Everything here came from vendor documents, Jia et al. and compiler output; nothing GPU-related was timed on a GPU.**

**Next:** tutorial 23, GPU architecture II (the memory system: global memory coalescing, shared-memory banks, the caches and HBM; tensor cores; the roofline view of a GPU; and scaling out across GPUs with NVLink). Say "next" when you are ready.
