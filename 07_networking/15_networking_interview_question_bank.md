# NET 15: Networking interview question bank

*Part of the [networking plan](00_plan.md) · Time: about 10 hours over a week · You need: [NET 01 to NET 14](00_plan.md) (NET 10 is the Wi-Fi stretch and is not needed). This is the **last core networking tutorial**. The earlier ones taught the subject; this one **drills it in the form an interview asks it**: 82 questions (plus eight one line debugging answers) with answers short enough to say aloud, two triage trees, fifteen troubleshooting scenarios (eleven reproduced in network namespaces), six design questions and a one page cheat sheet.*

A networking interview for a senior or staff role has three halves, and the third is the one candidates skip. The first asks **"what is X and why does it work that way?"** and rewards a crisp definition followed by one level of depth. The second hands you a **broken path** and asks **"what do you do?"**, and rewards a method: a first command, a reading of its output, a narrowed hypothesis, a fix and a way to know the fix worked. The third asks you to **design** something (a load balancer, a firewall data path, a packet router) and rewards numbers: how many packets a second, how much memory, what happens when a node dies. Sections 2 and 3 train the method, section 4 the definitions, section 5 the designs.

"A service sees random 200 ms stalls on small requests: walk me through it" and "a connection hangs: where do you look?" are the two most common openings in infrastructure and network-equipment loops. This tutorial gives each of them a tree, then reproduces the usual causes so that the symptom, the command that shows it and the number that proves it are all things you have seen once.

**Seven honest limits, stated first.**

1. **Eleven of the fifteen scenarios were reproduced** in this session (S1 to S11, scripts `s1_*.sh` to `s11_*.sh`, output frozen in `v/`). S12 to S15 are **from memory** or point at **measured results in NET 06, 11 and 14**, and are marked.
2. **The 82 answers are written from knowledge, not run one by one.** An answer that is backed by a run carries a tag such as **[run S4]** or **[NET 03]**. The numbers tagged **[NET nn]** were **copied from that tutorial's own measured output**; I did not rerun those experiments for this one. Untagged answers are standard material.
3. **No `netem`, so no propagation delay.** The round trip in every kernel run here is **0.036 ms**. Anything that depends on a long path (a window limit, a loss collapse, a policer's burst) is **computed** in `ans_a.py` with the formula shown, not measured.
4. **The DNS server in S7 is a 25 line stand-in** that answers every A query with one address. It shows resolver timeouts, not DNS behaviour.
5. **S10 ran in the host namespace over loopback**, because the conntrack limit cannot be set inside a namespace on this kernel; the limit was restored afterwards (262,144).
6. **One host, two vCPUs, single runs.** Times are what happened once. Where a repeat differed I say so (the loss table moved from 10,666 to 9,362 Mbit/s between two runs of the same script).
7. **Wi-Fi is not covered** (NET 10 is a stretch sub-track), and **hardware** (switch ASICs, NIC offload engines) appears only from memory.

By the end you can:

- **tell a refused connection from a dropped one in one command** (measured: a closed port answered with a reset in **0.000 s**; the same port behind a `reject with tcp reset` rule gave the same answer; behind a `drop` rule the client **timed out after 8.0 s** and the SYN was sent at **0, 1, 2, 3, 4, 5 and 7 s**, the sixth gap doubling because this kernel keeps its first retransmissions linear);
- **find a path MTU black hole** (measured: with the router's ICMP errors dropped, a DF ping of 1,372 bytes passed and one of **1,472 vanished**; a **200,000 byte** transfer delivered **0 bytes** and stalled for 6 s; with the ICMP errors let through, or with the MSS clamped on the router, it delivered **200,000 of 200,000**);
- **find reverse path filtering** (measured: the receiver saw both echo requests from an address it had no route to, answered **none**, and counted **2** in `IPReversePathFilter`);
- **diagnose the 40 ms request** (measured: a 4 byte header and a 100 byte body written separately took **43.8 ms** on average, **0.05 ms** with `TCP_NODELAY` or one write);
- **read an overflowing accept queue** (measured: a listener with backlog 4 that never called `accept` showed **Recv-Q 5, Send-Q 4**; **5 of 12** clients connected, **7 timed out**, and the kernel counted **42** listen overflows);
- **explain `Cannot assign requested address`** (measured: with 50 local ports the **51st** connection failed, **50** sockets sat in `TIME-WAIT`, a second server port took **40 of 40** more, and `tcp_tw_reuse` reused only sockets older than one second);
- **find a slow resolver** (measured: a dead first nameserver made a lookup take **6.16 s**, **2.01 s** with `timeout:1 attempts:1`, **0.01 s** with the working server first);
- **see a receiver limit the sender** (measured: a reader taking 1,000 bytes every 10 ms held a sender to **0.76 Mbit/s** with `rwnd_limited` at **87.4 %** and a send window of **1,448 bytes**);
- **see what loss does to TCP** (measured, cubic over a 0.036 ms path: **9,362 Mbit/s** with no loss, **1,103** at 0.1 %, **160** at 1 % and **14.5** at 5 %; computed for a 100 ms path, 1 % loss gives **1.4 Mbit/s**);
- **recognise a full conntrack table** (measured: with a limit of 200, **201** of 400 new flows were sent, **199** were refused to the sender with `Operation not permitted` and the log said `table full, dropping packet`);
- **say how long a dead peer takes to notice** (measured: **13.4 s** with `tcp_retries2` at 5 against **12.6 s** computed, which is **924.6 s** at the default 15; keepalive at 1, 1 and 3 gave **2.6 s**, which is **7,875 s** at the defaults);
- **answer 82 questions across the track**, and **design** a load balancer, a firewall data path, a packet router, a rate limiter, a NAT gateway and an incast mitigation with the numbers from NET 01 to 14.

## Where the numbers come from

Each experiment is a script `sN_*.sh` in `prog/` with its output frozen in `v/`, run with `run.sh`. `qnet.sh` builds two namespaces `qa` and `qb` joined by a veth pair `va` to `vb` (`10.9.0.1` and `10.9.0.2`); S2 builds a three namespace chain with a router. `tcpconn.py` connects with a timeout, `lsn.py`, `lsn_close.py` and `lsn_noaccept.py` are listeners, `bulk.py` sends and receives a byte count, `nagle.py` is the two write request loop, `portcycle.py` opens and closes connections, `minidns.py` is the stand-in resolver, `slowread.py` is a slow reader, `deadpeer.py` waits on a silent peer and `udpspray.py` opens many UDP flows. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` (or from the NET tutorial it is tagged with) or computed from a printed figure with the arithmetic shown. A number that is neither is marked **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| Refused, rejected and dropped (`s1_refuse.sh`) | 3 cases, an 8 s timeout, about 20 s |
| Path MTU black hole (`s2_pmtu.sh`) | 3 transfers with a 6 s stall limit, about 25 s |
| Reverse path filter (`s3_rpf.sh`) | 5 short pings, about 10 s |
| Nagle and delayed ACK (`s4_nagle.sh`) | 3 modes of 200 requests, about 10 s |
| Accept queue (`s5_backlog.sh`) | 12 clients with a 6 s limit, about 10 s |
| Port exhaustion (`s6_ports.sh`) | 220 connections, about 5 s |
| Resolver timeouts (`s7_dns.sh`) | 4 lookups, about 12 s |
| Slow reader (`s8_window.sh`) | 1 transfer of 8 s |
| Loss (`s9_loss.sh`) | 4 runs of 6 s |
| Conntrack full (`s10_conntrack.sh`) | 400 UDP flows, about 6 s |
| Dead peer (`s11_deadpeer.sh`) | 2 waits, about 20 s |

---


## 1. How to answer, and the two triage trees

### 1.1 Shape of a good answer

A senior answer has the same shape every time, and saying the shape out loud is half of the performance.

1. **One sentence that defines it**, with the mechanism and not a synonym. "A switch forwards on the destination MAC from a table it learns from source addresses" is a definition. "A switch connects devices" is not.
2. **One level of depth**: the number, the header field, the state or the failure that shows you have seen it. "...and it floods a frame whose destination it has not learned, which is why one broadcast in a loop made **290,810** packets in half a second (NET 01)."
3. **The trade-off or the failure mode.** Every mechanism in networking costs something; naming the cost is what separates recall from understanding.
4. **A way to see it.** The command that would show it on a real machine: `ss -ti`, `tcpdump`, `nstat`, `conntrack -S`, `ip route get`.

For a design question add a fifth: **the number that sizes it** (packets a second, entries, bytes of memory, the time to detect a failure).

### 1.2 The method for a broken path

An interviewer who hands you "the connection fails" or "it is slow" is not asking for the cause. They are watching your method. The method has six steps and the order matters.

1. **Restate and bound the symptom.** Which two ends, which protocol, since when, one direction or both, every request or some, constant or intermittent. Two questions often cut the tree in half: **does it fail at once or after a wait** (refusal against filtering), and **do small requests work** (an MTU or window problem against a reachability problem).
2. **Walk the layers from the bottom, with the cheapest tool at each.** Link (`ip link`), address and neighbour (`ip addr`, `ip neigh`), route (`ip route get DST`), reachability (`ping`, `ping -M do -s N` for the MTU), transport (`ss`, `nc`, `curl -v`), name (`dig`, `getent hosts`), application.
3. **Capture on both ends when you can.** A packet seen at one end and not at the other is a drop in between; a packet seen at neither is the sender's own stack. This single comparison is the most decisive move there is.
4. **Read the counters before the packets.** `nstat -az`, `ss -s`, `ip -s link`, `conntrack -S`, `ethtool -S` often name the cause (a counter that moved) without a capture.
5. **Form one hypothesis, predict what you will see, and test it.** "If this is a black hole, a 1,300 byte ping works and a 1,472 byte one does not."
6. **Fix, then prove the fix** with the same measurement that showed the problem, and say what you would monitor so that it is seen earlier next time.

### 1.3 Tree one: "I cannot connect"

```text
connect() fails or hangs
|
+-- fails AT ONCE with "connection refused"                      [S1: RST in 0.000 s]
|     a reset came back: the host is up and the path works
|     -> nothing listens on that port (ss -ltn on the server), or a firewall REJECTs,
|        or it listens on another address (127.0.0.1 only, the wrong interface)
|
+-- HANGS, then "timed out" (SYN-SENT in ss, SYNs repeated)      [S1: 8.0 s, SYN at 0,1,2,3,4,5,7 s]
|     no answer at all
|     |
|     +-- capture on both ends: does the SYN ARRIVE at the server?
|     |     NO  -> dropped on the path: a firewall DROP, a missing route or return route,
|     |            a wrong next hop, ARP not resolving (ip neigh shows FAILED)
|     |     YES -> does a SYN-ACK leave?
|     |           NO  -> the server drops it: rp_filter on an unroutable source [S3],
|     |                  a full accept queue [S5: ListenOverflows], conntrack full [S10],
|     |                  a local firewall DROP, SYN cookies off with a full SYN queue
|     |           YES -> it is lost on the way back: asymmetric route, a stateful firewall
|     |                  that never saw the SYN, NAT without the return mapping
|     |
|     +-- "No route to host" or "Network is unreachable" at once: no route (ip route get)
|
+-- CONNECTS, then the first large write hangs, small ones work   [S2: 0 of 200,000 bytes]
|     path MTU black hole: ping -M do -s 1472, then fix with the ICMP error or an MSS clamp
|
+-- connects, then "name or service not known", or a long pause before the SYN [S7: 6.16 s]
|     resolution: getent hosts, dig @server, resolv.conf order and timeouts
|
+-- "Cannot assign requested address" on the CLIENT               [S6: the 51st of 50 ports]
|     ephemeral ports or TIME_WAIT: ss -tan | grep -c TIME-WAIT, the port range, more
|     destinations, a connection pool
|
+-- "Address already in use" on the SERVER at start
      a listener or a TIME_WAIT on the port: ss -ltnp, SO_REUSEADDR (NET 03, NET 04)
```

The two questions at the top of the tree do most of the work. **Refusal at once** means a reset came back: the far host is alive and reachable, so the problem is a port or a rule. **A wait** means silence: either the packet never arrived, or it arrived and was dropped, or the answer was lost, and one capture on the server says which.

### 1.4 Tree two: "the network is slow"

```text
slow: say which of latency, throughput, or stalls
|
+-- ONE slow request among fast ones (a stall of a fixed size)
|     40 ms  -> Nagle and delayed ACK on a two write request             [S4: 43.8 ms]
|     200 ms -> a retransmission timeout (loss, a tail loss, RTO floor of 200 ms)
|     1 s / 3 s -> a SYN retransmission (S1), or a DNS retry (S7: 5 s timeout, 6.16 s)
|     5 s    -> the default resolver timeout; 15 s, 30 s -> application retry timers
|
+-- every request is slow (latency)
|     idle ping high -> distance (a long round trip), or a bad route (traceroute)
|     ping fine idle, bad under load -> a standing queue: bufferbloat     [NET 14: 99 ms vs 0.036 ms]
|     slow only the FIRST request -> handshakes: DNS + TCP + TLS in round trips [NET 08]
|
+-- throughput is low on a fast link
|     ss -ti on the sender, then read which limit it reports:
|     |  rwnd_limited high    -> the receiver or its application is slow       [S8: 87.4 %, 0.76 Mbit/s]
|     |  cwnd_limited / loss  -> congestion window: loss or a policer          [S9: 9,362 -> 1,103 Mbit/s at 0.1 %]
|     |  sndbuf_limited       -> the send buffer or autotuning is too small
|     |  app-limited (busy low) -> the application does not send enough
|     long path, small window: window / RTT is the limit                        [ans_a: 64 KB at 100 ms is 5.24 Mbit/s]
|
+-- intermittent drops or resets
      counters: nstat (retransmits, ListenOverflows), ip -s link (drops), ethtool -S,
      conntrack -S (drop, insert_failed), softnet_stat; then capture                [NET 06]
```

The tree for slowness starts with a **shape**, because a stall of a fixed size is a fingerprint: **40 ms** is delayed ACK, **200 ms** is the minimum retransmission timeout, **1 s** and **3 s** are SYN timers and **5 s** is a resolver timeout. A latency that scales with load is a queue; a latency that is a multiple of the round trip is a handshake.

### 1.5 The ten commands

| Command | What it answers |
|---|---|
| `ip -s link show DEV` | Errors, drops, overruns on an interface |
| `ip route get DST` | Which route, which source address, which interface, any cached MTU |
| `ip neigh` | Is the next hop resolved (REACHABLE, STALE, FAILED) |
| `ss -tan` and `ss -ltn` | Every TCP socket and its state; for a listener, accept queue length and limit |
| `ss -ti dst IP` | One connection's window, RTT, retransmissions and which limit applies |
| `nstat -az` | Kernel counters: `ListenOverflows`, `IPReversePathFilter`, `TCPTimeouts` |
| `tcpdump -nn -i DEV 'filter'` | The packets themselves, on both ends if possible |
| `conntrack -S` and `-C` | The state table's size and its drops |
| `ping -M do -s N` and `tracepath` | The path MTU and the hops |
| `dig @SERVER NAME +time=1` and `getent hosts` | Resolution, with the resolver the application actually uses |

---


## 2. Fifteen troubleshooting scenarios

Each scenario has the **symptom** as a user would report it, the **first command**, the output (frozen from this session for S1 to S11), a **reading**, the **fix** and the **proof**. S12 to S15 are not rerun here; they point at where the number was measured or say plainly that they are from memory.

### S1. "Connection refused" against "timed out" against "rejected"

**Symptom.** A client cannot reach a service. The error text is the first clue and most people do not read it.

```bash
# s1_refuse.sh: "I cannot connect": no listener, a firewall that rejects, and a firewall that drops. The three look different in time and in packets.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
B python3 lsn.py 8080 12 &
sleep 0.5
echo "--- 1. a listener on 8080 and nothing on 8081"
A python3 tcpconn.py 10.9.0.2 8080 5
A python3 tcpconn.py 10.9.0.2 8081 5
B nft add table ip t; B nft "add chain ip t i { type filter hook input priority 0; }"
echo "--- 2. the firewall rejects 8080 with a tcp reset: the same answer as no listener"
B nft add rule ip t i tcp dport 8080 reject with tcp reset
A python3 tcpconn.py 10.9.0.2 8080 5
B nft flush chain ip t i
echo "--- 3. the firewall drops 8080: no answer at all, and the SYN is retried"
B nft add rule ip t i tcp dport 8080 drop
B tcpdump -l -nn -tt -i vb 'tcp port 8080' > /tmp/s1.txt 2> /dev/null &
sleep 0.5
A python3 tcpconn.py 10.9.0.2 8080 8
sleep 0.3; pkill tcpdump; sleep 0.3
awk 'NR==1{t0=$1} /Flags \[S\]/{printf "SYN at %.1f s\n", $1-t0}' /tmp/s1.txt
echo "--- 4. the socket state of the client while it waits"
A python3 tcpconn.py 10.9.0.2 8080 3 > /dev/null &
sleep 1; A ss -tan state syn-sent | sed 's/  */ /g'; wait %2 2> /dev/null
echo "--- 5. a UDP port with no listener: ICMP port unreachable comes back"
A python3 -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(('10.9.0.2', 9999)); s.settimeout(2); s.send(b'x')
try: s.recv(10)
except ConnectionRefusedError: print('send then recv: connection refused (ICMP port unreachable)')
"
```

```text
--- 1. a listener on 8080 and nothing on 8081
connected        after 0.000 s
refused (RST)    after 0.000 s
--- 2. the firewall rejects 8080 with a tcp reset: the same answer as no listener
refused (RST)    after 0.000 s
--- 3. the firewall drops 8080: no answer at all, and the SYN is retried
timed out        after 8.009 s
SYN at 0.0 s
SYN at 1.0 s
SYN at 2.0 s
SYN at 3.1 s
SYN at 4.1 s
SYN at 5.1 s
SYN at 7.1 s
--- 4. the socket state of the client while it waits
Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
0 1 10.9.0.1:33506 10.9.0.2:8080
--- 5. a UDP port with no listener: ICMP port unreachable comes back
send then recv: connection refused (ICMP port unreachable)
```

**Reading.** A closed port and a `reject with tcp reset` rule give the **same** answer, an RST in **0.000 s**, so "refused" cannot tell you whether the service is down or a firewall says no; a reject rule with an ICMP error looks different (`No route to host` or `administratively prohibited`). A `drop` gives **silence**: the client waited **8.0 s** (the script's limit) and the SYN left at **0, 1, 2, 3.1, 4.1, 5.1 and 7.1 s**. The first retransmissions are one second apart because `tcp_syn_linear_timeouts` is 4 on this kernel (**from memory**: it was added so that early SYN retries do not back off while a path is flapping); after the linear phase the gap doubles, so the classic schedule of 1, 2, 4, 8 s that gives up at 127 s is only the tail. In `ss`, the waiting socket is `SYN-SENT` with `Send-Q 1`. UDP has no handshake: the refusal arrives as an **ICMP port unreachable** on the *next* call, which Python raised as `connection refused`.

**Fix and proof.** Refused: start the service or fix the bind address (`ss -ltn`). Timed out: find where the SYN stops with a capture on both ends (tree one in section 1.3). Proof: the same `nc -vz` returns at once.

### S2. A path MTU black hole

**Symptom.** SSH connects and works, `ls` works, `cat bigfile` hangs. HTTPS handshakes stall after the ClientHello. Small requests succeed; large ones never finish.

```bash
# s2_pmtu.sh: "small requests work and large transfers hang": a router with a smaller MTU on its far side, and ICMP "fragmentation needed" that a firewall eats.
cd "$(dirname "$0")"
for n in qa qr qb; do ip netns add $n; ip netns exec $n ip link set lo up; done
trap 'for n in qa qr qb; do ip netns del $n 2> /dev/null; done; pkill -f bulk.py' EXIT
ip link add a1 netns qa type veth peer name r1 netns qr
ip link add r2 netns qr type veth peer name b1 netns qb
ip netns exec qa ip addr add 10.1.0.1/24 dev a1; ip netns exec qr ip addr add 10.1.0.2/24 dev r1
ip netns exec qr ip addr add 10.2.0.2/24 dev r2; ip netns exec qb ip addr add 10.2.0.1/24 dev b1
for p in "qa a1" "qr r1" "qr r2" "qb b1"; do set -- $p; ip netns exec $1 ip link set $2 up; ip netns exec $1 ethtool -K $2 tso off gso off gro off > /dev/null 2>&1; done
ip netns exec qa ip route add 10.2.0.0/24 via 10.1.0.2; ip netns exec qb ip route add 10.1.0.0/24 via 10.2.0.2
ip netns exec qr sysctl -qw net.ipv4.ip_forward=1
ip netns exec qr ip link set r2 mtu 1400
RA() { ip netns exec qa "$@"; }; RB() { ip netns exec qb "$@"; }; RR() { ip netns exec qr "$@"; }
echo "--- 1. the path: A and B both have MTU 1500, so both advertise an MSS of 1460, but the router's far side has MTU 1400"
RA ip -o link show a1 | grep -o 'mtu [0-9]*' | sed 's/^/A  /'; RB ip -o link show b1 | grep -o 'mtu [0-9]*' | sed 's/^/B  /'; RR ip -o link show r2 | grep -o 'mtu [0-9]*' | sed 's/^/router far side  /'
RR nft add table ip t; RR nft "add chain ip t o { type filter hook output priority 0; }"
echo "--- 2. the router's firewall drops ICMP destination unreachable; ping with the DF bit set: 1,372 bytes of data fits in 1,400, 1,472 does not"
RA ping -c 1 -W 1 -M do -s 1372 10.2.0.1 | grep -E 'transmitted|bytes from' | sed 's/ time.*//' | paste -sd' '
RR nft add rule ip t o icmp type destination-unreachable drop
RA ping -c 1 -W 1 -M do -s 1472 10.2.0.1 | grep -E 'transmitted' | sed 's/, time.*//'
echo "--- 3. with the ICMP errors still dropped, 200 KB goes from A to B"
RB python3 bulk.py recv 10.2.0.1 7000 200000 6 &
sleep 0.5; RA python3 bulk.py send 10.2.0.1 7000 200000 6 2> /dev/null; wait %1
echo "--- 4. the same ping now"
RA ping -c 2 -W 1 -M do -s 1472 10.2.0.1 | grep -E 'transmitted' | sed 's/, time.*//'
echo "--- 5. let the ICMP errors through: the sender learns the path MTU and the transfer finishes"
RR nft flush chain ip t o
RB python3 bulk.py recv 10.2.0.1 7001 200000 6 &
sleep 0.5; RA python3 bulk.py send 10.2.0.1 7001 200000 6; wait %1
RA ip route get 10.2.0.1 | sed -n 1,2p | sed 's/^ *//'
echo "--- 6. the other fix: clamp the MSS on the router, so no oversized packet is ever sent"
RA ip route flush cache; RA ip route del 10.2.0.0/24; RA ip route add 10.2.0.0/24 via 10.1.0.2
RR nft add table ip m; RR nft "add chain ip m f { type filter hook forward priority 0; }"
RR nft add rule ip m f tcp flags syn tcp option maxseg size set rt mtu
RR nft add rule ip t o icmp type destination-unreachable drop
RB python3 bulk.py recv 10.2.0.1 7002 200000 6 &
sleep 0.5; RA python3 bulk.py send 10.2.0.1 7002 200000 6; wait %1
RA ip route get 10.2.0.1 | sed -n 1,3p | sed 's/^ *//'
```

```text
--- 1. the path: A and B both have MTU 1500, so both advertise an MSS of 1460, but the router's far side has MTU 1400
A  mtu 1500
B  mtu 1500
router far side  mtu 1400
--- 2. the router's firewall drops ICMP destination unreachable; ping with the DF bit set: 1,372 bytes of data fits in 1,400, 1,472 does not
1380 bytes from 10.2.0.1: icmp_seq=1 ttl=63 1 packets transmitted, 1 received, 0% packet loss,
1 packets transmitted, 0 received, 100% packet loss
--- 3. with the ICMP errors still dropped, 200 KB goes from A to B
received 0 of 200000 bytes, then nothing for 6 s
--- 4. the same ping now
2 packets transmitted, 0 received, 100% packet loss
--- 5. let the ICMP errors through: the sender learns the path MTU and the transfer finishes
received 200000 of 200000 bytes in 0.00 s
10.2.0.1 via 10.1.0.2 dev a1 src 10.1.0.1 uid 0
cache expires 599sec mtu 1400
--- 6. the other fix: clamp the MSS on the router, so no oversized packet is ever sent
received 200000 of 200000 bytes in 0.00 s
10.2.0.1 via 10.1.0.2 dev a1 src 10.1.0.1 uid 0
cache
```

**Reading.** Both ends have MTU **1500** and negotiate an MSS of 1460, so the handshake and small packets work. The router's far side has MTU **1400**. A full size segment with DF set cannot cross, and the router should send an ICMP "fragmentation needed" that tells the sender to shrink. Here a firewall dropped that ICMP, so the sender never learns: a DF ping of **1,372** data bytes (1,400 on the wire) passed, one of **1,472** got **100 % loss**, and a **200,000 byte** transfer delivered **0 bytes** and then nothing for 6 s. With the ICMP errors allowed, the transfer finished (**200,000 of 200,000**) and `ip route get` showed `cache expires 599sec mtu 1400`: the sender had **cached the path MTU for 10 minutes**. The other fix, an MSS clamp on the router (`tcp option maxseg size set rt mtu`), finished the transfer with **no cached MTU at all**, because no oversized packet was ever sent.

**Fix and proof.** Never filter ICMP type 3 code 4 (and type 2 for IPv6); or clamp the MSS at the tunnel or router; or enable `tcp_mtu_probing`. Proof: `ping -M do -s 1472` against the far host, and the large transfer completes.

### S3. Replies that never come: reverse path filtering

**Symptom.** A host receives a request, `tcpdump` shows it arriving, the application never sees it and nothing is sent back.

```bash
# s3_rpf.sh: "it works from one address and not from another": the receiver has no route back to the source, and reverse path filtering drops the packet before anything replies.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
A ip addr add 10.77.0.1/32 dev va
B sysctl -qw net.ipv4.conf.all.rp_filter=1 net.ipv4.conf.vb.rp_filter=1
B nstat -n 2> /dev/null
echo "--- 1. ping from the address that B has a route to (10.9.0.1)"
A ping -c 2 -W 1 -I 10.9.0.1 10.9.0.2 | grep -E 'transmitted' | sed 's/, time.*//'
echo "--- 2. ping from 10.77.0.1: B has no route to it. Does B see the packets? Does anything come back?"
B tcpdump -l -nn -c 2 -i vb icmp > /tmp/s3.txt 2> /dev/null &
sleep 0.4
A ping -c 2 -W 1 -I 10.77.0.1 10.9.0.2 | grep -E 'transmitted' | sed 's/, time.*//'
sleep 0.5; pkill tcpdump
echo "B's capture:"; sed 's/^[0-9:.]* //' /tmp/s3.txt
echo "--- 3. what B counted"
B nstat -az TcpExtIPReversePathFilter IpInAddrErrors 2> /dev/null | awk 'NR>1{print $1, $2}'
echo "--- 4. rp_filter off: B now tries to answer and finds no route"
B sysctl -qw net.ipv4.conf.all.rp_filter=0 net.ipv4.conf.vb.rp_filter=0
B tcpdump -l -nn -c 2 -i vb icmp > /tmp/s3.txt 2> /dev/null &
sleep 0.4
A ping -c 1 -W 1 -I 10.77.0.1 10.9.0.2 | grep -E 'transmitted' | sed 's/, time.*//'
sleep 0.5; pkill tcpdump; sed 's/^[0-9:.]* //' /tmp/s3.txt
B ip route get 10.77.0.1 2>&1 | head -1
echo "--- 5. give B the route, rp_filter back on: works"
B sysctl -qw net.ipv4.conf.all.rp_filter=1 net.ipv4.conf.vb.rp_filter=1
B ip route add 10.77.0.1/32 dev vb
A ping -c 2 -W 1 -I 10.77.0.1 10.9.0.2 | grep -E 'transmitted' | sed 's/, time.*//'
```

```text
--- 1. ping from the address that B has a route to (10.9.0.1)
2 packets transmitted, 2 received, 0% packet loss
--- 2. ping from 10.77.0.1: B has no route to it. Does B see the packets? Does anything come back?
2 packets transmitted, 0 received, 100% packet loss
B's capture:
IP 10.77.0.1 > 10.9.0.2: ICMP echo request, id 18861, seq 1, length 64
IP 10.77.0.1 > 10.9.0.2: ICMP echo request, id 18861, seq 2, length 64
--- 3. what B counted
IpInAddrErrors 0
TcpExtIPReversePathFilter 2
--- 4. rp_filter off: B now tries to answer and finds no route
1 packets transmitted, 0 received, 100% packet loss
IP 10.77.0.1 > 10.9.0.2: ICMP echo request, id 18881, seq 1, length 64

RTNETLINK answers: Network is unreachable
--- 5. give B the route, rp_filter back on: works
2 packets transmitted, 2 received, 0% packet loss
```

**Reading.** Ping from the known neighbour worked. Ping from **10.77.0.1**, an address with no return route on B, was **captured on B twice and answered zero times**, and `TcpExtIPReversePathFilter` counted **2**. Strict `rp_filter` drops a packet whose source address would not be routed back out the interface it arrived on, **before** the transport layer, so no socket and no firewall counter sees it; only the capture and that one counter show it. Turning the filter off did not make it work: B then tried to reply and `ip route get` said `Network is unreachable`, the real fault. Adding the route made it work with the filter back on.

**Fix and proof.** Fix the routing (asymmetric paths, multihoming without policy routing) rather than disabling the filter; use `rp_filter=2` (loose) where asymmetry is intended. Proof: `nstat` stops counting and the echo reply appears.

### S4. The request that takes 40 ms

**Symptom.** A small RPC takes a flat 40 ms; two RPCs in a row take 80 ms; CPU is idle, the network is idle.

```bash
# s4_nagle.sh: "small requests sometimes take 40 ms": a request written in two pieces meets Nagle's algorithm and the receiver's delayed ACK.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
B python3 nagle.py server 9100 3 &
sleep 0.5
echo "--- 200 requests each, 4 byte header then 100 byte body, then wait for a 4 byte answer; latency in ms"
A python3 nagle.py client 10.9.0.2 9100 two_writes
A python3 nagle.py client 10.9.0.2 9100 nodelay
A python3 nagle.py client 10.9.0.2 9100 one_write
```

```text
--- 200 requests each, 4 byte header then 100 byte body, then wait for a 4 byte answer; latency in ms
two_writes  mean  43.82   p50  43.99   p99  46.71   max  47.86 ms
nodelay     mean   0.05   p50   0.05   p99   0.11   max   0.62 ms
one_write   mean   0.05   p50   0.04   p99   0.13   max   0.44 ms
```

**Reading.** The client writes a 4 byte header and then a 100 byte body, then waits for the answer. **Nagle's algorithm** holds the second small write until the first is acknowledged; the server's **delayed ACK** holds that acknowledgement until it has data to send or about 40 ms pass. Two write calls took **43.8 ms** on average (p99 **46.7**); with `TCP_NODELAY` **0.05 ms**; with one write of 104 bytes **0.05 ms**, 880 times faster without touching a socket option. NET 03 measured **41.8 ms** for the same pattern.

**Fix and proof.** Combine the writes (`writev`, or build the message in one buffer); set `TCP_NODELAY` for latency-sensitive request and response traffic. Proof: the 40 ms plateau in the latency histogram disappears.

### S5. Connections hang when the server is busy: the accept queue

**Symptom.** Under load, some clients connect instantly and others hang for seconds and then time out. The server shows no errors.

```bash
# s5_backlog.sh: "new connections stall for a second or three under load": the accept queue is full because the application is not calling accept().
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap 'q_down; pkill -f lsn.py' EXIT
B python3 lsn_noaccept.py 8300 9 4 &
sleep 0.5
B nstat -n 2> /dev/null
echo "--- 1. a listener with backlog 4 that never calls accept; twelve clients connect at once, with a 6 s limit each"
for i in $(seq 12); do A python3 tcpconn.py 10.9.0.2 8300 6 > /tmp/s5_$i.txt & done
sleep 1.5
echo "--- 2. the listening socket after 1.5 s: Recv-Q is the accept queue length, Send-Q its limit"
B ss -ltn 'sport = :8300' | awk 'NR>1{print "Recv-Q " $2 "  Send-Q " $3}'
echo "--- 3. the client sockets after 1.5 s, by state"
A ss -tan dst 10.9.0.2 | awk 'NR>1{c[$1]++} END{for (k in c) print c[k], k}' | sort -k2
wait %2 2> /dev/null; for i in $(seq 12); do wait; done 2> /dev/null; sleep 0.5
echo "--- 4. the outcome of the twelve connects"
cat /tmp/s5_*.txt | sed -E 's/after ([0-9]+)\.[0-9]+ s/after ~\1 s/' | sort | uniq -c
echo "--- 5. what the kernel counted on B"
B nstat -az TcpExtListenOverflows TcpExtListenDrops 2> /dev/null | awk 'NR>1{print $1, $2}'
```

```text
--- 1. a listener with backlog 4 that never calls accept; twelve clients connect at once, with a 6 s limit each
--- 2. the listening socket after 1.5 s: Recv-Q is the accept queue length, Send-Q its limit
Recv-Q 5  Send-Q 4
--- 3. the client sockets after 1.5 s, by state
5 FIN-WAIT-2
7 SYN-SENT
--- 4. the outcome of the twelve connects
      5 connected        after ~0 s
      7 timed out        after ~6 s
--- 5. what the kernel counted on B
TcpExtListenOverflows 42
TcpExtListenDrops 42
```

**Reading.** The server's `listen(4)` backlog is the length of the **accept queue**, filled by the kernel after the handshake and drained by `accept()`. This server never called `accept`. `ss -ltn` on a listener shows the queue as **Recv-Q 5** (4 plus one) against the limit as **Send-Q 4**; when Recv-Q reaches Send-Q the queue is full. Twelve clients arrived: **5 connected** (the client thinks it is connected, because the kernel completed the handshake), **7 timed out** after 6 s and were still `SYN-SENT`, and the server's counters read **`ListenOverflows 42`** and **`ListenDrops 42`**, one per dropped handshake packet including the SYN retries. The kernel drops the packet and lets the client retry, which looks like a network loss and is not.

**Fix and proof.** Find why `accept` is slow (a blocked thread, a single threaded loop, too few workers); raise `listen(backlog)` and `net.core.somaxconn` (the real limit is the smaller); alert on `ListenOverflows`. Proof: Recv-Q stays near 0 and the counter stops rising.

### S6. `Cannot assign requested address` on the client

**Symptom.** A client or proxy opening many short connections starts failing after some time, with `EADDRNOTAVAIL` (errno 99). The server is fine.

```bash
# s6_ports.sh: "Cannot assign requested address" from a client that opens many short connections to one server.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap 'q_down; pkill -f lsn_close.py' EXIT
B python3 lsn_close.py 8400 25 &
B python3 lsn_close.py 8401 25 &
sleep 0.5
A sysctl -qw net.ipv4.ip_local_port_range="40000 40049"
echo "--- 1. 50 local ports; 70 connect-and-close cycles to one server port"
A python3 portcycle.py 10.9.0.2 8400 70
echo "--- 2. the client sockets: each closed connection waits 60 s in TIME_WAIT and keeps its local port"
A ss -tan | awk 'NR>1{c[$1]++} END{for (k in c) print c[k], k}' | sort -k2
echo "--- 3. the same cycles to a second server port: the port limit is per (address, port) pair of the peer"
A python3 portcycle.py 10.9.0.2 8401 40
echo "--- 4. tcp_tw_reuse=1 lets an outgoing connection reuse a TIME_WAIT socket older than one second: the old 50 are reusable, the 50 just reused are not yet"
A sysctl -qw net.ipv4.tcp_tw_reuse=1
sleep 1.1
A python3 portcycle.py 10.9.0.2 8400 70
```

```text
--- 1. 50 local ports; 70 connect-and-close cycles to one server port
50 of 70 cycles worked; first failure at cycle 51: Cannot assign requested address (errno 99)
--- 2. the client sockets: each closed connection waits 60 s in TIME_WAIT and keeps its local port
50 TIME-WAIT
--- 3. the same cycles to a second server port: the port limit is per (address, port) pair of the peer
40 of 40 cycles worked
--- 4. tcp_tw_reuse=1 lets an outgoing connection reuse a TIME_WAIT socket older than one second: the old 50 are reusable, the 50 just reused are not yet
50 of 70 cycles worked; first failure at cycle 51: Cannot assign requested address (errno 99)
```

**Reading.** A TCP connection is identified by source address, source port, destination address and destination port. A client talking to **one server address and port** has only its local ports to vary. The range here was **50** ports; **50 cycles** worked and the **51st** failed. Each closed connection leaves its local port in `TIME-WAIT` for 60 s (the `ss` count was **50 TIME-WAIT**), so the ports stay taken. A **second server port** gave a different four-tuple and **40 of 40** cycles worked, which shows the limit is per destination. With `tcp_tw_reuse=1` the same 50 of 70 happened here, because a `TIME-WAIT` socket may be reused only if it is **older than one second** (the 50 reused at that moment were not yet). With the real range `32768 60999` there are **28,232** ports, which is **471** new connections a second to a single destination (`ans_d`).

**Fix and proof.** Reuse connections (keep alive, pools, multiplexing); spread across destinations or source addresses; widen `ip_local_port_range`; use `tcp_tw_reuse`. Do not use `tcp_tw_recycle` (removed from Linux, broke NAT clients, **from memory**). Proof: `ss -tan state time-wait | wc -l` stays far below the range.

### S7. Everything is slow to start: DNS

**Symptom.** A program takes about 6 seconds before it does anything; once running it is fast; `ping` of the IP is instant.

```bash
# s7_dns.sh: "everything is slow to start": the first resolver in resolv.conf does not answer, so every lookup waits for a timeout before the second is tried.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap 'q_down; rm -rf /etc/netns/qa; pkill -f minidns.py' EXIT
B python3 minidns.py 10.9.0.2 14 &
sleep 0.5
mkdir -p /etc/netns/qa
t() { local s=$(date +%s.%N); A getent hosts svc.example.test | head -1; echo "   took $(echo "$(date +%s.%N) - $s" | bc | xargs printf '%.2f') s"; }
echo "--- 1. resolv.conf: nameserver 10.9.0.99 (nobody there), then 10.9.0.2 (answers)"
printf 'nameserver 10.9.0.99\nnameserver 10.9.0.2\n' > /etc/netns/qa/resolv.conf
t
echo "--- 2. the same with options timeout:1 attempts:1"
printf 'nameserver 10.9.0.99\nnameserver 10.9.0.2\noptions timeout:1 attempts:1\n' > /etc/netns/qa/resolv.conf
t
echo "--- 3. the working server first"
printf 'nameserver 10.9.0.2\nnameserver 10.9.0.99\n' > /etc/netns/qa/resolv.conf
t
echo "--- 4. dig against the dead server: +time=1 +tries=3 is a 3 s wait before it gives up"
s=$(date +%s.%N); A dig +time=1 +tries=3 @10.9.0.99 svc.example.test 2>&1 | grep -c 'timed out' | sed 's/^/timeout messages: /'; echo "took $(echo "$(date +%s.%N) - $s" | bc | xargs printf '%.2f') s"
```

```text
--- 1. resolv.conf: nameserver 10.9.0.99 (nobody there), then 10.9.0.2 (answers)
10.9.0.77       svc.example.test
   took 6.16 s
--- 2. the same with options timeout:1 attempts:1
10.9.0.77       svc.example.test
   took 2.01 s
--- 3. the working server first
10.9.0.77       svc.example.test
   took 0.01 s
--- 4. dig against the dead server: +time=1 +tries=3 is a 3 s wait before it gives up
timeout messages: 3
took 3.03 s
```

**Reading.** `resolv.conf` listed a dead nameserver first, then a live one. The resolver waits for the first one's default timeout, then asks the next. The lookup took **6.16 s**; with `options timeout:1 attempts:1` **2.01 s**; with the live server first **0.01 s**. `dig +time=1 +tries=3` printed three timeout messages in **3.03 s**, which is the same arithmetic. The stand-in DNS server here only answers one address; this shows resolver timing, not DNS behaviour.

**Fix and proof.** Remove or reorder dead servers, shorten the timeout and attempts, run a local caching resolver. Proof: `time getent hosts name` returns in milliseconds. Use `getent`, not just `dig`: `dig` ignores `nsswitch.conf`, search domains and `/etc/hosts`, so it can succeed where the application fails.

### S8. Fast link, slow transfer: a slow reader

**Symptom.** `iperf3` between the hosts runs at line rate, but the application transfer is a few hundred kilobits a second. No loss, no retransmissions.

```bash
# s8_window.sh: "the link is fast and the transfer is slow": the receiving application reads slowly, the receive window closes, and the sender is limited by the receiver and not by the network.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap 'q_down; pkill -f slowread.py' EXIT
B python3 slowread.py server 9200 1000 0.01 8 &
sleep 0.5
A python3 slowread.py client 10.9.0.2 9200 8 &
sleep 4
echo "--- 1. the sender's socket after 4 s: what limits it"
A ss -tin dst 10.9.0.2 | tr '\n' ' ' | grep -oE 'rcv_space:[0-9]+|snd_wnd:[0-9]+|cwnd:[0-9]+|rwnd_limited:[0-9]+ms\([0-9.]+%\)|busy:[0-9]+ms|sndbuf_limited:[0-9]+ms\([0-9.]+%\)|unacked:[0-9]+' | tr '\n' ' '; echo
echo "--- 2. the receiver's socket: the queue it has not read"
B ss -tn 'sport = :9200' | awk 'NR>1{print "Recv-Q " $2 "  Send-Q " $3}'
wait %1 2> /dev/null; wait %2 2> /dev/null
```

```text
--- 1. the sender's socket after 4 s: what limits it
cwnd:12 busy:3988ms rwnd_limited:3484ms(87.4%) unacked:1 rcv_space:14480 snd_wnd:1448
--- 2. the receiver's socket: the queue it has not read
Recv-Q 792  Send-Q 0
receiver read 759856 bytes in 8 s: 0.76 Mbit/s
```

**Reading.** The receiver read **1,000 bytes every 10 ms**. Its socket buffer filled and it advertised a window of nearly zero. The sender's `ss -tin` says exactly which limit applies: **`rwnd_limited:3484ms(87.4%)`** of the **3,988 ms** it was busy, with `snd_wnd:1448` and `cwnd:12` (so the congestion window was not the limit). The receiver's `Recv-Q` held unread data. The transfer ran at **0.76 Mbit/s** (759,856 bytes in 8 s). The network is not at fault, and no capture of the path would find anything.

**Fix and proof.** Fix the reader: blocking work in the read loop, a full downstream queue, a lock. Check `ss -tin` for `rwnd_limited`, `cwnd_limited` and `sndbuf_limited` before blaming the network. Proof: `rwnd_limited` falls and throughput rises.

### S9. What loss does to throughput

**Symptom.** A transfer on a clean looking link is far below the line rate; `ping` shows a fraction of a percent of loss.

```bash
# s9_loss.sh: "throughput collapses on a lossy link": TCP throughput against random loss, on a path whose round trip is 0.036 ms (the model answer for a long path is in problem A).
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
B iperf3 -s > /dev/null &
sleep 0.5
B nft add table ip t; B nft "add chain ip t i { type filter hook prerouting priority -150; }"
echo "--- cubic, 6 s per row, with the stated random loss dropped on the receiver ingress (per mille: 1 is 0.1 %, 10 is 1 %, 50 is 5 %); rtt_ms is the sender smoothed round trip"
printf "%-8s %9s %6s %9s\n" loss Mbit/s retr rtt_ms
for p in 0 1 10 50; do
  B nft flush chain ip t i
  [ $p != 0 ] && B nft add rule ip t i numgen random mod 1000 \< $p drop
  A iperf3 -c 10.9.0.2 -t 6 -C cubic -J > /tmp/s9.json
  python3 -c "
import json
d = json.load(open('/tmp/s9.json'))['end']
print('%-8s %9.1f %6d %9.2f' % ('$p per mille', d['sum_received']['bits_per_second'] / 1e6, d['sum_sent']['retransmits'], d['streams'][0]['sender']['mean_rtt'] / 1000.0))
"
done
```

```text
warning: this system does not seem to support IPv6 - trying IPv4
--- cubic, 6 s per row, with the stated random loss dropped on the receiver ingress (per mille: 1 is 0.1 %, 10 is 1 %, 50 is 5 %); rtt_ms is the sender smoothed round trip
loss        Mbit/s   retr    rtt_ms
0 per mille    9362.2    211      0.11
1 per mille    1103.4   1539      0.07
10 per mille     159.9    989      0.21
50 per mille      14.5    342      1.56
iperf3: interrupt - the server has terminated
```

**Reading.** A tiny loss rate cuts TCP throughput hard because every loss is read as congestion and halves the window. Over this 0.036 ms path with cubic: **9,362 Mbit/s** with none, **1,103** at 0.1 % (an eighth), **160** at 1 % and **14.5** at 5 %. The numbers moved between two runs (10,666, 1,330, 171 and 13.8 earlier), so read the shape, not the digits. The loss here is random drops on the receiver. On a **long path** the damage is far worse because the window takes many round trips to regrow: the Mathis estimate, `MSS / (RTT × √p) × 1.22`, gives (`ans_a`) about **1.4 Mbit/s at 100 ms and 1 % loss**, whatever the link speed.

**Fix and proof.** Find and fix the loss (counters on every hop: `ip -s link`, `ethtool -S`, switch ports, a duplex mismatch, a bad optic); use a model that is not loss-based (BBR) on a long lossy path, with care; shorten the round trip (a nearby cache). Proof: `nstat` retransmissions fall and throughput climbs.

### S10. New connections fail when conntrack is full

**Symptom.** A gateway or busy host drops new flows at random while old ones keep working. `dmesg` has one line.

```bash
# s10_conntrack.sh: "new connections fail while old ones work": the conntrack table is full, so a packet that would need a new entry is dropped. Run in the host namespace over loopback, because the table limit cannot be set inside a namespace here; the limit is restored at the end.
cd "$(dirname "$0")"
OLD=$(sysctl -n net.netfilter.nf_conntrack_max)
trap 'sysctl -qw net.netfilter.nf_conntrack_max=$OLD; nft delete table ip t10 2> /dev/null; conntrack -F > /dev/null 2>&1; pkill -f udpspray.py; pkill -f sink.py' EXIT
nft add table ip t10; nft "add chain ip t10 p { type filter hook prerouting priority -200; }"; nft add rule ip t10 p ct state new counter accept
conntrack -F > /dev/null 2>&1
sysctl -qw net.netfilter.nf_conntrack_max=200
sysctl -n net.netfilter.nf_conntrack_max | sed 's/^/limit now: /'
python3 sink.py 4 6000 > /tmp/s10_sink.txt &
sleep 0.5
echo "--- 1. 400 UDP flows from 400 source ports, one packet each, over loopback"
python3 udpspray.py 127.0.0.1 6000 400
sleep 4
echo "--- 2. entries in the table and what the kernel counted"
conntrack -C | sed 's/^/entries: /'
conntrack -S 2> /dev/null | head -1 | grep -oE 'insert_failed=[0-9]+|drop=[0-9]+' | tr '\n' ' '; echo
echo "--- 3. what the receiver saw"
tail -1 /tmp/s10_sink.txt
echo "--- 4. the kernel log"
dmesg 2> /dev/null | grep -i 'table full' | tail -1 | sed 's/^\[[ 0-9.]*\] //'
```

```text
limit now: 200
--- 1. 400 UDP flows from 400 source ports, one packet each, over loopback
201 packets sent, 199 refused by the local stack {'Operation not permitted': 199}
--- 2. entries in the table and what the kernel counted
entries: 200
insert_failed=0 drop=210 drop=3
--- 3. what the receiver saw
port 6000: 201 packets, TOS seen by the application: 0x00
--- 4. the kernel log
nf_conntrack: nf_conntrack: table full, dropping packet
```

**Reading.** The table limit was set to **200** for the test (restored to **262,144**) and the host sent **400** UDP flows. **201** packets were sent and **199 were refused locally with `Operation not permitted`**, `conntrack -C` showed **200 entries**, `conntrack -S` showed `drop=210`, and `dmesg` said `table full, dropping packet`. The sink saw 201. The error text `EPERM` is the clue: `sendto` should not return a permission error for a datagram, and when it does on a host with a firewall, think conntrack. The default 262,144 entries fill in **26 s at 10,000 new flows a second** if nothing expires (`ans_d`); an entry takes about **256 bytes** (NET 09), so 120,000 live entries is **31 MB**. This run was on loopback in the host namespace because the limit cannot be set inside a namespace here.

**Fix and proof.** Raise `nf_conntrack_max` and the hash size (memory is the cost); shorten the timeouts for the traffic that fills it (UDP and half-open TCP); exempt traffic that needs no tracking with `notrack`; for a gateway, count flows in the design (section 4). Proof: `conntrack -C` is well below the max and `drop` stops growing.

### S11. A dead peer that is not noticed for 15 minutes

**Symptom.** A client keeps writing to a server that lost power; nothing fails for many minutes. Or a half-open connection stays in `ESTABLISHED` forever on an idle server.

```bash
# s11_deadpeer.sh: "the client hangs forever when the server host dies": a TCP connection does not notice a silent peer unless it sends (retransmission timeout) or probes (keepalive).
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap 'q_down; pkill -f lsn.py' EXIT
B python3 lsn.py 9300 40 &
sleep 0.5
A sysctl -qw net.ipv4.tcp_retries2=5
echo "--- 1. the sender writes 5 bytes after the path is cut; tcp_retries2 is 5 here (the default is 15)"
(sleep 1; B nft add table ip t; B nft "add chain ip t i { type filter hook input priority 0; }"; B nft add rule ip t i drop) &
A tcpdump -l -nn -tt -i va 'tcp port 9300 and tcp[tcpflags] & tcp-push != 0' > /tmp/s11.txt 2> /dev/null &
sleep 0.3
A python3 deadpeer.py 10.9.0.2 9300 data
sleep 0.3; pkill tcpdump
echo "sends of the 5 bytes (seconds after the first):"
awk '/Flags/ && $1 ~ /^[0-9.]+$/ {if (!t0) t0=$1; printf "%.1f ", $1-t0} END{print ""}' /tmp/s11.txt
echo "--- 2. the path is cut again and the client only waits, with keepalive: idle 1 s, interval 1 s, 3 probes"
B nft flush chain ip t i; sleep 1
(sleep 1; B nft add rule ip t i drop) &
A python3 deadpeer.py 10.9.0.2 9300 idle
```

```text
--- 1. the sender writes 5 bytes after the path is cut; tcp_retries2 is 5 here (the default is 15)
connected
Connection timed out after 13.4 s
sends of the 5 bytes (seconds after the first):
0.0 0.2 0.4 0.9 1.7 3.4 6.7
--- 2. the path is cut again and the client only waits, with keepalive: idle 1 s, interval 1 s, 3 probes
connected
Connection timed out after 2.6 s
```

**Reading.** There are two separate mechanisms. **Data in flight** is retransmitted with a doubling timeout until `tcp_retries2` tries are used up. With the value lowered to **5** the sender sent at **0.0, 0.2, 0.4, 0.9, 1.7, 3.4 and 6.7 s** and gave up with `ETIMEDOUT` at **13.4 s** (the arithmetic gives 12.6 s; the 0.8 s difference is the timer granularity, **inferred**). At the default of **15** the arithmetic gives **924.6 s, 15.4 minutes** (NET 03). A connection with **nothing in flight** has no retransmissions to give it away; only **keepalive** finds it. With idle 1 s, interval 1 s and 3 probes the client gave up **2.6 s** after the cut (at most idle plus probes, 4 s by `ans_e`, because the cut came part way through the idle time); the kernel defaults of 7,200, 75 and 9 give **7,875 s, 2.19 hours** (`ans_e`).

**Fix and proof.** Set an application level timeout and heartbeat (the dependable fix; `TCP_USER_TIMEOUT` is the socket level one, **from memory**); enable keepalive with realistic numbers on long idle connections. Proof: a cut cable shows up in the time you chose.

### S12. Latency under load: bufferbloat *(measured in NET 14, not rerun)*

**Symptom.** Pings are fine until someone uploads; then they jump by 100 ms or more, and everything on the link slows with the upload.

**Reading.** A bulk TCP flow fills the largest buffer on the path, and every other packet waits behind it. NET 14 measured it: a 10 Mbit link with a 1.5 MB buffer held a standing queue of **121 KB** and a round trip of **99 ms** against **0.036 ms** idle; Little's law (bytes queued divided by rate) gave **102 ms**; BBR held about **9 ms**. First command: `ping` the next hop while a transfer runs, and `tc -s qdisc show` for the backlog.

**Fix.** Smaller or smarter queues (CoDel, FQ-CoDel, which this kernel lacks and NET 14 modelled), shaping slightly below the link rate at the bottleneck, ECN. Proof: the loaded ping stays within a few milliseconds of the idle one.

### S13. Where are the packets dropped? *(NET 06, not rerun)*

**Symptom.** Counters say packets are lost; no one knows where.

**Reading.** The Linux receive path has many places to drop and each has its own counter. Read in order: the NIC (`ethtool -S` for `rx_dropped`, `rx_missed`, ring overruns), the driver queue and softirq (`/proc/net/softnet_stat`, third column for `time_squeeze` and second for drops), the protocol layer (`nstat`: `IpInDiscards`, `TcpExtListenDrops`, `UdpInErrors`, `UdpRcvbufErrors`) and the firewall (`nft` counters, `conntrack -S`). NET 06 measured the **drop reasons** that `dropwatch` and `perf trace -e skb:kfree_skb` print for each cause. First command: `nstat -az | grep -i drop` before and after, to see which moved.

**Fix.** Depends on the counter: larger socket buffers or a faster reader for `UdpRcvbufErrors`; more RX queues and RSS for a hot core; a better offload setting; a firewall rule.

### S14. A TLS handshake fails *(from memory, not run)*

**Symptom.** `curl` says `SSL certificate problem` or the connection resets right after the ClientHello.

**Reading.** Separate the layers with one command each: `openssl s_client -connect host:443 -servername host` shows the chain the server sent, its dates, and the verification result; a wrong `-servername` (SNI) returns the wrong certificate on a shared address. The usual causes, in order of frequency: an **expired** certificate; a **missing intermediate** certificate in the chain (works in a browser that caches intermediates, fails in `curl`); a **name mismatch**; a client **clock** that is wrong; a protocol or cipher mismatch (an old client against TLS 1.3 only); a middlebox that resets on a ClientHello it cannot parse or because of a size over the MTU (S2). A reset **immediately after** the ClientHello with small messages working and large ones failing is the MTU case, not a certificate. NET 08 measured the TLS 1.3 record sizes and handshake round trips.

**Fix.** Install the full chain, renew, fix the name or SNI, fix the clock.

### S15. One uplink is full while the others are idle: hash polarisation *(NET 11, not rerun)*

**Symptom.** An ECMP or LAG group shows one member at 100 % and the rest nearly idle; or two tiers of switches use the same hash and send all flows down the same link.

**Reading.** ECMP picks a member from a hash of header fields. A **few large flows** hash unevenly (the law of small numbers); **identical hash functions and seeds at two tiers** pick the same member twice, so the second tier sees a pre-selected subset (polarisation). NET 11 measured both and the fix: a different seed per tier, more entropy in the hash input (include the UDP source port, which VXLAN and RoCE choose from the inner flow), and **resilient hashing**, which moved **0 of 303** existing flows when a member was removed against **110 of 316** for a plain modulo hash. First command: per-member counters (`ip -s link`, `ethtool -S`, `nstat`), then `ip route get` with different ports to see which member a flow takes.

**Fix.** Seeds, hash inputs, flowlet switching or packet spraying where the fabric supports reordering (NET 12), and more flows through connection multiplexing choices.

---


## 3. The question bank

Answers are written to be **said aloud in 20 to 40 seconds**, then extended by the follow-up that is given in italics. A tag shows where a number came from: **[NET nn]** copied from that tutorial's measured output, **[run Sn]** measured in section 2, **[ans_x]** computed in section 5. Untagged answers are standard knowledge, not run.

### 3.1 Link layer and Ethernet (NET 01)

1. **What does a switch do with a frame?** It learns the source MAC against the ingress port, looks up the destination MAC and forwards out one port; an unknown unicast, a broadcast or a multicast is flooded to every port in the VLAN except the ingress. *Follow-up: with a loop the flooded frame circulates forever, which is why spanning tree exists; a broadcast storm made **290,810** packets in half a second [NET 01].*
2. **How big is a minimum Ethernet frame on the wire, and why does it matter?** 64 bytes of frame plus 8 of preamble and 20 with the inter-frame gap: **84 bytes**, which is **14.88 Mpps** at 10 Gbit/s [NET 01]. It is the worst case a forwarding engine must handle, so line-rate claims quote 64 byte packets.
3. **What is the MTU and what happens above it?** The largest payload a link carries: 1,500 for Ethernet. A frame of 1,514 passed and 1,515 failed [NET 01]. A router either fragments (IPv4, DF clear) or drops and sends ICMP (DF set); if the ICMP is lost you have S2 [run S2].
4. **What does ARP do and how does it fail?** It maps a next-hop IPv4 address to a MAC by broadcasting a request. It fails by a missing reply (the neighbour state shows `FAILED`), a stale entry after a MAC move (fixed by a gratuitous ARP), or poisoning by a host that answers for another's address.
5. **What is a VLAN and what is the tag?** A broadcast domain separated in software; 802.1Q inserts 4 bytes after the source MAC: a 12 bit VLAN ID and a 3 bit priority (PCP). A hand built tagged frame with DSCP 46 carried priority 5 in a mapping test [NET 14].
6. **Why spanning tree, and what replaced it in data centers?** To block redundant links so a bridged network has no loop. Data centers moved to routed fabrics (L3 to the top-of-rack, ECMP) so no link is blocked, with VXLAN where L2 adjacency is needed (NET 11).
7. **What is the difference between a hub, a switch and a router?** A hub repeats to all ports; a switch forwards by MAC within a broadcast domain; a router forwards by IP between them and decrements the TTL.
8. **What is a MAC table overflow and why is it a security issue?** An attacker fills the table with fake source addresses, the switch starts flooding unicast, and the attacker sees traffic. Port security (a limit of MACs per port) is the control.

### 3.2 IP and routing (NET 02, NET 11)

9. **How does a router pick a route?** The **longest prefix match** against the destination, then the lowest administrative distance and metric among equals. For `10.1.2.200` with `/0`, `/8`, `/16`, `/24` and `/25` routes the `/25` wins [ans_b].
10. **How is longest prefix match done fast?** In software a compressed trie (LC-trie in Linux); in hardware TCAM, or DIR-24-8 tables where a lookup is two memory reads: **8.4 ns** against **206 ns** for a trie at 500,000 routes [NET 02]. *Follow-up: the trade is memory and update cost against lookup time.*
11. **Subnet drill: how many hosts in 192.168.10.0/22?** The mask is 255.255.252.0; the network is **192.168.8.0** (not .10.0), the broadcast 192.168.11.255, **1,022 hosts** [ans_b, the NET 02 trap]. A `/31` has two usable addresses on a point to point link (RFC 3021).
12. **Can you aggregate 10.0.0.0/24 and 10.0.2.0/24?** Only into a **/22**, which also covers 10.0.1.0/24 and 10.0.3.0/24, so it is **not exact** [ans_b]. Aggregating non-adjacent blocks advertises addresses you do not own.
13. **What does TTL do?** Each router decrements it; at 0 the packet is dropped and an ICMP time exceeded returns. It stops loops and is how `traceroute` works. After two routers a TTL of 64 arrives as **62** [NET 02].
14. **What is in the IPv4 header checksum and does a router recompute it?** A 16 bit one's complement sum of the header words. A router updates it **incrementally** after a TTL change (RFC 1624) rather than summing again; the example header's checksum **0xb861** became **0xb961** with TTL 0x40 to 0x3f [ans_c].
15. **Fragmentation: who does it and what does it cost?** The sender (IPv4 routers too if DF is clear); the receiver reassembles, and one lost fragment loses the whole datagram. Modern stacks avoid it with path MTU discovery. IPv6 routers never fragment.
16. **What is path MTU discovery and how does it break?** The sender sets DF; a router with a smaller MTU returns ICMP "fragmentation needed" with its MTU; the sender caches it (**599 s** here) [run S2]. It breaks when a firewall drops the ICMP: a black hole. Fixes: allow the ICMP, clamp the MSS, or PLPMTUD.
17. **What is reverse path filtering?** A source address check: strict mode drops a packet whose source would not be routed out the arriving interface [run S3: counted in `IPReversePathFilter`]. It stops spoofing and breaks asymmetric routing.
18. **OSPF against BGP, in one breath?** OSPF is an IGP: link-state flooding, every router computes a shortest-path tree, fast convergence inside one domain. BGP is path vector between organisations, policy driven, scales to the Internet routing table, converges slowly. In a data center, BGP with a private ASN per rack is common (NET 11).
19. **What is ECMP and what is its main failure?** Equal cost multipath splits flows across next hops by a hash of the header. Failures: **polarisation** when tiers share a hash, and elephant flows landing on one link [S15]. Resilient hashing moved **0 of 303** flows when a member was removed against **110 of 316** [NET 11].
20. **What is a valley-free path?** A path that goes up then down the tiers and never down and up again; a route learnt from a lower tier and re-advertised upward creates a valley and a loop risk [NET 11].
21. **What is anycast?** The same address announced from many places; routing delivers each packet to the nearest. Used for DNS and CDNs; a route change mid-connection can send later packets to another site, so TCP over anycast needs care.
22. **How does NAT work and what does it cost?** A gateway rewrites the source address (and port) of outgoing packets and keeps a table to reverse it. It costs state (a conntrack entry of about **256 B** [NET 09]), breaks end to end addressing, and limits connections per external address to the port range [ans_d].
23. **IPv6 in three differences?** 128 bit addresses; no fragmentation by routers and no header checksum; neighbour discovery (ICMPv6) replaces ARP, so filtering ICMPv6 blindly breaks the network. *This sandbox has no IPv6, so nothing here is measured.*
24. **What is multicast and how is membership kept?** One sender, many receivers by a group address; hosts join with IGMP (IPv4) or MLD (IPv6) and routers build a tree (PIM). It is common in market data and in discovery protocols.

### 3.3 TCP and UDP (NET 03)

25. **Describe the handshake and what each side holds.** SYN, SYN-ACK, ACK. After the SYN the server holds a half-open entry in the SYN queue; after the ACK the connection moves to the **accept queue** until `accept()` [run S5]. SYN cookies let the server skip the state under a flood.
26. **Why does a SYN to a closed port differ from one to a filtered port?** The closed port answers RST at once (**0.000 s**); a filtered one gives silence and the client retries at 0, 1, 2, 3, 4, 5, 7 s [run S1].
27. **What is the TCP window and what limits throughput?** The sender may have `min(cwnd, receiver window)` bytes unacknowledged. Throughput is at most window / RTT: a 64 KB window gives **5.24 Mbit/s at 100 ms** [ans_a]. `ss -ti` says which applies: `rwnd_limited`, `cwnd_limited`, `sndbuf_limited` [run S8].
28. **What is the bandwidth delay product?** Rate times RTT, the bytes in flight needed to fill the pipe: **1.25 MB** for 100 Mbit at 100 ms [ans_a]. Buffers must be at least that for a single flow to reach line rate.
29. **Explain slow start and congestion avoidance.** The window starts at about 10 segments and doubles each RTT until loss or a threshold, then grows by about one segment per RTT; loss halves it (Reno) or reduces it by a factor of 0.7 (cubic). Moving 10 MB at 100 Mbit and 100 ms takes **1.40 s** against an ideal **0.90 s** [ans_a].
30. **Why does a small loss rate hurt TCP so much?** Every loss is read as congestion. The Mathis estimate is rate ≈ 1.22 × MSS / (RTT × √p): **1.4 Mbit/s at 100 ms and 1 % loss** [ans_a]. Measured at 0.036 ms RTT with cubic: **9,362** Mbit/s with no loss, **1,103** at 0.1 %, **160** at 1 % [run S9].
31. **Reno, cubic and BBR in a sentence each?** Reno: additive increase, halve on loss. Cubic: window grows as a cubic function of time since the last loss, the Linux default for years. BBR: models bottleneck bandwidth and minimum RTT, does not treat loss as the signal, and held a queue near 9 ms where loss-based flows held 99 ms [NET 14].
32. **What is the 40 ms stall?** Nagle holding a small write until the previous is acknowledged, against delayed ACK: **43.8 ms** mean for two writes against **0.05 ms** with `TCP_NODELAY` or one write [run S4, NET 03 41.8 ms].
33. **What are the retransmission timers?** The RTO is estimated from the smoothed RTT and its variance, with a floor of 200 ms on Linux, and doubles on each failure. The give-up time is set by `tcp_retries2`: **924.6 s** at the default 15 [ans_e, NET 03].
34. **Fast retransmit and SACK?** Three duplicate ACKs trigger a retransmission without waiting for the timer; SACK reports which ranges arrived so only the holes are resent. Tail loss (no later packets to produce duplicates) falls back to the timer, which is why tail loss is expensive.
35. **What is TIME_WAIT for and what does it cost?** The side that closes first holds the connection for 2×MSL (**59 s** measured) [NET 03] to absorb late duplicates and to retransmit the final ACK. It ties up a four-tuple: **50 ports gave 50 connections** [run S6]; the default range is **28,232** ports, **471** connections a second to one destination [ans_d].
36. **How do you fix `Cannot assign requested address`?** Reuse connections; spread over more destinations or source addresses; widen the range; `tcp_tw_reuse` (only for sockets older than 1 s) [run S6].
37. **What is a zero window and a probe?** The receiver advertises 0; the sender sends a one byte probe with a growing interval (**0.35, 0.80, 1.69, 3.45 s** measured) [NET 03] until the window reopens. A throughput limited by a small window with **87.4 % rwnd_limited** is the slow reader [run S8].
38. **How does a dead peer get noticed?** Retransmissions when data is in flight (**13.4 s** measured with `tcp_retries2=5`, 15.4 minutes at the default); keepalive when idle (**2.6 s** with idle 1, interval 1, 3 probes; **2.19 hours** at the defaults) [run S11, ans_e]. Use an application heartbeat or `TCP_USER_TIMEOUT`.
39. **UDP against TCP: when UDP?** When the application wants control: low latency with its own recovery (QUIC, DNS, RTP, games), multicast, or very many short exchanges. UDP gives no ordering, no retransmission, no congestion control; a closed port returns ICMP, seen as `connection refused` on the next call [run S1].
40. **What is QUIC and why does it exist?** A reliable, encrypted, multiplexed transport over UDP: no head-of-line blocking between streams, a faster handshake (one round trip, zero on resumption) and connection IDs that survive an address change. It moves congestion control into user space.
41. **What is head-of-line blocking?** One lost segment holds back every byte behind it in a TCP stream, even bytes for unrelated requests multiplexed on it. HTTP/2 over TCP has it; HTTP/3 over QUIC does not [NET 08: 20 requests in **0.11 s** on HTTP/2 against **2.04 s** in the comparison run].
42. **Explain SYN flood and the defence.** The attacker sends SYNs and never completes; the half-open queue fills. **SYN cookies** encode the state in the sequence number so no entry is held until a valid ACK returns.
43. **What is TCP Fast Open, and TCP segmentation offload?** TFO carries data in the SYN on a repeat connection (saves a round trip). TSO lets the stack hand the NIC a large buffer to cut into segments: skbs of **49,775 B** at **15.0 Gbit/s** [NET 06]; GSO does the same in software.

### 3.4 Sockets and I/O (NET 04, NET 05)

44. **Walk through a TCP server's system calls.** `socket`, `bind`, `listen`, then a loop of `accept`, `read`/`write`, `close`. `listen`'s backlog bounds the accept queue; if `accept` is slow the queue fills: **Recv-Q 5, Send-Q 4**, **42 overflows** [run S5].
45. **What does `read` returning less than requested mean?** Nothing special: TCP is a byte stream with no message boundaries. A correct program loops until it has all it needs (`read_full`) and frames messages itself (length prefix) [NET 04].
46. **Why do you need `SO_REUSEADDR`?** So a restarted server can bind a port that has sockets in `TIME_WAIT`. It does not allow two listeners; `SO_REUSEPORT` does, with the kernel spreading connections across them.
47. **What is SIGPIPE?** Writing to a connection the peer closed raises it and kills the process (exit status **141**) unless ignored or `MSG_NOSIGNAL` is used [NET 04].
48. **Blocking, non-blocking, `select`, `poll`, `epoll`: when each?** Blocking with a thread per connection is simple and limited by threads. `select` and `poll` scan every descriptor each call; `epoll` returns only ready ones: with **4,000 idle connections** `epoll` took **35 µs** against **1,193 µs** for `poll` [NET 05].
49. **Level against edge triggered epoll?** Level reports while the condition holds; edge reports once on the change, so you must read until `EAGAIN`. With threads, `EPOLLONESHOT` stops two threads handling one socket: **2,965 of 3,000** events handled correctly against **0** without it in the test [NET 05].
50. **What is `io_uring` and why?** A pair of shared ring buffers (submission and completion) so many I/O operations are submitted and reaped with few or no system calls; it also covers files, not just sockets (NET 05).
51. **What does a slow reader do to the sender?** The receive buffer fills, the window closes, the sender idles: **0.76 Mbit/s** with the link idle [run S8].
52. **How do you find which process owns a socket and its queues?** `ss -tanp` for state and owner; `ss -ltn` for accept queue (Recv-Q is the current length, Send-Q the limit); `ss -ti` for window, RTT, retransmissions and limits.
53. **What is `SO_SNDBUF` autotuning and when to set it by hand?** Linux grows buffers within `tcp_rmem` and `tcp_wmem`. Setting them by hand turns autotuning off for that socket, so do it only when the BDP is known and the defaults cap throughput on a long path.

### 3.5 The Linux network stack and zero copy (NET 06, NET 07)

54. **Trace a received packet through Linux.** NIC DMA into a ring buffer, interrupt, NAPI poll in a softirq, `sk_buff` allocated (**224 B** of metadata [NET 06]), GRO merging, netfilter and routing, the socket queue, then `read`. Each stage has a drop counter [S13].
55. **What is NAPI?** Interrupt then polling: after the first interrupt the driver disables it and polls in batches until the ring is empty, which avoids an interrupt per packet at high rates.
56. **What do RSS, RPS and XPS do?** RSS spreads flows to hardware queues (one per core) by a hash; RPS does it in software; XPS picks the transmit queue per core. They keep a flow on one core and spread load.
57. **What are GRO and GSO/TSO?** GRO merges received packets of one flow into one large skb to cut per-packet cost; GSO and TSO do the reverse on transmit: **49,775 B** per skb [NET 06].
58. **How does XDP differ from the normal path?** An eBPF program runs in the driver before an skb exists and can drop, pass, redirect or transmit; AF_XDP hands frames to user space without the stack (NET 07). It is for packet filtering, DDoS drops and load balancing at the highest rates.
59. **What is zero copy and what does `sendfile` save?** Moving data between file and socket without a pass through user space: `sendfile` took **0.33 s per GB** against **0.66** for `read` plus `write` [NET 07]. `MSG_ZEROCOPY` and `splice` are the relatives.
60. **How would you find where packets are dropped in the kernel?** Counters first (`nstat`, `ip -s link`, `ethtool -S`, `softnet_stat`), then `perf trace -e skb:kfree_skb` or `dropwatch` for the **drop reason** and the call site [NET 06, S13].
61. **What is a qdisc?** The queueing discipline between the stack and the driver. Default `pfifo_fast`/`fq_codel`/`fq`; others shape, schedule or meter (NET 14).
62. **What is DPDK against the kernel stack?** DPDK polls NIC queues in user space on dedicated cores with huge pages, skipping interrupts and the kernel; it gives the best per-core rate at the cost of cores, a user-space stack, and losing the kernel's tools.

### 3.6 Application protocols and security (NET 08, NET 09)

63. **What happens when you type a URL?** Resolve the name (stub, resolver, recursion), open TCP (or QUIC), negotiate TLS, send the request, receive and render. The first request pays DNS, TCP and TLS round trips; later ones reuse the connection [NET 08].
64. **DNS: why UDP, and when TCP?** A query is **33 bytes** and the reply **65** [NET 08], one datagram each; TCP is used when the answer is truncated (large DNSSEC answers) and for zone transfers. A dead first nameserver costs **6.16 s** [run S7].
65. **HTTP/1.1, HTTP/2 and HTTP/3?** 1.1: one request at a time per connection (pipelining unused). 2: multiplexed binary streams over TCP, **0.11 s** against **2.04 s** for 20 requests [NET 08], with TCP head-of-line blocking. 3: the same over QUIC.
66. **TLS 1.3 handshake in one round trip?** The client sends key shares in the ClientHello; the server replies with its share, certificate and finished; application data can follow after one round trip (zero with resumption, at the cost of replay protection). The certificate chain and SNI cause most failures [S14].
67. **Stateless against stateful firewall?** Stateless matches each packet on its headers (cost about **10 ns per rule** in a linear ACL [NET 09]); stateful tracks connections so the reply is allowed because the request was seen: it needs a conntrack entry (**256 B**), and fails when the table fills [run S10].
68. **What happens when conntrack is full?** New flows are dropped and `dmesg` says `table full, dropping packet`; a local sender can see `EPERM` [run S10]. Size it as new flows per second times entry lifetime [ans_d].
69. **Why can an IPS be evaded by segmentation?** A signature split across two TCP segments or two fragments is invisible to per-packet matching; the device must reassemble the stream the way the end host does, which costs memory and CPU [NET 09].
70. **What does a CDN or load balancer change about TLS?** It terminates TLS at the edge and may re-encrypt to the origin; the client sees the edge's certificate. Client IP is lost unless PROXY protocol or `X-Forwarded-For` carries it.

### 3.7 Data center, RDMA, load balancing and queueing (NET 11 to 14)

71. **Why leaf-spine, and how does it scale?** Every leaf connects to every spine, so any two servers are two or four hops apart with many equal cost paths; capacity grows by adding spines and bisection bandwidth follows the oversubscription ratio (NET 11).
72. **What is incast?** Many senders answering one receiver at once overflow the receiver's port buffer: a fan-in problem, handled by ECN and DCQCN, pacing, or smaller fan-out (NET 12).
73. **PFC and what can go wrong?** Priority flow control sends a pause frame (**60 B**) per priority to make a class lossless. Pausing spreads back through the fabric (head-of-line blocking, pause storms, deadlock on a cyclic dependency) [NET 12].
74. **What does RoCE need compared with TCP?** Reliable delivery in hardware with little loss tolerance, so a lossless or near-lossless fabric (PFC and ECN), and congestion control in the NIC (DCQCN) (NET 12).
75. **Hashing for load balancing: why not `hash mod N`?** Changing N remaps nearly everything: **89.8 %** of flows moved [NET 13]. Consistent hashing or Maglev moves about 1/N; Maglev gives a near-even spread with a lookup table (NET 13).
76. **How should a load balancer health-check?** Active probes at an interval with a failure threshold and a success threshold; the detection time is interval × failures, and a too short window flaps [NET 13]. Passive checks (observed errors) catch what a probe misses.
77. **L4 against L7 load balancing?** L4 forwards by the five tuple, never reads the payload, very fast and often stateless (DSR, ECMP). L7 terminates the connection, reads the request and can route on a path or header, and costs CPU and TLS work.
78. **Little's law and why it matters for queues?** `L = λ × W`: bytes queued equal rate times delay. A 125,000 byte buffer in front of a 10 Mbit link is 100 ms when full [NET 14]. Measured at load 0.9, backlog **4.07** frames against Little's **4.05**.
79. **Why does delay explode near full load?** For random arrivals the wait is proportional to `1 / (1 − ρ)`: mean **0.62, 2.13, 4.70, 10.35 ms** at loads 0.5, 0.8, 0.9, 0.95, and the p99 is **61 ms** at 0.95 [NET 14].
80. **Policing against shaping?** A shaper queues the excess and delays it (smooth, adds latency: **+30 ms** round trip); a policer drops it (no added delay, but loss that TCP reacts to: **686 to 983** retransmissions in the test) [NET 14].
81. **What is bufferbloat and the fix?** An oversized buffer holding a standing queue: **99 ms** against **0.036 ms** idle [NET 14]. Fix with AQM (CoDel), fair queueing, a shaper just under the link rate, or BBR.
82. **What does a token bucket guarantee?** Never more than `b + r × t` bytes in any interval: **15,000** bytes in the first millisecond and **1,015,000** by one second with burst 15,000 and 8 Mbit [NET 14].

### 3.8 Debugging, in one line each

83. **Connect times out.** Capture both ends for the SYN; refused means a reset came back [S1].
84. **Large transfers hang, small work.** Black-hole MTU: `ping -M do -s 1472` [S2].
85. **Replies missing, capture shows requests.** `rp_filter` or a missing return route [S3].
86. **Fixed 40 ms or 200 ms.** Nagle and delayed ACK; RTO [S4].
87. **Some connects hang under load.** `ss -ltn` Recv-Q against Send-Q; `ListenOverflows` [S5].
88. **EADDRNOTAVAIL.** TIME_WAIT and the port range [S6].
89. **Slow start of a program.** DNS timeouts; `time getent hosts` [S7].
90. **Slow with an idle link.** `ss -ti` limits; the reader [S8].

(Questions 83 to 90 repeat section 2 on purpose: they are the one line versions to say first, before the detail.)

---


## 4. Design style questions

A design question is answered in the same order every time: **requirements and numbers**, **the data path**, **state and what it costs**, **failure and how it is detected**, **what you would measure**. Say the numbers early; they decide the design.

### 4.1 Design a load balancer

**Numbers first.** Peak connections per second, concurrent connections, request rate and size, whether the payload must be read (L7) or not (L4), the availability target. A single CPU core forwards on the order of a few million packets a second in the kernel and more with XDP (NET 07); 10 Gbit/s of 64 byte packets is **14.88 Mpps** [NET 01].

**Data path.** A two tier design is the usual answer. **Tier 1, L4**: routers announce a virtual IP by anycast or ECMP and spread flows by a five tuple hash across L4 balancers; the balancers pick a backend by **consistent hashing or Maglev**, so that **adding or removing a backend moves about 1/N of flows** and not the **89.8 %** that `hash mod N` moved [NET 13], and forward by encapsulation or **direct server return**, so replies skip the balancer. **Tier 2, L7** (if needed): terminate TLS, read the request, route by path or header, retry, and enforce limits.

**State.** An L4 balancer can be nearly stateless if its hash is stable; a connection table pins flows that must survive a backend change. Size the table as for conntrack: new flows a second times lifetime times entry size [ans_d]. An L7 balancer holds two sockets per request.

**Failure.** Health checks with an interval, a failure threshold and a success threshold: detection time is interval × failures, and a window that is too short flaps [NET 13]. When an L4 balancer dies, ECMP rehashes flows to others; **resilient hashing** keeps existing flows in place (**0 of 303** moved against **110 of 316**) [NET 11]. Drain a backend before removal by stopping new flows and waiting for old ones.

**Measure.** Connections, per-backend load and its spread, health-check flaps, `ListenOverflows` on the backends [run S5], conntrack use [run S10], p99 latency.

### 4.2 Design a firewall data path

**Numbers.** Throughput in packets a second at the smallest packet, rule count, new flows a second, concurrent flows.

**Data path.** Classify each packet: a **fast path** for packets of an established flow (one hash lookup of the five tuple in the connection table, apply the stored verdict and any NAT rewrite), and a **slow path** for the first packet of a flow (walk the rule set, create the entry). A linear ACL costs about **10 ns per rule** [NET 09], so 1,000 rules is 10 µs per new flow: acceptable for the slow path, not for every packet, which is why established traffic skips it. Compile the rules into sets, hash tables or a trie (as `nft` does with sets) so the cost stops growing with the rule count.

**State.** Entries of about **256 B** [NET 09]; **1,000 new flows a second with a 120 s lifetime is 120,000 entries and 31 MB, 10,000 a second is 307 MB** [ans_d]. Size the table, set short timeouts for half-open and UDP flows, and alarm on the limit: when it fills, new flows are dropped [run S10]. The established TCP timeout of **432,000 s** is five days; an idle flow occupies a slot that long unless the timeout is lowered [NET 09].

**Deep inspection** (IPS, proxies) must **reassemble** the stream, or a signature split across segments evades it [NET 09]; this is where the cost per byte rises.

**Failure.** A pair in active-standby needs the connection table synchronised, or every flow resets at failover. **Measure** the drop counters by reason, table occupancy, the new flow rate and the rule hit counts.

### 4.3 Design a packet router (lookup and forwarding)

**Numbers.** Ports and speeds, route table size (a full Internet table is around a million IPv4 prefixes, **from memory**), packets a second.

**Data path.** Receive, validate the header (version, length, checksum), **look up the longest prefix**, decrement the TTL and **update the checksum incrementally** [ans_c], find the next hop's MAC (the neighbour table), rewrite the Ethernet header, queue on the output port, send. The lookup is the heart: a **trie** takes **206 ns** at 500,000 routes and a **DIR-24-8** table **8.4 ns** [NET 02]; hardware uses TCAM or algorithmic tables. DIR-24-8 spends memory (a table of 2²⁴ entries) and makes updates costly for long prefixes.

**Control plane against data plane.** Routing protocols (BGP, OSPF) run on a CPU and install the table; forwarding runs on the ASIC or in the kernel fast path. A packet that needs the CPU (TTL expired, options, ARP miss) is **punted** and rate limited, or the control plane can be starved by traffic.

**Queueing.** Each output port needs a queue and a scheduler. A buffer holds delay = bytes ÷ rate [NET 14]; strict priority starves lower classes (**5.3 s** with no reply in the test), so a guarantee needs a shaper or weighted scheduling.

**Failure and updates.** Fast reroute and equal cost paths for link failure; atomic table update so a packet never sees half a change. **Measure** lookups a second, drops by reason, punt rate, queue depth and the convergence time after a failure.

### 4.4 Design a NAT gateway

**Numbers.** New connections a second, concurrent connections, the number of public addresses. One public address and one destination give **28,232** ports: **471 new connections a second** (60 s `TIME_WAIT`), **941** with two destinations, **7,529** with sixteen [ans_d]. Conntrack memory is entries × **256 B**.

**Design.** Source NAT with a port allocator: choose a port per flow from a pool, keep the mapping in the connection table, and rewrite and update the checksums both ways. Allocate ports per source host in blocks to log which user had which port (a legal requirement in some regions). Add public addresses to raise the limit, because the limit is per address and destination. Short timeouts for UDP (DNS) and half-open TCP.

**Failure.** Table full drops new flows [run S10]; port exhaustion gives `EADDRNOTAVAIL` on the client behind it [run S6]. ICMP errors must be translated too (a path MTU message carries an inner header). Synchronise state to a standby.

### 4.5 Design a rate limiter

**Choice.** A **token bucket** is the usual building block: rate `r` and burst `b`; no more than **`b + r × t` bytes** (or requests) in any interval [NET 14]. Per key (user, IP, API key) in a distributed limiter: either a central store (Redis, a counter with an expiry), exact but a hop and a bottleneck, or local buckets with a share of the global rate, cheap but approximate.

**Shaper or policer.** A shaper delays (smooth, adds latency); a policer drops (cheap, no queue, but a policer with too small a burst hurts TCP: modelled at 40 ms a 1,500 byte burst held Reno to **0.30 Mbit/s** and a 25 KB burst reached **4.18 of 5** [NET 14]). Give a burst of at least two frames, more for TCP [NET 14].

**Answer to the client.** An HTTP 429 with `Retry-After`, or a drop with ECN where the transport reacts. **Measure** the rejected fraction per key and the limiter's own latency.

### 4.6 Debug the 200 ms stall

*The classic open question: "A service calls another and one call in a hundred takes about 200 ms more than the rest. Walk me through it."*

1. **Bound it.** Which hop, which pairs, since when, only under load? A **fixed size** is a fingerprint (section 1.4): 200 ms is the minimum RTO; **40 ms** would be Nagle and delayed ACK [run S4]; 1 s or 3 s a SYN retransmission [run S1]; 5 s a resolver [run S7].
2. **Decide loss against stall.** 200 ms means a **retransmission timeout**: a packet was lost and no duplicate ACKs rescued it (a tail loss: the last packets of a request, with nothing after them to trigger fast retransmit). Check `nstat` for `TCPTimeouts` and `TCPLossProbes`, and `ss -ti` for `retrans`.
3. **Find where the loss is.** Capture on both ends: a segment sent twice 200 ms apart with the first copy missing at the receiver is a loss on the path; a segment that arrived and was not acknowledged is the receiver's drop (`ListenOverflows` [run S5], a full socket buffer [run S8], conntrack [run S10]). Read counters along the path: `ip -s link`, `ethtool -S`, switch ports, `softnet_stat` [S13].
4. **Hypothesise and test.** If the loss correlates with bursts, look for microbursts and incast in the queue (NET 12, NET 14); if with a rule or table, conntrack or a firewall; if with one path, ECMP polarisation [S15].
5. **Fix and prove.** Remove the cause of loss; reduce the effect with a lower RTO floor (a tail loss probe, `TCP_USER_TIMEOUT` is a different knob), a retry with hedging at the application. Prove it with the p99 and the `TCPTimeouts` rate falling.

### 4.7 Common mistakes

- **Saying "network is slow" without a shape.** Latency, throughput and stalls have different trees; a stall of a fixed size names its cause.
- **Blaming the network first.** `ss -ti` often shows `rwnd_limited` or an application limit [run S8].
- **Confusing refused with filtered.** RST in **0.000 s** against silence for 8 s or more [run S1].
- **Filtering all ICMP.** It breaks path MTU discovery [run S2] and, for IPv6, neighbour discovery.
- **Turning off `rp_filter` as a fix.** The route is the fault [run S3].
- **Quoting TCP throughput without the RTT.** Window ÷ RTT: **5.24 Mbit/s** at 100 ms with 64 KB [ans_a].
- **Sizing conntrack by guess.** Flows a second × lifetime × **256 B** [ans_d].
- **Using the mean for latency.** At load 0.95 the mean is **10 ms** and the p99 **61 ms** [NET 14].
- **`hash mod N` for balancing.** **89.8 %** of flows move when N changes [NET 13].
- **Forgetting that a closed connection holds its port for 60 s** [run S6].

---

## 5. Problems, and the answers

**Problem A. Windows, slow start and loss.** (a) What throughput does a 64 KB window give at 1, 20, 100 and 200 ms? (b) How long to move 10 MB over 100 Mbit with an initial window of 10 segments, for the same four RTTs? (c) What rate does Mathis give for one flow at 100 ms and 1 % loss? *Solve: window ÷ RTT; count the doublings, 10 segments of 1,460 B at first; 1.22 × MSS ÷ (RTT × √p).*

```python
# ans_a.py: what limits a TCP transfer: the window, slow start, or loss. Window limit = window / RTT; Mathis rate = 1.22 x MSS / (RTT x sqrt(p)).
import math
MSS = 1448
print("rate limit of a 64 KB window, and the bandwidth delay product, by round trip")
print("%-8s %-18s %-16s %-16s" % ("RTT", "64 KB window", "BDP at 100 Mbit", "BDP at 1 Gbit"))
for rtt in (0.001, 0.02, 0.1, 0.2):
    print("%-8s %-18s %-16s %-16s" % ("%g ms" % (rtt * 1000), "%.2f Mbit/s" % (65535 * 8 / rtt / 1e6), "%d B" % (100e6 * rtt / 8), "%d B" % (1e9 * rtt / 8)))
print()
print("time to move 10 MB over 100 Mbit with an initial window of 10 segments that doubles each round trip (no loss), and the ideal time")
print("%-8s %-12s %-14s %-12s" % ("RTT", "rounds", "time", "ideal"))
for rtt in (0.001, 0.02, 0.1, 0.2):
    left, cwnd, t = 10e6, 10 * MSS, 0.0; cap = max(100e6 * rtt / 8, MSS)
    while left > 0:
        w = min(cwnd, cap); left -= w; t += rtt if left > 0 or w >= cap else (rtt / 2 + w * 8 / 100e6); cwnd *= 2
    rounds = round(t / rtt)
    print("%-8s %-12d %-14s %-12s" % ("%g ms" % (rtt * 1000), rounds, "%.2f s" % t, "%.2f s" % (10e6 * 8 / 100e6 + rtt)))
print()
print("Mathis rate of one Reno flow by loss and round trip (the formula holds for windows of more than a few segments)")
print("%-8s %-14s %-14s %-14s" % ("RTT", "p = 0.0001", "p = 0.001", "p = 0.01"))
for rtt in (0.001, 0.02, 0.1):
    print("%-8s " % ("%g ms" % (rtt * 1000)) + " ".join("%-14s" % ("%.1f Mbit/s" % (1.22 * MSS * 8 / (rtt * math.sqrt(p)) / 1e6)) for p in (1e-4, 1e-3, 1e-2)))
```

```text
rate limit of a 64 KB window, and the bandwidth delay product, by round trip
RTT      64 KB window       BDP at 100 Mbit  BDP at 1 Gbit
1 ms     524.28 Mbit/s      12500 B          125000 B
20 ms    26.21 Mbit/s       250000 B         2500000 B
100 ms   5.24 Mbit/s        1250000 B        12500000 B
200 ms   2.62 Mbit/s        2500000 B        25000000 B

time to move 10 MB over 100 Mbit with an initial window of 10 segments that doubles each round trip (no loss), and the ideal time
RTT      rounds       time           ideal
1 ms     800          0.80 s         0.80 s
20 ms    44           0.88 s         0.82 s
100 ms   14           1.40 s         0.90 s
200 ms   11           2.20 s         1.00 s

Mathis rate of one Reno flow by loss and round trip (the formula holds for windows of more than a few segments)
RTT      p = 0.0001     p = 0.001      p = 0.01
1 ms     1413.2 Mbit/s  446.9 Mbit/s   141.3 Mbit/s
20 ms    70.7 Mbit/s    22.3 Mbit/s    7.1 Mbit/s
100 ms   14.1 Mbit/s    4.5 Mbit/s     1.4 Mbit/s
```

The window limit falls from **524.28** to **2.62 Mbit/s** as the RTT goes from 1 to 200 ms. Slow start costs nothing at 1 ms (**0.80 s**, the ideal) and doubles the time at 200 ms (**2.20 s** against **1.00 s**), because a short transfer is spent in the ramp. One Reno flow at 100 ms and 1 % loss gets **1.4 Mbit/s** whatever the link.

**Problem B. Subnets, longest prefix match, aggregation.** Give the network, first host, broadcast and host count for seven prefixes; pick the route for six destinations from a table with `/0`, `/8`, `/16`, `/24`, `/25` and `/32` entries; find the smallest covering prefix for three sets and say whether it is exact. *Solve: the block size is 2^(32 − length); align the network address to it; the longest matching length wins; a covering prefix is exact only if the blocks fill it.*

```python
# ans_b.py: subnetting and longest prefix match drills, checked with the ipaddress module.
import ipaddress as ip
print("%-20s %-16s %-16s %-16s %-8s %s" % ("network", "mask", "first host", "broadcast", "hosts", "note"))
for c in ("192.168.10.0/22", "10.0.5.77/27", "172.16.0.0/12", "203.0.113.130/26", "10.1.2.3/31", "10.9.9.9/32", "192.168.1.200/25"):
    n = ip.ip_network(c, strict=False); h = list(n.hosts())
    print("%-20s %-16s %-16s %-16s %-8d %s" % (n, n.netmask, h[0] if h else "-", n.broadcast_address, len(h), "point to point (RFC 3021)" if n.prefixlen == 31 else "one address" if n.prefixlen == 32 else ""))
print()
table = {"0.0.0.0/0": "default via R1", "10.0.0.0/8": "R2", "10.1.0.0/16": "R3", "10.1.2.0/24": "R4", "10.1.2.128/25": "R5", "10.1.2.5/32": "host route"}
print("longest prefix match against:", ", ".join("%s (%s)" % kv for kv in table.items()))
for a in ("10.1.2.5", "10.1.2.200", "10.1.2.77", "10.1.9.9", "10.200.0.1", "8.8.8.8"):
    best = max((ip.ip_network(p) for p in table if ip.ip_address(a) in ip.ip_network(p)), key=lambda n: n.prefixlen)
    print("%-12s -> %-16s %s" % (a, best, table[str(best)]))
print()
print("aggregation: the smallest single prefix that covers each set")
for s in (("10.0.0.0/24", "10.0.1.0/24"), ("10.0.0.0/24", "10.0.2.0/24"), ("192.168.0.0/25", "192.168.0.128/26", "192.168.0.192/26")):
    nets = [ip.ip_network(x) for x in s]; sup = nets[0]
    while not all(n.subnet_of(sup) for n in nets): sup = sup.supernet()
    print("%-52s -> %-16s exact: %s" % (" + ".join(s), sup, list(ip.collapse_addresses(nets)) == [sup]))
```

```text
network              mask             first host       broadcast        hosts    note
192.168.8.0/22       255.255.252.0    192.168.8.1      192.168.11.255   1022
10.0.5.64/27         255.255.255.224  10.0.5.65        10.0.5.95        30
172.16.0.0/12        255.240.0.0      172.16.0.1       172.31.255.255   1048574
203.0.113.128/26     255.255.255.192  203.0.113.129    203.0.113.191    62
10.1.2.2/31          255.255.255.254  10.1.2.2         10.1.2.3         2        point to point (RFC 3021)
10.9.9.9/32          255.255.255.255  10.9.9.9         10.9.9.9         1        one address
192.168.1.128/25     255.255.255.128  192.168.1.129    192.168.1.255    126

longest prefix match against: 0.0.0.0/0 (default via R1), 10.0.0.0/8 (R2), 10.1.0.0/16 (R3), 10.1.2.0/24 (R4), 10.1.2.128/25 (R5), 10.1.2.5/32 (host route)
10.1.2.5     -> 10.1.2.5/32      host route
10.1.2.200   -> 10.1.2.128/25    R5
10.1.2.77    -> 10.1.2.0/24      R4
10.1.9.9     -> 10.1.0.0/16      R3
10.200.0.1   -> 10.0.0.0/8       R2
8.8.8.8      -> 0.0.0.0/0        default via R1

aggregation: the smallest single prefix that covers each set
10.0.0.0/24 + 10.0.1.0/24                            -> 10.0.0.0/23      exact: True
10.0.0.0/24 + 10.0.2.0/24                            -> 10.0.0.0/22      exact: False
192.168.0.0/25 + 192.168.0.128/26 + 192.168.0.192/26 -> 192.168.0.0/24   exact: True
```

`192.168.10.0/22` is the trap: it is **192.168.8.0/22**, **1,022 hosts**. `10.0.0.0/24 + 10.0.2.0/24` aggregates only to a `/22` that also covers the two blocks in between, so it is **not exact**; the same for any pair that is not adjacent and aligned.

**Problem C. The Internet checksum.** Compute the IPv4 header checksum for the header `4500 0073 0000 4000 4011 0000 c0a8 0001 c0a8 00c7`; show that the receiver gets `0xffff`; show the incremental update after a TTL change. *Solve: add the 16 bit words, fold the carry, complement.*

```python
# ans_c.py: the Internet checksum of an IPv4 header, step by step, and the check that the receiver does.
import struct
def words(b): return struct.unpack("!%dH" % (len(b) // 2), b)
def csum(b):
    s = sum(words(b))
    while s >> 16: s = (s & 0xffff) + (s >> 16)
    return s
hdr = bytes.fromhex("4500 0073 0000 4000 4011 0000 c0a8 0001 c0a8 00c7".replace(" ", ""))
print("header with the checksum field zero:", " ".join("%04x" % w for w in words(hdr)))
raw = sum(words(hdr)); print("sum of the words: 0x%x" % raw)
fold = (raw & 0xffff) + (raw >> 16); print("fold the carry: 0x%04x" % fold)
c = ~fold & 0xffff; print("one's complement: 0x%04x" % c)
full = hdr[:10] + struct.pack("!H", c) + hdr[12:]
print("receiver adds all ten words with the checksum in place and gets 0x%04x, so the header is accepted" % csum(full))
bad = bytearray(full); bad[8] -= 1
print("the same header with the TTL byte changed by one: sum folds to 0x%04x (not 0xffff), so it is rejected" % csum(bytes(bad)))
print("a router that decrements the TTL updates by a difference: old checksum 0x%04x, TTL 0x40 to 0x3f, new checksum 0x%04x (full recompute 0x%04x)" % (c, (c + 0x0100) % 0xffff if c + 0x0100 <= 0xffff else (c + 0x0100) % 0x10000 + 1, ~csum(hdr[:8] + bytes([0x3f]) + hdr[9:10] + b"\0\0" + hdr[12:]) & 0xffff))
```

```text
header with the checksum field zero: 4500 0073 0000 4000 4011 0000 c0a8 0001 c0a8 00c7
sum of the words: 0x2479c
fold the carry: 0x479e
one's complement: 0xb861
receiver adds all ten words with the checksum in place and gets 0xffff, so the header is accepted
the same header with the TTL byte changed by one: sum folds to 0xfeff (not 0xffff), so it is rejected
a router that decrements the TTL updates by a difference: old checksum 0xb861, TTL 0x40 to 0x3f, new checksum 0xb961 (full recompute 0xb961)
```

**Problem D. Ports and conntrack sizing.** (a) How many new connections a second can a client make to one destination with the default port range and a 60 s `TIME_WAIT`? With 2, 4 and 16 destinations? (b) How much memory and how many entries for 1,000, 10,000 and 50,000 new flows a second with a 120 s lifetime? When does the default table fill at 10,000 a second? *Solve: ports ÷ 60 s; flows × lifetime × 256 B; 262,144 ÷ 10,000.*

```python
# ans_d.py: sizing arithmetic for a busy client and for a connection tracker: ephemeral ports, TIME_WAIT, and conntrack entries and memory.
lo, hi = 32768, 60999; ports = hi - lo + 1; tw = 60
print("default ephemeral range %d to %d: %d ports; TIME_WAIT %d s (from memory for the 60)" % (lo, hi, ports, tw))
print("new connections per second to one (address, port) before ports run out: %.0f" % (ports / tw))
for dsts in (2, 4, 16): print("  spread over %2d destination (address, port) pairs: %.0f per second" % (dsts, dsts * ports / tw))
print("  with a range of 1024 to 65535: %.0f per second to one pair" % ((65535 - 1024 + 1) / tw))
print()
print("conntrack entries = new flows per second x how long an entry lives; memory = entries x entry size (256 B per entry as measured in NET 09)")
print("%-18s %-14s %-14s %-12s" % ("new flows/s", "lifetime", "entries", "memory"))
for rate, life in ((1000, 120), (10000, 120), (50000, 120), (50000, 432000), (200000, 30)):
    n = rate * life; print("%-18d %-14s %-14d %-12s" % (rate, "%d s" % life, n, "%.0f MB" % (n * 256 / 1e6)))
print()
print("the default limit of 262,144 entries is reached after %.0f s at 10,000 new flows per second if nothing expires; the 432,000 s row is an established entry that is never refreshed and never closed (the NET 09 timeout), which no table survives" % (262144 / 10000))
```

```text
default ephemeral range 32768 to 60999: 28232 ports; TIME_WAIT 60 s (from memory for the 60)
new connections per second to one (address, port) before ports run out: 471
  spread over  2 destination (address, port) pairs: 941 per second
  spread over  4 destination (address, port) pairs: 1882 per second
  spread over 16 destination (address, port) pairs: 7529 per second
  with a range of 1024 to 65535: 1075 per second to one pair

conntrack entries = new flows per second x how long an entry lives; memory = entries x entry size (256 B per entry as measured in NET 09)
new flows/s        lifetime       entries        memory
1000               120 s          120000         31 MB
10000              120 s          1200000        307 MB
50000              120 s          6000000        1536 MB
50000              432000 s       21600000000    5529600 MB
200000             30 s           6000000        1536 MB

the default limit of 262,144 entries is reached after 26 s at 10,000 new flows per second if nothing expires; the 432,000 s row is an established entry that is never refreshed and never closed (the NET 09 timeout), which no table survives
```

The default port range gives **28,232 ports** and so **471 new connections a second** to one destination, **7,529** to sixteen. At **10,000 new flows a second the default 262,144 entry table fills in 26 s** if nothing expires. The 432,000 s row shows why a long timeout cannot be left on for a busy gateway.

**Problem E. Timeout arithmetic.** (a) How long until a sender gives up for `tcp_retries2` of 5, 8 and 15, with a 0.2 s first RTO that doubles up to 120 s? (b) How long until a silent peer is noticed with the default keepalive? (c) When does a SYN to a dropped port give up? *Solve: sum the doubling waits; idle + interval × probes; the SYN schedule.*

```python
# ans_e.py: how long TCP takes to give up: the retransmission timeout schedule (RTO starts at the 200 ms minimum, doubles, capped at 120 s) and keepalive.
def give_up(retries, rto=0.2, cap=120.0):
    t, sched = 0.0, []
    for i in range(retries + 1):
        sched.append(rto); t += rto; rto = min(rto * 2, cap)
    return t, sched
for r in (5, 8, 15):
    t, s = give_up(r)
    print("tcp_retries2 = %-3d first waits %s ... last %s; the connection fails after %.1f s (%.1f min)" % (r, ", ".join("%g" % x for x in s[:5]), "%g" % s[-1], t, t / 60))
print("measured in s11 with tcp_retries2 = 5: failed after 13.4 s; the arithmetic gives %.1f s" % give_up(5)[0])
print()
idle, intvl, cnt = 7200, 75, 9
print("keepalive defaults (idle %d s, interval %d s, %d probes): a silent peer is noticed after %d s (%.2f hours); idle 1, interval 1, 3 probes (s11): %d s" % (idle, intvl, cnt, idle + intvl * cnt, (idle + intvl * cnt) / 3600, 1 + 3 * 1))
print()
print("SYN timeouts: measured in s1, SYN sent at 0, 1, 2, 3, 4, 5 and 7 s (tcp_syn_linear_timeouts = 4 on this kernel); the classic doubling schedule 1, 2, 4, 8, 16, 32, 64 gives sends at 0, 1, 3, 7, 15, 31, 63 s and gives up at %d s" % (1 + 2 + 4 + 8 + 16 + 32 + 64))
```

```text
tcp_retries2 = 5   first waits 0.2, 0.4, 0.8, 1.6, 3.2 ... last 6.4; the connection fails after 12.6 s (0.2 min)
tcp_retries2 = 8   first waits 0.2, 0.4, 0.8, 1.6, 3.2 ... last 51.2; the connection fails after 102.2 s (1.7 min)
tcp_retries2 = 15  first waits 0.2, 0.4, 0.8, 1.6, 3.2 ... last 120; the connection fails after 924.6 s (15.4 min)
measured in s11 with tcp_retries2 = 5: failed after 13.4 s; the arithmetic gives 12.6 s

keepalive defaults (idle 7200 s, interval 75 s, 9 probes): a silent peer is noticed after 7875 s (2.19 hours); idle 1, interval 1, 3 probes (s11): 4 s

SYN timeouts: measured in s1, SYN sent at 0, 1, 2, 3, 4, 5 and 7 s (tcp_syn_linear_timeouts = 4 on this kernel); the classic doubling schedule 1, 2, 4, 8, 16, 32, 64 gives sends at 0, 1, 3, 7, 15, 31, 63 s and gives up at 127 s
```

The default `tcp_retries2` of 15 is **924.6 s, 15.4 minutes**, and the default keepalive **7,875 s, 2.19 hours**. The measured **13.4 s** against **12.6 s** computed for 5 retries is a 6 % difference, timer granularity (inferred).

---

## 6. Cheat sheet (one page)

```text
FIRST QUESTIONS     both ends? one way? always or sometimes? fast fail or after a wait? small ok, large not?
FAIL AT ONCE        refused (RST)  -> no listener / REJECT / wrong bind        ss -ltn
FAIL AFTER A WAIT   SYN-SENT       -> dropped; capture BOTH ends               8 s, SYN at 0,1,2,3,4,5,7 s
LARGE HANGS         MTU black hole -> ping -M do -s 1472; allow ICMP / MSS clamp
NO REPLY            rp_filter / no return route   nstat IPReversePathFilter
STALL SIZES         40 ms Nagle+delayed ACK | 200 ms RTO | 1,3 s SYN | 5 s resolver | 15 min dead peer
ACCEPT QUEUE        ss -ltn  Recv-Q vs Send-Q;  nstat ListenOverflows
EADDRNOTAVAIL       ports x destinations; TIME_WAIT 60 s; 28,232 ports -> 471 conn/s per dest
CONNTRACK FULL      EPERM on sendto; dmesg "table full"; entries = flows/s x lifetime x 256 B
SLOW, LINK IDLE     ss -ti: rwnd_limited / cwnd_limited / sndbuf_limited
LOSS                Mathis 1.22 x MSS / (RTT x sqrt(p));  100 ms + 1% -> 1.4 Mbit/s
WINDOW LIMIT        window / RTT;  64 KB @100 ms = 5.24 Mbit/s;  BDP = rate x RTT
QUEUES              delay = bytes / rate (Little);  wait ~ 1/(1-load): 0.62/2.13/4.70/10.35 ms @0.5/0.8/0.9/0.95
BUCKET              never more than b + r*t
GIVE-UP TIMES       retries2=15 -> 924.6 s;  keepalive 7200+75x9 = 7875 s
LB                  mod N moves 89.8%;  consistent/Maglev ~1/N;  resilient hash 0 of 303
FIREWALL            ACL ~10 ns/rule; conntrack 256 B; est. timeout 432,000 s
COUNTERS            ip -s link | ethtool -S | nstat -az | ss -s | conntrack -S | softnet_stat
```

## Sources

**Read or run in this session:** the kernel behaviour and every number tagged **[run Sn]** or **[ans_x]** come from the scripts in `prog/` and their output in `v/`. The **[NET nn]** numbers are copied from those tutorials' measured output and were not rerun. **From memory** (marked where used): the `tcp_syn_linear_timeouts` motivation, `TCP_USER_TIMEOUT`, the removal of `tcp_tw_recycle`, the Mathis constant, RFC 1624 and RFC 3021 details, the size of the full Internet table, and the TLS, QUIC, IPv6 and hardware material. No RFC was re-read for this tutorial.
