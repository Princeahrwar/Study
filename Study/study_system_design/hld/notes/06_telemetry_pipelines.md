# 06 — Telemetry pipelines

> Prerequisite: `01_estimation_and_budgets.md`, `05_storage_engines.md`.
> This is Capstone 3, and it is the design question closest to your actual
> experience. Own it.

## The shape

```
  device agent -> gateway -> ingest -> storage -> query
       |             |          |         |
     buffer      backpressure  shard   tiering
      WAL         + auth      + dedupe  + rollup
```

Each stage exists for a reason you should be able to state in one sentence.
Being able to say why a stage exists — rather than listing stages — is the
difference between recitation and design.

## Start with the arithmetic

```
  50,000 APs x 200 series / 10 s          = 1.0 M samples/s
  x 16 B encoded                          = 16 MB/s = 1.4 TB/day raw
  delta-of-delta + XOR + zstd (~1.5 B)    = ~150 GB/day stored
  30-day retention at full resolution     = ~4.5 TB
  working set (last 1 h)                  = ~7 GB -> fits in memory

  Bottleneck: write throughput to the store. Not CPU, not network.
```

Say this in the first three minutes. Then design against it.

## The device agent

The stage most candidates skip, and the one you have real credibility on.

```cpp
// Requirements, in priority order:
//   1. Bounded memory. An agent that OOMs the AP is worse than losing data.
//   2. Survive disconnection. A 30-minute outage must lose nothing.
//   3. Never block the data plane. Telemetry is not worth a dropped frame.
//   4. Batch and compress before sending. Egress costs money.
```

Design: a lock-free SPSC ring (tutorial `lld/03`) written by the collection
thread, drained by a sender thread. When the ring fills, spill to a bounded
write-ahead log on flash; when the WAL fills, **drop the oldest** and increment a
counter that is itself telemetry.

Points that show operational experience:

- **Drop oldest, not newest.** For metrics, recent data is more valuable. For
  logs and events, often the opposite. State the choice.
- **Flash wear** is a real constraint on an AP. A WAL that writes constantly will
  kill the device. Write only on disconnection, and rate-limit.
- **Jitter the reporting interval.** 50,000 devices with synchronised clocks all
  reporting at `t % 10 == 0` is a self-inflicted thundering herd, and it hits
  exactly when you have a fleet-wide incident.
- **The agent must degrade before the device does.** Cap its CPU and memory
  explicitly.

## Backpressure

The single most important property, and the one that distinguishes a real design.

```
  credit-based flow control, end to end:

  storage slow  ->  ingest stops issuing credits
                ->  gateway stops issuing credits
                ->  agent stops sending, buffers locally
                ->  agent's buffer fills, agent sheds by policy
```

The alternative — unbounded queues — does not fail gracefully. It fails by
consuming all memory somewhere in the middle, and the failure is far from the
cause. **Every queue in a pipeline must be bounded, and every bounded queue must
have a stated policy for what happens when it is full.**

That is the sentence to say. Then: at 2x sustained overload, what gets shed?
A defensible answer for telemetry: drop the highest-cardinality, lowest-value
series first (per-client counters before per-radio counters), keep everything
needed for alerting, and never drop the counter that tells you you are dropping.

## Transport

| Choice | For | Against |
| --- | --- | --- |
| gRPC streaming | Efficient, typed, flow control built in | HTTP/2 overhead on tiny devices |
| MQTT | Designed for constrained devices, QoS levels | Broker becomes a bottleneck and a dependency |
| Plain HTTP batch POST | Simple, debuggable, works everywhere | No flow control, per-batch overhead |
| Custom UDP | Minimal overhead | You will reimplement reliability badly |

For 50,000 APs on reasonable hardware, gRPC streaming with application-level
credits is a good default. Say what you would pick and name the one thing that
would change your mind — for example, "if the devices were 8-bit MCUs on
cellular, MQTT."

## Ingest

```
  shard by device_id (consistent hashing, virtual nodes)
     -> each shard owns a set of devices, writes independently
     -> a hot device cannot affect other shards

  idempotency key = (device_id, series_id, timestamp)
     -> at-least-once transport + idempotent write = effectively once
     -> retries are free, which is what makes the transport simple
```

**Why shard by device rather than by series:** all of one device's data lands in
one place, so a device's reconnection burst is contained, and the WAL replay is
one shard's problem. The cost is that a query across all devices for one series
fans out, which the rollup tier mitigates.

Choosing the shard key and being able to defend it against the alternative is the
core of this question.

## Storage and rollups

```
  tier        resolution   retention   size
  hot         10 s         7 d         ~1 TB     in-memory + local SSD
  warm         1 m         90 d        ~2 TB     SSD
  cold         1 h         2 y         ~1.5 TB   object storage
```

Roll up **continuously**, not in a nightly batch — a batch job is a periodic
latency and I/O spike, which is exactly the tail-latency pattern from
`os_and_arch/09`. Store min/max/sum/count per bucket, not just the average, so
downstream aggregation stays correct.

## Query

- **Pre-aggregate what people actually ask for.** Dashboard queries are
  predictable; ad-hoc ones are rare. Serve the common case from a rollup.
- **Bound query cost.** A query that scans 2 years at 10 s resolution should be
  rejected, or silently served from the rollup tier with a note. Unbounded
  queries are how one user takes down the cluster.
- **Cache aggressively** — dashboards poll the same ranges repeatedly, and
  historical data is immutable, so it caches perfectly.

## Failure modes to have answers for

```
  Device offline 30 min       -> WAL replay on reconnect, jittered, rate-limited
                                 so 5,000 devices reconnecting do not melt ingest
  Ingest node dies            -> shard reassigned; in-flight batches retried;
                                 idempotency makes duplicates harmless
  Storage slow                -> credits withdrawn, backpressure to the edge
  Fleet-wide event            -> every device reports at once: the peak case.
                                 Size for it or shed by priority
  Clock skew on a device      -> timestamps in the future or far past. Clamp,
                                 count, and alert — do not silently accept
  Cardinality explosion       -> a bad label (per-client-MAC!) multiplies series
                                 count by 1000. THE classic metrics outage.
```

**Cardinality explosion deserves emphasis.** It is the single most common way
real metrics systems fall over: someone adds a label with unbounded values and
the series count explodes. Defences: a per-device series limit enforced at
ingest, cardinality alerting, and rejecting new series rather than degrading for
everyone. Raising this unprompted is a strong Staff signal because it is a
failure you only know about from having lived it.

## Interview angles

**"Design a telemetry system for 50,000 devices."**
Arithmetic first, bottleneck named, then the five stages each justified in a
sentence, then backpressure, then the failure table.

**"What happens at 2x overload?"**
Credits withdrawn, edge buffers, shed by priority with a stated policy. Never
"add more machines."

**"Exactly once?"**
Not achievable in general. At-least-once transport plus an idempotent write keyed
on (device, series, ts) gives effectively-once, which is what people actually
mean.

**"How do you handle a device that has been offline for a day?"**
Bounded WAL with a drop policy, jittered reconnect, rate-limited replay so the
herd does not overwhelm ingest.

**"Why shard by device and not by metric?"**
Containment of per-device bursts and WAL replay. Then name what it costs you.

## Exercises

1. Write the full sizing for your own fleet at current scale and at 10x. Name the
   bottleneck at each.
2. Build the device agent: SPSC ring, bounded WAL, jittered reporting, drop
   policy with a counter. Test it against a simulated 30-minute disconnect.
3. Implement credit-based backpressure between two processes and demonstrate that
   a slow consumer causes the producer to buffer and then shed, with no unbounded
   growth anywhere.
4. Simulate the reconnect herd: 5,000 agents come back at once. Measure ingest
   latency with and without jittered replay.
5. Cause a cardinality explosion in your own test system and implement the ingest
   limit that prevents it. Write up the detection signal you would alert on.
