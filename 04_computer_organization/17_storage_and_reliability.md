# CO 17: Storage and Reliability

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 03](03_boolean_logic_and_combinational_circuits.md) (XOR), [tutorial 04](04_sequential_logic_and_state_machines.md) (the LFSR, section 6) and [tutorial 16](16_dram_memory_controllers_and_numa.md) (ECC and the flash paragraph)*

By the end of this tutorial you can:

- place DRAM, NVMe and SATA SSDs, disks and network storage on one latency scale, and say what each is good for;
- compute the average access time of a hard disk from its rotation speed and seek time, and explain why random access is hundreds of times slower than sequential;
- explain NAND flash (pages, blocks, erase before write), the flash translation layer, garbage collection, wear levelling and **write amplification**, and compute the last two;
- explain NVMe queues and use Little's law to relate IOPS, latency and queue depth;
- explain RAID 0, 1, 5, 6 and 10: capacity, fault tolerance, the small-write penalty and why rebuilds are risky; rebuild a lost block with XOR;
- compute MTBF-based failure rates, availability ("nines") and the effect of series and parallel designs;
- encode and decode a **Hamming** code by hand, and explain SEC-DED;
- compute a **CRC** by hand, explain what it detects, and say where CRC-32 appears (Ethernet and Wi-Fi frames, storage); compute an Internet checksum;
- say what each layer of a storage write (application, file system, driver, SSD) does to make the data durable and intact.

Tutorial 16 ended at the memory bus. Everything on this side of the chip loses its contents when the power goes; **storage** does not. It is slower by a factor of a thousand to a million, and it is built on two ideas that this tutorial teaches together: *non-volatile devices have odd rules* (a disk has a moving arm, flash cannot be overwritten), and *every layer protects its data with redundancy*, either extra bits (ECC, CRC) or extra devices (RAID). The arithmetic of that redundancy is XOR.

**Where the numbers come from.** The by-hand parts (RAID, Hamming, CRC, checksums, failure arithmetic) are exact, and every answer was checked with a script. Timings marked *measured* were taken on the cloud virtual machine of tutorials 14 to 16. **Its disk is a virtual block device (`virtio`), not a flash chip**: every I/O passes through the hypervisor and probably a host cache, so the numbers show the shape of storage behaviour (queue depth, the cost of making data durable), not the speed of any particular SSD. Cloud numbers also drift between runs, so measured figures are ranges over three runs.

> **The one-sentence model.** Storage is cheap, huge and persistent but a thousand times slower than memory, its devices have awkward physics (seek arms, erase blocks) that software and firmware hide, and every layer protects the data with redundancy: extra bits that detect or correct errors, and extra devices that survive failures.

---

## 1. The storage hierarchy, in numbers

| Device | Latency of a random 4 KB read | Sequential bandwidth | Persistent? |
|---|---|---|---|
| DRAM (tutorial 16) | 0.1 µs (100 ns) | 25 to 300+ GB/s | no |
| NVMe SSD | 70 to 100 µs (bare metal) | 3 to 14 GB/s | yes |
| SATA SSD | about 100 µs | 0.55 GB/s (the SATA limit) | yes |
| Network block or file storage | 0.5 to 2 ms | 0.1 to 1 GB/s per client | yes |
| Hard disk | 5 to 15 ms | 0.15 to 0.28 GB/s | yes |

Between DRAM and a hard disk there are **five orders of magnitude** (100 ns to 10 ms). An NVMe SSD is about 1,000 times slower than DRAM but 100 times faster than a disk; this is why SSDs changed system design, and why the gap to DRAM still forces caching (the page cache, tutorial 15) in every operating system.

What *persistent* adds: data written to a file are not safe until they reach the non-volatile medium. Between the program and the medium sit the page cache, the device's own write cache, and the flash translation layer, and each can lose data on power failure unless asked to flush (section 9).

---

## 2. Hard disks, briefly

A hard disk has spinning **platters** with a magnetic surface, divided into concentric **tracks** and each track into **sectors** (512 B or 4 KB). A moving **arm** carries the read/write head to the right track. One access costs three parts:

```text
access time = seek time (move the arm) + rotational latency (wait for the sector) + transfer time
seek:        about 4 to 9 ms on average (less for nearby tracks)
rotation:    on average half a turn  =  0.5 × 60 / RPM     (7,200 RPM: 4.17 ms;  15,000 RPM: 2 ms)
transfer:    4 KB at 200 MB/s = 20 µs: negligible
```

**Worked example.** A 7,200 RPM disk with an 8 ms average seek: rotational latency 0.5 × 60 / 7,200 = 4.17 ms, so a random 4 KB access takes 8 + 4.17 + 0.02 = **12.2 ms**, which is **82 operations per second**, or 4 KB × 82 = **0.34 MB/s**. The same disk reading sequentially delivers about 200 MB/s. **Random access is about 600 times slower than sequential access.**

Consequences that shaped software: file systems and databases lay data out **sequentially** and write **logs** (append-only), the I/O scheduler sorts requests by position (the "elevator"), and data structures such as the LSM tree turn random writes into sequential ones ([storage engines](../11_hld/01_fundamentals/09_storage_engines.md)). Disks remain the cheapest bytes per dollar, for cold and bulk data.

(On a virtual machine, `/sys/block/vda/queue/rotational` reads `1` even though the host's storage may be flash: the virtual device reports the default. Do not trust that flag in the cloud.)

---

## 3. Flash memory and SSDs

### 3.1 The NAND cell and its rules

A NAND flash cell is a transistor with a floating gate that stores charge; the amount of charge sets its threshold voltage. A cell with 2 charge levels stores 1 bit (**SLC**), 4 levels 2 bits (**MLC**), 8 levels 3 bits (**TLC**), 16 levels 4 bits (**QLC**). More bits per cell means more capacity per dollar, but the levels are closer together: slower, more errors, fewer lifetimes.

| | Bits per cell | Read | Program (write) | Erase | Endurance (program/erase cycles) |
|---|---|---|---|---|---|
| SLC | 1 | about 25 µs | 200 µs | 2 to 5 ms | 50,000 to 100,000 |
| MLC | 2 | 50 µs | 600 µs | 3 to 6 ms | 3,000 to 10,000 |
| TLC | 3 | 50 to 100 µs | 0.5 to 1+ ms | 3 to 10 ms | 1,000 to 3,000 |
| QLC | 4 | 100+ µs | 1+ ms | 5 to 10 ms | a few hundred to 1,000 |

(Typical orders of magnitude; they vary by generation and vendor.) The organisation, and the three rules that follow from it:

- The **page** (4 to 16 KB) is the unit of **read and program**.
- The **block** (hundreds to a couple of thousand pages, a few MB or more) is the unit of **erase**.
- **A programmed page cannot be overwritten**. To change it, the whole block must first be **erased** (every bit returns to 1). Pages in a block are programmed in order.

A disk can overwrite a sector in place. Flash cannot, and an erase is thousands of times slower than a read and wears the cell. Everything about SSD design is about hiding this.

### 3.2 The flash translation layer (FTL)

The SSD presents an array of logical blocks (LBAs) that the OS can read and overwrite freely. Inside, the **flash translation layer**, firmware on the SSD's controller, makes that illusion true:

- It keeps a **mapping table** from logical page to physical page. A write of logical page 7 goes to a **free physical page** (a log-structured write), the table entry is changed, and the old physical page is marked **invalid** (stale).
- The mapping table is large: one 4-byte entry per 4 KB page is **1 MB per GB**, so a 4 TB drive has a table of about 4 GB, kept in the SSD's own DRAM. A journal or checkpoints, plus capacitors that give the controller time to save state on power loss, keep it consistent.

### 3.3 Garbage collection and write amplification

Writing only to free pages means that blocks fill with a mix of valid and invalid pages. When free blocks run low, **garbage collection** picks a **victim** block, copies its still-valid pages to a fresh block, and erases it. Those copies are extra writes that the host never asked for.

**Write amplification (WA)** = bytes written to NAND ÷ bytes written by the host.

If the victim block is a fraction *u* valid, then freeing a block's worth of space (1 − *u*) costs copying *u* of a block, so

```text
WA = 1 / (1 − u)
```

**Worked example.** A block has 256 pages and the victim has 192 valid ones (u = 0.75). Collecting it frees 64 pages but costs 192 page copies. The host wrote 64 new pages into the freed space; the NAND wrote 192 + 64 = 256. WA = 256 / 64 = **4** (= 1 / (1 − 0.75)).

What lowers WA:

- **Over-provisioning**: extra physical capacity the host cannot see (7% or 28% or more). More free space means victims are emptier (smaller *u*).
- **TRIM** (also called discard; `fstrim`, or the `discard` mount option, which this VM's root file system has): the OS tells the SSD which logical blocks it no longer uses, so the FTL can treat them as invalid instead of copying them.
- **Sequential or large writes** and data that die together (a log that is deleted whole): victims are fully invalid, WA near 1.
- **Not filling the drive**: a drive at 95% full has victims with high *u*, which is why a full SSD slows down and wears faster.

### 3.4 Wear levelling and endurance

Each block survives a limited number of erase cycles. If software kept rewriting the same logical page, the FTL would already move it each time, but data that never change (the operating system's files) would sit in the same blocks forever while the others wear out. **Wear levelling** spreads the erases: *dynamic* wear levelling chooses the least-worn free block for new writes, and *static* wear levelling occasionally moves cold data into worn blocks so that the young blocks get used. The controller also retires **bad blocks**, and reads that stop being reliable are retried and corrected by strong ECC (BCH or LDPC, section 7.5).

**Endurance arithmetic.** Host data that can be written before wear-out = capacity × cycles ÷ WA.

A 1 TB TLC drive rated at 1,000 cycles with WA = 3: 1 TB × 1,000 / 3 = **333 TB written** (TBW). At 100 GB per day that lasts 3,333 days, **9.1 years**. The vendors quote this as TBW or as **DWPD** (drive writes per day over the warranty). A log-heavy or database-heavy workload with WA of 10 shortens it by more than three times.

### 3.5 Raw flash in embedded devices

SSDs, eMMC, UFS and SD cards contain an FTL inside: the OS sees a block device. Many embedded products (routers, access points, set-top boxes) instead use **raw flash**, where the software must do what the FTL does:

- **NOR flash**: byte-addressable reads, so code can run directly from it (execute-in-place) and it holds the bootloader; slow sector erases; small (megabytes).
- **Raw NAND**: managed by Linux's **MTD** layer and a flash file system such as **UBI/UBIFS**, which does wear levelling, bad-block management and power-loss safety in software. The SoC's NAND controller computes the ECC (BCH, for example 4 or 8 bits per 512-byte chunk).
- A read-only `squashfs` root plus a small writable overlay on flash is the common layout, because it minimises writes and wear.

### 3.6 Measured: what a (virtual) disk does

A small C program on a 1 GiB file, with `O_DIRECT` (bypassing the page cache). *Measured*, range over three runs on this VM's virtual disk:

| Operation | Result |
|---|---|
| sequential read, 1 MiB requests | 2.0 to 2.9 GB/s |
| sequential write, 1 MiB requests, then `fsync` | 0.57 to 1.8 GB/s |
| random 4 KiB read, one at a time (queue depth 1) | mean 33 to 43 µs; median 33 to 40 µs; 99th percentile 75 to 106 µs |
| random 4 KiB write, `O_DIRECT`, queue depth 1 | mean 88 to 205 µs; median 87 to 93 µs; 99th percentile 139 to 285 µs |
| random 4 KiB write into the page cache (no sync) | mean 2.4 to 2.7 µs |
| random 4 KiB write followed by `fdatasync` | median 255 to 269 µs; mean 0.54 to 0.81 ms; 99th percentile 0.55 to 6.8 ms; **worst 0.7 to 1.4 s** |

Three lessons:

1. **Durability costs about a hundred times a cached write**: 2.5 µs to be in the page cache, 250 µs and more to be told that it reached the device. A program that calls `fsync` after every record is limited to a few thousand records per second here; databases batch commits for this reason (group commit).
2. **The tail is huge**: the median flush is 260 µs and the worst one took more than half a second. Storage latency budgets are about percentiles, not means.
3. A read from the device takes tens of microseconds even with nothing else going on: a thousand times DRAM (tutorial 16).

Then the same random 4 KiB reads with more of them in flight (threads, each issuing one read at a time), 2 virtual CPUs:

| Requests in flight | IOPS | Mean latency |
|---|---|---|
| 1 | 23,000 to 28,000 | 36 to 43 µs |
| 2 | 46,000 to 50,000 | 40 to 44 µs |
| 4 | 78,000 to 87,000 | 46 to 51 µs |
| 8 | 112,000 to 125,000 | 64 to 71 µs |
| 16 | 133,000 to 166,000 | 96 to 120 µs |
| 32 | 107,000 to 146,000 | 219 to 298 µs |

**Little's law applies to storage exactly as to memory (tutorial 16, section 7):** requests in flight = IOPS × latency. At 16 in flight: 150,000 IOPS × 105 µs = 16. Throughput grows almost in proportion to the queue depth while the device has spare capacity (1 to 8 requests), then flattens at about 130,000 to 160,000 IOPS (about 0.5 to 0.7 GB/s of 4 KB reads), and beyond that more requests only add queueing delay (latency rises from 100 to 300 µs for no more throughput; here the two vCPUs running the threads are also a limit). The general rule: **a flash device needs many requests in flight to show its rated IOPS**, and a program that issues one synchronous read at a time gets only a sixth of them on this VM (and far less on a real NVMe drive, which has much more internal parallelism).

---

## 4. NVMe: how the CPU talks to a fast SSD

Older SATA drives used the AHCI interface: **one queue of 32 commands**, shared by all cores under a lock, over a 6 Gb/s link (600 MB/s). That is too narrow and too serialised for flash. **NVMe** is the interface designed for it, over PCIe ([tutorial 25](25_buses_pcie_and_io_mapping.md)):

- Memory-resident **queue pairs**: a *submission queue* (SQ) of 64-byte commands and a *completion queue* (CQ) of 16-byte results, in host DRAM. The specification allows up to **65,535 I/O queue pairs with up to 65,536 entries each**.
- To issue a command the driver writes it into the SQ and writes the new tail index into a **doorbell register** on the device (one store to a memory-mapped register). The SSD reads the command by DMA, performs it, writes a completion entry to the CQ and raises an **interrupt** (MSI-X, one vector per queue; [tutorial 26](26_interrupts_exceptions_and_dma.md)).
- A typical driver creates **one queue pair per CPU core**, so submission needs no lock and the completion interrupt goes to the core that issued the request. This per-core design is why NVMe scales to millions of IOPS.

The link: PCIe Gen4 ×4 carries 16 GT/s × 4 lanes × (128/130) ÷ 8 = **7.88 GB/s**; Gen5 ×4 carries 15.75 GB/s. At 4 KB per request, 7.88 GB/s is 1.9 million IOPS. By Little's law, a drive delivering 1 million IOPS at 80 µs needs 80 requests in flight, which is why NVMe supports so many queues and why software uses asynchronous I/O (`io_uring`) or polling (SPDK) to keep them full.

At these speeds the **software** is the cost: a system call, the block layer and an interrupt take a few microseconds, a noticeable part of an 80 µs read, and most of the latency of the fastest devices.

---

## 5. RAID: surviving a dead disk

### 5.1 The idea and the levels

**RAID** (redundant array of independent disks) combines several drives into one logical volume to get capacity, speed, and survival of drive failures. The two tools are **striping** (split data into chunks and spread them across the drives: more bandwidth and capacity) and **redundancy** (a mirror copy, or parity).

For *N* drives of capacity *C*:

| Level | Layout | Usable capacity | Drive failures survived | Notes |
|---|---|---|---|---|
| **RAID 0** | stripe only | *N* × *C* | **0** | fastest; one failure loses everything |
| **RAID 1** | mirror (every block on two or more drives) | *C* (for a pair) | *N* − 1 | reads can use either copy; writes go to all |
| **RAID 5** | stripe with one distributed **parity** block per stripe | (*N* − 1) × *C* | **1** | small-write penalty; risky rebuild |
| **RAID 6** | stripe with two parity blocks per stripe | (*N* − 2) × *C* | **2** | survives a failure during a rebuild |
| **RAID 10** | stripe of mirrored pairs | *N* / 2 × *C* | 1 guaranteed (up to *N* / 2 if each failure is in a different pair) | fast writes; half the capacity |

For *N* = 8 and *C* = 4 TB: RAID 0 gives 32 TB, RAID 5 28 TB, RAID 6 24 TB, RAID 10 16 TB.

### 5.2 Parity is XOR

In RAID 5 each stripe has *N* − 1 data blocks and one **parity block** that is the XOR of the data blocks. Because XOR is its own inverse (tutorial 03), any one missing block is the XOR of all the others.

**Worked example.** Three data drives hold the bytes `D0 = 0xA5`, `D1 = 0x3C`, `D2 = 0xF0` in one stripe.

```text
P  = D0 ^ D1 ^ D2 = 0xA5 ^ 0x3C ^ 0xF0 = 0x69
Drive 1 fails. Rebuild:  D1 = D0 ^ D2 ^ P = 0xA5 ^ 0xF0 ^ 0x69 = 0x55 ^ 0x69 = 0x3C   ✓
```

The parity block rotates between the drives from stripe to stripe, so no single drive takes all the parity writes.

### 5.3 The small-write penalty

Overwriting one data block of a RAID 5 stripe must also change the parity, without reading the whole stripe. The trick is the XOR identity *new parity = old parity ^ old data ^ new data*:

```text
1. read the old data block          D1  = 0x3C
2. read the old parity block        P   = 0x69
3. compute  P' = P ^ D1 ^ D1'        with D1' = 0x0F:  0x69 ^ 0x3C ^ 0x0F = 0x5A
4. write the new data block, write the new parity block
```

That is **4 disk operations for one logical write** (2 reads and 2 writes). RAID 6 needs 6; RAID 1 and RAID 10 need 2. Effective random-write rate = raw rate ÷ penalty: eight SSDs that can each do 50,000 random writes per second give 400,000 raw; RAID 10 delivers 200,000, RAID 5 100,000 and RAID 6 66,700. A **full-stripe write** avoids the problem: parity is computed from the new data alone, with no reads, so the cost is only *N* / (*N* − 1) writes (1.14 for 8 drives). That is why parity RAID likes large sequential writes and suffers on small random ones.

### 5.4 RAID 6

RAID 6 stores two independent parities per stripe: **P**, the XOR of the data, and **Q**, a weighted sum computed in a finite field (arithmetic on bytes modulo a polynomial, the Reed–Solomon construction of section 7.5). Two independent equations can solve for any two missing blocks. It costs another drive of capacity and a 6-operation small write, and it is what large arrays use.

### 5.5 Why rebuilds are dangerous

When a RAID 5 drive fails, the array runs **degraded**, with no redundancy left. To restore redundancy, the controller reads **every surviving drive in full** and writes a spare.

- **Time.** Rebuilding a 4 TB drive at 100 MB/s takes 4 × 10^12 / 10^8 = 40,000 s = **11 hours**, longer if the array keeps serving users.
- **Unrecoverable read errors (UREs).** Drives specify a rate of unreadable sectors, for example 1 in 10^14 bits for consumer drives and 1 in 10^15 to 10^16 for enterprise drives. For the 8 × 4 TB RAID 5, the rebuild reads 7 × 4 TB = 28 TB = 2.24 × 10^14 bits. The expected number of UREs at 1 per 10^14 bits is 2.24, so the probability of **at least one** is 1 − e^(−2.24) = **89%**; at 1 per 10^15 it is 20%, at 1 per 10^16 it is 2.2%. One URE during a rebuild means a block cannot be reconstructed. (Specified rates are conservative and real errors are clustered, but the arithmetic shows why large RAID 5 arrays of consumer disks are discouraged.)
- **Correlated failures.** Drives from the same batch, of the same age, in the same chassis, tend to fail together; and the rebuild itself stresses them.
- **The write hole.** If power fails between the data write and the parity write of one stripe, the stripe is inconsistent, and a later rebuild computes wrong data. Cures: a battery-backed or journalled write log, or a copy-on-write layout (ZFS RAID-Z) that never updates in place.

RAID 6 (or 3-way mirroring) exists mainly for this reason: a second parity means one URE or one more failure during a rebuild is still survivable.

### 5.6 Beyond RAID

Large distributed systems spread data across machines and racks instead of drives in a box: **replication** (typically 3 copies: 3× the storage, simple, fast recovery) or **erasure coding** (*k* data fragments plus *m* parity fragments, for example 10 + 4: survives any 4 losses at only 1.4× the storage, at the price of reading *k* fragments to rebuild and more CPU). It is the same XOR and Reed–Solomon arithmetic. The design side belongs to the [storage engines](../11_hld/01_fundamentals/09_storage_engines.md) and distributed-storage notes.

---

## 6. Reliability arithmetic

**MTBF** (mean time between failures) is a failure *rate* for a population in its useful life: λ = 1 / MTBF failures per hour. The yearly view is the **annualised failure rate**, AFR ≈ 8,760 hours ÷ MTBF. A drive with MTBF = 1,000,000 hours has AFR = 0.876%. That does **not** mean one drive lasts 114 years; it means that in a fleet of 10,000 such drives you expect 87.6 failures a year, **one every 4.2 days**. Failures follow a bathtub curve (early failures, a flat middle, wear-out), and real fleets often show higher rates than the data sheet.

**MTTR** (mean time to repair) is the time from failure to full redundancy again: replacement plus rebuild.

**Availability** = the fraction of time the system works = MTBF ÷ (MTBF + MTTR). It is quoted in "nines":

| Availability | Downtime per year |
|---|---|
| 99% | 87.6 hours |
| 99.9% | 8.76 hours |
| 99.99% | 52.6 minutes |
| 99.999% | 5.26 minutes |

**Series and parallel.** If *every* component must work (a request path through five services), availabilities **multiply**: five services at 99.9% give 0.999^5 = **99.5%**. If *any one* is enough (redundant servers), unavailabilities multiply: two independent 99% servers give 1 − 0.01² = **99.99%**, provided failover is instant and failures are independent (shared power, software bugs and common causes break that assumption).

**Mean time to data loss for RAID 5**: data are lost if a second drive fails during the rebuild of the first, so

```text
MTTDL ≈ MTBF² / (N × (N − 1) × MTTR)
```

For *N* = 8, MTBF = 10^6 hours, MTTR = 24 hours: 10^12 / (56 × 24) = 7.4 × 10^8 hours, about **85,000 years**. That is the idealised answer; it ignores UREs and correlated failures, which dominate in practice (section 5.5). For RAID 6 the formula is MTBF³ / (*N*(*N* − 1)(*N* − 2) MTTR²).

---

## 7. Error-correcting codes by hand

### 7.1 Distance

A **code** adds redundancy so that valid words are far apart. The **Hamming distance** *d* between two words is the number of bit positions where they differ. If every pair of valid code words differs in at least *d* positions, the code can **detect up to *d* − 1** bit errors and **correct up to ⌊(*d* − 1) / 2⌋**. The **code rate** is data bits ÷ total bits.

### 7.2 Parity

Add one bit that makes the number of 1s even (**even parity**). Valid words differ in at least 2 positions (*d* = 2): it **detects any single-bit error** (and any odd number of errors) and corrects nothing. It is an XOR tree (tutorial 03). Example: data `1011001` has four 1s, so the parity bit is 0; if one bit flips, the count becomes odd.

### 7.3 Hamming(7,4): a single-error-correcting code

Four data bits need three check bits. Number the seven positions 1 to 7. The check bits go at the **powers of two** (positions 1, 2 and 4) and the data bits fill the rest:

```text
position:   1    2    3    4    5    6    7
content:    p1   p2   D1   p4   D2   D3   D4
```

Each check bit covers the positions whose **binary number contains its bit**:

| Check bit | Covers positions | Why |
|---|---|---|
| p1 | 1, 3, 5, 7 | numbers with the 1s bit set (001, 011, 101, 111) |
| p2 | 2, 3, 6, 7 | numbers with the 2s bit set (010, 011, 110, 111) |
| p4 | 4, 5, 6, 7 | numbers with the 4s bit set (100, 101, 110, 111) |

Each check bit is chosen so that its group has an **even** number of 1s.

**Encode `1011`.** D1 D2 D3 D4 = 1 0 1 1 at positions 3, 5, 6, 7.

```text
p1 = pos3 ^ pos5 ^ pos7 = 1 ^ 0 ^ 1 = 0
p2 = pos3 ^ pos6 ^ pos7 = 1 ^ 1 ^ 1 = 1
p4 = pos5 ^ pos6 ^ pos7 = 0 ^ 1 ^ 1 = 0
code word (positions 1..7):  0 1 1 0 0 1 1
```

**Decode with an error.** Suppose position 5 flips in transit: received `0110111`. Recompute the three group checks (each should come out 0):

```text
c1 = pos1 ^ pos3 ^ pos5 ^ pos7 = 0 ^ 1 ^ 1 ^ 1 = 1
c2 = pos2 ^ pos3 ^ pos6 ^ pos7 = 1 ^ 1 ^ 1 ^ 1 = 0
c4 = pos4 ^ pos5 ^ pos6 ^ pos7 = 0 ^ 1 ^ 1 ^ 1 = 1
syndrome (c4 c2 c1) = 1 0 1 = 5   →   the error is at position 5: flip it back.
```

The **syndrome is the position of the error**, which is the whole beauty of the layout: the failed checks, read as a binary number, point at the bad bit. A syndrome of 0 means no error.

How many check bits? *r* check bits give 2^r − 1 non-zero syndromes, which must name every position including the check bits themselves: **2^r ≥ *m* + *r* + 1** for *m* data bits.

| Data bits *m* | Check bits *r* | Code | Overhead *r* / *m* |
|---|---|---|---|
| 4 | 3 | (7,4) | 75% |
| 11 | 4 | (15,11) | 36% |
| 64 | 7 | (71,64) | 11% |
| 128 | 8 | (136,128) | 6.3% |

### 7.4 SEC-DED: add one more parity bit

A Hamming code that corrects one error will, given **two** errors, produce a non-zero syndrome that points at the wrong position and "correct" a third bit, silently corrupting the data. Adding one **overall parity bit** over the whole word fixes this and makes the code **SEC-DED** (single-error correcting, double-error detecting; distance 4). Decode with both the syndrome and the overall parity:

| Syndrome | Overall parity check | Meaning | Action |
|---|---|---|---|
| 0 | OK | no error | none |
| ≠ 0 | **fails** (odd number of flips) | one error, at the syndrome's position | correct it |
| ≠ 0 | OK (even number of flips) | **two errors** | detect, cannot correct: report |
| 0 | fails | the overall parity bit itself flipped | ignore (data fine) |

**Example.** Extend the code word above with the overall parity of its seven bits (`0110011` has four 1s, so the extra bit is 0): `01100110`.

- Flip position 6: syndrome 6, overall parity fails → a single error at position 6, corrected.
- Flip positions 2 and 5: syndrome 2 ^ 5 = 7 but the overall parity still checks out → **double error detected**, not miscorrected.
- Flip the last (overall parity) bit only: syndrome 0, parity fails → harmless.

ECC memory uses exactly this with *m* = 64: 7 Hamming bits + 1 overall bit = **8 check bits, a (72,64) code**, the extra chip of tutorial 16 section 9. Storage and networks use the same ideas with longer codes.

### 7.5 Stronger codes in a paragraph

- **BCH** codes correct several random bit errors per block; used for NAND flash and for the NAND controllers of embedded SoCs (for example 8 bits per 512 bytes).
- **Reed–Solomon** codes work on *symbols* (bytes), so they correct **bursts**: CDs and DVDs, QR codes, RAID 6 and erasure-coded storage, chipkill memory (tutorial 16), satellite links.
- **LDPC** codes, decoded iteratively with soft information, come close to the theoretical limit; they are used in modern SSD controllers (as flash gets noisier), and in Wi-Fi (802.11n/ac/ax), 5G and DVB at the physical layer.

---

## 8. CRC and checksums

Hamming-style ECC *corrects* a few errors in a short word. Networks and storage mostly want something different: a cheap, strong check that a whole **frame or block** arrived intact, so that a bad one can be dropped and retransmitted. That is the job of the **CRC** (cyclic redundancy check).

### 8.1 What a CRC is, by hand

Treat the message bits as the coefficients of a polynomial over the two-element field (arithmetic with no carries, so addition and subtraction are both XOR). Choose a **generator polynomial** *g*(x) of degree *r*. The sender appends *r* bits, the **remainder** of the message (shifted left by *r*) divided by *g*; the receiver divides the whole received frame and accepts it only if the remainder is **zero**.

**Worked example.** *g*(x) = x³ + x + 1, i.e. the bits `1011` (*r* = 3). Message `11010110`. Append three zeros and divide, XORing the divisor under every leading 1:

```text
 11010110000      message with 000 appended
^1011
 01100110000
  ^1011
 00111110000
   ^1011
 00010010000
    ^1011
 00000100000
      ^1011
 00000001100
        ^1011
 00000000111      remainder = 111
```

The transmitted frame is `11010110` followed by `111`: **`11010110111`**. The receiver divides it by `1011` and gets remainder 000. If one bit flips in transit, for example the third bit (`11110110111`), the division leaves a **non-zero remainder** (here `010`) and the frame is rejected.

### 8.2 In hardware: an LFSR

Long division by a fixed polynomial is a **linear-feedback shift register** (tutorial 04, section 6): *r* flip-flops and one XOR per non-zero term of *g*. The message bits are shifted in one per clock, and after the last bit the flip-flops hold the remainder. A CRC-32 is 32 flip-flops and about 14 XOR gates; to handle 8, 32 or 64 bits per clock, the hardware unrolls the same logic. This is why every Ethernet and Wi-Fi MAC computes its frame check sequence in hardware at line rate, at essentially no cost.

### 8.3 What a CRC does and does not detect

- **All single-bit errors**, and with a well-chosen *g* all 2- and 3-bit errors in a frame-sized message. For the Ethernet polynomial, CRC-32 detects every 1-, 2- and 3-bit error in a maximum-size 1,518-byte frame (checked exhaustively by a script for this tutorial).
- **Every burst of errors up to *r* bits long** (32 bits for CRC-32): a short burst, the typical shape of noise on a link or a flash page, cannot slip through.
- Random garbage passes with probability 2^−*r*: **1 in 4.3 billion** for CRC-32.
- An error pattern that happens to be a multiple of *g*(x) is **invisible**: in the example, an error pattern equal to `1011` placed anywhere in the frame (three flipped bits, spaced like the 1s of the generator) leaves a zero remainder.
- **A CRC is not a security feature.** It is linear, so an attacker can alter the message and adjust the CRC to match. Use a cryptographic hash or MAC against an adversary.

### 8.4 Where the common CRCs live

| CRC | Used in |
|---|---|
| **CRC-32** (IEEE 802.3, polynomial `0x04C11DB7`) | the **frame check sequence of Ethernet and of 802.11 Wi-Fi frames**; zip, gzip, PNG |
| **CRC-32C** (Castagnoli, `0x1EDC6F41`) | iSCSI, SCTP, ext4 and btrfs metadata; has its own instruction on x86 (SSE 4.2 `crc32`) and Arm (the CRC32C instructions) |
| CRC-16 | USB, Modbus, storage protection information (T10 PI, a 16-bit CRC per 512-byte sector for end-to-end protection) |
| CRC-8, CRC-24 | small sensor buses, Bluetooth Low Energy |

In a Wi-Fi access point or an Ethernet NIC, the transmitter's MAC appends the 4-byte FCS in hardware, and the receiver's MAC checks it and silently drops frames that fail (and for unicast Wi-Fi frames sends no acknowledgement, so the sender retransmits). The counters you read for this are the FCS or CRC error counters (`ethtool -S` on a NIC).

### 8.5 Measured: speed of a CRC-32 in software

When there is no hardware offload (storage, file formats), the CRC is computed by the CPU. *Measured*, one 32 KiB buffer that stays in the L1 cache, GB/s, three runs:

| Method | Speed |
|---|---|
| bit by bit (8 shift-and-XOR steps per byte) | 0.09 |
| byte table (one 256-entry lookup per byte) | 0.36 to 0.38 |
| slicing-by-8 (eight tables, 8 bytes per step) | 1.5 to 1.6 |
| hardware CRC-32C instruction, one stream | 7.8 to 8.0 |
| hardware CRC-32C, **three independent streams interleaved** | **23.1 to 23.9** |

Each step is faster for a reason you know. The table replaces 8 shifts by one lookup; slicing replaces 8 dependent lookups by 8 independent ones; the instruction does a whole 8-byte step in hardware. And the last row is **latency against throughput** ([tutorial 12](12_inside_a_modern_core.md), section 4.3): the `crc32` instruction has a latency of 3 cycles but a throughput of 1 per cycle, and each step depends on the previous one, so a single stream is limited to one step per 3 cycles. Three independent streams fill the pipeline and run three times faster. (Production code splits a large buffer into three parts and combines their CRCs with a carry-less multiply.) Checking: all methods give `0xCBF43926` for the ASCII text `123456789` (CRC-32) and the instruction gives `0xE3069283` (CRC-32C).

### 8.6 Checksums and the end-to-end argument

The **Internet checksum** (IPv4 header, TCP, UDP, ICMP) is cheaper and weaker than a CRC: add all the 16-bit words with **end-around carry** (ones' complement addition), and send the complement of the sum. The receiver adds everything, including the checksum, and gets `0xFFFF` if all is well.

**Worked example.** Words `0xF0F0`, `0x1234`, `0xABCD`:

```text
0xF0F0 + 0x1234 = 0x10324    carry out: fold it back in:  0x0324 + 1 = 0x0325
0x0325 + 0xABCD = 0xAEF2
checksum = ~0xAEF2 = 0x510D
check:  0xAEF2 + 0x510D = 0xFFFF   ✓
```

It detects most single-bit errors and is trivially computed by NICs (**checksum offload**) and updated incrementally when a router changes one header field (NAT), but it misses reordered words and some compensating errors. Related sums are Fletcher and Adler-32.

Why does TCP check data that the Ethernet CRC already protected? The **end-to-end argument**: a link CRC protects one hop, but corruption can happen in a router's memory, a buggy driver or a DMA engine, between hops. Only a check made by the two endpoints covers the whole path. The same logic in storage: disks and SSDs have internal ECC, but file systems such as ZFS and btrfs add their own **block checksums** to detect silent corruption ("bit rot") anywhere between the application and the medium, and use RAID redundancy to repair it. In your own design, the check belongs at the end that matters.

---

## 9. A write's journey: who makes it durable and intact

What happens to `write(fd, buf, n)` on its way to a flash cell, and what each layer contributes:

| Layer | What it does for durability and integrity |
|---|---|
| **Application** | `write()` only copies into the **page cache** (2.5 µs here). `fsync` / `fdatasync` (or `O_SYNC`, `O_DSYNC`) ask for the data to reach the device (250 µs and more here). `O_DIRECT` skips the page cache but is not itself a durability promise |
| **File system** | a **journal** (ext4, XFS) or copy-on-write (btrfs, ZFS) keeps metadata consistent across a crash; ordering rules; some systems checksum metadata (ext4) or all blocks (btrfs, ZFS) |
| **Block layer and driver** | splits and queues requests; sends **flush** commands (and "force unit access" writes) that tell the device to commit its cache |
| **Link** | SATA or PCIe link-level CRC and retry |
| **SSD** | a write cache (volatile unless the drive has **power-loss-protection capacitors**); the FTL mapping journal; ECC (BCH or LDPC) on every flash page; optionally end-to-end protection tags (T10 PI) |
| **Array** | RAID parity or mirrors; **scrubbing** (periodically reading everything to find and repair latent errors before they meet a second failure) |
| **Fleet** | replication or erasure coding across machines and racks |

`fsync` is only as good as the device's honesty. A consumer drive with a volatile cache that acknowledges a flush early can lose "durable" data at power loss; enterprise drives have capacitors, which is also why their flushes are cheap. Databases, file systems and message brokers are built around this one call, and the measurement of section 3.6 is why they batch commits.

---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Thousands of small records per second, each followed by `fsync` | every commit waits a full device flush (100× a cached write) | group commit (one flush for many records), a sequential log, `fdatasync` rather than `fsync` |
| Synchronous reads one at a time are slow on an SSD | queue depth 1: the device's parallelism is idle (Little's law) | asynchronous I/O (`io_uring`, `libaio`), more threads, deeper queues, prefetch |
| An SSD slows down and wears faster as it fills | high write amplification: victim blocks are full of valid pages | leave free space (over-provisioning), keep TRIM/`fstrim` on, write large aligned chunks |
| Small writes smaller than the flash page, or not 4 KB aligned | read-modify-write inside the FTL, and more write amplification | align to 4 KB, batch small writes, use log-structured layouts |
| A flash-based embedded product dies after months | frequent writes (logs, counters) wearing a small raw NAND or eMMC | write logs to RAM and flush rarely, use UBIFS/wear levelling, read-only root with an overlay, monitor wear |
| A random-write-heavy RAID 5 volume is slow | 4 operations per write | RAID 10, full-stripe-aligned writes, more cache |
| A large RAID 5 array lost data during a rebuild | an unrecoverable read error on a survivor | RAID 6 or erasure coding, scrub regularly, replace drives on early warnings |
| Corruption was noticed days later | no end-to-end check | checksums in the application or file system (ZFS/btrfs), verify on read |
| CRC shows up in a profile | software CRC at 0.4 to 1.5 GB/s | the hardware CRC-32C instruction (8 GB/s per stream), interleave streams, or offload to the NIC |
| FCS or CRC error counters rising on a link | electrical noise, a bad cable or transceiver, or (on Wi-Fi) interference and low signal | inspect the physical layer; compare counters on both ends |
| Using CRC-32 as an integrity guarantee against an attacker | a CRC is linear and can be forged | use a cryptographic hash or MAC |

---

## 11. Mistakes that cost points

1. **Treating `write` as durable.** It returns when the data are in the page cache. Durability needs `fsync` and an honest device.
2. **Quoting disk and SSD speed as one number.** Sequential and random differ by 600× on a disk; an SSD needs queue depth to show its IOPS; and writes differ from reads.
3. **Saying "flash overwrites in place".** Flash is programmed by page and erased by block; the FTL hides this with remapping, garbage collection and write amplification.
4. **Forgetting write amplification** in an endurance estimate: lifetime = capacity × cycles ÷ WA, not capacity × cycles.
5. **Calling RAID a backup.** RAID survives drive failures; it does not survive deletion, corruption written to all copies, a controller bug or a fire.
6. **Ignoring the RAID 5 rebuild.** The array is unprotected during the rebuild and every survivor is read in full; UREs make large RAID 5 arrays risky.
7. **Reading MTBF as a lifetime.** 1,000,000 hours is a failure rate across a fleet, not the life of a drive.
8. **Multiplying where you should add (or the reverse).** Components in series multiply availabilities; redundant components multiply unavailabilities, if failures are independent.
9. **Mixing up detection and correction.** A code with distance *d* detects *d* − 1 errors and corrects ⌊(*d* − 1) / 2⌋; parity detects one error and corrects none; SEC-DED corrects one and detects two.
10. **Believing a CRC finds everything.** It misses error patterns that are multiples of the generator, and it is not secure.
11. **Believing the link-level CRC makes end-to-end checks unnecessary.** Corruption happens between hops too.
12. **Trusting `rotational` in a VM,** or any device property a virtual disk reports.

---

## 12. Questions you will be asked

**Why is random access so much slower than sequential on a hard disk, and what changed with SSDs?** A random access pays a seek and half a rotation (about 12 ms for a 7,200 RPM disk, 82 IOPS) while sequential access streams at 200 MB/s: about 600 times. An SSD has no moving parts: random reads take tens of microseconds, and the penalty moves to writes and to needing many requests in flight.

**How does an SSD overwrite data if flash cannot be overwritten?** It does not. The FTL writes the new data to a free page, updates the logical-to-physical map, and marks the old page invalid. Garbage collection later copies the valid pages out of a mostly-invalid block and erases it.

**What is write amplification, and how do you reduce it?** NAND bytes written divided by host bytes written, 1 / (1 − *u*) for victim blocks that are a fraction *u* valid. Reduce it with over-provisioning, TRIM, large sequential writes and by not filling the drive.

**How long does a 1 TB TLC SSD last?** Host writes ≈ capacity × P/E cycles ÷ WA: with 1,000 cycles and WA 3, about 333 TB, which is 9 years at 100 GB per day. Wear levelling spreads the erases.

**How do NVMe queues work, and why is NVMe faster than SATA?** Submission and completion queue pairs in host memory, a doorbell store to submit, an interrupt (or polling) to complete; up to 65,535 queues per device, normally one per core, so no shared lock. SATA/AHCI has one queue of 32 commands.

**State Little's law for storage.** Requests in flight = IOPS × latency. A drive that does 1 million IOPS at 80 µs needs 80 in flight; a single synchronous reader gets 1 / 80 of that.

**Compare RAID 0, 1, 5, 6, 10.** Striping (0: no redundancy), mirroring (1), striping with one parity (5: (*N* − 1) capacity, survives 1), two parities (6: (*N* − 2), survives 2), stripes of mirrors (10: half the capacity, fast writes, survives 1 and up to *N* / 2 if lucky).

**What is the RAID 5 small-write penalty?** To change one block you read the old data and old parity, compute *new parity = old parity ^ old data ^ new data*, and write both: 4 operations. RAID 6 needs 6; a full-stripe write avoids reads.

**Why is a RAID 5 rebuild risky?** During it there is no redundancy, every surviving drive is read in full, and a single unrecoverable read error (1 per 10^14 bits is typical for consumer drives: 89% over 28 TB) loses data. RAID 6 or erasure coding tolerates it.

**What does 99.99% availability mean, and how do you compose it?** 52.6 minutes of downtime a year. Series components multiply availabilities; redundant ones multiply the unavailabilities.

**Walk through a Hamming(7,4) decode.** Check bits at positions 1, 2, 4 cover the positions whose binary numbers contain that bit. Recompute the three checks; read them as a binary number: that is the position of the single flipped bit (0 means no error). Add an overall parity bit and you also detect double errors (SEC-DED); that is the (72,64) code in ECC memory.

**What is a CRC, and what does it catch?** The remainder of the message polynomial divided by a generator, computed with an LFSR in hardware. It catches all bursts up to the CRC's width and nearly all other errors (1 in 2^32 get through for CRC-32), but it is not secure. Ethernet and Wi-Fi frames end in a CRC-32 frame check sequence.

**Why does TCP have a checksum if Ethernet has a CRC?** End-to-end: a link CRC covers one hop, and data can be corrupted in routers, drivers and DMA between hops.

**How do you make a database commit durable, and what does it cost?** Append to a log, then `fdatasync`; the cost is the device's flush time (about 250 µs here, with tails of a second). Group commit amortises it over many transactions.

**An embedded access point's flash is wearing out. What do you check?** What writes to it (logs, counters, statistics), whether the file system does wear levelling (UBIFS on raw NAND; eMMC has an FTL), the write rate against the device's rated cycles, and whether logs can go to RAM or be batched.

---

## 13. Problems

**How to use this section.** Try each problem before you read the answers. All four are paper problems of about 25 minutes (D has a short optional part on your machine).

### Problem A (paper): disks, flash, NVMe

(a) A 15,000 RPM disk has a 4 ms average seek. Give the average rotational latency, the time of a random 4 KB access, the IOPS, the random bandwidth, and the ratio to a 250 MB/s sequential read.
(b) An SSD keeps one 4-byte mapping entry per 4 KB page. How much DRAM does an 8 TB drive need? How much if the mapping unit is 16 KB, and what is the price of that choice?
(c) A flash block holds 512 pages. Garbage collection picks a victim with 128 invalid pages. What is the write amplification? If the host writes 100 GB at this WA, how much does the NAND write? What if the victim had only 25% valid pages?
(d) A 2 TB QLC drive is rated for 600 program/erase cycles and runs at WA = 2.5. How many terabytes can the host write? How many years does that last at 400 GB per day?
(e) An NVMe drive does 800,000 random 4 KB reads per second at 100 µs. How many requests are in flight? What bandwidth is that? Does a PCIe Gen3 ×4 link (3.94 GB/s) carry it?

*Where:* paper. *How:* sections 2, 3 and 4. Access time = seek + half a rotation + transfer; WA = 1 / (1 − *u*); in flight = IOPS × latency.

### Problem B (paper): RAID and reliability

(a) Six drives of 8 TB each: the usable capacity and the number of failures survived by RAID 0, 5, 6 and 10.
(b) A RAID 5 stripe has four data blocks `D0 = 0x12`, `D1 = 0xAB`, `D2 = 0x5C`, `D3 = 0xE7` and one parity block. Compute the parity. The drive holding `D2` fails: rebuild `D2`. Then `D3` is overwritten with `0x00`: compute the new parity with the 4-operation method, and check it by recomputing from scratch.
(c) Twelve SSDs can each do 40,000 random writes per second. What write rate does RAID 10, RAID 5 and RAID 6 deliver for small random writes?
(d) A RAID 5 array of five 10 TB drives loses one. How long does the rebuild take at 150 MB/s? What is the chance of at least one unrecoverable read error during it, at 1 per 10^14 bits and at 1 per 10^15 bits?
(e) A fleet has 50,000 drives with MTBF = 1.2 million hours. What is the AFR, how many failures a year, and how often? What availability does a service get from two independent replicas of 99.5% each? From a request path of four serial stages of 99.95% each?

*Where:* paper. *How:* sections 5 and 6. XOR for parity; penalty 2 / 4 / 6; probability of at least one error = 1 − e^(−expected number).

### Problem C (paper): Hamming and SEC-DED

(a) Encode the data bits `1101` with the Hamming(7,4) layout of section 7.3 (check bits at positions 1, 2, 4).
(b) A receiver gets `1000101`. Find the syndrome, the position of the error, and the corrected data.
(c) Extend the code with an overall parity bit. Encode `0110` as an 8-bit word, then classify each received word: `11001100`, `11101100`, `01001000`, `11001101`. For each: no error, single error (where), double error, or error in the parity bit?
(d) How many check bits does a single-error-correcting code need for 26, 120 and 256 data bits, and how many for SEC-DED? What is the overhead of the last?

*Where:* paper. *How:* section 7. Check bit *p* covers the positions whose binary number contains *p*; 2^*r* ≥ *m* + *r* + 1.

### Problem D (paper, optional machine): CRC and checksum

(a) With *g*(x) = x³ + x + 1 (`1011`), compute the CRC of the message `10011010` by long division. Write the transmitted frame.
(b) The receiver gets the frame with the bit at index 5 (counting from 0 at the left) flipped. Show that the remainder is non-zero. What is the remainder if the first three bits are flipped? Find an error pattern the receiver would **not** detect, and a pair of bit errors it would not detect.
(c) Compute the Internet checksum of the 16-bit words `0x1C46`, `0x0A2B`, `0xF9D3`, `0x8001`, and verify it.
(d) *On your machine:* run `python3 -c "import zlib; print(hex(zlib.crc32(b'123456789')))"`; time commits with `dd if=/dev/zero of=t bs=4k count=2000 oflag=dsync` (Linux) and compare with `oflag=` omitted; list your drives with `lsblk -d -o NAME,ROTA,SIZE,MODEL`; if you can, read the SSD wear with `sudo smartctl -a /dev/nvme0` ("Percentage Used", "Data Units Written"). On a Mac: `diskutil info disk0`, `system_profiler SPNVMeDataType`. What is your drive's durable-write latency?

*Where:* paper for (a) to (c); a terminal for (d). *How:* section 8. XOR the divisor under each leading 1; fold carries back for the checksum.

---

## Answers

### Problem A

(a) Rotational latency = 0.5 × 60 / 15,000 = **2 ms**. Random 4 KB access = 4 + 2 + 0.016 (transfer at 250 MB/s) = **6.0 ms**, so **166 IOPS**, a random bandwidth of 4 KB × 166 = **0.68 MB/s**; the ratio to the sequential 250 MB/s is **367×**.

(b) 8 × 10^12 / 4,096 = 1.95 × 10^9 pages × 4 B = **7.8 GB of DRAM**. With a 16 KB mapping unit it is a quarter: **1.95 GB**. The price: a 4 KB write is now smaller than the mapping unit, so the drive must read the 16 KB, merge, and write 16 KB (read-modify-write, write amplification of up to 4× for small writes). Large drives use bigger units or cache part of the table.

(c) 128 invalid of 512 means 384 valid, *u* = 0.75: **WA = 1 / (1 − 0.75) = 4**. Collecting the block frees 128 pages and copies 384: (384 + 128) / 128 = 4 ✓. The host writes 100 GB; the NAND writes **400 GB**. With *u* = 0.25, WA = 1 / 0.75 = **1.33**, so 133 GB. A mostly-invalid victim is cheap; a full drive has victims like the first case.

(d) TBW = 2 TB × 600 / 2.5 = **480 TB**. At 400 GB per day: 480,000 / 400 = 1,200 days = **3.3 years**. The drive's warranty and DWPD rating are tied to this arithmetic.

(e) In flight = 800,000 × 100 × 10^-6 = **80 requests**. Bandwidth = 800,000 × 4,096 = **3.28 GB/s**. A Gen3 ×4 link carries at most 3.94 GB/s, so it fits with only 17% margin (protocol overheads eat some); Gen4 ×4 (7.88 GB/s) has plenty.

### Problem B

(a) Six drives of 8 TB: **RAID 0** 48 TB, survives 0. **RAID 5** (6 − 1) × 8 = 40 TB, survives 1. **RAID 6** (6 − 2) × 8 = 32 TB, survives 2. **RAID 10** 6 / 2 × 8 = 24 TB, survives 1 guaranteed (up to 3 if each failure is in a different mirror pair).

(b) Parity: 0x12 ^ 0xAB = 0xB9; 0xB9 ^ 0x5C = 0xE5; 0xE5 ^ 0xE7 = **0x02**. Rebuild D2 = D0 ^ D1 ^ D3 ^ P = 0x12 ^ 0xAB ^ 0xE7 ^ 0x02: 0xB9 ^ 0xE7 = 0x5E, ^ 0x02 = **0x5C** ✓. Small write of D3 = 0x00: new parity = old P ^ old D3 ^ new D3 = 0x02 ^ 0xE7 ^ 0x00 = **0xE5**. From scratch: 0x12 ^ 0xAB ^ 0x5C ^ 0x00 = 0xB9 ^ 0x5C = 0xE5 ✓. (Operations: read D3, read P, write D3, write P.)

(c) Raw = 12 × 40,000 = 480,000. RAID 10: ÷ 2 = **240,000**. RAID 5: ÷ 4 = **120,000**. RAID 6: ÷ 6 = **80,000**.

(d) Rebuild reads 4 × 10 TB = 40 TB and writes 10 TB to the spare; the time is limited by the 10 TB write: 10 × 10^12 / (150 × 10^6) = 66,667 s = **18.5 hours**. Bits read = 40 × 10^12 × 8 = 3.2 × 10^14. At 1 per 10^14: expected 3.2 UREs, P(at least one) = 1 − e^(−3.2) = **96%**. At 1 per 10^15: expected 0.32, P = 1 − e^(−0.32) = **27%**. Either is too high for data that matter, which is the case for RAID 6.

(e) AFR = 8,760 / 1.2 × 10^6 = **0.73%**; failures per year = 50,000 × 0.0073 = **365**, so **one a day**. Two replicas of 99.5%: 1 − 0.005² = **99.9975%** (if failures are independent and failover is instant). Four serial stages of 99.95%: 0.9995^4 = **99.80%** (about 17.5 hours of downtime a year, against 4.4 hours per stage).

### Problem C

(a) Data `1101` → D1 D2 D3 D4 = 1 1 0 1 at positions 3, 5, 6, 7. p1 = pos3 ^ pos5 ^ pos7 = 1 ^ 1 ^ 1 = 1. p2 = pos3 ^ pos6 ^ pos7 = 1 ^ 0 ^ 1 = 0. p4 = pos5 ^ pos6 ^ pos7 = 1 ^ 0 ^ 1 = 0. Code word (positions 1 to 7): **1 0 1 0 1 0 1** = `1010101`.

(b) `1000101`: positions 1..7 = 1,0,0,0,1,0,1. c1 = pos1 ^ pos3 ^ pos5 ^ pos7 = 1 ^ 0 ^ 1 ^ 1 = 1. c2 = pos2 ^ pos3 ^ pos6 ^ pos7 = 0 ^ 0 ^ 0 ^ 1 = 1. c4 = pos4 ^ pos5 ^ pos6 ^ pos7 = 0 ^ 1 ^ 0 ^ 1 = 0. Syndrome (c4 c2 c1) = 011 = **3**: position 3 is wrong; flip it to get `1010101`, data (positions 3, 5, 6, 7) = **1101** ✓.

(c) `0110`: D1..D4 = 0, 1, 1, 0 at positions 3, 5, 6, 7. p1 = 0 ^ 1 ^ 0 = 1; p2 = 0 ^ 1 ^ 0 = 1; p4 = 1 ^ 1 ^ 0 = 0. Hamming word `1100110` has four 1s, so the overall parity is 0: **`11001100`**.

| Received | Syndrome | Overall parity | Verdict |
|---|---|---|---|
| `11001100` | 0 | OK | no error |
| `11101100` | 3 | fails | **single error at position 3**, corrected |
| `01001000` | 1 ^ 6 = 7 | OK | **double error** (positions 1 and 6), detected not corrected |
| `11001101` | 0 | fails | the **overall parity bit** itself flipped; data fine |

(d) 26 data bits: 2^5 = 32 ≥ 26 + 5 + 1 = 32, so **5** check bits (SEC-DED: 6). 120: 2^7 = 128 ≥ 120 + 7 + 1 = 128, so **7** (SEC-DED: 8). 256: 2^8 = 256 < 265, so **9** (SEC-DED: **10**), overhead 10 / 256 = **3.9%**. The bigger the word, the smaller the relative overhead, but the more bits one correction covers.

### Problem D

(a) Append three zeros: `10011010000`, divide by `1011`:

```text
 10011010000
^1011
 00101010000
   ^1011
 00000110000
      ^1011
 00000011100
       ^1011
 00000001010
        ^1011
 00000000001      remainder = 001
```

Transmitted frame: **`10011010001`**.

(b) Flip the bit at index 5: `10011110001`. Dividing gives the remainder **111**, non-zero: detected. Flip the first three bits: remainder **101**, non-zero: detected (a burst of 3 is within the 3-bit width). An undetected error pattern is any multiple of the generator: `1011` placed anywhere (for example flipping bits 3 to 6, giving `10001100001`) leaves remainder 000. A pair of errors is undetected when the bits are **7 apart** (positions 0 and 7, 1 and 8, 2 and 9, 3 and 10): the polynomial x³ + x + 1 has order 7, so x⁷ + 1 is a multiple of it. This is why a CRC-3 only protects frames of up to 7 bits against double errors, and why real CRCs are 16 or 32 bits wide: CRC-32 detects all 2-bit errors in frames up to 2^32 − 1 bits (512 MiB).

(c) `0x1C46 + 0x0A2B = 0x2671`; `+ 0xF9D3 = 0x12044`, fold the carry: `0x2044 + 1 = 0x2045`; `+ 0x8001 = 0xA046`. Checksum = `~0xA046` = **`0x5FB9`**. Verify: `0xA046 + 0x5FB9 = 0xFFFF` ✓.

(d) The check should print `0xcbf43926`. With `oflag=dsync` every 4 KB write waits for the device: expect about 50 µs to 1 ms per write on an NVMe drive (a consumer SATA drive with a volatile cache is often faster than its honest enterprise counterpart), 5 to 15 ms on a hard disk; without it, the speed is the page cache's. `lsblk` shows `ROTA` 0 for SSDs, 1 for disks (and 1 for most virtual disks, whatever the host has). Note your own numbers.

---

## Cheat sheet

- **Latency scale:** DRAM 0.1 µs; NVMe 70 to 100 µs (35 to 45 µs measured on the virtual disk); SATA SSD about 100 µs; network storage 0.5 to 2 ms; disk 5 to 15 ms.
- **Disk:** access = seek (4 to 9 ms) + 0.5 × 60 / RPM (4.17 ms at 7,200) + transfer: about 12 ms, 82 IOPS, 0.34 MB/s random against 200 MB/s sequential, ×600.
- **NAND:** SLC/MLC/TLC/QLC = 1/2/3/4 bits per cell; read 25 to 100 µs, program 0.2 to 1+ ms, erase 2 to 10 ms; endurance 100,000 (SLC) down to hundreds (QLC). Program by **page** (4 to 16 KB), erase by **block**; no overwrite in place.
- **FTL:** logical-to-physical map (1 MB per GB: 4 GB for 4 TB), log-structured writes, garbage collection, wear levelling, bad blocks, TRIM. **WA = NAND writes ÷ host writes = 1 / (1 − *u*)**; reduce with over-provisioning, TRIM, sequential writes, free space. **Lifetime = capacity × cycles ÷ WA**.
- **Raw flash (embedded):** NOR for boot and XIP; raw NAND with MTD and UBIFS (software wear levelling and bad blocks, BCH from the SoC); eMMC/UFS/SSD have an FTL inside.
- **NVMe:** SQ/CQ pairs in host memory, doorbell store, MSI-X per queue, up to 65,535 queues, one per core. PCIe Gen4 ×4 = 7.88 GB/s. **Little's law: in flight = IOPS × latency**; needs queue depth.
- **Measured (virtual disk, ranges):** QD1 random read 33 to 43 µs; page-cache write 2.4 to 2.7 µs; write + `fdatasync` median 255 to 269 µs, worst 0.7 to 1.4 s; 1 → 16 in flight: 23,000 → 150,000 IOPS, latency 40 → 100 µs.
- **RAID:** 0 stripe; 1 mirror; 5 parity (*N* − 1, survives 1, small write = 4 operations); 6 double parity (*N* − 2, survives 2, 6 operations); 10 stripe of mirrors (half, 2 operations). Parity = XOR; new parity = old parity ^ old data ^ new data. Rebuild reads all survivors: URE 1 per 10^14 bits over 28 TB = 89%. Not a backup.
- **Reliability:** AFR ≈ 8,760 / MTBF; availability = MTBF / (MTBF + MTTR); nines (99.9% = 8.76 h, 99.99% = 52.6 min, 99.999% = 5.26 min); series multiplies availability, parallel multiplies unavailability; MTTDL(RAID 5) ≈ MTBF² / (*N*(*N* − 1) MTTR).
- **Codes:** distance *d* detects *d* − 1, corrects ⌊(*d* − 1) / 2⌋. Parity detects 1. **Hamming(7,4):** check bits at 1, 2, 4 cover positions whose binary number contains that bit; the syndrome is the error's position; 2^*r* ≥ *m* + *r* + 1. **SEC-DED:** add an overall parity bit; (72,64) = 8 check bits. BCH (NAND), Reed–Solomon (bursts, RAID 6, chipkill, CDs), LDPC (SSDs, Wi-Fi, 5G).
- **CRC:** remainder of message × x^*r* ÷ *g*(x); LFSR in hardware; detects all bursts up to *r* bits, 1/2^*r* of random garbage passes (CRC-32: 1 in 4.3 billion); every 1-, 2-, 3-bit error in a 1,518-byte frame; not secure. CRC-32 = Ethernet and 802.11 FCS, zip, PNG; CRC-32C = iSCSI, ext4/btrfs metadata, hardware instruction. Measured: bit-by-bit 0.09, table 0.37, slicing-by-8 1.5, hardware 8, three streams 23 GB/s.
- **Internet checksum:** 16-bit ones' complement sum with end-around carry; receiver sums to `0xFFFF`; cheap, incremental, weaker than a CRC. **End-to-end argument:** the check must cover the whole path.
- **Durability:** `write` = page cache; `fsync` = device flush; group commit amortises it; power-loss-protection capacitors make flushes cheap and honest.

**Next:** tutorial 18, Multicore and Multithreading (the power wall and why multicore, Flynn's taxonomy, hardware threads and SMT, SMP and NUMA, parallel programming models, Amdahl's law and scaling limits, heterogeneous cores and chiplets). Say "next" when you are ready.
