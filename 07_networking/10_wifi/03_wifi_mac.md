# WF 03: The 802.11 MAC, from the frame to the airtime

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela sessions 3a to 3d (the MAC, frame formats and exchanges, DCF and EDCA, power save), with the management and control frames, QoS admission control, aggregation and block ack, rate adaptation and the HT elements folded in, and the CWAP weights for the MAC sublayer (25 %) and for frame exchanges (30 %) as the checklist · Time: about 12 hours · You need: [WF 01](01_wifi_landscape_and_ap_anatomy.md) (what an access point is), [WF 02](02_wifi_phy.md) (the PPDU and what it costs in microseconds) and [WF 11](11_rf_fundamentals_and_link_budget.md) (decibels and the SNR each MCS needs). The PHY of WF 02 decides how fast bits move once a station has the medium. The MAC decides who has the medium, for how long, what every frame says about itself, and what the station does when a frame is lost. This tutorial builds the MAC from the bottom: the frame control field and 24 frames decoded by `tshark`, the carrier sense and backoff of DCF checked against the Bianchi model, the NAV and hidden nodes, the four access categories of EDCA and the admission control around them, aggregation and block ack, power save from the TIM to target wake time, rate adaptation, scanning and the three states, and the HT elements. Every table is produced by a script that runs here.*

"Why does my network slow down when one phone is at the edge of the room" and "why is the 1.2 Gbit/s link moving 500 Mbit/s" are MAC questions, and the answers are airtime arguments: DCF gives every station the same number of **packets**, not the same time (section 7), and every frame pays a preamble, an inter-frame space, a backoff and an acknowledgement that aggregation (section 5) amortises. A Staff level answer carries the mechanism and the number, and says which number was measured, which was simulated and which was written from memory of the standard.

**Eight honest limits, stated first.**

1. **There is no radio here, so no number below is a measured over the air result.** Every figure is arithmetic or a simulation I wrote. The independent check is `tshark` 4.2.2, which decodes 24 hand built frames (section 1), a TIM element (section 6) and an HT element (section 9) to the fields I built.
2. **The standard was not re-read.** The frame type tables, the EDCA defaults, the retry limits, the maximum aggregate sizes, the class 1, 2 and 3 frame lists and the RFC 8325 map are **from memory** and marked so where they matter. `tshark` confirms the bytes I built, not that my memory of a default is current.
3. **The contention results are simulations of saturated stations** with a slotted model, checked against the Bianchi analytical model for plain DCF (section 2). The EDCA simulation (section 4) has no analytical check; its collision handling is a simplification (the virtual collision rule is simplified) and its numbers are worst cases, not predictions.
4. **The hidden node and fragmentation results are models** in which starts that overlap at the access point collide and byte errors are independent. Real hidden node losses depend on capture and on the geometry.
5. **The energy figures use assumed currents** (doze 0.3 mA, idle listening 50 mA, receive 70 mA). They show the shape of the trade (listen interval, DTIM, TWT), not the battery life of any chip.
6. **The rate adaptation algorithms are my own simplified versions** (a Minstrel-like sampler and an ARF) on a logistic success model of the SNR, so the comparison says how the two ideas behave on that model and not how the Linux or vendor code behaves. The real Linux code is [WF 10](10_linux_wireless_stack_and_openwrt.md).
7. **Scan times use assumed dwell times** (20 ms and 40 ms for an active scan, one beacon interval for a passive one). The standard sets no fixed values, and real drivers differ.
8. **Nothing here ran on the Linux wireless stack.** This VM has no `cfg80211` and no `mac80211`, so the driver side of the MAC (queues, the aggregation session state, the power save implementation) is [WF 10](10_linux_wireless_stack_and_openwrt.md), not run here. Security frames (the 4-way handshake, protected management frames) are [WF 04](04_wifi_security.md).

By the end you can:

- **decode any 802.11 frame header** (built and decoded by `tshark`: 24 frames from beacon to QoS null; the first byte is `(subtype << 4) | (type << 2) | version`, so a beacon is `0x80`, an RTS `0xb4` and a QoS data frame `0x88`; a QoS data frame with CCMP and LLC/SNAP carries **54 bytes** of overhead, **3.5 %** of a 1500 byte packet);
- **compute the airtime of one exchange and the DCF goodput** (computed: slot 9, SIFS 16, DIFS 34 µs; one station at 54 Mbit/s with a 1500 byte packet gets **30.5 Mbit/s, 56.5 %** of the PHY rate; at 6 Mbit/s the same packet gives **5.37**);
- **say what contention costs** (simulated against the Bianchi model, 150,000 events per row: **30.50, 31.02, 29.70, 28.04, 26.25, 25.14 and 23.48** Mbit/s for N = 1, 2, 5, 10, 20, 30 and 50 against the model's **30.50, 31.50, 30.13, 28.30, 26.32, 25.08 and 23.40**; the cell total falls to **43.5 %** of the PHY rate at N = 50);
- **explain the NAV, the hidden node and when RTS/CTS pays** (modelled: with 2 hidden stations RTS/CTS loses, **23.37 against 27.54**; with 5 it wins, **23.44 against 18.28**; with 20, **23.76 against 6.37**; the RTS and CTS cost **88 µs, 21.3 %** of a 414 µs exchange);
- **price protection and fragmentation** (computed: a CTS-to-self at 1 Mbit/s DSSS costs **314 µs** per protected exchange against **60 µs** at 6 Mbit/s OFDM and **44 µs** at 24 Mbit/s; fragmentation costs **87 %** more airtime on a clean link and pays only from a byte error rate of about 1e-3);
- **map DSCP to an access category and predict the contention** (simulated: with one saturated station per class VO takes **66.8 %** of the airtime and VI **28.7 %**, BK **0.0 %**; ten best effort stations and one voice station give voice **95.2 %**; EF (46) lands in VI under the naive map and in VO under RFC 8325);
- **size voice capacity and explain admission control** (computed upper bound for G.711 with 20 ms packets: **22, 36, 52 and 67** calls at 6, 12, 24 and 54 Mbit/s; a call from a phone stuck at 6 Mbit/s costs **3.1 times** the airtime of one at 54, which is why a TSPEC is in medium time);
- **choose between A-MPDU and A-MSDU and size the aggregate** (computed for HE 80 MHz MCS 11: **54.6 Mbit/s, 9.1 %** of the PHY rate with one MPDU, **503.4, 83.8 %** with 64 and **556.1, 92.6 %** with 256; at a packet error rate of 0.05 the A-MPDU delivers **424.6 Mbit/s** and the A-MSDU **91.6**; the block ack window, not the 5.484 ms limit, binds in all four cases printed);
- **follow power save from the TIM to TWT** (computed and decoded: a TIM for AIDs 1500 and 1507 is **6 bytes** and for AIDs 5 and 1500 it is **191**; modelled with assumed currents, a station that wakes every beacon draws **1.46 mA** against 50 mA always awake; legacy PS-Poll returns at most **9.8 frames per second**; U-APSD bounds the downlink delay at **20 ms**);
- **explain the performance anomaly and rate adaptation** (computed: with three stations at 54 and one at 6 Mbit/s the slow one takes **65 %** of the airtime and the cell carries **14.1 Mbit/s against 24.2** under airtime fairness; simulated on a logistic model: a Minstrel-like sampler reaches **93.3 %** of the oracle on a static 15 dB link and **67.3 %** on a fading one where ARF reaches **89.0 %**);
- **walk the association sequence and the three states** (computed: scanning all three bands takes **8.6 s** with passive 6 GHz and **4.1 s** with the 15 preferred scanning channels; the open join is six frames and about **1.6 ms** of airtime; a class 2 frame from state 1 draws deauthentication reason **6**, a class 3 frame from state 2 reason **7**);
- **read the HT elements** (built and decoded by `tshark`: capabilities info `0x016f`, a 26 byte HT Capabilities element, A-MPDU exponent 3 meaning **65,535 bytes**, and the four HT protection modes).

## Where the numbers come from

Each experiment is a script `sN_*.py` in `prog/` with its output frozen in `v/`, run with `run.sh`. `dot11.py` is the module every script imports: it builds frame control fields, management, control and data frames, the beacon body, a radiotap header and a pcap writer, so that `tshark` can decode the result. `s1_frames.py` builds the 24 frames and the type tables, `s2_dcf.py` the inter-frame spaces and the saturated DCF simulation with the Bianchi model, `s3_protect.py` the duration field, the hidden node model, protection and fragmentation, `s4_edca.py` the DSCP map, the EDCA parameters, the four class contention simulation and the voice capacity, `s5_aggregation.py` the byte and goodput tables for A-MPDU and A-MSDU, the loss model, the block ack sizes and the limits, `s6_powersave.py` the TIM, the latency and energy tables, U-APSD and TWT, `s7_rate.py` the performance anomaly and the rate adaptation algorithms, `s8_assoc.py` the scan times, the join, the state machine and the retry model, and `s9_htie.py` the HT capabilities and operation elements and the HT control field. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, simulated, modelled or computed here is marked **from memory**. The simulations use fixed seeds, so each run prints the same figures.

## Data and timing

| What | Size or time |
|---|---|
| Frames and tables, decoded by `tshark` (`s1_frames.sh`) | 24 frames in a pcap, under 1 s |
| DCF with 1 to 50 stations (`s2_dcf.sh`) | 150,000 transmission events per row, about 4 s |
| NAV, hidden nodes, protection, fragmentation (`s3_protect.sh`) | 60,000 transmissions per row, about 3 s |
| EDCA contention and voice capacity (`s4_edca.sh`) | 120,000 opportunities per scenario, about 4 s |
| Aggregation and block ack (`s5_aggregation.sh`) | tables only, under 1 s |
| TIM, power save, U-APSD, TWT (`s6_powersave.sh`) | one TIM decoded by `tshark`, under 1 s |
| Performance anomaly and rate adaptation (`s7_rate.sh`) | 20,000 frames per row, about 2 s |
| Scanning, join, states, retries (`s8_assoc.sh`) | 100,000 frames per retry row, under 1 s |
| HT elements (`s9_htie.sh`) | one element decoded by `tshark`, under 1 s |

## Mapping to the Candela course

Candela sessions 3a to 3d are the MAC. They cover the frame formats and the management and control exchanges, the distributed coordination function and the QoS extensions, and power save. This tutorial covers the same ground and adds what the course assumes or leaves to other sessions: the byte level frame decode (section 1), the Bianchi check of DCF and the hidden node and protection costs (sections 2 and 3), the DSCP to access category map and the voice capacity arithmetic (section 4), aggregation, block ack and the loss model (section 5), TWT beside the legacy mechanisms (section 6), rate adaptation and the performance anomaly (section 7), and the scan, state and retry mechanics (section 8). It also covers the CWAP **MAC sublayer** and **frame exchanges** domains (25 % and 30 % of that exam by the weights in the plan) as a checklist: frame types and fields, the exchanges for association, power save, block ack and protection, and the reading of captures in [WF 08](08_wifi_capture_tools_and_debug.md). Security frames are [WF 04](04_wifi_security.md), roaming and the radio measurement frames used for steering are [WF 05](05_wifi_roaming_and_radio_management.md), and the 6 GHz discovery frames and TWT negotiation in detail are [WF 06](06_wifi6_6e_7_features.md).

---

## 1. Frames: what every 802.11 frame says about itself

An 802.11 frame starts with a **frame control** field of 2 bytes. Its first byte is `(subtype << 4) | (type << 2) | version`, so the type and subtype are readable straight from the first hex digits of a capture: **0x80 is a beacon, 0xb4 an RTS, 0x88 a QoS data frame**. The second byte holds eight flags.

--- the frame control field: 2 bits version, 2 bits type, 4 bits subtype, then 8 flag bits
type 0 = management, 1 = control, 2 = data (from memory of the subtype names; the first byte is (subtype << 4) | (type << 2) | version)
subtype  management (byte0)               control (byte0)                         data (byte0)
  0      association request    0x00       reserved                     0x04       data                       0x08
  1      association response   0x10       reserved                     0x14       data + CF-Ack              0x18
  2      reassociation request  0x20       trigger (HE)                 0x24       data + CF-Poll             0x28
  3      reassociation response 0x30       TACK                         0x34       data + CF-Ack + CF-Poll    0x38
  4      probe request          0x40       beamforming report poll      0x44       null (no data)             0x48
  5      probe response         0x50       VHT/HE NDP announcement      0x54       CF-Ack (no data)           0x58
  6      timing advertisement   0x60       control frame extension      0x64       CF-Poll (no data)          0x68
  7      reserved               0x70       control wrapper              0x74       CF-Ack + CF-Poll (no data) 0x78
  8      beacon                 0x80       block ack request            0x84       QoS data                   0x88
  9      ATIM                   0x90       block ack                    0x94       QoS data + CF-Ack          0x98
 10      disassociation         0xa0       PS-Poll                      0xa4       QoS data + CF-Poll         0xa8
 11      authentication         0xb0       RTS                          0xb4       QoS data + CF-Ack + CF-Poll 0xb8
 12      deauthentication       0xc0       CTS                          0xc4       QoS null (no data)         0xc8
 13      action                 0xd0       ACK                          0xd4       reserved                   0xd8
 14      action no ack          0xe0       CF-End                       0xe4       QoS CF-Poll (no data)      0xe8
 15      reserved               0xf0       CF-End + CF-Ack              0xf4       QoS CF-Ack + CF-Poll (no data) 0xf8

There are three types. **Management** frames (type 0) run the network: beacon, probe, authentication, association, reassociation, disassociation, deauthentication and the action frames that carry everything added later (block ack setup, channel switch, radio measurement, TSPEC). **Control** frames (type 1) are the short frames of the access mechanism: RTS, CTS, ACK, PS-Poll, block ack request and block ack, CF-End, and in 802.11ax the trigger frame. **Data** frames (type 2) carry the payload, and the **QoS** variants (subtype 8 and up) add a 2 byte QoS control field with the traffic identifier (TID). The names of the reserved and rarely used subtypes are **from memory**; the byte values of the common ones are confirmed by the decode below.

--- flag byte of the frame control field: ToDS, FromDS, MoreFrag, Retry, PwrMgt, MoreData, Protected, Order (+HTC)
beacon                               bytes 80 00
data, STA to AP, power save on       bytes 08 11
QoS data, AP to STA, protected       bytes 88 42
retransmitted QoS data, STA to AP    bytes 88 49
mesh or WDS four address data        bytes 08 03

The eight flags are worth knowing by position, because they are what a capture filter reads. **ToDS and FromDS** say where the frame is going and decide what the three or four address fields mean (table below). **More Fragments** and **Retry** serve fragmentation and the repeat of a lost frame (section 8). **Power Management** is set by a station in every frame it sends to say "I will doze after this exchange", which is how the access point knows to buffer (section 6). **More Data** says the sender holds more frames for the receiver. **Protected** says the body is encrypted (WF 04). **Order** says the frames are strictly ordered, or, in a QoS frame, that a 4 byte HT/VHT/HE control field follows the QoS control (section 9).

**The address fields.** A data frame has three addresses in an infrastructure network and four in a mesh or a wireless bridge. Which is which depends on ToDS and FromDS:

| ToDS | FromDS | Address 1 | Address 2 | Address 3 | Address 4 | Used for |
|---|---|---|---|---|---|---|
| 0 | 0 | DA | SA | BSSID | none | ad hoc, and management frames |
| 1 | 0 | BSSID | SA | DA | none | station to access point |
| 0 | 1 | DA | BSSID | SA | none | access point to station |
| 1 | 1 | RA | TA | DA | SA | mesh or wireless bridge (WDS) |

Address 1 is always the **receiver** on the air and Address 2 the **transmitter**, which is what the acknowledgement and the NAV use; the original source and the final destination can sit behind them. That is why a station-to-station frame in an infrastructure network is two frames (station to access point, access point to station) with the same payload, and why the access point can see and police it.

**Twenty four frames, decoded by `tshark`.** The script builds each frame byte by byte (no FCS, an 8 byte radiotap header) and `tshark` 4.2.2 reads it back. The sizes are the sizes I built.

--- 24 frames built by hand (no FCS, 8 byte radiotap header) and decoded by tshark 4.2.2
frame                                                    bytes  type   tshark info column
beacon                                                      60 0x0008   Beacon frame, SN=1, FN=0, Flags=........, BI=100, SSID="lab"
probe request (wildcard)                                    36 0x0004   Probe Request, SN=2, FN=0, Flags=........, SSID=Wildcard (Broadcast)
probe response                                              54 0x0005   Probe Response, SN=3, FN=0, Flags=........, BI=100, SSID="lab"
authentication, open, seq 1                                 30 0x000b   Authentication, SN=4, FN=0, Flags=........
authentication, open, seq 2                                 30 0x000b   Authentication, SN=5, FN=0, Flags=........
association request                                         43 0x0000   Association Request, SN=6, FN=0, Flags=........, SSID="lab"
association response                                        40 0x0001   Association Response, SN=7, FN=0, Flags=........
reassociation request                                       49 0x0002   Reassociation Request, SN=8, FN=0, Flags=........, SSID="lab"
disassociation (reason 8)                                   26 0x000a   Disassociate, SN=9, FN=0, Flags=........
deauthentication (reason 3)                                 26 0x000c   Deauthentication, SN=10, FN=0, Flags=........
action: spectrum management, channel switch announcement    31 0x000d   Action, SN=11, FN=0, Flags=........
action: spectrum management, TPC request                    29 0x000d   Action, SN=12, FN=0, Flags=........, Dialog Token=1
action: radio measurement request                           42 0x000d   Action, SN=13, FN=0, Flags=........
action: block ack, ADDBA request                            33 0x000d   Action, SN=14, FN=0, Flags=........, Dialog Token=1
action: QoS, ADDTS request (TSPEC)                          84 0x000d   Action, SN=15, FN=0, Flags=........, Dialog Token=1, SSID=Wildcard (Broadcast)
RTS                                                         16 0x001b   Request-to-send, Flags=........
CTS                                                         10 0x001c   Clear-to-send, Flags=........
ACK                                                         10 0x001d   Acknowledgement, Flags=........
PS-Poll (association ID 5)                                  16 0x001a   Power-Save poll, Flags=........
block ack request                                           20 0x0018   802.11 Block Ack Req, Flags=........
block ack (64 bit bitmap)                                   28 0x0019   802.11 Block Ack, Flags=........
CF-End                                                      16 0x001e   CF-End (Control-frame), Flags=........
QoS data (UP 6), STA to AP                                 126 0x0028   QoS Data, SN=16, FN=0, Flags=.p.....T
QoS null, STA to AP, power save on                          26 0x002c   QoS Null function (No data), SN=17, FN=0, Flags=...P...T

Read three things from the table. **The type column is `(type << 4)` plus the subtype**, the number `tshark` calls `wlan.fc.type_subtype` (0x0008 for the beacon, 0x001b for the RTS, 0x0028 for QoS data). **The action frames all look alike** at the type level (0x000d) and are told apart by a **category** and an **action** byte after the header: spectrum management (channel switch announcement, TPC request), radio measurement (the neighbour and beacon reports of [WF 05](05_wifi_roaming_and_radio_management.md)), block ack (ADDBA request) and QoS (the ADDTS request that carries a TSPEC, section 4). **The control frames are tiny**: an ACK or CTS is 10 bytes without the FCS and 14 with it, an RTS 20, a block ack 32 with a 64 bit bitmap.

--- sizes: the header and trailer bytes that every data frame carries (computed)
MAC header, 3 addresses (infrastructure)      24 bytes
+ QoS control                                  2 bytes
+ HT/VHT/HE control (when present)             4 bytes
+ fourth address (mesh, WDS)                   6 bytes
CCMP header                                    8 bytes
CCMP MIC                                       8 bytes
GCMP-256 MIC                                  16 bytes
FCS                                            4 bytes
LLC/SNAP                                       8 bytes
QoS data frame with CCMP and LLC/SNAP: 54 bytes of overhead around the IP packet, 3.5 % of a 1500 byte packet
ACK is 14 bytes (2 FC, 2 duration, 6 RA, 4 FCS), RTS 20, CTS 14, PS-Poll 20, block ack 32 with a 64 bit bitmap

So a QoS data frame with CCMP and LLC/SNAP spends **54 bytes** around the packet (a 26 byte header with QoS control, 16 bytes of CCMP header and MIC, 8 of LLC/SNAP and 4 of FCS), and a plain ACK is 14 bytes. These are the numbers aggregation (section 5) works to amortise. A few frame facts an interview asks for:

- **Beacon**: sent every beacon interval (**100 TU = 102.4 ms**, 1 TU = 1,024 µs) with the timestamp, the capabilities, the SSID, the supported rates, the TIM (section 6) and the capability elements (HT, VHT, HE and the rest). The beacon is the only periodic management frame and the access point's clock reference.
- **Probe request and response**: a station sends a request on a channel (wildcard or named SSID) and any access point that matches answers; a response carries what a beacon carries. This is the **active scan** (section 8).
- **Authentication** in open system is two frames and carries no secret; the real authentication is in the 4-way handshake ([WF 04](04_wifi_security.md)). The word survives from WEP.
- **Association and reassociation request**: a station tells the access point its rates and capabilities, and the response gives the **association ID (AID)** that indexes the TIM. Reassociation names the previous access point (roaming, WF 05).
- **Disassociation and deauthentication** end an association or an authentication, with a 2 byte **reason code**.
- **Action** frames carry a category, an action and a body. The **channel switch announcement** tells every station in the BSS to move to a new channel at a stated beacon count, the **TPC request and report** exchange link margin and transmit power, the **radio measurement request and report** let an access point ask a station what it hears, and **ADDTS** asks for a scheduled QoS stream with a TSPEC.
- **PS-Poll** from a sleeping station asks for one buffered frame; its duration field carries the AID with the top two bits set (section 3).

## 2. DCF: carrier sense, backoff and what contention costs

Wi-Fi is a shared half duplex medium and the **distributed coordination function (DCF)** is the rule that lets stations share it without a coordinator: **CSMA/CA**, carrier sense multiple access with collision avoidance. A station with a frame listens; if the medium has been idle for a **DIFS** it sends, otherwise it waits for the end of the frame in progress, then for a DIFS, then draws a **random backoff** (a number of idle slots) and counts it down only while the medium is idle. The receiver answers a unicast frame with an ACK after a **SIFS**, shorter than any other wait, so the ACK cannot be pre-empted.

--- inter-frame spaces (us): the wait before a transmission is SIFS plus a number of slots
PHY        slot   SIFS   PIFS = SIFS + 1 slot   DIFS = SIFS + 2 slots   EIFS
11a/g/n/ac/ax (OFDM)      9     16           25                     34                    94
11b (DSSS)               20     10           30                     50                   364
EIFS = SIFS + the time of an ACK at the lowest rate + DIFS: the wait after a frame that could not be decoded; AIFS[n] = SIFS + n slots (EDCA, section 5)

The timings follow from one rule, `wait = SIFS + n slots`: **PIFS = SIFS + 1 slot, DIFS = SIFS + 2 slots**, and EDCA's **AIFS[n] = SIFS + n slots** (section 4). For OFDM PHYs the slot is **9 µs**, SIFS **16** and DIFS **34**; for the 802.11b PHY the slot is 20 µs, SIFS 10 and DIFS 50. The **EIFS** (**94 µs** for OFDM) is a longer wait after a frame the station could not decode, because it may have missed the ACK that follows. Values are from memory and checked by the arithmetic of the next table.

**What one exchange costs.** One station alone, a 1500 byte packet at 54 Mbit/s, ACK at 24 Mbit/s:

--- one 1500 byte packet at 54 Mbit/s, one station alone, with an ACK at 24 Mbit/s (MPDU 1536 bytes)
data PPDU 248 us, SIFS 16, ACK 28 us, DIFS 34: 326 us per frame, plus a mean backoff of 7.5 slots = 67.5 us
goodput = 12000 bits / 393.5 us = 30.5 Mbit/s, which is 56.5 % of the 54 Mbit/s PHY rate
the PHY rate moves the data part only: at 6 Mbit/s the same packet takes 2233 us and gives 5.37 Mbit/s (89.5 % of 6)

The PHY part of the exchange is **248 µs** for the data, **16** for the SIFS, **28** for the ACK and **34** for the DIFS: **326 µs**, plus a mean backoff of 7.5 slots (**67.5 µs**, the window 0 to 15 has mean 7.5), **393.5 µs** in all, so the goodput is **12,000 bits / 393.5 µs = 30.5 Mbit/s, 56.5 %** of the PHY rate. A faster PHY makes this worse, not better: only the data part shrinks, and the fixed parts (SIFS, ACK, DIFS, backoff, preamble) do not. At 6 Mbit/s the same packet takes 2,233 µs and gives **5.37 Mbit/s, 89.5 %** of its rate. This is the reason aggregation exists (section 5).

**What N stations cost.** Every station keeps a **contention window** (CW) that starts at CWmin (15 for OFDM), doubles after each failed attempt up to CWmax (1,023) and resets after a success:

--- the contention window after k failed attempts, and the mean backoff
after 0 collisions: CW =   15, backoff uniform on 0 to   15 slots, mean    7.5 slots =    67.5 us
after 1 collisions: CW =   31, backoff uniform on 0 to   31 slots, mean   15.5 slots =   139.5 us
after 2 collisions: CW =   63, backoff uniform on 0 to   63 slots, mean   31.5 slots =   283.5 us
after 3 collisions: CW =  127, backoff uniform on 0 to  127 slots, mean   63.5 slots =   571.5 us
after 4 collisions: CW =  255, backoff uniform on 0 to  255 slots, mean  127.5 slots =  1147.5 us
after 5 collisions: CW =  511, backoff uniform on 0 to  511 slots, mean  255.5 slots =  2299.5 us
after 6 collisions: CW = 1023, backoff uniform on 0 to 1023 slots, mean  511.5 slots =  4603.5 us
after 7 collisions: CW = 1023, backoff uniform on 0 to 1023 slots, mean  511.5 slots =  4603.5 us
the retry limit (7 for frames shorter than the RTS threshold, 4 for longer ones, from memory) ends the frame, and the sender then reports a failure

The simulation puts N saturated stations (always a packet to send) on one channel. It is checked against the **Bianchi model**, the standard analytical model of saturated DCF in which each station transmits in a slot with a probability `tau` and collides with a probability `p`, and the two are a fixed point:

--- DCF with N saturated stations (all always have a packet), 1500 bytes at 54 Mbit/s, CWmin 15, CWmax 1023; 150,000 transmission events each
N    simulated Mbit/s   Bianchi model Mbit/s   share of the PHY rate   collisions per attempt   (Bianchi p)   fairness: lowest and highest station share of packets
1        30.50              30.50                56.5 %               0.000               0.000        1.000 to 1.000
2        31.02              31.50                57.4 %               0.111               0.105        0.497 to 0.503
5        29.70              30.13                55.0 %               0.264               0.272        0.198 to 0.204
10       28.04              28.30                51.9 %               0.368               0.384        0.095 to 0.103
20       26.25              26.32                48.6 %               0.460               0.481        0.045 to 0.054
30       25.14              25.08                46.6 %               0.509               0.533        0.029 to 0.038
50       23.48              23.40                43.5 %               0.574               0.595        0.016 to 0.023
(the Bianchi model is the standard analytical model of saturated DCF: a fixed point for the attempt probability tau and the collision probability p)

Four observations. **First**, the simulation tracks the model to about 1.5 % at every row (31.02 against 31.50 at N = 2, 23.48 against 23.40 at N = 50), which checks both. **Second**, two stations carry slightly **more** than one (31.02 against 30.50): with two stations some of the backoff time is shared, so the medium sits idle less, and then collisions take over. **Third**, the loss is gradual, not a cliff: **30.5 to 23.5 Mbit/s** from 1 to 50 stations, as collisions per attempt rise from 0 to **57.4 %**. **Fourth**, the fairness columns show DCF is fair in **packets** to within a few percent (a station gets about `1/N` of the transmissions); section 7 shows why that is not fair in **time**.

The doubling window is a congestion control: after 6 collisions the mean backoff is **4,603.5 µs** against 67.5 µs for a fresh frame. The retry limit (7 for frames shorter than the RTS threshold, 4 for longer ones, **from memory**) ends a frame, and the sender then reports a failure upward.

## 3. Protection: the NAV, hidden nodes, RTS/CTS, legacy and fragmentation

**Virtual carrier sense.** Listening for energy only works if a station can hear the sender. To cover stations that cannot, every frame carries a **duration** field, in microseconds, that says how long the medium stays busy **after this frame ends**. A station that decodes any frame sets its **network allocation vector (NAV)** to that value and treats the medium as busy until the NAV counts down, whatever its antenna hears. The values for a 1500 byte frame at 54 Mbit/s with control frames at 24 Mbit/s:

--- the Duration/ID field and the NAV: every frame says how long the medium stays busy after it ends (microseconds), for a 1500 byte frame at 54 Mbit/s, control frames at 24 Mbit/s
data PPDU 248 us, ACK 28 us, RTS 28 us, CTS 28 us, SIFS 16 us
frame            duration field                                  value
data (unicast)   SIFS + ACK                                        44
RTS              3 SIFS + CTS + data + ACK                        352
CTS              2 SIFS + data + ACK (RTS duration - SIFS - CTS)  308
data after CTS   SIFS + ACK                                        44
a station that hears only the CTS (hidden from the sender) sets its NAV to 308 us, long enough for the data and the ACK it never hears
the field has 15 bits for a duration (up to 32,767 us); with the top two bits set (0xc000 + AID) it is the association ID in a PS-Poll

Read the RTS and the CTS: the RTS announces **352 µs** (3 SIFS, the CTS, the data and the ACK) and the CTS announces **308 µs**, the RTS value less one SIFS and the CTS itself. A station that hears **only the CTS** (it is hidden from the sender) sets 308 µs and defers through the data and ACK that it never hears. The field is 15 bits (up to 32,767 µs); when the top two bits are set it is not a duration but the AID in a PS-Poll.

**The hidden node.** Two stations that cannot hear each other but both reach the access point will transmit over each other, and no carrier sensing helps them. The model: n stations that are all hidden from one another send 1500 byte frames, and starts that overlap at the access point collide.

--- hidden nodes: n stations that cannot hear each other all send 1500 byte frames to one AP; without RTS/CTS they never defer, with it they defer to the CTS
(a model: starts that overlap at the AP collide; 60,000 transmissions per row; the same 54 Mbit/s, 24 Mbit/s control frames)
n    no RTS/CTS Mbit/s   collisions   RTS/CTS Mbit/s   collisions   change
2        27.54             8.1 %         23.37          14.7 %      -15.2 %
3        24.38            16.6 %         23.23          23.3 %       -4.7 %
5        18.28            33.9 %         23.44          31.2 %      +28.2 %
10       10.17            62.0 %         23.72          40.3 %     +133.2 %
20        6.37            78.4 %         23.76          49.0 %     +273.3 %
for stations that hear each other, RTS/CTS only adds the 88 us of RTS, CTS and two SIFS to every frame: 21.3 % of a 414 us exchange
what a collision wastes: a data frame wastes 248 us, an RTS wastes 28 us, so RTS/CTS pays when collisions are frequent or frames are long

This is a crossover, not a rule. **For 2 and 3 hidden stations RTS/CTS loses** (**23.37 against 27.54**, -15.2 %, and 23.23 against 24.38), because it adds **88 µs** (the RTS, the CTS and two SIFS) to every frame, **21.3 %** of a 414 µs exchange. **From 5 stations it wins** (**23.44 against 18.28**, +28.2 %) and the gap grows: **+133 % at 10 and +273 % at 20**, because a collision of data frames wastes **248 µs** each and a collision of RTS frames wastes **28 µs**. RTS/CTS pays when collisions are frequent or frames are long, and not when stations hear one another. Real devices therefore set an **RTS threshold** (a frame size above which RTS is used) and many leave it at the maximum, so RTS/CTS is off by default. Hidden nodes in an enterprise show up as retries on the access point against clients that the access point hears at a good level, and the fix is usually the cell layout, not the threshold ([WF 05](05_wifi_roaming_and_radio_management.md)).

**Protection for older stations.** A station that cannot decode an OFDM PPDU of a newer generation cannot read its duration field either, so it will transmit over it. **Protection** makes the newer station send a short frame the older stations can decode first, a **CTS-to-self** (a CTS addressed to the sender itself, which sets the NAV of everyone who hears it) or an RTS/CTS, at a rate they understand:

--- protection: a modern PPDU preceded by a CTS-to-self so that legacy stations set their NAV (cost per protected exchange)
CTS rate               CTS time us   + SIFS   cost us   share of a 1 ms PPDU   share of a 4 ms PPDU
1 Mbit/s DSSS (11b)        304         10       314             23.9 %                     7.3 %
6 Mbit/s OFDM               44         16        60              5.7 %                     1.5 %
12 Mbit/s OFDM              32         16        48              4.6 %                     1.2 %
24 Mbit/s OFDM              28         16        44              4.2 %                     1.1 %
a 2.4 GHz network that still admits 802.11b clients makes every OFDM transmission pay the first row (ERP protection, from memory of the rule: the AP sets the "use protection" bit in the ERP element)

A 2.4 GHz network that still admits an 802.11b client makes **every** OFDM transmission pay the first row, **314 µs** (a 1 Mbit/s DSSS CTS of 304 µs plus a SIFS), which is **23.9 %** of a 1 ms PPDU, against **60 µs** at 6 Mbit/s OFDM and **44 µs** at 24 Mbit/s. The access point turns it on with the "use protection" bit of the **ERP** element when it hears an 802.11b station (**from memory** of the rule); the HT protection modes of section 9 are the same idea for 802.11n. This is why disabling the 1, 2, 5.5 and 11 Mbit/s rates (the **basic rate set**) is one of the first things a dense 2.4 GHz design does.

**Fragmentation.** A packet can be cut into fragments, each acknowledged, so that a corrupted fragment costs less to repeat. The model corrupts each byte with a probability `e` independently:

--- fragmentation: a 1500 byte packet in 1 or 3 fragments at 54 Mbit/s, each fragment acknowledged, when each byte is corrupted with probability e (independent errors, a model)
e per byte    1 frame: mean time us   3 fragments: mean time us   better
0                     394                      736          1 frame
1e-05                 400                      740          1 frame
0.0001                459                      777          1 frame
0.0003                624                      865          1 frame
0.001                1830                     1259          3 fragments
0.003               39736                     3686          3 fragments
with e = 0 fragmentation costs 87 % more airtime; it pays only on a link whose frame error rate is already high, and aggregation with block ack (section 7) retransmits single MPDUs, which made fragmentation rare

With no errors three fragments cost **87 %** more airtime (736 against 394 µs): each carries its own header, ACK and wait. Fragmentation pays only when the byte error rate is around **1e-3**, a link that is already nearly unusable, and block ack (section 5) retransmits single MPDUs anyway, so fragmentation is rare in practice. It remains an exam topic (the More Fragments flag, the fragment number in the sequence control field, the fragmentation threshold).

---

## 4. EDCA and WMM: priority as a share of the chances to transmit

DCF treats every frame alike. **EDCA** (enhanced distributed channel access, the mechanism of 802.11e, certified as **WMM**) gives each station **four queues**, the **access categories (AC)** voice (VO), video (VI), best effort (BE) and background (BK), and each queue contends **as if it were a separate station** with its own parameters: a shorter wait (**AIFS**), a smaller backoff window (**CWmin and CWmax**) and, for VO and VI, a **TXOP limit** (the time one win may be used for a burst). There is no reservation and no guarantee. High priority means **more chances to transmit**, nothing else.

**From a DSCP to a queue.** The IP header carries a 6 bit DSCP. The access point or the station maps it to a **user priority (UP)** from 0 to 7 (the 802.1D priority), and the UP to an AC by the WMM table: **UP 1 and 2 are BK, UP 0 and 3 are BE, UP 4 and 5 are VI, UP 6 and 7 are VO.** The simple map takes the top three bits of the DSCP; the IETF guidance of RFC 8325 changes several rows.

--- from a DSCP to an access category: the user priority (UP) is the top three bits of the DSCP, UP to AC by the WMM table
DSCP name  value  naive UP (value >> 3)  AC     RFC 8325 UP   AC
CS0          0              0          BE             0     BE
CS1          8              1          BK             1     BK
AF11        10              1          BK             1     BK
CS2         16              2          BK             0     BE
AF21        18              2          BK             3     BE
CS3         24              3          BE             4     VI
AF31        26              3          BE             4     VI
CS4         32              4          VI             4     VI
AF41        34              4          VI             4     VI
CS5         40              5          VI             5     VI
EF          46              5          VI             6     VO
CS6         48              6          VO             7     VO
CS7         56              7          VO             7     VO
so EF (voice, 46) lands in the VIDEO class under the naive map, and the common mistake is to expect it in voice; RFC 8325 moves EF to UP 6 (the RFC table is from memory)
UP 1 and 2 are background, 0 and 3 best effort, 4 and 5 video, 6 and 7 voice: user priority 3 is higher than 0 but shares its class

The consequence is the classic mistake: **EF (46), the DSCP for voice, becomes UP 5 and lands in VIDEO under the naive map**, and RFC 8325 moves it to UP 6 (VO) (the RFC table is **from memory**). CS2 and AF21 are the opposite problem: the naive map puts them in BK, below best effort, which surprises anyone who marks "priority" traffic with them. A WLAN design therefore states where the DSCP is trusted, since a client can mark anything it likes, and how the access point rewrites it ([WF 07](07_wifi_architectures_mesh_and_services.md)).

**The default parameter set** (**from memory** of the standard) is a table of four rows, each of which the access point can override in its beacon:

--- the default EDCA parameter set for a station (from memory of the standard): the window, the wait and the burst limit of each class
AC   CWmin  CWmax  AIFSN  AIFS us (SIFS + AIFSN x slot)  TXOP limit us   mean first backoff slots
BK     15   1023      7             79                          0             7.5
BE     15   1023      3             43                          0             7.5
VI      7     15      2             34                       3008             3.5
VO      3      7      2             34                       1504             1.5
an AP typically advertises a more generous set for BE (AIFSN 1, CWmax 63) and VI, VO (AIFSN 1) in its beacon: the AP carries all downlink traffic for every station

Read the rows as a stack: BK waits **79 µs** (AIFSN 7) and draws from a window of 15 to 1,023; BE waits **43 µs** with the same window; VI and VO wait the shortest time, **34 µs**, with windows of **7 to 15** and **3 to 7**, so a voice station's first backoff averages 1.5 slots (13.5 µs) against 67.5 µs for best effort. The access point usually advertises a more generous set for itself than the one stations use (**from memory**: AIFSN 1 for BE, VI and VO), because it carries all the downlink traffic of every station.

**What the contention does.** The simulation has stations in all four classes, every station saturated (always a frame to send), 54 Mbit/s, 1500 byte packets for BE, BK and VI and 200 byte voice packets, 120,000 opportunities each:

--- four classes in contention, every station saturated, 54 Mbit/s, 1500 byte packets (voice packets are 200 bytes); 120,000 transmission opportunities each
one station of each class:
  AC    stations   Mbit/s   share of airtime
  BK          1     0.00          0.0 %
  BE          1     0.05          0.1 %
  VI          1    11.24         28.7 %
  VO          1     9.31         66.8 %
  collisions per transmission opportunity: 13.0 %
ten best effort stations and one voice station:
  AC    stations   Mbit/s   share of airtime
  BE         10     0.41          1.0 %
  VO          1    13.27         95.2 %
  collisions per transmission opportunity: 7.1 %
ten best effort stations and one background station (AIFSN 7):
  AC    stations   Mbit/s   share of airtime
  BK          1     0.18          0.4 %
  BE         10    27.70         67.4 %
  collisions per transmission opportunity: 21.3 %
five video stations against five best effort stations:
  AC    stations   Mbit/s   share of airtime
  BE          5     0.10          0.2 %
  VI          5    36.74         93.8 %
  collisions per transmission opportunity: 30.4 %
a saturated voice or video station wins almost every contention: priority is a share of the chances to transmit, and real voice is not saturated (50 packets a second), so these rows are the worst case, not a prediction

**Strict priority emerges from the parameters.** With one station per class, **VO takes 66.8 % of the airtime and VI 28.7 %; BE gets 0.1 % and BK 0.0 %.** Ten best effort stations and **one** voice station give voice **13.27 Mbit/s (95.2 %)** and the ten together **0.41 Mbit/s (1.0 %)**. Five video stations leave five best effort stations **0.10 Mbit/s**. Background is close to starvation but not at zero: ten BE stations and one BK station give BE **27.70 Mbit/s (67.4 %)** and BK **0.18 Mbit/s (0.4 %)**. Treat these as **worst cases and not predictions**: real voice is not saturated (50 packets a second, section below), and the point is that priority is a share of the chances to transmit, which is also why marking **all** traffic as voice (a common misconfiguration) destroys the differentiation (reasoning, not simulated): everyone then contends with the voice parameters, so no class is ahead of another, and the small windows of VO raise the collision rate.

**The TXOP limit.** A station that wins contention with a nonzero TXOP limit may send several frame exchanges in a row, a **burst**, separated by SIFS, for up to the limit (VI 3,008 µs, VO 1,504 µs, BE and BK zero, meaning one frame):

--- what the TXOP limit does: a video station with a burst limit of 3.008 ms against the same station sending one frame per access, against 5 best effort stations
  VI burst limit    0 us: VI 19.47 Mbit/s (47.4 % of airtime), best effort total 11.35 Mbit/s
  VI burst limit 3008 us: VI 35.06 Mbit/s (89.5 % of airtime), best effort total 2.27 Mbit/s
  frames per VI burst at 54 Mbit/s: one exchange takes 292 us, so a 3.008 ms burst holds 9 of them

A video station that sends a burst takes **89.5 %** of the airtime against five best effort stations and gets **35.06 Mbit/s** instead of **19.47** (47.4 %) without the burst: nine exchanges fit in 3.008 ms. The burst is how a short wait and a long TXOP together give video its throughput guarantee, and it is also why a few video streams hurt best effort so much.

**Voice capacity, and why a TSPEC is in time.** A G.711 voice call is 50 packets a second each way. With a 200 byte IP packet, each packet needs a whole exchange (preamble, data, SIFS, ACK, a mean wait of AIFS plus 1.5 slots), so the capacity depends on the PHY rate more than on the bits:

--- voice capacity: G.711 with 20 ms packets, one call = 50 packets per second each way; every packet is an IP packet of 200 bytes (RTP, UDP, IP headers included)
this is an upper bound that counts the exchange and the mean backoff and ignores collisions and the beacon
rate Mbit/s   exchange us   + mean backoff (AIFS + 1.5 slots)   packets/s per call   airtime per call   calls that fit in 100 % of airtime   in 70 %
       6            400                     47.5                    100               4.47 %                         22               15
      12            228                     47.5                    100               2.76 %                         36               25
      24            144                     47.5                    100               1.92 %                         52               36
      54            100                     47.5                    100               1.47 %                         67               47
the same call costs 3.1 times as much airtime from a phone stuck at 6 Mbit/s as from one at 54, even though the bits are the same, which is why admission control (TSPEC) works in medium time and not in bits

A cell carries at most **22 calls at 6 Mbit/s, 36 at 12, 52 at 24 and 67 at 54** (upper bounds that ignore collisions and the beacon; **15, 25, 36 and 47** at 70 % utilisation, a common planning figure). The last line is the one to remember: **the same call costs 3.1 times as much airtime from a phone stuck at 6 Mbit/s than from one at 54** (4.47 % against 1.47 %), though the bits are the same. That is why **admission control** works in **medium time**.

**TSPEC and admission control.** A station that wants a protected voice or video stream sends an **ADDTS request** (an action frame, section 1) carrying a **traffic specification (TSPEC)**: the nominal packet size, the mean data rate, the minimum PHY rate, the delay bound and the **surplus bandwidth allowance** (the field list is **from memory**). The access point turns that into a **medium time**, the microseconds per second the stream will use at the stated rate, adds it to the admitted total and answers **accept or reject**; an access category that requires admission control (**ACM**, from the beacon) may be used only after an accepted ADDTS. The airtime arithmetic is the table above, and the reason a design that only counts Mbit/s over-admits slow clients. WMM-AC is rarely deployed beyond voice on managed phones (**from memory**), and many networks use **WMM without admission control** plus a limit on the number of voice clients per radio.

**WMM power save and the other legacy queues** are in section 6; the **four-address header and QoS control** are in section 1.

## 5. Aggregation and block ack: amortising the fixed cost

Section 2 showed the fixed cost of an exchange: with a 54 Mbit/s PHY only **56.5 %** of the PHY rate survives. At HE rates the fixed cost is a much bigger share, because the data part of the exchange shrinks. **Aggregation** sends several packets in one PPDU so that one preamble, one wait and one acknowledgement serve them all.

**Two kinds.** An **A-MSDU** joins packets **above** the MAC: they share one MAC header and one FCS, each with a **14 byte subframe header** (DA, SA, length) padded to a multiple of 4 bytes. An **A-MPDU** joins whole **MPDUs** (MAC frames) **below** the MAC in one PPDU: each keeps its own header and FCS, plus a **4 byte delimiter** and padding. The byte arithmetic:

--- bytes around the payload: one MPDU = MAC header 26 + CCMP 16 + LLC/SNAP 8 + FCS 4 = 54 bytes of overhead
A-MPDU: each MPDU is its own frame with its own header and FCS, plus a 4 byte delimiter and padding to a multiple of 4 bytes
A-MSDU: many packets share one MAC header: each packet gets a 14 byte subframe header (DA 6, SA 6, length 2) and padding to a multiple of 4 bytes, and one FCS covers all

--- share of the bytes on the air that is payload (before the PHY preamble)
packet bytes   N    A-MPDU    A-MSDU
    1500       1     96.2 %    95.5 %
    1500       8     96.2 %    98.5 %
    1500      32     96.2 %    98.8 %
    1500      64     96.2 %    98.9 %
     300       1     83.3 %    81.1 %
     300       8     83.3 %    93.0 %
     300      32     83.3 %    94.4 %
     300      64     83.3 %    94.7 %
     100       1     62.5 %    58.8 %
     100       8     62.5 %    81.5 %
     100      32     62.5 %    85.0 %
     100      64     62.5 %    85.6 %
      64       1     51.6 %    47.8 %
      64       8     51.6 %    73.8 %
      64      32     51.6 %    78.3 %
      64      64     51.6 %    79.2 %

For 1500 byte packets headers do not matter: **96.2 %** payload for A-MPDU and **98.9 %** for A-MSDU at N = 64. For small packets they do: a 100 byte packet is **62.5 %** payload as an A-MPDU and **85.6 %** as an A-MSDU at N = 64. So **A-MSDU saves bytes and A-MPDU saves time.** The time:

--- goodput of one exchange (HE, one stream, GI 0.8 us, preamble 43.2 us, block ack at 24 Mbit/s, DIFS and a mean backoff of 7.5 slots), 1500 byte packets, no losses
setting               N    PPDU us   exchange us   goodput Mbit/s   share of PHY rate   (A-MPDU)
HE  20 MHz MCS  7     1      192.8       342.3           35.1             40.7 %
HE  20 MHz MCS  7     4      628.0       777.5           61.7             71.8 %
HE  20 MHz MCS  7    16     2368.8      2518.3           76.2             88.6 %
HE  20 MHz MCS  7    64     9332.0      9481.5           81.0             94.2 %
HE  20 MHz MCS  7   256    37184.8     37334.3           82.3             95.6 %
HE  80 MHz MCS 11     1       70.4       219.9           54.6              9.1 %
HE  80 MHz MCS 11     4      138.4       287.9          166.7             27.8 %
HE  80 MHz MCS 11    16      383.2       532.7          360.4             60.0 %
HE  80 MHz MCS 11    64     1376.0      1525.5          503.4             83.8 %
HE  80 MHz MCS 11   256     5374.4      5523.9          556.1             92.6 %
HE 160 MHz MCS 11     1       56.8       206.3           58.2              4.8 %
HE 160 MHz MCS 11     4       97.6       247.1          194.3             16.2 %
HE 160 MHz MCS 11    16      220.0       369.5          519.6             43.3 %
HE 160 MHz MCS 11    64      709.6       859.1          894.0             74.4 %
HE 160 MHz MCS 11   256     2708.8      2858.3         1074.8             89.5 %

For **HE 80 MHz MCS 11 one MPDU delivers 54.6 Mbit/s, 9.1 % of the 600.5 Mbit/s PHY rate**: the 12,000 bits of payload need only **20.0 µs** at 600.5 Mbit/s, **9.1 %** of the 219.9 µs exchange, and the rest is preamble (43.2 µs), SIFS, block ack, DIFS and backoff. **Four MPDUs give 166.7 (27.8 %), sixteen 360.4 (60.0 %), sixty four 503.4 (83.8 %) and 256 give 556.1 (92.6 %).** At 160 MHz the same effect is stronger (**4.8 %** with one MPDU, **89.5 %** with 256). At HE 20 MHz MCS 7 one MPDU is already **40.7 %** because the PHY part is long. **The faster the PHY, the larger the aggregate it needs.** That is the headline of this section and of the WF 02 section on airtime efficiency.

**What losses do.** An A-MPDU loses only the damaged MPDUs, because each has its own FCS and the receiver acknowledges them one by one with a **block ack**. An A-MSDU has one FCS for all, so **one bit error loses the whole aggregate**:

--- losses: the same exchange when each 1500 byte MPDU is lost with probability PER; an A-MPDU loses only the damaged MPDUs (selective retransmission by block ack), an A-MSDU loses the whole aggregate on one bit error
(HE 80 MHz MCS 11, 32 packets of 1500 bytes per exchange; the PER of one 1500 byte packet, independent byte errors; goodput counts only packets delivered)
PER per packet   A-MPDU goodput Mbit/s   A-MSDU goodput Mbit/s   A-MSDU loss probability of the whole aggregate
0                         447.0                  454.2                     0.0000
0.001                     446.5                  440.2                     0.0307
0.01                      442.5                  331.9                     0.2692
0.05                      424.6                   91.6                     0.7983
0.1                       402.3                   16.9                     0.9627
0.3                       312.9                    0.0                     1.0000
A-MSDU is cheaper in headers (above) but all or nothing; it pays for small packets on a clean link and is dangerous on a lossy one, which is why most implementations aggregate with A-MPDU and use A-MSDU only on top of it for tiny packets

On a clean link A-MSDU is slightly ahead (**454.2 against 447.0 Mbit/s**). At a packet error rate of **0.01** it is behind (**331.9 against 442.5**), at **0.05** it collapses (**91.6 against 424.6**) and at **0.3** it delivers **0**, while the A-MPDU still moves **312.9**. A-MSDU therefore suits tiny packets on clean links, and it is used **inside** A-MPDU for them. The cross-over for small packets, from the script `ans_d`, is a loss probability of well under 1 % (problem D).

**Block ack.** A transmitter sets up a **block ack agreement** per TID with an **ADDBA request and response** (action frames, section 1) that fixes the window size, and from then on sends aggregates and a **block ack request** (or sets an implicit request in the A-MPDU) and receives a **block ack**: a starting sequence number plus a bitmap saying which MPDUs of the window arrived.

--- block ack: the window, the bitmap and the cost of acknowledging (sizes from memory of the standard)
generation   window (MPDUs)   bitmap bytes   block ack frame bytes   time at 24 Mbit/s us   (24 Mbit/s: 96 bits per 4 us symbol)
HT                     64              8                    32                     32
VHT                    64              8                    32                     32
HE                    256             32                    56                     40
EHT                  1024            128                   152                     72
sequence numbers have 12 bits (0 to 4095); a window of 64 keeps the receiver able to tell new from old, and a lost MPDU at the head holds the window (and in-order delivery) until it is retried or given up

The bitmap and the window grow with the generation: **64 MPDUs and 8 bytes (HT, VHT), 256 and 32 bytes (HE), 1,024 and 128 bytes (EHT)**, so the block ack frame is **32, 32, 56 and 152 bytes** and takes **32, 32, 40 and 72 µs** at 24 Mbit/s. A sequence number has 12 bits (0 to 4,095), and a window must stay under half the space so a receiver can tell new from old. A lost MPDU at the head of the window holds delivery in order until it is retried or given up.

**What limits the aggregate.**

--- what limits an aggregate: the PPDU time limit, the window and the maximum length (maxima from memory)
generation   max A-MPDU bytes   max A-MSDU bytes   window MPDUs   limit on PPDU time
HT                    65,535     3,839 or 7,935             64     10 ms (5.484 ms for legacy protection)
VHT                1,048,575 3,895 / 7,991 / 11,454             64     5.484 ms
HE                 6,500,631             11,454            256     5.484 ms
EHT               15,523,200             11,454           1024     5.484 ms

PPDU time against the window: how many 1500 byte MPDUs fit in 5.484 ms, and how many the window allows
setting                      MPDUs in 5.484 ms   window   binding limit   share of PHY rate at the binding limit
HE 80 MHz MCS 11 x1                 261          256   window                 92.6 %
HE 160 MHz MCS 11 x2               1044          256   window                 83.2 %
VHT 160 MHz MCS 9 x4               1358           64   window                 53.3 %
EHT 320 MHz MCS 13 x2              2500         1024   window                 87.4 %
(the preamble of extra streams is approximated; the point is that at the top rates the window, not the time limit, caps the aggregate for VHT)

Three limits apply at once: the maximum A-MPDU length (**65,535 bytes for HT, 1,048,575 for VHT, 6,500,631 for HE, 15,523,200 for EHT**, from memory), the **window** (64, 64, 256, 1,024 MPDUs) and the **PPDU time**. The last four rows show that **the window, not the 5.484 ms time limit, binds in all four cases printed**: 261 MPDUs fit in 5.484 ms at HE 80 MHz MCS 11 against a window of 256 (which gives **92.6 %**), and VHT 160 MHz MCS 9 with four streams can only reach **53.3 %** of its PHY rate with a 64 MPDU window. The 1,044 and 2,500 MPDU rows use an approximated preamble, as the script says. The peers negotiate the limits in the capability elements (the A-MPDU parameters byte of section 9), and an access point that is configured for a lower aggregate limit to protect voice latency lowers its own ceiling.

## 6. Power save: the TIM, the DTIM, U-APSD and TWT

A station that listens all the time (called **CAM** for constantly awake mode) spends an average of **50 mA** in the model; a station that sleeps between beacons spends a few percent of that. Power save is a **buffering** agreement: the station tells the access point "I am dozing" with the **Power Management bit** (section 1), and the access point holds frames for it and announces them in the beacon.

**The TIM.** Every beacon carries a **traffic indication map**: one bit per AID, "the access point holds frames for you". The element has a bitmap control byte (bit 0 is the multicast-buffered flag, bits 1 to 7 the offset N1/2) and a **partial virtual bitmap** that holds only the bytes from N1 to N2, so a cell with a few active clients costs 4 to 6 bytes per beacon:

--- the traffic indication map (TIM) in every beacon: one bit per association ID says "the AP holds frames for you"
bitmap control: bit 0 = multicast frames buffered (valid in a DTIM beacon), bits 1 to 7 = offset N1 / 2; the partial virtual bitmap holds only bytes N1 to N2
case                 element bytes (hex)                       N1   partial bitmap bytes   element length
nobody has data      00030000                                      0              0                    4
AID 2 and 7          00030084                                      0              1                    4
AID 1                00030002                                      0              1                    4
AID 20               00030210                                      2              1                    4
AIDs 300 and 301     0003240030                                   36              2                    5
AIDs 5 and 1500      0003002000000000000000000000000000000...      0            188                  191
AIDs 1500 and 1507   0003ba001008                                186              3                    6
tshark decodes the same elements (DTIM count, DTIM period, bitmap offset N1 / 2 as 0x12 = 18 for N1 = 36, multicast bit): 0,3,0x00,False; 0,3,0x00,False; 0,3,0x00,False; 0,3,0x01,False; 0,3,0x12,False; 0,3,0x00,False; 0,3,0x5d,False
the worst case is a 251 byte bitmap (2,007 AIDs); the offset trick makes a cell with a few active clients cost 4 to 6 bytes per beacon

Read the rows. With nobody holding frames the element is **4 bytes**; AIDs 2 and 7 fit in **one bitmap byte** (`0x84`); AIDs **300 and 301** need the offset (N1 = 36) and **two bytes**, 5 in all; AIDs **1500 and 1507** cost only **3 bitmap bytes and 6 in all** because the offset skips 186 bytes. The costly case is a **spread**: AIDs 5 and 1500 need **188 bytes of bitmap**, 191 in all, because the bitmap must span both. `tshark` decodes the same elements (the DTIM count and period and the offset), which checks the layout. The worst case is a 251 byte bitmap, 2,007 AIDs.

**DTIM.** A **delivery TIM** beacon, one in every DTIM period, announces that **multicast and broadcast** frames follow right after it. Multicast has no ACK and no individual station, so the access point holds it until a DTIM beacon and every sleeping station that wants multicast wakes for that beacon. A station also has a **listen interval**, the number of beacon intervals it may sleep through, which it tells the access point at association so that the access point keeps its frames at least that long. The latency table shows the price:

--- latency: a frame arrives for a sleeping station at a random moment; unicast waits for the next beacon the station listens to, multicast for the next DTIM beacon
beacon interval = 100 TU = 102.4 ms (1 TU = 1,024 us)
DTIM period   listen interval   mean unicast wait ms   worst unicast wait ms   mean multicast wait ms   worst multicast wait ms
       1                1                 51.2                 102.4                   51.2                    102.4
       1                3                153.6                 307.2                   51.2                    102.4
       3                3                153.6                 307.2                  153.6                    307.2
       3               10                512.0                1024.0                  153.6                    307.2
      10               10                512.0                1024.0                  512.0                   1024.0
multicast and broadcast (ARP, mDNS, router advertisements) are held until a DTIM beacon, so a long DTIM period delays them and, since every sleeping station must wake for them, a chatty network wakes everyone

With a 100 TU beacon (102.4 ms), a station with listen interval 1 sees a mean unicast wait of **51.2 ms** and a worst case of **102.4**; with listen interval 3 the worst case is **307.2 ms**, and with **10 it is 1,024 ms**. Multicast waits for the DTIM, so DTIM period 3 with listen interval 3 gives a mean multicast wait of **153.6 ms** and, with a period of 10, **512 ms**. Multicast and broadcast (ARP, mDNS, router advertisements) are held until a DTIM beacon, so a chatty network wakes everyone, and the common remedies are to **convert multicast to unicast** at the access point (one copy per interested station at the station's own rate, **from memory** of the feature name VideoStream or multicast to unicast, as implemented by vendors) and to **rate limit** multicast, which otherwise goes at the lowest basic rate.

**What power save saves.**

--- energy: the average current of a station that sleeps between beacons (assumed currents: doze 0.3 mA, receive 70 mA, idle listening 50 mA; a model, not a measurement)
a wake-up costs: 1.5 ms of early wake and clock drift margin, the beacon (0.4 ms at the lowest rate with a long element list), and 0.5 ms of idle listening to see the TIM is clear
mode                              wakes per second   awake ms per second   average mA   relative to always awake
always awake (CAM)                           -                 1000          50.0                100.0 %
beacon every 1, TIM check                 9.77                 23.4          1.46                  2.9 %
beacon every 3, TIM check                 3.26                  7.8          0.69                  1.4 %
beacon every 10, TIM check                0.98                  2.3          0.42                  0.8 %
a phone that must still hear every DTIM beacon (DTIM 1) cannot sleep longer than one interval for multicast, whatever its listen interval; some APs use a DTIM period of 3 to let phones sleep through more beacons (from memory), at the price of the multicast wait in the table above

With the assumed currents a station that wakes **every beacon** averages **1.46 mA, 2.9 %** of always awake; at listen interval 3 it is **0.69 mA (1.4 %)** and at 10 **0.42 mA (0.8 %)**. Past the first step the saving per extra beacon is small, which is why a typical phone listens to **every** beacon or every third, and the price is latency. This is a model with assumed currents, not a measurement: use it for the shape of the trade.

**Why legacy delivery cannot carry voice.** Legacy power save delivers one frame per poll:

--- the legacy delivery mechanism cannot carry voice: PS-Poll returns one frame per poll
a VoIP call needs 50 packets per second each way (20 ms packets). At most one frame per beacon without the More Data bit is 9.8 frames per second
with More Data the station polls again at once: each poll is a PS-Poll (16 + 4 FCS bytes at 24 Mbit/s), SIFS, a data frame, SIFS, an ACK; about 188 us per frame, so the delay is bounded by the beacon interval (102 ms), not the call period (20 ms)
U-APSD (WMM Power Save): the station marks voice as trigger and delivery enabled; its own uplink voice frame is the trigger and the AP answers with a burst of buffered frames ending in an end-of-service-period bit
  mean wake-ups for a call: 50 per second (the station wakes anyway to send), downlink delay at most one packet period = 20 ms, and the station sleeps between: 7.5 % of the time awake if each wake is 1.5 ms

A call needs **50 packets a second** each way; at most one frame per beacon is **9.8 frames per second**. With the More Data bit the station polls again at once (each poll is a PS-Poll of 20 bytes, SIFS, data, SIFS, ACK, about **188 µs** per frame), so the delay is bounded by the **beacon interval (102 ms)** and not by the call period (20 ms). **U-APSD** (WMM Power Save) fixes this: the station marks voice as **trigger and delivery enabled**, **its own uplink voice frame is the trigger**, and the access point answers with a burst of buffered frames ending in an **end of service period** bit. The station wakes anyway to send, so the downlink delay is **at most one packet period, 20 ms**, and the station sleeps between: **7.5 %** of the time awake if each wake is 1.5 ms, at 50 wakes a second.

**Target wake time (TWT)**, introduced in 802.11ax, replaces polling with a **schedule** the station and access point agree on: a wake interval `mantissa x 2^exponent` microseconds and a wake duration of a number of 256 µs units.

--- target wake time (TWT, 802.11ax): the station and AP agree on a wake interval and a wake duration; interval = mantissa x 2^exponent us, duration = value x 256 us
mantissa  exponent  interval         wake duration   awake share   average mA (assumed currents)
  1000        10         1.02 s         1024 us         0.1000 %          0.350
  1000        12         4.10 s         2048 us         0.0500 %          0.325
  1000        15        32.77 s         4096 us         0.0125 %          0.306
  1000        17       131.07 s        10240 us         0.0078 %          0.304
  3000        20      3145.73 s        25600 us         0.0008 %          0.300
TWT can sleep for minutes where a beacon schedule cannot, and the AP can schedule stations so that they do not wake together: but an interval of 3146 s means a downlink frame waits up to that long

TWT lets a station sleep for **minutes**, where a beacon schedule cannot: a mantissa of 1,000 and an exponent of 17 give **131.07 s**, and 3,000 with 20 give **3,145.73 s**, at an average of **0.304 and 0.300 mA** on the assumed currents. The access point can schedule stations so they do **not wake together**, which also removes contention. The price is the one the last line states: **a downlink frame may wait up to the interval**, so TWT fits sensors, cameras that upload and doors that report, not phones that must ring. Individual and broadcast TWT, the negotiation frames and the Wi-Fi 6 context are in [WF 06](06_wifi6_6e_7_features.md), and the sleep modes of Wi-Fi 7 (the multi-link ones) are in [WF 12](12_wifi7_eht_and_mlo.md).

---

## 7. Rate adaptation and the performance anomaly

**The anomaly.** DCF gives every contending station the same chance at the medium, so in the long run every station sends the same number of **packets**. A station at 6 Mbit/s takes 2,233 µs per 1500 byte packet and one at 54 Mbit/s takes 393 µs, so the slow one holds the medium **5.7 times as long** per packet and every other station waits for it.

--- the performance anomaly: DCF gives every station the same number of PACKETS per second, so a slow station takes most of the airtime
saturated stations, 1500 byte packets, per packet time = data + SIFS + ACK + DIFS + mean backoff (collisions ignored: section 2 puts them at 11 to 26 % of attempts for 2 to 5 stations)
packet time us: 6 Mbit/s: 2233, 12 Mbit/s: 1197, 24 Mbit/s: 681, 36 Mbit/s: 509, 54 Mbit/s: 393

stations (rates Mbit/s)   per packet fairness (DCF)                       airtime fairness (equal airtime)
                          each station Mbit/s   total   slow share of air   each station Mbit/s (fast / slow)   total
1 x 54 + 1 x 54               15.25          30.50           50 %             15.25 / 15.25                30.50
3 x 54 + 1 x 54                7.62          30.50           25 %              7.62 / 7.62                 30.50
1 x 54 + 1 x 24               11.16          22.33           63 %             15.25 / 8.80                 24.05
3 x 54 + 1 x 24                6.44          25.78           37 %              7.62 / 4.40                 27.27
1 x 54 + 1 x 12                7.54          15.08           75 %             15.25 / 5.01                 20.26
3 x 54 + 1 x 12                5.05          20.19           50 %              7.62 / 2.51                 25.38
1 x 54 + 1 x 6                 4.57           9.14           85 %             15.25 / 2.69                 17.93
3 x 54 + 1 x 6                 3.51          14.06           65 %              7.62 / 1.34                 24.21
with one station at 6 Mbit/s among three at 54, the slow one takes 65 % of the airtime and the cell total is 14.1 Mbit/s against 24.2 under airtime fairness; each fast station gets 3.51 instead of 7.62 Mbit/s, 54 % less, because of the slow one
airtime fairness is a scheduler in the AP (one transmit queue per station and a deficit counted in microseconds), not part of DCF: the bytes are the same, the unit of fairness is time

Read the last two rows. With **three stations at 54 and one at 6 Mbit/s** the slow one takes **65 %** of the airtime, the cell carries **14.06 Mbit/s** and each fast station gets **3.51 Mbit/s**; with **airtime fairness** (every station gets equal time) the same cell carries **24.21 Mbit/s** and each fast station **7.62**, **54 % less** with the slow client in the per packet case, as the last line says. With one fast and one slow station (54 and 6) the slow one takes **85 %** of the air. Two things follow. **Airtime fairness is a scheduler in the access point** (one transmit queue per station and a deficit counted in microseconds), not part of DCF, which is why vendors differ and why it is a product feature. And **the bytes are the same**: the unit of fairness is time. The design consequences are the **minimum data rate** setting (drop rates below the cell edge so that a slow client roams or fails instead of holding the air) and the coverage plan of [WF 05](05_wifi_roaming_and_radio_management.md).

**Choosing a rate.** The transmitter picks an MCS for each frame, and the receiver never says which would have worked. The transmitter must **learn** from the ACKs. Two classic algorithms:

- **ARF (auto rate fallback)**: after a number of successes in a row (the model uses 10) try one step up; after a failure (or two) step down. Cheap, and reactive, with no memory of other rates.
- **Minstrel-like sampling**: keep a **success statistic per rate**, send most frames at the rate with the best **expected throughput** (success probability times rate) and a fraction (the model uses 10 %) at a random other rate to refresh the statistics. The Linux `minstrel_ht` family works this way (**from memory**; its code is [WF 10](10_linux_wireless_stack_and_openwrt.md)).

The model gives the success probability of each MCS as a logistic function of the SNR around the threshold of [WF 11](11_rf_fundamentals_and_link_budget.md) plus 1 dB, 0.9 dB wide, on HE 20 MHz, one stream, GI 0.8 µs:

--- rate adaptation: which MCS to send each frame at. A model: success probability of MCS m is a logistic of the SNR around the WF 11 threshold (+1 dB), 0.9 dB wide; HE 20 MHz, one stream, GI 0.8 us
MCS: 0 1 2 3 4 5 6 7 8 9 10 11
rate Mbit/s: 9 17 26 34 52 69 77 86 103 115 129 143
SNR needed dB: 4 8 11 13 16 20 21 23 26 28 31 33

--- mean goodput (Mbit/s) over 20,000 frames, including the airtime of failed frames
channel          oracle   Minstrel-like   ARF     fixed MCS 4   fixed MCS 11   (Minstrel-like and ARF as a share of the oracle)
static 15 dB                   17.3      16.1       16.9       1.7          0.0       93.3 % and  97.7 %
static 25 dB                   32.4      28.1       28.6      27.7          0.0       86.6 % and  88.2 %
static 35 dB                   39.5      35.2       34.4      27.7         31.0       89.1 % and  87.2 %
fading, mean 25 dB, sd 5 dB    30.5      20.5       27.1      26.3          1.6       67.3 % and  89.0 %
the sampler pays about 10 % in the static rows for its sampling frames (10 % of frames at a random rate), and ARF is ahead at 15 dB (97.7 % of the oracle against 93.3 %) and behind at 35 dB (87.2 % against 89.1 %) because it climbs one step per 10 successes

The **oracle** (a transmitter that knows the SNR) is the ceiling: **17.3 Mbit/s at 15 dB, 32.4 at 25 and 39.5 at 35**. Read the rows. **Static channels**: both algorithms get **87 to 98 %** of the oracle; the sampler pays about **10 %** for its sampling frames, and ARF is ahead at 15 dB (**97.7 %** against **93.3 %**) and behind at 35 dB (**87.2 %** against **89.1 %**) because it climbs one step per ten successes. **A fixed MCS** is a lesson of its own: MCS 4 gives **1.7 Mbit/s at 15 dB** (below its threshold) and **27.7 at 25 and 35**, and MCS 11 gives **0.0 below 33 dB**; no fixed rate is right everywhere. **Fading**: the sampler falls to **67.3 %** of the oracle and ARF holds **89.0 %**, because the sampler's statistics describe a channel that has already changed:

--- the same fading channel with the statistics updated more or less often, and a faster channel (correlation per frame 0.9 instead of 0.99)
update every N frames   Minstrel-like on rho 0.99 (share of oracle)   on rho 0.9
          20              23.9 Mbit/s (78.5 %)               21.3 Mbit/s (71.6 %)
          50              21.8 Mbit/s (71.6 %)               20.0 Mbit/s (67.0 %)
         100              20.5 Mbit/s (67.3 %)               19.3 Mbit/s (64.6 %)
         400              20.9 Mbit/s (68.6 %)               20.0 Mbit/s (67.2 %)
fast fading (rho 0.9): oracle 29.8, ARF 25.2, fixed MCS 4 25.7 Mbit/s: a fixed MCS 4 is level with ARF and well ahead of the sampler (19 to 21 Mbit/s above): when the channel changes faster than the statistics, learning has little to offer

The table answers "how often should the statistics update": every **20 frames** gives **78.5 %** of the oracle on the slow fading channel (correlation 0.99 per frame) and **400 frames 68.6 %**, and on a channel that changes faster (0.9 per frame) **every setting is below 72 %** and **a fixed MCS 4 (25.7 Mbit/s) is level with ARF (25.2)** and well ahead of the sampler (19 to 21). The lesson is general: **when the channel changes faster than the statistics, learning has little to offer**, and the better tool is the SNR the receiver reports (the HT control field and the VHT/HE feedback of section 9 are the in-band channels for that) and a conservative rate for the first retry. These are my own simplified algorithms on a logistic model, so they teach the shape of the trade and not the numbers of any driver (limit 6).

**Retries.** A real driver does not give up at the first failure: it uses a **multi-rate retry chain** (try the best rate a few times, then the next, then a robust fallback), and each retry costs the airtime of a whole frame, so a link that retries at 30 % loses far more than 30 % of its throughput once the contention window doubles (section 2).

## 8. Scanning, association, states and retries

**Scanning.** A station that wants a network must find it, and the cost of finding it is time on every channel.

--- scanning: how long it takes to look at every channel (times are model inputs; the dwell times are assumptions, not the standard)
passive scan: listen on each channel for one beacon interval (102.4 ms) to be sure of hearing a beacon; active scan: send a probe request and wait
active scan wait: a minimum (nothing answered) and a maximum (something answered) per channel; assumed 20 ms and 40 ms
band / channel set                         channels   passive s   active s (nothing heard)   active s (every channel busy)
2.4 GHz, channels 1 to 13                     13        1.33               0.26                       0.52
5 GHz, 25 channels (US), 16 are DFS           25        2.56               0.50                       1.00
5 GHz, non DFS only                            9        0.92               0.18                       0.36
6 GHz, all 59 channels                        59        6.04               1.18                       2.36
6 GHz, 15 preferred scanning channels         15        1.54               0.30                       0.60
DFS channels cannot be probed until the client has heard a beacon there (it may not transmit before radar is ruled out, from memory), so they are scanned passively: 16 DFS channels cost 1.64 s of the 5 GHz scan
6 GHz: devices may not probe a channel until they know an AP is there, which is why 6 GHz APs use preferred scanning channels, FILS discovery frames (every 20 ms) and a reduced neighbor report in the 2.4 and 5 GHz beacons (WF 06); a full passive scan costs 6.0 s
a full scan of all three bands, passive 6 GHz: 8.6 s; with the 15 preferred channels: 4.1 s (an active scan on 2.4 and 5 GHz plus passive 6 GHz)

A **passive scan** listens on each channel for one beacon interval (102.4 ms) to be sure of hearing a beacon; an **active scan** sends a probe request and waits for a **minimum** wait (nothing answered, assumed 20 ms) or a **maximum** (something answered, assumed 40 ms). The totals are **1.33 s passive for 13 channels in 2.4 GHz, 2.56 s for the 25 channels of 5 GHz and 6.04 s for the 59 channels of 6 GHz**; actively, 2.4 GHz costs **0.26 to 0.52 s**. In 5 GHz the 16 **DFS** channels cannot be probed until a beacon has been heard there, so they are scanned passively and cost **1.64 s** of the 5 GHz scan. In 6 GHz a device may not probe before it knows an access point is present, which is why 6 GHz access points use **preferred scanning channels** (15 of the 59), **FILS discovery frames** every 20 ms and a **reduced neighbour report** in the 2.4 and 5 GHz beacons ([WF 06](06_wifi6_6e_7_features.md)). A full scan of all three bands takes **8.6 s** with a passive 6 GHz and **4.1 s** with the 15 preferred channels; this is the number behind "why is my first connection to a tri-band network slow", and roaming avoids it by scanning only what an access point reports ([WF 05](05_wifi_roaming_and_radio_management.md)).

**The join.** After the scan, the open join is six frames: a probe request and response, authentication in two frames and association in two.

--- the open system join, frame by frame, at 6 Mbit/s with the contention and SIFS gaps (a model of the airtime only: the real time is dominated by the scan and by the key handshake and DHCP that follow)
frame                      bytes   airtime us   + ACK and gaps us
probe request                 36           80               242
probe response                84          144               306
authentication 1              30           72               234
authentication 2              30           72               234
association request          103          168               330
association response          80          136               298
the six frames take about 1.6 ms of airtime; then the 4-way handshake (four EAPOL frames, WF 04) and DHCP (four frames, NET 08) follow

The six frames take about **1.6 ms** of airtime at 6 Mbit/s with the gaps: tiny against the scan, and tiny against what follows. The real time is dominated by the **scan**, the **4-way handshake** (four EAPOL frames, [WF 04](04_wifi_security.md)) and **DHCP** (four frames, [NET 08](../08_application_protocols_dns_http_tls.md)). That is the answer to "why does it take two seconds to connect": it is the scan, the handshake and the address assignment, not the association.

**The three states.** A station and an access point track one of three states per station: **state 1** (unauthenticated, unassociated), **state 2** (authenticated, not associated) and **state 3** (authenticated and associated). Frames come in three **classes**, and a frame is allowed only in the state that matches its class (the class lists are **from memory**): **class 1** (any state: probe, beacon, authentication, deauthentication, ACK, RTS, CTS and some action frames), **class 2** (state 2 and up: association, reassociation, disassociation) and **class 3** (state 3: data, PS-Poll, block ack).

--- the three states and the three frame classes: what an AP does with a frame from a station in the wrong state
state 1: unauthenticated and unassociated; state 2: authenticated, not associated; state 3: authenticated and associated
class 1 (any state): probe, beacon, authentication, deauthentication, ACK, RTS, CTS, some action frames; class 2 (state 2 and up): association, reassociation, disassociation; class 3 (state 3): data, power save poll, block ack (the class lists are from memory)
event                                                  AP reply
station in state 1 sends an authentication request (class 1) none, the frame is accepted
state 1 sends an association request (class 2)         deauthentication, reason 6 (class 2 frame from an unauthenticated station)
state 1 sends data (class 3)                           deauthentication, reason 7 (class 3 frame from an unassociated station)
state 2 sends an association request (class 2)         none, the frame is accepted
state 2 sends data (class 3)                           deauthentication, reason 7 (class 3 frame from an unassociated station)
state 3 sends data (class 3)                           none, the frame is accepted
tshark reads the reason codes of the built deauthentication frames as: 0x0006, 0x0007, 0x0007 (6 and 7 as built)
this is why an attack that spoofs a deauthentication frame works against an unprotected network: the station accepts a class 1 frame in any state and drops to state 1 (WF 04, management frame protection)

The table is the logic of the access point. A class 2 frame from a station in state 1 draws a **deauthentication with reason 6**, a class 3 frame from a station in state 1 or 2 draws **reason 7**, and `tshark` reads back **6, 7 and 7** from the frames I built. The last line has a security meaning: **deauthentication is a class 1 frame**, so a station accepts it in any state, which is why a spoofed deauthentication works against a network without protected management frames and why 802.11w ([WF 04](04_wifi_security.md)) exists.

**Retries and duplicates.** A sender repeats a unicast frame when the ACK is lost. The receiver must not deliver the packet twice, so each frame carries a **12 bit sequence number** and a **4 bit fragment number**; a repeat carries the same numbers and the **Retry** bit, and the receiver keeps the last (sequence, fragment) per sender and drops a repeat.

--- retries and duplicate detection: the sender repeats a frame when the ACK is lost; the receiver must not deliver it twice
a frame carries a 12 bit sequence number and a 4 bit fragment number; a repeat carries the same numbers and the Retry bit; the receiver keeps the last (sequence, fragment) per sender and drops a repeat
ACK loss 0.00: 100000 frames sent for 100000 packets (1.000 per packet); without duplicate detection 0.0 % of deliveries would be duplicates
ACK loss 0.05: 105269 frames sent for 100000 packets (1.053 per packet); without duplicate detection 5.0 % of deliveries would be duplicates
ACK loss 0.20: 124754 frames sent for 100000 packets (1.248 per packet); without duplicate detection 19.8 % of deliveries would be duplicates
the Retry bit marks a repeat, a retry count of 7 (4 above the RTS threshold, from memory) ends the packet; the 12 bit sequence number wraps after 4,096 frames, so a receiver with a block ack window larger than 2,048 could not tell new from old (the window is at most 1,024 in EHT)

At an ACK loss of **5 %** the sender sends **1.053 frames per packet** and without duplicate detection **5.0 %** of the deliveries would be duplicates; at **20 %** it sends **1.248** and **19.8 %** would be duplicates. The retry limit ends a packet (7 below the RTS threshold and 4 above, **from memory**). The sequence number wraps after 4,096 frames, so the block ack window must be well below 2,048 (it is 64, 256 or 1,024). A capture shows retries as the **Retry flag**, and a **retry rate** over about 10 % is the first symptom to read in a capture ([WF 08](08_wifi_capture_tools_and_debug.md)).

## 9. The HT elements: how a station says what it can do

Management frames carry the **capabilities** of each side as **information elements (IE)**: an ID byte, a length byte, a body. The 802.11n elements are the template for the VHT and HE ones.

--- HT capabilities element (id 45), field by field, for a typical 2 stream 40 MHz access point
HT capabilities info (2 bytes) = 0x016f = 0000000101101111
  bit  0  LDPC coding                    1
  bit  1  40 MHz channel width           1
  bit  4  greenfield                     0
  bit  5  short GI 20 MHz                1
  bit  6  short GI 40 MHz                1
  bit  7  transmit STBC                  0
  bit 10  delayed block ack              0
  bit 11  max A-MSDU 7935 (else 3839)    0
  bit 12  DSSS/CCK in 40 MHz             0
  bit 14  40 MHz intolerant              0
  bit 15  L-SIG TXOP protection          0
  bits 2-3 SM power save = 3 (0 static, 1 dynamic, 3 disabled); bits 8-9 RX STBC streams = 1
A-MPDU parameters byte: maximum length exponent 3 gives 2^(13 + 3) - 1 = 65535 bytes; minimum MPDU start spacing code 6
supported MCS set: bytes 0 and 1 = 0xff 0xff: MCS 0 to 7 (stream 1) and 8 to 15 (stream 2); the highest supported data rate field is 0 (not stated)
element: id 45, length 26, hex starts 6f011bffff00000000000000

The **HT capabilities info** field (2 bytes, `0x016f` in the example) is a bit map: **LDPC** coding, **40 MHz** support, **short guard interval** for 20 and 40 MHz, **greenfield** mode, transmit STBC, the **receive STBC** stream count, the **SM power save** mode (0 static, 1 dynamic, 3 disabled), the delayed block ack, the **maximum A-MSDU length** (3,839 or 7,935 bytes), **DSSS/CCK in 40 MHz**, **40 MHz intolerant** and L-SIG TXOP protection. The **A-MPDU parameters** byte gives the **maximum length exponent** (3 means `2^(13 + 3) - 1 = 65,535` bytes, the HT limit of section 5) and a **minimum MPDU start spacing**. The **supported MCS set** (16 bytes in a 26 byte element, with extended capabilities, transmit beamforming and antenna selection fields after it) lists the MCS of each stream count: `0xff 0xff` is MCS 0 to 15, two streams.

--- HT operation element (id 61): what the BSS actually uses
primary channel 6; subset 1 byte = 0x05: secondary channel offset 1 (0 none, 1 above, 3 below), STA channel width 1 (1 = any width), RIFS 0
HT protection modes, from the operation field (bits 0-1 of the second field):
  0  no protection: no non-HT stations anywhere in range
  1  non-member protection: non-HT stations are in an overlapping BSS
  2  20 MHz protection: a 20 MHz only HT station is associated
  3  non-HT mixed: a non-HT station is associated, so use protection (legacy preamble, CTS-to-self)

tshark reads the same element back: LDPC True, 40 MHz True, short GI 20 True, short GI 40 True, A-MPDU maximum length code 0x03, primary channel 6, secondary channel offset 0x01

The **HT operation** element is what the BSS **actually uses**: the primary channel (6), the **secondary channel offset** (1 above, 3 below, 0 none), the **STA channel width** and the **HT protection mode**. The four modes are exactly the protection ideas of section 3 applied to HT: mode **0** (no protection, no non-HT station anywhere in range), **1** (non-member protection, non-HT stations in an overlapping BSS), **2** (20 MHz protection, a 20 MHz only HT station is associated) and **3** (non-HT mixed, a non-HT station is associated, so use a legacy preamble and CTS-to-self). `tshark` reads back LDPC, 40 MHz, both short guard bits, the A-MPDU length code **0x03**, the primary channel and the secondary offset, which checks the layout of every field I built.

--- the HT control field (4 bytes, present when the Order bit is set in a QoS frame): link adaptation and sounding requests in the MAC header
bit 0 = 0 for HT (bit 0 = 1, bit 1 = 0 for VHT, bits 0-1 = 11 for HE); in the HT variant: link adaptation control (training request, MCS request and feedback), calibration position and sequence, CSI/steering (0 none, 1 CSI, 2 non-compressed, 3 compressed), NDP announcement (bit 18), AC constraint, RDG/more PPDU (bit 31)
the control field turns a data frame into a carrier for a rate request: an HT station asks its peer for a link adaptation MCS or a sounding exchange in-band, which is how HT explicit beamforming (WF 02) was requested

The **HT control field** is a 4 byte extension of the MAC header, present when the **Order bit** is set in a QoS frame. The first bits tell the generation (bit 0 = 0 for HT, bit 0 = 1 and bit 1 = 0 for VHT, bits 0 and 1 = 11 for HE), and in the HT variant it carries **link adaptation** (training request, MCS request and feedback), **calibration** and the **CSI/steering** request for sounding (0 none, 1 CSI, 2 non-compressed, 3 compressed). It turns a data frame into a **carrier for a rate request**: an HT station asks its peer for a rate or a sounding exchange in band, which is how explicit beamforming ([WF 02](02_wifi_phy.md)) was requested. **VHT and HE capabilities** (from memory of the layout) add the wider channel widths, the streams per MCS map, the beamformer and beamformee capabilities, the MU-MIMO and OFDMA fields and the HE 6 GHz band capabilities. Parse them from a capture ([WF 08](08_wifi_capture_tools_and_debug.md)): the field names are in `tshark` (`wlan.ht.capabilities`, `wlan.ht.info`, `wlan.vht.capabilities`, `wlan_ext.he_mac_caps`).

## 10. Common mistakes

- **"More MCS means more throughput."** The first MPDU at HE 80 MHz MCS 11 delivers **9.1 %** of the PHY rate. The preamble, the waits, the backoff and the acknowledgement are fixed costs (section 5).
- **"RTS/CTS always helps."** With 2 or 3 hidden stations it **loses** (23.37 against 27.54 Mbit/s) and costs **21.3 %** of a short exchange (section 3).
- **"EF is voice, so it gets the voice queue."** Under the naive map EF (46) is user priority 5 and goes to **video**; RFC 8325 moves it to UP 6 (section 4).
- **"Priority means guaranteed bandwidth."** EDCA gives **chances to transmit**. Saturated voice takes **95.2 %** of the air against ten best effort stations; real voice is not saturated, and there is no reservation unless a TSPEC is admitted (section 4).
- **"One slow client only slows itself."** It takes **65 %** of the airtime in a cell of four and cuts the others by **54 %** (section 7).
- **"Aggregate as much as you can."** A-MSDU at a packet error rate of **0.05** delivers **91.6 Mbit/s** against **424.6** for A-MPDU. Aggregation needs the loss to be low and the window to be respected (section 5).
- **"Deauthentication needs the station's state."** It is a class 1 frame, accepted in any state (section 8).
- **"Longer listen interval is free."** A listen interval of 10 puts the worst unicast wait at **1,024 ms**, and DTIM period 10 does the same for multicast (section 6).
- **"TWT is a better PS-Poll."** TWT trades latency for sleep: an interval of **3,146 s** is only for devices that tolerate it (section 6).
- **"Association is the slow part of connecting."** The six frames take **1.6 ms**; the scan (up to **8.6 s**) and the handshake and DHCP dominate (section 8).
- **"Fragmentation reduces retries."** On a clean link it costs **87 %** more airtime and pays only from a byte error rate near 1e-3 (section 3).
- **"The 2.4 GHz network must keep the 1 Mbit/s rate for compatibility."** It makes every OFDM frame carry a **314 µs** protection frame while an 802.11b client is present (section 3).

## 11. Interview questions

1. **Walk through one unicast exchange on the air.** Wait DIFS (AIFS) plus a random backoff counted down on idle slots, send the data, wait SIFS, receive the ACK; **393.5 µs** for 1500 bytes at 54 Mbit/s, **30.5 Mbit/s**, **56.5 %** of the PHY rate. Say which part is fixed (section 2).
2. **Why is Wi-Fi throughput so far below the PHY rate?** The fixed cost per exchange; with one MPDU HE 80 MHz MCS 11 delivers **9.1 %** and 256 MPDUs **92.6 %**; the number is a function of the aggregate (section 5).
3. **What is the NAV and who sets it?** A counter set from the duration field of any decoded frame; the RTS sets **352 µs** and the CTS **308 µs** in the example; a hidden station hears the CTS and defers (section 3).
4. **When does RTS/CTS help?** When collisions are frequent or frames long: it loses with 2 to 3 hidden stations and wins from 5 (**+28 %**, **+273 % at 20**); cost **88 µs** (section 3).
5. **What are the four access categories and how does EDCA enforce priority?** AIFS, CWmin and CWmax and the TXOP limit per class; **VO takes 66.8 %** against one station per class; EF lands in VI under the naive map (section 4).
6. **How many voice calls does a cell carry?** At most **22, 36, 52 and 67** at 6, 12, 24 and 54 Mbit/s (upper bounds) and a call at 6 Mbit/s costs **3.1 times** the airtime; so admission control is in medium time (section 4).
7. **A-MPDU or A-MSDU?** A-MSDU saves bytes (one header) and loses the whole aggregate on one error; A-MPDU saves time and recovers per MPDU with block ack; use A-MSDU only for tiny packets on clean links, inside an A-MPDU (section 5).
8. **How does block ack work and what limits it?** An agreement per TID by ADDBA, a window of **64, 256 or 1,024** and a bitmap of 8, 32 or 128 bytes; the window caps the aggregate (section 5).
9. **How does a phone sleep?** The Power Management bit, the TIM and DTIM in the beacon, the listen interval, PS-Poll (9.8 frames a second at most), U-APSD for voice (**20 ms** bound) and TWT for scheduled wake (section 6).
10. **What is the performance anomaly and how do you fix it?** DCF is fair in packets, not in time; a 6 Mbit/s client takes **65 %** of the air among three at 54 Mbit/s; airtime fairness in the scheduler and a higher minimum rate fix it (section 7).
11. **How does rate adaptation decide, and when does it fail?** ARF steps on successes and failures, a sampler keeps statistics per rate; both are close to the oracle on a static link (**87 to 98 %**) and the sampler loses on a fast channel (**67.3 %** against **89.0 %** for ARF in the model) (section 7).
12. **What happens between "scan" and "connected"?** Scan (up to **8.6 s**), open authentication and association (**1.6 ms**), the 4-way handshake and DHCP; and the three states and classes decide what the access point accepts (section 8).

---

## 12. Problems

Each problem has a script in `prog/` (`ans_a.py` to `ans_e.py`) whose output is frozen in `v/`. Try them on paper first with the formulas of this tutorial.

**Problem A: read the frames.** Decode seven frames from their first bytes: a beacon, a QoS data frame from an access point to a station with the Protected flag, a QoS null frame from a station with ToDS and the power management bit set, an RTS, a PS-Poll, a CTS and a retransmitted QoS data frame from a station. For each give the type and subtype, the flags, the meaning of the three addresses, the duration or NAV, and the TID or AID where there is one.

**Problem B: airtime and the cost of RTS/CTS.** For a 1000 byte payload at 24 Mbit/s, a 1500 byte payload at 54 Mbit/s, a 200 byte payload at 54 Mbit/s and a 1500 byte payload at 6 Mbit/s, compute the exchange time and the goodput with and without RTS/CTS, and the cost of RTS/CTS as a share. Give the duration field of the RTS and of the CTS for the first case. For how many saturated stations does the Bianchi model put the cell below 80 % of the single station rate (1500 bytes at 54 Mbit/s)?

**Problem C: EDCA.** Map EF, AF41, CS3, CS1 and DSCP 0 to an access category under the naive map. Compute the AIFS of each class. For fresh backoffs in one contention, compute the chance that VO beats BE, VI beats BE, BE beats BK, VO beats VI and BE beats BE, with the chance of a tie (a collision). How many frames fit in the TXOP of a voice station (200 bytes at 24 Mbit/s, TXOP 1,504 µs) and of a video station (1500 bytes at 54 Mbit/s, 3,008 µs)?

**Problem D: aggregation.** For HE one stream with 1500 byte MPDUs at 20 MHz MCS 7, 80 MHz MCS 11 and 160 MHz MCS 11, find the smallest aggregate that reaches 50, 70, 80 and 90 % of the PHY rate. For 32 packets of 100, 300 and 1500 bytes at HE 80 MHz MCS 11 find the loss probability at which an A-MSDU falls below an A-MPDU.

**Problem E: power save.** Build the TIM for AIDs 3, 9, 70 and 71. For a unicast latency target of 300 ms pick the listen interval and its average current. For a sensor that sends 100 bytes every 60 s and tolerates a downlink wait of 10 s, choose a TWT interval and compute the average current and the coin cell life.

## 13. Answers

**Answer A.** The script decodes each frame from its bytes with the rules of section 1.

```python
# ans_a.py: decode 802.11 frames from their bytes
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dot11 import *
def mac(b): return ':'.join('%02x' % x for x in b)
def decode(f):
    b0, b1 = f[0], f[1]
    ver, typ, sub = b0 & 3, (b0 >> 2) & 3, b0 >> 4
    flags = [n for i, n in enumerate(['ToDS', 'FromDS', 'MoreFrag', 'Retry', 'PwrMgt', 'MoreData', 'Protected', 'Order']) if (b1 >> i) & 1]
    dur = struct.unpack('<H', f[2:4])[0]
    name = (MG, CT, DT)[typ][sub]
    s = '%s %s (type %d subtype %d), flags %s, duration/ID 0x%04x' % (['management', 'control', 'data'][typ], name, typ, sub, ','.join(flags) or 'none', dur)
    if typ == 1:
        if sub in (11, 10, 8, 9): s += ', RA %s TA %s' % (mac(f[4:10]), mac(f[10:16]))
        else: s += ', RA %s' % mac(f[4:10])
        if sub == 10: s += ', association ID %d' % (dur & 0x3fff)
        elif dur and sub in (11, 12): s += ', NAV %d us' % dur
        return s
    a1, a2, a3 = mac(f[4:10]), mac(f[10:16]), mac(f[16:22])
    seq = struct.unpack('<H', f[22:24])[0]
    s += ', sequence %d fragment %d' % (seq >> 4, seq & 15)
    if typ == 0: s += ', DA %s SA %s BSSID %s' % (a1, a2, a3)
    else:
        tods, fromds = b1 & 1, (b1 >> 1) & 1
        who = {(0, 0): 'DA %s SA %s BSSID %s', (1, 0): 'BSSID %s SA %s DA %s', (0, 1): 'DA %s BSSID %s SA %s'}.get((tods, fromds))
        if who: s += ', ' + who % (a1, a2, a3)
        if sub >= 8: s += ', QoS control 0x%04x (TID %d)' % (struct.unpack('<H', f[24:26])[0], f[24] & 15)
    return s
frames = [mgmt(8, BCAST, AP, AP, beacon_body(b'lab'), 1405), data(8, STA, AP, STA2, b'', 77, qos=5, dur=44, from_ds=1, prot=1), data(12, AP, STA, AP, b'', 78, qos=0, to_ds=1, pwr=1), ctrl(11, 352, AP, STA), ctrl(10, 0xc005, AP, STA), ctrl(12, 308, STA), data(8, AP, STA, STA2, b'x', 79, qos=6, to_ds=1, retry=1, prot=1, dur=44)]
for f in frames:
    print(f[:16].hex(), '...' if len(f) > 16 else '')
    print('   ->', decode(f))
```

80000000ffffffffffff020000000001 ...
   -> management beacon (type 0 subtype 8), flags none, duration/ID 0x0000, sequence 1405 fragment 0, DA ff:ff:ff:ff:ff:ff SA 02:00:00:00:00:01 BSSID 02:00:00:00:00:01
88422c000200000000aa020000000001 ...
   -> data QoS data (type 2 subtype 8), flags FromDS,Protected, duration/ID 0x002c, sequence 77 fragment 0, DA 02:00:00:00:00:aa BSSID 02:00:00:00:00:01 SA 02:00:00:00:00:bb, QoS control 0x0005 (TID 5)
c81100000200000000010200000000aa ...
   -> data QoS null (no data) (type 2 subtype 12), flags ToDS,PwrMgt, duration/ID 0x0000, sequence 78 fragment 0, BSSID 02:00:00:00:00:01 SA 02:00:00:00:00:aa DA 02:00:00:00:00:01, QoS control 0x0000 (TID 0)
b40060010200000000010200000000aa
   -> control RTS (type 1 subtype 11), flags none, duration/ID 0x0160, RA 02:00:00:00:00:01 TA 02:00:00:00:00:aa, NAV 352 us
a40005c00200000000010200000000aa
   -> control PS-Poll (type 1 subtype 10), flags none, duration/ID 0xc005, RA 02:00:00:00:00:01 TA 02:00:00:00:00:aa, association ID 5
c40034010200000000aa
   -> control CTS (type 1 subtype 12), flags none, duration/ID 0x0134, RA 02:00:00:00:00:aa, NAV 308 us
88492c000200000000010200000000aa ...
   -> data QoS data (type 2 subtype 8), flags ToDS,Retry,Protected, duration/ID 0x002c, sequence 79 fragment 0, BSSID 02:00:00:00:00:01 SA 02:00:00:00:00:aa DA 02:00:00:00:00:bb, QoS control 0x0006 (TID 6)

The first byte tells the type and subtype at once (`0x80` beacon, `0x88` QoS data, `0xc8` QoS null, `0xb4` RTS, `0xa4` PS-Poll, `0xc4` CTS). In the second frame **FromDS** is set, so Address 1 is the station (DA), Address 2 the BSSID and Address 3 the original source; in the third and the last **ToDS** is set, so Address 1 is the BSSID. The RTS announces **352 µs** and the CTS **308 µs**; in the PS-Poll the duration field `0xc005` is **association ID 5** (the top two bits are set). The last frame has the **Retry** flag set, the signal that a capture counts to find the retry rate.

**Answer B.**

```python
# ans_b.py: airtime and goodput of one exchange, RTS/CTS cost, and the station count at which DCF loses 20 % of its throughput
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
SIFS, SLOT, DIFS = 16, 9, 34
def ppdu(n, r): return 20 + 4 * math.ceil((16 + 8 * n + 6) / (r * 4))
def exch(payload, rate, rts=False, ackrate=24):
    mpdu = payload + 36
    t = ppdu(mpdu, rate) + SIFS + ppdu(14, ackrate) + DIFS + 7.5 * SLOT
    if rts: t += ppdu(20, ackrate) + SIFS + ppdu(14, ackrate) + SIFS
    return t
print('payload  rate   no RTS/CTS: exchange us  goodput Mbit/s   with RTS/CTS: exchange us  goodput Mbit/s   cost of RTS/CTS')
for p, r in ((1000, 24), (1500, 54), (200, 54), (1500, 6)):
    a, b = exch(p, r), exch(p, r, True)
    print('%5d  %4d %20.1f %16.2f %24.1f %17.2f %14.1f %%' % (p, r, a, 8 * p / a, b, 8 * p / b, 100 * (b / a - 1)))
d = ppdu(1000 + 36, 24)
print('RTS duration for the 1000 byte frame at 24 Mbit/s: 3 SIFS + CTS %d + data %d + ACK %d = %d us; the CTS carries %d us' % (ppdu(14, 24), d, ppdu(14, 24), 3 * SIFS + 2 * ppdu(14, 24) + d, 2 * SIFS + ppdu(14, 24) + d))
W, M = 16, 6
def tau_of(p):
    if abs(p - 0.5) < 1e-9: p += 1e-9
    return 2 * (1 - 2 * p) / ((1 - 2 * p) * (W + 1) + p * W * (1 - (2 * p) ** M))
def bianchi(n, payload=1500, rate=54):
    TS = ppdu(payload + 36, rate) + SIFS + ppdu(14, 24) + DIFS; TC = ppdu(payload + 36, rate) + DIFS
    lo, hi = 1e-9, 1 - 1e-9
    for _ in range(200):
        p = (lo + hi) / 2; q = 1 - (1 - tau_of(p)) ** (n - 1)
        if q > p: lo = p
        else: hi = p
    tau = tau_of((lo + hi) / 2); ptr = 1 - (1 - tau) ** n; ps = n * tau * (1 - tau) ** (n - 1) / ptr
    return ps * ptr * 8 * payload / ((1 - ptr) * SLOT + ptr * ps * TS + ptr * (1 - ps) * TC)
s1 = bianchi(1)
print('Bianchi model, 1500 bytes at 54 Mbit/s: one station %.2f Mbit/s' % s1)
for n in range(2, 60):
    if bianchi(n) < 0.8 * s1:
        print('the cell total falls below 80 %% of the single station figure at N = %d (%.2f Mbit/s); at N = 50 it is %.2f' % (n, bianchi(n), bianchi(50))); break
```

payload  rate   no RTS/CTS: exchange us  goodput Mbit/s   with RTS/CTS: exchange us  goodput Mbit/s   cost of RTS/CTS
 1000    24                513.5            15.58                    601.5             13.30           17.1 %
 1500    54                393.5            30.50                    481.5             24.92           22.4 %
  200    54                201.5             7.94                    289.5              5.53           43.7 %
 1500     6               2217.5             5.41                   2305.5              5.20            4.0 %
RTS duration for the 1000 byte frame at 24 Mbit/s: 3 SIFS + CTS 28 + data 368 + ACK 28 = 472 us; the CTS carries 428 us
Bianchi model, 1500 bytes at 54 Mbit/s: one station 30.50 Mbit/s
the cell total falls below 80 % of the single station figure at N = 38 (24.32 Mbit/s); at N = 50 it is 23.40

RTS/CTS costs **17.1 % at 24 Mbit/s with 1000 bytes**, **22.4 %** at 54 Mbit/s with 1500 bytes, **43.7 %** for a 200 byte payload and only **4.0 %** at 6 Mbit/s: **the cost is fixed and the exchange shrinks as the PHY rate rises**, so RTS/CTS is most expensive for short frames at high rates. The RTS of the first case announces **472 µs** (3 SIFS, CTS 28, data 368, ACK 28) and the CTS **428 µs**. The Bianchi model puts the cell below 80 % of the single station **30.50 Mbit/s** at **N = 38 (24.32 Mbit/s)**, and at N = 50 it is **23.40**.

**Answer C.**

```python
# ans_c.py: EDCA: the DSCP map, the AIFS, and the exact chance that voice beats best effort in one contention
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
SIFS, SLOT = 16, 9
UPAC = {0: 'BE', 1: 'BK', 2: 'BK', 3: 'BE', 4: 'VI', 5: 'VI', 6: 'VO', 7: 'VO'}
for n, v in (('EF', 46), ('AF41', 34), ('CS3', 24), ('CS1', 8), ('0x2e = 0b101110', 0x2e), ('DSCP 0', 0)):
    print('%-18s DSCP %2d -> UP %d -> %s' % (n, v, v >> 3, UPAC[v >> 3]))
P = {'BK': (15, 7), 'BE': (15, 3), 'VI': (7, 2), 'VO': (3, 2)}
print('AIFS (us) of BK, BE, VI, VO:', ', '.join('%s %d' % (a, SIFS + P[a][1] * SLOT) for a in P))
def race(a, b):
    ca, na = P[a]; cb, nb = P[b]
    win = tie = lose = 0
    for x in range(ca + 1):
        for y in range(cb + 1):
            ta, tb = na + x, nb + y
            if ta < tb: win += 1
            elif ta == tb: tie += 1
            else: lose += 1
    n = (ca + 1) * (cb + 1)
    return win / n, tie / n, lose / n
print('first contention after the medium goes idle, both fresh backoffs (a wins, collision, b wins):')
for a, b in (('VO', 'BE'), ('VI', 'BE'), ('BE', 'BK'), ('VO', 'VI'), ('BE', 'BE')):
    w, t, l = race(a, b)
    print('  %s against %s: %5.1f %%  %5.1f %%  %5.1f %%' % (a, b, 100 * w, 100 * t, 100 * l))
import math
def ppdu(n, r): return 20 + 4 * math.ceil((16 + 8 * n + 6) / (r * 4))
for ac, pl, r, txop in (('VO', 200, 24, 1504), ('VI', 1500, 54, 3008)):
    e = ppdu(pl + 36, r) + SIFS + ppdu(14, 24)
    k = 1; used = e
    while used + SIFS + e <= txop: used += SIFS + e; k += 1
    print('TXOP of %d us, %s frames of %d bytes at %d Mbit/s: %d frames of %d us each, burst %d us' % (txop, ac, pl, r, k, e, used))
```

EF                 DSCP 46 -> UP 5 -> VI
AF41               DSCP 34 -> UP 4 -> VI
CS3                DSCP 24 -> UP 3 -> BE
CS1                DSCP  8 -> UP 1 -> BK
0x2e = 0b101110    DSCP 46 -> UP 5 -> VI
DSCP 0             DSCP  0 -> UP 0 -> BE
AIFS (us) of BK, BE, VI, VO: BK 79, BE 43, VI 34, VO 34
first contention after the medium goes idle, both fresh backoffs (a wins, collision, b wins):
  VO against BE:  90.6 %    4.7 %    4.7 %
  VI against BE:  78.1 %    5.5 %   16.4 %
  BE against BK:  69.5 %    4.7 %   25.8 %
  VO against VI:  68.8 %   12.5 %   18.8 %
  BE against BE:  46.9 %    6.2 %   46.9 %
TXOP of 1504 us, VO frames of 200 bytes at 24 Mbit/s: 9 frames of 144 us each, burst 1424 us
TXOP of 3008 us, VI frames of 1500 bytes at 54 Mbit/s: 9 frames of 292 us each, burst 2756 us

EF and AF41 reach **VI** under the naive map (UP 5 and 4), CS3 goes to **BE** and CS1 to **BK**. The AIFS values are **79, 43, 34 and 34 µs**. The chance of winning a **single** contention is **90.6 % for VO against BE** (4.7 % collisions), **78.1 % for VI against BE**, **69.5 % for BE against BK**, **68.8 % for VO against VI** (12.5 % collisions, since both wait 34 µs and their windows overlap) and **46.9 % each way** for two BE queues with a **6.2 %** collision. One contention is not the long run: the saturated simulation of section 4 gives voice **66.8 %** of the airtime because the winners also keep their windows small. The voice TXOP holds **9 frames of 144 µs** (a 1,424 µs burst) and the video TXOP **9 of 292 µs** (2,756 µs).

**Answer D.**

```python
# ans_d.py: aggregation: the smallest aggregate for a share of the PHY rate, and the loss rate where A-MSDU stops paying
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import phy
SIFS, SLOT, DIFS = 16, 9, 34
HDR = 54
def ppdu_he(nb, mcs, mhz): return 43.2 + 13.6 * math.ceil((16 + 8 * nb + 6) / phy.ndbps('ax', mcs, mhz))
def ba(): return 20 + 4 * math.ceil((16 + 8 * 36 + 6) / 96)
def ex(nb, mcs=11, mhz=80): return ppdu_he(nb, mcs, mhz) + SIFS + ba() + DIFS + 7.5 * SLOT
def ampdu_bytes(n, pkt): return n * (4 + (HDR + pkt + 3) // 4 * 4)
def amsdu_bytes(n, pkt): return HDR + n * ((14 + pkt + 3) // 4 * 4)
print('smallest A-MPDU (1500 byte packets) that reaches a share of the PHY rate, HE one stream, GI 0.8 us')
for mhz, mcs in ((20, 7), (80, 11), (160, 11)):
    row = []
    for share in (0.5, 0.7, 0.8, 0.9):
        n = 1
        while n * 12000 / ex(ampdu_bytes(n, 1500), mcs, mhz) < share * phy.rate('ax', mcs, mhz) and n < 5000: n += 1
        row.append('%d' % n)
    print('  HE %3d MHz MCS %2d: 50 %% %s, 70 %% %s, 80 %% %s, 90 %% %s MPDUs (window 256)' % (mhz, mcs, *row))
print('goodput of 32 packets in A-MSDU and in A-MPDU at HE 80 MHz MCS 11 against the loss probability of one packet (byte errors independent)')
for pkt in (100, 300, 1500):
    cross = None
    for per_x in range(0, 1000):
        per = per_x / 10000
        e = 1 - (1 - per) ** (1 / (pkt + HDR)) if per else 0
        ga = 32 * (1 - per) * 8 * pkt / ex(ampdu_bytes(32, pkt))
        gs = 8 * 32 * pkt * (1 - e) ** amsdu_bytes(32, pkt) / ex(amsdu_bytes(32, pkt))
        if gs < ga and cross is None: cross = (per, ga, gs)
    g0a = 32 * 8 * pkt / ex(ampdu_bytes(32, pkt)); g0s = 8 * 32 * pkt / ex(amsdu_bytes(32, pkt))
    print('  %4d byte packets: clean link A-MPDU %.1f, A-MSDU %.1f Mbit/s; A-MSDU falls below A-MPDU at a packet loss probability of %s' % (pkt, g0a, g0s, '%.4f (A-MPDU %.1f against A-MSDU %.1f)' % cross if cross else 'above 10 %'))
```

smallest A-MPDU (1500 byte packets) that reaches a share of the PHY rate, HE one stream, GI 0.8 us
  HE  20 MHz MCS  7: 50 % 2, 70 % 4, 80 % 7, 90 % 22 MPDUs (window 256)
  HE  80 MHz MCS 11: 50 % 11, 70 % 26, 80 % 49, 90 % 140 MPDUs (window 256)
  HE 160 MHz MCS 11: 50 % 22, 70 % 51, 80 % 98, 90 % 280 MPDUs (window 256)
goodput of 32 packets in A-MSDU and in A-MPDU at HE 80 MHz MCS 11 against the loss probability of one packet (byte errors independent)
   100 byte packets: clean link A-MPDU 92.0, A-MSDU 102.0 Mbit/s; A-MSDU falls below A-MPDU at a packet loss probability of 0.0044 (A-MPDU 91.6 against A-MSDU 91.5)
   300 byte packets: clean link A-MPDU 213.4, A-MSDU 230.8 Mbit/s; A-MSDU falls below A-MPDU at a packet loss probability of 0.0029 (A-MPDU 212.8 against A-MSDU 212.4)
  1500 byte packets: clean link A-MPDU 444.9, A-MSDU 452.0 Mbit/s; A-MSDU falls below A-MPDU at a packet loss probability of 0.0006 (A-MPDU 444.6 against A-MSDU 443.6)

The faster the PHY, the more MPDUs are needed: 90 % of the PHY rate needs **22** MPDUs at HE 20 MHz MCS 7, **140** at 80 MHz MCS 11 and **280** at 160 MHz MCS 11, and the last is above the **256** window, so at 160 MHz 90 % is not reachable with one stream and a window of 256 (the 256 of section 5 gives **89.5 %**). The A-MSDU loses to the A-MPDU at a packet loss probability of **0.0044 for 100 byte packets, 0.0029 for 300 and 0.0006 for 1500**: small packets tolerate more loss before the all or nothing aggregate hurts, and for 1500 byte packets **0.06 %** is enough. On a clean link the A-MSDU is ahead by **10 Mbit/s** for 100 byte packets (102.0 against 92.0), **17.4** for 300 bytes and **7.1** for 1500.

**Answer E.**

```python
# ans_e.py: power save: a TIM, a listen interval for a latency target, and a TWT agreement for a sensor
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
def tim(aids):
    bm = bytearray(251)
    for a in aids: bm[a // 8] |= 1 << (a % 8)
    first = min(a // 8 for a in aids); last = max(a // 8 for a in aids); n1 = first & ~1
    return n1, bytes(bm[n1:last + 1])
n1, pv = tim([3, 9, 70, 71])
print('TIM for AIDs 3, 9, 70, 71: bitmap control offset N1 = %d (field %d), partial virtual bitmap %s (%d bytes), element 3 + %d = %d bytes' % (n1, n1 // 2, pv.hex(), len(pv), len(pv), 3 + len(pv)))
BI = 102.4; I_SLEEP, I_IDLE = 0.3, 50.0; AWAKE = 2.4
print('latency target 300 ms for unicast (assumed currents: sleep 0.3 mA, awake 50 mA, 2.4 ms awake per wake):')
for listen in (1, 2, 3, 4):
    worst = listen * BI; w = 1000 / (BI * listen); ms = w * AWAKE
    ma = (ms * I_IDLE + (1000 - ms) * I_SLEEP) / 1000
    print('  listen interval %d: worst wait %.1f ms %s, wakes %.2f per s, average %.2f mA' % (listen, worst, 'fits' if worst <= 300 else 'too long', w, ma))
print('a sensor sends 100 bytes every 60 s and tolerates a downlink wait of 10 s: TWT interval = mantissa x 2^exponent us')
want = 10e6
e = 0
while want / 2 ** e > 65535: e += 1
m = round(want / 2 ** e)
iv = m * 2 ** e
dur = 16 * 256
share = dur / iv
cell = 220.0
ma = share * 50 + (1 - share) * 0.3
print('  exponent %d, mantissa %d: interval %.4f s; wake duration 16 x 256 = %d us; awake %.4f %%; average %.3f mA; a 220 mAh coin cell lasts %.0f days on this alone (ignoring the uplink sends and self discharge)' % (e, m, iv / 1e6, dur, 100 * share, ma, cell / ma / 24))
ul = 60.0
ma2 = ma + (3.0 * 250 / 1000) / ul
print('  adding one 3 ms uplink send every 60 s at 250 mA adds %.3f mA: %.3f mA in total, %.0f days' % (3.0 * 250 / 1000 / ul, ma2, cell / ma2 / 24))
```

TIM for AIDs 3, 9, 70, 71: bitmap control offset N1 = 0 (field 0), partial virtual bitmap 0802000000000000c0 (9 bytes), element 3 + 9 = 12 bytes
latency target 300 ms for unicast (assumed currents: sleep 0.3 mA, awake 50 mA, 2.4 ms awake per wake):
  listen interval 1: worst wait 102.4 ms fits, wakes 9.77 per s, average 1.46 mA
  listen interval 2: worst wait 204.8 ms fits, wakes 4.88 per s, average 0.88 mA
  listen interval 3: worst wait 307.2 ms too long, wakes 3.26 per s, average 0.69 mA
  listen interval 4: worst wait 409.6 ms too long, wakes 2.44 per s, average 0.59 mA
a sensor sends 100 bytes every 60 s and tolerates a downlink wait of 10 s: TWT interval = mantissa x 2^exponent us
  exponent 8, mantissa 39062: interval 9.9999 s; wake duration 16 x 256 = 4096 us; awake 0.0410 %; average 0.320 mA; a 220 mAh coin cell lasts 29 days on this alone (ignoring the uplink sends and self discharge)
  adding one 3 ms uplink send every 60 s at 250 mA adds 0.013 mA: 0.333 mA in total, 28 days

The TIM for AIDs 3, 9, 70 and 71 spans bytes 0 to 8, **9 bytes**, **12 in all**; the bits for 70 and 71 sit in byte 8 (`0xc0`) and the gap in between costs six zero bytes. For a **300 ms** target **listen interval 2** is the longest that fits (worst wait **204.8 ms**, **0.88 mA**), and **3** (307.2 ms) does not. The sensor needs an interval below 10 s: exponent 8 and mantissa **39,062** give **9.9999 s**, with a 4,096 µs wake duration (**0.041 %** awake) and **0.320 mA**; a 220 mAh cell lasts **29 days** on that alone, and **28** with an uplink send of 3 ms at 250 mA every minute. The lesson is that **the sleep current dominates**: 0.3 mA of the 0.32 is the assumed doze current, so the interval hardly matters past a few seconds, and the real design variable is the doze current of the chip.

## 14. Cheat sheet

| Fact | Value |
|---|---|
| First byte of frame control | `(subtype << 4) or (type << 2) or version`; beacon 0x80, RTS 0xb4, CTS 0xc4, ACK 0xd4, QoS data 0x88 |
| Types | 0 management, 1 control, 2 data |
| Addresses | ToDS/FromDS 0/0 DA SA BSSID; 1/0 BSSID SA DA; 0/1 DA BSSID SA; 1/1 RA TA DA SA |
| Overhead per QoS data frame | 54 bytes (26 header, 16 CCMP, 8 LLC/SNAP, 4 FCS); ACK 14, RTS 20, CTS 14, block ack 32 |
| OFDM timing | slot 9, SIFS 16, PIFS 25, DIFS 34, EIFS 94 µs (11b: 20, 10, 30, 50, 364) |
| Beacon interval | 100 TU = 102.4 ms |
| DCF, 1500 bytes at 54 Mbit/s | 393.5 µs, 30.5 Mbit/s (56.5 %); 23.5 Mbit/s at 50 stations |
| Contention window | 15 doubling to 1,023 (mean 7.5 slots to 511.5) |
| NAV | duration field, 15 bits; RTS 3 SIFS + CTS + data + ACK; CTS RTS minus SIFS minus CTS |
| RTS/CTS | costs 88 µs (21.3 % of 414 µs); wins from about 5 hidden stations |
| Protection | CTS-to-self 314 µs at 1 Mbit/s, 60 at 6, 44 at 24 Mbit/s |
| UP to AC | 1,2 BK; 0,3 BE; 4,5 VI; 6,7 VO; EF 46 is VI naive, VO by RFC 8325 |
| EDCA defaults (from memory) | BK 15/1023/7, BE 15/1023/3, VI 7/15/2 (3,008 µs), VO 3/7/2 (1,504 µs) |
| Voice upper bound | 22, 36, 52, 67 calls at 6, 12, 24, 54 Mbit/s; a 6 Mbit/s call costs 3.1 times a 54 one |
| Aggregation | HE 80 MHz MCS 11: 9.1 % (1 MPDU), 83.8 % (64), 92.6 % (256) |
| A-MPDU against A-MSDU | A-MSDU all or nothing: 91.6 against 424.6 Mbit/s at PER 0.05 |
| Block ack window | 64 (HT, VHT), 256 (HE), 1,024 (EHT); bitmap 8, 32, 128 bytes |
| TIM | 4 to 6 bytes with few clients; worst 251 byte bitmap |
| Power save | PS-Poll at most 9.8 frames/s; U-APSD 20 ms bound; TWT interval mantissa x 2^exponent µs |
| Performance anomaly | 3 at 54 plus 1 at 6 Mbit/s: 14.1 against 24.2 Mbit/s with airtime fairness |
| Scan | 8.6 s all bands passive 6 GHz; 4.1 s with 15 preferred channels |
| Join | 6 frames, 1.6 ms of airtime |
| Reason codes | 6 class 2 from state 1; 7 class 3 from state 1 or 2 |
| HT | A-MPDU exponent 3 = 65,535 bytes; capabilities element 26 bytes; four protection modes |

## Sources

- Candela Technologies, *Wi-Fi training course*, sessions 3a to 3d (the MAC, frame formats and exchanges, DCF and EDCA, power save): <https://www.candelatech.com/course.php>.
- IEEE Std 802.11 (the standard with the 802.11e, n, ac, ax and be amendments): the frame type tables, the EDCA defaults, the retry limits, the class lists, the block ack and aggregate maxima and the HT elements, all **from memory** and not re-read for this tutorial.
- G. Bianchi, *Performance analysis of the IEEE 802.11 distributed coordination function*, IEEE JSAC 2000: the saturation model used to check section 2 (from memory of the model, implemented here).
- RFC 8325, *Mapping Diffserv to IEEE 802.11* (the DSCP to UP table, from memory).
- M. Heusse and others, *Performance anomaly of 802.11b*, INFOCOM 2003 (the anomaly of section 7, from memory; the numbers here are computed from the model).
- CWNP, CWAP and CWSP study guides and the mrncciew.com study material index, used as the syllabus checklist for the MAC sublayer and frame exchanges (the index gives topics, not content).
- Tools used here: Python 3 with `numpy` and `scipy`, `scapy` 2.5.0 (to build the radiotap frames) and `tshark` 4.2.2 (the independent frame decoder).
