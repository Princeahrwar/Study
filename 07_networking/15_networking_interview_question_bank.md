# NET 15: Networking interview question bank: how to answer, troubleshooting scenarios, questions by area and design questions

*Part of the [networking plan](00_plan.md) · You need: [NET 01 to NET 14](00_plan.md) (NET 10 is the Wi-Fi sub-track, and is not needed here). This is the **last core networking tutorial**. The earlier ones taught the subject; this one drills it in the form an interview asks it.*

A networking interview for a senior or staff role has three halves, and the third is the one candidates skip. The first asks **"what is X and why does it work that way?"** and rewards a crisp definition followed by one level of depth. The second hands you a **broken path** and asks **"what do you do?"**, and rewards a method: a first command, a reading of its output, a narrowed hypothesis, a fix and a way to know the fix worked. The third asks you to **design** something (a load balancer, a firewall data path, a rate limiter) and rewards numbers: what it must carry, what state it holds, what breaks and how you notice.

"A service sees random 200 ms stalls on small requests: walk me through it" and "a connection hangs: where do you look?" are the two most common openings in infrastructure and network-equipment loops at Google, Nvidia, Meta, Microsoft, Cisco and Palo Alto Networks. This tutorial gives each of them a tree, then walks through the usual causes so that the symptom, the command that shows it and the reasoning behind it are things you have seen once.

By the end you can:

- answer in the shape interviewers reward: definition, one level of depth, trade-off, how to see it;
- run a method for a broken path, with a decision tree for "I cannot connect" and one for "the network is slow";
- recognise fifteen classic faults by their fingerprint (a refused or dropped connection, a path MTU black hole, reverse path filtering, the 40 ms request, a full accept queue, ephemeral port exhaustion, slow DNS, a slow reader, loss, a full conntrack table, a silent dead peer, bufferbloat, kernel drops, TLS failures, ECMP imbalance);
- answer around 70 questions across the whole track in 20 to 40 seconds each;
- structure a design answer for a load balancer, a firewall data path, a packet router, a NAT gateway, a rate limiter and an incast mitigation.

---

## 1. How to answer

### 1.1 The shape of a good answer

A senior answer has the same shape every time, and saying the shape out loud is half of the performance.

1. **One sentence that defines it**, with the mechanism and not a synonym. "A switch forwards on the destination MAC from a table it learns from source addresses" is a definition. "A switch connects devices" is not.
2. **One level of depth**: the header field, the state, or the failure that shows you have seen it. "...and it floods a frame whose destination it has not learned, which is why one broadcast in a loop circulates until the network falls over."
3. **The trade-off or the failure mode.** Every mechanism in networking costs something; naming the cost is what separates recall from understanding.
4. **A way to see it.** The command that would show it on a real machine: `ss -ti`, `tcpdump`, `nstat`, `conntrack -S`, `ip route get`.

For a design question add a fifth: **the number that sizes it** (packets per second, entries, bytes of memory, the time to detect a failure).

### 1.2 The method for a broken path

An interviewer who says "the connection fails" or "it is slow" is not asking for the cause. They are watching your method.

1. **Restate and bound the symptom.** Which two ends, which protocol, since when, one direction or both, every request or some, constant or intermittent. Two questions cut the tree in half: **does it fail at once or after a wait** (a refusal against a filter), and **does it affect all traffic or only large, only new, or only some destinations**.
2. **Walk the layers from the bottom, with the cheapest tool at each.** Link (`ip link`), address and neighbour (`ip addr`, `ip neigh`), route (`ip route get DST`), reachability (`ping`, and `ping -M do -s N` for the MTU), transport (`ss`, `nc`, `curl -v`), name (`getent hosts`, `dig`), application.
3. **Capture on both ends when you can.** A packet seen at one end and not at the other is a drop in between; a packet seen at neither is the sender's own stack. This single comparison is the most decisive move there is.
4. **Read the counters before the packets.** `nstat -az`, `ss -s`, `ip -s link`, `conntrack -S`, `ethtool -S` often name the cause (a counter that moved) without a capture.
5. **Form one hypothesis, predict what you will see, and test it.** "If this is a black hole, a 1,300 byte ping works and a 1,472 byte one does not."
6. **Fix, then prove the fix** with the same measurement that showed the problem, and say what you would monitor so it is seen earlier next time.

### 1.3 Tree one: "I cannot connect"

```text
connect() fails or hangs
|
+-- fails AT ONCE with "connection refused"
|     a reset came back: the host is up and the path works
|     -> nothing listens on that port (ss -ltn on the server), or a firewall REJECTs,
|        or it listens on another address (127.0.0.1 only, the wrong interface)
|
+-- HANGS, then "timed out" (SYN-SENT in ss, SYNs repeated)
|     no answer at all
|     |
|     +-- capture on both ends: does the SYN ARRIVE at the server?
|     |     NO  -> dropped on the path: a firewall DROP, a missing route or return route,
|     |            a wrong next hop, ARP not resolving (ip neigh shows FAILED)
|     |     YES -> does a SYN-ACK leave?
|     |           NO  -> the server drops it: rp_filter on an unroutable source,
|     |                  a full accept queue (ListenOverflows), conntrack full,
|     |                  a local firewall DROP
|     |           YES -> it is lost on the way back: asymmetric route, a stateful firewall
|     |                  that never saw the SYN, NAT without the return mapping
|     |
|     +-- "No route to host" or "Network is unreachable" at once: no route (ip route get)
|
+-- CONNECTS, then the first large write hangs, small ones work
|     path MTU black hole: ping -M do -s 1472, then fix with the ICMP error or an MSS clamp
|
+-- connects, but "name or service not known", or a long pause before the SYN
|     resolution: getent hosts, dig @server, resolv.conf order and timeouts
|
+-- "Cannot assign requested address" on the CLIENT
|     ephemeral ports or TIME_WAIT: ss -tan | grep -c TIME-WAIT, the port range, more
|     destinations, a connection pool
|
+-- "Address already in use" on the SERVER at start
      a listener or a TIME_WAIT on the port: ss -ltnp, SO_REUSEADDR
```

The first branch does most of the work. **Refusal at once** means a reset came back: the far host is alive and reachable, so the problem is a port or a rule. **A wait** means silence: the packet never arrived, or it arrived and was dropped, or the reply never came back. The next question is always "where does the packet stop", and two captures answer it.

### 1.4 Tree two: "the network is slow"

```text
slow: say which of latency, throughput, or stalls
|
+-- ONE slow request among fast ones (a stall of a fixed size)
|     40 ms  -> Nagle and delayed ACK on a two write request
|     200 ms -> a retransmission timeout (loss, tail loss, the 200 ms RTO floor)
|     1 s / 3 s -> a SYN retransmission, or a DNS retry
|     5 s    -> the default resolver timeout; 15 s, 30 s -> application retry timers
|
+-- every request is slow (latency)
|     idle ping high -> distance (a long round trip), or a bad route (traceroute)
|     ping fine idle, bad under load -> a standing queue: bufferbloat
|     slow only the FIRST request -> handshakes: DNS + TCP + TLS in round trips
|
+-- throughput is low on a fast link
|     ss -ti on the sender, then read which limit it reports:
|     |  rwnd_limited high    -> the receiver or its application is slow
|     |  cwnd_limited / loss  -> congestion window: loss or a policer
|     |  sndbuf_limited       -> the send buffer or autotuning is too small
|     |  app-limited          -> the application does not send enough
|     long path, small window: window / RTT is the limit
|
+-- intermittent drops or resets
      counters: nstat (retransmits, ListenOverflows), ip -s link (drops), ethtool -S,
      conntrack -S (drop, insert_failed), softnet_stat; then capture
```

The tree for slowness starts with a **shape**, because a stall of a fixed size is a fingerprint: **40 ms** is delayed ACK, **200 ms** is the minimum retransmission timeout, **1 s** and **3 s** are SYN timers and **5 s** is a resolver timeout. A latency that scales with load is a queue; a latency that is constant is distance or a timer.

### 1.5 The ten commands

| Command | What it answers |
|---|---|
| `ip -s link show DEV` | Errors, drops, overruns on an interface |
| `ip route get DST` | Which route, which source address, which interface, any cached MTU |
| `ip neigh` | Is the next hop resolved (REACHABLE, STALE, FAILED) |
| `ss -tan` and `ss -ltn` | Every TCP socket and its state; for a listener, the accept queue length and its limit |
| `ss -ti dst IP` | One connection's window, RTT, retransmissions and which limit applies |
| `nstat -az` | Kernel counters: `ListenOverflows`, `IPReversePathFilter`, `TCPTimeouts` |
| `tcpdump -nn -i DEV 'filter'` | The packets themselves, on both ends if possible |
| `conntrack -S` and `-C` | The state table's size and its drops |
| `ping -M do -s N` and `tracepath` | The path MTU and the hops |
| `dig @SERVER NAME +time=1` and `getent hosts` | Resolution, with the resolver the application actually uses |

---

## 2. Fifteen troubleshooting scenarios

Each scenario gives the **symptom**, the **fingerprint** that identifies it, the **command** that shows it, the **cause** and the **fix**. Several come with a snippet you can run in two network namespaces.

### S1. Refused, rejected or dropped

**Symptom.** "I cannot connect." **Fingerprint.** A closed port answers with a reset at once; a firewall rule that **rejects** with a reset looks the same; a firewall that **drops** gives silence, and the client retries its SYN at growing intervals until it times out. So the *time* the failure takes is the diagnosis: instant means a reset came back, a wait means nothing did.

```python
# s1.py HOST PORT: how does connect() fail, and how long does it take?
import socket, sys, time
host, port = sys.argv[1], int(sys.argv[2])
t = time.time()
s = socket.socket()
s.settimeout(4)
try:
    s.connect((host, port))
    print("connected")
except Exception as e:
    print(f"{type(e).__name__}: {e} after {time.time() - t:.2f} s")
```

```bash
# a server namespace with three kinds of port: closed, rejected, dropped
ip netns add srv; ip netns add cli
ip link add vc netns cli type veth peer name vs netns srv
ip netns exec cli ip addr add 10.5.0.1/24 dev vc; ip netns exec cli ip link set vc up
ip netns exec srv ip addr add 10.5.0.2/24 dev vs; ip netns exec srv ip link set vs up
ip netns exec srv nft -f - <<'RULES'
table inet f {
  chain in {
    type filter hook input priority 0; policy accept;
    tcp dport 9001 reject with tcp reset
    tcp dport 9002 drop
  }
}
RULES
for p in 9000 9001 9002; do ip netns exec cli python3 s1.py 10.5.0.2 $p; done
ip netns del srv; ip netns del cli
```

Run, the closed port (9000) and the rejected port (9001) both fail with "Connection refused" in 0.00 s, and the dropped port (9002) fails with a timeout after the full wait. **Fix.** Refused: start the listener, or bind it to the right address. Dropped: find the filter (`nft list ruleset`, cloud security groups) or the missing route. The practical lesson: **a drop is invisible to the sender, so the evidence is on the receiver** (a capture, or a counter on the firewall).

### S2. A path MTU black hole

**Symptom.** Small requests work and large transfers hang. **Fingerprint.** The TCP handshake and the first small packets pass; the first full-size segment never gets acknowledged. **Why.** A router with a smaller MTU on the far side must send ICMP "fragmentation needed" back, so the sender shrinks its packets (path MTU discovery). A firewall that drops ICMP hides the message, so the sender keeps resending full-size packets that cannot pass. **Command.** `ping -M do -s 1472 DST` (DF set, 1,500 byte packet) fails while `-s 1372` works; `tracepath DST` reports the MTU where it can. **Fix.** Let the ICMP type 3 code 4 (and ICMPv6 packet-too-big) through; or **clamp the MSS** on the router (`tcp option maxseg size set rt mtu` in nftables); or enable PLPMTUD (`net.ipv4.tcp_mtu_probing`). Tunnels (VXLAN, GRE, IPsec, WireGuard) are the usual source, because they subtract header bytes from the MTU.

### S3. Replies that never come: reverse path filtering

**Symptom.** A request reaches the server (a capture shows it) and no reply leaves; it works from one address and not another. **Fingerprint.** `nstat` shows `IPReversePathFilter` increasing. **Why.** With `rp_filter` in strict mode, the kernel drops a packet whose source address would not be routed back out the interface it arrived on, to stop spoofing. Asymmetric routing and a missing return route trigger it. **Command.** `nstat -az IPReversePathFilter`, `sysctl net.ipv4.conf.all.rp_filter`, `ip route get SRC from DST iif IFACE`. **Fix.** Add the route, or use loose mode (`rp_filter=2`) where paths are asymmetric by design.

### S4. The request that takes 40 ms

**Symptom.** Small requests, sometimes fast, sometimes exactly about 40 ms. **Cause.** The client writes a request in two small pieces (a header, then a body). **Nagle's algorithm** holds the second small write until the first is acknowledged; the receiver's **delayed ACK** waits up to 40 ms hoping to piggyback the acknowledgement on a reply. Each side waits for the other. The snippet below reproduces it on loopback:

```python
import socket, threading, time
srv = socket.socket(); srv.bind(("127.0.0.1", 0)); srv.listen()
port = srv.getsockname()[1]
def handle(c):
    while True:
        hdr = c.recv(4)                      # the server waits for a 4 byte header, then the 100 byte body
        if not hdr: return
        c.recv(100); c.sendall(b"ok")
def serve():
    while True:
        c, _ = srv.accept()
        threading.Thread(target=handle, args=(c,), daemon=True).start()
threading.Thread(target=serve, daemon=True).start()

def timed(nodelay, split):
    s = socket.create_connection(("127.0.0.1", port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, int(nodelay))
    t = []
    for _ in range(20):
        t0 = time.perf_counter()
        if split:
            s.sendall(b"HDR!"); s.sendall(b"x" * 100)        # two small writes back to back
        else:
            s.sendall(b"HDR!" + b"x" * 100)                  # one write
        s.recv(2)
        t.append(time.perf_counter() - t0)
    return sum(t) / len(t) * 1e3
print(f"two writes, Nagle on : {timed(False, True):6.2f} ms per request")
print(f"two writes, NODELAY  : {timed(True, True):6.2f} ms per request")
print(f"one write,  Nagle on : {timed(False, False):6.2f} ms per request")
```

Two writes with Nagle on take about **42 ms** per request here; with `TCP_NODELAY`, or with one write, about **0.05 ms**. **Fix.** Write a message in one call (`writev` or build the buffer), and set `TCP_NODELAY` on latency-sensitive sockets. Setting NODELAY alone fixes the stall but still sends tiny packets, so batching in the application is the better cure.

### S5. New connections stall under load: the accept queue

**Symptom.** Under load some connects hang for a second or three, then succeed or time out; the server CPU is not busy. **Fingerprint.** For a listening socket, `ss -ltn` shows **Recv-Q** (connections waiting to be accepted) and **Send-Q** (the limit, the backlog). When Recv-Q reaches the limit, the kernel drops the final ACK or the SYN, and `nstat` counts `ListenOverflows`. The client retries on its SYN timer, which is the 1 second stall.

```python
import socket, subprocess, time
srv = socket.socket(); srv.bind(("127.0.0.1", 9100)); srv.listen(4)      # backlog 4, and accept() is never called
clients = []
for i in range(12):
    c = socket.socket(); c.setblocking(False)
    try: c.connect(("127.0.0.1", 9100))
    except BlockingIOError: pass
    clients.append(c)
time.sleep(1)
print(subprocess.run(["ss", "-ltn", "sport", "=", ":9100"], capture_output=True, text=True).stdout.strip())
print(subprocess.run("nstat -az TcpExtListenOverflows TcpExtListenDrops", shell=True, capture_output=True, text=True).stdout.strip())
```

The listener shows **Recv-Q 5, Send-Q 4** (the queue holds backlog plus one) and the overflow counters rise as the other clients are refused entry. **Cause.** The application is not calling `accept()` fast enough (a blocked thread, a slow startup, too few workers), or the backlog is small. **Fix.** Fix the accept loop (dedicated acceptor, `SO_REUSEPORT` with several listeners, epoll); raise the backlog (`listen(fd, 1024)` and `net.core.somaxconn`); watch `ListenOverflows` as an alert.

### S6. `Cannot assign requested address`

**Symptom.** A client that opens many short connections to one server starts failing with `EADDRNOTAVAIL`. **Cause.** Each connection needs a unique four-tuple. To one server address and port, the only free field is the **client port**, and a closed connection holds its port in **TIME_WAIT** for 60 seconds. So the rate is bounded by ports divided by TIME_WAIT:

```python
low, high, time_wait_s = 32768, 60999, 60                  # default ephemeral range, TIME_WAIT of 60 s
ports = high - low + 1
print(f"{ports} ports, TIME_WAIT {time_wait_s} s: at most {ports / time_wait_s:.0f} new connections/s to ONE server address and port")
print(f"to 4 server addresses: {4 * ports / time_wait_s:.0f} per second")
```

About **470 new connections per second** to one destination, and the failure arrives the moment the rate exceeds that for a minute. **Fix.** **Reuse connections** (keep-alive, pooling, HTTP/2), which is the real answer; spread over more server addresses or client source addresses; widen the range (`ip_local_port_range`); `tcp_tw_reuse` (client side, only reuses sockets older than a second). Do not "fix" by shortening TIME_WAIT: it protects against old duplicates.

### S7. Everything is slow to start: DNS

**Symptom.** A command or service takes several seconds before it does anything; once running it is fast. **Cause.** The first nameserver in `/etc/resolv.conf` does not answer, so every lookup waits for the full timeout (5 seconds by default) before trying the second, and a resolver may try both address families. **Command.** `time getent hosts NAME` (the resolver the application actually uses), `dig @SERVER NAME +time=1`, `strace -r -e trace=connect,sendto`. In an earlier run a dead first nameserver made a lookup take 6.2 seconds, 2 seconds with `timeout:1 attempts:1`, and 0.01 s with the working server first. **Fix.** Remove or reorder dead servers, set `options timeout:1 attempts:2`, run a local caching resolver, and fix `search` domains that multiply each query (a short name can trigger several lookups).

### S8. Fast link, slow transfer: the receiver

**Symptom.** A transfer runs at a fraction of the link rate and the link is idle. **Fingerprint.** `ss -ti` on the sender reports **`rwnd_limited`** for most of the time and a tiny send window: the receiving application reads slowly, its receive buffer fills, the advertised window shrinks toward zero, and the sender is allowed only that much in flight. An earlier test with a reader taking 1,000 bytes every 10 ms held the sender to 0.76 Mbit/s with `rwnd_limited` at 87 percent. **Fix.** Fix the reader (it is the bottleneck, so profile it), not the network. The same tool tells the other limits apart: `cwnd_limited` (loss or congestion), `sndbuf_limited` (buffer too small), `app_limited` (the sender has nothing to send).

### S9. What loss and delay do to throughput

**Symptom.** A transfer over a long path is far below the link rate. Two limits apply, and the arithmetic says which:

```python
import math

def window_limit_mbps(window_bytes, rtt_s):        # a sender can have one window in flight per round trip
    return window_bytes * 8 / rtt_s / 1e6

def mathis_mbps(mss, rtt_s, loss):                 # loss-limited Reno-style throughput, Mathis et al.
    return 1.22 * mss * 8 / (rtt_s * math.sqrt(loss)) / 1e6

print(f"64 KB window at 100 ms: {window_limit_mbps(65536, 0.100):.2f} Mbit/s")
print(f"BDP of 100 Mbit/s at 100 ms: {100e6 * 0.100 / 8 / 1e6:.2f} MB")
for loss in (1e-4, 1e-3, 1e-2):
    print(f"1448 B MSS, 100 ms, loss {loss:g}: {mathis_mbps(1448, 0.100, loss):7.2f} Mbit/s")
```

A 64 KB window over a 100 ms path caps at **5.24 Mbit/s** whatever the link; filling a 100 Mbit/s path needs a **1.25 MB** window (the bandwidth delay product). And loss costs far more than its percentage suggests: at 100 ms, 1 percent loss caps a Reno-style flow at about **1.4 Mbit/s**, and 0.01 percent at 14. **Fix.** For a window limit, enlarge buffers or let autotuning work (`tcp_rmem`, `tcp_wmem`); for loss, find where it happens (a queue, a bad link, a policer) or use a congestion control less sensitive to loss (BBR) or a split connection near the client.

### S10. New connections fail when conntrack is full

**Symptom.** Established connections work, new ones fail or time out, on a firewall or NAT box or a host with many connections. **Fingerprint.** The kernel log says `nf_conntrack: table full, dropping packet`; `conntrack -C` is at `nf_conntrack_max`; `conntrack -S` shows `drop` or `insert_failed`. **Cause.** Every new flow needs an entry, and the table is sized for far fewer than the traffic (or a flood, or long timeouts on idle entries, is filling it). **Fix.** Size the table as **new flows per second times entry lifetime** (an entry is a few hundred bytes), shorten timeouts for half-open and for idle established flows, drop junk early (a `notrack` or an early filter) and, for floods, SYN cookies or a front-end that holds no state.

### S11. A dead peer that is not noticed for 15 minutes

**Symptom.** A client hangs for many minutes after the server's host dies or a cable is cut. **Why.** TCP only learns that a peer is gone from the absence of acknowledgements: it retransmits with a **doubling timeout**, and gives up after `tcp_retries2` tries, about **15 minutes** at the default of 15. An **idle** connection has nothing in flight, so it never notices at all unless **keepalive** probes (which default to 2 hours idle). Compute the give-up time:

```python
def give_up_seconds(retries, rto_min=0.2, rto_max=120):   # RTO doubles each time, capped at 120 s
    t, rto = 0.0, rto_min
    for _ in range(retries + 1):
        t += rto
        rto = min(rto * 2, rto_max)
    return t
print(f"dead peer, tcp_retries2 = 15: {give_up_seconds(15) / 60:.1f} minutes; = 5: {give_up_seconds(5):.1f} s")
```

That is **15.4 minutes** at 15 retries and 12.6 seconds at 5. **Fix.** Do not rely on TCP: use application **heartbeats** with their own timeout, `TCP_USER_TIMEOUT` for data in flight, and keepalive with sensible values for idle connections.

### S12. Latency under load: bufferbloat

**Symptom.** Calls and SSH are fine until someone uploads, then ping rises to hundreds of milliseconds. **Fingerprint.** Idle latency is low, loaded latency rises by the size of a buffer divided by the link rate, and `tc -s qdisc` shows a large backlog. **Fix.** AQM (fq_codel, CAKE), a shaper just under the real link rate, a right-sized buffer. Covered, with arithmetic, in [NET 14](14_queueing_qos_and_traffic_management.md).

### S13. Where are the packets dropped?

**Symptom.** "The packets are lost somewhere on the host." **Method.** Each stage of the receive path has its own counter: the NIC (`ethtool -S`, ring overruns), the driver and softirq backlog (`/proc/net/softnet_stat`, `ip -s link`), netfilter (`nft` counters, `conntrack -S`), the protocol (`nstat`: `IPReversePathFilter`, `TcpExtListenOverflows`, checksum errors), the socket buffer (`ss -m`, `UdpRcvbufErrors`). When counters do not name it, `perf trace -e skb:kfree_skb` or `dropwatch` give the **drop reason and call site**. See [NET 06](06_linux_network_stack.md).

### S14. A TLS handshake fails

**Symptom.** `curl` reports a certificate or handshake error. **Method.** `openssl s_client -connect HOST:443 -servername HOST` shows the chain the server actually sends. The usual causes: a **missing intermediate certificate** (the server must send the chain), an expired certificate or clock skew, a hostname that does not match (SAN) or a missing **SNI** (the server picked its default certificate), a protocol or cipher mismatch, a client that does not trust the issuing CA, or a middlebox re-signing. See [NET 08](08_application_protocols_dns_http_tls.md).

### S15. One uplink is full while the others are idle

**Symptom.** One of several equal-cost links runs hot. **Causes.** A few huge flows (a hash cannot split a single flow), **hash polarisation** (every tier uses the same hash function and seed, so the second tier sees an already-sorted set of flows), or an ECMP hash that includes too few fields. **Command.** Per-link counters, and hash the flow set offline to see where each lands. **Fix.** Different hash seeds per tier, more fields (the 5-tuple), more flows (parallel connections or queue pairs), resilient hashing, or flowlet and per-packet schemes. See [NET 11](11_routing_protocols_and_datacenter_fabrics.md).

---

## 3. The question bank

Answers are written to be **said aloud in 20 to 40 seconds**, then extended by the follow-up in italics. The tutorial that teaches each area is named in the heading.

### 3.1 Link layer and Ethernet ([NET 01](01_network_models_and_layer2.md))

1. **What does a switch do with a frame?** It learns the source MAC against the ingress port, looks up the destination MAC and forwards out one port; an unknown unicast, a broadcast or a multicast is flooded to every port in the VLAN except the ingress. *Follow-up: with a loop the flooded frame circulates forever, which is why spanning tree (or no L2 loops, with routed fabrics) exists.*
2. **How big is a minimum Ethernet frame on the wire, and why does it matter?** 64 bytes of frame plus 8 of preamble and 12 of inter-frame gap: **84 bytes**, which is **14.88 million packets per second** at 10 Gbit/s. It is the worst case a forwarding engine must handle, so line-rate claims quote 64 byte packets.
3. **What is the MTU and what happens above it?** The largest payload a link carries: 1,500 for standard Ethernet. A router either fragments (IPv4, DF clear) or drops and sends ICMP (DF set); if the ICMP is lost you have S2.
4. **What does ARP do and how does it fail?** It maps a next-hop IPv4 address to a MAC by broadcasting a request. It fails by a missing reply (neighbour state `FAILED`), a stale entry after a MAC move (fixed by a gratuitous ARP), or poisoning by a host that answers for another's address.
5. **What is a VLAN and what is the tag?** A broadcast domain separated in software; 802.1Q inserts 4 bytes after the source MAC: a 12 bit VLAN ID and a 3 bit priority (PCP).
6. **What is spanning tree for?** It blocks redundant links so a switched network has no loop. Convergence takes seconds and the blocked links carry nothing, which is why datacenters moved to routed leaf-spine with ECMP.
7. **What is a MAC table overflow attack?** An attacker fills the table with fake sources, the switch starts flooding unicast, and the attacker sees traffic. Port security (a limit of MACs per port) is the control.

### 3.2 IP and routing ([NET 02](02_ip_routing_and_layer3.md), [NET 11](11_routing_protocols_and_datacenter_fabrics.md))

8. **How does a router pick a route?** The **longest prefix match** on the destination, then lowest administrative distance and metric among equals. For `10.1.2.200` with `/0`, `/8`, `/16`, `/24` and `/25` routes, the `/25` that covers it wins.
9. **How is longest prefix match done fast?** In software a compressed trie (an LC-trie in Linux); in hardware TCAM, or DIR-24-8 tables where a lookup is two memory reads. *The trade is memory and update cost against lookup time.*
10. **Subnet drill: how many hosts in 192.168.10.0/22?** The mask is 255.255.252.0, so the network is **192.168.8.0** (not .10.0), the broadcast is 192.168.11.255, and there are **1,022 hosts**. A `/31` has two usable addresses on a point-to-point link (RFC 3021).
11. **Can you aggregate 10.0.0.0/24 and 10.0.2.0/24?** Only into a **/22**, which also covers 10.0.1.0/24 and 10.0.3.0/24, so it is not exact. Aggregating non-adjacent blocks advertises addresses you do not own.
12. **What does TTL do?** Each router decrements it; at 0 the packet is dropped and an ICMP time-exceeded returns. It stops loops and is how `traceroute` works.
13. **Does a router recompute the IPv4 header checksum?** It updates it incrementally after a TTL change (RFC 1624) rather than summing the header again.
14. **Fragmentation: who does it and what does it cost?** The sender, and IPv4 routers if DF is clear; the receiver reassembles, and one lost fragment loses the whole datagram. Modern stacks avoid it with path MTU discovery. IPv6 routers never fragment.
15. **What is reverse path filtering?** A source address check: strict mode drops a packet whose source would not be routed out the arriving interface. It stops spoofing and breaks asymmetric routing (S3).
16. **OSPF against BGP, in one breath?** OSPF is an IGP: link-state flooding, every router computes a shortest-path tree, fast convergence inside one domain. BGP is path-vector between organisations, policy-driven, scales to the Internet table, converges slowly. In a datacenter, eBGP with a private AS per rack is common.
17. **What is ECMP and what is its main failure?** Equal-cost multipath splits **flows** over next hops by a hash. Failures: **polarisation** when tiers share a hash, and elephant flows landing on one link (S15). **Resilient hashing** keeps existing flows in place when a member leaves.
18. **What is a valley-free path?** A path that goes up the tiers and then down and never down and up again; a route learnt from a lower tier and re-advertised upward creates a valley and a loop risk.
19. **What is anycast?** The same address announced from many places; routing delivers each packet to the nearest. Used for DNS and CDNs; a route change mid-connection can send later packets to another site, so TCP over anycast needs care ([NET 13](13_load_balancing_proxies_and_anycast.md)).
20. **How does NAT work and what does it cost?** A gateway rewrites the source address and port of outgoing packets and keeps a table to reverse it. It costs state (a conntrack entry of a few hundred bytes), breaks end-to-end addressing, and limits connections per external address to the port range.
21. **IPv6 in three differences?** 128 bit addresses; no fragmentation by routers and no header checksum; neighbour discovery (ICMPv6) replaces ARP, so filtering ICMPv6 blindly breaks the network.
22. **What is multicast and how is membership kept?** One sender, many receivers by a group address; hosts join with IGMP (IPv4) or MLD (IPv6) and routers build a tree (PIM). Common in market data and discovery protocols.

### 3.3 TCP and UDP ([NET 03](03_tcp_udp_and_transport.md))

23. **Describe the handshake and what each side holds.** SYN, SYN-ACK, ACK. After the SYN the server holds a half-open entry in the SYN queue; after the ACK the connection moves to the **accept queue** until `accept()` (S5). SYN cookies let the server hold nothing during a flood.
24. **Why does a SYN to a closed port differ from one to a filtered port?** The closed port answers RST at once; a filtered one gives silence and the client retries (S1).
25. **What is the TCP window and what limits throughput?** The sender may have `min(cwnd, receiver window)` bytes unacknowledged. Throughput is at most window / RTT (64 KB at 100 ms gives about 5 Mbit/s). `ss -ti` says which limit applies: `rwnd_limited`, `cwnd_limited`, `sndbuf_limited`.
26. **What is the bandwidth delay product?** Rate times RTT, the bytes in flight needed to fill the pipe: 1.25 MB for 100 Mbit/s at 100 ms. Buffers must be at least that for one flow to reach line rate.
27. **Explain slow start and congestion avoidance.** The window starts at about 10 segments and doubles each RTT until loss or a threshold, then grows about one segment per RTT; loss halves it (Reno) or cuts it by about 30 percent (CUBIC). Short transfers never leave slow start, so their time is round trips, not bandwidth.
28. **Why does a small loss rate hurt TCP so much?** Every loss is read as congestion. The Mathis estimate is rate = 1.22 x MSS / (RTT x sqrt(loss)), so at 100 ms and 1 percent loss a flow gets about 1.4 Mbit/s (S9).
29. **Reno, CUBIC and BBR in a sentence each?** Reno: additive increase, halve on loss. CUBIC: window grows as a cubic function of time since the last loss, the Linux default for years. BBR: models bottleneck bandwidth and minimum RTT, does not treat loss as the signal, and keeps queues short.
30. **What is the 40 ms stall?** Nagle holding a small write until the previous one is acknowledged, against delayed ACK (S4).
31. **What are the retransmission timers?** The RTO comes from the smoothed RTT and its variance with a floor of 200 ms on Linux, and doubles on each failure. The give-up time is set by `tcp_retries2` (about 15 minutes at the default of 15).
32. **Fast retransmit and SACK?** Three duplicate ACKs trigger a retransmission without waiting for the timer; SACK reports which ranges arrived so only the holes are resent. **Tail loss** (no later packets to produce duplicates) falls back to the timer, which is why it is expensive.
33. **What is TIME_WAIT for and what does it cost?** The side that closes first holds the connection for 2 x MSL (60 seconds on Linux) to absorb late duplicates and to retransmit the final ACK. It ties up a four-tuple (S6).
34. **What is a zero window and a probe?** The receiver advertises 0; the sender sends a small probe at growing intervals until the window reopens. A throughput limited by a tiny window is the slow reader (S8).
35. **How does a dead peer get noticed?** Retransmissions when data is in flight, keepalive when idle, both slow by default (S11). Use an application heartbeat or `TCP_USER_TIMEOUT`.
36. **UDP against TCP: when UDP?** When the application wants control: low latency with its own recovery (QUIC, DNS, RTP, games), multicast, or very many short exchanges. UDP gives no ordering, retransmission or congestion control.
37. **What is QUIC and why does it exist?** A reliable, encrypted, multiplexed transport over UDP: no head-of-line blocking between streams, a faster handshake (one round trip, zero on resumption) and connection IDs that survive an address change. Congestion control lives in user space.
38. **What is head-of-line blocking?** One lost segment holds back every byte behind it in a TCP stream, even bytes of unrelated requests multiplexed on it. HTTP/2 over TCP has it; HTTP/3 over QUIC does not.
39. **Explain a SYN flood and the defence.** The attacker sends SYNs and never completes; the half-open queue fills. **SYN cookies** encode the state in the sequence number so nothing is held until a valid ACK returns.
40. **TCP Fast Open and segmentation offload?** TFO carries data in the SYN on a repeat connection (saves a round trip). TSO lets the stack hand the NIC a large buffer to cut into segments; GSO does it in software.

### 3.4 Sockets and I/O ([NET 04](04_socket_programming_in_c.md), [NET 05](05_epoll_and_io_uring.md))

41. **Walk through a TCP server's system calls.** `socket`, `bind`, `listen`, then a loop of `accept`, `read`/`write`, `close`. The backlog of `listen` bounds the accept queue; if `accept` is slow the queue overflows (S5).
42. **What does `read` returning less than requested mean?** Nothing special: TCP is a byte stream with no message boundaries. A correct program loops until it has what it needs and frames messages itself (a length prefix).
43. **Why do you need `SO_REUSEADDR`?** So a restarted server can bind a port that has sockets in TIME_WAIT. It does not allow two listeners; `SO_REUSEPORT` does, with the kernel spreading connections across them.
44. **What is SIGPIPE?** Writing to a connection the peer closed raises it and kills the process unless it is ignored or `MSG_NOSIGNAL` is used.
45. **Blocking, non-blocking, `select`, `poll`, `epoll`: when each?** Blocking with a thread per connection is simple and limited by threads. `select` and `poll` scan every descriptor on each call; `epoll` returns only ready ones, so it wins with many mostly idle connections.
46. **Level against edge-triggered epoll?** Level reports while the condition holds; edge reports once on the change, so you must read until `EAGAIN`. With several threads, `EPOLLONESHOT` stops two threads handling one socket.
47. **What is `io_uring` and why?** Shared submission and completion rings so many operations are submitted and reaped with few or no system calls; it covers files as well as sockets.
48. **How do you find which process owns a socket and its queues?** `ss -tanp` for state and owner; `ss -ltn` for accept queue (Recv-Q is the current length, Send-Q the limit); `ss -ti` for window, RTT, retransmissions and limits.
49. **When do you set `SO_SNDBUF` by hand?** Linux grows buffers automatically within `tcp_rmem` and `tcp_wmem`. Setting them by hand turns autotuning off for that socket, so do it only when the BDP is known and the defaults cap throughput on a long path.

### 3.5 The Linux network stack and zero copy ([NET 06](06_linux_network_stack.md), [NET 07](07_zero_copy_networking.md))

50. **Trace a received packet through Linux.** The NIC DMAs into a ring buffer, an interrupt starts NAPI polling in a softirq, an `sk_buff` is built, GRO may merge segments, netfilter and routing run, the packet lands in a socket queue, then `read`. Each stage has a drop counter (S13).
51. **What is NAPI?** Interrupt then polling: after the first interrupt the driver disables it and polls in batches until the ring is empty, which avoids an interrupt per packet at high rates.
52. **What do RSS, RPS and XPS do?** RSS spreads flows to hardware queues (one per core) by a hash; RPS does it in software; XPS picks the transmit queue per core. They keep a flow on one core and spread load.
53. **What are GRO and GSO/TSO?** GRO merges received packets of one flow into one large buffer to cut per-packet cost; GSO and TSO do the reverse on transmit.
54. **How does XDP differ from the normal path?** An eBPF program runs in the driver before a buffer is allocated and can drop, pass, redirect or transmit; AF_XDP hands frames to user space without the stack. It is for filtering, DDoS drops and load balancing at the highest rates.
55. **What is zero copy and what does `sendfile` save?** Moving data between file and socket without a pass through user space: `sendfile` avoids two copies and a system call per chunk. `MSG_ZEROCOPY` and `splice` are relatives, with their own completion rules.
56. **How would you find where packets are dropped in the kernel?** Counters first (`nstat`, `ip -s link`, `ethtool -S`, `softnet_stat`), then `perf trace -e skb:kfree_skb` or `dropwatch` for the drop reason and call site.
57. **What is a qdisc?** The queueing discipline between the stack and the driver: default `fq_codel` or `fq` on modern systems; others shape, schedule or meter ([NET 14](14_queueing_qos_and_traffic_management.md)).
58. **DPDK against the kernel stack?** DPDK polls NIC queues in user space on dedicated cores with huge pages, skipping interrupts and the kernel; the best per-core rate at the cost of cores, a user-space stack and the loss of the kernel's tools.

### 3.6 Application protocols and security ([NET 08](08_application_protocols_dns_http_tls.md), [NET 09](09_network_security_firewalls_ips_dpi.md))

59. **What happens when you type a URL?** Resolve the name (stub, resolver, recursion), open TCP (or QUIC), negotiate TLS, send the request, receive and render. The first request pays DNS, TCP and TLS round trips; later ones reuse the connection.
60. **DNS: why UDP, and when TCP?** A query and reply fit in one datagram each; TCP is used when the answer is truncated (large DNSSEC answers) and for zone transfers. A dead first nameserver costs seconds (S7).
61. **HTTP/1.1, HTTP/2 and HTTP/3?** 1.1: one request at a time per connection. 2: multiplexed binary streams over TCP, with TCP head-of-line blocking. 3: the same over QUIC.
62. **TLS 1.3 handshake in one round trip?** The client sends key shares in the ClientHello; the server replies with its share, certificate and finished; application data can follow after one round trip (zero with resumption, at the cost of replay protection). The certificate chain and SNI cause most failures (S14).
63. **Stateless against stateful firewall?** Stateless matches each packet on its headers; stateful tracks connections so a reply is allowed because the request was seen. Stateful needs a conntrack entry and fails when the table fills (S10).
64. **What happens when conntrack is full?** New flows are dropped, the log says `table full, dropping packet`, and a local sender can see `EPERM`. Size it as new flows per second times entry lifetime.
65. **Why can an IPS be evaded by segmentation?** A signature split across two TCP segments or two fragments is invisible to per-packet matching; the device must reassemble the stream the way the end host does, which costs memory and CPU.
66. **What does a CDN or load balancer change about TLS?** It terminates TLS at the edge and may re-encrypt to the origin; the client sees the edge's certificate. The client IP is lost unless the PROXY protocol or `X-Forwarded-For` carries it.

### 3.7 Datacenter, RDMA, load balancing and queueing ([NET 11](11_routing_protocols_and_datacenter_fabrics.md) to [NET 14](14_queueing_qos_and_traffic_management.md))

67. **Why leaf-spine, and how does it scale?** Every leaf connects to every spine, so any two servers are a few hops apart with many equal-cost paths; capacity grows by adding spines and the oversubscription ratio sets the bisection bandwidth.
68. **What is incast?** Many senders answering one receiver at once overflow the receiver's port buffer: a fan-in problem, handled by ECN and rate control, pacing, or smaller fan-out ([NET 12](12_rdma_roce_and_lossless_fabrics.md)).
69. **PFC and what can go wrong?** Priority flow control pauses one traffic class per link to make it lossless. Pausing spreads back through the fabric: head-of-line blocking of victim flows, pause storms, and deadlock on a cyclic dependency.
70. **What does RoCE need compared with TCP?** Reliable delivery in hardware with little loss tolerance, so a lossless or near-lossless fabric (PFC and ECN) and congestion control in the NIC (DCQCN).
71. **Why not `hash mod N` for load balancing?** Changing N remaps nearly everything. Consistent hashing, rendezvous or Maglev move about 1/N of flows ([NET 13](13_load_balancing_proxies_and_anycast.md)).
72. **How should a load balancer health-check?** Active probes at an interval with failure and success thresholds; detection time is about interval times failures, and a too-short window flaps. Passive checks (observed errors) catch what a probe misses.
73. **L4 against L7 load balancing?** L4 forwards by the 5-tuple and never reads the payload, so it is fast and often stateless (DSR, ECMP). L7 terminates the connection, reads the request and can route on a path or header, at the cost of CPU and TLS work.
74. **Little's law and why it matters for queues?** Number queued equals rate times wait, so a buffer is a delay: 125,000 bytes in front of a 10 Mbit/s link is 100 ms when full.
75. **Why does delay explode near full load?** In a simple queue the wait grows as load / (1 - load), so a 95 percent loaded link waits about nineteen times longer than a 50 percent one at the same packet size.
76. **Policing against shaping?** A shaper queues the excess and delays it (smooth, adds latency); a policer drops it (no added delay, but loss that TCP reacts to).
77. **What is bufferbloat and the fix?** An oversized buffer holding a standing queue so latency under load is huge. Fix with AQM (CoDel), fair queueing, a shaper just under the link rate, or BBR.
78. **What does a token bucket guarantee?** Never more than burst plus rate times time in any interval.

### 3.8 One-line debugging answers (say these first)

79. **Connect times out.** Capture both ends for the SYN; a refusal means a reset came back (S1).
80. **Large transfers hang, small ones work.** Black-hole MTU: `ping -M do -s 1472` (S2).
81. **Replies missing, capture shows requests.** `rp_filter` or a missing return route (S3).
82. **Fixed 40 ms or 200 ms.** Nagle and delayed ACK; retransmission timeout (S4).
83. **Some connects hang under load.** `ss -ltn` Recv-Q against Send-Q; `ListenOverflows` (S5).
84. **EADDRNOTAVAIL.** TIME_WAIT and the port range (S6).
85. **A program is slow to start.** DNS timeouts; `time getent hosts` (S7).
86. **Slow with an idle link.** `ss -ti` limits; the reader (S8).

### 3.9 IPv6 in depth ([NET 18](18_ipv6_in_depth.md))

79. **Walk through how an IPv6 host gets an address.** It builds a link-local address and checks it with duplicate address detection, sends a router solicitation, gets a router advertisement with the prefix, flags and router lifetime, builds a global address if the A flag is set (then checks it again), and asks DHCPv6 if the M or O flag says so. The default route comes only from the advertisement.
80. **What do the M, O, A and L flags mean?** M: addresses from stateful DHCPv6. O: other options (DNS) from DHCPv6. Per prefix, A: SLAAC may form an address; L: the prefix is on-link. They are advice, and hosts differ in how they react.
81. **Why are IPv6 subnets /64?** SLAAC needs a 64-bit interface identifier, and a fixed boundary keeps design uniform with room for any number of interfaces (RFC 7421). Plan on nibble boundaries and for summarisation, not address saving.
82. **Large transfers hang over IPv6 but small requests work. Why?** Routers never fragment, so a filtered ICMPv6 Packet Too Big leaves the sender at a size the path cannot carry. Allow ICMPv6, clamp MSS (1,440 on a 1,500 link), or advertise a smaller MTU.
83. **What is a rogue router advertisement and how do you stop it?** A device on the link announces itself as a router, so hosts send traffic through it. Use RA guard on access ports (and drop fragmented ND, which evades it), SAVI, and alert on RAs from unexpected sources. Hop limit 255 does not help against a sender on the same link.
84. **What does Happy Eyeballs do and what is its operational catch?** It races IPv6 and IPv4 (50 ms resolution delay, 250 ms between attempts) so a broken family costs the user little, which also hides broken IPv6. Test each family separately with a large transfer.
85. **NAT64, DNS64 and 464XLAT in one answer?** NAT64 translates IPv6 clients to IPv4 servers, DNS64 synthesises AAAA records by embedding the IPv4 address in `64:ff9b::/96`, and 464XLAT adds a client-side translator so IPv4-literal apps work. Mobile networks use it; IPv6-mostly networks use DHCPv4 option 108 to let capable hosts opt out of IPv4.
86. **Why is IPv6 harder on Wi-Fi?** Neighbour discovery and router advertisements are multicast, which on Wi-Fi goes at a low rate after the DTIM with no retry, and each address needs its own group. Mitigations are multicast-to-unicast, ND proxy, RA throttling, MLD snooping and RA guard.

### 3.10 Inside a switch and router ([NET 19](19_inside_a_switch_and_router.md))

87. **What is a punt, and why is it policed?** The data plane hands a packet to the CPU: traffic to the router, TTL expiry, unresolved ARP or ND, learning, ACL logging. The CPU is small, so each class gets its own rate limit and queue (CoPP), so a flood of one class cannot starve routing protocols.
88. **Why does a FIB entry point to a next-hop group and not a MAC?** Many prefixes share one next-hop entry, so one update repairs all of them when a neighbour fails (prefix-independent convergence). A one-level FIB would rewrite every prefix.
89. **TCAM against SRAM?** TCAM matches all entries in parallel with don't-care bits, in constant time, but costs power, has about half the density and needs ordered entries, so inserts shift entries. SRAM with hashing or a trie is cheaper and updates incrementally. Real chips mix them, and the memory is divided among MAC, routes, ACLs and next-hop groups by a template.
90. **What is head-of-line blocking?** A packet waiting for a busy output blocks packets behind it that could go elsewhere, capping an input-queued switch at about 60 percent. Virtual output queues with a scheduler remove it.
91. **Why can a switch with a big buffer still drop an incast?** Headroom and reservations come out first, the rest is split among congested queues by thresholds (q = alpha x B / (1 + n x alpha)), and the burst lands on one egress queue.
92. **Traceroute shows loss at one hop only. Real?** No. That hop rate-limits the ICMP it generates (a punt). Real loss continues through later hops. Test end to end.
93. **Route is in the RIB but traffic fails. Check what?** Whether it was programmed into hardware (FIB, kernel, ASIC database, chip), table utilisation, an unresolved next hop, and the queue and ACL counters on egress.

---

## 4. Design style questions

A design question is answered in the same order every time: **requirements and numbers**, **the data path**, **state and what it costs**, **failure and how it is detected**, **what you would measure**. Say the numbers early; they decide the design.

### 4.1 Design a load balancer

**Numbers first.** Peak connections per second, concurrent connections, request rate and size, whether the payload must be read (L4 or L7), the availability target. 10 Gbit/s of 64 byte packets is 14.88 million packets per second, which is more than a kernel core forwards, so the front tier uses XDP or bypass ([NET 07](07_zero_copy_networking.md)).

**Data path.** A two-tier design is the usual answer. **Tier 1, L4**: routers announce a virtual IP (anycast or ECMP) and spread flows by hash across L4 balancers; the balancers pick a backend by **consistent hashing or Maglev**, so adding or removing a backend moves about 1/N of flows and not nearly all. Replies go direct (DSR) when responses are large. **Tier 2, L7**: proxies terminate TLS, route by request, pool backend connections and retry.

**State.** An L4 balancer can be nearly stateless if its hash is stable; a connection table pins flows that must survive a backend change, sized as new flows per second times lifetime times entry size. An L7 balancer holds two sockets per request.

**Failure.** Health checks with an interval, failure and success thresholds; when an L4 balancer dies, ECMP rehashes flows to others and **resilient hashing** keeps existing flows in place. Drain backends before removal; budget retries.

**Measure.** Connections, per-backend load and spread, health-check flaps, `ListenOverflows` on backends, conntrack use, p99 latency. See [NET 13](13_load_balancing_proxies_and_anycast.md).

### 4.2 Design a firewall data path

**Numbers first.** Packets per second at the smallest packet size, new connections per second, concurrent connections, rule count, whether content inspection is needed.

**Data path.** Fast path and slow path. The **fast path** matches an established flow in a hash table keyed by the 5-tuple and forwards with a rewrite (no rule walk). The **slow path** handles the first packet of a flow: classify against the rule set (a compiled structure, not a linear list when there are thousands of rules), create the flow entry, then everything after takes the fast path. Content inspection (IPS) adds stream reassembly and multi-pattern matching ([NET 09](09_network_security_firewalls_ips_dpi.md)).

**State.** One entry per flow (a few hundred bytes), timers by protocol state (short for half-open, long for established), a bound on the table with an eviction policy and SYN-flood protection (cookies, a separate half-open limit).

**Failure.** Table full, rule updates that must not drop packets (build the new ruleset and swap atomically), HA pairs that **sync flow state** so a failover keeps connections.

**Measure.** New flows per second, table occupancy, drops by reason, rule-hit counters, latency through the box.

### 4.3 Design a packet router (lookup and forwarding)

**Numbers first.** Port speeds, packets per second per port, routing table size (hundreds of thousands to a million prefixes), update rate.

**Data path.** Receive, parse, **longest prefix match**, find the next hop and its MAC (neighbour table), decrement TTL and update the checksum, queue on the egress port, transmit. The lookup is the hard part: a trie in software, **TCAM** or algorithmic DIR-24-8 in hardware, with a **next-hop group** indirection so a link failure updates one group and not every route. Queues use a scheduler (DRR, priority) and AQM ([NET 14](14_queueing_qos_and_traffic_management.md)).

**Control plane.** BGP and OSPF compute the routes, the RIB selects the winners, and the FIB is programmed to hardware; separation of control and data plane lets forwarding keep running while routing converges ([NET 11](11_routing_protocols_and_datacenter_fabrics.md)).

**Failure.** Detect with link-down and BFD, update next-hop groups, and size convergence as detect + propagate + compute + program.

### 4.4 Design a NAT gateway

**Numbers first.** Inside hosts, new connections per second per host, concurrent connections, external addresses available.

**Data path.** Source NAT: rewrite the source address and port on outbound packets, keep a table mapping the translated tuple back to the inside host, rewrite replies in the opposite direction. Each external address offers about 28,000 to 64,000 ports **per destination**, so capacity depends on how many distinct destinations the clients reach.

**State.** One entry per flow, timers per protocol (short for UDP and DNS, long for established TCP), port allocation that avoids collisions and, for logging requirements, a record of which inside host used which port.

**Failure.** Port exhaustion (add addresses, shorten timeouts, share ports across destinations), table full, ALG-dependent protocols (FTP, SIP) that carry addresses in the payload, and asymmetric routing that sends replies around the NAT.

### 4.5 Design a rate limiter

**Numbers first.** Requests per second per key, number of keys, burst tolerance, accuracy needed.

**Algorithms.** A **token bucket** allows bursts up to its size and a steady rate (the usual choice); a **leaky bucket** smooths output; a **fixed window** counter is cheap but lets double the rate through at a window edge; a **sliding window** (log or weighted counters) is accurate at more cost.

**Distributed.** A local counter per node plus periodic sync (cheap, slightly loose), or a central store such as Redis with atomic increments (accurate, adds a hop and a failure point). Decide what happens when the store is down (fail open or closed).

**Response.** `429` with `Retry-After`, and consistent keys (user, token, IP) because limiting by IP behind a NAT punishes everyone. See [NET 14](14_queueing_qos_and_traffic_management.md) for the bucket arithmetic.

### 4.6 Mitigate incast

**Problem.** Many senders answer one receiver at once and overflow its port buffer, so TCP times out (the 200 ms floor) or RDMA stalls. **Mitigations, in order of cheapness:** reduce the fan-out or stagger the responses; lower `rto_min` so a loss costs milliseconds; use ECN and a rate controller (DCTCP, DCQCN) so senders slow before the buffer fills; size and share switch buffers for bursts; and for RDMA use PFC only as a backstop ([NET 12](12_rdma_roce_and_lossless_fabrics.md)).

### 4.7 Debug the 200 ms stall

A service sees random 200 ms stalls on small requests. A strong walkthrough: **bound** it (all requests or some, one client or all, one direction); note that **200 ms is the minimum retransmission timeout**, so suspect a lost packet with no following traffic to trigger fast retransmit (a **tail loss**, typical of small requests), and distinguish from 40 ms (Nagle plus delayed ACK) by the number. **Capture** on both ends and look for a retransmission exactly 200 ms after the original; **count** with `nstat` (`TcpExtTCPTimeouts`, `TCPLossProbes`) and `ss -ti` (`retrans`, `rto`). Then find **where the packet was lost**: interface drops, a full qdisc or ring, a policer, an overflowing accept queue, a microburst at a switch port (incast). **Fix** at the cause (buffer, microburst smoothing, a bad link), and mitigate with tail loss probe, a lower `rto_min` where the path allows, or request hedging at the application.

---

## 5. Common mistakes in interviews

1. **Naming the protocol and stopping**: no mechanism, no failure, no command.
2. **Jumping to a fix before bounding the symptom.**
3. **Blaming the network first**: the application, DNS and the receiver are as likely.
4. **Saying "check the firewall" without saying how** (capture on both ends, counters).
5. **Confusing a refusal with a drop**, or missing that a stall of a fixed size is a fingerprint.
6. **Designing without numbers**: no packets per second, no entries, no memory, no detection time.
7. **Ignoring state** in a load balancer, firewall or NAT: what it holds, what it costs, what a restart loses.
8. **Quoting defaults as facts** (TIME_WAIT, retries, timers): say they are defaults and that they differ by system.
9. **Forgetting the failure mode of your own design.**
10. **Claiming a measurement you did not make.** Say "I would expect" or "the formula gives" and keep the line between measured and reasoned.

---

## 6. Where to practise

- Rebuild scenarios S1, S4, S5, S6 and S9 yourself in two network namespaces with a veth pair, and break and fix each one while watching `ss`, `nstat` and `tcpdump`.
- Use the two trees of section 1 as a script: for each scenario say aloud the first command, what you expect, and what each answer would mean.
- For every question in section 3, answer in 30 seconds with the four-part shape, then give the follow-up.
- For each design question, write down the numbers first, then draw the data path on paper in two minutes.
- Mock with a partner: one breaks something in a namespace lab (a drop rule, a small MTU, a full backlog), the other debugs with only the ten commands.
- Read Brendan Gregg's Linux performance material, the `ip`, `ss` and `nstat` manual pages, the Cloudflare and Meta engineering posts on load balancing and packet drops, and RFC 1122 and RFC 5681 for TCP behaviour.

---

## 7. Cheat sheet

- Answer shape: definition, one level of depth, trade-off, how to see it; designs add the number that sizes it.
- Method: bound, walk the layers, capture both ends, read counters, one hypothesis, fix and prove.
- Refused at once means a reset came back; a wait means silence. A black hole is small works, large hangs.
- Fingerprints: 40 ms Nagle and delayed ACK; 200 ms RTO floor; 1 s and 3 s SYN timers; 5 s resolver timeout; about 15 minutes dead-peer give-up.
- `ss -ltn` Recv-Q / Send-Q is the accept queue; `ss -ti` names the limit; `nstat` counts `ListenOverflows` and `IPReversePathFilter`; `conntrack -S` counts table drops.
- Throughput = min(window / RTT, loss limit 1.22 MSS / (RTT sqrt p), link). TIME_WAIT bounds new connections per destination.
- Little's law: queue = rate x wait; queueing delay grows as load / (1 - load).
- Design order: numbers, data path, state, failure, measurement.

---

## 8. Sources and caveats

Sources used: the interview-oriented material on networking and infrastructure questions ([techinterview.org](https://www.techinterview.org/post/3233474140/system-design-load-balancer-architecture-l4-l7-nginx-haproxy-round-robin-least-connections-health-checks-ssl-termination/), [IGotAnOffer](https://igotanoffer.com/blogs/tech/load-balancing-system-design-interview), [Codemia](https://codemia.io/courses/system_design_fundamentals/load_balancing_fundamentals_algorithms)); RFC 793 and RFC 9293 (TCP), RFC 1122, RFC 5681 (congestion control), RFC 1191 and RFC 8201 (path MTU discovery), RFC 3704 (ingress filtering and reverse path checks); the Mathis, Semke, Mahdavi and Ott paper on TCP throughput; the `ss`, `ip`, `nstat`, `nft` and `conntrack` manual pages; and the tutorials NET 01 to NET 14, from which the numbers quoted in the answers are taken.

Caveats. The scenario snippets were **run fresh for this tutorial** on one Linux 6.18 VM as root: the refused, rejected and dropped ports (S1, in two namespaces with nftables), the Nagle comparison (S4, about 42 ms against about 0.05 ms on loopback), the accept queue (S5, Recv-Q 5 against Send-Q 4 with overflow counters rising) and the arithmetic for the port limit (S6), the window and Mathis limits (S9) and the dead-peer give-up time (S11). The other scenarios (the path MTU black hole, reverse path filtering, slow DNS, the slow reader, a full conntrack table, bufferbloat, kernel drops, TLS failures and ECMP imbalance) are described from the earlier runs of the previous version of this tutorial and from NET 06, 08, 11 and 14; they were **not re-run** here, and figures such as a 6.2 second DNS lookup or a 0.76 Mbit/s slow reader come from those earlier runs. The question bank answers are standard material written from the tutorials, not run one by one. The VM has no `netem`, so no propagation delay: long-path behaviour is computed from formulas, not measured. TCP defaults (TIME_WAIT, retries, timers, port ranges) are Linux defaults and differ by kernel, distribution and other operating systems. Hardware behaviour (switch ASICs, NIC offloads) is from documentation. Wi-Fi is not covered in this bank; see the WF tutorials.

Earlier and later: [NET 14](14_queueing_qos_and_traffic_management.md), [the networking plan](00_plan.md).
