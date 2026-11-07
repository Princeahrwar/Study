# CO 23: GPU Architecture II, Memory, Tensor Cores and Scaling Out

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 6 hours (3.5 reading, 2.5 problems) · You need: [tutorial 22](22_gpu_execution_model.md) (warps, SMs, Little's law and occupancy, sections 3, 7 and 8), [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (cache lines, struct of arrays) and [tutorial 16](16_dram_memory_controllers_and_numa.md) (bandwidth arithmetic and Little's law for memory)*

By the end of this tutorial you can:

- list the memory spaces of a GPU (registers, shared memory, L1, L2, HBM, constant, local), say where each lives, how large it is on an H100, and what an access costs;
- count the 32-byte sectors that one warp-wide load touches, give the efficiency of a pattern (unit stride, stride, offset, array of structures, random), and fix a bad one;
- compute the number of shared-memory wavefronts of an access (the bank-conflict degree) with the gcd rule, and choose padding or an XOR swizzle;
- say when constant memory, the read-only path and local memory help or hurt, and recognise a register spill in PTX;
- explain what one tensor-core instruction computes, read a `wmma` and an AMD `mfma` instruction, and say why tensor cores are about 15 times faster than the ordinary lanes;
- compare FP32, TF32, FP16, BF16, FP8 and INT8 (range, precision, speed), explain why products are summed in FP32, and read "with sparsity" in a data sheet;
- explain asynchronous copies (`cp.async`), the Tensor Memory Accelerator and thread-block clusters, and what problem each solves;
- place a kernel on the H100 roofline for each precision, and explain with numbers why LLM decoding is memory-bound at batch 1 and still memory-bound at batch 64;
- explain kernel fusion and FlashAttention-style tiling as ways to cut HBM traffic, with the byte counts;
- compute host-to-device transfer times over PCIe and NVLink, explain pinned and unified memory and kernel-launch overhead, and decide when a GPU loses to a CPU;
- estimate a ring all-reduce, and explain why tensor parallelism stays inside a node, pipeline parallelism crosses nodes and data parallelism goes outside;
- choose between time-slicing, MPS, MIG and vGPU when several jobs share one GPU.

Tutorial 22 explained how a GPU runs code: warps, SMs, occupancy. This tutorial explains what feeds it. On a GPU almost every performance question is a question about bytes: how many cross the HBM interface, how many sectors a warp's access touches, how many cycles a shared-memory access occupies, how many bytes a tensor core consumes per flop. The arithmetic units are so fast that a kernel is usually waiting for data, and the techniques of this tutorial (coalescing, tiling, fusion, lower precision, overlapping copies with compute) are all ways to move fewer bytes, or to move them while something else computes. The last third moves outward: the link between the CPU and the GPU, and the links between GPUs, because a model that does not fit on one device is limited by those links.

**Where the numbers come from.** **Nothing in this tutorial about GPU speed was measured by me.** The virtual machine used in tutorials 14 to 22 has no GPU and no CUDA toolkit. Three kinds of evidence are used instead. (1) **Real compiler output**: clang 18 compiles device code to NVIDIA PTX (`sm_80`) and AMD assembly (`gfx90a`, `gfx942`) without hardware, so every PTX or assembly listing here is genuine output, with the source shown next to it. (2) **Small Python scripts** that apply the published rules (32-byte sectors, 32 banks of 4 bytes, the roofline formula, ring all-reduce) to concrete cases; they check arithmetic, they do not time anything, and the ones that matter are shown in full. (3) **Vendor documents and published measurements**, linked where they are used: the [CUDA C++ Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html), the [Nsight Compute profiling guide](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html), NVIDIA's [Hopper](https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/) and [Ampere](https://developer.nvidia.com/blog/nvidia-ampere-architecture-in-depth/) architecture articles, the [H100 data sheet page](https://www.nvidia.com/en-us/data-center/h100/), and Chips and Cheese's [H100 measurements](https://chipsandcheese.com/p/nvidias-h100-funny-l2-and-tons-of-bandwidth). One warning about those data sheets, repeated in section 5: NVIDIA's headline tensor-core figures for TF32, FP16, BF16, FP8 and INT8 are quoted **with 2:4 structured sparsity**, which doubles them; this tutorial always uses the dense figure (half of the headline) unless it says otherwise. Section 11 of tutorial 22 explains how to get PTX without a GPU; the headers used there (`mini_cuda.h`, `mini_hip.h`) are reused here.

> **The one-sentence model.** A GPU is a machine for moving bytes to arithmetic units, so its performance is set by how many bytes a kernel moves and how well they travel: HBM (3.35 TB/s on an H100) feeds a 50 MB L2 and then 132 SMs, each with 256 KB of registers and 256 KB of L1 and shared memory; a warp's 32 accesses are served in 32-byte sectors, so only contiguous patterns use the bandwidth; shared memory has 32 banks, so strides that share a factor with 32 serialise; tensor cores multiply matrix tiles about 15 times faster than the ordinary lanes, which turns most kernels memory-bound and makes the cures fewer bytes (fusion, tiling, lower precision) and overlapped copies (`cp.async`, TMA); and beyond one GPU the same rule applies to links (NVLink about 450 GB/s each way, PCIe Gen5 about 63 GB/s, a network card about 50 GB/s), which decides where tensor, pipeline and data parallelism run.

---

## 1. The memory system of a GPU

### 1.1 The levels

Tutorial 22 listed the latencies of a V100. Here are the same levels with the sizes of an H100 SXM (sources: NVIDIA's [Hopper article](https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/), the [H100 page](https://www.nvidia.com/en-us/data-center/h100/), Chips and Cheese, and Jia et al.'s [V100 microbenchmarks](https://arxiv.org/pdf/1804.06826) for the cycle counts, which are for the older chip and shown only for scale).

| Level | Size on an H100 SXM | Shared by | What it is | Latency (V100, cycles) |
|---|---|---|---|---|
| **Registers** | 256 KB per SM (64K × 32 bit); 33 MiB over 132 SMs | one thread | the working set of every resident thread | 0 extra (operand) |
| **Shared memory** | up to 228 KB per SM, carved out of the 256 KB L1 array | one thread block | a *scratchpad* the program manages itself | about 19 |
| **L1 data cache** | the rest of that 256 KB array | one SM | a hardware-managed cache of global memory | about 28 |
| **L2 cache** | 50 MB, in two partitions | all SMs | the cache in front of HBM, and the point where SMs meet | about 193 |
| **HBM3** | 80 GB, 3.35 TB/s | the whole GPU | the "global memory" | about 391 (1029 for an L2 and TLB miss) |
| **Constant memory** | 64 KB visible to a kernel | the grid | read-only, cached, fast when all lanes read one address | close to L1 |
| **Local memory** | per thread, any size | one thread | *not local*: it lives in HBM and is cached; spills and dynamically indexed arrays | like global |

Three things in this table differ from a CPU.

First, **registers are the largest fast memory on the chip**. The 132 SMs hold 33 MiB of registers and 33 MiB of L1-plus-shared SRAM (132 × 256 KB each), against 50 MB of L2. On a CPU the register file is a few kilobytes. A GPU can afford this because it keeps thousands of threads resident (tutorial 22, section 8) and every one of them needs its registers to stay put.

Second, **shared memory is a scratchpad, not a cache**. The program decides what goes in, when, and where; nothing is evicted behind its back. The hardware shares one SRAM array between L1 and shared memory, and the split is chosen per kernel.

Third, **there is no coherence between the L1 caches of different SMs**. Blocks on different SMs communicate through L2 and HBM, with fences and atomics ([tutorial 20](20_memory_consistency_and_atomics.md) discusses the model). This is what lets the L1 stay small and simple.

### 1.2 HBM: width, not speed

The HBM of an H100 SXM delivers 3.35 TB/s through **five active stacks, a 5120-bit interface** (ten 512-bit memory controllers; the full die has six stacks). Bandwidth is `data rate × width / 8`, the same arithmetic as [tutorial 16](16_dram_memory_controllers_and_numa.md):

```
H100 SXM:   3.35e12 B/s × 8 / 5120 pins = 5.23 Gbit/s per pin
V100:       877 MHz × 2 (double data rate) × 4096 bits / 8 = 898 GB/s   (the worked example in the CUDA guide)
one DDR5-6400 channel:   6.4 Gbit/s per pin × 64 pins / 8 = 51 GB/s
```

An HBM pin is *slower* than a DDR5 pin. The GPU gets 65 times the bandwidth of one DDR5 channel by having 80 times as many pins, each stack sitting next to the GPU die on a silicon interposer with a 1024-bit bus (tutorial 16, section 2.5). Achieved bandwidth falls short of the peak, as on a CPU: Chips and Cheese measured just under 2 TB/s on an H100 PCIe card, whose five HBM2e stacks are rated at about 2 TB/s. Plan on 80 to 90% of the data-sheet figure for a pure streaming kernel.

### 1.3 The L2, and why it is "two-level"

The H100's L2 is 50 MB (the A100's was 40 MB), split into two partitions that sit on opposite sides of a crossbar. Every SM can reach all of it, but Chips and Cheese found that it behaves "like a two-level setup": on the PCIe card, reads from the near partition reached over 5.5 TB/s, reads across the whole 50 MB about 3.8 TB/s, and a far-partition access took nearly twice the latency of a near one. Two consequences:

- The L2 has roughly 2 to 3 times the bandwidth of HBM (from that PCIe-card measurement; the SXM ratio will differ, since its HBM is faster). A kernel that re-reads a working set that fits in 50 MB runs well above the HBM roofline. Tiling for L2 reuse (section 6.4) exploits this.
- Software can ask for part of the L2 to be kept for chosen data (the *residency controls* introduced with the A100 and kept in Hopper), useful when a small table is read by every block.

Global atomics resolve at about L2 cost (Chips and Cheese measured them slightly above L2 latency), so a histogram with one hot counter serialises on it: [tutorial 19](19_cache_coherence.md)'s contended cache line, at GPU scale.

---

## 2. Global memory: sectors and coalescing

### 2.1 What a warp-wide load does

A load instruction in a warp carries **32 addresses**, one per lane. The load/store unit merges them and asks the memory system for the minimum set of **32-byte sectors** that covers every lane's bytes. This is the rule since compute capability 6.0, stated in the [CUDA Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html): a warp's accesses are combined "into the fewest 32-byte transactions". The caches use **128-byte lines made of four sectors** ([Nsight Compute guide](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html)), and the sector is the unit in which requests are counted and data move.

So the cost of a warp-wide access is the **number of distinct 32-byte sectors it touches**. Call an access **coalesced** when that number is close to the minimum, `ceil(bytes the warp needs / 32)`. For 32 floats the minimum is `128 / 32 = 4`.

### 2.2 Counting sectors

The rule is simple enough to program. This function takes the 32 byte addresses of one warp-wide access and the access size, and counts sectors:

```python
def sectors(addrs, size, sector=32):
    s = set()
    for a in addrs:
        for b in range(a, a + size):          # every byte this lane touches
            s.add(b // sector)                # which 32-byte sector it falls in
    return len(s)
```

Applied to a 128-byte-aligned array, with lane `i` reading the address in the first column:

| Access pattern (lane `i` reads…) | Sectors | Bytes moved | Bytes the warp uses | Share used |
|---|---|---|---|---|
| float at `4i` (unit stride) | **4** | 128 | 128 | 100% |
| float at `4(31-i)` (reversed) | 4 | 128 | 128 | 100% |
| float at `4((5i) mod 32)` (shuffled inside the 128 B) | 4 | 128 | 128 | 100% |
| float at `4 + 4i` (offset by one element) | **5** | 160 | 128 | 80% |
| float at `8i` (stride 2) | **8** | 256 | 128 | 50% |
| float at `16i` (stride 4) | 16 | 512 | 128 | 25% |
| float at `32i` (stride 8) or more | **32** | 1024 | 128 | 12.5% |
| float at `4096i` (a column of a 1024-wide matrix) | 32 | 1024 | 128 | 12.5% |
| 16-byte struct, read its `.x` field | 16 | 512 | 128 | 25% |
| 24-byte struct, read its `.x` field | 24 | 768 | 128 | 16.7% |
| random float in 1 MB | 32 | 1024 | 128 | 12.5% |
| `double` at `8i` | 8 | 256 | 256 | 100% |
| `half` at `2i` | 2 | 64 | 64 | 100% |
| `float4` (16 B) at `16i` | 16 | 512 | 512 | 100% |
| all 32 lanes read the same float (broadcast) | **1** | 32 | 4 | the cheapest possible |

(Computed with the function above; the rows for unit stride, offset by one and stride 2 agree with the Best Practices Guide, which gives four 32-byte transactions for adjacent floats, five when misaligned, and "50%" efficiency at stride 2.)

Read the table in four steps.

1. **Order does not matter, the set of sectors does.** Reversed and shuffled accesses inside the same 128 bytes cost the same four sectors as the sorted one. (The guide says the same: permuting addresses within the segments adds no transactions.) The old rule "thread `i` must read word `i`" is gone.
2. **Efficiency is `1 / stride`, down to 1/8.** A stride of `s` floats spreads the warp over `s × 128` bytes, and from stride 8 on every lane has a sector of its own. At 12.5% a kernel that should run at 3 TB/s moves useful data at 0.4 TB/s.
3. **Misalignment costs one extra sector, not a collapse.** Shifting a unit-stride access by one element touches five sectors instead of four. The guide's copy kernel lost only about 10% (measured on a V100), not the 20% the count predicts, because the neighbouring warp reuses the over-fetched sector from cache. `cudaMalloc` returns memory aligned to at least 256 bytes, and block sizes that are multiples of 32 keep warps aligned.
4. **An array of structures is a strided access.** Reading one field of a 16-byte struct is stride 4 in disguise: 25%.

### 2.3 Array of structures against structure of arrays

Here is the same read written both ways, compiled for `sm_80` with clang 18:

```cpp
struct Particle { float x, y, z, w; };                       // array of structures (AoS)
__global__ void read_x_aos(float *out, const Particle *p, int n)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; if (i < n) out[i] = p[i].x; }
__global__ void read_x_soa(float *out, const float *px, int n)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; if (i < n) out[i] = px[i]; }
```

The two loads, from the PTX the compiler produced:

```
read_x_aos:   mul.wide.s32  %rd8, %r5, 16;    // address = base + 16 * i   (the struct is 16 bytes)
              add.s64       %rd2, %rd5, %rd8;
              ld.global.f32 %f1, [%rd2];      // 16 sectors per warp: 25% used

read_x_soa:   mul.wide.s32  %rd7, %r5, 4;     // address = base + 4 * i
              ...
              ld.global.f32 %f1, [%rd2];      // 4 sectors per warp: 100% used
```

The instruction is the same; only the multiplier (16 against 4) differs, and with it the traffic: four times as many sectors for the same four useful bytes per lane. [Tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) made this point for CPU caches ("struct of arrays"); on a GPU the penalty is the same 4× but it is measured per warp rather than per cache line. Three fixes, in order of preference:

- **Store structure of arrays** (`px[]`, `py[]`, `pz[]`), so each field is a unit-stride stream.
- **Load the whole struct as a `float4`** when the kernel uses all four fields. Each lane then reads 16 aligned bytes, the warp touches 16 sectors, and all 512 bytes are used. In PTX this is `ld.global.v4.f32 {%f2, %f3, %f4, %f5}, [%rd6]` (tutorial 22, section 11.2).
- **Stage through shared memory** (section 3): read coalesced into a tile, then pick fields from the tile.

### 2.4 Wide loads and bytes in flight

[Tutorial 22](22_gpu_execution_model.md) (section 7.2) applied Little's law to a full H100: 3.35 TB/s at 500 ns needs 1.7 MB in flight, while 270,336 resident threads with one 4-byte load each have only 1.08 MB outstanding. Sectors give the same fix a second face. A scalar `float` load puts 4 sectors (128 bytes) in flight per warp instruction; a `float4` load puts 16 sectors (512 bytes), so the same number of resident warps keeps 4 times as many bytes in flight (4.3 MB for the whole chip). That is why memory-bound kernels use `float4`/`int4` loads (`ld.global.v4.f32`) or unroll to keep two or more independent loads per thread, and why they need the 16-byte alignment that goes with it.

### 2.5 Matrices

A row-major `A[row][col]` is contiguous along `col`. If consecutive lanes (`threadIdx.x`) step along `col`, a warp reads 128 contiguous bytes: 4 sectors. If consecutive lanes step along `row`, the stride is the row length: 32 sectors. A matrix transpose has exactly this problem: reading rows is coalesced, then writing the transposed element means writing columns. The cure is a tile in shared memory, which the next section builds.

---

## 3. Shared memory and bank conflicts

### 3.1 Banks

Shared memory is built from **32 banks**, each 4 bytes wide, and each bank can deliver one 32-bit word per clock ([Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html)). Successive 32-bit words are in successive banks: **word `w` is in bank `w mod 32`**, where `w` is the byte address divided by 4. Thirty-two banks of 4 bytes give 128 bytes per clock per SM, which is why a warp reading 32 consecutive words (128 bytes) is served in one pass.

The rule for one warp-wide request:

- If every lane touches a **different bank**, the request is served in one pass (one *wavefront* in Nsight Compute's term, the work the L1 pipeline does in a cycle).
- If lanes touch **different words in the same bank**, those accesses are *serialised*: the request needs as many wavefronts as the largest number of distinct words in one bank. This is an *n-way bank conflict*.
- If lanes touch the **same word**, the value is **broadcast**, which is free. Two lanes reading the same word are not in conflict.

### 3.2 Counting wavefronts

```python
def wavefronts(word_addrs):                      # one word index (byte address / 4) per lane
    by_bank = {}
    for w in word_addrs:
        by_bank.setdefault(w % 32, set()).add(w)   # a set: the same word twice counts once
    return max(len(s) for s in by_bank.values())
```

| Lane `i` reads word… | Wavefronts | Why |
|---|---|---|
| `i` (stride 1) | **1** | 32 different banks |
| `2i` | **2** | lanes `i` and `i+16` share a bank |
| `3i` | 1 | 3 is odd |
| `4i` | **4** | |
| `8i` | **8** | |
| `16i` | **16** | |
| `32i` | **32** | every lane in bank 0 |
| `33i` | 1 | 33 is odd |
| the same word for all lanes | 1 | broadcast |
| `i // 2` (pairs share a word) | 1 | broadcast within each pair |
| `double`, 2 words per lane, unit stride | 2 | 256 bytes cannot pass a 128 B/clock port in one pass |
| `float4`, 4 words per lane, unit stride | 4 | 512 bytes need four passes |

The pattern is a rule: for a stride of `s` words, **the conflict degree is `gcd(s, 32)`**. Odd strides are conflict-free, because an odd number is coprime to 32 and visits all 32 banks before repeating; stride 2 repeats after 16 lanes (2-way); stride 32 never leaves bank 0 (32-way). Running the function for every stride from 1 to 64 confirms it. The last two rows are not conflicts: they are cases where the warp simply needs more than 128 bytes, so more passes are unavoidable.

A conflict costs *throughput*, not latency: each extra wavefront occupies the shared-memory pipeline for one more cycle, so a 32-way conflict makes one load take 32 cycles of the pipe where 1 would do, and the tensor-core and arithmetic units wait behind it.

### 3.3 The transpose, and the padding fix

A tile transpose reads a 32 × 32 tile of the input row by row into shared memory, then writes it out column by column. Written with 256 threads per block (8 rows of 32 lanes) and the tile as `tile[32][32]`:

```cpp
__shared__ float tile[32][32];                      // or [32][33]
// bx, by: the tile's origin in the matrix, computed from blockIdx; n: the matrix width
int tx = threadIdx.x & 31, ty = threadIdx.x >> 5;
for (int j = 0; j < 32; j += 8) tile[ty + j][tx] = in[(by + ty + j) * n + bx + tx];   // row write
__syncthreads();
for (int j = 0; j < 32; j += 8) out[(bx + ty + j) * n + by + tx] = tile[tx][ty + j];  // column read
```

The row write puts lane `tx` at word `(ty+j)*32 + tx`: 32 consecutive words, no conflict. The column read puts lane `tx` at word `tx*32 + (ty+j)`: a stride of 32 words, so **bank `(ty+j) mod 32` for every lane: a 32-way conflict**. Change the declaration to `tile[32][33]` and the stride becomes 33 words, which is odd, so all 32 lanes land in different banks. The cost is 32 wasted floats per tile.

The compiler output shows exactly this difference (clang 18, `sm_80`); the row stride is the multiplier in the address of the column read:

```
transpose_smem  (tile[32][32]):   mul.wide.u32 %rd18, %r10, 128;   ... ld.shared.f32 %f5, [%rd21];   // 128 B per row
transpose_pad   (tile[32][33]):   mul.wide.u32 %rd18, %r10, 132;   ... ld.shared.f32 %f5, [%rd21];   // 132 B per row
```

(128 = 32 × 4 bytes; 132 = 33 × 4 bytes; and the row-write stores step by `+1024` against `+1056` bytes for 8 rows.) The CUDA guide measures the same fix on a related kernel: padding the tile to `TILE_DIM+1` raised a V100 from 140.2 to 199.4 GB/s in its `C = AAT` example. The numbers belong to a different kernel; the cause is the same.

### 3.4 "+1" is not a universal rule

Padding by one works because the warp is one row of 32 lanes. With a different tile shape the warp is two-dimensional and the arithmetic changes. Take a 16 × 16 tile with the warp laid out as 16 columns and 2 rows (`tx = lane % 16`, `ty = lane / 16`), reading `tile[tx][ty]`. The function gives:

| Row length in words | 16 | 17 | 18 | 19 | 20 | 22 | 24 | 32 |
|---|---|---|---|---|---|---|---|---|
| Wavefronts | 8 | **2** | **1** | 2 | 2 | 1 | 4 | 16 |

The "+1" padding (17) leaves a 2-way conflict, because lanes 0 to 15 occupy 16 distinct banks and lanes 16 to 31, shifted by one, collide with some of them. A row length of 18 works: `tx × 18` is always even, so the first 16 lanes use the 16 even banks and the second 16 (offset by 1) use the 16 odd ones. **Always compute the pattern for the real warp layout; do not memorise "+1".**

### 3.5 Swizzling

Padding wastes memory and breaks the alignment that vector loads need. Libraries instead **XOR-swizzle**: store element `(row, col)` at word `row*32 + (col ^ row)`. Row writes stay a permutation within one row (conflict-free) and column reads hit bank `col ^ row`, which differs for every `row` (conflict-free too). The function confirms it for all 32 rows and columns. The Hopper TMA engine does this swizzling in hardware when it fills shared memory: the CUDA programming guide lists `CU_TENSOR_MAP_SWIZZLE_128B` as a way to eliminate conflicts for transposed access.

### 3.6 On AMD

The same rule holds with the bank count of the part. The LDS has 32 banks of 4 bytes on CDNA 1 to 3 and 64 on CDNA4 and on RDNA2 to RDNA4 (see the [ROCm hardware notes](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/hardware_implementation.html)), so the conflict degree is `gcd(stride, banks)`. A wave64 reading one 4-byte word per lane moves 256 bytes, so on a 32-bank LDS (128 bytes per clock) it needs at least two passes even with no conflict; the gcd rule then applies on top.

### 3.7 Why shared memory is worth the trouble: reuse

Shared memory pays off when a value loaded once is used many times. A matrix multiply of two `N × N` matrices does `2N³` flops. Without tiling every output reads a row of `A` and a column of `B`: `2N` loads per output, `2N³` loads in all. With a `T × T` tile in shared memory, each block loads a tile of `A` and `B` once and each element is reused `T` times: `2N³ / T` loads from global memory. For `T = 32` that is **32 times less global traffic**. The tiled multiply is the CUDA track's first optimisation lab (`12_gpu_cuda`, topic 02).

---

## 4. The other memories: constant, read-only and local

### 4.1 Constant memory

Constant memory is a 64 KB read-only space, cached close to the SM; Chips and Cheese measured its latency flat across the whole 64 KB and nearly identical to L1. Its trick is **broadcast**. The [Best Practices Guide](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html) states the rule: accesses to different addresses by the lanes of a warp are *serialised*, so the cost scales with the number of distinct addresses, and when all lanes read the same address the access can be as fast as a register.

| Lanes read… | Cost |
|---|---|
| one address (every lane the same coefficient) | one access, broadcast to 32 lanes |
| 4 distinct addresses | 4 serialised accesses |
| 32 distinct addresses (`lut[threadIdx.x]`) | 32 serialised accesses: slower than a plain global load |

Good uses: filter taps, kernel parameters, small tables indexed by a *uniform* value (the loop counter, the layer number). Kernel arguments themselves travel the same way: PTX reads them with `ld.param`, and in SASS they appear as constant-bank operands, which is why reading an argument is as cheap as a register. Bad use: a per-lane lookup table. Put that in shared memory (and check the banks).

### 4.2 The read-only path

Marking input data as read-only for the kernel lets the compiler use a *non-coherent* load, `ld.global.nc` in PTX. The cache may serve it from a read-only path, and the compiler may reorder and hoist such loads because nothing in the kernel writes the data. You request it with `const` and `__restrict__` (which promises that the pointers do not alias), or with the `__ldg()` intrinsic. The compiler output shows the effect (clang 18, `sm_80`):

```cpp
__global__ void plain(float *out, const float *in, int n)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; if (i < n) out[i] = in[i] * 2.0f; }
__global__ void restrict_ro(float * __restrict__ out, const float * __restrict__ in, int n)
{ int i = blockIdx.x * blockDim.x + threadIdx.x; if (i < n) out[i] = in[i] * 2.0f; }
```

```
plain:         ld.global.f32     %f1, [%rd2];     // may alias `out`: ordinary load
restrict_ro:   ld.global.nc.f32  %f1, [%rd2];     // read-only and unaliased: non-coherent load
```

Without `__restrict__` the compiler must assume a store to `out` could change `in`, so it can neither use the non-coherent path nor move loads past stores. This is the same aliasing problem as the auto-vectorisation of [tutorial 21](21_simd_and_vector_processing.md) (`restrict` in C), with the same cure.

### 4.3 Local memory and register spills

**Local memory is not local.** It is per-thread private memory *in the device DRAM*, cached by L1 and L2. The compiler puts a variable there when it cannot keep it in registers:

- an array **indexed with a runtime value** (registers have no addresses, so `tmp[k]` with unknown `k` cannot be a register);
- a large array or structure;
- a **spill**: the kernel needs more live values than the register budget allows (at most 255 per thread, and less if a launch bound or `-maxrregcount` caps registers to raise occupancy, tutorial 22, section 8.2).

The compiler output of a kernel that fills a 64-float array and then reads it with a runtime index (clang 18, `sm_80`):

```cpp
__global__ void local_array(float *out, const int *idx, int n)
{ float tmp[64];
  for (int k = 0; k < 64; ++k) tmp[k] = k * 0.5f + threadIdx.x;
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) out[i] = tmp[idx[i] & 63]; }          // runtime index: the array must live in memory
```

```
.local .align 4 .b8 __local_depot7[256];            // 256 bytes of local memory per thread
   st.local.f32 [%rd15+-8], %f3;   ...               // 64 stores into it
   ld.local.f32 %f10, [%rd13];                       // the indexed read
```

`.local`, `st.local` and `ld.local` in PTX, or "lmem" and "spill stores/loads" in the output of `nvcc --ptxas-options=-v`, are how you detect it (the Best Practices Guide recommends both). Three facts size the problem:

- **Layout.** NVIDIA's programming guide (quoted in [this forum thread](https://forums.developer.nvidia.com/t/how-does-the-compiler-lay-out-local-variables-in-local-memory/176643)) says local memory is organised so that consecutive 32-bit words are accessed by consecutive thread IDs. If all lanes use the *same* index the warp reads 32 adjacent words: fully coalesced. If the lanes use *different* indexes (as in `tmp[idx[i] & 63]`) the access scatters, and each lane may cost its own sector.
- **Footprint.** 256 bytes per thread across the 270,336 resident threads of a full H100 is `256 × 270,336 = 69 MB`, more than the 50 MB L2. At full occupancy such an array does not stay in cache; it streams to and from HBM.
- **Cost.** A spill that hits L1 costs tens of cycles (28 on a V100); one that misses costs a trip to HBM (hundreds). Spills in an inner loop are the usual cause of an unexplained 2× slowdown after "just adding one more variable".

Fixes, in order: make the index a compile-time constant (`#pragma unroll` the loop so `tmp[k]` becomes a register); shrink the per-thread array; move per-thread arrays into shared memory with a conflict-free layout (section 3); restructure to hold fewer values live at once; raise the register cap and accept lower occupancy.

### 4.4 Summary

| Space | Where it lives | Scope | Fast when | Slow when |
|---|---|---|---|---|
| Register | register file | thread | always | never; but too many lowers occupancy |
| Shared | SM SRAM | block | no bank conflicts; data reused | conflicts; one-time use |
| Constant | HBM + constant cache | grid (read-only) | all lanes read one address | lanes read distinct addresses |
| Global, read-only (`.nc`) | HBM + L1/L2 | grid | coalesced; cached re-use | strided; no re-use |
| Global | HBM + L1/L2 | grid | coalesced | strided, misaligned, random |
| Local | HBM + L1/L2 | thread | same index in all lanes; fits L1 | divergent index; large footprint |

---

## 5. Tensor cores and number formats

### 5.1 Why a matrix instruction

An ordinary FMA lane does one multiply-add (2 flop) per clock. Deep learning is dominated by matrix multiplies, which are nothing but multiply-adds, so the cheapest way to get more of them is a unit that does a *tile* at once. A **tensor core** (NVIDIA) or **matrix core** (AMD) computes `D = A × B + C` for small matrices in a single instruction, reading operands from registers (and, on Hopper, from shared memory) and writing the accumulators back to registers.

How large is the step? Compile one tile multiply (clang 18, `sm_80`; the code uses clang's low-level builtins because the C++ `nvcuda::wmma` API needs the toolkit's `mma.h`; that API produces the same instructions):

```cpp
__global__ void wmma_tile(float *d, const int *a, const int *b, const float *c)
{ int fa[8], fb[8]; float fc[8], fd[8];
  __hmma_m16n16k16_ld_a(fa, a, 16, 0);               // each lane loads 8 registers of the A tile
  __hmma_m16n16k16_ld_b(fb, b, 16, 0);
  __hmma_m16n16k16_ld_c_f32(fc, c, 16, 0);
  __hmma_m16n16k16_mma_f32f32(fd, fa, fb, fc, 0, 0); // D = A*B + C, FP16 inputs, FP32 accumulate
  __hmma_m16n16k16_st_c_f32(d, fd, 16, 0); }
```

```
wmma.load.a.sync.aligned.row.m16n16k16.f16  {%r2, ... %r9}, [%rd2], %r1;
wmma.load.b.sync.aligned.row.m16n16k16.f16  {%r10, ... %r17}, [%rd3], %r1;
wmma.load.c.sync.aligned.row.m16n16k16.f32  {%f1, ... %f8}, [%rd4], %r1;
wmma.mma.sync.aligned.row.row.m16n16k16.f32.f32
        {%f9, ... %f16}, {%r2, ... %r9}, {%r10, ... %r17}, {%f1, ... %f8};
wmma.store.d.sync.aligned.row.m16n16k16.f32 [%rd1], {%f9, ... %f16}, %r1;
```

The one `wmma.mma.sync` instruction multiplies a 16 × 16 tile of A by a 16 × 16 tile of B and adds a 16 × 16 tile of C: `16 × 16 × 16 = 4096` multiply-adds, executed by the whole warp together. One ordinary `FFMA` warp instruction does 32. **The tile instruction replaces 128 FMA instructions.** The accumulator type is part of the instruction name (`.f32.f32`): the products are summed in FP32 (section 5.6 shows why).

The *throughput* gain is smaller than the instruction-count gain, because the tensor core still needs time and operand bandwidth for 4096 multiply-adds. From NVIDIA's published peaks (dense, no sparsity):

| GPU | FP16 tensor (dense) | FP32 on the ordinary lanes | Ratio |
|---|---|---|---|
| A100 | 312 Tflop/s | 19.5 Tflop/s | **16×** |
| H100 SXM | 989 Tflop/s (headline 1,979 with sparsity) | 67 Tflop/s | **14.8×** |

So roughly 15 times the arithmetic rate for matrix work, which is why every GPU for AI is built around them, and why code that cannot be expressed as tile multiplies (a stencil, a reduction) does not benefit.

### 5.2 The same idea on AMD

AMD's matrix cores are used through MFMA instructions. Compiling a 16 × 16 × 16 tile with clang 18 (`gfx90a`, the MI200 generation; MI300 is `gfx942`):

```cpp
typedef _Float16 half4 __attribute__((ext_vector_type(4)));
typedef float    float4v __attribute__((ext_vector_type(4)));
__global__ void mfma_tile(float4v *d, const half4 *a, const half4 *b, const float4v *c)
{ int t = threadIdx.x;
  d[t] = __builtin_amdgcn_mfma_f32_16x16x16f16(a[t], b[t], c[t], 0, 0, 0); }
```

```
global_load_dwordx2 v[4:5], v1, s[2:3]       ; A: 2 registers per lane (4 halves)
global_load_dwordx2 v[6:7], v1, s[4:5]       ; B: 2 registers per lane
global_load_dwordx4 v[0:3], v8, s[6:7]       ; C: 4 registers per lane (4 floats)
v_mfma_f32_16x16x16f16 v[0:3], v[4:5], v[6:7], v[0:3]    ; the whole tile, one instruction
global_store_dwordx4 v8, v[0:3], s[0:1]
```

The sizes are consistent: a wavefront has 64 lanes, so A holds `64 × 4 = 256 = 16 × 16` half-precision values and C holds `64 × 4 = 256` floats. The instruction name is `v_mfma_f32_16x16x16f16` on `gfx90a` and `v_mfma_f32_16x16x16_f16` on `gfx942`. NVIDIA and AMD differ in vocabulary and in tile shape, not in idea.

### 5.3 What the programmer sees

The A, B and C tiles are distributed across the lanes of the warp; which element sits in which register is fixed by the hardware. Three levels of access exist:

- **Libraries** (cuBLAS, cuDNN, CUTLASS, rocBLAS): you call a GEMM and they choose tiles and instructions. This is how almost all code uses tensor cores.
- **The warp-level API** (`nvcuda::wmma` in C++, or the PTX `mma.sync` instruction): one warp issues the tile multiply and the layout is yours (PTX) or hidden (WMMA).
- **Hopper's warpgroup MMA** (`wgmma.mma_async` in PTX). Per [Colfax's tutorial](https://research.colfax-intl.com/cutlass-tutorial-wgmma-hopper/): a *warpgroup* is four contiguous warps (128 threads) that issue one instruction together; the instruction is **asynchronous**, with explicit `wgmma.fence`, `commit_group` and `wait_group`; M is always 64 and N a multiple of 8 up to 256 (K is 16 for 16-bit types); operand **B must be in shared memory**, A may be in registers or shared memory, and the accumulator stays in registers. Asynchronous means the tensor cores work while the warps issue the next copies (section 6).

### 5.4 Number formats

A floating-point format splits its bits into a sign, an exponent (which sets the **range**) and a mantissa (which sets the **precision**). The values below come from the definitions (the maximum and the smallest normal value follow from the formulas under the table); "epsilon" is the gap just above 1.0, `2^-mantissa`, so it is the relative rounding error scale.

| Format | Bits (S/E/M) | Max finite | Epsilon | Smallest normal | H100 SXM dense | Typical use |
|---|---|---|---|---|---|---|
| FP32 | 1/8/23 | 3.4 × 10³⁸ | 1.2 × 10⁻⁷ | 1.2 × 10⁻³⁸ | 67 Tflop/s (ordinary lanes) | the reference; accumulators |
| **TF32** | 1/8/10 (19 bits, kept in a 32-bit container) | 3.4 × 10³⁸ | 9.8 × 10⁻⁴ | 1.2 × 10⁻³⁸ | 494 Tflop/s | FP32 data with FP16-class multiply |
| **FP16** | 1/5/10 | 65,504 | 9.8 × 10⁻⁴ | 6.1 × 10⁻⁵ | 989 Tflop/s | inference, mixed-precision training |
| **BF16** | 1/8/7 | 3.4 × 10³⁸ | 7.8 × 10⁻³ | 1.2 × 10⁻³⁸ | 989 Tflop/s | training (FP32 range, no loss scaling) |
| **FP8 E4M3** | 1/4/3 | 448 | 0.125 | 1.6 × 10⁻² | 1,979 Tflop/s | weights and activations |
| **FP8 E5M2** | 1/5/2 | 57,344 | 0.25 | 6.1 × 10⁻⁵ | 1,979 Tflop/s | gradients (more range) |
| **INT8** | integer −128..127 | 127 | 1 (a scale factor sets the real value) | — | 1,979 Tops | quantised inference |

(Dense rates are half of NVIDIA's headline figures for TF32, FP16/BF16, FP8 and INT8, which the [H100 page](https://www.nvidia.com/en-us/data-center/h100/) lists "with sparsity": 989, 1,979, 3,958 and 3,958. FP64 is 34 Tflop/s on the ordinary lanes and 67 on the tensor cores. TF32's bit layout is from NVIDIA's [Ampere article](https://developer.nvidia.com/blog/nvidia-ampere-architecture-in-depth/); FP8's two encodings and the missing infinity in E4M3 are from the [FP8 paper](https://arxiv.org/abs/2209.05433).)

The largest finite value of an IEEE-style format follows from its exponent and mantissa widths:

```python
def fp_max(e, m):                             # e exponent bits, m mantissa bits
    bias = 2**(e - 1) - 1
    return (2 - 2**-m) * 2**(2**e - 2 - bias)  # all-ones exponent is reserved for inf/NaN
# fp_max(5, 10) = 65504 (FP16);  fp_max(5, 2) = 57344 (E5M2);  fp_max(8, 7) = 3.39e38 (BF16)
# E4M3 is the exception: it uses the top exponent and reserves only S.1111.111 for NaN: 1.75 * 2**8 = 448
```

Reading the table:

- **Range against precision.** FP16 has more precision than BF16 (10 mantissa bits against 7) but a much smaller range: it overflows at 65,504. BF16 uses FP32's exponent, so a value that is fine in FP32 is fine in BF16. Rounding 70,000 gives `inf` in FP16 but 70,144 in BF16 (three significant digits survive), and `1 + 2^-8` staying exactly representable in FP16 but rounding to 1.0 in BF16. That is why FP16 training needs *loss scaling* (keep gradients inside the range) and BF16 training usually does not.
- **FP8 has two encodings** because no single 8-bit split does both jobs. E4M3 (4 exponent, 3 mantissa bits) has more precision and omits infinities, using only one bit pattern for NaN to gain range up to 448; E5M2 is IEEE-like, with range up to 57,344 but only 2 mantissa bits. The FP8 paper reports that FP8 training matched 16-bit results on CNNs, RNNs and Transformers, including language models up to 175 billion parameters, without changing hyperparameters; in practice each tensor carries a *scale factor*, and Hopper's Transformer Engine chooses FP8 or 16-bit per layer and rescales (see NVIDIA's [Hopper article](https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/)).
- **Each halving of the element size does two things**: it doubles the tensor-core rate, and it halves the bytes per element, which doubles the arithmetic intensity of the same computation (section 7). For memory-bound LLM decoding the second effect is the larger one.
- **INT8** is exact integer arithmetic with a real-valued scale factor kept outside, accumulated in 32-bit integers.

### 5.5 Why products are summed in FP32

A tensor core multiplies in a narrow format but **accumulates in FP32** (or INT32 for integers): the PTX above says `.f32.f32`. The reason is rounding. In FP16 the spacing of numbers near 2048 is 2, so adding 1 to 2048 returns 2048 (the exact sum rounds back down). Summing 4096 ones in a loop gives:

```
fp16 accumulator:  sum of 4096 ones = 2048.0      (stuck at 2048)
fp32 accumulator:  sum of 4096 ones = 4096.0
```

and a dot product of 4096 random FP16 values (NumPy, a fixed seed; exact value −18.1610) gave −18.3594 with an FP16 accumulator (error 0.198) and −18.1610 with an FP32 accumulator (error 0.000026). A matrix multiply with `K = 4096` is 4096 such additions per output, so narrow accumulation would destroy the result. The standard recipe is therefore: **store and move in 16 or 8 bits, multiply in 16 or 8 bits, accumulate in FP32, and round to the narrow format once at the end.**

### 5.6 Structured sparsity and the headline numbers

Ampere introduced **2:4 structured sparsity**: in every group of four consecutive values at most two may be non-zero. The matrix is stored compressed (two values and their positions per group), nearly halving storage and bandwidth, and the sparse tensor core skips the zeros, doubling the math rate ([Ampere article](https://developer.nvidia.com/blog/nvidia-ampere-architecture-in-depth/): 312 Tflop/s dense and 624 with sparsity for A100 FP16). The weights must be pruned to that pattern and usually fine-tuned afterwards.

The consequence for reading a data sheet: **an H100's "1,979 Tflop/s FP16" is the sparse figure; a model that is not pruned 2:4 runs against 989.** Quote the dense figure unless you know the model is sparse. The same applies to "3,958 FP8 Tflop/s".

---

## 6. Feeding the engine: asynchronous copies, TMA and clusters

### 6.1 The classic tile loop and its stall

A tiled kernel (a matrix multiply, a convolution, attention) repeats three steps for every tile along the reduction dimension:

```cpp
for (int k = 0; k < K; k += TILE) {
    float4 r = global[...];          // 1. load a tile piece into a register: 400+ cycles of latency
    shared[...] = r;                 // 2. store it into shared memory
    __syncthreads();                 //    all threads wait here
    compute(shared);                 // 3. tensor cores or FMAs work on the tile
    __syncthreads();                 //    all threads wait again before the buffer is reused
}
```

Two things waste time. The data pass through registers, which uses register-file capacity and instruction slots. And the compute cannot begin until the whole tile has arrived, so the tensor cores sit idle during each load; other resident blocks can fill that gap only if there are some, and tensor-core kernels use big tiles and a lot of shared memory, often leaving room for only one or two blocks per SM (tutorial 22, section 8). The remedy is **software pipelining inside the block**: load tile `k+1` while computing on tile `k`.

### 6.2 `cp.async`: global to shared without registers (Ampere)

Compute capability 8.0 added an instruction that copies from global memory **directly into shared memory**, asynchronously, with no register in between. Per the [CUDA programming guide](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html), it copies 4, 8 or 16 bytes per thread; the 4- and 8-byte forms go through L1 and the 16-byte form bypasses it so it does not pollute L1; addresses must be aligned to the copy size, and 128-byte alignment performs best. Completion is tracked per thread with *commit groups*. The compiler output (clang 18, `sm_80`, using the NVPTX builtins):

```cpp
__shared__ float buf[256];
// (the builtins take address-space-qualified pointers; the casts are left out here)
__nvvm_cp_async_ca_shared_global_4(&buf[threadIdx.x], &in[blockIdx.x*256 + threadIdx.x]);   // start the copy
__nvvm_cp_async_commit_group();       // close the group of copies issued so far
__nvvm_cp_async_wait_group(0);        // wait until at most 0 groups are still pending
__syncthreads();                      // each thread waits only for its own copies, so a barrier is still needed
```

```
cp.async.ca.shared.global [%rd7], [%rd9], 4;     // 4-byte copy, cache at all levels
cp.async.commit_group;
cp.async.wait_group 0;
bar.sync 0;
```

and the 16-byte form (`cp.async.cg.shared.global ..., 16`, the `.cg` suffix meaning "cache at the L2 level only") for `float4`-sized pieces. The point of `wait_group N` is that it waits until *at most N groups are still in flight*, which allows a **multi-stage pipeline**:

```
stage buffers:   S0   S1   S2          (three tiles of shared memory)

iteration 0:  issue copy tile 0 → S0 ; issue copy tile 1 → S1 ; wait for S0 ...
iteration 1:  issue copy tile 2 → S2 ; compute on S0   (copies of S1, S2 still in flight)
iteration 2:  issue copy tile 3 → S0 ; compute on S1   (S2, S0 in flight)
```

With enough stages, the time per iteration is `max(copy time, compute time)` instead of their sum: if a tile takes 1.0 µs to arrive and 0.8 µs to compute, the sequential loop needs 1.8 µs per tile and the pipelined one 1.0 µs. (Illustrative numbers; the best number of stages depends on the shared-memory budget, which is the same budget that limits occupancy.)

### 6.3 The Tensor Memory Accelerator (Hopper)

Hopper (compute capability 9.0) went further: a dedicated hardware unit, the **Tensor Memory Accelerator (TMA)**, copies whole tiles. From NVIDIA's [Hopper article](https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/) and the [programming guide](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/async-copies.html):

- **One thread starts the copy.** It names a *tensor map* (a descriptor built on the host with `cuTensorMapEncodeTiled`, describing a tensor of up to 5 dimensions, its strides and the tile shape) and a coordinate. The hardware generates every address and can apply a swizzle so the tile lands in shared memory conflict-free (section 3.5). The rest of the block spends no instructions or registers on address arithmetic.
- **Completion is a counting barrier.** The copy signals a shared-memory *transaction barrier* that counts bytes; consumers wait on it and sleep rather than spin.
- **Requirements**: 16-byte aligned global address, strides that are multiples of 16 bytes, 128-byte aligned shared-memory destination. Fewer, larger copies perform best.
- **Directions**: global to shared, shared to global, and shared to the shared memory of another block in the same cluster; plus **multicast** to several blocks of a cluster at once.

The resulting pattern is **warp specialisation**: one producer warp issues TMA copies into a ring of shared-memory stages, while consumer warpgroups run `wgmma` on the stages as they fill (section 5.3). Both sides are asynchronous, and the tensor cores are kept busy by barriers instead of by threads polling.

### 6.4 Thread-block clusters and distributed shared memory

A **cluster** is a group of thread blocks that the hardware guarantees to run at the same time on SMs of one GPC (graphics processing cluster, a group of SMs). Per NVIDIA, the portable cluster size on H100 is 8 and the maximum 16. Blocks in a cluster can read, write and do atomics on **each other's shared memory** (*distributed shared memory*) over a dedicated SM-to-SM network, which NVIDIA reports as about 7 times faster than exchanging data through global memory. Clusters also enable TMA multicast, and that is where the arithmetic matters.

How much data must flow into an SM to keep its tensor cores busy? Take FP16 tiles, an output tile of `m × n` and a long reduction. Each step loads `m` rows of A and `n` columns of B (2 bytes each) and does `2·m·n` flops per reduction index, so the **arithmetic intensity of the tile is `m·n / (m+n)` flop per byte**. One H100 SM has `989.5 / 132 = 7.5 Tflop/s` of dense FP16:

| Output tile | Flop per byte | Operand bandwidth one SM needs at peak | All 132 SMs |
|---|---|---|---|
| 64 × 64 | 32 | 234 GB/s | 30.9 TB/s |
| 128 × 128 | 64 | 117 GB/s | 15.5 TB/s |
| 128 × 256 | 85 | 88 GB/s | 11.6 TB/s |
| 256 × 256 | 128 | 59 GB/s | 7.7 TB/s |

For scale, HBM delivers 3.35 TB/s and Chips and Cheese measured over 5.5 TB/s from the near L2 partition of the PCIe card. So even with the biggest tiles the demand on the L2, if every block fetches its own operands, is of the order of what the L2 can supply, with no headroom, and far above what HBM could. Practical GEMM kernels therefore use the largest tiles that fit in shared memory and rely on L2 hits. **Multicast** attacks the rest: if two blocks of a cluster compute neighbouring `128 × 256` tiles that need the same B tile, one TMA fetch can deliver it to both. Per reduction step the pair needs `(128 + 128 + 256) × 2 = 1024` bytes from L2 instead of `2 × (128 + 256) × 2 = 1536`, a saving of one third. (Arithmetic on a hypothetical configuration, not a statement about any particular library kernel.) This is the reason for the Hopper additions as a set: tensor cores fast enough that **feeding them is the problem**, and then asynchronous copies, TMA, clusters and multicast to solve it.

| Mechanism | Since | What it removes |
|---|---|---|
| `cp.async` (LDGSTS) | Ampere, CC 8.0 | the register round trip; lets copies overlap compute |
| `wgmma` | Hopper, CC 9.0 | warps waiting for the tensor core; operands read from shared memory |
| TMA | Hopper, CC 9.0 | address arithmetic in threads; bounds checks; manual swizzle |
| Clusters, DSMEM, multicast | Hopper, CC 9.0 | repeated L2 fetches of the same tile by neighbouring blocks |

---

## 7. The GPU roofline

### 7.1 The formula, with GPU numbers

[Tutorial 12](12_inside_a_modern_core.md) (section 9) introduced the idea: a kernel is limited either by arithmetic or by memory, and which one depends on its **arithmetic intensity** (AI), the flops it performs per byte it moves. The roofline states the limit:

```
attainable performance = min( peak flop/s ,  AI × bandwidth )
ridge point            = peak flop/s ÷ bandwidth        (the AI where the two limits meet)
```

In code:

```python
def roofline(flops, bytes_moved, peak, bw):
    ai = flops / bytes_moved                  # arithmetic intensity, flop per byte
    return ai, min(peak, ai * bw)             # attainable flop/s
```

A kernel with AI below the ridge is **memory-bound**: its speed is `AI × bandwidth` and extra arithmetic units do nothing. Above the ridge it is **compute-bound**. The ridge depends on *which* peak applies, and a GPU has several (H100 SXM at 3.35 TB/s, dense figures):

| Peak that applies | Tflop/s | Ridge (flop per byte) | Ridge in flop per 4-byte float loaded |
|---|---|---|---|
| FP64 on the ordinary lanes | 34 | 10 | 40 |
| FP32 on the ordinary lanes | 67 | **20** | 80 |
| TF32 tensor | 494 | 148 | 590 |
| FP16 / BF16 tensor | 989 | **295** | 1,180 |
| FP8 / INT8 tensor | 1,979 | 591 | 2,360 |

For comparison, the A100's FP16 ridge is 201 flop/B (40 GB version, 1.555 TB/s) or 153 (80 GB, 2.039 TB/s), and the V100's FP32 ridge was 16.6 (tutorial 22). Two trends matter. Compute has grown faster than bandwidth, so ridges rise with each generation. And each step down in precision doubles the ridge, because the arithmetic rate doubles while the bandwidth is the same. **A kernel that is compute-bound in FP32 can become memory-bound in FP8 without changing a line** — it has not got worse, it has hit the other roof.

### 7.2 Where common kernels sit

The AI of a kernel is a count you can do on paper: flops divided by the bytes that must cross the HBM interface at least once. For a single-pass kernel, take every array read once and written once. For a matrix multiply, `2N³` flops over `3N²` elements (the "ideal" figure assumes A, B and C each cross HBM exactly once):

| Kernel (H100 SXM) | Flops | Bytes | AI (flop/B) | Attainable | Bound |
|---|---|---|---|---|---|
| vector add, FP32 | 1 per element | 12 | 0.083 | 0.28 Tflop/s (0.4% of FP32 peak) | memory |
| saxpy, FP32 | 2 | 12 | 0.167 | 0.56 Tflop/s (0.8%) | memory |
| dot product, FP32 | 2 | 8 | 0.25 | 0.84 Tflop/s (1.2%) | memory |
| `y = relu(a*x + b)`, one fused kernel | 3 | 8 | 0.375 | 1.26 Tflop/s (1.9%) | memory |
| GEMM 128³, FP16 tensor | `2N³` | `6N²` | 43 | 143 Tflop/s (14%) | memory |
| GEMM 256³, FP16 tensor | | | 85 | 286 Tflop/s (29%) | memory |
| GEMM 1024³, FP16 tensor | | | 341 | 989 Tflop/s (100%) | compute |
| GEMM 4096³, FP16 tensor | | | 1,365 | 989 Tflop/s (100%) | compute |

What the table says in words:

- **The elementwise and reduction kernels of a network** (bias, activation, residual add, normalisation, softmax, dropout) sit two to three orders of magnitude below the ridge. They are limited by HBM bandwidth *entirely*, which makes the number of passes over the data the only thing that matters (section 8).
- **A GEMM is compute-bound only if it is big enough.** `AI = N/3` for FP16 square matrices, so N must exceed about 900 to cross the 295 ridge even with perfect reuse. The small GEMMs inside decoding and in small batches are memory-bound.
- **The ideal AI is an upper bound.** Real traffic is higher when tiles are re-fetched, and real kernels reach 80 to 90% of the bandwidth roof at best. Tutorial 22's wave quantisation and the launch overheads of section 9.5 take more.

### 7.3 Using it

The procedure is three lines: compute the kernel's AI from its flops and bytes; compare it with the ridge for the precision it uses; then act on the side it is on.

| If the kernel is… | Reduce bytes… | …or raise the roof |
|---|---|---|
| **memory-bound** (AI below ridge) | fuse kernels; keep data in registers and shared memory; structure of arrays and `float4` loads; lower precision; compress (quantise the KV cache); recompute instead of storing | more bytes in flight (occupancy, wide loads, unrolling); overlap copies with compute; use L2 reuse |
| **compute-bound** (AI above ridge) | nothing to gain from fewer bytes | use the right unit (tensor cores); the right precision; fix divergence and instruction mix; keep the units fed (section 6) |

The roofline in Nsight Compute's `SpeedOfLight_RooflineChart` section plots your kernel as a point against these roofs (see the [profiling guide](https://docs.nvidia.com/nsight-compute/ProfilingGuide/index.html)); the vertical distance to the roof is the room left, the horizontal distance to the ridge is how far the kernel is from changing regime.

### 7.4 Worked example: decoding a language model

A large language model generates one token at a time (*decoding*). Each new token needs the whole model's weights and the attention state of all previous tokens (the **KV cache**). Take Llama 3 8B (the configuration is on [Hugging Face](https://huggingface.co/unsloth/llama-3-8b/raw/main/config.json): 32 layers, hidden size 4096, feed-forward size 14,336, 32 attention heads, 8 key-value heads, vocabulary 128,256) on one H100 SXM with BF16 weights.

**Parameters.** Per layer: attention `4096×4096` (Q) + `4096×1024` (K) + `4096×1024` (V) + `4096×4096` (O) = 41.9 M; feed-forward `3 × 4096 × 14,336` = 176.2 M; total 218.1 M per layer, 6.98 B for 32 layers. The embedding and the output head are `128,256 × 4096` each: 1.05 B. **Total 8.03 B parameters = 16.06 GB in BF16.**

**KV cache per token** = 2 (K and V) × 32 layers × 8 KV heads × 128 head dimension × 2 bytes = **131,072 bytes = 128 KiB**. A 4,096-token sequence therefore holds 512 MiB. (With 32 KV heads instead of 8, i.e. no grouped-query attention, it would be 512 KiB per token and 2 GiB per sequence.)

**One decoding step for a batch of B sequences** moves the weights once, whatever B is, and each sequence's KV cache once; it does `2 × params` flops per sequence for the matrices plus the attention flops. The step time is bounded below by `max(bytes / 3.35 TB/s, flops / 989 Tflop/s)`:

| Batch B (context 4,096) | Bytes per step | Memory time | Compute time | Step time | Tokens/s | Memory ÷ compute |
|---|---|---|---|---|---|---|
| **1** | 16.6 GB | 4.95 ms | 0.02 ms | **4.95 ms** | **202** | 269 |
| 8 | 20.4 GB | 6.08 ms | 0.15 ms | 6.08 ms | 1,317 | 41 |
| 32 | 33.2 GB | 9.92 ms | 0.59 ms | 9.92 ms | 3,225 | 17 |
| **64** | 50.4 GB | 15.05 ms | 1.18 ms | **15.05 ms** | **4,252** | 13 |
| 119 (all of the 80 GB used) | 79.9 GB | 23.86 ms | 2.19 ms | 23.86 ms | 4,986 | 11 |

(The step time is the larger of the two columns, and it is the memory column throughout. 119 is `(80 GB − 16.06 GB) / 0.537 GB`, the number of 4,096-token sequences whose KV caches fit beside the weights; a real server keeps a margin.)

Read the table.

- **Batch 1 is memory-bound by a factor of 269.** The matrix-vector products do 2 flops per 2-byte weight, AI = 1 flop/byte, against a ridge of 295. The GPU's arithmetic is idle 99.6% of the time; the step time is the time to stream 16.6 GB, 4.95 ms, so **at most 202 tokens/s**, whatever the tensor cores can do. (Databricks' [LLM inference notes](https://www.databricks.com/blog/llm-inference-performance-engineering-best-practices) call the ratio of achieved to peak bandwidth *MBU*; their example is a 7B model at 14 ms per token, moving 14 GB in 14 ms = 1 TB/s of a 2 TB/s peak, an MBU of 50%.)
- **Batch 64 is still memory-bound, by a factor of 13.** It does 64 times the flops and takes only 3.0 times as long per step (15.05 against 4.95 ms), so throughput is **21 times higher (4,252 against 202 tokens/s)** while each user waits three times longer per token. This is the batching trade-off in one line: bandwidth is shared, latency is not. (Databricks measured the same shape on an A100: 14 times the throughput for about 4 times the latency at batch 64, for a different model and workload.)
- **The weights alone would cross the ridge at batch 295**, because decode AI equals the batch size: `B × 2 flop / 2 B = B` flop/byte against 295. With FP8 weights the AI doubles to `2B` and the FP8 ridge also doubles, so the crossover stays at 295. But the **KV cache never amortises**: each sequence has its own, so its bytes grow with B, and its AI is fixed. For this model each layer reads `4096 × context` bytes of K and V per token and does `4 × 4096 × context` flops, **AI = 4 flop/B** (it would be 1 without grouped-query attention). At batch 64 and context 4,096 the KV traffic is 34 GB, more than twice the weights' 16 GB. At a shorter context of 512 the crossover moves out, but at batch 295 it is still memory-bound by a factor of 2.2 and at 512 by 1.8.
- **Capacity stops you before the roof does.** On one 80 GB card the 4,096-token batch cannot exceed about 119, still 11 times memory-bound.

So the practical levers on decode speed are all about bytes, in the order of their effect: (1) fewer bytes per weight (FP8 or 4-bit weights roughly halve or quarter the weight traffic: with FP8 the batch-1 ceiling rises from 202 to 391 tokens/s at context 4,096); (2) fewer KV bytes (grouped-query attention, a quantised KV cache, paging); (3) a larger batch to share the weight reads; (4) more GPUs, which add bandwidth as well as capacity (section 10). Then there is **prefill**, the pass that processes the whole prompt at once: reading the weights once for `P` prompt tokens gives AI ≈ `P`, compute-bound above about 300 tokens. That is why prefill and decode are tuned, and sometimes scheduled on separate hardware, differently.

---

## 8. Cutting bytes: fusion and FlashAttention

### 8.1 Kernel fusion

The simplest way to cut traffic is to stop writing intermediate results to HBM. Take `y = relu(a·x + b)` over 2²⁸ floats (1 GiB), done as three kernels (multiply, add, relu) or as one:

| | Kernels | Traffic | Time at the 3.35 TB/s peak |
|---|---|---|---|
| Unfused | 3 (each reads 1 GiB, writes 1 GiB) | 6 GiB | 1.92 ms |
| Fused | 1 (read x, write y) | 2 GiB | 0.64 ms |

A **3× speed-up** with no change to the arithmetic, and one kernel launch instead of three (section 9.5). The same idea applies at every scale: fuse bias, activation and dropout into one pass; apply them to a GEMM's accumulators while they are still in registers (the "epilogue") instead of writing the output and reading it back; fuse the layer normalisation. Deep-learning compilers (for example `torch.compile` and XLA) do this automatically for elementwise chains; the large wins come from fusions across a reduction, which is where attention comes in.

### 8.2 Attention and the `N × N` matrix

For one attention head, with `N` tokens and head dimension `d`, the standard algorithm computes

```
S = Q Kᵀ            (N × N scores)
P = softmax(S)      (row by row)
O = P V             (N × d output)
```

and, as implemented layer by layer, it writes `S` to HBM, reads it back for the softmax, writes `P`, and reads it back for the product with `V`. Take a prompt of `N = 8192`, `d = 128`, FP16:

| | Standard attention | FlashAttention-style |
|---|---|---|
| `S` (or `P`) size | 8192² × 2 B = **134 MB per head** | never written to HBM |
| HBM traffic per head | about 4 × 134 MB + Q, K, V, O ≈ **545 MB** | Q, K, V, O only: 4 × 8192 × 128 × 2 B ≈ **8.4 MB** |
| Flops per head | `4N²d` = 34.4 Gflop | 34.4 Gflop (the same) |
| AI | 63 flop/B: below the 295 ridge, **memory-bound** | 4,096 flop/B: far above it, **compute-bound** |
| 32 heads: HBM time | **5.2 ms** | 0.08 ms |
| 32 heads: tensor-core time at 989 Tflop/s | 1.1 ms | 1.1 ms |

Standard attention spends almost five times longer moving the score matrix than computing with it; the FlashAttention-style version moves 65 times fewer bytes and turns the same flops into a compute-bound kernel. (Traffic counts are my estimate from the algorithm; a fused-softmax implementation shaves some of the standard figure. The order of magnitude is the point.) It also needs `O(N)` memory instead of `O(N²)`, which is what makes long contexts possible.

### 8.3 How FlashAttention gets there

[FlashAttention](https://arxiv.org/abs/2205.14135) is an *exact* algorithm (not an approximation) that is "IO-aware": it **tiles** Q, K and V into blocks small enough for shared memory, computes each block's scores in SRAM, and accumulates the output without ever storing `S` or `P`. The obstacle is the softmax, which needs the maximum and the sum over the *whole* row before it can normalise any element. The trick is the **online softmax**: process the keys one block at a time, keep a running maximum `m`, a running sum `ℓ` and a running output `o`, and when a later block raises the maximum, rescale what has been accumulated by `exp(m_old − m_new)`. The streaming version below, for one query against `n` keys, equals the full computation (checked in NumPy against the ordinary softmax-weighted sum: maximum difference 2.2 × 10⁻¹⁶):

```python
import numpy as np
m, l, o = -np.inf, 0.0, np.zeros(d)               # running max, running sum, running output
for i in range(0, n, block):
    sb, vb = s[i:i+block], v[i:i+block]           # one block of scores and the matching value vectors
    m_new = max(m, sb.max())
    scale = np.exp(m - m_new)                     # shrink everything seen so far to the new maximum
    pb    = np.exp(sb - m_new)
    l     = l * scale + pb.sum()
    o     = o * scale + pb @ vb
    m     = m_new
result = o / l                                    # identical to softmax(s) @ v
```

The paper analyses the HBM accesses and shows they are fewer than standard attention's for the SRAM sizes of real GPUs; it reports about 15% faster end-to-end BERT-large training (sequence 512) than the MLPerf 1.1 record, about 3× on GPT-2 (sequence 1K) and 2.4× on the Long Range Arena (sequences 1K to 4K). Those are the authors' numbers on the GPUs of the time.

### 8.4 The general lesson

On a GPU **flops are cheap and bytes are expensive**: the FP16 ridge says you can do about 295 flops for every byte you avoid moving. That makes *recomputation* attractive (recompute a value from data already in SRAM instead of writing it out and reading it back), and makes every intermediate tensor a suspect. The list of ways to move fewer bytes is the list of this tutorial: coalesced and vectorised access (section 2), tiling in shared memory (section 3), lower precision (section 5), fusion (section 8), and on the system side, keeping data resident on the GPU between kernels (section 9).

---

## 9. Between the CPU and the GPU

### 9.1 The links

Everything so far lived inside the GPU. Data come from the host over a link, and that link is far slower than HBM:

| Link | One-way bandwidth | Note |
|---|---|---|
| **HBM3** (inside the GPU) | 3,350 GB/s | the reference |
| PCIe Gen3 x16 | 15.75 GB/s | 8 GT/s × 16 lanes × 128/130 ÷ 8 |
| PCIe Gen4 x16 | 31.5 GB/s | |
| **PCIe Gen5 x16** | **63 GB/s** | NVIDIA quotes 128 GB/s, the two directions added |
| **NVLink 4** (H100 SXM, 18 links) | 450 GB/s | NVIDIA quotes 900 GB/s total |
| **NVLink-C2C** (Grace CPU to Hopper GPU, GH200) | 900 GB/s coherent | about 7 times PCIe Gen5, per [NVIDIA](https://www.nvidia.com/en-us/data-center/grace-hopper-superchip/) |

(The PCIe figures are the line rate minus the 128b/130b encoding overhead, computed from the line rate; achieved rates are lower. [Tutorial 25](25_buses_pcie_and_io_mapping.md) covers the protocol.) The GPU's memory is **53 times faster than a Gen5 link in one direction** (106 times for Gen4). Achievable host copies reach about 75% of the link: the Best Practices Guide quotes roughly 12 GB/s of a 16 GB/s Gen3 link for pinned memory. Every byte you move across PCIe costs about as much time as 50 to 100 bytes read from HBM.

### 9.2 Pageable, pinned and mapped memory

Memory from `malloc` is **pageable**: the operating system may move it or swap it out. A DMA engine needs fixed physical addresses, so the driver typically copies pageable data into a pinned staging buffer first. **Pinned (page-locked)** memory, from `cudaMallocHost`, `cudaHostAlloc` or `cudaHostRegister`, stays put, so the GPU's copy engine can read it directly. The [CUDA programming guide](https://docs.nvidia.com/cuda/cuda-programming-guide/02-basics/understanding-memory.html) says pinned memory is *required* for asynchronous copies and also speeds up synchronous ones. The cost is that it permanently removes physical pages from the operating system's use, so it is allocated once and reused, not allocated per call.

**Mapped (zero-copy)** host memory lets a kernel load directly from pinned host memory over the link. Each access crosses PCIe, with high latency and low bandwidth; the guide warns that it "should not be considered a performant alternative" to proper memory management. It suits data touched once.

### 9.3 Unified memory

`cudaMallocManaged` returns one pointer valid on the CPU and the GPU. The driver migrates pages to whichever processor touches them, using page faults: the GPU faults on a page that is not local, the driver moves it, the SM resumes. That is convenient and allows datasets larger than GPU memory (oversubscription). The cost shows in NVIDIA's [measurement](https://developer.nvidia.com/blog/maximizing-unified-memory-performance-cuda/) (an older one, CUDA 9) on a PCIe link with a 16 GB/s peak:

| Method | Bandwidth |
|---|---|
| on-demand page migration | 5.4 GB/s |
| `cudaMemPrefetchAsync` before the kernel | 10.9 GB/s |
| explicit `cudaMemcpyAsync` | 11.4 GB/s |

Faults stall the SM while the driver resolves them; prefetching ahead of the kernel recovers nearly all the performance. On Grace Hopper systems the CPU and GPU share a coherent address space over NVLink-C2C and the CPU can reach GPU-resident memory without migration, which changes the picture. The rule: managed memory for convenience and oversubscription, plus `cudaMemPrefetchAsync` and `cudaMemAdvise` hints when speed matters.

### 9.4 Overlapping copies with compute

A GPU has separate copy engines, so it can copy while computing, provided the copies use pinned memory and different **streams**. For a pipeline of three stages (host to device, kernel, device to host) of 100 ms each, cut the data into `n` chunks and pipeline them:

| Chunks `n` | 1 | 2 | 4 | 8 | 16 | limit |
|---|---|---|---|---|---|---|
| Total time | 300 ms | 200 ms | 150 ms | 125 ms | 112.5 ms | 100 ms |

The total is `(n + 2) × 100 / n` ms: the first chunk fills the pipeline, and in the limit only the slowest stage matters. The CUDA guide gives the same shape for a copy-and-compute pipeline: about `tE + tT/nStreams` against `tE + tT`. Overlap requires pinned host memory, non-default streams, and a GPU whose `asyncEngineCount` allows it.

### 9.5 Launch overhead

Every kernel launch and copy costs the CPU a few microseconds to submit. NVIDIA's [CUDA Graphs article](https://developer.nvidia.com/blog/cuda-graphs/) measured it on a V100 with a kernel that itself runs 2.9 µs:

| How 20 such kernels are submitted per step | Time per kernel (includes the kernel's own 2.9 µs) | Overhead per kernel |
|---|---|---|
| launch, then synchronise after each | 9.6 µs | 6.7 µs |
| launch all, synchronise once per step | 3.8 µs | 0.9 µs |
| captured once as a **CUDA graph** and relaunched | 3.4 µs | 0.5 µs |

A graph is captured from a stream once (about 400 µs, paid once) and relaunched with a single call; it was about 2.8 times faster than the synchronise-each-time loop and about 11% faster than the asynchronous one. The lesson is about proportions. When kernels run longer than the CPU takes to submit the next one (about 4 µs here), launches overlap with execution and cost nothing visible. When kernels are shorter, or the CPU stalls (a synchronisation, a copy that waits, framework code between operations), the GPU sits idle between them. **A kernel that runs for only a few microseconds is dominated by submission cost**, and a program made of hundreds of tiny kernels is limited by the CPU's issue rate rather than by the GPU. For scale, the decode step of section 7.4 takes about 5 ms; if it is 300 kernels the average runs about 16 µs, long enough to hide submission only while the CPU stays ahead. Inference engines therefore capture whole decode steps as graphs, so the CPU leaves the loop, and fuse the small kernels (section 8).

### 9.6 When a GPU loses to a CPU

Not every parallel-looking job should go to a GPU. Six reasons, in the order they are met:

**1. The data must cross the link and there is too little work per byte.** The GPU wins only if `bytes/B_link + work/F_gpu < work/F_cpu`. For a GPU much faster than the CPU this is, to a good approximation,

```
flop per byte transferred  >  F_cpu / B_link
```

With a 1 Tflop/s CPU and a 25 GB/s link (80% of Gen4) the break-even is **40 flop per byte moved**. A single streaming pass is hopeless: for `N = 2²⁴` floats of vector add (201 MB moved), the copies alone take 8.1 ms at 25 GB/s, while a CPU working at 50 to 200 GB/s of memory bandwidth finishes the whole job in 1.0 to 4.0 ms, and the GPU kernel itself would take only 0.067 ms. A matrix multiply improves with size, because it moves `12N²` bytes (FP32, two inputs in and one output out) for `2N³` flops: AI over the link is `N/6`. With assumptions stated (25 GB/s link, a GPU that achieves 40 Tflop/s in FP32, 10 µs launch):

| N | GPU: copy + compute (ms) | CPU at 100 Gflop/s | CPU at 1 Tflop/s | Winner |
|---|---|---|---|---|
| 64 | 0.012 (copy 0.002) | 0.005 | 0.001 | CPU |
| 128 | 0.018 (copy 0.008) | 0.042 | 0.004 | GPU vs the slow CPU, CPU vs the fast one |
| 256 | 0.042 (copy 0.031) | 0.336 | 0.034 | CPU is still ahead of the fast one |
| 512 | 0.143 (copy 0.126) | 2.68 | 0.268 | GPU |
| 1024 | 0.567 (copy 0.503) | 21.5 | 2.15 | GPU; **the copy is 89% of its time** |
| 4096 | 11.5 (copy 8.05) | 1,374 | 137 | GPU by 12× to 120× |

The break-even is about `N = 96` against the 100 Gflop/s CPU and about `N = 304` against the 1 Tflop/s one (assumed rates, not measurements). Even when the GPU wins, the copy is most of its time, so the real fix is to **keep data on the GPU** across many kernels and amortise one transfer over a long computation.

**2. Too little parallelism.** Tutorial 22: below a few tens of thousands of independent threads the SMs are idle and latency is not hidden.

**3. Irregular, branchy or pointer-chasing code** with divergent warps and uncoalesced access (section 2 here and tutorial 22, section 5).

**4. Latency matters more than throughput.** One small request on a cold GPU pays launch and copy overhead that a CPU does not.

**5. Heavy synchronisation or serial dependence.** GPUs synchronise within a block cheaply and across blocks expensively (Lee et al. note that on the GPU of their study a global barrier meant returning control to the host).

**6. The data does not fit in HBM (80 GB)** and cannot be streamed efficiently.

The classic reference is Lee et al.'s ["Debunking the 100X GPU vs. CPU myth"](https://www.cs.umd.edu/class/fall2019/cmsc714/readings/Lee-GPU.pdf): after optimising both sides of 14 throughput kernels on a Core i7-960 and a GTX 280, the GPU averaged only about 2.5× faster, with a spread from kernels where the CPU won (sort, a constraint solver) to about 15× (collision detection, which used the GPU's texture hardware). The authors credited the gap mostly to the GPU's roughly 4.7× memory bandwidth, and, importantly for this section, they **excluded the PCIe transfer** from their timings, noting that it could cut real-world GPU performance considerably. Treat the figure as dated (hardware has moved), but the method, comparing tuned code on both sides and counting the transfer, is the one to use.

---

## 10. More than one GPU

### 10.1 Why

A model outgrows one GPU in three ways. **Capacity**: a 70-billion-parameter model in BF16 is 140 GB, and an H100 has 80 GB. **Bandwidth**: decoding is bound by HBM bandwidth (section 7.4), and eight GPUs have eight times the bandwidth to spread the weights over. **Compute**: training a large model takes more flops than one GPU delivers in a reasonable time. The price is communication, and whether it is affordable depends on the links (topologies, switches and the network between servers are in [tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) sections 2 and 7).

### 10.2 The links and the hierarchy

Inside a server the GPUs are joined by **NVLink**, and an **NVSwitch** chip lets every GPU reach every other at full link speed (per NVIDIA's [Hopper article](https://developer.nvidia.com/blog/nvidia-hopper-architecture-in-depth/): 18 fourth-generation NVLinks per H100 for 900 GB/s in total; third-generation NVSwitch with 64 NVLink ports per switch, plus in-network reduction (SHARP) and multicast). An NVIDIA DGX H100 holds eight H100s with 640 GB of GPU memory, and for scale-out has eight ConnectX-7 network cards of up to 400 Gb/s each ([DGX H100 guide](https://docs.nvidia.com/dgx/dgxh100-user-guide/introduction-to-dgxh100.html)). One NVLink domain can extend beyond one server: the same article describes an NVLink Switch System connecting up to 256 GPUs.

Per GPU and per direction, the hierarchy looks like this:

| Tier | Bandwidth one way | Relative to NVLink |
|---|---|---|
| HBM, inside one GPU | 3,350 GB/s | 7.4× |
| **NVLink 4** inside the server (900 GB/s total) | **450 GB/s** | 1× |
| PCIe Gen5 x16 (GPU to host or to a network card) | 63 GB/s | 0.14× |
| one 400 Gb/s network port | **50 GB/s** | **0.11×** |

Leaving the server costs about a factor of nine in bandwidth per GPU, and more in latency. Everything in this section follows from that cliff.

### 10.3 Collectives

GPUs exchange data with a few standard patterns, implemented by NVIDIA's NCCL library and by AMD's RCCL:

| Collective | What it does |
|---|---|
| **all-reduce** | every GPU ends up with the element-wise sum (or max) of all GPUs' arrays |
| **reduce-scatter** | the sum is computed and each GPU keeps one slice of it |
| **all-gather** | each GPU contributes a slice and everyone ends up with the whole array |
| **broadcast** | one GPU sends its array to all |
| **all-to-all** | every GPU sends a different slice to every other (used for mixture-of-experts) |

An all-reduce is a reduce-scatter followed by an all-gather. In a **ring**, each GPU sends to its neighbour and receives from the other; over `n` GPUs each element crosses `2(n−1)` links in total, spread over `n` links of bandwidth `B`. The [NCCL tests documentation](https://github.com/NVIDIA/nccl-tests/blob/master/doc/PERFORMANCE.md) gives the best-case time for a buffer of `S` bytes:

```
t_allreduce ≈ S × 2(n − 1) / (n × B)         bus bandwidth = algorithm bandwidth × 2(n − 1) / n
```

The factor `2(n−1)/n` approaches 2 for large `n`, so **a large all-reduce takes about `2S/B` however many GPUs take part**: adding GPUs does not slow the bandwidth term, it only adds latency steps (`2(n−1)` of them in a ring). Three cases (the bandwidths are peak per-direction figures, so real times are longer):

| Case | `S` | `n` | `B` | Time |
|---|---|---|---|---|
| gradients of an 8B model in BF16, inside one server | 16 GB | 8 | 450 GB/s | **62 ms** |
| the same, across two servers (one 400 Gb/s port per GPU) | 16 GB | 16 | 50 GB/s | **600 ms** (9.6× slower) |
| one tensor-parallel activation all-reduce at decode, 64 tokens × 4096 × 2 B | 524 KB | 8 | 450 GB/s | 2.0 µs of wire time, but 14 ring steps of latency |

The last row is the other regime. For small messages the wire time is negligible and the cost is the latency of the steps (and the kernel launches that start them), so decode-time collectives are limited by latency, not bandwidth.

### 10.4 Three ways to split a model

| | Data parallel (DP) | Tensor parallel (TP) | Pipeline parallel (PP) |
|---|---|---|---|
| **What is split** | the batch; every GPU holds the full model | each weight matrix, across GPUs | the layers, into consecutive stages |
| **What is communicated** | an all-reduce of the gradients, once per step | an all-reduce of the activations, twice per layer, on the critical path | the activations between neighbouring stages (point to point) |
| **Volume** | the model size per step, amortised over the whole batch | the largest: per layer, per token | the smallest: one activation per stage boundary |
| **Tolerates a slow link?** | yes, it can be overlapped with the backward pass | **no** | yes, but stages idle in a "bubble" while the pipeline fills |

Per token, forward pass only, for the Llama 3 8B shape (hidden size 4096, BF16, 32 layers, 8 GPUs): tensor parallelism sends each GPU `2 all-reduces × 32 layers × 2(n−1)/n × 8,192 B = 917,504 B ≈ 0.92 MB`; pipeline parallelism sends 8,192 B across each stage boundary. **TP moves about 112 times more bytes per token than PP**, in small, latency-critical pieces. That ratio, combined with the 9× bandwidth cliff of section 10.2, gives the standard placement rule:

- **tensor parallelism inside an NVLink domain** (one server, up to 8 GPUs);
- **pipeline parallelism across servers**, because its traffic is small and point to point;
- **data parallelism outermost**, because its all-reduce happens once per step and overlaps with compute.

Real systems combine all three (and expert parallelism for mixture-of-experts models). NVIDIA's Megatron-LM paper ([Narayanan et al.](https://arxiv.org/abs/2104.04473)) shows the combination reaching 502 petaflop/s on 3,072 GPUs for a one-trillion-parameter model, 52% of the theoretical peak, and notes that applying the three naively causes costly inter-node communication and idle devices. The placement rule above is standard practice that follows from the volumes; the abstract does not itself state it.

### 10.5 Example: decoding a 70-billion-parameter model

BF16 weights are 140 GB. Decode reads them for every token, so the weights-only floor per token is `140 GB / (TP × 3.35 TB/s)`:

| Layout | Weights per GPU | Fits in 80 GB? | HBM time per token (floor) |
|---|---|---|---|
| 1 GPU, BF16 | 140 GB | no | — |
| TP = 2, BF16 | 70 GB | yes, 10 GB left for the KV cache | 20.9 ms |
| TP = 4, BF16 | 35 GB | yes, 45 GB left | 10.4 ms |
| TP = 8, BF16 | 17.5 GB | yes, 62.5 GB left | 5.2 ms |
| 1 GPU, FP8 | 70 GB | barely | 20.9 ms |
| TP = 2, FP8 | 35 GB | yes | 10.4 ms |

More GPUs shorten the floor by adding bandwidth, and free capacity for KV cache and larger batches; the all-reduces (two per layer) add latency on top. Quantising to FP8 gives the same speed-up per token as doubling the GPUs, with no network at all, which is one reason low-precision weights are so widely used for serving.

---

## 11. Sharing one GPU

A GPU costs a lot and a small job rarely fills it, so sharing is common. The mechanisms differ in how much isolation they give.

| Mechanism | How it shares | Isolation | Notes |
|---|---|---|---|
| **Time-slicing** (default for separate processes) | the GPU runs one process's work at a time and switches between them | separate address spaces, but no performance isolation | simple; a long kernel from one process delays the others |
| **MPS** (Multi-Process Service) | one server process holds a single connection to the GPU, and kernels from several client processes can run together | limited; NVIDIA documents SM and memory partitioning in newer versions | improves utilisation and reduces context switching for cooperating jobs ([NVIDIA MPS](https://docs.nvidia.com/deploy/mps/index.html)) |
| **MIG** (Multi-Instance GPU) | the hardware is partitioned into up to 7 instances, each with its own SMs, L2 slices, memory controllers and DRAM | **hard**: fault isolation, memory protection, SM isolation and memory-bandwidth QoS | fixed profiles; reconfigured only when the GPU is idle ([MIG concepts](https://docs.nvidia.com/datacenter/tesla/mig-user-guide/concepts.html)) |
| **vGPU** | the hypervisor presents a virtual GPU to each VM | VM-level | can be time-sliced or backed by MIG instances; [tutorial 27](27_hardware_virtualization.md) covers the virtualisation hardware, and Hopper's PCIe Gen5 interface adds SR-IOV |

MIG is the one with guarantees. On an H100 80 GB the profiles are (from NVIDIA's [profile table](https://docs.nvidia.com/datacenter/tesla/mig-user-guide/supported-mig-profiles.html)):

| Profile | Memory | SM share | Maximum instances |
|---|---|---|---|
| 1g.10gb | 1/8 | 1/7 | 7 |
| 1g.20gb | 2/8 | 1/7 | 4 |
| 2g.20gb | 2/8 | 2/7 | 3 |
| 3g.40gb | 4/8 | 3/7 | 2 |
| 4g.40gb | 4/8 | 4/7 | 1 |
| 7g.80gb | all | 7/7 | 1 |

The GPU has **seven SM slices but eight memory slices**, so packing is never perfect: seven 1g.10gb instances use 70 GB of the 80 and leave one memory slice unused; two 3g.40gb instances use six of the seven SM slices. If memory bandwidth is divided in proportion to the memory slices (my assumption; NVIDIA states that each instance gets dedicated bandwidth), a 1g.10gb instance has about `3.35 TB/s / 8 ≈ 420 GB/s`, so streaming its 10 GB takes about 24 ms: a small instance is a small GPU in bandwidth as well as in capacity. MIG instances can communicate only through limited IPC, so they are for **independent jobs** (several small inference services, one per tenant), not for splitting one model across instances; for that, use whole GPUs and NVLink.

How to choose: MIG when tenants must not interfere (and each job fits a profile); MPS when the jobs are one team's cooperating processes and you want utilisation; time-slicing when convenience matters more than predictability; vGPU when the unit of isolation is a virtual machine.

---

## 12. Where this goes next: the CUDA track

This tutorial gave the model; the [GPU and CUDA track](../12_gpu_cuda/00_plan.md) turns it into code and measurements on a real GPU, which this virtual machine does not have. How the pieces map:

| Idea in this tutorial | Where it becomes code |
|---|---|
| sectors, coalescing, bank conflicts, padding | CUDA programming (topic 02) and the `coalescing_and_transpose` lab |
| tiling and tensor-core GEMM on a roofline | topic 02 and the `tiled_gemm` lab |
| pinned memory, streams, overlap | topic 03 and the `streams_overlap` lab |
| PCIe, doorbells, the driver, profiling with Nsight | topic 03; [tutorial 25](25_buses_pcie_and_io_mapping.md) and [tutorial 26](26_interrupts_exceptions_and_dma.md) for the hardware side |
| decode, KV cache, FlashAttention, collectives, parallelism | topic 04 (inference systems) and Project 04 |

[Tutorial 24](24_accelerators_and_fpgas.md) steps back from the GPU to the other accelerators (systolic arrays, FPGAs, SmartNICs); [tutorial 25](25_buses_pcie_and_io_mapping.md) and [tutorial 26](26_interrupts_exceptions_and_dma.md) cover the PCIe and DMA machinery that every transfer in section 9 used.

---

## 13. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Streaming kernel reaches 12 to 25% of the HBM bandwidth; Nsight shows 16 to 32 sectors per request | stride or array-of-structures access | structure of arrays, `float4` loads, a shared-memory tile to transpose |
| Shared-memory kernel slower than its arithmetic suggests; many wavefronts per request | bank conflict: a stride sharing a factor with 32 | compute `gcd(stride, 32)` for the real warp shape; pad or XOR-swizzle |
| Kernel slows after adding one variable; `lmem` or spill stores in the `ptxas` report | register spill or a runtime-indexed array in local memory | unroll so indices are constants; shrink the array; raise the register cap and accept lower occupancy |
| Tensor-core GEMM reaches only 30% of peak at small sizes | memory-bound: AI `N/3` is below the 295 ridge | larger or batched matrices; fuse the epilogue; check tile and wave quantisation |
| FP16 training produces `inf` or NaN | overflow past 65,504 | loss scaling, or BF16 |
| Sums drift or stall in half precision | accumulating in FP16 | accumulate in FP32 |
| LLM decode far below the compute peak at batch 1 | memory-bound: weights and KV cache stream from HBM every token | quantise weights, batch requests, grouped-query attention, a quantised KV cache, more GPUs |
| Many short kernels; gaps between them in the Nsight Systems timeline | launch-bound | fuse kernels; capture a CUDA graph |
| Host-to-device copies dominate the timeline; no overlap | pageable memory, default stream, copies not chunked | pinned memory, several streams, keep data resident across kernels |
| A managed-memory program is 2× slower than the explicit-copy version | page faults on first touch | `cudaMemPrefetchAsync`, `cudaMemAdvise` |
| Multi-node training scales badly | an all-reduce or TP all-reduce crossing the 50 GB/s network | keep tensor parallelism in the node; overlap the data-parallel all-reduce; pipeline across nodes |
| Two jobs on one GPU slow each other | time-slicing or MPS interference | MIG, or separate GPUs |

---

## 14. Mistakes that cost points

1. **Quoting the sparse tensor-core number as the peak.** "1,979 Tflop/s FP16" on an H100 assumes 2:4 sparsity; the dense figure is 989.
2. **Treating coalescing as "thread `i` reads word `i`".** The count of 32-byte sectors is what matters; order within the sectors does not.
3. **Padding by one out of habit.** `+1` works for a 32-wide row of lanes and fails for other warp shapes; compute the pattern (the 16 × 16 tile with a 2-D warp needs row length 18).
4. **Calling local memory fast.** It is HBM, cached; and a spilled array times 270,336 threads can exceed the L2.
5. **Timing the kernel and ignoring the copy.** The honest comparison is copy in + kernel + copy out against the CPU; the GPU's win usually lives or dies on whether the data stay resident.
6. **Calling batch-1 decode compute-bound, or saying a batch of 64 makes it compute-bound.** Batch 1 is memory-bound by a factor of about 270; batch 64 is still memory-bound by about 13; the weights alone would cross the ridge near batch 295.
7. **Leaving the KV cache out of the byte count.** At batch 64 and 4,096 tokens it is more than twice the weights, and it does not amortise across the batch.
8. **Using the ideal (one-pass) arithmetic intensity as the achieved one.** Re-fetched tiles and spills raise the real traffic; and achieved bandwidth is 80 to 90% of the data sheet.
9. **Mixing up one-way and total link bandwidth.** NVLink's 900 GB/s and PCIe Gen5's 128 GB/s add both directions; one way they are 450 and 63.
10. **Putting tensor parallelism across the network.** Its all-reduces sit on the critical path of every layer.
11. **Believing MIG shares bandwidth.** It partitions it; a small instance gets a small slice, in return for isolation.
12. **Expecting a faster precision to help a bandwidth-bound kernel only through the arithmetic rate.** Its real benefit there is fewer bytes.

---

## 15. Questions you will be asked

**Q1. A warp loads `x[i*3]` (floats). How many sectors does it touch and how efficient is it?** The 32 lanes read bytes `12·i`, spanning `31×12 + 4 = 376` bytes. Counting 32-byte sectors gives 12 (the same as Problem A (d), a 12-byte struct), so 384 bytes move for 128 useful: 33% efficiency. Fix it with a structure-of-arrays layout, or a shared-memory tile that loads coalesced and reads the fields from the tile.

**Q2. What is a bank conflict and how do you remove one in a matrix transpose?** Shared memory has 32 banks of 4 bytes; word `w` is in bank `w mod 32`; lanes touching different words in one bank are serialised, and the number of wavefronts is `gcd(stride, 32)` for a regular stride. A column read of a `[32][32]` tile has stride 32 words, a 32-way conflict. Pad the row to 33 words (odd, so all banks differ) or XOR-swizzle the column index. Check the real warp shape, because `+1` is not universal.

**Q3. Why are tensor cores so much faster, and why do they accumulate in FP32?** One instruction does a whole tile (4,096 multiply-adds for 16 × 16 × 16), which is about 15 times the arithmetic rate of the ordinary lanes (989 against 67 Tflop/s dense on an H100). Narrow formats halve the bytes and double the rate, but adding thousands of products in FP16 stalls (2,048 plus 1 stays 2,048), so products are summed in FP32 and rounded once at the end.

**Q4. Why is LLM decoding memory-bound at batch 1, and does batching fix it?** Each token streams all the weights (16 GB for an 8B model in BF16) while doing 2 flops per weight, an intensity of 1 flop/B against a ridge of 295. At 3.35 TB/s that is 4.8 ms per token whatever the compute. Batching shares the weight reads: batch 64 gives about 21 times the throughput for 3 times the latency, but is still memory-bound by about 13 times, because the per-sequence KV cache (AI about 4) does not amortise and grows with the batch. Beyond batching, cut bytes (FP8 weights, grouped-query attention, a quantised KV cache) or add GPUs.

**Q5. What does FlashAttention change, and is it an approximation?** Standard attention writes the `N × N` score and probability matrices to HBM and reads them back; at N = 8192 that is hundreds of MB per head and makes the layer memory-bound. FlashAttention tiles Q, K and V into shared memory and uses an online softmax (a running maximum and sum) so the matrix is never stored: the same flops, about 65 times fewer HBM bytes, `O(N)` memory. It is exact, not an approximation.

**Q6. Pinned, pageable and unified memory: when each?** Pageable memory can be moved by the operating system, so copies go through a staging buffer and cannot overlap with kernels. Pinned memory is locked, gives the best copy bandwidth and is required for asynchronous copies, but is a scarce resource. Unified memory (`cudaMallocManaged`) migrates pages on demand: convenient and allows oversubscription, but page faults cost bandwidth (5.4 GB/s on demand against 10.9 GB/s with prefetch in NVIDIA's measurement), so prefetch.

**Q7. When does a GPU lose to a CPU?** When the data cross PCIe with too little work per byte (the break-even is about `F_cpu / B_link` flop per byte moved: 40 for a 1 Tflop/s CPU and a 25 GB/s link), when there is too little parallelism, when the code is branchy or irregular, when latency matters for a single small request, or when the data do not fit. Even tuned kernels often gain only a small factor over a well-tuned CPU.

**Q8. How would you place tensor, pipeline and data parallelism on a cluster, and what does an all-reduce cost?** Tensor parallelism inside an NVLink domain (450 GB/s per direction), pipeline parallelism across nodes (small point-to-point traffic over 50 GB/s ports), data parallelism outermost, overlapped with the backward pass. A ring all-reduce of `S` bytes over `n` GPUs takes about `2(n−1)/n × S / B`, nearly `2S/B` for any large `n`; 16 GB takes 62 ms over NVLink and 600 ms across two servers.

**Q9. MIG, MPS or time-slicing?** MIG partitions the hardware (SMs, L2 slices, memory controllers) into up to 7 instances with fault and bandwidth isolation, for tenants who must not interfere. MPS lets several cooperating processes share one GPU's execution for utilisation, without hard isolation. Time-slicing gives each process the GPU in turn: simplest, least predictable.

---

## 16. Problems

**How to use this section.** All four are solvable on paper; each says *Where* to check the answer and *How*. The checks use only Python, which you have; nothing needs a GPU. Do the paper part first, then run the check, and fix the paper answer if they disagree (the disagreement is where the learning is). Time: about 30 to 40 minutes each.

### Problem A (paper, then a script): count the sectors

Assume the base address is 128-byte aligned and the warp has 32 lanes. For each access, give the number of 32-byte sectors, the bytes moved, and the share of moved bytes the warp actually uses.

(a) `float x[i + 3]` (b) `float x[2*i]` (c) `double x[i]` (d) `struct { float a, b, c; } s[...]`, reading `s[i].a` (e) `float x[33*i]` (f) `half x[i]` (g) `float4 x[i]` (h) `float x[perm[i]]`, where `perm` is a permutation of 0 to 31 (i) `float A[i*1000]` (j) `float x[i/4]`. Then say which two you would fix first in a real kernel and how.

*Where:* paper first; then Python (the 8-line `sectors` function from section 2.2). To see the compiler's view of (d), compile a kernel with a 12-byte struct using the `mini_cuda.h` setup of tutorial 22, section 11.2, and look for `mul.wide.s32 ..., 12`.
*How:* for each lane compute the byte range `[addr, addr+size)`, list the distinct `addr // 32` values, count them. Use `used = 32 × size` for the denominator (case (j) is the exception: 4 distinct floats).

### Problem B (paper, then a script): spot the bank conflict

For each shared-memory access by one warp (32 lanes), give the number of wavefronts (1 means conflict-free):

(a) `float s[32][32]`, lane `l` reads `s[l][5]` (b) `float s[32][36]`, lane `l` reads `s[l][5]` (c) `float s[32][17]`, lane `l` reads `s[l][5]` (d) `double s[32]`, lane `l` reads `s[l]` (e) lane `l` reads the float `s[2*l]` (f) a 16 × 16 float tile read as `tile[tx][ty]` by a warp laid out as 16 columns × 2 rows (`tx = l % 16`, `ty = l / 16`), for row lengths 16, 17, 18, 20 and 24. Finally, (g) give an index formula for a `[32][32]` tile that makes both its row writes and its column reads conflict-free without padding.

*Where:* paper with the `gcd` rule; check with the `wavefronts` function of section 3.2.
*How:* word index = row × row_length + column; bank = word mod 32; wavefronts = the largest number of *distinct words* in one bank. For (g) think of XOR.

### Problem C (paper, then a script): place it on the roofline

H100 SXM: 3.35 TB/s, 989 Tflop/s dense BF16 (ridge 295 flop/B). Give the arithmetic intensity, the attainable performance and the time, and say memory- or compute-bound:

(i) a residual add of two `[8192 × 4096]` BF16 tensors into a third (ii) a BF16 GEMM `8192 × 4096 × 4096` (M × K × N), each matrix crossing HBM once (iii) the same GEMM with M = 16 (decode at batch 16, one weight matrix). Then use the Llama 3 8B numbers of section 7.4 (16.06 GB of weights, 128 KiB of KV cache per token, 4 attention flops per KV byte-pair as derived there): (iv) for batch 32 and context 8,192, give the bytes per step, the step time, the tokens per second and the share of the bytes that is KV cache; (v) how much does the throughput improve if the weights go to FP8 (KV stays BF16)?

*Where:* paper, then a few lines of Python; section 7.4 is the model.
*How:* AI = flops / bytes; time = max(flops / peak, bytes / BW). For (iv) bytes = weights + B × 128 KiB × context; flops = B × (2 × 8.03e9 + 4 × 32 × 4096 × context).

### Problem D (paper, then a script): when does the PCIe copy cancel the speed-up?

A job takes **80 ms on the CPU**. On the GPU its kernel takes **2 ms**, and it needs **400 MB in** and **400 MB out**. (a) With pageable memory at 10 GB/s, what is the GPU total time and the speed-up? (b) With pinned memory at 25 GB/s? (c) With pinned memory, the work cut into 8 chunks and pipelined on two copy engines (host-to-device and device-to-host can run at the same time as the kernel)? (d) What is the best speed-up possible, even with a kernel that takes zero time, without and with overlap of the two transfers? (e) The data stay on the GPU and the kernel runs 20 times (the CPU also does 20 runs): what is the speed-up? (f) What pageable bandwidth is needed for the GPU to break even in (a)?

*Where:* paper; a calculator or three lines of Python.
*How:* total = copy in + kernel + copy out; for the pipeline, the slower copy engine sets the rate: chunk time = (400 MB / 8) / 25 GB/s.

---

## Answers

### Problem A

| | Sectors | Bytes moved | Used | Share used |
|---|---|---|---|---|
| (a) `x[i+3]` | 5 | 160 | 128 | 80% (misaligned) |
| (b) `x[2*i]` | 8 | 256 | 128 | 50% |
| (c) `double x[i]` | 8 | 256 | 256 | 100% |
| (d) 12-byte struct, `.a` | 12 | 384 | 128 | 33% |
| (e) `x[33*i]` | 32 | 1,024 | 128 | 12.5% (132-byte stride: one sector per lane) |
| (f) `half x[i]` | 2 | 64 | 64 | 100% (a small request: vectorise to `half2`/`float4` for more bytes in flight) |
| (g) `float4 x[i]` | 16 | 512 | 512 | 100% |
| (h) permutation inside one 128 B block | 4 | 128 | 128 | 100% (order does not matter) |
| (i) `A[i*1000]` | 32 | 1,024 | 128 | 12.5% |
| (j) `x[i/4]` | 1 | 32 | 16 (4 distinct floats) | broadcast-like; one sector |

Fix first: **(i)** and **(e)/(d)** are the worst: the column walk (i) by transposing through a shared-memory tile (or changing which index the lanes step along), and the strided/AoS cases (d) and (e) by a structure-of-arrays layout. (a) costs only one extra sector and is cheap to leave, but align the base if you control the allocation. A check by hand against the definition: for (b) the lanes cover bytes `8i … 8i+3`, which sit in sectors `⌊8i/32⌋ = 0 … 7`: eight sectors.

### Problem B

(a) **32-way**: word `32·l + 5`, bank 5 for every lane. (b) **4-way**: `gcd(36, 32) = 4`. (c) **1**: 17 is odd, so the 32 banks are all different. (d) **2**: not a conflict, since 32 lanes × 8 bytes = 256 bytes need two passes of the 128-byte port. (e) **2**: stride 2 words, `gcd(2, 32) = 2`; lanes `l` and `l+16` share a bank. (f) For row lengths 16, 17, 18, 20, 24 the function gives **8, 2, 1, 2, 4**: only 18 (and 22, 26, 30, i.e. 2 modulo 4) is conflict-free, because `tx × 18` is always even, so the 16 lanes of the first row fill the 16 even banks and the second row (`+1`) the 16 odd ones. Padding by one, the habit from the 32-wide case, leaves a 2-way conflict. (g) Store element `(row, col)` at word `row·32 + (col XOR row)`: a row write is a permutation of the 32 banks and a column read hits bank `col XOR row`, different for every row. The function confirms it for all 32 columns.

```python
# first paste wavefronts() from section 3.2, then:
L = range(32)
print(wavefronts([l*32 + 5 for l in L]), wavefronts([l*36 + 5 for l in L]), wavefronts([l*17 + 5 for l in L]))   # 32 4 1
print({R: wavefronts([(l % 16)*R + l // 16 for l in L]) for R in (16, 17, 18, 20, 24)})                          # {16: 8, 17: 2, 18: 1, 20: 2, 24: 4}
print(all(wavefronts([tx*32 + (c ^ tx) for tx in L]) == 1 for c in range(32)))                                   # True
```

### Problem C

(i) Flops: `8192 × 4096 = 33.6 M` (one add per element); bytes: 3 tensors × 67.1 MB (33.6 M elements × 2 B) = 201 MB; **AI = 0.17**, attainable 0.6 Tflop/s, time = 201 MB / 3.35 TB/s = **60 µs: memory-bound**. (ii) Flops `2·8192·4096·4096 = 275 G`; bytes `2·(8192·4096 + 4096·4096 + 8192·4096) = 168 MB`; **AI = 1,638**, compute-bound, time = 275 G / 989 T = **278 µs**. (iii) Flops `2·16·4096·4096 = 0.54 G`; bytes ≈ 33.8 MB (dominated by the 33.6 MB weight matrix); **AI = 15.9**, attainable 53 Tflop/s (5% of peak), time = 33.8 MB / 3.35 TB/s = **10.1 µs: memory-bound**. It is the same weight read as batch 1; the batch got 16 times the flops for free.

(iv) KV per sequence at 8,192 tokens: `8192 × 128 KiB = 1.00 GiB`; for 32 sequences 34.4 GB; plus 16.06 GB of weights: **50.4 GB per step**, time 50.4 GB / 3.35 TB/s = **15.05 ms**. Flops `32·(2·8.03e9 + 4·32·4096·8192) = 651 G`, 0.66 ms at the peak: memory-bound by 23×. Throughput **32 / 15.05 ms ≈ 2,126 tokens/s**, and **68% of the bytes are KV cache**. (v) With FP8 weights the weights drop to 8.03 GB, the step to 42.4 GB, **12.65 ms, 2,529 tokens/s: only 19% better**, because the KV cache is two thirds of the traffic. This is the case for a quantised KV cache and grouped-query attention, not just quantised weights.

### Problem D

(a) Pageable: 400 MB / 10 GB/s = 40 ms in, 40 ms out, plus 2 ms: **82 ms against 80 ms on the CPU: 0.98×, slower**. (b) Pinned: 16 ms each way: **34 ms, 2.35×**. (c) Eight chunks of 50 MB: 2.0 ms in, 0.25 ms of kernel, 2.0 ms out per chunk. The host-to-device engine runs for 16 ms; the first device-to-host copy can start at 2.25 ms and its engine is then busy for 8 × 2.0 ms, so the total is about `2.0 + 0.25 + 16 = 18.25 ms`: **4.4×**. (d) A kernel of zero time without overlap still pays 32 ms: at most **2.5×**; with the two transfers perfectly overlapped, 16 ms: at most **5×**. The link, not the kernel, sets the ceiling. (e) Data resident: one transfer pair (32 ms) plus 20 kernels (40 ms) = 72 ms against 1,600 ms: **22×**. (f) `2 × 400 MB / B + 2 ms < 80 ms` gives `B > 10.3 GB/s`: the pageable link at 10 GB/s misses by a hair, and any slower and the GPU loses outright.

The moral for an interview: quote the speed-up of the *whole pipeline*, say which memory the copies use, and say whether the data stay resident.

---

## Cheat sheet

- **Memory levels (H100 SXM):** registers 256 KB/SM (33 MiB total); L1 + shared 256 KB/SM (up to 228 KB shared); L2 50 MB (two partitions); HBM3 80 GB at 3.35 TB/s over 5,120 pins (5.2 Gbit/s each); constant 64 KB; local = HBM.
- **Coalescing:** a warp access costs the number of distinct 32-byte sectors (128-byte line = 4 sectors). Unit-stride floats 4; offset by one 5; stride 2 8 (50%); stride ≥ 8 or random 32 (12.5%); AoS field of a 16-byte struct 16 (25%); broadcast 1. Order does not matter. Wide loads (`float4`) put more bytes in flight.
- **Shared memory:** 32 banks × 4 bytes, bank = word mod 32; wavefronts = largest number of *distinct* words in one bank; same word = broadcast; stride conflict = `gcd(stride, 32)`; fix with odd row length (+1 for a 32-wide warp, 18 for a 16 × 2 warp) or XOR swizzle.
- **Constant:** fast only when all lanes read one address. **Read-only:** `const __restrict__` gives `ld.global.nc`. **Local:** runtime-indexed arrays and spills; in HBM; check `ptxas -v`.
- **Tensor cores:** one instruction = a tile (16 × 16 × 16 = 4,096 multiply-adds, 128 FMA instructions' worth); about 15× the ordinary lanes; accumulate in FP32. `wmma.mma.sync` (Ampere), `wgmma` (Hopper: warpgroup of 128 threads, asynchronous, B from shared memory); AMD `v_mfma_*`.
- **Formats (S/E/M):** FP32 1/8/23; TF32 1/8/10; FP16 1/5/10 (max 65,504); BF16 1/8/7 (FP32 range); FP8 E4M3 1/4/3 (max 448), E5M2 1/5/2 (max 57,344); INT8 with a scale. H100 dense Tflop/s: TF32 494, FP16/BF16 989, FP8/INT8 1,979. **Data-sheet numbers are with 2:4 sparsity: halve them.**
- **Feeding the engine:** `cp.async` (Ampere) global to shared without registers, multi-stage pipelines; TMA (Hopper) one thread copies a tile described by a tensor map, swizzles, signals a byte-counting barrier; clusters (≤ 8 portable, 16 max) share shared memory (DSMEM) and multicast tiles.
- **Roofline (H100):** ridge = peak/3.35 TB/s: FP32 20, TF32 148, FP16/BF16 295, FP8 591 flop/B. Tile AI = `mn/(m+n)` flop/B (FP16 inputs). GEMM `N³` FP16: AI = `N/3`.
- **LLM decode (Llama 3 8B, H100):** 8.03 B params = 16.06 GB; KV = 128 KiB/token; batch 1: 4.95 ms/token = 202 tok/s (memory-bound ×269); batch 64, ctx 4096: 15.05 ms step = 4,252 tok/s (21× throughput, 3× latency, still memory-bound ×13); weights-only crossover batch ≈ 295; attention AI ≈ 4; prefill AI ≈ prompt length.
- **Fusion and FlashAttention:** fusing three elementwise kernels cuts traffic 3×; standard attention at N = 8192 writes 134 MB per head, FlashAttention streams it away with online softmax (65× fewer bytes, exact, O(N) memory).
- **Links (one way):** HBM 3,350; NVLink 4 450; PCIe Gen5 x16 63 (Gen4 31.5, Gen3 15.75); a 400 Gb/s port 50 GB/s; NVLink-C2C 900 GB/s (GH200). Pinned reaches ~75% of the link. Unified memory: 5.4 on demand, 10.9 prefetched, 11.4 explicit (16 GB/s link, NVIDIA).
- **Launch:** kernels of a few µs are launch-bound (V100: 9.6 µs sync, 3.8 µs async, 3.4 µs graph, for a 2.9 µs kernel); CUDA graphs and fusion fix it.
- **A GPU loses** when flop per byte moved over the link < `F_cpu / B_link`, when parallelism is low, code is irregular, latency matters, or the data exceed HBM. Count the copy.
- **Multi-GPU:** ring all-reduce `t = 2(n−1)/n × S/B`; TP inside the NVLink domain, PP across nodes, DP outermost; TP moves ~112× the bytes of PP per token (8B shape). 70B BF16 = 140 GB → TP ≥ 2.
- **Sharing:** time-slicing (no isolation), MPS (shared execution), MIG (hardware partitions: 7 SM slices, 8 memory slices; H100 profiles 1g.10gb … 7g.80gb), vGPU (per VM).
- **Everything here came from vendor documents, published measurements and compiler output; nothing GPU-related was timed on a GPU.**

**Next:** tutorial 24, Accelerators and FPGAs (why accelerators exist, the TPU and systolic arrays, NPUs for edge AI, dataflow designs as the contrast to GPUs, SmartNICs and DPUs, FPGAs and heterogeneous SoCs). Say "next" when you are ready.
