# WF 12: Wi-Fi 7 (802.11be, EHT) in depth: the PHY, multi-RU and puncturing, and multi-link operation

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela material on Wi-Fi 7 · You need: [WF 02](02_wifi_phy.md) (OFDM, MCS, widths), [WF 06](06_wifi6_6e_7_features.md) (OFDMA, TWT, 6 GHz), [WF 05](05_wifi_roaming_and_radio_management.md) (roaming), [WF 04](04_wifi_security.md) (WPA3, PMF)*

Wi-Fi 7 is the generation where marketing numbers and engineering reality drift furthest apart. The headline is a peak rate in the tens of gigabits, but what a customer feels is different: fewer stalls when a channel is busy, lower latency at the tail, and a 6 GHz network that survives interference. The technical story behind that is three ideas. The **PHY gets wider and denser** (320 MHz channels, 4096-QAM). **Multi-RU and preamble puncturing** let a transmission use a channel that has a hole in it instead of giving up the whole block. And **multi-link operation (MLO)** lets one device talk over several bands as one logical device.

Interviewers ask about Wi-Fi 7 to see whether you can separate what the standard defines from what silicon ships and from what a network can actually deliver. A good answer names the feature, says what problem it solves, says what it costs (SNR, power, spectrum, complexity) and says what a capture shows. MLO is the part to master: the modes (STR, eMLSR, NSTR, MLSR), what is shared across links and what is per link, and why a client that advertises MLO is not necessarily using two links.

By the end you can:

- state what changed from Wi-Fi 6 to Wi-Fi 7 and compute an EHT rate from the formula;
- explain what 4096-QAM costs in SNR and why it rarely appears outside a short, clean link;
- explain 320 MHz, why spectrum limits it, and where it is worth using;
- explain multi-RU and preamble puncturing, and what they are worth when part of a channel is busy;
- explain an MLD, what is shared across links and what stays per link;
- read a multi-link element, an association and the action frames that manage a live MLO association;
- compare STR, eMLSR, NSTR and MLSR, with the trade each one makes, and say what real clients ship;
- explain TID-to-link mapping, power save, restricted TWT and the security rules of Wi-Fi 7;
- verify MLO in a capture, in order, and say why a measured rate is far below the peak;
- plan a Wi-Fi 7 deployment (spectrum, power, switching, coverage, security).

---

## 1. What changed from Wi-Fi 6 to Wi-Fi 7

| Feature | Wi-Fi 6 (HE) | Wi-Fi 7 (EHT) | What it buys |
|---|---|---|---|
| Widest channel | 160 MHz | **320 MHz** (6 GHz only) | Double the peak rate |
| Modulation | up to 1024-QAM | up to **4096-QAM** (EHT-MCS 12 and 13) | About 20 percent more rate at high SNR |
| Resource units | One RU per user | **Multiple RUs per user** (multi-RU) | Use a channel that has a busy part |
| Puncturing | Limited, in OFDMA | **Preamble puncturing** for single-user and OFDMA at 80 MHz and above | Do not lose the whole channel to one narrow interferer |
| Multi-band | Separate associations per band | **Multi-link operation (MLO)**: one MLD, several links | Throughput aggregation, lower latency, reliability |
| Aggregation | 256 MPDUs per A-MPDU | Up to **1024** | Less per-frame overhead at high rates |
| Streams | 8 (spec) | 16 in the standard; products ship fewer (clients usually 2, APs up to 8) | Peak rate |
| Low latency | TWT | **Restricted TWT** | Protected service periods for latency-sensitive traffic |
| Security | WPA2 allowed | **WPA3, OWE, PMF and beacon protection required** for Wi-Fi 7 certification | Closes old gaps |
| Coordination | None | Groundwork; real multi-AP coordination is Wi-Fi 8 | See [WF 13](13_wifi8_uhr_802_11bn.md) |

The two things to remember about this table. **Almost every row is optional or capability-negotiated**, so "Wi-Fi 7" on a box says little about which features a given client implements. And **most of the gains are conditional**: 4096-QAM needs a short clean link, 320 MHz needs 6 GHz spectrum that most countries have in limited supply, and MLO helps most when links are busy, not idle.

## 2. The EHT PHY

### 2.1 One formula for every rate

The rate of an OFDM PHY is the number of data subcarriers times the bits per subcarrier, times the code rate and the number of spatial streams, over the symbol time. EHT keeps HE's 12.8 µs symbol and guard intervals of 0.8, 1.6 and 3.2 µs, and extends the subcarrier counts to 320 MHz:

```python
DATA_SC = {20: 234, 40: 468, 80: 980, 160: 1960, 320: 3920}      # data subcarriers per width
BITS = {0: (1, .5), 1: (2, .5), 2: (2, .75), 3: (4, .5), 4: (4, .75), 5: (6, 2/3), 6: (6, .75),
        7: (6, 5/6), 8: (8, .75), 9: (8, 5/6), 10: (10, .75), 11: (10, 5/6), 12: (12, .75), 13: (12, 5/6)}

def eht_rate(width, mcs, nss=1, gi_us=0.8):          # Mbit/s
    b, r = BITS[mcs]
    return DATA_SC[width] * b * r * nss / (12.8 + gi_us)

print(round(eht_rate(320, 13, 16) / 1000, 1))        # 46.1 Gbit/s: the standard's peak (16 streams, 320 MHz, 4096-QAM)
print(round(eht_rate(320, 13, 2) / 1000, 2))         # 5.76 Gbit/s: a 2-stream client at 320 MHz
print(round(eht_rate(160, 13, 2) / 1000, 2))         # 2.88 Gbit/s: the same client at 160 MHz
print(round(eht_rate(80, 13) / eht_rate(80, 11), 2)) # 1.2: 4096-QAM over 1024-QAM at the same code rate
```

The **46 Gbit/s** peak is an arithmetic limit that no product reaches, because no device has 16 streams on 320 MHz. The number to quote for a phone or laptop is the **2-stream rate**: about **2.9 Gbit/s at 160 MHz and 5.8 Gbit/s at 320 MHz**, still a PHY rate before MAC efficiency (section 11). The 20 percent gain from 4096-QAM is just 12 bits against 10 bits per symbol.

EHT-MCS 0 to 11 are the HE set; **12 and 13** add 4096-QAM at code rates 3/4 and 5/6. Two further EHT-MCS values (14 and 15, from memory) are robust duplicated modes for long-range use, mainly in 6 GHz.

### 2.2 What 4096-QAM costs

Packing 12 bits into each symbol means the 4096 constellation points are close together, so the receiver needs a **very high SNR** (a deployment guide quotes about **42 dB** against **31 dB** for 1024-QAM), and the **transmitter** needs very low distortion: its error vector magnitude (EVM) must be small enough that its own errors do not blur neighbouring points, which forces the power amplifier to back off and costs range or power. The result is that **4096-QAM appears only in a short, clean, line-of-sight link**, such as a laptop a few metres from an AP, and every metre of distance or a wall removes it. That is why a benchmark that shows the new rate and a customer who never sees it are both honest. Also 4096-QAM needs beamformed transmission to keep the SNR up, a reason for the sounding overhead of [WF 06](06_wifi6_6e_7_features.md).

### 2.3 320 MHz and where the spectrum is

A 320 MHz channel exists only in **6 GHz** (a contiguous 320 MHz block, or two 160 MHz blocks combined in some modes). The band has 1,200 MHz in the US and the EU's upper part and less elsewhere, so a country with the full band offers **three** non-overlapping 320 MHz channels and a country with only 500 MHz offers **one**. Under **standard power** (AFC-controlled outdoor and higher-power operation) the count is lower again because only some sub-bands qualify. Practical consequences:

- **320 MHz is scarce and reusable only barely.** A building with several APs cannot give each its own 320 MHz channel, so wide channels belong where traffic is heavy and co-channel neighbours are few; most APs use 80 or 160 MHz.
- **A wider channel has 3 dB more noise per doubling** ([WF 11](11_rf_fundamentals_and_link_budget.md)), so the SNR for the top MCS shrinks with distance faster at 320 MHz.
- **6 GHz coverage follows the 5 GHz plan**, not the 2.4 GHz plan: verify that 6 GHz cell sizes match your 5 GHz coverage, with a survey if needed.

### 2.4 The PPDU and what is new in it

The EHT PPDU keeps the HE preamble structure (legacy fields, then an RL-SIG, a U-SIG with version-independent fields and a EHT-SIG with the resource unit allocation) with new fields for **puncturing** (a disabled-subchannel bitmap), 320 MHz and the EHT long training fields. The detail matters mostly to chip designers. What an engineer needs is that the **signalling of which subchannels are punctured is in the preamble**, so the receiver knows without guessing.

## 3. Multi-RU and preamble puncturing

### 3.1 The problem puncturing solves

In Wi-Fi 6 a transmission occupies a contiguous channel. If a neighbouring network or a radar occupies 20 MHz of an 80 MHz channel, the AP must either use a narrower channel (losing three quarters of the width), or send in the busy part and collide. Real networks have this **partial busy** state all the time in dense deployments, and it is the main reason peak widths are not used.

**Preamble puncturing** lets a transmission skip the busy 20 MHz subchannels. The PPDU is sent on the rest, and the punctured part is left empty and signalled in the preamble. It works for **channels of 80 MHz and above** and is part of Wi-Fi 7 (and the 6 GHz regulatory work). Puncturing patterns are limited (typically one or two 20 MHz subchannels per 80 MHz block), and the AP decides when a subchannel is busy from the CCA result ([WF 03](03_wifi_mac.md)).

### 3.2 Multi-RU: how a user gets a punctured channel

In Wi-Fi 6 an OFDMA user receives a **single** resource unit. If the channel has a hole, a single RU cannot span it. Wi-Fi 7 allows **multiple RUs per user**, merged into a **multi-RU (MRU)**: for example a 484-tone RU plus a 242-tone RU (the 80 MHz channel minus one 20 MHz subchannel). The standard restricts which RUs may be combined (small-size MRUs inside a 20 MHz subchannel, large-size MRUs spanning several), so a puncturing pattern maps to a legal MRU.

### 3.3 How much it is worth

With one 20 MHz subchannel of an 80 MHz channel busy, a Wi-Fi 6 AP's options are an 80 MHz channel it cannot fully use or a 40 MHz fallback. With puncturing and MRU the AP keeps **three quarters of the 80 MHz channel**:

```python
def mru_rate(tones, mcs, nss=1, gi_us=0.8):
    b, r = BITS[mcs]
    return tones * b * r * nss / (12.8 + gi_us)

full  = mru_rate(980, 11, 2)            # 80 MHz, 2 streams, MCS 11: 1201 Mbit/s
punct = mru_rate(484 + 242, 11, 2)      # 80 MHz with one 20 MHz subchannel punctured (MRU of 726 tones)
narrow = mru_rate(468, 11, 2)           # fallback to a clean 40 MHz channel
print(round(full), round(punct), round(narrow))   # 1201 890 574
```

A punctured 80 MHz channel (about 890 Mbit/s) beats a fallback to 40 MHz (about 570 Mbit/s) by roughly 55 percent here, and the gain is larger the wider the channel and the more often the partial-busy state occurs. This is a **reliability** feature as much as a rate one: the network keeps working at close to its width in the presence of an interferer, which is what customers feel as "stable".

## 4. The multi-link device

### 4.1 Vocabulary

An **MLD** is one logical device with one MAC service interface to the layers above and several **affiliated** stations, each a full 802.11 station on its own link (its own radio, band and channel). An **AP MLD** has affiliated APs; a **non-AP MLD** (a phone or laptop) has affiliated STAs. Each affiliated station has its **link MAC address**, which is the address on the air (receiver, transmitter and BSSID fields), and the MLD as a whole has an **MLD MAC address**, which the upper layers and the key hierarchy see. A link is identified by a **link ID**.

| Thing | Per MLD (shared) | Per link (separate) |
|---|---|---|
| Address | MLD MAC address | Link MAC address, BSSID |
| Association | One, set up on one link and covering all that are enabled | The link's BSS: beacons, capabilities, channel |
| Pairwise keys and the 4-way handshake | One handshake, one key set for the MLD | None |
| Group keys | Per link (each BSS sends its own group traffic) | Yes |
| Sequence numbers, block-ack scoreboard | One per TID across links, so frames can go on any link and be reordered at the receiver | None |
| EDCA and the NAV | None | Each link contends for its own channel |
| Power state | The MLD has rules for the whole | Per link |

The one fact to hold: **an MLD joins a network once, then each link is a separate contention domain.** Everything that is channel access stays per link; everything that is identity, security and ordering is per MLD.

**BSSIDs.** Each affiliated AP keeps its own BSSID (its link address) and the multi-link element carries the common MLD address, so a three-band AP MLD shows three BSSIDs and one MLD address. A vendor article may describe it differently (one BSSID across bands). Count BSSIDs and the MLD address in your own capture before relying on either.

### 4.2 Why one device, several links

MLO aggregates: a client can use whichever link is free (contention diversity), so it **gets lower latency when one band is busy**, and it can send **parts of a flow on different links** (more throughput) if the hardware supports simultaneous operation. It also adds reliability: if a link degrades, traffic moves without a re-association. This is the property that turns a multi-band AP from three independent networks into one.

## 5. The frames that make a multi-link association

### 5.1 The multi-link element

The element is an extension element (ID 255, extension ID 107). Its 2-byte **control** field holds the type (0 Basic, used in beacons, probe responses and association; the other types cover probe requests, reconfiguration, TDLS and priority access) and flags saying which optional fields are in the **common info**. The common info starts with its own length, then the **MLD address**, and optional fields (link ID, BSS parameters change count, medium synchronisation delay, **EML capabilities**, **MLD capabilities**). After it come **per-STA profile subelements**, one per other link, each with a 2-byte STA control (link ID, a complete-profile bit, a MAC-address-present bit, beacon interval, TSF offset and DTIM bits, an **NSTR link pair** bit) and the STA info. A builder and a `tshark` decode:

```python
import struct

def mac(s): return bytes(int(x, 16) for x in s.split(":"))

def ml_basic(mld, links, link_id=0, bss_chg=1, eml=0x21, mldcap=0x01):
    pres = 0x10 | 0x20 | 0x80 | 0x100             # link id, BSS change count, EML caps, MLD caps present
    common = mac(mld) + bytes([link_id, bss_chg]) + struct.pack('<H', eml) + struct.pack('<H', mldcap)
    body = struct.pack('<H', pres) + bytes([len(common) + 1]) + common
    for lid, addr in links:
        info = mac(addr)
        prof = struct.pack('<H', lid | (1 << 5)) + bytes([len(info) + 1]) + info
        body += bytes([0, len(prof)]) + prof      # per-STA profile subelement
    return bytes([255, len(body) + 1, 107]) + body

elt = ml_basic("02:aa:00:00:00:01", [(1, "02:aa:00:00:00:02"), (2, "02:aa:00:00:00:03")])
print(len(elt))                                   # 40 bytes
```

Appended to a beacon, `tshark` decodes it as an EHT multi-link element with the MLD address `02:aa:00:00:00:01`, EMLSR support true and the maximum-simultaneous-links field 1, confirming the layout. The EHT beacon also carries the **EHT capabilities** and **EHT operation** elements (the operation element holds the **basic EHT-MCS and NSS set**, the minimum every EHT client must support in the BSS, and the optional disabled-subchannel bitmap for puncturing), the **multi-link traffic indication** (a bitmap of which links have buffered frames) and the **TID-to-link mapping** element. **Beacon protection** is signalled in the extended capabilities element and Wi-Fi 7 requires it.

### 5.2 The association

The non-AP MLD sends **one** association request on one link. It contains the ordinary request for that link plus a multi-link element with its MLD address, its EML and MLD capabilities and a per-STA profile for each additional link it wants. The AP MLD answers with one response carrying its MLD address and a per-STA profile for each link it accepts, with a status per link. The MLD then has **one association and one 4-way handshake**. The **maximum simultaneous links** field is the number of links the device can use at once **minus one** (0 means one at a time), and the **NSTR bitmap** in a profile marks link pairs the client cannot run in STR mode.

### 5.3 The action frames that manage a live association

Two **Protected EHT** action frames (category 37) manage it. The **TID-to-link mapping request** carries a link bitmap per TID (for example TIDs 0 to 5 on links 0 and 1, and the two voice TIDs only on link 0). The **EML operating mode notification** switches EMLSR or EMLMR on or off and says which links are enabled. Because they are protected, a sniffer without the keys sees only the category after the handshake.

| To find | Display filter field (tshark 4.2; names move between versions) |
|---|---|
| Any multi-link element | `wlan.eht.multi_link.common_info.mld_mac_address` (or `ap_mld_mac_address`) |
| Client MLD capabilities | `wlan.eht.multi_link.common_info.mld_capabilities.max_simultaneous_links` |
| EMLSR support | `wlan.eht.multi_link.common_info.eml_capabilities.emlsr_support` |
| NSTR pair | `wlan.eht.multi_link.sta_profile.sta_control.nstr_link_pair_present` |
| EML notifications | `wlan.eht.eml_control.emlsr_mode`, `wlan.eht.eml_control.link_bitmap` |
| TID-to-link mapping | `wlan.eht.tid_to_link_mapping.tid_0.link_mapping` to `tid_7` |

## 6. The MLO modes

What separates the modes is **how many radios the client has**, whether there are filters between them, and **whether links may transmit and receive at the same time**.

| Mode | Links associated | Transmit and receive at once | Needs |
|---|---|---|---|
| **SLO** (single link) | One | n/a | Nothing |
| **MLSR** (multi-link single radio) | Several | One link at a time; switching is slow | One radio |
| **eMLSR** (enhanced) | Several | The device **listens on every enabled link with one radio** and sends or receives on one at a time; an initial control frame on a link makes it switch its full radio there | One radio, fast switching |
| **eMLMR** (enhanced multi-radio) | Several | Chains combine to use more than one link for one exchange | Rarely shipped |
| **NSTR** | Several, on pairs that interfere | Simultaneous only if both links start and end aligned | Two radios, no isolation |
| **STR** (simultaneous TX and RX) | Several | Each link independent | Two radios with enough isolation and frequency separation |

**Why STR is hard.** A transmitter on one link must not deafen the receiver on another. That depends on the band pair: 2.4 GHz with 5 or 6 GHz is the easiest; the **low 6 GHz and the high 5 GHz** are close in frequency and often form an **NSTR pair** (a capture of a recent laptop shows an NSTR bitmap marking exactly the 5 and 6 GHz links). The standard allows NSTR; a vendor deployment guide states that **nSTR clients are not part of Wi-Fi 7 certification and no vendor has implemented it**, and that most client vendors implement **eMLSR or STR**, while an AP can run several modes at once.

**eMLSR in detail.** An eMLSR client keeps one receive chain per enabled link. To send to it the AP first transmits an **initial control frame** (a MU-RTS or BSRP trigger) on one link; the client receives it, switches its other chains to that link, and the AP waits the client's advertised **transition delay** before data, with the control frame padded by at least the client's **padding delay**. Afterwards the client returns to listening on all links. The delays (padding 0 to 256 µs, transition 0 to 256 µs, from memory) are in the EML capabilities. **eMLSR never uses two links at once**: it buys contention diversity (use whichever link is free first), not a higher rate.

### 6.1 What a simple model says

A model with two links (a 5 GHz 80 MHz link of 1,201 Mbit/s PHY and a 6 GHz 160 MHz link of 2,402 Mbit/s PHY, MAC efficiency 0.65, 4 ms bursts) makes the trade visible:

```python
def mlo_models(ra, rb, ba, bb, eff=0.65, sw_us=128, burst_us=4000):
    """ra, rb: PHY Mbit/s; ba, bb: fraction of time each link is busy with others (a is the faster link)."""
    fa, fb = ra * eff, rb * eff
    if fa < fb: fa, fb, ba, bb = fb, fa, bb, ba
    single = fa * (1 - ba)                                        # best single link
    emlsr = (fa * (1 - ba) + fb * ba * (1 - bb)) * (1 - (60 + sw_us) / burst_us)   # fall back to the slow link
    str_  = fa * (1 - ba) + fb * (1 - bb)                         # both links, independent
    return round(single), round(emlsr), round(str_)

for ba, bb in ((0, 0), (0.2, 0.2), (0.6, 0.6)):
    print(ba, bb, mlo_models(1201, 2402, ba, bb))
# 0   0   (1561, 1488, 2342)
# 0.2 0.2 (1249, 1309, 1874)
# 0.6 0.6 (625, 774, 937)
```

Read it as three statements. **STR** sums the two links, a gain of 1.5 times over the best single link when both are free. **eMLSR** does not sum: with both links idle it is about 5 percent *below* the best single link because of the control frame and the transition delay, and it pays off only when the faster link is busy (about 24 percent above single link when both are 60 percent busy). **NSTR** sits between them. The model is an illustration with assumed parameters; published ns-3 studies of STR against eMLSR agree on the **ordering**: STR above eMLSR above single link, with eMLSR closest to single link when the links are lightly loaded.

### 6.2 What a real client does

Do not infer behaviour from a label. One published capture of a recent laptop on a tri-band WPA3-Enterprise SSID showed a multi-link element with two profiles, an NSTR bitmap marking 5 GHz and 6 GHz as a non-STR pair, **uplink-only use of two links**, and **0** in the maximum simultaneous links field. The author reads it as MLSR-like. That is one pair of devices, but the lesson is general: **a client advertising MLO is not a client using two links**, and the only way to know is to read the association and then the traffic (section 10).

## 7. TID-to-link mapping, power save, restricted TWT

**TID-to-link mapping (TTLM).** By default every TID may use every enabled link. A TTLM restricts it, per direction or both: keep voice on the cleanest link, keep a power-limited link out of bulk traffic, or push everything off a link that is about to be vacated. The AP can advertise a mapping in beacons with a switch time. Related management: **link reconfiguration** (add or remove a link without a new association), **link recommendation**, the **multi-link traffic indication** in the TIM (so a dozing link learns that another link holds data), and the **BSS parameters change count** in the common info (a client sees that another link's beacon changed without reading it). A roam within an MLD can add and drop links; a roam between AP MLDs is a reassociation of the whole MLD ([WF 05](05_wifi_roaming_and_radio_management.md)).

**Power save is per link.** An MLD can doze one link and stay awake on another. eMLSR makes the client listen on every enabled link at reduced capability, which costs power on each; a client that wants to save power turns EMLSR off with the notification frame and falls back to one link. **TWT** still schedules wake windows per link ([WF 06](06_wifi6_6e_7_features.md)). **Restricted TWT (R-TWT)** adds **protected service periods** announced in the broadcast TWT element: stations that are members get reserved windows for latency-sensitive traffic, and other stations are held off. This is the Wi-Fi 7 tool for cloud gaming, AR/VR and video-call jitter, and its value depends on both the AP and the client implementing it.

Other EHT MAC items worth knowing by name: **triggered TXOP sharing** (the AP gives part of its TXOP to a client), the larger **A-MPDU** limit, and the **EHT operating-mode control** for a client to change its mode. Coordination between APs (coordinated beamforming, joint transmission) is a Wi-Fi 8 topic ([WF 13](13_wifi8_uhr_802_11bn.md)).

## 8. Security with Wi-Fi 7

- **WPA3, OWE and PMF are required** for Wi-Fi 7 certification and for the 802.11be rates and MLO; a deployment guide advises enabling WPA3 or Enhanced Open **before** enabling 802.11be rates or MLO. Ciphers are CCMP-128 and **GCMP-256** (Wi-Fi 7 hardware accelerates GCMP-256 at line rate); AKM suites include SAE and the 802.1X suites ([WF 04](04_wifi_security.md)).
- **One handshake, one pairwise key set per MLD.** The 4-way handshake runs once after the multi-link association, and a roam that drops a link does not rerun it.
- **Beacon protection** is required of both APs and clients: beacons carry a message integrity element and a client that sees the capability must check it. It closes the hole PMF leaves open (forged beacons), by which an attacker used to steer a client with a fake channel-switch announcement.
- **Security must be established across all links** of a multi-link association. A driver that falls back to a legacy key type on one link shows up as an association that succeeds on one link and fails on another; read the per-link status in the association response.

## 9. Deploying Wi-Fi 7

- **Spectrum.** Count the 6 GHz channels your country allows, plan 320 MHz only for the few APs that need it, and keep 80 or 160 MHz elsewhere. Standard-power operation depends on AFC and the sub-band.
- **Coverage.** Check that 6 GHz cells match the 5 GHz coverage; if not, a client on the edge uses 5 GHz and the MLO gain falls away.
- **Power over Ethernet.** A deployment guide recommends **802.3bt** for full function; 802.3at works with reduced function (fewer radios or chains).
- **Switching.** Combined throughput can exceed 1 Gbit/s, so use multigigabit uplinks (2.5 or 5 Gbit/s) on the access switches.
- **Clients.** Features are optional: 4096-QAM, 320 MHz, STR and eMLSR vary by client chip. Test with the actual clients.
- **Mixed generations.** Wi-Fi 7 APs still serve Wi-Fi 4 to 6 clients, and airtime fairness and legacy-rate settings ([WF 08](08_wifi_capture_tools_and_debug.md)) matter more than the new features for most users.

## 10. Verifying MLO in a capture

Do these in order and stop at the first failure.

1. **Is the AP an MLD?** The beacon on each band carries a multi-link element with the AP MLD address and a profile per other link; the reduced neighbour report lists the same MLD. Compare the MLD address across bands, and count BSSIDs.
2. **Is the client an MLD?** Its association request carries a multi-link element with its own MLD address. If the element is missing, it associated as a single link.
3. **What did the client ask for?** Read the **maximum simultaneous links** field (0 means one link at a time), the **EMLSR support** bit and delays, and the **NSTR bitmap**.
4. **What did the AP accept?** The association response lists links with a status each. A rejected link never carries traffic.
5. **Did it switch EMLSR on?** An EML notification right after association; none means the client stays on one link.
6. **Does traffic use two links?** Capture each band with its own sniffer ([WF 08](08_wifi_capture_tools_and_debug.md)), filter on the client's two link addresses, and check whether data appears on both links and **whether it overlaps in time**. Interleaved frames with no overlap are eMLSR or MLSR; overlap in both directions is STR; overlap only when both start together is NSTR. Data is encrypted, so use sequence numbers and A-MPDU timing.

A sniffer built for Wi-Fi 6E may not capture Wi-Fi 7 data frames but can still capture the management and control frames enough for steps 1 to 5.

## 11. Why a measured rate is far below the peak

Start from the PHY rate of a 2-stream client (2.9 Gbit/s at 160 MHz and MCS 13) and take away what each layer costs:

- **The link must reach MCS 13**: about 40 dB SNR at the top of the range, which a client only sees close to the AP, so most of the time the MCS is lower.
- **MAC efficiency** (preamble, contention, ACKs, aggregation limits): at the highest PHY rates fixed overheads take a larger share, so a MAC efficiency of 60 to 70 percent is typical, even with large aggregates.
- **Retries and sharing**: other stations on the channel, hidden nodes and interference.
- **The host**: TCP behaviour, a USB or PCIe bottleneck, the CPU, the server, and an uplink or switch port of 1 Gbit/s that caps everything.
- **The test**: a single TCP stream, a small window or a long RTT ([WF 08](08_wifi_capture_tools_and_debug.md)).

A laptop that shows 1.5 to 2 Gbit/s of TCP goodput next to the AP is performing normally; the 5.8 Gbit/s figure is for 320 MHz, a client with two streams and a nearly perfect link.

## 12. Debugging by symptom

| Symptom | Likely cause | First check |
|---|---|---|
| Client shows Wi-Fi 7 but speeds look like Wi-Fi 6 | No MLO, MCS 12 and 13 unused, or a 160 MHz channel | Association request (multi-link element); the MCS and width in the capture |
| 320 MHz enabled, throughput worse at distance | 3 dB more noise per doubling, lower MCS | SNR at the client; try 160 MHz |
| MLO enabled, no gain | eMLSR or MLSR client with idle links | Section 10 steps 3 to 6 |
| Association succeeds on one link only | Key or security mismatch on another link, or a rejected link | Per-link status in the association response |
| Wi-Fi 7 SSID will not accept older clients | WPA3-only or PMF-required settings | Transition mode; separate SSID for legacy |
| High latency on one band | Busy link, no TTLM, client sticks to it | Per-link airtime; TTLM or steering |
| Throughput stuck near 1 Gbit/s | Switch port or uplink | Check Ethernet speed and the PoE budget |
| Puncturing not used | Client or AP support; the interferer covers the primary channel | EHT operation element bitmap; the primary 20 MHz channel is never punctured |

## 13. Common mistakes

1. **Quoting the 46 Gbit/s peak** instead of the 2-stream rate.
2. **Expecting 4096-QAM** beyond a short, clean link.
3. **Using 320 MHz everywhere**, spending the scarce 6 GHz spectrum and shortening range.
4. **Assuming "MLO" means two links in use.**
5. **Reading the number of BSSIDs as the number of MLDs.**
6. **Forgetting that STR needs isolation**, so the 5 and 6 GHz pair is often NSTR.
7. **Enabling Wi-Fi 7 before WPA3 and PMF**, then losing legacy clients.
8. **Planning 6 GHz coverage from 2.4 GHz experience.**
9. **A 1 Gbit/s uplink** under a multi-gigabit AP.
10. **Testing with one stream and a small window**, then blaming the AP.
11. **Ignoring that features are optional** and client-dependent.
12. **Believing puncturing removes the need for a channel plan.**

## 14. Interview questions

1. **What are the headline Wi-Fi 7 features and what does each cost?** 320 MHz (spectrum, noise), 4096-QAM (SNR, EVM), MRU and puncturing (complexity), MLO (radios, power, isolation), R-TWT (support), mandatory WPA3.
2. **How is an EHT rate computed?** Data subcarriers times bits per subcarrier times code rate times streams, over 12.8 µs plus the guard interval; 2-stream 320 MHz MCS 13 is about 5.8 Gbit/s.
3. **Why is 4096-QAM rarely used?** About 42 dB SNR and a very clean transmitter; it exists only close to the AP.
4. **What is preamble puncturing and what does multi-RU add?** Skipping busy 20 MHz subchannels in an 80 MHz or wider channel; MRU lets one user take the non-contiguous remainder.
5. **What is an MLD and what is shared across links?** One logical device; identity (MLD address), security keys, sequence numbers and the association are per MLD; channel access, NAV, beacons and group keys per link.
6. **Compare STR, eMLSR, NSTR and MLSR.** Simultaneous on two radios with isolation; one radio listening on all links and switching; simultaneous only if aligned; one radio, slow switching.
7. **Why does a client advertising MLO use only one link?** eMLSR or MLSR hardware, an NSTR pair, or idle links; read the association and the traffic.
8. **How does eMLSR work?** An initial control frame on one link, the client switches its chains, transition and padding delays apply, and it returns to listening on all links.
9. **What does TID-to-link mapping do?** Restricts which TIDs use which links, for latency, power or planned link removal.
10. **What does restricted TWT give?** Protected service periods for latency-sensitive traffic, if both AP and client support it.
11. **What security does Wi-Fi 7 require?** WPA3 or OWE, PMF, beacon protection, and keys across all links.
12. **How do you verify MLO in a capture?** The six steps: AP MLD, client MLD, request fields, response status, EML notification, overlapping traffic on both links.
13. **Why does a customer see far less than the peak rate?** Link SNR, MAC overhead, sharing, host limits and the test method.
14. **How many 320 MHz channels does 6 GHz offer?** Three with the full band, one with a 500 MHz allocation, fewer under standard power.

## 15. Scenarios and design questions

**S1. A customer's new Wi-Fi 7 laptop gets the same speed as the old Wi-Fi 6 laptop.** *Where to practise:* capture both associations on a lab AP. *Approach:* check width, MCS, streams, MLO elements, link SNR and the wired uplink; explain what Wi-Fi 7 changes only close to the AP.

**S2. Design the channel plan for 30 Wi-Fi 7 APs on a floor with a 1,200 MHz 6 GHz allocation.** *Where to practise:* a spreadsheet of channels and reuse distance. *Approach:* 3 channels of 320 MHz cannot be reused, so mix 160 and 80 MHz, keep SINR targets, plan puncturing.

**S3. MLO is enabled and latency on voice calls got worse.** *Where to practise:* a lab with a voice stream and a busy link. *Approach:* check whether voice uses the busy link, apply TTLM to keep voice on the cleaner link, check eMLSR switching delay.

**S4. A device reports MLO support but only uses one link in the capture.** *Where to practise:* the six-step capture routine. *Approach:* read the multi-link element, the simultaneous-links field and the NSTR bitmap, then time-overlap analysis.

**S5. Compare STR and eMLSR for a product's client chip.** *Where to practise:* the simple model above with real link parameters. *Approach:* isolation and cost against throughput gain, busy-link benefit, power, and which bands pair well.

**S6. Roll out Wi-Fi 7 to an enterprise that still has WPA2 clients.** *Where to practise:* a pilot SSID. *Approach:* WPA3 transition mode or a separate SSID, PMF settings, test clients, enable 802.11be after the security change.

**S7. A site wants 320 MHz because the brochure says so.** *Where to practise:* a link budget with 320 MHz noise. *Approach:* SNR at the cell edge, spectrum scarcity, co-channel, where the clients really are, offer 160 MHz.

**S8. Explain why puncturing helps a dense network.** *Where to practise:* simulate a partially busy 80 MHz channel. *Approach:* the partial-busy state, rates with and without puncturing, the primary channel rule, the regulator's view.

**S9. Design the verification for a Wi-Fi 7 AP firmware.** *Where to practise:* a lab with several client chips. *Approach:* conformance of elements and frames, MLO modes per client, TTLM and EML actions, security across links, and throughput by MCS and width.

## 16. Strong answers (outlines)

**S1.** Capture the association and a throughput run. Check the negotiated width, streams, MCS distribution and whether the client is an MLD. If the client is at 80 MHz, two streams and MCS 9 to 11, the new laptop can do no better than Wi-Fi 6 on that link. Wi-Fi 7's gains appear at higher SNR (4096-QAM), wider channels, MLO with busy links, and under interference. Check the uplink to the AP and the test method too.

**S2.** With three 320 MHz channels the AP count that can use them is three, and co-channel APs share airtime, so use 320 MHz only at a few lightly shared spots. Assign 160 MHz to most and 80 MHz where density is high, keep a reuse distance of about three cells for the SINR target, enable puncturing, and verify with a 6 GHz survey. Confirm AFC or LPI rules and coverage match.

**S3.** Look at per-link airtime and where voice is sent. With default mapping, voice may use a busy link. Apply a TTLM keeping voice TIDs on the cleaner link, check the eMLSR transition delay (a switch adds latency), and consider turning EMLSR off for the client during calls. Verify with a capture on both links.

**S4.** Read the association: maximum simultaneous links, EMLSR support, NSTR bitmap, then the EML notification. If it is eMLSR or MLSR, one link at a time is the design. Time-overlap analysis of the two links' frames confirms it. The device is not broken, it is a single-radio client.

**S5.** STR needs two full radios and isolation (often impossible for some band pairs), gives the sum of the links, and uses more power. eMLSR needs one radio plus fast switching, gives contention diversity and lower latency under load, and uses less power. A product on a phone often picks eMLSR; a high-end laptop or an AP picks STR where bands allow it.

**S6.** Keep a WPA2 or transition SSID for legacy clients, put Wi-Fi 7 clients on WPA3 (or transition mode), turn on PMF, test client by client, and enable 802.11be rates and MLO after the security change. Document which clients fall back.

**S7.** Compute the link budget at 160 and 320 MHz: 3 dB more noise at 320. Show where the 320 MHz SNR drops below the top MCS. Explain scarcity and co-channel limits. Suggest 160 MHz in most places and 320 MHz where clients are close and the neighbourhood is quiet.

**S8.** In a dense network some 20 MHz subchannel is often busy with a neighbour. Without puncturing, the AP falls back to a narrower channel or waits. With puncturing and MRU it uses the rest (about three quarters of an 80 MHz channel), so the network keeps most of its width. The primary 20 MHz channel is not punctured, and the interferer's regulation limits it.

**S9.** Conformance tests for each element and action frame against a reference, interoperability across several client chips (eMLSR, STR, MLSR), TTLM and EML notification handling, security across all links, association with link rejection, throughput by MCS and width with attenuators, failover when a link degrades, and the six-step capture routine as an automated check.

## 17. Cheat sheet

| Fact | Value |
|---|---|
| New in EHT | 320 MHz, 4096-QAM, MRU, puncturing, MLO, R-TWT, 1024 MPDUs per A-MPDU |
| Rate formula | data subcarriers x bits x code rate x streams / (12.8 + GI) |
| Data subcarriers | 234, 468, 980, 1960, 3920 for 20 to 320 MHz |
| Peak | 46.1 Gbit/s (16 streams, 320 MHz, MCS 13) |
| 2-stream client | 2.88 Gbit/s at 160 MHz, 5.76 Gbit/s at 320 MHz |
| 4096-QAM | 12 bits, +20 percent, needs about 42 dB SNR |
| 320 MHz | 6 GHz only; 3 channels with the full band, 1 with 500 MHz |
| Puncturing | 80 MHz and above; the primary 20 MHz channel cannot be punctured |
| MLD | One MAC, several links; per-MLD identity, keys, sequence numbers |
| Per-link | Channel access, NAV, beacons, group keys |
| Multi-link element | ext ID 107; MLD address, EML and MLD capabilities, per-STA profiles |
| Max simultaneous links | Number of links usable at once, minus one |
| Modes | MLSR, eMLSR, STR, NSTR, eMLMR; shipped clients: eMLSR or STR |
| eMLSR | One radio listens on all links; initial control frame, padding and transition delay |
| TTLM | Per-TID link bitmap; Protected EHT action |
| Security | WPA3 or OWE, PMF, beacon protection, GCMP-256 |
| Deploy | 802.3bt PoE, multigigabit uplink, 6 GHz coverage survey |

## 18. Sources and caveats

Web sources used for this tutorial: the arXiv [IEEE 802.11be Wi-Fi 7 feature summary](https://arxiv.org/html/2309.15951v3) (320 MHz, 4096-QAM, MRU, preamble puncturing, STR and NSTR, restricted TWT, 1024-MPDU aggregation); the Cisco Meraki [Wi-Fi 7 technical guide](https://documentation.meraki.com/MR/Wi-Fi_Basics_and_Best_Practices/Wi-Fi_7_(802.11be)_Technical_Guide) (the MLO modes clients ship, 320 MHz channel counts, mandatory puncturing, the 42 dB against 31 dB SNR figures, security requirements and deployment advice); and the arXiv [STR and eMLSR comparison](https://arxiv.org/pdf/2501.04149). The mrncciew Wi-Fi 7 articles and the Candela course are the syllabus.

Caveats. The rate function, the multi-RU rates, the MLO model and the multi-link element builder were run, and `tshark` 4.2 decoded the element built here (MLD address, EMLSR support and the simultaneous-links field). The MLO model uses assumed parameters (efficiency 0.65, 4 ms bursts, a 60 µs control frame) and shows the ordering, not a product's performance. EHT-MCS 14 and 15, the eMLSR delay values, the action frame numbers, the BSSID arrangement and the details of which MRUs are legal are from my memory of the amendment and were not re-read from the standard. Client behaviour (which modes ship, what a given laptop does) varies by chip and driver release, and the MacBook-style capture cited in section 6.2 is one device pair. Check the vendor's own capability sheet for any client you plan around.
