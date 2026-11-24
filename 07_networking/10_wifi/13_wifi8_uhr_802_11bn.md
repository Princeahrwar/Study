# WF 13: Wi-Fi 8 (802.11bn, Ultra High Reliability): multi-AP coordination, distributed MLO and what the draft changes

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · not in the Candela course; built from the 802.11bn primer on arXiv (2023), a 2026 tutorial on multi-AP coordination, the Wikipedia summary of the draft and the MaxLinear and arXiv search results listed in the Sources. Builds on [WF 12](12_wifi7_eht_and_mlo.md) (EHT, MLO, eMLSR), [WF 05](05_wifi_roaming_and_radio_management.md) (roaming) and [WF 06](06_wifi6_6e_7_features.md) (OFDMA, spatial reuse, TWT).*

Wi-Fi 7 raised the peak rate. **Wi-Fi 8 does not**: the draft keeps 320 MHz, 4096-QAM and the same bands, and aims at what the user feels, which is the tail of the delay and loss distributions when networks overlap, devices move and traffic is time-sensitive. The design question changes from "how fast can one AP send to one client" to "how do several APs behave as one system", so most of the new material is **coordination between APs** and **a client that stays attached while it moves**.

**Eight honest limits, stated first.**

1. **The standard is a draft, and this is the one tutorial built on drafts and papers.** Final approval is expected in 2028 and Wi-Fi Alliance certification in early 2028 according to the Wikipedia summary I read; the 2023 primer says its own targets were still under discussion. Every mechanism below is draft or from a paper and may change. Nothing here is a product behaviour.
2. **Sources disagree and I do not settle them.** The primer mentions raising the stream count from 8 to 16; the Wikipedia summary says up to 8 streams and a theoretical maximum near 23 Gbit/s (which my formula reproduces: 8 x 2,882.4 = **23,059 Mbit/s**). I use 8 and flag it.
3. **The 25 percent targets are draft goals, not results.** Higher throughput, lower 95th percentile latency and fewer lost MPDUs, each about 25 percent over Wi-Fi 7, are quoted by the tutorial and the summary; the primer states no final figures.
4. **My own models are not the papers' simulations.** Every number marked modelled comes from a short model of mine (a two-AP 6 GHz scene, a Shannon style link with an 8 dB gap, fixed overheads). The primer reports that coordinated beamforming was worse than plain MLO at 10 dB of null accuracy and better from 20 dB; my model puts the break-even near 11 dB for one geometry. The agreement is in shape only.
5. **I read only part of the long tutorial.** The fetch of the multi-AP tutorial returned the first 100,000 of 264,817 characters, so its performance results and sections on joint transmission and coordinated OFDMA were **not read**.
6. **Joint transmission and coordinated OFDMA are not in the first release, as far as I can tell.** The tutorial excerpt names them as candidate directions beyond 802.11bn, while the primer describes joint transmission as part of the study. I treat them as future work.
7. **No radio, no frames.** Unlike WF 12 there is no decoder for 802.11bn frames in `tshark` 4.2, so nothing here is a built frame; every experiment is arithmetic or a model.
8. **Numbers for new MCS, the long range PPDU and distributed-tone RUs are from memory or a one-line mention.** I give the arithmetic that motivates them, not their tables.

By the end you can:

- **say what Wi-Fi 8 is for** and what it leaves unchanged (same bands, 320 MHz, 4096-QAM, about **23.06 Gbit/s** theoretical at 8 streams; goals of about 25 percent better throughput at a given SINR, 95th percentile latency and MPDU loss, draft).
- **name the multi-AP coordination schemes** (coordinated beamforming, spatial reuse, TDMA, restricted TWT and channel recommendation; joint transmission as a later step) and say what each needs.
- **work out when coordinated beamforming pays** (modelled, two APs 8 m and 12 m from clients, 20 dBm, 160 MHz at 6 GHz: both transmitting at once with no nulling gives **92 Mbit/s** per BSS against **370** for taking turns, and the break-even null depth is **11.0 dB** with 4 percent sounding overhead, **16.7 dB** with no overhead if the neighbour is as close as the own AP, and 0 dB when it is 30 m away).
- **say when spatial reuse by power control pays** (modelled: it loses to turns when the neighbour is within 20 m, at **x0.85** and **x0.98**, and wins at **x1.30** at 30 m and **x1.71** at 45 m).
- **compare a roam with a distributed multi-link device** (modelled, 120 frames a second: a full roam loses **174** frames, 802.11r with 802.11k **11**, an MLD with a link already up on the next AP **0**).
- **explain the distributed-tone resource unit** (computed: a 26-tone RU spread over 20 MHz may transmit **9.93 dB** more power under a PSD limit than a contiguous one, which is a range factor of **1.92** at exponent 3.5).
- **say what Wi-Fi 8 demands of the system around it** (a coordinating backhaul, time sync, channel state sharing, roaming state transfer) and how to test it.

## Where the numbers come from

Each experiment is a script `sN_*.py` in `prog/` with its output frozen in `v/`, run with `run.sh`. `mapc.py` is the link model they share: path loss at 6 GHz with a chosen exponent, a noise floor of -85.0 dBm over 160 MHz (7 dB noise figure), and a rate from a Shannon style formula with an 8 dB gap, capped at the 4096-QAM 5/6 efficiency, times an OFDM efficiency of 0.80.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not modelled or computed here is marked **from the source** (stated in a paper or summary) or **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| Coordinated beamforming (`s1_cbf.py`) | closed form, 8 null depths and 6 sounding rates, under 1 s |
| Spatial reuse and TDMA (`s2_sr_tdma.py`) | power searches over 441 pairs for 5 distances, under 2 s |
| Roaming (`s3_roam.py`) | 4 mechanisms, 100,000 frames each, about 2 s |
| Distributed-tone RU (`s4_dru.py`) | arithmetic, under 1 s |
| Problems (`ans_a` to `ans_e`) | each under 1 s |

## Mapping to the Candela course

The Candela course does not cover Wi-Fi 8. This tutorial follows the mrncciew and primer syllabus the plan names for WF 13.

---


## 1. What Wi-Fi 8 is for

| Question | Wi-Fi 7 answer | Wi-Fi 8 draft answer |
|---|---|---|
| Peak rate | 46.1 Gbit/s at 16 streams ([WF 12](12_wifi7_eht_and_mlo.md)) | unchanged width and QAM; the summary I read gives up to 8 streams and about 23 Gbit/s |
| Overlapping BSSs | contend and defer (CSMA/CA), or reuse space blindly (OBSS PD) | **coordinate**: share a TXOP, null interference, set powers |
| A client that moves | roam: reassociate to the next AP, with or without 802.11r | **seamless roaming domain**; links on several APs held by one MLD |
| Urgent traffic in a busy cell | EDCA priority, restricted TWT | high-priority EDCA, preemption of a lower-priority transmission |
| Weak clients and low-power uplink | long-range PPDU in 802.11be ER SU | a 20 MHz long-range PPDU, distributed-tone RUs |
| What is measured | rate | 95th and 99.9th percentile latency, MPDU loss, effective throughput at a given SINR |

The use cases the primer lists (from the source, all marked as needing more than the standard alone) show why the tail matters: immersive streaming (99.9 percent reliability, under 20 ms latency, 1 to 10 Gbit/s down), digital twins in manufacturing (99.9 to 99.999999 percent reliability, 0.1 to 100 ms), e-health and cooperative robots (up to nine nines, 0.5 to 25 ms for control). A rate of tens of Gbit/s does not help a control loop that needs the 99.999th percentile packet to arrive in 10 ms.

**Status.** The summary I read gives a task group formed in May 2021, drafts D1 to D4 in 2022 to 2024 and D5 to D7 through 2027, approval in 2028 and certification starting in early 2028, with products possibly before ratification (two different approval months appear on the page). The Wi-Fi 7 timeline is a guide: its chips shipped on drafts and the layout moved afterwards (the D3.0 decode of WF 12). Treat a Wi-Fi 8 chip announced in 2026 as a draft implementation.

**What stays the same.** The 2.4, 5 and 6 GHz bands, the 320 MHz maximum width, 4096-QAM, the multi-link device and its modes from WF 12, WPA3 and PMF. The U-SIG preamble of WF 12 is the reason this is possible: the front of the PPDU is version independent, so a new amendment adds its signalling after it.

## 2. Multi-AP coordination

### 2.1 The idea and the set of schemes

APs in one building today treat each other as noise. Multi-AP coordination (MAPC in the papers) lets a group of APs agree, for one transmission opportunity, who sends when, on which subchannels, at what power and in which spatial directions. The summary I read lists **five schemes**: coordinated restricted TWT, coordinated spatial reuse, coordinated beamforming, coordinated TDMA and channel recommendation. The tutorial describes three of them in detail (beamforming, spatial reuse and TDMA) and names **joint transmission** (several APs act as one distributed antenna array) and **coordinated OFDMA** (different APs on different parts of the band) as directions beyond the first release. The scheme comparison, in the order of what each demands:

| Scheme | What the APs share | What they need | Gain comes from |
|---|---|---|---|
| Coordinated TDMA | one TXOP, divided in time | agreed time slots; same primary channel | no collisions, no backoff between the APs |
| Coordinated spatial reuse | transmit power limits | signal strength between BSSs, no channel state | simultaneous transmission when separation allows |
| Coordinated beamforming | spatial nulls toward each other's clients | channel state from sounding, tight timing | simultaneous transmission even when the cells overlap |
| Joint transmission (later) | data and channel state, one distributed array | time, frequency and phase synchronisation, fast backhaul | all antennas serve all clients |

The cost climbs the same way: TDMA needs only messages, spatial reuse needs messages and measurements, beamforming adds sounding and tight timing, joint transmission adds a phase-locked backhaul.

### 2.2 Coordinated beamforming

From the tutorial (draft): two coordinated APs transmit at the same time, each steering a main lobe at its own client and a **spatial null** at the other AP's client. The limits in the draft I read are two APs, two spatial streams per client, four streams in total and four clients. The sounding is a joint procedure: the initiating AP sends an invitation and the other accepts, an NDP announcement follows, then a **joint NDP** in which both APs transmit on the same channel with orthogonal training columns, then a poll that makes each client send a compressed beamforming report that both APs receive. The coordinating AP's trigger frame carries what is needed for a common preamble (the L-SIG length, guard interval, LTF type and number of LTF symbols), so the two PPDUs start together and look like one. The draft's own caveat: it is not always beneficial, particularly at low or moderate load or when the channel changes quickly, for example with a moving client.

The question to answer before building it is **how deep the null has to be**. A null removes interference by D dB at the other BSS's client. If D is small the clients see a poor SINR and fall to a low MCS; if it is large the clients are noise limited and both BSSs run at near full rate on all of the air instead of half.

```
--- two overlapping BSSs, 6 GHz, 160 MHz, 20 dBm, exponent 3.5: noise -85.0 dBm; at a client 8 m from its AP and 12 m from the other AP the signal is -59.6 dBm and the neighbour AP is -65.8 dBm
```

```
--- the two baselines: taking turns (each AP gets half the air at SNR 25.3 dB, 741 Mbit/s while on the air) and transmitting together with no nulling (SINR 6.1 dB, 92 Mbit/s)
    per BSS: turns 370 Mbit/s; both at once with no coordination 92 Mbit/s
```

```
    per BSS: turns 370 Mbit/s; both at once with no coordination 92 Mbit/s
--- coordinated beamforming: both APs transmit together and each puts a null of depth D toward the other BSS client; the interference falls by D dB; sounding and feedback take a share of the air
null depth dB   SINR dB   rate while on air   per BSS before overhead   after 4 percent sounding   against turns
            0       6.1                 92                        92                        88           0.24
            5      11.0                203                       203                       194           0.52
           10      15.7                355                       355                       341           0.92
           15      19.8                512                       512                       491           1.33
           20      22.7                632                       632                       607           1.64
           25      24.3                699                       699                       671           1.81
           30      25.0                727                       727                       697           1.88
           40      25.3                739                       739                       710           1.92
```

Read it as three regimes. **Below about 5 dB** the transmit-together strategy is worse than turns: the neighbour's signal is only 6.2 dB below the wanted one, and the rate collapses to 92 Mbit/s (x0.24 of turns). **From 10 to 15 dB** the clients run at 355 to 512 Mbit/s, and with a sounding overhead of 4 percent the break-even is **11.0 dB**. **Above 25 dB** the gain saturates at about **x1.8 to x1.9** because the SNR is the limit; a null deeper than the noise floor adds nothing. The primer's simulation found the same shape (worse than plain MLO at 10 dB of null accuracy, better from 20 dB, about an order of magnitude better in the 99.9999th percentile delay at 30 dB); its conditions were different (two 4-antenna APs, two 160 MHz links, 2 Gbit/s on/off traffic) and my numbers are not a reproduction of them.

How deep the null *can* be is a hardware and channel question: it needs accurate channel state, fresh enough, and calibrated transmit chains. Practical nulls of 20 to 30 dB are plausible; deeper ones are limited by calibration and feedback quantisation (from memory).

```
--- the break-even null depth, where coordination first beats taking turns (4 percent sounding overhead): 11.0 dB
```

```
--- the sounding cost against how fast the channel changes: one sounding round is 0.5 ms (modelled); if the channel is stable for T ms the overhead is 0.5 / T
  stable for   2 ms: overhead 25.0 percent, per BSS at a 25 dB null 524 Mbit/s (1.41 of turns)
  stable for   5 ms: overhead 10.0 percent, per BSS at a 25 dB null 629 Mbit/s (1.70 of turns)
  stable for  10 ms: overhead  5.0 percent, per BSS at a 25 dB null 664 Mbit/s (1.79 of turns)
  stable for  25 ms: overhead  2.0 percent, per BSS at a 25 dB null 685 Mbit/s (1.85 of turns)
  stable for  50 ms: overhead  1.0 percent, per BSS at a 25 dB null 692 Mbit/s (1.87 of turns)
  stable for 100 ms: overhead  0.5 percent, per BSS at a 25 dB null 695 Mbit/s (1.88 of turns)
```

The sounding cost is the second constraint: a 0.5 ms round every 2 ms (a moving client) is **25 percent** of the air and the gain falls from x1.88 to x1.41; every 50 ms it is **1 percent**. Coordinated beamforming is therefore a feature for slow channels (fixed devices, an office) and not for people walking. The break-even also depends on the geometry:

```
--- baseline: taking turns gives 370 Mbit/s per BSS (741 Mbit/s at SNR 25.3 dB on half the air)
--- break even null depth by neighbour distance and sounding overhead (smallest D in 0.1 dB steps for which coordination matches turns)
neighbour m   overhead 0%   overhead 10%   overhead 25%
          8       16.7 dB        18.0 dB        20.6 dB
         12       10.5 dB        11.8 dB        14.4 dB
         20        2.8 dB         4.0 dB         6.7 dB
         30        0.0 dB         0.0 dB         0.5 dB
```

A neighbour as near as your own AP (8 m) needs a **16.7 dB** null even with no overhead; at 30 m the cells barely interact and no null is needed.

### 2.3 Coordinated spatial reuse

Spatial reuse by power control needs no channel state. The coordinating AP sets its own power and a **ceiling** for the coordinated AP so that both can transmit at once; the tutorial's example is 17 dBm and 12 dBm and says the standard does not specify how to compute them. The exchange is an invitation and response, an optional control frame exchange when a client uses DPS or eMLSR, and a coordinated trigger that confirms the PPDU duration and power limits. Two PPDU modes exist, one allowing mixed Wi-Fi 8 and Wi-Fi 7 APs and one Wi-Fi 8 only (from the tutorial).

```
--- coordinated spatial reuse: AP 1 at 17 dBm, AP 2 capped at a power chosen by the coordinator; both transmit together; per BSS Mbit/s (turns at 20 dBm give 370 each)
AP 2 cap dBm   BSS 1    BSS 2     sum   against turns (sum 741)
          -5     575        1     576    0.78
           0     511        2     513    0.69
           5     396        7     403    0.54
          10     253       22     275    0.37
          15     128       63     191    0.26
          20      52      152     204    0.28
```

```
--- the best cap for the sum rate, scanning 1 dB steps: -5 dBm (sum 576 Mbit/s); the standard does not say how to choose the numbers, so this is a search I ran, not a rule
```

```
--- the 17 and 12 dBm example of the tutorial: BSS 1 198, BSS 2 34, sum 232 Mbit/s
```

In the 8 m and 12 m scene the tutorial's example powers deliver **198 and 34 Mbit/s** (sum 232) against 741 for turns: the cells are too close. The question is the distance:

```
--- when does spatial reuse pay? the distance from each client to the other BSS AP, the best pair of powers found by a search over 0 to 20 dBm in 1 dB steps, against taking turns at 20 dBm
cross distance m   best powers dBm   sum Mbit/s   against turns
              12       20 and  0          633            0.85
              20       20 and  0          724            0.98
              30       20 and 20          960            1.30
              45       20 and 20         1268            1.71
              60       20 and 20         1388            1.87
```

Spatial reuse **loses** when the neighbour is within 20 m of the client (x0.85 at 12 m and x0.98 at 20 m, with the best pair of powers I could find), and **wins from 30 m** (x1.30) and strongly at 45 and 60 m (x1.71 and x1.87). The mechanism is the same as the old OBSS PD of WF 06; what the coordination adds is that both sides know the other's power, so the receiver can plan its MCS around the interference instead of discovering it with retries.

### 2.4 Coordinated TDMA

The coordinating AP, having won a TXOP, **gives part of it to another AP** on the same primary 20 MHz channel, so the two never collide. The draft's exchange: an initial control frame polls the other AP, its reply carries the time it wants in a multi-STA block ack, trigger frames then name the next AP and the time and channels, the coordinated AP answers with a CTS, and if it finishes early a TXOP return frame hands the time back. Traffic priority is kept: shared time may only carry traffic at or above the access category of the TXOP. The APs must have overlapping BSS bandwidth, checked through a profile exchange. The cost the tutorial notes: the control exchange costs airtime before any data, which hurts at low and moderate load, equal splits are not optimal, and BSSs that are not part of the group can see longer backoff.

```
--- coordinated TDMA: the air of one 741 Mbit/s link is split in time; the coordination exchange costs airtime
  demand 600 and 150 Mbit/s: equal split delivers 370 + 150 = 520; demand based delivers 593 + 148 = 741
  demand 400 and 400 Mbit/s: equal split delivers 370 + 370 = 741; demand based delivers 370 + 370 = 741
  demand 700 and  40 Mbit/s: equal split delivers 370 +  40 = 410; demand based delivers 700 +  40 = 740
  demand 300 and 300 Mbit/s: equal split delivers 300 + 300 = 600; demand based delivers 300 + 300 = 600
```

```
--- the exchange before the first data frame (initial control frame, response, trigger, time allocation, about 150 us modelled) against the shared TXOP
  TXOP 0.5 ms: 30.0 percent overhead
  TXOP 1.0 ms: 15.0 percent overhead
  TXOP 2.0 ms: 7.5 percent overhead
  TXOP 4.0 ms: 3.8 percent overhead
```

The arithmetic shows the two design questions. **Split by demand, not equally**: with demands of 600 and 150 Mbit/s an equal split delivers 520 and a demand-based split delivers all 741; with 700 and 40 it is 410 against 740. **Keep the TXOP long**: a 150 us exchange (my assumption) is **30 percent** of a 0.5 ms TXOP and **3.8 percent** of a 4 ms one, so coordination wants aggregation, not short voice bursts.

## 3. Distributed MLO and seamless roaming

In Wi-Fi 7 an AP MLD is one box with several radios. The Wi-Fi 8 work (from the primer, draft) lets **non-co-located APs** be the affiliated APs of one MLD: a distributed virtual cell. A device that moves keeps at least one of its links up on the AP it is leaving while it brings up a link on the next, which is native make-before-break roaming. The same idea sits behind the **Seamless Roaming Domain** of the summary: session context (keys, sequence numbers, buffered frames) is transferred between AP MLDs so a handoff does not need a new handshake or lose the queue.

What it must keep in sync across APs: keys (one pairwise key set per MLD in WF 12), the sequence number and block ack state per TID, and the buffered downlink frames, over a backhaul fast enough to move a frame between APs inside one frame time. That is the demand Wi-Fi 8 places on the wired network: not bandwidth but latency and synchronisation.

```
--- packets lost and the worst gap during one move between two APs, 120 frames per second, by mechanism (modelled outage times)
mechanism                                     outage ms   frames lost   worst gap ms
legacy roam: scan, 802.1X (WF 05 full)             1451           174        1459.3
802.11r fast transition with 11k (WF 05)             96            11         104.3
MLD, one link per AP, make before break               0             0           8.3
MLD, the second link was not yet up                  40             4          48.3
--- tail latency of a 2 ms jitter background plus a move: 100000 frames, 99th and 99.99th percentile one-way delay in ms, one move per 20 s
```

At 120 frames a second (a holographic stream, the primer's example) a **full roam loses 174 frames** and a gap of 1.46 s; **802.11r with 802.11k loses 11** and a gap of 104 ms (the WF 05 figures); a distributed MLD whose second link is already up loses **0** and the worst gap is one frame time, **8.3 ms**; if the second link was not yet up it loses 4 (a 40 ms outage, my assumption).

```
legacy roam: scan, 802.1X (WF 05 full)        p99 1252.5 ms   p99.99  1454.1 ms
802.11r fast transition with 11k (WF 05)      p99   10.5 ms   p99.99    98.9 ms
MLD, one link per AP, make before break       p99    9.2 ms   p99.99    17.7 ms
MLD, the second link was not yet up           p99    9.8 ms   p99.99    42.5 ms
--- what the distributed MLD needs that a plain roam does not (from memory of the primer): the APs act as affiliated APs of one AP MLD, so they share keys, sequence numbers and buffers over a backhaul fast enough to move frames between them within a frame time
```

The tail tells the story the average hides: with one move every 20 s the 99th percentile delay is **1,252 ms** for a full roam, **10.5 ms** for 802.11r and **9.2 ms** with a ready MLD link; the 99.99th percentile is **1,454, 98.9 and 17.7 ms**. A 20 ms budget is met only by the last case, and only if the next link is up before the move.


## 4. PHY and MAC changes beyond coordination

### 4.1 Distributed-tone resource units and the long-range PPDU

A contiguous resource unit occupies a narrow slice of the band. In 6 GHz a low power client is limited by **power spectral density**, so a narrow RU is also a low power RU. A **distributed-tone RU** takes the same number of tones but spreads them across 20, 40 or 80 MHz, so the PSD rule applies to the whole spread width. With the limit I assume (-1 dBm per MHz for a client, from memory of the FCC rules; check the current rules):

```
RU tones   occupied bandwidth MHz   power limit dBm (PSD times width)   against a 242 tone RU
      26                     2.03                                2.08      -9.69 dB
      52                     4.06                                5.09      -6.68 dB
     106                     8.28                                8.18      -3.59 dB
```

```
--- a distributed-tone RU takes the same number of tones but spreads them over 20, 40 or 80 MHz (from memory of the 802.11bn draft work), so the PSD limit applies to the whole spread width
   26 tones spread over 20 MHz: power limit 12.01 dBm against  2.08 dBm for the contiguous RU: +9.93 dB
   26 tones spread over 40 MHz: power limit 15.02 dBm against  2.08 dBm for the contiguous RU: +12.94 dB
   26 tones spread over 80 MHz: power limit 18.03 dBm against  2.08 dBm for the contiguous RU: +15.95 dB
   52 tones spread over 20 MHz: power limit 12.01 dBm against  5.09 dBm for the contiguous RU: +6.92 dB
   52 tones spread over 40 MHz: power limit 15.02 dBm against  5.09 dBm for the contiguous RU: +9.93 dB
   52 tones spread over 80 MHz: power limit 18.03 dBm against  5.09 dBm for the contiguous RU: +12.94 dB
  106 tones spread over 20 MHz: power limit 12.01 dBm against  8.18 dBm for the contiguous RU: +3.83 dB
  106 tones spread over 40 MHz: power limit 15.02 dBm against  8.18 dBm for the contiguous RU: +6.84 dB
  106 tones spread over 80 MHz: power limit 18.03 dBm against  8.18 dBm for the contiguous RU: +9.85 dB
```

A 26-tone RU spread over 20 MHz may transmit **12.0 dBm** instead of **2.1 dBm**: **9.93 dB** more, a range factor of about **1.92** at exponent 3.5 (problem D). The price is that the spread tones fade differently, so the RU is not one flat narrow channel; the gain is link budget, not rate. The **20 MHz long-range PPDU** (from the summary) is the companion for weak clients. I have no table for either; I give the arithmetic that motivates them.

### 4.2 The rest of the list (from the sources, draft, not modelled)

- **Four new MCS values**, which the summary says improve rates by 5 to 30 percent depending on conditions (the values are not in my sources).
- **Dynamic subband operation and non-primary channel access**: a transmitter or receiver may use a part of the channel other than the one that holds the primary 20 MHz (so an AP is not blocked by a busy primary when a secondary is free; compare puncturing in WF 12 section 3). Details not read.
- **High-priority EDCA and TXOP preemption**: the primer describes an aggregated multi-dimensional PPDU design that lets the TXOP holder replace a best-effort transmission on one RU with urgent traffic, with the interrupted frames retransmitted; and, for a device that is not the holder, using gaps between PPDUs or a short guard at the end of scheduled TXOPs.
- **HARQ**: a receiver combines a corrupted transmission with its retransmission, which lowers the SINR needed for the same delivery probability (draft; the primer lists it among the determinism tools).
- **TXOP sharing and peer to peer**: part of an AP's TXOP given to a client for uplink or for a direct transfer.
- **Coordinated restricted TWT and channel recommendation**: the APs align protected service periods, and recommend channels to each other.
- **Better power save for APs and better coexistence** with Bluetooth, Zigbee and UWB in the same device.

## 5. What Wi-Fi 8 demands of the system around it

| Mechanism | Needs from the network | If it is missing |
|---|---|---|
| Coordinated TDMA | a way for APs to find each other and exchange frames (over the air or wired), agreement on the primary channel | the group falls back to ordinary contention |
| Spatial reuse | RSSI between BSSs, fresh | power limits too high or too low, retries |
| Coordinated beamforming | channel state, calibration, timing within a fraction of a symbol, a stable channel | nulls shallower than the break-even (11.0 dB with 4 percent overhead in the model) and a loss against turns |
| Joint transmission (later) | phase locked clocks, a low latency data backhaul | not usable |
| Distributed MLO | the APs share keys, queues and sequence state over a fast backhaul | roam losses of a normal roam |
| Seamless roaming domain | context transfer between AP MLDs | full reassociation |

Two points for a platform engineer. **The controller or the cloud becomes part of the PHY loop**: grouping, time slots and power limits are computed somewhere (the draft does not say how), so a bad coordinator is a bad radio. **Mixed fleets are the norm**: the spatial reuse PPDU has a mode for Wi-Fi 7 and Wi-Fi 8 APs together precisely because coordination groups will include older APs.

## 6. Testing and debugging Wi-Fi 8 behaviour

The ideas of [WF 09](09_wifi_testing.md) and [WF 08](08_wifi_capture_tools_and_debug.md) carry over with three changes. **Measure the tail, not the mean**: collect 99th and 99.99th percentile latency and MPDU loss per move, as the roaming table does (in the roam model the 99.99th percentile ranged from 17.7 to 1,454 ms across the four cases). **Test with more than one AP and one client**: a coordination bug appears only when the groups form and break, so the test needs two or more APs, overlapping cells, moving clients and traffic with a deadline. **Capture each AP and the coordination frames**: trigger frames, invitations and responses are the new management traffic; there is no `tshark` 4.2 decoder for them, so a capture shows timing (who transmitted together, who deferred) before it shows fields. Compare simultaneous transmissions of two APs against the expected schedule.

## 7. Common mistakes

1. **Reading Wi-Fi 8 as a faster Wi-Fi 7.** The peak is unchanged (about 23 Gbit/s at 8 streams); the target is the tail.
2. **Treating the 25 percent figures as results.** They are draft goals.
3. **Expecting coordination to win everywhere.** In the model coordinated beamforming loses to taking turns below 11.0 dB of null depth, and spatial reuse loses within 20 m.
4. **Ignoring sounding cost.** 25 percent of the air at a 2 ms channel coherence; it is a feature for slow channels.
5. **Splitting TDMA time equally.** 520 Mbit/s against 741 for a 600 and 150 Mbit/s demand.
6. **Using short bursts with a 150 us exchange.** 30 percent overhead at 0.5 ms.
7. **Counting the average latency.** Averages hid a 1,454 ms tail in the roam table.
8. **Assuming the AP can do it alone.** Coordination needs a group, a protocol and a backhaul.
9. **Treating a draft chip as the standard.** The layout moved between Wi-Fi 7 drafts and final; expect the same here.
10. **Mixing up the paper's simulation and my model.** They agree in shape only.
11. **Assuming joint transmission ships first.** The sources I read treat it as beyond the first release.
12. **Forgetting the PSD limit in 6 GHz.** A narrow RU at a low-power class is power limited by 9.93 dB against a spread one.

## 8. Interview questions

1. **What problem does Wi-Fi 8 address that Wi-Fi 7 does not?** The tail of latency and loss under overlap, mobility and urgent traffic, not the peak rate.
2. **Name the multi-AP coordination schemes and their costs.** Coordinated TDMA, spatial reuse, beamforming (and later joint transmission), with costs rising from messages to power measurements to channel state and finally phase synchronisation (section 2.1).
3. **When does coordinated beamforming beat taking turns?** When the null is deeper than the break-even for the geometry (11.0 dB at 8 m and 12 m with 4 percent sounding) and the channel is stable enough that sounding is cheap (1 to 5 percent).
4. **Why does spatial reuse fail between close cells?** The signal to interference ratio at the client is too low for a useful MCS; the model shows x0.85 at 12 m and x1.30 at 30 m.
5. **How is a distributed MLD different from a roam?** The device keeps links on two APs and shares keys and queues, so it loses 0 frames in the model against 11 for 802.11r and 174 for a full roam.
6. **What does the backhaul need to provide?** Latency and time sync rather than bandwidth (sections 3 and 5).
7. **Why a distributed-tone RU?** A PSD-limited uplink gets 9.93 dB more power by spreading 26 tones over 20 MHz.
8. **How would you test a coordination feature?** Two or more APs, overlapping cells, moving clients, deadline traffic, per-AP captures, and tail percentiles.
9. **Why is the standard's draft status important to an engineer?** Frame layouts and parameters may change; code and tests must tolerate it.
10. **What does coordinated TDMA do to a non-member BSS?** It may see longer backoff, because the group holds the TXOP.

## 9. Problems

Work each one before reading section 10.

**Problem A. Break-even null depth.** For a client 8 m from its AP, a neighbour AP at 8, 12, 20 and 30 m, 20 dBm transmit power, 160 MHz at 6 GHz, find the smallest null depth at which both BSSs transmitting at once match taking turns, with 0, 10 and 25 percent sounding overhead.

**Problem B. Sounding overhead.** A joint sounding round has an invitation (60 us), an NDP announcement (50 us), a joint NDP (80 us) and, per client, a poll (40 us), a report and two SIFS (16 us each), plus three SIFS. For 2, 4 and 8 clients and reports of 100, 200 and 400 us give the round length, and the share of the air if the channel stays good for 5, 10, 25 and 50 ms (4 clients, 200 us reports).

**Problem C. Frames lost in a roam.** For streams of 30, 60, 120 and 240 frames a second, the frames lost for outages of 0, 40, 96 and 1,451 ms.

**Problem D. A distributed-tone RU.** For 26, 52 and 106 tones spread over 20, 40 and 80 MHz, the power limit gain over the contiguous RU under -1 dBm per MHz and the range factor at exponent 3.5.

**Problem E. Sharing a TXOP.** For demands of 600 and 150, 700 and 40, 500 and 500 and 200 and 100 Mbit/s on a 741 Mbit/s link, what an equal split and a demand-based split deliver.

## 10. Answers

**Answer A.** Compare the rate with the neighbour's interference reduced by D, scaled by one minus the overhead, with half the air at the interference-free SNR. A neighbour as close as your own AP needs a deep null even with no overhead, and one at 30 m needs almost none.

```
--- baseline: taking turns gives 370 Mbit/s per BSS (741 Mbit/s at SNR 25.3 dB on half the air)
--- break even null depth by neighbour distance and sounding overhead (smallest D in 0.1 dB steps for which coordination matches turns)
neighbour m   overhead 0%   overhead 10%   overhead 25%
          8       16.7 dB        18.0 dB        20.6 dB
         12       10.5 dB        11.8 dB        14.4 dB
         20        2.8 dB         4.0 dB         6.7 dB
         30        0.0 dB         0.0 dB         0.5 dB
```

**Answer B.** The round length is 60 + 50 + 80 + clients x (40 + report + 32) + 48 us. Four clients with 200 us reports take 1,326 us, which is 26.5 percent of a 5 ms coherence and 2.7 percent of 50 ms.

```
--- one sounding round: the invitation and response, an NDP announcement, a joint NDP, then a poll and one compressed beamforming report per client
clients   report 100 us   report 200 us   report 400 us    (round length in us)
      2             582             782            1182
      4             926            1326            2126
      8            1614            2414            4014
--- as a share of the air if the channel stays good for T ms, 4 clients, 200 us reports
  T =  5 ms: 26.5 percent
  T = 10 ms: 13.3 percent
  T = 25 ms: 5.3 percent
  T = 50 ms: 2.7 percent
```

**Answer C.** The frames lost are the outage divided by the frame interval, rounded down.

```
---  30 frames per second (one frame every 33.3 ms): 0 ms outage loses 0, 40 ms outage loses 1, 96 ms outage loses 2, 1451 ms outage loses 43
---  60 frames per second (one frame every 16.7 ms): 0 ms outage loses 0, 40 ms outage loses 2, 96 ms outage loses 5, 1451 ms outage loses 87
--- 120 frames per second (one frame every 8.3 ms): 0 ms outage loses 0, 40 ms outage loses 4, 96 ms outage loses 11, 1451 ms outage loses 174
--- 240 frames per second (one frame every 4.2 ms): 0 ms outage loses 0, 40 ms outage loses 9, 96 ms outage loses 23, 1451 ms outage loses 348
--- a latency budget of 20 ms (immersive streaming, from the primer) tolerates an outage of at most 20 ms minus the delivery time; any of the three non-zero outages above breaks it
```

**Answer D.** The gain depends on the tones and the width: the same ratio of width to tone bandwidth gives the same gain (9.93 dB for 26 tones over 20 MHz and 52 over 40).

```
 26 tones: contiguous  2.08 dBm (2.03 MHz), spread over 20 MHz 12.01 dBm, gain +9.93 dB, which at path loss exponent 3.5 extends the range by a factor 1.92
 52 tones: contiguous  5.09 dBm (4.06 MHz), spread over 40 MHz 15.02 dBm, gain +9.93 dB, which at path loss exponent 3.5 extends the range by a factor 1.92
106 tones: contiguous  8.18 dBm (8.28 MHz), spread over 80 MHz 18.03 dBm, gain +9.85 dB, which at path loss exponent 3.5 extends the range by a factor 1.91
 26 tones: contiguous  2.08 dBm (2.03 MHz), spread over 80 MHz 18.03 dBm, gain +15.95 dB, which at path loss exponent 3.5 extends the range by a factor 2.86
```

**Answer E.** The equal split wastes the unused half of a lightly loaded partner. At 700 and 40 it delivers 410 where the link could carry 740.

```
--- three BSS pairs, link capacity 741 Mbit/s, shares of the TXOP
demand 600 and 150: equal 370 + 150 = 520; demand based 593 + 148 = 741; waste of the equal split 30 percent of the capacity
demand 700 and  40: equal 370 +  40 = 410; demand based 700 +  40 = 740; waste of the equal split 44 percent of the capacity
demand 500 and 500: equal 370 + 370 = 741; demand based 370 + 370 = 741; waste of the equal split 0 percent of the capacity
demand 200 and 100: equal 200 + 100 = 300; demand based 200 + 100 = 300; waste of the equal split 0 percent of the capacity
```

## 11. Cheat sheet

| Item | Value or rule |
|---|---|
| Aim | tail latency, MPDU loss and effective throughput at a given SINR (about 25 percent over Wi-Fi 7, draft) |
| Unchanged | 2.4, 5 and 6 GHz, 320 MHz, 4096-QAM; about 23.06 Gbit/s at 8 streams |
| Schemes | coordinated TDMA, spatial reuse, beamforming, restricted TWT, channel recommendation; joint transmission later |
| Co-BF model | 92 Mbit/s at 0 dB null, 355 at 10, 699 at 25 (370 for turns); break-even 11.0 dB at 4 percent overhead |
| Sounding | 0.5 ms round: 25 percent of the air at 2 ms coherence, 1 percent at 50 ms |
| Co-SR model | loses within 20 m (x0.85, x0.98), wins at 30 m (x1.30) and 45 m (x1.71) |
| Co-TDMA | split by demand; a 150 us exchange is 30 percent of 0.5 ms and 3.8 percent of 4 ms |
| Roam at 120 frames per second | full 174, 802.11r 11, ready MLD link 0 frames lost |
| DRU | +9.93 dB power for 26 tones over 20 MHz, range x1.92 at exponent 3.5 |
| Status | draft, approval about 2028; sources disagree on streams (8 or 16) |
| Not here | built frames, joint transmission, the new MCS table |

Next: [WF 14, amendments and special PHYs](14_wifi_amendments_and_special_phys.md) (not yet written). Related: [WF 12](12_wifi7_eht_and_mlo.md), [WF 05](05_wifi_roaming_and_radio_management.md), [WF 06](06_wifi6_6e_7_features.md), [WF 09](09_wifi_testing.md).

## Sources

- "What Will Wi-Fi 8 Be? A Primer on IEEE 802.11bn Ultra High Reliability": https://arxiv.org/html/2303.10442v2
- "A Tutorial on IEEE 802.11bn Multi-AP Coordination for Wi-Fi 8: From Standardization to Performance Evaluation": https://arxiv.org/html/2606.13759v1 (first 100,000 of 264,817 characters read)
- Wikipedia, IEEE 802.11bn: https://en.wikipedia.org/wiki/IEEE_802.11bn
- Found in search and not read: "Towards Ultra-High Reliability in Wi-Fi 8" (arXiv 2607.19931), the Wi-Fi 8 tutorial on ResearchGate, the MaxLinear Wi-Fi 8 white paper for broadband providers
- IEEE 802.11be and the multi-link material of [WF 12](12_wifi7_eht_and_mlo.md)
- FCC 6 GHz rules for the PSD figure (from memory)

Numbers marked modelled come from `s1_cbf.py`, `s2_sr_tdma.py`, `s3_roam.py` and the problem scripts; mechanisms are from the sources or from memory and are marked draft.
