# LX 08: The Linux networking toolbox

*Part of the [Linux plan](00_plan.md) · Time: about 5 hours · You need: [LX 03](03_processes_signals_and_proc.md) (`/proc`, namespaces), [LX 05](05_performance_methodology_and_triage.md) (triage by resource, saturation, errors) and the TCP basics from the [networking track](../07_networking/00_plan.md). This tutorial is the toolbox: which command shows which layer, what each field means, and the order in which to look when packets go missing. Every tool is run on a three machine lab built from network namespaces inside one VM, so you can rebuild all of it.*

"Users in one office cannot reach the service", "the transfer is slow but the link is idle", "connections time out after the load balancer change" and "DNS is slow" are all questions that are answered by reading counters in the right order, not by guessing. This tutorial builds a lab (section 2), then goes up and down the stack with the standard tools: `ip` for addresses, routes and neighbours (3), `ss` for sockets and their queues (4), `tcpdump` and path MTU (5), `nft` and `conntrack` (6), where loss shows up in the counters and where it does not (7), and DNS, `sysctl` and `ethtool` (8). Section 9 lists what the VM could not run and what to know about it, then come the costs, mistakes, questions and five problems.

**Seven honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs) as **root**, with iproute2 6.1.0, nftables 1.0.9, tcpdump 4.99.4, iperf3 3.16, conntrack 1.4.8, ethtool 6.7, mtr 0.95 and dig 9.18 (tools that were missing were installed with `apt`). The lab traffic goes over **`veth` pairs between namespaces**: there is **no NIC, no wire, no switch**, so latencies are 10 to 100 µs and throughput (10 to 17 Gbit/s) is **memory copy speed**, not a network. Read the shapes and the counters, not the speeds.
2. **This kernel has no `netem`, `fq`, `fq_codel`, `cake`, `sfq`, `prio`, VLAN, bonding or dummy devices** (each fails with `Specified qdisc kind is unknown` or `Unknown device type`). So **loss and delay are injected with `nft`** (a random drop rule) and **shaping is done with `tbf`** (which does work), and `netem`, VLANs and bonds are explained **from memory** in section 9.
3. **`sysctl net.netfilter.nf_conntrack_max` cannot be changed** (`Operation not permitted`: the sandbox has no `CAP_SYS_RESOURCE`), so a **full conntrack table is described, not shown**. There is **no `systemd-resolved`** (`resolvectl` is not usable) and **no route to the Internet**, so DNS is tested against a small **DNS server written for the lab**, and `mtr` and `traceroute` run inside the lab only.
4. **`ethtool` on a virtual NIC** (`virtio_net` for `eth0`, `veth` for the lab) shows few counters; the driver level statistics of a real NIC (`rx_missed_errors`, per queue drops, ring size changes) are **from memory**.
5. **A lab with no injected loss still retransmits**: unlimited `cubic` over a `veth` showed **656 retransmitted segments in 3 seconds** and the default `bbr` showed **13,278** in one probe run, because the `veth` queue overflows when the sender is faster than the receiver's softirq. Section 7 uses this to make a point; it means "zero retransmits" is **not** something to expect from a baseline without a rate limit.
6. **Single runs.** Throughput under loss varies by tens of percent between runs and the counters shown are **absolute counts since the namespace was created**, not deltas, unless the text says otherwise.
7. `ping` and `traceroute` show the **TTL and hop** of the namespace router (`ttl=63`), and `ip netns` names (`c`, `r`, `s`) and addresses (`10.1.0.0/24`, `10.2.0.0/24`) are those of the lab, not the sandbox's own network (`eth0`, `192.0.2.2/24`).

By the end you can:

- build a **routed lab of three namespaces** on one machine and say what each `ip` command changes (measured: a ping across the router showed **`ttl=63`**, one hop; with the router's second link down the client got **`Destination Net Unreachable` from `10.1.0.1`**; with its default route removed **`Network is unreachable` from its own kernel**);
- use **`ip route get`**, **`ip rule`** and **`ip neigh`** to say what the kernel will do with a packet before it is sent (measured: a policy rule on the source address moved the route to table 100 and `ip route get` showed `table 100`);
- read **`ss`** states and the **`Recv-Q` and `Send-Q`** columns for **both** listening and connected sockets (measured: a listen backlog of 1 showed `Recv-Q 2, Send-Q 1`, **3 `ListenOverflows`** and 2 clients stuck in `SYN-SENT`; a server that never read left **about 126,000 bytes in its `Recv-Q`** (125,816) and 12,120 in the client's `Send-Q`; the side that closed first held the **4 `TIME-WAIT`** sockets, the other held 0);
- read the fields of **`ss -ti`** (measured: `cwnd`, `rtt`, `retrans:0/8147` and `delivery_rate` on a busy flow) and the counters in **`nstat`**;
- **capture with `tcpdump`** and filter with **BPF** expressions (measured on one capture of 21 to 22 packets: `icmp` 10, `udp` 4, `tcp` 7 to 8, a SYN-only filter **2**, `greater 1000` **1**), and recognise a handshake, a **reset from a closed port** and an ICMP error;
- diagnose **path MTU** problems (measured: a 1000 byte link answered `Frag needed and DF set (mtu = 1000)` and `ip route get` then showed `mtu 1000`; with that ICMP message dropped a 300 KB TCP transfer **stalled for the full 5 second timeout**, and **MSS clamping** fixed it);
- write **`nft` rules** for filtering, **masquerade** and **port forwarding** and read **`conntrack -L`** (measured: the server saw the client as `10.2.0.1` behind masquerade; a per-source limit accepted **3 of 6** connections and refused 3; 60 UDP flows made **63** conntrack entries);
- tell **where packet loss is counted** (measured: injected loss at 1 in 1,000, 1 in 100 and 1 in 10 packets cut `cubic` from **17.4 Gbit/s** to about **14.0 Gbit/s, 0.79 Gbit/s and 2.79 Mbit/s**; the **interface counters showed 0 drops** while `nft`, `tc -s qdisc` and `nstat` each counted a different part of it);
- read **DNS** symptoms (measured: a dead first nameserver added **2.0 seconds** to every lookup, a single dead nameserver **9.2 seconds**) and know which `sysctl net.*` values are **per namespace** (measured: `ip_forward` was 0 on the host and 1 in the router, and `somaxconn` capped a `listen(1000000)` at **4096** and then at **64**).

**Where the numbers come from.** Six experiment scripts (`x1` to `x6`) and a problem-answer script were run in this session. Transcripts print `$ command` before each command's output, and `nse NS cmd` in a transcript means `ip netns exec NS cmd`. Numbers that are not from these runs are marked "from memory" or come from a source listed at the end.

**Data and timing.** No data sets. The lab is created and deleted by a 12 line shell function (section 2) in well under a second, and each experiment runs in a few seconds. All timings are single runs.

---

## 1. Where a packet can be changed or lost

Learn this path once and every tool in this tutorial has a place on it.

```text
 application  --write()-->  socket send buffer
                                  |
                      TCP/UDP, routing decision, (netfilter output)
                                  |
                      qdisc (queue and shaper)      <- tc
                                  |
                      driver, TX ring, NIC          <- ethtool, ip -s link
                                  |
                              the wire
                                  |
                      NIC, RX ring, driver, softirq <- ethtool -S, softnet_stat
                                  |
                      netfilter (prerouting, input or forward)  <- nft, conntrack
                                  |
                      routing, local delivery  <- ip route, ip rule
                                  |
                      socket receive buffer  --read()-->  application   <- ss, nstat
```

| Layer | Question | Tool |
| --- | --- | --- |
| addresses, links, routes, neighbours | does the kernel know how to reach it? | `ip addr`, `ip route get`, `ip neigh` |
| a hop | where does the path stop or change? | `traceroute`, `mtr`, `tracepath` |
| sockets | is a listener there, are the queues full? | `ss`, `nstat` |
| packets on the wire | what was actually sent and received? | `tcpdump` |
| filter, NAT, state | did a rule change or drop it? | `nft`, `conntrack`, `iptables` |
| queue | was it dropped by a shaper or a full queue? | `tc -s qdisc` |
| driver and NIC | errors, drops, offloads, rings | `ip -s link`, `ethtool` |
| names | does the name resolve, how fast? | `dig`, `getent hosts` |

The rule for triage is: **walk the path from where the symptom is visible, and find the first counter or capture that disagrees with the next one**. A packet visible on one interface and not on the next was lost between them.

---

## 2. A lab on one machine: network namespaces and `veth`

A **network namespace** is a private copy of the network stack: its own interfaces, addresses, routes, firewall, sockets, `sysctl net.*` values and `/proc/net`. A **`veth` pair** is two virtual Ethernet ends joined by a cable: whatever enters one comes out of the other, and each end can live in a different namespace. Three namespaces and two `veth` pairs make a **client, a router and a server**:

```text
     c (10.1.0.2) ---- rc (10.1.0.1)   r   rs (10.2.0.1) ---- s (10.2.0.2)
         net 10.1.0.0/24                           net 10.2.0.0/24
```

```bash
# net.sh: a three namespace lab on one machine:  c (10.1.0.2) -- r (10.1.0.1 | 10.2.0.1, forwards) -- s (10.2.0.2)
lab_up() {
  ip netns add c; ip netns add r; ip netns add s
  ip link add c0 type veth peer name rc; ip link add s0 type veth peer name rs
  ip link set c0 netns c; ip link set rc netns r; ip link set s0 netns s; ip link set rs netns r
  ip -n c addr add 10.1.0.2/24 dev c0; ip -n r addr add 10.1.0.1/24 dev rc
  ip -n s addr add 10.2.0.2/24 dev s0; ip -n r addr add 10.2.0.1/24 dev rs
  for n in c r s; do ip -n $n link set lo up; done
  ip -n c link set c0 up; ip -n r link set rc up; ip -n r link set rs up; ip -n s link set s0 up
  ip -n c route add default via 10.1.0.1; ip -n s route add default via 10.2.0.1
  ip netns exec r sysctl -qw net.ipv4.ip_forward=1
}
lab_down() { pkill iperf3 2>/dev/null; pkill -f "lx08_srv" 2>/dev/null; for n in c r s; do ip netns del $n 2>/dev/null; done; }
```

`ip netns add` creates a namespace, `ip link add ... type veth peer name ...` makes the pair, `ip link set X netns N` moves an end into a namespace, and **`ip netns exec N cmd`** runs any command inside it (in this tutorial, `nse N cmd`). The client and server get a default route through the router, and the router has **`net.ipv4.ip_forward=1`**, which turns a Linux box from a host into a router. Everything that follows runs on this. Cleaning up is `ip netns del`, which also removes the `veth` ends inside it. A bridge instead of a router is shown in section 3.6.

**Why this matters for an interview:** you can reproduce NAT, routing, MTU and loss problems on a laptop in minutes, which is how production network issues are usually reproduced and how network code is tested.

---


## 3. `ip`: addresses, links, routes, rules, neighbours

```text
--- 1. the lab: addresses, links, and the routing table of each namespace
$ ip netns list | sort
c (id: 0)
r (id: 1)
s (id: 2)
$ ip -n c -br addr; ip -n r -br addr; ip -n s -br addr
lo               UNKNOWN        127.0.0.1/8
c0@if19          UP             10.1.0.2/24
lo               UNKNOWN        127.0.0.1/8
rc@if20          UP             10.1.0.1/24
rs@if22          UP             10.2.0.1/24
lo               UNKNOWN        127.0.0.1/8
s0@if21          UP             10.2.0.2/24
$ ip -n c route; ip -n r route
default via 10.1.0.1 dev c0
10.1.0.0/24 dev c0 proto kernel scope link src 10.1.0.2
10.1.0.0/24 dev rc proto kernel scope link src 10.1.0.1
10.2.0.0/24 dev rs proto kernel scope link src 10.2.0.1
```

`ip -br addr` is the compact view (`-br` is brief) and the first command on any box: interface, state, addresses. The `@if10` after `c0` is the **peer's interface index**, which is how you find the other end of a `veth` (or the host side of a container's interface: `ip link` in the other namespace with that index). The routing tables show the whole design: the client has a **connected route** for its own `/24` (added by the kernel, `proto kernel scope link`) and a **default route** through the router; the router has a connected route for each side and nothing else.

### 3.1 `ip route get`: ask the kernel what it would do

```text
--- 2. ip route get: what the kernel would do with one destination
$ ip -n c route get 10.2.0.2
10.2.0.2 via 10.1.0.1 dev c0 src 10.1.0.2 uid 0
    cache
$ ip -n r route get 10.2.0.2 from 10.1.0.2 iif rc
10.2.0.2 from 10.1.0.2 dev rs
    cache iif rc
```

`ip route get DEST` runs a real route lookup and prints the chosen **next hop (`via`), device, and source address**. The second command asks it as the router would see a packet arriving **on `rc` from `10.1.0.2`**, which is the one to use when forwarding looks wrong: it says `dev rs`, so the router would send it out the right side. Use it **before** blaming a firewall. A path that `ip route get` rejects (`Network is unreachable`, section 3.5) never reaches the wire.

### 3.2 Neighbours and the TTL

```text
--- 3. a ping across the router, TTL shows the hop, then the neighbour (ARP) tables
$ ip netns exec c ping -c 2 -W 1 10.2.0.2 | grep -E "ttl|transmitted"
64 bytes from 10.2.0.2: icmp_seq=1 ttl=63 time=0.046 ms
64 bytes from 10.2.0.2: icmp_seq=2 ttl=63 time=0.051 ms
2 packets transmitted, 2 received, 0% packet loss, time 1016ms
$ ip -n c neigh; ip -n r neigh
10.1.0.1 dev c0 lladdr 6a:fa:e6:ea:88:eb REACHABLE
10.1.0.2 dev rc lladdr 6a:47:af:c2:68:85 DELAY
10.2.0.2 dev rs lladdr ea:a7:97:1f:7c:bc REACHABLE
```

`ttl=63` on the reply: the server's reply started at 64 and the router decremented it once, so **there is one router between the two**. `ip neigh` is the **ARP table** (IPv6: neighbour discovery) mapping a next hop's IP to its MAC. The states matter: `REACHABLE` (confirmed recently), `STALE` (not confirmed but usable), `DELAY` and `PROBE` (being re-checked), `FAILED` (no answer to ARP, so the **host or the link is down**, not a routing problem), and `INCOMPLETE` (waiting for a reply). Note that the client has only **one** neighbour entry, the router, because **a client only ever ARPs for its next hop**, never for a remote destination.

### 3.3 `traceroute`, `tracepath` and `mtr`

```text
--- 4. traceroute and mtr see the router as hop 1
$ ip netns exec c traceroute -n -q1 -w1 10.2.0.2 2>&1 | tail -2
 1  10.1.0.1  0.035 ms
 2  10.2.0.2  0.012 ms
$ ip netns exec c mtr -n -c 5 -r 10.2.0.2 2>&1 | tail -3 | cut -c1-70
HOST: vm                          Loss%   Snt   Last   Avg  Best  Wrst
  1.|-- 10.1.0.1                   0.0%     5    0.1   0.1   0.1   0.1
  2.|-- 10.2.0.2                   0.0%     5    0.1   0.1   0.1   0.1
```

`traceroute` sends packets with TTL 1, 2, 3... and each router that decrements TTL to 0 answers with an ICMP **time exceeded**, which names the hop. **`mtr`** repeats that and keeps per-hop loss and latency (`-r` report mode, `-n` no DNS lookups, `-c` count). Two cautions, both from memory of how it works: a hop that **does not answer** (`???` or `*`) is often just a router that rate limits or ignores ICMP, so **loss at a middle hop that does not continue to the end is not real loss**; and the path out and the path back can differ, so **trust only loss that is still present at the last hop**.

### 3.4 Policy routing: `ip rule`

```text
--- 5. policy routing: ip rule sends traffic from one source to another table
$ ip -n c rule; ip -n r rule
0:      from all lookup local
32766:  from all lookup main
32767:  from all lookup default
0:      from all lookup local
32766:  from all lookup main
32767:  from all lookup default
$ ip -n r rule
0:      from all lookup local
100:    from 10.1.0.2 lookup 100
32766:  from all lookup main
32767:  from all lookup default
$ ip -n r route show table 100
10.2.0.0/24 dev rs scope link
$ ip -n r route get 10.2.0.2 from 10.1.0.2 iif rc
10.2.0.2 from 10.1.0.2 dev rs table 100
    cache iif rc
```

Linux can consult **several routing tables**, chosen in order by **rules**: the three default rules mean "local table first, then main, then default". Adding **`ip rule add from 10.1.0.2 lookup 100`** at priority 100 sent traffic from that source to **table 100**, and `ip route get` confirmed it (`table 100` at the end of the line). This is how multi-homed hosts answer on the interface a request came in on, how a VPN client sends only some traffic through the tunnel, and how container networking steers traffic. Rules can also match `fwmark` (a number set by `nft` or `tc`), `iif`, `oif`, `uidrange` and destination. The usual mistake is **forgetting the rule is separate from the route**: a route in table 100 does nothing without a rule that points at it.

### 3.5 Counters and what failure looks like

```text
--- 6. link counters and errors: ip -s link, then the interface statistics in /proc
$ ip -n c -s link show c0
20: c0@if19: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DEFAULT group default qlen 1000
    link/ether 6a:47:af:c2:68:85 brd ff:ff:ff:ff:ff:ff link-netns r
    RX:  bytes packets errors dropped  missed   mcast
          1992      22      0       0       0       0
    TX:  bytes packets errors dropped carrier collsns
          2322      31      0       0       0       0
$ ip netns exec c cat /proc/net/dev | cut -c1-90
Inter-|   Receive                                                |  Transmit
 face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs dr
    lo:       0       0    0    0    0     0          0         0        0       0    0
    c0:    1992      22    0    0    0     0          0         0     2322      31    0
```

`ip -s link` gives the per-interface counters: bytes, packets, **errors** (bad frames: CRC, length), **dropped** (the driver or stack discarded the packet: queue full, no handler, bad VLAN), **missed** (the NIC had no buffer: ring full) on the receive side, and **carrier** and **collisions** on transmit. The same data is in `/proc/net/dev`. On a real NIC, `ethtool -S` breaks them down by cause (section 8.4). Counters are cumulative since boot: **always take two readings and subtract**.

```text
--- 7. what happens when a link goes down, and when the route is removed
$ ip netns exec c ping -c 1 -W 1 10.2.0.2 2>&1 | grep -E "From|errors"
From 10.1.0.1 icmp_seq=1 Destination Net Unreachable
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms
$ ip netns exec c ping -c 1 -W 1 10.2.0.2 2>&1 | grep -E "unreachable"
ping: connect: Network is unreachable
$ ip -n c route get 10.2.0.2 2>&1
RTNETLINK answers: Network is unreachable
```

The three common failures look different, and each tells you where to look:

- **A link or route is missing on the router**: the router answers with ICMP **`Destination Net Unreachable`**, and the client's ping says `From 10.1.0.1 ... Unreachable`. The error **comes from a named device**, so you know which hop gave up.
- **The client has no route at all**: `ping: connect: Network is unreachable`, with **no packet sent**, and `ip route get` fails with the same words. This is a configuration problem on the machine you are on.
- **Silence** (neither of the above): a drop with no ICMP, a firewall, a missing return route (Problem A), or a dead host. Only `tcpdump` on both sides of the suspect hop separates these (section 5).

### 3.6 A bridge instead of a router

```text
--- 8. a bridge instead of a router: two namespaces on one segment, then the forwarding database
$ ip netns exec b1 ping -c 2 -W 1 10.5.0.2 | grep -E "transmitted"
2 packets transmitted, 2 received, 0% packet loss, time 1006ms
$ bridge link show | grep -E "b1p|b2p" | cut -c1-80
24: b1p@if25: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 master br0 state forwar
26: b2p@if27: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 master br0 state forwar
$ bridge fdb show br br0 | grep -E "master br0" | grep -v permanent | cut -c1-60
ae:bd:ee:d9:2d:a4 dev b1p master br0
f2:e4:df:58:38:07 dev b2p master br0
```

A **Linux bridge** is a software switch: ports (`b1p` and `b2p`) are enslaved to `br0` with `master br0`, and it **learns source MACs per port** into a forwarding database, which `bridge fdb show` prints (the two non-permanent entries, one per port, are what it learned from the ping). Two namespaces on the same bridge are on **one L2 segment** (`10.5.0.0/24`) and talk without any router; `bridge link` shows each port's STP state (`forwarding`). This is what Docker's `docker0` and most Kubernetes node setups are: containers connect to a bridge with a `veth` pair, and the host routes or NATs out of it. **Not run here:** VLAN sub-interfaces and bonding (this kernel lacks both); from memory, `ip link add link eth0 name eth0.10 type vlan id 10` adds an 802.1Q tag, and a bond (`type bond mode active-backup` or `802.3ad`) joins NICs for failover or aggregation, with state in `/proc/net/bonding/bond0`.

---

## 4. `ss`: sockets, states and queues

`ss` replaced `netstat`. It reads sockets from the kernel directly and, with `-i`, `-m` and `-e`, shows the **internal TCP state** that decides throughput. The options to remember: **`-t` TCP, `-u` UDP, `-l` listening, `-a` all, `-n` no name resolution, `-p` owning process, `-i` TCP internals, `-m` socket memory, `-s` summary**.

```text
--- 1. a normal server: LISTEN, ESTAB, and the same connection seen from both ends
$ nse s ss -tan | grep -E "State|5001"
State  Recv-Q Send-Q Local Address:Port Peer Address:Port Process
LISTEN 0      128          0.0.0.0:5001      0.0.0.0:*
ESTAB  0      0           10.2.0.2:5001     10.1.0.2:55188
$ nse c ss -tan | grep -E "State|5001"
State Recv-Q Send-Q Local Address:Port  Peer Address:PortProcess
ESTAB 0      0           10.1.0.2:55188     10.2.0.2:5001
$ nse s ss -tlnp sport = :5001 | cut -c1-100
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 0      128          0.0.0.0:5001      0.0.0.0:*    users:(("python3",pid=8860,fd=3))
```

The server shows a **`LISTEN`** socket on `0.0.0.0:5001` (every address) and an **`ESTAB`** socket for the one connection; the client shows the **same connection from the other side**, with the roles of local and peer addresses swapped and a random **ephemeral port** (here from 32768 to 60999, the `ip_local_port_range` of section 8.2). `-p` shows which process owns a socket (root only for other users' sockets).

### 4.1 What `Recv-Q` and `Send-Q` mean

The two columns mean **different things for listening and connected sockets**, and mixing them up is a classic mistake:

| Socket | `Recv-Q` | `Send-Q` |
| --- | --- | --- |
| `LISTEN` | connections **completed the handshake and waiting for `accept()`** (the accept queue) | the **maximum** size of that queue (the backlog, capped by `somaxconn`) |
| `ESTAB` | bytes received that the application **has not yet read** | bytes sent that the peer **has not yet acknowledged** (or not sent yet) |

A growing `Recv-Q` on an `ESTAB` socket means **the application is slow**; a growing `Send-Q` means **the network or the peer is slow** (or its receive window is shut). A listener with `Recv-Q` close to `Send-Q` is about to overflow.

### 4.2 Filters

```text
--- 2. filters: state, port and address expressions
$ nse s ss -tan state established "( sport = :5001 )" | wc -l
4
$ nse s ss -tn dst 10.1.0.2/32 | tail -n +2 | wc -l
3
$ nse s ss -tan "( sport = :5001 and dst = 10.1.0.2 )" | head -2 | cut -c1-90
State Recv-Q Send-Q Local Address:Port Peer Address:Port Process
ESTAB 0      0           10.2.0.2:5001     10.1.0.2:55202
```

`ss` has a filter language: `state established`, `dst 10.1.0.2/32` (address with mask), and expressions such as `( sport = :5001 and dst = 10.1.0.2 )` using `sport`, `dport`, `src`, `dst` and the operators `=`, `!=`, `<`, `>`. The counts include the header line, so 4 lines for **three** connections. The parentheses and spaces matter and must be quoted for the shell. State names for the filter: `established`, `syn-sent`, `syn-recv`, `fin-wait-1`, `fin-wait-2`, `time-wait`, `close-wait`, `last-ack`, `listening`, `closing`, and the groups `connected` and `synchronized`.

### 4.3 `TIME-WAIT` and who holds it

```text
--- 3. TIME-WAIT: the side that closes first keeps the socket for 60 s
$ nse s ss -tan state time-wait | tail -n +2 | wc -l; nse c ss -tan state time-wait | tail -n +2 | wc -l
0
4
$ nse c ss -s | head -4
Total: 1
TCP:   18 (estab 0, closed 18, orphaned 2, timewait 4)
```

After a normal close, **the side that closed first** keeps the socket in **`TIME-WAIT`** (60 seconds on Linux, fixed in the kernel) so late packets of the old connection cannot corrupt a new one with the same address pair. The client closed first here and holds **4**, the server **0**. Consequences: a **client** that opens and closes many short connections to one server uses up its **ephemeral ports** (about 28,000 in the default range, so roughly 470 new connections per second to one destination at steady state, from the arithmetic 28,232 divided by 60 seconds), and a **server** that closes first (HTTP `Connection: close`) pays it instead. Cures, in order: **reuse connections** (keep alive, pools), let the **client** close, widen the port range, `net.ipv4.tcp_tw_reuse` (2 here, meaning enabled for loopback only), and **never** rely on `tcp_tw_recycle` (removed from the kernel; from memory).

### 4.4 The accept queue overflows

```text
--- 4. accept queue overflow: listen backlog 1, never accept, 6 clients
$ nse s ss -ltn sport = :5002
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 2      1            0.0.0.0:5002      0.0.0.0:*
$ nse s ss -tan | grep 5002 | awk "{print \$1}" | sort | uniq -c
      2 ESTAB
      1 LISTEN
$ nse s nstat -az TcpExtListenOverflows TcpExtListenDrops | tail -2
TcpExtListenOverflows           3                  0.0
TcpExtListenDrops               3                  0.0
$ nse c ss -tan | grep 5002 | awk "{print \$1}" | sort | uniq -c
      2 ESTAB
      2 SYN-SENT
```

A server called `listen()` with a **backlog of 1** and then **never called `accept()`**, and six clients connected. The listener's line shows **`Recv-Q 2`, `Send-Q 1`**: two completed connections waiting (the queue holds backlog+1) against a maximum of 1. Two of the clients are `ESTAB` (the kernel completed their handshake and queued them), and the next ones cannot be queued, so **their handshake is not completed**: they sit in `SYN-SENT` and retry (the helper opens them one at a time with a 1 second timeout, so the snapshot shows 2 `ESTAB` and 2 `SYN-SENT`). The counters say it: **`TcpExtListenOverflows` 3 and `TcpExtListenDrops` 3** (on a host, `nstat -az TcpExtListenOverflows` is the alarm to put on every server).

What you see from outside is **connections that succeed slowly or time out under load while the server's CPU is idle**, because the application is busy and not accepting. Fixes: make the server `accept()` faster (more threads or an event loop), **raise the backlog** passed to `listen()` **and** `net.core.somaxconn` (the smaller of the two wins; section 8.2), and check `tcp_max_syn_backlog` for half open connections. Problem C does it.

### 4.5 A receiver that does not read

```text
--- 5. a server that never reads: Send-Q on the client, Recv-Q on the server, zero window
$ cat /tmp/lx08_push.txt
sent before blocking: 134 KiB
$ nse c ss -tn dport = :5003 | cut -c1-70
State Recv-Q Send-Q Local Address:Port  Peer Address:PortProcess
ESTAB 0      12120       10.1.0.2:40712     10.2.0.2:5003
$ nse s ss -tn sport = :5003 | cut -c1-70
State Recv-Q Send-Q Local Address:Port Peer Address:Port Process
ESTAB 125816 0           10.2.0.2:5003     10.1.0.2:40712
$ nse c ss -tmn dport = :5003 | tail -1 | cut -c1-110
         skmem:(r0,rb131072,t0,tb518400,f3432,w12952,o0,bl0,d0)
```

The client wrote until it could write no more (about 134 to 157 KiB before blocking). The **server's `Recv-Q` holds about 126,000 bytes**: data the kernel received and acknowledged but the application never read, against a **receive buffer (`rb`) of 131,072 bytes** shown by `-m`. When that buffer is full the receiver advertises a **zero window** and the sender's bytes pile up in **its own `Send-Q`** (12,120 here, with 518,400 bytes of send buffer, `tb`, still free to fill on the client side). So the picture of "**`Recv-Q` large on the server, `Send-Q` large on the client**" is a **slow reader**, not a network problem, and `tcpdump` would show `win 0`. `-m` prints the **`skmem`** block: `r` receive memory in use, `rb` its limit, `t` transmit memory in use, `tb` its limit, `f` forward alloc, `w` write queue bytes, `d` drops.

### 4.6 `ss -ti`: the fields that explain throughput

```text
--- 6. ss -ti on a busy connection: the fields that explain throughput (iperf3, 2 s)
$ nse c ss -tin dport = :5201 | tail -1 | tr -s " \t" " " | fold -s -w 88
 bbr wscale:10,10 rto:204 rtt:0.05/0.029 mss:1448 pmtu:1500 rcvmss:536 advmss:1448
cwnd:278 ssthresh:649 bytes_sent:2997885133 bytes_retrans:11795608
bytes_acked:2985688374 segs_out:2078054 segs_in:45959 data_segs_out:2078052
bbr:(bw:32821333096bps,mrtt:0.001,pacing_gain:1,cwnd_gain:2) send 64407040000bps
lastrcv:1512 pacing_rate 32493119768bps delivery_rate 10860000000bps delivered:2076291
busy:1504ms unacked:278 retrans:0/8147 dsack_dups:7518 reordering:300 reord_seen:79
rcv_space:14480 rcv_ssthresh:64088 notsent:3779280 minrtt:0.001 snd_wnd:19891200
```

On a busy connection (an `iperf3` flow, cut from 3 seconds) `ss -ti` prints the TCP stack's internal state. The fields to know: `rtt:0.05/0.029` **smoothed RTT and its variation in ms**; `mss` and `pmtu` the segment and path MTU sizes; **`cwnd:278`** the congestion window in segments (with `mss`, the amount allowed in flight); `ssthresh` the slow start threshold; **`bbr:(...)`** or `cubic` the congestion control and its model (here `bw`, the estimated bandwidth, and `mrtt`); **`retrans:0/8147`** segments currently being retransmitted and the **total** ever retransmitted; `bytes_retrans` and `dsack_dups` (duplicates the receiver reported seeing, so the retransmission was unneeded); `delivery_rate` and `pacing_rate` the measured and the allowed rates; `unacked` segments in flight; `notsent` bytes queued but not sent; `snd_wnd` the peer's advertised window; `rcv_space` the receive autotuning size; and `busy`, `rwnd_limited` and `sndbuf_limited` (when present) the milliseconds the sender was limited by the receiver's window or its own buffer.

**How to read it:** if `cwnd` is small and `retrans` grows, the path is **losing packets**; if `rwnd_limited` is large, the **receiver** is the limit; if `sndbuf_limited` is large, **raise `tcp_wmem`** or the application's buffer; if none of those and `busy` is low, **the application is not sending fast enough**. Note that here a flow with **no injected loss** still shows 8,147 total retransmissions: section 7.1 explains why.

**`nstat`** prints the kernel's protocol counters (the same as `/proc/net/snmp` and `/proc/net/netstat`, section 8.5); `nstat -az` includes zeroes and absolute values, and plain `nstat` prints the **change since its last call**, which is the form to use when watching.

---

## 5. `tcpdump`: seeing the packets

```text
--- 1. the TCP handshake and close on the router, as flags (S SYN, . ACK, P PSH, F FIN, R RST)
$ cat /tmp/lx08_t1.txt | sed -E "s/10\\.1\\.0\\.2\\.[0-9]+/10.1.0.2.PORT/"
IP 10.1.0.2.PORT > 10.2.0.2.5001: Flags [S]
IP 10.2.0.2.5001 > 10.1.0.2.PORT: Flags [S.]
IP 10.1.0.2.PORT > 10.2.0.2.5001: Flags [.]
IP 10.1.0.2.PORT > 10.2.0.2.5001: Flags [P.], length 5
IP 10.2.0.2.5001 > 10.1.0.2.PORT: Flags [.]
IP 10.1.0.2.PORT > 10.2.0.2.5001: Flags [F.]
IP 10.2.0.2.5001 > 10.1.0.2.PORT: Flags [F.]
```

`tcpdump -nn -i IF 'FILTER'` captures on an interface (`-nn` no name or port lookups, `-t` no timestamps here, `-c N` stop after N, `-w file` save, `-r file` read). The capture on the router's client side shows a complete life of a connection, with the **flags** as letters: **`S`** SYN, **`S.`** SYN-ACK (the dot is ACK), **`.`** a bare ACK, **`P.`** data pushed (`length 5` is the five bytes `hello`), **`F.`** FIN, **`R`** reset. The first three lines are the **three way handshake**; the sequence numbers and windows have been removed here for width, and `-S` prints absolute sequence numbers while the default prints relative ones.

```text
--- 2. a connection to a port with no listener gets a reset
client: [Errno 111] Connection refused
IP 10.1.0.2.PORT > 10.2.0.2.6000: Flags [S]
IP 10.2.0.2.6000 > 10.1.0.2.PORT: Flags [R.]
```

A SYN to a port **with no listener** is answered at once with **`R.`** (reset and ack), and the client gets `Connection refused`. **This is the signature of a closed port**: an immediate reset. A **firewall drop** looks different: the SYN is sent again after 1, 2, 4 seconds with **no answer at all** (the `timed out` result in section 6 and Problem B), and a **reject** with `tcp reset` looks like a closed port again. So **refused means something answered; timed out means nothing did**.

### 5.1 BPF filters, applied to a saved capture

```text
--- 3. one capture file, then filters applied when reading it
filter: (none)                                             22
filter: icmp                                               10
filter: udp                                                4
filter: tcp                                                8
filter: tcp[tcpflags] & tcp-syn != 0                       2
filter: tcp[tcpflags] & (tcp-syn|tcp-fin) != 0 and dst port 5001 2
filter: greater 1000                                       1
filter: arp                                                0
```

One capture of 21 to 22 packets, read back with different **BPF** (Berkeley Packet Filter) expressions. Capture filters run **in the kernel**, so a good filter keeps a busy interface from overloading `tcpdump`; the same expressions work with `-r`. The ones to know: `host A`, `net 10.1.0.0/24`, `port 80`, `src` and `dst` qualifiers, `tcp`, `udp`, `icmp`, `arp`, **`greater N`** and `less N` (packet length), the operators `and`, `or`, `not`, and byte level tests such as **`tcp[tcpflags] & tcp-syn != 0`** (SYN set: it matched the 2 packets of the handshake, the SYN and the SYN-ACK). A common set is `'tcp[tcpflags] & (tcp-syn|tcp-fin|tcp-rst) != 0'` to see only connection setup and teardown, and `'icmp[icmptype] != icmp-echo and icmp[icmptype] != icmp-echoreply'` to see only ICMP errors. Do not capture on a busy production interface without `-c`, a filter, and `-s` (snap length) set: capture to a **file** and read it later, or with `-W` and `-C` for a ring of files (from memory).

**Capture on both sides of the suspect hop.** A packet present on the first interface and absent on the second was dropped **between** them (Problem B). On the host that is the question for `nft` and `ip_forward`.

```text
--- 4. a request for a destination that gets an ICMP error back
$ ip netns exec c ping -c 1 -W 1 10.77.0.1 2>&1 | grep -E "From|transmitted"
From 10.1.0.1 icmp_seq=1 Destination Net Unreachable
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms
```

A destination the router has no route to produces an **ICMP error from the router**, visible to the sender as `Destination Net Unreachable` (type 3, code 0). ICMP errors are how the network **tells** you what is wrong, which is why **blocking all ICMP is a bug** (next section).

### 5.2 Path MTU discovery

An Ethernet link carries **at most 1500 bytes** of IP packet by default (its MTU). Hosts send TCP segments of at most **MSS** bytes, usually 1460, with the **DF (don't fragment) bit** set. If a router in the middle has a smaller MTU, it **drops the packet and sends ICMP "fragmentation needed" (type 3, code 4) containing the next hop MTU**, and the sender lowers its packet size. That is **path MTU discovery (PMTUD)**.

```text
--- 5. path MTU discovery: the router link is 1000 bytes, the client sends 1200 with DF set
$ nse c ping -c 1 -W 1 -M do -s 1200 10.2.0.2 2>&1 | grep -E "From|mtu|transmitted"
From 10.1.0.1 icmp_seq=1 Frag needed and DF set (mtu = 1000)
1 packets transmitted, 0 received, +1 errors, 100% packet loss, time 0ms
$ nse c ping -c 1 -W 1 -M do -s 900 10.2.0.2 2>&1 | grep -E "bytes from|transmitted"
908 bytes from 10.2.0.2: icmp_seq=1 ttl=63 time=0.020 ms
1 packets transmitted, 1 received, 0% packet loss, time 0ms
$ nse c ip route get 10.2.0.2 | tr -s " \\n" " "; echo
10.2.0.2 via 10.1.0.1 dev c0 src 10.1.0.2 uid 0 cache expires 599sec mtu 1000
$ nse c tracepath -n 10.2.0.2 2>&1 | head -4
 1?: [LOCALHOST]                      pmtu 1500
 1:  10.1.0.1                                              0.025ms
 1:  10.1.0.1                                              0.003ms
 2:  10.1.0.1                                              0.003ms pmtu 1000
```

The router-to-server link was set to **MTU 1000**. A ping of 1200 bytes with DF set (`-M do`) gave **`Frag needed and DF set (mtu = 1000)`** from the router; one of 900 bytes passed; and **`ip route get` now showed `cache expires 599sec mtu 1000`**: the client's kernel had **cached the discovered MTU for 10 minutes** for that destination. `tracepath` (iputils) does PMTU discovery per hop: it started at `pmtu 1500` and reported **`pmtu 1000`** at the router.

```text
--- 6. the same path with ICMP "fragmentation needed" blocked: a black hole for large TCP segments
$ nse c ip route flush cache; nse c ip route get 10.2.0.2 | tr -s " \n" " "; echo
10.2.0.2 via 10.1.0.1 dev c0 src 10.1.0.2 uid 0 cache
$ nse c python3 /tmp/lx08_big.py
300 KB delivered and closed in 0.00 s
$ nse c ip route get 10.2.0.2 | tr -s " \n" " "; echo
10.2.0.2 via 10.1.0.1 dev c0 src 10.1.0.2 uid 0 cache expires 599sec mtu 1000
$ nse c ip route get 10.2.0.2 | tr -s " \n" " "; echo
10.2.0.2 via 10.1.0.1 dev c0 src 10.1.0.2 uid 0 cache
$ nse c python3 /tmp/lx08_big.py
stalled: timed out after 5.0 s
$ nse r nft list chain inet lx out | grep counter
                icmp type destination-unreachable counter packets 7 bytes 4032 drop
```

With ICMP intact, a 300 KB transfer completed in **under 10 ms** (the first large segment was dropped, the ICMP returned, the route cache showed `mtu 1000`, TCP resent smaller segments). Then the router was told to **drop outgoing ICMP destination-unreachable** messages, as an over-strict firewall does, the cached MTU was flushed, and **the same transfer stalled for the whole 5 second timeout**, with **7 ICMP packets dropped** (the nft counter). This is the **PMTU black hole**: the TCP handshake works (small packets), small requests work, and **the first large response hangs**. It is the cause of "SSH connects but hangs when I list a big directory" and "HTTPS site loads partly" behind VPNs and tunnels (which lower the MTU). Fixes, best first: **do not block ICMP type 3 code 4**; **clamp the MSS** at the tunnel or router (`nft ... tcp option maxseg size set rt mtu`, shown in Problem E; or `iptables ... --clamp-mss-to-pmtu`); lower the interface MTU on the hosts; or enable `net.ipv4.tcp_mtu_probing=1` so TCP probes for a working size itself (from memory).

---


## 6. `nft`, NAT and `conntrack`

**Netfilter** is the kernel's packet hook framework: packets pass **hooks** (`prerouting`, `input`, `forward`, `output`, `postrouting`) at fixed points of the path, and rules attached there can accept, drop, reject, count, mark or rewrite them. **`nftables`** (`nft`) is the current rule language; **`iptables`** is the older one, now usually a front end that writes `nftables` rules (section 6.4). A rule set is **tables** (per address family: `ip`, `ip6`, `inet` for both) containing **chains** (a hook and priority, or a plain list) containing **rules**.

### 6.1 A default-drop chain with an allow list

```text
--- 1. a default-drop forward chain with an allow list, and the counters that tell you which rule matched
$ nse c python3 /tmp/lx08_try.py 10.2.0.2 5001
server saw client 10.1.0.2
$ nse c python3 /tmp/lx08_try.py 10.2.0.2 5002
connection refused (reset)
$ nse c python3 /tmp/lx08_try.py 10.2.0.2 5003
timed out (packets dropped silently)
$ nse r nft list chain inet fw forward
table inet fw {
        chain forward {
                type filter hook forward priority filter; policy drop;
                ct state established,related counter packets 7 bytes 399 accept
                ip saddr 10.1.0.0/24 tcp dport 5001 ct state new counter packets 1 bytes 60 accept
                ip saddr 10.1.0.0/24 tcp dport 5002 counter packets 1 bytes 60 reject with tcp reset
                counter packets 2 bytes 120
        }
}
```

The router's `forward` chain was created with **`policy drop`** and three rules: accept packets of **established or related** connections (the stateful rule, which makes return traffic work without writing a rule for it), accept **new** TCP connections from the client network to port 5001, and **`reject with tcp reset`** for port 5002; a final **bare `counter`** counts what falls through to the policy. The three client attempts show the three behaviours: port 5001 was **accepted** (`server saw client 10.1.0.2`); port 5002 was **rejected** and the client saw **`connection refused`** immediately; port 5003 matched nothing, so the **policy dropped it silently** and the client saw **`timed out`**. The counters in the listing say which rule fired: 7 packets through the established rule, 1 new SYN accepted, 1 rejected, **2 left over** (the SYN and its retry to 5003).

Remember the earlier rule: **refused means something answered, timed out means nothing did**. `reject` is friendlier to clients (an instant error instead of a 2 minute wait) and `drop` gives away less to a scanner; choose per rule. The counters are the debugging tool: **put a `counter` on a rule while you debug**, and `nft list ruleset` shows how many packets and bytes it matched.

**The shape of an `nft` rule file** (shown in the script, from memory of the syntax rules it enforces): a chain is `type filter|nat|route hook NAME priority N; policy accept|drop;`; rules are `match... statement`; matches include `ip saddr`, `tcp dport`, `iifname`, `ct state`, `meta mark`; statements include `accept`, `drop`, `reject`, `counter`, `log`, `masquerade`, `dnat to`, `snat to`, `limit rate`, `ct count over`. In a one line chain, **a semicolon is needed before the closing brace**, and a chain called `fwd` is **rejected as a keyword**; both bit the experiments.

### 6.2 Source NAT: masquerade

```text
--- 2. masquerade: the server sees the router, not the client
$ nse c python3 /tmp/lx08_try.py 10.2.0.2 5001
server saw client 10.2.0.1
$ nse r conntrack -L 2>/dev/null | grep 5001 | head -1 | cut -c1-150
tcp      6 118 TIME_WAIT src=10.1.0.2 dst=10.2.0.2 sport=54384 dport=5001 src=10.2.0.2 dst=10.1.0.2 sport=5001 dport=54384 [ASSURED] mark=0 use=1
```

With a **`masquerade`** rule in the `postrouting` hook of a **`nat`** chain on the router's server side, the server's log of the client address changed from `10.1.0.2` to **`10.2.0.1`** (the router's own address on that link): the client network is hidden behind the router. The conntrack line shows both views of the same flow: the **original direction** (`src=10.1.0.2 dst=10.2.0.2 sport=... dport=5001`) and the **reply direction** the kernel expects (`src=10.2.0.2 dst=10.2.0.1`), which is how it rewrites the replies back. `masquerade` picks the outgoing interface's current address, which suits dynamic addresses; **`snat to ADDR`** is faster for static ones.

### 6.3 Destination NAT: port forwarding

```text
--- 3. destination NAT (port forward): the client talks to the router, the server answers
$ nse c python3 /tmp/lx08_try.py 10.1.0.1 8080
server saw client 10.2.0.1
$ nse r nft list chain ip nat pre | grep counter
                iifname "rc" tcp dport 8080 counter packets 1 bytes 60 dnat to 10.2.0.2:5002
$ nse r conntrack -L 2>/dev/null | grep 8080 | head -1 | cut -c1-150
tcp      6 119 TIME_WAIT src=10.1.0.2 dst=10.1.0.1 sport=45248 dport=8080 src=10.2.0.2 dst=10.2.0.1 sport=5002 dport=45248 [ASSURED] mark=0 use=1
```

A **`dnat to 10.2.0.2:5002`** rule in a `prerouting` nat chain made a connection to **the router's own address on port 8080** reach the server on 5002. The rule's counter shows the first packet matched it, and the conntrack line shows the **client believing it connected to `10.1.0.1:8080`** while the kernel maps replies from `10.2.0.2:5002` back. (The server still sees `10.2.0.1`, because the masquerade rule from the previous step was still in place.) For a DNAT to work the packet must also be **allowed by the forward chain** and the host must have **`ip_forward=1`**: the three places a port forward breaks (Problem B).

### 6.4 `conntrack`

```text
--- 4. the conntrack table: entries, timeouts, and a per-source connection limit
$ nse r conntrack -C; cat /proc/sys/net/netfilter/nf_conntrack_max
3
262144
$ nse r conntrack -C
63
$ nse r conntrack -L -p udp 2>/dev/null | head -1 | cut -c1-110
udp      17 29 src=10.1.0.2 dst=10.2.0.2 sport=60727 dport=9017 [UNREPLIED] src=10.2.0.2 dst=10.2.0.1 sport=90
$ for f in tcp_timeout_established tcp_timeout_time_wait udp_timeout udp_timeout_stream; do printf "%s " $f; cat /proc/sys/net/netfilter/nf_conntrack_$f; done
tcp_timeout_established 432000
tcp_timeout_time_wait 120
udp_timeout 30
udp_timeout_stream 120
$ nse c python3 /tmp/lx08_many.py
6 connections held open: 3 accepted, 3 refused
$ nse r nft list chain inet lim forward | grep counter
                tcp dport 5001 ct state new ct count over 3 counter packets 3 bytes 180 reject with tcp reset
```

**Connection tracking** records every flow that passes the host (once any rule needs state or NAT) in a table, and `conntrack -L` lists it, `-C` counts it, `-S` gives statistics (`insert_failed`, `drop`, `invalid`). The 60 UDP datagrams to 60 different ports made **60 new entries** (count 3 before, 63 after), each marked **`[UNREPLIED]`** because the server never answered, and each lives for the **UDP timeout (30 seconds here)**. The timeouts to know: **TCP established 432,000 seconds (5 days)**, TIME_WAIT 120, UDP 30 (single packet) and 120 (stream), and the table limit `nf_conntrack_max` is **262,144** here.

**What a full table does** (from memory, because the VM does not allow changing the limit): new connections are **dropped** and the kernel logs `nf_conntrack: table full, dropping packet`; `conntrack -S` shows `insert_failed` and `drop` rising and `/proc/sys/net/netfilter/nf_conntrack_count` approaching the max. Typical causes: a **load balancer, NAT gateway or Kubernetes node** with many short flows, and a 5 day established timeout holding entries for dead peers. Fixes: raise `nf_conntrack_max` (and the hash size), lower the timeouts, **skip tracking** for traffic that does not need it (`notrack` in a `raw` chain), and size by flows per second times timeout.

The lower half of the same section shows a **per-source connection limit**, a rule `ct count over 3 ... reject`: of **6 connections held open, 3 were accepted and 3 refused**, and the rule's counter read 3 packets. The same pattern with `limit rate over N/second` protects a service from a single noisy client.

### 6.5 `iptables` is a front end

```text
--- 5. iptables on a modern system is a front end to nftables
$ iptables --version
iptables v1.8.10 (nf_tables)
$ nse r iptables -S FORWARD | tail -1; nse r nft list ruleset | grep -E "table|dport|masquerade" | head -5
-A FORWARD -p tcp -m tcp --dport 5001 -j ACCEPT
# Warning: table ip nat is managed by iptables-nft, do not touch!
table ip filter {
                tcp dport 5001 counter packets 0 bytes 0 accept
table ip nat {
                oifname "rs" counter packets 0 bytes 0 masquerade
```

`iptables v1.8.10 (nf_tables)` says the legacy command now **writes nftables rules**: the `-A FORWARD ... -j ACCEPT` appeared in `nft list ruleset` as a `table ip filter` with a `tcp dport 5001 ... accept` rule, and the NAT rule as `table ip nat` with `masquerade`, with a warning that **the table is managed by `iptables-nft`** and should not be edited with `nft`. The two systems can coexist but **mixing them is a source of confusion**: when `iptables -S` shows nothing but traffic is being dropped, run **`nft list ruleset`** (and, on some distributions, check for `firewalld`, `ufw` or Docker adding rules). **Docker** adds its own `DOCKER` and `FORWARD` chains and **inserts rules ahead of yours**, which is why a host firewall may not protect published container ports (from memory).

---

## 7. Where is the loss? A triage order

The rest of the triage is about counters: **each layer counts its own drops in a different place**. The next five transcripts are from one lab with the same iperf3 test and loss placed at different layers.

### 7.1 No loss, no rate limit: retransmits anyway

```text
--- 1. baseline: cubic, no loss injected, limited to 1 Gbit/s so nothing overflows; then unlimited
$ ipf cubic -b 1G
1000 Mbits/sec retransmits 0
$ ipf cubic
17.4 Gbits/sec retransmits 656
```

With the sender **capped at 1 Gbit/s** (`-b 1G`), the flow ran at **1,000 Mbit/s with 0 retransmits**. With **no cap**, it ran at **about 17 Gbit/s** with **656 retransmits** in 3 seconds, **and no packet had been dropped by any rule**. The sender is faster than the receiving softirq can drain the `veth` queue, the queue overflows, and TCP retransmits: **this is congestion on a virtual link, not corruption**. Remember it when you read a lab: unbounded TCP over `veth` is not loss free, and a production flow with a few retransmits is also not proof of a bad link; **look at the rate of retransmits, as a share of segments sent** (`TcpRetransSegs` against `TcpOutSegs`), and whether it correlates with a problem.

### 7.2 Loss injected in the router

```text
--- 2. random loss injected in the router with nft (this is not a NIC or queue event)
one packet in 1000  14.0 Gbits/sec retransmits 4475
one packet in 100   791 Mbits/sec retransmits 2115
one packet in 10    2.79 Mbits/sec retransmits 104
$ nse r nft list chain inet lossy forward | grep counter
                numgen random mod 10 0 counter packets 121 bytes 154048 drop
```

An `nft` rule **`numgen random mod N == 0 counter drop`** in the router's forward chain drops one packet in N at random. Running the same `cubic` test: **1 in 1,000** gave **14.0 Gbit/s**, **1 in 100** gave **791 Mbit/s**, **1 in 10** gave **2.79 Mbit/s** (and the `nft` counter after the last run showed 121 drops). **A loss rate of 1 percent costs about 95 percent of the throughput** of this path, and 10 percent leaves almost nothing: TCP treats every loss as congestion and halves its window (the throughput of a loss based TCP falls roughly as `MSS / (RTT x sqrt(loss))`, the **Mathis formula**, from memory). This is why packet loss, even a small amount, is **the** network problem to find, and why `bbr`, which does not treat each loss as congestion, behaves differently.

### 7.3 Which counters saw it

```text
--- 3. where that loss did and did not show up (same router, same run)
$ nse r ip -s link show rs | tail -4
    RX:   bytes packets errors dropped  missed   mcast
       14172202  214365      0       0       0       0
    TX:   bytes packets errors dropped carrier collsns
    12456835031  299117      0       0       0       0
$ nse r tc -s qdisc show dev rs | head -2
qdisc noqueue 0: root refcnt 2
 Sent 0 bytes 0 pkt (dropped 0, overlimits 0 requeues 0)
$ nse c nstat -az TcpRetransSegs TcpExtTCPLossProbes TcpExtTCPFastRetrans | tail -3
TcpRetransSegs                  7351               0.0
TcpExtTCPFastRetrans            7291               0.0
TcpExtTCPLossProbes             49                 0.0
```

The same router, the same runs, **where the loss was visible and where it was not**:

- **`ip -s link` on the router's interface: 0 errors and 0 dropped.** The packets were destroyed by a **netfilter rule**, above the driver, so **no NIC or driver counter moves**. This is the central point: **clean interface counters do not mean clean packet delivery**.
- **`tc -s qdisc`**: `noqueue`, nothing sent or dropped here (a `veth` has no queue by default).
- **`nstat` on the sender**: **`TcpRetransSegs` 7,351**, of which **`TcpExtTCPFastRetrans` 7,291** (retransmitted after three duplicate ACKs, the normal loss recovery) and **`TcpExtTCPLossProbes` 49** (tail loss probes). These counters are **absolute for the namespace** and so include the earlier runs. The **sender's counters show the loss; the network did not**.
- **`nft list chain ... | grep counter`**: the rule's own counter (section 7.2) shows exactly what it dropped.

### 7.4 Loss in a queue

```text
--- 4. a shaper: tbf at 100 Mbit/s on the router egress, queue of 20 KB
$ ipf cubic
94.3 Mbits/sec retransmits 1114
$ nse r tc -s qdisc show dev rs | head -3
qdisc tbf 8005: root refcnt 3 rate 100Mbit burst 16Kb lat 327us
 Sent 37020507 bytes 24468 pkt (dropped 157, overlimits 13424 requeues 0)
 backlog 0b 0p requeues 0
```

A **`tbf`** (token bucket filter) qdisc of 100 Mbit/s with a 20 KB limit was put on the router's egress. The flow ran at **94.3 Mbit/s** (the shaped rate, not the 17 Gbit/s of the link) with **1,114 retransmits**, and **`tc -s qdisc show dev rs`** counted **157 dropped and 13,424 overlimits**: `overlimits` is the number of times the queue was **throttled** because a packet would have exceeded the rate, and `dropped` the packets that did not fit in the queue. The counter that **does not** show it: the interface's own `dropped` column (in Problem D the same shaper dropped 180 packets while `ip -s link` still read 0). **Qdisc drops are counted by the qdisc**, so a large queue or a shaper drop is invisible to `ip -s link` and visible only to `tc -s qdisc`. On a real host the default qdisc (`fq_codel` or `pfifo_fast`; this VM shows `pfifo_fast` as `default_qdisc`) and its `dropped` and `backlog` fields are where to look for **bufferbloat or a saturated egress link**.

### 7.5 Loss at the receiving socket

```text
--- 5. a full UDP socket buffer: the receiver reads slowly, the NIC and the wire are fine
$ nse s ss -uamn sport = :9100 | cut -c1-110
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
UNCONN 129024 0            0.0.0.0:9100      0.0.0.0:*
         skmem:(r129024,rb131072,t0,tb212992,f2048,w0,o0,bl0,d1944)
receiver got 56 datagrams
$ nse s nstat -az UdpInErrors UdpRcvbufErrors UdpInDatagrams | tail -3
UdpInDatagrams                  56                 0.0
UdpInErrors                     1944               0.0
UdpRcvbufErrors                 1944               0.0
$ nse s ip -s link show s0 | sed -n 3,4p
    RX:   bytes packets errors dropped  missed   mcast
    12494637028  305850      0       0       0       0
```

A UDP receiver with a **64 KiB receive buffer** (the kernel doubled it to `rb131072`) slept for 2.5 seconds while a sender sent **2,000 datagrams of 1,000 bytes**. The socket held **129,024 bytes** (the `Recv-Q`), the receiver eventually read **56 datagrams**, and the kernel counted **1,944 `UdpRcvbufErrors`** (the same number added to `UdpInErrors`): the **socket buffer was full and the packets were dropped after the NIC had delivered them**. **`ip -s link` on the receiving interface again shows no drop** (its RX counters show the 300,000 packets of the earlier tests and no errors). The fixes: **read faster**, a larger buffer (`SO_RCVBUF`, limited by `net.core.rmem_max`, which is 4,194,304 here), or applying back pressure. In production, `nstat -az UdpRcvbufErrors` and `ss -uamn` (the `d` field in `skmem` is the drop count) find this, and so does `/proc/net/udp` (the last column is `drops`).

### 7.6 The order to look in

When a flow is slow or losing packets, take **two readings a few seconds apart** and walk **from the application toward the wire**:

1. **The socket**: `ss -ti` for the flow (`retrans`, `cwnd`, `rwnd_limited`, `Send-Q`, `Recv-Q`), and `nstat` for `TcpRetransSegs`, `ListenOverflows`, `UdpRcvbufErrors`.
2. **The firewall and NAT**: `nft list ruleset` counters, `conntrack -S` (`insert_failed`, `drop`).
3. **The qdisc**: `tc -s qdisc` (`dropped`, `overlimits`, `backlog`).
4. **The interface**: `ip -s link` (`errors`, `dropped`, `missed`), then **`ethtool -S`** for the driver's per cause counters (CRC errors, `rx_missed`, ring `fifo` errors, per queue drops; from memory for real NICs) and `ethtool -g` for ring sizes.
5. **The softirq**: `/proc/net/softnet_stat` (the second column is packets dropped because the per CPU backlog was full, `netdev_max_backlog`), and `/proc/interrupts` to see whether one CPU takes all the receive interrupts ([LX 05](05_performance_methodology_and_triage.md)).
6. **The path**: `tcpdump` on **both** sides of each hop, **MTU** (`tracepath`, section 5.2), then `mtr` for where on the path loss begins.

Stop at the first layer whose counter explains the symptom. **A layer that shows no drops does not clear the layers below it**: the examples above are, in order, a netfilter drop, a qdisc drop and a socket drop that each left the interface counters at zero.

---

## 8. Names, `sysctl` and the NIC

### 8.1 DNS with `dig`

```text
--- 1. dig against a lab DNS server: status, answer and query time
$ nse c dig +noall +comments +answer +stats @10.2.0.2 web.lab | grep -E "status|web.lab|Query time"
;; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 59023
web.lab.                60      IN      A       10.2.0.2
;; Query time: 0 msec
$ nse c dig +noall +comments +stats @10.2.0.2 nothere.lab | grep -E "status|Query time"
;; ->>HEADER<<- opcode: QUERY, status: NXDOMAIN, id: 41920
;; Query time: 0 msec
$ nse c dig +short +time=1 +tries=1 @10.2.0.99 web.lab; echo "exit $?"
;; communications error to 10.2.0.99#53: timed out
;; no servers could be reached
exit 9
```

`dig @SERVER NAME TYPE` asks one server directly and prints the **status** (`NOERROR`, `NXDOMAIN` for a name that does not exist, `SERVFAIL` for a server error, `REFUSED`), the answer with its **TTL** (60 seconds here), and the **query time**. A server that does not answer gives **`communications error ... timed out`** and `no servers could be reached` after `+time` seconds times `+tries`. `+short` prints only the answer, `+trace` follows the delegation from the root (not usable here: no Internet), `+tcp` forces TCP, and `-x` does reverse lookups. **`dig` talks to the server you name and ignores `/etc/hosts`, `nsswitch.conf` and the search path**, so a name that `dig` resolves and an application cannot is a **resolver configuration** problem (next section). **Not run:** `resolvectl` (no `systemd-resolved` in the VM; on a system with it, `resolvectl status` shows per link servers and `resolvectl query NAME` shows which of them and which cache answered).

### 8.2 What an application does: the resolver

```text
--- 2. the resolver: /etc/resolv.conf per namespace, and what a dead nameserver costs
$ nse c getent hosts web.lab
10.2.0.2        web.lab
$ nse c bash -c "time getent hosts web.lab" 2>&1 | grep -E "web.lab|real"
10.2.0.2        web.lab
real    0m2.005s
$ nse c bash -c "time getent hosts web.lab" 2>&1 | grep -E "web.lab|real"
10.2.0.2        web.lab
real    0m0.002s
$ nse c bash -c "time getent hosts web.lab" 2>&1 | grep -E "real|web.lab"; true
real    0m9.247s
$ grep -E "^hosts" /etc/nsswitch.conf
hosts:          files dns
```

Applications use the C library: **`getaddrinfo`**, which follows **`/etc/nsswitch.conf`** (`hosts: files dns`: first `/etc/hosts`, then DNS) and then `/etc/resolv.conf` (nameservers tried in order, `options timeout:N attempts:N`, `search` domains). `getent hosts NAME` runs the same path as a program, so **it is the right test of "what will my application get"**. The four runs show the cost of getting it wrong:

- A **working** first nameserver: answered in **2 ms**.
- A **dead first nameserver** with `timeout:1 attempts:1` before a working one: **2.0 seconds** for each lookup (I did not trace why it is 2 seconds and not 1; the A and the AAAA query each waiting for the dead server is the likely reason).
- With **only a dead nameserver and the default settings** (5 second timeout, 2 attempts): **9.2 seconds** and no answer.
- `ip netns exec` bind mounts **`/etc/netns/NAME/resolv.conf`** over the system file, so each namespace can have its own resolver; this is how the lab pointed the client at its own DNS server.

A dead first nameserver is **a common cause of slow SSH logins, slow `sudo` and slow application start**: every lookup pays the timeout. The cures are to remove or fix the dead entry, lower `timeout` and `attempts`, or run a **local caching resolver**. Also from memory: `ndots` in `resolv.conf` (Kubernetes sets 5) turns a short external name into several search domain queries before the real one, multiplying DNS load.

### 8.3 `sysctl net.*`: per namespace and what matters

```text
--- 3. sysctl net.* is per network namespace: the router forwards, the host does not
$ sysctl -n net.ipv4.ip_forward; nse r sysctl -n net.ipv4.ip_forward
0
1
$ for k in net.core.somaxconn net.ipv4.tcp_max_syn_backlog net.ipv4.ip_local_port_range net.ipv4.tcp_rmem net.ipv4.tcp_wmem net.core.rmem_max net.ipv4.tcp_congestion_control net.core.default_qdisc net.ipv4.tcp_fin_timeout net.ipv4.tcp_tw_reuse net.ipv4.conf.all.rp_filter; do printf "%-36s %s\n" $k "$(sysctl -n $k | tr "\t" " ")"; done
net.core.somaxconn                   4096
net.ipv4.tcp_max_syn_backlog         512
net.ipv4.ip_local_port_range         32768 60999
net.ipv4.tcp_rmem                    4096 131072 33554432
net.ipv4.tcp_wmem                    4096 16384 4194304
net.core.rmem_max                    4194304
net.ipv4.tcp_congestion_control      bbr
net.core.default_qdisc               pfifo_fast
net.ipv4.tcp_fin_timeout             60
net.ipv4.tcp_tw_reuse                2
net.ipv4.conf.all.rp_filter          0
```

The first two lines show the point: **`net.ipv4.ip_forward` is 0 on the host and 1 in the router namespace**. Most `net.*` settings are **per network namespace**, so a container or a lab can differ from the host; some (`nf_conntrack_max` here) are **not changeable** from a restricted one. The values to know, with their meaning:

| Setting | Here | Meaning |
| --- | --- | --- |
| `net.core.somaxconn` | 4096 | cap on the `listen()` backlog |
| `net.ipv4.tcp_max_syn_backlog` | 512 | cap on half open (SYN received) connections |
| `net.ipv4.ip_local_port_range` | 32768 60999 | ephemeral ports for outgoing connections |
| `net.ipv4.tcp_rmem`, `tcp_wmem` | min, default, max | TCP receive and send buffer sizes (the max is what autotuning may reach) |
| `net.core.rmem_max`, `wmem_max` | 4194304 (rmem) | cap on what `SO_RCVBUF` and `SO_SNDBUF` may ask for |
| `net.ipv4.tcp_congestion_control` | bbr | default algorithm (`cubic` and `reno` also available) |
| `net.core.default_qdisc` | pfifo_fast | qdisc for new interfaces (`fq` or `fq_codel` on most distributions; unavailable here) |
| `net.ipv4.tcp_fin_timeout` | 60 | time in `FIN-WAIT-2` for orphaned sockets |
| `net.ipv4.tcp_tw_reuse` | 2 | reuse `TIME-WAIT` ports for new outgoing connections (2 means loopback only) |
| `net.ipv4.conf.all.rp_filter` | 0 | reverse path filter (drops packets that arrive on an interface the kernel would not route the source back out of; 1 or 2 on many distributions, **a classic cause of asymmetric routing drops**) |

Change one with `sysctl -w name=value` (until reboot) and make it permanent in `/etc/sysctl.d/*.conf` (`sysctl --system` loads them). Tune from a **measured symptom** (a counter in section 7), not from a list found online.

```text
--- 4. somaxconn caps the accept queue that listen(1000000) asks for
$ nse s ss -ltn sport = :5004 | tail -1 | tr -s " " | cut -d" " -f1,2,3
LISTEN 0 4096
$ nse s ss -ltn sport = :5004 | tail -1 | tr -s " " | cut -d" " -f1,2,3
LISTEN 0 64
```

`listen(fd, 1000000)` asked for a backlog of a million, and `ss -ltn` showed a **`Send-Q` of 4096**: `somaxconn` caps it. After `sysctl -w net.core.somaxconn=64` **in the server's namespace only**, the same program got **64**. So the accept queue is `min(backlog, somaxconn)`, and **raising only the application's backlog does nothing** (Problem C).

### 8.4 `ethtool`

```text
--- 5. ethtool on the sandbox NIC and on a veth
$ ethtool -i eth0 | head -3
driver: virtio_net
version: 1.0.0
firmware-version:
$ ethtool -k eth0 | grep -E "^(tcp-segmentation-offload|generic-segmentation-offload|generic-receive-offload|rx-checksumming|tx-checksumming):"
rx-checksumming: on [fixed]
tx-checksumming: on
tcp-segmentation-offload: on
generic-segmentation-offload: on
generic-receive-offload: on
$ ethtool -g eth0 | grep -E "^(RX|TX):"
RX:                     256
TX:                     256
RX:                     256
TX:                     256
$ nse c ethtool -S c0 | head -4
NIC statistics:
     peer_ifindex: 84
     rx_queue_0_xdp_packets: 0
     rx_queue_0_xdp_bytes: 0
```

`ethtool -i` names the driver (`virtio_net` for the sandbox's NIC); **`-k`** lists **offloads**, work the NIC or driver does instead of the CPU: checksumming, **TSO** (TCP segmentation offload: the stack hands down one 64 KB buffer and the NIC cuts it into packets), **GSO** and **GRO** (the software equivalents, which is why a `tcpdump` on a host can show 64 KB "packets"); **`-g`** shows **ring buffer** sizes (256 here; `-G rx 4096` raises them where supported, a standard fix for `rx_missed` drops under bursts); **`-S`** prints driver statistics (for a `veth`: queue statistics and `peer_ifindex`); and `ethtool eth0` shows speed and duplex on a physical link (not meaningful on this virtual one). **From memory** for real NICs: `-S` shows per queue drops and CRC and alignment errors; `-l` and `-L` set the number of queues; `-c` coalescing (interrupt moderation); `--show-fec`; `-m` reads the transceiver module. **Offloads confuse captures** (a checksum looks wrong on the sending host because the NIC adds it later), so `ethtool -K IF tx off` may help while debugging.

### 8.5 `/proc/net` and `nstat`

```text
--- 6. /proc/net: where the counters live
$ ls /proc/net | tr "\n" " " | fold -s -w 88; echo
arp arp_tables_matches arp_tables_names arp_tables_targets connector dev dev_mcast
fib_trie fib_triestat icmp igmp ip6_tables_matches ip6_tables_names ip6_tables_targets
ip6t_hashlimit ip_mr_cache ip_mr_vif ip_tables_matches ip_tables_names
ip_tables_targets ipconfig ipt_hashlimit mcfilter netfilter netlink netstat
nf_conntrack nf_conntrack_expect packet pnp protocols psched ptype raw route rt_acct
rt_cache snmp sockstat softnet_stat stat tcp udp udplite unix xfrm_stat xt_recent
$ nse c grep -E "^Tcp:" /proc/net/snmp | cut -c1-110
Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs Out
Tcp: 1 200 120000 -1 0 0 0 0 0 0 0 0 0 0 0
$ nse c cat /proc/net/sockstat | head -3
sockets: used 0
TCP: inuse 0 orphan 0 tw 0 alloc 13 mem 205
UDP: inuse 0 mem 0
$ nse c cat /proc/net/softnet_stat | head -2
0012bb47 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
00122c89 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 00000000 00000000
```

`/proc/net` is the kernel's table of live network state, **per namespace**: `dev` (interface counters), `route`, `arp`, `tcp` and `udp` (sockets, what `ss` reads), `snmp` and `netstat` (protocol counters, in a header line then a value line, which `nstat` pairs for you), `sockstat` (socket counts and memory pages), `softnet_stat` (per CPU receive processing: columns are processed, **dropped**, and **time_squeeze**, the number of times the softirq ran out of budget; one row per CPU, hexadecimal), `nf_conntrack` and `protocols`. `ss -s` and `nstat` are the friendly views of the same data.

---


## 9. What the VM could not run, and what to know about it

None of this was run; it is **from memory** of the manual pages and common practice, and is marked so that you do not quote it as measured.

- **`tc qdisc add dev eth0 root netem delay 50ms 10ms loss 1% reorder 25%`** is the standard way to **inject delay, jitter, loss, duplication, corruption and reordering** on one interface; the kernel module `sch_netem` was absent here, which is why the lab used `nft` for loss. On a normal Linux machine this is the tool for testing how a protocol or application behaves on a bad link. It applies to **egress only**, so to delay both directions you add it on both ends or on an `ifb` device for ingress.
- **`fq`**, **`fq_codel`** and **`cake`** are queue disciplines that keep a long queue from adding delay (**bufferbloat**). `fq_codel` is the usual default on modern distributions; `fq` is needed for BBR's pacing on older kernels. `tc -s qdisc show` reports `dropped`, `ecn_mark`, `backlog` and per flow stats.
- **`htb`** and **`tbf`** shape traffic to a rate; `htb` builds a class tree with guaranteed and ceiling rates and a filter (`tc filter ... u32` or `flower`, or an `nft` mark) that assigns packets to classes. Only `tbf` was exercised here.
- **VLANs** (`ip link add link eth0 name eth0.10 type vlan id 10`) tag frames with an 802.1Q ID so one cable carries several networks; **bonding** (`mode active-backup` for failover, `802.3ad` for LACP aggregation) joins NICs into one logical link; **`ip link add type macvlan, ipvlan, vxlan, geneve, wireguard`** are the building blocks of container and overlay networks. None existed in this kernel.
- **`ethtool -S`, `-G`, `-L`, `-C`** on a physical NIC (section 8.4), and **XDP** and **eBPF** (`bpftrace`, `tc` programs, `xdp-tools`) for per packet processing in the driver; `bpftrace` and `perf` are not available in this VM (from the earlier tutorials).
- **IPv6** works with the same tools (`ip -6`, `ss -6`, `nft` family `ip6` or `inet`), uses **neighbour discovery** instead of ARP and **never fragments in the network**, so PMTUD (section 5.2) is **mandatory**: blocking ICMPv6 "packet too big" breaks it completely.
- **A full conntrack table, `nf_conntrack: table full`, `ethtool` ring drops, interface errors and TCP offload artefacts in captures** are all described in the sections above as from memory, since the VM has no real NIC.

**What to say in an interview about the limit:** "I built the lab with namespaces and `veth`, which gives the same kernel code paths for routing, netfilter and TCP but no real driver or wire; for the NIC level I would read `ethtool -S` and `softnet_stat` on the real host."

---

## 10. What it costs

Costs, using only what was measured here.

- **A loss rate of 1 percent costs about 95 percent of a TCP flow's throughput on this path** (17.4 Gbit/s unlimited, 0.79 Gbit/s at 1 in 100). Per cent level loss is a major incident for a loss based congestion control, whatever the link speed.
- **A dead nameserver costs 2 seconds per lookup (timeout 1) or 9 seconds (defaults, one server)**, on every connect that resolves a name, which is why it shows up as "everything is slow to start".
- **A PMTU black hole costs the full application timeout** (5 seconds here, 2 minutes or more for real stacks) per affected connection, and **every large transfer** on that path.
- **Connection tracking costs memory per flow and holds entries for a long time**: 60 one packet UDP flows made 63 entries living 30 seconds, an idle established TCP flow lives 5 days (432,000 s), the limit here is 262,144. A NAT or load balancer that handles N new flows per second needs about N times the timeout in entries.
- **`TIME-WAIT` costs ports and memory on the closing side:** 4 sockets for 4 closed connections here, about 470 new connections per second to one destination at steady state from the default port range, from the arithmetic.
- **A listen queue overflow costs a whole retransmission timer** (1 second for the first SYN retry) for each rejected connection, and **it can happen with an idle CPU.**
- **A shaper or small queue turns offered load into drops:** `tbf` at 100 Mbit/s with a 20 KB queue gave 94.3 Mbit/s with 1,114 retransmits and 157 queue drops; the application sees a slow transfer and retransmits, **not an error**.
- **A capture is not free:** `tcpdump` copies each matching packet to user space; a BPF filter in the kernel keeps this cheap, an unfiltered capture on a busy link can drop packets itself (`tcpdump` prints `packets dropped by kernel`).
- **Offloads hide packet sizes:** with GRO and TSO on, a capture shows super packets of up to 64 KB, which is not the wire.

---

## 11. Common mistakes

1. **Debugging with `ping` only.** ICMP echo is often filtered and says nothing about TCP port reachability. Test the real protocol and port (`nc -vz`, `curl`, a client program) and read the **result type**: refused, timed out, reset after connect.
2. **Reading `timed out` and `refused` the same.** Refused: something answered with a reset (closed port or a `reject` rule). Timed out: nothing answered (a drop, a missing return path, a dead host, or a PMTU black hole after the handshake).
3. **Trusting the interface counters** (`ip -s link`) to prove there is no loss. Netfilter, qdisc and socket drops are counted elsewhere (section 7).
4. **Capturing on only one side.** A packet seen on the way in and not on the way out was lost in between; one capture cannot say that.
5. **Blocking all ICMP.** It breaks PMTUD, traceroute and error reporting. Allow at least destination-unreachable (codes 3 and 4) and time-exceeded; **never block ICMPv6 packet-too-big**.
6. **Raising `listen()` backlog without `somaxconn`**, or the reverse: the smaller wins (4096 against a request of 1,000,000 here, 64 after lowering it).
7. **Forgetting `ip_forward`** (a router that does not route) or the **forward chain** (a policy drop with no allow rule) when a port forward times out; or forgetting that a DNAT also needs the **return path** to go through the same host (a server with a different default gateway replies directly and the client resets the connection).
8. **Adding a route without the rule that selects its table**, or a rule without a route in the table it points to.
9. **Mixing `iptables` and `nft` rulesets** without checking `nft list ruleset`; and trusting a host firewall when Docker or Kubernetes inserts its own rules first.
10. **Tuning `sysctl` from a blog.** Change a setting only to fix a measured symptom (a counter that moves), one at a time, and keep the old value.
11. **Reading `Recv-Q` and `Send-Q` without knowing the socket type**: on `LISTEN` they are accept queue length and backlog; on `ESTAB` they are unread and unacknowledged bytes.
12. **Using `dig` to test what an application sees.** `dig` ignores `nsswitch.conf`, `/etc/hosts` and `search`; use `getent hosts` for the application's view.
13. **Treating retransmits as proof of a bad network** or **zero retransmits as proof of a good one**: the lab here retransmitted on a loss free `veth`, and a quiet connection retransmits nothing; use the rate of retransmits against segments sent.

---

## 12. Questions out loud

Answer each in two minutes, aloud, before reading the answer.

1. **A service times out from one client network only. How do you find where?** Check the symptom type (`refused` or `timed out`), `ip route get SERVER` on the client and `ip route get CLIENT from X iif IF` on each router, then **capture on both sides of each hop**; the first hop where the SYN arrives and does not leave is the culprit, and the usual causes are `ip_forward`, a firewall drop, or a missing return route.
2. **How do you tell a closed port from a firewall drop from a dead host?** Closed port: an immediate **RST** (`refused`). Firewall drop: **no answer**, SYN retransmissions at 1, 2, 4 seconds. A **reject** rule answers like a closed port or with ICMP unreachable. Dead host on the local segment: **ARP gets no answer**, `ip neigh` shows `FAILED` or `INCOMPLETE`.
3. **What does `Recv-Q 2 Send-Q 1` on a listening socket mean?** Two connections are complete and waiting for `accept()` against a backlog of 1 (the queue holds backlog plus one): the application is not accepting fast enough, and `ListenOverflows` counts the drops.
4. **A large response hangs but small ones work. Why?** A **path MTU black hole**: the handshake and small packets fit, the first full size segment is dropped by a smaller MTU, and the ICMP "fragmentation needed" that would fix it is blocked. Fix: allow ICMP type 3 code 4, clamp the MSS, or lower the MTU.
5. **How does masquerade differ from DNAT, and what must be true for each to work?** SNAT/masquerade rewrites the **source** in `postrouting` so replies return through the NAT box; DNAT rewrites the **destination** in `prerouting` for a port forward. Both need **`ip_forward=1`**, a **forward rule** that allows the traffic, and **connection tracking** to reverse the rewrite on replies.
6. **Where can a packet be dropped and still leave `ip -s link` at zero?** Netfilter (a rule), the qdisc (a shaper or full queue), a socket buffer (`UdpRcvbufErrors`), the accept queue, and the **conntrack** table. Each has its own counter.
7. **A TCP transfer is slow on a clean link. What do you read first?** `ss -ti` on the flow: `cwnd`, `rtt`, `retrans`, `rwnd_limited` and `sndbuf_limited`, then `nstat` for the retransmit rate, then the receiver's `Recv-Q`: who is the limit, the network, the receiver or the sender.
8. **Why do some lookups take exactly 2 or 5 seconds?** A resolver timeout: a dead first nameserver in `resolv.conf` (timeout 5 by default, 1 here), or a missing AAAA/search domain answer. `getent hosts` reproduces it and `dig @each-server` finds the bad one.
9. **What does a namespace give you, and which `sysctl` values does it change?** A private network stack (interfaces, routes, firewall, sockets, `/proc/net`) and its own `net.*` settings, such as `ip_forward` (0 on the host, 1 in the router namespace here) and `somaxconn`. It is the basis of containers and of a one machine lab.
10. **Packet loss of 1 percent was found on a link. Why does it matter so much?** Loss based TCP congestion control halves its window on each loss: throughput falls roughly as `MSS / (RTT x sqrt(loss))`; measured here, 1 in 100 cut the flow from 17.4 Gbit/s to 0.79 Gbit/s.
11. **What is `conntrack` and how does it fail?** A per flow state table that stateful rules and NAT depend on. It fails by **filling** (new flows are dropped and the log says `table full`) or by **holding stale entries** (a 5 day established timeout); size, time out and `notrack` accordingly.
12. **How do you build a network lab on one host?** Namespaces for the machines, `veth` pairs or a bridge for the wires, `ip_forward=1` in the router, `nft` for firewall, NAT and loss injection, `tc` for rate shaping and (where available) delay, and `tcpdump` in each namespace.

---

## 13. Problems

Do these before reading the answers. Each was run in the three namespace lab in this session; the answers section shows the output.

**Problem A.** The client pings the server through the router and gets 100 percent loss, yet the router **forwards** the requests. Show with `tcpdump` that the requests reach the server's side, find the **cause on the server** with one command, and fix it.

**Problem B.** A TCP connection from the client to the server through the router **times out**. Show on **both router interfaces** that the SYN arrives on the client side and **never leaves** the server side, find the setting responsible, and fix it.

**Problem C.** A server with a **listen backlog of 4** that is slow to accept refuses connections under a burst of 20 clients. Show the **listener's queue**, the **counters** that prove the overflow, and then show that a server that accepts promptly takes all 20.

**Problem D.** A transfer through a shaped link shows **1,000 or more retransmits** but **no loss on the interface**. Find **which layer** dropped the packets and prove that the **firewall is not involved**.

**Problem E.** The router link has an MTU of 1000, ICMP "fragmentation needed" is filtered, and a **100,000 byte** upload stalls although a 500 byte one works. Reproduce it, then **fix it on the router without touching the endpoints or the ICMP rule**.

---

## 14. Answers

### Problem A

```text
=== A: the server answers nothing, the router sees the requests
$ nse c ping -c 2 -W 1 10.2.0.2 | grep transmitted
2 packets transmitted, 0 received, 100% packet loss, time 1022ms
$ cat /tmp/lx08_a.txt
IP 10.1.0.2 > 10.2.0.2: ICMP echo request, id 9165, seq 1, length 64
IP 10.1.0.2 > 10.2.0.2: ICMP echo request, id 9165, seq 2, length 64
$ nse s ip route get 10.1.0.2 2>&1
RTNETLINK answers: Network is unreachable
$ nse c ping -c 2 -W 1 10.2.0.2 | grep transmitted
2 packets transmitted, 2 received, 0% packet loss, time 1004ms
```

The 100 percent loss is real, and **`tcpdump` on the router's server side shows the echo requests going out** (`10.1.0.2 > 10.2.0.2: ICMP echo request`) with no replies coming back. The server received them. The cause is on the server: **`ip route get 10.1.0.2` says `Network is unreachable`**: the server's default route was missing, and `10.1.0.2` is not on its connected network, so **it has no way to send the reply**. This is an **asymmetric path or missing return route**, which looks like loss from the client. Adding `ip route add default via 10.2.0.1` on the server fixed it (`2 received, 0% packet loss`). The method: **the request leaves, the reply does not come, so test the reverse path from the other end with `ip route get`.**

### Problem B

```text
=== B: a port forward that only times out
$ nse c python3 /tmp/lx08_try.py 10.2.0.2 5001
timed out
$ echo "on rc:"; cat /tmp/lx08_b1.txt; echo "on rs: $(grep -c IP /tmp/lx08_b2.txt) packets"
on rc:
IP 10.1.0.2.PORT > 10.2.0.2.5001: Flags [S]
IP 10.1.0.2.PORT > 10.2.0.2.5001: Flags [S]
on rs: 0 packets
$ nse r sysctl -n net.ipv4.ip_forward
0
$ nse c python3 /tmp/lx08_try.py 10.2.0.2 5001
connected
```

The client's connection **timed out**, which means nothing answered. On the router's client side two SYNs are visible (the original and the 1 second retransmission); on the server side **no packet** appeared (`on rs: 0 packets`). So the router **received and did not forward**. `sysctl net.ipv4.ip_forward` on the router read **0**; setting it to 1 made the connection succeed (`connected`). If the sysctl had been 1, the next suspects are the **forward chain of the firewall** (`nft list ruleset` counters), a **missing route** on the router, and a **missing return route** (Problem A).

### Problem C

```text
=== C: accept queue overflow with 20 clients and a backlog of 4
$ nse s ss -ltn sport = :5005 | tail -1 | tr -s " " | cut -d" " -f1,2,3
LISTEN 5 4
$ nse s nstat -az TcpExtListenOverflows TcpExtListenDrops | tail -2
TcpExtListenOverflows           3                  0.0
TcpExtListenDrops               3                  0.0
$ cat /tmp/lx08_c.txt
connect ok 5 timed out 15
$ cat /tmp/lx08_c.txt
connect ok 20 timed out 0
```

The listener with backlog 4 showed **`Recv-Q 5, Send-Q 4`** (queue full, plus one), **`ListenOverflows` and `ListenDrops` both 3** after 1.6 seconds, and the 20 clients got **5 connections and 15 time outs** (1 second timeout each). A server in the normal `accept` mode (backlog 128) accepted **all 20** (`connect ok 20 timed out 0`). The remedies in order: accept faster (more workers, an event loop, or hand off the accepted socket at once), then raise **both** the `listen()` backlog and `net.core.somaxconn`, then watch `nstat -az TcpExtListenOverflows` so it never happens silently again.

### Problem D

```text
=== D: retransmits, and the loss is in a queue, not on the wire
$ ipf
191 Mbits/sec retransmits 1281
$ nse r nft list ruleset | wc -l
0
$ nse r tc -s qdisc show dev rs | sed -n 1,2p
qdisc tbf 8008: root refcnt 3 rate 200Mbit burst 16Kb lat 163us
 Sent 74850808 bytes 49455 pkt (dropped 180, overlimits 28250 requeues 0)
$ nse r ip -s link show rs | sed -n 5p; nse r ip -s link show rs | sed -n 6p
    TX:  bytes packets errors dropped carrier collsns
      72242912    9923      0       0       0       0
```

The iperf3 flow ran at **about 190 Mbit/s** (the `tbf` was set to 200 Mbit/s) with **1,281 retransmits**. `nft list ruleset | wc -l` printed **0**: there is no firewall rule at all, so the firewall is **not** involved. **`tc -s qdisc show dev rs`** shows `dropped 180, overlimits 28,250`: **the shaper's queue dropped the packets**, which is how TCP found out it was above the allowed rate and slowed down. And **`ip -s link` for the same interface shows `dropped 0`** in the TX line: the qdisc's drops are not in the interface counters. So the order of the triage matters: the interface counters were clean while a lower level counter held the answer. The fix is either a bigger queue (`limit`) if the application is bursty, or a lower sending rate; **drops at a shaper are the design**, not a fault.

### Problem E

```text
=== E: a path MTU black hole, and the fix with MSS clamping
$ nse c python3 /tmp/lx08_big.py 500; nse c python3 /tmp/lx08_big.py 100000
   500 bytes: ok in 0.00 s
100000 bytes: stalled after 4.0 s
$ nse c python3 /tmp/lx08_big.py 500; nse c python3 /tmp/lx08_big.py 100000
   500 bytes: ok in 0.00 s
100000 bytes: ok in 0.00 s
$ nse r nft list chain inet lx mangle | grep maxseg
                tcp flags syn tcp option maxseg size set rt mtu
```

Without the fix, a **500 byte** upload completed instantly and a **100,000 byte** upload **stalled for 4 seconds** (the client's timeout): the **PMTU black hole** of section 5.2. The rule added on the router is **`tcp flags syn tcp option maxseg size set rt mtu`** in a `forward` chain: it rewrites the **MSS option in SYN packets** to **fit the outgoing route's MTU**, so that both ends **never send a segment larger than 1000 minus headers**, and no ICMP message is needed. After it, both uploads completed in 0.00 s. This is the standard fix for tunnels and PPPoE links (`iptables ... --clamp-mss-to-pmtu` is the old spelling); it only helps **TCP, and only connections that start after the rule exists**. (A chain called `fwd` would not parse in the first attempt: `fwd` is an `nft` keyword.)

---

## 15. Cheat sheet

```text
LAB
  ip netns add N ; ip netns exec N cmd ; ip netns del N
  ip link add a type veth peer name b ; ip link set a netns N
  ip link add br0 type bridge ; ip link set p master br0
  ip netns exec R sysctl -w net.ipv4.ip_forward=1       turn a host into a router

IP
  ip -br addr ; ip -br link ; ip -s link show IF        brief views, counters
  ip route ; ip route get DEST [from SRC iif IF]        what the kernel would do
  ip rule ; ip route show table N                       policy routing
  ip neigh                                              ARP: REACHABLE STALE FAILED
  traceroute -n ; tracepath -n ; mtr -n -r -c 10        hops; PMTU; loss per hop

SOCKETS
  ss -tan ; ss -tlnp ; ss -uan ; ss -s                  states, listeners, summary
  ss -ti ; ss -tmn                                      cwnd rtt retrans / memory
  ss state established '( dport = :443 )'               filters
  LISTEN: Recv-Q = accept queue, Send-Q = backlog       ESTAB: unread / unacked
  nstat -az ; nstat                                     absolute / change since last call
  ListenOverflows ListenDrops TcpRetransSegs UdpRcvbufErrors

CAPTURE
  tcpdump -nn -i IF -c N 'FILTER'  [-w f | -r f] [-S] [-s 96]
  host A  net N  port P  tcp  udp  icmp  greater N
  'tcp[tcpflags] & tcp-syn != 0'      flags: S  S.  .  P.  F.  R
  refused = RST ; timed out = no answer ; ICMP 3/4 = frag needed (PMTUD)

FILTER, NAT, STATE
  nft list ruleset ; nft add rule inet T C ... counter drop
  masquerade (postrouting) ; dnat to H:P (prerouting) ; ct state established,related accept
  conntrack -L ; conntrack -C ; conntrack -S ; nf_conntrack_max
  iptables is nft underneath: nft list ruleset shows both

LOSS TRIAGE (walk toward the wire, two readings)
  ss -ti / nstat -> nft counters, conntrack -S -> tc -s qdisc -> ip -s link, ethtool -S
  -> /proc/net/softnet_stat -> tcpdump both sides -> mtu (tracepath) -> mtr
  1% loss: 17.4 Gbit/s -> 0.79 Gbit/s ; interface counters read 0

NAMES AND TUNING
  dig @SRV NAME ; getent hosts NAME                     server view / application view
  /etc/nsswitch.conf  /etc/resolv.conf (timeout attempts search ndots)
  sysctl net.core.somaxconn net.ipv4.tcp_{rmem,wmem,congestion_control,tw_reuse} ip_local_port_range
  ethtool -i -k -g -S IF                                driver, offloads, rings, stats

NOT RUN HERE: tc netem, fq_codel, VLAN, bonding, conntrack table full, resolvectl, ethtool on a real NIC
```

---

## 16. Sources

- `ip(8)`, `ip-route(8)`, `ip-rule(8)`, `ip-netns(8)`, `ip-link(8)`: iproute2 manual pages. <https://man7.org/linux/man-pages/man8/ip.8.html>
- `ss(8)`: socket statistics, filters and the meaning of `Recv-Q` and `Send-Q`. <https://man7.org/linux/man-pages/man8/ss.8.html>
- `tcpdump(1)` and `pcap-filter(7)`: capture options and BPF filter syntax. <https://www.tcpdump.org/manpages/pcap-filter.7.html>
- nftables wiki: ruleset language, NAT, connection tracking, counters. <https://wiki.nftables.org/>
- Linux kernel documentation, **IP sysctl** (`ip-sysctl`): the meaning of the `net.ipv4.*` settings, including `tcp_tw_reuse`, `tcp_max_syn_backlog`, `ip_local_port_range`. <https://docs.kernel.org/networking/ip-sysctl.html>
- `tc(8)`, `tc-tbf(8)`, `tc-netem(8)`: qdisc and shaping; `netem` described from memory. <https://man7.org/linux/man-pages/man8/tc.8.html>
- `conntrack(8)`: connection tracking tools and timeouts. <https://manpages.debian.org/conntrack>
- `ethtool(8)`: offloads, ring and driver statistics. <https://man7.org/linux/man-pages/man8/ethtool.8.html>
- RFC 1191 (path MTU discovery) and RFC 8201 (IPv6 path MTU discovery). <https://www.rfc-editor.org/rfc/rfc1191> and <https://www.rfc-editor.org/rfc/rfc8201>
- Mathis, Semke, Mahdavi, Ott: *The macroscopic behavior of the TCP congestion avoidance algorithm* (1997), for the loss and throughput relation quoted from memory.
- This plan: [LX 03](03_processes_signals_and_proc.md) for namespaces and `/proc`, [LX 05](05_performance_methodology_and_triage.md) for the triage method, the [networking track](../07_networking/00_plan.md) for the protocols.
- Measurements: the scripts `x1` to `x6` and `ans_prob` of this session, Linux 6.18.44, one VM, as stated in the honest limits.
