# 01 — CPU pipelines, out-of-order execution and speculation

> Prerequisite: none. Everything else in this tree assumes this file.
> Lab: `os_and_arch/labs/01_ilp/`. Exercises at the end.

## The one-sentence model

A modern core is a **dataflow machine wearing a sequential disguise**: it accepts
instructions in program order, executes them in whatever order their inputs
become available, and retires them in program order so the illusion holds.

Almost every performance question reduces to "what stopped the dataflow engine
from finding independent work?" There are only four answers, and they map
exactly onto the top-down profiling method you will use later in this file.

## Anatomy of a modern core

### Front end — turning bytes into uops

1. **Fetch.** A 16-32 B window is pulled from the L1i using the predicted next
   address. Note that fetch is *speculative from the very first stage* — the
   core does not know the next address, it predicts it.
2. **Decode.** x86 instructions are variable length (1-15 bytes), so decode is
   genuinely hard and is the reason x86 cores carry a **uop cache** (~1.5-4k
   uops) that bypasses decode entirely on hot loops. ARM's fixed 4-byte encoding
   is why ARM cores can decode 8 instructions wide without the same machinery —
   this is the single most real architectural difference between the two, far
   more than "RISC vs CISC".
3. **Allocate / rename.** Architectural registers (16 on x86-64, 31 on AArch64)
   are mapped onto a much larger physical register file (hundreds of entries).

Register renaming is worth pausing on because it eliminates two of the three
classic data hazards for free:

| Hazard | Example | Real dependency? | Fixed by renaming? |
| --- | --- | --- | --- |
| RAW (true) | `a = b + c; d = a + 1` | Yes | No — this is real dataflow |
| WAR (anti) | `d = a + 1; a = x` | No | Yes |
| WAW (output) | `a = 1; a = 2` | No | Yes |

**Only RAW dependencies actually cost you.** This is why "reuse the same
variable" never hurts performance and why breaking a *true* dependency chain is
the single most effective micro-optimisation available to you.

### Back end — the dataflow engine

- The **scheduler** (reservation station) holds uops whose operands are not yet
  ready. Each cycle it issues any uop whose inputs have landed to a free
  execution port.
- **Execution ports** are specialised. A typical big core has 2 load ports, 1-2
  store ports, and 4-6 ALU/FP/vector ports. Port pressure is a real limit: eight
  independent multiplies do not run in one cycle if only two ports do multiplies.
- The **reorder buffer (ROB)** — 300-600+ entries on current big cores — holds
  every in-flight instruction so they can be retired in order.
- **Retire** commits results architecturally. Until retire, everything is
  speculative and reversible.

The ROB size is your **speculation window**. If a load misses to DRAM and takes
300 cycles, the core keeps executing past it until the ROB fills. A 512-entry
ROB at 4 uops/cycle covers about 128 cycles of work. So a DRAM miss is *partly*
hideable and an L2 miss is usually *fully* hideable — provided there is
independent work to find.

### The memory side

- **Store buffer.** Stores retire into a buffer and drain to L1 later. This is
  the only reordering x86 allows (store to load), and it is why `mfence` exists.
- **Store-to-load forwarding.** A load matching a pending store gets the value
  from the buffer. When it partially overlaps, forwarding fails and you eat a
  10-15 cycle stall — a real and nasty effect in packet-parsing code that writes
  a header field then reads a different width from the same region.
- **Line fill buffers / MSHRs.** Typically 10-16 per core. This is your
  **memory-level parallelism (MLP)** budget: at most this many cache misses can
  be outstanding simultaneously. It matters enormously, as the next section shows.

## Why a pointer chase is the worst case

Consider two loops that touch exactly the same memory:

```c
// A: independent loads — the core can have ~12 in flight at once
for (size_t i = 0; i < n; ++i) sum += array[index[i]];

// B: dependent loads — strictly one at a time, forever
while (p) { sum += p->value; p = p->next; }
```

Loop A issues misses in parallel up to the MSHR limit, so the *effective* cost
per miss is `latency / MLP` — perhaps 80 ns / 12 ≈ 7 ns. Loop B cannot start
load `i+1` until load `i` returns, because the address *is* the result. Every
miss costs its full latency.

This is exactly why every latency benchmark in this tree (`tlb2.cpp`) uses a
pointer chase: it is the only way to measure true latency rather than
throughput-with-parallelism. And it is why linked lists lose to arrays far more
badly than their O-notation suggests.

**Interview phrasing:** "That structure serialises the memory accesses, so I
lose memory-level parallelism — I'm paying full latency per element instead of
latency divided by twelve."

## Speculation and what a misprediction costs

Because fetch is speculative from stage one, a mispredicted branch means every
instruction fetched since the branch is wrong. The core must flush the pipeline
and restart at the correct target.

The cost is roughly **the pipeline depth**: 15-20 cycles on big x86 cores,
similar on big ARM cores. At 3 GHz that is 5-7 ns, or about the cost of an L2
miss. That framing matters: *a mispredicted branch is as expensive as a cache
miss*, which is why data-dependent branches in a packet-processing inner loop
are a genuine architectural problem and not a style issue.

Detail worth knowing: modern cores do not flush the *whole* ROB on a
misprediction. They checkpoint at the branch and roll back to it, which is why
the penalty is pipeline depth rather than ROB depth.

Branch prediction gets its own tutorial (`04_branch_prediction.md`). What you
need here is only the cost model.

## The top-down method — the only profiling framework worth memorising

Every retirement slot in every cycle is in exactly one of four buckets. This
partition is exhaustive, which is what makes it useful:

```
                    ┌─ Retiring ......... useful work (goal: maximise)
   Every uop slot ──┼─ Bad speculation .. work done then thrown away
                    ├─ Front-end bound .. starved: no uops delivered
                    └─ Back-end bound ... stalled: uops delivered, can't execute
                         ├─ Core bound ..... port pressure, long dependency chains
                         └─ Memory bound ... waiting on L1/L2/L3/DRAM
```

Run it like this:

```bash
perf stat --topdown -a ./your_app
# or, for the detailed breakdown on Intel:
perf stat -e '{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound}' ./your_app
```

**How to use it in an interview.** When someone says "this service is slow, what
do you do?", the senior answer names a tool. The staff answer names this
partition first: "I'd start top-down to find out whether we're retiring, mis-
speculating, starved, or stalled — those need completely different fixes, and
guessing between them wastes days."

## The practical lever: break dependency chains

This is the most directly actionable consequence of everything above. Floating
point addition is not associative, so the compiler **cannot** legally reassociate
this reduction without `-ffast-math`:

```cpp
// One dependency chain. Each add waits for the previous.
// FP add latency ~4 cycles => ~4 cycles per element, regardless of throughput.
double sum_serial(const double* a, size_t n) {
    double s = 0.0;
    for (size_t i = 0; i < n; ++i) s += a[i];
    return s;
}

// Four independent chains. The core interleaves them across FP ports.
// Approaches ~1 cycle per element on a core with 2 FP add units.
double sum_unrolled(const double* a, size_t n) {
    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i + 0];
        s1 += a[i + 1];
        s2 += a[i + 2];
        s3 += a[i + 3];
    }
    double s = (s0 + s1) + (s2 + s3);
    for (; i < n; ++i) s += a[i];
    return s;
}
```

Expect roughly 3-4x on a machine with adequate FP throughput. The lab directory
has this as a runnable benchmark — run it, do not trust the paragraph.

Note the honest caveat: the two functions produce **different results**, because
FP addition is not associative. For summing sensor readings that is fine. For
money it is not. Knowing which situation you are in is the senior part.

## Common misconceptions to unlearn

- *"Fewer instructions is faster."* Often false. `sum_unrolled` executes more
  instructions than `sum_serial` and runs 3-4x faster. Instructions are not the
  currency; **stalls** are.
- *"The compiler will unroll it."* It will unroll the loop but it will not
  create independent accumulators for FP, because that changes results.
- *"Branchless is always better."* A `cmov` has a real dependency on its inputs;
  a correctly-predicted branch is effectively free. Branchless wins only when the
  branch is genuinely unpredictable. Details in tutorial 04.
- *"IPC is the metric."* IPC of 4 while spinning on a lock is worthless. Retiring
  *useful* work is the metric, which is why top-down separates retiring from the
  rest.

## Interview angles

**"Walk me through what happens when the CPU executes `a = b + c`."**
They are checking whether you know fetch/decode/rename/schedule/execute/retire
and, critically, that retire is in-order while execute is not. Mention the store
buffer if `a` is memory.

**"Why is a linked list slow?"**
Weak answer: cache locality. Strong answer: locality *and* serialised
dependent loads that destroy memory-level parallelism — you pay full latency per
node instead of latency/MLP.

**"This loop has an IPC of 0.4. What now?"**
Top-down first. Do not guess.

**"What limits how far ahead the CPU can run past a cache miss?"**
ROB size, and secondarily the MSHR count for how many misses can be outstanding.

## Exercises

1. Build and run `sum_serial` vs `sum_unrolled` over 64M doubles. Sweep the
   accumulator count from 1 to 16 and plot cycles per element. Find the knee, and
   explain it in terms of FP add latency divided by the number of FP ports on
   your chip. Commit the plot and the explanation.
2. Write the pointer-chase loop and the indexed-gather loop over the same
   randomly permuted 256 MB array. Measure both. Compute the implied MLP of the
   gather version (`latency_chase / latency_gather`). Compare to your chip's
   documented fill-buffer count.
3. Run `perf stat --topdown` on all four programs above. Predict which bucket
   dominates each one *before* running it. Write down your predictions first —
   the value is in being wrong on paper, not in being right.
4. Construct a store-to-load forwarding stall: write a 32-bit field of a struct,
   then immediately read an overlapping 64-bit field. Measure against the
   non-overlapping version. This one is subtle; expect to need `perf` counters
   (`ld_blocks.store_forward` on Intel) to confirm you actually hit it.
