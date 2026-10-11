# Three capstone projects

One arc: silicon to kernel to fleet. Each ends in a number you measured, not a
README that claims it works. The number is what you bring to the interview.

A capstone is done when a stranger can clone it, run one command, and see your
numbers reproduce on their machine.

---

## Capstone 1 — `rkdma`

**A character driver that DMAs a packet stream into a userspace-mapped ring with
zero copies.** Lives in `capstones/cap1_dma_chardev/`. Start it after
`kernel_drivers/notes/04_dma.md`.

### Scope

A virtual device plus its driver. You are not writing a driver for real silicon —
you are writing every mechanism a real driver uses, against a device you also
write, so you can inject faults a real device would never give you on demand.

### Hard requirements

1. Out-of-tree module against a 6.x kernel **you built from source and booted in
   QEMU**. Building the tree is part of the exercise.
2. A virtual device bound as a `platform_driver` through a devicetree overlay,
   with an `hrtimer` standing in for the hardware interrupt.
3. Both DMA flavours, deliberately: a `dma_alloc_coherent` descriptor ring and
   `dma_map_sg` streaming payload buffers. You must be able to defend why each
   one is where it is.
4. The RX ring exposed to userspace via `mmap` using `vm_insert_page`, with
   `dma_sync_single_for_cpu` / `_for_device` in the correct places.
5. A threaded IRQ with a bounded budget loop, NAPI-style.
6. `ioctl` for configuration, `sysfs` attributes for counters, a tracepoint on
   every descriptor.

### Acceptance criteria

- 1000 insmod/rmmod cycles under KASAN and `CONFIG_DEBUG_KOBJECT` with zero
  leaks and zero warnings.
- A `memcpy` count of **zero** on the data path, proven by a tracepoint rather
  than by inspection.
- Sustained descriptor rate recorded with `perf stat`, plus the cycles-per-
  descriptor breakdown.
- A written memory-ordering argument for the producer/consumer indices that
  names the exact barriers and explains why a weaker one is wrong.

### Stretch

Add an IOMMU path and measure the translation cost. Then add a second consumer
and defend the ring's multi-reader semantics.

### What it proves

That you can be dropped at the user/kernel boundary and not flinch. Highest-
signal artifact for Nvidia and Cisco. Turns "stale data after DMA" from a quiz
into a story.

---

## Capstone 2 — `pfilt`

**A lock-free userspace packet classifier that survives an adversary.**
Lives in `capstones/cap2_lockfree_filter/`. Start it after
`lld/notes/04_lockfree_data_structures.md`.

### Hard requirements

1. Ingest from `AF_XDP`, or from a pcap replayer that hits the same API, into
   one SPSC ring per RX queue with head and tail on separate cache lines.
2. An open-addressed flow table keyed on the 5-tuple, with **a bucket of four
   entries in one 64 B line**, so a lookup is one cache miss and not three.
3. Keyed hashing (SipHash-class). An attacker who controls source addresses must
   not be able to construct your collision chain.
4. Rule updates from a control thread using epoch-based reclamation. **No lock
   on the packet path, ever.**
5. Per-core counters read through a seqlock snapshot. No exceptions, no
   allocation, no syscalls in the hot loop.

### Acceptance criteria

- A packets-per-second figure *for your machine*, with core count, packet size
  and `perf stat` output beside it. A borrowed number is worse than none.
- p50 / p99 / p99.9 per-packet latency from a histogram, not an average.
- False sharing eliminated and **proven** with `perf c2c` before and after.
- A rule update visible to the data path within a bounded number of epochs, and
  you state the bound.
- Clean under TSAN, ASAN and UBSAN with a multi-hour randomised stress harness.

### Stretch

Port the classifier to an XDP program and compare in-kernel against userspace on
identical traffic. The crossover point is an excellent interview anecdote.

### What it proves

That your lock-free claims come with evidence. Nearly a take-home for Palo Alto
Networks; the epoch-reclamation section answers half of Meta's and Google's
concurrency questions.

---

## Capstone 3 — `telemetrix`

**Edge-to-cloud telemetry ingest for a fleet the size of the one you already
run.** Lives in `capstones/cap3_telemetry_ingest/`. Start it after
`hld/notes/06_telemetry_pipelines.md`.

### The sizing, done once, in front of them

    50,000 APs x 200 series / 10 s interval  = 1.0 M samples/s
    x ~16 B encoded                          = 16 MB/s ~ 1.4 TB/day raw
    delta-of-delta + zstd                    ~ 100-150 GB/day stored

Do this arithmetic out loud in the first three minutes of any telemetry design
question. It reframes you from implementer to architect.

### Hard requirements

1. A device agent with a bounded local ring plus a write-ahead log, so a
   30-minute disconnect loses nothing and an 8-hour one degrades predictably.
2. Credit-based backpressure end to end. When the store is slow, the *edge* must
   know.
3. Consistent hashing by device id with virtual nodes, and a documented rebalance
   that does not stop ingest.
4. Idempotent writes keyed on (device, series, timestamp). At-least-once
   transport, effectively-once storage.
5. Rollup tiers 10s to 1m to 1h, with retention per tier.
6. Control plane strictly separate from the data plane: versioned config, canary
   cohort, automatic rollback.

### Acceptance criteria

- Sustained samples/s measured on a stated core count, with the bottleneck
  resource named.
- Documented behaviour at **2x overload**: exactly which data is shed first, and
  the argument for why that is the right thing to lose.
- Zero loss and a bounded duplicate rate after a simulated 30-minute partition.
- p99 ingest-to-queryable under 5 s, measured.
- A one-page cost model in dollars per day, naming the three inputs it is most
  sensitive to.

### What it proves

That you reason past one box. This is the artifact that unlocks Google and Meta
infra loops, and the one Cisco will use to level you *up* rather than sideways.

---

## The section interviewers actually read

Every capstone README needs a final section titled **"What I tried that did not
work."** Not a humility gesture — it is the highest-signal part of the document.
It shows the search, not just the answer. Candidates who include it read as
several years more senior than candidates who ship only the clean result.
