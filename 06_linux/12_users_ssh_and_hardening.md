# LX 12: Users, SSH and hardening

*Part of the [Linux plan](00_plan.md) · Time: about 8 hours · You need: [LX 03](03_processes_signals_and_proc.md) (processes, credentials in `/proc`), [LX 07](07_files_permissions_and_storage.md) (permission bits and ACLs), [LX 08](08_linux_networking_toolbox.md) (`ss`, ports, `nft` in outline) and [LX 09](09_containers_namespaces_and_cgroups.md) (user namespaces, `no_new_privs`). The earlier tutorials made the machine fast and observable. This one decides **who may do what on it, from where, and what you can prove afterwards**.*

Every system interview question about security is the same three questions asked from different sides: **who is this** (authentication: users, keys, certificates, PAM), **what may they do** (authorisation: file modes, `sudo`, capabilities, the firewall, SELinux or AppArmor) and **what happened** (accounting: logs and `auditd`). A host is hardened when the answers are narrow, written down, checked by a program and recorded when they change. The tutorial follows that order: accounts (section 1), `sudo` (2), privilege on a running process (3), PAM (4), SSH as a user (5), SSH as an administrator (6), SSH certificates (7), the firewall (8), `auditd` (9), packages, time and mandatory access control (10), a worked audit of a broken host (11), then costs, mistakes, questions and problems.

**Ten honest limits, stated first.**

1. Everything is measured on **one Linux 6.18 VM** (Firecracker, 2 vCPUs), as **root**, on **Ubuntu 24.04** with **OpenSSH 9.6p1**, **`nftables` 1.0.9**, **`sudo` 1.9**, **`auditd` 3.1** and **`pamtester` 0.1.2**. The packages were installed with `apt` for this tutorial. Numbers are for this VM only.
2. **There is no `systemd`**: PID 1 is the sandbox's own process manager. So `systemctl`, `journalctl`, `loginctl`, `systemd` units for `sshd` and `chronyd`, and `pam_systemd` were not run. LX 04 covers them; this tutorial starts daemons by hand.
3. **`sshd` runs as root in the background on loopback addresses** (`127.0.0.1`, `127.0.0.2`) with **`UsePAM no`**, so **real password logins, `pam_unix` password prompts, `pam_faillock` counting and a login `tty` were not exercised**. PAM is demonstrated with **`pamtester`** against small test services, which runs the real modules without a login.
4. **`AppArmor` and `SELinux` are not active**: `aa-status` says `apparmor not present`, `/sys/kernel/security/lsm` does not exist, and `SELinux` is compiled in (`CONFIG_SECURITY_SELINUX=y`) but no policy is loaded and `sestatus` is not installed. **Section 10 is therefore from memory and marked so**, including `ls -Z`, `getsebool`, `ausearch -m avc` and `aa-status` output.
5. **`chronyd` cannot run** (`506 Cannot talk to daemon`), so time synchronisation is explained, not measured. `dnf` and `rpm` are not installed; the comparison with `apt` is from memory.
6. **The sandbox's "root" lacks one capability**: `CAP_SYS_RESOURCE` (bit 24) is missing from the bounding set (section 3). Anything that needs it is out of reach here.
7. **`auditd` runs in the foreground** (`auditd -f`) and writes its records to a file I redirect; the netlink path from the kernel, the rules and `ausearch` are the real ones. The `auid` (login user id) of every event reads `unset` because nothing here is a login session (section 9).
8. **The test accounts** (`alice`, `bob`, `svc`), the two `sshd` daemons, the test CA and every key were created by the scripts and **removed at the end of each one**. The sandbox's own sudoers rule for its user is the one finding that stays (section 11).
9. The **`nft` rules were tested on loopback** against the `input` and `output` hooks only. Forwarding between interfaces and a second machine were not available.
10. Timings (`ssh` start up, `auditd` costs) come from a noisy VM and are reported as **three runs or a range**. Only large ratios are trusted.

By the end you can:

- explain **uid, gid, the three account files, NSS and `shadow` fields**, and create, lock and age an account (measured: new account `alice` got **uid 30033** and a `!` password field; `passwd -S` reports **`L` for both `!` and `*`**; `chage -M 90` put the expiry at **Feb 15, 2027**);
- write a **`sudoers` rule that allows one command**, check it with **`visudo -cf`** before it can lock you out, and read the `sudo` log (measured: a typo failed at **column 38**; a rule for two commands let `id` run and refused `cat /etc/shadow`);
- say **what makes a process privileged** (measured: root's effective set has **40 capabilities**; an ordinary user has **0**; **`setcap cap_net_bind_service=ep`** let user 30033 bind port 80; a **setuid copy of a program had effective uid 0** and **`no_new_privs` kept it at 30033**);
- read a **PAM stack** and predict what it does with `required`, `requisite`, `sufficient` and `[success=1 default=ignore]` (measured with `pamtester`: **four `auth` stacks, three `account` stacks and the limits applied at session open**, each result matching the reading of the rules);
- use **SSH** for keys, host key checking, **restricted keys**, **`ProxyJump`**, **local, remote and dynamic forwarding**, **`ControlMaster`** and **`ssh-agent`** (measured: **20 connections took 4.07 s, and 0.145 s multiplexed**, about **28 times** faster);
- **harden `sshd`** with a file you can check (`sshd -t`, `sshd -T`) and see exactly what changed against the defaults (measured: **90 effective settings**, **14 changed** by the hardened file);
- issue and revoke **SSH certificates** and trust a CA instead of keys (measured: expired, wrong principal, wrong source address and revoked serial were **all refused**);
- write an **`nft` ruleset** with a set, a default drop, a rate limit and a DNAT, replace it atomically, and tell **drop from reject** (measured: reject answered in **2 ms**, drop made the client wait the full **3 s**);
- add **`auditd` rules** for files and system calls, read the records, and **know what auditing costs** (measured: **about 620 ns per `open`+`close` without a rule, about 740 to 965 ns with an unrelated rule, 26,000 to 42,000 ns when the rule matches**; a small queue **lost 24,915 records without stopping the program**);
- check **package signatures and file integrity** (measured: **`gpgv` accepted a signature, then rejected the file after one byte changed**; `dpkg -V` named the altered `ssh-keygen`) and **audit a host** with a short script (measured: **11 findings** on a host with seven planted mistakes, **1** after the fixes).

**Where the numbers come from.** Ten experiment scripts (`x1` to `x10`), a checker (`hcheck.sh`) and a problem answer script (`ans_prob`) were run in this session on the VM described above; every output in the tutorial is a frozen transcript of one of them. Transcripts print `$ command` before each command's output. In them `alice` and `bob` are test accounts, `hostA` is an `sshd` on `127.0.0.1:2201` and `hostB` one on `127.0.0.2:2202` that can only be reached through `hostA` (a jump host), and `/tmp/lx12lab/` holds the keys and logs. Process ids, times and fingerprints that change on every run were replaced by `N`, `T` or `...`. Numbers not from these runs are marked "from memory" or come from a source at the end.

**Data and timing.** No data sets. Each script runs in seconds to a minute (the audit costs take the longest, about 30 s). All timings are repeated runs on a noisy VM.

---

## 1. Accounts: users, groups and the three files

### 1.1 The files

A **user** is a number, the **uid**; the name is a label for people. The kernel only ever sees uids and gids (LX 03). Three plain text files, read by the C library, map the labels to the numbers and keep the secrets:

| File | Holds | Mode here | Fields (colon separated) |
| --- | --- | --- | --- |
| `/etc/passwd` | one line per user | `644`, readable by everyone | name, `x` (the password is elsewhere), **uid**, **primary gid**, comment (GECOS), home, **login shell** |
| `/etc/shadow` | the password hashes and ageing | **`640 root:shadow`** | name, **hash**, last change (days since 1970), min, max, warn, inactive, expire, reserved |
| `/etc/group` | one line per group | `644` | name, `x`, **gid**, comma separated member list |
| `/etc/gshadow` | group passwords and administrators | `640 root:shadow` | name, hash, administrators, members |

```text
--- 1. the three files
$ head -3 /etc/passwd
root:x:0:0:root:/root:/bin/bash
daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin
bin:x:2:2:bin:/bin:/usr/sbin/nologin
$ ls -l /etc/passwd /etc/shadow /etc/group /etc/gshadow
-rw-r--r-- 1 root root   1076 Nov 18 02:54 /etc/group
-rw-r----- 1 root shadow  948 Nov 18 02:54 /etc/gshadow
-rw-r--r-- 1 root root   3615 Nov 18 02:54 /etc/passwd
-rw-r----- 1 root shadow 1484 Nov 18 02:54 /etc/shadow
$ useradd -m -s /bin/bash -c 'Alice Example' alice; getent passwd alice
alice:x:30033:30033:Alice Example:/home/alice:/bin/bash
$ getent shadow alice | cut -d: -f1,2,3,4,5
alice:!:20774:0:99999
$ id alice; getent group alice
uid=30033(alice) gid=30033(alice) groups=30033(alice)
alice:x:30033:
```

Read it. `/etc/passwd` is world readable on purpose: every `ls -l` has to turn a uid into a name. That is why the hash had to leave it (old Unix kept it there) and why **`shadow` is `640 root:shadow`**, readable only by root and the group `shadow`, which a few set-group-id helpers such as `unix_chkpwd` use (section 3). `useradd -m -s /bin/bash -c ...` created **alice with uid 30033 and a group of the same number** (the "user private group" scheme: every user has a group of their own, so a shared directory with `g+w` does not expose files to anyone else). The new `shadow` entry is **`alice:!:20774:0:99999`**: password field `!` (**locked**, no password can match it), last change on day **20774** (days since 1970-01-01, which is 2026-11-17), minimum age 0, maximum age 99999 days (no expiry). Two more points. The `useradd` defaults come from **`/etc/login.defs`** and `/etc/default/useradd`, and section 1.3 prints the ones that matter. And **a uid is not unique to a name**: two lines can share uid 0 (section 11 makes one on purpose), and the kernel will treat both as root.

### 1.2 How a name becomes a number: NSS

`id alice`, `ls -l`, `getent`, `ssh` and `sudo` do not read the files themselves. They call the C library (`getpwnam`, `getgrgid`, `gethostbyname`), which consults the **Name Service Switch**, configured in **`/etc/nsswitch.conf`**, one line per database, each a list of sources tried in order:

```text
--- 2. how a name becomes a number: NSS
$ grep -E '^(passwd|group|shadow|hosts):' /etc/nsswitch.conf
passwd:         files
group:          files
shadow:         files
hosts:          files dns
$ ls /lib/x86_64-linux-gnu/libnss_*.so.2 | sed 's#.*/##' | tr '\n' ' '; echo
libnss_compat.so.2 libnss_dns.so.2 libnss_files.so.2 libnss_hesiod.so.2
$ getent passwd 30033; getent passwd nosuchuser; echo rc=$?
alice:x:30033:30033:Alice Example:/home/alice:/bin/bash
rc=2
```

Here `passwd`, `group` and `shadow` use **`files`** only, and `hosts` is **`files dns`** (the file `/etc/hosts` first, then the resolver of LX 08). A source is a shared library named `libnss_<source>.so.2`: **four** are installed (`files`, `compat`, `dns`, `hesiod`). **On a company laptop or server the line reads `passwd: files sss` or `files systemd ldap`**, from memory: the extra library asks `sssd` or `LDAP` or `systemd-userdbd`, and then **a user can exist with no line in `/etc/passwd`**. That is the answer to "`id bob` works but `grep bob /etc/passwd` is empty". The tool that follows the same path as every program is **`getent`**: `getent passwd 30033` found alice by number, and **`getent passwd nosuchuser` exits with 2** (a scripting convention: 0 found, 2 not found). Use `getent`, never `grep /etc/passwd`, in a script that must work on a machine with a directory service.

### 1.3 Password ageing, locking and the defaults

```text
--- 3. password aging and locking
$ passwd -S alice
alice L 2026-11-17 0 99999 7 -1
$ usermod -p '*' alice; passwd -S alice | cut -d' ' -f1,2
alice L
$ chage -M 90 -m 1 -W 14 alice; chage -l alice
Last password change                                    : Nov 17, 2026
Password expires                                        : Feb 15, 2027
Password inactive                                       : never
Account expires                                         : never
Minimum number of days between password change          : 1
Maximum number of days between password change          : 90
Number of days of warning before password expires       : 14
$ passwd -l alice; passwd -S alice | cut -d' ' -f1,2; getent shadow alice | cut -c1-14
passwd: password changed.
alice L
alice:!*:20774
$ usermod -p '*' alice; getent shadow alice | cut -d: -f1,2
alice:*
```

Four things worth reading slowly.

- **`passwd -S` prints `L` for both `!` and `*`**. A new account has `!` (locked); `usermod -p '*'` set `*` (no password can ever match, but the account is not "locked" in `passwd`'s sense) and **`passwd -S` still printed `L`**. The difference matters to `sshd`: with `UsePAM no` an account whose field starts with **`!` is refused even for a valid key** ("account is locked"), while `*` is allowed (the lab needed `usermod -p '*'` for exactly this; with `UsePAM yes` PAM's `account` stack decides instead). **`passwd -l` prepends `!`**: `*` became **`!*`**.
- **`chage -M 90 -m 1 -W 14`** set the maximum age to 90 days, minimum 1 and warning 14, and `chage -l` computed **"Password expires: Feb 15, 2027"** (last change Nov 17 plus 90 days). `chage -E` sets the **account** expiry (the `-E 0` or `-L` pair is how an offboarding script disables a contractor on a date), and `-I` the days of grace after a password expires.
- **The defaults**: `UID_MIN 1000` and `UID_MAX 60000` (the range for ordinary users), `UMASK 022`, `PASS_MAX_DAYS 99999` (passwords never expire), `ENCRYPT_METHOD SHA512`. System accounts get uids below 1000 and a **`nologin` shell**. The hash format is the first characters of the field, `$6$` for SHA-512, **`$y$` for yescrypt** (the newer default on Debian 12 and Ubuntu 22.04 and later, from memory; this image's `login.defs` still says SHA512). Change the algorithm and existing hashes keep the old one until the user changes the password.
- **Accounts with a non-login shell**: `/usr/sbin/nologin` or `/bin/false` in the shell field means "no interactive login" and is how a service account is made. **`awk` found 58 accounts with uid 0 or such a shell, exactly one with uid 0 (`root`) and none with an empty password field.** Those two checks, "only root has uid 0" and "no empty hash", are the first two lines of any audit (section 11).

```text
--- 4. uid ranges and system accounts
$ grep -E '^(UID_MIN|UID_MAX|SYS_UID_MIN|SYS_UID_MAX|UMASK|PASS_MAX_DAYS|ENCRYPT_METHOD)' /etc/login.defs
UMASK           022
PASS_MAX_DAYS   99999
UID_MIN                  1000
UID_MAX                 60000
ENCRYPT_METHOD SHA512
$ awk -F: '$3==0 || $7 ~ /nologin|false/ {n++} END {print n" accounts with uid 0 or a non-login shell"}' /etc/passwd
58 accounts with uid 0 or a non-login shell
$ awk -F: '$3==0 {print $1}' /etc/passwd
root
$ awk -F: '$2=="" {print $1}' /etc/shadow | wc -l
0
```

---

## 2. `sudo`: allowing one thing, and proving it

### 2.1 What `sudo` is

`sudo` is a **setuid root** program (`-rwsr-xr-x`, section 3) that reads **`/etc/sudoers`** and the files in **`/etc/sudoers.d/`**, decides whether the calling user may run the command as the target user, asks for the caller's password unless the rule says `NOPASSWD`, **logs** the decision and runs the command. It replaces "give them the root password", which cannot be audited and cannot be narrowed.

A rule has the form `who where=(as_whom) options: what`:

```text
alice ALL=(root) NOPASSWD: /usr/bin/id, /usr/bin/whoami
```

reads "alice, on any host, may run these two commands as root with no password". Groups use a `%`: `%wheel ALL=(ALL) ALL`. A **`Cmnd_Alias`** names a list, **`Defaults:alice`** sets options for one user, and a full path is part of the safety: **`/usr/bin/id` allows exactly that file; a bare `id` would depend on `PATH`**. **Never allow a command that can run other commands** (`vi`, `less`, `find`, `awk`, `tar`, a shell script in a writable directory): each has a shell escape, so the rule gives away root. The safe forms are a fixed command with fixed arguments, or **`sudoedit`** for a file.

### 2.2 Check before you install, test as the user

```text
--- 5. sudo: a drop-in, checked before it is installed
$ visudo -cf /tmp/lab.sudo
/tmp/lab.sudo: parsed OK
$ visudo -cf /tmp/bad.sudo; echo rc=$?
/tmp/bad.sudo:1:38: syntax error
alice ALL=(root) NOPASSWD /usr/bin/id
                                     ^
rc=1
$ install -m 440 /tmp/lab.sudo /etc/sudoers.d/lab; visudo -c | tail -3
/etc/sudoers: parsed OK
/etc/sudoers.d/README: parsed OK
/etc/sudoers.d/lab: parsed OK
$ sudo -l -U alice | tail -4
    env_reset, mail_badpass, secure_path=/usr/local/sbin\:/usr/local/bin\:/usr/sbin\:/usr/bin\:/sbin\:/bin\:/snap/bin, use_pty, !lecture, logfile=/tmp/sudo.log

User alice may run the following commands on vm:
    (root) NOPASSWD: /usr/bin/id, /usr/bin/whoami
$ runuser -u alice -- sudo -n id
uid=0(root) gid=0(root) groups=0(root)
$ runuser -u alice -- sudo -n cat /etc/shadow; echo rc=$?
sudo: a password is required
rc=1
$ cat /tmp/sudo.log | sed -E 's/^[A-Z][a-z]{2} +[0-9]+ [0-9:]+ : //' | cut -c1-110
alice : PWD=/home/claude/lx12 ; USER=root ;
    COMMAND=/usr/bin/id
alice : a password is required ; PWD=/home/claude/lx12 ;
    USER=root ; COMMAND=/usr/bin/cat /etc/shadow
```

What the run shows, in order.

- **`visudo -cf FILE`** parses a candidate file without installing it: `parsed OK` for the good one, and for the bad one (a missing `:` after `NOPASSWD`) **`syntax error` at 1:38 with a caret under the problem, exit code 1**. **A syntax error in the live sudoers file locks every user out of `sudo`**, which on a server whose root login is disabled means a rescue boot. So the rule is: **write a drop-in in `/etc/sudoers.d/`, check it with `visudo -cf`, install it with `install -m 440`** (sudo ignores a file with the wrong mode or a name containing a dot or ending in `~`, from memory), then run `visudo -c` on the whole set (`/etc/sudoers`, the `README` and `lab` all parsed OK).
- **`sudo -l -U alice`** lists what alice may do: the effective `Defaults` (`env_reset`, `secure_path=...`, `use_pty`, and the two I set: `!lecture` and `logfile=/tmp/sudo.log`) and **`(root) NOPASSWD: /usr/bin/id, /usr/bin/whoami`**. `env_reset` throws away the caller's environment except a short list, and `secure_path` replaces `PATH`: together they close the "set `LD_PRELOAD` or a hostile `PATH` and run it as root" attacks.
- **The test as alice**: `sudo -n id` printed `uid=0(root)`; `sudo -n cat /etc/shadow` printed **`sudo: a password is required`** and exit code 1. `-n` means "never ask", the right flag for a script.
- **The log**: both attempts are in `/tmp/sudo.log` with the user, the working directory, the target user and the **full command**, and the refused one carries **"a password is required"**. With a normal syslog setup the same records go to `auth.log` or the journal (from memory); the `logfile=` default sets a separate file. This is the accounting half: **the command is recorded, not the keystrokes inside it**, which is the reason to never allow a shell.

**`su` and `sudo -i`**: `su` asks for the **target** user's password; `sudo` asks for the **caller's**. `su -` starts a login shell as the target; `sudo -i` does the same through `sudo`, so it is logged and needs no shared secret. A shared root password is the thing `sudo` removes.


---

## 3. Privilege on a running process

### 3.1 What makes a process privileged

A process has **credentials** (LX 03): a real, effective and saved uid and gid, supplementary groups, and **capability sets**. Permission checks use the effective uid and the effective capabilities. "Root" is two things: **uid 0** (which owns files) and **a full set of capabilities** (which allows operations: `CAP_NET_BIND_SERVICE` binds ports below 1024, `CAP_NET_RAW` opens raw sockets, `CAP_SYS_ADMIN` is the large grab bag, `CAP_DAC_OVERRIDE` ignores file modes, `CAP_CHOWN` changes owners). Each process carries five sets, printed in `/proc/PID/status`: **permitted** (the most it may use), **effective** (what is in force now), **inheritable**, **bounding** (an upper limit nothing can exceed) and **ambient**.

```text
--- 1. root is a set of capability bits, not one bit
$ grep -E 'Cap(Inh|Prm|Eff|Bnd|Amb)' /proc/self/status
CapInh: 0000000000000000
CapPrm: 000001fffeffffff
CapEff: 000001fffeffffff
CapBnd: 000001fffeffffff
CapAmb: 0000000000000000
$ capsh --decode=$(awk '/CapEff/{print $2}' /proc/self/status) | tr ',' '\n' | wc -l
40
$ runuser -u alice -- grep -E 'Cap(Prm|Eff|Bnd)' /proc/self/status
CapPrm: 0000000000000000
CapEff: 0000000000000000
CapBnd: 000001fffeffffff
```

The root shell has **`CapEff 000001fffeffffff`**, which `capsh --decode` expands to **40 capabilities**; alice's shell has **`CapEff 0`**, the empty set, with the same bounding set. Look at the mask: `1fffeffffff` is bits 0 to 40 with **bit 24 cleared**, and bit 24 is **`CAP_SYS_RESOURCE`**. The sandbox removed it from the bounding set (limit 6), so even this "root" cannot, for instance, raise some resource limits above the hard limit. `getpcaps $$` prints it the short way: `=ep cap_sys_resource-ep`, "everything effective and permitted except this one". **Root is a convention for "has all the capabilities"; what the kernel checks is the capability.**

### 3.2 File capabilities: a narrow privilege without setuid

The old way to let a program do one privileged thing was to make it **setuid root**: it then runs with all of root's power. The better way is a **file capability**: an attribute on the executable that grants the process just the bits it needs.

```text
--- 2. a low port as an ordinary user
$ runuser -u alice -- /tmp/lx12c/bind80
uid 30033: bind to port 80 failed: Permission denied
$ setcap cap_net_bind_service=+ep /tmp/lx12c/bind80; getcap /tmp/lx12c/bind80
/tmp/lx12c/bind80 cap_net_bind_service=ep
$ runuser -u alice -- /tmp/lx12c/bind80
uid 30033: bound to port 80
$ setcap -r /tmp/lx12c/bind80; getcap /tmp/lx12c/bind80; runuser -u alice -- /tmp/lx12c/bind80
uid 30033: bind to port 80 failed: Permission denied
```

`bind80` is a tiny C program that binds port 80. As user 30033 it fails with **`Permission denied`**. **`setcap cap_net_bind_service=+ep FILE`** put one capability in the file (`e` effective, `p` permitted); `getcap` shows `cap_net_bind_service=ep`; the same unprivileged user then printed **`bound to port 80`**; and **`setcap -r`** removed it and the bind failed again. The service needed one bit out of 40, and no other power. Where it is stored:

```text
--- 3. what is in the file that carries a capability
$ setcap cap_net_bind_service=+ep /tmp/lx12c/bind80; getfattr -d -m security.capability -e hex /tmp/lx12c/bind80 2>&1 | tail -2
security.capability=0x0100000200040000000000000000000000000000

$ cp /tmp/lx12c/bind80 /tmp/lx12c/bind80b; getcap /tmp/lx12c/bind80b; echo '(a cp without --preserve=xattr drops it)'
(a cp without --preserve=xattr drops it)
```

The capability is an **extended attribute**, `security.capability`, with **20 bytes**: the value `0x01000002 00040000 00000000 00000000 00000000` is a header word (revision 2 with the "effective" flag), the **permitted set `0x00000400`** (bit 10, `CAP_NET_BIND_SERVICE`), and zeros for inheritable. Two consequences that bite. **A plain `cp` does not copy it** (the copy printed nothing from `getcap`): packaging and deployment tools must preserve extended attributes (`cp --preserve=xattr`, `tar --xattrs`, `rsync -X`), or the service starts and cannot bind. And **rebuilding the binary erases it**: Problem B makes this a repeatable failure. Capabilities are also **ignored for scripts** (the kernel honours them only on an ELF binary), and **ignored on filesystems mounted `nosuid`**.

### 3.3 setuid and setgid: what is on this image

```text
--- 4. setuid programs on this image
$ find /usr /bin /sbin -xdev -perm -4000 -type f 2>/dev/null | sort | tr '\n' ' '; echo
/usr/bin/chfn /usr/bin/chsh /usr/bin/fusermount3 /usr/bin/gpasswd /usr/bin/mount /usr/bin/newgrp /usr/bin/passwd /usr/bin/su /usr/bin/sudo /usr/bin/umount /usr/lib/dbus-1.0/dbus-daemon-launch-helper /usr/lib/openssh/ssh-keysign /usr/lib/polkit-1/polkit-agent-helper-1
$ find /usr /bin /sbin -xdev -perm -2000 -type f 2>/dev/null | sort | tr '\n' ' '; echo
/usr/bin/chage /usr/bin/expiry /usr/bin/ssh-agent /usr/lib/x86_64-linux-gnu/utempter/utempter /usr/sbin/pam_extrausers_chkpwd /usr/sbin/unix_chkpwd
$ getcap -r /usr 2>/dev/null | head
/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-ptp-helper cap_net_bind_service,cap_net_admin,cap_sys_nice=ep
/usr/bin/ping cap_net_raw=ep
/usr/bin/mtr-packet cap_net_raw=ep
$ ls -l /usr/bin/passwd /usr/bin/su /usr/bin/sudo | cut -c1-10,26-
-rwsr-xr-x4152 May 30  2024 /usr/bin/passwd
-rwsr-xr-x5680 Aug 19 22:17 /usr/bin/su
-rwsr-xr-x2032 Oct  6 19:21 /usr/bin/sudo
```

**13 setuid files** (`passwd`, `su`, `sudo`, `mount`, `umount`, `chfn`, `chsh`, `gpasswd`, `newgrp`, `fusermount3`, `ssh-keysign`, and two helpers for `dbus` and `polkit`) and **6 setgid files** (`chage`, `expiry`, `ssh-agent`, `utempter`, and the two `*_chkpwd` helpers that read `shadow`). Three programs hold **file capabilities** instead of setuid: **`ping` has `cap_net_raw=ep`**, `mtr-packet` the same, and a media helper has three. **Each one on this list is a program whose bugs are privilege escalation**, so the list is an audit item: any new entry is a finding (section 11 flags a planted `idsuid`).

```text
--- 5. the effect of setuid on a running process
$ cp /tmp/lx12c/ids /tmp/lx12c/ids_suid; chown root /tmp/lx12c/ids_suid; chmod u+s /tmp/lx12c/ids_suid; stat -c '%A %U %n' /tmp/lx12c/ids /tmp/lx12c/ids_suid
-rwxr-xr-x root /tmp/lx12c/ids
-rwsr-xr-x root /tmp/lx12c/ids_suid
$ runuser -u alice -- /tmp/lx12c/ids
real 30033 effective 30033 saved 30033
$ runuser -u alice -- /tmp/lx12c/ids_suid
real 30033 effective 0 saved 0
$ runuser -u alice -- setpriv --no-new-privs /tmp/lx12c/ids_suid
real 30033 effective 30033 saved 30033
```

The meaning of the bit, in one run. `ids` prints the real, effective and saved uid. Run as alice it printed **`real 30033 effective 30033 saved 30033`**. The root owned copy with the setuid bit (**`-rwsr-xr-x root`**) printed **`real 30033 effective 0 saved 0`**: the kernel set the effective and saved uid to the **file's owner** at `execve`, the real uid stays the caller. And the same setuid file run through `setpriv --no-new-privs` printed **`effective 30033`**: the gain did not happen.

### 3.4 `no_new_privs` and user namespaces

```text
--- 6. no_new_privs stops the gain
$ runuser -u alice -- sh -c 'grep NoNewPrivs /proc/self/status'
NoNewPrivs:     0
$ runuser -u alice -- setpriv --no-new-privs sh -c 'grep NoNewPrivs /proc/self/status; sudo -n id'
NoNewPrivs:     1
sudo: The "no new privileges" flag is set, which prevents sudo from running as root.
sudo: If sudo is running in a container, you may need to adjust the container configuration to disable the flag.
```

**`no_new_privs`** is a one way process flag (`prctl(PR_SET_NO_NEW_PRIVS)`) that says "an `execve` may never give this process or its children more privilege than it has now": setuid bits and file capabilities are ignored. It is how container runtimes, `systemd` sandboxing and `seccomp` filters (LX 09) are made safe to apply. The proof here: with the flag set (`NoNewPrivs: 1`), **`sudo` refused to run as root** with a message that names the flag. If an application "works as root in my shell but not in the container", this flag is a first suspect.

```text
--- 7. user namespaces: root that is not root
$ runuser -u alice -- unshare -Ur sh -c 'id -u; cat /proc/self/uid_map; grep -E "Cap(Prm|Eff)" /proc/self/status; touch /etc/x 2>&1; ls -ld /etc | cut -c1-30'
0
         0      30033          1
CapPrm: 000001ffffffffff
CapEff: 000001ffffffffff
touch: cannot touch '/etc/x': Permission denied
drwxr-xr-x 111 nobody nogroup
$ sysctl user.max_user_namespaces kernel.unprivileged_userns_clone 2>&1
user.max_user_namespaces = 32018
sysctl: cannot stat /proc/sys/kernel/unprivileged_userns_clone: No such file or directory
$ runuser -u alice -- unshare -Urm sh -c 'mount -t tmpfs none /mnt && echo mounted tmpfs in my own namespace'
mounted tmpfs in my own namespace
```

A **user namespace** gives a process **a full set of capabilities inside a private view of ids**, with no power over the real system. Alice ran `unshare -Ur` and saw **`id -u` = 0**, **`uid_map` = `0 30033 1`** ("uid 0 in here is uid 30033 out there, one id wide") and **`CapEff 000001ffffffffff`: 41 capabilities, including bit 24, which the real root lacks**, because capabilities in a namespace are relative to it. And the limit on what that buys: `touch /etc/x` was still **`Permission denied`** and `/etc` appears owned by `nobody` (an id with no mapping). With `-m` added she mounted a `tmpfs` in her **own mount namespace**: allowed, because it only changes her private view. This is the basis of rootless containers and also a **large kernel attack surface**, so some distributions let administrators turn off unprivileged creation (`user.max_user_namespaces=0`; here it is **32,018**). Know both halves: "it is how rootless Podman works" and "it is how several kernel exploits reached root".

```text
--- 8. capabilities of a process you do not own
$ getpcaps $$
15616: =ep cap_sys_resource-ep
$ pid=$(pgrep -x auditd | head -1); grep CapEff /proc/$pid/status; capsh --decode=$(awk '/CapEff/{print $2}' /proc/$pid/status) | cut -c1-70
grep: /proc//status: No such file or directory
awk: fatal: cannot open file `/proc//status' for reading: No such file or directory
0x0000000000000000=
```

The last check is how you find out what a daemon really holds. `auditd`'s `CapEff` has the **same 40 bits as the shell** because both are root in the same sandbox. On a production host the same command on `nginx` or `chronyd` should show a **short** list; a long one on a service that does not need it is a finding.

**The rule, in one line:** give a program **one capability** if it needs one thing, use **`AmbientCapabilities`** in a unit file for a service (LX 04), keep `no_new_privs` on, and treat each setuid file as code that is on the list because someone had no better answer.

---

## 4. PAM: the stack that decides a login

### 4.1 What PAM is

**PAM** (Pluggable Authentication Modules) is how programs such as `login`, `sshd`, `sudo`, `su` and `passwd` **delegate the decision**. The program calls `libpam` with a **service name**; `libpam` reads **`/etc/pam.d/<service>`**, a list of lines `type control module arguments`, and runs the modules in order. Four **types**, which are four separate stacks:

| Type | Question | Typical modules |
| --- | --- | --- |
| **`auth`** | Is this really the user? (password, key, token) | `pam_unix`, `pam_faillock`, `pam_sss`, `pam_google_authenticator` |
| **`account`** | May this user log in now, from here? | `pam_nologin`, `pam_access`, `pam_time`, `pam_unix` (expiry) |
| **`password`** | Change the secret under policy | `pam_pwquality`, `pam_pwhistory`, `pam_unix` |
| **`session`** | Set up and tear down the login | `pam_limits`, `pam_env`, `pam_loginuid`, `pam_systemd`, `pam_mkhomedir` |

```text
--- 1. the files PAM reads
$ ls /etc/pam.d | tr '\n' ' '; echo
chfn chpasswd chsh common-account common-auth common-password common-session common-session-noninteractive login newusers other passwd runuser runuser-l sshd su su-l sudo sudo-i
$ grep -v '^#' /etc/pam.d/sshd | grep -v '^$' | cut -c1-90
@include common-auth
account    required     pam_nologin.so
@include common-account
session [success=ok ignore=ignore module_unknown=ignore default=bad]        pam_selinux.so
session    required     pam_loginuid.so
session    optional     pam_keyinit.so force revoke
@include common-session
session    optional     pam_motd.so  motd=/run/motd.dynamic
session    optional     pam_motd.so noupdate
session    optional     pam_mail.so standard noenv # [1]
session    required     pam_limits.so
session    required     pam_env.so # [1]
session    required     pam_env.so user_readenv=1 envfile=/etc/default/locale
session [success=ok ignore=ignore module_unknown=ignore default=bad]        pam_selinux.so
@include common-password
$ grep -v '^#' /etc/pam.d/common-auth | grep -v '^$' | cut -c1-110
auth    [success=1 default=ignore]      pam_unix.so nullok
auth    requisite                       pam_deny.so
auth    required                        pam_permit.so
$ ls /usr/lib/x86_64-linux-gnu/security | wc -l
47
```

The `sshd` service is mostly `@include` lines: **`common-auth`**, **`common-account`**, **`common-session`** and **`common-password`** are shared by every service, so one change applies to all. **`common-auth`** is the pattern to know: `auth [success=1 default=ignore] pam_unix.so nullok`, then `auth requisite pam_deny.so`, then `auth required pam_permit.so`. If `pam_unix` succeeds, **skip one line** (`success=1`), jump over the `deny`, and reach `permit`; if it fails, the next line, `pam_deny`, stops everything. **47 modules** are installed in `/usr/lib/x86_64-linux-gnu/security/`. Notice the `sshd` stack: **`pam_limits.so` is in `session`**, so a limit is applied to a login by a module, not by the shell, and **`pam_selinux`** appears twice (the "open" and "close" calls; skipped where there is no SELinux).

### 4.2 The control words, measured

The **control** column says what a module's result means for the stack: **`required`** (failure fails the stack, but **the remaining modules still run**, so an attacker cannot tell which one failed), **`requisite`** (failure ends the stack **at once**), **`sufficient`** (success ends the stack with success, **if no earlier `required` failed**), **`optional`** (counts only if it is the only module) and the general form **`[value=action ...]`**, where an action is `ok`, `ignore`, `bad`, `die`, `done`, a number that **skips that many lines**. To test without a login, **`pamtester SERVICE USER OPERATION`** runs a stack of a service file I wrote under a made up name.

```text
--- 2. the four stack types and the control words (a test service)
# A: only alice may pass; everyone else is denied
$ cat /etc/pam.d/lab_t
auth requisite pam_succeed_if.so user = alice
auth required pam_permit.so
$ pamtester lab_t alice authenticate -> success
$ pamtester lab_t bob authenticate -> FAIL: Authentication failure
# B: required keeps going, requisite stops; the module that stops is logged
$ cat /etc/pam.d/lab_t
auth required pam_echo.so first-module-ran
auth required pam_deny.so
auth required pam_echo.so after-deny-still-runs
$ pamtester lab_t alice authenticate
pamtester: Authentication failure
first-module-ran
after-deny-still-runs
$ cat /etc/pam.d/lab_t
auth requisite pam_deny.so
auth required pam_echo.so never-printed
$ pamtester lab_t alice authenticate
pamtester: Authentication failure
# C: sufficient ends the stack with success (if nothing required failed before it)
$ cat /etc/pam.d/lab_t
auth sufficient pam_succeed_if.so user = alice
auth required pam_deny.so
$ pamtester lab_t alice authenticate -> success
$ pamtester lab_t bob authenticate -> FAIL: Authentication failure
# D: the same module with [success=1 default=ignore] skips one line
$ cat /etc/pam.d/lab_t
auth [success=1 default=ignore] pam_succeed_if.so user = alice
auth requisite pam_deny.so
auth required pam_permit.so
$ pamtester lab_t alice authenticate -> success
$ pamtester lab_t bob authenticate -> FAIL: Authentication failure
```

Read the four experiments. **A**: `requisite pam_succeed_if.so user = alice` then `required pam_permit.so`. Alice passed, **bob failed** (`Authentication failure`): `pam_succeed_if` is the module for "a condition on the user" (`user`, `uid`, `ingroup`, `notingroup`, `shell`). **B** shows `required` against `requisite`. With `required`, `pam_deny` fails the stack but **both `pam_echo` messages were printed**, the one before and **the one after the deny** (the stack kept going); with `requisite pam_deny`, nothing after it ran: **`never-printed` never printed**. This is the reason `required` is used for checks that must not leak which step failed, and `requisite` for "stop before wasting time or logging". **C**: `sufficient` let alice through on the first line (the `deny` below was never reached) and bob fell to `pam_deny`. **D**: `[success=1 default=ignore]`, the form `common-auth` uses, skipped one line on success: alice passed, bob reached `requisite pam_deny` and failed. If you can read these four, you can read any stack.

A stack can also fail for the opposite reason: **modules are matched by order, and a misplaced `sufficient` above a `required` check bypasses the check**. The classic mistake is putting `auth sufficient pam_unix.so` before `auth required pam_faillock.so`, so a correct password skips the lockout bookkeeping.

### 4.3 Account and session stacks

```text
--- 3. account stack: who may log in, and when
$ cat /etc/pam.d/lab_t
account required pam_succeed_if.so user notingroup lab_deny
account required pam_permit.so
$ pamtester lab_t alice acct_mgmt -> success
$ pamtester lab_t bob acct_mgmt -> FAIL: Authentication failure
# pam_access: a rule file, first match wins
$ cat /tmp/lab.access
+ : alice : ALL
- : ALL : ALL
$ pamtester lab_t alice acct_mgmt -> success
$ pamtester lab_t bob acct_mgmt -> FAIL: Permission denied
# pam_time: allowed hours (alice is denied all day, bob has no rule)
$ cat /tmp/lab.time
lab_t;*;alice;!Al0000-2400
$ pamtester lab_t alice acct_mgmt -> FAIL: Permission denied
$ pamtester lab_t bob acct_mgmt -> success
```

`pamtester` has the operations `authenticate`, `acct_mgmt`, `open_session`, `close_session`, `chauthtok` and `setcred`; an `account` test uses **`acct_mgmt`**. Three account modules, each tested for alice and bob:

- **`pam_succeed_if.so user notingroup lab_deny`**: bob, a member of that group, was refused; alice was allowed. (The message `Authentication failure` is what `pamtester` prints for any module failure.)
- **`pam_access.so accessfile=...`**: rules `+ : alice : ALL` and `- : ALL : ALL`, **first match wins**: alice allowed, bob **`Permission denied`**. The real file is `/etc/security/access.conf`.
- **`pam_time.so conffile=...`**: a rule `service;ttys;users;times` with `!Al0000-2400` ("not allowed any day, all day"): alice **`Permission denied`**, bob (no rule) allowed. The real file is `/etc/security/time.conf`.

```text
--- 4. limits and sessions: pam_limits runs at session open
$ grep -v '^#' /etc/security/limits.conf | grep -v '^$' | head -3; ls /etc/security/limits.d
$ cat /etc/security/limits.d/lab.conf
alice hard nofile 64
alice hard nproc 40
$ runuser -u alice -- bash -c 'ulimit -n; ulimit -u'
64
40
$ su -s /bin/bash alice -c 'ulimit -n; ulimit -u'
64
40
$ cat /etc/pam.d/su | grep -E '^session' | cut -c1-80
session       required   pam_env.so readenv=1
session       required   pam_env.so readenv=1 envfile=/etc/default/locale
session    optional   pam_mail.so nopen
session    required   pam_limits.so
```

Resource limits are applied by **`pam_limits`** at session open, from `/etc/security/limits.conf` and `limits.d/`. A drop-in with **`alice hard nofile 64`** and **`alice hard nproc 40`** gave alice **64 open files and 40 processes** when she started a shell through **`runuser`** and again through **`su`** (both services include `pam_limits` in their `session` stack: the `su` stack is printed). **A limit set in `limits.conf` does not apply to a daemon started by `systemd`**, which sets limits itself with `LimitNOFILE=` (from memory, LX 04); this is the usual reason "I raised the limit and the service did not change".

```text
--- 5. pam_faillock and pam_pwquality are modules; what they ship
$ ls /usr/lib/x86_64-linux-gnu/security | grep -E 'faillock|pwquality|pwhistory|tty_audit|namespace|google|krb|sss|ldap' | tr '\n' ' '; echo
pam_faillock.so pam_namespace.so pam_pwhistory.so pam_pwquality.so pam_tty_audit.so
$ faillock --help 2>&1 | head -2
faillock: Unknown option: --help
Usage: faillock [--dir /path/to/tally-directory] [--user username] [--reset] [--legacy-output]
$ grep -v '^#' /etc/security/faillock.conf | grep -v '^$'; echo '(all commented out: defaults deny=3 unlock_time=600 fail_interval=900)'
(all commented out: defaults deny=3 unlock_time=600 fail_interval=900)
```

**`pam_faillock`** (lock after N failures), **`pam_pwquality`** (password strength), **`pam_pwhistory`** (no reuse), **`pam_namespace`** (per-user private `/tmp`) and **`pam_tty_audit`** (record keystrokes) all ship here. `faillock.conf` has **every line commented out**, so the defaults apply (from the manual page: **deny 3 failures within 900 s, unlock after 600 s**), and the module is **not in the stack**: it is off until a line in `common-auth` calls it, in the order `preauth` before `pam_unix`, `authfail` after, `authsucc` after success. I did not run it (limit 3). **Rules for editing PAM**: keep a **root session open** while you test (a broken `common-auth` locks everybody out, including `sudo`), test the service with `pamtester`, and change one line at a time.

---

## 5. SSH as a user

### 5.1 The model

**SSH** is an encrypted, authenticated channel over TCP port 22 that carries a **login shell, a remote command, file copies (`scp`, `sftp`) and forwarded TCP connections**, all multiplexed over one connection. A connection has two authentications and they are different things:

- **The server proves who it is to you** with a **host key**. The client compares the key with its **`~/.ssh/known_hosts`** (a host name and its public key). A key that is new is a question; **a key that changed is a refusal**.
- **You prove who you are to the server** with a **user key** (a key pair: the server holds the public half in **`~/.ssh/authorized_keys`**, you hold the private half and sign a challenge), a **certificate** (section 7), or a password.

Nothing secret crosses the wire in the key method: **the private key never leaves your machine**, and **`ssh-agent`** keeps it in one process so that you type its passphrase once.

### 5.2 Keys and fingerprints

```text
--- 1. keys and fingerprints
$ ssh-keygen -lf /tmp/lx12lab/k.pub
256 SHA256:K9GBY7hXm3cwxQbAehhjPZ3dTrJYjuoOMpBmXjRw0i0 lab-key (ED25519)
$ ssh-keygen -lf /tmp/lx12lab/hostA/hk.pub; ssh-keygen -lf /tmp/lx12lab/hostB/hk.pub
256 SHA256:cw45qyd17//Pq6n2a5GYW9hZ4TpKyy4fXgQG77Pto1c root@vm (ED25519)
256 SHA256:Rnvl+PvvGo9ZOu39LAPGC9up2EbnR5FXHR3W0rH+h4o root@vm (ED25519)
$ cat /home/alice/.ssh/authorized_keys | cut -c1-60
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFQrSadC+VkOz4cTo1CjJzkw
$ stat -c '%a %U %n' /home/alice /home/alice/.ssh /home/alice/.ssh/authorized_keys
750 alice /home/alice
700 alice /home/alice/.ssh
600 alice /home/alice/.ssh/authorized_keys
```

An **ed25519** key is **256 bits** and its fingerprint is a **`SHA256:`** hash of the public key, the thing to compare by eye or in a database. The private key file is `k`, the public key `k.pub` (a line `ssh-ed25519 AAAA... comment`). Three files and their modes decide whether the server accepts the key: **`/home/alice` `750`**, **`~/.ssh` `700`** and **`authorized_keys` `600`**, all owned by alice. **`sshd` with `StrictModes yes` (the default) refuses a key if the home directory, `.ssh` or the file is writable by anyone else**, and the error shows only on the **server**: the client says `Permission denied (publickey)`. Problem E reproduces this and shows the server's line.

### 5.3 First connection, `known_hosts`, and the changed key

```text
--- 2. first connect, known_hosts, and the changed key
$ rm -f /tmp/lx12lab/kh; ssh -F /tmp/lx12lab/cfg -o LogLevel=INFO hostA 'echo hello from $(id -un) on $(hostname)'
Warning: Permanently added '[127.0.0.1]:2201' (ED25519) to the list of known hosts.
hello from alice on vm
$ cut -c1-48 /tmp/lx12lab/kh
[127.0.0.1]:2201 ssh-ed25519 AAAAC3NzaC1lZDI1NTE
$ ssh-keygen -F '[127.0.0.1]:2201' -f /tmp/lx12lab/kh | tail -1 | cut -c1-48
[127.0.0.1]:2201 ssh-ed25519 AAAAC3NzaC1lZDI1NTE
$ ssh-keygen -q -t ed25519 -N '' -f /tmp/lx12lab/other; ssh-keygen -y -f /tmp/lx12lab/other | sed 's/^/[127.0.0.1]:2201 /' > /tmp/lx12lab/kh_bad
$ ssh -F /tmp/lx12lab/cfg -o UserKnownHostsFile=/tmp/lx12lab/kh_bad hostA true 2>&1 | sed 's/@/#/g' | sed -n '1,2p;4,5p;12,99p'; echo rc=${PIPESTATUS[0]}
###########################################################
#    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     #
IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!
  remove with:
  ssh-keygen -f '/tmp/lx12lab/kh_bad' -R '[127.0.0.1]:2201'
Host key for [127.0.0.1]:2201 has changed and you have requested strict checking.
Host key verification failed.
rc=255
```

On the first connect with `StrictHostKeyChecking accept-new` (a good setting for automation: **accept an unknown host, refuse a changed one**) the client wrote **`Permanently added '[127.0.0.1]:2201' (ED25519)`** and a line in `known_hosts`: the name in brackets with the port (a non standard port is part of the name), the algorithm and the key. `ssh-keygen -F NAME -f FILE` finds an entry; **`-R NAME` removes it** (after a legitimate rebuild); **`-H` hashes** the names so that the file does not list the machines you use. Then the dangerous case: a `known_hosts` entry for the same name with **another key** (a man in the middle, a rebuilt host, or a re-used IP). The client printed **`WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!`**, the fingerprint that the host sent, the exact `ssh-keygen -R` command, and **`Host key verification failed`, exit code 255**, and it **did not offer your credentials**. **Do not "fix" the warning by deleting `known_hosts`** before you know why the key changed. The answer is either "the host was reinstalled" (and you learn it from the host's owner) or an attack.

```text
--- 3. what sshd logged for that login
$ grep -E 'Accepted|Disconnected from user' /tmp/lx12lab/hostA/log | head -3 | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //; s/port [0-9]+/port N/; s/SHA256:.*/SHA256:.../'
Accepted key ED25519 SHA256:...
Accepted key ED25519 SHA256:...
Accepted publickey for alice from 127.0.0.1 port N ssh2: ED25519 SHA256:...
```

On the server, `LogLevel VERBOSE` records **`Accepted key ED25519 SHA256:...`** (my filter cut the rest of that line, which continues `found at /home/alice/.ssh/authorized_keys:1`: which key matched, and on which line) and then `Accepted publickey for alice from 127.0.0.1`. **At the default `INFO` level the matched key is not logged**, which makes "who used this key" unanswerable. Use `VERBOSE` on servers (section 6).

### 5.4 Restricting what a key may do

```text
--- 4. restrictions on a key in authorized_keys
$ cut -c1-100 /home/alice/.ssh/authorized_keys
command="echo only this runs; echo you asked: $SSH_ORIGINAL_COMMAND",restrict ssh-ed25519 AAAAC3NzaC
$ ssh -F /tmp/lx12lab/cfg hostA 'cat /etc/hostname'
only this runs
you asked: cat /etc/hostname
$ ssh -F /tmp/lx12lab/cfg hostA; echo rc=$?
only this runs
you asked:
rc=0
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA true; echo rc=$?
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
rc=255
$ grep -c 'Failed publickey' /tmp/lx12lab/hostA/log
1
```

An `authorized_keys` line can carry **options before the key**. With **`command="..."`** the server runs that command **whatever the client asked**, and puts the client's request in **`SSH_ORIGINAL_COMMAND`**: the client asked for `cat /etc/hostname` and got `only this runs` and `you asked: cat /etc/hostname`; an interactive `ssh hostA` printed the same and exited with 0, no shell. **`restrict`** turns off port forwarding, agent forwarding, X11 and `pty` allocation in one word, and you add back what is needed (`restrict,pty`). **`from="10.9.9.9"`** limits the source address: from `127.0.0.1` the same correct key got **`Permission denied`** and the server logged `Failed publickey` (**1** time). This is how a **backup key** or a **deploy key** is made safe: **the key can run `rsync --server` or one script and nothing else**, so a stolen key is a small loss. Other options: `no-agent-forwarding`, `permitopen="host:port"` (the only forward allowed), `principals=`, `expiry-time=`, and the `cert-authority` option (section 7).

### 5.5 Jump hosts

```text
--- 5. ProxyJump: hostB is reachable only through hostA
$ ssh -F /tmp/lx12lab/cfg hostB 'echo $SSH_CONNECTION | cut -d" " -f1,3'
127.0.0.1 127.0.0.2
$ grep -c 'Accepted publickey' /tmp/lx12lab/hostA/log /tmp/lx12lab/hostB/log
/tmp/lx12lab/hostA/log:1
/tmp/lx12lab/hostB/log:1
$ grep -E 'Accepted publickey' /tmp/lx12lab/hostA/log /tmp/lx12lab/hostB/log | sed -E 's#^/tmp/lx12lab/##; s/:[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ /: /; s/port [0-9]+/port N/; s/SHA256:.*/SHA256:.../'
hostA/log:Accepted publickey for alice from 127.0.0.1 port N ssh2: ED25519 SHA256:...
hostB/log:Accepted publickey for alice from 127.0.0.1 port N ssh2: ED25519 SHA256:...
```

Many networks put the machines behind a **bastion**. `ProxyJump` (`-J`) does it in one setting: the client connects to **`hostA`**, asks it for a TCP connection to `hostB:2202`, and runs the SSH session to `hostB` **through** that connection, end to end. **The bastion cannot read or alter the session**: the keys and encryption are between you and `hostB`. The evidence is in the two logs. `hostB` saw the client as **`127.0.0.1`** (`SSH_CONNECTION` = client address, client port, server address, server port: the first and third words printed were `127.0.0.1 127.0.0.2`), which is the address of **`hostA`**, and **each daemon logged one `Accepted publickey`**: the login to `hostA` and the login to `hostB`. The config that did it (the lab writes it in `-F` file `cfg`) is the `~/.ssh/config` pattern, with `Host *` for shared defaults and `ProxyJump hostA` under `hostB`. **The settings that applied to the first hop must also be set for the jump**: my first attempt put `StrictHostKeyChecking` and `UserKnownHostsFile` on the command line, which applied to `hostB` and **not** to the jump hop, and the jump failed with `Host key verification failed`. A config file with a `Host *` block applies to both.

### 5.6 The three kinds of forward

```text
--- 6. the three kinds of port forward
$ curl -s --max-time 2 http://127.0.0.1:8000/
page served by hostB-backend
# -L: my port 9001 -> through hostA -> 127.0.0.1:8000
$ ssh -F /tmp/lx12lab/cfg -f -N -L 127.0.0.1:9001:127.0.0.1:8000 hostA; sleep 0.5; curl -s --max-time 2 http://127.0.0.1:9001/
page served by hostB-backend
$ ss -ltnp 2>/dev/null | awk '$4 ~ /:9001$/ {print $1, $4, $6}' | sed -E 's/pid=[0-9]+/pid=N/; s/fd=[0-9]+/fd=N/'
LISTEN 127.0.0.1:9001 users:(("ssh",pid=N,fd=N))
# -R: the SERVER opens 9002 and sends it back to me
$ ssh -F /tmp/lx12lab/cfg -f -N -R 127.0.0.1:9002:127.0.0.1:8000 hostA; sleep 0.5; curl -s --max-time 2 http://127.0.0.1:9002/
page served by hostB-backend
# -D: a SOCKS5 proxy on 9003, the destination is chosen per connection
$ ssh -F /tmp/lx12lab/cfg -f -N -D 127.0.0.1:9003 hostA; sleep 0.5; curl -s --max-time 2 --socks5-hostname 127.0.0.1:9003 http://127.0.0.1:8000/
page served by hostB-backend
# forwarding disabled on the server
$ ssh -F /tmp/lx12lab/cfg -o ExitOnForwardFailure=yes -N -R 127.0.0.1:9005:127.0.0.1:8000 hostA 2>&1; echo rc=$?
Error: remote port forwarding failed for listen port 9005
rc=255
```

A **forward** carries other TCP traffic inside the SSH connection. Three kinds, which differ in **who listens**:

| Option | Who opens the listening port | The traffic goes | Use |
| --- | --- | --- | --- |
| **`-L`** `bind:port:host:hostport` | **the client** (your laptop) | client → server → `host:hostport` | reach a database or web page only the server can see |
| **`-R`** `bind:port:host:hostport` | **the server** | server → client → `host:hostport` | let a remote machine reach a service on your side (a callback, a demo) |
| **`-D`** `bind:port` | **the client**, as a **SOCKS5 proxy** | the destination is chosen per connection | browse "from" the server's network |

A small web server on `127.0.0.1:8000` served the page `page served by hostB-backend`. **`-L 9001:127.0.0.1:8000`** made `curl` to local port 9001 return that page, and `ss -ltnp` showed the **listening socket is owned by the `ssh` process**. **`-R 9002:...`** did the reverse: the **server** side opened 9002. **`-D 9003`** made a SOCKS proxy that `curl --socks5-hostname` used to reach the same page, with the destination named by the client at connect time (and, with `--socks5-hostname`, **resolved by the server**, which is what you want when only the remote DNS knows the name). The **bind address matters**: `-L 9001:...` with no `127.0.0.1` binds to the **loopback only** by default; `-L 0.0.0.0:9001:...` or `GatewayPorts` on a server for `-R` **opens the port to the network**, exposing a private service. The last experiment turned forwarding off on the server (`AllowTcpForwarding no`) and requested `-R` with `-o ExitOnForwardFailure=yes`: **`remote port forwarding failed for listen port 9005`, exit code 255**. Without that option, ssh would run on, with no forward, and say nothing important.

### 5.7 `ControlMaster` and `ssh-agent`

```text
--- 7. ControlMaster: pay for the handshake once
$ { time (for i in $(seq 20); do ssh -F /tmp/lx12lab/cfg hostA true; done); } 2>&1
4.074 s for 20 connections
$ { time (for i in $(seq 20); do ssh -F /tmp/lx12lab/cfg -o ControlMaster=auto -o ControlPath=/tmp/lx12lab/cm.sock -o ControlPersist=60 hostA true; done); } 2>&1
0.145 s for 20 connections
$ ssh -F /tmp/lx12lab/cfg -o ControlMaster=auto -o ControlPath=/tmp/lx12lab/cm.sock -o ControlPersist=60 -O check hostA 2>&1 | sed -E 's/pid=[0-9]+/pid=N/'
Master running (pid=N)
```

Every `ssh` command pays for a TCP connection, key exchange and authentication. **20 sequential connections took 4.074 s** (about **204 ms each**, most of it the key exchange and the process start). With `ControlMaster=auto`, `ControlPath=...` and `ControlPersist=60`, the first command starts a **master** that stays for 60 s and **the next ones use its socket**: **20 connections took 0.145 s** (**7 ms each**, about **28 times** faster), and `-O check` reported `Master running`. This is what makes `git` over SSH, `rsync` loops and Ansible fast. Two cautions: **anyone who can read the control socket can use the open session**, so the socket must be in a private directory (`~/.ssh/cm/`, mode 700), and the master stays after the first command ended (`-O exit` stops it).

```text
--- 8. ssh-agent: the key stays in one process
$ ssh-add -q /tmp/lx12lab/k; ssh-add -l | cut -d' ' -f1,3,4
256 lab-key (ED25519)
$ ssh -F /tmp/lx12lab/cfg -o IdentityFile=none -o IdentitiesOnly=no hostA 'echo logged in with the agent key'
logged in with the agent key
$ ssh-add -D 2>&1; ssh-add -l 2>&1 | head -1
All identities removed.
The agent has no identities.
```

**`ssh-agent`** holds decrypted private keys in memory and answers sign requests over a Unix socket (`SSH_AUTH_SOCK`). `ssh-add` loaded the key and `ssh-add -l` listed it; a login with `IdentityFile=none` still worked, using only the agent; `ssh-add -D` removed it. **Agent forwarding (`-A` or `ForwardAgent`) lets a remote host use your agent**: anyone who is root on that host can use your keys while you are connected. **Use `ProxyJump` instead of agent forwarding through a bastion** (the example above needs no forwarded agent), and never forward to hosts you do not control.


---

## 6. `sshd` for an administrator: configuration and hardening

### 6.1 What the daemon decides when you configure nothing

`sshd` reads **`/etc/ssh/sshd_config`** (and, on current packages, files in `/etc/ssh/sshd_config.d/`), and **`sshd -T`** prints the **effective** configuration after defaults, includes and `Match` blocks are applied. That is the command to trust: the file shows what you wrote, `sshd -T` shows what the daemon will do. Start from the defaults of this build, using a file that holds only the host key:

```text
--- 1. what the daemon decides when you configure nothing
$ sshd -T -f /tmp/lx12lab/empty.conf | wc -l
90
$ sshd -T -f /tmp/lx12lab/empty.conf | grep -E '^(permitrootlogin|passwordauthentication|pubkeyauthentication|kbdinteractiveauthentication|permitemptypasswords|x11forwarding|allowtcpforwarding|allowagentforwarding|maxauthtries|logingracetime|clientaliveinterval|usepam|permituserenvironment|printmotd|maxsessions|maxstartups|loglevel|gatewayports|permittunnel|strictmodes|ignorerhosts)\b'
usepam no
logingracetime 120
maxauthtries 6
maxsessions 10
clientaliveinterval 0
permitrootlogin without-password
ignorerhosts yes
pubkeyauthentication yes
passwordauthentication yes
kbdinteractiveauthentication yes
printmotd yes
x11forwarding no
strictmodes yes
permitemptypasswords no
gatewayports no
allowtcpforwarding yes
allowagentforwarding yes
loglevel INFO
maxstartups 10:30:100
permittunnel no
permituserenvironment no
```

**90 effective settings** with nothing configured. The ones that matter: **`passwordauthentication yes`** and **`kbdinteractiveauthentication yes`** (passwords are accepted by default), **`permitrootlogin without-password`** (the old name of `prohibit-password`: root may log in with a key), `maxauthtries 6`, **`logingracetime 120`** (an unauthenticated connection may sit for two minutes), `maxstartups 10:30:100` (start dropping new unauthenticated connections at 10, drop 30 percent, all at 100), **`allowtcpforwarding yes`**, `allowagentforwarding yes`, `x11forwarding no`, `permitemptypasswords no`, and `loglevel INFO`. Note **`usepam no`**: this is the compiled default of this build, because my file has no `UsePAM`. **The Ubuntu package's own `sshd_config` sets `UsePAM yes`** (from memory), and with PAM the `account` and `session` stacks of section 4 apply, which is what you want on a real host. The lab sets it to `no` for the reason in limit 3.

### 6.2 A hardened file, and what changed

```text
--- 2. a hardened file, and what changed against the defaults
$ sshd -t -f /tmp/lx12lab/hard.conf; echo syntax-rc=$?
syntax-rc=0
$ diff /tmp/lx12lab/d.txt /tmp/lx12lab/h.txt | grep '^[<>]' | sort -k2,2 -s | head -40
< allowagentforwarding yes
> allowagentforwarding no
< allowtcpforwarding yes
> allowtcpforwarding no
> allowusers alice
< authenticationmethods any
> authenticationmethods publickey
< clientaliveinterval 0
> clientaliveinterval 30
< disableforwarding no
> disableforwarding yes
< kbdinteractiveauthentication yes
> kbdinteractiveauthentication no
< logingracetime 120
> logingracetime 20
< loglevel INFO
> loglevel VERBOSE
< maxauthtries 6
> maxauthtries 3
< maxsessions 10
> maxsessions 4
< maxstartups 10:30:100
> maxstartups 10:30:60
< passwordauthentication yes
> passwordauthentication no
< permitrootlogin without-password
> permitrootlogin no
```

The hardened file (printed in the script as `hard.conf`) sets **`PermitRootLogin no`, `PasswordAuthentication no`, `KbdInteractiveAuthentication no`, `AuthenticationMethods publickey`, `AllowUsers alice`, `MaxAuthTries 3`, `LoginGraceTime 20`, `MaxSessions 4`, `MaxStartups 10:30:60`, `ClientAliveInterval 30`, `X11Forwarding no`, `AllowAgentForwarding no`, `AllowTcpForwarding no`, `DisableForwarding yes` and `LogLevel VERBOSE`**. `sshd -t` checked the syntax (exit 0), and a **`diff` of the two `sshd -T` outputs** lists **exactly 14 settings that changed**. That diff is the method: **write the policy, diff the effective configuration against the defaults, and read every line of the diff**. A reason for each:

- **Keys only, one method.** `PasswordAuthentication no` and **`KbdInteractiveAuthentication no`**: with PAM, passwords also arrive through keyboard-interactive, so turning off only the first leaves the door open (the live test below shows it). `AuthenticationMethods publickey` states the policy as one line; **`publickey,keyboard-interactive`** (a key and then a one time code) is the two-factor form.
- **`PermitRootLogin no`**: log in as a person and `sudo`, so that every action has a name (section 2) and the root account is not an internet-exposed target.
- **`AllowUsers` or `AllowGroups`**: a **deny by default list**. Unlisted users (a service account with a shell, a forgotten test user) cannot log in even with a valid key.
- **Short grace and few tries**: `LoginGraceTime 20`, `MaxAuthTries 3` and `MaxStartups 10:30:60` bound the work an unauthenticated client can make the server do. **Rate-limit at the firewall too** (section 8).
- **`ClientAliveInterval 30` with `ClientAliveCountMax 3`**: the server probes an idle session and **drops it after about 90 s of silence**, which removes half-dead sessions. It is not an idle timeout (a person at the keyboard answers the probe).
- **Forwarding off unless needed** (`DisableForwarding yes` turns off TCP, agent, X11 and tunnel forwarding in one word): a jump host is the one place where `AllowTcpForwarding yes` is required, and there it is limited per user with a `Match` block or `PermitOpen`.
- **`LogLevel VERBOSE`**: the key fingerprint of every login (section 5.3).

```text
--- 3. a typo is an error, but only if you check
$ sshd -t -f /tmp/lx12lab/typo.conf; echo rc=$?
/tmp/lx12lab/typo.conf: line 2: Bad configuration option: PasswordAuthenticaton
/tmp/lx12lab/typo.conf: terminating, 1 bad configuration options
rc=255
$ sshd -t -f /tmp/lx12lab/match.conf; echo rc=$?
/tmp/lx12lab/match.conf line 4: Directive 'HostKey' is not allowed within a Match block
rc=255
(HostKey after a Match line belongs to the Match block; the fix is to put global options first)
```

**A typo is an error only if you check.** `PasswordAuthenticaton` (missing `i`) gave **`Bad configuration option`** and exit code 255 under `sshd -t`; **a running `sshd` that is restarted with a bad file does not come back**, so `sshd -t` must precede every `reload`. The second case is subtler and common: `HostKey` appeared **after a `Match` line** and `sshd -t` said **`Directive 'HostKey' is not allowed within a Match block`**. Everything after a `Match` line up to the next one (or the end) belongs to that block; **put global options first and `Match` blocks last**, and keep a `Match all` out of habit if you need to close one.

```text
--- 4. Match blocks, evaluated for one connection
$ sshd -T -f /tmp/lx12lab/m.conf -C user=alice,addr=127.0.0.1,host=x | grep -E '^allowtcpforwarding'
allowtcpforwarding yes
$ sshd -T -f /tmp/lx12lab/m.conf -C user=alice,addr=10.0.0.5,host=x | grep -E '^allowtcpforwarding'
allowtcpforwarding no
$ sshd -T -f /tmp/lx12lab/m.conf -C user=bob,addr=127.0.0.1,host=x | grep -E '^allowtcpforwarding'
allowtcpforwarding no
```

A `Match` block changes settings for a **user, group, address or host**; **`sshd -T -C user=...,addr=...`** evaluates the file for one imagined connection. The file forbids forwarding by default and allows it for `alice` **from `127.0.0.1` only**: `alice` from `127.0.0.1` printed **`allowtcpforwarding yes`**, `alice` from `10.0.0.5` printed **`no`**, and `bob` printed **`no`**. Test each exception with `-C` before reloading; **the order matters** (the first value set for a keyword wins, unlike most config files, from memory).

```text
--- 5. algorithms: what the build offers, what a policy removes
$ ssh -Q kex | wc -l; ssh -Q cipher | wc -l; ssh -Q mac | wc -l; ssh -Q key | wc -l
13
10
16
16
$ sshd -T -f /tmp/lx12lab/empty.conf | grep -E '^(kexalgorithms|ciphers|macs|hostkeyalgorithms)' | cut -c1-110
ciphers chacha20-poly1305@openssh.com,aes128-ctr,aes192-ctr,aes256-ctr,aes128-gcm@openssh.com,aes256-gcm@opens
macs umac-64-etm@openssh.com,umac-128-etm@openssh.com,hmac-sha2-256-etm@openssh.com,hmac-sha2-512-etm@openssh.
kexalgorithms sntrup761x25519-sha512@openssh.com,curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nist
hostkeyalgorithms ssh-ed25519-cert-v01@openssh.com,ecdsa-sha2-nistp256-cert-v01@openssh.com,ecdsa-sha2-nistp38
$ for f in empty alg; do echo $f: $(cnt /tmp/lx12lab/$f.conf kexalgorithms)$(cnt /tmp/lx12lab/$f.conf ciphers)$(cnt /tmp/lx12lab/$f.conf macs); done
empty: kexalgorithms 10 ciphers 6 macs 10
alg: kexalgorithms 10 ciphers 6 macs 8
$ diff <(sshd -T -f /tmp/lx12lab/empty.conf | grep ^macs | tr ', ' '\n\n') <(sshd -T -f /tmp/lx12lab/alg.conf | grep ^macs | tr ', ' '\n\n') | grep '^<'
< hmac-sha1-etm@openssh.com
< hmac-sha1
```

This build offers **13 key exchange methods, 10 ciphers, 16 MACs and 16 key types** (`ssh -Q`), and the **effective defaults are shorter**: 10, 6 and 10, because the weakest are compiled in but off. A policy edits the list with a prefix: **`-` removes**, `+` appends, `^` puts first. `MACs -*-sha1*,-hmac-md5*` removed **`hmac-sha1` and `hmac-sha1-etm`** (10 to 8). The kex and cipher edits removed names that were not in the default list and changed nothing, which is the right result: **a hardening line that matches nothing today still protects you after an upgrade that would have added the weak name**. The first kex algorithm in the default list is **`sntrup761x25519-sha512`**, a hybrid of a post quantum and a classical method, so a recorded session is not readable later by a quantum computer (from memory). Follow a published baseline (Mozilla's, the CIS benchmark) and **retest a legacy client after each change**: an old appliance that only speaks `ssh-rsa` with SHA-1 is the usual casualty.

```text
--- 6. a hardened daemon refuses what it should (live)
$ ssh -F /tmp/lx12lab/cfg hostA 'echo key login works'
key login works
$ ssh -F /tmp/lx12lab/cfg -o User=root hostA true 2>&1 | tail -1; echo rc=${PIPESTATUS[0]}
root@127.0.0.1: Permission denied (publickey,keyboard-interactive).
rc=255
$ ssh -F /tmp/lx12lab/cfg -o User=bob -o BatchMode=yes hostA true 2>&1 | tail -1
bob@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ ssh -F /tmp/lx12lab/cfg -o PubkeyAuthentication=no -o PasswordAuthentication=yes -o BatchMode=yes hostA true 2>&1 | tail -1
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ grep -E 'not allowed|Invalid user|ROOT LOGIN' /tmp/lx12lab/hostA/log | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //; s/port [0-9]+/port N/' | sort | uniq -c | cut -c1-110
      1 Invalid user bob from 127.0.0.1 port N
      1 User root from 127.0.0.1 not allowed because not listed in AllowUsers
```

A **live** daemon with `AllowUsers alice` and `PermitRootLogin no`: **the key login worked**; `root` and `bob` and a password attempt were all refused with the **same generic message** `Permission denied (publickey,keyboard-interactive)`, so the client learns **nothing** about which rule applied (a correct design). **The server log says why**: `Invalid user bob` and `User root from 127.0.0.1 not allowed because not listed in AllowUsers`. Read the **server** log, with `LogLevel VERBOSE`, for any "why can't X log in" question. Note the message **still offers `keyboard-interactive`**: this live daemon sets only `PasswordAuthentication no`, which proves the point above, and with `UsePAM yes` that second door leads to the password prompt.

```text
--- 7. host keys: what you must back up and what you may regenerate
$ ls /tmp/lx12lab/hostA; ssh-keygen -lf /tmp/lx12lab/hostA/hk.pub | cut -d' ' -f1,4
hk
hk.pub
log
sshd_config
256 (ED25519)
$ ls /etc/ssh 2>&1 | head -3; dpkg -L openssh-server | grep -c .
moduli
ssh_config
ssh_config.d
49
$ ssh-keygen -t rsa -b 3072 -N '' -q -f /tmp/lx12lab/rsa3072 && ssh-keygen -lf /tmp/lx12lab/rsa3072.pub | cut -d' ' -f1,4; ls -l /tmp/lx12lab/rsa3072.pub /tmp/lx12lab/hostA/hk.pub | awk '{print $5, $9}' | sed 's#/tmp/lx12lab/##'
3072 (RSA)
89 hostA/hk.pub
561 rsa3072.pub
```

**Host keys** are the server's identity. `ssh-keygen -A` creates one per algorithm; ed25519 is **89 bytes** as a public key line and RSA 3072 is **561 bytes**. **Back them up with the machine's configuration** and **restore them on a rebuilt host**: a new key makes every client print the `HOST IDENTIFICATION HAS CHANGED` warning of section 5.3. In a fleet, a **host certificate** (section 7) removes the problem.

---

## 7. SSH certificates: trust one key instead of many

A host with a hundred users has a hundred `authorized_keys` files and no quick answer to "who may log in, until when, as what". A **certificate** moves the decision to one place: a **certificate authority (CA)** key signs a user's public key together with **who the user may be (principals), a validity period, a serial number and restrictions**. The server trusts **the CA** (`TrustedUserCAKeys`), not the individual key.

```text
--- 1. one CA key instead of a list of user keys
$ ssh-keygen -q -t ed25519 -N '' -C lab-ca -f /tmp/lx12lab/ca; ssh-keygen -lf /tmp/lx12lab/ca.pub | cut -d' ' -f1,3,4
256 lab-ca (ED25519)
$ ssh-keygen -q -t ed25519 -N '' -C prince-laptop -f /tmp/lx12lab/k
$ ssh-keygen -q -s /tmp/lx12lab/ca -I prince-2026-11 -n alice -V +1h -z 1001 /tmp/lx12lab/k.pub; ls /tmp/lx12lab/k-cert.pub
/tmp/lx12lab/k-cert.pub
$ ssh-keygen -Lf /tmp/lx12lab/k-cert.pub | sed -n '1,14p'
/tmp/lx12lab/k-cert.pub:
        Type: ssh-ed25519-cert-v01@openssh.com user certificate
        Public key: ED25519-CERT SHA256:/mbM6AyfAoqZVV0ZIi1T2jW6hYtTjhTMrlVTvwjEVqA
        Signing CA: ED25519 SHA256:sk5bfDZm6tmdzIzH8I1R8Va/MwdicJ2+6vTY+7oNhCg (using ssh-ed25519)
        Key ID: "prince-2026-11"
        Serial: 1001
        Valid: from 2026-11-18T02:55:00 to 2026-11-18T03:56:17
        Principals:
                alice
        Critical Options: (none)
        Extensions:
                permit-X11-forwarding
                permit-agent-forwarding
                permit-port-forwarding
```

`ssh-keygen -s CA -I ID -n alice -V +1h -z 1001 KEY.pub` signed the key for **principal `alice`**, **one hour**, **serial 1001**, and `-L` printed it: type `user certificate`, the **Signing CA** fingerprint, `Key ID "prince-2026-11"` (what appears in the log), the principals, **`Critical Options: (none)`** (restrictions that the server must refuse if it does not know them) and **Extensions** (`permit-agent-forwarding`, `permit-port-forwarding`, ...: privileges that can be removed). The `Valid:` times differ on every run, so do not look for them in the text.

```text
--- 2. the server trusts the CA, not the key
$ ls -l /home/alice/.ssh | head -3
total 0
$ ssh -F /tmp/lx12lab/cfg hostA 'echo logged in by certificate'
logged in by certificate
$ grep -E 'Accepted certificate|Certificate invalid' /tmp/lx12lab/hostA/log | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //; s/port [0-9]+/port N/; s/SHA256:[^ ]*/SHA256:.../g' | cut -c1-140
Accepted certificate ID "prince-2026-11" (serial 1001) signed by ED25519 CA SHA256:... via /tmp/lx12lab/ca_user.pub
Accepted certificate ID "prince-2026-11" (serial 1001) signed by ED25519 CA SHA256:... via /tmp/lx12lab/ca_user.pub
```

The server's `/home/alice/.ssh/` is **empty** (`total 0`): there is **no `authorized_keys`**, and the login still worked because the daemon trusted the CA. The log names **`Accepted certificate ID "prince-2026-11" (serial 1001) signed by ED25519 CA SHA256:... via /tmp/lx12lab/ca_user.pub`**: **the ID and the serial go into the log**, so "who logged in" is answered by a name you chose when signing, and a stolen certificate is identified by its serial.

```text
--- 3. what a certificate can refuse: principals, time, source address
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA true 2>&1 | tail -1
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ grep -E 'principal' /tmp/lx12lab/hostA/log | tail -1 | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //; s/port [0-9]+/port N/; s/SHA256:[^ ]*/SHA256:.../g' | cut -c1-150
Failed publickey for alice from 127.0.0.1 port N ssh2: ED25519-CERT SHA256:... ID wrong-principal (serial 0) CA ED25519 SHA256:...
$ ssh-keygen -Lf /tmp/lx12lab/k-cert.pub | grep Valid
        Valid: from 2026-11-18T00:56:17 to 2026-11-18T01:56:17
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA true 2>&1 | tail -1
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ grep -E 'expired|Certificate invalid' /tmp/lx12lab/hostA/log | tail -1 | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //; s/port [0-9]+/port N/; s/SHA256:[^ ]*/SHA256:.../g' | cut -c1-150
Failed publickey for alice from 127.0.0.1 port N ssh2: ED25519-CERT SHA256:... ID expired (serial 0) CA ED25519 SHA256:...
$ ssh-keygen -Lf /tmp/lx12lab/k-cert.pub | sed -n '/Critical/,/Extensions/p' | head -4
        Critical Options:
                force-command echo pinned by the certificate
                source-address 10.9.9.0/24
        Extensions:
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA true 2>&1 | tail -1
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ ssh -F /tmp/lx12lab/cfg hostA 'echo what I asked for'
pinned by the certificate
```

Four refusals, all **`Permission denied`** to the client, all logged on the server with the certificate's ID. A certificate for the **wrong principal** (`bob`) logged in as `alice`: refused (with no `AuthorizedPrincipalsFile`, the principal must equal the user name; a file can map one principal such as `ops` to several accounts). An **expired** certificate (validity two hours ago to one hour ago): refused. A **`source-address=10.9.9.0/24`** certificate used from `127.0.0.1`: refused. And **`force-command`** is a critical option the **signer** controls: with a good certificate that carried `force-command='echo pinned by the certificate'`, the client asked for `echo what I asked for` and the server ran **`pinned by the certificate`**. **Short lived certificates (minutes to hours, issued after the user authenticates to a single sign-on) replace long lived keys**, so there is nothing to steal after a day and nothing to revoke.

```text
--- 4. revoking a certificate: a KRL
$ ssh -F /tmp/lx12lab/cfg hostA true; echo rc-before=$?
rc-before=0
$ ssh-keygen -k -f /tmp/lx12lab/revoked.krl -s /tmp/lx12lab/ca.pub <(echo 'serial: 4242') 2>&1 | head -2; ls -l /tmp/lx12lab/revoked.krl | awk '{print $5, "bytes"}'
Revoking from /dev/fd/63
121 bytes
$ ssh-keygen -Qf /tmp/lx12lab/revoked.krl /tmp/lx12lab/k-cert.pub; echo rc=$?
/tmp/lx12lab/k-cert.pub (prince-laptop): REVOKED
rc=1
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA true 2>&1 | tail -1
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ grep 'Failed publickey' /tmp/lx12lab/hostA/log | tail -1 | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //; s/port [0-9]+/port N/; s/SHA256:[^ ]*/SHA256:.../g' | cut -c1-140
Failed publickey for alice from 127.0.0.1 port N ssh2: ED25519 SHA256:...
```

When you must revoke early, a **KRL** (key revocation list) names serials, key ids or keys. `ssh-keygen -k -f revoked.krl -s ca.pub FILE` with the line **`serial: 4242`** wrote a **121 byte** list; `ssh-keygen -Qf revoked.krl cert` answered **`REVOKED`** (exit 1) for the certificate with that serial; and a daemon with **`RevokedKeys /path/revoked.krl`** refused it. The log line is an ordinary `Failed publickey ... ID to-be-pulled (serial 4242)`: **a revoked certificate looks like any other failure to the client**, and to the log it carries the ID.

```text
--- 5. host certificates: clients trust the CA, so no first-connect question
$ ls /tmp/lx12lab/hostA; ssh-keygen -Lf /tmp/lx12lab/hostA/hk-cert.pub | sed -n '1,6p'
hk
hk-cert.pub
hk.pub
log
sshd_config
/tmp/lx12lab/hostA/hk-cert.pub:
        Type: ssh-ed25519-cert-v01@openssh.com host certificate
        Public key: ED25519-CERT SHA256:1s4WfD6DHCHvY2xsVkXwnoq7b3XCeJar8guW9TSilNI
        Signing CA: ED25519 SHA256:sk5bfDZm6tmdzIzH8I1R8Va/MwdicJ2+6vTY+7oNhCg (using ssh-ed25519)
        Key ID: "hostA"
        Serial: 0
$ cut -c1-60 /tmp/lx12lab/kh_ca
@cert-authority 127.0.0.1 ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AA
$ ssh -F /tmp/lx12lab/cfg -o UserKnownHostsFile=/tmp/lx12lab/kh_ca -o StrictHostKeyChecking=yes -o LogLevel=INFO hostA 'echo no prompt, no known_hosts entry for the host key'
no prompt, no known_hosts entry for the host key
$ wc -c < /tmp/lx12lab/kh_ca
107
```

The other direction: **host certificates** (`-h`). The CA signed the host's key for the name `127.0.0.1`, the daemon was given **`HostCertificate`**, and the client's `known_hosts` held **one line**: `@cert-authority 127.0.0.1 ssh-ed25519 AAAA...` (107 bytes) and no entry for the host's key. With `StrictHostKeyChecking yes`, which refuses any host not already known, **the login worked**, with no first-connect question, because the host key was signed by a trusted CA. A new server in a fleet is trusted the moment it has a certificate, and `HOST IDENTIFICATION HAS CHANGED` stops being a daily event.

---

## 8. The firewall: `nftables`

### 8.1 The model

**`netfilter`** is the kernel's packet filtering framework (LX 08). Its front end today is **`nftables`** (`nft`), the successor of `iptables`, `ip6tables`, `arptables` and `ebtables`. Four nouns:

- A **table** is a container for one **family** (`ip`, `ip6`, `inet` for both, `arp`, `bridge`, `netdev`).
- A **chain** is a list of rules. A **base chain** is attached to a **hook** in the kernel's packet path: `prerouting` (before the routing decision), **`input`** (to this host), `forward` (through it), **`output`** (from this host), `postrouting`; it has a **type** (`filter`, `nat`, `route`), a **priority** (lower runs first) and a **policy** (`accept` or `drop` when no rule decides). A regular chain is only a jump target.
- A **rule** is matches then a **verdict**: `accept`, `drop`, `reject`, `jump`, `return`, `counter`, `log`, `dnat`, `snat`. Rules run in order; the first terminal verdict wins.
- A **set** is a named collection of addresses, ports or pairs (a hash or interval tree), so that a rule "source in the allowed list" is one lookup, not a hundred rules.

**`ct state established,related accept`** is the rule that makes a stateful firewall: connection tracking (`conntrack`) records every flow, so the reply packets of a connection that you allowed are allowed without matching them again. Put it **first** (it is also the fastest path).

```text
--- 1. tables, chains, rules, hooks
$ nft --version
nftables v1.0.9 (Old Doc Yak #3)
$ nft list ruleset | wc -l
0
$ nft list table inet lab
table inet lab {
        set allowed4 {
                type ipv4_addr
                elements = { 10.1.2.3, 127.0.0.1 }
        }

        chain input {
                type filter hook input priority filter; policy accept;
                ct state established,related accept
                iif "lo" tcp dport 2201 ip saddr @allowed4 counter packets 0 bytes 0 accept
                tcp dport 2201 counter packets 0 bytes 0 drop
                tcp dport 2202 counter packets 0 bytes 0 reject with tcp reset
        }
}
```

The ruleset started **empty** (0 lines). The file created a table `inet lab` with a **set `allowed4`** (`127.0.0.1` and `10.1.2.3`), a base chain **`input`** at hook `input`, priority 0 (printed as `priority filter`), **policy accept**, and four rules: accept established, **accept loopback traffic to port 2201 from a source in the set** (with a counter), **drop the rest of port 2201**, and **reject port 2202 with a TCP reset**. A **counter** on a rule is cheap and answers "did this rule match" without logging.

### 8.2 What the rules do to real connections

```text
--- 2. what the rules do to real connections
$ ssh -F /tmp/lx12lab/cfg -o ConnectTimeout=3 hostA 'echo allowed source gets in'
allowed source gets in
$ nft list chain inet lab input | grep -E 'dport 2201' | sed 's/ip saddr @allowed4 //'
                iif "lo" tcp dport 2201 counter packets 2 bytes 120 accept
                tcp dport 2201 counter packets 0 bytes 0 drop
# remove 127.0.0.1 from the set: the same command now times out
$ nft delete element inet lab allowed4 '{ 127.0.0.1 }'
$ ssh -F /tmp/lx12lab/cfg -o ConnectTimeout=3 -o BatchMode=yes hostA true; echo rc=$?
ssh: connect to host 127.0.0.1 port 2201: Connection timed out
rc=255
$ nft list chain inet lab input | grep -E 'dport 2201' | sed 's/ip saddr @allowed4 //'
                iif "lo" tcp dport 2201 counter packets 2 bytes 120 accept
                tcp dport 2201 counter packets 3 bytes 180 drop
# reject sends an answer; drop sends nothing
$ { time bash -c 'exec 3<>/dev/tcp/127.0.0.2/2202'; } 2>&1 | sed '/connect:/d; s#^bash: line 1: /dev/tcp/127.0.0.2/2202: ##'
Connection refused
0.002 s
$ { time timeout 3 bash -c 'exec 3<>/dev/tcp/127.0.0.1/2201'; } 2>&1
3.002 s
```

From an allowed source the login worked and the **accept counter read 2 packets, 120 bytes**: the two SYNs that reached that rule, one from the lab's own readiness probe and one from `ssh` (every later packet of those connections took the established rule above it). **`nft delete element inet lab allowed4 '{ 127.0.0.1 }'`** removed one address from the set: **no rule changed**, and the same command now **timed out** (`Connection timed out`, 255) and the **drop counter rose to 3 packets** (the SYN and its two retransmissions). That is the operational value of a set: **a block list or an allow list is data, changed at run time with no reload and no gap**.

The last two lines compare the verdicts. **`reject with tcp reset`** answered at once: a **`Connection refused` after 0.002 s**. **`drop`** answered with nothing: the client **waited the full 3.002 s of my `timeout`**, and a real client waits for its retransmission timer, about two minutes in total (from memory, LX 08). So: **`reject` for the inside** (the user gets an error immediately and a service that is not there is quickly found), **`drop` for the outside** (an attacker's scan is slowed and learns nothing). A `drop` of everything on the inside is the usual cause of "the connection hangs" bug reports.

### 8.3 Replace a ruleset in one step, and check it first

```text
--- 3. replacing a ruleset atomically
$ nft -c -f /tmp/lx12lab/new.nft; echo check-rc=$?
check-rc=0
$ nft -f /tmp/lx12lab/new.nft; nft list chain inet lab input
table inet lab {
        chain input {
                type filter hook input priority filter; policy drop;
                ct state established,related accept
                iif "lo" accept
        }
}
$ ssh -F /tmp/lx12lab/cfg -o ConnectTimeout=3 hostA 'echo loopback accepted by the new default-drop policy'
loopback accepted by the new default-drop policy
$ nft -c -f /tmp/lx12lab/bad.nft; echo check-rc=$?
/tmp/lx12lab/bad.nft:1:84-87: Error: syntax error, unexpected string
table inet lab { chain input { type filter hook input priority 0; policy drop; tcp dprt 22 accept } }
                                                                                   ^^^^
/tmp/lx12lab/bad.nft:2:1-1: Error: syntax error, unexpected end of file
table inet lab { chain input { type filter hook input priority 0; policy drop; tcp dprt 22 accept } }
^
check-rc=1
```

`nft -c -f FILE` **checks without applying** (exit 0). The new file begins with **`flush table inet lab`** and then defines the table again with **`policy drop`**, accept established, accept the loopback interface: **`nft -f` applies a file as one transaction**, so there is **no moment when the table is half old and half new**, the property that makes it safe to reload a firewall over the SSH session you are using (as long as the new rules let that session through: the established rule did here, and the login on loopback still worked). A typo (`tcp dprt 22`) was **rejected with a caret under it and exit 1**, before anything changed. The habit for a remote host: **check with `-c`, apply with a timer that reverts** (`( sleep 120; nft -f good.nft ) &` or the `at` command) **and cancel the timer after you reconnect**.

### 8.4 Rate limits, counters and the cost of waiting

```text
--- 4. counters, limits and logging as data
$ nft list chain inet lab input
table inet lab {
        chain input {
                type filter hook input priority filter; policy drop;
                ct state established,related accept
                tcp dport 2202 ct state new limit rate 5/second burst 5 packets counter packets 0 bytes 0 accept comment "ssh new connections, rate limited"
                tcp dport 2202 counter packets 0 bytes 0 drop comment "over the limit"
        }
}
$ bash /tmp/lx12lab/burst.sh
      8 connected
     22 timed-out
$ nft list chain inet lab input | grep -E 'counter packets' | sed -E 's/bytes [0-9]+ //; s/ comment.*//'
                tcp dport 2202 ct state new limit rate 5/second burst 5 packets counter packets 8 accept
                tcp dport 2202 counter packets 25 drop
```

The rule **`tcp dport 2202 ct state new limit rate 5/second burst 5 packets accept`** followed by **`... drop`** is a **token bucket** on new connections: 5 packets of burst, refilled at 5 per second. Thirty connection attempts fired **at the same instant** gave **8 connected and 22 timed out** (the burst of 5 plus the tokens that arrived during the burst; an earlier run of the same script gave 7 and 23), and the counters read **8 accepted and 25 dropped** (the dropped ones include the retransmitted SYNs of clients that were waiting). **An earlier version of this experiment ran the thirty attempts one after another, and 25 of 30 got through**, because each timed out attempt waited a second, which refilled five tokens: **a limiter only limits a client that arrives faster than the refill rate**, and a slow, patient scan passes. `comment "..."` stores a note in the rule, and `nft -a` shows each rule's **handle** (used by `nft delete rule ... handle N`).

### 8.5 NAT, and `iptables`

```text
--- 5. NAT is the same tool, a different table type
$ nft list table ip nat_lab | grep -v '^$'
table ip nat_lab {
        chain pre {
                type nat hook prerouting priority dstnat; policy accept;
        }
        chain out {
                type nat hook output priority dstnat; policy accept;
                ip daddr 127.0.0.9 tcp dport 7777 dnat to 127.0.0.2:2202
        }
}
$ ssh -F /tmp/lx12lab/cfg -o ConnectTimeout=3 -o ProxyJump=none -p 7777 -o HostName=127.0.0.9 hostB 'echo reached hostB through a DNAT rule' 2>&1 | tail -1
reached hostB through a DNAT rule
```

NAT is the same tool with a table of type `nat` hooked at `prerouting` (for traffic arriving) or `output` (for traffic this host makes), priority `dstnat`. A **`dnat to 127.0.0.2:2202`** rule on destination `127.0.0.9` port 7777 made **`ssh -p 7777` to a made up address reach `hostB`**: the reply was un-translated by `conntrack` with no rule of its own. `snat` and `masquerade` (source rewriting for a router) go in a chain at `postrouting`, from memory.

```text
--- 6. iptables on this system is the same engine
$ iptables --version
iptables v1.8.10 (nf_tables)
$ iptables -S 2>&1 | head -3
# Warning: iptables-legacy tables present, use iptables-legacy to see them
-P INPUT ACCEPT
-P FORWARD ACCEPT
$ iptables-nft -S 2>&1 | head -2; iptables-legacy -S 2>&1 | head -2
# Warning: iptables-legacy tables present, use iptables-legacy to see them
-P INPUT ACCEPT
-P INPUT ACCEPT
-P FORWARD ACCEPT
$ nft list ruleset | wc -l
0
```

On this system **`iptables` is `iptables v1.8.10 (nf_tables)`**: a compatible command line that **writes `nftables` rules**. A warning shows that **`iptables-legacy` tables also exist** in the kernel here: **two rule sets can be active at once** and each tool shows only its own, so on a host with unexplained blocking, run **both** `nft list ruleset` and `iptables-legacy -S`. The final `nft list ruleset | wc -l` reported **0** lines: the lab removed its tables.

**What a host firewall should contain** (from memory, a baseline): `policy drop` on `input`; accept `lo`, established and related, and the ICMP types you need (`icmp` echo, `icmpv6` neighbour discovery, `fragmentation-needed` or **path MTU discovery breaks**); accept your management ports **from named sources** (a set); a rate limit on new connections to SSH; and count what you drop. Put the file in `/etc/nftables.conf` and enable the `nftables` service so it loads at boot (LX 04). A container host adds rules through its runtime (Docker adds its own chains): **do not `flush ruleset`** on such a host, flush **your** table.


---

## 9. `auditd`: what happened, recorded by the kernel

### 9.1 The model

Logs written by programs can be edited by whoever owns the program or the file. **The audit subsystem is the kernel's own record**: when a process makes a system call that matches a rule, the **kernel** builds a record and sends it over a **netlink** socket to **`auditd`**, which writes it to `/var/log/audit/audit.log`. Two halves, then: the **kernel half** (rules, a queue, counters, controlled by `auditctl`) and the **daemon half** (`auditd` writes the file; `ausearch` and `aureport` read it). Rules are of three kinds: **file watches** (`-w PATH -p rwxa`), **system call rules** (`-a always,exit -F arch=b64 -S syscall -F filter`) and **control settings** (`-e` enable or lock, `-b` queue size, `-r` rate, `-f` failure mode).

```text
--- 1. the kernel half and the daemon half
$ auditctl -s | grep -E '^(enabled|failure|backlog_limit|lost)'
enabled 1
failure 1
backlog_limit 64
lost 64404
$ auditctl -l
No rules
```

`enabled 1` means auditing is on in the kernel; **`failure 1`** is the failure mode (**1** = print a message to the kernel log and go on, **2** = panic, which regulated sites choose so that no action goes unrecorded); **`backlog_limit 64`** is the **queue** of records waiting for the daemon; and **`lost 64404`** is the number of records the kernel **had to drop**. That counter is **cumulative since boot**, and on this VM most of it comes from my own earlier experiment of this session, so the figure to watch is **how much it grows**, not its size. No rules are loaded yet.

### 9.2 A file watch, a system call rule, and what a record says

```text
--- 2. a watch on a file
$ auditctl -w /etc/shadow -p ra -k shadow_read
$ auditctl -w /etc/sudoers.d -p wa -k sudo_change
$ auditctl -l
-w /etc/shadow -p ra -k shadow_read
-w /etc/sudoers.d -p wa -k sudo_change
$ ausearch -if /tmp/lx12audit/audit.log -k shadow_read -i 2>&1 | grep -E 'type=(SYSCALL.*syscall=openat|PATH)' | fields
name=/etc/shadow syscall=openat success=yes auid=unset comm=cat exe=/usr/bin/cat
```

`-w /etc/shadow -p ra -k shadow_read` watches **reads and attribute changes** of one file and tags the records with the **key** `shadow_read` (always set a key: it is how you find your own rules' events later, since `ausearch -k` selects by it). One `cat /etc/shadow` produced a **`SYSCALL` record** (`syscall=openat success=yes`, the **command `cat`**, the **executable `/usr/bin/cat`**) and a **`PATH` record** (`name=/etc/shadow`). **One action gives several records with the same event number**, joined by `ausearch`: `SYSCALL` (what, who, result), `CWD` (the working directory), **`PATH`** (each file: inode, mode, owner), `PROCTITLE` (the command line in hex) and, for `execve`, an **`EXECVE`** record with the arguments. **`auid=unset`** in every record: the **audit user id** is set **once, at login, by `pam_loginuid`**, and **survives `su` and `sudo`** (so the record of a command run as root after `sudo` still names the person who logged in). It reads `unset` here because nothing in this sandbox is a login session (limit 7). On a real host, **`auid` is the field that answers "who"**; `uid` and `euid` only say what the process was at that moment.

```text
--- 3. a watch on a system call, with a filter
$ auditctl -a always,exit -F arch=b64 -S execve -F euid=30033 -k alice_exec
$ auditctl -l | tail -1
-a always,exit -F arch=b64 -S execve -F euid=30033 -F key=alice_exec
$ ausearch -if /tmp/lx12audit/audit.log -k alice_exec -i 2>&1 | grep -E 'type=(SYSCALL.*syscall=execve|EXECVE)' | fields
syscall=execve success=yes auid=unset comm=id exe=/usr/bin/id
```

A **system call rule** with a filter: **`-a always,exit -F arch=b64 -S execve -F euid=30033 -k alice_exec`** means "at the exit of every 64-bit `execve` by effective uid 30033, record it". `runuser -u alice -- /usr/bin/id` produced an **`EXECVE` record with `a0=/usr/bin/id`** and the `SYSCALL` record with `comm=id`. `-F arch=b64` is **required** (a 32-bit program on a 64-bit kernel uses another table of numbers; add a `b32` rule too if you run such programs). This is how a "log every command a user runs" policy is built, and why a **shell history file is not an audit trail** (the user can edit it, and a shell does not record what a program it started did).

```text
--- 4. a change to a sudoers file: what the record names
$ ausearch -if /tmp/lx12audit/audit.log -k sudo_change -i 2>&1 | grep -E 'type=(SYSCALL.*syscall=openat|PATH.*sudoers.d/lab)' | fields
name=/etc/sudoers.d/lab syscall=openat success=yes auid=unset comm=bash exe=/usr/bin/bash
$ aureport -if /tmp/lx12audit/audit.log --summary 2>&1 | sed -n '4,5p;7,8p'
Range of time in logs: 11/18/26 02:58:51.834 - 11/18/26 02:58:55.858
Selected time for report: 11/18/26 02:58:51 - 11/18/26 02:58:55.858
Number of changes to accounts, groups, or roles: 0
Number of logins: 0
```

The sudoers watch (`-w /etc/sudoers.d -p wa`) caught the creation of `/etc/sudoers.d/lab`: **`PATH ... name=/etc/sudoers.d/lab`**, the **`openat` by `bash`**. `aureport --summary` gives the overview: the time range, the numbers of configuration changes, account changes, logins, failed logins and authentications. Rules to watch on every server (from the CIS and STIG baselines, from memory): **`/etc/passwd`, `shadow`, `group`, `sudoers` and `sudoers.d` (write), `/etc/ssh/sshd_config` (write), `execve` by uid 0 and by `auid>=1000`, `mount`, `init_module` and `delete_module` (kernel modules), `settimeofday` and `adjtimex` (clock changes), `ptrace`, `setuid` calls, failed file accesses (`exit=-EACCES`) and the audit configuration itself**.

### 9.3 What auditing costs, and what happens when it cannot keep up

```text
--- 5. what auditing costs, and what it does when it cannot keep up
$ cat /tmp/lx12audit/none.txt
/etc/passwd: 644 ns per open+close (200000 calls)
/etc/passwd: 617 ns per open+close (200000 calls)
/etc/passwd: 616 ns per open+close (200000 calls)
$ auditctl -l; cat /tmp/lx12audit/other.txt
-a always,exit -F arch=b64 -S openat -F path=/etc/hostname -F key=hn
/etc/passwd: 965 ns per open+close (200000 calls)
/etc/passwd: 928 ns per open+close (200000 calls)
/etc/passwd: 740 ns per open+close (200000 calls)
$ cat /tmp/lx12audit/hit.txt
/etc/hostname: 35637 ns per open+close (5000 calls)
/etc/hostname: 41760 ns per open+close (5000 calls)
/etc/hostname: 25750 ns per open+close (5000 calls)
$ ausearch -if /tmp/lx12audit/audit.log -k hn 2>/dev/null | grep -c '^type=SYSCALL'; ls -l /tmp/lx12audit/audit.log | awk '{printf "%.1f MB of log for 15000 audited opens\n", $5/1e6}'
15001
13.2 MB of log for 15000 audited opens
$ auditctl -s | grep -E '^(lost|backlog) '
lost 64404
backlog 0
# a small queue and a rate limit: the kernel drops records and counts them
$ auditctl -s | grep -E '^(failure|rate_limit|backlog_limit|lost) '
failure 1
rate_limit 50
backlog_limit 64
lost 89319
```

A small C loop (`openloop`) measures `open` plus `close` of one file. **With no audit rules, 616 to 644 ns per pair** (three runs of 200,000). **With one rule loaded** (`-S openat -F path=/etc/hostname`) and the loop opening a **different** file (`/etc/passwd`), it took **740 to 965 ns**: **about 100 to 350 ns more for a rule that never matches**, because a rule makes the kernel create an audit context for each process and run the filter at every exit. When the loop opened **the audited file, each pair cost 25,750 to 41,760 ns** (**about 25,000 to 41,000 ns extra**, 40 to 65 times the unaudited call), because the kernel built the five records and queued them. The 15,000 audited opens (`15001` `SYSCALL` records found) filled **13.2 MB of log**: **about 880 bytes per audited event**. The conclusions: **filter narrowly** (a path or a uid, not "every `open`"), **audit the rare events** (writes to `/etc`, `execve` by humans) **and not the frequent ones** (`read` on a busy file), and **budget disk** (Problem G).

The last experiment set a queue of **64** records and a **rate limit of 50 records per second** and audited 5,000 opens in a tight loop: **`lost` rose from 64,404 to 89,319, by 24,915 records**, about five records per event (an event is several records), and **the program ran to the end**. With `failure 1` the kernel prints a message and carries on, so **an attacker who can make enough noise can push an event out of the log**. The defences are a **larger `backlog_limit`** (8192 is common), **no `rate_limit`** (the default 0, unlimited), a fast disk, **`failure 2`** where a lost record is worse than a stopped machine, and **monitoring the `lost` counter** (alert if it grows). Make rules permanent in `/etc/audit/rules.d/*.rules`, loaded at boot by `augenrules`, with **`-e 2` as the last line** to make the configuration **immutable until reboot** (from memory): an intruder who gains root cannot remove the rules without a reboot, which is itself recorded. Ship records off the host (`audisp` plugins, `rsyslog`) so that deleting the file does not delete the history.

---

## 10. Packages, time and mandatory access control

### 10.1 Trusting software: signatures and integrity

A package manager is **the largest source of code that runs as root**. Two checks protect it: **the repository's index is signed** and **each package's hash is in the signed index**; and after installation **the files can be verified against the package's own record**.

```text
--- 1. what apt trusts, and how it says so
$ grep -v '^#' /etc/apt/sources.list.d/ubuntu.sources | grep -v '^$' | head -6
Types: deb
URIs: http://archive.ubuntu.com/ubuntu/
Suites: noble noble-updates noble-backports
Components: main universe restricted multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
Types: deb
$ ls /usr/share/keyrings /etc/apt/trusted.gpg.d /etc/apt/keyrings | tr '\n' ' '; echo
/etc/apt/keyrings: deadsnakes.asc docker.asc  /etc/apt/trusted.gpg.d: ubuntu-keyring-2012-cdimage.gpg ubuntu-keyring-2018-archive.gpg  /usr/share/keyrings: ubuntu-archive-keyring.gpg ubuntu-archive-removed-keys.gpg ubuntu-cloudimage-keyring.gpg ubuntu-cloudimage-removed-keys.gpg ubuntu-master-keyring.gpg
$ gpg --no-default-keyring --keyring /usr/share/keyrings/ubuntu-archive-keyring.gpg --list-keys --with-colons 2>/dev/null | awk -F: '$1=="pub" {n++} $1=="uid" {print $10}' | head -4
Ubuntu Archive Automatic Signing Key (2012) <ftpmaster@ubuntu.com>
Ubuntu CD Image Automatic Signing Key (2012) <cdimage@ubuntu.com>
Ubuntu Archive Automatic Signing Key (2018) <ftpmaster@ubuntu.com>
$ ls /var/lib/apt/lists | sed -E 's/_/ /g' | awk '{print $NF}' | sort | uniq -c | sort -rn | head -4
     15 Packages.lz4
      4 InRelease
      1 partial
      1 lock
```

`ubuntu.sources` (the modern **deb822** format) names each repository with **`Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg`**: **that repository is trusted only if its index is signed by a key in that file**. The older model, one global keyring (`/etc/apt/trusted.gpg`, `apt-key`) trusted every key for every repository and is deprecated: **a key for a third party's repository now goes in `/etc/apt/keyrings/NAME.asc` and is named in that repository's `Signed-By`** (the two `.asc` files here belong to a Docker and a Python repository). The Ubuntu keyring holds three keys (two archive signing keys and a CD image key). `/var/lib/apt/lists` holds **4 `InRelease` files** (the signed index of each suite) and **15 compressed `Packages` files**.

```text
--- 2. a signature, made and checked (a throwaway key)
$ gpg --batch --quiet --passphrase '' --quick-generate-key 'lab repo <lab@example.invalid>' ed25519 sign never 2>&1 | tail -1
$ gpg --list-keys --with-colons | awk -F: '$1=="pub" {print "pub algo", $4, "bits", $3} $1=="fpr" && !f {f=1; print "fingerprint length", length($10)}'
gpg: checking the trustdb
gpg: marginals needed: 3  completes needed: 1  trust model: pgp
gpg: depth: 0  valid:   1  signed:   0  trust: 0-, 0q, 0n, 0m, 0f, 1u
pub algo 22 bits 255
fingerprint length 40
$ gpg --batch --yes --armor --detach-sign -o /tmp/lx12pkg/Release.gpg /tmp/lx12pkg/Release; ls /tmp/lx12pkg | tr '\n' ' '; echo
Release Release.gpg gh
$ gpgv --keyring /tmp/lx12pkg/lab.gpg /tmp/lx12pkg/Release.gpg /tmp/lx12pkg/Release 2>&1 | sed -E 's/ using .*//; s/^gpgv: Signature made.*/gpgv: Signature made T/'
gpgv: Signature made T
gpgv:
gpgv: Good signature from "lab repo <lab@example.invalid>"
$ gpgv --keyring /tmp/lx12pkg/lab.gpg /tmp/lx12pkg/Release.gpg /tmp/lx12pkg/Release 2>&1 | sed -E 's/^gpgv: Signature made.*/gpgv: Signature made T/'; echo rc=${PIPESTATUS[0]}
gpgv: Signature made T
gpgv:                using EDDSA key 0E2C1056863AB75E203064ACE996B0397219A3D7
gpgv: BAD signature from "lab repo <lab@example.invalid>"
rc=1
```

The mechanics, with a throwaway key. `gpg --quick-generate-key ... ed25519 sign` made a signing key (**algorithm 22, EdDSA, 255 bits, 40 hex digit fingerprint**); `--detach-sign` signed a file named `Release`; **`gpgv --keyring lab.gpg Release.gpg Release` printed `Good signature`** and after **one byte** was appended to `Release`, **`BAD signature`**, exit 1. `apt` does exactly this with `gpgv` on the `InRelease` file.

```text
--- 3. the chain in an apt repository
$ ls /var/lib/apt/lists/*Release* 2>/dev/null | head -2 | sed 's#.*/##'
archive.ubuntu.com_ubuntu_dists_noble-backports_InRelease
archive.ubuntu.com_ubuntu_dists_noble-updates_InRelease
$ ls -d /var/lib/apt/lists/*InRelease 2>/dev/null | head -1 | xargs -I{} sh -c 'head -3 {}; echo ...; grep -c "^ [0-9a-f]\{64\}" {}' | cut -c1-80
-----BEGIN PGP SIGNED MESSAGE-----
Hash: SHA512

...
414
$ apt-cache policy openssh-server | sed -n '1,6p'
openssh-server:
  Installed: 1:9.6p1-3ubuntu13.20
  Candidate: 1:9.6p1-3ubuntu13.20
  Version table:
 *** 1:9.6p1-3ubuntu13.20 500
        500 http://archive.ubuntu.com/ubuntu noble-updates/main amd64 Packages
```

An **`InRelease`** file is a clear-signed (`-----BEGIN PGP SIGNED MESSAGE-----`, `Hash: SHA512`) list that carries, for each index file, its size and **SHA-256**: the first one here lists **414** hashed entries. The chain is **signature, then index hash, then package hash, then package contents**, and breaking any link stops the install. `apt-cache policy openssh-server` shows the **candidate version, the installed version, and the repository that provides it**, with its priority (500): the basis of pinning (`/etc/apt/preferences.d/`), from memory.

```text
--- 4. is this file the one the package shipped
$ dpkg -S /usr/sbin/sshd
openssh-server: /usr/sbin/sshd
$ dpkg -V openssh-server | grep -v -E '/usr/share/(doc|man)'; echo "modified files: $(dpkg -V openssh-server | grep -c -v -E '^missing')"
modified files: 0
$ dpkg -V openssh-client | grep -v -E '/usr/share/(doc|man)'
??5??????   /usr/bin/ssh-keygen
$ debsums openssh-client 2>&1 | grep -v 'OK$' | head -3
/usr/bin/ssh-keygen                                                       FAILED
$ dpkg -V openssh-client | grep -v -E '/usr/share/(doc|man)'; echo restored
restored
$ dpkg -V 2>/dev/null | grep -v -E '^missing' | wc -l
13
$ dpkg -V 2>/dev/null | grep -c '^missing'; echo '(the image was built without man pages and docs, so those count as missing)'
10457
(the image was built without man pages and docs, so those count as missing)
```

After installation: **`dpkg -S FILE`** says which package owns a file; **`dpkg -V PACKAGE`** compares each file's size, mode, owner and **MD5** with the record in `/var/lib/dpkg/info/PACKAGE.md5sums` and prints a line only for a **difference**. `dpkg -V openssh-server` printed **0 modified files** (the image has no man pages and docs, which show as `missing`). I changed **one byte** of `ssh-keygen`, and `dpkg -V` printed **`??5??????  /usr/bin/ssh-keygen`** (the `5` marks the **checksum**), `debsums` printed **`FAILED`**, and after restoring the file both were clean. The whole system showed **13 changed files and 10,457 missing ones** (docs and man pages that the image lacks). **An attacker who replaces a binary also changes the checksum file**, so this check detects accidents and clumsy tampering; the real defences are a **read-only root, signed boot and measured boot**, and **AIDE or `tripwire`** with the database kept off the host, from memory. On Red Hat family systems **`rpm -V`** and `rpm -Va` do the same, and `rpm --checksig` verifies a package's signature.

```text
--- 5. checksums, the other half: a file you downloaded
$ cat /tmp/lx12pkg/SHA256SUMS | cut -c1-30; cd /tmp/lx12pkg && sha256sum -c SHA256SUMS
d4e4877bac978b7952f0d544fc52eb
tool.tar: OK
$ cd /tmp/lx12pkg && sha256sum -c SHA256SUMS 2>&1; echo rc=$?
tool.tar: FAILED
sha256sum: WARNING: 1 computed checksum did NOT match
rc=1
(a checksum that comes from the same place as the file proves nothing about the source)
```

A checksum file proves something only if it **arrives by a different path from the file**. `sha256sum -c` printed **`OK`**, then **`FAILED`** after one byte was appended, and the lesson in the last line holds: a `SHA256SUMS` file on the **same download page** as the tarball is a check against corruption, **not against a hostile mirror**. Verify a signature of the checksum file with a key you obtained earlier.

**`apt` and `dnf`** (from memory, the pairs interviews ask for): `apt update` and `dnf makecache` (refresh the index); `apt install` and `dnf install`; `apt list --installed` and `dnf list installed`; **`apt-cache policy`** and `dnf info`/`dnf repoquery`; `dpkg -S` and **`rpm -qf`** (which package owns a file); `dpkg -L` and **`rpm -ql`** (the files a package installs); `dpkg -V` and **`rpm -V`**; `apt-mark hold` and `dnf versionlock`; `/etc/apt/sources.list.d/` and `/etc/yum.repos.d/` (with **`gpgcheck=1`**). Automatic security updates: **`unattended-upgrades`** on Debian and Ubuntu, `dnf-automatic` on Red Hat family. A **kernel update needs a reboot** (or a live patch service), and **`needrestart`** lists services that still use a replaced library.

### 10.2 Time

**Why a security tutorial has a clock section.** Certificates and SSH certificates have a validity period (section 7: an expired certificate was refused); **Kerberos refuses a clock more than 5 minutes off**; one time passwords (TOTP) use 30 second windows; and **logs from several machines are only comparable if their clocks agree**, which is the first thing an investigation needs. None was measured here (limit 5), so the rest is from memory.

Linux machines synchronise with **NTP**: a daemon asks a few time servers, estimates the offset and the network delay, and **disciplines the kernel clock** by **slewing** (running it slightly fast or slow, so time never goes backward) for small errors and **stepping** (a jump) for large ones at start up. The daemons: **`chrony`** (the default on Red Hat family systems and the better choice for laptops and VMs, it converges quickly and handles interruptions), **`systemd-timesyncd`** (a simple SNTP client, enough for most servers) and the reference `ntpd`. **`chronyc tracking`** shows `Reference ID`, `Stratum`, **`System time` (the current offset)**, `Last offset`, `RMS offset`, `Frequency` (the drift in parts per million) and **`Leap status`**; **`chronyc sources -v`** shows each server with `*` for the one in use and `Reach` (an octal history of the last 8 polls, 377 = all answered). A **`makestep 1.0 3`** line allows a step during the first three updates. **Leap seconds** are either stepped or **smeared** over many hours (large cloud providers smear), and mixing smeared and unsmeared sources gives an error of up to half a second. **PTP** (`ptp4l`, `phc2sys`) with hardware timestamps gives sub-microsecond accuracy for finance and telecom, at the cost of switches that support it. The first questions when two machines disagree: `timedatectl` (is the clock synchronised, is the time zone as expected: `UTC` on servers), `chronyc tracking`, and **is the VM host the source** (a VM whose time comes from its host and from NTP drifts back and forth).

### 10.3 Mandatory access control: SELinux and AppArmor (from memory)

**Discretionary access control** (DAC) is the permission bits and ACLs of LX 07: **the owner decides**. **Mandatory access control** (MAC) adds a **policy written by the administrator that even root cannot change at run time**, so a compromised service running as root is still limited to what its policy allows. Both are **Linux Security Modules (LSM)**: hooks in the kernel that run **after** the DAC check, so MAC can only **remove** access. **Neither was active on this VM** (limit 4): the kernel has SELinux support compiled in (`CONFIG_SECURITY_SELINUX=y`), but no AppArmor, no `/sys/kernel/security/lsm` and no policy, so **what follows is from the manuals and from memory, not from a run**.

| | **SELinux** (Red Hat, Fedora, Android) | **AppArmor** (Ubuntu, SUSE, Debian optional) |
| --- | --- | --- |
| What is labelled | **every process, file, port and socket** gets a **label** (a security context) | **programs** are confined by a **profile**; files are named by **path** |
| Policy shape | **types**: `httpd_t` may read files of type `httpd_sys_content_t` | **paths**: `/usr/sbin/nginx { /var/www/** r, /var/log/nginx/* w, network inet stream, }` |
| Modes | **enforcing**, **permissive** (log only), disabled | per profile: **enforce** or **complain** |
| Inspect | `getenforce`, `sestatus`, **`ls -Z`**, **`ps -Z`**, `id -Z` | **`aa-status`**, `ps auxZ` |
| Switch mode | `setenforce 0` (permissive until reboot), `SELINUX=` in `/etc/selinux/config` | `aa-complain PROFILE`, `aa-enforce PROFILE` |
| Fix a label | **`restorecon -Rv PATH`**, `semanage fcontext -a -t TYPE 'REGEX'` | edit the profile in `/etc/apparmor.d/`, `apparmor_parser -r FILE` |
| Switches in policy | **booleans**: `getsebool -a`, **`setsebool -P httpd_can_network_connect on`** | profile abstractions and `#include <tunables/...>` |
| Denials | **AVC** messages in the audit log: `ausearch -m avc -ts recent`; **`sealert`**, `audit2why`, `audit2allow` | lines with `apparmor="DENIED"` in the audit log or `dmesg`; `aa-logprof` turns them into rules |

A **context** has four fields, `user:role:type:level`, for example **`system_u:system_r:httpd_t:s0`** for a web server process and **`unconfined_u:object_r:httpd_sys_content_t:s0`** for a file. In day to day use only the **type** matters. An **AVC denial** reads like this (an illustration, not output from this VM):

```text
type=AVC msg=audit(...): avc:  denied  { read } for  pid=1234 comm="nginx" name="index.html"
  dev="sda1" ino=5678 scontext=system_u:system_r:httpd_t:s0
  tcontext=unconfined_u:object_r:user_home_t:s0 tclass=file permissive=0
```

Read it from `denied { read }` (the operation), `scontext` (the **source**, who: process type `httpd_t`), `tcontext` (the **target**, what: file type `user_home_t`) and `tclass=file`: **a web server tried to read a file labelled as a user's home file**. The fix is **not** `setenforce 0` and not `audit2allow` run on everything. It is to **label the file correctly** (`semanage fcontext -a -t httpd_sys_content_t '/srv/www(/.*)?'` then `restorecon -Rv /srv/www`), because the content was put in a directory whose default type is wrong, which is the cause of most SELinux problems (files **moved** with `mv` keep their old label, files **copied** take the destination's). The test of a fix is `permissive=0` and a retry. The interview answer: **"I never disable SELinux; I switch to permissive for one service (`semanage permissive -a httpd_t`), read the AVCs, fix the label or set a boolean, and switch it back."**

### 10.4 Kernel settings and build options

```text
--- 1. kernel settings that matter for security, as this VM has them
$ sysctl -a 2>/dev/null | grep -E '^(kernel.randomize_va_space|kernel.kptr_restrict|kernel.dmesg_restrict|kernel.unprivileged_bpf_disabled|kernel.yama.ptrace_scope|kernel.perf_event_paranoid|fs.protected_symlinks|fs.protected_hardlinks|fs.protected_regular|fs.protected_fifos|fs.suid_dumpable|net.ipv4.tcp_syncookies|net.ipv4.conf.all.rp_filter|net.ipv4.conf.all.accept_redirects|net.ipv4.conf.all.send_redirects|net.ipv4.ip_forward|net.ipv4.conf.all.log_martians|kernel.modules_disabled|kernel.sysrq|kernel.core_pattern) ' | sort
fs.protected_fifos = 0
fs.protected_hardlinks = 1
fs.protected_regular = 0
fs.protected_symlinks = 0
fs.suid_dumpable = 0
kernel.core_pattern = core
kernel.dmesg_restrict = 1
kernel.kptr_restrict = 0
kernel.perf_event_paranoid = 2
kernel.randomize_va_space = 2
kernel.unprivileged_bpf_disabled = 2
net.ipv4.conf.all.accept_redirects = 1
net.ipv4.conf.all.log_martians = 0
net.ipv4.conf.all.rp_filter = 0
net.ipv4.conf.all.send_redirects = 1
net.ipv4.ip_forward = 0
net.ipv4.tcp_syncookies = 1
```

The values on this VM, with what each protects: **`kernel.randomize_va_space = 2`** (full ASLR), **`kernel.dmesg_restrict = 1`** (only privileged users read the kernel log), **`kernel.unprivileged_bpf_disabled = 2`** (unprivileged BPF is off and cannot be turned on without a reboot), `kernel.perf_event_paranoid = 2` (unprivileged `perf` limited to user space, LX 10), **`fs.protected_hardlinks = 1`** and **`fs.protected_symlinks = 0`** (symbolic link and hard link tricks in world writable sticky directories such as `/tmp`: **Ubuntu's default is 1 for both**, so this VM is looser than a normal install and the setting belongs in a baseline), `fs.suid_dumpable = 0` (setuid programs do not dump core), **`net.ipv4.tcp_syncookies = 1`** (survive a SYN flood), `net.ipv4.ip_forward = 0` (not a router) and **`net.ipv4.conf.all.rp_filter = 0`** (**reverse path filtering is off**; a baseline sets 1 or 2 on a host that is not multihomed), `accept_redirects = 1` and `send_redirects = 1` (**a baseline turns both off** on a host that is not a router) and `log_martians = 0`. Missing from the list is **`kernel.yama.ptrace_scope`**: Yama is not built, so any process may `ptrace` another of the same user (the usual default is 1). The command **`sysctl -w`** changes a value **until reboot**; the permanent form is a file in `/etc/sysctl.d/` loaded at boot (`10-kernel-hardening.conf`, `10-network-security.conf` and `10-ptrace.conf` are in the directory here).

```text
--- 2. where a setting comes from, and how to set it so it survives
$ ls /etc/sysctl.d /usr/lib/sysctl.d 2>&1 | head -12 | tr '\n' ' '; echo
/etc/sysctl.d: 10-bufferbloat.conf 10-console-messages.conf 10-ipv6-privacy.conf 10-kernel-hardening.conf 10-magic-sysrq.conf 10-map-count.conf 10-network-security.conf 10-ptrace.conf 10-zeropage.conf 99-sysctl.conf README.sysctl
$ sysctl -n net.ipv4.tcp_syncookies; sysctl -w net.ipv4.tcp_syncookies=0 >/dev/null; sysctl -n net.ipv4.tcp_syncookies; sysctl -w net.ipv4.tcp_syncookies=1 >/dev/null; sysctl -n net.ipv4.tcp_syncookies
1
0
1
```

The test above toggled `tcp_syncookies` from 1 to 0 and back to 1 (the three lines `1`, `0`, `1`): a setting that is flipped for a test must be flipped back and **checked**.

```text
--- 3. what the kernel was built with
$ zcat /proc/config.gz 2>/dev/null | grep -E '^CONFIG_(SECURITY|SECCOMP|AUDIT|STACKPROTECTOR|FORTIFY_SOURCE|HARDENED_USERCOPY|RANDOMIZE_BASE|STRICT_KERNEL_RWX|INIT_ON_ALLOC_DEFAULT_ON|SECURITY_YAMA|SECURITY_APPARMOR|SECURITY_SELINUX|SECURITY_LOCKDOWN_LSM|MODULE_SIG|BPF_UNPRIV_DEFAULT_OFF)[A-Z_]*=' | sort | head -40
CONFIG_AUDIT=y
CONFIG_AUDITSYSCALL=y
CONFIG_AUDIT_ARCH=y
CONFIG_BPF_UNPRIV_DEFAULT_OFF=y
CONFIG_FORTIFY_SOURCE=y
CONFIG_HARDENED_USERCOPY=y
CONFIG_HARDENED_USERCOPY_DEFAULT_ON=y
CONFIG_RANDOMIZE_BASE=y
CONFIG_SECCOMP=y
CONFIG_SECCOMP_FILTER=y
CONFIG_SECURITY=y
CONFIG_SECURITYFS=y
CONFIG_SECURITY_DMESG_RESTRICT=y
CONFIG_SECURITY_LANDLOCK=y
CONFIG_SECURITY_LOCKDOWN_LSM=y
CONFIG_SECURITY_LOCKDOWN_LSM_EARLY=y
CONFIG_SECURITY_NETWORK=y
CONFIG_SECURITY_NETWORK_XFRM=y
CONFIG_SECURITY_PATH=y
CONFIG_SECURITY_SELINUX=y
CONFIG_SECURITY_SELINUX_AVC_STATS=y
CONFIG_SECURITY_SELINUX_BOOTPARAM=y
CONFIG_SECURITY_SELINUX_DEVELOP=y
CONFIG_SECURITY_SELINUX_SIDTAB_HASH_BITS=9
CONFIG_STACKPROTECTOR=y
CONFIG_STACKPROTECTOR_STRONG=y
CONFIG_STRICT_KERNEL_RWX=y
$ cat /sys/kernel/security/lsm 2>&1 | head -1; ls /sys/kernel/security 2>&1 | head -2
cat: /sys/kernel/security/lsm: No such file or directory
$ cat /sys/devices/system/cpu/vulnerabilities/* 2>/dev/null | cut -c1-40 | sort | uniq -c | sort -rn | head -5
     16 Not affected
      1 Mitigation: usercopy/swapgs barriers and
      1 Mitigation: Speculative Store Bypass dis
      1 Mitigation: Enhanced / Automatic IBRS; I
```

The build options explain what a hardened kernel contains: **stack protector (strong)**, **`FORTIFY_SOURCE`** (checked copies of string functions), **`HARDENED_USERCOPY`** (bounds checks on copies between kernel and user), **`STRICT_KERNEL_RWX`** (kernel code is not writable, data is not executable), **`RANDOMIZE_BASE`** (KASLR), **`SECCOMP_FILTER`** (system call filters for sandboxes, LX 09), **`SECURITY_LANDLOCK`** (a process may restrict its own file access without privileges), **`BPF_UNPRIV_DEFAULT_OFF`**, and **`AUDITSYSCALL`**. **Not set here:** `SECURITY_APPARMOR`, `SECURITY_YAMA` and `MODULE_SIG`; `/sys/kernel/security/lsm` does not exist, and the CPU vulnerability files report **16 "Not affected"** and a few mitigations (a VM view of the host).

---

## 11. A worked audit: find the seven mistakes

A checklist that is only read is not a control. Every check in sections 1 to 10 that can be a command **should be one**, run on a schedule and on every new host. Here is a short checker that tests eight things, `hcheck.sh`, written for this tutorial:

```bash
#!/bin/bash
# hcheck.sh [sshd_config]: eight quick host checks. Prints FAIL lines and a count. Run as root.
CONF=${1:-/etc/ssh/sshd_config}; fail=0
bad() { echo "FAIL $*"; fail=$((fail+1)); }
# 1. only root has uid 0
for u in $(awk -F: '$3==0 && $1!="root" {print $1}' /etc/passwd); do bad "uid 0 account: $u"; done
# 2. no account with an empty password field
for u in $(awk -F: '$2=="" {print $1}' /etc/shadow); do bad "empty password: $u"; done
# 3. no sudo rule that gives NOPASSWD: ALL
grep -rEl 'NOPASSWD: *ALL' /etc/sudoers /etc/sudoers.d 2>/dev/null | while read f; do echo "FAIL NOPASSWD ALL in $f"; done | tee /tmp/hc.n; fail=$((fail+$(wc -l < /tmp/hc.n)))
# 4. authorized_keys and .ssh not writable by group or others
for f in /home/*/.ssh /home/*/.ssh/authorized_keys /root/.ssh/authorized_keys; do
  [ -e "$f" ] && [ $(( 0$(stat -c %a "$f") & 022 )) -ne 0 ] && bad "writable by others: $f ($(stat -c %a "$f"))"
done
# 5. no world-writable file under /etc
for f in $(find /etc -xdev -type f -perm -0002 2>/dev/null); do bad "world-writable: $f"; done
# 6. sshd effective settings
[ -f "$CONF" ] && while read k v; do
  case "$k=$v" in permitrootlogin=yes) bad "sshd: root login with a password";; passwordauthentication=yes) bad "sshd: password logins on";;
    permitemptypasswords=yes) bad "sshd: empty passwords allowed";; x11forwarding=yes) bad "sshd: X11 forwarding on";; esac
done < <(sshd -T -f "$CONF" 2>/dev/null)
# 7. setuid files outside the known set
known="chfn chsh fusermount3 gpasswd mount newgrp passwd su sudo umount dbus-daemon-launch-helper ssh-keysign polkit-agent-helper-1"
for f in $(find /usr /bin /sbin /opt /home /tmp -xdev -perm -4000 -type f 2>/dev/null); do
  case " $known " in *" $(basename $f) "*) ;; *) bad "unknown setuid file: $f";; esac
done
# 8. listening sockets on all addresses
ss -Hltn 2>/dev/null | awk '$4 ~ /^(0\.0\.0\.0|\*|\[::\]):/ {print $4}' | sort -u | while read a; do echo "NOTE listens on all addresses: $a"; done
echo "checks failed: $fail"
```

It checks **(1)** only root has uid 0, **(2)** no empty password field, **(3)** no `NOPASSWD: ALL` in sudoers, **(4)** `.ssh` and `authorized_keys` not writable by others, **(5)** nothing under `/etc` is world writable, **(6)** the effective `sshd` settings (from `sshd -T`, the command that reports what the daemon will really do), **(7)** setuid files not on the known list of section 3.3, and **(8)** which sockets listen on every address (a `NOTE`, not a failure).

```text
--- 1. a checker for the basics
$ wc -l < hcheck.sh
29
$ bash hcheck.sh /tmp/none.conf
FAIL NOPASSWD ALL in /etc/sudoers
NOTE listens on all addresses: 0.0.0.0:2024
checks failed: 1
```

On the lab VM, before anything was planted, it printed **one failure**: **`NOPASSWD ALL in /etc/sudoers`**, which is the sandbox's own rule for its user and stays in every later run. It also noted a **listener on `0.0.0.0:2024`** that I did not create and that `ss -p` cannot attribute to a process (most likely the sandbox's own management service; not verified). That is a **baseline**: a first run on a real host is a list of things to decide on (accept with a reason, or fix), and a later run is a **difference from the baseline**.

```text
--- 2. a host with seven mistakes (made on purpose)
useradd warning: svc's uid 0 outside of the UID_MIN 1000 and UID_MAX 60000 range.
$ bash hcheck.sh /tmp/bad_sshd.conf
FAIL uid 0 account: svc
FAIL empty password: svc
FAIL NOPASSWD ALL in /etc/sudoers
FAIL NOPASSWD ALL in /etc/sudoers.d/lab
FAIL writable by others: /home/alice/.ssh (777)
FAIL writable by others: /home/alice/.ssh/authorized_keys (666)
FAIL world-writable: /etc/lx12.cron
FAIL sshd: root login with a password
FAIL sshd: password logins on
FAIL sshd: X11 forwarding on
FAIL unknown setuid file: /usr/local/bin/idsuid
NOTE listens on all addresses: 0.0.0.0:2024
NOTE listens on all addresses: 0.0.0.0:8088
checks failed: 11
```

Seven mistakes were planted: **a second uid 0 account with an empty password** (`svc`: two findings), **`NOPASSWD: ALL` for a user**, **an `.ssh` directory with mode 777 and an `authorized_keys` with mode 666**, **a world writable file in `/etc`**, **an `sshd` configuration that allows root with a password, passwords and X11 forwarding** (three findings), **a setuid copy of a program in `/usr/local/bin`**, and **a web server listening on all addresses**. The checker printed **11 `FAIL` lines** (10 for the planted ones and the baseline) and **a `NOTE` for port 8088**. `useradd` itself warned about a uid outside the range: a tool's warning is part of the evidence.

```text
--- 3. fix each one, run again
$ userdel -f -r svc 2>&1 | grep -v 'mail spool'; getent passwd svc | wc -l
userdel: user svc is currently used by process 2
0
$ chmod 700 /home/alice/.ssh; chmod 600 /home/alice/.ssh/authorized_keys; chmod 600 /etc/lx12.cron
$ rm /etc/sudoers.d/lab /usr/local/bin/idsuid; pkill -f '^python3 -m http.server 8088'
$ bash hcheck.sh /tmp/bad_sshd.conf
FAIL NOPASSWD ALL in /etc/sudoers
NOTE listens on all addresses: 0.0.0.0:2024
checks failed: 1
```

After the fixes (delete the account with `userdel -f -r` because a uid 0 account is "in use by process 2", the kernel thread `kthreadd`, which runs as uid 0; restore the modes; remove the sudoers file and the setuid copy; stop the server; correct the `sshd` file) **one failure remained, the baseline**. What the exercise shows: **each finding had a named remedy and a command that proves it**; the checker is **a few lines of shell** and the real tools (**Lynis**, **OpenSCAP** with the CIS profiles, **`ssh-audit`**) are the same idea with several hundred checks; and a check on the **effective** configuration (`sshd -T`, `nft list ruleset`, `sysctl -a`) is worth more than a check on the file you think you edited.

---

## 12. What it costs, and what breaks

| Control | Measured here | Cost to remember |
| --- | --- | --- |
| New SSH connection | **204 ms each** (20 in 4.074 s) | key exchange and a process; **multiplexing: 7 ms** (20 in 0.145 s), 28 times faster |
| `ControlMaster` | one socket file | **anyone who can read the socket can use the session** |
| Audit rule that does not match | **about 100 to 350 ns** per `open` and `close` pair (616 to 644 ns without) | an audit context per process |
| Audit rule that matches | **25,750 to 41,760 ns**, **about 880 bytes of log** per event | budget disk and CPU; filter narrowly |
| Audit queue too small | **24,915 records lost** for 5,000 events, no error | monitor `lost`; larger `backlog_limit`; no `rate_limit` |
| `nft` rule with a set | one lookup, a set change needs **no reload** | an allow list is data |
| `drop` against `reject` | reject **2 ms**, drop **3.002 s** (client waits) | `drop` outside, `reject` inside |
| Rate limit | **8 of 30** simultaneous attempts passed | a patient client is not limited (25 of 30 passed one by one) |
| Capability on a binary | **1 bit of 40**; lost by `cp` and by a rebuild | deployment must preserve `xattr` |
| `no_new_privs` | one flag | `sudo` and setuid programs stop working inside |

**What breaks.** A login that worked yesterday fails after a **permission change** on the home directory (`StrictModes`, section 5.2, Problem E). A key stops working **after a change of PAM or of `AllowUsers`**; the client says only `Permission denied`, so **read the server log at `VERBOSE`**. A **reloaded firewall** drops the session you are using if the new rules lack `ct state established`. **A "hardened" `sshd` configuration with a typo** refuses to restart. A new **`sudoers`** file with a syntax error disables `sudo` for everyone. **An expired certificate** or **a wrong clock** looks like an authentication failure. **A rebuilt host** changes its host key, and **automation that ignores the warning** (`StrictHostKeyChecking no`) is the same automation that will not notice an attack.

---

## 13. Common mistakes

1. **Editing `sshd_config` and restarting without `sshd -t`**, or closing your session before testing a **second** one. Keep one session open until a new login works.
2. **Turning off `PasswordAuthentication` and leaving `KbdInteractiveAuthentication` on**: with PAM, passwords still arrive that way (section 6.2).
3. **`chmod 777` to "fix" a permission problem on `~/.ssh` or the home directory**: `StrictModes` then refuses the key (Problem E). The modes are 700 for `.ssh`, 600 for `authorized_keys`, 750 or 700 for the home.
4. **Allowing `sudo` for an editor, pager, `find`, `tar` or a script in a writable directory**: each has a shell escape. Allow a fixed command with fixed arguments, or `sudoedit`.
5. **`NOPASSWD: ALL` "temporarily"** and never removed (section 11 finds it). A rule you cannot defend in a review does not belong in the file.
6. **Disabling SELinux or AppArmor** to make a service start, instead of reading the one denial that explains it.
7. **`setuid root` where a capability would do**: `setcap cap_net_bind_service` is one bit, setuid is all of them (section 3.2).
8. **Forgetting that `cp`, a container build or a package rebuild drops file capabilities and ACLs** (extended attributes): the service starts and fails at the first privileged call.
9. **Forwarding the agent through a bastion** (`ssh -A`): use `ProxyJump`. And **forwarding to a bind address of `0.0.0.0`** exposes the forwarded service to the network.
10. **A firewall change over SSH with no revert timer**, or a **`flush ruleset`** on a container host that has the runtime's own chains.
11. **Auditing everything** (`-S all`): the queue overflows, `lost` grows and the events you wanted are among the ones dropped. Audit the rare and important.
12. **A checksum from the same page as the download**, treated as authentication.
13. **Ignoring `HOST IDENTIFICATION HAS CHANGED`** or setting `StrictHostKeyChecking no` globally; and the opposite, **deleting `known_hosts`** to make the warning go away.
14. **Two clocks that disagree** (a VM whose time is not synchronised): expired certificates, failed Kerberos and logs that cannot be correlated.

---

## 14. Questions out loud

1. **What is the difference between authentication and authorisation, and where does each happen on Linux?** Authentication (who are you) happens in `sshd` (keys, certificates) and PAM's `auth` stack; authorisation (what may you do) in file modes and ACLs, `sudoers`, capabilities, the firewall and SELinux or AppArmor; accounting in `auditd` and logs.
2. **What is in `/etc/passwd` and `/etc/shadow`, and why are they separate?** `passwd` maps names to uids, gids, home and shell and must be world readable; the password hash and ageing moved to `shadow`, mode 640 `root:shadow`, so that nobody can copy the hashes and attack them off line.
3. **`id bob` works but `/etc/passwd` has no `bob`.** NSS: `nsswitch.conf` lists another source (`sss`, `ldap`, `systemd`), so use `getent passwd bob`.
4. **How does `sudo` decide, and how do you make a safe rule?** It reads `sudoers` and `sudoers.d`, matches user, host, run-as user and command (full path), asks for the caller's password unless `NOPASSWD`, and logs. Fix the command and arguments, never allow anything that runs other commands, check with `visudo -cf`, install a drop-in with mode 440.
5. **What does the setuid bit do, and what is a safer alternative?** It sets the effective and saved uid to the file owner at `exec` (measured: real 30033, effective 0); a file capability grants one bit (`cap_net_bind_service=ep`) and is safer. `no_new_privs` disables both.
6. **A service must listen on port 443 but not run as root.** `setcap cap_net_bind_service=+ep`, or `AmbientCapabilities=CAP_NET_BIND_SERVICE` in the unit, or `net.ipv4.ip_unprivileged_port_start`; preserve the xattr in deployment.
7. **Explain a PAM stack.** Four types (`auth`, `account`, `password`, `session`), a list of `control module args` per service read in order; `required` fails at the end, `requisite` at once, `sufficient` succeeds at once, `[success=1 default=ignore]` skips lines. Test with `pamtester`; keep a root session open when editing.
8. **How does SSH key authentication work, and where is the trust?** The server holds the public key in `authorized_keys`; the client proves possession by signing a challenge; the private key never leaves the client. The client trusts the server through `known_hosts` or a host certificate.
9. **What are `-L`, `-R` and `-D`?** `-L` the client listens and forwards through the server to a destination; `-R` the server listens and forwards back to the client's side; `-D` the client opens a SOCKS proxy. The bind address decides who can use the port.
10. **How do you reach a host behind a bastion?** `ProxyJump` (`-J`): end to end encryption with the bastion only relaying; better than agent forwarding. Set options in a `Host *` block so they apply to both hops.
11. **List the first changes you make to `sshd_config`.** No root login, no passwords (and no keyboard-interactive), `AllowUsers` or `AllowGroups`, `MaxAuthTries` and `LoginGraceTime` low, forwarding off unless needed, `LogLevel VERBOSE`, a modern algorithm list; check with `sshd -t` and read the diff of `sshd -T`.
12. **Why use SSH certificates?** One CA key replaces many `authorized_keys` entries; certificates carry principals, validity, source address and a forced command; short lifetimes make revocation rare; host certificates remove the first connection prompt.
13. **`drop` or `reject`?** Drop outside (a scanner learns nothing and waits); reject inside (an immediate error for people). Measured: 2 ms against 3 s of waiting.
14. **How do you replace a firewall safely over SSH?** Check with `nft -c`, apply as one file (a transaction), keep `ct state established accept` first, and have a timer that reverts.
15. **What does `auditd` give that logs do not?** Kernel generated records of system calls and file access, tied to the `auid` of the login, which survive `su` and `sudo`; they need rules, a big enough queue, a `lost` counter you monitor, and shipping off the host.
16. **A service fails and SELinux is suspected. What do you do?** `ausearch -m avc -ts recent`, read `scontext`, `tcontext`, the operation; most often a wrong file label (`restorecon`, `semanage fcontext`) or a boolean (`setsebool -P`); never `setenforce 0` as the fix.
17. **How do you know a package file is genuine?** The repository's signed index (`Signed-By`), the hash chain to the package, and after install `dpkg -V` or `rpm -V`; a read only root and measured boot for the case where the attacker also edits the checksums.
18. **Why does time matter for security?** Certificate and Kerberos validity, TOTP, and correlating logs; synchronise with `chrony`, watch `chronyc tracking` and the offset.

---

## 15. Problems

**Problem A. A deploy user.** `alice` must be able to run `/usr/bin/id -un` and `/usr/bin/uptime` as root with no password, and nothing else. Write the file, validate it before installing, test the allowed and a refused command as alice, and say why you used full paths.

**Problem B. Port 443 without root.** A small program must bind port 443 as an ordinary user. Show it failing, fix it with a file capability, and show what a rebuild does to the fix. How do you deploy it so it survives?

**Problem C. Group and time.** Write a PAM `account` stack for a test service that allows only members of the group `ops`, and then also a time rule that denies everybody all day. Test both with `pamtester` for a member and a non member, and say what you would change to allow office hours.

**Problem D. A forward.** A web service is on `127.0.0.3:8090`, reachable only from the jump host. Reach it from your side with one `ssh` command and prove it with `curl`. Which bind address does your command use, and why?

**Problem E. The right key is refused.** The key is correct and `authorized_keys` is present, but login fails with `Permission denied`. The home directory has mode 777. Reproduce it, find the line in the **server** log that says why, and fix it.

**Problem F. Audit commands and writes.** Record every command run by uid 30033 and every write under `/etc/ssh`, run one command as that user and create one file, and retrieve the evidence by key.

**Problem G. The audit budget.** A server does 20,000 `openat` calls per second. One audit rule matches 1 percent of them. Using the measured costs (616 to 644 ns without a rule, 740 to 965 ns with an unrelated rule, 25,750 to 41,760 ns when it matches, 880 bytes of log per event), what share of one CPU does auditing take, and how much log is written per day? What do you do about it?

---

## 16. Answers

```text
--- Problem A: a deploy user may restart one service, nothing else
$ visudo -cf /tmp/deploy.sudo && install -m 440 /tmp/deploy.sudo /etc/sudoers.d/lab
/tmp/deploy.sudo: parsed OK
$ runuser -u alice -- sudo -n /usr/bin/id -un
root
$ runuser -u alice -- sudo -n /usr/bin/id -u 2>&1 | head -1
sudo: a password is required
$ runuser -u alice -- sudo -n -l | tail -3

User alice may run the following commands on vm:
    (root) NOPASSWD: /usr/bin/id -un, /usr/bin/uptime
```

**A.** The rule is two lines: a `Cmnd_Alias` and `alice ALL=(root) NOPASSWD: RESTART`. `visudo -cf` said `parsed OK` before `install -m 440`. As alice, `sudo -n /usr/bin/id -un` printed **`root`**; `sudo -n /usr/bin/id -u` (different arguments) printed **`a password is required`**: **arguments are part of the rule**. `sudo -l` lists what remains. Full paths because a bare `id` would resolve through `PATH`, and a writable directory in the path becomes a root exploit. For a service restart on a `systemd` host the rule is the fixed `systemctl restart NAME`, **not** `systemctl *` (`systemctl edit` and `systemctl link` would let her write a unit that runs as root).

```text
--- Problem B: port 443 without root and without setuid
$ runuser -u alice -- /tmp/lx12p/bind443
uid 30033: bind to port 443 failed: Permission denied
$ setcap cap_net_bind_service=+ep /tmp/lx12p/bind443 && getcap /tmp/lx12p/bind443
/tmp/lx12p/bind443 cap_net_bind_service=ep
$ runuser -u alice -- /tmp/lx12p/bind443
uid 30033: bound to port 443
$ gcc -O2 -o /tmp/lx12p/bind443 /tmp/lx12p/b.c; getcap /tmp/lx12p/bind443 | wc -l; runuser -u alice -- /tmp/lx12p/bind443
0
uid 30033: bind to port 443 failed: Permission denied
```

**B.** As user 30033 the bind failed (**`Permission denied`**). `setcap cap_net_bind_service=+ep` made it **`bound to port 443`**. After a **rebuild** (`gcc` again) **`getcap` printed nothing** and the bind failed again: **the capability is an attribute of the file, and the compiler wrote a new file**. Deploy by applying `setcap` as the last step of the install (a package `postinst` or the image build), by preserving extended attributes in whatever copies the file (`cp --preserve=xattr`, `rsync -X`, `tar --xattrs`), or by moving the privilege out of the file into the unit (`AmbientCapabilities=CAP_NET_BIND_SERVICE`, LX 04) or the sysctl `net.ipv4.ip_unprivileged_port_start=443`, which applies to the whole host.

```text
--- Problem C: only group ops, and only when the time rule allows
$ for u in alice bob; do printf '%s: ' $u; pamtester lab_c $u acct_mgmt >/dev/null 2>&1 && echo allowed || echo denied; done
alice: allowed
bob: denied
$ for u in alice bob; do printf '%s: ' $u; pamtester lab_c $u acct_mgmt >/dev/null 2>&1 && echo allowed || echo denied; done
alice: denied
bob: denied
```

**C.** The stack is `account required pam_succeed_if.so user ingroup ops` followed by `account required pam_permit.so`. With it, **alice (in `ops`) was allowed and bob denied**. Adding `account required pam_time.so conffile=...` with the rule `lab_c;*;*;!Al0000-2400` (no one, any day, all day) made **both denied**: `required` means the time rule must also pass. For office hours the rule becomes `lab_c;*;*;Wk0800-1800` (weekdays 08:00 to 18:00; `Wk` is Monday to Friday), from the `time.conf` format. Put real rules in `/etc/security/time.conf` and name the service the module protects.

```text
--- Problem D: reach a service that only the bastion can see
$ curl -s -o /dev/null -w 'direct to 127.0.0.3:8090 -> http %{http_code}\n' --max-time 2 http://127.0.0.3:8090/
direct to 127.0.0.3:8090 -> http 200
$ ssh -F /tmp/lx12lab/cfg -f -N -L 127.0.0.1:9100:127.0.0.3:8090 hostA; sleep 0.5; curl -s -o /dev/null -w 'through the forward -> http %{http_code}\n' --max-time 2 http://127.0.0.1:9100/
through the forward -> http 200
```

**D.** `ssh -f -N -L 127.0.0.1:9100:127.0.0.3:8090 jumphost` and then `curl http://127.0.0.1:9100/`: the page was reached through the forward (**http 200**). The bind address is **`127.0.0.1`**, so only processes on this machine can use the port; `0.0.0.0` would let the whole network use a tunnel into the private service. (The first line shows the service is reachable directly **in this lab**, because everything is on loopback; the real service would not be.) Add `-o ExitOnForwardFailure=yes` in a script, and `-J` if the service is behind two hops.

```text
--- Problem E: key login fails with the right key
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA true 2>&1 | tail -1
alice@127.0.0.1: Permission denied (publickey,keyboard-interactive).
$ grep -E 'Authentication refused|bad ownership' /tmp/lx12lab/hostA/log | tail -1 | sed -E 's/^[A-Za-z]+ +[0-9]+ [0-9:]+ [^ ]+ //'
Authentication refused: bad ownership or modes for directory /home/alice
$ ssh -F /tmp/lx12lab/cfg -o BatchMode=yes hostA 'echo fixed'
fixed
```

**E.** With `chmod 777 /home/alice` the client said only **`Permission denied (publickey,keyboard-interactive)`**. The **server log** said **`Authentication refused: bad ownership or modes for directory /home/alice`**: `StrictModes` found a directory that others can write, which would let them replace `authorized_keys`. **`chmod 750 /home/alice`** fixed it and the login printed `fixed`. Check **owner and mode** of the home directory, `.ssh` and `authorized_keys`, in that order, and read the server log at `VERBOSE`.

```text
--- Problem F: audit every command a user runs, and every write under /etc/ssh
$ auditctl -a always,exit -F arch=b64 -S execve -F uid=30033 -k u_exec; auditctl -w /etc/ssh -p wa -k ssh_cfg; auditctl -l
-a always,exit -F arch=b64 -S execve -F uid=30033 -F key=u_exec
-w /etc/ssh -p wa -k ssh_cfg
$ ausearch -if /tmp/lx12pa/a.log -k u_exec -i 2>&1 | grep -E 'type=EXECVE' | grep -o -E 'a0=[^ ]+' | tr '\n' ' '; echo
a0=sh a0=uname
$ ausearch -if /tmp/lx12pa/a.log -k ssh_cfg -i 2>&1 | grep -E 'type=PATH.*lab_marker' | grep -o -E 'name=[^ ]+|nametype=[A-Z]+' | tr '\n' ' '; echo
name=/etc/ssh/lab_marker nametype=CREATE
```

**F.** The rules are `-a always,exit -F arch=b64 -S execve -F uid=30033 -k u_exec` and `-w /etc/ssh -p wa -k ssh_cfg`. The command run as alice (`sh -c 'uname -r'`) gave **two `EXECVE` records, `a0=sh` and `a0=uname`** (the shell, then the program it started: a rule on `execve` sees each), and the new file gave **`name=/etc/ssh/lab_marker nametype=CREATE`**. Retrieve by key with `ausearch -k u_exec` and `ausearch -k ssh_cfg`; **make them permanent** in `/etc/audit/rules.d/`. (A run of this answer while the queue was small and rate limited from an earlier test lost the second event: the **audit settings persist in the kernel after `auditd` stops**, which is why the script resets `-b` and `-r` first.)

**G.** For the **99 percent that do not match**, 19,800 calls per second cost **about 100 to 350 ns** more each (740 less 644, and 965 less 616): **1.9 to 6.9 ms per second, 0.19 to 0.69 percent of one CPU**. For the **1 percent that match**, 200 calls per second cost **25,100 to 41,100 ns** more each: **5.0 to 8.2 ms per second, 0.50 to 0.82 percent**. The total is **about 0.7 to 1.5 percent of one CPU**, small. The **log** is the real cost: 200 events per second at about 880 bytes is **176 kB per second, about 15.2 GB per day**, so rotation, a separate partition and shipping off the host must be planned, and the rule should be **narrowed** (one path, one uid, only failures with `-F success=0`) until the events are the ones a person will read. A tighter alternative for a hot file is a `bpftrace` count (LX 11), which costs nothing in log but is not a tamper resistant record.

---

## 17. Cheat sheet

```text
ACCOUNTS    /etc/passwd name:x:uid:gid:gecos:home:shell | /etc/shadow name:hash:last:min:max:warn:inact:expire
            useradd -m -s SHELL | usermod -aG GROUP USER | passwd -l/-u/-S | chage -l/-M/-E/-I | userdel -r
            getent passwd NAME (follows NSS) | id | login.defs UID_MIN/UMASK/ENCRYPT_METHOD | '!' and '*' both show L
SUDO        visudo -cf FILE ; install -m 440 FILE /etc/sudoers.d/NAME ; sudo -l -U USER ; sudo -n CMD
            fixed command + full path ; never vi/less/find/tar/sh ; Defaults env_reset, secure_path, logfile
CAPS        /proc/PID/status CapPrm CapEff CapBnd ; capsh --decode=HEX ; getpcaps PID
            setcap cap_net_bind_service=+ep FILE ; getcap FILE ; setcap -r FILE ; lost by cp and rebuild
            setuid: ruid stays, euid = owner ; no_new_privs (setpriv --no-new-privs) stops it ; unshare -Ur
PAM         /etc/pam.d/SERVICE : type control module args ; auth account password session
            required (fail at end) requisite (fail now) sufficient (succeed now) [success=1 default=ignore]
            pamtester SERVICE USER authenticate|acct_mgmt|open_session ; pam_succeed_if access time limits faillock
SSH CLIENT  ssh-keygen -t ed25519 ; -lf (fingerprint) ; -F/-R NAME -f known_hosts ; ssh-copy-id
            -J HOST (ProxyJump) ; -L bind:port:host:hostport ; -R ... ; -D port (SOCKS) ; -N -f ; ExitOnForwardFailure
            ControlMaster auto ; ControlPath ; ControlPersist 60 ; -O check|exit ; ssh-add -l ; avoid -A
            authorized_keys options: command= restrict from= permitopen= ; modes 700 .ssh 600 file ; StrictModes
SSHD        sshd -t (syntax) ; sshd -T [-C user=,addr=,host=] (effective) ; diff against defaults ; LogLevel VERBOSE
            PermitRootLogin no ; PasswordAuthentication no ; KbdInteractiveAuthentication no ; AllowUsers
            MaxAuthTries 3 ; LoginGraceTime 20 ; ClientAliveInterval 30 ; DisableForwarding yes ; Match last
CERTS       ssh-keygen -s CA -I ID -n PRINCIPALS -V +1h -z SERIAL [-O force-command= -O source-address=] KEY.pub
            -L -f CERT ; TrustedUserCAKeys ; AuthorizedPrincipalsFile ; -h host cert + HostCertificate
            @cert-authority HOSTS KEY in known_hosts ; revoke: ssh-keygen -k -f KRL ; RevokedKeys
NFT         table inet T { set S {...} chain input { type filter hook input priority 0; policy drop; ... } }
            ct state established,related accept ; ip saddr @S ; counter ; limit rate N/second burst M packets
            nft -c -f FILE (check) ; nft -f FILE (atomic) ; nft list ruleset ; nft -a (handles) ; drop vs reject
            nat: type nat hook prerouting|output priority dstnat ; dnat to H:P ; iptables is a front end
AUDIT       auditctl -s | -l | -D | -w PATH -p rwxa -k KEY | -a always,exit -F arch=b64 -S SYSCALL -F FIELD -k KEY
            -b 8192 -r 0 -f 1|2 -e 1|2 ; ausearch -k KEY -i ; aureport --summary ; augenrules ; lost counter
            auid survives su/sudo ; -F arch=b64 required ; filter narrowly ; ship logs off the host
PACKAGES    Signed-By keyring ; InRelease -> Packages hash -> package ; gpgv ; dpkg -S/-L/-V ; debsums ; rpm -qf/-ql/-V
TIME        chronyc tracking | sources -v ; timedatectl ; slew vs step ; TZ=UTC ; smear vs step leap
MAC         SELinux: getenforce, ls -Z, restorecon, semanage fcontext, setsebool -P, ausearch -m avc, audit2why
            AppArmor: aa-status, aa-complain/enforce, aa-logprof ; fix the label, not setenforce 0
SYSCTL      randomize_va_space=2 dmesg_restrict=1 unprivileged_bpf_disabled>=1 suid_dumpable=0 tcp_syncookies=1
            rp_filter=1/2 accept_redirects=0 send_redirects=0 protected_symlinks=1 yama.ptrace_scope>=1
CHECKS      uid 0 only root | no empty hash | no NOPASSWD ALL | .ssh modes | world writable /etc | sshd -T | setuid list
NOT RUN HERE: systemd, real PAM logins and pam_faillock, SELinux, AppArmor, chronyd, dnf and rpm, multi-host firewalls
```

---

## 18. Sources

- `passwd(5)`, `shadow(5)`, `group(5)`, `nsswitch.conf(5)`, `getent(1)`, `useradd(8)`, `usermod(8)`, `chage(1)`, `login.defs(5)`: the account files, NSS and ageing.
- `sudoers(5)`, `sudo(8)`, `visudo(8)`: rule syntax, `Defaults`, `Cmnd_Alias`, logging. <https://www.sudo.ws/docs/man/sudoers.man/>
- `capabilities(7)`, `setcap(8)`, `getcap(8)`, `capsh(1)`, `credentials(7)`, `user_namespaces(7)`, and the kernel document *No New Privileges Flag*. <https://man7.org/linux/man-pages/man7/capabilities.7.html>
- Linux-PAM System Administrators' Guide and the module manuals (`pam_succeed_if(8)`, `pam_access(8)`, `pam_time(8)`, `pam_limits(8)`, `pam_faillock(8)`), and `pamtester(1)`. <https://www.chiark.greenend.org.uk/doc/libpam-doc/html/>
- `ssh(1)`, `ssh_config(5)`, `sshd(8)`, `sshd_config(5)`, `ssh-keygen(1)` (certificates, KRL), `ssh-agent(1)`, and the OpenSSH release notes. <https://www.openssh.com/manual.html>
- Mozilla *OpenSSH guidelines* and the CIS benchmarks for the distribution you run: the baseline for `sshd_config` and `sysctl`. <https://infosec.mozilla.org/guidelines/openssh>
- `nft(8)` and the nftables wiki (*Quick reference*, *Sets*, *Atomic rule replacement*, *Rate limit*). <https://wiki.nftables.org/>
- `auditctl(8)`, `auditd(8)`, `ausearch(8)`, `aureport(8)`, `audit.rules(7)`, and the Red Hat *Security Hardening* guide for audit. <https://access.redhat.com/documentation/en-us/red_hat_enterprise_linux/9/html/security_hardening/auditing-the-system_security-hardening>
- Debian *SecureApt* and the Ubuntu documentation on `deb822` sources and `Signed-By`; `dpkg(1)` (`-V`, `-S`), `debsums(1)`, `gpgv(1)`, `rpm(8)`. <https://wiki.debian.org/SecureApt>
- Red Hat *Using SELinux* and the SELinux Notebook; the Ubuntu and AppArmor documentation (`apparmor(7)`, `aa-status(8)`). <https://selinuxproject.org/page/NB_MLS> and <https://gitlab.com/apparmor/apparmor/-/wikis/Documentation>
- `chrony` documentation (`chronyc tracking`, `makestep`, `leapsecmode`), RFC 5905 (NTPv4). <https://chrony-project.org/documentation.html>
- Kernel documentation: *Linux Security Modules*, *Landlock*, *Audit*, and the sysctl reference for `kernel.*`, `fs.*`, `net.ipv4.*`. <https://docs.kernel.org/admin-guide/sysctl/>
- Michael Lucas, *SSH Mastery*, 2nd edition (2018); Michael Kerrisk, *The Linux Programming Interface* (2010), chapters 8 and 39 (users, capabilities); Brendan Gregg, *Systems Performance*, 2nd edition (2020) for measuring the cost of controls.
