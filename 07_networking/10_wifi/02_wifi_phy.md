# WF 02: The Wi-Fi physical layer, from the channel to the PPDU

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela sessions 2a to 2d (the 802.11 PHY, modulation, MIMO and OFDMA), with the beamforming, regulatory power and DFS gaps folded in · Time: about 10 hours · You need: [WF 01](01_wifi_landscape_and_ap_anatomy.md) (the rate formula and the access point blocks) and [WF 11](11_rf_fundamentals_and_link_budget.md) (decibels, the noise floor and the SNR each MCS needs). The physical layer is the part of Wi-Fi that turns a number of bits into a number of microseconds of air. Everything above it, the contention, the aggregation, the rate control and the roaming, is a way of spending or saving those microseconds. This tutorial builds the PHY from the bottom: which frequencies exist, how a bit becomes a point on a constellation, how a convolutional code buys back 4 dB, how OFDM survives echoes, what the preamble costs, and how MIMO, beamforming and OFDMA multiply what one channel can carry. Every table is produced by a script that runs here, and the one external check is that `tshark` decodes hand built radiotap headers to the same rates as the formula.*

"Walk me through how Wi-Fi 6 gets from 54 Mbit/s to 9.6 Gbit/s" and "why is my 1.2 Gbit/s link only moving 500 Mbit/s" are the two standard PHY questions, and they are the same question: the first is the **peak rate formula**, the second is **everything that formula leaves out** (the preamble, the guard interval, the amplifier back-off, the sounding airtime). A Staff level answer carries both: the number of data subcarriers times the bits per subcarrier times the coding rate times the streams, divided by the symbol time, and then the list of overheads and the one aggregate size at which each stops mattering. This tutorial puts a computed or simulated number on every step, and says which step is from the standard and was written from memory.

**Eight honest limits, stated first.**

1. **There is no radio here, so no number below is a measured over the air result.** Every figure is arithmetic from a formula or a Monte Carlo simulation I wrote. The one independent check is section 6: `tshark` 4.2.2 decodes my hand built radiotap headers to the rates my formula gives.
2. **The standard was not re-read.** The subcarrier counts, the MCS tables, the field durations of the preamble, the 12 bit LENGTH limit, the EVM limits per MCS, the RU tone plan and the DFS timers are **from memory** and marked so where they matter. The formulas reproduce the well known peak rates (54, 600, 6,933.3, 9,607.8 and 46,117.6 Mbit/s), which checks the subcarrier counts and the coding rates but not the EVM limits or the DFS timers.
3. **The channel is simulated, not measured**: additive white Gaussian noise (AWGN) or an i.i.d. Rayleigh model, with no correlation between antennas, no real interference and no hardware impairments except the amplifier of section 5. Real arrays are correlated, so real MIMO gains are smaller than section 8 shows.
4. **The coded results use one code**, the 802.11 constraint length 7 convolutional code with a soft decision Viterbi decoder I wrote. LDPC, which 802.11n made optional and 802.11ax uses for the larger blocks, is **not simulated**; its gain over the convolutional code (about 1 to 2 dB, **from memory**) is stated, not measured.
5. **The amplifier is a Rapp model**, a standard textbook shape and not any real power amplifier. The EVM limit per MCS in section 5 is from memory of the 802.11ax table.
6. **The sounding, MU-MIMO and OFDMA results are models of the airtime and of the channel ageing**, not measurements of any access point. The OFDMA airtime model uses nominal frame times that I assumed (section 10 lists what it leaves out).
7. **The DFS timers and the channel plans are the US and European values from memory** and change with the regulator and the year; the 6 GHz rules most of all. Check the regulator before using a channel count in a design.
8. **Nothing here ran on the Linux wireless stack.** This VM has no `cfg80211` and no `mac80211`, so the driver side of the PHY (rate control, the regulatory database, the channel context) is [WF 10](10_linux_wireless_stack_and_openwrt.md), not run here.

By the end you can:

- **turn a channel number into a frequency and a bonded block** (computed: 2.4 GHz centre is 2407 + 5n MHz, 5 GHz is 5000 + 5n and 6 GHz is 5950 + 5n; the 5 GHz plan has **25** channels of 20 MHz of which **9** need no DFS, but only **2** blocks of 80 MHz and **none** of 160 MHz without DFS; the 6 GHz band has **59, 29, 14, 7 and 3** channels at 20, 40, 80, 160 and 320 MHz, with **15** preferred scanning channels);
- **say what each extra pair of bits costs** (simulated against the formula for 1,000,000 symbols each: the signal to noise ratio for a symbol error rate of 0.001 is **6.79, 10.35, 17.63, 23.97, 30.08, 36.14 and 42.17 dB** for BPSK to 4096-QAM, so each extra 2 bits per symbol costs about **6 dB**);
- **say what the convolutional code buys** (simulated, 64 state soft Viterbi on 1,188,000 bits per point: at a bit error rate of 0.001 uncoded BPSK needs **6.79 dB** of Eb/N0 and the rate 1/2, 2/3, 3/4 and 5/6 codes need **2.62, 3.17, 3.65 and 4.19 dB**, a gain of **4.17 to 2.60 dB**);
- **say why OFDM exists** (simulated: an echo 3 dB down that arrives inside the guard interval leaves an error vector magnitude below **-200 dB**, numerically zero, while the same echo at 1,200 ns against a 0.8 µs guard gives **-12 dB**; channel estimation from 1, 2 and 4 training symbols at 25 dB SNR measured **-22.00, -23.23 and -23.98 dB** against the formula **-21.99, -23.24 and -24.03**);
- **explain why the amplifier sets the range of high MCS** (modelled: an OFDM signal has a peak to average ratio of **10.5 to 12.2 dB** at the 99.9th percentile, and meeting the EVM limit of MCS 11 needs **10.18 dB** of output back-off, **8.29 dB** less power than MCS 0 and 1 can use);
- **compute any rate in the standard** (computed and cross-checked: **600.5 Mbit/s** for HE 80 MHz MCS 11 one stream, and `tshark` agrees with the formula to within **0.3 Mbit/s** on the eight HT and VHT frames I built: 65, 72.2222, 300, 600, 78, 390, 433.3, 1733.4 and 6933.6);
- **read a preamble** (computed: the legacy preamble is **20 µs**, 11n **36**, 11ac **40**, 11ax **43.2** and 11be **47.2** µs at one stream; a legacy SIGNAL field for 6 Mbit/s and 100 bytes is `1101 | 0 | 001001100000 | 0 | 000000`; the 12 bit LENGTH caps a PPDU a legacy station can be told about at **5.484 ms**);
- **say how much of the PHY rate an aggregate delivers** (computed: HE 80 MHz MCS 11 delivers **9.1 %** of 600.5 Mbit/s with one 1500 byte MPDU, **43.3 %** with 8 and **90.4 %** with 128; reaching 90 % needs **116** MPDUs while the 5.484 ms limit allows **265**);
- **size MIMO, diversity and rank** (simulated: capacity grows **7.5 times** from 1x1 to 8x8; two receive antennas move the error slope from **0.95 to 1.97**; the second eigenmode of a 2x2 channel reaches 10 dB in **67.1 %** of channels at a mean SNR of 20 dB; two streams beat one only from **20 dB** with eigenmodes and **25 dB** with a linear MMSE receiver);
- **say when beamforming and MU-MIMO pay** (simulated: beamforming gains **3.01, 6.02 and 9.03 dB** with 2, 4 and 8 antennas; sounding 4 users costs **824 µs**; a walking client at 5.5 GHz is **0.9** correlated after **3.97 ms**, and for it MU-MIMO never beat serving one client at a time, while for a still client it won at 10 and 50 ms sounding intervals with **14.12 and 9.63** against **8.4 bit/s/Hz**);
- **say when OFDMA pays** (modelled: nine uplink RU26 transmissions of 100 bytes take **440 µs** against **1,943 µs** one by one, saving **77.4 %**, while a single station loses **27.8 %** to the trigger frame);
- **explain DFS and what it costs** (modelled with the timers from memory: **64 %** of the US 20 MHz channels need DFS, **100 %** of the 160 MHz ones, and a plan that returns to a radar channel loses it for 30 minutes per event).

## Where the numbers come from

Each experiment is a script `sN_*.py` in `prog/` with its output frozen in `v/`, run with `run.sh`. `phy.py` is the module every script imports: the data subcarrier counts by generation and width, the MCS table with its modulation and coding rate, the symbol times, the rate formula and the 8 dB gap model of [WF 11](11_rf_fundamentals_and_link_budget.md) for the SNR each MCS needs. `s1_channels.py` builds the channel plans, `s2_mod.py` the constellation and error rate tables, `s3_coding.py` the convolutional code and the Viterbi decoder, `s4_ofdm.py` the subcarrier plans, the echo test and the channel estimator, `s5_papr.py` the peak to average ratio and the amplifier, `s6_mcs.py` every rate table, `s7_plcp.py` the L-SIG field, the preamble lengths, the airtime efficiency and two detectors, `s8_radiotap.py` the `tshark` cross-check, `s9_mimo.py` capacity, diversity and rank, `s10_beamform.py` beamforming, sounding and MU-MIMO with an ageing channel, `s11_ofdma.py` the RU table and the airtime model and `s12_dfs.py` the DFS timers and downtime. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, simulated, modelled or computed here is marked **from memory**. The simulations use fixed seeds, so each run prints the same figures.

## Data and timing

| What | Size or time |
|---|---|
| Channel plans (`s1_channels.sh`) | tables only, under 1 s |
| Modulation (`s2_mod.sh`) | 14 points of 1,000,000 symbols, about 5 s |
| Convolutional code (`s3_coding.sh`) | 5 SNR points, 4 codes, 1,188,000 bits per point, about 48 s |
| OFDM plans, echoes and channel estimation (`s4_ofdm.sh`) | under 1 s |
| PAPR and the amplifier (`s5_papr.sh`) | 20,000 symbols per size, about 15 s |
| Rate tables (`s6_mcs.sh`) | tables only, under 1 s |
| PLCP, preambles, efficiency and detectors (`s7_plcp.sh`) | 20,000 frames per point, about 18 s |
| `tshark` cross-check (`s8_radiotap.sh`) | 10 frames in a pcap, under 1 s |
| MIMO capacity and error rate (`s9_mimo.sh`) | 4,000 channels per capacity point, about 16 s |
| Beamforming, sounding and MU-MIMO (`s10_beamform.sh`) | about 4 s |
| OFDMA (`s11_ofdma.sh`) | tables only, under 1 s |
| DFS (`s12_dfs.sh`) | tables only, under 1 s |

## Mapping to the Candela course

Candela sessions 2a to 2d are the PHY. They cover the OFDM waveform and the 802.11 generations, MIMO and beamforming, and OFDMA. This tutorial covers the same ground and adds the pieces the course assumes: the constellation and coding arithmetic (sections 2 and 3), the amplifier and the EVM limits (section 5), the L-SIG mechanics (section 7) and the numbers behind sounding (section 9). The DFS rules are taught by Candela in session 5a, with channel selection and radio resource management, so section 11 here is the mechanism and the cost, and [WF 05](05_wifi_roaming_and_radio_management.md) is where it is used in a plan.

---

## 1. Bands and channels

Wi-Fi uses three bands, and a **channel number** is only a way of writing a centre frequency: 2.4 GHz is `2407 + 5n` MHz (channel 14 at 2484 MHz is the exception, Japan and 802.11b only), 5 GHz is `5000 + 5n` and 6 GHz is `5950 + 5n`. So channel 6 is 2437 MHz, channel 36 is 5180 MHz and 6 GHz channel 1 is 5955 MHz. The 5 GHz and 6 GHz numbers step by 4 between adjacent 20 MHz channels, because 20 MHz is 4 steps of 5 MHz.

--- 2.4 GHz: centre = 2407 + 5 n MHz (channel 14 is 2484, Japan, 802.11b only)
ch  centre   20 MHz edges     overlap with channel 6 (20 MHz masks)
 1  2412     2402 to 2422        0 %
 2  2417     2407 to 2427        0 %
 3  2422     2412 to 2432       25 %
 4  2427     2417 to 2437       50 %
 5  2432     2422 to 2442       75 %
 6  2437     2427 to 2447      100 %
 7  2442     2432 to 2452       75 %
 8  2447     2437 to 2457       50 %
 9  2452     2442 to 2462       25 %
10  2457     2447 to 2467        0 %
11  2462     2452 to 2472        0 %
12  2467     2457 to 2477        0 %
13  2472     2462 to 2482        0 %
14  2484     2474 to 2494        0 %
non overlapping 20 MHz sets: spacing 25 MHz (22 MHz DSSS masks) [1, 6, 11]; spacing 20 MHz [1, 5, 9, 13]
40 MHz channels that fit in 2401 to 2483 MHz: 2

Two things in that table matter in practice. A 20 MHz OFDM channel is about 20 MHz wide, so channels **1, 5, 9 and 13** do not overlap each other, but the old 22 MHz DSSS mask of 802.11b needs 25 MHz and gives the familiar **1, 6, 11**. And a 40 MHz channel in 2.4 GHz takes two of those three, which is why bonding in 2.4 GHz is a bad idea in any building with more than one access point (the band holds **2** channels of 40 MHz in total).

--- 5 GHz: centre = 5000 + 5 n MHz; 20 MHz channels by sub-band (US plan, from memory)
UNII-1   36 40 44 48               5170 to 5250 MHz   no DFS
UNII-2A  52 56 60 64               5250 to 5330 MHz   DFS
UNII-2C  100 104 108 112 116 120 124 128 132 136 140 144  5490 to 5730 MHz   DFS
UNII-3   149 153 157 161 165       5735 to 5835 MHz   no DFS
UNII-4   169 173 177               5835 to 5895 MHz   no DFS
20 MHz channels without UNII-4: 25, of which no DFS: 9
width  blocks (all)   blocks (no DFS)   centre channels of the blocks (all)
 20    25              9                36 40 44 48 52 56 60 64 100 104 108 112 116 120 124 128 132 136 140 144 149 153 157 161 165
 40    12              4                38 46 54 62 102 110 118 126 134 142 151 159
 80     6              2                42 58 106 122 138 155
160     2              0                50 114
an AP whose primary channel is 40 (beacons and legacy frames go there):
   20 MHz: channels 40, centre channel 40 = 5200 MHz
   40 MHz: channels 36 40, centre channel 38 = 5190 MHz
   80 MHz: channels 36 40 44 48, centre channel 42 = 5210 MHz
  160 MHz: channels 36 40 44 48 52 56 60 64, centre channel 50 = 5250 MHz

The 5 GHz plan (US, **from memory**) is five sub-bands. The part that matters is the **DFS** column: **25** channels of 20 MHz, of which only **9** can be used without radar detection. Bonding makes it worse, because a block is DFS if any of its 20 MHz members is: **4** of 12 blocks of 40 MHz and **2** of 6 blocks of 80 MHz are free of DFS, and **neither** of the 2 blocks of 160 MHz. The last four lines show how an access point with primary channel 40 builds its wider channels: the **primary** 20 MHz is always the channel that beacons and legacy frames use, and the wider channel is the aligned block that contains it, named by its centre channel (38, 42 and 50 for 40, 80 and 160 MHz).

--- 6 GHz: centre = 5950 + 5 n MHz, n = 1, 5, 9 ... 233 (5925 to 7125 MHz, 1200 MHz in all)
20 MHz channels 59, first 1 at 5955 MHz, last 233 at 7115 MHz
 40 MHz: 29 channels, centres 3 11 19 27 35 43 51 ...
 80 MHz: 14 channels, centres 7 23 39 55 71 87 103 ...
160 MHz:  7 channels, centres 15 47 79 111 143 175 207
320 MHz:  3 channels, centres 31 95 159
320 MHz, second (offset by 160 MHz) set: 63 127 191, so 320-1 and 320-2 overlap
PSC (preferred scanning) channels: every 16th 20 MHz channel from 5: 5 21 37 53 69 85 101 117 133 149 165 181 197 213 229 (15 of them, from memory of the rule)

The 6 GHz band (5925 to 7125 MHz, **from memory**) is the clean slate: **59** channels of 20 MHz, no legacy devices, no DFS in most rules and room for **7** channels of 160 MHz and **3** of 320 MHz. Two of its rules are design inputs. The **preferred scanning channels** (every 16th channel, **15** of them) exist because a client cannot afford to scan 59 channels, so access points are expected to put a discoverable beacon (or a pointer in a 2.4 or 5 GHz beacon) on them. And the 320 MHz channels come in two overlapping sets, `320-1` and `320-2`, offset by 160 MHz, so a plan must choose a set.

## 2. Modulation: what a symbol carries and what it costs

A subcarrier carries one **constellation point** per symbol. With unit average energy the points of BPSK, QPSK, 16-QAM and larger squares sit closer together as the order grows, and the **minimum distance** decides the error rate. The table is computed.

--- constellation geometry (unit average energy)
M      bits  min distance  d_min^2 (dB re BPSK)
2       1    2.0000          0.00
4       2    1.4142         -3.01
16      4    0.6325        -10.00
64      6    0.3086        -16.23
256     8    0.1534        -22.30
1024   10    0.0766        -28.34
4096   12    0.0383        -34.36

Each step from one square QAM to the next with 2 more bits per symbol moves the minimum distance squared down by about **6 dB** (16-QAM to 64-QAM is -6.23 dB, then -6.07, -6.04 and -6.02), which is the whole story of why higher MCS needs more SNR. The simulation checks the standard formula for the symbol error rate over additive white Gaussian noise.

--- symbol error rate over AWGN, simulated (1,000,000 symbols) against the formula
M      Es/N0 dB   simulated    formula
2         6       2.33e-03     2.39e-03
2         9       2.80e-05     3.36e-05
4         9       4.84e-03     4.82e-03
4        12       7.40e-05     6.86e-05
16       16       7.13e-03     7.15e-03
16       19       9.40e-05     1.01e-04
64       22       1.03e-02     1.05e-02
64       25       1.79e-04     1.82e-04
256      28       1.21e-02     1.20e-02
256      31       2.54e-04     2.23e-04
1024     34       1.28e-02     1.28e-02
1024     37       2.31e-04     2.45e-04
4096     40       1.35e-02     1.33e-02
4096     43       2.45e-04     2.59e-04

The simulated and formula columns agree to the precision a million symbols allows, and the only larger gaps are where only a few tens of errors were counted (for example 2.80e-05 against 3.36e-05 is about 28 errors). The needed SNR for a target error rate is then a bisection on the formula.

--- Es/N0 needed for a symbol error rate of 1e-3 and 1e-5 (formula, bisection)
M      1e-3 dB   1e-5 dB   step from previous (1e-3)
2        6.79      9.59
4       10.35     12.90     +3.56
16      17.63     20.06     +7.28
64      23.97     26.36     +6.34
256     30.08     32.46     +6.12
1024    36.14     38.50     +6.06
4096    42.17     44.53     +6.03

**BPSK needs 6.79 dB for a symbol error rate of 1e-3 and 4096-QAM needs 42.17 dB**, a spread of **35.4 dB**. The step from QPSK to 16-QAM is **+7.28 dB**, a little above the **6.99 dB** that the minimum distance table gives, because a 16-QAM point has more nearest neighbours; the later steps converge to **+6.03 dB**, the 6.02 dB of the factor of 4 in energy per added bit pair. This is the mechanism behind every rate adaptation table: **bits per symbol grow linearly while the SNR needed grows linearly in decibels, which is exponentially in power.** It also answers why 1024-QAM (introduced in 802.11ax) matters only within a few metres of the access point, and why 4096-QAM in 802.11be needs a transmitter and a receiver whose own error vector magnitude is better than the -35 dB that the table of section 5 already demands for 1024-QAM (the limit for 4096-QAM is about -38 dB, **from memory**).

## 3. Coding: the convolutional code and what it buys

802.11 protects every bit with a **convolutional code** of rate 1/2 (constraint length 7, generator polynomials 133 and 171 in octal) and then **punctures** it, removing coded bits in a fixed pattern, to reach rates 2/3, 3/4 and 5/6. The decoder is a Viterbi decoder with 64 states. The script builds the encoder, the puncturing and a soft decision decoder over BPSK in AWGN, and measures the bit error rate against Eb/N0, the energy per **information** bit, which is the fair axis when the rate changes.

BPSK over AWGN, soft decision Viterbi with 64 states, 5 x 400 blocks of 594 bits (1,188,000 bits) per point
Eb/N0 dB   uncoded BPSK     1/2         2/3         3/4         5/6
   2       3.75e-02        4.72e-03        2.08e-02        5.52e-02        1.55e-01
   3       2.29e-02        3.91e-04        1.59e-03        6.13e-03        2.42e-02
   4       1.25e-02        2.19e-05        9.51e-05        3.69e-04        2.01e-03
   5       5.95e-03        <3e-06        9.26e-06        8.42e-06        4.80e-05
   6       2.39e-03        <3e-06        <3e-06        <3e-06        1.18e-05

Eb/N0 needed (log-linear interpolation between grid points; points with fewer than 10 errors are skipped)
target BER   uncoded       1/2         2/3         3/4         5/6
1e-03         6.79 dB     2.62 dB     3.17 dB     3.65 dB     4.19 dB
1e-04         8.40 dB     3.47 dB     3.98 dB     4.35 dB     4.80 dB

Read the second table. For a bit error rate of 1e-3 an uncoded BPSK link needs **6.79 dB**, and the coded links need **2.62 dB (rate 1/2), 3.17 (2/3), 3.65 (3/4) and 4.19 dB (5/6)**: the **coding gain** is **4.17, 3.62, 3.14 and 2.60 dB**. At 1e-4 the gains are **4.93, 4.42, 4.05 and 3.60 dB** (8.40 dB uncoded). Two consequences follow.

- **A higher rate code gives away gain.** Going from 1/2 to 5/6 costs **1.57 dB** at 1e-3 (4.19 minus 2.62) for **1.67 times** the throughput. That is a good trade whenever the link has SNR to spare, which is the whole content of the MCS ladder: at fixed modulation the coding rate is the fine step and the constellation is the coarse step.
- **The gain is real but finite.** It is 2.6 to 4.9 dB, comparable to doubling the transmit power or halving the distance in free space (6 dB), and it is the reason the lowest MCS (BPSK, rate 1/2) works at a signal to noise ratio where uncoded BPSK would fail.

I did not simulate LDPC. 802.11n made it optional and 802.11ax and 802.11be use it for longer blocks, with a gain over the convolutional code of **about 1 to 2 dB (from memory)** and a decoder that costs more silicon. The effect of this is that high MCS rate numbers in the standard assume the better code, so a convolutional-only device (some very cheap IoT chips) sees a smaller advantage than the tables suggest.

## 4. OFDM: many slow subcarriers and a guard interval

A single carrier at 20 MHz bandwidth sends a symbol every 50 ns, shorter than the echoes of a room, so each symbol smears into the next. **Orthogonal frequency division multiplexing (OFDM)** sends instead **many slow carriers at once**: the 20 MHz channel is split into 64 subcarriers 312.5 kHz apart, each carrying a symbol of 3.2 µs plus a **guard interval** (a cyclic prefix, a copy of the end of the symbol placed in front of it) of 0.8 µs. An echo that arrives within the guard interval only rotates and scales each subcarrier, which one complex number per subcarrier undoes. The table gives the subcarrier plans.

--- subcarrier plans (spacing = sample rate / FFT size; useful symbol time = 1 / spacing)
standard        width  FFT   spacing kHz  useful us  tones used  data  pilots  occupied MHz
11a/g            20    64    312.500        3.2       52        48      4      16.25
11n 20           20    64    312.500        3.2       56        52      4      17.50
11n 40           40   128    312.500        3.2      114       108      6      35.62
11ac 80          80   256    312.500        3.2      242       234      8      75.62
11ac 160        160   512    312.500        3.2      484       468     16     151.25
11ax 20          20   256     78.125       12.8      242       234      8      18.91
11ax 40          40   512     78.125       12.8      484       468     16      37.81
11ax 80          80  1024     78.125       12.8      996       980     16      77.81
11ax 160        160  2048     78.125       12.8     1992      1960     32     155.62
11be 320        320  4096     78.125       12.8     3984      3920     64     311.25

The rule: **spacing is the sample rate over the FFT size, and the useful symbol time is its inverse.** 802.11a to 802.11ac use 312.5 kHz and 3.2 µs. 802.11ax uses **four times** the FFT, 78.125 kHz and **12.8 µs**, so the same 0.8 µs guard costs 5.9 % instead of 20 % (computed in problem C), and so it has **234 data tones in 20 MHz** against 52 in 802.11n. Each width also leaves guard tones at both band edges and in the middle for the carrier: 802.11ax at 20 MHz uses **242 of 256** tones, an occupied bandwidth of **18.91 MHz**. The pilots (**4, 6, 8, 16, 32 and 64** by width) are the known tones the receiver uses to follow the phase and the timing drift in each symbol.

**What the guard interval does**, simulated: a 20 MHz symbol stream after an echo 3 dB below the direct path, with perfect channel knowledge and no noise, measured as error vector magnitude (EVM).

--- EVM (dB) of one 20 MHz OFDM symbol stream after an echo 3 dB below the direct path; no noise, perfect channel knowledge
echo delay ns:    50   200   400   800   1000  1200  1600  3200  4000
GI 0.4 us  EVM dB:   none  none  none   -12   -10    -9    -8    -4    -3
GI 0.8 us  EVM dB:   none  none  none  none   -15   -12    -9    -6    -3
GI 1.6 us  EVM dB:   none  none  none  none  none  none  none    -4    -4
GI 3.2 us  EVM dB:   none  none  none  none  none  none  none  none    -9
("none" means the error is below -200 dB: numerical zero, the echo fell inside the guard interval)

The word "none" means the error is below -200 dB, which is numerical zero: **inside the guard interval OFDM is exact**, and it degrades only when the echo is later than the guard. A 0.8 µs guard handles an echo at **1,000 ns with -15 dB EVM**, a 1.6 µs guard handles all echoes up to 1,600 ns exactly and a 3.2 µs guard still gives **-9 dB at 4,000 ns**. The guard of 0.8 µs corresponds to a path difference of **240 m**, more than an office or a warehouse produces; the long guards (1.6 and 3.2 µs) exist for outdoor and stadium use, which is why 802.11ax added them.

**Channel estimation**: the receiver measures the channel from the known training symbols (the LTF) by least squares and divides by it. Noise in that estimate adds to the noise of the data, and averaging over more training symbols reduces it:

--- EVM (dB) at a per tone SNR of 25 dB on a flat channel, channel estimated from 1, 2 and 4 training symbols (least squares, 64 FFT)
                perfect CSI      1 LTF        2 LTF        4 LTF
simulated        -25.01        -22.00       -23.23       -23.98
-25 + 10 log10(1 + 1/n):  -25.00        -21.99       -23.24       -24.03

With one training symbol the estimate has the same noise as the data, so the receiver's effective noise is **doubled**; the formula `-SNR + 10 log10(1 + 1/n)` gives **-21.99 dB** with one LTF against **-25.00 dB** with perfect knowledge, and the simulation gave **-22.00**. Two LTFs cost **1.76 dB** and four LTFs **0.97 dB**. That is why a multi stream preamble repeats the LTF once per stream: the cost of the second stream's training is real and is part of the preamble length in section 7. It is also why the 802.11ax preamble offers a 2x or 4x LTF: a longer LTF is a better estimate and a harder-to-fool receiver in a long guard environment.

---

## 5. The peak to average ratio and the amplifier

OFDM adds up 52 to 3,984 independent subcarriers, so its instantaneous power is a sum of many random terms, which is Gaussian, and **occasionally very large**. The ratio of the peak power to the mean power (PAPR) is the number that the power amplifier has to be built for.

--- PAPR of OFDM symbols (QPSK, 4 times oversampled): the value that the stated share of symbols stays below
FFT   tones   symbols    90 %     99 %     99.9 %    99.99 %
64    52      20000     8.47     9.67    10.52     11.19
256   242     20000     9.40    10.44    11.35     12.26
1024  996     20000    10.09    11.03    11.88     12.36
4096  3984    10000    10.65    11.47    12.15      (needs more symbols)

The PAPR at the 99.9th percentile is **10.52 dB at 64 tones and 12.15 dB at 4,096**: it grows only as the logarithm of the number of tones, so wider channels cost an extra **1.6 dB** of peak, not an extra 6. An amplifier has a saturation power it cannot exceed, so the average power has to sit well below it or the peaks are clipped. Clipping adds in-band distortion (EVM) and out-of-band spectral regrowth (leakage into the next channel). The table runs the signal through a **Rapp** amplifier model, a standard smooth saturating curve; the **input back-off** (IBO) is how far the average input power sits below saturation.

--- Rapp amplifier (smoothness 2), 256 FFT symbols; input back-off = saturation power over average input power
IBO dB   output backoff dB   gain compression dB   EVM dB   adjacent channel leakage dBc
  -2       1.89               -4.22              -10.98   -19.03
   0       2.68               -2.90              -12.98   -21.06
   1       3.17               -2.33              -14.19   -22.30
   2       3.72               -1.84              -15.59   -23.71
   3       4.33               -1.41              -17.17   -25.30
   4       5.01               -1.06              -18.97   -27.10
   5       5.74               -0.78              -21.00   -29.11
   6       6.54               -0.56              -23.27   -31.37
   7       7.38               -0.39              -25.78   -33.87
   8       8.26               -0.27              -28.55   -36.61
   9       9.18               -0.18              -31.55   -39.59
  10      10.12               -0.12              -34.77   -42.78
  12      12.05               -0.05              -41.75   -49.72
  14      14.02               -0.02              -49.24   -57.18

Two things are visible. **EVM falls fast with back-off**: -15.59 dB at an input back-off of 2 dB, -23.27 dB at 6 dB and -34.77 dB at 10 dB, about **2.4 dB of EVM per decibel** of back-off (19.18 dB over 8 dB). And **leakage falls with it**: -23.71 dBc at 2 dB and -42.78 dBc at 10 dB. What matters for range is the **output back-off** of the second column: the average transmit power is the saturation power minus that number. It is a little larger than the input back-off because the amplifier also compresses the signal (6.54 dB against 6 dB at an input back-off of 6, with a compression of 0.56 dB).

Each MCS has a limit on the transmitter's own EVM (the limits are **from memory** of the 802.11ax table), and the table below interpolates the amplifier table to find the back-off each limit needs.

--- output backoff each MCS needs to meet its EVM limit (the limits are from memory of the 802.11ax table); the table above, interpolated
MCS  modulation  EVM limit dB   output backoff dB   output power relative to MCS 11 (dB)
 0   BPSK           -5            1.89               +8.29
 1   QPSK          -10            1.89               +8.29
 2   QPSK          -13            2.69               +7.49
 3   16-QAM        -16            3.88               +6.30
 4   16-QAM        -19            5.02               +5.16
 5   64-QAM        -22            6.10               +4.08
 6   64-QAM        -25            7.12               +3.06
 7   64-QAM        -27            7.77               +2.41
 8   256-QAM       -30            8.70               +1.48
 9   256-QAM       -32            9.31               +0.87
10   1024-QAM      -35           10.18               +0.00
11   1024-QAM      -35           10.18               +0.00

**MCS 11 needs 10.18 dB of output back-off and MCS 0 or 1 about 1.89 dB, so a radio that is allowed to transmit at maximum power for MCS 0 must lower it by 8.29 dB for MCS 11.** (The MCS 0 and 1 rows sit at the edge of the table, so 1.89 dB is a lower bound; the true figure is at least that.) This is the mechanism behind a fact that surprises people: **the regulatory limit on transmit power is a ceiling, not what the radio does.** A chip's datasheet gives its output power per MCS, and the high MCS rows are several decibels lower, so an access point's range at its fastest rate is smaller than at its slowest by the back-off **plus** the SNR difference of [WF 11](11_rf_fundamentals_and_link_budget.md). It is also why a transmitter's EVM is a headline specification for a Wi-Fi 6 or 7 radio and why 4096-QAM only works within a few metres.

## 6. The MCS tables and the rate formula

**Rate = data subcarriers x bits per subcarrier x coding rate x spatial streams / (useful symbol + guard).** The script builds every table in the standard from that and from `phy.py`. Start with the oldest.

--- legacy 802.11b (DSSS and CCK): chips per second 11 million, so the rate is symbols per second times bits per symbol
1 Mbit/s DSSS BPSK   11 chips per symbol, 1.000 Msymbols/s x 1 bits = 1.00
2 Mbit/s DSSS QPSK   11 chips per symbol, 1.000 Msymbols/s x 2 bits = 2.00
5.5 Mbit/s CCK        8 chips per symbol, 1.375 Msymbols/s x 4 bits = 5.50
11 Mbit/s CCK         8 chips per symbol, 1.375 Msymbols/s x 8 bits = 11.00

802.11b predates OFDM: it spreads each bit over 11 chips (DSSS, **1 and 2 Mbit/s**) or 8 chips (CCK, **5.5 and 11 Mbit/s**) at 11 million chips per second. It matters today for one reason: **a 2.4 GHz network that still allows 1 and 2 Mbit/s lets any 802.11b client in, and every beacon, every management frame and every broadcast at those rates holds the channel 54 times longer** than at 54 Mbit/s. Turning off the low rates is the single most effective cleanup of an old 2.4 GHz network.

--- 802.11a/g, 48 data subcarriers, symbol 4.0 us: N_CBPS = 48 x bits, N_DBPS = N_CBPS x coding rate
Mbit/s  modulation  coding   N_CBPS   N_DBPS   computed
   6    BPSK        1/2        48       24     6.0
   9    BPSK        3/4        48       36     9.0
  12    QPSK        1/2        96       48     12.0
  18    QPSK        3/4        96       72     18.0
  24    16-QAM      1/2       192       96     24.0
  36    16-QAM      3/4       192      144     36.0
  48    64-QAM      2/3       288      192     48.0
  54    64-QAM      3/4       288      216     54.0

The 802.11a/g table is the one to memorise as a pattern: **48 data subcarriers, a 4.0 µs symbol, so N_CBPS is 48 times the bits per subcarrier and N_DBPS is N_CBPS times the coding rate.** 6 Mbit/s is 24 data bits per symbol every 4 µs. The management and control frames of the whole network use these rates, because every station including the oldest has to be able to decode them.

--- 802.11n (HT), 20 and 40 MHz, long and short guard interval, one stream; four streams are the one stream rate times 4
MCS  modulation  coding   20 MHz 0.8   20 MHz 0.4   40 MHz 0.8   40 MHz 0.4
 0   BPSK        1/2         6.5          7.2         13.5         15.0
 1   QPSK        1/2        13.0         14.4         27.0         30.0
 2   QPSK        3/4        19.5         21.7         40.5         45.0
 3   16-QAM      1/2        26.0         28.9         54.0         60.0
 4   16-QAM      3/4        39.0         43.3         81.0         90.0
 5   64-QAM      2/3        52.0         57.8        108.0        120.0
 6   64-QAM      3/4        58.5         65.0        121.5        135.0
 7   64-QAM      5/6        65.0         72.2        135.0        150.0
HT peak: MCS 7, 4 streams (MCS 31), 40 MHz, short GI = 600.0 Mbit/s

802.11n added **MIMO** (up to 4 streams, so MCS 0 to 31 where MCS 8 to 15 are 2 streams of MCS 0 to 7), **40 MHz channels** (108 data subcarriers against 52), a **short guard interval** of 0.4 µs (11 % shorter symbols, **+11.1 %** rate) and one coding rate more, 5/6. The peak is **600 Mbit/s**: MCS 7 (64-QAM 5/6), 4 streams, 40 MHz and the short guard.

--- 802.11ac (VHT), one stream, long guard interval; MCS 9 is not defined for 20 MHz with one or two streams
MCS  modulation  coding   20 MHz   40 MHz   80 MHz   160 MHz
 0   BPSK        1/2        6.5     13.5     29.2      58.5
 1   QPSK        1/2       13.0     27.0     58.5     117.0
 2   QPSK        3/4       19.5     40.5     87.8     175.5
 3   16-QAM      1/2       26.0     54.0    117.0     234.0
 4   16-QAM      3/4       39.0     81.0    175.5     351.0
 5   64-QAM      2/3       52.0    108.0    234.0     468.0
 6   64-QAM      3/4       58.5    121.5    263.2     526.5
 7   64-QAM      5/6       65.0    135.0    292.5     585.0
 8   256-QAM     3/4       78.0    162.0    351.0     702.0
 9   256-QAM     5/6       86.7    180.0    390.0     780.0
VHT peak: MCS 9, 8 streams, 160 MHz, short GI = 6933.3 Mbit/s

802.11ac made the MCS index (0 to 9) independent of the number of streams, which is now stated separately, added **80 and 160 MHz** channels, **256-QAM** (MCS 8 and 9) and downlink **MU-MIMO**. MCS 9 is not defined for some width and stream combinations (20 MHz with one or two streams) because the coded bits per symbol would not divide evenly into the interleaver (**from memory** of the reason). The peak is **6,933.3 Mbit/s**, with 8 streams, 160 MHz and the short guard.

--- 802.11ax (HE), one stream, guard interval 0.8 us
MCS  modulation  coding   20 MHz   40 MHz   80 MHz   160 MHz   N_DBPS at 80 MHz
 0   BPSK        1/2        8.6     17.2     36.0      72.1      490
 1   QPSK        1/2       17.2     34.4     72.1     144.1      980
 2   QPSK        3/4       25.8     51.6    108.1     216.2     1470
 3   16-QAM      1/2       34.4     68.8    144.1     288.2     1960
 4   16-QAM      3/4       51.6    103.2    216.2     432.4     2940
 5   64-QAM      2/3       68.8    137.6    288.2     576.5     3920
 6   64-QAM      3/4       77.4    154.9    324.3     648.5     4410
 7   64-QAM      5/6       86.0    172.1    360.3     720.6     4900
 8   256-QAM     3/4      103.2    206.5    432.4     864.7     5880
 9   256-QAM     5/6      114.7    229.4    480.4     960.8     6533
10   1024-QAM    3/4      129.0    258.1    540.4    1080.9     7350
11   1024-QAM    5/6      143.4    286.8    600.5    1201.0     8167
HE peak: MCS 11, 8 streams, 160 MHz, 0.8 us = 9607.8 Mbit/s (HE has no 0.4 us guard: the choices are 0.8, 1.6 and 3.2 us)
HE MCS 11, 80 MHz, one stream, guard 0.8 / 1.6 / 3.2 us: 600.5 / 567.1 / 510.4 Mbit/s

802.11ax changed the symbol: four times as many subcarriers, a 12.8 µs useful time, three guard choices (**0.8, 1.6 and 3.2 µs**, with no 0.4 µs), **1024-QAM** (MCS 10 and 11) and **OFDMA**. The effect of a longer symbol is that a guard of 0.8 µs is 5.9 % of it and a 160 MHz channel reaches **1,960 data subcarriers**. The rate of one stream at MCS 11, 80 MHz and guards of 0.8, 1.6 and 3.2 µs is **600.5, 567.1 and 510.4 Mbit/s**: the long guard for outdoor use costs **15 %** of the rate. The peak is **9,607.8 Mbit/s**, which is **1.39 times** the 802.11ac peak and not a factor of 10, because 802.11ax added only **1.25 times** the bits per subcarrier (10 against 8) and **1.11 times** the data subcarriers per microsecond (1,960 / 13.6 = 144.1 against 468 / 3.6 = 130.0 at 160 MHz): 1.25 x 1.109 = 1.39. The four times denser tones were almost cancelled by the four times longer symbol.

--- 802.11be (EHT), one stream, guard interval 0.8 us, with the two new MCS (4096-QAM)
MCS  modulation  coding   80 MHz   160 MHz   320 MHz
 9   256-QAM     5/6      480.4     960.8    1921.6
10   1024-QAM    3/4      540.4    1080.9    2161.8
11   1024-QAM    5/6      600.5    1201.0    2402.0
12   4096-QAM    3/4      648.5    1297.1    2594.1
13   4096-QAM    5/6      720.6    1441.2    2882.4
EHT peak: MCS 13, 16 streams, 320 MHz = 46117.6 Mbit/s

802.11be adds **4096-QAM** (MCS 12 and 13), **320 MHz** channels (3,920 data subcarriers) and up to 16 streams, giving **46,117.6 Mbit/s** at the peak. The tables show the pattern: each generation multiplies one or two of the four factors.

--- what changed from one generation to the next, peak rates by the same formula
standard   width  streams  bits  coding  subcarriers  symbol us  peak Mbit/s  step
11a          20       1      6    0.75        48         4.0          54.0
11n          40       4      6    0.83       108         3.6         600.0   11.11x
11ac        160       8      8    0.83       468         3.6        6933.3   11.56x
11ax        160       8     10    0.83      1960        13.6        9607.8   1.39x
11be        320      16     12    0.83      3920        13.6       46117.6   4.80x

**The 11n step (11.11 times) came from 4 streams, 2.25 times the subcarriers, the 5/6 code and the short symbol, the 11ac step (11.56 times) from 2 times the streams, 4.33 times the subcarriers and 1.33 times the bits, the 11ax step (1.39 times) from bits and subcarrier density alone and the 11be step (4.80 times) from 2x the width, 2x the streams and 20 % more bits.** The conclusion for an interview answer: since 2013 the peak rate has been bought almost entirely with **more spectrum and more antennas**, which a phone with two antennas and an 80 MHz channel does not have. A typical phone tops out at 2 streams, 80 or 160 MHz, so its real peak is **2,402 Mbit/s** for HE at 160 MHz with 2 streams (**1,201** at 80 MHz), and nothing like 9.6 Gbit/s (problem B works this out).

**The external check.** The rate formula agrees with the independent dissector in `tshark` (version 4.2.2): the script builds 10 radiotap frames (HT, VHT and HE rate fields) in a pcap, and `tshark` decodes them with its own tables.

built a pcap of 10 radiotap frames (HT, VHT and HE rate fields); tshark decodes them with its own tables
the formula column is subcarriers x bits x coding rate x streams / symbol time
frame                              tshark Mbit/s   formula Mbit/s
HT  MCS 7  20 MHz long GI          65                   65.0
HT  MCS 7  20 MHz short GI         72.2222              72.2
HT  MCS 15 40 MHz short GI         300                 300.0
HT  MCS 31 40 MHz short GI         600                 600.0
VHT MCS 8  20 MHz 1 ss long GI     78                   78.0
VHT MCS 9  80 MHz 1 ss long GI     390                 390.0
VHT MCS 9  80 MHz 1 ss short GI    433.3               433.3
VHT MCS 9  160 MHz 2 ss short GI   1733.4             1733.3
VHT MCS 9  160 MHz 8 ss short GI   6933.6             6933.3
HE  MCS 11 80 MHz 1 ss GI 0.8      n/a                 600.5
the HE frame: tshark decodes MCS 11, bandwidth code 2, guard interval code 0 and 1 space-time stream(s) from the HE field, but prints no rate

`tshark` agrees with the formula for every HT and VHT row, and the only differences are rounding in its table (1733.4 against 1733.3 and 6933.6 against 6933.3). For the HE frame it decodes the MCS (11), the bandwidth code (2 for 80 MHz), the guard interval code and the number of streams, **but prints no rate**, so the HE rate of 600.5 Mbit/s is checked only against the peak rate arithmetic. That is a real limit of this check and I state it rather than hide it.

## 7. The PLCP: preamble, L-SIG and what a frame costs

Every transmission is a **PPDU**: a **preamble** (training and signalling fields), then the data. The first three fields are the same in every generation after 802.11a, so that **every station can decode the start of every frame**: the L-STF (8 µs, for detection and coarse frequency), the L-LTF (8 µs, for the channel and the fine frequency) and the L-SIG (4 µs, one BPSK symbol saying the rate and length). The script builds an L-SIG field bit by bit.

--- the legacy SIGNAL field (L-SIG): 24 bits = RATE 4, reserved 1, LENGTH 12 (least significant bit first), parity 1, tail 6
6 Mbit/s, PSDU of 100 bytes:  1101 | 0 | 001001100000 | 0 | 000000
rate 1/2 convolutional code: 24 bits become 48 coded bits, sent as BPSK on the 48 data subcarriers of one 4.0 us symbol:
111010111001100011000010001111100111000000000000
a legacy receiver computes the frame time as 20 + 4 x ceil((16 + 8 x LENGTH + 6) / N_DBPS) us: 160 us for LENGTH 100 at 6 Mbit/s

The fields are RATE (4 bits), a reserved bit, a 12 bit LENGTH sent least significant bit first, an even parity bit and 6 tail bits, **24 in all, coded at rate 1/2 to 48 bits** and sent as BPSK on the 48 data subcarriers of one 4 µs symbol. A legacy receiver computes the frame time as `20 + 4 x ceil((16 + 8 x LENGTH + 6) / N_DBPS)` µs: **160 µs for 100 bytes at 6 Mbit/s**.

That is the whole **protection mechanism** of every modern frame: an HT, VHT or HE sender writes a RATE of 6 Mbit/s and a LENGTH chosen so that the legacy station's computed time equals the real PPDU time.

--- how an HT, VHT or HE sender fills LENGTH so that a legacy station keeps quiet for the whole PPDU (rate 6 Mbit/s, N_DBPS 24)
PPDU time us   n = ceil((T - 20) / 4)   LENGTH = 3n - 3   legacy station computes   m = 2 (HE) gives LENGTH   and computes
     100           20                       57                100 us                  55                        100 us
     500          120                      357                500 us                 355                        500 us
    1000          245                      732               1000 us                 730                       1000 us
    2000          495                     1482               2000 us                1480                       2000 us
    5484         1366                     4095               5484 us                4093                       5484 us
LENGTH is 12 bits, so at most 4095: the longest PPDU a legacy station can be told about is 5484 us = 5.484 ms

The rule is `LENGTH = 3n - 3` for `n = ceil((T - 20) / 4)` (an HE sender may use `m = 2`, which makes LENGTH two lower and gives the same computed time; why HE does so I did not look up). Because LENGTH has 12 bits the largest value is **4095, which corresponds to a PPDU of 5,484 µs**, and so **5.484 ms is the longest PPDU a legacy station can be told about**. That is the origin of the 5.484 ms limit on the length of a PPDU in 802.11n and 802.11ac (**from memory**), and it is the limit used in problem D.

Each generation then adds its own signalling after L-SIG. **How long is the preamble?** The durations of the fields are **from memory**; the table adds them up.

--- preamble length by generation (from memory of the field durations), in us
field order: L-STF 8, L-LTF 8, L-SIG 4, then HT-SIG 8 + HT-STF 4 + HT-LTF 4 each | VHT-SIG-A 8, VHT-STF 4, VHT-LTF 4 each, VHT-SIG-B 4 | RL-SIG 4, HE-SIG-A 8, HE-STF 4, HE-LTF (3.2, 6.4 or 12.8 + guard) each | RL-SIG 4, U-SIG 8, EHT-SIG, EHT-STF 4, EHT-LTF each
generation               1 stream   2 streams   4 streams   8 streams
11a (legacy)                20.0        20.0        20.0        20.0
11n mixed format            36.0        40.0        48.0         n/a
11ac                        40.0        44.0        52.0        68.0
11ax, 2x LTF, GI 0.8        43.2        50.4        64.8        93.6
11ax, 4x LTF, GI 3.2        52.0        68.0       100.0       164.0
11be (EHT-SIG 4 us)         47.2        54.4        68.8        97.6

The preamble grows with streams (one extra LTF per stream) and with the generation: **20 µs for 11a, 36 for 11n, 40 for 11ac, 43.2 for 11ax and 47.2 for 11be** at one stream, and **93.6 µs for 8 streams of 11ax**. A 2x LTF with a 0.8 µs guard is the common case in 802.11ax; a 4x LTF with a 3.2 µs guard (the outdoor mode) lengthens it to **52 µs** at one stream and **164 µs** at 8.

**What this costs.** A short frame at a high rate is mostly preamble, and the table shows the share of a PPDU that carries payload.

--- how much of a PPDU is payload: one 1500 byte MPDU against an aggregate, HE, 1 stream, GI 0.8, 2x LTF
PHY setting              MPDUs  bytes     PPDU us   preamble share   payload bits per us   MAC goodput Mbit/s   share of PHY rate
HE 80 MHz MCS 11             1     1540       70.4     61.4 %            170.5                54.6               9.1 %
HE 80 MHz MCS 11             2     3080       97.6     44.3 %            245.9                97.1              16.2 %
HE 80 MHz MCS 11             8    12320      220.0     19.6 %            436.4               259.8              43.3 %
HE 80 MHz MCS 11            32    49280      709.6      6.1 %            541.1               447.0              74.4 %
HE 80 MHz MCS 11           128   197120     2681.6      1.6 %            572.8               542.5              90.4 %
HE 20 MHz MCS 7              1     1540      192.8     22.4 %             62.2                35.1              40.7 %
HE 20 MHz MCS 7              2     3080      342.4     12.6 %             70.1                48.8              56.7 %
HE 20 MHz MCS 7              8    12320     1199.2      3.6 %             80.1                71.2              82.7 %
HE 20 MHz MCS 7             32    49280     4626.4      0.9 %             83.0                80.4              93.5 %
HE 20 MHz MCS 0              1     1540     1484.8      2.9 %              8.1                 7.3              85.3 %
HE 20 MHz MCS 0              2     3080     2912.8      1.5 %              8.2                 7.8              91.1 %

**HE 80 MHz MCS 11 with one 1500 byte MPDU is a PPDU of 70.4 µs, of which 43.2 µs (61.4 %) is preamble, and the MAC goodput is 54.6 Mbit/s: 9.1 % of the PHY rate of 600.5.** With 8 MPDUs in an aggregate it is 259.8 Mbit/s (43.3 %), with 32 it is 447.0 (74.4 %) and with 128 it is 542.5 (90.4 %). At the other end, HE 20 MHz MCS 0 (8.6 Mbit/s) has a preamble of only 2.9 % of a 1500 byte frame, because the data takes 1.4 ms. **The faster the PHY, the larger the aggregate needed to use it**, and the aggregation of [WF 03](03_wifi_mac.md) is therefore not an optimisation but the only way the high MCS rates reach the user. (The model counts the preamble and the payload, and not the contention, SIFS and ACK of [WF 03](03_wifi_mac.md), which lower every figure further.)

**Telling PPDU types apart.** A receiver does not know which generation a frame is from until it decodes the symbols after the L-SIG. The tricks are in the constellation of those symbols:

--- telling the PPDU types apart from the symbols after L-SIG (48 tones per symbol, flat channel, known)
legacy 6 Mbit/s data: BPSK on the real axis | HT-SIG: both symbols on the imaginary axis (Q-BPSK) | VHT-SIG-A: real, then imaginary
SNR per tone     legacy       HT          VHT      (share classified correctly, 20,000 frames each)
   -9 dB        61.22 %     61.98 %     61.07 %
   -6 dB        85.77 %     86.09 %     85.36 %
   -3 dB        98.89 %     99.06 %     99.02 %
    0 dB       100.00 %    100.00 %    100.00 %
    3 dB       100.00 %    100.00 %    100.00 %

HE and EHT repeat L-SIG as RL-SIG, so the receiver correlates the symbol after L-SIG with L-SIG; threshold at half the expected value snr / (1 + snr)
SNR per tone    repeat detected    false alarm on another symbol   (20,000 frames each)
   -3 dB           96.95 %                 6.12 %
    0 dB           99.96 %                 1.32 %
    3 dB          100.00 %                 0.23 %
    6 dB          100.00 %                 0.09 %
    9 dB          100.00 %                 0.04 %

A legacy frame has BPSK data on the **real axis** after L-SIG; an HT frame puts its HT-SIG on the **imaginary axis** (Q-BPSK, rotated by 90 degrees) for both symbols; a VHT frame has VHT-SIG-A with the first symbol on the real and the second on the imaginary axis. A receiver that measures which axis carries the energy classifies the frame correctly in **98.89 to 99.06 % of 20,000 frames at -3 dB per tone** and in 100 % at 0 dB. HE and EHT repeat the L-SIG as an RL-SIG, so the receiver correlates the symbol after L-SIG with L-SIG itself and decides there is a repeat above a threshold at half the expected value `snr / (1 + snr)`: the repeat is detected in **99.96 %** of frames at 0 dB with a false alarm of **1.32 %** on a symbol that is not a repeat. (The detectors are my models of the idea, not the circuits of any chip; the standard's own detection is **from memory**.)

## 8. MIMO: capacity, diversity and rank

With several antennas at both ends the channel is a matrix **H** (receive antennas by transmit antennas). Three different things can be bought with the extra antennas, and an interview answer separates them: **diversity** (a more reliable link), **array gain** (more SNR) and **spatial multiplexing** (more streams).

**Capacity** of an i.i.d. Rayleigh channel with a fixed total transmit power:

--- ergodic capacity in bit/s/Hz of i.i.d. Rayleigh channels, total transmit power fixed (4,000 channels each)
SNR dB    1x1     1x2     2x2     3x3     4x4    8x8    (the last gain over 1x1)
   0     0.87    0.92    1.70    2.52    3.36   6.70   7.7x
   5     1.73    1.88    3.30    4.92    6.58  13.07   7.6x
  10     2.86    3.17    5.57    8.24   10.96  21.80   7.6x
  20     5.91    6.29   11.32   16.71   22.15  43.96   7.4x
  30     9.13    9.63   17.76   26.39   34.94  69.22   7.6x

At 20 dB SNR a 1x1 link carries **5.91 bit/s/Hz**, a 2x2 link **11.32** and an 8x8 link **43.96**, **7.4 times** the single antenna figure, and the ratio is about the same (7.4 to 7.7) at every SNR in the table. The capacity grows with the **smaller** of the two antenna counts: 1x2 (**6.29**) is only **6.4 %** above 1x1, because one transmit antenna can carry one stream. And the increase is a straight line in the number of antennas (**11.32, 16.71, 22.15** for 2, 3 and 4 at 20 dB, steps of 5.4), where more SNR gives only a logarithmic gain. That is the economic reason for MIMO.

**Diversity** is about the error rate at a fixed SNR. A single Rayleigh link has deep fades, so its bit error rate falls only as `1 / SNR`; with two independent paths it falls as `1 / SNR^2`, and so on. The slope on a log-log plot is the **diversity order**.

--- bit error rate of uncoded QPSK on Rayleigh channels, total transmit power fixed, SNR per receive antenna
scheme                       0 dB      5 dB      10 dB     15 dB     20 dB     25 dB   slope 10 to 20 dB (decades per decade of SNR)
1x1 (no diversity)        2.11e-01  1.09e-01  4.36e-02  1.51e-02  4.85e-03  1.63e-03   0.95
1x2 receive MRC           1.15e-01  3.31e-02  5.50e-03  6.97e-04  5.87e-05  7.50e-06   1.97
1x4 receive MRC           4.00e-02  3.69e-03  1.30e-04  1.25e-06    <1e-6     <1e-6    nan
2x1 Alamouti              1.87e-01  7.51e-02  1.70e-02  2.42e-03  2.88e-04  3.69e-05   1.77
2x2 Alamouti              9.75e-02  1.82e-02  1.08e-03  2.50e-05    <1e-6     <1e-6    nan
2x2 two streams, ZF       2.77e-01  1.68e-01  7.74e-02  2.92e-02  9.71e-03  3.19e-03   0.90
2x2 two streams, MMSE     2.18e-01  1.23e-01  5.42e-02  1.98e-02  6.78e-03  2.07e-03   0.90
4x4 four streams, ZF      3.34e-01  2.34e-01  1.27e-01  5.32e-02  1.91e-02  6.26e-03   0.82
4x4 four streams, MMSE    2.18e-01  1.24e-01  5.57e-02  2.02e-02  6.81e-03  2.12e-03   0.91

The slope between 10 and 20 dB is **0.95** for 1x1 (order 1), **1.97** for 1x2 receive combining (order 2), **1.77** for 2x1 Alamouti transmit diversity (order 2 in theory; I did not investigate why the measured slope is lower, and the short SNR range may be short of the asymptote) and **0.90** for two streams with a linear receiver (order 1). At 20 dB the bit error rate of 1x1 is **4.85e-03**, of 1x2 **5.87e-05** and of 1x4 below 1e-6: **a second receive antenna is worth about 10 dB at this error rate**: 1x2 reaches 5.50e-03 at 10 dB, where 1x1 needs 20 dB for 4.85e-03. The cost of spatial multiplexing is visible too: **two streams with zero forcing (ZF) carry twice the bits at about twice the bit error rate (9.71e-03 against 4.85e-03 for one antenna)**, because the receiver inverts the channel and amplifies the noise in the weak direction, and the minimum mean square error receiver (MMSE) recovers some of it (6.78e-03).

So **rank has a price**: a second stream is useful only when the channel has a second strong direction, and the strength of that direction is a property of the channel at each moment.

--- the rank of a 2x2 Rayleigh channel: eigenmode SNRs are snr / 2 times the squared singular values
ratio of the two squared singular values (dB): median 9.4, 90th percentile 17.6, 99th percentile 27.9
mean SNR   share of channels where eigenmode 2 reaches 10 dB   20 dB    (eigenmode 1 reaches 20 dB)
  20 dB           67.1 %                                          1.8 %    ( 79.4 %)
  25 dB           88.1 %                                         28.2 %    ( 99.3 %)
  30 dB           96.0 %                                         67.1 %    (100.0 %)
  35 dB           98.8 %                                         88.1 %    (100.0 %)
  40 dB           99.6 %                                         96.0 %    (100.0 %)

A 2x2 Rayleigh channel has two eigenmodes (independent paths), whose power is the squared singular values of **H**, and the median ratio between the strong and the weak is **9.4 dB**, with a 99th percentile of **27.9 dB**. So at a mean SNR of 20 dB the weak eigenmode reaches 10 dB in **67.1 %** of channels and 20 dB in only **1.8 %**, and it takes **30 dB** of mean SNR to reach 20 dB on the weak mode in **67.1 %**. **Two streams therefore need an SNR roughly 10 dB above the one stream threshold**, and a client with 30 dB or more has it while one at the edge does not. Problem E puts a rate on it: **two eigenmode streams beat a single beamformed stream only from 20 dB, and with a linear MMSE receiver from 25 dB.** That is why rate adaptation chooses the **number of streams** as well as the MCS.

---

## 9. Beamforming, sounding and MU-MIMO (the gap in the course)

**Transmit beamforming** weights the signal from each of the N antennas so the copies add in phase at the client. For one single antenna client, with the total transmit power held fixed, the weights are the conjugate of the channel (maximum ratio transmission), and the gain is the number of antennas.

--- transmit beamforming to one single antenna client (maximum ratio transmission): SNR against one antenna at the same total power
AP antennas   mean gain dB   theory 10 log10(N)   10th percentile gain dB   (a single Rayleigh antenna is -9.8 dB at its 10th percentile)
     1           0.02           0.00                -9.78
     2           3.01           3.01                -2.77
     4           6.02           6.02                 2.40
     8           9.03           9.03                 6.68

The mean gain is **3.01, 6.02 and 9.03 dB** for 2, 4 and 8 antennas, exactly `10 log10(N)`. The second column is the part people forget: **beamforming also removes the deep fades.** A single Rayleigh antenna is 9.78 dB below its mean at its 10th percentile, a fade that rate control has to be ready for, while 4 antennas with beamforming are **2.40 dB above** it. A link budget with beamforming therefore gains not only the mean but also the fade margin of [WF 11](11_rf_fundamentals_and_link_budget.md).

The catch is that the AP needs the channel to the client, and a transmitter cannot measure it directly (the client has the receive antennas). In **explicit beamforming** (the 802.11n, 802.11ac and 802.11ax method) the AP sends a **null data packet (NDP)** with training fields, and the client measures the channel and sends it back as a **compressed beamforming report**, a set of angles from a Givens rotation decomposition. This is the **sounding**, and it costs airtime.

--- explicit sounding: size of the compressed beamforming feedback (Givens angles) and the airtime of one sounding round (VHT, 80 MHz, group size 4)
AP antennas  users  streams per client  angles  feedback bytes per client  sounding airtime us
     4          1           1               6              328                    244
     4          4           1               6              328                    824
     4          2           2              10              539                    580
     8          4           2              26             1259                   2088
     8          8           1              14              688                   2568

Read the rows. One user with 4 antennas and one stream costs **328 bytes of feedback and 244 µs**. Four users (1 stream each) cost **824 µs**, 3.4 times as much, since the NDP is shared but each report is separate and polled (**from memory** of the exchange). An 8 antenna AP with 4 clients of 2 streams costs **2,088 µs**, and 8 clients cost **2,568 µs**. The feedback grows with `antennas x subcarriers` (the table uses VHT 80 MHz, group size 4, **from memory** of the rules); 802.11ax and 802.11be use more subcarriers per width, so the report gets larger rather than smaller.

**The weakness of explicit sounding is ageing.** The channel at the time of use is not the channel at the time of sounding, and the correlation falls with the **Doppler frequency** `fd = v f / c` as `rho = J0(2 pi fd dt)`.

--- how fast the channel ages: correlation between the channel at sounding and the channel at use, rho = J0(2 pi fd dt), fd = v f / c
speed            carrier   fd Hz    time for rho = 0.99    0.95     0.9      0.7      0.5
slow walk 0.5 m/s   2.4 GHz     4.1         7.85 ms          17.64    25.10    44.72    59.61
slow walk 0.5 m/s   5.5 GHz     9.2         3.48 ms           7.81    11.12    19.81    26.41
walking 1.4 m/s    2.4 GHz    11.4         2.80 ms           6.30     8.97    15.97    21.29
walking 1.4 m/s    5.5 GHz    25.7         1.24 ms           2.79     3.97     7.08     9.43
running 4 m/s     2.4 GHz    32.5         0.98 ms           2.20     3.14     5.59     7.45
running 4 m/s     5.5 GHz    73.3         0.43 ms           0.98     1.39     2.48     3.30
vehicle 50 km/h   2.4 GHz   112.9         0.28 ms           0.63     0.90     1.61     2.14
vehicle 50 km/h   5.5 GHz   254.8         0.13 ms           0.28     0.40     0.71     0.95

A client walking at 1.4 m/s on 5.5 GHz has `fd = 25.7 Hz`, and the channel is **0.99 correlated for only 1.24 ms, 0.95 for 2.79 ms and 0.9 for 3.97 ms**. A person running (4 m/s) cuts those times to about a third (0.43 ms for 0.99) and a vehicle at 50 km/h is at **0.13 ms** for 0.99. At 2.4 GHz the times are **2.3 times** longer. Therefore the channel measured by one sounding is good for a few milliseconds even for a slow walker (7.85 ms for 0.99 at 0.5 m/s on 2.4 GHz).

**Multi-user MIMO** (MU-MIMO) uses the sounding to send **several streams to several clients at once** with zero forcing: the AP shapes each stream so that it arrives at its own client and as a null at the others. It needs a **precise** channel, since the nulls are only as deep as the channel estimate is accurate. The simulation: a 4 antenna AP serving 4 single antenna clients, against serving one client at a time, when the AP's copy of the channel is stale.

--- 4 antenna AP, 4 single antenna clients: MU zero forcing sum rate against serving one client at a time, bit/s/Hz, with a stale channel copy
rho      SNR 10 dB: MU    one at a time     SNR 20 dB: MU    one at a time     SNR 30 dB: MU    one at a time
1.00         6.02             5.18             16.11             8.46             28.69            11.78
0.99         5.63             5.16             12.05             8.43             15.21            11.75
0.95         4.51             5.04              7.63             8.31              8.51            11.63
0.90         3.73             4.90              5.67             8.16              6.06            11.47
0.80         2.87             4.59              3.93             7.83              4.15            11.15
0.70         2.40             4.28              3.11             7.48              3.26            10.79
0.50         1.86             3.68              2.32             6.81              2.38            10.09

At a perfect copy (`rho = 1.00`) and an SNR of 20 dB MU-MIMO gives **16.11 bit/s/Hz against 8.46**, **1.9 times** the one at a time rate, and at 30 dB **28.69 against 11.78** (2.4 times). But at `rho = 0.99` the 20 dB rate drops to **12.05**, at 0.95 to **7.63**, which is **below** the one at a time rate (8.31), and at 0.7 to 3.11. **A correlation of 0.95 is enough to make MU-MIMO lose to doing nothing clever**, because the nulls fill in and every other client's stream becomes interference. The one at a time rate hardly moves (8.46 to 8.31) because beamforming to a single client tolerates a stale copy much better than nulling does. At 10 dB, where the gain was only 1.16 times (6.02 against 5.18) at best, MU-MIMO loses already at 0.95.

The last table adds the cost of sounding to the ageing, and gives the answer to "should I turn on MU-MIMO?" as a calculation.

--- net gain: MU sum rate at SNR 20 dB after the airtime of sounding 4 clients (one stream each), the sounding repeated every interval;
    the channel is taken as half an interval old on average. The sounding round takes 824 us. Fresh MU rate 16.04; one at a time 8.46 (its own sounding is not charged, which favours it)
walking at 1.4 m/s on 5.5 GHz, fd 25.7 Hz
interval ms   rho (half age)   MU rate with ageing   sounding share   net MU rate   one at a time   MU better?
      2        0.993              13.07                41.2 %           7.68         8.44         no
      5        0.960               8.29                16.5 %           6.92         8.34         no
     10        0.844               4.51                 8.2 %           4.14         7.97         no
     20        0.447               2.18                 4.1 %           2.09         6.64         no
still client in a room with moving people, fd 2 Hz (assumed)
interval ms   rho (half age)   MU rate with ageing   sounding share   net MU rate   one at a time   MU better?
     10        0.999              15.38                 8.2 %          14.12         8.46         yes
     50        0.975               9.79                 1.6 %           9.63         8.39         yes
    100        0.904               5.80                 0.8 %           5.75         8.17         no
    250        0.472               2.24                 0.3 %           2.23         6.71         no

For a **walking client at 5.5 GHz MU-MIMO never wins**: at a sounding interval of 2 ms the sounding takes **41.2 %** of the airtime and the net rate is 7.68 against 8.44; at 5 ms the channel has aged to 0.96 and the net is 6.92; at 10 ms it is 4.14. For a **still client in a room with moving people** (a Doppler of 2 Hz, assumed) it wins at **10 ms (14.12 against 8.46)** and at **50 ms (9.63 against 8.39)**, and loses at 100 ms (5.75 against 8.17). The shape of the answer is the point: **MU-MIMO is a gain for static clients (televisions, desktops, set-top boxes, laptops on desks) and a loss for mobile ones**, and an implementation has to decide per client group and per interval, which is why vendors tune it and why some disable it by default. The model favours MU-MIMO by not charging the one at a time rate for its own sounding, and it takes the channel as half an interval old on average, so read it as the shape and not the break-even point.

## 10. OFDMA: sharing a symbol among stations

Before 802.11ax **one station used the whole channel at a time**. A VoIP packet of 100 bytes in a 20 MHz channel used the whole 20 MHz for a few tens of microseconds, with a preamble, a SIFS and an ACK around it. **OFDMA** divides the channel into **resource units (RU)** of contiguous subcarriers and assigns different RUs to different stations in the **same** PPDU.

--- HE resource units: tones, data tones, pilots, and the rate of one RU at three MCS (one stream, GI 0.8 us)
RU      tones  data  pilots   per 20 MHz  40 MHz  80 MHz  160 MHz   MCS 0 Mbit/s   MCS 5   MCS 11
RU26      26    24     2        9         18      37      74         0.88        7.06    14.71
RU52      52    48     4        4          8      16      32         1.76       14.12    29.41
RU106    106   102     4        2          4       8      16         3.75       30.00    62.50
RU242    242   234     8        1          2       4       8         8.60       68.82   143.38
RU484    484   468    16        0          1       2       4        17.21      137.65   286.76
RU996    996   980    16        0          0       1       2        36.03      288.24   600.49
(counts per width are from memory of the HE tone plan; 2 x RU996 uses a whole 160 MHz channel)
9 RU26 in 20 MHz carry 9 x 24 = 216 data tones against 234 for one RU242: OFDMA costs 7.7 % of the tones in pilots and guards

The units run from **26 tones (24 data and 2 pilots)** up to 996 (a whole 80 MHz channel); a 20 MHz channel holds **nine RU26, four RU52, two RU106 or one RU242**. The rate of one RU26 at MCS 11 is **14.71 Mbit/s**, which is what one client of nine in a 20 MHz channel gets at best. OFDMA costs something: **nine RU26 carry 216 data tones against 234 for one RU242, 7.7 % fewer**, because every RU brings its own pilots and the guard tones between units. The airtime model shows when it pays: N stations each send one packet at MCS 5 in 20 MHz, **one by one** (each wins the medium and gets an ACK) against **one trigger frame, one uplink OFDMA PPDU and one block ack**.

--- N stations each send one packet in 20 MHz at MCS 5: one by one (each wins the medium, gets an ACK) against one trigger frame, one uplink OFDMA PPDU and one block ack
payload bytes  stations  RU used     one by one us   OFDMA us   OFDMA saves
     100          1    RU242             216          276      -27.8 %
     100          2    RU106             432          294       32.0 %
     100          4    RU52              864          338       60.8 %
     100          9    RU26             1943          440       77.4 %
     500          1    RU242             257          317      -23.4 %
     500          2    RU106             513          402       21.6 %
     500          4    RU52             1027          570       44.5 %
     500          9    RU26             2310          889       61.5 %
    1500          1    RU242             379          439      -15.8 %
    1500          2    RU106             758          674       11.1 %
    1500          4    RU52             1516         1141       24.8 %
    1500          9    RU26             3412         2031       40.5 %

not in this model:
  - a narrow RU puts the transmit power on fewer tones (+9.7 dB per tone for RU26 against RU242), so a station can use a higher MCS
  - collisions between contending stations, which grow with the number of stations and which the trigger removes
  - the real trigger frame size, about 5 bytes of user info per station (included roughly above)

With **one station OFDMA loses**: **-27.8 %** for a 100 byte packet (276 µs against 216), because it pays the trigger frame and a block ack. It wins from two: **32.0 %** at 2 stations, **60.8 %** at 4 and **77.4 % at 9** (440 µs against 1,943). With bigger packets the saving shrinks, **61.5 % at 500 bytes and 40.5 % at 1500 bytes** for 9 stations, since the payload then dominates the overhead that OFDMA removes. **OFDMA is a technique for many small packets from many stations**: IoT sensors, VoIP, TCP acknowledgements, game traffic, video call uplink. It is not a way of making one download faster. A single large file transfer gets no benefit, and it would see the 7.7 % tone loss if the AP chose small RUs for it.

The model leaves out three things that favour OFDMA, as the script lists: a station that uses a narrow RU puts all its power on fewer tones (**+9.7 dB per tone for an RU26 against an RU242**, `10 log10(242 / 26)`), which lets a weak client use a much higher MCS and is the **range** benefit of uplink OFDMA, the **collisions** of contending stations that a trigger removes and the real size of the trigger frame. It also leaves out what works against it: the scheduler in the AP must know every station's queue, which it does by buffer status reports, and **an uplink trigger needs all stations to transmit at the same moment with tight timing and frequency accuracy** (**from memory** of the requirement).

## 11. Regulatory power and DFS (the gap in the course)

**Regulatory power.** The limit is on the **effective isotropic radiated power (EIRP)**, which is the transmit power plus the antenna gain, and so a 30 dBm radio into a 6 dBi antenna is 36 dBm EIRP. The limits (**from memory**, and different by country and by sub-band) are of the order of **30 dBm (1 W) in 2.4 GHz** with some allowance for gain, **a tier of 5 GHz bands with limits between about 23 and 36 dBm** and, in 6 GHz, **different limits for indoor low power access points, standard power outdoor access points (with a frequency coordination service to protect incumbents) and very low power devices**. Three consequences for a design: a higher gain antenna can **lower** the allowed conducted power in some rules; the power limit is **per device**, not per client, so a client that follows the AP's power in the beacon (transmit power control and the power envelope element, **from memory**) is limited by that too; and a link budget (WF 11) must use the **lowest** of the limits and the amplifier back-off of section 5. The radio's actual power per MCS is lower than the limit by the back-off.

**DFS.** In parts of the 5 GHz band (the DFS columns of section 1), Wi-Fi is a **secondary user** of spectrum that **radar** uses first (weather radar, military radar). An access point must therefore check for radar and leave. The rules (US and Europe, **from memory** and in `s12_dfs.py`): a **channel availability check (CAC)** of **60 s** before any transmission (600 s on a few European weather radar channels), a **channel closing transmission time of 260 ms** after a detection, a **channel move time of 10 s** and a **non-occupancy period of 30 minutes** during which the channel may not be used again.

What this costs, with those timers and the strategy after a detection:

DFS timing (FCC and ETSI, from memory): channel availability check 60 s before use (600 s on 5600 to 5650 MHz in ETSI),
channel closing transmission time 260 ms, channel move time 10 s, non-occupancy period 30 min after a radar detection

downtime per radar event by recovery strategy (seconds): a move to a channel that needs no check costs one channel switch announcement, about 1 s; to a DFS channel not yet checked, the 60 s check
radar events per day   never on DFS           fall back to non-DFS   fall back to unchecked DFS   second radio pre-checks the next channel
       0.1                      0.0 s                   0.1 s                    6.0 s                    0.1 s
       0.5                      0.0 s                   0.5 s                   30.0 s                    0.5 s
       2.0                      0.0 s                   2.0 s                  120.0 s                    2.0 s
       6.0                      0.0 s                   6.0 s                  360.0 s                    6.0 s
      24.0                      0.0 s                  24.0 s                 1440.0 s                   24.0 s

A radar event moves the AP to another channel. **If a non-DFS channel is the fallback, the cost is one channel switch announcement, about 1 s**; a plan with 2 events a day loses **2.0 s** a day. **If the fallback is a DFS channel that has not been checked, the AP must wait 60 s for a CAC**, and 2 events a day cost **120 s**, **60 times** more. An AP with a **second radio that pre-checks the next channel** in the background pays the same as the non-DFS fallback. The same numbers as availability over a day:

availability over a day (86,400 s) for the same strategies, as a share of the day the AP is on air, after one initial check of 60 s at boot
events per day   non-DFS fallback   unchecked DFS fallback   pre-checked
     0.1             99.9304 %             99.9236 %         99.9304 %
     0.5             99.9300 %             99.8958 %         99.9300 %
     2.0             99.9282 %             99.7917 %         99.9282 %
     6.0             99.9236 %             99.5139 %         99.9236 %
    24.0             99.9028 %             98.2639 %         99.9028 %

All of them are above **98 %** and the unchecked fallback loses **1.64 percentage points** at 24 events a day (98.26 % against 99.90 %). Two lessons: in most places **radar events are rare** and DFS channels are very usable, and the cost depends on the **recovery strategy** much more than on the radar rate. A radar event also makes the channel unavailable for 30 minutes, which at 6 events a day on one channel is **3.0 hours a day**, so **a plan that keeps returning to the same radar channel is wrong** (channel selection is in [WF 05](05_wifi_roaming_and_radio_management.md)). Last, the share of DFS channels is the reason the channel plan cannot avoid it.

why a 5 GHz plan cannot do without DFS channels (US plan from s1_channels.py: 25 channels of 20 MHz, 16 of them DFS)
width   channels   needing DFS   share
  20        25          16       64.0 %
  40        12           8       66.7 %
  80         6           4       66.7 %
 160         2           2      100.0 %

**64 % of the 20 MHz channels, 66.7 % of the 40 and 80 MHz blocks and all of the 160 MHz blocks need DFS.** A network that wants 80 MHz channels in a dense deployment has 2 blocks without DFS and 4 with. So the answer to "should I avoid DFS channels?" is a calculation: in a building with a few access points, yes if clients are known to scan poorly (some do not support DFS channels, **from memory**), and in a dense one, no, because avoiding DFS means 2 channels of 80 MHz for the whole site.

## 12. Common mistakes

1. **Quoting the peak rate as throughput.** 9.6 Gbit/s needs 8 streams, 160 MHz and 1024-QAM, which no phone has; and the share of that rate a frame delivers depends on the aggregate (section 7: **9.1 %** with one MPDU, **90.4 %** with 128).
2. **Treating the MCS ladder as a pure SNR ladder.** Each MCS has an SNR requirement, a transmitter back-off and an EVM limit, and the high MCS rates also lose **8.29 dB** of power to the back-off of section 5.
3. **Believing wider is faster.** A 160 MHz channel doubles the rate and the noise (3 dB), so it **loses range**, and **both** 160 MHz blocks of the US plan need DFS.
4. **Using 40 MHz in 2.4 GHz.** The band holds **2** channels of 40 MHz and 3 of 20 MHz.
5. **Confusing the channel number and the centre channel.** An 80 MHz channel with primary 36 is "channel 42" (centre) and an AP reports its primary (**36**).
6. **Forgetting the guard interval costs.** 20 % of every symbol in 802.11a, n and ac, 5.9 % to 20 % in 802.11ax by choice.
7. **Assuming OFDMA speeds up one download.** It helps many small packets from many stations, and with one station it loses 27.8 % (section 10).
8. **Assuming MU-MIMO always helps.** At a channel correlation of 0.95 it loses to one at a time; for a walking client in this model it never won.
9. **Assuming more antennas always means more streams.** Two streams need the second eigenmode to be strong, which in a 2x2 channel is **about 10 dB** more SNR than one stream.
10. **Mixing up diversity and multiplexing.** Diversity lowers the error rate for the same data, multiplexing carries more data at about the same error rate.
11. **Turning on DFS channels and forgetting the recovery.** The cost of a radar event is a 1 s switch or a 60 s wait, and depends on what the AP does next (section 11).
12. **Treating radiotap or `tshark` rate fields as ground truth for HE.** `tshark` 4.2.2 decodes the HE MCS, bandwidth and guard fields but prints no rate (section 6).

## 13. Twelve interview questions

1. **How is the rate of an 802.11 link computed?** Data subcarriers x bits per subcarrier x coding rate x spatial streams, divided by the symbol time (useful plus guard). HE 80 MHz MCS 11, one stream: 980 x 10 x 5/6 / 13.6 µs = **600.5 Mbit/s**.
2. **Why did the peak rate go from 6.9 to 9.6 Gbit/s in 802.11ax and not by 10 times?** 1.25 times the bits per subcarrier and 1.11 times the data tones per µs (the four times denser tones were cancelled by four times longer symbols). The gain of 802.11ax is in **efficiency and many stations** (OFDMA), not in peak rate.
3. **What does the guard interval do, and how long should it be?** An echo inside it only rotates each subcarrier and is exactly undone (EVM below -200 dB in the simulation); a later echo is intersymbol interference (-12 dB at 1.2 µs for a 0.8 µs guard). Longer for outdoor, shorter for an office; it costs 20 % at 0.8 µs of a 4 µs symbol and 5.9 % of an 802.11ax symbol.
4. **Why does 1024-QAM only work near the access point?** It needs about **36 dB** of SNR for a symbol error rate of 1e-3 against 6.79 for BPSK and an amplifier back-off of 10.18 dB for the EVM limit.
5. **What is coding gain?** The SNR a code saves for the same error rate: **4.17 dB** for the rate 1/2 convolutional code at a bit error rate of 1e-3, **2.60 dB** for rate 5/6.
6. **What is the PAPR and why does it limit range?** Peak to average power, **about 10.5 to 12 dB** for OFDM; the amplifier needs back-off, which the EVM of high MCS sets at **10.18 dB** against 1.89 for MCS 0 and 1 (a lower bound), costing **8.29 dB** of power.
7. **What is in a preamble and why is it long?** The legacy part (L-STF, L-LTF, L-SIG, 20 µs) that everyone decodes, then the generation specific signalling and one LTF per stream: **43.2 µs for HE at one stream** and **93.6 µs at 8**. It is 61 % of a 1500 byte frame at HE 80 MHz MCS 11.
8. **How does a legacy station defer to an HE frame?** The L-SIG says 6 Mbit/s and a LENGTH that makes the computed time equal the PPDU time, and with 12 bits it covers at most **5.484 ms**.
9. **What is the difference between diversity, beamforming and spatial multiplexing?** Diversity improves reliability (slope 1.97 with two receive antennas against 0.95), beamforming improves SNR (**10 log10 N**) and removes fades, multiplexing adds streams where the channel has rank (the second eigenmode of a 2x2 reaches 10 dB in 67.1 % of channels at 20 dB).
10. **Does MU-MIMO always help?** No: it needs a fresh channel copy. At 20 dB it gives 16.11 against 8.46 at `rho = 1`, and **loses** at 0.95 (7.63). A client walking at 5.5 GHz is at 0.95 after **2.79 ms**.
11. **When does OFDMA beat OFDM?** With many stations and small packets: **77.4 %** less airtime for 9 stations with 100 bytes, **-27.8 %** for one.
12. **What does DFS cost?** About a **1 s** switch to a non-DFS channel, or a **60 s** check on an unchecked DFS channel, plus **30 minutes** of non-occupancy; and **64 %** of the 20 MHz channels in the US 5 GHz plan are DFS.

---

## 14. Problems

Each problem has a script in `prog/` (`ans_a.py` to `ans_e.py`) whose output is frozen in `v/`. Try them on paper first with the formulas of this tutorial.

**Problem A: channels and frequencies.** Give the centre frequency of 2.4 GHz channels 1, 6, 11, 13 and 14, of 5 GHz channels 36, 100, 149 and 165 and of 6 GHz channels 1, 93 and 233. Give the bonded block and the centre channel of: primary 52 at 80 MHz, primary 100 at 160 MHz, primary 149 at 80 MHz, primary 44 at 40 MHz, primary 36 at 160 MHz and primary 112 at 80 MHz. Which of 2.4 GHz channels (1, 4), (1, 5), (1, 6), (6, 11) and (3, 8) overlap, for OFDM and for 22 MHz DSSS? What do HT40+ and HT40- on channel 6 use?

**Problem B: rates.** Compute the rate of eight configurations: HT MCS 15 at 40 MHz with the short guard, VHT MCS 9 at 80 MHz with 2 streams and the short guard, VHT MCS 7 at 20 MHz with 3 streams, HE MCS 11 at 160 MHz with 2 streams and GI 0.8 µs, HE MCS 7 at 20 MHz with 2 streams and GI 1.6 µs, HE MCS 9 at 80 MHz with 4 streams and GI 3.2 µs, EHT MCS 13 at 320 MHz with 2 streams and GI 0.8 µs, and EHT MCS 12 at 160 MHz with 4 streams and GI 1.6 µs. Give the time to move 1 MB. Which HE settings reach 1 Gbit/s for a 2 stream phone, and what SNR does the cheapest need with the model of [WF 11](11_rf_fundamentals_and_link_budget.md)?

**Problem C: guard interval, Doppler and carrier offset.** For each generation give the guard overhead and the longest path difference the guard absorbs (the speed of light times the guard). Pick a guard interval for four environments (a small room, an open office with echoes to 300 ns, a warehouse with echoes to 800 ns and a stadium with echoes to 2 µs). Give the Doppler shift at 5, 30 and 120 km/h on 2.4 and 5.5 GHz as a share of both subcarrier spacings. Two crystals of 20 ppm can differ by 40 ppm: how many subcarrier spacings is that at 2.437, 5.5 and 6.5 GHz?

**Problem D: aggregation.** For four PHY settings (HE 160 MHz MCS 11, HE 80 MHz MCS 11, HE 20 MHz MCS 7 and HE 20 MHz MCS 0) with one stream and 1500 byte MPDUs, find the smallest aggregate that reaches 50, 70, 80 and 90 % of the PHY rate, and the largest allowed by the 5.484 ms limit. What share of one 1500 byte PPDU at HE 80 MHz MCS 11 is preamble?

**Problem E: rank adaptation.** For a 2x2 Rayleigh channel at HE 20 MHz and GI 0.8 µs, compare one beamformed stream, two streams with a linear MMSE receiver and two eigenmode streams, with rate adaptation per stream and perfect channel knowledge, at mean SNRs from 0 to 40 dB. From which SNR do two streams win?

## 15. Answers

**Answer A.** The scripts compute the channel arithmetic exactly as in section 1.

```python
# ans_a.py: channel numbers, centre frequencies and bonded blocks
def c24(n): return 2484 if n == 14 else 2407 + 5 * n
print('centre frequencies, MHz')
print('2.4 GHz channels 1, 6, 11, 13, 14:  ' + ', '.join('%d: %d' % (n, c24(n)) for n in (1, 6, 11, 13, 14)))
print('5 GHz channels 36, 100, 149, 165:   ' + ', '.join('%d: %d' % (n, 5000 + 5 * n) for n in (36, 100, 149, 165)))
print('6 GHz channels 1, 93, 233:          ' + ', '.join('%d: %d' % (n, 5950 + 5 * n) for n in (1, 93, 233)))
print()
def block(primary, width):
    start = {20: 4, 40: 8, 80: 16, 160: 32}[width]
    # 5 GHz runs of contiguous channels start at 36, 100 and 149; blocks are aligned inside each run
    for lo in (36, 100, 149):
        if lo <= primary < lo + 48 or (lo == 36 and primary <= 64):
            idx = (primary - lo) // start
            chans = [lo + idx * start + 4 * i for i in range(width // 20)]
            return chans, (chans[0] + chans[-1]) / 2
print('bonded channels, 5 GHz')
for p, w in ((52, 80), (100, 160), (149, 80), (44, 40), (36, 160), (112, 80)):
    c, ce = block(p, w)
    print('primary %3d, %3d MHz: channels %s, centre channel %g = %g MHz' % (p, w, c, ce, 5000 + 5 * ce))
print()
print('2.4 GHz: two 20 MHz channels share spectrum when their centres are less than 20 MHz apart (22 MHz for DSSS)')
for a, b in ((1, 4), (1, 5), (1, 6), (6, 11), (3, 8)):
    d = c24(b) - c24(a)
    print('channel %2d and %2d: %2d MHz apart, %s' % (a, b, d, 'overlap' if d < 20 else 'clear for OFDM' + ('' if d >= 25 else ', but not for 22 MHz DSSS')))
print('HT40+ on channel 6 uses channels 6 and 10 (centre %d MHz); HT40- on channel 6 uses 6 and 2 (centre %d MHz)' % ((c24(6) + c24(10)) / 2, (c24(6) + c24(2)) / 2))
print('with 3 usable 20 MHz channels (1, 6, 11) a 40 MHz channel takes two of them; 13 channels hold %d 40 MHz channels' % ((2483 - 2401) // 40))
```

centre frequencies, MHz
2.4 GHz channels 1, 6, 11, 13, 14:  1: 2412, 6: 2437, 11: 2462, 13: 2472, 14: 2484
5 GHz channels 36, 100, 149, 165:   36: 5180, 100: 5500, 149: 5745, 165: 5825
6 GHz channels 1, 93, 233:          1: 5955, 93: 6415, 233: 7115

bonded channels, 5 GHz
primary  52,  80 MHz: channels [52, 56, 60, 64], centre channel 58 = 5290 MHz
primary 100, 160 MHz: channels [100, 104, 108, 112, 116, 120, 124, 128], centre channel 114 = 5570 MHz
primary 149,  80 MHz: channels [149, 153, 157, 161], centre channel 155 = 5775 MHz
primary  44,  40 MHz: channels [44, 48], centre channel 46 = 5230 MHz
primary  36, 160 MHz: channels [36, 40, 44, 48, 52, 56, 60, 64], centre channel 50 = 5250 MHz
primary 112,  80 MHz: channels [100, 104, 108, 112], centre channel 106 = 5530 MHz

2.4 GHz: two 20 MHz channels share spectrum when their centres are less than 20 MHz apart (22 MHz for DSSS)
channel  1 and  4: 15 MHz apart, overlap
channel  1 and  5: 20 MHz apart, clear for OFDM, but not for 22 MHz DSSS
channel  1 and  6: 25 MHz apart, clear for OFDM
channel  6 and 11: 25 MHz apart, clear for OFDM
channel  3 and  8: 25 MHz apart, clear for OFDM
HT40+ on channel 6 uses channels 6 and 10 (centre 2447 MHz); HT40- on channel 6 uses 6 and 2 (centre 2427 MHz)
with 3 usable 20 MHz channels (1, 6, 11) a 40 MHz channel takes two of them; 13 channels hold 2 40 MHz channels

Channel 14 is 2484 MHz, not 2407 + 70 = 2477, and 5 GHz channel 112 with an 80 MHz width belongs to the block of 100 to 112 (centre 106, **5530 MHz**), not to 108 to 120. Channels 1 and 5 are clear for OFDM and not for 22 MHz DSSS. With a 40 MHz channel on channel 6, **HT40+** adds channel 10 (centre 2447 MHz) and **HT40-** adds channel 2 (centre 2427 MHz).

**Answer B.** One formula for every row.

```python
# ans_b.py: PHY rates from the formula, for eight configurations
from phy import *
print('rate = data subcarriers x bits x coding rate x streams / (useful symbol + guard)')
print('configuration                    subcarriers  bits  coding  streams  N_DBPS   symbol us   Mbit/s   1 MB takes ms')
cfg = [('HT  MCS 15  40 MHz  SGI', 'n', 15 % 8, 40, 2, 0.4), ('VHT MCS 9   80 MHz  2 ss SGI', 'ac', 9, 80, 2, 0.4),
       ('VHT MCS 7   20 MHz  3 ss', 'ac', 7, 20, 3, 0.8), ('HE  MCS 11  160 MHz 2 ss GI 0.8', 'ax', 11, 160, 2, 0.8),
       ('HE  MCS 7   20 MHz  2 ss GI 1.6', 'ax', 7, 20, 2, 1.6), ('HE  MCS 9   80 MHz  4 ss GI 3.2', 'ax', 9, 80, 4, 3.2),
       ('EHT MCS 13  320 MHz 2 ss GI 0.8', 'be', 13, 320, 2, 0.8), ('EHT MCS 12  160 MHz 4 ss GI 1.6', 'be', 12, 160, 4, 1.6)]
for name, g, m, w, ss, gi in cfg:
    b, r = MCS[m]
    nd = ndbps(g, m, w, ss); sym = symbol_us(g, gi); rt = nd / sym
    print('%-32s %6d      %3d    %.3f    %3d    %6.0f    %6.1f    %7.1f    %7.2f' % (name, SUBC[g][w], b, r, ss, nd, sym, rt, 8e6 / rt / 1000))
print()
print('which HE settings reach 1 Gbit/s for a 2 stream phone (GI 0.8 us)?')
for w in (20, 40, 80, 160):
    for m in range(11, -1, -1):
        if rate('ax', m, w, 2, 0.8) >= 1000:
            lo = min(k for k in range(12) if rate('ax', k, w, 2, 0.8) >= 1000)
            print('%3d MHz: MCS %d and up (MCS %d gives %.1f)' % (w, lo, lo, rate('ax', lo, w, 2, 0.8)))
            break
    else: print('%3d MHz: none (top is %.1f)' % (w, rate('ax', 11, w, 2, 0.8)))
print('the SNR the cheapest of them needs (80 MHz, MCS 10), from the WF 11 model with its 8 dB gap: %.1f dB; the 160 MHz MCS 5 option needs %.1f dB' % (snr_needed(10), snr_needed(5)))
```

rate = data subcarriers x bits x coding rate x streams / (useful symbol + guard)
configuration                    subcarriers  bits  coding  streams  N_DBPS   symbol us   Mbit/s   1 MB takes ms
HT  MCS 15  40 MHz  SGI             108        6    0.833      2      1080       3.6      300.0      26.67
VHT MCS 9   80 MHz  2 ss SGI        234        8    0.833      2      3120       3.6      866.7       9.23
VHT MCS 7   20 MHz  3 ss             52        6    0.833      3       780       4.0      195.0      41.03
HE  MCS 11  160 MHz 2 ss GI 0.8    1960       10    0.833      2     32667      13.6     2402.0       3.33
HE  MCS 7   20 MHz  2 ss GI 1.6     234        6    0.833      2      2340      14.4      162.5      49.23
HE  MCS 9   80 MHz  4 ss GI 3.2     980        8    0.833      4     26133      16.0     1633.3       4.90
EHT MCS 13  320 MHz 2 ss GI 0.8    3920       12    0.833      2     78400      13.6     5764.7       1.39
EHT MCS 12  160 MHz 4 ss GI 1.6    1960       12    0.750      4     70560      14.4     4900.0       1.63

which HE settings reach 1 Gbit/s for a 2 stream phone (GI 0.8 us)?
 20 MHz: none (top is 286.8)
 40 MHz: none (top is 573.5)
 80 MHz: MCS 10 and up (MCS 10 gives 1080.9)
160 MHz: MCS 5 and up (MCS 5 gives 1152.9)
the SNR the cheapest of them needs (80 MHz, MCS 10), from the WF 11 model with its 8 dB gap: 30.6 dB; the 160 MHz MCS 5 option needs 19.8 dB

Two lessons. First, **the symbol time depends on the generation and the guard, not on the width**: 3.6 µs for a short guard in HT and VHT and 13.6, 14.4 or 16.0 µs for HE and EHT. Second, **a 2 stream phone reaches 1 Gbit/s in HE only at 80 MHz from MCS 10 (1080.9) and at 160 MHz from MCS 5 (1152.9)**, and the cheaper of the two in SNR is the **wider** channel: 30.6 dB against 19.8 dB, because 160 MHz at MCS 5 is 64-QAM and 80 MHz at MCS 10 is 1024-QAM. Its price is 3 dB more noise ([WF 11](11_rf_fundamentals_and_link_budget.md)) and the DFS channels of section 11.

**Answer C.**

```python
# ans_c.py: guard interval, Doppler and carrier frequency offset
from phy import *
print('guard interval overhead: the guard as a share of the whole symbol, and the longest path difference it absorbs (c x GI)')
print('standard   useful us   GI us   symbol us   share lost   path difference m   one stream, MCS 7 (11a: 54 Mbit/s; 11n 40 MHz; 11ac and 11ax 80 MHz)')
for g, gi in (('a', 0.8), ('n', 0.8), ('n', 0.4), ('ac', 0.8), ('ac', 0.4), ('ax', 0.8), ('ax', 1.6), ('ax', 3.2)):
    u = 3.2 if g in ('a', 'n', 'ac') else 12.8
    w = {'a': 20, 'n': 40, 'ac': 80, 'ax': 80}[g]
    print('%-9s  %6.1f     %4.1f     %6.1f     %6.1f %%       %7.0f             %8.1f' % ({'a': '11a', 'n': '11n', 'ac': '11ac', 'ax': '11ax'}[g], u, gi, u + gi, 100 * gi / (u + gi), 3e8 * gi * 1e-6, 54.0 if g == 'a' else rate(g, 7, w, 1, gi)))
print()
print('which guard interval for which delay spread? (rule used here: the guard should exceed the longest echo that matters)')
for name, ds in (('flat, small room', 30), ('open office, rms 50 ns, echoes to 300 ns', 300), ('warehouse, echoes to 800 ns', 800), ('stadium or outdoor, echoes to 2 us', 2000)):
    gi = next((g for g in (0.8, 1.6, 3.2) if g * 1000 >= ds), None)
    print('%-44s echo %5d ns -> GI %s' % (name, ds, '%.1f us' % gi if gi else 'longer than any HE guard: expect ISI'))
print()
print('Doppler shift against the subcarrier spacing')
print('speed           carrier    Doppler Hz   share of the 312.5 kHz spacing   share of the 78.125 kHz spacing')
for v, label in ((1.4, '5 km/h'), (8.3, '30 km/h'), (33.3, '120 km/h')):
    for f in (2.437e9, 5.5e9):
        d = v * f / 3e8
        print('%-14s  %.1f GHz    %7.1f            %8.4f %%                        %8.4f %%' % (label, f / 1e9, d, 100 * d / 312500, 100 * d / 78125))
print()
print('carrier frequency offset: two crystals of +/- 20 ppm (the 802.11 limit, from memory) can differ by 40 ppm')
for f in (2.437e9, 5.5e9, 6.5e9):
    off = 40e-6 * f
    print('%.3f GHz: up to %6.0f kHz = %.2f subcarrier spacings at 312.5 kHz, %.2f at 78.125 kHz' % (f / 1e9, off / 1e3, off / 312500, off / 78125))
print('so the receiver must measure and remove the offset from the preamble before it can demodulate: the STF and LTF exist for this, and HE needs a four times finer estimate')
```

guard interval overhead: the guard as a share of the whole symbol, and the longest path difference it absorbs (c x GI)
standard   useful us   GI us   symbol us   share lost   path difference m   one stream, MCS 7 (11a: 54 Mbit/s; 11n 40 MHz; 11ac and 11ax 80 MHz)
11a           3.2      0.8        4.0       20.0 %           240                 54.0
11n           3.2      0.8        4.0       20.0 %           240                135.0
11n           3.2      0.4        3.6       11.1 %           120                150.0
11ac          3.2      0.8        4.0       20.0 %           240                292.5
11ac          3.2      0.4        3.6       11.1 %           120                325.0
11ax         12.8      0.8       13.6        5.9 %           240                360.3
11ax         12.8      1.6       14.4       11.1 %           480                340.3
11ax         12.8      3.2       16.0       20.0 %           960                306.2

which guard interval for which delay spread? (rule used here: the guard should exceed the longest echo that matters)
flat, small room                             echo    30 ns -> GI 0.8 us
open office, rms 50 ns, echoes to 300 ns     echo   300 ns -> GI 0.8 us
warehouse, echoes to 800 ns                  echo   800 ns -> GI 0.8 us
stadium or outdoor, echoes to 2 us           echo  2000 ns -> GI 3.2 us

Doppler shift against the subcarrier spacing
speed           carrier    Doppler Hz   share of the 312.5 kHz spacing   share of the 78.125 kHz spacing
5 km/h          2.4 GHz       11.4              0.0036 %                          0.0146 %
5 km/h          5.5 GHz       25.7              0.0082 %                          0.0329 %
30 km/h         2.4 GHz       67.4              0.0216 %                          0.0863 %
30 km/h         5.5 GHz      152.2              0.0487 %                          0.1948 %
120 km/h        2.4 GHz      270.5              0.0866 %                          0.3462 %
120 km/h        5.5 GHz      610.5              0.1954 %                          0.7814 %

carrier frequency offset: two crystals of +/- 20 ppm (the 802.11 limit, from memory) can differ by 40 ppm
2.437 GHz: up to     97 kHz = 0.31 subcarrier spacings at 312.5 kHz, 1.25 at 78.125 kHz
5.500 GHz: up to    220 kHz = 0.70 subcarrier spacings at 312.5 kHz, 2.82 at 78.125 kHz
6.500 GHz: up to    260 kHz = 0.83 subcarrier spacings at 312.5 kHz, 3.33 at 78.125 kHz
so the receiver must measure and remove the offset from the preamble before it can demodulate: the STF and LTF exist for this, and HE needs a four times finer estimate

The guard overhead is **20 %** for 11a, 11n and 11ac with the normal guard, **11.1 %** with the short guard and **5.9 %, 11.1 % and 20 %** for HE guards of 0.8, 1.6 and 3.2 µs. The guard absorbs a path difference of **240 m** at 0.8 µs, so the rule "the guard should exceed the longest echo that matters" gives 0.8 µs for all the indoor cases (300 ns and 800 ns are both inside 0.8 µs by this rule, which is the rule I used and not a standard recommendation) and 3.2 µs for the stadium. **The Doppler shift is negligible**: even at 120 km/h on 5.5 GHz it is **0.1954 %** of the 312.5 kHz spacing and 0.78 % of the 78.125 kHz one. The **crystal offset is not**: at 5.5 GHz a 40 ppm difference is **220 kHz, 0.70 of a spacing at 312.5 kHz and 2.82 spacings at 78.125 kHz**. That is why the preamble (STF and LTF) measures the offset before demodulation and why 802.11ax, whose spacing is four times finer, needs a four times finer estimate.

**Answer D.**

```python
# ans_d.py: how much aggregation a rate needs, and the longest aggregate
import math
from phy import *
DIFS, SLOT, SIFS, BA = 34.0, 9.0, 16.0, 32.0
HE_SU = 43.2
def exchange(n, mcs, mhz, ss=1):                          # one HE access: n MPDUs of 1500 bytes, one block acknowledgement
    nb = n * (1500 + 4 + 36)
    t = HE_SU + math.ceil((16 + 8 * nb) / ndbps('ax', mcs, mhz, ss)) * 13.6
    return t, DIFS + 7.5 * SLOT + t + SIFS + BA
print('smallest aggregate (MPDUs of 1500 bytes) that reaches a share of the PHY rate, and the longest allowed (5.484 ms)')
print('setting              PHY Mbit/s   one MPDU goodput   50 %   70 %   80 %   90 %   most MPDUs in 5.484 ms')
for label, mcs, mhz in (('HE 160 MHz MCS 11', 11, 160), ('HE 80 MHz MCS 11 ', 11, 80), ('HE 20 MHz MCS 7  ', 7, 20), ('HE 20 MHz MCS 0  ', 0, 20)):
    phy = rate('ax', mcs, mhz)
    need, nmax = [], 0
    for n in range(1, 2000):
        t, tot = exchange(n, mcs, mhz)
        if t > 5484: break
        nmax = n
    for share in (0.5, 0.7, 0.8, 0.9):
        k = next((n for n in range(1, nmax + 1) if n * 12000 / exchange(n, mcs, mhz)[1] >= share * phy), None)
        need.append('%4s' % (k if k else 'no'))
    t1, tot1 = exchange(1, mcs, mhz)
    print('%-20s %7.1f      %8.1f         %s   %6d' % (label, phy, 12000 / tot1, '   '.join(need), nmax))
print()
print('one 1500 byte MPDU at HE 80 MHz MCS 11: PPDU %.1f us of which the preamble is %.1f us (%.0f %%)' % (exchange(1, 11, 80)[0], HE_SU, 100 * HE_SU / exchange(1, 11, 80)[0]))
print('the same preamble in bytes: at 600.5 Mbit/s, 43.2 us is %d bytes of payload that could have been sent' % (43.2 * 600.5 / 8))
```

smallest aggregate (MPDUs of 1500 bytes) that reaches a share of the PHY rate, and the longest allowed (5.484 ms)
setting              PHY Mbit/s   one MPDU goodput   50 %   70 %   80 %   90 %   most MPDUs in 5.484 ms
HE 160 MHz MCS 11     1201.0          58.2           21     49     90    232      530
HE 80 MHz MCS 11       600.5          54.6           11     25     45    116      265
HE 20 MHz MCS 7         86.0          35.1            2      4      7     18       37
HE 20 MHz MCS 0          8.6           7.3            1      1      1      2        3

one 1500 byte MPDU at HE 80 MHz MCS 11: PPDU 70.4 us of which the preamble is 43.2 us (61 %)
the same preamble in bytes: at 600.5 Mbit/s, 43.2 us is 3242 bytes of payload that could have been sent

**The faster the PHY, the bigger the aggregate it needs**: 50 % of HE 80 MHz MCS 11 needs **11** MPDUs, 80 % needs **45** and 90 % needs **116**, within the **265** that fit in 5.484 ms; at 160 MHz, 90 % needs **232** of **530** allowed. At HE 20 MHz MCS 0 one MPDU is already 85 % efficient. The preamble of one 1500 byte frame at HE 80 MHz MCS 11 is **61 %** of the PPDU, and it is as long as **3,242 bytes** of payload at that rate.

**Answer E.**

```python
# ans_e.py: one stream or two on a 2x2 Rayleigh channel, HE 20 MHz, thresholds from the 8 dB gap model
import numpy as np
from phy import *
rng = np.random.default_rng(11)
cn = lambda *s: (rng.normal(size=s) + 1j * rng.normal(size=s)) / np.sqrt(2)
def rate_of(snr_db):
    m = best_mcs(snr_db)
    return 0.0 if m is None else rate('ax', m, 20)
v = np.vectorize(rate_of)
print('2x2 Rayleigh channel, HE 20 MHz, GI 0.8 us; mean Mbit/s over 20,000 channels (rate adaptation per stream, perfect knowledge)')
print('mean SNR   1 stream (beamformed)   2 streams, MMSE   2 streams, eigenmodes   best choice')
H = cn(20000, 2, 2)
lam = np.linalg.svd(H, compute_uv=False) ** 2
Hh = H.conj().transpose(0, 2, 1)
cross = {}
for snr in (0, 5, 10, 15, 20, 25, 30, 35, 40):
    s = 10 ** (snr / 10)
    one = v(snr + 10 * np.log10(lam[:, 0])).mean()
    inv = np.linalg.inv(np.eye(2) + (s / 2) * Hh @ H)
    sinr = 1 / np.real(np.stack([inv[:, 0, 0], inv[:, 1, 1]], 1)) - 1
    mm = v(10 * np.log10(sinr)).sum(1).mean()
    eig = (v(snr - 3.01 + 10 * np.log10(lam[:, 0])) + v(snr - 3.01 + 10 * np.log10(lam[:, 1]))).mean()
    best = max((one, '1 stream'), (mm, '2 MMSE'), (eig, '2 eigenmodes'))[1]
    if mm > one: cross.setdefault('MMSE', snr)
    if eig > one: cross.setdefault('eigenmodes', snr)
    print('%4d dB      %8.1f              %8.1f            %8.1f            %s' % (snr, one, mm, eig, best))
print()
print()
print('first SNR on the grid where two streams beat one: eigenmodes %d dB, MMSE %d dB; the first stream alone tops out at %.1f Mbit/s (MCS 11, one stream)' % (cross['eigenmodes'], cross['MMSE'], rate('ax', 11, 20)))
```

2x2 Rayleigh channel, HE 20 MHz, GI 0.8 us; mean Mbit/s over 20,000 channels (rate adaptation per stream, perfect knowledge)
mean SNR   1 stream (beamformed)   2 streams, MMSE   2 streams, eigenmodes   best choice
   0 dB           6.2                   0.2                 1.3            1 stream
   5 dB          19.6                   5.3                11.0            1 stream
  10 dB          36.9                  20.0                30.0            1 stream
  15 dB          62.5                  44.3                58.8            1 stream
  20 dB          89.8                  79.6               101.4            2 eigenmodes
  25 dB         118.1                 126.3               149.1            2 eigenmodes
  30 dB         139.6                 178.5               201.8            2 eigenmodes
  35 dB         143.3                 230.1               242.1            2 eigenmodes
  40 dB         143.4                 264.2               266.5            2 eigenmodes


first SNR on the grid where two streams beat one: eigenmodes 20 dB, MMSE 25 dB; the first stream alone tops out at 143.4 Mbit/s (MCS 11, one stream)

Below about 15 dB **one beamformed stream is the best choice**: at 0 dB it gets 6.2 Mbit/s against 1.3 for eigenmodes. Two eigenmode streams win from **20 dB** (101.4 against 89.8) and MMSE from **25 dB** (126.3 against 118.1). One stream **tops out at 143.4 Mbit/s** (MCS 11 on one stream), while two eigenmodes continue to 266.5 at 40 dB, which is **1.86 times** the one stream rate. This is the reason the number of streams is a rate control decision and the median ratio of 9.4 dB between the two eigenmodes (section 8) is the reason the second stream needs a high SNR.

## 16. Cheat sheet

| Fact | Value |
|---|---|
| Channel to MHz | 2.4 GHz: 2407 + 5n; 5 GHz: 5000 + 5n; 6 GHz: 5950 + 5n (2.4 GHz channel 14: 2484) |
| 5 GHz plan (US) | 25 channels of 20 MHz, 9 without DFS; 6 blocks of 80 MHz, 2 without DFS |
| 6 GHz plan | 59, 29, 14, 7, 3 channels at 20, 40, 80, 160, 320 MHz; 15 preferred scanning channels |
| Rate | data subcarriers x bits x coding x streams / (useful + guard) |
| Data subcarriers | 48 (a), 52 / 108 (n), 234 / 468 (ac 80 / 160), 234 to 1,960 (ax 20 to 160), 3,920 (be 320) |
| Symbol | 4.0 µs (a, guard 0.8), 3.6 µs (short guard), 13.6 / 14.4 / 16.0 µs (ax, guard 0.8 / 1.6 / 3.2) |
| Peak rates | 54, 600, 6,933.3, 9,607.8, 46,117.6 Mbit/s for a, n, ac, ax, be |
| SNR per 2 bits | about +6 dB (BPSK 6.79 to 4096-QAM 42.17 dB at SER 1e-3) |
| Coding gain at BER 1e-3 | 4.17 (1/2), 3.62 (2/3), 3.14 (3/4), 2.60 dB (5/6) |
| PAPR (99.9 %) | 10.5 to 12.2 dB; MCS 11 needs 10.18 dB of back-off, 8.29 dB more than MCS 0 and 1 |
| Preamble | 20 µs (a), 36 (n), 40 (ac), 43.2 (ax, 1 stream), 47.2 (be) |
| L-SIG | 24 bits, 12 bit LENGTH, 5.484 ms maximum PPDU for a legacy station |
| Share of PHY rate, HE 80 MHz MCS 11 | 9.1 % with 1 MPDU, 43.3 % with 8, 90.4 % with 128 |
| MIMO | capacity grows with the smaller antenna count; 8x8 is 7.5 times 1x1; two streams need about 10 dB more SNR |
| Diversity slope | 1 (one antenna), 2 (two receive antennas) |
| Beamforming gain | 10 log10 N: 3.01, 6.02, 9.03 dB for 2, 4, 8 antennas |
| Sounding, 4 users | 824 µs |
| Channel age, walking 1.4 m/s, 5.5 GHz | rho 0.99 at 1.24 ms, 0.9 at 3.97 ms |
| MU-MIMO | wins for static clients (14.12 against 8.46 at 10 ms), loses for walking ones |
| OFDMA | 9 RU26 x 24 data tones; 77.4 % saving for 9 stations of 100 bytes; -27.8 % for 1 |
| DFS | CAC 60 s, closing 260 ms, move 10 s, non-occupancy 30 min (from memory); 64 % of 20 MHz channels in the US plan |

## Sources

- Candela Technologies, *Wi-Fi training course*, sessions 2a to 2d (PHY, MIMO and beamforming, OFDMA) and 5a (DFS): <https://www.candelatech.com/course.php>.
- IEEE Std 802.11 (the standard, current revision with the 802.11a, n, ac, ax and be amendments): the subcarrier counts, the MCS tables, the preamble field durations, the L-SIG format, the RU tone plan, the sounding procedure and the EVM limits, all **from memory** and not re-read for this tutorial.
- MathWorks, *WLAN Toolbox* documentation (OFDM numerology, PPDU formats and the EVM requirements), as a reference for the formats.
- ETSI EN 301 893 and FCC Part 15 Subpart E, for the DFS timers and the 5 GHz and 6 GHz power limits (**from memory**).
- Tse and Viswanath, *Fundamentals of Wireless Communication* (capacity of fading channels, diversity order, the Alamouti scheme and MU-MIMO precoding): the theory behind sections 8 and 9.
- A. Goldsmith, *Wireless Communications*, for the OFDM and the Rapp amplifier models.
- Tools used here: Python 3 with `numpy` 2.5.3 and `scipy` 1.18.1, `scapy` 2.5.0 (to build the radiotap frames) and `tshark` 4.2.2 (the independent rate decoder).
