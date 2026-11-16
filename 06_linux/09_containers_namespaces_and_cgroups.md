# LX 09: Containers from first principles

*Part of the [Linux plan](00_plan.md) · Time: about 6 hours · You need: [LX 03](03_processes_signals_and_proc.md) (`/proc`, signals, process trees), [LX 06](06_memory_swap_and_the_oom_killer.md) (memory accounting and the OOM killer), [LX 07](07_files_permissions_and_storage.md) (mounts, permissions) and [LX 08](08_linux_networking_toolbox.md) (network namespaces and `veth`). This tutorial takes a container apart: there is no such object in the kernel, only a process with a set of namespaces, a cgroup, a different root file system, a reduced set of privileges and a system call filter. Every piece is built by hand here, then compared with what `docker` and `runc` do.*

"A container is a lightweight VM" is the answer that fails the interview. The answer that passes is a list: which **namespaces** hide what, which **cgroup** files limit what, how the **root file system** is swapped and layered, and which **capabilities, seccomp filter and user namespace** decide what the process is still allowed to do. This tutorial measures each item (sections 2 to 7), assembles a working container from shell commands (section 5), reads what Docker and `runc` set up for the same thing (section 8), and ends with how Kubernetes requests and limits become cgroup files (section 9), containers against virtual machines (section 10), costs, mistakes, questions and five problems.

**Seven honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs) as **root**, with `runc` 1.5.2, Docker 29.9 (`dockerd` and `containerd`), util-linux `unshare`, `nsenter` and `lsns`, `setpriv`, `capsh` and a static `busybox` (installed with `apt` where missing). The VM is itself a sandbox: its shell has no seccomp filter and no `no_new_privs`, and it lacks `CAP_SYS_RESOURCE`.
2. **This kernel runs cgroup v1 (hybrid)**: every controller is bound to a v1 tree and a mounted v2 tree shows **only the `hugetlb` controller**. So all cgroup measurements use the **v1 files** (`memory.limit_in_bytes`, `cpu.cfs_quota_us`, `pids.max`). The **v2 interface** (`memory.max`, `cpu.max`, `io.max`, the no internal process rule, delegation, `memory.events`) is described **from memory** and marked as such.
3. **There is no Kubernetes, no `systemd-run`/`systemd` as init, no `podman` or `crun`, no rootless runtime and no Internet or registry.** The image used by Docker is a busybox tree loaded with `docker import`. How the kubelet turns requests and limits into cgroup settings is **from memory** (section 9), confirmed only for the cgroup files that `docker run --memory --cpus --pids-limit` writes.
4. The private `dockerd` ran with **`--iptables=false --bridge=none`**, so **published ports (`-p`) and the `docker0` bridge were not measured**; container networking is shown with a hand made `veth` pair (the mechanism that Docker automates) and the port mapping is explained from LX 08.
5. The root file system used for the hand made container is a **loop-free directory on the VM's ext4 disk**; **overlayfs** (kernel `overlay`) is real, but the image has **one layer** and the multi layer cases (whiteouts, copy up of big files, `redirect_dir`) are **from memory**.
6. **Single runs.** CPU throttling counters (`nr_periods`, `nr_throttled`) differ by one or two between runs, and the `uptime` shown with the time namespace changes every run, so the text quotes ranges or the shape, not a decimal.
7. Several demos need a **root** that a real laptop or a CI job may not give you. The user namespace demos (section 2.7) are run as **uid 1000** and need no privilege at all, which is the point of that feature.

By the end you can:

- say what each of the **eight namespaces** hides and run it by hand (measured: a new UTS namespace let the container say `ctr-1` while the host stayed `vm`; the first process in a new PID namespace was **PID 1** and killing it killed its children; a mount made in a new mount namespace was invisible outside; a new network namespace held **only a `lo` that is down**; the System V segment count was 1 outside and 0 inside; a **time namespace** moved `uptime` by 86,400 s; a **cgroup namespace** showed `4:memory:/` instead of the full path);
- explain a **user namespace** in one sentence and show it (measured: as uid 1000, `unshare --user --map-root-user` gave **uid 0 inside**, the map `0 1000 1`, **every capability** (`000001ffffffffff`) and a file touched inside **owned by 0 inside and 1000 outside**; without a map the uid is **65534**; inside it `tmpfs` and bind mounts were **allowed** and mounting a real disk was **denied**);
- **enter** a running container's namespaces with `nsenter` and list them with `lsns -p PID`;
- set **cgroup limits** and read the counters (measured: a 20 ms per 100 ms CPU quota cut a 3 s spin from **2.97 s to about 0.6 s of CPU** with **30 to 31 throttled periods**; a `pids.max` of 8 let a process start **7** children and refused **23**; a 40 MiB memory limit **killed** a hog with **exit status 137** and `oom_kill 1`; a cpuset limited a process to **CPU 0 of 0-1**; the freezer stopped a group without a signal);
- state the **cgroup v2** differences from memory: one tree, `cpu.max`, `memory.max`, `memory.high`, `io.max`, `pids.max`, the **no internal process** rule, **delegation**, `memory.events` and PSI;
- explain why **`chroot` is not a container** (measured: a root process escaped a chroot with a 10 line C program) and how **`pivot_root`** replaces the root of a whole mount namespace (measured: after `pivot_root` the old root was gone, PID 1 was the container's shell and the escape found nothing);
- build a **container by hand** from an overlay root, cgroups, a `veth` pair and `unshare` (measured: hostname `container-1`, PID 1, ping to the host **2 of 2**, writes landed in the **upper** directory while the **lower** image stayed `image 1.0`, a 100 MiB allocation against a 48 MiB limit ended `Killed` with **status 137**, and 30 forks against `pids.max` 16 got **11**);
- read what a **container is allowed to do** (measured: root holds capability mask `000001fffeffffff` (40 capabilities), a Docker container `00000000a80425fb` (**14**), `--cap-drop ALL` **0**, `--privileged` back to `000001fffeffffff`; `no_new_privs` turned a setuid program's `euid=0` into `euid=1001`; Docker's default **seccomp filter** showed as `Seccomp: 2` against `Seccomp: 0` for `seccomp=unconfined`, and a custom profile made `mkdir` fail with `Operation not permitted` while `touch` worked);
- read the **runtime view** (measured: `docker run --memory 64m --cpus 0.5 --pids-limit 32` produced `memory.limit_in_bytes` 67108864, `cpu.cfs_quota_us` 50000 on a period of 100000, `pids.max` 32, and the container process had **its own** `mnt`, `net`, `pid`, `ipc` and `uts` namespaces but the **host's `user` and `cgroup`** ones; a 32 MiB limit gave `OOMKilled=true`, `ExitCode=137`; the container's `uname -r` equalled the host's);
- translate **Kubernetes requests and limits** into cgroup values and predict **throttling** and **`OOMKilled`** (from memory, with the CPU and memory mechanics measured above).

**Where the numbers come from.** Six experiment scripts (`x1` to `x6`) and a problem-answer script (`ans_prob`) were run in this session on the VM described above. Transcripts print `$ command` before each command's output. In transcripts `$C` is `/sys/fs/cgroup`, `$R` and `$B` are scratch directories under `/var/tmp`, `$U` is `setpriv --reuid=1000 --regid=1000 --clear-groups` (run as uid 1000) and `as 1001 CMD` runs `CMD` as uid 1001. Numbers not from these runs are marked "from memory" or come from a source at the end.

**Data and timing.** No data sets. The root file system is a busybox tree of about 2 MB, each experiment runs in a few seconds, and every scratch directory, mount, namespace, `veth` pair and cgroup is removed by a `cleanup` function in the script. All timings are single runs.

---

## 1. What a container is

```text
 a container = ONE ordinary process tree on the host kernel, plus:

   namespaces  : what it can SEE      (mnt pid net uts ipc user cgroup time)
   cgroups     : what it can USE      (cpu memory io pids cpuset)
   root fs     : what files it has    (pivot_root into an overlayfs mount)
   privileges  : what it may DO       (capabilities, no_new_privs, seccomp, user ns, LSM)
   plumbing    : how it talks         (veth to a bridge, NAT and port mapping)
```

| Piece | Kernel feature | Tool that sets it | Image or runtime? |
| --- | --- | --- | --- |
| isolated views | namespaces: `clone`, `unshare`, `setns` | `unshare`, `nsenter`, `runc` | runtime |
| resource limits | cgroups | `systemd-run`, `docker --memory`, kubelet | runtime |
| files | `pivot_root`, overlayfs | image layers + `runc` | image + runtime |
| privilege | capabilities, seccomp, `no_new_privs`, user ns, AppArmor or SELinux | `runc` from a spec | runtime |
| network | netns, `veth`, bridge, NAT | CNI plugin or Docker | runtime |

Two consequences to remember. First, **the host kernel is shared**: a container runs the same kernel as the host (measured in section 8: `uname -r` inside and outside gave the same release, `6.18.44-fc-v470`), so a kernel bug or a missing filter is a host problem, and a container cannot run a different kernel version or load its own driver. Second, **a container has no PID of its own on the host**: it is a process, so `ps`, `strace`, `perf` and `kill` on the host see it (section 8 shows the host PID).

The word "container" in the kernel's own documents does not appear; the source of truth for each part is `namespaces(7)`, `cgroups(7)`, `pivot_root(2)`, `capabilities(7)` and `seccomp(2)`. The **OCI** (Open Container Initiative) specifications standardise two things: the **image** format (layers and a configuration) and the **runtime spec** (`config.json`) that a low level runtime such as `runc` executes. Section 8 covers them.


---

## 2. Namespaces: what the process can see

A **namespace** wraps one global resource so that a group of processes sees its own copy. Every process belongs to **one namespace of each type**, and the identity of a namespace is the **inode number** in `/proc/PID/ns/`. Two processes with the same inode share that namespace.

| Namespace | Isolates | Flag | Created by |
| --- | --- | --- | --- |
| mount (`mnt`) | the mount table | `CLONE_NEWNS` | `unshare --mount` |
| UTS | hostname, domain name | `CLONE_NEWUTS` | `unshare --uts` |
| IPC | System V IPC, POSIX message queues | `CLONE_NEWIPC` | `unshare --ipc` |
| PID | process IDs | `CLONE_NEWPID` | `unshare --pid --fork` |
| network (`net`) | interfaces, routes, firewall, sockets | `CLONE_NEWNET` | `unshare --net`, `ip netns` |
| user | uids, gids, capabilities | `CLONE_NEWUSER` | `unshare --user` |
| cgroup | the view of the cgroup tree | `CLONE_NEWCGROUP` | `unshare --cgroup` |
| time | `CLOCK_MONOTONIC` and `CLOCK_BOOTTIME` offsets | `CLONE_NEWTIME` | `unshare --time` |

Three system calls do all of it. **`clone(flags)`** (or `clone3`) creates a child directly inside new namespaces. **`unshare(flags)`** moves the *calling* process into new ones. **`setns(fd, type)`** joins an existing namespace through a file descriptor opened on `/proc/PID/ns/TYPE`. The tools `unshare(1)` and `nsenter(1)` are thin wrappers over `unshare` and `setns`; `runc` uses `clone` flags from the spec.

```text
--- 1. every process is in one namespace of each kind; the inode number is the identity
$ ls -l /proc/self/ns | tail -n +2 | awk "{print \$9, \$10, \$11}"
cgroup -> cgroup:[4026531835]
ipc -> ipc:[4026531839]
mnt -> mnt:[4026531832]
net -> net:[4026531833]
pid -> pid:[4026531836]
pid_for_children -> pid:[4026531836]
time -> time:[4026531834]
time_for_children -> time:[4026531834]
user -> user:[4026531837]
uts -> uts:[4026531838]
$ readlink /proc/self/ns/uts; unshare --uts readlink /proc/self/ns/uts
uts:[4026531838]
uts:[4026532264]
```

The host shell has one namespace of every kind; `unshare --uts` made a new UTS namespace (a new inode). Sections 2.1 to 2.6 try each kind.

### 2.1 UTS, mount, IPC: simple private copies

```text
--- 2. UTS: a private hostname
$ hostname; unshare --uts sh -c "hostname ctr-1; hostname"; hostname
vm
ctr-1
vm
```

```text
--- 4. mount: mounts made inside are invisible outside
$ unshare --mount sh -c "mount -t tmpfs tmpfs /mnt && echo secret > /mnt/f && ls /mnt && findmnt -n -o FSTYPE /mnt"; test -e /mnt/f || echo "outside: /mnt/f does not exist"
f
tmpfs
outside: /mnt/f does not exist
```

```text
--- 6. IPC: private System V objects
$ ipcs -m | grep -c 0x; unshare --ipc sh -c "ipcs -m | grep -c 0x; true"
1
0
```

A `hostname` change in a new UTS namespace never reaches the host. A tmpfs mounted in a new **mount namespace** was visible inside (`f`, `tmpfs`) and absent outside. Mount propagation matters here: a new mount namespace starts as a **copy** of the parent's mount table, and whether later mounts propagate between them depends on the propagation mode (`shared`, `private`, `slave`) of each mount. `unshare` makes the new tree `private` by default, which is why the tmpfs did not leak. A container runtime does the same and then makes the root `rslave` or `private` explicitly so a mount on the host does not appear inside, and a mount inside does not appear outside. The IPC demo counted `ipcs -m` segments: 1 on the host, 0 in the new namespace.

### 2.2 PID: the first process is PID 1

```text
--- 3. PID: the first process in a new PID namespace is PID 1 (and ps needs a private /proc)
$ unshare --pid --fork --mount-proc sh -c "echo my pid: \$\$; ps -e -o pid,comm | head -4"
my pid: 1
    PID COMMAND
      1 sh
      2 ps
      3 head
$ unshare --pid --fork --mount-proc sh -c "sleep 30 & sleep 0.2; kill -9 \$\$" 2>&1 | head -1; sleep 0.3; pgrep -c "sleep 30" || echo "children died with their PID 1"
0
children died with their PID 1
```

A new PID namespace needs `--fork`: the process that calls `unshare(CLONE_NEWPID)` stays in the old namespace, and only its **children** enter the new one (that is why the kernel exposes `pid_for_children` in `/proc/self/ns`). The first child is **PID 1**. `ps` read the *old* `/proc` until `--mount-proc` gave it a private one, which shows that `/proc` is a file system mounted by someone, not the live truth about the caller's namespace.

PID 1 has two special duties that cause real production bugs:

1. **Signals.** The kernel does not deliver a signal to PID 1 unless the process installed a handler for it, so `kill -TERM 1` (which `docker stop` sends) is ignored by a shell or a program that never installed one, and Docker falls back to `SIGKILL` after its timeout (10 seconds by default, from memory). This is why containers run `tini` or `docker run --init`, or an entrypoint that handles `SIGTERM`.
2. **Reaping.** An orphan inside the namespace is re-parented to PID 1, and if PID 1 never calls `wait`, **zombies pile up**.

When PID 1 exits the kernel kills every other process in the namespace with `SIGKILL`. The transcript shows it: the shell was killed with `kill -9`, and a `sleep 30` it had started was gone (`children died with their PID 1`). A process in a PID namespace has **one PID per namespace level**: PID 1 inside, a different PID on the host (section 8 prints the host PID of a container).

### 2.3 Network: an empty stack

```text
--- 5. network: an empty stack with only a down loopback
$ unshare --net ip -br addr
lo               DOWN
$ unshare --net sh -c "ip link set lo up; ip -br addr; ip route | wc -l"
lo               UNKNOWN        127.0.0.1/8
0
```

A new network namespace has **no interfaces except a `lo` that is down**, no routes and its own firewall, sockets and `sysctl net.*` values (measured in LX 08). It is connected to the world by moving one end of a `veth` pair into it (section 6).

### 2.4 User namespace: root that is not root

```text
--- 7. user: uid 0 inside is an ordinary uid outside (run here as uid 1000)
$ $U unshare --user --map-root-user sh -c "id -u; cat /proc/self/uid_map; grep -E \"^CapEff\" /proc/self/status"
0
         0       1000          1
CapEff: 000001ffffffffff
$ $U unshare --user sh -c "id -u; wc -c < /proc/self/uid_map"
65534
0
$ $U unshare --user --map-root-user sh -c "touch /tmp/lx09_owned; ls -ln /tmp/lx09_owned | cut -d\" \" -f3; chown 5 /tmp/lx09_owned"
0
chown: changing ownership of '/tmp/lx09_owned': Invalid argument
$ ls -ln /tmp/lx09_owned | cut -d" " -f3; rm -f /tmp/lx09_owned
1000
$ $U unshare --user --map-root-user --mount sh -c "mount -t tmpfs tmpfs /mnt >/dev/null 2>&1 && echo tmpfs: allowed || echo tmpfs: denied; mount --bind /etc /var/tmp >/dev/null 2>&1 && echo bind mount: allowed || echo bind mount: denied; mount -t ext4 /dev/vda /var/tmp >/dev/null 2>&1 && echo ext4 device: allowed || echo ext4 device: denied"
tmpfs: allowed
bind mount: allowed
ext4 device: denied
```

Run as uid 1000, with no capabilities, `unshare --user --map-root-user` created a user namespace in which the process is **uid 0** and holds **every capability** (`CapEff 000001ffffffffff`). The mapping file says why it is safe: `0 1000 1` means "uid 0 inside is uid 1000 outside, for 1 uid". A file created inside is owned by **0 inside and 1000 outside**, and `chown 5` failed with `Invalid argument` because **uid 5 has no mapping**: an id without a mapping cannot be written. Without any map the process is the overflow uid **65534** (`nobody`) and `uid_map` is empty.

The capabilities are **relative to that user namespace**: they apply only to resources that namespace owns. That is why inside a user namespace plus a mount namespace the process could mount a `tmpfs` and bind mounts (it owns the mount namespace) but could not mount the real disk (the device belongs to the initial user namespace). This is the mechanism of **rootless containers**: an unprivileged user maps their own uid to 0 inside, builds the other namespaces under that, and a break out lands as an ordinary user. A real rootless runtime maps a **range** of uids, for example `0 -> 1000` and `1..65535 -> 100000..165534`, using the setuid helpers `newuidmap` and `newgidmap` and the ranges in `/etc/subuid` and `/etc/subgid` (from memory; only the single uid map was run here).

### 2.5 Time and cgroup namespaces

```text
--- 8. time: a private boot clock offset
$ cut -d" " -f1 /proc/uptime; unshare --time --boottime=86400 sh -c "cut -d\" \" -f1 /proc/uptime"
89.28
86489.28
```

```text
--- 9. cgroup: the process sees its own cgroup as the root
$ cat /proc/self/cgroup | grep ":memory:"; unshare --cgroup cat /proc/self/cgroup | grep ":memory:"
4:memory:/process_api/01a1e61a-9d46-7737-a9dc-047c5c707dcb/claude-code-bash
4:memory:/
```

The time namespace shifted the boot clock by 86,400 s (`/proc/uptime` 89 s outside and 86,489 s inside, one day more). It exists for **container checkpoint and restore**, so a restored process sees a monotonic clock that did not jump. It does **not** virtualise the wall clock (`CLOCK_REALTIME`). The cgroup namespace makes `/proc/self/cgroup` show `/` instead of the full path from the host root, which hides the host's layout and lets a container see "its own" cgroup as the root.

### 2.6 `setns` in practice: `nsenter` and `lsns`

```text
--- 10. setns: nsenter joins the namespaces of a running process, lsns lists them
$ nsenter -t $P -u hostname
inner
$ nsenter -t $P -n ip -br link | cut -c1-40
lo               DOWN           00:00:00
$ lsns -p $P -o TYPE,NPROCS,PID,COMMAND | cut -c1-70
TYPE   NPROCS   PID COMMAND
time       75     2 kthreadd
cgroup     75     2 kthreadd
pid        73     2 kthreadd
user       75     2 kthreadd
ipc        75     2 kthreadd
mnt         3   850 unshare --pid --fork --mount-proc --uts --net sh -
uts         3   850 unshare --pid --fork --mount-proc --uts --net sh -
net         3   850 unshare --pid --fork --mount-proc --uts --net sh -
```

`nsenter -t PID -u -n ...` joined the UTS and network namespaces of a running process and ran one command there: the hostname was `inner` and only `lo` existed. This is how you debug a container that has no shell or tools: run the **host's** `tcpdump`, `ss` or `strace` inside the container's network or PID namespace. `lsns -p PID` lists the namespaces of one process with the number of processes in each (the three `mnt`, `uts` and `net` namespaces held 3 processes). `ip netns exec` and `docker exec` are the same operation with different front ends.

### 2.7 What namespaces do not do

A namespace changes **what a name refers to**, not what the process may do or how much it may use. Many resources are **not namespaced**: the kernel itself (modules, `sysctl kernel.*` outside `net` and IPC), the system clock, `/dev` devices (unless the file system or a device cgroup hides them), `dmesg`, kernel keyrings (partly), and the time `CLOCK_REALTIME`. Limits come from cgroups (next section), privilege from capabilities and seccomp (section 7).


---

## 3. Cgroups: what the process can use

A **control group** is a node in a tree of process groups to which controllers apply limits and accounting. A process is in exactly one cgroup per hierarchy, children inherit the cgroup of their parent, and `/proc/PID/cgroup` shows where a process is. The controllers that matter: **`cpu`** (share and quota), **`memory`**, **`io`** (`blkio` in v1), **`pids`**, **`cpuset`** (which CPUs and memory nodes), plus `freezer` (v1) and `devices`.

### 3.1 v1 and v2 on this kernel

```text
--- 1. the hierarchies: v1 has one tree per controller; the unified v2 tree has no controllers left
$ ls $C | tr "\n" " "; echo
blkio cpu cpu,cpuacct cpuacct cpuset devices freezer memory pids systemd unified
$ grep -E "^(cpu|memory|pids|cpuset|freezer)" /proc/cgroups | cut -f1,2,4 | tr "\t" " "
cpuset 3 1
cpu 1 1
cpuacct 2 1
memory 4 1
freezer 6 1
pids 8 1
$ cat /tmp/lx09_v2/cgroup.controllers; ls /tmp/lx09_v2 | grep -c cgroup
hugetlb
8
```

This VM is **v1 (hybrid)**: one tree per controller (`blkio cpu cpuacct cpuset devices freezer memory pids systemd`) and the `unified` v2 directory which can be mounted but exposes only `hugetlb` here. `/proc/cgroups` lists each controller, its hierarchy ID and the number of cgroups. A **v2 (unified)** system has **one tree**, and a controller is turned on for children with `echo +memory > cgroup.subtree_control`. Docker on this VM reported `Cgroup Version 1`, driver `cgroupfs`.

| What | cgroup v1 (measured here) | cgroup v2 (from memory) |
| --- | --- | --- |
| hierarchy | one tree per controller | one unified tree |
| CPU quota | `cpu.cfs_quota_us` and `cpu.cfs_period_us` | `cpu.max` = `QUOTA PERIOD`, e.g. `50000 100000` |
| CPU weight | `cpu.shares` (default 1024) | `cpu.weight` (default 100, range 1 to 10000) |
| memory hard limit | `memory.limit_in_bytes` | `memory.max` |
| memory soft limit | `memory.soft_limit_in_bytes` | `memory.high` (throttles and reclaims) and `memory.low`, `memory.min` (protection) |
| swap | `memory.memsw.limit_in_bytes` (memory plus swap) | `memory.swap.max` |
| OOM events | `memory.oom_control` (`oom_kill N`) | `memory.events` (`oom`, `oom_kill`, `max`, `high`) |
| processes | `pids.max`, `pids.current`, `pids.events` | same |
| block I/O | `blkio.throttle.*`, `blkio.weight` | `io.max`, `io.weight`, `io.stat` |
| CPUs | `cpuset.cpus`, `cpuset.mems` | same |
| freeze | `freezer.state` | `cgroup.freeze` |
| kill the group | write each PID | `cgroup.kill` (kernel 5.14 and later) |
| pressure | not in v1 | `cpu.pressure`, `memory.pressure`, `io.pressure` (PSI) |

Rules of the **v2** design that the interview asks about (from memory): a cgroup that has controllers enabled for its children **cannot also contain processes** (the **no internal process** rule: processes live in the leaves, except the root), a **threaded** mode exists for per thread controllers, and **delegation** hands a subtree to an unprivileged user by changing the owner of the directory and of `cgroup.procs`, `cgroup.subtree_control` and `cgroup.threads`, so a user-level service manager or a rootless runtime can create children without root. `systemd` is the usual owner of the tree: one **slice** per grouping (`system.slice`, `user.slice`), one **scope** or **service** per unit, and `systemd-run --scope -p MemoryMax=200M cmd` or `systemd-run -p CPUQuota=50% cmd` creates a cgroup, sets the limit and starts the command in it (not available here: this VM is not running systemd).

### 3.2 CPU: share and quota

A `cpu` cgroup has two different controls. **Shares** (`cpu.shares`, `cpu.weight`) only matter when the CPU is **contended**: they divide the time between runnable groups in proportion and never limit an otherwise idle machine. A **quota** is a **hard cap**: the group may run for `quota` microseconds in every `period` microseconds (summed over all its CPUs). When it uses the quota up, every task in the group is **throttled** until the next period starts, even if the CPUs are idle.

```text
--- 2. a CPU quota: 20 ms of CPU per 100 ms period is 20 percent of one CPU
$ python3 $D/spin.py 3
wall 3.0 s, cpu 2.97 s
$ cat $C/cpu/lx09/cpu.cfs_period_us $C/cpu/lx09/cpu.cfs_quota_us | tr "\n" " "; echo
100000 20000
$ (echo $BASHPID > $C/cpu/lx09/cgroup.procs; python3 $D/spin.py 3)
wall 3.0 s, cpu 0.61 s
$ grep -E "nr_periods|nr_throttled|throttled_time" $C/cpu/lx09/cpu.stat
nr_periods 32
nr_throttled 31
throttled_time 2454650687
```

`spin.py 3` is a loop that burns CPU for 3 seconds. Unlimited it used **2.97 s of CPU in 3.0 s of wall time**. In a cgroup with a quota of 20,000 µs per period of 100,000 µs (20 percent of one CPU) it used about **0.6 s** (20 percent of 3 s), and `cpu.stat` showed **`nr_periods` 32, `nr_throttled` 31, `throttled_time` about 2.45 s** (30 to 31 throttled periods and 2.4 to 2.5 s in different runs): throttled in almost every period, for a total of nearly the 2.4 s it was kept off the CPU.

This is the mechanism behind a classic latency bug: a multi threaded service with a limit of one CPU runs 8 threads, burns its whole quota in the first 12 ms of each 100 ms period, and is **frozen for the other 88 ms**, so a request that arrives then waits up to 88 ms with the CPUs idle and the load average looking calm. The signals are `nr_throttled` and `throttled_time` rising, request tail latency at multiples of the period, and a low overall CPU use. Fixes: raise the limit, lower the thread count to the quota, or remove the limit and rely on shares (a trade off discussed in section 9). On a v2 system the same counters are in `cpu.stat` as `nr_throttled`, `throttled_usec` (from memory).

### 3.3 Processes: `pids.max`

```text
--- 3. a process limit: fork bomb contained
$ (echo $BASHPID > $C/pids/lx09/cgroup.procs; python3 $D/forks.py 30) 2>&1
started 7, refused 23 (Resource temporarily unavailable)
$ cat $C/pids/lx09/pids.current $C/pids/lx09/pids.max | tr "\n" " "; echo
0 8
$ cat $C/pids/lx09/pids.events 2>/dev/null || echo "(no pids.events in this kernel)"
max 46
```

A `pids.max` of 8 let the test start **7** children and refused **23** with `Resource temporarily unavailable` (`EAGAIN` from `fork`). The group's `pids.events` file counts how often the limit was hit (`max 46`: this is a **cumulative** count of failed forks including retries and is not equal to the 23 refused children). The limit protects the host from a fork bomb: a process in the group cannot make the kernel run out of PIDs for everyone else. Docker sets it with `--pids-limit` and Kubernetes with the kubelet's `podPidsLimit`.

### 3.4 Memory: the cgroup OOM killer

```text
--- 4. a memory limit: allocation beyond it is an OOM kill inside the group only
$ ( (echo $BASHPID > $C/memory/lx09/cgroup.procs; python3 /tmp/lx09_hog.py; echo "exit status $?") 2>/dev/null )
exit status 137
$ cat $C/memory/lx09/memory.failcnt; grep -E "^oom_kill" $C/memory/lx09/memory.oom_control; echo "peak $(( $(cat $C/memory/lx09/memory.max_usage_in_bytes) / 1048576 )) MiB"
0
oom_kill_disable 0
oom_kill 1
peak 40 MiB
```

A hog that allocated and touched memory in a cgroup limited to 40 MiB was killed with **exit status 137** (128 plus signal 9), `oom_kill` went to **1**, and the peak was **40 MiB**. The limit covers **anonymous memory, page cache and kernel memory** charged to the group (LX 06): the page cache of files the group reads counts, but is **reclaimed first**, so a limit with plenty of clean cache does not kill on its own. Only when reclaim cannot free enough does the **cgroup OOM killer** pick a victim inside the group (the rest of the machine is untouched, which is the whole point). `memory.failcnt` counts how many times the limit was hit, even if the allocation then succeeded after reclaim (it was **0** here and **65** in the container run of section 5, where the hog pushed hard against the limit).

In v2 (from memory) `memory.max` is the hard limit and `memory.high` a soft one: above `high` the group is **throttled and reclaimed** but not killed, which gives a service time to react; `memory.events` lists `high`, `max`, `oom` and `oom_kill`; and `memory.oom.group` makes the kernel kill the whole group instead of one process. The v1 file `memory.limit_in_bytes` and v2 `memory.max` are both **per cgroup**, and a container limit is **not** what `free` inside the container reports unless a tool such as `lxcfs` fixes `/proc/meminfo` (from memory): `free`, `top` and many language runtimes read host values, which is why older JVMs sized their heap from the host's RAM.

### 3.5 cpuset and freezer

```text
--- 5. cpuset: pin a group to CPU 0
$ cat $C/cpuset/cpuset.cpus; cat $C/cpuset/lx09/cpuset.cpus
0-1
0
$ (echo $BASHPID > $C/cpuset/lx09/cgroup.procs; grep Cpus_allowed_list /proc/self/status)
Cpus_allowed_list:      0
$ grep Cpus_allowed_list /proc/self/status
Cpus_allowed_list:      0-1
```

```text
--- 6. freezer: stop every process in a group without signalling them
$ cat $C/freezer/lx09/freezer.state
THAWED
$ cat $C/freezer/lx09/freezer.state; grep State /proc/$S/status
FROZEN
State:  D (disk sleep)
$ cat $C/freezer/lx09/freezer.state
THAWED
```

A `cpuset` of `0` made the process see `Cpus_allowed_list: 0` instead of `0-1`: the group's CPUs are enforced in the scheduler (the mask is an input to `sched_setaffinity`). It is how Kubernetes' static CPU manager gives a guaranteed pod **exclusive** cores. The **freezer** stopped every process of a group without signalling them (the state read `FROZEN` and the frozen process showed `D (disk sleep)` in `/proc/PID/status`) and `THAWED` resumed them. Container checkpointing and `docker pause` use it (v2: `cgroup.freeze`).

### 3.6 What cgroups do not limit

Cgroups account for **CPU time, memory, I/O bandwidth and process count**. They do not give **isolation of latency** (a noisy neighbour still evicts the shared L3 cache and fills the memory bus), they do not limit **the kernel's own shared structures** except partly through the memory controller's kernel memory accounting, and **buffered writeback** I/O limits work only in v2 (the v1 `blkio` controller throttles direct I/O). The tools for the questions "what is this group using right now" are `systemd-cgtop`, `cat memory.stat`, `cat cpu.stat` and the PSI files, in the order of LX 05.


---

## 4. The root file system: `chroot`, `pivot_root` and overlayfs

### 4.1 An image is a directory tree

```text
--- 1. an image is a directory tree: here 1 binary and 15 links, no libc, no shell outside it
$ du -sk $R | cut -f1; ls $R/bin | wc -l; ls $R/bin/sh -l | cut -c1-20; file -L $R/bin/busybox | cut -d, -f1-2
2108
19
lrwxrwxrwx 1 root ro
/var/tmp/lx09_root/bin/busybox: ELF 64-bit LSB executable, x86-64
```

The root file system of the demo is **one static binary (`busybox`) with 15 links** and a few directories: 2,108 KB, no libc and no shell outside it. A real image is the same thing with more files: the user space of a distribution (a `libc`, `/etc`, tools) or just a static application. The container needs **no kernel** inside it, which is why images are small compared with VM disks.

### 4.2 `chroot`: a changed `/`, nothing else

```text
--- 2. chroot changes where "/" is for one process, and nothing else
$ chroot $R /bin/sh -c "ls /; cat /etc/motd"
bin
dev
etc
old
proc
tmp
hello-from-the-image
$ chroot $R /bin/sh -c "mount -t proc proc /proc; ps | head -3 | cut -c1-50; ls /proc | head -2"
PID   USER     COMMAND
    1 0        /process_api --firecracker-init --a
    2 0        [kthreadd]
1
10
$ findmnt -n -o TARGET,FSTYPE $R/proc; echo "(visible from the host: chroot does not make a mount namespace)"
/var/tmp/lx09_root/proc proc
(visible from the host: chroot does not make a mount namespace)
```

`chroot` changes where `/` points **for one process and its children**. It does nothing about PIDs, mounts or the network: after `mount -t proc proc /proc` inside the chroot, `ps` showed the **host's** PID 1 (`/process_api ...`), and `findmnt` on the host listed the proc mount at `$R/proc`, so a mount made "inside" was **visible from the host**: no mount namespace, no PID namespace.

### 4.3 `chroot` is not a security boundary

```text
--- 3. chroot is not a security boundary: root can walk out of it
$ chroot $R /bin/ls /var/tmp 2>&1 | head -1; chroot $R /bin/escape 2>&1 | head -4
ls: /var/tmp: No such file or directory
lx09_root
```

A process with root privilege (`CAP_SYS_CHROOT`) can leave: it creates a subdirectory, `chroot`s into it (so its working directory is now **outside** its root), walks up with `chdir("..")` until it reaches the real root and `chroot(".")`. The result above is `ls` printing the host's `/var/tmp` (`lx09_root`) after it could not find `/var/tmp` inside. The whole program:

```c
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
int main(void) {
    mkdir("sub", 0755); chroot("sub");          /* a second chroot below the first, cwd stays outside it */
    for (int i = 0; i < 40; i++) chdir("..");   /* walk up from the old working directory */
    chroot(".");                                /* "." is now the real root */
    execl("/bin/ls", "ls", "/var/tmp", (char *)0);
    return 1;
```

### 4.4 `pivot_root`: replace the root of the mount namespace

```text
--- 4. pivot_root swaps the root of the whole mount namespace; the old root is then unmounted
$ unshare --mount --pid --fork sh -c "mount --bind $R $R; cd $R; pivot_root . old; exec /bin/sh /bin/inner.sh"
root contains: bin dev etc old proc sub tmp
old root entries: 0
hello-from-the-image
processes:
PID   USER     COMMAND
    1 0        /bin/sh /bin/inner.sh
   13 0        {ps} /bin/sh /bin/inner.sh
mounts:
/dev/vda on / type ext4 (rw,relatime,discard,no_prefetch_block_bitmaps,resv_strict,resuid=65534,resgid=65534)
proc on /proc type proc (rw,relatime)
escape attempt:
ls: /var/tmp: No such file or directory
```

`pivot_root(new, put_old)` **swaps the root of the whole mount namespace**: the new tree becomes `/` and the old root is mounted at `put_old` (here `/old`), where it can then be **unmounted** (`umount -l /old`), after which **nothing of the host file system is reachable by any path**. The listing proves it: the directory `old` was empty (`old root entries: 0`), the mount table showed only the disk as `/` (its device, `/dev/vda`, appears because the demo pivoted into a directory on the host's disk) and `proc`, PID 1 was the container's own `/bin/sh /bin/inner.sh`, and the escape program that worked in section 4.3 found no `/var/tmp`. The recipe has fixed steps: new mount namespace, `mount --bind new new` (`pivot_root` requires the new root to be a **mount point**), `cd new`, `pivot_root . old`, `umount -l /old`. `runc` does exactly this (from memory: it falls back to `chroot` only where `pivot_root` is impossible, such as a root on a ramfs).

### 4.5 Layers and overlayfs

An image is a stack of **layers**, each a directory tree of changes (a Dockerfile step adds one). **overlayfs** presents the stack as one tree: a `lowerdir` list of read only layers, an `upperdir` for writes and a `workdir` it needs internally.

```text
 merged view  =  upper  (writable, per container)
                 lower2 (read only layer 2)      \  shared between all containers
                 lower1 (read only layer 1)      /  that use the image
```

The rules (from memory except where measured in section 5): a **read** finds the file in the topmost layer that has it; a **write to a file that exists only in a lower layer first copies the whole file up** to `upperdir` (**copy up**, whole file even for a one byte change, so a write heavy container on big files should use a **volume**); a **delete** creates a **whiteout** (a character device 0/0) in `upperdir` that hides the lower file; a deleted directory is marked **opaque** with an extended attribute. The container's changes live only in the upper directory, so **removing it resets the container**, and one image on disk serves any number of containers.

---

## 5. A container by hand

All five pieces together, as a script. The image is the busybox tree plus `/dev/null`, `/dev/zero` (made with `mknod`), a memory hog, a fork test and a start script. The container is: an **overlay** root, three **cgroups**, a **network namespace** joined to the host by a **`veth`** pair, and `unshare` for the mount, PID, UTS and IPC namespaces, then `pivot_root`.

```bash
#!/usr/bin/env bash
# x4_container.sh: a container assembled by hand: overlay root, namespaces, cgroups, veth, pivot_root
. "$(dirname "$0")/h.sh"
D=$(dirname "$(readlink -f "$0")")
B=/var/tmp/lx09_ctr; C=/sys/fs/cgroup
cleanup() {
  for c in memory cpu pids; do [ -d $C/$c/lx09ctr ] && { for p in $(cat $C/$c/lx09ctr/cgroup.procs); do kill -9 $p 2>/dev/null; done; sleep 0.2; rmdir $C/$c/lx09ctr 2>/dev/null; }; done
  umount $B/merged 2>/dev/null; ip netns del lx09ctr 2>/dev/null; ip link del lxh0 2>/dev/null; rm -rf $B
}
cleanup
mkdir -p $B/lower/{bin,proc,dev,etc,tmp,old} $B/upper $B/work $B/merged
cp /usr/bin/busybox $B/lower/bin/; (cd $B/lower/bin && for a in sh ls cat ps id mount umount echo sleep mkdir hostname head wc grep ip ping touch rm; do ln -sf busybox $a; done)
mknod -m 666 $B/lower/dev/null c 1 3; mknod -m 666 $B/lower/dev/zero c 1 5
gcc -static -O1 -o $B/lower/bin/hog $D/hog.c; gcc -static -O1 -o $B/lower/bin/forks $D/forks.c; echo "image 1.0" > $B/lower/etc/release; echo "do not modify" > $B/lower/etc/readonly.conf
echo '--- 1. layer 1: the image is the read-only lower directory, the container gets an empty upper directory'
mount -t overlay overlay -o lowerdir=$B/lower,upperdir=$B/upper,workdir=$B/work $B/merged
show 'ls $B/lower/etc | tr "\n" " "; echo; ls $B/upper | wc -l; ls $B/merged/etc | tr "\n" " "; echo'
echo; echo '--- 2. layer 2: cgroups with a memory limit, a CPU quota and a process limit'
for c in memory cpu pids; do mkdir $C/$c/lx09ctr; done
echo 48M > $C/memory/lx09ctr/memory.limit_in_bytes; echo 100000 > $C/cpu/lx09ctr/cpu.cfs_period_us; echo 50000 > $C/cpu/lx09ctr/cpu.cfs_quota_us; echo 16 > $C/pids/lx09ctr/pids.max
show 'echo "memory $(( $(cat $C/memory/lx09ctr/memory.limit_in_bytes) / 1048576 )) MiB, cpu $(( $(cat $C/cpu/lx09ctr/cpu.cfs_quota_us) / 1000 )) ms per $(( $(cat $C/cpu/lx09ctr/cpu.cfs_period_us) / 1000 )) ms, pids $(cat $C/pids/lx09ctr/pids.max)"'
echo; echo '--- 3. layer 3: a network namespace joined to the host by a veth pair (10.20.0.1 host, 10.20.0.2 container)'
ip netns add lx09ctr; ip link add lxh0 type veth peer name lxc0; ip link set lxc0 netns lx09ctr
ip addr add 10.20.0.1/24 dev lxh0; ip link set lxh0 up
ip -n lx09ctr addr add 10.20.0.2/24 dev lxc0; ip -n lx09ctr link set lxc0 up; ip -n lx09ctr link set lo up
show 'ip -n lx09ctr -br addr; ip -br addr show lxh0'
echo; echo '--- 4. the container: join the cgroups and the network namespace, create the others, pivot_root into the overlay'
cat > $B/lower/bin/init.sh <<'I'
umount -l /old
mount -t proc proc /proc
hostname container-1
echo "[hostname] $(hostname)  [pid] $$  [uid] $(id -u)"
echo "[processes]"; ps
echo "[net]"; ip addr | grep -E "^[0-9]+:|inet " | cut -c1-60
echo "[ping host]"; ping -c 2 -W 1 10.20.0.1 | grep -E "transmitted"
echo "[cgroup] $(cat /proc/self/cgroup | grep -E ':(memory|cpu|pids):' | cut -d: -f2,3 | tr '\n' ' ')"
echo "[mounts] $(mount | head -2 | cut -d' ' -f1,3,5 | tr '\n' ';')"
echo "[write] "; echo "changed in container" >> /etc/release; echo "new file" > /etc/created; cat /etc/release; cat /etc/created
echo "[memory 100 MiB against a 48 MiB limit]"; /bin/hog 100; echo "hog exit status $?"
echo "[memory 20 MiB]"; /bin/hog 20
echo "[processes: try 30 children against pids.max 16]"
/bin/forks 30
I
chmod +x $B/lower/bin/init.sh
show 'cd $B; for c in memory cpu pids; do echo $$ > $C/$c/lx09ctr/cgroup.procs; done; ip netns exec lx09ctr unshare --mount --pid --uts --ipc --fork sh -c "mount --bind $B/merged $B/merged; cd $B/merged; pivot_root . old; exec /bin/sh /bin/init.sh" 2>&1 | cut -c1-110'
echo "$$" > $C/memory/cgroup.procs; echo "$$" > $C/cpu/cgroup.procs; echo "$$" > $C/pids/cgroup.procs
echo; echo '--- 5. afterwards, from the host: the lower layer is untouched, the changes are in the upper layer'
show 'cat $B/lower/etc/release; ls $B/upper/etc; cat $B/upper/etc/release | head -2'
show 'grep -E "^oom_kill " $C/memory/lx09ctr/memory.oom_control; echo "failcnt $(cat $C/memory/lx09ctr/memory.failcnt), peak $(( $(cat $C/memory/lx09ctr/memory.max_usage_in_bytes) / 1048576 )) MiB"'
show 'cat $C/pids/lx09ctr/pids.events; grep -E "nr_throttled|nr_periods" $C/cpu/lx09ctr/cpu.stat | tr "\n" " "; echo'
cleanup
```

The helper programs: `hog` allocates and touches MiB until it stops, `forks` starts children until `fork` fails.

```c
/* hog.c MIB: allocate and touch MIB mebibytes, then report; used to hit a memory limit */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 100;
    for (int i = 1; i <= n; i++) {
        char *p = malloc(1 << 20);
        if (!p) { printf("malloc failed at %d MiB\n", i); return 2; }
        memset(p, 1, 1 << 20);
    }
    printf("allocated %d MiB\n", n);
    return 0;
}
```

```text
--- 1. layer 1: the image is the read-only lower directory, the container gets an empty upper directory
$ ls $B/lower/etc | tr "\n" " "; echo; ls $B/upper | wc -l; ls $B/merged/etc | tr "\n" " "; echo
readonly.conf release
0
readonly.conf release

--- 2. layer 2: cgroups with a memory limit, a CPU quota and a process limit
$ echo "memory $(( $(cat $C/memory/lx09ctr/memory.limit_in_bytes) / 1048576 )) MiB, cpu $(( $(cat $C/cpu/lx09ctr/cpu.cfs_quota_us) / 1000 )) ms per $(( $(cat $C/cpu/lx09ctr/cpu.cfs_period_us) / 1000 )) ms, pids $(cat $C/pids/lx09ctr/pids.max)"
memory 48 MiB, cpu 50 ms per 100 ms, pids 16

--- 3. layer 3: a network namespace joined to the host by a veth pair (10.20.0.1 host, 10.20.0.2 container)
$ ip -n lx09ctr -br addr; ip -br addr show lxh0
lo               UNKNOWN        127.0.0.1/8
lxc0@if10        UP             10.20.0.2/24
lxh0@if9         UP             10.20.0.1/24

--- 4. the container: join the cgroups and the network namespace, create the others, pivot_root into the overlay
$ cd $B; for c in memory cpu pids; do echo $$ > $C/$c/lx09ctr/cgroup.procs; done; ip netns exec lx09ctr unshare --mount --pid --uts --ipc --fork sh -c "mount --bind $B/merged $B/merged; cd $B/merged; pivot_root . old; exec /bin/sh /bin/init.sh" 2>&1 | cut -c1-110
[hostname] container-1  [pid] 1  [uid] 0
[processes]
PID   USER     COMMAND
    1 0        /bin/sh /bin/init.sh
    9 0        {ps} /bin/sh /bin/init.sh
[net]
1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue qlen 1
    inet 127.0.0.1/8 scope host lo
9: lxc0@if10: <BROADCAST,MULTICAST,UP,LOWER_UP,M-DOWN> mtu 1
    inet 10.20.0.2/24 scope global lxc0
[ping host]
2 packets transmitted, 2 packets received, 0% packet loss
[cgroup] pids:/lx09ctr memory:/lx09ctr cpu:/lx09ctr
[mounts] overlay / overlay;proc /proc proc;
[write]
image 1.0
changed in container
new file
[memory 100 MiB against a 48 MiB limit]
Killed
hog exit status 137
[memory 20 MiB]
allocated 20 MiB
[processes: try 30 children against pids.max 16]
fork allowed 11 of 30, then: Resource temporarily unavailable

--- 5. afterwards, from the host: the lower layer is untouched, the changes are in the upper layer
$ cat $B/lower/etc/release; ls $B/upper/etc; cat $B/upper/etc/release | head -2
image 1.0
created
release
image 1.0
changed in container
$ grep -E "^oom_kill " $C/memory/lx09ctr/memory.oom_control; echo "failcnt $(cat $C/memory/lx09ctr/memory.failcnt), peak $(( $(cat $C/memory/lx09ctr/memory.max_usage_in_bytes) / 1048576 )) MiB"
oom_kill 1
failcnt 65, peak 48 MiB
$ cat $C/pids/lx09ctr/pids.events; grep -E "nr_throttled|nr_periods" $C/cpu/lx09ctr/cpu.stat | tr "\n" " "; echo
max 1
nr_periods 3 nr_throttled 0
```

How to read it, line by line:

- **Layer 1.** The image directory `lower` holds `etc/release` (`image 1.0`) and `etc/readonly.conf`; the overlay `merged` shows both and the empty `upper` directory has 0 entries.
- **Layer 2.** The cgroups were created before the container and the container joined them with `echo $$ > cgroup.procs`: 48 MiB of memory, a quota of 50 ms per 100 ms (half a CPU) and 16 processes. From inside, `/proc/self/cgroup` shows `pids:/lx09ctr memory:/lx09ctr cpu:/lx09ctr`.
- **Layer 3.** `lxh0` (10.20.0.1) stays on the host, `lxc0` (10.20.0.2) went into the namespace. The container's own view lists `lo` and `lxc0`, and `ping` to the host answered **2 of 2**.
- **Layer 4.** `unshare` created the mount, PID, UTS and IPC namespaces; `pivot_root` into the overlay; `init.sh` unmounted the old root and mounted a private `/proc`. The result: hostname `container-1`, the shell is **PID 1** and `ps` shows only `init.sh` and the `ps` itself, `mount` shows `overlay /` and `proc /proc`.
- **Writes.** The container changed `/etc/release` and created `/etc/created`. From the host the **lower** file was still `image 1.0`, and the **upper** directory held `etc/release` (the changed copy, **copied up** from the lower layer) and `etc/created`.
- **Memory.** `hog 100` against the 48 MiB limit ended `Killed` with exit status **137**, `oom_kill` was **1**, `memory.failcnt` **65** and the peak 48 MiB. `hog 20` allocated 20 MiB without trouble.
- **Processes.** `forks 30` against `pids.max 16` reported `fork allowed 11 of 30` (not 16: the `unshare`, `sh` and `ip` processes that set the container up are in the same cgroup and count) and `pids.events` showed `max 1`.
- **CPU.** `nr_periods 3, nr_throttled 0`: the container used almost no CPU, so the quota was never hit. The quota is checked in section 3.2 where a spin hits it.

What this container does **not** have, which is the rest of the section 7 list: it runs as **root with the full capability set** (a hand made container is not safe), no seccomp filter, no user namespace and no LSM profile. `runc` and Docker add those by default.

---

## 6. Container networking

A container's network namespace is empty (section 2.3). Connecting it is a pipeline from LX 08:

1. a **`veth` pair**: one end in the container's namespace (`eth0` there), the other on the host;
2. the host end is attached to a **bridge** (`docker0`, `cni0`) that has the gateway address, or, as in section 5, just given an address (a point to point link);
3. the container gets an address and a **default route through the bridge**;
4. the host **forwards** (`net.ipv4.ip_forward=1`) and **masquerades** (`nft` or `iptables` NAT, LX 08 section 6) so that the container's private address becomes the host's address on the way out;
5. **published ports** (`-p 8080:80`) are a **DNAT** rule in `prerouting` that rewrites the host's port 8080 to the container's address and port 80, plus the matching conntrack entry for the reply path.

The measured part is steps 1 and 3 (section 5: `lxc0@if10` with `10.20.0.2/24` in the namespace, `lxh0@if9` with `10.20.0.1/24` on the host, ping 2 of 2). Steps 2, 4 and 5 are exactly the bridge, masquerade and port forwarding measured in LX 08 (the server saw the client as `10.2.0.1` behind masquerade), and the Docker bridge itself was not run here (`--bridge=none`). Two practical points: **containers on one bridge talk through the bridge and the host's firewall rules**, so a `FORWARD` policy of `drop` blocks them; and **`--network host`** puts the container in the **host's** network namespace (no `veth`, no NAT, ports are the host's ports), which is faster and removes network isolation. In Kubernetes all containers of a pod share **one network namespace** (created by the pause container), so they talk over `localhost`, and a **CNI plugin** (Calico, Cilium, flannel) does steps 1 to 4 for every pod.


---

## 7. What the process may do: capabilities, `no_new_privs`, seccomp, user namespaces

Namespaces and cgroups say what a process sees and uses. A container that runs as root with a full capability set can still do harm to the shared kernel. Four mechanisms narrow that.

### 7.1 Capabilities

**Capabilities** split root's power into about 40 bits (`CAP_CHOWN`, `CAP_NET_ADMIN`, `CAP_SYS_ADMIN`, `CAP_SYS_PTRACE` and so on). A process has several sets in `/proc/PID/status`: `CapEff` (checked now), `CapPrm` (allowed), `CapInh`, `CapBnd` (the **bounding set**, the ceiling that no `exec` can exceed) and `CapAmb`. `capsh --decode=MASK` names the bits.

```text
--- 1. a root process holds every capability; a container gets a short list
$ grep -E "^(CapEff|CapBnd)" /proc/self/status
CapEff: 000001fffeffffff
CapBnd: 000001fffeffffff
$ docker run --rm --network none lx:1 /bin/sh -c "grep -E \"^(CapEff|CapBnd)\" /proc/self/status"
CapEff: 00000000a80425fb
CapBnd: 00000000a80425fb
$ capsh --decode=00000000a80425fb | tr "," "\n" | wc -l; capsh --decode=00000000a80425fb | cut -c1-150
14
0x00000000a80425fb=cap_chown,cap_dac_override,cap_fowner,cap_fsetid,cap_kill,cap_setgid,cap_setuid,cap_setpcap,cap_net_bind_service,cap_net_raw,cap_sy
$ docker run --rm --network none --cap-drop ALL lx:1 /bin/sh -c "grep -E \"^(CapEff|CapBnd)\" /proc/self/status"
CapEff: 0000000000000000
CapBnd: 0000000000000000
```

```text
--- 2. dropping one capability removes exactly one privilege
$ setpriv --bounding-set -chown --inh-caps -chown sh -c "touch $W/f; chown 1234 $W/f 2>&1"
chown: changing ownership of '/var/tmp/lx09_sec/f': Operation not permitted
$ sh -c "chown 1234 $W/f; ls -ln $W/f | cut -d\" \" -f3"
1234
```

Root on the VM holds mask `000001fffeffffff` (40 bits). A Docker container, **still uid 0 inside**, got `00000000a80425fb`: 14 capabilities (`chown`, `dac_override`, `fowner`, `fsetid`, `kill`, `setgid`, `setuid`, `setpcap`, `net_bind_service`, `net_raw`, `sys_chroot`, `mknod`, `audit_write`, `setfcap`), and `--cap-drop ALL` gave **0**. Dropping a single bit takes exactly that privilege: with `chown` removed from the bounding set the `chown 1234` failed with `Operation not permitted`, and the same command with the full set changed the owner to 1234. `CAP_SYS_ADMIN` is the one to deny: it is the catch all that gates `mount`, namespace creation and many ioctls, and a container that holds it is nearly as strong as root on the host.

### 7.2 `no_new_privs`

```c
#include <stdio.h>
#include <unistd.h>
int main(void) { printf("ruid=%d euid=%d\n", getuid(), geteuid()); return 0; }
```

```text
--- 3. no_new_privs: a setuid program stops giving privilege
$ as 1001 $W/euid
ruid=1001 euid=0
$ as 1001 setpriv --no-new-privs $W/euid
ruid=1001 euid=1001
$ as 1001 setpriv --no-new-privs grep NoNewPrivs /proc/self/status
NoNewPrivs:     1
```

A setuid root binary run by uid 1001 gave `euid=0`. The same run under `setpriv --no-new-privs` gave `euid=1001`: the bit `NoNewPrivs` is set and **`execve` can never again grant privilege** (setuid and setgid bits, file capabilities and some LSM transitions are ignored). The bit is inherited and cannot be cleared. It is a requirement for an **unprivileged process to install a seccomp filter** and a safeguard for containers (`--security-opt no-new-privileges`, Kubernetes `allowPrivilegeEscalation: false`).

### 7.3 seccomp

A **seccomp filter** is a small BPF program that the kernel runs on **every system call** of a process (and its descendants) and that answers: allow, return an `errno`, kill the process or notify a supervisor. It does not see pointers' contents, only the call number and register arguments. It shrinks the kernel attack surface a container exposes.

```text
--- 4. seccomp: a filter on system calls, chosen per container
$ docker run --rm --network none lx:1 /bin/sh -c "grep -E \"^(Seccomp|Seccomp_filters)\" /proc/self/status"
Seccomp:        2
Seccomp_filters:        1
$ docker run --rm --network none --security-opt seccomp=unconfined lx:1 /bin/sh -c "grep -E \"^Seccomp:\" /proc/self/status"
Seccomp:        0
$ docker run --rm --network none lx:1 /bin/sh -c "mkdir /tmp/a && echo mkdir allowed"
mkdir allowed
$ docker run --rm --network none --security-opt seccomp=$W/nomkdir.json lx:1 /bin/sh -c "mkdir /tmp/a && echo mkdir allowed; touch /tmp/b && echo touch allowed"
mkdir: can't create directory '/tmp/a': Operation not permitted
touch allowed
```

Docker's default profile showed as `Seccomp: 2` (filter mode) with one filter, `seccomp=unconfined` as `Seccomp: 0`. A tiny custom profile (`defaultAction` allow, `mkdir` and `mkdirat` returning `EPERM`) made `mkdir` fail with `Operation not permitted` while `touch` worked, so the filter is checked per call and does not care what the program is. The default profile (from memory) allows most calls and blocks ones such as `kexec_load`, `reboot`, `mount`, `bpf` and `keyctl`, and is why a tool that needs a blocked call fails inside a container with `EPERM` while it works on the host. Kubernetes selects it with `securityContext.seccompProfile.type: RuntimeDefault` (from memory), and a strace of a failing program shows the call that returned `EPERM`.

### 7.4 User namespaces and rootless containers

Section 2.4 showed the mechanism. The security statement is: with a user namespace, **uid 0 in the container is an unprivileged uid on the host**, so a process that escapes the other isolation holds only that uid's file permissions.

```text
--- 5. a user namespace keeps "root" inside from being root outside
$ as 1000 unshare --user --map-root-user sh -c "id -u; cat /etc/shadow 2>&1 | head -1; touch /root/x 2>&1 | head -1; touch /var/tmp/lx09_sec/own && echo created; ls -ln /var/tmp/lx09_sec/own | cut -d\" \" -f3"
0
cat: /etc/shadow: Permission denied
touch: cannot touch '/root/x': Permission denied
touch: cannot touch '/var/tmp/lx09_sec/own': Permission denied
ls: cannot access '/var/tmp/lx09_sec/own': No such file or directory
$ docker run --rm --network none --privileged lx:1 /bin/sh -c "grep -E \"^(CapEff|Seccomp):\" /proc/self/status"
CapEff: 000001fffeffffff
Seccomp:        0
```

As uid 1000 with `--map-root-user`, the process was uid 0 inside but `cat /etc/shadow` failed with `Permission denied`, as did `touch /root/x`, and creating a file in a directory owned by root failed too: its power is limited to what its mapped host uid (1000) may do. The last command shows the opposite case: `docker run --privileged` handed the container the **full capability set** (`000001fffeffffff`, the same as host root) and **no seccomp filter**, so `--privileged` removes nearly every protection in this section and is for debugging only. Docker's default does **not** use a user namespace (the container's root is the host's root with fewer capabilities); `--userns-remap`, **rootless Docker**, **rootless Podman** and Kubernetes `hostUsers: false` (from memory) do.

The remaining layer is a **Linux Security Module**: **AppArmor** (path based, default on Debian and Ubuntu) or **SELinux** (label based, default on Red Hat systems) confines a container's file and network access even as root. They were not available on this VM.

---

## 8. Runtimes, images and OCI

### 8.1 What Docker actually did

```text
--- 1. a container is a process on the host kernel
$ uname -r
6.18.44-fc-v470
$ docker run --rm --network none lx:1 /bin/busybox uname -r
6.18.44-fc-v470
--- 2. the limits as the kernel sees them (host pid 3362)
$ docker inspect -f "memory={{.HostConfig.Memory}} nanocpus={{.HostConfig.NanoCpus}} pids={{.HostConfig.PidsLimit}}" lx9a
memory=67108864 nanocpus=500000000 pids=32
$ cat /proc/3362/cgroup | grep -E 'memory|cpu,|pids'
8:pids:/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7
4:memory:/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7
$ cat /sys/fs/cgroup/memory/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/memory.limit_in_bytes
67108864
$ cat /sys/fs/cgroup/cpu/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/cpu.cfs_quota_us /sys/fs/cgroup/cpu/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/cpu.cfs_period_us
50000
100000
$ cat /sys/fs/cgroup/pids/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/pids.max
32
$ ls -l /proc/3362/ns | awk '{print $9, $11}' | grep -E 'mnt|net|pid|uts|ipc|user|cgroup' | tr '\n' ' '; echo
cgroup cgroup:[4026531835] ipc ipc:[4026532266] mnt mnt:[4026532264] net net:[4026532268] pid pid:[4026532267] pid_for_children pid:[4026532267] user user:[4026531837] uts uts:[4026532265]
$ ls -l /proc/self/ns | awk '{print $9, $11}' | grep -E 'mnt|net|pid|uts|ipc|user|cgroup' | tr '\n' ' '; echo
cgroup cgroup:[4026531835] ipc ipc:[4026531839] mnt mnt:[4026531832] net net:[4026531833] pid pid:[4026531836] pid_for_children pid:[4026531836] user user:[4026531837] uts uts:[4026531838]
--- 3. the process tree: runtime shim, then the container process
$ ps -o pid,ppid,args -p 3362,$(ps -o ppid= -p 3362 | tr -d ' ') | cut -c1-90
  PID  PPID COMMAND
 3338     1 /usr/bin/containerd-shim-runc-v2 -namespace moby -id 19f2816167c1a083d3f590c6f
 3362  3338 /bin/sleep 300
--- 4. OOMKilled is the cgroup OOM killer, reported as exit 137
$ docker run --name lx9b --network none --memory 32m lx:1 /bin/sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a); echo done'; echo "exit status $?"
exit status 137
$ docker inspect -f "OOMKilled={{.State.OOMKilled}} ExitCode={{.State.ExitCode}}" lx9b
OOMKilled=true ExitCode=137
--- 5. image layers are directories that overlayfs stacks
$ docker image inspect -f "{{len .RootFS.Layers}} layer(s), type {{.RootFS.Type}}" lx:1
1 layer(s), type layers
$ docker run --rm --network none lx:1 /bin/sh -c "grep \" / \" /proc/self/mounts | cut -c1-60"
overlay / overlay rw,relatime,lowerdir=/tmp/lxdocker/contain
--- 6. runc: the same thing from a config.json
$ grep -E '"(type|path)": "(pid|network|mount|ipc|uts|cgroup)"|"terminal"|"readonly"' /tmp/b1/config.json | tr -s ' ' | head -12
 "terminal": false,
 "readonly": false
 "type": "cgroup",
 "type": "pid"
 "type": "network"
 "type": "ipc"
 "type": "uts"
 "type": "mount"
$ grep -c . /tmp/b1/config.json
172
$ cd /tmp/b1 && runc run lx9r < /dev/null 2>&1 | head -3; true
uid=0 gid=0
runc
PID   USER     COMMAND
```

```text
--- 2. the limits as the kernel sees them (host pid 3362)
$ docker inspect -f "memory={{.HostConfig.Memory}} nanocpus={{.HostConfig.NanoCpus}} pids={{.HostConfig.PidsLimit}}" lx9a
memory=67108864 nanocpus=500000000 pids=32
$ cat /proc/3362/cgroup | grep -E 'memory|cpu,|pids'
8:pids:/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7
4:memory:/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7
$ cat /sys/fs/cgroup/memory/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/memory.limit_in_bytes
67108864
$ cat /sys/fs/cgroup/cpu/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/cpu.cfs_quota_us /sys/fs/cgroup/cpu/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/cpu.cfs_period_us
50000
100000
$ cat /sys/fs/cgroup/pids/docker/19f2816167c1a083d3f590c6ff4bd849579d71429667dfed51e43c2d6d827bb7/pids.max
32
$ ls -l /proc/3362/ns | awk '{print $9, $11}' | grep -E 'mnt|net|pid|uts|ipc|user|cgroup' | tr '\n' ' '; echo
cgroup cgroup:[4026531835] ipc ipc:[4026532266] mnt mnt:[4026532264] net net:[4026532268] pid pid:[4026532267] pid_for_children pid:[4026532267] user user:[4026531837] uts uts:[4026532265]
$ ls -l /proc/self/ns | awk '{print $9, $11}' | grep -E 'mnt|net|pid|uts|ipc|user|cgroup' | tr '\n' ' '; echo
cgroup cgroup:[4026531835] ipc ipc:[4026531839] mnt mnt:[4026531832] net net:[4026531833] pid pid:[4026531836] pid_for_children pid:[4026531836] user user:[4026531837] uts uts:[4026531838]
--- 3. the process tree: runtime shim, then the container process
$ ps -o pid,ppid,args -p 3362,$(ps -o ppid= -p 3362 | tr -d ' ') | cut -c1-90
  PID  PPID COMMAND
 3338     1 /usr/bin/containerd-shim-runc-v2 -namespace moby -id 19f2816167c1a083d3f590c6f
 3362  3338 /bin/sleep 300
--- 4. OOMKilled is the cgroup OOM killer, reported as exit 137
$ docker run --name lx9b --network none --memory 32m lx:1 /bin/sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a); echo done'; echo "exit status $?"
exit status 137
$ docker inspect -f "OOMKilled={{.State.OOMKilled}} ExitCode={{.State.ExitCode}}" lx9b
OOMKilled=true ExitCode=137
--- 5. image layers are directories that overlayfs stacks
$ docker image inspect -f "{{len .RootFS.Layers}} layer(s), type {{.RootFS.Type}}" lx:1
1 layer(s), type layers
$ docker run --rm --network none lx:1 /bin/sh -c "grep \" / \" /proc/self/mounts | cut -c1-60"
overlay / overlay rw,relatime,lowerdir=/tmp/lxdocker/contain
--- 6. runc: the same thing from a config.json
$ grep -E '"(type|path)": "(pid|network|mount|ipc|uts|cgroup)"|"terminal"|"readonly"' /tmp/b1/config.json | tr -s ' ' | head -12
 "terminal": false,
 "readonly": false
 "type": "cgroup",
 "type": "pid"
 "type": "network"
 "type": "ipc"
 "type": "uts"
 "type": "mount"
$ grep -c . /tmp/b1/config.json
172
$ cd /tmp/b1 && runc run lx9r < /dev/null 2>&1 | head -3; true
uid=0 gid=0
runc
PID   USER     COMMAND
```

`docker run --memory 64m --cpus 0.5 --pids-limit 32` is **three cgroup files**: `memory.limit_in_bytes` 67108864 (64 MiB), `cpu.cfs_quota_us` 50000 on `cpu.cfs_period_us` 100000 (half a CPU) and `pids.max` 32, under a cgroup named `docker/<container id>` that the `/proc/PID/cgroup` file shows. The container process has **its own** `mnt`, `net`, `pid`, `ipc` and `uts` namespaces (inodes 4026532264 and up) and **the host's** `user` and `cgroup` namespaces (the same inodes as the host shell: `user:[4026531837]`, `cgroup:[4026531835]`), which is the default described in section 7.4. The container's `uname -r` is the host kernel's release.

```text
--- 3. the process tree: runtime shim, then the container process
$ ps -o pid,ppid,args -p 3362,$(ps -o ppid= -p 3362 | tr -d ' ') | cut -c1-90
  PID  PPID COMMAND
 3338     1 /usr/bin/containerd-shim-runc-v2 -namespace moby -id 19f2816167c1a083d3f590c6f
 3362  3338 /bin/sleep 300
--- 4. OOMKilled is the cgroup OOM killer, reported as exit 137
$ docker run --name lx9b --network none --memory 32m lx:1 /bin/sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a); echo done'; echo "exit status $?"
exit status 137
$ docker inspect -f "OOMKilled={{.State.OOMKilled}} ExitCode={{.State.ExitCode}}" lx9b
OOMKilled=true ExitCode=137
--- 5. image layers are directories that overlayfs stacks
$ docker image inspect -f "{{len .RootFS.Layers}} layer(s), type {{.RootFS.Type}}" lx:1
1 layer(s), type layers
$ docker run --rm --network none lx:1 /bin/sh -c "grep \" / \" /proc/self/mounts | cut -c1-60"
overlay / overlay rw,relatime,lowerdir=/tmp/lxdocker/contain
--- 6. runc: the same thing from a config.json
$ grep -E '"(type|path)": "(pid|network|mount|ipc|uts|cgroup)"|"terminal"|"readonly"' /tmp/b1/config.json | tr -s ' ' | head -12
 "terminal": false,
 "readonly": false
 "type": "cgroup",
 "type": "pid"
 "type": "network"
 "type": "ipc"
 "type": "uts"
 "type": "mount"
$ grep -c . /tmp/b1/config.json
172
$ cd /tmp/b1 && runc run lx9r < /dev/null 2>&1 | head -3; true
uid=0 gid=0
runc
PID   USER     COMMAND
```

The process tree has **no VM boundary**: on the host the container is a plain process (`/bin/sleep 300`) whose parent is **`containerd-shim-runc-v2`**. The shim stays to hold the container's stdio and exit status after `runc` has exited, and lets `containerd` and `dockerd` restart without killing containers.

```text
--- 4. OOMKilled is the cgroup OOM killer, reported as exit 137
$ docker run --name lx9b --network none --memory 32m lx:1 /bin/sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a); echo done'; echo "exit status $?"
exit status 137
$ docker inspect -f "OOMKilled={{.State.OOMKilled}} ExitCode={{.State.ExitCode}}" lx9b
OOMKilled=true ExitCode=137
--- 5. image layers are directories that overlayfs stacks
$ docker image inspect -f "{{len .RootFS.Layers}} layer(s), type {{.RootFS.Type}}" lx:1
1 layer(s), type layers
$ docker run --rm --network none lx:1 /bin/sh -c "grep \" / \" /proc/self/mounts | cut -c1-60"
overlay / overlay rw,relatime,lowerdir=/tmp/lxdocker/contain
--- 6. runc: the same thing from a config.json
$ grep -E '"(type|path)": "(pid|network|mount|ipc|uts|cgroup)"|"terminal"|"readonly"' /tmp/b1/config.json | tr -s ' ' | head -12
 "terminal": false,
 "readonly": false
 "type": "cgroup",
 "type": "pid"
 "type": "network"
 "type": "ipc"
 "type": "uts"
 "type": "mount"
$ grep -c . /tmp/b1/config.json
172
$ cd /tmp/b1 && runc run lx9r < /dev/null 2>&1 | head -3; true
uid=0 gid=0
runc
PID   USER     COMMAND
```

A container whose limit is 32 MiB and whose shell tried to hold about 100 MB in a variable was killed: `docker inspect` showed `OOMKilled=true` and `ExitCode=137`. **`OOMKilled` is the cgroup OOM killer** of section 3.4, and `137` is `128 + 9`. Exit code 137 **without** `OOMKilled=true` means somebody sent `SIGKILL` (for example `docker kill`, or `docker stop` after its timeout). Exit code 143 is `SIGTERM` (128 + 15).

### 8.2 Layers, images and the stack of tools

```text
--- 5. image layers are directories that overlayfs stacks
$ docker image inspect -f "{{len .RootFS.Layers}} layer(s), type {{.RootFS.Type}}" lx:1
1 layer(s), type layers
$ docker run --rm --network none lx:1 /bin/sh -c "grep \" / \" /proc/self/mounts | cut -c1-60"
overlay / overlay rw,relatime,lowerdir=/tmp/lxdocker/contain
--- 6. runc: the same thing from a config.json
$ grep -E '"(type|path)": "(pid|network|mount|ipc|uts|cgroup)"|"terminal"|"readonly"' /tmp/b1/config.json | tr -s ' ' | head -12
 "terminal": false,
 "readonly": false
 "type": "cgroup",
 "type": "pid"
 "type": "network"
 "type": "ipc"
 "type": "uts"
 "type": "mount"
$ grep -c . /tmp/b1/config.json
172
$ cd /tmp/b1 && runc run lx9r < /dev/null 2>&1 | head -3; true
uid=0 gid=0
runc
PID   USER     COMMAND
```

The test image has **1 layer** (imported from a tarball) and the container's root was `overlay`. Real images are stacks of content addressed layers; the **OCI image spec** defines a manifest listing a config JSON (environment, entrypoint, user) and the layer tarballs by digest, and the **OCI runtime spec** defines the bundle that `runc` runs.

```text
--- 6. runc: the same thing from a config.json
$ grep -E '"(type|path)": "(pid|network|mount|ipc|uts|cgroup)"|"terminal"|"readonly"' /tmp/b1/config.json | tr -s ' ' | head -12
 "terminal": false,
 "readonly": false
 "type": "cgroup",
 "type": "pid"
 "type": "network"
 "type": "ipc"
 "type": "uts"
 "type": "mount"
$ grep -c . /tmp/b1/config.json
172
$ cd /tmp/b1 && runc run lx9r < /dev/null 2>&1 | head -3; true
uid=0 gid=0
runc
PID   USER     COMMAND
```

A bundle is a `rootfs/` directory and a **`config.json`** (172 lines here) that lists the `namespaces` to create (`pid`, `network`, `ipc`, `uts`, `mount` and `cgroup`), `capabilities`, `mounts`, the `process` (args, uid, `noNewPrivileges`), `linux.resources` (the cgroup limits), the `seccomp` profile and `rlimits`. `runc run NAME` in that directory started the container: the output above is its `id` and `ps` (`uid=0 gid=0`, then the process list). The whole of section 5 is what `runc` automates from this file.

The layers of tools, from the user to the kernel (from memory, matching what was observed):

```text
 docker / nerdctl / kubectl   ->  dockerd or kubelet  ->  containerd   (images, snapshots, lifecycle)
 containerd  ->  containerd-shim-runc-v2  ->  runc   (OCI runtime: clone, pivot_root, cgroups, seccomp, exec)
 Kubernetes talks to containerd or CRI-O through the CRI; the OCI runtime may be runc, crun, gVisor or Kata
```

`runc` creates the container and **exits**; the shim and the container remain. **crun** is a C reimplementation (smaller, faster start), **gVisor** puts a user space kernel between the container and the host kernel, and **Kata Containers** run each pod in a lightweight VM, trading the shared kernel for hardware isolation (section 10).

---

## 9. Kubernetes requests and limits become cgroup files

From memory (no Kubernetes was run), with the mechanism measured above. For a container with `resources.requests` and `resources.limits`:

| Field | CPU | Memory |
| --- | --- | --- |
| **request** | scheduler placement and, in cgroup v1, **`cpu.shares`** = `milliCPU * 1024 / 1000` (a request of `500m` gives 512); in v2, `cpu.weight` | scheduler placement; with the `MemoryQoS` feature, `memory.min` |
| **limit** | **`cpu.cfs_quota_us`** = `milliCPU * 100` on a period of 100,000 µs (a limit of `500m` gives 50,000, the value measured in section 8) | **`memory.limit_in_bytes`** (v1) or **`memory.max`** (v2) |
| exceeded | **throttled** (never killed) | **OOM killed**: `OOMKilled`, exit code 137 |

Behaviour to know:

- **CPU is compressible, memory is not.** Over the CPU limit a container is slowed; over the memory limit it is killed. A request with no limit lets a container use idle CPU (and be preempted by shares under contention), which is why many teams set **memory limits but no CPU limits**.
- **QoS classes** come from the numbers: **Guaranteed** (requests equal limits for every container), **Burstable** (some request or limit set) and **BestEffort** (none). Under node memory pressure the kubelet evicts BestEffort first, then Burstable above their request, and the **kernel OOM score** (`oom_score_adj`: -997 for Guaranteed, 1000 for BestEffort, in between for Burstable, from memory) decides who dies when the node itself runs out.
- **Throttling is invisible in average CPU use.** A pod at 40 percent of its limit on average can be throttled in every burst (section 3.2). Watch `container_cpu_cfs_throttled_periods_total` over `container_cpu_cfs_periods_total` (the cAdvisor names of `nr_throttled` and `nr_periods`) and the request latency tail.
- **`OOMKilled` triage:** the container restarts (`RESTARTS` rises, `kubectl describe pod` shows `Last State: Terminated, Reason: OOMKilled, Exit Code: 137`). Check the limit against the working set (`memory.stat`, `container_memory_working_set_bytes`), not against `free` inside the container, and check for page cache or a memory leak. A pod that is **evicted** (not killed by the kernel) says `Evicted` and the node's memory pressure, a different mechanism.
- A **pids limit** and **ephemeral storage limit** exist too (`podPidsLimit` and `ephemeral-storage`, the latter enforced by the kubelet, not the kernel, from memory).

---

## 10. Containers against virtual machines

| | Container | Virtual machine |
| --- | --- | --- |
| isolation boundary | kernel features (namespaces, cgroups, seccomp, LSM) | hardware virtualisation (VT-x, EPT) and a hypervisor |
| kernel | **shared with the host** (measured: same `uname -r`) | each guest has its own |
| start time | milliseconds to a second (a process start) | seconds (firmware, kernel boot), less for microVMs |
| memory overhead | the process only | the guest kernel and its page cache, per VM |
| density | hundreds per host | tens per host |
| attack surface | the **whole system call interface** of the host kernel | the hypervisor and device models, much smaller |
| different OS or kernel version | no | yes |
| noisy neighbour | cgroup limits only; shared caches and kernel locks | strong CPU and memory partitioning, shared caches still |

A container is the right unit to **package and schedule** a program; a VM is the right unit for **mutually distrusting tenants**. That is why cloud providers run customer containers **inside VMs**, and why **Firecracker** and **Kata** exist: a minimal VM per function or pod with the speed of a container. The VM used for these measurements is a Firecracker microVM, and containers on it share its single guest kernel.

---

## 11. What this VM could not run

| Not run | What to know (from memory) |
| --- | --- |
| cgroup v2 controllers | `cpu.max`, `memory.max`, `memory.high`, `io.max`, `cgroup.kill`, PSI, delegation; table in section 3.1 |
| systemd, `systemd-run`, `systemd-cgls`, `systemd-cgtop` | the usual way to create a transient cgroup with limits; systemd is the cgroup v2 manager |
| Kubernetes, CNI, kubelet | section 9 |
| rootless Docker or Podman | needs `newuidmap` and subuid ranges; networking via `slirp4netns` or `pasta` since a user cannot create a host `veth` |
| `docker0` bridge, published ports, `iptables` rules | DNAT in `prerouting`, `MASQUERADE` in `postrouting`, the `DOCKER` chain; `-p` also starts `docker-proxy` for loopback |
| AppArmor, SELinux | profile per container, path or label based confinement |
| multi layer images, a registry, `docker build` | OCI layers pulled by digest; `docker build` makes a layer per step |
| gVisor, Kata, crun | alternative OCI runtimes (section 8.2) |
| user namespace with a uid range | `/etc/subuid`, `newuidmap`; only a one uid map was run |


---

## 12. What it costs

| Item | Cost (measured here unless marked) |
| --- | --- |
| start a namespace set with `unshare` | one process start, a few milliseconds (no number was recorded, so none is claimed) |
| a container root file system | 2,108 KB for the busybox tree; shared lower layers cost nothing per container, only the upper directory |
| memory of a container | the processes' memory plus kernel objects (page tables, cgroup structures); no guest kernel (from memory, tens of KiB for the cgroup and namespace objects) |
| CPU quota enforcement | throttling: unlimited 2.97 s of CPU in 3 s, capped at 20 percent about 0.6 s (section 3.2), and a stall of up to a period for each throttled burst |
| `seccomp` | a BPF program runs on every system call; small per call and a measurable cost only on call heavy loads (from memory) |
| overlayfs copy up | the **whole file** is copied to the upper layer on the first write (from memory); a volume avoids it |
| network (`veth`, bridge, NAT) | an extra hop and conntrack entry per flow; `--network host` removes it (from memory, see LX 08 for the lab numbers) |
| `docker run` as a whole | containerd, shim and `runc` processes, an overlay mount, a cgroup, a `veth` pair (not timed here) |

---

## 13. Common mistakes

1. **"A container is a small VM."** It is a process on the host kernel (`uname -r` was identical, the host saw the container as a `sleep` under a shim). A kernel exploit or an allowed dangerous system call reaches the host.
2. **Treating `chroot` as a sandbox.** A root process left a chroot with 10 lines of C (section 4.3). Use `pivot_root` in a mount namespace with dropped capabilities.
3. **Running as root with all capabilities.** The hand made container of section 5 had `000001fffeffffff`. Docker's default 14 is better; `--cap-drop ALL` plus the few needed, `no_new_privs` and a non root user is the target.
4. **`--privileged`.** Full capabilities and no seccomp (section 7.4): the container can mount disks and load modules. Use specific `--cap-add` and `--device` instead.
5. **Believing `OOMKilled` means the host ran out of memory.** It is the container's own limit. Exit code 137 without `OOMKilled=true` is a `SIGKILL` from somewhere else.
6. **A CPU limit set to what the average use is.** The average hides the bursts; the group is throttled in nearly every period (30 to 31 of 32 in section 3.2) while the CPU looks idle. Look at `nr_throttled` and `throttled_time`.
7. **Reading `free`, `nproc` or `/proc/cpuinfo` in a container** and trusting it: they show the host unless the runtime or `lxcfs` adapts them; size threads and heaps from the cgroup limit.
8. **PID 1 that ignores `SIGTERM` or never reaps.** `docker stop` waits for its timeout and then kills; zombies accumulate. Use `--init`, `tini` or an application that handles the signal.
9. **Writing big files into the container layer.** Copy up and a lost upper directory on removal: use volumes for data and logs.
10. **Assuming mount or PID isolation exists because only one flag was used.** The `chroot` demo shared the host's PID and mount namespaces (section 4.2); a `docker run --pid=host` or `--network=host` removes one namespace on purpose.
11. **Expecting the cgroup limit on a v1 host to behave as on v2.** The file names and semantics differ (section 3.1: `memory.high`, `cgroup.kill`, PSI and delegation exist only in v2).
12. **A `pids.max` that the setup counts against.** The container's start-up processes are in the group; `forks 30` got 11 with a limit of 16, not 16.

---

## 14. Questions out loud

1. **What is a container?** A process (tree) on the host kernel that has its own namespaces for what it sees, a cgroup for what it uses, a different root file system, and a reduced set of privileges (capabilities, seccomp, no_new_privs, maybe a user namespace). There is no kernel object called a container.
2. **List the namespaces and what each isolates.** Mount (mount table), UTS (hostname), IPC (System V and POSIX queues), PID (process IDs), network (interfaces, routes, sockets, firewall), user (uids and capabilities), cgroup (view of the cgroup tree), time (boot and monotonic clock offsets).
3. **Which system calls create or join them?** `clone`/`clone3` with `CLONE_NEW*` flags, `unshare`, and `setns` on a file descriptor from `/proc/PID/ns/`.
4. **Why does `unshare --pid` need `--fork`?** The caller stays in its old PID namespace; only its children enter the new one, and the first child is PID 1.
5. **What is special about PID 1 in a container?** Signals without a handler are not delivered to it, orphans are re-parented to it so it must reap, and when it exits the kernel kills the rest of the namespace.
6. **cgroup `shares` against a `quota`?** Shares divide a contended CPU in proportion and never limit an idle machine; a quota is a hard cap per period and throttles even if CPUs are idle.
7. **Why can a pod at low average CPU have high latency?** CFS bandwidth: the quota is used up early in each 100 ms period, the group is throttled for the rest (`nr_throttled`, `throttled_time`), and the tail latency shows it.
8. **What does `OOMKilled` with exit code 137 mean?** The cgroup memory limit was hit and reclaim could not satisfy it, so the cgroup OOM killer sent `SIGKILL` (128 + 9); the host may have plenty of free memory.
9. **`chroot` against `pivot_root`?** `chroot` changes one process's `/` and can be escaped by a process with `CAP_SYS_CHROOT`; `pivot_root` swaps the root of the whole mount namespace and the old root is unmounted, so no path leads back.
10. **How do images and layers work?** Layers are directories of changes shared between containers; overlayfs stacks read only `lowerdir`s with a writable `upperdir`; the first write to a lower file copies it up and a delete leaves a whiteout.
11. **What does a user namespace give you, and what is rootless?** A uid mapping so uid 0 inside is an unprivileged uid outside; capabilities apply only to resources the namespace owns. A rootless container runs the whole runtime that way.
12. **When do you use a VM rather than a container?** For mutually distrusting tenants or a different kernel: the boundary is the hypervisor, not the whole system call interface of a shared kernel; microVMs (Firecracker, Kata) give VM isolation with near container start times.

---

## 15. Problems

**Problem A. Read a container from the host.** A host process with PID 2782 shows `memory:/docker/29cb1e...`, `cpu.cfs_quota_us` 50000 and `cpu.cfs_period_us` 100000, `memory.limit_in_bytes` 67108864 and `pids.max` 32, and namespace inodes that differ from the shell's for `mnt`, `net`, `pid`, `ipc` and `uts`, but are the same for `user` and `cgroup` (section 8.1). State the memory limit in MiB, the CPU limit in CPUs, what the process cannot see and what it still shares with the host, and the one `docker run` command that produces these.

**Problem B. Kubernetes CPU limits.** A container has `limits.cpu: 750m` and 6 busy threads. Give the `cfs_quota_us` for a period of 100,000 and the longest time a thread can be stalled in one period when all threads start at the start of the period. Section 3.2 measured a 20 percent quota on a 3 s spin: what CPU time and how many throttled periods do you expect, and how does your answer compare with the measurement?

**Problem C. Two kinds of failure.** A container `limit 32m` runs `sh -c 'x=$(head -c 100000000 /dev/zero | tr "\0" a)'` and exits with 137. A second container with `--pids-limit 4` prints `/bin/sh: can't fork: Resource temporarily unavailable`. Say which limit each hit, which field or file confirms it, and which one a larger memory limit would not fix.

**Problem D. Capabilities.** Decode the masks `000001fffeffffff`, `00000000a80425fb` and `0`: how many capabilities does each hold? Which of the fourteen in the Docker default would you drop for a web server that listens on port 8080 and never changes file owners, and what does `no_new_privs` protect against that dropping capabilities does not?

**Problem E. Build and break.** (1) Put these steps in the correct order for a container with an isolated root: `pivot_root . old`, `unshare --mount --pid --fork`, `umount -l /old`, `mount --bind new new`, `cd new`, create the cgroup and join it. (2) The 10 line C program of section 4.3 runs inside (a) a `chroot` and (b) the result of that sequence: which one does it escape and why? (3) Which one extra thing does the container still share with the host that `uname -r` proves?

---

## 16. Answers

**Problem A.**

- Memory: 67,108,864 / 1,048,576 = **64 MiB**. CPU: 50,000 / 100,000 = **0.5 CPU**. The pids limit is 32.
- It cannot see the host's mounts, network interfaces, processes, IPC objects or hostname (new `mnt`, `net`, `pid`, `ipc` and `uts`). It shares the **user namespace** (its uid 0 is the host's uid 0, so a break out is root), the **cgroup namespace**, and the **kernel** (`uname -r` equal).
- Command: `docker run --memory 64m --cpus 0.5 --pids-limit 32 IMAGE ...` (the transcript's container was `lx:1` with `/bin/sleep 300`).

**Problem B.**

```text
--- B: quota arithmetic for a limit of 500m and 2 vCPUs
limit  250m -> cpu.cfs_quota_us= 25000 period=100000 -> 0.25 CPUs worth per period
limit  500m -> cpu.cfs_quota_us= 50000 period=100000 -> 0.50 CPUs worth per period
limit 1000m -> cpu.cfs_quota_us=100000 period=100000 -> 1.00 CPUs worth per period
limit 1500m -> cpu.cfs_quota_us=150000 period=100000 -> 1.50 CPUs worth per period
```

- `limits.cpu: 750m` gives quota **75,000** per 100,000 µs period (0.75 CPU). Six threads that start together burn 75 ms of quota in 75 / 6 = **12.5 ms** of wall time, so they are stalled for the remaining **87.5 ms** of the period.
- For the 3 s spin at 20 percent: 0.2 x 3 = **0.6 s** of CPU, and a throttle in nearly every one of the **30 to 32 periods** in 3 s. The measurement gave about **0.6 s** and **30 to 31 throttled of 31 to 32 periods**, a close match. The CPU time matches the formula because one thread was always runnable and used each period's full allowance; the number of periods is 3 s / 0.1 s.

**Problem C.**

- The first hit the **memory limit**: `docker inspect` printed `OOMKilled=true`, `ExitCode=137` (section 8.1), and in a hand made cgroup `memory.oom_control` shows `oom_kill 1`. A larger limit (or a smaller allocation) fixes it.
- The second hit the **pids limit**: `fork` returned `EAGAIN` (`Resource temporarily unavailable`), confirmed by `pids.events` `max` and `pids.current` against `pids.max`. A larger memory limit **does not** help; only `--pids-limit` (or fewer processes) does. The shell is not killed by the kernel, it just cannot start another process.
- Section 5 also shows that the shell and its helpers count: `forks 30` got **11** of a limit of 16.

**Problem D.**

```text
--- A: decode the capability masks
root            000001fffeffffff -> 40 capabilities
docker default  00000000a80425fb -> 14 capabilities
drop ALL        0000000000000000 -> 0 capabilities
$ capsh --decode=00000000a80425fb | tr "," "\n" | sed "s/^0x[0-9a-f]*=//" | tr "\n" " "; echo
cap_chown cap_dac_override cap_fowner cap_fsetid cap_kill cap_setgid cap_setuid cap_setpcap cap_net_bind_service cap_net_raw cap_sys_chroot cap_mknod cap_audit_write cap_setfcap
```

- `000001fffeffffff`: **40**; `00000000a80425fb`: **14**; `0`: **0**.
- For a web server on port 8080 (above 1024, so no `net_bind_service`) that never changes owners, drop at least `chown`, `fowner`, `fsetid`, `net_bind_service`, `net_raw`, `mknod`, `setfcap`, `audit_write`, `sys_chroot` and `dac_override`, and in practice `--cap-drop ALL` with nothing added. Removing one bit removes exactly that privilege (section 7.1).
- `no_new_privs` stops a **later `execve`** from raising privilege through a setuid binary or file capability: measured, the setuid `euid` program printed `euid=0` for uid 1001 and `euid=1001` under `--no-new-privs`. Dropping capabilities does not remove a setuid binary that is already in the image.

**Problem E.**

1. Order: create the cgroup and join it; `unshare --mount --pid --fork` (a private mount namespace first, so the next mounts do not leak); `mount --bind new new` (the new root must be a mount point); `cd new`; `pivot_root . old`; `umount -l /old`.
2. (a) The `chroot` is **escaped**: the program keeps its working directory outside the new root, walks up with `chdir("..")` and `chroot(".")`, and listed the host's `/var/tmp` (`lx09_root`). (b) After `pivot_root` and `umount -l /old` it is **not**: there is no path to the old root, and `ls /var/tmp` reported `No such file or directory`.
3. It still shares the **kernel**: `uname -r` printed the host's `6.18.44-fc-v470` inside a container. The process also shares the user namespace by default, so uid 0 is the host's uid 0.

---

## 17. Cheat sheet

```text
NAMESPACES  ls -l /proc/PID/ns   lsns -p PID   nsenter -t PID -m -u -i -n -p CMD   ip netns exec N CMD
  unshare --mount --uts --ipc --net --pid --fork --mount-proc --user --map-root-user --cgroup --time
  mnt uts ipc net pid user cgroup time        PID 1: no default signal action, reaps, exit kills all
  user ns: uid_map "INSIDE OUTSIDE COUNT"; caps apply only to resources the ns owns

CGROUP v1 (this VM)                               CGROUP v2 (from memory)
  cpu.cfs_quota_us / cpu.cfs_period_us            cpu.max "QUOTA PERIOD"
  cpu.shares (1024)                               cpu.weight (100)
  memory.limit_in_bytes                           memory.max, memory.high, memory.low/min
  memory.oom_control (oom_kill)  failcnt          memory.events  memory.oom.group
  pids.max pids.current pids.events               same
  cpuset.cpus  freezer.state                      cpuset.cpus  cgroup.freeze  cgroup.kill
  cpu.stat: nr_periods nr_throttled throttled_time
  v2: no internal processes, subtree_control, delegation, PSI

ROOT FS     chroot = change "/" only (escapable)    pivot_root . old; umount -l /old (new root must be a mount point)
  overlay: lowerdir (ro, shared) + upperdir (rw) + workdir; copy up whole file; delete = whiteout

DO IT BY HAND  cgroup mkdirs -> ip netns + veth -> unshare --mount --pid --uts --ipc --fork
               -> bind new new; cd new; pivot_root . old; umount -l /old; mount proc; exec

PRIVILEGE   /proc/PID/status: CapEff CapBnd NoNewPrivs Seccomp   capsh --decode=MASK
  root 000001fffeffffff (40)   docker default 00000000a80425fb (14)   --cap-drop ALL 0
  --privileged = full caps, no seccomp, devices      --security-opt no-new-privileges  seccomp=PROFILE

DOCKER      --memory --cpus --pids-limit -> memory.limit_in_bytes, cfs_quota_us, pids.max
  OOMKilled=true ExitCode=137 (128+9) ; 137 without OOMKilled = other SIGKILL ; 143 = SIGTERM
  dockerd -> containerd -> containerd-shim-runc-v2 -> runc (config.json) -> container process

K8S         requests -> cpu.shares / cpu.weight, scheduling ; limits.cpu 500m -> quota 50000/100000
  limits.memory -> memory.max ; CPU over limit = throttled, memory over limit = OOMKilled
  QoS: Guaranteed / Burstable / BestEffort ; watch nr_throttled and working set

NOT RUN HERE: cgroup v2 controllers, systemd-run, Kubernetes, rootless runtimes, docker0 and -p, AppArmor, SELinux
```

---

## 18. Sources

- `namespaces(7)`, `unshare(2)`, `clone(2)`, `setns(2)`, `unshare(1)`, `nsenter(1)`, `lsns(8)`: namespace types, flags and tools. <https://man7.org/linux/man-pages/man7/namespaces.7.html>
- `user_namespaces(7)`, `pid_namespaces(7)`, `mount_namespaces(7)`, `time_namespaces(7)`, `cgroup_namespaces(7)`: the per type rules (mapping files, PID 1, propagation, offsets). <https://man7.org/linux/man-pages/man7/user_namespaces.7.html>
- `cgroups(7)`: v1 and v2 interfaces. Linux kernel documentation, **Control Group v1** and **Control Group v2**: `memory.max`, `cpu.max`, delegation, the no internal process rule. <https://docs.kernel.org/admin-guide/cgroup-v2.html> and <https://docs.kernel.org/admin-guide/cgroup-v1/index.html>
- Linux kernel documentation, **CFS bandwidth control**: `cpu.cfs_quota_us`, `cpu.cfs_period_us`, `nr_throttled`. <https://docs.kernel.org/scheduler/sched-bwc.html>
- `pivot_root(2)`, `chroot(2)`: the semantics and the requirement that the new root be a mount point. <https://man7.org/linux/man-pages/man2/pivot_root.2.html>
- Linux kernel documentation, **Overlay Filesystem**: layers, copy up, whiteouts. <https://docs.kernel.org/filesystems/overlayfs.html>
- `capabilities(7)`, `seccomp(2)`, and the kernel's `no_new_privs` documentation. <https://man7.org/linux/man-pages/man7/capabilities.7.html> and <https://docs.kernel.org/userspace-api/no_new_privs.html>
- Open Container Initiative: **Runtime Specification** (`config.json`) and **Image Specification**; `runc` project. <https://github.com/opencontainers/runtime-spec> and <https://github.com/opencontainers/runc>
- Docker documentation: resource constraints (`--memory`, `--cpus`, `--pids-limit`), security (seccomp default profile, capabilities, `--privileged`). <https://docs.docker.com/engine/containers/resource_constraints/>
- Kubernetes documentation: **Resource Management for Pods and Containers**, **Pod Quality of Service Classes**, and **Linux Kernel Version Requirements** for cgroup v2. <https://kubernetes.io/docs/concepts/configuration/manage-resources-containers/>
- Agache et al., *Firecracker: Lightweight Virtualization for Serverless Applications* (NSDI 2020), for microVMs against containers.
- This plan: [LX 03](03_processes_signals_and_proc.md) for `/proc` and namespaces, [LX 06](06_memory_swap_and_the_oom_killer.md) for memory and the OOM killer, [LX 07](07_files_permissions_and_storage.md) for mounts, [LX 08](08_linux_networking_toolbox.md) for network namespaces, `veth` and NAT.
- Measurements: the scripts `x1` to `x6` and `ans_prob` of this session, Linux 6.18.44, one VM, as stated in the honest limits.
