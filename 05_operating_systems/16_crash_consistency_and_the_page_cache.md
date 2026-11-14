# OS 16: Crash consistency, journaling and the page cache

*Part of the [Operating systems plan](00_plan.md) · Time: about 10 hours (7 reading and experiments, 3 problems) · You need: [OS 15](15_file_systems.md) (inodes, bitmaps, directories, block groups), [OS 14](14_io_systems_and_disk_scheduling.md) (block devices, request cost) and [OS 11](11_virtual_memory.md) (the page cache is file-backed memory)*

This is a new tutorial. OS 15 described a file system that never fails. This one asks what is left on the disk when the power goes out in the middle of an operation, and what the application can do about it. The interview questions come in a fixed order. What does `write` actually do, and when does the data reach the disk? What can go wrong if the machine crashes between the three disk writes that make up one file append, and what does `fsck` do about it? How does a journal fix it, in what order are its blocks written, and what is the difference between data, ordered and writeback modes? What does `fsync` guarantee that `write` does not, and why does the "write a temporary file, then `rename`" pattern exist? What are `fdatasync`, `O_SYNC`, `O_DIRECT` and the directory `fsync`? How does a log-structured or copy-on-write file system avoid the problem by never overwriting, and what does it pay for that (the cleaner)? How do checksums catch corruption that the disk does not report? And what do ext4, XFS, btrfs, ZFS and F2FS each choose?

**Two honest limits, stated first.** (1) A power cut cannot be done from inside this cloud VM, so the crashes are **simulated**: a loop-mounted ext4 image is run under a workload, the loop device is made read-only at a random instant (every write issued before the instant reaches the image, every write after it fails), and the image is then copied and recovered. That models "the disk stopped accepting writes at time t". It is **milder than a real power failure**: a real disk with a volatile write cache can lose or reorder writes it already acknowledged, which this model cannot do, so a file system that survives here is not proven to survive there. (2) The tools for XFS, btrfs, ZFS and F2FS are not installed, so those are described from sources and from memory and are marked as such; everything measured is ext4 or `tmpfs`.

By the end you can:

- explain what `write` does to the page cache and when data reaches the disk (measured: 512 MiB went into the cache at 5,159 MB/s, the `fsync` that made it durable took 0.43 s, and 64 MiB of unsynced dirty data was written out by the kernel after 30.7 s, the 30-second expiry);
- list the six outcomes of a crash during a one-block append and say which are harmful (measured: of 40 random-instant crashes with no journal, 22 left fsck errors; with the journal replayed at mount, 0 did);
- write the journaling protocol (journal write, commit, checkpoint, free), say why the commit block is a separate write, and read a real journal (measured: one `mkdir`, `create` and 4 KiB write logged 7 blocks in one transaction, an 8th with `data=journal`);
- say what each journal mode guarantees and costs (measured: `data=journal` sent 2.01 times the data to the device and ran at about 215 MB/s against 552 for `ordered`);
- use `fsync`, `fdatasync`, `O_DSYNC` and the directory `fsync` correctly, and explain the write-then-rename pattern (measured: renaming a new file over an old one with no `fsync` left a zero-length file in 17 of 20 crashes with `noauto_da_alloc`, and a durable new file in 20 of 20 with `fsync` first);
- explain log-structured and copy-on-write designs, the cleaner and write amplification (simulated: 5.0 at 90 percent full under uniform writes, 3.3 for cost-benefit cleaning of a hot and cold mix against 5.6 for greedy);
- compare checksums by what they miss and what they cost (measured: XOR and addition missed 100,000 and 51,566 of 100,000 same-bit flips in two words; CRC32C missed none and ran at 10 GB/s with the hardware instruction against 0.47 GB/s for the table version);
- say what ext4, XFS, btrfs, ZFS and F2FS each choose and what an application can still rely on.

**Where the numbers come from.** Programs and scripts were compiled and run with GCC 13.3 (`-O2 -Wall -Wextra`) and e2fsprogs 1.47.0 on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory, as root. The root file system is ext4 on a virtio disk that is probably cached by the host, so device latencies here are far below a real disk's. The journal and crash experiments use ext4 images of 64 MiB to 4 GiB on loop devices. The kernel documents (the ext4 journal, ext4 mount options, `vm` sysctls, F2FS) and the manual pages for `fsync` and `open` were fetched, as were the OSTEP chapters on crash consistency, log-structured file systems and data integrity; see Sources for what each did and did not say. XFS, btrfs and ZFS internals, the `fsyncgate` history, SSD power-loss protection and similar facts are marked "from memory".

**Data and timing.** Counts of blocks, requests and crash outcomes are deterministic or close to it (the crash instants are random, so the outcome counts vary by a few between runs). Timings were taken once or twice and vary by about 20 percent between my two runs, which is shown where it matters. Crash outcomes are counts out of 20 or 40 trials, so a difference of one or two is noise.

---

## 1. The page cache and write-back

### 1.1 What `write` does

[OS 14](14_io_systems_and_disk_scheduling.md) and [OS 11](11_virtual_memory.md) (section 1.5) introduced the **page cache**: file data lives in memory pages keyed by (file, offset). A `write` by default does **not** go to the disk. The kernel copies the bytes into the page for that file offset, marks the page **dirty** and returns. The page is written to the device later by **write-back**, done by kernel flusher threads, and the process is not waiting for it. That is the reason `write` is fast, and also the reason that `write` returning says nothing about whether the data would survive a crash.

The program writes 512 MiB in 1 MiB writes under four regimes (the file lives on the root ext4 disk):

```c
static void mode_write(const char *dir)
{
    char p[512]; snprintf(p, sizeof p, "%s/g1_w", dir);
    char *buf = NULL; IGN(posix_memalign((void **)&buf, 4096, 1 << 20)); memset(buf, 7, 1 << 20);
    printf("512 MiB written in 1 MiB writes\n");
    int fd = open(p, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    double t0 = now();
    for (int i = 0; i < 512; i++) IGN(write(fd, buf, 1 << 20));
    double t1 = now();
    printf("  buffered, write() calls only:      %8.0f MB/s (%.2f s); Dirty now %ld MiB\n", 512 / (t1 - t0), t1 - t0, meminfo_kb("Dirty") / 1024);
    fsync(fd);
    double t2 = now();
    printf("  then fsync() took:                 %8.2f s; with it %6.0f MB/s all in\n", t2 - t1, 512 / (t2 - t0));
    close(fd);
    fd = open(p, O_CREAT | O_TRUNC | O_WRONLY | O_DIRECT, 0644);
    t0 = now();
    for (int i = 0; i < 512; i++) IGN(write(fd, buf, 1 << 20));
    t1 = now();
    printf("  O_DIRECT:                          %8.0f MB/s (%.2f s); Dirty now %ld MiB\n", 512 / (t1 - t0), t1 - t0, meminfo_kb("Dirty") / 1024);
    close(fd);
    fd = open(p, O_CREAT | O_TRUNC | O_WRONLY | O_DSYNC, 0644);
    t0 = now();
    for (int i = 0; i < 512; i++) IGN(write(fd, buf, 1 << 20));
    t1 = now();
    printf("  O_DSYNC (each write is durable):   %8.0f MB/s (%.2f s)\n", 512 / (t1 - t0), t1 - t0);
    close(fd);
    unlink(p);
}
```

```text
512 MiB written in 1 MiB writes
  buffered, write() calls only:          5159 MB/s (0.10 s); Dirty now 512 MiB
  then fsync() took:                     0.43 s; with it    961 MB/s all in
  O_DIRECT:                              1757 MB/s (0.29 s); Dirty now 0 MiB
  O_DSYNC (each write is durable):        713 MB/s (0.72 s)
```

- **Buffered**: the `write` calls completed at 5,159 MB/s, a memory-copy speed, and `/proc/meminfo` showed **512 MiB dirty**: all of it was only in memory. The `fsync` that followed took about 0.4 s, and the whole job, durable, ran at about 960 MB/s.
- **`O_DIRECT`** bypasses the cache: the data goes from the program's buffer to the device, `Dirty` stays 0, and the rate (about 1,750 MB/s) is the device's. It is slower than the cache copy and **faster than buffered plus `fsync`**, because it avoids the copy and the cache lookups (section 6).
- **`O_DSYNC`** makes each `write` durable before it returns, as if each were followed by `fdatasync` (the `open` manual page, fetched): about 710 MB/s with 1 MiB writes. With small writes the same flag is far worse (section 5.2).

All of these numbers belong to this virtual disk. The ratios, buffered much faster than durable, are what generalise.

### 1.2 Reads: hit, miss and readahead

The cache also serves reads. The program reads a 1 GiB file after dropping all caches, then again, then with `O_DIRECT`:

```text
1 GiB file, 1 MiB reads
  resident in the cache after dropping caches:       0.0 MiB
  cold buffered read:                               3378 MB/s
  resident after the read:                        1024.0 MiB
  warm buffered read:                               6770 MB/s
  warm mmap, one byte touched per page:               38 ns per page (262144 pages; checksum 0)
  O_DIRECT read of a warm file (cache bypassed):    2563 MB/s
  O_DIRECT read after dropping caches:              2603 MB/s
  resident after O_DIRECT reads:                     0.0 MiB
  after one cold 4 KiB read at offset 0:        16 KiB resident (readahead)
  after reading the first 32 KiB in 4 KiB steps:    64 KiB resident
```

A cold read ran at 3,378 MB/s, filling the cache (1,024 MiB resident afterwards, found with `mincore`); the warm read ran at 6,770 MB/s, limited by the copy to user space, with no device I/O. The `O_DIRECT` read, which neither uses nor fills the cache, ran at about 2,600 MB/s whether or not the file was cached, and left **0 MiB** resident. A program that maps the file with `mmap` pays no copy: touching one byte per page of the warm file cost about 38 ns per page (a minor fault that maps the cache page). One cold 4 KiB `read` left **16 KiB** resident and reading the first 32 KiB in 4 KiB steps left **64 KiB**: the **readahead** window starts small and grows as the access pattern proves sequential (the mechanism is in OS 14 section 2.3; the sizes are this machine's).

### 1.3 Dirty data: thresholds, flusher threads, throttling

Write-back is governed by four numbers, the `vm` sysctls (the kernel document, fetched):

| Sysctl | Default here | Meaning |
| --- | --- | --- |
| `dirty_background_ratio` | 10 | when dirty pages reach this percentage of **available** memory, the background flusher threads start writing |
| `dirty_ratio` | 20 | when dirty pages reach this percentage, **the writing process itself** is made to write out dirty data (it is throttled) |
| `dirty_expire_centisecs` | 3,000 | dirty data older than 30 s becomes eligible for write-out |
| `dirty_writeback_centisecs` | 500 | the flusher threads wake every 5 s to write out the old data |

"Available memory" is **not** total memory: it is free pages plus reclaimable pages (the document says so explicitly), which is about 7,676 MiB here, so the two thresholds are about 767 MiB and 1,535 MiB. There are byte-valued twins (`dirty_background_bytes`, `dirty_bytes`); setting one zeroes the other.

**The expiry, measured.** 64 MiB were written with no `fsync`, and `Dirty` was polled:

```text
64 MiB written with no fsync; dirty_expire_centisecs 3000, dirty_writeback_centisecs 500
  t  0.0 s: Dirty 64 MiB
  t  5.0 s: Dirty 64 MiB
  t 10.0 s: Dirty 64 MiB
  t 15.0 s: Dirty 64 MiB
  t 20.1 s: Dirty 64 MiB
  t 25.1 s: Dirty 64 MiB
  t 30.1 s: Dirty 64 MiB
Dirty fell below 8 MiB at t = 30.7 s
```

`Dirty` stayed at 64 MiB for 30 s and the data was on the disk at 30.7 s: expiry (30 s) plus a flusher wake-up within the 5 s interval. **That is the window in which a crash loses data that `write` reported as done**: up to about 35 seconds for a data page nobody synced (the ext4 documentation makes the same point about `commit=` and delayed allocation: writeback waits for `dirty_expire_centisecs`).

**The thresholds, measured, with a limit.** The program writes 3 GiB in 1 MiB writes, sampling `Dirty` and `Writeback` every quarter second:

```text
dirty_background_ratio 10, dirty_ratio 20; free plus file pages 7676 MiB, so about 767 MiB and 1535 MiB
 t (s) written MiB  Dirty MiB  Writeback   write MB/s
  0.26        926        670         92         3587
  0.52       1415        687         72         1845
  0.77       1988        644        180         2290
  1.02       2317        789        364         1312
  1.28       2359        831        364          162
  1.55       2657        609        344         1134
  1.79       3072        616          0         1678
3 GiB written in 1.79 s without fsync; Dirty 616 MiB
fsync of what was left took 0.55 s
```

The writer finished 3 GiB in under 2 seconds, and `Dirty` hovered between about 610 and 830 MiB: the **background threshold** (767 MiB) is where the flusher caught the writer, and the writer's rate dipped once, to about 160 MB/s, when `Writeback` reached 364 MiB. **It never reached `dirty_ratio` (1,535 MiB)**, because this virtual disk absorbs more than 2 GB/s, so the writer was never throttled for long. To see the throttle I lowered the limits to 128 MiB and 512 MiB (`dirty_background_bytes`, `dirty_bytes`) for one run and restored them:

```text
dirty_background_bytes 128 MiB, dirty_bytes 512 MiB
 t (s) written MiB  Dirty MiB  Writeback   write MB/s
  0.25        724        112        204         2896
  0.52       1154         66         40         1610
  1.13       1292         84        104          225
  1.40       1957        257         20         2501
  1.65       2361        117         84         1584
  1.90       2814         94         68         1810
  2.01       3072         80         68         2510
3 GiB written in 2.01 s without fsync; Dirty 80 MiB
fsync of what was left took 0.27 s
```

The shape is the same with smaller numbers: `Dirty` never passed 260 MiB, and the writer's rate fell to about 225 MB/s at the moments when it had to wait. The honest summary: the mechanism is visible (flusher starts at the background limit; the writer is slowed at the hard limit), but on this device I could not make the writer stall for long, and on a slow disk the same writes would block much more. The effect on a real server: **a burst of writes larger than `dirty_ratio` makes every writer wait for the disk**, which shows as latency spikes in unrelated processes (OS 14 section 7.3 measured the read side of this).

---

## 2. The crash-consistency problem

### 2.1 One append, three writes

OSTEP's example (fetched) is the smallest one that shows the problem. A file has one data block. The program appends a second 4 KiB block. The file system must change **three** things on the disk:

- the **inode**, from version 1 (size 1, one pointer) to version 2 (size 2, a second pointer, to block 5): `I[v2]`;
- the **data bitmap**, marking block 5 allocated: `B[v2]`;
- the new **data block** `Db`, at block 5.

These are three separate writes to three different places, and the disk guarantees atomicity only for **one 512-byte sector** (OSTEP says the same about the journal's commit block). A crash can occur after any subset. The six interesting cases, from the chapter:

| Writes that reached the disk | Result | Harm |
| --- | --- | --- |
| only `Db` | no inode points to it and the bitmap says free | none to the file system; the append is lost |
| only `I[v2]` | the inode points to block 5, which holds old garbage; the bitmap says free | **inconsistent**; the file reads garbage and the block can be given to another file |
| only `B[v2]` | block 5 marked allocated, no inode uses it | a **space leak**, consistent otherwise |
| `I[v2]` and `B[v2]`, not `Db` | the metadata agrees, but block 5 holds garbage | **consistent metadata, garbage data** (the file system cannot tell) |
| `I[v2]` and `Db`, not `B[v2]` | the inode and the bitmap disagree | **inconsistent**; the block may be allocated twice |
| `B[v2]` and `Db`, not `I[v2]` | allocated, written, owned by no file | a leak |

The **crash-consistency problem** is to leave the file system in a state that is either entirely before or entirely after the operation, **atomically**, when the hardware gives no multi-block atomic write. The classic answers, in the order history found them: do nothing now and repair afterwards (`fsck`, section 3); log the intent first (journaling, section 4); never overwrite (log-structured and copy-on-write, section 7); order the writes so that any partial state is harmless (soft updates, section 4.7).

### 2.2 The experiment: crashing a real file system

The harness stops the disk at a random instant and looks at what is left. The function that does it, which every crash test below uses:

```bash
crash_once() {            # $1 mkfs options, $2 mount options, $3.. workload arguments; leaves $W/snap
    local mk=$1 mo=$2; shift 2
    rm -f $W/img $W/snap; truncate -s 64M $W/img
    mkfs.ext4 -q -F -b 4096 $mk $W/img
    local dev; dev=$(losetup -f --show $W/img)
    mount -o $mo $dev $W/m
    setsid $BIN/g5_work "$1" $W/m "${@:2}" >/dev/null 2>&1 &
    local pid=$!
    sleep $(awk -v s=$RANDOM 'BEGIN{srand(s); printf "%.3f", 0.8 + rand() * 2.2}')
    blockdev --setro $dev
    sleep 0.3
    kill -9 -- -$pid 2>/dev/null; wait $pid 2>/dev/null
    umount -f $W/m 2>/dev/null || umount -l $W/m 2>/dev/null
    blockdev --setrw $dev 2>/dev/null; losetup -d $dev 2>/dev/null
    cp --sparse=always $W/img $W/snap
}
```

The workload runs in its own process group; `blockdev --setro` makes every later write fail (so the image holds exactly the writes issued before the instant); the workload is killed; the file system is unmounted and the image copied. The first test uses a metadata-heavy workload (create files of 1 to 16 blocks, rename every third one, delete old ones, append to a log) with a second process calling `sync()` in a loop, so the disk is constantly being written, and crashes 40 times, then runs `e2fsck -fn` (check, change nothing) on what was left. The comparison is **ext4 with no journal** against **ext4 with its journal, replayed by mounting the image** before the check:

```text
  no journal: 40 crashes, 18 clean, 22 with fsck errors
    7 Fix? no
    2 Deleted inode 55 has zero dtime.  Fix? no
    2 Deleted inode 39 has zero dtime.  Fix? no
    1 Unattached zero-length inode 64.  Clear? no
  journal, replayed at mount: 40 crashes, 40 clean, 0 with fsck errors
```

Without the journal, **22 of 40 crashes left a file system that `fsck` complained about**. The complaints printed are the ones from section 2.1 in real form: "Deleted inode 55 has zero dtime" (a delete that did not finish) and "Unattached zero-length inode 64" (an allocated inode that no directory names: the leak); a shorter development run also printed "Entry 'g43110' in /d6 (25) has deleted/unused inode 52" (a directory entry whose inode was freed: the dangling name). The other 18 crashes happened to land between bursts of writes and left a clean file system, which is why the probability of damage depends on how busy the disk is when the power goes. With the journal, **replayed at mount, all 40 were clean**. Two cautions about reading this. The model is a clean cut of the write stream, the friendliest crash there is (see the second limit above). And "clean" here means the **structure** is consistent; whether a given file's contents are the ones the application wanted is the subject of section 5.

---
## 3. `fsck`: repair after the fact

The oldest answer is to **accept inconsistency and repair it at the next boot**. `fsck` (file system check; `e2fsck` for ext2, ext3 and ext4) runs **before the file system is mounted**, assumes nobody else is using it, and makes the metadata consistent again. The checks, from OSTEP (fetched):

| Check | What it does |
| --- | --- |
| Superblock | sanity checks (for example a size larger than the blocks allocated); falls back to a backup superblock |
| Free blocks | scans every inode and indirect block and **rebuilds the bitmaps**, trusting the inodes over the bitmaps |
| Inode state | checks each inode's fields (type, for example); clears suspect inodes and updates the inode bitmap |
| Link counts | walks the **whole directory tree from the root**, recounts the links to every inode and fixes the stored counts; an allocated inode that no directory names goes to `lost+found` |
| Duplicates | two inodes claiming one block: clears one inode or copies the block |
| Bad pointers | a pointer outside the valid range is cleared |
| Directories | `.` and `..` come first, every entry names an allocated inode, no directory is linked twice |

Two limits matter. **`fsck` guarantees only that the metadata is consistent.** It cannot tell that an inode points to a block of garbage (the fourth row of the table in section 2.1), because the metadata is consistent. And its **cost is proportional to the file system's metadata**, not to what was being done when the machine crashed: it must read every inode and every directory. OSTEP's analogy is searching the whole house for the keys you dropped somewhere in one room. The program times `e2fsck -fn` (check only) on a 4 GiB ext4 image with an increasing number of files, in 2,000-file directories:

```text
         0 files: e2fsck -fn took   0.02 s
    100000 files: e2fsck -fn took   0.09 s
    400000 files: e2fsck -fn took   0.28 s
   1600000 files: e2fsck -fn took   1.22 s
```

The time grows in proportion to the number of files, about 0.8 seconds per million. **That is the best case**: the image sits in the host's cache, so the figure is CPU time for walking the metadata; on a real disk the same pass must also **read** about 256 bytes of inode per file (400 MiB of inode table for the 1.6 million files, plus the directory blocks), and on a hard disk with seeks that is minutes, and for a multi-terabyte array with hundreds of millions of files, hours. That is the reason `fsck`-only file systems (ext2, FAT) were abandoned for servers: **recovery time is O(size of the file system)**. Journaling makes it O(size of the log).

---

## 4. Journaling

### 4.1 Write-ahead logging

**Journaling** (write-ahead logging, from databases; first applied to file systems in the Cedar system, per OSTEP) writes down **what it is about to do** in a log, the **journal**, before doing it in place. After a crash the system reads the log and either finishes the operation or ignores it, so it never has to search the whole disk. ext3, ext4, XFS, JFS, ReiserFS and NTFS are journaling file systems (OSTEP's list).

For the append of section 2.1 the log holds a **transaction** of five blocks:

```text
journal:   | TxB | I[v2] | B[v2] | Db | TxE |
              ^                          ^
   transaction begin:                 transaction end:
   id + final addresses               same id, a single 512-byte sector
   of the blocks that follow
```

The middle blocks are **physical copies** of the blocks to be written (the alternative, logical logging, stores a compact description of the operation). The protocol has four steps:

1. **Journal write**: write the transaction (`TxB` and the block copies) to the log and wait for it.
2. **Journal commit**: write `TxE` and wait. **The transaction is now committed.**
3. **Checkpoint**: write the blocks to their **final locations** in the file system.
4. **Free**: some time later, mark the transaction's space in the log free (in the **journal superblock**), because the file system now holds everything.

**Why the commit is a separate step.** The disk may reorder writes it was given together. If all five blocks were issued at once and the power failed after `TxB`, `I[v2]`, `B[v2]` and `TxE` reached the disk but before `Db`, recovery would see a transaction that looks complete, replay it, and write garbage over block 5. So steps 1 and 2 are issued **in order, with a wait between them**, and `TxE` must be exactly one 512-byte sector because that is the unit the disk promises not to tear (OSTEP). The alternative is to put a **checksum of the whole transaction** in `TxB` and `TxE`: recovery then recomputes it, and a mismatch means the transaction never finished and is discarded, so the writes can go out together without the wait. ext4 offers this as the `journal_checksum` mount option (the ext4 mount-option page, fetched), and the jbd2 format has a flag for asynchronous commit.

Disk caches complicate this: a drive with a volatile write buffer may report a write as done before it is on the platter, so the file system must issue **write barriers** (cache flushes) between the steps. ext4 enables them by default (`barrier=1`, fetched), and the `fsync` manual page notes that old kernels and some file systems did not know how to flush a disk cache. OSTEP adds that some drives were found to ignore flush requests.

### 4.2 Recovery, batching and the circular log

**Recovery.** At mount the file system reads the journal. A transaction **without a commit record** (or whose checksums do not match) is **ignored**: the update never happened, which is a legal outcome. A **committed** transaction whose checkpoint did not finish is **replayed**: its blocks are written to their final places, in order, up to the latest commit record. This is **redo logging**. Replaying a transaction twice does the same writes twice, which is harmless, so a crash during recovery is also safe, and recovery is rare enough that the repeated writes do not matter. The jbd2 document says the same: replay guarantees "atomicity of journaled writes".

**Batching.** One transaction per file-system call would rewrite the same metadata blocks (the inode table block, the bitmap, the directory) again and again; instead the file system gathers updates into **one global transaction** that is committed periodically. The ext4 default is `commit=5`, every 5 seconds (fetched). This is why a few files created in the same directory cost one transaction, and why `fsync` is expensive: it forces the commit now (section 5).

**The circular log.** The journal is a fixed-size file used as a ring. The **journal superblock** records the oldest and the newest transaction that is not yet checkpointed. A transaction's space is freed once its blocks are in place. A bigger journal allows more uncheckpointed work and a longer recovery; a full journal **blocks new commits** until a checkpoint frees space. The measured defaults are in section 4.5.

### 4.3 Metadata journaling and the ordering rule

**Data journaling** writes every data block twice: once to the journal and once in place. OSTEP's cost estimate for sequential writes is **about half the drive's peak bandwidth**, plus a seek between the journal and the file system. The measurement is in section 4.6.

Most file systems therefore journal **only metadata** and write data directly. The danger is the order. If the inode's pointer to block 5 is committed before the data in block 5 has been written, a crash leaves a valid inode pointing at garbage. The rule (OSTEP's phrase) is **write the pointed-to object before the object that points to it**: the data write must **complete before the metadata commit is issued**. The protocol for metadata journaling, called **ordered mode** in ext3 and ext4:

1. **Data write**: write `Db` to its final location (waiting is optional here).
2. **Journal metadata write**: write `TxB`, `I[v2]`, `B[v2]` and wait. The data write must be **complete** before the next step.
3. **Journal commit**: write `TxE` and wait.
4. **Checkpoint** the metadata, then **free** the transaction.

ext4 has three data modes (the jbd2 document and the mount-option page, fetched):

| Mount option | What goes through the journal | What a crash can leave |
| --- | --- | --- |
| `data=journal` | all data and metadata (no delayed allocation, no `O_DIRECT`; the page says the mode is deprecated and its support will be removed in January 2028) | data and metadata both atomic; the slowest |
| `data=ordered` (the **default**) | metadata only; data is written to the file system **before** the metadata commit | no garbage-pointing inodes; but a data block overwritten in place may be old or new, and "file data blocks are not guaranteed to be in any consistent state after a crash" |
| `data=writeback` | metadata only; **no ordering** between data and metadata | a file may have the new size and **old or garbage contents** (stale blocks of other files can be exposed) |

NTFS and XFS use metadata journaling (OSTEP); ext3 offered all three modes.

### 4.4 The block-reuse problem and revoke records

One tricky case shows that the log has to be **semantically** careful, not only ordered. Directory `foo` uses block 1000, and its update is logged. `foo` is deleted, which frees block 1000, and then file `bar` is created and **reuses block 1000 for its data**. Only `bar`'s inode is journaled (in ordered mode its data is not). If the system crashes and **replays** the old logged write of directory `foo` into block 1000, it overwrites `bar`'s data with stale directory contents. The fix, from ext3, is a **revoke record** written when blocks are freed: during recovery the file system scans first for revoke records and **skips** the replay of any revoked block. The jbd2 format has a revocation block type for exactly this (next section), and adding a block to a transaction cancels earlier revocations of it.

### 4.5 A real journal, read with `debugfs`

The journal of ext4 is a hidden file, **inode 8**, handled by the generic journaling layer **jbd2**. The sizes on this machine:

```text
Block count:              16384
Block size:               4096
Journal inode:            8
Journal backup:           inode blocks
Journal features:         (none)
Total journal size:       4096k
Total journal blocks:     1024
Journal sequence:         0x00000001
Journal start:            0
```

```text
Block count:              262144
Journal inode:            8
Journal backup:           inode blocks
Journal features:         (none)
Total journal size:       32M
Total journal blocks:     8192
Journal sequence:         0x00000001
Journal start:            0
```

A 64 MiB file system has a 4,096 KiB journal of 1,024 blocks (inode 8); the 1 GiB default has **32 MiB, 8,192 blocks**. The jbd2 document gives the maximum (10,240,000 blocks) and does not give a default size, so the defaults are measured. The on-disk format (the same document): every journal block starts with a 12-byte header, **magic `0xC03B3998`**, a block type (1 descriptor, 2 commit, 3 and 4 superblock v1 and v2, 5 revocation) and the transaction's sequence number; a transaction is a **descriptor block** (a list of tags, each naming the final block number of one data block that follows), then the **data blocks**, then a **commit block** holding a checksum and a timestamp. Data whose first four bytes equal the magic is **escaped** (zeroed, with a tag flag) so that recovery cannot mistake it for a header. All jbd2 fields are big-endian, the opposite of ext4's own.

The experiment creates a directory, a 4 KiB file inside it, calls `sync`, freezes the image and dumps the journal:

```text
debugfs 1.47.0 (5-Feb-2023)
Journal starts at block 1, transaction 2
Found expected sequence 2, type 1 (descriptor block) at block 1
Dumping descriptor block, sequence 2, at block 1:
  FS block 25 logged at journal block 2 (flags 0x0)
  FS block 1 logged at journal block 3 (flags 0x2)
  FS block 41 logged at journal block 4 (flags 0x2)
  FS block 9 logged at journal block 5 (flags 0x2)
  FS block 2065 logged at journal block 6 (flags 0x2)
  FS block 10 logged at journal block 7 (flags 0x2)
  FS block 0 logged at journal block 8 (flags 0xa)
Found expected sequence 2, type 2 (commit block) at block 9
No magic number at block 10: end of journal.
```

The journal holds **transaction 2** (the first, 1, was the mount): the descriptor block at journal block 1, **seven logged blocks** at journal blocks 2 to 8, and the commit block at 9; block 10 has no magic number, so the log ends there. The seven file-system blocks are, from the same image:

```text
  Primary superblock at 0, Group descriptors at 1-1
  Block bitmap at 9 (+9), csum 0x277d12b4
  Inode bitmap at 25 (+25), csum 0x2313954e
  Inode table at 41-1064 (+41)
/: Inode: 2, data in block 10
/d: Inode: 12, data in block 2065
/d/file: Inode: 13, data in block 2066
Inode 12 is part of block group 0
 located at block 41, offset 0x0b00
```

- **FS block 0** is the **superblock** (free counts changed) and **block 1** the **group descriptors**.
- **Block 9** is the **block bitmap** and **block 25** the **inode bitmap**.
- **Block 41** is the **inode-table block** that holds inodes 2 (the root), 12 (`d`) and 13 (`file`): all three live in it, which is OS 15's lesson that 16 inodes share a 4 KiB block.
- **Block 10** is the **root directory's data block** (it gained the entry `d`) and **block 2065** is the **new directory `d`'s data block**.
- The file's own data block (2066) is **not** in the journal: this is `data=ordered`.

The journal logs **whole blocks** (physical logging): changing a few bytes of an inode logs the entire 4 KiB inode-table block. Creating one 4 KiB file therefore logs 28 KiB of metadata, plus the data written in place. The same transaction by mode:

```text
  data=writeback: 7 file system blocks logged in 1 transaction(s) with descriptor blocks
  data=ordered: 7 file system blocks logged in 1 transaction(s) with descriptor blocks
  data=journal: 8 file system blocks logged in 1 transaction(s) with descriptor blocks
```

`writeback` and `ordered` log the same seven blocks (they differ in **when the data is written**, not in what is logged); `data=journal` logs **eight**, because the data block is copied into the journal as well. ext4 also has **fast commits** (the jbd2 document, fetched; enabled only at `mkfs` time, ordered mode): a log of small tagged records (add a range to an inode, create a directory entry, link, unlink) that stores the **result** of an operation, not the operation, so that replay is idempotent, and falls back to a full commit when its area fills or the timer expires.

### 4.6 What each mode costs

The program writes 256 MiB with one `fsync`, then creates 1,000 4 KiB files each followed by an `fsync` of the file and of its directory, on loop-mounted ext4 images in each mode, and reads the **loop device's** counters for what reached the device:

```text
ext4, journal mode: none
  256 MiB written and fsynced:    485 MB/s; the device received  256.0 MiB (1.00 times the data) in 515 requests
  1000 files of 4 KiB, each fsynced with its directory:   1432 files/s (698 us each); device: 16.0 KiB and 6.00 requests per file
ext4, journal mode: writeback
  256 MiB written and fsynced:    570 MB/s; the device received  256.0 MiB (1.00 times the data) in 516 requests
  1000 files of 4 KiB, each fsynced with its directory:   1125 files/s (889 us each); device: 36.1 KiB and 5.00 requests per file
ext4, journal mode: ordered
  256 MiB written and fsynced:    552 MB/s; the device received  256.0 MiB (1.00 times the data) in 516 requests
  1000 files of 4 KiB, each fsynced with its directory:   1447 files/s (691 us each); device: 36.1 KiB and 5.00 requests per file
ext4, journal mode: journal
  256 MiB written and fsynced:    215 MB/s; the device received  513.3 MiB (2.01 times the data) in 1113 requests
  1000 files of 4 KiB, each fsynced with its directory:    865 files/s (1156 us each); device: 44.1 KiB and 9.01 requests per file
```

- **Sequential write.** `none`, `writeback` and `ordered` all send **1.00 times the data** to the device in about 515 requests (the journal is touched only for a few blocks), at 485 to 570 MB/s. **`data=journal` sent 2.01 times the data in 1,113 requests and ran at 215 MB/s**, a bit under half: OSTEP's estimate, measured. A different run of the same program gave 223 and 453 to 557 MB/s, so the 20 percent variation is noise and the factor of two is not.
- **Small files with `fsync`.** Without a journal each file cost **16 KiB in 6 requests**: its inode, the bitmaps, the directory block and the data, each written in place. With the journal in ordered mode each file cost **36 KiB in 5 requests**: the metadata blocks go to the journal first (whole blocks, as above), then the commit, **and later again to their final place** when the checkpoint writes them (that second write is not counted here, since the program ends before the checkpoint). So **the journal is not free**: 2.3 times the bytes per file, with 5 requests instead of 6 because one commit covers the file and the directory. `data=journal` cost 44 KiB in 9 requests.
- **What it buys** is section 2.2: 0 of 40 crashes needed repair, against 22 of 40 without it. It also buys a recovery that reads the log (milliseconds) instead of the whole file system (section 3).

### 4.7 Other ways to get consistency

OSTEP (fetched) lists the alternatives. **Soft updates** order every write so that any prefix of the writes is a safe state: no log, but every update must be understood and ordered, which is why it is complex and was implemented in BSD's FFS rather than in Linux. **Copy-on-write** never overwrites a live block and finishes with an **atomic pointer flip** (section 7). **Backpointer-based consistency** puts in each block a pointer back to its owner, so a mismatch reveals a crash, and needs no ordering. **Optimistic crash consistency** issues as many writes as possible and uses checksums to detect the incomplete ones; the paper reports order-of-magnitude gains on `fsync`-heavy workloads but needs a slightly different disk interface. The summary OSTEP gives for the whole chapter: journaling cuts recovery time from O(size of the volume) to O(size of the log), ordered metadata journaling is the common compromise, and strong guarantees for **user data** are still work in progress. Section 5 is what an application does about that.

---
## 5. What an application must do to be durable

The file system's journal protects **the file system**. It does not promise that **your data** is on the disk, nor that a sequence of your writes survives in order. For that the application has to ask.

### 5.1 The calls and what each one promises

From the `fsync` and `open` manual pages (fetched):

| Call | Promise |
| --- | --- |
| `write` | the data is in the kernel's page cache; nothing is promised about the disk |
| `fsync(fd)` | the file's **data and metadata** (size, timestamps, block map) are transferred to the disk, and the **disk's own write cache is flushed**; the call returns after the device reports completion |
| `fdatasync(fd)` | the same, but **metadata that is not needed to read the data back** may be skipped (a changed timestamp is skipped; a changed **file size** is not, because the data could not be read without it) |
| `open` with `O_DSYNC` | every `write` behaves as if followed by `fdatasync` ("synchronized I/O data integrity completion") |
| `open` with `O_SYNC` | every `write` behaves as if followed by `fsync` ("file integrity completion") |
| `sync_file_range`, `msync` | not covered by the fetched pages; `msync` is the `fsync` of a memory mapping (from memory) |

Three rules follow from the manual page, and each is a common bug.

1. **`fsync` of a file does not make its name durable.** The page says that to be sure a newly created file survives, **the directory containing it must also be `fsync`ed** (open the directory, `fsync` that descriptor). Without it the file's data is on disk and the directory entry that reaches it may not be.
2. **`O_RSYNC`** (read-side synchronization) is defined by POSIX but is not implemented as a mode of its own on Linux (the `open` page, as I read it), so it is not a tool.
3. **Errors.** `fsync` can return `EIO` because a write-back failed **long after the `write` that caused it succeeded**. Since Linux 4.13 the error is reported to **every descriptor that may have written the file**, not only the first caller (the manual page, fetched). The page does not say whether a *second* `fsync` reports the same failure again. From memory, this was the substance of the 2018 PostgreSQL "fsyncgate" discussion: on some kernels the failed pages were marked clean and a retried `fsync` returned success although the data was lost, and the lesson that stayed is that **a failed `fsync` is not retryable; treat it as fatal for that data** (the application must rewrite the data from its own copy or stop).

### 5.2 What the calls cost, measured

The program times each operation 2,000 times on one 4 KiB write, with the file on the root ext4 (journal, ordered mode, the disk host-cached), on a loop image **without** a journal, on a loop image **with** a journal, and on `tmpfs`:

```text
ext4 on the root disk (journal, ordered, commit 5 s):
  overwrite 4 KiB, no sync at all                      median      1.3 us   p99      1.9 us   mean      1.4 us
  overwrite 4 KiB + fdatasync                          median    109.7 us   p99    229.7 us   mean    122.1 us
  overwrite 4 KiB + fsync                              median    112.6 us   p99    269.1 us   mean    122.1 us
  append 4 KiB + fdatasync (the size changes)          median    172.6 us   p99    392.2 us   mean    186.0 us
  append 4 KiB + fsync                                 median    125.5 us   p99    238.3 us   mean    138.3 us
  overwrite 4 KiB with O_DSYNC                         median     95.0 us   p99    175.6 us   mean     93.7 us
  create a new file, write 4 KiB, no sync              median     11.8 us   p99     34.9 us   mean     13.0 us
  create, write 4 KiB, fsync the file                  median    211.4 us   p99    374.0 us   mean    212.4 us
  create, write 4 KiB, fsync the file and the directory median    263.2 us   p99    424.2 us   mean    276.8 us
```

- **The call, not the data, is the cost.** The same 4 KiB overwrite takes 1.3 µs without a sync and **109.7 µs with `fdatasync`**, about 85 times more, with a 99th percentile of 230 µs. Nothing about the data changed: the extra time is the **round trip to the device and the flush**. On a real SSD a flush is typically tens to hundreds of microseconds and on a hard disk milliseconds (from memory, not measured here), so the ratio would be larger; the shape would not change.
- **`O_DSYNC` per write is the same cost paid on every call.** Overwrite with `O_DSYNC` took 95 µs: no cheaper than a separate `fdatasync`. Its cost is only visible when writes are small, which is why section 1.1's 1 MiB `O_DSYNC` writes ran at 713 MB/s while 4 KiB ones cost 95 µs each (about 43 MB/s, computed).
- **Creating a durable file is three steps.** Create and write with no sync: 11.8 µs. With `fsync` of the file: **211.4 µs**. With `fsync` of the file **and the directory**: **263.2 µs**. The directory `fsync` is a second commit.
- **An unexplained result.** On both journaled stacks, `append + fdatasync` was **slower** than `append + fsync` (172.6 against 125.5 µs on the root disk; 577.4 against 403.4 on the loop image with a journal). The manual page promises that `fdatasync` does no more work than `fsync`. The result is consistent across two stacks, so it is not noise alone, but I did not find the cause and will not guess one.

The loop images show the same three costs with more layers underneath, and they must not be compared with the root disk's numbers (an extra block layer and a second file system sit under them). Compare **within one stack**:

```text
ext4 without a journal (loop image):
  overwrite 4 KiB, no sync at all                      median      1.0 us   p99      1.4 us   mean      1.0 us
  overwrite 4 KiB + fdatasync                          median    154.2 us   p99    266.1 us   mean    169.7 us
  overwrite 4 KiB + fsync                              median    194.9 us   p99    380.5 us   mean    206.6 us
  append 4 KiB + fdatasync (the size changes)          median    264.3 us   p99    416.8 us   mean    268.2 us
  append 4 KiB + fsync                                 median    255.2 us   p99    453.3 us   mean    266.3 us
  overwrite 4 KiB with O_DSYNC                         median    173.7 us   p99    303.6 us   mean    183.7 us
  create a new file, write 4 KiB, no sync              median      6.8 us   p99     32.3 us   mean      7.5 us
  create, write 4 KiB, fsync the file                  median    290.1 us   p99    559.5 us   mean    312.9 us
  create, write 4 KiB, fsync the file and the directory median    422.1 us   p99    682.4 us   mean    456.0 us
```

```text
ext4 with a journal (loop image):
  overwrite 4 KiB, no sync at all                      median      0.9 us   p99      1.4 us   mean      1.0 us
  overwrite 4 KiB + fdatasync                          median    166.6 us   p99    260.7 us   mean    173.8 us
  overwrite 4 KiB + fsync                              median    174.3 us   p99    623.2 us   mean    197.2 us
  append 4 KiB + fdatasync (the size changes)          median    577.4 us   p99    910.6 us   mean    572.7 us
  append 4 KiB + fsync                                 median    403.4 us   p99    644.9 us   mean    415.5 us
  overwrite 4 KiB with O_DSYNC                         median    181.9 us   p99    339.4 us   mean    188.9 us
  create a new file, write 4 KiB, no sync              median      7.0 us   p99     26.8 us   mean      7.5 us
  create, write 4 KiB, fsync the file                  median    416.8 us   p99   1020.4 us   mean    450.2 us
  create, write 4 KiB, fsync the file and the directory median    504.4 us   p99   1029.3 us   mean    530.2 us
```

The journal adds cost on the loop image: `append + fdatasync` was 264 µs without it and 577 µs with it, and `create + fsync` 290 against 417 µs. That is the commit: the metadata blocks of section 4.5 go to the log, a flush follows, then the commit block and a second flush. `tmpfs`, which has no disk, shows what the calls cost when there is nothing to flush:

```text
tmpfs:
  overwrite 4 KiB, no sync at all                      median      0.8 us   p99      2.8 us   mean      0.9 us
  overwrite 4 KiB + fdatasync                          median      0.5 us   p99      0.8 us   mean      0.6 us
  overwrite 4 KiB + fsync                              median      0.4 us   p99      0.6 us   mean      0.5 us
  append 4 KiB + fdatasync (the size changes)          median      0.6 us   p99      1.8 us   mean      0.7 us
  append 4 KiB + fsync                                 median      0.6 us   p99      1.7 us   mean      0.7 us
  overwrite 4 KiB with O_DSYNC                         median      0.4 us   p99      2.2 us   mean      0.5 us
  create a new file, write 4 KiB, no sync              median      1.8 us   p99      5.9 us   mean      2.3 us
  create, write 4 KiB, fsync the file                  median      2.4 us   p99      6.2 us   mean      2.8 us
  create, write 4 KiB, fsync the file and the directory median      2.5 us   p99      6.8 us   mean      2.9 us
```

All nine medians are between 0.4 and 2.5 µs: **`fsync` on `tmpfs` returns immediately because RAM is the storage**. A program that works on `/tmp` as `tmpfs` and loses data on a disk file system has not tested its durability at all.

**What this means for design.** A database does not `fsync` after each transaction if it can avoid it. It writes a **log record** to an append-only file and calls `fdatasync`, which writes **one sequential block and no directory change** (the file does not grow if it is preallocated, so no size update), and it **batches**: many transactions arrive while one flush is in flight, and the next flush covers all of them (**group commit**). Throughput then scales with the batch size, not with `1 / flush latency`. Section 5.4 lists the other patterns.

### 5.3 Replacing a file safely: write, `fsync`, `rename`

The commonest crash bug: an editor or a configuration tool wants to replace `config` with new contents. Writing in place is unsafe (a crash in the middle leaves half of each version). The accepted pattern:

```text
fd = open("config.tmp", O_CREAT | O_WRONLY | O_EXCL, 0644);
write(fd, new_contents, n);
fsync(fd);                       /* the data is on disk before the name moves */
close(fd);
rename("config.tmp", "config");  /* atomic: readers see old or new */
fd = open(".", O_RDONLY | O_DIRECTORY);
fsync(fd);                       /* the rename itself is durable */
close(fd);
```

`rename` is atomic with respect to other processes, and it also survives a crash as old or new **if the data was written first**. Without the first `fsync`, the order the file system chooses may make the rename durable **before the data**. The harness of section 2.2 crashed 20 times in each configuration, with a loop that repeatedly renamed a new file over `target` (`target` always holds either old or new content, by the rule), and classified `target` afterwards:

```text
  ordered, noauto_da_alloc, no fsync             old  3  new  0  zero-length 17  missing  0  zero-filled  0  other  0
  ordered, auto_da_alloc (default), no fsync     old  2  new  8  zero-length  0  missing  0  zero-filled 10  other  0
  ordered, noauto_da_alloc, fsync before rename  old  0  new 20  zero-length  0  missing  0  zero-filled  0  other  0
  data=journal, noauto_da_alloc, no fsync        old  0  new 20  zero-length  0  missing  0  zero-filled  0  other  0
  no journal, no fsync                           old 16  new  4  zero-length  0  missing  0  zero-filled  0  other  0
```

Reading it row by row, with the counts (out of 20):

- **Ordered journal, `noauto_da_alloc`, no `fsync`: `target` was zero-length in 17 of 20 crashes.** The rename was committed while the data was not, and the file the name points to has size 0 (the likely cause is delayed allocation, which holds the data back until write-back; I did not trace it): the classic "my config file is empty after a crash". Three crashes left the old version, which I read as the rename not yet being in a committed transaction.
- **Ordered journal, `auto_da_alloc` (the default), no `fsync`: zero-length 0, but zero-filled 10.** ext4's `auto_da_alloc` heuristic (the mount-option page, fetched) forces the data blocks to disk when a file is renamed over an existing one, to fix exactly the zero-length case. It worked as far as the **size** is concerned: the file had size 4,096 (one 4 KiB block) in all ten cases, but **the contents were all zero bytes**. The size was committed and the data blocks were not yet (or were the wrong ones). I did not find the cause: my hypothesis is that the allocated extent was still marked unwritten when the commit happened. **Treat it as proof that a file of the right size says nothing about its contents.**
- **`fsync` before the rename: new content, 20 of 20.** The pattern works.
- **`data=journal` without `fsync`: new content 20 of 20**, because data and rename are in the same transaction and are atomic together. This is the guarantee ordered mode does not give.
- **No journal, no `fsync`: old 16, new 4, never zero-length or zero-filled.** In these runs the writes reached the disk in an order that preserved the data. That is luck of timing, not a guarantee (the file system was not checked with `fsck` here, so its structure is unknown), and it is a different failure mode from the journaled ones.

The crashes are cuts of the write stream (section 2.2's limit), so a real power loss, where the disk reorders and tears, would not be kinder. The **numbers** will differ on your machine; the **ordering rule** will not.

### 5.4 Other application patterns

- **Append-only log with a checksum per record.** After a crash the tail may be half-written. On restart, read records until one fails its length or checksum, and truncate there. A CRC32C (section 8) costs a few hundred nanoseconds per 4 KiB with the hardware instruction.
- **Preallocate the log** (`fallocate`) so that an append does not change the file's block map, which keeps `fdatasync` from needing a metadata commit (the size change is the other reason; hence keep the file at full size and write a sequence number in each record).
- **Write twice, then compare.** Keep two copies of a small structure with version numbers; update the older one; recovery takes the newer valid one (this is the two checkpoint regions of section 7).
- **`O_DIRECT` plus `fdatasync`** for a database's own buffer pool (section 6).
- **Do not trust a `close`.** `close` does not flush; on some network file systems it may return an error from a deferred write (from memory), so a durable save checks the return of `fsync` **and** `close`.

---

## 6. Buffered, direct and synchronous I/O

The three ways to move 512 MiB, from the program of section 1.1:

```text
512 MiB written in 1 MiB writes
  buffered, write() calls only:          5159 MB/s (0.10 s); Dirty now 512 MiB
  then fsync() took:                     0.43 s; with it    961 MB/s all in
  O_DIRECT:                              1757 MB/s (0.29 s); Dirty now 0 MiB
  O_DSYNC (each write is durable):        713 MB/s (0.72 s)
```

| Mode | Where the data goes | Page cache | When `write` returns |
| --- | --- | --- | --- |
| buffered (default) | copied to dirty pages, written back later | yes (`Dirty` grew by 512 MiB) | at once, at memory speed (5,159 MB/s) |
| buffered + `fsync` at the end | same, then one flush | yes | after the flush; 961 MB/s all in |
| `O_DIRECT` | straight from your buffer to the device | **no** (`Dirty` 0 MiB) | when the device has taken it (1,757 MB/s) |
| `O_DSYNC` / `O_SYNC` | cache **and** an immediate flush of each write | yes | after each write is durable (713 MB/s) |

**What `O_DIRECT` demands and what it costs.** The `open` page (fetched) says direct I/O needs the **buffer address, the transfer length and the file offset** to be aligned to the device's logical block size (512 bytes or 4 KiB; the program uses 4 KiB-aligned buffers from `posix_memalign`). It does **not** by itself guarantee durability: it bypasses the cache, but the device's own write cache may still hold the data, and file-system metadata (a new file's size) may need a separate `fdatasync` (from memory). It must not be mixed on the same file with buffered writes or with `mmap`, because the cache and the device would disagree about what the file holds (the manual page's warning, fetched). The cost is that **every read must go to the device**; the cache's benefit is lost, which section 1.2 measured:

- Cold buffered read: 3,378 MB/s, and the file stayed resident (1,024 MiB); a second pass ran at 6,770 MB/s from memory.
- `O_DIRECT` read: 2,563 MB/s warm and 2,603 cold, with **0 MiB resident**; it is the same speed twice, because there is nothing to warm.

So `O_DIRECT` loses on a file that is read again and wins on a file that is bigger than memory and is read once. A database uses it because **it has its own buffer pool**, which knows what to evict; the kernel's page cache would hold a second copy of the same pages (double caching) and decide with less information. The kernel's cache is the right default for everything else: it is shared among processes, it does readahead, and it needs no alignment.

**Choosing a mode.**

- A program that writes a lot and can tolerate losing the last few seconds on a crash: buffered, **no sync calls** (the flusher's 30-second expiry and the 5-second journal commit set the window).
- A program that must not lose a **committed** result: buffered writes plus an `fsync` or `fdatasync` at the commit point, **batched across clients**.
- A program that manages its own cache and wants predictable latency: `O_DIRECT` plus explicit `fdatasync` for durability.
- Do not use `O_SYNC` for small writes on a file system with a journal unless you need exactly that: it makes each call pay the full commit (section 5.2).

---
## 7. Never overwrite: log-structured and copy-on-write file systems

### 7.1 The idea (Rosenblum and Ousterhout's LFS, via OSTEP)

A journal writes everything twice. A **log-structured file system (LFS)** makes the log the **only** copy: all updates, data and metadata, are buffered in memory in a **segment** (about a megabyte or more), and the segment is written to a **free** place on the disk in one long sequential write. Nothing is overwritten in place. The motivation (OSTEP, fetched) was that memory was growing and absorbing reads, so writes dominate the disk, and that sequential bandwidth is far higher than random bandwidth.

**How big must the segment be?** Writing a segment costs one positioning time plus the transfer. To reach a fraction `F` of the disk's peak rate, the segment must satisfy `D = F / (1 - F) * R_peak * T_position`. The program evaluates it for a disk with 10 ms positioning and 100 MB/s peak:

```text
required segment size to reach a fraction F of the disk's peak write rate (T_position 10 ms, R_peak 100 MB/s):
  F = 0.50: D = F / (1 - F) * R_peak * T_position = 1 MB
  F = 0.90: D = F / (1 - F) * R_peak * T_position = 9 MB
  F = 0.95: D = F / (1 - F) * R_peak * T_position = 19 MB
  F = 0.99: D = F / (1 - F) * R_peak * T_position = 99 MB
```

Half the peak needs 1 MB, 90 percent needs 9 MB, 99 percent needs 99 MB. The price of the last few percent grows quickly, which is why real LFS designs use segments of a few megabytes.

### 7.2 Finding the data when it keeps moving

If an inode is rewritten at a new address every time, how does anyone find it? An FFS-style inode table is at a fixed place. LFS adds one more level:

- The **inode map (imap)** maps an inode number to the disk address of that inode's **latest version**. It is itself written into the log, in pieces; each entry is about 4 bytes (OSTEP).
- A **checkpoint region (CR)** at a **fixed** location holds the addresses of the imap pieces. It is rewritten only every 30 seconds or so, and **two copies** are kept, each with a timestamp at its start and at its end: if the two timestamps differ the CR was torn, and recovery uses the other copy.
- The **recursive update problem** that plagues copy-on-write trees (changing a leaf changes its parent, which changes its parent, up to the root) is solved by the imap: a new inode changes the imap entry, not the directory that names the file, because the directory holds the **inode number**, which is stable.

**Reading** a file is therefore: read the CR (cached), find the imap piece, read the inode, then read the data blocks as in any Unix file system.

**Crash recovery** is **roll-forward**: start from the last good checkpoint, then read the log forward from where it ended and apply any later complete segments (the segment summaries say what each block is), so that work done after the last checkpoint is not lost.

### 7.3 The price: the cleaner and write amplification

Overwriting a block leaves the **old copy** in an old segment as **garbage**. To get free segments again, the **cleaner** reads old segments, determines which blocks are still **live** (the segment summary block names each block's owner, inode number and offset; a block is live if the file's inode still points to it, and a **version number** in the imap lets the cleaner discard a deleted file's blocks without reading its inode), and rewrites the live blocks into a new segment. The question is how much extra writing this causes: the **write amplification** (bytes written to the disk per byte the application wrote).

If a segment chosen for cleaning is a fraction `u` live, cleaning it frees `1 - u` of a segment of space and costs a write of `u`. To make room for one segment of new data you must clean `1 / (1 - u)` segments' worth, so **amplification is about `1 / (1 - u)`**: 1.25 at `u = 0.2`, 2 at `0.5`, 5 at `0.8`, 10 at `0.9`. The simulation builds a log of 512 segments of 64 blocks, overwrites the live data 40 times with three policies, and measures amplification and the live fraction of the segments the cleaner chose:

```c
static void append(int lb, int by_cleaner)
{
    if (head_fill == SPB) open_segment();
    int old = where[lb];
    if (old >= 0) { owner[old] = -1; live[old / SPB]--; }
    int slot = head * SPB + head_fill++;
    where[lb] = slot; owner[slot] = lb; live[head]++;
    clock_t_ += 1;
    stamp[head] = clock_t_;
    if (by_cleaner) clean_w++; else user_w++;
}

static int pick_victim(void)
{
    int best = -1; double bs = -1;
    for (int s = 0; s < SEGS; s++) {
        if (is_free[s] || s == head) continue;
        double u = (double)live[s] / SPB, score;
        if (policy == 0) score = 1.0 - u;
        else score = (1.0 - u) * (clock_t_ - stamp[s] + 1) / (1.0 + u);
        if (score > bs) { bs = score; best = s; }
    }
    return best;
}

static void clean(void)
{
    while (nfree < 3) {
        int v = pick_victim();
        cleaned_segs++; cleaned_util += (double)live[v] / SPB;
        for (int i = 0; i < SPB; i++) {
            int lb = owner[v * SPB + i];
            if (lb >= 0) { owner[v * SPB + i] = -1; where[lb] = -1; live[v]--; append(lb, 1); }
        }
        is_free[v] = 1; nfree++;
    }
}
```

```text
log-structured store: 512 segments of 64 blocks; writes counted over 40 overwrites of the live set
  uniform overwrites, greedy         utilisation 0.50: write amplification 1.24, cleaned segments were 20% live on average
  90/10 hot and cold, greedy         utilisation 0.50: write amplification 1.54, cleaned segments were 35% live on average
  90/10 hot and cold, cost-benefit   utilisation 0.50: write amplification 1.38, cleaned segments were 28% live on average
  uniform overwrites, greedy         utilisation 0.70: write amplification 1.85, cleaned segments were 46% live on average
  90/10 hot and cold, greedy         utilisation 0.70: write amplification 2.28, cleaned segments were 56% live on average
  90/10 hot and cold, cost-benefit   utilisation 0.70: write amplification 1.62, cleaned segments were 38% live on average
  uniform overwrites, greedy         utilisation 0.80: write amplification 2.65, cleaned segments were 62% live on average
  90/10 hot and cold, greedy         utilisation 0.80: write amplification 3.06, cleaned segments were 67% live on average
  90/10 hot and cold, cost-benefit   utilisation 0.80: write amplification 1.99, cleaned segments were 50% live on average
  uniform overwrites, greedy         utilisation 0.90: write amplification 5.02, cleaned segments were 80% live on average
  90/10 hot and cold, greedy         utilisation 0.90: write amplification 5.57, cleaned segments were 82% live on average
  90/10 hot and cold, cost-benefit   utilisation 0.90: write amplification 3.33, cleaned segments were 70% live on average
```

- **The formula holds.** Uniform overwrites at 80 percent utilisation: the cleaned segments were 62 percent live and the amplification was 2.65 (the formula gives 2.6); at 90 percent: 80 percent live and 5.02 (the formula gives 5.0).
- **Utilisation is the main knob.** From 50 to 90 percent full the amplification of uniform writes goes from 1.24 to 5.02: **running such a file system (or an SSD, which is a log-structured device) nearly full makes it slow and wears it out faster**. This is why SSDs reserve **over-provisioned** space (F2FS's default is 5 percent of the volume, the F2FS document, fetched).
- **Hot and cold data change the answer.** When 90 percent of the writes go to 10 percent of the data, a **greedy** cleaner (always take the emptiest segment) does worse than on uniform data (5.57 against 5.02 at 90 percent), because hot data are mixed with cold data in the same segments and cold data are copied again and again. The **cost-benefit** policy from the LFS paper picks segments by the ratio of benefit (free space gained times the **age** of the data) to cost, so it **waits** for a hot segment to empty and cleans cold ones first: 3.33 at 90 percent, **40 percent less** than greedy's 5.57 on the same workload. The cost of a few more lines of code is a large cut in write traffic.
- **Hot/cold separation** is the real fix, and what F2FS and flash translation layers do: write hot and cold data to **different logs** so that a segment tends to die all at once.

These are **simulation numbers** from my own model (a block store with perfect knowledge of liveness), not measurements of LFS, F2FS or an SSD. The shape (the `1 / (1 - u)` law, the effect of utilisation and of hot/cold separation) is what carries over.

### 7.4 Descendants

LFS as a product did not last, but its two ideas did (OSTEP and the fetched F2FS document):

- **Copy-on-write (COW) file systems.** WAFL (NetApp), ZFS and btrfs never overwrite a live block; an update writes new blocks and finishes by flipping **one root pointer** atomically, so the file system is consistent at every instant and there is no journal and no `fsck` (from memory for ZFS and btrfs; OSTEP names them as heirs). Snapshots are free, because the old root still describes a complete old tree. The costs are the same as the cleaner's (fragmentation, and free-space management) and the recursive update (fixed with large nodes and batching).
- **Flash.** An SSD's firmware keeps a **flash translation layer** that is a log-structured store (pages cannot be overwritten, only erased in large blocks), so write amplification inside the drive is the same arithmetic as above.
- **F2FS** (Flash-Friendly File System, in Linux; the document, fetched) is "based on the log-structured file system approach" and tuned for flash. It cuts the recursive update with a **node address table (NAT)** (a node's address is looked up in the table, so moving a node changes one table entry and not its parent chain); it keeps up to **six logs** at once (`active_logs=2, 4 or 6`, default 6) so that hot, warm and cold data and nodes land in different segments; it offers **greedy** and **cost-benefit** garbage-collection policies (the document names both; I did not check which is used when); it writes **checkpoints**; and it stores very small files **inside the inode** (about 3.4 KB). Its `fsync_mode=posix` default is the usual `fsync`; `strict` adds stronger ordering and `nobarrier` skips the flush.

---

## 8. Data integrity: when the disk lies

Crash consistency assumes that what the disk returns is what was written. It is not always so. OSTEP's integrity chapter (fetched) names two failure types.

- **Fail-stop**: the disk works or it is dead and says so. RAID was designed for this.
- **Fail-partial**: the disk mostly works but individual blocks fail. Two kinds: a **latent sector error (LSE)**, where a block can no longer be read and the drive's own error code reports it (the sector is damaged, or the head touched the surface); and **block corruption**, where the drive returns **wrong data with no error** (a firmware bug, a bad transfer over the bus).

OSTEP's data on how often (a study of about 1.5 million drives over about three years): for **cheap** drives 9.40 percent had at least one latent sector error and 0.50 percent had a corruption; for **costly** drives, 1.40 and 0.05 percent. Two further failure types, **misdirected writes** (the data goes to the wrong place) and **lost writes** (the disk says the write finished and it did not), are rarer and nastier because there is no read error to see.

### 8.1 Checksums

Store a small **checksum** with each block and verify it on every read. Choices (OSTEP):

| Function | Idea | Weakness |
| --- | --- | --- |
| XOR | XOR all words of the block | any change that keeps the column parity (two equal flips in the same bit position) is missed |
| Addition | sum all words | a changed pair that sums the same; swapped words |
| Fletcher | two running sums modulo 255 | rare misses; the second sum makes it sensitive to order |
| CRC | the block is a polynomial, the remainder after division is the checksum | burst errors up to its length are always caught; random misses at about 2^-32 for 32 bits |

The program tests each on 100,000 random 4 KiB blocks, with six kinds of corruption, and counts the corruptions that go **undetected**:

```text
undetected corruptions out of 100000, 4 KiB blocks of random data (a 32-bit checksum misses a random change about 1 in 4.3 billion)
                                        XOR (32-bit)   addition (32-bit)  Fletcher (mod 255)        CRC32C table     CRC32C hardware
one bit flipped                                    0                   0                   0                   0                   0
same bit flipped in two words                 100000               51566                 120                   0                   0
two words swapped                             100000              100000                2211                   0                   0
a 32-bit burst inverted                            0                   0                   0                   0                   0
8 random bytes                                     0                   0                   4                   0                   0
block shifted by one byte                          0                   0                   1                   0                   0
```

- XOR and addition miss **every** swap of two words, and XOR misses every same-bit flip in two words. Addition missed 51,566 of 100,000 of the same-bit flips (carries sometimes change the sum).
- Fletcher missed 120 of the same-bit flips, 2,211 of the swaps, 4 of the 8-random-byte changes and 1 of the shifts, which is **small but not zero**.
- **CRC32C missed nothing in any of the six** (a random miss has probability about 1 in 4.3 billion, so 100,000 trials would show zero with very high probability, and zero is the expected result, not a proof).
- A single flipped bit and a 32-bit burst were caught by all five, so the weak functions are adequate for those two failures; the table shows where they stop being adequate.

The speed matters as well as the quality:

```text
speed on one 4 KiB block that stays in the cache (the data is touched between calls so nothing is hoisted):
  XOR (32-bit)            55.73 GB/s        73 ns per 4 KiB block
  addition (32-bit)       55.71 GB/s        74 ns per 4 KiB block
  Fletcher (mod 255)       0.42 GB/s      9663 ns per 4 KiB block
  CRC32C table             0.47 GB/s      8629 ns per 4 KiB block
  CRC32C hardware         10.26 GB/s       399 ns per 4 KiB block
```

A table-driven CRC32C runs at about **0.47 GB/s**, about 8.6 µs per 4 KiB block: too slow for a fast SSD. The **hardware** instruction (`crc32` in SSE4.2 on x86, present on this machine) runs at about **10 GB/s, 0.4 µs per block**, about 22 times faster. This is why modern file systems pick CRC32C: the **ext4 journal checksum** (CRC32C among the types the jbd2 document lists, fetched), and, from memory, ext4's `metadata_csum` and btrfs. The space cost is small: an 8-byte checksum per 4 KiB block is 0.195 percent (a 4-byte one 0.098 percent).

### 8.2 Where to put the checksum

- **Next to the data**, in a bigger sector (a "520-byte sector" format: 512 bytes of data and 8 of checksum): the checksum is read with the block, and a write that goes to the wrong place or is lost leaves a checksum that still matches the wrong block, so **it does not detect misdirected or lost writes**.
- **Packed in a separate block** that holds the checksums of several data blocks: a write needs an extra read (to get the checksum block) and two writes (data and checksum block), unless the checksums are written in the same sequential segment. In an LFS the cost disappears.
- **In the parent** (ZFS's design, from memory): each pointer carries the checksum of the block it points to, so the checksum is **not stored with** the block it protects. A **lost write** is detected, because the parent was updated and the child's content is old, so the checksum does not match.
- To catch a **misdirected write**, add a **physical identifier** (the block's own disk and block number) to what is checksummed, so a good block at the wrong address does not verify.

### 8.3 Scrubbing and what to do on a mismatch

Checksums verify only the blocks that are **read**. A block that is never read again can rot until the day its other copy fails too. **Scrubbing** reads every block periodically and verifies it, so that a bad copy is found while a good copy (a mirror, a parity group) still exists and can be used to **repair**. A checksum alone detects; redundancy corrects. A checksum without a second copy turns silent corruption into an error return (`EIO`), which is better, because the program knows.

---

## 9. What real file systems choose

| File system | Crash-consistency mechanism | Data protection | Notes |
| --- | --- | --- | --- |
| ext2, FAT | none; `fsck` at boot | none | recovery time grows with the volume (section 3) |
| ext3 / ext4 | **physical metadata journal** (`jbd2`), `data=ordered` default; checksums on the journal and (option) metadata; fast commit option | file data not checksummed | `data=journal` and `writeback` available; `auto_da_alloc` heuristics for rename (section 5.3) |
| XFS | **logical** metadata journal (from memory: it logs operations in a compact form instead of whole blocks) | metadata checksums in recent versions (from memory) | scalable allocation groups; not tested here |
| btrfs, ZFS | **copy-on-write** with an atomic root update | **checksums of data and metadata**, with scrub; ZFS keeps the checksum in the parent | snapshots; not tested here |
| F2FS | log-structured, **checkpoints** and roll-forward | not covered by what I fetched | built for flash; the cleaner is the cost |

What I tested: ext4 in its modes (sections 2.2, 4.6, 5.3), and the log-structured model (section 7.3). The rest of the table is from OSTEP's survey, the fetched ext4 and F2FS documents, and memory where marked. The point for an interview is the **pattern**: **ordering plus a log, or never overwrite**, with checksums to detect what the hardware does silently.

---
## What it costs

| Operation | Cost (measured here, unless marked) | Why |
| --- | --- | --- |
| buffered `write` of 1 MiB blocks | about 5,159 MB/s | a copy into dirty pages |
| the `fsync` after 512 MiB of that | 0.43 s; 961 MB/s all in | the data must reach the device |
| `O_DIRECT` write | about 1,757 MB/s, no dirty pages | device speed, no cache copy |
| 4 KiB overwrite, no sync / `fdatasync` | 1.3 µs / 109.7 µs | one device round trip and flush |
| create a 4 KiB file, `fsync` / plus directory | 211.4 µs / 263.2 µs | a journal commit, then a second one |
| dirty data left alone | written out after about 30 s (64 MiB below 8 MiB at 30.7 s) | `dirty_expire_centisecs` 3000 |
| `e2fsck -fn` | about 0.8 s per million files (CPU only, cached image) | reads every inode and directory |
| `data=journal` sequential write | 2.01 times the data, 215 MB/s against 552 for `ordered` | everything goes through the log twice |
| journal commit of one new directory plus file | 7 whole 4 KiB blocks plus descriptor and commit | physical logging |
| 1,000 small files with `fsync` | 16 KiB per file without a journal, 36 KiB with `ordered` | metadata goes to the log |
| `rename` over old file, no `fsync` | zero-length in 17 of 20 crashes (`noauto_da_alloc`) | rename durable before data |
| log-structured cleaning at 90 percent full | amplification 5.0 uniform, 5.6 hot/cold greedy, 3.3 cost-benefit (model) | `1 / (1 - u)` |
| CRC32C of 4 KiB, table / hardware | 8.6 µs / 0.4 µs | about 22 times apart |
| 8-byte checksum per 4 KiB | 0.195 percent of the space | |

---

## Common mistakes

1. **Believing `write` returned means the data is safe.** It means the data is in the page cache. 512 MiB went in at 5,159 MB/s with 512 MiB still dirty.
2. **Thinking `fsync` on a new file makes it findable.** The directory entry is a separate piece of metadata: `fsync` the directory too (211 against 263 µs).
3. **Replacing a file in place, or renaming without `fsync` first.** The first leaves half of each version; the second left a zero-length file in 17 of 20 crashes and, with the default `auto_da_alloc`, a file of the right size and all zeros in 10 of 20.
4. **Trusting a file's size.** The zero-filled files had the new size. Only a checksum of the contents, or a successful `fsync` before the rename, says the contents are there.
5. **Retrying a failed `fsync`.** Write-back errors can be reported once; treat `EIO` as loss of that data and rewrite it from your own copy.
6. **Testing durability on `tmpfs`.** Every `fsync` there takes under 3 µs because there is nothing to flush.
7. **Saying journaling protects your data.** In `ordered` mode it protects the **structure**; data written in place may be old or new. Only `data=journal` makes data and metadata atomic, at twice the writes.
8. **Choosing `data=writeback` for speed without understanding it.** A file can have the new size and old or garbage contents (stale blocks of other files can be exposed).
9. **Believing a checksum repairs.** It detects; repair needs a second copy. And a checksum stored with its block cannot detect a lost or misdirected write.
10. **Choosing XOR or an additive checksum for a block.** Both missed every swap of two words in 100,000 trials. Use CRC32C, which the hardware does at 10 GB/s.
11. **Running a log-structured store or an SSD nearly full.** Amplification followed `1 / (1 - u)`: 1.2 at half full, 5 at 90 percent.
12. **Mixing `O_DIRECT` with buffered or `mmap` access to the same file**, or passing an unaligned buffer.

---

## Questions out loud

Answer each in two minutes, with a number or a diagram, before looking back (the section in brackets).

1. What does `write` do, and when does the data reach the disk? What sets the delay? [1.1, 1.3]
2. What are the dirty background and hard limits, and what happens to a writer that crosses each? [1.3]
3. Append one block to a file: what are the three writes, and what does each of the six partial outcomes leave? [2.1]
4. What does `fsck` check, and why does its time grow with the size of the volume and not with the damage? [3]
5. Walk through a journal transaction: what is written, in what order, where must the disk be asked to wait, and why? [4.1]
6. How does recovery treat a transaction with and without a commit record? Why is replaying twice harmless? [4.2]
7. What do `journal`, `ordered` and `writeback` modes log, and what can each leave after a crash? Which does ext4 default to? [4.3, 4.5]
8. What is a revoke record and what bug does it prevent? [4.4]
9. Why does a journal cost extra writes, and what did `data=journal` cost on this machine? [4.6]
10. What does `fsync` promise that `write` does not? `fdatasync`? `O_DSYNC`? What does the directory `fsync` add? [5.1]
11. Write the safe sequence for replacing a config file. What does each step prevent? [5.3]
12. Why is `O_DIRECT` right for a database and wrong for most programs? What does it require? [6]
13. How does a log-structured file system find an inode that keeps moving? What is a checkpoint region? [7.2]
14. What is write amplification in a cleaner, and how does the live fraction of the chosen segment set it? Why does hot/cold separation help? [7.3]
15. What failures does a disk have besides dying (fail-partial, latent sector errors, corruption, misdirected and lost writes)? Which does a checksum next to the data miss? [8]
16. Why do file systems choose CRC32C over XOR or addition? [8.1]
17. Compare ext4, XFS, btrfs and F2FS in one sentence each on how they stay consistent. [9]

---

## Problems

**Where to solve them.** On your own machine: make an ext4 image (`truncate -s 64M f.img; mkfs.ext4 f.img`), mount it with `mount -o loop`, run the operation, `sync`, and read the journal with `debugfs -R "logdump -a" f.img` and the layout with `dumpe2fs`; `e2fsck -fn f.img` checks without changing anything. To crash a mounted image, copy its device state with `blockdev --setro` on the loop device at a chosen moment, as in section 2.2. The OSTEP chapters on crash consistency and journaling and on log-structured file systems have homework simulators in the course's repository (`fsck`, `journal` and `lfs`; names from memory). For coding practice, LeetCode 146 (LRU Cache) is the cache-eviction part of this topic and 1166 (Design File System) a path-store drill (numbers from memory).

**How to approach them.** For crash problems, write the writes as a list, then enumerate which subsets reached the disk and classify each as metadata-consistent or not, and as data-correct or not. For counting problems, list the **distinct blocks** touched, because the journal logs whole blocks once per transaction. For amplification, start from `1 / (1 - u)`. For throughput, find the **time per flush** first and divide by the batch.

### Problem A (E, entry level): the six partial outcomes

Appending one block to a file writes the inode `I`, the data bitmap `B` and the data block `Db`. Without a journal, any subset can reach the disk before a crash. For each of the six subsets of one or two writes, say whether the metadata is **consistent** and whether the file is **correct**.

### Problem B (E, entry level): cleaning and segments

A log-structured store keeps cleaned segments 75 percent live on average. (a) What is the write amplification? (b) The application writes 50 MB/s: what rate does the device see? (c) What live fraction keeps the amplification at 2 or below? (d) A disk has 8 ms positioning time and 250 MB/s peak. How big must a segment be to reach 95 percent of the peak, and 50 percent?

### Problem C (M, medium): one journal transaction for a thousand files

An application creates 1,000 empty-looking 4 KiB files (each with one data block) in one directory within one 5-second commit interval. The file system has 4 KiB blocks, 256-byte inodes, 20-byte directory entries, a journal of 1,024 blocks, and the inodes and data are allocated contiguously in one group. (a) How many distinct file-system blocks does the transaction log, in `ordered` mode? (b) How many journal blocks is that with the descriptor and commit blocks (assume one descriptor suffices)? (c) How many such transactions fit before the journal is full? (d) Compare with 1,000 separate transactions of 7 blocks each.

### Problem D (M, medium): what each missing step costs

The safe replace is: write `config.tmp`, `fsync` it, `rename` over `config`, `fsync` the directory. For each of the four variations, state what a crash can leave: (a) no `fsync` of the file; (b) no `fsync` of the directory; (c) write `config` in place with `fsync`; (d) `fsync` of the directory but not of the file.

### Problem E (H, hard): a durable log with group commit

A service appends 100-byte records to a log and must not acknowledge a record before it is durable. Take the measured cost of one `write` plus `fdatasync` as 110 µs, of which the flush is almost all, and 1 µs for each extra record added to a batch. (a) With one record per flush, what is the maximum rate? (b) With 64 clients writing at once and one flush covering all records that arrived during the previous flush, what rate and what worst-case latency? (c) Design the record format so that recovery can find the end of the log after a crash, and say what the space overhead is. (d) Why preallocate the file?

---

## Answers

**A.** Writes: `I`, `B`, `Db`.

| Reached the disk | Metadata | File |
| --- | --- | --- |
| `Db` only | consistent | correct as before: the append is lost, nothing is damaged |
| `I` only | **inconsistent** (the inode claims a block the bitmap says is free) | the inode points to **garbage** |
| `B` only | **inconsistent** (a block marked used, owned by no one: a leak) | correct as before |
| `I` and `B` | consistent | **garbage**: the pointer is valid and the data is old (the worst silent case) |
| `I` and `Db` | **inconsistent** (the bitmap still says free, so the block may be given to another file) | the new data is reachable now |
| `B` and `Db` | **inconsistent** (a leaked block) | correct as before; the data is orphaned |

Four of six are metadata-inconsistent (`fsck` fixes them). `I` and `B` is consistent and wrong: `fsck` cannot see it. `Db` alone is harmless. That is why the journal needs the data before the metadata commit.

**B.** (a) `1 / (1 - 0.75) =` **4**. (b) `50 * 4 =` **200 MB/s**. (c) `1 / (1 - u) <= 2` gives `u <= 0.5`: cleaned segments must be at most **50 percent live**. (d) `D = F / (1 - F) * R_peak * T_position`. `R_peak * T_position = 250 MB/s * 0.008 s = 2 MB`. For `F = 0.95`: `19 * 2 MB =` **38 MB**. For `F = 0.5`: `1 * 2 MB =` **2 MB**.

**C.** (a) Inode table: 1,000 inodes at 16 per block `= 62.5`, so **63** blocks. The block bitmap: 1,000 contiguous data blocks fit in one bitmap block (it covers 32,768 blocks), **1**. The inode bitmap: **1**. Directory blocks: 1,000 entries at 4,096 / 20 `=` 204 per block `= 4.9`, so **5**. Group descriptor **1** and superblock **1** (the free counts change). Total `63 + 1 + 1 + 5 + 1 + 1 =` **72 blocks** (the data blocks are written in place, not logged, in `ordered`). (b) `72 + 1` descriptor `+ 1` commit `=` **74 journal blocks**, 296 KiB. (c) `1,024 / 74 =` 13.8, so **13** transactions fit before the journal must checkpoint (section 4.2). (d) 1,000 transactions of 7 blocks (plus 2) would be 9,000 journal blocks, **122 times** more; batching is the point. The 7-block figure is the measurement of section 4.5 for the first file; with 1,000 files in one transaction the same inode block, bitmap and directory block are shared.

**D.** (a) **Zero-length or old file** (the rename can be committed before the data; 17 of 20 crashes zero-length in the measurement with `noauto_da_alloc`, and a zero-filled file of the right size in 10 of 20 with the default `auto_da_alloc`). (b) The file is whole, but after the crash **the old name may be back** (the rename is not durable: the directory still shows `config` with the old contents, or the new file only under `config.tmp`). Either is a valid whole state, but an application that told its caller "saved" has lied. (c) A crash during the write leaves **a mix of old and new** or a truncated file; `fsync` only helps after the damage is done. (d) The directory is durable, but the new file's **data is not**: the same zero-length or zero-filled file as (a). So the order of the two `fsync`s is not arbitrary: **the file first**, then the rename, then the directory.

**E.** (a) `1 / 110 µs =` **9,091 records/s**. (b) A batch of 64 takes `110 + 64 * 1 = 174 µs`, so `64 / 174 µs =` **367,816 records/s**, about **40 times** the single rate. Worst latency: a record arrives just after a flush starts, waits up to 110 µs for it, then its own batch takes 174 µs: about **284 µs** (queueing ignored). (c) A record is `length (4 bytes) + CRC32C (4 bytes) + payload`. Recovery reads records from the start and stops at the first one whose length runs past the file or whose CRC does not match, then **truncates there**; everything before it was fully durable when acknowledged. The overhead is 8 bytes per 108, **7.4 percent**; a 10 GB/s CRC costs about 10 ns for 100 bytes (from the measured hardware rate). (d) A preallocated file does not change size on an append, so `fdatasync` does not have to commit a size change in a journal transaction, and the blocks already exist; the cost per flush is then a data write and one flush (section 5.4). All figures in (a) and (b) are model arithmetic from the stated costs, not a measurement of a real log.

---

## Cheat sheet

- **`write`** goes to the page cache and returns (5,159 MB/s measured); the kernel writes it back by the flusher threads: after `dirty_expire_centisecs` 3000 (30 s), when `Dirty` passes `dirty_background_ratio` 10 percent (flusher starts), and `dirty_ratio` 20 percent (writers are throttled). `fsync` forces it.
- **Append = 3 writes** (`I`, `B`, `Db`); six partial outcomes; four leave metadata inconsistent; `I`+`B` leaves garbage that `fsck` cannot see.
- **`fsck`**: checks superblock, free blocks, inodes, link counts, duplicates, bad pointers, directories; consistent metadata only; time linear in the volume (0.8 s per million files cached).
- **Journal**: `TxB`, blocks, `TxE`; write the log, **wait**, commit, **wait**, checkpoint, free. Recovery replays committed, drops uncommitted. Batch (5 s). Circular log. Revoke records for reused blocks. jbd2 magic `0xC03B3998`, types descriptor 1, commit 2, superblock 3 and 4, revoke 5. Default journal on this machine: 4,096 KiB for 64 MiB, 32 MiB for 1 GiB.
- **Modes**: `journal` (all data and metadata; 2.01 times the writes), `ordered` (default; data before the metadata commit), `writeback` (no ordering; may expose stale data). Rule: write the pointed-to object before the pointer.
- **Durability calls**: `fsync` (data, metadata, flush), `fdatasync` (skips metadata not needed to read the data, but not size), `O_DSYNC`, `O_SYNC`, **directory `fsync`** for names. 110 µs for a 4 KiB overwrite; create 211 µs, with directory 263 µs; a failed `fsync` is not retryable.
- **Replace a file**: write temp, `fsync`, `rename`, `fsync` directory. Without the first `fsync`: zero-length (17 of 20) or zero-filled (10 of 20).
- **`O_DIRECT`**: aligned buffer, length and offset; no cache, no double caching; not mixed with buffered or `mmap`; a database's choice.
- **LFS**: segments written sequentially; imap and a checkpoint region at a fixed place (two copies, timestamps); roll-forward; cleaner; `D = F / (1 - F) * R * T`; amplification `1 / (1 - u)`; cost-benefit beats greedy on hot and cold data; COW (ZFS, btrfs, WAFL), F2FS and every SSD are descendants.
- **Integrity**: fail-partial; latent sector error and corruption (LSE 9.4 and corruption 0.5 percent for cheap drives, OSTEP); checksums in order of strength XOR, addition, Fletcher, CRC32C (0 misses in 100,000; hardware 10 GB/s); store the checksum in the parent to catch lost writes; physical ID for misdirected writes; scrub; a checksum detects, a second copy repairs.
- **Real file systems**: ext4 physical metadata journal, `ordered`; XFS logical journal (memory); btrfs and ZFS COW with data checksums (memory); F2FS log-structured with checkpoints.

---

## Sources

- [OSTEP (Arpaci-Dusseau), Crash Consistency: FSCK and Journaling](https://pages.cs.wisc.edu/~remzi/OSTEP/file-journaling.pdf) (fetched): the append example and six cases, `fsck` checks, data and metadata journaling, the ordering rule, revoke records, soft updates, COW, backpointers, optimistic crash consistency.
- [OSTEP, Log-structured File Systems](https://pages.cs.wisc.edu/~remzi/OSTEP/file-lfs.pdf) (fetched): segments, the amortisation formula, the inode map, checkpoint regions, roll-forward, the cleaner, cost-benefit, WAFL, ZFS and btrfs as heirs.
- [OSTEP, Data Integrity and Protection](https://pages.cs.wisc.edu/~remzi/OSTEP/file-integrity.pdf) (fetched): fail-partial, LSE and corruption rates, checksum functions, misdirected and lost writes, scrubbing, checksum overhead.
- [Linux kernel: ext4 journal (jbd2)](https://docs.kernel.org/filesystems/ext4/journal.html) (fetched): journal inode 8, block types and magic, descriptor and commit blocks, revocation, checksums, fast commits. The page did not state a default journal size.
- [Linux kernel: ext4 mount options](https://docs.kernel.org/admin-guide/ext4.html) (fetched): `data=` modes, `commit=`, `barrier`, `journal_checksum`, `auto_da_alloc`, `delalloc`, `errors=`.
- [Linux kernel: `/proc/sys/vm`](https://docs.kernel.org/admin-guide/sysctl/vm.html) (fetched): the dirty ratios, byte twins, expire and writeback intervals, `drop_caches`.
- [Linux kernel: F2FS](https://docs.kernel.org/filesystems/f2fs.html) (fetched): log-structured design, NAT, multi-head logging, GC policies, `fsync_mode`, inline data, over-provisioning.
- [fsync(2)](https://man7.org/linux/man-pages/man2/fsync.2.html) and [open(2)](https://man7.org/linux/man-pages/man2/open.2.html) (fetched): `fsync` and `fdatasync` semantics, the directory entry rule, `EIO` reporting since 4.13, `O_SYNC`, `O_DSYNC`, `O_DIRECT` alignment.
- Measurements on this machine: Linux 6.18.44 in a Firecracker VM, GCC 13.3, e2fsprogs 1.47.0, ext4 on a host-cached virtio disk and on loop images, `tmpfs`; crashes simulated by freezing a loop device at a random instant (a prefix of the write stream), 20 to 40 runs per case.
- Claims marked "from memory" were not fetched: the PostgreSQL `fsyncgate` history, XFS logical journaling, btrfs and ZFS checksums and COW details, soft-updates details, SSD flush times, `msync`, `close` errors on network file systems, the OSTEP simulator names and the LeetCode numbers. The cause of the zero-filled files under `auto_da_alloc` and of the `fdatasync` against `fsync` ordering in section 5.2 is not established.
