# NET 13: Load balancing, proxies and anycast

*Part of the [networking plan](00_plan.md) · Time: about 8 hours · You need: [NET 11](11_routing_protocols_and_datacenter_fabrics.md) (ECMP and its hash in section 8, resilient next hop groups, the leaf-spine fabric and BGP in the datacenter), [NET 09](09_network_security_firewalls_ips_dpi.md) (connection tracking, NAT and SYN cookies), and [NET 07](07_zero_copy_networking.md) (XDP). A load balancer sits in front of many identical servers behind one address and decides, for every new connection, which server gets it. That one decision hides four problems: **choosing** a backend, **remembering** the choice for the rest of the connection, **noticing** that a backend died, and **routing the reply** back. This tutorial takes each in turn on a real Linux data path (nftables NAT and direct server return, an XDP selector, haproxy as a layer 7 proxy), measures what the sandbox can, and says which is which.*

"Design a load balancer that survives a backend being added or removed without breaking existing connections" is a standard question at Google, Meta and Cloudflare-style infrastructure teams, and "where would you put an L4 and where an L7 balancer" is the first follow-up to almost every system design question. A Staff level answer has five parts that connect: the **forwarding mode** (NAT, direct server return or encapsulation, and which direction carries the bytes), the **selection** (a consistent hash and what it does when the backend set changes), the **state** (where the flow table lives and what happens when it is lost), the **health model** (how long a dead backend stays in rotation) and the **front door** (anycast and ECMP, and what they do to long connections). This tutorial puts a measured number on each.

**Eight honest limits, stated first.**

1. **No IPVS.** The kernel (Linux 6.18, Firecracker VM, 2 vCPUs, root) has `CONFIG_IP_VS` switched off, so the classic Linux layer 4 balancer is **not here**: its three modes, its schedulers and its connection hash table are explained from memory and marked so. What runs instead is **nftables** (`dnat` with `numgen` and `jhash`, and a `netdev` ingress rule that rewrites the destination MAC and forwards), which implements the same ideas with the pieces exposed.
2. **No real NIC and no native XDP.** The XDP program in section 10 runs in **generic** mode on a veth device, so its **picks are real, but its speed is not representative**. Katran, DPDK balancers and NIC offload are from memory.
3. **No BGP in the anycast experiment.** Anycast in section 9 uses **static multipath routes and resilient next hop groups** on a router namespace, which is what a BGP-learned route becomes in the kernel after best path selection. The announcements, withdrawals and their timers were measured in NET 11 and are not repeated.
4. **The Maglev and consistent hash numbers come from my implementations** (`chash.py`, a few dozen lines, hashing with `blake2b`), not from Google's code. I implemented the table population from the paper's description **as I remember it**; I did not re-read the paper for this tutorial. The paper's own sizes and throughput figures are **from memory**.
5. **One host, three roles.** The client, the balancer and the backends are namespaces on the same two vCPUs, and the backends are small Python servers. **Throughput and latency figures are therefore relative, never absolute**; counts, byte totals and which connection went where are exact.
6. **TLS costs are measured as proxy CPU per connection** (a one thread haproxy answering 200 itself, read from `/proc`), because a client and a proxy fighting for two cores make a connections per second figure meaningless. The ticks are 10 ms, so the cost per connection has a resolution of 5 µs.
7. **haproxy is Ubuntu's 2.8.30.** Its defaults (`retries 3`, `http-reuse safe`) are the ones that shipped; other versions differ, and I name each setting I rely on.
8. **Hardware is from memory:** NIC receive-side scaling and its indirection table, a switch's ECMP hash seed and bucket count, how a hardware load balancer fails over its state, and every cloud provider's balancer internals.

By the end you can:

- **choose a forwarding mode and say what it costs** (measured: 300 requests of 100 KB through NAT mode put **31,474 KB** on the balancer's client side; the same 300 requests with direct server return put **0 KB** there and the balancer received **187 KB** against **604 KB**; a backend with no route back through the balancer lost **3 of 9** connections silently);
- **say what a stateful balancer holds** (measured: one conntrack entry carries both tuples, `src=10.0.1.2 dst=10.0.1.100` and the reply `src=10.0.12.2 dst=10.0.1.2`, and the client address the backends saw was the real one in all 3,000 connections);
- **compare four consistent hashes with numbers** (measured, 100,000 flows, 10 backends, one removed: **hash mod N** moved **89.8 %** with **79.7 %** collateral; a ring with 100 points moved **9.5 %** with **0 %** collateral and a busiest backend at **1.14** times the average; rendezvous **10.0 %**, **0 %**, **1.02**; Maglev with a 65,537 entry table **10.2 %**, **0.2 %**, **1.02**);
- **size a Maglev table** (measured: 100 backends, one removed: a table of 1,009 entries moved **4.22 %** with **3.19 %** collateral, 65,537 gave **1.54 %** and **0.55 %**, 655,373 gave **1.11 %** and **0.15 %**; ideal is 1 % and 0);
- **say what happens to open connections when the backend set changes** (measured, 300 connections held open through a rule change: **0** broke while the connection table was kept, **158** broke when the table was lost at the same moment; nftables `jhash ... mod N` is not a modulo: growing 2 backends to 3 kept **54 %** of flows, where a true modulo keeps 33 %);
- **put a number on health check delay** (measured, haproxy, 20 requests per second, one backend of three killed, `inter 1s fall 3`: **14 failed requests** in two seconds with `retries 0`; **0** with `retries 3` and `option redispatch`; computed: detection takes between `(fall - 1) × inter` and `fall × inter`);
- **say what an L7 proxy buys and costs** (measured: 1,000 requests, each on a new client connection, made **1,001** backend connections in `mode tcp` and with `http-reuse safe`, and **2** with `http-reuse always`; `/api` went to one backend and everything else to two others; the backend saw the proxy as its peer and the client in `X-Forwarded-For`);
- **cost TLS termination** (measured, proxy CPU per new connection, one thread: **40 µs** plain, **890 µs** TLS 1.3 with an RSA-2048 certificate, **500 µs** with ECDSA P-256, **405 µs** when the client resumes);
- **explain what anycast does to a long connection** (measured: three sites behind a 3-way ECMP route took **989, 1,021 and 990** of 3,000 connections; withdrawing one site broke **39** connections on it and **28 of 50** on a surviving site with a plain multipath route, but **0** surviving ones with a resilient group);
- **write the selection step of an XDP balancer** (measured, generic XDP: **1,006, 1,066, 928** picks of 3,000 by hash modulo 3; **1,017, 1,001, 982** from a 1,009 entry Maglev table; **1,463, 1,537, 0** after the table was rebuilt for 2 backends and written);
- **say why random choice is not enough** (simulated: 400 equal connections on 20 backends put the busiest at **1.46** times the average with random choice, **1.07** with the better of two random choices; with Pareto sized work the busiest stayed at **1.70** even with least loaded choice).

## Where the numbers come from

Each experiment is a script `eN_*.sh` in `prog/` with its output frozen in `v/`, run with `run.sh`. `lbnet.sh` builds the topology used by most of them: a client `c`, a balancer `lb` and three backends `b1` to `b3`, one veth pair per backend, the VIP `10.0.1.100` on the balancer. `be.py` and `be11.py` are the backends (HTTP/1.0 and HTTP/1.1 with keep-alive), `echo.py` an echo server for long connections, `cl.py`, `hold.py`, `fresh.py`, `rate.py` and `tlsrate.py` the clients. `chash.py` holds the four hashing schemes, `e3_hashing.py` compares them, `xdp_sel.c` is the XDP program with `xdp_fill.py` and `xdp_picks.py` around it. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, modelled or computed here is marked **from memory**. The hashing and balance simulations use fixed seeds or fixed flow lists, so each run prints the same figures.

## Data and timing

| What | Size or time |
|---|---|
| NAT mode (`e1_nat_lb.sh`) | 3 backends, 2 times 3,000 connections, 9 connections with a broken return path |
| Direct server return (`e2_dsr.sh`) | 300 requests of 100 KB in each mode, interface byte counters, `tcpdump` of 3 packets |
| Hashing (`e3_hashing.py`) | 100,000 flows, 6 schemes, 5 sections, about 8 s |
| Connection state (`e4_state.sh`) | 300 long connections twice, 400 fixed-port flows twice, about 1 minute |
| Health checks (`e5_haproxy.sh`) | 3 haproxy configurations, 12 s each at 20 requests per second |
| Layer 7 proxy (`e6_proxy.sh`) | 4 modes of 1,000 connections, a forwarded-for request, 200 routed requests |
| Anycast (`e7_anycast.sh`) | 3 sites, 3,000 connections, 2 times 150 long connections with a withdrawal |
| XDP selector (`e8_xdp.sh`) | one compiled program, 3 times 3,000 connections |
| TLS termination (`e9_tls.sh`) | 4 times 2,000 connections, proxy CPU from `/proc` |

---


## 1. What a load balancer decides

A **virtual IP (VIP)** is an address that belongs to a service, not to a machine. Clients connect to the VIP; a balancer takes each new connection and sends it to one of N **backends** (also called real servers). Everything else in this tutorial is a consequence of four questions:

| Question | What goes wrong if you ignore it |
|---|---|
| **Choose:** which backend gets this connection? | uneven load; or a choice that changes when the backend set changes (section 4) |
| **Remember:** how do later packets of the same connection reach the same backend? | open connections break whenever a backend is added or removed (section 5) |
| **Notice:** how do you learn a backend is dead? | clients are sent to a dead server for as long as detection takes (section 6) |
| **Return:** how does the reply get back to the client with the right addresses? | the client sees a reply from an address it never contacted, and drops it (sections 2 and 3) |

### 1.1 Layer 4 against layer 7

A **layer 4** balancer looks at the IP and TCP or UDP headers only: addresses, ports, flags. It never terminates the client's TCP connection; it rewrites or forwards packets, and the backend and the client end up in the **same** TCP connection. It is cheap (a table lookup per packet), it handles any protocol over TCP or UDP, and it cannot see a URL, a header, a cookie or a TLS server name. A **layer 7** balancer is a **proxy**: it **terminates** the client's connection, reads the request, picks a backend using what it read, and opens or reuses a **second** connection to that backend. It is costly (two sockets and a parser per request) and it can route by path, retry a failed request, add headers, terminate TLS and reuse backend connections. Production systems stack them: an L4 tier spreads connections over many L7 proxies, and the proxies spread requests over the services (section 11).

```text
client ---- VIP ----> [ L4: picks per connection, never reads a byte of payload ]
                          | same TCP connection continues to the backend
                          v
                      backend

client ---- VIP ----> [ L7 proxy: terminates TCP, parses HTTP, picks per request ]
                          |  second TCP connection (pooled, reused)
                          v
                      backend
```

### 1.2 Four ways to forward

| Mode | What the balancer does to the packet | Reply path | Costs and limits | Section |
|---|---|---|---|---|
| **NAT** (DNAT, sometimes with SNAT) | rewrites the destination address (and port) to the backend; remembers the translation | **through** the balancer, which translates back | the balancer carries both directions; needs connection state; backends must route replies via it | 2 |
| **Direct server return (DSR)** | rewrites only the destination **MAC** (same L2 segment) or **encapsulates** the packet (IP in IP, GRE) toward the backend | **straight** from the backend to the client, source address = the VIP | the balancer sees only requests; backends must own the VIP; no port translation; no L7 | 3 |
| **Proxy** (L7) | terminates the client connection and opens its own to the backend | back through the proxy, on the proxy's connection | per-request CPU; the backend sees the proxy as its peer; full flexibility | 7 |
| **Anycast in front** | does nothing itself: the network (ECMP over routes to the same prefix) picks a site or a balancer instance | direct | long connections break when the route set changes (section 9) | 9 |

The rest of the tutorial uses one topology, so the experiments compare like with like:

```bash
# lbnet.sh: client c -- lb -- b1 b2 b3, one veth per backend. VIP 10.0.1.100 lives on the lb.
lb_up() {
  for n in c lb b1 b2 b3; do ip netns add $n; ip netns exec $n ip link set lo up; done
  ip link add c0 netns c type veth peer name l0 netns lb
  ip netns exec c ip addr add 10.0.1.2/24 dev c0; ip netns exec lb ip addr add 10.0.1.1/24 dev l0
  ip netns exec c ip link set c0 up; ip netns exec lb ip link set l0 up
  for i in 1 2 3; do
    ip link add b0 netns b$i type veth peer name l$i netns lb
    ip netns exec b$i ip addr add 10.0.1$i.2/24 dev b0; ip netns exec lb ip addr add 10.0.1$i.1/24 dev l$i
    ip netns exec b$i ip link set b0 up; ip netns exec lb ip link set l$i up
    ip netns exec b$i ip route add default via 10.0.1$i.1
    ip netns exec b$i ethtool -K b0 tso off gso off gro off > /dev/null 2>&1
  done
  ip netns exec c ip route add default via 10.0.1.1
  ip netns exec lb sysctl -qw net.ipv4.ip_forward=1
  ip netns exec lb ip addr add 10.0.1.100/32 dev l0
}
lb_down() { for n in c lb b1 b2 b3; do ip netns del $n 2> /dev/null; done; }
```

The client `c` has one link to `lb`, `lb` has one link to each backend, and the VIP `10.0.1.100` is an address on the balancer's client-facing interface. Each backend's default route points at the balancer, which matters in section 2.

---

## 2. NAT mode: a stateful rewrite, measured

The simplest balancer is a `dnat` rule: a packet to the VIP is given the address of a chosen backend, and the kernel's connection tracking (**conntrack**, NET 09) remembers the translation so that later packets of the connection get the same one and the replies are translated back.

```bash
ip netns exec lb nft -f - <<'RULES'
table ip lbnat {
  chain pre { type nat hook prerouting priority dstnat;
    ip daddr 10.0.1.100 tcp dport 80 dnat to numgen inc mod 3 map { 0 : 10.0.11.2, 1 : 10.0.12.2, 2 : 10.0.13.2 }
  }
}
RULES
```

`numgen inc mod 3` is a counter that goes 0, 1, 2, 0, 1, 2; the map turns it into an address. It is **round robin per new connection**. The alternative is to **hash** something about the connection (`jhash ip saddr . tcp sport mod 3`) so that the choice is a function of the client and port. Both ran for 3,000 connections from the client:

```text
--- 1. nftables DNAT with a round-robin counter (numgen inc), 3000 connections
replies {'b1': 1000, 'b2': 1000, 'b3': 1000} failed 0 client address seen by backends {'10.0.1.2': 3000}
--- 2. the same with a hash of the client address and port (jhash), 3000 connections
replies {'b1': 1028, 'b2': 998, 'b3': 974} failed 0 client address seen by backends {'10.0.1.2': 3000}
```

Round robin is exact (**1,000 each**). The hash gives **1,028, 998 and 974**, which is a binomial spread: for 3,000 connections over 3 backends the standard deviation is `sqrt(3000 × 1/3 × 2/3)` = **25.8**, and 1,028 is 1.1 deviations from 1,000, so this is what a fair hash looks like. The reason to accept that spread is section 5: a hash needs **no per-balancer counter**, so two balancers with the same rule make the same choice.

Both runs also show the **client address the backends saw** was `10.0.1.2` for all 3,000: DNAT changes the destination only, so the backend sees the real client and can log it, rate limit it or geolocate it. That is NAT mode's advantage over a proxy.

### 2.1 What the balancer remembers

```text
--- 3. what the lb holds for one connection (conntrack)
tcp      6 119 TIME_WAIT src=10.0.1.2 dst=10.0.1.100 sport=44932 dport=80 src=10.0.12.2 dst=10.0.1.2 sport=80 dport=44932 [ASSURED] mark=0 use=1
```

One conntrack entry has two tuples. The **original** direction is `src=10.0.1.2 dst=10.0.1.100 sport=44932 dport=80`; the **reply** direction is what the backend will send: `src=10.0.12.2 dst=10.0.1.2 sport=80 dport=44932`. When the reply arrives from `10.0.12.2`, conntrack finds the entry by the reply tuple and rewrites the source to `10.0.1.100`, which is why the client accepts it. The entry also holds the TCP state (`TIME_WAIT` here, 119 seconds left) and the translation for **the life of the connection**. The cost is memory per connection (a few hundred bytes in the kernel's table, from memory) and a hard ceiling in `nf_conntrack_max`.

### 2.2 The return path is the whole design

For the reply to be translated back it must **pass through the balancer**. Section 4 of the experiment takes b1's default route away, so a reply to the client (not on b1's subnets) has nowhere to go:

```text
--- 4. b1 loses its route back through the lb, 9 connections (3 should go to b1)
replies {'b2': 5, 'b3': 1} failed 3 client address seen by backends {'10.0.1.2': 6}
```

Three of nine connections hung and timed out: exactly those hashed to b1. The SYN reached b1, the SYN-ACK was dropped for lack of a route, and the client waited out its 3 s timeout. Note what the **balancer** knew: nothing. Its rule kept sending a third of the traffic to a backend that could not answer, because forwarding has no idea whether the reply came back. Detecting that is a separate mechanism (section 6).

Three consequences to say aloud in an interview:

- **The balancer carries both directions**, so its bandwidth bounds the service, and for web traffic responses outweigh requests by a large factor (section 3: 31,474 KB went out toward the client against 604 KB that came in, mostly acknowledgements).
- **A backend must send its replies through the balancer.** If clients can reach backends directly (same subnet, a flat network), the reply skips the balancer, the client sees a reply from `10.0.12.2` to a connection it opened to `10.0.1.100`, and drops it. The usual fix is to **also SNAT** (rewrite the source to the balancer's address), at the price that the **backend loses the client address** (it must be passed in a header such as `X-Forwarded-For`, or in the PROXY protocol; section 7).
- **The state is the failure domain.** If the balancer restarts, every connection's translation is gone (section 5 measures what that does).

---

## 3. Direct server return: the reply skips the balancer

Replies are typically much larger than requests. **Direct server return** lets the backend answer the client itself. The balancer forwards only the **request** direction, without changing the IP header, and the backend owns the VIP, so it can send a reply whose source is the VIP and which the client accepts.

On a shared Ethernet segment the balancer needs to change only the **destination MAC**: the IP packet still says `dst = VIP`, but the frame goes to the chosen backend's NIC. Here each backend sits on its own link, and the `netdev` ingress hook plus `fwd to` does exactly that. The setup gives each backend the VIP on its loopback and a direct return path to the client:

```bash
for i in 1 2 3; do
  ip link add d$i netns c type veth peer name e$i netns b$i
  ip netns exec c ip addr add 10.0.2$i.1/24 dev d$i; ip netns exec b$i ip addr add 10.0.2$i.2/24 dev e$i
  ip netns exec c ip link set d$i up; ip netns exec b$i ip link set e$i up
  ip netns exec b$i ip addr add 10.0.1.100/32 dev lo
  ip netns exec b$i ip route add 10.0.1.2/32 via 10.0.2$i.1 dev e$i
  ip netns exec b$i sysctl -qw net.ipv4.conf.all.rp_filter=0 net.ipv4.conf.b0.rp_filter=0
  M[$i]=$(ip netns exec b$i cat /sys/class/net/b0/address)
done
```

and the balancer's rules, which pick a backend by hash (the same `seed` in all three rules matters: see below), rewrite the MAC and forward the frame:

```bash
ip netns exec lb nft -f - <<RULES
table netdev dsr {
  chain in { type filter hook ingress device "l0" priority 0;
    ip daddr 10.0.1.100 tcp dport 80 jhash ip saddr . tcp sport mod 3 seed 0x1234 0 ether daddr set ${M[1]} fwd to "l1"
    ip daddr 10.0.1.100 tcp dport 80 jhash ip saddr . tcp sport mod 3 seed 0x1234 1 ether daddr set ${M[2]} fwd to "l2"
    ip daddr 10.0.1.100 tcp dport 80 jhash ip saddr . tcp sport mod 3 seed 0x1234 2 ether daddr set ${M[3]} fwd to "l3"
  }
}
RULES
```

Three details a real deployment must get right:

- **The VIP on `lo`, not on the backend's NIC.** The backend must accept packets for the VIP but must **never answer ARP for it** on the shared segment, or clients would reach the backend directly and bypass the balancer. On a shared segment this means `arp_ignore` and `arp_announce` settings (from memory). Point to point links, as here, avoid the problem and so hide it.
- **`rp_filter` off** on the client and the backends: the reply arrives on an interface whose reverse path to the VIP is a different one.
- **One seed.** My first version had three rules with `jhash` and no seed. Each rule got its **own random seed**, so a packet matched none of the three about `(2/3)^3` = **30 %** of the time, fell through to the balancer's own TCP stack, and was refused: **86 of 300** connections failed (29 %). Writing `seed 0x1234` in all three rules fixed it. Hash-and-dispatch must use one hash value per packet.

The measurement: 300 requests of 100 KB, with the balancer's client-side interface counters read before and after.

```text
--- 1. NAT mode: replies come back through the lb (300 requests, 100 KB each)
replies {'b1': 96, 'b2': 109, 'b3': 95} failed 0 client address seen by backends {'10.0.1.2': 300}
NAT mode: lb sent 31474 KB toward the client, received 604 KB from it
--- 2. direct server return: the lb rewrites the destination MAC and forwards, the backends answer on a separate path
replies {'b1': 95, 'b2': 96, 'b3': 109} failed 0 client address seen by backends {'10.0.1.2': 300}
DSR     : lb sent 0 KB toward the client, received 187 KB from it
```

In NAT mode the balancer sent **31,474 KB** toward the client: all the response bytes, 300 × 100 KB plus headers. With DSR it sent **0 KB**: nothing came back through it. That is the point of the mode. The 604 KB against 187 KB received is mostly an artifact of my links, **not a property of DSR**: the direct return links kept TCP segmentation offload on, so the backends sent 7,240 byte segments and the client acknowledged less often, while the balancer-side links had it off. The reply as the client saw it, on the direct link:

```text
--- 3. what one reply looks like on the client's direct link (source is the VIP, no NAT anywhere)
01:25:31.554662 IP 10.0.1.100.80 > 10.0.1.2.45324: Flags [S.], seq 2656507811, ack 366198158, win 65160, options [mss 1460,sackOK,TS val 3794368265 ecr 2305753496,nop,wscale 10], length 0
01:25:31.554694 IP 10.0.1.100.80 > 10.0.1.2.45324: Flags [.], ack 19, win 64, options [nop,nop,TS val 3794368265 ecr 2305753496], length 0
01:25:31.554940 IP 10.0.1.100.80 > 10.0.1.2.45324: Flags [P.], seq 1:7241, ack 19, win 64, options [nop,nop,TS val 3794368266 ecr 2305753496], length 7240: HTTP: HTTP/1.0 200 OK
```

The source is `10.0.1.100`, the VIP, in the first packet to the last: **no NAT anywhere**, and the client cannot tell which backend answered.

### 3.1 What DSR costs

| Limit | Why |
|---|---|
| Layer 2 adjacency, or a tunnel | the MAC trick works only on one segment; across routers the packet must be **encapsulated** toward the backend (IP in IP adds **20 bytes**, GRE **24**, so the effective MTU shrinks or MSS is clamped; large web balancers used encapsulation for this reason, from memory) |
| No port translation | the packet reaches the backend with the VIP's port, so every backend serves the service on the same port |
| No layer 7 | the balancer never sees the response, and for the request it must pick from the first packet of the connection (no URL) |
| The balancer sees half the connection | it cannot see the connection end, so it relies on timeouts for its own state (section 5) |
| Asymmetric paths | a stateful firewall on the return path sees replies to connections it never saw start |
| Health | a backend that stops answering is invisible in the forwarding path: health checks are the **only** signal (section 6) |

**Say it like this:** "NAT mode puts the balancer in both directions, which simplifies everything and caps throughput at the balancer; DSR puts it only in the request direction, which is typically a small fraction of the bytes, at the cost of layer 2 adjacency or encapsulation and the loss of anything layer 7."

---

## 4. Choosing a backend: hashing and what it does when the set changes

A balancer that keeps no per-connection state must compute the **same** backend for every packet of a connection, so the choice has to be a function of the packet: a hash of the 5-tuple, or of the 4-tuple without the protocol. That immediately raises the question section 5 answers: what if the backend set changes? A good choice function has three properties:

- **Balance:** each backend gets about `1/N` of the flows.
- **Minimal disruption:** when a backend is removed, only the flows that were on it move; when one is added, only about `1/(N+1)` of the flows move to it.
- **Speed:** a lookup per packet, ideally one memory access.

Four schemes, implemented in 29 lines so that the experiment below is on code you can read:

```python
# chash.py: four ways to map a flow to a backend. Each builds from a list of names and returns a function flow -> name.
import hashlib, bisect
def h(s, salt=""):
    return int.from_bytes(hashlib.blake2b((salt + s).encode(), digest_size=8).digest(), "big")
def mod_n(names):
    return lambda f: names[h(f) % len(names)]
def ring(names, vnodes):
    pts = sorted((h(n + "#" + str(i)), n) for n in names for i in range(vnodes))
    keys = [p[0] for p in pts]
    return lambda f: pts[bisect.bisect(keys, h(f)) % len(pts)][1]
def rendezvous(names):
    return lambda f: max(names, key=lambda n: h(f, n))
def maglev_table(names, m):
    n = len(names)
    off = [h(x, "offset") % m for x in names]
    skip = [h(x, "skip") % (m - 1) + 1 for x in names]
    nxt, table, filled = [0] * n, [None] * m, 0
    while True:
        for i in range(n):
            c = (off[i] + nxt[i] * skip[i]) % m
            while table[c] is not None:
                nxt[i] += 1
                c = (off[i] + nxt[i] * skip[i]) % m
            table[c] = names[i]; nxt[i] += 1; filled += 1
            if filled == m:
                return table
def maglev(names, m):
    table = maglev_table(names, m)
    return lambda f: table[h(f) % m]
```

- **`mod_n`**: the hash modulo the number of backends. Perfect balance, and **terrible** disruption: the residue of a hash changes for almost every value when N changes.
- **`ring`** (consistent hashing, Karger et al.): each backend owns `vnodes` points on a circle of hash values; a flow belongs to the next point clockwise. Removing a backend gives its arcs to the neighbours only. Balance improves with more points, at the price of a bigger structure and a binary search per lookup.
- **`rendezvous`** (highest random weight): score every backend with `hash(flow, backend)` and take the highest. Ideal balance and disruption, but **O(N) hashes per lookup**: fine in a control plane, expensive per packet.
- **`maglev`** (Google, from memory of the paper): build a lookup **table** of `M` entries once, then a lookup is **one hash and one array index**. Each backend has its own **permutation** of the table slots (a starting `offset` and a step `skip`); the backends take turns claiming the next free slot in their permutation until the table is full. Each backend ends up with `M/N` slots, plus or minus one, and a removal frees only that backend's slots, which are refilled by the others, so most slots keep their owner.

**`M` must be prime.** The permutation `offset + k × skip` mod `M` visits every slot only if `skip` and `M` share no factor. My first run used `M = 655`, which is `5 × 131`, and the program **hung**: a backend whose `skip` shared a factor with `M` cycled through a subset of slots that other backends had already filled, and the `while` loop never found a free one. With a prime `M` (653, 1,009, 10,007, 65,537 and 655,373 below) it terminates.

The comparison used 100,000 flows and 10 backends:

```text
--- 1. one of 10 backends is removed: share of flows that change backend (ideal 10.0), of which collateral (flows that were on a surviving backend, ideal 0.0)
scheme                moved % collateral %
hash mod N               89.8        79.7
ring, 100 points          9.5         0.0
ring, 1000 points         9.6         0.0
rendezvous               10.0         0.0
Maglev, M=653            12.7         2.8
Maglev, M=65537          10.2         0.2
--- 2. a backend is added to 10: share of flows that change backend (ideal 9.1)
scheme                moved %
hash mod N               90.9
ring, 100 points          8.0
ring, 1000 points         9.0
rendezvous                9.0
Maglev, M=653            11.9
Maglev, M=65537           9.3
--- 3. balance with 10 backends: busiest backend divided by the average (ideal 1.00)
hash mod N            1.01   (least loaded 0.99)
ring, 100 points      1.14   (least loaded 0.89)
ring, 1000 points     1.07   (least loaded 0.96)
rendezvous            1.02   (least loaded 0.99)
Maglev, M=653         1.02   (least loaded 0.99)
Maglev, M=65537       1.02   (least loaded 0.98)
```

Reading the tables:

- **`hash mod N`: 89.8 % of flows move** when 1 of 10 backends leaves, against an ideal of 10 %; and **79.7 %** of all flows were on a *surviving* backend and still moved. Adding an 11th backend moves 90.9 % against an ideal of 9.1 %. This is why a modulo of the backend count is the wrong answer to "what happens when you add a server".
- **Ring:** 9.5 % and 0 % collateral at 100 points per backend, but the busiest backend is **1.14** times the average and the least loaded 0.89: with 100 points each, the arc lengths vary by about `1/sqrt(100)` = 10 %. At 1,000 points the busiest drops to **1.07**. Balance costs memory.
- **Rendezvous:** **10.0 %** moved, **0 %** collateral, busiest **1.02**. The best numbers, paid for with 10 hashes per flow here and 100 at 100 backends.
- **Maglev:** with a small table (`M = 653`, 65 entries per backend) **12.7 %** moved with **2.8 %** collateral; with `M = 65,537` **10.2 %** and **0.2 %**. Balance is the same **1.02** in both, which is the point of the design: a flat table gives each backend an almost equal count of slots whatever `M` is. What `M` buys is **fewer collateral moves**.

```text
--- 4. cost: time to build for 100 backends, memory of the structure
ring, 1000 points      0.12 s
Maglev, M=65537        0.09 s
--- 5. Maglev table size against disruption: 100 backends, one removed (ideal 1.0 moved, 0.0 collateral), 100,000 flows
M           moved % collateral %  busiest
1009           4.22        3.19     1.13
10007          2.75        1.75     1.07
65537          1.54        0.55     1.08
655373         1.11        0.15     1.09
```

Section 5 of that output is the sizing rule. With **100** backends and one removed, the ideal is 1 % of flows moved and none collateral:

| `M` | entries per backend | moved | collateral |
|---|---|---|---|
| 1,009 | 10 | 4.22 % | 3.19 % |
| 10,007 | 100 | 2.75 % | 1.75 % |
| 65,537 | 655 | 1.54 % | 0.55 % |
| 655,373 | 6,554 | 1.11 % | 0.15 % |

**Collateral falls as the table gets bigger, and it is still above zero at 6,554 entries per backend.** The paper's recommendation of `M` at least 100 times the number of backends is from memory; these numbers show why that is a floor, not a comfortable margin. A table of `M` entries of 4 bytes is 262 KB at 65,537 and 2.6 MB at 655,373, so the table is a cache pressure question as well (from memory: the paper used 65,537 for small and 655,373 for large pools). Building the table for 100 backends at `M = 65,537` took **0.09 s** here, against 0.12 s for a ring with 1,000 points, so rebuilding on every health change is cheap.

**Weights** (a backend with twice the capacity) fit each scheme: more ring points, more turns in the Maglev population, or a scaled rendezvous score (from memory; I did not run a weighted comparison).

---

## 5. Remembering the choice: state, and what a restart does

There are two ways to make every packet of a connection reach the same backend.

- **Stateless:** compute the backend from the packet each time (section 4). Nothing to store, nothing to lose when a balancer restarts, and **two balancers with the same function and the same backend list agree**, which is what lets many balancers sit behind ECMP (section 9). The price is that the function's answer changes when the backend list changes.
- **Stateful:** a **flow table** keyed by the 5-tuple. The first packet of a connection is assigned by the function (or by load); the result is stored; later packets look it up. Changing the backend list affects **new** connections only. The price is memory, a table that can be **filled by an attacker** with SYNs (NET 09 measured a table limited to 1,000 entries going under in a one-way flood), and state that is **lost** when the balancer restarts.

Real balancers use both: the table pins existing connections, and the consistent hash decides new connections and the packets whose entry was evicted (from memory of the Maglev paper; a Maglev instance keeps a local connection table and does not share it with other instances).

### 5.1 Measured: change the rule, keep or lose the table

The nftables rule hashes `ip saddr . tcp sport` over **2** backends. Three seconds into a run with 300 long connections, the rule is replaced by one that hashes over **3**:

```bash
rule() { # N: hash over the first N backends
  local m="0 : 10.0.11.2, 1 : 10.0.12.2, 2 : 10.0.13.2"
  ip netns exec lb nft flush chain ip lbnat pre
  ip netns exec lb nft add rule ip lbnat pre ip daddr 10.0.1.100 tcp dport 9000 dnat to jhash ip saddr . tcp sport mod 2 seed 7 map { 0 : 10.0.11.2, 1 : 10.0.12.2 }
}
```

```bash
ip netns exec lb nft flush chain ip lbnat pre
ip netns exec lb nft add rule ip lbnat pre ip daddr 10.0.1.100 tcp dport 9000 dnat to jhash ip saddr . tcp sport mod 3 seed 7 map { 0 : 10.0.11.2, 1 : 10.0.12.2, 2 : 10.0.13.2 }
ip netns exec lb conntrack -F > /dev/null 2>&1
```

In the first run the rule change alone; in the second, the rule change **and** `conntrack -F`, which empties the connection table at the same moment, the way a restart or a failover to a second balancer with no shared state would.

```text
--- 1. 300 long connections through a hash over 2 backends; mid-run the hash is changed to 3 backends
opened 300, broken after 8 s: 0
  on b1: 147 opened, 0 broken
  on b2: 153 opened, 0 broken
(conntrack entries were kept: only new connections see the new rule)
--- 2. the same change, but the lb loses its connection table at the same moment (restart, failover to a second lb)
opened 300, broken after 8 s: 158
  on b1: 155 opened, 60 broken
  on b2: 145 opened, 98 broken
```

With the table kept, **0 of 300** connections broke: the rule change affected only new connections, because the NAT translation lives in conntrack. With the table lost, **158 of 300** broke. Each broken connection is one whose next packet was hashed to a different backend, which had no socket for it and answered with a reset.

### 5.2 `jhash ... mod N` is not a modulo

A true `hash % N` going from 2 to 3 would keep a flow only when `h % 2 == h % 3`, which is 2 values of 6, so **33 %** would keep their backend and **67 %** would break. The run shows **53 %** broken (158 of 300), and the split by backend is lopsided: **60 of 155** (39 %) on b1 against **98 of 145** (68 %) on b2. Section 3 of that experiment looks at the cause directly: the same 400 flows (fixed source ports) under both rules.

```text
--- 3. the same 400 flows (fixed source ports) under the 2-backend rule, then under the 3-backend rule
  b1 -> b1: 146 flows
  b1 -> b2: 59 flows
  b2 -> b2: 71 flows
  b2 -> b3: 124 flows
  flows that kept their backend: 54 %
```

b1 flows never went to b3, and b2 flows never went to b1. That is the pattern of a hash **scaled into N ranges**, not a modulo: with the 32-bit hash as a fraction `x` of the whole range, the 2-backend rule gives b1 `x < 1/2`, and the 3-backend rule gives b1 `x < 1/3`, b2 `1/3 <= x < 2/3`, b3 `x >= 2/3`. Flows that stay are `[0, 1/3)` and `[1/2, 2/3)`, which is **1/2** of all flows, and the four counts (146, 59, 71, 124 of 400) match the predicted `1/3, 1/6, 1/6, 1/3`. I inferred the scaling from these pairs; I did not read nftables' source. The consequence is useful: a range-scaled hash moves **fewer** flows than a modulo when N grows by one (a half, not two thirds), the same property as the "hash threshold" that Linux applies to multipath routes (section 9). It is still far from the `1/(N+1)` of a consistent hash: it stays a poor answer for a growing pool.

### 5.3 How big does a flow table have to be?

A table of `C` entries with new connections arriving at `R` per second, evicting the least recently used entry, forgets an **idle** connection after about `C / R` seconds. Computed, with 64 bytes per entry (my assumption):

| entries `C` | new flows per second `R` | an idle connection survives | memory |
|---|---|---|---|
| 1,000,000 | 10,000 | 100 s | 64 MB |
| 1,000,000 | 100,000 | 10 s | 64 MB |
| 10,000,000 | 1,000,000 | 10 s | 640 MB |
| 100,000,000 | 1,000,000 | 100 s | 6.4 GB |

A connection forgotten while the backend set changed is re-hashed, and with a Maglev table of 65,537 entries and 100 backends a removal re-hashes about **0.55 %** of surviving flows (section 4), so the share of long idle connections broken after `k` removals is `1 - (1 - 0.0055)^k`: 0.5 % after one, 5.4 % after ten (problem B). The design lesson is that **table size sets how long idle connections are safe**, and the consistent hash sets how much is lost when the table forgets.

### 5.4 Draining and failing over

- **Removing a backend gracefully (draining):** take it out of the hash for **new** connections and let the table carry the existing ones until they end or a time limit passes. Stateless balancers cannot drain; they can only move flows (from memory).
- **Failing over between balancers:** if the two balancers compute the same function over the same backend list, a flow whose table entry is missing on the new balancer still reaches its backend. Anything that makes the lists differ for a moment (two health checkers that disagree) breaks the flows in between, in proportion to the collateral figure of the hash in use. That is the argument for Maglev style tables over a modulo.

---

## 6. Noticing failure: health checks and the window they leave open

A forwarding rule has no idea whether a backend works (section 2.2: a third of the connections hung with no signal at the balancer). **Health checks** supply it. An **active** check probes each backend on a schedule: a TCP connect, an HTTP request that must return a given status, or a script. A **passive** check watches real traffic for errors and timeouts (from memory; not run here). Each backend is marked down after `fall` consecutive failed checks and up again after `rise` consecutive successes; `inter` is the time between checks.

### 6.1 The detection window

If a backend dies at a random moment, the first failing check comes between 0 and `inter` later, and it is marked down at the `fall`-th failure. The window during which requests are still sent to it is therefore between `(fall - 1) × inter` and `fall × inter` (add the check's own timeout when the failure is silence rather than a refusal). Measured with haproxy, three backends round robin, 20 requests per second, b1 killed 3 s into a 12 s run, `inter 1s fall 3`:

```bash
  cat <<CFG
global
  maxconn 4096
defaults
  mode http
  timeout connect 1s
  timeout client 5s
  timeout server 5s
frontend fe
  bind 10.0.1.100:80
  default_backend be
backend be
  balance roundrobin
  option httpchk GET /
$1
  server b1 10.0.11.2:80 check inter 1s fall 3 rise 2
  server b2 10.0.12.2:80 check inter 1s fall 3 rise 2
  server b3 10.0.13.2:80 check inter 1s fall 3 rise 2
CFG
```

```text
--- 1. no retry (retries 0): b1 is killed 3 s into a 12 s run at 20 requests per second (check inter 1s fall 3)
requests 240, failed 14, slower than 0.5 s 0 (slowest 0.0 s); failures per second: 0 0 0 7 7 0 0 0 0 0 0 0
--- 2. the default (retries 3, same server each time): b1 is killed 3 s into a 12 s run at 20 requests per second (check inter 1s fall 3)
requests 240, failed 2, slower than 0.5 s 2 (slowest 2.0 s); failures per second: 0 0 0 0 0 1 0 1 0 0 0 0
--- 3. retries 3 with redispatch: b1 is killed 3 s into a 12 s run at 20 requests per second (check inter 1s fall 3)
requests 240, failed 0, slower than 0.5 s 0 (slowest 0.0 s); failures per second: 0 0 0 0 0 0 0 0 0 0 0 0
```

- **`retries 0`:** a request that lands on b1 gets a refused connection and an error, **14 failures**, 7 in each of two seconds. That is `20 requests/s × 1/3` = 6.7 per second for **2.1 s** of detection time, inside the predicted 2 to 3 s.
- **The default (`retries 3`):** only **2 failures**, but **2 requests were slower than 0.5 s and the slowest took 2.0 s**, the client's timeout. My client sends one request at a time, so a request stuck behind retries to the dead server holds up everything after it: two stalls of 2 s covered the whole 4 s window. A real population of clients would see **dozens** of stalled requests, not two. I did not read how haproxy spaces its retries; the stall is what I observed.
- **`retries 3` with `option redispatch 1`:** **0 failures** and no slow request: a refused connection is retried on **another** server, and the client saw no failure and no slow request.

The lesson is general. **Retrying on a different backend hides detection delay for failures that are fast and safe to retry** (a refused connection, nothing sent yet). It does not help for failures that are slow (a hung server holds each request until the timeout) or unsafe (a `POST` that may have been processed). Retries also multiply load on a struggling service (a retry storm), which is why services bound them with a **retry budget** (from memory).

Choosing the numbers is arithmetic (problem C): halving `inter` halves the window but doubles the check traffic, and a check that is too sensitive (`fall 1`) flaps a healthy backend on one dropped probe. `rise` bigger than `fall` keeps a half-recovered backend out until it is stable.

### 6.2 What to check

- **Shallow** (TCP connect): proves the process listens, not that it works.
- **Deep** (HTTP `GET /healthz` that touches the database): proves more, and a shared dependency failing makes **every backend fail at once**, taking the whole service out. The usual compromise: the check reports the process's own health and a separate signal covers dependencies (from memory).
- **Where the check comes from:** from the balancer, over the **same path** real traffic uses. With DSR a check to the backend's own address does not prove that the VIP path works (from memory).

---

## 7. Layer 7 proxies: what you buy, and the connection pool

A layer 7 proxy terminates the client's TCP connection and makes its own to the backend. Four things follow that a layer 4 balancer cannot do.

**1. Route by what is in the request.** Measured with haproxy: requests to `/api` go to b1, everything else is balanced over b2 and b3.

```bash
cfg() { # mode extra-backend-lines
  cat > /tmp/p.cfg <<CFG
global
  maxconn 4096
defaults
  mode $1
  timeout connect 1s
  timeout client 5s
  timeout server 5s
  timeout http-keep-alive 5s
frontend fe
  bind 10.0.1.100:80
  default_backend be
backend be
  balance roundrobin
$2
  server b1 10.0.11.2:80
CFG
  [ -n "$HP" ] && kill $HP 2> /dev/null && wait $HP 2> /dev/null
  ip netns exec lb haproxy -f /tmp/p.cfg > /dev/null 2>&1 &
  HP=$!; sleep 1
}
```

```text
--- 3. layer 7 routing: /api goes to b1, everything else to b2 and b3 (a layer 4 balancer cannot see the path)
100 requests to /api/x:
replies by backend {'b1': 100}
100 requests to /img/y:
replies by backend {'b2': 50, 'b3': 50}
```

**2. Reuse backend connections.** 1,000 requests, each on its **own new client connection**, to one backend:

```text
--- 1. 1000 requests, each from a new client connection, to one backend: connections the backend accepted
mode tcp (no http-reuse line):
    backend b1 accepted 1001 connections for 1001 requests (the stats request itself is one of each)
mode http   http-reuse never:
    backend b1 accepted 1001 connections for 1001 requests (the stats request itself is one of each)
mode http   http-reuse safe:
    backend b1 accepted 1001 connections for 1001 requests (the stats request itself is one of each)
mode http   http-reuse always:
    backend b1 accepted 2 connections for 1001 requests (the stats request itself is one of each)
```

In `mode tcp` the proxy passes bytes: every client connection makes **one backend connection**, 1,001 (the 1,000 plus the stats request). In `mode http` the proxy knows where requests start and end, but with `http-reuse never` it still opens one backend connection per request (1,001). With `http-reuse safe`, which is haproxy's default and the setting I did not change, the result is also **1,001**: from memory of its documentation, "safe" lets a request reuse a backend connection only for later requests on the **same client connection**, and each client connection here carried one request. With `http-reuse always` the backend accepted **2** connections for 1,001 requests: the proxy kept a pool of idle keep-alive connections and handed one to each request. A backend that accepts 2 connections instead of 1,001 spends no time in `accept`, no kernel memory on connections in `TIME_WAIT`, and, if the backend leg is TLS, **no TLS handshakes** (section 8). The risk of `always` is that connections are shared **across clients**: a backend that keeps per-connection state (some authentication schemes) would mix users.

**3. Tell the backend who the client was.** The backend's peer is the proxy:

```text
--- 2. what the backend sees: the proxy is the peer, the client address travels in a header (option forwardfor)
last reply: b1 connections=1 requests=1 path=/who peer=10.0.11.1 xff=10.0.1.2
```

`peer=10.0.11.1` is the proxy's address, and `xff=10.0.1.2` is the real client, added by `option forwardfor` as an `X-Forwarded-For` header. A header can be forged by a client, so a backend must trust it only from the proxy and the proxy must **replace or append**, never pass a client's header through (from memory). For **non HTTP** protocols and for TLS passthrough there is no header: the **PROXY protocol** prepends one line (v1, text) or a binary block (v2) to the TCP stream with the original source and destination addresses (from memory; not run here).

**4. Retry, limit, observe.** Retrying a request on another backend (section 6), rate limiting per key, per-request latency and status counters: these exist at L7 because the proxy sees requests.

**Costs.** Per request the proxy parses headers and copies bytes in and out; it holds **two** sockets per connection; it must buffer or stream bodies and set **three timeouts** (`connect`, `client`, `server`) so that a slow client cannot hold a backend (the slow-loris class of attack; from memory). The boundary between proxy and backend is where HTTP request smuggling happens, when the two parse a request differently (from memory). And a proxy fleet is itself something to balance (section 11).

---

## 8. TLS termination: what the handshake costs the balancer

A proxy that **terminates** TLS does the handshake for the whole backend fleet. Three arrangements:

| Arrangement | Proxy sees | Cost |
|---|---|---|
| **Termination:** TLS to the client, plain HTTP to the backend | everything | one handshake per client connection, no TLS on the backend leg |
| **Re-encryption:** TLS to the client, TLS to the backend | everything | handshake costs on both legs; pooling (section 7) amortises the backend leg |
| **Passthrough:** the proxy forwards TLS bytes unread, choosing the backend by the **SNI** in the ClientHello (from memory) | only the server name | no decryption cost; no L7 features; the backend holds the certificate |

The cost of terminating was measured as the proxy's **CPU time per new connection**, from `/proc`, for a single haproxy thread that answers 200 itself (so no backend is involved), 2,000 connections per row from a Python client:

```bash
cpu() { awk '{print $14 + $15}' /proc/$HP/stat; }   # user + system ticks of the proxy, 100 per second
cost() { # label port mode: 2000 connections, proxy CPU per connection
  local a=$(cpu); ip netns exec c python3 prog/tlsrate.py 10.0.1.100 $2 2000 $3; local b=$(cpu)
  printf '%-34s proxy CPU %5d us per connection\n' "$1" $(( (b - a) * 10000 / 2000 ))
}
echo "--- 1. haproxy CPU per new connection, 2000 connections each (answers 200 itself, one thread; ticks are 10 ms)"
cost "plain HTTP" 80 plain
cost "TLS 1.3, RSA-2048 certificate" 443 tls
cost "TLS 1.3, ECDSA P-256 certificate" 444 tls
echo "--- 2. the same with the client resuming its session"
cost "TLS 1.3 resumed, RSA-2048" 443 resume
```

```text
--- 1. haproxy CPU per new connection, 2000 connections each (answers 200 itself, one thread; ticks are 10 ms)
plain HTTP                         proxy CPU    40 us per connection
TLS 1.3, RSA-2048 certificate      proxy CPU   890 us per connection
TLS 1.3, ECDSA P-256 certificate   proxy CPU   500 us per connection
--- 2. the same with the client resuming its session
TLS 1.3 resumed, RSA-2048          proxy CPU   405 us per connection
```

| | CPU per connection | connections per second per core, `1,000,000 ÷ cost` |
|---|---|---|
| plain HTTP | 40 µs | 25,000 |
| TLS 1.3, RSA-2048 certificate | 890 µs | 1,124 |
| TLS 1.3, ECDSA P-256 certificate | 500 µs | 2,000 |
| TLS 1.3, session resumed, RSA-2048 | 405 µs | 2,469 |

- **A full handshake costs 12 to 22 times a plain connection**, and the difference is cryptography, not data: none of these connections carried more than "ok".
- **The certificate's key type matters:** the server signs the handshake, and an RSA-2048 signature costs about **390 µs** more than an ECDSA P-256 one here (890 against 500). An ECDSA certificate is the cheaper choice for the fleet.
- **Resumption saves the signature, not the key exchange:** the resumed RSA run cost **405 µs**, which is a little below the ECDSA figure. TLS 1.3 resumption normally still performs an ephemeral key exchange (from memory), which is why a resumed connection is far from free. Resumption across a proxy fleet needs the proxies to **share ticket keys** (from memory).
- **These are one VM's numbers**, with a client sharing the two vCPUs, so use the ratios. They say: a fleet's capacity for *new* TLS connections is two orders of magnitude lower than for plain ones, so a client population that reconnects constantly (mobile networks, short lived connections) needs far more proxy cores than a steady one, and **keeping connections alive** is a capacity decision, not just a latency one.

NET 08 covers the handshake record by record and the 0-RTT replay risk; this section adds only what it costs the balancer.

---

## 9. Anycast and ECMP: the network as the first balancer

**Anycast** means the same address (usually a /32 for a VIP) is **announced from several places**, and the routing system sends each client to one of them. Between sites it is BGP's shortest path; inside a site, when several balancer instances announce the VIP to the same router, the router sees **equal cost paths** and spreads flows over them with **ECMP** (NET 11 section 8). Nothing stateful is involved: the router hashes the packet header and picks a next hop. That makes anycast the cheapest way to scale the layer 4 tier horizontally, and it is why long connections are the problem.

The experiment: a router `r`, three sites `s1` to `s3` each owning `10.99.0.1/32` on `lo`, a 3-way multipath route, and the hash set to include ports (`fib_multipath_hash_policy=1`; the default hashes addresses only, from memory):

```bash
echo "--- 1. anycast address 10.99.0.1 behind a 3-way ECMP route, 3000 new connections (hash over addresses and ports)"
ip netns exec r ip route add 10.99.0.1/32 nexthop via 10.9.1.2 nexthop via 10.9.2.2 nexthop via 10.9.3.2
ip netns exec c python3 prog/cl.py 10.99.0.1 3000
```

```text
--- 1. anycast address 10.99.0.1 behind a 3-way ECMP route, 3000 new connections (hash over addresses and ports)
replies {'s1': 989, 's2': 1021, 's3': 990} failed 0 client address seen by backends {'10.9.0.2': 3000}
```

3,000 connections spread **989, 1,021 and 990**: a fair hash over 5-tuples. A client population with **few source addresses** and the default address-only hash would land on one site; the port-aware hash fixes that for many connections from few clients.

### 9.1 Withdrawing a site breaks flows that were not on it

Now 150 long connections, and at 3 s site `s1` is **withdrawn** (the route stops using it; its server keeps running so the experiment can see where each connection ends up). Two kinds of route:

```bash
echo "--- 2. 150 long connections; site s1 is withdrawn at 3 s (its server keeps running, the route stops using it)"
echo "plain multipath route (the hash is taken modulo the number of next hops):"
ip netns exec c python3 prog/hold.py 10.99.0.1 150 8 &
HP=$!; sleep 3
ip netns exec r ip route replace 10.99.0.1/32 nexthop via 10.9.2.2 nexthop via 10.9.3.2
wait $HP
echo "resilient next hop group (128 buckets):"
ip netns exec r ip route del 10.99.0.1/32
ip netns exec r ip nexthop add id 1 via 10.9.1.2 dev r1; ip netns exec r ip nexthop add id 2 via 10.9.2.2 dev r2; ip netns exec r ip nexthop add id 3 via 10.9.3.2 dev r3
ip netns exec r ip nexthop add id 100 group 1/2/3 type resilient buckets 128 idle_timer 120
ip netns exec r ip route add 10.99.0.1/32 nhid 100
ip netns exec c python3 prog/hold.py 10.99.0.1 150 8 &
HP=$!; sleep 3
ip netns exec r ip nexthop del id 1
wait $HP
```

```text
--- 2. 150 long connections; site s1 is withdrawn at 3 s (its server keeps running, the route stops using it)
plain multipath route (the hash is taken modulo the number of next hops):
opened 150, broken after 8 s: 67
  on s1: 39 opened, 39 broken
  on s2: 50 opened, 28 broken
  on s3: 61 opened, 0 broken
resilient next hop group (128 buckets):
opened 150, broken after 8 s: 61
  on s1: 61 opened, 61 broken
  on s2: 46 opened, 0 broken
  on s3: 43 opened, 0 broken
```

| | on the withdrawn site `s1` | on `s2` | on `s3` | survivors broken |
|---|---|---|---|---|
| **plain multipath route** | 39 of 39 | **28 of 50** | 0 of 61 | **28 of 111** |
| **resilient next hop group** (128 buckets) | 61 of 61 | 0 of 46 | 0 of 43 | **0 of 89** |

All the connections on `s1` break in both cases, which is expected: they are re-hashed to a server that has no socket for them. The difference is the **collateral**. With a plain multipath route, **28 of the 50 connections on `s2`**, a site that never failed, broke: the kernel divides the hash range into N regions, one per next hop (called hash-threshold, from memory of the kernel documentation), and removing the first next hop shifts the boundaries. `s2`'s region `[1/3, 2/3)` becomes `[0, 1/2)`, so the part of it above `1/2` now belongs to `s3`: half of `s2`'s flows, and the 28 of 50 observed match. `s3`'s region only grows, so `s3` lost nothing. A **resilient group** has a fixed table of buckets, each assigned to one member; removing a member reassigns **only its buckets**, so no survivor's flows move.

NET 11 introduced the resilient group for ECMP in the fabric; this is the same mechanism applied to a balancer tier, and the numbers say why it is worth the extra configuration. Two caveats:

- **Granularity:** a group with `B` buckets can only divide load in multiples of `1/B`. With 100 members and 128 buckets some member has two buckets against an even share of 1.28, so the busiest is **1.56** times the average (problem D). Use many more buckets than members.
- **Adding a member back:** moving buckets to the returning member would break their flows, so the kernel moves a bucket only when it has been idle for `idle_timer` (from memory of the nexthop documentation; the group above set `idle_timer 120`). Until then the load is uneven.

### 9.2 Making the tier behind ECMP agree

If a router's ECMP set changes (a balancer is added or fails), some flows arrive at a **different balancer instance**. For a stateless balancer using a consistent hash over the same backend list, the new instance sends the flow to the **same backend** as the old one: the state it lacks is not needed. That is the design of Google's Maglev, as I remember the paper: ECMP at the router, a local connection table per instance, and the same consistent hash table everywhere. It is why section 4's collateral figures matter in production: they are the fraction of flows lost when two instances' views of the backend set differ for a moment (health checkers disagreeing, or a table update reaching one instance first).

### 9.3 What anycast does not give you

- **Failure detection is the routing protocol's:** a dead site stays in the route until BGP withdraws it, or until the node's own agent withdraws it after its health check fails; NET 11 measured convergence after carrier loss and silent failure, with hold timers and with BFD. The time a failed balancer keeps attracting traffic is `health detection + withdrawal + FIB update` (from memory for the sum).
- **TCP is only as stable as the route:** a route flap or a topology change mid-connection can send packets to another site, which resets them. Anycast fits UDP (DNS) well and TCP when routes are stable or the connections are short.
- **No load awareness:** ECMP hashes, so one heavy flow stays on one path (NET 11 section 8, and NET 12 for what that does to collectives).

---

## 10. Data planes: nftables, IPVS, XDP

Where the forwarding rule runs decides its cost per packet and how it fails.

| Data plane | Where | State | Strength | Limit |
|---|---|---|---|---|
| **nftables / iptables NAT** (sections 2 and 3) | netfilter hooks, with conntrack | conntrack entry per flow | already in the kernel and scriptable | conntrack cost on every packet; a `dnat` per new flow |
| **IPVS** | kernel module `ip_vs`, a connection hash table | its own table | built for balancing: `ipvsadm`, three modes **NAT, DR (direct routing), TUN (IP in IP)**, schedulers including round robin, weighted least connection, source hash and Maglev hashing, connection sync between a master and a backup | **not in this kernel** (`CONFIG_IP_VS` off); everything in this row is **from memory** |
| **XDP** (Katran style) | driver receive path, before the stack allocates an `sk_buff` | BPF maps | highest packet rate on one core; no conntrack | needs driver support for native mode; you write the whole forwarding logic |
| **L7 proxy** (haproxy, nginx, Envoy) | user space | per connection | all of section 7 | sockets and parsing per request |

### 10.1 An XDP backend selector, measured

The program parses Ethernet, IPv4 and TCP, hashes the 4-tuple, finds a backend, counts the pick, and **passes the packet on** (the nftables rule from section 2 still does the real forwarding; this program shows the **selection step** a Katran-like balancer performs, and counts what it picked). It runs in **generic** XDP mode on a veth device, so the picks are real and the speed is not representative.

```c
SEC("xdp")
int select_backend(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data, *end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > end || eth->h_proto != bpf_htons(ETH_P_IP)) return XDP_PASS;
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > end || ip->protocol != IPPROTO_TCP || ip->ihl != 5) return XDP_PASS;
    struct tcphdr *tcp = (void *)(ip + 1);
    if ((void *)(tcp + 1) > end) return XDP_PASS;
    __u32 h = mix(ip->saddr, tcp->source, ip->daddr ^ tcp->dest), zero = 0;
    __u32 *n = bpf_map_lookup_elem(&mode, &zero);
    if (!n) return XDP_PASS;
    __u32 idx = h % M, pick;
    if (*n == 0) {                       // hash modulo a number of backends
        pick = h % 3;
    } else {                             // one lookup in a table that user space fills
        __u32 *t = bpf_map_lookup_elem(&table, &idx);
        if (!t) return XDP_PASS;
        pick = *t;
    }
    __u64 *c = bpf_map_lookup_elem(&picks, &pick);
    if (c && tcp->syn && !tcp->ack) __sync_fetch_and_add(c, 1);
    return XDP_PASS;
}
```

Reading it, the parts every XDP balancer has:

- **Bounds checks before every header access.** The verifier rejects a program that might read past `data_end`; each `if ((void *)(x + 1) > end)` is what lets the load succeed.
- **A hash of the flow**, here a small mixing function over source address, source port and a combination of destination address and port.
- **A table in a BPF map** (`table`, 1,009 entries) that **user space fills**: the lookup is `table[h % M]`. This is the Maglev lookup of section 4 in the kernel: one hash, one array read.
- **A per-backend counter** (`picks`), incremented only on a SYN, so each connection is counted once.

A real forwarder would continue after the lookup: set the outer destination (add an outer IP header for IP in IP with `bpf_xdp_adjust_head`, or rewrite the MAC), and return `XDP_TX` to send the frame out of the same interface, with a per-CPU LRU hash map for pinned flows (from memory of Katran's design; I did not run any of that).

`xdp_fill.py` builds the table with `chash.maglev_table` and writes it with `bpftool batch`; the experiment compares the two modes and then rebuilds the table for 2 backends:

```text
--- 1. mode 0: the program hashes the 4-tuple and takes it modulo 3; 3000 connections
replies {'b1': 1048, 'b2': 1008, 'b3': 944} failed 0 client
picks per backend (new connections seen by the program): be0 1006  be1 1066  be2 928
--- 2. mode 1: one lookup in a 1009 entry Maglev table for 3 backends, filled by user space
replies {'b1': 999, 'b2': 955, 'b3': 1046} failed 0 client a
picks per backend (new connections seen by the program): be0 1017  be1 1001  be2 982
--- 3. backend be2 is removed: the table is rebuilt for 2 backends and written, then 3000 new connections arrive
replies {'b1': 1055, 'b2': 940, 'b3': 1005} failed 0 client
picks per backend (new connections seen by the program): be0 1463  be1 1537  be2 0
```

Hash modulo 3 gave **1,006, 1,066, 928** of 3,000; the Maglev table gave **1,017, 1,001, 982**; after removing backend 2 the table sent **1,463 and 1,537** to the two survivors and **0** to the removed one. The program is the same in all three; **only the table changed**, which is the property that makes a control plane able to add and remove backends without touching the data plane. One caution: `bpftool batch` updates the 1,009 entries one at a time, so a packet arriving mid-update sees a mixture of old and new entries. A production design writes the new table into a second map and **swaps** (from memory), so every packet sees one consistent table.

---

## 11. Putting it together: a path, a failure walk, and what to check

### 11.1 The usual shape

```text
clients
   |  DNS (geo) or BGP anycast  -> picks a site
   v
site edge: ECMP over L4 balancer instances (all announce the VIP)
   |  consistent hash, DSR or encapsulation, no shared state     (sections 3, 4, 9)
   v
L7 proxy fleet: TLS termination, pooling, retries, routing        (sections 7, 8)
   |  per request, least loaded or two choices
   v
services: backends with health checks                             (section 6)
```

The L4 tier is sized by **packets**, the L7 tier by **new connections and requests**, the services by their own work. Because the tiers fail differently, each needs its own detection and its own consistency story.

### 11.2 Sizing arithmetic, with the numbers measured here

An example with **stated assumptions**, to show the method (the per-connection costs come from section 8, on this VM; use your own hardware's): 50,000 new TLS connections per second, ECDSA certificate, answered by one thread per core.

- TLS handshakes alone: `50,000 × 500 µs` = **25 cores** of proxy handshake work (RSA-2048: `50,000 × 890 µs` = **44.5 cores**). Plain HTTP at 40 µs each would be 2 cores; the same load needs **12 to 22 times** more cores once terminated.
- Bytes: 200,000 responses per second of 20 KB is **4 GB/s = 32 Gb/s** toward clients. In **NAT mode** the balancer must carry all of it; with **DSR** it carries the requests only, which for 1 KB requests is **1.6 Gb/s** (an assumption: a 1 KB request is a guess). That is the case for DSR at the L4 tier even though the L7 proxies, which must see the responses, cannot use it.

### 11.3 A failure walk

| What fails | Who notices | After how long | What breaks |
|---|---|---|---|
| A backend process | health check | `(fall - 1) × inter` to `fall × inter`, measured 2.1 s at `inter 1s fall 3` | requests to it until detected, unless retried on another server (measured: 14 failures with `retries 0`, 0 with redispatch) |
| An L7 proxy instance | ECMP router: health or BGP | seconds, set by BFD and BGP timers (NET 11) | its in-flight connections; the rest re-hashed: 28 of 111 survivors with a plain multipath route, 0 with a resilient group |
| An L4 balancer instance | the same | the same | with identical consistent tables 0.2 to 0.55 % collateral (measured, section 4); with a modulo and lost state about half (measured: 158 of 300) |
| A bad deploy on every backend | the same health check, all at once | one detection window | the whole service, if health checks depend on a shared dependency (section 6.2) |

### 11.4 What to configure and check

- **One hash, one seed, everywhere**: every L4 instance must compute the same function over the same backend list; a per-rule random seed (section 3) or a per-instance seed breaks agreement. Hash seeds **between tiers** should differ, or the polarisation of NET 11 section 8 appears.
- **MTU on the path:** encapsulation adds 20 bytes (IP in IP) or 24 (GRE); an unclamped MSS gives connections that stall on large responses.
- **Return path:** backends' default route through the balancer (NAT), the VIP on `lo` with ARP suppressed and `rp_filter` off (DSR).
- **Capacity of state:** `nf_conntrack_max`, the flow table size against new connection rate (section 5.3), the SYN flood defence of NET 09.
- **Health checks:** depth, `inter`, `fall`, `rise`, and what they depend on.
- **Draining:** a procedure that takes a backend out of new flows and waits for old ones before stopping it.
- **Timeouts:** an idle timeout in the balancer shorter than the clients' keepalive interval resets healthy idle connections (from memory).
- **Observability:** per backend connection counts, error and retry rates, health state changes, and the L4 tier's table occupancy.

---

## 12. Common mistakes

1. **Using `hash mod N` and calling it consistent.** Removing 1 of 10 backends moved **89.8 %** of flows with **79.7 %** collateral (section 4). Use a ring, rendezvous hashing or a Maglev table, or keep a flow table.
2. **Assuming `jhash ... mod N` in nftables is a modulo.** It behaves as a range-scaled hash: growing 2 to 3 backends kept **54 %** of flows, not 33 % (section 5.2). The behaviour is fine; the mistake is predicting disruption with the wrong model.
3. **Different hash seeds on different rules or instances.** Three `jhash` rules without a common seed let **86 of 300** connections match no rule (section 3); two balancers with different seeds disagree about every flow.
4. **A Maglev table size that is not prime.** `M = 655` hung the population loop (section 4). Use a prime, and one at least 100 times the backend count; at 100 backends even `M = 65,537` left **0.55 %** collateral.
5. **Sending replies around the balancer in NAT mode.** The client drops a reply from an address it never contacted; with the return route missing, **3 of 9** connections hung with no signal at the balancer (section 2.2).
6. **SNAT without passing the client address on.** The backend's logs, rate limits and geolocation see the balancer. Pass `X-Forwarded-For` or the PROXY protocol, and trust the header only from the proxy (section 7).
7. **DSR on a shared segment without suppressing ARP for the VIP.** A backend that answers ARP for the VIP pulls client traffic directly to itself and bypasses the balancer (section 3; from memory for the settings).
8. **Thinking DSR saves nothing because requests are small.** That is exactly why it saves: **31,474 KB** crossed the balancer in NAT mode and **0 KB** with DSR for the same 300 requests. But it costs layer 2 adjacency or encapsulation, and the MTU (section 3.1).
9. **Treating the flow table as free.** It is the failure domain: losing it at a rule change broke **158 of 300** connections, keeping it broke **0**; and an attacker can fill it (NET 09).
10. **Setting health checks without doing the arithmetic.** `inter 1s fall 3` leaves a 2 to 3 s window (measured: **14 failed requests** at 20 requests per second with no retries). A check every 100 ms at 100 proxies is **1,000** checks per second at each backend (problem C).
11. **Letting a deep health check depend on a shared dependency.** When the database fails, every backend fails its check together and the balancer removes the whole service (section 6.2).
12. **Trusting retries blindly.** A retry on another backend hid detection delay completely (**0** failures) for a refused connection, but it does nothing for a hung server, can duplicate a non-idempotent request, and multiplies load on a struggling service.
13. **Believing `mode http` pools backend connections by itself.** 1,000 requests on new client connections made **1,001** backend connections in `mode tcp`, with `http-reuse never` and with the default `safe`; only `http-reuse always` brought it to **2**, with the cross-client risk that carries (section 7).
14. **Counting TLS as a latency cost only.** A full TLS 1.3 handshake cost the proxy **890 µs** (RSA-2048) against **40 µs** for plain TCP, so capacity for new connections falls by 12 to 22 times (section 8). Keep connections alive and prefer ECDSA.
15. **Using a plain multipath route behind anycast and expecting graceful changes.** Withdrawing one of three sites broke **28 of 50** connections on a site that never failed; a resilient group broke **0** (section 9).
16. **Too few buckets in a resilient group.** With 100 members and 128 buckets the busiest member carries **1.56** times the average (problem D).
17. **Assuming random choice balances.** 400 equal connections on 20 backends put the busiest at **1.46** times the average with random choice; with heavy-tailed work it stayed at **1.70** even for least loaded choice (problem E).
18. **Presenting an XDP program in generic mode as a speed result.** The picks are real; the speed is not. The program also only counted: forwarding was nftables (section 10).

---

## 13. Twelve interview questions

Each has an outline of a good answer; the section to go back to is in brackets.

1. **L4 against L7 load balancing?** L4 sees addresses and ports and keeps one TCP connection end to end; it is cheap and protocol agnostic. L7 terminates the connection, parses the request, and routes per request, with retries, header changes, TLS termination and pooled backend connections, at a per-request CPU cost. Production stacks both. [1, 7]
2. **NAT mode against DSR?** NAT: both directions through the balancer, which keeps state and the backends see the client address; DSR: only requests through it, replies direct with the VIP as source, needs L2 adjacency or encapsulation, no port translation, no L7. Measured: 31,474 KB against 0 KB on the balancer's client side. [2, 3]
3. **Why does a backend need the VIP on a loopback in DSR, and what must it not do?** To accept packets addressed to the VIP and reply with it as source; it must not answer ARP for the VIP on a shared segment or it bypasses the balancer. [3]
4. **Design a balancer that survives adding and removing backends without breaking connections.** A flow table pins existing connections; a consistent hash (Maglev table, ring or rendezvous) chooses new ones and covers table misses; every instance uses the same function and backend list, so failover between instances loses little; drain a backend by removing it for new flows only. [4, 5]
5. **How does Maglev hashing work, and what does `M` do?** A table of `M` prime entries is filled by backends taking turns claiming the next free slot in their own pseudo-random permutation; lookup is `table[hash % M]`. Each backend gets `M/N` slots; removal frees only its slots. `M` sets how many other flows move: collateral fell from 3.19 % to 0.15 % as `M` went from 1,009 to 655,373 at 100 backends. [4]
6. **Compare mod N, ring, rendezvous and Maglev.** Modulo moves almost everything; ring is cheap but needs many points for balance (busiest 1.14 at 100 points); rendezvous is ideal but costs O(N) hashes per lookup; Maglev is O(1) lookup with near-ideal balance and a little extra disruption. [4]
7. **What happens to existing connections when the balancer restarts?** If it keeps per-flow state in memory, the state is gone and flows are re-hashed: with a modulo, about half broke (158 of 300 measured); with a consistent table that every instance shares, only the collateral fraction. State sync or a stateless design avoids the loss. [5]
8. **How long can a dead backend keep receiving traffic, and how do you reduce the cost?** Between `(fall - 1) × inter` and `fall × inter`, plus the check timeout for silent failures; measured 2.1 s at `inter 1s fall 3`. Retry refused connections on another backend, shorten the window within a check budget, and add passive checks. [6]
9. **What does a layer 7 proxy do to the backend's view of a connection, and how is the client address preserved?** The peer is the proxy; the client address goes in `X-Forwarded-For` (HTTP) or the PROXY protocol (any TCP). Trust it only from the proxy. [7]
10. **How expensive is TLS termination, and how do you cut it?** A full handshake cost 12 to 22 times a plain connection at the proxy here (500 to 890 µs against 40 µs). Use ECDSA certificates, resumption with shared ticket keys, long lived client connections and pooled backend connections. [8]
11. **Explain anycast for a service, and what goes wrong with long connections.** The VIP is announced from several places; routers pick by path and ECMP. A change in the next hop set re-hashes flows; with a plain multipath route 28 of 50 connections on a healthy site broke when another site left, with a resilient group none. [9]
12. **Where would you put XDP in a balancer, and what does it not do for you?** In the L4 tier: parse, hash, look up a table that the control plane updates, encapsulate and `XDP_TX`, with an LRU map for pinned flows; it gives the highest packet rate per core and no conntrack, but you own forwarding, health, draining and the MTU effects. [10]

---

## 14. Problems

Do each on paper first, then with the script named. The scripts run from `/home/claude/net13` (or your copy of `prog/`), and the parameters are at the top of each file.

**Problem A (paper, then `ans_a.py`): which hash for a 20 backend pool?** Twenty backends, one leaves. For a ring with 100 and 1,000 points, rendezvous hashing, and Maglev tables of 1,009, 10,007 and 65,537 entries: what share of flows move, how many of those moves are collateral, and how unbalanced is the busiest backend? Which schemes meet "collateral under 1 % and busiest under 1.10", and which would you pick if each lookup must take one memory access? *Approach:* predict from section 4's tables (ideal move is 5 %), then run `python3 prog/ans_a.py`.

**Problem B (paper, then `ans_b.py`): how long is an idle connection safe?** A balancer keeps a flow table with LRU eviction. For tables of 1 million to 100 million entries and 10,000 to 1 million new flows per second: how long does an idle connection survive, how much memory does the table need, and what share of long idle connections is broken after `k` backend removals if the consistent table re-hashes 0.55 % of survivors per removal? *Approach:* the idle lifetime is `C ÷ R`; the broken share is `1 - (1 - 0.0055)^k`; then run `python3 prog/ans_b.py`.

**Problem C (paper, then `ans_c.py`): choose the health check numbers.** For `inter` from 2 s to 0.1 s with `fall` 2 or 3: the minimum and maximum time a dead backend stays in rotation, and the failed requests for a service at 20 requests per second over 3 backends and at 20,000 requests per second over 50. How many checks per second does each backend receive from 100 proxies at `inter 0.1s`? *Approach:* the window is `(fall - 1) × inter` to `fall × inter`; failures are `rate ÷ backends × window`; compare with the measured 14 failures at `inter 1s fall 3`, then run `python3 prog/ans_c.py`.

**Problem D (paper, then `ans_d.py`): bucket count for a resilient group.** For 3, 10, 50, 100 and 200 members and 64, 128, 1,024 and 8,192 buckets: how much more than an even share does the busiest member carry? When one of 10 members leaves a 128 bucket group, how many buckets does each remaining member gain, and is the group still balanced? *Approach:* the busiest member has `ceil(B/N)` buckets against `B/N`; then run `python3 prog/ans_d.py`.

**Problem E (script, `ans_e.py`): is random choice enough?** For 20 backends and 400, 4,000 or 40,000 connections, compare random choice, the better of two random choices and least loaded choice, first with equal connections and then with Pareto sized work. What is the busiest backend over the average in each case, and why does least loaded choice not reach 1.00 with heavy tailed work? *Approach:* run `python3 prog/ans_e.py`; reason about the largest single connection relative to the average load per backend.

---

## 15. Answers

### Problem A: hash for 20 backends

```python
# ans_a.py: 20 backends, one leaves. Moved flows, collateral and balance for six ways of choosing a backend.
import sys, collections
sys.path.insert(0, "prog")
from chash import *
FLOWS = ["10.2.%d.%d:%d" % (i // 250 % 250, i % 250, 1024 + i * 11 % 60000) for i in range(100000)]
B = ["be%d" % i for i in range(20)]
print("%-20s %8s %11s %9s" % ("scheme", "moved %", "collateral %", "busiest"))
for name, mk in [("ring, 100 points", lambda n: ring(n, 100)), ("ring, 1000 points", lambda n: ring(n, 1000)), ("rendezvous", rendezvous),
                 ("Maglev, M=1009", lambda n: maglev(n, 1009)), ("Maglev, M=10007", lambda n: maglev(n, 10007)), ("Maglev, M=65537", lambda n: maglev(n, 65537))]:
    f, g = mk(B), mk(B[:-1])
    a = [f(x) for x in FLOWS]; b = [g(x) for x in FLOWS]
    moved = sum(1 for p, q in zip(a, b) if p != q); coll = sum(1 for p, q in zip(a, b) if p != q and p != "be19")
    c = collections.Counter(a)
    print("%-20s %8.2f %11.2f %9.2f" % (name, 100.0 * moved / len(a), 100.0 * coll / len(a), max(c.values()) / (len(a) / 20.0)))
```

scheme                moved % collateral %   busiest
ring, 100 points         4.70        0.00      1.13
ring, 1000 points        5.18        0.00      1.07
rendezvous               4.99        0.00      1.03
Maglev, M=1009           8.20        3.28      1.02
Maglev, M=10007          6.00        0.89      1.02
Maglev, M=65537          5.42        0.36      1.03

With 20 backends the ideal is **5.0 %** moved and **0 %** collateral. The **ring** (4.70 and 5.18 % moved) and **rendezvous** (4.99 %) all have **0.00 %** collateral. Of the Maglev tables only 65,537 comes near: **0.36 %** collateral, against **0.89 %** at 10,007 and **3.28 %** at 1,009. Balance (the busiest over the average): rendezvous **1.03**, Maglev **1.02 to 1.03**, ring **1.13** at 100 points and **1.07** at 1,000. So "collateral under 1 % and busiest under 1.10" is met by the ring with 1,000 points, by rendezvous, and by Maglev at 10,007 and 65,537; it is not met by the ring with 100 points (busiest 1.13) or by Maglev at 1,009 (3.28 % collateral). If a lookup must be one memory access, only the **Maglev table** qualifies (a ring needs a binary search, rendezvous 20 hashes), and 65,537 entries is the choice: 262 KB at 4 bytes per entry.

### Problem B: idle connection lifetime

```text
   entries C    new flows/s  idle lifetime memory at 64 B
     1000000          10000        100.0 s        64 MB
     1000000         100000         10.0 s        64 MB
     1000000        1000000          1.0 s        64 MB
    10000000         100000        100.0 s       640 MB
    10000000        1000000         10.0 s       640 MB
   100000000        1000000        100.0 s      6400 MB

a flow idle longer than that is forgotten; if the table changed while it was forgotten, it is re-hashed:
with a Maglev table of M=65537 and 100 backends a removal re-hashes about 0.55 % of surviving flows (e3 section 5)
so the expected share of long idle flows broken by one removal is about 0.55 %, and by ten removals in that time about 5 %
share broken after k removals: k=1: 0.5 %  k=2: 1.1 %  k=5: 2.7 %  k=10: 5.4 %  k=20: 10.4 %
```

The lifetime is `C ÷ R`: 100 s for a 1 million entry table at 10,000 new flows per second, but **1 s** at 1 million per second, where any connection idle for a second is forgotten. Memory at 64 bytes per entry (my assumption) is 64 MB at a million entries and 6.4 GB at a hundred million. With 0.55 % collateral per removal, **0.5 %** of long idle connections break after one removal and **5.4 %** after ten (**10.4 %** after twenty). The calculation assumes removals are independent and that every idle connection was forgotten, so it is an upper bound for connections that stayed in the table.

### Problem C: health check numbers

```text
inter, fall        min detect max detect | failed requests: 20 req/s over 3 backends | 20,000 req/s over 50 backends
2.0s, 3                  4.0s       6.0s |    33 (up to    40)             |   2000 (up to   2400)
1.0s, 3                  2.0s       3.0s |    17 (up to    20)             |   1000 (up to   1200)
1.0s, 2                  1.0s       2.0s |    10 (up to    13)             |    600 (up to    800)
0.5s, 3                  1.0s       1.5s |     8 (up to    10)             |    500 (up to    600)
0.2s, 2                  0.2s       0.4s |     2 (up to     3)             |    120 (up to    160)
0.1s, 2                  0.1s       0.2s |     1 (up to     1)             |     60 (up to     80)

measured in e5 (inter 1 s, fall 3, 20 req/s, retries 0): 14 failures, which is 14 / (20/3) = 2.1 s of one third of the traffic
check traffic: 1 check per backend per inter; with 50 backends and 100 proxies at inter 0.1 s the backends see 1000 checks per second
```

These are **arithmetic from the formula**, not new measurements. The window for `inter 1s fall 3` is 2 to 3 s, and at 20 requests per second over 3 backends the expected failures are `6.67 × 2.5` = **17** (up to **20**); the measured run had **14**, inside the range of 13 to 20 that the window predicts (`6.67 × 2 s` to `6.67 × 3 s`), with a detection time of **2.1 s**. At 20,000 requests per second over 50 backends the same setting costs **1,000** failed requests per dead backend; `0.2 s, 2` brings it to **120**, at the price of five times the check traffic of `1 s`. A check every 100 ms from 100 proxies is **1,000 checks per second** at each backend, which is why large fleets use fewer, centralised checkers or shared health state (from memory).

### Problem D: buckets in a resilient group

```text
 sites      B=64     B=128    B=1024     B=8192
     3      1.03      1.01      1.00      1.00
    10      1.09      1.02      1.01      1.00
    50      1.56      1.17      1.03      1.00
   100      1.56      1.56      1.07      1.00
   200      3.12      1.56      1.17      1.00

when one of N sites leaves, its buckets are shared among the others: each other site gains about B/N/(N-1) buckets
N=3, B=128: leaving site had 43 buckets, each of the 2 others gains 21.3; the busiest other site now has 64 buckets against an even share of 64.0
N=10, B=128: leaving site had 13 buckets, each of the 9 others gains 1.4; the busiest other site now has 15 buckets against an even share of 14.2
N=50, B=1024: leaving site had 21 buckets, each of the 49 others gains 0.4; the busiest other site now has 21 buckets against an even share of 20.9
```

The busiest member carries `ceil(B/N) ÷ (B/N)` of an even share, so the table has to be **much larger than the member count**: 100 members need 1,024 buckets to reach **1.07**, and 128 buckets give **1.56**. When one of 10 members leaves a 128 bucket group, the 13 buckets it held spread over 9 members: each gains **1.4** on average, so some gain 1 and some 2 buckets, and the busiest survivor has **15** buckets against an even share of **14.2**: balanced to within about 5 %. At 3 members the 43 buckets split into 21 and 22 exactly. A group of 50 members on 1,024 buckets is balanced within 3 % and hardly notices one leaving.

### Problem E: is random choice enough?

```python
# ans_e.py: 20 backends, 4000 connections with Pareto-sized work. Busiest backend over the average, for random choice, power of two choices and least loaded.
import random
random.seed(5)
def trial(conns, k, heavy):
    work = [random.paretovariate(1.5) if heavy else 1.0 for _ in range(conns)]
    res = {}
    for name in ("hash (random)", "two choices", "least loaded"):
        load = [0.0] * k
        for w in work:
            if name == "hash (random)": i = random.randrange(k)
            elif name == "two choices":
                a, b = random.randrange(k), random.randrange(k); i = a if load[a] <= load[b] else b
            else: i = min(range(k), key=load.__getitem__)
            load[i] += w
        res[name] = max(load) / (sum(load) / k)
    return res
for conns, heavy, label in [(400, False, "400 equal connections"), (4000, False, "4000 equal connections"), (4000, True, "4000 Pareto(1.5) connections, load = work"), (40000, True, "40000 Pareto(1.5) connections, load = work")]:
    runs = [trial(conns, 20, heavy) for _ in range(20)]
    print("%-46s" % label + "".join("  %s %.2f" % (k, sum(r[k] for r in runs) / len(runs)) for k in runs[0]))
```

400 equal connections                           hash (random) 1.46  two choices 1.07  least loaded 1.00
4000 equal connections                          hash (random) 1.13  two choices 1.01  least loaded 1.00
4000 Pareto(1.5) connections, load = work       hash (random) 2.07  two choices 1.71  least loaded 1.70
40000 Pareto(1.5) connections, load = work      hash (random) 1.35  two choices 1.10  least loaded 1.10

With **equal** connections, random choice leaves the busiest at **1.46** times the average at 400 connections and **1.13** at 4,000: the imbalance shrinks as `1/sqrt(connections per backend)`, which is the binomial spread seen in section 2. **Two choices** brings 400 connections to **1.07** and least loaded to 1.00, because comparing two random backends removes the worst collisions. With **Pareto(1.5)** work the picture changes: random choice gives **2.07**, and **two choices (1.71) and least loaded (1.70) barely improve on it** at 4,000 connections. Heavy tailed work contains connections whose single weight is a large share of a backend's total; once one of them lands on a backend, no assignment rule can move it, so the floor is set by the **largest connection**, not by the choice policy. At 40,000 connections the biggest one is a smaller share, and two choices and least loaded reach **1.10**. In practice this is why a balancer cannot fix a few huge flows or hot keys: the fix is to split the work (sharding, request level balancing at L7, or splitting large transfers).

---

## 16. Cheat sheet

**Modes:** NAT: both directions through the balancer, backends see the client, return route must pass the balancer. DSR: MAC rewrite or encapsulation, VIP on `lo`, ARP suppressed, replies direct; measured 31,474 KB against 0 KB on the client side. Proxy: two connections, per request. Encapsulation overhead: 20 B (IP in IP), 24 B (GRE).

**Hash choices (10 backends, one removed; 100,000 flows):** mod N 89.8 % moved, 79.7 % collateral. Ring 100 points 9.5 %, 0 %, busiest 1.14. Rendezvous 10.0 %, 0 %, 1.02. Maglev 65,537: 10.2 %, 0.2 %, 1.02. Maglev table needs `M` prime; 100 backends: M = 1,009 / 10,007 / 65,537 / 655,373 gave collateral 3.19 / 1.75 / 0.55 / 0.15 %.

**State:** table kept, 0 of 300 broke; table lost, 158 of 300 broke. nftables `jhash mod N` scales the hash into N ranges: 2 to 3 backends keeps 1/2 of flows (54 % measured). Idle lifetime of an LRU table = entries ÷ new flows per second.

**Health:** window = `(fall - 1) × inter` to `fall × inter` (+ check timeout if silent). Measured: 14 failures with `retries 0` (inter 1s fall 3, 20 requests per second), 0 with `retries 3` and `option redispatch 1`.

**L7:** 1,000 requests on new client connections: 1,001 backend connections (tcp, never, safe), 2 (always). `X-Forwarded-For` for HTTP, PROXY protocol for TCP.

**TLS (proxy CPU per new connection, one thread, this VM):** plain 40 µs; TLS 1.3 RSA-2048 890 µs; ECDSA P-256 500 µs; resumed 405 µs.

**Anycast:** 3 sites, 3,000 connections: 989 / 1,021 / 990. Withdraw one: plain multipath breaks 28 of 50 on a surviving site, a resilient group breaks 0. Busiest member of a group = `ceil(B/N) ÷ (B/N)`.

**XDP selector:** bounds checks, hash the 4-tuple, `table[h % M]` from a BPF array map, count, pass; picks 1,017 / 1,001 / 982 of 3,000 from a 1,009 entry table.

**Random choice:** 400 equal connections on 20 backends: busiest 1.46 (random), 1.07 (two choices), 1.00 (least loaded); Pareto work: 2.07, 1.70, 1.70.

**Debug order:** return path and `rp_filter`, MTU on the encapsulation path, VIP and ARP on backends, conntrack size and entries, health check state and window, per backend connection counts, hash seed agreement across instances, route set behind the anycast address.

---

## 17. Sources

Read or run for this tutorial:

- Linux `nftables` (`dnat`, `numgen`, `jhash`, `netdev` ingress with `fwd to`), `conntrack`, the nexthop and multipath route interfaces of `iproute2`: used and observed on the sandbox kernel (6.18).
- haproxy 2.8.30 (Ubuntu), clang and `bpftool` for the XDP program: all run here.
- The behaviour of `jhash ... mod N` (range scaling) and of the kernel's multipath hash threshold is **inferred from measured flow pairs**, not from the source code.

From memory (not re-read for this tutorial):

- Eisenbud et al., *Maglev: A Fast and Reliable Software Network Load Balancer*, NSDI 2016: the table population algorithm, `M` sizes of 65,537 and 655,373, ECMP in front, per instance connection table, GRE encapsulation.
- Karger et al., *Consistent Hashing and Random Trees*, STOC 1997 (ring); Thaler and Ravishankar, *Using name-based mappings to increase hit rates* (rendezvous hashing); Mitzenmacher, *The Power of Two Choices in Randomized Load Balancing*.
- Meta's Katran (XDP load balancer) design and the Linux IPVS documentation (modes, schedulers, connection table, sync daemon).
- The haproxy documentation for `http-reuse`, `retries`, `option redispatch`, health check parameters; the PROXY protocol specification (v1 and v2); RFC 7239 (`Forwarded`) and the de facto `X-Forwarded-For`.
- The Linux kernel documentation for resilient nexthop groups (`idle_timer`, `unbalanced_timer`) and the `fib_multipath_hash_policy` sysctl.

Cross references: NET 11 (ECMP and its hash in section 8, resilient groups, BGP and BFD convergence), NET 09 (conntrack, NAT, SYN cookies and table exhaustion), NET 08 (TLS handshake in detail), NET 07 (XDP), the load balancing design problem in `11_hld`.
