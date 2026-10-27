# CO 29: Interconnection Networks and Warehouse-Scale Computing

*Part of the [Computer Organization plan](00_plan.md) · Optional tutorial · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: [tutorial 19](19_cache_coherence.md) (section 3: directory protocols and their messages), [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) (section 10: NVLink and all-reduce), [tutorial 25](25_buses_pcie_and_io_mapping.md) (section 3.4: packets and credits), [tutorial 28](28_performance_and_power.md) (sections 6 and 9: Little's law, the tail, power)*

By the end of this tutorial you can:

- model the cost of a message as a fixed delay plus size over bandwidth, fit the two numbers from a ping-pong, and say what the half-bandwidth size means;
- describe the standard topologies (bus, ring, mesh, torus, hypercube, tree, fat tree, butterfly, crossbar, Clos, dragonfly) and compute degree, diameter, average distance, bisection width and link count for each;
- size a k-ary fat tree (hosts, switches, links, equal-cost paths) and a leaf-spine network, and compute an oversubscription ratio;
- route a message by dimension order, explain what oblivious (Valiant), adaptive and ECMP routing buy and cost, and compute the load a traffic pattern puts on the busiest channel;
- compare store-and-forward, virtual cut-through and wormhole switching with latency formulas, and size a buffer from the credit round trip;
- find a deadlock in a channel dependency graph, fix it with dimension order, a dateline with two virtual channels or a turn restriction, and name the two deadlocks that routing alone does not solve (protocol deadlock, and PFC deadlock in Ethernet);
- explain head-of-line blocking (the 58.6 % limit) and virtual output queues;
- say why a chip moved from a ring to a mesh, what a data-centre network is built from, and why ECMP collides, incast collapses and RDMA wants a lossless fabric;
- do PUE and availability arithmetic, and say why a warehouse-scale computer is designed around failure, power and the tail.

Every earlier tutorial moved bits inside one machine. This one is about what happens when many things must talk to many things: cores on a chip, GPUs in a server, servers in a building. The same few ideas (a topology, a routing rule, a flow-control rule, a way to avoid deadlock) appear at every scale, with different numbers. For a Senior or Staff interview it is the tutorial behind questions such as "how would you connect 1,000 servers", "why is my all-reduce slow", "why does the tail latency of this service spike when a job starts", and "what does a data centre cost to run".

**Where the numbers come from.** The virtual machine of tutorials 14 to 28 (two vCPUs of an Intel Xeon under KVM, Linux 6.18) has no network hardware that I can measure: one virtual NIC (`eth0`), the loopback interface, and an egress path that goes only through an allowlist proxy, so there is no round-trip time to another host. **I measured no switch, no NIC and no data centre.** What I could measure is the cost of sending a message between two processes over the loopback interface (section 1.2), which is the fixed cost that a kernel network stack puts under every message before any wire is involved, and I reuse core-to-core, DRAM and wake-up timings from tutorials 14, 19, 25 and 26. Everything about topologies, routing and deadlock was **computed by short scripts** shown in the Answers: I built each topology as a graph and counted by breadth-first search (the bisection of the 16-node networks by exhaustive search over all balanced cuts), built k-ary fat trees for k = 4, 6 and 8 and counted their parts, summed the load that dimension-order routing puts on each channel of an 8 by 8 mesh, tested channel dependency graphs for cycles, simulated ECMP hashing and an input-queued switch, and did the PUE, incast and availability arithmetic. Everything about real networks and data centres is **published**, linked where used: Google's ["Jupiter Rising"](https://research.google/pubs/jupiter-rising-a-decade-of-clos-topologies-and-centralized-control-in-googles-datacenter-network/) (five generations of Clos networks) and its [data-centre efficiency page](https://datacenters.google/efficiency/) (PUE); Barroso, Hölzle and Ranganathan, [*The Datacenter as a Computer*](https://pages.cs.wisc.edu/~shivaram/cs744-readings/dc-computer-v3.pdf) (third edition, excerpt: racks, oversubscription, failures); Vasudevan et al., ["Safe and Effective Fine-grained TCP Retransmissions for Datacenter Communication"](https://www.cs.princeton.edu/courses/archive/fall10/cos597B/papers/tcpincast-sigcomm09.pdf) (incast); Karol, Hluchyj and Morgan, ["Input vs Output Queueing on a Space-Division Packet Switch"](https://web.stanford.edu/class/cs244/papers/InputvsOutput.pdf) (head-of-line blocking); Zhu et al., ["Congestion Control for Large-Scale RDMA Deployments"](https://www.microsoft.com/en-us/research/publication/congestion-control-for-large-scale-rdma-deployments/) (DCQCN) and Guo et al., ["RDMA over Commodity Ethernet at Scale"](https://www.microsoft.com/en-us/research/?p=317771); the [TPU v4 paper](https://arxiv.org/abs/2304.01433); WikiChip on the [Skylake-SP mesh](https://fuse.wikichip.org/news/1017/isscc-2018-intels-skylake-sp-mesh-and-floorplan/2); the structure of the fat tree from [lecture slides](https://cs.ucr.edu/~ztan/courses/CS204/f23/lec11_dcn.pdf) and the Wikipedia articles on the [fat tree](https://en.wikipedia.org/wiki/Fat_tree), [Clos networks](https://en.wikipedia.org/wiki/Clos_network), [torus interconnects](https://en.wikipedia.org/wiki/Torus_interconnect), [bisection bandwidth](https://en.wikipedia.org/wiki/Bisection_bandwidth), [wormhole switching](https://en.wikipedia.org/wiki/Wormhole_switching) and [networks on chip](https://en.wikipedia.org/wiki/Network_on_a_chip). I did not fetch the original fat-tree paper (Al-Fares, Loukissas and Vahdat, SIGCOMM 2008); I took its construction from the slides and confirmed every count by building the network in a script. The classic results (Clos on non-blocking networks, Valiant routing, Dally and Seitz on deadlock, Glass and Ni's turn model, Duato's escape channels) are quoted from the textbooks, and the ones that a script can check were checked. Where a figure is my own assumption (a switch latency, a price of electricity) the text says "assumed".

> **The one-sentence model.** A network is links, switches and buffers; a message pays a fixed delay plus its size over the bandwidth (`α + n/β`), the network's capacity is what its narrowest cut can carry (the bisection) divided by how much of the traffic the pattern forces across that cut, and almost every failure is waiting for a shared resource that someone else holds: a channel held while the next one is full (deadlock), a buffer that overflows (loss or back-pressure), or a link that two flows happened to hash onto (collision); a warehouse-scale computer is the same network one level up, with failure, power and the tail as the extra constraints.

---

## 1. The cost of sending a message

### 1.1 A fixed price and a per-byte price

Send `n` bytes between two endpoints and the time is, to a good first approximation,

```
T(n) = α + n / β
```

`α` (alpha) is the **latency**: what even an empty message costs (software, the first bit crossing the wires and switches, waking the receiver). `β` (beta) is the **bandwidth** of the path in bytes per second. Two consequences carry through the whole tutorial.

- **The half-bandwidth size.** The effective bandwidth is `n / T(n)`. It equals `β / 2` when `n = α × β`. Call that size `n_half`. Messages much smaller than `n_half` are dominated by `α` and waste the link; messages much larger approach `β`. With `α` = 5 µs and `β` = 10 GB/s, `n_half` = 50,000 bytes: a 1 KB message gets 1000 / 5.1 µs = 196 MB/s, **2 % of the link**.
- **Batching is the cure for `α`.** Sending 1 MB as one message takes 5 + 100 = 105 µs. Sending it as a thousand 1 KB messages takes 1000 × 5.1 = 5,100 µs, **49 times longer**, on the same wire. Every technique in tutorial 26 (interrupt moderation, `sendmmsg`, `io_uring`, doorbell batching) and every network library that "coalesces small writes" is this arithmetic.

The model is a first approximation. Real paths have several regimes (a different `β` when the data no longer fits a cache, a protocol that changes strategy at some size), as the next section shows.

### 1.2 Measured: a TCP ping-pong on loopback

Two processes, a client and a server, are pinned to chosen CPUs. The client sends `n` bytes over a TCP connection on `127.0.0.1`; the server reads them all and sends them back; the client reads them all. One-way time is half the round trip, taken as the median of 15 blocks of many round trips (the program is in the Answers, Problem A). `TCP_NODELAY` is set so the stack does not wait to coalesce small writes.

| Bytes | Same CPU, one-way µs | Different CPUs, one-way µs | Different CPUs, MB/s each way |
|---:|---:|---:|---:|
| 8 | 5.5 | 17.4 | 0.5 |
| 64 | 5.3 | 17.2 | 3.7 |
| 256 | 5.3 | 16.3 | 16 |
| 1,024 | 5.2 | 15.8 | 65 |
| 4,096 | 5.7 | 17.1 | 240 |
| 16,384 | 6.2 | 19.6 | 835 |
| 65,536 | 11.1 | 28.7 | 2,281 |
| 262,144 | 33.4 | 48.4 | 5,420 |
| 1,048,576 | 203 | 336 | 3,120 |

Each cell is the mean of two runs (for "different CPUs": client on CPU 0 and server on CPU 1, then the reverse; for "same CPU": two runs on CPU 0). The two runs differed by less than 10 % in all but three same-CPU cells (16 KiB: 12 %, 256 KiB: 16 %, 1 MiB: 11 %), so read the same-CPU large sizes as rough.

Fitting `α + n/β`: **`α` = 16.8 µs** between two CPUs (the median of the sizes up to 1 KiB) and **5.3 µs** on one CPU. Between 16 KiB and 256 KiB the points lie close to a line of slope 0.112 ns per byte, so **`β` is about 8.9 GB/s** (9.0 on one CPU). That gives `n_half` = 16.8 µs × 8.9 GB/s = **150 KB** between CPUs and 47 KB on one. Fitting each run on its own (three between CPUs, three on one CPU, the last pair from re-running the program printed in the Answers) gives `α` of 16.2 to 17.0 µs (5.3 to 5.4 on one CPU), `β` of 8.2 to 9.8 GB/s (8.1 to 10.5 on one CPU) and `n_half` of 139 to 165 KB (43 to 56 KB on one CPU), so read `β` as 9 GB/s give or take one. Four observations.

1. **The fixed cost is software, not wire.** There is no wire. Sending 8 bytes costs 17 µs, which is about 300 times the 56 to 60 ns of a cache line moving between two cores ([tutorial 26](26_interrupts_exceptions_and_dma.md) section 5.2). A message passes through a system call, a copy into a kernel buffer, the TCP/IP code, the scheduler, a wake-up of the receiving process, a copy out and a second system call.
2. **Where the processes run changes `α` by a factor of 3.2.** On different CPUs the receiver is asleep and must be woken by an inter-processor interrupt (a VM exit in this guest, [tutorial 27](27_hardware_virtualization.md)), the same mechanism that made a futex hand-off cost 10 µs across CPUs and 1.7 µs on one in tutorial 26. Placement is part of a network's latency.
3. **The model breaks at large sizes.** Above 256 KiB the slope is 0.366 ns per byte between CPUs (2.7 GB/s) and 0.216 on one CPU (4.6 GB/s). The straight line from the middle of the range predicts 134 µs for 1 MiB; the measurement is 336, **2.5 times more** (2.4 to 2.8 times in the three runs between CPUs). I did not isolate the cause. Candidates are the copies no longer fitting in the 2 MB L2 ([tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md); three 1 MiB buffers are live) and socket-buffer limits forcing the two sides to take turns. The lesson for the model is that **`β` is not a property of the link alone**: it depends on where the data lives and on buffer sizes.
4. **Loopback is not a network.** It shows what the endpoints cost. A kernel-bypass NIC removes most of it: tutorial 25 (section 3.6) quotes a published 128-byte loopback through such a NIC at about 1,000 ns, of which about 900 ns is the PCIe crossing. That is what RDMA and DPDK are for (section 7.5), and it is why a rack's worth of 400 ns switches is a small part of the latency of an ordinary RPC.

### 1.3 The latency ladder

Collect the timings from this track and add the speed of light.

| What | Time | Source |
|---|---:|---|
| L1 hit | 0.4 to 1.3 ns | measured, tutorials 25 and 14 |
| A cache line moved from another core (spin hand-off) | 56 to 102 ns | measured, tutorials 26 and 19 |
| DRAM miss | 146 to 149 ns (221 ns with a TLB miss) | measured, tutorials 25 and 14 |
| A PCIe device reading 64 B of host memory | 547 ns | published, tutorial 25 |
| A kernel-bypass NIC's 128 B loopback | about 1,000 ns | published, tutorial 25 |
| One cut-through switch, port to port | 400 ns | **assumed** for the examples below |
| Waking a sleeping thread: same CPU, other CPU | 1.6 to 1.8 µs, 10 µs | measured, tutorial 26 |
| TCP loopback, 8 bytes, one way: same CPU, other CPU | 5.3 µs, 16.8 µs | measured, this tutorial |
| Light in fibre | **4.9 µs per km** (100 m is 0.49 µs) | computed: `c` divided by an assumed refractive index of 1.47 |

Each rung is roughly ten times the one above, and the lesson depends on where you stand. **Inside a chip, hops decide**: a few cycles per router, so topology (section 2) is the design. **Between servers, software decides** unless it is bypassed: five switches at 400 ns are 2 µs, a tenth of the 17 µs this VM's stack adds. **Between buildings, distance decides**: 10 km is 49 µs one way no matter what is built. And in every case **queueing decides the tail** (sections 4 to 8), which no table of minimum latencies shows.

---

## 2. Topologies and the numbers that describe them

A **topology** is the graph: which nodes (cores, chips, servers, switches) are joined by which links. The network designer's choices are the topology, the **routing** rule that picks a path (section 3) and the **flow control** that decides when a packet may advance (section 4).

### 2.1 Six numbers

For a network of `N` terminals:

- **Degree**: links per node (the router's port count; cost and wiring).
- **Diameter**: the longest shortest path, in hops (worst-case latency).
- **Average distance**: the mean shortest path over all pairs (typical latency under uniform traffic).
- **Link count**: the cost in wires and ports.
- **Bisection width**: the smallest number of links whose removal splits the network into two halves of `N/2` terminals each. Multiplied by the link rate it is the **bisection bandwidth**. It is the capacity for traffic where half the nodes talk to the other half, the worst case for any all-to-all pattern, and it is the number marketing and papers quote.
- **Path diversity**: how many shortest paths a pair has (for load balancing and fault tolerance).

A **direct** network has a router at every terminal (ring, mesh, torus, hypercube); an **indirect** network has terminals at the edge and separate switches inside (crossbar, butterfly, fat tree, Clos). Compare: a bus has degree 1, diameter 1, bisection 1 (everything shares one medium) and one transfer at a time; a full crossbar has `N²` crosspoints, diameter 1, bisection `N/2` and no sharing.

### 2.2 Direct networks, computed

The script in the Answers (Problem B) builds each graph, finds distances by breadth-first search and finds the bisection by trying every balanced cut (feasible at `N` = 16).

**N = 16** (bisection by exhaustive search):

| Topology | Degree | Links | Diameter | Average distance | Bisection |
|---|---:|---:|---:|---:|---:|
| Ring | 2 | 16 | 8 | 4.27 | 2 |
| 4 × 4 mesh | 4 | 24 | 6 | 2.67 | 4 |
| 4 × 4 torus | 4 | 32 | 4 | 2.13 | 8 |
| Hypercube (2⁴) | 4 | 32 | 4 | 2.13 | 8 |

**N = 64** (bisection from the closed forms, which the N = 16 search confirms):

| Topology | Degree | Links | Diameter | Average distance | Bisection |
|---|---:|---:|---:|---:|---:|
| Ring | 2 | 64 | 32 | 16.25 | 2 |
| 8 × 8 mesh | 4 | 112 | 14 | 5.33 | 8 |
| 8 × 8 torus | 4 | 128 | 8 | 4.06 | 16 |
| 4 × 4 × 4 torus | 6 | 192 | 6 | 3.05 | 32 |
| Hypercube (2⁶) | 6 | 192 | 6 | 3.05 | 32 |

The closed forms: a `k × k` mesh has diameter `2(k - 1)` and bisection `k`; wrapping the edges to make a torus halves the diameter to `k` and doubles the bisection to `2k`; a hypercube of `N = 2^d` nodes has degree and diameter `d` and bisection `N/2`; a ring has diameter `N/2` and bisection 2. The average distance of a ring is `N² / (4(N - 1))`, and for a `k × k` mesh exactly `2k / 3` (8 by 8: 5.33).

What the tables say:

- **A 4 × 4 torus is a 4-dimensional hypercube.** A ring of 4 is a square, the product of two 2-node rings, so the 4 × 4 torus and the 2⁴ cube are the same graph (same row in the table). The families differ only as `N` grows.
- **Bisection per node falls as the network grows, unless the dimension grows too.** The ring's is 2 for any `N`; the 2D mesh's is `√N`; only the hypercube keeps `N/2`. At `N` = 64 a ring offers 2 links to 32 nodes that all want to talk across (a sixteenth of a link each); the 8 × 8 torus gives 16 links (half a link each); the 6-dimensional networks give a full link each. Dimension is bought with degree: the hypercube's router grows with `log N` ports, and its wires get long.
- **Wrap-around costs wire length.** In the flat picture the wrap links of a torus are as long as the whole chip or board; a "folded" layout interleaves the nodes so that every link is two hops long physically. The saving (half the diameter, twice the bisection) is real, the price is longer wires and one more deadlock problem (section 5).
- **Dimension is a trade, not a rule.** With a fixed number of wires per node, a low-degree network can give each link more bits (wider links), while a high-degree one has more, narrower links and a shorter diameter. That is why small on-chip networks are 2D meshes (section 6) and accelerator pods are 3D tori (section 2.5), not hypercubes.

### 2.3 Indirect networks

**Crossbar.** Every input has a wire to every output through a crosspoint, `N²` of them. Non-blocking (any permutation at once), one hop, no routing decision, and a cost that grows as `N²`. A 64-port crossbar has 4,096 crosspoints. It is the inside of most switch chips, and it is why "a switch with `N` ports" is a different thing from "a network of `N` nodes".

**Butterfly (k-ary n-fly).** `N = k^n` terminals, `n` stages of `k^(n-1)` switches, each `k × k`. A 4-ary 3-fly has 64 terminals and 3 × 16 = 48 switches of size 4 × 4, with 2 × 64 = 128 links between stages. It is cheap, every path crosses exactly `n` switches, but there is **exactly one path per source and destination**: if two flows need the same inter-stage link, one waits, and nothing offers a detour. That is its weakness and the reason designs that need load balancing add stages or choose a Clos.

**Tree and fat tree.** A binary tree has bisection 1: all cross-half traffic goes through the root, so the root link is the bottleneck (the problem in a campus network with one uplink). A **fat tree** makes links fatter toward the root, or equivalently uses many parallel links and switches at each level, so that the capacity above each level equals the capacity below it. Then bisection is `N/2` links of the host rate: every host can talk to a host in the other half at full speed at once. A data-centre fat tree is built from identical switches rather than special fat links (section 2.4).

**Clos network.** Charles Clos (1953) built a telephone switch from three stages: `r` ingress switches of `n` inputs each, `m` middle switches and `r` egress switches. It is **strictly non-blocking** (a new call can always be added without disturbing the existing ones) when `m ≥ 2n - 1`, and **rearrangeably non-blocking** (any set of calls can be carried if existing ones may be re-routed) when `m ≥ n`; the middle stage can itself be a Clos network, recursively. Folding a Clos network over, so that the ingress and egress switches are the same boxes, gives the leaf-spine and fat-tree networks of data centres ([Wikipedia: Clos network](https://en.wikipedia.org/wiki/Clos_network)).

### 2.4 The data-centre fat tree, counted

The k-ary fat tree, built from identical `k`-port switches:

- there are `k` **pods**; each has `k/2` **edge** switches and `k/2` **aggregation** switches;
- each edge switch has `k/2` ports down to hosts and `k/2` up to the pod's aggregation switches (every edge switch to every aggregation switch of the pod);
- there are `(k/2)²` **core** switches, in `k/2` groups of `k/2`; aggregation switch number `i` of every pod connects to the `k/2` core switches of group `i`.

I built this network in a script for `k` = 4, 6 and 8 and counted:

| k | Hosts | Edge + aggregation + core switches | Links | Shortest paths, same edge switch | Same pod | Different pods |
|---:|---:|---:|---:|---:|---:|---:|
| 4 | 16 | 8 + 8 + 4 = 20 | 48 | 1 (2 hops) | 2 (4 hops) | 4 (6 hops) |
| 6 | 54 | 18 + 18 + 9 = 45 | 162 | 1 | 3 | 9 |
| 8 | 128 | 32 + 32 + 16 = 80 | 384 | 1 | 4 | 16 |

The pattern gives the formulas, which then extend to sizes too big to build:

```
hosts                  k^3 / 4
switches               5 k^2 / 4      (k^2 edge + aggregation, (k/2)^2 core)
links                  3 k^3 / 4      (host-edge, edge-aggregation, aggregation-core: k^3/4 each)
equal-cost paths       1 (same edge), k/2 (same pod), (k/2)^2 (different pods)
bisection              hosts / 2 links at the host rate (full bisection)
```

| k | Hosts | Switches | Core switches / paths between pods |
|---:|---:|---:|---:|
| 16 | 1,024 | 320 | 64 |
| 32 | 8,192 | 1,280 | 256 |
| 48 | 27,648 | 2,880 | 576 |
| 64 | 65,536 | 5,120 | 1,024 |

Three things to remember: **commodity 64-port switches connect 65,536 hosts in three tiers** (about 0.08 switches per host); **a flow between pods has `(k/2)²` equal-cost paths**, which is what ECMP (section 3.5) spreads traffic over; and **a failure removes a fraction of the paths, not connectivity**.

**Leaf-spine** is the two-tier case: `R`-port switches as leaves with `R/2` ports down and `R/2` up, and `R/2` spines; hosts `R²/2`, switches `3R/2`. With `R` = 64: 2,048 hosts and 96 switches, every pair of leaves joined by 32 paths (one through each spine), at most 3 switch hops. Three tiers give `R³/4` hosts.

**Oversubscription** is the ratio of capacity below a switch to capacity above it. Barroso et al. give the usual example: a 48-port rack switch connects 40 servers to 8 uplinks, **5 to 1**; if every server sends off the rack at once, each gets one fifth of its line rate. They explain the choice: a switch with ten times the bisection bandwidth often costs about a hundred times more, so racks are oversubscribed and software places communicating work close together. A full-bisection fat tree has a ratio of 1 to 1 at every tier, and costs for it.

### 2.5 Other topologies you should be able to name

- **Dragonfly** (Kim, Dally, Scott, Abts, ISCA 2008, [Google Research page](https://research.google/pubs/technology-driven-highly-scalable-dragonfly-topology/)): routers are grouped; inside a group every router is linked to every other; between groups there are a few long **global** links. With high-radix routers, any pair is within three hops (local, global, local), and the expensive long (optical) links are used sparingly. Proposed for large supercomputers.
- **Torus for ML.** The [TPU v4 paper](https://arxiv.org/abs/2304.01433) describes a supercomputer of **4,096 chips** joined by **optical circuit switches** that can reconfigure the topology; users can choose a "twisted 3D torus"; the optical switches are under 5 % of system cost and under 3 % of system power. As an illustration of the arithmetic only (a shape I chose, not a statement about which shapes the system offers): 4,096 chips wired as a 16 × 16 × 16 torus have 12,288 links, a diameter of 24 hops, an average distance of 12.0 hops and a bisection of 2 × 16 × 16 = **512 links**.
- **A hierarchy, in practice.** On a chip, a ring or mesh (section 6). Inside a server, point-to-point links and a switch (NVLink and NVSwitch, tutorial 23). Between servers in a building, a Clos (section 7). In a supercomputer or ML pod, a torus or dragonfly. The topology follows the scale, the traffic and the cost of wires at that scale.

---

## 3. Routing

### 3.1 Choosing a path

A **routing algorithm** picks the path from source to destination. The families:

- **Deterministic**: the path depends only on the source and destination (dimension order, below). Simple and keeps packets in order, and blind to load.
- **Oblivious**: a random choice that ignores the network's state (Valiant's random intermediate node, ECMP hashing).
- **Adaptive**: the path depends on the state, usually queue lengths or free credits, so a congested port is avoided.
- **Minimal** routing uses only shortest paths; **non-minimal** may take longer ones to get around congestion.

What a routing algorithm must deliver: low latency, good load balance for every traffic pattern (not only the friendly ones), freedom from deadlock (section 5), in-order delivery where the protocol needs it, and hardware simple enough to run at link speed.

### 3.2 Dimension-order routing, and the load it creates

On a mesh, **dimension-order routing** (XY routing) corrects the X coordinate first, then Y. Route a message from (1, 6) to (5, 2) in an 8 by 8 mesh: east to (2,6), (3,6), (4,6), (5,6), then south to (5,5), (5,4), (5,3), (5,2). Eight hops, the Manhattan distance. The router needs two comparisons to choose the output; every pair has one path, so packets arrive in order; and, as section 5 shows, it cannot deadlock on a mesh.

To compare routing algorithms, count the **channel load**: if every node injects one flit per cycle in total according to some traffic pattern, how many flits per cycle does the busiest directed channel carry? A channel can carry 1, so the network **saturates** when the busiest channel reaches 1, and the saturation throughput per node is `1 ÷ (busiest load)` (at most 1). A script (Answers, Problem B) routes every flow of three patterns on the 8 by 8 mesh and sums the load on each channel:

| Traffic pattern | Busiest channel load | Saturation throughput per node | Average hops |
|---|---:|---:|---:|
| Uniform random (each node to a random other node) | 2.03 | **0.49** | 5.33 |
| Transpose, `(x, y)` to `(y, x)` | 7.00 | **0.14** | 5.25 |
| Bit complement, `(x, y)` to `(7 - x, 7 - y)` | 4.00 | **0.25** | 8.00 |

Why these numbers.

- **Uniform random** is limited by the bisection: a node sends about 32/63 of its traffic to the other half, so 32 nodes push 32 × 0.508 = 16.3 flits per cycle through the 8 channels that cross the middle in each direction: 16.3 / 8 = 2.03. A mesh cannot give each node more than half a link under uniform traffic.
- **Bit complement** sends every packet across the middle, so all 32 nodes of one half use the 8 channels: 32 / 8 = 4.
- **Transpose** is the dimension-order trap. The seven nodes of the top row (`y` = 7) send to the seven nodes of the right column, so all seven route east along row 7 and then south down column 7: the channel into the corner carries seven flows. Seven flows on one channel is a load of 7 and a throughput of 1/7. The bisection alone would allow 0.5 (16 flows per direction over 8 channels); the routing wastes most of it.

### 3.3 Valiant: pay twice to be safe

**Valiant routing** sends each packet first to a *random* intermediate node, then to its destination (each phase by dimension order). Any traffic permutation then looks like two phases of uniform random traffic, so the busiest channel carries twice the uniform load: 4.06 on this mesh, for **every** permutation, a throughput of **0.25**.

| Pattern | Dimension order | Valiant |
|---|---:|---:|
| Uniform random | 0.49 | 0.25 |
| Transpose | 0.14 | 0.25 |
| Bit complement | 0.25 | 0.25 |

Valiant raises the worst case (transpose: 1.7 times better) and lowers the best (uniform: half) because it doubles every path. It is a *guarantee*, not an optimisation. The idea survives in practice as "route minimally when the network is quiet, non-minimally when it is not" (adaptive routing, next), and as the random-spreading step in some data-centre fabrics.

### 3.4 Adaptive routing

An adaptive router chooses among the allowed output ports by looking at queue occupancy or credits, so it spreads load and routes around hot spots and failed links. Three costs. It needs a way to avoid deadlock that does not forbid its freedom (an escape channel, section 5.3). It can **reorder** packets of one flow, which breaks protocols that assume order (TCP reads reordering as loss, and RDMA transports typically assume in-order delivery). And a decision made on local information can be wrong globally: two routers can each send traffic to a port that looks empty and fill it together.

### 3.5 In the data centre: ECMP

A fat tree has many equal-cost paths (section 2.4). Switches use **equal-cost multipath (ECMP)**: hash the flow's identifiers (source and destination addresses, protocol, source and destination ports) and use the result to pick one of the equal-cost uplinks. It is stateless, cheap, and keeps each flow on one path, so packets stay in order. It balances well when there are many flows and badly when there are few big ones, because a hash does not look at flow size.

A simulation (Answers, Problem D): `F` flows of equal size are hashed at random onto 8 equal uplinks (100,000 trials each).

| Flows | Uplinks in use | Busiest uplink carries (flows) | Fair share (flows) | Busiest ÷ fair |
|---:|---:|---:|---:|---:|
| 8 | 65.7 % | 2.59 | 1 | **2.59** |
| 16 | 88.2 % | 4.20 | 2 | 2.10 |
| 64 | 100 % | 12.24 | 8 | 1.53 |
| 800 | 100 % | 114.5 | 100 | 1.14 |

It is the birthday problem. Throw 8 flows at 8 links: the chance that every link gets exactly one is `8! ÷ 8⁸` = 0.24 %, so **in 99.76 % of cases at least one link carries two or more flows**; on average 2.75 of the 8 links carry nothing. A flow on the busiest link gets about 1/2.59 = 39 % of the rate it could have, while another uplink idles. Short flows ("mice") average out; long ones ("elephants": storage replication, shuffles, checkpoints, RDMA transfers) do not, and that is where the tail of their completion time comes from.

What operators do about it: split a transfer into several flows or queue pairs so the hash has more to spread; **flowlet switching** (change the path when a flow pauses long enough that reordering is unlikely); **per-packet spraying** with a receiver that tolerates reordering; congestion-aware routing in the switches; or central control. Google's Jupiter ([paper](https://research.google/pubs/jupiter-rising-a-decade-of-clos-topologies-and-centralized-control-in-googles-datacenter-network/)) replaced decentralised routing protocols, which it found excessive for a single-operator, pre-planned network, with a global configuration computed centrally and pushed to every switch.

---

## 4. Switching and flow control

### 4.1 Units

A **message** is cut into **packets**, each with a header that carries the routing information. On a chip a packet is cut further into **flits** (flow-control digits), the units that buffers and credits count; Ethernet has no flits and uses the packet as the unit. **Switching** is how a packet advances through a router; **flow control** is the rule that decides when it may, based on whether the next buffer has room.

### 4.2 Four ways to switch

- **Circuit switching.** Reserve every link on the path, then send. No buffering in the middle, but a set-up delay and idle reserved capacity. The optical circuit switches of TPU v4 are this idea at a slow time scale: they reconfigure the topology of the machine, not the path of each packet.
- **Store-and-forward.** A switch receives the whole packet, checks it, then forwards it. Needs a packet-sized buffer per input, and the packet is re-serialised at every hop.
- **Virtual cut-through.** A switch forwards as soon as it has read the header and knows the next switch can accept the **whole packet**. Unblocked, it behaves like a pipe; blocked, the packet is stored entirely in one switch.
- **Wormhole.** Like cut-through, but a switch needs room only for a **flit** or a few, so buffers are tiny. A blocked packet stays stretched across several switches (the "worm"), each holding the channel its flits occupy. That frees chips from big buffers and creates two problems: the held channels block other packets (head-of-line blocking), and a cycle of worms can deadlock (section 5). Wormhole is the usual choice on chips.

With `H` hops, a per-hop router delay `t_r`, a packet of `L` bits and a link rate `b`, so that serialising the packet takes `L / b`, and ignoring wire propagation:

```
T_store_and_forward = H × (t_r + L / b)
T_cut_through       = H × t_r + L / b          (wormhole is the same when nothing blocks)
difference          = (H - 1) × L / b
```

A **network on chip**: 6 hops, a 3-cycle router, a packet of 4 flits at one flit per cycle (`L / b` = 4 cycles): store-and-forward 6 × (3 + 4) = **42 cycles**; cut-through 6 × 3 + 4 = **22 cycles**.

A **data-centre path**: 5 switches at an assumed 400 ns each:

| Packet | `L / b` | Store-and-forward | Cut-through |
|---|---:|---:|---:|
| 64 B at 100 Gb/s | 5 ns | 2.026 µs | 2.005 µs |
| 1,500 B at 100 Gb/s | 120 ns | 2.600 µs | 2.120 µs |
| 9,000 B at 10 Gb/s | 7.2 µs | 38.0 µs | **9.2 µs** |

Store-and-forward costs `(H - 1) × L / b` extra: negligible for small packets on fast links, 28.8 µs (four times 7.2 µs) for a jumbo frame on a slow one. That is why low-latency switches forward on the header. The price of cut-through is that a packet is already on its way when its checksum is verified at the end, so a corrupted packet travels on and is dropped later.

### 4.3 Credits, and how big a buffer must be

**Credit-based flow control** is the usual lossless rule. The sender holds a counter of free slots in the receiver's buffer (initially the buffer size), spends one per flit, and stops at zero. When the receiver forwards a flit it returns a credit. Back-pressure therefore travels one hop at a time toward the source, and nothing is dropped. PCIe does exactly this per packet type ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 3.4).

How many slots? A slot, once used, comes back only after a **credit round trip**: the flit crosses the link, is processed, and the credit crosses back. To keep the link busy during that time the buffer must hold everything sent meanwhile: `bandwidth × credit round trip`, which is Little's law ([tutorial 28](28_performance_and_power.md) section 6.5) applied to a link.

- **On a chip**, a link carries one flit per cycle and the credit loop takes 4 cycles: the buffer needs **4 flits per virtual channel**. With 2 flits the link would be idle half the time.
- **Between switches**, the loop is dominated by cable length. Light in fibre takes 4.9 ns per metre, so the round trip of the cable alone is `2 × length × 4.9 ns` (computed, before adding switch processing time):

| Link | Cable | Propagation round trip | Buffer to stay full |
|---|---:|---:|---:|
| 100 Gb/s (12.5 GB/s) | 10 m | 0.10 µs | 1.2 KB |
| 100 Gb/s | 100 m | 0.98 µs | 12.2 KB |
| 100 Gb/s | 1 km | 9.8 µs | 122 KB |
| 400 Gb/s (50 GB/s) | 100 m | 0.98 µs | 49 KB |

A 64-port switch with 8 lossless priorities, each needing 12.2 KB for 100 m cables, needs 64 × 8 × 12.2 KB = **6.2 MB** of buffer for this alone. That is why such switches need megabytes of buffer, and it is the same quantity that Ethernet calls **headroom** (section 5.5): after a receiver says "stop", data already in flight keeps arriving.

### 4.4 Virtual channels

A **virtual channel (VC)** is one of several logical queues that share a physical link. Flits of different packets may interleave on the link, each VC has its own buffer and credit counter, and a packet blocked in one VC does not stop the others. VCs are used for three things:

1. **Reducing head-of-line blocking**: a blocked worm occupies one VC's buffers, so other packets can pass on the same wires.
2. **Avoiding deadlock** (the dateline and escape channels of section 5.3).
3. **Separating traffic classes**, so a flood of one class cannot starve another and so that request and reply messages never wait for each other (section 5.4). Ethernet's eight priorities play this role.

The cost is buffer memory (VCs × depth × ports), per-VC credit counters and a more complex allocator. On chips, two to four VCs are common; a switch chip may have eight or more priorities.

### 4.5 Head-of-line blocking in the switch itself

Take a switch with `N` inputs, `N` outputs, and one FIFO queue per input. If the packet at the head of input 1 wants output 3, and output 3 is busy serving input 2, input 1 waits, even though the packet behind it wants an idle output 5. Karol, Hluchyj and Morgan (1987, [paper](https://web.stanford.edu/class/cs244/papers/InputvsOutput.pdf)) showed that with uniform random destinations such a switch saturates at **2 − √2 = 58.6 %** of line rate for large `N` (75 % at `N` = 2 and 65.5 % at `N` = 4). A simulation (Answers, Problem C): inputs always have a packet; in each slot, each output serves one randomly chosen input that wants it; the winner's input draws a new random destination.

| N | 2 | 4 | 8 | 16 | 32 | 64 | limit |
|---|---:|---:|---:|---:|---:|---:|---:|
| Throughput | 0.749 | 0.659 | 0.622 | 0.601 | 0.593 | 0.590 | 0.586 |

So a FIFO-per-input switch wastes about 41 % of its capacity under saturating random traffic, however fast its fabric. The cures: **output queueing** (memory fast enough for `N` writes per slot, costly), a **shared buffer**, or **virtual output queues (VOQ)**, one queue per output at every input, so a blocked head cannot hold up the others, with a scheduler that matches inputs to outputs each slot. With VOQs and a good matching algorithm a switch can reach close to full throughput on uniform traffic (a textbook result). The same pattern repeats a level up: PFC pauses a whole priority at once, so flows that did nothing wrong wait behind the congested ones (section 5.5).

---

## 5. Deadlock

### 5.1 Four buffers, nobody can move

```
   R0 --> R1        Each arrow is a one-packet buffer (a channel).
   ^       |        Each buffer holds a packet that wants the NEXT arrow.
   |       v        All four are full, so nobody can move.
   R3 <-- R2
```

Four routers in a one-way ring, each channel with room for one packet, each holding a packet that must go two hops ahead. Every packet waits for the buffer in front of it; every buffer waits for the packet in front of it. A **deadlock** is a cycle of agents each holding a resource the next one needs. It needs four conditions together (exclusive use, hold and wait, no pre-emption, circular wait); routing can remove the last. Wormhole switching makes deadlock easy to hit, because one packet holds many channels at once; a ring or torus with shortest-path routing and no precautions deadlocks under load.

### 5.2 The channel dependency graph

Dally and Seitz (1987) made the check mechanical. Draw one vertex per **channel** (per virtual channel, if there are several) and an edge from channel A to channel B if the routing function lets a packet that holds A ask for B next. For deterministic routing, **the network is deadlock-free if and only if this graph has no cycle**. (For adaptive routing, no cycle in an "escape" subset of channels is enough, section 5.3.)

A script (Answers, Problem C) builds the graph from the routing rule for all source and destination pairs and searches for cycles:

| Network and routing | Channels | Dependencies | Cycle? |
|---|---:|---:|---|
| 4 × 4 mesh, dimension order, 1 VC | 48 | 68 | **no** |
| Ring of 4, shortest path, 1 VC | 8 | 4 | **yes** |
| 4 × 4 torus, dimension order, 1 VC | 64 | 96 | **yes** |
| Ring of 4, dateline, 2 VCs | 9 | 4 | no |
| 4 × 4 torus, dimension order, dateline, 2 VCs | 72 | 104 | no |

Why dimension order works on a mesh: a packet uses X channels, then Y channels, never Y then X, so no dependency leads from a Y channel to an X channel; and inside one dimension a minimal packet never reverses, so dependencies run one way along a line and cannot come back. The mesh graph is acyclic. Join the ends of the line into a circle (a ring or a torus) and the dependencies run around it: the cycle appears.

### 5.3 Three ways to break the cycle

1. **Forbid some turns or routes.** Dimension order forbids every turn from Y to X, 4 of the 8 possible 90-degree turns in two dimensions. The **turn model** (Glass and Ni, 1992) observes that the 8 turns form two abstract cycles of 4 (clockwise and counter-clockwise); forbid one turn from each and no cycle can form. **West-first** forbids the two turns into west (north to west and south to west): a packet that must go west goes west first, and may then adapt among the other directions. From (1, 1) to (3, 3) (two steps east, two north), XY routing allows one path; west-first allows all 6 shortest ones (`C(4, 2)`).
2. **A dateline with two virtual channels** (rings and tori). Mark one link of each ring as the dateline. A packet starts on VC 0 and moves to VC 1 when it crosses the dateline. Dependencies in VC 0 end at the dateline and those in VC 1 never return to VC 0, so the graph is acyclic (the last two rows of the table). The price is twice the buffers.
3. **An escape channel** (Duato, 1993). Let most VCs route fully adaptively, but keep one VC on a deadlock-free route (dimension order) that any blocked packet can always enter. Adaptive freedom most of the time, guaranteed progress when it matters. Many adaptive networks work this way.

### 5.4 Protocol deadlock: routing is fine, the messages are not

Suppose the network is deadlock-free, but a node cannot finish handling a **request** until it can send a **reply**, and requests and replies share the same queues. Two nodes each have an input queue full of requests; to consume one, each must put a reply into the network; the network's queues are full of requests going the other way. Nothing moves, although no routing cycle exists.

The cure is to give each **message class** its own virtual network (separate VCs and separate buffers) and order the classes so that handling one never waits on an earlier one: request, then reply. The last class, the replies, is always consumed on arrival, so the chain ends. The directory protocol of [tutorial 19](19_cache_coherence.md) section 3 (a request to the home node, a forward to the current owner, the data or acknowledgement coming back) typically uses three such classes. This is one reason the networks inside coherent multicore chips have several VCs although their XY routing needs none.

### 5.5 Deadlock in Ethernet: PFC

RDMA over Ethernet wants a network that does not drop packets, and Ethernet gets it with **priority flow control (PFC)**: when a switch's queue for one priority passes a threshold, it sends a pause for that priority to the neighbour upstream, which stops sending it. That is back-pressure like credits, but in a coarser form: per link and per priority, with the "stop" arriving one round trip late (the headroom of section 4.3). If paused queues form a cycle (possible when some packets are routed up, down and up again in the tree, for example after a failure or a misconfiguration) every queue in the cycle waits for the next, and part of the network freezes. Guo et al. ([RDMA over Commodity Ethernet at Scale](https://www.microsoft.com/en-us/research/?p=317771)) report running RoCEv2 in Microsoft's data centres and meeting PFC deadlocks, RDMA transport livelock and NIC pause-frame storms, which they had to design around. Zhu et al. ([DCQCN](https://www.microsoft.com/en-us/research/publication/congestion-control-for-large-scale-rdma-deployments/)) add that PFC also causes head-of-line blocking and unfairness, and propose an end-to-end congestion control (DCQCN, built on ECN marks and implemented in NIC hardware) so that PFC is rarely triggered.

---

## 6. Networks on chip

### 6.1 From a bus to a network

A shared bus ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 1) lets one agent talk at a time over wires that span the whole chip. That works for a few agents and fails for dozens of cores, cache slices and memory controllers. A **network on chip (NoC)** replaces it with **routers** joined by short point-to-point links that carry packets of flits: many transfers happen at once, each wire is one tile long (so it clocks fast), and capacity grows by adding tiles. Everything in sections 2 to 5 applies, with different constraints: wires are cheap, buffers cost area and power, and a hop costs a few cycles instead of hundreds of nanoseconds ([Wikipedia: network on a chip](https://en.wikipedia.org/wiki/Network_on_a_chip)).

### 6.2 Ring, then mesh

Intel's server chips connected their cores, L3 slices and memory controllers with **rings** for years and moved to a **mesh** with Skylake-SP. WikiChip's report on the ISSCC 2018 presentation ([source](https://fuse.wikichip.org/news/1017/isscc-2018-intels-skylake-sp-mesh-and-floorplan/2)) says: the mesh replaced the ring and provides considerably higher bisection bandwidth (no figure given); the highest-end parts have up to 28 cores on a 30-tile mesh, and the 28-core die has 36 converged mesh stops; each core tile connects to the mesh through its stop, which sits where the tile's caching and home agent is; a packet is routed vertically first, then horizontally, which is dimension-order routing (YX); and the vertical tile-to-tile hop takes a single cycle at frequencies above 2 GHz.

The reason is the arithmetic of section 2. Every L3 access goes to the slice that owns the address, and addresses are spread evenly over the slices ([tutorial 18](18_multicore_and_multithreading.md) section 4.3), so the **average distance** sets a large part of the L3 hit latency. As an illustration with 36 stops (I assume a 6 × 6 arrangement to show the scaling; I do not claim that is the real floor-plan), counting all pairs of distinct stops (script in the Answers, Problem B):

| Stops | Ring: average hops, bisection | Mesh: average hops, bisection | Torus: average hops, bisection |
|---:|---:|---:|---:|
| 16 | 4.27, 2 | 2.67, 4 | 2.13, 8 |
| 36 | 9.26, 2 | **4.00**, 6 | 3.09, 12 |
| 64 | 16.25, 2 | 5.33, 8 | 4.06, 16 |
| 144 | 36.25, 2 | 8.00, 12 | 6.04, 24 |

The ring's distance grows in proportion to the number of stops and its bisection stays at 2; the mesh's grows as the square root. With an assumed 2 cycles per hop (a link and a router), reaching a random slice costs 9.26 × 2 = 18.5 cycles one way on a 36-stop ring and 4.0 × 2 = 8 on the mesh. The mesh's price is more wires, five-port routers instead of three-port ones, and a harder floor-plan. Intel's two-ring compromise before it (each half of the die on its own ring) shortened the paths but, per the same report, did not solve the growing problems.

### 6.3 What is different on a chip

- **Small buffers, wormhole switching, few virtual channels.** Buffers are area and leakage; a router has a pipeline of a few stages (3 cycles in the examples of section 4.2, an assumption) and a handful of flit buffers per VC (section 4.3: 4 flits cover a 4-cycle credit loop).
- **Dimension-order routing, no tables.** It is cheap and deadlock-free on a mesh (section 5.2). The chip still needs several **virtual networks** for the message classes of the coherence protocol (section 5.4).
- **Short packets.** A request is one flit; a cache line of 64 B is 4 flits on 128-bit links, plus a header. Latency, more than bandwidth, is what the cores feel.
- **The same ideas one level out.** The links between sockets (UPI, Infinity Fabric; [tutorial 16](16_dram_memory_controllers_and_numa.md)), between chiplets ([tutorial 18](18_multicore_and_multithreading.md) section 4.4) and between GPUs (NVLink, [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)) are point-to-point networks with credits, packets and routing, and the NUMA ratios of tutorial 16 are hop counts.

---

## 7. The data-centre network

### 7.1 Why Clos, and why commodity

Google's "Jupiter Rising" ([paper](https://research.google/pubs/jupiter-rising-a-decade-of-clos-topologies-and-centralized-control-in-googles-datacenter-network/)) covers five generations of its data-centre networks. Three themes: **multi-stage Clos topologies built from commodity switch silicon** make building-scale networks cost-effective (with more than 1 Pb/s of bisection bandwidth); a **centralised control plane** (one global configuration pushed to every switch) replaced complex distributed routing protocols; and modular hardware with simple, robust software extends the design between clusters. The economics are Barroso's: a switch with ten times the bisection bandwidth often costs about a hundred times more, so the way to build a big network is many identical small switches in a fat tree, not a few huge ones.

### 7.2 Anatomy of a path

A server's NIC connects to a **top-of-rack (ToR)** switch; ToRs connect to aggregation or spine switches; in a three-tier fat tree, to core switches above. With the fat tree of section 2.4 and the assumed 400 ns per switch:

| Destination | Switches crossed | Switching time (assumed) |
|---|---:|---:|
| Same rack | 1 | 0.4 µs |
| Same pod | 3 | 1.2 µs |
| Another pod | 5 | 2.0 µs |

Add up to 1.5 µs for an assumed 300 m of cable (4.9 ns per metre). The network proper therefore costs a few microseconds one way, against the 17 µs this VM's kernel stack adds at the endpoints (section 1.2). What the network adds on top of that is queueing, and queueing is where the next three sections come from.

### 7.3 How much leaves the rack

The design question is the **oversubscription** at each tier (section 2.4). If software keeps chatty partners together and most traffic stays in the rack, a 5-to-1 uplink is fine and much cheaper. If every server talks to every other (a shuffle, or an all-to-all in training), only full bisection works. The network is sized to the traffic, and the scheduler's placement is part of the network design.

### 7.4 Incast

A client asks `N` servers for one block each, all at the same moment (a storage read striped over servers, the reduce side of a shuffle). All `N` replies arrive at the client's switch port together. The port's buffer overflows, packets are lost, and the senders wait for TCP's **retransmission timeout**, whose minimum in Linux and BSD is **200 ms** (Vasudevan et al.). The transfer needed milliseconds; the link then sits idle for a timeout.

In their experiments (a 1 Gb/s link, a fixed 1 MB block size, switch buffers they estimate at 32 to 64 KB per port), goodput fell to **about 3 % of link capacity when striping across 47 servers**. Lowering the minimum timeout to 200 µs, with high-resolution timers, sustained full goodput up to 47 senders; a 5 ms floor, the best that jiffy-based timers allow, recovered only about 65 % on the 16-node cluster and 70 to 80 % on the 48-node one.

A worked example with my own numbers: 48 servers each send 256 KB to one client on a 10 Gb/s port. The data is 12.6 MB and the ideal transfer time is 12.6 MB ÷ 1.25 GB/s = **10.07 ms**. One timeout adds its length once:

| Timeout floor | Total time | Goodput (share of line rate) |
|---:|---:|---:|
| 200 ms | 210 ms | **4.8 %** |
| 5 ms | 15.1 ms | 66.8 % |
| 0.2 ms | 10.3 ms | 98.1 % |

To absorb all the replies the port would need about `(N - 1) × R` = 47 × 256 KB = **12.3 MB** of buffer, against the tens of kilobytes the paper's switches had. You cannot buffer your way out of incast. What works is a short timeout, congestion marking (ECN) that makes senders slow down before the buffer fills, limiting how many servers answer at once (the application staggers or batches its requests), and pacing driven by the receiver.

### 7.5 RDMA, and why it wants a lossless fabric

The 17 µs of section 1.2 is endpoint cost: system calls, copies, TCP, interrupts and wake-ups. **Remote direct memory access (RDMA)** moves the transport into the NIC. An application posts work requests to a queue in registered memory and rings a doorbell ([tutorial 25](25_buses_pcie_and_io_mapping.md) section 2.5); the NIC reads and writes the memory directly and does the protocol in hardware, bypassing the kernel on the data path ([tutorial 26](26_interrupts_exceptions_and_dma.md)). Latency falls toward the microsecond range (tutorial 25 quotes about 1 µs for a kernel-bypass NIC's loopback) and the CPU is freed from the per-packet work. **InfiniBand** was built for this and has credit-based link-level flow control, so congestion does not drop packets. **RoCE** carries the same operations over Ethernet, and Zhu et al. ([DCQCN](https://www.microsoft.com/en-us/research/publication/congestion-control-for-large-scale-rdma-deployments/)) say that RoCEv2 relies on PFC for a lossless network.

That moves the difficulty into the fabric: PFC brings head-of-line blocking, unfairness and the deadlocks of section 5.5. DCQCN's answer is an end-to-end rate control, based on ECN marks from the switches and implemented in the NIC, that keeps queues short so that PFC rarely fires (tested on a three-tier Clos network). The pattern to remember is that **every lossless network is back-pressure, and back-pressure spreads**: a congested port stops its upstream neighbour, which stops its own, and so on, until innocent flows stop too.

### 7.6 Networks for accelerators

[Tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md) section 10 gave the numbers: inside a server, NVLink 4 carries 450 GB/s per GPU in each direction through NVSwitch; between servers, a 400 Gb/s port carries 50 GB/s, **9 times less**. A ring all-reduce of `S` bytes over `n` GPUs takes about `S × 2(n - 1) ÷ (n × B)`, which approaches `2S / B` for large `n`. For `S` = 1 GB that is 2 ÷ 450 = **4.4 ms** inside the NVLink domain and 2 ÷ 50 = **40 ms** over the 400 Gb/s port. This is the topology argument behind the parallelism rule of tutorial 23: tensor parallelism, with its all-reduces on the critical path of every layer, stays inside the fast island; data parallelism, whose all-reduce happens once per step and overlaps with compute, spans the slow network.

How a collective maps onto a topology is part of the design. A ring needs a ring embedded in the network; a fat tree gives any pairing full bandwidth, so the placement question is locality under one ToR; a torus runs a collective along one dimension after another. TPU v4's optical circuit switches (section 2.5) let the system choose the torus shape for the job.

---

## 8. Warehouse-scale computing

### 8.1 The building is the computer

Barroso, Hölzle and Ranganathan ([*The Datacenter as a Computer*](https://pages.cs.wisc.edu/~shivaram/cs744-readings/dc-computer-v3.pdf)) treat the data centre as one computer, a **warehouse-scale computer (WSC)**: a building of many thousands of servers, run by one operator for a few very large services, with hardware, software, network, power and cooling designed together. The unit of design is the building, not the server. Their hierarchy: server, rack (their example: 40 servers, more than 4,000 hardware threads), a cluster of racks joined by the network of section 7, and the building with its power and cooling.

### 8.2 PUE: how much of the power does computing

**Power usage effectiveness** is the total energy of the facility divided by the energy that reaches the IT equipment. A PUE of 1.0 would mean no overhead; the rest is cooling, power conversion and distribution, and lighting. Google's [efficiency page](https://datacenters.google/efficiency/) reports a fleet-wide trailing-twelve-month PUE of **1.09** (also its 2025 annual average), against an industry average of **1.54** from the Uptime Institute's 2025 Global Data Center Survey, and says that this is 83 % less overhead energy; the arithmetic agrees: `(0.54 - 0.09) ÷ 0.54` = 83.3 %.

A facility with 10 MW of IT load:

| | PUE 1.09 | PUE 1.54 |
|---|---:|---:|
| Total facility power | 10.9 MW | 15.4 MW |
| Overhead (cooling, conversion, lighting) | 0.9 MW | 5.4 MW |
| Overhead as a share of the total, `(PUE - 1) ÷ PUE` | 8.3 % | 35.1 % |

The difference is 4.5 MW × 8,760 h = 39,420 MWh, **39.4 GWh a year**, which at an assumed $0.07 per kWh is **$2.76 million a year**. The second way to see it is capacity: when a site has a fixed power feed, the IT power available is `feed ÷ PUE`. On a 10 MW feed that is 9.17 MW at PUE 1.09 and 6.49 MW at 1.54, or, at an assumed 700 W per server, 13,106 against 9,276 servers: **41 % more servers on the same utility connection**. When the feed is the limit (it often is at a large site), PUE is capacity.

What PUE does not tell you: it counts only facility overhead. The fans inside a server count as IT power, so an inefficient server fleet can sit in a facility with an excellent PUE; and it says nothing about useful work. Pair it with energy per request or performance per watt ([tutorial 28](28_performance_and_power.md) section 9.6).

### 8.3 Failure is the normal state

Barroso et al. report between 1.2 and 16 average server-level restarts per server per year across deployments, and disk drives with annualised failure rates above 4 %. At fleet scale that is a constant stream:

- 10,000 disks at 4 % a year: **400 failures a year**, 1.1 a day, one every 21.9 hours;
- 10,000 servers at 1.2 to 16 restarts a year: 12,000 to 160,000 restarts a year, **33 to 438 a day**.

Availability multiplies. Services aim for at least 99.99 % ("four nines", about an hour of downtime a year in the book; exactly 52.6 minutes). If a request needs 100 components in series, each 99.9 % available, the whole is `0.999^100` = **90.5 %**; with 1,000 components, 36.8 %. So the design assumes failure: replicate state, retry idempotent operations, use timeouts and hedged requests, have many paths in the network (a fat tree loses a fraction of its paths, not connectivity), and let the software cope so that the hardware can be cheaper.

### 8.4 The tail, and where the network adds to it

[Tutorial 28](28_performance_and_power.md) section 6.4 showed that a request fanning out to 100 servers, each slow 1 % of the time, is slow in 63 % of cases. The network feeds that tail in ways this tutorial has now covered: queueing at an oversubscribed uplink (an M/M/1 queue's mean delay is 10 times the service time at 90 % utilisation, and its 99th percentile 46 times), ECMP collisions (a flow at 39 % of its rate), incast timeouts (200 ms), PFC pauses and head-of-line blocking. The remedies are those of the earlier sections: keep link utilisation low enough to have headroom, give the hash more flows, shorten the timeouts, mark congestion early, keep latency-critical traffic in its own priority class, and send hedged requests to the application's slow tail.

### 8.5 One request, end to end

Take a request that goes to a server in another pod and returns, through the kernel's TCP path. The network proper is 5 switches (assumed 2.0 µs) and up to 300 m of cable (assumed, 1.5 µs) each way: 3.5 µs one way, 7 µs for the round trip. The endpoints, at the 17 µs per one-way message measured in section 1.2 (a VM; a tuned bare-metal kernel is lower but probably of the same order), add 34 µs for the request and the reply. The total is about 41 µs, so in the **common case software dominates** (about 80 % of it). In the **tail, queues dominate**: a single 200 ms timeout is about 5,000 times the whole 41 µs. That is the working summary of this tutorial: the median is a software and topology question; the tail is a queueing, hashing and flow-control question.

---

## 9. What you see, and what it usually is

| What you see | Likely cause | First check |
|---|---|---|
| Small messages are slow; bandwidth is a tiny fraction of the link; the CPU is busy | `α` dominates: one system call and one wake-up per message | batch writes (`writev`, `sendmmsg`, `io_uring`); measure `α` and `n_half` as in section 1.2 |
| One transfer runs at half or a third of line rate while other uplinks idle | ECMP collision of long flows | per-uplink byte counters; use more flows or queue pairs; flowlets |
| Throughput collapses and latency jumps by about 200 ms when many servers answer one client | incast and a TCP retransmission timeout | drops at the client's switch port; `nstat` retransmit counters; lower the RTO floor, ECN, limit the fan-in |
| A group of RDMA flows freezes and pause-frame counters climb | PFC storm or deadlock | `ethtool -S` pause counters per port (names vary by driver); look for loops in the routing |
| A bursty tenant delays another tenant's small RPCs on a fast fabric | head-of-line blocking in a shared queue or priority | separate priority classes; check what shares a queue |
| All-reduce time stops improving past one server | the bandwidth term `2S / B` is fixed and the slow tier is crossed | keep tensor parallelism inside the NVLink domain; check placement |
| L3 hit latency is higher on the bigger chip | more hops on the on-chip mesh | the chip's topology; keep shared data near its users |
| A many-node job is slower than the sum of its parts | oversubscription at the ToR | rack-aware placement; measure traffic that leaves the rack |
| The site cannot add servers though racks are empty | the power feed, and PUE | power capacity, not floor space |

---

## 10. Mistakes that cost points

1. **Quoting the switch latency as the latency.** Five switches cost about 2 µs (assumed 400 ns each); the kernel's stack added 17 µs per message in this VM. State both ends and the queueing, not only the network.
2. **Treating bisection bandwidth as per-host bandwidth.** Divide it by the number of hosts that cross it: a ring's bisection is 2 links whatever `N` is, which at `N` = 64 is a sixteenth of a link per node; a full-bisection fat tree gives every host its full rate.
3. **Counting a link in both directions.** A 100 Gb/s Ethernet link carries 100 Gb/s each way. NVLink 4's "900 GB/s" is 450 each way ([tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)). Say which, and count bisection per direction.
4. **Calling oversubscription good or bad without a number.** It is a ratio of capacities, not of port counts: 40 servers at 25 Gb/s on 8 uplinks of 100 Gb/s is 1.25 to 1, not 5 to 1. It is cheap, and it is fine if traffic stays in the rack.
5. **Believing a fat tree gives every flow full bandwidth.** The topology has the capacity; ECMP's hash decides who gets it. Eight flows on eight uplinks collide in 99.76 % of cases.
6. **Using average utilisation to rule out congestion.** A link that is 40 % busy on average can be full for milliseconds when an incast arrives. Look at queue depth, drops and pause counters over short intervals.
7. **Putting a torus or a ring on one virtual channel.** Shortest-path routing on a wrap-around network has a cycle in its channel dependency graph (section 5.2) and can deadlock. Use a dateline and two VCs, or avoid the wrap.
8. **Mixing up routing deadlock and protocol deadlock.** XY routing proves the *network* deadlock-free; if requests and replies share queues the *protocol* can still deadlock. Separate message classes need separate virtual networks.
9. **Treating "lossless" as free.** Back-pressure spreads: PFC brings head-of-line blocking, pause storms and, with a loop, deadlock. DCQCN exists to make PFC rare.
10. **Using one latency formula for all three switching modes.** Store-and-forward pays `L/b` at every hop (`(H - 1) × L/b` more); cut-through and wormhole pay it once; wormhole also holds its channels while blocked.
11. **Sizing a buffer without the round trip.** It must cover `bandwidth × credit round trip`: 12.2 KB per VC for 100 Gb/s over 100 m of fibre, and the cable is only part of the loop.
12. **Mixing the two PUE improvements.** Going from PUE 1.54 to 1.09 cuts the *overhead* by 83 % but the *total* energy for the same IT load by only 29.2 % (`1 - 1.09 ÷ 1.54`). Name which one you mean.
13. **Reading PUE as server efficiency.** The fans in a server count as IT power; a poor fleet in a good building has a good PUE. Add energy per request.
14. **Designing for the happy path.** With 100 components of 99.9 % each in series, availability is 90.5 %; with 10,000 disks at 4 % a year, a disk fails every 22 hours. Failure is an input to the design, not an exception.
15. **Using loopback or same-host numbers as network numbers.** Loopback measures the stack: 5.3 µs on one CPU and 16.8 µs across two in this VM, a factor of 3.2 from placement alone, with no wire involved.
16. **Ignoring that bandwidth is not constant in `n`.** The straight line `α + n/β` from mid-sized messages predicted 134 µs for 1 MiB; the measurement was 336 µs. Measure the sizes you will use.
17. **Forgetting placement.** Rack locality changes the oversubscription a job sees; CPU placement changed `α` by 3.2 times; tensor parallelism across a 50 GB/s port costs 9 times the all-reduce time of NVLink.

---

## 11. Questions you will be asked

**Q1. How do you model the cost of sending a message, and what does `n_half` mean?** `T(n) = α + n/β`: `α` is the fixed cost (software, first-bit latency, wake-up), `β` the asymptotic bandwidth. The effective bandwidth `n / T(n)` reaches half of `β` at `n_half = α × β`. Measured on loopback in the VM: `α` = 16.8 µs between two CPUs (5.3 µs on one), `β` about 9 GB/s for 16 KiB to 256 KiB messages, so `n_half` is about 150 KB (139 to 165 KB over three runs); below that you are paying mostly for `α`. Consequences: batch small messages (1 MB as one message is 105 µs, as a thousand 1 KB messages 5,100 µs with `α` = 5 µs and `β` = 10 GB/s), and measure over the sizes you use, because the line broke above 256 KiB (2.5 times slower than predicted at 1 MiB).

**Q2. What is bisection bandwidth and why does it matter?** The bandwidth across the narrowest cut that splits the network into two equal halves, i.e. the minimum number of links crossing times the link rate. It bounds any pattern where half the nodes talk to the other half, the worst case of all-to-all traffic. A ring has 2 links, an 8 by 8 mesh 8, an 8 by 8 torus 16, a 6-dimensional hypercube 32, and a fat tree `hosts / 2`. Under uniform random traffic on `N` nodes about half of every node's traffic crosses the cut, so the throughput per node is at most `bisection links ÷ (N / 4)` link rates: 1.0 for the 8 by 8 torus, 0.5 for the 8 by 8 mesh (the 0.49 that XY routing reached in section 3.2) and 0.125 for the ring. Divide by the number of hosts before comparing networks.

**Q3. Compare ring, mesh, torus and hypercube.** For `N` = 64: ring degree 2, diameter 32, average distance 16.25, bisection 2; 8 by 8 mesh degree 4, diameter 14, average 5.33, bisection 8; 8 by 8 torus degree 4, diameter 8, average 4.06, bisection 16; hypercube (or 4 × 4 × 4 torus) degree 6, diameter 6, average 3.05, bisection 32. Wrap-around halves the diameter and doubles the bisection for the same degree, at the price of long wires (fixed by folding) and a deadlock problem (a dateline and two VCs). Higher dimension buys distance and bisection with ports and wires. Rings and meshes appear on chips, 3D tori in accelerator pods.

**Q4. How is a k-ary fat tree built and how big is it?** `k` pods; each pod has `k/2` edge and `k/2` aggregation switches; each edge switch has `k/2` hosts below and the `k/2` aggregation switches above; there are `(k/2)²` core switches, and aggregation switch `i` of every pod reaches the `k/2` cores of group `i`. Hosts `k³/4`, switches `5k²/4`, links `3k³/4`; between pods there are `(k/2)²` equal-cost paths, within a pod `k/2`. `k` = 48 gives 27,648 hosts on 2,880 switches; `k` = 64 gives 65,536 on 5,120. Full bisection: any host can send at line rate to any other at once, if the load is spread over the paths.

**Q5. Leaf-spine, fat tree and Clos: how are they related?** A Clos network has ingress, middle and egress stages and is non-blocking in the strict sense when `m ≥ 2n - 1` middle switches serve `n`-port ingress switches (rearrangeably when `m ≥ n`). Folding it, so ingress and egress are the same switches, gives a leaf-spine (two tiers: leaves and spines) and, recursively, a fat tree (three tiers). With `R`-port switches, two tiers reach `R²/2` hosts (64 ports: 2,048 hosts, 96 switches, 3 switch hops at most) and three tiers `R³/4`. Google's Jupiter is a multi-stage Clos of commodity switch silicon.

**Q6. Store-and-forward, cut-through and wormhole: what is the difference?** Store-and-forward receives the whole packet before forwarding: `H × (t_r + L/b)`. Cut-through forwards after the header, if the next buffer takes the whole packet: `H × t_r + L/b`. Wormhole is cut-through with flit-sized buffers, so a blocked packet stays stretched over several switches and holds their channels (head-of-line blocking, deadlock risk). Example, 5 switches at 400 ns: a 9,000-byte frame at 10 Gb/s takes 38 µs store-and-forward and 9.2 µs cut-through; a 64-byte packet at 100 Gb/s takes 2.026 and 2.005 µs. The difference `(H - 1) × L/b` matters for long packets on slow links.

**Q7. How does credit-based flow control work, and how big must the buffer be?** The sender keeps a count of free slots at the receiver, spends one per flit, stops at zero, and gets a credit back when the receiver frees a slot. Nothing is dropped; back-pressure moves one hop at a time. To keep the link busy the buffer must cover the credit round trip: `bandwidth × RTT` (Little's law). On a chip with a 4-cycle loop, 4 flits per VC; for 100 Gb/s over 100 m of fibre, 12.2 KB per VC from propagation alone; for 1 km, 122 KB. That is also the headroom in Ethernet PFC: data keeps arriving after "stop" is sent.

**Q8. What is a virtual channel for?** Several logical queues share one physical link, each with its own buffer and credits. They (1) stop a blocked packet from blocking the link, reducing head-of-line blocking; (2) break cycles in the channel dependency graph (dateline, escape channels); (3) separate traffic or message classes (priorities, requests against replies). The cost is buffers and allocation logic.

**Q9. What is deadlock in an interconnect, and how do you show a routing function is free of it?** A cycle of packets each holding a channel the next needs. Build the channel dependency graph (a vertex per channel, an edge A to B if routing lets a packet holding A request B); for deterministic routing the network is deadlock-free exactly when the graph has no cycle. XY routing on a mesh passes (48 channels, 68 dependencies, no cycle); shortest-path routing on a ring or XY on a torus fails with one VC; the same torus with a dateline and two VCs passes (72 channels, 104 dependencies). Other fixes: the turn model (forbid one turn per abstract cycle, e.g. west-first) and an escape VC for adaptive routing.

**Q10. What is protocol deadlock?** Routing can be deadlock-free and the protocol still deadlock: a node must send a reply to consume a request, and requests and replies share queues. Both nodes' queues fill with requests; neither can emit its reply. The cure is a separate virtual network (separate VCs and buffers) per message class, ordered so that the last class (replies) is always consumed. Coherence protocols use three or so classes (request, forward, response), which is why the chip's network has several VCs although XY routing needs none.

**Q11. Why do elephant flows collide under ECMP, and what do you do?** ECMP hashes a flow's identifiers to one of the equal-cost uplinks, so it is stateless and keeps order, but the hash ignores size. Eight equal flows on eight uplinks give a busiest uplink with 2.59 flows on average; only 65.7 % of the uplinks are used; the chance of no collision is `8!/8⁸` = 0.24 %. A flow on the busiest link runs at about 39 % of its rate. Remedies: more flows or queue pairs per transfer, flowlet switching, per-packet spraying with reordering tolerance, congestion-aware routing, or central path computation as in Jupiter.

**Q12. What is incast and how bad is it?** Many servers answer one client at the same moment; the client's switch port overflows; the senders wait for TCP's retransmission timeout (200 ms minimum in Linux and BSD) although the data took milliseconds. Vasudevan et al.: goodput of about 3 % of a 1 Gb/s link across 47 servers; with a 200 µs timeout floor, full goodput. Worked example: 48 senders × 256 KB into a 10 Gb/s port is 10.07 ms ideal, 210 ms with one 200 ms timeout (4.8 %), 15.1 ms with a 5 ms floor (66.8 %), 10.3 ms with 0.2 ms (98.1 %). Buffering would need `(N - 1) × R` = 12.3 MB. Fixes: small RTO, ECN, limit the fan-in, receiver pacing.

**Q13. Why does RDMA want a lossless network, what is PFC, and what goes wrong?** RDMA NICs implement the transport in hardware, and many have only simple loss recovery, so loss is costly; RoCEv2 relies on PFC to avoid it (Zhu et al.). PFC pauses one priority on a link when its queue passes a threshold. It spreads back-pressure: a congested port stops its upstream neighbour and, through it, flows that were not congested (head-of-line blocking, unfairness), and a cycle of pauses is a deadlock; Guo et al. report PFC deadlocks, RDMA livelock and NIC pause storms in production. DCQCN adds end-to-end rate control from ECN marks, in the NIC, so that PFC rarely fires.

**Q14. Why did Intel's server chips move from a ring to a mesh?** The ring's average distance and its latency grow in proportion to the number of stops and its bisection stays at 2 links; a mesh's grow with the square root. For 36 stops: 9.26 average hops on a ring, 4.0 on a 6 × 6 mesh (my illustration), bisection 2 against 6. Skylake-SP's 28-core die has 36 mesh stops, routes vertically then horizontally, and takes a single cycle per vertical hop above 2 GHz. The price: five-port routers, more wiring, a harder floor-plan. The L3 is address-sliced, so the average distance sets a large part of the L3 hit latency.

**Q15. What is PUE, and what do 1.09 and 1.54 mean for a 10 MW load?** Total facility energy over IT energy. At 10 MW of IT load the facility draws 10.9 MW (overhead 0.9 MW, 8.3 % of the total) or 15.4 MW (5.4 MW, 35.1 %). The 4.5 MW difference is 39.4 GWh a year, $2.76 million at an assumed $0.07 per kWh. On a fixed 10 MW feed, IT power is `10 ÷ PUE`: 9.17 MW against 6.49 MW, 41 % more servers at 700 W each (assumed). Overhead falls 83 % but total energy for the same work 29 %. PUE does not measure server efficiency or useful work.

**Q16. How does a warehouse-scale computer cope with failures?** By assuming them. 10,000 disks at 4 % a year is 400 failures a year (one every 22 hours); 10,000 servers at 1.2 to 16 restarts a year is 33 to 438 restarts a day (Barroso et al.). Availability multiplies: 100 components at 99.9 % in series give 90.5 %. So: replicate data, make operations idempotent and retry them, use timeouts and hedged requests, spread over racks and power domains, rely on a network with many equal-cost paths so that a failure removes a fraction of the capacity, and automate repair.

**Q17. Why does tensor parallelism stay inside a server and data parallelism go across the network?** A large ring all-reduce takes about `2S / B`: 1 GB over NVLink at 450 GB/s is 4.4 ms and over a 400 Gb/s port at 50 GB/s is 40 ms, 9 times more. Tensor parallelism puts all-reduces on the critical path of every layer, so it needs the fast island. Data parallelism does one all-reduce per step and overlaps it with compute, so the slow tier can carry it. The topology (a fat tree, or the reconfigurable 3D torus of TPU v4) decides how the ring or tree of the collective is laid out.

**Q18. "Design the network for 10,000 servers." Where do you start?** Back-of-envelope first. Two tiers of 64-port switches reach 2,048 hosts and 128-port ones 8,192, so use three tiers: a `k`-ary fat tree needs `k³/4 ≥ 10,000`, which gives `k` = 36: 11,664 hosts, 36 pods, 324 core switches, 1,620 switches, 34,992 links, full bisection. Then say what you would change: oversubscribe the rack uplink (a 40-server rack on 8 uplinks is 5 to 1) if jobs are rack-local, which removes the core's cost; ECMP with enough flows, and flowlets for the elephants; ECN and a short RTO against incast; RDMA only where needed, with PFC limited to its own priority and DCQCN; telemetry on per-port queue depth, drops and pause counters; a power budget in MW with the PUE; and the failure story (the loss of one core switch removes `1 / (k/2)²` of the inter-pod paths).

---

## 12. Problems

**How to use this section.** Four problems. Each has paper parts, a script in the Answers that checks them, and a *Where* and *How* line saying where to check and how to start. Problem A also has a part for your own machine. Do the paper part first, then run the script, and fix the paper answer if they disagree. Time: about 20 to 30 minutes each.

### Problem A (paper, then a script, then your machine): the cost of a message

(a) A path has `α` = 5 µs and `β` = 10 GB/s (take 1 KB as 1,000 bytes). Find `T(n)` and the effective bandwidth for 1 KB, 100 KB and 10 MB, the half-bandwidth size `n_half`, and the time to send 1 MB as one message and as a thousand 1 KB messages.
(b) A path of 5 switches, 250 ns each, with 25 Gb/s links. Compute the latency of a 4,500-byte packet and of a 64-byte packet under store-and-forward and under cut-through (ignore wire time). Which term dominates in each case?
(c) A 200 Gb/s link with 50 m of cable (4.9 ns per metre) and 100 ns of switch processing in each direction of the credit loop. How big must the buffer be per virtual channel to keep the link full? How much does a 64-port switch with 8 virtual channels per port need?
(d) *On your machine:* compile the program in the Answers, run it with the two processes on different CPUs and on the same CPU, and fit `α`, `β` and `n_half`. Where does the straight line stop describing the data?

*Where:* (a) to (c) on paper, checked by `ab.py`; (d) on any Linux machine with at least two CPUs (use `0 0` for both if you have one).
*How:* (a) `T = α + n/β`, `n_half = αβ`. (b) section 4.2. (c) `bandwidth × round trip` (section 4.3). (d) `α` is the flat part at small sizes; `β` is the slope of the steepest straight stretch.

### Problem B (paper, then a script): topologies and routing

(a) For `N` = 256 nodes give the degree, link count, diameter, average distance and bisection width of: a ring, a 16 × 16 mesh, a 16 × 16 torus and a hypercube of dimension 8.
(b) Every link carries 100 Gb/s in each direction. Give the bisection bandwidth (per direction) of each network. If every node sends to a uniformly random other node at 100 Gb/s, about how much traffic must cross the bisection in each direction, and what fraction of the offered load can each network carry at most?
(c) On the 8 × 8 mesh with XY routing list the channels from (1, 6) to (5, 2). How many hops would Valiant routing take if the random intermediate node were (6, 0)? How many hops from (0, 0) to (7, 7) on the 8 × 8 torus, and on the mesh?
(d) Under transpose traffic, `(x, y)` to `(y, x)`, on the 8 × 8 mesh with XY routing: which channel is busiest, how many flows share it, and what is the throughput per node? Why does Valiant routing give 0.25 for this pattern and for every other permutation?

*Where:* paper; check with `topo.py`.
*How:* (a) closed forms of section 2.2. (b) half of each node's traffic crosses the cut: `(N/2) × rate × (N/2) / (N - 1)`. (c) X first, then Y; on a torus take the shorter way round each ring. (d) find which sources route through the corner of the top row and right column; Valiant is two phases of uniform traffic.

### Problem C (paper, then a script): deadlock and head-of-line blocking

(a) A one-way ring R0 to R1 to R2 to R3 to R0. Each channel holds one packet, and each router holds a packet for the router two hops ahead. Show that nothing can move, list the edges of the channel dependency graph and find the cycle.
(b) Put a dateline on the link R3 to R0 and add a second virtual channel (VC). Say which VC each hop of each two-hop packet uses, and show that the dependency graph has no cycle.
(c) List the eight 90-degree turns of a 2D mesh. Which does XY routing forbid? Which does west-first forbid? Count the shortest paths each allows from (1, 1) to (3, 3) and from (3, 3) to (1, 1).
(d) Nodes A and B each have an input queue of two slots shared by requests and replies. To take a request out of its queue, a node must first send a reply into the sender's queue. Both queues start full of requests from the other node. Show the deadlock and give the fix.
(e) A 4 × 4 input-queued switch has FIFO inputs holding packets (given as output numbers, head first): input 0 `[1, 3]`, input 1 `[1, 0]`, input 2 `[1, 2]`, input 3 `[3, 2]`. How many packets leave in the first slot? How many could leave with virtual output queues?

*Where:* paper; check (a), (b) and the rest of the table of section 5.2 with `dead.py`.
*How:* (a) an edge from channel A to channel B when a packet holding A wants B next. (b) cross the dateline, switch VC. (c) the two abstract cycles of four turns. (d) who waits for whom. (e) heads of the queues, then a matching.

### Problem D (paper, then a script): a data centre

(a) A fat tree of 32-port switches with 100 Gb/s links: count the hosts, the switches in each tier, the links, the equal-cost paths between pods and the full bisection bandwidth. What is the smallest (even) `k` that holds 30,000 hosts?
(b) A rack has 40 servers at 25 Gb/s and a top-of-rack switch with 8 uplinks at 100 Gb/s. What is the oversubscription, and how much can each server send off the rack if all do? Repeat for Barroso's example of 40 servers and 8 uplinks, all at the same rate.
(c) Eight equal flows are hashed onto 8 uplinks. What is the probability that no two share an uplink, and how many uplinks are idle on average? Then 32 servers each send 128 KB to one client on a 25 Gb/s port: the ideal time, the goodput if one 200 ms timeout occurs, and the buffer needed to absorb the burst.
(d) A site has 20 MW of IT load. At PUE 1.2 and 1.5, give the facility power, the overhead as a share of the total, and the difference in annual energy and cost at an assumed $0.08 per kWh. On a 30 MW feed, how many 500 W servers fit in each case?
(e) A request needs 20 services in series, each 99.95 % available. Give the availability and the downtime per year. If every service is replicated twice and either copy suffices, what are they now? Which assumption is optimistic?

*Where:* paper; check with `dc.py`.
*How:* (a) the formulas of section 2.4. (b) capacity below over capacity above. (c) the birthday problem; incast as in section 7.4. (d) section 8.2. (e) multiply availabilities in series; for two copies, `1 - (1 - a)²`.

---

## Answers

### Problem A

**(a)** `T(1 KB)` = 5 + 0.1 = **5.1 µs**, 196 MB/s, **2.0 %** of `β`. `T(100 KB)` = 5 + 10 = **15 µs**, 6.67 GB/s, 66.7 %. `T(10 MB)` = 5 + 1000 = **1,005 µs**, 9.95 GB/s, 99.5 %. `n_half` = 5 µs × 10 GB/s = **50,000 bytes**. One 1 MB message takes 105 µs; a thousand 1 KB messages take 1000 × 5.1 = 5,100 µs, **48.6 times** longer.

**(b)** Serialising 4,500 bytes at 25 Gb/s takes 36,000 ÷ 25 × 10⁹ = 1.44 µs. Store-and-forward: 5 × (0.25 + 1.44) = **8.45 µs**; cut-through: 5 × 0.25 + 1.44 = **2.69 µs**. For 64 bytes the serialisation is 20.5 ns: store-and-forward **1.352 µs**, cut-through **1.270 µs**. For the large packet, serialisation dominates and store-and-forward pays it five times (4 × 1.44 = 5.76 µs extra); for the small one, the five switches (1.25 µs) are almost everything and the two modes are within 7 %.

**(c)** The loop is 2 × 50 m × 4.9 ns = 490 ns of cable plus 2 × 100 ns of processing = **690 ns**. At 25 GB/s that is 25 × 10⁹ × 690 × 10⁻⁹ = **17.25 KB per VC**. A 64-port switch with 8 VCs per port needs 64 × 8 × 17.25 KB = **8.8 MB**.

```python
# ab.py: Problem A, parts (a) to (c)
alpha, beta = 5e-6, 10e9                                   # 5 us and 10 GB/s; 1 KB = 1,000 B here
for n in (1_000, 100_000, 10_000_000):
    t = alpha + n / beta
    print("n=%10d B: T=%7.1f us, effective %7.1f MB/s = %5.1f%% of beta" % (n, t * 1e6, n / t / 1e6, 100 * n / t / beta))
print("n_half = %d B" % (alpha * beta))
one = alpha + 1e6 / beta; many = 1000 * (alpha + 1e3 / beta)
print("1 MB as one message %.0f us; as 1000 x 1 KB %.0f us; ratio %.1f" % (one * 1e6, many * 1e6, many / one))

def sf(H, tr, ser): return H * (tr + ser)                  # store-and-forward
def ct(H, tr, ser): return H * tr + ser                    # cut-through
for nbytes in (4500, 64):
    ser = nbytes * 8 / 25e9
    print("%5d B at 25 Gb/s: serialisation %.3f us; store-and-forward %.3f us; cut-through %.3f us" % (nbytes, ser * 1e6, sf(5, 250e-9, ser) * 1e6, ct(5, 250e-9, ser) * 1e6))

bw = 200e9 / 8; rtt = 2 * 50 * 4.9e-9 + 2 * 100e-9         # cable both ways plus 100 ns of processing each way
print("credit loop %.0f ns -> %.2f KB per VC; 64 ports x 8 VCs = %.2f MB" % (rtt * 1e9, bw * rtt / 1e3, 64 * 8 * bw * rtt / 1e6))
```

Output:

```
n=      1000 B: T=    5.1 us, effective   196.1 MB/s =   2.0% of beta
n=    100000 B: T=   15.0 us, effective  6666.7 MB/s =  66.7% of beta
n=  10000000 B: T= 1005.0 us, effective  9950.2 MB/s =  99.5% of beta
n_half = 50000 B
1 MB as one message 105 us; as 1000 x 1 KB 5100 us; ratio 48.6
 4500 B at 25 Gb/s: serialisation 1.440 us; store-and-forward 8.450 us; cut-through 2.690 us
   64 B at 25 Gb/s: serialisation 0.020 us; store-and-forward 1.352 us; cut-through 1.270 us
credit loop 690 ns -> 17.25 KB per VC; 64 ports x 8 VCs = 8.83 MB
```

**(d)** The program (`gcc -O2 pp.c -o pp`; run `./pp 0 1` for client on CPU 0 and server on CPU 1, then `./pp 0 0`):

```c
// pp.c: TCP loopback ping-pong between two processes.  gcc -O2 pp.c -o pp && ./pp 0 1   (client CPU, server CPU)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/wait.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void pin(int cpu) { cpu_set_t s; CPU_ZERO(&s); CPU_SET(cpu, &s); sched_setaffinity(0, sizeof s, &s); }
static void rd(int fd, char *b, size_t n) { while (n) { ssize_t r = read(fd, b, n); if (r <= 0) exit(1); b += r; n -= r; } }
static void wr(int fd, char *b, size_t n) { while (n) { ssize_t r = write(fd, b, n); if (r <= 0) exit(1); b += r; n -= r; } }
static int cmp(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return (x > y) - (x < y); }
int main(int argc, char **argv) {
    int ccpu = argc > 1 ? atoi(argv[1]) : 0, scpu = argc > 2 ? atoi(argv[2]) : 1, one = 1;
    int ls = socket(AF_INET, SOCK_STREAM, 0); setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = 0 };
    bind(ls, (void *)&a, sizeof a); listen(ls, 1); socklen_t al = sizeof a; getsockname(ls, (void *)&a, &al);
    size_t maxn = 1 << 20; char *buf = malloc(maxn); memset(buf, 1, maxn);
    if (fork() == 0) {                                           // server: echo whatever size the client announces
        pin(scpu); int fd = accept(ls, 0, 0); setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        for (;;) { size_t n; rd(fd, (char *)&n, sizeof n); if (!n) exit(0); rd(fd, buf, n); wr(fd, buf, n); }
    }
    pin(ccpu); int fd = socket(AF_INET, SOCK_STREAM, 0); connect(fd, (void *)&a, sizeof a); setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    printf("client cpu %d, server cpu %d\n  bytes   one-way us (median of blocks)   min us   MB/s each way\n", ccpu, scpu);
    for (size_t n = 8; n <= maxn; n *= (n < 64 ? 8 : 4)) {
        int iters = n <= 4096 ? 20000 : (n <= 65536 ? 3000 : 300); double blk[15];
        for (int b = -2; b < 15; b++) {                           // two warm-up blocks, then 15 timed
            double t0 = now();
            for (int i = 0; i < iters; i++) { wr(fd, (char *)&n, sizeof n); wr(fd, buf, n); rd(fd, buf, n); }
            if (b >= 0) blk[b] = (now() - t0) / iters / 2;
        }
        qsort(blk, 15, sizeof *blk, cmp);
        printf("%8zu   %10.2f   %10.2f   %10.1f\n", n, blk[7] * 1e6, blk[0] * 1e6, n / blk[7] / 1e6);
    }
    size_t z = 0; wr(fd, (char *)&z, sizeof z); wait(0); return 0;
}
```

and the fit (`./pp 0 1 | python3 fitpp.py`):

```python
# fitpp.py: ./pp 0 1 | python3 fitpp.py
import sys
import numpy as np
rows = [(int(p[0]), float(p[1])) for p in (l.split() for l in sys.stdin) if len(p) == 4 and p[0].isdigit()]
n = np.array([r[0] for r in rows]); t = np.array([r[1] for r in rows])     # bytes, one-way microseconds
alpha = np.median(t[n <= 1024])
mid = (n >= 16384) & (n <= 262144)
slope, icpt = np.polyfit(n[mid], t[mid], 1)                                # microseconds per byte
beta = 1 / (slope * 1e-6)
print("alpha %.1f us (median of sizes <= 1 KiB); intercept of the mid-range fit %.1f us" % (alpha, icpt))
print("beta %.2f GB/s (16 KiB to 256 KiB); n_half = alpha * beta = %.0f KB" % (beta / 1e9, alpha * 1e-6 * beta / 1e3))
print("1 MiB: model %.0f us, measured %.0f us" % (alpha + 1048576 / beta * 1e6, t[-1]))
```

In this VM the four runs gave: CPUs 0 and 1, `α` = 17.0 µs, `β` = 8.17 GB/s, `n_half` = 139 KB; CPUs 1 and 0, 16.2 µs, 9.82 GB/s, 160 KB; both on CPU 0, 5.3 µs, 10.12 GB/s, 54 KB and 5.3 µs, 8.05 GB/s, 43 KB. A rerun of the program exactly as printed here gave 16.9 µs, 9.82 GB/s and 165 KB between CPUs, and 5.4 µs, 10.45 GB/s and 56 KB on one CPU. The 1 MiB point was measured at 330 to 346 µs between CPUs (each run's line predicts 123 to 145) and 192 to 214 µs on one CPU (it predicts 106 to 136). Yours will differ, but check three things: `α` on one CPU is a third of `α` across two; `β` from the mid-range slope is a few GB/s to ten; and the largest message is slower than the line predicts. Then change something and predict the effect: raise the socket buffers (`sysctl net.ipv4.tcp_rmem`, `tcp_wmem`), or send from a cold buffer, and see which part of the curve moves.

### Problem B

**(a)** `N` = 256:

| Network | Degree | Links | Diameter | Average distance | Bisection |
|---|---:|---:|---:|---:|---:|
| Ring | 2 | 256 | 128 | 64.25 | 2 |
| 16 × 16 mesh | 4 | 480 | 30 | 10.67 | 16 |
| 16 × 16 torus | 4 | 512 | 16 | 8.03 | 32 |
| Hypercube (2⁸) | 8 | 1,024 | 8 | 4.02 | 128 |

The closed forms: ring average `N² ÷ (4(N - 1))` = 65,536 ÷ 1,020 = 64.25; mesh links `2k(k - 1)` = 480, diameter `2(k - 1)` = 30, average `2k/3` = 10.67; torus links `2k²` = 512, diameter `k` = 16, average `(k/2) × N/(N - 1)` = 8.03; hypercube links `N d / 2` = 1,024, average `d 2^(d-1) ÷ (2^d - 1)` = 4.02.

**(b)** Per direction: ring 2 × 100 Gb/s = **0.2 Tb/s**; mesh 16 × 100 = **1.6 Tb/s**; torus **3.2 Tb/s**; hypercube **12.8 Tb/s**. The demand across the cut in each direction is 128 nodes × 100 Gb/s × 128/255 = **6.4 Tb/s**. So at most: ring 0.2 ÷ 6.4 = **3 %**, mesh **25 %**, torus **50 %**, hypercube **100 %** (it has twice the capacity needed). These are upper bounds from the cut alone; a routing algorithm can reach less (section 3.2: XY got 0.49 of the mesh's 0.5 at `N` = 64).

**(c)** XY from (1, 6) to (5, 2): (2,6), (3,6), (4,6), (5,6), (5,5), (5,4), (5,3), (5,2): **8 hops**. Valiant via (6, 0): (1,6) to (6,0) is 5 + 6 = 11 hops, then (6,0) to (5,2) is 1 + 2 = 3: **14 hops**, 1.75 times the minimum. On the torus (0, 0) to (7, 7) is **2 hops** (one wrap-around link in each dimension); on the mesh, **14**.

**(d)** The seven nodes (0, 7) to (6, 7) of the top row send to (7, 0) to (7, 6) of the right column. XY sends each east along row 7, so the channel (6,7) to (7,7) carries all **7 flows** (and the channel (7,7) to (7,6) after it also carries all 7). The throughput per node is 1/7 = **0.14**. Valiant routing makes the traffic of any permutation two phases of uniform random traffic, each loading the busiest channel as uniform traffic does (2.03), so 2 × 2.03 = 4.06 and a throughput of 1/4.06 = **0.25** whatever the permutation.

```python
# topo.py: Problem B. Build the networks, measure them, and load the channels of XY routing.
import itertools
from collections import deque

def grid(k, d, wrap):                       # k nodes per dimension, d dimensions; k = 2 with wrap is a hypercube
    nodes = list(itertools.product(range(k), repeat=d)); adj = {n: set() for n in nodes}
    for n in nodes:
        for i in range(d):
            for s in (-1, 1):
                m = n[i] + s
                if wrap: m %= k
                elif not 0 <= m < k: continue
                v = n[:i] + (m,) + n[i + 1:]
                if v != n: adj[n].add(v)
    return adj

def distances(adj):                         # (diameter, average over ordered pairs of distinct nodes)
    tot = cnt = dia = 0
    for s in adj:
        d = {s: 0}; q = deque([s])
        while q:
            u = q.popleft()
            for v in adj[u]:
                if v not in d: d[v] = d[u] + 1; q.append(v)
        for t, x in d.items():
            if t != s: tot += x; cnt += 1; dia = max(dia, x)
    return dia, tot / cnt

def bisection(adj):                         # exhaustive: every balanced cut with node 0 on side A (feasible at N = 16)
    nodes = list(adj); best = 10**9
    for rest in itertools.combinations(nodes[1:], len(nodes) // 2 - 1):
        A = set(rest) | {nodes[0]}
        best = min(best, sum(1 for u in A for v in adj[u] if v not in A))
    return best

def links(adj): return sum(map(len, adj.values())) // 2
def degree(adj): return max(map(len, adj.values()))

print("N = 16, bisection by exhaustive search")
for name, adj in (("ring", grid(16, 1, True)), ("4x4 mesh", grid(4, 2, False)), ("4x4 torus", grid(4, 2, True)), ("hypercube 2^4", grid(2, 4, True))):
    dia, avg = distances(adj)
    print("%-14s degree %d links %3d diameter %2d average %.2f bisection %d" % (name, degree(adj), links(adj), dia, avg, bisection(adj)))
print("N = 64 and 256, bisection from the closed forms")
for name, adj, bis in (("ring 64", grid(64, 1, True), 2), ("8x8 mesh", grid(8, 2, False), 8), ("8x8 torus", grid(8, 2, True), 16),
                       ("4x4x4 torus", grid(4, 3, True), 32), ("hypercube 2^6", grid(2, 6, True), 32), ("ring 256", grid(256, 1, True), 2),
                       ("16x16 mesh", grid(16, 2, False), 16), ("16x16 torus", grid(16, 2, True), 32), ("hypercube 2^8", grid(2, 8, True), 128)):
    dia, avg = distances(adj)
    print("%-14s degree %d links %4d diameter %3d average %6.2f bisection %3d" % (name, degree(adj), links(adj), dia, avg, bis))
print("average hops, ring / mesh / torus")
for N, k in ((16, 4), (36, 6), (64, 8), (144, 12)):
    print("N=%3d  %6.2f  %5.2f  %5.2f" % (N, distances(grid(N, 1, True))[1], distances(grid(k, 2, False))[1], distances(grid(k, 2, True))[1]))

k = 8                                       # XY routing on the 8 x 8 mesh
def route(s, d):                            # X first, then Y; returns the directed channels
    x, y = s; path = []
    while x != d[0]: nx = x + (1 if d[0] > x else -1); path.append(((x, y), (nx, y))); x = nx
    while y != d[1]: ny = y + (1 if d[1] > y else -1); path.append(((x, y), (x, ny))); y = ny
    return path
nodes = list(itertools.product(range(k), repeat=2)); N = len(nodes)
patterns = {"uniform random": [(s, d, 1 / (N - 1)) for s in nodes for d in nodes if s != d],
            "transpose": [(s, (s[1], s[0]), 1) for s in nodes if s[0] != s[1]],
            "bit complement": [(s, (k - 1 - s[0], k - 1 - s[1]), 1) for s in nodes]}
def loads(p):
    L = {}
    for s, d, w in p:
        for c in route(s, d): L[c] = L.get(c, 0) + w
    return L
for name, p in patterns.items():
    L = loads(p); m = max(L.values()); hot = max(L, key=L.get)
    print("%-15s busiest channel load %.2f (%s -> %s)  throughput %.2f  average hops %.2f" % (name, m, hot[0], hot[1], min(1, 1 / m), sum(len(route(s, d)) * w for s, d, w in p) / N))
v = 2 * max(loads(patterns["uniform random"]).values())
print("Valiant: busiest load %.2f -> throughput %.2f for any permutation" % (v, 1 / v))
print("route (1,6)->(5,2):", [c[1] for c in route((1, 6), (5, 2))], len(route((1, 6), (5, 2))), "hops")
print("Valiant via (6,0):", len(route((1, 6), (6, 0))) + len(route((6, 0), (5, 2))), "hops")
ring = lambda a, b: min((a - b) % k, (b - a) % k)
print("torus (0,0)->(7,7):", ring(0, 7) + ring(0, 7), "hops; mesh:", len(route((0, 0), (7, 7))), "hops")
```

Output:

```
N = 16, bisection by exhaustive search
ring           degree 2 links  16 diameter  8 average 4.27 bisection 2
4x4 mesh       degree 4 links  24 diameter  6 average 2.67 bisection 4
4x4 torus      degree 4 links  32 diameter  4 average 2.13 bisection 8
hypercube 2^4  degree 4 links  32 diameter  4 average 2.13 bisection 8
N = 64 and 256, bisection from the closed forms
ring 64        degree 2 links   64 diameter  32 average  16.25 bisection   2
8x8 mesh       degree 4 links  112 diameter  14 average   5.33 bisection   8
8x8 torus      degree 4 links  128 diameter   8 average   4.06 bisection  16
4x4x4 torus    degree 6 links  192 diameter   6 average   3.05 bisection  32
hypercube 2^6  degree 6 links  192 diameter   6 average   3.05 bisection  32
ring 256       degree 2 links  256 diameter 128 average  64.25 bisection   2
16x16 mesh     degree 4 links  480 diameter  30 average  10.67 bisection  16
16x16 torus    degree 4 links  512 diameter  16 average   8.03 bisection  32
hypercube 2^8  degree 8 links 1024 diameter   8 average   4.02 bisection 128
average hops, ring / mesh / torus
N= 16    4.27   2.67   2.13
N= 36    9.26   4.00   3.09
N= 64   16.25   5.33   4.06
N=144   36.25   8.00   6.04
uniform random  busiest channel load 2.03 ((0, 3) -> (0, 4))  throughput 0.49  average hops 5.33
transpose       busiest channel load 7.00 ((6, 7) -> (7, 7))  throughput 0.14  average hops 5.25
bit complement  busiest channel load 4.00 ((3, 0) -> (4, 0))  throughput 0.25  average hops 8.00
Valiant: busiest load 4.06 -> throughput 0.25 for any permutation
route (1,6)->(5,2): [(2, 6), (3, 6), (4, 6), (5, 6), (5, 5), (5, 4), (5, 3), (5, 2)] 8 hops
Valiant via (6,0): 14 hops
torus (0,0)->(7,7): 2 hops; mesh: 14 hops
```

### Problem C

**(a)** Name the channels c0 (R0 to R1), c1, c2, c3 (R3 to R0). The packet in c0 wants to go on to R2, so it needs c1; the packet in c1 needs c2; the one in c2 needs c3; the one in c3 needs c0. All four are full and each waits for the next: **no packet can move**. The dependency graph has the edges c0 to c1, c1 to c2, c2 to c3 and c3 to c0: **a cycle**.

**(b)** Packets start on VC 0 and move to VC 1 after crossing the dateline R3 to R0. From R0: c0 on VC 0, then c1 on VC 0. From R1: c1, c2 on VC 0. From R2: c2, c3 on VC 0. From R3: c3 on VC 0 (the dateline link), then c0 on **VC 1**. Edges: c0v0 to c1v0, c1v0 to c2v0, c2v0 to c3v0, c3v0 to c0v1. No edge leads from VC 1 back to VC 0, so there is **no cycle** (the script, which also sends three-hop packets, finds 5 dependencies and none closes a loop).

**(c)** The eight turns: east to north, east to south, west to north, west to south (X then Y) and north to east, north to west, south to east, south to west (Y then X). **XY forbids the four Y-then-X turns.** **West-first forbids two**: north to west and south to west (a packet that must go west does so first). From (1, 1) to (3, 3), two steps east and two north: XY allows **1** path; west-first allows all `C(4, 2)` = **6**. From (3, 3) to (1, 1), two steps west and two south: XY **1**; west-first also only **1**, because both west moves must come first. The adaptivity of a turn-model algorithm depends on the direction of travel.

**(d)** A's queue holds two requests from B; B's holds two from A. To consume a request, A must put a reply into B's queue, which is full of requests; B is in the same position. Each waits for the other, although no routing cycle exists. **Fix:** give replies their own queue (a second virtual network) at each node. A reply is consumed on arrival without producing any further message, so the reply queue always drains, and so the request queue can too. That is two queues per node instead of one.

**(e)** FIFO: the heads want outputs 1, 1, 1 and 3, so output 1 serves one input and output 3 serves one: **2 packets** leave, although outputs 0 and 2 are idle and wanted by packets behind the heads. With virtual output queues, input 0 sends to 1, input 1 to 0, input 2 to 2 and input 3 to 3: **4 packets**.

```python
# dead.py: Problem C. Channel dependency graphs and head-of-line blocking.
import itertools, random, sys
sys.setrecursionlimit(100000)

def has_cycle(deps):                        # deps: set of (channel, next channel)
    adj = {}
    for a, b in deps: adj.setdefault(a, []).append(b); adj.setdefault(b, [])
    color = {}
    def dfs(u):
        color[u] = 1
        for v in adj[u]:
            if color.get(v) == 1 or (v not in color and dfs(v)): return True
        color[u] = 2; return False
    return any(u not in color and dfs(u) for u in adj)

def one_way_ring(n, dateline):              # every destination 2 .. n-1 hops ahead; channel = (from node, virtual channel)
    deps = set()
    for s in range(n):
        for hops in range(2, n):
            ch, vc, cur = [], 0, s
            for _ in range(hops):
                ch.append((cur, vc))
                if dateline and cur == n - 1: vc = 1          # this hop crossed the link n-1 -> 0
                cur = (cur + 1) % n
            deps |= set(zip(ch, ch[1:]))
    return deps
for n in (4, 8):
    for dl in (False, True):
        d = one_way_ring(n, dl); print("one-way ring of %d, dateline %-5s: %2d dependencies, cycle: %s" % (n, dl, len(d), has_cycle(d)))

def grid(k, d, wrap, two_vcs):              # dimension-order, shortest-path routing on a mesh or torus of k^d nodes
    chans, deps = set(), set()
    for s in itertools.product(range(k), repeat=d):
        for t in itertools.product(range(k), repeat=d):
            if s == t: continue
            cur, prev = s, None
            for dim in range(d):
                vc = 0                                         # each dimension starts below its dateline
                while cur[dim] != t[dim]:
                    f = (t[dim] - cur[dim]) % k if wrap else t[dim] - cur[dim]
                    sign = 1 if f > 0 and (not wrap or f <= k - f) else -1
                    nxt = cur[:dim] + ((cur[dim] + sign) % k if wrap else cur[dim] + sign,) + cur[dim + 1:]
                    ch = (cur, nxt, vc if two_vcs else 0); chans.add(ch)
                    if prev: deps.add((prev, ch))
                    if two_vcs and wrap and ((sign == 1 and cur[dim] == k - 1) or (sign == -1 and cur[dim] == 0)): vc = 1
                    prev, cur = ch, nxt
    return chans, deps
for name, args in (("4x4 mesh, dimension order, 1 VC", (4, 2, False, False)), ("ring of 4, shortest path, 1 VC", (4, 1, True, False)),
                   ("4x4 torus, dimension order, 1 VC", (4, 2, True, False)), ("ring of 4, dateline, 2 VCs", (4, 1, True, True)),
                   ("4x4 torus, dimension order, dateline, 2 VCs", (4, 2, True, True))):
    c, d = grid(*args); print("%-46s channels %3d dependencies %4d cycle: %s" % (name, len(c), len(d), has_cycle(d)))

def hol(N, slots=20000):                    # FIFO input queues, always full; each output serves one random contender per slot
    random.seed(7); head = [random.randrange(N) for _ in range(N)]; served = 0
    for _ in range(slots):
        want = {}
        for i, d in enumerate(head): want.setdefault(d, []).append(i)
        for d, ins in want.items():
            w = random.choice(ins); head[w] = random.randrange(N); served += 1
    return served / (N * slots)
for N in (2, 4, 8, 16, 32, 64): print("N=%2d FIFO input-queued switch, saturated: %.3f of line rate" % (N, hol(N)))
print("limit 2 - sqrt(2) = %.3f" % (2 - 2 ** 0.5))
```

Output:

```
one-way ring of 4, dateline False:  4 dependencies, cycle: True
one-way ring of 4, dateline True :  5 dependencies, cycle: False
one-way ring of 8, dateline False:  8 dependencies, cycle: True
one-way ring of 8, dateline True : 13 dependencies, cycle: False
4x4 mesh, dimension order, 1 VC                channels  48 dependencies   68 cycle: False
ring of 4, shortest path, 1 VC                 channels   8 dependencies    4 cycle: True
4x4 torus, dimension order, 1 VC               channels  64 dependencies   96 cycle: True
ring of 4, dateline, 2 VCs                     channels   9 dependencies    4 cycle: False
4x4 torus, dimension order, dateline, 2 VCs    channels  72 dependencies  104 cycle: False
N= 2 FIFO input-queued switch, saturated: 0.749 of line rate
N= 4 FIFO input-queued switch, saturated: 0.659 of line rate
N= 8 FIFO input-queued switch, saturated: 0.622 of line rate
N=16 FIFO input-queued switch, saturated: 0.601 of line rate
N=32 FIFO input-queued switch, saturated: 0.593 of line rate
N=64 FIFO input-queued switch, saturated: 0.590 of line rate
limit 2 - sqrt(2) = 0.586
```

### Problem D

**(a)** `k` = 32: hosts `k³/4` = **8,192**; edge switches 32 × 16 = **512**, aggregation **512**, core (16)² = **256**, so **1,280** switches; links 3 × 8,192 = **24,576**; equal-cost paths between pods (`k/2`)² = **256**; full bisection 4,096 links × 100 Gb/s = **409.6 Tb/s** per direction. For 30,000 hosts: `k³/4 ≥ 30,000` needs `k ≥ 49.3`, so **`k` = 50** (31,250 hosts, 3,125 switches); `k` = 48 holds only 27,648.

**(b)** Down: 40 × 25 = 1,000 Gb/s; up: 8 × 100 = 800 Gb/s; **1.25 to 1**. If all send off the rack, each gets 800 ÷ 40 = **20 Gb/s** of its 25. Barroso's example (all links the same rate): 40 to 8 is **5 to 1**, so each server gets **20 %** of its line rate.

**(c)** Probability of no collision `8! ÷ 8⁸` = 40,320 ÷ 16,777,216 = **0.24 %**; expected idle uplinks `8 × (7/8)⁸` = **2.75**; the simulation gives 2.59 flows on the busiest uplink on average, so a flow there runs at about **39 %** of its rate. Incast: 32 × 128 KB = 4.19 MB; at 25 Gb/s (3.125 GB/s) the ideal time is **1.342 ms**; with one 200 ms timeout, 201.3 ms, a goodput of **0.67 %**; the buffer to absorb the burst is 31 × 128 KB = **4.06 MB**.

**(d)** PUE 1.2: facility **24 MW**, overhead 4 MW, **16.7 %** of the total. PUE 1.5: **30 MW**, overhead 10 MW, **33.3 %**. Difference 6 MW × 8,760 h = 52,560 MWh = **52.56 GWh** a year, **$4.20 million** at $0.08 per kWh. A 30 MW feed supports 25 MW of IT at PUE 1.2 (**50,000 servers**) and 20 MW at 1.5 (**40,000**): 25 % more.

**(e)** In series: `0.9995²⁰` = **99.005 %**, which is 87 hours of downtime a year. Replicated twice: each service is down only when both copies are, `1 - 0.0005²` = 0.99999975; for 20 services **99.9995 %**, about **2.6 minutes** a year. The optimistic assumption is **independence**: copies in one rack or on one power feed, running the same software version and deployed at the same moment, fail together, so replication helps only across independent failure domains.

```python
# dc.py: Problem D. Fat-tree counts, ECMP, incast, PUE and availability.
import math, random
from collections import deque

def fat_tree(k):                            # hosts, edge, aggregation and core switches wired as in section 2.4
    h = k // 2; adj = {}
    def link(a, b): adj.setdefault(a, set()).add(b); adj.setdefault(b, set()).add(a)
    for p in range(k):
        for e in range(h):
            for x in range(h): link(('host', p, e, x), ('edge', p, e))
            for a in range(h): link(('edge', p, e), ('agg', p, a))
        for a in range(h):
            for c in range(h): link(('agg', p, a), ('core', a, c))
    return adj
def shortest(adj, s, t):                    # (hops, number of shortest paths)
    d = {s: 0}; n = {s: 1}; q = deque([s])
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in d: d[v] = d[u] + 1; n[v] = 0; q.append(v)
            if d[v] == d[u] + 1: n[v] += n[u]
    return d[t], n[t]
print("(a) built and counted")
for k in (4, 6, 8):
    adj = fat_tree(k); c = {}
    for v in adj: c[v[0]] = c.get(v[0], 0) + 1
    h = ('host', 0, 0, 0)
    print("k=%d: hosts %3d, switches %3d (%d edge + %d agg + %d core), links %3d; shortest paths same edge %s, same pod %s, other pod %s" %
          (k, c['host'], c['edge'] + c['agg'] + c['core'], c['edge'], c['agg'], c['core'], sum(map(len, adj.values())) // 2,
           shortest(adj, h, ('host', 0, 0, 1)), shortest(adj, h, ('host', 0, 1, 0)), shortest(adj, h, ('host', 1, 0, 0))))
k = 32
print("k=32 from the formulas: hosts %d, switches %d (edge+agg %d, core %d), links %d, paths between pods %d, bisection %.1f Tb/s per direction" %
      (k**3 // 4, 5 * k * k // 4, k * k, (k // 2)**2, 3 * k**3 // 4, (k // 2)**2, k**3 // 8 * 100e9 / 1e12))
kk = next(k for k in range(2, 200, 2) if k**3 // 4 >= 30000)
print("smallest even k with at least 30,000 hosts: %d -> %d hosts, %d switches (k=48 gives %d)" % (kk, kk**3 // 4, 5 * kk * kk // 4, 48**3 // 4))

print("(b) oversubscription")
print("25 Gb/s x 40 down, 100 Gb/s x 8 up: %.2f : 1; off-rack share per server %.0f Gb/s of 25" % (40 * 25 / (8 * 100), 8 * 100 / 40))
print("10 Gb/s x 40 down, 10 Gb/s x 8 up: %.0f : 1; off-rack share per server %.0f%% of line rate" % (40 * 10 / (8 * 10), 100 * 8 * 10 / (40 * 10)))

print("(c) ECMP and incast")
print("P(8 flows on 8 links, no collision) = %.4f%%; expected empty links %.2f" % (100 * math.factorial(8) / 8**8, 8 * (7 / 8)**8))
random.seed(1); T = 100000; busiest = 0
for _ in range(T):
    load = [0] * 8
    for _ in range(8): load[random.randrange(8)] += 1
    busiest += max(load)
print("simulated: busiest uplink carries %.2f of 8 flows on average -> a flow there gets %.0f%% of its rate" % (busiest / T, 100 * T / busiest))
N, R, rate = 32, 128 * 1024, 25e9 / 8
ideal = N * R / rate
print("incast: %d x %d KB at 25 Gb/s: ideal %.3f ms; with one 200 ms timeout %.1f ms, goodput %.2f%%; buffer (N-1) R = %.2f MB" %
      (N, R // 1024, ideal * 1e3, (ideal + 0.2) * 1e3, 100 * ideal / (ideal + 0.2), (N - 1) * R / 1e6))

print("(d) PUE")
for pue in (1.2, 1.5):
    print("PUE %.1f: 20 MW IT -> facility %.1f MW, overhead share %.1f%%; on a 30 MW feed IT = %.1f MW = %d servers at 500 W" % (pue, 20 * pue, 100 * (pue - 1) / pue, 30 / pue, 30e6 / pue / 500))
d = 20 * (1.5 - 1.2) * 8760
print("difference %.0f MWh = %.2f GWh a year = $%.2f M at $0.08/kWh; servers ratio %.2f" % (d, d / 1e3, d * 1e3 * 0.08 / 1e6, (30e6 / 1.2) / (30e6 / 1.5)))

print("(e) availability")
a = 0.9995**20; r = (1 - 0.0005**2)**20
print("20 services in series at 99.95%%: %.4f%% -> %.1f hours a year down" % (100 * a, (1 - a) * 8760))
print("each replicated twice (independent): per service %.8f; all 20 %.5f%% -> %.1f minutes a year" % (1 - 0.0005**2, 100 * r, (1 - r) * 525600))
```

Output:

```
(a) built and counted
k=4: hosts  16, switches  20 (8 edge + 8 agg + 4 core), links  48; shortest paths same edge (2, 1), same pod (4, 2), other pod (6, 4)
k=6: hosts  54, switches  45 (18 edge + 18 agg + 9 core), links 162; shortest paths same edge (2, 1), same pod (4, 3), other pod (6, 9)
k=8: hosts 128, switches  80 (32 edge + 32 agg + 16 core), links 384; shortest paths same edge (2, 1), same pod (4, 4), other pod (6, 16)
k=32 from the formulas: hosts 8192, switches 1280 (edge+agg 1024, core 256), links 24576, paths between pods 256, bisection 409.6 Tb/s per direction
smallest even k with at least 30,000 hosts: 50 -> 31250 hosts, 3125 switches (k=48 gives 27648)
(b) oversubscription
25 Gb/s x 40 down, 100 Gb/s x 8 up: 1.25 : 1; off-rack share per server 20 Gb/s of 25
10 Gb/s x 40 down, 10 Gb/s x 8 up: 5 : 1; off-rack share per server 20% of line rate
(c) ECMP and incast
P(8 flows on 8 links, no collision) = 0.2403%; expected empty links 2.75
simulated: busiest uplink carries 2.59 of 8 flows on average -> a flow there gets 39% of its rate
incast: 32 x 128 KB at 25 Gb/s: ideal 1.342 ms; with one 200 ms timeout 201.3 ms, goodput 0.67%; buffer (N-1) R = 4.06 MB
(d) PUE
PUE 1.2: 20 MW IT -> facility 24.0 MW, overhead share 16.7%; on a 30 MW feed IT = 25.0 MW = 50000 servers at 500 W
PUE 1.5: 20 MW IT -> facility 30.0 MW, overhead share 33.3%; on a 30 MW feed IT = 20.0 MW = 40000 servers at 500 W
difference 52560 MWh = 52.56 GWh a year = $4.20 M at $0.08/kWh; servers ratio 1.25
(e) availability
20 services in series at 99.95%: 99.0047% -> 87.2 hours a year down
each replicated twice (independent): per service 0.99999975; all 20 99.99950% -> 2.6 minutes a year
```

---

## Cheat sheet

- **Cost of a message:** `T = α + n/β`, `n_half = αβ`. Measured on loopback in the VM: `α` 16.8 µs across two CPUs, 5.3 µs on one; `β` about 9 GB/s for 16 KiB to 256 KiB; `n_half` about 150 KB (43 to 56 KB on one CPU); the line fails above 256 KiB (1 MiB takes 2.5 times the prediction). Batch small messages. The ladder: L1 0.4 to 1.3 ns, line from another core 56 to 102 ns, DRAM 146 to 149 ns, PCIe DMA read 547 ns, switch (assumed) 400 ns, futex wake 1.7 or 10 µs, loopback TCP 5 or 17 µs, light 4.9 µs per km.
- **Topology metrics:** degree, links, diameter, average distance, bisection width. `N` = 64: ring 2/64/32/16.25/2; 8 × 8 mesh 4/112/14/5.33/8; 8 × 8 torus 4/128/8/4.06/16; hypercube or 4 × 4 × 4 torus 6/192/6/3.05/32. Mesh: diameter `2(k - 1)`, bisection `k`, average `2k/3`; torus: `k`, `2k`; hypercube: `d`, `N/2`. A 4 × 4 torus is the 4-cube. Crossbar `N²` crosspoints; butterfly one path per pair; tree bisection 1; fat tree `N/2`.
- **Fat tree (k-ary):** hosts `k³/4`, switches `5k²/4`, links `3k³/4`, `(k/2)²` core switches and equal-cost paths between pods (`k/2` within a pod, 1 under a switch). `k` = 48: 27,648 hosts, 2,880 switches; `k` = 64: 65,536 and 5,120. Leaf-spine from `R`-port switches: `R²/2` hosts, `3R/2` switches. Oversubscription is a ratio of capacities (40 servers on 8 uplinks, same rate: 5 to 1). Clos: non-blocking if `m ≥ 2n - 1`, rearrangeable if `m ≥ n`.
- **Routing:** XY (dimension order) is deterministic and deadlock-free on a mesh; on 8 × 8 the busiest-channel load is 2.03 (uniform, throughput 0.49), 7.00 (transpose, 0.14), 4.00 (bit complement, 0.25). Valiant: 4.06, 0.25 for any permutation, paths doubled. Adaptive routing reorders and needs an escape channel. ECMP hashes flows: 8 flows on 8 uplinks, 99.76 % collide, busiest has 2.59 flows (a flow at 39 %), 2.75 uplinks idle.
- **Switching and flow control:** `T_SF = H(t_r + L/b)`, `T_CT = H t_r + L/b`, difference `(H - 1) L/b`; NoC example 42 against 22 cycles; datacentre 9,000 B at 10 Gb/s over 5 switches 38 against 9.2 µs. Wormhole holds channels while blocked. Credits: buffer = bandwidth × credit round trip (4 flits on chip; 100 Gb/s over 100 m is 12.2 KB per VC; 1 km 122 KB). VCs: HOL relief, deadlock avoidance, traffic classes. FIFO input-queued switch saturates at 58.6 % (0.749, 0.659, 0.622, 0.601, 0.593, 0.590 for `N` = 2 to 64); use virtual output queues.
- **Deadlock:** a cycle in the channel dependency graph (Dally and Seitz). XY on a mesh: acyclic (48 channels, 68 dependencies). Ring or torus with one VC: cycle. Fixes: forbid turns (turn model, west-first), a dateline with 2 VCs, an escape VC (Duato). Protocol deadlock: separate virtual networks per message class (request, forward, response). PFC deadlock, pause storms and HOL in lossless Ethernet; DCQCN keeps PFC rare.
- **On chip and in the building:** a mesh replaced the ring in Skylake-SP (28 cores on a 30-tile mesh, 36 stops; vertical then horizontal routing; ring 9.26 average hops against 4.0 for 36 stops, bisection 2 against 6). Jupiter: Clos of commodity switches, over 1 Pb/s, centralised control. Incast: 47 servers gave about 3 % goodput with a 200 ms floor; 48 × 256 KB into 10 Gb/s is 10.07 ms ideal, 4.8 % with one timeout. RDMA: lossless needed, 1 µs class latency, DCQCN. All-reduce `2S/B`: 4.4 ms over NVLink and 40 ms over a 400 Gb/s port for 1 GB.
- **WSC:** PUE = facility ÷ IT; Google fleet 1.09 against an industry 1.54 (Uptime Institute 2025, as quoted by Google): overhead 83 % less, total 29 % less; 10 MW of IT is 10.9 against 15.4 MW, 39.4 GWh a year, $2.76 M at $0.07 per kWh (assumed); a 10 MW feed hosts 41 % more servers at 1.09. Failure: 1.2 to 16 restarts a server a year, disks above 4 % a year (10,000 disks: 400 a year); four nines is 52.6 minutes; 100 components at 99.9 % in series 90.5 %; tail 1 - 0.99^N (63 % at 100). The 5-switch network is about 3.5 µs one way against 17 µs of kernel stack; a 200 ms timeout is 5,000 times a whole 41 µs request.
- **Everything here was either measured in this VM with a short program (the loopback ping-pong: `α`, `β`, `n_half`, placement), computed by scripts (topology metrics by breadth-first search and exhaustive bisection, fat-tree counts by building the network, channel loads, channel dependency graphs, ECMP hashing, the input-queued switch, incast, PUE and availability arithmetic), reused from tutorials 14, 19, 25 and 26 (core-to-core, DRAM and wake-up timings), taken from published sources linked in the text (Jupiter, the Google efficiency page, Barroso et al., the incast, DCQCN, RDMA-at-scale and TPU v4 papers, the Skylake-SP mesh report, Karol et al.), or assumed and labelled so (400 ns per switch, 300 m of cable, 700 W and 500 W servers, electricity prices).** No switch, NIC or data centre was measured, the VM has no path to another host to time, and the classic results (Clos, Valiant, Dally and Seitz, the turn model, Duato) are quoted from the textbooks.

**Next:** tutorial 30, Hardware security (a skim: side channels and cache attacks, Spectre and Meltdown and the cost of their mitigations, Rowhammer, constant-time code, NX, ASLR and pointer authentication, trusted execution and confidential VMs, secure boot). Say "next" for it, or "skip" to go to tutorial 31, ARM against x86 and the end-to-end walkthroughs.
