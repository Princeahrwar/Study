# NET 14: Queueing, QoS and traffic management

*Part of the [networking plan](00_plan.md) · Time: about 8 hours · You need: [NET 03](03_tcp_udp_and_transport.md) (congestion control, the window and `ss -ti`), [NET 06](06_linux_network_stack.md) (where the qdisc sits in the transmit path, GSO), and [NET 12](12_rdma_roce_and_lossless_fabrics.md) (ECN marking, PFC and incast, which are queueing problems in a fabric). Every packet that crosses a link waits somewhere, and what that wait costs depends on four decisions: **how much** buffer there is, **what is dropped** when it fills, **who goes first** when several flows are waiting, and **who is allowed to send at all** when a rate is contracted. This tutorial takes each in turn on a real Linux queue (token bucket, HTB, priority bands, policing), measures what the sandbox can, models what its kernel cannot run (RED, CoDel, FQ-CoDel, DRR), and says which is which.*

"What is bufferbloat, and how do CoDel and fair queueing fix it?" is a standard question in networking, infrastructure and Wi-Fi loops, and "policing against shaping" is the first follow-up in any router or firewall interview. A Staff level answer connects five things: the **arithmetic** (Little's law, the delay a buffer adds, the burst a bucket allows), the **mechanism** (tail drop, RED, CoDel, ECN, per-flow queues), the **scheduler** (strict priority, DRR, WFQ, HTB and what each does to a flow that is not sending), the **classification** (DSCP, ECN bits, 802.1p, and who is trusted to set them) and the **failure modes** (starvation, a policer with too little burst, a standing queue). This tutorial puts a measured or modelled number on each.

**Eight honest limits, stated first.**

1. **Most queueing disciplines are missing from this kernel.** Linux 6.18 in this Firecracker VM has `tbf`, `htb`, `pfifo`, `bfifo`, `pfifo_fast`, the `ingress` qdisc, the `ifb` device, `u32` and `cls_bpf` classifiers and `mirred`. It does **not** have `fq_codel`, `codel`, `red`, `drr`, `qfq`, `cake`, `prio`, `sfq`, `hfsc`, `netem`, `pie`, `ets` or `act_police` (read from the kernel configuration). So **RED, CoDel, FQ-CoDel, DRR and WFQ are run in a model**, `sim.py`, not in the kernel. I wrote CoDel and FQ-CoDel from the descriptions in RFC 8289 and RFC 8290 **as I remember them**; I did not re-read the RFCs for this tutorial.
2. **No `netem`, so no propagation delay.** The idle round trip on the veth pair is **0.036 ms**. Every kernel run therefore shows how a queue behaves when the path is short; the effect of a 40 or 100 ms round trip on a window, a policer or a buffer comes only from the model and is marked as such.
3. **The model is checked against the kernel in one case only**: one Reno flow through a tail drop queue (section 5). There it matches the goodput and the maximum delay and **overstates the mean queue delay by about 70 %** at a 10 frame buffer (11.5 ms against 6.7 ms). Read its numbers as a comparison between disciplines under the same conditions, never as a prediction. The 80 frame window cap in the check was chosen after seeing the kernel run, so that row is a fit, not an independent test.
4. **No `act_police`, no `8021q`, no byte queue limits.** The policer here is the nftables `limit` statement, a token bucket whose rate is **5 to 8 % above** what its byte rate implies (measured in section 4; I did not find why). The 802.1Q frames are built by hand. Byte queue limits and the NIC transmit ring are from memory.
5. **The default congestion control in this VM is BBR** (`net.ipv4.tcp_congestion_control = bbr`). Every TCP run names its algorithm with `iperf3 -C`, so the table says which was used.
6. **One host, two vCPUs, Python senders.** Delays and rates are exact counts of what happened here; they are not absolute. The queueing runs differ by about 10 % between runs at 95 % load (mean wait **9.0 ms** in one run and **10.3 ms** in the one printed).
7. **Hardware is from memory:** switch ASIC queues and shared buffers, weighted round robin in hardware, virtual output queues, cut-through, PFC priorities.
8. **Wi-Fi is from memory:** airtime fairness and the per-station queues in `mac80211` are named in section 9, not run.

By the end you can:

- **predict queueing delay from load** (measured, 1,250 byte frames on a 10 Mbit token bucket with Poisson arrivals: mean wait **0.62, 2.13, 4.70 and 10.35 ms** at loads of **0.50, 0.80, 0.90 and 0.95**, against **0.50, 2.00, 4.50 and 9.50 ms** from the M/D/1 formula, and **1.0, 4.0, 9.0 and 19.0 ms** if the arrivals were exponential in size too; the backlog sampled from `tc` was **4.07** frames at load 0.90, Little's law gave **4.05**);
- **say what a token bucket allows** (measured, 8 Mbit with a burst of 15,000 bytes after an idle period: **15,000** bytes left in the first millisecond and **1,015,000** by one second, exactly burst plus rate times time; at 100 Mbit a burst of 1,600 bytes reached **88 Mbit/s** of the **95.6** that fits, and 3,000 bytes reached **95.2**);
- **choose between a shaper and a policer and say what each does to TCP** (measured, 5 Mbit, cubic: the shaper gave **4.71 Mbit/s**, **32** retransmissions and **+30 ms** of round trip; a policer on the receiving side gave **5.07 to 5.24 Mbit/s**, **686 to 983** retransmissions and no added delay; a policer on the sender's own output hook gave **0** retransmissions, because the stack saw a local error; modelled at 40 ms, a policer with a 1,500 byte burst held Reno to **0.30 Mbit/s** and one with a 25 KB burst reached **4.18** of 5);
- **explain bufferbloat with a number** (measured, one 10 Mbit link with a 1.5 MB buffer: cubic and reno held a standing queue of **121 KB**, a round trip of **99 ms** against **0.036 ms** idle, and BBR held **9 ms**; Little's law, backlog over rate, gave **102 ms**);
- **compare queue disciplines under load** (modelled, 10 Mbit, 40 ms, two bulk Reno flows, a probe every 100 ms: the probe saw **1,096 ms** behind a 1,000 frame tail drop queue, **64.3 ms** behind a queue of one bandwidth delay product, **47.0 ms** under CoDel and **40.8 ms** under FQ-CoDel, which is close to the bare 40 ms of propagation; short transfers took **2,210 ms** on average against **114 ms**);
- **compute what a hierarchical shaper does** (measured, HTB with three classes, 10 Mbit: guaranteed rates **2, 2, 2** with the first class at the better priority gave **6.00, 2.00, 2.00**; the same with that class idle gave **0.00, 5.00, 5.00**; rates **1, 3, 1** at equal priority gave **2.00, 6.00, 2.00**; and an `ans_c.py` allocator reproduces all four);
- **say what strict priority costs** (measured, a flood of 12 Mbit into a 10 Mbit link: a probe in its own HTB class saw **0.033 ms** where one behind the flood saw **69.9 ms**; in `pfifo_fast` the lowest band got no reply for **5.3 s**);
- **say why fairness has to count bytes** (modelled, three sources offering 6 Mbit each with frames of 1,500, 750 and 150 bytes: a FIFO gave **2.91, 3.26, 3.83** Mbit/s, round robin by packet **6.18, 3.18, 0.64**, deficit round robin **3.33** each);
- **read and set the markings** (measured: nftables set DSCP **0x2e** (EF) to a TOS byte of **0xb8**, AF21 to **0x48**, CS1 to **0x20**; an ingress rule rewrote **0xb8** to **0x00** before the socket saw it; HTB picked its class from `meta priority` with no filter; a hand built 802.1Q frame with DSCP 46 carried priority **5**);
- **build a three class uplink policy** (measured, HTB on 10 Mbit with voice at 1 Mbit, ceil 2 and the best priority, interactive at 4 and the rest at 5: shares **1.00, 4.00 and 5.00** with every class loaded, the same when the voice source offered 12 Mbit, and an EF probe at **0.053 ms** with voice idle);
- **size a buffer for many flows** (modelled, 10 Mbit, 100 ms: **36** flows kept the link full with **14** frames of buffer where one flow needed **83**, the bandwidth delay product divided by the square root of the flow count).

## Where the numbers come from

Each experiment is a script `eN_*.sh` in `prog/` with its output frozen in `v/`, run with `run.sh`. `qnet.sh` builds the topology used by all of them: two namespaces `qa` and `qb` joined by one veth pair `va` to `vb`, with the queue under test on `va`, so traffic from `qa` to `qb` is shaped and the return path is not. Offloads are off on both ends. `pois.py` is a Poisson UDP sender and receiver that records the one way delay of every packet, `bucket.py` blasts frames after an idle period, `flood.py` and `sink.py` are paced floods and a receiver that reports the TOS bytes it saw, `qsample.py` polls the backlog from `tc`, `sumq.py` turns an `iperf3` run, a `ping` run and a backlog sample into one table row, `htbshare.py` reads class byte counters, and `dot1q.py` builds 802.1Q frames. `sim.py` is the model and `wfq.py` the worked scheduling example. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, modelled or computed here is marked **from memory**. The model and the problem scripts use fixed seeds, so each run prints the same figures; the kernel runs do not, and where two runs differed I say so.

## Data and timing

| What | Size or time |
|---|---|
| Delay against load (`e1_md1.sh`) | 4 loads of 30 s each, 15,037 to 28,451 packets, backlog sampled every 20 ms, about 2.5 minutes |
| Token bucket after idle (`e2_bucket.sh`) | 3 bursts, 1 s of blasting each |
| Buffers and congestion control (`e3_bloat.sh`) | 4 buffer sizes, 3 algorithms, 12 s each, ping every 50 ms, about 3 minutes |
| Policing against shaping (`e4_police.sh`) | 6 cases of 12 s |
| HTB shares (`e5_htb.sh`) | 4 scenarios of 12 s |
| Priority and starvation (`e5b_prio.sh`) | 3 setups, 25 pings each, about 1 minute |
| Marking and classification (`e6_dscp.sh`) | one `tcpdump` per section, a few packets |
| The model (`e7_model.sh`) | 7 sections, about 6 s |
| Burst size (`e8_burst.sh`) | 8 runs of 6 s |
| What holds the standing queue (`e3b_window.sh`) | 1 run of 9 s, two samples of `ss` |
| A three class design (`e9_design.sh`) | 3 scenarios of 12 s, 40 pings each |

---

## 1. Why queues exist, and what a queue costs

A link sends one frame at a time. A frame of `s` bytes on a link of rate `R` occupies it for the **service time** `S = s / R`. Whenever frames arrive faster than that for a moment, the later ones wait. That wait is the **queueing delay**, and it is the only part of latency that changes with load: propagation is fixed by distance, serialisation by `S`, and the queue by whoever else is using the link.

| Link | `S` for a 1,500 byte frame |
|---|---|
| 1 Mbit | 12 ms |
| 10 Mbit | 1.2 ms |
| 100 Mbit | 120 µs |
| 1 Gbit | 12 µs |
| 10 Gbit | 1.2 µs |
| 100 Gbit | 0.12 µs |

These are the arithmetic `1500 × 8 / R`. They decide how much a queue of a given length hurts: ten frames waiting cost 12 ms at 10 Mbit and 12 µs at 1 Gbit.

### 1.1 Little's law: backlog and delay are the same number

For any queue that is stable, in any arrival pattern, the average number of items in it `L` equals the arrival rate `λ` times the average time each spends in it `W`:

```text
L = λ × W
```

On a link, a frame leaves at rate `R`, so the bytes waiting and the delay are linked directly: **delay = bytes in the queue ÷ rate**. A buffer holding 125,000 bytes in front of a 10 Mbit link is a 100 ms delay when it is full, whatever the traffic is. This is the equation behind every buffer sizing argument in this tutorial, and the line to say first in an interview.

### 1.2 How much a queue costs as load rises

Take frames of equal size arriving at random (a Poisson process) at rate `λ`, with utilisation `ρ = λ × S`. The Pollaczek-Khinchine formula (**from memory**) gives the mean wait before service starts as `λ × E[S²] / (2 × (1 - ρ))`. With a fixed frame size `E[S²] = S²`, so

```text
M/D/1:  wait = ρ × S / (2 × (1 - ρ))
M/M/1:  wait = ρ × S / (1 - ρ)          (sizes exponential too: twice the wait)
```

The factor `1 / (1 - ρ)` is the whole story: at half load the wait is half a frame time, at 90 % it is 4.5 frame times and at 95 % it is 9.5. The queue does not grow gradually; it has a knee.

I measured it. `qnet.sh` builds the two namespaces, `pois.py` sends 1,250 byte frames (1,208 bytes of payload, so one frame takes exactly `S = 1.000` ms at 10 Mbit) at random with exponential gaps and stamps each with its send time, and the receiver subtracts. A token bucket with a burst of one frame and a very large queue is the bottleneck; `qsample.py` reads its backlog from `tc` every 20 ms.

```bash
# qnet.sh: two namespaces qa and qb joined by one veth. The queue under test sits on va (qa), so traffic qa -> qb is shaped and the return path is not.
q_up() {
  for n in qa qb; do ip netns add $n; ip netns exec $n ip link set lo up; done
  ip link add va netns qa type veth peer name vb netns qb
  ip netns exec qa ip addr add 10.9.0.1/24 dev va; ip netns exec qb ip addr add 10.9.0.2/24 dev vb
  ip netns exec qa ip link set va up; ip netns exec qb ip link set vb up
  ip netns exec qa ethtool -K va tso off gso off gro off > /dev/null 2>&1
  ip netns exec qb ethtool -K vb tso off gso off gro off > /dev/null 2>&1
}
q_down() { pkill -f 'iperf3 -s' 2> /dev/null; pkill -f 'python3 (pois|sink|flood|qsample|bucket)[.]py' 2> /dev/null; for n in qa qb; do ip netns del $n 2> /dev/null; done; }
A() { ip netns exec qa "$@"; }
B() { ip netns exec qb "$@"; }
```

```bash
# e1_md1.sh: Poisson arrivals of fixed size packets into a 10 Mbit token bucket, delay and backlog against the M/D/1 formula and Little's law.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
echo "--- 1. the link: 1,250 byte frames (1,208 byte payload) at 10 Mbit, so one packet takes S = 1.000 ms"
A tc qdisc add dev va root tbf rate 10mbit burst 1300 limit 1000000
A tc qdisc show dev va
for pps in 500 800 900 950; do
  echo "--- load $pps packets per second"
  B python3 pois.py rx 9000 32 & sleep 0.3
  A python3 qsample.py va 30 &
  A python3 pois.py tx 10.9.0.2 9000 $pps 30 1208
  wait
  python3 md1.py $pps
done
```

```python
def tx(ip, port, pps, secs, size):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); pad = b"x" * (size - 8)
    t0 = time.perf_counter(); t = t0; k = 0; random.seed(1)
    while t - t0 < secs:
        t += random.expovariate(pps)
        while time.perf_counter() < t - 0.0004: time.sleep(0.0002)
        while time.perf_counter() < t: pass
        s.sendto(struct.pack("!q", time.time_ns()) + pad, (ip, port)); k += 1
    print("sent %d packets, %.1f per second" % (k, k / secs))
```

```text
--- load 500 packets per second
sent 15037 packets, 501.2 per second
backlog samples 1320  mean 309 bytes (0.25 packets)  max 13750 bytes
received 15037  min 0.006  mean 0.616  p50 0.161  p99 3.827  max 11.203 ms
rho 0.500  M/D/1 wait 0.500 ms  M/M/1 wait 1.000 ms  Little: packets waiting 0.25 (M/D/1)

--- load 800 packets per second
sent 24009 packets, 800.3 per second
backlog samples 1325  mean 1942 bytes (1.55 packets)  max 18750 bytes
received 24009  min 0.009  mean 2.127  p50 1.380  p99 10.847  max 16.727 ms
rho 0.800  M/D/1 wait 2.000 ms  M/M/1 wait 4.000 ms  Little: packets waiting 1.60 (M/D/1)

--- load 900 packets per second
sent 26931 packets, 897.7 per second
backlog samples 1319  mean 5092 bytes (4.07 packets)  max 28750 bytes
received 26931  min 0.007  mean 4.699  p50 3.200  p99 19.321  max 24.848 ms
rho 0.900  M/D/1 wait 4.500 ms  M/M/1 wait 9.000 ms  Little: packets waiting 4.05 (M/D/1)

--- load 950 packets per second
sent 28451 packets, 948.4 per second
backlog samples 1325  mean 11958 bytes (9.57 packets)  max 86250 bytes
received 28451  min 0.008  mean 10.349  p50 7.078  p99 61.066  max 72.239 ms
rho 0.950  M/D/1 wait 9.500 ms  M/M/1 wait 19.000 ms  Little: packets waiting 9.03 (M/D/1)
```

Putting the four runs next to the formulas, with the mean delay as the receiver measured it (it includes about 0.01 ms of veth and Python floor):

| Load `ρ` (sent per second) | Measured mean | M/D/1 | M/M/1 | Measured p99 | Backlog sampled | Little: `λ × wait` |
|---|---|---|---|---|---|---|
| 0.50 (501.2) | 0.62 ms | 0.50 ms | 1.00 ms | 3.8 ms | 0.25 frames | 0.25 |
| 0.80 (800.3) | 2.13 ms | 2.00 ms | 4.00 ms | 10.8 ms | 1.55 frames | 1.60 |
| 0.90 (897.7) | 4.70 ms | 4.50 ms | 9.00 ms | 19.3 ms | 4.07 frames | 4.05 |
| 0.95 (948.4) | 10.35 ms | 9.50 ms | 19.00 ms | 61.1 ms | 9.57 frames | 9.03 |

The fixed frame size makes the queue M/D/1, and it sits within 4 to 9 % of the formula from 0.80 up (the 0.50 row is 0.12 ms above it, about a tenth of a frame, which is the sender's timing jitter). The 0.95 row moved from 9.0 to 10.3 ms between two runs of the same script, which is the noise of a heavily loaded queue with 28,000 samples. The sampled backlog agrees with Little's law at every load: **the bytes in the buffer and the delay they cause are one measurement seen twice**.

Three things follow.

1. **The tail is much worse than the mean.** At load 0.90 the mean wait is 4.7 ms and the 99th percentile 19.3 ms; at 0.95 it is **61 ms**. A latency target set on the mean says little about the packets that hurt.
2. **Fast links do not queue from steady load, only from bursts.** The load at which an M/D/1 queue reaches a mean wait of 1 ms is **0.625** on 10 Mbit with 1,500 byte frames, **0.943** on 100 Mbit and **0.994** on 1 Gbit (`ans_a.py`, problem A). On a fast link the queue you see is built by microbursts, not by utilisation, which is why datacenter queueing (incast, NET 12) is about synchronised senders rather than average load.
3. **Poisson arrivals are the kind case.** Real traffic is burstier, because TCP sends a window back to back, so a real queue at the same average load is longer than M/D/1. Use the formula as a lower bound.

---

## 2. The Linux transmit queue

A packet leaving a Linux host passes the socket, TCP, IP, the neighbour layer and `dev_queue_xmit`, which hands it to the **queueing discipline** (qdisc) of the device. The qdisc is the only place where Linux queues by policy: it enqueues the packet, and a transmit softirq or the sending context dequeues from it into the driver's transmit ring and then the NIC, which are plain FIFOs. Everything in this tutorial that is a choice (buffer size, drop rule, scheduler, rate limit) is a qdisc, a class inside one, or a filter that picks the class.

```text
--- 1. a new veth has no queue; the sysctl says what a new root qdisc would be
qdisc noqueue 0: root refcnt 2
net.core.default_qdisc = pfifo_fast
```

A fresh veth has **no queue** (`noqueue`): a packet goes straight to the peer. A physical NIC gets whatever `net.core.default_qdisc` names. In this VM that is `pfifo_fast`; on current distributions it is usually `fq_codel` or `fq` (**from memory**; set by `sysctl` or `systemd`).

```text
--- 2. pfifo_fast: three bands, and the table that maps the packet priority (0 to 15) to a band
qdisc pfifo_fast 801b: root refcnt 3 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
```

`pfifo_fast` is three FIFOs served in strict priority. The **priomap** has sixteen entries, indexed by the packet's `skb->priority` (0 to 15), each giving a band: priority 0 goes to band 1, priority 6 and 7 to band 0, priority 2 to band 2. The kernel derives the priority from the IP TOS byte for locally generated traffic (section 8). The queue length of each band is the device's `txqueuelen` (`ip link show` prints it as `qlen`; `ip link set va txqueuelen 100` changes it).

```text
--- 3. a token bucket asked for a latency, with a child queue; tc prints the limit it derived as a latency
qdisc tbf 1: root refcnt 3 rate 10Mbit burst 15000b lat 50ms
qdisc tbf 1: root refcnt 3 rate 10Mbit burst 15000b lat 50ms
qdisc pfifo 10: parent 1:1 limit 100p
```

Three reading rules. **Qdiscs are named `major:minor`**, with the qdisc itself `handle 1:` and its classes `1:10`, `1:20`; a child attaches with `parent 1:10`. **`tbf` accepts one child**, which is where its queue lives; given `latency 50ms` it derives a byte limit from the rate and the burst and prints it back as `lat 50ms`. **`tc -s qdisc show`** adds `Sent` (bytes and packets), `dropped`, `overlimits` (the count of times a packet had to wait for tokens or was refused), `requeues` and `backlog` (bytes and skbs waiting now), which is what `qsample.py` reads.

```text
--- 4. the backlog of a 10 Mbit bucket under a TCP flow, in bytes and in skbs, with offloads off and on
tso/gso off backlog   116578 bytes in   77 skbs: 1514 bytes per skb
tso/gso off backlog    96896 bytes in   64 skbs: 1514 bytes per skb
tso/gso off backlog   142316 bytes in   94 skbs: 1514 bytes per skb
tso/gso on  backlog   124148 bytes in   82 skbs: 1514 bytes per skb
tso/gso on  backlog   101438 bytes in   67 skbs: 1514 bytes per skb
tso/gso on  backlog   148372 bytes in   98 skbs: 1514 bytes per skb
```

A qdisc can see large segments when TSO or GSO is on (**from memory**: up to 64 KB per skb), and a packet limit then counts skbs, not frames. A `tbf` with a 1,600 byte burst queued **1,514 bytes per skb** with offloads off and on, so it split them before queueing (inferred from this output; the kernel's `tbf` segments an skb larger than its burst). The consequence for tuning is to set byte limits, not packet limits, where a token bucket is involved, and to keep offloads off while measuring a shaper, as `qnet.sh` does.

Below the qdisc, **byte queue limits** (BQL, **from memory**) cap how many bytes sit in the driver ring so that the qdisc, not the ring, holds the standing queue; veth has no BQL here (`/sys/class/net/va/queues/tx-0/` has no `byte_queue_limits`). A multi-queue NIC gets an `mq` root with one child qdisc per hardware queue (**from memory**), and the `ingress` qdisc has no queue at all: it is a hook for classifying, policing or redirecting what arrives (section 4).

---

## 3. The token bucket

A **token bucket** has two numbers: a **rate** `r` at which tokens are added, and a **burst** `b`, the most tokens it can hold. Sending a packet of `n` bytes needs `n` tokens and spends them. A full bucket therefore lets `b` bytes out at once, and after that traffic is held to `r`. Over any interval of length `t`, no more than **`b + r × t` bytes** can leave. That bound is the whole definition.

I measured it by blasting 1,250 byte frames as fast as Python could send them, after an idle second so the bucket was full, through `tbf` at 8 Mbit (1,000,000 bytes per second) with three bursts, and counting what the receiver saw by time.

```bash
# e2_bucket.sh: a token bucket after an idle period: what leaves in the first milliseconds is the burst plus the rate times t.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
for burst in 1300 15000 100000; do
  echo "--- tbf rate 8mbit (1,000,000 bytes per second), burst $burst"
  A tc qdisc replace dev va root tbf rate 8mbit burst $burst limit 2000000
  sleep 1
  B python3 bucket.py rx 9100 4 1000000 $burst & sleep 0.3
  A python3 bucket.py tx 10.9.0.2 9100 1.0
  wait
  A tc -s qdisc show dev va | sed -n 2p
done
```

```text
--- tbf rate 8mbit (1,000,000 bytes per second), burst 15000
  t ms   frames  seen bytes  burst + rate * t
     1      12       15000             16000
     5      16       20000             20000
    10      20       25000             25000
    50      52       65000             65000
   100      92      115000            115000
   500     412      515000            515000
  1000     812     1015000           1015000
 Sent 2190042 bytes 1753 pkt (dropped 0, overlimits 5213 requeues 0)
```

```text
--- tbf rate 8mbit (1,000,000 bytes per second), burst 100000
  t ms   frames  seen bytes  burst + rate * t
     1      80      100000            101000
     5      84      105000            105000
    10      88      110000            110000
    50     120      150000            150000
   100     159      198750            200000
   500     480      600000            600000
  1000     879     1098750           1100000
 Sent 3406292 bytes 2726 pkt (dropped 0, overlimits 7890 requeues 0)
```

With a 15,000 byte burst, **15,000 bytes** left in the first millisecond and **1,015,000** by one second, exactly `b + r × t`. With 100,000, **100,000 bytes** left in the first millisecond (80 frames, which is the whole burst) and **1,098,750** by one second, 1,250 bytes under the bound. With a 1,300 byte burst only one frame left in the first millisecond. The numbers never exceeded the bound in any row; they sit at it, so the bucket is exact and a `b` of one frame means no burst at all.

### 3.1 Shaper and meter, and the two buckets of a real policy

The same arithmetic is used two ways. As a **shaper** the packets that find no tokens wait in a queue and leave when tokens arrive: traffic is smoothed and nothing is lost until the queue fills. As a **meter** (policer) the packets that find no tokens are dropped or marked: nothing is held, the output is capped, and the sender is told by loss (section 4). A **leaky bucket** is the shaper with `b` of one packet: the queue drains at a constant rate and there is no burst. Hardware and standards use two buckets: a committed rate and burst (`CIR`, `CBS`) and a peak rate and burst (`PIR`, `PBS`) giving green, yellow and red packets (the single-rate and two-rate three-colour markers of RFC 2697 and RFC 2698, **from memory**). `tbf` has `peakrate` and `mtu` for the second bucket.

### 3.2 How much burst is enough

`tbf` takes `rate`, `burst` and either `limit` (the queue, in bytes) or `latency` (the delay that queue may add, from which it derives the limit). The burst must be at least one full frame or the frame never leaves. The old rule in the `tc` manual page (**from memory**) is that the burst should be at least the rate divided by the kernel tick `HZ`, which is 250 here: **50,000 bytes at 100 Mbit**. I measured where a 100 Mbit bucket actually reaches its rate, with TCP and offloads off and on:

```bash
printf "%7s %-8s %9s %6s\n" burst offload Mbit/s retr
for off in off on; do
  A ethtool -K va tso $off gso $off > /dev/null 2>&1
  for burst in 1600 3000 15000 100000; do
    A tc qdisc replace dev va root tbf rate 100mbit burst $burst limit 300000
    A iperf3 -c 10.9.0.2 -t 6 -C cubic -J > /tmp/q_b.json
    python3 - $burst $off <<'PY'
import json, sys
j = json.load(open("/tmp/q_b.json"))["end"]
print("%7s %-8s %9.1f %6d" % (sys.argv[1], sys.argv[2], j["sum_received"]["bits_per_second"] / 1e6, j["sum_sent"]["retransmits"]))
```

```text
--- tbf rate 100mbit, TCP cubic, 6 s; burst in bytes; offload is TSO and GSO on the sending veth
  burst offload     Mbit/s   retr
   1600 off           88.3      0
   3000 off           95.2      0
  15000 off           95.6      0
 100000 off           95.7      0
   1600 on            88.1      0
   3000 on            95.4      0
  15000 on            95.5      0
 100000 on            95.6      0
qdisc tbf 8013: root refcnt 3 rate 100Mbit burst 100000b lat 16ms
```

(Both offload settings gave the same result; the 95.6 Mbit/s ceiling is 100 Mbit of frames carrying 1,448 bytes of payload in 1,514.) A **burst of 1,600 bytes reached 88 Mbit/s**, 92 % of what fits, and **3,000 bytes reached 95.2**, the full rate. So on this kernel, which has high resolution timers, the burst needed was about two frames, not 50,000 bytes. A burst of one frame loses rate because the bucket cannot absorb a late timer: tokens earned while the timer was late are lost once the bucket is full (inferred from the shortfall; I did not trace the timer). The practical rule is **at least two full frames, and more if a policer, not a shaper, is in use** (section 4), and the table in problem B gives the sizes next to the bandwidth delay product.


---

## 4. Policing and shaping

Both enforce a rate with a token bucket (section 3). They differ in what happens to the packet that finds no tokens.

| | Shaper | Policer |
|---|---|---|
| Packet without tokens | waits in a queue | dropped (or marked) |
| Adds delay | yes, up to the queue length divided by the rate | no |
| Needs a buffer | yes | no |
| What TCP sees | a longer round trip, then a loss if the queue fills | a loss, with the round trip unchanged |
| Where it can sit | on egress, where there is a queue to hold packets | egress or ingress; ingress is where it is used, since you cannot delay what has already arrived |
| Typical use | your own uplink, a CPE, a hierarchy of classes | an ISP edge enforcing a contract, control plane protection (**from memory**: CoPP on routers) |

The mechanical consequence is that a shaper is **kind to TCP** and costs delay, and a policer costs nothing on the data path and is **hard on TCP**. An ingress shaper exists (redirect to an `ifb` device and queue there), and it is the way to shape what arrives on a Linux host: the queue forms on the `ifb` device, so the sender is slowed by delay rather than by loss.

I held one TCP flow (cubic, 12 s) to 5 Mbit in five ways on the same veth pair. `qsample.py` reads the backlog, `ping` runs every 50 ms, and `sumq.py` prints one row per case. The policers are the nftables `limit` statement (`limit rate over 625000 bytes/second ... drop`); `act_police` is not in this kernel.

```bash
# e4_police.sh: one TCP flow held to 5 Mbit three ways: a shaper (token bucket with a queue), a policer (token bucket that drops) with three burst sizes, and an ingress shaper through an ifb device.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
B iperf3 -s -D > /dev/null; sleep 0.5
A ip link set va txqueuelen 1000
BASE=0.06
echo "--- rate 5 Mbit is 625,000 bytes per second; W = backlog / goodput; cubic, 12 s, ping every 50 ms"
printf "%-22s %6s %8s %6s %9s %9s %9s %10s %9s\n" case cc Mbit/s retr rtt_min rtt_avg rtt_max backlog_B W_ms
one() {  # one LABEL: run the flow, ping and sampler against whatever is configured now
  A iperf3 -c 10.9.0.2 -t 12 -C cubic -J > /tmp/q_ip.json &
  sleep 2
  A python3 qsample.py $2 8 > /tmp/q_qs.txt &
  A ping -i 0.05 -w 8 -q 10.9.0.2 > /tmp/q_pg.txt
  wait
  python3 sumq.py "$1" cubic /tmp/q_ip.json /tmp/q_pg.txt /tmp/q_qs.txt $BASE
}
A tc qdisc add dev va root tbf rate 5mbit burst 3000 limit 30000
one "shaper, queue 30 KB" va
A tc qdisc del dev va root; A tc qdisc add dev va root pfifo_fast
echo "--- policer on the sender's own output hook: the drop is returned to TCP as a local error (the hook sees 64 KB segments before the device splits them)"
A nft add table ip t; A nft "add chain ip t o { type filter hook output priority 0; }"
A nft add rule ip t o ip daddr 10.9.0.2 limit rate over 625000 bytes/second burst 15000 bytes counter drop
one "policer at sender output" va
A nft list chain ip t o | grep -o 'counter packets [0-9]* bytes [0-9]*' | sed 's/^/    dropped by the policer: /'
A nft flush chain ip t o
echo "--- policer on the receiver's ingress (the real place for one): the drop is a loss on the wire"
B nft add table ip t; B nft "add chain ip t i { type filter hook prerouting priority -150; }"
for burst in 1514 15000 150000; do
  B nft flush chain ip t i
  B nft add rule ip t i ip daddr 10.9.0.2 limit rate over 625000 bytes/second burst $burst bytes counter drop
  one "policer, burst $burst" va
  B nft list chain ip t i | grep -o 'counter packets [0-9]* bytes [0-9]*' | sed 's/^/    dropped by the policer: /'
done
B nft flush chain ip t i
echo "--- the same 5 Mbit enforced on the receiving side: ingress redirect to an ifb device with a token bucket"
B ip link add ifbq type ifb; B ip link set ifbq up
B tc qdisc add dev vb handle ffff: ingress
B tc filter add dev vb parent ffff: protocol ip u32 match u32 0 0 action mirred egress redirect dev ifbq
B tc qdisc add dev ifbq root tbf rate 5mbit burst 3000 limit 30000
one "ingress shaper via ifb" va
B tc -s qdisc show dev ifbq | sed -n 1,3p
```

```text
--- rate 5 Mbit is 625,000 bytes per second; W = backlog / goodput; cubic, 12 s, ping every 50 ms
case                       cc   Mbit/s   retr   rtt_min   rtt_avg   rtt_max  backlog_B      W_ms
shaper, queue 30 KB  cubic     4.71     32       2.9      29.8      44.7      19048      32.4

--- policer on the sender's own output hook: the drop is returned to TCP as a local error (the hook sees 64 KB segments before the device splits them)
policer at sender output  cubic     5.33      0       0.0       0.0       0.1          0       0.0
    dropped by the policer: counter packets 999 bytes 65146788

--- policer on the receiver's ingress (the real place for one): the drop is a loss on the wire
policer, burst 1514  cubic     5.15    686       0.0       0.0       0.1          0       0.0
    dropped by the policer: counter packets 686 bytes 1029000
policer, burst 15000  cubic     5.07    891       0.0       0.1       0.1          0       0.0
    dropped by the policer: counter packets 892 bytes 1338000
policer, burst 150000  cubic     5.24    983       0.0       0.1       3.1          0       0.0
    dropped by the policer: counter packets 983 bytes 1473804

--- the same 5 Mbit enforced on the receiving side: ingress redirect to an ifb device with a token bucket
ingress shaper via ifb  cubic     4.70     35       5.1      29.7      44.6          0       0.0
qdisc tbf 8011: root refcnt 2 rate 5Mbit burst 3000b lat 43.2ms
 Sent 7548812 bytes 5151 pkt (dropped 36, overlimits 10271 requeues 0)
 backlog 0b 0p requeues 0
```

What the table says, case by case.

1. **The shaper** (a `tbf` with a 30 KB queue) held the flow to **4.71 Mbit/s**, **32** retransmissions, and a round trip that rose from 0.036 ms idle to **29.8 ms** on average. A 5 Mbit link carries 5,000,000 / 8 = 625,000 bytes a second, so 19,048 bytes of backlog is 30.5 ms; the measured 29.8 and the computed 32.4 (backlog divided by goodput) agree with Little's law. The shaper converted the excess into delay, and TCP backed off when the 30 KB queue overflowed.
2. **The policer on the receiver's ingress** (the real place for one) gave **5.07 to 5.24 Mbit/s**, **686, 891 and 983** retransmissions, and **no added delay** (average round trip 0.0 to 0.1 ms). Compare the policer's own drop counter with the sender's retransmission count: **686 against 686, 892 against 891, 983 against 983**. Every drop became exactly one retransmission, which is how a policer talks to TCP: only by loss.
3. **The policer on the sender's own output hook** gave **5.33 Mbit/s and 0 retransmissions**, with 999 drops of about **65,200 bytes each** (65,146,788 bytes in all). At that hook the stack still holds 64 KB segments that the device would split later, and a drop there is reported to the sender as a **local transmit failure** that TCP does not treat as loss on the network (inferred from the zero retransmissions and the 64 KB drops; the exact path in the kernel is from memory). A policer applied before the NIC is not a policer in the sense of an ISP edge, and a measurement made that way says nothing about TCP over a policed path.
4. **The ingress shaper through `ifb`** gave **4.70 Mbit/s, 35 retransmissions and a 29.7 ms round trip**, the same as the egress shaper. The queue is on the receiving host, so its delay is added to the path all the same.

Two corrections to read in the numbers. First, the achieved rate is **above** the configured one for every policer: iperf counts payload, 1,448 bytes in each 1,500 byte IP packet, so 5 Mbit of IP bytes is `5 × 1448 / 1500 = 4.83` Mbit/s of goodput, and the policers delivered **5.07 to 5.24**, **5 to 8 % more**. I did not find why (the `limit` statement may count bytes differently, or its bucket may start with extra credit; both are guesses). The shaper delivered 4.71 against `5 × 1448 / 1514 = 4.78` for 1,514 byte frames, 1.5 % short. Second, the three burst sizes (1,514, 15,000 and 150,000 bytes) gave **the same rate** here, because the round trip is 0.036 ms and the flow never has more than a few segments in flight. A burst only starts to matter when a window is large, which is a question of round trip time, and I cannot create one without `netem`. So the policer's real weakness is measured in the model.

### 4.1 Why a policer needs a burst of a bandwidth delay product

A Reno flow with a round trip `RTT` and a target `R` has a window of about `R × RTT` bytes when it is at the rate, and sends it in a burst at the start of each round trip (ack clocking is weaker after a loss). A policer whose bucket holds less than that window drops part of every burst, even though the flow's average is under the rate. The loss then halves the window, so the flow does not reach `R`. I ran this in the model, with a 40 ms round trip, a 5 Mbit target and so a bandwidth delay product of 17 frames (25 KB):

```text
--- 3. policing against shaping at a realistic round trip
one Reno flow, 40 ms round trip, a 5 Mbit target (the bandwidth delay product is 17 frames, 25 KB); the policer sits in front of a 100 Mbit link
case                       goodput Mbit  drops per s     rtt ms
shaper, queue 17 frames            5.00          0.9       66.3
shaper, queue 100 frames           5.00          0.1      224.7
policer, burst 1500 B              0.30         12.5       40.1
policer, burst 15000 B             2.69          4.2       40.1
policer, burst 25000 B             4.18          7.4       40.2
policer, burst 150000 B            5.00         12.4       40.2
policer, burst 625000 B            5.00         12.3       40.2
```

With a **1,500 byte burst the policer held Reno to 0.30 Mbit/s** of a 5 Mbit contract, with 12.5 drops a second. A **15,000 byte** burst gave **2.69**, **25,000** gave **4.18**, and **150,000 and 625,000** gave the full **5.00**. A shaper with a queue of 17 frames (one bandwidth delay product) gave 5.00 with 0.9 drops a second and a round trip of **66.3 ms** (40 ms of propagation plus 26 ms of queue), and one with a 100 frame queue gave 5.00 as well, with a round trip of **224.7 ms**. These are model numbers: the model overstates queues (section 5) and was not checked against a policer in the kernel.

The practical rules that follow:

- **A policer needs a burst of at least the bandwidth delay product of the flows it carries** (rate times the largest round trip), more for several flows, and a customer behind a 150 ms path needs 6 times the burst of one behind 25 ms. Vendors' default bursts are often a few milliseconds of the rate (**from memory**), which is far too small for a long path: the symptom is a customer who gets a fraction of a contracted rate that a speed test with many flows or a short path does reach.
- **A shaper needs a queue of about one bandwidth delay product**, not more: the 100 frame queue gave the same rate as the 17 frame queue and 158 ms more delay.
- **Shape at your own edge, police at the other side's.** Traffic you send into a contracted link should be shaped slightly below the contracted rate, so that you hold the queue and not the provider's policer.

---

## 5. Buffer sizing and bufferbloat

A buffer exists to absorb a burst without loss. By Little's law (section 1.1) every byte in it is paid for in delay, `bytes ÷ rate`, so the question is how many bytes are wanted and who needs them.

### 5.1 Bufferbloat, measured

**Bufferbloat** is a buffer much larger than the path needs, kept full by a flow that only learns the rate by losing a packet. I put one 10 Mbit token bucket on `va` and ran cubic, reno and BBR through four buffer sizes while `ping` measured the round trip every 50 ms and `qsample.py` read the backlog. `W_ms` is the backlog divided by the goodput, Little's law computed from the sample.

```bash
# e3_bloat.sh: one TCP bulk flow through a 10 Mbit token bucket with a small, medium and huge buffer: goodput, RTT under load and backlog, and Little's law.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
B iperf3 -s -D > /dev/null; sleep 0.5
A tc qdisc add dev va root tbf rate 10mbit burst 1600 limit 15000
echo "--- idle round trip time"
A ping -c 20 -i 0.05 -q 10.9.0.2 | tail -1
BASE=$(A ping -c 20 -i 0.05 -q 10.9.0.2 | tail -1 | sed -E 's#.*= [0-9.]+/([0-9.]+)/.*#\1#')
echo "--- limit is the buffer in bytes (1,514 bytes is one full frame); W = backlog / goodput is Little's law"
printf "%9s %6s %8s %6s %9s %9s %9s %10s %9s\n" limit cc Mbit/s retr rtt_min rtt_avg rtt_max backlog_B W_ms
for lim in 15000 75000 378000 1500000; do
  A tc qdisc change dev va root tbf rate 10mbit burst 1600 limit $lim
  for cc in cubic reno bbr; do
    A iperf3 -c 10.9.0.2 -t 12 -C $cc -J > /tmp/q_ip.json &
    sleep 2
    A python3 qsample.py va 8 > /tmp/q_qs.txt &
    A ping -i 0.05 -w 8 -q 10.9.0.2 > /tmp/q_pg.txt
    wait
    python3 sumq.py $lim $cc /tmp/q_ip.json /tmp/q_pg.txt /tmp/q_qs.txt $BASE
  done
done
```

```text
--- idle round trip time
rtt min/avg/max/mdev = 0.011/0.036/0.050/0.011 ms
--- limit is the buffer in bytes (1,514 bytes is one full frame); W = backlog / goodput is Little's law
    limit     cc   Mbit/s   retr   rtt_min   rtt_avg   rtt_max  backlog_B      W_ms
    15000  cubic     9.52     67       2.6       6.7      10.7       9183       7.7
    15000   reno     9.52    264       2.8       6.7      10.9       9068       7.6
    15000    bbr     9.52    785       2.0       7.6      11.6      10360       8.7
    75000  cubic     9.40     21      10.4      36.7      58.0      46159      39.3
    75000   reno     9.50     26      16.1      33.5      51.4      42896      36.1
    75000    bbr     9.51     91       4.8       8.7      13.9      11586       9.7
   378000  cubic     9.47      0      72.1      99.2     122.8     121318     102.5
   378000   reno     9.46      0      71.1      98.3     101.3     121149     102.4
   378000    bbr     9.51      0       5.2       9.0      13.8      11830      10.0
  1500000  cubic     9.47      0      64.0      99.1     124.5     120889     102.2
  1500000   reno     9.53      0      95.2      99.2     102.2     120665     101.3
  1500000    bbr     9.51      0       4.8       8.8      14.2      11596       9.8
```

Read the table by buffer size.

- **15,000 bytes (10 frames)** is enough for the full link: **9.52 Mbit/s** for all three, with a round trip of **6.7 to 7.6 ms**. On a path this short a few frames are all that is needed to keep the link busy.
- **75,000 bytes** (50 frames) put **36.7 ms** (cubic) and **33.5 ms** (reno) on the round trip for **9.40 to 9.51 Mbit/s**: a longer delay, and no more throughput.
- **378,000 and 1,500,000 bytes** gave **99 ms** for cubic and reno **in both**, a backlog of **121 KB**, and **0 retransmissions**. The 1.5 MB buffer was never filled; something else capped the queue at 121 KB, which is **102 ms** by Little's law (`121,318 ÷ 1,184,000`) and 99 ms by `ping`. The idle round trip was **0.036 ms**, so the flow added **about 99 ms**, about 2,750 times the idle path.
- **BBR** held **about 9 ms** (8.7 to 9.0) in every row from 75 KB up, with a backlog of 11.6 KB, because BBR (**from memory**) sizes its window to the bandwidth delay product it measures and does not wait for loss. Its retransmissions (785 at the 10 frame buffer, 91 at 75 KB, none from 378 KB up) are what it pays when the buffer is smaller than the queue it wants to build.

What capped the standing queue at 121 KB when the buffer allowed 1.5 MB? I looked at the sender's socket state during the 1.5 MB run:

```bash
# e3b_window.sh: what holds the standing queue of a loss based flow when the buffer is huge: the window the receiver allows.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
B iperf3 -s -D > /dev/null; sleep 0.5
A tc qdisc add dev va root tbf rate 10mbit burst 1600 limit 1500000
A iperf3 -c 10.9.0.2 -t 9 -C reno > /dev/null &
sleep 3
for t in 3 6; do
  A ss -tin dst 10.9.0.2 | tr '\n' ' ' | sed 's/ESTAB/\nESTAB/g' | grep -E 'unacked:[0-9]{2,}' | grep -oE 'rtt:[0-9.]+|cwnd:[0-9]+|unacked:[0-9]+|snd_wnd:[0-9]+' | tr '\n' ' ' | sed "s/^/after $t s: /"; echo
  sleep 3
done
wait
sysctl net.ipv4.tcp_rmem
```

```text
after 3 s: rtt:96.013 cwnd:207 unacked:82 rtt:0.002 snd_wnd:176128
after 6 s: rtt:91.997 cwnd:207 unacked:76 rtt:0.002 snd_wnd:176128
net.ipv4.tcp_rmem = 4096        131072  33554432
```

`unacked` was **76 to 82 packets**, about **110 to 119 KB** of 1,448 byte segments, which is the standing queue. The congestion window was **207** packets and the receiver offered **176,128 bytes** (`snd_wnd`), so **neither was the limit**. I did not find what was. A reasonable suspect is the sender's own accounting of bytes queued below the socket (**from memory**, called TCP small queues), but I did not test it. What I can say is that in this kernel a loss based flow stood at about 80 packets, and the model of section 5.2 uses a cap of 80 frames **taken from this observation**: it is a fit, not a prediction.

The lesson is what the table does not show: **a bigger buffer did not raise throughput at any size and cost 30 to 92 ms of delay (cubic, against the 10 frame buffer)**. On a real path with a 40 ms round trip a buffer helps up to a bandwidth delay product and then only adds delay.

### 5.2 What the model says, and how far to trust it

The kernel has no `fq_codel`, so the comparisons of this section and the next are run in `sim.py`, a packet level simulation of a bottleneck link with Reno flows (written for this tutorial, 263 lines). I checked it against the kernel runs above in the one case it can reproduce: **one Reno flow, 10 Mbit, 0.1 ms round trip, tail drop**.

```text
--- 2. validation against the kernel runs of e3: one Reno flow, 10 Mbit, 0.1 ms round trip, window capped at 80 frames
limit_pkts  goodput_Mbit  rtt_avg_ms  rtt_max_ms  drops_per_s
        10         10.00        11.5        13.3         13.1
        50         10.00        49.6        61.8          0.8
       250         10.00        96.9        96.9          0.0
      1000         10.00        96.9        96.9          0.0
```

| Buffer | Model round trip | Kernel round trip (cubic, reno) | Model over kernel |
|---|---|---|---|
| 10 frames (15,000 B) | 11.5 ms | 6.7, 6.7 ms | 72 % high |
| 50 frames (75,000 B) | 49.6 ms | 36.7, 33.5 ms | 35 to 48 % high |
| 250 frames (378,000 B) | 96.9 ms | 99.2, 98.3 ms | 1 to 2 % low |
| 1,000 frames (1.5 MB) | 96.9 ms | 99.1, 99.2 ms | 2 % low |

The model **agrees where the queue is capped by the window** (the two large buffers) and **overstates the queue where the buffer is the limit** (10 and 50 frames). The 80 frame cap was chosen after seeing the kernel's 76 to 82 unacked packets, so the large buffer rows are a fit. Use the model for what it was built for: the same traffic through different queue disciplines, where the overstatement is the same for each, and never as a prediction of a real network.

### 5.3 How much buffer

Three rules, and what each assumes.

1. **One bandwidth delay product** (`rate × RTT`) keeps the link full for **one** Reno flow, because the window halves on a loss and a buffer of one bandwidth delay product holds exactly the halved window's gap (**from memory**: the classic rule from the 1990s, Villamizar and Song).
2. **BDP divided by the square root of the number of flows**, for many flows (**from memory**: Appenzeller, Keslassy and McKeown, 2004). The flows lose at different times, so their windows do not all halve together and the total fluctuates less. I ran it in the model at 10 Mbit and a 100 ms round trip (BDP 125,000 bytes, 83 frames):

```text
--- 5. buffer sizing with many flows
10 Mbit, 100 ms round trip, BDP 83 frames; N long Reno flows; utilisation after 20 s
flows  buffer = BDP         BDP / sqrt(N)       BDP / 10
    1   83 frames 100.0 %    83 frames 100.0 %     8 frames  81.9 %
    4   83 frames 100.0 %    42 frames  98.3 %     8 frames  86.4 %
   16   83 frames 100.0 %    21 frames  97.3 %     8 frames  97.1 %
   36   83 frames 100.0 %    14 frames 100.0 %     8 frames 100.0 %
```

   With **36 flows** the link stayed full (**100 %**) with **14 frames** where one flow needed **83**; with 16 flows **21 frames** gave **97.3 %**. Even **8 frames** (a tenth of the BDP) gave 100 % with 36 flows, and only **81.9 %** with one. The fewer the flows, the more buffer the link needs; a backbone link with thousands of flows needs little, an access link with one or two needs the full product.
3. **A delay bound**: the buffer is at most `rate × the delay you accept`. A 50 ms bound on a 10 Mbit link is 62,500 bytes whatever the flow count, and on 1 Gbit it is 6.25 MB (the table in problem A). This is the rule for **interactive traffic**: it ignores throughput entirely.

The rules disagree when the path is long and the link is slow, and the tension is the whole subject: the buffer a bulk flow wants is the delay a voice flow cannot take. Section 6 is the answer that does not choose between them.

---

## 6. Active queue management

A tail drop queue discards a packet only when it is full. By then the queue has already been standing for a round trip or more, the loss tells the sender late, and in a queue shared by many flows the losses land on many of them in the same round trip so that they all halve together (**global synchronisation**, **from memory**). **Active queue management** (AQM) drops or marks earlier, before the buffer is full, so that senders slow while the queue is still short.

### 6.1 RED

**Random early detection** (**from memory**: Floyd and Jacobson, 1993) keeps an exponentially weighted average of the queue length and drops with a probability that rises with it. Below `minth` it never drops; above `maxth` it always drops; between them the probability climbs linearly to `maxp`. It is parameterised by four numbers, `minth`, `maxth`, `maxp` and the averaging weight `wq`, which is the problem with it: there is no setting that works for every link speed and flow mix, and the queue length it controls is in packets, not in time.

```python
class Red(Taildrop):
    def __init__(s, limit, minth, maxth, maxp, wq=0.002, ecn=False):
        super().__init__(limit); s.minth, s.maxth, s.maxp, s.wq, s.avg, s.ecn, s.rng = minth, maxth, maxp, wq, 0.0, ecn, random.Random(7)
    def enq(s, p, now):
        s.avg += s.wq * (len(s.q) - s.avg)
        if s.avg >= s.minth:
            pr = 1.0 if s.avg >= s.maxth else s.maxp * (s.avg - s.minth) / (s.maxth - s.minth)
            if s.rng.random() < pr:
                if s.ecn and p.ecn and s.avg < s.maxth: p.marked = True
                else: return [p]
        return super().enq(p, now)
```

```text
RED, minth 10, maxth 50, maxp 0.1: drop probability at an average queue of q frames
q  5  p 0.000
q 10  p 0.000
q 20  p 0.025
q 30  p 0.050
q 40  p 0.075
q 50  p 1.000
q 60  p 1.000
```

The numbers are the drop probability for `minth 10, maxth 50, maxp 0.1` as `ans_e.py` computes it: **0.025 at an average of 20 frames, 0.075 at 40, and 1.000 from 50**. The cliff at `maxth` is a known weakness; a variant softens it above `maxth` (**from memory**, gentle RED). In the model, `red 10/50/0.1` with a 1,000 frame buffer kept a probe's round trip at **53.6 ms** where a 1,000 frame tail drop queue gave **1,096 ms**, at **94.1 %** utilisation against 100 % (section 6.5).

### 6.2 ECN

With **explicit congestion notification** a queue sets the two ECN bits in the IP header to "congestion experienced" instead of dropping, and the receiver echoes it so that the sender halves its window without losing a packet (RFC 3168, **from memory**; the bits are the low two bits of the TOS byte, section 8; NET 12 uses ECN to drive DCQCN in a fabric). The model shows the point: **`red` dropped 202 packets and `red + ecn` dropped 3** for the same delay (53.4 against 53.6 ms) and the same short transfer time (127 against 129 ms). The cost is that both ends and every router on the path must cooperate, and a middlebox that clears the bits defeats it.

### 6.3 CoDel

CoDel (**controlled delay**, **from memory**: Nichols and Jacobson, RFC 8289) discards RED's tuning for a single idea: **measure how long each packet waited, not how long the queue is**. It records the **sojourn time** of each packet as it leaves. If the minimum sojourn time stays above a **target of 5 ms** for a whole **interval of 100 ms**, the queue is a standing queue and not a burst, and CoDel begins to drop the head of the queue. It then drops again after `interval / sqrt(count)` where `count` is the number of drops in this episode, so the rate of drops climbs until the sojourn time falls below the target for a moment. The 5 ms and 100 ms are defaults that suit a path with a round trip up to about a hundred milliseconds (**from memory**).

```text
--- 1. the control law of CoDel
count   next drop after (ms)   at 100 ms interval
    1      100.0               cumulative  100.0 ms
    2       70.7               cumulative  170.7 ms
    3       57.7               cumulative  228.4 ms
    4       50.0               cumulative  278.4 ms
    5       44.7               cumulative  323.2 ms
    6       40.8               cumulative  364.0 ms
    7       37.8               cumulative  401.8 ms
    8       35.4               cumulative  437.1 ms
    9       33.3               cumulative  470.5 ms
   10       31.6               cumulative  502.1 ms
```

The ramp is the control law: the first drop comes 100 ms into the standing queue, the second 70.7 ms later, the tenth 31.6 ms after the ninth. The square root is chosen to match how TCP responds: Reno's throughput varies as one over the square root of the drop probability (the Mathis formula, **from memory**), so a drop interval that shrinks as `1 / sqrt(count)` makes the drop rate climb in step with what TCP needs to slow by a given amount. Problem E computes the drop rate that N flows need and the time CoDel takes to reach it.

The implementation I used is the part of the model that follows the RFC as I remember it, and is the part most likely to differ from it:

```python
class CodelState:
    # RFC 8289 control law as I remember it: target 5 ms over a 100 ms interval, then drop at interval / sqrt(count).
    def __init__(s, target=0.005, interval=0.1, ecn=False):
        s.target, s.interval, s.ecn = target, interval, ecn
        s.first_above, s.dropping, s.count, s.lastcount, s.drop_next = 0.0, False, 0, 0, 0.0
    def law(s, t): return t + s.interval / math.sqrt(s.count)
    def ok_to_drop(s, p, now, backlog):
        if p is None: s.first_above = 0.0; return False
        if now - p.enq < s.target or backlog <= 1514: s.first_above = 0.0; return False
        if s.first_above == 0.0: s.first_above = now + s.interval; return False
        return now >= s.first_above
    def deq(s, pop, backlog, now):
        # pop() returns the next packet or None; backlog() the bytes left. Returns (packet or None, dropped list).
        dropped = []
        p = pop(); ok = s.ok_to_drop(p, now, backlog())
        if s.dropping:
            if not ok: s.dropping = False
            else:
                while now >= s.drop_next and s.dropping:
                    s.count += 1
                    if s.ecn and p.ecn: p.marked = True; s.drop_next = s.law(s.drop_next); break
                    dropped.append(p); p = pop(); ok = s.ok_to_drop(p, now, backlog())
                    if not ok: s.dropping = False
                    else: s.drop_next = s.law(s.drop_next)
        elif ok:
            if s.ecn and p.ecn: p.marked = True
            else: dropped.append(p); p = pop(); ok = s.ok_to_drop(p, now, backlog())
            s.dropping = True
            d = s.count - s.lastcount
            s.count = d if (d > 1 and now - s.drop_next < 16 * s.interval) else 1
            s.drop_next = s.law(now); s.lastcount = s.count
        return p, dropped
```

`ok_to_drop` is the test (sojourn under 5 ms, or one frame left, resets everything; a sojourn above target for a full interval turns it on), `law` is the `interval / sqrt(count)` step, and `deq` re-enters the dropping state after a recent episode at `count - lastcount` rather than at 1, so that a returning congestion is met with the old rate.

### 6.4 FQ-CoDel

CoDel controls how long a queue stands, but one queue still mixes a bulk flow with a voice flow, and a packet of the voice flow waits behind the bulk flow's backlog. **FQ-CoDel** (**from memory**: RFC 8290) hashes each flow (the 5 tuple) to one of many queues (1,024 by default), serves them by **deficit round robin** with a quantum of one MTU, and runs CoDel inside each queue. Two properties matter. **Isolation:** a flow with few packets waits only for the round, not for the backlog of the others. **New flow priority:** a queue that has just become active is served ahead of the ones that have been backlogged, so a sparse flow (a DNS lookup, a TCP handshake, a VoIP stream) is almost never delayed. The cost is per-flow state and a hash, and the limits that a hash brings: two flows can collide, and the queue is per flow, not per host, so a host with many flows gets many shares.

### 6.5 The same link under eleven disciplines

Everything above in one run of the model: **10 Mbit, 40 ms round trip, two bulk Reno flows, a probe every 100 ms (a flow that sends one packet and waits for the answer), and short transfers of 20 frames arriving every 0.8 s on average**. The probe shows what an interactive packet suffers, and the transfers show what a user sees loading a page while a download is running. `util` is link utilisation, `q p50` and `q p99` the bulk flows' own queue time, `probe rtt` the round trip of the probe, `fct` the completion time of a short transfer.

```text
--- 4. the same bottleneck under eleven queue disciplines
10 Mbit, 40 ms round trip (BDP 33 frames of 1,500 bytes), 2 bulk Reno flows, a probe every 100 ms, 20 frame transfers every 0.8 s on average
queue                    util    q p50   q p99     probe     p99       fct     p99     done  drops
                                    ms      ms    rtt ms      ms        ms      ms
taildrop 1000 frames   100.0%   1056.5  1125.0    1096.0  1165.4    2209.9  2329.3    52/52      0
taildrop 100 frames    100.0%     92.1   119.6     128.0   159.8     405.2  1083.0    52/52    180
taildrop 33 (BDP)      100.0%     26.1    39.3      64.3    79.4     258.5   578.8    52/52    420
taildrop 10             93.4%      3.3    11.7      44.1    52.0     199.6   488.1    52/52    592
red 10/50/0.1, 1000     94.1%     13.1    38.7      53.6    79.5     129.1   206.8    52/52    202
red + ecn               93.3%     12.9    42.9      53.4    83.5     127.4   204.0    52/52      3
codel                   98.3%      5.7    22.4      47.0    64.2     125.9   393.7    52/52    182
codel + ecn             97.8%      5.7    23.7      47.1    62.8     118.6   179.2    52/52      1
drr, tail drop         100.0%   1260.6  2458.0      40.7    41.3     114.4   135.8    52/52      0
fq_codel                94.7%      2.5    29.6      40.8    43.1     114.1   135.1    52/52    185
fq_codel + ecn          96.1%      2.5    32.1      40.8    43.0     114.1   135.3    52/52      0
```

Read the table in four groups.

1. **Tail drop, by buffer size.** A 1,000 frame buffer gave the probe **1,096 ms** and a short transfer **2,210 ms**; 100 frames, **128 ms** and **405 ms**; one bandwidth delay product (33 frames), **64.3 ms** and **258.5 ms**; **10 frames, 44.1 ms and 199.6 ms but with the link only 93.4 % busy**. Shrinking the buffer buys the probe its delay and costs the link its throughput, which is the tension of section 5.3.
2. **RED and CoDel**, with and without ECN, hold the probe at **47 to 54 ms** and the transfers at **119 to 129 ms**, **with the link 93 to 98 % busy**: CoDel is the best of them for utilisation (**98.3 %**). Marking instead of dropping (the `+ ecn` rows) cut the drops from 202 to 3 for RED and from 182 to 1 for CoDel, and brought the **99th percentile transfer time** of CoDel from **393.7 ms to 179.2 ms**.
3. **`drr, tail drop`** has **no AQM** and yet gave the probe **40.7 ms**, about the bare 40 ms of propagation, and transfers of **114.4 ms**: because the probe and the transfers are in queues of their own, they never wait behind the bulk flows. The bulk flows' own queue is **1,260.6 ms**, and nobody else pays for it. This is the point of per-flow queues: **isolation fixes the delay for the flows that are not bulk, and AQM fixes it for the bulk flows themselves**.
4. **`fq_codel`** gave the best of both: probe **40.8 ms**, transfers **114.1 ms**, bulk queue **2.5 ms** at the median, utilisation **94.7 to 96.1 %**, and with ECN **0 drops**. The 1,000 frame tail drop queue and `fq_codel + ecn` carry the same traffic: **1,096 ms and 2,210 ms** against **40.8 ms and 114.1 ms**, a factor of **27** and **19**.

The utilisation of `fq_codel` (94.7 %) is lower than CoDel's (98.3 %) in this run; I did not investigate whether it is the quantum, the per-flow limit or the two flows being served alternately. Treat the difference as small and unexplained.


---

## 7. Scheduling: who goes first

Sections 5 and 6 decided how long a queue may be. When there are several queues, or several flows in one, a **scheduler** decides which packet leaves next. Every scheduler is a trade between three things: **latency isolation** (a flow that sends little should not wait for one that sends a lot), **fairness** (shares of the rate) and **cost per packet** (a router decides a packet in nanoseconds).

### 7.1 FIFO, and why it is not fair

First in, first out gives each flow a share of the link in proportion to what it **offers**, and drops by arrival. I ran three sources, each offering 6 Mbit into a 10 Mbit link, with frames of 1,500, 750 and 150 bytes (model):

```text
--- 6. fairness with different frame sizes
10 Mbit link; three constant rate sources offering 6 Mbit each; frame sizes 1500, 750, 150 bytes; throughput of each in Mbit/s
fifo, tail drop          big 2.91  mid 3.26  small 3.83   total 10.00
round robin by packet    big 6.18  mid 3.18  small 0.64   total 10.00
DRR by byte              big 3.33  mid 3.33  small 3.33   total 10.00
DRR, weights 3:1:1       big 6.00  mid 2.00  small 2.00   total 10.00
```

The FIFO gave **2.91, 3.26 and 3.83 Mbit/s**: the source with the smallest frames got the most, because the queue dropped by packet and the small source sent five times as many packets per bit. Nothing in a FIFO knows what a flow is.

### 7.2 Strict priority

The scheduler always serves the highest non-empty band. The top band sees almost no queue: in `e5b` a probe in a high priority HTB class waited **0.033 ms** (average of 25 pings, maximum 0.082 ms) while a 12 Mbit flood in the lowest class filled a 10 Mbit link, and a probe with no marking, behind the same flood in a plain FIFO of 100 frames, waited **69.8 ms**.

```bash
# e5b_prio.sh: what a priority queue does to delay and to starvation. A best effort flood offers 12 Mbit to a 10 Mbit link, and ping probes ride in other classes or bands.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
A ip link set va txqueuelen 100
B python3 sink.py 70 5003 > /dev/null &
probe() { A ping -c 25 -i 0.2 -W 1 -q "$@" 10.9.0.2 | grep -E 'transmitted|rtt' | sed -E 's/, time [0-9]+ms//; s/^rtt //' | paste -sd' '; }
echo "--- 1. one 100 frame queue (plain pfifo): a probe waits behind the flood"
A tc qdisc add dev va root handle 1: tbf rate 10mbit burst 1600 limit 1000000
A tc qdisc add dev va parent 1:1 handle 10: pfifo limit 100
A python3 flood.py 10.9.0.2 5003 0x00 12 9 & F=$!; sleep 3
echo -n "probe, TOS 0x00: "; probe; wait $F
echo "--- 2. HTB, class 10 (DSCP EF, prio 0) and class 30 (the rest, prio 1): the flood fills class 30 only"
A tc qdisc del dev va root
A tc qdisc add dev va root handle 1: htb default 30
A tc class add dev va parent 1: classid 1:1 htb rate 10mbit ceil 10mbit
A tc class add dev va parent 1:1 classid 1:10 htb rate 4mbit ceil 10mbit prio 0
A tc class add dev va parent 1:1 classid 1:30 htb rate 6mbit ceil 10mbit prio 1
A tc filter add dev va parent 1: protocol ip prio 1 u32 match ip tos 0xb8 0xfc flowid 1:10
A python3 flood.py 10.9.0.2 5003 0x00 12 14 & F=$!; sleep 3
echo -n "probe, TOS 0x00: "; probe
echo -n "probe, EF 0xb8:  "; probe -Q 0xb8
A tc -s class show dev va | grep -A1 -E '^class htb 1:(10|30) ' | grep -oE 'class htb 1:[0-9]+|Sent [0-9]+ bytes [0-9]+ pkt' | paste - -; wait $F
echo "--- 3. pfifo_fast under the bucket: three strict priority bands chosen by the TOS byte (0x10 is band 0, 0x00 band 1, 0x08 band 2)"
A tc qdisc del dev va root
A tc qdisc add dev va root handle 1: tbf rate 10mbit burst 1600 limit 1000000
A tc qdisc add dev va parent 1:1 handle 10: pfifo_fast
A tc qdisc show dev va | sed -n 2p
A python3 flood.py 10.9.0.2 5003 0x00 12 26 & F=$!; sleep 3
for tos in 0x10 0x00 0x08; do echo -n "probe, TOS $tos: "; probe -Q $tos; done
wait $F
```

```text
--- 1. one 100 frame queue (plain pfifo): a probe waits behind the flood
probe, TOS 0x00: 25 packets transmitted, 25 received, 0% packet loss min/avg/max/mdev = 47.834/69.764/92.889/13.566 ms

--- 2. HTB, class 10 (DSCP EF, prio 0) and class 30 (the rest, prio 1): the flood fills class 30 only
probe, TOS 0x00: 25 packets transmitted, 25 received, 0% packet loss min/avg/max/mdev = 48.833/69.949/92.844/13.457 ms
probe, EF 0xb8:  25 packets transmitted, 25 received, 0% packet loss min/avg/max/mdev = 0.020/0.033/0.082/0.013 ms
class htb 1:10  Sent 2450 bytes 25 pkt
class htb 1:30  Sent 15956200 bytes 12788 pkt

--- 3. pfifo_fast under the bucket: three strict priority bands chosen by the TOS byte (0x10 is band 0, 0x00 band 1, 0x08 band 2)
qdisc pfifo_fast 10: parent 1:1 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
probe, TOS 0x10: 25 packets transmitted, 25 received, 0% packet loss min/avg/max/mdev = 0.017/0.036/0.081/0.018 ms
probe, TOS 0x00: 25 packets transmitted, 22 received, 12% packet loss min/avg/max/mdev = 53.840/84.397/88.379/6.726 ms
probe, TOS 0x08: 25 packets transmitted, 12 received, 52% packet loss min/avg/max/mdev = 5320.654/6442.244/7563.668/703.930 ms, pipe 25
```

The price is in the third run, `pfifo_fast` under the bucket with the same flood in band 1 (the flood uses TOS 0x00): the probe in band 0 (TOS 0x10) took **0.036 ms**; the probe in the **same band as the flood** (0x00) took **84 ms** and lost 12 %; the probe in band 2 (TOS 0x08) **lost 52 %, and the 12 replies that came back took 5.3 to 7.6 seconds**. A band above a saturated one gets only what that band leaves, which is nothing. That is **starvation**, and it is the reason a priority class must be bounded: by a policer, by a `ceil`, or by a guarantee that it is a small share of the link (section 10).

### 7.3 Round robin and deficit round robin

**Round robin** gives each queue one packet per turn. It is fair in packets, not in bytes, and the same experiment shows the cost: by packet, the 1,500 byte source got **6.18 Mbit/s**, the 750 byte **3.18** and the 150 byte **0.64**, because every turn moves ten times more bytes for the big frames than for the small. **Deficit round robin** (DRR, **from memory**: Shreedhar and Varghese, 1995) fixes the unit. Each queue has a **deficit counter**. At its turn the counter grows by a **quantum**, and the queue sends packets while the head packet fits in the counter, subtracting the size of each sent; what is left carries to the next turn, and an empty queue resets to zero. With the quantum at least one MTU, every turn sends at least one packet and the cost per packet is constant.

From the same table: DRR by byte gave **3.33 Mbit/s each**, and with weights 3:1:1 (a larger quantum for the first) **6.00, 2.00 and 2.00**. The cost of DRR is delay, set by the quantum and the number of active flows: the first packet of a new flow can wait for every other flow to send one quantum, `((N - 1) × quantum + 1514) × 8 / rate`. Problem D computes it:

```text
worst wait of the first packet of a new flow: the other N-1 flows each send one quantum (plus the frame in service)
N      quantum  10 Mbit        1 Gbit         rounds per 1,514 B frame
2      300      1.45 ms        0.015 ms       6
2      1514     2.42 ms        0.024 ms       1
2      9000     8.41 ms        0.084 ms       1
10     300      3.37 ms        0.034 ms       6
10     1514     12.11 ms       0.121 ms       1
10     9000     66.01 ms       0.660 ms       1
100    300      24.97 ms       0.250 ms       6
100    1514     121.12 ms      1.211 ms       1
100    9000     714.01 ms      7.140 ms       1
1000   300      240.97 ms      2.410 ms       6
1000   1514     1211.20 ms     12.112 ms      1
1000   9000     7194.01 ms     71.940 ms      1
```

With ten flows and a 1,514 byte quantum, a new flow waits up to **12.1 ms** on a 10 Mbit link and **0.12 ms** on 1 Gbit; with 100 flows **121 ms**; with a 9,000 byte quantum (jumbo frames) and 100 flows **714 ms**. A quantum of 300 bytes lowers the wait to 25 ms (N of 100) but takes **6 turns per 1,514 byte frame**, so a scheduler visits a queue six times to send one packet. The quantum is set near the MTU for that reason (**from memory**: FQ-CoDel uses 1,514 by default).

### 7.4 Weighted fair queueing

**WFQ** gives each packet a **finish tag**, the time at which it would finish if the link were shared by bits in proportion to the weights, and sends the packet with the smallest tag. It is the ideal that DRR approximates. `wfq.py` computes the order for three backlogged flows, with A at 1,000 byte packets, B at 300 and C at 600, and compares it with DRR at a 600 byte quantum:

```python
# wfq.py: the transmission order of weighted fair queueing (finish tags) against deficit round robin, for three backlogged flows.
import sys
flows = {"A": (1000, 8), "B": (300, 26), "C": (600, 13)}   # packet size in bytes, number of packets queued at time 0
def wfq(w):
    tags = []
    for f, (size, n) in flows.items():
        for k in range(1, n + 1): tags.append((k * size / w[f], f, size))
    tags.sort(); return [(f, s) for _, f, s in tags]
def drr(w, quantum):
    q = {f: [size] * n for f, (size, n) in flows.items()}; d = {f: 0 for f in flows}; out = []
    while any(q.values()):
        for f in flows:
            if not q[f]: continue
            d[f] += quantum * w[f]
            while q[f] and q[f][0] <= d[f]: d[f] -= q[f][0]; out.append((f, q[f].pop(0)))
            if not q[f]: d[f] = 0
    return out
def show(name, order, n=40):
    sent = {f: 0 for f in flows}
    for f, s in order[:n]: sent[f] += s
    print("%-28s %s" % (name, "".join(f for f, _ in order[:n])))
    print("%-28s bytes after %d packets: %s" % ("", n, "  ".join("%s %d" % (f, sent[f]) for f in flows)))
eq = {"A": 1, "B": 1, "C": 1}; wt = {"A": 2, "B": 1, "C": 1}
print("flows: A 1000 byte packets, B 300 byte, C 600 byte, all backlogged; the first 40 transmissions")
show("WFQ, equal weights", wfq(eq)); show("DRR quantum 600, equal", drr(eq, 600))
show("WFQ, weights A 2, B 1, C 1", wfq(wt)); show("DRR quantum 600, weights 2:1:1", drr(wt, 600))
```

```text
--- 7. weighted fair queueing against deficit round robin
flows: A 1000 byte packets, B 300 byte, C 600 byte, all backlogged; the first 40 transmissions
WFQ, equal weights           BBCBABCBBCABBCBABCBBCBABCBBCABBCBABCBBCB
                             bytes after 40 packets: A 6000  B 6900  C 6600
DRR quantum 600, equal       BBCABBCBBCABBCABBCBBCABBCBBCABBCABBCBBCA
                             bytes after 40 packets: A 7000  B 6600  C 6600
WFQ, weights A 2, B 1, C 1   BABCBABCABBCABBCABABCBABCBABCBBCBBCBBCBB
                             bytes after 40 packets: A 8000  B 6600  C 6000
DRR quantum 600, weights 2:1:1 ABBCABBCABBCABBCAABBCABBCABBCBBCBBCBBCBB
                             bytes after 40 packets: A 8000  B 6600  C 6000
```

Both give each flow about the same bytes: after 40 packets **6,000, 6,900 and 6,600** from WFQ and **7,000, 6,600 and 6,600** from DRR, within one packet of A (1,000 bytes) of each other; with weights 2:1:1 both give **8,000, 6,600 and 6,000**. The orders differ from the fourth packet on (WFQ `BBCBA`, DRR `BBCAB`): WFQ sends A when its finish tag (1,000) is the smallest, DRR at the first turn on which its accumulated deficit covers 1,000 bytes. The byte shares are alike and the order is not. The tags here ignore the virtual time that a real WFQ advances when a flow becomes idle and returns (the flows are all backlogged from time zero), so this shows the order and no more. WFQ has to sort tags (a heap, `O(log N)` per packet) and DRR does not, which is why DRR is what runs at line rate (**from memory**).

### 7.5 HTB: shares with a hierarchy

**Hierarchical token bucket** (HTB) is the scheduler in most Linux QoS setups. Each class has a **rate** (assured), a **ceil** (the most it may reach by borrowing), a **prio** (who borrows first) and a quantum (how a share of spare is cut, derived from the rate; **from memory**: `r2q`). A class that is under its rate sends; one that is over it asks its parent for tokens up to its ceil. The sharing rule, which `ans_c.py` implements as an allocator, is:

1. **Each class gets `min(demand, rate)`.** This is the guarantee, and it holds whatever the others do.
2. The spare capacity goes to the classes that still have demand, **lowest `prio` number first**.
3. **Inside one prio, spare is shared in proportion to the rates**, each class stopping at `min(demand, ceil)`.

I measured it with three classes at the top of a 10 Mbit bucket, a u32 filter on the TOS byte, and floods of 12 Mbit each for 12 s (the shares are the last 8 s):

```bash
mk() {  # mk R10 P10 R20 P20 R30 P30: rebuild the tree (rates in Mbit)
  A tc qdisc del dev va root 2> /dev/null
  A tc qdisc add dev va root handle 1: htb default 30
  A tc class add dev va parent 1: classid 1:1 htb rate 10mbit ceil 10mbit
  A tc class add dev va parent 1:1 classid 1:10 htb rate ${1}mbit ceil 10mbit prio $2
  A tc class add dev va parent 1:1 classid 1:20 htb rate ${3}mbit ceil 10mbit prio $4
  A tc class add dev va parent 1:1 classid 1:30 htb rate ${5}mbit ceil 10mbit prio $6
  A tc filter add dev va parent 1: protocol ip prio 1 u32 match ip tos 0xb8 0xfc flowid 1:10
  A tc filter add dev va parent 1: protocol ip prio 2 u32 match ip tos 0x48 0xfc flowid 1:20
}
```

```text
--- 1. rates 2, 2, 2 Mbit; class 10 has prio 1, classes 20 and 30 prio 2; everyone offers 12 Mbit
class htb 1:10 parent 1:1 prio 1 rate 2Mbit ceil 10Mbit
class htb 1:20 parent 1:1 prio 2 rate 2Mbit ceil 10Mbit
class htb 1:30 parent 1:1 prio 2 rate 2Mbit ceil 10Mbit
1:10 6.00 Mbit/s  1:20 2.00 Mbit/s  1:30 2.00 Mbit/s

--- 2. the same, but class 10 sends nothing: the spare 6 Mbit goes to the equal priority classes
1:10 0.00 Mbit/s  1:20 5.00 Mbit/s  1:30 5.00 Mbit/s

--- 3. rates 1, 3, 1 Mbit, all prio 2; everyone offers 12 Mbit: the 5 Mbit of spare is shared by rate
1:10 2.00 Mbit/s  1:20 6.00 Mbit/s  1:30 2.00 Mbit/s

--- 4. rates 1, 3, 1 Mbit, all prio 2; class 20 offers only 1 Mbit: it keeps what it needs and the rest is shared
1:10 4.50 Mbit/s  1:20 1.00 Mbit/s  1:30 4.50 Mbit/s
```

Run 1: guarantees of 2, 2 and 2 Mbit leave 4 Mbit spare, and class 10 (prio 1, better than the 2 of the others) takes all of it: **6.00, 2.00, 2.00**. Run 2: with class 10 idle, the 6 Mbit it does not use goes to the other two, equal in prio and rate: **0.00, 5.00, 5.00**. Run 3: rates 1, 3 and 1 at equal prio, spare 5 Mbit shared 1:3:1: **2.00, 6.00, 2.00**. Run 4: class 20 offers only 1 Mbit, so it keeps **1.00** and the spare 7 Mbit goes to the other two, equal rates: **4.50 and 4.50**. `ans_c.py` reproduces all four, and gives three more that I did not run on the kernel: with `ceil 6` on class 10 the result is unchanged (**6.00, 2.00, 2.00**, since 6 is where it ended anyway); rates 5, 3, 2 at equal prio with the first class offering 3: **3.00, 4.20, 2.80**; rates 3, 3, 3 with three different prios: **4.00, 3.00, 3.00** (the first takes the 1 Mbit of spare). Those three are the rule applied, not measurements.

HTB's strength is exactly this: **a guarantee, a cap, and a priority for borrowing**, in one tree with a leaf qdisc under each class. Its weaknesses: it is a software scheduler with a lock on the tree (**from memory**, the cost at 10 Gbit and up is why hardware does this), its rates need `r2q` or a quantum tuned for very low or very high rates, and a class cannot guarantee delay, only rate.

### 7.6 What each scheduler gives

| Scheduler | Isolates latency | Fair in bytes | Guarantees a rate | Starves | Per packet cost |
|---|---|---|---|---|---|
| FIFO | no | no (by offer) | no | no | constant |
| Strict priority | top band only | no | the top band | the lowest band (measured) | constant, few bands |
| Round robin | yes | no, by packet | no | no | constant |
| DRR | yes (one round) | yes | by weight | no | constant if quantum is at least the MTU |
| WFQ | yes, best | yes | by weight | no | `O(log N)` |
| HTB | per class | per class by rate | yes (rate) | only by prio above the guarantee | a tree walk, a lock |

---

## 8. Classification and marking

A scheduler needs to know which class a packet is in. That is **classification**, done by a filter (`u32` on a header field, `cls_bpf`, an `nftables` rule) or by a mark the packet already carries. The marks are three:

- **DSCP**, the top six bits of the IP TOS byte (traffic class in IPv6): `EF` is 46 (voice), `AF21` is 18, `CS1` is 8 (a common scavenger or "lower effort" class), `CS0` is 0 (default), `CS6` and `CS7` are 48 and 56 (network control) (**from memory**: RFC 2474, 2597, 3246; the meaning of a value is a convention that each network is free to ignore).
- **ECN**, the low two bits of the same byte: `00` not capable, `10` and `01` capable, `11` congestion experienced (section 6.2).
- **802.1p**, the three priority bits of an 802.1Q tag (the PCP), which are the top three bits of the DSCP by a common default (vendors differ).

The TOS byte on the wire is `DSCP << 2 | ECN`. I set it and read it back with `nftables` and `tcpdump`:

```bash
# e6_dscp.sh: marking, classification and remarking. nftables sets the DSCP, tcpdump shows the byte, HTB classes follow the packet priority, an ingress rule rewrites a mark the application then sees, and a hand built 802.1Q frame carries the priority bits.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
A nft add table ip t; A nft "add chain ip t o { type filter hook output priority 0; }"
echo "--- 1. nftables marks by destination port; the TOS byte on the wire is the DSCP shifted left two bits plus the two ECN bits"
A nft add rule ip t o udp dport 5001 ip dscp set ef
A nft add rule ip t o udp dport 5002 ip dscp set af21
A nft add rule ip t o udp dport 5003 ip dscp set cs1
A nft list chain ip t o | sed -n 4,6p | sed 's/^[[:space:]]*//'
B tcpdump -l -nn -v -i vb -c 3 udp > /tmp/dump.txt 2> /dev/null &
sleep 0.5
for p in 5001 5002 5003; do A python3 flood.py 10.9.0.2 $p 0 0.5 0.001; done
wait %1
grep -oE 'tos 0x[0-9a-f]+|> 10.9.0.2.[0-9]+' /tmp/dump.txt | paste - - | sed 's/> 10.9.0.2./to port /'
echo "--- 2. HTB picks the class from the packet priority when its major number equals the qdisc handle: no tc filter at all"
A tc qdisc add dev va root handle 1: htb default 30
A tc class add dev va parent 1: classid 1:10 htb rate 4mbit ceil 10mbit
A tc class add dev va parent 1: classid 1:20 htb rate 3mbit ceil 10mbit
A tc class add dev va parent 1: classid 1:30 htb rate 3mbit ceil 10mbit
A nft flush chain ip t o
A nft add rule ip t o udp dport 5001 meta priority set 1:10
A nft add rule ip t o udp dport 5002 meta priority set 1:20
A nft list chain ip t o | sed -n 4,5p | sed 's/^[[:space:]]*//'
B python3 sink.py 3 5001 5002 5003 > /dev/null &
for p in 5001 5002 5003; do n=$((p - 5000)); A python3 flood.py 10.9.0.2 $p 0 5 0.0$((n * 3)); done
sleep 0.3
A tc -s class show dev va | grep -A1 -E '^class htb 1:(10|20|30) ' | grep -oE 'class htb 1:[0-9]+|Sent [0-9]+ bytes [0-9]+ pkt' | paste - -
wait
echo "--- 3. an ingress rule rewrites the mark before the socket sees it (the sender used EF, 0xb8)"
B python3 sink.py 2 5001 > /tmp/s1.txt &
sleep 0.3; A python3 flood.py 10.9.0.2 5001 0xb8 1 0.01; wait; cat /tmp/s1.txt | sed 's/^/before the rule: /'
B nft add table ip t; B nft "add chain ip t i { type filter hook prerouting priority -150; }"
B nft add rule ip t i ip saddr 10.9.0.1 ip dscp set cs0
B python3 sink.py 2 5001 > /tmp/s2.txt &
sleep 0.3; A python3 flood.py 10.9.0.2 5001 0xb8 1 0.01; wait; cat /tmp/s2.txt | sed 's/^/after the rule:  /'
echo "--- 4. 802.1Q priority: a hand built frame per DSCP, the priority code point is the top three bits (a common default; vendors differ)"
B tcpdump -l -e -nn -v -i vb -c 4 vlan > /tmp/dot.txt 2> /dev/null &
sleep 0.5
A python3 dot1q.py va 46 18 8 0
wait %1
grep -oE 'vlan 10, p [0-9]|tos 0x[0-9a-f]+' /tmp/dot.txt | paste - -
```

```text
--- 1. nftables marks by destination port; the TOS byte on the wire is the DSCP shifted left two bits plus the two ECN bits
udp dport 5001 ip dscp set ef
udp dport 5002 ip dscp set af21
udp dport 5003 ip dscp set cs1
tos 0xb8        to port 5001
tos 0x48        to port 5002
tos 0x20        to port 5003
--- 2. HTB picks the class from the packet priority when its major number equals the qdisc handle: no tc filter at all
udp dport 5001 meta priority set 1:10
udp dport 5002 meta priority set 1:20
class htb 1:10  Sent 20000 bytes 16 pkt
class htb 1:20  Sent 38750 bytes 31 pkt
class htb 1:30  Sent 57500 bytes 46 pkt
--- 3. an ingress rule rewrites the mark before the socket sees it (the sender used EF, 0xb8)
before the rule: port 5001: 1 packets, TOS seen by the application: 0xb8
after the rule:  port 5001: 1 packets, TOS seen by the application: 0x00
--- 4. 802.1Q priority: a hand built frame per DSCP, the priority code point is the top three bits (a common default; vendors differ)
vlan 10, p 5    tos 0xb8
vlan 10, p 2    tos 0x48
vlan 10, p 1    tos 0x20
vlan 10, p 0    tos 0x0
```

```python
# dot1q.py IFACE DSCP...: send one hand built 802.1Q frame per DSCP value, with the priority code point set to the top three bits of the DSCP.
import socket, struct, sys
iface = sys.argv[1]; s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW); s.bind((iface, 0))
for d in map(int, sys.argv[2:]):
    pcp = d >> 3; tci = (pcp << 13) | 10
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, d << 2, 20 + 8 + 4, 0, 0, 64, 17, 0, socket.inet_aton("10.9.0.1"), socket.inet_aton("10.9.0.2"))
    udp = struct.pack("!HHHH", 5000, 5000, 12, 0) + b"hi!!"
    s.send(b"\xff" * 6 + b"\x02\x00\x00\x00\x00\x01" + struct.pack("!HHH", 0x8100, tci, 0x0800) + ip + udp)
```

What each section shows.

1. **DSCP to TOS.** `ip dscp set ef`, `af21` and `cs1` produced **`0xb8`, `0x48` and `0x20`**, that is `46 × 4`, `18 × 4` and `8 × 4`. The numbers in a `tcpdump -v` line are the whole byte, not the DSCP.
2. **HTB by `skb->priority`.** Setting `meta priority` to `1:10` or `1:20` put the packet in that class with **no `tc filter` at all**, because HTB uses the priority as the class id when its major number is the qdisc handle. The three classes counted **16, 31 and 46 packets** from floods of three different paces on ports 5001, 5002 and 5003, the last with no mark and so the default class `1:30`. This is the cheap way to classify: one rule marks, HTB picks.
3. **A mark is rewritten at a trust boundary.** An ingress rule at the receiver changed EF to `cs0` and the application read **`0x00`** where the sender had sent **`0xb8`**. A network owner who does not trust the sender's marks clears or remaps them at the edge, and that is routine: **do not design a service that depends on a mark surviving the internet**. Within one's own administrative domain it is the contract.
4. **802.1p.** The hand built frames with DSCP 46, 18, 8 and 0 carried priorities **5, 2, 1 and 0**, the top three bits of the DSCP.

Where the mark is set is a design decision, not a detail. **Trust the host?** Marks set by applications are free to anyone, so a switch that honours EF from every port lets any host take the voice queue; the usual answer is to **remark at the edge** based on a classifier (a port, a subnet, a protocol) and to police the voice class. **Classify by flow, not by claim**, where the cost is acceptable.

---

## 9. Where the queues are

A packet waits in more places than the qdisc. Going from a sender to a receiver across a switch:

| Place | Who manages it | How to see it |
|---|---|---|
| Socket send buffer (`SO_SNDBUF`) | the application and the kernel autotuning | `ss -tm` (**from memory**) |
| Qdisc on the egress device | `tc` | `tc -s qdisc show dev X`, the backlog line (section 2) |
| Driver transmit ring and NIC | the driver; byte queue limits cap it (**from memory**) | `ethtool -g`, `ethtool -S` (**from memory**) |
| Switch egress queue per port and class | the switch ASIC, a shared buffer pool (**from memory**) | switch counters, ECN or PFC counters |
| Virtual output queues inside a switch | the ASIC, to avoid head of line blocking (**from memory**) | vendor specific |
| Receive ring, softirq backlog | the driver and the kernel (`netdev_max_backlog`, **from memory**) | `/proc/net/softnet_stat`, `ethtool -S` (**from memory**) |
| Socket receive buffer | the application and the kernel | `ss -tm`, `netstat -s` drops (**from memory**) |
| Wi-Fi: per station queues, airtime | `mac80211` with a per station FQ-CoDel and an airtime scheduler (**from memory**) | `iw`, debugfs (**from memory**) |

Two points of method follow. **The standing queue is where the rate steps down**: the first link that is slower than the flow's rate, which is usually the access link, a WAN circuit or a shaper, and every other queue on the path is nearly empty. So a latency problem is solved by finding that link and putting the AQM or the shaper **there**: a queue managed at a fast interface that is not the bottleneck does nothing, which is why home routers are set to shape the uplink a little below the line rate, so that the queue forms in their own FQ-CoDel and not in the modem (**from memory**). **PFC and ECN in a lossless fabric** (NET 12) are the same problem at 100 Gbit with microsecond queues: a switch queue crossing a threshold marks (ECN) or pauses (PFC) instead of dropping, and incast is queueing caused by synchronised senders.

The measurement that shows where the queue is, for a Linux path, is a round trip under load against idle, as in section 5: the difference is the standing queue, and `tc -s qdisc` on each hop you control says which one.

---

## 10. A QoS design, end to end

The task: a 10 Mbit uplink carrying voice, interactive traffic and bulk. The design constraints are the ones this tutorial measured.

1. **Shape at the edge, below the line rate**, so that the queue is yours and not the modem's. Here that is the `htb` root at the full 10 Mbit (a real DSL or cable uplink needs a few percent less, **from memory**; I could not test it, since the veth has no modem behind it).
2. **Three classes**: voice (EF) with the best priority and a small rate, interactive (AF21) at the next, everything else as the default.
3. **Voice sized by arithmetic**, not by feel. G.711 (**from memory**) sends 160 bytes of payload every 20 ms; with the 40 bytes of RTP, UDP and IP and 18 of Ethernet that is 218 bytes, `218 × 8 × 50 = 87.2` kbit/s a call, so **1 Mbit carries 11 calls**.
4. **A cap on the priority class**, `ceil 2 Mbit`, so a misbehaving voice source cannot starve the others (section 7.2).
5. **Guarantees that add up** to the link: 1 + 4 + 5 = 10 Mbit.

```bash
# e9_design.sh: a three class design on a 10 Mbit uplink: voice (EF) with the best priority, interactive (AF21), everything else. The voice class is capped, so a misbehaving voice source cannot starve the rest.
cd "$(dirname "$0")"; . ./qnet.sh; q_up; trap q_down EXIT
A ip link set va txqueuelen 100
B python3 sink.py 400 5001 5002 5003 > /dev/null &
A tc qdisc add dev va root handle 1: htb default 30
A tc class add dev va parent 1: classid 1:1 htb rate 10mbit ceil 10mbit
A tc class add dev va parent 1:1 classid 1:10 htb rate 1mbit ceil 2mbit prio 0
A tc class add dev va parent 1:1 classid 1:20 htb rate 4mbit ceil 10mbit prio 1
A tc class add dev va parent 1:1 classid 1:30 htb rate 5mbit ceil 10mbit prio 2
A tc filter add dev va parent 1: protocol ip prio 1 u32 match ip tos 0xb8 0xfc flowid 1:10
A tc filter add dev va parent 1: protocol ip prio 2 u32 match ip tos 0x48 0xfc flowid 1:20
```

I tested three conditions: normal voice (1 Mbit offered), no voice but a probe, and a voice source that offers 12 Mbit, always with 6 Mbit of interactive and 12 Mbit of best effort offered:

```bash
run() {  # run EF AF21 BEST_EFFORT: offered Mbit/s of each flood for 12 s, a voice sized probe in EF during the last 8 s
  A python3 flood.py 10.9.0.2 5001 0xb8 $1 12 & p1=$!
  A python3 flood.py 10.9.0.2 5002 0x48 $2 12 & p2=$!
  A python3 flood.py 10.9.0.2 5003 0x00 $3 12 & p3=$!
  sleep 3; A python3 htbshare.py va 8 & h=$!
  A ping -c 40 -i 0.2 -W 1 -q -Q 0xb8 10.9.0.2 | grep -E 'rtt' | sed 's/^rtt /EF probe /'
  wait $p1 $p2 $p3 $h; sleep 1
}
```

```text
--- 1. voice offers 1 Mbit, interactive 6, best effort 12
EF probe min/avg/max/mdev = 0.035/10.633/23.520/8.566 ms
1:10 1.00 Mbit/s  1:20 4.00 Mbit/s  1:30 5.00 Mbit/s
--- 2. voice offers nothing but the probe: interactive 6, best effort 12
EF probe min/avg/max/mdev = 0.025/0.053/0.117/0.016 ms
1:10 0.01 Mbit/s  1:20 5.00 Mbit/s  1:30 5.00 Mbit/s
--- 3. the voice source misbehaves and offers 12 Mbit
1:10 1.00 Mbit/s  1:20 4.00 Mbit/s  1:30 5.00 Mbit/s
EF probe min/avg/max/mdev = 523.829/730.416/932.916/136.493 ms, pipe 5
```

1. **Normal voice.** Shares **1.00, 4.00 and 5.00 Mbit/s**: every class got exactly its guarantee, because with 1 + 4 + 5 = 10 and every class offering at least its rate there is nothing to borrow. The EF probe averaged **10.6 ms** (maximum 23.5 ms): the probe shares the class queue with the 1 Mbit voice flood, whose 1,250 byte packets take 10 ms each at the class rate, so the probe waits behind a few of **its own class's** packets. Priority isolates a class from other classes, and does not fix a class that is itself at its rate.
2. **Voice idle.** The probe took **0.053 ms** (maximum 0.117 ms) against 69.8 ms behind a flood in a FIFO (section 7.2), and the interactive and best effort classes split the unused megabit: **5.00 and 5.00**.
3. **Voice misbehaves** (12 Mbit offered). The class was held to **1.00 Mbit/s** and the others still got **4.00 and 5.00**. The cost was inside the voice class: the probe, in the same class as the flood, took **730 ms** on average (524 to 933 ms), the order of magnitude of a 100 frame queue of 1,250 byte frames draining at 1 Mbit (1.0 s when full). A strict priority queue without a cap would have given the voice flood 10 Mbit and starved the rest, as in section 7.2.

The result is a design with three properties that each have a measurement behind them: **bounded delay for the class that needs it** (0.053 ms), **a floor for every class** (4.00 and 5.00 under attack) and **a ceiling for the one with priority**. What it does not give is protection **inside** a class, and per-flow fairness inside the interactive and bulk classes is what FQ-CoDel as the leaf qdisc adds (section 6.4; not available in this kernel, so not measured here).


---

## 11. Common mistakes

1. **Sizing a buffer by what memory allows instead of by delay.** A 1.5 MB buffer in front of a 10 Mbit link gave cubic and reno a round trip of **99 ms** against **0.036 ms** idle, and no more throughput than a 15 KB buffer (section 5.1). Size to `rate × the delay you accept` (problem A).
2. **Judging a queue by its mean.** At load 0.95 the mean wait was 10.35 ms and the 99th percentile **61.1 ms** (section 1.2). Latency targets belong on a percentile.
3. **Forgetting that a queue is invisible when the link is fast.** On 1 Gbit an M/D/1 queue reaches a 1 ms mean wait only at a load of **0.994**; what you see there is a microburst, not a high average (problem A).
4. **Testing with the default congestion control and concluding there is no bufferbloat.** BBR held **9 ms** at every buffer size from 75 KB up where cubic and reno held **99 ms** (section 5.1). Name the algorithm in every test.
5. **A token bucket with a burst of one frame.** At 100 Mbit a 1,600 byte burst reached **88 Mbit/s** of the **95.6** that fits, and 3,000 bytes reached **95.2** (section 3.2).
6. **Reading `tc -s` packet counts as wire frames.** The counts are skbs, and a TSO or GSO skb is one count for up to 64 KB; `tbf` segments an skb larger than its burst (section 2).
7. **Measuring a policer on the sender's own output hook.** It gave **0 retransmissions** and a rate of 5.33 Mbit/s because the drop is a local error; the same rule at the receiver's ingress gave **686 to 983** retransmissions (section 4). Test a policer where the drop is a loss on the wire.
8. **Giving a policer a burst of a few packets on a long path.** In the model a **1,500 byte** burst held Reno to **0.30 Mbit/s** of a 5 Mbit contract at 40 ms, and **25,000 bytes** reached **4.18** (section 4.1). The burst is a bandwidth delay product, not a frame.
9. **Expecting the configured rate as goodput.** Five Mbit of IP bytes is 4.83 Mbit/s of TCP payload; the policers delivered **5.07 to 5.24** and I do not know why (section 4). Compare against the counted bytes, not the label.
10. **Strict priority with no bound.** In `pfifo_fast` under a flood the lowest band lost **52 %** and the replies that returned took **5.3 to 7.6 s** (section 7.2). Bound a priority class by a rate, a ceil or a policer.
11. **Counting packets, not bytes, in a fair scheduler.** Round robin by packet gave the 150 byte source **0.64 Mbit/s** of 6 offered, DRR **3.33** (section 7.3).
12. **A DRR quantum far from the MTU.** At 300 bytes a 1,514 byte frame needs **6 turns**; at 9,000 bytes with 100 flows a new flow waits up to **714 ms** at 10 Mbit (problem D).
13. **Believing a guarantee holds a class's delay.** HTB guaranteed the voice class its rate, and a voice source offering 12 Mbit still queued **730 ms** inside its own class (section 10). A class bounds a rate; per-flow queues bound a delay.
14. **Letting the sum of guarantees exceed the link.** The three guarantees in section 10 add to exactly 10 Mbit, so no class can be promised more than the others leave. Check the sum before you check the borrowing.
15. **Depending on a DSCP mark to survive.** An ingress rule at the receiver rewrote EF `0xb8` to `0x00` before the application read it (section 8). Treat marks as valid inside one administrative domain.
16. **Reading the model as a prediction.** It overstated the mean queue by **72 %** at a 10 frame buffer and by 35 to 48 % at 50 (section 5.2). It compares disciplines under the same load; it does not predict a network.
17. **Turning on ECN at one end.** Marking needs a sender that reacts, a receiver that echoes and routers that do not clear the bits (section 6.2). The model's `red` dropped **202** packets and `red + ecn` dropped **3**, but only when every part of the path took part.
18. **Shaping above the line rate.** If the shaper's rate is higher than the modem's, the queue forms in the modem, where you have no AQM (section 9; from memory for real modems).

---

## 12. Twelve interview questions

Each has an outline of a good answer; the section to go back to is in brackets.

1. **What is bufferbloat?** A buffer much larger than the path needs, kept full by a flow that finds the rate only by losing a packet, so every other packet pays the queue. Measured: a 1.5 MB buffer at 10 Mbit put a standing queue of 121 KB, **99 ms** of round trip against 0.036 ms idle; BBR held 9 ms. [5.1]
2. **State Little's law and use it.** `L = λ × W`: the bytes in a queue divided by the rate are the delay. A 125,000 byte buffer on 10 Mbit is 100 ms when full; backlog sampled from `tc` agreed with the measured delay within a few percent. [1.1, 5.1]
3. **How does queueing delay grow with load?** As `1 / (1 - ρ)`: M/D/1 mean wait `ρ S / (2 (1 - ρ))`. Measured 0.62, 2.13, 4.70 and 10.35 ms at loads 0.50, 0.80, 0.90 and 0.95, with the 99th percentile at 61 ms for 0.95. Real traffic is burstier than Poisson, so the formula is a lower bound. [1.2]
4. **What does a token bucket guarantee?** At most `b + r × t` bytes in any interval `t`. Measured exactly: 15,000 bytes in the first millisecond and 1,015,000 by one second at 8 Mbit with a 15,000 byte burst. [3]
5. **Policing against shaping: what does each do to TCP?** A shaper queues the excess, adding delay (+30 ms measured) and few losses (32 retransmissions); a policer drops it, adding no delay and many losses (686 to 983), and needs a burst of a bandwidth delay product on a long path. [4]
6. **Why do loss-based flows fill a big buffer, and what is the fix?** They only learn the rate from loss, so they grow the window until the buffer overflows. Fixes: a smaller buffer, an AQM that drops or marks early (CoDel), a model based sender (BBR), or per-flow queues. [5, 6]
7. **How does CoDel work?** It measures each packet's sojourn time. If the minimum stays above 5 ms for 100 ms it starts dropping at the head, then at `interval / sqrt(count)` so the rate climbs until the queue drains. It needs no tuning for the link speed. [6.3]
8. **What does FQ-CoDel add?** A hash of each flow into its own queue, served by DRR with new flows first, and CoDel inside each. A sparse flow never waits behind a bulk one. Modelled: probe round trip 40.8 ms against 1,096 ms for a 1,000 frame tail drop queue, and 114 ms short transfers against 2,210 ms. [6.4, 6.5]
9. **Strict priority, DRR, WFQ and HTB: when each?** Strict priority for a small, bounded, delay critical class; DRR for fair bulk at line rate; WFQ for exact weights where the sort is affordable; HTB where you need a guarantee, a ceiling and a borrowing order in one tree. [7]
10. **Why must a priority class be bounded?** It starves the rest: the lowest band in `pfifo_fast` got no reply for 5.3 s. A `ceil` and a guarantee for every class keep it honest; with a 12 Mbit voice source the other classes still got 4 and 5 Mbit. [7.2, 10]
11. **What is DSCP, and who may trust it?** Six bits of the TOS byte (EF 46 gives 0xb8), a convention not a guarantee. A network trusts marks only from its own edge; the edge remaps or clears the rest. [8]
12. **Design QoS for a branch uplink with voice.** Shape slightly below the line rate, mark at the edge, a capped priority class for voice sized by arithmetic (87.2 kbit/s a call, 11 calls per Mbit), guarantees that sum to the link, FQ-CoDel or per-flow queues for the rest, and measure the round trip under load. [10, 9]

---

## 13. Problems

Do each on paper first, then with the script named. The scripts run from `/home/claude/net14` (or your copy of `prog/`; `sim.py` is imported by `ans_e.py`), and the parameters are at the top of each file. Where to solve: **A, B and D** on paper or in a Python shell, with the script as the check; **C** on paper, then run the allocator and try the kernel with `e5_htb.sh`; **E** as a script, since it needs the simulator.

**Problem A (paper, then `ans_a.py`): how much buffer for a delay bound?** For links of 1 Mbit to 100 Gbit and queue delay bounds of 5, 20 and 100 ms, how many bytes (and 1,500 byte frames) does the buffer hold? At what load does the mean wait of an M/D/1 queue with 1,500 byte frames reach 0.1, 1 and 5 ms on 10 Mbit to 10 Gbit? *Approach:* the first is `rate × delay`; for the second solve `wait = ρ S / (2 (1 - ρ))` for ρ; then run `python3 prog/ans_a.py`.

**Problem B (paper, then `ans_b.py`): size a token bucket.** For 1 Mbit to 1 Gbit and round trips of 1, 20 and 100 ms: the bandwidth delay product, the burst the old rule (`rate / HZ`) asks for at HZ of 250 and 1,000, and the `tbf` limit for a 50 ms latency bound. Compare the old rule with the burst that section 3.2 measured. *Approach:* BDP is `rate × RTT`; the limit is `rate × 0.05 + burst`; then run `python3 prog/ans_b.py` and check one row against the numbers of `e8_burst.sh`.

**Problem C (paper, then `ans_c.py`): predict an HTB allocation.** Three classes on a 10 Mbit link. (i) Rates 2, 2, 2, prios 1, 2, 2, with a `ceil` of 6 on the first; (ii) rates 5, 3, 2, all prio 2, the first class offering only 3; (iii) rates 3, 3, 3, prios 0, 1, 2. All other classes offer 12 Mbit. What does each class get? *Approach:* apply the three rules of section 7.5 in order (guarantee, spare by prio, spare by rate inside a prio); run `python3 prog/ans_c.py`; then try one case on the kernel by editing `mk` in `e5_htb.sh`.

**Problem D (paper, then `ans_d.py`): DRR delay.** For 2, 10, 100 and 1,000 flows and quanta of 300, 1,514 and 9,000 bytes: the longest a new flow's first packet can wait on 10 Mbit and 1 Gbit, and how many turns a 1,514 byte frame needs at each quantum. *Approach:* the other `N - 1` flows each send a quantum, plus the frame in service: `((N - 1) × quantum + 1514) × 8 / rate`; turns are `ceil(1514 / quantum)`; run `python3 prog/ans_d.py`.

**Problem E (script, `ans_e.py`): what drop rate do Reno flows need, and when does CoDel supply it?** For 1, 4 and 16 Reno flows sharing 10 Mbit at a 100 ms round trip: the drop probability and drops per second that hold each flow to its share (the Mathis formula), the drops per second the model's tail drop and CoDel queues produced, how many CoDel drops in a row that needs and how long the ramp takes; and the RED drop probability at an average queue of 20, 30 and 40 frames for `minth 10, maxth 50, maxp 0.1`. *Approach:* `p = (1.22 × MSS × 8 × N / (RTT × C))²`; drops per second is `p` times 833 packets per second; CoDel's `count` is `(drops per second × interval)²`; then run `python3 prog/ans_e.py` (about 20 s).

---

## 14. Answers

### Problem A: buffer for a delay bound

```python
# ans_a.py: how much buffer holds a delay bound, and at what load an M/D/1 queue reaches a mean wait.
print("buffer that holds at most a given queue delay, in bytes and in 1,500 byte frames")
print("%-10s %-22s %-22s %-22s" % ("link", "5 ms", "20 ms", "100 ms"))
for name, bps in (("1 Mbit", 1e6), ("10 Mbit", 10e6), ("100 Mbit", 100e6), ("1 Gbit", 1e9), ("10 Gbit", 10e9), ("100 Gbit", 100e9)):
    print("%-10s " % name + " ".join("%-22s" % ("%d B (%d)" % (bps * ms / 8000, bps * ms / 8000 / 1500)) for ms in (5, 20, 100)))
print()
print("load at which the mean wait of an M/D/1 queue (1,500 byte frames) reaches a target: rho = 2w / (S + 2w)")
print("%-10s %-10s %-12s %-12s %-12s" % ("link", "S (frame)", "w = 0.1 ms", "w = 1 ms", "w = 5 ms"))
for name, bps in (("10 Mbit", 10e6), ("100 Mbit", 100e6), ("1 Gbit", 1e9), ("10 Gbit", 10e9)):
    S = 1500 * 8 / bps
    print("%-10s %-10s " % (name, "%.1f us" % (S * 1e6)) + " ".join("%-12s" % ("%.3f" % (2 * w / (S + 2 * w))) for w in (1e-4, 1e-3, 5e-3)))
```

```text
buffer that holds at most a given queue delay, in bytes and in 1,500 byte frames
link       5 ms                   20 ms                  100 ms
1 Mbit     625 B (0)              2500 B (1)             12500 B (8)
10 Mbit    6250 B (4)             25000 B (16)           125000 B (83)
100 Mbit   62500 B (41)           250000 B (166)         1250000 B (833)
1 Gbit     625000 B (416)         2500000 B (1666)       12500000 B (8333)
10 Gbit    6250000 B (4166)       25000000 B (16666)     125000000 B (83333)
100 Gbit   62500000 B (41666)     250000000 B (166666)   1250000000 B (833333)

load at which the mean wait of an M/D/1 queue (1,500 byte frames) reaches a target: rho = 2w / (S + 2w)
link       S (frame)  w = 0.1 ms   w = 1 ms     w = 5 ms
10 Mbit    1200.0 us  0.143        0.625        0.893
100 Mbit   120.0 us   0.625        0.943        0.988
1 Gbit     12.0 us    0.943        0.994        0.999
10 Gbit    1.2 us     0.994        0.999        1.000
```

A buffer of `rate × delay`: **25,000 bytes (16 frames)** holds 20 ms at 10 Mbit, **1,250,000 (833)** holds 100 ms at 100 Mbit, and **625,000 bytes (416 frames)** holds 5 ms at 1 Gbit. A 1 Mbit link at 5 ms holds **625 bytes**, less than half a frame: a buffer that small cannot hold even one packet, so a slow link must accept a longer delay.

The load at which an M/D/1 queue reaches a mean wait `w` follows from `w = ρ S / (2 (1 - ρ))`, which gives `ρ = 2 w / (S + 2 w)`. For a 0.1 ms wait the load on 10 Mbit is **0.143** (the wait is a tenth of a 1.2 ms frame), on 100 Mbit **0.625** and on 1 Gbit **0.943**; for 1 ms it is **0.625, 0.943 and 0.994**. The answer to "how loaded can a link be" depends on the link speed more than on any queue discipline, and the measured delays of section 1.2 bracket the 10 Mbit figure: 0.62 ms at load 0.50 and 2.13 ms at 0.80 on either side of 1 ms, which the formula puts at 0.625.

### Problem B: a token bucket

```python
# ans_b.py: sizing a token bucket: bandwidth delay product, smallest sensible burst, queue limit for a latency bound, refill time.
frame = 1514
print("%-9s %-8s %-12s %-14s %-14s %-12s %-16s" % ("rate", "rtt", "BDP bytes", "rate/HZ 250", "rate/HZ 1000", "refill 1 BDP", "limit for 50 ms"))
for name, bps in (("1 Mbit", 1e6), ("10 Mbit", 10e6), ("100 Mbit", 100e6), ("1 Gbit", 1e9)):
    for rtt in (0.001, 0.02, 0.1):
        bdp = bps * rtt / 8; burst = max(frame, bps / 8 / 250)
        print("%-9s %-8s %-12d %-14d %-14d %-12s %-16d" % (name, "%g ms" % (rtt * 1000), bdp, bps / 8 / 250, bps / 8 / 1000, "%g ms" % (bdp * 8 / bps * 1000), bps / 8 * 0.05 + burst))
```

```text
rate      rtt      BDP bytes    rate/HZ 250    rate/HZ 1000   refill 1 BDP limit for 50 ms
1 Mbit    1 ms     125          500            125            1 ms         7764
1 Mbit    20 ms    2500         500            125            20 ms        7764
1 Mbit    100 ms   12500        500            125            100 ms       7764
10 Mbit   1 ms     1250         5000           1250           1 ms         67500
10 Mbit   20 ms    25000        5000           1250           20 ms        67500
10 Mbit   100 ms   125000       5000           1250           100 ms       67500
100 Mbit  1 ms     12500        50000          12500          1 ms         675000
100 Mbit  20 ms    250000       50000          12500          20 ms        675000
100 Mbit  100 ms   1250000      50000          12500          100 ms       675000
1 Gbit    1 ms     125000       500000         125000         1 ms         6750000
1 Gbit    20 ms    2500000      500000         125000         20 ms        6750000
1 Gbit    100 ms   12500000     500000         125000         100 ms       6750000
```

The bandwidth delay product is `rate × RTT`: **25,000 bytes** at 10 Mbit and 20 ms, **12,500,000** at 1 Gbit and 100 ms. The old rule's burst, `rate / HZ`, is **5,000 bytes** at 10 Mbit, **50,000** at 100 Mbit and **500,000** at 1 Gbit for HZ of 250, and one quarter of that at 1,000. The `refill 1 BDP` column is the round trip by definition: a bucket of one bandwidth delay product refills in one round trip. The limit for a 50 ms bound is `rate × 0.05 + burst`: **67,500 bytes** at 10 Mbit (62,500 plus a 5,000 byte burst) and **6,750,000** at 1 Gbit.

Measured against the rule: `e8_burst.sh` reached the full rate of a 100 Mbit bucket with a burst of **3,000 bytes** (95.2 of 95.6 Mbit/s), where the old rule asks for 50,000. The old rule is for a kernel with a coarse timer; this kernel has high resolution timers (section 3.2). The measured rule is two frames; use the larger of that and the policer's need, which is a bandwidth delay product (section 4.1), not `rate / HZ`.

### Problem C: HTB allocation

```python
# ans_c.py: the HTB sharing rule as an allocator: every class gets min(demand, rate), then spare capacity goes to the lowest prio number first, shared inside a prio by rate, up to min(demand, ceil).
def htb(link, classes, demand):
    got = {c: min(demand[c], classes[c][0]) for c in classes}
    spare = link - sum(got.values())
    for prio in sorted({v[2] for v in classes.values()}):
        group = [c for c in classes if classes[c][2] == prio]
        while spare > 1e-9:
            room = {c: min(demand[c], classes[c][1]) - got[c] for c in group}
            live = [c for c in group if room[c] > 1e-9]
            if not live: break
            w = sum(classes[c][0] for c in live); step = min(spare, min(room[c] * w / classes[c][0] for c in live))
            for c in live: got[c] += step * classes[c][0] / w
            spare -= step
    return got
def show(title, classes, demand):
    g = htb(10, classes, demand)
    print("%-46s %s" % (title, "  ".join("%s %.2f" % (c, g[c]) for c in classes)))
eq = {"1:10": (2, 10, 1), "1:20": (2, 10, 2), "1:30": (2, 10, 2)}
w = {"1:10": (1, 10, 2), "1:20": (3, 10, 2), "1:30": (1, 10, 2)}
print("measured in e5: 6.00/2.00/2.00, 0.00/5.00/5.00, 2.00/6.00/2.00, 4.50/1.00/4.50 Mbit/s")
show("rates 2,2,2 prio 1,2,2; all offer 12", eq, {"1:10": 12, "1:20": 12, "1:30": 12})
show("same, class 10 idle", eq, {"1:10": 0, "1:20": 12, "1:30": 12})
show("rates 1,3,1 prio 2,2,2; all offer 12", w, {"1:10": 12, "1:20": 12, "1:30": 12})
show("same, class 20 offers 1", w, {"1:10": 12, "1:20": 1, "1:30": 12})
print()
show("new: ceil 6 on class 10, rates 2,2,2, prio 1,2,2", {"1:10": (2, 6, 1), "1:20": (2, 10, 2), "1:30": (2, 10, 2)}, {"1:10": 12, "1:20": 12, "1:30": 12})
show("new: rates 5,3,2 prio 2; demands 3,12,12", {"1:10": (5, 10, 2), "1:20": (3, 10, 2), "1:30": (2, 10, 2)}, {"1:10": 3, "1:20": 12, "1:30": 12})
show("new: rates 3,3,3 prios 0,1,2; all offer 12", {"1:10": (3, 10, 0), "1:20": (3, 10, 1), "1:30": (3, 10, 2)}, {"1:10": 12, "1:20": 12, "1:30": 12})
```

```text
measured in e5: 6.00/2.00/2.00, 0.00/5.00/5.00, 2.00/6.00/2.00, 4.50/1.00/4.50 Mbit/s
rates 2,2,2 prio 1,2,2; all offer 12           1:10 6.00  1:20 2.00  1:30 2.00
same, class 10 idle                            1:10 0.00  1:20 5.00  1:30 5.00
rates 1,3,1 prio 2,2,2; all offer 12           1:10 2.00  1:20 6.00  1:30 2.00
same, class 20 offers 1                        1:10 4.50  1:20 1.00  1:30 4.50

new: ceil 6 on class 10, rates 2,2,2, prio 1,2,2 1:10 6.00  1:20 2.00  1:30 2.00
new: rates 5,3,2 prio 2; demands 3,12,12       1:10 3.00  1:20 4.20  1:30 2.80
new: rates 3,3,3 prios 0,1,2; all offer 12     1:10 4.00  1:20 3.00  1:30 3.00
```

(i) Rates 2, 2, 2 with a ceil of 6 on the first: guarantees take 6, spare is 4, the first class (prio 1) takes all of it and reaches **6.00**, exactly its ceiling, so the ceiling changes nothing: **6.00, 2.00, 2.00**. (ii) Rates 5, 3, 2, first offering 3: it keeps **3.00**, and the 2 Mbit of its guarantee that it does not use is spare and goes to the other two in the ratio 3:2: **4.20 and 2.80**. (iii) Rates 3, 3, 3 with prios 0, 1, 2: guarantees take 9 and the spare Mbit goes to prio 0: **4.00, 3.00, 3.00**. The measured runs of section 7.5 are the first four lines, and the allocator reproduces all four; (i), (ii) and (iii) are the rule applied, not kernel runs.

### Problem D: DRR delay

```python
# ans_d.py: deficit round robin: how long a packet of a new flow can wait, and how many rounds a frame needs when the quantum is smaller than the frame.
import math
print("worst wait of the first packet of a new flow: the other N-1 flows each send one quantum (plus the frame in service)")
print("%-6s %-8s %-14s %-14s %-14s" % ("N", "quantum", "10 Mbit", "1 Gbit", "rounds per 1,514 B frame"))
for n in (2, 10, 100, 1000):
    for q in (300, 1514, 9000):
        wait = ((n - 1) * q + 1514) * 8
        print("%-6d %-8d %-14s %-14s %-14d" % (n, q, "%.2f ms" % (wait / 10e6 * 1000), "%.3f ms" % (wait / 1e9 * 1000), math.ceil(1514 / q)))
```

```text
worst wait of the first packet of a new flow: the other N-1 flows each send one quantum (plus the frame in service)
N      quantum  10 Mbit        1 Gbit         rounds per 1,514 B frame
2      300      1.45 ms        0.015 ms       6
2      1514     2.42 ms        0.024 ms       1
2      9000     8.41 ms        0.084 ms       1
10     300      3.37 ms        0.034 ms       6
10     1514     12.11 ms       0.121 ms       1
10     9000     66.01 ms       0.660 ms       1
100    300      24.97 ms       0.250 ms       6
100    1514     121.12 ms      1.211 ms       1
100    9000     714.01 ms      7.140 ms       1
1000   300      240.97 ms      2.410 ms       6
1000   1514     1211.20 ms     12.112 ms      1
1000   9000     7194.01 ms     71.940 ms      1
```

The worst wait is `((N - 1) × quantum + 1514) × 8 / rate`. With 10 flows and a 1,514 byte quantum it is **12.11 ms** at 10 Mbit and **0.121 ms** at 1 Gbit; with 100 flows **121.12 ms**; with 1,000 flows **1,211 ms**. A 9,000 byte quantum multiplies the wait by almost six (**714 ms** at 100 flows, 10 Mbit); a 300 byte quantum divides it by about five (**24.97 ms**) but needs **6 turns** per 1,514 byte frame, so the scheduler does six times the work per packet. The quantum is chosen near the MTU for that trade. The wait is a worst case: every other flow is backlogged and arrives ahead of the new flow's first packet, and a new flow that is served first (FQ-CoDel's new flow list, section 6.4) waits for one frame in service, not for the round.

### Problem E: the drops TCP needs

```python
# ans_e.py: how many drops per second Reno flows need to fill a link (the Mathis formula), against the model and against what CoDel's control law supplies.
import math
from sim import Sim, Flow, Taildrop, Codel
C, rtt, mss = 10e6, 0.1, 1448
print("10 Mbit, 100 ms round trip; N Reno flows share the link")
print("%-4s %-10s %-16s %-16s %-16s %-14s %-12s" % ("N", "p needed", "drops/s needed", "model taildrop", "model codel", "codel count", "ramp time"))
for n in (1, 4, 16):
    p = (1.22 * mss * 8 * n / (rtt * C)) ** 2; pps = C / (1500 * 8); need = p * pps
    row = []
    for qd in (Taildrop(83), Codel()):
        sm = Sim(C, rtt, qd, end=80, warm=20)
        for i in range(n): Flow(sm, "f%d" % i, start=0.01 * i)
        sm.run(); row.append(sm.drops / 60)
    count = (need * 0.1) ** 2
    print("%-4d %-10.5f %-16.1f %-16.1f %-16.1f %-14.1f %-12s" % (n, p, need, row[0], row[1], count, "%.2f s" % (2 * 0.1 * math.sqrt(max(count, 1)))))
print()
print("RED, minth 10, maxth 50, maxp 0.1: drop probability at an average queue of q frames")
for q in (5, 10, 20, 30, 40, 50, 60): print("q %2d  p %.3f" % (q, 0 if q < 10 else 1 if q >= 50 else 0.1 * (q - 10) / 40))
```

```text
10 Mbit, 100 ms round trip; N Reno flows share the link
N    p needed   drops/s needed   model taildrop   model codel      codel count    ramp time
1    0.00020    0.2              0.1              0.2              0.0            0.20 s
4    0.00320    2.7              1.1              3.6              0.1            0.20 s
16   0.05113    42.6             14.4             35.7             18.2           0.85 s

RED, minth 10, maxth 50, maxp 0.1: drop probability at an average queue of q frames
q  5  p 0.000
q 10  p 0.000
q 20  p 0.025
q 30  p 0.050
q 40  p 0.075
q 50  p 1.000
q 60  p 1.000
```

The Mathis formula (**from memory**) gives a Reno flow's rate as `1.22 × MSS / (RTT × sqrt(p))`, so each of `N` flows needs a drop probability of `(1.22 × MSS × 8 × N / (RTT × C))²` to be held at its share `C / N`. For 1, 4 and 16 flows it is **0.0002, 0.0032 and 0.0511**, which at 833 packets a second is **0.2, 2.7 and 42.6** drops a second. The model's tail drop queue (buffer of one bandwidth delay product) produced **0.1, 1.1 and 14.4** a second and CoDel **0.2, 3.6 and 35.7**: CoDel supplied 16 % less than the formula asks at 16 flows, and the tail drop queue a third of it. The formula stops being a good guide at 16 flows (each flow's window is then about 5 packets, where timeouts, not the formula, set the rate), so the check is of the order of magnitude, not a fit.

CoDel reaches that rate by counting: its drop interval is `100 ms / sqrt(count)`, so a rate of 42.6 a second is a `count` of `(42.6 × 0.1)² = 18.2`, and the ramp (the sum of `100 ms / sqrt(k)` for `k` up to the count) takes about `2 × 100 ms × sqrt(18.2) = 0.85 s`; for 1 flow it is the first interval, **0.20 s** (the formula gives a count under one, which the script rounds up to 1). The RED probabilities at an average queue of 20, 30 and 40 frames are **0.025, 0.050 and 0.075**, the line from 0 at `minth` 10 to `maxp` 0.1 at `maxth` 50, then a cliff to 1.

---

## 15. Cheat sheet

**Little and load:** delay = bytes ÷ rate. M/D/1 wait `ρ S / (2 (1 - ρ))`: measured 0.62, 2.13, 4.70, 10.35 ms at 0.50, 0.80, 0.90, 0.95 (formula 0.50, 2.00, 4.50, 9.50); p99 61.1 ms at 0.95. Load for a 1 ms wait with 1,500 byte frames: 0.625 (10 Mbit), 0.943 (100 Mbit), 0.994 (1 Gbit).

**Token bucket:** output at most `b + r × t`. Measured exact: 8 Mbit, burst 15,000: 15,000 bytes in 1 ms, 1,015,000 in 1 s. 100 Mbit: burst 1,600 gives 88 Mbit/s, 3,000 gives 95.2 of 95.6.

**Policing against shaping (5 Mbit, cubic):** shaper 4.71 Mbit/s, 32 retransmissions, +30 ms; ingress policer 5.07 to 5.24, 686 to 983 retransmissions (equal to its drop count), no delay; a policer on the sender's output hook: 0 retransmissions (local error). Model at 40 ms: burst 1,500 B gives 0.30 Mbit/s, 25,000 B gives 4.18, 150,000 B gives 5.00.

**Bufferbloat (10 Mbit):** buffer 15 KB: 6.7 ms; 75 KB: 36.7 ms; 378 KB and 1.5 MB: 99 ms, backlog 121 KB (102 ms by Little), cause not found (unacked 76 to 82 packets, cwnd 207 and receive window 176 KB were not the limit). BBR: 9 ms. Buffer rules: BDP for one flow, BDP over sqrt(N) for many (36 flows: 14 frames kept 100 %), `rate × delay` for interactive.

**AQM:** RED `minth 10, maxth 50, maxp 0.1`: p 0.025, 0.050, 0.075 at 20, 30, 40 frames, then 1. CoDel: target 5 ms, interval 100 ms, drop at `interval / sqrt(count)`: 100, 70.7, 57.7, 50.0 ms. ECN: `red` 202 drops, `red + ecn` 3. FQ-CoDel: hash to queues, DRR, new flows first.

**Disciplines under load (model, 10 Mbit, 40 ms, 2 bulk flows):** probe round trip / short transfer: tail drop 1,000 frames 1,096 / 2,210 ms; BDP 64.3 / 258.5; CoDel 47.0 / 125.9; FQ-CoDel 40.8 / 114.1. Model overstates a 10 frame queue by 72 %.

**Schedulers:** strict priority: top class 0.033 ms, lowest band lost 52 % with replies after 5.3 s. FIFO: 2.91 / 3.26 / 3.83; round robin by packet 6.18 / 3.18 / 0.64; DRR 3.33 each; weights 3:1:1: 6.00 / 2.00 / 2.00. DRR wait `((N - 1) q + 1514) × 8 / rate`: 12.1 ms (N 10, 10 Mbit).

**HTB:** guarantee, then spare by prio, then by rate. Measured: 6.00/2.00/2.00; 0.00/5.00/5.00; 2.00/6.00/2.00; 4.50/1.00/4.50. Design: voice 1 Mbit ceil 2 prio 0, interactive 4, rest 5: 1.00 / 4.00 / 5.00 normally, and the same under a 12 Mbit voice flood; the voice probe 0.053 ms idle and 730 ms inside the flooded class.

**Marking:** TOS = DSCP × 4 + ECN: EF 0xb8, AF21 0x48, CS1 0x20 (measured). `meta priority set 1:10` picks an HTB class with no filter. 802.1p is the top three bits of the DSCP (EF gives 5). Marks may be rewritten at an edge (0xb8 became 0x00).

**Where the queue is:** the first link slower than the sender; find it by round trip under load against idle, and manage it there.

---

## 16. Sources

Read or run for this tutorial:

- Linux 6.18 `tc` with `tbf`, `htb`, `pfifo`, `pfifo_fast`, `ingress` and `ifb`, `u32`, `mirred`; `nftables` (`limit`, `ip dscp set`, `meta priority set`); `iperf3`, `ping`, `tcpdump`, `ss`: used and observed on the sandbox kernel.
- `sim.py`, a packet level model written for this tutorial; it is validated against the kernel in one case (section 5.2) and otherwise stands for what it computes. CoDel, RED, FQ-CoDel and DRR in it follow my memory of the descriptions, not the RFC text.
- The kernel configuration of the sandbox, read to find which qdiscs are missing.

From memory (not re-read for this tutorial):

- Floyd and Jacobson, *Random Early Detection Gateways for Congestion Avoidance*, 1993.
- Nichols and Jacobson, *Controlling Queue Delay*, 2012; RFC 8289 (CoDel); RFC 8290 (FQ-CoDel); Gettys and Nichols, *Bufferbloat: Dark Buffers in the Internet*, 2011.
- Shreedhar and Varghese, *Efficient Fair Queuing using Deficit Round Robin*, 1995; Parekh and Gallager on generalised processor sharing (the ideal WFQ approximates).
- Villamizar and Song, *High Performance TCP in ANSNET*, 1994 (the bandwidth delay product buffer); Appenzeller, Keslassy and McKeown, *Sizing Router Buffers*, 2004.
- Mathis et al., *The Macroscopic Behavior of the TCP Congestion Avoidance Algorithm*, 1997; Cardwell et al., *BBR: Congestion-Based Congestion Control*, 2016.
- RFC 3168 (ECN); RFC 2474, 2597 and 3246 (DSCP, AF and EF); RFC 2697 and 2698 (single and two rate three colour markers); RFC 8622 (lower effort).
- Linux documentation for HTB (Martin Devera), the `tc-tbf`, `tc-htb` and `tc-fq_codel` manual pages, byte queue limits, and the `mac80211` airtime fairness and per station queueing work (Høiland-Jørgensen et al.).

Cross references: NET 03 (congestion control, the window and `ss -ti`), NET 06 (where the qdisc sits in the transmit path), NET 12 (ECN, PFC and incast in a lossless fabric), NET 13 (balancers and their connection queues), and the traffic design problems in `11_hld`.
