# LX 04: Boot, systemd and logs

*Part of the [Linux plan](00_plan.md) · Time: about 5 hours · You need: [LX 01](01_shell_and_scripting.md) (shell, exit statuses), [LX 03](03_processes_signals_and_proc.md) (processes, signals, `/proc`, the deleted-file trap) and [OS 21](../05_operating_systems/21_os_interview_question_bank.md) for the vocabulary. This tutorial is what happens **before** your process exists (boot), what **supervises** it while it runs (`systemd`), and what it **leaves behind** (logs).*

"Walk me through what happens from power on to a login prompt" and "a service keeps dying, what do you do?" are both standard questions in infrastructure, networking-appliance and embedded loops. The first is a list of stages with a failure mode at each stage. The second is a list of `systemd` facts: the unit says how to start it, what it needs, how it is restarted, what it may use and what it may touch, and the journal says what it said when it died. This tutorial makes each of those facts something you produce and read.

The sequence is: the boot chain from firmware to `systemd` (section 1), the kernel log and the kernel command line (2), modules and `sysctl` (3), the `systemd` model, unit types and `systemd-analyze` (4), dependencies against ordering (5), service types, restart policy, start limits and the watchdog (6), socket activation and timers (7), resource control (8), sandboxing (9), the journal, `dmesg` and `logrotate` (10), recovery from a broken `fstab` and a lost password (11), a triage table (12), and then the costs, mistakes, questions and five problems with run answers.

**Six honest limits, stated first.**

1. This was measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs, about 8 GiB), as **root**, with `systemd` 255.4 from Ubuntu 24.04, bash 5.2.21 and logrotate 3.21.
2. **This VM did not boot with `systemd`.** Its PID 1 is a small program (`/process_api`) started straight from the kernel with `rdinit=`; there is no firmware menu, no bootloader, no initramfs image, no `/boot`, no EFI and the kernel has no module support. So the **boot chain of section 1 and recovery recipes of section 11 are from memory**, plus the `systemd` manual page `bootup(7)`, which I fetched. What I could measure here about boot is how *this* machine was started (section 1) and what the kernel log and command line show.
3. **The `systemd` experiments ran a real `systemd` 255 as PID 1 of a new PID and mount namespace**, stopped at `basic.target` so that it does not start the network or Docker. It is the real manager, with real units, cgroups, journal and timers, but it is **not a boot**: no firmware, no kernel start-up and no initramfs time, so `systemd-analyze` times are userspace only, and about 0.4 s because there is almost no hardware to bring up. Section 4 says exactly how it was done and what went wrong the first time.
4. **The control groups here are version 1 (hybrid)**; current distributions use version 2. Where that changes a result (the `CPUUsageNSec` counter is not available, and a memory kill is reported as a plain `SIGKILL`), the text says so.
5. **Not installed** in the VM: `cron`, `rsyslog`, `udevadm`, `ip`, `ss`, `dracut`, GRUB tools. Where one of these matters, the answer is **from memory** and marked.
6. Timings are **single runs** and vary by 10 to 30 percent.

By the end you can:

- say what each boot stage does and what breaks at it, and state **this VM's** boot honestly (measured: `rdinit=/process_api`, no `/sys/firmware/efi`, no `/lib/modules`, PID 1 is `/process_api`, the kernel log has 642 lines);
- read the kernel log (measured: `dmesg -x` gives facility and level, `/dev/kmsg` accepts `<4>text` and the line comes back as `warn`, and the first line `dmesg` prints is the kernel version);
- explain why `modprobe` fails on a kernel without modules and where `sysctl` settings live (measured: `sysctl -w vm.swappiness=10` took effect at once in `/proc/sys`, and of two files in `/etc/sysctl.d` the **later** one, `90-`, won: 30 against 20);
- read `systemd-analyze` (measured: userspace **406 ms**, `basic.target` reached at **404 ms**, and the critical chain from `systemd-journald.socket` at 129 ms to `basic.target`);
- tell **ordering** from **requirement** (measured: `Requires=` alone started both units at once, **A active 1,010 ms before B finished**; adding `After=` made A active **21 ms after** B);
- predict `Restart=` (measured: a service that exits 1 with `Restart=on-failure` and `StartLimitBurst=3` ran 3 times then stayed `failed`, `NRestarts=3`; a clean exit gave **0** restarts under `on-failure` and **3 in 2 s** under `always`);
- explain the watchdog (measured: with `WatchdogSec=1` and silence after two pings the service was killed after **1.70 s** with `Result=watchdog` and status 6, `SIGABRT`);
- explain socket activation and timers (measured: **0** service instances before the connection, **1** during, **0** after; a 1 s timer ran 4 times with gaps of 1.11, 1.11 and 1.04 s);
- cap a service with cgroups (measured: `MemoryMax=40M` killed a 200 MB program with status 9 and the kernel logged `anon-rss:40704kB`; `CPUQuota=20%` gave **20 percent** against **98** uncapped, and `systemctl set-property` raised a running service to **60 percent**; `TasksMax=5` stopped the sixth `fork`);
- sandbox a service and read the evidence (measured: the same program as root saw a writable `/etc`, `/root`, every interface and a full capability set; with six options it got a read-only `/etc`, no `/root`, only `lo`, `CapEff` all zero, and `systemd-analyze security` fell from **9.6 UNSAFE** to **5.6 MEDIUM**);
- query the journal by field (measured: `-t`, `-p`, `-u`, `-F`, `--since`, `-o json`), know that it rate-limits **per service** (measured: 150 of 600 lines kept) and that `logrotate` by rename leaves the writer on the **old** file (measured: after rotation the writer still had `app.log.1`, while with `copytruncate` it kept `copy.log`);
- explain a broken `fstab` (measured: a bad line without `nofail` gave `is-system-running` = **maintenance** and `emergency.target` active; with `nofail` the system was `running`).

**Where the numbers come from.** Six experiment scripts and a problem-answer script were run in this session on Linux 6.18.44 (2 vCPUs, about 8 GiB). Transcripts print `$ command` before each command's output; `S` in a transcript means "run inside the namespace of the test `systemd`". Numbers that are not from these runs are marked "from memory" or come from a source listed at the end.

**Data and timing.** Nothing here needs a data set. The test programs are small shell and Python files and `systemd` unit files, printed where they are used. Each experiment names its timing (for example "after 5 s"); treat all times as ranges.

---

## 1. The boot chain, and how this machine was started

This section's chain is **from memory** except where it cites `bootup(7)`, which I fetched. What I can show is how *this* VM started.

### 1.1 The stages

| Stage | What it does | What breaks, and what you see |
| --- | --- | --- |
| **Firmware** (UEFI, or BIOS on old machines) | Initialises the CPU, memory and basic devices (POST); reads the boot entries kept in NVRAM (UEFI) or the first sector of the disk (BIOS); starts a boot loader from the **EFI system partition** | no entry, wrong disk order, a dead disk: "no bootable device", a firmware menu; nothing from Linux yet |
| **Secure boot** (if enabled) | The firmware checks the signature of the next stage; a small signed program (`shim`) checks the boot loader, which checks the kernel | an unsigned kernel or an out-of-tree module is refused; see [CO 30](../04_computer_organization/00_plan.md) |
| **Boot loader** (GRUB, `systemd-boot`) | Shows a menu, loads the **kernel image** and the **initramfs** into memory, builds the **kernel command line**, jumps to the kernel | a wrong path or a bad `grub.cfg`: a `grub>` prompt; editing the command line here is how recovery starts (section 11) |
| **Kernel** | Decompresses itself, sets up memory, the scheduler and interrupts, probes built-in drivers, unpacks the initramfs into a RAM file system, and runs `/init` from it | a panic before the first user program: the last lines on the console are the cause (section 2) |
| **Initramfs** | A small file system with just enough to find and mount the **real root**: drivers for the disk controller, RAID, LVM, LUKS (passphrase), NFS; it finds the root from `root=UUID=...`, mounts it at `/sysroot` and **switches** to it | "cannot find root device", a `dracut` or `initramfs` shell: a driver is missing from the image, a UUID changed, a passphrase was wrong |
| **`systemd` as PID 1** (on the real root) | Reads the unit files, builds a transaction for the **default target**, starts units in parallel as dependencies allow | a unit failing, a wait for a device that never appears (section 11), `emergency` mode |
| **Login** | `getty` on the console, `sshd`, a display manager | usually a service or a network ordering problem (section 5) |

The manual page `bootup(7)` (fetched) gives the same chain and, for the part inside `systemd`: in the initramfs `systemd` runs toward `initrd.target`, reaches `initrd-root-device.target` when the root device appears, mounts it at `/sysroot`, and `initrd-switch-root.service` runs last and switches root. On the real root, `sysinit.target` comes after local file system setup, swap and low-level services such as `udev` and `sysctl`; `basic.target` follows it, with sockets, timers and paths; `multi-user.target` follows `basic.target`; `graphical.target` comes after `multi-user.target` with the display manager started in between. That order, `sysinit` then `basic` then `multi-user` then `graphical`, is the one to say aloud.

**Why an initramfs exists at all.** The kernel needs the driver for the disk before it can read the root file system, and the driver may be a module that lives **on** that file system. The initramfs breaks the circle: it is loaded by the boot loader into memory, so the kernel needs no disk to run its first program. A kernel with the disk driver built in (as in a VM like this one, or many embedded boards) does not need one.

### 1.2 How this VM started

The VM has none of the middle stages. The kernel was started directly by the hypervisor with a command line that names its first program.

```text
--- how this VM was started: no firmware menu, no bootloader, no initramfs
$ tr " " "\n" < /proc/cmdline | grep -E "^(console|quiet|rdinit|panic|nomodule|psi|ipv6)"
console=ttyS0
quiet
panic=1
nomodule
psi=1
ipv6.disable=1
rdinit=/process_api
$ ls /sys/firmware/efi 2>&1 | head -1; ls /boot /lib/modules 2>&1 | head -3
ls: cannot access '/sys/firmware/efi': No such file or directory
ls: cannot access '/lib/modules': No such file or directory
/boot:
$ cat /proc/sys/kernel/osrelease; ps -o pid,args -p 1 | cut -c1-60
6.18.44-fc-v470
    PID COMMAND
      1 /process_api --firecracker-init --addr 0.0.0.0:2024
```

Reading it: `console=ttyS0` sends kernel messages to a serial port; `quiet` lowers the console log level; **`rdinit=/process_api`** tells the kernel which program to run from the initramfs instead of `/init` (the plain `init=` option names the program to run from the **real** root after the initramfs, and `rdinit=` the one in the initramfs); `nomodule` is on the line, and this kernel has **no module support at all** (section 3); I did not find which of the two is the cause, so I report both facts; `psi=1` enables the pressure stall information of [LX 03](03_processes_signals_and_proc.md); `ipv6.disable=1` turns IPv6 off. `/sys/firmware/efi` does not exist (firmware is not UEFI), `/boot` is empty, and PID 1 is `/process_api`, not `systemd`: there is no `systemd` on this VM's boot path at all.

This is the **shape** of a minimal boot, firmware to kernel to one program, and it is what many containers-as-VMs and embedded devices look like; a server adds the stages of the table.

---

## 2. The kernel log and the kernel command line

The kernel keeps its messages in a **ring buffer** in memory: when it is full, the oldest messages are overwritten. `dmesg` reads it. Each message has a **timestamp** (seconds since the kernel started), a **facility** (`kern`, `user`, ...) and a **level** from 0 (emergency) to 7 (debug).

```text
--- the kernel ring buffer
$ dmesg | wc -l
642
$ dmesg | head -3 | cut -c1-100
[    0.000000] Linux version 6.18.44-fc-v470 (builder@sandboxing) (gcc (GCC) 15.3.0, GNU ld (GNU Bin
[    0.000000] Command line: console=ttyS0 quiet reboot=k panic=1 nomodule random.trust_cpu=1 swiotl
[    0.000000] BIOS-provided physical RAM map:
$ dmesg -x | sed -n "3,5p" | cut -c1-90
kern  :info  : [    0.000000] BIOS-provided physical RAM map:
kern  :info  : [    0.000000] BIOS-e820: [mem 0x0000000000000000-0x000000000009fbff] usabl
kern  :info  : [    0.000000] BIOS-e820: [mem 0x000000000009fc00-0x00000000000fffff] reser
$ dmesg --level=err,warn | head -3 | cut -c1-110
[    0.453412] software IO TLB: No low mem
[    1.252730] EXT4-fs (vda): warning: mounting unchecked fs, running e2fsck is recommended
[  255.838008] lx04: a test message at warning level
$ cat /proc/sys/kernel/printk
4       4       1       7
$ echo "<4>kmsgtest: a message at warning level" > /dev/kmsg; dmesg -x | tail -1; dmesg -T | tail -1 | cut -c1-70
user  :warn  : [  553.888527] kmsgtest: a message at warning level
[Sun Nov 15 20:12:56 2026] kmsgtest: a message at warning level
```

Reading it, line by line:

- The ring holds 642 messages. The first is the **kernel version** line and the second is `Command line:`, which is the kernel command line as received (section 1.2 printed the same from `/proc/cmdline`): when a machine "behaves differently after a reboot", compare the two.
- `dmesg -x` prints the facility and level (`kern  :info  :`); `--level=err,warn` filters. The third line of the filtered output, `lx04: a test message at warning level`, is one I wrote earlier from user space.
- **The last command writes into the kernel log from user space**: `echo "<4>kmsgtest: ..." > /dev/kmsg`; the leading `<4>` is the level (4 is warning), and `dmesg -x` printed it as `user  :warn  :`. This is how a script leaves a mark in the kernel log (for example before a test, so you can find the start in a long log).
- `/proc/sys/kernel/printk` prints four numbers, `4 4 1 7`: the **console log level** (messages **below** 4, that is levels 0 to 3, go to the console), the default level for a message with none, the minimum console level, and the default console level at boot. The labels are from memory; the values are measured.
- `dmesg -T` converts the timestamps to wall-clock time, shown as `[Sun Nov 15 20:12:56 2026]`. From memory, this conversion is approximate on a machine that has been suspended, because it works from the boot time.

**Where kernel messages go on a normal system.** To the ring buffer, to the console (by level), and, on a `systemd` system, `journald` reads the ring and stores it; `journalctl -k` shows only kernel messages and `journalctl -k -b -1` those of the **previous boot** (section 10). After a crash and reboot, the ring of the dead boot is gone; the persistent journal, `pstore` or `kdump` are what keep it (from memory; [LX 12](00_plan.md) and the [debugging track](../16_debugging/00_plan.md) cover the last two).

---

## 3. Modules and `sysctl`

### 3.1 Kernel modules

A **module** is kernel code loaded after boot (`.ko` files under `/lib/modules/$(uname -r)/`). The tools, **from memory** because this kernel has none: `lsmod` lists them (it reads `/proc/modules`); `modinfo NAME` prints a module's parameters and aliases; `modprobe NAME` loads a module **and its dependencies** using the table `modules.dep` that `depmod` builds; `modprobe -r NAME` removes it; `insmod` loads one file with no dependency handling. Modules are loaded **automatically** when hardware appears: the kernel emits an event with the device's **alias** (for a PCI card, its vendor and device IDs), `udev` receives it and runs `modprobe` on the matching module. Options go in `/etc/modprobe.d/*.conf` (`options NAME key=value`, `blacklist NAME`), and a module to load at every boot in `/etc/modules-load.d/*.conf`.

This VM's kernel has no module support (and `nomodule` is on its command line), and the tools say so:

```text
--- modules: this kernel was started with nomodule
$ lsmod | wc -l; ls /lib/modules 2>&1 | head -1; cat /proc/sys/kernel/modules_disabled
libkmod: ERROR ../libkmod/libkmod-module.c:1762 kmod_module_new_from_loaded: could not open /proc/modules: No such file or directory
Error: could not get list of modules: No such file or directory
0
ls: cannot access '/lib/modules': No such file or directory
cat: /proc/sys/kernel/modules_disabled: No such file or directory
$ modprobe dummy 2>&1 | head -2
modprobe: FATAL: Module dummy not found in directory /lib/modules/6.18.44-fc-v470
```

`lsmod` fails because `/proc/modules` does not exist; `/lib/modules` does not exist; `modprobe` says `Module dummy not found in directory /lib/modules/6.18.44-fc-v470`. Note the path in the message: **`modprobe` looks under the running kernel's exact version string**, so after a kernel update without a reboot, loading a module for the **old** running kernel can fail because its directory was removed (from memory, a classic surprise).

### 3.2 `sysctl`: runtime kernel parameters

Every file under `/proc/sys` is a kernel setting (`vm.swappiness` is `/proc/sys/vm/swappiness`). `sysctl -w` writes it **now**; nothing survives a reboot unless it is in a file.

```text
--- sysctl: runtime, then persistent
$ sysctl vm.swappiness; ls /proc/sys/vm/swappiness; cat /proc/sys/vm/swappiness
vm.swappiness = 60
/proc/sys/vm/swappiness
60
$ sysctl -w vm.swappiness=10; cat /proc/sys/vm/swappiness
vm.swappiness = 10
10
$ sysctl --system 2>&1 | grep -A1 99-lx04
* Applying /etc/sysctl.d/99-lx04.conf ...
* Applying /usr/lib/sysctl.d/99-protect-links.conf ...
$ sysctl vm.swappiness
vm.swappiness = 25
```

The first block shows the value 60, the file path and `cat` agreeing; `sysctl -w vm.swappiness=10` changed it at once with no restart. For persistence, I wrote `vm.swappiness = 25` to `/etc/sysctl.d/99-lx04.conf` and ran `sysctl --system`, which prints each file it applies (`Applying /etc/sysctl.d/99-lx04.conf`, then `/usr/lib/sysctl.d/99-protect-links.conf`) and the value became 25. At boot the same files are applied by `systemd-sysctl.service` (from memory). **Order matters:** problem D measures two files, `50-` and `90-`, setting different values; the result (30) is that of the **later** file name. Files with the same name in `/etc/sysctl.d` replace those in `/usr/lib/sysctl.d` (from memory). Practical rules: put your change in a file with a high number so it comes last, never edit the package's file, and **check with `sysctl NAME`** afterwards, because a later file can undo yours.

---

## 4. `systemd`: the model, the unit types and `systemd-analyze`

### 4.1 What `systemd` is

As PID 1 it does what [LX 03](03_processes_signals_and_proc.md) section 10 described (it adopts and reaps orphans), and much more: it reads **unit files**, builds a **transaction** (a set of start and stop jobs ordered by dependencies), runs them **in parallel** where it can, and keeps every service in its own **cgroup** so that it can track and kill all of a service's processes. A **unit** is any object it manages, named `name.type`:

| Type | What it is |
| --- | --- |
| `.service` | a process or a set of processes it starts and supervises |
| `.socket` | a listening socket it owns, which starts a service on the first connection (section 7) |
| `.timer` | a schedule that starts another unit (section 7) |
| `.mount`, `.automount` | a mount point, usually generated from `/etc/fstab` (section 11) |
| `.path` | a watch on a file or directory that starts a unit when it changes |
| `.target` | a **named group** of units, a synchronisation point (`multi-user.target`) |
| `.slice` | a node in the cgroup tree for resource control (section 8) |
| `.scope` | a set of processes started by something else and only **tracked** (a login session) |
| `.device`, `.swap` | a kernel device, a swap area |

**Where unit files live** (from the fetched `systemd.unit(5)`): `/etc/systemd/system` is searched first, then `/run/systemd/system`, then `/usr/lib/systemd/system`, and a unit in an earlier directory **overrides** the same name in a later one. Administrator-written units go in `/etc/systemd/system`; packages install into `/usr/lib/systemd/system`. A **drop-in** directory `NAME.service.d/*.conf` adds to or overrides single settings of a unit without copying the file; its files are merged in alphanumeric order, and a drop-in in `/etc` beats one in `/run`, which beats one in `/usr/lib`. `systemctl edit NAME` creates a drop-in for you, and **after any change to a unit file, run `systemctl daemon-reload`**, otherwise the manager keeps using the old text (from memory, and visible in every experiment below: I wrote the files and ran `daemon-reload` before starting).

### 4.2 How the test `systemd` was run

To test `systemd` on a machine whose PID 1 is not `systemd`, each experiment starts the real manager as the first process of a new PID namespace and mount namespace and talks to it with `nsenter`:

```bash
# sd.sh: boot a real systemd (PID 1 of a new PID and mount namespace) and talk to it
SD_PID=
sd_boot() {   # sd_boot [target]; set SD_FSTAB to a file to replace /etc/fstab inside
    setsid unshare --pid --fork --mount --uts --mount-proc bash -c '
        mount -t tmpfs tmpfs /run; mount -t tmpfs tmpfs /var/log; mkdir /var/log/journal
        mount -t tmpfs tmpfs /tmp; mount -t tmpfs tmpfs /etc/systemd/system
        if [ -n "$SD_FSTAB" ]; then mount --bind "$SD_FSTAB" /etc/fstab; fi
        export container=docker
        exec /lib/systemd/systemd --system --unit='"${1:-basic.target}"' --log-target=null' >/dev/null 2>&1 &
    local i
    for i in $(seq 1 100); do
        SD_PID=$(pgrep -f '^/lib/systemd/systemd --system' | head -1)
        [ -n "$SD_PID" ] && break
        sleep 0.1
    done
    [ -n "$SD_NOWAIT" ] && return 0
    for i in $(seq 1 100); do
        case $(S systemctl is-system-running 2>/dev/null) in running|degraded) return 0;; esac
        sleep 0.2
    done
    return 1
}
S() { nsenter -t "$SD_PID" -p -m -u -- "$@"; }
sd_stop() { [ -n "$SD_PID" ] && kill -KILL "$SD_PID" 2>/dev/null; sleep 0.3; }
```

It is ready in about half a second. The four `tmpfs` mounts are **not optional**, and I learned this by getting it wrong:

- `/run` on `tmpfs`: `systemd` keeps its runtime state there.
- `/tmp` on `tmpfs`: **without it, `systemd-tmpfiles` cleaned the host's `/tmp`** on the first run and deleted files of this session, including its working area. It runs at start and removes old entries.
- `/etc/systemd/system` on `tmpfs`: without it the units that the image had enabled were started.
- `--unit=basic.target`: **I first let it start the default target. That started Docker, which created a `docker0` bridge and firewall rules in this VM's network namespace** (the one shared with the rest of the VM). The cleanup of those rules was refused by the environment and I did not retry it; so this VM still has `docker0` (you can see it in the interface list of section 9) and Docker's `FORWARD` rules. It is a sandbox VM with no other use, but the lesson is the real one: **a `systemd` started in a namespace shares the host's network and block devices unless you isolate them, and starting the default target starts everything that is enabled.**

`S`, in every transcript, is `nsenter -t PID -p -m -u --`.

### 4.3 `systemd-analyze`: where the time went

```text
--- a real systemd as PID 1 of a new PID namespace (basic.target, no network, no docker)
systemd answered after .527 s
$ S sh -c "echo PID1=\$(cat /proc/1/comm); readlink /proc/1/exe"
PID1=systemd
/usr/lib/systemd/systemd
$ S systemctl is-system-running
running
$ S systemd-analyze
Startup finished in 406ms (userspace)
graphical.target was never reached.
$ S systemd-analyze blame --no-pager | head -5
85ms dev-hugepages.mount
84ms dev-mqueue.mount
77ms sys-kernel-debug.mount
73ms sys-kernel-tracing.mount
61ms modprobe@configfs.service
$ S systemd-analyze critical-chain basic.target --no-pager | head -12
The time when unit became active or started is printed after the "@" character.
The time the unit took to start is printed after the "+" character.
```

- `systemd` answered after 0.53 s. `is-system-running` printed `running`; other possible answers are `starting`, `degraded` (some unit failed) and `maintenance` (emergency or rescue mode, section 11).
- `systemd-analyze` printed **406 ms (userspace)**. On a real machine it prints three numbers, `kernel + initrd + userspace` (from memory); here only userspace exists. It also says `graphical.target was never reached`, true because I stopped at `basic.target`. `systemctl get-default` says the image's default is `graphical.target`.
- **`blame` against `critical-chain`.** `blame` lists units by how long each took to start (here 85 ms for `dev-hugepages.mount` at the top), but the units start **in parallel**, so the sum is far more than the elapsed time and a slow unit that nothing waits for costs nothing. `critical-chain` prints the **chain of units that actually delayed** the target: each line is `unit @ time it became active`, with `+ time it took`. Here `basic.target` was reached at 404 ms, preceded by `sysinit.target` at 394 ms, then `systemd-update-utmp.service`, `systemd-tmpfiles-setup.service` (23 ms), `local-fs.target`, and so on back to `systemd-journald.socket` at 129 ms. **To speed a boot, shorten the chain, not the longest `blame` entry.**
- `systemctl list-dependencies basic.target` prints what the target pulls in: `tmp.mount`, `paths.target`, `slices.target`, `sockets.target` and each socket below it. A `●` is active, a `○` inactive.

Other `systemd-analyze` commands used below: `verify FILE` (lint a unit file, section 6), `calendar EXPR` (explain a timer expression, section 7), `security UNIT` (the exposure score, section 9). `systemd-analyze plot > boot.svg` draws the whole boot as a timeline (from memory, not run).


---

## 5. Dependencies against ordering

The most common `systemd` mistake is to think that "A needs B" also means "A starts after B". It does not. The fetched `systemd.unit(5)` says it in one line: requirement dependencies (`Wants=`, `Requires=`, `Requisite=`, `BindsTo=`) **do not control start or stop order**, and ordering (`After=`, `Before=`) **does not pull units in**; the two are separate, and a unit in both `After=` and `Wants=` is the common pattern.

| Directive | Meaning (from the manual page) |
| --- | --- |
| `Wants=B` | start B with this unit; if B fails, this unit still starts. The recommended way to hook units together |
| `Requires=B` | start B too; if B fails **and** there is an `After=` on it, this unit does not start; stopping or restarting B also stops or restarts this unit |
| `Requisite=B` | like `Requires=`, but B must **already be active**, otherwise this unit fails at once; implies no ordering |
| `BindsTo=B` | like `Requires=`, and this unit also stops if B stops unexpectedly (a service tied to a device) |
| `PartOf=B` | only stop and restart of B propagate to this unit, one way |
| `Conflicts=B` | starting either stops the other; implies no ordering |
| `After=B` / `Before=B` | ordering only; shutdown order is the reverse of start-up order |
| `OnFailure=B` | start B when this unit enters the `failed` state (a notification or recovery unit) |

The experiment: B is a one-second job (`Type=oneshot`, `ExecStart=/bin/sleep 1`), A is a service that `Requires=` it, once with no ordering and once with `After=`.

```ini
[Unit]
Description=B: a one-second job others depend on

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sleep 1
```

```ini
[Unit]
Description=A requires B, no ordering
Requires=req-b.service

[Service]
ExecStart=/bin/sleep 100
```

```ini
[Unit]
Description=A requires B and starts after it
Requires=req-b.service
After=req-b.service

[Service]
ExecStart=/bin/sleep 100
```

Result (`ActiveEnterTimestampMonotonic` of A against B):

```text
--- Requires= alone starts both at once; After= makes the order
unordered: A became active -1010 ms relative to B (B is a 1 s job; negative means A came first)
ordered:   A became active 21 ms relative to B
```

With `Requires=` alone, **both units started at the same instant**: A became active about **1,010 ms before** B finished its second of work, so A ran while the thing it "needed" was not ready. With `After=` added, A became active **21 ms after** B. That is the whole rule, measured: use `Wants=` plus `After=` for almost everything, `Requires=` plus `After=` when A cannot work without B, and never `Requires=` alone when you mean "after".

**The network is the classic case** (from memory). `After=network.target` only means the network *management* has been started, which says nothing about an address being configured. A service that needs a working network at start should say `Wants=network-online.target` and `After=network-online.target`; a better service retries instead of failing when the network is late.

**Targets are synchronisation points.** `WantedBy=multi-user.target` in a unit's `[Install]` section is what `systemctl enable` turns into a symlink, so that the target **wants** the unit at boot. `systemctl enable` and `systemctl start` are different: `enable` changes what happens at the **next boot**, `start` acts **now**; `enable --now` does both (from memory). To see why a unit was started or what it waits for, `systemctl list-dependencies UNIT` (what it pulls in) and `--reverse` (who pulls it in).

---

## 6. Services: type, restart policy, start limits and the watchdog

### 6.1 `Type=`: when does `systemd` consider the service started?

This matters because units that `After=` this one wait for "started". From the fetched `systemd.service(5)`:

| `Type=` | Started when |
| --- | --- |
| `simple` | right after the process is forked; a missing binary is **not** caught at that point |
| `exec` | after the binary was executed successfully (recommended for long-running services over `simple`) |
| `forking` | when the original process exits after it forked a daemon (discouraged; use `PIDFile=`) |
| `oneshot` | after the process **exits**; without `RemainAfterExit=yes` the unit is never shown as active (used for B above) |
| `notify` | like `exec`, but waits for the service to send `READY=1` (`sd_notify`) |
| `dbus` | when it has taken its D-Bus name |
| `idle` | like `simple`, with the start delayed until other jobs are dispatched (at most 5 s) |

A modern service should **not daemonise** (no double fork); `systemd` wants the foreground process, which is how it tracks it ([LX 03](03_processes_signals_and_proc.md) section 10.1).

### 6.2 `Restart=`: what restarts, what does not

The fetched table, as a rule: `no` never; `on-failure` after a non-zero exit, a fatal signal, a timeout or a watchdog failure; `on-success` only after a clean exit; `always` after any of them including a clean exit; `on-abnormal` after a signal, timeout or watchdog but not a plain non-zero exit; `on-abort` only after an uncaught signal; `on-watchdog` only after a watchdog timeout. `systemctl stop` never triggers a restart. `RestartSec=` is the delay (default 100 ms).

Two units that differ in one word, both running `sh -c "sleep 0.2; exit 0"`:

```ini
[Service]
ExecStart=/bin/sh -c "sleep 0.2; exit 0"
Restart=on-failure
RestartSec=0.3
```

```ini
[Service]
ExecStart=/bin/sh -c "sleep 0.2; exit 0"
Restart=always
RestartSec=0.3
StartLimitIntervalSec=0
```

A third fails every time, and has a **start limit**:

```ini
[Unit]
Description=Fails after 0.2 s, restarts on failure
StartLimitIntervalSec=10
StartLimitBurst=3

[Service]
ExecStart=/bin/sh -c "sleep 0.2; exit 1"
Restart=on-failure
RestartSec=0.3
```

```text
--- Restart= policy and the start limit
$ S systemctl show flaky.service -p ActiveState,SubState,Result,NRestarts,ExecMainStatus
Result=exit-code
NRestarts=3
ExecMainStatus=1
ActiveState=failed
SubState=failed
$ S systemctl is-failed flaky.service
failed
$ S systemctl show flaky.service -p StartLimitBurst,StartLimitIntervalUSec,RestartUSec
RestartUSec=300ms
StartLimitIntervalUSec=10s
StartLimitBurst=3
exits-clean-onfailure: NRestarts=0 state=inactive/dead
exits-clean-always: NRestarts=3 state=active/running
```

- `flaky`: exit status 1 each time, `Restart=on-failure`; it ended `failed`, `Result=exit-code`, `NRestarts=3`. The limit `StartLimitBurst=3` in `StartLimitIntervalSec=10` means **at most 3 starts in 10 s**: when the limit is reached the unit goes to `failed` and **stops restarting**. Problem E counts the starts directly (a counter file) and finds **3 starts**, the first plus two restarts, with `NRestarts=3`; my reading is that `NRestarts` also counts the third restart that the limit then refused (this is an inference, the manual page was not fetched for the counter).
- A **clean exit** is not a failure: `on-failure` gave `NRestarts=0` and the unit was `inactive`, while `always` gave `NRestarts=3` in 2 s (and I set `StartLimitIntervalSec=0` to remove the limit, otherwise it would stop too). **`Restart=always` on a program that exits 0 on purpose is a restart loop.**
- After a start-limit failure, `systemctl reset-failed UNIT` clears the state so that it can be started again (I used it between the two tests). Without it, `systemctl start` is refused with "start request repeated too quickly" (from memory).

**Restart policy is for transient failures.** A service that crashes on every start should not be retried forever by `Restart=always` with `RestartSec=0`; that burns a CPU and fills the journal. Use a delay, a limit, and `OnFailure=` to alert.

### 6.3 The watchdog

A service can be required to **prove it is alive**. With `Type=notify` and `WatchdogSec=1`, it must send `WATCHDOG=1` to `systemd` at least once per second after it says `READY=1`; if it does not, `systemd` kills it with `SIGABRT` (the fetched page: "killed with SIGABRT (or `WatchdogSignal=`)") and treats the failure as `Result=watchdog`, restarting it if `Restart=` is `on-failure`, `on-abnormal`, `always` or `on-watchdog`. The test service sends `READY=1`, two pings 0.3 s apart, then sleeps without pinging:

```ini
[Service]
Type=notify
NotifyAccess=all
WatchdogSec=1
ExecStart=/bin/bash -c "systemd-notify --ready; for i in 1 2; do sleep 0.3; systemd-notify WATCHDOG=1; done; sleep 30"
```

```text
--- the watchdog: READY=1 and two pings, then silence, WatchdogSec=1
stopped after 1.70 s
$ S systemctl show wd.service -p ActiveState,Result,ExecMainStatus,ExecMainCode
Result=watchdog
ExecMainCode=2
ExecMainStatus=6
ActiveState=failed
```

It was stopped after **1.70 s** (the 0.6 s of pings plus the 1 s timeout, and the manager's own check interval), with `Result=watchdog`, `ExecMainCode=2` (the process was killed by a signal) and `ExecMainStatus=6`, which is **`SIGABRT`**, as the manual says. The point for an interview: a **hung** process (deadlock, stuck in `D`) is not a **dead** one, and `Restart=` alone never notices; the watchdog does. The service has to ping from the code path that proves it works, not from a helper thread that always runs.

### 6.4 Checking a unit before you start it

```text
--- verify a broken unit file
$ systemd-analyze verify /run/lx04-bad/bad.service 2>&1 | cut -c1-120
/run/lx04-bad/bad.service:5: Failed to parse service restart specifier, ignoring: on-fail
/run/lx04-bad/bad.service:6: Unknown key name 'ExecStrt' in section 'Service', ignoring.
bad.service: Service has no ExecStart=, ExecStop=, or SuccessAction=. Refusing.
Unit bad.service has a bad unit file setting.
```

`systemd-analyze verify FILE` found all three faults in the broken unit: the bad value `on-fail` (`Failed to parse service restart specifier`), the misspelt key `ExecStrt` (`Unknown key name`), and then the consequence, `no ExecStart=`. Unknown keys are **ignored with a warning**, not an error: a typo in a hardening option such as `ProtectSystm=strict` means the service runs **unprotected** while looking configured. Run `verify` in CI for every unit you ship.

---

## 7. Socket activation and timers

### 7.1 Socket activation

A `.socket` unit makes `systemd` itself listen. When the first connection arrives, it starts the service and **hands it the already-open socket** (descriptor 3 onward, `LISTEN_FDS`). The service does not have to exist until needed; and because `systemd` holds the socket, the service can be restarted without refusing connections (the kernel queues them). The test is an echo service started once per connection (`Accept=yes`, so each connection gets its own instance, `echo@.service`):

```ini
[Socket]
ListenStream=18081
Accept=yes

[Install]
WantedBy=sockets.target
```

```ini
[Service]
ExecStart=/bin/cat
StandardInput=socket
```

```text
--- socket activation: the service does not exist until someone connects
$ S systemctl is-active echo.socket; S systemctl list-units "echo@*" --no-legend --no-pager | wc -l
active
0
$ S awk "\$2 ~ /:46A1\$/ {print \$2, \"state=\" \$4}" /proc/net/tcp
00000000:46A1 state=0A
sent hello, received: hello
$ S systemctl list-units "echo@*" --no-legend --no-pager | awk "{print \$1, \$3, \$4}"
echo@0-127.0.0.1:18081-127.0.0.1:57334.service active running
$ S systemctl list-units "echo@*" --no-legend --no-pager | wc -l
0
```

- Before any connection: the socket unit is `active` and **no `echo@` instance exists** (0). The listener is visible in `/proc/net/tcp` as `00000000:46A1`, state `0A` (listen); `0x46A1` is 18081, the port in the unit. Nobody is running to hold it: `systemd` is the listener.
- During the connection: one instance, `echo@0-127.0.0.1:18081-127.0.0.1:57334.service`, named by connection number and both addresses, `active running`. The text sent came back.
- After the client closed: **0** instances; the service exited when its input closed.

It is the old `inetd` idea made manageable, and it is why `systemd` can start many services in parallel at boot with no ordering between a service and its clients: the socket exists first (from memory, this is also how `journald` and `dbus` start, and the `systemd-journald.socket` unit appears at 129 ms in section 4.3).

### 7.2 Timers

A `.timer` unit starts another unit (by default the service of the same name). Compared with `cron` (from memory, `cron` is not installed here): a timer's job is a normal unit, so it has dependencies, resource limits, a sandbox, and its output goes to the journal with its unit name; `systemctl list-timers` shows the last and next run; `Persistent=yes` runs a missed job after a reboot (`cron` skips it); `RandomizedDelaySec=` spreads load. `cron` is simpler for a one-line job on one machine and is in every environment.

Two forms. A **calendar** timer (`OnCalendar=`) is wall-clock; `systemd-analyze calendar` normalises an expression and prints when it next fires, which is the way to check one:

```text
--- timers
$ systemd-analyze calendar "Mon..Fri *-*-* 09:00" "*-*-01 00:00:00" "hourly" | grep -E "Original|Next elapse|Normalized"
  Original form: Mon..Fri *-*-* 09:00
Normalized form: Mon..Fri *-*-* 09:00:00
    Next elapse: Mon 2026-11-16 09:00:00 Asia
Normalized form: *-*-01 00:00:00
    Next elapse: Tue 2026-12-01 00:00:00 Asia
  Original form: hourly
Normalized form: *-*-* *:00:00
    Next elapse: Sun 2026-11-15 21:00:00 Asia
$ wc -l < /var/tmp/lx04_tick
4
$ awk "NR>1 {printf \"%.2f \", \$1 - p} {p = \$1} END {print \"seconds between runs\"}" /var/tmp/lx04_tick
1.11 1.11 1.04 seconds between runs
transient timer fired 2 s after creation (asked for 2)
```

- `Mon..Fri *-*-* 09:00` normalised to `Mon..Fri *-*-* 09:00:00`, next elapse Monday 2026-11-16 at 09:00 (the VM's date was Sunday 2026-11-15); `*-*-01 00:00:00` is the first of the month (next: 2026-12-01); `hourly` expands to `*-*-* *:00:00`.
- A **monotonic** timer fires a time after an event: `OnActiveSec=` after the timer started, `OnUnitActiveSec=` after the **unit last started**:

```ini
[Unit]
Description=Runs a job every second

[Timer]
OnActiveSec=1
OnUnitActiveSec=1
AccuracySec=100ms

[Install]
WantedBy=timers.target
```

```ini
[Service]
Type=oneshot
ExecStart=/bin/sh -c "date +%%s.%%N >> /var/tmp/lx04_tick"
```

(The `%%` is a literal percent: `systemd` expands `%` specifiers in `ExecStart=`, so `date +%s` must be written `date +%%s`; this bit me in the first run.) The timer fired **4 times in about 5 s**, with gaps of **1.11, 1.11 and 1.04 s**. They are not 1.00: `AccuracySec` lets `systemd` move a firing inside a window to coalesce wake-ups (I set 100 ms; the default is 1 minute, from memory), and each run starts after the previous one *finished* starting. **Do not use timers for sub-second periods**; that is a loop in the program.

`systemd-run --on-active=2 ... COMMAND` makes a one-off transient timer; with `AccuracySec=100ms` it ran the command 2 s after creation (measured, last line of the transcript): without that property it can be up to a minute late (from memory, the default accuracy).


---

## 8. Resource control: every service is a cgroup

`systemd` puts each service in its own **cgroup**, named after the unit, which is what lets it track every process of a service, kill them all, and limit them together ([LX 03](03_processes_signals_and_proc.md) showed `/proc/PID/cgroup`). The limits are unit settings that `systemd` writes into the cgroup files for you; `systemd-run -p NAME=value COMMAND` starts a throw-away service with settings, and `systemctl set-property` changes them on a **running** unit.

```text
--- every service is a cgroup
$ S systemctl status probe --no-pager | sed -n "1,2p;/CGroup/,\$p" | cut -c1-80
● probe.service - /usr/bin/sleep 100
     Loaded: loaded (/run/systemd/transient/probe.service; transient)
     CGroup: /system.slice/probe.service
             └─72 /usr/bin/sleep 100
$ S cat /proc/$(prop probe MainPID)/cgroup | grep -E "^(4|0):"
4:memory:/system.slice/probe.service
0::/system.slice/probe.service
```

The service `probe` is in `/system.slice/probe.service`; `/proc/PID/cgroup` shows the same path under the `memory` controller (line `4:`) and the unified entry (line `0::`), which is the **hybrid** layout of this VM (cgroup v1 controllers plus a v2 mount for `systemd`). On a version 2 host the file is a single `0::/system.slice/NAME.service` line and the limits are the files `memory.max`, `cpu.max` and `pids.max` (from memory). A service's place in the tree comes from its **slice** (`system.slice` for system services, `user.slice` for logins); a limit on a slice limits everything in it.

### 8.1 Memory: `MemoryMax=`

A program that allocates and touches 200 MB, in a service with `MemoryMax=40M` and no swap:

```text
--- MemoryMax=40M: a program that wants 200 MB
$ S systemctl show hog -p Result,ExecMainStatus,ActiveState,MemoryMax
Result=signal
ExecMainStatus=9
MemoryMax=41943040
ActiveState=failed
$ dmesg | grep -i "memory cgroup out of memory" | tail -1 | cut -c1-110
[  517.744039] Memory cgroup out of memory: Killed process 3771 (python3) total-vm:76500kB, anon-rss:40704kB,
```

The service ended `failed` with `Result=signal` and `ExecMainStatus=9`: `SIGKILL`. The kernel log says why: `Memory cgroup out of memory: Killed process ... (python3) ... anon-rss:40704kB`, that is 40,704 kB resident, just under the 40 MiB (40,960 kB) limit. It is an **OOM kill inside the cgroup**, the same mechanism as the global OOM killer but scoped to the group, so **one runaway service cannot take the machine down**. On a cgroup v2 host `systemd` reports this as `Result=oom-kill` (from memory); here it appears as a plain signal, so **when you see status 9 on a service with a memory limit, check `dmesg` for `Memory cgroup out of memory`** before you suspect a human. `MemoryHigh=` is the soft version: above it the group is throttled and reclaimed hard but not killed (from memory). Exit status 137 in a container is the same event ([LX 03](03_processes_signals_and_proc.md) section 12).

### 8.2 CPU: `CPUQuota=`

Two busy loops, one capped at 20 percent of one CPU, one not; CPU time was read from `/proc/PID/stat` over 5 s (the `cpuacct` counter that `systemd` would use is not usable under this hybrid layout):

```text
--- CPUQuota=20% on a busy loop, 5 s (CPU time read from /proc/PID/stat; the cpuacct controller is not usable under systemd here)
capped: 20 percent of one CPU, uncapped (same VM, other loop running): 98 percent
$ S systemctl show capped -p CPUQuotaPerSecUSec
CPUQuotaPerSecUSec=200ms
```

The capped loop got **20 percent** of one CPU, the uncapped one **98 percent**. `CPUQuotaPerSecUSec=200ms` is the same setting as time: 200 ms of CPU per second. It is a **ceiling**, not a share: the capped service is throttled even when the machine is idle, unlike `CPUWeight=`, which only matters when tasks compete (the `nice` result of [LX 03](03_processes_signals_and_proc.md) section 9, now per service).

```text
--- change a limit on a running service: set-property
after set-property CPUQuota=60%: 60 percent
```

`systemctl set-property --runtime capped CPUQuota=60%` changed the running service: 60 percent a second later, **no restart**. Without `--runtime` the change is also written as a drop-in file and survives reboot.

### 8.3 Processes: `TasksMax=`

```text
--- TasksMax=5
$ S systemctl show forky -p TasksCurrent,TasksMax
TasksCurrent=5
TasksMax=5
$ S journalctl -u forky -o cat --no-pager | head -3 | cut -c1-100
/usr/bin/bash: fork: retry: Resource temporarily unavailable
/usr/bin/bash: fork: retry: Resource temporarily unavailable
```

With `TasksMax=5` the cgroup held 5 tasks (`TasksCurrent=5`), and the shell's attempts to start more got `fork: retry: Resource temporarily unavailable` (`EAGAIN`): a **fork bomb** stops at the limit. It is the cgroup counterpart of `RLIMIT_NPROC`, which is per user and not enforced against root.

Other settings of the family, from memory: `IOWeight=` and `IOReadBandwidthMax=` for the block layer, `MemorySwapMax=`, `CPUAffinity=`, `LimitNOFILE=` (the `RLIMIT_NOFILE` of [LX 03](03_processes_signals_and_proc.md), set per service, since `limits.conf` does not apply to `systemd` services). `systemd-cgtop` shows live per-cgroup CPU, memory and I/O, and `systemctl status UNIT` prints the cgroup tree of the service with every PID, which is the first thing to run to see "what is this service really running".

---

## 9. Sandboxing a service

A service is code that can be wrong or attacked; a unit can take away most of what it should not need. The same program, `probe.sh`, was run as a plain service and as one with six options. It tries to write to `/etc` and `/var/tmp`, looks for a marker file in `/tmp` made outside, lists `/root`, lists network interfaces, prints its capabilities and tries to read the kernel log:

```bash
#!/bin/bash
# probe.sh: what can this process do? run as a service with and without sandbox options
echo "uid=$(id -u)"
for f in /etc/lx04_probe /var/tmp/lx04_probe; do
    if r=$( { echo x > $f; } 2>&1 ); then echo "write $f: succeeded"; rm -f $f; else echo "write $f: ${r##*: }"; fi
done
echo "/tmp holds $(ls /tmp | grep -c lx04_marker) of the marker files made outside"
echo "/root readable: $(ls /root >/dev/null 2>&1 && echo yes || echo no)"
echo "network interfaces: $(awk 'NR>2 {sub(":", "", $1); printf "%s ", $1}' /proc/net/dev)"
echo "CapEff=$(awk '/^CapEff/ {print $2}' /proc/self/status) NoNewPrivs=$(awk '/^NoNewPrivs/ {print $2}' /proc/self/status)"
echo "can read kernel log: $(dmesg >/dev/null 2>&1 && echo yes || echo no)"
```

```ini
[Service]
Type=oneshot
ExecStart=/run/lx04-probe.sh
```

```ini
[Service]
Type=oneshot
ExecStart=/run/lx04-probe.sh
ProtectSystem=strict
ReadWritePaths=/var/tmp
PrivateTmp=yes
ProtectHome=yes
PrivateNetwork=yes
NoNewPrivileges=yes
CapabilityBoundingSet=
ProtectKernelLogs=yes
```

```text
--- open.service
uid=0
write /etc/lx04_probe: succeeded
write /var/tmp/lx04_probe: succeeded
/tmp holds 1 of the marker files made outside
/root readable: yes
network interfaces: lo ifb0 ifb1 eth0 docker0
CapEff=000001fffeffffff NoNewPrivs=0
can read kernel log: yes
--- hardened.service
uid=0
write /etc/lx04_probe: Read-only file system
write /var/tmp/lx04_probe: succeeded
/tmp holds 0 of the marker files made outside
/root readable: no
network interfaces: lo
CapEff=0000000000000000 NoNewPrivs=1
can read kernel log: no
```

```text
--- hardened.service
uid=0
write /etc/lx04_probe: Read-only file system
write /var/tmp/lx04_probe: succeeded
/tmp holds 0 of the marker files made outside
/root readable: no
network interfaces: lo
CapEff=0000000000000000 NoNewPrivs=1
can read kernel log: no
```

| Check | Plain unit (as root) | With the six options | Option responsible |
| --- | --- | --- | --- |
| write `/etc/lx04_probe` | succeeded | `Read-only file system` | `ProtectSystem=strict` (everything but `/dev`, `/proc`, `/sys` read-only) |
| write `/var/tmp/lx04_probe` | succeeded | succeeded | `ReadWritePaths=/var/tmp` (the one path I allowed) |
| marker made in `/tmp` outside | visible (1 of 1) | **0** | `PrivateTmp=yes` (the service has its own empty `/tmp`) |
| read `/root` | yes | no | `ProtectHome=yes` (`/home`, `/root`, `/run/user` made inaccessible) |
| network interfaces | `lo ifb0 ifb1 eth0 docker0` | `lo` | `PrivateNetwork=yes` (a new network namespace with only loopback) |
| effective capabilities | `000001fffeffffff`, `NoNewPrivs=0` | `0000000000000000`, `NoNewPrivs=1` | `CapabilityBoundingSet=` (empty: drop all) and `NoNewPrivileges=yes` |
| read the kernel log | yes | no | `ProtectKernelLogs=yes` |

(The meaning of each option in the last column is from memory of the manual page; the effects are what was measured. `docker0` is the bridge left by my first careless boot, section 4.2; on a clean machine the list would be `lo` and the real interfaces.) Note that **the service is still `uid=0`**: root with every capability removed and a read-only world is much less than root, but the better settings are `User=` or `DynamicUser=yes`, which the next table shows are still missing.

How much is left to do, according to `systemd-analyze security` (a score from 0, no exposure, to 10, fully exposed; it counts the options that are not set):

```text
--- the exposure score systemd-analyze security gives each unit (10 is fully exposed)
open      → Overall exposure level for open.service: 9.6 UNSAFE :-{
hardened  → Overall exposure level for hardened.service: 5.6 MEDIUM :-|
$ S systemd-analyze security hardened.service --no-pager 2>&1 | grep -c "✗"
42
$ S systemd-analyze security hardened.service --no-pager 2>&1 | grep "✗" | head -4 | cut -c1-110
✗ RootDirectory=/RootImage=                                   Service runs within the host's root directory
✗ User=/DynamicUser=                                          Service runs as root user
✗ PrivateDevices=                                             Service potentially has access to hardware dev
✗ ProtectClock=                                               Service may write to the hardware clock or sys
```

The plain unit scored **9.6 UNSAFE**, the hardened one **5.6 MEDIUM**, and 42 checks were still failed (`✗`); the first ones listed are `RootDirectory=/RootImage=` (the service runs in the host's root), `User=/DynamicUser=` (runs as root), `PrivateDevices=` and `ProtectClock=`. Use it as a **checklist**, not as a goal: some options break some programs. Method: start from the score, add options one at a time, run the service, read the journal for `Permission denied`, and keep what the program tolerates. Two caveats: an option that is misspelt is ignored with only a warning (section 6.4), and sandboxing options are enforced by the kernel features they use (mount namespaces, capabilities, `seccomp`), so they limit a compromised service, not a compromised kernel.

---

## 10. Logs: the journal, `dmesg` and `logrotate`

### 10.1 What `journald` stores

`journald` collects the output of services (standard output and error, which it captures through the socket `systemd` gives each service), the kernel log, `syslog` calls and messages sent with `systemd-cat`, and stores each entry as **structured fields**, not as a line of text. One error entry, as data:

```text
--- one entry as structured data (journald keeps fields, not just text)
MESSAGE            write failed
PRIORITY           3
SYSLOG_IDENTIFIER  lx04
_UID               0
_COMM              -
_TRANSPORT         stdout
_SYSTEMD_UNIT      -
_SYSTEMD_CGROUP    -
_BOOT_ID           39f9c5c4...
```

`MESSAGE`, `PRIORITY` (3 is `err`), `SYSLOG_IDENTIFIER` (the tag) are what the sender gave; the fields starting with an underscore (`_UID`, `_TRANSPORT`, `_SYSTEMD_UNIT`, `_BOOT_ID`) are added by `journald` from what the kernel says about the sender and **cannot be forged by it**. This message came from `systemd-cat`, so it has no unit (`-`) and the transport is `stdout`. The `_BOOT_ID` is what separates boots.

### 10.2 Filters

Test entries: three from `systemd-cat -t lx04` at `info`, `warning` and `err`, and one from another tag.

```text
--- filters
$ S journalctl -t lx04 -o short --no-pager | cut -c1-90 | sed -E "s/^[A-Z][a-z]{2} [0-9]+ [0-9:]+ //"
vm lx04: a test message at warning level
vm lx04[70]: service started
vm lx04[72]: disk 91 percent full
vm lx04[74]: write failed
$ S journalctl -t lx04 -p warning --no-pager -o cat
a test message at warning level
disk 91 percent full
write failed
$ S journalctl -t lx04 -p err..err --no-pager -o cat
write failed
$ S journalctl -t lx04 --since "-1 min" --no-pager -o cat | wc -l
4
$ S journalctl -F PRIORITY --no-pager | sort | tr "\n" " "; echo
3 4 5 6 7
```

- `-t lx04` selects by tag (the first line is the kernel message with the prefix `lx04:` that an earlier test wrote to `/dev/kmsg`: the kernel log is in the journal too).
- `-p warning` means **that level and every more severe** (here `warning` and `err`: 2 messages plus the kernel one); `-p err..err` is the range form for exactly one level.
- `--since "-1 min"` takes relative or absolute times (`--until` too, and `-b` for "this boot", `-b -1` for the previous one).
- `-F PRIORITY` lists the **values a field has taken** (3 to 7 in this journal): a quick way to see what is in there. Any field can be a filter: `journalctl _SYSTEMD_UNIT=sshd.service _PID=123`; `-u UNIT` is the shorthand for the unit field.
- `-o cat` prints only the message; `-o json` the full entry (used above); `-o verbose` every field; `-f` follows like `tail -f`; `-g PATTERN` greps (from memory); `-n 50` the last 50 lines.

A service's standard output and standard error are logged by default:

```text
--- a service, filtered by unit; the stdout of a service is logged
$ S journalctl -u chatty --no-pager -o cat | head -5
to-stdout
to-stderr
$ S journalctl _SYSTEMD_UNIT=chatty.service -o verbose --no-pager | grep -E "^ +(_PID|_COMM|PRIORITY|_STREAM_ID|SYSLOG_IDENTIFIER)" | sed -E "s/=[0-9a-f]{32}/=.../" | head -5
    PRIORITY=6
    _STREAM_ID=...
    SYSLOG_IDENTIFIER=sh
    _PID=85
    _COMM=sh
```

Both lines of the test service (`to-stdout` at `info`, `to-stderr` also captured) are under `-u chatty`; the `verbose` fields show `PRIORITY=6` (info), the `_STREAM_ID` that groups one stream and the process's `_PID` and `_COMM`. **A service should log to standard output and let `journald` do the rest**: no log file to rotate, no permission problem, and each line carries the unit and the PID.

### 10.3 Rate limits

A service that loops on an error can write millions of lines. `journald` limits each service: the fetched `journald.conf(5)` gives the default as **10,000 messages per 30 seconds per service** (`RateLimitIntervalSec=` and `RateLimitBurst=`; 0 turns the limit off), and a unit can set its own with `LogRateLimitIntervalSec=` and `LogRateLimitBurst=`. The test: 600 lines in under a second, limit 50 per 5 s.

```text
--- rate limiting per service: 600 lines in under a second, limit 50 per 5 s
$ S journalctl -u spam --no-pager -o cat | grep -c "^line"
150
$ S journalctl --no-pager -o cat --grep Suppressed | tail -2 | cut -c1-100
```

**150 of 600 lines were kept**, not 50, and the search for a "Suppressed" notice found nothing in this harness. I did not find why it is 150: from memory, `journald` scales the allowed burst with the free space of the file system, so the configured number is a base, not a guarantee; the shape of the result (a quarter of the lines survive, silently) is the part to remember. The practical consequence: **when a burst of errors looks truncated in the journal, the rate limit ate it, and the number of lines is not a count of events.** Count in the program, or raise the limit for that unit.

### 10.4 Where the journal lives

```text
--- where the journal lives
$ S sh -c "ls /var/log/journal | wc -l; ls /run/log/journal 2>&1 | head -2"
1
$ S journalctl --disk-usage
Archived and active journals take up 8.0M in the file system.
$ S journalctl --list-boots --no-pager | cut -c1-70 | sed -E "s/[0-9a-f]{32}/BOOTID/"
IDX BOOT ID                          FIRST ENTRY                  LAST
  0 BOOTID Sun 2026-11-15 20:12:57 Asia Sun
$ S journalctl -k --no-pager | wc -l
647
```

The fetched `journald.conf(5)`: `Storage=auto` keeps the journal in `/var/log/journal` (persistent, survives reboot) **if that directory exists**, otherwise in `/run/log/journal` (volatile, lost at reboot). In my test I created the directory, so it is persistent: one entry in `/var/log/journal` and the listing of `/run/log/journal` found nothing to print. **On a machine where a service dies and the box reboots, a volatile journal has no record of it** (a classic gap in embedded images). `journalctl --disk-usage` said 8.0M; the size limit (`SystemMaxUse=`) defaults to 10 percent of the file system, capped at 4 GiB (fetched), and `journalctl --vacuum-size=200M` or `--vacuum-time=2weeks` trims by hand. `--list-boots` shows one boot here (the namespace is a single boot), `-k` is the kernel part (647 lines, the host's kernel log). `ForwardToSyslog=` sends messages on to a traditional `syslog` daemon such as `rsyslog` (from the page: a boolean, off by default; `rsyslog` is not installed here, so what a distribution enables is from memory), which matters when logs must go to a remote server in a format that tool understands.

### 10.5 `logrotate`: the writer follows the inode

Programs that write their own log files need rotation, or the disk fills. `logrotate` does it on a schedule (itself usually started by a `systemd` timer or `cron`, from memory) and the **way** it rotates decides whether the program keeps logging to the right file. Two logs, each held open by a process that appends a line every 0.1 s, one rotated by renaming and re-creating (`create`), one by `copytruncate`:

```text
=== logrotate, and the process that keeps writing to the old file
before: app.log inode 10387478 size 25;copy.log inode 10387477 size 25;
$ logrotate -d -s /var/tmp/lr/state /var/tmp/lr/rotate.conf 2>&1 | grep -E "considering|rotating|renaming|creating" | head -5 | sed "s#/var/tmp/lr/##g"
rotating pattern: app.log  1 bytes (3 rotations)
considering log app.log
  log needs rotating
rotating log app.log, log->rotateCount is 3
renaming app.log.3.gz to app.log.4.gz (rotatecount 3, logstart 1, i 3),
after:
  app.log inode 10387480 size 0
  app.log.1 inode 10387478 size 75
  copy.log inode 10387477 size 45
  copy.log.1 inode 10387479 size 30
the writer of app.log still has: app.log.1
the writer of copy.log still has: copy.log
```

The first run line, `-d`, is the **debug (dry-run) mode**: it printed what would be done and changed nothing: `app.log needs rotating`, the rename chain. Then the real run:

- **Rename style** (`app.log`): after rotation the new `app.log` is a new file (inode 10387480, size 0), and the old one is `app.log.1` with the **same inode, 10387478**, and it grew from 25 to **75 bytes after** the rotation. `the writer of app.log still has: app.log.1`: the process holds a descriptor to the **inode**, not to the name ([LX 03](03_processes_signals_and_proc.md) section 11), so it **keeps writing to the rotated file** and the fresh `app.log` stays empty. The fix is to tell the program to reopen its log (a `postrotate` script that signals it, usually `SIGHUP` or `systemctl reload`) or never to write its own file (log to the journal).
- **`copytruncate`** (`copy.log`): `logrotate` **copies** the content to `copy.log.1` (30 bytes) and **truncates** the same file, so the writer's descriptor stays valid (`still has: copy.log`, size 45 after, including lines written after). The price (from memory): lines written between the copy and the truncate are lost, and the copy temporarily doubles the disk use. It is the choice for programs that cannot be told to reopen.

It is the same file-and-inode fact as the deleted-file trap of [LX 03](03_processes_signals_and_proc.md); a disk that stays full after rotation is the same symptom.

---

## 11. Recovery: a broken `fstab`, rescue mode and a lost password

### 11.1 A bad line in `/etc/fstab` (measured)

At boot, `systemd` turns each `fstab` line into a `.mount` unit, and the units for local file systems are **required** by `local-fs.target`, so one that cannot be mounted **fails the boot into emergency mode**, with a console asking for the root password. The test line names a device that does not exist (`/dev/lx04-missing`) and a short wait (`x-systemd.device-timeout=3s`; the usual wait is about 90 seconds, from memory, which is the classic "A start job is running for dev-disk-by-uuid ... (1min 30s)" screen):

```text
--- fstab line: /mnt/lx04 ext4 defaults,x-systemd.device-timeout=3s
$ S systemctl is-system-running
maintenance
$ S systemctl --failed --no-legend --no-pager | cut -c1-100
$ S systemctl list-jobs --no-legend --no-pager | cut -c1-70
$ S systemctl show local-fs.target -p ActiveState,Wants,Requires | cut -c1-110
Requires=mnt-lx04.mount
Wants=systemd-remount-fs.service
ActiveState=inactive
$ S systemctl show emergency.target -p ActiveState
ActiveState=active
```

```text
--- fstab line: /mnt/lx04 ext4 defaults,nofail,x-systemd.device-timeout=3s
$ S systemctl is-system-running
running
$ S systemctl --failed --no-legend --no-pager | cut -c1-100
$ S systemctl list-jobs --no-legend --no-pager | cut -c1-70
$ S systemctl show local-fs.target -p ActiveState,Wants,Requires | cut -c1-110
Requires=
Wants=systemd-remount-fs.service mnt-lx04.mount
ActiveState=active
$ S systemctl show emergency.target -p ActiveState
ActiveState=inactive
```

Without `nofail`: `is-system-running` = **`maintenance`**, `emergency.target` **active**, and `local-fs.target` **inactive** with `Requires=mnt-lx04.mount`: the file system the boot required never came. With `nofail`: `running`, `emergency.target` inactive, and the mount moved from `Requires=` to **`Wants=`** of `local-fs.target`, so its failure is allowed. (At that moment `--failed` and `list-jobs` printed nothing in either case, so they would **not** have told you; the target states did.) The rule: **every `fstab` entry that is not needed to boot (data disks, network mounts, USB) gets `nofail`**, and network file systems `_netdev`. Test an edited `fstab` with `mount -a` (and `findmnt --verify`, from memory) **before** rebooting; an unreachable remote machine with a typo in `fstab` is a trip to the data centre.

### 11.2 The recovery tools (from memory, not run here)

| Situation | Method |
| --- | --- |
| Boots into emergency mode | Read the failed unit: `systemctl --failed`, `journalctl -xb`; fix `fstab` or comment the line; `systemctl default` or reboot. The root file system is read-only in emergency mode, so `mount -o remount,rw /` first |
| `rescue.target` against `emergency.target` | rescue: basic system, local file systems mounted, services mostly not started (single-user); emergency: only the root file system, read-only, nothing else. Add `systemd.unit=rescue.target` (or `emergency.target`) to the kernel command line at the boot loader to choose it for one boot |
| Bad kernel update | choose the older kernel in the GRUB menu (that is what the menu is for); keep two |
| Lost root password | at the GRUB menu press `e`, append `init=/bin/bash` (or `rd.break` on Red Hat family initramfs images, which stops before the switch to the real root), boot, `mount -o remount,rw /`, run `passwd`, and on SELinux systems `touch /.autorelabel`, then reboot. This needs **physical or console access**, which is why a boot loader password and disk encryption exist |
| Cannot boot at all | boot a live image, mount the root file system, `chroot` into it (with `/dev`, `/proc` and `/sys` bind-mounted), repair, regenerate the initramfs and boot loader configuration |
| A service stops the boot | `systemd.mask=NAME.service` on the command line, or `systemctl mask NAME` from a rescue shell (a mask links the unit to `/dev/null`) |

---

## 12. A triage playbook

| Symptom | First commands | How to read the answer |
| --- | --- | --- |
| "service will not start" | `systemctl status UNIT`; `journalctl -u UNIT -b --no-pager` | the `Active:` line and `Result` say how it ended (`exit-code`, `signal`, `timeout`, `watchdog`, `start-limit-hit`); the last log lines say why |
| "it keeps restarting" | `systemctl show UNIT -p NRestarts,Result,ExecMainStatus`; `journalctl -u UNIT` | `NRestarts` rising; status 9 plus `dmesg` memory cgroup line: the memory limit; `start-limit-hit`: `reset-failed` after the fix |
| "unit file has no effect" | `systemd-analyze verify FILE`; `systemctl cat UNIT`; `systemctl daemon-reload` | a typo is ignored with a warning; `cat` shows the unit **with its drop-ins**, so you see what is really loaded |
| "boot is slow" | `systemd-analyze`; `systemd-analyze critical-chain` | shorten the **chain** (section 4.3); a unit waiting for a device or the network is the usual cause |
| "dropped into emergency mode" | `systemctl --failed`; `journalctl -xb`; `cat /etc/fstab` | a required mount or a dependency failed (section 11); add `nofail` to non-essential mounts |
| "service should run at boot but does not" | `systemctl is-enabled UNIT`; `systemctl list-dependencies --reverse UNIT` | `enable` was never run, or nothing `WantedBy=` a target that is reached |
| "job did not run" | `systemctl list-timers`; `systemd-analyze calendar EXPR`; `journalctl -u NAME.service` | the next elapse; a timer is only the trigger, its service has the log |
| "logs missing after a reboot" | `ls /var/log/journal`; `journalctl --list-boots` | no directory means a volatile journal (section 10.4) |
| "disk full of logs" | `journalctl --disk-usage`; `du -sh /var/log/*`; `lsof +L1` | a journal over its limit (vacuum) or an app log held open after deletion or rotation (section 10.5) |
| "what did the kernel say" | `dmesg -T --level=err,warn`; `journalctl -k -b -1` | hardware, file system and OOM messages; `-b -1` for the boot before a crash, if the journal is persistent |
| "something is hogging CPU or memory" | `systemctl status UNIT` (the cgroup tree); `systemd-cgtop` | which service the processes belong to; limit it (section 8) |

---

## 13. What it costs: the numbers worth remembering

Measured on one VM; quote them as ranges.

| Quantity | Value |
| --- | --- |
| `systemd` ready after being started as PID 1 of a namespace, to `basic.target` | 0.53 s; `systemd-analyze` userspace 406 ms |
| `Requires=` alone, A started relative to B's end | 1,010 ms before (no ordering) |
| `Requires=` with `After=` | 21 ms after |
| `Restart=on-failure`, `StartLimitBurst=3`, exit 1 | 3 starts, `NRestarts=3`, then `failed` |
| clean exit, `on-failure` against `always` | 0 restarts against 3 in 2 s |
| `WatchdogSec=1`, no pings after two | killed at 1.70 s, status 6 (`SIGABRT`), `Result=watchdog` |
| socket-activated instances before, during, after one connection | 0, 1, 0 |
| 1 s timer gaps | 1.11, 1.11, 1.04 s |
| one-off transient timer, `AccuracySec=100ms`, asked for 2 s | fired at 2 s |
| `MemoryMax=40M`, 200 MB wanted | killed, status 9, anon-rss 40,704 kB |
| `CPUQuota=20%` against uncapped | 20 against 98 percent; 60 percent after `set-property` |
| `TasksMax=5` | `fork` failed with `EAGAIN` at the sixth |
| `systemd-analyze security`, plain against six options | 9.6 UNSAFE against 5.6 MEDIUM (42 checks still failed) |
| journal rate limit, 600 lines with burst 50 per 5 s | 150 kept |
| `logrotate` rename against `copytruncate`, writer's file | `app.log.1` (the old inode) against `copy.log` (the same file) |
| `fstab` bad line without `nofail` against with | `maintenance` against `running` |

---

## 14. Common mistakes

1. **`Requires=` without `After=`** (section 5): both start at once; the "dependency" is not ready.
2. **`After=network.target` for a service that needs the network** (section 5): use `network-online.target` and retry.
3. **Editing a unit file and not running `systemctl daemon-reload`**, or editing the vendor file under `/usr/lib` instead of a drop-in (`systemctl edit`) (section 4.1).
4. **A typo in an option** (section 6.4): ignored with a warning, so the hardening you think you have is absent; run `systemd-analyze verify`.
5. **`Restart=always` for a program that exits cleanly on purpose** (section 6.2): a restart loop; and `RestartSec=0` with no start limit on a crashing one.
6. **Treating "started" as "ready"** (section 6.1): `Type=simple` is up when forked; use `notify`, or an explicit readiness check, for units that others wait on.
7. **A daemon that forks itself under `systemd`** (section 6.1): use the foreground process and `Type=exec` or `notify`.
8. **Reading `blame` as the thing to fix** (section 4.3): the critical chain is what delays boot.
9. **A service log written to its own file with rename rotation and no reopen** (section 10.5): the program keeps writing to the rotated file; log to standard output instead.
10. **A volatile journal on a machine that reboots on failure** (section 10.4): create `/var/log/journal` or set `Storage=persistent`.
11. **Assuming the journal holds every line** (section 10.3): per-service rate limits drop bursts.
12. **An `fstab` entry for a non-essential disk without `nofail`** (section 11): a missing USB stick or a down NFS server turns into an emergency shell.
13. **`systemctl enable` when you meant `start`** (and the reverse) (section 5).
14. **Raising `LimitNOFILE` in `limits.conf` for a `systemd` service**: it does not apply; set it in the unit (section 8).
15. **Using `MemoryMax=` as a tuning knob**: it is a kill threshold; if you want pressure without killing, `MemoryHigh=`, and find out why the service needs the memory.
16. **Starting a `systemd` experiment in a namespace without isolating `/tmp`, `/run` and the target** (section 4.2): `systemd-tmpfiles` can empty the host's `/tmp`, and the default target can start whatever is enabled (measured, to my cost).
17. **Putting a secret in `ExecStart=` or `Environment=`**: unit files and `systemctl show` are world-readable by default (from memory); use `LoadCredential=` or a file with mode 0600 (`EnvironmentFile=`).

---

## 15. Questions out loud

Say each answer in 30 to 60 seconds, with one command or one measured number.

1. **Walk me through boot, from power-on to a login.** Firmware (UEFI) reads its boot entry and starts the boot loader from the EFI system partition; the loader loads the kernel and the initramfs and passes the command line; the kernel starts, unpacks the initramfs and runs its `/init`; the initramfs loads the drivers needed, finds the root file system and switches to it; `systemd` starts as PID 1 and reaches `sysinit.target`, `basic.target`, `multi-user.target` and `graphical.target`. A failure at each stage looks different (section 1.1).
2. **What is the initramfs for?** It is a small RAM file system that the loader gives the kernel, with the drivers and tools (RAID, LVM, LUKS) needed to find and mount the real root; without it the kernel would need the disk driver built in.
3. **How do you see the kernel's messages, and make your own?** `dmesg -x` or `journalctl -k`; `echo "<4>text" > /dev/kmsg` writes one at level warning (measured).
4. **How does `sysctl` persist?** Files in `/etc/sysctl.d/*.conf` applied by `sysctl --system` and at boot; a later file name wins (measured: 30 over 20); check the value afterwards.
5. **What is a unit, and the main types?** A named object that `systemd` manages: service, socket, timer, mount, path, target, slice, scope, device. Unit files live in `/etc/systemd/system` (first), `/run/systemd/system`, `/usr/lib/systemd/system`; drop-ins add settings.
6. **`Requires=` against `After=`?** Requirement against order; independent. Measured: with `Requires=` alone both started together and A was active 1,010 ms before B finished; with `After=` it came 21 ms after.
7. **`Wants=` against `Requires=`?** `Wants` is a soft pull (failure does not stop this unit); `Requires` is hard when combined with `After`; `BindsTo` also stops this unit when the other stops.
8. **How do you make a service restart, and how do you stop a crash loop?** `Restart=on-failure` with `RestartSec=`, and `StartLimitBurst=` in `StartLimitIntervalSec=` (measured: 3 starts then `failed`); `reset-failed` to start again; `OnFailure=` to alert.
9. **`on-failure` against `always`?** A clean exit is restarted only by `always` (measured: 0 against 3 restarts in 2 s).
10. **What does the watchdog do that `Restart=` does not?** It catches a hung process: no ping within `WatchdogSec` means `SIGABRT` and `Result=watchdog` (measured: killed at 1.70 s).
11. **What is socket activation for?** `systemd` holds the listening socket and starts the service on the first connection; start-up order does not matter, and the service can restart without refusing connections (measured: 0, 1, 0 instances).
12. **Timer or cron?** A timer is a normal unit (logs, limits, dependencies, `Persistent=` catch-up, `list-timers`); `cron` is simpler and universal. Check an expression with `systemd-analyze calendar`.
13. **How do you limit a service's memory and CPU?** `MemoryMax=` (a kill: status 9 and a `Memory cgroup out of memory` line in `dmesg`), `CPUQuota=` (a ceiling: measured 20 against 98 percent), `TasksMax=`; `systemctl set-property` changes a running one.
14. **How do you harden a unit?** `ProtectSystem=strict`, `PrivateTmp`, `ProtectHome`, `PrivateNetwork`, `NoNewPrivileges`, an empty `CapabilityBoundingSet`, `User=`/`DynamicUser=`; check with `systemd-analyze security` (measured 9.6 to 5.6).
15. **How do you find why a service died?** `systemctl status UNIT` for `Result` and the exit status, `journalctl -u UNIT -b` for the last words, `dmesg` for a kernel kill.
16. **Why can a log look incomplete?** Rate limiting per service (measured: 150 of 600), a volatile journal lost at reboot, or `logrotate` leaving the writer on the old file.
17. **Rename rotation against `copytruncate`?** Rename leaves the program on the old file (measured: still `app.log.1`) unless it is told to reopen; `copytruncate` keeps the file but can lose lines.
18. **The machine drops into emergency mode after an edit to `fstab`. What now?** `journalctl -xb`, read the failed mount; fix or comment the line; `nofail` for non-essential mounts (measured: `maintenance` against `running`).
19. **How do you recover a lost root password?** At the boot loader add `init=/bin/bash` or `rd.break`, remount root read-write, `passwd`, relabel on SELinux, reboot; this needs console access (from memory).
20. **A boot takes three minutes; where do you look?** `systemd-analyze` for the split (firmware, loader, kernel, initrd, userspace), `critical-chain` for the delaying units (usually a device or network wait), then the unit's own log.


---

## Problems

**Where to solve them.** On a machine with `systemd` 250 or later (or in the namespace harness of section 4.2), a shell, `systemd-analyze`, `journalctl` and Python 3, as root for Problems C to E. **Run your solution and compare with the stated result** before reading the answer.

**How to approach them.** Start with the question: which fact do I need (a value the manager parsed, a time, a state, a counter), and which command prints it? Then change one thing and print the fact again.

### Problem A (E, entry level): lint a unit

A unit file contains `Restart=on-fail`, `ExecStrt=/bin/true` and nothing else. Without starting anything, show every fault, then write the corrected file and show that the lint is clean.

### Problem B (E, entry level): when does it fire?

For the calendar expressions `Mon,Wed *-*-* 02:30`, `*-*-31 00:00` and `*:0/15`, show how `systemd` normalises each and when it next fires. Which of the three skips some months?

### Problem C (M, medium): one program's errors

Four messages are logged: `billing` at `err`, `info` and `crit`, and `search` at `err`. Print only the messages of `billing` at priority `err` **or worse** from the last minute, as plain text.

### Problem D (M, medium): which `sysctl` file wins

Make `vm.swappiness` persistent with **two** files, `50-` setting 20 and `90-` setting 30, apply them, and say which value is live and why. Restore the machine.

### Problem E (H, hard): a bounded, self-limiting service

Write a unit for a program that tries to use 200 MB: it may use 30 MB, is restarted after a failure with a 0.5 s delay, is started **at most 3 times in 20 s**, and then stays `failed`. Prove the number of starts with a counter, and say which process ended it and how.

---

## Answers

The answers were run, and the checks are in the output.

```bash
#!/usr/bin/env bash
# ans_prob.sh: the answers to Problems A to E, each run and checked
. "$(dirname "$0")/h.sh"; . "$(dirname "$0")/sd.sh"
prop() { S systemctl show "$1" -p "$2" --value; }
echo '=== Problem A: find what is wrong with a unit file without starting it'
d=/run/lx04-ans; mkdir -p $d
printf '[Service]\nRestart=on-fail\nExecStrt=/bin/true\n' > $d/a.service
printf '[Service]\nRestart=on-failure\nExecStart=/bin/true\n' > $d/a-fixed.service
show 'systemd-analyze verify $d/a.service 2>&1 | cut -c1-110'
show 'systemd-analyze verify $d/a-fixed.service 2>&1 | wc -l'
rm -rf $d
echo
echo '=== Problem B: when does this timer fire? (today in this VM: '"$(date '+%a %Y-%m-%d')"')'
show 'systemd-analyze calendar "Mon,Wed *-*-* 02:30" "*-*-31 00:00" "*:0/15" 2>&1 | grep -E "Normalized|Next elapse|Original" | cut -c1-70'
echo
echo '=== Problem C: errors of one program in the last minute, as plain text'
sd_boot || echo "boot failed"
S systemd-cat -t billing -p err echo "card declined"
S systemd-cat -t billing -p info echo "order 17 ok"
S systemd-cat -t billing -p crit echo "database down"
S systemd-cat -t search -p err echo "index stale"
sleep 0.5
show 'S journalctl -t billing -p err --since "-1 min" -o cat --no-pager'
echo
echo '=== Problem D: make vm.swappiness persistent, and see which file wins'
printf 'vm.swappiness = 20\n' > /etc/sysctl.d/50-lx04.conf
printf 'vm.swappiness = 30\n' > /etc/sysctl.d/90-lx04.conf
show 'sysctl --system 2>&1 | grep -E "lx04"'
show 'cat /proc/sys/vm/swappiness'
rm -f /etc/sysctl.d/50-lx04.conf /etc/sysctl.d/90-lx04.conf; sysctl -qw vm.swappiness=60
echo
echo '=== Problem E: a service that may use 30 MB, is restarted twice at most, and then stays failed'
cat > /var/tmp/lx04_hog.py <<'PY'
import time
open('/var/tmp/lx04_starts', 'a').write('start\n')
b = []
for i in range(40):
    b.append(bytearray(5 * 1024 * 1024))
    for j in range(0, len(b[-1]), 4096): b[-1][j] = 1
    time.sleep(0.02)
PY
S sh -c "cat > /run/systemd/system/limited.service" < "$(dirname "$0")/units/limited.service"
S systemctl daemon-reload
S systemctl start limited.service; sleep 5
show 'S systemctl show limited.service -p ActiveState,Result,NRestarts,ExecMainStatus'
show 'wc -l < /var/tmp/lx04_starts'
show 'S systemctl show limited.service -p StartLimitBurst,MemoryMax,Restart'
sd_stop
rm -f /var/tmp/lx04_hog.py /var/tmp/lx04_starts
```

```ini
[Unit]
StartLimitIntervalSec=20
StartLimitBurst=3

[Service]
ExecStart=/usr/bin/python3 /var/tmp/lx04_hog.py
MemoryMax=30M
MemorySwapMax=0
Restart=on-failure
RestartSec=0.5
```

```text
=== Problem A: find what is wrong with a unit file without starting it
$ systemd-analyze verify $d/a.service 2>&1 | cut -c1-110
/run/lx04-ans/a.service:2: Failed to parse service restart specifier, ignoring: on-fail
/run/lx04-ans/a.service:3: Unknown key name 'ExecStrt' in section 'Service', ignoring.
a.service: Service has no ExecStart=, ExecStop=, or SuccessAction=. Refusing.
Unit a.service has a bad unit file setting.
$ systemd-analyze verify $d/a-fixed.service 2>&1 | wc -l
0

=== Problem B: when does this timer fire? (today in this VM: Sun 2026-11-15)
$ systemd-analyze calendar "Mon,Wed *-*-* 02:30" "*-*-31 00:00" "*:0/15" 2>&1 | grep -E "Normalized|Next elapse|Original" | cut -c1-70
  Original form: Mon,Wed *-*-* 02:30
Normalized form: Mon,Wed *-*-* 02:30:00
    Next elapse: Mon 2026-11-16 02:30:00 Asia
  Original form: *-*-31 00:00
Normalized form: *-*-31 00:00:00
    Next elapse: Thu 2026-12-31 00:00:00 Asia
  Original form: *:0/15
Normalized form: *-*-* *:00/15:00
    Next elapse: Sun 2026-11-15 20:30:00 Asia

=== Problem C: errors of one program in the last minute, as plain text
$ S journalctl -t billing -p err --since "-1 min" -o cat --no-pager
card declined
database down

=== Problem D: make vm.swappiness persistent, and see which file wins
$ sysctl --system 2>&1 | grep -E "lx04"
* Applying /etc/sysctl.d/50-lx04.conf ...
* Applying /etc/sysctl.d/90-lx04.conf ...
$ cat /proc/sys/vm/swappiness
30

=== Problem E: a service that may use 30 MB, is restarted twice at most, and then stays failed
$ S systemctl show limited.service -p ActiveState,Result,NRestarts,ExecMainStatus
Result=signal
NRestarts=3
ExecMainStatus=9
ActiveState=failed
$ wc -l < /var/tmp/lx04_starts
3
$ S systemctl show limited.service -p StartLimitBurst,MemoryMax,Restart
Restart=on-failure
MemoryMax=31457280
StartLimitBurst=3
```

**Problem A.** `systemd-analyze verify` printed the three faults of section 6.4: the bad restart value (`Failed to parse service restart specifier, ignoring: on-fail`), the unknown key (`ExecStrt`), and the consequence, `Service has no ExecStart=`; the corrected file (`Restart=on-failure`, `ExecStart=/bin/true`) printed **0 lines**. The lint never starts a unit, so it is safe to run on a production machine and in CI.

**Problem B.** `Mon,Wed *-*-* 02:30` normalised to `Mon,Wed *-*-* 02:30:00` and next fires on Monday 2026-11-16 at 02:30 (the VM's date was Sunday 2026-11-15); `*:0/15` normalised to `*-*-* *:00/15:00`, every quarter of an hour (next: 20:30 on the VM's clock); and **`*-*-31 00:00` skips months**: the next elapse was **Thursday 2026-12-31**, because November has 30 days. If "the last day of every month" is what you mean, use `*-*-~1` (from memory, not run here), not day 31.

**Problem C.** `journalctl -t billing -p err --since "-1 min" -o cat` printed `card declined` and `database down`: `-p err` includes everything more severe than `err` (`crit` here), the `info` message is left out, and the `search` message is excluded by the tag. Drop `-o cat` to see times and the unit; add `-o json` to process the entries.

**Problem D.** The live value is **30**: `sysctl --system` applied `50-lx04.conf` and then `90-lx04.conf`, in name order, so the later setting overwrote the earlier. Files are applied in lexical order of the file name, which is why the number prefix is part of the convention. Both files were removed afterwards and the value set back to 60.

**Problem E.** The unit is `limited.service` above: `MemoryMax=30M` with `MemorySwapMax=0`, `Restart=on-failure`, `RestartSec=0.5`, and in `[Unit]` `StartLimitBurst=3` and `StartLimitIntervalSec=20`. The result: `ActiveState=failed`, `Result=signal`, `ExecMainStatus=9`, **3 lines in the counter file, so 3 starts** (the first plus two restarts), and `NRestarts=3`. The process that ended each run was the Python program, killed by `SIGKILL` from the kernel's memory-cgroup OOM killer (status 9; on a cgroup v2 host the unit would more likely show `Result=oom-kill`, from memory). `StartLimitBurst` and `StartLimitIntervalSec` belong in `[Unit]`, not `[Service]` (a mistake I see often; `systemd-analyze verify` would warn about the wrong section, from memory).

---

## Cheat sheet

```text
Boot            firmware (UEFI) -> boot loader (GRUB, systemd-boot) -> kernel + initramfs -> switch root -> systemd -> sysinit.target -> basic.target -> multi-user.target -> graphical.target
Kernel          dmesg -x  -T  --level=err,warn     journalctl -k -b -1     echo "<4>text" > /dev/kmsg     /proc/cmdline     /proc/sys/kernel/printk
Modules         lsmod  modinfo  modprobe NAME  modprobe -r     /etc/modules-load.d  /etc/modprobe.d     modprobe looks in /lib/modules/$(uname -r)
sysctl          sysctl -w k=v (now)     /etc/sysctl.d/NN-name.conf + sysctl --system (later file wins)     check with sysctl k
Units           /etc/systemd/system > /run/systemd/system > /usr/lib/systemd/system     drop-in: NAME.service.d/*.conf   systemctl edit / cat / daemon-reload
Order           Requires/Wants = pull in;  After/Before = order;  they are independent:  Wants= + After=  is the usual pair;  BindsTo, PartOf, Conflicts, OnFailure
Type            simple (forked)  exec  forking  oneshot (+ RemainAfterExit)  notify (READY=1)  dbus  idle        do not daemonise
Restart         no  on-failure  on-success  on-abnormal  on-abort  on-watchdog  always;  RestartSec=;  StartLimitBurst= in StartLimitIntervalSec= ([Unit]);  reset-failed
Watchdog        Type=notify  WatchdogSec=N  service sends WATCHDOG=1  ->  miss = SIGABRT, Result=watchdog
Sockets         NAME.socket (ListenStream, Accept=)  starts NAME.service on first connect;  LISTEN_FDS from fd 3
Timers          OnCalendar=  OnActiveSec=  OnUnitActiveSec=  Persistent=  AccuracySec=  RandomizedDelaySec=   systemd-analyze calendar EXPR    systemctl list-timers
Limits          MemoryMax (kill) MemoryHigh (throttle) CPUQuota (ceiling) CPUWeight TasksMax IOWeight LimitNOFILE     systemctl set-property [--runtime]  systemd-run -p ...
Sandbox         ProtectSystem=strict ReadWritePaths= PrivateTmp ProtectHome PrivateNetwork NoNewPrivileges CapabilityBoundingSet= ProtectKernelLogs User=/DynamicUser=   systemd-analyze security UNIT
Analyze         systemd-analyze  blame  critical-chain [TARGET]  plot  verify FILE  calendar  security
Journal         journalctl -u UNIT -b -p err -t TAG --since "-1 h" -f -n 50 -k -o cat|json|verbose -F FIELD _FIELD=value   --disk-usage --vacuum-size=  --list-boots
Journal files   /var/log/journal exists = persistent (Storage=auto), else /run/log/journal (lost at reboot);  rate limit per service (default 10000 per 30 s)
logrotate       rename style: writer stays on the old inode, needs postrotate reload;  copytruncate: same file, may lose lines;  best: log to stdout/journal
fstab           nofail for non-essential mounts, _netdev for network;  mount -a before reboot;  bad line without nofail -> emergency.target, is-system-running = maintenance
Recovery        systemd.unit=rescue.target | emergency.target;  init=/bin/bash or rd.break + remount rw + passwd (+ /.autorelabel);  live image + chroot;  systemd.mask=UNIT
```

---

## Sources

Fetched in this session (what each confirmed):

- [bootup(7)](https://man7.org/linux/man-pages/man7/bootup.7.html): firmware to boot loader to kernel plus initramfs; in the initrd, `initrd.target`, `initrd-root-device.target`, root mounted at `/sysroot`, `initrd-switch-root.service` last; on the real root `sysinit.target`, `basic.target`, `multi-user.target`, `graphical.target` in that order.
- [systemd.service(5)](https://man7.org/linux/man-pages/man5/systemd.service.5.html): the `Restart=` table (which exit causes restart), the `Type=` values and when each counts as started, `WatchdogSec=` (default 0, killed with `SIGABRT`, restart when `Restart=` is `on-failure`, `on-abnormal`, `always` or `on-watchdog`), `RestartSec=` default 100 ms.
- [systemd.unit(5)](https://man7.org/linux/man-pages/man5/systemd.unit.5.html): `Wants=`, `Requires=`, `Requisite=`, `BindsTo=`, `PartOf=`, `Conflicts=`, `Before=`/`After=`, `OnFailure=`; ordering and requirement are independent; load-path precedence `/etc`, `/run`, `/usr/lib`; drop-in directories. (Only the first 100,000 of about 147,000 characters of the page were read.)
- [journald.conf(5)](https://man7.org/linux/man-pages/man5/journald.conf.5.html): `Storage=auto` is persistent only if `/var/log/journal` exists; the rate limit defaults of 10,000 messages per 30 seconds per service; `SystemMaxUse=` default 10 percent of the file system capped at 4 GiB; `ForwardToSyslog=` off by default.

Run in this session: systemd 255.4 (`systemctl`, `systemd-analyze`, `systemd-run`, `journalctl`, `systemd-cat`), logrotate 3.21, kmod, util-linux (`unshare`, `nsenter`), Python 3 and bash 5.2.21, on Linux 6.18.44 (2 vCPUs, about 8 GiB) as root, with PID 1 `/process_api`, cgroup v1 (hybrid). Not installed, so not used: `cron`, `rsyslog`, `udevadm`, `ip`, `ss`, `dracut`, GRUB.

Not retrieved: `systemd.exec(5)`, `systemd.resource-control(5)`, `systemd.timer(5)`, `systemd.socket(5)`, `systemd-analyze(1)` and `logrotate(8)`. Everything about them is **from the experiments** or **from memory**.

From memory, not fetched or not tested in this session: the whole boot chain below the firmware level except what `bootup(7)` says (UEFI boot entries and the EFI system partition, secure boot with `shim`, GRUB and `systemd-boot`, kernel and initramfs duties, `root=UUID=`), `init=` against `rdinit=` semantics, the printk labels, `dmesg -T` being approximate after suspend, `pstore` and `kdump`, `udev` and module aliases, `modules.dep` and `depmod`, `modprobe.d` and `modules-load.d`, files in `/usr/lib/sysctl.d` being replaced by same-name files in `/etc`, `systemd-sysctl.service` applying them at boot, `network-online.target` against `network.target`, `enable` against `start`, the "start request repeated too quickly" message, the `AccuracySec` default of one minute, `Persistent=` and `RandomizedDelaySec=`, `cron` behaviour, `Result=oom-kill` on cgroup v2, `MemoryHigh=` as throttling, the cgroup v2 file names, `systemd-cgtop`, the exact meaning of each sandbox option, the journal burst scaling with free space, the 90 second device timeout, `findmnt --verify`, `_netdev`, the rescue and emergency target definitions, `init=/bin/bash`, `rd.break`, `/.autorelabel`, `systemd.mask=`, `*-*-~1`, and `LoadCredential=`.

Cross-references: [LX 01](01_shell_and_scripting.md) (shell, exit statuses), [LX 03](03_processes_signals_and_proc.md) (processes, signals, `/proc`, the deleted-file trap, limits, PID 1), [OS 02](../05_operating_systems/02_processes.md), [OS 21](../05_operating_systems/21_os_interview_question_bank.md), [CO 30](../04_computer_organization/00_plan.md) (secure boot), the [debugging track](../16_debugging/00_plan.md).
