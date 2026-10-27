# The mock bank

Fourteen scenarios with rubrics. Run one per week under a real clock, out loud,
writing on paper or a whiteboard — not in your head. Save each attempt as
`YYYY-MM-DD_<name>.md` in this directory with your answer and your self-score.

The rubric column that matters is the gap between your answer and the Staff
column. That gap is your study plan.

---

## M1 — Tail latency in a packet thread (45 min, debugging)

> "One of our access points runs a single packet-processing thread pinned to a
> core. Mean per-packet service time is 3 us. At 2 Gbps offered load, p99.9 is
> 400 us. The core is 40% idle and there is no lock in the hot path. Where is the
> time going?"

**Clock plan.** 0-4 min: quantify (167 kpps at 1500 B, 6 us budget, ~50%
utilised, so this is variance not throughput; 400 us ≈ 130 packets of service
time). 4-12: enumerate failure domains before testing any — scheduling,
memory/TLB, coherence, queueing, periodic work. 12-28: cheapest discriminating
tests first. 28-45: commit to a hypothesis with a falsification test and a fix.

| | Answer |
| --- | --- |
| No hire | Lists cache misses, context switches and page faults as a flat menu, no ordering, no arithmetic |
| Senior | Picks a plausible cause, names the right tool, would find it — but is led by the interviewer's hints |
| Staff | Converts 400 us into "130 packets of service time" in two minutes, enumerates domains before touching a tool, picks the cheapest discriminating test, ends with a fix *and* the measurement that would prove them wrong |

**Model answer sketch.** Leading hypothesis: a periodic aging sweep over the flow
table. 400 us at ~0.4 ns per touched line is roughly a 1M-entry sequential scan.
Test: does spike period match the sweep interval, and does
`dtlb_load_misses.walk_active` spike with it? Fix: amortise the sweep into the
packet path — N buckets per packet — converting a 400 us tail into nanoseconds of
mean. (See `05_operating_systems/10_virtual_memory_and_tlb`, `09`, and `10_lld/02_systems_lld/04_lockfree_data_structures`.)

**Follow-up ladder.** "The core is isolated and tickless, now what?" (removes the
easy answer) -> "another core just called munmap" (TLB shootdown IPI) -> "there is
one shared counter, bound its cost" (RFO traffic) -> "same code on ARM behaves
differently, why" -> "you cannot reproduce it in the lab, ship something anyway"
(the real Staff question: build observability instead of guessing).

---

## M2 — Per-flow statistics table (40 min, LLD)

> "Design a per-flow statistics table for one million concurrent flows on a
> four-core ARM64 access point with 512 MB of RAM, updated at 3 Mpps. Memory
> layout first, then the concurrency model."

Must produce: bytes per entry and total footprint; probe strategy and expected
cache misses per lookup; a hugepage decision **with the TLB-reach arithmetic**;
eviction/aging policy and its tail-latency cost; the update model across four
cores; and one sentence on what an attacker who controls the 5-tuple does.

| | Answer |
| --- | --- |
| No hire | A `std::unordered_map` with a mutex |
| Senior | Open addressing, sharded locks, sensible sizing |
| Staff | Bucket-per-cache-line so a lookup is one miss; keyed hash for DoS resistance; 2 MiB pages justified by reach arithmetic; aging amortised into the packet path; per-core counters; states the eviction policy and what it counts |

Self-score by counting the numbers in your answer. Fewer than six and you wrote
an opinion. (See `10_lld/02_systems_lld/04_lockfree_data_structures`, `05_operating_systems/10_virtual_memory_and_tlb`.)

---

## M3 — Telemetry ingest (45 min, HLD)

> "Fifty thousand access points, 200 metrics each, every 10 seconds. Design the
> ingest and storage."

| | Answer |
| --- | --- |
| No hire | Draws Kafka, a database and a dashboard with no numbers |
| Senior | Correct components, reasonable sharding, mentions retention |
| Staff | Arithmetic in the first three minutes (1 M samples/s, 16 MB/s, 1.4 TB/day raw, ~150 GB/day stored), **names the bottleneck resource**, credit-based backpressure end to end, idempotency key, rollup tiers with min/max/count, and a stated 2x-overload shed policy |

**Follow-ups to expect:** device offline for a day; 5,000 devices reconnecting at
once; cardinality explosion; what it costs per day. (See `11_hld/01_fundamentals/02_estimation_and_budgets`, `05`, `06`.)

---

## M4 — Zero-copy receive path (35 min, systems)

> "Get 10 million packets per second into userspace on one machine."

Staff answer climbs the ladder explicitly (recvmmsg -> AF_PACKET -> XDP ->
AF_XDP -> DPDK), does the 100 ns/packet arithmetic first, picks a rung, and says
what that rung gives up. Reaching straight for DPDK without the arithmetic is the
failure mode. (See `07_networking/07_zero_copy_networking`, `05_operating_systems/15_user_kernel_boundary`.)

---

## M5 — Config push to 50,000 devices (40 min, HLD)

> "Push a configuration change to the whole fleet safely."

Staff answer: desired state not commands, versioned and idempotent, canary
cohorts with health gates and automatic halt, and — the part most candidates miss
— **device-side automatic revert that does not depend on the controller**.
(See `11_hld/03_systems_domain/03_edge_to_cloud_fleet_management`.)

---

## M6 — Lock-free queue review (30 min, code review)

> Here is an SPSC queue. Is it correct?

The bugs to plant and find: a `release` weakened to `relaxed`; `head` and `tail`
adjacent in one cache line; a non-power-of-two capacity with a modulo; missing
`acquire` on the opposite index; full/empty ambiguity.

Staff answer names each, says which are *correctness* bugs and which are
*performance* bugs, and notes that the relaxed one will pass every test on x86
and fail on ARM. (See `05_operating_systems/07_memory_barriers`, `10_lld/02_systems_lld/03_thread_safe_primitives`.)

---

## M7 — Character driver with DMA (45 min, kernel)

> "Write a driver that moves data from a device into userspace with no copies."

Must cover: `file_operations` and cdev registration; coherent descriptor ring vs
streaming payload buffers with a justification for each; the barrier placement
around descriptor and doorbell; `mmap` via `dma_mmap_coherent`; threaded IRQ with
a bounded budget; and the ownership rule between map and unmap.
(See `09_drivers/03_char_driver_and_model`, `03`, `04`.)

---

## M8 — Distributed rate limiter (35 min, HLD)

> "Rate limit an API across fifty servers."

Staff answer gives all three designs (central store, local buckets with
reconciliation, consistent-hash ownership), picks one, and answers "what happens
when the coordination store is down" before being asked. Bonus for distinguishing
rate limiting, load shedding and backpressure. (See `11_hld/01_fundamentals/15_rate_limiters`.)

---

## M9 — Cache design (30 min, HLD)

> "Add a cache in front of this database."

Staff answer justifies the cache from the access distribution first, then
cache-aside with **invalidate not update**, then the three failure modes
(stampede, penetration, avalanche) with fixes, then consistent hashing with
virtual nodes and *why* virtual nodes exist. (See `11_hld/01_fundamentals/06_distributed_caching`.)

---

## M10 — Edge inference (30 min, embedded AI)

> "Run a vision model on a fanless access point at 30 fps."

Staff answer leads with memory-bound and the roofline, treats quantization as a
bandwidth optimisation, covers fusion, the host-device copy, preprocessing cost,
and **thermal throttling on sustained load** — and insists on measuring sustained
rather than burst. (See `13_embedded/02_edge_ai_acceleration`.)

---

## M11 — Debugging war story (30 min, behavioral+technical)

> "Tell me about the hardest bug you have root-caused."

This is scored on method, not on the bug. Structure: symptom, what made it hard,
hypotheses considered and eliminated, the measurement that discriminated, the
fix, and **what you changed so it could not recur silently**. That last clause is
the Staff marker.

---

## M12 — Ambiguous ownership (30 min, behavioral)

> "You inherit a system nobody understands and it is on fire. First week?"

Staff answer: stabilise before understanding (stop the bleeding), build
observability before refactoring, find the one person who knows something, write
down what you learn as you learn it, and communicate a plan with dates. The
failure mode is starting with a rewrite.

---

## M13: Timestamps that jump by 71 minutes (30 min, hardware/firmware)

> "Our devices log a timestamp from a free-running 64-bit microsecond counter that
> the SoC exposes as two 32-bit registers. About once every few hundred
> device-days a log line is wrong by exactly 71.6 minutes. The code reads high,
> then low. Explain it, put a number on how often it should happen, and fix it."

**Clock plan.** 0-5 min: recognise 2^32 us = 71.6 min, so the error is a carry
from the low word into the high word, not a clock fault. 5-15: draw the
timeline: read `hi`, the low word wraps, read `lo`; the pair is a value that
never existed. 15-25: estimate the rate. The window is about 200 ns, the code
reads 1000 times a second, so one carry in 5,000 lands in the window
(2 x 10^-4); a carry comes once per 71.6 minutes, so one bad line per device in
about 250 days, and for a fleet of 50,000 devices one somewhere every 7 minutes.
25-30: fix and prevention.

| | Answer |
| --- | --- |
| No hire | "Race condition, add a lock"; a lock cannot stop a free-running hardware counter |
| Senior | Finds the torn read, proposes disabling interrupts or reading twice, cannot say how rare it is |
| Staff | Names the carry, does the rate arithmetic, fixes it with the hi-lo-hi retry (read `hi`, `lo`, `hi` again; repeat if `hi` changed) or a latched/shadow register, and adds a test that forces the counter near the carry so the bug is reproducible in minutes instead of months |

The same shape appears on any multi-word status register that hardware updates
while software reads it, and the Gray-code and synchroniser material in
`04_computer_organization/04_sequential_logic_and_state_machines` section 9 is
the hardware side of the same problem: a multi-bit value crossing a boundary
without a single atomic snapshot.

---

## M14: The filter that got 20 times slower (30 min, performance)

> "An audio gain filter processes 10,000-sample blocks in about 20 us. When the
> microphone is muted, blocks take about 400 us, and the code path is the same.
> The filter is `y = 0.99 * y + x`. Why?"

**Clock plan.** 0-5 min: the work per block is identical, so the cost is per
operation, not per branch; ask what changes in the data. 5-15: with `x = 0` the
state decays by 0.99 each sample and, after about 70,000 samples of silence,
enters the subnormal range. 15-25: the arithmetic: a dependent multiply and add is
about 6 cycles, 10,000 of them at 3.2 GHz is 19 us; if each multiply costs an extra
130-cycle assist that becomes about 420 us, a factor of 22. 25-30: fixes and
how to confirm.

| | Answer |
| --- | --- |
| No hire | Blames cache misses or the OS because the code did not change |
| Senior | Suspects denormals from experience, knows flush-to-zero exists, cannot say why only some inputs trigger it |
| Staff | Gets the sample count and the 22x from the arithmetic, explains that the multiply with a subnormal input or result takes a microcode assist, names the fixes (set FTZ and DAZ for the thread, add a tiny DC offset or clamp the state to zero below a threshold), and says what they cost: FTZ changes results below 2^-126 or 2^-1022, so check the algorithm tolerates it; it is also per-thread state, so set it in each worker |

On Arm the equivalent controls are the FZ bit in FPCR; the cost of a subnormal
there is implementation-defined, so measure rather than assume. (See
`04_computer_organization/05_computer_arithmetic_and_the_alu` section 5.5.)

---

## Scoring yourself

After each mock, score honestly against the rubric and write one line: *what
would the Staff answer have included that mine did not?* Track that line across
attempts. When it stops changing, you are ready for that category.
