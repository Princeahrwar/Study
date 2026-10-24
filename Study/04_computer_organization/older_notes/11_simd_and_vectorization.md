# CO 11 — SIMD and vectorization

> **Older note.** Written before the current plan. In the plan it becomes tutorial **CO 21**, and it will be rewritten in the simple tutorial style before we reach it. The number in the title above is the old one.

> Prerequisite: `../13_cache_basics.md` and `../14_cache_optimisation_and_the_real_hierarchy.md`.
> Relevant to: packet parsing, checksums, DSP in the Wi-Fi stack, edge AI kernels.

## When SIMD is worth anything

SIMD multiplies your *compute* throughput. It does nothing for memory latency.
So the decision rule is simply:

**SIMD helps when you are compute-bound on data that is already in cache.**

Check with the roofline model. Arithmetic intensity is FLOPs per byte moved from
memory. If your kernel does 0.25 operations per byte — which is typical of packet
processing, memcpy-like work, and most graph traversal — you are bandwidth-bound
and vectorizing the arithmetic changes nothing. Measure `topdown-be-bound` split
into memory vs core before reaching for intrinsics.

This is the reason most "I vectorized it and got nothing" stories happen, and
being able to explain it in one sentence is worth more in an interview than
knowing intrinsic names.

## The instruction sets you will meet

| ISA | Width | Where you meet it |
| --- | --- | --- |
| SSE2 | 128 bit | Baseline on every x86-64; always available |
| AVX2 | 256 bit | Mainstream x86 since ~2013; the safe default target |
| AVX-512 | 512 bit | Server x86; historically caused frequency throttling, much less so on recent parts |
| NEON (ASIMD) | 128 bit | **Baseline on every AArch64 — always available, no runtime check needed** |
| SVE / SVE2 | vector-length agnostic | Newer ARM server and some mobile silicon |

For your work the important line is NEON: on AArch64 it is mandatory, so unlike
x86 you never need a runtime dispatch check. That simplifies embedded builds
considerably.

SVE is worth understanding conceptually because it is genuinely different: code
is written without knowing the vector length, and the hardware supplies it
(128-2048 bit). Loops use predication and an increment-by-vector-length idiom
rather than a fixed unroll. "Write once, run at whatever width the chip has" is
the pitch, and it is a good thing to be able to describe.

## Let the compiler do it first

Auto-vectorization is genuinely good now. Always start here, and always check
whether it happened:

```bash
g++ -O3 -march=native -fopt-info-vec-missed -c kernel.cpp
clang++ -O3 -march=native -Rpass-analysis=loop-vectorize -c kernel.cpp
```

The compiler will refuse to vectorize when it cannot prove safety. The four
common blockers, and what to do about each:

1. **Possible aliasing.** The compiler cannot prove `dst` and `src` do not
   overlap. Fix with `__restrict`.
2. **Non-associative reductions.** FP addition, as in `../12_inside_a_modern_core.md`. Fix with
   manual multiple accumulators, or `#pragma omp simd reduction(+:sum)`, or
   accept `-ffast-math` if your numerics tolerate it.
3. **Data-dependent control flow** inside the loop. Fix by restructuring into a
   branchless/masked form.
4. **Unknown trip count or non-unit stride** making it unprofitable.

```cpp
// __restrict is the single highest-value annotation for numeric loops.
void scale_add(float* __restrict dst,
               const float* __restrict a,
               const float* __restrict b,
               float k, size_t n) {
    for (size_t i = 0; i < n; ++i)
        dst[i] = a[i] + k * b[i];        // vectorizes cleanly with restrict
}
```

## Intrinsics, when you must

Reach for intrinsics only when the compiler cannot express what you need —
shuffles, saturating arithmetic, horizontal reductions, table lookups. Here is a
realistic one for your domain: summing 16-bit counters, which appears in RSSI
aggregation and per-flow statistics rollups.

```cpp
#include <cstdint>
#include <cstddef>

#if defined(__aarch64__)
#include <arm_neon.h>

// NEON: sum n uint16 values into a uint64. Always available on AArch64.
uint64_t sum_u16_neon(const uint16_t* p, size_t n) {
    uint64x2_t acc = vdupq_n_u64(0);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        uint16x8_t v   = vld1q_u16(p + i);
        uint32x4_t w   = vpaddlq_u16(v);    // pairwise widen 16 -> 32
        acc            = vpadalq_u32(acc, w); // pairwise widen-accumulate 32 -> 64
    }
    uint64_t total = vgetq_lane_u64(acc, 0) + vgetq_lane_u64(acc, 1);
    for (; i < n; ++i) total += p[i];
    return total;
}

#elif defined(__AVX2__)
#include <immintrin.h>

// AVX2 equivalent. Note the widening: summing 16-bit lanes directly overflows.
uint64_t sum_u16_avx2(const uint16_t* p, size_t n) {
    __m256i acc = _mm256_setzero_si256();
    size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        __m256i v  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p + i));
        __m256i lo = _mm256_unpacklo_epi16(v, _mm256_setzero_si256());
        __m256i hi = _mm256_unpackhi_epi16(v, _mm256_setzero_si256());
        acc = _mm256_add_epi32(acc, _mm256_add_epi32(lo, hi));
    }
    alignas(32) uint32_t lanes[8];
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), acc);
    uint64_t total = 0;
    for (int k = 0; k < 8; ++k) total += lanes[k];
    for (; i < n; ++i) total += p[i];
    return total;
}
#endif
```

Three habits visible in that code, and they are what a reviewer looks for:

- **Widen before accumulating.** Summing many 16-bit values in 16-bit lanes
  silently overflows. This is the most common SIMD bug.
- **Always write the scalar tail.** `n` is rarely a multiple of the vector width.
- **Keep a scalar reference implementation** and assert they agree in tests. SIMD
  bugs are data-dependent and will not show up in a smoke test.

## Portable SIMD, and why you should prefer it

Maintaining two intrinsic paths is a real cost. Options in rough order of
preference for production code:

1. **Auto-vectorization with `__restrict` and clean loops.** Free, portable.
2. **`std::experimental::simd`** (C++ TS, landing as `std::simd`) or a library
   like Google Highway / xsimd. One source, per-target codegen.
3. **GCC/Clang vector extensions** — `typedef float v8f __attribute__((vector_size(32)));`
   Surprisingly capable and portable across both compilers.
4. **Raw intrinsics**, per architecture. Last resort.

If you write intrinsics, isolate them behind a narrow interface with a scalar
fallback, exactly as the HAL tutorial (`10_lld/02_systems_lld/05_hal_architecture`) describes. Intrinsics scattered
through business logic is the thing that makes codebases unportable.

## Runtime dispatch on x86

AVX2 is not universal, so a binary that must run anywhere needs a check. The
clean way is function multiversioning:

```cpp
// GCC/Clang: the compiler emits all variants and an ifunc resolver.
__attribute__((target_clones("default,avx2,avx512f")))
float dot(const float* a, const float* b, size_t n) {
    float s = 0.0f;
    for (size_t i = 0; i < n; ++i) s += a[i] * b[i];
    return s;
}
```

On AArch64 none of this is needed for NEON. That asymmetry is worth mentioning
when discussing embedded builds.

## The AVX-512 frequency caveat

Historically, heavy AVX-512 use — particularly 512-bit FMA — caused Intel cores
to drop frequency, so a vectorized routine could make the *rest* of the program
slower. This was real and led to advice to avoid AVX-512 in mixed workloads. On
recent server parts the effect is much reduced but not entirely gone.

The interview-safe framing: "I'd measure whole-application throughput, not just
the kernel, because on some Intel parts wide vector use has licence-based
frequency effects that show up outside the loop you optimised." That shows you
think about system effects rather than microbenchmarks.

## Interview angles

**"Would SIMD help here?"**
Ask about arithmetic intensity first. If the kernel is memory-bound, say so and
propose layout changes instead. This reframing is the answer they want.

**"Why didn't the compiler vectorize this?"**
Aliasing, non-associative reduction, control flow, or unprofitable stride. Name
all four, then say you would check `-fopt-info-vec-missed`.

**"How do you sum a vector register?"**
Horizontal reduction, and note it is relatively expensive — which is why you keep
several vector accumulators and reduce once at the end, not per iteration.

**"How do you ship one binary across x86 generations?"**
`target_clones` / ifunc, or manual `cpuid` dispatch. Mention that AArch64 has
NEON unconditionally.

## Exercises

1. Write `dot` scalar, then with `__restrict`, then with `-ffast-math`, then with
   explicit intrinsics for your architecture. Measure all four over 1M floats
   resident in L2. Then repeat over 256M floats. Explain why the ranking changes.
2. Compute the arithmetic intensity of three kernels you care about: a 5-tuple
   hash, an IP checksum, and a 3x3 convolution. Predict which benefits from SIMD.
   Then verify.
3. Deliberately write the 16-bit overflow bug in `sum_u16`, and construct the
   input that exposes it. Add the test that catches it.
4. Take `sum_u16_neon` and write the scalar reference plus a randomised
   differential test comparing them over 10,000 random inputs of random lengths.
   This is the discipline that makes SIMD safe to ship.
5. Read the disassembly of an auto-vectorized loop (`objdump -d`, or godbolt).
   Identify the vector body, the scalar prologue for alignment, and the scalar
   tail. Being able to recognise this shape on sight is genuinely useful.
