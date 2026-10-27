# CO 21: SIMD and Vector Processing

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 11](11_superscalar_and_out_of_order_execution.md), section 10.1 (latency chains and multiple accumulators), [tutorial 12](12_inside_a_modern_core.md), section 4 (ports, latency and throughput) and [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (struct of arrays)*

By the end of this tutorial you can:

- explain data-level parallelism, and why one instruction on many elements is cheaper than many instructions on one;
- describe a vector register as a row of lanes, and say which operations work lane by lane and which cross lanes;
- explain the classic vector-machine ideas (a vector length register, strip-mining, chaining, masks) and recognise them again in Arm SVE and the RISC-V vector extension;
- compare SSE, AVX, AVX2 and AVX-512 with Arm NEON and SVE and with RISC-V vectors: register width, register count, masks, tail handling;
- vectorise a loop by hand with intrinsics, tail included, and say what the compiler needs before it will do the same;
- read a vectoriser report and name the reason a loop was not vectorised;
- explain why a floating-point sum vectorises only if the compiler may reorder it, what `-ffast-math` permits, and why several accumulators matter;
- explain alignment, cache-line splits, the real cost of gather and scatter, and when SIMD is limited by memory and not by arithmetic;
- explain denormal numbers, flush-to-zero and saturating arithmetic;
- choose between compile-time targets and run-time dispatch (`cpuid`, ifunc, `target_clones`, hardware capability bits);
- recognise the SIMD patterns inside real code: `memchr`, filtering, parsing, UTF-8 checks.

Tutorials 11 and 12 showed how a modern core starts four to six micro-operations per cycle, and that each of them does one scalar piece of work. A loop that adds two arrays is then limited to roughly one element per cycle by the loads, the store and the loop overhead, however wide the core is. SIMD attacks that limit from the other side: make each micro-operation carry 4, 8, 16 or 64 elements. It is the cheapest large speed-up in a processor, and it is why tutorial 12 called SIMD "the way below one cycle per element". It is also the feature most often left unused, because the compiler cannot always prove it is safe and a default build does not even enable the wide instructions.

**Where the numbers come from.** Measured on the cloud virtual machine of tutorials 14 to 20: an Intel Xeon (Golden Cove class core) with AVX2, FMA and AVX-512, two separate cores, a clock of about 3.2 GHz (I deduced it from a chain of dependent adds, 0.30 to 0.32 ns each), 48 KB of L1 data cache and 2 MB of L2 per core. Loops were timed with `clock_gettime`, best of five, pinned to one core, and cloud numbers drift, so figures are ranges over three runs. The Arm and RISC-V code is real compiler output (clang 18 and GCC 13 cross-compiling) and was **run under QEMU for correctness only**: I have no Arm or RISC-V hardware here, so nothing about their speed is measured. What Arm and RISC-V cores *do* is stated from the architecture rules.

> **The one-sentence model.** SIMD puts several independent elements into one wide register and applies one operation to all of them at once, which multiplies arithmetic throughput by the number of lanes but not memory bandwidth, and only works when the lanes are independent, the data are contiguous and there are no per-element branches; the compiler will find it for you only when it can prove all of that, so you read the assembly to see what you actually got.

---

## 1. Data-level parallelism

### 1.1 One instruction, many elements

Add two arrays of floats, element by element:

```c
for (int i = 0; i < n; i++) c[i] = a[i] + b[i];
```

A scalar compiler turns each trip into about seven instructions: two loads, one add, one store, an index increment, a compare and a branch. Every one of them is fetched, decoded, renamed, scheduled and retired. The arithmetic itself, the add, is one seventh of the work. Commonly quoted energy estimates say the same thing in joules: the adder costs far less than the machinery that decides to run it.

A vector instruction keeps all that machinery and widens the data:

```
scalar:   a0+b0     a1+b1     a2+b2     ...             8 add instructions

vector:   | a0 a1 a2 a3 a4 a5 a6 a7 |
        + | b0 b1 b2 b3 b4 b5 b6 b7 |                    1 vaddps (256-bit)
        = | c0 c1 c2 c3 c4 c5 c6 c7 |
```

The same seven instructions now handle 8 elements, so the front end, the scheduler and the retire stage do an eighth of the work per element. The cost is in the datapath: eight floating-point adders side by side instead of one, plus registers eight times wider. That is cheap compared with building a core that is eight times wider in every stage, which is why every general-purpose processor has SIMD units.

This is **SIMD**, single instruction multiple data, from Flynn's taxonomy in tutorial 18. The parallelism comes from the *data*: the same operation applies to many independent elements.

### 1.2 Registers as rows of lanes

A vector register is a row of **lanes**. The same 256 bits can be read as different shapes, chosen by the instruction, not by the register:

| 256-bit register holds | Lanes | Example instruction |
|---|---|---|
| 8 × 32-bit float | 8 | `vaddps ymm1, ymm2, ymm3` |
| 4 × 64-bit double | 4 | `vaddpd` |
| 32 × 8-bit integers | 32 | `vpaddb` |
| 16 × 16-bit integers | 16 | `vpaddw` |
| 8 × 32-bit integers | 8 | `vpaddd` |

Narrow elements give more lanes, which is why byte-level work (text, pixels, packets) gains the most from SIMD: a 256-bit register does 32 byte operations per instruction.

Operations come in two kinds, and the difference matters for cost.

- **Lane-wise** operations treat every lane independently: add, subtract, multiply, fused multiply-add, minimum and maximum, compare, bitwise logic, shifts, conversions. They are fast (latency 1 to 4 cycles, two or three per cycle) and scale perfectly with the lane count.
- **Cross-lane** operations move data between lanes: shuffle and permute, broadcast, pack and unpack, extract and insert, horizontal add, and the bridges to scalar code (`movemask`, which turns a vector of lane flags into an integer bitmask). They need wiring between lanes, so they are slower or limited. On x86 AVX2, many shuffles work *separately in each 128-bit half* of a 256-bit register, and a full cross-half permute is a different, slower instruction.

The first design rule of SIMD code follows: **keep the work lane-wise and cross lanes as rarely as possible**, ideally once at the end of a loop.

### 1.3 A compare and select, by hand

Sum the elements of `x` that are greater than 5, using 8 lanes and no branch:

```
x       =  3   9   1   7   5   8   2   6
t       =  5   5   5   5   5   5   5   5          (broadcast of the threshold)
x > t   =  0  ff   0  ff   0  ff   0  ff          (compare: all ones where true)
x & m   =  0   9   0   7   0   8   0   6          (keep x where the mask is set)
sum of lanes = 9 + 7 + 8 + 6 = 30
```

Three instructions (compare, and, add into an accumulator) handle eight elements and there is no branch to mispredict. The scalar loop does one compare-and-branch per element, and when the data are random the branch is wrong half the time (tutorial 10). Section 9.2 measures the difference. The masking idea, "compute everything, then select", is how SIMD handles `if`; it costs the work of both sides, but never a misprediction.

### 1.4 Three places to find data parallelism

| | SIMD in one core | Multiple cores | GPU |
|---|---|---|---|
| Unit | one thread with wide registers | several threads | thousands of threads, run in groups called warps |
| What it multiplies | arithmetic throughput per thread | everything, but needs synchronisation and cache coherence | arithmetic throughput and memory bandwidth |
| Conditionals | masks | each thread branches freely | warps diverge and lose speed ([tutorial 22](22_gpu_execution_model.md)) |
| Overhead to start | none (it is an instruction) | thread start-up and sharing (tutorials 18, 19) | kernel launch and a data transfer ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)) |

They multiply, not exclude each other. A sensible order is: use SIMD first, because it is free of synchronisation and needs no extra threads; then use cores; reach for a GPU when the data are large enough to pay for moving them.

### 1.5 How much can you gain?

The peak of the arithmetic units is the lane count times the number of units times the clock. I measured a chain-free fused multiply-add loop (12 independent accumulators, so no latency limit, as in tutorial 12) at each width:

| Width | Lanes (float) | Time per FMA instruction | FMA instructions per ns | Single-precision GFLOP/s |
|---|---|---|---|---|
| scalar | 1 | 0.156 to 0.172 ns | about 6.4 | about 12.8 |
| 128-bit (SSE) | 4 | 0.158 to 0.179 ns | about 6.3 | about 51 |
| 256-bit (AVX2) | 8 | 0.157 to 0.189 ns | about 6.4 | about 102 |
| 512-bit (AVX-512) | 16 | 0.174 to 0.199 ns | about 5.7 | about 184 |

*The ranges come from twelve measurements on a noisy cloud machine; I left out two bursts, one in which every column rose to about 0.2 ns and one stray 0.209 ns in the 256-bit column. The GFLOP/s column uses the quietest measurement of each row, 2 flops per lane per FMA.*

Read the table column by column. Every width takes about the same time per *instruction*, 0.157 ns, which is half a cycle at 3.2 GHz: two FMA units, each starting one instruction per cycle (tutorial 12). Width is therefore free until you run out of it. The 512-bit row is 10% slower per instruction in my best runs and I did not find out why (a lower clock under 512-bit load is the usual suspect, section 8.3), but it still does the most work.

So the arithmetic ceiling grows with the lane count, from 12.8 to 184 GFLOP/s here, a factor of 14. Real loops get less, because the loads, stores, tails and memory also matter. Section 5 shows how much less.

---

## 2. Vector machines and strip-mining

### 2.1 The classic design

Before SSE existed, scientific computers were built around **vector processors**. Their design is worth knowing because Arm SVE and the RISC-V vector extension brought it back. Five ideas:

1. **Long vector registers**, for example 64 elements of 64 bits each, holding a whole chunk of an array.
2. A **vector length register** (VL). An instruction operates on the first VL elements. The program does not hard-code how wide the machine is.
3. **Deeply pipelined functional units.** A vector add streams the elements through a pipelined adder, one result per cycle after a start-up delay, instead of using many adders side by side.
4. **Chaining.** The result of one vector operation feeds the next operation element by element, as soon as each result is ready, so a multiply and an add that depend on each other overlap in time.
5. **Memory operations built for arrays**: strided loads, indexed loads and stores (gather and scatter) and masks for conditionals.

Modern desktop SIMD (SSE, AVX, NEON) took the cheap half: wide registers and parallel lanes. It left out the vector length register, so the width is fixed in the instruction set, and it is why a new width (AVX after SSE) needed new instructions and a recompile. SVE and RVV put the length register back.

### 2.2 Strip-mining

If the hardware holds at most `MAXVL` elements, a loop over `n` elements is cut into strips. The hardware tells the program how many elements the next strip may take:

```
loop:   vl = setvl(n)           ; vl = min(n, MAXVL), chosen by the hardware
        v1 = vload  x           ; load vl elements
        v2 = vload  y
        v2 = a * v1 + v2        ; one vector FMA on vl elements
        vstore y, v2
        n  = n - vl
        x += 4 * vl;  y += 4 * vl
        if n > 0 goto loop
```

There is **no separate tail loop**: the last strip is simply shorter. Count the trips for `n = 21`:

| Machine | Elements per full strip | Trips | Scalar leftovers |
|---|---|---|---|
| vector machine, `MAXVL` = 8 | 8 | 3 (8 + 8 + 5) | none |
| the same binary on `MAXVL` = 4 | 4 | 6 (4 × 5 + 1) | none |
| AVX2 fixed 8 lanes | 8 | 2 vector trips | 5 scalar iterations |
| NEON fixed 4 lanes | 4 | 5 vector trips | 1 scalar iteration |

The fixed-width machines need a second loop for the remainder (section 4.5). The length-agnostic machines run the same code at any width.

### 2.3 Chaining, with numbers

Suppose a vector multiply and a vector add, each with a start-up latency of 4 cycles and then one element per cycle, run on 8 elements, and the add uses the multiply's result.

```
without chaining      multiply:  elements leave at cycles 4, 5, ... 11
                      add starts only when the whole multiply is done (cycle 11)
                      add:       elements leave at cycles 15, 16, ... 22         total 22 cycles

with chaining         add element i starts when multiply element i leaves (cycle 4 + i)
                      add:       elements leave at cycles 8, 9, ... 15            total 15 cycles
```

Chaining saves the whole length of the first operation except the start-up. It is the vector machine's version of forwarding and out-of-order overlap (tutorial 11), and a modern out-of-order core gives the same overlap automatically between dependent SIMD instructions, which is one reason it does not need a special feature for it.

### 2.4 Fixed width against length-agnostic

| | Fixed width (SSE, AVX, NEON) | Length-agnostic (SVE, RVV) |
|---|---|---|
| Width in the code | built into every instruction and intrinsic | read at run time (`cntw`, `vsetvli`) |
| Wider hardware later | needs new instructions and new code | the same binary uses it |
| Tail elements | scalar loop, or masked instruction | predicate or `vl` handles them |
| Cost | several code paths to maintain | harder to hand-tune shuffles; compiler support still maturing |

I ran one SVE binary under QEMU at vector lengths of 128, 256 and 512 bits and one RISC-V vector binary at VLEN 128, 256 and 512; both gave correct results for every array length from 0 to 70 with no change. That is the practical meaning of "length-agnostic".

---

## 3. The instruction sets

### 3.1 x86: SSE, AVX, AVX2, AVX-512

| Extension | Registers | Width | Count (64-bit mode) | Main additions |
|---|---|---|---|---|
| SSE, SSE2 | `xmm` | 128 bits | 16 | float and integer lane operations; two-operand form (`dest = dest op src`); SSE2 is part of the x86-64 baseline |
| AVX | `ymm` | 256 bits | 16 | 256-bit float; three-operand form (`dest = a op b`, sources untouched) |
| AVX2 | `ymm` | 256 bits | 16 | 256-bit integer; gather; variable shifts; cross-lane permutes |
| FMA3 | | | | fused multiply-add (own `cpuid` flag) |
| AVX-512 | `zmm` | 512 bits | 32 | eight mask registers `k0` to `k7`; masked operations; embedded broadcast; scatter; compress and expand; conflict detection; many sub-extensions (F, BW, DQ, VL, CD, VNNI and more) |

The registers overlap: `xmm0` is the low 128 bits of `ymm0`, which is the low 256 bits of `zmm0`. Three things to know:

- **The baseline is SSE2.** A default `gcc -O3` build for x86-64 uses 128-bit instructions only. To use AVX2 you pass `-mavx2 -mfma`, or `-march=x86-64-v3`, or `-march=native`, or you dispatch at run time (section 8). The microarchitecture levels are `x86-64-v2` (SSE4.2, POPCNT), `v3` (AVX2, FMA, BMI1, BMI2, LZCNT) and `v4` (AVX-512 F, BW, CD, DQ, VL).
- **`vzeroupper`.** The VEX (AVX) encoding zeroes the upper part of a register when it writes the lower part; legacy SSE instructions do not. Mixing the two with dirty upper halves costs a false dependency or a slow state change, depending on the core. Compilers insert `vzeroupper` before calls and returns for you; hand-written assembly must do it itself.
- **AVX-512 is uneven.** It is the widest and most flexible set, but availability varies: many Intel client parts do not have it, and AMD's Zen 4 supports it on 256-bit datapaths (a 512-bit instruction takes two passes), as far as I know. AVX-512VL lets you use masks and the new instructions on 128-bit and 256-bit vectors, which is often the sweet spot. Intel's AVX10 effort aims to make a single, clearer level; check the current state before relying on it.

### 3.2 Arm: NEON and SVE

**NEON** (Advanced SIMD) is part of AArch64: 32 registers of 128 bits (`v0` to `v31`), lanes of 8, 16, 32 or 64 bits. It has fused multiply-add (`fmla`), saturating arithmetic (`uqadd`, `sqadd`), table lookup (`tbl`), dot products for 8-bit integers (`sdot`, `udot`) and, unlike x86, **interleaving loads**: `ld4` loads an array of four-field structs and splits it into four registers, one per field, in one instruction. That undoes the array-of-structs layout of section 5.4 in hardware.

**SVE** (Scalable Vector Extension) is length-agnostic. The vector length is a multiple of 128 bits from 128 to 2048, chosen by the *implementation*; code never assumes it. It adds 16 **predicate registers** (`p0` to `p15`, one bit per byte of the vector) that mask every operation, a `whilelt` instruction that builds the predicate for a loop tail, first-fault loads for loops that stop early, and gather and scatter. SVE2 adds the integer and crypto instructions that NEON had. As far as I know, real cores differ widely: some implement SVE at only 128 bits (several Neoverse cores), one server core at 256 bits and the Fujitsu A64FX at 512. Do not assume.

### 3.3 RISC-V: the V extension

32 vector registers `v0` to `v31`, each `VLEN` bits where `VLEN` is chosen by the implementation (128 bits and up in practice). Three settings, written by one instruction `vsetvli`, describe a loop:

- `SEW`: the element width (8, 16, 32 or 64 bits);
- `LMUL`: how many registers are grouped into one long vector (1, 2, 4, 8, or fractions down to 1/8), trading register count for length;
- `vl`: how many elements this strip handles, returned by `vsetvli` and never larger than `VLEN × LMUL / SEW`.

Register `v0` doubles as the mask. Because `vl` is a register, there is no tail code, exactly as in the strip-mining loop of section 2.2.

### 3.4 Side by side

| | AVX2 | AVX-512 | NEON | SVE | RISC-V V |
|---|---|---|---|---|---|
| Vector width | 256 | 512 | 128 | 128 to 2048, set by the hardware | `VLEN` × `LMUL`, set by the hardware |
| Registers | 16 | 32 | 32 | 32 vector + 16 predicate | 32 (grouped by `LMUL`) |
| Conditional lanes | compare gives a lane mask, then blend or and | masks `k0` to `k7` on any instruction | compare gives a lane mask, then bitwise select | predicate `p0` to `p15` on any instruction | mask `v0` |
| Loop tail | scalar loop or a masked move | masked load and store | scalar loop | `whilelt` predicate | `vl` |
| Gather and scatter | gather only | both | neither | both | indexed loads and stores |
| Same binary on wider hardware | no | no | no | yes | yes |

---

## 4. Writing SIMD code

### 4.1 Four levels

| Level | How | Control | Portability |
|---|---|---|---|
| 1. Let the compiler | `-O3`, a target flag, `#pragma omp simd` (auto-vectorisation, section 7) | least; you check the assembly | best |
| 2. Portable vector types | GCC and clang `vector_size` types, libraries such as Highway and xsimd, `std::experimental::simd` in libstdc++ (`std::simd` is in the C++26 draft) | medium: you choose the lanes, the library picks the instruction | good across ISAs |
| 3. Intrinsics | C functions that map to one instruction each: `<immintrin.h>`, `<arm_neon.h>`, `<arm_sve.h>`, `<riscv_vector.h>` | full: you choose instructions and registers (the compiler allocates) | one ISA family per code path |
| 4. Assembly | inline or separate | total | none |

The usual route is 1, then read the assembly, then 3 for the one hot loop the compiler missed. Level 4 is for kernels in libraries such as `memcpy`, and for crypto.

### 4.2 Reading an intrinsic

An x86 intrinsic name has three parts: `_mm256_fmadd_ps` is *256-bit*, *fused multiply-add*, *packed single* (floats). The prefix is `_mm` (128), `_mm256` or `_mm512`; the suffix names the element type.

| Suffix | Meaning | Type |
|---|---|---|
| `_ps`, `_pd` | packed float, packed double | `__m128`, `__m256`, `__m512` / `...d` |
| `_ss`, `_sd` | scalar float or double in the low lane | |
| `_epi8`, `_epi16`, `_epi32`, `_epi64` | packed signed or sign-agnostic integers | `__m128i`, `__m256i`, `__m512i` |
| `_epu8`, `_epu16` | packed unsigned (used where it matters: saturation, compare, convert) | |
| `_si256` | the whole register as bits (loads, stores, bitwise) | |

Arm names read `vfmaq_f32`: `v` for vector, `fma`, `q` for the 128-bit form, `f32` for the lane type. SVE names (`svadd_f32_x`, `svwhilelt_b32`) carry the element type and the predicate behaviour; RISC-V names (`__riscv_vfmacc_vf_f32m1`) carry the type and the `LMUL`.

Each intrinsic must be compiled with its instruction set enabled, either by a flag (`-mavx2`) or per function with `__attribute__((target("avx2,fma")))`, which is how the examples below keep a single file buildable on any x86-64 machine. A function that uses AVX2 and runs on a CPU without it dies with an illegal-instruction error, which is why section 8 exists.

### 4.3 The same loop in five instruction sets

The task is `y[i] += a * x[i]` ("saxpy": single-precision a times x plus y). Every version below was compiled and run against a scalar reference for every length from 0 to 70.

**AVX2 with FMA**, scalar tail:

```c
__attribute__((target("avx2,fma")))
void saxpy_avx2(float *y, const float *x, float a, long n) {
    __m256 va = _mm256_set1_ps(a);                   // broadcast a into all 8 lanes
    long i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256 vx = _mm256_loadu_ps(x + i), vy = _mm256_loadu_ps(y + i);
        _mm256_storeu_ps(y + i, _mm256_fmadd_ps(va, vx, vy));    // y = a*x + y, 8 lanes
    }
    for (; i < n; i++) y[i] += a * x[i];             // scalar tail: 0 to 7 elements
}
```

**AVX-512**, masked tail (no scalar loop at all):

```c
__attribute__((target("avx512f")))
void saxpy_avx512(float *y, const float *x, float a, long n) {
    __m512 va = _mm512_set1_ps(a);
    long i = 0;
    for (; i + 16 <= n; i += 16)
        _mm512_storeu_ps(y + i, _mm512_fmadd_ps(va, _mm512_loadu_ps(x + i), _mm512_loadu_ps(y + i)));
    if (i < n) {                                         // the tail: 1 to 15 elements
        __mmask16 m = (__mmask16)((1u << (n - i)) - 1);  // the low (n - i) bits set
        __m512 vx = _mm512_maskz_loadu_ps(m, x + i), vy = _mm512_maskz_loadu_ps(m, y + i);
        _mm512_mask_storeu_ps(y + i, m, _mm512_fmadd_ps(va, vx, vy));
    }
}
```

A masked load does not touch memory for the lanes that are off, and does not fault on them, so the tail may end exactly at the end of the array.

**Arm NEON** (4 lanes, scalar tail):

```c
void saxpy_neon(float *y, const float *x, float a, long n) {
    long i = 0;
    for (; i + 4 <= n; i += 4)
        vst1q_f32(y + i, vfmaq_n_f32(vld1q_f32(y + i), vld1q_f32(x + i), a));   // y + x*a
    for (; i < n; i++) y[i] += a * x[i];
}
```

**Arm SVE** (any vector length; the predicate handles the tail):

```c
void saxpy_sve(float *y, const float *x, float a, long n) {
    for (long i = 0; i < n; i += svcntw()) {                  // svcntw(): floats per vector, known at run time
        svbool_t pg = svwhilelt_b32((uint64_t)i, (uint64_t)n);   // lane j on while i + j < n
        svfloat32_t vx = svld1(pg, x + i), vy = svld1(pg, y + i);
        svst1(pg, y + i, svmla_n_f32_x(pg, vy, vx, a));
    }
}
```

**RISC-V vector** (`vl` handles the tail):

```c
void saxpy_rvv(float *y, const float *x, float a, size_t n) {
    for (size_t vl; n > 0; n -= vl, x += vl, y += vl) {
        vl = __riscv_vsetvl_e32m1(n);                         // hardware picks vl <= n
        vfloat32m1_t vx = __riscv_vle32_v_f32m1(x, vl);
        vfloat32m1_t vy = __riscv_vle32_v_f32m1(y, vl);
        vy = __riscv_vfmacc_vf_f32m1(vy, a, vx, vl);          // vy += a * vx
        __riscv_vse32_v_f32m1(y, vy, vl);
    }
}
```

The RISC-V compiler output for that loop is eleven instructions, with no tail code at all:

```
        vsetvli  a3, a2, e32, m1, ta, ma     # a3 = vl = min(n, VLMAX), 32-bit elements
        vle32.v  v8, (a1)                    # load vl floats from x
        vle32.v  v9, (a0)                    # load vl floats from y
        vfmacc.vf v9, fa0, v8                # y += a * x
        vse32.v  v9, (a0)
        sub      a2, a2, a3                  # n -= vl
        slli     a3, a3, 2                   # bytes = vl * 4
        add      a1, a1, a3                  # advance both pointers
        add      a0, a0, a3
        bnez     a2, loop
```

The SVE and RISC-V binaries ran correctly under QEMU at 128, 256 and 512-bit vector lengths. That is the length-agnostic property of section 2.4 shown on a real binary.

**What a compiler does by itself.** For the plain C loop with `restrict` pointers, clang 18 at `-O3 -mavx2 -mfma` produces a main loop that is unrolled four times: four 256-bit loads of `x`, four `vfmadd213ps` instructions that each take their `y` operand straight from memory, four 256-bit stores, then 32 elements done per trip, plus a scalar loop for the leftover. On Arm, clang's NEON loop uses `ldp q` and `fmla` on pairs of registers (8 floats per trip). The compiler does the unrolling and tail for you; what it cannot do is guess that your data are aligned, long enough to be worth it, or free of overlap, which is what section 7 is about.

### 4.4 The toolbox

| Need | AVX2 intrinsic (128-bit and 512-bit forms differ only in prefix) | Note |
|---|---|---|
| Load and store | `_mm256_loadu_ps`, `_mm256_storeu_ps`; aligned forms `_mm256_load_ps`; non-temporal `_mm256_stream_ps` | unaligned forms are as fast on aligned data (section 5.1); streaming stores bypass the cache |
| Broadcast | `_mm256_set1_ps` | one value into every lane |
| Arithmetic | `_mm256_add_ps`, `mul`, `fmadd`, `min`, `max`, `sqrt` | `rcp_ps` and `rsqrt_ps` are fast 12-bit approximations; one Newton step makes them accurate |
| Compare | `_mm256_cmp_ps(a, b, _CMP_GT_OQ)`, `_mm256_cmpeq_epi8` | the result is a vector of all-ones or all-zero lanes |
| Select | `_mm256_blendv_ps`, or `and`, `andnot`, `or` | `blendv` takes the sign bit of each mask lane |
| Vector to scalar | `_mm256_movemask_ps`, `_mm256_movemask_epi8` | one bit per lane (or per byte): the bridge to `if`, `popcnt` and `tzcnt` |
| Shuffle | `_mm256_shuffle_epi8` (`pshufb`), `_mm256_permutevar8x32_epi32`, `unpacklo`, `alignr` | many work inside each 128-bit half only |
| Convert and pack | `_mm256_cvtepi32_ps`, `cvtps_epi32`, `cvtepu8_epi32`, `packus_epi16` | widening and narrowing change the lane count |
| Horizontal | `_mm256_hadd_ps` (three micro-ops), or extract and add | avoid inside loops |
| Gather | `_mm256_i32gather_ps` | not a fast load (section 5.3) |

The horizontal sum that every reduction needs, written as the shortest sequence (8 lanes to 4, to 2, to 1):

```c
__attribute__((target("avx"))) static inline float hsum256(__m256 v) {
    __m128 s = _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1));  // 8 -> 4 lanes
    s = _mm_add_ps(s, _mm_movehl_ps(s, s));                                          // 4 -> 2
    s = _mm_add_ss(s, _mm_shuffle_ps(s, s, 1));                                      // 2 -> 1
    return _mm_cvtss_f32(s);
}
```

Do this **once after the loop**, not on every vector. I measured the mistake: summing 8192 floats with a horizontal sum on every 8-float vector took 0.147 to 0.152 ns per element, seven times slower than adding vectors into an accumulator and reducing at the end (0.021 ns, section 6.2).

### 4.5 Tails, masks and reading past the end

An array of `n` elements is rarely a multiple of the vector length. Four ways to handle the remainder:

| Method | How | Cost and catch |
|---|---|---|
| Scalar epilogue | a plain loop for the last 0 to `L-1` elements | simple; with 8 lanes and `n = 15` you do 1 vector trip and 7 scalar ones, so almost half the work is scalar |
| Overlapping last vector | redo the last full vector so that it ends exactly at the end | only for operations that can be repeated: `c[i] = a[i] + b[i]` is fine, `y[i] += a*x[i]` is **not** (it would add twice to the overlap) |
| Mask, predicate or `vl` | AVX-512 `k` mask, SVE `whilelt`, RISC-V `vsetvli` | cleanest; needs the hardware support |
| Pad the buffer | allocate a multiple of the width, fill the padding with a neutral value (0 for sums) | fastest, and possible whenever you own the buffer |

**Reading past the end can crash.** A 32-byte load that starts inside the last 31 bytes of a buffer reads bytes you do not own; if they lie on the next page and it is unmapped, the program faults. Library routines such as `memchr` avoid it with an alignment trick: round the pointer *down* to a multiple of 32, load the aligned block, and ignore the lanes before the real start. An aligned 32-byte block never straddles a 4 KB page, so the load cannot fault if any byte of it is valid.

---

## 5. Memory: alignment, bandwidth, layout, gather

### 5.1 Alignment

- Legacy SSE instructions with an aligned form (`movaps`, `_mm_load_ps`) **fault** if the address is not a multiple of 16. The AVX forms with an `u` (`vmovups`, `_mm256_loadu_ps`) accept any address.
- On a modern core the unaligned form on an *aligned* address is as fast as the aligned form. There is no reason to use the aligned form except to catch a bug.
- The real penalty is a **cache-line split**: a load that crosses a 64-byte boundary needs two cache accesses. A page split (crossing 4 KB) costs more again.

I timed loads from a 16 KB array that stays in the L1 cache, with four independent accumulators:

| Load | Where it starts (offset in a 64-byte line) | Time per load | Cycles at 3.2 GHz |
|---|---|---|---|
| 32 bytes | 0 or 32 (never splits) | 0.178 to 0.200 ns | about 0.6 |
| 32 bytes | 1, 16 or 60 (half of the loads split) | 0.241 to 0.300 ns | 0.8 to 0.95 |
| 64 bytes | 0 (one full line) | 0.298 to 0.302 ns | about 0.96 |
| 64 bytes | 8 or 32 (every load splits) | 0.455 to 0.467 ns | about 1.5 |

A split costs roughly one extra load slot: the 32-byte case where half the loads split is roughly 30 to 60% slower, the 64-byte case where all split is 50% slower. A 64-byte load can only be unsplit if it starts on a line boundary, so **align arrays used with AVX-512 to 64 bytes** (a cache line), arrays used with AVX2 to 32 at least. Do it with `alignas(64)`, `aligned_alloc(64, bytes)`, `posix_memalign`, or in C++ `std::aligned_alloc`, and tell the compiler with `__builtin_assume_aligned(p, 64)` or `std::assume_aligned<64>(p)`. In a loop limited by DRAM the split does not show, because the line has to be fetched anyway.

### 5.2 SIMD multiplies arithmetic, not bandwidth

Each saxpy element needs 8 bytes read (`x`, `y`) and 4 written (`y`), for 2 floating-point operations: 6 bytes per flop. The arithmetic units can do 12.8 to 184 GFLOP/s (section 1.5); memory feeds far fewer. I timed the same saxpy as scalar code (vectorisation turned off), 128-bit SSE, AVX2 and AVX-512 intrinsics, for arrays of growing size (the size is per array; there are two):

| Each array | Where it lives | Scalar ns/elem | Speed-up of SSE | of AVX2 | of AVX-512 |
|---|---|---|---|---|---|
| 4 KB | L1 | 0.36 | 3.4 to 3.6 | 4.7 | 8.1 to 8.3 |
| 64 KB | L2 | 0.37 to 0.39 | 3.0 to 3.2 | 3.7 to 3.9 | 3.7 to 4.0 |
| 1 MB | edge of L2 | 0.45 | 1.6 to 2.6 | 1.7 to 3.1 | 1.5 to 2.6 |
| 16 MB | L3 | 0.50 to 0.51 | 1.5 to 1.7 | 1.6 to 1.7 | 1.6 to 1.7 |
| 256 MB | DRAM | 0.78 to 0.84 | 1.4 to 1.5 | 1.4 to 1.5 | 1.4 to 1.6 |

Three lessons:

1. **In the L1 cache the speed-up follows the width** (3.5×, 4.7×, 8×), a little under the lane counts (4, 8, 16) because the loop has loads and stores too.
2. **Once the data leave L1, all widths converge**: from 64 KB on, AVX-512 is no better than AVX2, because the L2 delivers about the same bytes per cycle to either. In DRAM every width, including scalar, is within a factor 1.5 of the others.
3. **In DRAM the best vector loop is only 1.4 to 1.6 times faster than the scalar one.** Rewriting a memory-bound loop in intrinsics buys little; fix the access pattern first (blocking, fusing two passes into one, tutorial 14).

This is the roofline view of tutorial 12 section 9: compare the loop's bytes per flop with the machine's bytes-per-flop budget before deciding whether SIMD can help.

### 5.3 Gather and scatter

A **gather** loads `base[idx[j]]` into lane `j`. It lets a vector loop read a lookup table or follow an index list. AVX2 and AVX-512 have gather instructions; AVX-512, SVE and RISC-V have scatter (stores through indices); NEON has neither. A gather is *not* a fast load: it is a number of separate element loads under one instruction. I measured summing 65,536 floats chosen by an index array, scalar (four independent chains), with AVX2 gather and with AVX-512 gather:

| Table, indices | Scalar ns/elem | AVX2 gather | AVX-512 gather |
|---|---|---|---|
| 4 KB (L1), random | 0.24 | 0.18 | 0.18 |
| 256 KB (L2), random | 0.41 to 0.42 | 0.36 to 0.37 | 0.40 to 0.41 |
| 256 MB (DRAM), random | 9.6 to 9.7 | 9.7 to 10.1 | 10.5 to 11.0 |
| 256 KB, indices 0, 1, 2, ... | 0.27 to 0.29 | 0.23 to 0.24 | 0.24 to 0.26 |
| the same data as a plain AVX2 load | | 0.039 to 0.041 | |

Reading the table:

- Random indices from L1 gain only 1.3 times, from L2 about 1.1 times, from DRAM **nothing**: the time is the cache miss (with TLB misses on top, tutorial 15), and the gather issues the same misses as the scalar loop. Gather does not make random access faster.
- When the indices happen to be consecutive, the gather is **five to six times slower than the plain vector load** it replaces. Never gather what you can load.
- On some older Intel parts the gather was made slower still by a microcode update that closed a data-leak vulnerability (Gather Data Sampling, "Downfall"). Compilers are cautious as a result: with generic tuning at `-O3 -mavx2`, GCC 13 and clang 18 vectorise `y[i] = t[idx[i]]` (with `restrict` on the pointers) using ordinary element loads and **no `vgather` instruction at all**; with `-march=sapphirerapids` they do emit it. A scatter loop `t[idx[i]] += v[i]` is not vectorised by either: two indices may be equal, so the order of the updates matters (AVX-512 has a conflict-detection instruction, `vpconflictd`, for exactly this).

If a loop gathers, the cure is almost always a layout change (next section), a sort of the indices for locality, or a tiled table that fits in cache.

### 5.4 Array of structs against struct of arrays

```c
struct P { float x, y, z, w; };   struct P p[N];     // array of structs (AoS)
float x[N], y[N], z[N], w[N];                         // struct of arrays (SoA)

for (i = 0; i < N; i++) p[i].x += dx;                 // AoS: stride 16 bytes
for (i = 0; i < N; i++) x[i]   += dx;                 // SoA: contiguous
```

In the AoS loop the `x` values are 16 bytes apart, so a 256-bit load holds two structs and only two useful values. Both GCC 13 and clang 18 do vectorise this loop at `-O3 -mavx2`, but look at what they emit for each 8 elements: about ten permutes and blends to collect the eight `x` values, one vector add, and then eight separate single-lane stores (`vextractps`). That is over twenty instructions for eight elements, scarcely better than the scalar loop. In the SoA loop the same eight elements take one load, one add and one store. This is the layout rule of tutorial 14, now with a second reason. On Arm, NEON's `ld4` de-interleaves an AoS array in hardware and removes most of the penalty; on x86 you build the same effect from shuffles. When SIMD is the goal, store the data the way the loop reads it: contiguous per field.

When a loop needs several fields of the same record but you still want contiguous vectors, use the hybrid from tutorial 14, **AoSoA** (array of structs of arrays): store the records in blocks of 8 (or 16) and, inside a block, field by field.

```c
struct Block { float x[8], y[8], z[8], w[8]; };      // 8 records; 128 bytes = two cache lines
struct Block b[N / 8];                               // record i lives at b[i / 8].x[i % 8]

for (int j = 0; j < N / 8; j++)                      // one 256-bit load per field per block
    for (int k = 0; k < 8; k++) b[j].x[k] += dx;     // vectorises as one load, add, store
```

The fields of one record stay within 128 bytes of each other, so a loop that uses all four fields touches two cache lines per 8 records, and a loop that uses only `x` still gets contiguous 32-byte vectors. The price is index arithmetic (`i / 8`, `i % 8`) in code that picks out single records.

---

## 6. Arithmetic: floating point, denormals and saturation

### 6.1 Floating-point addition is not associative

In floating point, `(a + b) + c` is not always `a + (b + c)`, because each addition rounds. A tiny example in 32-bit floats: `(16777216 + 1) + 1 = 16777216`, since 16,777,217 does not exist as a float and the sum rounds back down, but `16777216 + (1 + 1) = 16777218`. A vectorised sum adds the elements in a **different order** (lane 0 gets elements 0, 8, 16, ...), so it can return a different answer. I summed one hundred million copies of 1.0f, whose exact sum is 100,000,000:

| How it was summed | Result |
|---|---|
| in a `double` | 100,000,000 |
| `float`, one accumulator, in order | **16,777,216** (stuck: adding 1 to 2²⁴ rounds back to 2²⁴) |
| `float`, four accumulators | 67,108,864 (four stuck partial sums of 2²⁴) |
| AVX2, 4 vectors of 8 lanes = 32 partial sums | 100,000,000 |
| AVX-512, 8 vectors of 16 lanes = 128 partial sums | 100,000,000 |

The in-order loop is the worst: its running total grows large and every small term is rounded away. The wider vector sums are exact here because each partial sum stays small (3 million per lane). This is the same reason pairwise summation is more accurate than naive summation.

On random numbers in [0, 1) (4,194,304 of them) the exact sum, computed in `double`, is 2,097,190.02, and the `float` versions gave:

| Order | Sum | Relative error |
|---|---|---|
| one scalar accumulator | 2,097,248.50 | +2.8 × 10⁻⁵ |
| four scalar accumulators | 2,097,184.75 | −2.5 × 10⁻⁶ |
| AVX2, 8 lanes, one accumulator | 2,097,195.00 | +2.4 × 10⁻⁶ |
| AVX2, 4 accumulators (32 partial sums) | 2,097,190.00 | −1.1 × 10⁻⁸ |
| AVX2, 8 accumulators (64 partial sums) | 2,097,189.75 | −1.3 × 10⁻⁷ |
| AVX-512, 8 accumulators (128 partial sums) | 2,097,190.75 | +3.5 × 10⁻⁷ |

Two consequences: **more partial sums usually means a more accurate result** (shorter chains of rounding), and **the answer depends on the vector width**. The same source compiled for AVX2 and for AVX-512, or run on a machine with a different SVE length, gives different low-order bits. For reproducible results across machines, fix the number of partial sums in the algorithm (always 32 lanes, say) and do not let the hardware width decide.

Integer addition does not have this problem: it wraps around and is associative, so the compiler vectorises an integer sum without being asked (tutorial 12 section 9.3).

### 6.2 Reductions are limited by latency, not by width

A sum has a chain: each add needs the previous result. Tutorial 11 and 12 showed that the cure is several independent accumulators, and that the number needed is *latency × throughput*. SIMD changes the arithmetic but not the rule. On this core a dependent floating-point add takes 2 cycles (I measured 0.625 to 0.64 ns per dependent `vaddps`, at 3.2 GHz) and there are two FP add ports, so a vector sum needs `2 × 2 = 4` accumulators to keep both ports busy. Summing 8192 floats (32 KB, in L1), ns per element:

| Method | ns per element | Speed-up over the compiler's scalar loop |
|---|---|---|
| plain scalar loop, `gcc -O3` (one chain, in order) | 0.59 to 0.62 | 1 (the base) |
| scalar, four accumulators written by hand | 0.155 to 0.157 | 4 |
| AVX2, horizontal sum on every vector (the mistake) | 0.147 to 0.152 | 4 |
| `gcc -O3 -ffast-math` (it vectorises, one accumulator) | 0.070 to 0.077 | about 8 |
| AVX2 intrinsics, **one** vector accumulator | 0.071 to 0.073 | about 8 |
| `clang -O3 -ffast-math` (vectorises, four accumulators) | 0.028 | about 21 |
| AVX2 intrinsics, **four** accumulators | 0.0215 to 0.0221 | about 28 |
| AVX2 intrinsics, eight accumulators | 0.0211 to 0.0215 | about 28 |
| AVX-512 intrinsics, eight accumulators | 0.0155 to 0.0156 | about 38 |

The pattern is the one from tutorial 12 section 4.5, with a wider vector. One vector accumulator is stuck at the add latency: 8 floats every 2 cycles = 0.078 ns per element, measured 0.071 to 0.073. Four accumulators reach 0.0215 ns per element, 0.55 cycles per 8-float vector, close to the 0.5 of two ports; eight add nothing, because the ports are now the limit. AVX-512 gets 16 floats per instruction and runs out of L1 load bandwidth (about one 64-byte load per cycle, from the table in section 5.1) at 0.8 cycle per vector.

Notice what the compilers did. GCC, given permission, produced **one** vector accumulator and so reached the latency limit, about 8 times over scalar; clang produced four and got about 21 times. The last factor of 3 was available only to someone who looked at the assembly and wrote the accumulators by hand.

### 6.3 What the compiler needs to vectorise a float sum

Without permission, the compiler may not reorder a floating-point sum, so it keeps one in-order chain. GCC 13 even reports *"loop vectorized using 32 byte vectors"* for such a loop, but the code it emits is eight scalar `vaddss` instructions in a row (an in-order reduction) and runs at the scalar speed (0.59 to 0.62 ns per element). **A "vectorised" message is not a speed-up; read the assembly.**

Ways to give permission, from the narrowest:

| Option | What it permits | Measured on the sum (GCC / clang, ns per element) |
|---|---|---|
| `#pragma omp simd reduction(+:s)` with `-fopenmp-simd` | reordering of exactly this reduction | 0.073 / 0.029 |
| `-fassociative-math -fno-signed-zeros -fno-trapping-math` (GCC needs all three) | reassociation everywhere in the file | |
| `-ffast-math` | all of the above, plus assuming no NaN or infinity, using reciprocals, flushing denormals (section 6.4) and more | 0.07 to 0.077 / 0.028 |

`-ffast-math` is a bundle with side effects that reach beyond the loop: tests such as `isnan(x)` may be removed, results change in the last bits (and with the vector width, section 6.1), and GCC links a start-up object that turns on flush-to-zero for the whole process. Prefer the pragma or write the accumulators yourself.

### 6.4 Denormal numbers

A float closer to zero than about 1.2 × 10⁻³⁸ is stored as a **denormal** (subnormal): the exponent is the smallest possible and the leading digit is no longer an implicit 1, so tiny numbers lose bits gradually instead of snapping to zero. It is correct, and it is slow: most x86 and many other cores run a denormal operand or result through a microcode assist. I multiplied 4096 floats by 0.5 with AVX2:

| Data | ns per element |
|---|---|
| normal numbers (1e-30) | 0.053 to 0.054 |
| denormal numbers (1e-39, so inputs and results are denormal) | **4.9 to 5.0** (92 to 94 times slower) |
| the same denormal data with flush-to-zero and denormals-are-zero on | 0.051 to 0.054 |

That is about 130 cycles per 8-lane instruction. Denormals appear when a value decays toward zero: an audio or control filter at the end of a note, a recursive average, a physics step, a neural-network activation. The program becomes slow without any error.

The fix is to turn the feature off. On x86, set two bits in the `MXCSR` control register: flush-to-zero (FTZ, bit 15: denormal *results* become 0) and denormals-are-zero (DAZ, bit 6: denormal *inputs* are read as 0).

```c
#include <immintrin.h>
void enable_ftz_daz(void) { _mm_setcsr(_mm_getcsr() | 0x8040); }    // FTZ | DAZ
```

Arm has an equivalent flush-to-zero bit in the `FPCR` register. The costs: results that would have been tiny become exactly 0 (usually harmless, occasionally visible), the setting is **per thread** (set it in every thread that does the maths), and it also affects every library the thread calls. A second cure is to add a tiny constant (say 10⁻²⁰) to a value that decays, so it never reaches the denormal range.

### 6.5 Saturating arithmetic and integer promotion

Pixels, audio samples and quantised neural-network values are small integers that must not wrap: 250 + 10 should be 255, not 4. SIMD has **saturating** adds and subtracts for 8 and 16-bit lanes (x86 `paddusb`, `paddsb`; Arm `uqadd`, `sqadd`; RISC-V `vsaddu`, `vsadd`), and packing instructions that narrow with saturation (`packuswb`).

Brightening an image by adding 40 to every byte, with a clamp at 255, on 16 KB buffers:

| Version | GB/s |
|---|---|
| scalar loop `int v = p[i] + add; d[i] = v > 255 ? 255 : v;` | 2.3 to 2.4 |
| the same loop, `gcc -O3 -mavx2` | 6.6 to 6.8 |
| `_mm256_adds_epu8` (32 bytes per instruction) | 94 to 95 |
| `_mm256_add_epi8` (wrapping add; **wrong result**) | 94 to 96 |

Two points. First, the saturating instruction costs nothing extra over the wrapping one. The wrapping version is shown to prove it: its output differed from the correct one in 2,543 of 16,384 bytes (every byte that overflowed wrapped round to a small value, so a bright pixel turned dark). Second, GCC did vectorise the C loop, but C's **integer promotion** made it widen each byte to a 16 or 32-bit int, so it handled fewer lanes per instruction, and it ran 14 times slower than the intrinsic. The same effect hit the byte counter of section 9.1. When a loop works on bytes, the compiler is only as good as the types you wrote.

A related instruction for machine learning: 8-bit dot products (x86 VNNI `vpdpbusd`, Arm `sdot`/`udot`) multiply four pairs of bytes and add them into each 32-bit lane in one instruction, which is why int8 inference is so much faster than float32.

### 6.6 Fused multiply-add changes the last bit

A fused multiply-add (`fma`) rounds once for `a*b + c`; separate multiply and add round twice, so the results can differ in the last bit. GCC contracts `a*b + c` into an FMA by default in its GNU mode when the target has one (`-ffp-contract=fast`), and not in strict ISO mode, so the same source can give different bits with and without `-mfma`, or in a vector body and its scalar tail if they are compiled differently. Tests that compare floating-point results bit for bit break for this reason; compare with a tolerance.

---

## 7. Auto-vectorisation: what the compiler can and cannot do

### 7.1 What it has to prove

For the compiler to turn a scalar loop into a vector loop it must establish, for every loop:

1. **The iterations are independent**, or depend on each other only in ways it understands: a running reduction (sum, minimum), an induction variable (`i`). A value computed in one iteration and read in the next (`a[i] = a[i-1] + b[i]`) is a **loop-carried dependence** and blocks it.
2. **No aliasing**: a store through one pointer must not change something another pointer reads in a later iteration. If it cannot prove that, it may emit both versions and a run-time overlap check before the loop, or give up.
3. **The trip count is known before the loop starts.** A loop that can leave early (`break`, `return`, `while (s[i])`) cannot be cut into blocks of eight, because the ninth to sixteenth iterations may not be allowed to run.
4. **Every operation has a vector form.** An opaque function call does not; neither does a library routine that sets `errno`.
5. **The access pattern is acceptable**: contiguous is best, constant stride and gathers cost extra.
6. **The cost model says it pays**: setup, run-time checks, the tail and (for small `n`) the whole thing are weighed against the gain.
7. **The target allows the instructions.** With no flag, an x86-64 compiler may use SSE2 only.

### 7.2 Twelve loops, two compilers

I compiled the loops below with GCC 13.3 and clang 18.1 at `-O3 -mavx2 -mfma` and read their vectoriser reports (`-fopt-info-vec-optimized -fopt-info-vec-missed` for GCC; `-Rpass`, `-Rpass-missed` and `-Rpass-analysis=loop-vectorize` for clang).

| # | Loop | GCC 13 | clang 18 | Reason, from the reports |
|---|---|---|---|---|
| 1 | `y[i] += a * x[i]` (no `restrict`) | yes | yes | both add a run-time overlap check |
| 2 | `a[i] = a[i-1] + b[i]` (prefix sum) | no | no | loop-carried dependence |
| 3 | `s += x[i]` (float sum) | "yes", but in order: no gain | no | "cannot prove it is safe to reorder floating-point operations" |
| 4 | `if (x[i] == key) return i;` (search) | no | no | early exit: "control flow in loop", "could not determine number of loop iterations" |
| 5 | `y[i] = opaque(x[i])` | no | no | "statement clobbers memory", "call instruction cannot be vectorized" |
| 6 | `y[i] = t[idx[i]]` (gather), no `restrict` | no | no | cannot rule out overlap between `y` and `t`; with `restrict` both vectorise, **without** a `vgather` (section 5.3) |
| 7 | `t[idx[i]] += v[i]` (scatter, histogram) | no | no | two indices may be equal, so the updates cannot be reordered |
| 8 | `y[i] = x[2*i]` (stride 2) | yes | yes | needs shuffles |
| 9 | `p[i].x += dx` (array of structs) | yes | yes | over twenty instructions per 8 elements (section 5.4) |
| 10 | `if (x[i] > t) y[i] = x[i];` | yes | yes | becomes a compare and a masked store (`vmaskmovps` in both) |
| 11 | `y[i] = sqrtf(x[i])` | no | no | `sqrtf` may set `errno`; with `-fno-math-errno` both vectorise it |
| 12 | `while (s[i]) i++;` (string length) | no | no | trip count unknown, early exit |

At **`-O2`** the picture differs by compiler. GCC 13 vectorised **none** of these twelve loops, not even loop 1 or the `restrict` saxpy, because its `-O2` uses a "very cheap" cost model that refuses any loop needing a run-time check or a tail. Clang vectorises at `-O2` as at `-O3`. When a GCC build is slow, the first thing to try is `-O3`.

The pattern behind the table: **simple, contiguous, branch-free loops over arrays with no early exit** vectorise. Anything that searches, chains iterations, calls something opaque or goes through an index does not, and it is rarely the compiler's fault: the transformation would change the program's meaning.

### 7.3 Helping the vectoriser

| Obstacle | What you can do |
|---|---|
| Possible overlap | `restrict` on the pointers (a promise that they do not overlap, and wrong if you lie); `#pragma GCC ivdep` or `#pragma clang loop vectorize(assume_safety)` |
| Float reduction | `#pragma omp simd reduction(+:s)` with `-fopenmp-simd`, or write the accumulators yourself (section 6.2) |
| `errno` in `sqrtf`, `fabsf`-like calls | `-fno-math-errno` if you never read `errno` |
| An unrelated `break` or function call | move the test out of the loop (search block by block), inline the function, or `#pragma omp declare simd` |
| Index arithmetic | use a signed index (`int` or `long`): signed overflow is undefined, so the compiler can assume no wrap; an `unsigned` index that may wrap is harder to analyse |
| Default target | `-march=x86-64-v3`, `-mavx2 -mfma`, or per-function `target` attributes with dispatch (section 8) |
| Narrow data, wide accumulator | declare the accumulator as narrow as it can be (next paragraph) |

The last row has a spectacular example. Counting the newlines in a 16 KB buffer, GCC 13 at `-O3 -mavx2`, **the same loop with only the type of the counter changed**:

| Counter | GB/s |
|---|---|
| `long k` | 4.8 |
| `int k` | 11.4 |
| `unsigned char k`, flushed into a `long` every 240 bytes | 59.7 |

With a `long` counter the compiler compares 32 bytes in one instruction and then has to widen them to 64-bit lanes before adding, a long sequence of unpack instructions. With an 8-bit counter every byte stays a byte: one compare and one subtract per 32 bytes. The flush every 240 bytes is the price: an 8-bit counter overflows after 255 additions. Clang gave the same ordering (9.5, 19.2 and 32.5 GB/s) at a lower level.

### 7.4 Check what you got

Never assume. Ask the compiler, then look.

```
gcc   -O3 -mavx2 -fopt-info-vec-optimized -fopt-info-vec-missed -c file.c
clang -O3 -mavx2 -Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize -c file.c
```

Then read the hot loop in the assembly (`objdump -d`, `perf annotate`, or Compiler Explorer). Three things to look for:

```
vaddps  ymm0, ymm0, [rdi+rax*4]     ; packed: 8 floats per instruction      -> vectorised
vaddss  xmm0, xmm0, [rdi+rax*4]     ; scalar (the "s" in "ss"): 1 float     -> not
vfmadd231ps ymm1, ymm2, [rsi+rax*4] ; packed fused multiply-add
```

- The register names: `ymm` or `zmm` mean wide; `xmm` with a `ps` suffix is 128-bit SIMD; `xmm` with an `ss` suffix is scalar.
- The suffix: `ps`, `pd` and the integer forms (`epi8`, `epi32`, ...) are packed; `ss` and `sd` are scalar.
- The shape: a vectorised loop is followed by a remainder loop, and often preceded by a run-time overlap check (a few `cmp`/`ja` instructions that jump to a scalar copy).

And remember GCC's in-order reduction (section 6.3): it prints "vectorized" over a loop of eight serial `vaddss`.

### 7.5 What a default build gives up

A distribution builds its packages for the baseline of the architecture, SSE2 on x86-64. A library you install from a package manager has therefore *not* been compiled for AVX2, whatever your CPU has. A binary you build with `-march=native` uses everything your machine has and **crashes with an illegal-instruction error (SIGILL) on an older machine**. Neither is what you want for software that ships to many machines; the answer is the next section.

---

## 8. One binary, many CPUs: run-time dispatch

### 8.1 Asking the CPU

On x86, the `cpuid` instruction reports which instruction sets the processor implements. That is not enough: the operating system must also save and restore the wide registers on a context switch, and it enables that through the `XCR0` register (read with `xgetbv`). A correct check tests both. GCC and clang provide it:

```c
__builtin_cpu_init();
if (__builtin_cpu_supports("avx2")) { /* the AVX2 path */ }     // CPU has it and the OS saves ymm
```

On Arm, user code cannot read the ID registers directly. Linux passes feature bits in the auxiliary vector, read with `getauxval(AT_HWCAP)` and `AT_HWCAP2` (bits such as `HWCAP_ASIMD` for NEON, `HWCAP_SVE`, `HWCAP2_SVE2`). RISC-V Linux does the same with `AT_HWCAP` for the base extensions and a `riscv_hwprobe` system call for the rest.

### 8.2 Three ways to dispatch

**By hand**: choose a function pointer once, at start-up, and call through it.

```c
typedef float (*sum_fn)(const float *, long);
static sum_fn pick_sum(void) {
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx2")) return sum_avx2;     // the version with intrinsics
    return sum_generic;                                      // the portable fallback
}
static sum_fn sum_impl;                                      // set once, e.g. in main()
```

**Function multi-versioning**: the compiler builds one clone of a function per target and a resolver that picks among them.

```c
__attribute__((target_clones("default,avx2,avx512f")))
float dot(const float *a, const float *b, long n) {
    float s = 0;
    #pragma omp simd reduction(+:s)                 // compile with -fopenmp-simd
    for (long i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
```

Each clone is compiled for its target, so the plain loop gets AVX2 or AVX-512 instructions in the right clone, and the program contains a baseline version too.

**ifunc**: an ELF feature where the dynamic linker calls a *resolver function* once, when the symbol is first bound, and uses the pointer it returns. This is how glibc selects the version of `memcpy`, `strlen` and `memchr` for your CPU (an SSE2, AVX2 or AVX-512-based implementation).

```c
static float twice_generic(float x) { return 2 * x; }
__attribute__((target("avx2"))) static float twice_avx2(float x) { return x + x; }
static float (*resolve_twice(void))(float) {
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx2") ? twice_avx2 : twice_generic;
}
float twice(float) __attribute__((ifunc("resolve_twice")));    // callers just call twice()
```

All three snippets were compiled and run. Some practical rules:

- **Dispatch at a coarse grain**: once per big function call, never once per element. The call through a pointer prevents inlining.
- **A function compiled for AVX2 cannot be inlined into one without it.** The compiler stops with "inlining failed in call to always_inline `_mm256_...`: target specific option mismatch". I hit exactly this while writing the benchmarks for this tutorial: a tiny helper used AVX intrinsics and had no `target` attribute. Give helpers the right attribute (as `hsum256` has in section 4.4).
- **Test every path.** The fallback is the one that rots, because your own machine never runs it. In CI, force each choice.
- **Dispatch on size too.** A vector kernel with fixed set-up costs is slower than a scalar loop for a handful of elements (section 9.5).

### 8.3 AVX-512: more than a wider register

AVX-512 brings masking on every instruction, more registers and new instructions, so it is useful even at 128 or 256 bits (AVX-512VL). It also brings costs that are easy to miss:

- **Clock speed.** On some Intel server generations, heavy 512-bit (and heavy 256-bit floating-point) instructions made the core drop to a lower frequency level, and moving between levels took tens of microseconds. A program that used a little AVX-512 inside otherwise scalar code could slow *everything* on that core. Newer cores reduced the effect a great deal.
- **What I measured.** The fused multiply-add took 0.174 to 0.199 ns per 512-bit instruction against 0.157 to 0.189 ns per 256-bit instruction, about 10% slower per instruction in the quiet runs (section 1.5), while doing twice the work. The VM has no performance counters, so I could not tell whether it was the clock or the pipes.
- **Context size.** The saved register state grows to several kilobytes, which is one reason signal-stack sizes had to be raised on Linux (the real minimum is exposed as `AT_MINSIGSTKSZ`).
- **Availability.** Not every x86 CPU has it, and some implement it at half width (section 3.1).

The guidance: use AVX-512 for long, streaming, vectorisable loops, measure end to end on the target hardware and not just the kernel, and for short bursts inside scalar code prefer 256-bit with the AVX-512VL features.

---

## 9. SIMD in practice

### 9.1 Searching and counting bytes

The core trick of `memchr`, `strlen` and "count the newlines" is **compare, then movemask**: compare 32 bytes with the target byte (each equal lane becomes 0xFF), turn the lane flags into one 32-bit integer (`movemask`), then use scalar bit instructions on that integer: `popcnt` to count the matches, `tzcnt` to find the first.

```c
__attribute__((target("avx2,popcnt")))
long count_byte_avx2(const unsigned char *p, long n, unsigned char c) {   // n a multiple of 32
    __m256i vc = _mm256_set1_epi8((char)c);
    long k = 0;
    for (long i = 0; i < n; i += 32) {
        unsigned m = (unsigned)_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(p + i)), vc));
        k += __builtin_popcount(m);                 // one bit per matching byte
    }
    return k;
}

__attribute__((target("avx2,bmi")))
long find_byte_avx2(const unsigned char *p, long n, unsigned char c) {    // index of the first c, or -1
    __m256i vc = _mm256_set1_epi8((char)c);
    long i = 0;
    for (; i + 32 <= n; i += 32) {
        unsigned m = (unsigned)_mm256_movemask_epi8(
            _mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)(p + i)), vc));
        if (m) return i + __builtin_ctz(m);         // position of the lowest set bit
    }
    for (; i < n; i++) if (p[i] == c) return i;
    return -1;
}
```

Five instructions (load, compare, movemask, popcount, add) cover 32 bytes, against five or six instructions per byte in the scalar loop. Counting the `'\n'` bytes in buffers of 16 KB, 1 MB and 64 MB, GB/s:

| Version | 16 KB (L1) | 1 MB (L2) | 64 MB (memory) |
|---|---|---|---|
| scalar loop | 3.1 | 3.1 | 1.5 |
| `gcc -O3`, `long` counter | 4.8 to 4.9 | 4.8 to 4.9 | 1.9 to 2.1 |
| SSE (16 bytes at a time) | 33.8 to 34.1 | 32.9 to 34.1 | 9.0 to 14.1 |
| AVX2 (32 bytes) | 66.4 to 67.3 | 65.8 to 66.5 | 17.2 to 24.6 |
| AVX-512 (64 bytes) | 144.0 to 146.2 | 104.3 to 108.1 | 22.7 to 26.5 |
| glibc `memchr`, byte absent (full scan, for reference) | 132.8 to 134.3 | 93.8 to 95.8 | 23.4 to 26.1 |

In the cache, AVX2 is 21 times faster than the scalar loop and AVX-512 is 46 times (first column), and a hand-written AVX-512 loop is as fast as the library routine. In memory (last column) every vector width collapses onto the same 23 to 26 GB/s, the bandwidth one core can pull from DRAM, and only the scalar code is below it (section 5.2 again).

### 9.2 Filtering without branches

Sum the elements of a float array that exceed 0.5, 4096 random values in L1. The AVX2 and AVX-512 versions use the compare-and-mask pattern of section 1.3 (the AVX-512 one uses a `k` mask on the add):

| Version | Random data | Same data sorted (branch predictable) |
|---|---|---|
| scalar loop with a branch | 0.83 to 0.87 ns/element | 0.55 |
| AVX2 compare and `and` | 0.047 to 0.048 | 0.046 to 0.047 |
| AVX-512 masked add | 0.033 to 0.035 | 0.033 to 0.034 |

On random data the scalar branch is wrong about half the time (tutorial 10), so the vector version is **18 times** faster with AVX2 and **25 times** with AVX-512; even when the branch is predictable it is 12 times faster. The vector versions do not care what the data look like. That insensitivity is the reason database engines and analytics libraries scan columns with SIMD predicates.

### 9.3 Parsing: classify 32 bytes at once

Text parsers (JSON, CSV, HTTP headers, log lines) spend most of their time finding structure bytes: delimiters, quotes, newlines. The SIMD technique, used by high-speed parsers such as simdjson, has two steps.

**Step 1: classify every byte with two table lookups.** A 128-bit shuffle (`pshufb`) is a table lookup inside a register: each byte of the index vector picks one of 16 bytes of a table. Split each input byte into its low and high nibble (4 bits each) and look both up; a byte is a delimiter if the two table values share a bit. To find `','` (0x2C), `' '` (0x20) and `'\n'` (0x0A), give each class a bit (comma 1, space 2, newline 4):

| Byte | Low nibble → `lo_tab` | High nibble → `hi_tab` | AND | Delimiter? |
|---|---|---|---|---|
| `,` 0x2C | C → 1 | 2 → 3 | 1 | yes |
| space 0x20 | 0 → 2 | 2 → 3 | 2 | yes |
| `\n` 0x0A | A → 4 | 0 → 4 | 4 | yes |
| `a` 0x61 | 1 → 0 | 6 → 0 | 0 | no |
| `*` 0x2A | A → 4 | 2 → 3 | 0 | no |

```c
__attribute__((target("avx2")))
unsigned delimiter_mask32(const unsigned char *p) {       // bit i of the result: byte i is a delimiter
    const __m256i lo_tab = _mm256_setr_epi8(2,0,0,0,0,0,0,0,0,0,4,0,1,0,0,0,  2,0,0,0,0,0,0,0,0,0,4,0,1,0,0,0);
    const __m256i hi_tab = _mm256_setr_epi8(4,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,  4,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0);
    __m256i v   = _mm256_loadu_si256((const __m256i *)p);
    __m256i lo  = _mm256_and_si256(v, _mm256_set1_epi8(0x0F));                        // low nibbles
    __m256i hi  = _mm256_and_si256(_mm256_srli_epi16(v, 4), _mm256_set1_epi8(0x0F)); // high nibbles
    __m256i cls = _mm256_and_si256(_mm256_shuffle_epi8(lo_tab, lo), _mm256_shuffle_epi8(hi_tab, hi));
    __m256i is_zero = _mm256_cmpeq_epi8(cls, _mm256_setzero_si256());
    return ~(unsigned)_mm256_movemask_epi8(is_zero);
}
```

(`pshufb` looks up within each 128-bit half, so the 16-entry table is written twice for the 256-bit version.) The function was tested against a scalar loop on a 32-byte sample.

**Step 2: walk the set bits with scalar code.** You now hold a 32 or 64-bit mask with one bit per structure byte. Visit only those:

```c
while (m) {
    int pos = __builtin_ctz(m);       // index of the lowest set bit
    handle(base + pos);               // one delimiter
    m &= m - 1;                       // clear that bit
}
```

The scalar work is proportional to the number of delimiters, not the number of bytes. Text that is mostly payload runs at several GB/s. The same shape appears in **UTF-8 validation** (a fast path checks 32 bytes for any byte with the high bit set, with one `movemask`; the full check classifies byte pairs with three nibble lookups) and in **packet parsing**, where one `pcmpeqb` plus `movemask` compares a 16-byte address or key in a single step.

### 9.4 Where SIMD already is in your system

| Where | What uses it |
|---|---|
| C library | `memcpy`, `memset`, `memcmp`, `strlen`, `memchr`, chosen by ifunc for your CPU |
| Cryptography and checksums | AES-NI, carry-less multiply for CRC and GCM, SHA extensions, ChaCha20 in AVX2 |
| Compression and codecs | match finding in LZ4 and Zstandard, image and video codecs |
| Databases and analytics | column scans with predicate masks, hash probes, sorting libraries |
| Numerical libraries | BLAS and FFT kernels, hand-written for each ISA |
| Machine learning inference | int8 dot products (VNNI, `sdot`), BF16 and FP16 instructions |
| Networking | packet-processing frameworks batch descriptors and header fields with SIMD |

When you write a kernel of this kind, the library already has a tuned version; your job in an interview is to know what to look for in it.

### 9.5 When SIMD does not help

| Situation | Why | What to do instead |
|---|---|---|
| Few elements | fixed cost of set-up, tail and horizontal sum | scalar path below a threshold |
| Memory-bound loop (section 5.2) | the units wait for data | fix the access pattern: block, fuse passes |
| Pointer chasing: lists, trees, hash chains | the next address depends on the last load | restructure into arrays; batch independent lookups |
| Each element takes a different long path | divergence: you run all paths (section 1.3) | sort or partition the data first |
| Random access (gather) | the miss time dominates (section 5.3) | improve locality |
| A library already does it | BLAS, `memchr` | call it |

The cost of a few elements, measured. Summing `n` floats with a plain loop against an AVX2 loop with a scalar tail and one horizontal sum, ns per call (each call is independent of the previous one and they follow each other directly, so the out-of-order core overlaps them):

| n | Scalar | AVX2 |
|---|---|---|
| 1 | 0.85 to 1.04 | 1.70 to 2.15 |
| 4 | 1.23 to 1.28 | 2.01 to 2.16 |
| 7 | 1.68 to 2.10 | 2.68 to 3.17 |
| 8 | 1.71 to 1.90 | 1.90 to 1.92 |
| 15 | 2.83 to 2.96 | 3.12 to 4.18 |
| 16 | 6.64 to 7.05 | 1.95 to 2.03 |
| 32 | 11.4 to 12.1 | 2.18 to 2.35 |
| 64 | 20.4 to 27.4 | 2.66 to 2.89 |
| 256 | 110 to 113 | 7.9 to 8.4 |
| 1024 | 585 to 600 | 43.0 to 43.4 |

The vector loop costs about 2 ns whatever the length up to 32, so the break-even is **between 8 and 16 elements**. Two cautions: the vector time stays low at 1024 because the out-of-order window overlaps neighbouring calls, so a single isolated call (cold branch predictors, cold instruction cache) costs more; and the scalar time jumps from 15 to 16 elements, probably because the calls stop overlapping from that length on, which I did not investigate. The rule survives both: below a handful of elements, do not bother.

---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| A float sum or dot product is slow although the compiler says "vectorised" | an in-order reduction (a chain of scalar adds), or one vector accumulator at the add latency | several accumulators; `#pragma omp simd reduction`; `-fassociative-math` where allowed; read the assembly for `vaddss` |
| The program dies with "illegal instruction" on another machine | built with `-march=native`, or an AVX-512 path run on a CPU without it | baseline build plus run-time dispatch; check with `cpuid`-based tests, not by CPU name |
| A library or package is slower than expected on a modern CPU | built for the SSE2 baseline | your own build with `-march=x86-64-v3`, or a library with run-time dispatch |
| A loop is not vectorised at GCC `-O2` | GCC's `-O2` cost model refuses loops that need a tail or an overlap check | `-O3`, and read the vectoriser report |
| A byte loop runs at 5 GB/s instead of 50 | wide accumulator or integer promotion: the compiler works in 32 or 64-bit lanes | narrow accumulator flushed in blocks; intrinsics |
| A gather loop is no faster than the scalar one | random access is limited by cache misses, and a gather is many loads | layout change (struct of arrays), sort indices, tile the table |
| A SIMD loop is only 1.5 times faster than scalar | memory-bound: the data do not fit in cache | roofline check (bytes per flop); block or fuse passes; fewer bytes |
| Some inputs make the program 50 to 100 times slower | denormal numbers | flush-to-zero and denormals-are-zero; add a tiny offset to decaying values |
| Results differ between machines, or between SIMD and scalar | reassociation, fused multiply-add, different vector widths | tolerances; fix the number of partial sums in the algorithm |
| An AVX-512 version is slower than AVX2 in the full program | lower clock, memory-bound, or the 512-bit part is too short | measure end to end; use 256-bit with AVX-512VL features |
| A SIMD routine is slower for short inputs | fixed set-up, horizontal sum and tail | scalar path below a threshold of 8 to 16 elements |
| A crash or sanitizer report at the end of a buffer | a vector load read past the end | masked load, aligned-down trick, or padded buffer |
| Slow after mixing old SSE code with AVX code | upper-half state transition | `vzeroupper`; build the whole file with the same flags |

---

## 11. Mistakes that cost points

1. **Trusting "loop vectorized" in a report.** GCC prints it for an in-order reduction made of scalar adds. Check the assembly for packed instructions (`ps`, `ymm`), and the time.
2. **Benchmarking only on data that fit in L1.** The 8× of the small test shrinks to 1.4× at 256 MB (section 5.2). Test with the real size.
3. **Shipping `-march=native`.** It builds for the machine you compile on and crashes elsewhere.
4. **Using `-ffast-math` for the whole project to speed up one reduction.** It removes NaN checks, changes results and turns on flush-to-zero for the process. Use the pragma on the loop.
5. **A horizontal sum inside the loop.** It costs seven times more than reducing once at the end (0.147 against 0.021 ns per element).
6. **Assuming `malloc` returns 32 or 64-byte aligned memory.** On x86-64 glibc it is 16. Use `aligned_alloc(64, ...)`.
7. **Reading past the end in the tail** without checking the page boundary, or applying the "overlapping last vector" trick to an operation that is not idempotent (`y += a*x` would add twice).
8. **Using a gather for contiguous data**, or expecting a gather to speed up random access. It was 5 to 6 times slower than a plain load and no faster than scalar in DRAM.
9. **Mixing legacy SSE and AVX code** without `vzeroupper`, or leaving a helper function without its `target` attribute and getting an inlining error.
10. **Assuming AVX-512 is always faster.** It may lower the clock, be absent on a client CPU, or run at half width.
11. **Comparing floating-point results bit for bit** between a vector body, a scalar tail and a reference.
12. **Not testing the fallback path.** Your machine never runs it, so it rots.
13. **Hard-coding the SVE or RISC-V vector length.** Read it at run time; the point of those ISAs is that it varies.
14. **Writing a SIMD routine for 3 elements.** The fixed cost loses below 8 to 16 elements.

---

## 12. Questions you will be asked

**What is SIMD, and how does it differ from multithreading and from a GPU?** SIMD applies one instruction to several data elements held in a wide register, inside one thread of one core. It multiplies arithmetic throughput by the lane count without any synchronisation. Multithreading runs independent instruction streams on several cores and needs coherence and synchronisation; a GPU runs thousands of threads grouped into warps that execute in lock step (SIMT), and handles divergence by masking. The three combine: vectorise the inner loop, split the outer loop across cores, move very large data-parallel jobs to a GPU.

**What is a lane, and which operations are lane-wise?** A lane is one element position in a vector register. Arithmetic, FMA, compare, logic, min and max, shifts and conversions work independently per lane and are fast. Shuffles, permutes, broadcasts, packs, horizontal adds and `movemask` move data between lanes; they are slower and, on AVX2, many work only inside each 128-bit half. Good SIMD code stays lane-wise and crosses lanes once at the end.

**Why can the compiler not vectorise `for (...) s += x[i]` for floats, and what do you do about it?** Vectorising reorders the additions (lane 0 sums elements 0, 8, 16, ...), and floating-point addition is not associative, so the result can change. Without permission the compiler keeps the in-order chain, which runs at the add latency, 0.6 ns per element here. Permission comes from `-ffast-math`, `-fassociative-math`, or `#pragma omp simd reduction`. Even then the compiler often uses a single vector accumulator and stays at the latency bound; for the last factor of three, write four to eight accumulators yourself (latency times throughput of the add unit), and reduce horizontally once.

**A SIMD loop is 3 times faster, not 8 times. Where do you look?** In this order: is it memory-bound (bytes per flop against the machine's bandwidth; does the data fit in cache)? Is there a dependence chain (a reduction with one accumulator)? How large is the tail compared with the vector part? Is the data misaligned so that loads split lines? Is the compiler really emitting packed instructions? Is the clock lower for wide instructions? Measure each with a quick experiment before changing code.

**How do you ship one binary that uses AVX2 when the CPU has it?** Build the portable fallback for the baseline, build the AVX2 version with a `target("avx2,fma")` attribute (never a global `-mavx2`), and choose between them once. The choice tests `cpuid` and the `XCR0` register (the OS must save the wide registers), which `__builtin_cpu_supports` does. The mechanisms are a function pointer set at start-up, GCC and clang `target_clones` (the compiler generates the clones and the resolver), or an ELF ifunc resolved by the dynamic linker, which is how glibc picks `memcpy`. Dispatch once per call of a large function, not per element, and test every path.

**How would you implement `memchr` or "count the newlines" with SIMD?** Load 32 bytes, compare with the target byte (`pcmpeqb`), turn the result into an integer with `movemask`, then use `popcnt` to count or `tzcnt` to find the first match. Handle the tail with a scalar loop or a mask, and avoid faulting on reads past the end by aligning the pointer down to the vector size (an aligned block never crosses a page). In cache this runs 20 to 45 times faster than a byte loop; from DRAM it is limited by memory bandwidth.

**What are denormal numbers and why can they make a loop 100 times slower?** Floats closer to zero than the smallest normal number are stored in a gradual-underflow format. Most cores handle them with a microcode assist of around a hundred cycles (I measured 94 times slower on AVX2, about 130 cycles per instruction). They appear when values decay toward zero, such as filter tails. Setting flush-to-zero and denormals-are-zero in `MXCSR` (or Arm's `FPCR.FZ`) removes the cost at the price of treating tiny values as zero; the setting is per thread.

**Why is a gather not a fast load, and when is a scatter hard?** A gather issues one cache access per lane under a single instruction, so it costs roughly the same as the scalar loads and is several times slower than a contiguous vector load; with random indices the cache misses dominate and it is no faster than scalar code. A scatter is hard because two lanes may target the same address, so the order of updates matters (AVX-512 provides conflict detection). The usual cure is a layout change so that the loop reads contiguous data.

**What are the AVX-512 pitfalls?** Availability (absent on many Intel client CPUs, half width on some others), possible lower clock under heavy 512-bit work on some server generations, larger saved context, and a benefit that vanishes when the loop is memory-bound. It is excellent for long streaming loops and for its mask registers; for short bursts, use 256-bit with the AVX-512VL features and measure the whole program.

---

## 13. Problems

**How to use this section.** Try each problem before you read the answers. A and C are paper problems (30 to 40 minutes each); B is paper plus a compile; D is a program on your own machine. Sections 1.3, 2.2 and 4.5 give A; sections 7 and 6 give B; sections 1.5, 5.2 and 6.2 give C; section 9.1 and the table in 7.3 give D.

### Problem A (paper): vectorise by hand

You have an 8-lane float vector unit (AVX2). Count instructions per loop trip as: 2 loads, 1 FMA, 1 store, 1 index increment, 1 compare, 1 branch = 7, and a one-off broadcast of the constant.

(a) `y[i] = 3*x[i] + y[i]` for `n = 21`. Write the vector loop and its scalar tail, and count the vector trips, the scalar trips and the total instructions. Compare with the scalar loop. Repeat the comparison for `n = 24`. What does it say about the tail?

(b) `for each i: if (x[i] > 5) s += x[i];` Use compare, and, add with an accumulator vector. Run two vectors by hand: `x0 = [3, 9, 1, 7, 5, 8, 2, 6]` and `x1 = [10, 4, 5, 12, 0, 6, 5, 7]`. Give the mask of each vector, the accumulator lanes after both, and the final sum. Check against the scalar loop.

(c) `a[i] = a[i-1] + b[i]` (a running sum) cannot be vectorised lane by lane. Show how one 8-lane vector `b = [1, 2, 3, 4, 5, 6, 7, 8]` can be turned into its running sum with three shift-and-add steps, and say how the next vector is continued. How many vector operations per 8 elements, against 8 scalar adds?

(d) For `n = 21`, how many vector trips and how many scalar leftovers does the loop of (a) need on: AVX2 (8 lanes, scalar tail), AVX-512 (16 lanes, masked tail), NEON (4 lanes, scalar tail), SVE at 128, 256 and 512 bits?

*Where:* paper; sections 1.3, 2.2 and 4.5. *How:* write the lanes out in a row, and trace each vector instruction on them.

### Problem B (paper plus a compile): why does this not auto-vectorise?

Each loop below has a parameter list with `restrict` pointers, so aliasing is not the problem.

```c
// 1
void f1(float *restrict y, const float *restrict x, int n) {
    for (int i = 0; i < n; i++) { if (x[i] < 0) break; y[i] = sqrtf(x[i]); } }
// 2
void f2(float *restrict a, const float *restrict b, int n) {
    for (int i = 1; i < n; i++) a[i] = a[i-1] * 0.5f + b[i]; }
// 3
float f3(const float *restrict x, const float *restrict y, int n) {
    float s = 0; for (int i = 0; i < n; i++) s += x[i] * y[i]; return s; }
// 4
void f4(int *restrict hist, const unsigned char *restrict d, int n) {
    for (int i = 0; i < n; i++) hist[d[i]]++; }
// 5
void f5(float *restrict out, const float *restrict in, int n) {
    for (int i = 0; i < n; i++) out[i] = in[i] > 0 ? logf(in[i]) : 0; }
```

(a) For each loop, name the obstacle and say whether it is a property of the program or of the compiler's caution.
(b) Say what you would change to get a vectorised, correct loop.
(c) Compile with `gcc -O3 -mavx2 -mfma -fopt-info-vec-optimized -fopt-info-vec-missed -c` and `clang -O3 -mavx2 -mfma -Rpass=loop-vectorize -Rpass-analysis=loop-vectorize -c` and compare your answers with the reports.

*Where:* paper plus your machine; section 7 and its table. *How:* ask for each loop "which iteration must finish before the next one can start, or which result depends on the order?"

### Problem C (paper): how fast can it go?

A core runs at 3 GHz. It has two 512-bit FMA units (each starts one instruction per cycle; latency 4 cycles), and the L1 cache supplies two 64-byte loads and one 64-byte store per cycle. One core can pull 20 GB/s from DRAM. All data are 32-bit floats.

(a) What is the peak single-precision GFLOP/s?
(b) For the dot product `s += x[i] * y[i]` with the data in L1: how many loads does each FMA need, what is the attainable GFLOP/s, and how many vector accumulators are needed to reach it?
(c) The same dot product with the data in DRAM: bytes per flop and attainable GFLOP/s. How does a scalar loop (one FMA per cycle) compare?
(d) The same for saxpy `y[i] += a * x[i]` from DRAM.
(e) How many flops per byte of DRAM traffic must a kernel perform to be compute-bound? Compare with the dot product and saxpy.

*Where:* paper; sections 1.5, 5.2, 6.2 and tutorial 12 section 4.4. *How:* for each case, take the minimum of the limits: arithmetic, loads, latency chain, memory.

### Problem D (your machine): count bytes four ways

Save the program below as `countbytes.c`, build it with `gcc -O3 -mavx2 -mpopcnt -o countbytes countbytes.c` and run it.

```c
// countbytes.c - count newline bytes four ways.   Build: gcc -O3 -mavx2 -mpopcnt -o countbytes countbytes.c
#include <immintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef long (*count_fn)(const unsigned char *, long);
static volatile long sink;

__attribute__((noinline, optimize("no-tree-vectorize")))
long count_scalar(const unsigned char *p, long n) {              // 1: one byte at a time
    long k = 0;
    for (long i = 0; i < n; i++) k += (p[i] == '\n');
    return k;
}
__attribute__((noinline))
long count_auto_long(const unsigned char *p, long n) {           // 2: same loop, the compiler may vectorise it
    long k = 0;
    for (long i = 0; i < n; i++) k += (p[i] == '\n');
    return k;
}
__attribute__((noinline))
long count_auto_u8(const unsigned char *p, long n) {             // 3: 8-bit counters, flushed every 240 bytes
    long total = 0;
    for (long b = 0; b < n; b += 240) {
        long m = (n - b < 240) ? n - b : 240;
        unsigned char k = 0;                                     // cannot overflow: at most 240 per block
        for (long i = 0; i < m; i++) k += (p[b + i] == '\n');
        total += k;
    }
    return total;
}
__attribute__((noinline))
long count_avx2(const unsigned char *p, long n) {                // 4: compare, movemask, popcount (n multiple of 32)
    __m256i nl = _mm256_set1_epi8('\n');
    long k = 0;
    for (long i = 0; i < n; i += 32) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(p + i));
        k += __builtin_popcount((unsigned)_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, nl)));
    }
    return k;
}

static double seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(void) {
    const char *name[4] = {"scalar", "auto, long counter", "auto, u8 counter", "AVX2 intrinsics"};
    count_fn fn[4] = {count_scalar, count_auto_long, count_auto_u8, count_avx2};
    long sizes[3] = {1L << 14, 1L << 20, 1L << 26};              // 16 KB (L1), 1 MB (L2), 64 MB (DRAM)
    for (int s = 0; s < 3; s++) {
        long n = sizes[s];
        unsigned char *p = aligned_alloc(64, n);
        srand(5);
        for (long i = 0; i < n; i++) p[i] = (rand() % 100 < 4) ? '\n' : (unsigned char)('a' + rand() % 26);
        long want = count_scalar(p, n);
        printf("%ld KB, %ld newlines\n", n >> 10, want);
        for (int v = 0; v < 4; v++) {
            if (fn[v](p, n) != want) { printf("  %s: WRONG ANSWER\n", name[v]); continue; }
            long reps = 1;
            for (;;) {                                           // double reps until one trial takes at least 50 ms
                double t = seconds(); for (long r = 0; r < reps; r++) sink = fn[v](p, n); t = seconds() - t;
                if (t > 0.05) break;
                reps *= 2;
            }
            double best = 1e30;
            for (int trial = 0; trial < 5; trial++) {            // best of five
                double t = seconds(); for (long r = 0; r < reps; r++) sink = fn[v](p, n); t = seconds() - t;
                if (t < best) best = t;
            }
            printf("  %-20s %6.1f GB/s\n", name[v], (double)n * reps / best / 1e9);
        }
        free(p);
    }
    return 0;
}
```

(a) Predict first: which version wins at each size, and by how much? Then run it three times.
(b) Why do the four versions converge at 64 MB?
(c) Find the inner loop of `count_auto_long` and of `count_auto_u8` in the assembly (`objdump -d -M intel countbytes`, or `gcc -O3 -mavx2 -S`). What differs?
(d) Change `-O3` to `-O2` for GCC. What happens to versions 2 and 3, and why?
(e) Extend version 4 to count two different byte values at once (say `'\n'` and `','`) in the same pass. What does it cost compared with two passes?

*Where:* your machine; sections 7.3 and 9.1. *How:* the build line is above; the numbers should scale like the table in 9.1.

---

## Answers

### Problem A

**(a)** Scalar: 21 trips × 7 = **147** instructions. Vector, `n = 21`: 1 broadcast + 2 vector trips (16 elements) × 7 + 5 scalar tail trips × 7 = 1 + 14 + 35 = **50**, so a speed-up of 147 / 50 = **2.9**. For `n = 24`: scalar 168; vector 1 + 3 × 7 = **22**, so **7.6**. The tail is five elements out of 21, but it costs 35 of the 50 instructions: it runs seven instructions per element against under one per element in the body. Tails are why short arrays and lengths that are not a multiple of the width disappoint; a masked tail (one more masked vector trip, about 9 instructions) would bring `n = 21` to about 24 instructions, 6 times fewer than scalar.

**(b)** Mask of `x0`: lane is true when `x > 5`: `[0, 1, 0, 1, 0, 1, 0, 1]` (3 no, 9 yes, 1 no, 7 yes, 5 no because the test is strict, 8 yes, 2 no, 6 yes). `x0 & mask = [0, 9, 0, 7, 0, 8, 0, 6]`. Mask of `x1`: 10 yes, 4 no, 5 no, 12 yes, 0 no, 6 yes, 5 no, 7 yes, so `[1, 0, 0, 1, 0, 1, 0, 1]` and `x1 & mask = [10, 0, 0, 12, 0, 6, 0, 7]`. Accumulator after both: `[10, 9, 0, 19, 0, 14, 0, 13]`. Horizontal sum: 10 + 9 + 0 + 19 + 0 + 14 + 0 + 13 = **65**. Scalar check: 9 + 7 + 8 + 6 + 10 + 12 + 6 + 7 = 65.

**(c)** With `shift(v, k)` meaning "move every lane up by `k` positions and put zeros in the vacated lanes":

```
b            = [1, 2,  3,  4,  5,  6,  7,  8]
v1 = b  + shift(b, 1)  = [1, 3,  5,  7,  9, 11, 13, 15]
v2 = v1 + shift(v1, 2) = [1, 3,  6, 10, 14, 18, 22, 26]
v3 = v2 + shift(v2, 4) = [1, 3,  6, 10, 15, 21, 28, 36]     the running sum
```

To continue into the next vector, broadcast lane 7 of `v3` (36) and add it to the next vector's `v3`. That is 3 shifts + 3 adds + 1 broadcast + 1 add = about 8 vector operations for 8 elements, one per element, **no better than the scalar loop** (8 adds). Prefix sums vectorise poorly lane by lane; the practical trick is to vectorise *across* independent sequences (eight separate rows at once) instead.

**(d)**

| Machine | Vector trips | Scalar leftovers |
|---|---|---|
| AVX2, 8 lanes, scalar tail | 2 | 5 |
| AVX-512, 16 lanes, masked tail | 2 (16 + a masked 5) | 0 |
| NEON, 4 lanes, scalar tail | 5 | 1 |
| SVE at 128 bits (4 lanes) | 6 (4 × 5 + 1) | 0 |
| SVE at 256 bits (8 lanes) | 3 (8 + 8 + 5) | 0 |
| SVE at 512 bits (16 lanes) | 2 (16 + 5) | 0 |

### Problem B

**1. Early exit.** The loop may stop at any iteration, so the compiler cannot run iterations 8 to 15 until it knows 0 to 7 did not break (GCC: "control flow in loop"; clang: "could not determine number of loop iterations"). It is a property of the *program*: the meaning depends on order. Fix: find the stopping point first, then run a plain loop. For example scan blocks of 8 with a compare and `movemask` for `x < 0`, and when a block has a negative, finish it with scalar code; the part before it is a branch-free `sqrtf` loop (add `-fno-math-errno`).

**2. Recurrence.** `a[i]` needs `a[i-1]` (clang: "unsafe dependent memory operations"; GCC: no vectype). A true loop-carried dependence: program property. Fix: vectorise across independent data, not along time. If you filter eight channels, store them interleaved and let one vector hold one sample of every channel; the recurrence stays in time and the lanes are channels. Or restructure with a block form (`a[i+1] = 0.25 * a[i-1] + 0.5 * b[i] + b[i+1]`) if the extra work is acceptable.

**3. Dot product.** The obstacle is only floating-point reordering (GCC emitted an in-order reduction while claiming it vectorised; clang: "cannot prove it is safe to reorder floating-point operations"). A permission problem, not a dependence: you may allow the reordering. Fix: `#pragma omp simd reduction(+:s)` with `-fopenmp-simd`, or four to eight vector accumulators written with intrinsics (section 6.2), and one horizontal sum at the end.

**4. Histogram.** Two bytes in a block can have the same value, so two lanes would update the same bin and one increment would be lost (clang: "unsafe dependent memory operations"; GCC gave no vectorised loop). A real conflict: program property. Fix: several sub-histograms (`hist0[d[i]]++` for `i%4 == 0`, `hist1` for `i%4 == 1`, ...) merged at the end, which also breaks the store-to-load chain on one bin; or AVX-512 conflict detection (`vpconflictd`).

**5. Library call.** `logf` is an opaque scalar function that may set `errno`; clang says "call instruction cannot be vectorized", GCC "control flow in loop". The cause is the compiler's caution: a vector version exists. Fix: `-O3 -ffast-math` lets GCC call glibc's vector `logf` (I saw `_ZGVdN8v_logf`, from libmvec, in its output), or use a vector maths library (SLEEF, SVML) or your own polynomial approximation if the accuracy allows.

### Problem C

**(a)** 2 units × 16 lanes × 2 flops per FMA × 3 GHz = **192 GFLOP/s**.

**(b)** Each FMA needs **2 loads** (`x[i]` and `y[i]`) and the cache supplies 2 per cycle, so **1 vector FMA per cycle**: 16 lanes × 2 flops × 3 GHz = **96 GFLOP/s**, half of peak, limited by loads. Accumulators = latency × throughput = 4 × 1 = **4** (it would be 8 if the loads were not the limit).

**(c)** 8 bytes per element for 2 flops: **4 bytes per flop**. 20 GB/s ÷ 4 = **5 GFLOP/s**, 2.6% of peak. A scalar loop at one FMA per cycle gives 3 G FMA/s × 2 = 6 GFLOP/s, so **the scalar loop is already above the DRAM bound**: vectorising a DRAM-resident dot product gains nothing (the measured saxpy at 256 MB, section 5.2, gains 1.4×).

**(d)** 12 bytes (read `x`, read `y`, write `y`) per 2 flops = **6 bytes per flop**: 20 ÷ 6 = **3.3 GFLOP/s**.

**(e)** Machine balance: 192 GFLOP/s ÷ 20 GB/s = **9.6 flops per byte**. The dot product does 0.25 flops per byte and saxpy 0.17, so they sit 38 and 58 times below the balance point: memory-bound by a wide margin. A kernel needs reuse of data in cache (blocked matrix multiplication reaches tens of flops per DRAM byte) to be compute-bound.

### Problem D

My numbers over three runs (an unpinned run of the program above; GB/s):

| Version | 16 KB | 1 MB | 64 MB |
|---|---|---|---|
| scalar | 3.0 to 3.1 | 2.6 to 3.1 | 1.5 |
| auto, `long` counter | 4.8 to 4.9 | 4.6 to 4.9 | 1.9 to 2.3 |
| auto, `u8` counter | 53 to 56 | 50 to 54 | 10.9 to 12.2 |
| AVX2 intrinsics | 63 to 66 | 53 to 65 | 11.3 to 12.9 |

(a) The AVX2 intrinsic version should win at the two cache-resident sizes, by about 20 times over scalar, with the `u8` auto-vectorised version close behind; the `long` version gains only about 1.6 times. At 64 MB the section 9.1 run (pinned to one core, a loop unrolled by two) reached 17 to 25 GB/s and this program (unpinned) 11 to 13; I did not track down the difference, so expect anything between 10 and 25 on a cloud VM.

(b) At 64 MB the data come from DRAM at about 10 to 25 GB/s for one core. Any version that can consume bytes faster than that waits for memory, so the fast versions land on the same number. Only the scalar versions, which cannot keep up with the memory, are below it.

(c) `count_auto_long`: the loop body is 34 instructions for 32 bytes. One is the compare (`vpcmpeqb`) and one an `and` that turns 0xFF into 1; the other thirty are `vpmovzxbw`, `vpmovzxwd`, `vpmovzxdq`, `vextracti128` and `vpaddq`, which widen the 32 flags to 64-bit lanes and add them to a `long` accumulator. `count_auto_u8`: GCC unrolled the 240-byte block fully into pairs of `vpcmpeqb` and `vpsubb`: **two instructions per 32 bytes**, with the compare result (0 or −1) subtracted straight into an 8-bit vector accumulator. The widening happens once per block, with a single `vpsadbw` that sums the bytes into 64-bit lanes.

(d) At `-O2` GCC 13 vectorises none of the loops; I measured 3.0 GB/s for version 2 and 2.5 for version 3, the scalar rate, while the intrinsic version (65 GB/s at 16 KB) is unaffected because it is already vector code. Clang vectorises at `-O2`.

(e) Add a second broadcast constant and a second compare, `movemask` and `popcnt` on the *same loaded vector*. I measured counting `'\n'` and `','` in one pass against two separate passes (three runs). In cache (16 KB and 1 MB) the one-pass loop took 0.90 to 0.98 of the time of the two passes: **sharing the load saved almost nothing**, because the loop is limited by something other than the loads (most likely the single ports for `vpmovmskb` and `popcnt`; I did not confirm). From DRAM it moves half the data, and in two of three runs it took 0.64 to 0.69 of the time (the third run was noisy and showed 1.29). The lesson: removing a pass only helps when the pass was the bottleneck.

---

## Cheat sheet

- **SIMD:** one instruction applies to all lanes of a wide register: 128-bit SSE/NEON, 256-bit AVX/AVX2, 512-bit AVX-512, 128 to 2048-bit SVE, RISC-V `VLEN` (grouped by `LMUL`). Speed-up in compute is at most the lane count; memory bandwidth does not scale. Measured peak here: 12.8 / 51 / 102 / 184 GFLOP/s for scalar / SSE / AVX2 / AVX-512 (0.157 ns per FMA instruction at any width).
- **Lane-wise** operations (add, FMA, compare, logic, min/max, shifts) are cheap; **cross-lane** operations (shuffle, permute, horizontal add, `movemask`) are not, and many AVX2 shuffles work only within each 128-bit half. Reduce horizontally **once**, after the loop (mistake measured: 7× slower).
- **Vector machines:** vector length register, strip-mining (`vl = min(n, MAXVL)` each trip, no tail loop), chaining, masks. SVE (predicates `p0` to `p15`, `whilelt`, `cntw`) and RISC-V V (`vsetvli`, `vl`, `LMUL`) bring this back: the same binary runs at any vector length (checked at 128, 256 and 512 bits under QEMU). NEON has no gather or scatter; AVX2 has gather only.
- **x86 levels:** baseline x86-64 is SSE2; `x86-64-v2` SSE4.2, `v3` AVX2+FMA+BMI, `v4` AVX-512. A default build uses SSE2 only. `-march=native` crashes elsewhere. `vzeroupper` between AVX and legacy SSE.
- **Tails:** scalar epilogue, overlapping last vector (idempotent ops only), mask/predicate/`vl`, or pad. Reading past the end can fault; align the pointer down (an aligned block never crosses a page).
- **Alignment:** unaligned instructions on aligned data are free; a cache-line split costs one extra load slot (32-byte loads: 0.18 to 0.20 ns aligned, 0.24 to 0.30 with half splitting; 64-byte: 0.30 against 0.46 ns). Align to 64 for AVX-512. `malloc` gives 16 on glibc x86-64.
- **Memory-bound:** saxpy speed-up over scalar: 3.4 / 4.7 / 8.1 in L1 (SSE / AVX2 / AVX-512), about 4 in L2, 1.6 in L3, 1.4 in DRAM. Check bytes per flop against the machine balance before vectorising.
- **Gather:** not a fast load. Random from L1 1.3× faster than scalar, from L2 1.1×, from DRAM 1.0×; consecutive indices are 5 to 6× slower than a plain load. Scatter conflicts need AVX-512 `vpconflictd`. Prefer struct-of-arrays; the compiler's AoS vectorisation is about twenty instructions for eight elements.
- **FP reductions:** addition is not associative; the vector sum is a different, usually more accurate, answer that depends on the width (sum of 10⁸ ones: 16,777,216 in order, 100,000,000 with 32 or 128 partial sums). A chain runs at the add latency (0.6 ns per element scalar, 0.073 ns with one AVX2 accumulator); use latency × throughput accumulators (4 on this core: 0.0215 ns; AVX-512 with 8: 0.0156 ns, 38× the scalar loop). GCC without permission prints "vectorized" over an in-order chain. `#pragma omp simd reduction`, `-fassociative-math`, `-ffast-math` (bundle with side effects).
- **Denormals:** 92 to 94× slower (5 ns per element against 0.053); FTZ and DAZ (`MXCSR |= 0x8040`) fix it, per thread, at the price of treating tiny values as zero.
- **Saturating arithmetic:** `paddusb`, `uqadd`, `vsaddu`; free relative to wrapping; C integer promotion makes compilers work in wider lanes (scalar 2.4, GCC 6.8, intrinsics 95 GB/s). Fused multiply-add rounds once, so bits can differ from separate multiply and add.
- **Auto-vectorisation:** needs independent iterations, no aliasing (`restrict`), a known trip count (no early exit), vector forms for every operation (`-fno-math-errno` for `sqrtf`), contiguous access, and a target that has the instructions. GCC `-O2` vectorises none of the twelve test loops; `-O3` does. Narrow accumulators matter (`long` 4.8, `int` 11.4, `u8` blocks 59.7 GB/s). Read the report (`-fopt-info-vec-*`, `-Rpass*=loop-vectorize`) and the assembly (`ps`/`ymm` packed, `ss` scalar).
- **Dispatch:** `cpuid` plus `XCR0` (`__builtin_cpu_supports`); a function pointer, `target_clones`, or an ifunc resolver (glibc `memcpy`). Arm: `getauxval(AT_HWCAP)`; RISC-V: `AT_HWCAP`, `riscv_hwprobe`. Coarse grain; `target` attributes on helpers; test every path.
- **AVX-512:** masks, 32 registers, scatter, usable at 128 and 256 bits (VL); possible clock reduction, absent or half width on some CPUs, larger context; 512-bit FMA about 10% slower per instruction here; use for long streaming loops.
- **Patterns:** compare, `movemask`, `popcnt`/`tzcnt` (`memchr`, count, parse: 66 GB/s AVX2 and 144 GB/s AVX-512 in L1 against 3.1 scalar; all collapse to the DRAM limit of about 24 GB/s); masks instead of branches (18 to 25× on random data); nibble lookups with `pshufb` for byte classification; walk set bits with `tzcnt` and `m &= m - 1`.
- **Break-even:** a vector sum costs about 2 ns whatever the length up to 32; the scalar loop wins below 8 elements and loses from 16.

**Next:** tutorial 22, GPU architecture I: the execution model (why GPUs are built for throughput, the SIMT model with warps and wavefronts, thread blocks and streaming multiprocessors, divergence and predication, latency hiding by multithreading, registers, shared memory and occupancy). Say "next" when you are ready.
