# NET 14: Queueing, QoS and traffic management: buffers, bufferbloat, shapers and policers, AQM, schedulers and DSCP

*Part of the [networking plan](00_plan.md) · You need: [NET 03](03_tcp_udp_and_transport.md) (congestion control, the window and `ss -ti`), [NET 06](06_linux_network_stack.md) (where the qdisc sits in the transmit path, GSO), [NET 12](12_rdma_roce_and_lossless_fabrics.md) (ECN marking, PFC and incast, which are queueing problems in a fabric).*

Every packet that crosses a link waits somewhere, and what that wait costs depends on four decisions: **how much** buffer there is, **what is dropped** when it fills, **who goes first** when several flows are waiting, and **who is allowed to send at all** when a rate is contracted. Everything in this tutorial (token buckets, RED, CoDel, DRR, HTB, DSCP) is one of those four decisions.

"What is bufferbloat, and how do CoDel and fair queueing fix it?" is a standard question in networking, infrastructure and Wi-Fi interviews, and "policing against shaping" is the first follow-up in any router or firewall loop. A weak answer lists algorithm names. A strong answer connects five things: the **arithmetic** (Little's law: a queue is a delay), the **mechanism** (tail drop, RED, CoDel, ECN, per-flow queues), the **scheduler** (strict priority, DRR, WFQ, HTB and what each does to a flow that is not sending), the **classification** (DSCP, ECN bits, 802.1p, and who is trusted to set them) and the **failure modes** (starvation, a policer with too little burst, a standing queue).

By the end you can:

- predict queueing delay from load, and explain why a full buffer is a delay and not a safety margin;
- explain a token bucket, and what rate and burst each control;
- choose between a shaper and a policer, and say what each does to TCP;
- explain bufferbloat, buffer sizing (the bandwidth delay product and its refinement for many flows), and why a standing queue is bad;
- explain RED, ECN, CoDel, FQ-CoDel and CAKE, and what each fixes;
- compare FIFO, strict priority, round robin, DRR, WFQ and HTB by fairness, isolation and cost;
- read and set DSCP, ECN and 802.1p markings, and say where to trust them;
- build a QoS policy for an uplink and say how to check it;
- say where queues live in a host, a router, a datacenter switch and a Wi-Fi access point.

---

## 1. Why queues exist, and what a queue costs

A link sends one packet at a time at a fixed rate. Packets arrive at random times. Whenever one arrives while another is being sent, it **waits**, and that waiting space is the queue. Queues are not a bug: they absorb **bursts** so that a link can run busy at a modest average load. The cost is **delay**, and the delay grows sharply as load approaches the link's capacity.

### 1.1 Little's law

For any stable queue: **average number waiting = arrival rate x average time waiting**. Backlog and delay are the same quantity seen two ways. A router that has 100 KB queued on a 10 Mbit/s link is adding **80 ms** to every packet that arrives behind it, because that is how long it takes to send 100 KB. The practical form is the one used all through this tutorial:

```
delay added by a queue  =  bytes queued  x  8  /  link rate
```

### 1.2 How much a queue costs as load rises

For packets of a fixed size arriving randomly (Poisson), the mean wait in an M/D/1 queue is `rho / (2 (1 - rho)) x service time`, where `rho` is the load (offered rate over link rate) and the service time is the time to send one packet. The factor `1 / (1 - rho)` is the point: the wait does not grow linearly, it **explodes near full load**.

```python
def md1_wait(load, service_s):                       # mean wait in an M/D/1 queue: rho / (2 (1 - rho)) x service time
    return load / (2 * (1 - load)) * service_s

service = 1250 * 8 / 10e6                            # a 1,250 byte frame on a 10 Mbit link: 1 ms
for load in (0.5, 0.8, 0.9, 0.95):
    print(f"load {load:.2f}: mean wait {md1_wait(load, service) * 1e3:5.2f} ms, queue {load * load / (2 * (1 - load)):4.1f} frames (Little: rate x wait)")

def standing_delay_ms(buffer_bytes, rate_bps):       # a full buffer adds this much delay to every packet behind it
    return buffer_bytes * 8 / rate_bps * 1e3

print(f"1.5 MB buffer on 10 Mbit: {standing_delay_ms(1.5e6, 10e6):.0f} ms; 33-frame buffer: {standing_delay_ms(33 * 1500, 10e6):.0f} ms")
```

At 50 percent load the mean wait is half a packet time; at 80 percent it is 2 ms; at 90 percent 4.5 ms; at 95 percent 9.5 ms, and the queue holds about 9 frames (a measured token bucket with Poisson arrivals gave 0.6, 2.1, 4.7 and 10.4 ms at the same loads, and a backlog of 4.07 frames at 0.9 against 4.05 from the formula, in earlier runs). The lesson for operations: **a link run at 95 percent load has a queue even when nothing is misbehaving**, and interactive traffic sharing it feels that wait. Real traffic is burstier than Poisson, so real queues are worse.

The second half of the lesson is the last line of the snippet: a **1.5 MB buffer in front of a 10 Mbit link can hold 1.2 seconds of data.** Whether that is allowed to fill is the subject of section 5.

---

## 2. Where the queues are

A packet from an application to the wire passes through several queues. Knowing which one is the bottleneck decides what you can tune.

| Place | The queue | Who controls it |
|---|---|---|
| Application and kernel | socket send buffer; TCP congestion window and TCP small queues (TSQ) | `SO_SNDBUF`, TCP autotuning, `tcp_limit_output_bytes` |
| Host egress | the **qdisc** (queueing discipline) attached to the interface | `tc qdisc` |
| Driver and NIC | the transmit ring; **byte queue limits (BQL)** cap how many bytes sit there so the qdisc, which is smart, does the queueing instead of the ring, which is not | driver, `ethtool -G` |
| Router or switch | per output port queues, in a shared packet memory | switch configuration |
| Wi-Fi | per station and per access category queues in the AP (and in the driver) | the wireless stack |
| Modem or access link | usually a large dumb FIFO | the ISP's device, which you cannot tune; you shape in front of it |

The most important practical idea: **the queue should be where the bottleneck is, and it should be one you control.** The usual home-router problem is a bottleneck (the modem, the Wi-Fi link) with a big dumb FIFO that you cannot manage. The fix is to **shape slightly below the bottleneck rate** on a device you do control, so the queue forms where a smart discipline (FQ-CoDel, CAKE) runs.

### 2.1 The Linux qdisc layer

Every interface has a root qdisc. **Classless** disciplines (`pfifo`, `fq_codel`, `tbf`, `cake`) treat all traffic alike or manage their own flows. **Classful** disciplines (`htb`, `prio`, `drr`, `hfsc`) contain classes; a **classifier** (a `tc filter`, or the packet's priority or mark) puts each packet in a class, and each class can hold its own qdisc. Many distributions default to `fq_codel` as the root qdisc; older kernels used `pfifo_fast`, three priority bands chosen by the TOS byte.

```bash
tc qdisc show dev eth0                               # what is attached
tc -s qdisc show dev eth0                            # with counters: sent, dropped, backlog, requeues
tc -s class show dev eth0                            # per class counters for classful disciplines
```

The counters are the first things to read: `dropped` and `overlimits` (a shaper holding packets back) and `backlog` (what is queued right now).

---

## 3. The token bucket

The token bucket is the primitive behind almost every rate limiter. A bucket holds up to **burst** bytes of tokens, and tokens arrive at **rate** bytes per second. A packet may be sent if the bucket holds at least its size in tokens, and sending it removes them. An idle link therefore saves up credit (up to the burst size), and a busy one is limited to the rate.

```python
class TokenBucket:
    """Rate in bytes/s, burst in bytes. allow() says whether a packet may leave now."""
    def __init__(self, rate, burst):
        self.rate, self.burst, self.tokens, self.t = rate, burst, burst, 0.0
    def allow(self, now, size):
        self.tokens = min(self.burst, self.tokens + (now - self.t) * self.rate)
        self.t = now
        if self.tokens >= size:
            self.tokens -= size
            return True
        return False

tb = TokenBucket(rate=1e6, burst=15000)              # 8 Mbit/s, 15 KB burst, starts full (an idle link)
sent = 0
for ms in range(1000):                               # offer 1,500 byte packets as fast as the loop runs: 10 per ms
    for _ in range(10):
        if tb.allow(ms / 1000, 1500):
            sent += 1500
    if ms in (0, 99, 999):
        print(f"after {ms + 1:4d} ms: {sent:9,d} bytes left; burst + rate x t = {15000 + 1e6 * (ms + 1) / 1000:,.0f}")
```

What leaves after an idle period is **burst plus rate times time**, to within one packet. In a real `tbf` test at 8 Mbit/s with a 15,000 byte burst, 15,000 bytes left in the first millisecond and 1,015,000 by one second (earlier runs). The two parameters answer two different questions: **rate** is the long-run average you allow, **burst** is how much may exceed it in a short moment.

### 3.1 How much burst

Burst is not decoration. Packets are indivisible, so a bucket smaller than one packet can never send it; a bucket close to one packet makes the sender stop and start. TCP also sends in bursts (a window of packets back to back, and with GSO segments of up to 64 KB). A bucket with too little burst **under-delivers**: in a test at 100 Mbit/s, a burst of 1,600 bytes reached only 88 Mbit/s of the 95.6 that fits, a burst of 3,000 bytes reached 95.2, and 15,000 reached 95.6 (earlier runs). A reasonable minimum for a shaper is one to a few MTUs plus the kernel timer granularity (`rate / HZ`).

For a **policer** the burst must be larger, for a reason covered next.

---

## 4. Policing and shaping

Both use a token bucket. They differ in what they do to a packet that does not conform.

| | **Shaper** | **Policer** |
|---|---|---|
| Non-conforming packet | **queued** until tokens arrive (or dropped if the queue is full) | **dropped** (or re-marked to a lower class) |
| Output | smooth, at the rate | bursty, with gaps |
| Delay | adds queueing delay | none |
| Where | egress, where you own the queue | ingress or the edge, where you cannot queue |
| Effect on TCP | the sender sees rising RTT and few losses | the sender sees loss in bursts and no warning |
| Memory | needs a queue | needs only a counter |

Why the difference matters for TCP: TCP finds the available rate by probing until it loses a packet. Through a **shaper**, the excess waits in a queue; delay rises, TCP slows when the queue overflows or when delay-based control notices, and losses are few. Through a **policer**, there is no queue to absorb the sender's window bursts, so any burst that exceeds the bucket is dropped immediately, and TCP's loss recovery goes round and round.

The measured version, one TCP flow held to 5 Mbit/s (cubic, earlier runs): the shaper gave 4.71 Mbit/s with 32 retransmissions and about 30 ms of added round trip; a policer on the receiving side gave 5.07 to 5.24 Mbit/s but **686 to 983 retransmissions** and no added delay. Throughput is similar; the cost moved from delay to loss. A policer applied on the sender's own output hook gave zero retransmissions, because the stack saw a local drop and reacted before the packet left. And in the model at a 40 ms round trip, a policer with a 1,500 byte burst held Reno to 0.3 Mbit/s of a 5 Mbit/s contract while one with a 25 KB burst reached 4.2.

### 4.1 Why a policer needs a bandwidth delay product of burst

A TCP sender with window `W` sends up to `W` bytes back to back per round trip. A policer that cannot absorb that bursts drops the tail of every window. A policer's burst should therefore be about the **rate times the round trip time** (the bandwidth delay product, BDP) of the paths it polices: at 5 Mbit/s and 40 ms that is 25 KB, which is exactly where the model recovered. Policers with the default burst of a few packets are the usual reason "my 100 Mbit/s contract only delivers 20".

### 4.2 The Linux tools

```bash
dev=va

# 1. a shaper: queue the excess and release it at the contracted rate
tc qdisc replace dev $dev root tbf rate 5mbit burst 15000 latency 50ms

# 2. a three class uplink with HTB: voice, interactive, everything else
tc qdisc replace dev $dev root handle 1: htb default 30
tc class add dev $dev parent 1: classid 1:1 htb rate 10mbit
tc class add dev $dev parent 1:1 classid 1:10 htb rate 1mbit ceil 2mbit prio 0     # voice: small, capped, best priority
tc class add dev $dev parent 1:1 classid 1:20 htb rate 4mbit ceil 10mbit prio 1    # interactive
tc class add dev $dev parent 1:1 classid 1:30 htb rate 5mbit ceil 10mbit prio 2    # the rest
tc filter add dev $dev parent 1: protocol ip prio 1 u32 match ip dsfield 0xb8 0xfc flowid 1:10   # DSCP EF
tc filter add dev $dev parent 1: protocol ip prio 1 u32 match ip dsfield 0x48 0xfc flowid 1:20   # DSCP AF21
tc -s class show dev $dev | grep -E '^class'

# 3. shape traffic arriving on an interface by redirecting it through an ifb device
ip link add ifb0 type ifb && ip link set ifb0 up
tc qdisc add dev $dev handle ffff: ingress
tc filter add dev $dev parent ffff: protocol all u32 match u32 0 0 action mirred egress redirect dev ifb0
tc qdisc add dev ifb0 root tbf rate 5mbit burst 15000 latency 50ms
```

```
table inet qos {
  chain set_dscp {                              # set DSCP on locally generated traffic
    type route hook output priority mangle; policy accept;
    udp dport 5060 ip dscp set ef
    tcp dport 22 ip dscp set af21
  }
  chain policer {                               # a policer: drop what exceeds the rate, queue nothing
    type filter hook input priority 0; policy accept;
    iifname "vb" limit rate over 625 kbytes/second burst 15000 bytes drop
  }
}
```

The first block is a shaper (`tbf`), a hierarchy (`htb`) with classes picked by DSCP, and **ingress shaping**: Linux cannot queue on ingress, so incoming packets are redirected to an **ifb** (intermediate functional block) device whose egress queue can shape them. The nftables rules mark traffic and police arrivals. One caveat from earlier runs: the nftables `limit` token bucket delivered 5 to 8 percent more than its nominal rate, so test the real rate and do not trust the arithmetic.

---

## 5. Buffer sizing and bufferbloat

### 5.1 Bufferbloat

TCP (the loss-based kinds, Reno and CUBIC) keeps increasing its window until a packet is dropped. Given a **huge buffer**, nothing is dropped until the buffer is full, so the flow fills it and **holds it full**: a **standing queue** that adds its whole delay to every packet, for every flow sharing the link. That is bufferbloat. The buffer is not a safety margin: it is latency that buys no extra throughput.

Measured, one bulk TCP flow over a 10 Mbit/s link with a 1.5 MB buffer (earlier runs): CUBIC and Reno held a standing queue of about 121 KB and a round trip of **99 ms** against 0.036 ms idle on the same veth pair; Little's law (backlog divided by rate) gave 102 ms. BBR, which paces to the bottleneck rate and does not fill buffers, held about 9 ms. The standing queue was set by the **window the receiver allowed**, not by the buffer size: with a huge buffer the queue stops growing when the receive window is full. A video call or an SSH session on that link sees 100 ms of extra latency whenever someone uploads.

The symptoms are the signature: a link that is fast but **feels slow under load**, latency that grows when a transfer starts and shrinks when it ends, and `ping` that does not match the "speed test".

### 5.2 How much buffer

The classical rule (Villamizar and Song) is one **bandwidth delay product**: `buffer = rate x RTT`. With a buffer of that size, TCP's window halving on a drop leaves the pipe exactly full, so the link stays busy. A smaller buffer means the link idles after each backoff; a bigger one is pure delay.

The refinement for **many flows** (Appenzeller, McKeown and others) is that flows are not synchronised, their windows average out, and the buffer needed falls to `BDP / sqrt(N)` for N flows. In the model at 10 Mbit/s and 100 ms, 36 flows kept the link full with 14 frames of buffer where one flow needed 83 (earlier runs). Backbone routers with a million flows can run with very small buffers by this logic; a home router with one or two flows cannot.

Two warnings. First, the rules are about **utilisation**; they say nothing about **delay**, and a buffer of one BDP still doubles the RTT under a bulk flow. Second, in a datacenter the problem is different: short, synchronised bursts (incast) need buffer to absorb them, which is why switch buffers and ECN thresholds are tuned together (NET 12).

The practical answer for the edge is not a better number but a **smarter queue**: active queue management.

---

## 6. Active queue management

**Tail drop** (drop what arrives when the buffer is full) tells the sender about congestion only after the queue is full, and tends to hit all flows at once, which makes them back off together and idle the link (**global synchronisation**). AQM drops or marks packets **early**, before the buffer is full, to keep the queue short and to spread the signal.

### 6.1 RED and ECN

**RED** (random early detection) tracks an average queue length. Below a minimum threshold nothing is dropped; between the minimum and a maximum threshold the drop probability rises linearly to `maxp`; above the maximum every packet is dropped. Random drops hit flows in proportion to their share, and desynchronise them. RED works, but it is **hard to tune** (the right thresholds depend on the link rate and RTT) and an untuned RED can be worse than tail drop, which is why it was little used on the edge.

**ECN** (explicit congestion notification, RFC 3168) lets the router **mark** a packet (set the IP header's ECN bits to CE, congestion experienced) instead of dropping it, and the receiver echoes the mark to the sender, which reduces its window as if it had seen a loss. Marking avoids the loss, the retransmission and the delay of recovery. It needs the sender to declare ECN capability (ECT in the IP header), and it needs the AQM to mark. In the model, RED with ECN cut drops from 202 to 3 for the same delay. DCQCN (NET 12) is this idea taken to rate control.

### 6.2 CoDel

CoDel (controlled delay, RFC 8289) fixes RED's tuning problem by measuring the **right thing**: not queue length but **sojourn time**, how long each packet actually waited. Two parameters have defaults that work for nearly every link: a **target** of 5 ms, and an **interval** of 100 ms (about a worst-case RTT). If the minimum sojourn time stays above the target for a whole interval, a standing queue exists, and CoDel starts dropping (or marking), then drops **faster** each time (`interval / sqrt(count)`) until the delay falls below the target.

```python
import math
TARGET, INTERVAL = 0.005, 0.100                      # RFC 8289 defaults: 5 ms, 100 ms

class CoDel:
    def __init__(self):
        self.first_above, self.dropping, self.count, self.drop_next = 0.0, False, 0, 0.0
    def should_drop(self, now, sojourn):             # called for each packet as it leaves the queue
        if sojourn < TARGET:                         # the queue drained below the target: reset
            self.first_above, self.dropping = 0.0, False
            return False
        if self.first_above == 0.0:                  # first time above target: wait one interval before acting
            self.first_above = now + INTERVAL
            return False
        if now < self.first_above:
            return False
        if not self.dropping:                        # above target for a whole interval: start dropping
            self.dropping, self.count = True, 1
            self.drop_next = now + INTERVAL / math.sqrt(self.count)
            return True
        if now >= self.drop_next:                    # keep dropping, faster each time: interval / sqrt(count)
            self.count += 1
            self.drop_next += INTERVAL / math.sqrt(self.count)
            return True
        return False

c, drops = CoDel(), []
for i in range(1, 1001):                             # a standing queue: every packet waited 20 ms, 1 packet per ms
    t = i / 1000
    if c.should_drop(t, 0.020):
        drops.append(t)
print("first drops at (s):", [f"{d:.3f}" for d in drops[:6]])
print("gaps between drops (ms):", [f"{(b - a) * 1e3:.0f}" for a, b in zip(drops, drops[1:6])], "= 100 / sqrt(n) for n = 1, 2, 3, ...")
```

This is a simplified version of the RFC's state machine (the real one also keeps its drop rate across brief recoveries and ignores a queue holding less than one MTU), but it shows the control law: the first drop comes **one interval after the queue goes bad**, and the gaps shrink as 100, 71, 58, 50, 45 ms. The sender backs off, the sojourn time falls below 5 ms, and CoDel stops. Because TCP's throughput as a function of loss is known (the Mathis formula, throughput proportional to 1/(RTT x sqrt(p))), the drop rate that holds a link full rises with the number of flows, and CoDel's rising rate supplies it.

### 6.3 FQ-CoDel and CAKE

CoDel limits delay but does not isolate flows: a thin interactive flow still shares one queue with bulk flows. **FQ-CoDel** (RFC 8290) hashes each flow to its own queue, serves them by **deficit round robin** (section 7) with a priority for flows that have just started ("new flow" boost), and runs CoDel on each queue. A bulk flow gets its own long queue, kept short by CoDel, and a sparse flow (a DNS query, a game packet, a TCP handshake) goes to the front. **CAKE** bundles a shaper, FQ-CoDel-like flow isolation, per-host fairness, DSCP tiers and overhead compensation into one discipline for edge links, which is why it is the common answer on home gateways.

```bash
# reference commands, not run here (this kernel lacks fq_codel and cake)
tc qdisc replace dev eth0 root fq_codel
tc qdisc replace dev eth0 root cake bandwidth 90mbit      # shape just below the real link rate
```

### 6.4 The same link under eleven disciplines

All the above on one link in a model (10 Mbit/s, 40 ms round trip, two bulk Reno flows, a probe packet every 100 ms, and short 20-frame transfers). These are model results (earlier runs), best read as a comparison between disciplines:

| Queue | Probe round trip | Short transfer | Notes |
|---|---|---|---|
| Tail drop, 1,000 frames | 1,096 ms | 2,210 ms | bufferbloat |
| Tail drop, 100 frames | 128 ms | 405 ms | |
| Tail drop, one BDP (33 frames) | 64 ms | 259 ms | |
| Tail drop, 10 frames | 44 ms | 200 ms | link only 93 percent busy |
| RED, with or without ECN | 53 ms | 127 to 129 ms | ECN cut drops from 202 to 3 |
| CoDel, with or without ECN | 47 ms | 119 to 126 ms | best utilisation, 98 percent |
| DRR with tail drop | 41 ms | 114 ms | isolated, but the bulk queue is 1.26 s |
| **FQ-CoDel** | **41 ms** | **114 ms** | bulk queue 2.5 ms, link 95 to 96 percent busy |

Read it in groups. A long tail drop queue is delay for everyone. A short one trades delay for idle capacity. AQM (RED, CoDel) holds the delay down while keeping the link busy. **Per-flow queueing** (DRR) gives the probe and the short transfers almost the bare 40 ms of propagation, because they never wait behind the bulk flows, but without AQM the bulk flows' own queue is a second long. FQ-CoDel does both.

---

## 7. Scheduling: who goes first

When several flows or classes are queued, a **scheduler** decides which packet leaves next. The choice trades isolation, fairness, guarantees and cost.

### 7.1 FIFO and strict priority

**FIFO** serves in arrival order. It is not fair: a flow that sends more gets more of the link, and a flood fills the queue and delays everyone. **Strict priority** always serves the highest non-empty class first. It gives the top class minimal delay and, if that class never ends, **starves** everyone below. In a test, a 12 Mbit/s flood into a 10 Mbit/s link meant a probe in its own priority class saw 0.033 ms where a probe behind the flood saw 70 ms; in `pfifo_fast` the lowest band got no reply for 5.3 seconds (earlier runs). Strict priority is safe only for a small, **rate-limited** class (voice), never for a class an application can fill.

### 7.2 Round robin, deficit round robin and WFQ

**Round robin** gives each queue a turn, one packet per turn. It is fair in packets, not bytes: a flow with 1,500 byte frames takes ten times the bandwidth of one with 150 byte frames. **Deficit round robin** (DRR) fixes this by giving each queue a **quantum** of credit per visit and sending packets while the credit covers them, keeping the remainder (the deficit) for the next turn. It is fair in bytes, constant time per packet, and is what FQ-CoDel uses.

```python
from collections import deque

def drr(flows, quantum=1500, rounds=1000):
    """flows: list of (frame_size, backlog) kept always backlogged. Returns bytes served per flow."""
    queues = [deque([size] * 10) for size, _ in flows]
    deficit, served = [0] * len(flows), [0] * len(flows)
    for _ in range(rounds):
        for i, q in enumerate(queues):
            deficit[i] += quantum                      # each visit adds one quantum of credit
            while q and q[0] <= deficit[i]:
                size = q.popleft()
                deficit[i] -= size
                served[i] += size
                q.append(flows[i][0])                  # the source always has another frame
    return served

for name, served in (("DRR", drr([(1500, 1), (750, 1), (150, 1)])),):
    total = sum(served)
    print(name, "share by bytes:", [f"{s / total:.1%}" for s in served])

def round_robin_by_packet(sizes, rounds=1000):          # one packet per flow per round, whatever its size
    served = [s * rounds for s in sizes]
    return [f"{x / sum(served):.1%}" for x in served]
print("round robin by packet:", round_robin_by_packet([1500, 750, 150]))
```

Three always-backlogged flows with 1,500, 750 and 150 byte frames get **62.5, 31.2 and 6.2 percent** from round robin by packet, and **33.3 percent each** from DRR. (In the model with three 6 Mbit/s sources, FIFO gave 2.9, 3.3 and 3.8 Mbit/s, round robin by packet 6.2, 3.2 and 0.6, DRR 3.3 each.) The quantum should be at least one MTU, or a frame takes several rounds to accumulate credit. The worst wait for the first packet of a new flow is about one quantum per other active flow: with 100 flows, a quantum of 1,514 bytes and a 10 Mbit/s link, 121 ms, but 1.2 ms at 1 Gbit/s, which is why DRR is fine at high speeds and awkward on slow links with many flows.

**Weighted fair queueing** (WFQ) approximates the ideal bit-by-bit round robin by computing a **finish time** for each packet and sending in finish-time order, with weights. It is the tightest in delay and fairness, at `O(log N)` per packet for the sorted order, which is why hardware often uses DRR or weighted round robin instead.

### 7.3 HTB: shares with a hierarchy

**HTB** (hierarchical token bucket) is the Linux tool for dividing a link among classes. Each class has a **rate** (guaranteed), a **ceil** (the most it may borrow to), and a **prio** (who gets spare bandwidth first). The sharing rule: every class first gets `min(demand, rate)`; leftover capacity is then offered to classes still wanting more, **lowest prio number first**, shared among equal priorities in proportion to rate, never beyond `ceil`. Measured with three classes on 10 Mbit/s (earlier runs): guaranteed 2, 2, 2 with the first class at the better priority and all three loaded gave 6.0, 2.0, 2.0 Mbit/s; the same with the first class idle gave 0, 5, 5; guarantees of 1, 3, 1 at equal priority gave 2, 6, 2. HTB lets you promise a minimum without wasting capacity when a class is idle, which a fixed partition cannot.

### 7.4 What each gives

| Scheduler | Isolates latency | Fair in bytes | Guarantees a rate | Starves | Cost |
|---|---|---|---|---|---|
| FIFO | no | no (by offer) | no | no | constant |
| Strict priority | the top band only | no | the top band | the lowest band | constant, few bands |
| Round robin | yes | no (by packet) | no | no | constant |
| DRR | yes (within a round) | yes | by weight | no | constant, quantum at least the MTU |
| WFQ | yes, best | yes | by weight | no | `O(log N)` |
| HTB | per class | per class by rate | yes | only lower prio above its guarantee | a tree walk and a lock |

The interview pattern: for each, say **what it protects and what it risks**. Priority protects latency and risks starvation; round robin protects flows and risks unfairness by size; DRR fixes that; HTB adds guarantees and borrowing.

---

## 8. Classification and marking

A scheduler needs to know which class a packet belongs to. Classification uses the 5-tuple, the interface, a firewall mark, or a **marking carried in the packet** so every hop can classify cheaply.

| Field | Where | Bits | Use |
|---|---|---|---|
| **DSCP** | IP header (the top 6 bits of the old TOS byte) | 64 values | per-hop behaviour class across routers |
| **ECN** | IP header (the low 2 bits of the same byte) | 00 not-ECT, 10 ECT(0), 01 ECT(1), 11 CE | congestion marking (section 6) |
| **802.1p / PCP** | the 802.1Q VLAN tag | 3 bits (8 priorities) | class on an Ethernet link |
| **WMM access category** | Wi-Fi QoS header (TID, user priority) | 4 categories | Wi-Fi channel access priority |

The common DSCP values: **EF** (46, TOS byte 0xB8) for voice, with low delay and loss and a strict rate limit; **AF** classes (for example AF21 = 18, TOS 0x48) with a drop precedence inside each class; **CS** classes (CS1 = 8 for scavenger or background, CS6 = 48 and CS7 for network control); **default** (0) for best effort. Because the old TOS byte is DSCP in the top 6 bits and ECN in the low 2, a DSCP of EF with ECT(0) is TOS 0xBA, and tools that print the whole byte confuse people: **mask the low two bits** (`dsfield 0xb8 0xfc` in the filter above) when matching DSCP.

DSCP maps to 802.1p by taking the **top three bits**, so DSCP 46 gives priority 5 (measured with a hand-built 802.1Q frame, earlier runs), and to a Wi-Fi access category by the same three bits (user priority). That naive mapping puts EF (priority 5) into the **video** category and not the voice one; RFC 8325 recommends mapping EF to priority 6 (voice) explicitly. A wrong mapping at the wired to wireless boundary is a classic cause of "VoIP is choppy on Wi-Fi only".

### 8.1 Who may set a marking

Markings are **requests**, and nothing stops an application or a user from asking for EF. So QoS has a **trust boundary**: at the edge, the network either trusts marks from known devices (an IP phone) or **re-marks** (resets) everything else to default, and polices the privileged classes to a rate so a lying source cannot starve the rest. Across the internet, DSCP is usually **bleached** (reset to zero) at provider boundaries, so end-to-end DSCP only works inside one administrative domain. Setting and rewriting marks on Linux, with an ingress rule that reset 0xB8 to 0 before the socket saw it, was measured in an earlier run, and the HTB classifier above picks the class from the mark.

---

## 9. A QoS design, end to end

A good design starts from **what must be protected, what can be sacrificed, and what rate each gets**, and only then picks mechanisms.

1. **Find the bottleneck.** QoS only matters where there is congestion: the uplink, the WAN link, the Wi-Fi hop, a switch port. Everywhere else it is cost with no benefit.
2. **Classify at the edge**, with a trust boundary (section 8.1).
3. **Make the queue yours.** Shape just below the bottleneck rate (say 90 to 95 percent) so the queue forms on a device that runs a smart discipline.
4. **Pick a few classes.** Three to five is plenty: network control, real time (voice), interactive, bulk. More classes mean more tuning and more mistakes.
5. **Give each class a rate, a ceiling and a priority**, and **cap the privileged ones**, so a misbehaving voice source cannot starve the rest.
6. **Use per-flow fairness inside the best-effort class** (FQ-CoDel), so one bulk flow cannot hide a short one.
7. **Measure with the right probe**: latency under load for the sensitive class, and throughput for the bulk class, with every class loaded at the same time.

The three-class uplink of section 4.2 follows this: voice (EF) at 1 Mbit/s guaranteed with ceil 2 and the best priority, interactive (AF21) at 4, the rest at 5, on a 10 Mbit/s link. With every class loaded the shares were **1, 4 and 5** Mbit/s; the same when the voice source offered 12 Mbit/s (the cap held, nothing starved); and an EF probe saw about 0.05 ms with voice otherwise idle (earlier runs).

### 9.1 Where else the queues are

- **Datacenter switches.** Output queues share a packet memory, with per-port and per-class limits. Traffic is bursty and synchronised (incast), so buffer headroom and **ECN marking thresholds** are the main tools, with a few priority classes (RDMA, storage, default) and PFC for lossless ones ([NET 12](12_rdma_roce_and_lossless_fabrics.md)). Switches use **virtual output queues** (a queue per input and output pair) to avoid head-of-line blocking inside the switch, and weighted round robin between classes. Tail latency under microbursts, not average load, is the metric.
- **Wi-Fi.** The shared radio channel is the bottleneck, and a slow station takes more airtime per byte than a fast one, so a bulk transfer to a far-away client can slow every client (the **performance anomaly**). The Linux wireless stack puts a **per-station FQ-CoDel queue** in the driver path and schedules by **airtime** instead of packets, and the four WMM access categories give voice and video earlier channel access. Details are in [WF 03](10_wifi/03_wifi_mac.md).
- **Hosts.** TCP small queues, BQL and pacing (`fq` qdisc with BBR) keep the host's own queues short, so the congestion signal reaches the sender and the application sees a few packets of delay, not hundreds.

---

## 10. Common mistakes

1. **A huge buffer on a slow link**, giving seconds of latency under load.
2. **Shaping at the full line rate**, so the queue stays in the modem and your discipline never runs. Shape below the real rate.
3. **A policer with a tiny burst**, which collapses TCP throughput. Burst about the bandwidth delay product.
4. **Strict priority for a class an application can fill**, which starves everything below it.
5. **Round robin by packet when sizes differ**, where DRR (bytes) is needed.
6. **Matching DSCP against the whole TOS byte**, so ECN-marked packets stop matching. Mask the low two bits.
7. **Trusting markings from everyone**, so any application can claim EF.
8. **Assuming DSCP survives the internet.**
9. **EF mapped to the Wi-Fi video category** by the default top-three-bits rule.
10. **Too many classes**, which nobody can reason about.
11. **Measuring only throughput**, which hides the latency problem QoS is meant to fix.
12. **Tuning RED by hand** where CoDel needs no tuning.
13. **Forgetting ingress**: you cannot queue what has already arrived (use ifb, or shape at the other end).

---

## 11. Interview questions

1. What is Little's law and what does it say about a queue?
2. Why does delay explode as load nears 100 percent?
3. What is bufferbloat? How do you recognise it and what is the fix?
4. How big should a buffer be? What changes with many flows?
5. Explain the token bucket. What do rate and burst each control?
6. Shaping against policing: what does each do to a packet and to TCP?
7. Why does a policer need a large burst?
8. Where can you queue, and where can you not (ingress)? How do you shape incoming traffic?
9. Explain tail drop, RED and ECN. What problem does each solve or leave?
10. How does CoDel work, and why is it better than RED?
11. What does FQ-CoDel add to CoDel? What does CAKE add?
12. Compare FIFO, strict priority, round robin and DRR.
13. Why must fairness count bytes, not packets?
14. Explain HTB: rate, ceil and prio. How is spare bandwidth shared?
15. What is the difference between DSCP, ECN, 802.1p and WMM markings?
16. Where should markings be trusted, and what happens to DSCP across the internet?
17. Design the QoS for an office uplink carrying voice, video calls and bulk backups.
18. How are the queues in a datacenter switch different from those on a home router?
19. Why can one slow Wi-Fi client slow everyone, and how is it fixed?
20. A user complains calls are bad only when someone uploads. What do you check?

---

## 12. Scenarios and design questions

**S1. Video calls stutter whenever a laptop syncs files to the cloud.** Diagnose and fix without buying bandwidth.

**S2. After a 100 Mbit/s policer was added, a download is stuck near 20 Mbit/s.** Why?

**S3. A voice class set to strict priority starves other traffic after a firmware bug makes a phone flood.** How should the design have prevented this?

**S4. Ping times to the gateway go from 5 ms to 400 ms during a backup, and return to 5 ms after.** What is it and what exactly would you change?

**S5. VoIP is fine on Ethernet and choppy on Wi-Fi.** What marking and mapping do you check?

**S6. A switch shows no drops but latency spikes for a storage workload.** Where do you look?

**S7. Design per-customer rate limiting for an API gateway.** Which primitive and which parameters?

**S8. You cannot touch the modem, which has a 2 second buffer.** What do you do?

**Where to practise.** In two Linux network namespaces joined by a veth, run `iperf3` through `tbf` with different `burst` and `latency` values and watch `tc -s qdisc`; compare a shaper with an nftables `limit` policer on one TCP flow; build the HTB tree of section 4.2 and load it with three flows. Implement the token bucket, DRR and CoDel snippets as a small simulator. Read RFC 8289 (CoDel), RFC 8290 (FQ-CoDel), the bufferbloat.net material and the `tc-htb` and `tc-tbf` manual pages. For drill, explain scenarios S1 to S8 aloud in two minutes, naming the queue that is the problem first.

---

## 13. Strong answers (outlines)

**Q1.** Average number in the queue equals arrival rate times average wait, so backlog and delay are the same quantity: a queue of B bytes on a link of rate R adds 8B/R seconds.

**Q2.** Mean wait scales as rho / (1 - rho). Half-full links add half a packet time, 95 percent full links add about nineteen. Real traffic is burstier, so it is worse.

**Q3.** A big buffer lets loss-based TCP fill it and hold it full, so every packet waits the whole buffer: high latency under load, normal when idle. Fix with AQM (CoDel, FQ-CoDel, CAKE), a right-sized buffer, or shaping just below the bottleneck so the queue is one you control.

**Q4.** About one bandwidth delay product for a single flow, divided by the square root of the number of flows for many unsynchronised flows. Size for delay, not just utilisation, and use AQM.

**Q5.** Tokens arrive at the rate and are capped at the burst; a packet needs tokens to leave. Rate is the long-run average, burst is how much can exceed it briefly after idleness.

**Q6.** A shaper queues the excess and smooths output, adding delay and few losses. A policer drops the excess immediately, adding no delay but causing bursts of loss that TCP handles badly. Shape at egress where you own the queue, police where you cannot queue.

**Q7.** TCP sends a window in a burst. Without a bucket as large as roughly rate times RTT, the policer drops the tail of every window and TCP's throughput collapses.

**Q8.** You queue on egress. For incoming traffic, either shape at the sender's side or redirect ingress to an ifb device and shape its egress.

**Q9.** Tail drop signals only when full and synchronises flows. RED drops randomly early based on average queue length but is hard to tune. ECN marks instead of dropping so the signal costs no loss.

**Q10.** It measures sojourn time against a 5 ms target over a 100 ms interval, and when the minimum stays above target it drops at an increasing rate (interval over the square root of the count) until delay falls. It needs no tuning because the target is in time, not packets.

**Q11.** FQ-CoDel puts each flow in its own queue served by DRR with a boost for new flows, so sparse flows are isolated from bulk ones. CAKE adds the shaper, per-host fairness, DSCP tiers and framing overhead compensation.

**Q12.** FIFO is unfair and has no isolation. Strict priority isolates the top class and starves the rest. Round robin isolates flows but is fair by packet. DRR is fair by bytes in constant time.

**Q13.** A flow with larger packets would otherwise take proportionally more of the link, so a byte-counting scheduler (DRR, WFQ) is needed for real fairness.

**Q14.** Rate is the guarantee, ceil the borrowing limit, prio the order spare capacity is offered; equal priorities share spare in proportion to rate.

**Q15.** DSCP is the IP-layer class across routers; ECN shares its byte and carries congestion; 802.1p is a 3-bit class on a VLAN link; WMM access category sets Wi-Fi channel access. DSCP maps to the others by its top three bits unless configured otherwise.

**Q16.** At the edge, with re-marking for untrusted sources and rate limits on privileged classes. Providers often reset DSCP, so it works only within a domain.

**Q17.** Find the bottleneck, classify at the edge with a trust boundary, shape just under the link rate, use a capped priority class for voice, a guaranteed class for video, FQ-CoDel for the rest with backups in a low-priority class, and verify latency under load.

**Q18.** Datacenter queues are shared-memory output queues that must absorb microbursts, with ECN thresholds, a few classes and sometimes PFC; the metric is tail latency. A home router queue is a single slow link where bufferbloat and fairness matter.

**Q19.** A slow station takes more airtime per byte, so one far client lowers everyone's throughput. Per-station queues with airtime-fair scheduling and FQ-CoDel in the AP fix it.

**Q20.** Bufferbloat in the uplink: check latency under load, the qdisc and its backlog, whether the bottleneck is the modem; shape below its rate with fq_codel or CAKE and give voice a priority class.

---

## 14. Cheat sheet

- Delay of a queue = bytes x 8 / rate. Wait explodes as 1 / (1 - load).
- Token bucket: rate = average, burst = excess allowed. Shaper queues (delay); policer drops (loss). Policer burst about rate x RTT.
- Bufferbloat: big buffer plus loss-based TCP equals a standing queue. Buffer about BDP, or BDP / sqrt(N). Shape below the bottleneck.
- AQM: RED (average length, hard to tune), ECN (mark, do not drop), CoDel (sojourn time, 5 ms and 100 ms), FQ-CoDel (per flow plus CoDel), CAKE (all in one).
- Schedulers: FIFO, strict priority (starves), round robin (packet fair), DRR (byte fair), WFQ (finish time), HTB (rate, ceil, prio).
- Marks: DSCP (EF 46 = 0xB8, AF21 = 0x48), ECN (low 2 bits), 802.1p (top 3 bits of DSCP), WMM. Mask the ECN bits when matching.
- Trust boundary at the edge; DSCP bleached across the internet; EF may map to Wi-Fi video.
- Linux: `tc qdisc|class|filter`, `tc -s`, `ifb` for ingress, nftables `ip dscp set`.

---

## 15. Sources and caveats

Sources used: RFC 8289 (CoDel), RFC 8290 (FQ-CoDel), RFC 3168 (ECN), RFC 2474 and RFC 4594 (DSCP and service classes), RFC 8325 (mapping Diffserv to IEEE 802.11); the `tc`, `tc-htb`, `tc-tbf`, `tc-fq_codel` and `tc-cake` manual pages; the bufferbloat.net documentation; "Sizing Router Buffers" (Appenzeller, Keslassy, McKeown) and "High Performance TCP in ANSNET" (Villamizar and Song); "Controlling Queue Delay" (Nichols and Jacobson); and the Linux wireless airtime fairness and FQ-CoDel in mac80211 papers by Høiland-Jørgensen et al.

Caveats. The M/D/1 and buffer arithmetic, the token bucket, the DRR and round robin shares, the CoDel control law, and the `tc` and nftables configurations (`tbf`, `htb` with classes and filters, `ifb` ingress shaping, DSCP marking and the policer rule) were **run fresh for this tutorial**, on one Linux 6.18 VM as root, in network namespaces; the CoDel snippet is a simplified model of the RFC state machine, not the kernel's. The measured network results (queueing delay against the formula, the token bucket after idle, shaper against policer on TCP, bufferbloat and the standing queue, HTB shares, strict priority starvation, DSCP marking and 802.1Q priority, the three-class uplink) and every row of the discipline comparison and the many-flow buffer figure come from **earlier runs** of the previous version of this tutorial and were not re-run. This kernel lacks `fq_codel`, `codel`, `cake`, `drr`, `sfq`, `netem` and `act_police`, so CoDel, FQ-CoDel and DRR comparisons are from a **model** whose agreement with the kernel was checked in one case only, and which overstated the mean delay in that case by about 70 percent: read the table as a comparison between disciplines, not a prediction. There is no `netem`, so there is no propagation delay in the kernel runs (the idle round trip was 0.036 ms). The policer here is nftables `limit`, which delivered 5 to 8 percent above its nominal rate. The default congestion control in this VM is BBR, so every TCP run named its algorithm explicitly. Switch ASIC queues, virtual output queues, Wi-Fi airtime fairness, CAKE and the datacenter practices are described from documentation.

Earlier and later: [NET 03](03_tcp_udp_and_transport.md), [NET 12](12_rdma_roce_and_lossless_fabrics.md), [NET 15](15_networking_interview_question_bank.md), [the networking plan](00_plan.md).
