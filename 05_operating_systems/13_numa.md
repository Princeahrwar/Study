# OS 13 — NUMA

> Prerequisite: `04_computer_organization/19_cache_coherence.md`, `12_virtual_memory_and_tlb.md`.
> Relevant to: any server-side role. Less so on a single-socket AP, but it is
> standard interview material for Google, Meta, Nvidia and Microsoft.

## What NUMA is

On a multi-socket machine (and on some large single-socket parts with multiple
memory controllers), memory is physically attached to a particular socket.
Accessing your own socket's memory is *local*; accessing the other socket's goes
across an interconnect (UPI on Intel, Infinity Fabric on AMD, CCIX/CXL variants
on ARM servers).

Typical penalty: **1.5-2.2x latency**, and meaningfully lower bandwidth. Not
catastrophic per access, which is exactly why NUMA problems hide — they look like
"everything is 40% slower" rather than an obvious stall.

```bash
numactl --hardware        # nodes, sizes, and the distance matrix
lscpu | grep -i numa      # which CPUs are on which node
numastat -m               # per-node memory usage
```

The distance matrix is a relative cost table where 10 means local. A value of 21
means roughly 2.1x. Read it before you tune anything.

## The first-touch rule

This is the single most important NUMA fact, and it surprises people:

**A page is allocated on the node of the thread that first *touches* it — not the
thread that called `malloc`.**

`malloc` only reserves address space. The physical page appears on first write.
So this common pattern is a NUMA disaster:

```cpp
// WRONG on a NUMA machine.
// The main thread touches every page during init, so ALL memory lands on node 0.
// Workers on node 1 then make every access remote, forever.
std::vector<float> data(N);
std::fill(data.begin(), data.end(), 0.0f);      // main thread touches everything

#pragma omp parallel for
for (size_t i = 0; i < N; ++i) process(data[i]);
```

```cpp
// RIGHT: let each worker first-touch the region it will own.
std::vector<float> data(N);                      // reserve only, do not touch

#pragma omp parallel for schedule(static)
for (size_t i = 0; i < N; ++i) data[i] = 0.0f;   // parallel first touch

#pragma omp parallel for schedule(static)
for (size_t i = 0; i < N; ++i) process(data[i]); // same partitioning => local
```

The two loops must use the **same schedule**, otherwise thread k initialises one
range and processes another. That detail is where people get this wrong in
practice.

## Explicit control

```cpp
#include <numa.h>
#include <numaif.h>

// Allocate explicitly on a node.
void* buf = numa_alloc_onnode(size, node);
// ... use ...
numa_free(buf, size);

// Or bind an existing region.
unsigned long nodemask = 1UL << node;
mbind(addr, len, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, 0);

// Pin the calling thread to a node's CPUs so first-touch does the right thing.
struct bitmask* cpus = numa_allocate_cpumask();
numa_node_to_cpus(node, cpus);
numa_sched_setaffinity(0, cpus);
```

From the shell, for whole processes:

```bash
numactl --cpunodebind=0 --membind=0 ./app        # strict: fail rather than go remote
numactl --cpunodebind=0 --preferred=0 ./app      # soft: prefer node 0, spill if full
numactl --interleave=all ./app                   # spread evenly — good for shared read-only data
```

`--interleave` is the right answer more often than people expect. For a large
structure read by threads on every node — a routing table, a shared cache — 
interleaving gives every node the same average cost instead of making one node
fast and the rest slow. Predictable beats locally-optimal when access is uniform.

## The architecture that avoids the problem

The robust design is **shared-nothing per node**, and it generalises far beyond
NUMA:

```
   per-NUMA-node:
     - its own worker threads, pinned to that node's cores
     - its own memory pools, first-touched by those threads
     - its own NIC receive queues (set IRQ affinity to that node)
     - its own shard of the data
   cross-node traffic: only aggregated results, rarely
```

This is exactly how DPDK applications, Seastar/ScyllaDB, and modern high-
throughput proxies are structured. The NUMA question and the "how do you scale
past 8 cores" question have the same answer: **partition so that sharing is rare,
then make the rare sharing explicit.**

For a NIC specifically, the full path matters: the device is attached to one
socket's PCIe root complex, so DMA lands in that socket's memory. Put the
handling threads there. `cat /sys/class/net/eth0/device/numa_node` tells you which.

## Diagnosing

```bash
numastat -p <pid>                       # local vs remote allocation per process
perf stat -e node-loads,node-load-misses ./app
perf c2c record -a                      # also shows remote HITM
```

The symptom pattern to recognise: throughput scales fine up to the core count of
one socket, then goes flat or *degrades* when threads spill onto the second
socket. If you see that shape, check NUMA before anything else.

## AMD chiplets and sub-NUMA clustering

Modern AMD EPYC and Intel server parts expose NUMA-like structure *within* a
socket — CCXs/CCDs on AMD, sub-NUMA clustering (SNC) on Intel. Cores in different
chiplets have different L3 and different memory-controller affinity, so
"single-socket therefore no NUMA" is no longer safe. `numactl --hardware` on a
modern EPYC often shows multiple nodes for one physical socket.

The practical consequence: the shared-nothing structure above is worth adopting
even on single-socket servers.

## Interview angles

**"What is NUMA and when does it matter?"**
Give the 1.5-2x figure, then immediately go to first-touch, because that is where
real bugs live.

**"Your app scales to 16 cores then stops. Ideas?"**
NUMA crossing, coherence on a shared line, or a lock. Say how you would
discriminate: `numastat` for the first, `perf c2c` for the second, `perf lock` or
off-CPU profiling for the third.

**"How do you lay out a 100 GB in-memory index across two sockets?"**
Shard it and pin threads, if access is partitionable by key. Interleave it, if
every thread touches everything. Say which question decides between them.

**"Where should the NIC IRQs go?"**
The node whose PCIe root complex the NIC hangs off. Show you know to check
`/sys/class/net/*/device/numa_node`.

## Exercises

1. Run `numactl --hardware` on any multi-socket machine you can reach and read
   the distance matrix. Convert it to an expected latency ratio.
2. Write the wrong and right first-touch versions above. Measure both with
   threads pinned across two nodes. Confirm the gap, then confirm the cause with
   `numastat -p`.
3. Benchmark a large read-only lookup table under `--membind=0` versus
   `--interleave=all`, with threads spread across both nodes. Explain which wins
   and why.
4. Find which NUMA node your NIC is on. Move its IRQs to the *wrong* node with
   `/proc/irq/*/smp_affinity` and measure the throughput difference. Put it back.
5. On a modern AMD or recent Intel server, check whether `numactl --hardware`
   reports more nodes than sockets. Write a paragraph on what that means for how
   you would pin a data-plane application.
