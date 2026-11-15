# LX 05: Performance methodology and the 60-second triage

*Part of the [Linux plan](00_plan.md) · Time: about 5 hours · You need: [LX 03](03_processes_signals_and_proc.md) (states, load average, `/proc`, PSI first seen), [LX 04](04_boot_systemd_and_logs.md) (cgroups, `dmesg`, the journal) and [OS 05](../05_operating_systems/05_process_scheduling.md) (run queues). The earlier tutorials each taught one tool or one subsystem. This one teaches the **order**: what to look at first, what each number means, and how to say "the saturated resource is X" before you say why.*

"A service is slow. You have a shell on the machine and one minute. What do you run?" is a standard question in infrastructure, SRE and systems loops, and a strong answer is not a list of tools. It is a **method**: take an inventory of resources, check each for **utilisation, saturation and errors**, name the one that is saturated, and only then look for the cause. The weak answer is `top`, then a guess. This tutorial gives the method (section 1), the checklist for Linux (2), the ten commands and how to read each column (3), what saturation looks like for CPU, disk, network and memory with numbers (4), pressure stall information (5), latency against throughput, percentiles and coordinated omission (6), the ways a benchmark lies (7), `sar` as a flight recorder (8), a decision tree from symptom to resource (9), and then the costs, mistakes, questions and five problems, one of which is a working triage script.

**Six honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, about 8 GiB), as **root**, with procps-ng 4.0.4, sysstat 12.6.1, fio 3.36, iproute2, stress-ng 0.17 and gcc 13. The sandbox VM was restarted between LX 04 and this tutorial (`uptime` printed `up 0 min` in the first run), so it is a fresh machine.
2. **The disk is a virtual disk that is very fast** (about 130,000 random 4 KiB writes per second at a queue depth of 32, with latencies under a millisecond), and the host caches under it. A real disk saturates much earlier. Because of that, the tutorial **demonstrates disk saturation by raising the queue depth**, which shows the shape of the effect, not the numbers you will see on a server.
3. **Packet loss and delay with `tc netem` cannot be shown here**: the kernel has no module support and the qdisc is not built in (`tc qdisc add ... netem` failed with `Specified qdisc kind is unknown`). The network section uses a **full listen queue** instead, which is real and measurable. **`perf`, `bpftrace`, `ftrace` and `tcpdump` are not available**, so profiling and tracing are left to LX 10 and LX 11 and any statement about them is from memory.
4. The control groups are **version 1**, so the memory-pressure test uses a v1 memory limit; current systems use version 2.
5. Timings and rates are **single runs** and vary by 10 to 30 percent between runs; the I/O rates also depend on the host.
6. `sar` normally reads files that a timer or `cron` job writes; neither is installed or running here, so section 8 records a file by hand.

By the end you can:

- state the **USE method** (utilisation, saturation, errors for every resource) and the **RED method** for services, and say what each is for;
- run the ten-command triage in order and read each column (measured: `vmstat`'s first line is an average since boot and showed 8 percent user time on a machine that was idle for the rest of the run);
- recognise **CPU saturation** (measured: four busy loops on two CPUs gave a run queue `r` of **4**, 100 percent user time on both CPUs, each loop **47 to 56 percent running and 44 to 53 percent waiting** in `pidstat`, and a 1-minute load average of only **1.18** a few seconds in, which is why the run queue and PSI come first);
- recognise **disk saturation** and say why `%util` is not enough (measured: `%util` was **86 percent** with `w_await` of only **0.08 ms**; sweeping the queue depth 1, 8, 64, 512 gave **19,980, 83,733, 155,796 and 235,728** IOPS at mean latencies of **50, 95, 410 and 2,171 µs**, so throughput grew 11.8 times while latency grew 43 times, and **IOPS equals depth divided by latency** at every point);
- recognise **network queue saturation** (measured: a listener with backlog 2 and 20 connecting clients showed `Recv-Q 3`, `Send-Q 2` in `ss -ltn` and **34 `ListenOverflows`** in `nstat`);
- recognise **memory pressure** (measured: a 64 MB cgroup reading a 200 MB file moved `/proc/pressure/memory` from `total=322` to `total=81623` µs of stall, with 29,205 allocation failures counted by the cgroup and file pages being read back in again);
- read pressure stall information and say why it beats the load average (measured: a load average of **1.18** against CPU PSI `some avg10` of **47.95** during the same CPU fault);
- predict queueing delay (simulated: mean response time at **50, 70, 80, 90, 95 percent** utilisation was **19.9, 33.4, 50.5, 102.2, 196.6 ms** for a 10 ms job, against 20.0, 33.3, 50.0, 100.0, 200.0 from `1/(μ-λ)`);
- say why averages and a closed-loop load generator hide tail latency (measured: one 2 s stall in 10 s gave `p99 = 1.2 ms` to a closed-loop client and **`p99 = 1,920 ms`** to an open-loop client, with means of 1.4 and 226.6 ms), and compute that a page of 20 calls meets a 1-in-100 slow call **18.2 percent** of the time;
- spot a benchmark that measures something else (measured: a 1 GiB `dd` read ran at **3.7, then 7.4 GB/s** from cache and **2.5 GB/s** with `iflag=direct`; a loop whose result is unused took **0.0000 s** at `-O2` and **0.68 s** when used; the same job 12 times spread by **8 percent**);
- record and query `sar` history, and use a decision tree from symptom to resource and a script that names the saturated resource (measured: it flagged CPU, disk and network faults and stayed quiet on an idle machine).

**Where the numbers come from.** Six experiment scripts plus a problem-answer script and a triage script were run in this session on Linux 6.18.44. Transcripts print `$ command` before each command's output. Numbers that are not from these runs are marked "from memory" or come from a source listed at the end.

**Data and timing.** Nothing here needs a data set. The load programs are busy loops, `fio` jobs and small Python files, printed or named where they are used. Each experiment names its duration (for example "4 s each"); treat all rates as ranges.

---

## 1. Method before tools

A tool tells you a number; a method tells you **which numbers to ask for, in what order, and when to stop**. Three bad habits show up in every incident, and Gregg's name for them (from memory of his methodology pages) is worth knowing: **street-light** (look where the tool is bright: run `top` because it is the one you know), **random change** (try a setting, see if it helps), and **blame someone else** (it must be the network, the database). The cure is to make the questions fixed.

### 1.1 The USE method

For **every resource** (CPU, memory, storage devices, network interfaces, and the limits that act like resources: file descriptors, threads, connection slots, a lock), ask three questions:

| Question | Meaning | Example |
| --- | --- | --- |
| **Utilisation** | the fraction of time the resource was busy (or the fraction of its capacity in use) | CPUs 99 percent busy |
| **Saturation** | work that **has to wait** because the resource is busy: a queue | a run queue of 4 on 2 CPUs |
| **Errors** | a count of failures | dropped packets, I/O errors |

Errors are cheap to check and often explain everything at once, so they come first (this ordering advice is from memory). **Saturation is the one that hurts**: a resource at 100 percent utilisation with no queue is fully used but not hurting anyone, while any queue means added latency. A resource can also saturate **below** 100 percent average utilisation, because averages hide bursts and because a device with parallelism (an SSD, a RAID set) is "busy" while it still has room (section 4.2 measures this). The method is a **checklist over a resource inventory**, which is why it finds problems that you did not think to look for.

### 1.2 The RED method, and the others

For a **service** (not a resource) the equivalent is **RED** (from memory, Tom Wilkie): **R**ate (requests per second), **E**rrors (failed requests per second), **D**uration (how long requests take, as a distribution). A dashboard with those three per service says whether users are hurt; USE then finds which resource explains it. Two companions: **workload characterisation** (who is sending load, what kind, how much: the cheapest way to find a problem is to remove load that should not be there) and **drill-down** (start broad, pick the most suspicious part, go one level in, repeat). The fetched SREcon page for the ten-command checklist does not describe these methods; the definitions here are from memory.

**The order the tutorial follows:** inventory and the 60-second checklist (sections 2 and 3), name the saturated resource (4 and 5), then characterise the latency and the load (6), then drill into the cause with the tools of the later tutorials (profilers, tracing, memory and network tools).

---

## 2. The USE checklist for Linux

The fetched Linux USE checklist (Gregg) lists these metrics; the last column is what this tutorial measured.

| Resource | Utilisation | Saturation | Errors | Measured here |
| --- | --- | --- | --- | --- |
| CPU | `vmstat 1` (`us+sy+st`), `mpstat -P ALL 1`, `pidstat 1`, `top` | `vmstat 1` run queue `r` against CPU count, `sar -q` (`runq-sz`), `/proc/PID/schedstat`, `perf sched` | processor error events (`perf`) | sections 4.1, 5 |
| Memory capacity | `free -m`, `vmstat 1`, `sar -r` | `vmstat 1` `si` and `so`, `sar -B` (scans), `sar -W`, OOM kills in `dmesg` | `dmesg` hardware messages, failed allocations | 4.4, 5 |
| Storage device I/O | `iostat -xz 1` (`%util`), `sar -d`, `pidstat -d` | `iostat` `aqu-sz` and `await` | `ioerr_cnt`, `smartctl` | 4.2 |
| Network interface | `sar -n DEV 1`, `ip -s link`, `/proc/net/dev` | drops and overruns, `netstat -s` retransmits, `sar -n EDEV` | interface error counters | 4.3 |
| Storage capacity | `df -h`, `swapon -s` | none defined: a full disk returns `ENOSPC` | `ENOSPC` in `strace` and logs | [LX 03](03_processes_signals_and_proc.md) section 11 |

Three notes that the table cannot hold. **First**, the checklist names tools of its time: on a current machine `ss -s`, `ss -ti` and `nstat` (all in `iproute2`) replace `netstat`, and PSI (section 5) gives a direct saturation figure for CPU, memory and I/O that the 2013 list did not have. **Second**, software resources are resources: a thread pool, a connection pool, a mutex and the file descriptor limit of [LX 03](03_processes_signals_and_proc.md) each have the three questions (how much used, how many waiting, how many failed), and "waiting on a lock" is saturation of a resource that no kernel counter shows. **Third**, for **storage capacity** the checklist itself questions whether saturation applies, because the failure is an error, not a queue.

---

## 3. The 60-second checklist: ten commands in order

The fetched SREcon 2016 page lists them (`uptime`, `dmesg -T | tail`, `vmstat 1`, `mpstat -P ALL 1`, `pidstat 1`, `iostat -xz 1`, `free -m`, `sar -n DEV 1`, `sar -n TCP,ETCP 1`, `top`). Each run below was done **on a quiet machine**, to learn the columns; section 4 reads the same commands under load.

```text
--- 1 uptime: load averages (1, 5, 15 minutes)
$ uptime
 21:16:05 up 0 min,  0 user,  load average: 0.09, 0.03, 0.01

--- 2 dmesg: the last kernel messages, with readable times
$ dmesg -T | tail -3 | cut -c1-90
[Sun Nov 15 21:15:19 2026] EXT4-fs (vda): mounted filesystem d9cc69ef-aefc-4eeb-862b-3188a
[Sun Nov 15 21:15:19 2026] EXT4-fs (vdc): mounted filesystem 00000000-0000-4000-8000-00000
[Sun Nov 15 21:15:19 2026] EXT4-fs (vdd): mounted filesystem 00000000-0000-4000-8000-00000

--- 3 vmstat 1 3: r b swpd free buff cache si so bi bo in cs us sy id wa st
$ vmstat 1 3
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 1  0      0 7349800  20440 408668    0    0  6732  1925  765    5  8  3 87  1  0  0
 0  0      0 7349752  20440 408740    0    0     0    12   92   68  0  0 100  0  0  0
 0  0      0 7349752  20440 408740    0    0     0     0   40   38  0  0 100  0  0  0

--- 4 mpstat -P ALL 1 1: per CPU
$ mpstat -P ALL 1 1 | tail -4
Average:     CPU    %usr   %nice    %sys %iowait    %irq   %soft  %steal  %guest  %gnice   %idle
Average:     all    0.00    0.00    0.00    0.00    0.00    0.50    0.00    0.00    0.00   99.50
Average:       0    0.00    0.00    0.00    0.00    0.00    0.00    0.00    0.00    0.00  100.00
Average:       1    0.00    0.00    0.00    0.00    0.00    0.99    0.00    0.00    0.00   99.01

--- 5 pidstat 1 1: per process (an idle machine shows only the average rows of the busy ones)
$ pidstat 1 1 | tail -4

21:16:08      UID       PID    %usr %system  %guest   %wait    %CPU   CPU  Command

Average:      UID       PID    %usr %system  %guest   %wait    %CPU   CPU  Command

--- 6 iostat -xz 1 2: per device, idle devices hidden by -z
$ iostat -xz 1 2 | sed -n "/Device/,\$p" | tail -3 | cut -c1-120
Device            r/s     rkB/s   rrqm/s  %rrqm r_await rareq-sz     w/s     wkB/s   wrqm/s  %wrqm w_await wareq-sz



--- 7 free -m
$ free -m
               total        used        free      shared  buff/cache   available
Mem:            8024         650        7177          16         419        7373
Swap:              0           0           0

--- 8 sar -n DEV 1 1: network interfaces
$ sar -n DEV 1 1 | grep -E "IFACE|Average" | head -4
21:16:10        IFACE   rxpck/s   txpck/s    rxkB/s    txkB/s   rxcmp/s   txcmp/s  rxmcst/s   %ifutil
Average:        IFACE   rxpck/s   txpck/s    rxkB/s    txkB/s   rxcmp/s   txcmp/s  rxmcst/s   %ifutil
Average:           lo      0.00      0.00      0.00      0.00      0.00      0.00      0.00      0.00
Average:         ifb0      0.00      0.00      0.00      0.00      0.00      0.00      0.00      0.00

--- 9 sar -n TCP,ETCP 1 1: TCP rates and errors
$ sar -n TCP,ETCP 1 1 | grep -E "active|atmptf|Average" | head -4
21:16:11     active/s passive/s    iseg/s    oseg/s
21:16:11     atmptf/s  estres/s retrans/s isegerr/s   orsts/s
Average:     active/s passive/s    iseg/s    oseg/s
Average:         0.00      0.00      0.00      0.00

--- 10 top: the header and the first processes
$ top -bn1 | head -12 | cut -c1-90
top - 21:16:12 up 0 min,  0 user,  load average: 0.08, 0.03, 0.01
Tasks:  72 total,   1 running,  69 sleeping,   0 stopped,   2 zombie
%Cpu(s):  0.0 us,  0.0 sy,  0.0 ni,100.0 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st
MiB Mem :   8024.1 total,   7177.5 free,    650.8 used,    419.5 buff/cache
MiB Swap:      0.0 total,      0.0 free,      0.0 used.   7373.3 avail Mem

  PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND
    1 root      20   0   35148   7444   6912 S   0.0   0.1   0:00.35 process_a+
    2 root      20   0       0      0      0 S   0.0   0.0   0:00.00 kthreadd
    3 root      20   0       0      0      0 S   0.0   0.0   0:00.00 pool_work+
    4 root       0 -20       0      0      0 I   0.0   0.0   0:00.00 kworker/R+
    5 root       0 -20       0      0      0 I   0.0   0.0   0:00.00 kworker/R+
```

Reading each, with the question it answers:

1. **`uptime`**: the load averages for 1, 5 and 15 minutes ([LX 03](03_processes_signals_and_proc.md) section 8). Read it as a **trend**: 1 minute above 15 minutes means the load is rising now. It counts tasks that are runnable **or** in `D` state, so it is not CPU use, and it lags: it is a damped average updated about every 5 s.
2. **`dmesg -T | tail`**: the last kernel messages with times. Errors here (OOM kills, file system errors, link flaps, a driver reset) can explain the whole problem in a second; this is the **errors** column of USE for most resources. On this VM the last lines are file system mounts.
3. **`vmstat 1`**: one line per second. **The first line is an average since boot**, not the last second: here it says `us 8 sy 3 id 87` on a machine that was idle for the next two lines (`id 100`). Columns: `r` runnable tasks (the **CPU run queue**, saturation if above the CPU count), `b` tasks in uninterruptible sleep, `swpd free buff cache` memory, `si so` swap in and out (**memory saturation**, should be 0), `bi bo` blocks in and out, `in cs` interrupts and context switches, `us sy id wa st gu` CPU split (user, system, idle, I/O wait, stolen by the hypervisor, guest). `st` above a few percent in a VM means the host is giving your vCPU away.
4. **`mpstat -P ALL 1`**: the same split **per CPU**. One CPU at 100 percent with the others idle is a **single-threaded bottleneck**, and an average of 50 percent on two CPUs hides it. `%iowait` is idle time while I/O is outstanding (a tasks-waiting measure, not a device one), `%soft` is softirq time (networking), `%steal` is stolen time.
5. **`pidstat 1`**: per **process** CPU, which shows who. The header row alone prints on an idle machine (nothing used CPU in that second). Its `%wait` column is the time a task spent **runnable but not running**, per-process saturation (section 4.1).
6. **`iostat -xz 1`**: per device: `r/s w/s` operations, `rkB/s wkB/s` throughput, `r_await w_await` average latency in ms, `aqu-sz` the **average queue size**, `%util` the fraction of time at least one request was outstanding; `-z` hides idle devices (here no device was active, so none printed). As with `vmstat`, the first report is since boot; read the later ones.
7. **`free -m`**: `available` is the number to read, not `free`: it estimates how much can be given to programs without swapping (page cache that can be dropped counts as available). Here 7,373 of 8,024 MiB. No swap is configured on this VM.
8. **`sar -n DEV 1`**: per-interface packets and kilobytes per second and `%ifutil`. The first rows kept here are `lo` and `ifb0`, both with nothing moving.
9. **`sar -n TCP,ETCP 1`**: TCP **connection rates** (`active/s` outgoing, `passive/s` incoming, segments in and out) and **errors** (`retrans/s` retransmitted segments, `atmptf/s` failed attempts, `orsts/s` resets sent). Retransmits are the cheap sign of a lossy path.
10. **`top`**: the overview that confirms what the others said. The header here shows 72 tasks and 2 zombies ([LX 03](03_processes_signals_and_proc.md) section 2).

The commands are in this order because the first three give the **errors, the trend and the resource split**, the next three narrow to a CPU, a process and a disk, and the last four check memory, the network and the picture as a whole. **Say the output aloud as a sentence**: "load is rising, the run queue is 4 on 2 CPUs, both CPUs are at 100 percent user, three processes each get half a CPU: CPU saturation by user code."


---

## 4. Saturation, measured: CPU, disk, network and memory

For each resource below the procedure is the same: make the fault, run the commands of section 3, and write the sentence. The faults are deliberately simple so that the signature is clear.

### 4.1 CPU saturation: four busy loops on two CPUs

```text
--- fault 1: four busy loops on two CPUs (CPU saturation)
$ vmstat 1 3 | tail -3
 6  0      0 7402180  21764 415528    0    0  3093 36783 2568   20 16  6 77  1  0  0
 4  0      0 7402180  21764 415528    0    0     0     0  514  569 100  0  0  0  0  0
 4  0      0 7402180  21764 415528    0    0     0     0  515  535 99  0  0  0  1  0
$ mpstat -P ALL 1 1 | grep -E "Average:" | awk "{print \$2, \$3, \$5, \$12}"
CPU %usr %sys %idle
all 99.00 0.00 0.00
0 98.02 0.00 0.00
1 100.00 0.00 0.00
$ pidstat 1 1 | grep Average | head -6 | cut -c1-80
Average:      UID       PID    %usr %system  %guest   %wait    %CPU   CPU  Comma
Average:        0       736   48.04    0.00    0.00   51.96   48.04     -  bash
Average:        0       737   55.88    0.00    0.00   44.12   55.88     -  bash
Average:        0       738   47.06    0.00    0.00   51.96   47.06     -  bash
Average:        0       739   47.06    0.00    0.00   52.94   47.06     -  bash
$ cat /proc/pressure/cpu | head -1
some avg10=47.95 avg60=17.45 avg300=4.63 total=16765630
$ uptime | sed "s/.*load/load/"
load average: 1.18, 0.32, 0.11
```

- **`vmstat`**: `r` is **4** on 2 CPUs (the second and third lines; the first shows the 6 of the moment the loops started) and `id` is 0: four tasks are runnable at all times and two can run. That is the CPU saturation signature: **`r` above the CPU count**.
- **`mpstat`**: both CPUs at 98 to 100 percent `%usr`, `%sys` 0. User code is burning the CPU; the kernel is not the cause.
- **`pidstat`**: each of the four loops shows **47 to 56 percent `%usr` and 44 to 53 percent `%wait`** (about half and half). Each loop wants a whole CPU, gets half, and spends the other half **runnable and waiting**. This is saturation **per process**, and `%wait` is the column that shows it; `top` alone shows each at about 50 percent and does not say why.
- **`/proc/pressure/cpu`**: `some avg10=47.95`: for about 48 percent of the last 10 s at least one runnable task was waiting (section 5).
- **`uptime`**: load averages **1.18, 0.32, 0.11**, taken a few seconds after the loops started. The load average **lags** a sudden fault by minutes (it is a damped average, [LX 03](03_processes_signals_and_proc.md) section 8), so `uptime` first can say "nothing is wrong" in the first minute of a real CPU problem. Problem B measures a load of 0.35 with a run queue of 4 five seconds into the same fault.

**How long does a runnable task wait?** The kernel keeps the answer per task: field 2 of `/proc/PID/schedstat` is the **time spent waiting on a run queue**, in nanoseconds.

```text
--- CPU: how long a runnable task waits for a CPU (field 2 of /proc/PID/schedstat is run-queue wait in ns)
one loop, two CPUs:  ran 2.97 s, waited 0.00 s
five loops, two CPUs (after 3 more s): ran 4.21 s, waited 1.76 s
```

With one loop and two CPUs the loop ran 2.97 s and waited **0.00 s**. After four more loops joined (five on two CPUs), the same loop had waited **1.76 s** during the next 3 s while running 1.24 s: it spent about 59 percent of that interval **ready but not on a CPU**. This is the run-queue latency that `perf sched latency` and the `runqlat` tool of the BPF collection (from memory) report as a histogram. **Utilisation of 100 percent says nothing; the wait says how bad it is.**

### 4.2 Disk saturation: queue depth, latency and why `%util` misleads

```text
--- fault 2: random 4 KiB synchronous writes with fio on the root disk (disk saturation)
$ iostat -xz 1 2 | awk "/^Device/ {n++; for (i=1;i<=NF;i++) h[i]=\$i} n==2 && /^vda/ {for (i=1;i<=NF;i++) if (h[i] ~ /^(w\\/s|wkB\\/s|w_await|aqu-sz|%util)\$/) printf \"%s=%s \", h[i], \$i; print \"\"}"
w/s=147285.15 wkB/s=589140.59 w_await=0.08 aqu-sz=11.39 %util=86.34
$ vmstat 1 2 | tail -1 | awk "{print \"r=\" \$1, \"b=\" \$2, \"bo=\" \$10, \"wa=\" \$16}"
r=0 b=0 bo=593288 wa=0
$ cat /proc/pressure/io
some avg10=0.22 avg60=0.56 avg300=0.25 total=1086915
full avg10=0.20 avg60=0.53 avg300=0.23 total=1043523
$ grep -E "IOPS|lat \(usec\)|99.00th" /var/tmp/lx05_fio.out | head -4 | cut -c1-100
  write: IOPS=130k, BW=508MiB/s (533MB/s)(4064MiB/8001msec); 0 zone resets
    clat (usec): min=50, max=22389, avg=240.80, stdev=190.69
     lat (usec): min=64, max=22391, avg=245.77, stdev=193.37
     | 99.00th=[  660], 99.50th=[  725], 99.90th=[ 1385], 99.95th=[ 2073],
```

A `fio` job doing random 4 KiB writes with `O_DIRECT` at a queue depth of 32 for 8 s gave about **130,000 IOPS** and **508 MiB/s** here, with a mean latency of 246 µs and a 99th percentile of 660 µs. In `iostat -x`: `aqu-sz` **11.4** (on average 11 requests were in flight), `w_await` **0.08 ms**, and **`%util` 86.34**. The rest of the picture: `vmstat` shows `bo` (blocks out) 593,288 and `wa` 0, and **PSI for I/O is almost zero** (`some avg10=0.22`).

This is the lesson of the disk section: **`%util` of 86 percent here means "at least one request was outstanding 86 percent of the time", not "the device is 86 percent used"**. A device that serves many requests in parallel (an SSD, a RAID set, a virtual disk) can be busy by that definition long before it is slow. The man page for `iostat` makes this point (from memory), and it is why this tutorial teaches **`aqu-sz` and `await`** as the saturation metrics, and `%util` only as a hint.

The way to see real saturation is to raise the offered load and watch the **latency**, not the throughput:

```text
--- disk: the same random 4 KiB direct writes at queue depth 1, 8, 64, 512 (one fio job, 4 s each)
depth        IOPS  mean_lat_us     p99_us
1           19980           50         83
8           83733           95        185
64         155796          410       1155
512        235728         2171       8159
```

| Queue depth | IOPS | Mean latency | 99th percentile | depth ÷ latency |
| --- | --- | --- | --- | --- |
| 1 | 19,980 | 50 µs | 83 µs | 20,000 |
| 8 | 83,733 | 95 µs | 185 µs | 84,200 |
| 64 | 155,796 | 410 µs | 1,155 µs | 156,100 |
| 512 | 235,728 | 2,171 µs | 8,159 µs | 235,800 |

Two facts. **Little's law**: the number of requests in the system equals the arrival rate times the time each spends there, so `IOPS = depth ÷ mean latency`, and the last column agrees with the measured IOPS to within a few percent at every depth. It is also what lets you compute one of `aqu-sz`, `IOPS` and `await` from the other two (Problem A checks it on `iostat` output: 134,986 writes per second × 0.05 ms is 6.7 against an `aqu-sz` of 6.80). **The knee**: going from depth 64 to 512 (8 times more offered load) bought only 51 percent more IOPS and cost 5.3 times the latency and 7.1 times the 99th percentile. Past the knee extra queue depth is **pure waiting**. This is also the shape of every saturated resource, and it is what the queueing section (6.1) explains.

On a rotating disk or a network-attached volume the knee comes at a depth of a handful and a latency of tens of milliseconds; the logic is the same and the numbers differ by orders of magnitude (from memory).

### 4.3 Network saturation: the listen queue

A server that calls `listen(fd, 2)` and never `accept`s, with 20 clients connecting at once, is the TCP version of "workers all busy":

```text
--- fault 3: a server that does not accept() with a backlog of 2, and 20 clients (network queue saturation)
$ ss -ltn "sport = :19099" | cut -c1-60
State  Recv-Q Send-Q Local Address:Port  Peer Address:PortPr
LISTEN 3      2          127.0.0.1:19099      0.0.0.0:*
$ nstat -z TcpExtListenOverflows TcpExtListenDrops | awk "{print \$1, \$2}"
#kernel
TcpExtListenOverflows 34
TcpExtListenDrops 34
```

`ss -ltn` for a **listening** socket reinterprets two columns: `Recv-Q` is the **number of connections waiting to be accepted** and `Send-Q` is the **backlog limit**. Here `Recv-Q 3` against `Send-Q 2`: the queue is **over its limit** (it holds one more than the backlog, a kernel detail from memory). The overflow counters confirm it: `nstat` printed **`TcpExtListenOverflows 34`** and **`TcpExtListenDrops 34`**, which are the number of times a connection was refused or dropped because the accept queue was full. A client sees a connection that hangs and then retries (the SYN is dropped and retransmitted after a delay, from memory), so the symptom is **latency spikes of seconds**, not errors, and CPU use is low: the application is not saturated in CPU, it is saturated in **accept rate**.

`nstat` prints the **change since its last call** by default (`-n` marks "now" without printing; `-a` shows absolute values, which includes everything since boot and misleads). For interface-level saturation: `sar -n DEV` (rates and `%ifutil`), `ip -s link` (drops, overruns), `ss -ti` (per-connection `retrans` and `rtt`, from memory) and `sar -n EDEV`. Packet loss and delay emulation with `netem`, the usual fault for this section, is **not available on this kernel** (limit 3), so no loss or retransmission experiment is shown.

### 4.4 Memory saturation: reclaim and refault, seen as pressure

On a machine with enough memory nothing shows. The signature of memory saturation is **work to get memory**: scanning, reclaim, swapping, refaults and, at the end, the OOM killer. The test uses a **64 MB cgroup** and a process that reads a **200 MB file** in a loop, so that the page cache of the group must be recycled all the time:

```text
--- memory: PSI when a cgroup is squeezed (cgroup v1 memory limit 64 MB, a process that touches 200 MB over a file)
$ grep -E "^some" /proc/pressure/memory
some avg10=0.00 avg60=0.00 avg300=0.00 total=322
$ grep -E "^(some|full)" /proc/pressure/memory
some avg10=0.45 avg60=0.09 avg300=0.02 total=81623
full avg10=0.45 avg60=0.09 avg300=0.02 total=80534
$ grep -E "^(total_cache|total_rss|total_pgmajfault|total_workingset_refault)" $cg/memory.stat
total_cache 63963136
total_rss 0
total_rss_huge 0
total_workingset_refault_anon 0
total_workingset_refault_file 5273600
total_pgmajfault 0
$ cat $cg/memory.max_usage_in_bytes $cg/memory.failcnt
67108864
29205
```

- `/proc/pressure/memory` went from `total=322` to **`some total=81623`**: **81.6 ms** of stall time during the 5 s test (`full total=80534`: for almost all of it **every** non-idle task in the machine was stalled, because only one task was running).
- The cgroup's `memory.stat` shows `total_cache 63,963,136`: the group sat right at its 64 MB limit; `total_workingset_refault_file 5,273,600`: **5.3 MB of file pages were evicted and then read back in** (a refault means the page was needed again soon after eviction: a working set larger than the memory). `memory.failcnt` was **29,205**: the number of times the group hit its limit and had to reclaim.
- `vmstat` `si/so` and `free` would show swap traffic on a machine with swap; this VM has none (so the 0 here means "no swap", not "no pressure"). On a host with swap, **sustained `so` above 0 is the memory saturation signal**, and `sar -B` (`pgscand/s`, direct reclaim scans) shows the same one level down (from the fetched checklist).

The stall is small because the host's cache serves the re-reads at memory speed; on a slow disk the same refaults would be I/O waits, and the PSI figure would be large. The OOM kill (a `Memory cgroup out of memory` line in `dmesg`) is the extreme end and the subject of [LX 06](00_plan.md).

---

## 5. Pressure stall information

PSI (from the fetched kernel documentation) is a per-resource measure of **lost time**: the share of wall-clock time that tasks were stalled for want of CPU, memory or I/O. The files are `/proc/pressure/cpu`, `memory` and `io`; each has a `some` line and a `full` line, in the form `avg10=… avg60=… avg300=… total=…`:

- `avg10`, `avg60`, `avg300` are **percentages** of time stalled, averaged over the last 10, 60 and 300 seconds;
- `total` is the **absolute stall time in microseconds**, which lets you compute any interval yourself (the memory test above used it: 81,623 µs);
- **`some`**: at least one task was stalled on the resource; **`full`**: **all** non-idle tasks were stalled at the same time, so the CPU time was wasted (a workload that spends time in `full` is thrashing);
- the CPU file has a `full` line for compatibility (reported as zero at the system level, since kernel 5.13, per the page), so on this VM `cpu` shows `some` meaningfully and `full` as zero.

Why it is better than the load average: it **separates the resource** (the load average mixes runnable and uninterruptible tasks), it is a **percentage of lost time** that you can compare, and it has short windows. What it measured here: CPU `some avg10` of **47.95** in the four-loop fault, memory `some` total of 81.6 ms in the cgroup test, I/O `some avg10` of **0.22** in the disk fault. That last number is the point of section 4.2 once more: the fast disk was busy and **not stalling anyone**.

**Two cautions.** **`avg10` decays slowly**: in the first triage-script run of Problem E, taken 15 s after a CPU fault had stopped, the CPU `some avg10` still read 4.13 while the run queue `r` read 0.0. Use the **run queue for "now"** and PSI for "how much, over how long", or read the `total` counter twice. **Triggers**: you can open a pressure file and write a threshold (for example `some 150000 1000000`: 150 ms of stall in a 1 s window) and wait with `poll`, `select` or `epoll` for an event (fetched); this is how a user-space daemon reacts to memory pressure before the OOM killer acts (from memory, `systemd-oomd` and Android's `lmkd` do it). Per-cgroup PSI files (`cpu.pressure`, `memory.pressure`, `io.pressure`) exist on cgroup v2 (from memory), so a pressured **service** can be named; this VM's hybrid layout has only the global files.


---

## 6. Latency, throughput and what to report

### 6.1 Why latency explodes before the resource is full

A server with random arrivals and random service times builds a queue even when it is **on average** not full. The simplest model (M/M/1, one server, exponential arrivals and service times) gives a mean response time of `1/(μ-λ)`, where `μ` is the service rate and `λ` the arrival rate; the mean time in the system is the service time divided by `(1 - utilisation)`. Simulated, for a service time of 10 ms:

```text
--- one server, mean service time 10 ms: response time against utilisation (simulation, 400,000 jobs each)
utilisation     mean ms  theory ms     p99 ms theory p99
50%                19.9       20.0       92.5       92.1
70%                33.4       33.3      154.1      153.5
80%                50.5       50.0      233.6      230.3
90%               102.2      100.0      449.7      460.5
95%               196.6      200.0      857.8      921.0
99%               778.7     1000.0     2273.8     4605.2
```

At **50 percent** utilisation a 10 ms job takes 20 ms on average (double); at **90 percent** 102 ms; at 95 percent 197 ms. The simulation matches the formula to within 2 percent up to 95 percent (the 99 percent row did not converge in 400,000 jobs: 779 against 1,000 ms, because at that load the queue takes far longer to reach its long-run behaviour). The **99th percentile is about 4.6 times the mean** (the formula `ln(100)` times the mean, 92 against 20 ms at 50 percent), so a service whose mean looks fine at 50 percent has a tail of nearly 100 ms. The consequences, which are what the method is for:

- **Plan for 60 to 70 percent**, not 100: the delay grows without bound as utilisation approaches 1, and bursts (which the model does not include) make it worse.
- **Latency is the early warning**; throughput keeps rising through the knee (section 4.2) and hides it.
- **Every queue adds**: a request that crosses five services, each at 80 percent, waits in five queues.

### 6.2 Percentiles, not averages

A latency set that is 990 values of 10 ms and 10 values of 1,000 ms (Problem D):

Problem D computes it: `mean=19.9 p50=10 p99=10 p99.9=1000 max=1000` (ms). The **mean (19.9 ms) describes nobody**: 99 percent of requests took 10 ms, 1 percent took 1,000. The median and 99th percentile are both 10 ms, and only the **99.9th** shows the problem. So: report **a distribution** (p50, p90, p99, p99.9, max), never a mean alone; a histogram or a table of percentiles is the minimum. The standard tool for recording one cheaply is HdrHistogram (from memory).

**The tail is amplified by fan-out** (the point of the paper "The Tail at Scale", whose abstract I fetched; the arithmetic below is mine). If a page needs 20 independent calls and each is slow 1 percent of the time, the page meets at least one slow call with probability `1 - 0.99^20`, which Problem D computes as **18.2 percent**. At 100 calls it is 63 percent. At scale, **a 1-in-100 event is the normal experience of a user**, which is why the p99 and p99.9 of a backend matter more than its median.

### 6.3 Coordinated omission: the benchmark that lies by waiting

A load generator can produce wrong tail numbers without any bug in the server. The test: a server answers in about 1 ms, and **once**, at 3 s into a 10 s test, it stalls for 2 s. Two clients measure it:

- a **closed-loop** client sends a request, waits for the answer, sends the next (the usual way to write a quick benchmark);
- an **open-loop** client has a schedule (100 requests per second) and measures each request's latency from the time it **should have been sent**.

```text
--- a 2 s stall at t=3 s in a 10 s test: what a closed-loop and an open-loop (100 requests/s) client report
closed loop  n= 7260  mean=    1.4  p50=   1.1  p99=    1.2  p99.9=    1.5  max= 2000.2  (ms)
open loop    n= 1000  mean=  226.6  p50=   1.2  p99= 1920.2  p99.9= 2000.4  max= 2000.4  (ms)
```

The closed-loop client reports `mean 1.4 ms`, **`p99 1.2 ms`** and `p99.9 1.5 ms`, and the only trace of the stall is `max 2000.2`. It sent 7,260 requests, almost all in the good periods, and the one request that hit the stall was **one sample out of 7,260**; during those 2 s the client sent nothing, so the 200 requests that **would have been sent** are missing from the data. The open-loop client reports **`p99 1,920 ms`**, `mean 226.6 ms` and the p50 unchanged at 1.2 ms: it counted the 200 requests that were due during the stall and that waited behind it, each for the remaining time of the stall. **Both numbers are "correct"; only the second is what users experience.** The fetched ScyllaDB article gives the definition and the remedy: the measuring system unintentionally coordinates with the system under test and so avoids measuring outliers; the fix is a schedule of intended send times and latency measured from the **intended** time, with missing samples filled in as HdrHistogram's expected-interval recording does; its example, a YCSB run, had an uncorrected p99 of about 249 µs and a corrected one of about 665 ms.

Rule: when a latency benchmark's p99 is better than you believe, check whether it is closed-loop, and whether the stall (garbage collection, a compaction, a failover) appears in its max but not its percentiles.

---

## 7. How a benchmark lies

The fetched page on **active benchmarking** (Gregg) states the cure: run the benchmark for a long time in a steady state and **analyse the system with the other tools while it runs** (`vmstat`, `iostat`, `mpstat`, the USE method) to find what limits it, and read its configuration and source before you trust it. The failure it names, **passive benchmarking**, is running a benchmark, ignoring it, and presenting the number; the listed ways a benchmark measures something else are: perturbation by other activity (neighbours on shared hardware), throttling by software resource controls (the cgroup limits of [LX 04](04_boot_systemd_and_logs.md)), throttling by the network, a single-threaded benchmark program, different software versions compared by accident, **raw disk I/O measured when file system I/O was meant**, and an unrealistic workload. Three of them, measured:

### 7.1 The page cache

```text
--- the page cache: write 1 GiB, then read it back cold and warm
$ dd if=/dev/zero of=/var/tmp/lx05_f bs=1M count=1024 2>&1 | tail -1
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.362691 s, 3.0 GB/s
$ dd if=/dev/zero of=/var/tmp/lx05_f bs=1M count=1024 conv=fsync 2>&1 | tail -1
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.855959 s, 1.3 GB/s
$ dd if=/var/tmp/lx05_f of=/dev/null bs=1M 2>&1 | tail -1
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.29349 s, 3.7 GB/s
$ dd if=/var/tmp/lx05_f of=/dev/null bs=1M 2>&1 | tail -1
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.144821 s, 7.4 GB/s
$ dd if=/var/tmp/lx05_f of=/dev/null bs=1M iflag=direct 2>&1 | tail -1
1073741824 bytes (1.1 GB, 1.0 GiB) copied, 0.425324 s, 2.5 GB/s
```

A 1 GiB write with `dd` reported **3.0 GB/s**: that is the speed of **copying into the page cache**, not of the disk; adding `conv=fsync` (wait until it is on the device) gave **1.3 GB/s**. Reading it back after dropping the cache gave **3.7 GB/s**; reading it again at once gave **7.4 GB/s** (cache), and `iflag=direct` (bypass the cache) gave **2.5 GB/s**. Here the "cold" read is itself fast because the **host** caches the virtual disk, so the guest's cold-to-warm ratio is only 2 and you must not expect that on real hardware, where it is usually larger (from memory). The point stands: **a throughput number without a statement of what was cached is not a measurement of the device.** For file system benchmarks: choose the file size well above memory, or drop caches and say so, use `O_DIRECT` when measuring a device, and say which one.

### 7.2 The compiler

```c
/* dead.c: a benchmark whose result is never used; the compiler removes the work */
#include <stdio.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(void) {
    const long n = 1000000000L;
    double t0 = now();
    long sum = 0;
    for (long i = 0; i < n; i++) sum += i ^ (i >> 3);
    double t1 = now();
    printf("result unused:  %.4f s\n", t1 - t0);        /* sum is never printed: -O2 deletes the loop */

    t0 = now();
    volatile long sink = 0;
    long s2 = 0;
    for (long i = 0; i < n; i++) s2 += i ^ (i >> 3);
    sink = s2;                                           /* the result is used: the loop must run */
    t1 = now();
    printf("result used:    %.4f s  (sum %ld)\n", t1 - t0, sink);
    return 0;
}
```

```text
--- the compiler: the same loop with the result unused and used (gcc -O2)
$ /var/tmp/lx05_dead
result unused:  0.0000 s
result used:    0.6817 s  (sum 504403734230768384)
$ /var/tmp/lx05_dead0
result unused:  2.3393 s
result used:    2.2648 s  (sum 504403734230768384)
```

At `-O2` the loop whose result is never used took **0.0000 s**: the compiler removed it, because nothing observes its effect; the same loop with the result stored in a `volatile` variable took **0.68 s**. At `-O0` both took about 2.3 s, because nothing is optimised. A micro-benchmark that does not consume its result measures **nothing**, and one compiled without optimisation measures the compiler's worst code. For a CPU benchmark: use the result, compile as production does, and inspect the generated code (or the counters) if a number looks too good. Any time that is a few cycles per iteration is suspect.

### 7.3 One run is not a measurement

```text
--- one number: the same job, 12 times
$ python3 $D/spread.py
runs (ms): 168 167 166 166 167 174 172 173 180 178 176 176
first 168   min 166   median 173   mean 172   max 180   spread (max/min) 1.08   CV 3.0%
```

The same 170 ms job, twelve times: min 166, median 173, max 180 ms, a spread of 1.08 times and a coefficient of variation of **3 percent**. This machine was quiet; a loaded or shared one gives far more. Report **the number of runs and the spread** (minimum and median, or a confidence interval), discard or flag the first run (warm-up: caches, JIT, frequency), and compare two builds with enough runs that the difference exceeds the spread. For a latency, record the distribution (section 6), not one number.

**A benchmark checklist** (synthesis of the above): what is it supposed to measure, what **actually** limits it (check with `vmstat`, `iostat`, `mpstat` while it runs), is the workload realistic (sizes, access pattern, concurrency, think time), is the result used, is the cache state stated, how many runs and what spread, what changed besides the thing under test, and does the p99 survive an open-loop client.

---

## 8. `sar`: the flight recorder

Every tool above shows **now**. When the slowness was at 03:00, you need history, and `sar` (sysstat) is the standard answer: a collector (`sadc`) writes counters to a binary file at an interval, and `sar -f FILE` reads any metric back, by time window. On a normal installation the collection is started by a `systemd` timer or a `cron` job (every 10 minutes, one file per day under `/var/log/sa` or `/var/log/sysstat`, from memory; neither is running on this VM). Here the file is recorded by hand: 12 s of CPU, run queue, memory and network counters, with **three busy loops started at second 4** and lasting 4 s.

```text
--- the file is binary and holds every metric recorded
$ ls -l $f | awk "{print \$5, \"bytes\"}"
86624 bytes
```

```text
--- CPU history (-u): the spike is visible after the fact
$ sar -f $f -u | sed -n "3,14p" | awk "{print \$1, \$3, \$5, \$8}" | column -t
21:20:55  %user  %system  %idle
21:20:56  0.00   0.00     100.00
21:20:57  0.00   0.00     100.00
21:20:58  0.50   0.00     99.50
21:20:59  0.00   0.50     99.50
21:21:00  98.02  0.50     0.00
21:21:01  99.50  0.00     0.00
21:21:02  98.51  0.00     0.00
21:21:03  98.99  0.00     0.00
21:21:04  0.00   0.00     100.00
21:21:05  0.50   0.50     99.01
21:21:06  0.00   0.00     100.00
```

```text
--- run queue and load (-q): runq-sz against load average
$ sar -f $f -q | sed -n "3,14p" | awk "{print \$1, \$2, \$3, \$4, \$5}" | column -t
21:20:55  runq-sz  plist-sz  ldavg-1  ldavg-5
21:20:56  0        119       0.51     0.56
21:20:57  0        119       0.51     0.56
21:20:58  0        119       0.51     0.56
21:20:59  4        121       0.47     0.55
21:21:00  3        124       0.47     0.55
21:21:01  3        123       0.47     0.55
21:21:02  3        123       0.47     0.55
21:21:03  0        117       0.47     0.55
21:21:04  0        117       0.43     0.54
21:21:05  0        117       0.43     0.54
21:21:06  0        117       0.43     0.54
```

```text
--- one window of the day, other metrics from the same file
$ sar -f $f -r | sed -n "3,5p" | awk "{print \$1, \$2, \$3, \$4, \$5}" | column -t
21:20:55  kbmemfree  kbavail  kbmemused  %memused
21:20:56  7597304    7595572  373804     4.55
21:20:57  7597304    7595572  373800     4.55
```
After the fact, the same file answers different questions: `-u` shows `%user` at **98.02, 99.50, 98.51 and 98.99** for four consecutive seconds (idle 0.00) and about 0 on both sides; `-q` shows `runq-sz` **4, 3, 3, 3** during those seconds and 0 before and after (the three loops, plus one more task in the first sample); and, instructively, `ldavg-1` read **0.51 before, 0.47 during and 0.43 after**: the 1-minute load average **did not rise at all** for a four-second 100 percent CPU saturation of both CPUs. The load average and any per-minute dashboard would have **missed this event**; the per-second CPU and run queue did not. The lessons: **the sampling interval decides what you can see** (10-minute `sar` data cannot show a 4 s spike), the **window** (`-s 03:00:00 -e 03:30:00`) and the day's file (`-f /var/log/sysstat/saDD`) are how you ask about last night, and the metric families to know are `-u`/`-P` (CPU), `-q` (queue and load), `-r` (memory), `-B` and `-W` (paging, swapping), `-d` (disks), `-n DEV,EDEV,TCP,ETCP` (network) and `-S` (swap space), the last from memory.

---

## 9. From symptom to resource: a decision tree

Start from what the user sees, find the resource that is **saturated** (or erroring), and then the process that causes it. Each branch uses a measurement from this tutorial.

```text
"it is slow" (a service, a command, a machine)
|
+-- 1. errors first: dmesg -T | tail; journalctl -p err -b; sar -n EDEV; interface and disk error counters
|        found one (OOM kill, I/O error, link flap, driver reset)  -->  that is the lead; stop here
|
+-- 2. is something waiting on the CPU?   vmstat 1: r > CPUs ? mpstat -P ALL: one CPU at 100 ? cat /proc/pressure/cpu
|        yes, us high    -->  user code: pidstat 1 (who), then a profiler (later tutorials)
|        yes, sy high    -->  system calls or interrupts: pidstat, mpstat %soft, strace -c
|        st high (VM)   -->  the host is taking the CPU: not your code, move or ask the platform
|        one CPU only    -->  a single-threaded bottleneck or an interrupt on one core (affinity)
|
+-- 3. is something waiting for memory?   free -m (available), vmstat si/so, sar -B, PSI memory, dmesg | grep -i oom
|        so > 0 sustained / PSI memory some > 5  -->  memory saturation: who? (RSS in ps, smaps), a leak or a limit
|        cgroup limit hit (failcnt, 'Memory cgroup out of memory')  -->  a limit, not the machine
|
+-- 4. is something waiting for the disk?   iostat -xz 1: aqu-sz and await rising (not %util alone); vmstat b and wa; PSI io; pidstat -d
|        await up, queue deep  -->  device saturation: who (pidstat -d), what pattern (random small, sync writes), is it the log or the DB
|        await fine, wa high   -->  the waiting is elsewhere (network file system, a lock): look at D-state wchan (LX 03)
|
+-- 5. is something waiting on the network?   sar -n DEV (rate against the link), sar -n TCP,ETCP (retrans), ss -ltn (Recv-Q > Send-Q), nstat
|        listener overflow / retransmits / drops  -->  accept rate, a lossy path, a full ring or socket buffer
|
+-- 6. nothing saturated, still slow   -->  it is waiting on something that is not a resource counter:
         a lock, a downstream service, a DNS timeout, a retry sleep, a thread pool:  strace -f -T, ss -tnp, the app's own metrics (RED), wchan
```

**Rules of use.** Name the resource before the cause (this tutorial's one sentence). Check the **trend** (is it rising?) and **who changed** (a deploy, a config change, new load). If two resources are saturated, the downstream one is usually the cause (a slow disk makes the CPU "busy" waiting in the kernel). If **no** resource is saturated and users are slow, the delay is in a queue that no OS counter sees; that is where the RED metrics and tracing take over.

Problem E turns the first four branches into a script that samples for four seconds and prints one line per resource.

---

## 10. What it costs: the numbers worth remembering

Measured on one VM; quote them as ranges.

| Quantity | Value |
| --- | --- |
| four busy loops on two CPUs | `vmstat` r = 4, each loop 47 to 56% running and 44 to 53% waiting, CPU PSI `some avg10` 47.95 |
| load average a few seconds into that fault | 1.18 (and 0.35 in another run): it lags |
| one runnable task, two CPUs, five tasks competing | waited 1.76 s of the next 3 s |
| random 4 KiB direct writes, queue depth 32 | about 130,000 IOPS, 508 MiB/s, 246 µs mean, `%util` 86%, `w_await` 0.08 ms, I/O PSI 0.22 |
| IOPS and mean latency at depth 1, 8, 64, 512 | 19,980 at 50 µs; 83,733 at 95 µs; 155,796 at 410 µs; 235,728 at 2,171 µs |
| depth 64 to 512 | +51% IOPS, 5.3 times the mean latency, 7.1 times the p99 |
| listener with backlog 2 and 20 clients | `Recv-Q 3`, `Send-Q 2`, 34 `ListenOverflows` |
| 64 MB cgroup reading 200 MB | memory PSI total 322 to 81,623 µs, 29,205 limit hits, 5.3 MB refaulted |
| M/M/1, 10 ms job: mean at 50, 70, 80, 90, 95% utilisation | 19.9, 33.4, 50.5, 102.2, 196.6 ms |
| p99 against mean for that queue | about 4.6 times |
| 2 s stall in a 10 s run: closed-loop p99, open-loop p99 | 1.2 ms, 1,920 ms (means 1.4 and 226.6 ms) |
| a page of 20 calls, each slow 1% of the time | 18.2% slow |
| `dd` 1 GiB write, then with `conv=fsync` | 3.0 GB/s, 1.3 GB/s |
| `dd` read: cold, warm, `iflag=direct` | 3.7, 7.4, 2.5 GB/s |
| loop with unused result at `-O2`, used result, `-O0` | 0.0000 s, 0.68 s, 2.3 s |
| the same 170 ms job, 12 runs | min 166, median 173, max 180 ms; CV 3% |
| `sar` 4 s CPU spike on both CPUs | `%user` 98 to 99.5 for 4 samples; 1-minute load 0.51, 0.47, 0.43 (no rise) |

---

## 11. Common mistakes

1. **Starting with `top`** and a guess (section 1): run the checklist in order, errors first.
2. **Reading `%util` as "how full the disk is"** (section 4.2): look at `aqu-sz` and `await`, and at the I/O pressure.
3. **Reading the first line of `vmstat` or `iostat`** (section 3): it is an average since boot.
4. **Trusting the load average to detect a short fault** (sections 4.1 and 8): it is damped and lags; read `r` and PSI.
5. **Reading CPU as one number**: an average of 50 percent on two CPUs can be one CPU at 100 percent (`mpstat -P ALL`).
6. **Looking at utilisation without saturation** (section 1.1): 100 percent with no queue is fine; 60 percent with a queue is not.
7. **Reading `free` instead of `available`** (section 3), and reading "no swap activity" as "no memory pressure" on a machine with no swap (section 4.4).
8. **Reporting a mean latency** (section 6.2), or a p99 from a closed-loop generator (section 6.3).
9. **Benchmarking the cache** (section 7.1), **the compiler** (7.2) or **one run** (7.3).
10. **Running a stress test and not watching the system** (section 7): passive benchmarking.
11. **Calling `nstat -a`** and reading counters since boot as if they were a rate; use the deltas (section 4.3).
12. **Treating a hung `connect` as a network failure**: a full accept queue (section 4.3) looks like latency, not like an error.
13. **Adding queue depth, threads or connections to a saturated resource**: it adds waiting (section 4.2).
14. **Optimising the thing that is not saturated**: the checklist exists to stop this.
15. **Believing PSI `avg10` is "now"** (section 5): it decays.

---

## 12. Questions out loud

Say each answer in 30 to 60 seconds, with one command or one measured number.

1. **A service is slow and you have one minute on the box. What do you run?** The ten commands, in order: `uptime`, `dmesg -T | tail`, `vmstat 1`, `mpstat -P ALL 1`, `pidstat 1`, `iostat -xz 1`, `free -m`, `sar -n DEV 1`, `sar -n TCP,ETCP 1`, `top`; I name the saturated resource first, then the process.
2. **What is the USE method?** For every resource: utilisation, saturation, errors. Saturation (a queue) is the one that adds latency; errors are cheap to check first.
3. **What is the RED method?** Rate, errors, duration for a service; it says whether users are hurt, and USE says which resource.
4. **How do you tell CPU saturation from high CPU use?** The run queue: `vmstat` `r` above the CPU count, or `pidstat` `%wait` high (measured: four loops on two CPUs, r = 4, each about half running and half waiting), and CPU PSI.
5. **What is wrong with `%util` in `iostat`?** It is the fraction of time with a request outstanding; a parallel device is "busy" long before it is slow (measured: `%util` 86, `await` 0.08 ms). Read `aqu-sz` and `await`.
6. **Explain Little's law and use it.** In-flight work equals rate times time in system; so IOPS equals queue depth over latency (measured at depth 1, 8, 64, 512, within a few percent), and `aqu-sz` equals `IOPS × await`.
7. **Where is the knee?** Where more offered load stops buying throughput and only buys latency (measured: depth 64 to 512 gave 51 percent more IOPS for 5.3 times the latency).
8. **Why does latency rise before the resource is full?** Random arrivals and service times build a queue: at 50 percent utilisation a 10 ms job averages 20 ms, at 90 percent 102 ms (simulated).
9. **Why not report the mean?** It describes nobody: 990 requests of 10 ms and 10 of 1,000 ms have a mean of 19.9 ms; give p50, p99, p99.9.
10. **Why does fan-out make the tail matter?** A page of 20 calls with each slow 1 percent of the time is slow 18.2 percent of the time.
11. **What is coordinated omission?** A closed-loop load generator slows down with the system it tests and omits the requests it would have sent during a stall; measure from the intended send time (measured: p99 1.2 ms against 1,920 ms for the same stall).
12. **What does the load average miss?** Anything short and anything resource-specific: it lags, mixes CPU and `D`-state tasks, and in my test a 4 s saturation of both CPUs did not raise it (0.51, 0.47, 0.43).
13. **What is PSI?** Percent of time tasks were stalled on CPU, memory or I/O, `some` or `full`, over 10, 60 and 300 s, plus a total in microseconds; I can set a trigger and `poll` it.
14. **How do you spot memory saturation on Linux?** Swap-out rate, reclaim scans (`sar -B`), refaults, PSI memory, OOM kills in `dmesg`; with a limit, the cgroup's failure count and memory PSI (measured: 81.6 ms of stall and 29,205 limit hits for a 64 MB group).
15. **A listener hangs under load and CPU is idle. What do you check?** `ss -ltn`: `Recv-Q` above `Send-Q`, and `nstat` `ListenOverflows` (measured 34): the accept queue is full.
16. **Why can a disk benchmark be wrong?** The page cache (3.0 GB/s write against 1.3 with fsync, cache reads of 7.4 GB/s), the wrong layer, or no steady state; use `O_DIRECT` for the device and say what was cached.
17. **What is active benchmarking?** Watching the system with `vmstat`, `iostat`, `mpstat` while the benchmark runs, to see what limits it and whether it measures what you meant.
18. **How would you find what happened at 03:00 last night?** `sar -f` over that day's file with `-s` and `-e`; the interval limits what it shows (a 10 minute interval cannot show a 4 s spike).
19. **Nothing is saturated and users are slow. Now what?** The delay is in a software queue no OS counter sees (a lock, a pool, a downstream call, a retry): RED metrics, `strace -f -T`, tracing.


---

## Problems

**Where to solve them.** On any Linux machine with `bash`, `vmstat`, `sysstat` (`iostat`, `mpstat`, `pidstat`, `sar`), `iproute2` (`ss`, `nstat`), `fio` and Python 3, as root for the disk and cache tests; the faults are CPU loops, `fio` and a socket that never calls `accept`. **Run your solution and compare with the stated result** before reading the answer.

**How to approach them.** Start with the question: which resource, and which of utilisation, saturation or errors does the answer need? Make the fault, read the command that shows it, and write the one-sentence diagnosis.

### Problem A (E, entry level): Little's law on a disk

Run a random-write `fio` job at queue depth 16 for 6 s. From `iostat -x` take `w/s`, `w_await` and `aqu-sz`, and check that `aqu-sz` is about `w/s × w_await / 1000`.

### Problem B (E, entry level): the load average that is not the run queue

Start four busy loops on a 2-CPU machine. After 5 s print the load average, the CPU pressure and the run queue, and say which of the three describes the situation right now.

### Problem C (M, medium): find the listener that is overflowing

A server listens with a backlog of 2 and does not accept; 12 clients connect. Write one pipeline over `ss -ltn` that prints every listener whose accept queue is above its backlog, and exits with status 0 only when it found one.

### Problem D (M, medium): percentiles and the page that waits for 20 calls

A file holds 1,000 latencies: 990 of 10 ms and 10 of 1,000 ms. Print the mean, p50, p99, p99.9 and max, and the probability that a page of 20 independent calls includes at least one of the slow kind (each call slow with probability 0.01).

### Problem E (H, hard): the 60-second triage as a script

Write a script that samples for about 4 seconds and prints **one line per resource** (CPU, memory, disk, network) saying `SATURATED` or `ok`, from signals you chose (run queue against CPU count, queue depth, listen-queue overflows, pressure). Test it on a quiet machine and on a CPU fault, a disk fault and a network fault; it must be silent about the wrong resources.

---

## Answers

The answers were run, and the checks are in the output.

```bash
#!/usr/bin/env bash
# ans_prob.sh: the answers to Problems A to E, each run and checked
. "$(dirname "$0")/h.sh"
export LC_ALL=C S_COLORS=never
D=$(dirname "$(readlink -f "$0")")
echo '=== Problem A: Little'"'"'s law on a real disk (queue depth 16, 6 s)'
fio --name=l --filename=/var/tmp/lx05_a --size=512m --rw=randwrite --bs=4k --iodepth=16 --ioengine=libaio --direct=1 --runtime=6 --time_based --output=/dev/null >/dev/null 2>&1 &
sleep 2
iostat -x 1 3 | awk '/^Device/ {for (i=1;i<=NF;i++) h[i]=$i; n++} n==3 && /^vda/ {for (i=1;i<=NF;i++) f[h[i]]=$i; printf "w/s=%s w_await=%s ms aqu-sz=%s\n", f["w/s"], f["w_await"], f["aqu-sz"]; printf "w/s x w_await / 1000 = %.1f\n", f["w/s"] * f["w_await"] / 1000}'
wait; rm -f /var/tmp/lx05_a
echo
echo '=== Problem B: a load average far below the run queue while the CPUs are saturated'
for i in 1 2 3 4; do bash -c 'while :; do :; done' & done
sleep 5
show 'uptime | sed "s/.*load/load/"'
show 'awk "\$1 == \"some\" {print \"cpu PSI\", \$2}" /proc/pressure/cpu'
show 'vmstat 1 2 | tail -1 | awk "{print \"run queue r =\", \$1}"'
pkill -f 'while :; do :; done'; wait 2>/dev/null
echo
echo '=== Problem C: find the overloaded listener'
python3 - <<'PY' &
import socket, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 19098)); s.listen(2); time.sleep(4)
PY
sleep 0.5
python3 - <<'PY'
import socket, time
cs = []
for i in range(12):
    c = socket.socket(); c.setblocking(False)
    try: c.connect(("127.0.0.1", 19098))
    except BlockingIOError: pass
    cs.append(c)
time.sleep(1)
PY
show 'ss -ltn | awk "NR > 1 && \$2 > \$3 {print \"listener over its backlog:\", \$4, \"Recv-Q\", \$2, \"Send-Q\", \$3; f = 1} END {exit !f}"; echo "exit status: $?"'
wait
echo
echo '=== Problem D: percentiles, and how many page views see the tail'
awk 'BEGIN {for (i = 0; i < 990; i++) print 10; for (i = 0; i < 10; i++) print 1000}' > /var/tmp/lx05_lat
show 'sort -n /var/tmp/lx05_lat | awk "{a[NR] = \$1; s += \$1} END {printf \"mean=%.1f p50=%d p99=%d p99.9=%d max=%d\\n\", s / NR, a[int(NR * 0.50)], a[int(NR * 0.99)], a[int(NR * 0.999)], a[NR]}"'
show 'awk "BEGIN {printf \"a page of 20 calls hits at least one slow call: %.1f%%\\n\", 100 * (1 - 0.99 ^ 20)}"'
rm -f /var/tmp/lx05_lat
echo
echo '=== Problem E: triage60.sh on a quiet machine and on three faults'
sleep 15; echo '--- quiet'; bash $D/triage60.sh
echo '--- CPU fault'
for i in 1 2 3 4; do bash -c 'while :; do :; done' & done
bash $D/triage60.sh
pkill -f 'while :; do :; done'; wait 2>/dev/null; sleep 15
echo '--- disk fault'
fio --name=w --filename=/var/tmp/lx05_e --size=512m --rw=randwrite --bs=4k --iodepth=32 --ioengine=libaio --direct=1 --runtime=8 --time_based --output=/dev/null >/dev/null 2>&1 &
sleep 1; bash $D/triage60.sh; wait; rm -f /var/tmp/lx05_e; sleep 15
echo '--- network fault'
python3 - <<'PY' &
import socket, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 19097)); s.listen(2); time.sleep(7)
PY
sleep 0.5
python3 - <<'PY' &
import socket, time
cs = []
for i in range(30):
    c = socket.socket(); c.setblocking(False)
    try: c.connect(("127.0.0.1", 19097))
    except BlockingIOError: pass
    cs.append(c)
time.sleep(6)
PY
sleep 0.5; bash $D/triage60.sh; wait
```

The script of Problem E:

```bash
#!/usr/bin/env bash
# triage60.sh: sample for 4 seconds and name the saturated resource, one line per resource
export LC_ALL=C S_COLORS=never
ncpu=$(nproc)
nstat -n >/dev/null 2>&1
vm=$(vmstat 1 4 | tail -3)
io=$(iostat -x 1 3 | awk '/^Device/ {n++} n==3 && /^(vd|sd|nvme)/ {if ($(NF-1) > m) m = $(NF-1)} END {print m + 0}')
psi() { awk -v k="$2" '$1 == "some" {sub("avg10=", "", $2); print $2}' /proc/pressure/$1; }
r=$(echo "$vm" | awk '{s += $1} END {print s / NR}')
sw=$(echo "$vm" | awk '{s += $7 + $8} END {print s}')
lo=$(nstat -z TcpExtListenOverflows | awk '/ListenOverflows/ {print $2}')
flag() { awk -v v="$1" -v t="$2" 'BEGIN {print (v + 0 > t + 0) ? "SATURATED" : "ok"}'; }
printf 'cpu     run queue %.1f on %d CPUs, PSI some avg10 %s   -> %s\n' "$r" "$ncpu" "$(psi cpu)" "$(flag "$r" "$ncpu")"
printf 'memory  swap-in/out pages %s, PSI some avg10 %s   -> %s\n' "$sw" "$(psi memory)" "$(flag "$(psi memory)" 5)"
printf 'disk    deepest device queue %s, PSI some avg10 %s   -> %s\n' "$io" "$(psi io)" "$(flag "$io" 8)"
printf 'network listen-queue overflows %s   -> %s\n' "${lo:-0}" "$(flag "${lo:-0}" 0)"
```

```text
=== Problem A: Little's law on a real disk (queue depth 16, 6 s)
w/s=134986.00 w_await=0.05 ms aqu-sz=6.80
w/s x w_await / 1000 = 6.7

=== Problem B: a load average far below the run queue while the CPUs are saturated
$ uptime | sed "s/.*load/load/"
load average: 0.35, 0.43, 0.31
$ awk "\$1 == \"some\" {print \"cpu PSI\", \$2}" /proc/pressure/cpu
cpu PSI avg10=31.14
$ vmstat 1 2 | tail -1 | awk "{print \"run queue r =\", \$1}"
run queue r = 4

=== Problem C: find the overloaded listener
$ ss -ltn | awk "NR > 1 && \$2 > \$3 {print \"listener over its backlog:\", \$4, \"Recv-Q\", \$2, \"Send-Q\", \$3; f = 1} END {exit !f}"; echo "exit status: $?"
listener over its backlog: 127.0.0.1:19098 Recv-Q 3 Send-Q 2
exit status: 0

=== Problem D: percentiles, and how many page views see the tail
$ sort -n /var/tmp/lx05_lat | awk "{a[NR] = \$1; s += \$1} END {printf \"mean=%.1f p50=%d p99=%d p99.9=%d max=%d\\n\", s / NR, a[int(NR * 0.50)], a[int(NR * 0.99)], a[int(NR * 0.999)], a[NR]}"
mean=19.9 p50=10 p99=10 p99.9=1000 max=1000
$ awk "BEGIN {printf \"a page of 20 calls hits at least one slow call: %.1f%%\\n\", 100 * (1 - 0.99 ^ 20)}"
a page of 20 calls hits at least one slow call: 18.2%

=== Problem E: triage60.sh on a quiet machine and on three faults
--- quiet
cpu     run queue 0.0 on 2 CPUs, PSI some avg10 4.13   -> ok
memory  swap-in/out pages 0, PSI some avg10 0.00   -> ok
disk    deepest device queue 0, PSI some avg10 0.09   -> ok
network listen-queue overflows 0   -> ok
--- CPU fault
cpu     run queue 4.0 on 2 CPUs, PSI some avg10 33.31   -> SATURATED
memory  swap-in/out pages 0, PSI some avg10 0.00   -> ok
disk    deepest device queue 0, PSI some avg10 0.06   -> ok
network listen-queue overflows 0   -> ok
--- disk fault
cpu     run queue 2.0 on 2 CPUs, PSI some avg10 6.52   -> ok
memory  swap-in/out pages 0, PSI some avg10 0.00   -> ok
disk    deepest device queue 18.07, PSI some avg10 0.00   -> SATURATED
network listen-queue overflows 0   -> ok
--- network fault
cpu     run queue 0.0 on 2 CPUs, PSI some avg10 0.67   -> ok
memory  swap-in/out pages 0, PSI some avg10 0.00   -> ok
disk    deepest device queue 0, PSI some avg10 0.41   -> ok
network listen-queue overflows 135   -> SATURATED
```

**Problem A.** At depth 16 the disk did **134,986 writes per second** with `w_await` **0.05 ms** and `aqu-sz` **6.80**; the product `w/s × w_await / 1000` is **6.7**. The two agree to 2 percent, which is Little's law with the rounding of `w_await` to two decimals: the average number of requests in the system equals the rate times the time each spends there. It is the reason you can read saturation from either `aqu-sz` or `await`: given any two of the three you can compute the third.

**Problem B.** Five seconds into the fault the **load average was 0.35**, the **CPU pressure `some avg10` was 31.14** and the **run queue `r` was 4** on 2 CPUs. The run queue describes **now** (four tasks want two CPUs: saturated), the pressure says that **about 31 percent of the last 10 s** had a waiting task, and the load average says almost nothing yet because it is damped (section 4.1; in another run the same moment read 1.21 and in the first run 1.18). Look at `r` and PSI in the first minute, at the load average when the fault has lasted several.

**Problem C.** `ss -ltn | awk 'NR > 1 && $2 > $3 {...; f = 1} END {exit !f}'` printed `listener over its backlog: 127.0.0.1:19098 Recv-Q 3 Send-Q 2` and exited 0. For a **listening** socket `Recv-Q` is the number of connections waiting to be accepted and `Send-Q` the backlog; the test is the comparison of the two, not their values. The pipeline's exit status is that of `awk`, which is what makes it usable in a monitoring check. To see whether it **has been** overflowing and not only is, `nstat -z TcpExtListenOverflows` gives the count since its previous call (`nstat -a` would give the total since boot).

**Problem D.** The set has `mean=19.9 p50=10 p99=10 p99.9=1000 max=1000` (ms): the mean is **twice** what 99 percent of requests experienced, and the median and p99 do not show the problem at all; only the p99.9 does. A page of 20 calls meets at least one slow call with probability `1 - 0.99^20`, **18.2 percent**, so about one page in five shows the 1,000 ms event even though only 1 call in 100 is slow.

**Problem E.** `triage60.sh` (above) takes `vmstat 1 4` (run queue `r` averaged over the last three rows, and `si` plus `so`), `iostat -x 1 3` (the largest `aqu-sz` of any block device in the last report), the PSI `some avg10` of each resource and the **change** of `TcpExtListenOverflows` since the start of the run. Its rules: CPU is saturated when the average run queue exceeds the CPU count; memory when memory PSI `some avg10` exceeds 5; disk when a device's `aqu-sz` exceeds 8; network when listen overflows rose. The results:

- **quiet**: all four `ok` (CPU PSI 4.13 is the decaying tail of the previous test);
- **CPU fault** (four loops): run queue **4.0** on 2 CPUs, PSI 33.31: **CPU SATURATED**, the others `ok`;
- **disk fault** (`fio` at depth 32): deepest device queue **18.07**: **disk SATURATED**, CPU `ok` with a run queue of 2.0 (`fio` itself, not above the CPU count), and **disk PSI of 0.00**, which is why the rule uses `aqu-sz` and not pressure on this fast disk;
- **network fault** (30 clients on a backlog of 2): **135 overflows**: **network SATURATED**, the others `ok`.

**Limits of the script, stated honestly.** The thresholds (run queue above the CPU count, `aqu-sz` above 8, memory PSI above 5) are my choices for this VM, not universal values; the **memory rule was not exercised** by a fault in this run (the 64 MB cgroup test of section 4.4 produced a PSI `avg10` of only 0.45 because it lasted 5 s, so it would not have crossed the threshold of 5); and the script samples for 4 s, so it misses a stall shorter than that, which is the `sar` lesson of section 8. A real version would also read errors first (`dmesg`, interface error counters) and per-process attribution (`pidstat`), which is what the human does next.

---

## Cheat sheet

```text
Method          USE per resource: Utilisation, Saturation, Errors (errors first, saturation hurts);  RED per service: Rate, Errors, Duration;  name the resource, then the cause
The ten         uptime | dmesg -T | tail | vmstat 1 | mpstat -P ALL 1 | pidstat 1 | iostat -xz 1 | free -m | sar -n DEV 1 | sar -n TCP,ETCP 1 | top
First lines     vmstat and iostat first report = average since boot;  nstat prints the delta since its last call (-a = since boot)
CPU             saturated: vmstat r > CPUs, pidstat %wait high, PSI cpu some;  one CPU at 100 in mpstat = single thread;  st = the host steals;  /proc/PID/schedstat field 2 = run-queue wait (ns)
Load average    damped (5 s sample, 1/5/15 min), counts R + D;  lags a fault (0.35 to 1.21 five seconds in, in two runs);  a 4 s saturation did not move it
Memory          read available, not free;  saturation: si/so, sar -B scans, PSI memory, OOM in dmesg, cgroup failcnt;  no swap means 0 si/so proves nothing
Disk            iostat -xz: aqu-sz and await are saturation;  %util = time with a request outstanding (86% at 0.08 ms await);  IOPS = depth / latency;  knee: depth 64 to 512 = +51% IOPS, 5.3x latency
Network         sar -n DEV (rate, %ifutil), sar -n TCP,ETCP (retrans), ss -ltn (Recv-Q > Send-Q on a listener = accept queue full), nstat ListenOverflows, ss -ti
PSI             /proc/pressure/{cpu,memory,io}: some / full, avg10 avg60 avg300 (%), total (us);  trigger: write "some 150000 1000000", poll();  avg10 decays slowly
Queueing        M/M/1: mean = service / (1 - utilisation): 2x at 50%, 10x at 90%, 20x at 95%;  p99 about 4.6x the mean;  plan for 60 to 70%
Latency         report p50 p99 p99.9 max, never the mean;  fan-out: 1 - 0.99^20 = 18.2%;  open-loop clients measure from the intended send time (closed-loop p99 1.2 ms vs open-loop 1,920 ms)
Benchmarks      state the cache (write 3.0 GB/s vs 1.3 with fsync);  use the result (-O2 removed the loop: 0.0000 s);  repeat (12 runs, CV 3%);  watch vmstat/iostat/mpstat while it runs
sar             sar -o FILE -u -q -r -n DEV 1 N  records;  sar -f FILE -u -s HH:MM:SS -e HH:MM:SS  queries;  -u -q -r -B -W -d -n DEV,EDEV,TCP,ETCP;  interval decides what you can see
Decision tree   errors > CPU (r, mpstat, PSI) > memory (available, si/so, PSI) > disk (aqu-sz, await) > network (retrans, listen queue) > nothing saturated: locks, pools, downstream (RED, strace -T)
```

---

## Sources

Fetched in this session (what each confirmed):

- [Linux USE method checklist](https://www.brendangregg.com/USEmethod/use-linux.html) (Brendan Gregg): the utilisation, saturation and error metrics and tools for CPU, memory capacity, storage device I/O, network interfaces and storage capacity (for capacity, no saturation metric is defined, a full disk returns `ENOSPC`); the specific tools named per cell are those in section 2.
- [Performance checklists for SREs (SREcon 2016)](https://www.brendangregg.com/blog/2016-05-04/srecon2016-perf-checklists-for-sres.html): the ten commands in order (`uptime`, `dmesg -T | tail`, `vmstat 1`, `mpstat -P ALL 1`, `pidstat 1`, `iostat -xz 1`, `free -m`, `sar -n DEV 1`, `sar -n TCP,ETCP 1`, `top`) and what each is used for. **It does not describe the USE method, workload characterisation or drill-down**; those definitions in section 1 are from memory.
- [Pressure stall information](https://docs.kernel.org/accounting/psi.html) (kernel documentation): `some` and `full` lines with `avg10`, `avg60`, `avg300` as percentages and `total` in microseconds; `some` is at least one task stalled, `full` is all non-idle tasks stalled; the CPU `full` line is reported as zero at system level since 5.13; triggers are set by writing `<some|full> <stall us> <window us>` to the opened file and waiting with `poll`, `select` or `epoll`.
- [Active benchmarking](https://www.brendangregg.com/activebenchmarking.html) (Brendan Gregg): run in steady state while analysing with other tools; passive benchmarking failures; the list of ways a benchmark measures something else.
- [On coordinated omission](https://www.scylladb.com/2021/04/22/on-coordinated-omission/) (ScyllaDB): the definition, closed-model generators as the cause, measuring from the intended start time, HdrHistogram's expected-interval correction, and the YCSB example (uncorrected p99 about 249 µs against about 665 ms corrected).
- [The Tail at Scale](https://research.google/pubs/the-tail-at-scale/) (Dean and Barroso): **the abstract only** was read: temporary high-latency episodes that are minor in moderate systems dominate performance at scale, and large services should be tail-tolerant. The fan-out arithmetic in section 6.2 is mine.

Run in this session: procps-ng 4.0.4 (`vmstat`, `top`), sysstat 12.6.1 (`mpstat`, `pidstat`, `iostat`, `sar`), iproute2 (`ss`, `nstat`, `tc`), fio 3.36, stress-ng 0.17 (installed, not used), gcc 13, coreutils `dd`, Python 3 and bash, on Linux 6.18.44 (2 vCPUs, about 8 GiB) as root, cgroup v1. Not available, so not used: `perf`, `bpftrace`, `ftrace` (no tracefs), `tcpdump`, and `tc netem` (the qdisc is not built in).

From memory, not fetched or not tested in this session: the RED method (Tom Wilkie), the names of Gregg's anti-methods (street-light, random change, blame someone else), workload characterisation and drill-down, errors-first ordering, the `iostat` manual's caution about `%util` for parallel devices, the one-more-than-backlog accept queue detail and the SYN retransmission behaviour of a client facing a full queue, `ss -ti`, `sar -n EDEV`, `perf sched latency` and `runqlat`, `systemd-oomd` and `lmkd` using PSI triggers, per-cgroup `*.pressure` files on cgroup v2, the `/var/log/sa` or `/var/log/sysstat` collection by a timer or `cron`, HdrHistogram, the expectation that cold reads on real hardware are far slower than warm ones, that the knee of a rotating disk comes at a depth of a handful, and the `-S` option of `sar`.

Cross-references: [LX 01](01_shell_and_scripting.md), [LX 03](03_processes_signals_and_proc.md) (load average, states, PSI, `/proc`, sockets and files), [LX 04](04_boot_systemd_and_logs.md) (cgroup limits, `dmesg`, the journal), [OS 05](../05_operating_systems/05_process_scheduling.md) (run queues, scheduling), [CO 13](../04_computer_organization/13_cache_basics.md) if you want caches and memory latency under the numbers, the [debugging track](../16_debugging/00_plan.md); next in this track: [LX 06](00_plan.md) (memory, swap and the OOM killer), [LX 08](00_plan.md) (the networking toolbox) and [LX 10](00_plan.md) (`perf` and flame graphs), [LX 11](00_plan.md) (`ftrace`, eBPF, `bpftrace`) and [LX 13](00_plan.md) (tuning for latency and throughput).
