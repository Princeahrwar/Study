# WF 01: The Wi-Fi landscape and the anatomy of an access point

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela sessions 1a, 1b, 1c and 1d · Time: about 6 hours · You need: [NET 01](../01_network_models_and_layer2.md) (Ethernet frames, MAC addresses, a bridge and 802.11 framing at a glance), [NET 02](../02_ip_routing_and_layer3.md) (NAT and PAT) and [NET 08](../08_application_protocols_dns_http_tls.md) (the DHCP exchange). A Wi-Fi network is not one thing. It is a radio standard that moved by a factor of **854** in peak rate in twenty-five years (54 Mbit/s to 46,118), a set of **topologies** that decide who talks to whom and over how many hops, two **organisations** that each own half of what a customer calls "Wi-Fi", and a box, the **access point**, that is a radio, a switch, a router, a DHCP server and a firewall in one case. This tutorial takes the four in order and puts a computed or captured number on each.*

"Walk me through what is inside a home router, and say where the Wi-Fi ends and the router begins" opens many wireless and device interviews, and "what is the difference between IEEE 802.11 and the Wi-Fi Alliance" is the first follow-up. A Staff level answer connects four things: the **arithmetic** behind a peak rate, the **frame bits** that make a topology (ToDS, FromDS, the BSS flags), the **split of work** between hardware that must answer in microseconds and software that can take milliseconds, and the **services** that make an access point a router. The Candela course covers each of them in a short class note; this tutorial goes to the formula, the frame and the running process.

**Eight honest limits, stated first.**

1. **There is no radio here, and no wireless stack in this kernel.** Linux 6.18 in this Firecracker VM has no `mac80211_hwsim`, no `cfg80211` and no wireless driver (section 5.2 prints the kernel configuration and the empty `/sys/class/ieee80211`). Nothing in this tutorial is an over-the-air capture. The 802.11 frames are **built by hand with scapy** and read back by `tshark`; the "wireless" link of the router experiment is a `veth` pair.
2. **Peak rates are computed from parameters, not measured.** The generation table is `subcarriers × bits × coding rate × streams ÷ symbol time` applied to numbers I know; the **years, the names and the parameters are from memory** and I did not re-read the standards for this tutorial.
3. **The amendment alphabet and the Wi-Fi Alliance programmes are from memory** (section 3). So is every statement about what a given chip or product does.
4. **The backhaul model assumes a throughput efficiency of 0.65 and that nodes up to three hops apart hear each other.** Both are assumptions printed with the result; WF 03 measures the efficiency of the MAC and WF 07 returns to mesh planning.
5. **The router is built from parts**: a Linux bridge for the switch, `dnsmasq` for DHCP, `nft` for NAT, three network namespaces for the client, the router and the ISP. It shows the services and the packets; it is not a commercial router's software.
6. **One host, two vCPUs.** The DHCP and NAT runs are exact counts of what happened; the address `dnsmasq` handed the second client (`.52` in the frozen run, `.53` in an earlier one, never `.51`) is its choice and I did not trace why. Likewise which of the two NAT flows has its port rewritten, and to what, depends on which connection arrived second and on a port the kernel picks (15,380 in one run, 6,902 in the frozen one).
7. **Hardware block diagrams are from memory**: the radio front end, the baseband, the lower MAC engine and the offload hardware of real chips are described from general knowledge, not from a datasheet.
8. **Regulatory limits, channel plans and power levels are left to WF 02 and WF 11**, where they are used.

By the end you can:

- **compute the peak rate of any generation from its parameters** (computed: **54, 600, 6,933.3, 9,607.8 and 46,117.6 Mbit/s** for 802.11a/g, n, ac, ax and be at their widest, and **150.0, 433.3, 600.5 and 720.6** for one stream at 80 MHz, or 40 MHz for n, which is what a phone gets);
- **say what each generation changed** and which change gave how much: a wider channel, a denser constellation, more streams, a longer symbol (section 1.2);
- **read a topology from the frame** (captured, built with scapy and decoded by `tshark`: an access point beacon has **ESS = 1**, an ad hoc beacon **IBSS = 1**, a mesh beacon **both 0 plus a Mesh ID**; a station to access point data frame has **ToDS = 1** and the destination in `addr3`; a bridge frame has **both bits set and four addresses**);
- **say how throughput falls with hops** (modelled: one radio repeating on one channel gave **600, 300, 200, 150, 120 and 100 Mbit/s** at one to six hops; a dedicated backhaul radio held **200**; a wire held **600**);
- **separate IEEE from the Wi-Fi Alliance** and the amendment letters from the marketing names (section 3);
- **say where each job of an access point is done**: radio, baseband, lower MAC in hardware or firmware (the acknowledgement must leave **16 µs** after a frame at 5 GHz), upper MAC in `hostapd`, and the router services in Linux (sections 4 to 6);
- **trace a client joining and sending a packet through a home router** (captured: **four DHCP frames**, the offer `192.168.1.50` from server `192.168.1.1`, a default route, and two clients using **source port 40,000** to the same server, which NAT told apart as **40,000** and **6,902** on the public side);
- **size a DHCP pool and a NAT table** (computed: a `/24` holds 253 clients; **240,000** TIME_WAIT entries for 2,000 clients at one new connection a second, against a table limit of **262,144**).

## Where the numbers come from

Each experiment is a script in `prog/` with its output frozen in `v/`, run with `run.sh`. `gen.py` is the generation table and `s1_gen.sh` runs it; `s2_topo_frames.py` and `s2_topo.sh` build seven 802.11 frames with scapy and dissect them with `tshark`; `s3_router.sh` builds the home router in four namespaces; `backhaul.py` and `s4_backhaul.sh` are the hop model; `s5_anatomy.sh` reads what software and kernel support this VM has. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, modelled or computed here is marked **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| Generation table (`s1_gen.sh`) | 6 generations, under 1 s |
| Topology frames (`s2_topo.sh`) | 7 frames built and dissected, about 2 s |
| Home router (`s3_router.sh`) | 4 namespaces, 2 DHCP exchanges, 2 NAT flows, about 15 s |
| Backhaul model (`s4_backhaul.sh`) | 6 hop counts, under 1 s |
| Anatomy probe (`s5_anatomy.sh`) | version strings and a kernel configuration read, under 1 s |

### How this maps to the Candela course

| Candela session | Where it is covered here | What this tutorial adds |
|---|---|---|
| 1a Evolution of Wi-Fi | Section 1 | The rate formula for every generation, and what each generation changed |
| 1b Network topologies | Section 2 | Frame evidence for each topology, the four address layout, a backhaul model, Wi-Fi Direct and P2P |
| 1c Standards alphabet soup | Section 3 | IEEE against Wi-Fi Alliance against regulators, the lettered amendments grouped by job, the Wi-Fi Alliance programmes |
| 1d Functional blocks of an AP | Sections 4 and 5 | The timing argument for the split, FullMAC against SoftMAC, a router built from parts and captured |

---


## 1. The evolution of Wi-Fi (Candela 1a)

### 1.1 One formula for every generation

Every OFDM generation of Wi-Fi, from 802.11a on, reaches its peak rate the same way. A symbol is sent every `T` microseconds on a number of **data subcarriers** `N`; each subcarrier carries `b` coded bits; a fraction `R` of those are payload; and each of `S` spatial streams does it in parallel:

```text
rate = N × b × R × S ÷ T
```

The rest of the physical layer (WF 02) is a table of which values of `N`, `b`, `R`, `S` and `T` are allowed. Applying the formula to the widest configuration each standard allows, with the top constellation:

```text
standard  year  band GHz     MHz subcar  bits   code  SS  sym us  peak Mbit/s  modulation
802.11a   1999  5             20     48     6    3/4   1     4.0         54.0  64-QAM
802.11g   2003  2.4           20     48     6    3/4   1     4.0         54.0  64-QAM
802.11n   2009  2.4, 5        40    108     6    5/6   4     3.6        600.0  64-QAM
802.11ac  2013  5            160    468     8    5/6   8     3.6       6933.3  256-QAM
802.11ax  2021  2.4, 5, 6    160   1960    10    5/6   8    13.6       9607.8  1024-QAM
802.11be  2024  2.4, 5, 6    320   3920    12    5/6  16    13.6      46117.6  4096-QAM
802.11b (1999, 2.4 GHz, 20 MHz, DSSS/CCK, one stream): 11 Mbit/s, a fixed rate set, not an OFDM formula

the same formula for one stream at 80 MHz, the rate of a typical phone, by generation (best MCS)
802.11n (40 MHz)    108 subcarriers,  6 bits, symbol  3.6 us:  150.0 Mbit/s
802.11ac            234 subcarriers,  8 bits, symbol  3.6 us:  433.3 Mbit/s
802.11ax            980 subcarriers, 10 bits, symbol 13.6 us:  600.5 Mbit/s
802.11be            980 subcarriers, 12 bits, symbol 13.6 us:  720.6 Mbit/s
```

Check one row by hand. 802.11n at 40 MHz has 108 data subcarriers, 6 bits each (64-QAM), coding 5/6, four streams and a 3.6 µs symbol: `108 × 6 × 5/6 × 4 ÷ 3.6 µs = 600` Mbit/s. And 802.11be: `3920 × 12 × 5/6 × 16 ÷ 13.6 µs = 46,117.6` Mbit/s. From the first row to the last is **854 times** (`46,117.6 ÷ 54`) in 25 years (years **from memory**).

The 802.11b row is not in the formula: it used a single carrier with direct-sequence spread spectrum and complementary code keying, with a fixed set of rates of 1, 2, 5.5 and 11 Mbit/s (**from memory**). OFDM arrived with 802.11a.

### 1.2 What each generation changed, factor by factor

The formula says where a gain can come from. `decomp.py` divides each generation's parameters by the previous one:

```bash
cd "$(dirname "$0")"; python3 decomp.py
```

```text
step        subcarr   bits  coding  streams     symbol  product    check
a to n       2.250x 1.000x  1.111x   4.000x     1.111x  11.111x  11.111x
n to ac      4.333x 1.333x  1.000x   2.000x     1.000x  11.556x  11.556x
ac to ax     4.188x 1.250x  1.000x   1.000x     0.265x   1.386x   1.386x
ax to be     2.000x 1.200x  1.000x   2.000x     1.000x   4.800x   4.800x
```

Read each line as a product of five factors.

- **a to n (11.1x):** the channel doubled (108 against 48 data subcarriers, 2.25x, because 40 MHz carries more than twice the 20 MHz count), **four streams** (4x), a better code (5/6 against 3/4, 1.11x) and a shorter guard interval (3.6 against 4.0 µs, 1.11x). Most of the gain is MIMO and width.
- **n to ac (11.6x):** wider again (**160 MHz**: 4.33x), a denser constellation (**256-QAM**, 1.33x) and twice the streams (8). A client of the time had one or two streams, so the real gain was width and the constellation.
- **ac to ax (1.39x only):** subcarriers 4.19x and bits 1.25x, but the **symbol grew from 3.6 to 13.6 µs**, a factor of 3.8 slower, which is `0.265x` in the table. The longer symbol has four times the subcarriers in the same width and tolerates a longer guard interval, which makes it robust outdoors and in dense deployments. **802.11ax raised the peak by 39 percent; its gain was in efficiency** (OFDMA, uplink multi-user, spatial reuse, target wake time, WF 06), not in the headline rate.
- **ax to be (4.8x):** twice the width (320 MHz, 2.0x), **4096-QAM** (1.2x) and 16 streams (2x). The 16 streams are a ceiling the standard allows; consumer silicon is mostly 2 to 4 (**from memory**).

The decomposition is the interview answer to "why did Wi-Fi 6 not double Wi-Fi 5": the formula says its peak went up 1.39 times, and the headline rates agree (9.6 against 6.9 Gbit/s).

### 1.3 The rate a phone gets, and what the class label means

Peak is for the widest channel and the most streams. A phone has one or two. The same formula for **one stream at 80 MHz** (40 MHz for n, which has no 80 MHz):

```text
the same formula for one stream at 80 MHz, the rate of a typical phone, by generation (best MCS)
802.11n (40 MHz)    108 subcarriers,  6 bits, symbol  3.6 us:  150.0 Mbit/s
802.11ac            234 subcarriers,  8 bits, symbol  3.6 us:  433.3 Mbit/s
802.11ax            980 subcarriers, 10 bits, symbol 13.6 us:  600.5 Mbit/s
802.11be            980 subcarriers, 12 bits, symbol 13.6 us:  720.6 Mbit/s
```

A one stream 802.11ax phone at 80 MHz tops out at **600.5 Mbit/s** and a Wi-Fi 7 phone at **720.6**, which is a factor of 4 to 5 under the numbers of the table. A client's rate is set by **its own** antennas: a two-antenna laptop talking to a four-antenna access point gets two streams.

Routers are sold with a class label such as AX1800 or AX3000 that **adds the peak rate of the bands**. The arithmetic (label meanings **from memory**, rates computed):

```text
class labels of consumer routers add the peak rate of each band (labels from memory, rates computed)
2.4 GHz 40 MHz 2 streams:    573.5 Mbit/s
5 GHz   80 MHz 2 streams:   1201.0 Mbit/s   dual band sum  1774.5 (sold as AX1800)
5 GHz  160 MHz 2 streams:   2402.0 Mbit/s   dual band sum  2975.5 (sold as AX3000)
one client on one band sees only that band: 1201.0 Mbit/s for a 2 stream phone at 80 MHz, 600.5 for 1 stream
```

2.4 GHz at 40 MHz and two streams is **573.5**; add 5 GHz at 80 MHz and two streams (**1,201.0**) and the sum **1,774.5** is sold as AX1800; add 5 GHz at 160 MHz (**2,402.0**) and **2,975.5** is sold as AX3000. No client can use both bands at once at that rate, because a client's radio sits on one band. The label is a statement about the box, and the line in the experiment that matters to a user is the last: **1,201.0 Mbit/s** for a two-stream phone on 5 GHz at 80 MHz, and half of that for one stream.

And peak PHY rate is not throughput. Contention, the interframe spaces, the preamble, the acknowledgement and the headers take a large share of the airtime; a single client at a good rate typically sees around **half to two thirds** of the PHY rate as TCP goodput (**from memory**; WF 03 measures it). Section 2.3 uses an assumed **0.65**.

### 1.4 The generations and their names

The Wi-Fi Alliance gave the generations numbers in 2018 so that customers need not remember letters (**from memory**):

| IEEE | Wi-Fi Alliance name | Bands | What it introduced (**from memory** except the arithmetic above) |
|---|---|---|---|
| 802.11 (1997) | none | 2.4 GHz | 1 and 2 Mbit/s, the MAC (CSMA/CA), the basic service set |
| 802.11b (1999) | none | 2.4 | DSSS and CCK, 11 Mbit/s |
| 802.11a (1999) | none | 5 | OFDM, 54 Mbit/s |
| 802.11g (2003) | none | 2.4 | OFDM on 2.4 GHz, compatible with b (and so protection mechanisms when a b client is present) |
| 802.11n (2009) | **Wi-Fi 4** | 2.4, 5 | MIMO up to 4 streams, 40 MHz, frame aggregation (A-MPDU), block acknowledgement |
| 802.11ac (2013) | **Wi-Fi 5** | 5 | 80 and 160 MHz, 256-QAM, downlink multi-user MIMO (wave 2), up to 8 streams |
| 802.11ax (2021) | **Wi-Fi 6**, and **Wi-Fi 6E** when it uses 6 GHz | 2.4, 5, 6 | OFDMA, uplink multi-user, 1024-QAM, BSS colouring, target wake time, 6 GHz |
| 802.11be (2024) | **Wi-Fi 7** | 2.4, 5, 6 | 320 MHz, 4096-QAM, multi-link operation, multiple resource units, preamble puncturing |
| 802.11bn | Wi-Fi 8 (in development) | | ultra high reliability: lower tail latency and fewer retransmissions, not a higher peak |

Two other radios share the 802.11 name and are not what a router sells: **802.11ah (HaLow)** in sub-GHz bands for long range and low power, and **802.11ad and 802.11ay (WiGig)** at 60 GHz for short range and several Gbit/s (**from memory**).

A practical consequence of the history: **every access point supports its predecessors**, because the older clients are still in the building. The oldest client on a channel can set the rules for everyone on it. A client at 1 Mbit/s occupies the air for `1 MB × 8 ÷ 1 Mbit/s = 8,000 ms` where a 1,201 Mbit/s client needs **6.7 ms** (problem E), a **1,200 times** difference in airtime for the same bytes, and the next sections of this track keep returning to that.

---


## 2. Network topologies (Candela 1b)

### 2.1 The five shapes

A **basic service set (BSS)** is the group of stations that share one radio channel and one identity, the **BSSID** (in an access point it is the radio's MAC address). Everything in Wi-Fi topology is a way of arranging BSSs.

| Topology | Who relays | The identity | Used for |
|---|---|---|---|
| **Infrastructure BSS** | Every frame goes through the access point (AP), even between two stations in the same room | BSSID = the AP's address; the beacon says **ESS** | Homes, offices, hotspots: nearly all Wi-Fi |
| **ESS** (extended service set) | Several APs with the same SSID joined by a wired or wireless **distribution system**; a station roams between them | One SSID, a different BSSID per AP | Campus and enterprise (WF 05 for roaming) |
| **IBSS** (ad hoc, independent BSS) | No AP; stations talk directly, and all share the duty of beaconing | A random BSSID made by the first station; the beacon says **IBSS** | Two devices, field work, early file sharing |
| **Mesh BSS** (802.11s) | Every node forwards for the others by a routing protocol in the MAC (HWMP, from memory) | A **Mesh ID** in the beacon, no ESS or IBSS bit | Large areas without cables; the standard form of mesh |
| **Bridge (WDS)** | Two APs relay a wired segment across the air with a **four address frame** | Both radios' addresses in the frame | Point to point links; repeaters; the backhaul of a vendor mesh |

Two more arrangements are built from these and have no BSS type of their own. A **repeater or range extender** is a station that associates to an AP and then runs an AP of its own, forwarding between the two. A **vendor mesh** (a set of nodes sold as one system) is usually infrastructure BSSs on the client side and a private backhaul between nodes; it is standard Wi-Fi underneath, with a controller or a protocol on top (WF 07; the interoperable version is Wi-Fi Alliance **EasyMesh**, from memory). **Wi-Fi Direct** (P2P) lets two devices connect without a router by one of them acting as a soft AP (**from memory**: the group owner), which is how a phone casts to a television; **Wi-Fi Aware** is discovery without association (**from memory**).

### 2.2 The topology is in the frame

The frame control field of an 802.11 header has two bits, **ToDS** and **FromDS**, and the management beacon has capability bits **ESS** and **IBSS**. Their combinations decide how many addresses the header carries and what each one means. `s2_topo_frames.py` builds one frame of each kind with scapy, writes them to a capture file and has `tshark` read the fields back, so the table below is the dissector's reading of real bytes, not my reading of a diagram:

```bash
cd "$(dirname "$0")"; python3 -W ignore s2_topo_frames.py 2>&1
```

```python
def beacon(src, ssid, cap, extra=b""):
    return (RadioTap() / Dot11(type=0, subtype=8, addr1=BC, addr2=src, addr3=src) /
            Dot11Beacon(cap=cap, beacon_interval=100) / Dot11Elt(ID=0, info=ssid) / rates / chan / extra)
```

```python
def data(fc_to, fc_from, a1, a2, a3, a4=None):
    d = Dot11(type=2, subtype=0, FCfield=(0x01 if fc_to else 0) | (0x02 if fc_from else 0), addr1=a1, addr2=a2, addr3=a3)
    if a4: d.addr4 = a4
    return RadioTap() / d / payload
```

```text
frame                                                          type  ToDS  FromDS  ESS  IBSS mesh id  addr1 (RA) addr2 (TA) | DA SA BSSID
beacon: access point (infrastructure BSS)                      0x0008 False   False True False -        ff:ff:ff:ff:ff:ff :01 | ff:ff:ff:ff:ff:ff :01 :01
beacon: ad hoc station (IBSS)                                  0x0008 False   False False  True -        ff:ff:ff:ff:ff:ff :11 | ff:ff:ff:ff:ff:ff :11 :11
beacon: mesh point (mesh BSS)                                  0x0008 False   False False False meshnet  ff:ff:ff:ff:ff:ff :11 | ff:ff:ff:ff:ff:ff :11 :11
data: station to access point (ToDS)                           0x0020  True   False    -     - -        :01 :11 | :aa :11 :01
data: access point to station (FromDS)                         0x0020 False    True    -     - -        :11 :01 | :11 :aa :01
data: ad hoc, station to station                               0x0020 False   False    -     - -        :12 :11 | :12 :11 :cc
data: wireless bridge or WDS (ToDS and FromDS, four addresses) 0x0020  True    True    -     - -        :01 :11 | :12 :aa -
```

(Addresses are shortened to their last byte: `:01` is the AP, `:11` and `:12` are stations, `:aa` is a host behind the AP, `:cc` is the ad hoc BSSID.)

**The three beacons.** The AP beacon has **ESS true**; the ad hoc station's has **IBSS true**; the mesh point's has **neither** and carries a **Mesh ID** element (`meshnet`). That is how a scan tells the three apart before any association (the scan itself is WF 03).

**The four data frames.** Each frame has three or four address fields, `addr1` to `addr4`. What they mean depends on the two bits:

| ToDS | FromDS | Case | `addr1` | `addr2` | `addr3` | `addr4` |
|---|---|---|---|---|---|---|
| 0 | 0 | ad hoc, or between two stations by a direct link | **DA** | **SA** | BSSID | none |
| 1 | 0 | station to AP | **BSSID** (the AP) | **SA** | **DA** | none |
| 0 | 1 | AP to station | **DA** | **BSSID** | **SA** | none |
| 1 | 1 | AP to AP (WDS, mesh data) | **RA** (receiver) | **TA** (transmitter) | **DA** | **SA** |

Check each against the captured lines. In `data: station to access point (ToDS)` the capture shows `addr1 :01` (the AP), `addr2 :11` (the station), then `DA :aa`, `SA :11`, `BSSID :01`: the **ultimate destination is in `addr3`**, because `addr1` has to be the next receiver on the air. In the FromDS frame the **source is in `addr3`**: `DA :11 SA :aa BSSID :01`. The ad hoc frame has both bits clear and puts the BSSID in `addr3` (`:cc`). And the **wireless bridge** frame has both bits set, so there is no BSSID in the header at all (the dissector prints `-` for it) and a fourth address carries the source host: `RA :01, TA :11, DA :12, SA :aa`.

**Why three addresses at all.** In an Ethernet frame the receiver and the destination are the same device. On Wi-Fi they are not: a station sending to a server on the wired side must address its frame to the AP (the receiver that will acknowledge it) while naming the server as the destination for the AP to forward to. So `addr1` and `addr2` are always the **receiver address (RA)** and **transmitter address (TA)** of this hop, and the end-to-end addresses sit in `addr3` and `addr4`. WF 03 returns to this header with the rest of the MAC.

### 2.3 Backhaul and what a hop costs

When a node reaches the internet over the air through another node, that link is the **backhaul**, and a node that serves clients and also forwards the backhaul on the same radio has to do both in the same airtime. Every frame of a client behind two hops is sent **twice**, once per hop, and the two transmissions cannot overlap if the nodes hear each other. `backhaul.py` is a deliberately small model; its assumptions are in the output itself.

```bash
cd "$(dirname "$0")"; python3 backhaul.py
```

```text
model: a line of nodes, every link has rate R = 600 Mbit/s when it has the air to itself
  repeater:  one radio per node carries both the client side and the next hop, all on one channel;
             every frame is sent once per hop and the hops hear each other, so they take turns: R / hops
  dedicated: a second radio carries only the backhaul, on one channel; nodes up to 3 hops apart still
             hear each other (an assumption), so the backhaul shares the air among 3 hops at most: R / min(hops, 3)
  wired, or a different channel for every hop: no sharing: R

 hops           repeater    dedicated radio  wired/own channel
    1        600.0 Mbit/s        600.0 Mbit/s        600.0 Mbit/s
    2        300.0 Mbit/s        300.0 Mbit/s        600.0 Mbit/s
    3        200.0 Mbit/s        200.0 Mbit/s        600.0 Mbit/s
    4        150.0 Mbit/s        200.0 Mbit/s        600.0 Mbit/s
    5        120.0 Mbit/s        200.0 Mbit/s        600.0 Mbit/s
    6        100.0 Mbit/s        200.0 Mbit/s        600.0 Mbit/s
```

A **repeater** (one radio, one channel) gives `R ÷ hops`: **600, 300, 200, 150, 120 and 100 Mbit/s** at one to six hops. A node with a **dedicated backhaul radio** keeps the client side clear, but the backhaul still shares the air between neighbouring hops, so it falls as far as three hops and then holds at **200** (an assumption that hops three apart no longer hear each other; if they do, it keeps falling). A **wire**, or a different channel for every hop, holds **600**. This is the arithmetic behind three pieces of advice: put a wire behind the nodes if you can; use a separate radio (often a second 5 GHz or a 6 GHz radio on a tri-band node) for the backhaul if you cannot; and do not chain more than two or three hops. The model ignores contention, rate adaptation and interference, so it is a ceiling for the shape of the curve, not a prediction. Problem C puts numbers on a real node.

### 2.4 Which topology for which job

| Need | Topology | Why |
|---|---|---|
| A house or a flat | One AP, infrastructure BSS | One BSSID and nothing to roam between |
| A house with a dead corner | Wired nodes of an ESS, or a mesh with a dedicated backhaul | A repeater halves the rate at each hop (section 2.3) |
| An office or campus | An ESS of many APs on a wired network, one SSID, 802.11k, v and r (WF 05) | Roaming and capacity from many small cells |
| A stadium or a classroom | An ESS with many APs on narrow channels and low power | Capacity comes from reuse, not from peak rate (WF 02) |
| A factory, a warehouse | An ESS with a controller, sometimes a mesh for the last link | Moving devices, cables hard to run |
| Two devices, no network | Wi-Fi Direct or ad hoc | No infrastructure to depend on |
| A building to building link | Bridge (WDS) on a point to point radio, directional antennas | The four address frame carries the wired segment |
| A disaster or a temporary site | 802.11s mesh or a vendor mesh | Nodes find each other and reroute |

---


## 3. The standards alphabet (Candela 1c)

### 3.1 Who owns what

Three kinds of body decide what a Wi-Fi product is allowed to do, and a customer calls the result "Wi-Fi":

| Body | What it produces | Example |
|---|---|---|
| **IEEE 802.11 working group** | The standard: frame formats, the physical layer, the MAC, state machines. Written as **amendments** identified by lower case letters | 802.11n, 802.11ax, 802.11i |
| **Wi-Fi Alliance (WFA)** | An interoperability programme: test plans, a certification lab network, the logo and the generation names. Certifies a **subset** of the standard, in combinations vendors agreed on | Wi-Fi CERTIFIED 6, WPA3, Passpoint |
| **Regulators** (FCC in the United States, ETSI and national bodies in Europe, MIC in Japan, and so on; **from memory**) | Which frequencies, at what power, indoors or outdoors, with which radar avoidance | The 6 GHz rules, DFS (WF 02) |

Other bodies supply parts of a Wi-Fi product without being "Wi-Fi" (**from memory**): the **IETF** writes DHCP, DNS, RADIUS and EAP; **3GPP** defines the cellular side of Wi-Fi calling and of EAP-SIM and EAP-AKA; the **Wireless Broadband Alliance** runs OpenRoaming (WF 04); the **Broadband Forum** defines how a service provider manages a home gateway (TR-069 and its successor TR-369); and **OpenWrt** and the kernel's wireless subsystem are the software most open access points run (WF 10).

The distinction matters in an interview and at work. A datasheet line "802.11a/b/g/n/ac/ax" says which **amendments the radio implements**. "Wi-Fi CERTIFIED 6" says a lab ran the Alliance's tests on that product. A feature in the standard that no certification programme covers (many optional ones) may exist in silicon and interoperate with nothing.

### 3.2 The lifetime of a feature

A feature reaches a customer through five gates, each by a different body (**from memory**; the example dates are as I remember them):

1. **A task group** in IEEE 802.11 drafts an amendment (D0.1, D1.0 and so on) and the working group votes on each draft in a letter ballot until the comments are resolved.
2. **Silicon vendors build to a draft** as it stabilises, usually before the amendment is published. 802.11ax was certified from a late draft in 2019 and published in 2021; Wi-Fi 7 products were certified from a draft of 802.11be before its 2024 publication.
3. **The Wi-Fi Alliance writes a certification** and runs interoperability events; products that pass can carry the generation name.
4. **A regulator** permits the band or the power. The 6 GHz band was opened in the United States in 2020 (**from memory**), which is why "Wi-Fi 6E" is a regulatory event as much as a technical one: the same 802.11ax radio, on a new band.
5. **Periodically the amendments are merged** into a new revision of the base standard (a "rollup": 802.11-2007, -2012, -2016 and -2020, **from memory**), after which the older letters are no longer cited on their own.

### 3.3 The lettered amendments, grouped by job

There are more letters than a person can hold, but they fall into a handful of jobs (**from memory**, and the list is of those worth knowing, not complete):

| Job | Amendments | What they added |
|---|---|---|
| **Physical layer** | b, a, g, n, ac, ax, be, bn; also p (vehicles, 5.9 GHz), ah (HaLow), ad and ay (60 GHz), af (TV white space), ba (wake up radio) | Rates, bands and widths (section 1) |
| **Quality of service** | e | EDCA with four access categories, block acknowledgement, power save by APSD (WMM is its Wi-Fi Alliance profile); WF 03 |
| **Security** | i, w, ai | i: the robust security network (WPA2, CCMP, the four way handshake); w: protected management frames; ai: fast initial link setup (FILS); WF 04 |
| **Radio management** | h, k, v | h: dynamic frequency selection and transmit power control; k: neighbour reports and measurement; v: network assisted BSS transition; WF 05 |
| **Roaming** | r | Fast BSS transition, a keyed hand off in one round trip; WF 05 |
| **Mesh** | s | Mesh BSS, a routing protocol in the MAC (section 2); WF 07 |
| **Interworking** | u | Network discovery and selection (the basis of Passpoint); WF 04 |
| **Direct links** | z | TDLS, two stations in one BSS exchange frames without the AP |
| **Location and sensing** | az, bf | az: positioning (round trip timing); bf: WLAN sensing; WF 07 |
| **Next reliability** | bn | Wi-Fi 8: ultra high reliability |

Rules that save confusion: the lower case letter names an **amendment**; an upper case letter (802.11F, 802.11T) was a recommended practice or a withdrawn work item and is not a feature; and two letters after `z` (`aa`, `ab` ... `az`, `ba` ... ) are the next names after the alphabet ran out. A letter that stands alone in a vendor list (`k/v/r`, `a/b/g/n/ac/ax`) is the set of features the product implements.

### 3.4 The Wi-Fi Alliance programmes worth knowing

The programmes (**from memory**) fall in two groups. **Generations**: Wi-Fi CERTIFIED n, ac, 6 (and 6E), 7. **Everything else a product can be certified for**:

| Programme | What it certifies |
|---|---|
| **WPA2, WPA3** (Personal and Enterprise), **Enhanced Open** | The security suites; Enhanced Open is OWE, an unauthenticated key exchange for open networks (WF 04) |
| **WMM**, **WMM Power Save** | The QoS access categories and the power save of 802.11e |
| **Wi-Fi Protected Setup (WPS)** | Pairing by a push button or a PIN (and its weaknesses, WF 04) |
| **Device Provisioning Protocol (DPP)** programme | Provisioning a device with a QR code instead of typing a key |
| **Passpoint** | Automatic secure joining of a hotspot by a credential (built on 802.11u; WF 04) |
| **Wi-Fi Agile Multiband** | The set of 802.11k, v and r behaviours for steering and roaming (WF 05) |
| **Wi-Fi Direct**, **Wi-Fi Aware** | Peer to peer connections and discovery (section 2) |
| **EasyMesh** | Interoperable multi-AP systems with a controller and agents (WF 07) |
| **Wi-Fi Optimized Connectivity**, **Wi-Fi Vantage** | Faster roaming and discovery for managed networks (from memory, with less confidence about the details) |

### 3.5 The letters on the air

The generation letters are not only paperwork: each one is announced on the air by an **information element** that an access point puts in its beacon, so that a client can see what the network supports before it joins. `s6_ies.py` builds a beacon with the elements of each generation (their bodies are zeros, placeholders: only the element identifiers matter here) and has `tshark` name them:

```bash
cd "$(dirname "$0")"; python3 -W ignore s6_ies.py
```

```text
information element ids in the beacon: 0,1,45,191
extension ids (for element 255):       35,108

Tag: Supported Rates 1(B), 2(B), 5.5(B), 11(B), 6, 9, 12, 18, [Mbit/sec]
Tag: HT Capabilities (802.11n D1.10)
Tag: VHT Capabilities
Ext Tag: HE Capabilities
Ext Tag: EHT Capabilities (802.11be D3.0)
```

The legacy **Supported Rates** element (identifier 1) carries the 802.11a, b and g rates; **HT Capabilities** (45) announces 802.11n; **VHT Capabilities** (191) announces 802.11ac; and the newest generations use **extension elements** under identifier 255, **HE Capabilities** (extension 35, 802.11ax) and **EHT Capabilities** (extension 108, 802.11be). The extension mechanism exists because the one byte element identifier was running out. The dissector also prints the draft version of the standard that it implements (`D1.10`, `D3.0`), a trace of the draft based history of section 3.2. A capture of a real beacon (WF 08) shows these elements full of the actual capabilities: channel widths, streams, guard intervals, supported MCS. The tutorial on the PHY (WF 02) explains what is in them.

---


## 4. The blocks of an access point (Candela 1d)

An access point is a stack of blocks, each with a different deadline. From the antenna inward (the block contents are **from memory**; the split of deadlines is measured in section 5):

```text
 antennas
    |
 RF front end        filters, power amplifier (PA), low noise amplifier (LNA), transmit/receive switch
    |
 transceiver         mixers, ADC and DAC (analogue to digital and back), frequency synthesiser
    |
 baseband (PHY)      OFDM modem: FFT and IFFT, channel estimation, MIMO detection, FEC encoder and decoder
    |                time scale: one symbol, 4 to 13.6 microseconds
 lower MAC           interframe spaces and backoff, ACK, block ACK and CTS replies, aggregation, encryption
    |                engine, transmit queues, retries and rate control hardware assist
    |                time scale: microseconds (SIFS 16 us at 5 GHz)
 upper MAC           scanning, authentication, association, beacon contents, power save buffering,
    |                802.1X, management frames, BSS parameters
    |                time scale: milliseconds
 host processor      Linux: bridge, routing, NAT, DHCP, DNS, firewall, QoS, management agents
    |                time scale: milliseconds to seconds
 switch and ports    Ethernet PHYs and a switch core (the LAN ports), the WAN port
```

### 4.1 Radio and baseband

The **RF front end** and **transceiver** turn the digital samples into a signal at 2.4, 5 or 6 GHz at a regulated power and back. A dual or tri band access point has one transceiver chain per band, and a MIMO device has one per spatial stream (a "4x4" radio has four chains). The **power amplifier** sets the transmit power (regulator limits, WF 11) and the **low noise amplifier** sets how weak a signal can be heard. The **baseband** is the digital signal processing: it makes the OFDM symbol (WF 02), estimates the channel, separates the spatial streams and runs the error correcting code. It runs a fixed pipeline in dedicated hardware, because the rate of the numbers in the formula of section 1 (a 320 MHz channel is 3,920 data subcarriers every 13.6 µs) is far beyond what a general purpose core could keep up with.

### 4.2 The lower MAC and the upper MAC

The MAC is the layer that decides **when** to transmit and **what a frame is** (WF 03). The standard's own timing forces it into two halves:

- The **lower MAC** (the "real time" half) does what has a deadline in microseconds: it counts the slots of the backoff, enforces the interframe spaces, sends the **acknowledgement** a fixed time after every frame it receives, builds the aggregates, encrypts and decrypts every frame (hardware engines for CCMP and GCMP), and retries. It lives in the baseband chip or in the firmware that runs on a small processor beside it.
- The **upper MAC** (the "management" half) does what can wait a few milliseconds: the state machines for scanning, authentication and association, the beacon contents, the buffering of frames for a sleeping station, and the handling of every management frame. It can live in the firmware or on the host.

### 4.3 The host and the services

The **host processor** runs an operating system, almost always Linux, and on it the services that make the box useful at home or in an office: a bridge joining the LAN ports and the radios, a router and NAT to the provider, a DHCP server and a DNS forwarder, a firewall, a traffic shaper (NET 14), and an agent that a service provider or a cloud controller talks to. Section 6 builds that part from components and captures it. In a larger access point the **forwarding** can be done by a dedicated hardware engine beside the host CPU, so that packets of an established flow never reach the CPU at all (a network processor or flow offload; **from memory**, and the Linux software version of the idea is the `flowtable` of NET 02 and NET 09).

---

## 5. Where the work is split, and why

### 5.1 The 16 microsecond deadline

A station that receives a frame addressed to it has to send an **acknowledgement** after a fixed gap, the **short interframe space (SIFS)**: **16 µs** at 5 and 6 GHz and **10 µs** at 2.4 GHz (**from memory**). If it is late, the sender has already assumed the frame was lost, and it retransmits. The next fixed gap is a **slot**, 9 µs in a modern network (20 µs where 802.11b clients are supported, **from memory**).

How long does a general purpose operating system need to react to an event? I measured it. `wake.c` asks Linux to wake a thread 16 µs from now, 20,000 times in a row, and records how late each wakeup is; it does it three ways: sleeping in `clock_nanosleep`, sleeping with real time priority (`SCHED_FIFO`) and **spinning** on the clock with no sleep at all, on an idle machine and with both vCPUs busy:

```c
// wake.c: ask Linux to wake this thread 16 us from now, many times, and record how late it is.
// The 5 GHz acknowledgement deadline (SIFS) is 16 us; this is what a software ACK would be up against.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sched.h>
static long ns(struct timespec a) { return a.tv_sec * 1000000000L + a.tv_nsec; }
static int cmp(const void *a, const void *b) { long x = *(const long *)a, y = *(const long *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 20000, fifo = argc > 2 && !strcmp(argv[2], "fifo"), spin = argc > 2 && !strcmp(argv[2], "spin");
    if (fifo) { struct sched_param p = {.sched_priority = 50}; if (sched_setscheduler(0, SCHED_FIFO, &p)) { perror("fifo"); return 1; } }
    long *late = malloc(n * sizeof(long));
    struct timespec t, now;
    clock_gettime(CLOCK_MONOTONIC, &t);
    for (int i = 0; i < n; i++) {
        t.tv_nsec += 16000;
        if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
        if (spin) { do clock_gettime(CLOCK_MONOTONIC, &now); while (ns(now) < ns(t)); }
        else clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
        clock_gettime(CLOCK_MONOTONIC, &now);
        late[i] = ns(now) - ns(t);
        if (late[i] > 1000000) { clock_gettime(CLOCK_MONOTONIC, &t); }  // resync after a long stall
    }
    qsort(late, n, sizeof(long), cmp);
    long over = 0; for (int i = 0; i < n; i++) if (late[i] > 16000) over++;
    printf("%-10s %d wakeups: median %6.1f us late, p99 %7.1f us, max %8.1f us, %5.1f %% later than 16 us\n", fifo ? "SCHED_FIFO" : spin ? "spinning" : "sleeping", n, late[n / 2] / 1e3, late[(int)(n * 0.99)] / 1e3, late[n - 1] / 1e3, 100.0 * over / n);
    return 0;
}
```

```text
--- 1. an idle machine
sleeping   20000 wakeups: median  537.5 us late, p99  1000.3 us, max   2305.7 us, 100.0 % later than 16 us
SCHED_FIFO 20000 wakeups: median  509.9 us late, p99   997.5 us, max   1023.2 us,  99.5 % later than 16 us
spinning   20000 wakeups: median    0.0 us late, p99    14.8 us, max    323.5 us,   0.9 % later than 16 us
--- 2. both cpus busy with other work
sleeping   20000 wakeups: median  540.2 us late, p99  1001.2 us, max   6085.5 us, 100.0 % later than 16 us
SCHED_FIFO 20000 wakeups: median  512.1 us late, p99   997.3 us, max   1028.7 us,  99.9 % later than 16 us
spinning   20000 wakeups: median    0.0 us late, p99    12.4 us, max   4460.9 us,   0.7 % later than 16 us
```

**Sleeping** and waiting for the kernel to wake a thread misses the deadline **every time**: the median wakeup is **about 0.5 ms late** (510 to 540 µs) with or without real time priority, which is thirty times the whole SIFS. (This VM's timer is coarse: its wakeups cluster at 0.5 and 1 ms, so a bare metal machine would do much better; I did not measure one.) **Spinning** on one core is far better, a median of **0.0 µs**, but still **0.7 to 0.9 percent** of the wakeups are later than 16 µs, with a worst case of **323 µs idle and 4,461 µs with the machine busy**. One missed acknowledgement in a hundred would cost every sender a retry and a backoff.

The consequence is the design of every access point:

- **Anything with a microsecond deadline is in hardware or in firmware on a dedicated core** that does nothing else. A host CPU running Linux, even a spinning one, would drop one deadline in about a hundred and could not be left to do the acknowledgement.
- **Anything that can wait milliseconds** (a scan, an association, a DHCP lease) can run on the host, where it can be programmed, updated and secured with ordinary tools.

### 5.2 FullMAC and SoftMAC, and what this VM has

Chip vendors draw the line between those two halves in different places, and the Linux driver model has a name for each (**from memory**):

| Name | What runs where | Examples and consequence |
|---|---|---|
| **SoftMAC** | The hardware does the lower MAC; **the upper MAC runs in the kernel** (`mac80211`) and is driven by `hostapd` or `wpa_supplicant` | The Atheros `ath9k` driver is of this kind. The software is fully open and inspectable |
| **FullMAC** | The firmware does both halves; the host sees a device that takes high level commands (scan, connect) through `cfg80211` | Many Broadcom and mobile chips. Simple for the host, opaque to the developer |
| **Offloaded design** | The firmware does the real time MAC, most of the lower MAC and much of the upper MAC, with a driver that mirrors the state | Modern high end chips (`ath11k`, `ath12k` and MediaTek designs, with differences between them that I do not remember in detail) |

The path in Linux from `hostapd` down is `hostapd`, the `nl80211` netlink interface, `cfg80211`, `mac80211`, the driver, the firmware, the radio (WF 10 builds it). What does this VM have of that stack? `s5_anatomy.sh` looks:

```bash
# Which parts of an AP are software you can run here, and which are not.
echo "--- 1. the upper MAC: hostapd and the drivers it was built with"
hostapd -v 2>&1 | head -1
echo "drivers compiled into hostapd: $(strings $(command -v hostapd) | grep -xE 'nl80211|wired|none|bsd' | sort -u | tr '\n' ' ')"
echo "--- 2. the client side: wpa_supplicant"
wpa_supplicant -v 2>&1 | head -1
wpa_supplicant -h 2>&1 | awk '/drivers:/{f=1} f&&NF{print} /^$/{f=0}' | head -8
echo "--- 3. the kernel's wireless layers, from the configuration of this kernel"
( zcat /proc/config.gz 2>/dev/null || cat /boot/config-$(uname -r) 2>/dev/null ) | grep -E '^(CONFIG_CFG80211|CONFIG_MAC80211|CONFIG_WLAN|CONFIG_MAC80211_HWSIM|CONFIG_BRIDGE|CONFIG_NF_NAT|CONFIG_NETFILTER)(=| )' | head -12
[ -r /proc/config.gz ] || [ -r /boot/config-$(uname -r) ] || echo "no kernel configuration readable"
echo "--- 4. radios present"
ls /sys/class/ieee80211 2>/dev/null | wc -l | awk '{print "wireless phys: " $1}'
iw dev 2>&1 | head -2
echo "--- 5. the router services as packages here (the rest of an AP's upper layers)"
for b in dnsmasq nft conntrack bridge hostapd wpa_supplicant iw; do printf "%-14s %s\n" $b "$(command -v $b || echo missing)"; done
```

```text
--- 1. the upper MAC: hostapd and the drivers it was built with
hostapd v2.10
drivers compiled into hostapd: nl80211 none wired
--- 2. the client side: wpa_supplicant
wpa_supplicant v2.10
drivers:
  nl80211 = Linux nl80211/cfg80211
  wext = Linux wireless extensions (generic)
  wired = Wired Ethernet driver
  macsec_linux = MACsec Ethernet driver for Linux
  none = no driver (RADIUS server/WPS ER)
options:
  -b = optional bridge interface name
--- 3. the kernel's wireless layers, from the configuration of this kernel
CONFIG_NETFILTER=y
CONFIG_NF_NAT=y
CONFIG_BRIDGE=y
--- 4. radios present
wireless phys: 0
nl80211 not found.
--- 5. the router services as packages here (the rest of an AP's upper layers)
dnsmasq        /usr/sbin/dnsmasq
nft            /usr/sbin/nft
conntrack      /usr/sbin/conntrack
bridge         /usr/sbin/bridge
hostapd        /usr/sbin/hostapd
wpa_supplicant /usr/sbin/wpa_supplicant
iw             /usr/sbin/iw
```

The software is here: `hostapd` and `wpa_supplicant` 2.10, built with the **`nl80211`**, **`wired`** and **`none`** drivers. The kernel part is not: the configuration prints only `CONFIG_NETFILTER`, `CONFIG_NF_NAT` and `CONFIG_BRIDGE`, with no `CFG80211`, no `MAC80211` and no `MAC80211_HWSIM`, no wireless phys exist, and `iw dev` fails with `nl80211 not found`. That is the reason for the limits at the top. The **`wired` driver** is the way in: `hostapd` and `wpa_supplicant` can run the same 802.1X and key management over an Ethernet cable, with no radio, so the authentication protocols of WF 04 can still be run for real. The services of section 6 are all present.

---

## 6. The router in the same case

### 6.1 One box, several devices

A home "router" is five things in one enclosure (**from memory** for the general description; the build below is the measurement):

| Device | What it does | In the build below |
|---|---|---|
| **Access point** | Radio, bridges the wireless clients to the LAN | A `veth` pair stands in for the radio |
| **Ethernet switch** | Joins the LAN ports and the access point at layer 2, learning MAC addresses | A Linux bridge `brlan` |
| **Router** | Forwards between the LAN and the provider (WAN) | The kernel's IP forwarding |
| **DHCP and DNS server** | Hands out addresses and a gateway, forwards DNS | `dnsmasq` (DHCP only here) |
| **Firewall and NAT** | Shares one public address, blocks unsolicited inbound | An `nft` masquerade rule |

`s3_router.sh` builds the whole thing in four network namespaces: `rt` (the router), `c1` and `c2` (two clients) and `isp` (a server beyond the WAN). The two client links are bridged on the LAN side of the router; the WAN side is a point to point link to the provider.

```bash
for n in rt c1 c2 isp; do ip netns add $n; ip netns exec $n ip link set lo up; done
R ip link add brlan type bridge; R ip link set brlan up
for c in 1 2; do
  ip link add r$c netns rt type veth peer name e0 netns c$c
  R ip link set r$c master brlan; R ip link set r$c up; ip netns exec c$c ip link set e0 up
done
ip link add w0 netns rt type veth peer name w1 netns isp
R ip addr add 192.168.1.1/24 dev brlan; R ip addr add 203.0.113.2/24 dev w0; R ip link set w0 up
ip netns exec isp ip addr add 203.0.113.1/24 dev w1; ip netns exec isp ip link set w1 up
R sysctl -qw net.ipv4.ip_forward=1
R nft add table ip nat; R nft add chain ip nat post '{ type nat hook postrouting priority 100; }'
R nft add rule ip nat post oifname w0 masquerade
```

The client links join the bridge `brlan`, which carries the router's own LAN address `192.168.1.1/24`; the WAN link is a separate pair with a public looking address `203.0.113.2/24`; and the last three lines turn on forwarding and add the single NAT rule, a masquerade on the WAN interface.

### 6.2 A client gets an address

A client that has joined (associated and authenticated, WF 03 and WF 04) has a link but no IP address. It broadcasts a DHCP **discover**; the server **offers** an address; the client **requests** it; the server **acknowledges**. NET 08 covers the protocol; here is the exchange captured on the router's bridge, with `tshark`:

```bash
R dnsmasq --interface=brlan --bind-interfaces --port=0 --dhcp-range=192.168.1.50,192.168.1.60,1h --dhcp-option=3,192.168.1.1 --dhcp-leasefile=/tmp/dm.leases --pid-file=/tmp/dm.pid --log-facility=/tmp/dm.log --no-resolv --no-hosts
sleep 1
echo "--- 1. the AP side: a client joins and runs DHCP (capture on the bridge, filter 'udp port 67 or 68')"
R timeout 8 tshark -i brlan -f 'udp port 67 or udp port 68' -T fields -e frame.number -e ip.src -e ip.dst -e bootp.option.dhcp -e bootp.ip.your -e bootp.option.dhcp_server_id -E separator='|' -c 4 > /tmp/dora.txt 2>/dev/null &
sleep 1
ip netns exec c1 dhclient -1 -q -pf /tmp/c1.pid -lf /tmp/c1.lease e0
wait
awk -F'|' 'BEGIN{split("1 Discover,2 Offer,3 Request,5 ACK",m,",")} {t=$4; n="?"; for(i in m){split(m[i],a," "); if(a[1]==t) n=a[2]}; printf "%s %-8s src %-12s dst %-16s yiaddr %-12s server %s\n", $1, n, ($2==""?"-":$2), $3, ($5=="0.0.0.0"?"-":$5), ($6==""?"-":$6)}' /tmp/dora.txt
```

```text
--- 1. the AP side: a client joins and runs DHCP (capture on the bridge, filter 'udp port 67 or 68')
1 Discover src 0.0.0.0      dst 255.255.255.255  yiaddr -            server -
2 Offer    src 192.168.1.1  dst 192.168.1.50     yiaddr 192.168.1.50 server 192.168.1.1
3 Request  src 0.0.0.0      dst 255.255.255.255  yiaddr -            server 192.168.1.1
4 ACK      src 192.168.1.1  dst 192.168.1.50     yiaddr 192.168.1.50 server 192.168.1.1
```

```text
--- 2. what the client was given
address 192.168.1.50/24
default route default via 192.168.1.1 dev e0
second client: address 192.168.1.52/24
```

Four frames, in the classical order, DORA. The discover and the request are **broadcasts** from `0.0.0.0` (the client has no address yet) to `255.255.255.255`; the request still carries the **server identifier** `192.168.1.1`, which is how the client tells the other servers on the segment that it has chosen this one. The offer and the acknowledgement come **from the server to the offered address** `192.168.1.50`. The client ended up with `192.168.1.50/24` and a **default route via 192.168.1.1**; a second client received `192.168.1.52/24` (`dnsmasq` chose it; an earlier run gave `.53` and I did not trace how).

On Wi-Fi, one more fact matters here (**from memory**; WF 03 measures the cost): a **broadcast or multicast frame is not acknowledged** and is sent at a low basic rate so that every client can decode it, so a network full of chatty broadcast protocols spends its airtime on the slowest rate, and an access point that buffers multicast for sleeping clients sends it only after the beacon that announces it (the delivery traffic indication message, WF 03).

### 6.3 NAT tells two clients apart

The two clients now each open a TCP connection to the same server (`203.0.113.1:80`) **from the same source port 40,000**. On the LAN side that is two different sources, `.50` and `.52`; on the WAN side they would both come from `203.0.113.2:40000` and the server could not tell them apart. The NAT's conntrack table is what resolves that:

```bash
ip netns exec isp nc -l -k -p 80 >/dev/null 2>&1 &
sleep 0.5
for c in 1 2; do ip netns exec c$c nc -q 1 -p 40000 203.0.113.1 80 </dev/null >/dev/null 2>&1 & done; sleep 0.7
R conntrack -L 2>/dev/null | awk '/dport=80/ {print}' | sed -E 's/ +/ /g; s/ mark=[0-9]+//; s/ use=[0-9]+//; s/^tcp +6 [0-9]+ //' | cut -c1-140
```

```text
--- 3. both clients open a connection from the same source port to the same server: NAT must tell them apart
TIME_WAIT src=192.168.1.50 dst=203.0.113.1 sport=40000 dport=80 src=203.0.113.1 dst=203.0.113.2 sport=80 dport=6902 [ASSURED]
TIME_WAIT src=192.168.1.52 dst=203.0.113.1 sport=40000 dport=80 src=203.0.113.1 dst=203.0.113.2 sport=80 dport=40000 [ASSURED]
```

The first line is the client `.50`: its source port 40,000 was **rewritten to 6,902** on the way out, and the reply, addressed to `203.0.113.2:6902`, is mapped back. The second line, the client `.52`, kept **40,000**. Which client is rewritten is a race (the one whose connection arrived second), and the new port is chosen by the kernel: an earlier run rewrote the other client to 15,380. The table has, for every connection, **two tuples**: what the client sent and what the server will send back. That is the whole of NAT and PAT: the first connection keeps its port and a later one that would collide gets another. (The state shows `TIME_WAIT` because `nc` had already closed.) Section 7 and problem D size the table.

### 6.4 A packet through the whole box

Put the pieces together for one request from a phone, say a TCP packet to `203.0.113.1:80`. The same packet is a different frame in each place (the 802.11 addresses are those of section 2.2):

| Step | Where | What the packet looks like |
|---|---|---|
| 1 | Over the air, phone to AP | An 802.11 data frame with **ToDS = 1**: `addr1` = AP (the receiver), `addr2` = phone (the transmitter), `addr3` = the router's LAN address (the destination); the payload is **encrypted** by the lower MAC (WF 04) |
| 2 | AP lower MAC | Acknowledges within the SIFS, decrypts, passes it up |
| 3 | AP upper MAC and bridge | The frame becomes an Ethernet frame from the phone's MAC to the router's MAC and goes into the bridge `brlan`; the bridge learns the phone's port |
| 4 | The router | The destination MAC is the router's own, so the frame goes up to IP; the route says 203.0.113.0/24 is on the WAN port; conntrack creates the entry |
| 5 | NAT | The source `192.168.1.50:40000` is rewritten to `203.0.113.2:40000` (or to another port if that was taken, section 6.3) |
| 6 | WAN | An Ethernet frame to the provider's next hop |
| 7 | The reply | The same steps in reverse: conntrack maps `203.0.113.2:40000` back, the bridge looks up the phone's MAC to find which port (the radio), and the AP sends a **FromDS** frame, `addr1` = phone, `addr2` = AP, `addr3` = the original source |

Three layers of address are in play at once: the **802.11 addresses** (who is the next receiver on the air), the **Ethernet addresses** (next hop on the LAN) and the **IP addresses and ports** (the end to end identity, rewritten by NAT).

---

## 7. How big must the tables be

Two tables in the router grow with the number of clients, and both obey Little's law (NET 14): **entries = arrival rate × lifetime**.

- The **DHCP pool** must hold every client seen within one lease time. A `/24` holds 253 clients after the gateway. A cafe with 600 visitors an hour and a one hour lease needs about 600 addresses at once; a 10 minute lease cuts that to about 100, at the price of a renewal at half the lease (problem D).
- The **conntrack table** holds one entry for every connection that is open or still waiting out its timers. This kernel's limit is **262,144 entries** (`nf_conntrack_max`), with a **120 s** TIME_WAIT and a **432,000 s** (five days) timeout for an established but silent TCP connection (problem D). At one new connection a second per client, **2,184** clients fill the table with TIME_WAIT entries alone.

---


## 8. Common mistakes

1. **Reading a peak PHY rate as throughput.** The 9,607.8 Mbit/s of 802.11ax needs 160 MHz, eight streams and the top MCS; a one stream phone at 80 MHz gets **600.5** (section 1.3), and contention, preambles and acknowledgements take a share of that (about half to two thirds reaches TCP, **from memory**; WF 03 measures it).
2. **Adding the bands of a class label.** AX3000 is **573.5 + 2,402.0 = 2,975.5**, the sum of two bands that no single client can use at once (section 1.3).
3. **Calling Wi-Fi 6 twice as fast as Wi-Fi 5.** The formula gives **1.39 times** at the peak (9,607.8 over 6,933.3): the symbol is 3.8 times longer. Its gain is efficiency, not peak rate (section 1.2).
4. **Putting the destination in `addr1` of a data frame to the AP.** `addr1` is the **receiver** of this hop, the AP; the destination is in `addr3` when ToDS is set, and the source is in `addr3` when FromDS is set (section 2.2).
5. **Expecting a bridge frame to have a BSSID.** With **both ToDS and FromDS set** the header has four addresses (RA, TA, DA, SA) and no BSSID field (section 2.2).
6. **Telling the topologies apart by the SSID.** An access point, an ad hoc node and a mesh point can all announce the same name; the tell is the capability bits (**ESS**, **IBSS**) and a **Mesh ID** element (section 2.2).
7. **Believing a single radio repeater keeps its speed.** One radio on one channel serving clients and the next hop gives `R ÷ hops`: **600, 300, 200, 150, 120, 100** Mbit/s at one to six hops (section 2.3).
8. **Assuming a dedicated backhaul radio removes all sharing.** It clears the client side; the backhaul still shares between hops that hear each other, and held **200** from three hops in the model (section 2.3).
9. **Confusing IEEE amendments with Wi-Fi Alliance certification.** A datasheet that lists "802.11ax" says the radio implements it; "Wi-Fi CERTIFIED 6" says a lab tested it; neither says every optional feature of the standard is present (section 3.1).
10. **Thinking 6E is a different radio.** It is 802.11ax on a new band; the change is a regulatory one (section 3.2, **from memory**).
11. **Putting a microsecond deadline on the host.** Sleeping threads woke a median **0.5 ms** late; even a spinning thread missed the 16 µs mark **0.7 to 0.9 percent** of the time, with a worst case of **4,461 µs** under load (section 5.1). The acknowledgement belongs in hardware.
12. **Debugging Wi-Fi in a build that has no wireless stack and expecting radios.** This kernel has no `cfg80211` or `mac80211` (section 5.2). The `wired` driver of `hostapd` and `wpa_supplicant` runs the security protocols without one; a real radio or `mac80211_hwsim` is needed for the rest (WF 10).
13. **Making the DHCP pool smaller than the clients inside one lease.** The pool of this experiment, `.50` to `.60`, holds **11** clients; a cafe needs hundreds (problem D).
14. **Sizing the NAT table by the number of clients, not the connection lifetime.** 2,000 clients at one new connection a second hold **240,000** TIME_WAIT entries against a limit of **262,144** (section 7, problem D).
15. **Counting a router's antennas as a client's.** A client's rate is set by **its** streams; a phone with one stream gets **600.5**, not the 1,201 of a two stream laptop (section 1.3).

---

## 9. Twelve interview questions

Each has an outline of a good answer; the section to go back to is in brackets.

1. **What is inside a home router?** An access point (radio, baseband, lower MAC and upper MAC), a switch for the LAN ports, a router with NAT, a DHCP and DNS server, a firewall, a shaper and a management agent, on one Linux host. Built and captured here from a bridge, `dnsmasq` and `nft`. [4, 6]
2. **What is the difference between IEEE 802.11 and the Wi-Fi Alliance?** IEEE writes the standard as lettered amendments; the Alliance certifies interoperability of a subset and names the generations; regulators set bands and power. A datasheet letter is implementation, a Wi-Fi CERTIFIED logo is a tested profile. [3.1]
3. **How do you compute a peak rate?** `subcarriers × bits × coding rate × streams ÷ symbol time`. 802.11be at 320 MHz: `3920 × 12 × 5/6 × 16 ÷ 13.6 µs = 46,117.6` Mbit/s; a one stream 80 MHz Wi-Fi 6 phone: **600.5**. [1.1, 1.3]
4. **Why was Wi-Fi 6 only 39 percent faster in peak rate than Wi-Fi 5?** The subcarriers went up 4.19 times and the constellation 1.25 times, but the symbol got 3.8 times longer; the benefits are OFDMA, uplink multi-user and spatial reuse, which raise the efficiency in a busy cell. [1.2]
5. **What are ToDS and FromDS, and why does 802.11 have three or four addresses?** `addr1` and `addr2` are the receiver and the transmitter of this hop; the end to end addresses are in `addr3` and `addr4`. Station to AP: destination in `addr3`; AP to station: source in `addr3`; both bits set: four addresses and no BSSID. [2.2]
6. **How do you tell an access point, an ad hoc node and a mesh point apart?** The beacon: ESS bit for an AP, IBSS bit for ad hoc, neither plus a Mesh ID element for a mesh point. [2.2]
7. **Why does a wireless repeater halve the throughput, and how do you design around it?** One radio carries the client side and the next hop in the same airtime, so each frame is sent once per hop: `R ÷ hops` (600, 300, 200 ...). Use a wire, or a separate backhaul radio, and no more than two or three hops. [2.3]
8. **Why is the acknowledgement generated in hardware?** The deadline is the SIFS, 16 µs at 5 GHz. Measured here, a sleeping thread woke a median 0.5 ms late and a spinning one still missed 0.7 to 0.9 percent of deadlines. [5.1]
9. **FullMAC against SoftMAC?** In FullMAC the firmware runs the whole MAC and the host issues high level commands; in SoftMAC the hardware does the real time part and the kernel's `mac80211` the management part, driven by `hostapd`. SoftMAC is open to inspection, FullMAC is simpler for the host. [5.2]
10. **What happens on the wire when a client joins and asks for an address?** Association and key setup first (WF 03 and WF 04), then DHCP: discover (broadcast), offer, request (broadcast, with the server id), acknowledge: four frames, with a default route and an address from the pool. Captured: `192.168.1.50/24`, route via `192.168.1.1`. [6.2]
11. **How does NAT keep two clients with the same source port apart?** The conntrack table holds both tuples for every connection; the second client's port was rewritten to 6,902 while the first kept 40,000. The table is sized by connection rate times lifetime. [6.3, 7]
12. **What do "AX3000" and "Wi-Fi 6E" mean?** AX3000 is the sum of the peak rates of all bands (573.5 + 2,402.0 = 2,975.5); no client can use it all. 6E is Wi-Fi 6 on the 6 GHz band, which the regulators opened, so it has wide channels free of legacy devices. [1.3, 3.2]

---

## 10. Problems

Do each on paper first, then with the script named. The scripts run from `/home/claude/wf01` (or your copy of `prog/`).

**Problem A (paper, then `ans_a.py`): rates by MCS and width.** For 802.11ax with one stream: the rate at 20, 40, 80 and 160 MHz for MCS 0 to 11 at a 0.8 µs guard interval; what a 1.6 and a 3.2 µs guard cost at 80 MHz and MCS 11; and the smallest MCS and width that reach 100, 300, 600 and 1,000 Mbit/s with one and with two streams. *Approach:* the formula of section 1.1, with 234, 468, 980 and 1,960 data subcarriers for the four widths and a symbol of 12.8 µs plus the guard; then run `python3 prog/ans_a.py`.

**Problem B (paper, then `ans_b.py`): decode the addresses.** For four data frames (station to AP, AP to station, ad hoc, AP to AP) given by the two control bits and the address fields, say which address is the destination, the source and the BSSID. *Approach:* the table of section 2.2; then run `python3 -W ignore prog/ans_b.py`, which builds the frames and compares your decoder with `tshark`.

**Problem C (paper, then `ans_c.py`): plan a mesh.** A node's client radio can do 1,201 Mbit/s PHY and its backhaul radio 2,402; assume 0.65 efficiency and that hops up to three apart share the air. What does a client get at one to six hops with a repeater, a dedicated backhaul radio and a wire, and how many 25 Mbit/s 4K streams fit at hop one and at hop four? *Approach:* repeater `E × client ÷ hops`; dedicated `min(E × client, E × backhaul ÷ min(hops, 3))`; then run `python3 prog/ans_c.py`.

**Problem D (paper, then `ans_d.py`): size the DHCP pool and the NAT table.** For leases of 10 minutes to 24 hours: the renewal times T1 and T2 and the renewal rate for 50 clients; the size of a pool of `.50` to `.60`; the number of conntrack entries for 20, 200, 2,000 and 5,000 clients at one new connection a second with this kernel's timers; the clients that fill the table; and the ports per subscriber behind carrier grade NAT. *Approach:* T1 is half the lease and T2 seven eighths (**from memory**); entries are rate times lifetime; then run `python3 prog/ans_d.py`.

**Problem E (paper, then `ans_e.py`): one slow client.** Four clients share a cell. What does each get when all are fast, when one is at 6.5 and when one is at 1 Mbit/s, if the cell gives every client the same number of packets, and if it gives every client the same airtime? *Approach:* equal packets: each client gets `1 ÷ Σ(1 ÷ rate)`; equal airtime: each gets `rate ÷ n`; then run `python3 prog/ans_e.py`. This is a preview of WF 03, which adds the overheads and measures the behaviour of the MAC.

---


## 11. Answers

### Problem A: rates by MCS and width

```python
# ans_a.py: 802.11ax rate by MCS, width and guard interval, and the rate a target needs.
from fractions import Fraction as F
MCS = [(1, F(1, 2)), (2, F(1, 2)), (2, F(3, 4)), (4, F(1, 2)), (4, F(3, 4)), (6, F(2, 3)), (6, F(3, 4)), (6, F(5, 6)), (8, F(3, 4)), (8, F(5, 6)), (10, F(3, 4)), (10, F(5, 6))]
SUB = {20: 234, 40: 468, 80: 980, 160: 1960}
def rate(mcs, mhz, ss, gi):
    bits, r = MCS[mcs]
    return SUB[mhz] * bits * float(r) * ss / (12.8 + gi)
print("802.11ax, one stream, 0.8 us guard interval: Mbit/s by MCS and width")
print("%-4s %-12s %8s %8s %8s %8s" % ("MCS", "modulation", "20 MHz", "40 MHz", "80 MHz", "160 MHz"))
names = ["BPSK", "QPSK", "QPSK", "16-QAM", "16-QAM", "64-QAM", "64-QAM", "64-QAM", "256-QAM", "256-QAM", "1024-QAM", "1024-QAM"]
for m in range(12):
    print("%-4d %-6s %-5s %8.1f %8.1f %8.1f %8.1f" % (m, names[m], MCS[m][1], *[rate(m, w, 1, 0.8) for w in (20, 40, 80, 160)]))
print()
print("guard interval, 80 MHz MCS 11, one stream: the cost of a longer guard")
for gi in (0.8, 1.6, 3.2):
    print("GI %.1f us: %6.1f Mbit/s (%.1f %% of 0.8 us)" % (gi, rate(11, 80, 1, gi), 100 * rate(11, 80, 1, gi) / rate(11, 80, 1, 0.8)))
print()
print("smallest MCS that reaches the target PHY rate (0.8 us GI), per stream count")
for target in (100, 300, 600, 1000):
    row = []
    for ss in (1, 2):
        hit = next(((m, w) for w in (20, 40, 80, 160) for m in range(12) if rate(m, w, ss, 0.8) >= target), None)
        row.append("%d SS: %s" % (ss, "MCS %d at %d MHz = %.0f" % (hit[0], hit[1], rate(hit[0], hit[1], ss, 0.8)) if hit else "none"))
    print("%5d Mbit/s  %s" % (target, "    ".join(row)))
```

```text
802.11ax, one stream, 0.8 us guard interval: Mbit/s by MCS and width
MCS  modulation     20 MHz   40 MHz   80 MHz  160 MHz
0    BPSK   1/2        8.6     17.2     36.0     72.1
1    QPSK   1/2       17.2     34.4     72.1    144.1
2    QPSK   3/4       25.8     51.6    108.1    216.2
3    16-QAM 1/2       34.4     68.8    144.1    288.2
4    16-QAM 3/4       51.6    103.2    216.2    432.4
5    64-QAM 2/3       68.8    137.6    288.2    576.5
6    64-QAM 3/4       77.4    154.9    324.3    648.5
7    64-QAM 5/6       86.0    172.1    360.3    720.6
8    256-QAM 3/4      103.2    206.5    432.4    864.7
9    256-QAM 5/6      114.7    229.4    480.4    960.8
10   1024-QAM 3/4      129.0    258.1    540.4   1080.9
11   1024-QAM 5/6      143.4    286.8    600.5   1201.0

guard interval, 80 MHz MCS 11, one stream: the cost of a longer guard
GI 0.8 us:  600.5 Mbit/s (100.0 % of 0.8 us)
GI 1.6 us:  567.1 Mbit/s (94.4 % of 0.8 us)
GI 3.2 us:  510.4 Mbit/s (85.0 % of 0.8 us)

smallest MCS that reaches the target PHY rate (0.8 us GI), per stream count
  100 Mbit/s  1 SS: MCS 8 at 20 MHz = 103    2 SS: MCS 4 at 20 MHz = 103
  300 Mbit/s  1 SS: MCS 6 at 80 MHz = 324    2 SS: MCS 6 at 40 MHz = 310
  600 Mbit/s  1 SS: MCS 11 at 80 MHz = 600    2 SS: MCS 6 at 80 MHz = 649
 1000 Mbit/s  1 SS: MCS 10 at 160 MHz = 1081    2 SS: MCS 10 at 80 MHz = 1081
```

Width scales the rate exactly: at MCS 11 the rate goes **143.4, 286.8, 600.5, 1,201.0** for 20, 40, 80 and 160 MHz, and from 80 to 160 MHz it is **2.00 times** (`1,960 ÷ 980`); from 20 to 80 MHz it is 4.19 times, not 4, because 80 MHz has 980 data subcarriers and four 20 MHz channels only 936 (4 × 234), the saving of not needing guard tones between them. A step up in MCS buys less than a step in width: MCS 7 to 8 is `432.4 ÷ 360.3 = 1.20` at 80 MHz, then 1.11, 1.12 and 1.11 for MCS 9, 10 and 11; and each step also needs a better signal to noise ratio (WF 02 and WF 11). A longer guard interval costs rate in exact proportion to the symbol: **567.1** (94.4 percent) at 1.6 µs and **510.4** (85.0 percent) at 3.2 µs, which is the price of reach in a noisy or outdoor setting.

For the targets, the smallest configurations are: 100 Mbit/s needs **MCS 8 at 20 MHz with one stream (103)** or MCS 4 with two; 300 needs **MCS 6 at 80 MHz with one stream (324)** or MCS 6 at 40 MHz with two (310); 600 needs **MCS 11 at 80 MHz with one stream (600.5, only just)** or MCS 6 at 80 MHz with two (649); and 1,000 needs **MCS 10 at 160 MHz with one stream (1,081)**, since MCS 9 gives 960.8, or MCS 10 at 80 MHz with two streams. The pattern: wide channels and many streams are cheaper in signal to noise than a dense constellation, and a second stream at 80 MHz equals a first stream at 160 MHz.

### Problem B: decode the addresses

```python
# ans_b.py: decode the four addresses of an 802.11 data frame from its ToDS and FromDS bits, then check against tshark.
from scapy.all import RadioTap, Dot11, LLC, SNAP, IP, ICMP, wrpcap
import subprocess
def decode(to_ds, from_ds, a1, a2, a3, a4):
    # returns (DA, SA, BSSID); RA is always addr1 and TA is always addr2
    return {(0, 0): (a1, a2, a3), (1, 0): (a3, a2, a1), (0, 1): (a1, a3, a2), (1, 1): (a3, a4, None)}[(to_ds, from_ds)]
M = lambda n: "02:00:00:00:00:%02x" % n
cases = [
    ("station 1 to a host behind the AP", 1, 0, M(1), M(17), M(170), None),
    ("AP to station 1 for a host behind the AP", 0, 1, M(17), M(1), M(170), None),
    ("ad hoc, station 2 to station 1", 0, 0, M(17), M(18), M(204), None),
    ("AP to AP over a wireless bridge", 1, 1, M(1), M(2), M(18), M(170)),
]
payload = LLC() / SNAP() / IP(src="10.0.0.2", dst="10.0.0.1") / ICMP()
frames = []
for name, t, f, a1, a2, a3, a4 in cases:
    d = Dot11(type=2, subtype=0, FCfield=(1 if t else 0) | (2 if f else 0), addr1=a1, addr2=a2, addr3=a3)
    if a4: d.addr4 = a4
    frames.append(RadioTap() / d / payload)
wrpcap("/tmp/ans_b.pcap", frames)
out = subprocess.run(["tshark", "-r", "/tmp/ans_b.pcap", "-T", "fields", "-E", "separator=|", "-e", "wlan.da", "-e", "wlan.sa", "-e", "wlan.bssid"], capture_output=True, text=True).stdout.strip().split("\n")
short = lambda m: ":" + m[-2:] if m else "-"
print("%-42s %2s %2s  %-4s %-4s %-4s %-4s  %-14s %-14s" % ("frame", "To", "Fr", "a1", "a2", "a3", "a4", "decoded DA SA B", "tshark DA SA B"))
for (name, t, f, a1, a2, a3, a4), line in zip(cases, out):
    d = decode(t, f, a1, a2, a3, a4)
    ts = (line.split("|") + ["", "", ""])[:3]
    print("%-42s %2d %2d  %-4s %-4s %-4s %-4s  %-14s %-14s" % (name, t, f, short(a1), short(a2), short(a3), short(a4), " ".join(short(x) for x in d), " ".join(short(x) for x in ts)))
```

```text
frame                                      To Fr  a1   a2   a3   a4    decoded DA SA B tshark DA SA B
station 1 to a host behind the AP           1  0  :01  :11  :aa  -     :aa :11 :01    :aa :11 :01
AP to station 1 for a host behind the AP    0  1  :11  :01  :aa  -     :11 :aa :01    :11 :aa :01
ad hoc, station 2 to station 1              0  0  :11  :12  :cc  -     :11 :12 :cc    :11 :12 :cc
AP to AP over a wireless bridge             1  1  :01  :02  :12  :aa   :12 :aa -      :12 :aa -
```

The decoder is the table of section 2.2 as code, and `tshark` agrees on all four frames. Station to AP (ToDS only): the **destination is `addr3`** (`:aa`), the source is `addr2` (`:11`) and the BSSID is `addr1` (`:01`). AP to station (FromDS only): `DA` is `addr1`, the **source is `addr3`** and the BSSID is `addr2`. Ad hoc (both clear): `DA` and `SA` are `addr1` and `addr2` and the BSSID is `addr3` (`:cc`). AP to AP (both set): `DA` is `addr3` and `SA` is **`addr4`**, and there is no BSSID (`-`). `RA` is `addr1` and `TA` is `addr2` in all four, because they name the two ends of this hop on the air.

### Problem C: plan a mesh

```python
# ans_c.py: plan a mesh: what a client gets at each hop for three backhaul designs, with the assumptions as parameters.
EFF = 0.65            # assumed share of the PHY rate that becomes IP throughput (WF 03 measures it); an assumption
CLIENT = 1201.0       # Wi-Fi 6, 160 MHz, 1 stream, MCS 11 (ans_a)
BH5, BH6 = 2402.0, 2402.0   # backhaul PHY: 2 streams at 160 MHz (5 GHz, 6 GHz), Wi-Fi 6
def row(hops, designs):
    return " ".join("%12.0f" % x for x in designs(hops))
def designs(h):
    rep = CLIENT * EFF / h                     # one radio for clients and backhaul, one channel, hops take turns
    tri = min(CLIENT * EFF, BH5 * EFF / min(h, 3))   # a separate 5 GHz backhaul radio, hops within 3 share the air
    wired = CLIENT * EFF                       # Ethernet backhaul: client link is the limit
    return rep, tri, wired
print("assumptions: PHY %.0f for clients, %.0f for a 2 stream backhaul, efficiency %.2f (assumed), hops within 3 hear each other" % (CLIENT, BH5, EFF))
print("%-5s %12s %12s %12s   (client throughput, Mbit/s)" % ("hops", "repeater", "dedicated", "wired"))
for h in range(1, 7):
    print("%-5d %s" % (h, row(h, designs)))
print()
print("how many 25 Mbit/s 4K streams the first hop can carry, and the same for the last of 4 hops")
for name, i in (("repeater", 0), ("dedicated backhaul", 1), ("wired", 2)):
    one, four = designs(1)[i], designs(4)[i]
    print("%-20s hop 1: %3d streams    hop 4: %3d streams" % (name, one // 25, four // 25))
```

```text
assumptions: PHY 1201 for clients, 2402 for a 2 stream backhaul, efficiency 0.65 (assumed), hops within 3 hear each other
hops      repeater    dedicated        wired   (client throughput, Mbit/s)
1              781          781          781
2              390          781          781
3              260          520          781
4              195          520          781
5              156          520          781
6              130          520          781

how many 25 Mbit/s 4K streams the first hop can carry, and the same for the last of 4 hops
repeater             hop 1:  31 streams    hop 4:   7 streams
dedicated backhaul   hop 1:  31 streams    hop 4:  20 streams
wired                hop 1:  31 streams    hop 4:  31 streams
```

One hop gives **781 Mbit/s** to a client with any design (`1,201 × 0.65 = 780.7`). With a repeater the rate is that divided by the hops: **390, 260, 195, 156, 130**. A dedicated backhaul radio of 2,402 Mbit/s PHY holds **781 at two hops** (its 780.7 equals the client side exactly) and then falls to **520** at three (`2,402 × 0.65 ÷ 3`) and stays there under the assumption that hops more than three apart do not hear each other. A wire holds 781 at any hop count. In streams of 25 Mbit/s: **31** at the first hop for every design, and at the fourth hop **7** for a repeater, **20** for the dedicated radio and **31** for the wire. The efficiency of 0.65 and the hearing range are assumptions; changing them moves the numbers but not the shape (WF 03 measures the efficiency, WF 07 plans real meshes).

### Problem D: size the DHCP pool and the NAT table

```python
# ans_d.py: DHCP lease timers and pool size, and how many connections a NAT table must hold.
import subprocess, math
def sysctl(k): return subprocess.run(["sysctl", "-n", k], capture_output=True, text=True).stdout.split()
print("DHCP timers (RFC 2131 defaults, from memory): T1 = 0.5 x lease, T2 = 0.875 x lease")
print("%-10s %8s %8s %12s %s" % ("lease", "T1", "T2", "renewals/h", "for 50 clients"))
for name, s in (("10 min", 600), ("1 h", 3600), ("12 h", 43200), ("24 h", 86400)):
    print("%-10s %8s %8s %12.2f %.1f per minute" % (name, "%d s" % (s // 2), "%d s" % (s * 7 // 8), 3600 / (s / 2), 50 / (s / 2 / 60)))
print()
print("a /24 has 254 host addresses; minus the gateway is 253. A pool of 50 to 60 holds %d clients" % (60 - 50 + 1))
print("pool needed = clients seen inside one lease time; a cafe with 600 visitors an hour and a 1 h lease needs about 600; with a 10 min lease about 100")
print()
lo, hi = map(int, sysctl("net.ipv4.ip_local_port_range"))
est, tw, udp, mx = (int(sysctl(k)[0]) for k in ("net.netfilter.nf_conntrack_tcp_timeout_established", "net.netfilter.nf_conntrack_tcp_timeout_time_wait", "net.netfilter.nf_conntrack_udp_timeout", "net.netfilter.nf_conntrack_max"))
print("this kernel: ephemeral ports %d to %d = %d, conntrack max %d, TCP established timeout %d s, TIME_WAIT %d s, UDP %d s" % (lo, hi, hi - lo + 1, mx, est, tw, udp))
print()
print("table size = connection rate x lifetime (Little's law); one new flow per second per client")
for clients in (20, 200, 2000, 5000):
    print("%5d clients: short TCP (TIME_WAIT %d s) %7d entries, UDP (%d s) %7d" % (clients, tw, clients * tw, udp, clients * udp))
print("clients that fill a %d entry table at one new TCP flow a second: %d; at one every 10 s: %d" % (mx, mx // tw, mx * 10 // tw))
print()
print("ports per client behind carrier grade NAT (one public IP, 64,512 ports above 1023, same destination)")
for subs in (1, 10, 100, 500):
    print("%4d subscribers per address: %6d ports each" % (subs, 64512 // subs))
```

```text
DHCP timers (RFC 2131 defaults, from memory): T1 = 0.5 x lease, T2 = 0.875 x lease
lease            T1       T2   renewals/h for 50 clients
10 min        300 s    525 s        12.00 10.0 per minute
1 h          1800 s   3150 s         2.00 1.7 per minute
12 h        21600 s  37800 s         0.17 0.1 per minute
24 h        43200 s  75600 s         0.08 0.1 per minute

a /24 has 254 host addresses; minus the gateway is 253. A pool of 50 to 60 holds 11 clients
pool needed = clients seen inside one lease time; a cafe with 600 visitors an hour and a 1 h lease needs about 600; with a 10 min lease about 100

this kernel: ephemeral ports 32768 to 60999 = 28232, conntrack max 262144, TCP established timeout 432000 s, TIME_WAIT 120 s, UDP 30 s

table size = connection rate x lifetime (Little's law); one new flow per second per client
   20 clients: short TCP (TIME_WAIT 120 s)    2400 entries, UDP (30 s)     600
  200 clients: short TCP (TIME_WAIT 120 s)   24000 entries, UDP (30 s)    6000
 2000 clients: short TCP (TIME_WAIT 120 s)  240000 entries, UDP (30 s)   60000
 5000 clients: short TCP (TIME_WAIT 120 s)  600000 entries, UDP (30 s)  150000
clients that fill a 262144 entry table at one new TCP flow a second: 2184; at one every 10 s: 21845

ports per client behind carrier grade NAT (one public IP, 64,512 ports above 1023, same destination)
   1 subscribers per address:  64512 ports each
  10 subscribers per address:   6451 ports each
 100 subscribers per address:    645 ports each
 500 subscribers per address:    129 ports each
```

A client renews at **T1, half the lease**, by asking the same server, and rebinds at **T2, seven eighths**, by asking any server; so a 1 hour lease renews at 1,800 s and 50 clients make **1.7 renewals a minute**, while a 10 minute lease makes **10 a minute** (12 renewals per client per hour). The pool of `.50` to `.60` holds **11** clients; a `/24` holds 253 after the gateway. A pool must cover every client seen within a lease time: 600 visitors an hour on a 1 hour lease needs about 600, on a 10 minute lease about 100.

For the table, this kernel allows **262,144** conntrack entries, with a **120 s** TIME_WAIT, a **30 s** UDP timeout and a **432,000 s** established timeout; the ephemeral port range holds 28,232 ports. At one new connection a second per client, 20 clients hold **2,400** TIME_WAIT entries, 200 hold 24,000, 2,000 hold **240,000** and 5,000 hold **600,000**, more than the table, so **2,184** clients fill it (`262,144 ÷ 120`), or **21,845** at one new connection every 10 seconds. Behind carrier grade NAT a single address shares its **64,512** ports: 10 subscribers get **6,451** each, 100 get **645**, 500 get **129**, and a browser that opens a few hundred connections to one site then runs out.

### Problem E: one slow client

```python
# ans_e.py: one slow client in a cell. Same packet counts (a FIFO) against equal airtime.
# Preview of WF 03: the MAC overhead is left out, so these are upper bounds. Rates are PHY rates in Mbit/s.
def per_packet(rates):        # every station sends one packet per round: round time = sum of s/r_i
    t = sum(1 / r for r in rates)
    return [1 / t] * len(rates)
def airtime(rates):           # every station gets 1/n of the time
    return [r / len(rates) for r in rates]
cases = [("4 fast clients", [600, 600, 600, 600]), ("3 fast and 1 at 6.5", [600, 600, 600, 6.5]), ("3 fast and 1 at 1 (802.11b)", [600, 600, 600, 1]), ("1 fast, 1 slow (150 and 6.5)", [150, 6.5])]
print("%-30s %-26s %-26s" % ("cell", "equal packets: each, total", "equal airtime: each, total"))
for name, r in cases:
    a, b = per_packet(r), airtime(r)
    print("%-30s %7.1f each %8.1f total   %-14s %8.1f total" % (name, a[0], sum(a), " ".join("%.1f" % v for v in b), sum(b)))
print()
print("airtime a 1 MB upload takes at a given PHY rate (frames only, no overhead)")
for r in (1, 6.5, 54, 150, 600, 1201):
    print("%7.1f Mbit/s: %8.1f ms" % (r, 8 / r * 1000))
```

```text
cell                           equal packets: each, total equal airtime: each, total
4 fast clients                   150.0 each    600.0 total   150.0 150.0 150.0 150.0    600.0 total
3 fast and 1 at 6.5                6.3 each     25.2 total   150.0 150.0 150.0 1.6    451.6 total
3 fast and 1 at 1 (802.11b)        1.0 each      4.0 total   150.0 150.0 150.0 0.2    450.2 total
1 fast, 1 slow (150 and 6.5)       6.2 each     12.5 total   75.0 3.2           78.2 total

airtime a 1 MB upload takes at a given PHY rate (frames only, no overhead)
    1.0 Mbit/s:   8000.0 ms
    6.5 Mbit/s:   1230.8 ms
   54.0 Mbit/s:    148.1 ms
  150.0 Mbit/s:     53.3 ms
  600.0 Mbit/s:     13.3 ms
 1201.0 Mbit/s:      6.7 ms
```

If every client gets the same number of packets, every client gets the same rate and the **slowest sets it for all**: with one client at 6.5 Mbit/s the other three fall from 150 to **6.3** each and the cell total from 600 to **25.2**; with one at 1 Mbit/s everyone gets **1.0** and the total is **4.0**. If every client gets the same airtime, each gets `rate ÷ 4`: the fast clients keep **150.0** and the slow one gets **1.6** (or 0.2), and the total stays at **451.6** (or 450.2). The effect is the same size as the airtime table at the end of the output: a megabyte at 1 Mbit/s holds the air for **8,000 ms**, at 1,201 Mbit/s for **6.7 ms**, a ratio of 1,200. WF 03 shows how the DCF gives equal packets by default and what airtime fairness changes.

---

## 12. Cheat sheet

**Rate formula:** `N × b × R × S ÷ T`. Peaks (computed): a/g **54**, n **600**, ac **6,933.3**, ax **9,607.8**, be **46,117.6** Mbit/s. One stream at 80 MHz (40 for n): **150.0, 433.3, 600.5, 720.6**. Step factors: a to n 11.1, n to ac 11.6, ac to ax **1.39** (symbol 3.8 times longer), ax to be 4.8. Class labels add bands: 573.5 + 1,201.0 = 1,774.5 (AX1800), 573.5 + 2,402.0 = 2,975.5 (AX3000).

**Names (from memory):** n = Wi-Fi 4, ac = Wi-Fi 5, ax = Wi-Fi 6 (6E on 6 GHz), be = Wi-Fi 7, bn = Wi-Fi 8 in development. IEEE writes the standard; the Wi-Fi Alliance certifies and names; regulators set bands and power.

**Topology bits:** AP beacon ESS = 1; ad hoc IBSS = 1; mesh neither plus a Mesh ID (captured). Data frames: ToDS only: `addr1` BSSID, `addr2` SA, `addr3` DA; FromDS only: `addr1` DA, `addr2` BSSID, `addr3` SA; neither: `addr3` BSSID; both: four addresses, no BSSID. `addr1` is always RA, `addr2` always TA.

**Backhaul (model):** with a link rate of 600 and no other loss, a repeater gives 600, 300, 200, 150, 120, 100 at 1 to 6 hops, a dedicated radio 600, 300, 200 and then 200, a wire 600. With a 1,201 Mbit/s client radio, a 2,402 backhaul and an assumed efficiency of 0.65: 781 at one hop; at four hops a repeater gives 195, the dedicated radio 520, a wire 781.

**Timing (measured here):** SIFS 16 µs at 5 and 6 GHz, 10 µs at 2.4 (from memory). Sleeping wakeup median 0.5 ms late; spinning median 0.0 µs but 0.7 to 0.9 percent later than 16 µs, worst 4,461 µs. Real time MAC in hardware or firmware; management on the host.

**AP blocks:** antennas, RF front end, transceiver, baseband (PHY), lower MAC (microseconds), upper MAC (milliseconds), host (Linux services), switch and ports. SoftMAC: kernel `mac80211` plus `hostapd`; FullMAC: firmware does it all. This VM: `hostapd` and `wpa_supplicant` 2.10 with `nl80211`, `wired` and `none` drivers; no `cfg80211`, no radios.

**Router in a box (captured):** DHCP DORA four frames, client `192.168.1.50/24`, default via `192.168.1.1`; NAT: same source port 40,000 from two clients came out as 40,000 and 6,902. Tables: entries = rate × lifetime; 262,144 conntrack entries, 120 s TIME_WAIT: 2,184 clients at one new connection a second fill it; pool of 11 holds 11 clients; `/24` holds 253.

**One slow client (computed):** equal packets: everyone gets the slowest rate (6.3 each, total 25.2 with one at 6.5); equal airtime: 150 for fast ones, 1.6 for the slow, total 451.6.

---

## 13. Sources

Read or run for this tutorial:

- The Candela Technologies course "Wi-Fi Technology Fundamentals" (candelatech.com/course.php), sessions 1a to 1d: the outline and descriptions, used to set the coverage of this tutorial.
- Linux 6.18 in the sandbox with `hostapd` and `wpa_supplicant` 2.10, `tshark` 4.2.2, `scapy` 2.5.0, `dnsmasq`, `nft`, `conntrack`, `dhclient`, `gcc`: used and observed here (frames built by hand, DHCP and NAT run in namespaces, wake-up latency measured with `wake.c`).
- The kernel configuration and `sysctl` values of the sandbox, read for section 5.2 and problem D.

From memory (not re-read for this tutorial):

- IEEE Std 802.11 and its lettered amendments (a, b, g, n, ac, ax, be and the others in section 3), the 802.11-2020 rollup, and the draft numbers `tshark` prints; IEEE 802.11s for mesh and 802.11u for interworking.
- The Wi-Fi Alliance generation names (2018) and certification programmes: WPA3, Enhanced Open, WMM, Passpoint, Agile Multiband, EasyMesh, Wi-Fi Direct, Wi-Fi Aware.
- Regulators (FCC, ETSI, MIC) and the 6 GHz rules of 2020.
- The SIFS and slot values, the DHCP timers of RFC 2131 (T1 half, T2 seven eighths) and the architecture of FullMAC and SoftMAC drivers.
- Chip block diagrams, offload engines and class labels (AX1800, AX3000).

To go further (WF 02 to WF 11 build on these): the MathWorks "WLAN (Wi-Fi) Explained" video series for the physical layer, the Linux wireless documentation at `wireless.docs.kernel.org` and the 802.11 driver developer's guide at `docs.kernel.org` for the stack, and the CWNA study material for an operator's view.

Cross references: NET 01 (802.11 framing against Ethernet), NET 02 (NAT and PAT), NET 08 (DHCP), NET 14 (airtime fairness and per station queues, section 9), and WF 02 (the PHY), WF 03 (the MAC) and WF 10 (the Linux stack, `hostapd`, `mac80211_hwsim`).
