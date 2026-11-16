# LX 07: Files, permissions and storage in practice

*Part of the [Linux plan](00_plan.md) · Time: about 5 hours · You need: [LX 03](03_processes_signals_and_proc.md) (`/proc`, file descriptors), [LX 04](04_boot_systemd_and_logs.md) (mount units, `dmesg`), [LX 05](05_performance_methodology_and_triage.md) (`iostat`, `fio`, saturation), [LX 06](06_memory_swap_and_the_oom_killer.md) (page cache, tmpfs as `Shmem`) and, for the theory under the tools, [OS 15](../05_operating_systems/15_file_systems.md) and [OS 16](../05_operating_systems/16_crash_consistency_and_the_page_cache.md). The earlier tutorials used the disk as a source of load. This one is about what is on it: why `df` and `du` disagree, what a mount is, which file system to pick, who may do what to a file, and what to do when the file system is damaged.*

"The disk is full but `du` says there is space" and "why can the service not read this file when the mode looks right" are two of the most common Linux questions in infrastructure and systems interviews, and both have mechanical answers. This tutorial gives the mechanism first (a file name, an inode and the data blocks are three different things, section 1), then the four standard reasons a disk looks full (2), mounts (3), the file systems you will meet and when to choose each (4), repair and tuning (5), permissions from mode bits to capabilities (6), the block layer and how to measure it (7), what quotas, LVM, RAID and SMART add (8), and then the costs, mistakes, questions and five problems.

**Seven honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs), as **root**, with e2fsprogs 1.47, xfsprogs, btrfs-progs, util-linux, acl, attr and fio 3.36 (the tools that were missing were installed with `apt`). Every file system in the experiments lives in a **loop device** backed by a file on the sandbox's own disk, so the timings include the host's disk cache and a second layer (the loop driver); they show **shape**, not server numbers.
2. **The kernel has no quota support** (`The kernel was not built with CONFIG_QUOTA and CONFIG_QFMT_V2` for ext4, `quota support not available in this kernel` for XFS), so quotas are **explained from the documentation, not run**. Section 8 shows the exact errors.
3. **LVM and `mdadm` cannot create volumes**: `pvcreate` and `vgcreate` work (they only write metadata), but `lvcreate` fails with `Failure to communicate with kernel device-mapper driver`, and there is no `/proc/mdstat`. So LVM and RAID are shown at the point where they stop and explained from memory. **`smartctl` cannot read SMART data** from a virtual disk, and **`blktrace` is refused** (`Operation not permitted`), so those two are from memory.
4. **The sandbox disk reports `rotational 1`**, which is what virtual block devices commonly report whatever the host has behind them, and its latencies (30 µs for a random read) are those of a host cache. Do not read the `fio` numbers as a disk.
5. There is **no SELinux or AppArmor** here, so mandatory access control is described, not run. File capabilities and ACLs do work.
6. **Timings are single runs** (the small-file test is three) and vary by 10 to 30 percent between runs.
7. The sandbox clock and the user names are those of the sandbox (`ubuntu` is uid 1000). Users 1001, 1002 and 1003 and group 2000 are **bare numbers** made with `setpriv`; no accounts were created.

By the end you can:

- explain **names, inodes and blocks** and say what `df`, `du` and `ls` each count (measured: a file deleted while a process held it open left `df` at **31 MB used** while `du` fell from 31 to **1 MB**);
- diagnose the four classic "full" cases and fix each (measured: a file system with **304 inodes** gave `No space left on device` after **293 files** with `df -h` still showing **56 MB free**; a **1 GiB** sparse file occupied **4 KiB** and `cp --sparse=never` of it filled a 56 MB file system; a 40 MB file hidden under a mount was invisible to `du` and visible through a bind mount of `/`);
- use `mount`, `findmnt`, `blkid`, `losetup` and `fstab` and read `/proc/self/mountinfo` (measured: `remount,ro` gave `Read-only file system`, `noexec` gave `Permission denied` and exit status 126, a read-only bind mount left the original writable);
- choose between **ext4, XFS, btrfs, tmpfs and overlayfs** (measured on 300 MB images: usable space **265, 236 and 300 MB** total with **244, 217 and 219 MB** available; 5,000 small files in **0.13, 0.21 and 1.35 s**; `cp --reflink` of 100 MB cost **100, 0 and 0 MB** on ext4, XFS and btrfs; a 60 MB text file cost **1.79 MiB** on btrfs with `compress=zstd`);
- see **overlayfs copy-up and whiteouts** (measured: a deleted lower file appeared in the upper layer as a character device `0, 0`, and the lower layer was never changed);
- repair a damaged file system (measured: `e2fsck -fn` exited **4**, `-fy` exited **1** and put the orphaned files in `lost+found` as `#43`, `#44`, `#45`; `xfs_repair` found a secondary superblock after the primary was zeroed and the data survived), and tune it (measured: `tune2fs -m 0` raised available space from **24 to 26 MB**);
- read and set **permissions** to the last bit (measured: a directory with `x` and no `r` lets you open a known name but not list it; the sticky bit turned `rm` of another user's file from success into `Operation not permitted`; `umask` 022, 002 and 077 gave modes 644, 664 and 600; a setuid program ran with `euid=0` and `nosuid` turned that off; an ACL mask of `---` made a named-user `r--` entry effectively `---`);
- use **`chattr`**, **extended attributes** and **capabilities** (measured: `chattr +i` stopped root from removing a file; `setcap cap_net_bind_service=+ep` let uid 1001 bind port 80 and `setcap -r` took it away);
- measure storage with `fio` and say what each result means (measured on the loop file system: buffered writes **262,398 IOPS** against direct writes **26,639 IOPS** (mean 37 µs), a write plus `fsync` each time **3,079 IOPS** with a mean `fsync` of **319 µs**, random reads **30,373 IOPS** at queue depth 1 and **335,436 IOPS** at depth 32);
- say what quotas, LVM, RAID and SMART do and what each cannot do.

**Where the numbers come from.** Six experiment scripts (`x1` to `x6`) plus a problem-answer script were run in this session on Linux 6.18.44. Transcripts print `$ command` before each command's output. Numbers that are not from these runs are marked "from memory" or come from a source listed at the end.

**Data and timing.** Nothing here needs a data set. Each experiment builds a small file system (32 to 400 MB) in a file, mounts it under `/mnt/lx07_*`, and removes it. Every one takes seconds. Treat all timings as ranges.

---

## 1. Names, inodes and blocks

A Unix file system separates three things that everyday language calls "a file":

- A **name** (a directory entry) is a line in a directory that maps a string to an **inode number**. A file can have many names (hard links) or none.
- An **inode** holds everything about the file **except its name**: type, owner, group, mode bits, size, timestamps, **link count**, and the pointers to the data (extents on ext4 and XFS). `stat` shows it.
- The **data blocks** hold the contents.

Three consequences explain most of this tutorial:

1. **Deleting a file removes a name** (`unlink`). The inode and blocks are freed only when **the link count is 0 and no process still has the file open**. That is how a deleted log keeps using space (section 2.2) and how an editor can replace a file safely (write a new inode, rename over the old name).
2. **Every file costs one inode, and the number of inodes is fixed at `mkfs` time** on ext4 and XFS (btrfs allocates them as needed). A file system can run out of inodes before it runs out of blocks (section 2.1).
3. **The name lives in the directory, the permissions live in the inode.** Permission to delete or rename a name is a permission on the **directory** (section 6.2), and permission to read the data is a permission on the **inode**.

**What the usual tools count.** This is the table to learn once:

| Tool | Counts | Is blind to |
| --- | --- | --- |
| `df` | blocks (and with `-i`, inodes) the **file system** has allocated, from its own counters (`statfs`) | nothing: it sees deleted-but-open files, hidden mounts and metadata |
| `du` | the blocks of **files it can reach by name**, walking directories | deleted-but-open files, anything under a mount point, other file systems (with `-x`) |
| `ls -l` | the **apparent size** (`st_size`) | sparse holes: a 1 GiB file may use 4 KiB |
| `ls -s`, `du`, `stat %b` | **allocated blocks** (`st_blocks`, in 512 byte units) | compression and deduplication on btrfs (section 4.3) |

So **"`df` is bigger than `du`" means the space is held by something `du` cannot walk to**, and **"`ls` is bigger than `du`" means holes**. The next section measures each.

---

## 2. "The disk is full, but `du` disagrees": four causes

```text
--- 1. no space left, but df says there is space: inodes ran out
$ df -h /mnt/lx07_a | tail -1; df -i /mnt/lx07_a | tail -1
/dev/loop0       60M   24K   56M   1% /mnt/lx07_a
/dev/loop0        304    11   293    4% /mnt/lx07_a
created 293 files, then: No space left on device (errno 28)
$ df -h /mnt/lx07_a | tail -1; df -i /mnt/lx07_a | tail -1
/dev/loop0       60M   24K   56M   1% /mnt/lx07_a
/dev/loop0        304   304     0  100% /mnt/lx07_a
$ echo hello > /mnt/lx07_a/one_more
bash: /mnt/lx07_a/one_more: No space left on device
```

### 2.1 Out of inodes

A 64 MB ext4 file system was made with `mkfs.ext4 -N 300`, which gives **304 inodes** (11 were in use by the root, `lost+found` and the journal). A loop created empty files until it failed: **293 files, then `No space left on device` (errno 28)**, with `df -h` still showing **56 MB available and 1 percent used** and `df -i` showing **100 percent of inodes used**. The error is `ENOSPC` for **both** shortages, which is why the message misleads.

On a real machine the file system has far more inodes (the default is one per 16 KiB of space, from memory of `mke2fs.conf`), so this happens to **mail spools, cache directories, session files, container layers and anything that makes millions of tiny files**. The diagnosis is always `df -i`; the cure is to **delete files** (not to grow the disk, unless you also grow the inode table, which ext4 cannot do without `mkfs`), or to make the file system with `-i` set lower, or to use XFS or btrfs, which create inodes dynamically. Problem A finds the directory that ate them.

### 2.2 Deleted, but still open

```text
--- 2. deleted but still open: df counts it, du cannot see it
$ df -m /mnt/lx07_b | tail -1; du -sm /mnt/lx07_b
/dev/loop0            56    31        22  59% /mnt/lx07_b
31      /mnt/lx07_b
$ df -m /mnt/lx07_b | tail -1; du -sm /mnt/lx07_b
/dev/loop0            56    31        22  59% /mnt/lx07_b
1       /mnt/lx07_b
$ lsof +L1 2>/dev/null | grep -E "^COMMAND|lx07" | cut -c1-110
COMMAND  PID     USER   FD   TYPE DEVICE SIZE/OFF NLINK NODE NAME
python3 1174     root    3r   REG    7,0 31457280     0   12 /mnt/lx07_b/big.log (deleted)
$ ls -l /proc/$P/fd | awk "/deleted/{print \$9, \$10, \$11, \$12}"
3 -> /mnt/lx07_b/big.log (deleted)
$ : > /proc/$P/fd/$FD; df -m /mnt/lx07_b | tail -1
/dev/loop0            56     1        52   1% /mnt/lx07_b
```

A 30 MB file was written, a process opened it and slept, and the file was removed with `rm`. Before and after, **`df` stayed at 31 MB used and 22 MB available**; **`du` fell from 31 to 1 MB**. The space was not freed because **the inode still had an open reference**, and `du` could not find it because it had no name. Two tools find it:

- **`lsof +L1`** lists open files whose **link count is below 1**. The line shows `NLINK 0`, the size in the `SIZE/OFF` column (31,457,280 bytes) and the name followed by `(deleted)`.
- **`ls -l /proc/PID/fd`** shows the same thing from the process side: `3 -> /mnt/lx07_b/big.log (deleted)`. (The first `lsof` run also printed a line for the sandbox's own telemetry program, an anonymous `memfd`; the `grep lx07` removes it.)

Two ways to get the space back:

- **Restart the process**: it closes the file and the kernel frees the blocks. Often impossible in production.
- **Truncate it through `/proc`**: `: > /proc/PID/fd/3`. The file is still open and still has no name, but it is now **0 bytes**, and `df` returned to **1 MB used and 52 MB available** in the run. Because the process still has its descriptor, it can keep writing (the offset does not move back, so a process writing at offset 31 MB makes a sparse file); this is the standard emergency fix for a runaway deleted log, and the right fix is to configure the program to reopen its log on `SIGHUP` or to use `logrotate` with `copytruncate` ([LX 04](04_boot_systemd_and_logs.md)).

The same `/proc` path **reads** the file, so the data of a deleted-but-open file is **recoverable**, which Problem B does with a checksum.

### 2.3 Sparse files: the size is not the space

```text
--- 3. sparse files: the size is not the space
$ ls -ls /mnt/lx07_c | tail -3
20480 -rw-r--r-- 1 root root   20971520 Nov 16 20:04 prealloc.img
    4 -rw-r--r-- 1 root root 1073741824 Nov 16 20:04 sparse.img
20480 -rw-r--r-- 1 root root   20971520 Nov 16 20:04 zeros.img
$ du -h --apparent-size /mnt/lx07_c/sparse.img; du -h /mnt/lx07_c/sparse.img
1.0G    /mnt/lx07_c/sparse.img
4.0K    /mnt/lx07_c/sparse.img
$ stat -c "%n: size %s bytes, %b blocks of %B bytes" /mnt/lx07_c/sparse.img
/mnt/lx07_c/sparse.img: size 1073741824 bytes, 8 blocks of 512 bytes
$ filefrag -v /mnt/lx07_c/sparse.img | head -6
Filesystem type is: ef53
File size of /mnt/lx07_c/sparse.img is 1073741824 (262144 blocks of 4096 bytes)
 ext:     logical_offset:        physical_offset: length:   expected: flags:
   0:   128000..  128000:      13329..     13329:      1:     128000: last
/mnt/lx07_c/sparse.img: 1 extent found
cp: /mnt/lx07_c/copy_dense.img: write failed: No space left on device
$ du -h /mnt/lx07_c/copy_default.img; ls -l /mnt/lx07_c/copy_dense.img 2>&1 | cut -c1-80; df -h /mnt/lx07_c | tail -1
4.0K    /mnt/lx07_c/copy_default.img
-rw-r--r-- 1 root root 15335424 Nov 16 20:04 /mnt/lx07_c/copy_dense.img
/dev/loop0       56M   55M     0 100% /mnt/lx07_c
```

Three 20 MB or 1 GiB files, three different costs:

- `truncate -s 1G` made a file whose **size is 1,073,741,824 bytes and whose allocation is 8 blocks of 512 bytes (4 KiB)**: a **hole**. One byte written at offset 500 MB became **one extent** at logical block 128,000 (500 MB divided by 4096), so the file is one real block plus a hole before it and a hole after it. `du` says **4.0K**, `du --apparent-size` says **1.0G**, `filefrag` shows the one extent.
- `fallocate -l 20M` **reserves** 20 MB of blocks without writing them (allocated, so `ls -s` shows 20,480 KB): the right call to guarantee space for a database file or a swap file.
- A 20 MB file of written zeros costs the same 20,480 KB: **the file system does not detect zeros**.

Copying matters: `cp` (default) made a copy that is **4 KiB on disk** because it detects holes, but **`cp --sparse=never` wrote the whole gigabyte** and failed with `No space left on device` after **15,335,424 bytes**, leaving `df` at **100 percent**. So **a backup or copy that is not sparse-aware can fill a disk with zeros**, and the same applies to `tar` without `-S`, `scp`, and many container image tools (Problem D measures the archive sizes). Virtual machine disk images, Docker's `devicemapper` data files, core files and some databases are sparse on purpose.

### 2.4 A mount hid the data

```text
--- 4. a mount hid the data: files under the mount point still use the parent disk
$ du -sm /var/tmp/lx07_mp; ls /var/tmp/lx07_mp
41      /var/tmp/lx07_mp
hidden.dat
$ du -sm /var/tmp/lx07_mp; ls /var/tmp/lx07_mp
1       /var/tmp/lx07_mp
lost+found
$ ls -l /mnt/lx07_rootview/var/tmp/lx07_mp
total 40960
-rw-r--r-- 1 root root 41943040 Nov 16 20:04 hidden.dat
$ findmnt -o TARGET,SOURCE /var/tmp/lx07_mp
TARGET           SOURCE
/var/tmp/lx07_mp /dev/loop0
$ ls /var/tmp/lx07_mp
hidden.dat
```

A 40 MB file was written to the directory `/var/tmp/lx07_mp`, which lives on the **parent** file system. Then a different file system was mounted **on** that directory. After the mount, `ls` shows the new file system (`lost+found` only) and `du` says **1 MB**: **the 40 MB is still allocated on the parent**, now hidden, and `df` of the parent still counts it. To see it without unmounting, **bind mount `/` somewhere else**: a bind mount of the root shows the parent without the mounts on top, and `ls -l` printed the `41943040` byte file. After the unmount, the file was back.

This is how the data written **before** a disk was mounted (a service that started before `/var/lib/data` was mounted, or an `fstab` entry that failed with `nofail`) ends up hiding on the root file system, filling it, while the application sees an empty directory. `findmnt /path` tells you whether a mount is really there.

**Other causes of "full" (from memory, none run here):** **reserved blocks** (ext4 keeps 5 percent for root by default; section 5.2 measures the effect); **snapshots and clones** that keep old blocks alive (btrfs, ZFS, LVM); **btrfs running out of metadata or unallocated chunks** while `df` shows free space; a **container's overlay layer** growing under `/var/lib/docker` or `/var/lib/containers`; and **a log or core dump directory with no rotation**.

---


## 3. Mounts: how a device becomes a directory

Linux has one directory tree. A **mount** attaches a file system (the contents of a block device, or a pseudo file system like `tmpfs` or `proc`) at a directory of the tree, and the kernel keeps a table of those attachments per **mount namespace**. Everything about mounts follows from that: a mount covers whatever was in the directory (section 2.4), it can be made in one namespace and not another, and the same file system can appear in several places.

```text
--- 1. a file as a block device, and how to identify it
$ losetup -a | grep lx07
/dev/loop0: [65024]:10387457 (/var/tmp/lx07_m.img)
$ lsblk -o NAME,MAJ:MIN,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS $L
NAME  MAJ:MIN SIZE TYPE FSTYPE LABEL MOUNTPOINTS
loop0   7:0    48M loop
$ blkid $L
/dev/loop0: LABEL="labdata" UUID="b2fc9a32-f655-435a-bade-32c6511c6b83" BLOCK_SIZE="4096" TYPE="ext4"
```

A file is not a block device, but `losetup` makes one out of it (`/dev/loop0`, here 48 MB), and `mkfs`, `mount` and `fsck` treat it like a disk. That is how every experiment in this tutorial stays away from the real disk, and it is also how you open a disk image or an ISO. Two ways to identify a device without mounting it: `lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINTS` (the tree of devices, from sysfs and udev) and `blkid` (reads the superblock and prints the **UUID**, label and type). The UUID is what `fstab` should use, because `/dev/sda` and `/dev/nvme0n1` names can change between boots.

### 3.1 Reading the kernel's mount table

```text
--- 2. what the kernel knows: findmnt and mountinfo
$ findmnt /mnt/lx07_m
TARGET      SOURCE     FSTYPE OPTIONS
/mnt/lx07_m /dev/loop0 ext4   rw,relatime
$ findmnt -o TARGET,SOURCE,FSTYPE,OPTIONS /mnt/lx07_m
TARGET      SOURCE     FSTYPE OPTIONS
/mnt/lx07_m /dev/loop0 ext4   rw,relatime
$ grep lx07_m /proc/self/mountinfo
51 27 7:0 / /mnt/lx07_m rw,relatime - ext4 /dev/loop0 rw
```

`findmnt` shows the tree (here one line: target, source, file system type, options) and is the tool to use; it reads **`/proc/self/mountinfo`**, whose line is: mount ID (51), parent ID (27), device major:minor (7:0, the loop device), the root of the mount **inside** the file system (`/`; for a bind mount of a subdirectory it is that subdirectory), the mount point, per-mount options, then ` - ` followed by the file system type, source and **per-superblock** options. That split matters: `ro` can be set **per mount** (a bind mount) or **per file system** (`mount -o remount,ro`), and a file system is read only if either says so. `/proc/mounts` is the older, flatter view of the same table. `findmnt --verify` checks a table for mistakes and is worth running before every reboot after editing `/etc/fstab`.

### 3.2 Options change behaviour

```text
--- 3. mount options change behaviour: ro and noexec
$ echo more >> /mnt/lx07_m/note
bash: /mnt/lx07_m/note: Read-only file system
$ /mnt/lx07_m/prog; echo status=$?
bash: /mnt/lx07_m/prog: Permission denied
status=126
$ echo more >> /mnt/lx07_m/note && echo write ok
write ok
$ findmnt -no OPTIONS /mnt/lx07_m
rw,noexec,relatime
```

`mount -o remount,ro` made the next write fail with **`Read-only file system`** (EROFS), which is also what you see when ext4 **remounts itself read only after an I/O error** (the default `errors=remount-ro` behaviour on many distributions, from memory); `dmesg` has the reason. After `remount,noexec`, running a program from that mount gave **`Permission denied` with exit status 126** (the shell's "found but could not execute"), and the option list read `rw,noexec,relatime`. The ones to know:

| Option | Effect |
| --- | --- |
| `ro` / `rw` | no writes / writes allowed |
| `noexec` | **cannot execute** files from here (scripts run through an interpreter still work: `sh file` reads it as data) |
| `nosuid` | setuid and setgid bits **and file capabilities** are ignored (section 6.4 and 6.7 measure this) |
| `nodev` | device nodes on this mount are not interpreted |
| `relatime` (default) | update `atime` only when it is older than `mtime`/`ctime`, or older than a day (from memory); `noatime` never updates it, which saves a write per read |
| `nofail` (fstab) | boot continues if the device is missing; **without it a missing disk drops boot into emergency mode** ([LX 04](04_boot_systemd_and_logs.md)) |
| `x-systemd.automount`, `x-systemd.device-timeout=` | systemd mount unit settings written in `fstab` |

`/tmp`, `/var/tmp`, `/home` and `/dev/shm` are common places for `nodev,nosuid,noexec` as hardening (a hardening checklist item, from memory); note that it stops `./prog` but not `ld.so /tmp/prog` (from memory, on most distributions).

### 3.3 Bind mounts

```text
--- 4. a bind mount shows one directory in a second place, and can be read only
$ cat /var/tmp/lx07_view/f; echo x > /var/tmp/lx07_view/g; echo x > /mnt/lx07_m/dir/g && echo "original still writable"
data
bash: /var/tmp/lx07_view/g: Read-only file system
original still writable
$ findmnt -o TARGET,SOURCE,OPTIONS /var/tmp/lx07_view
TARGET             SOURCE           OPTIONS
/var/tmp/lx07_view /dev/loop0[/dir] ro,relatime
```

A **bind mount** (`mount --bind /a /b`) makes an existing directory appear at a second place, with no copy. `mount -o remount,bind,ro /b` then makes **only the second view** read only: the output shows `cat` works, a write through `/var/tmp/lx07_view` gives `Read-only file system`, and a write through the original path **still succeeds**. `findmnt` shows the source as `/dev/loop0[/dir]`: the device and the subdirectory. This is the building block of containers (the container's `/` and its volumes are bind mounts), of read-only exposure of a data directory to a service (`ReadOnlyPaths=` and `BindReadOnlyPaths=` in systemd units), and of the trick in section 2.4 to look under a mount.

### 3.4 `fstab`, UUIDs and a safe way to test it

```text
--- 5. fstab: the same mount by UUID, tested with a private table
$ cat /var/tmp/lx07_fstab
UUID=b2fc9a32-f655-435a-bade-32c6511c6b83 /mnt/lx07_fstab ext4 defaults,noatime,nofail 0 2
$ findmnt --verify --tab-file /var/tmp/lx07_fstab 2>&1 | head -5
Success, no errors or warnings detected
$ mount -T /var/tmp/lx07_fstab -a 2>&1; findmnt -n -o TARGET,OPTIONS /mnt/lx07_fstab
/mnt/lx07_fstab rw,noatime
$ ls /mnt/lx07_fstab
dir
lost+found
note
prog
```

An `fstab` line has **six fields**: what to mount (`UUID=...`, `LABEL=...` or a device), where, the type, options, `dump` (almost always 0 now) and `pass` (the `fsck` order: 1 for root, 2 for the rest, 0 for none; from memory). The run used a **private** table (`mount -T file -a`, `findmnt --verify --tab-file file`) so that a typing mistake could not touch the real `/etc/fstab`. Do the same: test with `findmnt --verify`, then `mount -a`, **before** rebooting. A wrong line in `fstab` without `nofail` is a classic cause of a machine that does not come back.

### 3.5 Busy mounts, lazy unmount and namespaces

```text
--- 6. unmount fails when something is using it
$ umount /mnt/lx07_m
umount: /mnt/lx07_m: target is busy.
$ fuser -vm /mnt/lx07_m 2>&1 | head -4
                     USER        PID ACCESS COMMAND
/mnt/lx07_m:         root     kernel mount /mnt/lx07_m
                     root       3449 ..c.. python3
$ lsof +D /mnt/lx07_m 2>/dev/null | cut -c1-90
COMMAND  PID USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
python3 3449 root  cwd    DIR    7,0     4096    2 /mnt/lx07_m
```

`umount` failed with `target is busy` because a shell process (pid 3449 in the run) had its **current directory** inside the mount. `fuser -vm` and `lsof +D` name it (`..c..` means "current directory"; `lsof` shows `cwd`). A process only needs to have **a file or directory open or be inside it** to block an unmount. Options, in order of safety: stop or move the process; `umount -l` (lazy: the mount **leaves the tree immediately**, but the file system stays alive until the last user is gone); never `umount -f` on a local disk.

```text
--- 8. lazy unmount: the mount leaves the tree now (the loop device stays attached, because I attached it by hand)
$ umount -l /mnt/lx07_m; findmnt /mnt/lx07_m || echo "gone from the tree"; losetup -a | grep -c lx07_m
gone from the tree
1
```

The lazy unmount made `findmnt` find nothing, and the `losetup` count stayed 1: **the loop device still existed**, because it was attached by hand, whereas `mount -o loop` sets autoclear so that unmounting detaches it. Hand-attached loop devices leak until `losetup -d`.

```text
--- 7. mount namespaces: a mount made inside is invisible outside
$ unshare -m sh -c "mount -t tmpfs tmpfs /var/tmp/lx07_view; echo inside: \$(findmnt -n -o FSTYPE /var/tmp/lx07_view)"
inside: tmpfs
$ findmnt -n -o FSTYPE /var/tmp/lx07_view || echo "outside: nothing mounted"
outside: nothing mounted
```

**Mount namespaces** make the table per process group: `unshare -m` ran a shell in a new namespace, mounted a `tmpfs` there, and saw it inside while the outside saw nothing. This is how containers, `systemd`'s `PrivateTmp=` and `ProtectSystem=` get a private view of the file system ([LX 03](03_processes_signals_and_proc.md)). It also explains a common puzzle: **"I mounted it, but the service cannot see it"**. The service runs in its own namespace, created before your mount, and by default your mount does not propagate into it unless the mount was `shared` (from memory of `mount(8)`'s propagation types: `shared`, `slave`, `private`, `unbindable`).

---

## 4. File systems: what is different and when to pick each

```text
--- 1. what each one reserves for itself on 300 MB (df -m, empty)
$ df -m -t ext4 -t xfs -t btrfs | grep lx07 | awk "{print \$1, \$2\" MB total\", \$3\" MB used\", \$4\" MB avail\", \$6}"
/dev/loop0 265 MB total 1 MB used 244 MB avail /mnt/lx07_ext4
/dev/loop1 236 MB total 20 MB used 217 MB avail /mnt/lx07_xfs
/dev/loop2 300 MB total 6 MB used 219 MB avail /mnt/lx07_btrfs
$ for t in ext4 xfs btrfs; do printf "%-6s " $t; stat -f -c "type %T  block size %S  files(inodes) %c  name max %l" /mnt/lx07_$t; done
ext4   type ext2/ext3  block size 4096  files(inodes) 76800  name max 255
xfs    type xfs  block size 4096  files(inodes) 153600  name max 255
btrfs  type btrfs  block size 4096  files(inodes) 0  name max 255
```

Three 300 MB images, freshly made, empty. The same 300 MB:

| | ext4 | XFS | btrfs |
| --- | --- | --- | --- |
| Total shown by `df` | 265 MB | 236 MB | 300 MB |
| Available | 244 MB | 217 MB | 219 MB |
| Inodes (`statfs` files) | 76,800 (fixed) | 153,600 (can grow) | 0 (dynamic) |

ext4 took **35 MB** off the total for its inode tables and journal and sets aside 5 percent as a root reservation (section 5.2); XFS left out its **internal log (16,384 blocks of 4 KiB, which is 64 MiB, so the total is 300 MiB minus 64 MiB)** and showed **20 MB used** for its metadata; btrfs reports the full size as total, but its **219 MB available** is lower than that because the default `single` data profile with `DUP` metadata reserves chunks for metadata duplication (from memory of the btrfs documentation; `btrfs filesystem df` shows the profiles). So **do not compare file systems by `df` right after `mkfs`**: the overhead is part of the design.

### 4.1 ext4 and XFS: the journalled workhorses

```text
--- 5. ext4 and XFS fixed properties
$ dumpe2fs -h $(losetup -j /var/tmp/lx07_ext4.img | cut -d: -f1) 2>/dev/null | grep -E "^(Filesystem features|Block size|Inode count|Inode size|Journal size|Reserved block count|Block count)"
Filesystem features:      has_journal ext_attr resize_inode dir_index filetype needs_recovery extent 64bit flex_bg sparse_super large_file huge_file dir_nlink extra_isize metadata_csum
Inode count:              76800
Block count:              76800
Reserved block count:     3840
Block size:               4096
Inode size:               256
$ xfs_info /mnt/lx07_xfs | head -9
meta-data=/dev/loop1             isize=512    agcount=4, agsize=19200 blks
         =                       sectsz=512   attr=2, projid32bit=1
         =                       crc=1        finobt=1, sparse=1, rmapbt=1
         =                       reflink=1    bigtime=1 inobtcount=1 nrext64=0
data     =                       bsize=4096   blocks=76800, imaxpct=25
         =                       sunit=0      swidth=0 blks
naming   =version 2              bsize=4096   ascii-ci=0, ftype=1
log      =internal log           bsize=4096   blocks=16384, version=2
         =                       sectsz=512   sunit=0 blks, lazy-count=1
```

Both are **journalling** file systems with **extents** (a run of blocks stored as start and length, instead of a block list) and both are what production Linux uses most. The difference that matters:

- **ext4**: simple, well understood, **fixed inode count**, can be **grown online and shrunk offline**, great general default, `tune2fs`/`e2fsck`/`debugfs` are mature. The features line above is the feature set: `has_journal`, `extent`, `64bit`, `metadata_csum` (checksums on metadata) and `flex_bg`. The journal protects the **metadata** by default (`data=ordered`), so a crash cannot corrupt the directory structure but a file's data written just before may be old or partly missing ([OS 16](../05_operating_systems/16_crash_consistency_and_the_page_cache.md)).
- **XFS**: built for **large files, large directories and parallel I/O** (allocation groups: `agcount=4` here, many on real disks), dynamic inodes (`imaxpct=25` caps inode space to 25 percent), reflink and `bigtime` support on this version, and it **cannot be shrunk**. Its repair tool, `xfs_repair`, is separate and strict (section 5.3).

**Rule of thumb** (from memory of vendor guidance, not measured): ext4 for boot, small to medium volumes and anything you may need to shrink; XFS for large data volumes, databases and many parallel writers (it is the Red Hat family default).

### 4.2 Speed on many small files

```text
--- 2. 5000 small files (4 KiB each) then sync, three runs each, seconds
ext4  0.15 0.13 0.12
xfs   0.23 0.18 0.20
btrfs 1.40 1.35 1.40
```

Five thousand 4 KiB files plus `sync`, three runs each: ext4 **0.12 to 0.15 s**, XFS **0.18 to 0.23 s**, btrfs **1.35 to 1.40 s**. The loop device exaggerates the cost of anything that writes **more metadata and more flushes**: btrfs's copy-on-write tree updates and checksums cost about ten times ext4 here, which is a **shape** to remember (btrfs is slower for metadata-heavy small-file workloads such as untarring a source tree and for databases without `nodatacow`) and not a number to quote for a server.

### 4.3 btrfs: copy on write, snapshots, checksums, compression

```text
--- 3. copy a 100 MB file: a real copy, then a reflink (shared blocks)
ext4   used before 101 MB, after cp --reflink=auto 201 MB (extra 100 MB)
xfs    used before 120 MB, after cp --reflink=auto 120 MB (extra 0 MB)
btrfs  used before 107 MB, after cp --reflink=auto 107 MB (extra 0 MB)
$ cp --reflink=always /mnt/lx07_ext4/src /mnt/lx07_ext4/copy2 2>&1
cp: failed to clone '/mnt/lx07_ext4/copy2' from '/mnt/lx07_ext4/src': Operation not supported
```

A **reflink** (`cp --reflink`) makes a second file that **shares the data blocks** with the first until one is changed (copy on write). On the 100 MB file the extra space used was **100 MB on ext4** (it silently made a real copy with `--reflink=auto`), and **0 MB on XFS and btrfs**. `--reflink=always` on ext4 fails with `Operation not supported`, which is the right flag for a script that must not fall back to a full copy. The same mechanism makes **snapshots** cheap:

```text
--- 4. btrfs: subvolume, snapshot, compression, and a checksum
$ btrfs subvolume create /mnt/lx07_btrfs/sv | cut -c1-80
Create subvolume '/mnt/lx07_btrfs/sv'
$ btrfs subvolume snapshot /mnt/lx07_btrfs/sv /mnt/lx07_btrfs/snap | cut -c1-80
Create a snapshot of '/mnt/lx07_btrfs/sv' in '/mnt/lx07_btrfs/snap'
$ cat /mnt/lx07_btrfs/snap/file.txt /mnt/lx07_btrfs/sv/file.txt
version one
version two
$ btrfs subvolume list /mnt/lx07_btrfs
ID 256 gen 340 top level 5 path sv
ID 257 gen 340 top level 5 path snap
$ btrfs filesystem df /mnt/lx07_btrfs | grep Data
Data, single: total=136.00MiB, used=100.00MiB
$ btrfs filesystem df /mnt/lx07_btrfs | grep Data
Data, single: total=168.00MiB, used=101.79MiB
$ ls -l /mnt/lx07_btrfs/text.dat | cut -c1-50; du -m /mnt/lx07_btrfs/text.dat
-rw-r--r-- 1 root root 60000000 Nov 16 20:05 /mnt/
58      /mnt/lx07_btrfs/text.dat
```

`btrfs subvolume snapshot` of `sv` took no measurable time and no space until `file.txt` was changed: the snapshot kept `version one` and the live subvolume had `version two`. The two `Data` lines are from before and after a 60 MB text file was written with **`compress=zstd`**: data used rose from 100.00 MiB to **101.79 MiB**, so **60 MB of text cost 1.79 MiB** (it was very repetitive; real text compresses 2 to 4 times, from memory). `du` reported 58 MB for the same file because it counts **the logical size** the file system reports; use `btrfs filesystem df` and `compsize` for the real figure. Also notable: btrfs keeps **checksums of data** and so detects (and with a redundant profile, repairs) silent corruption that ext4 and XFS pass through, and `df` on btrfs is an **estimate**, since the space is shared between data, metadata and snapshots.

Rules: pick btrfs when you want **snapshots, compression and checksums** (workstations, backup targets, container hosts); avoid it for the busiest random-write database volumes unless you set `nodatacow` for them (`chattr +C`, section 6.6) and accept that this turns off checksums for them.

### 4.4 tmpfs: a file system in RAM

```text
--- 1. tmpfs: RAM-backed, size-capped, gone on unmount
$ dd if=/dev/zero of=/mnt/lx07_tmp/fill bs=1M count=30 2>&1 | tail -1
20971520 bytes (21 MB, 20 MiB) copied, 0.0084039 s, 2.5 GB/s
$ df -m /mnt/lx07_tmp | tail -1; grep -E "^Shmem:" /proc/meminfo
tmpfs                 20    20         0 100% /mnt/lx07_tmp
Shmem:             37036 kB
$ ls /mnt/lx07_tmp | wc -l
0
```

`tmpfs` is a file system whose pages live in the **page cache and swap**, with no disk behind it, capped by `size=`. The run wrote 30 MB to a 20 MB mount: the write **stopped at 20,971,520 bytes** and `df` showed **100 percent**, and `Shmem` in `/proc/meminfo` (the tmpfs line from [LX 06](06_memory_swap_and_the_oom_killer.md)) is where that memory is counted. Contents are **gone at unmount or reboot**. Use it for `/run`, `/tmp` on some distributions, `/dev/shm`, build scratch space, test fixtures; **do not** put data you need after a power cut on it, and remember that **it competes with processes for RAM** and, without a `size=`, defaults to half of RAM (from memory).

### 4.5 overlayfs: layers for containers

```text
--- 2. overlayfs: a read-only lower layer, a writable upper layer, one merged view
$ ls $O/merged; ls $O/upper | wc -l
edit.txt
gone.txt
keep.txt
0
$ ls $O/merged
edit.txt
keep.txt
new.txt
$ ls -l $O/upper | tail -n +2 | cut -c1-70
-rw-r--r-- 1 root root   18 Nov 16 20:06 edit.txt
c--------- 2 root root 0, 0 Nov 16 20:06 gone.txt
-rw-r--r-- 1 root root    9 Nov 16 20:06 new.txt
$ cat $O/lower/edit.txt $O/lower/gone.txt
to be changed
to be deleted
$ findmnt -n -o FSTYPE,OPTIONS $O/merged | cut -c1-150
overlay rw,relatime,lowerdir=/var/tmp/lx07_ov/lower,upperdir=/var/tmp/lx07_ov/upper,workdir=/var/tmp/lx07_ov/work,uuid=on
```

An **overlay** mount merges a read-only **`lowerdir`** and a writable **`upperdir`** (plus a `workdir` on the same file system as `upper`, for atomic operations) into one **merged** view. The experiment shows all three behaviours:

- **Read**: `merged` shows `edit.txt`, `gone.txt` and `keep.txt` from the lower layer; `upper` starts empty.
- **Copy up**: editing `edit.txt` through `merged` **copied the whole file into `upper`** first (18 bytes there), and `lower/edit.txt` still reads `to be changed`. The lower layer is **never modified**.
- **Delete**: deleting `gone.txt` created **a character device with major and minor `0, 0`** in `upper` (the `c---------` line): a **whiteout**, which tells the merged view to hide the name. The lower file still has `to be deleted` in it.
- A new file, `new.txt`, exists only in `upper`.

Docker and containerd use this for image layers (many read-only lowers, one writable upper per container), which explains why **writing a big file inside a container grows `/var/lib/docker/overlay2/...`** on the host and why **deleting data in an upper layer does not shrink the image** (it adds a whiteout). The cost is the first-write **copy-up of the whole file** (even for a one-byte change), so a large file that a container edits should live on a volume, not in the layer.

### 4.6 Choosing

| Need | Pick |
| --- | --- |
| general root and boot file system, shrinkable | **ext4** |
| large volume, many parallel writers, big files, databases | **XFS** |
| snapshots, compression, checksums, backups | **btrfs** |
| scratch, sockets, runtime state, `/dev/shm` | **tmpfs** |
| container image layers | **overlayfs** over ext4 or XFS |
| removable media, firmware, interchange | **FAT32 or exFAT** (no permissions, no journal; from memory) |

---

## 5. Repair and tuning

### 5.1 `fsck`: check read only, then repair

```text
--- 3. ext4 damage and e2fsck: kill an inode, check read-only, then repair
$ ls -i /mnt/lx07_e/d | head -3
43 g1
44 g2
45 g3
$ debugfs -w -R "clri <$DI>" $DEV 2>&1 | tail -1
debugfs 1.47.0 (5-Feb-2023)
$ e2fsck -fn $DEV > /var/tmp/lx07_fsck.txt 2>&1; echo exit=$?; grep -v "^\$" /var/tmp/lx07_fsck.txt | head -12
exit=4
e2fsck 1.47.0 (5-Feb-2023)
Pass 1: Checking inodes, blocks, and sizes
Pass 2: Checking directory structure
Entry 'd' in / (2) has deleted/unused inode 42.  Clear? no
Entry 'd' in / (2) has an incorrect filetype (was 2, should be 0).
Fix? no
Pass 3: Checking directory connectivity
Pass 4: Checking reference counts
Inode 2 ref count is 4, should be 3.  Fix? no
Unattached inode 43
Connect to /lost+found? no
Unattached inode 44
$ e2fsck -fy $DEV > /var/tmp/lx07_fsck.txt 2>&1; echo exit=$?; grep -v "^\$" /var/tmp/lx07_fsck.txt | tail -3
exit=1
Fix? yes
/dev/loop0: ***** FILE SYSTEM WAS MODIFIED *****
/dev/loop0: 44/8192 files (2.3% non-contiguous), 1582/8192 blocks
$ e2fsck -fn $DEV > /var/tmp/lx07_fsck.txt 2>&1; echo exit=$?; tail -1 /var/tmp/lx07_fsck.txt
exit=0
/dev/loop0: 44/8192 files (2.3% non-contiguous), 1582/8192 blocks
$ ls /mnt/lx07_e/lost+found | head; ls /mnt/lx07_e | wc -l
#43
#44
#45
31
```

**Method.** Never run `fsck` on a **mounted** file system (it can corrupt a good one); unmount or boot from rescue media. Run **`e2fsck -fn`** first (`-f` force a check even if marked clean, `-n` answer no to everything so nothing is changed) and read the report; only then `-fy` (`-y` answer yes). In the run, the directory entry `d` in the root pointed at an inode that `debugfs` had zeroed (`clri`), so:

- `e2fsck -fn` exited **4** (**errors left uncorrected**) and listed them: a directory entry with a **deleted/unused inode**, a bad file type, a wrong reference count on the root, and **unattached inodes 43, 44, 45** (the files that used to be in the lost directory).
- `e2fsck -fy` exited **1** (**errors corrected**) and printed `FILE SYSTEM WAS MODIFIED`.
- A second `-fn` exited **0** (clean).
- The three orphans were **reconnected to `lost+found` under their inode numbers** (`#43`, `#44`, `#45`). The names were lost, since names are in the directory that was destroyed. **That is the point of `lost+found`**: the data survives, the names do not.

The exit status is a bit mask (from memory of `e2fsck(8)`): 0 clean, 1 errors corrected, 2 corrected and a reboot needed, 4 errors left uncorrected, 8 operational error, 16 usage error, 32 cancelled, 128 shared library error. The five passes are also in the output: inodes, directory structure, directory connectivity, reference counts, group summaries. `debugfs` is a read-write explorer of an unmounted ext4 (`-w` to write; as here, to cause damage in a lab) and `dumpe2fs` prints the superblock and group layout.

**Where the damage comes from in real life** (from memory): power loss (a journal replay at the next mount handles most of it), failing hardware, bugs, and **writing to the underlying device while mounted**. If `e2fsck` finds many errors on a disk, **copy the device first with `dd` or `ddrescue`** and repair the copy.

### 5.2 `tune2fs`: reserved space, labels, check policy

```text
--- 4. tune2fs: reserved blocks, label, and what the journal costs
$ df -m /mnt/lx07_e | tail -1
/dev/loop0            26     1        24   1% /mnt/lx07_e
$ tune2fs -m 0 -L newname $DEV 2>&1 | tail -2
tune2fs 1.47.0 (5-Feb-2023)
Setting reserved blocks percentage to 0% (0 blocks)
$ df -m /mnt/lx07_e | tail -1
/dev/loop0            26     1        26   1% /mnt/lx07_e
$ tune2fs -l $DEV | grep -E "^(Filesystem volume name|Reserved block count|Mount count|Maximum mount count|Check interval|Journal)"
Filesystem volume name:   newname
Reserved block count:     0
Mount count:              2
Maximum mount count:      -1
Check interval:           0 (<none>)
Journal inode:            8
Journal backup:           inode blocks
```

ext4 reserves **5 percent of blocks for root** (`Reserved block count` 3,840 of 76,800 here) so that root and system daemons can still work and fragmentation stays lower when users fill the disk. On a **data volume of several terabytes, 5 percent is wasted space**: `tune2fs -m 0` raised the available space on this small image from **24 to 26 MB** (the same 5 percent). On the root file system, leave it. The same command changed the label (`newname`), and `tune2fs -l` shows the policy: **`Maximum mount count -1` and `Check interval 0`** mean there is **no periodic forced `fsck`**, which is the modern default (a journalled file system replays instead). `tune2fs -O` adds and removes features, and **`resize2fs`** grows (online) or shrinks (offline) an ext4.

### 5.3 XFS: `xfs_repair`

```text
--- 5. XFS: lose the primary superblock, repair from a secondary copy
$ mount $DEV /mnt/lx07_x 2>&1 | head -2
mount: /mnt/lx07_x: wrong fs type, bad option, bad superblock on /dev/loop0, missing codepage or helper program, or other error.
       dmesg(1) may have more information after failed mount system call.
$ xfs_repair -n $DEV 2>&1 | head -6 | cut -c1-110
Phase 1 - find and verify superblock...
bad primary superblock - bad magic number !!!
```

The first block of an XFS file system (the primary superblock) was **overwritten with zeros**, and the mount failed (`wrong fs type, bad option, bad superblock`; `dmesg` has the detail). `xfs_repair -n` (no modify) said `bad primary superblock - bad magic number`, **searched the device for a secondary superblock** (XFS keeps one at the start of every allocation group) and found and verified one; the real `xfs_repair` then rebuilt the primary from it and the file `precious` still read `keepme`. Notes: `xfs_repair` needs an **unmounted** file system; there is **no `-y`** (it does not ask), and **`xfs_repair -L` zeroes the log and loses recent metadata changes**, so it is a last resort used only when the log cannot be replayed (from memory of `xfs_repair(8)`). The general `fsck` wrapper for XFS does nothing, because XFS recovers by **replaying its log at mount time**.

---


## 6. Permissions: who may do what

Every access check by the kernel compares the **process's credentials** (effective uid, effective gid, supplementary groups, capabilities) with the **inode's** owner, group, mode and ACL. All the experiments below use bare user ids made with `setpriv`, on an ext4 image, with a helper `as UID cmd` and `asg UID GID cmd` in the scripts.

### 6.1 Mode bits: three triplets

```text
--- 1. the three triplets: owner, group, other (user 1001 in group 2000, user 1002 outside it)
$ ls -l $M/f; stat -c "%A  octal %a  owner %u  group %g" $M/f
-rw-r----- 1 ubuntu 2000 7 Nov 16 20:07 /mnt/lx07_p/f
-rw-r-----  octal 640  owner 1000  group 2000
$ asg 1001 2000 cat $M/f
secret
$ as 1002 cat $M/f
cat: /mnt/lx07_p/f: Permission denied
$ asg 1001 2000 sh -c "echo x >> $M/f"
sh: 1: cannot create /mnt/lx07_p/f: Permission denied
```

`-rw-r-----` is **owner `rw-`, group `r--`, other `---`**, octal **640** (r is 4, w is 2, x is 1). The file belonged to uid 1000 and group 2000. User 1001, a member of group 2000, **read** it; user 1002, in neither, got `Permission denied`; and 1001 could not **write** it, because the group triplet has no `w`. The kernel picks **exactly one** triplet: owner if the uid matches, **else** group if any of the process's groups match, **else** other. So an owner with mode `070` is **denied** what the group is granted, a famous trap. **Root** (uid 0, or anyone holding `CAP_DAC_OVERRIDE`) skips the check for reading and writing, and for executing needs at least one `x` bit set somewhere.

### 6.2 Directories: `r`, `w`, `x` mean different things, and the sticky bit

```text
--- 2. directories: r lists names, x lets you reach a name, w lets you create and delete names
$ as 1002 ls $M/d; as 1002 cat $M/d/known
known
cat: /mnt/lx07_p/d/known: Permission denied
$ as 1002 ls $M/d; as 1002 cat $M/d/known
ls: cannot open directory '/mnt/lx07_p/d': Permission denied
inside
$ as 1002 rm $M/d/known && echo "deleted a file it does not own (no sticky bit)"
deleted a file it does not own (no sticky bit)
$ as 1002 rm $M/d/known; ls -ld $M/d
rm: cannot remove '/mnt/lx07_p/d/known': Operation not permitted
drwxrwxrwt 2 ubuntu ubuntu 4096 Nov 16 20:07 /mnt/lx07_p/d
```

On a directory, **`r` lets you list the names, `x` lets you reach a name** (look it up, `cd` into it, open files in it), and **`w` (with `x`) lets you create, delete and rename names**. The four lines show it:

- Mode with `r` but **no `x`**: `ls` printed `known`, but `cat d/known` failed with `Permission denied`.
- Mode with `x` but **no `r`**: `ls` failed (`cannot open directory`), but `cat d/known` **worked and printed `inside`**: you can open a name you already know.
- A directory with `w` for everyone and **no sticky bit**: user 1002 **deleted a file it did not own**. Deleting needs `w` on the directory, **not on the file**.
- After setting the **sticky bit** (`drwxrwxrwt`, the trailing `t`): the same `rm` gave **`Operation not permitted`**. With it, only the file's owner, the directory's owner or root can delete or rename an entry. That is why `/tmp` is mode **1777**.

### 6.3 `umask`: new files start loose and the mask removes bits

```text
--- 3. umask: new files start at 666, new directories at 777, and the mask is subtracted
$ (umask 022; rm -rf /mnt/lx07_p/u; touch /mnt/lx07_p/u; mkdir /mnt/lx07_p/ud; stat -c '%a %n' /mnt/lx07_p/u /mnt/lx07_p/ud; rmdir /mnt/lx07_p/ud)
644 /mnt/lx07_p/u
755 /mnt/lx07_p/ud
$ (umask 002; rm -rf /mnt/lx07_p/u; touch /mnt/lx07_p/u; mkdir /mnt/lx07_p/ud; stat -c '%a %n' /mnt/lx07_p/u /mnt/lx07_p/ud; rmdir /mnt/lx07_p/ud)
664 /mnt/lx07_p/u
775 /mnt/lx07_p/ud
$ (umask 077; rm -rf /mnt/lx07_p/u; touch /mnt/lx07_p/u; mkdir /mnt/lx07_p/ud; stat -c '%a %n' /mnt/lx07_p/u /mnt/lx07_p/ud; rmdir /mnt/lx07_p/ud)
600 /mnt/lx07_p/u
700 /mnt/lx07_p/ud
```

A new file is created with mode **666** (no execute) and a new directory with **777**, **minus the bits set in `umask`** of the creating process. `umask 022` gave **644 and 755**, `002` gave **664 and 775** (the setting for shared group work, with a per-user group), `077` gave **600 and 700** (private). `umask` is inherited from the parent process and so is set in a login profile or in a systemd unit (`UMask=`); an application can pass an explicit mode to `open` but the mask still applies.

### 6.4 setgid directories and setuid programs

```text
--- 4. setgid directory: new files inherit the directory's group
$ ls -ld $M/team
drwxrwsr-x 2 ubuntu 2000 4096 Nov 16 20:07 /mnt/lx07_p/team
$ ls -l $M/team/by1001
-rw-r--r-- 1 1001 2000 0 Nov 16 20:07 /mnt/lx07_p/team/by1001
```

A directory with the **setgid bit** (`drwxrwsr-x`, the `s` in the group triplet) gives **new files the directory's group** instead of the creator's primary group: user 1001's file came out with group **2000**. This is the standard base for a shared project directory.

```text
--- 5. setuid: the program runs with the owner's identity; nosuid turns it off
$ as 1001 $M/ids
  ruid=1001 euid=1001
$ ls -l $M/ids | cut -c1-30; as 1001 $M/ids
-rwsr-xr-x 1 root root 16048 N
  ruid=1001 euid=0
$ as 1001 $M/ids
  ruid=1001 euid=1001
```

A **setuid** program (`-rwsr-xr-x`, the `s` in the owner triplet) runs with the **file owner's** effective uid. The helper `ids` prints the real and effective uid: for user 1001 it printed `ruid=1001 euid=1001` before, **`ruid=1001 euid=0`** with the bit set on a root-owned file, and `euid=1001` again after the file system was remounted **`nosuid`**. This is how `passwd`, `sudo` and `su` work, and why **a setuid-root binary is the highest-risk file on a system** and `nosuid` on data mounts is a basic control. Setuid is ignored for shell scripts on Linux (from memory), so it only matters for compiled programs.

### 6.5 ACLs: more than one owner and group

```text
--- 6. ACLs: more than one group or user on a file
$ as 1002 cat $M/a
cat: /mnt/lx07_p/a: Permission denied
$ ls -l $M/a; getfacl -p $M/a | grep -v "^$"
-rw-r-----+ 1 root root 4 Nov 16 20:07 /mnt/lx07_p/a
# file: /mnt/lx07_p/a
# owner: root
# group: root
user::rw-
user:1002:r--
group::---
mask::r--
other::---
$ as 1002 cat $M/a
acl
$ getfacl -p $M/a | grep -E "user:1002|mask"; as 1002 cat $M/a
user:1002:r--   #effective:---
mask::---
cat: /mnt/lx07_p/a: Permission denied
$ getfacl -p $M/team | grep default
default:user::rwx
default:group::rwx
default:group:2000:rwx
default:mask::rwx
default:other::r-x
```

Mode bits give one user and one group. **POSIX ACLs** add **named users and groups** (`setfacl -m u:1002:r file`, read with `getfacl`). `ls -l` shows a trailing **`+`** on a file with an ACL. In the run, 1002 was denied until `user:1002:r--` was added, and then read it. The entry that surprises everybody is the **mask**: it caps the permissions of **all named users, all groups and the owning group**. Here `mask::---` turned the named entry into **`user:1002:r--   #effective:---`**, and 1002 was denied again, while `getfacl` still showed `r--` in the entry. Note that **`chmod g=...` on a file with an ACL changes the mask**, not the owning-group entry, which is a classic way for an ACL to stop working after a `chmod`.

A **default ACL** on a directory (`setfacl -d -m ...`, shown by `getfacl` as lines starting `default:`) is the template that **new files inside inherit**, so a group gets access to files created later by anyone. The last lines of the output show the directory's default entries (`default:group:2000:rwx`).

`ls -l` and `getfacl` also depend on the file system having ACL support (ext4, XFS and btrfs have it on by default; NFS depends on the version, from memory).

### 6.6 `chattr` flags and extended attributes

```text
--- 7. chattr: flags that even root obeys
$ lsattr $M/imm; rm -f $M/imm; echo "write: $(echo x 2>&1 >> $M/imm)"
----i---------e------- /mnt/lx07_p/imm
rm: cannot remove '/mnt/lx07_p/imm': Operation not permitted
write: bash: /mnt/lx07_p/imm: Operation not permitted
$ echo more >> $M/app.log && echo "append ok"; echo over > $M/app.log; echo "overwrite status $?"; lsattr $M/app.log
append ok
bash: /mnt/lx07_p/app.log: Operation not permitted
overwrite status 1
-----a--------e------- /mnt/lx07_p/app.log
```

`chattr` sets **inode flags that root also obeys**, shown by `lsattr`. The `e` is the extents flag, set automatically on ext4. **`+i` (immutable)** made `rm` and a write both fail with `Operation not permitted` **for root**; **`+a` (append only)** allowed `>>` (the first line, `append ok`) but refused `>` (overwrite, status 1). Setting or clearing `i` and `a` needs `CAP_LINUX_IMMUTABLE`, which is why a container without that capability cannot unset them. Use `+a` for audit logs, `+i` for a file an attacker or a bad script must not change (and for a **marker file that stops a service writing to an unmounted mount point**: make the empty mount point directory `chattr +i`, so that a write before the mount fails instead of silently filling the root, section 2.4). Other flags: **`+C`** (no copy on write, for btrfs files that get random writes), `+c`, `+s`, `+u` (not honoured by ext4, from the manual page), **`+j`** (journal data, needs `CAP_SYS_RESOURCE`).

```text
--- 8. extended attributes: name and value pairs stored with the inode
$ getfattr -d $M/f 2>&1 | grep -v "^\$"
getfattr: Removing leading '/' from absolute path names
# file: mnt/lx07_p/f
user.owner_team="storage"
```

**Extended attributes** are name and value pairs kept with the inode; names have a **namespace**: `user.` (free use, checked by the normal permissions), `trusted.` and `security.` (root and security modules), `system.` (the kernel's own: POSIX ACLs are stored as `system.posix_acl_access`, from memory). `setfattr -n user.owner_team -v storage file` and `getfattr -d file` set and show them. The overlayfs whiteout marker in section 4.5 is one (`trusted.overlay.whiteout`, from the kernel documentation), file capabilities are one (`security.capability`), and SELinux labels are `security.selinux`. `cp` and `tar` drop them unless told otherwise (`cp -a`, `tar --xattrs`).

### 6.7 Capabilities: part of root instead of all of it

```text
--- 9. capabilities: one privilege instead of all of root
$ as 1001 $M/bind80
  uid 1001: bind(80) failed: Permission denied
$ getcap $M/bind80; as 1001 $M/bind80
/mnt/lx07_p/bind80 cap_net_bind_service=ep
  uid 1001: bind(80) ok
$ as 1001 $M/bind80
  uid 1001: bind(80) failed: Permission denied
$ grep -E "^(CapEff|CapBnd)" /proc/self/status; capsh --decode=000001fffeffffff 2>/dev/null | cut -c1-90
CapEff: 000001fffeffffff
CapBnd: 000001fffeffffff
0x000001fffeffffff=cap_chown,cap_dac_override,cap_dac_read_search,cap_fowner,cap_fsetid,ca
```

Linux splits root into about forty **capabilities**. The experiment: user 1001 ran a program that tries to `bind` TCP port 80; it failed with `Permission denied` (ports below 1024 need **`CAP_NET_BIND_SERVICE`**). `setcap cap_net_bind_service=+ep` on the **binary** let the same user bind port 80 (`getcap` showed `cap_net_bind_service=ep`); after `setcap -r` it failed again. The `ep` means **effective and permitted**: the capability is in the process's permitted set at `exec` and turned on. The sandbox root's `CapEff` and `CapBnd` are both `000001fffeffffff`, which `capsh --decode` expands to the long list that starts `cap_chown,cap_dac_override,cap_dac_read_search,cap_fowner,...`; the one bit missing from the full set is `CAP_SYS_RESOURCE`, which is why some limit changes are refused here.

The ones to know: `CAP_NET_BIND_SERVICE` (ports below 1024), `CAP_NET_RAW` (ping, raw sockets), `CAP_NET_ADMIN` (interfaces, routes, firewall), `CAP_DAC_OVERRIDE` (skip file permission checks), `CAP_CHOWN`, `CAP_FOWNER`, `CAP_SYS_ADMIN` (the large catch-all: mounts, namespaces, many ioctls; treat it as root), `CAP_SYS_RESOURCE` (override limits and quotas), `CAP_LINUX_IMMUTABLE`. Four sets matter: **permitted, effective, inheritable** and the **bounding set** (the ceiling that `docker run --cap-drop` lowers). `nosuid` also ignores file capabilities (section 3.2), and systemd offers `AmbientCapabilities=` and `CapabilityBoundingSet=` so a service gets only the capability it needs, which is **the right answer to "my service needs port 443 and must not run as root"**.

**Not run here:** SELinux and AppArmor (mandatory access control, an extra check **after** the ones above; `ls -Z` shows a context and `getenforce` the mode; there is none in this VM). When a permission looks right and access is still denied on a Red Hat style system, look at `ausearch -m avc -ts recent` and `getenforce` (from memory).

### 6.8 The order of checks, summarised

For one `open`: 1. the **mount**: read only, or `noexec` for execute; 2. every **directory on the path** needs `x`; 3. the **inode flags** (`i`, `a`); 4. **owner, then named user ACL, then group or named group, then other** (the first match decides, not the union), limited by the ACL mask; 5. **capabilities** can override step 4 (`CAP_DAC_OVERRIDE`, `CAP_DAC_READ_SEARCH`); 6. **MAC** (SELinux or AppArmor) if present. A denied access that "should work" is almost always in step 1, 2 or 3.

---

## 7. The block layer, measured

```text
--- 1. the block device and its queue (the sandbox disk, /dev/vda)
$ lsblk -d -o NAME,SIZE,ROTA,TYPE /dev/vda
NAME  SIZE ROTA TYPE
vda   256G    1 disk
$ for f in rotational scheduler read_ahead_kb nr_requests logical_block_size physical_block_size max_sectors_kb; do printf "%-22s %s\n" $f "$(cat /sys/block/vda/queue/$f)"; done
rotational             1
scheduler              none [mq-deadline] kyber bfq
read_ahead_kb          128
nr_requests            256
logical_block_size     512
physical_block_size    4096
max_sectors_kb         4096
```

Below the file system sits the **block layer**: requests are queued per device, merged, scheduled and sent to the driver. `/sys/block/DEV/queue/` shows its settings: the sandbox disk `vda` is `256G`, **`rotational` 1** (virtio disks usually say this whatever the host has behind them, so do not trust it), scheduler **`mq-deadline`** (the others offered: `none`, `kyber`, `bfq`; NVMe devices default to `none`, from memory), `read_ahead_kb` 128, `nr_requests` 256, logical block 512 and **physical block 4096**. Partitions should be **aligned to the physical block** (modern tools use 1 MiB) or each 4 KiB write becomes a read-modify-write.

### 7.1 `iostat -x`: what each field means

`iostat -x 1` ([LX 05](05_performance_methodology_and_triage.md) used it; this section is the field list, **from memory of `iostat(1)`**, not a measurement):

| Field | Meaning | What to read from it |
| --- | --- | --- |
| `r/s`, `w/s` | completed reads and writes per second | the IOPS the device is serving |
| `rkB/s`, `wkB/s` | throughput | MB/s |
| `rareq-sz`, `wareq-sz` | average request size (KiB) | small and many = random; large = sequential |
| `r_await`, `w_await` | average time (ms) a request spent **queued plus serviced** | **the number users feel**; compare with the device's normal latency |
| `aqu-sz` | average number of requests in the queue | depth: above the device's useful parallelism, you are queueing |
| `%util` | share of time the device had **at least one request in flight** | **see below** |
| `%rrqm`, `%wrqm` | requests merged before dispatch | high = the scheduler is coalescing sequential I/O |

**Why `%util` misleads on SSD and NVMe.** `%util` is "the device was busy at least one request at a time". A rotating disk can do one request at a time, so 100 percent really means saturated. An **SSD or NVMe drive handles dozens to thousands in parallel**, so one request in flight all the time already shows 100 percent while the drive could do many times more. The fio result below shows it: **queue depth 1 gave 30,373 IOPS and queue depth 32 gave 335,436 IOPS on the same device**, about 11 times, and both would show `%util` near 100. Use **`aqu-sz` and `await`** (and compare with a baseline) instead: saturation shows as **`await` rising well above the idle service time with `aqu-sz` growing**.

### 7.2 `fio`: measuring a file system and a device

```text
--- 2. fio on ext4 (a 256 MB loop device on that disk), 3 s per job, 4 KiB blocks, one file of 128 MB
seqwrite_buffered      write    262398 IOPS   1025.0 MiB/s  mean      1.9 us  p99      5.7 us
seqwrite_direct        write     26639 IOPS    104.1 MiB/s  mean     37.0 us  p99     62.7 us
write_fsync_each       write      3079 IOPS     12.0 MiB/s  mean      5.0 us  p99     20.6 us  fsync mean    318.6 us
randread_qd1           read      30373 IOPS    118.6 MiB/s  mean     32.3 us  p99     52.5 us
randread_qd32          read     335436 IOPS   1310.3 MiB/s  mean     95.2 us  p99    207.9 us
randread_buffered      read      30311 IOPS    118.4 MiB/s  mean     32.3 us  p99     67.1 us
```

`fio` generates a defined I/O pattern and reports IOPS, bandwidth and latency percentiles. Each line above is one 3 second job on ext4, 4 KiB blocks, a 128 MB file; **read the shape, not the numbers** (a loop device on a virtual disk, honest limit 4):

- **Buffered sequential write** (`psync`, through the page cache): **262,398 IOPS, 1,025 MiB/s, mean 1.9 µs**. This only measures **copying into memory**: the page cache absorbs the write ([OS 16](../05_operating_systems/16_crash_consistency_and_the_page_cache.md)), the disk is not in the loop.
- **Direct write, queue depth 1** (`--direct=1`, `libaio`, bypass the cache): **26,639 IOPS, 104 MiB/s, mean 37.0 µs, p99 62.7 µs**. This is the real cost of getting a write to the device, about **ten times slower** than the buffered line (26,639 against 262,398 IOPS).
- **Write followed by `fsync` each time**: **3,079 IOPS, 12 MiB/s**, with a mean **`fsync` of 319 µs**. This is a **database commit**: durability (the data is on stable storage when the call returns) costs about **nine times** more again (26,639 against 3,079 IOPS), and it is why databases batch commits, use a log, and why battery or capacitor backed write caches exist.
- **Random read, depth 1, direct**: **30,373 IOPS, mean 32.3 µs, p99 52.5 µs**.
- **Random read, depth 32, direct**: **335,436 IOPS, 1,310 MiB/s, mean 95.2 µs, p99 207.9 µs**. Eleven times the throughput for three times the latency: the device is **parallel**, and latency rises with depth because requests wait their turn (Little's law: depth is about IOPS times latency).
- **Random read, buffered**: 30,311 IOPS, mean 32.3 µs, the same as the direct depth 1 line. In this run the reads were therefore **not served from the page cache** (I did not find out why; the loop device and the host cache are the suspects). On a real system a re-read file would be far faster: **always check that the file is much larger than RAM, or use `--direct=1`, when you mean to measure the disk**.

**Rules for a trustworthy `fio` run** (from memory of the fio documentation and practice): use a file or device **bigger than RAM** or `--direct=1`; run **long enough to pass cache and burst effects** (minutes, not 3 seconds, on real drives, since SSDs slow down after their fast cache fills); state the **block size, depth, read and write mix and number of jobs**; report **percentiles**, not just means; never run a write test on a device that has data you want. **Do not run `fio` on a production disk during business hours** (it is a load generator, [LX 05](05_performance_methodology_and_triage.md)).

### 7.3 What this VM could not show

The last two commands in the output of section 8 (`smartctl` and `blktrace`) are the relevant ones.

**`blktrace`** records every request through the block layer (queue, merge, dispatch, complete), and **`blkparse`** or `btt` turn that into per-request latency split into Q2D (queue time), D2C (device time) and the rest; `bpftrace` and `biolatency` do the same with less overhead. Here `blktrace` was refused (`Operation not permitted`; it needs debugfs and a capability the sandbox lacks), so this is **from memory**. **`smartctl -a /dev/DEV`** reads the drive's **SMART** counters (reallocated sectors, pending sectors, wear level, temperature, power-on hours, for NVMe the `Percentage Used` and `Media errors`); on this virtual disk it said **`Unable to detect device type`**. A failing drive often shows **rising reallocated or pending sector counts** before it fails, and `smartctl -H` gives a one-word health verdict that is **optimistic**, so read the attributes.

---

## 8. Quotas, LVM, RAID: what they add, and where the VM stopped

```text
--- 3. quota: ext4 needs kernel support for it, XFS has its own
$ mount -o usrquota $L /mnt 2>&1 | head -1; dmesg | tail -1 | cut -c1-110
mount: /mnt: wrong fs type, bad option, bad superblock on /dev/loop0, missing codepage or helper program, or other error.
[  294.992829] EXT4-fs (loop0): The kernel was not built with CONFIG_QUOTA and CONFIG_QFMT_V2
$ mount -o uquota $Q /mnt/lx07_q 2>&1 | head -1; dmesg | tail -2 | cut -c1-120
mount: /mnt/lx07_q: wrong fs type, bad option, bad superblock on /dev/loop0, missing codepage or helper program, or other error.
[  295.033462] XFS (loop0): Unmounting Filesystem 7e5bff31-d31b-46ae-bc41-dfb595ae1a5b
[  295.043879] XFS (loop0): quota support not available in this kernel.
```

**Quotas** limit space and file counts per user, group or project (a directory tree). ext4 needs the kernel built with **`CONFIG_QUOTA` and `CONFIG_QFMT_V2`**, and this kernel was not: the `mount -o usrquota` failed and `dmesg` said so. XFS has its own accounting (`uquota`, `gquota`, `pquota` mount options) and also **refused** (`quota support not available in this kernel`). What would have been shown (from memory of `quota(1)` and `xfs_quota(8)`): a **soft limit** that can be exceeded for a **grace period** (default 7 days) and a **hard limit** that never can; `edquota -u USER` to set, `quota -u USER` and `repquota -a` to report; and, on XFS, **project quotas** that cap one directory tree regardless of who owns the files, which is how container storage limits are done on XFS. When a quota is hit the error is **`Disk quota exceeded` (EDQUOT)**, not `No space left on device`, which is the message that tells you to look at quotas and not `df`.

```text
--- 4. LVM, md RAID, SMART, blktrace: what this VM allows
$ pvcreate -q $A $B 2>&1 | tail -2; vgcreate -q vgt $A $B 2>&1 | tail -1; pvs --noheadings -o pv_name,vg_name,pv_size 2>&1 | grep -E "loop"; vgs --noheadings -o vg_name,vg_size,vg_free
  Physical volume "/dev/loop0" successfully created.
  Physical volume "/dev/loop1" successfully created.
  Volume group "vgt" successfully created
  /dev/loop0 vgt 60.00m
  /dev/loop1 vgt 60.00m
  vgt 120.00m 120.00m
$ lvcreate -q -L 20M -n lv1 vgt 2>&1 | head -2
  /dev/mapper/control: open failed: No such device
  Failure to communicate with kernel device-mapper driver.
$ mdadm --create /dev/md0 --level=1 --raid-devices=2 --run $A $B 2>&1 | tail -1; ls /proc/mdstat 2>&1
mdadm: unexpected failure opening /dev/md0
ls: cannot access '/proc/mdstat': No such file or directory
$ smartctl -i /dev/vda 2>&1 | grep -i "unable"
/dev/vda: Unable to detect device type
$ blktrace -d /dev/vda -o - 2>&1 | head -1
Invalid path /dev/vda specified: 1/Operation not permitted
```

**LVM** (Logical Volume Manager) stacks three layers: **physical volumes** (PV, a disk or partition) are pooled into a **volume group** (VG), and **logical volumes** (LV) are carved from the pool and hold the file system. The first two layers worked in the VM (the output shows two 60 MB PVs joined into a 120 MB VG `vgt` with 120 MB free) because they only write metadata. **`lvcreate` then failed** (`Failure to communicate with kernel device-mapper driver`), since this kernel has no device-mapper. On a real host the workflow is: `lvcreate -L 20G -n data vg`, `mkfs.xfs /dev/vg/data`, and later **`lvextend -r -L +10G /dev/vg/data`** (the `-r` grows the file system too), which is why LVM exists: **resize without repartitioning, spanning disks, and snapshots** (`lvcreate -s`, copy on write at the block level; a full snapshot is **invalidated**, a common surprise, from memory).

**`mdadm` software RAID** (the same output block): `mdadm --create` failed (`unexpected failure opening /dev/md0`) and there is no **`/proc/mdstat`**, because the md driver is absent. From memory: RAID 1 mirrors (survives one disk failure, reads can be spread), RAID 5 stripes with one disk of parity (survives one failure, **slow rebuild and a write penalty**), RAID 6 two parities, RAID 10 mirrors of stripes. `cat /proc/mdstat` shows the state and `[UU]` or `[U_]`: **`_` is a failed member**. **RAID is not a backup**: it protects against a disk dying, not against `rm`, corruption or a mistake that is mirrored instantly.

---

## 9. What it costs

The costs of the choices above, using only what was measured:

- **A durable write costs about a hundredth of a cached write, and the cache costs memory:** 262,398 IOPS buffered, 26,639 direct, 3,079 with `fsync` each time on the same file system. If a program calls `fsync` after every record, **batching ten records per `fsync` is about a tenfold gain**; if it never calls it, **a crash loses the cache** (up to the `dirty` settings, [LX 06](06_memory_swap_and_the_oom_killer.md)).
- **Depth buys throughput at the price of latency:** 11 times the IOPS at 3 times the mean latency going from depth 1 to 32. Latency-sensitive services run shallow queues; batch jobs run deep.
- **btrfs costs about ten times ext4 on 5,000 small files** (1.35 to 1.40 s against 0.12 to 0.15 s) and buys snapshots, checksums and compression (60 MB of text for 1.79 MiB); **XFS costs about 1.5 times ext4** here (0.18 to 0.23 s).
- **File system overhead:** 300 MB images left 265 (ext4), 236 (XFS) and 300 (btrfs) of total, with 244, 217 and 219 MB **available**: **19 to 28 percent** is not yours before the first file.
- **A reflink or snapshot is free until written:** 0 extra MB for a 100 MB `cp --reflink` on XFS and btrfs, 100 MB on ext4; after 10 MB of changes, 10 MB extra (Problem E).
- **A full-copy backup of a sparse file** fills the disk with zeros (section 2.3: 1 GiB of file for 4 KiB of data).
- **Overlayfs copy-up** moves the **whole file** to the upper layer on the first write, even for one byte.
- **`e2fsck` on a big file system takes minutes to hours** and the file system is **offline** the whole time; XFS and ext4 journal replay at mount is quick (seconds). Plan maintenance windows from the size of the file system, not from the damage.
- **A hung unmount** (`target is busy`) costs a person; `fuser -vm` and `lsof +D` find the holder in seconds.

---

## 10. Common mistakes

1. **Growing a disk when it is an inode problem.** `df -h` has space, `df -i` is at 100 percent: delete files (or `mkfs -N`/`-i` for the next time), do not add space.
2. **Deleting a big log and expecting space.** The process still has it open (`lsof +L1`); truncate through `/proc/PID/fd/N` or restart the process, and configure rotation to signal the program.
3. **Copying a sparse file without `--sparse`-aware tools.** `cp --sparse=never`, `tar` without `-S`, a plain `scp` or `rsync` without `-S` write the holes as zeros.
4. **Writing to a mount point before the mount exists.** The data lands on the parent and is hidden later. Use `nofail` carefully, set `chattr +i` on the empty mount point directory, and have the service depend on the mount (`RequiresMountsFor=` in a unit).
5. **Using `/dev/sdX` in `fstab`.** Names change between boots. Use `UUID=`, test with `findmnt --verify` and `mount -a`.
6. **`fsck` on a mounted file system, or `-y` before looking.** Run `-n` first and **image the disk before repairing a damaged one**.
7. **`chmod -R 777` to "fix" a permission error.** Find which check fails: the mount, an `x` on a parent directory, the group, the ACL mask, an immutable flag, or SELinux. `namei -l /path` prints the mode of **every component** of a path.
8. **Trusting `%util` on SSDs** instead of `await` and `aqu-sz`.
9. **Benchmarking the page cache.** A `fio` buffered run reports memory speed. Use `--direct=1` or a file much larger than RAM, and a run long enough to pass burst effects.
10. **Treating RAID or a snapshot as a backup.** They share fate with the data they protect.
11. **Running a service as root only to bind port 80.** Give it `CAP_NET_BIND_SERVICE` (`AmbientCapabilities=`) or let a proxy bind and pass the socket.
12. **Forgetting that the ACL mask caps named entries**, and `chmod` rewrites the mask.

---


## 11. Questions out loud

Answer each in two minutes, aloud, before reading the answer.

1. **`df` says 100 percent and `du` of the mount says half. Where is the rest?** Four places, checked in this order: **deleted files still open** (`lsof +L1`, `ls -l /proc/*/fd | grep deleted`), **data hidden under a mount point** (bind mount `/` elsewhere and `du` there), **reserved blocks or metadata** (`tune2fs -l`, `btrfs filesystem df`), and **snapshots or clones** holding old blocks. Fix the first by restarting the process or truncating through `/proc/PID/fd/N`.
2. **`No space left on device` with `df -h` showing free space. Why?** Inodes (`df -i`), or on btrfs unallocated chunks for metadata, or a quota (that one says `Disk quota exceeded`), or a **per-directory limit** on some file systems. `df -i` first.
3. **What does `x` mean on a directory, and how can you open a file in a directory you cannot list?** `x` is the right to look up a name. With `x` and no `r` you cannot list but you can open `d/known` if you know the name. Deleting needs `w` on the directory, not the file, which is why `/tmp` has the sticky bit.
4. **A file has mode 640 and the user is in the group, but access is denied. List the causes.** The group is not in the process's credentials yet (the user must log in again, or the service needs `SupplementaryGroups=`), a **parent directory** lacks `x`, the **mount** is `ro` or `noexec`, an **ACL mask** caps the group, an **immutable** flag, **SELinux or AppArmor**, or the process runs in a **container with a different uid map**. `namei -l` shows each component.
5. **What does `nosuid` do, and why is it set on data mounts?** It makes the kernel ignore **setuid and setgid bits and file capabilities** on that mount, so an attacker who can put a root-owned executable on a data volume cannot use it to become root. Measured: `euid=0` became `euid=1001`.
6. **ext4, XFS or btrfs for a database server, a container host and a workstation?** Database: **XFS** (parallel writers, large files; ext4 is also fine). Container host: **XFS with `ftype=1` or ext4 under overlayfs**, or btrfs if you want snapshots of the host. Workstation: ext4, or btrfs for snapshots and compression. State the trade-off: btrfs cost about ten times ext4 on small files here and gave 0-cost snapshots and checksums.
7. **What is overlayfs copy-up, and why does it matter for containers?** The first write to a lower-layer file copies **the whole file** into the writable upper layer. Deleting a lower file adds a **whiteout** (a `0,0` character device) and frees nothing. So edit big files on a **volume**, and clean up in the same image layer that created the files.
8. **`iostat` shows `%util` 100 on an NVMe drive. Is it saturated?** Not necessarily: `%util` only says there was always a request in flight, and these devices serve many at once (here 30,373 IOPS at depth 1 and 335,436 at depth 32). Look at **`await` against the idle latency and `aqu-sz`**.
9. **How do you check and repair a damaged ext4, in order?** Unmount, **image the disk if the data matters**, `e2fsck -fn` and read it, then `e2fsck -fy`, rerun `-fn` until exit 0, look in `lost+found`. Exit status 4 means errors remain; the names of orphaned files are lost (`#43`).
10. **Why is a durable write so much slower than a normal one, and what do databases do about it?** A normal write goes into the page cache (262,398 IOPS buffered); `fsync` waits for the device (3,079 IOPS with a mean `fsync` of 319 µs here). Databases write a **sequential log**, **group commit** many transactions per `fsync`, and use devices with power-loss protected caches.
11. **How can a non-root service bind port 443?** Give the binary or the unit `CAP_NET_BIND_SERVICE` (`setcap`, `AmbientCapabilities=`), not setuid root. Measured: `setcap cap_net_bind_service=+ep` made bind(80) work for uid 1001, and `setcap -r` took it away.
12. **Why does a RAID 1 not replace backups, and what does LVM add?** RAID mirrors **mistakes and corruption** instantly. LVM adds **resizable volumes, spanning disks and block-level snapshots**, and none of that protects against deleting the data.

---

## 12. Problems

Do these before reading the answers. Each was run in a loop-mounted file system in this session; the answers section shows the output.

**Problem A.** A 32 MB ext4 file system made with `-N 2000` has `No space left on device` on `touch`, and `df -h` shows free blocks. Prove the cause, and find **which directory** holds the inodes (three directories: `mail`, `logs`, `data`, plus `lost+found`).

**Problem B.** A 25 MB log was deleted while a process had it open, and `du -sm` shows 1 MB but `df -m` shows 26 MB used. Find the process and the path to the file, **recover the contents** and prove they are intact with a checksum, then **release the space** without killing the process.

**Problem C.** Make a directory for group 2000 where members can **create and append to each other's files, cannot delete each other's files, and a user outside the group can read nothing**. User 1001 creates a file with `umask 077`; user 1002 (also in the group) must still be able to append to it. Show that 1002 cannot remove it and 1003 (not in the group) cannot read it.

**Problem D.** A 256 MB sparse disk image holds 1 MB of real data. Compare the size of `tar cf`, `tar cSf` and `gzip -c`, and show that extracting the sparse archive gives an identical file that still uses about 1 MB of disk.

**Problem E.** On XFS, make a **point-in-time copy** of a 200 MB file that takes almost no time and no space, show the blocks are shared, then rewrite 10 MB of the original and say how much extra space that cost and what the copy still contains.

---

## 13. Answers

### Problem A

```text
=== A: which directory ate the inodes?
$ df -i $M | tail -1
/dev/loop0       2000  2000     0  100% /mnt/lx07_a
$ for d in $M/*/; do echo "$(find "$d" | wc -l) $d"; done | sort -rn
1501 /mnt/lx07_a/mail/
41 /mnt/lx07_a/logs/
6 /mnt/lx07_a/data/
1 /mnt/lx07_a/lost+found/
```

`df -i` shows **2,000 of 2,000 inodes used, 100 percent**; the `find ... | wc -l` loop counts entries per directory and **`mail` holds 1,501** (1,500 files and the directory) against 41 and 6. The fix is to delete or archive the mail spool files (or, for a file system that must hold millions of small files, to make it with a lower bytes-per-inode ratio, `mkfs.ext4 -i 4096`, or to use XFS). Adding blocks (growing the device) would **not** have helped.

### Problem B

```text
=== B: find the deleted file that holds the space, read it, then release it
$ df -m /mnt/lx07_b | tail -1; du -sm /mnt/lx07_b
/dev/loop0            56    26        27  49% /mnt/lx07_b
1       /mnt/lx07_b
$ bash $D/deleted_open.sh | grep lx07
25 MB  pid 4707  python3  /proc/4707/fd/3  /mnt/lx07_b/app.log (deleted)
$ cp $FD /var/tmp/lx07_rescued.log; md5sum /var/tmp/lx07_rescued.log | cut -c1-32; echo "original was $md5"
952c29f18c847b741a481e13c5b2c215
original was 952c29f18c847b741a481e13c5b2c215
$ : > $FD; df -m /mnt/lx07_b | tail -1
/dev/loop0            56     1        52   1% /mnt/lx07_b
```

`du` shows 1 MB and `df` 26 MB used. The helper script `deleted_open.sh` walks `/proc/*/fd`, finds links ending in `(deleted)` on the mount, and printed **`25 MB pid 4707 python3 /proc/4707/fd/3 .../app.log (deleted)`**. `cp /proc/4707/fd/3 rescued.log` made a copy whose **md5 is identical** to the original's (`952c29f1...`). `: > /proc/4707/fd/3` truncated the open file and `df` fell to **1 MB used, 52 MB available**, while the process was still running. In production, prefer `lsof +L1` to find it, restart or signal the process properly, and fix the rotation.

### Problem C

```text
=== C: a shared team directory
1002 appended
rm: cannot remove '/mnt/lx07_c/proj/a.txt': Operation not permitted
$ ls -ld $M/proj; ls -l $M/proj; getfacl -p $M/proj/a.txt | grep -E "^(group|mask|other)"
drwxrws--T+ 2 ubuntu 2000 4096 Nov 16 20:08 /mnt/lx07_c/proj
total 4
-rw-rw----+ 1 1001 2000 18 Nov 16 20:08 a.txt
group::rwx      #effective:rw-
group:2000:rwx  #effective:rw-
mask::rw-
other::---
$ setpriv --reuid=1003 --regid=1003 --clear-groups cat $M/proj/a.txt 2>&1 | cut -c1-70
cat: /mnt/lx07_c/proj/a.txt: Permission denied
```

The recipe: directory owned by `ubuntu:2000` with mode **`3770`** (setgid for group inheritance, sticky so members cannot delete each other's files, `rwx` for owner and group, nothing for others), plus a **default ACL** `setfacl -d -m g:2000:rwx` so new files get a named group entry whatever the creator's `umask`. In the run, the file made by 1001 under `umask 077` is `rw-rw----+` with group 2000: the ACL mask is `rw-` and the group entries show `#effective:rw-`. **1002 appended successfully** (`1002 appended`), **`rm` gave `Operation not permitted`** (sticky bit, 1002 does not own the file), and **1003 got `Permission denied`**. (Without the default ACL, `umask 077` would have made the file `600` and 1002's append would have failed.)

### Problem D

```text
=== D: archive a sparse file: 256 MB apparent size, 1 MB of data
$ ls -ls $S/disk.img | cut -c1-60
1024 -rw-r--r-- 1 root root 268435456 Nov 16 20:08 /var/tmp/
$ ls -l $S/plain.tar $S/sparse.tar $S/gz.gz | awk "{print \$5, \$9}"
1308687 /var/tmp/lx07_sp/gz.gz
268441600 /var/tmp/lx07_sp/plain.tar
1054720 /var/tmp/lx07_sp/sparse.tar
$ du -k $S/out/disk.img; ls -l $S/out/disk.img | awk "{print \$5}"; cmp $S/disk.img $S/out/disk.img && echo identical
1024    /var/tmp/lx07_sp/out/disk.img
268435456
identical
```

The file is **256 MiB (268,435,456 bytes) of size and 1,024 KiB of disk** (`ls -ls` shows 1024 blocks of 1 KiB). `tar cf` wrote **268,441,600 bytes** (the zeros included), `tar cSf` wrote **1,054,720 bytes** (about 1 MB: only the data and the hole map), and `gzip -c` wrote **1,308,687 bytes**: gzip compresses the zeros away but the 1 MB of random data does not compress, and `gzip` also reads the whole 256 MB. `tar xSf` recreated a file with the **same apparent size, `du` 1,024 KB, and `cmp` says identical**. Use `-S` (`--sparse`) with `tar` and `rsync`, and `cp --sparse=always` for files with long runs of zeros that are not yet holes.

### Problem E

```text
=== E: a point-in-time copy that costs nothing until you write (XFS reflink)
copy took .0050 s; used before   227 MB, after   227 MB
$ filefrag -v $M/snap.bin | head -4 | cut -c1-100
Filesystem type is: 58465342
File size of /mnt/lx07_e/snap.bin is 209715200 (51200 blocks of 4096 bytes)
 ext:     logical_offset:        physical_offset: length:   expected: flags:
   0:        0..   25094:      25613..     50707:  25095:             shared
after rewriting 10 MB of the original: used   237 MB (extra 10 MB)
$ cmp $M/data.bin $M/snap.bin | head -1; cmp -n 5242880 $M/data.bin $M/snap.bin | head -1; cmp -i 10485760 -n 5242880 $M/data.bin $M/snap.bin && echo "bytes after 10 MB identical"
/mnt/lx07_e/data.bin /mnt/lx07_e/snap.bin differ: char 1, line 1
/mnt/lx07_e/data.bin /mnt/lx07_e/snap.bin differ: char 1, line 1
bytes after 10 MB identical
```

`cp --reflink=always data.bin snap.bin` took about **5 ms** and **used 0 extra MB** (227 MB before and after). `filefrag -v` shows the copy as one extent flagged **`shared`**: its blocks are the same blocks as the original's. After 10 MB of the original were rewritten, used space rose by **10 MB** (237 MB): copy on write allocated new blocks for the changed range only. `cmp` reports `differ: char 1` for the first 5 MB, because those bytes of the original changed, while the snapshot **still has the old content**, and the bytes **after 10 MB are identical**. Reflinks work on XFS (with `reflink=1`), btrfs and some others, **not on ext4** (section 4.3). They protect against a bad write, **not against losing the file system**.

---

## 14. Cheat sheet

```text
DISK FULL, DU DISAGREES
  df -h ; df -i                       blocks, then inodes
  du -xh --max-depth=1 /path | sort -h   what the names add up to (-x: one file system)
  lsof +L1                            deleted files still open (NLINK 0)
  : > /proc/PID/fd/N                  empty it without killing the process
  ls -ls ; du --apparent-size         allocated against apparent size (holes)
  mount --bind / /mnt/x ; du /mnt/x   look under a mount point

MOUNTS
  lsblk -o NAME,SIZE,FSTYPE,UUID,MOUNTPOINTS ; blkid
  findmnt [path] ; findmnt --verify ; /proc/self/mountinfo
  mount -o remount,ro|noexec|nosuid /m     change options live
  mount --bind a b ; mount -o remount,bind,ro b
  fstab: UUID= mountpoint type options dump pass   (nofail for non-critical)
  umount busy: fuser -vm /m ; lsof +D /m ; umount -l /m
  losetup -f --show file ; losetup -d /dev/loopN

FILE SYSTEMS
  ext4  fixed inodes, grow online, shrink offline     mkfs.ext4 [-N n | -i bytes]
  xfs   big files, parallel I/O, cannot shrink        xfs_repair (unmounted, log -L is last resort)
  btrfs snapshots, reflink, checksums, compression    btrfs filesystem df ; compsize
  tmpfs RAM, size=, gone at unmount ; overlayfs lower+upper, copy-up, whiteout (0,0)

REPAIR AND TUNING
  e2fsck -fn dev  then  e2fsck -fy dev      exit 0 ok, 1 fixed, 4 errors left
  tune2fs -l ; tune2fs -m 0 dev (data volumes) ; tune2fs -L name dev
  dumpe2fs -h dev ; debugfs ; resize2fs

PERMISSIONS
  r=4 w=2 x=1 ; owner, else group, else other (one triplet only)
  dir: r list, x reach name, w create/delete ; sticky t: only owner deletes
  umask: file 666, dir 777 minus mask (022 -> 644/755, 077 -> 600/700)
  setgid dir: group inherited ; setuid file: runs as owner ; nosuid: ignored
  getfacl/setfacl -m u:N:r f ; -d default ; mask caps named entries and group
  lsattr/chattr +i (immutable) +a (append) +C (no CoW)
  getfattr -d ; setfattr -n user.k -v v
  getcap/setcap cap_net_bind_service=+ep bin ; capsh --decode=HEX
  namei -l /path                      the mode of every component

BLOCK LAYER AND MEASURING
  cat /sys/block/DEV/queue/{rotational,scheduler,read_ahead_kb}
  iostat -x 1 : r_await w_await aqu-sz %util (misleads on SSD) rareq-sz
  fio --name=n --filename=f --size=1g --bs=4k --rw=randread --direct=1 --iodepth=32 --runtime=60 --time_based
  smartctl -a dev ; blktrace -d dev | blkparse -i -     (not run here)

NOT RUN HERE: quotas (edquota, repquota, xfs_quota), lvcreate/lvextend, mdadm, smartctl, blktrace
```

---

## 15. Sources

- Linux kernel documentation, **overlay filesystem**: lowerdir, upperdir, workdir, copy-up, whiteouts and opaque directories. <https://docs.kernel.org/filesystems/overlayfs.html>
- `capabilities(7)`: capability list, file capability sets, the bounding set. <https://man7.org/linux/man-pages/man7/capabilities.7.html>
- `acl(5)`: ACL entry types, the mask, default ACLs, the access check algorithm. <https://man7.org/linux/man-pages/man5/acl.5.html>
- `chattr(1)`: file attribute flags and the capabilities they need. <https://man7.org/linux/man-pages/man1/chattr.1.html>
- `mount(8)`: bind mounts, mount options, propagation. <https://man7.org/linux/man-pages/man8/mount.8.html>
- `e2fsck(8)`, `tune2fs(8)`, `xfs_repair(8)`, `iostat(1)`, `fio` documentation, `quota(1)`, `xfs_quota(8)`, `lvm(8)`, `mdadm(8)`, `smartctl(8)`, `blktrace(8)`: used from memory for exit codes, options and field meanings, as marked in the text.
- This plan: [OS 15 File systems](../05_operating_systems/15_file_systems.md) and [OS 16 Crash consistency and the page cache](../05_operating_systems/16_crash_consistency_and_the_page_cache.md) for the theory; [LX 05](05_performance_methodology_and_triage.md) for `iostat` and `fio` method; [LX 06](06_memory_swap_and_the_oom_killer.md) for page cache and `Shmem`.
- Measurements: the scripts `x1` to `x6` and `ans_prob` of this session, Linux 6.18.44, one VM, as stated in the honest limits.
