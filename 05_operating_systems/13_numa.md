# OS 13: NUMA: placement, policy and diagnosis

*Part of the [Operating systems plan](00_plan.md) · Time: about 8 hours (5 reading and experiments, 3 problems) · You need: [OS 11](11_virtual_memory.md) (demand paging: a page is allocated at its first touch), [OS 12](12_virtual_memory_and_tlb.md) (reading `/proc` memory files, huge pages) and [CO 19](../04_computer_organization/19_cache_coherence.md) (coherence traffic between cores)*

This replaces an earlier, shorter note. NUMA, non-uniform memory access, is the fact that on a multi-socket server (and on some single-socket chiplet parts) memory is attached to a particular part of the machine, so the same load costs more or less depending on which core issues it and where the page sits. The performance consequences are the standard interview material for server roles: an application scales to the core count of one socket and then goes flat; everything is 40 percent slower and nothing looks wrong; a data plane loses throughput when its NIC interrupts land on the wrong node. The questions come in a fixed set. What is NUMA and how large is the penalty? Where does a page go, and what decides it (first touch, policy)? What do `numactl`, `mbind` and interleaving do and when is each right? What does automatic NUMA balancing do? How do you diagnose remote access? And what does the answer look like when the machine is a single socket with chiplets?

**An honest limit, stated first.** The machine these programs run on has **one NUMA node**. Remote latency and bandwidth cannot be measured on it, and this tutorial does not pretend to. Three things are measured here: the real kernel interface for placement (policies, page queries and migration, the `numa_maps` file and the per-node counters, which all work on a one-node machine and show what they would show on a larger one), the local costs of this machine (DRAM latency and bandwidth), and the interface errors that tell you a node does not exist. The remote penalty comes from published measurements that were fetched and are cited. The comparison of placement policies is a **cost model**: arithmetic with stated inputs, some measured here and some assumed, and it is labelled as a model wherever it appears.

By the end you can:

- say what NUMA is and quote the penalty with its source (a published measurement of a two-socket Intel Xeon Gold 6248 system: local DRAM about 80 ns, other socket about 138 to 149 ns, a ratio of about 1.7 to 1.9; for an AMD EPYC 7702 system in four-nodes-per-socket mode: local about 110 ns, other socket about 203 to 218 ns);
- read a distance matrix and say what it is (firmware's relative hint, 10 means local) and what it is not (a measurement: the 2.1 of a typical two-socket matrix against a measured ratio of about 1.7);
- state the rule for where a page lands (the node chosen by the policy in force when the page is allocated, which is by default the node of the CPU that first touches it) and write the right and wrong initialisation of a large array;
- use the four policies (default or local, bind, preferred, interleave) through `numactl` and through `mbind` and `set_mempolicy`, and verify the result with `numa_maps` and the counters (measured: 256 touched pages gave `numa_hit` +256 and `local_node` +256; `bind:0` and `interleave:0` appeared in `numa_maps`; binding to a node that does not exist failed with `EINVAL`);
- predict, with the cost model, why a serial initialisation capped a 16-thread bandwidth-bound loop at 36 percent of its demand where per-node initialisation reached 91 percent, and why interleaving sits between them;
- explain what automatic NUMA balancing does and when to switch it off;
- diagnose remote access from `numastat`, `/proc/PID/numa_maps` and `perf`, and design the shared-nothing per-node layout that avoids it.

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs, 8 GiB of memory and one NUMA node, as root. `libnuma` (the shared library, without headers) is installed and the programs call it through prototypes they declare themselves; `numactl` and `numastat` are not installed, so their output is described, not shown. The policy semantics, the automatic-balancing description and the cost figures come from the kernel documents on NUMA memory policy and on the `numa_balancing` sysctl (both fetched) and from a published measurement paper and an AMD whitepaper (both fetched; see Sources, where the limits of what the fetched pages said are stated). `numactl` options, `perf` event names and the `numastat` field meanings are from memory. Facts from general knowledge are marked "from memory".

**Data and timing.** The interface outputs (`numa_maps` lines, counter deltas, error codes) are exact. The local latency and bandwidth timings were taken twice and both runs are shown; they vary between runs by 10 percent or more at some sizes, which the text notes. The cost model is deterministic arithmetic from its inputs.

---

## 1. What NUMA is and what it costs

### 1.1 Why memory is not uniform

A multi-socket server has memory controllers in each socket, and each controller serves its own DIMMs. A core reads its own socket's memory directly (**local**) and the other socket's memory across a socket-to-socket link (UPI on Intel, Infinity Fabric on AMD, from memory), which adds the link's latency and shares its limited bandwidth. The operating system sees each such region as a **NUMA node**: a set of CPUs and the memory attached to them. The same structure appears inside one socket on chiplet parts, where memory controllers sit on separate dies (section 5).

### 1.2 The distance matrix

Firmware describes the topology in a table (ACPI SLIT, from memory) that the kernel exposes as a row of numbers per node in `/sys/devices/system/node/nodeN/distance`, printed by `numactl --hardware`. **10 means local**, and larger numbers are relative costs, so a typical two-socket machine reads:

```text
node   0   1
  0:  10  21
  1:  21  10
```

The convention reads 21 as "about 2.1 times local". It is a **hint from the firmware, not a measurement**: the published measurement quoted below found about 1.7 on a machine whose firmware would plausibly report 21. Use the matrix for ordering (which node is nearer) and for a first estimate, and measure when the number matters. This machine's matrix is a single row:

```text
nodes online                       0
nodes possible                     0
node0 cpulist                      0-1
node0 distance row                 10
kernel.numa_balancing              0
eth0 numa_node                     (cannot read /sys/class/net/eth0/device/numa_node)
node0 meminfo:
  Node 0 MemTotal:        5073128 kB
  Node 0 MemFree:         4375332 kB
node0 numastat:
  numa_hit 7092299
  numa_miss 0
  numa_foreign 0
  interleave_hit 2090
  local_node 7092299
  other_node 0
automatic balancing counters in /proc/vmstat:
  numa_pte_updates 0
  numa_huge_pte_updates 0
  numa_hint_faults 0
  numa_hint_faults_local 0
  numa_pages_migrated 0
libnuma: numa_available 0, max node 0, configured nodes 1, configured cpus 2
libnuma: numa_distance(0,0) = 10, node of cpu 0 = 0, node of cpu 1 = 0
libnuma: node 0 size 4954 MiB, free 4272 MiB
```

The one-row matrix (`10`), the CPU list `0-1` and the single online node are exactly what a one-node machine reports. A line deserves a note: the per-node `MemTotal` is **5,073,128 kB** while `/proc/meminfo` reports 8,216,668 kB for the whole machine. All 64 memory blocks are online and attached to node 0, so I do not know why the node figure is lower (a memory balloon in the hypervisor is a possible cause, not checked); this is a virtual-machine artefact and not something to expect on a physical server. `libnuma` agrees with the files: one node, two CPUs, `numa_distance(0, 0)` is 10. The network interface reports no NUMA node at all (`/sys/class/net/eth0/device/numa_node` does not exist for a virtual device); on a physical server that file is how you find which node a NIC hangs off (section 4.4).

### 1.3 How large is the penalty

The figures below are from a published measurement paper on two server processors (fetched; page summary, see Sources): a two-socket system of Intel Xeon Gold 6248 (Cascade Lake) with sub-NUMA clustering, and a two-socket system of AMD EPYC 7702 (Rome) with four nodes per socket.

| Access | Intel Xeon Gold 6248 | AMD EPYC 7702 (NPS4) |
| --- | --- | --- |
| Local DRAM | about 80 ns | about 110 ns |
| Another node, same socket | about 86 ns | about 115 to 144 ns |
| The other socket | about 138 to 149 ns | about 203 to 218 ns |
| Other socket against local | about 1.7 to 1.9 | about 1.85 to 2.0 |

The ratios are computed from those quoted latencies. The same source reports that bandwidth is also affected (the other socket is reached through a link of limited capacity). Two points to take from it. **The penalty is moderate, not catastrophic, per access**: about 1.7 to 2 times, not 10. That is exactly why NUMA problems hide, since they look like "everything is 40 percent slower", not like a stall. And **it is not the whole cost**: when many threads hit the same remote node, the link and the memory controller saturate (section 3), and the loss is in bandwidth.

### 1.4 The local costs of this machine

The model in section 3 needs a local latency and a local bandwidth, and measuring them here also shows how a machine's memory looks before NUMA enters. The program chases a pointer through a random cycle of 64-byte lines (every load depends on the previous, so nothing overlaps), with 4 KiB pages and with huge pages, and reads a 512 MiB buffer sequentially with one and with two threads:

```c
static double chase_ns(size_t bytes, int huge)               /* one dependent load per 64-byte line, random cyclic order */
{
    size_t n = bytes / 64;
    char *a = region(bytes, huge);
    size_t *perm = malloc(n * sizeof *perm);
    for (size_t i = 0; i < n; i++) perm[i] = i;
    unsigned long s = 88172645463325252UL;
    for (size_t i = n - 1; i > 0; i--) {                      /* Sattolo: one cycle through all lines */
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        size_t j = s % i, t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (size_t i = 0; i < n; i++) *(size_t *)(a + perm[i] * 64) = perm[(i + 1) % n] * 64;
    free(perm);
    size_t pos = 0; long hops = 20000000;
    double t0 = now();
    for (long i = 0; i < hops; i++) pos = *(size_t *)(a + pos);
    double dt = now() - t0;
    if (pos == 1) puts("");
    munmap(a, bytes);
    return dt / hops * 1e9;
}
```

```text
pointer chase over    1 MiB:    7.7 ns per load with 4 KiB pages,    8.2 ns with huge pages
pointer chase over    8 MiB:   60.8 ns per load with 4 KiB pages,   52.5 ns with huge pages
pointer chase over   64 MiB:  161.0 ns per load with 4 KiB pages,  137.7 ns with huge pages
pointer chase over  512 MiB:  223.4 ns per load with 4 KiB pages,  163.9 ns with huge pages
sequential read bandwidth, 1 thread:    13.7 GB/s
sequential read bandwidth, 2 threads:   23.7 GB/s
```

```text
pointer chase over    1 MiB:   14.0 ns per load with 4 KiB pages,    6.4 ns with huge pages
pointer chase over    8 MiB:   61.0 ns per load with 4 KiB pages,   60.8 ns with huge pages
pointer chase over   64 MiB:  159.7 ns per load with 4 KiB pages,  121.1 ns with huge pages
pointer chase over  512 MiB:  225.1 ns per load with 4 KiB pages,  160.4 ns with huge pages
sequential read bandwidth, 1 thread:    12.3 GB/s
sequential read bandwidth, 2 threads:   27.7 GB/s
```

Reading it: a 1 MiB region fits the cache and a load costs a few nanoseconds; at 8 MiB it is about 52 to 61 ns (the cache is overflowing); at 512 MiB it is **223.4 / 225.1 ns with 4 KiB pages and 163.9 / 160.4 ns with huge pages** (first run, second run): the difference between them is the TLB walk, the same effect as in OS 12. The huge-page figure is the better estimate of the DRAM latency itself and is what the model uses as "local". It is higher than the 80 to 110 ns of the physical servers above because this is a virtual machine on a shared host (the guest's walk is longer, CO 15 section 6.5). Sequential read bandwidth is 13.7 / 12.3 GB/s with one thread and 23.7 / 27.7 GB/s with two, so a second thread roughly doubles the bandwidth: this guest's memory is not saturated at two threads, which is the opposite of the situation in the model below, where many threads share one node.

---

## 2. Where a page lands

### 2.1 The rule

**A page is placed on the NUMA node chosen by the memory policy in force when the page is allocated.** Allocation happens at the first touch (OS 11 section 1.4: `malloc` and `mmap` only reserve address space), so with the default policy the page lands on the node of the CPU that executes the first touch. The kernel document describes it in those terms (placement is decided when a page is allocated, typically at fault time, using the policy in force at that moment; it does not use the word "first touch", which is the name everyone else uses). The consequence is the central NUMA bug:

```cpp
// WRONG on a NUMA machine: the constructor and std::fill touch every page from the main thread, so all memory lands on its node.
std::vector<float> data(N);
std::fill(data.begin(), data.end(), 0.0f);

#pragma omp parallel for
for (size_t i = 0; i < N; ++i) process(data[i]);      // threads on the other node are remote forever
```

```cpp
// RIGHT: raw storage (malloc reserves address space only), then each worker first-touches the part it will use.
float *data = (float *)malloc(N * sizeof *data);

#pragma omp parallel for schedule(static)
for (size_t i = 0; i < N; ++i) data[i] = 0.0f;         // parallel first touch

#pragma omp parallel for schedule(static)
for (size_t i = 0; i < N; ++i) process(data[i]);       // same partition, so the same thread, so local
```

(These two fragments are illustrative and were not compiled or run here: there is no second node to show the difference. The wrong version uses `std::vector<float> data(N)`, which value-initialises every element from the constructing thread and so touches all the pages from one node; that is why the right version uses raw storage. It is a real trap: a vector does not give you untouched memory.) The two loops must use the **same schedule**: if thread 3 initialises one range and processes another, the first-touch placement is for the wrong thread.

### 2.2 Policies and scopes

The kernel's policy interface (kernel document, fetched) has four modes that matter and two scopes:

- **Default, or local:** allocate on the node of the CPU running the thread. After boot this is the system default.
- **`MPOL_BIND`:** allocate only from the listed nodes (choosing, among them, the one with free memory nearest the CPU). If they are full, the allocation fails or reclaims, it does not spill elsewhere.
- **`MPOL_PREFERRED`:** try one node first, then others in order of distance. The soft form.
- **`MPOL_INTERLEAVE`:** spread pages round-robin over the listed nodes at page granularity (for anonymous memory, the node follows from the page's offset in the mapping).
- Newer modes exist (preferred-many, weighted interleave with per-node weights in `/sys/kernel/mm/mempolicy/weighted_interleave/`), useful for memory tiers.

**Task policy** (`set_mempolicy`) covers all of a task's later allocations and is **inherited across `fork` and `exec`**. **VMA policy** (`mbind`) covers one address range, applies only to anonymous pages (and to copy-on-write pages of private file mappings), and is shared by threads and inherited across `fork` but not `exec`. Policies affect **pages allocated after they are installed**; pages already placed stay where they are, unless migration is requested. A **cpuset** (the container and cgroup mechanism for restricting CPUs and nodes) takes priority over any policy: a policy whose nodes do not intersect the cpuset's nodes is invalid.

### 2.3 The policy interface, run

`libnuma` has no headers here, and the raw system calls are the ground truth beneath it anyway. The program wraps `mbind`, `get_mempolicy` and `move_pages` directly. Three helpers: one installs a VMA policy, one asks the kernel which node holds a given page (`get_mempolicy` with `MPOL_F_NODE | MPOL_F_ADDR`), one prints a mapping's line from `/proc/self/numa_maps`, which shows the policy and the page count per node.

```c
static long sys_mbind(void *a, unsigned long len, int mode, unsigned long mask, unsigned long maxnode, unsigned flags)
{ return syscall(SYS_mbind, a, len, mode, &mask, maxnode, flags); }

static int node_of(void *addr)                          /* the node holding the page at addr, or -errno */
{
    int node = -1;
    if (syscall(SYS_get_mempolicy, &node, NULL, 0, addr, MPOL_F_NODE | MPOL_F_ADDR) != 0) return -errno;
    return node;
}

static int node_of(void *addr)                          /* the node holding the page at addr, or -errno */
{
    int node = -1;
    if (syscall(SYS_get_mempolicy, &node, NULL, 0, addr, MPOL_F_NODE | MPOL_F_ADDR) != 0) return -errno;
    return node;
}

static void maps_line(void *a, const char *label)       /* the policy as /proc/self/numa_maps prints it */
{
    FILE *f = fopen("/proc/self/numa_maps", "r"); char line[512]; char pre[32];
    snprintf(pre, sizeof pre, "%lx ", (unsigned long)a);
    while (fgets(line, sizeof line, f)) if (!strncmp(line, pre, strlen(pre))) { line[strcspn(line, "\n")] = 0; printf("  %s: %s\n", label, line + strlen(pre)); }
    fclose(f);
}
```

Output of the whole program:

```text
task policy at start: default
node of an untouched page: 0 (negative is -errno)
after touching 256 pages: node of first page 0, numa_hit +256, local_node +256, numa_miss +0
  numa_maps: default anon=256 dirty=256 active=0 N0=256 kernelpagesize_kB=4
mbind BIND to node 0: rc 0
  numa_maps: bind:0 anon=256 dirty=256 active=0 N0=256 kernelpagesize_kB=4
mbind BIND to node 1 (does not exist here): rc -1, Invalid argument
mbind PREFERRED node 1: rc -1, Invalid argument
mbind INTERLEAVE over {0}: rc 0, interleave_hit +256 for 256 pages
  numa_maps: interleave:0 anon=256 dirty=256 active=0 N0=256 kernelpagesize_kB=4
move_pages one page to node 0: rc 0, status 0
move_pages one page to node 1 (does not exist here): rc -1, No such device
move_pages query with no target: rc 0, page is on node 0
set_mempolicy BIND {0} for this task: rc 0, task policy now bind
child after fork inherits task policy: bind
```

Reading it line by line:

- **The task policy starts as `default`**, and a page that has not been touched is reported on node 0 (the node a fault would use). After touching 256 pages, **`numa_hit` rose by 256, `local_node` by 256 and `numa_miss` by 0**. The counters are the per-node record of every allocation: `numa_hit` counts allocations that landed on the node the policy asked for, `numa_miss` those that landed here although another node was wanted, `numa_foreign` the reverse (wanted here, placed elsewhere), `local_node` allocations made while a process ran on this node's CPUs and `other_node` those made while it ran on another node (the meanings are from memory; this machine can only show the first two kinds, which is why `other_node` and the miss counters stay 0). On a real server the interesting signals are the other three.
- **`numa_maps` shows `default`, `bind:0`, `interleave:0`** as the policy of each region and `N0=256` as 256 pages on node 0. This file is the first thing to read for a running process: it lists every mapping, its policy and how its pages are split across nodes, which is exactly the evidence needed to say "this data is all on node 0".
- **Binding or preferring node 1 failed with `EINVAL`**, and **`move_pages` to node 1 failed with `ENODEV`**: the node does not exist. On a real two-node machine the same calls succeed; an `EINVAL` from `mbind` in production usually means a node number outside the cpuset or the online set.
- **Interleaving over `{0}` is allowed** and counted: `interleave_hit` rose by 256 for 256 pages (over one node, interleave is the same as local, but the counter shows the policy was applied).
- **`move_pages` with no target node is a query**: it reports where each page is. With a target it migrates, and that is also how tools relocate memory after a process has moved.
- **After `set_mempolicy(MPOL_BIND, {0})` the child created by `fork` reports `bind`**: the task policy is inherited, as the document says.

`mbind` can also request migration of existing pages (`MPOL_MF_MOVE`) when the policy is installed, which is how a process re-places data that was first touched in the wrong place, at the price of copying it.

---

## 3. A cost model of placement

### 3.1 What the model is

Because there is no second node, the effect of a placement policy cannot be run here. It can be computed. The model has two nodes with 8 threads each. A thread's accesses are split across nodes according to the policy. Two quantities follow:

- **Latency-bound code** (pointer chasing, dependent loads) pays the average latency: a local access costs `L`, a remote one `r * L`.
- **Bandwidth-bound code** (streaming over big arrays) is limited by capacity: each node's memory controller serves at most `C` GB/s in total, and the link between the nodes carries at most `R` GB/s in each direction. The model scales every thread by the same factor so that no resource is over capacity, so the total is the demand times that factor.

The function that does it, in the program (`share[i][j]` is the fraction of node `i`'s accesses that go to memory on node `j`):

```c
static void run(const struct policy *p, double L, double r, double d, double C, double R)
{
    double lat_sum = 0, node_load[NODES] = {0}, link_load[NODES][NODES] = {{0}};
    for (int i = 0; i < NODES; i++)
        for (int j = 0; j < NODES; j++) {
            double f = p->share[i][j];
            lat_sum += TPN * f * (i == j ? L : r * L);
            node_load[j] += TPN * d * f;
            if (i != j) link_load[j][i] += TPN * d * f;      /* data flows from node j to node i */
        }
    double avg_lat = lat_sum / (NODES * TPN);
    double s = 1;
    for (int j = 0; j < NODES; j++) if (node_load[j] > C && C / node_load[j] < s) s = C / node_load[j];
    for (int i = 0; i < NODES; i++) for (int j = 0; j < NODES; j++) if (i != j && link_load[i][j] > R && R / link_load[i][j] < s) s = R / link_load[i][j];
    double demand = NODES * TPN * d;
    printf("%-34s avg latency %6.1f ns (%.2fx local)   bandwidth-bound total %6.1f GB/s of %6.1f demanded (%.0f%%)\n",
           p->name, avg_lat, avg_lat / L, s * demand, demand, 100 * s);
}
```

### 3.2 The inputs, and which are measured

`L` is the local latency of section 1.4 (huge pages, 512 MiB: **163.9 ns**, measured here), and the per-thread demand `d` is the one-thread bandwidth of section 1.4 (**13.7 GB/s**, measured). The remote ratio `r` is 1.7, the Intel figure of the published paper. The node bandwidth `C` = 100 GB/s and the link capacity `R` = 40 GB/s per direction are **assumed round numbers** of the right order for a server socket; they are not measured and not from a source. Every result depends on them, so section 3.4 changes them.

### 3.3 The result

```text
inputs: local latency 163.9 ns, remote ratio 1.70, demand 13.7 GB/s per thread, controller 100 GB/s per node, link 40 GB/s per direction; 8 threads on each of 2 nodes
one thread first-touches everything avg latency  221.3 ns (1.35x local)   bandwidth-bound total   80.0 GB/s of  219.2 demanded (36%)
each node first-touches its own part avg latency  163.9 ns (1.00x local)   bandwidth-bound total  200.0 GB/s of  219.2 demanded (91%)
interleave over both nodes         avg latency  221.3 ns (1.35x local)   bandwidth-bound total  160.0 GB/s of  219.2 demanded (73%)
each node's threads, wrong node's data avg latency  278.6 ns (1.70x local)   bandwidth-bound total   80.0 GB/s of  219.2 demanded (36%)
```

(A model, not a measurement.) Read the four policies:

- **One thread first-touches everything** (the wrong pattern of section 2.1): all memory is on node 0, so node 0's 8 threads are local and node 1's 8 are remote. The latency is only 1.35 times local on average (half the threads are fully remote), but the bandwidth-bound total is **36 percent of demand**. Node 0's controller serves 16 threads and the link carries all of node 1's traffic; with the assumed link of 40 GB/s the link is the limit (8 threads of 14 GB/s want 110 GB/s across it).
- **Each node first-touches its own part**: every access is local, latency 1.00 of local, and the total is **91 percent of demand** (the controllers are the limit: each node's 8 threads want 110 GB/s against 100). This is the right design.
- **Interleave** over both nodes: half of every thread's accesses are remote, so latency is the same 1.35 times local as the serial-init case, but the traffic is balanced across both controllers and both link directions, so the bandwidth total is **73 percent**: much better than serial first-touch, worse than matching data to threads.
- **Each node's threads use the other node's data** (a deliberately bad placement, for scale): every access is remote, latency 1.70, bandwidth 36 percent.

So the ranking holds for both measures: matched local placement beats interleave, interleave beats concentrating everything on one node, and the worst case is data on the wrong node. The ranking is what the model is for; the exact percentages depend on the assumed `C` and `R`.

### 3.4 Sensitivity

Two reruns show which conclusions are robust. With a **faster link** (`R` = 80 GB/s):

```text
inputs: local latency 163.9 ns, remote ratio 1.70, demand 13.7 GB/s per thread, controller 100 GB/s per node, link 80 GB/s per direction; 8 threads on each of 2 nodes
one thread first-touches everything avg latency  221.3 ns (1.35x local)   bandwidth-bound total  100.0 GB/s of  219.2 demanded (46%)
each node first-touches its own part avg latency  163.9 ns (1.00x local)   bandwidth-bound total  200.0 GB/s of  219.2 demanded (91%)
interleave over both nodes         avg latency  221.3 ns (1.35x local)   bandwidth-bound total  200.0 GB/s of  219.2 demanded (91%)
each node's threads, wrong node's data avg latency  278.6 ns (1.70x local)   bandwidth-bound total  160.0 GB/s of  219.2 demanded (73%)
```

the serial-first-touch case improves from 36 to 46 percent (it is still limited by node 0's one controller) and **interleave rises to the same 91 percent as the matched placement**: if the link is wide enough, spreading the data costs no bandwidth. And with a **remote ratio of 2.1**, the firmware-style figure instead of the measured 1.7:

```text
inputs: local latency 163.9 ns, remote ratio 2.10, demand 13.7 GB/s per thread, controller 100 GB/s per node, link 40 GB/s per direction; 8 threads on each of 2 nodes
one thread first-touches everything avg latency  254.0 ns (1.55x local)   bandwidth-bound total   80.0 GB/s of  219.2 demanded (36%)
each node first-touches its own part avg latency  163.9 ns (1.00x local)   bandwidth-bound total  200.0 GB/s of  219.2 demanded (91%)
interleave over both nodes         avg latency  254.0 ns (1.55x local)   bandwidth-bound total  160.0 GB/s of  219.2 demanded (73%)
each node's threads, wrong node's data avg latency  344.2 ns (2.10x local)   bandwidth-bound total   80.0 GB/s of  219.2 demanded (36%)
```

the bandwidth totals do not change (they depend on `C` and `R`, not on `r`) but the latency penalties grow: serial first-touch and interleave rise to 1.55 times local and the all-remote case to 2.10. The lesson is the one an interviewer wants: **for latency-bound code the penalty is the ratio times the remote share; for bandwidth-bound code the question is whether the traffic is balanced across controllers and links**, and a placement that is fine for one kind can be bad for the other.

---
## 4. Controlling placement in practice

### 4.1 `numactl` and the three useful policies

For a whole process, `numactl` sets the task policy before `exec` (options from memory; `numactl` is not installed on this machine, so these were not run):

```bash
numactl --hardware                                   # nodes, sizes, the distance matrix
numactl --cpunodebind=0 --membind=0 ./app            # strict: CPUs and memory on node 0; fail rather than go remote
numactl --cpunodebind=0 --preferred=0 ./app          # soft: prefer node 0, spill to others if it is full
numactl --interleave=all ./app                       # spread pages round-robin over all nodes
```

The choice between them follows from the model of section 3:

- **`--membind` with matching CPUs** is for data that one set of threads owns: every access is local, and the price is a hard failure (or reclaim) when the node is full.
- **`--preferred`** is the same intent without the hard failure, and the right default when you are unsure of capacity.
- **`--interleave`** is for **shared data read by threads on every node** (a routing table, a lookup index, a shared cache): every node gets the same average cost instead of making one node fast and the rest slow, and the load is balanced over every controller and link, as the model's 73 percent against 36 percent shows. "Predictable beats locally optimal when access is uniform." It is the answer more often than people expect.

Inside a program, the same choices are `set_mempolicy` (task), `mbind` (a range) and the `numa_alloc_onnode`, `numa_alloc_interleaved` and `numa_run_on_node` wrappers of `libnuma` (names from memory; section 2.3 shows the raw calls working).

### 4.2 Pin the threads, or first touch means nothing

First-touch only gives locality if the thread that touched the page **keeps running near it**. If the scheduler migrates a worker to the other node after initialisation, every access is remote from then on. A NUMA-aware program therefore pins its threads (`sched_setaffinity`, `pthread_setaffinity_np`, or `numactl --cpunodebind`), usually one thread per core, and initialises data from the pinned thread that will use it. The Linux scheduler does try to keep a task near its memory, and automatic balancing (section 4.3) moves pages or tasks toward each other, but a latency-sensitive design should not depend on either.

### 4.3 Automatic NUMA balancing

Linux can adjust placement by itself. With `kernel.numa_balancing` on, the kernel periodically unmaps pages of a process so that the **next access faults**, uses the fault to learn which thread (and so which node) touches the page, and decides at fault time whether to migrate the page to the accessing node (kernel document, fetched). The values: 0 disabled, 1 normal balancing across nodes, 2 memory tiering (placing hot pages in faster memory types); they combine. The document is plain about the cost: the sampling and the faults add overhead, the locality gain should usually offset it but there is "no universal guarantee", and it advises disabling it when the workload is already bound to nodes.

On this machine `kernel.numa_balancing` is 0 and the counters that measure it (`numa_pte_updates`, `numa_hint_faults`, `numa_pages_migrated` in `/proc/vmstat`) are all 0 (section 1.2 output: balancing is off, and there is only one node to move between). On a server where it is on, these counters tell you what it is doing: many hint faults and migrations in a steady state mean the placement keeps changing, which costs CPU time and latency spikes (a fault per sampled page, a page copy per migration). The decision rule: leave it on for general-purpose multi-threaded programs whose threads wander; turn it off for a data plane or a database that pins its threads and places its memory explicitly.

### 4.4 The architecture that avoids the problem

The robust design is **shared-nothing per node**, and it generalises beyond NUMA:

```text
per NUMA node:
  its own worker threads, pinned to that node's cores
  its own memory pools, first-touched by those threads
  its own NIC receive queues (interrupt affinity set to that node)
  its own shard of the data
cross-node traffic: only aggregated results, rarely
```

DPDK applications, Seastar-based databases and high-throughput proxies are structured this way (from memory). The question "how do you scale past the cores of one socket" has the same answer as the NUMA question: **partition so that sharing is rare, then make the rare sharing explicit.** For a NIC the device is attached to one socket's PCIe root complex, so its DMA lands in that socket's memory: put the handling threads and the buffers there. On a physical server `cat /sys/class/net/<if>/device/numa_node` names the node (a virtual NIC, as here, has no such file; section 1.2), and the interrupt affinity of its queues (`/proc/irq/N/smp_affinity`) should match.

---

## 5. Chiplets, sub-NUMA clustering and "single socket" machines

"One socket, therefore no NUMA" stopped being safe. Modern server parts expose NUMA-like structure inside a socket. AMD EPYC processors put cores in chiplets around an I/O die with the memory controllers; the firmware setting **NPS** (NUMA nodes per socket: 1, 2 or 4) chooses whether the socket appears as one node or as several, each tied to a group of memory channels (from memory; the AMD whitepaper that was fetched discusses the structure but not NPS settings). Intel's **sub-NUMA clustering** (SNC) splits a socket into two or more nodes in the same way. The measurement paper quoted in section 1.3 puts numbers on it: on the EPYC 7702 in four-nodes-per-socket mode the other nodes **inside the same socket** were about 115 to 144 ns against 110 ns local, so the intra-socket penalty is about 5 to 30 percent (the paper's text and its table disagree on the largest figure; the table's is used), small next to the 1.85 to 2.0 times of the other socket; on the Xeon Gold 6248 with clustering the other cluster in the same socket cost about 86 ns against 80. The paper's conclusion for Rome is that the I/O-die design makes placement matter even in a single socket, and that if the system is presented as one domain, pinning threads avoids the penalties; for Cascade Lake it is that the intra-socket cost of clustering is small and the other socket is the expensive step.

The practical consequences. `numactl --hardware` on a modern server often shows more nodes than sockets, so read it before assuming. The shared-nothing structure of section 4.4 is worth adopting even on one socket. And the trade between modes is the one the model shows: **more, smaller nodes** give better locality (shorter paths, less shared bandwidth) to code that is partitioned by node and cost latency and capacity to code that is not; **one big node** is simpler and uniform, at the cost of the longer average path.

A virtual machine hides all of it. The guest here sees one node and a flat memory, while the host decides which physical node backs each guest page, and the guest cannot see that decision. Cloud providers expose "vNUMA" for large instances (from memory), and a guest that is told it has two nodes should treat them as real; one that sees one should not assume the host got it right. This is the same limit CO 15 section 6.4 meets with huge pages: **measure on the machine you will run on.**

---

## 6. Diagnosing remote access

### 6.1 The shape of the symptom

The signature to recognise: throughput scales with threads **up to the core count of one socket**, then flattens or **falls** when threads spill onto the second socket. Everything is uniformly somewhat slower, with no hot spot in a profile. If a scaling curve has that shape, check NUMA before anything else; a lock or a shared cache line (CO 19) can give the same curve, which is why the check comes before the cure.

### 6.2 The instruments

| Question | Where to look |
| --- | --- |
| How is the machine laid out? | `numactl --hardware`, `lscpu`, `/sys/devices/system/node/` (section 1.2) |
| Where is this process's memory? | `/proc/PID/numa_maps` (policy and pages per node per mapping), `numastat -p PID` (from memory) |
| Is the system placing badly? | per-node `numastat` counters: `numa_miss`, `numa_foreign`, `other_node` rising (section 2.3) |
| Is the kernel migrating? | `numa_hint_faults`, `numa_pages_migrated` in `/proc/vmstat` (section 4.3) |
| Do loads go remote? | `perf stat -e node-loads,node-load-misses` (event names vary by CPU; from memory) |
| Is it coherence instead of placement? | `perf c2c` for contended cache lines and remote HITM (from memory; CO 19) |
| Which node is the NIC on? | `/sys/class/net/<if>/device/numa_node` |

The `numa_maps` line is the most direct evidence and is what section 2.3 demonstrates: a mapping whose line reads `N0=262144` while its threads run on node 1 is the bug stated in one line.

### 6.3 Discriminating the causes

"The service scales to 16 cores and stops" has three standard explanations, each with its test: **NUMA crossing** (`numa_maps` and `numastat` show remote placement; pinning and per-node first-touch fix it), **coherence on a shared line** (`perf c2c`; padding and sharding fix it), **a lock** (lock profiling or off-CPU profiling; a different design fixes it). Naming the discriminating measurement for each is what separates a Staff answer from "probably NUMA".

---
## What it costs

| Mechanism | What it costs | What it buys | Evidence |
| --- | --- | --- | --- |
| Remote access (other socket) | about 1.7 to 2.0 times the latency, shared link bandwidth | none | published: Xeon 80 against 138 to 149 ns; EPYC (NPS4) 110 against 203 to 218 ns |
| Remote access (same socket, clustered) | about 5 to 30 percent | none | published: Xeon 86 against 80 ns; EPYC 115 to 144 against 110 ns |
| Serial first touch | all memory on one node: one controller and one link serve everyone | none | model: 36 percent of demand reached |
| Per-node first touch | initialisation must be parallel and the threads pinned | all accesses local | model: 91 percent of demand reached |
| Interleave | every thread is half remote | balanced controllers and links, predictable | model: 73 percent, 1.35 times local latency |
| `mbind` with migration, `move_pages` | a copy per page | fixes a wrong placement | interface run on this machine (no second node to move to) |
| Automatic NUMA balancing | hint faults and page copies | placement that follows the threads | counters are 0 here; the kernel document states the overhead without a figure |
| One big node (NPS1) | longer average path | simplicity | from memory |

## Common mistakes

1. **Saying `malloc` places memory.** It reserves address space; the page is placed when it is first touched, by the policy in force then (section 2.1).
2. **Initialising in the main thread and computing in many.** All pages land on the main thread's node and every other node's threads are remote forever.
3. **Using `std::vector<float> v(N)` to get untouched memory.** It value-initialises every element, touching the pages from the constructing thread.
4. **Initialising and computing with different schedules.** Thread k first-touches one range and then processes another, so the placement is for the wrong thread.
5. **Not pinning threads.** First-touch locality is lost when the scheduler moves a worker to the other node.
6. **Treating the distance matrix as a measurement.** It is the firmware's relative hint (the typical 21 against a measured ratio of about 1.7).
7. **Concluding "single socket, no NUMA".** Chiplet and sub-NUMA modes expose several nodes in one socket; read `numactl --hardware`.
8. **Binding shared read-mostly data to one node.** Every other node's threads go remote and one controller serves all; interleave it.
9. **Interleaving data that one thread owns.** Half its accesses become remote for no benefit; place it with its owner.
10. **Leaving automatic balancing on for a program that pins and places explicitly.** It adds sampling faults and page copies to placement that is already right.
11. **Putting NIC interrupts and buffers on the wrong node.** DMA lands in the socket the device hangs off; check `device/numa_node` and the IRQ affinity.
12. **Blaming NUMA without checking coherence and locks.** The same scaling curve comes from a shared cache line or a lock; discriminate with `numa_maps`, `perf c2c` and lock profiling.

## Questions out loud

1. *What is NUMA and how big is the penalty?* Memory attached to a particular socket or chiplet; the other node costs more. Published measurements: about 1.7 to 1.9 times local latency to the other socket on a Xeon Gold 6248, about 1.85 to 2.0 on an EPYC 7702, with bandwidth also constrained by the link.
2. *Where does a page go?* The node chosen by the memory policy when the page is allocated; by default the node of the CPU that first touches it.
3. *Why is the usual parallel-array initialisation a NUMA bug?* If the main thread initialises, all pages land on its node; initialise in parallel with the same static schedule as the compute loop, from pinned threads.
4. *Explain `bind`, `preferred` and `interleave`.* Bind allocates only from the listed nodes; preferred tries one node and spills in distance order; interleave spreads pages round-robin over the nodes.
5. *When is interleave the right answer?* For large data read by threads on every node (routing tables, shared indexes): uniform average cost and balanced controllers, as in the model's 73 percent against 36 percent.
6. *What does automatic NUMA balancing do?* It samples access with hint faults and migrates pages toward the nodes that use them; it costs faults and copies, and should be disabled when the workload already places and pins explicitly.
7. *How do you see where a process's memory is?* `/proc/PID/numa_maps` (policy and pages per node per mapping), `numastat -p`, per-node counters, `move_pages` with no target as a query.
8. *What do `numa_hit`, `numa_miss`, `numa_foreign`, `local_node` and `other_node` mean?* Hit: placed where the policy asked. Miss: placed here although another node was wanted. Foreign: wanted here, placed elsewhere. Local and other: whether the allocating process ran on this node's CPUs.
9. *How do you lay out a 100 GB index across two sockets?* Shard by key and pin the threads, if access is partitionable; interleave if every thread touches everything. Say which question decides.
10. *Where should NIC interrupts go?* To the node the device hangs off (`device/numa_node`), with the receiving threads and buffers there.
11. *Does a single-socket machine have NUMA?* It can: chiplet parts with NPS or sub-NUMA clustering present several nodes; the intra-socket penalty is small (5 to 30 percent) next to the other socket.
12. *An application scales to 16 cores then stops. Ideas?* NUMA crossing, coherence on a shared line, or a lock, and the test for each (`numa_maps` and `numastat`, `perf c2c`, lock profiling).

---

## Problems

**Where to solve them.** Paper for all five. To check A and C, the cost model program (`n3_model`) takes the latency, ratio, demand and capacities on its command line and prints the same tables; its source is in section 3. On a multi-node Linux machine, `numactl --hardware`, `/proc/PID/numa_maps` and the per-node `numastat` files give the real values for D.

**How to approach them.** For A, convert each distance to a multiple of 10. For B, ask who writes the data and who reads it. For C, count which thread first-touches and which thread computes each element. For D, write the identity each pair of counters must satisfy before reading the numbers. For E, compute the load on each controller and each link direction, take the smallest capacity-to-load ratio, and multiply the demand.

### Problem A (E, entry level): the distance matrix

A two-node machine reports distances 10 (local) and 21 (other node). (a) What multiple of local cost does a remote access carry according to the matrix? (b) A thread's data is interleaved evenly over both nodes: what is its average cost as a multiple of local? (c) A published measurement of a similar machine found a remote latency 1.7 times local: if local is 80 ns, what is the matrix's estimate for the remote latency and the measured one?

### Problem B (E, entry level): choose the policy

For each case choose `membind` with CPUs bound, `preferred`, or `interleave`, and give the reason in one line. (1) A 64 GiB read-only lookup table read by 32 threads spread over 2 nodes. (2) A per-node packet-processing worker pool with its own buffers. (3) A batch job that should use node 0's memory but must not fail if it is full. (4) A shared hash table with updates from every node.

### Problem C (M, medium): first touch by reading code

A 2-node machine runs 16 threads, 8 pinned on each node, over a large array of N elements, each thread processing a contiguous 1/16 of it. Give the fraction of accesses that are remote, and the average latency for local 100 ns and a ratio of 1.7, for (a) the main thread (on node 0) initialises the whole array, then the 16 threads process their parts; (b) the array is initialised in parallel with `schedule(static)` and processed with the same schedule; (c) initialised in parallel with `schedule(dynamic)` and processed with `schedule(static)`; (d) as (b) but the threads are not pinned and after initialisation the scheduler moves each thread to a uniformly random node.

### Problem D (M, medium): read the counters

Over an interval the per-node `numastat` counters changed by: **node 0** `numa_hit` +9,000, `numa_miss` +0, `numa_foreign` +1,000, `local_node` +4,000, `other_node` +5,000; **node 1** `numa_hit` +1,000, `numa_miss` +1,000, `numa_foreign` +0, `local_node` +1,500, `other_node` +500. (a) How many pages were allocated on each node? (b) How many pages were placed on a node other than the one wanted, and what does that suggest? (c) How many pages on node 0 were allocated by processes running on node 1's CPUs, and what pattern does that suggest?

### Problem E (H, hard): the model by hand

Use the model of section 3: 2 nodes, 8 threads each, demand 10 GB/s per thread, local latency 100 ns, ratio 1.7, link 30 GB/s per direction. For node capacity C = 50 GB/s, then C = 100 GB/s, compute the bandwidth-bound total and the average latency for (i) serial first touch (all data on node 0), (ii) per-node first touch, (iii) interleave. Which policy ranks differently between the two capacities, and why?

---

## Answers

**A.** (a) 21 / 10 = 2.1 times. (b) Half local, half remote: (10 + 21) / 2 = 15.5, so 1.55 times local. (c) The matrix estimates `2.1 x 80 = 168` ns; the measurement gives `1.7 x 80 = 136` ns. The matrix overstates by about 24 percent, which is why it is used for ordering and not for numbers.

**B.** (1) **Interleave**: shared read-only data read from every node; interleaving gives every thread the same average cost and spreads the load over both controllers, where binding to one node would make the other node's 16 threads remote. (2) **`membind` with CPUs bound to the same node**: the data is owned by one set of threads, so all accesses are local; the workers should also first-touch the buffers. (3) **`preferred`**: node 0 first, spilling to another node instead of failing. (4) **Interleave**: updates come from every node and there is no owner to place the data with; interleaving gives balanced, predictable cost (a sharded design with per-node tables would be better if the key space can be partitioned).

**C.** (a) Every page is on node 0, so the 8 threads on node 1 are fully remote: the remote fraction is 8/16 = **0.5** of all accesses, and the average latency is `0.5 x 100 + 0.5 x 170 = 135 ns`. (b) The thread that initialises a range is the thread that processes it: remote fraction **0**, average latency **100 ns**. (c) With dynamic initialisation the thread that touches each chunk is arbitrary, so each page is on a uniformly random node: each thread has expected remote fraction 0.5, the average is **0.5**, **135 ns**, the same as (a) in expectation (but spread evenly over the nodes, so the controllers are balanced, which helps the bandwidth-bound case). (d) The placement is local to the thread's original node, but after a uniformly random move half the threads are on the other node: remote fraction **0.5**, **135 ns**: pinning matters as much as first touch.

**D.** (a) Pages allocated on a node are `numa_hit + numa_miss`: node 0 received `9,000 + 0 = 9,000`, node 1 received `1,000 + 1,000 = 2,000`. Check with `local_node + other_node`: node 0 `4,000 + 5,000 = 9,000`, node 1 `1,500 + 500 = 2,000`. (b) `numa_foreign` on node 0 (1,000) must equal `numa_miss` on node 1 (1,000): **1,000 pages** were wanted on node 0 and placed on node 1. The usual cause is that node 0 had no free memory and the allocator fell back, which suggests node 0 is overcommitted (or its memory is held by page cache). (c) `other_node` on node 0 is **5,000 pages**, 56 percent of its allocations, made while the allocating process ran on another node's CPUs (here node 1). The policy still wanted node 0 for them (they are `numa_hit`), so it suggests data bound or first-touched on node 0 by threads that run on node 1: the serial initialisation or `membind` to the wrong node pattern, and the pages will be remote for those threads.

**E.** Demand per thread 10, so total demand 160 GB/s. **C = 50:** (i) serial: node 0 carries 160 GB/s (ratio 50/160 = 0.3125) and the link from node 0 to node 1 carries 80 GB/s (30/80 = 0.375); the smallest ratio is 0.3125, so the total is **50 GB/s**, and the average latency is `0.5 x 100 + 0.5 x 170 = 135 ns`. (ii) per-node: each node carries 80 GB/s (50/80 = 0.625), no link traffic, so **100 GB/s** and **100 ns**. (iii) interleave: each node carries 80 GB/s (0.625); each link direction carries 40 GB/s (30/40 = 0.75); the smallest is 0.625, so **100 GB/s**, and latency 135 ns. **C = 100:** (i) serial: node 0 160 (100/160 = 0.625), link 80 (0.375), so `0.375 x 160 = ` **60 GB/s**, 135 ns. (ii) per-node: 80 per node (1.25, capped at 1), so **160 GB/s**, 100 ns. (iii) interleave: nodes 80 (1.25), link 40 (0.75), so `0.75 x 160 = ` **120 GB/s**, 135 ns. (The program's output for both capacities agrees.) **Interleave ranks differently:** with the small controller (C = 50) the controllers are the bottleneck, interleave balances them as well as the matched placement does (100 against 100 GB/s) and its latency cost is hidden; with the large controller (C = 100) the link becomes the bottleneck and interleave, whose half-remote traffic crosses it, falls to 75 percent while the matched placement (no link traffic) stays at 100 percent. The conclusion: interleave is good when memory bandwidth is the scarce resource and costs you when the link is.

---

## Cheat sheet

- NUMA: memory attached to a socket or chiplet; same-socket local, other node remote. Published: other socket about 1.7 to 1.9 times (Xeon 80 against 138 to 149 ns) and 1.85 to 2.0 times (EPYC NPS4 110 against 203 to 218 ns); same-socket other node about 5 to 30 percent.
- Distance matrix: 10 is local, others are firmware's relative hint (21 is "about 2.1"); measured can differ (1.7).
- A page goes where the **policy in force at allocation** says; default is the node of the first-touching CPU. `malloc` places nothing; `std::vector(N)` touches everything.
- Right: parallel first touch with the same static schedule as the compute loop, from pinned threads. Wrong: serial init, different schedules, unpinned threads.
- Policies: default or local, bind (strict), preferred (soft, distance order), interleave (round-robin pages). Scopes: task (`set_mempolicy`, inherited by `fork` and `exec`), VMA (`mbind`, anonymous pages, inherited by `fork` not `exec`). Cpusets override. New pages only; `MPOL_MF_MOVE` or `move_pages` migrates.
- Interface checks: `numa_maps` (policy, `N<node>=pages`), `get_mempolicy` with `MPOL_F_NODE | MPOL_F_ADDR`, `move_pages` without a target as a query; `EINVAL` for a bad node in `mbind`, `ENODEV` from `move_pages`.
- Counters per node: `numa_hit`, `numa_miss`, `numa_foreign` (foreign of one node equals miss of another), `local_node`, `other_node` (sum with hit and miss to the same allocations), `interleave_hit`.
- Model (assumed `C` and `R`): serial first touch 36 percent of demand, per-node 91, interleave 73, wrong-node 36; faster link lifts interleave to 91.
- Interleave for shared data read everywhere; membind with matched CPUs for owned data; preferred for soft intent.
- Automatic balancing: hint faults plus migration; disable when explicit placement and pinning already hold.
- Shared-nothing per node: threads, pools, NIC queues and data shard per node. NIC node: `device/numa_node`.
- Diagnose: scaling flat at one socket's core count; `numa_maps`, `numastat`, `perf c2c`, lock profiling to separate NUMA, coherence and locks.
- Chiplets and sub-NUMA modes make "one socket" several nodes; read `numactl --hardware`.

## Sources

Measured on the VM described above (one NUMA node). Fetched and used: the two kernel documents, the measurement paper and the AMD whitepaper below. The fetched pages were summarised by the fetch tool, not read in full by me, so the paper's figures are as that summary reported them, including its own note that the paper's text and its table disagree on one same-socket EPYC value (the 115 to 144 ns range takes the table); I did not check the paper's figures independently. The AMD whitepaper discusses die-level NUMA on the first-generation EPYC but did not cover the NPS settings, so the NPS description is from memory. Not fetched, and stated from memory in the text: the SLIT as the source of the distance matrix, the `numastat` counter meanings, the `numactl` and `libnuma` call names and options, the `perf` event names, interrupt affinity files, DPDK and Seastar structure, and vNUMA in clouds.

- [Linux kernel: NUMA memory policy](https://docs.kernel.org/admin-guide/mm/numa_memory_policy.html), fetched: modes, scopes, inheritance, placement at allocation time, mode flags, the system calls
- [Linux kernel: sysctl kernel (numa_balancing)](https://docs.kernel.org/admin-guide/sysctl/kernel.html), fetched: what automatic NUMA balancing does, its values and its overhead
- [Memory Performance of AMD EPYC Rome and Intel Cascade Lake SP Server Processors (arXiv 2204.03290)](https://ar5iv.labs.arxiv.org/html/2204.03290), fetched: local, same-socket and other-socket latencies for the Xeon Gold 6248 and the EPYC 7702
- [AMD: Optimizing EPYC memory with NUMA (whitepaper)](https://www.amd.com/content/dam/amd/en/documents/epyc-business-docs/white-papers/AMD-Optimizes-EPYC-Memory-With-NUMA.pdf), fetched: die-level NUMA structure of the first-generation EPYC, pinning and interleaving advice
- [OS 11: Virtual memory](11_virtual_memory.md), [OS 12: Virtual memory and the TLB in practice](12_virtual_memory_and_tlb.md), [CO 19: Cache coherence](../04_computer_organization/19_cache_coherence.md) and [CO 15: Virtual memory and the TLB](../04_computer_organization/15_virtual_memory_and_the_tlb.md), the neighbouring material
