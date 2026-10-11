# The mock bank

Twelve scenarios with rubrics. Run one per week under a real clock, out loud,
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
mean. (See `os_and_arch/07`, `09`, and `lld/04`.)

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
an opinion. (See `lld/04`, `os_and_arch/07`.)

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
once; cardinality explosion; what it costs per day. (See `hld/01`, `05`, `06`.)

---

## M4 — Zero-copy receive path (35 min, systems)

> "Get 10 million packets per second into userspace on one machine."

Staff answer climbs the ladder explicitly (recvmmsg -> AF_PACKET -> XDP ->
AF_XDP -> DPDK), does the 100 ns/packet arithmetic first, picks a rung, and says
what that rung gives up. Reaching straight for DPDK without the arithmetic is the
failure mode. (See `kernel_drivers/05`, `os_and_arch/12`.)

---

## M5 — Config push to 50,000 devices (40 min, HLD)

> "Push a configuration change to the whole fleet safely."

Staff answer: desired state not commands, versioned and idempotent, canary
cohorts with health gates and automatic halt, and — the part most candidates miss
— **device-side automatic revert that does not depend on the controller**.
(See `hld/07`.)

---

## M6 — Lock-free queue review (30 min, code review)

> Here is an SPSC queue. Is it correct?

The bugs to plant and find: a `release` weakened to `relaxed`; `head` and `tail`
adjacent in one cache line; a non-power-of-two capacity with a modulo; missing
`acquire` on the opposite index; full/empty ambiguity.

Staff answer names each, says which are *correctness* bugs and which are
*performance* bugs, and notes that the relaxed one will pass every test on x86
and fail on ARM. (See `os_and_arch/10`, `lld/03`.)

---

## M7 — Character driver with DMA (45 min, kernel)

> "Write a driver that moves data from a device into userspace with no copies."

Must cover: `file_operations` and cdev registration; coherent descriptor ring vs
streaming payload buffers with a justification for each; the barrier placement
around descriptor and doorbell; `mmap` via `dma_mmap_coherent`; threaded IRQ with
a bounded budget; and the ownership rule between map and unmap.
(See `kernel_drivers/02`, `03`, `04`.)

---

## M8 — Distributed rate limiter (35 min, HLD)

> "Rate limit an API across fifty servers."

Staff answer gives all three designs (central store, local buckets with
reconciliation, consistent-hash ownership), picks one, and answers "what happens
when the coordination store is down" before being asked. Bonus for distinguishing
rate limiting, load shedding and backpressure. (See `hld/03`.)

---

## M9 — Cache design (30 min, HLD)

> "Add a cache in front of this database."

Staff answer justifies the cache from the access distribution first, then
cache-aside with **invalidate not update**, then the three failure modes
(stampede, penetration, avalanche) with fixes, then consistent hashing with
virtual nodes and *why* virtual nodes exist. (See `hld/04`.)

---

## M10 — Edge inference (30 min, embedded AI)

> "Run a vision model on a fanless access point at 30 fps."

Staff answer leads with memory-bound and the roofline, treats quantization as a
bandwidth optimisation, covers fusion, the host-device copy, preprocessing cost,
and **thermal throttling on sustained load** — and insists on measuring sustained
rather than burst. (See `embedded_ai/02`.)

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

## Scoring yourself

After each mock, score honestly against the rubric and write one line: *what
would the Staff answer have included that mine did not?* Track that line across
attempts. When it stops changing, you are ready for that category.
