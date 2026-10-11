# Core numbers — spaced repetition deck

Test yourself weekly. Write them from memory, then check. Ratios matter more than
absolutes; the starred ones you should have measured yourself.

## Latency ladder

    L1 hit                      ~1 ns      *
    Branch mispredict           ~5 ns
    L2 hit                      ~4 ns      *
    L3 hit                      ~20 ns     *
    Main memory                 ~80 ns     *
    TLB miss + page walk        ~25 ns     *
    Syscall                     ~100-400 ns  *  (depends on mitigations)
    Context switch              ~1-3 us
    Mutex lock, uncontended     ~20 ns
    Atomic RMW, uncontended     ~15-25 ns
    Atomic RMW, contended       ~100-300 ns
    SSD random read             ~50-100 us
    Same-DC network RTT         ~0.5 ms
    Cross-continent RTT         ~100-150 ms

## Throughput

    Memory bandwidth            ~20-100 GB/s per socket
    NVMe                        ~3-7 GB/s, 500k-1M IOPS
    10 GbE                      1.25 GB/s, 14.88 Mpps at 64 B
    100 GbE                     12.5 GB/s

## Geometry

    Cache line                  64 B (128 B on Apple silicon)
    Page                        4 KiB (2 MiB and 1 GiB huge pages)
    L1 dTLB reach               ~64 x 4 KiB = 256 KiB
    STLB reach                  ~2048 x 4 KiB = ~8 MiB
    STLB reach, 2 MiB pages     ~1024 x 2 MiB = ~2 GiB
    ROB                         300-600+ entries
    Line fill buffers (MLP)     10-16 outstanding misses

## Measured on my own hardware (fill these in)

    x86 false sharing penalty         5.0x
    ARM false sharing penalty         6.9x
    Column-major vs row-major, x86    10.0x
    Column-major vs row-major, ARM    5.1x
    AoS vs SoA, x86                   4.4x
    AoS vs SoA, ARM                   1.5x
    Hugepage gain at 16 MiB, x86      ~1.2x
    Hugepage gain at 16 MiB, ARM      4.4x
    4K aliasing penalty at 4 MiB      4.5x (x86), 3.2x (ARM)

## The arithmetic to have instant

    10 Mpps  => 100 ns per packet  => no syscall per packet, ever
    1 Gbps at 1500 B => ~83 kpps
    10 Gbps at 64 B  => 14.88 Mpps (line rate)
    50k devices x 200 series / 10 s => 1 M samples/s
    1 M samples/s x 16 B => 16 MB/s => 1.4 TB/day
    Rate-monotonic bound, n->inf => 69.3%
    EDF bound => 100%
    Branchless wins above ~6% mispredict rate (1 cycle vs 17 x P)
