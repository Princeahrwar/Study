# WF 09: Testing, from one noisy number to a defensible verdict

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela sessions 8a, 8b, 8c and 8d (testing fundamentals, testing in the lab, testing in test houses, test plan development), with the gaps the CWNA, CWAP and CCIE Wireless material covers (what a Wi-Fi throughput test must report, how roaming and latency are measured, attenuators and shielding, certification and regulatory testing) · Prerequisites: [WF 03](03_wifi_mac.md) (retries, aggregation, power save), [WF 05](05_wifi_roaming_and_radio_management.md) (roaming), [WF 08](08_wifi_capture_tools_and_debug.md) (iperf3, ping, capture) and [WF 11](11_rf_fundamentals_and_link_budget.md) (link budget, used for the isolation arithmetic) · Time: about 8 hours · Practice: six experiments and five problem scripts in `prog/`*

A test result is a random variable. The senior skill in Wi-Fi testing is not running iperf3 but knowing **how many runs make a number mean something, what the environment did between run A and run B, and what the test cannot see**. Candela's session 8 lists the places a test can run (the lab, a test house, the field) and the process of writing a plan. This tutorial turns each choice into a measurement or a computation: how much one throughput number moves on a real path and on a modelled Wi-Fi link in a lab and in a house, how many runs a precision costs, why testing build A in the morning and build B in the afternoon flags a regression **23.8 percent** of the time when there is none, how 17,280 configurations shrink to 43 and what that loses, what a retry policy does to the false failure rate and to the bugs that escape, how long a real outage a 10 ms prober sees, and how long 100 real supplicants take to authenticate.

**Eight honest limits, stated first.**

1. **There is no radio here, so no Wi-Fi throughput is measured.** The two Wi-Fi sets (lab and house) are a model of mine: an 80 MHz, 2 stream link whose per-run level is shadowing (1 dB standard deviation in the lab, 4 dB in the house), whose neighbour airtime share is uniform from 0 to 5 percent (lab) or 0 to 40 percent (house), and whose per-10 ms slot is a Rayleigh fade. The **real** measurements are on a shaped veth path (iperf3, a 20 Mbit/s token bucket), on a route that is removed and restored, and on 802.1X with real `hostapd` and `wpa_supplicant`.
2. **The real path is far quieter than any radio.** Its relative spread is 0.11 percent because a token bucket is the bottleneck; that is a property of the shaper, not of the host, and it shows what a *wired reference leg* of a test bed costs you in noise (almost nothing), not what Wi-Fi does.
3. **The 802.1X scale test uses N `hostapd` processes, not one authenticator with N stations.** A multi-BSS `hostapd` needs a BSSID block and refused to start on veth interfaces, and a Linux bridge dropped the PAE group address frames, so each client has its own veth and its own `hostapd`. It measures supplicant and EAP-MD5 server cost under CPU contention on 2 cores, not the capacity of one access point. The floor of about 2.05 s at N = 1 is present in every run and I did not isolate which timer sets it.
4. **A removed route is not a roam.** The gap experiment shows how a prober measures an outage (what it reports, what the latency statistics hide, what a slow prober misses). A real roam also has a scan, authentication, reassociation and a key exchange whose durations are in [WF 05](05_wifi_roaming_and_radio_management.md).
5. **The tool, vendor, standard and certification tables are from memory** (LANforge, Ixia and Keysight, Spirent, Octoscope and similar systems, the Wi-Fi Alliance programs, the RFC numbers of the benchmarking family, shield box and chamber figures). They were not checked here and are marked **from memory**.
6. **The coverage and retry experiments are simulations** with independent runs and faults placed uniformly at random. Real faults cluster in the configurations nobody thought of, which is exactly where a pairwise suite is weakest.
7. **The paired against sequential comparison uses a drift model I chose** (an AR(1) process, 15 percent standard deviation, correlation time 12 runs, 6 percent fast noise). The direction of the result is robust; the percentages are not a forecast for a building.
8. **Not run here:** LANforge, Ixia or any commercial emulator, real clients, a shield box, an attenuator matrix, a chamber, a house, a regulatory or certification test. Those parts are described from memory and from the course.

By the end you can:

- **say how much one number moves and how many runs a claim needs** (measured and modelled: 25 real iperf3 runs on the shaped path have a relative spread of **0.11 percent** quiet and **0.12 percent** with three busy loops on 2 cores, the modelled Wi-Fi link has **2.89 percent** in the lab and **18.25 percent** in the house, so a 5 percent half width at 95 percent confidence needs **4 runs** in the lab and **54** in the house, and **323** for 2 percent).
- **say why a single run cannot call a regression** (modelled: one run against one run flags a 2 percent drop rule as a regression **44.1 percent** of the time when nothing changed; five runs per build and a t test flag **2.5 percent**, but detect a true 5 percent drop only **4.8 percent** of the time in the house, and **10.9 percent** with 15 runs).
- **design a comparison that survives a drifting environment** (modelled: 10 runs of A then 10 runs of B flag a non-existent regression **23.8 percent** of the time, randomising the order **2.2 percent**, interleaved pairs with a paired test **2.3 percent**, and for a true 5 percent drop with 30 runs per build the paired design finds it **74.8 percent** of the time against **31.4 percent** for randomised order).
- **measure an outage and know what the prober hides** (measured: a 10 ms prober reports a gap of D + 10 ms for outages of 20, 50, 200 and 800 ms (**30.0, 60.0, 209.9 and 810.4 ms**) and loses D/10 packets, while the mean round trip stays at **0.22 to 0.24 ms** and the worst reply at **0.7 to 1.9 ms**; a 100 ms prober reports **300 ms** for a 200 ms outage and misses **70.7 percent** of 30 ms outages).
- **size a scale test and read its numbers** (measured: 1, 10, 30, 60 and 100 real supplicants authenticate in a median of **2.06, 2.05, 2.11, 2.22 and 2.29 s**, all of them every time, the slowest of 100 at **2.52 s**).
- **cut a test matrix and say what was lost** (computed and simulated: 7 factors give **17,280** cases, an all-pairs suite covers all **376** value pairs in **43** cases (**0.25 percent**), finds **100 percent** of single and double factor faults against **90.9 percent** for 43 random cases, but only **54.5 percent** of 3 factor and **16.2 percent** of 4 factor faults; **180 days** of rig time become **3.6 hours** at 5 minutes a case).
- **choose a retry policy with the arithmetic in front of you** (computed and Monte Carlo checked: with 5 percent noise per run, retry up to 3 times blocks a healthy build **0.01 percent** of the time instead of **5.00**, and lets a bug that fails half the runs through **87.5 percent** of the time instead of **50.0**; 5 runs find a q = 0.5 bug with 95 percent confidence, **299** runs find a 1 percent one).
- **write a test plan** that a colleague can execute: objective, topology, preconditions, procedure, measurements, pass criteria with a statistical rule, noise handling, cleanup and a rig-time budget (the 43 case suite is **6.81 hours** on one rig at 9.5 minutes a case, **19.7 hours** if the house noise forces 15 runs a case).

## Where the numbers come from

Each experiment is a script `sN_*.py` or `.sh` in `prog/` with its output frozen in `v/`, run with `run.sh`. `topo.sh` builds the client, router and server namespaces with the token bucket shaper. `s1_repeat.sh` and `s1_stats.py` are the repeatability study, `s2_scale.sh` the 802.1X scale test, `s3_gap.py` the outage prober, `s4_plan.py` the test matrix, `s5_flaky.py` the retry policies and `s6_paired.py` the build comparison designs. The five problem scripts `ans_a.py` to `ans_e.py` are in sections 13 and 14.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, simulated, modelled or computed here is marked **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| Repeatability (`s1_repeat.sh`) | 75 real iperf3 runs of 3 s over the shaped path, about 5 minutes, plus 50 modelled Wi-Fi runs and the statistics |
| Scale (`s2_scale.sh`) | 1, 10, 30, 60 and 100 real `wpa_supplicant` and `hostapd` pairs in network namespaces, about 1 minute |
| Outage prober (`s3_gap.py`) | 75 trials of 1.3 to 2.1 s with a 10 ms prober, about 2 minutes |
| Test matrix (`s4_plan.sh`) | 17,280 combinations, greedy all-pairs with 300 candidates a step, 8,000 faults, under 5 s |
| Retry policies (`s5_flaky.sh`) | exact binomial plus 200,000 Monte Carlo trials a cell, under 3 s |
| Build comparison (`s6_paired.sh`) | 3,000 and 1,500 trials a cell, 24 cells, about 20 s |

## Mapping to the Candela course

Candela session 8a (testing fundamentals: approaches, lab and field, automation and manual) is sections 1, 2, 3 and 8. Session 8b (testing in the lab: benchmarking, scale and stress, repeatability and automation) is sections 4, 5, 6 and 8. Session 8c (testing in test houses: real houses and enterprise environments, challenges and solutions) is section 7. Session 8d (testplan development: develop and execute test plans, engineering tools) is sections 9 and 10. This tutorial adds the numbers behind each topic, and, at CWNA and CWAP level and **from memory** (the mrncciew pages were not re-read for this tutorial): what the certification and regulatory programs test, attenuator and shielding arithmetic, and the metric definitions behind throughput, roaming and latency claims.

---


## 1. Testing fundamentals

### 1.1 What a test is for

A test answers a question with evidence. Before choosing a tool, write the question, because the question fixes the environment, the metric and the number of runs.

| Question | Kind of test | Typical environment | Typical output |
|---|---|---|---|
| Does the code do what the author meant? | unit, component | developer machine, simulation | pass or fail |
| Does the device follow the standard? | conformance | shielded lab, golden reference, protocol analyzer | pass or fail per clause |
| Does it work with other vendors' devices? | interoperability | lab with a client zoo, test house | pass or fail per pair, a matrix |
| How fast, how far, how low a latency? | performance, benchmarking | conducted lab, then OTA chamber | a distribution, not a number |
| How many clients, how fast a churn? | scale and stress | emulated clients plus real ones | a capacity curve and a knee |
| Does it still work after 3 days? | soak, endurance | lab rack | a time series of the metrics |
| Does a failure heal? | fault injection, negative | lab with controllable power, links and RF | recovery time distribution |
| Does it work where people live? | field, test house, beta | houses, offices, campuses | a population of results with the environment recorded |
| May it be sold here? | regulatory (emissions, SAR, DFS, power) | accredited lab | a report from a notified body |
| May it carry the logo? | certification (Wi-Fi Alliance programs, **from memory**) | authorised test lab | a certificate |

Two words that are used loosely and should not be. **Verification** asks whether the product meets its specification ("did we build it right"); **validation** asks whether the specification serves the user ("did we build the right thing"). A throughput test that passes against a datasheet verifies; a customer who sees the video freeze in the bedroom validates. A Wi-Fi product can pass every verification test and fail validation, which is why the field and the test house exist.

A **test oracle** is the rule that says what a correct result is. For a conformance test it is the standard; for a performance test it is a requirement with a statistical rule ("median of 10 runs at least 450 Mbit/s with a 95 percent lower bound at least 430"); for an interoperability test it is "the same as the reference stack". A test with no oracle produces a number, not a verdict. Most disputes about a result are disputes about the oracle, so write it before the first run.

### 1.2 Black, grey and white box

- **Black box** exercises the product only through its interfaces (the air, the Ethernet port, the web page). It is what a customer sees and what a certification lab does. It cannot say why a test failed.
- **White box** uses internal access: logs, counters, debug commands, instrumented builds, a serial console. It explains failures and can force states (inject a deauthentication, drop a key message, set a rate) that the air interface cannot produce on demand.
- **Grey box** is a black box test with the white box evidence collected alongside: run the throughput test through the air and record the driver's rate statistics, retry counters and airtime every second. This is the usual choice in Wi-Fi platform work, because a throughput drop is only useful with the rate and retry history next to it, which is the capture and counter discipline of [WF 08](08_wifi_capture_tools_and_debug.md).

### 1.3 The test pyramid for a Wi-Fi platform

```
                 field / beta                  few, slow, realistic, uncontrolled
              test house, certification
           system tests in the RF lab          tens to hundreds, hours each
        integration: driver + firmware + AP    hundreds, minutes each
     component and protocol tests (simulated)  thousands, seconds each
   unit tests, static analysis, sanitizers     tens of thousands, milliseconds each
```

The cost per test rises about a factor of ten per level and so does the realism. The rule that follows: **a bug found at a lower level costs a tenth as much and is easier to reproduce**, so every escape from a higher level should become a test at the lowest level that can express it. A radio-only failure that was reproduced by a frame replay becomes a protocol test with a captured frame; a race that needed 4 hours of soak becomes a component test with an injected delay.

### 1.4 Approaches that recur

| Approach | Idea | Wi-Fi example |
|---|---|---|
| Baseline and delta | measure a reference, change one thing, measure again | same AP, firmware N against N+1, one client, one position |
| One variable at a time | vary a single factor, hold the rest | sweep attenuation in 3 dB steps, everything else fixed |
| Factorial and pairwise | vary many factors, cover combinations | band, width, security, client, AP mode, roam, traffic (section 9.3) |
| Boundary and equivalence | test at the edges and one per class | channel 36, 64, 100, 144, 165; 20, 40, 80, 160 MHz |
| Regression | rerun the known tests on every change | nightly suite gating the merge |
| Exploratory | a person with the product and a hypothesis | an engineer walking away from the AP during a video call |
| Negative and fault injection | provoke the failure on purpose | rogue deauth, wrong PSK, kill the RADIUS server, pull the uplink |
| Soak | run at moderate load for days | 50 clients, 72 hours, memory and association count trended |
| Stress | exceed the specified load until it breaks | associations per second until the rejection rate rises |
| Differential | the same test on two implementations | our AP and a reference AP in the same position, same client |

## 2. Lab, field and test house

### 2.1 What each one buys

| Property | Conducted lab (cables, attenuators, shield boxes) | OTA lab (chamber, turntable) | Test house or office | Field (customer site) |
|---|---|---|---|---|
| RF control | complete | high | low | none |
| Repeatability | best (relative spread of a few percent or below) | very good | poor (tens of percent) | poorest |
| Realism of propagation | none (a wire has no multipath) | good if the chamber and the positioning are good | real | real |
| Realism of clients and neighbours | what you bring | what you bring | partly | all of it |
| Cost per run | low | high (the chamber) | medium (a place and people) | high (travel, access, risk) |
| What it finds | protocol, rate adaptation, capacity, regression | antenna, beamforming, MIMO, range, orientation | roaming, coverage, interference from real devices, usability | the unknown unknowns |
| Failure mode | a perfect result that no customer will see | long and expensive | non-repeatable result | an anecdote |

A good programme uses all four and does not ask one to do the job of another: **lab for regression and diagnosis, test house for realism and interoperability, field for what nobody thought of**, and the findings flow downward into automated lab tests.

### 2.2 Isolation arithmetic

A conducted test is only conducted if nothing else reaches the receiver. The arithmetic uses the free space path loss of [WF 11](11_rf_fundamentals_and_link_budget.md), FSPL = 20 log10(4 π d f / c).

At 1 m the loss is **40.2 dB at 2.44 GHz** and **47.3 dB at 5.5 GHz** (computed: 4 π × 1 m × 2.44 GHz / c = 102.3 gives 40.2 dB; 4 π × 5.5 GHz / c = 230.5 gives 47.3 dB). An access point transmitting at 20 dBm EIRP puts **-27.3 dBm** into a receiver 1 m away at 5.5 GHz. A 20 MHz receiver has a thermal floor of -101 dBm (-174 + 73), plus a noise figure of about 6 dB, so about **-95 dBm**. To keep a leaked signal 10 dB under that floor you need **-105 - (-27.3) = 77.7 dB** of isolation at 1 m. Two bare devices on a bench are 30 to 50 dB short of that.

Three consequences:

1. **Cables are not isolation.** A coaxial cable with a connector leaks, and an unterminated port radiates. Use shielded enclosures (a shield box is rated at 80 to 100 dB of isolation, **from memory**) or a shielded room, and terminate unused ports.
2. **An attenuator range is a dynamic range.** A cabled test can sweep the path loss only between the isolation floor and the connection loss; with 70 dB of isolation and a 20 dB minimum path loss, the usable sweep is 50 dB, which has to cover the span between the highest and the lowest usable rate (about 40 dB between the sensitivity of the top and bottom rate in the tables of [WF 02](02_wifi_phy.md), **from memory**). If the isolation is worse, the low-rate end of the sweep is polluted by the leak, and the rate adaptation looks better than it is.
3. **Distance is not attenuation.** At 3 m the loss is 56.8 dB at 5.5 GHz, so a neighbouring AP at 20 dBm arrives at **-36.8 dBm**, which is **45 dB above** the -82 dBm preamble detection level. Two test beds in the same room with open air between them interfere, and the interference is deterministic enough to look like a product bug.

An **attenuator matrix** (programmable, one step of 0.25 to 1 dB, **from memory**) connects N APs to M clients with a controlled path loss between each pair, which is how a conducted roaming test is built: attenuate AP1 from 40 to 90 dB while attenuating AP2 from 90 to 40 dB. A **turntable** inside an anechoic chamber rotates the device under test to measure antenna pattern and throughput against orientation, and a **channel emulator** adds delay spread and Doppler (**from memory**).

## 3. What to measure, and exactly what the number means

A metric without a definition is the main reason two labs disagree. Candela's session 8 names throughput, roaming and latency; write each as a definition that fixes the end points, the traffic and the statistic.

### 3.1 Throughput

- **Offered load**: what the generator sends. **Goodput**: what the application receives, in bits per second, over a stated interval, excluding the warm-up. **Throughput** in a test report should say which, and at which layer (PHY rate, MAC throughput, TCP payload).
- **Zero-loss throughput** (the RFC 2544 definition, **from memory**): the highest offered rate at which no frame is lost, found by binary search. On a wired device this is a stable number. On a radio link with rate adaptation and a fading channel it is a distribution, because the loss-free rate itself moves, so report the median and a low percentile of repeated searches, or use a fixed offered load and report the loss.
- **TCP throughput** depends on the window, the round-trip time and the congestion control: a 16 KB window gives 64, 6.4 and 1.3 Mbit/s at 2, 20 and 100 ms (measured in [WF 08](08_wifi_capture_tools_and_debug.md)). RFC 6349 (**from memory**) defines a framework: measure the path MTU and the baseline round trip first, compute the bandwidth-delay product, then compare the TCP result with the ideal.
- **Directions and streams**: uplink and downlink differ (the AP and the client have different power, antennas and queues), and one stream differs from eight. A report that says "500 Mbit/s" and not how many streams, which direction and which congestion control cannot be reproduced.

### 3.2 Latency, jitter and loss

- **Round trip** (ping, RFC 2681 **from memory**) needs one clock and can include a slow reply path. **One way delay** (RFC 2679 **from memory**) needs synchronised clocks at both ends (PTP or GPS at microsecond level), and is the right measure for a voice or video stream.
- **Jitter** is the variation of the delay: the inter-packet delay variation of RFC 3393 (**from memory**) or the running estimate of RFC 3550 that RTP receivers report.
- **Use percentiles, not means.** The mean of a latency distribution is set by the bulk; the experience is set by the tail (p99, p99.9) and by the lost packets, which have no latency at all and therefore **vanish from a latency average**. Section 3.4 measures this.
- **Loss** is reported as a fraction of the packets sent, with the packet size and rate, and its pattern (isolated or bursty) matters more than the mean for voice.
- **Load matters.** Latency on an idle link and latency with a saturated link are different quantities; a deep buffer costs latency, not goodput (measured in [WF 08](08_wifi_capture_tools_and_debug.md): 49.1 ms with cubic and 6.6 ms with BBR on a 50 ms buffer).

### 3.3 Roaming and failover

Define the roam from the **application's view**, not the protocol's: the **gap** is the longest interval in which a stream that normally delivers a packet every T seconds delivers none, around the event. Report it with the stream's packet interval, because the measurement resolution is that interval. Distinguish:

- **Link-layer roam time**: from the last data frame through the old AP to the first through the new one (read from a capture, as in [WF 05](05_wifi_roaming_and_radio_management.md)).
- **Application gap**: the same plus the time the network needs to learn the new location (ARP, bridge learning, a mobility anchor, DHCP if the address changed, a TCP retransmission timeout that has backed off).
- **Roam success rate**: the share of attempted roams that completed without a re-authentication or a lost session.

The common requirement of a 50 ms gap for voice (**from memory**) is an application figure; fast transition (802.11r) is the tool that makes the link-layer part fit inside it.

### 3.4 An outage measured with real packets

`s3_gap.py` sends a 10 ms UDP probe to an echo server across the shaped veth path, replaces the route with a blackhole for D ms and restores it, and reports the longest silence between two replies. Fifteen trials per D.

```
--- the prober sends one UDP packet every 10 ms; the route to the server is made a blackhole for D ms and then restored; 15 trials per D; the gap is the longest silence between two replies
(configured outage D ms | median gap seen ms | min | max | median packets lost | mean RTT ms with the event | p99 RTT ms | max RTT ms)
     0 |    11.4 |   10.7 |   13.9 |    0 |  0.224 |  0.414 |  0.704
    20 |    30.0 |   29.3 |   30.6 |    2 |  0.222 |  0.371 |  0.871
    50 |    60.0 |   58.9 |   60.7 |    5 |  0.240 |  0.586 |  1.107
   200 |   209.9 |  208.7 |  210.9 |   20 |  0.243 |  0.577 |  1.910
   800 |   810.4 |  809.7 |  820.2 |   80 |  0.225 |  0.557 |  0.882
```

Reading the table:

1. **The gap is D plus one probe interval**, **30.0, 60.0, 209.9 and 810.4 ms** for D = 20, 50, 200 and 800 (D = 0 gives 11.4 ms, the probe interval plus scheduling jitter). The prober cannot see the start and end of the outage inside its own interval, so it overstates by up to one interval. Packets lost are D/10: **2, 5, 20, 80**.
2. **The mean round trip is blind to the event.** It is **0.222 to 0.243 ms** in every row, including the 800 ms outage that lost 80 packets, because a lost probe has no round trip and does not enter the average. The p99 is 0.37 to 0.59 ms and the worst reply 0.7 to 1.9 ms. A latency dashboard would report a healthy network through an outage that every user would feel.
3. **An outage is found in the loss and the gap statistics, not in latency.**

The same arithmetic shows what a slow prober does. `ans_c.py` (section 14) gives the reported gap for a true 200 ms outage:

```
--- true outage 200 ms, a prober that sends every I ms and sees a reply only if the probe is sent outside the outage; 20000 random phases
(I ms | mean measured gap | min | max | outages shorter than 50 ms missed entirely, for a 30 ms outage)
     10 |   210.0 |  210.0 |  210.0 |   0.0 %
     20 |   220.0 |  220.0 |  220.0 |   0.0 %
    100 |   300.0 |  300.0 |  300.0 |  70.7 %
    500 |   697.1 |  500.0 | 1000.0 |  94.7 %
   1000 |  1190.8 | 1000.0 | 2000.0 |  97.2 %
```

A probe every 100 ms reports **300 ms** (the gap is a multiple of the probe interval: (floor(D / I) + 1) × I), a probe every 500 ms reports 500 or 1000 ms depending on whether a probe fell inside the outage (mean **697 ms**), and a 30 ms outage escapes a 100 ms prober **70.7 percent** of the time and a 1 s prober **97.2 percent**. **A roaming test needs a prober an order of magnitude faster than the gap it wants to resolve**, and the interval belongs in the report next to the result.

### 3.5 Scale and capacity metrics

- **Associated clients** (the count that stays associated for the test interval), **association rate** (clients per second that complete 4-way handshakes), **authentication rate** (802.1X exchanges per second), **ARP and DHCP rate**.
- **Per-client throughput and its fairness**: the Jain index (sum x)^2 / (n sum x^2) is 1 when all get the same and 1/n when one gets everything. Airtime fairness is the Wi-Fi-specific form: one slow client takes airtime from the rest ([WF 03](03_wifi_mac.md)).
- **The knee**: the load at which a metric (latency, loss, rejection rate) leaves its flat region. Report the knee and the load at which the system fails, not the average over the whole sweep.
- **Soak metrics**: memory, handles, association table size and error counters, trended over hours; the interesting number is the slope.



## 4. Repeatability: how much one number moves

### 4.1 Three real sets and two modelled ones

`s1_repeat.sh` runs real iperf3 (2 s after a 1 s omit, one TCP stream with cubic, 25 runs) through the client, router and server namespaces with a 20 Mbit/s token bucket in each direction. It repeats the set while three busy loops compete for the 2 cores, and runs 25 UDP runs at 18 Mbit/s. `s1_stats.py` adds two modelled Wi-Fi sets (limit 1) and computes the statistics.

```
--- real iperf3 runs: 2 s each after a 1 s omit, TCP, one stream, to a server behind a 20 Mbit/s token bucket
(condition | runs | mean Mbit/s | sd | CV % | min | max | 95 % CI half width % of mean)
  TCP, quiet VM (real)           |  25 | 18.972 |  0.020 |  0.11 |  18.93 |  19.03 |  0.04
  TCP, 3 busy loops (real)       |  25 | 18.966 |  0.022 |  0.12 |  18.92 |  19.02 |  0.05
  UDP at 18 Mbit/s (real)        |  25 | 17.986 |  0.029 |  0.16 |  17.92 |  18.04 |  0.07
  Wi-Fi model, lab (modelled)    |  25 | 512.106 | 14.794 |  2.89 | 475.76 | 541.76 |  1.19
  Wi-Fi model, house (modelled)  |  25 | 396.955 | 72.431 | 18.25 | 271.06 | 531.75 |  7.53
```

What the table says:

- The real shaped path has a relative spread of **0.11 percent** (sd 0.020 on a mean of 18.972 Mbit/s), **0.12 percent** with three busy loops and **0.16 percent** for UDP. The CPU competition made no measurable difference because the shaper, not the host, sets the rate. A wired leg that is the bottleneck-free part of a test bed adds almost no noise, so the noise in a Wi-Fi test is the radio's.
- The modelled lab link has a relative spread of **2.89 percent** and the modelled house **18.25 percent** (min 271, max 532 around a mean of 397 Mbit/s: the best run is **1.96 times** the worst). A 5 percent difference between builds is smaller than the noise of one house run by a factor of 3.6.
- The 95 percent confidence interval of the mean is **±1.19 percent** after 25 lab runs and **±7.53 percent** after 25 house runs. The interval says how well the *mean* is known; it does not shrink the run to run variation that a customer sees.

### 4.2 Runs needed for a precision

The half width of the confidence interval of a mean of n runs is t × s / √n, with t the Student quantile for n - 1 degrees of freedom. Requiring it to be within a fraction e of the mean gives the smallest n with **t × cv / √n ≤ e** where cv is the relative spread. The script finds it by search:

```
--- how many runs for a given precision: the smallest n with t * sd / sqrt(n) <= e * mean, e as a fraction of the mean, with the relative sd of each set above
(precision e | runs: real shaped path | modelled Wi-Fi lab | modelled Wi-Fi house)
    5.0 % |     2 |     4 |    54
    2.0 % |     2 |    11 |   323
    1.0 % |     2 |    35 |  1282
    0.5 % |     3 |   131 |  5119
```

For the house, **54 runs** for ±5 percent, **323** for ±2 percent, about **5,100** for ±0.5 percent; for the lab **4, 11, 35 and 131**. Because n grows as the square of 1/e, halving the precision interval costs four times the runs. Two practical consequences:

1. **When the environment sets the noise, buy variance reduction, not runs.** Section 5 and section 7 show three ways (control the environment, pair the runs, measure the environment). Going from 18 to 3 percent spread (house to lab) saves a factor of **(18.25 / 2.89)^2 = 39.9** in runs, which is the real reason labs exist.
2. **The precision you can afford sets the smallest difference you can claim.** With 54 runs a build, a 5 percent difference is about the limit in a house; a 2 percent claim needs a lab.

### 4.3 One run cannot call a regression

```
--- can one run tell two builds apart? the modelled house runs against a "build B" that is a fixed percent lower (the same runs scaled), single run against five and fifteen runs with a t test
  true change  0.0 %,  1 runs per build: flagged as a regression  44.1 % of the time
  true change  0.0 %,  5 runs per build: flagged as a regression   2.5 % of the time
  true change  0.0 %, 15 runs per build: flagged as a regression   2.9 % of the time
  true change  2.0 %,  1 runs per build: flagged as a regression  48.4 % of the time
  true change  2.0 %,  5 runs per build: flagged as a regression   3.3 % of the time
  true change  2.0 %, 15 runs per build: flagged as a regression   5.0 % of the time
  true change  5.0 %,  1 runs per build: flagged as a regression  59.1 % of the time
  true change  5.0 %,  5 runs per build: flagged as a regression   4.8 % of the time
  true change  5.0 %, 15 runs per build: flagged as a regression  10.9 % of the time
```

(The rule for one run: call it a regression when B is more than 2 percent below A. For 5 and 15 runs: a t test at 5 percent on the house model, one-sided in effect because only B lower counts.)

- With **no change at all**, a single run against a single run raises a false alarm **44.1 percent** of the time. Nearly every regression ticket opened from a single noisy run is that.
- A t test on 5 runs a build keeps the false alarm at **2.5 percent** (the test is working) but a true 5 percent drop is found **4.8 percent** of the time, **10.9 percent** with 15 runs: in a house, neither is enough. This is the power problem: **the false alarm rate is a choice, the detection rate is a consequence of the noise and the number of runs**. A test report should state the smallest drop it could have detected (the minimum detectable effect) next to "no regression found".

## 5. Benchmarking in the lab

### 5.1 The discipline

A benchmark is a controlled comparison. The steps, in order:

1. **Fix the configuration and write it down**: firmware hash, driver and kernel versions, country code, channel and width, security mode, client model and driver, positions or attenuation, traffic generator version and command. Treat a result without it as lost.
2. **Calibrate the bed**: measure the path loss of every cable and attenuator with a network analyzer or a known reference, and verify the isolation (section 2.2) by measuring the leak with the DUT's antenna ports terminated. A lab that has not measured its own noise has no right to claim a precision.
3. **Use a golden reference** (a known AP and client that never change) and run it every day. If the reference moves, the bed changed, not the build; this is the control chart of the lab.
4. **Warm up and omit**: skip the first seconds (rate adaptation converging, TCP slow start) with `iperf3 -O`, and say so.
5. **Change one thing, or randomise the order of changes** (section 5.3).
6. **Run enough times** (section 4.2) and **report the distribution**: n, median, a low percentile, the spread and the interval, not one number.
7. **Record the environment with the result**: the other traffic on the channel (airtime busy time from a monitor), the temperature of the DUT, the time. A result you cannot explain is only repeatable if it carries its context.

### 5.2 Variance components

The total variance of a benchmark is a sum of contributions: run to run noise (fading, scheduling), setup to setup (cabling, antenna position, client reseating), day to day (temperature, neighbours), and the build difference you want. The first can be averaged by more runs; the others cannot, and they set a **floor** below which more runs buy nothing. A lab that measures its own floor (the same build, the same bed, reseated each day) learns the real minimum detectable effect. A gauge repeatability and reproducibility study (**from memory**, a method of manufacturing quality) does exactly this: parts (devices), operators (setups) and repeats (runs), and decomposes the variance.

### 5.3 Comparing two builds when the environment drifts

Sections 4.1 to 4.3 assumed independent runs. A real environment drifts: neighbours' traffic, temperature, a client's thermal state. `s6_paired.py` models the drift as a slow AR(1) process and compares three ways to schedule 10 runs of A and 10 of B:

```python
def trial(r, delta, pairs, design):
    n = 2 * pairs; d = drift(r, n); f = r.normal(0, 0.06, n)
    if design == 'blocks': lab = np.array([0] * pairs + [1] * pairs)
    elif design == 'paired': lab = np.array([0, 1] * pairs)
    else: lab = r.permutation(np.array([0, 1] * pairs))          # randomised order
    y = 400 * (1 - delta * lab) * np.exp(d + f)
    if design == 'paired':
        a, b = y[lab == 0], y[lab == 1]
        return stats.ttest_rel(a, b).pvalue < 0.05 and b.mean() < a.mean()
    a, b = y[lab == 0], y[lab == 1]
    return stats.ttest_ind(a, b, equal_var=False).pvalue < 0.05 and b.mean() < a.mean()
```

```
--- 10 runs per build, 3000 trials per cell; the fraction of trials that call build B a regression (p < 0.05, B lower)
(true drop of B | sequential blocks | randomised order | interleaved pairs)
     0 % |   23.8 % |    2.2 % |    2.3 %
     3 % |   30.9 % |    7.1 % |   11.9 %
     5 % |   37.1 % |   16.6 % |   28.3 %
    10 % |   54.2 % |   53.4 % |   81.6 %
--- the same with 30 runs per build
     0 % |   31.5 % |    2.7 % |    1.8 %
     3 % |   40.7 % |   13.3 % |   31.7 %
     5 % |   47.6 % |   31.4 % |   74.8 %
    10 % |   63.9 % |   82.1 % |  100.0 %
```

- **All of A, then all of B (blocks)** treats the drift between the blocks as a build difference: with no real change it calls B a regression **23.8 percent** of the time at 10 runs a build and **31.5 percent** at 30 (more runs make it worse, because the confidence interval narrows around a biased difference). This is how most manual comparisons are made.
- **Randomised order** restores the false alarm rate (**2.2 percent**), but its detection of a true 5 percent drop is only **16.6 percent** at 10 runs and **31.4 percent** at 30, because the drift is still inside the variance.
- **Interleaved pairs (ABAB) with a paired test** also keeps the false alarm rate (**2.3 percent**) and removes the slow drift, because adjacent runs share it: for a true 5 percent drop it finds **28.3 percent** at 10 pairs and **74.8 percent** at 30; for 10 percent, **81.6** and **100.0 percent**.

The rule: **never compare builds measured at different times; interleave them, randomise the order within pairs if the order could matter, and use a paired test.** The same logic applies to antennas, positions and clients.

### 5.4 What the conducted lab cannot tell you

A cabled lab has no multipath, no antenna pattern and no body in the way. It will pass a build that fails in a house because of a spatial stream imbalance, a beamforming weakness or a flaw in rate adaptation on a time-varying channel. Include a channel emulator (**from memory**: delay profiles such as the TGn models) or move the same test to a chamber and a test house before you claim a throughput. The conducted number is the **upper bound** of what the product can do and the right place to find a regression; it is the wrong place to find a user's experience.



## 6. Scale and stress

### 6.1 What scale testing is for

A scale test asks what happens when the number of clients, flows or events grows. Wi-Fi platforms fail in four ways under scale, and a test plan needs one test for each:

| Failure | Cause | What to sweep | What to watch |
|---|---|---|---|
| Capacity cliff | a table, a queue or a CPU fills | clients associated, flows, VLANs, ACL entries | rejection rate, latency knee, CPU, memory |
| Airtime collapse | contention, low-rate clients, management overhead | clients active, offered load per client, beacon rate and SSIDs | per-client throughput, Jain fairness, channel busy time |
| Control-plane storm | all clients act at once (reboot, power return, roam wave) | association rate, authentication rate, DHCP rate | time to last client up, failures, retries |
| Slow leak | a resource that is not returned | soak at constant load, churn at constant population | memory slope, handles, table sizes |

Client population matters. **Emulated clients** (a traffic generator that associates many virtual stations from a few radios, as a Candela LANforge or an Ixia or Spirent system does, **from memory**) give the count cheaply and exercise the control plane, but each one shares a radio and an RF path with its siblings, so their airtime and rates are not those of real devices. **Real clients** (a rack of phones and laptops) give the radio behaviour and the client-specific bugs but cost a room and manual care, so a good bed uses emulated clients for the count and a small set of real clients as a probe on the same cell.

### 6.2 A control-plane scale test with real software

`s2_scale.sh` starts N real `wpa_supplicant` instances at once, each in its own network namespace, against `hostapd` 802.1X servers with the built-in EAP server and an EAP-MD5 user, and reads the time from the common start to the `CTRL-EVENT-EAP-SUCCESS` line in the AP logs (limit 3 says what this does and does not model).

```
--- N supplicants started at the same instant against N hostapd processes (one per veth, since multi-BSS needs a BSSID block), EAP-MD5, the time from start to the AP logging EAP success for that client
(clients | authenticated | median s | p95 s | max s)
     1 |    1 |   2.06 |   2.06 |   2.06
    10 |   10 |   2.05 |   2.06 |   2.06
    30 |   30 |   2.11 |   2.14 |   2.14
    60 |   60 |   2.22 |   2.28 |   2.28
   100 |  100 |   2.29 |   2.48 |   2.52
```

Reading the table:

- **All clients authenticated every time**, up to 100.
- The **median time barely moves**: 2.06 s for 1 client, 2.29 s for 100, a rise of 0.23 s over 99 extra clients, about **2.3 ms a client** on 2 cores. The slowest of 100 took 2.52 s, 10 percent above the median.
- The **common floor of 2.05 s** is not the server's work: a single client has it too. A wired 802.1X supplicant starts with a timer before it sends its first frame, and I did not isolate which; the lesson for a test is that **a fixed latency in the control plane hides under the scale effect**, so measure N = 1 first and subtract it before computing a per-client cost.
- **Do not extrapolate.** 100 on 2 cores with EAP-MD5 and one process per client says nothing about 2,000 TLS-based EAP clients on a controller. The cost of PEAP or EAP-TLS is dominated by public key operations on the server ([WF 04](04_wifi_security.md), **from memory**), and the failure mode at the knee is a queue that grows without bound, which this sweep never reached.

### 6.3 Designing a sweep

1. **Find the knee with a geometric sweep** (1, 2, 4, ... clients) until a metric leaves its flat range, then refine linearly between the last good and first bad point.
2. **Repeat each point** (section 4): the knee is a statistic, and a single run on each side can place it a factor of two wrong.
3. **Hold everything else fixed**: the offered load per client, packet size and traffic mix.
4. **Record the resource trends** of the DUT (CPU, memory, table sizes) with the test, not only the traffic results.
5. **Test the recovery** as well: after the overload, remove the load and check that the system returns to its baseline performance without a reboot. A system that survives the peak and never recovers is a failure.
6. **Stress beyond the specification** to learn how the product fails (graceful rejection, a crash, a silent drop) and write the answer into the plan as an expected behaviour.

### 6.4 Soak and churn

A soak test runs a realistic mix at moderate load for 24 to 72 hours. Its pass criterion is not "it did not crash" but **a bounded slope** for the monitored resources: define a threshold for memory growth per hour and for the count of each table, and fail the run when the fitted slope exceeds it. A churn test keeps the population constant and replaces clients at a steady rate (associate, run for a while, disassociate), which finds leaks that a static population hides, such as a per-association structure not freed on the error path.

## 7. Testing in test houses and real environments

### 7.1 Why a house is different

A house or an office has what the lab removes: multipath from walls and furniture, a dozen neighbouring networks, microwave ovens and baby monitors, IoT devices with poor radios, people walking, and clients of every age. The same build that gives a spread of 3 percent in a cabled lab gives **18 percent** in the modelled house of section 4, because the environment adds variation between runs (the neighbours' airtime, the client's position, the time of day). Candela's session 8c lists the challenges; they group into four.

| Challenge | Why it hurts | Solution |
|---|---|---|
| **Uncontrolled interference** | neighbours' airtime and non-Wi-Fi emitters change between runs | measure the channel busy time and the spectrum during every run ([WF 08](08_wifi_capture_tools_and_debug.md)); test at fixed hours; pair and interleave the runs (section 5.3); use a channel with the least occupation and record which |
| **Position and orientation** | a 10 cm shift changes the signal by several dB through multipath | mark positions with tape; use a fixed rig with a repeatable height; take several positions per room and report the distribution; keep the client orientation fixed or rotate it on purpose |
| **Heterogeneous clients** | each phone and laptop has its own radio, drivers, power save and quirks | keep a named client zoo with versions; test every client on the same position set; record the client and its OS build with every result |
| **People and access** | testing needs people, time and permission | automate the repetitive parts (a test box that runs the full script from a button); schedule overnight when the owner is away; agree the scope and privacy (a capture contains other people's traffic, so capture only what the test needs and treat it as personal data) |

### 7.2 Residential and enterprise are different problems

- **Residential**: one or a few APs, a mix of walls, a hard-to-model client mix, a small number of simultaneous users with a video call, a game and a set of IoT devices. The user's question is "does it work in every room?", so test the **coverage** (rate and loss per room), the **worst room**, the behaviour of a mesh satellite ([WF 07](07_wifi_architectures_mesh_and_services.md)) and the **recovery** (the router restarts, the uplink goes down).
- **Enterprise**: many APs, controlled by a controller or a cloud, high density (a lecture hall or a stadium), roaming under the sound of a thousand clients, RADIUS and a captive portal, VLANs and policies. The question is "does it work for everyone at the same time, and can I find the problem when it does not?", so test **capacity per AP**, **roaming** along a path (with a prober as in section 3.4), **band steering and load balancing**, **authentication storms** (section 6.2), and the **observability** of the product (does the management system show the client, the AP and the reason?).

### 7.3 A test house protocol

1. **Survey first.** Run a passive survey of the spaces and a spectrum capture ([WF 08](08_wifi_capture_tools_and_debug.md)), and record the neighbours and the noise before touching the product. A result without that baseline cannot be explained later.
2. **Define the positions and the paths** (a table: room, position, height, distance, walls to the AP) and the **walking route** for roaming, with the timing marks.
3. **Define the client set and the traffic mix**, and the run length and count from section 4.2 for the precision you need; if the budget cannot reach it, say what precision you can claim.
4. **Interleave the builds and the positions** (section 5.3); never test one build on Monday and the other on Friday.
5. **Capture alongside** the throughput: a monitor-mode sniffer on the channel (its limits are in [WF 08](08_wifi_capture_tools_and_debug.md): it hears the data frames worse than the ACKs), the AP's counters, and the client's logs.
6. **Convert findings into lab tests.** A house failure becomes a repro in the cabled lab, where it can be fixed and kept as a regression test; if it cannot be reproduced in the lab, the lab is missing a capability and that is also a finding.

### 7.4 What a home result can claim

A single walk through a single house gives an **anecdote**. Ten houses with the same protocol give a **distribution**, and the claim is about the distribution ("the median of the worst-room rates is X and the 10th percentile Y"). Report the number of houses and the construction (brick, wood, concrete), because the spread across houses is larger than the spread across runs, and a build that wins in a wooden house can lose in a brick one.



## 8. Automation and manual testing

### 8.1 What each is good for

| | Automated | Manual |
|---|---|---|
| Repetition, regression, nightly gating | yes | no |
| Statistics (many runs, sweeps, soak) | yes | impractical |
| Combinatorial coverage | yes | no |
| Exploration, new features, "does this feel wrong" | no | yes |
| Odd clients, usability, physical interactions (reset button, LEDs, power cycles) | partly, with relays and cameras | yes |
| Cost structure | high to build, near zero per run | no build, high per run |
| Failure mode | a green dashboard that tests the wrong thing | a result that depends on who ran it |

Automate a test when it will run **often enough that the build cost is repaid** and its result is **well defined**. Keep manual what needs judgement, and turn what the manual tester finds into an automated check so that the finding is not forgotten. A reasonable split for a Wi-Fi platform team is that the nightly suite covers the regression and the matrix, the weekly run covers scale and soak, and the humans spend their time on new features, odd clients and the house.

### 8.2 The harness

An automated Wi-Fi bed has the same parts everywhere (the names vary, **from memory**):

```
 scheduler / CI  ->  test runner  ->  bed control   ->  DUT control
   (nightly,          (pytest or       (attenuators,      (CLI, SSH, REST,
    on commit)         a framework)     power relays,      serial console,
                                        turntable)         firmware flashing)
        |                  |
        v                  v
   results store  <-  traffic and capture  (iperf3, a traffic generator,
   (database,          (monitor sniffers, counters, logs from every device)
    trend charts)
```

Requirements that separate a harness that is trusted from one that is ignored:

1. **Every test starts from a known state** (factory reset or a stored configuration, a cleared client association) and ends by restoring it. A test that depends on the previous test's leftover is the source of most mystery failures.
2. **Every result carries its context** (section 5.1): build, bed, positions, environment measurements.
3. **Logs from every device are collected on failure**, including the captures, so that a failed test can be diagnosed without being rerun.
4. **The bed can be health-checked** by a self-test (a golden link) before the suite starts, so that a bed fault is reported as a bed fault and not as 200 failed tests.
5. **A test distinguishes a product failure from an infrastructure failure** (the DUT answered incorrectly versus the SSH connection dropped) and reports them differently.

### 8.3 Flaky tests and the retry policy

A noisy test fails sometimes for reasons unrelated to the product. The tempting fix is to retry, and the arithmetic of retries is worth having in front of you. Model each run of a healthy build as failing with probability f (noise) and each run of a buggy build as failing with probability q (an intermittent bug). A policy decides pass or fail from n runs. `s5_flaky.py` computes the exact binomial results and checks them with 200,000 Monte Carlo trials per cell (the largest difference between the two is **0.0018**).

```python
def p_pass(policy, n, p):  # p = probability one run passes
    if policy == 'single': return p
    if policy == 'retry': return 1 - (1 - p) ** n                      # pass if any run passes
    if policy == 'majority': return sum(comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(n // 2 + 1, n + 1))
    if policy == 'all': return p ** n                                     # pass only if every run passes
```

```
--- healthy build: a run fails with probability f=0.05 (noise only). bug build: a run fails with probability q.
(policy | n | healthy build blocked | bug q=0.50 passes (escapes) | bug q=0.20 escapes | bug q=0.05 escapes | runs spent healthy | Monte Carlo check max abs diff)
  single   | 1 |   5.00% |  50.00% |  80.00% |  95.00% | 1.00 | 0.0012
  retry    | 2 |   0.25% |  75.00% |  96.00% |  99.75% | 1.05 | 0.0004
  retry    | 3 |   0.01% |  87.50% |  99.20% |  99.99% | 1.05 | 0.0005
  majority | 3 |   0.72% |  50.00% |  89.60% |  99.28% | 3.00 | 0.0018
  majority | 5 |   0.12% |  50.00% |  94.21% |  99.88% | 5.00 | 0.0006
  all      | 3 |  14.26% |  12.50% |  51.20% |  85.74% | 2.85 | 0.0008
```

With f = 0.05:

- **A single run** blocks a healthy build **5.00 percent** of the time and lets a bug that fails half its runs through **50.00 percent** of the time.
- **Retry until pass, up to 3 runs** brings the false block rate to **0.01 percent** (0.05^3) at an average of **1.05 runs** a test, which is why teams adopt it. The cost is the escape rate: a q = 0.5 bug passes **87.50 percent** of the time, a q = 0.2 bug **99.20 percent**. **Retry hides the intermittent bugs, which are the expensive ones.**
- **Majority of 3** costs 3 runs and gives 0.72 percent false blocks, a q = 0.5 escape of 50.00 percent: it filters noise without changing the verdict on a 50 percent bug, but a 20 percent bug escapes at **89.60 percent**.
- **Fail if any of 3 fails** catches the intermittent bugs best (a q = 0.5 bug escapes **12.50 percent**) and blocks **14.26 percent** of healthy builds.

The suite effect is larger than it looks. A suite of 40 independent tests with f = 0.05 is red **87.15 percent** of the time with no retry, **9.53 percent** with 2 runs and **0.50 percent** with 3: a developer who sees a red suite 7 days in 8 stops reading it.

```
--- a suite of 40 independent tests, healthy build, probability the suite is red
  single   n=1 :  87.15%
  retry    n=2 :   9.53%
  retry    n=3 :   0.50%
```

```
--- how many runs to be 95% sure to see a bug that fails with probability q at least once: n = ceil(ln 0.05 / ln(1 - q))
  q=0.50 -> 5 runs
  q=0.20 -> 14 runs
  q=0.10 -> 29 runs
  q=0.05 -> 59 runs
  q=0.01 -> 299 runs
```

The last table is the other side: to be 95 percent sure of seeing a bug that fails with probability q at least once you need ln 0.05 / ln(1 - q) runs, **5 for q = 0.5, 29 for q = 0.1, 299 for q = 0.01**. A bug that fails once in a hundred needs about three hundred runs of the right test, which is a soak, not a nightly test.

The practical policy that follows:

1. **Do not retry silently.** Retry for the gate, but **record every retry** and report the retry rate per test; a test that needs retries is a test with a defect (in the test or the product) that has an owner.
2. **Quarantine** a test with a measured flake rate above a threshold: it runs but does not gate, and its failures are trended; the owner has a deadline.
3. **Run the intermittent-bug hunters separately** (soak, repeated runs of the failing test), because a retry policy is built to hide exactly what they look for.
4. **Treat a pass after retry as a result with a different label** ("passed on retry 2"), not as a pass.

## 9. Test plan development

### 9.1 What a test plan is

A test plan is a document that lets another engineer run the tests and reach the same verdict. It states **what is tested, how, with what, how often, by whom, and how a result is judged**. Candela's session 8d covers developing and executing a test plan with the engineering tools; the template below is the structure I use, and it works at two levels: the plan for a feature or release, and the test case.

```
TEST PLAN: <feature or release>                                 owner, version, date
1. Scope          what is in, what is out, what changed, what risk this covers
2. References     spec clauses, requirements, tickets, earlier test reports
3. Test items     firmware and driver builds, hardware revisions
4. Bed            topology diagram, equipment and versions, calibration date,
                  isolation measured, golden reference
5. Matrix         factors and levels, the cases selected, what was cut and why
6. Metrics        each defined as in section 3 (end points, traffic, statistic)
7. Method         warm-up, run length, runs per case (from section 4), order
                  (interleaved), environment recording
8. Pass criteria  per metric: statistic, threshold, confidence rule,
                  minimum detectable effect achieved
9. Entry/exit     what must be true to start, what must be true to stop
10. Schedule      rig-hours (section 9.4), people, dates, dependencies
11. Risks         known noise sources, missing equipment, untested areas
12. Reporting     where results go, what the summary says, who decides

TEST CASE: <id> <title>
objective        the question, one sentence
traceability     requirement or clause or ticket
preconditions    state of the bed and of the DUT
steps            numbered, with commands exactly as run
measurements     what is recorded and at what interval
pass/fail        the oracle, with numbers and the statistical rule
cleanup          return to the known state
duration         setup, run, teardown
```

### 9.2 Entry and exit criteria, and prioritisation

**Entry criteria** keep the lab from testing garbage: the build passed the unit and smoke tests, the bed passed its self-test, the plan is reviewed. **Exit criteria** say when to stop: all high priority cases run, no open blocker, the pass criteria met with the minimum detectable effect achieved, and the remaining risks written down and accepted by a named person. Without exit criteria, testing ends when the schedule does.

Prioritise by **risk = likelihood of a defect times its impact**, and by **change**: a feature that changed this release is tested before one that did not; a function that handles every packet before a rarely used management feature; a security function before a cosmetic one. Say in the plan what was *not* tested and why: that is a risk statement, and leaving it out is how an untested path ships.

### 9.3 Covering a matrix: all-pairs

A Wi-Fi feature has many independent factors, and the product of their levels explodes. Take seven (`s4_plan.py`): band (3 levels), width (4), security (5), client (8), AP mode (3), roam type (3), traffic (4). The full factorial is 3 × 4 × 5 × 8 × 3 × 3 × 4 = **17,280** cases.

The observation behind **pairwise (all-pairs) testing** is that most faults involve one or two factors: a client that fails with WPA3, a width that fails on one band. A suite that contains every pair of factor values at least once catches all of those at a small fraction of the size. `s4_plan.py` builds one greedily (300 random candidates per step, pick the one that covers the most uncovered pairs) and checks the coverage:

```python
def greedy():
    left = set(allp); suite = []
    while left:
        best, bc = -1, None
        for _ in range(300):
            c = tuple(rng.randrange(l) for l in L)
            g = len(pairs_of(c) & left)
            if g > best: best, bc = g, c
        suite.append(bc); left -= pairs_of(bc)
    return suite
```

```
--- the matrix: band=3, width=4, security=5, client=8, ap_mode=3, roam=3, traffic=4
full factorial: 17280 cases
all-pairs (greedy, seed 7): 43 cases (0.25% of full), pairs to cover 376
pair coverage check: 376 of 376
--- detection of 2000 random faults per order (a fault fires when its factor values all occur in one case)
(order | all-pairs suite 43 | random suite 43 | random suite 3x size | full)
  1 |  100.0% |  100.0% |  100.0% | 100.0%
  2 |  100.0% |   90.9% |   99.3% | 100.0%
  3 |   54.5% |   51.3% |   82.8% | 100.0%
  4 |   16.2% |   17.5% |   41.2% | 100.0%
--- time: cases x minutes per case
   5 min/case: full  1440.0 h (180.0 days of 8 h), all-pairs    3.6 h
  12 min/case: full  3456.0 h (432.0 days of 8 h), all-pairs    8.6 h
```

- The number of value pairs to cover is **376** (the sum over factor pairs of the product of their levels), and the greedy suite covers all of them in **43 cases**, **0.25 percent** of the full matrix. The lower bound is the product of the two largest level counts, 8 × 5 = 40, so 43 is 3 cases above that bound.
- **Detection of simulated faults** (a fault fires when its factor values all occur in one case, 2,000 random faults of each order): single factor faults **100 percent** and pair faults **100 percent**, as the construction guarantees, against **90.9 percent** for 43 random cases. For 3 factor faults all-pairs finds **54.5 percent** (random 43: 51.3 percent, random 129: 82.8 percent), and for 4 factor faults **16.2 percent**.
- The honest summary: **all-pairs is the cheapest way to cover single and pair interactions, and a poor way to cover triples**. If the faults in your product are three-way (a band, a width and a client together), use a 3-way suite (a few hundred cases for these levels), or put the known risky combinations in as mandatory cases and use all-pairs for the rest.
- **Constraints** matter in real matrices: the 6 GHz band requires WPA3 or OWE (no WPA2-only network), and a client may not support 160 MHz. A pairwise generator must exclude impossible combinations and still cover every possible pair; check the output for pairs that cannot occur and for missing pairs that can.
- **Always add the failures you have already had**: a case that found a bug once is a mandatory regression case, whatever the generator says.

### 9.4 The schedule is arithmetic

`ans_e.py` (section 14) puts numbers on the 43 cases with setup 4 min, 3 runs of 90 s, teardown 1 min: **9.5 minutes** a case, **6.81 hours** on one rig, 3.40 hours on 2 rigs and 1.70 on 4. The full 17,280 case matrix at the same 9.5 minutes is **2,736 hours**, or 342 days of 8 hours. If the house noise needs 15 runs a case, the 43 cases take **19.71 hours** on one rig. A plan that does not state the rig hours has not been checked for feasibility, and the usual response (cut runs per case) silently destroys the precision of section 4.

### 9.5 Reporting

A report that can be acted on has:

1. **A verdict per requirement** (pass, fail, not run, passed with retry) and the **summary of risk**: what is known, what is not.
2. **The statistics**: for each metric, the median, a low percentile, n, the interval and the minimum detectable effect; a plot of the distribution (a box or a strip plot) is more honest than a bar of the mean.
3. **The context**: build, bed, environment, dates.
4. **The failures with evidence**: log, capture, counters, and the reproduction command.
5. **The trend**: the same metric over the last builds, because a single result is a point and the trend is a regression detector.



## 10. Tools, standards and programs

Everything in this section is **from memory** and was not run here; check names, versions and numbers against the vendor and the standard before relying on them.

### 10.1 Test equipment

| Kind | Examples | What it does |
|---|---|---|
| Traffic and client emulation | Candela LANforge, Keysight (Ixia) IxChariot and IxLoad, Spirent Avalanche, iperf3, flent, netperf, `tc netem` | generates load, emulates many clients, measures throughput, latency, loss |
| Wi-Fi bed automation | Octoscope-class systems, shielded racks, RF switch and attenuator matrices, WLANPi | builds a repeatable RF topology under software control |
| RF measurement | vector signal analyzers, spectrum analyzers, power meters, network analyzers, real-time spectrum tools | EVM, spectral mask, power, path loss, leak |
| Isolation | shield boxes, shielded rooms, anechoic chambers, turntables | removes the environment, or makes the environment a controlled one |
| Channel emulation | fading and delay-spread emulators | multipath and Doppler on a cable |
| Capture and analysis | monitor-mode sniffers, Wireshark and tshark, Ekahau Sidekick, AirMagnet-class tools | see the air ([WF 08](08_wifi_capture_tools_and_debug.md)) |
| Roaming test | mobility emulators with attenuators, a robot or a trolley with a client | moves a client through the cell while a prober measures |
| Power and thermal | power analyzers, thermal chambers | energy per bit, behaviour at temperature limits |

### 10.2 Standards and programs

| Name | What it covers |
|---|---|
| RFC 2544 | benchmarking methodology for network devices: throughput, latency, frame loss, back to back frames |
| RFC 2679, 2680, 2681 | one-way delay, one-way packet loss, round trip delay metrics |
| RFC 3393 | IP packet delay variation |
| RFC 6349 | framework for TCP throughput testing |
| RFC 6815 | applicability of RFC 2544 and its limits |
| RFC 3550 | RTP, including the interarrival jitter estimator |
| IEEE 802.11-2024 | the base standard; the protocol clauses define what conformance means |
| Wi-Fi Alliance programs | Wi-Fi CERTIFIED for each generation (4, 5, 6, 6E, 7), WPA3, Passpoint, Wi-Fi Agile Multiband, Wi-Fi EasyMesh, Wi-Fi Optimized Connectivity; interoperability testing against reference devices at authorised labs |
| Regulatory | FCC (US), ETSI (EU), ISED (Canada), MIC (Japan), others: radiated and conducted power, spectral mask, spurious emissions, DFS radar detection, SAR, safety, EMC |
| Test lab accreditation | ISO/IEC 17025 for the lab that issues the report |

The certification programs test **interoperability**, not performance: a device that is Wi-Fi CERTIFIED joined, authenticated and moved data with reference devices in a defined set of tests. It says nothing about throughput, range or latency, and does not replace your own matrix.

## 11. Common mistakes

1. **Reporting one number.** One run is a sample; the claim needs a median or a mean with n and an interval (section 4).
2. **Comparing builds measured at different times.** The drift between the blocks becomes a regression in **23.8 percent** of comparisons with no change (section 5.3). Interleave.
3. **Averaging latency.** Lost packets have no latency; the mean stays at 0.22 ms through an 800 ms outage (section 3.4). Report percentiles, loss and the gap.
4. **A prober slower than the event.** A 100 ms prober reports a 200 ms outage as 300 ms and misses 70.7 percent of the 30 ms ones.
5. **Mistaking cabling for isolation.** Two devices on a bench are 30 to 50 dB short of the 77.7 dB needed at 1 m for a 5.5 GHz receiver (section 2.2).
6. **Testing only in the lab, or only in a house.** The lab finds regressions and cannot find a user's experience; the house finds the experience and cannot be repeated. Use both and move findings downward.
7. **A conducted result presented as a range or capacity claim.** The conducted number is an upper bound.
8. **Retrying until green.** Retry up to 3 hides a q = 0.5 bug **87.5 percent** of the time and a q = 0.2 bug 99.2 percent. Record every retry and label a pass on retry differently (section 8.3).
9. **Calling pairwise coverage "full coverage".** It finds 100 percent of single and pair faults and **54.5 percent** of 3 factor faults and **16.2 percent** of 4 factor faults in the simulation.
10. **Cutting runs to fit the schedule.** The schedule is rig hours; cutting 15 runs to 3 widens the confidence interval by a factor of about 4.5 (the square root of 5 for the runs and 2.0 for the Student quantile with 2 degrees of freedom instead of 14), which must be written in the plan, not found in the post-mortem.
11. **No oracle.** A test that produces a number without a threshold and a statistical rule produces an argument.
12. **A test that depends on the previous test.** Reset to a known state and restore it at the end.
13. **No environment record.** A result that cannot be explained cannot be repeated; log the channel busy time, the neighbours and the time of day.
14. **Treating the reference as fixed without checking it.** Run a golden link daily and chart it; when it moves, the bed moved.
15. **Scale from emulated clients alone.** Many emulated stations share a radio; use real clients as a probe in the same cell.
16. **Extrapolating a scale result.** 100 EAP-MD5 clients on 2 cores say nothing about 2,000 EAP-TLS ones.
17. **Ignoring the fixed latency.** The 2.05 s floor under the scale result is not a per-client cost; measure N = 1 and subtract.
18. **Capturing other people's traffic in a test house** without agreement and without limiting it to what the test needs.

## 12. Interview questions

1. **A colleague says build B is 3 percent slower in a Wi-Fi throughput test. How do you decide whether to believe it?** State the oracle (what is a regression), ask n, spread, order and environment. With a house spread of 18 percent a 3 percent claim needs hundreds of runs; with a lab spread of 3 percent, about 35 for ±1 percent. Insist on interleaved or randomised order and a paired test; blocks flag a false regression in 23.8 percent of cases here. Report the minimum detectable effect.
2. **How many runs do you need for ±5 percent?** n is the smallest with t × cv / √n ≤ 0.05: 4 at 2.9 percent spread, 54 at 18.25 percent. It grows as the square of 1/e, so halving the interval costs four times the runs.
3. **Why does a retry make a flaky test look fixed, and why is that a problem?** It reduces the false block rate (0.05 to 0.0001 with 3 tries) and the detection of intermittent bugs by the same factor (a q = 0.5 bug passes 87.5 percent). The bug that fails sometimes is the expensive one.
4. **A roam test reports 40 ms of gap with a 100 ms probe. Is it right?** No: a 100 ms prober can only report a gap of 0, 100, 200, ..., and the 40 ms result is not possible from a gap measurement; check the method. The reported gap is a multiple of the interval, and an outage of 30 ms is missed 70.7 percent of the time.
5. **Latency average is 5 ms, but users complain of freezes. What do you look at?** Packet loss and gap statistics, percentiles (p99, p99.9), the queue (bufferbloat), retries and rate changes, and a capture; the average excludes the lost packets and the tail. Measure under load.
6. **How would you build a roaming test?** An attenuator matrix with two APs and one client, ramp AP1 from 40 to 90 dB while AP2 goes from 90 to 40, a prober every 5 to 10 ms, a capture on both channels, report the gap distribution over many roams, the success rate and the link-layer breakdown (scan, authentication, reassociation, key exchange).
7. **What can the lab not tell you, and what can a test house not tell you?** The lab: multipath, antenna, real interferers, user behaviour. A house: repeatability and the cause. Hence both, and a path from house findings to lab regression tests.
8. **Design the matrix for a feature with 7 factors.** Full is 17,280. Use all-pairs (43 cases here), add the mandatory risky and historical cases, and say that triples are only 54.5 percent covered; add a 3-way suite for the risky subset.
9. **How do you stop a nightly suite being ignored?** Fix flake (record retries, quarantine, owner and deadline), make failures diagnosable (logs and captures collected), health-check the bed first, gate only on trusted tests, trend the results.
10. **What goes into a Wi-Fi test report?** Verdict per requirement, statistics (n, median, low percentile, interval, MDE), context (build, bed, environment), evidence (logs, captures), trend, and the risk statement of what was not tested.
11. **Isolation: why is a cabled test not enough, and how do you check it?** Leak at 1 m is -27.3 dBm at 5.5 GHz against a -95 dBm floor; cables and unterminated ports leak; measure the leak with the DUT antennas terminated and a sensitive receiver; use shield boxes and terminate ports.
12. **Throughput versus goodput, and what does an RFC 2544 zero-loss test mean on a radio link?** Offered load against delivered payload; the zero-loss rate moves with the channel, so it is a distribution; report the median and a low percentile or a loss at a fixed load.
13. **A customer's house shows half the lab throughput. What is your plan?** Survey the house (spectrum, neighbours, rate per room), capture (retries, rates, airtime), compare with a golden client in the same position, change one variable at a time, interleave AP builds, reproduce in the lab with an attenuator and an interferer, and add the repro as a regression test.
14. **How do you size a scale test for 5,000 clients?** Geometric sweep to find the knee, real clients as probes beside emulated ones, control-plane storm tests (authentication, DHCP) separately from the data plane, record the resources, test recovery, soak the working point; state what was extrapolated.
15. **Soak: what is the pass criterion?** A bounded slope on each monitored resource (memory per hour, table sizes), no crash, and no performance degradation against the start, not the absence of a crash alone.
16. **Verification versus validation in Wi-Fi?** Meeting the spec and the datasheet versus working for the user in the bedroom; the field and the test house exist for validation.



## 13. Problems

Each problem has a script in `prog/` and its output in section 14. Work it on paper first, then run it.

**Problem A. Runs for a claim.** The house model has a relative spread of 18.25 percent and the lab model 2.89 percent. How many runs give a 95 percent interval of ±10, ±5 and ±2 percent of the mean in the house, and ±5, ±2 and ±1 percent in the lab? Which factor of runs does the lab save at ±5 percent?

**Problem B. A small matrix.** A feature has four factors with 3, 3, 2 and 2 levels. How many cases does the full matrix have, how many value pairs must an all-pairs suite cover, and what is the lower bound for the suite size? Build a suite that reaches it and check the coverage.

**Problem C. A prober for a roam test.** The requirement is a roam gap of at most 50 ms. A true outage of 200 ms is measured by probers that send every 10, 20, 100, 500 and 1000 ms. What does each report, and which can verify a 50 ms limit? What share of 30 ms outages does each miss?

**Problem D. A retry policy.** A suite of 60 independent tests has a per-run noise failure probability of 0.02. How many tries per test are needed for a healthy build to be red less than 1 percent of the time? What does that policy do to a test that has a bug failing with probability 0.3, and to a suite with three such bugs?

**Problem E. A rig budget.** The 43 case suite has a setup of 4 min, 3 runs of 90 s and a teardown of 1 min per case. How long is it on one rig, how many builds can 1, 2 and 4 rigs test in a 10 hour night, and what does the house noise (15 runs per case) do to the budget?

## 14. Answers

**A.** The interval half width is t × cv / √n, so n is the smallest integer with t × cv / √n ≤ e:

```
--- relative sd 18.25 percent (the modelled house set), 95 percent confidence
  half width 10.0 % of the mean -> 16 runs
  half width  5.0 % of the mean -> 54 runs
  half width  2.0 % of the mean -> 323 runs
--- relative sd 2.89 percent (the modelled lab set)
  half width  5.0 % of the mean -> 4 runs
  half width  2.0 % of the mean -> 11 runs
  half width  1.0 % of the mean -> 35 runs
```

House: **16, 54 and 323 runs** for ±10, ±5 and ±2 percent. Lab: **4, 11 and 35** for ±5, ±2 and ±1 percent. At ±5 percent the lab saves 54 / 4 = **13.5 times**; the asymptotic ratio is (18.25 / 2.89)^2 = 39.9, and the difference is the Student quantile, which is large for 4 runs (t = 3.18 with 3 degrees of freedom). The smallest n is found by search, because the quantile depends on n and an iteration of n = (t cv / e)^2 can oscillate at small n.

**B.** Full matrix: 3 × 3 × 2 × 2 = **36**. Pairs to cover: factor pairs 3·3 + 3·2 + 3·2 + 3·2 + 3·2 + 2·2 = 9 + 6 + 6 + 6 + 6 + 4 = **37**. Lower bound: every pair of values of the two largest factors needs its own case, 3 × 3 = **9**. A greedy search over 200 seeds finds it:

```
--- factors 3,3,2,2: full factorial 36 cases, pairs to cover 37, lower bound 3*3 = 9 cases
smallest suite found over 200 greedy seeds: 9 cases
   (0, 0, 0, 1)
   (0, 1, 1, 0)
   (0, 2, 0, 0)
   (1, 0, 1, 0)
   (1, 1, 0, 1)
   (1, 2, 1, 1)
   (2, 0, 1, 0)
   (2, 1, 0, 1)
   (2, 2, 1, 1)
coverage: 37 of 37 pairs
```

Nine cases cover all 37 pairs, a quarter of the full matrix, and the bound is reached, so it is optimal.

**C.** The reported gap is (floor(D / I) + 1) × I when no probe falls in the outage, so a 200 ms outage is seen as 210 ms (10 ms), 220 (20 ms), 300 (100 ms), and 500 or 1000 ms (500 ms and 1 s probers):

```
--- true outage 200 ms, a prober that sends every I ms and sees a reply only if the probe is sent outside the outage; 20000 random phases
(I ms | mean measured gap | min | max | outages shorter than 50 ms missed entirely, for a 30 ms outage)
     10 |   210.0 |  210.0 |  210.0 |   0.0 %
     20 |   220.0 |  220.0 |  220.0 |   0.0 %
    100 |   300.0 |  300.0 |  300.0 |  70.7 %
    500 |   697.1 |  500.0 | 1000.0 |  94.7 %
   1000 |  1190.8 | 1000.0 | 2000.0 |  97.2 %
```

The 30 ms outage is missed **0, 0, 70.7, 94.7 and 97.2 percent** of the time. A 50 ms limit needs a resolution well below 50 ms: the 10 ms prober reports 30.0 ms for a 20 ms outage (section 3.4) and can separate 40 from 60; the 100 ms prober cannot see an outage shorter than its interval, and its answer is a multiple of 100. The prober interval should be about one fifth of the limit (10 ms for 50 ms) and must be reported with the gap.

**D.** A test with n tries is blocked with probability f^n, and a suite of 60 is red with probability 1 - (1 - f^n)^60:

```
--- 60 independent tests, each run fails with p=0.02 when healthy
(retries allowed n | healthy suite red | one test with a q=0.3 bug escapes | all 3 such bugs in the suite escape)
  1 |  70.24469 % |  70.00 % |  34.30 %
  2 |   2.37190 % |  91.00 % |  75.36 %
  3 |   0.04799 % |  97.30 % |  92.12 %
  4 |   0.00096 % |  99.19 % |  97.59 %
```

One try leaves the healthy suite red **70.2 percent** of the time, two tries **2.37 percent**, three **0.048 percent**: the answer is **3 tries**. The cost: a bug that fails with probability 0.3 passes **97.3 percent** of the time (1 - 0.3^3), and a suite with three such bugs lets all of them through **92.1 percent** of the time (from 34.3 percent with one try). Three tries solve the noise by making the suite blind to intermittent bugs. The better fix is to reduce f (find the source of the noise), and to run the intermittent hunters as a separate test with many runs.

**E.** Per case: 4 + 3 × 1.5 + 1 = **9.5 minutes**. Suite: 43 × 9.5 = 408.5 minutes = **6.81 hours**.

```
--- per case: setup 4.0 min + 3 runs x 90 s + teardown 1.0 min = 9.50 min
suite: 43 cases x 9.50 min = 6.81 h on one rig
  1 rigs: 6.81 h wall, builds per 10 h night: 1
  2 rigs: 3.40 h wall, builds per 10 h night: 2
  4 rigs: 1.70 h wall, builds per 10 h night: 5
--- with 15 runs per case for the noisy house set: 19.71 h on one rig
```

One rig fits **1 build** in a 10 hour night, two rigs 2 (3.40 hours each), four rigs 5 (1.70 hours each, floor(10 / 1.70) = 5). With 15 runs a case the case takes 4 + 22.5 + 1 = 27.5 minutes and the suite **19.71 hours**: it no longer fits a night on one rig and takes 4.93 hours on four. This is how the schedule decides the precision, and why the plan has to state the rig hours.

## 15. Cheat sheet

| Topic | Rule |
|---|---|
| Oracle | Write the pass rule with a number and a statistical rule before the first run |
| Spread | Real shaped wired path 0.11 percent; modelled Wi-Fi lab 2.89, house 18.25 percent |
| Runs for ±e | smallest n with t × cv / √n ≤ e; 54 for ±5 percent at 18.25 percent spread; grows as 1/e^2 |
| One run | cannot call a regression: 44.1 percent false alarm at a 2 percent rule |
| Order | Blocks AAA..BBB false regression 23.8 percent; randomise or interleave and pair |
| Latency | Report p99 and loss, not the mean; the mean stayed 0.22 ms through an 800 ms outage |
| Gap | Prober reports D + I; (floor(D / I) + 1) × I for a slow prober; interval about one fifth of the limit |
| Isolation | FSPL at 1 m: 40.2 dB at 2.44 GHz, 47.3 dB at 5.5 GHz; a 20 dBm AP leaks -27.3 dBm at 1 m, needs 77.7 dB |
| Scale | Find the knee; N = 1 first, subtract the floor; 100 real supplicants 2.29 s median, 2.3 ms per client |
| Matrix | 17,280 to 43 with all-pairs; pairs 100 percent, triples 54.5 percent, quads 16.2 percent |
| Retry | 3 tries: false block 0.01 percent, but a q = 0.5 bug escapes 87.5 percent; record retries |
| Runs to see a bug | n = ln 0.05 / ln(1 - q): 5, 14, 29, 59, 299 for q = 0.5, 0.2, 0.1, 0.05, 0.01 |
| Lab and house | Lab for regression and cause, house for realism and interop, field for the unknown; findings flow down to lab tests |
| Schedule | Rig hours = cases × minutes a case; 43 cases at 9.5 min is 6.81 h; 15 runs a case is 19.71 h |
| Report | Verdict, statistics with MDE, context, evidence, trend, what was not tested |

## Sources

- Candela Technologies course, sessions 8a, 8b, 8c and 8d (testing fundamentals, lab, test houses, test plan development): https://www.candelatech.com/course.php
- IEEE Std 802.11-2024 (the standard that defines conformance); Wi-Fi Alliance certification program descriptions: https://www.wi-fi.org/certification
- IETF benchmarking and measurement RFCs: RFC 2544, RFC 2679, RFC 2680, RFC 2681, RFC 3393, RFC 3550, RFC 6349, RFC 6815 (numbers and scope from memory, check at https://www.rfc-editor.org)
- iperf3 documentation: https://software.es.net/iperf/ and the `tc` tbf manual page of iproute2
- hostapd and wpa_supplicant (version 2.10): https://w1.fi
- mrncciew.com study material (CWNA, CWAP, CWSP and CCIE Wireless), background only; no page was quoted for this tutorial
- Combinatorial testing background: pairwise and t-way coverage, NIST combinatorial testing resources (concept from memory)
- Tutorials of this series: [WF 02](02_wifi_phy.md), [WF 03](03_wifi_mac.md), [WF 04](04_wifi_security.md), [WF 05](05_wifi_roaming_and_radio_management.md), [WF 07](07_wifi_architectures_mesh_and_services.md), [WF 08](08_wifi_capture_tools_and_debug.md), [WF 11](11_rf_fundamentals_and_link_budget.md)
