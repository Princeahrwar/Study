# CO 16: DRAM, Memory Controllers and NUMA

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 13](13_cache_basics.md) and [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (caches, miss costs, memory-level parallelism) and [tutorial 15](15_virtual_memory_and_the_tlb.md) (pages, huge pages)*

By the end of this tutorial you can:

- compare the SRAM and DRAM cells, and explain why DRAM reads are destructive and why DRAM must be refreshed (and what that costs);
- describe how DRAM is organised (channel, rank, bank group, bank, row, column) and what the row buffer is;
- compute the latency of a row hit, an access to a closed row and a row conflict from the timings (tCL, tRCD, tRP);
- compute peak bandwidth for DDR4, DDR5, LPDDR and HBM, and say why the achieved figure is 70 to 90% of it;
- split a physical address into channel, bank, row and column bits, and explain interleaving and XOR hashing;
- explain what a memory controller does: queues, the first-ready scheduling rule, write draining, refresh, and interference between cores;
- use Little's law for memory (bytes in flight = bandwidth × latency), and tell a latency-bound program from a bandwidth-bound one;
- measure bandwidth the STREAM way and interpret the result, including the bytes that the program does not count;
- explain ECC, chipkill, scrubbing, machine-check errors and Rowhammer;
- say in a paragraph each what flash, persistent memory, CXL and processing-in-memory change;
- explain NUMA: local and remote memory, first-touch placement, `numactl`, and the mistakes that make a multi-socket server slower than a single socket.

Caches (tutorials 13 and 14) are small SRAM arrays on the chip. When the last-level cache misses, the request leaves the chip, goes through a **memory controller** and reaches **DRAM**. This tutorial opens that box. Almost everything about main memory comes from one fact: *a DRAM row is slow to open and cheap to read once it is open*. Latency, bandwidth, address mapping, scheduling and interference all follow from it.

**Where the numbers come from.** Bandwidth and timing arithmetic (sections 3 and 4) is exact: it comes from the transfer rates and timing parameters in the memory standards, and you can redo it on paper. Timings marked *measured* were taken on the same kind of cloud virtual machine as tutorials 14 and 15 (an Intel Xeon core under KVM, two virtual CPUs, 7 GB of memory, Linux 6.18, transparent huge pages in `madvise` mode, GCC 13). **Cloud numbers drift from one session to the next by 30% or more, so every measured figure is a range over several runs.** This machine has **one NUMA node and does not expose ECC counters**, so NUMA, ECC and Rowhammer are explained from the arithmetic and not measured. Sections say so where it matters.

> **The one-sentence model.** DRAM is a large array of leaky capacitors read one whole row at a time into a row buffer; hitting the open row is cheap, changing rows is expensive, and many banks and channels working in parallel turn that into bandwidth, so latency is decided by row state and bandwidth by concurrency.

---

## 1. Two kinds of memory cell

### 1.1 SRAM and DRAM

| | **SRAM** (registers, caches) | **DRAM** (main memory) |
|---|---|---|
| Cell | 6 transistors: two cross-coupled inverters that hold the bit, plus two access transistors | 1 transistor and 1 capacitor (**1T1C**): the bit is the charge on the capacitor |
| Keeps its value | as long as it has power (static) | for tens of milliseconds, then the charge leaks away (dynamic) |
| Read | non-destructive, fast (a fraction of a nanosecond for a small array) | **destructive**: reading disturbs the charge, so the value must be written back |
| Area per bit | large (6 transistors) | an order of magnitude or more smaller |
| Where | on the processor die: kilobytes to tens or hundreds of megabytes | on separate chips (or stacked next to the processor): gigabytes to terabytes |
| Needs refresh | no | **yes**, every few tens of milliseconds |

The cell size explains the hierarchy: SRAM is too big to build in gigabytes, DRAM is too slow to sit next to the pipeline.

### 1.2 How a DRAM read works, and why it leads to the row buffer

A DRAM chip stores bits in a grid. A horizontal wire (the **wordline**) selects a **row** of cells, and each cell in that row connects to a vertical wire (a **bitline**) with a **sense amplifier** at the bottom.

```text
1. Precharge:  all bitlines are set to half the supply voltage.
2. Activate:   the wordline of the chosen row turns on. Each capacitor shares its charge with its
               bitline and moves the voltage up or down by a tiny amount.
3. Sense:      the sense amplifiers amplify that small difference to a full 0 or 1.
               This also drives the value back into the capacitors (the restore).
4. Now the whole row (8,192 bits per chip, 8 KB across a rank) sits in the sense amplifiers.
   They are the ROW BUFFER. Reads and writes now take the bits they want from the row buffer.
5. To use a different row, the open row must be closed (precharge) first.
```

Two consequences. **Every read destroys the row's stored charge**, so the sense amplifiers must restore it before the row is closed; that takes time (tRAS, section 3). And **the unit of access inside the chip is a whole row**. It is wasteful to open a row for 64 bytes, so the chip is built to open a row once and then hand out many pieces of it quickly.

### 1.3 Refresh

The capacitors leak. Every cell must be read and restored within its **retention time**: 64 ms for DDR4 at normal temperature, **32 ms for DDR5**, and half of that when the chip is hot (above 85 °C).

The chip does not refresh everything at once. The controller issues **8,192 refresh commands (REF)** per window, and each one refreshes a few rows in every bank:

```text
tREFI  =  64 ms / 8192  =  7.8 µs     (DDR4: one REF command every 7.8 µs)
tREFI  =  32 ms / 8192  =  3.9 µs     (DDR5)
tRFC   =  how long one REF keeps the chip busy (grows with chip size)
```

While a REF runs, the rank cannot serve accesses. The fraction of time lost is **tRFC / tREFI**:

| Chip | tRFC | tREFI | Time lost |
|---|---|---|---|
| DDR4, 8 Gb | 350 ns | 7.8 µs | **4.5%** |
| DDR4, 16 Gb | 550 ns | 7.8 µs | 7.0% |
| DDR5, 16 Gb | 295 ns | 3.9 µs | 7.6% |
| DDR5, 32 Gb | 410 ns | 3.9 µs | 10.5% |

Bigger chips have more rows to refresh per command, so refresh costs more as capacity grows. Three effects reach software:

- **Bandwidth**: 4 to 10% of peak is never available.
- **Latency spikes**: a read that arrives during a refresh waits for the rest of tRFC, up to a few hundred nanoseconds. It is one reason the latency distribution has a tail (section 6.6).
- **Power**: an idle DRAM still burns power refreshing. Laptops and phones put DRAM in *self-refresh* during sleep, which is why standby uses power in proportion to the amount of RAM.

DDR5 can refresh **one bank at a time** (same-bank refresh), so the other banks keep serving requests during the refresh.

**Worked example.** DDR4-3200 has a peak of 25.6 GB/s per channel. With 8 Gb chips the refresh takes 4.5%, so the most a channel can deliver is 25.6 × (1 − 0.045) = **24.4 GB/s**. A read arriving at a random moment has a 4.5% chance of hitting a refresh, and then waits on average half of 350 ns = 175 ns.

---

## 2. How DRAM is organised

### 2.1 From chip to channel

```text
channel      one memory controller, one 64-bit data bus (72 bits with ECC), one command bus
 └ DIMM      the module you plug in; one or two DIMMs per channel
    └ rank   a set of chips that together fill the whole 64-bit bus, answering one access;
             a DIMM has 1, 2 or 4 ranks (chip-select lines choose the rank)
       └ chip   x4, x8 or x16 data pins. Eight x8 chips = 64 bits; a ninth x8 chip carries the ECC byte
          └ bank group → bank    DDR4: 4 groups × 4 banks = 16 banks;  DDR5: 8 groups × 4 banks = 32 banks
             └ rows × columns    one row open at a time per bank (the row buffer)
```

Typical numbers for an 8 GB DDR4 rank built from eight x8 chips of 8 Gb: each chip contributes **1 KB of a row**, so a rank-wide row (the **DRAM page**) is **8 KB**; each chip has 16 banks; each bank has 64 K rows of 1 KB. A 16 GB DIMM is two such ranks. (The word "page" here means a DRAM row, not an operating-system page; this tutorial says *row* to avoid mixing them up.)

### 2.2 Commands

The controller does not send "read address X". It sends a small set of commands on the command bus and the chip obeys timing rules between them:

| Command | Meaning |
|---|---|
| **ACT** (activate) | open row *r* of bank *b*: copy the row into the row buffer |
| **RD / WR** | read or write one **burst** starting at column *c* of the open row |
| **PRE** (precharge) | close the open row of bank *b* (write the buffer back, reset the bitlines) |
| **REF** | refresh (section 1.3) |

### 2.3 A cache line is one burst

The data bus is 64 bits (8 bytes) wide, and a burst is 8 transfers: 8 × 8 B = **64 bytes, exactly one cache line**. That is why the cache line is 64 bytes: it is the natural size of one DDR4 burst.

DDR5 changed the shape but kept the 64 bytes: each DIMM is split into **two independent 32-bit subchannels**, and a burst is 16 transfers: 16 × 4 B = 64 B on **one** subchannel. Two subchannels can serve two different lines at the same time, which is more concurrency per DIMM.

"DDR" means *double data rate*: data move on both edges of the clock. **DDR4-3200** means 3200 million transfers per second from a 1600 MHz clock.

### 2.4 Why banks and bank groups

Opening a row takes tens of nanoseconds, but moving data takes only a few. So the chip has **many banks that work in parallel**: while bank 3 opens a row, bank 5 can be sending data, and bank 9 can be closing a row. The controller keeps the data bus busy by interleaving commands to different banks.

**Bank groups** are a refinement: two consecutive reads to the *same* bank group need more time between them (tCCD_L) than two reads to *different* groups (tCCD_S). Good address mappings therefore rotate through bank groups first (section 5).

### 2.5 LPDDR and HBM

| | Used in | What is different |
|---|---|---|
| **LPDDR** (LPDDR4X, LPDDR5) | phones, laptops, embedded and automotive SoCs (e.g. Jetson, Apple) | low voltage, soldered next to the SoC, narrow 16-bit channels that are combined (a 256-bit interface is 16 channels) |
| **HBM** (HBM2e, HBM3, HBM3e) | GPUs and AI accelerators, some server CPUs | **8 to 12 DRAM dies stacked** with vertical wires (through-silicon vias) on a base die; the stack sits beside the processor on a silicon interposer and has a **1024-bit** interface. Lower speed per pin, a huge number of pins |

The row, bank and command ideas are the same in all three.

---

## 3. DRAM timing: from a request to data

### 3.1 The three cases

Whether the wanted row is already open decides everything:

| State of the bank | What the controller must do | Time to the first data |
|---|---|---|
| **Row hit**: the wanted row is open | `RD` | **tCL** |
| **Row closed** (bank idle, precharged) | `ACT`, wait tRCD, `RD` | **tRCD + tCL** |
| **Row conflict**: another row is open | `PRE`, wait tRP, `ACT`, wait tRCD, `RD` | **tRP + tRCD + tCL** |

```text
row hit:       RD ──tCL──► data
row closed:    ACT ──tRCD──► RD ──tCL──► data
row conflict:  PRE ──tRP──► ACT ──tRCD──► RD ──tCL──► data
```

The timings are quoted in **clock cycles** ("CL22") and converted with the clock period.

**Worked example, DDR4-3200 with timings 22-22-22 (tCL-tRCD-tRP).** The clock is 1600 MHz, so one cycle is 0.625 ns.

- tCL = tRCD = tRP = 22 × 0.625 = **13.75 ns** each.
- Row hit: **13.75 ns**. Closed row: 13.75 + 13.75 = **27.5 ns**. Row conflict: 3 × 13.75 = **41.25 ns**.
- Then the burst itself: 8 transfers at two per clock = 4 cycles = **2.5 ns** before the last byte arrives.

A row conflict costs three times a row hit. The gap between a hit and a conflict, tRP + tRCD = 27.5 ns, is the price of changing rows.

Two more limits matter for random traffic. **tRAS** is the minimum time a row must stay open after `ACT` (the restore of section 1.2; about 32 ns), so a bank cannot open a new row more often than once per **tRC = tRAS + tRP** (about 46 ns). And **tFAW** forbids more than four `ACT`s to a rank in a short window (tens of ns), because opening rows draws a lot of current. So a workload that opens a new row on every access runs at roughly half the bandwidth of a streaming one, even with every bank busy.

### 3.2 Latency in nanoseconds has hardly changed

| Memory | Transfer rate | CAS latency | tCL in ns | Peak bandwidth per channel |
|---|---|---|---|---|
| DDR3-1600 | 1600 MT/s | CL11 | 13.75 ns | 12.8 GB/s |
| DDR4-3200 | 3200 MT/s | CL22 | 13.75 ns | 25.6 GB/s |
| DDR5-4800 | 4800 MT/s | CL40 | 16.7 ns | 38.4 GB/s |

Bandwidth tripled; the time to open a row and read from it did not improve. Each generation made the interface faster (more transfers per second, wider bursts), not the cell array: the capacitor and the sense amplifier take the time they take. The cycle counts grow (CL11, CL22, CL40) while the cycle gets shorter, and the product stays about 14 to 17 ns. **A newer, faster DRAM gives you more bandwidth, not lower latency.**

### 3.3 Row-buffer policy

After an access the controller chooses whether to leave the row open.

- **Open-page policy**: leave it open. A later access to the same row is a hit (13.75 ns); an access to another row pays the conflict (41.25 ns). Good for streams and local accesses.
- **Closed-page policy**: precharge at once (a "read with auto-precharge"). Every access to that bank is then a closed-row access (27.5 ns): never a hit, never a conflict. Predictable, and good for random traffic.
- **Adaptive**: guess from recent history, per bank. Most real controllers do this.

### 3.4 Measured: the row buffer is visible from software

A pointer chase over 1 GiB (so every hop goes to DRAM) can be arranged so that consecutive hops stay inside a block of *S* bytes: the chase visits all nodes of one block in random order, then jumps to a random block. A small block means consecutive hops tend to find their row already open. *Measured*, ns per hop (range over two or three runs, 2 MB pages so that TLB misses stay small, tutorial 15 section 6.3):

| Block size *S* | Nodes per block | ns per hop | What happened |
|---|---|---|---|
| 128 B | 1 | 126 to 132 | every hop goes to a random row: closed row or conflict |
| 512 B | 4 | 122 to 125 | same |
| 1 KB | 8 | 119 to 123 | same: probably the few hops of a block land in different banks, each opening its own row |
| 2 KB | 16 | 101 to 106 | some hops find their row open |
| 4 KB | 32 | 92 to 97 | **most hops are row hits** |
| 8 KB | 64 | 93 to 94 | same |
| 64 KB | 512 | 94 to 97 | same: a block spans a few rows spread over several banks, all left open |
| 128 KB | 1,024 | 97 to 99 | same |
| 256 KB | 2,048 | 102 to 111 | hits start to fade |
| 512 KB | 4,096 | 121 to 129 | the block spans more rows than there are banks: conflicts again |
| 1 MB to 4 MB | 8,192 to 32,768 | 119 to 127 | same |

The saving from keeping to an open row is **25 to 30 ns**, which is what the arithmetic says: a row hit costs tCL and a conflict costs tRP + tRCD + tCL, so the difference is tRP + tRCD (27.5 ns for DDR4-3200 22-22-22; a little more for DDR5). This virtual machine does not say which memory the host has, so treat the match as a consistency check, not a proof.

**Row-buffer reach** is the analogue of TLB reach: with one open row per bank, the memory can hold open about (channels × banks × row size) bytes of recent rows. Random accesses over a region smaller than that mostly hit; over a larger region they mostly conflict. On this host the hits fade between 128 KB and 512 KB, so its reach is a few hundred kilobytes. (Which exact channel and bank count that corresponds to cannot be seen from inside a virtual machine: the host decides where the guest's pages sit in the DIMMs, and other tenants close rows too.)

---

## 4. Bandwidth

### 4.1 Peak bandwidth is arithmetic

**Peak bandwidth = transfers per second × bytes per transfer × number of channels.**

| Memory | Rate | Width | Peak |
|---|---|---|---|
| DDR4-3200, one channel | 3200 MT/s | 8 B | **25.6 GB/s** |
| DDR5-4800, one channel (2 × 32 bit) | 4800 MT/s | 8 B | **38.4 GB/s** |
| DDR5-5600, two channels (a desktop) | 5600 MT/s | 2 × 8 B | 89.6 GB/s |
| DDR5-4800, **8 channels** (a server socket) | 4800 MT/s | 8 × 8 B | **307 GB/s** |
| DDR5-4800, 12 channels | 4800 MT/s | 12 × 8 B | 461 GB/s |
| LPDDR5-6400, 256-bit interface | 6400 MT/s | 32 B | 205 GB/s (an embedded SoC such as Jetson AGX Orin) |
| **HBM3**, one stack | 6.4 Gb/s per pin | 1024 bits = 128 B | **819 GB/s** |
| HBM3e, one stack | about 9.6 Gb/s per pin | 128 B | about 1.2 TB/s |

A data-centre GPU with five or six HBM stacks therefore has several terabytes per second. The route to more bandwidth is always *more pins in parallel*: more channels, a wider interface, stacked dies. Pin speed alone does not scale, because faster pins need more power and tighter timing. HBM goes the other way: slow-ish pins, a thousand of them, a few millimetres from the processor.

### 4.2 Achieved bandwidth is 70 to 90% of peak

Even a perfect streaming program does not reach the peak. What takes the rest:

- **Refresh**: 4.5 to 10% (section 1.3).
- **Read-write turnaround**: reversing the bus direction costs a few nanoseconds each time (tWTR, tRTW), so a mixed read/write stream is slower than reads alone (about 60 to 75% of peak).
- **Row changes** and the activation limits of section 3.1, when the access pattern is not purely sequential.
- Command-bus and scheduling inefficiency.

The practical rule: **plan on 80% of peak for sequential reads, less for mixed or random traffic.** The number to quote is the STREAM figure of the machine (section 8), measured, not the data-sheet peak.

### 4.3 Bandwidth and latency are different resources

A memory system can have huge bandwidth and mediocre latency (HBM and GPUs), or the reverse. Bandwidth is the *width* of the pipe; latency is its *length*. A program that follows pointers (one request at a time) sees the length; a program that streams (many requests in flight) sees the width. Section 7 makes this exact with Little's law.

---

## 5. Address mapping and interleaving

### 5.1 Which address bits pick what

The controller must turn a physical address into **(channel, rank, bank group, bank, row, column)**. It does this by taking fields of address bits, and which bits go where decides the performance. The aims pull in different directions:

1. Consecutive cache lines should go to **different channels**, so that one stream uses all of them (bandwidth).
2. Then to different **bank groups** and **banks**, so that their row openings overlap.
3. But enough consecutive lines must still fall in **the same row**, so that a stream gets row hits.
4. The **row** bits go at the top: the highest bits change the slowest, so nearby addresses stay in the same row.

Here is a concrete mapping for the exercises: 16 GiB (34-bit physical addresses), 2 channels, one rank per channel, 16 banks per rank (4 bank groups × 4 banks), 8 KB rows, 64-byte lines.

```text
bits   33 ........ 18 | 17 16 | 15 ...... 9 | 8 7 | 6   | 5 ... 0
field        row      | bank  |    column    | BG  | chan | byte in line
width      16 bits    | 2     |    7 bits    | 2   | 1    | 6 bits
```

(Check: 2 channels × 4 groups × 4 banks × 65,536 rows × 8 KB = 16 GiB.)

What this does to a sequential stream:

- bit 6 is the lowest field above the line, so consecutive lines **alternate between the two channels**; bits 8:7 then rotate through the four **bank groups**. A run of 8 lines covers 2 channels × 4 groups.
- A 64 KB aligned block is **one row in each of 8 banks** (2 channels × 4 groups), 128 lines in each. A sequential scan opens 8 rows and then takes 127 hits from each: a row-hit rate of 127/128, with all 8 banks working in parallel.

### 5.2 Decoding an address, step by step

Take the physical address `0x1_2345_6780` and the mapping above. Shift and mask each field:

| Field | How | Value |
|---|---|---|
| byte in line | `addr & 0x3F` | 0 |
| channel | `(addr >> 6) & 1` | 0 |
| bank group | `(addr >> 7) & 3` | 3 |
| column (line within the row) | `(addr >> 9) & 0x7F` | 51 |
| bank | `(addr >> 16) & 3` | 1 |
| row | `addr >> 18` | 18,641 |

Moving the address by `0x40` (the next line) changes the channel; by `0x80`, the bank group; by `0x1_0000` (64 KB), the bank; by `0x4_0000` (256 KB), the row.

### 5.3 Bad strides and XOR hashing

With a plain mapping like this, a program that steps by **256 KB** (a power of two equal to the row stride) changes only the row bits: **the same channel, bank group and bank every time, a new row every time**. Every access is a row conflict in one bank: 41 ns of latency, and the bank can open a row only once per tRC (about 46 ns), so 64 bytes per 46 ns is **1.4 GB/s**, against 20 GB/s or more for the same number of lines laid out sequentially. This is the DRAM version of the cache-set conflict of tutorial 14: power-of-two strides collide in any structure indexed by address bits.

The fix in hardware is **XOR hashing**: instead of taking the bank bits straight from the address, XOR them with some higher bits:

```text
bank = addr[17:16]  XOR  addr[19:18]       (the low two bits of the row number)
```

Now a 256 KB stride changes `addr[19:18]` by 1 each step, and the accesses rotate over **four banks**: the same rows are still opened, but four at a time in parallel. Real controllers XOR many address bits into the channel, bank-group and bank selections (the exact functions are undocumented and differ between CPUs; researchers recover them by timing experiments), so that no simple stride lands on one bank.

### 5.4 Interleave granularity, and what the operating system controls

- **Channel interleave granularity**: 64 B to a few hundred bytes. Fine interleave gives one stream all the channels; coarse interleave (4 KB or more) would keep a page on one channel. Multi-socket machines also interleave across *sockets* in some modes (section 11).
- **The OS only chooses the bits above the page offset**. With 4 KB pages, address bits 11:0 come from the program's offset and bits 12 and up from the frame the OS hands out. In the mapping above, channel (bit 6), bank group (8:7) and part of the column are in the offset, so even one 4 KB page spreads across both channels and all four bank groups. The **bank and row** come from the frame number, so the OS decides which bank a page lands in. Real-time and mixed-criticality systems use this: they give each core or each task its own banks (**bank colouring**), so that one task's row conflicts cannot delay another's.
- **Huge pages** (tutorial 15) make physical addresses contiguous over 2 MB. That fixes the low 21 address bits, which helps predictability and row locality.

---

## 6. The memory controller

### 6.1 What it is and what it sees

The **memory controller** sits on the processor (or on its I/O die) and owns the channels. It receives every request that leaves the last-level cache: demand misses, **hardware prefetches**, write-backs of dirty lines, and **DMA** from devices (NIC, GPU, NVMe, [tutorial 26](26_interrupts_exceptions_and_dma.md)). It also serves page-table walks (tutorial 15) that missed in the caches.

```text
requests (read / write, address)
   │
   ▼ decode address → channel, rank, bank, row, column     (section 5)
   ▼ read queue and write queue per channel (tens of entries)
   ▼ scheduler: choose the next request or command that is allowed by the timing rules
   ▼ command generator: PRE / ACT / RD / WR / REF, spaced by tRCD, tRP, tRAS, tFAW, tWTR, ...
   ▼ PHY: drives the channel's pins
```

### 6.2 The first-ready rule (FR-FCFS)

A first-come-first-served scheduler would do every request in arrival order and pay a row conflict whenever two neighbours want different rows. The standard improvement is **FR-FCFS: first-ready, first-come-first-served**:

1. Among the queued requests, take one that is a **row hit** (its row is already open) before any that is not.
2. Among equals, take the **oldest**.

**Worked example.** One bank, DDR4-3200 22-22-22 (hit 13.75 ns, closed 27.5 ns, conflict 41.25 ns). The bank is idle. Four requests arrive in this order: A (row 5), B (row 9), C (row 5), D (row 5).

| Order | Costs | Total |
|---|---|---|
| FCFS: A, B, C, D | A closed 27.5 + B conflict 41.25 + C conflict 41.25 + D hit 13.75 | **123.75 ns** |
| FR-FCFS: A, C, D, B | A closed 27.5 + C hit 13.75 + D hit 13.75 + B conflict 41.25 | **96.25 ns** |

Reordering saves 22% of the DRAM time, at the price of fairness: B waited behind C and D, and a thread that streams through a row can keep winning. Real schedulers therefore cap the run of row hits, and add an age limit after which the oldest request goes first, so no one starves.

### 6.3 Writes are batched

A write has no one waiting for it (the cache has already accepted the store), so the controller holds writes in a **write queue** and gives reads priority. When the write queue passes a high watermark, the controller **drains** it: it switches the bus to writes and sends a batch, then returns to reads at a low watermark. Batching matters because turning the data bus around costs a few nanoseconds each way (tWTR, tRTW). The effect for software: stores are cheap *until* the queue fills, and then a burst of writes holds up reads. A write-heavy workload therefore raises the **read latency** of everyone sharing the channel.

### 6.4 Refresh scheduling

The controller can postpone up to 8 refresh commands and issue them later, back to back, when the channel is quiet. This moves the refresh stall away from busy periods, but the stall still exists (section 1.3).

### 6.5 Interference between cores and devices

All cores, the GPU, and DMA share the same channels and banks. Two things go wrong:

- **Bandwidth contention**: one thread that streams at full speed raises the queueing delay of every other request (section 7.2).
- **Row-buffer interference**: two streams that each have perfect row locality alone destroy each other's: each access of one closes the row the other had open, and both pay conflicts. FR-FCFS favours whichever has the higher row-hit rate, so a streaming thread can slow a random-access neighbour a lot more than the other way round.

Hardware answers: **memory-bandwidth monitoring and allocation** (Intel RDT: MBM, MBA; Arm MPAM) lets the OS measure and limit bandwidth per group of cores, the memory-bandwidth sibling of the cache partitioning of tutorial 14. In SoCs shared by CPU, GPU, camera and display, the controller also has **priority classes and deadlines**: a display that misses its refresh deadline shows a glitch, so its requests must not wait behind a GPU copy.

### 6.6 Measured: the latency is a distribution, not a number

A dependent random chase over 1 GiB (every hop goes to DRAM), with each hop timed separately (reading the timestamp counter before and after adds about 20 ns). *Measured*, 20 million hops:

| Latency of a hop | Share of hops |
|---|---|
| under 100 ns | 1% |
| 100 to 150 ns | **67%** |
| 150 to 250 ns | 21% |
| 250 to 500 ns | 10% |
| over 500 ns | 0.3% |
| over 2 µs | 0.05% (interrupts and the hypervisor, not DRAM) |

The typical hop is 100 to 150 ns, but **one hop in ten takes more than twice as long**. The tail comes from everything in this section: a refresh in progress (up to about 350 ns), a bank busy with another request, the write queue draining, other tenants' traffic on the same channel. A virtual machine cannot separate these causes, and the tail is smooth, not a clean refresh bump. The lesson is for design: **latency budgets and SLAs should use a high percentile, not the average**, and the tail of the memory system is part of every tail-latency number you will see.

---

## 7. Bandwidth, latency and Little's law

### 7.1 Little's law for memory

**Little's law:** the number of requests in flight = throughput × latency. For memory it reads:

**Bytes in flight = bandwidth × latency.**

To sustain a bandwidth, the system must keep that many bytes in flight; with less, the bandwidth is lower. The arithmetic:

| System | Bandwidth | Latency | Bytes in flight | Lines in flight |
|---|---|---|---|---|
| One DDR4-3200 channel | 25.6 GB/s | 100 ns | 2,560 B | **40** |
| A socket with 8 DDR5-4800 channels | 307 GB/s | 100 ns | 30.7 KB | **480** |
| A GPU with 3.35 TB/s of HBM | 3.35 TB/s | 500 ns (loaded) | 1.7 MB | 13,000 requests of 128 B |
| One core streaming (*measured*, below) | 12.3 GB/s | 133 ns | 1.6 KB | **26** |
| One core chasing pointers (*measured*) | 0.5 GB/s | 130 ns | 64 B | **1** |

Three consequences:

1. **A pointer chase uses a tiny fraction of the memory system**: one line per 130 ns is 0.5 GB/s, about a twenty-fifth of the single-core streaming rate and a seventy-fifth of one DDR5-4800 channel. Latency-bound code does not benefit from faster DRAM; it benefits from more requests in flight (tutorial 14, section 3).
2. **A core can only have so many requests in flight**: the L1 has 10 to 16 fill buffers; the L2 and its prefetchers keep more (tutorial 14 measured 12 for random lines; the streaming read here reaches about 26 because the prefetchers add outstanding requests). So *a single core cannot saturate a server's memory*: 480 lines in flight need about 20 cores streaming.
3. **GPUs hide DRAM latency with parallelism, not caches**: to sustain terabytes per second at several hundred nanoseconds they need megabytes in flight, which is why a GPU keeps hundreds of thousands of threads resident, each with several loads in flight ([tutorial 22](22_gpu_execution_model.md), section 7).

### 7.2 The loaded-latency curve

As more traffic enters the memory system, requests queue, and latency rises. A simple queueing model gives the shape: latency = idle latency + a queueing term that grows like ρ/(1 − ρ), where ρ is the utilisation of the channel. With an idle latency of 90 ns and a queueing constant of 25 ns (illustrative values):

| Utilisation ρ | 10% | 30% | 50% | 70% | 80% | 90% | 95% |
|---|---|---|---|---|---|---|---|
| Latency (ns) | 93 | 101 | 115 | 148 | 190 | 315 | 565 |

The curve is flat up to about half of the achievable bandwidth, bends at 70 to 80%, and climbs steeply after that. Two rules follow. **A system that must keep latency low should run below 70 to 80% of its achievable bandwidth**; and **your latency depends on your neighbours' bandwidth**, because the queue is shared.

*Measured*, a chase thread on one virtual CPU while the other virtual CPU streams memory:

| Other CPU doing | Its traffic | Chase latency |
|---|---|---|
| nothing | 0 | 133 to 137 ns |
| sequential read | 6.5 to 7.8 GB/s | 137 to 141 ns |
| a triad (2 reads, 1 write) | 8.6 to 10.5 GB/s | 135 to 136 ns |

Latency **did not move**, because two virtual CPUs cannot generate enough traffic to leave the flat part of the curve on a host with many channels. That is a result and not a failure: it says the machine was far from saturation. To see the knee you need enough cores to fill the channels, which a small cloud VM does not have; the problems use the model above instead.

### 7.3 Latency-bound or bandwidth-bound?

| | **Latency-bound** | **Bandwidth-bound** |
|---|---|---|
| Typical code | pointer chasing, hash-table probes one at a time, tree walks, linked lists | scans, `memcpy`, vector and matrix streaming, stencils |
| In flight | 1 to a few lines | as many as the core allows |
| Bandwidth used | well below the STREAM number | close to the STREAM number |
| Faster DRAM helps? | a little (lower latency) | yes (more bandwidth) |
| What helps | more independent requests at once (batching, interleaving lookups, software prefetch), smaller structures, huge pages | fewer bytes (narrower types, compression, fusing passes, blocking), more cores or channels until saturated, non-temporal stores |

To tell which you have: compute the bandwidth your program achieves (bytes moved per second) and compare it with the STREAM figure of the machine. Far below with a low IPC means latency-bound; near the figure means bandwidth-bound. Hardware counters for the memory controller (on Linux, `perf` uncore events such as the integrated memory controller's CAS counts) give the real traffic including prefetches and write-backs.

---

## 8. Measuring bandwidth, STREAM-style

### 8.1 The kernels

STREAM, the standard memory-bandwidth benchmark, times four simple loops over arrays that are much larger than any cache. It *counts* the bytes the program reads and writes:

| Kernel | Loop | Bytes counted per element |
|---|---|---|
| Copy | `c[i] = a[i]` | 16 (read 8, write 8) |
| Scale | `b[i] = q * c[i]` | 16 |
| Add | `c[i] = a[i] + b[i]` | 24 |
| Triad | `a[i] = b[i] + q * c[i]` | 24 |

Rules for a trustworthy number:

- **Arrays at least four times the last-level cache**; here 512 MB each, far above any cache.
- Initialise the arrays with the threads that will use them (**first-touch**; matters for NUMA, section 11), pinned to cores, with huge pages if the TLB would otherwise interfere (tutorial 15).
- Take the **best of several repetitions** and discard the first.
- Make sure the loop is limited by memory and not by the arithmetic: a sum over an array with one floating-point accumulator is limited by the floating-point add latency (a few cycles per element: 5.6 GB/s here); **eight independent accumulators** removed that limit, and the same loop then reached 12 GB/s.

### 8.2 The bytes STREAM does not count

A normal store to a line that is not in the cache first **reads the line** (read for ownership, tutorial 13 section 7.2), and later writes it back. So the hardware moves more than the program asks for:

| Kernel | Counted | Actually moved by the memory system |
|---|---|---|
| Copy, Scale | 16 B | **24 B** (read a, read-for-ownership of c, write c) |
| Add, Triad | 24 B | **32 B** |
| Write only | 8 B | **16 B** (read for ownership, then write back) |
| Read only | 8 B | 8 B |

Non-temporal stores (`_mm_stream_pd`, `movnt`; tutorial 14, section 8.3) skip the read for ownership, so counted and actual then agree. They are the right choice when the data will not be reused soon.

### 8.3 Measured

One thread, then two threads pinned to the two virtual CPUs; GB/s, range over two runs; the "actual" column is the counted figure scaled by the table above:

| Kernel | 1 thread, counted | 1 thread, actual | 2 threads, counted |
|---|---|---|---|
| read only (sum) | 12.2 to 12.3 | 12.2 to 12.3 | 20.8 to 24.1 |
| write only | 7.4 to 7.5 | 14.8 to 15.0 | 12.9 to 14.2 |
| copy | 10.6 to 10.9 | 15.9 to 16.4 | 19.8 to 20.2 |
| scale | 10.6 | 15.9 | 19.9 to 20.0 |
| add | 12.3 | 16.4 | 23.6 to 23.9 |
| triad | 12.1 to 12.3 | 16.1 to 16.4 | 23.5 to 24.3 |

What the table shows:

1. **The counted numbers differ between kernels (7.5 to 12.3), but the memory traffic is nearly the same, 15 to 16 GB/s, for every kernel that writes.** The write-only loop looks slow because half of its traffic is a read the program never asked for.
2. **A plain read stream is lower (12.3) than the mixed kernels (16.4)**: with writes and read-for-ownership requests there are more requests in flight. Little's law again: 12.3 GB/s × 133 ns = 1.6 KB, about 26 lines in flight.
3. **Two threads give 1.7 to 2.0 times one thread.** One core did not saturate the host's memory system; bandwidth grows with the number of requests in flight. On a real server, you keep adding threads until the figure stops growing; that plateau is the machine's STREAM bandwidth, typically 70 to 90% of the arithmetic peak of section 4.
4. These are floors, not the machine's capability: the cloud VM has two cores and shares the host. Compare with the 25.6 GB/s of *one* DDR4-3200 channel.

STREAM is a best case: sequential, prefetch-friendly, all banks busy. A program that makes random 64-byte accesses with many in flight reaches only a fraction of it (row conflicts, section 3.1), and a pointer chase reaches almost nothing (section 7.1).

---

## 9. Reliability: ECC, scrubbing and Rowhammer

### 9.1 Bits flip

A DRAM bit is a tiny charge, and it can be changed by a particle strike, by electrical noise, by a weak cell that leaks too fast, or by a failing chip. Large-scale field studies of production servers found that memory errors are far more common than lab tests suggested: **more than 8% of DIMMs were affected by errors in a year**, a few bad DIMMs produced most of the errors (20% of the machines with errors made over 90% of them), and repeating "hard" errors were more common than random one-off flips. At fleet scale, memory errors are routine events, not rare ones. This is why servers use ECC memory.

### 9.2 ECC DIMMs and SECDED

An ECC DIMM stores **72 bits for every 64 bits of data**: the extra 8 bits are a code computed from the data, and the DIMM has a ninth chip (or an extra one per rank) for them. The usual code is **SECDED**: *single-error correcting, double-error detecting*. On every read, the controller recomputes the code:

| Result | Meaning | What happens |
|---|---|---|
| no mismatch | data are fine | data returned |
| mismatch that points at one bit | a **correctable error** (CE) | the bit is flipped back, the data returned correct, the event **counted and logged**, usually written back corrected |
| mismatch that cannot be a single bit | an **uncorrectable error** (UE), two or more bits | a **machine-check exception** is raised; the OS kills the process that owns the page, takes the page out of use, or, if it was kernel memory, stops the machine |

The overhead is 8 extra bits per 64: **12.5%** more chips, and a small delay in the read path. Tutorial 17 builds the Hamming code by hand; for now, the number to remember is *8 check bits protect 64 data bits against any single-bit error and detect any double-bit error*.

### 9.3 Chipkill, scrubbing, and on-die ECC

- **Chipkill (SDDC, single-device data correction)**: SECDED fixes one bit, but a whole failing chip corrupts several bits of every word it contributes to. Chipkill uses a code over *symbols* (the 4 or 8 bits that one chip supplies), so that the loss of an **entire chip** is corrected. The field study estimated that it reduces uncorrectable errors by a factor of 4 to 10. Servers that cannot afford a crash use it.
- **Scrubbing**: a correctable error left alone can become a double-bit error when a second bit flips in the same word. A **patrol scrubber** reads every location in the background (a full pass in hours), corrects any single-bit error it finds and writes the result back, so errors cannot accumulate.
- **DDR5 on-die ECC**: every DDR5 chip corrects single-bit errors **inside itself**, to hide the weak cells of small processes. It is not visible to the controller, reports nothing, and does not cover the bus. It is not a replacement for ECC DIMMs: server DDR5 modules still carry the 72- or 80-bit side-band ECC.

### 9.4 Seeing errors on Linux

```bash
sudo dmidecode -t memory | grep -E 'Type:|Speed|Size|Error'     # DIMM type, speed, "Multi-bit ECC" or none
grep . /sys/devices/system/edac/mc/mc*/ce_count /sys/devices/system/edac/mc/mc*/ue_count 2>/dev/null
dmesg | grep -iE 'mce|edac|hardware error'
sudo ras-mc-ctl --summary      # needs the rasdaemon package
```

A rising corrected-error count on one DIMM is the signal to replace it **before** it produces an uncorrectable error. A cloud VM shows none of this (the host handles memory errors, and this machine's `edac` directory is empty), so there is nothing to measure here.

### 9.5 Rowhammer

The rows of a DRAM chip are packed so tightly that activating a row disturbs its neighbours. If a row (the **aggressor**) is activated tens to hundreds of thousands of times within one refresh window, charge leaks out of cells in the adjacent rows (the **victims**) faster than normal, and **some of their bits flip**, although the program never wrote to them. The number of activations needed has fallen by roughly an order of magnitude or more as cells have shrunk; in the original study, 110 of 129 tested DDR3 modules showed flips.

```text
loop:   read  address X   (row 100)       each read is an ACT: the row is opened and closed
        read  address Y   (row 102)       X and Y are in different rows of the same bank, so the
        clflush X ; clflush Y              reads go to DRAM and row 101, between them, is the victim
```

Why it is a security problem and not only a reliability one: a flip in the right place changes **who can access what**. A published attack flipped bits in a **page-table entry** (the frame number or a permission bit, tutorial 15 section 4) so that an unprivileged process gained access to page tables and then to all of physical memory. Variants have run from JavaScript, from a neighbouring cloud VM, and on phones. [Tutorial 30](30_hardware_security.md), section 5, says why the Spectre fixes do not touch it.

Mitigations, all partial:

- **Target Row Refresh (TRR)**: the DRAM tracks frequently activated rows and refreshes their neighbours early. The implementations are vendor-specific and secret, and researchers have defeated them with access patterns that hammer many rows at once ("many-sided" patterns) on a good share of tested DIMMs.
- **DDR5** adds refresh-management commands and per-row activation counting in the newer specifications, and some modules have still been broken since.
- **ECC** helps (a single flip is corrected) but a carefully chosen three-bit flip in a word passes SECDED undetected.
- **Faster refresh** reduces flips at the cost of bandwidth and power.

The takeaway for an interview: *DRAM bits are not isolated; reading memory repeatedly can change other memory; ECC and TRR reduce the risk and do not remove it.* Tutorial 30 returns to this in the security context.

---

## 10. Beyond DRAM, in a paragraph each

- **NAND flash** (SSDs, phone storage). Non-volatile, dense and cheap, but a read takes tens of microseconds (a thousand times slower than DRAM), a cell can only be written after a whole block is erased, and cells wear out. It sits behind a storage interface and a controller (tutorial 17), not on the memory channel.
- **Persistent memory.** Byte-addressable, non-volatile memory on the memory bus, a few times slower than DRAM, was available as a product and has been withdrawn, but its programming model remains the model for anything that survives power loss: stores reach a cache, not the memory, so software must **write back the line (`CLWB`) and then fence (`SFENCE`)** before it may treat the data as durable, and must order the writes so a crash leaves a consistent state (tutorial 20).
- **CXL.** A cache-coherent protocol carried over PCIe wires ([tutorial 25](25_buses_pcie_and_io_mapping.md)). A CXL memory expander puts DRAM on a card that the CPU reads and writes with ordinary loads and stores. To the OS it appears as a **NUMA node with no CPUs** (section 11), with higher latency than local DRAM, typically at least that of a remote socket. Uses: more capacity than the DIMM slots allow, memory shared or pooled between servers, and tiering (hot pages in DRAM, cold ones on CXL). The placement problem is exactly NUMA placement.
- **Processing in memory (PIM).** Moving data over a 64-bit channel costs far more time and energy than computing on them. PIM puts simple compute units inside or next to the DRAM banks (the banks' internal bandwidth is far higher than the channel's), so that operations such as scans, reductions and matrix-vector products run where the data are. Early products and research prototypes exist for DIMMs and HBM; it is a way around the bandwidth wall for bandwidth-bound work ([tutorial 24](24_accelerators_and_fpgas.md)).

---

## 11. NUMA: Non-Uniform Memory Access

### 11.1 Why memory is not uniform

A server with two sockets has two memory controllers, one on each processor, each with its own DIMMs. A core reads **its own socket's memory (local)** directly. To read the other socket's memory (**remote**) the request crosses the socket-to-socket interconnect (Intel UPI, AMD Infinity Fabric; a point-to-point network, [tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) section 6.3), is served by the other controller, and comes back. So:

- **Remote latency is higher**, typically 1.3 to 2 times local (for example 90 ns local and 150 ns remote);
- **Remote bandwidth is lower**, limited by the interconnect (tens of GB/s per link) instead of the local channels (hundreds of GB/s);
- Every remote access also uses the interconnect, which other remote traffic and cache-coherence messages share (tutorial 19).

Modern single-socket processors can be non-uniform too: chiplet designs (AMD EPYC) and large meshes can be configured as several **NUMA nodes per socket** (AMD "NPS", Intel "sub-NUMA clustering"), each with its own memory controllers, because a core reaches the nearby controllers faster. A machine with 2 sockets × 4 nodes has 8 nodes.

### 11.2 Looking at the topology

```bash
lscpu | grep -i numa            # number of nodes and which CPUs belong to each
numactl --hardware              # nodes, sizes, and the distance matrix
cat /sys/devices/system/node/node0/distance
```

```text
available: 2 nodes (0-1)
node 0 cpus: 0 1 2 3 ... 31         node 0 size: 128000 MB
node 1 cpus: 32 33 ... 63           node 1 size: 128000 MB
node distances:
node     0    1
   0:   10   21
   1:   21   10
```

The distances are relative hints from the firmware (10 = local); 21 means "about twice as far". The real ratio has to be measured.

**This VM has one node** (`lscpu` shows one, and `numactl` is not installed), so I have not measured NUMA effects here. To measure them on a multi-node machine, repeat the pointer chase of tutorial 14 with the threads on node 0 and the memory first on node 0, then on node 1, and compare. Expect 1.3 to 2 times.

### 11.3 Where does a page go? Policies and first touch

The OS decides which node supplies the physical frame for a virtual page. The default policy is **local allocation with first touch**: the frame comes from the node of the CPU that *first touches* the page (tutorial 15 section 7.3: the frame is attached at the first write, not at `malloc`).

This causes the classic NUMA bug:

```c
double *a = malloc(64UL << 30);
memset(a, 0, 64UL << 30);            // one thread touches everything → ALL pages on that thread's node
#pragma omp parallel for             // 64 threads on two sockets, each uses part of a[]
for (...) work(a[i]);                // half the threads are remote, and one controller carries all the traffic
```

The program is correct and runs about half as fast as it should. The fixes:

| Fix | How | When |
|---|---|---|
| **Parallel first touch** | initialise with the same threads and the same partition that will later use each part | regular data partitioned between threads |
| **Interleave** | `numactl --interleave=all ./app`, or an interleave policy on the region: pages are placed round-robin on all nodes | shared data accessed by everyone; gives balanced bandwidth and an average latency |
| **Bind** | `numactl --cpunodebind=0 --membind=0 ./app`, or `mbind` / `set_mempolicy` / `libnuma` (`numa_alloc_onnode`) in code | a process or a data plane that should live on one node |
| **Pin threads** | `taskset`, `sched_setaffinity`, cgroup cpusets | always: a thread that migrates to the other socket leaves its memory behind |

To check where the pages are: `numastat -p <pid>` (pages per node), `/proc/<pid>/numa_maps` (per region), and `perf` events for remote accesses.

**Automatic NUMA balancing** (`/proc/sys/kernel/numa_balancing`) lets the kernel fix some mistakes at run time: it periodically makes pages inaccessible to sample who uses them (the next access takes a minor fault), then **migrates the page** or moves the task. The price is those faults and the TLB shootdowns that migration causes (tutorial 15 section 10.3), which is why latency-sensitive services often switch it off and place memory themselves.

### 11.4 Devices have a home node too

A NIC, GPU or NVMe drive hangs off one socket's PCIe lanes. DMA to buffers on the other node crosses the interconnect in both directions. The rule for packet processing and GPU feeding: **run the polling thread and allocate the buffers (huge pages, per node) on the device's node**.

```bash
cat /sys/class/net/eth0/device/numa_node      # the NIC's node (-1 means unknown/one node)
nvidia-smi topo -m                             # GPU to CPU affinity
```

---

## 12. One cache miss, end to end

Idle system, DDR4-3200 22-22-22, a load that misses in L1, L2 and L3:

| Stage | Time |
|---|---|
| lookups in L1, L2 and L3 (all miss) | 25 to 40 ns |
| across the on-chip network to the memory controller | 5 to 20 ns |
| controller: decode the address, queue, schedule | 5 to 20 ns when idle; much more under load |
| DRAM: row hit / closed row / row conflict | 14 / 28 / 41 ns |
| data burst (last byte of the line) | 2.5 ns |
| ECC check, return across the chip, fill L3, L2, L1 | 10 to 20 ns |
| **Total, idle** | **about 60 to 120 ns** |

Tutorial 13 quoted 60 to 100 ns for DRAM; this virtual machine measured **120 to 135 ns** for a dependent miss with 2 MB pages (and 45 to 75 ns more when the access also missed the TLB with 4 KB pages, tutorial 15 section 6.3). A large server die has a longer on-chip path than a laptop, and a loaded system adds queueing (section 7.2). Whatever the exact number, the DRAM array itself, the part this tutorial calls tCL, is only 14 to 41 ns of it.

---

## 13. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| A streaming loop reaches far less than the machine's STREAM number | too few requests in flight (a latency chain, no vectorisation, one core) | independent accumulators, vectorise, more threads; check with Little's law |
| More threads stop helping a scan | memory channels are saturated: bandwidth-bound | move fewer bytes (narrower types, fused passes, compression, blocking); stop adding threads |
| Random lookups are slow and bandwidth is low | latency-bound, one miss at a time | batch independent lookups, software prefetch, huge pages, smaller tables |
| An access with a power-of-two stride is many times slower | one bank (and cache sets) hit again and again | pad the stride, change the layout |
| A write-only loop (`memset`, a fill) costs twice what you expect | read-for-ownership doubles the traffic | non-temporal stores or `memset` for large blocks |
| Tail-latency spikes with no cause on a quiet core | refresh, write-queue drains, neighbours' traffic | budget with percentiles; partition bandwidth; isolate tenants |
| The same program is slower on 2 sockets than on 1 | remote memory: first touch on one node, migrating threads | parallel first touch, bind or interleave, pin threads |
| Corrected-error counts rising on one DIMM | a failing DIMM | replace before it produces an uncorrectable error |

---

## 14. Mistakes that cost points

1. **"DDR5 is faster, so the latency is lower."** Bandwidth went up; the time to open a row and read from it stayed near 14 to 17 ns.
2. **Using the data-sheet peak as the achievable figure.** Plan on 70 to 90% for sequential reads, less for mixed or random traffic. Use a measured STREAM number.
3. **Ignoring the bytes the program does not count.** A normal store reads the line first, so write-only loops move twice what they write.
4. **One latency number for DRAM.** There are three cases (hit, closed, conflict), plus queueing, refresh and neighbours. Quote a percentile.
5. **Assuming one core can saturate the memory.** Little's law: a socket with 480 lines in flight needs about 20 streaming cores.
6. **Applying STREAM bandwidth to random access.** Random lines get a fraction of it (row conflicts), and a pointer chase gets a fiftieth or less.
7. **Forgetting address mapping.** Power-of-two strides collide in DRAM banks as they do in cache sets.
8. **Mixing up a DRAM row and an OS page.** A row is 8 KB across a rank and is a hardware matter; a page is 4 KB and is the OS's.
9. **"ECC protects against everything."** SECDED corrects one bit and detects two; chipkill covers a chip; Rowhammer can flip bits ECC does not catch; DDR5 on-die ECC is not DIMM ECC.
10. **Thinking `malloc` places memory.** Placement happens at the first touch. Initialise with the threads that will use the data.
11. **Leaving threads unpinned on a NUMA machine.** They migrate away from their memory.
12. **Benchmarking bandwidth with arrays that fit in cache,** or with one unpinned thread, or without discarding the first run.

---

## 15. Questions you will be asked

**Why does DRAM need refresh, and what does it cost?** A bit is charge on a capacitor and leaks away. Every row must be read and restored within the retention time (64 ms for DDR4, 32 ms for DDR5), using 8,192 refresh commands per window. The chip is busy for tRFC each time: about 4.5% of the time for an 8 Gb DDR4 chip (350 ns every 7.8 µs), 7 to 10% for larger and DDR5 chips. It costs bandwidth, a few hundred nanoseconds of latency for requests that collide with a refresh, and power.

**Explain a row hit, a closed row and a row conflict.** A read of an open row costs tCL. A read of an idle bank costs tRCD + tCL (activate, then read). A read of a bank with a different row open costs tRP + tRCD + tCL (precharge, activate, read). For DDR4-3200 22-22-22 that is 13.75, 27.5 and 41.25 ns.

**How much bandwidth does an 8-channel DDR5-4800 socket have?** 4800 MT/s × 8 bytes × 8 channels = 307 GB/s peak; about 80% of that, 245 GB/s, is realistic for sequential reads.

**Why does a DDR burst deliver exactly one cache line?** 64-bit bus × burst length 8 = 64 bytes for DDR4; DDR5 uses two 32-bit subchannels with burst length 16, again 64 bytes.

**What do banks and bank groups give you?** Parallelism: while one bank opens a row, another transfers data. Back-to-back accesses to different bank groups are allowed closer together than to the same group, so mappings rotate through groups first.

**What does the memory controller do?** It decodes addresses, queues reads and writes, schedules commands under the timing rules (FR-FCFS: row hits first, then oldest, with age and streak caps), batches writes to avoid bus turnarounds, schedules refresh, and is where interference between cores and devices appears.

**State Little's law for memory and use it.** Bytes in flight = bandwidth × latency. One DDR4-3200 channel at 100 ns needs 2,560 bytes (40 lines) in flight. A core has about 10 to 16 fill buffers plus prefetch, so one core reaches 10 to 20 GB/s and a server socket needs many cores to saturate its memory; a dependent pointer chase has one line in flight and gets about 0.5 GB/s.

**How do you tell a latency-bound program from a bandwidth-bound one, and what do you do?** Compare the achieved bandwidth with the machine's STREAM figure, and look at IPC. Latency-bound (low bandwidth, low IPC): increase independent requests in flight, prefetch, shrink the structure, huge pages. Bandwidth-bound (near STREAM): move fewer bytes, fuse passes, narrower types, non-temporal stores for write-once data.

**Why can STREAM's reported bandwidth be lower than what the controller sees?** STREAM counts the bytes the program reads and writes. A normal store first reads the line (read for ownership), so copy moves 24 bytes per element for 16 counted and triad 32 for 24. Non-temporal stores remove the difference.

**Why can a power-of-two stride be slow on DRAM?** With a plain bit-field mapping, a stride equal to the bank-row distance keeps the same channel, group and bank and changes only the row: every access is a row conflict in one bank, limited by tRC (~46 ns per line, 1.4 GB/s). Controllers XOR higher address bits into the bank and channel selection to break such strides.

**What are ECC, SECDED and chipkill?** ECC adds check bits (8 per 64 data bits). SECDED corrects any single-bit error and detects any double-bit error. Chipkill uses a symbol-based code to survive the failure of a whole chip. Scrubbing reads and rewrites memory in the background so single-bit errors do not pile up into double-bit ones. An uncorrectable error raises a machine-check exception.

**What is Rowhammer?** Repeatedly activating a row makes bits in the neighbouring rows flip before they are refreshed. It lets software change memory it was not given, for example page-table entries, to escalate privilege. TRR and DDR5's refresh-management features reduce it; ECC helps but is not a cure.

**What is NUMA, and how do you avoid its traps?** Memory attached to the other socket (or chiplet group) is slower and has less bandwidth. Placement is by first touch: initialise data with the threads that use it, pin threads, interleave shared data, and bind processes with `numactl`. Check with `numastat -p`. Put device buffers and polling threads on the device's node.

**What does CXL memory look like to software?** A NUMA node with no CPUs and a latency at least like a remote socket's; the OS or an application must decide which pages stay in DRAM and which go there.

**A router must forward 400 Gb/s through host memory. What memory bandwidth does that imply?** 400 Gb/s is 50 GB/s each way. Packets are written by the NIC's DMA (50 GB/s) and read out again by the NIC (50 GB/s), so at least **100 GB/s** of DRAM traffic, plus whatever the CPU reads and writes. An 8-channel DDR5-4800 socket delivers about 245 GB/s, so it uses 40% before the CPU's own accesses. With cache-injected DMA the packets may never reach DRAM, which is why the answer depends on the I/O design ([tutorial 26](26_interrupts_exceptions_and_dma.md)).

---

## 16. Problems

**How to use this section.** Try each problem before you read the answers. A to C are paper problems (25 to 30 minutes each); D is paper plus commands on your own machine.

### Problem A (paper): timings, policy and refresh

(a) DDR4-3200 has timings 22-22-22 (tCL-tRCD-tRP) and DDR5-4800 has 40-39-39. Give the clock period, tCL, tRCD and tRP in nanoseconds, and the latency of a row hit, a closed row and a row conflict for each. Which has the lower latency and by how much? Which has the higher peak bandwidth per channel and by what ratio?
(b) For DDR4-3200: a workload has 60% row hits, 30% closed-row accesses and 10% row conflicts. What is the average DRAM latency? Now compare an open-page policy with a hit rate of 70% (the other 30% are conflicts) against a closed-page policy (every access is a closed-row access). What hit rate makes the two equal?
(c) A 16 Gb DDR4 chip has tRFC = 550 ns and tREFI = 7.8 µs; a 16 Gb DDR5 chip has tRFC = 295 ns and tREFI = 3.9 µs. For each: the fraction of time lost to refresh, the peak bandwidth that remains (25.6 and 38.4 GB/s), the probability that a random read arrives during a refresh, and the average extra latency of a random read.
(d) A bank can open a new row only once per tRC = 46.25 ns, and a rank may issue only four activates per tFAW = 21 ns. DDR4-3200 has 16 banks. What is the highest bandwidth for 64-byte accesses that all need a new row? As a fraction of 25.6 GB/s?

*Where:* paper. *How:* sections 1.3, 3 and 4. Latency = cycles × clock period; average = sum of probability × cost.

### Problem B (paper, optional run): bandwidth and Little's law

(a) Peak bandwidth of: two channels of DDR5-5600; twelve channels of DDR5-4800; one HBM3 stack (1024 bits at 6.4 Gb/s per pin); a 128-bit LPDDR5-6400 interface. How many HBM3 stacks are needed for 3.2 TB/s?
(b) A core has 12 line-fill buffers (64-byte lines) and a DRAM latency of 90 ns. What is the highest bandwidth one core can draw? How many such cores are needed to use 80% of the 307 GB/s of an 8-channel DDR5-4800 socket? How many if the loaded latency rises to 150 ns?
(c) A GPU has 3 TB/s of memory bandwidth and a loaded latency of 600 ns. How many bytes must be in flight? How many 128-byte requests? If each request is one warp of 32 threads loading 4 bytes each, how many threads is that?
(d) A STREAM triad reports 80 GB/s while the memory-controller counters show 107 GB/s. Explain the difference. If the loop is rewritten with non-temporal stores and the controllers still carry 107 GB/s, what will STREAM report?
(e) A pointer chase with a latency of 100 ns walks a 4 GiB table. What bandwidth does it achieve? How many independent chains are needed to reach 20 GB/s?

*Where:* paper. *How:* sections 4, 7 and 8. Bandwidth = rate × width × channels; in flight = bandwidth × latency.

### Problem C (paper): address mapping and scheduling

Use the mapping of section 5.1 (bits 5:0 byte, bit 6 channel, 8:7 bank group, 15:9 column, 17:16 bank, 33:18 row).

(a) Decode `a = 0x4012_3440`, `b = a + 0x80`, `c = a + 0x1_0000`, `d = a + 0x4_0000` and `e = a + 0x200` into channel, bank group, bank, row and column. If they are accessed in the order a, e, b, c, d, which accesses are row hits and which go to a different bank than the one before?
(b) A program scans a 1 MB array sequentially. How many rows are opened and what is the row-hit rate?
(c) A program reads one line every 256 KB (stride 2^18). Which banks and rows does it touch? How fast can it go, if every access needs a new row in the same bank? Repeat with the hash `bank = addr[17:16] XOR addr[19:18]`. Then repeat both for a stride of 1 MB (2^20): does this hash help?
(d) One bank is idle. Requests arrive in the order R1 (row 3), R2 (row 7), R3 (row 3), R4 (row 7), R5 (row 3). With the DDR4-3200 costs of section 3.1, what is the total DRAM time for FCFS and for FR-FCFS? Which request is delayed longest by FR-FCFS?

*Where:* paper. *How:* sections 3.1, 5 and 6.2. Shift and mask for the fields; count hit, closed and conflict for each request in order.

### Problem D (paper and your machine): ECC, fleets and NUMA

(a) How many check bits does a single-error-correcting Hamming code need for 64 data bits (the condition is 2^r ≥ data + r + 1)? How many for SECDED? What is the overhead? Repeat for 128 data bits.
(b) A data centre has 20,000 servers with 16 DIMMs each. If 8% of DIMMs log correctable errors in a year and an affected DIMM logs about 4,000 per year, how many DIMMs are affected and how many correctable errors are logged per day? Why is a patrol scrubber worth its cost?
(c) A two-socket server has local latency 90 ns and remote latency 150 ns. A 64 GB array is initialised by one thread on node 0, then 64 threads (32 on each socket) read it at uniformly random positions. (i) What fraction of the accesses is remote and what is the average latency? (ii) What if each thread initialised and then used its own 1 GB part? (iii) What if the array is interleaved across both nodes? (iv) Each node's memory can deliver 150 GB/s: what is the total bandwidth ceiling in (i) and (ii)?
(d) *On your machine:* on Linux run `lscpu | grep -i numa`, `numactl --hardware` (if installed), `sudo dmidecode -t memory | grep -E 'Type:|Speed|Size|Rank|Error'`; on a Mac run `sysctl hw.memsize` and `system_profiler SPMemoryDataType`. From the DIMM speed and the number of channels, compute the peak bandwidth. *(Optional)* Download the single-file `stream.c`, compile it with `gcc -O3 -march=native -fopenmp -DSTREAM_ARRAY_SIZE=80000000 stream.c -o stream`, run it with `OMP_NUM_THREADS` set to 1, 2, 4, ..., and note where the triad stops growing. What fraction of the peak is that?

*Where:* paper for (a) to (c); a terminal for (d). *How:* sections 9, 11 and 4. Average latency = fraction local × local + fraction remote × remote.

---

## Answers

### Problem A

(a) **DDR4-3200:** clock 1600 MHz → 0.625 ns; tCL = tRCD = tRP = 22 × 0.625 = 13.75 ns; hit 13.75 ns, closed 27.5 ns, conflict 41.25 ns. **DDR5-4800:** clock 2400 MHz → 0.417 ns; tCL = 40 × 0.417 = 16.7 ns, tRCD = tRP = 39 × 0.417 = 16.25 ns; hit 16.7 ns, closed 16.25 + 16.7 = 32.9 ns, conflict 16.25 + 16.25 + 16.7 = 49.2 ns. DDR4 has the lower latency, by 21% (hit), 20% (closed) and 19% (conflict); DDR5 has 38.4 / 25.6 = **1.5 times** the bandwidth. More bandwidth, not lower latency.

(b) Mix: 0.6 × 13.75 + 0.3 × 27.5 + 0.1 × 41.25 = 8.25 + 8.25 + 4.125 = **20.6 ns**. Open page with 70% hits: 0.7 × 13.75 + 0.3 × 41.25 = 9.625 + 12.375 = **22.0 ns**; closed page: **27.5 ns** always. Equal when h × 13.75 + (1 − h) × 41.25 = 27.5, so 41.25 − 27.5 h = 27.5, h = 13.75 / 27.5 = **0.5**. Above 50% hits, keep rows open.

(c) DDR4 16 Gb: lost 550 / 7,812 = **7.0%**; remaining 25.6 × 0.930 = **23.8 GB/s**; probability that a read arrives during a refresh 7.0%; the wait is on average half of 550 = 275 ns, so the average extra latency is 0.070 × 275 = **19 ns** (worst case +550 ns). DDR5 16 Gb: 295 / 3,906 = **7.6%**; 38.4 × 0.924 = **35.5 GB/s**; 7.6% chance; average wait 147.5 ns; average extra **11 ns** (worst case +295 ns). DDR5 loses a little more bandwidth to refresh but each stall is shorter (and same-bank refresh shortens it further).

(d) tFAW: 4 activates per 21 ns = 190 million per second × 64 B = **12.2 GB/s**. The banks alone allow 16 / 46.25 ns = 346 million per second = 22.1 GB/s, so tFAW is the binding limit. 12.2 / 25.6 = **48%** of the peak. A random-row workload gets about half the bandwidth of a streaming one.

### Problem B

(a) 5600 × 8 B × 2 = **89.6 GB/s**; 4800 × 8 B × 12 = **460.8 GB/s**; HBM3: 1024 bits = 128 B × 6.4 G = **819.2 GB/s**; LPDDR5-6400 128-bit: 16 B × 6400 M = **102.4 GB/s**. For 3.2 TB/s: 3,200 / 819.2 = 3.9, so **4 stacks**.

(b) One core: 12 × 64 B / 90 ns = 768 B / 90 ns = **8.5 GB/s**. 80% of 307.2 = 245.8 GB/s; 245.8 / 8.53 = 28.8, so **29 cores**. At 150 ns: 768 / 150 = 5.12 GB/s per core and 245.8 / 5.12 = **48 cores**. (Prefetchers let a real core keep more than its fill buffers in flight, so real servers need somewhat fewer; the principle stands: latency under load raises the core count needed.)

(c) 3 × 10^12 B/s × 600 × 10^-9 s = **1.8 MB** in flight. 1.8 MB / 128 B = **14,063 requests**. With 32 threads per request: 14,063 × 32 = **450,000 threads** (1.8 MB / 4 B). This is why a GPU keeps hundreds of thousands of threads resident.

(d) The program counts 24 B per element, but each store first reads its line (read for ownership), so the memory system moves 32 B per element: 80 × 32 / 24 = **106.7 GB/s**. With non-temporal stores the read for ownership disappears and the traffic is 24 B per element; at the same 107 GB/s of controller traffic, STREAM reports **107 GB/s**, a third higher.

(e) One 64-byte line per 100 ns = **0.64 GB/s**. For 20 GB/s: 20 × 10^9 × 100 × 10^-9 = 2,000 B = **31 chains** (31.25 lines in flight). One core can hold only 10 to 16 or so, so it takes **two to three cores** to reach 20 GB/s with independent chases.

### Problem C

(a) Decoding (fields from the table of section 5.2):

| | Channel | Bank group | Column | Bank | Row |
|---|---|---|---|---|---|
| a `0x4012_3440` | 1 | 0 | 26 | 2 | 4,100 |
| b `a + 0x80` | 1 | 1 | 26 | 2 | 4,100 |
| c `a + 0x1_0000` | 1 | 0 | 26 | 3 | 4,100 |
| d `a + 0x4_0000` | 1 | 0 | 26 | 2 | 4,101 |
| e `a + 0x200` | 1 | 0 | 27 | 2 | 4,100 |

Order a, e, b, c, d: a opens its row (a closed-row access, first use). **e**: same channel, bank group, bank and row as a (only the column differs): a **row hit**. **b**: bank group 1: a different bank than the one before, and its row is not yet open: a closed-row access in another bank. **c**: bank 3 (group 0): a different bank, a closed-row access. **d**: back to the bank of a, e (group 0, bank 2), but row 4,101: the row opened by a is still open, so d is a **row conflict**. Different bank than the previous access: b and c.

(b) 1 MB = 16,384 lines. Each row holds 128 lines (8 KB), so a sequential scan opens 1 MB / 8 KB = **128 rows** (always spread over the 8 channel/group combinations and the four banks) and the other 16,384 − 128 accesses are hits: hit rate 16,256 / 16,384 = **99.2%**.

(c) Stride 2^18 = 256 KB: bits 17:0 never change, so the channel, bank group and bank are the same every time and the row increases by 1 each access: **one bank, a new row every access**. The bank can open a row once per tRC = 46.25 ns, so 64 B / 46.25 ns = **1.4 GB/s**. With the hash, the bank is `[17:16] XOR [19:18]`; `[19:18]` is the row's low two bits and counts 0, 1, 2, 3, 0, ..., so the accesses rotate over **four banks**, each opening a row at most once per tRC: about four times faster, 5.5 GB/s. Stride 2^20 = 1 MB: the changing bits are 20 and up, so bits 19:18 stay 0; the XOR does nothing, and it is **one bank again, 1.4 GB/s**. This hash does not help; real hashes fold in many more high bits (here, bits 21:20 and above, too).

(d) FCFS: R1 closed 27.5; R2 conflict 41.25; R3 conflict 41.25; R4 conflict 41.25; R5 conflict 41.25: **192.5 ns**. FR-FCFS: R1 closed 27.5 (opens row 3); then the row hits R3 and R5, 13.75 each; then R2 (conflict, 41.25), then R4 (hit, 13.75): 27.5 + 13.75 + 13.75 + 41.25 + 13.75 = **110.0 ns**, 43% less. The request delayed longest is **R2**: it completes after 27.5 + 13.75 + 13.75 + 41.25 = 96.25 ns instead of 68.75 ns under FCFS, because R3 and R5 jump ahead of it.

### Problem D

(a) 64 data bits: r = 6 gives 64 < 71; r = **7** gives 128 ≥ 72, so **7** check bits for single-error correction; SECDED adds one overall parity bit: **8**, overhead 8 / 64 = **12.5%**. 128 data bits: r = 8 gives 256 ≥ 137, so 8 + 1 = **9** bits, overhead 9 / 128 = **7.0%**. A wider code word costs less per data bit.

(b) DIMMs: 20,000 × 16 = 320,000; affected: 0.08 × 320,000 = **25,600 DIMMs** per year; errors: 25,600 × 4,000 = 102.4 million per year = about **280,000 correctable errors per day**, so the fleet needs automatic logging and replacement. A patrol scrubber costs a little background bandwidth and power and prevents a corrected single-bit error from combining with a second flip into an uncorrectable one, which would crash a machine or kill a process.

(c) (i) All pages are on node 0. The 32 threads on node 0 are local, the 32 on node 1 always remote: **50% remote**; average 0.5 × 90 + 0.5 × 150 = **120 ns**. (ii) All accesses local: **90 ns**, a 1.33 times lower latency; in a latency-bound loop about 33% faster. (iii) Interleaved uniformly: half of every thread's accesses are remote, **120 ns** on average, the same as (i), but each controller carries half the traffic instead of node 0 carrying all of it. (iv) In (i) all data are served by node 0's memory: **at most 150 GB/s** in total (and node 1's share has to cross the interconnect, which may be lower still). In (ii) each node serves its own threads: up to **300 GB/s**. Interleaving gives about 300 GB/s of controller bandwidth too, but half of it crosses the interconnect.

(d) Peak bandwidth = DIMM speed (MT/s) × 8 B × channels, e.g. DDR4-3200 in 2 channels = 51.2 GB/s. The triad rate stops growing after a few threads (a desktop: 2 to 4), at roughly 70 to 85% of that figure; a laptop or Apple chip reports its own peak (Apple M-series have 100 GB/s and more from LPDDR5 on wide interfaces). Write down your own numbers.

---

## Cheat sheet

- **Cells:** SRAM 6T (static, fast, big); DRAM 1T1C (leaky, dense). A DRAM read is destructive: the sense amplifiers hold the whole row (**row buffer**) and restore it.
- **Refresh:** 64 ms (DDR4) / 32 ms (DDR5), 8,192 REFs, tREFI 7.8 / 3.9 µs; lost time = tRFC / tREFI: 4.5% (8 Gb DDR4), 7 to 10% for 16 to 32 Gb. Costs bandwidth, tail latency and power.
- **Organisation:** channel → DIMM → rank → chip → bank group → bank → row × column. DDR4 16 banks (4 × 4), DDR5 32 (8 × 4). Row 8 KB per rank. A burst is one 64 B line (DDR4 BL8 × 8 B; DDR5 BL16 × 4 B on a subchannel).
- **Commands and timing:** ACT, RD/WR, PRE, REF. Hit = tCL; closed = tRCD + tCL; conflict = tRP + tRCD + tCL. DDR4-3200 22-22-22: 13.75 / 27.5 / 41.25 ns. DDR5-4800 40-39-39: 16.7 / 32.9 / 49.2 ns. Latency in ns has not improved; bandwidth has.
- **Bandwidth:** rate × 8 B × channels: DDR4-3200 25.6 GB/s; DDR5-4800 38.4; 8 channels 307; 12 channels 461; HBM3 819 GB/s per stack; HBM3e about 1.2 TB/s; LPDDR5-6400 256-bit 205 GB/s. Achieved 70 to 90%; random-row traffic about half (tFAW, tRC).
- **Mapping:** low bits channel then bank group (parallelism), middle bits column (row hits), high bits row. XOR hashing breaks power-of-two strides. The OS controls bits 12 and up (bank colouring).
- **Controller:** read and write queues; FR-FCFS (row hits first, then oldest, with caps); write drain with watermarks; postponed refresh; interference between cores; bandwidth monitoring and allocation (MBM/MBA, MPAM).
- **Little's law:** bytes in flight = bandwidth × latency. 25.6 GB/s × 100 ns = 40 lines. One core: 12 GB/s × 133 ns ≈ 26 lines; a chase: 1 line, 0.5 GB/s. A socket needs about 20 streaming cores; a GPU needs megabytes in flight. Latency flat to about 50% of the achievable bandwidth, steep above 70 to 80%.
- **Measured here (ranges):** DRAM dependent hop 120 to 135 ns (2 MB pages; 45 to 75 ns more with a TLB miss); open-row saving 25 to 30 ns ≈ tRP + tRCD; row-buffer reach a few hundred KB; STREAM 1 thread: read 12.2 GB/s, copy 10.6 to 10.9, triad 12.1 to 12.3 (actual traffic 16), 2 threads 1.7 to 2.0 times; a competing 7 to 10 GB/s did not change chase latency; latency tail: 67% in 100 to 150 ns, 10% over 250 ns.
- **STREAM:** copy, scale (16 B counted), add, triad (24 B); real traffic 24 and 32 B because of read-for-ownership; non-temporal stores avoid it; arrays at least 4× the LLC; best of N; pin; first touch.
- **Reliability:** ECC DIMM 72 bits per 64; SECDED corrects 1, detects 2, UE gives a machine-check exception; chipkill survives a chip; patrol scrubbing; DDR5 on-die ECC is internal only. Over 8% of DIMMs show errors per year. `edac`, `rasdaemon`, `dmidecode`.
- **Rowhammer:** activating a row repeatedly flips bits in neighbours; page-table entry flips escalate privilege; TRR, DDR5 refresh management and ECC reduce but do not remove it.
- **Other memories:** flash (block erase, page program, wear); persistent memory (`CLWB` + fence); CXL (a CPU-less NUMA node); PIM (compute inside the DRAM).
- **NUMA:** local vs remote (1.3 to 2× latency, less bandwidth); first-touch places the page; fixes: parallel first touch, `numactl --interleave/--cpunodebind/--membind`, pin threads, `numastat -p`; automatic balancing costs faults and shootdowns; devices have a home node. This VM has one node, so NUMA was not measured.

**Next:** tutorial 17, Storage and Reliability (hard-disk geometry, SSD and NAND flash, the flash translation layer, wear levelling and write amplification, RAID 0, 1, 5, 6, 10, MTTF and availability, and error-correcting codes: parity, Hamming and SEC-DED, CRC and checksums, by hand). Say "next" when you are ready.
