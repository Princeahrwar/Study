# WF 14: Amendments and special-purpose Wi-Fi: HaLow, 60 GHz, V2X, ranging, wake-up radio, sensing, groupcast and direct links

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · not in the Candela course; added after the 24 Nov 2026 coverage audit, which found these amendments only named in WF 01, 06 and 07. Builds on [WF 02](02_wifi_phy.md) (OFDM and rate formulas), [WF 03](03_wifi_mac.md) (DCF, TIM, scanning, power save), [WF 06](06_wifi6_6e_7_features.md) (TWT) and [WF 11](11_rf_fundamentals_and_link_budget.md) (link budget).*

Most of 802.11 is one design stretched over eight PHY generations. A handful of amendments break away from it because the use case demands a different radio: **802.11ah (HaLow)** trades rate for range and thousands of sensors below 1 GHz, **802.11ad and ay** trade range for gigabits at 60 GHz, **802.11p and bd** drop the association because a car cannot wait for one, **802.11ba** adds a second, almost passive receiver, **802.11mc and az** turn the frame exchange into a tape measure, **802.11bf** turns the channel into a motion sensor, **802.11aa** makes multicast reliable and **TDLS** lets two clients skip the AP. Each is a different answer to "what if the medium access, the rate or the radio itself were designed for this one job?", and each fails in a characteristic way that a platform engineer is expected to know.

**Eight honest limits, stated first.**

1. **Nothing here was run on a radio.** The sandbox has no wireless stack. Every number is arithmetic from the numerology, a model, or a simulation, and each is marked **computed**, **modelled** or **simulated**. Four frames were built byte by byte and decoded by `tshark` 4.2: an FTM request and two FTM frames, and a TDLS setup request. Nothing else was captured.
2. **I checked key facts against sources this session, but not every one.** I fetched the Wikipedia pages for 802.11ah and 802.11ay and three arXiv papers (802.11ba, an 802.11mc and az survey, and an 802.11bd and NR V2X comparison). Pages for 802.11bd and 802.11az on Wikipedia could not be fetched. Anything not in those is **from memory** of the amendments and is marked that way. I did not read the standard itself.
3. **A source disagrees with my arithmetic, and I do not settle it.** The 802.11ah page gives a maximum of "433.3 Mbit/s" (four streams, 16 MHz) and a table ending at 325.0 Mbit/s (three streams). My formula gives **346.67 Mbit/s** for four streams at MCS 9 with the short guard interval, which is the figure I remember from the standard. I use 346.67 and flag the gap.
4. **802.11ay sources disagree on streams.** The page says "MIMO up to 8 streams" in its introduction and four in its body. I give 4 channels bonded (8.64 GHz) and leave the stream count open.
5. **The wake-up radio power figure is my assumption.** The 802.11ba paper I read says "consumes thousands of times less than the Primary Communication Radio" and gives **no** power target; the "1 mW" it mentions belongs to earlier low-power radios. My battery model uses 0.5 mW for WUR listening as a stand-in, and the conclusion (below) depends on it.
6. **Beam training timings, the sensing channel and the TIM block layout are modelled or from memory.** The 17 us per sector-sweep frame, the S1G TIM block byte layout and the WUR frame sizes are from memory, flagged where used.
7. **Regulation is from memory.** Band edges, power limits and the 5.9 GHz V2X band differ by country and have changed; verify before use.
8. **Several amendments get a paragraph, not a section.** 802.11ak (general link), 802.11ai (FILS, covered in WF 06), the 60 GHz MAC details beyond the access periods, and 802.11bk are summarised, not modelled.

By the end you can:

- **derive the HaLow rate table** from 24, 52, 108, 234 and 468 data subcarriers and a 40 us symbol (**0.30 Mbit/s** at 1 MHz MCS0 to **346.67 Mbit/s** at 16 MHz, four streams), and explain why the range is about **5.2 times** that of 2.4 GHz in the model (1.19 km against 229 m) and why the real constraint is contention, not rate.
- **size a TIM** for sparse traffic (about **38 bytes** for 10 sleeping stations in the S1G encoding against about **212** in the legacy bitmap, from memory of the layout) and say what **RAW** buys (x2.9 in the model for 1,000 saturated stations).
- **compute the 60 GHz rates** (**385** to **4,620 Mbit/s** single carrier, **6,756.75** OFDM) and the **range** at 40 dBm EIRP (**410 m** at MCS 1, **120 m** at MCS 12), and show why a 25 dB body block cuts it to **43 m** and **8 m**.
- **say what beam training costs** (sector sweep **1.1 ms** for 32 sectors per side; **14 percent** of a 31 ms sector crossing for 128).
- **explain OCB mode**: no association, 10 MHz half-clocked OFDM at **3 to 27 Mbit/s**, a Doppler of **1,366 Hz** at 250 km/h that changes the channel **2.7 times** within a 300 byte frame at 3 Mbit/s, and about **111 vehicles** at 50 percent load.
- **read an FTM exchange**, compute the distance from four timestamps, and bound the error: **0.6 m** per 100 us of turnaround at a 40 ppm clock difference, and a multipath error of **1.0 to 2.1 m** at 20 MHz that falls to **0.1 to 0.3 m** at 160 MHz in the simulation.
- **say when a wake-up radio pays**: only for latencies below the break-even interval (**0.40 s** in the model), because beacon listening at 1 s costs less than the WUR receiver itself.
- **explain why Wi-Fi sensing has blind spots** (the breathing frequency doubles when the static phase puts the reflection in phase or antiphase with the direct path: **0 percent** success at phases 0 and pi, **100 percent** elsewhere in the simulation).
- **choose between multicast, GCR and unicast copies** (blind multicast reaches all of 10 receivers only **34 percent** of the time at 10 percent loss; GCR with block ack is **100 percent** at **3,550 us** against **3,103 us** for unicast copies, and cheaper from about 14 receivers by interpolation; at 30 percent loss unicast copies stay cheaper up to 30 receivers) and say when a **TDLS** direct link pays (above half the AP rate).

## Where the numbers come from

Each experiment is a script `sN_*.py` in `prog/`, its output frozen in `v/` and shown verbatim below, run by `run.sh`. The six experiments are `s1_halow` (rates, range, TIM, RAW), `s2_dmg` (60 GHz rates, budget, training), `s3_ocb` (V2X numerology, Doppler, load), `s4_ranging` (FTM frames, clock error, bandwidth against accuracy), `s5_wur_sense` (battery model, CSI sensing) and `s6_gcr_tdls` (groupcast, TDLS). The five problem scripts are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is neither is marked **from memory** or **from the source**. **Assumed** means I chose it to make a model run and it is not a property of any device.

## Data and timing

| What | Size or time |
|---|---|
| HaLow rates, range, TIM and Bianchi (`s1_halow.py`) | closed form plus 1,400 random AID sets, under 2 s |
| 60 GHz (`s2_dmg.py`) | closed form, under 1 s |
| OCB (`s3_ocb.py`) | closed form, under 1 s |
| Ranging (`s4_ranging.py`) | 3 frames through `tshark`, 20 multipath cells of 200 trials, about 10 s |
| WUR and sensing (`s5_wur_sense.py`) | 24 cells of 300 trials plus 900 phase trials, about 20 s |
| Groupcast and TDLS (`s6_gcr_tdls.py`) | 3,000 trials for each of 32 cells, about 10 s |
| Problems (`ans_a` to `ans_e`) | each under 1 s |

## Mapping to the Candela course

The Candela course names none of these amendments. They come from the plan's WF 14 audit row and from the 802.11-2020 and later amendment list.

---


## 1. 802.11ah (HaLow): sub-1 GHz, thousands of sensors

### 1.1 What it is for

HaLow is the S1G PHY and MAC of 802.11ah, aimed at sensors, meters and wearables that need a kilometre of reach and a battery that lasts years, not a gigabit. It runs in licence-exempt bands below 1 GHz: **902 to 928 MHz** in North America (26 channels of 1 MHz) and **863 to 868 MHz** in Europe, with other regions in between (**from the source** for the first two; the others, such as Japan, Korea and China, are from memory and differ by country). The Wikipedia page says the protocol aims at connecting thousands of devices under one access point and covering a radius near one kilometre.

The design decisions follow from three needs, and the rest of this section takes them in order: a **PHY** that reaches far at low rate, a **TIM** that can address thousands of sleeping stations, and a **MAC** that does not collapse when a thousand of them wake at once.

### 1.2 The PHY: 802.11ac clocked down by ten

The S1G PHY is the 802.11ac OFDM design with the clock divided by ten: 20, 40, 80 and 160 MHz become **2, 4, 8 and 16 MHz**, the symbol grows from 4 us to **40 us** (short guard 36 us), and a new **1 MHz** mode uses a 32-point FFT with 24 data subcarriers. The Wikipedia page confirms the 2 MHz case: a 64-point FFT, 31.25 kHz spacing, 52 data and 4 pilot subcarriers, symbol 36 or 40 us. The rate is then the WF 02 formula with different numbers:

rate = data subcarriers x bits per subcarrier x code rate x streams / symbol time

```
--- S1G data rate in Mbit/s = data subcarriers x bits x code rate x streams / symbol time (40 us, 36 us short GI)
MHz  sub  MCS0   MCS2   MCS4   MCS7   MCS9   MCS10
  1   24   0.30   0.90   1.80   3.00   4.00  0.15
  2   52   0.65   1.95   3.90   6.50   8.67  -
  4  108   1.35   4.05   8.10  13.50  18.00  -
  8  234   2.92   8.78  17.55  29.25  39.00  -
 16  468   5.85  17.55  35.10  58.50  78.00  -
top rate 16 MHz, 4 streams, MCS9, short GI: 346.67 Mbit/s; 1 MHz MCS0 long GI: 0.30; 1 MHz MCS10: 0.15
```

Three observations. **First**, the bottom of the table is very low: 1 MHz MCS0 moves a 1500 byte frame in 40 ms and MCS2 in 13.3 ms (problem A), before the preamble. **Second**, MCS10 (BPSK 1/2 with every bit sent twice) halves the rate to **0.15 Mbit/s** and exists only at 1 MHz, to buy another 3 dB. **Third**, MCS9 at 1 MHz is not a valid single-stream combination in the standard (from memory), so the 4.00 in the first row is the formula's value, not a usable rate. The peak, **346.67 Mbit/s** (16 MHz, 4 streams, MCS9, short guard), disagrees with the 433.3 the Wikipedia page prints; I do not know which count it used.

### 1.3 Why the range is long

Two effects add. **Noise falls with bandwidth**: 1 MHz instead of 20 MHz lowers the noise floor by 13.0 dB, and the lowest rate (BPSK 1/2) needs the same SNR. **Path loss falls with frequency**: free space loss at 900 MHz is 8.5 dB lower than at 2.4 GHz and 15.2 dB lower than at 5.2 GHz. In the model (20 dBm, exponent 3, noise figure 6 dB, 8 dB implementation gap, no walls):

```
--- range: where the received power falls to the sensitivity of the lowest rate (modelled: 20 dBm, exponent 3, no walls, noise figure 6 dB)
2.4 GHz, 20 MHz, MCS0 (BPSK 1/2)       sensitivity   -90.8 dBm   range  228.6 m   x1.00 of the 2.4 GHz case
5 GHz, 20 MHz, MCS0                    sensitivity   -90.8 dBm   range  136.5 m   x0.60 of the 2.4 GHz case
900 MHz, 1 MHz, MCS0                   sensitivity  -103.8 dBm   range 1193.2 m   x5.22 of the 2.4 GHz case
900 MHz, 1 MHz, MCS10 (repetition)     sensitivity  -107.2 dBm   range 1549.3 m   x6.78 of the 2.4 GHz case
```

The 21.5 dB total (13.0 + 8.5) is a factor of 10 to the power 21.5/30, which is the **x5.22** in the table, and the repetition mode adds 3.4 dB for **x6.78**. Two cautions: walls attenuate sub-GHz less than 5 GHz but not zero, and **the received signal is only half the story**: a 1 MHz channel at 0.30 Mbit/s still shares the air with a thousand stations.

### 1.4 The TIM for 8,191 stations

Legacy power save (WF 03) announces buffered frames in the **partial virtual bitmap** of the TIM element, a bitmap over AIDs 1 to 2,007 of which only the span from the first to the last set octet is sent. With 10 sleeping stations holding traffic, that span is nearly the whole bitmap. S1G raises the limit to **AID 8,191** and organises it hierarchically: **4 pages x 32 blocks x 8 subblocks x 8 bits = 8,192**, so an AP sends only the blocks and subblocks that have a set bit. Each page can be sent in a different beacon (**page slicing**), and stations that know their page wake only for it.

```
--- TIM: legacy partial virtual bitmap against the S1G page, block and subblock encoding for k stations with buffered frames among AIDs 1 to 2007
  k    legacy bytes (mean of 200)   S1G bytes (mean of 200)
   1         6.5                        6.0
   5       173.3                       21.1
  10       211.7                       37.8
  25       239.1                       78.8
  50       247.4                      124.1
 100       252.3                      177.7
 200       254.7                      240.4
addressable AIDs: legacy 2007, S1G 8191 = 4 pages x 32 blocks x 8 subblocks x 8 bits = 8192
```

The legacy column is the span from the first to the last octet holding a set bit plus 5 bytes of header, averaged over random AIDs. The S1G column assumes **3 bytes of block overhead plus one byte per non-empty subblock** (from memory of the layout; I did not read the clause). The ratio is the point: 10 stations cost about 212 bytes in the legacy form and about 38 in the S1G form, and the legacy form cannot address more than 2,007. For 200 stations the encodings converge, because everything is set anyway.

### 1.5 The MAC additions

- **Restricted Access Window (RAW).** The AP divides stations into groups and assigns each group a **slot** inside a RAW; stations outside the slot may not contend. The source describes its purpose as reducing contention and avoiding simultaneous transmissions from many mutually hidden stations.
- **Target Wake Time (TWT).** The same mechanism as in WF 06 (the Wikipedia page describes it for 802.11ah): a station negotiates with the AP to wake only at agreed times, and it can doze for very long periods. The **maximum idle period** is extended so that a station can stay associated while silent for a very long time (from memory; I recall months to years).
- **Short frames.** A **null data packet (NDP)** carries control information in the PHY header alone, and the acknowledgement can be an NDP. At 1 MHz the preamble is long compared with a 14 byte ACK, so cutting the frame matters (the model below assumes a 640 us preamble, which is **assumed**).
- **Relay.** The page says an AP logically consists of a relay plus a station, bidirectional and "limited to two hops only", which lets a far sensor use a higher MCS to a nearby relay.
- **Sectorisation.** Coverage is split into sectors by antennas or beams, with stations contending only within their sector, which cuts contention and allows spatial sharing among overlapping BSSs.

### 1.6 Why contention, not rate, is the HaLow bottleneck

The Bianchi model of WF 03 applied to S1G timings (slot 52 us, SIFS 160 us, 640 us preamble, a 100 byte payload at 1.2 Mbit/s and a 14 byte ACK at 0.3 Mbit/s, all **assumed**) shows what a thousand stations do:

```
--- contention of N saturated stations (Bianchi fixed point; modelled with assumed S1G timings: slot 52 us, SIFS 160 us, preamble 640 us, 100 byte payload at 1.2 Mbit/s, ACK 14 bytes at 0.3 Mbit/s)
  N   DCF Mbit/s  collision prob   share of 1.2 Mbit/s
   1     0.255       0.000           21.3 %
   2     0.262       0.105           21.8 %
   5     0.254       0.272           21.2 %
  10     0.243       0.384           20.2 %
  20     0.230       0.481           19.2 %
  50     0.211       0.595           17.6 %
 100     0.193       0.678           16.1 %
 300     0.154       0.806           12.9 %
1000     0.085       0.935            7.1 %
RAW: 1000 stations split into 100 groups of 10, one slot each (the slots share the time, so the aggregate is the single-slot rate): 0.243 Mbit/s against 0.085 without RAW, x2.9
```

Even **one** station achieves only 21 percent of the 1.2 Mbit/s, because the preambles and the slow ACK dominate a 100 byte frame. A thousand saturated stations collide on 93.5 percent of attempts and the aggregate falls to **0.085 Mbit/s**. RAW with groups of ten gives **0.243 Mbit/s**, x2.9, at the cost of idling slots for stations that have nothing to send. The model is saturated and ignores the RAW boundary cost; a real deployment of mostly idle sensors is easier on the MAC and harder on the TIM and the wake schedule.

## 2. 802.11ad and ay: 60 GHz

### 2.1 The DMG PHY

The directional multi-gigabit (DMG) PHY of **802.11ad** works in the 60 GHz band over **2.16 GHz** channels (the sources I read give the channel table as about 57.24 to 74.52 GHz for ay; band edges differ by region, from memory). It has three PHYs: a **control PHY** (very low rate, for beacons and training, **from memory** 27.5 Mbit/s), a **single carrier** PHY (up to 4.62 Gbit/s) and an **OFDM** PHY (up to 6.76 Gbit/s), plus a low-power single carrier PHY. The single carrier rate follows from the symbol rate of 1.76 Gsym/s and a 512-symbol block of which 448 carry data:

```
--- single carrier rate = 1.76 Gsym/s x 448/512 (data per 512 symbol block, 64 symbol guard) x bits per symbol x code rate (repetition counts in the rate)
MCS  1  pi/2-BPSK   code rate 0.25     385.0 Mbit/s
MCS  4  pi/2-BPSK   code rate 0.75    1155.0 Mbit/s
MCS  8  pi/2-QPSK   code rate 0.75    2310.0 Mbit/s
MCS 12  pi/2-16QAM  code rate 0.75    4620.0 Mbit/s
OFDM (MCS 13 to 24): 336 data subcarriers, symbol 640 samples at 2.64 GSa/s = 242.42 ns (including a 48.4 ns guard): 64-QAM at 13/16: 6756.75 Mbit/s, QPSK at 1/2: 1386.00 Mbit/s
```

The OFDM peak comes out at **6,756.75 Mbit/s**, the figure I remember from the standard: 336 data subcarriers, 6 bits, code rate 13/16, one symbol of 640 samples at 2.64 GSa/s, **242.42 ns**.

### 2.2 The link budget is the whole story

At 60 GHz the free space loss at 1 m is **68.0 dB** (20 log of 4 pi f over c), 20 dB worse than at 6 GHz for the same distance, and oxygen absorbs about 15 dB per km (**from memory**). Wavelength is 5 mm, so a small array has large gain, and the standard relies on it. The model:

```
--- link budget (modelled: EIRP 40 dBm, receive gain 15 dBi, noise figure 10 dB, free space exponent 2, 1.76 GHz noise bandwidth; oxygen absorption 15 dB per km from memory)
noise -71.5 dBm; free space loss at 1 m 68.0 dB; at 10 m 88.0 dB
MCS  eta (bit/s/Hz)  SNR needed dB   range LOS m   with 25 dB body block m   (a 1.76 GHz channel carries eta x 1.76 Gbit/s before overhead)
  1     0.219            0.14          409.8          43.39
  4     0.656            5.60          275.5          23.93
  8     1.312            9.71          196.7          15.13
 12     2.625           15.13          120.3           8.21
```

With 40 dBm EIRP and 15 dBi of receive gain the line-of-sight reach is **410 m** at MCS 1 and **120 m** at MCS 12. The pair of columns on the right is the reason 60 GHz is not a coverage technology: a human body in the beam costs 20 to 30 dB (from memory), and 25 dB cuts the range to **43 m** and **8 m**. Each 6.02 dB is a factor of two in free space range, so every dB of transmit power is worth about 12 percent of distance and every dB of blockage loses the same. Problem B tabulates this for three EIRP values and three extra losses.

### 2.3 Beam training and the beacon interval

A 60 GHz link works only when both ends point beams at each other, so the MAC has a training phase and a beacon interval built around it (from memory of the structure):

- **BTI** (beacon transmission interval): the PCP or AP sweeps its beacon over all transmit sectors.
- **A-BFT** (association beamforming training): stations answer with their own sweeps in a few slots.
- **ATI** (announcement transmission interval): request and response management exchange.
- **DTI** (data transfer interval): **service periods** (scheduled, contention free) and **CBAPs** (contention based access periods, using EDCA).

The beamforming itself has two stages: **SLS** (sector level sweep: the initiator sends a frame in every sector, the responder in every sector, then feedback and acknowledgement) and **BRP** (beam refinement, with training fields appended to data frames so beams can be refined without a new sweep). The sweep cost is a count of frames:

```
--- beam training: sector level sweep, M sectors each side (one SSW frame about 16 us plus 1 us spacing, from memory), then feedback
M sectors  sweep time ms  sector width deg   time to leave the sector at 90 deg/s ms   retrain overhead if retrained once per sector crossing
     8        0.332         45.00               500.0                                0.07 %
    16        0.604         22.50               250.0                                0.24 %
    32        1.148         11.25               125.0                                0.92 %
    64        2.236          5.62                62.5                                3.58 %
   128        4.412          2.81                31.2                               14.12 %
```

The 17 us per frame is **from memory**. At 32 sectors a side the sweep is **1.1 ms**; at 128 it is **4.4 ms**. The last column assumes a device that turns at 90 degrees per second (my choice) and retrains once for every sector it crosses: at 128 sectors the beam width is 2.8 degrees, the sector lasts 31 ms and training eats **14 percent** of the air. Finer beams give more gain and cost more to keep. This is also why 60 GHz client devices are rarely rotated quickly.

### 2.4 802.11ay

802.11ay (approved March 2021, from the source) is the next generation: it **bonds up to four 2.16 GHz channels** (8.64 GHz, from the source), adds **channel aggregation**, **MIMO** (the source gives 8 and 4 streams in different places) including multi-user, and higher-order constellations. The page quotes 20 to 40 Gbit/s transmission rates, a link rate of 44 Gbit/s per stream, 176 Gbit/s with four streams, and ranges of 300 to 500 m. Those are marketing-scale peaks; my budget above shows why range at the highest MCS is a fraction of that. It is a PHY and training evolution of 802.11ad, not a new access design.

## 3. 802.11p and bd: Outside the Context of a BSS

### 3.1 No association, by design

A car passes a roadside unit in seconds, so there is no time for scan, authentication and association. **802.11p** (merged into the base standard) defines **OCB**, "outside the context of a BSS" (the flag is `dot11OCBActivated`, from memory): a station transmits data frames immediately using the **wildcard BSSID** (all ones), with ToDS and FromDS both zero, with no beacon, no authentication, no association and no MAC-layer security exchange (from memory). Priority comes from EDCA. Safety messages (cooperative awareness messages and basic safety messages) are sent as **broadcast at about 10 per second**, with no acknowledgement and no retransmission.

The cost of joining is what the design avoids. A vehicle crossing a 300 m cell at 100 km/h has 10.8 s in range:

```
--- a vehicle crossing a 300 m cell at 100 km/h has 10.8 s in range; a join through scan, authentication and association (WF 03: 4.1 to 8.6 s scans, plus the exchange) against none in OCB
a 4.1 s scan leaves 6.7 s of the pass and an 8.6 s scan leaves 2.2 s, before authentication, association, DHCP and the first useful frame
```

A scan of 4.1 s (the WF 03 figure with preferred channels) leaves 6.7 s, and 8.6 s leaves 2.2 s, **before** authentication, association, DHCP and the first useful frame.

### 3.2 The PHY: half the clock

OCB uses a **10 MHz** channel: the 20 MHz OFDM design clocked at half the speed, so every duration doubles (symbol **8 us**, guard **1.6 us**, subcarrier spacing **156.25 kHz**), and rates halve to 3 to 27 Mbit/s:

```
--- 10 MHz channel: the 20 MHz OFDM numerology at half the clock (symbol 8.0 us with a 1.6 us guard, 48 data subcarriers, 156.25 kHz spacing)
rates Mbit/s: 3.0  4.5  6.0  9.0  12.0  18.0  24.0  27.0
```

The doubled guard (1.6 us) tolerates a path difference of **480 m**, against 240 m for the 20 MHz guard, which suits outdoor multipath with delays of microseconds.

### 3.3 Doppler is the problem

At 5.9 GHz the Doppler shift is the carrier frequency times the closing speed over c:

```
--- Doppler at 5.9 GHz (the 5.855 to 5.925 GHz band)
closing speed km/h   Doppler Hz   coherence time us (0.423/fd)   share of the 156.25 kHz spacing
      50                 273          1549                     0.17 %
     120                 656           645                     0.42 %
     250                1366           310                     0.87 %
     500                2731           155                     1.75 %
```

At 250 km/h the shift is **1,366 Hz**, 0.87 percent of the subcarrier spacing, which is small enough that subcarriers do not collide with one another. The damage is in time: the channel changes after about **310 us**, while a 300 byte frame at 3 Mbit/s lasts 848 us, **2.74 coherence times**. The receiver estimated the channel on the preamble at the start and the channel has changed by the end of the frame. In 802.11p there is no mid-frame tracking, so long frames at high speed decode badly, and lower rates (longer frames) do worse than higher ones.

```
--- airtime of one 300 byte safety message (preamble 32 us, signal 8 us, symbols 8 us each, service and tail 22 bits)
rate  3 Mbit/s:    848 us   frame at 250 km/h spans 2.74 coherence times
rate  6 Mbit/s:    448 us   frame at 250 km/h spans 1.45 coherence times
rate 12 Mbit/s:    248 us   frame at 250 km/h spans 0.80 coherence times
```

### 3.4 Channel load

Broadcast safety messages never back off in a crowd, so the limit is airtime:

```
--- how many vehicles in radio range fill the channel (everyone sends 10 messages a second; 6 Mbit/s, 300 bytes; ignoring collisions and the guard and backoff)
channel load  10 %:   22 vehicles   (one vehicle uses 0.45 % of the air)
channel load  25 %:   55 vehicles   (one vehicle uses 0.45 % of the air)
channel load  50 %:  111 vehicles   (one vehicle uses 0.45 % of the air)
```

At 6 Mbit/s and 300 bytes one vehicle uses 0.45 percent of the air, so **111 vehicles in carrier sense range** fill half the channel and 224 fill it (problem C tabulates sizes and rates). Carrier sense range is larger than the range at which a message is useful, so congestion control (lower rate, lower power, fewer messages) is part of the V2X standards (from memory).

### 3.5 802.11bd

**802.11bd** (NGV) is the evolution. From the 802.11bd and NR V2X paper I read: coding changes from **BCC to LDPC**; **midambles** inserted between data symbols re-estimate the channel and counter Doppler; **dual carrier modulation** repeats symbols on two widely separated subcarriers for diversity (the paper cites 802.11ax gains of 4, 0.6 and 2 dB at MCS 0, 1 and 2, not 802.11bd measurements); **retransmissions** depend on congestion, giving a combining gain of **3 to 8 dB** against 0.5 to 1.7 dB for 802.11p at 10 percent BLER; and the design targets twice the MAC throughput at relative speeds up to **500 km/h** and twice the range. The paper lists 802.11p at 252 km/h in a table and 200 km/h in the text. 802.11bd can use 10 or 20 MHz channels and, per the paper, a **mmWave** mode based on 802.11ad and ay. It must be **interoperable and coexistent with 802.11p**: it must detect and defer to 802.11p transmissions and give them equal access. The doubled throughput appears "only at large SNR values (> 20 dB)", so it is a short-range benefit. The 5.9 GHz allocation has itself changed in some countries (from memory, check) and a cellular alternative (C-V2X, 5G NR V2X) competes with it.


## 4. 802.11mc and az: fine timing measurement

### 4.1 The exchange

**Fine Timing Measurement (FTM)** measures distance as time of flight. The initiator sends an **FTM request** (a public action frame, action code 32); the responder answers with FTM frames (action code 33) that carry the times its radio sent the previous frame and received its acknowledgement. Four timestamps give a round trip time, because the two clocks only have to be stable over the short interval each one measures:

RTT = (t4 - t1) - (t3 - t2), distance = c x RTT / 2

From the survey I read: a burst starts with the request; with n exchanges there are n - 1 round trips, so n is at least 2; the burst duration can be set from **250 us to 128 ms**, the minimum delta between FTM frames is in **100 us** units, the burst period in **100 ms** units, the **ASAP** flag lets the first FTM frame double as a measurement, and the recommended burst period is **10 ms or less**. Three frames built byte by byte and read by `tshark` 4.2:

```
--- three public action frames (request, measurement, final) built by hand; tshark 4.2 summary line per frame
  1 | 46 | FTM Request, Trigger=1 (Start or continue sending FTM frames)
  2 | 63 | FTM, Dialog Token=1
  3 | 52 | FTM, Dialog Token=0 (Termination)
  frame 2: Category code: Public Action (4)
  frame 2: Public Action: FTM (0x21)
  frame 2: Dialog token: 0x01
  frame 2: Followup Dialog token: 0x00
  frame 2: FTM TOD: 1000000
  frame 2: FTM TOA: 1000900
  frame 2: FTM TOD Error: 256
  frame 2: FTM TOA Error: 256
  frame 2: FTM Params (Subset 1 of 3): 0x0001
  frame 2: FTM Params (Subset 2 of 3): 0x190a0200
  frame 2: .... .... .... .... .... .... 0000 0000 = Min Delta FTM: 0x00
  frame 2: 0001 1... .... .... .... .... .... .... = FTM per burst: 0x03
  frame 2: FTM Params (Subset 3 of 3): 0x000000
```

`tshark` reads the action codes, dialog tokens and the time of departure and arrival, which I set to 1,000,000 and 1,000,900 (units of 0.1 ns, from memory). The parameters element is **not right**: I wrote a hand-set 9-byte body and `tshark` shows a minimum delta FTM of 0, not the 10 I intended, so my layout of that element is wrong and the element is only evidence that the dissector reads it.

### 4.2 The clock is the first error

Each side measures an interval on its own clock, so a frequency difference of ppm parts per million between the two devices turns the **turnaround time** (t3 - t2, the responder's delay between receiving and answering) into a distance error:

```
--- the round trip time: RTT = (t4 - t1) - (t3 - t2); the one way delay is RTT/2 and the distance c times that
true 10.0 m; turnaround and clock error in the two devices -> computed distance error in m
turnaround us   0 ppm both   20 ppm and -20 ppm   40 ppm and -40 ppm
        10        -0.000           0.060                0.120
       100         0.000           0.600                1.200
      1000        -0.000           5.996               11.992
     10000         0.000          59.959              119.917
```

At 40 ppm of difference a turnaround of 100 us costs **0.60 m**, of 1 ms **6.0 m**. This is why the responder answers with a short, fixed turnaround and why a long burst should be avoided without a clock ratio estimate. The numbers are exact arithmetic, c / 2 x turnaround x difference (problem D).

### 4.3 Bandwidth and multipath set the floor

Timing noise alone falls with bandwidth: the standard deviation of a time of arrival estimate is c over (2 pi beta sqrt(2 SNR)), with beta the RMS bandwidth, about B over the square root of 12 for a flat spectrum (the Cramer-Rao bound, which the survey cites in words).

```
--- timing noise: the standard deviation of a time of arrival estimate is c / (2 pi beta sqrt(2 SNR)), beta the RMS bandwidth (about B / sqrt 12 for a flat spectrum); one measurement, no multipath
MHz   SNR 10 dB   SNR 20 dB   SNR 30 dB   (metres)
 20      1.848      0.584      0.185
 40      0.924      0.292      0.092
 80      0.462      0.146      0.046
160      0.231      0.073      0.023
```

Noise is not the limit indoors. **Multipath** is: a reflection arriving within 1/B of the direct path merges with it and moves the leading edge. The simulation puts a reflection of half the amplitude, with random phase, after the direct path and takes the first peak of the channel estimate:

```
--- multipath: line of sight plus one reflection (50 percent of the amplitude), the first peak of the channel estimate taken as the arrival; error of the direct path against reflection delay (simulated, 200 trials with random phase, SNR 25 dB)
delay ns     20 MHz     40 MHz     80 MHz    160 MHz      (RMS error in metres)
      5       1.12       0.72       0.53       0.28
     10       1.45       1.04       0.59       0.12
     20       2.06       1.11       0.24       0.11
     50       1.91       0.58       0.23       0.10
    100       1.01       0.44       0.22       0.09
one nanosecond is 0.300 m; 1 / B is 50.0 ns at 20 MHz and 6.25 ns at 160 MHz
```

At 20 MHz the RMS error is **1.0 to 2.1 m** for every reflection delay in the table, the "1 to 2 m" the survey reports for current FTM accuracy; at 160 MHz it is **0.1 to 0.3 m**. One nanosecond is 0.3 m, so the 50 ns resolution of 20 MHz is 15 m of path. The simulation is one random two-path channel with an assumed peak threshold; a room has many reflections.

### 4.4 802.11az and bk

From the survey: **802.11az** improves accuracy to below 1 m, extends to **160 MHz**, adds **trigger-based and non-trigger-based ranging** from one station to many using the 802.11ax multi-user machinery, adds **passive ranging** (the AP broadcasts, nearby stations time the frames), repeats the HE-LTF to improve accuracy, exchanges FTM data within one TXOP using NDP frames, and adds a **secure LTF** (a randomised training sequence that protects against time-advance attacks) and protected FTM frames. **802.11bk** adds **320 MHz** channels in 6 GHz with accuracy below 0.1 m and two static puncturing modes. Both fit the table: more bandwidth, less error.

## 5. 802.11ba: the wake-up radio

### 5.1 The mechanism

A station's main radio is the expensive part. 802.11ba adds a second receiver, the **wake-up radio (WUR)**, that does almost nothing but listen for one pattern; when it hears its address it wakes the main radio. From the 802.11ba paper I read:

- The WUR part occupies **4 MHz** and uses **on-off keying**, so the receiver can be a simple envelope detector.
- The frame begins with a **legacy preamble** (L-STF, L-LTF, L-SIG) so ordinary stations defer, then a BPSK-Mark, the **WUR-Sync** (a 32-bit sequence that also tells low from high rate) and the **WUR-Data**.
- Data rates are **62.5 kbit/s** (4 us symbols, bit 1 as 1010 and bit 0 as 0101) and **250 kbit/s** (2 us symbols, 10 and 01), Manchester coded.
- The MAC frame has an 8-bit frame control, a **12-bit shortened address**, 12 bits of type-dependent control, an optional body and a **16-bit checksum**. The four frame types are wake-up, WUR beacon, WUR discovery and vendor specific. WUR frames are not acknowledged.
- The AP and station agree on **duty cycles**: the WUR listens only in agreed periods, and "the price for this is the increased delay for downlink transmission". WUR beacons carry a partial timestamp because clocks drift: **0.36 s per hour at 100 ppm**.
- Multi-user WUR sends one frame per 20 MHz subchannel (FDMA).

In the paper's ns-3 run (20 MHz, MCS0, 10 ordinary and 10 sensor stations) the data frame lasted 1,480 us and the WUR frame 920 us, channel time was 50 percent higher than for TWT without a guard interval, and WUR gave the smallest energy only "provided that the clock drift is not too high".

### 5.2 When does it pay? A model with assumed powers

The paper gives no power target for 802.11ba. My model assumes 0.5 mW for a listening WUR, 100 mW for the main receiver, 0.05 mW for a sleeping main radio, 0.2 mJ for waking the main radio to read a beacon, and one 0.6 mJ downlink event per minute, on a 2,430 J coin cell. **All are assumed.**

```
--- 802.11ba wake-up radio against beacon listening on a 2430 J coin cell (modelled; every power below is an assumption, WUR listening 0.5 mW is a stand-in for 'under 1 mW' from memory)
one downlink event a minute (0.6 mJ), main radio asleep at 0.05 mW, a beacon wake costs 0.2 mJ
method                       latency worst  average power mW   battery life days
beacon listening every   0.1 s       0.1 s       2.0131                14
beacon listening every   0.3 s       0.3 s       0.7110                40
beacon listening every   1.0 s       1.0 s       0.2600               108
beacon listening every   5.0 s       5.0 s       0.1000               281
beacon listening every  30.0 s      30.0 s       0.0667               422
wake-up radio (about 1 ms)       0.001 s       0.5617                50
break-even interval: beacon listening matches WUR power at I = 0.2 mJ / (0.5 mW) = 0.40 s; slower than that, listening costs less than the wake-up receiver itself
```

```
--- WUR timing: the wake-up packet is on-off keyed at a few hundred kbit/s (62.5 and 250 kbit/s, from memory); airtime of a 20 byte wake-up frame
   62.5 kbit/s:   2.56 ms
  250.0 kbit/s:   0.64 ms
```

The result is not what the amendment's reputation suggests. A beacon listener waking every **1 s** averages 0.26 mW and lasts **108 days**; the WUR, at 0.56 mW, lasts **50 days** while offering a latency of about a millisecond. The break-even is the beacon wake energy over the WUR power: 0.2 mJ / 0.5 mW = **0.40 s**. If the application can wait more than 0.4 s, listening costs less than the wake-up receiver itself; if it needs a response in tens of milliseconds, listening every 0.1 s costs 2.0 mW and lasts **14 days**, and the WUR wins by a factor of 3.6. Problem E tabulates the break-even for other powers. The assumption that matters is the WUR power: the paper's "thousands of times less than the main radio" would be 0.05 mW against a 100 mW receiver, which is 10 times lower than I assumed and moves the break-even to 4 s. That figure is the one to measure on a real part.

## 6. 802.11bf: WLAN sensing

### 6.1 The idea

Every received frame carries channel state information (CSI): amplitude and phase on every subcarrier. A person moving, even breathing, changes the reflections and so the CSI. **802.11bf** standardises how stations arrange sensing: setting up a measurement, sending sounding frames at an agreed rate, and reporting the result (from memory; I read no source on it), for sub-7 GHz and for 60 GHz (DMG) links.

### 6.2 What a simulation shows

One subcarrier, one reflector 3 m away whose path length changes by 5 mm at 0.25 Hz (a chest), direct path 1, reflection amplitude a, noise 0.02, sampled at fs for 20 s; the estimate is the FFT peak between 0.1 and 0.7 Hz; success means within 0.03 Hz of 0.25:

```
--- CSI sensing (802.11bf style): a reflector 3 m away moves by chest motion of 5 mm at 0.25 Hz, 5.2 GHz (lambda 5.77 cm); amplitude of one subcarrier sampled at rate fs for 20 s; noise sigma 0.02 relative to a direct path of 1
reflection amplitude relative to the direct path; success = the FFT peak within 0.03 Hz of 0.25 Hz in 300 trials
sampling Hz   a=0.02   a=0.05   a=0.10   a=0.30      airtime of one 100 us sounding per sample
      0.5       39 %     35 %     36 %     36 %         0.01 %
      1.0       48 %     67 %     67 %     67 %         0.01 %
      2.0       67 %     81 %     86 %     80 %         0.02 %
      5.0       78 %     80 %     80 %     87 %         0.05 %
     10.0       78 %     84 %     82 %     81 %         0.10 %
    100.0       84 %     86 %     82 %     84 %         1.00 %
phase swing of the chest: 2 x 5 mm path change = 1.09 rad at 5.2 GHz; at 60 GHz (lambda 5 mm) 12.6 rad, the reason 802.11ay sensing resolves small motion
```

Three things stand out. **Sampling**: at 0.5 Hz the success rate is 35 to 39 percent because 0.25 Hz is the Nyquist limit; at 2 Hz it is 67 to 86 percent. **The plateau at 80 to 86 percent** does not improve with more samples or a stronger reflection, which points at something other than noise: the next table shows it.

```
--- why the success rate stops near 80 percent: the amplitude is |1 + a exp(j phi)|, its slope with the path length is zero when the static phase puts the reflection in phase or in antiphase with the direct path, and then the breathing appears at twice the rate (blind spots)
static phase (rad)   success at fs = 10 Hz, a = 0.10, 100 trials each; peak frequency of one trial
      0.00              0 %        0.50 Hz
      0.40            100 %        0.25 Hz
      0.80            100 %        0.25 Hz
      1.20            100 %        0.25 Hz
      1.57            100 %        0.25 Hz
      2.00            100 %        0.25 Hz
      2.40            100 %        0.25 Hz
      2.80             99 %        0.25 Hz
      3.14              0 %        0.50 Hz
```

The amplitude is |1 + a exp(j phi(t))|, which is approximately 1 + a cos(phi), with phi(t) = phi0 + m sin(2 pi f t) and m = 1.09 rad here (the phase swing of a 5 mm motion at 5.2 GHz). Expanding in Bessel functions, the term at the breathing frequency is proportional to **sin(phi0) J1(m)** and the term at twice that frequency to **cos(phi0) J2(m)**. At phi0 = 0 or pi the fundamental vanishes and only the second harmonic remains, so the detector reports 0.50 Hz. These are **blind spots**: 0 percent success at phase 0 and pi, 100 percent at the other phases except 99 percent at 2.8. A real system averages over many subcarriers whose phases differ, which removes most blind spots; one antenna pair and one subcarrier does not. At 60 GHz the wavelength is 5 mm, so the same motion swings the phase by 12.6 rad and the signal resolves millimetre motion, the reason sensing is attractive there.

The airtime column is small: sounding every 10 ms uses 1 percent of the air at 100 us per sounding. The model has one reflector, no other motion and no interference.

## 7. 802.11aa: reliable groupcast

### 7.1 Why multicast is the weak case

A multicast or broadcast frame has no acknowledgement, no rate adaptation (it is sent at a basic rate so everyone hears it) and no retry, so a receiver that misses it never gets it. **GCR (groupcast with retries)** adds two ways to improve it (from memory): **unsolicited retries**, in which the AP sends each frame several times blind, and **GCR block ack**, in which the AP polls the group and resends only what receivers report missing. (**Directed multicast service, DMS**, which converts a group frame into unicast copies, is an 802.11v mechanism and is covered with multicast in WF 15.) The model sends a 1000 byte frame to N receivers, each missing a transmission independently with probability p, with 20 us preamble, 67 us backoff, a unicast ACK of 28 us and a block ack exchange of 60 us per receiver, all **assumed**:

```
--- 1000 byte group frame to N receivers each missing a transmission with probability p (modelled; 20 us preamble, 67 us average backoff, unicast ack 28 us plus SIFS 16 us, block ack exchange 60 us per receiver)
p = 0.1   N   multicast (us, all got it %)   3 blind copies          GCR block ack            N unicast copies
           2       1420   81.4 %                 4261   99.8 %          1860  100.0 %            618  100.0 %
           5       1420   58.8 %                 4261   99.4 %          2542  100.0 %           1543  100.0 %
          10       1420   34.4 %                 4261   99.0 %          3550  100.0 %           3103  100.0 %
          30       1420    4.6 %                 4261   97.4 %          7258  100.0 %           9289  100.0 %
p = 0.3   N   multicast (us, all got it %)   3 blind copies          GCR block ack            N unicast copies
           2       1420   49.0 %                 4261   95.1 %          2718  100.0 %            798  100.0 %
           5       1420   17.4 %                 4261   87.4 %          4110   99.9 %           2001   99.7 %
          10       1420    2.8 %                 4261   76.5 %          5852   99.9 %           3988   99.8 %
          30       1420    0.0 %                 4261   43.2 %         12322   99.3 %          11954   99.3 %
```

At 10 percent loss, plain multicast reaches **all** of 10 receivers only **34.4 percent** of the time and 30 receivers **4.6 percent**; three blind copies reach 99 percent at 10 receivers but cost **4,261 us** for any N. **GCR with block ack is 100 percent** and costs 3,550 us at N = 10 against **3,103 us** for ten unicast copies, and 7,258 us against 9,289 at N = 30, so GCR wins from about N = 14 by interpolation at p = 0.1. At p = 0.3 unicast stays cheaper through N = 30 (11,954 us against 12,322 us for GCR), because each retry round resends the full frame. This is the answer to "why not just use GCR everywhere": it saves air only when the group is large and the loss moderate, and unicast conversion wins for small groups.

## 8. TDLS: a direct link between two clients

### 8.1 The mechanism

Two clients in the same BSS normally send data through the AP, which transmits every bit twice. **Tunnelled Direct Link Setup (TDLS)** lets them set up a direct link. The setup frames are sent **through the AP as ordinary data frames** (LLC and SNAP with ethertype 89-0d, payload type 2, from memory), so they work even when the peers cannot hear each other yet; the request carries a **link identifier** with the BSSID and both addresses. After a setup request, response and confirm the peers can exchange data directly, optionally on an **off-channel** with a channel switch, and they stay associated to the AP. The older **DLS** of 802.11e is the earlier form (from memory). Built and decoded:

```
--- a TDLS setup request built as the data frame it is (LLC and SNAP, ethertype 89-0d, payload type 2) and read by tshark
  76 | TDLS | TDLS Setup Request, Dialog Token=1
  Payload Type: TDLS (2)
  Category code: TDLS (12)
  Action code: TDLS Setup Request (0)
  Dialog token: 0x01
  Tag: Link Identifier
  Tag Number: Link Identifier (101)
  TDLS initiator STA Address: 02:00:00:00:00:02 (02:00:00:00:00:02)
  TDLS responder STA Address: 02:00:00:00:00:03 (02:00:00:00:00:03)
```

The first attempt left out the category byte (12) and `tshark` read the action code wrongly; the version shown parses cleanly. The capability and supported rates fields are from memory and unchecked.

### 8.2 When it pays

```
--- TDLS: two clients at 120 Mbit/s to the AP either relay through it (two transmissions of every bit) or talk directly
direct rate Mbit/s   relayed 120 + 120: 60.0 Mbit/s   direct goodput   gain
        30                            60.0                30.0     x0.50
        60                            60.0                60.0     x1.00
       120                            60.0               120.0     x2.00
       300                            60.0               300.0     x5.00
       600                            60.0               600.0     x10.00
a direct link must beat HALF of the AP rate to pay (here 60 Mbit/s); channel switching to an off-channel direct link costs the AP traffic the time away (50 ms away every 500 ms is 10 %)
```

Relaying through the AP halves the rate: 1/(1/120 + 1/120) = **60 Mbit/s**. A direct link must therefore exceed **half the AP rate** to pay, x2 at 120 and x5 at 300 Mbit/s, and loses (x0.5) at 30. Off-channel operation costs the AP's traffic the time the clients are away: 50 ms every 500 ms is 10 percent.

## 9. Briefly: the rest of the list

- **802.11ak (general link)** extends the bridged-network model so that a non-AP station can act as a bridge to a wired network behind it (from memory, unread).
- **802.11ai (FILS)** speeds up the initial link setup to tens of milliseconds and is covered in [WF 06](06_wifi6_6e_7_features.md).
- **802.11bk** (see section 4) is the 320 MHz ranging extension.
- **Wi-Fi Aware (NAN)** and **Wi-Fi Direct** are in [WF 07](07_wifi_architectures_mesh_and_services.md); multicast, MAC randomisation and onboarding are in WF 15.

## 10. Testing and debugging these radios

- **HaLow**: count the TIM bytes at your station count, check the page and block of each AID, and measure how long a sensor takes to join when 1,000 wake together; RAW slot assignments should match the sleep schedule.
- **60 GHz**: measure throughput against angle and blockage, and the time to retrain after rotation. A drop to zero when a hand moves in front is normal, not a fault.
- **V2X**: emulate Doppler with a channel emulator at 250 to 500 km/h, load the channel with 100 or more stations, and test 802.11p and bd interoperation in both directions.
- **FTM**: calibrate the antenna and processing delay per device model, and measure at known distances in an open space and in a room; a systematic offset of a metre is the calibration, not noise.
- **WUR**: measure the real WUR current and the sleep current of the main radio before believing any battery life, and measure the wake latency end to end.
- **Sensing**: record a baseline of the empty room, test at several positions because of blind spots, and combine subcarriers or antennas.
- **GCR**: measure the per-receiver loss before choosing; unsolicited retries are blind and cost the same air for any group size.
- **TDLS**: confirm that the direct rate beats half the AP rate in the position the clients will actually be in.

## 11. Common mistakes

1. **Quoting HaLow's range without its rate.** 1.19 km in the model is at 0.30 Mbit/s; at 4 Mbit/s (MCS9 at 1 MHz, if it were valid) the sensitivity is much worse.
2. **Thinking a thousand stations works because the TIM can address them.** Addressing is not access: contention drops the aggregate to 0.085 Mbit/s without RAW.
3. **Reading 60 GHz range as free space.** Blockage costs 20 to 30 dB (from memory); 25 dB in the model reduces 120 m to 8 m.
4. **Counting only the data rate.** Beam training at 128 sectors takes 14 percent of the air in the model.
5. **Treating OCB as a Wi-Fi network.** There is no association, no ACK for broadcast and no security exchange at the MAC.
6. **Ignoring Doppler inside a frame.** A 300 byte frame at 3 Mbit/s spans 2.74 coherence times at 250 km/h; the lowest rate is the worst.
7. **Trusting FTM to the nanosecond.** 100 us of turnaround at 40 ppm is 0.60 m before multipath.
8. **Using 20 MHz for indoor ranging and expecting 1 m.** The model gives 1.0 to 2.1 m RMS.
9. **Assuming a wake-up radio always saves power.** Below break-even latency yes; above 0.40 s in the model, no.
10. **Detecting breathing from one subcarrier.** Blind spots at two phases give a doubled frequency.
11. **Using GCR for a small group.** Unicast copies are cheaper below about 14 receivers.
12. **Setting up TDLS without checking rates.** A direct link at half the AP rate gains nothing.

## 12. Interview questions

1. **Why does HaLow reach farther than 2.4 GHz?** 13.0 dB less noise bandwidth and 8.5 dB less path loss in the model, 21.5 dB total, a factor of 5.2 at exponent 3.
2. **How does S1G address 8,191 stations?** Four pages of 32 blocks of 8 subblocks of 8 bits, only non-empty parts sent, page slicing across beacons.
3. **What does RAW do and what does it not do?** It limits contention to a slot per group; it does not change the rate or the wake schedule.
4. **Why is 60 GHz a line-of-sight technology?** Free space loss of 68 dB at 1 m, body blockage of 20 to 30 dB, and beams that need training.
5. **Explain sector level sweep.** Each side sends a frame in every sector, feedback picks the best, and beam refinement then tunes it; cost grows with the number of sectors.
6. **What changes in OCB mode?** No BSS, wildcard BSSID, no association, 10 MHz numerology, broadcast without ACK.
7. **Why does Doppler hurt 802.11p and how does 802.11bd fix it?** The channel changes within a frame; midambles re-estimate it, with LDPC and retransmission.
8. **How does FTM compute distance and what limits it?** (t4 - t1) - (t3 - t2); clock difference times turnaround, bandwidth and multipath.
9. **When does a wake-up radio save power?** When the latency needed is shorter than the beacon wake energy over the WUR power.
10. **Why might Wi-Fi sensing miss a breathing person?** A blind spot at a static phase of 0 or pi doubles the frequency; combine subcarriers.
11. **GCR or unicast conversion?** Unicast for small groups, GCR with block ack for large groups at moderate loss.
12. **When is TDLS worth it?** When the direct rate is above half the AP rate.


## 13. Problems

Work each one before reading section 14.

**Problem A. S1G rates.** Compute the data rate of 2 MHz MCS7 with 2 streams and the short guard; 4 MHz MCS4 with 1 stream and the long guard; 8 MHz MCS9 with 2 streams, short guard; 16 MHz MCS9 with 4 streams, short guard; 1 MHz MCS2 and 2 MHz MCS0, one stream, long guard. Compare each with the 65 Mbit/s of one stream of 802.11n at 20 MHz MCS7, and give the time to send a 1500 byte frame at 1 MHz MCS0 and MCS2 before the preamble.

**Problem B. 60 GHz range.** For EIRP of 30, 40 and 43 dBm, extra loss of 0, 10 and 25 dB, receive gain 15 dBi, noise figure 10 dB, an 8 dB gap and oxygen at 15 dB per km, find the line-of-sight range at MCS 1, 4, 8 and 12 (spectral efficiencies 0.219, 0.656, 1.312 and 2.625 bit/s/Hz).

**Problem C. V2X channel load.** How many vehicles in carrier sense range load a 10 MHz channel to 50 percent, each sending 10 messages a second of 200, 300, 500 and 800 bytes at 3, 6 and 12 Mbit/s?

**Problem D. FTM clock error.** The distance error from a clock difference of 1, 10, 40 and 80 ppm and a turnaround of 16 us, 100 us, 500 us, 2 ms and 10 ms, and the longest turnaround that keeps the error under 0.5 m at 10, 40 and 80 ppm.

**Problem E. Wake-up radio break-even.** For a beacon wake energy of 0.1, 0.2 and 0.5 mJ and a WUR listening power of 0.1, 0.5, 1 and 2 mW, the break-even listening interval, and the battery life of a 2,430 J cell for beacon listening at 1 s and for the WUR, with one 0.6 mJ downlink event a minute.

## 14. Answers

**Answer A.** Multiply subcarriers (24, 52, 108, 234, 468 for 1, 2, 4, 8, 16 MHz) by bits per subcarrier, code rate and streams, and divide by 36 or 40 us. 2 MHz MCS7 with 2 streams and the short guard is 52 x 6 x 5/6 x 2 / 36 us = 14.44 Mbit/s.

```
--- S1G data rate: subcarriers x bits x code rate x streams / (36 or 40 us)
bandwidth MHz  MCS  streams  short GI   rate Mbit/s   against 802.11n 20 MHz MCS7 (65.0)
       2         7       2        yes        14.44      x0.222
       4         4       1         no         8.10      x0.125
       8         9       2        yes        86.67      x1.333
      16         9       4        yes       346.67      x5.333
       1         2       1         no         0.90      x0.014
       2         0       1         no         0.65      x0.010
a 1 MHz channel moves one 1500 byte frame (12,000 bits) in 40.0 ms at MCS0 and 13.3 ms at MCS2, before the preamble
```

**Answer B.** Range comes from the budget EIRP + receive gain - noise - needed SNR - loss at 1 m - extra loss, solved against 20 log10(d) + 0.015 d (the 0.015 dB per metre is oxygen). Doubling the range costs 6.02 dB, so each 10 dB of extra loss shrinks the range by a factor 3.16 where oxygen is negligible; the table shows that 10 dB of blockage is worth 10 dB of EIRP.

```
--- 60 GHz range in metres (free space exponent 2, oxygen 15 dB per km, receive gain 15 dBi, noise figure 10 dB, 8 dB implementation gap)
EIRP dBm, extra loss dB   MCS 1 (385 Mbit/s)    MCS 4 (1155)    MCS 8 (2310)   MCS 12 (4620)
30 dBm, 0 dB                         189.5           115.0            76.6            43.4
30 dBm, 10 dB                         73.2            41.3            26.4            14.4
30 dBm, 25 dB                         14.4             7.8             4.9             2.6
40 dBm, 0 dB                         409.7           275.6           196.7           120.3
40 dBm, 10 dB                        189.5           115.0            76.6            43.4
40 dBm, 25 dB                         43.4            23.9            15.1             8.2
43 dBm, 0 dB                         497.4           345.2           252.4           158.9
43 dBm, 10 dB                        243.7           152.3           103.3            59.6
43 dBm, 25 dB                         59.6            33.3            21.2            11.5
each 6.02 dB of EIRP or loss changes the free space range by a factor 2; with 10 dB extra loss the range is x0.316 of the loss free one
```

**Answer C.** The airtime per message is 32 us of preamble, 8 us of signal and 8 us per symbol, with 16 service bits and 6 tail bits added to the payload; the vehicle count is 0.5 over 10 times the airtime. At 6 Mbit/s and 300 bytes it is 111.

```
--- vehicles in range that load a 10 MHz channel to 50 percent, 10 messages per second each (no backoff or collisions counted)
bytes   3 Mbit/s    6 Mbit/s   12 Mbit/s    (airtime us at each rate)
  200        85         160         284         584    312    176
  300        58         111         201         848    448    248
  500        36          70         132        1384    712    376
  800        22          44          86        2184   1112    576
the channel is shared by every vehicle in carrier sense range, which is much larger than a data range: at 6 Mbit/s and 300 bytes a vehicle counts as 0.45 % of the air
```

**Answer D.** Error is c/2 x turnaround x difference. The responder's turnaround is the lever: at 16 us even 80 ppm is 0.19 m, at 10 ms it is 120 m.

```
--- distance error in metres = c / 2 x turnaround x (clock difference in ppm) (the time of flight itself adds about the same ppm of 2 x d / c and is negligible)
turnaround   1 ppm      10 ppm     40 ppm     80 ppm
      16 us    0.002     0.024     0.096     0.192
     100 us    0.015     0.150     0.600     1.199
     500 us    0.075     0.749     2.998     5.996
    2000 us    0.300     2.998    11.992    23.983
   10000 us    1.499    14.990    59.958   119.917
longest turnaround for 0.5 m of error: 10 ppm: 334 us, 40 ppm: 83 us, 80 ppm: 42 us
a correction by the measured clock ratio (from two FTM frames in a burst) removes most of it; the residual is the error of the ratio estimate, not the clock offset
```

**Answer E.** Break-even is wake energy over WUR power. The last two columns compare the lives: a WUR beats 1 s beacon listening only when its power is below the wake energy per second. At 0.2 mJ a WUR at 0.1 mW lasts 174 days against 108 for listening, and at 0.5 mW only 50.

```
--- listening every I seconds costs (wake energy) / I; a wake-up receiver costs its listening power all the time (modelled, assumed numbers)
wake energy mJ  WUR power mW   break-even interval s   battery life of a 2430 J cell at I = 1 s (days), WUR (days), one event a minute at 0.6 mJ
       0.1          0.1              1.00                     176                 174
       0.1          0.5              0.20                     176                  50
       0.1          1.0              0.10                     176                  26
       0.1          2.0              0.05                     176                  14
       0.2          0.1              2.00                     108                 174
       0.2          0.5              0.40                     108                  50
       0.2          1.0              0.20                     108                  26
       0.2          2.0              0.10                     108                  14
       0.5          0.1              5.00                      50                 174
       0.5          0.5              1.00                      50                  50
       0.5          1.0              0.50                      50                  26
       0.5          2.0              0.25                      50                  14
a wake-up receiver pays only if the latency needed is below the break-even interval, and the tighter the latency the more it pays
```

## 15. Cheat sheet

| Item | Value or rule |
|---|---|
| HaLow bands | 902 to 928 MHz (North America), 863 to 868 MHz (Europe), source; others from memory |
| S1G numerology | 1, 2, 4, 8, 16 MHz; 24, 52, 108, 234, 468 data subcarriers; 40 us symbol (36 us short GI) |
| S1G rates | 0.15 (MCS10) to 346.67 Mbit/s (16 MHz, 4 streams, MCS9, computed; page says 433.3) |
| S1G range model | 1,193 m at 900 MHz 1 MHz MCS0 against 229 m at 2.4 GHz (x5.22); contention dominates |
| TIM | legacy 2,007 AIDs; S1G 8,191 = 4 pages x 32 blocks x 8 subblocks x 8 bits |
| RAW (model) | 1,000 saturated stations: 0.085 Mbit/s without, 0.243 with groups of 10 |
| 60 GHz rates | SC 385, 1,155, 2,310, 4,620 Mbit/s; OFDM 6,756.75; 802.11ay bonds 4 channels (8.64 GHz) |
| 60 GHz budget | free space 68.0 dB at 1 m; 410 m at MCS 1 and 120 m at MCS 12 at 40 dBm; 25 dB block: 43 m and 8 m |
| Beam training | 32 sectors per side: 1.1 ms; 128: 4.4 ms, 14 percent of the air if retrained per sector crossing |
| OCB | 10 MHz, symbol 8 us, guard 1.6 us, 3 to 27 Mbit/s; no association; 1,366 Hz Doppler at 250 km/h; 111 vehicles at 50 percent load (300 B, 6 Mbit/s) |
| 802.11bd | LDPC, midambles, DCM, retransmissions; target 500 km/h; 10 or 20 MHz; coexists with 802.11p |
| FTM | RTT = (t4 - t1) - (t3 - t2); 0.60 m per 100 us of turnaround at 40 ppm; 1.0 to 2.1 m at 20 MHz and 0.1 to 0.3 m at 160 MHz (simulated); az below 1 m, bk below 0.1 m (source) |
| WUR | 4 MHz OOK, 62.5 and 250 kbit/s, 12-bit address, 16-bit checksum; break-even 0.40 s at 0.2 mJ and 0.5 mW (assumed) |
| Sensing | Nyquist for breathing is above 0.5 Hz; blind spots at phase 0 and pi double the frequency |
| GCR | blind multicast reaches 10 receivers 34 percent at 10 percent loss; GCR-BA 100 percent, cheaper than unicast above about 14 receivers |
| TDLS | setup tunnelled through the AP; direct rate must beat half the AP rate |

Next: [WF 15, multicast, privacy, onboarding and interworking](15_wifi_multicast_privacy_onboarding_and_interworking.md) (not yet written). Related: [WF 02](02_wifi_phy.md), [WF 03](03_wifi_mac.md), [WF 06](06_wifi6_6e_7_features.md), [WF 07](07_wifi_architectures_mesh_and_services.md), [WF 11](11_rf_fundamentals_and_link_budget.md).

## Sources

- Wikipedia, IEEE 802.11ah: https://en.wikipedia.org/wiki/IEEE_802.11ah (fetched; bands, channel widths, FFT numerology, RAW, TWT, relay, sectorisation, the 433.3 Mbit/s figure)
- Wikipedia, IEEE 802.11ay: https://en.wikipedia.org/wiki/IEEE_802.11ay (fetched; channel bonding, rates, range, the conflicting stream counts)
- "IEEE 802.11ba: Extremely Low Power Wi-Fi for Massive Internet of Things": https://arxiv.org/pdf/1909.00594 (fetched; WUR PHY, frame, duty cycle, ns-3 results)
- "Indoor Positioning with Wi-Fi Location: A Survey of IEEE 802.11mc/az/bk Fine Timing Measurement Research": https://arxiv.org/pdf/2509.03901 (first 100,000 of 141,331 characters read)
- "IEEE 802.11bd and 5G NR V2X: Evolution of Radio Access Technologies for V2X Communications": https://arxiv.org/pdf/1903.08391 (fetched)
- Not fetched or not readable: the Wikipedia pages for 802.11bd and 802.11az (page not found), and the search results for 802.11bd patents and mmWave V2X
- Not read at all: the IEEE 802.11-2020 and later standards; every clause-level detail marked from memory (S1G TIM layout, DMG beacon interval structure, sector sweep timing, OCB parameters, 802.11aa, TDLS fields, 802.11bf, 802.11ak) is exactly that

Numbers marked modelled, computed or simulated come from `s1_halow.py` to `s6_gcr_tdls.py` and the problem scripts; powers, timings and loss probabilities marked assumed are my choices, not device properties.
