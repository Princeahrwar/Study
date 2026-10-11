# 05 — Zero-copy networking

> Prerequisite: `04_dma.md`, `os_and_arch/12_user_kernel_boundary.md`.
> The most directly relevant tutorial for Palo Alto, Cisco and Meta infra roles.

## The copies you are trying to remove

Trace a packet through a conventional receive path and count:

```
  NIC --DMA--> kernel sk_buff        (no CPU copy: DMA)
      --copy--> userspace buffer      (copy_to_user in recv)
      --copy--> application struct    (parsing into your own layout)
```

Plus a syscall per packet, plus an interrupt (amortised by NAPI), plus sk_buff
allocation and freeing. At 10 Mpps you have 100 ns per packet total; a single
`copy_to_user` of a 1500 B frame is already a meaningful fraction of that, and
the syscall pair alone can exceed it.

So "zero-copy" is really shorthand for **removing the copy, the syscall and the
per-packet allocation together**. Solutions that remove only one rarely matter.

## The ladder, from least to most invasive

| Technique | Copies | Syscalls/pkt | Keeps kernel stack? | Difficulty |
| --- | --- | --- | --- | --- |
| `recvmmsg` batching | 1 | 1/N | yes | trivial |
| `AF_PACKET` TPACKET_V3 | 0 | ~0 | yes (raw only) | low |
| `PACKET_MMAP` TX | 0 | ~0 | yes | low |
| `io_uring` | 0-1 | ~0 | yes | medium |
| eBPF / XDP | 0 | 0 | partially | medium |
| `AF_XDP` | 0 | ~0 | bypass for selected queues | medium-high |
| DPDK / VFIO | 0 | 0 | **no** | high |

Always climb from the bottom. The interview failure mode is jumping to DPDK for a
problem that `recvmmsg` would solve.

## AF_PACKET with a mapped ring

The kernel's oldest zero-copy receive path, and still a good fit for capture and
simple filtering:

```c
/* Set up TPACKET_V3: a block-based ring shared with the kernel. */
struct tpacket_req3 req = {
    .tp_block_size = 1 << 22,           /* 4 MiB blocks */
    .tp_block_nr   = 64,
    .tp_frame_size = 2048,
    .tp_frame_nr   = (1 << 22) * 64 / 2048,
    .tp_retire_blk_tov = 60,            /* ms before a partial block is handed over */
    .tp_feature_req_word = TP_FT_REQ_FILL_RXHASH,
};

int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
int ver = TPACKET_V3;
setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver));
setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req));

uint8_t *ring = mmap(NULL, req.tp_block_size * req.tp_block_nr,
                     PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);

/* Then walk blocks, checking the ownership bit — the same descriptor
 * discipline as a DMA ring (tutorial 04). */
```

Key properties: **block-based** (V3) rather than frame-based, so the kernel hands
you a batch at a time; combine with `PACKET_FANOUT` to spread across threads by
hash, and with a classic BPF filter attached to drop uninteresting traffic before
it reaches the ring.

## XDP: run your code where the packet already is

XDP attaches an eBPF program at the earliest point in the driver, before any
`sk_buff` is allocated. Because the allocation is the expensive part, dropping a
packet in XDP is dramatically cheaper than dropping it in netfilter.

```c
/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <linux/if_ether.h>
#include <linux/ip.h>

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);   /* per-CPU: no cross-core contention */
    __uint(max_entries, 1 << 20);
    __type(key, __u32);                        /* source IPv4 */
    __type(value, __u64);                      /* packet count */
} flow_count SEC(".maps");

SEC("xdp")
int rk_filter(struct xdp_md *ctx)
{
    void *data     = (void *)(long)ctx->data;
    void *data_end = (void *)(long)ctx->data_end;

    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)          /* the verifier REQUIRES this check */
        return XDP_PASS;
    if (eth->h_proto != __constant_htons(ETH_P_IP))
        return XDP_PASS;

    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    __u32 key = ip->saddr;
    __u64 *cnt = bpf_map_lookup_elem(&flow_count, &key);
    if (cnt)
        __sync_fetch_and_add(cnt, 1);
    else {
        __u64 init = 1;
        bpf_map_update_elem(&flow_count, &key, &init, BPF_ANY);
    }

    return XDP_PASS;    /* or XDP_DROP, XDP_TX, XDP_REDIRECT */
}

char _license[] SEC("license") = "GPL";
```

The bounds checks before every dereference are not defensive style — the
**verifier rejects the program without them**. That constraint is the whole
reason eBPF is safe to run in the kernel, and explaining it correctly is a good
interview answer: the verifier proves termination and memory safety statically,
which is why there is no sandbox cost at runtime.

XDP return codes: `XDP_PASS` (into the normal stack), `XDP_DROP` (free
immediately — the cheapest drop in Linux), `XDP_TX` (bounce out the same NIC),
`XDP_REDIRECT` (to another device or to an `AF_XDP` socket).

Attach modes, in decreasing performance: **native** (in the driver), **offloaded**
(on the NIC, few devices), **generic** (after sk_buff allocation — a fallback for
testing, with most of the benefit gone).

## AF_XDP: the modern answer

`XDP_REDIRECT` into an `AF_XDP` socket gives userspace the packet with no copy
and no sk_buff, while leaving the rest of the machine's networking intact.

```
   UMEM         a big mmap'd region of fixed-size frames, registered with the kernel
   FILL ring    userspace -> kernel: "here are empty frames you may fill"
   RX ring      kernel -> userspace: "these frames now hold packets"
   TX ring      userspace -> kernel: "please transmit these frames"
   COMPLETION   kernel -> userspace: "these TX frames are free again"
```

Four single-producer/single-consumer rings, which is exactly the structure from
`os_and_arch/10`. The descriptor discipline is the same; only the names change.

Why it is usually the right choice over DPDK:

- Only the queues you bind are bypassed. Everything else still goes through the
  kernel, so `tcpdump`, `iptables` and your management plane keep working.
- No NIC driver rewrite; it works on any driver with XDP support.
- Zero-copy mode needs driver support; without it you get a copy mode that still
  removes the syscall.
- You keep the kernel's security model rather than mapping device registers into
  a userspace process.

## DPDK: full bypass

Unbind the NIC from the kernel, bind it to `vfio-pci`, map its BARs and rings into
a userspace process, and poll. Maximum performance, maximum cost.

What you give up, and you must be able to list this: the kernel network stack,
`tcpdump`, `iptables`/`nftables`, standard sockets, the kernel's routing table,
and a dedicated core spinning at 100% per queue. What you gain is complete
control of the data path and no unpredictable kernel work on your core.

DPDK's other contributions are worth knowing by name because they show up as
design patterns elsewhere: hugepage-backed mempools (tutorial 07's arithmetic),
per-core lockless rings, burst-oriented APIs that amortise everything over 32
packets at a time, and RSS/flow steering to bind queues to cores.

## Transmit-side zero-copy

- **`sendfile`** — file to socket entirely in the kernel. The classic web-server
  optimisation.
- **`splice`/`vmsplice`** — move pages between fds via a pipe without copying.
- **`MSG_ZEROCOPY`** — `send()` pins user pages instead of copying, and notifies
  completion on the error queue. Worth it above roughly 10 KiB; below that the
  pinning bookkeeping costs more than the copy. That threshold is a good concrete
  detail to know.

## Interview angles

**"How do you get to 10 Mpps on one machine?"**
Walk the ladder. Say the arithmetic first (100 ns/packet), then pick a rung, then
say what that rung gives up.

**"XDP vs DPDK?"**
XDP keeps the kernel and is selective; DPDK takes the whole device and burns
cores. Choose XDP/AF_XDP unless you need absolute control.

**"Why is XDP_DROP so much cheaper than an iptables drop?"**
It happens before sk_buff allocation, which is the dominant cost.

**"What makes eBPF safe to run in the kernel?"**
Static verification: bounded loops, no unchecked pointer arithmetic, proven
termination. Hence the mandatory bounds checks.

**"Is zero-copy always faster?"**
No. `MSG_ZEROCOPY` loses below ~10 KiB; page pinning and completion notification
have real cost. Ring setup adds complexity and ABI coupling. Say when it does not
pay.

## Exercises

1. Build the four-rung receive ladder yourself: `recvfrom`, `recvmmsg(64)`,
   `AF_PACKET` TPACKET_V3, and `AF_XDP`. Measure pps for each on the same
   traffic. This table is a portfolio artifact in its own right.
2. Write the XDP counting program above, load it with `bpftool`, and verify the
   map from userspace. Then deliberately remove a bounds check and read the
   verifier's rejection message — it is more educational than it looks.
3. Compare `XDP_DROP` against an equivalent `iptables -j DROP` rule under a flood.
   Report the pps difference and explain it in one sentence.
4. Implement an `AF_XDP` receiver with the four rings. Confirm zero-copy mode is
   actually active (it silently falls back to copy mode) and say how you checked.
5. Benchmark `send` vs `MSG_ZEROCOPY` across payload sizes from 1 KiB to 1 MiB and
   find your crossover point. Compare it to the ~10 KiB rule of thumb.
