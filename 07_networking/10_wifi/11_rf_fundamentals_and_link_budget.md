# WF 11: RF fundamentals and the link budget

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · A gap in the Candela course, which assumes this and never teaches it · Time: about 6 hours · You need: [WF 01](01_wifi_landscape_and_ap_anatomy.md) (the rate formula and the access point blocks). Read this between WF 01 and WF 02: the PHY tutorial talks about signal to noise ratios, channel widths and MCS thresholds, and each is a number from this one. Every wireless problem has the same shape: **a transmitter makes some power, the path takes most of it away, noise sets a floor, and the ratio of what is left to the floor decides which rate the link can use.** The rest is arithmetic in decibels. This tutorial does the arithmetic, the fading that makes the arithmetic uncertain, the antennas that move it by a few dB, and the planning of channels that keeps neighbouring cells from spoiling each other.*

"A client shows -72 dBm; is that good?" and "why does 5 GHz not reach as far as 2.4 GHz" are the first questions of a wireless loop, and "how do you plan the channels for a building" is the first design question. A Staff level answer is a **calculation**, not an adjective: it converts to decibels, adds the gains and the losses, subtracts the noise floor of the channel width in use, and reads the rate from the SNR. This tutorial gives each step a computed number, and the one simulation in it is checked against its closed form.

**Eight honest limits, stated first.**

1. **There is no radio here, so nothing in this tutorial is a measured signal.** Every number is arithmetic from a stated formula or a simulation. The only independent check I could make is the fading simulation against the closed form of the Rayleigh distribution (section 5.1); the rest are formulas applied to assumed inputs.
2. **The indoor path loss uses assumed exponents (3.0, 3.5, 4.0) and assumed wall losses (5 dB at 2.4 GHz, 8 dB above).** Real buildings differ by tens of dB; the exponent and the walls are inputs, and the answers move with them.
3. **The SNR each MCS needs comes from a model**: the Shannon limit plus an assumed **8 dB gap** for implementation loss (section 4.3). Real receivers differ by a few dB per MCS, and the standard's minimum sensitivities (**from memory**) are looser than any good receiver.
4. **The receiver noise figure of 6 dB is assumed.** Good chips are lower and cheap ones higher.
5. **Regulatory limits, channel counts and power levels are from memory, vary by country and change over time** (the 6 GHz rules most of all). Check the regulator before using any number of that kind.
6. **Antenna gains and patterns are from memory** except where computed from the beamwidth bound of section 6.
7. **Coexistence with Bluetooth, microwave ovens and radar is from memory** (section 8).
8. **RSSI is not a standard unit.** Its scale is vendor specific (**from memory**), so this tutorial works in dBm and SNR.

By the end you can:

- **do decibel arithmetic in your head** (computed: 3 dB is a factor of **1.995**, 10 dB a factor of 10; 20 dBm + 17 dBm is **21.76 dBm**; the noise floor of a 20 MHz channel is **-95.0 dBm** with a 6 dB receiver, and each doubling of the width adds **3.01 dB**, so **-89.0** at 80 MHz and **-82.9** at 320 MHz);
- **compute free space loss and say why 5 GHz is worse** (computed: **40.2, 47.2 and 48.3 dB** at one metre for 2.4, 5 and 6 GHz; **7.1 and 8.1 dB** more than 2.4 GHz at any distance, because a given antenna collects **5.1 times** less area at 5 GHz; doubling the distance costs **6.02 dB** in free space and **10.54 dB** at an exponent of 3.5);
- **build a link budget and read the rate from it** (computed: an AP at 20 dBm with 5 dBi, a client 15 m away through one wall at 5 GHz and 80 MHz gets **-71.4 dBm**, an SNR of **17.6 dB**, MCS 4 and **216.2 Mbit/s**);
- **size a fade margin** (simulated and closed form agree: power falls **10 dB below the mean 9.5 percent of the time** in Rayleigh fading; **20 dB of margin** gives 99 percent availability and **30 dB** gives 99.9 percent, which at an exponent of 3.5 costs **73 and 86 percent** of the range);
- **say what multipath does to a channel** (computed: a reflection **50 ns** late has a notch every **20 MHz**, **-3.3 dB** deep for a reflection 10 dB down and **-10.7 dB** for one 3 dB down);
- **plan channels** (computed: with three channels and an exponent of 3.5 neighbouring cells hold each other to **8.9 dB** of signal to interference, enough for MCS 1; **19 channels** in a cluster reach **22.9 dB**, enough for MCS 7).

## Where the numbers come from

Each experiment is a script in `prog/` with its output frozen in `v/`, run with `run.sh`. `rf.py` is the formulas that every script imports (decibels, free space and log-distance loss, noise floor, the MCS table and the SNR model). `s1_db.py` is the decibel and noise tables; `s2_budget.py` the path loss and link budget tables; `s3_fading.py` the fading simulation and the two path channel; `s4_reuse.py` the channel overlap and the reuse model; `s5_antenna.py` the antenna arithmetic. The answers to the problems are `ans_a` to `ans_e`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, modelled or computed here is marked **from memory**. The fading simulation uses a fixed seed, so each run prints the same figures.

## Data and timing

| What | Size or time |
|---|---|
| Decibel tables (`s1_db.sh`) | under 1 s |
| Path loss and link budget (`s2_budget.sh`) | 2 widths, 3 bands, 9 distances, under 1 s |
| Fading (`s3_fading.sh`) | 2,000,000 simulated paths, about 2 s |
| Channel reuse (`s4_reuse.sh`) | under 1 s |
| Antennas (`s5_antenna.sh`) | under 1 s |

### How this relates to the Candela course

The Candela notes begin at frequency allocation (session 2a) and assume the reader knows what a dB is and why 5 GHz is shorter than 2.4. This tutorial is the missing groundwork, and WF 02 builds the PHY on it.

---


## 1. The chain from power to rate

A Wi-Fi link is a chain of five numbers, and each section of this tutorial is one link of it:

```text
transmit power  -->  antenna gains  -->  path loss  -->  received power  -->  SNR  -->  best MCS  -->  rate
 (dBm)               (dBi)               (dB)             (dBm)               (dB)       (WF 02)
                                                                  ^
                                                                  |
                                         noise floor of the channel width (dBm), set by section 2.4
```

Received power is the sum `Pt + Gt + Gr - path loss` (section 4). **SNR** is that power minus the noise floor. The SNR decides the highest MCS that decodes, and the MCS and the width decide the rate (the formula of WF 01). Two practical facts follow at once:

- Raising the received power by 3 dB is the same as halving the noise: **only the ratio matters**, so a wide channel, which has more noise (section 2.4), needs more received power for the same MCS.
- Everything on the right of the chain is steps, because MCS is discrete: a client at 22.9 dB of SNR and one at 22.8 dB get different rates by a step (section 4.3).

---

## 2. Decibels

### 2.1 Why decibels

Powers in a link span many orders of magnitude (a 200 mW transmitter, a picowatt at the receiver), and every stage multiplies the power by a factor. A **decibel** is ten times the base 10 logarithm of a power ratio, which turns every multiplication into an addition:

```text
dB = 10 × log10(P2 / P1)       a gain of 10 dB is a factor of 10; 3 dB is a factor of 2 (1.995, to be exact)
dBm = 10 × log10(P / 1 mW)     power relative to one milliwatt
dBi = gain relative to an isotropic antenna (one that radiates equally in every direction)
```

The two numbers to know cold are **3 dB = ×2** and **10 dB = ×10**; everything else comes from them (6 dB is ×4, 13 dB is ×20, 17 dB is ×50, 20 dB is ×100, -3 dB is half). Two reading rules: a ratio of powers is in **dB** and an absolute power is in **dBm**, so you can add dB to dBm (a gain to a power) but never two dBm figures (adding powers needs the milliwatts, section 2.3).

### 2.2 A table of dBm

```bash
cd "$(dirname "$0")"; python3 s1_db.py
```

```text
the two numbers worth knowing: 3 dB is a factor of 1.995 in power, 10 dB is a factor of 10

     dBm    milliwatt   where it shows up (descriptions from memory unless marked computed)
      30        1e+03   1 W: the legal ceiling of a high power outdoor radio (from memory)
      23          200   200 mW: a typical access point at the antenna, with 3 dBi, near 26 dBm EIRP
      20          100   100 mW: a common 2.4 GHz limit for the transmitter in Europe (from memory)
      15         31.6   a phone's transmit power
       0            1   1 mW, the reference
     -17         0.02   a client 1 m from a 23 dBm AP at 2.4 GHz (computed: 23 - 40.2 dB of free space loss)
     -67        2e-07   the usual target for video and voice
     -82     6.31e-09   weak, the lowest rate
     -95     3.16e-10   the noise floor of a 20 MHz channel with a 6 dB receiver
    -100        1e-10   below the noise floor nothing decodes

```

A transmitter at **20 dBm** is 100 mW and a typical access point at the antenna is **23 dBm**, 200 mW (**from memory**); a client one metre from such an AP at 2.4 GHz receives about **-17 dBm** (computed: 23 dBm minus 40.2 dB of free space loss, section 3). The weakest signal a client can use at all is near **-82 dBm** and below the noise floor of **-95 dBm** nothing decodes. The whole usable range is therefore about **80 dB**, a factor of 100 million in power, which is why decibels are the only workable unit. The "usual target of -67 dBm" for video and voice is a rule of thumb (**from memory**): it leaves room above the sensitivity for fading (section 5).

### 2.3 Adding and subtracting powers

Gains and losses add in dB, but **two signals add in milliwatts**:

```text
adding powers: two equal signals of 20 dBm make 23.0 dBm; ten of them make 30.0 dBm
a signal 10 dB weaker than another adds 0.41 dB to it; one 20 dB weaker adds 0.04 dB
```

Two equal 20 dBm signals make **23.0 dBm** (twice the power, +3 dB) and ten make **30.0 dBm**. This is why a second transmitter on the same channel looks like a small increase in the noise floor when it is far weaker, and like a large one when it is as strong: **a signal 10 dB below another adds only 0.41 dB to it, and one 20 dB below 0.04 dB**. It is also why a Wi-Fi network with many clients and neighbours has a noise floor higher than the thermal figure (section 7).

### 2.4 The noise floor

Even with no one transmitting, a receiver sees **thermal noise**: **-174 dBm for every hertz of bandwidth** at room temperature, so a channel of width `B` has `-174 + 10 × log10(B)` dBm. The receiver's own circuits add to it by the **noise figure** (NF); this tutorial assumes **6 dB**:

```text
thermal noise: -174 dBm per hertz at room temperature, plus 10 log10(bandwidth), plus the receiver's noise figure (assumed 6 dB)
width MHz  thermal dBm    with NF 6
20              -101.0        -95.0
40               -98.0        -92.0
80               -95.0        -89.0
160              -92.0        -86.0
320              -88.9        -82.9
each doubling of the width adds 3.01 dB of noise
```

For 20 MHz the thermal floor is **-101.0 dBm** and **-95.0 dBm** with the receiver; each doubling of the channel adds **3.01 dB**, because the receiver now listens to twice the bandwidth, so the floor is **-92.0, -89.0, -86.0 and -82.9 dBm** for 40, 80, 160 and 320 MHz. This is the **cost of a wide channel**: a signal that gives 28 dB of SNR in 20 MHz gives 22 dB in 80 MHz and 16 dB in 320 MHz, and the lower SNR supports a lower MCS (section 4.3). A wider channel has more subcarriers, but each carries a smaller share of the power.

---

## 3. Path loss

### 3.1 Free space

In free space the power spreads over a sphere, so it falls with the **square of the distance**, and the area an antenna collects shrinks with the **square of the frequency** (section 6): the loss in dB is

```text
FSPL = 20 × log10(d in km) + 20 × log10(f in MHz) + 32.44
```

Two consequences, the first the core of every range question:

```bash
cd "$(dirname "$0")"; python3 s2_budget.py
```

```text
free space loss at 1 m and at 10 m, and the extra loss of each band over 2.4 GHz
band           1 m      10 m    vs 2.4
2.4 GHz     40.2 dB    60.2 dB     0.0 dB
5 GHz       47.2 dB    67.2 dB     7.1 dB
6 GHz       48.3 dB    68.3 dB     8.1 dB
doubling the distance adds 6.02 dB in free space (exponent 2) and 10.54 dB indoors with exponent 3.5
```

- **Distance:** doubling it adds **6.02 dB** in free space. Ten times the distance adds 20 dB.
- **Frequency:** at the same distance **5 GHz loses 7.1 dB more than 2.4 GHz and 6 GHz loses 8.1 dB more** (`20 × log10(5500 ÷ 2437) = 7.1`). That is not a property of the air: an antenna of fixed gain collects an area proportional to the wavelength squared, **12.04 cm2 at 2.4 GHz and 2.36 cm2 at 5 GHz** for the same 0 dBi, a factor of 5.1, which is 7.1 dB (section 6). A 7 dB deficit in free space is a factor of 2.3 in range (`10^(7.1 ÷ 20)`).

### 3.2 Indoors: the log-distance model

Inside a building the signal does not fall as in free space: walls, floors and furniture absorb, reflect and scatter it. The usual model keeps the one metre free space loss as a reference and lets the exponent `n` carry the building:

```text
PL(d) = FSPL(1 m) + 10 × n × log10(d) + wall losses
```

`n` is 2 in free space and, in an open office or a house, about **3 to 3.5**; a cluttered floor with many partitions is **4 or more** (**from memory**: typical ranges, and the reason to keep `n` an input). With `n = 3.5`, doubling the distance costs **10.54 dB** (printed above), almost twice the free space figure, and ten times the distance costs 35 dB. Walls add fixed losses on top, a few dB for plasterboard and ten dB or more for concrete or a lift shaft (**from memory**), higher at 5 and 6 GHz than at 2.4 (the experiment assumes **5 dB at 2.4 GHz and 8 dB above**). Both inputs are guesses that a **site survey** replaces with measured values (WF 08).

---


## 4. The link budget

### 4.1 The sum

Once the pieces are in decibels the whole link is one addition and one subtraction:

```text
received power (dBm) = Pt + Gt + Gr - path loss - other losses
SNR (dB)             = received power - noise floor
```

`Pt` is the transmit power at the antenna connector, `Gt` and `Gr` the antenna gains, and "other losses" the cables, connectors and body or wall losses that the path loss model did not include. In the model of section 3.2 the path loss already includes the walls.

### 4.2 RSSI is not SNR

Drivers report a number called **RSSI** (received signal strength indication). It is a measure of power, but its **scale is vendor specific**: some chips report dBm, others a unitless index that a driver converts (**from memory**). It also says nothing about the noise, and the rate depends on the ratio. Two clients at the same -72 dBm differ by 6 dB of SNR if one is in a 20 MHz channel and the other in 80 MHz, and by a lot more if one has a neighbour on the same channel. Use **SNR** (and where the driver reports it, the noise floor) to judge a link, and RSSI only to compare two signals measured the same way.

### 4.3 From SNR to rate

The highest rate a channel can carry at a given SNR is bounded by **Shannon's formula**, `C = B × log2(1 + SNR)`, and each MCS of the PHY (WF 02) is a constellation and a code rate with an SNR below which it cannot be decoded. A model for that threshold is the Shannon limit for the bits per symbol of the MCS, plus a fixed **gap** that accounts for implementation loss, the real code and the real modem:

```text
SNR needed = gap + 10 × log10(2^(bits × code rate) - 1)       gap = 8 dB (assumed)
```

```python
GAP_DB = 8.0     # assumed gap to the Shannon limit, covering implementation loss: a model parameter, not a standard value
def snr_needed(mcs):
    b, r = MCS[mcs]
    return GAP_DB + db(2 ** (b * r) - 1)
```

```text
the SNR each MCS needs in this model, next to the receiver sensitivity at 20 MHz it implies
MCS  bits x rate     SNR dB    sensitivity
0    0.50              4.2       -90.8 dBm
1    1.00              8.0       -87.0 dBm
2    1.50             10.6       -84.4 dBm
3    2.00             12.8       -82.2 dBm
4    3.00             16.5       -78.5 dBm
5    4.00             19.8       -75.2 dBm
6    4.50             21.4       -73.6 dBm
7    5.00             22.9       -72.1 dBm
8    6.00             26.0       -69.0 dBm
9    6.67             28.0       -67.0 dBm
10   7.50             30.6       -64.4 dBm
11   8.33             33.1       -61.9 dBm
```

This is **a model with an assumed gap**; it is not the table of any real chip. Its shape matches what is true of every receiver: BPSK 1/2 (MCS 0) decodes near 4 dB of SNR, each step up needs 2 to 3 dB more, and the top, 1024-QAM at 5/6, needs **33.1 dB** here (real transmitters must have an error vector magnitude of about -35 dB for it, which is why the top MCS only appears in a good link, **from memory**). The standard states **minimum receiver sensitivities** for each MCS at 20 MHz, from about **-82 dBm at MCS 0** to about **-52 dBm at MCS 11** (**from memory** of the 802.11ax table; I did not re-read it). Good receivers beat them by several dB: the model's -90.8 dBm for MCS 0 is 9 dB better than the standard's minimum, which is the order of difference datasheets show.

### 4.4 A link budget table

`s2_budget.py` applies the budget to an AP at 23 dBm with a 3 dBi antenna and a client with 0 dBi, at an exponent of 3.5, one wall of 5 dB at 2.4 GHz (8 dB above) from 5 m on, and reads the best MCS and the one stream Wi-Fi 6 rate:

```python
PT, GT, GR, EXP = 23, 3, 0, 3.5     # transmit dBm, AP antenna dBi, client antenna dBi, indoor path loss exponent (assumed)
print("link budget: RSSI = Pt + Gt + Gr - path loss. AP %d dBm, %d dBi; client %d dBi; exponent %.1f; one wall of 5 dB at 2.4 GHz and 8 dB above (assumed)" % (PT, GT, GR, EXP))
print("rate: Wi-Fi 6, one stream, best MCS the SNR allows under an assumed %.0f dB gap to Shannon; noise figure 6 dB" % GAP_DB)
```

```text
link budget: RSSI = Pt + Gt + Gr - path loss. AP 23 dBm, 3 dBi; client 0 dBi; exponent 3.5; one wall of 5 dB at 2.4 GHz and 8 dB above (assumed)
rate: Wi-Fi 6, one stream, best MCS the SNR allows under an assumed 8 dB gap to Shannon; noise figure 6 dB

channel width 20 MHz, noise floor -95.0 dBm
      2.4 GHz                       5 GHz                         6 GHz
m     RSSI dBm  SNR dB  MCS  Mbit/s RSSI dBm  SNR dB  MCS  Mbit/s RSSI dBm  SNR dB  MCS  Mbit/s
1       -14.2   80.8   11   143.4     -21.2   73.7   11   143.4     -22.3   72.7   11   143.4
3       -30.9   64.1   11   143.4     -37.9   57.0   11   143.4     -39.0   56.0   11   143.4
5       -43.6   51.3   11   143.4     -53.7   41.3   11   143.4     -54.7   40.3   11   143.4
10      -54.2   40.8   11   143.4     -64.2   30.7   10   129.0     -65.3   29.7    9   114.7
15      -60.3   34.6   11   143.4     -70.4   24.6    7    86.0     -71.4   23.6    7    86.0
20      -64.7   30.3    9   114.7     -74.8   20.2    5    68.8     -75.8   19.2    4    51.6
30      -70.9   24.1    7    86.0     -80.9   14.0    3    34.4     -82.0   13.0    3    34.4
40      -75.2   19.7    4    51.6     -85.3    9.7    1    17.2     -86.3    8.7    1    17.2
60      -81.4   13.6    3    34.4     -91.5    3.5    -     0.0     -92.5    2.5    -     0.0
```

Read the 20 MHz rows. At 10 m the 2.4 GHz client has -54.2 dBm and **40.8 dB** of SNR (the top MCS and **143.4 Mbit/s**); the 5 GHz client has -64.2 dBm and **30.7 dB** (MCS 10, **129.0**), and the 6 GHz client **29.7 dB** (MCS 9). The drop between 3 and 5 m is the wall. At 20 m 2.4 GHz is at MCS 9 and 5 GHz at MCS 5, **68.8 Mbit/s**: this is the 7 dB of section 3.1 turning into a loss of four MCS steps. At 60 m the 2.4 GHz client still decodes MCS 3 (**34.4 Mbit/s**) and 5 and 6 GHz clients have **no link** (SNR 3.5 and 2.5 dB against 4.2 needed).

### 4.5 Width against range

The same budget at 80 MHz (the noise floor is 6 dB higher):

```text
channel width 80 MHz, noise floor -89.0 dBm
      2.4 GHz                       5 GHz                         6 GHz
m     RSSI dBm  SNR dB  MCS  Mbit/s RSSI dBm  SNR dB  MCS  Mbit/s RSSI dBm  SNR dB  MCS  Mbit/s
1       -14.2   74.8   11   600.5     -21.2   67.7   11   600.5     -22.3   66.7   11   600.5
3       -30.9   58.1   11   600.5     -37.9   51.0   11   600.5     -39.0   50.0   11   600.5
5       -43.6   45.3   11   600.5     -53.7   35.3   11   600.5     -54.7   34.3   11   600.5
10      -54.2   34.8   11   600.5     -64.2   24.7    7   360.3     -65.3   23.7    7   360.3
15      -60.3   28.6    9   480.4     -70.4   18.6    4   216.2     -71.4   17.6    4   216.2
20      -64.7   24.3    7   360.3     -74.8   14.2    3   144.1     -75.8   13.2    3   144.1
30      -70.9   18.1    4   216.2     -80.9    8.0    1    72.1     -82.0    7.0    0    36.0
40      -75.2   13.7    3   144.1     -85.3    3.6    -     0.0     -86.3    2.6    -     0.0
60      -81.4    7.6    0    36.0     -91.5   -2.5    -     0.0     -92.5   -3.5    -     0.0
```

Every row has the same RSSI and an SNR **6.0 dB lower**, so every MCS step is lost sooner. At 30 m on 5 GHz the 20 MHz channel gives MCS 3 and **34.4 Mbit/s**; at 80 MHz the same position gives MCS 1 and **72.1 Mbit/s**, still more because the channel is four times wider. But at 40 m, **the 80 MHz channel has no link (SNR 3.6 dB) while the 20 MHz channel still carries MCS 1 and 17.2 Mbit/s**. That is the wide channel trade: **peak rate near the AP, range and robustness far from it**, which is why an access point in a large building often runs 20 or 40 MHz channels (and why WF 02 and WF 06 discuss channel width as a design decision).

---

## 5. Multipath and fading

### 5.1 Why the received power is not a number

The path loss model gives a mean. In a room the signal does not travel one path: it arrives directly and by reflections from walls, floor, ceiling and people, each with its own delay and phase. At a given point the copies add or subtract. Two paths that arrive in phase double the amplitude and two in antiphase cancel, so the received power **swings by tens of dB over a distance of a few centimetres** (half a wavelength is 6 cm at 2.4 GHz). With many paths of random phase the power follows the **Rayleigh distribution**, and the probability that it falls `m` dB below its mean has a closed form, `1 - exp(-10^(-m/10))`. I simulated two million random phase sums and compared:

```bash
cd "$(dirname "$0")"; python3 s3_fading.py
```

```text
1. many paths with random phases (Rayleigh fading): power relative to its mean, 2000000 samples
below the mean by         simulated  closed form
3 dB                        39.495%      39.419%
6 dB                        22.244%      22.212%
10 dB                        9.545%       9.516%
15 dB                        3.117%       3.113%
20 dB                        0.991%       0.995%
30 dB                        0.095%       0.100%
fade margin for an availability target, closed form: margin = -10 log10(-ln(1 - outage))
   outage 10%     margin   9.8 dB   (simulated: the 10.0 percent quantile is   9.8 dB below the mean)
   outage 1%      margin  20.0 dB   (simulated: the 1.0 percent quantile is  20.0 dB below the mean)
   outage 0.1%    margin  30.0 dB   (simulated: the 0.1 percent quantile is  29.8 dB below the mean)
```

The simulation and the closed form agree to the third decimal (**9.545 against 9.516 percent** at 10 dB, **0.991 against 0.995** at 20 dB). So in a rich multipath room a client sits **10 dB below its mean power 9.5 percent of the time, 20 dB below one percent of the time and 30 dB below 0.1 percent**.

### 5.2 Fade margin

To keep a link up for 99 percent of the time you must budget for the 1 percent tail: **20.0 dB of fade margin** for 99 percent and **30.0 dB** for 99.9 percent (and 9.8 dB for 90 percent). Margin is range spent: at an exponent of 3.5 each 20 dB of margin costs a factor of `10^(20 ÷ 35) = 3.7` in distance, a loss of **73 percent** of the range, and 30 dB loses **86 percent** (problem D). A high availability link therefore does not budget the whole tail, and Wi-Fi does not try: it relies on **rate adaptation** (WF 03) to step down in a fade, and on **diversity** (several antennas, and several channels or APs), whose fades are independent and so rarely coincide (section 6).

### 5.3 Delay spread and frequency selective fading

A reflection arrives **later** than the direct signal, and the delay turns the fade into a pattern across frequency: a direct path plus one echo `τ` later has a **notch every `1 ÷ τ` hertz**, and the depth is set by how strong the echo is. The simulation, with the echo 10 dB and 3 dB below the direct signal:

```text
2. two paths: the direct one and a reflection that arrives later. Depth of the notches across a 20 MHz channel
delay      notch spacing    reflection 10 dB down    reflection 3 dB down
10 ns      100.0 MHz              2.1 dB (range  0.3)         4.2 dB (range  0.4)
25 ns      40.0 MHz               0.4 dB (range  2.0)         1.8 dB (range  2.9)
50 ns      20.0 MHz              -3.3 dB (range  5.7)       -10.7 dB (range 15.3)
100 ns     10.0 MHz              -3.3 dB (range  5.7)       -10.7 dB (range 15.3)
300 ns     3.3 MHz               -3.3 dB (range  5.7)       -10.7 dB (range 15.3)
an indoor reflection 50 ns later has travelled 15 m further (50 ns x 3e8 m/s); a delay much shorter than the symbol is not resolved, a longer one puts a notch in every few subcarriers
```

An echo **50 ns** late, which has travelled **15 m** farther (a reflection off a far wall in a hall), puts a notch every **20 MHz**: **-3.3 dB** deep for an echo 10 dB down and **-10.7 dB** for one 3 dB down. An echo of 10 ns is almost unresolved (the notches are 100 MHz apart and shallow); one of 300 ns notches every 3.3 MHz. This is why Wi-Fi uses **OFDM**: it splits a channel into hundreds of narrow subcarriers (312.5 kHz apart in 802.11a, n and ac, 78.125 kHz in 802.11ax; **from memory**, WF 02), so each notch removes a few of them instead of the whole signal, and the code and the interleaver recover the lost bits. A guard interval at the start of every symbol longer than the delay spread keeps echoes of one symbol from smearing into the next, which is what the choice of 0.8, 1.6 or 3.2 µs in WF 01 and WF 02 is for. A narrow channel can fall completely inside a notch; a wide one averages over several, which is the one respect in which wide channels fade less.

### 5.4 What moves, and how fast

A client that walks at 1 m/s moves a half wavelength in 60 ms at 2.4 GHz, so it passes through a fade every few tens of milliseconds; the channel changes much faster than a rate adaptation algorithm can follow, and a person in the path or a door opening is a slower, deeper change (**from memory**: indoor body shadowing is 5 to 15 dB). A stationary client in a quiet room sees a nearly constant channel, which is why a bench test of a link, with nothing moving, flatters it.

---

## 6. Antennas

### 6.1 Gain is focus

An antenna cannot create power; it **redistributes** it. An isotropic antenna spreads it over a whole sphere and defines 0 dBi; a **directional** antenna concentrates it into a smaller solid angle and gains what it takes from the rest. The gain is bounded by the sphere over the beam:

```bash
cd "$(dirname "$0")"; python3 s5_antenna.py
```

```text
1. gain against beamwidth: a sphere has 41,253 square degrees; gain <= 41253 / (horizontal x vertical beamwidth). Upper bound; real antennas are a dB or two lower
beamwidth (h x v)      gain dBi   x power
90 x 90 degrees        7.1        5.1
60 x 30 degrees        13.6       22.9
30 x 30 degrees        16.6       45.8
10 x 10 degrees        26.2       412.5
```

(`41253 ÷ (h × v)` is the upper bound, with the beamwidths in degrees; real antennas are a dB or two lower.) A beam of 90 by 90 degrees gives at most **7.1 dBi**, 60 by 30 degrees **13.6 dBi** and 10 by 10 degrees **26.2 dBi**, the reason a point to point link uses a dish or a panel while a ceiling access point, which must cover a circle, uses an omnidirectional antenna of about 3 to 6 dBi (**from memory**) that flattens the pattern into a doughnut. That doughnut is also why a client directly below the AP, in its null, may be weaker than one across the room.

### 6.2 The area an antenna collects, and why 5 GHz is worse

The area an antenna collects is `G × λ² ÷ 4π`. For the same gain it shrinks with the **square of the frequency**:

```text
2. the area an antenna of a given gain collects: A = G x wavelength^2 / (4 pi)
band      wavelength     0 dBi antenna      a +6 dBi antenna
2.4 GHz   12.3 cm        12.04 cm2          47.94 cm2
5 GHz     5.5 cm         2.36 cm2           9.41 cm2
6 GHz     4.9 cm         1.88 cm2           7.47 cm2
the 0 dBi antenna collects 5.1 times less area at 5 GHz than at 2.4 GHz: 7.1 dB, the same figure as the extra free space loss
```

A 0 dBi antenna collects **12.04 cm2 at 2.4 GHz and 2.36 cm2 at 5 GHz**, so it hears **5.1 times less power**, 7.1 dB, from the same field. This is the physical reason behind the extra path loss of section 3.1. It also says what to do about it: a higher gain antenna of the same physical size makes up the difference, and at 5 and 6 GHz the antennas of a router are small enough that a few more dB are cheap, so the penalty is smaller in practice than the free space figure suggests (**from memory** for what real products do).

### 6.3 EIRP and combining antennas

What a regulator limits is not the power of the transmitter but the **equivalent isotropically radiated power**, `EIRP = Pt + antenna gain - cable loss`, because what harms other users is the power radiated in the strongest direction (**from memory**). The same EIRP can be reached by a low power transmitter with a high gain antenna or the reverse:

```text
3. EIRP = transmit power + antenna gain - cable loss (the quantity a regulator limits, from memory)
Pt dBm       gain dBi   cable dB   EIRP dBm   EIRP mW
20           3          0.0        23.0       200
23           3          0.5        25.5       355
20           6          1.0        25.0       316
17           14         1.0        30.0       1000
```

A 20 dBm transmitter with a 3 dBi antenna radiates **23 dBm (200 mW)**, one with 17 dBm and 14 dBi reaches **30 dBm (1 W)**, and the regulator treats them alike (the allowed values are by band, country and use and change; check them, **from memory**). The advantage of the second, at the receiver, is only for clients inside its beam.

On the receive side, **combining several antennas** (maximal ratio combining) adds up to `10 × log10(N)` dB when their fades are independent: **3.0 dB for two, 6.0 dB for four, 9.0 dB for eight**, equal to **1.22, 1.49 and 1.81 times** the range at an exponent of 3.5. This is the gain a client with two antennas has over one, and the first benefit of MIMO before any extra spatial stream is used (WF 02). Antennas 3 dB better on the AP would extend the range by the same 1.22 (problem B).

---

## 7. Channel planning

### 7.1 Overlap

Channel numbers on 2.4 GHz are only 5 MHz apart and a channel is 20 MHz wide, so neighbouring numbers overlap:

```bash
cd "$(dirname "$0")"; python3 s4_reuse.py
```

```text
1. channels that overlap on 2.4 GHz: 20 MHz wide channels 5 MHz apart (a model that ignores the skirts of the spectral mask)
channels apart               0      1      2      3      4      5
share of the band in common  100%    75%    50%    25%     0%     0%
channels 1, 6 and 11 are 25 MHz apart: the usual three channel plan (from memory); channels four apart only just touch in this model
```

Channels one apart share **75 percent** of their band, two apart **50**, three **25**, and from four apart (**20 MHz**) they just touch in this model, which ignores the skirts of the spectral mask. Channels **1, 6 and 11** are 25 MHz apart and are the usual three channel plan (**from memory**). Choosing channel 3 for a network next to one on channel 1 is the worst choice, because it overlaps one fully but not exactly: **co-channel** and **adjacent channel** interference are different problems.

- **Co-channel** (same channel): the two networks can hear each other's frames, so **carrier sense (WF 03) makes them take turns**. They lose capacity (they share airtime) but behave politely, and each frame is decoded.
- **Adjacent channel** (overlapping, not equal): the neighbour's signal is **noise**, not a decodable frame, so the receiver cannot defer to it and loses frames. It is usually worse than sharing the channel (**from memory**; the measured case is in WF 08).

### 7.2 Reuse and the number of channels

A building needs many cells on a few channels, so cells must reuse them, and the cells on the same channel interfere with each other. For cells on a hexagonal grid, reusing channels in clusters of `K`, the nearest co-channel cells are at a distance `D = R × sqrt(3K)` for a cell radius `R`, six of them form the first ring, and the signal to interference ratio at the cell edge is about `(D ÷ R)^n ÷ 6` (**from memory**, the classic cellular formula):

```text
2. cells on a hexagonal grid reusing channels in clusters of K: signal to interference with six first-ring interferers
   D/R = sqrt(3 K), SIR = (D/R)^n / 6. n is the path loss exponent (assumed values)
K   D/R    n = 3.0: SIR, MCS          n = 3.5: SIR, MCS          n = 4.0: SIR, MCS
1   1.73    -0.6 dB, none               0.6 dB, none               1.8 dB, none
3   3.00     6.5 dB, MCS 0              8.9 dB, MCS 1             11.3 dB, MCS 2
4   3.46     8.4 dB, MCS 1             11.1 dB, MCS 2             13.8 dB, MCS 3
7   4.58    12.1 dB, MCS 2             15.4 dB, MCS 3             18.7 dB, MCS 4
12  6.00    15.6 dB, MCS 3             19.5 dB, MCS 4             23.3 dB, MCS 7
19  7.55    18.6 dB, MCS 4             22.9 dB, MCS 7             27.3 dB, MCS 8

```

With **three channels** (2.4 GHz) and an exponent of 3.5, neighbouring cells hold each other to **8.9 dB** of SIR, which supports MCS 1 at best whatever the AP can do (in the model's table of section 4.3 MCS 1 needs 8.0 dB). With a cluster of **19 channels** the SIR reaches **22.9 dB**, enough for MCS 7. The same exponent gives the rule for the channel count: for a target MCS, the cluster size needed (problem E) is **K = 7 for MCS 3, 19 for MCS 7 and 39 for MCS 9** at an exponent of 3.5, and much more at 3.0. A real plan does not have 19 free channels, so it **shrinks the cells with lower power**, uses **wider reuse distance** between channels, and accepts a lower MCS at the cell edge: **more channels, not a higher peak rate, is what adds capacity to a dense network**, which is the case for the many 20 MHz channels of 5 and 6 GHz (and why 6 GHz, with up to 59 of them in the United States, **from memory**, is attractive) and the case against fixing every AP at 160 MHz.

### 7.3 Power and channels together

Two knobs, transmit power and channel, set the interference a cell causes. Halving the transmit power of every AP (3 dB) leaves the SNR inside a cell 3 dB lower and the interference from the neighbours 3 dB lower as well, so the **SIR is unchanged** but the cell shrinks: the clients lose range, and the networks lose less to each other because **fewer neighbours hear each other** (and carrier sense sees fewer of them). This is the logic of radio resource management (WF 05), which picks a channel and a power for each AP so that the sum is best, not each AP at its maximum.

---

## 8. Coexistence and non-Wi-Fi interference

The 2.4 GHz band is shared with everything (**from memory** for this section). **Bluetooth** hops over 79 channels of 1 MHz and, with adaptive frequency hopping, avoids the channels that Wi-Fi is using; Bluetooth Low Energy uses 40 channels of 2 MHz. **Zigbee and Thread** use 2 MHz channels in the same band. **Microwave ovens** radiate near 2.45 GHz in short bursts and leak enough to cut a nearby 2.4 GHz link while they run. **Cordless phones, baby monitors and video senders** add to the noise. At 5 GHz the problem is the **radar** that shares the band, which an AP must detect and leave in a fixed time (**dynamic frequency selection**, WF 02 and WF 05), so a radar event is a forced channel change. 6 GHz, newly opened for Wi-Fi in many countries, has incumbent services of its own, which is why it needs rules on power and, outdoors, a coordination database (**automated frequency coordination**, WF 06).

The effect of all of them on a link is the same as in section 2.3: they raise the noise floor. A microwave oven that adds interference 10 dB above the thermal floor removes 10 dB of SNR and about three MCS steps. The tool that sees it is a **spectrum analyser**, not a Wi-Fi radio, because a Wi-Fi radio decodes only Wi-Fi (WF 08).

---

## 9. A worked design

A client 15 m from a ceiling access point, one wall between them, on 5 GHz and 80 MHz (problem B computes the budget): **path loss 96.4 dB**, an RSSI of **-71.4 dBm**, a noise floor of **-89.0 dBm**, an SNR of **17.6 dB**, **MCS 4 and 216.2 Mbit/s** for one stream. The levers, with their numbers:

| Lever | Effect | Cost |
|---|---|---|
| Move the AP closer, removing the wall | +8 dB wall, plus distance | A cable run or a second AP |
| Raise the transmit power by 3 dB | SNR +3 dB, one MCS step or two | Regulator limit; the client's uplink does not improve (the phone transmits at 15 dBm, **from memory**) |
| Drop to 40 MHz | Noise floor -3 dB, so SNR +3 dB, but half the subcarriers | Peak rate halves |
| Use 2.4 GHz | +7.1 dB of free space loss avoided | A crowded band, 20 MHz of width, no 160 MHz |
| Add a second client antenna (diversity) | Up to +3 dB (**from memory** that real phones have two or more) | None for the user |

Each row is a number from this tutorial, and the one the budget alone does not capture is the **uplink**: the AP can transmit at 23 dBm and the phone at 15, so a link can be good downstream and poor upstream. The downlink budget says the AP can be heard; **the uplink budget says whether the AP can hear the client**, and a design must satisfy both (WF 05 and WF 07 return to this when choosing AP power).

---


## 10. Common mistakes

1. **Adding dBm to dBm.** Two 20 dBm signals make **23.0 dBm**, not 40; powers add in milliwatts (section 2.3).
2. **Judging a link by RSSI alone.** The rate follows **SNR**, and the noise floor changes by **3.01 dB** with every doubling of the channel width: the same -72 dBm is 23 dB of SNR at 20 MHz and 17 dB at 80 MHz (section 4.2).
3. **Believing 5 GHz is shorter because of the air.** It is the area an antenna collects: **12.04 cm2 at 2.4 GHz, 2.36 cm2 at 5 GHz**, a loss of **7.1 dB** that a higher gain antenna can win back (sections 3.1 and 6.2).
4. **Using the free space formula indoors.** An exponent of 3.5 costs **10.54 dB** per doubling of distance, against 6.02; and walls add more (section 3.2).
5. **Budgeting only the mean.** A Rayleigh channel is **10 dB below its mean 9.5 percent of the time** and 20 dB below one percent of the time; 99 percent availability needs **20 dB** of margin, which at an exponent of 3.5 costs **73 percent** of the range (section 5.2).
6. **Assuming a wide channel is faster everywhere.** At 40 m on 5 GHz the 20 MHz channel gave **17.2 Mbit/s** while the 80 MHz channel had **no link** (section 4.5).
7. **Planning only the downlink.** An AP at 23 dBm and a phone at about 15 dBm (**from memory**) have an 8 dB difference in the two directions; the link that works must work both ways (section 9).
8. **Choosing channel 3 because 1 is busy.** An overlapping channel is noise the radio cannot defer to; channels 1, 6 and 11 either share airtime politely or are clear (section 7.1).
9. **Maximum power on every AP.** It raises the interference each cell causes as much as it raises the signal, so the SIR does not improve while the cells overlap more; planning picks power and channel together (section 7.3).
10. **Expecting more channels from a wider channel.** Wider channels have fewer channels to reuse, and with three channels at 2.4 GHz neighbouring cells reach only **8.9 dB** of SIR (section 7.2).
11. **Reading a bench test as a field result.** A stationary client in a quiet room does not fade; a person walking through changes it every tens of milliseconds and by 5 to 15 dB (**from memory**, section 5.4).
12. **Trusting a vendor's sensitivity table as the budget.** The standard's minimums are looser than real receivers, and a datasheet number is at a specific width and error rate; the model of section 4.3 is **9 dB** away from the standard at MCS 0.

---

## 11. Twelve interview questions

Each has an outline of a good answer; the section to go back to is in brackets.

1. **What is a dB, and why do we use it?** Ten times the log of a power ratio; it turns the multiplications of a link into additions. 3 dB is ×2, 10 dB is ×10. [2.1]
2. **A client reports -72 dBm. Is that good?** It depends on the noise floor: in 20 MHz (-95 dBm) the SNR is 23 dB (MCS 7 in the model), in 80 MHz (-89 dBm) it is 17 dB. Say the SNR, not the RSSI. [2.4, 4.2]
3. **Why is the noise floor higher on a wide channel?** Thermal noise is -174 dBm per hertz plus 10 log10(bandwidth): +3.01 dB per doubling, **-95.0 dBm at 20 MHz and -82.9 dBm at 320 MHz** with a 6 dB receiver. [2.4]
4. **Why does 5 GHz not reach as far as 2.4 GHz?** The path loss at 1 m is 47.2 against 40.2 dB: 7.1 dB more at any distance, because a fixed antenna collects 5.1 times less area at 5 GHz. Indoors the range ratio is about 0.63 (problem C). [3.1, 6.2]
5. **What is the path loss exponent?** The `n` in `10 × n × log10(d)`: 2 in free space, 3 to 3.5 in a house or an open office, 4 and above in clutter. At 3.5, doubling the distance costs 10.5 dB. [3.2]
6. **Walk through a link budget.** `Pt + Gt + Gr - path loss`, minus the noise floor of the width in use, gives the SNR, which sets the MCS and so the rate. Worked: 20 dBm AP with 5 dBi, 15 m through one wall, 5 GHz, 80 MHz: -71.4 dBm, SNR 17.6 dB, MCS 4, 216.2 Mbit/s. [4, 9]
7. **What is fading and how do you plan for it?** Multipath copies add and cancel; the power is Rayleigh distributed: 9.5 percent of the time 10 dB below the mean. Margin of 20 dB for 99 percent, 30 dB for 99.9 percent, at the price of range; rate adaptation and diversity do the rest. [5]
8. **Why does OFDM work in a room full of echoes?** An echo 50 ns late has a notch every 20 MHz; OFDM puts the bits on hundreds of narrow subcarriers, so a notch loses a few and the code recovers them, and the guard interval absorbs the echo. [5.3]
9. **How does an antenna give gain?** By focusing: gain is at most 41,253 divided by the beamwidth product in degrees, 7.1 dBi for 90 by 90 and 26.2 for 10 by 10; it takes the power away from other directions. [6.1]
10. **What does a regulator limit, and why?** EIRP: transmit power plus antenna gain minus cable loss, because what harms others is the power radiated in the strongest direction. 20 dBm + 3 dBi = 23 dBm; 17 dBm + 14 dBi - 1 dB of cable = 30 dBm. [6.3]
11. **Co-channel against adjacent channel interference?** Co-channel is decodable, so carrier sense shares the airtime; adjacent is noise, which cannot be deferred to and loses frames. A three channel plan is 1, 6, 11. [7.1]
12. **How many channels does a dense network need?** For a target SIR, the reuse cluster `K` with `SIR = (sqrt(3K))^n ÷ 6`: at n = 3.5, K = 3 gives 8.9 dB (MCS 1), K = 19 gives 22.9 dB (MCS 7). More channels and lower power, not a higher peak rate. [7.2, 7.3]

---

## 12. Problems

Do each on paper first, then with the script named. The scripts run from `/home/claude/wf11` (or your copy of `prog/`; `rf.py` is imported by the others).

**Problem A (paper, then `ans_a.py`): decibel arithmetic and the noise floor.** Convert 17, 23, 36, -10 and -73 dBm to milliwatts and 0.5, 25, 200 and 1,000 mW to dBm; add 20 dBm to 17 dBm; compute the noise floor for 20 to 320 MHz at noise figures of 4, 6 and 8 dB; and for five RSSI and width pairs give the SNR and the best MCS. *Approach:* `dBm = 10 log10(mW)`, add powers in milliwatts, noise is `-174 + 10 log10(B) + NF`; then run `python3 prog/ans_a.py`.

**Problem B (paper, then `ans_b.py`): a link budget.** An AP at 20 dBm with a 5 dBi antenna, a 0 dBi client 15 m away, 5.5 GHz, 80 MHz, an exponent of 3.5 and one wall of 8 dB. Find the path loss, the RSSI, the SNR, the best MCS and the rate; then the AP transmit power each of MCS 3, 5, 7, 9 and 11 would need; and what 3 dBi more at the AP buys. *Approach:* the budget of section 4.1; for the power, `Pt = noise + SNR needed + path loss - Gt - Gr`; run `python3 prog/ans_b.py`.

**Problem C (paper, then `ans_c.py`): range by band.** For an AP at 23 dBm with 3 dBi and a 0 dBi client, the distance at which the signal falls to -67, -75 and -82 dBm at 2.4, 5 and 6 GHz, in free space and at an exponent of 3.5, and the ratio of the 5 and 6 GHz ranges to that of 2.4 GHz. *Approach:* solve the model for `d` with the one metre free space loss as the reference; run `python3 prog/ans_c.py`.

**Problem D (paper, then `ans_d.py`): fade margin.** The margin that gives 90, 99, 99.9 and 99.99 percent availability under Rayleigh fading, the share of range each costs at exponents 2 and 3.5, and the SNR each MCS needs to survive a fade of 10, 20 and 30 dB. *Approach:* `margin = -10 log10(-ln(availability))`; range lost is `1 - 10^(-margin ÷ (10 n))`; run `python3 prog/ans_d.py`.

**Problem E (paper, then `ans_e.py`): how many channels.** For target MCS 0, 3, 5, 7, 9 and 11 the smallest hexagonal reuse cluster `K` whose SIR reaches the SNR the MCS needs, at exponents 3.0, 3.5 and 4.0. *Approach:* `SIR = (sqrt(3K))^n ÷ 6` in dB against the model of section 4.3, over `K = i² + ij + j²`; run `python3 prog/ans_e.py`.

---


## 13. Answers

### Problem A: decibel arithmetic and the noise floor

```python
# ans_a.py: decibels and the noise floor.
from rf import *
print("conversions")
for d in (17, 23, 36, -10, -73):
    print("%5d dBm = %12.6g mW" % (d, dbm_to_mw(d)))
for m in (0.5, 25, 200, 1000):
    print("%7.1f mW  = %6.1f dBm" % (m, mw_to_dbm(m)))
print()
print("adding powers: 20 dBm + 17 dBm = %.2f dBm; 3 equal 17 dBm transmitters = %.2f dBm; a 17 dBm and a -10 dBm = %.3f dBm" % (
    mw_to_dbm(dbm_to_mw(20) + dbm_to_mw(17)), mw_to_dbm(3 * dbm_to_mw(17)), mw_to_dbm(dbm_to_mw(17) + dbm_to_mw(-10))))
print()
print("noise floor in dBm: width by receiver noise figure")
print("%-9s %8s %8s %8s" % ("MHz", "NF 4", "NF 6", "NF 8"))
for w in (20, 40, 80, 160, 320):
    print("%-9d %8.1f %8.1f %8.1f" % (w, noise_floor(w, 4), noise_floor(w, 6), noise_floor(w, 8)))
print()
for rssi, w in ((-67, 20), (-67, 80), (-67, 160), (-75, 80), (-82, 20)):
    snr = rssi - noise_floor(w)
    m, r = best_mcs(snr, w)
    print("RSSI %4d dBm at %3d MHz: noise %.1f, SNR %.1f dB, best MCS %s, %.1f Mbit/s (one stream)" % (rssi, w, noise_floor(w), snr, "-" if m is None else m, r))
```

```text
conversions
   17 dBm =      50.1187 mW
   23 dBm =      199.526 mW
   36 dBm =      3981.07 mW
  -10 dBm =          0.1 mW
  -73 dBm =  5.01187e-08 mW
    0.5 mW  =   -3.0 dBm
   25.0 mW  =   14.0 dBm
  200.0 mW  =   23.0 dBm
 1000.0 mW  =   30.0 dBm

adding powers: 20 dBm + 17 dBm = 21.76 dBm; 3 equal 17 dBm transmitters = 21.77 dBm; a 17 dBm and a -10 dBm = 17.009 dBm

noise floor in dBm: width by receiver noise figure
MHz           NF 4     NF 6     NF 8
20           -97.0    -95.0    -93.0
40           -94.0    -92.0    -90.0
80           -91.0    -89.0    -87.0
160          -88.0    -86.0    -84.0
320          -84.9    -82.9    -80.9

RSSI  -67 dBm at  20 MHz: noise -95.0, SNR 28.0 dB, best MCS 8, 103.2 Mbit/s (one stream)
RSSI  -67 dBm at  80 MHz: noise -89.0, SNR 22.0 dB, best MCS 6, 324.3 Mbit/s (one stream)
RSSI  -67 dBm at 160 MHz: noise -86.0, SNR 19.0 dB, best MCS 4, 432.4 Mbit/s (one stream)
RSSI  -75 dBm at  80 MHz: noise -89.0, SNR 14.0 dB, best MCS 3, 144.1 Mbit/s (one stream)
RSSI  -82 dBm at  20 MHz: noise -95.0, SNR 13.0 dB, best MCS 3, 34.4 Mbit/s (one stream)
```

17 dBm is **50.1 mW**, 23 dBm **199.5 mW**, 36 dBm **3,981 mW** (about 4 W) and -10 dBm **0.1 mW**; -73 dBm is **5.0 × 10^-8 mW**, which is why receivers are described in dBm. In the other direction 0.5 mW is **-3.0 dBm**, 25 mW **14.0**, 200 mW **23.0** and 1,000 mW **30.0**. Adding 20 dBm to 17 dBm gives **21.76 dBm**, not 37: 100 mW plus 50.1 mW is 150.1 mW. Three equal transmitters add `10 log10(3)` = 4.77 dB, and a signal 27 dB weaker adds only 0.009 dB. The noise floor table is the rule `-174 + 10 log10(B) + NF`: at 20 MHz it is **-97.0, -95.0 and -93.0 dBm** for noise figures 4, 6 and 8, and every doubling of the width adds **3.01 dB**, to **-82.9 dBm** at 320 MHz with a 6 dB receiver. For the five link examples, **-67 dBm** is 28.0 dB of SNR at 20 MHz (MCS 8), 22.0 dB at 80 MHz (MCS 6) and 19.0 dB at 160 MHz (MCS 4): the same signal gives a wider channel more rate, but only **324.3 Mbit/s** at 80 MHz, not four times the **103.2** of 20 MHz, because the SNR fell by 6 dB.

### Problem B: a link budget

```python
# ans_b.py: one link budget, and the transmit power that a target rate needs.
from rf import *
PT, GT, GR, F, D, N, WALLS, MHZ = 20, 5, 0, 5500, 15, 3.5, 8.0, 80
pl = path_loss(D, F, N, WALLS)
rssi = PT + GT + GR - pl
snr = rssi - noise_floor(MHZ)
m, r = best_mcs(snr, MHZ)
print("AP %d dBm with %d dBi, client %d dBi, %d MHz, %d m, exponent %.1f, one wall of %.0f dB, %d MHz channel" % (PT, GT, GR, F, D, N, WALLS, MHZ))
print("path loss: 1 m reference %.1f + 10 x %.1f x log10(%d) = %.1f + walls %.0f = %.1f dB" % (fspl(1, F), N, D, 10 * N * math.log10(D), WALLS, pl))
print("RSSI %.1f dBm, noise %.1f dBm, SNR %.1f dB, best MCS %s, %.1f Mbit/s" % (rssi, noise_floor(MHZ), snr, m, r))
print()
print("transmit power at the AP (dBm) needed for each target MCS at the same distance")
print("%-4s %8s %8s %12s" % ("MCS", "SNR dB", "need dBm", "Mbit/s"))
for t in (3, 5, 7, 9, 11):
    need = noise_floor(MHZ) + snr_needed(t) + pl - GT - GR
    print("%-4d %8.1f %8.1f %12.1f" % (t, snr_needed(t), need, rate(t, MHZ)))
print()
print("what an extra 3 dBi on the AP antenna buys, in dB and as range at exponent %.1f: %.2fx farther" % (N, 10 ** (3 / (10 * N))))
```

```text
AP 20 dBm with 5 dBi, client 0 dBi, 5500 MHz, 15 m, exponent 3.5, one wall of 8 dB, 80 MHz channel
path loss: 1 m reference 47.2 + 10 x 3.5 x log10(15) = 41.2 + walls 8 = 96.4 dB
RSSI -71.4 dBm, noise -89.0 dBm, SNR 17.6 dB, best MCS 4, 216.2 Mbit/s

transmit power at the AP (dBm) needed for each target MCS at the same distance
MCS    SNR dB need dBm       Mbit/s
3        12.8     15.2        144.1
5        19.8     22.2        288.2
7        22.9     25.4        360.3
9        28.0     30.5        480.4
11       33.1     35.5        600.5

what an extra 3 dBi on the AP antenna buys, in dB and as range at exponent 3.5: 1.22x farther
```

The path loss is the 1 m reference at 5.5 GHz (47.2 dB) plus `10 × 3.5 × log10(15)` = 41.2 dB plus the 8 dB wall: **96.4 dB**. The signal is `20 + 5 + 0 - 96.4` = **-71.4 dBm**; the 80 MHz noise floor is -89.0 dBm; the SNR is **17.6 dB**, which the model turns into **MCS 4 and 216.2 Mbit/s**. For the power each MCS needs at the same distance, `Pt = noise + SNR needed + path loss - Gt - Gr`: **15.2 dBm** for MCS 3 (144.1 Mbit/s), **22.2** for MCS 5, **25.4** for MCS 7, **30.5** for MCS 9 and **35.5 dBm** for MCS 11 (600.5 Mbit/s). The last two are above the 30 dBm EIRP limit in the usual 5 GHz rules (**from memory**), so MCS 11 at this distance cannot be reached by power: it takes a closer client, no wall or a different band. Three more dBi at the AP is 3 dB of signal at the client and, at an exponent of 3.5, **1.22 times** the range (`10^(3 ÷ 35)`). An antenna has the same gain receiving as transmitting, so the uplink gains the 3 dB too, but the client's own transmit power is still the weaker side.

### Problem C: range by band

```python
# ans_c.py: how far does each band reach for the same required signal.
from rf import *
PT, GT, GR = 23, 3, 0
print("distance at which the signal falls to a required RSSI; AP %d dBm, %d dBi, client %d dBi" % (PT, GT, GR))
print("%-9s %-8s %14s %18s" % ("RSSI", "band", "free space m", "indoor n=3.5 m"))
for req in (-67, -75, -82):
    for name, f in (("2.4 GHz", 2437), ("5 GHz", 5500), ("6 GHz", 6175)):
        budget = PT + GT + GR - req                       # loss allowed
        free = 10 ** ((budget - fspl(1, f)) / 20)         # metres, exponent 2 from the 1 m reference
        indoor = 10 ** ((budget - fspl(1, f)) / 35)       # metres, exponent 3.5
        print("%-9d %-8s %14.0f %18.1f" % (req, name, free, indoor))
print()
for name, f in (("5 GHz", 5500), ("6 GHz", 6175)):
    print("range ratio %s against 2.4 GHz: free space %.2f, indoor %.2f (%.1f dB less signal at any distance)" % (name, 2437 / f, (2437 / f) ** (2 / 3.5), fspl(1, f) - fspl(1, 2437)))
```

```text
distance at which the signal falls to a required RSSI; AP 23 dBm, 3 dBi, client 0 dBi
RSSI      band       free space m     indoor n=3.5 m
-67       2.4 GHz             438               32.3
-67       5 GHz               194               20.3
-67       6 GHz               173               19.0
-75       2.4 GHz            1099               54.7
-75       5 GHz               487               34.3
-75       6 GHz               434               32.1
-82       2.4 GHz            2461               86.7
-82       5 GHz              1091               54.4
-82       6 GHz               971               50.9

range ratio 5 GHz against 2.4 GHz: free space 0.44, indoor 0.63 (7.1 dB less signal at any distance)
range ratio 6 GHz against 2.4 GHz: free space 0.39, indoor 0.59 (8.1 dB less signal at any distance)
```

For a -67 dBm target the indoor range (exponent 3.5) is **32.3 m at 2.4 GHz, 20.3 m at 5 GHz and 19.0 m at 6 GHz**; for -75 dBm, **54.7, 34.3 and 32.1 m**; for -82 dBm, **86.7, 54.4 and 50.9 m**. In free space the same targets reach 438, 194 and 173 m, 1,099, 487 and 434 m, and **2,461, 1,091 and 971 m**, so the distances are an order of magnitude longer and show why a model with the free space exponent is useless indoors. The range ratio of 5 GHz to 2.4 GHz is **0.44 in free space and 0.63 indoors**; the 6 GHz ratio is **0.39 and 0.59**. The reason is the one loss of **7.1 dB** (5 GHz) and **8.1 dB** (6 GHz) at any distance, which the exponent turns into a range ratio of `10^(-7.1 ÷ 35)` = 0.63. A designer needs 1.6 times as many 5 GHz APs along a corridor as 2.4 GHz ones for the same signal, and 1.7 times as many at 6 GHz.

### Problem D: fade margin

```python
# ans_d.py: how much margin an availability target costs under fading, and what the margin does to range.
import math
from rf import *
print("fade margin for Rayleigh fading: margin = -10 log10(-ln(1 - outage))")
print("%-14s %-10s %-24s %-24s" % ("availability", "margin", "range lost, n = 2", "range lost, n = 3.5"))
for av in (0.9, 0.99, 0.999, 0.9999):
    m = -10 * math.log10(-math.log(av))
    print("%-14s %7.1f dB %20.0f %% %20.0f %%" % ("%g%%" % (av * 100), m, 100 * (1 - 10 ** (-m / 20)), 100 * (1 - 10 ** (-m / 35))))
print()
print("the SNR a link needs to hold an MCS through a fade of 10, 20 and 30 dB (20 MHz, one stream)")
print("%-6s %-9s %-14s %-14s %-14s" % ("MCS", "no fade", "10 dB fade", "20 dB fade", "30 dB fade"))
for m in (3, 5, 7, 9, 11):
    print("%-6d %7.1f dB %11.1f dB %11.1f dB %11.1f dB" % (m, snr_needed(m), snr_needed(m) + 10, snr_needed(m) + 20, snr_needed(m) + 30))
print()
print("why wide channels fade less: the notches of section 5.3 are %.1f MHz apart at a 50 ns delay, so a 20 MHz channel loses a few subcarriers and a 5 MHz one can lose all of them" % (1 / 50e-9 / 1e6))
```

```text
fade margin for Rayleigh fading: margin = -10 log10(-ln(1 - outage))
availability   margin     range lost, n = 2        range lost, n = 3.5
90%                9.8 dB                   68 %                   47 %
99%               20.0 dB                   90 %                   73 %
99.9%             30.0 dB                   97 %                   86 %
99.99%            40.0 dB                   99 %                   93 %

the SNR a link needs to hold an MCS through a fade of 10, 20 and 30 dB (20 MHz, one stream)
MCS    no fade   10 dB fade     20 dB fade     30 dB fade
3         12.8 dB        22.8 dB        32.8 dB        42.8 dB
5         19.8 dB        29.8 dB        39.8 dB        49.8 dB
7         22.9 dB        32.9 dB        42.9 dB        52.9 dB
9         28.0 dB        38.0 dB        48.0 dB        58.0 dB
11        33.1 dB        43.1 dB        53.1 dB        63.1 dB

why wide channels fade less: the notches of section 5.3 are 20.0 MHz apart at a 50 ns delay, so a 20 MHz channel loses a few subcarriers and a 5 MHz one can lose all of them
```

The margin for an availability `a` is `-10 log10(-ln a)`: **9.8 dB** for 90 percent, **20.0 dB** for 99, **30.0 dB** for 99.9 and **40.0 dB** for 99.99; each extra nine costs 10 dB, because the Rayleigh tail is exponential in power. The exponent turns the margin into range: it takes **68, 90, 97 and 99 percent** of the range in free space but **47, 73, 86 and 93 percent** at an exponent of 3.5. The second table turns it round: holding MCS 7 through a 20 dB fade needs **42.9 dB** of mean SNR, which is more than MCS 11 needs without one (33.1 dB). That is why a link does not hold its top rate through fades; it **adapts** (the rate falls when the channel does) and the margin is kept for the rate it must always keep, usually a low MCS. The last line is the reason that wide channels fade less per subcarrier on average: an echo notch every 20 MHz hits a 5 MHz channel completely and a 20 MHz one in only a few subcarriers.

### Problem E: how many channels

```python
# ans_e.py: the smallest cluster of channels that gives a target signal to interference.
import math
from rf import *
def valid_clusters(limit):                  # hexagonal cluster sizes K = i^2 + i j + j^2
    return sorted({i * i + i * j + j * j for i in range(0, 8) for j in range(0, 8) if 0 < i * i + i * j + j * j <= limit})
print("cluster sizes that tile a hexagonal grid: %s" % valid_clusters(60))
print()
print("smallest K whose SIR reaches the SNR a target MCS needs (20 MHz, one stream); SIR = (sqrt(3K))^n / 6")
print("%-5s %-9s %-12s %-12s %-12s" % ("MCS", "SNR need", "n = 3.0", "n = 3.5", "n = 4.0"))
for mcs in (0, 3, 5, 7, 9, 11):
    row = []
    for n in (3.0, 3.5, 4.0):
        k = next((K for K in valid_clusters(500) if db(math.sqrt(3 * K) ** n / 6) >= snr_needed(mcs)), None)
        row.append("K = %s" % k if k else "K > 500")
    print("%-5d %7.1f dB %-12s %-12s %-12s" % (mcs, snr_needed(mcs), *row))
print()
print("channels available (20 MHz, from memory of the US plans, which vary): 2.4 GHz 3; 5 GHz about 25 (about 9 without the radar channels); 6 GHz 59")
```

```text
cluster sizes that tile a hexagonal grid: [1, 3, 4, 7, 9, 12, 13, 16, 19, 21, 25, 27, 28, 31, 36, 37, 39, 43, 48, 49, 52, 57]

smallest K whose SIR reaches the SNR a target MCS needs (20 MHz, one stream); SIR = (sqrt(3K))^n / 6
MCS   SNR need  n = 3.0      n = 3.5      n = 4.0
0         4.2 dB K = 3        K = 3        K = 3
3        12.8 dB K = 9        K = 7        K = 4
5        19.8 dB K = 25       K = 13       K = 9
7        22.9 dB K = 39       K = 19       K = 12
9        28.0 dB K = 91       K = 39       K = 21
11       33.1 dB K > 500      K = 75       K = 37

channels available (20 MHz, from memory of the US plans, which vary): 2.4 GHz 3; 5 GHz about 25 (about 9 without the radar channels); 6 GHz 59
```

The cluster sizes that tile a hexagonal grid are `i² + ij + j²`: 1, 3, 4, 7, 9, 12, 13, 16, 19, and so on. At an exponent of 3.5, **K = 3 holds MCS 0**, **K = 7 holds MCS 3**, **13 holds MCS 5**, **19 holds MCS 7**, **39 holds MCS 9** and **75 holds MCS 11**; at an exponent of 3.0 the same MCS 7 needs 39, and at 4.0 only 12. A lower exponent (open space) is worse for reuse, because the interferer is heard as well as the friend. The table ends with the supply: **3 channels** at 2.4 GHz, about **25** at 5 GHz of which about 9 are free of radar rules, and **59** at 6 GHz (from memory of the US plans). So a 2.4 GHz network cannot hold more than MCS 0 to 1 between neighbouring cells by channel planning alone, a 5 GHz one at 20 MHz holds MCS 5 to 7 in a good building, and the 6 GHz band is the one where a dense design can aim at the top of the table.

---

## 14. Cheat sheet

**Decibels:** `dB = 10 log10(ratio)`; **3 dB is ×2, 10 dB is ×10, 20 dB is ×100**. dBm is dB against 1 mW: 0 dBm = 1 mW, 20 = 100 mW, 30 = 1 W, -90 = 10^-12 mW. Add powers in milliwatts: 20 dBm + 20 dBm = **23.0 dBm**; 20 + 17 = 21.76.

**Noise floor:** `-174 + 10 log10(B) + NF` dBm. With NF = 6 dB (computed): **-95.0 dBm at 20 MHz, -92.0 at 40, -89.0 at 80, -86.0 at 160, -82.9 at 320**. +3.01 dB per doubling.

**Free space loss at 1 m (computed):** **40.2 dB at 2.4 GHz, 47.2 at 5, 48.3 at 6**; 5 GHz is 7.1 dB and 6 GHz 8.1 dB worse; an isotropic antenna collects **12.04 cm2 at 2.4 GHz, 2.36 at 5** (5.1 times less).

**Path loss:** `PL = PL(1 m) + 10 n log10(d) + walls`. Exponent **n = 2 free space, 3 to 3.5 indoors, 4 clutter**; doubling the distance costs **6.02 dB (n = 2) or 10.54 dB (n = 3.5)**.

**Budget:** `RSSI = Pt + Gt + Gr - PL`; `SNR = RSSI - noise`; SNR picks the MCS. Model thresholds (Shannon with an 8 dB gap): **MCS 0 4.2 dB, MCS 4 16.5, MCS 7 22.9, MCS 9 28.0, MCS 11 33.1 dB**. Worked: 15 m, 5 GHz, 80 MHz, one 8 dB wall, 20 dBm and 5 dBi: **-71.4 dBm, 17.6 dB, MCS 4, 216.2 Mbit/s**.

**Fading (Rayleigh, simulated and closed form):** 10 dB below the mean **9.5 percent** of the time (9.545 simulated, 9.516 closed form). Margin for 90, 99, 99.9 percent: **9.8, 20.0, 30.0 dB**, which cost **47, 73, 86 percent** of range at n = 3.5. A 50 ns echo gives a notch every **20 MHz**, **-3.3 dB** deep for an echo 10 dB down and **-10.7 dB** for one 3 dB down.

**Antennas and power:** gain bound **7.1, 13.6, 26.2 dBi** for beamwidths of 90 by 90, 60 by 30 and 10 by 10 degrees; diversity gain about `10 log10(N)`. EIRP = Pt + G - cable loss: 20 + 3 = **23 dBm**, 17 + 14 - 1 dB cable = **30.0 dBm** (the limit in the usual 5 GHz rules, **from memory**).

**Channel planning:** 2.4 GHz has **3 channels** (1, 6, 11); 5 GHz about 25; 6 GHz 59 (20 MHz, US, **from memory**). Reuse cluster `K`, `SIR = (sqrt(3K))^n ÷ 6`: at n = 3.5 **K = 3 gives 8.9 dB (MCS 1), K = 19 gives 22.9 dB (MCS 7)**; MCS 9 needs K = 39. Overlapping channels (3 between 1 and 6) are noise; co-channel is shared airtime.

**Habits:** quote **SNR not RSSI**; choose the width for the SNR you have, not the peak rate; budget the uplink too; measure at the edge of the cell, standing still and moving.

---

## 15. Sources

Read or run for this tutorial:

- The Candela Technologies course "Wi-Fi Technology Fundamentals" (candelatech.com/course.php): the outline of the RF and link-budget material in sessions 1 to 4 and the "RF fundamentals" gap noted in the networking plan, used to set the coverage of this tutorial.
- Python 3 with `numpy` and `matplotlib` in the sandbox: every table and the Rayleigh simulation (`rf.py`, `s1_db.py` to `s5_antenna.py`, `ans_a.py` to `ans_e.py`), run with fixed seeds, outputs frozen in `v/`.

From memory (not re-read for this tutorial):

- The thermal noise constant of -174 dBm per hertz at room temperature, the Friis free space equation, the log distance path loss model, the Rayleigh distribution, Shannon's capacity formula and the idea of a coding gap.
- The receiver minimum sensitivities of IEEE 802.11ax (about -82 dBm at MCS 0 to -52 dBm at MCS 11, 20 MHz), the error vector magnitude requirement for 1024-QAM, typical phone transmit power and antenna gains, and the US power and channel rules for 2.4, 5 and 6 GHz (FCC Part 15, including the 6 GHz classes and the 30 dBm 5 GHz EIRP figure).
- Cellular-style frequency reuse (hexagonal cluster `i² + ij + j²`) and the Wi-Fi channel plans of the United States.

To go further: the CWNA study guide chapters on RF mathematics, antennas and link budgets (the operator's view), the MathWorks "WLAN (Wi-Fi) Explained" series, and the Ekahau, Cisco and Aruba validated design guides for how site surveys apply the same model with measurements instead of an exponent.

Cross references: WF 01 (the chain from rate formula to AP, the peak rates used here), WF 02 (the PHY: what MCS, coding and guard interval mean), WF 03 (carrier sense and the cost of sharing a channel), WF 06 (rate adaptation, regulatory power and DFS, roaming thresholds), WF 08 (site survey and test tools), NET 14 (queueing, and airtime fairness).
