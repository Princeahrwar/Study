# WF 10: The Linux wireless stack and OpenWrt, from an nl80211 message to a UCI line

*Part of the [networking plan](../00_plan.md), Wi-Fi sub-track · Candela session 7d (OpenWrt basics) and the Linux wireless material beyond it (cfg80211, mac80211, FullMAC and SoftMAC drivers, `hostapd`, `wpa_supplicant`, `iw`, `mac80211_hwsim`) · Prerequisites: [WF 03](03_wifi_mac.md) (frames, aggregation, power save), [WF 04](04_wifi_security.md) (the 4-way handshake), [WF 05](05_wifi_roaming_and_radio_management.md) (roaming, DFS), [WF 08](08_wifi_capture_tools_and_debug.md) (captures, `hostapd` and `wpa_supplicant` logs) and [WF 09](09_wifi_testing.md) (how to compare two builds) · Time: about 9 hours · Practice: five experiments and five problem scripts in `prog/`*

On a Linux access point or client the radio is the smallest part of the software. Between an application and the antenna sit a netlink API, a configuration layer that owns the regulatory rules, a 802.11 MAC implementation (or a firmware that is one), a driver, and two user space daemons that run the parts of the protocol the kernel leaves out. A Wi-Fi platform engineer is asked "where does this happen" all day: where is the association handled, who sends the beacon, who owns the 4-way handshake, why did the channel not change, why does `iw` say "nl80211 not found". This tutorial builds each boundary as something you can check: the generic netlink controller that every tool asks first, the nl80211 messages a daemon sends (built by hand and decoded by `tshark`), the command surface of the tools, a UCI file turned into a `hostapd` configuration and judged by the real `hostapd`, and the scheduler that decides who gets the air.

**Eight honest limits, stated first.**

1. **This VM has no wireless stack at all.** The kernel is built with `CONFIG_WIRELESS` unset, there is no `cfg80211`, no `mac80211`, no `mac80211_hwsim`, no `/sys/class/ieee80211`, and `modprobe mac80211_hwsim` fails with "not found in directory". Nothing of the kernel wireless stack was run. What is real: generic netlink (the kernel's real list of families), the behaviour of `iw` and `wpa_supplicant` on a kernel without nl80211, `hostapd` 2.10 with `driver=none`, and the binaries' own option and command lists.
2. **The kernel internals are from memory.** Function, structure and callback names (`ieee80211_ops`, `wake_tx_queue`, `ieee80211_rx_handlers`, the TX and RX paths, the `cfg80211_ops` table, the regulatory code) are as I remember Linux 6.x. They were not read from source here and are marked **from memory**. Check them against the tree you work on before quoting them.
3. **The nl80211 numbers are checked only where used.** The seven messages of section 5 use 7 commands and 13 attributes. `tshark` printed their names from the numbers, so those numbers are right; every other command and attribute number in the text is from memory.
4. **The UCI to `hostapd` mapping is a model of mine** from memory of OpenWrt's `mac80211.sh` and `hostapd.sh` shell and ubus code, not a copy. The verdicts of `hostapd` are real, but with `driver=none` it checks syntax and a few rules, not what the kernel and the radio would say: it accepted SAE with management frame protection disabled and a wrong VHT centre channel index.
5. **The airtime scheduler is a model** (a saturated downlink, 120 µs per turn of overhead, 5.484 ms PPDU limit, 1500 byte MPDUs). It reproduces the effect, not the Linux implementation, and the numbers are mine.
6. **The driver, chip, firmware and OpenWrt tables are from memory** (which chip uses which driver, which are FullMAC, what an offload engine does, package names, the OpenWrt boot and ubus flow). Vendors change these; check the tree and the release.
7. **No OpenWrt image was built or booted**, and no hardware offload, no `trace-cmd`, no `wmediumd`, no `iw` against a device was run.
8. **Not run here:** `mac80211_hwsim`, any driver, a firmware load, a real regulatory database, the `hostap` test suite. Section 10 gives the commands to run them on a machine whose kernel has `hwsim`, marked as not run.

By the end you can:

- **say what each layer owns** and where a given feature lives (association in the kernel or in `wpa_supplicant`, beacons from `hostapd` or from the driver, keys, regulatory limits, rate control, aggregation, power save buffering), with the FullMAC and SoftMAC split and why it changes what you can debug.
- **ask the kernel what it has** (real: the generic netlink controller lists **13 families** on this kernel and answers `ENOENT` for `nl80211`, so `iw dev` prints "nl80211 not found", `wpa_supplicant -D nl80211` stops with "Unsupported driver", and `/sys/class/ieee80211` does not exist).
- **read and write nl80211 messages** (built and decoded here: **7 of 7** messages decode by `tshark` to the commands and attributes encoded, a `TRIGGER_SCAN` with 3 frequencies and 2 SSIDs is **72 bytes** by arithmetic and in the built message, attributes are padded to 4 bytes and nested ones carry bit 15).
- **navigate the tools** (real: `iw` 6.7 has **129 command forms**, 82 under `dev`, 32 under `phy`, 7 under `wdev`, 3 under `reg`; `hostapd` 2.10 contains **127** of the option names matched in section 6, **52** of them HE options and **none** for EHT; `wpa_supplicant` 2.10 has five driver wrappers).
- **turn a UCI wireless section into a working `hostapd` configuration** and know what the config check cannot see (real: of 6 edits of a generated file `hostapd` rejected **2** (a 5 character passphrase, a misspelt option) and accepted a SAE network with PMF disabled and a VHT80 channel with the wrong centre index).
- **explain the transmit queue and the airtime scheduler** (modelled: four saturated stations at 1201, 600, 150 and 6.5 Mbit/s give **116.8 Mbit/s** total with equal turns and **426.8 Mbit/s** with equal airtime, **3.65 times** more; the slow station takes **59.8 percent** of the air in the first case and **25.0** in the second; the model equals its closed form to 0.1 Mbit/s).
- **work out the per packet CPU budget** of a wireless router (computed: 1200 Mbit/s of 1500 byte packets is **100,000 packets a second**, 10 µs each, **24,000 cycles** per packet in total for two 1.2 GHz cores, **7,200** if the wireless path may use 40 percent of one 1.8 GHz core).
- **debug by layer** with the table of section 13, from `iw event` and `hostapd -dd` down to firmware logs and `debugfs`.

## Where the numbers come from

Each experiment is a script `sN_*.py` or `.sh` in `prog/` with its output frozen in `v/`, run with `run.sh`. `genl.py` is a small generic netlink encoder and decoder; `s1_genl` asks the real kernel for its families, `s2_nl80211` builds the nl80211 messages and has `tshark` decode them, `s3_uci` turns a UCI file into a `hostapd` configuration and runs the real daemon on it, `s4_airtime` is the scheduler model and `s5_iw` reads the real binaries' command and option lists. The five problem scripts `ans_a.py` to `ans_e.py` are in sections 16 and 17.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is not measured, simulated, modelled or computed here is marked **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| Generic netlink and tool behaviour (`s1_genl.sh`) | one dump of the controller, four family lookups, four tool runs, about 4 s |
| nl80211 messages (`s2_nl80211.sh`) | 8 messages, a capture file, one `tshark` decode, about 1 s |
| UCI to `hostapd` (`s3_uci.sh`) | seven `hostapd` runs of 1.2 s each, about 9 s |
| Airtime scheduler (`s4_airtime.sh`) | 20 s of simulated time for each of two schedulers, about 2 s |
| Tool inventory (`s5_iw.sh`) | `iw help`, `wpa_supplicant -h` and a scan of the `hostapd` binary, about 1 s |

## Mapping to the Candela course

Candela session 7d (OpenWrt basics) is section 11. The rest of this tutorial is the gap the course leaves: the Linux wireless architecture and APIs (sections 1 to 7), the data path and offloads (8 and 9), the virtual radio for testing (10), driver bring-up (12) and a debugging playbook (13). From the mrncciew material (CWNA, CWAP and CCIE Wireless study) the relevant part is the AP and controller view of what runs where; the kernel level is from the Linux sources and documentation and is marked from memory where it was not checked.

---


## 1. The layers and who owns what

```
 applications, LuCI, netifd, NetworkManager, management agents
        |   ubus, D-Bus, control sockets (/var/run/hostapd, /var/run/wpa_supplicant)
 hostapd (AP, 802.1X authenticator)       wpa_supplicant (station, SAE, EAP supplicant)
        |   nl80211: generic netlink, family "nl80211", libnl or a built-in encoder
 ==================================  kernel  ==================================
 cfg80211   wiphy and wireless_dev objects, regulatory, BSS scan cache,
            nl80211 command handlers, wext compatibility, connect/disconnect SME
        |   cfg80211_ops (SoftMAC: implemented by mac80211; FullMAC: by the driver)
 mac80211   802.11 MAC in software: MLME for stations, TX and RX paths, aggregation,
            rate control, power save, crypto, mesh, intermediate queues
        |   ieee80211_ops (the driver's callbacks), ieee80211_hw
 driver     ath9k, ath10k, ath11k, ath12k, mt76, iwlwifi, rtw88, ...  (SoftMAC)
            brcmfmac, mwifiex, ...                                      (FullMAC, below cfg80211)
 ==================================  hardware  ================================
 firmware (on the chip or loaded at probe)  ->  MAC and baseband  ->  radio
```

(The picture is from memory of the Linux wireless documentation; names are as of Linux 6.x.)

Who owns which decision, and why you care when a bug is reported:

| Question | Owner | Consequence |
|---|---|---|
| Which channels and power are legal here? | cfg80211 (regulatory), with the regulatory database and the driver's own limits | A channel missing from `iw phy info` is the regulatory code or the driver, not hostapd |
| Who transmits a beacon? | `hostapd` builds the beacon template and hands it to the kernel (`NL80211_CMD_START_AP`); mac80211 or the firmware repeats it every beacon interval | Beacon timing is a driver or firmware property; the content is in `hostapd` |
| Who answers a probe request? | the firmware or driver on many chips (a response from a template), `hostapd` in software on others | A probe response that differs from the beacon is usually this split |
| Who handles authentication and association frames? | AP: `hostapd` (management frames delivered to user space through registered frame handlers). Station: `wpa_supplicant` over nl80211 `AUTHENTICATE` and `ASSOCIATE` on SoftMAC; the driver or firmware on FullMAC (`CONNECT`) | The place to read the MLME log is different for each |
| Who runs the 4-way handshake? | `hostapd` and `wpa_supplicant`, over the control port or as data frames; the kernel only installs the keys | A handshake failure is in the daemon log, a decryption failure is in the kernel or the firmware |
| Who encrypts the data frames? | the hardware or firmware if it has the cipher and the key is installed with `NEW_KEY`, otherwise mac80211 in software | "CCMP works but GCMP-256 drops" is a hardware crypto capability |
| Who picks the rate? | mac80211 rate control (`minstrel_ht`) for a SoftMAC driver, or the firmware | Rate control behaviour can be a driver choice, so two chips differ with the same kernel |
| Who aggregates frames and runs the block ack window? | mac80211 for the session setup (`ADDBA`), the driver or firmware for the aggregation itself on many chips | Throughput bugs are often in the firmware's aggregation, not in the kernel |
| Who buffers frames for a sleeping station? | mac80211 (or the firmware) and `hostapd` sets the TIM through the beacon | See [WF 03](03_wifi_mac.md) for the TIM and DTIM mechanics |
| Who decides when to roam? | `wpa_supplicant` (bgscan, signal thresholds, 11k and 11v, 11r) on Linux clients, the driver or firmware on FullMAC | The roaming policy is not in the kernel |

The kernel provides mechanism and the daemons provide policy. A change in behaviour that does not need a kernel upgrade (a new roaming rule, a new key management type) lives in the daemons, and a change that does (a new cipher in hardware, a new aggregation mode) lives below.

## 2. cfg80211: the configuration layer

### 2.1 The objects

(From memory; check against `include/net/cfg80211.h` of your tree.)

- A **`wiphy`** represents a physical radio device. It carries the supported bands and channels (`ieee80211_supported_band`, each channel with flags and a maximum power), the supported interface types and combinations, the supported commands, the maximum number of scan SSIDs and channels, cipher suites, and feature flags. `iw phy phy0 info` prints it.
- A **`wireless_dev`** (wdev) is a logical interface on a wiphy; it usually has a `net_device` (`wlan0`) but need not (a P2P device and a NAN interface do not).
- **`cfg80211_ops`** is the table of operations the driver or mac80211 implements: `add_virtual_intf`, `change_virtual_intf`, `scan`, `connect`, `auth`, `assoc`, `start_ap`, `change_beacon`, `add_key`, `set_channel`, `get_station`, `set_txpower` and so on. Each nl80211 command ends in one of these calls.
- **BSS entries** from scans are cached in cfg80211 with their elements and a time stamp, which is why `iw dev wlan0 scan dump` returns without scanning and why a stale entry can be returned (the age is printed as "last seen").

### 2.2 The nl80211 interface

nl80211 is the generic netlink family the tools use to talk to cfg80211. A request is a netlink message with a generic netlink header (a command number and a version) followed by attributes (type, length, value, padded to 4 bytes). The kernel answers with an acknowledgement, a dump (a multipart message) or an error code, and sends **events** to multicast groups (`config`, `scan`, `regulatory`, `mlme`, `vendor`, `nan`, `testmode`, from memory) that `iw event` and the daemons subscribe to. Section 5 builds and decodes these messages.

### 2.3 Regulatory

The regulatory code sits in cfg80211 because every driver needs the same rules. The main elements (from memory):

- A **regulatory domain** is a set of frequency ranges, each with a maximum bandwidth, a maximum EIRP and flags: `NO-IR` (no initiating radiation: do not transmit first, so no beacons and no active scan), `DFS` (radar detection is required), `NO-OFDM`, `INDOOR-ONLY`, `AUTO-BW`, and so on. They are the flags `iw phy phy0 info` and `iw reg get` print next to each channel.
- The **database** is `wireless-regdb`, a signed file `regulatory.db` (with the public key compiled in, or the older compiled-in table) loaded by cfg80211 from `/lib/firmware`. The kernel asks user space for the country's rules by name through a firmware request. This VM has no `regulatory.db` and no wireless stack, so no regulatory domain can be shown.
- The **country** comes from the user (`iw reg set DE`), from the country information element in beacons of the neighbours (802.11d, "country IE hint"), from the driver (many cards have it burned in) or from the database default (world, `00`). A driver can be **self managed** (it does its own regulatory in firmware and tells cfg80211, as Intel cards do, from memory), in which case `iw reg set` does not change what the radio does.
- Each rule is then **intersected** with the driver's own limits, with the "last" setting winning for a user hint under the rules of the code. The practical result is that the allowed channel list is the intersection of the database, the hint, the card's capabilities and the antenna, and you read the result, not the database.
- **DFS** (from memory, with the timers of [WF 02](02_wifi_phy.md)): a DFS channel starts as `usable`; after a channel availability check (CAC, 60 s, 600 s on some weather radar channels) it becomes `available`; on radar it becomes `unavailable` for the 30 minute non occupancy period. `hostapd` starts the CAC with `NL80211_CMD_RADAR_DETECT`, and the kernel reports `CAC finished`, `radar detected` and `channel switch` events. A radar detector is in the driver or the firmware, because it needs the raw pulses.

## 3. mac80211: the SoftMAC implementation

mac80211 is a library that drivers for SoftMAC chips use to implement `cfg80211_ops` and 802.11 behaviour. A driver registers an `ieee80211_hw` with an `ieee80211_ops` table (from memory, a selection):

| Callback | What mac80211 asks the driver |
|---|---|
| `start`, `stop` | bring the radio up and down |
| `add_interface`, `remove_interface` | create and remove a virtual interface (`vif`) in hardware |
| `config` and the channel context ops (`add_chanctx`, `assign_vif_chanctx`) | tune to a channel, bandwidth and power |
| `bss_info_changed` / `link_info_changed` | beacon interval, BSSID, association state, ERP and HT protection, basic rates, TXPower, MU and OFDMA parameters |
| `sta_state` (formerly `sta_add`, `sta_remove`) | a station moved between the none, auth, assoc and authorized states; allocate per station state in the firmware |
| `set_key` | install a key in hardware or refuse (and mac80211 encrypts in software) |
| `conf_tx` | EDCA parameters per access category |
| `wake_tx_queue` | there are frames waiting on an intermediate queue; pull them (section 8) |
| `tx` (older) | transmit this one frame |
| `ampdu_action` | start, stop, operate an A-MPDU session |
| `hw_scan`, `sw_scan_start` | scan in firmware, or let mac80211 drive the channel changes |
| `set_rts_threshold`, `set_tim`, `set_bitrate_mask`, `get_survey`, `get_stats`, `flush`, `reconfig_complete` | the rest of the control surface |

What mac80211 itself does, so the driver does not have to:

- **Station state machine**: the none, authenticated, associated and authorized states, with the transitions requested by `wpa_supplicant` or `hostapd` and checks on each frame.
- **Station MLME** (when the user space daemon asks for it): authentication and association frame exchange, the SA query, the channel switch announcement handling, the beacon loss and connection monitor (a keep alive null frame when beacons stop), the managed mode power save timers.
- **Transmit and receive paths** (section 8): 802.3 to 802.11 header conversion, fragmentation, defragmentation, A-MSDU, duplicate detection, the reorder buffer for the block ack, and monitor mode delivery of copies.
- **Rate control**: `minstrel_ht` (a statistics based sampler, from memory the default) and a hook for the driver or firmware to do its own.
- **Power save**: the AP buffers frames for a dozing station and sets its TIM bit; the station side sets the PM bit and polls with PS-Poll or U-APSD triggers.
- **Mesh** (802.11s peering, path selection with HWMP, from memory), **IBSS** (ad hoc, beacon generation by stations), **WDS** and **AP VLAN** interfaces.
- **Crypto in software** when the hardware cannot: CCMP, GCMP, TKIP, WEP, BIP for management frames.
- **Aggregation session management**: the ADDBA request and response exchange; the aggregation of the frames themselves may then be in mac80211 or in the driver.
- **MLO (multi-link operation)** infrastructure since Linux 6.x: `struct ieee80211_bss_conf` per link, `link_id` in the callbacks, MLD addresses (from memory; see WF 12 when written).

### FullMAC against SoftMAC

| | FullMAC (hardware MAC) | SoftMAC (mac80211) |
|---|---|---|
| Where the MLME runs | on the chip: it authenticates, associates, scans and roams by itself | in the kernel, driven by `wpa_supplicant` |
| What the driver implements | `cfg80211_ops` directly (`connect`, `disconnect`, `scan`, `start_ap`) | `ieee80211_ops`; cfg80211_ops comes from mac80211 |
| What you can see from Linux | netlink commands and events; the frames only on a sniffer or in firmware logs | the 802.11 frames in a monitor interface, the state in `iw` and debugfs |
| Typical examples (from memory) | Broadcom `brcmfmac`, Marvell `mwifiex`, Quantenna `qtnfmac`, RSI | `ath9k`, `ath10k`, `ath11k`, `ath12k`, MediaTek `mt76`, Intel `iwlwifi`, Realtek `rtw88` and `rtw89` |
| When it is chosen | phones and small embedded designs where the host is weak and the chip vendor provides the whole MAC | access points and clients where a vendor wants Linux to own the MAC |
| Cost of a bug | a firmware release | a kernel patch |

The two are not a binary choice. Modern SoftMAC chips run a large firmware that does a lot (scans, aggregation, rate selection, power save, parts of the data path, the TX completion), so "SoftMAC" means the host owns the **management** of the MAC (state, keys, scan policy) and the firmware runs the **real time** parts. `iwlwifi` and `ath11k` are examples (from memory): they use mac80211 but an event and command interface with a firmware that is itself a MAC. This matters for debugging: a tcpdump in a monitor interface shows the frames the host saw, the firmware may have sent others, and the real answer is in the firmware log.

## 4. Drivers and firmware

A wireless driver has four jobs: **probe** the bus (PCIe, SDIO, USB, AHB on an SoC) and set up the interrupts and DMA; **load firmware and board data** with `request_firmware` (the files live in `/lib/firmware/<vendor>/`; a board data file or a per board entry in a `board-2.bin` selects the calibration and the regulatory limits for the specific module, from memory); **translate** between the kernel's queues and the hardware's rings (descriptors, DMA mappings, completion interrupts, NAPI polling); and **expose** the capabilities in the `wiphy`.

A short orientation, all from memory and not checked here:

| Driver | Chips and use | Notes |
|---|---|---|
| `ath9k` | Atheros 802.11n (AR9xxx) | Open, all MAC in the host; the reference for how a SoftMAC driver works |
| `ath10k` | Qualcomm 802.11ac (QCA988x, QCA9984, QCA4019 and so on) | Firmware based, WMI command interface, PCIe and AHB |
| `ath11k` | Qualcomm 802.11ax (IPQ807x, QCN9074, WCN6855) | Firmware based, QMI and WMI, many offload paths |
| `ath12k` | Qualcomm 802.11be | Shares much structure with `ath11k`; MLO support |
| `mt76` family | MediaTek MT76xx, MT7915, MT7921, MT7922, MT7996 | A compact SoftMAC driver shared across chips, popular on OpenWrt |
| `iwlwifi` and `iwlmvm`, `iwlmld` | Intel Wi-Fi 6, 6E, 7 clients | Closed firmware, self managed regulatory, client focused |
| `rtw88`, `rtw89` | Realtek | Many consumer parts |
| `brcmfmac` | Broadcom FullMAC | Phones, Raspberry Pi |
| `mwifiex` | Marvell FullMAC | |

The data that tells you which driver and firmware a system has: `ethtool -i wlan0` (driver, firmware version, bus), `dmesg` at load time, `iw phy info` (capabilities), and `/sys/class/net/wlan0/device`.

## 5. nl80211: the API every tool uses

### 5.1 The first step: ask the controller

Every nl80211 program starts by asking the generic netlink controller (family `nlctrl`, id 16) for the id of the family named `nl80211`; the id is assigned when the module loads, so it is not constant. `genl.py` builds and parses the messages, and `s1_genl.py` asks the real kernel of this VM:

```python
def nla(t, data):
    n = struct.pack('HH', 4 + len(data), t) + data
    return n + b'\0' * (-len(n) % 4)
def nla_str(t, s): return nla(t, s.encode() + b'\0')
def nla_u32(t, v): return nla(t, struct.pack('I', v))
def nla_u16(t, v): return nla(t, struct.pack('H', v))
def nla_u8(t, v): return nla(t, struct.pack('B', v))
def genl(family, cmd, attrs=b'', flags=NLM_F_REQUEST, seq=1, version=0):
    body = struct.pack('BBH', cmd, version, 0) + attrs
    return struct.pack('IHHII', 16 + len(body), family, flags, seq, 0) + body
def parse_attrs(b):
    out = {}; i = 0
    while i + 4 <= len(b):
        ln, t = struct.unpack_from('HH', b, i)
```

```
--- real: this kernel config for wireless
  # CONFIG_WIRELESS is not set
--- real: generic netlink families registered in this kernel (id, name)
   16  nlctrl
   17  VFS_DQUOT
   19  thermal
   20  netdev
   21  ethtool
   22  NLBL_MGMT
   23  NLBL_CIPSOv4
   24  NLBL_CALIPSO
   25  NLBL_UNLBL
   26  acpi_event
   27  tcp_metrics
   28  mptcp_pm
   29  TASKSTATS
--- real: CTRL_CMD_GETFAMILY by name
  nlctrl     -> family id 16
  nl80211    -> error 2 (ENOENT)
  devlink    -> error 2 (ENOENT)
  TASKSTATS  -> family id 29
--- real: the tools on a kernel without cfg80211
$ iw dev
nl80211 not found.
$ iw reg get
nl80211 not found.
$ modprobe mac80211_hwsim
modprobe: FATAL: Module mac80211_hwsim not found in directory /lib/modules/6.18.44-fc-v819
$ ls /sys/class/ieee80211
ls: cannot access '/sys/class/ieee80211': No such file or directory
$ wpa_supplicant -D nl80211 -i wlan0 -c /dev/null -t
1795510690.232439: Successfully initialized wpa_supplicant
1795510690.233558: nl80211: 'nl80211' generic netlink not found
1795510690.233578: Failed to initialize driver 'nl80211'
1795510690.233586: wlan0: Unsupported driver 'nl80211'
```

Reading it:

- The kernel config line is the root cause: **`# CONFIG_WIRELESS is not set`**. No cfg80211, so the family never registers.
- The controller lists **13 families** (ids 16 to 29, with gaps where a module is not loaded). `nlctrl` and `TASKSTATS` resolve; **`nl80211` and `devlink` give error 2 (`ENOENT`)**.
- The tools react exactly as they should: `iw` prints "nl80211 not found" for every command, `modprobe mac80211_hwsim` cannot find the module, `/sys/class/ieee80211` is absent, and `wpa_supplicant -D nl80211` starts, logs `nl80211: 'nl80211' generic netlink not found` and stops with `Unsupported driver 'nl80211'`. The first thing to check on a "wifi does not work" report from a custom kernel is whether this lookup succeeds.

### 5.2 Building the requests

`s2_nl80211.py` encodes the messages of an ordinary bring-up and scan, writes them into a capture file with the netlink link layer, and has `tshark` decode them. A NEWFAMILY reply is included at the start so that the dissector learns that family id 33 is `nl80211`, as a real capture of a program starting would show.

```python
CMD = dict(GET_WIPHY=1, SET_INTERFACE=6, NEW_INTERFACE=7, GET_STATION=17, TRIGGER_SCAN=33, AUTHENTICATE=37, REGISTER_FRAME=58)
A = dict(WIPHY=1, IFINDEX=3, IFNAME=4, IFTYPE=5, MAC=6, WIPHY_FREQ=38, SCAN_FREQUENCIES=44, SCAN_SSIDS=45, SSID=52, AUTH_TYPE=53, FRAME_MATCH=91, FRAME_TYPE=101)
IFTYPE_AP, IFTYPE_STATION, IFTYPE_MONITOR = 3, 2, 6
def nest(t, items): return nla(t | 0x8000, b''.join(items))
msgs = []
def add(cmd, attrs, flags=NLM_F_REQUEST | NLM_F_ACK): msgs.append(genl(FAM, cmd, b''.join(attrs), flags, seq=len(msgs) + 1))
```

```
--- 8 messages written, sizes in bytes: 48, 28, 48, 36, 72, 64, 40, 44
--- tshark decode of each message against what was encoded (the names confirm the command and attribute numbers)
(frame | bytes | command | top level attributes as decoded | matches the encoder)
  2 |  28 | GET_WIPHY      | WIPHY                                                | yes
  3 |  48 | NEW_INTERFACE  | WIPHY IFNAME IFTYPE                                  | yes
  4 |  36 | SET_INTERFACE  | IFINDEX IFTYPE                                       | yes
  5 |  72 | TRIGGER_SCAN   | IFINDEX SCAN_FREQUENCIES SCAN_SSIDS                  | yes
  6 |  64 | AUTHENTICATE   | IFINDEX WIPHY_FREQ MAC SSID AUTH_TYPE                | yes
  7 |  40 | GET_STATION    | IFINDEX MAC                                          | yes
  8 |  44 | REGISTER_FRAME | IFINDEX FRAME_TYPE FRAME_MATCH                       | yes
7 of 7 messages decode to the commands and attributes that were encoded
family name resolved from the NEWFAMILY message for 7 of 7 messages
```

All **7 of 7** requests decode to the command and the attributes encoded, which also confirms the numbers used for the 7 commands and 12 attributes (a wrong number would print another name). What the table shows:

- A message is a 16 byte `nlmsghdr`, a 4 byte `genlmsghdr` and the attributes. `GET_WIPHY` with one 8 byte attribute is **28 bytes**; `TRIGGER_SCAN` is **72 bytes**.
- Every attribute is a 4 byte header (length including the header, type) plus the value, padded to a multiple of 4. A **nested** attribute has bit 15 of the type set (`0x802c` for `SCAN_FREQUENCIES`) and its payload is more attributes, here numbered 0, 1, 2 for the list entries. The SSID list holds `corp` and an empty entry (a wildcard SSID, which requests a broadcast probe request).
- `NEW_INTERFACE` creates `wlan1` as type AP (3) on wiphy 0; `SET_INTERFACE` turns the interface 5 into monitor type (6). These are the commands behind `iw phy phy0 interface add wlan1 type ap` and `iw dev wlan0 set type monitor`.
- `AUTHENTICATE` carries the frequency, the BSSID, the SSID and the authentication algorithm (0 is open system): this is the call the supplicant makes on a SoftMAC driver, and the kernel then builds and sends the authentication frame. `REGISTER_FRAME` asks the kernel to deliver received management frames of a type (here 0x00d0, action frames) to this socket, which is how `hostapd` receives the frames it handles in user space.
- In a **FullMAC** driver the supplicant would send one `CONNECT` instead of `AUTHENTICATE` and `ASSOCIATE`, because the firmware performs both.

### 5.3 Events, and the control port

The kernel sends events to the multicast groups: scan results ready, authentication, association and disconnect (`mlme`), regulatory changes, radar detection, channel switch, station added or removed, TX status of management frames. `iw event -t` prints them with timestamps, and `hostapd` and `wpa_supplicant` turn them into their own log lines (`CTRL-EVENT-CONNECTED`, `AP-STA-CONNECTED`).

The 4-way handshake frames (EAPOL-Key) are data frames. With the **control port** feature (`NL80211_ATTR_CONTROL_PORT`, from memory) the kernel passes EAPOL frames to user space over nl80211 instead of a raw `AF_PACKET` socket, keeping them ordered with the association state, and the daemon installs the derived keys with `NEW_KEY` and `SET_STATION` (authorize). This is the line between the protocol the daemon runs and the encryption the kernel and the hardware do.



## 6. User space: `iw`, `wpa_supplicant`, `hostapd`

### 6.1 `iw`

`iw` is the thin nl80211 client: one command is one netlink request, and the output is the decoded reply. `s5_iw.py` reads the real `iw` 6.7 help text of this machine (the tool prints its help without a wireless stack):

```
--- real: iw 6.7 help lists 129 command forms
(object | command forms)
  dev       82
  phy       32
  wdev       7
  reg        3
  event      1
  list       1
  commands   1
  features   1
  help       1
```

The command surface by object: **82 forms under `dev`** (the interface), **32 under `phy`** (the radio), **7 under `wdev`** (an interface without a net device), 3 under `reg`, and the global `event`, `list`, `commands` and `features`. Under `dev` and `phy` the largest verb is `set` (33 forms), then `cac` (9, the DFS channel availability check), `station` (8), `mpath` (6, mesh paths), `scan` (6), `nan` (5), `hwsim` (4), and `ap`, `ftm`, `ibss`, `mesh`, `ocb`, `p2p`, `wowlan`, `coalesce`, `switch`, `vendor`. The ones used daily:

| Command | Does | nl80211 command |
|---|---|---|
| `iw phy phy0 info` | wiphy capabilities, bands, channels with flags, interface combinations, supported commands | `GET_WIPHY` (dump) |
| `iw dev` | the interfaces and their type, channel, txpower | `GET_INTERFACE` |
| `iw phy phy0 interface add mon0 type monitor` | create a virtual interface | `NEW_INTERFACE` |
| `iw dev wlan0 scan` and `scan dump` | trigger a scan, or print the cached results | `TRIGGER_SCAN`, `GET_SCAN` |
| `iw dev wlan0 link` | the current association, signal, bitrate | `GET_STATION` |
| `iw dev wlan0 station dump` | per station signal, rates, counters, inactive time, TX failed | `GET_STATION` (dump) |
| `iw dev wlan0 survey dump` | per channel noise, active, busy and transmit time (airtime utilisation) | `GET_SURVEY` |
| `iw dev wlan0 set channel 36 HT40+` | change channel | `SET_CHANNEL` or `SET_WIPHY` |
| `iw reg get`, `iw reg set DE` | regulatory domain | `GET_REG`, `REQ_SET_REG` |
| `iw event -t` | print every event with a time stamp | multicast subscription |
| `iw dev wlan0 set power_save off` | power save | `SET_POWER_SAVE` |
| `iw dev wlan0 set bitrates ht-mcs-5 9` | restrict rates, a debug tool | `SET_TX_BITRATE_MASK` |

`iw station dump` and `survey dump` are the two commands that turn a vague complaint into numbers: the first gives a per client signal, TX and RX bitrate, retry and failure counts, the second the airtime utilisation of the channel from the radio's own counters. (`iw dev wlan0 info` shows the type and channel; the older `iwconfig` uses wireless extensions, a deprecated interface that cfg80211 only emulates.)

### 6.2 `wpa_supplicant`

`wpa_supplicant` runs the station: scanning policy, network selection from its configuration, authentication and association through the driver, the 4-way handshake, EAP, SAE, roaming (11k, 11v, 11r, OKC), background scans, P2P and MBO. Its structure (from memory):

- A **driver interface** is a table of operations (`struct wpa_driver_ops`) with several backends. The real binary of this machine lists them: **`nl80211`, `wext`, `wired`, `macsec_linux`, `none`**. The `wired` backend is the one used for the 802.1X experiments of [WF 08](08_wifi_capture_tools_and_debug.md) and [WF 09](09_wifi_testing.md), and on a kernel with no wireless stack it is the only one that starts. The `nl80211` backend sends `AUTHENTICATE` and `ASSOCIATE` on a SoftMAC driver and `CONNECT` when the driver advertises `CONNECT` support.
- An **event loop** (`eloop`) serves the netlink socket, control sockets and timers, so the daemon is single threaded.
- A **control interface** (a Unix socket in `/var/run/wpa_supplicant/<iface>` when `ctrl_interface=` is set): `wpa_cli` and applications send `STATUS`, `SCAN`, `SCAN_RESULTS`, `ADD_NETWORK`, `SET_NETWORK`, `ENABLE_NETWORK`, `RECONFIGURE`, `BSS_FLUSH`, `ROAM`, and receive unsolicited events after `ATTACH`, as measured in [WF 08](08_wifi_capture_tools_and_debug.md).
- A **configuration file** of `network={}` blocks with `ssid`, `key_mgmt` (`WPA-PSK`, `SAE`, `WPA-EAP`, `OWE`, `FT-PSK`), `proto`, `pairwise`, `ieee80211w`, `eap`, `identity`, `ca_cert`, `bgscan`, `priority`, and global options such as `country`, `sae_pwe`, `ap_scan`.
- **Debug**: `-dd -t` writes the events with time stamps; the useful lines are the `nl80211:` ones (what was sent to and received from the kernel), the `EAPOL:` and `WPA:` ones (handshake state) and `CTRL-EVENT-*`.

### 6.3 `hostapd`

`hostapd` runs the access point: the beacon content, the management frames the driver does not handle, authentication (open, SAE, 802.1X with a built-in EAP server or a RADIUS client), the 4-way and group handshakes, association and station state, 11k, 11v and 11r responses, WPS, multi AP, MBO and OCE, DFS handling, the channel switch and the control interface (`hostapd_cli`: `status`, `all_sta`, `deauthenticate`, `disassociate`, `bss_tm_req`, `req_beacon`, `chan_switch`).

Its configuration file has hundreds of options. `s5_iw.py` scans the **real `hostapd` 2.10 binary** for the option names of the families a Wi-Fi 6 deployment touches:

```
--- real: configuration option names found in the hostapd 2.10 binary, by family (127 names matched)
  he 52  wpa 27  sae 12  ieee80211 6  owe 5  multi 4  ft 3  vht 3  mbo 2  rrm 2  wnm 2  ap 1  beacon 1  bss 1  country 1  dtim 1  hw 1  mobility 1  uapsd 1  wmm 1
  ieee80211 options: ieee80211ac ieee80211ax ieee80211d ieee80211h ieee80211n ieee80211w
  eht options: none (EHT support came after 2.10)
```

The matched names: **52 `he_*`** (HE capabilities, MU EDCA, BSS colour, 6 GHz capabilities, TWT), **27 `wpa*`**, **12 `sae_*`**, 6 `ieee80211*` (`ieee80211n`, `ac`, `ax`, `d`, `h`, `w` in the pattern used), 5 `owe_*`, 4 `multi_ap*`, 3 `ft_*`, 3 `vht_*`, 2 each of `mbo`, `rrm` and `wnm`. There is **no option with the `eht_` prefix**, because EHT (Wi-Fi 7) support was added to `hostapd` after 2.10 (from memory, in the 2.11 release), so a Wi-Fi 7 deployment needs a newer daemon than this one. The scan only counts the strings that match a fixed pattern; it is a map of where the options are, not a complete list.

### 6.4 What `hostapd -t` accepts: the configuration check is not the radio

`hostapd` with `driver=none` parses and validates its configuration and then runs without touching a radio. That makes it a free test for the syntax and the cross-option rules, and `s3_uci.py` uses it (section 11 shows the whole experiment). The result is a lesson in what the check does and does not see:

```
--- real hostapd 2.10 (driver none) on the generated file and on edits of it
  generated as is                                accepted
  SAE with PMF disabled (ieee80211w=0)           accepted
  mixed mode with PMF required (ieee80211w=2)    accepted
  passphrase of 5 characters                     rejected  Line 15: invalid WPA passphrase length 5 (expected 8..63); WPA-PSK enabled, but PSK or passphrase is not configured.; 2 errors found in configuration file '/tmp/s3_h.conf'
  misspelt option (ieee80211x)                   rejected  Line 16: unknown configuration item 'ieee80211x'; 1 errors found in configuration file '/tmp/s3_h.conf'; Failed to set up interface with /tmp/s3_h.conf
  VHT80 with the wrong centre index              accepted
  channel 40 with seg0 index 42 and VHT80        accepted
```

- **Rejected, with the line number and the reason**: a 5 character passphrase (`invalid WPA passphrase length 5 (expected 8..63)`, which also stops `WPA-PSK enabled, but PSK or passphrase is not configured`) and a misspelt option (`unknown configuration item 'ieee80211x'`).
- **Accepted although wrong**: SAE with `ieee80211w=0` (the standard requires management frame protection with SAE) and a VHT80 configuration with a centre channel index of 50 instead of 42 for channel 36. The first would be caught by a client that refuses to join, or by the nl80211 backend on a real driver (from memory); the second by the kernel or the driver when the channel is set, and shows up as `Failed to set up interface`, a failed `START_AP`, or a beacon with a channel width that no client can use.

So a "config reload works in the test" result does not mean the radio will start. The sequence of checks is: syntax (hostapd), cross-option rules (hostapd), the regulatory and capability limits (cfg80211 and the driver at `START_AP`), and the clients' behaviour (the air).

## 7. Virtual interfaces and concurrency

A wiphy can host several interfaces at once, as many as its **interface combinations** allow. `iw phy phy0 info` prints them as "valid interface combinations", each a set of limits such as `{ managed } <= 2, { AP } <= 8, total <= 10, #channels <= 2` (the shape is from memory; the real values depend on the driver). The **interface types** (from memory):

| Type | Use |
|---|---|
| `managed` (station) | a client, run by `wpa_supplicant` |
| `AP` | an access point, run by `hostapd`; several BSSs can share a radio (multiple SSIDs, up to the driver's limit; "MBSSID" is the same idea carried in one beacon, see [WF 06](06_wifi6_6e_7_features.md)) |
| `AP_VLAN` | one per VLAN or per station for dynamic VLAN assignment |
| `monitor` | receives all frames, with radiotap headers; flags control what is delivered (`fcsfail`, `control`, `otherbss`, `cook`, `active`) |
| `mesh point` | 802.11s |
| `ad-hoc` (IBSS) | peer to peer without an AP |
| `P2P-client`, `P2P-GO`, `P2P-device` | Wi-Fi Direct; the device has no net device (a wdev) |
| `OCB` | outside the context of a BSS (802.11p vehicular) |
| `NAN` | neighbour awareness networking, also a wdev |

Three facts that explain real problems:

1. **Concurrency is limited by channels, not interfaces.** Two interfaces on different channels need the radio to switch channels (time slicing, a "channel context" per channel) and each loses airtime. Drivers advertise `#channels <= N`; many single radio chips support `1` for AP plus a station, which forces the station to follow the AP's channel. This is why a repeater built from one radio has half the throughput ([WF 07](07_wifi_architectures_mesh_and_services.md)).
2. **A monitor interface changes behaviour.** On many drivers adding a monitor interface next to an AP disables some aggregation or offload because the firmware must deliver every frame to the host. Capture on a separate radio when you measure performance (the rule of [WF 08](08_wifi_capture_tools_and_debug.md)).
3. **`NO-IR` and DFS flags decide whether an interface may start.** An AP on a `NO-IR` channel cannot transmit a beacon; a station can still receive and then, after it hears an AP (a "beacon hint"), may initiate. A radar detection stops the AP and `hostapd` moves it with a channel switch announcement ([WF 05](05_wifi_roaming_and_radio_management.md)).



## 8. The data path, the transmit queues and airtime

### 8.1 Transmit (from memory)

```
 socket -> TCP/IP -> qdisc (fq_codel or noqueue) -> ndo_start_xmit = ieee80211_subif_start_xmit
   -> 802.3 to 802.11 header (ieee80211_build_hdr: addresses by ToDS/FromDS, QoS control, TID)
   -> station lookup, key lookup, fragmentation if above the threshold
   -> intermediate queue per (station, TID): fq with CoDel   <-- section 8.3
   -> driver wake_tx_queue(txq) -> driver pulls ieee80211_tx_dequeue()
   -> A-MPDU aggregation (mac80211 or firmware), rate selection (minstrel_ht or firmware)
   -> DMA ring -> firmware -> PPDU on the air -> TX status (acked, retries, rate) back up
```

The change that matters most is the **intermediate queue** (`ieee80211_txq`, from the "make Wi-Fi fast" work of the mid 2010s, from memory): before it, the only queue was the driver's FIFO and the qdisc above it, which saw the Wi-Fi link as one device with one rate, so it could neither isolate flows nor know that one station was slow. With a queue per station and TID, mac80211 can run an FQ-CoDel on each and decide **which station** to serve next with knowledge of airtime.

### 8.2 Receive (from memory)

The driver passes an `sk_buff` with the status (signal, rate, flags, decrypted or not, A-MPDU position) to `ieee80211_rx`. mac80211 then runs a chain of **receive handlers** in a fixed order: monitor interface copies first, then the checks of data and management frames (the station lookup, duplicate detection, the PM bit handling, decryption if the hardware did not, defragmentation, the block ack reorder buffer, A-MSDU deaggregation, and the conversion to 802.3), and finally delivery to the network stack or, for management frames, to the MLME and to the user space daemons registered with `REGISTER_FRAME`. The order explains why a frame can be visible in a monitor interface and still be dropped (a replay check failure, a duplicate, a wrong key).

### 8.3 Who gets the air: round robin against airtime fairness

[WF 03](03_wifi_mac.md) showed the performance anomaly: when every station takes equal turns, a slow station takes most of the air. The Linux answer, in the transmit queues, is an **airtime deficit scheduler** (from memory: each station has a deficit counter charged with the airtime of each transmission, taken from the TX status rate and the length, and the scheduler serves the station with the lowest used airtime). `s4_airtime.py` models it against round robin with equal aggregates (the scheduler a plain FIFO driver gives), for four saturated downlink stations at 1201, 600, 150 and 6.5 Mbit/s, aggregates of up to 20 MPDUs (never longer than the 5.484 ms PPDU limit of [WF 02](02_wifi_phy.md)) in the first case, and about 2 ms for everyone in the second. The model is checked against a closed form:

```python
def agg_bits_equal_count(rate, n=20): return max(min(n, int(rate * 5.484e-3 / MPDU)), 1) * MPDU   # one aggregate per turn: up to 20 MPDUs, never longer than the 5.484 ms PPDU limit
def agg_bits_equal_air(rate, air=2e-3): return min(int(rate * air / MPDU), MAXAGG) * MPDU   # airtime fairness: about 2 ms for everyone
```

```
--- four saturated stations on one AP, 20 s of simulated downlink; Mbit/s per station and the share of the air
(scheduler | station | simulated Mbit/s | closed form Mbit/s | share of airtime)
  equal turns (round robin)          | fast 1201 Mbit/s  |     37.7 |     37.7 |   5.0 %
  equal turns (round robin)          | good 600 Mbit/s   |     37.7 |     37.7 |   8.2 %
  equal turns (round robin)          | mid 150 Mbit/s    |     37.7 |     37.7 |  27.0 %
  equal turns (round robin)          | far 6.5 Mbit/s    |      3.8 |      3.8 |  59.8 %
  equal turns (round robin)          | total             |    116.8 |    116.8 |
  equal airtime                      | fast 1201 Mbit/s  |    252.8 |    252.8 |  25.0 %
  equal airtime                      | good 600 Mbit/s   |    137.2 |    137.1 |  25.0 %
  equal airtime                      | mid 150 Mbit/s    |     35.4 |     35.4 |  25.0 %
  equal airtime                      | far 6.5 Mbit/s    |      1.5 |      1.5 |  25.0 %
  equal airtime                      | total             |    426.8 |    426.9 |
--- summary
  equal turns        total   116.8 Mbit/s | slow station takes  59.8 % of the air | fast station gets    37.7 Mbit/s | Jain index of throughput 0.798
  equal airtime      total   426.8 Mbit/s | slow station takes  25.0 % of the air | fast station gets   252.8 Mbit/s | Jain index of throughput 0.542
the fast station alone would get 1011.2 Mbit/s, so equal airtime gives it 25.0 % of that against 3.7 % with equal turns
```

- **Equal turns**: each station gets one aggregate per round, so the first three each get 37.7 Mbit/s (20 MPDUs per turn at very different rates) and the 6.5 Mbit/s station, limited to 2 MPDUs by the PPDU limit, gets 3.8. Total **116.8 Mbit/s**, and the slow station takes **59.8 percent of the air** to deliver 3.8 Mbit/s.
- **Equal airtime**: every station has 25.0 percent of the time, so each gets its own rate times its payload fraction divided by 4: **252.8, 137.2, 35.4 and 1.5 Mbit/s**, total **426.8**, which is **3.65 times** more. The fast station goes from 3.7 percent of what it would get alone to 25.0 percent.
- The simulation equals its closed form (the payload part of each turn is b / (OV + b / r) of its own airtime and a station's share is 1 / N) to 0.1 Mbit/s.
- **What airtime fairness does not do**: it does not give the slow station more throughput (1.5 Mbit/s against 3.8); it removes its effect on the others. The Jain index of the throughputs is lower (0.542 against 0.798) because the throughputs are unequal on purpose; the fairness is in airtime, which is the resource that is shared. This is the answer to "why not equal throughput": that would give the slow station 59.8 percent of the air.
- **The model leaves out** retries and loss (a slow station at a bad rate loses more and costs more), contention between stations (the downlink here is one queue), uplink (the AP cannot schedule the clients' airtime with this mechanism, only with OFDMA triggers of [WF 06](06_wifi6_6e_7_features.md)), and the real accounting error (the airtime is estimated from the rate and length, not measured).

### 8.4 Aggregation and rate control

- **Aggregation** is set up by an `ADDBA` exchange per (station, TID) and then the A-MPDU contains up to 64 (HT, VHT) or 256 (HE) MPDUs. At 1200 Mbit/s of 1500 byte packets (`ans_e`), one MPDU per transmission is **100,000 transmissions a second**, 8 per aggregate is 12,500, 32 is **3,125** and 64 is **1,562**: the CPU cost of the TX completion and the block ack handling falls with the aggregate size, which is why a latency sensitive flow (a small packet each 20 ms) pays the overhead of a whole transmission while a bulk flow does not.
- **Rate control** (`minstrel_ht`, from memory): for each station it keeps a table of the success probability and the throughput of each rate (MCS, streams, width, guard interval), uses the best known rate for most frames and **samples** other rates for about 10 percent of the frames, with a fixed retry chain (the best rate, the second best, a safe fallback). It is a bandit algorithm, which is why it has a convergence time of a few hundred milliseconds after a change and why a fast changing channel makes it hunt. Many firmwares run their own rate control and report the rate in the TX status.

## 9. Offloads: where the data path leaves the kernel

A modern wireless router does not move 2 Gbit/s through the Linux stack on a small CPU; the packet path is shortened at several points. All of this is from memory and varies by chip and release.

| Offload | What it moves | What you lose or must know |
|---|---|---|
| Hardware crypto | CCMP, GCMP, BIP in the MAC | `set_key` can refuse a cipher; the fallback is software and slow |
| Hardware scan, beacon and probe response | scan sequencing, the beacon timer and a probe response template in firmware | the host does not see every probe request; timing bugs are in firmware |
| TX and RX rate and aggregation in firmware | aggregation, retries, rate selection | `ieee80211_tx_status` arrives as batches or estimates; the rate in `iw station dump` can be a firmware summary |
| 802.3 decapsulation and encapsulation offload (`ethernet decap`, "native 802.11 or ethernet mode") | the 802.11 to 802.3 header conversion | frames reach the host as Ethernet, and a monitor or `tcpdump` on the interface shows a different frame than the air carried |
| Flow offloading in netfilter (software and hardware) | established forwarding flows bypass most of the stack | rules after the offload point are not evaluated for the flow; counters can be wrong |
| Hardware NAT and packet processing engines (a PPE on MediaTek and Qualcomm SoCs, an NSS on Qualcomm IPQ, from memory) | routing, NAT and bridging in a switch or accelerator, with the wireless driver delivering frames directly | `tcpdump` does not see the flow; SQM and some firewall features do not apply; a PPE bug looks like a random connectivity failure |
| Receive side scaling, GRO, NAPI | the usual NIC techniques also in the wireless driver | latency from batching |

A test rule follows from this: **always test with the offloads in the state that ships**, and when something is wrong disable them one at a time (`ethtool -K`, the flow offload option, the vendor knob) to find the layer. A capture on the AP's wireless interface may be missing exactly the flows that offload takes, so capture on a separate sniffer, as in [WF 08](08_wifi_capture_tools_and_debug.md).

The CPU budget makes the reason concrete. `ans_e.py` (section 17) computes the cycles available for each packet:

```
--- packets per second and the CPU cycles available per packet at 1500 byte packets (all cores fully used, an upper bound)
(goodput Mbit/s | packets/s | ns per packet | cycles per packet, 4 cores at 1.8 GHz | 2 cores at 1.2 GHz | 4 cores at 2.2 GHz | 40 percent of one 1.8 GHz core)
   300 |    25000 |   40000 |  288000 |   96000 |  352000 |   28800
   600 |    50000 |   20000 |  144000 |   48000 |  176000 |   14400
  1200 |   100000 |   10000 |   72000 |   24000 |   88000 |    7200
  2400 |   200000 |    5000 |   36000 |   12000 |   44000 |    3600
--- aggregation: one A-MPDU of 32 MPDUs per transmission opportunity cuts the completion and block ack events
   1 MPDUs per aggregate at 1200 Mbit/s goodput:  100000 transmissions/s
   8 MPDUs per aggregate at 1200 Mbit/s goodput:   12500 transmissions/s
  32 MPDUs per aggregate at 1200 Mbit/s goodput:    3125 transmissions/s
  64 MPDUs per aggregate at 1200 Mbit/s goodput:    1562 transmissions/s
```

At 1200 Mbit/s of 1500 byte packets a router must handle **100,000 packets a second**, one every **10 µs**. Two 1.2 GHz cores give **24,000 cycles** in total for each packet; if the wireless data path may use only 40 percent of one 1.8 GHz core (the rest is routing, firewall, SQM and the web interface), it has **7,200 cycles**, which a general stack with several memory accesses that miss the cache does not meet comfortably. At 2400 Mbit/s the same budget halves to **3,600 cycles**. This is the engineering reason for the offloads of the table.

## 10. `mac80211_hwsim`: a software radio for testing

`mac80211_hwsim` is a kernel module that registers N virtual radios with mac80211 and passes the frames between them in memory, optionally through a user space medium simulator (`wmediumd`, from memory) that adds a loss model, interference and per link rates. With it a full association, a 4-way handshake, a roam and a DFS event run on a laptop with no hardware: `hostapd` and `wpa_supplicant` run against the virtual radios exactly as against real ones, and a capture on the `hwsim0` monitor interface shows the 802.11 frames. It is the basis of the `hostap` project's automatic test suite (hundreds of tests that exercise the daemons, from memory).

**Not run here** (this VM has no wireless stack, section 5.1): on a machine whose kernel has `CONFIG_MAC80211_HWSIM`, the sequence is

1. `modprobe mac80211_hwsim radios=2` creates `phy0` and `phy1` and, with no other setup, the interfaces `wlan0` and `wlan1` and a monitor interface `hwsim0` that carries every frame.
2. Move `wlan1` into a network namespace (`iw phy phy1 set netns name <ns>`), run `hostapd` on `wlan0` and `wpa_supplicant` on `wlan1`, with a WPA2 or SAE configuration.
3. Capture on `hwsim0` and read the authentication, association, EAPOL and data frames in `tshark`, using the display filters of [WF 08](08_wifi_capture_tools_and_debug.md).
4. Use the `iw hwsim` verbs (the real `iw` of this machine has 4 forms under `hwsim`) to control the simulation, and `wmediumd` to make a link lossy.

Use it for protocol and daemon behaviour (handshakes, roaming logic, configuration, regression tests of the daemons). Do not use it for anything that depends on the air (rates, aggregation timing, interference), which the simulation does not model beyond the loss rule you give it.



## 11. OpenWrt

### 11.1 What OpenWrt is

OpenWrt is a Linux distribution for routers and access points, built from source with a build root (buildroot style) system into a firmware image for one device. The features that matter for Wi-Fi work (all from memory of the project's documentation, and varying by release):

| Piece | Role |
|---|---|
| Kernel with the `mac80211` backports | OpenWrt ships its own copy of the wireless stack (`kmod-cfg80211`, `kmod-mac80211`) built from the backports tree, so a newer wireless stack can run on an older kernel; driver packages (`kmod-ath11k`, `kmod-mt76`, ...) come with it |
| `hostapd`, `wpa_supplicant` | one binary `wpad` that contains both, in variants by size and features (`basic`, full, `mesh`; with `mbedtls`, `wolfssl` or `openssl` for the crypto) |
| `procd` | the init system and process supervisor; services have init scripts that declare their instances and triggers |
| `ubus` | the message bus; the daemons expose objects (`network.wireless`, `hostapd.wlan0`, `network.interface.lan`) with methods and events |
| `netifd` | the network interface daemon: reads `/etc/config/network` and `/etc/config/wireless`, creates the interfaces, bridges and routes, and runs the protocol handlers (static, DHCP, PPPoE) and the wireless handler script |
| UCI | the configuration format and tool: plain text files in `/etc/config/` of `config` sections with `option` and `list` lines, read and written with `uci`, committed with `uci commit` |
| `uhttpd`, `rpcd`, LuCI | the web server, the RPC daemon and the web interface, which are front ends over ubus and UCI |
| `fw4` (nftables) or `fw3` (iptables) | the firewall, generated from `/etc/config/firewall` |
| `dnsmasq`, `odhcpd` | DHCP and DNS, IPv6 |
| `squashfs` plus an `overlayfs` | a read only root with a writable overlay for the configuration, which makes "reset to defaults" a deletion of the overlay |
| `opkg` or `apk` | the package manager |
| `usteer`, `dawn` | optional band and client steering daemons over ubus |

### 11.2 From `/etc/config/wireless` to a running radio

A `wifi-device` section describes a radio (`type`, `band`, `channel`, `htmode`, `country`, `txpower`, `cell_density`), and each `wifi-iface` section a virtual interface on it (`device`, `network`, `mode`, `ssid`, `encryption`, `key`, `ieee80211w`, `ieee80211r`, `isolate`, `hidden`, `wmm`, ...). The flow (from memory) when the configuration changes or the system starts:

```
/etc/config/wireless  --uci-->  netifd  --wireless handler script (mac80211.sh)-->
   builds a JSON description per radio
     --ubus call hostapd config_add / wpa_supplicant config_add-->  hostapd, wpa_supplicant
        --nl80211-->  cfg80211 / mac80211 / driver
   then netifd adds the interface to the bridge named by the `network` option
```

`wifi reload` or `wifi` re-runs the flow, `ubus call network.wireless status` shows each radio and interface as netifd sees it, `ubus call hostapd.wlan0 get_clients` returns the associated stations as JSON, `hostapd_cli -i wlan0 status` the BSS state, `iwinfo` and `iw` the kernel's view, and `logread -f` the logs of `netifd`, `hostapd` and the kernel in one stream.

### 11.3 A UCI section turned into a `hostapd` configuration

`s3_uci.py` parses a UCI file, generates the `hostapd` configuration with a mapping that models what the OpenWrt scripts do, and lets the real `hostapd` judge it:

```python
ENC = {  # encryption -> (wpa_key_mgmt, ieee80211w default)
    'psk2': ('WPA-PSK', '0'), 'psk2+ccmp': ('WPA-PSK', '0'), 'sae': ('SAE', '2'), 'sae-mixed': ('WPA-PSK SAE', '1'), 'owe': ('OWE', '2')}
def centre_idx(ch, width):   # 5 GHz VHT: segment 0 centre channel index
    blocks = {80: [(36, 48, 42), (52, 64, 58), (100, 112, 106), (116, 128, 122), (132, 144, 138), (149, 161, 155)], 40: [(36, 40, 38), (44, 48, 46), (52, 56, 54), (60, 64, 62)]}
    for a, b, c in blocks[width]:
        if a <= ch <= b: return c
```

```
--- the UCI file parsed: 2 sections
  wifi-device  radio1   type=mac80211 band=5g channel=36 htmode=VHT80 country=US
  wifi-iface   corp     device=radio1 network=lan mode=ap ssid=corp encryption=sae-mixed key=correct horse ieee80211w=1 isolate=1
```

```
--- the generated hostapd configuration
driver=none
interface=wlan0
ssid=corp
hw_mode=a
channel=36
country_code=US
ieee80211n=1
ieee80211ac=1
vht_oper_chwidth=1
vht_oper_centr_freq_seg0_idx=42
wpa=2
wpa_key_mgmt=WPA-PSK SAE
rsn_pairwise=CCMP
ieee80211w=1
wpa_passphrase=correct horse
ap_isolate=1
```

How the options map:

- `band 5g` gives `hw_mode=a`, `2g` gives `hw_mode=g`; a 6 GHz radio uses the same `a` mode with an `op_class` and `he_oper_*` options (from memory).
- `htmode VHT80` gives `ieee80211n=1`, `ieee80211ac=1`, `vht_oper_chwidth=1` and `vht_oper_centr_freq_seg0_idx` equal to the centre channel of the 80 MHz block. For channel 36 that is **42** (the mean of the frequencies 5180, 5200, 5220 and 5240 MHz is 5210 MHz, index (5210 - 5000) / 5), and `ans_c` computes the index of every block. The script uses the primary channel to find the block, as the real scripts do.
- `encryption sae-mixed` gives `wpa_key_mgmt=WPA-PSK SAE`, `wpa=2` (RSN), `rsn_pairwise=CCMP` and `ieee80211w=1` (PMF optional, so WPA2 only clients can still join). `sae` alone sets PMF required (`ieee80211w=2`) and `owe` uses key management OWE with PMF required. `psk2` gives `WPA-PSK` and no PMF by default.
- `isolate 1` gives `ap_isolate=1`: clients of the BSS cannot talk to each other.

The real `hostapd` run on the generated file and on edits gave the verdicts of section 6.4: it rejected the short passphrase and the misspelt option and accepted the generated file, SAE without PMF and a wrong VHT centre index. The lesson for an OpenWrt developer is that **`wifi reload` succeeding does not mean the radio is on the channel you asked for**; read `iw dev wlan0 info` and the beacon in a capture.

### 11.4 Practical points

- **Defaults differ between releases**: the default encryption, the default PMF setting, whether new networks start disabled, and the country (a missing `country` leaves the radio on the world domain at lower power and with fewer channels, a common cause of "no 5 GHz channel above 64").
- **Per radio, not per interface**: the channel and the width belong to the radio, so changing one SSID's channel changes them all; MBSS on a radio shares the channel, the beacon interval and the capabilities.
- **The bridge and the VLANs**: `option network 'lan'` puts the wireless interface in the bridge of the `lan` interface; with 802.1Q and dynamic VLAN the AP uses `AP_VLAN` interfaces.
- **SQM and offloading**: SQM (`fq_codel` or `cake`, see [NET 14](../14_queueing_qos_and_traffic_management.md)) needs software forwarding, so enabling it disables flow offloading on that path; this is a classic cause of a router that is fast without SQM and slow with it.
- **Fast roaming and steering**: `ieee80211r`, `ieee80211k`, `ieee80211v` options plus `usteer` or `dawn` implement the mechanisms of [WF 05](05_wifi_roaming_and_radio_management.md) on a fleet of OpenWrt APs, using ubus events from `hostapd`.
- **Developer loop**: build one image with `make menuconfig`, test a package change with `opkg install` of a built package or a `sysupgrade` of the image, read `dmesg` and `logread`; a kernel module of a driver can be unloaded and reloaded with `rmmod` and `modprobe` without a reboot on many devices.

## 12. Driver bring-up, as a platform engineer meets it

The sequence for a new SoC or radio module on Linux (from memory, as a checklist rather than a recipe):

1. **Hardware description**: the device tree node (bus, interrupts, clocks, reset and power GPIOs, the memory regions, the firmware name, the board data and calibration source, the regulatory region). A radio on an SoC may keep its calibration data in a flash partition read through `nvmem`.
2. **Probe and firmware**: the driver probes, enables clocks and power, `request_firmware` loads the firmware and the board data from `/lib/firmware`; the first sign of a problem is a firmware timeout in `dmesg` (a wrong file, a wrong version, an unpowered chip).
3. **The control interface to the firmware**: WMI or similar commands and events, a QMI service for host and firmware setup on some Qualcomm parts, with a ready event before anything else works.
4. **The data rings**: DMA descriptors for TX and RX, interrupt and NAPI handling, the completion and the status path; the common bugs are a ring that stalls (a missing doorbell write or a missed interrupt), a cache coherency problem (a missing barrier or an unsynced buffer), and a leak of buffers on the error path.
5. **Capabilities in the `wiphy`**: bands and channels, HT, VHT, HE and EHT capabilities, the number of streams and the maximum aggregate, the interface combinations, the cipher suites, the features (scan, probe response offload, TWT, OFDMA, MLO); a wrong capability is a wrong beacon and a client that cannot use the rates.
6. **Regulatory and power**: the board data and the regulatory limits per channel; a TX power table error shows up as low range or an illegal emission.
7. **Test**: association with `wpa_supplicant`, an AP with `hostapd`, throughput with the method of [WF 09](09_wifi_testing.md), a capture with a separate sniffer, a soak, and the offload paths with and without.

What to read when it fails: `dmesg`, the firmware log (many drivers have a debugfs file or a trace event for it), `iw event`, `hostapd -dd`, `ethtool -S`, `/sys/kernel/debug/ieee80211/phy0/` (per station and per queue state, from memory), and `trace-cmd record -e mac80211 -e cfg80211` (from memory) for the kernel's own events.



## 13. A debugging playbook by layer

| Symptom | Layer to suspect first | First tools | What the answer looks like |
|---|---|---|---|
| `iw` says "nl80211 not found", no `phy` | no cfg80211 in the kernel, or the module is not loaded | `zcat /proc/config.gz`, `lsmod`, `ls /sys/class/ieee80211`, the genl family lookup of section 5.1 | the config option is off, or the driver did not probe |
| `phy` exists, no interface works | driver probe or firmware | `dmesg`, `ethtool -i`, the firmware files in `/lib/firmware` | a firmware timeout or a missing board file |
| A channel is missing or has `NO-IR` | regulatory | `iw reg get`, `iw phy phy0 info` flags, the country in the config | wrong country, a self managed driver, a DFS channel not yet available |
| AP does not start | hostapd config, then START_AP | `hostapd -dd`, the `nl80211:` lines, `iw dev` | a rejected option, a channel the kernel refuses, a missing capability |
| Clients cannot associate | hostapd MLME | `hostapd -dd`, `iw event -t`, a capture ([WF 08](08_wifi_capture_tools_and_debug.md)) | a status code in the association response, or no response at all |
| Association works, no traffic | keys, then the bridge | `hostapd` handshake lines, `iw dev wlan0 station dump`, `bridge fdb`, `tcpdump` on the bridge | a handshake that did not finish, the station not authorized, a VLAN or isolation setting |
| Throughput low | rate and airtime | `iw dev wlan0 station dump` (TX bitrate, retries, failed), `iw dev wlan0 survey dump` (busy time), a sniffer | a low rate from a poor signal, a high retry share, a busy channel, a slow client taking the air (section 8.3) |
| Throughput collapses with many clients | queues, offload, CPU | `top` and `mpstat` (softirq), `/proc/softirqs`, `ethtool -S`, flow offload state | a CPU limit at the packet rate (section 9), an aggregation limit, an offload bug |
| Roaming is slow or wrong | supplicant policy and handshake | `wpa_supplicant -dd`, `wpa_cli`, 11k and 11v frames in a capture | a scan that costs too long, no 11r, a client that ignores the steering request |
| The AP stops beaconing | firmware crash or DFS | `dmesg`, `iw event`, the firmware log, the radar events | a firmware restart (the driver reports and recovers), or a radar detection and a channel switch |
| Works in the lab, fails in the house | RF, not software | the method of [WF 09](09_wifi_testing.md) and the capture rules of [WF 08](08_wifi_capture_tools_and_debug.md) | interference, positions, or a client quirk |

Two habits that save time. **Decide the layer before reading logs**: one `iw event` run and one `hostapd -dd` run show whether the failure is a daemon decision (a log line with a reason), a kernel refusal (an errno from nl80211) or silence from the driver (no event at all). **Find the log with the highest level that has the information**: a status code in a management frame is in the capture; a refused `START_AP` is in the `nl80211:` log lines with the errno; a firmware assert is only in the driver or firmware log.

## 14. Common mistakes

1. **Assuming `iw` works everywhere.** It needs nl80211; on a kernel without it every command fails with "nl80211 not found" (section 5.1).
2. **Treating a config check as a radio check.** `hostapd` with `driver=none` rejected only 2 of 6 edits and accepted a SAE network with PMF disabled and a wrong VHT centre index (section 6.4).
3. **Believing the beacon is built by the driver.** `hostapd` builds the content; the kernel or the firmware repeats it. A wrong element is a `hostapd` configuration or a capability from the driver.
4. **Mixing up FullMAC and SoftMAC when reading logs.** On FullMAC the association is in the firmware and `iw event` shows the result, not the frames.
5. **Debugging with a monitor interface next to the AP and then measuring throughput.** A monitor interface can disable offloads and aggregation on some drivers.
6. **Forgetting the country.** The world domain limits power and channels; many "missing 5 GHz channel" reports are this.
7. **Reading the firmware's rate as the air's rate.** Offloaded rate control reports a summary, and it is not the same as a capture.
8. **Using equal throughput as the fairness goal.** Equal turns gave 116.8 Mbit/s in the model and equal airtime 426.8; the slow station takes 59.8 percent of the air under equal turns (section 8.3).
9. **Testing a regression with offloads off.** The shipped path is the offloaded one. Test with the offload in the shipped state, and disable it to isolate (section 9).
10. **Turning on SQM and expecting line rate.** SQM needs software forwarding and the flow offload is bypassed (section 11.4).
11. **Using `hwsim` for performance.** It models the protocol, not the air.
12. **Reloading Wi-Fi and believing the new configuration is active.** `wifi reload` returning means netifd ran, not that the radio is on the channel you set; check `iw dev` and a capture.
13. **Editing `/etc/config/wireless` and not committing.** `uci set` changes are in memory until `uci commit`.
14. **A daemon from an older release on a newer kernel feature.** `hostapd` 2.10 has no EHT options (section 6.3); a Wi-Fi 7 AP needs a newer one.
15. **Quoting kernel function names from memory in a design review.** They move between releases; check the tree you work on.

## 15. Interview questions

1. **Draw the Linux wireless stack and say who owns the association on an AP and on a station.** User space `hostapd` (AP, frames delivered by `REGISTER_FRAME`) and `wpa_supplicant` (station, `AUTHENTICATE` and `ASSOCIATE`, or `CONNECT` on FullMAC), cfg80211 as the API and the regulatory owner, mac80211 as the state machine and data path, the driver and the firmware below (section 1).
2. **What is the difference between FullMAC and SoftMAC, and why does it change how you debug?** Where the MLME runs; with SoftMAC the host sees the frames and owns the state, with FullMAC the host sees only commands and events and the firmware log is the truth (section 3).
3. **What does `iw dev wlan0 scan` do on the wire?** One `TRIGGER_SCAN` with SSIDs and frequencies (72 bytes in section 5.2), an event when the scan finishes, then a `GET_SCAN` dump of the cached BSS list; `scan dump` alone reads the cache.
4. **How does a daemon learn the nl80211 family id?** Generic netlink controller lookup by name (`CTRL_CMD_GETFAMILY`); the id is dynamic; the lookup returns `ENOENT` if cfg80211 is not loaded (section 5.1).
5. **Who sends the beacons?** `hostapd` builds the template (head, tail and the TIM position), the kernel or the firmware transmits it every beacon interval and updates the TIM and the counters (the channel switch countdown, the BSS colour change countdown).
6. **Walk through what happens when a station sends a frame in mac80211.** Section 8.1: header conversion, key and station lookup, the intermediate queue per station and TID, `wake_tx_queue`, aggregation and rate selection, DMA, TX status.
7. **Why an intermediate queue per station, and what is airtime fairness?** To isolate flows and stations and to schedule by airtime; 116.8 against 426.8 Mbit/s in the model (section 8.3).
8. **What does the regulatory code decide, and what can override it?** The allowed channels, power, DFS and NO-IR flags, intersected with the driver and the hint; a self managed driver does its own and `iw reg set` has no effect on it (section 2.3).
9. **How would you test a change to the roaming logic without a radio?** `mac80211_hwsim` with `hostapd` and `wpa_supplicant` and `wmediumd` for loss; accept that it tests the protocol and not the air (section 10).
10. **What happens when you run `wifi reload` on OpenWrt?** netifd re-reads UCI, the wireless handler generates the per radio description, `ubus` calls add or remove the interfaces in `hostapd` and `wpa_supplicant`, netifd sets up the bridge membership (section 11.2).
11. **A customer says 5 GHz channel 100 does not appear. What do you check?** The country, the regulatory flags in `iw phy phy0 info`, DFS state (`usable` before the CAC, `available` after, 60 s or 600 s), and whether the driver is self managed.
12. **How do offloads change testing?** The shipped path is the offloaded one; captures on the AP can miss offloaded flows; disable one at a time to find the layer (section 9).
13. **The AP crashes under load but only with many clients. Where do you start?** `dmesg` for the firmware assert, the firmware log, the interrupt and softirq distribution, the CPU budget per packet (section 9), the aggregation and queue state in debugfs, and a reproduction with a client emulator and the repeatable method of [WF 09](09_wifi_testing.md).
14. **Why does `wpa_supplicant` run in user space, and what stays in the kernel?** Policy, EAP, SAE and handshakes are code that changes and needs libraries and key material handling; the kernel keeps keys, crypto, queues and frames.
15. **Why does `hostapd` accept a bad configuration?** It checks syntax and some cross rules; the radio limits come later from the kernel and the driver (section 6.4).



## 16. Problems

Work each one before reading section 17. Problems A, C and E are arithmetic, B is a configuration, D is a scheduling model.

**Problem A. Size a scan request.** A `NL80211_CMD_TRIGGER_SCAN` message carries a netlink header (16 bytes), a generic netlink header (4 bytes), the interface index (8 bytes), a nested frequency list with 3 frequencies (28 bytes with its header) and a nested SSID list with 2 SSIDs (16 bytes). Work out the size, then say how it grows with 16 frequencies and 4 SSIDs.

**Problem B. UCI to hostapd.** Write the hostapd configuration for a 2.4 GHz AP on channel 6 with a 40 MHz channel, OWE (the open network with encryption), and say which secondary channel you picked and why. Then say what the `driver=none` check of section 6.4 can and cannot tell you about it.

**Problem C. VHT centre index.** For 5 GHz channels 36, 52, 100, 116, 132 and 149, give the centre channel index of the 80 MHz block that holds each, and the index for a 160 MHz channel starting at 36 and at 100.

**Problem D. Airtime fairness with three stations.** Three stations have rates 866, 300 and 6.5 Mbit/s. Use the model of section 8.3 (120 us overhead per transmission, 1500 byte MPDUs, the 5.484 ms cap). Give the per station and the total throughput under equal turns and under equal airtime, and the airtime shares under equal turns.

**Problem E. A CPU budget.** An AP forwards 1500 byte packets in software. For 300, 600, 1200 and 2400 Mbit/s: the packets per second, the time per packet, and the cycles per packet with two 1.2 GHz cores and with 40 percent of one 1.8 GHz core. Then give the transmissions per second at 1200 Mbit/s with 1, 8, 32 and 64 MPDUs per aggregate.

## 17. Answers

**Answer A.** The two headers are 20 bytes, so 20 + 8 + 28 + 16 = 72 bytes. The size grows by 4 bytes of header plus 4 bytes per frequency and by 4 bytes of header plus the SSID length per SSID, rounded up to 4 bytes. The computed result:

--- by arithmetic: header 20 + ifindex 8 + frequencies 28 + ssids 16 = 72 bytes
built: 72 bytes, nlmsg_len field 72
attributes found by parse_attrs: [3, 44, 45]
--- the same message with 16 frequencies and 4 SSIDs
212 bytes (a driver advertises its own limit on SSIDs and channels per scan request in the wiphy; the values differ by chip, from memory)

**Answer B.** Channel 6 is 2437 MHz. HT40+ puts the secondary channel above (channel 10, 2457 MHz, centre 2447 MHz) and HT40- puts it below (channel 2, 2417 MHz, centre 2427 MHz). Both fit in the 2.4 GHz band, so the choice is about the neighbours: pick the side that overlaps less with other networks, and remember that 40 MHz on 2.4 GHz is rarely a good idea in a crowded building. OWE needs `ieee80211w=2` and a transition or a plain OWE BSS. Real hostapd accepted this, and also accepted PMF off and the other secondary channel, so a pass means the syntax is right and says nothing about the radio:

--- generated for channel 6 HT40 OWE:
driver=none
interface=wlan0
ssid=guest
hw_mode=g
channel=6
country_code=US
ieee80211n=1
ht_capab=[HT40-]
wpa=2
wpa_key_mgmt=OWE
rsn_pairwise=CCMP
ieee80211w=2
real hostapd on it: accepted
with ieee80211w=0:  accepted
with HT40+ instead:  accepted
channel 6 is 2437 MHz; HT40+ puts the secondary at 2457 MHz (channel 10) and the block centre at 2447 MHz; HT40- puts it at 2417 MHz (channel 2), centre 2427 MHz

**Answer C.** The 80 MHz blocks are 36 to 48 (centre 42), 52 to 64 (58), 100 to 112 (106), 116 to 128 (122), 132 to 144 (138) and 149 to 161 (155). The 160 MHz channel from 36 has centre 50 and the one from 100 has centre 114. A wrong centre index was accepted by `hostapd` with `driver=none` in section 6.4, so a table lookup in the generator is what protects you:

--- the centre channel index of each 80 MHz and 160 MHz block in 5 GHz, from the mean of its 20 MHz channel frequencies
   80 MHz from channel  36: channels 36 40 44 48                  centre  5210.0 MHz -> index  42.0
   80 MHz from channel  52: channels 52 56 60 64                  centre  5290.0 MHz -> index  58.0
   80 MHz from channel 100: channels 100 104 108 112              centre  5530.0 MHz -> index 106.0
   80 MHz from channel 116: channels 116 120 124 128              centre  5610.0 MHz -> index 122.0
   80 MHz from channel 132: channels 132 136 140 144              centre  5690.0 MHz -> index 138.0
   80 MHz from channel 149: channels 149 153 157 161              centre  5775.0 MHz -> index 155.0
  160 MHz from channel  36: channels 36 40 44 48 52 56 60 64      centre  5250.0 MHz -> index  50.0
  160 MHz from channel 100: channels 100 104 108 112 116 120 124 128 centre  5570.0 MHz -> index 114.0
--- primary channel 44 in 80 MHz: the block is 36 40 44 48 (index 42), the primary is its third 20 MHz channel

**Answer D.** Equal turns give the same number of transmissions to each station, so the slow station holds the air. Equal airtime gives each station a third of the air and its own rate times that share (with the overhead term). The result:

--- equal turns: one aggregate each, cycle 5.13 ms
    866 Mbit/s station: aggregate 20 MPDUs, 0.40 ms, throughput   46.8 Mbit/s, airtime share   7.7 %
    300 Mbit/s station: aggregate 20 MPDUs, 0.92 ms, throughput   46.8 Mbit/s, airtime share  17.9 %
    6.5 Mbit/s station: aggregate  2 MPDUs, 3.81 ms, throughput    4.7 Mbit/s, airtime share  74.3 %
--- equal airtime: each station gets 1/3 of the time
    866 Mbit/s station: aggregate 64 MPDUs, payload fraction of its own air 0.881, throughput  254.3 Mbit/s
    300 Mbit/s station: aggregate 50 MPDUs, payload fraction of its own air 0.943, throughput   94.3 Mbit/s
    6.5 Mbit/s station: aggregate  1 MPDUs, payload fraction of its own air 0.939, throughput    2.0 Mbit/s
totals: equal turns 98.3 Mbit/s, equal airtime 350.6 Mbit/s (3.6 times)

**Answer E.** At 1200 Mbit/s the AP forwards 100,000 packets per second, which is 10 us per packet, and two 1.2 GHz cores give 24,000 cycles per packet while 40 percent of one 1.8 GHz core gives 7,200. At 2400 Mbit/s the same 40 percent is 3,600 cycles. Aggregation divides the transmission rate: 100,000, 12,500, 3,125 and 1,562 transmissions per second for 1, 8, 32 and 64 MPDUs. The table:

--- packets per second and the CPU cycles available per packet at 1500 byte packets (all cores fully used, an upper bound)
(goodput Mbit/s | packets/s | ns per packet | cycles per packet, 4 cores at 1.8 GHz | 2 cores at 1.2 GHz | 4 cores at 2.2 GHz | 40 percent of one 1.8 GHz core)
   300 |    25000 |   40000 |  288000 |   96000 |  352000 |   28800
   600 |    50000 |   20000 |  144000 |   48000 |  176000 |   14400
  1200 |   100000 |   10000 |   72000 |   24000 |   88000 |    7200
  2400 |   200000 |    5000 |   36000 |   12000 |   44000 |    3600
--- aggregation: one A-MPDU of 32 MPDUs per transmission opportunity cuts the completion and block ack events
   1 MPDUs per aggregate at 1200 Mbit/s goodput:  100000 transmissions/s
   8 MPDUs per aggregate at 1200 Mbit/s goodput:   12500 transmissions/s
  32 MPDUs per aggregate at 1200 Mbit/s goodput:    3125 transmissions/s
  64 MPDUs per aggregate at 1200 Mbit/s goodput:    1562 transmissions/s

## 18. Cheat sheet

| Item | Value or rule |
|---|---|
| Stack | hostapd and wpa_supplicant (user space), nl80211 (genl), cfg80211, mac80211 or FullMAC driver, firmware |
| Who owns what | cfg80211 owns the API and the regulatory rules; mac80211 owns the MLME state machine and data path on SoftMAC; the firmware owns it on FullMAC |
| nl80211 | a generic netlink family, id looked up by name at run time; commands such as `GET_WIPHY`, `TRIGGER_SCAN`, `AUTHENTICATE`, `START_AP`, `GET_STATION` |
| Kernel without nl80211 | `iw` prints "nl80211 not found"; `wpa_supplicant -D nl80211` fails |
| Config check | `hostapd` with `driver=none` checks syntax only |
| OpenWrt | UCI, netifd, wireless handler, hostapd and wpa_supplicant over `ubus`; commit before reload |
| Fairness | equal airtime gave 3.65 times the total of equal turns in the model |
| CPU budget | 1200 Mbit/s of 1500 B packets is 100,000 packets per second, 10 us per packet |
| Aggregation | transmissions per second fall by the MPDU count per aggregate |
| VHT centre | 80 MHz blocks: 42, 58, 106, 122, 138, 155; 160 MHz: 50, 114 |
| Debug order | decide the layer, then the log with the most information at that layer (section 13) |
| Not tested here | any radio behaviour; every kernel and driver detail is from the documents or from memory and is marked so |

Next: [WF 12, Wi-Fi 7 EHT and MLO](12_wifi7_eht_and_mlo.md) is the next in order (not yet written). Related: [WF 03 MAC](03_wifi_mac.md), [WF 04 security](04_wifi_security.md), [WF 05 roaming and radio management](05_wifi_roaming_and_radio_management.md), [WF 06 Wi-Fi 6 features](06_wifi6_6e_7_features.md), [WF 07 architectures](07_wifi_architectures_mesh_and_services.md), [WF 08 capture tools](08_wifi_capture_tools_and_debug.md), [WF 09 testing](09_wifi_testing.md).

## Sources

- Candela Technologies, wireless course, session 7d (Linux wireless stack and OpenWrt): https://www.candelatech.com/course.php
- Linux wireless documentation: https://wireless.docs.kernel.org/ and the kernel documentation at https://docs.kernel.org/driver-api/80211/
- `nl80211.h` and the cfg80211 and mac80211 sources of the kernel you work on (names and attributes move between releases)
- OpenWrt documentation: https://openwrt.org/docs/guide-user/network/wifi/basic and the UCI and netifd pages
- hostapd and wpa_supplicant: https://w1.fi/hostapd/ and the sample configuration files
- Manual pages for `iw`, `hostapd`, `wpa_supplicant`, `tc` and `ip`
- Background: mrncciew.com CWNA, CWAP and CCIE Wireless study notes
- IEEE 802.11-2020 for the frame and element rules referred to

Numbers marked measured come from the tools in this session; numbers marked modelled come from the stated model; the kernel and OpenWrt details not run here are from the documentation or from memory.
