# CO 15: Virtual Memory and the TLB

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 13](13_cache_basics.md) and [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (caches, VIPT, memory types)*

By the end of this tutorial you can:

- explain why virtual memory exists, and the difference between segmentation, paging, and internal and external fragmentation;
- split a virtual address into page-table indices and an offset, and walk a multi-level page table by hand (x86-64 with 4 and 5 levels, 32-bit two-level, Arm);
- read a page-table entry: present, read/write, user/supervisor, accessed, dirty, no-execute, memory type, protection key;
- explain the TLB: what an entry holds, how big the TLB is, **TLB reach**, and what a miss costs;
- explain the page walk, page-walk caches, and why a walk is much slower inside a virtual machine;
- follow a page fault from the hardware exception to the restart, and tell minor from major faults;
- explain demand paging, copy-on-write and `fork`, and compare page-replacement policies (including Belady's anomaly);
- decide when huge pages help and what they cost;
- explain ASIDs/PCIDs, global pages, TLB shootdowns, the cost of a context switch, and the cost of KPTI;
- explain protection (NX, guard pages, protection keys), the process address-space layout, and the user/kernel split.

Tutorials 13 and 14 treated the address that reaches the cache as a given. It is not: every address a program uses is a **virtual** address, and the hardware translates it on every single access. This tutorial is about that translation, what it costs, and how to keep the cost small.

**Where the numbers come from.** Timings marked *measured* were taken on the same kind of cloud virtual machine as tutorial 14 (an Intel Xeon core under KVM, two virtual CPUs, 7 GB of memory, Linux 6.18, 4 KB pages, transparent huge pages in `madvise` mode), pinned to one core, compiled with GCC 13 and `-O2`. **A virtual machine adds a second layer of address translation, so TLB misses and page faults cost much more here than on bare metal** (section 6.4). Where it matters the text says what to expect on a physical machine. Run-to-run noise is 10 to 30%, so some numbers are ranges.

> **The one-sentence model.** Every address is virtual and is translated by a tree of page tables that the hardware caches in the TLB; almost every cost in this tutorial is a TLB miss, a page fault, or a flush of those caches.

---

## 1. Why virtual memory

The addresses in your program (pointers, the program counter, the stack pointer) are **virtual addresses**. A hardware unit, the **memory management unit (MMU)**, turns each one into a **physical address** (the address that goes to the caches and to DRAM) using tables that the operating system builds. This costs time and hardware, and it is worth it for four jobs:

| Job | What it gives you |
|---|---|
| **Isolation** | each process has its own address space; process A has no address that names process B's memory, so a stray pointer cannot corrupt it |
| **Simple layout** | every process can put its code at the same address and have a large contiguous heap, without finding a contiguous block of physical memory |
| **Lazy and larger-than-RAM memory** | a page gets physical memory only when it is first touched (*demand paging*); `malloc(1 GiB)` costs almost nothing until used; files can be mapped into memory (`mmap`); rarely used pages can be pushed out to disk |
| **Sharing and protection** | the same physical page can appear in many address spaces (the C library's code, shared memory, copy-on-write pages), and every mapping has its own permissions: read, write, execute, user or kernel |

### 1.1 Paging, and what it replaced

There are three ways to place a program in physical memory:

- **Base and bounds:** two registers; every address is added to *base* and checked against *bounds*. The program gets one contiguous block of physical memory. Simple, but the block cannot grow in place, and free memory ends up in pieces.
- **Segmentation:** several base-and-bounds pairs (code, data, stack). Still variable-sized blocks.
- **Paging:** memory is cut into fixed-size **pages** (4 KB on x86-64; 4, 16 or 64 KB on Arm), physical memory into **frames** of the same size, and a table says which frame holds which page. Any free frame will do for any page.

Two kinds of waste, named in every interview:

- **External fragmentation:** there is enough free memory in total, but not in one piece large enough for the request. Base and bounds and segmentation suffer from it; paging does not, because every request is made of equal pages.
- **Internal fragmentation:** waste *inside* an allocated unit. A 100-byte object in its own 4 KB page wastes 3,996 bytes; on average a region wastes half a page at its end. Larger pages waste more (section 9).

x86-64 still has segment registers, but the base is zero except for `FS` and `GS` (which hold the thread-local-storage base), so in practice the address space is flat and paging does all the work. One caveat: physical memory can still be fragmented at a coarse scale. A 2 MB huge page needs 512 *consecutive* free frames, and after a machine has run for weeks there may be none.

---

## 2. Pages and the translation as arithmetic

A virtual address is split into a **virtual page number (VPN)** and an **offset** inside the page. A physical address has a **physical frame number (PFN)** and the same offset:

```text
virtual address   =  [ virtual page number  |  offset ]
physical address  =  [ physical frame number |  offset ]      the offset is copied unchanged
```

For 4 KB pages the offset is 12 bits. Example with a 32-bit address: `0x0804A123` has VPN `0x0804A` and offset `0x123`. If the page table says "page `0x0804A` is in frame `0x00231`", the physical address is `0x00231123`.

The simplest table is an array indexed by VPN. For a 48-bit address space with 4 KB pages that is 2^36 entries of 8 bytes each: **512 GiB per process**, which is absurd. (Even a 32-bit address space needs 4 MiB per process with 4-byte entries, for hundreds of processes.) The way out is the observation that nearly all of a process's address space is empty. So the table is a **tree**, and the parts of the tree that cover nothing do not exist.

---

## 3. Multi-level page tables

### 3.1 The x86-64 walk

Cut the VPN into groups of 9 bits. Each group indexes one level of the tree. Each table is exactly one page: 512 entries × 8 bytes = 4096 bytes. That is the reason for 9 bits: **every level of the tree is itself one page**, so one allocator serves both data and tables.

```text
 CR3 ─► PML4 ─────► PDPT ─────► PD ─────► PT ─────► data page
        (level 4)   (level 3)   (level 2) (level 1)
        bits 47:39  bits 38:30  bits 29:21 bits 20:12      bits 11:0 = offset
        9 bits      9 bits      9 bits     9 bits          12 bits
```

`CR3` holds the physical address of the top table (the PML4). Each entry holds the physical address of the next table. The hardware address of an entry is `table base + 8 × index`.

**A worked walk.** Virtual address `0x00007F12_3456_7ABC`:

```text
bits 47:39  →  254    PML4 entry 254   (read 8 bytes at CR3 + 254 × 8)
bits 38:30  →   72    PDPT entry 72    (at the address found in the PML4 entry, + 72 × 8)
bits 29:21  →  418    PD entry 418
bits 20:12  →  359    PT entry 359     (this entry holds the frame number of the data page)
bits 11:0   →  0xABC  offset           (physical address = frame × 4096 + 0xABC)
```

Four dependent memory reads, then the data access itself: five memory accesses to read one byte. This is why the TLB (section 5) is not optional. If any entry on the way has its **present** bit clear, the walk stops with a page fault (section 7).

**Huge pages are the same walk, stopped early.** If a PD entry has its "page size" bit set, the walk ends there, the remaining bits become the offset, and the entry maps a **2 MB** page (21 offset bits). Setting the bit in a PDPT entry gives a **1 GB** page (30 offset bits). For the address above, a 2 MB page would give offset `0x167ABC`.

### 3.2 Why a tree is small

Table pages exist only where something is mapped:

| What is mapped | Table pages needed | Size |
|---|---|---|
| 1 GiB, contiguous, 4 KB pages | 512 PT pages (one per 2 MB) + 1 PD + 1 PDPT + 1 PML4 = 515 | 2.01 MiB (0.2% of the data) |
| the same 1 GiB with 2 MB pages | 1 PD + 1 PDPT + 1 PML4 = 3 | 12 KiB |
| one single 4 KB page | 1 PML4 + 1 PDPT + 1 PD + 1 PT = 4 | 16 KiB, four times the data |
| 4 pages in 4 far-apart regions | up to 16 | 64 KiB |

So a dense mapping costs 8 bytes of table per 4 KB page (0.2%), and a sparse one costs up to four table pages per mapped page. Both are fine for the kernel's memory; the real cost of the tree is the **number of dependent reads in a walk**.

### 3.3 Canonical addresses, 5 levels, and the user/kernel split

x86-64 uses 48 of the 64 address bits (256 TiB). The upper 16 bits must be copies of bit 47 (a **canonical** address), otherwise the access faults. This splits the space into two halves:

```text
0x0000_0000_0000_0000 ... 0x0000_7FFF_FFFF_FFFF    lower half: user space (128 TiB)
          (non-canonical hole)
0xFFFF_8000_0000_0000 ... 0xFFFF_FFFF_FFFF_FFFF    upper half: kernel space (128 TiB)
```

**5-level paging** (LA57) adds one more 9-bit level at the top (bits 56:48): 57-bit addresses, 128 PiB, for machines with enormous memory. It costs one more dependent read per walk, so Linux enables the larger space only for processes that ask for an address above 47 bits. (This VM does not expose it.)

### 3.4 Arm

AArch64 uses the same idea, with **translation tables** at levels 0 to 3 (four levels with a 4 KB granule and 48-bit addresses, 9 bits per level, so the walk of section 3.1 applies unchanged). Two differences matter:

- There are **two base registers**: `TTBR0_EL1` for the lower half of the address space (user) and `TTBR1_EL1` for the upper half (kernel). Switching processes changes only `TTBR0`; the kernel mapping stays.
- The **granule** (page size) is a choice, and it changes the numbers:

| Granule | Offset bits | Index bits per level | Largest block at level 2 |
|---|---|---|---|
| 4 KB | 12 | 9 | 2 MB (and 1 GB at level 1) |
| 16 KB | 14 | 11 | 32 MB |
| 64 KB | 16 | 13 | 512 MB |

Apple silicon uses 16 KB pages (run `pagesize` on a Mac and it prints 16384), so the same number of TLB entries covers four times more memory than on x86 with 4 KB pages. This also fits the 128 KB, 8-way L1 cache reported for these chips (tutorial 14, section 5.2: one way may be as large as one page).

---

## 4. The page-table entry

### 4.1 x86-64 entry

A page-table entry (PTE) is 8 bytes. The frame number takes bits 12 to 51; the bits around it are permissions and status:

```text
bit 63      NX       no-execute: instruction fetch from this page faults
bits 62:59  PK       protection key (4 bits, section 11.3)
bits 51:12  PFN      physical frame number (the address of the next table, or of the data page)
bit 8       G        global: not flushed when the address space changes (section 10)
bit 7       PS       page size (in a PD or PDPT entry: this entry maps a 2 MB or 1 GB page)
bit 6       D        dirty: set by hardware on the first write
bit 5       A        accessed: set by hardware on the first access
bits 4,3    PCD,PWT  with PAT: the memory type (write-back, write-combining, uncached; tutorial 14)
bit 2       U/S      user/supervisor: 1 = user-mode code may access
bit 1       R/W      writable (0 = read-only)
bit 0       P        present
```

When P is 0, the hardware ignores the other 63 bits, and the operating system uses them freely (for example, to remember where a swapped-out page is).

On every access the hardware checks, in order: is the entry present; does the access type (read, write, instruction fetch) and privilege (user or kernel) match R/W, U/S and NX? A mismatch raises a **page fault** (vector 14). The hardware puts the faulting virtual address in `CR2` and an error code on the stack (was the page present, was it a write, was it a user access, was it an instruction fetch).

### 4.2 Accessed and dirty bits

The hardware sets **A** the first time a page is used and **D** the first time it is written. Nobody has to trap to do that. The operating system uses them: a page whose D bit is 0 can be dropped without writing it to disk, and clearing the A bits periodically and looking again later tells which pages are *not* being used, the basis of approximate-LRU page replacement (section 7.4). Since the TLB caches the entry, the first write to a clean page needs one more trip to the page table to set D.

### 4.3 Permissions in practice

- **W^X** (write xor execute): a page is writable or executable, not both. The no-execute bit stops injected data from running as code. Programs that generate code (JIT compilers) map the memory writable, write the code, and then change it to executable, which costs a system call and a TLB flush (section 10.3).
- **SMEP and SMAP** (x86; **PXN and PAN** on Arm): the kernel may not *execute* user pages, and may not *read or write* them except in marked places. They turn many kernel bugs from exploits into crashes.
- **Read-only pages with a hidden purpose:** the page tables are also how copy-on-write (section 8) catches the first write.

### 4.4 Arm entry

The Arm descriptor holds the same information with different names: *valid* bit; **AP** bits (read-only, and whether user code may access); **UXN** and **PXN** (no-execute for user and privileged code); **AF**, the access flag; **nG**, the "not global" bit; a *contiguous* hint (section 9.3); and **AttrIndx**, an index into the `MAIR` register that selects the memory type (Normal write-back, Normal non-cacheable, or the Device types of tutorial 14). Dirty tracking is optional in hardware (the DBM mechanism); otherwise the first write to a clean page faults and software records it.

---

## 5. The TLB

### 5.1 The problem and the answer

Every load, store and instruction fetch needs a translation, and a walk costs four dependent reads (section 3.1). The answer is the same as for data: a small, fast cache, the **translation lookaside buffer (TLB)**, indexed by virtual page number.

A TLB entry holds:

```text
VPN (tag) | PFN | permissions (R/W, U/S, NX) | memory type | page size | address-space tag (ASID/PCID) | global bit
```

On a hit, the physical page number comes back in one cycle or so, in parallel with the L1 cache's set lookup (that is VIPT, tutorial 14), and the physical tag is compared with it. On a miss, a hardware **page walker** reads the tables (section 6) and fills the entry. x86 and Arm walk in hardware; a few architectures (MIPS) take an exception and refill the TLB in software.

### 5.2 Structure

| TLB | Typical size | Organisation | Extra delay on a hit |
|---|---|---|---|
| L1 instruction TLB | 64 to 256 entries | fully or highly associative | none (hidden in the L1 access) |
| L1 data TLB | 48 to 128 entries | fully or highly associative | none |
| L2 (second-level, "STLB"), unified | 1,500 to 3,000 entries | 8 to 16 ways | a few cycles (about 7) |

Several things follow from the hardware:

- Small, because every load goes through it, so its comparators must be fast and low power (tutorial 13, section 9: a fully associative TLB is a CAM). And each entry describes only one page, so a TLB of 64 entries covers only 256 KB of 4 KB pages.
- Often separate arrays for 4 KB, 2 MB and 1 GB pages, or entries that record the page size.
- A program can hit in the L2 TLB and miss in the L1 TLB (a few cycles), or miss in both (a walk): two thresholds, as the measurement in section 6.2 shows.

### 5.3 TLB reach

The memory the TLB can map without a walk is its **reach**:

```text
reach = number of entries × page size
```

| Level (sizes of this core) | Entries | Reach with 4 KB pages | Reach with 2 MB pages (same entry count) |
|---|---|---|---|
| L1 data TLB | 96 | 384 KB | 192 MB |
| L2 TLB (STLB) | 2,048 | 8 MB | 4 GB |

(Real L1 TLBs often have fewer entries for 2 MB pages than for 4 KB pages, so treat the 2 MB column as the best case.) This one number explains a lot. The L1 TLB reaches 384 KB, less than the 2 MB L2 cache, and the L2 TLB reaches 8 MB, less than the L3. **A program with a working set above 8 MB starts walking page tables while its data would still fit in the L3.** A 400 MB flow table on 4 KB pages needs about 100,000 entries when the TLB has 2,048. Huge pages are the main fix (section 9).

TLB misses and cache misses are separate events that add up. A random read of a large array can miss the TLB (a walk) and then miss the cache (a DRAM access).

---

## 6. The page walk

### 6.1 What the walker does

On a TLB miss, the **page walker** (a small state machine, "page-miss handler" on Intel) starts at `CR3` and reads the four levels (section 3.1). Three facts keep this from being a disaster:

1. **The page-table entries are cached like data.** Each walk read goes through the L1/L2/L3 caches. The upper levels cover huge ranges, change rarely and almost always hit.
2. **Page-walk caches** (also called paging-structure caches) remember the upper-level entries of recent walks: "virtual bits 47:21 lead to this PT page". A walk that hits there skips the top three reads and does only the **leaf** read. So a typical walk is one memory read, usually an L2 or L3 hit.
3. **Eight neighbouring pages share one cache line of leaf entries** (8 entries × 8 bytes = 64 bytes). A sequential scan takes a TLB miss for every new 4 KB page, but the leaf entries of 8 pages arrive in a single line, so only one walk in eight has to go to memory for its leaf.

Modern cores have two to four walkers, so several TLB misses can proceed at once, and walks can be started speculatively. On a physical machine, an L2-TLB hit costs a few cycles and a walk that hits in the walk caches costs tens of nanoseconds, a few cache accesses' worth. When the entries themselves miss in the caches (a big random footprint), a walk costs about a DRAM access.

### 6.2 Measuring it: a chase with one node per page

To see the TLB, use a pointer chase (so that no memory-level parallelism hides the cost) with **one node per 4 KB page**, visited in random order, each node on a different cache line offset inside its page. The number of lines touched is then equal to the number of pages, so the *data* footprint is tiny while the *page* footprint grows. Run once on ordinary 4 KB pages and once with 2 MB transparent huge pages (checked in `/proc/self/smaps_rollup`: 1 GiB of `AnonHugePages`).

*Measured*, ns per hop (two runs shown as a range):

| Pages touched | Data lines | 4 KB pages | 2 MB pages (THP) | What happened |
|---|---|---|---|---|
| 32 | 2 KB | 1.2 to 1.6 | 1.2 to 1.6 | L1 data TLB hit and L1 cache hit: 5 cycles |
| 96 | 6 KB | 1.2 to 1.5 | 1.5 to 1.6 | still inside the L1 TLB (96 entries) |
| 128 | 8 KB | **3.6 to 3.7** | 3.7 to 3.8 | L1 TLB overflow: **+2.2 ns** for an L2-TLB hit |
| 512 | 32 KB | 3.7 to 3.8 | 3.0 to 3.5 | flat: the L2 TLB covers it |
| 1,024 | 64 KB | 6.3 to 6.4 | 5.9 to 7.2 | not the TLB: 1,024 lines no longer fit the 48 KB L1 cache |
| 2,048 | 128 KB | 8.1 to 9.3 | 6.9 to 8.0 | the L2 TLB is full |
| 3,072 | 192 KB | **14 to 17.5** | 13.4 to 14.2 | **L2 TLB overflow: page walks begin** |
| 8,192 | 512 KB | 20 | 15 to 16 | walks, data from L2 |
| 65,536 | 4 MB | 98 to 100 | 71 to 77 | data from L3, almost every access walks |
| 262,144 | 16 MB | 243 to 263 | 194 to 206 | the page-table entries (2 MB of them) also miss the L1 and L2 caches |

Reading the table:

- The **first TLB step** is between 96 and 128 pages: this core has a **96-entry** L1 data TLB, and an L2-TLB hit costs about 2.2 ns, 7 cycles. The **second** is between 2,048 and 3,072 pages: the L2 TLB has about **2,048** entries. These are the reach figures of section 5.3, measured instead of read from a manual.
- The step between 768 and 1,024 pages is the L1 *data cache* (768 lines of 64 bytes = 48 KB), not the TLB. Both columns show it. When you do a TLB experiment, check that the data lines still fit the cache you think they do; the comparison with the 2 MB column is how to tell the two kinds of step apart.
- Walks are expensive here: 100 to 250 ns per hop in the last rows, against roughly 55 ns for an L3 hit. A TLB miss costs about as much as a DRAM access.

### 6.3 What the huge-page column shows, and what it does not

The 2 MB column helps only once the walks begin: 20 to 30% fewer nanoseconds from about 8,000 pages on, because a 2 MB page needs a walk of three reads instead of four, and covers the same memory with 512 times fewer page-table entries to be cached (one entry per 2 MB instead of 512). It does **not** move the first two steps: at 128 pages, which all lie in one 2 MB page, the L1-TLB step is still there. On bare metal one 2 MB entry would cover all 128 pages and the step would vanish.

That is a property of virtual machines. The TLB caches the *final* translation from guest-virtual to host-physical address, and its page size is the **smaller** of the guest's page size and the host's. If the hypervisor backs the guest's memory with 4 KB pages, the TLB entry is 4 KB however large the guest's page is. You cannot see this from inside the guest except by experiments like this one.

### 6.4 Why a walk is longer in a VM

In a virtual machine the "physical" address that the guest's page tables produce is itself a *guest-physical* address, translated by a second set of tables owned by the hypervisor (Intel EPT, AMD NPT, Arm stage-2 translation). Every one of the four guest-table reads needs its own translation through the second tree, so the worst-case walk grows from 4 reads to **24** (each of the 4 guest levels needs 4 host reads plus the guest read, and the final data address needs 4 more). Caches and walk caches shorten this a lot, but it is why a TLB miss in the cloud can cost well over 100 ns (section 6.2), where a walk that hits the walk caches on bare metal typically costs 20 to 40 ns. Tutorial 27 covers nested paging properly; for now: **in a VM, TLB reach matters even more, and huge pages in the guest help only as far as the host also uses them.**

---
## 7. Page faults and demand paging

### 7.1 What happens at a fault

```text
1. An access finds a not-present entry, or a permission mismatch: the core raises a page fault.
2. The hardware records the address of the faulting instruction, puts the faulting virtual address
   in CR2 (FAR_EL1 on Arm) plus an error code, and enters the kernel's fault handler.
3. The handler looks up which region of the address space contains that address and decides.
4a. Fixable: allocate a frame (or find the page), fill in the entry, return.
    The faulting instruction is restarted from the beginning and now succeeds.
4b. Not fixable (no region, or wrong permission): the process receives SIGSEGV.
```

The restart works because a page fault is a **precise exception** (tutorials 9 and 11): everything before the faulting instruction has completed, and the faulting instruction and everything after it have left no effect. A load that faults in the middle of an out-of-order window is simply squashed with its younger instructions.

### 7.2 Kinds of fault

| Kind | Cause | Typical cost |
|---|---|---|
| **Minor** (soft) | the page is in memory or can be made without I/O: first touch of anonymous memory (a zeroed frame), a file page already in the page cache, a copy-on-write copy (section 8) | about 1 to 3 µs (measured below) |
| **Major** (hard) | the data must be read from storage: a file page not in the cache, or a swapped-out page | 50 µs on a fast SSD to 10 ms on a disk |
| **Invalid** | nothing is mapped there, or the permission is wrong and not fixable (write to read-only code, NULL pointer) | `SIGSEGV` |

Linux counts the first two as `minor-faults` and `major-faults` (`perf stat -e minor-faults,major-faults ./app`, or `/usr/bin/time -v`). In a data-plane program, **any major fault is a bug**.

### 7.3 Demand paging, with numbers

`malloc(1 GiB)` and `mmap` only reserve a range of addresses. A physical frame is attached when the page is first touched. That is why a huge allocation returns instantly and the resident memory grows as you use it. *Measured*, touching one byte in each page of a fresh 1 GiB mapping:

| What | Cost |
|---|---|
| first write to a fresh 4 KB page (a minor fault) | **1.5 µs per page** in steady state (3 to 5 µs on some runs, and 18 µs the very first time this VM used that memory, when the host also had to supply it) |
| second write to the same page (present; only a TLB and a cache miss) | 30 ns |
| the whole range pre-faulted in one call (`MAP_POPULATE`) | 1.1 µs per page |
| first touch with 2 MB transparent huge pages | **280 to 300 µs per 2 MB page** (0.57 µs per 4 KB), sometimes 1.5 to 2 ms |

Where does 1.5 µs go? Entering and leaving the kernel (hundreds of nanoseconds, more in a VM), finding the region, taking a frame, **zeroing 4 KB** so that no process sees another's old data, and writing the entry. Three consequences:

- Touching 1 GiB for the first time costs 262,144 faults, about **0.4 s**. A latency-sensitive program should touch (or `MAP_POPULATE`, or `mlockall`) its memory at start-up, not on the first request.
- `MAP_POPULATE` is about 30% cheaper per page than faulting, because the kernel does not enter and leave the fault handler for each page; the zeroing is still there.
- A 2 MB fault is cheaper per byte but is **one stall of about 0.3 ms**, mostly spent zeroing 2 MB. Averages improve and the tail gets worse (section 9.2).

### 7.4 Page replacement, briefly

When memory is full, each new page needs a frame, so the system must pick a victim (and write it to storage first if its dirty bit is set). The policies are those of the cache (tutorial 13, section 6):

- **OPT** (Belady's MIN): evict the page used furthest in the future. Impossible, but the yardstick.
- **LRU**: evict the page unused for the longest time. Real systems approximate it with the accessed bit: periodically clear A, later see which pages got it set again (the **clock** or **second-chance** algorithm).
- **FIFO**: evict the oldest page. Cheap, and it has an odd flaw.

*Belady's anomaly:* with FIFO, **more frames can mean more faults.** For the reference string `1 2 3 4 1 2 5 1 2 3 4 5`:

| Frames | FIFO | LRU | OPT |
|---|---|---|---|
| 3 | 9 faults | 10 | 7 |
| 4 | **10 faults** | 8 | 6 |

LRU and OPT never do this, because with k + 1 frames they hold a superset of the pages they hold with k frames. When the pages in use (the **working set**) exceed memory, almost every access faults and the machine spends its time paging: **thrashing**. With a fault rate *p*, the average access time is `(1 − p) × t_mem + p × t_fault`. For 100 ns memory and an 8 ms major fault, even `p = 1` in 100,000 gives 100 + 80 = **180 ns**, an 80% slowdown. (The operating-system track goes deeper into replacement and the page cache.)

---

## 8. Copy-on-write and `fork`

### 8.1 The mechanism

Two address spaces can share a physical page as long as neither changes it. **Copy-on-write (COW)** does this with the permission bits:

```text
1. Share: map the same frame in both address spaces, marked read-only, and count the sharers.
2. A write by either side faults (permission mismatch, not a missing page).
3. The handler allocates a new frame, copies the 4 KB, points the writer's entry at the copy
   with write permission, and restarts the store. The other side keeps the original.
```

`fork()` is the main user: it **copies the page tables, not the data**, and marks every private writable page read-only in both parent and child. The same trick gives the zero page (a read of untouched anonymous memory maps one shared page of zeros until the first write) and private file mappings.

### 8.2 What it costs

*Measured*, a process with *N* MB of touched memory calls `fork()`:

| Resident memory | `fork()` time in the parent | Per page |
|---|---|---|
| 0 | 48 µs | |
| 64 MB (16,384 pages) | 836 µs | 51 ns |
| 256 MB | 3.8 ms | 58 ns |
| 1 GiB (262,144 pages) | **12.2 ms** | 47 ns |

The cost is proportional to the **number of pages**: about 50 ns each to copy an entry and mark it read-only. After the fork, the first write to each page took **2.5 to 2.8 µs** (a fault plus a 4 KB copy), against 1.5 µs for a fresh page.

### 8.3 What this means

- **Forking a large process stalls it.** A key-value store with 16 GiB resident (4.2 million pages) pauses for about 0.2 s when it forks to take a snapshot. This is the well-known latency spike of fork-based snapshotting, and a real design question for the `rkv` project.
- **Memory creeps up toward double.** While the child lives, every page the parent writes for the first time is copied. A write-heavy workload copies most of its hot set.
- **Huge pages make COW worse.** With 2 MB pages one write copies 2 MB (hundreds of microseconds), which is why such stores advise turning transparent huge pages off.
- **`fork` followed by `exec` wastes the copy** of the page tables. `vfork` and `posix_spawn` avoid it for programs that start many children.

---

## 9. Huge pages

### 9.1 Why and how much

A huge page (2 MB or 1 GB on x86-64) is a mapping that stops the walk early (section 3.1). It gives three things: **one TLB entry covers 512 times more memory** (reach: 2,048 entries × 2 MB = 4 GB), the walk is shorter, and the page-table entries that must stay cached shrink by 512 times. *Measured*, random gathers (`sum += a[idx[i]]` with 16 million random indices; independent loads, so the misses overlap as in tutorial 14, section 3), ns per access:

| Array size | 4 KB pages | 2 MB pages (THP) | Speed-up |
|---|---|---|---|
| 64 MB | 7.3 to 8.2 | 5.9 to 6.0 | 1.25 to 1.4 |
| 256 MB | 16.4 to 16.9 | 10.7 to 10.9 | **1.5** |
| 1 GiB | 27 to 29 | 16 to 17 | **1.6 to 1.8** |

The gain grows with the footprint, as the reach arithmetic says. (On the dependent chain of section 6.2 the gain was only 20 to 30% in this VM, probably because the hypervisor's own 4 KB mappings limit it.) The chain benchmark recorded in the operating-system note `05_operating_systems/10_virtual_memory_and_tlb.md` found gains of 1.2 to 1.3 times on one x86 Xeon and up to 4.4 times on an Arm machine, so measure on the hardware you will run on.

### 9.2 What they cost

- **Internal fragmentation and bloat:** a 2 MB page is allocated even if you use 100 KB of it.
- **Allocation stalls:** a fault must zero 2 MB (about 0.3 ms here), and if no 512 consecutive free frames exist, the kernel may first **compact** memory to make some, which can stall a thread for much longer. For latency-sensitive code this is the cost that matters.
- **Coarser sharing:** copy-on-write copies 2 MB (section 8.3); swapping and migration move 2 MB; a protection change in the middle of a huge page forces it to be split.

### 9.3 Getting them

On Linux there are two routes:

```bash
cat /sys/kernel/mm/transparent_hugepage/enabled     # always | madvise | never   (this VM: madvise)
grep -E 'AnonHuge|Rss' /proc/self/smaps_rollup       # did this process really get 2 MB pages?
```

```c
size_t len = 1UL << 30;
char *p = mmap(NULL, len + (2UL << 20), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
p = (char *)(((uintptr_t)p + (2UL << 20) - 1) & ~((2UL << 20) - 1));   // align to 2 MB, or THP cannot be used
madvise(p, len, MADV_HUGEPAGE);                                       // ask for transparent huge pages
```

- **Transparent huge pages (THP):** the kernel builds them when it can, and may split them back. Always check the `AnonHugePages` line: without alignment or with `enabled = never`, the request is silently ignored, and people conclude that "huge pages do nothing".
- **Reserved huge pages** (`hugetlbfs`, `MAP_HUGETLB`, with pages set aside by `nr_hugepages`, or 1 GB pages set aside at boot): never split, never swapped, no compaction at run time. This is what a packet-processing data plane uses.
- **Arm** has the same 2 MB / 1 GB blocks with a 4 KB granule, larger blocks with bigger granules (section 3.4), and a **contiguous bit** that lets 16 adjacent 4 KB entries (64 KB) share one TLB entry when the pages are laid out contiguously. Some cores also merge neighbouring entries on their own.

**Rule:** compute the reach (section 5.3). If the working set is far above the L2-TLB reach, and accesses are scattered, use huge pages; if the memory is small, or the program is latency-critical and cannot afford a compaction stall, reserve them in advance or do not use them.

---

## 10. Switching address spaces, shootdowns, context switches

### 10.1 ASIDs and PCIDs

The TLB entries of process A are wrong for process B. The simplest rule is to write the new `CR3` and **flush every entry**; the new process then starts with an empty TLB and pays one walk for every page it touches. If a process uses 1,000 pages and a walk costs 30 ns, that is 30 µs of misses per switch, more than the switch itself.

The fix is an **address-space tag** in each entry: **PCID** on x86 (12 bits, 4,096 contexts, placed in `CR3`) and **ASID** on Arm (8 or 16 bits, in `TTBR`). A lookup hits only if the tag matches the current one, so a switch just changes the current tag and **nothing is flushed**; entries of several processes coexist (and compete for the same entries). The operating system hands out the tags and flushes all when it has to recycle them. This VM reports `pcid` and `invpcid` in `/proc/cpuinfo`.

**Global pages** (the G bit; nG = 0 on Arm) are not tagged: they match in every address space and survive switches. The kernel's own mapping is the typical use.

### 10.2 What a context switch costs

- **Direct cost:** save and restore the registers, run the scheduler, and change the page-table base. A few hundred nanoseconds to a few microseconds.
- **Indirect cost, usually larger:** the new thread finds the L1 and L2 caches, the TLB and the branch predictors holding the *previous* thread's state, and runs slowly until they are warm again.
- A switch between **threads of one process** does not change the address space, so the TLB is kept; that is a large part of why thread switches are cheaper than process switches. A switch between user-level tasks (coroutines) does not enter the kernel at all.

### 10.3 TLB shootdowns

Caches are kept coherent by hardware (tutorial 19), but **TLBs are not**: when one core changes a page-table entry, other cores' TLBs may still hold the old translation, and nothing tells them. Software must. On x86, the core that made the change:

```text
1. changes the entry and invalidates its own TLB entry (INVLPG);
2. sends an inter-processor interrupt (IPI) to every core that is running this address space;
3. each of those cores stops what it is doing, runs INVLPG, and acknowledges;
4. the initiator waits for all acknowledgements before it goes on.
```

Cores that are idle or running another address space are skipped. Arm avoids the interrupt: the `TLBI` instructions are **broadcast** in hardware to the other cores, followed by a barrier (`DSB`) that waits for completion.

*Measured*, `mprotect` toggling one page between read-only and read-write in a loop:

| Situation | Time per `mprotect` call |
|---|---|
| the process has one thread | 1.2 to 1.6 µs |
| a second thread is **running** on the other CPU | **17 to 18 µs** |
| a second thread exists but is **asleep** | 1.6 to 1.7 µs |

Having a busy second core makes the call about **ten times slower**, and the second thread is also interrupted every time. (Interrupts are especially slow in a VM, because each one is a VM exit; on bare metal the gap is smaller but of the same shape.) Operations that change mappings and cause shootdowns: `munmap`, `mprotect`, `mremap`, freeing large blocks back to the system, copy-on-write breaks in a multi-threaded process, page migration and reclaim, splitting a huge page, and JIT compilers patching code. A tail-latency spike on an otherwise idle, isolated core is often a shootdown from a neighbouring thread's `munmap`. Remedies: do not map and unmap in the steady state (allocate from pools and arenas), batch changes, and for protection changes in a loop use protection keys (section 11.3).

---

## 11. Protection and the shape of an address space

### 11.1 A real address space

This is `/proc/self/maps` of a small program on this VM (paths and some columns trimmed). Each line is a region: start–end, permissions (`r`, `w`, `x`, `p` = private/copy-on-write):

```text
56104197c000-56104197d000 r--p  the program: ELF header, read-only data
56104197d000-56104197e000 r-xp  the program: code
561041980000-561041981000 rw-p  the program: initialised data and bss
56106d48d000-56106d4ae000 rw-p  [heap]            grows upward
7fa5c9300000-7fa5c9400000 rw-p  anonymous mmap (the 1 MB the program asked for)
7fa5c9428000-7fa5c95b1000 r-xp  libc code
7fa5c9604000-7fa5c9606000 rw-p  libc data
7fa5c978c000-7fa5c978e000 r-xp  [vdso]            kernel-provided code, e.g. fast clock_gettime
7fa5c978f000-7fa5c97ba000 r-xp  dynamic loader code
7fffe342e000-7fffe3455000 rw-p  [stack]           grows downward, near the top of the user half
```

Everything sits in the lower half (below `0x00007FFF_FFFFFFFF`); the kernel lives above `0xFFFF8000_00000000` and is invisible to user mode. Two runs of the same program placed the code at `0x56104197d209` and `0x560c7cd7d209`: **ASLR** (address-space layout randomisation, `randomize_va_space = 2`) moves the program, heap, libraries, stack and mmap area at each start, so an attacker cannot rely on addresses. The page tables are what make the shared parts shared: the libc code above is the same physical frames in every process that maps it.

### 11.2 Guard pages and NULL

Unmapped or `PROT_NONE` pages are free guards: page 0 is never mapped, so a NULL dereference faults instead of reading garbage; a gap below the stack makes a stack overflow a `SIGSEGV`; allocators and sanitizers put a `PROT_NONE` page next to a buffer so that an overrun faults at once. The cost is that every such guard is a mapping to create, and every later change of it is a shootdown (section 10.3).

### 11.3 Protection keys

Changing a page's permission with `mprotect` costs a system call and a shootdown (1.2 to 18 µs above). **Protection keys** (Intel/AMD PKU; Arm Permission Overlay Extension) do it from user mode:

- Each PTE carries a 4-bit **key** (bits 62:59), so a program can colour its pages with 16 groups.
- A user-writable register, `PKRU`, holds two bits per key: *access disable* and *write disable*.
- `WRPKRU` changes the register in a few tens of cycles. No system call, no page-table change, **no TLB invalidation**. The hardware checks the key on every data access, in parallel with the page-table permission.

Uses: a cryptographic library keeps its keys on pages that are accessible only inside its own functions; a JIT compiler keeps its code pages write-protected through a key and lifts the protection only for the thread that is patching code; a sandbox isolates components inside one process. Limits: keys apply to data accesses, not to instruction fetch; and malicious code that can run `WRPKRU` can undo it, so they protect against bugs, not against an attacker who already runs code in the process. This VM does not expose `pku`, so there is no measurement here; compare the few tens of cycles with the `mprotect` numbers of section 10.3.

### 11.4 The user/kernel split, and the cost of KPTI

For speed, the kernel is mapped into the upper half of **every** address space (with U/S = 0, so user code cannot touch it). A system call then needs no page-table switch: the core enters the kernel and the kernel's code and data are already mapped.

The Meltdown attack showed that on some processors, speculative execution in user mode could read kernel memory through that mapping before the permission check took effect (tutorial 30). The software fix is **kernel page-table isolation (KPTI)**: each process gets **two** sets of page tables. In user mode, the tables contain only the user pages and a tiny stub of kernel entry code. On each system call and interrupt, `CR3` is switched to the full tables, and switched back on the way out.

The cost is the two `CR3` writes per kernel entry, and the TLB misses that follow. With PCID the old entries survive and the overhead is tens to hundreds of nanoseconds per call. **Without PCID every entry and exit flushes the whole TLB**, and a workload with millions of small system calls per second slows down severely. Only processors that are affected need it: on this VM `/sys/devices/system/cpu/vulnerabilities/meltdown` reads `Not affected`, and a trivial system call (`getppid`) took **95 ns**. If you do see KPTI on a machine you run, cut the number of kernel entries: batch system calls (`io_uring`, `sendmmsg`, `readv`), and use the vDSO for time.

---

## 12. One load, end to end

```text
load rax, [virtual address]
  L1 data TLB hit?   yes  → physical page number at once (in parallel with the L1 set lookup, VIPT);
                            compare the physical tag; data in about 5 cycles (tutorials 13 and 14)
                     no   → L2 TLB hit?   yes → about 7 cycles more, then as above
                                          no  → page walk: walk caches, then the leaf entry through
                                                the data caches (tens of ns, over 100 ns in a VM),
                                                fill both TLBs
                                                  entry not present, or permission wrong
                                                  → page fault: the kernel fixes it (1 µs to ms)
                                                    and the instruction is restarted
  cache miss?        → L2 → L3 → DRAM, using the physical address (tutorials 13, 14 and 16)
```

Three different latencies hide behind "the load took long": a **cache miss** (up to about 200 ns), a **TLB miss** (a walk: tens to hundreds of ns), and a **page fault** (microseconds to milliseconds). They can all happen on the same load, and each has a different fix.

---

## 13. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Random access over more than ~8 MB is slower than the cache latencies explain | the working set exceeds the L2-TLB reach (section 5.3) | huge pages; process the data in page-sized chunks; shrink the structure |
| A column walk or large-stride loop is slow even with padding | one page, so one TLB entry, per access | tile so that the pages of a tile fit the L1 TLB (a tile of *T* rows with a row stride above 4 KB touches *T* pages per array) |
| Slow start-up or a latency spike on the first requests | demand-paging faults (1.5 µs per 4 KB page) | pre-fault (`MAP_POPULATE`, or touch every page, `mlockall`) before serving |
| Latency spikes on an isolated core that makes no system calls | shootdown IPIs from another thread's `munmap`/`mprotect`, or a compaction stall | no map/unmap in the steady state; pools and arenas; reserved huge pages; protection keys |
| A snapshot (fork) stalls the whole service | `fork` copies page tables in proportion to resident pages | measure it; reduce resident memory; consider turning THP off for forking stores; a fork-free snapshot design |
| Memory use doubles while a child process runs | copy-on-write of the pages the parent writes | expect up to 2× for write-heavy loads; plan headroom |
| System-call-heavy code is slow on one machine type | KPTI (no PCID, or an affected CPU) | batch system calls; `io_uring`; vDSO |
| The huge-page experiment "did nothing" | the pages were never granted (alignment, `enabled = never`) | check `AnonHugePages` in `/proc/self/smaps_rollup` before measuring |

---
## 14. Mistakes that cost points

1. **Treating translation as free.** Every access is translated. Name the TLB and its reach (8 MB here) before you blame the cache for a large random-access slowdown.
2. **Getting the arithmetic wrong.** 4 KB page = 12 offset bits; entries are 8 bytes, so 512 per page and 9 index bits per level; four levels = 48 bits. Mixing these up is the commonest slip.
3. **Forgetting what happens on a TLB miss.** The answer is a walk of up to four dependent reads, shortened by the walk caches, with the leaf read going through the data caches. Saying only "it's slow" scores nothing.
4. **Saying "page fault" for every fault.** Minor faults are normal (first touch, copy-on-write); major faults (storage I/O) are the bug; an invalid access is a segmentation fault.
5. **Believing `malloc` gives you memory.** It gives you addresses. The memory arrives at the first touch, 1.5 µs per 4 KB page (section 7.3). Pre-fault what a latency path needs.
6. **Saying `fork` copies the memory.** It copies the page tables, in time proportional to the resident pages (50 ns each); the data are copied lazily, one page per first write.
7. **Assuming caches and TLBs are both coherent.** Caches are; TLBs are not. A changed mapping needs a shootdown (IPIs on x86, a broadcast instruction on Arm), and that is how a thread's `munmap` delays another thread.
8. **Using `mprotect` or `munmap` in a hot loop of a multi-threaded program.** 1.5 µs alone, 17 µs with another thread running (section 10.3).
9. **Claiming huge pages are free, or that they never matter.** They change the reach by 512 times, and they cost compaction stalls, 2 MB fault stalls, bloat and coarse copy-on-write.
10. **Not checking that the huge pages were granted.** Alignment, the `enabled` mode and fragmentation all decide. Read `AnonHugePages` before you conclude anything.
11. **Using bare-metal numbers inside a VM.** A guest walk can need 24 reads, and a guest huge page does not give a huge TLB entry unless the host also uses large pages (section 6.3).
12. **Pricing a context switch at "save the registers".** The cost that hurts is the cold caches, TLB and predictors afterwards, unless PCID/ASID keeps the TLB.

---

## 15. Questions you will be asked

**Why do we have virtual memory?** Isolation, a uniform layout for every process, demand paging and larger-than-RAM memory, and sharing with per-mapping permissions. The price is a translation on every access.

**Walk through the translation of a virtual address on x86-64.** `CR3` points to the PML4. Bits 47:39 index it; the entry gives the PDPT; bits 38:30 index that; then the PD (29:21) and the PT (20:12); the PT entry gives the frame; bits 11:0 are the offset. Four dependent reads, then the access. Stop one level early and you get a 2 MB page (21 offset bits), two levels early a 1 GB page.

**Why 9 bits per level?** A page is 4096 bytes, an entry is 8 bytes, so a table of 512 entries is exactly one page: tables and data share one allocator.

**Why a tree and not one big table?** A flat table for a 48-bit space is 512 GiB per process. In the tree, only the ranges in use have tables: 1 GiB of data needs about 2 MiB of tables.

**What is in a page-table entry?** The frame number and permission and status bits: present, read/write, user/supervisor, accessed, dirty, no-execute, global, memory type, protection key. The hardware sets accessed and dirty; the operating system uses them for replacement and write-back.

**What is the TLB, and what happens on a miss?** A small cache of translations, checked in parallel with the L1 lookup. On a miss, a hardware walker reads the page tables (up to four dependent reads, shortened by page-walk caches) and fills it. Typical sizes: about 64 to 100 entries at L1, about 1,500 to 3,000 at L2.

**What is TLB reach, and why does it matter?** Entries × page size. 2,048 entries of 4 KB reach 8 MB, so a random-access working set above 8 MB starts to walk page tables, even if it fits in the L3.

**How does the L1 cache avoid waiting for the TLB?** It is virtually indexed and physically tagged: the set is chosen from the page-offset bits while the TLB translates, and the physical tag is compared afterwards (tutorial 14, section 5).

**Why is a TLB miss costlier in a VM?** The guest's page tables produce guest-physical addresses, which the host translates with a second tree, so a walk can take up to 24 reads. The TLB caches the combined result at the smaller of the two page sizes.

**What is a page fault, and what happens to the faulting instruction?** An exception on a not-present entry or a permission mismatch. The handler fixes the mapping (a minor fault) or reads the data (a major fault) or kills the process; then the instruction restarts from the beginning, which is possible because the exception is precise.

**What is demand paging, and how do you avoid its latency?** Frames are attached at first touch. Pre-fault the memory at start-up (`MAP_POPULATE`, touching every page, `mlockall`), or use reserved huge pages.

**What is copy-on-write? Why is `fork` fast but not free?** Pages are shared read-only and copied at the first write. `fork` copies only page tables, about 50 ns per resident page: 12 ms for 1 GiB, 0.2 s for 16 GiB. Then every first write costs a fault and a copy.

**When do you use huge pages, and what do they cost?** When the scattered working set exceeds the L2-TLB reach. They cost allocation and compaction stalls, bloat and coarse copy-on-write. For latency-critical code, reserve them (`hugetlbfs`) rather than rely on THP; always verify with `AnonHugePages`.

**What is a TLB shootdown?** Hardware does not keep TLBs coherent, so after a mapping change the initiating core interrupts every core running that address space to invalidate the entry, and waits. Arm broadcasts the invalidation in hardware instead.

**What do ASIDs and PCIDs do?** They tag TLB entries with an address-space number, so switching processes needs no flush. Without them, every switch costs a refill of the working set.

**What is KPTI and what does it cost?** Separate user and kernel page tables, to stop Meltdown-style reads of kernel memory. Each kernel entry and exit writes `CR3`; with PCID it costs tens to hundreds of nanoseconds, without it a full TLB flush each time. Unaffected processors skip it.

**What are protection keys?** Four bits in the PTE choose one of 16 groups; a user register says which groups are readable and writable; changing it takes tens of cycles, with no system call and no TLB invalidation.

**What is Belady's anomaly?** With FIFO replacement, giving a process more frames can raise its fault count (9 to 10 on `1 2 3 4 1 2 5 1 2 3 4 5` with 3 and then 4 frames). LRU and OPT cannot show it.

**A thread on an isolated core has a 400 µs latency spike. What could it be?** A shootdown IPI from a sibling thread's `munmap`/`mprotect`; a THP compaction or a huge-page fault (0.3 ms); a major fault; or a minor fault on a first touch. Name the shootdown: it is the one people miss.

**Trace what happens the first time a program reads a byte of freshly `malloc`'d memory.** L1 TLB miss, L2 TLB miss, page walk, the entry is not present, page fault, the kernel finds a region, takes a frame, zeroes it, fills the entry, the instruction restarts, TLB miss and walk again (now a hit in the tables), TLB filled, cache miss to DRAM, data returned.

---

## 16. Problems

**How to use this section.** Try each problem before you read the answers. A to C are paper problems (25 to 30 minutes each); D is paper plus a few commands on your own machine.

### Problem A (paper): addresses and page-table size

(a) Split the x86-64 virtual address `0x00005555_AAAA_1234` (4 KB pages): the four table indices in decimal, and the offset. What would the offset be if the address lay in a 2 MB page, and in a 1 GB page? Is the address canonical, and which half is it in?
(b) A process maps a contiguous, suitably aligned 6 GiB region. How many page-table pages and how many bytes of tables are needed with 4 KB pages, with 2 MB pages and with 1 GB pages?
(c) A 32-bit machine has two-level paging: 10 bits for the directory, 10 for the table, 12 for the offset, 4-byte entries. The page directory is at physical `0x00001000`. Entry 0 points to a page table at `0x00006000`, entry 3 to a page table at `0x00005000` (both present, writable, user); all other directory entries are not present. In the table at `0x00005000`, entry 1 maps frame `0x0A2` (present, user, **read-only**) and entry 2 is not present. In the table at `0x00006000`, entry 0 maps frame `0x1F0` (present, writable, user). For each access give the physical address or the kind of fault, and the number of memory references the walk needs: (i) read `0x00C01ABC`, (ii) write `0x00C01ABC`, (iii) read `0x00C02000`, (iv) write `0x00000FFF`, (v) read `0x01000000`.

*Where:* paper. *How:* sections 2 and 3. Directory index = address >> 22; table index = (address >> 12) & 0x3FF. A write needs write permission at **every** level.

### Problem B (paper, optional run): TLB reach and cost

A core has a 64-entry L1 data TLB and a 1,536-entry L2 TLB (shared by 4 KB and 2 MB pages). An L2-TLB hit adds 2 ns, a page walk costs 35 ns on average (walk caches hit; the leaf entry comes from the L2 or L3 cache), and a DRAM access costs 90 ns.

(a) Compute the reach of the L1 and L2 TLB with 4 KB pages, and of the L2 TLB with 2 MB pages.
(b) A 1.5 GiB hash table is probed at uniformly random positions, one DRAM access per probe. With 4 KB pages: how many pages, what fraction of probes hit the L2 TLB, and what is the average time per probe? With 2 MB pages? What is the speed-up?
(c) How large are the leaf page-table entries for the 1.5 GiB on 4 KB pages? Do they fit a 2 MB L2 cache?
(d) The same 1.5 GiB array is scanned sequentially at 10 GB/s. How much time do the TLB misses add?
(e) A program walks a column of a 2048 × 2048 matrix of doubles (row stride 16 KiB). How many distinct pages does one column touch? Will the L2 TLB hold them? What is the cost per element, and what does tiling change?
(f) *(Optional, your machine.)* Run `getconf PAGESIZE`, and on Linux `grep -E 'pcid|pdpe1gb' /proc/cpuinfo`. What page size does your machine use?

*Where:* paper for (a) to (e). *How:* sections 5 and 6, and tutorial 14, section 6 for the tile argument. Pages = size / page size; a sequential scan takes one miss per new page.

### Problem C (paper): faults, replacement, `fork`

(a) The reference string `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1` with 3 frames: how many faults for FIFO, LRU and OPT? Mark each fault.
(b) Show Belady's anomaly: FIFO on `1 2 3 4 1 2 5 1 2 3 4 5` with 3 frames, then 4 frames.
(c) A memory access takes 100 ns, a minor fault costs 2 µs and a major fault 8 ms. What is the average access time if 1 access in 1,000 is a minor fault? What is the largest major-fault rate that keeps the slowdown below 10%?
(d) A process has 8 GiB resident (4 KB pages). How long does `fork()` stall it, at 47 ns per page? While the child runs, the parent writes to random 4 KB pages until 5% of its pages have been written. How many copy-on-write faults is that, how much extra memory, and how much CPU time at 2.7 µs per fault? Then repeat the memory estimate if the process uses 2 MB transparent huge pages.

*Where:* paper. *How:* sections 7 and 8. For (d), with huge pages the parent has 4,096 huge pages; compute the expected number touched by 104,858 random writes.

### Problem D (paper and your machine): PTE bits, address spaces, features

(a) Decode the x86-64 entry `0x8000000012AB4025`: the frame number, and each of P, R/W, U/S, A, D, NX. What is the physical address of the byte at offset `0x123` in this page?
(b) What happens on each access: (i) user-mode read, (ii) user-mode write, (iii) user-mode instruction fetch, (iv) kernel-mode read with SMAP enabled and no override? After a successful access, which bit does the hardware change?
(c) Two processes alternate 10,000 times per second; each uses 500 pages; a walk costs 35 ns. How much of one core is spent refilling the TLB if every switch flushes it, and how much with PCID?
(d) *On your machine:* run `grep -o -w -E 'pcid|invpcid|pku|ospke|la57|pdpe1gb|nx|smep|smap' /proc/cpuinfo | sort -u`, `cat /sys/kernel/mm/transparent_hugepage/enabled` and `cat /sys/devices/system/cpu/vulnerabilities/meltdown`. Say in one line what each feature found does. Run any program that prints `/proc/self/maps` twice, and compare the code addresses. (On a Mac: `pagesize`, and `vmmap` on a running process.)

*Where:* paper for (a) to (c); a Linux terminal for (d). *How:* sections 4, 10 and 11.

---

## Answers

### Problem A

(a) `0x00005555_AAAA_1234`: bits 47:39 = **170**, bits 38:30 = **342**, bits 29:21 = **341**, bits 20:12 = **161**, offset **`0x234`**. In a 2 MB page the offset is the low 21 bits, **`0xA1234`**; in a 1 GB page the low 30 bits, **`0x2AAA1234`**. Bits 63:48 are zero and bit 47 is 0, so the address is canonical and lies in the **lower (user) half**.

(b) 6 GiB, aligned:

| Page size | Pages of tables | Bytes |
|---|---|---|
| 4 KB | 3,072 PT pages (one per 2 MB) + 6 PD pages (one per GB) + 1 PDPT + 1 PML4 = **3,080** | 12.03 MiB |
| 2 MB | 6 PD + 1 PDPT + 1 PML4 = **8** | 32 KiB |
| 1 GB | 1 PDPT + 1 PML4 = **2** | 8 KiB |

(c)

| Access | Directory / table index | Result | References |
|---|---|---|---|
| (i) read `0x00C01ABC` | 3 / 1, offset `0xABC` | frame `0x0A2` → physical **`0x000A2ABC`** | 2 for the walk (3 with the data) |
| (ii) write `0x00C01ABC` | same | **protection fault**: the table entry is read-only | 2 |
| (iii) read `0x00C02000` | 3 / 2 | **page fault**: entry not present | 2 |
| (iv) write `0x00000FFF` | 0 / 0, offset `0xFFF` | frame `0x1F0` → physical **`0x001F0FFF`** | 2 (3 with the data) |
| (v) read `0x01000000` | 4 / – | **page fault**: directory entry 4 not present | 1 |

### Problem B

(a) L1 TLB, 4 KB: 64 × 4 KB = **256 KB**. L2 TLB, 4 KB: 1,536 × 4 KB = **6 MB**. L2 TLB, 2 MB: 1,536 × 2 MB = **3 GB**.

(b) 4 KB pages: 1.5 GiB / 4 KB = **393,216 pages**. The L2 TLB holds 1,536 of them, so 1,536 / 393,216 = **0.4%** of probes hit it: effectively every probe walks. Average ≈ 90 + 35 = **125 ns**. With 2 MB pages: 1.5 GiB / 2 MB = **768 pages**, fewer than 1,536, so every probe hits the L2 TLB (the L1 TLB, 64 entries, mostly misses): about 90 + 2 = **92 ns**. Speed-up ≈ 125 / 92 = **1.36**.

(c) 393,216 entries × 8 B = **3 MiB**, more than a 2 MB L2 cache, so the leaf reads often come from the L3, which is why a walk costs more than a pure L2 hit. (With 2 MB pages there are only 768 entries, 6 KB.)

(d) 393,216 pages × 35 ns = 13.8 ms of walks, against a scan time of 1.5 GiB / 10 GB/s = 161 ms: about **8.5%** (less in practice: the walkers overlap with the streaming loads, and the leaf lines are shared by 8 pages).

(e) One column: **2,048 pages** (every row is 16 KiB apart). The L2 TLB holds only 1,536, and a cyclic sweep over more pages than entries with LRU-like replacement misses every time: **every element walks**, about 35 ns on top of the cache miss. Padding the rows does not help (they are still on different pages). **Tiling** helps: a tile of *T* rows touches *T* pages per array, so choose *T* so that the pages of the tile fit the 64-entry L1 TLB (for example *T* = 16 rows of two or three arrays); each page translation is then reused for the whole tile width.

(f) On x86 Linux, `getconf PAGESIZE` prints 4096; on an Apple-silicon Mac it prints 16384. `pcid` in the flags means tagged TLB entries; `pdpe1gb` means 1 GB pages are supported.

### Problem C

(a) Three frames, string `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1` (`F` = fault, `.` = hit):

```text
ref :  7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1
FIFO:  F F F F . F F F F F F . . F F . . F F F      15 faults
LRU :  F F F F . F . F F F F . . F . F . F . .      12 faults
OPT :  F F F F . F . F . . F . . F . . . F . .       9 faults
```

(b) `1 2 3 4 1 2 5 1 2 3 4 5` with FIFO:

```text
3 frames:  F F F F F F F . . F F .       9 faults
4 frames:  F F F F . . F F F F F F      10 faults
```

More memory, more faults.

(c) With 1 in 1,000 accesses a minor fault: 0.999 × 100 + 0.001 × 2,000 = 99.9 + 2 = **101.9 ns**, a 1.9% slowdown. For a slowdown below 10%, the average must be at most 110 ns, so `p × 8,000,000 ≤ 10` (approximately), giving `p ≤ 10 / 8,000,000 =` **1.25 × 10⁻⁶**, about 1 major fault in 800,000 accesses.

(d) 8 GiB / 4 KB = **2,097,152 pages**; `fork` stalls for 2,097,152 × 47 ns ≈ **99 ms**. Writing 5% of the pages: 104,858 copy-on-write faults × 2.7 µs ≈ **0.28 s** of CPU, and 104,858 × 4 KB ≈ **430 MB** of extra memory. With 2 MB pages: 8 GiB is 4,096 huge pages, and 104,858 random writes touch essentially all of them (expected distinct pages = 4,096 × (1 − e^(−25.6)) ≈ 4,096), so **the whole 8 GiB is copied**: memory doubles, and at about 0.2 ms per 2 MB copy (10 GB/s) that is roughly **0.8 s** of copying. The same workload costs 20 times more memory with huge pages.

### Problem D

(a) `0x8000000012AB4025`: frame number (bits 51:12) = **`0x12AB4`**. Flags `0x25` = `0b100101`: **P = 1**, **R/W = 0** (read-only), **U/S = 1** (user can access), **A = 1**, **D = 0** (never written), and bit 63: **NX = 1**. Physical address of offset `0x123`: `0x12AB4` × 4096 + `0x123` = **`0x12AB4123`**.

(b) (i) The read succeeds; nothing changes (A is already 1). (ii) The write faults: R/W = 0, so a **protection fault**. If this is a copy-on-write page, the kernel copies the page, makes the new entry writable and restarts the store (after which D becomes 1); otherwise the process gets SIGSEGV. (iii) The fetch faults: NX = 1. (iv) The kernel read of a user page with SMAP enabled and no override faults: the kernel is not supposed to touch user memory except in explicit, marked places. After a successful write to a writable page the hardware sets D (and A on any first access).

(c) Without PCID: 10,000 × 500 × 35 ns = 175 ms per second, **17.5% of a core**. With PCID the entries survive the switch, so the refill cost is close to **zero** (apart from any entries evicted because the two processes compete for TLB space).

(d) Example reading of a typical cloud x86 machine: `pcid` and `invpcid`: tagged entries and a selective invalidate, so address-space switches need no flush; `nx`: no-execute pages; `smep`, `smap`: the kernel cannot execute or touch user pages by accident; `pdpe1gb`: 1 GB pages; `pku`/`ospke` (when present): protection keys; `la57`: five-level paging. THP `enabled` shows `always`, `madvise` or `never`. `meltdown: Not affected` means KPTI is off. Two runs of the maps program should show different code, heap, stack and library addresses (ASLR) while the layout order (code, heap, libraries, stack) stays the same.

---

## Cheat sheet

- **Why:** isolation, uniform layout, demand paging, sharing; the cost is a translation per access.
- **Split:** 4 KB page = 12 offset bits. Entry = 8 bytes → 512 per table → 9 index bits per level. x86-64: PML4 (47:39), PDPT (38:30), PD (29:21), PT (20:12), offset (11:0); `CR3` is the base. 5-level: 57 bits. 2 MB page = stop at PD; 1 GB = stop at PDPT. Arm: `TTBR0` user, `TTBR1` kernel; granules 4/16/64 KB.
- **Canonical:** bits 63:48 = bit 47. User = lower half, kernel = upper half.
- **Table sizes:** flat 48-bit = 512 GiB; tree: 1 GiB of data ≈ 2 MiB of tables (515 pages); 2 MB pages: 12 KiB.
- **PTE:** P, R/W, U/S, A, D, PS, G, NX, memory type (PAT/PCD/PWT), key (62:59), frame (51:12). Hardware sets A and D. Permissions must hold at every level.
- **TLB:** entries = tag + frame + permissions + page size + PCID/ASID + global. L1 about 96 entries (reach 384 KB here), L2 about 2,048 (8 MB); hit in parallel with the VIPT L1 lookup. **Reach = entries × page size.** Measured knees: 96 and 2,048 pages.
- **Walk:** up to 4 dependent reads; walk caches skip the top levels; leaf entries come through the data caches (8 per line). Measured here: a hop that missed the TLB took 100 to 250 ns in the VM, data access included. In a VM, up to 24 reads (nested paging, tutorial 27).
- **Faults:** minor (about 1.5 µs; first touch, copy-on-write), major (storage; a bug in a data plane), invalid (SIGSEGV). The instruction restarts. `malloc` gives addresses; frames come at first touch. Pre-fault with `MAP_POPULATE`/`mlockall`.
- **Replacement:** OPT, LRU (approximated with A bits: clock), FIFO (Belady's anomaly: 9 to 10 faults on `1 2 3 4 1 2 5 1 2 3 4 5`). EAT = (1 − p) t_mem + p t_fault.
- **COW and `fork`:** page tables copied, about 50 ns per page (12 ms per GiB); first write after fork 2.5 to 2.8 µs; memory creeps up toward 2×; huge pages make it worse.
- **Huge pages:** 2 MB / 1 GB; reach × 512; 1.5 to 1.8 times on a 1 GiB random gather; cost: 0.3 ms fault, compaction, bloat, coarse COW. THP needs 2 MB alignment: verify `AnonHugePages`. Reserve (`hugetlbfs`) for data planes.
- **Switching:** PCID/ASID tags avoid flushes; global pages survive. Thread switches keep the address space. Indirect cost (cold caches, TLB) exceeds direct cost.
- **Shootdowns:** TLBs are not coherent; x86 uses IPIs, Arm a broadcast `TLBI`. `mprotect` 1.2 to 1.6 µs alone, 17 µs with a running second thread. Causes: `munmap`, `mprotect`, JIT, COW breaks, migration, THP split.
- **Protection:** NX and W^X, SMEP/SMAP (PXN/PAN), guard pages, NULL page unmapped; protection keys: 4 bits in the PTE, `PKRU`, `WRPKRU` in tens of cycles, no TLB flush.
- **Layout and KPTI:** code, data, heap, mmap area, stack, vDSO; ASLR. Kernel in the upper half of every space; KPTI = two page-table sets, `CR3` switched on each kernel entry; cheap with PCID; not needed on unaffected CPUs (`getppid` 95 ns here).

**Next:** tutorial 16, DRAM, Memory Controllers and NUMA (SRAM and DRAM cells, refresh, rows, banks and channels, row-buffer hits and DRAM timing, DDR4/DDR5 and HBM, bandwidth against latency and Little's law, address interleaving, controller scheduling, ECC and Rowhammer, NUMA placement and `numactl`). Say "next" when you are ready.
