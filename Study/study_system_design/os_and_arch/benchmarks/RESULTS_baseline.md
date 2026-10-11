# Module 1 baseline — reference run

Host: Intel Xeon @ 2.10 GHz, 2 vCPU, L1d 48 KiB/core, L2 2 MiB/core, shared L3.
Toolchain: g++ 13.3.0, `-O2 -std=c++17`. Shared cloud VM — absolute ns are soft,
ratios reproduce. Replace this file with YOUR numbers; keep this one as `RESULTS_reference.md`
if you want the comparison.

## ./cache

    [1] stride sweep (64 MiB buffer)
      stride     1 B :   33.77 ms   -> 0.50 ns/touch
      stride    16 B :    9.98 ms   -> 2.38 ns/touch
      stride    64 B :    5.67 ms   -> 5.41 ns/touch
      stride   256 B :    2.12 ms   -> 8.09 ns/touch
      stride  4096 B :    0.16 ms   -> 9.77 ns/touch

    [2] 4096x4096 float matrix traversal
      row-major   :   14.72 ms
      col-major   :  146.66 ms   (10.0x slower)

    [3] AoS vs SoA (4M particles, 64B each)
      AoS  .x sum :   16.67 ms
      SoA  .x sum :    3.79 ms   (4.4x faster)

    [4] false sharing, 2 threads x 20M relaxed RMW
      packed  (false sharing):  720.09 ms
      64B-aligned (no sharing): 145.05 ms   (5.0x)

## ./tlb2

      pages  footprint  lines touched | 4K scattered | 4K aliased | THP scattered
         16      64 KiB         1 KiB |      1.77 ns |    5.73 ns |      1.82 ns
         64     256 KiB         4 KiB |      1.77 ns |    5.79 ns |      1.81 ns
        256    1024 KiB        16 KiB |      4.29 ns |    8.18 ns |      4.29 ns
       1024    4096 KiB        64 KiB |      7.92 ns |   35.47 ns |      8.07 ns
       4096   16384 KiB       256 KiB |     20.34 ns |   45.94 ns |     16.86 ns
      16384   65536 KiB      1024 KiB |     27.44 ns |   48.35 ns |     21.14 ns
      65536  262144 KiB      4096 KiB |     71.11 ns |  134.74 ns |     56.80 ns

Note on the "4K aliased" column: version 1 of this benchmark placed every chase node at
offset 0 within its page — a clean 4096 B stride, which maps every node onto the same L1
set. It produced a plausible-looking TLB curve that was substantially cache-conflict
misses. Version 2 scatters each node to a pseudo-random 64 B slot inside its page. Both
columns are kept because the gap between them is the lesson.

Caveats worth stating in an interview: THP was confirmed granted on this host
(AnonHugePages == requested size), and still bought only 17-25% at the large footprints
and nothing below 4 MiB. Hugepages help exactly where TLB reach is the binding
constraint, and nowhere else.
