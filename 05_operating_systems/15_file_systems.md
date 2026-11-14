# OS 15: File systems, interface and implementation

*Part of the [Operating systems plan](00_plan.md) · Time: about 9 hours (6 reading and experiments, 3 problems) · You need: [OS 14](14_io_systems_and_disk_scheduling.md) (block devices, the page cache, request cost), [OS 11](11_virtual_memory.md) (the page cache is file-backed memory) and [OS 02](02_processes.md) (`fork` and the descriptor table)*

This is a new tutorial. A file system is the part of the operating system that most programs touch on every run and that interviewers use to test whether a candidate understands the machine below the API. The questions come in a fixed set. What is the difference between a file descriptor, an open file description and an inode, and what do `fork`, `dup` and a second `open` share? What does `unlink` do, and why can a deleted file still occupy space? How do hard and symbolic links differ, and what is a "fast" symlink? How does an inode name its data blocks (contiguous, linked, FAT, indexed, multi-level index, extents), and what is the largest file each can hold? How is free space tracked? How is a directory stored, and why does a directory of 50,000 files stay fast on ext4? What is a block group, and why does FFS (and ext4) place a file next to its directory? What is the VFS, how does a path become an inode (the dentry cache, negative dentries, mount points), and what happens at a mount point? And how many disk I/Os does `open` and `read` cost on a cold cache?

**Two honest limits, stated first.** (1) Everything on-disk is measured on **ext4**, because that is what this machine can build and inspect: it has `mkfs.ext4`, `dumpe2fs`, `debugfs` and `filefrag`, and it can loop-mount ext4 images. XFS and btrfs are in the kernel but their tools are missing, so they are described from sources, not measured. The classical designs (vsfs, FFS, FAT, Unix inodes) are shown with arithmetic and with ext4 running in modes that mimic them (block maps with no extents, no `flex_bg`, no hash-tree index). (2) Crash behaviour, journaling and `fsync` are **not** here: they are OS 16. This tutorial describes a file system that does not fail.

By the end you can:

- trace the three tables between a descriptor and a file (per-process descriptor table, system-wide open file description with its offset, inode), and say from the output what `open`, `dup`, `fork`, `O_APPEND` and `unlink` share (measured: two `open` calls on one file wrote `BBAA`; `dup` wrote `AAAABB`; after a child wrote 5 bytes the parent's offset was 5; an unlinked 64 MiB file kept all 16,384 blocks until the last `close`);
- explain hard links, symbolic links, `rename`, permissions, `umask`, the set-id and sticky bits, and the directory execute bit (measured: a short symlink used 0 blocks and a 199-byte one used one; a chain of 41 symlinks failed with `ELOOP`, so the limit is 40; `link` across a bind mount of the same disk failed with `EXDEV`);
- write the layout of an ext4 file system from `dumpe2fs` output and turn an inode number into a disk address by hand (measured: inode 12 is in group 0 at block 145, offset 0xb00; inode 8193 is in group 1 at block 657, offset 0);
- compute the largest file for each allocation method and the metadata each costs (computed: 4 TiB for a three-level block map with 4 KiB blocks; measured: the same four writes cost 10 blocks on a block map and 4 on extents, and a fifth extent forced a separate tree block);
- say how free space is tracked and what exhausts first (measured: an 8 MiB file system with 64 inodes refused file number 54 with 1,892 blocks free; a 32 MiB bitmap, the size for a 1 TiB disk, was scanned in about 2.03 ms);
- compare directory formats with a measurement (a linear ext4 directory took 162.6 / 160.4 µs to look up one name among 50,000; the hash-tree directory took about 3 to 7 µs; tmpfs about 1);
- explain FFS block groups and the ext4 placement rule, and show it (measured: without `flex_bg` each new top-level directory, its sub-directory and its files landed together in one group of their own; with `flex_bg` the top-level directories spread over groups 0 to 3 but everything inside them landed in group 0, because the unit of locality is a 16-group flex group);
- describe the VFS objects (superblock, inode, dentry, file) and the lookup walk, with the cost of a path (measured: about 19 ns per extra path component on a warm dentry cache; a warm `stat` of a missing name 0.2 µs once a negative dentry exists) and what a mount point does (measured: the same name shows `hidden_file` before a mount, nothing during, and the file again after; a `tmpfs` mount and unmount pair cost 34 µs);
- count the disk I/Os of file operations (measured: a cold `stat` of `/a/b/c/file` cost 5 disk reads, reading its data 1 more, creating a file about 10 writes).

**Where the numbers come from.** Programs and scripts were compiled and run with GCC 13.3 (`-O2 -Wall -Wextra`) and `mkfs.ext4` 1.47.0 on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory, as root. The root file system is ext4 on a virtio disk. The on-disk experiments loop-mount ext4 images (64 MiB to 2 GiB) created in `/tmp`, so the images sit on a file on the virtual disk and the loop device's I/O counters are used where disk I/Os are counted. The kernel's own views (`/proc/diskstats`, `/proc/self/fdinfo`, `/proc/self/mountinfo`, `/proc/sys/fs/*`, `debugfs`, `dumpe2fs`, `filefrag`) are read directly. The classical designs come from the OSTEP chapters on files and directories, file system implementation and FFS, and the kernel documents on the VFS, pathname lookup and the ext4 layout, extent tree, directory format and allocators (all fetched; see Sources for their limits). The Linux `flex_bg` placement rule, the symlink fast path, inline data capacity, the `stat` fields, XFS and btrfs designs and general facts marked "from memory" were not fetched.

**Data and timing.** The arithmetic (maximum file sizes, FAT and bitmap sizes, inode addresses) is exact. The block counts, extent counts and group numbers from `debugfs` and `dumpe2fs` are deterministic for these `mkfs` options (the group numbers of the classical placement test are random per run). The timings (path depth, lookups, mounts) are from this machine; where a program ran twice the results are shown as "first / second". The disk is virtual, so cold-cache times belong to this VM.

---

## 1. Files, descriptors and inodes

### 1.1 A file is an inode; a name is only a pointer to it

OSTEP's definition (fetched): a **file** is a linear array of bytes with a low-level name, the **inode number**; a **directory** is a list of (name, inode number) pairs; and directories nest into a tree rooted at `/`. The consequence that surprises people: **the name is not in the file.** The inode holds everything about the file except its name: the type and permission bits, the owner, the size, the timestamps, the link count and the map to the data blocks. `stat` returns the inode's fields:

```text
  a          inode 1278098  links 1  size 5    blocks(512B) 8   mode 0644 file
```

(`inode` is `st_ino`, `links` is `st_nlink`, `size` is `st_size`, `blocks(512B)` is `st_blocks`, in units of 512 bytes whatever the block size, and `mode` is the permission bits.) Because the inode does not know its name, one inode can have several names (section 2.1), and a file with no names can still exist (section 1.3).

### 1.2 Three tables between a descriptor and a file

`open` returns a **file descriptor**, a small integer that indexes a **per-process table**. Each entry points to an **open file description** (the kernel's `struct file`; OSTEP calls it an entry in the open file table), which records the access mode, the **current offset**, the status flags and a **reference count**, and points to the inode (through a dentry, section 8). Many descriptors can point to one description, and many descriptions to one inode:

```text
descriptor table (per process)   open file descriptions (system)        inode (one per file)
fd 3  ------------------------>  [offset 4, O_WRONLY]  --------+
fd 4  ------------------------>  [offset 2, O_WRONLY]  --------+--->  inode 1278098, size, blocks, links
fd 5 (dup of 4) --------------^
```

What each way of getting a descriptor shares, run:

```c
static long pos_of(int fd)                 /* the offset, as the kernel reports it */
{
    char p[64], line[128];
    long pos = -1;
    snprintf(p, sizeof p, "/proc/self/fdinfo/%d", fd);
    FILE *f = fopen(p, "r");
    while (f && fgets(line, sizeof line, f)) sscanf(line, "pos: %ld", &pos);
    if (f) fclose(f);
    return pos;
}
```

```text
1. two open() calls: two open file descriptions, two offsets
  fd 3 offset 4, fd 4 offset 2
  A wrote AAAA, B wrote BB:          file holds "BBAA"
2. dup(): two descriptors, one open file description, one offset
  fd 3 offset 6, fd 4 offset 6
  through the dup:                   file holds "AAAABB"
3. fork(): the child shares the parent's open file description
  parent wrote nothing, its offset is now 5
  parent continues after the child:  file holds "child+parent"
4. pread() and lseek()
  after pread at 3 the offset is still 10; lseek(a, 2, SEEK_SET) -> 2, with no disk I/O
5. O_APPEND: the offset is set to the end at every write
  without O_APPEND (b overwrites a): file holds "bbb"
  with O_APPEND:                     file holds "ababab"
```

- **Two `open` calls** give two descriptions with **independent offsets**: A wrote four bytes at offset 0, B wrote two at its own offset 0, and the file holds `BBAA`.
- **`dup`** gives a second descriptor for the **same description**: both report offset 6 and the two writes are sequential, `AAAABB`.
- **`fork`** copies the descriptor table, so parent and child share the descriptions. The child's 5-byte write moved the parent's offset to 5, and the parent continued after it: `child+parent`. This is why a shell can redirect a child's output into a file and a pipeline works.
- **`pread` and `pwrite`** name the offset and leave the description's offset alone, so they are safe for threads that share a descriptor. **`lseek` changes only the offset in memory and does no disk I/O**; the cost shows up at the next read or write.
- **`O_APPEND`** makes the kernel set the offset to the end of the file **at every write, atomically**. Without it two writers each with their own offset overwrite one another (`bbb`); with it the writes interleave as `ababab`. Appending to a shared log is exactly this case.

### 1.3 `unlink` removes a name, not a file

`unlink` removes a directory entry and decrements the inode's **link count**. The inode and its blocks are freed only when **the link count is zero and no open file description refers to it**. The program allocated 64 MiB, `unlink`ed the file while holding it open, and watched the free space:

```text
6. unlink() of an open file: the name goes, the inode and the data stay
  after unlink: st_nlink 0, st_size 64 MiB, /proc/self/fd/3 -> /tmp/f1_test.txt (deleted)
  free space change (4 KiB blocks): at unlink +0, after close +16384  (64 MiB is 16384 blocks)
```

After `unlink` the file has **zero names** (`st_nlink` 0) but its 64 MiB are intact and still accessible through the descriptor, and `/proc/self/fd/3` shows it as `(deleted)`. The free-block count did not move at `unlink` (+0) and rose by exactly 16,384 blocks, 64 MiB, at `close`. That explains the classic operations puzzle, "`df` says the disk is full but `du` cannot find the files": a process holds deleted files open (a rotated log that the daemon never reopened). `lsof +L1` or `ls -l /proc/PID/fd` finds them (from memory), and restarting or signalling the process frees the space. It is also the idiom for a temporary file that cleans itself up: create it, `unlink` it, keep the descriptor.

### 1.4 Descriptor numbers and limits

`open` returns the **lowest unused number**, which is why redirection works by closing a descriptor and opening a file (`close(0)` then `open` returns 0), and why a library that forgets to close descriptors shows up as an ever-growing number. The system has a per-process limit (`RLIMIT_NOFILE`) and a global one:

```text
8. the per-process limit and the system tables
  /proc/sys/fs/file-nr (open files, free, max): 146 0 818619
  RLIMIT_NOFILE soft 20000 hard 20000
  with the soft limit at 64: 61 more opens, then Too many open files
```

With the soft limit lowered to 64 the program opened 61 more files and then got `EMFILE` ("Too many open files"); three of the 64 were already in use (0, 1 and 2). The global figures are in `/proc/sys/fs/file-nr`: open file descriptions in use, free, and the maximum. A server that accepts connections will hit `EMFILE` long before it runs out of memory unless the limit is raised (here it is 20,000 for this session).

---
## 2. Links, rename and permissions

### 2.1 Hard links: more than one name for one inode

`link(old, new)` adds a directory entry that points at the same inode and raises the link count. There is no "original": both names are equal, and the data goes away only when the count reaches zero (section 1.3). The program makes `a`, links it to `b`, then removes `a`:

```c
    put("a", "hello");
    show("a");
    IGN(link(P("a"), P("b")));
    show("a"); show("b");
    unlink(P("a"));
    printf("  after unlink(a):\n");
    show("b");
```

```text
1. a hard link is another name for the same inode
  a          inode 1278098  links 1  size 5    blocks(512B) 8   mode 0644 file
  a          inode 1278098  links 2  size 5    blocks(512B) 8   mode 0644 file
  b          inode 1278098  links 2  size 5    blocks(512B) 8   mode 0644 file
  after unlink(a):
  b          inode 1278098  links 1  size 5    blocks(512B) 8   mode 0644 file
  b still reads "hello"
```

The inode number is the same under both names, `links` goes 1, 2, then 1, and `b` still reads `hello` after `a` is gone. Two limits, both from OSTEP (fetched) and both reproduced:

```text
2. what a hard link cannot do
  link() to a directory: Operation not permitted
  link() to /proc/version across file systems: Invalid cross-device link
```

A hard link to a **directory** is refused (`EPERM`), because it could create a cycle in the tree that `fsck` and directory walks cannot cope with. A hard link **across file systems** is refused (`EXDEV`), because an inode number is only meaningful inside one file system's inode table; `/proc/version` is on `proc`. The link count of a directory is special: an empty directory has 2 (its own entry in its parent and its own `.`), and each sub-directory adds 1 (its `..`). The `t` directory in section 2.4 shows `links 2`.

### 2.2 Symbolic links: a small file that holds a path

A **symbolic link** is its own inode, of type "symlink", whose data is a path string. Resolving it means reading that string and starting a lookup from it (section 8.2). So it can cross file systems, name a directory, and **dangle** (point at nothing), none of which a hard link can do:

```c
    IGN(symlink("b", P("s_short")));
    char longname[200];
    memset(longname, 'x', sizeof longname - 1); longname[sizeof longname - 1] = 0;
    IGN(symlink(longname, P("s_long")));
    show("s_short"); show("s_long");
    IGN(symlink("missing", P("s_dangle")));
    printf("  stat(s_dangle): %s;  lstat works: ", stat(P("s_dangle"), &(struct stat){0}) ? strerror(errno) : "ok");
    printf("%s\n", lstat(P("s_dangle"), &(struct stat){0}) ? "no" : "yes");
    IGN(symlink("loop2", P("loop1"))); IGN(symlink("loop1", P("loop2")));
    printf("  open() of a symlink loop: %s\n", open(P("loop1"), O_RDONLY) < 0 ? strerror(errno) : "worked");
```

```text
3. a symbolic link is a file that holds a path
  s_short    inode 1278100  links 1  size 1    blocks(512B) 0   mode 0777 symlink
  s_long     inode 1278101  links 1  size 199  blocks(512B) 8   mode 0777 symlink
  stat(s_dangle): No such file or directory;  lstat works: yes
  open() of a symlink loop: Too many levels of symbolic links
```

- `s_short` points at `b` (1 byte) and has **zero blocks**; `s_long` points at a 199-byte path and has one 4 KiB block (8 sectors of 512 bytes). That is the **fast symlink**: ext4 stores a short target inside the inode itself (in the 60 bytes of the `i_block` area, from memory: the fetched ext4 pages describe that area as the block map, extent root or inline data) and spends a block only when the target is too long. `lstat` reports the link, `stat` follows it and fails with `ENOENT` on the dangling one.
- A loop (`loop1` to `loop2` to `loop1`) ends with `ELOOP`, because the kernel counts symlinks followed during one lookup and stops at a limit. The loop that builds a chain shows the limit exactly:

```text
  a chain of 41 symlinks: Too many levels of symbolic links
  the longest chain that resolved had 40 symlinks
```

A chain of 41 symlinks fails and a chain of 40 resolves, so the kernel's limit is **40 links per lookup** (the path-lookup document, fetched, says 40 and `ELOOP`). The check is on the **lookup**, not on the file: nothing is wrong with the individual links.

| | Hard link | Symbolic link |
| --- | --- | --- |
| What it is | another directory entry for the same inode | a separate inode that holds a path |
| Across file systems | no (`EXDEV`) | yes |
| To a directory | no (`EPERM`) | yes |
| Dangles | cannot (the inode exists) | yes |
| Delete the target | data survives while another name remains | the link now dangles |
| Cost | none (one directory entry) | an inode, and a block if the path is 60 bytes or more (from memory for the threshold) |

### 2.3 `rename` is the atomic replace

`rename(old, new)` moves a name, and if `new` exists it is replaced **in one step**: at no moment does a reader see neither file or both (OSTEP, fetched). The program shows the replace and the boundary:

```c
    put("old", "OLD"); put("new", "NEW");
    IGN(rename(P("new"), P("old")));
    fd = open(P("old"), O_RDONLY); memset(buf, 0, sizeof buf); IGN(read(fd, buf, 15)); close(fd);
    printf("  rename(new, old): old now reads \"%s\", new: %s\n", buf, lstat(P("new"), &(struct stat){0}) ? "gone" : "exists");
    printf("  rename() across file systems: %s\n", rename(P("old"), "/dev/shm/f2_moved") ? strerror(errno) : "worked");
```

```text
4. rename() replaces the target in one step
  rename(new, old): old now reads "NEW", new: gone
  rename() across file systems: Invalid cross-device link
```

`old` now reads `NEW` and `new` is gone; and `rename` across file systems fails with `EXDEV`, because it only edits directory entries inside one file system. The standard pattern for updating a file so that readers never see a half-written one is: write a temporary file in the **same directory** (same file system), flush it, `rename` it over the target. Whether the flush makes it durable across a crash is OS 16. The bind-mount result in section 8.4 is the same rule seen from the mount side.

### 2.4 Permissions: nine bits, three more, and the directory rules

The mode is 12 bits in the inode: **read, write, execute for owner, group and other** (nine bits), and three special bits. The kernel picks **one** class by comparing the caller's user and group with the inode's: if you are the owner, only the owner bits count (a file mode `0070` locks its owner out of what the group may do, from memory). Root bypasses the checks through the `CAP_DAC_OVERRIDE` capability, which is why the test below drops root first.

```c
    put("p", "x");
    chmod(P("p"), 0640);
    show("p");
    chmod(P("p"), 02755 | 04000);
    show("p");
    mkdir(P("t"), 01777);
    chmod(P("t"), 01777);
    show("t");
    mode_t old = umask(077);
    put("masked", "x");
    umask(old);
    show("masked");
    printf("  umask 077 turned the requested 0644 into 0600\n");
```

```text
5. permissions
  p          inode 1278147  links 1  size 1    blocks(512B) 8   mode 0640 file
  p          inode 1278147  links 1  size 1    blocks(512B) 8   mode 6755 file
  t          inode 1278149  links 2  size 4096 blocks(512B) 8   mode 1777 directory
  masked     inode 1278150  links 1  size 1    blocks(512B) 8   mode 0600 file
  umask 077 turned the requested 0644 into 0600
  directory x bit: mode 0644 -> stat of nx/inside: Permission denied; access(nx, R_OK): readable
```

The last test drops root in a child so that the mode bits apply:

```c
    mkdir(P("nx"), 0755); put("nx/inside", "x");
    chmod(P("nx"), 0644);
    chmod(P("."), 0755);
    fflush(stdout);
    if (fork() == 0) {                                         /* drop root so the mode bits apply */
        IGN(setgid(65534)); IGN(setuid(65534));
        printf("mode 0644 -> stat of nx/inside: %s; ", stat(P("nx/inside"), &(struct stat){0}) ? strerror(errno) : "ok");
        printf("access(nx, R_OK): %s\n", access(P("nx"), R_OK) ? strerror(errno) : "readable");
        fflush(stdout);
        _exit(0);
    }
```

- **`0640`**: owner read and write, group read, others nothing.
- **`6755`** is `setuid` (4000) and `setgid` (2000) plus `0755`: an executable runs with the file owner's (or group's) identity, which is how `passwd` can write the password file. On a **directory** `setgid` makes new files inherit the directory's group (from memory).
- **`1777` on a directory is the sticky bit**: anyone can create files in it, but a file can be removed or renamed only by its owner, the directory's owner or root. `/tmp` is the example.
- **`umask`** removes bits from the mode a program asks for: asking for `0644` under `umask 077` produced `0600`. It is a process attribute inherited through `fork`, which is why a shell setting changes what every command creates.

The directory bits mean something different from the file bits, and this is the part that surprises:

| Bit | On a file | On a directory |
| --- | --- | --- |
| `r` | read the data | **list the names** |
| `w` | write the data | **create, delete or rename entries** (needs `x` as well) |
| `x` | execute it | **search**: use the directory as a step in a path |

The last line of the output ties it down (the nobody user in the child): with the directory at mode `0644` (no `x`) a non-root user gets `Permission denied` for `nx/inside`, even though `access(nx, R_OK)` says the directory itself is readable. Without `x` you can list the names (`r`) but you cannot resolve one of them, and **every directory on a path needs `x`**. Deleting a file needs write permission on its **directory**, not on the file, because `unlink` edits the directory. The chunk of the lookup that checks these bits is section 8.2.

---

## 3. On-disk structure: from vsfs to ext4

### 3.1 The very simple file system, and the inode address formula

OSTEP's **vsfs** (fetched) is the smallest design that shows everything. The disk is an array of blocks and is divided into regions, in this order: a **superblock** (the file system's own parameters: how many inodes and data blocks, where the inode table starts, a magic number), an **inode bitmap**, a **data bitmap** (one bit per inode and per data block, 1 meaning in use), the **inode table** (an array of fixed-size inodes) and the **data region**. A chapter example has 64 blocks of 4 KB, 256-byte inodes and 5 blocks of inode table, so 16 inodes fit in a block and 80 in the table.

An inode number is an **index into that array**, so the inode's address is arithmetic and needs no search. OSTEP's formula (fetched):

```text
blk    = (inumber * sizeof(inode)) / blockSize
sector = ((blk * blockSize) + inodeStartAddr) / sectorSize
```

The inode itself holds the metadata (type, size, owner, permissions, times, link count) and **pointers to the data blocks** (section 4). A directory is a file whose data is a list of (name, inode number) entries (section 6). To read `/foo/bar` the file system needs the root inode (a known number, 2 on ext4), then the root's data (to find `foo`), then `foo`'s inode and data, then `bar`'s inode. That is the I/O count of section 8.5.

### 3.2 ext4: the same idea, repeated per block group

A real disk is too big for one flat array of bitmaps with one inode table: the inodes would be far from the data they describe, and one bitmap would be huge. ext4 (and its ancestors FFS and ext2) divides the disk into **block groups**, each a miniature vsfs. The size of a group is not arbitrary: one block bitmap is **one block**, one block has 8 bits per byte, so a group holds `8 * block size` blocks, which is 32,768 blocks, **128 MiB at 4 KiB** (the ext4 layout document, fetched). The program builds a 1 GiB file system with `mkfs.ext4` defaults and reads it back with `dumpe2fs`:

```text
Filesystem features:      has_journal ext_attr resize_inode dir_index filetype extent 64bit flex_bg sparse_super large_file huge_file dir_nlink extra_isize metadata_csum
Inode count:              65536
Block count:              262144
Block size:               4096
Reserved GDT blocks:      127
Blocks per group:         32768
Inodes per group:         8192
Flex block group size:    16
Inode size:           256
groups: 8
Group 0: (Blocks 0-32767) csum 0x0611 [ITABLE_ZEROED]
  Primary superblock at 0, Group descriptors at 1-1
  Block bitmap at 129 (+129), csum 0x8e765e0b
  Inode bitmap at 137 (+137), csum 0x8f6ff5c6
  Inode table at 145-656 (+145)
  28521 free blocks, 8181 free inodes, 2 directories, 8181 unused inodes
Group 1: (Blocks 32768-65535) csum 0x32d7 [INODE_UNINIT, BLOCK_UNINIT, ITABLE_ZEROED]
  Backup superblock at 32768, Group descriptors at 32769-32769
  Block bitmap at 130 (bg #0 + 130), csum 0x00000000
  Inode bitmap at 138 (bg #0 + 138), csum 0x00000000
  Inode table at 657-1168 (bg #0 + 657)
  32639 free blocks, 8192 free inodes, 0 directories, 8192 unused inodes
```

Read it as follows.

- **Counts.** 262,144 blocks of 4,096 bytes is 1 GiB, in 8 groups of 32,768. 65,536 inodes, 8,192 per group, each 256 bytes. The default of **one inode per 16 KiB of disk** (1 GiB / 65,536) comes from `mkfs`'s `inode_ratio` (from memory); the number of inodes is fixed at creation, which is section 5.2.
- **Per group.** Group 0 begins with the primary superblock (block 0, plus 1 block of group descriptors at block 1). **Group 1 holds a backup of both** (blocks 32,768 and 32,769): `sparse_super` keeps backups only in some groups (0, 1 and powers of 3, 5 and 7; from memory) so that the cost of redundancy stays small. The reserved descriptor blocks (127 here, for growing the file system online with `resize_inode`) follow the descriptor. The check on the group 1 line: 32,768 blocks minus the backup superblock, the descriptor and the 127 reserved blocks is 32,768 - 129 = **32,639**, exactly the free-block count shown.
- **The surprise: group 1's bitmaps are in group 0.** Its block bitmap is at block 130 and its inode table at 657-1,168, which is inside group 0's range (0 to 32,767). That is the **`flex_bg`** feature: the metadata of a group of groups (here a flex group of up to 16, and this file system has only 8) is packed together: all the block bitmaps (129 to 136), then all the inode bitmaps (137 to 144), then all the inode tables (145 onward). The reason (from memory of the design, and the document confirms the packing) is that metadata read together is read from nearby blocks, and a large file can use a run of free blocks that would otherwise be cut by bitmaps. It matters for placement, section 7.
- **Lazy initialisation.** Group 1 is flagged `INODE_UNINIT` and `BLOCK_UNINIT`: its bitmaps are not yet written and its inode table was not initialised at `mkfs` time (from memory of `uninit_bg`/`metadata_csum`), so `mkfs` finishes quickly on a huge disk.
- **Features.** `extent` (section 4.4), `dir_index` (section 6.3), `flex_bg`, `has_journal` (OS 16), `64bit`, `metadata_csum` (checksums on metadata) and `huge_file` and `large_file` (file size beyond 2 GiB and 2 TiB).

Inode table size check: 8,192 inodes of 256 bytes is 2 MiB, which is 512 blocks, and group 0's table is blocks 145 to 656, **512 blocks**.

### 3.3 From an inode number to a disk address, by hand

The inode number gives the group, the index inside the group's table, and from the index the block and the offset. ext4 numbers inodes from 1, so the index is `(inumber - 1)`:

```text
group  = (inumber - 1) / inodes_per_group
index  = (inumber - 1) % inodes_per_group
block  = inode_table_start(group) + (index * inode_size) / block_size
offset = (index * inode_size) % block_size
```

`debugfs imap` does the same calculation, so the two can be compared. The program asked for five inodes:

```text
Inode 2 is part of block group 0
  located at block 145, offset 0x0100
Inode 12 is part of block group 0
  located at block 145, offset 0x0b00
Inode 8192 is part of block group 0
  located at block 656, offset 0x0f00
Inode 8193 is part of block group 1
  located at block 657, offset 0x0000
Inode 16385 is part of block group 2
  located at block 1169, offset 0x0000
```

Verify each by hand (256-byte inodes, 16 to a block, group 0's table starting at 145):

| Inode | Index | Block | Offset | Check |
| --- | --- | --- | --- | --- |
| 2 (the root) | 1 | 145 + 0 | 1 * 256 = 0x100 | matches |
| 12 (first file or directory made after `lost+found`) | 11 | 145 + 0 | 11 * 256 = 2,816 = 0xb00 | matches |
| 8192 (last of group 0) | 8,191 | 145 + 8,191 / 16 = 145 + 511 = 656 | 15 * 256 = 3,840 = 0xf00 | matches |
| 8193 (first of group 1) | 0 (group 1) | 657 | 0 | matches |
| 16385 (first of group 2) | 0 (group 2) | 1,169 (group 2's table follows group 1's: 657 + 512) | 0 | matches |

Inodes 3 to 10 are reserved (the journal is inode 8 and `lost+found` is inode 11; from memory), which is why the first file you create gets inode 12. **Finding an inode costs one arithmetic step and one block read** (a hit in the buffer cache after the first), with no search. Sixteen inodes share a 4 KiB block, so reading one inode brings its fifteen neighbours into the cache, and a directory's files, if created together, get consecutive inodes: the cold `stat` of section 8.5 reads inodes 2, 12, 13, 14 and 15 with one block read for that reason.

---
## 4. Allocation methods: how an inode names its blocks

A file is a sequence of **file blocks** 0, 1, 2, and so on; the disk holds **disk blocks** at addresses. The allocation method is the data structure that maps one to the other. Every design is judged on four things: how many disk reads it takes to reach file block `k` (random access), how well sequential access works (does the file sit in one place), how much metadata it costs, and how large a file it can address. The five classical methods are compared here on the same arithmetic, and the last two are run on ext4.

### 4.1 Contiguous allocation

Store the file as one run of adjacent blocks; the inode holds the start and the length. Reaching file block `k` is `start + k`: **one read, any `k`, and sequential reads are as fast as the disk can go** (OS 14: seek and rotation dominate, so one run is the best case). It is the model of CD-ROM file systems and of the old IBM systems (from memory). Its failure is the same as `malloc`'s: **external fragmentation** and the need to know the size in advance. The program makes the problem visible. A disk of 8,192 units is filled with files of 1 to 64 units, and random files are freed whenever it is more than 90 percent full, so the holes accumulate; three policies choose the hole for a new file:

```c
static int find(int len, int policy)                   /* 0 first fit, 1 best fit, 2 worst fit */
{
    int best = -1, best_len = policy == 1 ? N + 1 : -1;
    for (int i = 0; i < N;) {
        if (used[i]) { i++; continue; }
        int j = i;
        while (j < N && !used[j]) j++;
        int run = j - i;
        if (run >= len) {
            if (policy == 0) return i;
            if (policy == 1 && run < best_len) { best = i; best_len = run; }
            if (policy == 2 && run > best_len) { best = i; best_len = run; }
        }
        i = j;
    }
    return best;
}
```

```text
contiguous allocation, 8192 units, files of 1 to 64 units, freed at random above 90% full
  first fit  files  472  holes  188 (0.40 per file)  free 10.1%  in holes under 16 units 93.8% of the free space  largest hole   18  failed requests  52.6%
  best fit   files  243  holes   80 (0.33 per file)  free  9.7%  in holes under 16 units 33.0% of the free space  largest hole  122  failed requests   3.3%
  worst fit  files 2995  holes  797 (0.27 per file)  free 10.0%  in holes under 16 units 100.0% of the free space  largest hole    3  failed requests  94.7%
```

- **First fit** (take the first hole that fits) ends with 188 holes, **93.8 percent of the free space in holes under 16 units**, and refuses 52.6 percent of requests: the disk is 10 percent free and mostly unusable by a file of any size.
- **Best fit** (the smallest hole that fits) leaves the free space in fewer, larger holes (the largest is 122 units) and fails only 3.3 percent.
- **Worst fit** (the largest hole) is the worst: it carves every large hole into pieces and the largest hole ends at 3 units.

A caveat on reading this: a request that cannot be placed is **dropped**, so the three runs hold different files at the end (472, 243 and 2,995 of them: the policy that fails big requests fills up with small files). The comparison of hole counts is therefore not a clean one; the clean result is the failure rate and the largest hole. Compaction (copying everything together) fixes fragmentation and costs reading and writing the whole disk. Contiguous allocation survives where files are written once with a known size (a media file system, an archive) and in a weaker form inside **extents** (section 4.4).

### 4.2 Linked allocation and the FAT

Store each file as a **linked list of blocks**: every block holds the number of the next one, and the inode holds the first. There is no external fragmentation and a file can grow anywhere. But to reach file block `k` you must read blocks `0` to `k-1` to learn where the next one is, so **block `k` costs `k + 1` reads**, the pointer eats part of every block (a block holds less than a power of two of data), and one damaged block loses the rest of the file. The column is in the table of section 4.3.

The **FAT** (file allocation table; the file system of MS-DOS and of USB sticks and cameras, from memory) keeps the idea and moves the pointers **out of the data blocks into one table**: entry `i` of the table is the next cluster after cluster `i`. The table is read into memory, so following the chain costs no disk reads: **one read per data block, for any `k`** (the cost, column "FAT (in RAM)" below, assuming the table is cached). The price is memory and the table's size, which scales with the **disk**, not with the file in use:

```text
FAT in memory: one 4-byte entry per cluster
  disk 32 GiB  cluster  4 KiB:      8388608 entries, FAT     32.0 MiB; bitmap    1.00 MiB
  disk 1 TiB   cluster  4 KiB:    268435456 entries, FAT   1024.0 MiB; bitmap   32.00 MiB
  disk 16 TiB  cluster  4 KiB:   4294967296 entries, FAT  16384.0 MiB; bitmap  512.00 MiB
  disk 32 GiB  cluster 32 KiB:      1048576 entries, FAT      4.0 MiB; bitmap    0.12 MiB
  disk 1 TiB   cluster 32 KiB:     33554432 entries, FAT    128.0 MiB; bitmap    4.00 MiB
  disk 16 TiB  cluster 32 KiB:    536870912 entries, FAT   2048.0 MiB; bitmap   64.00 MiB
```

For a 1 TiB disk with 4 KiB clusters the FAT is **1,024 MiB** of RAM (268,435,456 entries of 4 bytes), and 16,384 MiB at 16 TiB, which is why FAT file systems use larger clusters on bigger disks, wasting space in the tail of small files (the block-size trade of section 4.5). A 32 KiB cluster cuts the 1 TiB figure to 128 MiB. A FAT also has no place for permissions, owners or links, which is why it is not used as a Unix root file system, and it keeps **two copies** of the table for redundancy (from memory). Random access is fast enough; the weakness is the table's size, and metadata that is one global structure with one chain per file.

### 4.3 Indexed allocation, and why the Unix inode is multi-level

Give each file an **index**: an array of block pointers, so block `k` is `index[k]`, **one read of the index (if it is in memory, none) plus one of the data**. The index needs a size that fits every file, and that is the whole difficulty: a one-block file should not carry a 1,000-entry index, and a 1 GiB file needs 262,144 entries.

The Unix answer, which ext2 and ext3 kept, is a **multi-level index** inside a fixed-size inode (OSTEP, fetched, describes it): the inode holds **12 direct pointers** to data blocks, then one **single indirect** pointer to a block full of pointers, one **double indirect** pointer to a block of pointers to blocks of pointers, and one **triple indirect**. Small files, which are most files, use only the direct pointers and need no extra block. With a 4 KiB block and 4-byte pointers a pointer block holds **1,024** pointers. The capacity at each level:

```c
static void max_file_size(void)
{
    printf("block-pointer inode (12 direct pointers, 4-byte block numbers)\n");
    printf("%6s %10s %12s %14s %16s %18s\n", "block", "ptrs/blk", "direct", "+ single", "+ double", "+ triple");
    for (int bs = 1024; bs <= 8192; bs *= 2) {
        double p = bs / 4.0, b = bs;
        double d = 12 * b, s = d + p * b, dd = s + p * p * b, t = dd + p * p * p * b;
        printf("%5dK %10.0f %9.0f KiB %11.1f MiB %13.2f GiB %15.2f TiB\n", bs / 1024, p, d / 1024, s / 1048576,
               dd / 1073741824, t / 1099511627776);
    }
}
```

```text
block-pointer inode (12 direct pointers, 4-byte block numbers)
 block   ptrs/blk       direct       + single         + double           + triple
    1K        256        12 KiB         0.3 MiB          0.06 GiB            0.02 TiB
    2K        512        24 KiB         1.0 MiB          0.50 GiB            0.25 TiB
    4K       1024        48 KiB         4.0 MiB          4.00 GiB            4.00 TiB
    8K       2048        96 KiB        16.1 MiB         32.02 GiB           64.03 TiB
```

For the 4 KiB row: the 12 direct pointers cover `12 * 4 KiB =` **48 KiB**; the single indirect adds `1,024 * 4 KiB =` **4 MiB**; the double indirect adds `1,024 * 1,024 * 4 KiB =` **4 GiB**; the triple indirect adds `1,024^3 * 4 KiB =` **4 TiB** (the columns are cumulative). With a 1 KiB block the pointer count is 256 and a triple-indirect file tops out at about 16 GiB, which is why block size and file size limits were once linked (the 1 KiB row prints the double column as 0.06 GiB, which is 64 MiB, and the triple as 0.02 TiB, about 16 GiB). The kernel's ext4 document gives the same ranges for ext4's block map as file block numbers: **12 to 1,035** are single indirect, **1,036 to 1,049,611** double, and **1,049,612 to 1,074,791,436** triple (fetched; they are the cumulative counts `12`, `12 + 1,024`, `12 + 1,024 + 1,024^2`, and so on). Maximum file size on the real ext4 is a function of other limits too (the 32-bit block count of the block map, the extent length); the figures here are the **index arithmetic**, nothing more.

The cost to reach file block `k`, with the inode in memory and nothing else cached, from the program's second function:

```c
static void read_cost(void)
{
    long long p = 1024;
    long long ks[] = { 0, 11, 12, 1035, 1036, 1049611, 1049612 };
    printf("\ndisk reads to fetch file block k, 4 KiB blocks, inode already in memory, nothing else cached\n");
    printf("%10s %11s %8s %13s %16s\n", "k", "contiguous", "linked", "FAT (in RAM)", "multi-level idx");
    for (int i = 0; i < 7; i++) {
        long long k = ks[i];
        int idx = k < 12 ? 0 : k < 12 + p ? 1 : k < 12 + p + p * p ? 2 : 3;
        printf("%10lld %11d %8lld %13d %16d\n", k, 1, k + 1, 1, 1 + idx);
    }
}
```

```text
disk reads to fetch file block k, 4 KiB blocks, inode already in memory, nothing else cached
         k  contiguous   linked  FAT (in RAM)  multi-level idx
         0           1        1             1                1
        11           1       12             1                1
        12           1       13             1                2
      1035           1     1036             1                2
      1036           1     1037             1                3
   1049611           1  1049612             1                3
   1049612           1  1049613             1                4
```

Read the columns as the argument for each design. **Contiguous**: 1 read always. **Linked**: block 11 costs 12 reads, block 1,036 costs 1,037; this is why nobody uses it for files that are read at random. **FAT**: 1 read, with the table in RAM. **Multi-level index**: 1 read for a block in the first 48 KiB, 2 for the next 4 MiB, 3 up to 4 GiB and 4 beyond, which is one extra read per level and, in practice, **fewer** because the index blocks are cached and a sequential reader reuses the same single-indirect block for a thousand data blocks. The design bets that most files are small, and wins: the common case costs nothing extra.

**Measured on ext4 with a block map.** `mkfs.ext4 -O ^extent,^64bit,^flex_bg,^has_journal` builds a file system that uses the old pointer scheme. The program writes **one byte** at the first byte of each region and reports the blocks the file system allocated after each write (the offsets are file block 0, 12, 1,036 and 1,075,050):

```c
    for (int i = 2; i < argc; i++) {
        off_t off = strtoll(argv[i], NULL, 10);
        IGN(pwrite(fd, "x", 1, off));
        fsync(fd);
        struct stat st;
        fstat(fd, &st);
        long blocks = st.st_blocks / 8;                      /* 4 KiB blocks */
        printf("  byte at offset %-14lld total blocks %-3ld (+%ld)\n", (long long)off, blocks, blocks - prev);
        prev = blocks;
    }
```

```text
  byte at offset 0              total blocks 1   (+1)
  byte at offset 49152          total blocks 3   (+2)
  byte at offset 4243456        total blocks 6   (+3)
  byte at offset 4403404800     total blocks 10  (+4)
inode of that file, from debugfs:
BLOCKS:
(0):2073, (IND):2585, (12):2074, (DIND):2586, (IND):2587, (1036):3084, (TIND):2588, (DIND):2589, (IND):2590, (1075050):5994
TOTAL: 10
```

One byte in the direct region costs **1 block**; the first byte in the single-indirect region costs **2** (the data block and the indirect block); the first in the double-indirect region costs **3** (a double-indirect block, an indirect block and the data); and the first in the triple region costs **4**. `debugfs` shows the tree the file system built:

```text
(0):2073, (IND):2585, (12):2074, (DIND):2586, (IND):2587, (1036):3084, (TIND):2588, (DIND):2589, (IND):2590, (1075050):5994
```

Each `IND`, `DIND` and `TIND` is a pointer block, and the numbers after the colon are disk addresses. The file's logical size is about 4.1 GiB; its disk use is 10 blocks, or 40 KiB, because the file is **sparse**: blocks never written have no pointer and are not stored, and reads of them return zeros. The logical size and the allocated size are different numbers (`st_size` against `st_blocks`), which is why a sparse VM image can be a few hundred MB on disk and report 20 GB. The metadata cost of the block map is the reason it was replaced.

### 4.4 Extents

An **extent** is a contiguous run recorded as three numbers: the first file block, the length, and the first disk block. A file allocated contiguously, which is the normal result when the allocator can find room, needs **one entry for the whole file** instead of one pointer per block, and a 1 GiB file costs the same as a 4 KiB one. ext4 inodes hold the **root of an extent tree** in the same 60 bytes that the block map used: a 12-byte header and room for **4 extents** of 12 bytes each; one extent covers up to **32,768 blocks, 128 MiB at 4 KiB**; and when a file needs more than four, the tree grows a level, with the leaves in separate blocks and a depth of up to 5 (all from the fetched ext4 extent-tree document). Compare the block map's four writes with the extent file's:

```text
  byte at offset 0              total blocks 1   (+1)
  byte at offset 49152          total blocks 2   (+1)
  byte at offset 4243456        total blocks 3   (+1)
  byte at offset 4403404800     total blocks 4   (+1)
EXTENTS:
(0):4139, (12):4140, (1036):4663, (1075050):36714
```

The same four writes cost **1 block each**: no pointer blocks at all, because four extents fit in the inode. `debugfs` lists `(0):4139, (12):4140, (1036):4663, (1075050):36714` as (file block):disk block, each a one-block extent. Now the fifth extent:

```text
  byte at offset 0              total blocks 1   (+1)
  byte at offset 40960          total blocks 2   (+1)
  byte at offset 81920          total blocks 3   (+1)
  byte at offset 122880         total blocks 4   (+1)
  byte at offset 163840         total blocks 6   (+2)
  byte at offset 204800         total blocks 7   (+1)
EXTENTS:
(ETB0):32801, (0):4141, (10):4142, (20):4673, (30):4683, (40):36680, (50):36690
```

The first four bytes cost one block each. The **fifth separate extent costs two blocks** (+2): the file system had to add a tree block (shown as `ETB0`, an extent tree block, holding the extents) and move the entries into it, so the inode now points at a leaf; the sixth costs one again. That is the tree growing one level, measured. The structure is the **B-tree** idea again: a shallow tree with a wide root keeps every lookup to a few levels.

The case extents are built for, and where they win, is the **contiguous file**:

```text
solo: 1 extent found
x1: 17 extents found
x2: 17 extents found
```

An 8 MiB file written in one go is **1 extent** (it would cost 2,048 pointers and an indirect block on a block map). The other two lines are the cautionary case: two files each grown by one block at a time, **alternately**, with `fsync` after every block (the allocator is forced to commit block placement for one file before the other grows), ended with **17 extents each** for 64 blocks. Each file is split into pieces by the other. (Why 17 and not 64: some blocks landed adjacent and merged; I did not isolate the cause beyond that.) This is the **fragmentation of an extent file system**, and it is mild here because ext4 has two features that fight it, both from the fetched allocator document: **delayed allocation** (blocks are chosen at write-back, when the file system knows how much was written, not at `write`, so a file written in one burst is placed in one run) and the **multiblock allocator** (`mballoc`), which allocates in larger runs and **preallocates speculatively** (the document says 8 KiB for a new file) so that a growing file finds room next to its tail. `filefrag` shows a file's extent count, and `e4defrag` rewrites fragmented files into fewer extents (the document names it).

**Inline data** goes the other way, for **tiny** files: with the `inline_data` feature a file smaller than the inode's spare room stores its bytes **in the inode** and uses no data block at all (the document: the first 60 bytes live in the `i_block` area). A test on a file system made with `-O inline_data,^has_journal` and 256-byte inodes wrote files of increasing size and read `st_blocks`:

```text
file of 10 bytes: 1 512-byte blocks allocated
file of 60 bytes: 1 512-byte blocks allocated
file of 100 bytes: 1 512-byte blocks allocated
file of 150 bytes: 8 512-byte blocks allocated
file of 200 bytes: 8 512-byte blocks allocated
```

Up to 100 bytes the file takes **one 512-byte unit of accounting** and no 4 KiB data block; at 150 bytes it takes a full block (8 units). The boundary between 100 and 150 is consistent with the 60 bytes of `i_block` plus a little more room in the inode's extended-attribute area, which the document describes only in part; I did not find the exact byte, and the sub-block size (1 unit) is how the kernel reports an inline file. The saving is real for directories of many small files (a config tree, a cache of tiny entries): one inode read instead of an inode read and a data-block read.

### 4.5 Comparison, and what a block really is

| Method | Reads for block `k` | Sequential | Metadata | Max file | Used by |
| --- | --- | --- | --- | --- | --- |
| Contiguous | 1 | best | start and length | the largest hole | CD-ROM (ISO 9660) and archives |
| Linked | `k + 1` | follows a chain | a pointer in each block | unlimited | not used alone |
| FAT | 1 (table in RAM) | follows the table | one entry per cluster, in RAM | the cluster count | FAT12, 16, 32 and exFAT |
| Multi-level index | 1 to 4 (1 for a small file) | indexed | pointers: 1 per block | 4 TiB at 4 KiB blocks | ext2, ext3, Unix FFS |
| Extents | 1 plus tree levels (0 to 2 in practice) | one entry per run | 12 bytes per run | length of the tree | ext4, XFS, btrfs, NTFS (from memory) |

The **block size** is the other half of every row. Small blocks waste little in the last block of each file (**internal fragmentation**, on average half a block per file) but need more pointers and more I/Os for the same data; large blocks do the opposite. The usual 4 KiB matches the page size, so a page of the page cache maps to one disk block ([OS 11](11_virtual_memory.md) section 1.5, and the cache pages of [OS 14](14_io_systems_and_disk_scheduling.md)). A 1-byte file takes a whole block (4 KiB) unless it is inline or the file system packs small tails (btrfs and ZFS do; from memory).

---

## 5. Free-space management

### 5.1 The bitmap, and why ext4 has one per group

The common structure is a **bitmap**: one bit per block, 1 for used, 0 for free (vsfs, ext2, ext3, ext4). Allocation is "find a zero bit" and freeing is clearing one. Its virtues are that it is **dense** (1 bit of metadata per block), that **finding a run of free blocks** is a scan for several zeros in a row, which is what an allocator wants, and that it is cheap to keep **near the data**: ext4's block bitmap for a group is one block and covers exactly that group (section 3.2). The size of the whole thing:

```text
  disk 32 GiB  cluster  4 KiB:      8388608 entries, FAT     32.0 MiB; bitmap    1.00 MiB
  disk 1 TiB   cluster  4 KiB:    268435456 entries, FAT   1024.0 MiB; bitmap   32.00 MiB
  disk 16 TiB  cluster  4 KiB:   4294967296 entries, FAT  16384.0 MiB; bitmap  512.00 MiB
```

For a 1 TiB disk with 4 KiB blocks the bitmap is **32 MiB** (268,435,456 bits), one thirty-second of the FAT for the same disk. Searching all of it for the one free block at the very end costs:

```c
static void bitmap_scan(void)
{
    size_t words = (32u << 20) / 8;
    uint64_t *bm = malloc(words * 8);
    memset(bm, 0xff, words * 8);                           /* every block in use ... */
    bm[words - 1] = ~(1ULL << 17);                         /* ... except one, at the very end */
    double t0 = now();
    size_t w = 0;
    for (int rep = 0; rep < 20; rep++)
        for (w = 0; w < words && bm[w] == ~0ULL; w++) { }
    double dt = (now() - t0) / 20;
    printf("\nscanning a 32 MiB bitmap (a 1 TiB disk) for the one free block at the end: %.2f ms, found block %zu, %.0f GB/s of bitmap\n",
           dt * 1e3, w * 64 + (size_t)__builtin_ctzll(~bm[w]), 32.0 / 1024 / dt);
    free(bm);
}
```

```text
scanning a 32 MiB bitmap (a 1 TiB disk) for the one free block at the end: 2.03 ms, found block 268435409, 15 GB/s of bitmap
```

About 2.03 ms for 32 MiB, by testing 64 bits per step (the figure is from one run and varied from 2.0 to 3.5 ms across runs). That is the **worst case of a single global bitmap**, and it is why the per-group split matters: a group's bitmap is 4 KiB, which at the same rate would take about 0.3 µs (computed, not measured), and the allocator starts at the right group (section 7).

### 5.2 Other structures, and the two resources that run out

- **A linked free list** (the original Unix scheme, from memory): free blocks chain through each other; allocating takes the head. It needs almost no space and finds a free block in O(1), but it returns blocks in whatever order they were freed, so a file's blocks scatter, and it cannot find a **run**.
- **Counting (extents of free space)**: store `(start, length)` pairs, optionally in a B-tree indexed by size and by position, so "find the best fit" is a lookup (XFS does this, and ext4's `mballoc` builds a similar view in memory, both from memory). It is the free-space twin of file extents.
- **FAT**: a free cluster has the entry value 0, so the table is also the free map and "find free" scans the table.

A file system has **two** pools that can run out independently, and the second surprises people. Free **blocks** hold data; free **inodes** hold files. ext4's inode count is **fixed at `mkfs` time** (section 3.2), and when the inodes are gone `creat` fails even though blocks remain. The experiment: an 8 MiB file system with only 64 inodes (`mkfs.ext4 -N 64`), filled with empty files:

```text
files created before ENOSPC: 53
df -i: inodes 64, used 64, free 0
df: 4 KiB blocks 2040, used 6, free 1892
creating one more: No space left on device
```

**53 files** were created, then `ENOSPC` ("No space left on device") while `df` showed **1,892 free blocks**, nearly all of the disk. The other 11 inodes of the 64 are reserved by the file system itself (inodes 1 to 10 and `lost+found`, section 3.3). The diagnosis is `df -i`, which shows inode use: 64 of 64. This is the standard cause of "the disk is not full but I cannot create a file", seen on a mail spool or a cache directory with millions of tiny files, and the fix is a file system made with more inodes (`mkfs.ext4 -i` bytes per inode, or `-N`), or a file system whose inodes are allocated dynamically (XFS and btrfs do that; from memory).

There are also **reserved blocks**: ext4 keeps about 5 percent of the blocks for root by default (from memory) so that a full disk still lets an administrator log in, and so that an allocator with free runs left can keep files contiguous. `tune2fs -m` changes it.

---
## 6. Directories

### 6.1 A directory is a file of entries

A directory is an inode of type "directory" whose **data blocks hold entries**, each binding a name to an inode number (OSTEP, fetched). The simplest layout, the one ext2 used and ext4 still uses inside each block, is a **linear list**. The ext4 entry (the document, fetched) is:

```text
inode     4 bytes   the inode number, 0 if the entry is unused
rec_len   2 bytes   the length of this whole entry, which says where the next one starts
name_len  1 byte    the length of the name
file_type 1 byte    regular, directory, symlink, and so on (a hint that saves reading the inode)
name      up to 255 bytes, padded so that the entry is a multiple of 4 bytes
```

An entry is `8 + name length` rounded up to 4, so **at most 263 bytes**. Every directory starts with `.` (itself) and `..` (its parent), which is what makes `cd ..` and the link counts of section 2.1 work. **Deleting** an entry does not move the others: the entry before it absorbs its space by growing its `rec_len` (or, for the first, its inode is set to 0), and a later entry can reuse the gap (OSTEP describes the record length for this reason). The directory file does **not shrink** when entries are deleted (from memory), which is why a directory that once held a million files stays large after they are removed.

The test program creates files with 12-character names (`file_0000001`), so every entry is `8 + 12 = 20` bytes and a 4 KiB block holds `4,096 / 20 =` 204 of them. The check against the measured directory sizes of the linear experiment below: 1,000 files need 5 blocks (20,480 bytes, measured 20,480), 10,000 need 49 to 50 (204,800, measured 204,800), 50,000 need 246 blocks (1,007,616, measured 1,007,616). Three exact matches, which confirms the format.

### 6.2 Looking up a name: linear versus hash tree

With a linear list, finding a name means **comparing against entries from the start** until a match (or the end, for a name that is not there). The cost is proportional to the directory's size. The hash-tree alternative, ext4's `dir_index` feature (from the `EXT4_INDEX_FL` flag on the directory, the document says), keeps the same leaf blocks but puts an **index tree in front**: the first block holds a `dx_root` with the hash algorithm and a table that maps **ranges of the name's hash** to leaf blocks, so a lookup hashes the name, picks one leaf and scans only that block. The tree has two levels (three with the `largedir` feature), and since it is wide the depth stays 2 for millions of entries. Listing a directory returns the entries in **hash order**, not alphabetical and not creation order, which is why `ls -f` looks scrambled on ext4 and `ls` sorts (from memory).

The experiment makes one directory of `n` files on an ext4 image, then **drops the dentry cache** so that every name must be looked up in the directory itself (the directory's blocks stay in the page cache), and times `stat` for 2,000 random existing names and for 2,000 names that do not exist. The same program runs on ext4 with `dir_index`, ext4 without it (`mkfs -O ^dir_index`), and `tmpfs`. The first of the two runs:

```c
static void dirscale_mode(const char *dir, int n)
{
    char base[4096], p[4200];
    snprintf(base, sizeof base, "%s/big", dir);
    mkdir(base, 0755);
    double t0 = now();
    for (int i = 0; i < n; i++) {
        snprintf(p, sizeof p, "%s/file_%07d", base, i);
        int fd = open(p, O_CREAT | O_WRONLY, 0644);
        close(fd);
    }
    double create = (now() - t0) / n * 1e6;
    sync();
    struct stat st;
    stat(base, &st);
    long dir_bytes = (long)st.st_size;
    int k = n < 2000 ? n : 2000;
    int *order = malloc(n * sizeof(int));
    for (int i = 0; i < n; i++) order[i] = i;
    rng_s = 12345;
    shuffle(order, n);
    sys_write("/proc/sys/vm/drop_caches", "2");                      /* forget the dentries, keep the directory blocks */
    t0 = now();
    for (int i = 0; i < k; i++) {
        snprintf(p, sizeof p, "%s/file_%07d", base, order[i]);
        IGN(stat(p, &st));
    }
    double hit = (now() - t0) / k * 1e6;
    sys_write("/proc/sys/vm/drop_caches", "2");
    t0 = now();
    for (int i = 0; i < k; i++) {
        snprintf(p, sizeof p, "%s/absent_%07d", base, order[i]);
        IGN(stat(p, &st));
    }
    double miss = (now() - t0) / k * 1e6;
    printf("  %6d files: directory %8ld bytes, create %6.1f us each, stat of an existing name %7.1f us, of a missing name %7.1f us\n",
           n, dir_bytes, create, hit, miss);
    char cmd[4300];
    snprintf(cmd, sizeof cmd, "rm -rf %s", base);
    IGN(system(cmd));
}
```

```text
ext4 with feature dir_index:
    1000 files: directory    36864 bytes, create    5.2 us each, stat of an existing name     2.2 us, of a missing name     1.4 us
   10000 files: directory   278528 bytes, create    5.2 us each, stat of an existing name     2.3 us, of a missing name     2.7 us
   50000 files: directory  1441792 bytes, create    5.8 us each, stat of an existing name     7.1 us, of a missing name     2.4 us
  200000 files: directory  5812224 bytes, create    6.8 us each, stat of an existing name     3.6 us, of a missing name     1.9 us
ext4 with feature ^dir_index:
    1000 files: directory    20480 bytes, create    9.4 us each, stat of an existing name     4.0 us, of a missing name     3.7 us
   10000 files: directory   204800 bytes, create   67.4 us each, stat of an existing name    26.7 us, of a missing name    32.6 us
   50000 files: directory  1007616 bytes, create  389.4 us each, stat of an existing name   162.6 us, of a missing name   173.2 us
tmpfs:
    1000 files: directory    20040 bytes, create    1.8 us each, stat of an existing name     0.9 us, of a missing name     0.9 us
   10000 files: directory   200040 bytes, create    1.8 us each, stat of an existing name     1.0 us, of a missing name     0.8 us
   50000 files: directory  1000040 bytes, create    2.0 us each, stat of an existing name     1.1 us, of a missing name     0.9 us
  200000 files: directory  4000040 bytes, create    2.2 us each, stat of an existing name     1.2 us, of a missing name     1.1 us
```

Reading the table (microseconds per operation, from one run, with the second run's numbers in the text where they differ):

- **Without the index, the cost is linear and large.** The lookup of an existing name goes from 4.0 µs at 1,000 files to **26.7 / 36.4 µs at 10,000** (first / second run) to **162.6 / 160.4 µs at 50,000**: 50 times the files, about 40 times the cost. **Creating** a file is worse than looking one up: 9.4 µs, 67 µs and 389 µs each, because `create` must first check that the name is not already there, which is a full scan, and a linear directory pays that on every call. Filling a directory of 200,000 files this way would take minutes, which is why that row is skipped (the arithmetic: at an average of 389 µs per create for 50,000 files, growing in proportion to the size, 200,000 files would take about 5 minutes: 389 µs times 4 times 200,000 is 311 s, an estimate).
- **With the index it stays flat.** Lookups cost 1.4 to 7 µs from 1,000 to 200,000 files. The figures are noisy (the 50,000-file existing lookup was 7.1 / 3.2 µs in the two runs, a factor of two apart) and I did not try to separate scheduling noise from real differences, but the trend, **no growth with size**, is the same in both runs. Creating costs about 5 to 7 µs each, flat, except for one 41 µs outlier in the second run.
- **The index costs space**: the 50,000-file directory takes 1,441,792 bytes against 1,007,616 without it, because the leaves of the tree are only about 55 to 70 percent full (computed from the sizes: 9 blocks against 5 at 1,000 files, 352 against 246 at 50,000; a split leaves two half-full leaves) and there is an index block. At 200,000 files: 5.8 MB.
- **`tmpfs`** takes about 1 µs at every size: it has no directory blocks, its directory is the in-memory dentry list itself (from memory), so there is nothing to scan.

One result I do not explain: without the index, a lookup of a missing name took about the same time as a lookup of an existing one, where a scan would find an existing name halfway on average and a missing one only at the end, so I expected a factor of two. The stat also includes reading the file's inode from the (cached) inode table, which adds a fixed amount to the existing case. The shape (linear in the size, tens to hundreds of microseconds at the sizes where a real directory hurts) is what matters here.

The practical rules: a directory with a few thousand entries costs nothing on any file system; a directory with millions needs an indexed file system (ext4 with `dir_index`, which is the default, XFS, btrfs; from memory); and **programs that create millions of small files in one directory** (a mail spool, a build cache) should shard by a hash prefix (`ab/cd/abcd...`) whatever the file system, because it makes `ls` and backups tolerable as well.

---

## 7. Placement: FFS and block groups

### 7.1 The problem FFS solved

The first Unix file system put the **inode table at the start of the disk and the data after it**, and managed free blocks with a free list that scrambled the order over time. OSTEP (fetched) reports the result: a file's inode and its data could be far apart, the blocks of one file ended up scattered so that sequential reads became random reads, and the old Unix file system delivered **about 2 percent of the disk's bandwidth**. The Berkeley **Fast File System (FFS)** did not change the interface (`open`, `read`, `write` stayed the same, which is why every later Unix file system kept it); it changed the **placement policy** so that the disk's seek cost (OS 14) is paid as seldom as possible.

### 7.2 The FFS policy

FFS divided the disk into **cylinder groups** (a set of adjacent cylinders; ext2 and ext4 renamed them **block groups** because cylinders are meaningless on modern disks and on SSDs). Each group is a small file system of its own: a copy of the superblock, an inode bitmap, a data bitmap, a slice of the inode table and a slice of the data region, which is the layout of section 3.2. The principle is one sentence, **keep related things close**, and the rules (OSTEP, fetched) are:

1. **A directory** goes into a group with a **low number of allocated directories** and a **high number of free inodes**, which spreads directories across the disk so that no one group fills up, and leaves room in the group for the directory's files.
2. **A file's data blocks** go into the **same group as its inode**, so that reading the inode and then the data needs no long seek.
3. **Files in one directory** go into the **same group as the directory**, on the bet (usually true) that files named together are used together: `ls -l` on a directory reads the inodes of its files, all in neighbouring slots of one group's inode table.
4. **The large-file exception.** A big file would take over a whole group and leave the rest of the directory's files without room. So FFS puts the first part of a large file in the directory's group, then moves **the next piece into another group**, and so on, where the **piece size is chosen so that the seek is amortised**: if positioning takes 10 ms and the disk transfers 40 MB/s, a piece of `10 ms * 40 MB/s = 400 KB` spends half the time moving data and half seeking (the formula is OSTEP's, the numbers are my own example). Splitting a large file costs some performance but keeps the rest of the namespace local.

The result is that **sequential reads of a file** and **walks of a directory tree** both stay close to the disk's sequential rate, and the gain is in layout only: no new data structure.

### 7.3 ext4: the rule, measured

ext4 inherits the group idea, and its allocator (the fetched document) keeps a file's blocks in its inode's group and sends top-level directories to the least-loaded group. The program tests the **inode** placement (the group number is `(inumber - 1) / inodes_per_group`; where the **data** of each file went was not measured). It creates 8 top-level directories, each with a sub-directory and three files, and prints the group of each. First **without** `flex_bg`, the classical FFS situation:

```text
d1: directory group 4; its sub-directory group 4; its files group 4
d2: directory group 2; its sub-directory group 2; its files group 2
d3: directory group 6; its sub-directory group 6; its files group 6
d4: directory group 1; its sub-directory group 1; its files group 1
d5: directory group 3; its sub-directory group 3; its files group 3
d6: directory group 5; its sub-directory group 5; its files group 5
d7: directory group 7; its sub-directory group 7; its files group 7
d8: directory group 0; its sub-directory group 0; its files group 0
```

The 8 directories went to **8 different groups** (4, 2, 6, 1, 3, 5, 7, 0: the order is not 0 to 7, and it varied between my runs), and **each sub-directory and its files followed the directory into the same group**: the rules of section 7.2 reproduced, directories spread, children close. Now **with `flex_bg`**, which is the ext4 default and the file system of section 3.2:

```text
inodes per group: 8192
d1: directory inode 12 group 0; its sub-directory group 0; its files group 0
d2: directory inode 8193 group 1; its sub-directory group 0; its files group 0
d3: directory inode 8194 group 1; its sub-directory group 0; its files group 0
d4: directory inode 8195 group 1; its sub-directory group 0; its files group 0
d5: directory inode 16385 group 2; its sub-directory group 0; its files group 0
d6: directory inode 16386 group 2; its sub-directory group 0; its files group 0
d7: directory inode 16387 group 2; its sub-directory group 0; its files group 0
d8: directory inode 24577 group 3; its sub-directory group 0; its files group 0
```

The top-level directories still spread (inodes 12, 8193 to 8195, 16385 to 16387 and 24577: groups 0, 1, 1, 1, 2, 2, 2, 3), but their **sub-directories and files all went to group 0**, not to the directory's group. The explanation I would offer, from my memory of the kernel's inode allocator and not from a fetched source, is that with `flex_bg` the unit of locality is the **flex group** (16 groups here, so all 8 groups are one flex group), and the allocator rounds a parent's group down to the first group of its flex group, which is 0. Locality is then enforced at a coarser level: the metadata of the 16 groups is already packed together (section 3.2), so a file's inode in group 3 and its neighbours in group 0 are no farther apart in terms of seeks than in the classical layout. I did not read the allocator source or test a bigger file system with several flex groups, so treat the rule as a hypothesis consistent with the measurement; what is measured is that **top-level directories spread over groups and everything below stayed in group 0 on a 1 GiB file system with one flex group**.

The takeaway for an interview is the principle, with the vocabulary: **block group** (the unit), **spread directories, cluster files**, **the large-file exception**, and the reason for it (seek amortisation). It also explains an observable behaviour: files created together in one directory get adjacent inode numbers and adjacent data, so reading them in directory order is fast, and reading in a different order (for example sorted by name when the directory is in hash order) can seek more. A workload that copies a tree, such as `tar`, is faster when it reads in **inode order** (from memory).

---
## 8. The VFS, path lookup and mounting

### 8.1 One system-call interface, many file systems

The same `open`, `read`, `stat` and `readdir` work on ext4, on `/proc`, on `tmpfs` and on a read-only `squashfs` image, because between the system calls and the file systems sits the **virtual file system (VFS)**, a layer of common objects and **tables of function pointers** that each file system fills in (the kernel VFS document, fetched). The program asks `statfs` and `stat` about paths in different places:

```c
static void describe(const char *path)
{
    struct statfs sf;
    struct stat st;
    if (statfs(path, &sf) != 0 || stat(path, &st) != 0) { printf("  %-14s %s\n", path, strerror(errno)); return; }
    printf("  %-14s %-9s st_dev %2u:%-3u  block size %5ld  blocks %10lu  inodes %9lu\n", path, fs_name(sf.f_type),
           major(st.st_dev), minor(st.st_dev), (long)sf.f_bsize, (unsigned long)sf.f_blocks, (unsigned long)sf.f_files);
}
```

```text
1. the same system calls, different file systems (one tree, many types)
  /              ext2/3/4  st_dev 254:0    block size  4096  blocks   66053021  inodes  16777216
  /tmp           ext2/3/4  st_dev 254:0    block size  4096  blocks   66053021  inodes  16777216
  /proc          proc      st_dev  0:21   block size  4096  blocks          0  inodes         0
  /sys           sysfs     st_dev  0:22   block size  4096  blocks          0  inodes         0
  /dev           tmpfs     st_dev  0:6    block size  4096  blocks    1024033  inodes   1024033
  /dev/shm       tmpfs     st_dev  0:27   block size  4096  blocks    2054167  inodes   1027083
  /opt/rclone    squashfs  st_dev 254:16   block size 131072  blocks         76  inodes         2
  /sys/fs/cgroup tmpfs     st_dev  0:28   block size  4096  blocks    1027083  inodes   1027083
  read(/proc/version) -> 20 bytes, begins "Linux versio"
  read(/proc/sys/kernel/osrelease) -> 16 bytes, begins "6.18.44-fc-v"
  read(/etc/hostname) -> 3 bytes, begins "vm"
```

`/` and `/tmp` are ext4 on device 254:0 (the virtio disk); `/proc` and `/sys` are pseudo file systems with no blocks and no inodes of their own (0 blocks), whose files are generated by the kernel when read (`/proc/version` returned 20 bytes through the ordinary `read`); `/dev` and `/dev/shm` are `tmpfs`; `/opt/rclone` is `squashfs`, with a 131,072-byte block size, on a different device (254:16). One tree, one path syntax, one system-call set, five implementations.

The VFS has four central objects (the document):

| Object | What it is | Where it lives |
| --- | --- | --- |
| **superblock** | one mounted file system: its type, block size, root, operations | on disk (ext4's copy) and in memory |
| **inode** | one file: type, permissions, size, the map to its blocks | on disk (section 3) and in memory |
| **dentry** | one **name** in one directory, linked to its inode and its parent dentry; the cache of the namespace | **memory only**, no disk copy |
| **file** | one **open file description**: the offset, the flags, the reference count (section 1.2) | memory only |

Each object carries a pointer to a **table of operations** (`file_operations`, `inode_operations`, `super_operations`), and the VFS calls through them without knowing what is behind. This is the object-oriented interface pattern in C. A new file system is a new set of tables, which is why `fuse` can put a file system in a user process: its tables forward to a daemon (from memory).

### 8.2 Path lookup: the walk

`open("/a/b/c/file")` must turn a string into an inode. The kernel starts at the root (or at the process's current directory for a relative path) and walks **one component at a time** (the path-lookup document, fetched). For each component it:

1. checks that the directory it is in grants **search (`x`) permission** (section 2.4);
2. looks the name up in the **dentry cache**, a hash table keyed by (parent dentry, name). A hit gives the child's dentry at once;
3. on a **miss** asks the file system: it calls the parent inode's `lookup()` operation, which reads the directory (section 6) and creates a dentry, including a **negative dentry** (a name known **not** to exist) if the name is absent;
4. if the dentry is a **symbolic link**, restarts the walk with the link's contents (at most 40 times per lookup, section 2.2);
5. if the dentry is a **mount point**, switches to the root of the file system mounted there (section 8.4), found through a hash of mounts keyed on the mount point; and for `..` at the root of a mounted file system it crosses back out.

The fast case matters most. The document describes two modes: **RCU-walk**, which takes no locks and no reference counts and so scales across cores, and **ref-walk**, the slower one it falls back to when something changes during the walk or the lookup must call the file system. A cached path stays in RCU-walk. The mount option **`relatime`** (the default, and the setting in the `mountinfo` line below) exists partly for this: updating the access time on every read would force a fallback and a write, so `relatime` updates it only when it is older than the modification time or a day old (the document says it helps avoid fallbacks; the exact rule is from memory).

**What the walk costs, measured.** Warm `stat` of a file at depth `d` (every component cached), against `fstatat` of the same file by name from an open descriptor of its directory (a lookup of a single component):

```c
static double stat_loop(const char *path, int reps)
{
    struct stat st;
    double t0 = now();
    for (int i = 0; i < reps; i++) IGN(stat(path, &st));
    return (now() - t0) / reps * 1e9;
}
```

```text
warm stat() of a file at depth d (every component cached), ns per call
  depth  1 ( 19 characters):     294 ns;  the same file by name from its open directory:   237 ns
  depth  2 ( 21 characters):     311 ns;  the same file by name from its open directory:   236 ns
  depth  4 ( 25 characters):     348 ns;  the same file by name from its open directory:   235 ns
  depth  8 ( 33 characters):     438 ns;  the same file by name from its open directory:   236 ns
  depth 16 ( 49 characters):     581 ns;  the same file by name from its open directory:   235 ns
  depth 32 ( 81 characters):     884 ns;  the same file by name from its open directory:   238 ns
```

At depth 1 the full path costs 294 ns, and at depth 32 it costs **884 ns**: about `(884 - 294) / 31 =` **19 ns per extra component**, which is the price of one permission check and one hash lookup. The relative lookup stays at **about 236 ns** whatever the depth, because it does no walk. About 236 ns is the cost of the system call and the `stat` itself. The depth-1 path has four components (`tmp`, `f5_path`, `d`, `file`) against one for the relative lookup, and it costs 57 ns more, which is 19 ns for each of the three extra, the same figure as the slope. The lesson is practical: paths are cheap when cached (a 10-component path adds under 200 ns), and a program that opens thousands of files in one directory saves the walk by opening the directory once and using the `*at` calls (`openat`, `fstatat`), which also avoid races if a path component is renamed during the walk.

### 8.3 The dentry cache, cold and warm, and negative dentries

The program measures one existing file and one missing name at the cold end and the warm end:

```text
warm stat() of a file at depth d (every component cached), ns per call
  depth  1 ( 19 characters):     294 ns;  the same file by name from its open directory:   237 ns
  depth  2 ( 21 characters):     311 ns;  the same file by name from its open directory:   236 ns
  depth  4 ( 25 characters):     348 ns;  the same file by name from its open directory:   235 ns
  depth  8 ( 33 characters):     438 ns;  the same file by name from its open directory:   236 ns
  depth 16 ( 49 characters):     581 ns;  the same file by name from its open directory:   235 ns
  depth 32 ( 81 characters):     884 ns;  the same file by name from its open directory:   238 ns

the dentry cache: one existing file, one missing name
  first stat after dropping everything    430.7 us (the directory and inode come from the disk)
  second stat                               0.8 us
  first stat after dropping dentries        6.8 us (the blocks are still in the page cache)
  stat of a name that does not exist: first 1.2 us, second 0.4 us (a negative dentry now answers)
  warm stat of the missing name: 221 ns, of the existing one: 299 ns
  /proc/sys/fs/dentry-state (total, unused, age limit, want pages, negative, 0): 1240 8 45 0 2 0
```

The figures, from the two runs (first / second where they differ):

- **A first `stat` after dropping all caches took 430.7 / 451.9 µs**: the directory blocks and the inode come from the disk (section 8.5), and this is the cost of a cold path. **The second `stat` took 0.8 µs**, 500 times less: the dentry and inode cache. The disk figure belongs to this virtual disk.
- **After dropping only the dentries** (the directory blocks stay in the page cache), the first `stat` took 6.8 / 0.5 µs. The two runs differ by more than 10 times and I did not find the reason (a likely candidate is whether the inode was still in the inode cache when the dentries were dropped, which varies; this is a guess). The honest reading is "a few microseconds or less": the work is a directory-block scan in memory plus building a dentry, with no I/O.
- **A name that does not exist** costs 1.2 / 1.3 µs the first time, because the file system scans the directory and finds nothing, and 0.4 / 0.3 µs the second time: the lookup left a **negative dentry**. Warm, the missing name costs 221 / 220 ns against 299 / 295 ns for an existing file (the existing one also fills a `struct stat`). The `dentry-state` file of the kernel shows the cache's size: 1,240 dentries in all, 2 of them negative.

Negative dentries are not a curiosity: a program that probes for files that are usually absent (a shell searching `PATH`, a loader searching library directories, a web server checking for `index.html`) would otherwise pay the directory scan on every probe. The cache is also the reason a large directory (section 6) is fast to lookup the second time, and why memory used for dentries grows without bound until the kernel shrinks it under pressure.

### 8.4 Mounting

A **mount** attaches a file system's root to a directory of another one. The mounted tree is **in front of** whatever was there, which is **hidden, not changed**. The program mounts a `tmpfs` over a directory that holds one file:

```text
2. mounting over a directory hides what was there
  before mount: 1 entry, st_dev 254:0
  after mount:  0 entries, st_dev 0:42 (a different file system), root inode 1
  50 27 0:42 / /tmp/f6_mp rw,relatime - tmpfs none rw,size=1024k
  ".." of the mounted root is inode 28 on 254:0, the same as /tmp itself (28 on 254:0): lookup crosses back out
  after umount: 1 entry again ("hidden_file" was never touched); mount took 26 us, umount 44 us
```

- Before the mount the directory has 1 entry (`hidden_file`) on device 254:0. After it, the same path shows **0 entries on device 0:42**, a different file system whose root is inode 1.
- The `mountinfo` line reads: mount id 50, parent mount 27, device `0:42`, the root inside the file system `/`, mounted at `/tmp/f6_mp`, options `rw,relatime`, then `-` and the type `tmpfs`, the source `none` and the file system's own options (`size=1024k`).
- `..` from the mounted root goes to **inode 28 on 254:0**, which is `/tmp` on the disk: the lookup **crosses back out**, as step 5 of section 8.2 says.
- After `umount` the file is back, untouched: it was never opened or changed. The first run measured the mount at 26 µs and the unmount at 44 µs.

A **bind mount** attaches a directory that is already in the tree at a second place:

```text
3. a bind mount: one directory, two paths, one inode
  f6_mp: inode 1278225 dev 254:0;  f6_bind: inode 1278225 dev 254:0
  a file created through f6_mp is visible through f6_bind: yes
  link() from f6_mp into f6_bind (same disk, different mount): Invalid cross-device link
  rename() from f6_mp into f6_bind: Invalid cross-device link
```

Both paths show **the same inode on the same device**, and a file made through one is visible through the other. But `link` and `rename` between the two paths fail with `EXDEV`, **although it is one disk and one file system**: the kernel treats a mount boundary as a boundary for these calls, even when both sides are the same superblock. (This is a rule I observed on this kernel; the reason, as I understand it, is that the two mounts could carry different options, such as read-only, and an edit across them would bypass the one that is stricter.) Bind mounts are how containers get a directory from the host into their own view, and what `mount --bind` and Docker volumes do.

A mount costs **about 34 µs** per `tmpfs` mount and unmount pair (2,000 pairs, measured), which is cheap enough that containers create them freely, and the mount hash that step 5 of section 8.2 consults is why a path walk does not slow down as the number of mounts grows (the claim about the hash is the document's; I did not measure the growth).

### 8.5 How many disk I/Os is a file operation?

OSTEP's accounting (fetched) for `open("/foo/bar")` on vsfs with a cold cache: read the **root inode**, read the **root's data** (to find `foo`), read **foo's inode**, read **foo's data** (to find `bar`), then read **bar's inode**: five reads, and every extra directory level adds two. A `read` then costs one read of the data block, and the inode is written back later for its access time. A **create** costs about ten I/Os (the inode bitmap, the new inode, the directory data, the directory inode, and more); an allocating **write** costs about five (data bitmap, inode, data, and so on). The chapter then says that **caching and write buffering** (a delay of 5 to 30 seconds before dirty data is written) turn most of this into memory operations.

The measurement. A fresh ext4 image (no journal, to keep the count plain) is mounted, all caches dropped, and the reads counted on the loop device:

```text
cold stat of /a/b/c/file (4 directories to walk): 5 disk reads
then reading its 4 KiB of data: 1 disk reads
reading it again: 0 disk reads (everything cached)
the same read after dropping the caches, with no separate stat: 5 disk reads
creating a new 2-byte file and syncing: 10 disk writes
appending one more 4 KiB block to it and syncing: 7 disk writes
```

- **A cold `stat` of `/a/b/c/file` costs 5 reads.** My accounting: **one** read for the inode-table block that holds the root's and all of `a`, `b`, `c` and `file`'s inodes (inodes 2 and 12 to 15 are within one 4 KiB block, 16 inodes per block, section 3.3), plus **four** reads for the data block of each of the four directories (`/`, `a`, `b`, `c`). The count matches OSTEP's per-level pattern (an inode read and a data read per level) with the inode reads merged, because the directories were made together and sit next to each other. A path whose inodes are spread over several blocks needs more. I did not trace the individual blocks (`blktrace` is not installed), so the accounting is consistent with the count and not proven by it.
- **Reading the file's data adds 1 read**; **reading it again adds 0**: the page cache (OS 14, [OS 11](11_virtual_memory.md) section 1.5).
- The same read from a cold start, **without a separate `stat`, also counted 5 reads**, one fewer than the 5 plus 1 above. I did not trace why (a one-time read in the first run is a possibility, a guess); the safe reading is "5 or 6 reads for a cold open and read of a file four directories deep".
- **Creating a 2-byte file and syncing costs 10 writes**, in line with OSTEP's "about ten". The writes are metadata (the new inode's block, the inode bitmap, the directory block, the parent directory's inode for its times, the block bitmap, the group's counters, and so on) and one data block; I did not trace which, and the list is from my accounting of what must change. **Appending one more block costs 7** (one run; an earlier run counted 6).

These are counts of the **device's** requests after the page cache and the block layer's merging (OS 14), not of the file system's logical operations, and they come from one run each. What an engineer takes away is the **order of magnitude**: a cold open is a handful of reads of about 0.1 ms each on this disk (so under a millisecond), and the same operation warm is a fraction of a microsecond. The whole structure of caches (dentries, inodes, pages) exists to turn the first number into the second.

---

## What it costs

| Operation | Cost (measured here, unless marked) | Why |
| --- | --- | --- |
| `stat` of a cached path, depth 1 | about 0.3 µs | system call plus a dcache hit |
| each extra path component | about 19 ns | one permission check and one hash lookup |
| `stat` of a missing name, warm | about 0.2 µs | a negative dentry |
| first `stat` of a cold path (4 directories) | about 0.43 to 0.45 ms, 5 disk reads | directory blocks and the inode table block |
| `stat` after dropping dentries only | a few µs or less | rebuilding dentries from cached directory blocks |
| lookup in a 50,000-entry directory, linear | about 160 µs | scan of 246 blocks of entries |
| the same, with the hash tree | about 3 to 7 µs (noisy) | hash, one leaf block |
| `create` in a 50,000-entry linear directory | about 390 to 420 µs | a scan to check the name |
| `mount` plus `umount` of `tmpfs` | about 34 µs | superblock and dentry set-up |
| create a file, with sync | 10 device writes | inode, bitmaps, directory, data |
| the largest file by block map (4 KiB) | 4 TiB (index arithmetic) | 12 direct plus three levels |
| one byte in each of 4 far regions: block map / extents | 10 / 4 blocks | pointer blocks against inline extents |
| bitmap for 1 TiB, 4 KiB blocks | 32 MiB; a full scan about 2 to 3.5 ms | 1 bit per block |
| FAT for 1 TiB, 4 KiB clusters | 1,024 MiB of RAM | 4 bytes per cluster |

---

## Common mistakes

1. **Saying `fork`, `dup` and a second `open` do the same thing.** Only the first two share the offset. A second `open` of the same file gets its own description and offset (`BBAA`).
2. **Thinking `unlink` deletes data.** It removes one name; the data goes when the last name **and** the last open description are gone (64 MiB held after `unlink`, 16,384 blocks freed at `close`).
3. **Treating a hard link as a copy with a special flag.** There is no original; both names are the inode. And a hard link to a directory, or across file systems, is refused.
4. **Calling every small symlink a block-consuming file.** A short target lives in the inode (0 blocks); a long one needs a block.
5. **Mixing up the directory bits.** `r` lists, `x` traverses, `w` creates or removes entries. A directory without `x` blocks every path through it, even when `access(R_OK)` says it is readable, and deleting a file needs `w` on its **directory**, not on the file.
6. **Quoting a maximum file size without the pointer arithmetic.** It is `12 * B + (B/4) * B + (B/4)^2 * B + (B/4)^3 * B` for block size `B` and 4-byte pointers; state the block size and show it (4 TiB at 4 KiB, not 4 GB).
7. **Believing extents cost the same as pointers.** One extent covers up to 128 MiB, and four fit in the inode; the fifth separate one forced a tree block (+2 blocks measured). Fragmentation raises the count (17 extents per 64 blocks when two files grew alternately).
8. **Reporting "the disk is full" without checking `df -i`.** The inode count is fixed at `mkfs`; 53 empty files filled a file system with 1,892 free blocks.
9. **Assuming a big directory is slow on every file system.** Linear lookup costs 160 µs at 50,000 entries; with a hash tree it costs a few microseconds. Know which one you have.
10. **Using `df` against `du` as if they measure the same thing.** `df` counts blocks the file system has allocated, including files unlinked but still open; `du` walks names.
11. **Expecting `link` or `rename` to work across a bind mount.** It is one disk, and the kernel still answers `EXDEV`.
12. **Calling `ext4` placement "cylinder groups".** The geometry is gone on modern disks; the unit is the **block group**, and the principle is directories spread, files clustered.
13. **Skipping the cache in a cost answer.** Every I/O count in this tutorial is for a cold cache; the same operation warm costs no device I/O.

---

## Questions out loud

Answer each in two minutes, with a number or a diagram, before looking back (the section in brackets).

1. A file descriptor, an open file description and an inode: what is each, and where does each live? [1.2]
2. After `fork`, what do parent and child share, and what does `dup` do differently from a second `open`? [1.2]
3. `O_APPEND` and two writers: what goes wrong without it? [1.2]
4. What does `unlink` do, and why can `df` report a full disk while `du` finds almost nothing? [1.3]
5. Hard link against symbolic link: five differences. What is a fast symlink? [2.1, 2.2]
6. What does `rename` guarantee, and why does it fail across file systems? [2.3]
7. What does the execute bit mean on a directory? What does the sticky bit do? [2.4]
8. Draw the layout of an ext4 file system: group, superblock, bitmaps, inode table, data. Why 32,768 blocks per group? [3.2]
9. Given an inode number, how do you find its disk block, with no search? [3.3]
10. Compare contiguous, linked, FAT, indexed and extent allocation: reads for block `k`, the failure mode of each. [4]
11. Compute the largest file for a 4 KiB block with 4-byte pointers and 12 direct entries. [4.3]
12. Why extents, and when do they stop saving space? What is delayed allocation? [4.4]
13. How is free space tracked? What exhausts first on a disk with millions of small files? [5]
14. How is a directory stored? Why can a directory of 50,000 files be fast on ext4 and slow on a file system with a linear list? [6]
15. What problem did FFS solve and what are its placement rules? Why is there an exception for large files? [7]
16. What is the VFS, and what does the dentry cache hold? What is a negative dentry for? [8.1, 8.3]
17. Walk through how the kernel resolves `/a/b/c/file`: permissions, cache, miss, symlinks, mount points. [8.2]
18. What does a mount do to the directory it covers? What is a bind mount, and why does `link` across one fail? [8.4]
19. How many disk I/Os does a cold `open` and `read` of a file four directories deep take? A create? [8.5]

---

## Problems

**Where to solve them.** On your own machine: build an ext4 image (`truncate -s 1G f.img; mkfs.ext4 f.img`), mount it with `mount -o loop`, and use `dumpe2fs`, `debugfs -R "stat <ino>"` and `debugfs -R "imap <ino>"`, `stat`, `filefrag` and `df -i`; all of them are what produced the numbers above. The OSTEP simulators for this topic, `file-implementation` (vsfs) and `file-ffs` (FFS), are in the course's homework repository (from memory of the repository names). For coding practice, in the style of the design interviews: LeetCode 1166 (Design File System) and 588 (Design In-Memory File System) (from memory of the numbers).

**How to approach them.** Write the formula first, then substitute. For layout problems: block size, then blocks per group, then groups. For size problems: pointers per block is `block size / 4`. For I/O counts: list the objects that must be read (inode, directory block, data) one by one and note which share a block. State every assumption that the problem does not give (cached or cold, inode size).

### Problem A (E, entry level): inode addresses

A file system has 4 KiB blocks, 256-byte inodes and 8,192 inodes per group. Group 0's inode table starts at block 100 and group 1's at block 700. Inodes are numbered from 1. Find the group, the block and the byte offset of inode 5,000 and of inode 9,000.

### Problem B (E, entry level): a different block map

A file system uses 8 KiB blocks and 4-byte block numbers. Each inode has 10 direct pointers, one single-indirect and one double-indirect pointer (no triple). (a) What is the largest file? (b) How many disk reads fetch the data block that holds byte 20 MiB, with the inode in memory and nothing else cached?

### Problem C (M, medium): FAT against linked

A 2 TiB disk uses a FAT file system with 16 KiB clusters and 4-byte entries. (a) How big is the FAT? (b) How big would a bitmap be? (c) How many disk reads fetch the cluster that holds byte 700 MiB of a file, with the FAT in memory, and how many without the FAT, following links stored in the clusters? (d) How much space does a 100-byte file waste?

### Problem D (M, medium): count the reads

`open("/a/b/f")` and read one byte at offset 0, on a cold cache. The inodes of `/`, `a`, `b` and `f` are in four different blocks, and each directory occupies one block. (a) How many reads? (b) The same with the dentry and inode caches warm but the data page not cached. (c) The file uses a block map, and the byte is at offset 100 KiB with 4 KiB blocks: how many reads cold?

### Problem E (H, hard): budgeting a 4 TiB volume

A 4 TiB ext4 volume has 4 KiB blocks, 256-byte inodes, 128 MiB groups and the default ratio of one inode per 16 KiB. (a) How many groups and inodes are there? (b) How big are the inode tables, as bytes and as a fraction of the disk? (c) How much do the block and inode bitmaps use if each is one block per group? (d) A service stores 600 million files of 2 KiB each. What happens, at what file count, and how much of the disk is used then? (e) Choose a `mkfs` setting that fits, and give the cost in space.

---

## Answers

**A.** 4,096 / 256 = 16 inodes per block. Inode 5,000: group `(5,000 - 1) / 8,192 =` 0; index 4,999; block `100 + 4,999 / 16 = 100 + 312 =` **412**; offset `(4,999 mod 16 = 7) * 256 = 1,792 =` **0x700**. Inode 9,000: group `(9,000 - 1) / 8,192 =` 1; index `8,999 - 8,192 = 807`; block `700 + 807 / 16 = 700 + 50 =` **750**; offset `(807 mod 16 = 7) * 256 =` **1,792 = 0x700**.

**B.** (a) Pointers per block: 8,192 / 4 = 2,048. Direct: 10 * 8 KiB = 80 KiB. Single: 2,048 * 8 KiB = 16 MiB. Double: 2,048 * 2,048 * 8 KiB = 4,194,304 * 8 KiB = 32 GiB. Total about **32 GiB + 16 MiB + 80 KiB**, which is 32.016 GiB. (b) Byte 20 MiB is in file block `20 MiB / 8 KiB = 2,560`. The direct region holds blocks 0 to 9 and the single indirect holds 10 to 2,057, so block 2,560 is in the **double-indirect** region: read the double-indirect block, then the indirect block it points to, then the data: **3 reads**.

**C.** (a) `2 TiB / 16 KiB = 2^41 / 2^14 = 2^27 = 134,217,728` clusters, so the FAT is `2^27 * 4 B = 512 MiB`. (b) A bitmap is 1 bit each: `2^27 / 8 = 16 MiB`. (c) Byte 700 MiB is in cluster `700 MiB / 16 KiB = 44,800`. With the FAT in memory, one read (the data cluster). Without it, the chain must be followed from cluster 0: `44,800 + 1 =` **44,801 reads**. (d) `16,384 - 100 = 16,284` bytes, 99.4 percent of the cluster.

**D.** (a) Inode of `/` and its data, inode of `a` and its data, inode of `b` and its data, inode of `f`, then `f`'s data: `2 + 2 + 2 + 1 + 1 =` **8 reads**. (b) Only the data block: **1 read**. (c) Offset 100 KiB is file block 25, beyond the 12 direct pointers (block 12 to 1,035 are single indirect): the 7 lookup reads, plus the **single-indirect block**, plus the data block: **9 reads**. (The measured file in section 8.5 needed only 5 for the lookup because its inodes shared one block.)

**E.** (a) `4 TiB / 128 MiB = 2^42 / 2^27 =` **32,768 groups**. Inodes: `2^42 / 2^14 = 2^28 =` **268,435,456**, which is 8,192 per group. (b) Inode tables: `2^28 * 256 B = 2^36 B =` **64 GiB**, `64 GiB / 4 TiB =` **1.56 percent**. (c) 32,768 block bitmaps and 32,768 inode bitmaps, one 4 KiB block each: `65,536 * 4 KiB =` **256 MiB**. (d) Files run out of inodes at **268,435,456 files**, with `268,435,456 * 4 KiB = 1 TiB` (25 percent) of the disk used, since each 2 KiB file takes one 4 KiB block; the other 3 TiB is free and unusable, and `creat` returns `ENOSPC` (the failure of section 5.2). 600 million files need 2.2 times as many inodes. (e) Make at least 600 million inodes: `-i 4096` gives `2^42 / 2^12 = 2^30 =` 1,073,741,824 inodes, with tables of `2^30 * 256 B = 256 GiB` (6.25 percent of the disk) instead of 64 GiB, a cost of 192 GiB; or `-N 640000000`, with tables of `640 M * 256 B =` 152.6 GiB, which is 88.6 GiB more than the default and the cheaper choice. Inline data (section 4.4) is a further option only for files under about 100 bytes, which these are not.

---

## Cheat sheet

- **Tables:** descriptor (per process) to open file description (offset, flags, refcount; shared by `dup` and `fork`, not by a second `open`) to inode (one per file).
- **`unlink`** removes a name; data is freed at link count 0 **and** no open description. `df` against `du` mismatch means deleted-but-open files.
- **Hard link**: another name, same inode, no directories, no other file system. **Symlink**: an inode holding a path, can dangle, 40 per lookup, a short target is stored in the inode (0 blocks).
- **`rename`** is atomic and stays inside one file system. **Directory bits**: `r` list, `w` create or delete, `x` traverse. **Sticky**: only the owner deletes. **`umask`** removes bits.
- **ext4 layout:** group = `8 * block size` blocks (32,768 at 4 KiB, 128 MiB); each group has a block bitmap (1 block), an inode bitmap, an inode table; `flex_bg` packs them; backup superblocks in a few groups.
- **Inode address:** `group = (n - 1) / ipg`, `index = (n - 1) % ipg`, `block = table + index * isize / bsize`, `offset = index * isize % bsize`.
- **Allocation:** contiguous (1 read, external fragmentation); linked (`k + 1` reads); FAT (table in RAM, 1 GiB per TiB at 4 KiB); multi-level index (12 direct 48 KiB, + 4 MiB, + 4 GiB, + 4 TiB at 4 KiB); extents (12 bytes per run, 4 in the inode, 128 MiB each, tree beyond).
- **Free space:** a bitmap (32 MiB per TiB at 4 KiB), per group in ext4; inodes are a separate fixed pool: `df -i`.
- **Directories:** a list of (inode, rec_len, name_len, type, name) entries; linear lookup is O(n) (160 µs at 50,000), the hash tree is flat (3 to 7 µs).
- **FFS:** block groups; spread directories, keep a file with its inode and its directory; large files are split across groups so the seek is amortised.
- **VFS:** superblock, inode, dentry (memory only), file; tables of operations; lookup walks components through the dentry cache (about 19 ns each warm), negative dentries remember absences, mount points switch file systems, `..` crosses back.
- **Cold I/O:** open a path 4 directories deep, 5 reads (inodes shared a block); create, about 10 writes; warm, none.

---

## Sources

- [OSTEP (Arpaci-Dusseau), Interlude: Files and Directories](https://pages.cs.wisc.edu/~remzi/OSTEP/file-intro.pdf) (fetched): files, inodes, directories, the open file table, `dup`, `fork`, `rename`, links, permissions.
- [OSTEP, File System Implementation](https://pages.cs.wisc.edu/~remzi/OSTEP/file-implementation.pdf) (fetched): vsfs, the inode formula, direct and indirect pointers, extents, directories, bitmaps, the I/O counts of open, read, create and write.
- [OSTEP, Locality and The Fast File System](https://pages.cs.wisc.edu/~remzi/OSTEP/file-ffs.pdf) (fetched): the old file system's 2 percent, groups, the placement policy, the large-file exception.
- [Linux kernel: Overview of the Linux Virtual File System](https://docs.kernel.org/filesystems/vfs.html) (fetched): superblock, inode, dentry, file, operations tables, negative dentries. The document does not fully describe the mount-point attachment.
- [Linux kernel: Pathname lookup](https://docs.kernel.org/filesystems/path-lookup.html) (fetched): the component walk, the dentry hash, RCU-walk against ref-walk, mount points, `..`, the 40-link symlink limit, `relatime`.
- [Linux kernel: ext4 overview](https://docs.kernel.org/filesystems/ext4/overview.html) (fetched, but its text did not cover `flex_bg`, the file and file system size limits or the hash-tree trigger, so those are from memory), [ext4 inode data fork (ifork)](https://docs.kernel.org/filesystems/ext4/ifork.html) (block map ranges, the extent tree, inline data), [ext4 directory entries](https://docs.kernel.org/filesystems/ext4/directory.html) (linear format, hash tree, `dx_root`) and [ext4 allocators](https://docs.kernel.org/filesystems/ext4/allocators.html) (the group locality rule, `mballoc`, delayed allocation) (all fetched).
- Measurements on this machine: Linux 6.18.44, ext4 and tmpfs, `mkfs.ext4` 1.47.0, `dumpe2fs`, `debugfs`, `filefrag`; the XFS, btrfs and F2FS designs were not run.
- Claims marked "from memory" were not fetched: the fast-symlink threshold, `lsof +L1`, `sparse_super` placement, reserved inodes, the placement rule with `flex_bg`, inline-data capacity, hash order of `readdir`, XFS and btrfs designs, `fuse`, the OSTEP simulator names and the LeetCode numbers.
