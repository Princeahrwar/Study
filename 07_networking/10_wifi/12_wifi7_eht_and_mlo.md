# WF 12: Wi-Fi 7 (802.11be, EHT) in depth: the PHY, multi-RU and puncturing, and multi-link operation

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela session 5d (Wi-Fi 7) and the material beyond it, taken from the mrncciew Wi-Fi 7 notes, the IEEE 802.11be draft and amendment, and a published ns-3 comparison of MLO modes. Builds on [WF 02](02_wifi_phy.md) (OFDM, MCS tables), [WF 03](03_wifi_mac.md) (EDCA, aggregation), [WF 04](04_wifi_security.md) (WPA3, PMF) and [WF 06](06_wifi6_6e_7_features.md) (the first look at 6 GHz, OFDMA and MLO).*

Wi-Fi 7 raises the peak rate by widening the channel to 320 MHz, adding 4096-QAM and allowing a channel with holes in it. The change that matters for an engineer is different: **multi-link operation** turns one device into a *multi-link device* (MLD) that holds one association and one set of keys across several radios and bands. That moves the unit of the connection from the radio to the MLD, and it changes beacons, association, power save, roaming, captures and debugging. Most of the failures people meet on real Wi-Fi 7 gear are about which MLO mode a client really runs, not about the peak rate.

**Eight honest limits, stated first.**

1. **No radio and no Wi-Fi 7 hardware here.** Every frame is built byte by byte and decoded by `tshark` 4.2; every throughput is a model. Nothing was transmitted.
2. **`tshark` 4.2 decodes the 802.11be D3.0 draft layout** (it labels the elements "802.11be D3.0"). The final amendment moved some fields and added others, so a decode agreeing with my bytes proves the layout of that draft, not the published standard. Field positions I rely on are marked; verify them against IEEE 802.11be-2024 and a current Wireshark.
3. **Standard text is from memory.** The values I give for eMLSR padding and transition delays, the allowed multi-RU combinations, the transmitter error limits, the beacon protection bit and the action frame numbers are from memory of the amendment, or from the mrncciew articles, and are marked.
4. **The mrncciew MLO pages were only partly readable.** I read the Wi-Fi 7 beacon article and the Apple N1 article in full. The page titled "Wi-Fi 7 Notes: MLO and Beyond" returned only the site index, so the articles it links (the MLO association deep dive, eMLSR in action, how to verify MLO) were **not read**, and I use only their titles as a syllabus.
5. **The mode model is mine.** It uses independent busy periods on two links, a fixed MAC efficiency of 0.65 and a 4 ms burst; it shows the shape of the trade, not what a chip does. The published ns-3 figures I cite are that paper's, not reproduced here.
6. **Client behaviour is from one article.** What I say about the Apple N1 (a MacBook Pro M5 behaving like MLSR in one test) is one author's single capture on one AP model; chip, firmware and AP change it.
7. **Wi-Fi 8 is not here.** Multi-AP coordination and the next amendment are in WF 13; distributed MLO and anything that needs AP to AP coordination is marked as outside this tutorial.
8. **No power measurement.** The power save rules are described; no energy figure was measured.

By the end you can:

- **compute any 802.11be rate** from one formula and say what each change buys (computed: the peak, 320 MHz, MCS 13, 0.8 us guard interval, 16 streams, is **46,117.6 Mbit/s**; one stream is **2,882.4**; at the same width and streams 4096-QAM is **x1.20** over Wi-Fi 6's 1024-QAM, and needs **6.03 dB** more SNR at a symbol error rate of 1e-3, **42.17 dB** against **36.14 dB**).
- **say what preamble puncturing and multi-RU do** and when they pay (modelled, exact over all 2^15 busy patterns: with each of 15 secondary 20 MHz subchannels busy 10 percent of the time a 320 MHz channel without puncturing uses a mean of **138 MHz**, with puncturing outside the primary 80 MHz **225 MHz**, and with any pattern **290 MHz**, so **x1.62** and **x2.10** in throughput).
- **read a multi-link element byte by byte** (built and decoded here: a beacon of **129** bytes, an association request of **97**, a response of **100**, a second request of **97** from a client that reports no EMLSR and 0 simultaneous links, a TID-to-link mapping request of **48** and EML notifications of **30** and **28** bytes, all decoded by `tshark` to the same MLD address, per-link profiles, NSTR bitmap, EMLSR bit and mapping).
- **separate the MLO modes** (SLO, MLSR, eMLSR, eMLMR, NSTR, STR) by what each can transmit and receive at once, and say what a capture shows about the mode a client runs.
- **estimate the cost of eMLSR switching** (computed: a 60 us initial control frame plus a 128 us transition delay is **4.7 percent** of a 4 ms burst and **7.9 percent** with a 256 us delay).
- **check whether MLO is really active** from the association exchange, the EML notifications and the traffic, and explain why a client that advertises MLO still delivers one link's rate.
- **debug a Wi-Fi 7 deployment** with the table of section 12.

## Where the numbers come from

Each experiment is a script `sN_*.py` in `prog/` with its output frozen in `v/`, run with `run.sh`. `mlo.py` builds the EHT elements and the multi-link element byte by byte; `dot11.py` is the same frame builder as in the earlier tutorials; `tshark` decodes the pcap files they write. The rate, puncturing and mode scripts are formulas and models; the frame scripts are built bytes and a real decoder.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, simulated, modelled or computed here is marked **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| EHT rate table, SNR for each QAM (`s1_rates.py`) | a table of 14 MCS by 5 widths, four root searches, under 1 s |
| MLO frames and decode (`s2_frames.py`) | seven frames, eleven `tshark` runs, about 3 s |
| Link mode model (`s3_modes.py`) | closed form, 7 load cases and 6 delays, under 1 s |
| Puncturing (`s4_puncture.py`) | 2^15 patterns for each of 5 busy probabilities, about 3 s |
| Problems (`ans_a` to `ans_e`) | each under 1 s; `ans_b` runs `tshark` once |

## Mapping to the Candela course

Candela session 5d (Wi-Fi 7) is sections 1 to 3 and the first half of 4. Everything on multi-link frames, the modes, verification and debugging (sections 4 to 12) is the gap the course leaves, filled from the mrncciew notes and the amendment.

---


## 1. What changed from Wi-Fi 6 to Wi-Fi 7

| Feature | Wi-Fi 6 (802.11ax, HE) | Wi-Fi 7 (802.11be, EHT) | Where in this tutorial |
|---|---|---|---|
| Widest channel | 160 MHz | 320 MHz, 6 GHz only | 2 |
| Highest modulation | 1024-QAM (MCS 11) | 4096-QAM (MCS 12 and 13) | 2 |
| Spatial streams | 8 | 16 | 2 |
| Peak PHY rate | 9,607.8 Mbit/s (160 MHz, 8 streams, MCS 11, 0.8 us) | 46,117.6 Mbit/s (computed, section 2) | 2 |
| Resource units | one RU per user | a user may hold a combination (multi-RU) | 3 |
| Holes in the channel | limited (from memory) | preamble puncturing in the standard PPDU formats | 3 |
| Association | per radio, per band | one MLD association over several links | 4, 5 |
| Using several bands at once | no (a client is on one band) | multi-link operation, with modes | 6 |
| Traffic placement | not applicable | TID-to-link mapping | 7 |
| Preamble | HE-SIG-A and B | U-SIG (version independent) and EHT-SIG | 2 |
| Power save | TWT | TWT, restricted TWT, per-link and EML power rules | 8 |

**What the headline figures do not say.** The peak assumes 16 streams, which is eight times the two or three a phone or laptop has, a 320 MHz channel that exists only where a 6 GHz regulator allows it, and an SNR near 42 dB (section 2). A shipping 2x2 client gets **1,441 Mbit/s at 80 MHz, 2,882 at 160 MHz and 5,765 at 320 MHz** from the same formula, and section 11 shows what is left after the MAC.

## 2. The EHT PHY

### 2.1 One formula for every rate

An OFDM PHY rate is the data subcarriers times the bits per subcarrier times the coding rate times the spatial streams, divided by the symbol time (12.8 us plus the guard interval). The data subcarrier counts for 20, 40, 80, 160 and 320 MHz are 234, 468, 980, 1,960 and 3,920 (the 320 MHz channel is four 996-tone units; the counts are from memory of the tone plans and match the HE counts below 320 MHz). The WF 02 tables for HT, VHT and HE come from the same formula.

```
--- the formula: rate = data subcarriers x bits per subcarrier x coding rate x streams / (12.8 us + guard interval); data subcarriers per 20, 40, 80, 160, 320 MHz: 234, 468, 980, 1960, 3920
--- one stream, 0.8 us guard interval, Mbit/s
MCS  mod       rate       20      40      80     160     320
  0  BPSK      1/2       8.6    17.2    36.0    72.1   144.1
  1  QPSK      1/2      17.2    34.4    72.1   144.1   288.2
  2  QPSK      3/4      25.8    51.6   108.1   216.2   432.4
  3  16-QAM    1/2      34.4    68.8   144.1   288.2   576.5
  4  16-QAM    3/4      51.6   103.2   216.2   432.4   864.7
  5  64-QAM    2/3      68.8   137.6   288.2   576.5  1152.9
  6  64-QAM    3/4      77.4   154.9   324.3   648.5  1297.1
  7  64-QAM    5/6      86.0   172.1   360.3   720.6  1441.2
  8  256-QAM   3/4     103.2   206.5   432.4   864.7  1729.4
  9  256-QAM   5/6     114.7   229.4   480.4   960.8  1921.6
 10  1024-QAM  3/4     129.0   258.1   540.4  1080.9  2161.8
 11  1024-QAM  5/6     143.4   286.8   600.5  1201.0  2402.0
 12  4096-QAM  3/4     154.9   309.7   648.5  1297.1  2594.1
 13  4096-QAM  5/6     172.1   344.1   720.6  1441.2  2882.4
```

The two rows that matter are MCS 11 (1024-QAM, 5/6, the Wi-Fi 6 top) and MCS 13 (4096-QAM, 5/6, the Wi-Fi 7 top). MCS 12 and 13 are new. MCS 14 and 15 are low-rate modes used with the duplicated (EHT-DUP) format in 6 GHz and for range; I do not tabulate them (from memory).

```
--- the peak: 320 MHz, MCS 13, 0.8 us guard interval, 16 streams: 46117.6 Mbit/s (46.12 Gbit/s); one stream 2882.4
```

```
--- what shipping hardware gets (the same formula, 2 spatial streams): 80 MHz MCS 13 1441, 160 MHz MCS 13 2882, 320 MHz MCS 13 5765 Mbit/s
```

```
--- against Wi-Fi 6 (1024-QAM, 5/6, 160 MHz, 2 streams, 0.8 us): 2402 Mbit/s; Wi-Fi 7 at the same width and streams: 2882 Mbit/s (x1.20); the whole gain is 4096-QAM (12 bits against 10, x1.20), since the subcarrier layout is the same
```

The whole gain of Wi-Fi 7 over Wi-Fi 6 at the same width and streams is the **x1.20** of 4096-QAM (12 bits against 10); the larger gains come from the 320 MHz channel (twice the subcarriers) and from more streams, neither of which a phone has.

### 2.2 What 4096-QAM costs

A denser constellation needs a cleaner signal. With Gray coded square QAM in white noise, the SNR for a symbol error rate of 1e-3 is a closed form; each step of two bits per symbol costs about 6 dB.

```
--- the SNR that a constellation needs (modelled: Gray coded square QAM in AWGN, symbol error rate 1e-3)
    64-QAM: 23.97 dB
   256-QAM: 30.08 dB
  1024-QAM: 36.14 dB
  4096-QAM: 42.17 dB
  each step up (256 to 1024, 1024 to 4096) costs 6.06 dB and 6.03 dB; 4096-QAM over 1024-QAM is 6.03 dB more SNR for 20 percent more bits
```

```
--- the transmitter limit that goes with it (from memory of the standard, check the final text): relative constellation error -35 dB for 1024-QAM MCS 10 and 11, -38 dB for 4096-QAM MCS 12 and 13; a 3.0 dB step
```

```
--- distance: free space path loss grows 6.02 dB per doubling of distance, so 6.03 dB of extra SNR is a distance factor of 2.00 (1 over 10^(dB/20))
```

Two consequences. First, **4096-QAM is a short-range feature**: with a path loss exponent of 3.5 (a typical indoor figure) the 6.03 dB is a distance factor of about 0.67, so a 1024-QAM edge at 20 m puts the 4096-QAM edge near 13.5 m (computed in section 16, problem E). Second, the transmitter must be cleaner: the error vector magnitude limit tightens from -35 dB to -38 dB (from memory; check the final text), which pushes power amplifier back-off up and is part of why a Wi-Fi 7 AP is not a Wi-Fi 6 AP with a new label.

### 2.3 The PPDU and what is new in it

An EHT PPDU is a legacy preamble (L-STF, L-LTF, L-SIG, then RL-SIG repeating L-SIG so a Wi-Fi 6 or Wi-Fi 7 receiver can tell its generation) followed by **U-SIG** (universal signal) and, for the multi-user format, **EHT-SIG**, then EHT-STF, EHT-LTF, the data and an optional packet extension (from memory of the PPDU layout). U-SIG is the part that is *version independent*: it carries the PHY version, the bandwidth, the uplink or downlink bit, the BSS colour and the TXOP duration, and it is laid out so that later amendments can add their own signal fields after it. That is why Wi-Fi 8 can reuse the front of the preamble.

The four PPDU formats are EHT MU (downlink to one or more users, with EHT-SIG), EHT TB (the trigger-based uplink answer), EHT ER SU (extended range, one user, boosted preamble) and the plain SU case inside EHT MU. The `L-SIG` length rule of WF 02 still bounds the PPDU: the longest is **5.484 ms**, which matters for the eMLSR cost in section 6.

## 3. Multi-RU and preamble puncturing

### 3.1 The problem puncturing solves

An 802.11ac or HE transmitter picks a channel width from the 20 MHz subchannels that were idle for the last PIFS before the transmission, and the usable channel must be contiguous and aligned around the primary. If the third 20 MHz subchannel of an 80 MHz channel is busy, the transmission falls back to 40 MHz, even though two of the four subchannels are free. **Preamble puncturing** lets the PPDU skip a busy subchannel and use the rest, and the preamble says which subchannels are punctured so the receiver does not try to decode them.

```
--- one busy subchannel on an 80 MHz channel (4 subchannels), by which one is busy: bandwidth without and with puncturing
  subchannel 1 busy: aligned rule 20 MHz, puncturing 60 MHz (200 percent more)
  subchannel 2 busy: aligned rule 40 MHz, puncturing 60 MHz (50 percent more)
  subchannel 3 busy: aligned rule 40 MHz, puncturing 60 MHz (50 percent more)
```

On 80 MHz one busy 20 MHz subchannel leaves 60 MHz, which is **3.00 times** the 20 MHz of the aligned rule when the second subchannel is busy and **1.50 times** the 40 MHz when one of the other two is busy (computed in problem D, section 16, at 2 streams and MCS 11: 287 and 574 against 860 Mbit/s).

### 3.2 Multi-RU: how a user gets a punctured channel

In Wi-Fi 6 a user gets one resource unit. A 60 MHz channel is not one RU: it is a 484-tone unit plus a 242-tone unit, and Wi-Fi 7 lets one user hold such a **combination** (multi-RU). The combinations I remember from the amendment, in tones: 52+26 and 106+26 inside a 20 MHz subchannel; 484+242 in an 80 MHz channel with one 20 MHz hole; 996+484 and 996+484+242 in 160 and 240 MHz layouts; 3x996 in 320 MHz with an 80 MHz hole; and 2x996+484 with a 40 MHz hole (from memory; the final table has more entries and limits which combinations may be used for OFDMA users with small RUs). The hardware consequence is a larger set of allocations for the scheduler and the channel estimator.

### 3.3 How much it is worth

The model below fills the 320 MHz channel with busy subchannels at random and compares three policies. *No puncturing* uses the widest aligned free block containing the primary. *Punctured outside the primary 80* is my model of a conservative rule: a hole is allowed only outside the primary 80 MHz; if one of the first four subchannels is busy the aligned rule applies. *Any pattern* is an upper bound: every free subchannel is used. The standard allows only some patterns (from memory), so the real result lies between the last two columns and the policy is mine, not the standard's table.

```
--- expected 320 MHz throughput (one stream, MCS 13 rate prorated by bandwidth used), exact over all 2^15 busy patterns of the 15 secondary subchannels; the primary is always free
p(busy)  no puncturing  punctured, outside primary 80 only  punctured, any  gain (outside 80)  gain (any)  mean bandwidth MHz (none / outside / any)
   0.02           2386                                2690            2828               1.13        1.19  265 / 299 / 314
   0.05           1831                                2421            2747               1.32        1.50  203 / 269 / 305
   0.10           1246                                2023            2612               1.62        2.10  138 / 225 / 290
   0.20            711                                1394            2342               1.96        3.30  79 / 155 / 260
   0.30            496                                 949            2072               1.91        4.18  55 / 105 / 230
```

At 10 percent busy per subchannel the mean bandwidth used is **138 MHz without puncturing, 225 MHz with the conservative rule and 290 MHz with any pattern**, a throughput gain of **x1.62 to x2.10**. At 2 percent the gain is **x1.13 to x1.19**; the benefit grows with the fraction of busy subchannels, which is why puncturing is a feature for crowded apartment buildings and 6 GHz channels near a radar or an incumbent. The rate is prorated by bandwidth in the model; a real punctured PPDU also pays a longer preamble signalling field and a lower per-RU efficiency, so these are upper figures.


## 4. The multi-link device

### 4.1 Vocabulary

An **MLD** is one logical device with one MAC service interface to the layers above and several **affiliated** stations, each a full 802.11 station on its own link (its own radio, band and channel). An **AP MLD** has affiliated APs; a **non-AP MLD** (a phone or laptop) has affiliated STAs. Each affiliated station has its **own link MAC address**, which is the address on the air (receiver, transmitter and BSSID fields), and the MLD as a whole has an **MLD MAC address**, which the upper layers and the key hierarchy see. A link is identified by a **link ID** (a 4 bit field, so at most 15 values, from memory).

| Thing | Per MLD (shared) | Per link (separate) |
|---|---|---|
| Address | MLD MAC address | link MAC address, BSSID |
| Association | one, set up on one link and covering all that are enabled | the link's BSS: beacons, capabilities, channel |
| Pairwise keys (PTK) and the 4-way handshake | one handshake, one key set for the MLD (from memory) | none |
| Group keys | per link (each BSS sends its own group traffic; from memory) | yes |
| Sequence numbers and block ack scoreboard | one per TID across the links, so frames can be sent on any link and reordered at the receiver (from memory) | none |
| EDCA and the NAV | none | each link contends for its own channel |
| Power state | the MLD has one state per link and rules for the whole (section 8) | per link |

The one fact to hold: **an MLD joins a network once, then each link is a separate contention domain**. Everything that is channel access stays per link; everything that is identity, security and ordering is per MLD.

### 4.2 A discrepancy to check in your own capture

The mrncciew beacon article says that because MLO uses a common MAC address across bands, the EHT beacons for an SSID come from one BSSID rather than a different one per radio. My reading from memory of the amendment is different: **each affiliated AP keeps its own BSSID (its link address) and the multi-link element carries the common MLD address**, so a three-band AP MLD shows three BSSIDs and one MLD address. Either reading may match a given vendor, and the article may describe a vendor choice or the summary I read may compress it. I built the frames here with distinct link addresses (`02:aa:00:00:00:02` and `...03`) and one MLD address (`02:aa:00:00:00:01`); the decode agrees with my construction and does not settle the question for a real AP. Count BSSIDs and the MLD address in your capture before relying on either.

## 5. The frames that make a multi-link association

### 5.1 The elements in a beacon

The EHT beacon carries, in addition to HT, VHT and HE (and only HE and EHT on 6 GHz, per the mrncciew article), the **EHT capabilities** (a MAC part, a PHY part, then the supported MCS and stream map per width), the **EHT operation** element (an operation parameters byte, the **basic EHT-MCS and NSS set**, and, if the information bit is set, a control byte with the channel width and two centre frequency segments, plus an optional disabled-subchannel bitmap for puncturing), the **multi-link element**, a **multi-link traffic indication** (a bitmap of which links have buffered frames) and the **TID-to-link mapping** element. Two details from the article worth keeping: the *basic EHT-MCS and NSS set* in the operation element is the minimum every EHT client must support in that BSS (in its 80 MHz capture, MCS 0 to 9 with one stream), and **beacon protection** is signalled by bit 84 of the extended capabilities element, which Wi-Fi 7 requires an AP to support.

### 5.2 The multi-link element, byte by byte

The element is an extension element (ID 255, extension ID 107). It has a 2 byte **control** field whose low 3 bits are the type (0 Basic, used in beacons, probe responses, association and reassociation; the other types cover probe requests, reconfiguration, TDLS and priority access) and whose next bits say which optional fields are in the **common info**. The common info starts with its own length, then the MLD address and the optional fields (link ID, BSS parameters change count, medium synchronisation delay, EML capabilities, MLD capabilities). After the common info come **per-STA profile subelements**, one per other link, each with a 2 byte STA control (link ID, a *complete profile* bit, a MAC address bit, beacon interval, TSF offset and DTIM bits, an *NSTR link pair* bit and the NSTR bitmap size) and the STA info.

The builder is 13 lines:

```python
def ml_basic(mld, links, link_id=0, bss_chg=1, eml=None, mldcap=None, medium_sync=None):
    pres = 0x10 | 0x20                      # link id info, BSS params change count
    common = mac(mld) + bytes([link_id]) + bytes([bss_chg])
    if medium_sync is not None: pres |= 0x40; common += struct.pack('<H', medium_sync)
    if eml is not None: pres |= 0x80; common += struct.pack('<H', eml)
    if mldcap is not None: pres |= 0x100; common += struct.pack('<H', mldcap)
    body = struct.pack('<H', 0 | pres) + bytes([len(common) + 1]) + common
    for (lid, addr, nstr) in links:
        ctrl_w = lid | (1 << 5)
        info = mac(addr)
        prof = struct.pack('<H', ctrl_w) + bytes([len(info) + 1]) + info
        body += bytes([0, len(prof)]) + prof
    return ext(107, body)
```

`ans_b.py` parses the same bytes by hand and then asks `tshark`; the two agree on every field:

```
--- the element body: b0010d02aa00000001000121000100000921000702aa00000002000922000702aa00000003
control 0x01b0: type 0 (0 is Basic); presence bits: link ID 1, BSS change count 1, medium sync 0, EML capabilities 1, MLD capabilities 1
common info length 13: MLD address 02:aa:00:00:00:01, link ID 0, BSS parameters change count 1, EML capabilities 0x0021 (bit 0 is EMLSR support: 1), MLD capabilities 0x0001 (low 4 bits, maximum simultaneous links: 1)
per-STA profile (subelement 0, 9 bytes): link ID 1, complete 0, MAC present 1; STA info length 7, MAC 02:aa:00:00:00:02
per-STA profile (subelement 0, 9 bytes): link ID 2, complete 0, MAC present 1; STA info length 7, MAC 02:aa:00:00:00:03
--- tshark on the same bytes (MLD address, EMLSR support, maximum simultaneous links): 02:aa:00:00:00:01, True, 1
```

### 5.3 The association exchange

The non-AP MLD sends **one** association request on one link. It contains the ordinary request for that link plus a multi-link element with its MLD address, its EML and MLD capabilities, and a per-STA profile for each additional link it wants. The AP MLD answers with one response that carries its MLD address and a per-STA profile for each link it accepts, with a status code per link (the response I built carries the profile list but not per-link status; from memory the standard puts a status code inside the complete profile). After the exchange the MLD has one association and the keys are set by a single 4-way handshake.

```
--- seven frames built here (sizes in bytes): beacon 129, association request 97, association response 100, TID-to-link mapping request 48, EML notification, EMLSR on 30, EML notification, EMLSR off 28, association request, MLSR style client 97
--- decoded by tshark, beacon: MLD address 02:aa:00:00:00:01, EMLSR support True, maximum simultaneous links field 1, NSTR link pair present False,False, NSTR bitmap -
--- decoded by tshark, association request: MLD address 02:bb:00:00:00:01, EMLSR support True, maximum simultaneous links field 1, NSTR link pair present True, NSTR bitmap 08
--- decoded by tshark, association response: MLD address 02:aa:00:00:00:01, EMLSR support True, maximum simultaneous links field 1, NSTR link pair present False, NSTR bitmap -
--- decoded by tshark, association request, MLSR style client: MLD address 02:bb:00:00:00:01, EMLSR support False, maximum simultaneous links field 0, NSTR link pair present True, NSTR bitmap 08
```

Read the lines for what they show. The beacon and the response carry the AP MLD address; the request carries the client MLD address; the **maximum simultaneous links** field is the number of links the device can use at the same time **minus one** (value 0 means one at a time, value 1 means two; this matches the article's N1 capture, where a field of 0 meant no simultaneous multi-link use); and the NSTR bitmap `08` in the request marks the pair the client cannot run in STR mode. The seventh frame is a client of the kind the N1 article describes: no EMLSR support, 0 in the simultaneous links field, and an NSTR pair.

### 5.4 The action frames

Two **Protected EHT** actions (category 37) manage a live association. The **TID-to-link mapping request** (action 0, from memory) carries one link bitmap per TID; the request I built allows TIDs 0 to 5 on links 0 and 1 and the two voice TIDs only on link 0. The **EML operating mode notification** (action 6, from memory) switches EMLSR or EMLMR on or off and says which links are enabled; the "off" frame is 28 bytes and the "on" frame with a link bitmap is 30.

```
--- the mapping request: Protected EHT action 0 (0 is TID-to-link mapping request); link bitmaps by TID 0 to 7: 0x0003 0x0003 0x0003 0x0003 0x0003 0x0003 0x0001 0x0001 (bit 0 is link 0, bit 1 is link 1)
    so TIDs 0 to 5 (best effort, background and video) may use links 0 and 1, and TIDs 6 and 7 (voice) only link 0
--- EML notification, EMLSR on: Protected EHT action 6 (6 is EML operating mode notification), EMLSR mode bit True, link bitmap 0x0003
--- EML notification, EMLSR off: Protected EHT action 6 (6 is EML operating mode notification), EMLSR mode bit False, link bitmap -
```

Both are *protected* action frames, so on a real network they are encrypted after the handshake and a sniffer without the keys sees only the category. The experiment frames are unprotected so `tshark` can decode them.

### 5.5 Filters

These field names decoded the frames above in `tshark` 4.2 (names move between versions):

| To find | Filter field |
|---|---|
| any multi-link element | `wlan.eht.multi_link.common_info.mld_mac_address` or `...ap_mld_mac_address` |
| client MLD capabilities | `wlan.eht.multi_link.common_info.mld_capabilities.max_simultaneous_links` |
| EMLSR support | `wlan.eht.multi_link.common_info.eml_capabilities.emlsr_support` |
| NSTR pair | `wlan.eht.multi_link.sta_profile.sta_control.nstr_link_pair_present` and `...sta_info.nstr_bitmap` |
| EML notifications | `wlan.eht.eml_control.emlsr_mode` and `wlan.eht.eml_control.link_bitmap` |
| TID-to-link mapping | `wlan.eht.tid_to_link_mapping.tid_0.link_mapping` to `tid_7` |

## 6. The MLO modes

### 6.1 What each mode can do

Two radios or one, filters or no filters, and who may talk at once: that is the whole difference. Defined from memory of the amendment and checked against the article and the paper where they say something:

| Mode | Links associated | Transmit and receive at once | What it needs |
|---|---|---|---|
| **SLO** (single link) | one | not applicable | nothing |
| **MLSR** (multi-link single radio) | several | one link at a time; switching between links is slow | one radio, no listening on others |
| **eMLSR** (enhanced) | several | the device **listens on every enabled link with one radio** and transmits or receives on one at a time; an *initial control frame* on a link makes it switch its full radio there | one radio, fast switching |
| **eMLMR** (enhanced multi-link multi-radio) | several | the radio chains can be combined to use more than one link for one exchange | rarely shipped (from memory) |
| **NSTR** (non-simultaneous transmit and receive) | several, on pairs that interfere | simultaneous only if both links start and end aligned; otherwise one direction at a time | two radios, no filters between them |
| **STR** (simultaneous transmit and receive) | several | each link independent | two radios with enough isolation and frequency separation |

STR needs the transmitter on one link not to desensitise the receiver on the other, which depends on the band pair: 2.4 GHz with 5 or 6 GHz is the least demanding pair; the low 5 GHz and the low 6 GHz channels are close in frequency and often form an NSTR pair (the mrncciew N1 article shows exactly this: an NSTR bitmap marking the 5 GHz and 6 GHz links). The AP's view is simpler: APs in the article support only MLMR-STR and eMLSR.

### 6.2 eMLSR in detail (from memory)

An eMLSR client keeps one receive chain on each enabled link. When the AP wants to send, it first sends an **initial control frame** (a MU-RTS trigger or a BSRP trigger) on one link. The client receives it, then switches the rest of its chains to that link; the AP must wait the client's advertised **transition delay** before sending data, and the control frame carries **padding** of at least the client's **padding delay** so the client can switch before the frame ends. After the exchange the client returns to listening on all links after the transition delay. The values are advertised in the EML capabilities field: padding delay 0, 32, 64, 128 or 256 us, transition delay 0, 16, 32, 64, 128 or 256 us (from memory). The constraint that matters for design is that **eMLSR never sends or receives on two links at once**: it buys contention diversity (take whichever link is free first), not rate.

### 6.3 What the model says

```
--- modelled downlink throughput in Mbit/s, link A 5 GHz 80 MHz (1201 PHY), link B 6 GHz 160 MHz (2402 PHY), MAC efficiency 0.65, 4 ms bursts
busy A  busy B   best single link   eMLSR (128 us)   NSTR (k 0.80)   STR      STR over single   eMLSR over single
  0.0     0.0              1561             1488            1874     2342             1.50               0.95
  0.2     0.0              1561             1488            1811     2186             1.40               0.95
  0.2     0.2              1249             1309            1574     1874             1.50               1.05
  0.4     0.2              1249             1280            1493     1717             1.38               1.02
  0.6     0.2              1249             1250            1411     1561             1.25               1.00
  0.6     0.6               625              774             862      937             1.50               1.24
  0.8     0.2              1249             1220            1330     1405             1.12               0.98
```

```
--- the eMLSR cost of switching: overhead per 4 ms burst (60 us initial control frame plus the transition delay), and the throughput of eMLSR with both links 20 percent busy
  transition delay   0 us: overhead 1.5 percent of the burst, eMLSR 1353 Mbit/s
  transition delay  16 us: overhead 1.9 percent of the burst, eMLSR 1348 Mbit/s
  transition delay  32 us: overhead 2.3 percent of the burst, eMLSR 1342 Mbit/s
  transition delay  64 us: overhead 3.1 percent of the burst, eMLSR 1331 Mbit/s
  transition delay 128 us: overhead 4.7 percent of the burst, eMLSR 1309 Mbit/s
  transition delay 256 us: overhead 7.9 percent of the burst, eMLSR 1265 Mbit/s
```

Read it in three steps. **STR** sums the two links: x1.50 over the best single link with both free, as the faster link carries two thirds of the total. **eMLSR** does not sum them; with both links idle it is at **0.95** of the best single link because of the control frame and the transition delay, and it pays off only when the faster link is busy: x1.05 with both links 20 percent busy and **x1.24** with both 60 percent busy, where the second link fills the gaps. **NSTR** sits between them because it can use both links only when they are free together (the 0.80 alignment efficiency is my assumption). The switching cost grows with the transition delay: 4.7 percent of a 4 ms burst at 128 us and 7.9 percent at 256 us, which is why bursts that are too short lose the benefit.

The published ns-3 paper on STR and EMLSR (arXiv 2501.04149) reports, in its base case of one AP and five saturated stations, about **120 Mbit/s for STR, 60 for EMLSR and 30 for a single link**, with STR ahead on queuing and access delay, and an added interferer on one link costing both modes about 10 Mbit/s. Those are that paper's simulation figures with its own link parameters; my model does not reproduce its 2:1 ratio of eMLSR over a single link, which depends on how the stations share the two links in its setup. The agreement is on the ordering: **STR above eMLSR above single link, with eMLSR the closest to single link when the links are lightly loaded.**

### 6.4 What a real client does

The mrncciew Apple N1 article tests a MacBook Pro (M5) on a WPA3-Enterprise tri-band SSID of a Cisco Catalyst 9800 controller with a CW1978 AP. The client's association request had a multi-link element with two profiles, and the profile for link 1 carried an NSTR bitmap of `0x08` marking the 5 GHz and 6 GHz links as non-STR. It used the 6 GHz and 2.4 GHz links at the same time **for uplink only**, never for downlink, and advertised **0 for maximum simultaneous links**. The author reads it as MLSR-like behaviour, and notes it is unclear whether the chip changes behaviour with traffic type or direction. This is one capture on one pair of devices. The consequence for a test plan is general: **a client advertising MLO is not a client using two links**, and the only way to know is to read the association (section 10) and then the traffic.

## 7. TID-to-link mapping and link management

By default every TID may use every enabled link. A **TID-to-link mapping** (TTLM) restricts that: the AP or the client sends the mapping element (section 5.4) per direction or both, and the other side accepts or rejects it if the MLD capabilities say negotiation is supported. Uses: keep latency-sensitive voice on the cleanest link (the example above keeps TIDs 6 and 7 on link 0), keep a power-limited link out of the bulk traffic, or push everything off a link that is about to be vacated. The AP can also advertise a TTLM in beacons, with a switch time so a client can prepare (the beacon elements of section 5.1).

Related management actions (from memory, check the amendment): **link reconfiguration** (adding or removing a link of the MLD without a new association, using the reconfiguration type of the multi-link element); **link recommendation** (the AP suggests which links a client should use); **multi-link traffic indication** in the TIM (so a client in power save on one link learns which links hold buffered frames); and the **BSS parameters change count** in the common info, which lets a client see that another link's beacon changed without reading it. Together these are the replacement for the single-BSS management of WF 05: roaming within an MLD can add and drop links, and a roam between AP MLDs is a reassociation of the whole MLD.


## 8. Power save, TWT and the other EHT MAC features

**Power save is per link and per MLD.** Each affiliated station has its own power state, so an MLD can doze one link and stay awake on another; the AP buffers frames per link and the multi-link traffic indication (section 7) tells a dozing link that another link holds data. With eMLSR the client listens on every enabled link at reduced capability, which costs listening power on each link; a client that wants to save power turns EMLSR off with the notification of section 5.4 (the 28 byte frame) and falls back to one link. **Target wake time** (WF 06) still schedules wake windows, per link; **restricted TWT** (from memory) adds protected service periods for latency-sensitive traffic, in which the AP stops other clients from contending, which is the Wi-Fi 7 answer to cloud gaming and video call jitter.

Other EHT MAC items worth knowing by name (from memory; none was run here): **triggered TXOP sharing** (the AP gives part of a TXOP to a client), **EHT TRS** (a triggered response scheduling control field), the larger **A-MPDU limit** (the MAC capability has an A-MPDU length exponent extension), and the **EHT OM control** for a client to change its operating mode. Coordinated work between APs (coordinated beamforming, joint transmission, distributed MLO) is a Wi-Fi 8 topic and is in WF 13.

## 9. Security with MLO

- **WPA3 and PMF.** On 6 GHz WPA3 (SAE for personal, or WPA3-Enterprise) or OWE and protected management frames are required, and Wi-Fi Alliance certification of Wi-Fi 7 requires WPA3 (from memory). The N1 article's SSID used WPA3-Enterprise with **GCMP-256 and CCMP-128** ciphers and AKM suites 3 and 5 (FT over 802.1X and 802.1X with SHA-256, from memory). GCMP-256 is the cipher that Wi-Fi 7 hardware accelerates at line rate.
- **One handshake, one pairwise key set per MLD** (section 4.1). The 4-way handshake of WF 04 runs once after the multi-link association; a roam that drops a link does not rerun it.
- **Beacon protection.** Bit 84 of the extended capabilities element signals that beacons carry a message integrity element; a client that sees it set must check it. It closes the hole that PMF leaves open, forged beacons, which is how an attacker used to steer a client by announcing a channel switch (WF 04, WF 05).
- **Where it can break.** A client or AP with a Wi-Fi 7 radio and a driver that falls back to a legacy key type for one link (from memory of vendor bug reports) shows up as an association that succeeds on one link and fails on the other; the per-link status in the response is where to read it.

## 10. Verifying MLO in a capture

Do these in order; stop at the first failure.

1. **Is the AP an MLD?** The beacon on each band carries a multi-link element with the AP MLD address and a per-STA profile for each other link, and the reduced neighbor report lists the same MLD. Compare the MLD address across the bands; section 4.2 explains why you must also count BSSIDs.
2. **Is the client an MLD?** Its association request carries a multi-link element with its own MLD address. If the element is missing, the client associated as a single link, whatever its label says.
3. **What did the client ask for?** Read in the same element: the **maximum simultaneous links** field (0 means one link at a time), the **EMLSR support** bit and the padding and transition delays, and the **NSTR bitmap** of the profile (which link pairs cannot run STR). The seven frames of section 5 are the five cases: a full MLD, an STR-capable client, an EMLSR client, an MLSR-style client and the AP.
4. **What did the AP accept?** The association response lists the links with their status. A link the AP rejected never carries traffic.
5. **Did it switch EMLSR on?** An EML notification with the EMLSR bit and the link bitmap, usually right after association; no notification means the client stays on one link.
6. **Does traffic use two links?** Capture each band with its own sniffer (WF 08, with the clock merge of that tutorial), filter on the client's two link addresses, and check whether data frames to the client appear on both links, and **whether they overlap in time**. Interleaved frames on two links with no overlap are eMLSR or MLSR; overlap in both directions is STR; overlap only when both start together is NSTR. Data frames are encrypted, so use sequence numbers and A-MPDU timing, not payload.

The article notes that a Wi-Fi 6E sniffer (Ekahau Sidekick 2) could not capture Wi-Fi 7 data frames but did capture the management and control frames enough to check steps 1 to 5. A capture of steps 1 to 5 costs a laptop-grade sniffer; step 6 needs one sniffer per band that understands EHT.

## 11. Why a measured rate is far below the peak

Start from the number a phone can reach and subtract what each layer takes, using the computed figures of this tutorial.

| Step | Value | Source |
|---|---|---|
| Peak PHY (320 MHz, 16 streams, MCS 13) | 46,118 Mbit/s | section 2 |
| Two streams instead of sixteen | 5,765 Mbit/s | section 2 |
| 160 MHz instead of 320 MHz | 2,882 Mbit/s | section 2 |
| MCS 11 instead of 13 (the SNR is 36 dB, not 42) | 2,402 Mbit/s | section 2 |
| MAC efficiency 0.65 (assumed, aggregation and contention) | 1,561 Mbit/s | section 6.3 |
| Another network busy 20 percent on the link | 1,249 Mbit/s | section 6.3 |

The three factors that dominate are the stream count of the client (x8 between peak and a phone), the channel width the regulator and the neighbours leave you, and the SNR (4096-QAM needs 42 dB, which exists within a few metres of an AP; section 2.2). MLO adds on top: STR, if the client supports it, sums the links (x1.5 in the model); eMLSR does not. A "Wi-Fi 7 router, 46 Gbit/s" label and a 1.2 Gbit/s speed test are the same network.

## 12. A debugging playbook

| Symptom | First look | What it usually is |
|---|---|---|
| Client connects, speed equals one band | association request (section 10, steps 2 and 3) | maximum simultaneous links field 0, or no EMLSR bit, or an MLSR-style client |
| Client associates but only one link in use | the response status per link | a link rejected (security mismatch, band disabled on the AP) |
| Throughput drops after a minute | EML notifications | client switched EMLSR off to save power, or the AP did |
| Voice jitter on a multi-link client | TID-to-link mapping | voice allowed on a busy link; restrict it (section 7) |
| 320 MHz never used | EHT operation width on the beacon, the regulatory domain | the 6 GHz channel plan or the AP's disabled-subchannel bitmap; DFS or standard power rules |
| Roam to the same AP model fails | the MLD address and the reassociation | a client that treats each link's BSSID as a separate AP (section 4.2) |
| Sniffer sees no data frames | sniffer capability | the sniffer is not EHT capable, or the frames are encrypted (use sequence numbers) |
| Connects at MCS 9 near the AP | SNR at the client | 4096-QAM needs 42 dB (section 2.2); the AP's EVM or the client's receiver limits it |
| AP and client disagree on the rate | the basic EHT-MCS set in the beacon | a client below the BSS minimum cannot associate as EHT |

## 13. Common mistakes

1. **Reading the peak as the speed.** 46.1 Gbit/s needs 16 streams, 320 MHz and 42 dB.
2. **Treating "MLO" as one thing.** eMLSR, NSTR and STR deliver different rates from the same radios (x0.95, x1.2 and x1.5 in the model).
3. **Expecting eMLSR to add the links.** It never uses two links at once.
4. **Reading the maximum simultaneous links field as a count.** The value is the count minus one; a field of 0 is one link.
5. **Trusting the client's label.** The association request, not the box, says what the client supports.
6. **Counting BSSIDs and assuming one MLD per BSSID.** Check the MLD address (section 4.2).
7. **Capturing one band and concluding.** A multi-link capture needs one sniffer per band.
8. **Looking for a decrypted EML or TTLM frame on a protected network.** They are protected action frames.
9. **Expecting puncturing to help on a quiet channel.** The gain is **x1.13 to x1.19** at 2 percent busy and about **x1.62 to x2.10** at 10 percent.
10. **Forgetting the SNR price of 4096-QAM.** 6.03 dB more than 1024-QAM, a distance factor near 0.67 at exponent 3.5.
11. **Quoting a decode from `tshark` as the standard.** The decode here is the D3.0 draft layout.
12. **Comparing MLO modes with one flow.** The mode matters under load and under interference, not for one idle link.
13. **Treating transition delay as free.** 7.9 percent of a 4 ms burst at 256 us.
14. **Testing 6 GHz with a Wi-Fi 6 only sniffer or client and calling the AP faulty.** Check the client's band support first.

## 14. Interview questions

1. **What does Wi-Fi 7 add to the PHY, and what does each addition cost?** 320 MHz (6 GHz only, twice the subcarriers), 4096-QAM (x1.20 for 6.03 dB more SNR), 16 streams, multi-RU and puncturing, and the U-SIG and EHT-SIG preamble.
2. **Derive the peak rate.** 3,920 data subcarriers x 12 bits x 5/6 x 16 streams / 13.6 us = 46,117.6 Mbit/s.
3. **What is an MLD, and what is shared and what is per link?** Identity, association, pairwise keys and sequence ordering per MLD; channel access, beacons and the BSS per link (section 4.1).
4. **Explain eMLSR and why it is not STR.** One radio listens on every link; an initial control frame makes it switch to one link after a transition delay; it never uses two at once.
5. **When is NSTR a pair, and what does the NSTR bitmap say?** Links too close in frequency for one to transmit while the other receives; the bitmap in the per-STA profile marks them; the N1 article's 5 GHz and 6 GHz pair is an example.
6. **A Wi-Fi 7 client gets one band's speed. List the checks.** The association request fields (simultaneous links, EMLSR, NSTR), the response status per link, the EML notification, then traffic on both links with overlap (section 10).
7. **What does preamble puncturing buy, and what does it need?** A usable channel around a busy subchannel (x1.62 to x2.10 at 10 percent busy in the model); a signalling field in the preamble and multi-RU allocation.
8. **What is TID-to-link mapping for?** Putting voice on the clean link, keeping bulk traffic off a weak one, and preparing for a link to be vacated.
9. **How is security handled with MLO?** One handshake and pairwise key set per MLD, PMF and beacon protection, GCMP-256 (from memory), WPA3 on 6 GHz.
10. **How would you test MLO behaviour of a client without a lab of clients?** Build association frames (section 5) for the AP side, and for a real client capture its request and the first EML notification; then measure two-link overlap with one sniffer per band.
11. **Why can the same AP show one BSSID or three?** Vendor choice and the reading of the amendment (section 4.2); the MLD address is the stable identifier.
12. **What is restricted TWT?** A protected service period for latency-sensitive traffic (from memory).

## 15. Problems

Work each one before reading section 16.

**Problem A. A rate.** A 160 MHz link with 3 streams at MCS 12 (4096-QAM, rate 3/4): the PHY rate at the 0.8, 1.6 and 3.2 us guard intervals, the rate at MCS 13 and at MCS 11, and the cost of the 3.2 us interval.

**Problem B. Read a multi-link element by hand.** Take the element bytes of an AP with two additional links (a beacon element built for you), parse the control field, the common info and the two per-STA profiles, and check the result with `tshark`.

**Problem C. The eMLSR cost.** For bursts of 0.5, 1, 2, 4 and 5.484 ms and transition delays of 0, 32, 64, 128 and 256 us (with a 60 us initial control frame), give the overhead, and the burst length that keeps it under 5 percent.

**Problem D. Puncturing.** An 80 MHz channel at 2 streams and MCS 11 with one 20 MHz subchannel busy: the rate without puncturing when the busy one is the secondary 20 and when it is in the secondary 40, the rate with puncturing, and the gain.

**Problem E. The range of 4096-QAM.** From the SNR difference between 4096-QAM and 1024-QAM, the distance factor for path loss exponents 2, 3, 3.5 and 4, and where a 1024-QAM edge at 20 m moves.

## 16. Answers

**Answer A.** The formula of section 2.1 with 1,960 subcarriers; the guard interval is the only difference between the first three rows, and the 3.2 us interval costs 15.0 percent against 0.8 us, because the symbol grows from 13.6 to 16.0 us.

```
--- 160 MHz, 3 streams, MCS 12 (4096-QAM, 12 bits, rate 3/4)
  guard interval 0.8 us: 3891.2 Mbit/s
  guard interval 1.6 us: 3675.0 Mbit/s
  guard interval 3.2 us: 3307.5 Mbit/s
--- the same link at MCS 13 (rate 5/6), 0.8 us: 4323.5 Mbit/s; at MCS 11 (1024-QAM, 5/6): 3602.9 Mbit/s
--- the 3.2 us guard interval costs 15.0 percent against 0.8 us
```

**Answer B.** The control field `0x01b0` says type 0 (Basic) with the link ID, BSS parameters change count, EML capabilities and MLD capabilities fields present; the common info length is 13 bytes; each per-STA profile carries a link ID and a MAC address. The hand parse and `tshark` agree:

```
--- the element body: b0010d02aa00000001000121000100000921000702aa00000002000922000702aa00000003
control 0x01b0: type 0 (0 is Basic); presence bits: link ID 1, BSS change count 1, medium sync 0, EML capabilities 1, MLD capabilities 1
common info length 13: MLD address 02:aa:00:00:00:01, link ID 0, BSS parameters change count 1, EML capabilities 0x0021 (bit 0 is EMLSR support: 1), MLD capabilities 0x0001 (low 4 bits, maximum simultaneous links: 1)
per-STA profile (subelement 0, 9 bytes): link ID 1, complete 0, MAC present 1; STA info length 7, MAC 02:aa:00:00:00:02
per-STA profile (subelement 0, 9 bytes): link ID 2, complete 0, MAC present 1; STA info length 7, MAC 02:aa:00:00:00:03
--- tshark on the same bytes (MLD address, EMLSR support, maximum simultaneous links): 02:aa:00:00:00:01, True, 1
```

**Answer C.** Overhead is (0.060 ms + delay) divided by the burst. Even a full length 5.484 ms PPDU pays 5.8 percent with a 256 us delay, so on one idle link eMLSR stays below a plain single link.

```
--- overhead = (initial control frame + transition delay) / burst length
burst ms        0 us     32 us     64 us    128 us    256 us
   0.500       12.0%     18.4%     24.8%     37.6%     63.2%
   1.000        6.0%      9.2%     12.4%     18.8%     31.6%
   2.000        3.0%      4.6%      6.2%      9.4%     15.8%
   4.000        1.5%      2.3%      3.1%      4.7%      7.9%
   5.484        1.1%      1.7%      2.3%      3.4%      5.8%
--- break even: the burst length at which the overhead is 5 percent, for a transition delay d: burst = (0.060 ms + d) / 0.05
  d =   0 us: 1.20 ms
  d =  32 us: 1.84 ms
  d =  64 us: 2.48 ms
  d = 128 us: 3.76 ms
  d = 256 us: 6.32 ms
--- the longest PPDU is 5.484 ms (WF 02), so with a 256 us delay even a full length PPDU pays 5.8 percent
```

**Answer D.** The 60 MHz case is a 484+242 multi-RU, 468 + 234 = 702 data subcarriers.

```
--- 80 MHz, 2 streams, MCS 11 (1024-QAM 5/6), 0.8 us: idle channel 1201 Mbit/s
  secondary 20 busy             : without puncturing 20 MHz = 287 Mbit/s; with puncturing 60 MHz (484+242) = 860 Mbit/s; gain 3.00 times
  one of the secondary 40 busy  : without puncturing 40 MHz = 574 Mbit/s; with puncturing 60 MHz (484+242) = 860 Mbit/s; gain 1.50 times
--- the 60 MHz layout is the sum 468 + 234 = 702 data subcarriers, which is 71.6 percent of the 980 of the idle 80 MHz channel (a 75 percent bandwidth)
```

**Answer E.** The range cost of the extra 6 dB shrinks as the path loss exponent grows: the worse the environment, the fewer metres 6 dB is worth.

```
--- extra SNR for 4096-QAM over 1024-QAM at symbol error rate 1e-3 (s1_rates): 6.03 dB
--- the distance at which a link has that much less SNR, for path loss exponent n: factor = 10^(dB / (10 n))
  n = 2.0 (free space): 4096-QAM reaches 50 percent of the 1024-QAM distance; a 1024-QAM edge at 20 m becomes 10.0 m
  n = 3.0 (open office): 4096-QAM reaches 63 percent of the 1024-QAM distance; a 1024-QAM edge at 20 m becomes 12.6 m
  n = 3.5 (typical indoor): 4096-QAM reaches 67 percent of the 1024-QAM distance; a 1024-QAM edge at 20 m becomes 13.5 m
  n = 4.0 (cluttered): 4096-QAM reaches 71 percent of the 1024-QAM distance; a 1024-QAM edge at 20 m becomes 14.1 m
--- the gain on the other side: 4096-QAM carries 12 bits against 10, so inside the range it adds 20 percent
```

## 17. Cheat sheet

| Item | Value or rule |
|---|---|
| Data subcarriers | 234, 468, 980, 1,960, 3,920 for 20, 40, 80, 160, 320 MHz |
| EHT rate | subcarriers x bits x coding x streams / (12.8 us + guard interval) |
| Peak | 46,117.6 Mbit/s (320 MHz, 16 streams, MCS 13, 0.8 us); one stream 2,882.4 |
| 4096-QAM | x1.20 bits, +6.03 dB SNR (42.17 against 36.14 dB), EVM -38 dB (from memory) |
| Puncturing | model: x1.13 to x1.19 at 2 percent busy, x1.62 to x2.10 at 10 percent |
| 60 MHz multi-RU | 484+242, 702 data subcarriers |
| MLD | one association, one handshake; links are separate contention domains |
| Element | multi-link element: extension element 255, extension ID 107; per-STA profile per link |
| Max simultaneous links | value is the count minus one |
| eMLSR | one radio, one link at a time; padding 0 to 256 us, transition 0 to 256 us (from memory) |
| Modes in the model | eMLSR x0.95 to x1.24 of the best single link, NSTR between, STR x1.12 to x1.50 |
| MLO check | multi-link element in the request, EMLSR bit, NSTR bitmap, response status, EML notification, traffic overlap |
| Action frames | Protected EHT: 0 TID-to-link mapping request, 6 EML operating mode notification (from memory) |
| Sniffer | one per band; data is encrypted, use sequence numbers |
| Not tested here | any radio, the final amendment layout, client behaviour beyond one article |

Next: [WF 13, Wi-Fi 8](13_wifi8_uhr_802_11bn.md) (not yet written). Related: [WF 02](02_wifi_phy.md), [WF 03](03_wifi_mac.md), [WF 04](04_wifi_security.md), [WF 05](05_wifi_roaming_and_radio_management.md), [WF 06](06_wifi6_6e_7_features.md), [WF 08](08_wifi_capture_tools_and_debug.md), [WF 09](09_wifi_testing.md).

## Sources

- IEEE 802.11be-2024 (EHT), and the 802.11be draft layout decoded by Wireshark 4.2 ("802.11be D3.0")
- mrncciew.com: Wi-Fi 7 beacon frame (15 Dec 2024), Apple N1 chip and Wi-Fi 7 MLO (18 Sep 2026); the page "Wi-Fi 7 Notes: MLO and Beyond" (16 Sep 2026) was read as an index only
- "Comparison of STR and EMLSR Performance in Wi-Fi 7 MLO", arXiv 2501.04149
- MediaTek, Wi-Fi 7 MLO white paper (WF7MLOWP0622), found in search and not read in full
- Candela Technologies, wireless course, session 5d: https://www.candelatech.com/course.php
- Wi-Fi Alliance Wi-Fi 7 certification materials (from memory)

Numbers marked computed come from the formulas of `s1_rates.py` and the problem scripts; modelled numbers come from `s3_modes.py` and `s4_puncture.py`; frame decodes come from `tshark` 4.2; everything else is marked from memory.
