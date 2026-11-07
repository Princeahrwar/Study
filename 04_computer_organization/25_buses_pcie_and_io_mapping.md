# CO 25: Buses, PCIe and I/O Mapping

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 4 hours (2.5 reading, 1.5 problems) · You need: [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (section 8: memory types, and why device registers are uncached), [tutorial 15](15_virtual_memory_and_the_tlb.md) (virtual versus physical addresses), [tutorial 03](03_boolean_logic_and_combinational_circuits.md) (decoders and tri-state buffers) and [tutorial 17](17_storage_and_reliability.md) (section 4: NVMe queues and doorbells)*

By the end of this tutorial you can:

- describe a shared bus (address, data and control lines), tell synchronous from asynchronous and split-transaction buses, and say why wide parallel buses gave way to serial point-to-point links;
- order requests under fixed-priority (daisy-chain) and round-robin arbitration, say which one starves a device, and trace I2C's wired-AND arbitration bit by bit;
- say what open-drain means, why it needs a pull-up resistor, and compute the range of resistor values that works;
- tell memory-mapped from port-mapped I/O, decode an address by hand, and explain what a load or store to a device register really does (uncached, posted write, round-trip read, read-back flush);
- compute the bandwidth of a PCIe link from its generation and width, and the share lost to packet headers at different transfer sizes;
- describe PCIe's topology (root complex, switches, endpoints, bus:device.function) and its three layers, and explain why NIC drivers batch their doorbells;
- read a PCI configuration space: header fields, capability list, base address registers (BARs) with the size probe, the ECAM address formula, and what enumeration does;
- explain MSI-X and DMA as nothing more than memory writes and reads issued by the device;
- explain what an IOMMU does and costs, how SR-IOV and device passthrough share one device, and what CXL adds, in a paragraph;
- recognise I2C, SPI, USB and AXI, and the mistakes each invites.

Tutorials 13 to 17 followed a load from the core to DRAM and to storage. This tutorial follows the other half of a driver's life: a load or a store whose address belongs to a *device*. Every NIC, GPU, NVMe drive, Wi-Fi radio and sensor you will meet in an interview is reached the same way: the CPU stores to an address that the hardware decodes to the device instead of DRAM, and the device reaches RAM by issuing memory requests of its own. The names change (PCIe, AXI, I2C) but the questions do not: how does the CPU find the device, how long does touching it take, how does the device get at memory, and who stops it from reaching the wrong memory?

**Where the numbers come from.** The virtual machine used for tutorials 14 to 24 is a Firecracker micro-VM with **ten virtio devices attached as PCI devices behind a PCI Express ECAM window** (section 4.2), so the *structure* in this tutorial (config space, BARs, capability lists, MSI-X, `/proc/iomem`) is real output from a running Linux kernel, shown as such. What this VM cannot show: it has **no real PCIe link** (so no lane or speed to read), **no IOMMU hardware**, and this container lacks the privileges to map a BAR (no `/dev/mem`, no `resourceN` files, no `CAP_SYS_RAWIO`). Four kinds of evidence are therefore used. (1) **Arithmetic**, with the formulas shown (link bandwidth, packet overhead, pull-up resistors, address decoding), checked by short Python scripts. (2) **Measurements here**: the cost of a PCI configuration-space read (36 to 39 µs, a VM exit) against an ordinary load, with the caveat that this is the cost of a *virtual* device. (3) **Published measurements on real hardware**, linked where used: Neugebauer et al., ["Understanding PCIe performance for end host networking"](https://www.cl.cam.ac.uk/research/srg/netos/projects/pcie-bench/neugebauer2018understanding.pdf) (SIGCOMM 2018) for TLP overhead, PCIe latency and IOMMU misses, and Markuze, Morrison and Tsafrir, ["True IOMMU Protection from DMA Attacks"](https://www.cs.tau.ac.il/%7Emad/publications/asplos2016-iommu.pdf) (ASPLOS 2016) for IOMMU cost. (4) **Specifications and kernel documentation**, linked where used. Nothing here was timed on a physical PCIe link.

> **The one-sentence model.** To the CPU a device is a range of physical addresses: a load or store to that range is decoded to the device (memory-mapped I/O) and, on PCIe, becomes a packet on a point-to-point serial link, where a **write is posted** (fire and forget, cheap) and a **read is a round trip** (about a microsecond on real hardware, dozens of microseconds in a VM); the device is found by **enumeration**, which reads each function's configuration space and gives its **BARs** a place in the address map; the device reaches memory by issuing memory-read and memory-write packets of its own (**DMA**, and an interrupt is just such a write), which an **IOMMU** can translate and confine; and the link's useful bandwidth is lanes × rate × encoding, minus a header cost that is tiny for 4 KB transfers and crushing for 4-byte ones.

---

## 1. Buses: how parts share wires

### 1.1 The classic shared bus

A **bus** is a set of wires shared by several devices. Three groups of wires matter:

- **address lines** say *which* location (and so, by decoding, which device);
- **data lines** carry the value;
- **control lines** say what is happening (read or write, "address is valid", "I am ready", "I want the bus").

One read on a simple synchronous bus, the way textbooks draw it:

| Cycle | What happens |
|---|---|
| 1 | The **master** (the CPU, or a DMA engine) puts the address on the address lines and asserts READ. |
| 2 | Each device's decoder (tutorial 03) compares the address with its own range; exactly one device is selected. If it is slow it holds a WAIT line and the master waits. |
| 3 | The selected device drives the data lines (all the others keep their outputs *high-impedance*, tutorial 03) and the master latches the value. |

The peak bandwidth is simply **width × clock**. Conventional PCI was 32 bits at 33 MHz: `4 B × 33.3 MHz = 133 MB/s`, **shared by every device on the bus**, because only one can talk at a time. A 64-bit, 66 MHz variant reached 533 MB/s. A fast modern NVMe drive moves more than 50 times that.

### 1.2 Synchronous, asynchronous, and split transactions

- **Synchronous:** every device follows one clock and a transfer takes a known number of cycles. Fast and simple, but the clock must suit the slowest device on the bus, and wire length limits it (section 1.3). Slow devices add **wait states**.
- **Asynchronous:** no shared clock; request and acknowledge wires form a handshake ("request, acknowledge, release request, release acknowledge"). Speed adapts to each device, at the price of more wires and logic. The idea survives wherever two sides run on different clocks, and in I2C's *clock stretching* (section 9).
- **Split transaction:** the master sends a request and *releases the bus*; the device replies later as a separate transaction. This matters whenever the device takes a long time to produce the first word. Example: a 64-byte burst on a 64-bit bus at 100 MHz needs 8 cycles (80 ns) of data transfer, and the memory needs 100 ns to find the first word. If the bus is held throughout, one burst takes 180 ns: `64 B ÷ 180 ns = 355 MB/s`. If the request takes 1 cycle (10 ns) and the bus is free during the wait, the bus is busy only 90 ns per burst, so overlapped requests can reach `64 B ÷ 90 ns = 711 MB/s`. **PCIe is split-transaction by construction**: a read is a request packet now and a completion packet later (section 3.4).

### 1.3 Why wide parallel buses lost

Parallel buses ran into three walls.

1. **Skew.** A 32-bit bus needs all 32 signals to arrive within a fraction of one clock period. A signal on a circuit-board trace travels roughly 15 cm per nanosecond, so a few millimetres of difference in trace length is a visible part of a 1 ns clock. Faster clocks shrink the skew budget until it vanishes.
2. **Loading and crosstalk.** Every device on a shared bus adds capacitance, which slows the edges; neighbouring wires interfere.
3. **Pins and board area.** Two hundred wires for one slot.

The fix was to give each device its **own point-to-point link**, carry bits on **one pair of wires per direction** with the clock recovered from the data itself (so there is no clock skew to manage), and add bandwidth by adding links ("lanes") rather than clock rate. The price is a **protocol**: bits must be framed into packets, encoded so the receiver can recover the clock, and checked for errors. That is PCI Express (section 3). DRAM kept a parallel bus (tutorial 16) because the wires are a few centimetres long and latency matters more than pin count.

### 1.4 Who may talk? Arbitration

When more than one device can be a master (the CPU, a DMA engine, a NIC), something must decide who drives the bus next. There are three structures.

**Daisy chain.** One *grant* line runs through the devices in order. A device that wants the bus swallows the grant; one that does not passes it on. Priority is simply **position**. It is cheap (one request line, one grant line, however many devices) and simple, but a hungry device near the front can **starve** the others, the grant ripples through every device (slower as the chain grows), and one dead device breaks the chain.

**Centralised parallel arbitration.** Each device has its own request and grant wire to a single **arbiter**, which can apply any policy: fixed priority, round-robin, weighted, oldest first. More wires, but the policy is in one place.

**Distributed arbitration.** No arbiter; every device watches the shared wires and decides for itself. Either each puts a priority code on arbitration lines and drops out when it sees a higher one, or devices transmit, detect a collision, and back off (Ethernet's old CSMA/CD). **I2C and CAN use a wired-AND trick** (section 1.5).

The two policies you must be able to compare are **fixed priority** (the urgent device gets minimum latency; low-priority devices can starve) and **round-robin** (each requester waits for at most N − 1 grants; the delay is spread fairly). A good arbiter also *hides* arbitration by deciding the next owner during the current transfer, so the bus is never idle between owners.

*Example.* Four masters D0 to D3, D0 the highest priority. Each has a backlog of one-cycle transfers: D0 has 3, D1 has 1, D2 has 2, D3 has 2, and all request from cycle 1. (Checked by a short script.)

| Policy | Grant order, cycle 1 to 8 | Cycle at which each finishes (D0, D1, D2, D3) | First grant of D3 |
|---|---|---|---|
| Fixed priority | D0 D0 D0 D1 D2 D2 D3 D3 | 3, 4, 6, 8 | cycle 7 |
| Round-robin (start at D0) | D0 D1 D2 D3 D0 D2 D3 D0 | 8, 2, 6, 7 | cycle 4 |

The average finishing time barely changes (5.25 against 5.75 cycles); what changes is *who* waits. Now suppose D0, D2 and D3 request **forever**. Under fixed priority D0 wins every cycle and D2 and D3 **never** get the bus. Under round-robin the order is D0 D2 D3 D0 D2 D3 and each gets one grant in three. Starvation is a property of the policy, not of the load.

You will meet arbitration again wherever there is a shared resource: PCIe switches arbitrate between ingress ports for an egress port; the on-chip interconnect (AXI, section 9) arbitrates between masters at each slave; the DRAM controller arbitrates between requests (tutorial 16, FR-FCFS); and a software **ticket lock** is round-robin while a test-and-set lock is "whoever wins the race" (tutorial 19).

### 1.5 Tri-state, open-drain and the wired-AND

Tutorial 03 introduced the **tri-state buffer**: a driver that can output 0, 1 or *disconnected*, so several drivers can share a wire if software or the arbiter guarantees that only one is enabled. Two enabled with opposite values is **contention**, a near short circuit.

There is a second way to share one wire that cannot cause contention. An **open-drain** (or open-collector) output has only two states: *pull the line to 0* or *let go*. A **pull-up resistor** to the supply pulls the line to 1 when nobody pulls down. The line is therefore 0 if **any** device pulls it low, and 1 only if **all** let go: the **wired-AND** of the devices' outputs. A 0 and a "let go" simply give 0, so nothing is damaged however the devices behave. This is how I2C's two wires work, and how many shared interrupt-request and reset lines work.

**The price is speed.** Only the *falling* edge is driven; the rising edge is the line capacitance `C` charging through the pull-up `R`, with voltage `V(t) = V_DD (1 − e^(−t/RC))`. The I2C specification measures rise time between 30 % and 70 % of `V_DD`, which is `RC × ln(0.7/0.3) = 0.8473 RC`. That gives a **window** for `R`:

- **Lower limit (a driver must be able to pull the line low):** `R ≥ (V_DD − V_OL) ÷ I_OL`. The I2C specification gives V_OL = 0.4 V at I_OL = 3 mA.
- **Upper limit (the rising edge must be fast enough):** `R ≤ t_r(max) ÷ (0.8473 × C_b)`, where `C_b` is the bus capacitance. Fast-mode I2C (400 kbit/s) allows t_r ≤ 300 ns and C_b ≤ 400 pF; Standard-mode (100 kbit/s) allows 1,000 ns ([I2C specification](https://www.csd.uoc.gr/~hy428/reading/i2c_spec.pdf), tables of timing and rise time).

| Case | Lower limit | Upper limit | Verdict |
|---|---|---|---|
| Fast-mode, 3.3 V, 100 pF | 967 Ω | 3,541 Ω | use 1.5 kΩ to 2.2 kΩ |
| Fast-mode, 3.3 V, 200 pF | 967 Ω | 1,770 Ω | narrow: about 1 kΩ to 1.5 kΩ |
| Fast-mode, 3.3 V, 400 pF | 967 Ω | **885 Ω** | **no resistor works** |
| Standard-mode, 3.3 V, 400 pF | 967 Ω | 2,951 Ω | fine |
| Fast-mode, 5 V, 100 pF | 1,533 Ω | 3,541 Ω | fine |

The row with the empty window is the practical lesson: a long cable or many devices raise `C_b`, and the pull-up that can still pull the line low is too weak to charge the wire in time. The specification's answer there is a current source instead of a resistor, or a slower mode, or a shorter bus. It is also why open-drain buses top out at a few megahertz while push-pull links (SPI, section 9) run at tens.

**I2C arbitration, worked.** Two masters start at the same moment and each sends its 7-bit target address, most significant bit first, *while reading back the line*. A sends `0x2A = 0101010`, B sends `0x29 = 0101001`.

```
bit sent:    1st 2nd 3rd 4th 5th 6th 7th
A (0x2A):     0   1   0   1   0   1   0
B (0x29):     0   1   0   1   0   0   1
line (AND):   0   1   0   1   0   0   .     <- on the 6th bit A releases (1), B pulls down (0)
```

On the sixth bit A tries to send 1, but B holds the line at 0. A reads back 0, sees that it differs from what it sent, **stops driving** and waits for the bus to be free. B read back exactly what it sent, so it never noticed anything, and **its message is not damaged**. The rule falls out of the wiring: **a 0 beats a 1, so the lower address wins**. Arbitration costs no extra wires and no time.

---

## 2. Talking to a device: registers and address maps

### 2.1 What a device looks like to software

To a programmer a device is a small block of **registers**. A minimal UART might have:

| Offset | Name | Access | Meaning |
|---|---|---|---|
| 0x00 | DATA | read/write | write = byte to send; read = next received byte (pops a FIFO) |
| 0x04 | STATUS | read | bit 0: a received byte is waiting; bit 1: transmitter empty; bit 2: overrun (write 1 to clear) |
| 0x08 | CONTROL | read/write | bit 0: enable; bit 1: interrupt on receive |
| 0x0C | BAUD | read/write | clock divisor |

```c
#define UART ((volatile uint32_t *)0x40000000)       /* the device's base address */
enum { DATA, STATUS, CONTROL, BAUD };                /* register index = offset / 4 */

void uart_putc(char c) {
    while (!(UART[STATUS] & TX_EMPTY)) { }           /* poll: one device read per loop */
    UART[DATA] = c;                                  /* one device write: the byte leaves */
}
```

Registers are **not** memory. A read can have a side effect (reading DATA pops the FIFO; reading an interrupt-status register may clear it); a write can have a side effect too (writing DATA starts a transmission); some bits are *write-1-to-clear*; some are read-only; the width of the access may matter (a byte read of a 32-bit register can behave differently). `volatile` only stops the *compiler* from caching or reordering the access; the **memory type** (uncached, tutorial 14) is what stops the *hardware* from doing so.

### 2.2 Two ways to address a register

- **Port-mapped I/O.** Devices live in a **separate I/O address space** reached with special instructions. On x86 that is `IN` and `OUT` with a 16-bit port number: **65,536 ports**, and a privilege check per port. Legacy PC devices still sit there: the serial port at `0x3F8`, the interrupt controller at `0x20`, the PCI configuration mechanism at `0xCF8`/`0xCFC` (section 4.2).
- **Memory-mapped I/O (MMIO).** Devices occupy ranges of the ordinary **physical address space**, and the program uses ordinary loads and stores. **Arm and RISC-V have no port space at all**, and x86 devices today are almost all MMIO.

MMIO won because it needs no special instructions (any addressing mode, any C pointer works), it uses the whole 64-bit address space instead of 64 K ports, and the MMU can give a driver, or a user process, access to exactly the pages of one device (tutorial 15).

This VM shows both on one screen (`/proc/ioports` and `/proc/iomem`, trimmed):

```
$ cat /proc/ioports                              $ cat /proc/iomem
0000-0cf7 : PCI Bus 0000:00                      00100000-bfffffff : System RAM
  0020-0021 : pic1                               c0001000-eebfffff : PCI Bus 0000:00
  0060-0060 : keyboard                           eec00000-febfffff : Reserved
  03f8-03ff : serial                               eec00000-eecfffff : PCI ECAM 0000 [bus 00-00]
0cf8-0cff : PCI conf1                            fec00000-fec003ff : IOAPIC 0
                                                 100000000-23fffffff : System RAM
                                                 4000000000-7fffffffff : PCI Bus 0000:00
                                                   4000000000-400007ffff : 0000:00:01.0
                                                     4000000000-400007ffff : virtio-pci-modern
```

Read the right-hand column as a map of the physical address space: RAM, then a window for PCI devices below 4 GiB, the ECAM window, the interrupt controller, **RAM again above 4 GiB** (the two RAM ranges add up to 8 GiB, split around the device window), and a second, 64-bit window at `0x4000000000` where the devices' BARs actually live.

### 2.3 Address decoding

The CPU puts a **physical address** on the interconnect (after translation, tutorial 15). Something has to decide which RAM chip or device answers. Each target has a **decoder** (tutorial 03) that compares the *high* address bits with its own base; the low bits then select a register inside it. Take this map:

| Region | Base | Size | Selected when address bits are |
|---|---|---|---|
| RAM | `0x0000_0000` | 512 MiB | `[31:29] == 000` |
| Boot ROM | `0x2000_0000` | 64 KiB | `[31:16] == 0x2000` |
| UART | `0x4000_0000` | 4 KiB | `[31:12] == 0x40000` |
| GPIO | `0x4000_1000` | 4 KiB | `[31:12] == 0x40001` |

For the UART the register is picked by `address[11:2]` (one 32-bit register per word). This is why bases and sizes are **powers of two, aligned to their size**: the compare is a handful of gates. The two bugs to recognise:

- **Overlap:** two devices both match an address and both drive the data lines. Reads return garbage or burn the output drivers.
- **Hole:** nothing matches. A simple bus hangs or raises a bus error. **PCI returns all ones** for a read that nobody claims and discards writes, which is exactly why a vendor ID of `0xFFFF` means "no device here" during enumeration (section 4.3).

### 2.4 What an MMIO load and a store really do

The page holding a device register is mapped **uncached** (tutorial 14, section 8: no caching, no combining, no speculation, strongly ordered), so every access leaves the core and travels through the uncore to the device. On PCIe it becomes a packet (section 3.4). The two directions behave very differently.

- **A store is posted.** The core hands it to the write path and carries on; no reply ever comes back. The CPU does not know when, or even whether, the device has received it.
- **A load is a round trip.** The request goes out, the device answers with a completion packet carrying the data, and the core cannot use the value until it arrives. Because the access is uncached and must not be speculated or reordered, the core cannot run ahead of it as it would past a cache miss (tutorial 11).

How slow? The same machine, five rows (my measurements here, except the one marked as published):

| Access | Cost |
|---|---|
| Load that hits L1 | 0.4 to 0.6 ns |
| Load that misses to DRAM (256 MB pointer chase) | 146 to 149 ns |
| A PCIe device reading 64 B of *host* memory, median, bare metal ([Neugebauer et al.](https://www.cl.cam.ac.uk/research/srg/netos/projects/pcie-bench/neugebauer2018understanding.pdf), section 6.2) | 547 ns (a CPU reading a device register is the same round trip in the other direction, so expect the same order: about a microsecond or less) |
| Read of PCI config space through `/sys` in **this VM** | **36 to 39 µs** |
| The same `pread` on another sysfs file that touches no device | 0.33 to 0.51 µs |

I could not map a BAR here (section "Where the numbers come from"), so I timed the other device-register-class access I could reach: a 4-byte read of a device's PCI configuration space, in a loop of 100,000 `pread` calls on `/sys/bus/pci/devices/0000:00:02.0/config`, against the same call on the neighbouring `vendor` attribute, which runs the same system call and sysfs path without touching the device. The difference, **about 36 to 38 µs per access**, is the device access itself. In this VM the likely reason is that the configuration read is an ECAM access (section 4.2) that traps to the hypervisor's emulation of the PCI device, a **VM exit** ([tutorial 27](27_hardware_virtualization.md)); on bare metal it would be on the order of a microsecond or less. The lesson holds on both: **a device read is thousands of times slower than a cached load**, which is why drivers avoid polling registers in hot loops. To repeat the measurement anywhere: a loop of `pread(fd, &v, 4, 0)` on a device's `config` file against the same loop on its `vendor` file, timed with `clock_gettime` (needs a Linux machine with PCI devices; on bare metal the config read is a few microseconds at most).

**The read-back flush.** A posted write may still be in flight when the next instruction runs. PCI's ordering rules say a read cannot pass an earlier posted write on the way to the same device, so the standard trick (documented in the kernel's [device I/O guide](https://www.kernel.org/doc/html/latest/driver-api/device-io.html)) is to read a register back after the write:

```c
writel(IRQ_DISABLE, regs + INT_MASK);   /* posted: may still be on its way */
(void)readl(regs + INT_MASK);           /* returns only after the write has reached the device */
/* now it is safe to free the data the interrupt handler uses */
```

On Linux `readl`/`writel` are ordered with respect to each other and `writel` orders earlier ordinary stores before itself (that is why filling a descriptor in RAM and then calling `writel` on a doorbell works); the `_relaxed` forms drop those guarantees and are for code that has justified it. `ioremap()` gives the uncached mapping for registers and `ioremap_wc()` the write-combining one for frame buffers and similar (tutorial 14).

### 2.5 The doorbell pattern

Put together, a high-speed device is driven like this: the CPU writes work descriptors into a **queue in ordinary RAM**, then does **one posted MMIO write**, the **doorbell**, telling the device that new work is there. The device fetches the descriptors by DMA, and reports completion by writing to another queue in RAM and raising an interrupt (tutorial 17 did this for NVMe; [tutorial 26](26_interrupts_exceptions_and_dma.md) does it for a NIC). Registers are touched rarely, and *never read in the fast path*. Section 3.7 puts numbers on why the doorbell and the interrupt are shared across many packets.

---

## 3. PCI Express

### 3.1 From a shared bus to a tree of links

PCI Express (2003) replaced PCI's shared bus with **point-to-point links**. A **lane** is two differential wire pairs, one for each direction, so every lane is full duplex (four wires). A link has 1, 2, 4, 8 or 16 lanes (written x1 to x16); bytes are striped across the lanes, and the clock is **recovered from the data stream** at the receiver, which is why the bits must be encoded so that the signal keeps changing (section 3.2). Each device has its own link, so the devices no longer compete for one bus; they compete only where links merge, at a switch or at the CPU.

### 3.2 Speed per generation

The headline number of each generation is a **transfer rate** in giga-transfers per second (GT/s), per lane, per direction. What you can use is smaller by the line code:

> **GB/s per lane, one direction = GT/s × (payload bits ÷ coded bits) ÷ 8**

| Generation | Year | GT/s | Line code | Per lane (GB/s) | x4 | x16 |
|---|---|---|---|---|---|---|
| Gen 1 | 2003 | 2.5 | 8b/10b (20 % overhead) | 0.250 | 1.00 | 4.00 |
| Gen 2 | 2007 | 5 | 8b/10b | 0.500 | 2.00 | 8.00 |
| Gen 3 | 2010 | 8 | 128b/130b (1.5 %) | 0.985 | 3.94 | 15.75 |
| Gen 4 | 2017 | 16 | 128b/130b | 1.969 | 7.88 | 31.5 |
| Gen 5 | 2019 | 32 | 128b/130b | 3.938 | 15.75 | 63.0 |
| Gen 6 | 2022 | 64 | PAM4, 256-byte FLITs (242 usable) | 7.563 | 30.25 | 121 |
| Gen 7 | 2025 | 128 | PAM4, FLITs | 15.125 | 60.5 | 242 |

(Years and the FLIT figure from the [Wikipedia generation table](https://en.wikipedia.org/wiki/PCI_Express); the arithmetic is mine and was checked by a script.) Three points to keep straight.

1. **GT/s is not Gb/s of data.** Gen 1 and 2 spend 2 of every 10 bits on the 8b/10b code that keeps the clock recoverable. Gen 3 onward uses 128b/130b and loses 1.5 %. Gen 3's raw rate is only 1.6 times Gen 2's (8 against 5 GT/s), but its usable bandwidth is 1.97 times, because the code got cheaper.
2. **Gen 6 changes the signalling.** PAM4 carries 2 bits per symbol, so the bit rate doubles to 64 Gb/s without raising the symbol rate; the higher error rate is handled by lightweight forward error correction and a CRC inside fixed-size **FLITs** (flow-control units). PCI-SIG's headline "256 GB/s for x16" ([press summary](https://insidehpc.com/2022/01/pci-sig-releases-pcie-6-0-specification)) counts *both directions* at the raw rate; one direction after FLIT overhead is the 121 GB/s in the table. Gen 7 doubles it again ([released June 2025](https://liliputing.com/pcie-7-0-specification-released-with-up-to-128-gt-s-transfer-speeds/)).
3. **One direction versus both.** Tutorial 23 had this: PCIe Gen 5 x16 is 63 GB/s each way and vendors quote 128 GB/s for the sum. A flow that only reads from a device or only writes to it sees one direction.

**Width and speed are negotiated.** At power-up the two ends of a link train and agree on the highest speed and the widest width both support. A x16 card in a slot that is wired x4 runs at x4: **a quarter of the bandwidth**, silently; a Gen 4 card in a Gen 3 slot runs at Gen 3 speed. Linux logs a warning when a device's link is narrower or slower than it could be (look for "available PCIe bandwidth, limited by" in `dmesg`), and `lspci -vv` shows `LnkCap` (what the device can do) and `LnkSta` (what it is doing). **Bifurcation** lets firmware split one x16 slot into x8 + x8 or four x4 links, which is how one slot hosts several NVMe drives. A CPU has a fixed number of lanes, so lanes are a budget that GPUs, NICs and drives compete for.

### 3.3 Topology

A PCIe system is a **tree**:

- the **root complex** is in the CPU package. It connects the cores and memory to the PCIe fabric and contains the **root ports**, one per downstream link;
- a **switch** fans one upstream link out to several downstream ones (inside, it is a set of virtual bridges);
- an **endpoint** is a device that terminates the tree (GPU, NIC, SSD, Wi-Fi radio);
- a **bridge** connects to legacy PCI.

Every function is named **bus:device.function** (8 + 5 + 3 bits = 16 bits, the **routing ID**), preceded by a 16-bit *domain* (segment) on machines with several root complexes, as in `0000:03:00.0`. An illustrative server (not this VM, which has one flat bus):

```
CPU ── root complex ─┬─ root port 00:01.0 ── x16 ── GPU        01:00.0
                     ├─ root port 00:02.0 ── x8  ── NIC        02:00.0
                     └─ root port 00:03.0 ── x8  ── switch     03:00.0 ─┬─ x4 ── NVMe  04:00.0
                                                                        └─ x4 ── NVMe  05:00.0
```

Two things follow from the shape. Traffic between the two NVMe drives can turn around inside the switch without touching the CPU (peer-to-peer, section 5), and both drives *share* the x8 upstream link, so two x4 drives at full speed fit exactly. And a device hangs off **one socket's** lanes, so DMA to memory on the other socket crosses the interconnect (tutorial 16, NUMA). `lspci -tv` prints this tree.

### 3.4 Three layers, and what a packet is

PCIe is a small network stack.

| Layer | Job |
|---|---|
| **Transaction layer** | Builds and consumes **TLPs** (transaction layer packets): memory read and write, I/O, configuration, messages, completions. Flow-control **credits** live here: the receiver advertises buffer space per packet type so the sender never overruns it (how big that buffer must be: [tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) section 4.3). |
| **Data link layer** | Adds a 12-bit **sequence number** and a 32-bit **LCRC** to each TLP. The receiver answers with ACK or NAK packets (DLLPs); the sender keeps a **replay buffer** and resends on a NAK. Bit errors on the wire are repaired here, invisibly to software. |
| **Physical layer** | Framing, scrambling, encoding (section 3.2), striping across lanes, and **link training** (negotiating speed and width). |

The kinds of request you must know:

| Request | Posted? | Reply | Used for |
|---|---|---|---|
| **Memory write** | posted | none | doorbells, DMA writes to host RAM, **MSI-X interrupts** |
| **Memory read** | non-posted | completion with data (may arrive in several pieces) | register reads, DMA reads of descriptors and data |
| **I/O read / write** | non-posted | completion | legacy devices |
| **Configuration read / write** | non-posted | completion | enumeration, reading capabilities (section 4) |
| **Message** | posted | none | legacy interrupt emulation, power management, error reports |

Packets are **routed** three ways: memory and I/O requests by **address** (each bridge compares it with its windows, section 4.4); configuration requests and completions by **ID** (a completion finds its way back to the requester's bus:device.function); a few messages implicitly. Ordering rules keep the pieces coherent, the one to remember being that **a non-posted read may not pass earlier posted writes**, which is what makes the read-back flush of section 2.4 work.

A TLP header is 3 or 4 doublewords (12 B for 32-bit addresses, 16 B for 64-bit). Three sizes shape performance: **MPS**, the Max Payload Size of one TLP (128 B to 4 KB, configured by software to a value every device on the path supports; the paper below finds 256 B or 512 B typical); **MRRS**, the Max Read Request Size; and the **RCB**, the Read Completion Boundary (usually 64 B), at which a completion may be split.

### 3.5 What the headers cost

Neugebauer et al. model the bytes a TLP occupies on a Gen 3 link (their section 3): **2 B** of framing, **6 B** of data-link overhead (sequence number and LCRC), **4 B** of common TLP header and a request-specific header, which comes to **24 B for a memory write or a memory read request** (64-bit addresses) and **20 B for a completion with data**. The payload is cut into `ceil(size ÷ MPS)` TLPs, so:

> **write efficiency = size ÷ (size + 24 × ceil(size ÷ MPS))**

| Payload | MPS 128 | MPS 256 | MPS 512 |
|---|---|---|---|
| 4 B (a doorbell or pointer) | 14.3 % | 14.3 % | 14.3 % |
| 16 B (one descriptor) | 40.0 % | 40.0 % | 40.0 % |
| 64 B | 72.7 % | 72.7 % | 72.7 % |
| 128 B | 84.2 % | 84.2 % | 84.2 % |
| 256 B | 84.2 % | 91.4 % | 91.4 % |
| 4,096 B | 84.2 % | 91.4 % | 95.5 % |

Now chain the losses for **Gen 3 x8**. Raw: `8 GT/s × 8 lanes × 128/130 = 63.0 Gb/s`. ACK/NAK and flow-control packets cost the data-link layer about 8 %, leaving the paper's **57.88 Gb/s** for TLPs. A stream of 64-byte writes then delivers `57.88 × 72.7 % = 42.1 Gb/s` (5.3 GB/s) and a stream of 4 KB writes with MPS 256 delivers `57.88 × 91.4 % = 52.9 Gb/s` (6.6 GB/s). The label said 7.88 GB/s. So **a PCIe link never delivers more than about 84 % of its headline rate, even for large transfers, and small transfers are far worse**. A 4-byte register update occupies 28 bytes of link, 14 % efficient. (The model leaves out the RCB, optional end-to-end CRC and some flow-control detail, so treat it as an upper bound.) The same chain for the **Gen 4 x16** link that tutorial 22 used gives `31.5 GB/s × 0.92 × 0.914 ≈ 26.5 GB/s` (assuming the same 8 % link-layer overhead), which is why "about 25 GB/s in practice" is a sound planning figure for a GPU copy.

### 3.6 Latency

Bandwidth is the width of the pipe; latency is its length. Measured on real hardware by the same paper (sections 2 and 6, bare metal, Xeon hosts):

| What | Value |
|---|---|
| 64 B DMA read, Xeon E5-2637 v3, median | **547 ns** (minimum 520 ns) |
| The same on a Xeon E3 system | median 1,213 ns, p99 5.7 µs |
| A kernel-bypass NIC's 128 B loopback, total | about 1,000 ns, **about 900 ns of it PCIe** (over 90 % for small packets) |
| Reading memory attached to the *other* socket | about +100 ns |
| Data hitting in the last-level cache instead of DRAM (DDIO) | about −70 ns |
| An IOMMU translation miss (section 6) | about **+330 ns** |

The consequence is Little's law (tutorial 16): `requests in flight = rate × latency`. To read 64 B blocks at one every 30 ns (the arrival rate of 128 B packets at 40 Gb/s) with 547 ns latency you need `547 ÷ 30 = 18` reads in flight. That is why devices are built to have many requests outstanding, and why *one dependent PCIe read at a time* is slow.

### 3.7 A NIC through PCIe: why drivers batch

The same paper models a simple NIC. For **each received packet** the following crosses the link:

| Step | Direction | Link bytes |
|---|---|---|
| Packet written to host RAM | device → host | `size + 24 × ceil(size ÷ 256)` |
| 16 B receive descriptor written back | device → host | 40 |
| Interrupt (a 4 B MSI-X write) | device → host | 28 |
| Free-list descriptor fetched (16 B) | request 24 up, completion 36 down | 60 |
| Head pointer read (4 B) | request 24 up, completion 24 down | 48 |
| Driver writes the tail pointer (4 B) | host → device | 28 |

Add the *upstream* column for a 64 B packet and you get 204 bytes of link for 64 bytes of data: **31 % efficient**, which on a Gen 3 x8 link (57.88 Gb/s per direction at the TLP layer) is 18.2 Gb/s or **35.5 Mpps**. A 40 GbE port at 64 B frames needs 59.5 Mpps. Now let the driver and NIC amortise the per-batch steps (the doorbell, the pointer read, the interrupt, the TLP headers on the descriptors) over `N` packets while the descriptors' 16 bytes each still move:

| Packet size | Batch 1 | Batch 16 | Batch 64 |
|---|---|---|---|
| 64 B | 31 % · 18.2 Gb/s · **35.5 Mpps** | 58 % · 33.6 Gb/s · **65.6 Mpps** | 61 % · 35.1 Gb/s · **68.5 Mpps** |
| 256 B | 65 % · 37.4 Gb/s · 18.3 Mpps | 85 % · 49.0 Gb/s · 23.9 Mpps | 86 % · 49.8 Gb/s · 24.3 Mpps |
| 1,500 B | 85 % · 49.3 Gb/s · 4.1 Mpps | 90 % · 52.1 Gb/s · 4.3 Mpps | 90 % · 52.3 Gb/s · 4.4 Mpps |

(My arithmetic on the paper's per-packet accesses, receive path only, checked by a script; the paper reports that its simple model reaches 40 Gb/s only for frames of roughly 512 B and up, which this reproduces: 43.8 Gb/s at 512 B with no batching.) Read it three ways.

- **Small packets are the problem.** A batch of 8 is the smallest power of two that lifts 64 B frames past the 59.5 Mpps a 40 GbE port needs (62.1 Mpps); one packet per doorbell cannot. This is why NIC drivers use descriptor batches, interrupt coalescing and tail-pointer updates every few packets ([tutorial 26](26_interrupts_exceptions_and_dma.md)).
- **Large packets don't care.** At 1,500 B the link is 85 to 90 % efficient whatever the batching.
- **There is a ceiling.** A 100 GbE port at 64 B frames is 148.8 Mpps, beyond what a Gen 3 x8 link can carry even if you ignore every header and descriptor (`57.88 Gb/s ÷ 512 bits = 113 Mpps`). Such a NIC needs a wider or faster link (Gen 4 x16, Gen 5 x8) just for the arithmetic.


### 3.8 Link power states, retimers and errors

Three physical-layer facts that explain real-world symptoms (all *standard*; none was measurable in this VM, which has no physical links).

- **Link power states (ASPM).** A link that is idle can drop from active (**L0**) to a short standby (**L0s**, rarely used now) or to **L1**, where the transmitters are off and the exit takes microseconds; L1 sub-states (L1.1, L1.2) also gate clocks and power. The first request after an idle period therefore pays the exit latency. The device and the root port negotiate the states and report how much latency they tolerate (**LTR**, latency tolerance reporting). Symptoms seen in practice: latency spikes on a NIC or an NVMe drive that wakes from L1, and the kernel parameter `pcie_aspm=off` used as a diagnostic. `lspci -vv` shows the enabled state under `LnkCtl`. This is the power-gating trade-off of [tutorial 28](28_performance_and_power.md) section 9.7 applied to a link: deeper saves more and wakes slower.
- **Retimers.** At the Gen 5 and Gen 6 rates a signal does not travel far on a printed circuit board, so long traces, connectors and risers use a **retimer**, a chip that recovers the clock and data and sends them out fresh. It adds a little latency per hop and one more thing that must be trained correctly.
- **Errors.** The data-link layer replays a TLP whose CRC failed (section 3.4), so a bad bit costs time, not data. Linux reports them through **AER** (advanced error reporting, the extended capability of section 4.1) as *correctable* errors in `dmesg`. A steady stream of correctable errors means the signal is marginal (a bad riser, a bent pin); the link may retrain at a lower speed, and your 'Gen 4 x16' device then shows `LnkSta` at Gen 3. *Uncorrectable* errors are non-fatal or fatal and can take the device down.

---

## 4. Configuration space, enumeration and BARs

### 4.1 Every function describes itself

Each PCI function has a **configuration space**: 256 bytes in conventional PCI, 4,096 bytes in PCI Express (the extra 3,840 bytes hold *extended capabilities*). It is a small register file that answers "who are you, what do you need, what can you do". The header of an endpoint (header type 0):

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | **Vendor ID** (`0xFFFF` means "no device here") |
| 0x02 | 2 | **Device ID** |
| 0x04 | 2 | **Command**: bit 0 I/O space enable, bit 1 **memory space enable**, bit 2 **bus master enable**, bit 10 INTx disable |
| 0x06 | 2 | **Status**: bit 4 = "I have a capability list" |
| 0x08 | 1 | Revision |
| 0x09 | 3 | **Class code** (programming interface, subclass, base class): `0x020000` Ethernet, `0x010802` NVMe, `0x030000` display |
| 0x0E | 1 | **Header type**: 0 endpoint, 1 bridge; bit 7 = multi-function |
| 0x10 to 0x27 | 6 × 4 | **BAR0 to BAR5** (section 4.4) |
| 0x2C | 4 | Subsystem vendor and device ID |
| 0x34 | 1 | Pointer to the first capability |
| 0x3C | 2 | Interrupt line and pin (legacy INTx) |

Here are the first 64 bytes of a real one, the virtio balloon device `0000:00:01.0` in this VM (`/sys/bus/pci/devices/0000:00:01.0/config`):

```
00: f4 1a 45 10  06 04 10 00  01 00 ff ff  00 00 00 00      vendor 0x1af4, device 0x1045; command 0x0406; status 0x0010
10: 04 00 00 00  40 00 00 00  00 00 00 00  00 00 00 00      BAR0 = 0x00000004, BAR1 = 0x00000040
20: 00 00 00 00  00 00 00 00  00 00 00 00  f4 1a 45 10      subsystem 0x1af4:0x1045
30: 00 00 00 00  40 00 00 00  00 00 00 00  00 00 00 00      capability pointer = 0x40
```

(PCI is little-endian: `f4 1a` is `0x1af4`, the virtio vendor ID.) **Command `0x0406`** is bit 1 (memory space enabled), bit 2 (**bus master enabled**: the device may do DMA) and bit 10 (legacy INTx off, MSI-X in use). **Status `0x0010`** says a capability list exists. The class bytes `ff ff 00` are the "other" class. **BAR0 = `0x00000004` and BAR1 = `0x00000040`**: bits [2:1] = `10` mean a 64-bit memory BAR, which uses BAR1 for the upper 32 bits, so the address is `0x40 << 32 = 0x40_0000_0000`, exactly the `4000000000` that `/proc/iomem` lists for this device.

**Capabilities** are a linked list: the pointer at 0x34 gives an offset; at each offset one byte is the capability ID and the next is the offset of the following one. Common IDs: `0x01` power management, `0x05` MSI, `0x09` vendor-specific, `0x10` PCI Express, `0x11` MSI-X. This device has five vendor-specific capabilities (at 0x40, 0x50, 0x60, 0x70, 0x84) and MSI-X (at 0x98). The five virtio ones are a nice example of how a device describes its own register layout. Each says which BAR and offset holds one structure:

| virtio structure | BAR0 offset | Length |
|---|---|---|
| common configuration (features, queue setup, status) | 0x0000 | 0x38 |
| ISR status (legacy interrupt) | 0x2000 | 1 |
| device-specific configuration | 0x4000 | 0x1000 |
| **notify (the doorbell region)** | **0x6000** | 0x1000 |

and the **MSI-X capability** says: 5 vectors, enabled, vector table at BAR0 + 0x8000, pending-bit array at BAR0 + 0x48000. Five vectors are the config-change interrupt plus four queues, which is the five lines `virtio0-config`, `-inflate`, `-deflate`, `-stats`, `-reporting_vq` in `/proc/interrupts` (section 5.2). **Extended capabilities** start at offset 0x100 and form their own list (each header: 16-bit ID, 4-bit version, 12-bit next offset): AER (error reporting, `0x01`), ACS (`0x0D`), ARI (`0x0E`), ATS (`0x0F`), **SR-IOV (`0x10`)**, Resizable BAR (`0x15`), PASID (`0x1B`). The virtio functions here expose only 256 bytes (the boot log calls them "conventional PCI endpoint"); the host bridge `00:00.0` exposes the full 4,096.

A **bridge** has header type 1 instead: it replaces most BARs with a *primary*, *secondary* and *subordinate* **bus number** and with **memory, prefetchable-memory and I/O windows** (section 4.4).

### 4.2 How the CPU reaches configuration space

There are two ways. Both end as configuration read or write TLPs, which are non-posted: a config read is a round trip (36 to 39 µs here).

1. **The legacy mechanism (x86 port I/O).** Write a 32-bit address word to port `0xCF8`, then read or write the data at `0xCFC`. The word is: bit 31 = enable, bits 23 to 16 = bus, 15 to 11 = device, 10 to 8 = function, 7 to 2 = register (a dword). For `00:02.0` register 0 the word is `0x80001000`. It reaches only the first 256 bytes. `/proc/ioports` above shows it as `0cf8-0cff : PCI conf1`.
2. **ECAM, the Enhanced Configuration Access Mechanism.** The whole 4 KB of every function is mapped into one **memory window**, and a configuration access is an ordinary uncached load or store at

> **address = ECAM base + (bus << 20 | device << 15 | function << 12 | register)**

(per the [OSDev wiki](https://wiki.osdev.org/PCI_Express)): 12 bits of register offset give 4 KB per function, 20 bits of bus/device/function give 1 MiB per bus, and the full 256 buses take **256 MiB** of address space. Firmware tells the OS where the window is in the ACPI **MCFG** table, one 16-byte entry per segment: 8-byte base, 2-byte segment, 1-byte first bus, 1-byte last bus. This VM's MCFG entry (offset 44 of the table):

```
2c: 00 00 c0 ee  00 00 00 00     base address = 0x00000000_eec00000
34: 00 00  00  00                 segment 0, first bus 0, last bus 0
```

so the window is `0xeec00000` to `0xeecfffff`: one bus, 1 MiB. The kernel agrees: `PCI: ECAM [mem 0xeec00000-0xeecfffff] (base 0xeec00000) for domain 0000 [bus 00-00]`. The configuration space of `00:01.0` is therefore at `0xeec00000 + (1 << 15) = 0xeec08000`, and that of `00:0a.0` at `0xeec50000`.

### 4.3 Enumeration

Someone has to find the devices, number the buses and give every BAR an address. **Firmware does it at boot** (so that it can boot from a PCIe disk) and the OS may redo or extend it (hot-plug; on Linux `echo 1 > /sys/bus/pci/rescan`). The algorithm is a depth-first walk:

```
scan(bus):
  for device in 0..31, function in 0..7:
      id = config_read(bus, device, function, offset 0)
      if vendor(id) == 0xFFFF:  continue          # empty slot: the read came back all ones (section 2.3)
      if header_type == 0:                         # an endpoint
          probe the size of each BAR and give it an address       (section 4.4)
      else:                                        # a bridge
          bridge.primary = bus; bridge.secondary = next_unused_bus_number()
          bridge.subordinate = 0xFF                # temporary: "everything below"
          scan(bridge.secondary)                   # recurse
          bridge.subordinate = highest bus number found below
          program the bridge's memory / prefetch / I/O windows to span its children's BARs
  (skip functions 1..7 unless function 0 has the multi-function bit set)
```

The bus numbers define the tree: a configuration request or a completion, routed by ID, goes down through the bridge whose secondary to subordinate range contains the target bus. The result on Linux is the tree you see in `lspci -t` and in `/sys/bus/pci/devices` (`0000:00:01.0` is domain 0, bus 0, device 1, function 0).

### 4.4 BARs: how a device gets a place in the address map

A **base address register** is how a function says "I need a block of address space of this size and kind", and how the firmware says "here it is". The size is found with a trick:

1. clear the Memory and I/O enable bits in Command, so the probe does not claim random addresses;
2. save the BAR, write `0xFFFFFFFF` to it and read it back;
3. the device hard-wires the bits it does not decode to 0, so what comes back is a **mask**. The lowest bits are flags, not address: for a memory BAR, bit 0 is 0, bits [2:1] are the type (`00` 32-bit, `10` 64-bit) and bit 3 is *prefetchable*; for an I/O BAR bit 0 is 1;
4. clear the flag bits; **size = (~mask) + 1**;
5. write the assigned address (and re-enable decoding).

Worked examples (checked by a short script):

| Read-back (low, high) | Meaning | Size |
|---|---|---|
| `0xFFFFC000` | 32-bit memory, mask `0xFFFFC000` | `~mask + 1 = 0x4000` = **16 KiB** |
| `0xFFF80004`, `0xFFFFFFFF` | type bits `10` = 64-bit; 64-bit mask `0xFFFFFFFF_FFF80000` | `0x80000` = **512 KiB** |
| `0xFFFFF001` | bit 0 = 1: I/O BAR, mask `0xFFFFF000` | **4 KiB** of port space |
| `0xFF000008` | 32-bit, prefetchable (bit 3), mask `0xFF000000` | **16 MiB** |

The 512 KiB row is no coincidence: it is the size of each virtio BAR in this VM (`4000000000-400007ffff`). I did not run the destructive probe on a live device; the kernel did it at boot.

A BAR's size is a power of two (at least 16 bytes for memory) and its address must be **naturally aligned to its size**: the decoder only compares the high bits (section 2.3). Prefetchable memory has no read side effects, so it may be merged and cached (a GPU's memory); non-prefetchable is registers. Large BARs are why server BIOSes have "Above 4G decoding" and "Resizable BAR": a GPU that wants its whole memory visible to the CPU needs a window of 16 GiB or more, which cannot be placed below 4 GiB.

**Windows.** A bridge forwards a memory request downstream only if the address falls in its **memory window** (base and limit with **1 MiB** granularity), its **prefetchable window** (64-bit capable) or, for port I/O, its **I/O window** (4 KiB granularity). Everything else goes upstream. So the firmware must pack all BARs below a bridge into one aligned window. Packing order matters because of the alignment rule. BARs of 16 MiB, 1 MiB, 64 KiB and 16 KiB placed from `0xC000_0000`, **largest first**, take exactly their sizes (ending at `0xC111_3FFF`, window limit rounded up to `0xC11F_FFFF`). In the order 16 KiB first, the 16 MiB BAR must skip to the next 16 MiB boundary and **16,760,832 bytes are wasted**. This is Problem C.

Two Command-register bits gate everything. **Memory space enable** makes the device answer at its BAR addresses; **bus master enable** lets it issue its own requests (DMA and MSI-X). The OS leaves bus mastering off until a driver binds, which is a basic safety property: an unclaimed device cannot write to memory.

### 4.5 Read it yourself

This short script decodes a function from sysfs (it prints the fields above; I checked it on this VM's real devices and on a hand-built 4 KiB blob with a PCI Express capability, three extended capabilities and 32-bit, 64-bit prefetchable and I/O BARs, which this VM has no endpoint to show):

```python
#!/usr/bin/env python3
"""Decode a PCI function's configuration space from sysfs.   usage: python3 pcicfg.py 0000:00:01.0"""
import struct, sys

CAPS  = {0x01: "Power Management", 0x05: "MSI", 0x09: "Vendor-specific", 0x10: "PCI Express", 0x11: "MSI-X"}
ECAPS = {0x01: "AER", 0x03: "Serial Number", 0x0B: "Vendor-specific", 0x0D: "ACS", 0x0E: "ARI",
         0x0F: "ATS", 0x10: "SR-IOV", 0x13: "PRI", 0x15: "Resizable BAR", 0x1B: "PASID"}

def decode(b):
    ven, dev, cmd, st = struct.unpack_from("<HHHH", b, 0)
    cls = b[0x0B] << 16 | b[0x0A] << 8 | b[0x09]
    print(f"vendor {ven:#06x}  device {dev:#06x}  class {cls:#08x}  header type {b[0x0E] & 0x7f}")
    print(f"command {cmd:#06x}: memory-enable={cmd >> 1 & 1} bus-master={cmd >> 2 & 1} intx-disable={cmd >> 10 & 1}")
    if b[0x0E] & 0x7f == 0:                                    # endpoint: BAR0..BAR5
        i = 0
        while i < 6:
            v = struct.unpack_from("<I", b, 0x10 + 4 * i)[0]
            if v & 1:       print(f"BAR{i}: I/O port {v & ~3:#x}")
            elif v:
                is64 = (v >> 1 & 3) == 2
                hi = struct.unpack_from("<I", b, 0x14 + 4 * i)[0] if is64 else 0
                print(f"BAR{i}: memory {hi << 32 | v & ~0xF:#x}  {'64' if is64 else '32'}-bit  prefetchable={v >> 3 & 1}")
                i += is64
            i += 1
    if st & 0x10:                                              # capability list
        p = b[0x34]
        while p:
            print(f"  cap {p:#04x}: {CAPS.get(b[p], hex(b[p]))}"); p = b[p + 1]
    p = 0x100 if len(b) > 0x100 else 0                         # extended capabilities
    while p:
        h = struct.unpack_from("<I", b, p)[0]
        if h in (0, 0xFFFFFFFF): break
        print(f"  ext cap {p:#05x}: {ECAPS.get(h & 0xFFFF, hex(h & 0xFFFF))}"); p = h >> 20

if __name__ == "__main__":
    decode(open(f"/sys/bus/pci/devices/{sys.argv[1]}/config", "rb").read())
```

Output for the balloon device on this VM:

```
vendor 0x1af4  device 0x1045  class 0xffff00  header type 0
command 0x0406: memory-enable=1 bus-master=1 intx-disable=1
BAR0: memory 0x4000000000  64-bit  prefetchable=0
  cap 0x40: Vendor-specific
  cap 0x50: Vendor-specific
  cap 0x60: Vendor-specific
  cap 0x70: Vendor-specific
  cap 0x84: Vendor-specific
  cap 0x98: MSI-X
```

On a physical machine, `lspci -vv -s <bus:dev.fn>` prints the same fields plus the PCI Express capability: `LnkCap` and `LnkSta` (link speed and width the device can do, and is doing), `MaxPayload` and `MaxReadReq` (section 3.4), and the BARs as `Region` lines. Problem D has you compare them.

---

## 5. The device's side: DMA and interrupts are memory requests too

### 5.1 Bus mastering is DMA

With **bus master enable** set, a function may issue memory requests of its own. A **DMA read** is a memory-read TLP carrying a host address: the root complex passes it to the memory controller, and the completion TLP(s) carry the data back, split at the RCB. A **DMA write** is a posted memory-write TLP. A descriptor ring (a NIC's, NVMe's) is nothing more than DMA reads of descriptors and DMA writes of data and completions. On x86 these accesses are cache-coherent (the hardware snoops the CPU caches); on many Arm SoCs the driver must flush or invalidate (tutorial 24, section 8, and [tutorial 26](26_interrupts_exceptions_and_dma.md)).

Keep two address spaces apart; mixing them up is a classic bug:

| Address | Who uses it | What it names |
|---|---|---|
| **BAR address** | the CPU | where to reach the *device's* registers or memory (`0x4000000000` above) |
| **DMA (bus) address** | the device | where to reach *RAM*; equals the physical address without an IOMMU and an **I/O virtual address (IOVA)** with one (section 6) |

### 5.2 An interrupt is a write

Legacy interrupts use four shared wires (INTx), emulated in PCIe by message packets; they are slow to share and cannot say which queue needs service. **MSI** and **MSI-X** replace them: the OS gives the device an *address* and a *data value*, and to interrupt, the device **performs a memory write of that value to that address**. On x86 the address lies in the local-APIC range, where the write is turned into an interrupt with a given vector at a given core. **MSI-X** allows up to 2,048 vectors per function in a table in a BAR, so each queue can interrupt its own core.

This VM has it all in `/proc/interrupts` (trimmed to the names): the balloon device's table (BAR0 + 0x8000, 5 entries, from the capability above) is these five vectors, each named by the driver:

```
29: PCI-MSIX-0000:00:01.0   0-edge  virtio0-config
30: PCI-MSIX-0000:00:01.0   1-edge  virtio0-inflate
31: PCI-MSIX-0000:00:01.0   2-edge  virtio0-deflate
32: PCI-MSIX-0000:00:01.0   3-edge  virtio0-stats
33: PCI-MSIX-0000:00:01.0   4-edge  virtio0-reporting_vq
```

[Tutorial 26](26_interrupts_exceptions_and_dma.md) follows an interrupt from the write to the handler (APIC, affinity, moderation, polling).

### 5.3 Peer to peer, in a paragraph

Memory requests are routed by address, so two endpoints can exchange them without touching host memory: a NIC writing into a GPU's BAR (the idea behind GPUDirect RDMA), or an NVMe drive reading into GPU memory. Below a switch the packets turn around inside it; across root ports it depends on the CPU. **Access Control Services (ACS)** let a switch or root port force such packets up to the root complex, where an IOMMU can check them (section 6.3 explains why that decides IOMMU groups).

---

## 6. The IOMMU

### 6.1 The problem

A DMA-capable device is a **bus master**: it puts physical addresses on the wire, and nothing checks them. A buggy driver programming a wrong address, buggy device firmware, or a malicious hot-plugged peripheral can read or overwrite any memory in the machine. Two lesser problems come with it: a 32-bit device cannot reach RAM above 4 GiB without copying through bounce buffers, and a buffer scattered over many physical pages must be described to the device as a scatter-gather list.

### 6.2 What it is

An **IOMMU** sits between the PCIe root complex and memory and translates the **I/O virtual address (IOVA)** in each DMA request into a physical address, the device-side twin of the CPU's MMU (tutorial 15). The page tables are chosen by the request's **requester ID** (bus:device.function), and each entry carries read and write permission. A recent translation is cached in an **IOTLB**; a miss makes the hardware walk the table; an access with no valid, permitted mapping is **blocked and logged** as a fault. It also remaps interrupts, validating the MSI writes of section 5.2. The names: Intel **VT-d** (a root table indexed by bus, then a context table indexed by device and function, then page tables), AMD **AMD-Vi**, Arm **SMMU** (indexed by a stream ID).

### 6.3 What it buys

- **Protection.** A device can touch only the pages the driver mapped for it.
- **Contiguity.** Scattered physical pages can be given consecutive IOVAs, so one descriptor covers them.
- **Reach.** A 32-bit device can address any RAM.
- **Safe user-space and guest drivers.** The Linux kernel's [VFIO](https://docs.kernel.org/driver-api/vfio.html) framework lets a user-space program (DPDK, SPDK) or a virtual machine drive a device directly, with the IOMMU confining its DMA. The unit of ownership is the **IOMMU group**: the set of devices that cannot be isolated from one another (devices behind a PCIe switch without ACS, multi-function packages) must be assigned together.

### 6.4 What it costs

Two different costs, both measured on real servers by the papers cited at the top.

- **A translation miss.** In Neugebauer et al. (section 6.5) a 64 B DMA read rose from about **430 ns to 760 ns** (a penalty of about 330 ns) once the accessed window exceeded 256 KB, which they read as a **64-entry IOTLB** with 4 KiB pages. Random DMA over a large buffer pays this on most requests; large IOMMU pages avoid it.
- **Mapping and unmapping.** Every `dma_map`/`dma_unmap` changes page tables, and an unmap must **invalidate the IOTLB** so the device cannot keep using the old translation. Markuze et al. measured about **2,000 cycles** per invalidation (Sandy Bridge era) behind a lock, and strict per-I/O invalidation giving about **5× lower multi-core receive throughput** than the alternatives they compared, at 40 Gb/s. The usual compromise is **deferred (lazy) invalidation**: batch them, flushing after 250 unmaps or 10 ms in the kernel they studied. It is fast, but a stale mapping stays usable for up to 10 ms after the buffer was freed, a window in which a faulty device could still write into reused memory. That is the real trade: **speed against a time window of protection**.

Those are numbers for 2015-era kernels and hardware; modern kernels batch more cleverly and hardware is faster, so quote the *shape* (two costs, and a security window in lazy mode) rather than the figures. Linux lets you choose: `iommu.strict` for strict or lazy invalidation, and `iommu=pt` (passthrough) to skip translation for host-owned devices while keeping it for devices assigned to guests.

### 6.5 In this VM

There is none: `/sys/class/iommu` and `/sys/kernel/iommu_groups` are empty. The two boot-log lines (`Default domain type: Translated`, `DMA domain TLB invalidation policy: lazy mode`) are the kernel's defaults, not evidence of hardware. Whether your own machine has one is Problem D.

### 6.6 ATS and page requests

Two optional features let the device take part. **Address Translation Services (ATS)** lets an endpoint ask the IOMMU to translate an IOVA in advance and cache the answer in a device-side translation cache, so later requests carry a ready-made physical address and skip the IOMMU's walk. The **Page Request Interface (PRI)** lets a device raise a *page fault* and have the OS fault the page in, which, together with a process address-space ID (**PASID**), allows a GPU or accelerator to use a process's pageable memory directly instead of pinned buffers. These are the hooks behind "shared virtual addressing".

---

## 7. Sharing one device: SR-IOV and passthrough

### 7.1 The problem: 64 virtual machines and one NIC

There are four answers. **Emulate** a device in software (the hypervisor interprets every register access: slowest). **Paravirtualise** it with virtio (shared queues that the host serves; [tutorial 27](27_hardware_virtualization.md)). **Pass a whole device through** to one VM (native speed, but one device per VM). Or **SR-IOV** (Single Root I/O Virtualization): one physical device presents itself as one **physical function (PF)**, which the host manages, plus many lightweight **virtual functions (VFs)**. Each VF has its own configuration space, BARs, queues and **routing ID**, so each can be passed through to a different VM and the IOMMU can tell their DMA apart.

### 7.2 How a VF is addressed

SR-IOV is an extended capability (ID `0x10`) in the PF. It holds the number of VFs the device supports and has enabled, the VF BARs, and two numbers, **First VF Offset** and **VF Stride**. A VF's routing ID is

> **RID(VF n) = RID(PF) + First VF Offset + (n − 1) × VF Stride**    (n counts from 1)

Example (illustrative values, not a real device): PF at `03:00.0`, RID `0x0300`, offset `0x80`, stride 2.

| Function | Routing ID | bus:device.function |
|---|---|---|
| PF | `0x0300` | 03:00.0 |
| VF1 | `0x0380` | 03:10.0 |
| VF2 | `0x0382` | 03:10.2 |
| VF3 | `0x0384` | 03:10.4 |
| VF64 | `0x03FE` | 03:1f.6 |

With **ARI** (alternative routing ID interpretation) the 8-bit device-and-function field is read as one plain function number, so a bus can name 256 functions (VF64 above would be function 254 of bus 03); Linux shows it as the `ari_enabled` file in each device's sysfs directory, which exists in this VM too. On Linux you create VFs by writing a count to `sriov_numvfs` in the PF's directory, and they appear as new PCI functions ([kernel SR-IOV how-to](https://docs.kernel.org/PCI/pci-iov-howto.html)).

### 7.3 Passthrough and the trade-offs

**VFIO** binds a function (a PF or a VF) to the `vfio-pci` driver, maps its BARs into the guest's address space, programs the IOMMU so the device's DMA is confined to the guest's memory, and delivers its MSI-X interrupts to the guest. Comparing the four options:

| Option | Data path | Speed | Live migration | Needs an IOMMU |
|---|---|---|---|---|
| Emulated device | hypervisor code on every register access | slowest | easy | no |
| virtio (paravirtual) | shared queues served by the host | good | easy | no |
| Whole-device passthrough | straight to the hardware | native | hard | yes |
| SR-IOV VF | straight to the hardware | native | hard (the state lives in the device) | yes |

[Tutorial 27](27_hardware_virtualization.md) builds on this: nested page tables, interrupt virtualisation and the choice between these four for a scenario.

---

## 8. CXL in a paragraph

**Compute Express Link** reuses PCIe's physical layer (same lanes and connectors; its first versions ran at PCIe 5.0 speed) and multiplexes three protocols on it: **CXL.io**, which is essentially PCIe (enumeration, configuration, DMA); **CXL.cache**, by which a device can cache host memory coherently; and **CXL.mem**, by which the host can load and store a device's memory with ordinary cached, coherent instructions. Device types follow: Type 1 (a cache only, such as a SmartNIC), Type 2 (an accelerator with its own memory), Type 3 (a **memory expander**). The contrast with this tutorial is the point: a BAR is reached with slow, uncached MMIO semantics, while CXL.mem is just memory, which is why the OS shows a Type 3 device as a CPU-less NUMA node (tutorial 16) and why coherent CPU-to-accelerator attach (tutorial 24, section 7.8) is built on it. Later versions add switching, memory pooling and sharing between hosts.

---

## 9. The small buses: I2C, SPI, UART, USB and on-chip AXI

| Bus | Wires | Topology and speed | Addressing | Pitfalls | Typical use |
|---|---|---|---|---|---|
| **I2C** | 2: SDA, SCL (open-drain, pull-ups) | multi-master, many devices; 100 kbit/s, 400 kbit/s (Fast), 3.4 Mbit/s (Hs); later versions add 1 Mbit/s | 7-bit address (about 112 usable) + R/W bit; **ACK** bit per byte | missing or wrong pull-ups (section 1.5), two parts with the same address, a slave holding SDA low hangs the bus, **clock stretching** | sensors, EEPROMs, power-management chips, SFP modules |
| **SPI** | 4: SCLK, MOSI, MISO + one chip-select per device (push-pull) | one master, full duplex, shift register; tens of MHz | none: chip-select picks the device | **no ACK, no flow control, no error check**: talking to an absent device looks like success; the mode (CPOL, CPHA, four combinations) must match | flash (quad SPI: 4 data lines), ADCs, displays, radios |
| **UART** | 2: TX, RX (no clock) | point to point, both sides agree a baud rate | none | wrong baud rate or framing gives garbage | consoles, GPS, modems |
| **USB** | differential pair (+ power; more pairs in USB 3) | tiered star with hubs, up to 127 devices; 1.5 and 12 Mbit/s, **480 Mbit/s** (USB 2), 5 to 20 Gbit/s (USB 3.x), 40 and 80 Gbit/s (USB4) | the host assigns a 7-bit address at enumeration | bandwidth is shared across the tree; in USB 1 and 2 **the host polls** and a device cannot speak until asked | peripherals, storage, cameras |
| **AXI** (on chip) | five channels (below) | the SoC's interconnect | memory-mapped addresses | handshake deadlock, ordering rules | everything inside an SoC or FPGA |

Notes that earn points.

- **I2C** (the [specification](https://www.csd.uoc.gr/~hy428/reading/i2c_spec.pdf)): a **START** is SDA falling while SCL is high; a **STOP** is SDA rising while SCL is high; a byte is eight bits plus an acknowledge in which the receiver pulls SDA low. A slave can hold SCL low to make the master wait (**clock stretching**). A hung bus is usually recovered by clocking SCL nine times to let a stuck slave finish its byte.
- **SPI** ([overview](https://en.wikipedia.org/wiki/Serial_Peripheral_Interface)): master and slave each hold a shift register; on every clock the master shifts one bit out on MOSI while the slave shifts one out on MISO, so after eight clocks they have swapped bytes. Push-pull outputs (no pull-up) are why it runs so much faster than I2C.
- **USB** ([overview](https://en.wikipedia.org/wiki/USB)): enumeration is a reset, then the host reads the device's *descriptors*, assigns it an address and loads a driver. Full-speed traffic is scheduled in 1 ms frames and high-speed in 125 µs microframes, so polled devices have latency measured in those units. USB4 can tunnel PCIe, so an external port is a path for DMA into the machine, which is one reason operating systems apply the IOMMU to hot-plugged external devices.
- **AXI** ([overview](https://en.wikipedia.org/wiki/Advanced_eXtensible_Interface)): five independent channels, **read address, read data, write address, write data and write response**, each with its own **VALID/READY** handshake: a beat transfers on a clock edge where both are high, so either side can throttle the other. A request can be a burst of beats, and IDs let transactions complete out of order. **AXI4-Lite** is the simple single-beat subset used to reach a block's control registers, **AXI4-Stream** carries addressless data streams, and a PCIe root complex or DMA engine hangs off the same fabric inside the chip. This is what an FPGA design (tutorial 24) or an SoC's register map looks like from the inside.

---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| A driver's polling loop on a status register burns a core and is slow | every register read is a round trip (about a microsecond on hardware, 37 µs in this VM) | take an MSI-X interrupt, or poll a *completion entry in RAM* that the device DMA-writes, not a register |
| An interrupt handler runs after you "disabled" the interrupt, or touches freed data | the disabling write was posted and had not reached the device | read a register of the same device back after the write (section 2.4) |
| A 100 GbE NIC is far below line rate with small packets | a doorbell, descriptor and interrupt per packet; or a Gen 3 x8 link (section 3.7) | batch descriptors and doorbells, coalesce interrupts, check `LnkSta`, use a wider or faster link |
| An NVMe drive or GPU gets a quarter or half of the bandwidth you computed | the link trained narrower or slower than the card can do (shared lanes, wrong slot, riser) | compare `LnkSta` with `LnkCap` in `lspci -vv`; move the card; check bifurcation settings |
| A DMA works on the development board and corrupts memory (or faults) on the server | an IOMMU is on and you gave the device a physical address, or a mapping is missing or already unmapped | use the DMA API for the address; read `dmesg` for IOMMU fault lines |
| Network throughput collapses when the IOMMU is in strict mode | a map, unmap and IOTLB invalidation per packet (section 6.4) | reuse mappings (page pools), batch invalidations, or use passthrough mode where the policy allows |
| A GPU, NIC or NVMe is slower on one socket than the other | its lanes belong to one socket; the buffers or threads are on the other | pin threads and memory to the device's NUMA node (`numa_node` in sysfs) |
| Filling device memory a byte at a time is slow | each store is an uncached, separate TLP | map write-combining for bulk writes (`ioremap_wc`), or let the device DMA it; never read it back in the loop |
| Reads of a device return `0xFFFFFFFF` | no device claimed the address: link down, memory space not enabled, device still in reset, or the BAR was not assigned | check enumeration (`lspci`), the Command register, `dmesg` for BAR assignment failures |
| A 64-bit BAR of a GPU is not assigned, or the device is unusable | firmware cannot place a window above 4 GiB | enable "Above 4G decoding" (and Resizable BAR) in the firmware |
| An I2C sensor NACKs now and then, or the bus is stuck low | pull-ups outside the window of section 1.5, an address clash, or a slave stuck mid-byte | measure the rise time; recover with nine SCL pulses; give clashing parts different addresses |
| An SPI device returns all zeros or all ones | wrong mode (CPOL/CPHA), wrong chip-select, or no device (SPI never says) | check the mode in the datasheet, the chip-select polarity and the wiring |

---

## 11. Mistakes that cost points

1. **Reading GT/s as GB/s.** A Gen 3 lane is 8 GT/s but 0.985 GB/s: divide by 8 for bytes and multiply by 128/130 for the code.
2. **Quoting both directions as one number.** Gen 5 x16 is 63 GB/s each way; the 128 GB/s figure adds the directions.
3. **Forgetting packet overhead.** Every TLP carries about 24 bytes; a 4-byte register write is 14 % efficient and no link delivers more than about 91 % even with 256-byte payloads.
4. **Confusing a x16 slot with a x16 link.** The link trains to what both ends support; read `LnkSta`.
5. **Treating a device register like RAM.** It is uncached, unspeculated and ordered; `volatile` stops only the compiler; a read is a round trip, not a cache hit.
6. **Thinking a posted write has reached the device when the store retires.** It may still be on the link; read back to be sure.
7. **Confusing the BAR address with the DMA address.** The BAR is where the CPU finds the device; the DMA address is where the device finds RAM, and with an IOMMU it is not the physical address.
8. **Handing a device the result of `virt_to_phys`.** Use the DMA API; the right address depends on the IOMMU and the platform.
9. **Calling fixed priority "fair" or round-robin "faster".** Round-robin bounds each requester's wait; neither policy changes total bus throughput.
10. **Forgetting that open-drain needs a pull-up.** Without it the line never rises; with the wrong one the edges are too slow or the driver cannot pull low (section 1.5).
11. **Believing the IOMMU is only for virtual machines, or that it is free.** It protects against faulty and hostile devices on bare metal too, and costs a table walk per miss and an invalidation per unmap.
12. **Treating lazy IOMMU invalidation as equal to strict.** Lazy leaves a window in which a freed buffer is still reachable by the device.
13. **Calling SR-IOV and virtio the same, or thinking passthrough works without an IOMMU.** A VF is hardware with its own queues; virtio is a shared-memory protocol served by the host; passthrough needs the IOMMU to confine the device.
14. **Describing enumeration as "the OS asks each device its name".** It is a depth-first walk that numbers buses, sizes and places BARs, and programs bridge windows.

---

## 12. Questions you will be asked

**Q1. What is the difference between memory-mapped and port-mapped I/O?** Port-mapped devices live in a separate 64 K-port address space reached with `IN`/`OUT` (x86 only); memory-mapped devices occupy ranges of the ordinary physical address space and are reached with loads and stores. Modern devices are MMIO; Arm and RISC-V have no port space. MMIO wins because any instruction and addressing mode works and the MMU can grant exactly one device's pages.

**Q2. Why are device registers mapped uncached, and what does a driver do after a posted write?** A cache would hold stale status, duplicate or merge writes and speculate reads that have side effects (tutorial 14). The mapping is uncached and ordered; a store is *posted* (no reply), so a driver that needs the write to have arrived reads a register of the same device back, since a read cannot pass an earlier posted write.

**Q3. What is a PCIe lane, and what is the bandwidth of a Gen 4 x4 link?** A lane is two differential pairs, one per direction. Gen 4 is 16 GT/s with 128b/130b: `16 × 128/130 ÷ 8 = 1.97 GB/s` per lane, so **7.88 GB/s each way** for x4, minus packet overhead (a bit under 7 GB/s usable with 256-byte payloads).

**Q4. Why can a x16 link not use its full rate for small transfers?** Each TLP carries about 24 bytes of framing, sequence number, CRC and header however small the payload: a 64-byte write is 72.7 % efficient, a 4-byte one 14 %. Add ACK and flow-control packets (about 8 %) and the ceiling for large transfers is about 84 % of the headline rate.

**Q5. What happens at boot to find the devices?** Enumeration: firmware walks the tree depth-first; it reads each function's vendor ID (all ones means nothing there), numbers the buses behind each bridge, probes every BAR's size, assigns addresses from the bridge's windows with natural alignment, and programs the bridges' memory, prefetchable and I/O windows. The OS reuses the result or repeats it for hot-plug.

**Q6. What is a BAR, and how is its size found?** A base address register is how a function asks for address space and how the OS tells it where it was placed. To size it, disable decoding, write all ones, read back, clear the flag bits, and take the two's complement of the mask: `size = ~mask + 1`. The size is a power of two and the address is aligned to it.

**Q7. How does the CPU read a device's configuration space?** Either the legacy x86 port pair `0xCF8`/`0xCFC` (256 bytes) or ECAM, a memory window at `base + (bus << 20 | device << 15 | function << 12 | register)` (4 KB per function) whose base comes from the ACPI MCFG table. Both generate configuration TLPs, which are non-posted, so they are slow.

**Q8. How does a PCIe device interrupt the CPU?** With MSI or MSI-X: the OS programs an address and data value into the device, and the device interrupts by *writing that value to that address*, a posted memory write that the interrupt controller turns into an interrupt at a given core. MSI-X has a table of up to 2,048 vectors so each queue can have its own.

**Q9. What does an IOMMU do, and what does it cost?** It translates and permission-checks each device's DMA using page tables chosen by the requester ID, so a device reaches only what the driver mapped. Costs: a table walk on an IOTLB miss (about 330 ns measured on one server) and an IOTLB invalidation on every unmap, which strict mode pays per I/O and lazy mode batches at the price of a window in which freed buffers are still reachable.

**Q10. SR-IOV, virtio or passthrough for a VM's NIC?** Virtio when you need easy migration and good-enough speed (the host serves shared queues). Whole-device passthrough or an SR-IOV virtual function for native speed (the VM talks to hardware, confined by the IOMMU); each VF has its own BARs, queues and routing ID. The costs are harder live migration and a fixed number of VFs.

**Q11. Why does I2C use open-drain, who wins arbitration, and what limits its speed?** Open-drain lets many devices share a wire without contention (a 0 beats a 1: wired-AND), which also gives arbitration for free: each master reads back what it sends and the one that sees a 0 where it sent a 1 drops out, so the lower address wins without corrupting the message. The rising edge is the pull-up charging the bus capacitance, `0.8473 × R × C`, which caps the speed.

**Q12. A GPU, NIC or NVMe drive delivers a fraction of its expected bandwidth. What do you check?** The negotiated link (`LnkSta` against `LnkCap`: speed and width), the transfer sizes and MPS (header overhead), whether the data crosses to the other NUMA node, IOMMU mode and map/unmap rate, interrupt and doorbell rate per packet, and whether the lanes are shared with another device (bifurcation, a switch's upstream link).

**Q13. What is CXL, and how does it differ from PCIe?** Same physical layer, extra protocols: CXL.io (PCIe), CXL.cache (a device caches host memory coherently) and CXL.mem (the host loads and stores device memory as ordinary cacheable, coherent memory). A BAR is slow MMIO; CXL memory looks like a CPU-less NUMA node.

---

## 13. Problems

**How to use this section.** All four can be done on paper; each says *Where* to check the answer and *How*. The checks use a calculator or a few lines of Python. Problem D uses a real Linux machine. Do the paper part first, then check, and fix the paper answer if they disagree. Time: about 20 minutes each.

### Problem A (paper, then a script): how much does a link carry?

Use the formulas of sections 3.2, 3.5 and 3.7. Gen 3 x8 delivers 57.88 Gb/s at the TLP layer (after ACK and flow-control packets); write overhead is 24 bytes per TLP; MPS = 256 bytes.

(a) One-direction bandwidth of Gen 2 x1, Gen 3 x16, Gen 4 x4, Gen 5 x8 and Gen 6 x16. A Gen 4 x16 card is installed in a slot that is electrically Gen 4 x4, then in one that is Gen 3 x4: what fraction of its bandwidth does it get?
(b) On Gen 3 x8, the efficiency and the delivered Gb/s of 64-byte, 256-byte and 4 KiB writes, and of a 4-byte register write.
(c) A 40 GbE port receives 64-byte frames (84 bytes on the wire). How many packets per second is that? Per received packet the upstream link bytes are `104 + 100/N` for a batch of `N` packets (section 3.7). What is the smallest power-of-two `N` for which Gen 3 x8 keeps up?
(d) A device issues 64-byte DMA reads at 10 million per second. With the 547 ns latency of section 3.6, how many must be in flight? How many if every read also misses the IOMMU (+330 ns)?

*Where:* paper; check with a calculator or a short script.
*How:* (b) efficiency = `size ÷ (size + 24 × ⌈size ÷ 256⌉)`. (c) packets per second = `rate ÷ (84 × 8)`; link capacity = `57.88e9 ÷ 8 ÷ (104 + 100/N)`. (d) Little's law: in flight = rate × latency.

### Problem B (paper): arbitration and I2C

(a) Four masters D0 (highest priority) to D3 have backlogs of 3, 1, 2 and 2 one-cycle transfers and all request from cycle 1. Give the grant order and each master's finishing cycle under fixed priority and under round-robin that starts at D0.
(b) Now D0, D2 and D3 request in every cycle, forever. Give the first nine grants under each policy. Who starves?
(c) Two I2C masters send the 7-bit addresses `0x50` and `0x68` at the same time. Who wins, on which bit does the loser drop out, and why does the winner never notice?
(d) Fast-mode I2C at 3.3 V with 150 pF of bus capacitance: the range of pull-up values, a value you would choose, and the current drawn while the line is held low.

*Where:* paper; a few lines of Python for (a) and (b) if you want to check.
*How:* in (c) write both addresses in binary and AND them bit by bit from the most significant bit; in (d) use `R_min = (V_DD − 0.4) ÷ 3 mA` and `R_max = 300 ns ÷ (0.8473 × C_b)`.

### Problem C (paper, then a script): decoding, BARs and ECAM

(a) A small SoC has: RAM at `0x0000_0000` (512 MiB), boot ROM at `0x2000_0000` (64 KiB), UART at `0x4000_0000` (4 KiB), GPIO at `0x4000_1000` (4 KiB) and a timer at `0x4000_1800` (4 KiB). Which device(s) answer at `0x1FFF_FFFC`, `0x3FFF_FFFC`, `0x2000_8000`, `0x4000_0FFC`, `0x4000_1804` and `0x4000_2004`? What is wrong with the map, and how do you fix it?
(b) After writing all ones to BARs, the read-backs are: (i) `0xFFFF0000`; (ii) low `0xFFF0000C`, high `0xFFFFFFFF`; (iii) `0xFFFFFF01`; (iv) low `0x0000000C`, high `0xFFFFFFFC`. For each: memory or I/O, 32 or 64-bit, prefetchable or not, and the size.
(c) Four BARs of 16 KiB, 16 MiB, 1 MiB and 64 KiB sit below one bridge whose window starts at `0xC000_0000`. Give their addresses when placed largest first, the bridge's window limit, and the bytes wasted when they are placed in the order 16 KiB, 16 MiB, 1 MiB, 64 KiB.
(d) With the ECAM window at `0xE000_0000`, what is the address of register `0x40` of function `02:03.1`? How big is the window for buses 0 to 0x3F?

*Where:* paper; check (b) and (c) with a few lines of Python (the two's complement of the mask; round up to the size).
*How:* (a) a region matches when `base ≤ address < base + size`; a 4 KiB device must also be 4 KiB aligned. (b) clear the flag bits (4 for memory, 2 for I/O), assemble the 64-bit mask from both halves if bits [2:1] = `10`, and compute `~mask + 1`. (c) each BAR's address must be a multiple of its size. (d) use the formula of section 4.2.

### Problem D (on your machine): read a real PCI tree

Use any Linux machine or VM with PCI devices (a cloud instance works; a container sees only what the host mounts; macOS has none of these files).

(a) Count the functions (`ls /sys/bus/pci/devices | wc -l`) and print the tree (`lspci -tv`, from the `pciutils` package). Which are root ports, bridges and endpoints?
(b) Pick your NIC, NVMe drive or GPU: `lspci -vv -s <bus:dev.fn> | grep -E 'LnkCap:|LnkSta:|MaxPayload|MaxReadReq|Region'`. From `LnkSta` compute the one-direction bandwidth. Is the link running below `LnkCap`?
(c) `sudo cat /proc/iomem` and find the same device. Do the BAR ranges match the `Region` lines? Compute each size.
(d) Run `python3 pcicfg.py <domain:bus:dev.fn>` (section 4.5) on the same device and compare with `lspci -vv`.
(e) Is there an IOMMU? `ls /sys/class/iommu`, `dmesg | grep -i -E 'DMAR|IOMMU|SMMU'`, `ls /sys/kernel/iommu_groups | wc -l`.
(f) Does any device offer SR-IOV? `cat /sys/bus/pci/devices/*/sriov_totalvfs 2>/dev/null`.
(g) How many MSI-X vectors does the device use, and which CPUs take them? `grep -i msi /proc/interrupts`.

*Where:* your own machine.
*How:* `lspci` needs root for some fields (`sudo lspci -vv`). Compare with the Firecracker VM's answers in the Answers section.

---

## Answers

### Problem A

(a) Per direction: Gen 2 x1 = **0.50 GB/s**; Gen 3 x16 = **15.75 GB/s**; Gen 4 x4 = **7.88 GB/s**; Gen 5 x8 = **31.5 GB/s**; Gen 6 x16 = **121 GB/s** (using the 242/256 FLIT efficiency). The Gen 4 x16 card is 31.5 GB/s in a Gen 4 x16 slot; in a Gen 4 x4 slot it gets 7.88 GB/s (**one quarter**); in a Gen 3 x4 slot 3.94 GB/s (**one eighth**).

(b) 64 B: `64 ÷ 88 = 72.7 %` → `57.88 × 0.727 = 42.1 Gb/s` (5.3 GB/s). 256 B: `256 ÷ 280 = 91.4 %` → **52.9 Gb/s** (6.6 GB/s). 4 KiB: sixteen TLPs, `4096 ÷ (4096 + 16 × 24) = 91.4 %`, the same 52.9 Gb/s. 4 B register write: `4 ÷ 28 = 14.3 %` → 8.3 Gb/s. Larger payloads help only up to the MPS; after that the ratio is fixed.

(c) `40e9 ÷ (84 × 8) = 59.5 Mpps`. Capacity `= 7.235e9 ÷ (104 + 100/N)` packets per second:

| `N` | Bytes per packet | Mpps | Enough for 59.5? |
|---|---|---|---|
| 1 | 204 | 35.5 | no |
| 2 | 154 | 47.0 | no |
| 4 | 129 | 56.1 | no |
| **8** | 116.5 | **62.1** | **yes** |
| 16 | 110.2 | 65.6 | yes |

So **N = 8**; one doorbell and one interrupt per packet cannot do it.

(d) `10e6 × 547 ns = 5.47`, so **6** reads in flight. With the IOMMU miss the latency is 877 ns and `10e6 × 877 ns = 8.77`, so **9**.

### Problem B

(a)

| Policy | Grant order (cycles 1 to 8) | Finishing cycle (D0, D1, D2, D3) |
|---|---|---|
| Fixed priority | D0 D0 D0 D1 D2 D2 D3 D3 | 3, 4, 6, 8 |
| Round-robin | D0 D1 D2 D3 D0 D2 D3 D0 | 8, 2, 6, 7 |

(b) Fixed priority: D0 in all nine cycles; **D2 and D3 starve** (and D1 would, if it requested). Round-robin: **D0 D2 D3 D0 D2 D3 D0 D2 D3**, one grant in three each.

(c) `0x50 = 1010000` and `0x68 = 1101000`. The first bits are equal (1). On the **second** bit `0x50` sends 0 and `0x68` sends 1; the line is the AND, so it reads 0. `0x68` sees 0 where it sent 1, loses and stops driving. **`0x50` wins**: the lower address always does. `0x50` read back exactly what it sent, so it cannot tell there was a competitor, and nothing it sent was corrupted.

(d) `R_min = (3.3 − 0.4) ÷ 3 mA = 967 Ω`; `R_max = 300 ns ÷ (0.8473 × 150 pF) = 2,360 Ω`. Choose **1.5 kΩ** or 2.2 kΩ. With 1.5 kΩ the current while the line is low is `2.9 V ÷ 1.5 kΩ = 1.9 mA` (the cost of a low-going line, and the reason very low pull-ups are avoided in battery devices).

### Problem C

(a)

| Address | Responds |
|---|---|
| `0x1FFF_FFFC` | RAM (its last word) |
| `0x3FFF_FFFC` | **nothing**: a hole; a simple bus hangs or errors, PCI would read all ones |
| `0x2000_8000` | boot ROM |
| `0x4000_0FFC` | UART |
| `0x4000_1804` | **GPIO and timer both**: an overlap |
| `0x4000_2004` | timer (offset `0x804`) |

The timer is placed at `0x4000_1800`, which is neither 4 KiB-aligned nor clear of the GPIO (it overlaps GPIO's upper half, `0x4000_1800` to `0x4000_1FFF`). Move it to `0x4000_2000`, a multiple of its size.

(b)

| | Decoded | Size |
|---|---|---|
| (i) `0xFFFF0000` | 32-bit memory, not prefetchable | `~0xFFFF0000 + 1 = 0x10000` = **64 KiB** |
| (ii) `0xFFF0000C`, `0xFFFFFFFF` | bit 3 = 1 prefetchable; bits [2:1] = `10` so 64-bit; mask `0xFFFFFFFF_FFF00000` | **1 MiB** |
| (iii) `0xFFFFFF01` | bit 0 = 1: I/O BAR; mask `0xFFFFFF00` | **256 bytes** of port space |
| (iv) `0x0000000C`, `0xFFFFFFFC` | 64-bit, prefetchable; mask `0xFFFFFFFC_00000000` | `2³⁴` = **16 GiB** (a GPU exposing its memory) |

(c) Largest first: 16 MiB at `0xC000_0000` to `0xC0FF_FFFF`; 1 MiB at `0xC100_0000` to `0xC10F_FFFF`; 64 KiB at `0xC110_0000` to `0xC110_FFFF`; 16 KiB at `0xC111_0000` to `0xC111_3FFF`. The end is `0xC111_4000`; with 1 MiB window granularity the **limit is `0xC11F_FFFF`**, an 18 MiB window. In the given order: 16 KiB at `0xC000_0000` to `0xC000_3FFF`; the 16 MiB BAR must start on a 16 MiB boundary, `0xC100_0000`; then 1 MiB at `0xC200_0000` and 64 KiB at `0xC210_0000`. The gap `0xC000_4000` to `0xC0FF_FFFF` wastes **16,760,832 bytes**, and the window grows to `0xC21F_FFFF` (34 MiB).

(d) `0xE000_0000 + (2 << 20 | 3 << 15 | 1 << 12 | 0x40) = 0xE000_0000 + 0x20_0000 + 0x1_8000 + 0x1000 + 0x40 =` **`0xE021_9040`**. Buses 0 to 0x3F are 64 buses × 1 MiB = **64 MiB**.

### Problem D

There is no single right answer, but here is what this machine, the Firecracker VM, gives, so you can see what to look for.

(a) 11 functions, all on bus 00: the host bridge `00:00.0` (Intel `8086:0d57`, class `0x060000`) and ten virtio devices `00:01.0` to `00:0a.0` (vendor `1af4`: a balloon, six block devices, a network device, a vsock device and an entropy source). A flat bus, no root ports or switches, so there is no tree to draw; on a server you expect a root complex, root ports and a few switches.
(b) No `LnkCap`/`LnkSta`: these functions have no PCI Express capability (the boot log calls them "conventional PCI endpoint"). On real hardware compare the two lines; a `LnkSta` of `Speed 8GT/s, Width x4` under a `LnkCap` of `16GT/s, x4` means the link is running at Gen 3 instead of Gen 4: half the bandwidth.
(c) Each device has one 64-bit BAR of **512 KiB** (`4000000000-400007ffff`, `4000080000-40000fffff`, and so on, each 0x80000 long, stacked from `0x4000000000`).
(d) For `00:01.0`: the output in section 4.5.
(e) None: `/sys/class/iommu` and `/sys/kernel/iommu_groups` are empty (section 6.5). On a server `dmesg` shows `DMAR` (Intel), `AMD-Vi` or `SMMU` lines, and the number of groups tells you how finely devices can be isolated.
(f) No `sriov_totalvfs` files. A NIC that supports SR-IOV shows a number such as 64 or 128.
(g) Each virtio function uses MSI-X, one vector per queue plus one for configuration changes, spread over the two CPUs: for example the balloon device has five (`virtio0-config`, `-inflate`, `-deflate`, `-stats`, `-reporting_vq`).

---

## Cheat sheet

- **Bus basics:** address + data + control lines; peak = width × clock (PCI 32 bit × 33 MHz = 133 MB/s, shared); split transactions free the bus during a long wait; parallel buses died of skew, loading and pins, replaced by serial point-to-point links with an embedded clock.
- **Arbitration:** daisy chain (position = priority, starvation, cheap); centralised (any policy); distributed (self-selection, collision detection, I2C/CAN wired-AND). Fixed priority starves; round-robin bounds the wait (N − 1 grants).
- **Open-drain:** pull low or let go, pull-up to 1, line = AND of devices. Rise time `0.8473 RC`. `R_min = (V_DD − 0.4) ÷ 3 mA`; `R_max = t_r ÷ (0.8473 C_b)` (Fast-mode: 300 ns, ≤ 400 pF). I2C: lower address wins; START = SDA falls while SCL high.
- **I/O mapping:** port-mapped (x86 `IN`/`OUT`, 64 K ports, `0xCF8`/`0xCFC` for config) vs memory-mapped (the norm; the only kind on Arm and RISC-V). Decode = compare high bits; sizes powers of two, aligned; an unclaimed PCI read returns all ones.
- **MMIO:** uncached, unspeculated, ordered; write posted, read a round trip; read back to flush. Measured: L1 0.4 to 0.6 ns, DRAM miss 146 to 149 ns, PCIe DMA read 547 ns (published), a config read in this VM 36 to 39 µs.
- **PCIe links:** GB/s per lane = GT/s × code ÷ 8: Gen 3 0.985, Gen 4 1.97, Gen 5 3.94, Gen 6 7.56 (FLIT), Gen 7 15.1; x16 = 15.75 / 31.5 / 63 / 121 / 242 GB/s one way. Width and speed negotiated; check `LnkSta`.
- **TLP costs:** memory write or read request 24 B, completion 20 B; MPS 128 to 4096 (256 typical); efficiency `size ÷ (size + 24⌈size/MPS⌉)`: 4 B 14 %, 64 B 73 %, 256 B 91 %; DLLP overhead ~8 %; Gen 3 x8: 63.0 raw → 57.88 → 52.9 Gb/s large writes. 40 GbE at 64 B needs 59.5 Mpps → batch ≥ 8.
- **Layers:** transaction (TLPs, credits), data link (sequence, LCRC, ACK/NAK replay), physical (encoding, training). Memory write posted; memory read, config and I/O non-posted. A read cannot pass a posted write.
- **Link physics:** idle links drop to ASPM L1 (microsecond exit, a source of latency spikes), retimers extend reach at Gen 5 and above, and correctable AER errors are replays that signal a marginal link.
- **Config space:** 256 B / 4 KB; vendor ID 0xFFFF = empty; Command bit 1 memory enable, bit 2 bus master; capabilities list from 0x34 (MSI-X `0x11`, PCIe `0x10`); extended from 0x100 (AER, ATS, SR-IOV `0x10`, ReBAR). ECAM address = base + (bus << 20 | dev << 15 | fn << 12 | reg), base from ACPI MCFG; legacy: `0xCF8`/`0xCFC`.
- **BARs:** write all ones, read back, clear flags, `size = ~mask + 1`; flags: memory/I/O, 32/64-bit, prefetchable; aligned to size; bridge windows 1 MiB (memory) and 4 KiB (I/O). Enumeration = depth-first walk numbering buses and assigning BARs.
- **Device side:** DMA = memory TLPs from a bus master; MSI-X = a memory write to an interrupt address (table of up to 2,048 vectors in a BAR); peer-to-peer by address; ACS forces traffic up to the IOMMU.
- **IOMMU:** IOVA → physical by requester ID; protection, contiguity, 32-bit reach, VFIO. Costs: miss ~330 ns (64-entry IOTLB), invalidation ~2,000 cycles per unmap, strict mode ~5× lower throughput in the 2016 study; lazy mode leaves a window (10 ms there). Group = unit of isolation.
- **SR-IOV:** PF + VFs, each with own BARs, queues and routing ID `RID(PF) + offset + (n − 1) × stride`; ARI gives 256 functions per bus; `sriov_numvfs`. Compare emulation / virtio / passthrough / VF.
- **CXL:** PCIe PHY + CXL.io, CXL.cache, CXL.mem; Type 3 = memory expander = CPU-less NUMA node.
- **Small buses:** I2C 2 wires open-drain, 7-bit address, ACK; SPI 4 wires push-pull, no ACK, modes 0 to 3; UART async; USB host-polled tiered star, 480 Mbit/s (USB 2), up to 80 Gbit/s (USB4); AXI five channels with VALID/READY.
- **Everything here came from real output of this VM's kernel, published papers, specifications and short calculations; nothing was timed on a physical PCIe link.**

**Next:** tutorial 26, Interrupts, exceptions and DMA (polling versus interrupts, interrupt vectors and controllers, privilege levels and what the hardware does on a system call, timers, DMA and descriptor rings, how a NIC delivers a packet, NVMe queues, polling in the style of DPDK). Say "next" when you are ready.
