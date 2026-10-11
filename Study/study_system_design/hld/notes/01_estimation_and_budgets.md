# 01 — Estimation and latency budgets

> No prerequisites. This is the first three minutes of every design round, and
> the thing that most separates Senior from Staff answers.

## Why this comes first

A design round is not a test of whether you know what Kafka is. It is a test of
whether you can **bound the problem before solving it**. Candidates who start
drawing boxes are doing architecture theatre. Candidates who start with
arithmetic are doing engineering.

The arithmetic takes ninety seconds and it determines everything after it: how
many machines, whether it fits in memory, whether you need a queue at all.

## The numbers to know cold

Learn these as **orders of magnitude and ratios**, not exact figures. Verify the
ones you can on your own machine (tutorials `os_and_arch/02` and `07` had you
measure several already).

| Operation | Time | Ratio to L1 |
| --- | --- | --- |
| L1 cache hit | ~1 ns | 1x |
| Branch mispredict | ~5 ns | 5x |
| L2 hit | ~4 ns | 4x |
| L3 hit | ~20 ns | 20x |
| Main memory | ~80 ns | 80x |
| TLB miss + walk | ~25 ns | 25x |
| Syscall | ~100-400 ns | 100x+ |
| Context switch | ~1-3 us | 1,000x |
| SSD random read | ~50-100 us | 50,000x |
| Same-DC network round trip | ~0.5 ms | 500,000x |
| Cross-continent round trip | ~100-150 ms | 100,000,000x |
| Disk (spinning) seek | ~5-10 ms | 5,000,000x |

Throughputs worth knowing:

```
  Memory bandwidth        ~20-100 GB/s per socket
  NVMe SSD                ~3-7 GB/s, ~500k-1M IOPS
  10 GbE                  ~1.25 GB/s, ~14.8 Mpps at 64 B frames
  100 GbE                 ~12.5 GB/s
  Single core, simple op  ~1 G ops/s
```

The two ratios that do most of the work in a design round:

- **Memory is 100x slower than cache; network is 10,000x slower than memory.**
  So a design that turns a memory access into a network call has made it 10^6
  times worse, and that is usually the whole argument about whether to shard.
- **A cross-region round trip is 100 ms.** Any design with a synchronous
  cross-region dependency per request has a 100 ms floor. Say that immediately.

## The estimation procedure

Do it in this order, out loud, every time:

```
  1. Requests or events per second       (peak, not average — get the ratio)
  2. Bytes per request or event
  3. => bandwidth
  4. Retention period
  5. => storage, before and after compression
  6. Working set that must be in memory
  7. => machine count, from the binding resource
  8. Name the bottleneck resource explicitly
```

Worked example, and it is the one you should have memorised because it is your
own domain:

```
  50,000 access points x 200 metrics / 10 s interval
     = 1,000,000 samples/s

  x 16 B encoded (timestamp delta + value + series id)
     = 16 MB/s  =  1.4 TB/day raw

  delta-of-delta timestamps + XOR float compression + zstd
     -> roughly 1.3-2 bytes per sample in practice
     = ~150 GB/day stored, ~4.5 TB/month at 30-day retention

  Working set: last 1 h at full resolution
     = 3.6 G samples x ~2 B = ~7 GB  -> fits in memory on one large node

  Binding resource: write throughput to the store, not CPU and not network.
```

That last line is the Staff move. **Name the bottleneck resource.** Everything
you propose afterwards should be justified by its effect on that resource.

## Peak versus average

Always ask for the ratio. Common shapes:

- Consumer traffic: peak is 2-3x daily average, with a predictable diurnal curve.
- Business tools: 5-10x, concentrated in working hours in one or two time zones.
- Telemetry from a fleet: nearly flat, **unless** devices sync their reporting
  clocks — in which case you get a thundering herd every interval. The fix is
  jitter on the device, and raising it unprompted is a nice detail from real
  operational experience.
- Incident-driven: a fleet-wide event makes every device report at once. Design
  for it, because that is exactly when you need the data.

## Latency budgets

Decompose a target end to end and see what is left:

```
  Target: p99 200 ms for a fleet dashboard query

    client -> edge                       20 ms   (real-world RTT, not LAN)
    edge   -> API                         1 ms
    API auth + authz                      5 ms
    query planning                        2 ms
    storage read (the real work)          ?
    serialisation + transfer             15 ms
    client render                        40 ms
    ------------------------------------------
    leaves ~115 ms for the storage read
```

Now the storage design has a number to hit, and every decision is checkable
against it. This is a far stronger structure than "we'll use a time-series
database."

Two facts to state about tails:

- **Tail latency amplifies with fan-out.** If a request touches 10 shards and
  each has p99 = 10 ms, the chance all ten are fast is 0.99^10 ≈ 0.90 — so your
  request's p90 is now 10 ms and its p99 is much worse. Fixes: hedged requests,
  fewer shards per request, or tail-tolerant replication.
- **Averages lie.** Always ask for p50, p99 and p99.9. A system with a 5 ms mean
  and a 2 s p99.9 is broken for 1 user in 1000, and at 1 M requests that is 1000
  angry users.

## Cost, because Staff-level means owning the budget

Rough public-cloud orders of magnitude — check current pricing, but the ratios
are stable:

```
  Compute      ~$0.04/vCPU-hour           => ~$30/vCPU-month
  Memory       ~$4/GB-month
  Block SSD    ~$0.10/GB-month
  Object store ~$0.02/GB-month            (5x cheaper than block)
  Egress       ~$0.05-0.09/GB             <- usually the surprise
  Intra-AZ     often free; cross-AZ is not
```

For the telemetry example: 150 GB/day at 30-day retention is ~4.5 TB. On block
storage ~$450/month; on object storage ~$90/month. That single choice is a 5x
cost difference, which is why tiering hot and cold data is a real architectural
decision rather than a detail.

**Egress is the classic trap.** A design that ships raw telemetry from edge to
cloud without aggregation can cost more in egress than in everything else
combined. Pointing that out is a strong Staff signal because it shows you think
about the bill.

## Interview angles

**"Design X for Y users."**
Do the arithmetic first, out loud, and name the bottleneck resource before
drawing anything.

**"How many machines?"**
Divide the binding resource by per-machine capacity, then add headroom for peak
and for failure. Say what headroom you chose and why.

**"What's your p99 target and can you hit it?"**
Decompose the budget. Show the remainder.

**"What does this cost to run?"**
Do the storage and egress arithmetic. Most candidates never mention cost, and
owning it is explicitly a Staff-level expectation.

**"What breaks at 10x?"**
Re-run the arithmetic with a 10x multiplier and find which resource saturates
first. This question is asked constantly and the arithmetic answers it directly.

## Exercises

1. Memorise the latency table by ratio, not by number. Test yourself by writing
   it from memory once a week.
2. Do the full estimation for three systems in 5 minutes each: a URL shortener at
   100 M links, a chat system at 10 M DAU, and your own Wi-Fi fleet at 10x its
   current size. Name the bottleneck for each.
3. Take a service you actually work on, decompose its latency budget, and compare
   against measured p50/p99/p99.9. Write down where the budget and reality
   diverge — that gap is an excellent interview story.
4. Compute the fan-out amplification for a request touching 5, 20 and 100 shards
   at p99 = 10 ms each. Then compute what hedging at p95 buys you.
5. Price your own fleet's telemetry three ways: all block storage, tiered
   hot/cold, and aggregated-at-edge. Present the three numbers. This is exactly
   the Capstone 3 cost model.
