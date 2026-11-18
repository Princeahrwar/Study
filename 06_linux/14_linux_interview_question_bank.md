# LX 14: Linux interview question bank

*Part of the [Linux plan](00_plan.md) · Time: about 10 hours over a week · You need: all of [LX 01 to LX 13](00_plan.md). This is the **last Linux tutorial**. The earlier ones taught the subject; this one **drills it in the form an interview asks it**: 80 questions (plus eight added after a review, section 9.1) with answers short enough to say aloud, 15 troubleshooting scenarios worked end to end, and one page of triage tree to carry in your head.*

A Linux interview for a senior or staff role has two halves. The first half asks **"what is X and why does it work that way?"** and rewards a crisp definition followed by one level of depth. The second half hands you a broken machine and asks **"what do you do?"** and rewards a **method**: a first command, a reading of its output, a narrowed hypothesis, a fix, and a way to know the fix worked. This tutorial trains both. The questions are grouped by area (sections 2 to 9), the scenarios are in section 10, and the **triage tree** that ties the second half together is in section 1.

**Seven honest limits, stated first.**

1. The experiments ran on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, 8 GB, Ubuntu 24.04) as **root**, on small `tmpfs` mounts, cgroups and network namespaces that were created and removed in the same script. **PID 1 here is not systemd**, so `systemctl`, `journalctl`, `cron`, restart loops and unit files were **not run**. Those answers are from memory and marked so.
2. **Ten of the fifteen scenarios were reproduced and their output is frozen below** (S1 to S10). S11 to S13 are **from memory**. S14 and S15 point at **measured results in LX 12 and LX 13** rather than repeating them.
3. The **88 answers are written from knowledge, not run one by one**. Where a claim is backed by a run it carries a tag such as **[run S7]**. Untagged answers are standard material; where a number varies by kernel or distribution, the text says so.
4. **cgroups on this VM are the version 1 hierarchy** (a hybrid mount), so the scenario outputs show v1 file names such as `memory.limit_in_bytes`. The **v2 equivalents** (`memory.max`, `memory.events`) are given from memory.
5. **No `netem`, no AppArmor or SELinux policy tools and no `chronyd`** exist here. Packet loss, MAC denials and clock drift are described, not produced.
6. An interview answer is a **starting position, not a specification**. Kernel behaviour changes between versions (for example the CPU scheduler is EEVDF since 6.6; CFS before). Say "on recent kernels" when it matters, and offer to check.
7. Nothing here is a model answer to recite. The goal is that **you can reconstruct each answer from first principles**, which is what a good interviewer is testing.

By the end you can:

- **answer 88 standard Linux questions** across shell and files, processes, memory, storage, networking, containers, performance and security in **30 to 90 seconds each**, with one level of depth ready when the interviewer pushes;
- run the **first sixty seconds of a triage** in a fixed order and **read the result**: for example, on a 2 CPU VM with three busy loops, `vmstat` showed a run queue **`r` of 3** and **99 to 100 percent user time** while the one-minute load average still read **0.09** (the average lags, section 10, S1);
- diagnose **"disk full but `du` disagrees"** and **fix it without a restart** (measured: `df` **6,144 KB used of 8,192**, `du` **0**, found with `lsof +L1`, freed with `: > /proc/PID/fd/3`);
- distinguish **no space because of blocks** from **no space because of inodes** (measured: **200 of 200 inodes** used at **0 percent** block use, `touch` failing with `No space left on device`);
- show that **exit code 137 has two causes** and **prove which one happened** (measured: both a `kill -9` and a cgroup OOM exit **137**; only the OOM raised `oom_kill` from **0 to 1**);
- tell **refused**, **timed out**, **unreachable** and **permission denied** apart and name the layer each points to (measured: `ping: connect: Network is unreachable`, a link at `NO-CARRIER`, `Permission denied` on an executable file on a `noexec` mount);
- explain **why a cron job fails when the same script works in a shell** (measured: PATH of 16 entries against 2, `go: found` against `go: not found`);
- hold a **one-page triage tree** and say what each branch rules out.

**Where the numbers come from.** Twelve experiment scripts (`s1_slow` to `s12_busy`) and four problem-answer scripts (`ans_a` to `ans_d`) were run in this session. Every transcript in the tutorial is a frozen output of one of them. Process ids were replaced by `N` where they changed. Numbers from earlier tutorials (LX 12 and LX 13) are cited with their tutorial. Anything else is marked "from memory".

**Data and timing.** No data sets. Each script runs in under ten seconds. Plan the week as: sections 2 to 9 (with 9.1) at one category a day (about 40 minutes each, **saying the answers aloud**), section 10 on the weekend (about 3 hours, with the scripts), sections 11 to 13 the day after.

---

## 1. How to answer, and the triage tree

### 1.1 Shape of a good answer

Every question has the same four-part shape, and the order matters because an interviewer may stop you after any part.

1. **One sentence that is correct on its own.** "A zombie is a process that has exited but whose parent has not yet read its exit status."
2. **The mechanism, one level down.** "The kernel keeps the process table entry, which holds the pid and the status, until the parent calls `wait`."
3. **How you would see it.** "`ps` shows state `Z`; `/proc/PID/status` says `Z (zombie)`."
4. **What goes wrong, or what you would do.** "`kill -9` does nothing because it is already dead; fix or restart the parent."

Parts 3 and 4 are what separate a senior answer from a textbook one. If a question has no part 3, you have not yet understood it.

### 1.2 The method for a broken machine

Say it before you start typing; interviewers listen for it.

1. **Define the symptom with numbers.** "Slow" means what: latency of what, since when, for whom, compared with what?
2. **Ask what changed.** Deploy, config, traffic, kernel, disk fill, certificate expiry. Most incidents are a change.
3. **Look at the whole machine before any one process.** CPU, memory, disk, network, each as **utilisation, saturation, errors** (the USE method from [LX 05](05_performance_methodology_and_triage.md)).
4. **Form one hypothesis, run the cheapest command that could disprove it.** Not the most impressive command.
5. **Fix, then prove the fix** with the same measurement that showed the problem.
6. **Say what you would leave behind**: an alert, a limit, a runbook line.

### 1.3 The triage tree

Keep this in your head. Each box is one question; each answer names the next command.

```text
SYMPTOM: "it is slow / down / failing"
|
+-- Can you reach the machine at all?
|     no  -> network path or sshd or the box is dead:
|            ping, ssh -v, console. Refused = RST (no listener or reject rule),
|            timeout = silent drop or host down, no route = routing/ARP.
|     yes
|
+-- First 60 seconds: uptime; dmesg | tail; vmstat 1 5; mpstat -P ALL 1 3;
|   pidstat 1 3; iostat -xz 1 3; free -m; ss -s
|
+-- CPU busy (us+sy high, vmstat r > nproc)?
|     us high -> one process? pidstat / top -> perf top, then profile it
|     sy high -> syscalls or locking: strace -c -p, perf top
|     st high -> the host is taking the CPU (VM): not your machine's fault
|     cgroup throttled? -> cat cpu.stat (nr_throttled)
|
+-- CPU idle but load high, b column > 0 (tasks in state D)?
|     -> waiting on disk, NFS or a lock: iostat -xz, ps -eo stat,wchan | grep D,
|        /proc/PID/stack, lsof -p PID
|
+-- Memory short (free low AND available low, si/so non-zero, OOM in dmesg)?
|     -> who: ps --sort=-rss, smem; leak or limit?  cgroup memory.max_usage
|        oom_kill counter and the dmesg line tell you which limit was hit
|
+-- Disk: write fails?
|     df -h  full?   -> du -xh | sort -h; then lsof +L1 (deleted but open)
|     df -i  full?   -> inode exhaustion: find a directory with millions of files
|     df fine, still ENOSPC / EROFS -> quota, read-only remount, tmpfs size
|
+-- Network: connect fails or is slow?
|     ss -ltnp on server: listening?   (address already in use? who owns it)
|     ip -br link / ip route get X: link and route sane?
|     nft list ruleset / iptables-save: drop vs reject
|     tcpdump: SYN without SYN-ACK = filtered; RST = refused; retransmits = loss
|     nstat -az | grep -i -E 'drop|overflow|retrans': counters that explain it
|
+-- Works by hand, fails from a service / cron / container?
|     environment (PATH, HOME, cwd), user, mount options (noexec), cgroup limits,
|     namespaces (different network or filesystem view)
|
+-- None of the above: tail latency or intermittent
      -> LX 13: timer slack, idle exit, throttling, page faults, interrupts;
         measure percentiles over repeated runs before changing anything
```

The tree is **ordered by cost and by how often each branch is the answer**. The first sixty seconds is meant to point at the right branch; if it does not, widen the net rather than guessing.

### 1.4 What a senior answer sounds like

Compare two answers to "the server is slow".

**Junior:** "I would run `top` and see what is using CPU."

**Senior:** "First I would ask what slow means and what changed. Then in the first minute I would check load and the run queue, per CPU utilisation, disk latency and saturation, memory and swap, and the dmesg tail, because between them they cover the four resources. Whichever resource is saturated tells me which tool to reach for next. I would not tune anything until I had a number that moved."

The second answer is longer, and it is not longer because it is more words: it names **what it will rule out and in what order**.

---

## 2. Shell and files (Q1 to Q10)

**Q1. What happens when you type `ls | grep foo > out`?**
The shell parses the line into a pipeline of two commands and a redirection, creates a **pipe**, and **forks** twice. In each child it wires the file descriptors with `dup2` (the first child's stdout is the pipe's write end, the second child's stdin is the read end), the second child also opens `out` with `O_WRONLY|O_CREAT|O_TRUNC` as its stdout, then each child calls `execve` (the command is looked up through `PATH`). The parent waits for the pipeline; `$?` is the **last** command's status unless `pipefail` is set. The pipe has a **64 KB buffer** by default: a fast writer blocks when it is full, which is how a pipeline is flow controlled for free.

**Q2. Hard link versus symbolic link. What does `rm` really do?**
A **hard link** is another directory entry pointing at the same inode; the inode has a **link count**. A **symlink** is a small file whose content is a path. Hard links cannot cross filesystems and cannot point at directories; symlinks can do both and can dangle. `rm` calls `unlink`, which removes one name and decrements the count; the **data is freed only when the count is 0 and no process still holds the file open**. That last clause is the cause of the "disk full but `du` disagrees" scenario [run S2].

**Q3. What is the difference between `cmd > file 2>&1` and `cmd 2>&1 > file`?**
Redirections apply **left to right**. In the first, stdout goes to the file and then stderr is duplicated onto where stdout now points, so **both** go to the file. In the second, stderr is duplicated onto the terminal (stdout's current target) **before** stdout is redirected, so stderr still reaches the terminal. In bash, `&> file` is the short form of the first.

**Q4. What do `set -euo pipefail` do, and where do they mislead?**
`-e` exits on a command that fails, `-u` treats an unset variable as an error, `pipefail` makes a pipeline fail if any stage fails. Traps: `-e` is **suppressed** inside conditions (`if cmd`, `cmd || true`, the left of `&&`), so a failing function called in an `if` does not stop the script; `local x=$(cmd)` **hides** the status of `cmd` (declare then assign); `((i++))` returns status 1 when `i` was 0; and `grep` returning 1 for "no match" kills a script that only wanted to count. Use them, and also check the returns that matter by hand.

**Q5. Find the ten largest files under `/var` without crossing into other filesystems.**
`du -xah /var | sort -rh | head`, or `find /var -xdev -type f -printf '%s %p\n' | sort -rn | head`. The `-x` and `-xdev` flags keep the walk on one filesystem, which matters because a mounted volume under `/var` would otherwise hide the real answer. Directory totals first (`du -xh --max-depth=1 /var | sort -h`) narrows faster than listing files.

**Q6. What is an inode and what does it not contain?**
The inode holds the file's **metadata and the location of its data**: mode, owner, size, timestamps, link count, and block pointers or extents. It does **not** hold the **name** (that lives in the directory entry) or the data itself. Consequences: renaming within a filesystem is a directory edit that does not touch data; a filesystem has a **fixed number of inodes** chosen at creation on ext4, so it can run out of them while having free blocks [run S3].

**Q7. Why does `cp` onto a running binary fail with "Text file busy" but `mv` or `install` succeeds?**
The kernel refuses to open an executing file for writing (`ETXTBSY`), because overwriting pages under a running process would corrupt it. `mv` and `install` instead **create a new inode and replace the directory entry**; the running process keeps the old inode, which is freed when it exits. The same trick is why a log rotation can rename a log without breaking the writer, and why the writer keeps filling the old file until it reopens.

**Q8. Explain permission bits on a directory.**
`r` lets you **list names**, `w` lets you **create, delete and rename entries** (and only works together with `x`), `x` lets you **traverse**: look up a name inside it. A file you can write in a directory you cannot write is modifiable but not deletable. The **sticky bit** (`/tmp`) lets only a file's owner or the directory owner delete it; **setgid** on a directory makes new files inherit its group.

**Q9. What is the difference between `"$@"` and `"$*"`, and why quote?**
`"$@"` expands to **each argument as its own word**, preserving spaces inside them; `"$*"` joins them into **one word** using the first character of `IFS`. Unquoted, an expansion is subject to **word splitting and globbing**, so a file named `my file` becomes two arguments and `*` becomes a file list. Quote every expansion unless you want splitting, and use `"$@"` to pass arguments through.

**Q10. How do you debug a shell script?**
`bash -n script` checks syntax without running it. `bash -x script` (or `set -x` around a region) traces each command after expansion; set `PS4='+ ${BASH_SOURCE}:${LINENO}: '` to see where. `shellcheck` finds quoting and portability errors before you run anything. `trap 'echo line $LINENO >&2' ERR` reports the failing line. For a script that works by hand and fails elsewhere, compare the **environment** (scenario S10).

---

## 3. Processes (Q11 to Q20)

**Q11. Explain `fork`, `exec` and `clone`.**
`fork` creates a child with a **copy-on-write** duplicate of the parent's address space. `execve` **replaces** the calling process's program with a new one, keeping the pid and open descriptors. `clone` is the general call: flags choose what is shared (`CLONE_VM` for memory, `CLONE_FILES`, `CLONE_NEWNET` for a new network namespace). **A Linux thread is a task created by `clone` sharing memory**, and a container process is a task created with namespace flags. `vfork` shares memory until the child execs, to avoid the copy.

**Q12. Zombie versus orphan.**
A **zombie** has exited, but its parent has not called `wait`, so the kernel keeps a process table entry (pid and exit status). It uses no memory but occupies a pid, and **`kill -9` cannot remove it** because it is already dead; the parent must reap it, or you kill the parent so that init does. An **orphan** is a live process whose parent died; it is **reparented** to init (or a "subreaper"). Measured: a Python parent that forked and slept left a child in state `Z` that survived `kill -9` [run S6].

**Q13. What do the state letters in `ps` mean?**
`R` running or runnable, `S` interruptible sleep (waiting for an event, can take a signal), `D` **uninterruptible sleep** (usually in the middle of I/O, **cannot be killed**, and counts toward load average), `T` stopped, `Z` zombie, `I` idle kernel thread. The `WCHAN` column or `/proc/PID/stack` names the kernel function it sleeps in [run S4: `wait_for_partner`, from `fifo_open`].

**Q14. When does `kill -9` not work?**
`SIGKILL` cannot be caught or ignored, but it is **delivered when the task next returns from the kernel**. So it waits behind a task in state **D** until the I/O completes (a dead NFS server can hold it for a long time); it does nothing to a **zombie**; it cannot be sent to a process you do not own without privilege; and the **init of a PID namespace** ignores signals it has no handler for when they come from inside the namespace.

**Q15. What can you safely do in a signal handler?**
Only call **async-signal-safe** functions (`write`, `_exit`, `kill`, and a short list in `signal-safety(7)`), because the handler can interrupt the program in the middle of `malloc` or `printf` and re-enter non-reentrant code. The safe pattern is to **set a `volatile sig_atomic_t` flag** or `write` one byte to a **self-pipe** (or use `signalfd`), and handle the event in the main loop.

**Q16. What are exit codes 137, 139 and 143?**
A process killed by a signal reports **128 plus the signal number** to its shell: **137 is SIGKILL** (9), **139 is SIGSEGV** (11), **143 is SIGTERM** (15), 134 is SIGABRT (6). **137 has two common causes**: someone ran `kill -9`, or the **OOM killer** did. They are told apart by the OOM counter and the kernel log, not by the exit code [run S7 and Problem B: both gave 137, only the OOM raised `oom_kill` from 0 to 1].

**Q17. What does load average mean?**
The **exponentially damped average, over 1, 5 and 15 minutes, of the number of tasks that are runnable or in uninterruptible sleep**. It is not CPU utilisation. Compare it to the CPU count (`nproc`), remember it **includes tasks waiting on I/O**, and remember it **lags**: on this 2 CPU VM, four seconds after three busy loops started, `vmstat` showed `r` of 3 and the one-minute load read **0.09** [run S1]. Use `vmstat` and `mpstat` for the present; load is for trends.

**Q18. `nice`, priorities and fairness: what actually decides who runs?**
The default scheduler class gives each runnable task a **weight** derived from its nice value (about a **1.25 times** difference per step), and shares CPU in proportion (CFS before 6.6, **EEVDF** since). `nice` only matters when CPUs are contended: the three loops in S1 each got about **two thirds of a CPU** (**67.3, 65.7 and 64.1 percent**) because three equal tasks shared two CPUs. To give a **group** a share regardless of how many tasks it has, use **cgroup** weights. **Real-time** classes (`SCHED_FIFO`, `SCHED_RR`) preempt all normal tasks and can starve them ([LX 13](13_tuning_for_latency_and_throughput.md)).

**Q19. How do you find what a hung process is doing?**
In order of cost: `ps -o stat,wchan:20 -p PID` (state and kernel sleep point), `cat /proc/PID/stack` (the kernel stack, root only), `ls -l /proc/PID/fd` and `lsof -p PID` (what it holds open), `strace -p PID` (the system call it is blocked in), then `gdb -p PID` or `perf top -p PID` for user space. Measured on a reader of a FIFO nobody writes: state `S`, `wait_for_partner`, and `strace` showing `openat(..., O_RDONLY` that never returned [run S4].

**Q20. What does daemonizing involve, and what changes under a service manager?**
The classic recipe: **fork and exit** the parent, `setsid` to leave the terminal session, **fork again** so it cannot reacquire a terminal, `chdir("/")`, set `umask`, close or redirect descriptors 0 to 2, write a pid file. Under **systemd** (from memory) you do **none** of that: run in the foreground (`Type=simple`), log to stdout, and let the manager track the process with its **cgroup** and restart it with `Restart=`. A double-forking program under `Type=simple` makes systemd think it exited.


---

## 4. Memory (Q21 to Q30)

**Q21. What does `free` mean by "available", and why is "free" low on a healthy machine?**
`MemAvailable` estimates how much memory could be handed to a new workload **without swapping**: free pages plus the parts of the page cache and slab that can be reclaimed cheaply. `MemFree` is only the pages nobody is using at all. Linux deliberately uses spare RAM as **page cache**, so low `free` with high `available` is healthy. Alarm when **available** is low **and** swap is active (`si`/`so` non-zero in `vmstat`) or `dmesg` shows OOM kills.

**Q22. Virtual, resident, shared: what do VSZ and RSS count?**
**VSZ** is the address space mapped (including reserved but untouched ranges and shared libraries). **RSS** is the pages currently resident in RAM, and it **counts shared pages in every process that maps them**, so summing RSS over processes overcounts. **PSS** divides each shared page among its users and sums correctly; read it from `/proc/PID/smaps_rollup`. "The process uses 40 GB" usually means VSZ, which is not memory.

**Q23. What is overcommit, and what do the three `vm.overcommit_memory` modes do?**
`malloc` succeeding does not mean the memory exists: Linux hands out **address space** and allocates physical pages on first touch. Mode **0** (default) refuses only clearly absurd requests (a heuristic), mode **1** always says yes, mode **2** enforces a hard limit (`CommitLimit`, swap plus a percentage of RAM). The cost of overcommit is that failure arrives **later, at a page fault**, as the **OOM killer**, not as a `NULL` from `malloc`.

**Q24. How does the OOM killer choose its victim?**
It computes a **badness score** per task, mostly proportional to the task's memory (resident pages, swap, page tables) as a share of the total, then adds `oom_score_adj` (from -1000 to +1000; **-1000 exempts** the task). The highest score dies. Inside a **cgroup** that hit its own limit, the choice is made **among that cgroup's tasks only**. Measured: a 100 MB allocation in a 32 MB cgroup was killed, and the kernel log said `Memory cgroup out of memory: Killed process ... (python3) total-vm:137956kB, anon-rss:32512kB` [run S7].

**Q25. What is `vm.swappiness`, and should you turn swap off?**
It biases reclaim between **anonymous memory** (needs swap) and **file cache** (can be dropped). A small amount of swap lets the kernel move genuinely cold anonymous pages out so the cache can grow, which often helps. Turning swap off makes memory exhaustion **abrupt** (OOM instead of slowdown). For latency-critical processes, do not rely on swappiness: **`mlock`** their memory or keep them in a cgroup with a memory guarantee.

**Q26. Is `echo 3 > /proc/sys/vm/drop_caches` a good fix?**
No. Cache is **reused memory**; dropping it makes the next reads go to disk and hides what was using it. It is correct for one thing: **benchmarking from a cold cache**. If memory pressure is the real problem, find the process or cgroup that grows (`ps --sort=-rss`, `smem`, `slabtop`).

**Q27. What are minor and major page faults?**
A **minor** fault is resolved without disk I/O: the page is already in memory (page cache hit, zero page, copy-on-write). A **major** fault must **read from storage** (a file page or swap). Minor faults are cheap but not free (microseconds at scale); major faults are milliseconds on disk. See them with `ps -o min_flt,maj_flt`, `/usr/bin/time -v`, or `perf stat -e page-faults`. A burst of major faults after a deploy usually means a cold cache or swapping.

**Q28. How do you tell a memory leak from cache growth from fragmentation?**
**Cache growth** shows in `Cached` and is reclaimable (`available` stays high). A **leak** shows as a process's RSS (or PSS) **rising steadily under constant load** and never falling; confirm with a heap profiler (`heaptrack`, `jemalloc` profiling, `valgrind --leak-check`). **Kernel leaks** show in `slabtop` or `/proc/meminfo` `Slab` and `SUnreclaim`. **Fragmentation** shows as high-order allocation failures (`/proc/buddyinfo`, "page allocation failure: order:N" in `dmesg`) while total free memory looks fine.

**Q29. When would you use huge pages, and what do they cost?**
They cut **TLB misses** for large, randomly accessed working sets. Measured in [LX 13](13_tuning_for_latency_and_throughput.md): a 2 GB random pointer chase fell from **254 to 267 ns** to **167 to 174 ns** per load (about **34 percent**) with 2 MB pages; at 4 MB there was no difference. Costs: **transparent** huge pages can cause **allocation and compaction stalls**, and memory bloat when only a little of a huge page is used, which is why many databases recommend `madvise` mode, not `always`. Explicit **hugetlb** pages are reserved up front and never swapped.

**Q30. When is `mmap` better than `read`, and when is it worse?**
`mmap` maps file pages into the address space: access is a memory load, the **page cache is shared** with other processes, and there is **no copy** into a user buffer. It wins for large files read repeatedly or randomly and for sharing between processes. It loses when each access is a cold page fault (one fault per 4 KB can be slower than one large `read`), when I/O errors arrive as **`SIGBUS`** instead of a return code, and when another process **truncates** the file under you.

---

## 5. Storage (Q31 to Q40)

**Q31. `df` says the filesystem is full but `du` finds little. Why?**
The usual cause is **deleted files that a process still holds open**: the name is gone, so `du` cannot see the blocks, but they are not freed until the last descriptor closes. Measured on a small `tmpfs`: `df` **6,144 KB used of 8,192**, `du` **0**; `lsof +L1` named `tail`, link count 0, size **6,291,456**; truncating through `/proc/PID/fd/3` took `df` to **0 KB used** without restarting the process [run S2]. Other causes: files **hidden under a mount point** (unmount to see them), and **reserved blocks** on ext4 (about 5 percent for root by default).

**Q32. "No space left on device" but `df` shows free space. What now?**
Check **`df -i`**: the filesystem may be out of **inodes**. Measured on a `tmpfs` limited to 200 inodes: `df -k` showed **0 percent** blocks used, `df -i` showed **200 of 200 (100 percent)**, and `touch` failed with `No space left on device` [run S3]. Find the directory with millions of small files (`for d in /*; do echo $(find $d -xdev | wc -l) $d; done | sort -n`). Other causes: a **quota**, a `tmpfs` size limit, and on btrfs or XFS **metadata or fragmentation** limits.

**Q33. What does the ext4 journal protect, and what does it not?**
It makes **metadata updates atomic** across a crash: changes are written to the journal first, then in place, and recovery replays or discards them, so the **filesystem structure** stays consistent without a long `fsck`. In the default `data=ordered` mode, file data is written before the metadata that points to it, so you do not see stale data in a new file. It does **not** make your **application's multi-step update atomic**, and it does not guarantee your data reached the disk unless you `fsync`.

**Q34. What is the right way to durably replace a file?**
Write the new content to a **temporary file in the same directory**, `fsync` the file, `rename` it over the old name (atomic on POSIX), then **`fsync` the directory** so the rename itself is durable. `fsync` flushes data and metadata and asks the device to flush its cache; `fdatasync` skips metadata that is not needed to read the data back. Skipping the directory `fsync` can leave you with the old file after a crash.

**Q35. Buffered versus direct I/O.**
Buffered I/O goes through the **page cache**: reads can hit memory, writes return when copied into the cache and are flushed later (so `write` returning means nothing about durability). **`O_DIRECT`** bypasses the cache and requires **aligned buffers, offsets and sizes**; databases use it when they run their own cache and do not want double buffering. Direct I/O is not "faster"; it is **predictable** and avoids the cache's memory use.

**Q36. What is an I/O scheduler and which should you use?**
It orders and merges requests per block device (`/sys/block/DEV/queue/scheduler`). **`none`** is right for fast NVMe, where the device queues are deep and the kernel should stay out of the way; **`mq-deadline`** bounds request latency and suits SSD and many virtual disks; **`bfq`** favours fairness and interactivity on slow rotational disks and desktops; **`kyber`** targets latency on fast devices. Measure before changing; for most servers the default is right.

**Q37. How do you read `iostat -x`?**
Per device: `r/s` and `w/s` (rates), `rkB/s` and `wkB/s` (throughput), **`r_await` and `w_await`** (average time a request spends queued plus served, in ms), **`aqu-sz`** (average queue depth), and **`%util`** (time the device had at least one request in flight). `%util` near 100 means "always busy", which is **saturation on a single-queue disk but not on NVMe**, where many requests run in parallel. High `await` with a long queue means the device is the bottleneck; high `await` with a short queue and low `%util` means the delay is somewhere else (a network disk, a throttled cgroup).

**Q38. LVM and RAID in two minutes.**
**LVM** stacks **physical volumes into a volume group** and carves **logical volumes** out of it, so you can resize, snapshot and move storage online. **RAID 0** stripes (speed, no redundancy), **1** mirrors, **5** and **6** stripe with one or two parity blocks (survive one or two disk failures; slow small writes), **10** stripes over mirrors. **RAID is not a backup**: it does not protect against deletion, corruption written to both copies, or a bad deploy.

**Q39. ext4, XFS, btrfs, tmpfs: how do you choose?**
**ext4**: the dependable default, can shrink, good everywhere. **XFS**: scales well with large files and parallel writers, cannot shrink. **btrfs** (and ZFS): copy-on-write with **checksums, snapshots and compression**, at the cost of complexity and, for some workloads, fragmentation. **tmpfs**: RAM backed, fast, **size and inode limited**, gone on reboot; good for `/run` and scratch, dangerous for anything you need to keep. The tests in this tutorial use small `tmpfs` mounts for exactly the reason that they can be made to fill in a second.

**Q40. The root filesystem is at 100 percent in production. What do you do?**
1. **Free space safely and quickly**: `du -xh --max-depth=1 / | sort -h` to find the growth; old logs, package caches, core dumps, container layers (`docker system prune`), journal (`journalctl --vacuum-size`).
2. **Truncate, do not delete, a log a process is writing** (`: > file`), or you free nothing until it closes the file [run S2].
3. **Check `lsof +L1`** for deleted-but-open files and `df -i` for inodes.
4. **Fix the cause**: rotation, a runaway logger, a missing disk alert.
5. **Add capacity if it is real growth**: LVM extend, then `resize2fs` or `xfs_growfs`, online.

---

## 6. Networking (Q41 to Q50)

**Q41. What happens when you run `curl https://example.com`?**
**Name resolution** (`nsswitch.conf`, then `/etc/resolv.conf` or the local stub resolver; `dig` shows the DNS part), a **route lookup** (`ip route get ADDR`), an **ARP or neighbour lookup** for the next hop (`ip neigh`), the **TCP three-way handshake** (SYN, SYN-ACK, ACK), the **TLS handshake** (`openssl s_client`), then the **HTTP request and response**, then the connection is reused or closed. Naming the **tool that shows each step** is what turns this from a recital into a diagnosis plan.

**Q42. What are `TIME_WAIT` and `CLOSE_WAIT`, and which one is a bug?**
**`TIME_WAIT`** is held by the side that **closed first**, for twice the maximum segment lifetime (60 seconds on Linux), so stray packets from the old connection cannot corrupt a new one on the same four-tuple. Thousands of them are **normal** for a busy client. **`CLOSE_WAIT`** means the **peer closed and your application has not called `close()`**: a **leak in the application**, and a growing count is a bug to chase, with `ss -tnp state close-wait`.

**Q43. What are the SYN queue and the accept queue?**
A new connection first sits in the **SYN queue** (half open) until the handshake finishes, then moves to the **accept queue** until the application calls `accept`. The listen backlog is capped by **`net.core.somaxconn`** (4096 on the VM in LX 13). For a listening socket, `ss -ltn` shows **Recv-Q as the current accept-queue length and Send-Q as its limit**. A full accept queue means the application is **too slow to accept**; look for `ListenOverflows` and `ListenDrops` in `nstat -az`.

**Q44. `ss` versus `netstat`.**
`ss` reads kernel socket tables over **netlink** and is faster, with richer filters (`ss -tnp state established '( sport = :443 )'`) and TCP internals (`-i`, `-m`). `netstat` reads `/proc/net` and is deprecated. Useful `ss` forms: `-ltnp` (listeners and owners), `-s` (summary), `-tmn` (socket memory) [run S5].

**Q45. Ping works but the TCP connection does not. Walk through it.**
Ping proves **IP reachability only**. Next: **is anything listening** (`ss -ltnp` on the server), **is something filtering** (`nft list ruleset`, cloud security group), then **look at the packets** (`tcpdump -ni IF port N` on both ends). **SYN seen, no SYN-ACK** means the packet is dropped or filtered on the way or at the host; **SYN then RST** means the port is closed or a firewall **rejects**. From [LX 12](12_users_ssh_and_hardening.md): a firewall reject answered in **2 ms**, a drop made the same connection wait **3.002 seconds**.

**Q46. Distinguish "connection refused", "timed out", "no route to host" and "network is unreachable".**
**Refused**: a **RST** came back, so the host is up and nothing listens (or a reject rule). **Timed out**: **nothing came back**; a drop rule, a dead host, or a broken return path. **No route to host**: usually an ICMP unreachable or an **ARP/ND failure** on the local segment. **Network is unreachable**: the **local routing table has no route** to that destination at all. Measured: `ping: connect: Network is unreachable` with only `10.9.0.0/24` in the routing table, and a neighbour entry stuck at `INCOMPLETE` while the peer's link was down [run S8].

**Q47. How does Linux choose a route?**
**Longest prefix match** among the routes of the table selected by the policy rules (`ip rule`), then the lowest **metric**. `ip route get ADDR` prints the decision, including the source address and interface it would use; that single command settles most "why does it go out the wrong interface" questions. Multiple tables plus `ip rule` give **policy routing** for multihomed hosts.

**Q48. What is a PMTU black hole?**
A path with a smaller **MTU** than the endpoints assume, where the ICMP "fragmentation needed" messages that would tell the sender are **filtered**. Symptom: **small requests work and large ones hang** (the TLS handshake's big packet, a large response). Test with `ping -M do -s SIZE` (do not fragment) to find the working size; fix by allowing ICMP type 3 code 4, or **clamping the MSS** on the router (`TCPMSS --clamp-mss-to-pmtu`) or lowering the MTU on tunnels.

**Q49. What is `conntrack`, and what happens when its table fills?**
Netfilter's connection tracking keeps a **state entry per flow**, which NAT and stateful firewall rules depend on. The table is bounded (`nf_conntrack_max`); when full, the kernel logs **`nf_conntrack: table full, dropping packet`** and **new connections are dropped** while existing ones continue, which looks like random intermittent failures under load. Fix: raise the limit and its hash size, shorten timeouts, or exempt high-volume traffic from tracking (`notrack`). From memory; not run here.

**Q50. How do you tune TCP for a fast, long link?**
Compute the **bandwidth-delay product**: the bytes in flight needed to keep the link full. From [LX 13](13_tuning_for_latency_and_throughput.md): **10 Gbit/s at 30 ms is 37.5 MB**, so a 128 KB window caps one connection at tens of Mbit/s. Make sure **`tcp_rmem` and `tcp_wmem` maxima** allow that (the VM had 32 MB for receive), keep **autotuning** on (a live connection's receive buffer grew from 131,072 to **1,864,135 bytes**), and choose the **congestion control** for the loss pattern (BBR on this VM; CUBIC is the common default). Then **measure with `iperf3`** instead of believing the arithmetic.


---

## 7. Containers (Q51 to Q60)

**Q51. What is a container, really?**
**An ordinary process (or group of them) with a restricted view and restricted resources**: **namespaces** limit what it can see (processes, mounts, network, hostnames, users), **cgroups** limit what it can use (CPU, memory, I/O, pids), **capabilities, seccomp and a security module** limit what it can do, and a **root filesystem** (usually a stack of overlay layers) is what it sees as `/`. There is **no separate kernel**: the container shares the host's, which is why a kernel bug is a container-escape bug and why `uname -r` inside matches the host. See [LX 09](09_containers_namespaces_and_cgroups.md).

**Q52. Name the namespaces and say what each isolates.**
**mnt** (mount points), **pid** (process ids; the first process is PID 1), **net** (interfaces, routes, ports, firewall), **ipc** (System V and POSIX message queues), **uts** (hostname and domain name), **user** (uid and gid mappings, so root inside can be an unprivileged uid outside), **cgroup** (the view of the cgroup tree), **time** (clock offsets). Inspect with `lsns`, `ls -l /proc/PID/ns`, enter with `nsenter`, create with `unshare`.

**Q53. cgroup v1 versus v2.**
**v1** has a separate hierarchy per controller (cpu, memory, blkio ...), which makes a process's resources hard to reason about together. **v2** has **one unified hierarchy**, cleaner delegation, a "no internal processes" rule, and better controllers (`memory.max` and `memory.high`, `io.max`, **pressure stall information** in `*.pressure`). This VM is a **hybrid** mount: the scenario below uses v1 names (`memory.limit_in_bytes`, `memory.failcnt`, `memory.oom_control`). The v2 equivalents are `memory.max`, `memory.events` (with an `oom_kill` field) and `memory.peak` (from memory).

**Q54. A container shows `OOMKilled` and exit code 137, but the node has plenty of free memory. Why?**
The **cgroup's own limit** was hit, not the node's. The kernel killed a task inside that cgroup when it could not reclaim enough to stay under the limit. Measured: a cgroup limited to 32 MB; a process that wanted 100 MB exited **137**; `memory.failcnt` read **93** (times the limit was hit); `max_usage_in_bytes` read **33,554,432**; `oom_kill` went from 0 to **1**; and `dmesg` printed `constraint=CONSTRAINT_MEMCG` [run S7]. Remember that **page cache counts toward the limit** but is reclaimed first, and that a managed runtime (a JVM heap, a Go heap) may be sized without knowing the cgroup limit.

**Q55. What is CPU throttling in a container, and why does it cause latency at low average CPU?**
A CPU **limit** is a **quota per period** (`cpu.max` in v2, `cpu.cfs_quota_us` and `cpu.cfs_period_us` in v1, typically 100 ms). A multi-threaded process can **burn the whole quota in the first part of the period**, and then every thread is **stopped until the next period**, so requests that arrive in the stopped part wait tens of milliseconds even though the average CPU use is low. Evidence: `cpu.stat` fields `nr_throttled` and `throttled_time`. Mitigation: a **larger limit or no limit with a request** (a weight, not a quota), fewer threads, or CPU pinning. In Kubernetes, a **request** sets the share and a **limit** sets the quota.

**Q56. How does PID 1 behave differently in a container?**
The kernel **does not apply default signal actions to the init of a PID namespace**: a `SIGTERM` is ignored unless the process installed a handler, so `docker stop` waits for its timeout and then sends `SIGKILL`. PID 1 is also **responsible for reaping orphaned children**; if the application does not, zombies accumulate [run S6 shows what one looks like]. Fixes: use an **init** (`docker run --init`, `tini`) as PID 1, or have the entrypoint `exec` the application so it **is** PID 1 and handles signals. A shell-form `ENTRYPOINT` makes `sh` PID 1, and it will not forward `SIGTERM`.

**Q57. How does container networking work?**
Each container gets a **network namespace** with a **veth pair**: one end inside, the other attached to a **bridge** on the host (or to a CNI plugin's data plane). Outbound traffic is **NATed** (`MASQUERADE`) and published ports use **DNAT** rules. The scenario S8 builds the smallest version with `ip netns` and a veth pair, and shows the **first failure to check**: one end left `DOWN`, which looks like `NO-CARRIER` on the other end and an `INCOMPLETE` neighbour entry [run S8]. Host networking removes the namespace entirely.

**Q58. How does overlayfs work, and why is writing to the container filesystem slow and temporary?**
An overlay mount combines **read-only lower layers** (the image) with a **writable upper layer** and presents a merged view. Reading a file finds it in the topmost layer that has it. **Writing to a file that lives in a lower layer first copies the whole file up** to the upper layer (copy-up), which is slow for large files, and the upper layer **disappears with the container**. Put data you care about, and write-heavy data, on a **volume** or bind mount, which bypasses the overlay.

**Q59. How do you debug a container that has no shell or tools?**
Do it from the **host**, using the container's **namespaces**: find the process id, then `nsenter -t PID -n ss -ltnp` runs the host's `ss` inside the container's **network** namespace; `nsenter -t PID -m -p` enters its mount and pid namespaces; `ls /proc/PID/root/` shows its filesystem; `cat /proc/PID/cgroup` finds its cgroup limits. In Kubernetes use **`kubectl debug`** with an ephemeral container that shares the target's namespaces. Do not install debug tools into the production image.

**Q60. What do user namespaces, capabilities and `--privileged` mean?**
In a **user namespace** uid 0 inside maps to an **unprivileged uid outside**, so "root in the container" has no real power on the host's files and devices (this is what rootless containers use). **Capabilities** split root's power into about forty pieces; a container starts with a reduced set, and you should drop the rest (`--cap-drop=ALL`, add back one). **`--privileged`** gives all capabilities, all devices and no seccomp filter: it is **almost no isolation**, and the usual cause of an escape.

---

## 8. Performance (Q61 to Q70)

**Q61. The server is slow. What are your first sixty seconds?**
In this order, reading each result before the next: `uptime` (load trend), `dmesg | tail` (OOM kills, disk errors, link flaps), `vmstat 1 5` (run queue `r`, blocked `b`, swap `si`/`so`, CPU split), `mpstat -P ALL 1 3` (one hot CPU or all), `pidstat 1 3` (which process), `iostat -xz 1 3` (disk), `free -m`, `ss -s`. Run on the 2 CPU VM with three busy loops: `vmstat` showed **`r` 3, `us` 99 to 100, `id` 0** (rows after the first, which is the average since boot), `ps` showed each loop at **64 to 67 percent**, and `pidstat` showed **`%wait` of 26 to 40 percent** per loop: the signature of CPU contention, tasks ready but not running [run S1].

**Q62. Load is high but CPU is idle. What is going on?**
Load counts **tasks in uninterruptible sleep (`D`)**, usually waiting on disk, a network filesystem or a lock. Look at `vmstat`'s **`b`** column and `ps -eo pid,stat,wchan:20,comm | awk '$2 ~ /D/'`, then `iostat -xz` for the device and `/proc/PID/stack` for where each is stuck. The commonest culprit is a **hung NFS mount** or a **saturated or failing disk**. From memory (not reproduced here, because the VM has no slow device to wait on).

**Q63. What does high `iowait` mean?**
It is the share of time a CPU was **idle while at least one task was waiting for I/O**. It is **not time spent doing I/O** and it is **not a cause**: it is a hint that the system waits on a device. A machine can have 90 percent `iowait` and be fine if there is nothing else to run. Confirm with `iostat -x` (`await`, `aqu-sz`) before blaming the disk.

**Q64. How do you use `perf` and what do you need for useful output?**
`perf top` for a live view; `perf record -g -p PID -- sleep 30` then `perf report` for a call-graph profile; `perf stat` for counters (cycles, instructions, cache misses, and the **instructions per cycle** ratio). You need **symbols** (debug info or at least unstripped binaries) and **call graphs** that work (frame pointers, or `--call-graph dwarf` which is heavier). A **flame graph** turns the samples into a picture where width is time. See [LX 10](10_perf_and_flame_graphs.md).

**Q65. What does `strace` cost, and what are the alternatives?**
`strace` uses `ptrace` and **stops the target at every system call**, which can slow a syscall-heavy program by an order of magnitude or more (from memory; do not run it on a hot production process without a reason). Use `strace -c` for a summary, `-e trace=...` to narrow, `-f` for threads. Lower cost: `perf trace`, `bpftrace` and `ftrace` ([LX 11](11_ftrace_ebpf_and_bpftrace.md)), which watch events without stopping the process.

**Q66. What do voluntary and involuntary context switches tell you?**
`pidstat -w` shows both. **Voluntary** switches happen when a task **blocks** (waits for I/O, a lock, a socket), so many of them mean the task is waiting a lot. **Involuntary** switches happen when the scheduler **preempts** a task that was still runnable, so many of them mean **CPU contention**: more runnable tasks than CPUs. A thread pool with high involuntary switches and a tail latency problem is a **thread-count and CPU-count mismatch**.

**Q67. What is CPU steal and when do you check it?**
In a virtual machine, **`%steal`** (`mpstat`, `vmstat st`) is the time the guest wanted to run but **the hypervisor ran something else**. High steal means the **host is oversubscribed** or the VM is throttled, and **nothing you do inside the guest** fixes it. On the VM used here, [LX 13](13_tuning_for_latency_and_throughput.md) noted the same effect in the tails: time the host took away appears in the p99.9 and the maximum, so only large ratios were trusted.

**Q68. How do you read and report a latency measurement?**
**Percentiles, not the mean**: p50, p99, p99.9 and max, plus the **count over the threshold you care about**. **Repeat the run**: three identical runs in LX 13 gave a p99.9 of **579, 1,037 and 1,367 µs**, so a single run proves nothing about a tail. Beware **coordinated omission** (a load generator that waits for each reply hides the requests it would have sent during a stall). State the **conditions** (idle machine, which CPUs, which kernel).

**Q69. How do you find which process uses a port, a file, or disk I/O?**
Port: `ss -ltnp 'sport = :PORT'` (the `users:(("name",pid=N,fd=N))` column) or `lsof -nP -iTCP:PORT -sTCP:LISTEN`; measured with a second server on the same port failing `OSError: [Errno 98] Address already in use` [run S5]. File: `lsof FILE`, `fuser -v FILE`. Disk I/O by process: `pidstat -d 1`, `iotop -o`. Network by process: `ss -tnp`, `nethogs`.

**Q70. List the common causes of tail latency on Linux.**
Garbage collection pauses, **CPU throttling** by a cgroup quota, **page faults** (cold or swapped pages), **THP compaction stalls**, **interrupts and softirqs** landing on the same CPU, **noisy neighbours** and steal, lock contention, and for sleep-based code the **timer slack** (50 µs by default) and **idle exit** (a halted virtual CPU took a median of **99 to 109 µs** to wake a thread, measured in LX 13). The method is the same every time: **measure the distribution, find the biggest contributor, change one thing, measure again**.

---

## 9. Security (Q71 to Q80)

**Q71. Describe the Linux permission model in layers.**
**DAC** (discretionary access control): the owner, group and other mode bits, plus POSIX **ACLs**. **Capabilities** divide root's powers so a process can hold only what it needs. **LSMs** (SELinux, AppArmor) add **mandatory** access control by label or by path: a process can be denied something DAC allows. **seccomp** filters which **system calls** a process may make. **Namespaces and cgroups** limit visibility and resources. A request must pass **every** layer.

**Q72. What are setuid and setgid, and what is the risk?**
A **setuid** executable runs with the **file owner's** effective uid, which is how `passwd` can edit `/etc/shadow` for an unprivileged user. The risk is that **any bug in it is a privilege escalation**, so keep the list short (`find / -xdev -perm -4000 -type f`), mount user-writable filesystems **`nosuid`**, and never make an interpreter script setuid (the kernel ignores setuid on scripts for a reason).

**Q73. What are capabilities, and how do you let a program bind port 80 without root?**
Capabilities split root into units such as `CAP_NET_BIND_SERVICE` (bind ports below 1024), `CAP_NET_RAW`, `CAP_SYS_TIME`, `CAP_SYS_ADMIN` (a large catch-all, treat as root). Give the binary only what it needs: `setcap cap_net_bind_service=+ep /path/to/server`, or `AmbientCapabilities=` in a systemd unit, or lower `net.ipv4.ip_unprivileged_port_start`. Inspect a process's sets in `/proc/PID/status` (`CapEff`, `CapPrm`) and decode with `capsh --decode=HEX`.

**Q74. SELinux versus AppArmor.**
**SELinux** labels every process and object (files, ports) with a **type** and decides access from policy rules on those labels; it is thorough, and its policy is complex. **AppArmor** confines programs by **path** with per-program profiles; it is simpler and weaker against path tricks. Diagnose a denial with `ausearch -m avc` or `journalctl` and `audit2allow` for SELinux, `aa-status` and the kernel log for AppArmor; **never disable the module as the fix**, and use permissive mode to collect denials. From memory (neither is available on this VM).

**Q75. How do you harden SSH?**
Key authentication only (`PasswordAuthentication no`), `PermitRootLogin no` (or `prohibit-password`), restrict users (`AllowUsers` or `AllowGroups`), modern ciphers and key exchange, a short `LoginGraceTime`, `MaxAuthTries`, and rate limiting or `fail2ban` for brute force; put sshd behind a bastion or a VPN where possible. Check the effective configuration with `sshd -T` rather than reading the file: in [LX 12](12_users_ssh_and_hardening.md) it printed **90 settings, 14 of them changed** by the hardening drop-in.

**Q76. What does the `noexec` mount option do, and what does it not do?**
It makes the kernel **refuse `execve` on files from that mount**, even with the execute bit set: measured, `/tmp/lx14m/run.sh` with mode `-rwxr-xr-x` gave `Permission denied`, while `sh /tmp/lx14m/run.sh` **ran fine**, because the shell only reads the file as data [run S9]. So `noexec` blocks casual execution of dropped binaries, but **does not block interpreters** and is **not a security boundary by itself**. Pair it with `nosuid` and `nodev` on `/tmp` and user-writable mounts.

**Q77. How does a Linux packet filter (nftables) work?**
Rules are in **tables**, grouped into **chains** attached to **hooks** in the packet path (`prerouting`, `input`, `forward`, `output`, `postrouting`); each chain has a **policy** (accept or drop) for packets no rule matched. A sound default is **drop on input** with explicit accepts for **established and related** traffic (`ct state`), loopback, and the needed ports. **Drop versus reject**: reject sends an error back so the client fails fast; drop is silent, which hides the host from scanners and makes honest clients **time out** (measured in LX 12: **2 ms** against **3.002 s**).

**Q78. How do you audit who did what on a Linux host?**
`last` and `lastb` and `w` for logins and current sessions, the authentication log (`/var/log/auth.log` or the journal), `ss -tnp` for live connections, and **`auditd`** for system-call and file-watch rules (`auditctl -w /etc/passwd -p wa`). Auditing has a **cost**: in LX 12, a call took **616 to 644 ns** with no rule, **740 to 965 ns** with an unrelated rule loaded, and **25,750 to 41,760 ns** when the rule matched and an event was written.

**Q79. Why is putting a secret on the command line a bad idea?**
Arguments are visible to **every user** in `ps` and in `/proc/PID/cmdline`, and they land in **shell history and logs**. The environment is a little better (`/proc/PID/environ` is readable by the owner and root) but is inherited by every child and appears in crash dumps. Prefer a file with mode `0600` read by the program, a **pipe or file descriptor**, or the service manager's credential feature, and clear it from memory when you can. Verify what leaks with `tr '\0' ' ' < /proc/PID/cmdline`.

**Q80. Name the kernel and userspace hardening features that blunt memory-corruption exploits.**
**ASLR** (`kernel.randomize_va_space=2`) randomises addresses; **NX** marks data pages non-executable; **stack protectors** (canaries) and **fortified libc functions** catch overflows; **RELRO and PIE** protect the GOT and make the binary relocatable; **SMEP and SMAP** stop the kernel executing or touching user pages by mistake; **`kernel.kptr_restrict`**, **`dmesg_restrict`** and **Yama `ptrace_scope`** hide kernel addresses and limit process tracing; **seccomp** shrinks the system-call surface. None is a fix for the bug, each raises the price of exploiting it.

### 9.1 Eight more questions, added after a review (Q81 to Q88)

A read-through of the finished bank found gaps in boot, limits, terminals and mounts, which Linux interviews do ask. Q82 and Q88 were reproduced; the rest are from memory.

**Q81. What happens between pressing the power button and a login prompt?** (from memory)
**Firmware** (UEFI) initialises hardware and runs the **bootloader** from the EFI system partition (GRUB or systemd-boot). The bootloader loads the **kernel and an initramfs** and passes a **command line**. The kernel starts, sets up memory, CPUs and drivers, unpacks the **initramfs** (a small root with the drivers needed to find the real root: storage, RAID, LVM, encryption), mounts the real root and **executes PID 1**. **systemd** (or another init) then reaches its default **target** by starting units in dependency order, and a **getty** or the display manager shows the login. Where it fails tells you the stage: no bootloader menu (firmware or ESP), kernel panic `VFS: unable to mount root` (initramfs or root device), a hang in userspace (a unit, often `fstab`). Tools: `systemd-analyze blame` and `critical-chain`, `journalctl -b`, `dmesg`; recovery with the rescue target or a `rd.break` shell. See [LX 04](04_boot_systemd_and_logs.md).

**Q82. "Too many open files." What is limited, and how do you find a leak?**
Each process has a **soft and a hard `RLIMIT_NOFILE`** (`ulimit -n`; under systemd `LimitNOFILE=`), and the system has a global ceiling (`fs.file-max`). When a process reaches its soft limit, `open`, `socket` and `accept` fail with **`EMFILE`**: measured, with a limit of 12, the next `open` printed `Too many open files`, and both `ulimit -Sn` and `-Hn` read 12 [run Q82, below]. To find a leak, compare `ls /proc/PID/fd | wc -l` against the `Max open files` line of `/proc/PID/limits`, watch the count rise, and group by target (`lsof -p PID | awk '{print $5}' | sort | uniq -c`). A leak is usually a missing `close()` on an error path or a socket never closed (the `CLOSE_WAIT` build-up of Q42).

```text
--- 1. a per-process limit of 12 descriptors
$ ulimit -n
20000
$ bash -c 'ulimit -n 12; for i in $(seq 1 20); do eval "exec $((i+2))</dev/null" || break; done 2>&1 | tail -1'
bash: line 1: /dev/null: Too many open files
$ bash -c 'ulimit -n 12; ulimit -Sn; ulimit -Hn'
12
12
--- 2. where to look for a leak
$ grep 'Max open files' /proc/N/limits | sed -E 's/ +/ /g'
Max open files 20000 20000 files
$ ls /proc/N/fd | wc -l
3
```

**Q83. What happens when you press Ctrl-C, Ctrl-Z or close the terminal?** (from memory)
The **terminal driver** turns the keystroke into a signal sent to the **foreground process group**: Ctrl-C is `SIGINT`, Ctrl-Z is `SIGTSTP` (the job stops; `fg` or `bg` resumes it with `SIGCONT`), Ctrl-\ is `SIGQUIT` (terminate with a core dump). Closing the terminal sends **`SIGHUP`** to the session's processes, which kills a job unless it ignores `SIGHUP` (`nohup`), has been detached from the shell's job table (`disown`), or runs in its own session (`setsid`). Pipelines work because the whole pipeline is one process group.

**Q84. Cron or a systemd timer?** (from memory)
Both run jobs on a schedule. A **timer** is a unit, so it has **dependencies**, **logs in the journal**, resource limits and sandboxing, can catch up after downtime (`Persistent=true`), can spread load (`RandomizedDelaySec=`) and shows its last and next run in `systemctl list-timers`. **Cron** is simple and universal but has a minimal environment (scenario S10), no catch-up and mail-or-lose output. Prefer timers on systemd hosts for anything that matters; either way, use absolute paths and log the output.

**Q85. `/proc`, `/sys` and `sysctl`: what is each for, and how do you make a setting persist?**
`/proc` exposes **process and kernel state** (and tunables under `/proc/sys`); `/sys` exposes the **device and driver model** (block queue settings, CPU and NIC attributes). `sysctl -w name=value` writes `/proc/sys` and **lasts until reboot**; to persist, put it in a file under `/etc/sysctl.d/` and apply with `sysctl --system`. Device attributes under `/sys` persist through **udev rules** or a unit that writes them at boot. A setting you changed live and did not persist is a classic cause of "it worked until the reboot".

**Q86. How do kernel modules and udev fit together?** (from memory)
A **module** is code loaded into the running kernel (`lsmod`, `modinfo`, `modprobe`, which resolves dependencies from the `depmod` map). When hardware appears the kernel emits a **uevent**; **udev** reads rules, loads the matching module (via its alias) and creates the **device node** and symlinks under `/dev`. So "the device is missing" can mean the driver did not load (`dmesg`, `lsmod`), the node was not created (`udevadm monitor`, `udevadm test`), or permissions are wrong (rules, group membership).

**Q87. What is a file descriptor, and what do `fork` and `exec` do to them?** (from memory)
A descriptor is an index into a **per-process table** whose entries point to **open file descriptions** (offset, flags) in the kernel, which point at the **inode** or socket. `dup` and `fork` make two descriptors share one description, so **they share the file offset**. `execve` **keeps descriptors open** unless they are marked **close-on-exec** (`O_CLOEXEC`), which is why a forgotten descriptor leaks into every child, can hold a port or a deleted file open (scenario S2), and why you should open with `O_CLOEXEC` by default.

**Q88. `umount` says "target is busy". What is using it?**
Something holds the filesystem: an open file, a **current working directory**, a mount below it, or a memory mapping. `fuser -vm MOUNT` or `lsof +f -- MOUNT` names the holders. Measured: a process whose working directory was inside a `tmpfs` made `umount` print `target is busy`, `fuser -vm` listed `sleep` with access `..c..` (the `c` means current directory), and after it exited `umount` succeeded [run Q88, below]. Avoid `umount -l` (lazy) as a first move: it hides the mount while holders keep using it.

```text
--- 1. a process whose working directory is inside the mount
$ umount /tmp/lx14m
umount: /tmp/lx14m: target is busy.
$ fuser -vm /tmp/lx14m 2>&1 | sed -E 's/ [0-9]+ / N /'
                     USER        PID ACCESS COMMAND
/tmp/lx14m:          root     kernel mount /tmp/lx14m
                     root        N ..c.. sleep
$ ls -l /proc/N/cwd | awk '{print $NF}'
/tmp/lx14m
--- 2. after the holder exits
$ umount /tmp/lx14m && echo unmounted
unmounted
```


---

## 10. Fifteen troubleshooting scenarios

Each scenario has the same layout: **symptom**, **the first command and how to read it**, **cause and fix**, and a **line to say aloud**. S1 to S10 were reproduced on the VM and the transcripts are real. S11 to S13 are from memory. S14 and S15 point at measured results elsewhere.

### S1. "The server is slow"

**First command.** The first-minute set from section 1.3. Here three busy loops ran on a 2 CPU VM.

```text
--- 1. a slow server: three CPU hogs on a 2 CPU box
$ nproc; cut -d" " -f1-3 /proc/loadavg
2
0.09 0.26 0.74
$ vmstat 1 3 | cut -c1-84
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 4  0      0 7028732   5044 656412    0    0  1667    57  963    6 12  2 85  1  0  0
 3  0      0 7028712   5044 656412    0    0     0     0  514  317 99  0  0  0  1  0
 3  0      0 7028712   5044 656412    0    0     0     0  517  302 100  0  0  0  0
$ ps -eo pid,stat,pcpu,comm --sort=-pcpu | head -5 | sed -E "s/^ *[0-9]+/ N/"
  PID STAT %CPU COMMAND
 N R    67.3 bash
 N R    65.7 bash
 N R    64.1 bash
 N Sl    1.3 claude
$ pidstat -u 1 1 | grep -E "Average|bash" | head -4 | sed -E "s/[0-9]{2}:[0-9]{2}:[0-9]{2}//; s/ +[0-9]+ +/ N /"
 N 19560   66.00    0.00    0.00   34.00   66.00     0  bash
 N 19561   73.00    0.00    0.00   26.00   73.00     0  bash
 N 19562   59.00    0.00    0.00   40.00   59.00     1  bash
Average:      UID       PID    %usr %system  %guest   %wait    %CPU   CPU  Command
```

**Reading.** `vmstat` row two and three: **`r` is 3** (three runnable tasks for two CPUs), `us` is **99 to 100**, `id` is **0**. `ps` gives each loop **64 to 67 percent**, which is exactly two CPUs shared by three tasks. `pidstat` shows the other half of the story: each loop spent **26 to 40 percent of its time in `%wait`**, ready but not running. And the load average read **`0.09 0.26 0.74`**: a one-minute average that has not caught up, because the loops had run for only four seconds. **Trust `vmstat` for now and the load average for trends.** The first `vmstat` row is the average since boot, so skip it.

**Cause and fix.** CPU saturation, not a slow disk or network: the run queue exceeds the CPU count. Find the process (`pidstat`, `top`), then decide: it is a bug (profile it, [LX 10](10_perf_and_flame_graphs.md)), a capacity problem (add CPUs), or a priority problem (`nice`, cgroup weights).

**Say aloud.** "Run queue above CPU count with user time at 100 percent and idle at 0 is CPU saturation. Load average alone would have told me nothing yet."

### S2. "The disk is full but `du` disagrees"

```text
--- 1. fill the disk, delete the file while a process holds it
$ df -k /tmp/lx14m | tail -1 | awk '{print $3" KB used of "$2}'
6144 KB used of 8192
$ du -sk /tmp/lx14m
0       /tmp/lx14m
--- 2. find the culprit
$ lsof +L1 /tmp/lx14m 2>/dev/null | grep -E 'COMMAND|lx14m' | awk '{print $1, $4, $5, $7, $8, $10}'
COMMAND FD TYPE SIZE/OFF NLINK NAME
tail 3r REG 6291456 0 /tmp/lx14m/big
--- 3. truncate it through /proc without killing the process
$ : > /proc/19408/fd/3
$ df -k /tmp/lx14m | tail -1 | awk '{print $3" KB used of "$2}'
tail: /tmp/lx14m/big: file truncated
0 KB used of 8192
```

**Reading.** `df` says **6,144 KB used of 8,192**, `du -sk` says **0**. `lsof +L1` lists files with a **link count of 0** that are still open: `tail` holds `/tmp/lx14m/big` at fd `3r`, size **6,291,456**. The name is gone, the blocks are not.

**Cause and fix.** Someone `rm`ed a file a process still has open (usually a log). Options: **restart or signal the process** so it reopens its log, or **truncate through `/proc`** as shown: `: > /proc/PID/fd/3` took `df` to **0 KB used** and `tail` itself noticed with `file truncated`. Problem A below finds these files **without `lsof`**.

**Say aloud.** "`du` walks names, `df` asks the filesystem. A difference means blocks with no name: deleted but open. I would find them with `lsof +L1` and truncate through `/proc` instead of restarting a production process."

### S3. "No space left on device" with free blocks

```text
--- 1. plenty of space, no inodes
$ for i in $(seq 1 300); do : > /tmp/lx14m/f$i || break; done 2>&1 | tail -1
bash: /tmp/lx14m/f200: No space left on device
$ df -k /tmp/lx14m | tail -1 | awk '{print $3" KB used of "$2" ("$5")"}'
0 KB used of 8192 (0%)
$ df -i /tmp/lx14m | tail -1 | awk '{print $3" inodes used of "$2" ("$5")"}'
200 inodes used of 200 (100%)
$ touch /tmp/lx14m/x
touch: cannot touch '/tmp/lx14m/x': No space left on device
$ ls /tmp/lx14m | wc -l
199
```

**Reading.** The loop stopped at file 200: **`No space left on device`**. `df -k` says **0 KB used (0 percent)**, `df -i` says **200 of 200 inodes (100 percent)**. A single inode is gone for every file, however small. (`ls` shows **199** files, because one inode is the filesystem's root directory.)

**Cause and fix.** Inode exhaustion: millions of tiny files (mail queue, session files, a cache directory). Find the directory (`find` with a count per top-level directory), delete or archive in bulk, and **prevent**: on ext4 the inode count is fixed at `mkfs` time (`-N` or `-i`); XFS allocates dynamically.

**Say aloud.** "ENOSPC has two resources behind it: blocks and inodes. I check both `df` and `df -i` before I conclude anything."

### S4. "A process hangs"

```text
--- 1. a process that hangs: it reads a FIFO nobody writes
$ ps -o stat,wchan:20,comm -p 19513
STAT WCHAN                COMMAND
S    wait_for_partner     cat
$ cat /proc/19513/stack 2>&1 | head -3
[<0>] wait_for_partner+0x5c/0x100
[<0>] fifo_open+0x2d9/0x330
[<0>] do_dentry_open+0x1a6/0x430
$ ls -l /proc/19513/fd | awk '{print $9,$10,$11}' | tail -n +2
0 -> /dev/null
1 -> pipe:[49559]
2 -> pipe:[49559]
$ timeout -s INT 1 strace -o /tmp/st.txt -p 19513 2>/dev/null; head -1 /tmp/st.txt | sed -E 's#/tmp/tmp.5i32luYlPT#DIR#'
openat(AT_FDCWD, "DIR/pipe", O_RDONLY <detached ...>
$ lsof -p 19513 2>/dev/null | awk '$4 ~ /^0/ {print $4, $5, $9}' | sed 's#/tmp/tmp.5i32luYlPT#DIR#'
0r CHR /dev/null
--- 2. release it
$ echo hi > /tmp/tmp.5i32luYlPT/pipe; sleep 0.2; ps -p 19513 >/dev/null || echo process exited
hi
process exited
```

**Reading.** State **`S`**, `WCHAN` **`wait_for_partner`**, and the kernel stack goes `fifo_open` into `do_dentry_open`: the process is blocked **opening a FIFO for reading until a writer appears**. `strace` agrees with the last system call, `openat(AT_FDCWD, "DIR/pipe", O_RDONLY <detached ...>`, that never returned. Its descriptors are `/dev/null` and a pipe (`ls -l` on `fd`, and `lsof` for descriptor 0): it holds nothing else. Writing to the FIFO released it and the process exited.

**Method.** A hung process is **always waiting for something**; the job is to name it. State `D` points at storage or a lock, state `S` with a `WCHAN` of `futex_wait` points at a lock or condition variable, `poll` or `epoll_wait` at an event, `wait_for_partner` at a FIFO, `tcp_recvmsg` at a socket. Then find **who owes the process the event**.

**Say aloud.** "I read the state and wait channel, then the stack, then the system call from strace, and then I look for the other party the process is waiting on."

### S5. "Address already in use"

```text
--- 1. second server on the same port
$ python3 -m http.server 8099 --bind 127.0.0.1 2>&1 | tail -1
OSError: [Errno 98] Address already in use
--- 2. who owns it
$ ss -ltnp 'sport = :8099' | sed -E 's/pid=[0-9]+/pid=N/; s/fd=[0-9]+/fd=N/'
State  Recv-Q Send-Q Local Address:Port Peer Address:PortProcess
LISTEN 0      5          127.0.0.1:8099      0.0.0.0:*    users:(("python3",pid=N,fd=N))
$ lsof -nP -iTCP:8099 -sTCP:LISTEN | awk 'NR>1{print $1, $8, $9}'
python3 TCP 127.0.0.1:8099
```

**Reading.** The second server failed with **`OSError: [Errno 98] Address already in use`**. `ss -ltnp` shows the listener on `127.0.0.1:8099` and, in the last column, **`users:(("python3",pid=N,fd=N))`**: the owner. `lsof` gives the same answer a second way.

**Cause and fix.** Another process owns the port (often **the previous instance of the same service that did not exit**). Stop it, or change the port. Connections in `TIME_WAIT` do not block a listener that sets **`SO_REUSEADDR`** (most servers do); if yours cannot rebind after a restart, that is the cause. Wildcard against specific addresses also matters: `0.0.0.0:80` conflicts with `127.0.0.1:80`.

**Say aloud.** "`ss -ltnp` names the owner. If the owner is a stale copy of my own service I want to know why it did not exit."

### S6. "Zombies are piling up"

```text
--- 1. a parent that never calls wait
$ ps -eo stat,ppid,comm | awk '$1 ~ /^Z/ {print $1, "ppid=N", $3}'
Z ppid=N python3
$ grep -E '^(State|PPid)' /proc/$(ps -eo pid,stat | awk '$2 ~ /^Z/{print $1; exit}')/status | sed -E 's/[0-9]+$/N/'
State:  Z (zombie)
PPid:   N
--- 2. kill -9 on a zombie does nothing; the parent must reap it
$ kill -9 19088; sleep 0.2; ps -o stat= -p 19088
Z
```

**Reading.** `ps` shows state **`Z`** with a parent still alive (the Python process sleeping), `/proc/PID/status` says **`Z (zombie)`**, and **`kill -9` left it in state `Z`** after 0.2 s. A zombie holds a pid and an exit status, nothing else.

**Cause and fix.** The parent forked children and **never called `wait`**. Fix the parent (`waitpid` in a `SIGCHLD` handler, or ignore `SIGCHLD` with `SA_NOCLDWAIT`), or restart the parent, which hands the zombies to init, which reaps them. A few are harmless; thousands can exhaust the **pid limit**. In containers, make sure PID 1 reaps (Q56).

**Say aloud.** "You cannot kill what is already dead. A zombie is a bug in the parent."

### S7. "The container is `OOMKilled`, exit 137"

```text
--- 1. a cgroup limited to 32 MB, a process that wants 100 MB
$ cat /sys/fs/cgroup/memory/lx14/memory.limit_in_bytes
33554432
$ bash -c 'echo $$ > /sys/fs/cgroup/memory/lx14/cgroup.procs; python3 -c "b=bytearray(100*1024*1024); b[::4096]=b\"x\"*len(b[::4096])"'; echo exit=$?
exit=137
--- 2. the evidence the kernel keeps
$ grep -E '^(oom_kill|under_oom) ' /sys/fs/cgroup/memory/lx14/memory.oom_control
under_oom 0
oom_kill 1
$ cat /sys/fs/cgroup/memory/lx14/memory.failcnt
93
$ cat /sys/fs/cgroup/memory/lx14/memory.max_usage_in_bytes
33554432
$ dmesg | grep -E 'Memory cgroup out of memory|oom-kill:' | tail -2 | sed -E 's/^\[ *[0-9.]+\] //; s/pid=[0-9]+/pid=N/; s/task=python3,pid=N,uid=0/task=python3/'
oom-kill:constraint=CONSTRAINT_MEMCG,nodemask=(null),cpuset=/,mems_allowed=0,oom_memcg=/lx14,task_memcg=/lx14,task=python3
Memory cgroup out of memory: Killed process 19230 (python3) total-vm:137956kB, anon-rss:32512kB, file-rss:8112kB, shmem-rss:0kB, UID:0 pgtables:168kB oom_score_adj:0
```

**Reading.** The 32 MB cgroup killed the 100 MB allocator: **`exit=137`**. The kernel's own record: `oom_kill` is **1**, `memory.failcnt` is **93** (the number of times the limit was reached), `max_usage_in_bytes` is **33,554,432** (exactly the limit), and `dmesg` says **`constraint=CONSTRAINT_MEMCG`** and `Memory cgroup out of memory: Killed process ... (python3) ... anon-rss:32512kB`.

**Cause and fix.** The **cgroup** limit, not the node, was hit. Find whether the workload truly needs more (raise the limit, with headroom for cache and runtime overhead), or whether it leaks (profile). In v2 read `memory.events` for the `oom_kill` count and `memory.peak` for the high-water mark.

**Say aloud.** "137 is 128 plus 9, SIGKILL, and the OOM killer sends it, but so does anyone with `kill -9`. The counter and `dmesg` settle which." Problem B proves that both cases give 137.

### S8. "Can't reach the other container"

```text
--- 1. healthy
$ ip netns exec a ping -c1 -W1 10.9.0.2 | grep -E 'transmitted'
1 packets transmitted, 1 received, 0% packet loss, time 0ms
--- 2. one side forgotten down
$ ip netns exec a ping -c1 -W1 10.9.0.2 | grep -E 'transmitted'
1 packets transmitted, 0 received, 100% packet loss, time 0ms
$ ip -n a neigh show dev va
10.9.0.2 INCOMPLETE
$ ip -n a -br link show va
va@if7           DOWN           b6:38:2b:c0:bd:25 <NO-CARRIER,BROADCAST,MULTICAST,UP>
$ ip -n b -br link show vb
vb@if8           DOWN           66:04:68:3b:32:50 <BROADCAST,MULTICAST>
--- 3. up, but no route to another subnet
$ ip netns exec a ping -c1 -W1 10.8.0.2 2>&1 | tail -1
ping: connect: Network is unreachable
$ ip -n a route
10.9.0.0/24 dev va proto kernel scope link src 10.9.0.1
```

**Reading.** Healthy first: **1 received, 0 percent loss**. Then `vb` was set down: ping got **100 percent loss**; the neighbour table said **`10.9.0.2 INCOMPLETE`** (ARP asked, nobody answered); and `va` showed **`DOWN` with flags `<NO-CARRIER,BROADCAST,MULTICAST,UP>`**: administratively up but with **no carrier**, because the peer is down. With the link restored, a different failure: `ping: connect: Network is unreachable`, because the routing table holds **only `10.9.0.0/24`**.

**Cause and fix.** Work layer by layer: **link** (`ip -br link`: carrier), **neighbour** (`ip neigh`: `INCOMPLETE` or `FAILED` means layer 2), **address and route** (`ip addr`, `ip route get`), **filter** (`nft list ruleset`), **service** (`ss -ltnp`). Each failure above lands in a different layer and a different message.

**Say aloud.** "I climb the stack: link, neighbour, route, filter, listener. The error message tells me which rung to start on."

### S9. "Permission denied" on a file that is executable

```text
--- 1. executable bit set, still refused
$ ls -l /tmp/lx14m/run.sh | cut -c1-10
-rwxr-xr-x
$ /tmp/lx14m/run.sh
bash: /tmp/lx14m/run.sh: Permission denied
$ findmnt -no OPTIONS /tmp/lx14m
rw,noexec,relatime,size=1024k
--- 2. the shell can still read it as data
$ sh /tmp/lx14m/run.sh
ran
--- 3. a bad interpreter line, and no interpreter line at all
$ /tmp/bad.sh
bash: /tmp/bad.sh: cannot execute: required file not found
$ bash -c /tmp/nobang
hi
```

**Reading.** The mode is **`-rwxr-xr-x`** and still `bash: /tmp/lx14m/run.sh: Permission denied`. `findmnt` shows the mount has **`noexec`**. `sh /tmp/lx14m/run.sh` works, because the shell reads the file as **data**. Two other messages look similar and differ, as the transcript shows: a missing interpreter reports **`cannot execute: required file not found`**, and a script with no `#!` line is run by bash itself (`hi`).

**Cause and fix.** The mount option, not the file mode. Other `EACCES` causes: a missing `x` on a **directory in the path**, an **LSM denial** (SELinux or AppArmor), a **capability** the process lacks, an **ACL**, or a **read-only** mount. `namei -l PATH` shows the mode of every path component; `findmnt -T PATH` shows the mount it is on.

**Say aloud.** "Permission denied has five or six causes. I check the mode, the path components, the mount options, then the security module, in that order."

### S10. "It works in my shell and fails in cron"

```text
--- 1. interactive shell
$ /tmp/job.sh
PATH has 16 entries, first: /home/claude/.npm-global/bin
HOME=/root SHELL=/bin/bash TERM=linux
go: found
--- 2. what cron gives: PATH=/usr/bin:/bin and almost nothing else
$ env -i PATH=/usr/bin:/bin HOME=/root SHELL=/bin/sh /tmp/job.sh
PATH has 2 entries, first: /usr/bin
HOME=/root SHELL=/bin/sh TERM=unset
go: not found
--- 3. find where a tool lives, then use the absolute path in the crontab
$ command -v go
/usr/local/go/bin/go
```

**Reading.** The interactive shell has a **PATH of 16 entries** (first: `/home/claude/.npm-global/bin`) and `go: found`. With the minimal environment cron provides, **PATH is `/usr/bin:/bin`**, `TERM` is unset, and **`go: not found`**. The tool lives in `/usr/local/go/bin`.

**Cause and fix.** Cron (and systemd units, and containers) start jobs with a **near-empty environment and the wrong working directory**. Use **absolute paths** for every command, set `PATH` at the top of the crontab or script, `cd` explicitly, do not rely on `~/.bashrc`, and send output somewhere you can read (`>> /var/log/job.log 2>&1`). Problem C reproduces the cron environment with `env -i`. From memory: cron mails or discards output depending on `MAILTO`, and `%` means newline in a crontab line.

**Say aloud.** "A job failing only under cron is an environment difference until proven otherwise. I reproduce it with `env -i` and compare."

### S11. "Load is high but CPU is idle" (from memory)

**Symptom.** `uptime` says 30 on a 4 CPU box; `top` says 95 percent idle; the service is slow.

**Command and reading.** `vmstat 1`: a **non-zero `b` column** (tasks in `D`) and **high `wa`**. `ps -eo pid,stat,wchan:24,comm | awk '$2 ~ /D/'` lists them with the kernel function they sleep in. `iostat -xz 1` shows whether a **device** has high `await` and queue depth. `cat /proc/PID/stack` names the code path (an NFS call, a block layer wait, a filesystem lock).

**Cause and fix.** Tasks blocked on **storage**: a saturated or failing disk, a hung NFS or network filesystem mount, a journal commit stall. Fix the device or the mount, and **add timeouts** (`soft,timeo=` on NFS, with care) so that a dead server cannot freeze every process that touches the path.

**Say aloud.** "Load counts D-state tasks. High load with idle CPU means waiting, not computing. I find what they wait on."

### S12. "Packets are being dropped" (from memory)

**Symptom.** Retransmits, slow transfers, UDP loss, or `ping` showing loss on a busy host.

**Command and reading.** Counters at each layer, **from the wire inward**: `ip -s link show IF` (RX and TX `dropped`, `overrun`, `errors`), `ethtool -S IF` (driver counters: `rx_missed`, `rx_no_buffer`), `/proc/net/softnet_stat` (second column: dropped at the CPU backlog; third: `time_squeeze`, the softirq ran out of budget), `nstat -az | grep -i -E 'drop|overflow|prune|collapse|retrans'`, `tc -s qdisc show` (qdisc drops), `ss -tmn` (a socket whose receive buffer is full). The **layer whose counter rises while you test** is the answer.

**Cause and fix.** NIC ring too small (`ethtool -G`), the CPU cannot keep up with one queue (spread with RSS, `netdev_max_backlog`, `netdev_budget`), a **UDP receive buffer** too small (`rmem_max` and `SO_RCVBUF`), a slow application, a congested path, or a firewall. Without `netem` here, none of these was reproduced. LX 13 section 6 covers softirqs and RPS with measurements.

**Say aloud.** "I find the layer where the counters rise: NIC, softirq backlog, qdisc, socket buffer, application. Then I fix that layer."

### S13. "The service keeps restarting" (from memory; no systemd on this VM)

**Command and reading.** `systemctl status NAME` shows **how it ended**: `code=exited, status=1` (the program failed), `code=killed, signal=SEGV` (a crash), `signal=KILL` (OOM or a watchdog), and `Result: start-limit-hit` (it restarted too fast, `StartLimitBurst`). `journalctl -u NAME -b -e` shows its last output. `coredumpctl list` shows crashes.

**Method.** Run the same `ExecStart` **by hand as the unit's user with the unit's environment** (`systemd-run --uid=... -p ...` or `sudo -u`), because the failure is usually the environment (S10), a **missing file or permission** (the unit's `User=`, `ProtectSystem=`), a **port in use** (S5), or an **OOM kill** (S7).

**Say aloud.** "The status line says whether it exited, crashed or was killed. That decides whether I read the log, the core dump or the OOM record."

### S14. "I cannot SSH in, or it is slow"

The measurements are in [LX 12](12_users_ssh_and_hardening.md), section 8 (the firewall) and section 5 (multiplexing): **refused** means a RST (no listener, or a reject rule; answered in **2 ms**), **timed out** means a silent drop (**3.002 s** in that test), and **connection multiplexing** took 20 connections from **4.074 s to 0.145 s**. From memory: a **slow login after the connection is made** is usually reverse DNS or GSSAPI negotiation on the server, and a key refused despite being right is usually `StrictModes` on a writable home directory (LX 12, Problem E). Use `ssh -vvv` to see which phase stalls and `sshd -T` on the server for the effective settings.

### S15. "Latency spikes on a pinned, high-priority thread"

Already measured in [LX 13](13_tuning_for_latency_and_throughput.md): a `taskset`-pinned thread next to roaming burners had a **p99 of 3,037 to 9,483 µs**, inside a **`cpuset` fence 216 to 239 µs**; an idle halted CPU adds a **median of 99 to 109 µs** to a wake-up (a keep-awake spinner brought it to **66 to 69 µs**); a `SCHED_FIFO` spinner starved a lower FIFO task for **3.5 seconds**. Walk through the sources of delay in order and **measure percentiles over repeated runs**.

---

## 11. Common mistakes in interviews

1. **Reaching for a tool before stating the question.** "I would run `strace`" is not a plan. Say what you want to learn.
2. **Answering with a definition and stopping.** Add the mechanism, how you would see it, and what goes wrong.
3. **Trusting load average as a CPU number.** It counts `D`-state tasks and lags (0.09 after four seconds of 100 percent CPU, S1).
4. **Treating exit 137 as "OOM".** It is SIGKILL; prove the cause with `oom_kill` and `dmesg` (S7, Problem B).
5. **Concluding "disk full" from `df` alone.** Check `df -i`, deleted-open files and mount points.
6. **Restarting to fix it.** A restart destroys the evidence and teaches nothing; gather state first (the stack, the descriptors, the counters), then restart if you must.
7. **Disabling the control** (SELinux, the firewall, `noexec`) **as the fix.** Find the denial and write the rule.
8. **Saying "it depends" and stopping.** Say **what** it depends on and which measurement would decide.
9. **Quoting numbers from memory as if measured.** Say "typically" or "in my experience", or better, say how you would measure.
10. **Not saying how you would know the fix worked.** Every fix ends with the same measurement that showed the problem.

---

## 12. Problems

Solve each **on any Linux machine where you are root** (a VM or a container with `mount` allowed). Use small `tmpfs` mounts and `ip netns` so that nothing real is touched. Spend **20 to 30 minutes each**, then compare with section 13.

**Problem A. Deleted-open files without `lsof`.** Create a 3 MB file on a small `tmpfs`, start `tail -f` on it, delete it. Print the **path and size of every deleted-but-open file on the machine** using only `/proc`. *Approach:* every open descriptor is a symlink in `/proc/PID/fd/`; `readlink` on a deleted file ends in `(deleted)`; `stat -L` on the descriptor gives the size of the still-existing inode.

**Problem B. Two kinds of 137.** In a memory cgroup with a 32 MB limit, produce an exit status of **137 twice**: once with `kill -9`, once with a real OOM. Show a command that **tells them apart**. *Approach:* run `bash -c` with its pid written to `cgroup.procs`, kill it from outside, then repeat with a 100 MB allocation, and read `oom_kill` from `memory.oom_control` after each (v1 names; v2 uses `memory.events`).

**Problem C. Reproduce cron's world.** Write the command that runs `/tmp/job.sh` with **only the variables cron sets** (`PATH=/usr/bin:/bin`, `HOME`, `SHELL`, `LOGNAME`), and show that nothing else leaks through. *Approach:* `env -i VAR=value ... command`; prove it by running `env` under the same wrapper.

**Problem D. Who owns port 8098?** Start a listener, then print its **full command line, working directory and state** from the port number alone, with no `lsof`. *Approach:* `ss -ltnpH 'sport = :8098'` gives the pid; `/proc/PID/cmdline` is NUL separated (`xargs -0 echo`), `/proc/PID/cwd` is a symlink, `/proc/PID/status` has the state.

**Problem E. The nightly full disk (written, not run).** A server's `/var` fills to 100 percent every night at about 02:10 and is back to 40 percent by 06:00 with nobody touching it. Describe **what you would collect and in what order**, and give **three hypotheses** with the evidence that would separate them. *Approach:* sample `df` and `du -x` once a minute into a file so the event is on record; during an event run `lsof +L1` and `df -i`; compare against the schedule (`crontab -l`, timers, rotation configuration). Do not guess before you have a timeline.

---

## 13. Answers

**Answer A.** Run in the transcript below (two forms; the second prints the size):

```text
--- A. deleted but open, without lsof
$ ls -l /proc/[0-9]*/fd 2>/dev/null | grep -B0 '(deleted)' | grep lx14m | sed -E 's/.* -> //'
/tmp/lx14m/log (deleted)
$ for f in /proc/[0-9]*/fd/*; do t=$(readlink $f 2>/dev/null); case $t in *'(deleted)') echo $(stat -L -c %s $f) $t;; esac; done | grep lx14m
3145728 /tmp/lx14m/log (deleted)
```

The size is **3,145,728 bytes**, the 3 MB that `df` would be charging for a file `du` cannot see. The first form needs only `ls` and `grep`; the second loops over `/proc/*/fd/*`, tests each link target for the `(deleted)` suffix, and asks `stat -L` for the size of the inode behind it. On a real host drop the `grep lx14m` and sort by size to find the culprit.

**Answer B.** Two processes, one status, two different records:

```text
--- B. exit 137 from two different causes
$ bash -c 'echo $$ > /sys/fs/cgroup/memory/lx14b/cgroup.procs; sleep 30' & P=$!; sleep 0.3; kill -9 $P; wait $P; echo exit=$?
exit=137
$ grep '^oom_kill ' /sys/fs/cgroup/memory/lx14b/memory.oom_control
oom_kill 0
$ bash -c 'echo $$ > /sys/fs/cgroup/memory/lx14b/cgroup.procs; python3 -c "b=bytearray(100*1024*1024); b[::4096]=b\"x\"*len(b[::4096])"'; echo exit=$?
exit=137
$ grep '^oom_kill ' /sys/fs/cgroup/memory/lx14b/memory.oom_control
oom_kill 1
```

Both exits are **137**. `oom_kill` stays **0** after the plain `kill -9` and becomes **1** after the allocation. In a real incident add `dmesg | grep -i 'out of memory'` for the **victim, its size and the constraint** (`CONSTRAINT_MEMCG` for a cgroup limit, `CONSTRAINT_NONE` for the whole machine), and on cgroup v2 read `memory.events`.

**Answer C.**

```text
--- C. what differs between my shell and cron
$ env | wc -l
243
$ env -i PATH=/usr/bin:/bin HOME=/root SHELL=/bin/sh LOGNAME=root env | sort
HOME=/root
LOGNAME=root
PATH=/usr/bin:/bin
SHELL=/bin/sh
```

`env -i` **empties the environment** and the listed variables are the only ones the job sees: four lines, against **243** in the interactive shell here (this sandbox exports a lot). Anything the job needs beyond these must be set in the script or the crontab.

**Answer D.**

```text
--- D. who owns port 8098, with the full command line
$ pid=$(ss -ltnpH 'sport = :8098' | grep -o 'pid=[0-9]*' | cut -d= -f2); xargs -0 echo < /proc/$pid/cmdline; readlink /proc/$pid/cwd; grep -E '^(Name|State)' /proc/$pid/status
python3 -m http.server 8098 --bind 127.0.0.1
/home/claude/lx14
Name:   python3
State:  S (sleeping)
```

The listener's owner, its exact arguments (`python3 -m http.server 8098 --bind 127.0.0.1`), the directory it runs from, and its state: enough to decide whether it is yours and whether to stop it. `ss -p` needs to run as the owner or root to see the process column.

**Answer E (from memory).** Collect first: a **timestamped sampling loop** (`while sleep 60; do date; df -h /var; df -i /var; du -xsh /var/*; done >> /root/var.log`), the cron and timer schedule, and, **during** a spike, `lsof +L1` and the biggest recent files (`find /var -xdev -mmin -30 -size +100M`). Three hypotheses and what separates them:

1. **A nightly job writes a big temporary file and deletes it.** `du` grows with the job and falls when it ends; files are visible by name.
2. **Log rotation leaves deleted-but-open files until a daemon is restarted at 06:00.** `df` high and `du` low during the event; **`lsof +L1` names the writer** (S2).
3. **A backup or snapshot stages data on `/var` and cleans up.** A new directory appears at 02:10 and is gone later; the cron line or timer unit that creates it matches the time.

The fix follows the evidence: stage elsewhere, signal the daemon to reopen logs (`copytruncate` or a `postrotate` hook), and alert on **growth rate** instead of a percentage.

---

## 14. Cheat sheet

**The first minute.**

| Command | What it answers |
| --- | --- |
| `uptime`, `dmesg \| tail` | load trend; OOM kills, disk errors, link flaps |
| `vmstat 1 5` | `r` run queue, `b` blocked, `si/so` swap, `us sy id wa st` |
| `mpstat -P ALL 1 3` | one hot CPU or all; `%steal` |
| `pidstat 1 3`, `pidstat -d`, `pidstat -w` | which process; its I/O; its context switches |
| `iostat -xz 1 3` | `await`, `aqu-sz`, `%util` per device |
| `free -m`, `ss -s` | available memory; socket summary |

**What an error means.**

| Message or code | Usually means | Check |
| --- | --- | --- |
| `No space left on device` | blocks, inodes, quota, or a size-limited tmpfs | `df -h`, `df -i`, `lsof +L1` |
| `Permission denied` | mode, path component, mount option, LSM, capability | `namei -l`, `findmnt -T`, `dmesg` |
| `Address already in use` | another listener owns the port | `ss -ltnp` |
| `Connection refused` | RST: nothing listening, or a reject rule | `ss -ltnp` on the server |
| `Connection timed out` | silent drop, dead host, broken return path | `tcpdump`, `nft list ruleset` |
| `Network is unreachable` | no local route | `ip route get` |
| `Text file busy` | writing to a running executable | `mv` or `install` instead |
| exit **137 / 139 / 143** | SIGKILL / SIGSEGV / SIGTERM (128 plus signal) | `oom_kill`, `dmesg`, core dump |
| state **D** / **Z** | uninterruptible I/O wait / unreaped child | `wchan`, `iostat` / the parent |

**Rules of thumb to say.** Load average counts runnable **and** D-state tasks and lags. `available` is the memory number, not `free`. `du` counts names, `df` counts blocks. 137 is SIGKILL, not always OOM. A zombie cannot be killed, only reaped. A cgroup limit kills inside the cgroup, whatever the node has free. `noexec` blocks `execve`, not interpreters. A drop makes clients time out, a reject makes them fail fast. Measure percentiles over repeated runs before and after any change.

---

## 15. Sources

- Linux man pages, section 7 and 5 and 2: `signal-safety(7)`, `cgroups(7)`, `namespaces(7)`, `capabilities(7)`, `proc(5)`, `sched(7)`, `fork(2)`, `execve(2)`, `fsync(2)`, <https://man7.org/linux/man-pages/>.
- Kernel documentation: cgroup v1 memory controller (`memory.oom_control`, `failcnt`), cgroup v2 (`memory.events`, `cpu.max`), `vm.rst` sysctls, overlayfs, <https://docs.kernel.org/>.
- Brendan Gregg, "Linux Performance Analysis in 60,000 Milliseconds" and the USE method (the first-minute command set).
- Brendan Gregg, *Systems Performance*, 2nd edition (load average, `iostat`, `perf`).
- Michael Kerrisk, *The Linux Programming Interface* (processes, signals, file I/O semantics).
- The experiments in sections 10 and 13: scripts `s1_slow` to `s12_busy` and `ans_a` to `ans_d`, run in this session.
- Earlier tutorials of this plan: [LX 03](03_processes_signals_and_proc.md), [LX 05](05_performance_methodology_and_triage.md), [LX 06](06_memory_swap_and_the_oom_killer.md), [LX 07](07_files_permissions_and_storage.md), [LX 08](08_linux_networking_toolbox.md), [LX 09](09_containers_namespaces_and_cgroups.md), [LX 12](12_users_ssh_and_hardening.md), [LX 13](13_tuning_for_latency_and_throughput.md).
