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

## Measured in the computer-organization tutorials (study VM, 2 vCPUs, about 3.2 GHz)

These came from one cloud VM, one session, run-to-run noise of 10 to 30%. They are
here to give you the right order of magnitude and the right ratios, and they are
separate from the numbers you measure yourself above. Where the VM reports a
different CPU than it runs on (it did), the measured figure wins.

    Arithmetic, latency in cycles (throughput per cycle in brackets)
    integer add                       1   (4)
    integer multiply                  3   (1)
    multiply, high half               4   (1)
    popcnt, lzcnt                     3   (1)
    divide, 32-bit                    11 to 12   (one per 6)
    divide, 64-bit                    14 to 15   (one per 10)
    divide by a constant (magic)      ~4  (multiply, shift, fix-up)
    FP add                            2   (2)
    FP multiply, FMA                  4   (2)
    FP divide, single / double        11 / 14 to 15
    FP square root, single / double   13 / 19
    subnormal assist                  ~130 cycles (~40 ns); flush-to-zero removes it

    Memory, pointer chase
    L1 hit                            5 cycles   ~1.5 ns   (48 KB, 12-way here)
    L2 hit                            14 cycles  4 to 5 ns  (~1 MB)
    L3 hit                            77 cycles  ~24 ns     (big server mesh)
    DRAM                              ~95 ns with huge pages, ~150 ns with 4 KB pages
    line from another core's cache    56 to 102 ns

    Operating-system rungs (each about 10x the one above)
    raw system call                   115 to 126 ns
    minor page fault                  1.1 to 1.3 us
    context switch, one way           1.2 to 1.5 us
    wake a thread on another CPU      ~10 us
    fork + exec + wait                0.8 to 1.2 ms

    Rules and shapes to say without looking
    adder depth                       log2(n) levels; 64-bit Kogge-Stone: 6 levels, 321 cells
    rounding                          round up if G and (R or S or L); ties go to even
    sticky bit                        without it a 5-bit adder was wrong on 2,292 of 173,056 pairs
    divide by constant                m = ceil(2^k / d); check x * e < 2^k
    AMAT                              T_L1 + m_L1 * (T_L2 + m_L2 * T_mem), local miss rates
    synchroniser MTBF                 e^(t_r / tau) / (T_W * f_clk * f_data); each extra flop multiplies it
    async FIFO                        Gray-coded (n+1)-bit pointers; empty: equal; full: top two bits differ
    break-even for power gating       energy to enter and leave / power saved = ~2.5 ms in the worked example
