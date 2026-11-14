# OS 20: Protection and security

*Part of the [Operating systems plan](00_plan.md) · Time: about 11 hours (8 reading and experiments, 3 problems) · You need: [OS 01](01_os_structure_and_system_calls.md) (system calls and the kernel as a service), [OS 15](15_file_systems.md) (inodes, directories, permission bits), [OS 18](18_ipc_mechanisms.md) (descriptors passed over a socket, which is the capability idea in practice) and [OS 19](19_user_kernel_boundary.md) (the boundary where every check in this tutorial runs). The hardware side of the same story (rings, NX, SMEP, SMAP, the speculative-execution attacks) is in [CO 30](../04_computer_organization/30_hardware_security.md) and is not repeated here.*

This is a new tutorial; the plan had a one-line row for it. It answers the questions a systems interview asks once the discussion reaches "and how do you make it safe": what decides whether this process may open that file, and what are the bits? What is the difference between the real, effective and saved user ID, and what does a `setuid` program do with them? Why does `root` exist as more than a user ID, and what are Linux capabilities for? What is an access-control list and how does it differ from a capability list? What do SELinux, AppArmor and Landlock add that file permissions cannot? How do you confine a process that you do not trust (`no_new_privs`, `seccomp`, namespaces), and what does each layer stop? What do ASLR, non-executable memory and the stack canary stop, and what do they not? And how should a password be stored? Each section ends in a program whose output is printed, so the claims are checked on a kernel rather than recited.

**Four honest limits, stated first.** (1) The machine is a Firecracker micro-VM and the programs run as `root`; the identity tests fork a child and drop to another user ID with `setresuid`, so what is shown is what that child sees. (2) **SELinux and AppArmor were not run.** The kernel image has SELinux compiled in (read from `/proc/config.gz`), but `/sys/fs/selinux` and `/sys/kernel/security` do not exist and `/proc/self/attr/current` returns `EINVAL`, so I cannot list the active security modules; my statements about SELinux and AppArmor are **from memory**. **Landlock is active** (ABI version 7) and is demonstrated for real. (3) There is no `perf`, no `libcap` and no `libacl` here, so capabilities and ACLs are driven through raw `capget`, `capset` and extended attributes, which has the side effect of showing the actual data formats. (4) The cost of the Spectre and Meltdown mitigations is not measured (see [OS 19](19_user_kernel_boundary.md)). Anything I did not run or fetch is marked "from memory".

By the end you can:

- state the access-matrix model and say which cell a Unix permission check reads (measured: the owner class wins even when it grants less, so a file of mode `0044` could not be read by its owner and could be read by a stranger);
- explain what the directory bits mean (measured: a directory without `x` refuses every name inside it while `ls` still works, and with `r` removed it is the reverse), why removing a file needs write permission on the **directory** and not on the file, and what the sticky bit changes (measured: a stranger could unlink a mode-0000 file in a mode-0777 directory and got `EPERM` once the directory was `1777`);
- trace real, effective and saved user IDs through a `setuid` program, and drop privilege so that it cannot come back (measured: after `setresuid(r, r, r)` a call to `seteuid(0)` returned `EPERM`);
- show that an open descriptor is a capability (measured: a child that could not open a file by name read 7 bytes through the inherited descriptor, even after the file was changed to mode 0000);
- read the capability sets in `/proc/self/status` and say which capability each root privilege needs (measured: dropping `CAP_DAC_OVERRIDE` and `CAP_DAC_READ_SEARCH` made root unable to open a mode-0000 file, and `CapPrm 0000000000000400` was enough to bind port 80 as user 65534);
- read and write a POSIX ACL as the raw extended attribute and say what the mask does (measured: lowering the mask to `r--` took write permission away from a named user whose entry said `rw-`, and `chmod 0600` set the mask to `---`);
- explain why `no_new_privs` exists (measured: a `setuid`-root program gave euid 0 to user 65534 without the flag, and euid 65534 with it, and a seccomp filter was refused with `EACCES` without it);
- price a seccomp filter (measured: a filter that looks only at the syscall number added about 10 ns to `getppid` whatever its length, because the kernel precomputes its verdict, while a filter that also reads an argument grew to +63 to +84 ns at 401 compares over three runs);
- say what a user namespace gives and withholds (measured: an unprivileged user became uid 0 inside it and could mount a `tmpfs` and bind port 80, and still got `EPERM` from `sethostname`, `settimeofday` and `mknod`, and `EACCES` on `/etc/shadow`);
- show that a Linux security module can bind root (measured: Landlock denied `/etc/passwd` and TCP port 8080 to a process with every capability);
- quantify address-space randomisation and non-executable memory (measured: 300 runs gave 28 varying bits for the executable, `libc`, the heap and `mmap`, 22 for the stack, and a call into a writable page raised `SIGSEGV`);
- say what the stack protector does and does not catch (measured: with `-fstack-protector-strong` a 16-byte buffer overwritten by 17 and 24 bytes went unnoticed and 40 bytes aborted with "stack smashing detected");
- choose and price a password hash (measured: one yescrypt hash took about 15 ms against 0.3 ms for the cheapest SHA-512-crypt setting).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run as root on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory (glibc 2.39, x86_64, an Intel Xeon at a nominal 2.10 GHz). Nine programs were written for this tutorial; the key functions are printed in the text and their output is pasted unedited. The kernel build options quoted come from `/proc/config.gz` on this machine. Kernel behaviour was checked against the manual pages `capabilities(7)`, `seccomp(2)`, `credentials(7)`, `user_namespaces(7)` and `acl(5)` and the kernel's `no_new_privs` document, listed under Sources with what each did and did not confirm.

**Data and timing.** Counts and error codes are deterministic and repeatable. Timings (the seccomp filter cost, the password hashes) are the best or median of repeated batches, and the VM adds noise: the baseline `getppid` cost moved between 89 and 113 ns from one run to the next, so differences smaller than about 15 ns are not meaningful and I quote ranges where it matters.

---

## 1. The model: subjects, objects and the access matrix

### 1.1 Who may do what to what

Protection is the part of an operating system that decides whether an **active entity** (a **subject**: a process, acting for a user) may perform an **operation** on a **resource** (an **object**: a file, a socket, a memory page, a device). The textbook model is the **access matrix**:

```text
                 file A      file B       socket S     /dev/mem
  process of u1  read write  -            connect      -
  process of u2  read        read write   -            -
  process of u3  -           read         -            -
  root process   all         all          all          all
```

Each row is a **domain** (the set of rights a subject holds); each cell is a set of operations. The matrix is far too big to store, so real systems store one of its slices:

- **By column, an access-control list (ACL).** Each object carries the list of who may do what to it. Unix permission bits are a tiny ACL (owner, group, others), and POSIX ACLs (section 5) extend them. Advantages: it is natural to ask "who can read this file?" and to revoke a right by editing the object. Cost: the check must identify the **subject**, so rights follow identity.
- **By row, a capability list.** Each subject carries a list of unforgeable tokens, each naming an object and the rights over it. A file descriptor is one (section 3.4). Advantages: no ambient authority, because you can only use what you were handed, and passing a token to a helper (over a socket, section 5 of [OS 18](18_ipc_mechanisms.md)) delegates exactly that right. Cost: revocation ("take back every copy of this descriptor") is hard, and "who can reach this object?" requires walking all subjects.

Linux uses both, and mixes the word: a descriptor is a capability in the textbook sense, whereas "Linux capabilities" (section 4) are something else, a splitting of root's privileges into units. Keep the two meanings apart in an interview.

### 1.2 The reference monitor and where the check runs

The checks are made by the kernel at the point of use, on every access, inside the system call path of [OS 19](19_user_kernel_boundary.md). A design that satisfies three properties is called a **reference monitor**: it is **always invoked** (complete mediation, no way around it), **tamper-proof** (the checking code and its data are not writable by the subjects), and **small enough to verify**. In Linux the mediation points are the system calls and, inside the kernel, the **LSM hooks** (section 6), a few hundred calls placed at each security-relevant operation.

Two timing facts matter for everything below and recur in the exercises:

- **A file is checked when it is opened, not when it is read.** The open compares the caller's credentials with the inode and, if it passes, hands back a descriptor that carries the granted access mode. Later `read` and `write` calls on the descriptor check only that mode (section 3.4).
- **Credentials belong to the process (more exactly, to each thread),** not to the program file. Running a program does not change who you are, unless the file's `setuid` bit or file capabilities say so (sections 3 and 4).

### 1.3 Design principles worth quoting

Saltzer and Schroeder's list (from memory, 1975) is still the vocabulary of a design review: **least privilege** (every subject runs with the minimum rights it needs), **fail-safe defaults** (deny unless permitted), **complete mediation**, **economy of mechanism** (small and simple), **open design** (secrecy of the design is not the security), **separation of privilege**, **least common mechanism**, **psychological acceptability**. The rest of the tutorial is mostly least privilege made concrete: drop what you do not need (sections 3 and 4), then restrict what remains (sections 6 and 7), then make the leftovers hard to exploit (section 8).

A final distinction used all the way through: **discretionary access control (DAC)** lets the owner of an object decide who gets access (permission bits, ACLs, `chmod`). **Mandatory access control (MAC)** is a policy set by an administrator or by the program that applies it, which the owner cannot change (SELinux, AppArmor, Landlock, section 6). The kernel checks both, and an access needs both to say yes.

---

## 2. Unix permission bits

### 2.1 The check

Each inode stores an owner user ID, a group ID and a 12-bit mode: nine permission bits (`rwx` for the **owner class**, the **group class**, the **other class**) and three special bits (`setuid`, `setgid`, sticky). When a process opens a file the kernel makes one decision, with the **filesystem user ID** and group IDs of the process (the effective IDs unless `setfsuid` was used, `credentials(7)`, fetched):

1. If the process may bypass the check through a capability (root, section 4), it passes.
2. Else if its user ID equals the file's owner, **only the owner bits** are consulted.
3. Else if its group ID or any supplementary group equals the file's group, only the group bits.
4. Else the other bits.

The consequence that surprises people is in step 2: **the first matching class decides, even when a later class would have granted more.** The program `n1_perm.c` creates a file owned by user 1001 and group 2000 and tries four identities against five modes. Each attempt is made by a child that has dropped to the identity with `setresuid`:

```c
static void drop_to(int uid, int gid)
{
    if (setgroups(0, NULL) < 0 || setresgid(gid, gid, gid) < 0 || setresuid(uid, uid, uid) < 0) { perror("drop"); _exit(9); }
}

static void who_can(const char *label, int uid, int gid, const char *path)
{
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        drop_to(uid, gid);
        int r = try_open(path, O_RDONLY); const char *rs = code(r);
        int w = try_open(path, O_WRONLY); const char *ws = code(w);
        printf("  %-18s read %-7s write %-7s\n", label, rs, ws);
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
}
```

```text
file /tmp/n1/f owned by uid 1001, group 2000
 mode 0640
  owner (1001)       read ok      write ok
  group member       read ok      write EACCES
  stranger           read EACCES  write EACCES
  root               read ok      write ok
 mode 0604
  owner (1001)       read ok      write ok
  group member       read EACCES  write EACCES
  stranger           read ok      write EACCES
  root               read ok      write ok
 mode 0460
  owner (1001)       read ok      write EACCES
  group member       read ok      write ok
  stranger           read EACCES  write EACCES
  root               read ok      write ok
 mode 0044
  owner (1001)       read EACCES  write EACCES
  group member       read ok      write EACCES
  stranger           read ok      write EACCES
  root               read ok      write ok
 mode 0000
  owner (1001)       read EACCES  write EACCES
  group member       read EACCES  write EACCES
  stranger           read EACCES  write EACCES
  root               read ok      write ok
```

How to read it:

- **Mode `0640`** is the textbook case: owner reads and writes, the group reads, a stranger gets nothing. Root passed everything.
- **Mode `0604`: the group member was refused while the stranger was allowed.** The group member matched step 3, whose bits are `---`, and never reached the other bits (`r--`). This is the shape of a real bug: someone tries to "exclude one group" this way and it works, but someone else assumes "other" means "everyone not otherwise listed" and is surprised that a group member is worse off than a stranger.
- **Mode `0044`: the owner could neither read nor write, and everyone else could read.** The owner matched step 2 and got `---`. The owner can `chmod` the file back (that needs ownership, not permission bits), so this is a nuisance, not a lock, but it shows that the classes are checked in order and not combined.
- **Mode `0000`: root still read and wrote.** Root bypasses the permission bits, with one exception the kernel enforces: `execute` needs at least one `x` bit set (from memory; not tested here).
- **The error is `EACCES`** for a permission failure on a path, and `EPERM` for an operation that needs ownership or a privilege (`chmod` on someone else's file, `unlink` in a sticky directory). The two are different in the programs below and in your error handling.

### 2.2 Directories are lists, and their bits mean different things

A directory is a file whose contents are a table of names and inode numbers ([OS 15](15_file_systems.md)). The three bits keep their letters but not their meaning:

| Bit on a directory | Permits |
| --- | --- |
| `r` | **list** the names (read the table) |
| `w` | **change** the table: create, delete and rename entries, **provided `x` is also set** |
| `x` | **traverse**: use the directory as a step in a path (look up a name that you already know) |

The program makes a directory owned by 1001 that holds a world-readable file, and varies the directory's mode as a stranger:

```c
static void sect_dirs(void)
{
    char d[64], f[96];
    snprintf(d, sizeof d, "%s/d", D);
    snprintf(f, sizeof f, "%s/d/inner", D);
    mkdir(d, 0777);
    IGN(chmod(d, 0755));
    put(f, 0644, OWNER, GRP);
    IGN(chown(d, OWNER, GRP));
    printf("\ndirectory %s (owner 1001) holds a file mode 0644 that a stranger could read\n", d);
    int dm[] = { 0755, 0744, 0655, 0711 };
    for (int i = 0; i < 4; i++) {
        IGN(chmod(d, dm[i]));
        fflush(stdout);
        pid_t p = fork();
        if (p == 0) {
            drop_to(STRANGER, 3000);
            int r = try_open(f, O_RDONLY);
            const char *a = code(r);
            DIR *dir = opendir(d);
            const char *b = dir ? "ok" : code(-1);
            if (dir) closedir(dir);
            printf("  dir mode %04o as stranger: open file by name %-7s list the directory %-7s\n", dm[i], a, b);
            fflush(stdout);
            _exit(0);
        }
        waitpid(p, NULL, 0);
    }
    /* write bit on the directory is what allows removing a file, not the file's own mode */
    IGN(chmod(d, 0777));
    put(f, 0000, OWNER, GRP);
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        drop_to(STRANGER, 3000);
        int rc = unlink(f);
        printf("  dir 0777, file 0000 owned by someone else: stranger unlink -> %s\n", code(rc));
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
    /* sticky bit */
    put(f, 0644, OWNER, GRP);
    IGN(chmod(d, 01777));
    fflush(stdout);
    p = fork();
    if (p == 0) {
        drop_to(STRANGER, 3000);
        int rc = unlink(f);
        printf("  dir 1777 (sticky), file owned by someone else: stranger unlink -> %s\n", code(rc));
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
    p = fork();
    if (p == 0) {
        drop_to(OWNER, GRP);
        int rc = unlink(f);
        printf("  dir 1777 (sticky), same file: its owner unlink -> %s\n", code(rc));
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
}
```

```text
directory /tmp/n1/d (owner 1001) holds a file mode 0644 that a stranger could read
  dir mode 0755 as stranger: open file by name ok      list the directory ok
  dir mode 0744 as stranger: open file by name EACCES  list the directory ok
  dir mode 0655 as stranger: open file by name ok      list the directory ok
  dir mode 0711 as stranger: open file by name ok      list the directory EACCES
  dir 0777, file 0000 owned by someone else: stranger unlink -> ok
  dir 1777 (sticky), file owned by someone else: stranger unlink -> EPERM
  dir 1777 (sticky), same file: its owner unlink -> ok
```

How to read it:

- **`0755`: both work. `0744` (no `x` for others): you can list the names but not open any of them, the `EACCES` is on the lookup.** This is the "I can see that the file exists and cannot touch it" state. Note that `0744` still **lists** the names, so the names themselves are not secret.
- **`0711` (no `r` for others): the stranger could open the file by name, and could not list the directory.** This is a standard trick: a directory that others can pass through but not browse, so that only people who know the exact name get in. It is secrecy by obscurity, so only as strong as the name (compare section 5.3 of [OS 18](18_ipc_mechanisms.md), where a `memfd` replaces the name altogether).
- **Deleting a file is an operation on the directory.** In a mode-`0777` directory the stranger removed a mode-`0000` file owned by someone else (`unlink -> ok`): the file's own bits do not matter, only write and search permission on the directory that holds the name. This is why an ordinary world-writable directory is a hazard.
- **The sticky bit fixes that.** With mode `1777` (as `/tmp` has), the same `unlink` by the stranger returned `EPERM`, and the file's owner could remove it. The rule: in a sticky directory a name may be removed or renamed only by the owner of the file, the owner of the directory or a privileged process (`chmod(2)` and `inode(7)`, from memory; the behaviour is what the output shows).
- **Path lookup checks every directory on the way.** To open `/a/b/c` you need `x` on `/`, `/a`, `/a/b` and then the permission on `c`. A file that looks readable can still be unreachable, and a descriptor you already hold is unaffected (section 3.4).

### 2.3 `umask` and the creation mode

A program that creates a file passes a mode (`open(path, O_CREAT, 0666)`) and the kernel clears the bits set in the process's **umask**: `mode & ~umask`. The umask is inherited across `fork` and `exec`, so a shell setting it affects every program it starts.

```c
static void sect_umask(void)
{
    printf("\n");
    mode_t masks[] = { 022, 077, 0 };
    for (int i = 0; i < 3; i++) {
        umask(masks[i]);
        char path[64];
        snprintf(path, sizeof path, "%s/um", D);
        unlink(path);
        int fd = open(path, O_WRONLY | O_CREAT, 0666);
        close(fd);
        struct stat st;
        stat(path, &st);
        printf("  umask %03o, open(..., O_CREAT, 0666) gives mode %04o\n", masks[i], st.st_mode & 07777);
    }
    umask(022);
}
```

```text
  umask 022, open(..., O_CREAT, 0666) gives mode 0644
  umask 077, open(..., O_CREAT, 0666) gives mode 0600
  umask 000, open(..., O_CREAT, 0666) gives mode 0666
```

The umask is a **cap on creation**, not a setting: it can only remove permissions, never add them, and an explicit `chmod` ignores it. Two uses: a daemon that writes secrets should set `umask(077)` first, and `0666` and `0777` in code are "the most the caller will allow", not "the result". POSIX ACLs add one more path, because a directory's **default ACL** replaces the umask rule for new files inside it (`acl(5)`, fetched).

---

## 3. Identities: real, effective and saved IDs

### 3.1 Four IDs per process

A process has several user IDs and several group IDs (`credentials(7)`, fetched):

- the **real** UID, who started the process (preserved across `execve`);
- the **effective** UID, used by the kernel for most permission decisions on shared objects;
- the **saved set-user-ID**, a copy of the effective UID set at `exec` time, so that a program can **drop** privilege and later **regain** it by switching the effective UID between the real and the saved values;
- the **filesystem UID** (a Linux addition), used for file permission checks, normally equal to the effective UID and changed together with it.

Plus a primary group, a list of supplementary groups (up to 65,536 on current kernels, `credentials(7)`), and the same trio for groups. The one number most code needs is the effective UID, and the one most bugs are about is the saved one.

### 3.2 `setuid` programs

When the `setuid` bit is set on an executable owned by user U, `execve` sets the new process's effective and saved UID to U, leaving the real UID alone (`credentials(7)` leaves the details to `execve(2)`; the result is what the output shows). The standard examples are `passwd` (needs to write `/etc/shadow`), `su`, `sudo` and `ping` on older systems (needed a raw socket). The program runs as the owner for its whole life, which is why every line of a `setuid` program is part of the trusted computing base.

The test program copies itself to `/tmp/n1/prog`, makes the copy `root`-owned with mode `4755`, and then a stranger (uid 1003) runs it. The program prints its IDs, reads a file only root can read, drops privilege temporarily, regains it, and finally drops it for good:

```c
static void ids(const char *tag)
{
    uid_t r, e, s;
    gid_t gr, ge, gs;
    getresuid(&r, &e, &s);
    getresgid(&gr, &ge, &gs);
    printf("  %-38s ruid %5u euid %5u suid %5u | rgid %5u egid %5u\n", tag, r, e, s, gr, ge);
}

static void show(void)
{
    ids("inside the program after exec");
    int fd = open("/etc/shadow", O_RDONLY);
    printf("  open /etc/shadow for read: %s\n", code(fd));
    uid_t r, e, s;
    getresuid(&r, &e, &s);
    if (e == 0) {
        IGN(seteuid(r));
        ids("after seteuid(ruid): drop for now");
        fd = open("/etc/shadow", O_RDONLY);
        printf("  open /etc/shadow for read: %s\n", code(fd));
        IGN(seteuid(s));
        ids("after seteuid(suid): regain");
        IGN(setresuid(r, r, r));
        ids("after setresuid(r,r,r): gone for good");
        printf("  try to regain root: %s\n", code(seteuid(0)));
    }
}
```

```text
copy of this program: owner uid 0, mode 4755 (setuid root)
  before exec (stranger)                 ruid  1003 euid  1003 suid  1003 | rgid  3000 egid  3000
  inside the program after exec          ruid  1003 euid     0 suid     0 | rgid  3000 egid  3000
  open /etc/shadow for read: ok
  after seteuid(ruid): drop for now      ruid  1003 euid  1003 suid     0 | rgid  3000 egid  3000
  open /etc/shadow for read: EACCES
  after seteuid(suid): regain            ruid  1003 euid     0 suid     0 | rgid  3000 egid  3000
  after setresuid(r,r,r): gone for good  ruid  1003 euid  1003 suid  1003 | rgid  3000 egid  3000
  try to regain root: EPERM
  inside the program after exec          ruid  1003 euid  1003 suid  1003 | rgid  3000 egid  3000
  open /etc/shadow for read: EACCES
```

How to read it:

- **The line after "before exec": `ruid 1003 euid 0 suid 0`.** The real UID is still the caller, the effective and saved are root, and `/etc/shadow` opened. This is the privilege the bit exists for.
- **`seteuid(ruid)`: the effective UID became 1003, the saved one stayed 0, and `/etc/shadow` returned `EACCES`.** This is a **temporary drop**: the program does the risky part (parsing user input, say) as an ordinary user, and can come back with `seteuid(suid)` for the one step that needs root. It works because the saved ID keeps the right.
- **`setresuid(r, r, r)`: all three equal 1003, and `seteuid(0)` returned `EPERM`.** This is a **permanent drop**: the saved ID no longer holds root, so no code in the process, including an attacker's, can return. A program that needs root only to start (open a port, read a key) should drop permanently as soon as it has what it needs. Using `setuid(uid)` from a root process also sets all three, and from a non-root process it only sets the effective one, which is why portable code uses `setresuid` and **checks the return value**.
- **The last two lines are the same file without the `setuid` bit**: `euid 1003` and `EACCES`. The kernel changes nothing when the bit is absent.
- **Order matters when dropping.** Clear the supplementary groups and set the group ID first (`setgroups(0, NULL)`, `setresgid`), then the user ID last: once the user ID is no longer root, the process may no longer change its groups. A program that drops the user ID first and keeps root's supplementary groups (such as `disk` or `shadow`) is still privileged in ways `id` will show. The `drop_to` function above is the correct order.

### 3.3 What can go wrong with `setuid`

`setuid` programs are the classic escalation target because they run with the owner's authority in an environment the **attacker controls**: environment variables, current directory, open descriptors, signals, `argv`, resource limits and the timing of races. The mitigations the kernel and the dynamic linker apply (from memory): the loader ignores `LD_PRELOAD` and similar variables for such programs ("secure-execution mode"), `execve` clears the dumpable flag so the caller cannot `ptrace` them, and a `nosuid` mount option makes the kernel ignore the bit on that filesystem. The robust advice is not to write new ones: give a service the one capability it needs (section 4) or put a small daemon behind a socket and check the peer's credentials (`SO_PEERCRED` in [OS 17](17_ipc_fundamentals.md), and `SCM_CREDENTIALS` in section 5.4 of [OS 18](18_ipc_mechanisms.md)).

### 3.4 A descriptor is a capability

The previous sections described credentials checked against the object by **name** (path lookup, then the open). A **descriptor** is the result of that check, and it carries the right with it. The program opens a mode-0600 root-owned file as root (descriptor 3), then forks a child that drops to uid 1003 (the descriptor stays open), and later has root change the file to mode `0000` and runs a second child:

```c
static void sect_fd(void)
{
    printf("\n");
    char path[64];
    snprintf(path, sizeof path, "%s/cap", D);
    put(path, 0600, 0, 0);
    int fd = open(path, O_RDWR);
    printf("root opens a mode 0600 root-owned file, fd %d, then drops to uid %d\n", fd, STRANGER);
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        drop_to(STRANGER, 3000);
        char b[16];
        ssize_t n = pread(fd, b, sizeof b, 0);
        printf("  read through the inherited descriptor: %zd bytes\n", n);
        printf("  open the same path by name: %s\n", code(try_open(path, O_RDONLY)));
        printf("  fchmod(fd, 0) as non-owner: %s\n", code(fchmod(fd, 0)));
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
    IGN(fchmod(fd, 0));
    p = fork();
    if (p == 0) {
        drop_to(STRANGER, 3000);
        char b[16];
        ssize_t n = pread(fd, b, sizeof b, 0);
        printf("  after root chmods the file to 0000, the held descriptor still reads: %zd bytes\n", n);
        /* reopen through /proc/self/fd: a fresh open is checked again */
        char pp[64];
        snprintf(pp, sizeof pp, "/proc/self/fd/%d", fd);
        printf("  open(\"%s\") again: %s\n", pp, code(try_open(pp, O_RDONLY)));
        printf("  the descriptor's access mode flags: O_RDWR bit set = %d\n", (fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDWR);
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
}
```

```text
root opens a mode 0600 root-owned file, fd 3, then drops to uid 1003
  read through the inherited descriptor: 7 bytes
  open the same path by name: EACCES
  fchmod(fd, 0) as non-owner: EPERM
  after root chmods the file to 0000, the held descriptor still reads: 7 bytes
  open("/proc/self/fd/3") again: EACCES
  the descriptor's access mode flags: O_RDWR bit set = 1
```

How to read it:

- **The child read 7 bytes through the inherited descriptor and got `EACCES` on the same path by name.** Nothing in the read path asks who the caller is; the descriptor was checked once, at the open, and records "read and write allowed".
- **`fchmod` as a non-owner is refused (`EPERM`)**: holding a descriptor lets you use the object, not own it.
- **After `chmod 0000` the held descriptor still read.** Permission changes affect the **next** open, not descriptors already granted. Revocation by `chmod` is therefore incomplete for anything that has the file open, which is a standard point in a security review (to revoke you must also close or kill the holders).
- **A fresh open through `/proc/self/fd/3` failed** with `EACCES`, because that is a new lookup and a new check; the magic link does not carry the old right.
- **The right is the access mode in the descriptor** (`O_RDWR` here), which is why a process that is handed a read-only descriptor cannot turn it into a writable one by `fcntl`.

This is the working form of a **capability system**: the kernel's `SCM_RIGHTS` (section 5 of [OS 18](18_ipc_mechanisms.md)) copies a descriptor into another process, and a privileged broker can open files that a sandboxed worker could never open itself and hand it only the one descriptor it needs. That pattern (a small privileged process and a large unprivileged one, communicating over a Unix socket) is how Chrome, `systemd` socket activation, OpenSSH privilege separation and many DBMS drivers are built (from memory), and it is the practical answer to "how do you let a sandboxed process open a user-chosen file?"

---

## 4. Linux capabilities: root, divided

### 4.1 The problem with one all-powerful user ID

Traditional Unix has a single test, "is the effective UID 0?", for everything privileged: binding a port below 1024, mounting a file system, loading a module, changing the clock, bypassing file permissions, killing any process. A program that needs only one of these (a web server binding port 80, `ping` opening a raw socket) must hold all of them, and a bug in it hands over the machine. Linux capabilities (`capabilities(7)`, fetched) split the power of root into **units that can be held, dropped and inherited separately**. The header on this machine defines 41 of them, numbered 0 to 40, and `/proc/sys/kernel/cap_last_cap` reads 40 (measured), so a capability set fits in 64 bits and is printed as a hexadecimal mask.

The ones that come up in interviews, with what the manual page says each permits (fetched, paraphrased):

| Capability (bit) | What it allows |
| --- | --- |
| `CAP_CHOWN` (0) | arbitrary changes of file owner and group |
| `CAP_DAC_OVERRIDE` (1) | bypass file read, write and execute permission checks |
| `CAP_DAC_READ_SEARCH` (2) | bypass read checks on files and read and search checks on directories |
| `CAP_FOWNER` (3) | bypass checks that require the caller to be the **owner** (`chmod`, `utime`), set ACLs and inode flags on any file |
| `CAP_SETUID` (7) | arbitrary UID changes, forging a UID over a Unix socket, writing a user namespace's UID map |
| `CAP_SETPCAP` (8) | change capability sets; drop from the bounding set |
| `CAP_NET_BIND_SERVICE` (10) | bind to ports below 1024 |
| `CAP_NET_ADMIN` (12) | interface, routing and firewall configuration, promiscuous mode and more |
| `CAP_NET_RAW` (13) | raw and packet sockets |
| `CAP_SYS_PTRACE` (19) | trace any process, read and write another process's memory |
| `CAP_SYS_ADMIN` (21) | mount, `setns`, `sethostname`, quota, and a very long list besides |
| `CAP_SYS_RESOURCE` (24) | override resource limits and quotas |
| `CAP_SYS_TIME` (25) | set the system clock |
| `CAP_SETFCAP` (31) | set file capabilities |
| `CAP_BPF` (39), `CAP_PERFMON` (38), `CAP_CHECKPOINT_RESTORE` (40) | split out of `CAP_SYS_ADMIN` by newer kernels |

The manual page itself calls `CAP_SYS_ADMIN` **overloaded**, advises kernel developers to avoid adding to it, and says that in effect it is "the new root". When you review a container configuration, a grant of `CAP_SYS_ADMIN` is the one to question first, because it is nearly as strong as running as root.

### 4.2 The sets, and what this process starts with

Each thread has five capability sets (the definitions are from `capabilities(7)`, fetched):

- **Permitted**: the upper limit of what the thread may have in its effective set. A capability can be dropped from it and never regained (without an `exec` that restores it).
- **Effective**: the set the kernel actually checks. A capability in permitted but not in effective is **available but not active**: the thread can raise it into effective and use it, and it has not exposed itself in the meantime.
- **Inheritable**: capabilities that may be passed through an `execve` (together with the file's inheritable set).
- **Ambient**: capabilities that survive an `execve` of an unprivileged program, so a script can run with a capability without the program file being marked. Cleared on running a program with `setuid` or file capabilities.
- **Bounding**: a ceiling on what an `execve` can ever give the thread. Dropping from it needs `CAP_SETPCAP` and is permanent.

They are printed in `/proc/self/status` as `CapInh`, `CapPrm`, `CapEff`, `CapBnd` and `CapAmb`. The first line of the program's output shows the starting point of this sandbox:

```c
static void show_caps(const char *tag)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256], eff[32] = "", prm[32] = "", amb[32] = "";
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "CapEff: %31s", eff) == 1) continue;
        if (sscanf(line, "CapPrm: %31s", prm) == 1) continue;
        if (sscanf(line, "CapAmb: %31s", amb) == 1) continue;
    }
    fclose(f);
    printf("  %-40s CapPrm %s CapEff %s CapAmb %s\n", tag, prm, eff, amb);
}
```

```text
capability sets of this process (root) in /proc/self/status
  at start                                 CapPrm 000001fffeffffff CapEff 000001fffeffffff CapAmb 0000000000000000
```

**The value `000001fffeffffff` is every capability except bit 24**, `CAP_SYS_RESOURCE` (`0x1ffffffffff` would be all 41; clearing bit 24, `0x1000000`, gives `0x1fffeffffff`). So even the "root" of this micro-VM is not the full root: whoever built the sandbox removed the ability to override resource limits. The uid is 0 and the effective set is 40 capabilities. The lesson for an interview: **"root" means "uid 0 and whatever capabilities the thread holds"**, and a container's root is usually trimmed in the same way.

### 4.3 How uid 0 and capabilities interact

The kernel keeps the old root semantics by tying capability sets to user-ID changes (`capabilities(7)`, fetched, paraphrased):

- If the real, effective or saved UID was 0 and **all three become non-zero**, the permitted, effective and ambient sets are **cleared**.
- Changing the **effective** UID from 0 to non-zero clears the effective set; changing it from non-zero to 0 copies permitted into effective.
- Changing the **filesystem** UID from 0 to non-zero clears a few effective capabilities that relate to files (`CAP_CHOWN`, `CAP_DAC_OVERRIDE`, `CAP_FOWNER` among them).
- **`PR_SET_KEEPCAPS`** (`SECBIT_KEEP_CAPS`) lets a thread keep its **permitted** set when its UIDs all become non-zero. The effective set is still cleared, and the flag is cleared by `execve`.

These rules are why "just call `setuid(nobody)`" at the end of a daemon's start-up also drops every capability, which is usually what you want, and why you must do something special to keep one.

### 4.4 Reading and changing the sets with raw system calls

There is no `libcap` on this machine, so the program calls `capget` and `capset` directly. The version-3 interface uses two 32-bit words per set, which is why a capability number `c` is bit `c % 32` of word `c / 32`:

```c
static int capget_(struct __user_cap_data_struct d[2])
{
    struct __user_cap_header_struct h = { _LINUX_CAPABILITY_VERSION_3, 0 };
    return (int)syscall(SYS_capget, &h, d);
}

static int capset_(struct __user_cap_data_struct d[2])
{
    struct __user_cap_header_struct h = { _LINUX_CAPABILITY_VERSION_3, 0 };
    return (int)syscall(SYS_capset, &h, d);
}

static void drop_cap(int cap)
{
    struct __user_cap_data_struct d[2];
    capget_(d);
    d[cap / 32].effective &= ~(1u << (cap % 32));
    d[cap / 32].permitted &= ~(1u << (cap % 32));
    d[cap / 32].inheritable &= ~(1u << (cap % 32));
    if (capset_(d) < 0) perror("capset");
}
```

The first test shows that root's file-permission bypass is made of **three separate capabilities**. A file with mode `0000` and owner 65534 is opened by uid 0 as its capabilities are removed one at a time:

```c
static void t_dac(void)
{
    IGN(mkdir("/tmp/n2", 0755));
    int fd = open("/tmp/n2/private", O_WRONLY | O_CREAT | O_TRUNC, 0);
    IGN(write(fd, "x", 1));
    close(fd);
    IGN(chown("/tmp/n2/private", NOBODY, NOBODY));
    IGN(chmod("/tmp/n2/private", 0));
    printf("  file mode 0000 owned by uid %d, opened by uid 0:\n", NOBODY);
    show_caps("root, full set");
    printf("    open for read: %s\n", code(try_open("/tmp/n2/private")));
    drop_cap(CAP_DAC_OVERRIDE);
    show_caps("after dropping CAP_DAC_OVERRIDE");
    printf("    open for read: %s\n", code(try_open("/tmp/n2/private")));
    drop_cap(CAP_DAC_READ_SEARCH);
    show_caps("after also dropping CAP_DAC_READ_SEARCH");
    printf("    open for read: %s\n", code(try_open("/tmp/n2/private")));
    printf("    chmod it back (owner is not us, CAP_FOWNER still held): %s\n", code(chmod("/tmp/n2/private", 0600)));
    drop_cap(CAP_FOWNER);
    printf("    chmod after dropping CAP_FOWNER too: %s\n", code(chmod("/tmp/n2/private", 0600)));
}
```

```text
1. Root is a bundle of capabilities
  file mode 0000 owned by uid 65534, opened by uid 0:
  root, full set                           CapPrm 000001fffeffffff CapEff 000001fffeffffff CapAmb 0000000000000000
    open for read: ok
  after dropping CAP_DAC_OVERRIDE          CapPrm 000001fffefffffd CapEff 000001fffefffffd CapAmb 0000000000000000
    open for read: ok
  after also dropping CAP_DAC_READ_SEARCH  CapPrm 000001fffefffff9 CapEff 000001fffefffff9 CapAmb 0000000000000000
    open for read: Permission denied
    chmod it back (owner is not us, CAP_FOWNER still held): ok
    chmod after dropping CAP_FOWNER too: Operation not permitted
```

How to read it:

- **Dropping `CAP_DAC_OVERRIDE` alone changed nothing for reading**: root could still open the file, because `CAP_DAC_READ_SEARCH` covers reads. The bitmask changed from `...fffffff` to `...ffffffd`: bit 1 cleared.
- **Dropping `CAP_DAC_READ_SEARCH` as well made the open fail with `Permission denied`.** Now root is an ordinary user, with no class bits that match, and the mode `0000` applies.
- **`chmod` of a file the caller does not own still worked** (`ok`), because `CAP_FOWNER` was still held; after dropping it too, `chmod` failed with `Operation not permitted`. The split matters in practice: a backup program needs `CAP_DAC_READ_SEARCH` to read everything and does not need `CAP_FOWNER` to change anything.
- **Dropping is permanent for the thread**; there is no call that gives the capability back (section 4.5 shows the failure).

### 4.5 The classic task: bind port 80 without being root

Ports below 1024 need `CAP_NET_BIND_SERVICE`. A service can therefore start as root, bind its port, then become an unprivileged user and keep running. The alternative shown here is to **keep only that one capability** while becoming the unprivileged user. Three attempts, each in a forked child:

```c
static void t_bind_keep(void)
{
    IGN(prctl(PR_SET_KEEPCAPS, 1));
    IGN(setresgid(NOBODY, NOBODY, NOBODY));
    IGN(setresuid(NOBODY, NOBODY, NOBODY));
    show_caps("after setuid with PR_SET_KEEPCAPS");
    printf("    bind port 80: %s\n", code(bind80()));
    struct __user_cap_data_struct d[2];
    capget_(d);
    d[0].effective = 0; d[1].effective = 0;
    d[0].permitted = 1u << CAP_NET_BIND_SERVICE; d[1].permitted = 0;
    d[0].inheritable = 0; d[1].inheritable = 0;
    printf("    capset(permitted = NET_BIND_SERVICE only, effective = none): %s\n", code(capset_(d)));
    show_caps("trimmed to one permitted capability");
    printf("    bind port 80 with it permitted but not effective: %s\n", code(bind80()));
    d[0].effective = 1u << CAP_NET_BIND_SERVICE;
    printf("    capset(effective = NET_BIND_SERVICE): %s\n", code(capset_(d)));
    show_caps("raised into effective");
    printf("    bind port 80: %s\n", code(bind80()));
    d[0].permitted = 0; d[0].effective = 0;
    capset_(d);
    show_caps("then dropped for good");
    printf("    bind port 80: %s\n", code(bind80()));
    d[0].permitted = 1u << CAP_NET_BIND_SERVICE;
    printf("    try to take the capability back: %s\n", code(capset_(d)));
}
```

```text
2. Port 80 as uid 65534, plain setuid
  uid 65534 after plain setuid             CapPrm 0000000000000000 CapEff 0000000000000000 CapAmb 0000000000000000
    bind port 80: Permission denied

3. Port 80 as uid 65534, capability kept across setuid
  after setuid with PR_SET_KEEPCAPS        CapPrm 000001fffeffffff CapEff 0000000000000000 CapAmb 0000000000000000
    bind port 80: Permission denied
    capset(permitted = NET_BIND_SERVICE only, effective = none): ok
  trimmed to one permitted capability      CapPrm 0000000000000400 CapEff 0000000000000000 CapAmb 0000000000000000
    bind port 80 with it permitted but not effective: Permission denied
    capset(effective = NET_BIND_SERVICE): ok
  raised into effective                    CapPrm 0000000000000400 CapEff 0000000000000400 CapAmb 0000000000000000
    bind port 80: ok
  then dropped for good                    CapPrm 0000000000000000 CapEff 0000000000000000 CapAmb 0000000000000000
    bind port 80: Permission denied
    try to take the capability back: Operation not permitted
```

How to read it:

- **Attempt 1, plain `setresuid(65534, ...)`**: every capability set became `0000000000000000` and `bind` on port 80 returned `Permission denied`. This is the "drop everything" default of section 4.3.
- **Attempt 2, with `PR_SET_KEEPCAPS` first**: after the change `CapPrm` was still `000001fffeffffff` but **`CapEff` was zero**, so the bind still failed. This is the state of "available but not active". It is also a trap: a process in this state looks privileged in `/proc` and is not.
- **`capset` can only remove from the permitted set, never add**: the program trimmed `CapPrm` to `0000000000000400` (bit 10, `CAP_NET_BIND_SERVICE`) and the bind failed again, since the effective set was still empty.
- **Raising bit 10 into the effective set made `bind` succeed.** This is the minimum privilege that serves the request: one capability, held by a user that owns no files and cannot override anything else.
- **Dropping it afterwards left everything at zero, and a `capset` that asked for the capability back failed with `Operation not permitted`.** A real server does this directly after binding, so a bug later in the program has no capability to abuse.

### 4.6 What `execve` does (not run here)

For a program that is run, `capabilities(7)` (fetched) gives the transformation, with `P` the thread's sets before, `F` the file's sets and `P'` after:

```text
P'(ambient)     = 0 if the file is privileged (has file capabilities, setuid or setgid), else P(ambient)
P'(permitted)   = (P(inheritable) AND F(inheritable)) OR (F(permitted) AND P(bounding)) OR P'(ambient)
P'(effective)   = P'(permitted) if the file's effective bit is set, else P'(ambient)
P'(inheritable) = P(inheritable)         P'(bounding) = P(bounding)
```

**File capabilities** are stored in the `security.capability` extended attribute, and setting them needs `CAP_SETFCAP`. They let `ping` be `cap_net_raw+ep` instead of `setuid` root: it can open a raw socket and do nothing else. I did not set one here, so the formula above is the manual page's, not something I ran; the tests above show the same sets on a running thread. Two interview points: a binary with file capabilities **is** a privileged program and needs the same care as a `setuid` one (the loader treats both as secure-execution, from memory), and the bounding set caps what any program can acquire, which is how a container runtime removes `CAP_SYS_ADMIN` from every program inside regardless of its file bits.

### 4.7 `no_new_privs`: the lock on the door behind you

All the gains in sections 3 and 4 come from `execve`: a `setuid` bit or a file capability gives the new program **more** than its caller had. That is a problem for every confinement mechanism, because a confined process can still run a `setuid` program such as `sudo` and escape. The thread-level flag `no_new_privs` (kernel document, fetched) says "an `execve` may never grant privileges": `setuid` and `setgid` bits do not change the IDs, file capabilities do not expand the permitted set, and security modules cannot relax their rules after the `exec`. The flag is inherited across `fork` and `execve` and **cannot be unset**. Set it with `prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)`.

```c
static void t_exec_nnp(void)
{
    IGN(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
    printf("  PR_SET_NO_NEW_PRIVS: get = %d\n", (int)prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0));
    IGN(setresgid(NOBODY, NOBODY, NOBODY));
    IGN(setresuid(NOBODY, NOBODY, NOBODY));
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        execl("/tmp/n1/prog", "/tmp/n1/prog", "show", (char *)NULL);
        _exit(9);
    }
    waitpid(p, NULL, 0);
}
```

```text
4. A setuid-root program run by uid 65534, without and with no_new_privs
  without:
  inside the program after exec          ruid 65534 euid     0 suid     0 | rgid 65534 egid 65534
  open /etc/shadow for read: ok
  after seteuid(ruid): drop for now      ruid 65534 euid 65534 suid     0 | rgid 65534 egid 65534
  open /etc/shadow for read: EACCES
  after seteuid(suid): regain            ruid 65534 euid     0 suid     0 | rgid 65534 egid 65534
  after setresuid(r,r,r): gone for good  ruid 65534 euid 65534 suid 65534 | rgid 65534 egid 65534
  try to regain root: EPERM
  with:
  PR_SET_NO_NEW_PRIVS: get = 1
  inside the program after exec          ruid 65534 euid 65534 suid 65534 | rgid 65534 egid 65534
  open /etc/shadow for read: EACCES
```

The same `setuid`-root program from section 3.2 was run by user 65534 twice. **Without the flag it got `euid 0`** and read `/etc/shadow`, and then performed its drop, regain and permanent drop. **With the flag set first, `PR_GET_NO_NEW_PRIVS` returned 1, the program ran with `euid 65534`, and `/etc/shadow` failed with `EACCES`**: the bit was ignored. The flag does not stop privilege changes that do not go through `execve`, such as a root process calling `setuid`, and it does not remove capabilities the thread already holds (the document says so). It exists to make unprivileged `seccomp` safe (section 7.2) and is the first line of every sandbox set-up.

---

## 5. POSIX ACLs

### 5.1 What they add

Nine permission bits cannot say "Alice may read, Bob may read and write, everybody else nothing". A POSIX ACL adds **named users and groups** to the file's own entries. An ACL has up to six kinds of entry (`acl(5)`, fetched):

| Entry | Meaning |
| --- | --- |
| `ACL_USER_OBJ` | the owner (the same as the owner bits) |
| `ACL_USER` | a named user, with a qualifier (the UID) |
| `ACL_GROUP_OBJ` | the owning group |
| `ACL_GROUP` | a named group, with a qualifier (the GID) |
| `ACL_MASK` | an **upper bound** on the named-user, named-group and owning-group entries |
| `ACL_OTHER` | everyone else |

An ACL is **valid** with exactly one each of `USER_OBJ`, `GROUP_OBJ` and `OTHER` (a "minimal ACL", equivalent to the mode bits), and any named entry requires exactly one `MASK`. The access check, in order (`acl(5)`, fetched, paraphrased):

1. If the effective UID is the owner, `USER_OBJ` decides.
2. Else if it matches a named user, the entry **and the mask** must both grant the access.
3. Else if the effective or a supplementary group matches the owning group or a named group, the matching entry and the mask must both grant it.
4. Else `OTHER` decides.

### 5.2 The attribute, byte for byte

The ACL lives in the extended attribute `system.posix_acl_access`: a 4-byte version (2) followed by 8-byte entries of a 16-bit tag, a 16-bit permission field (4 read, 2 write, 1 execute) and a 32-bit qualifier. There is no `setfacl` here, so the program writes it directly:

```c
static int set_acl(const struct ent *e, int n)
{
    unsigned char buf[4 + 8 * 8];
    uint32_t ver = 2;
    memcpy(buf, &ver, 4);
    memcpy(buf + 4, e, 8 * n);
    return setxattr(F, "system.posix_acl_access", buf, 4 + 8 * n, 0);
}
```

```c
    struct ent acl[] = {
        { USER_OBJ, 6, 0 }, { USER, 6, FRIEND }, { GROUP_OBJ, 4, 0 }, { MASK, 6, 0 }, { OTHER, 0, 0 },
    };
```

```text
  plain file, owner 1001 group 2000  no ACL attribute (No data available), mode 0640
    uid 1003 (not listed)  read EACCES  write EACCES

add user 1003 rw- with mask rw-
  setxattr: ok
  after setxattr                     mode 0660, attribute 44 bytes, version 2: user::rw- user:1003:rw- group::r-- mask::rw- other::---
    uid 1003 (named, rw-)  read ok      write ok
    uid 1004 (other)       read EACCES  write EACCES
```

How to read it:

- **The attribute is 44 bytes: 4 for the version and five 8-byte entries.** The kernel validates it: the program shows a 7-byte attribute and a named user without a mask were both refused with `EINVAL` (the last two lines of the output in section 5.3).
- **The file mode changed from `0640` to `0660` by itself.** The group bits of the mode **display the mask** once a mask exists, so the `rw-` in the middle is the mask's, and the owning group's own entry (`group::r--`) is hidden behind it. This is the source of the classic confusion: `ls -l` shows `rw-` for a group that has only `r--`.
- **User 1003, who is in no class, now reads and writes** through the named entry and the mask, and user 1004 still gets `EACCES`.

### 5.3 The mask and `chmod`

```text
lower the mask to r-- (what chmod g-w does)
  setxattr: ok
  mask r--                           mode 0640, attribute 44 bytes, version 2: user::rw- user:1003:rw- group::r-- mask::r-- other::---
    uid 1003 (rw- & mask r--) read ok      write EACCES

chmod 0600 on a file that has an ACL
  after chmod 0600                   mode 0600, attribute 44 bytes, version 2: user::rw- user:1003:rw- group::r-- mask::--- other::---
    uid 1003               read EACCES  write EACCES

wrong sizes are refused
  7-byte attribute: Invalid argument
  named user entry without a mask entry: Invalid argument
```

- **Lowering the mask to `r--` removed write permission from user 1003 even though the entry still says `rw-`.** Effective rights are the entry **and** the mask; the entry is an upper limit, the mask the actual one. This is what `chmod g-w` does on a file with an ACL, by design: the mode's group bits are the mask, so older programs that only know about `chmod` still revoke everything in the group class.
- **`chmod 0600` set the mask to `---`, and then user 1003 could not read.** A program that calls `chmod(path, 0600)` to "make it private" really does make it private, ACL or not: this is the property that keeps an ACL safe for programs that do not know about it. (A tool that reapplies the ACL's entries but forgets to reset the mask produces the opposite surprise: an entry that exists and is ineffective.)
- **Default ACLs** (`acl(5)`, fetched; not run here) apply to **directories only** and are copied to new files and directories created inside as their access ACL, trimmed by the mode passed to `open` or `mkdir`. So a shared project directory can give a group access to everything created in it without anyone running `chmod`. The page does not describe how `umask` interacts; I did not test it.

Where ACLs fit in a design: they are for the case where the group model does not (many different principals, rights that differ per file). They are a cost, because the effective access of a file is no longer visible in its mode, and many tools drop them (`cp` without `-p`, archive formats, some network file systems). When a group will do, use a group.

---

## 6. Mandatory access control and Linux security modules

### 6.1 What DAC cannot do

Everything so far is **discretionary**: the owner of a file, or root, decides. Two things DAC cannot express:

- **"A compromised web server must not read `/home` even though it runs as a user that can."** If the process is running as user `www` and some file is readable by `www`, the owner's choice decides. Confining the *program* rather than the user needs a rule outside the owners' control.
- **"Even root, even with every capability, must not do X."** Bypass capabilities defeat file permissions by definition.

**Mandatory access control (MAC)** adds a second, independent check, with a policy the file owners cannot change. In Linux it is implemented by the **Linux Security Modules (LSM)** framework: the kernel calls a hook (`security_inode_permission`, `security_socket_bind`, `security_file_open` and a few hundred more, from memory) at each security-relevant operation, and each loaded module may veto. The check comes **after** the ordinary DAC checks and can only refuse further. An access needs both.

### 6.2 What this kernel has

The configuration of the running kernel is readable in `/proc/config.gz`, and it records exactly which modules were built (measured):

```text
CONFIG_SECURITY=y
CONFIG_SECURITY_SELINUX=y          CONFIG_DEFAULT_SECURITY_SELINUX=y
CONFIG_SECURITY_LANDLOCK=y         CONFIG_SECURITY_LOCKDOWN_LSM=y     CONFIG_BPF_LSM=y
# CONFIG_SECURITY_APPARMOR is not set      # CONFIG_SECURITY_YAMA is not set
# CONFIG_SECURITY_SMACK is not set         # CONFIG_SECURITY_TOMOYO is not set
```

So here SELinux, Landlock, lockdown and the BPF LSM are built in, and AppArmor and Yama are not. That the kernel contains SELinux does not mean it is enforcing: `/sys/fs/selinux` does not exist and `/proc/self/attr/current` is not readable, so I treat it as **not active** on this machine (I could not list the active modules, because `securityfs` is not mounted). That is why section 6.3 describes SELinux and AppArmor from memory and section 6.4 runs the one that works.

### 6.3 SELinux and AppArmor (from memory, not run)

- **SELinux** labels every subject and object with a **security context** (`user:role:type:level`) and enforces a **policy** of allow rules over types, such as "processes of type `httpd_t` may read files of type `httpd_sys_content_t`". The default is deny. A process changes domain only at `exec`, by a **transition** rule. The policy is large (thousands of rules for a distribution) and is written by the distribution; the common operational tasks are `getenforce`, `setenforce 0` (permissive: log denials without enforcing), reading the audit log for `avc: denied` lines, `restorecon` to relabel files and `semanage` and booleans to adjust. Strength: fine-grained, labels live in the file system's extended attributes, and it confines root-owned daemons. Cost: policy complexity, and "turn it off" is a common bad answer.
- **AppArmor** attaches a **profile** to a program by **path**: which files it may read and write, which capabilities it may use, which network access it has, with an `enforce` and a `complain` mode. Strength: much easier to write and read than SELinux policy. Cost: it is path-based (a renamed or hard-linked file can be a different path), it has less expressive power for information-flow rules, and the profile must exist for each program.
- **Where each is found**: SELinux on Red Hat family distributions and Android; AppArmor on Ubuntu, Debian and SUSE (from memory). Both are **system-wide policy** set by an administrator.

### 6.4 Landlock: a MAC the process applies to itself

Landlock (added in kernel 5.13 and extended since, from memory; the version this kernel reports is below) is a security module designed for **unprivileged sandboxing**: a process describes a **ruleset** of what it will still be allowed to do, and the kernel enforces it for the process and its descendants **permanently**. No administrator, no policy file, no root. The rules cover file-system access by directory hierarchy and, in recent versions, TCP binding and connecting by port. A later `landlock_restrict_self` can only add further restriction; a layer is never removed. The kernel here reports **ABI version 7**.

```c
static void allow_path(int rs, const char *path, unsigned long long access)
{
    struct landlock_path_beneath_attr pb = { .allowed_access = access, .parent_fd = open(path, O_PATH | O_CLOEXEC) };
    if (pb.parent_fd < 0) { printf("  cannot open %s: %s\n", path, strerror(errno)); return; }
    if (syscall(SYS_landlock_add_rule, rs, LANDLOCK_RULE_PATH_BENEATH, &pb, 0) < 0) printf("  add_rule %s: %s\n", path, strerror(errno));
    close(pb.parent_fd);
}
```

```c
static void run(void)
{
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    printf("Landlock ABI version reported by the kernel: %ld; running as uid %d with CapEff full\n", abi, (int)geteuid());
    IGN(mkdir("/tmp/n8", 0755));
    IGN(mkdir("/tmp/n8/rw", 0755));
    IGN(mkdir("/tmp/n8/ro", 0755));
    int fd = open("/tmp/n8/ro/data", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    IGN(write(fd, "x", 1));
    close(fd);
    unlink("/tmp/n8/rw/new");
    unlink("/tmp/n8/ro/new");
    probe("before any rule");

    unsigned long long rd = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_EXECUTE;
    unsigned long long all = rd | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_REMOVE_FILE | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_REMOVE_DIR;
    struct landlock_ruleset_attr ra = { .handled_access_fs = all, .handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP };
    int rs = (int)syscall(SYS_landlock_create_ruleset, &ra, sizeof ra, 0);
    if (rs < 0) { printf("create_ruleset: %s\n", strerror(errno)); return; }
    allow_path(rs, "/usr", rd);
    allow_path(rs, "/tmp/n8/ro", rd);
    allow_path(rs, "/tmp/n8/rw", all);
    struct landlock_net_port_attr np = { .allowed_access = LANDLOCK_ACCESS_NET_BIND_TCP, .port = 9090 };
    printf("  add_rule for TCP port 9090: %s\n", code((int)syscall(SYS_landlock_add_rule, rs, LANDLOCK_RULE_NET_PORT, &np, 0)));
    printf("  restrict_self without no_new_privs, as root with CAP_SYS_ADMIN: %s\n", code((int)syscall(SYS_landlock_restrict_self, rs, 0)));
    probe("after landlock_restrict_self (rules: /usr and /tmp/n8/ro read-only, /tmp/n8/rw read-write, bind only 9090)");
    printf("    chmod /tmp/n8/ro/data to 0777:    %s\n", code(chmod("/tmp/n8/ro/data", 0777)));
    printf("    re-apply the same ruleset:         %s (a second layer can only narrow further)\n", code((int)syscall(SYS_landlock_restrict_self, rs, 0)));
    close(rs);
}
```

```text
Landlock ABI version reported by the kernel: 7; running as uid 0 with CapEff full
  before any rule
    read /usr/include/stdio.h:        ok
    read /etc/passwd:                 ok
    create /tmp/n8/rw/new:            ok
    create /tmp/n8/ro/new:            ok
    read /tmp/n8/ro/data:             ok
    bind TCP port 8080:               ok
    bind TCP port 9090:               ok
  add_rule for TCP port 9090: ok
  restrict_self without no_new_privs, as root with CAP_SYS_ADMIN: ok
  after landlock_restrict_self (rules: /usr and /tmp/n8/ro read-only, /tmp/n8/rw read-write, bind only 9090)
    read /usr/include/stdio.h:        ok
    read /etc/passwd:                 Permission denied
    create /tmp/n8/rw/new:            ok
    create /tmp/n8/ro/new:            Permission denied
    read /tmp/n8/ro/data:             ok
    bind TCP port 8080:               Permission denied
    bind TCP port 9090:               ok
    chmod /tmp/n8/ro/data to 0777:    ok
    re-apply the same ruleset:         ok (a second layer can only narrow further)
```

How to read it:

- **Before the rules every operation was `ok`**: the process is root with a full capability set.
- **After `landlock_restrict_self` it was refused `/etc/passwd` and a TCP bind to port 8080, and still read `/usr` and wrote `/tmp/n8/rw`.** The ruleset granted read and execute under `/usr` and under `/tmp/n8/ro`, everything under `/tmp/n8/rw`, and a bind to port 9090 only. Anything the ruleset **handles** and does not grant is denied. Root's `CAP_DAC_OVERRIDE` did not help: **MAC binds the superuser.** This is the property to quote in a design review.
- **Creating a file in `/tmp/n8/ro` failed and reading from it worked**, so the rule is by hierarchy and by access type, not on a single file.
- **Only what the ruleset handles is restricted.** The program asked for file reads, writes, creations, removals and TCP bind. `chmod` is not among them, and returned `ok`; so did any other operation outside the handled set. A Landlock sandbox is exactly as wide as the access types the program declares, and the documentation lists which exist in each ABI (not fetched; the header on this machine defines them).
- **`landlock_restrict_self` needs `no_new_privs` or `CAP_SYS_ADMIN`**, the same rule as seccomp. Here the process was root with `CAP_SYS_ADMIN`, so the call succeeded without the flag; an unprivileged process must set it first.
- **Applying it again succeeded** and adds a second layer; layers stack, and access needs every layer to say yes.

How the three differ in one sentence each: SELinux is policy for the whole system, written by the distribution, by labels; AppArmor is policy for chosen programs, by path; Landlock is policy a program writes for itself at run time, by directory and port. A modern service can use any combination: the distribution's MAC for the system, Landlock for a particular risky parser.

---

## 7. Confining a process you do not trust

### 7.1 Layers, not one wall

No single mechanism makes a process safe. Each one removes a different kind of power, and a sandbox is the stack of them. Here is the stack in the order the sections of this tutorial built it, with what each layer stops and what it leaves:

| Layer | Removes | Does not stop |
| --- | --- | --- |
| drop the user ID and groups (section 3) | access to other users' files by ownership | anything the user may do, kernel bugs |
| drop capabilities, bounding set (section 4) | the root-only operations | operations needing no capability |
| `no_new_privs` (section 4.7) | gaining rights through `exec` | rights already held |
| MAC: Landlock, SELinux, AppArmor (section 6) | files, ports and operations outside the policy, even for root | system calls the policy does not handle, kernel bugs |
| `seccomp` (7.2) | whole **system calls** (and some argument values) | anything done with the calls that remain, including exploiting a bug in one of them |
| namespaces (7.3) | the **view** of other processes, mounts, network, users | resources that no namespace virtualises |
| memory defences (section 8) | reliable exploitation of memory-safety bugs | logic bugs, information leaks |

The unifying idea is **attack-surface reduction** plus **blast-radius reduction**. A process that parses an untrusted image file should not be able to open a network connection or run a program, and if an attacker takes it over, what they get is that process and nothing else.

### 7.2 `seccomp`: filtering system calls

`seccomp` (secure computing) lets a thread install a **filter**, a small program in classic BPF that the kernel runs on every system call the thread makes. The filter sees a `struct seccomp_data` with the syscall number, the architecture (`AUDIT_ARCH_*`), the instruction pointer and the six arguments (`seccomp(2)`, fetched), and returns a verdict. The verdicts, from the highest precedence to the lowest (`seccomp(2)`, fetched):

```text
SECCOMP_RET_KILL_PROCESS > KILL_THREAD > TRAP > ERRNO > USER_NOTIF > TRACE > LOG > ALLOW
```

**Rules that follow from the manual page (fetched):**

- **Installing needs `no_new_privs` or `CAP_SYS_ADMIN`**, and without either the call fails with `EACCES`. The reason given: otherwise an unprivileged process could install a malicious filter and then run a `setuid` program under it, making that program misbehave in a way that helps the attacker.
- **Filters are inherited by `fork` and survive `execve`**, and they **stack**: all attached filters run, most recently installed first, and the result is the verdict of the highest precedence. A filter can only tighten the policy. There is no call to remove one.
- **The filter must check the architecture.** System call numbers differ by architecture, and x86-64 and its x32 variant share `AUDIT_ARCH_X86_64`; the page recommends an allow-list over a deny-list, which has to be updated for every new system call and can be bypassed by alternate representations (for instance the x32 bit).
- Loads must be 4-byte words (no `BPF_H` or `BPF_B`).

I wrote the filters by hand in classic BPF, with no `libseccomp`. The builder emits: load the architecture, kill if it is not x86-64, load the syscall number, a run of padding compares, then `getuid` is denied with the chosen action and everything else is allowed. `build_ex` can also add a load of an argument, which is what changes the cost below.

```c
static int install(struct sock_filter *f, int n)
{
    struct sock_fprog p = { .len = (unsigned short)n, .filter = f };
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &p);
}

static int build_ex(struct sock_filter *f, int pad, int readarg, int deny_nr, unsigned deny_action)
{
    int n = 0;
    f[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
    f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0);
    f[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
    if (readarg) f[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args));
    f[n++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, NR(0));
    for (int i = 0; i < pad; i++) f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 100000 + i, 0, 0);
    f[n++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, deny_nr, 0, 1);
    f[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, deny_action);
    f[n++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    return n;
}
```

**The installation rule, measured.**

```text
installing a filter
  uid 65534, no_new_privs not set, no CAP_SYS_ADMIN: Permission denied
  same, after PR_SET_NO_NEW_PRIVS: ok
  as root with CAP_SYS_ADMIN, without no_new_privs: ok
```

Uid 65534 without the flag got `Permission denied` (`EACCES`), with the flag it was accepted, and root with `CAP_SYS_ADMIN` could install without it. This matches the manual page.

**What a denied call looks like.**

```text
what a denied call looks like
  RET_ERRNO(EPERM)   getppid allowed; getuid -> -1, errno Operation not permitted
  RET_ERRNO(ENOSYS)  getppid allowed; getuid -> -1, errno Function not implemented
  RET_TRAP           getppid allowed; getuid -> SIGSYS delivered, si_syscall 102, si_arch 0xc000003e, si_code 1
  RET_KILL_PROCESS   getppid allowed; getuid -> killed by signal 31 (Bad system call)
  RET_LOG            getppid allowed; getuid -> 0, errno 0
```

- **`RET_ERRNO`**: the call returns -1 with the errno the filter chose (`EPERM` or `ENOSYS` above). The program sees an ordinary failure and can carry on. A filter that returns `ENOSYS` for calls newer than the program knows lets old and new libc versions coexist, and it is what container runtimes do for the system calls they do not allow.
- **`RET_TRAP`**: the kernel sends `SIGSYS` with `si_syscall` (102, `getuid`), `si_arch` (`0xc000003e`, x86-64) and `si_code` 1, so the process can emulate the call or log it (the handler in the program only prints and exits).
- **`RET_KILL_PROCESS`**: the whole process was killed by signal 31 (`SIGSYS`, "Bad system call"). Use it for calls that must never happen; the audit log gets a record. A thread-only kill (`KILL_THREAD`) leaves the rest of the process running in an unknown state and is almost never what you want.
- **`RET_LOG`**: allowed, logged. A mode for developing a policy from observed behaviour before enforcing it.
- `USER_NOTIF` (not run here) forwards the call to a **supervisor** process, which can decide and answer. If no supervisor is attached the call returns `ENOSYS` (fetched). It is a convenience for emulating calls in containers, and arguments that are pointers can change between the supervisor's check and the kernel's use, so it is **not a security boundary for pointer arguments** (that caveat is from memory; the page I fetched does not mention it).

**Stacking and the architecture check.**

```text
filters stack, and the strictest verdict wins
  two filters: getuid -> -1 (Operation not permitted), getgid -> -1 (Permission denied)
  /proc/self/status says Seccomp: 2 (2 = filter mode), Seccomp_filters: 2

the architecture check
  child ended by signal 31 (Bad system call): the arch check sent it to KILL_PROCESS
```

Two filters were installed (one denying `getuid` with `EPERM`, one denying `getgid` with `EACCES`) and both took effect; `/proc/self/status` reports `Seccomp: 2` (filter mode) and `Seccomp_filters: 2`. A 32-bit `int 0x80` call made from a 64-bit process carries a different architecture identifier; with the check in the filter it was killed. That is the attack the check exists for: without it, call number 102 on the 32-bit path means something else and may pass an allow-list built for 64-bit numbers.

### 7.3 What a filter costs

A filter runs on **every** system call, so its cost matters for exactly the workloads [OS 19](19_user_kernel_boundary.md) was about. The program measures `getppid` (about 90 to 110 ns here) before and after installing a filter of growing length, in two forms: a filter that reads only the architecture and the syscall number, and a filter that also loads an argument:

```c
static double per_call(void)
{
    enum { B = 15, N = 200000 };
    double r[B];
    for (int b = 0; b < B; b++) {
        double t0 = now();
        for (int i = 0; i < N; i++) IGN(syscall(SYS_getppid));
        r[b] = (now() - t0) / N * 1e9;
    }
    qsort(r, B, sizeof r[0], cmp);
    return r[0];                           /* the best batch: least disturbed by the host */
}

static void sect_cost(void)
{
    printf("getppid() cost under a filter that allows it (best of 15 batches of 200,000 calls, CPU 0)\n");
    int pads[] = { 0, 10, 100, 400 };
    for (int mode = 0; mode < 2; mode++) {
        printf(" %s\n", mode == 0 ? "filter looks only at the architecture and syscall number" : "filter also loads an argument");
        for (int k = 0; k < 4; k++) {
            fflush(stdout);
            pid_t p = fork();
            if (p == 0) {
                pin(0);
                struct sock_filter f[1024];
                double base = per_call();
                IGN(prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0));
                int n = build_ex(f, pads[k], mode, SYS_getuid, SECCOMP_RET_ERRNO | EPERM);
                if (install(f, n) < 0) { perror("seccomp"); _exit(1); }
                double t = per_call();
                printf("  %3d compares before the verdict (%3d instructions): no filter %6.1f ns, with filter %6.1f ns, difference %+6.1f ns\n", pads[k] + 1, n, base, t, t - base);
                fflush(stdout);
                _exit(0);
            }
            waitpid(p, NULL, 0);
        }
    }
}
```

```text
getppid() cost under a filter that allows it (best of 15 batches of 200,000 calls, CPU 0)
 filter looks only at the architecture and syscall number
    1 compares before the verdict (  7 instructions): no filter  100.0 ns, with filter  113.0 ns, difference  +13.0 ns
   11 compares before the verdict ( 17 instructions): no filter   98.0 ns, with filter  117.3 ns, difference  +19.3 ns
  101 compares before the verdict (107 instructions): no filter  113.1 ns, with filter  124.6 ns, difference  +11.4 ns
  401 compares before the verdict (407 instructions): no filter  111.9 ns, with filter  127.1 ns, difference  +15.1 ns
 filter also loads an argument
    1 compares before the verdict (  8 instructions): no filter  108.6 ns, with filter  127.0 ns, difference  +18.4 ns
   11 compares before the verdict ( 18 instructions): no filter  100.1 ns, with filter  119.6 ns, difference  +19.5 ns
  101 compares before the verdict (108 instructions): no filter   99.5 ns, with filter  120.9 ns, difference  +21.4 ns
  401 compares before the verdict (408 instructions): no filter   96.8 ns, with filter  169.1 ns, difference  +72.3 ns
```

How to read it, with the caveat that the baseline itself moved between 89 and 113 ns across runs, so differences under about 15 ns are noise:

- **A filter that depends only on the syscall number and architecture added about 10 ns, and the same at 7 instructions as at 407** (between -3 and +19 ns over three runs, with no trend in the length). The kernel can decide such a filter's verdict for every syscall number **once, when it is installed**, and keep an allow bitmap, so an allowed call does not run the filter at all (that mechanism, added around Linux 5.11, is from memory; the measured flat cost is consistent with it and does not prove it).
- **A filter that also reads an argument must run for every call, and its cost grows with its length.** At 401 compares it added +72.3 ns in the printed run (three runs gave +63.6, +84.2 and +72.3 ns), about 0.18 ns per compare, which is a handful of cycles per BPF instruction (the kernel compiles the program to native code, from memory). At 1, 11 and 101 compares it added between 1 and 40 ns over the three runs, which is within the noise of the baseline.
- **The rule for writing a filter: put the cheap, number-only decisions first, and treat an argument check as a per-call cost.** A filter that allows most calls by number and examines arguments only for the few dangerous ones (`socket`, `ioctl`, `prctl`, `clone` flags) is as cheap as a number-only filter for the rest, because the kernel's precomputation covers calls the filter decides without looking at arguments (from memory; not measured with a mixed filter).
- In the previous tutorial's terms: a 20 ns tax on a 100 ns system call is 20 percent, which matters for a workload doing a million calls a second and is invisible for one doing a thousand.

### 7.4 Namespaces: a different view of the system

A **namespace** wraps a global resource so that a group of processes sees its own copy. The kinds (from memory): mount (`CLONE_NEWNS`), PID, network, UTS (host name), IPC, user, cgroup, time. A process joins new ones with `unshare` or `clone`. Namespaces are **isolation of the view**, not of the resources: two containers still share one kernel, one clock (unless time-namespaced) and the same CPU and memory unless cgroups limit them.

The **user namespace** is the keystone, because creating the others normally needs privilege and creating a user namespace does not (`user_namespaces(7)`, fetched, paraphrased):

- Unprivileged users may create one (since Linux 3.8). The creator gets a **full set of capabilities inside it**, and **none** in the parent namespace.
- Those capabilities apply only to resources **owned by that namespace**, such as the namespaces it creates together with it. Global operations (the system clock, loading modules, creating device nodes) still need privilege in the initial namespace.
- The namespace starts with **no ID mapping**: IDs show as the overflow ID 65534. An unprivileged process may write one line to `uid_map` mapping a single ID, which must be its own effective UID. For `gid_map` it must first write `deny` to `/proc/pid/setgroups`, which permanently disables `setgroups` in that namespace and closes a loophole where a user could drop a group that was blocking access.
- Nesting is limited to 32 levels, and the maps may be written **once**.

The program runs as user 1003, calls `unshare` for user, PID, mount and network namespaces together, writes the maps, forks, and then tries a list of operations:

```c
static void inner(void)
{
    capline("inside, before any mapping");
    char map[64];
    IGN(prctl(PR_SET_DUMPABLE, 1));
    snprintf(map, sizeof map, "0 %d 1\n", USER);
    wfile("/proc/self/setgroups", "deny\n");
    wfile("/proc/self/gid_map", "0 3000 1\n");
    wfile("/proc/self/uid_map", map);
    capline("after writing uid_map");
    printf("  getpid() %d, getppid() %d (first process in the new pid namespace)\n", getpid(), getppid());
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        printf("  forked child: getpid() %d, getppid() %d\n", getpid(), getppid());
        printf("  mount a tmpfs on /mnt (own mount namespace): %s\n", code(mount("none", "/mnt", "tmpfs", 0, "size=1m")));
        printf("  open /etc/shadow (real owner root, unmapped): %s\n", code(open("/etc/shadow", O_RDONLY)));
        printf("  bind port 80 on this namespace's loopback: %s\n", code(bind_to(80)));
        printf("  sethostname: %s\n", code(sethostname("x", 1)));
        printf("  settimeofday: %s\n", code(syscall(SYS_settimeofday, NULL, NULL)));
        printf("  mknod /tmp/devzero c 1 5: %s\n", code(mknod("/tmp/devzero", S_IFCHR | 0600, makedev(1, 5))));
        printf("  chown /tmp/n5file to uid 5 (unmapped): %s\n", code(chown("/tmp/n5file", 5, 5)));
        fflush(stdout);
        _exit(0);
    }
    waitpid(p, NULL, 0);
    _exit(0);
}
```

```text
max_user_namespaces 32003
  unprivileged user, outside         uid 1003 CapEff 0000000000000000
  unshare(CLONE_NEWUSER|NEWPID|NEWNS|NEWNET): ok
  inside, before any mapping         uid 65534 CapEff 000001ffffffffff
  after writing uid_map              uid 0 CapEff 000001ffffffffff
  getpid() 1, getppid() 0 (first process in the new pid namespace)
  forked child: getpid() 2, getppid() 1
  mount a tmpfs on /mnt (own mount namespace): ok
  open /etc/shadow (real owner root, unmapped): Permission denied
  bind port 80 on this namespace's loopback: ok
  sethostname: Operation not permitted
  settimeofday: Operation not permitted
  mknod /tmp/devzero c 1 5: Operation not permitted
  chown /tmp/n5file to uid 5 (unmapped): Invalid argument
```

How to read it:

- **Outside, user 1003 has `CapEff 0000000000000000`. After `unshare`, before the map, the process shows uid 65534 (unmapped) and `CapEff 000001ffffffffff`**: all 41 capabilities, inside. After the map is written the same process is uid 0. The kernel check for every operation asks whether the caller has the capability **in the namespace that owns the resource**.
- **The first process in the new PID namespace had pid 1 and parent 0; its child had pid 2 and parent 1.** Inside, nothing else exists, and a PID-namespace init that dies takes the whole namespace with it.
- **Mounting a `tmpfs` worked, and binding port 80 worked.** The mount namespace and the network namespace belong to the new user namespace, so its "root" may use them; port 80 here is on the namespace's own loopback, not the host's.
- **`sethostname` failed with `EPERM`.** The program did not create a UTS namespace, so the host name is a global resource and needs privilege in the initial namespace. A sandbox only virtualises what it created.
- **`settimeofday` and `mknod` failed with `EPERM`**: clock and device nodes are global. **`/etc/shadow` failed with `EACCES`**: uid 0 inside is user 1003 outside, and a file owned by an unmapped user is owned by nobody the process can claim to be. **`chown` to an unmapped user failed with `EINVAL`.**
- **Setting `PR_SET_DUMPABLE` was needed to write the map** (the program does this first): changing credentials with `unshare` resets the dumpable flag, which makes `/proc/self` files root-owned and the write fails with `Permission denied`. The manual page lists the same hazard for a parent writing a child's maps.

The kernel limit `max_user_namespaces` is **32003** here (measured). User namespaces make a lot of kernel code reachable by unprivileged users (filesystem mounts, network configuration, `netfilter`), and some distributions disable them by default or by a sysctl (from memory), which is a common hardening question: the feature that makes rootless containers possible is also a source of local-privilege-escalation bugs.

### 7.5 Putting the pieces in order

The order in which a launcher applies the layers matters, and one rule explains it: **each layer must be applied while the calls it needs are still allowed.**

```text
1. open what the worker will need (files, sockets) and keep the descriptors
2. unshare / clone into new namespaces, write the id maps
3. mount a minimal root (pivot_root into a read-only tree), drop the old root
4. set no_new_privs
5. drop capabilities (permitted, effective, bounding) and become the unprivileged id
6. install the Landlock ruleset
7. install the seccomp filter last
8. execve the worker (or just continue)
```

The seccomp filter is last because it will block `unshare`, `mount`, `prctl` and the `landlock_*` calls that the earlier steps use. Chrome's renderer sandbox, `systemd`'s `SystemCallFilter=` and `RestrictAddressFamilies=`, Docker's default profile and `bubblewrap` are this recipe with different ingredients (from memory). The descriptor-passing pattern of section 3.4 is what lets the worker, with nothing, ask a broker for exactly what it needs.

---

## 8. Making memory-safety bugs hard to exploit

[CO 30](../04_computer_organization/30_hardware_security.md) covers what the hardware does (the NX bit, SMEP, SMAP, shadow stacks). This section covers what the operating system and the toolchain do with that hardware, measured.

### 8.1 The threat

A memory-safety bug (a buffer overflow, a use after free, an out-of-bounds index) lets an attacker write or read memory they should not. The classic path to control: overwrite a return address or a function pointer with an address of the attacker's choice. The defences make each step of that path **fail or become unreliable**: they do not remove the bug.

### 8.2 Non-executable memory (W^X)

A page's permissions (read, write, execute) are bits in its page-table entry ([OS 11](11_virtual_memory.md), [OS 12](12_virtual_memory_and_tlb.md)). If the **stack, heap and data pages are not executable**, injected shellcode cannot run. The program copies the one-byte program `ret` (`0xC3`) into an anonymous page and calls it, first when the page is writable, then after `mprotect` makes it executable:

```c
static void nx(void)
{
    unsigned char ret = 0xC3;
    struct sigaction sa = { .sa_sigaction = on_segv, .sa_flags = SA_SIGINFO | SA_NODEFER };
    sigaction(SIGSEGV, &sa, NULL);
    printf("\nnon-executable memory: copy the one-byte program 'ret' into an anonymous page and call it\n");
    void *pg = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memcpy(pg, &ret, 1);
    int r = sigsetjmp(jb, 1);
    if (r == 0) { ((void (*)(void))pg)(); printf("  page is PROT_READ|PROT_WRITE: the call returned\n"); }
    else printf("  page is PROT_READ|PROT_WRITE: SIGSEGV, si_code %s\n", r == 2 ? "SEGV_ACCERR (permission, not unmapped)" : "SEGV_MAPERR");
    int mp = mprotect(pg, 4096, PROT_READ | PROT_EXEC);
    printf("  mprotect to PROT_READ|PROT_EXEC: %s\n", mp == 0 ? "ok" : strerror(errno));
    r = sigsetjmp(jb, 1);
    if (r == 0) { ((void (*)(void))pg)(); printf("  call after the page became executable: returned normally\n"); }
    else printf("  call after mprotect: SIGSEGV\n");
    r = sigsetjmp(jb, 1);
    if (r == 0) { *(volatile unsigned char *)pg = 0x90; printf("  write to the now-executable page: succeeded\n"); }
    else printf("  write to the now-executable page: SIGSEGV (it is no longer writable)\n");
    void *both = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("  mmap of a page that is writable and executable at once: %s\n", both == MAP_FAILED ? strerror(errno) : "granted (nothing in the kernel forbids it here)");
}
```

```text
non-executable memory: copy the one-byte program 'ret' into an anonymous page and call it
  page is PROT_READ|PROT_WRITE: SIGSEGV, si_code SEGV_ACCERR (permission, not unmapped)
  mprotect to PROT_READ|PROT_EXEC: ok
  call after the page became executable: returned normally
  write to the now-executable page: SIGSEGV (it is no longer writable)
  mmap of a page that is writable and executable at once: granted (nothing in the kernel forbids it here)
```

- **Calling a `PROT_READ|PROT_WRITE` page raised `SIGSEGV` with `si_code SEGV_ACCERR`**: the page is mapped and the access is a permission error, not a missing page. This is NX at work.
- **After `mprotect` to read and execute, the call returned, and then a write to the same page faulted.** The sequence "write the code, then change the protection" is what a JIT compiler does.
- **A page that is writable and executable at once was granted.** Nothing in this kernel forbids it, so W^X is a convention for ordinary programs here, not an enforced rule. Systems that enforce it (some distributions' SELinux policies, OpenBSD) make JIT engines map the same memory twice, once writable and once executable (from memory).
- NX defeats code injection, not **code reuse**: return-oriented programming chains together existing instructions and needs no executable data page. That is why ASLR and the canary exist.

### 8.3 Address-space layout randomisation (ASLR)

If the attacker does not know where code and data are, a hijacked pointer is a guess. The kernel places the stack, the heap, `mmap` regions (and so the shared libraries) and, for a **position-independent executable (PIE)**, the program image at randomised addresses, with the setting `kernel.randomize_va_space = 2` (full, including the heap; measured). The program runs a probe 300 times through `fork` and `exec`, once with randomisation and once with `personality(ADDR_NO_RANDOMIZE)` (what `setarch -R` does), and reports how much each address varies:

```c
static void probe(void)
{
    int local;
    void *h = malloc(16);
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("%lx %lx %lx %lx %lx\n", (unsigned long)&local, (unsigned long)sbrk(0), (unsigned long)m, (unsigned long)&printf, (unsigned long)&main);
    (void)h;
}

static void report(const char *title, unsigned long a[NREG][RUNS])
{
    printf("%s (%d runs)\n", title, RUNS);
    printf("  %-18s %-16s %-16s %9s %13s\n", "region", "lowest seen", "highest seen", "distinct", "spread bits");
    for (int r = 0; r < NREG; r++) {
        unsigned long s[RUNS];
        memcpy(s, a[r], sizeof s);
        qsort(s, RUNS, sizeof s[0], cmp);
        int d = 1;
        for (int i = 1; i < RUNS; i++) if (s[i] != s[i - 1]) d++;
        double bits = s[RUNS - 1] > s[0] ? log2((double)(s[RUNS - 1] - s[0]) / 4096.0) : 0;
        printf("  %-18s %016lx %016lx %9d %13.1f\n", rname[r], s[0], s[RUNS - 1], d, bits);
    }
}
```

```text
kernel.randomize_va_space = 2

randomisation on (300 runs)
  region             lowest seen      highest seen      distinct   spread bits
  stack              00007ffc002f7054 00007fffffebfc64       300          22.0
  brk heap           0000555581535000 0000565568628000       300          28.0
  mmap               00007efc046de000 00007ffbb64a0000       300          28.0
  libc function      00007efc04260100 00007ffbb6060100       300          28.0
  executable (main)  00005555585503e0 00005655432073e0       300          28.0

randomisation off (personality ADDR_NO_RANDOMIZE, what setarch -R does) (300 runs)
  region             lowest seen      highest seen      distinct   spread bits
  stack              00007fffffff6244 00007fffffff6244         1           0.0
  brk heap           000055555555e000 000055555555e000         1           0.0
  mmap               00007ffff7fba000 00007ffff7fba000         1           0.0
  libc function      00007ffff7c60100 00007ffff7c60100         1           0.0
  executable (main)  00005555555553e0 00005555555553e0         1           0.0
```

How to read it:

- **Every one of 300 runs had a different address for each region** (300 distinct values), and with randomisation off every run had the same one (1 distinct value). The probe includes an address in the executable (`main`), which varies because the build is a PIE (`file` reports "pie executable").
- **The spread, `log2((highest - lowest) / 4096)`, is about 28 bits for the heap, the `mmap` area, `libc` and the executable, and 22 for the stack.** The 28 matches `vm.mmap_rnd_bits = 28` on this machine (measured): the kernel chooses the base in units of a page, so 28 bits means 2^28 = 268,435,456 positions. This is an estimate from 300 samples of the range, so it can read a little low.
- **What 28 bits buys.** An attacker who guesses blindly needs about 2^27 tries on average, and a crash on each wrong guess is visible. It is a real cost for a remote attacker and **no protection at all against an information leak**: one pointer printed by a bug reveals the base, and the offsets inside a library are fixed. In practice ASLR is a speed bump that turns "reliable exploit" into "needs a leak first".
- **A forking server re-uses its layout.** Every child of a `fork` has the same layout as the parent (a child only changes it at `exec`), so an attacker can probe one worker after another against the same addresses and the same canary (section 8.4). The exec-after-fork design, or restarting workers, is the remedy (from memory, and the canary line in section 8.4 shows the same fact).
- **Entropy depends on the architecture**: 32-bit systems have much less address space to randomise in (from memory, roughly 8 bits for `mmap`), which is why 32-bit ASLR is weak.
- The first entry for a stack shows `0x00007ffc...` to `0x00007fff...`, in the top of the 47-bit user address space; the kernel's own randomisation (KASLR) is the same idea applied to kernel addresses (section 8.6).

### 8.4 The stack canary

The **stack protector** (`-fstack-protector-strong` in GCC) places a random value (the **canary**) between the local arrays and the saved return address. The compiler-inserted epilogue compares it with the master copy and, if it differs, calls `__stack_chk_fail`, which prints "stack smashing detected" and aborts. A contiguous overflow of a buffer reaches the canary before the return address, so the overflow is detected before the corrupted return is used. The master value is in thread-local storage (`fs:0x28` on x86-64) and is chosen at program start from the kernel's random bytes.

The program has a function with a 16-byte buffer that copies a caller-chosen length into it, run in a forked child so the parent can report how the child ended. It was built three ways:

```c
static unsigned long canary(void)
{
    unsigned long c;
    __asm__ volatile("movq %%fs:0x28, %0" : "=r"(c));
    return c;
}
```

```c
__attribute__((noinline)) static void vuln(const char *src, int n)
{
    char buf[16];
    memcpy(buf, src, n);                 /* the bug: n is not checked against sizeof buf */
    __asm__ volatile("" : : "r"(buf) : "memory");
}
```

**Built with no protection at all** (`-fno-stack-protector -D_FORTIFY_SOURCE=0`):

```text
built without a stack protector
  canary in this process bd13f5faf0c83900
  canary in a forked child bd13f5faf0c83900 (same: fork copies it)
1ac62b42ea677e00
  copy  16 bytes into char[16]: exit status 0
  copy  17 bytes into char[16]: exit status 0
  copy  24 bytes into char[16]: exit status 0
  copy  40 bytes into char[16]: killed by signal 11 (Segmentation fault)
```

**Built with only the stack protector** (`-fstack-protector-strong -D_FORTIFY_SOURCE=0`):

```text
built with -fstack-protector-strong
  canary in this process aa6320fb69f85900
  canary in a forked child aa6320fb69f85900 (same: fork copies it)
9e2af4747a4e3800
  copy  16 bytes into char[16]: exit status 0
  copy  17 bytes into char[16]: exit status 0
  copy  24 bytes into char[16]: exit status 0
*** stack smashing detected ***: terminated
  copy  40 bytes into char[16]: killed by signal 6 (Aborted)
*** stack smashing detected ***: terminated
```

**Built with the compiler's default flags on this system** (no flags beyond `-O2`):

```text
built with -fstack-protector-strong
  canary in this process 8dc22e38ea326f00
  canary in a forked child 8dc22e38ea326f00 (same: fork copies it)
096c5fb5ad257d00
  copy  16 bytes into char[16]: exit status 0
*** buffer overflow detected ***: terminated
  copy  17 bytes into char[16]: killed by signal 6 (Aborted)
*** buffer overflow detected ***: terminated
  copy  24 bytes into char[16]: killed by signal 6 (Aborted)
*** buffer overflow detected ***: terminated
  copy  40 bytes into char[16]: killed by signal 6 (Aborted)
*** buffer overflow detected ***: terminated
  copy 200 bytes into char[16]: killed by signal 6 (Aborted)
```

How to read it:

- **No protection: copies of 17 and 24 bytes into the 16-byte array exited normally**, silently corrupting whatever sits past the array; **40 bytes crashed with `SIGSEGV`**, because the saved return address was overwritten and the return went to a non-address. This is the bug the defences are for.
- **With the stack protector, the 17- and 24-byte overflows were also not detected** (exit status 0), and 40 and 200 bytes aborted with "stack smashing detected" (signal 6). The overflow of 17 to 24 bytes lands in padding or in other locals between the array and the canary, so the canary was untouched. **A canary detects an overwrite that reaches it. It does not say there was no overflow.** The exact gap is the compiler's choice of stack layout and not something to rely on.
- **With the default flags, the first message was "buffer overflow detected", not "stack smashing detected", and it fired at 17 bytes.** This system's GCC enables `-D_FORTIFY_SOURCE` and the stack protector by default (the program's own macro `__SSP_STRONG__` reported "built with -fstack-protector-strong"). `memcpy` became `__memcpy_chk`, which knows the destination is 16 bytes and **aborts at the call**, before the canary is ever consulted. So a build with the system's defaults catches the same bug earlier, and a test of "does the canary catch this?" must turn fortification off, as the second block did.
- **The canary value changes between runs and is the same in a forked child.** The six canaries printed are all different (`8dc2...`, `096c...`, `bd13...` and so on), and the process and its forked child printed the same one. A server that forks workers from a master therefore lets an attacker learn the canary one byte at a time by crashing workers and watching which guesses survive (from memory: the technique is "byte-for-byte brute force").
- **Every one of the six canaries ends in `00`.** The low byte is zero on purpose, so a string function that stops at a NUL byte cannot copy the canary and then continue past it (from memory, the reason; the zero byte is measured).
- **What it does not stop**: overwriting a function pointer or a local variable that sits *before* the canary; reads (a disclosure); and non-contiguous writes (an index into an array that jumps over the canary). `-fstack-protector-strong` protects functions with arrays or address-taken locals, `-fstack-protector-all` every function, and a function with none gets no canary.

### 8.5 The other defences in the same build

Not run here, so from memory: **RELRO** (the relocation table is made read-only after start-up, so overwriting an entry in the global offset table fails), **`-fcf-protection`** (the compiler marks the valid targets of indirect branches for the CPU's branch-tracking feature, and `gcc -Q --help=common` listed `-fcf-protection=full` as this compiler's setting; see also the shadow stack in [CO 30](../04_computer_organization/30_hardware_security.md)), **`-D_FORTIFY_SOURCE`** (shown above), and **sanitizers** (AddressSanitizer for tests, not production, because it multiplies run time and memory). The usual hardened build is `-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -pie -Wl,-z,relro,-z,now`.

### 8.6 What the kernel does for itself

The same ideas apply inside the kernel, which is the larger target because a bug there gives full privilege. The build options of this kernel (`/proc/config.gz`, measured) and the CPU flags show what is on:

| Defence | This machine | What it does |
| --- | --- | --- |
| `CONFIG_RANDOMIZE_BASE`, `CONFIG_RANDOMIZE_MEMORY` | `y`, `y` | KASLR: the kernel image and the direct-map region start at random addresses |
| `CONFIG_STRICT_KERNEL_RWX` | `y` | kernel code is read-only and executable, data is non-executable |
| `CONFIG_STACKPROTECTOR_STRONG`, `CONFIG_VMAP_STACK` | `y`, `y` | canary in kernel functions; kernel stacks in guard-paged virtual memory so an overflow faults |
| `CONFIG_HARDENED_USERCOPY` (default on), `CONFIG_FORTIFY_SOURCE` | `y`, `y` | `copy_to_user` and `copy_from_user` check the size against the object they touch ([OS 19](19_user_kernel_boundary.md)) |
| `CONFIG_SLAB_FREELIST_RANDOM`, `CONFIG_SLAB_FREELIST_HARDENED` | `y`, `y` | the allocator's free lists are randomised and protected |
| `CONFIG_RANDOMIZE_KSTACK_OFFSET` (default on) | `y` | a random offset of the kernel stack at each system call entry |
| `CONFIG_DEBUG_LIST`, `CONFIG_BUG_ON_DATA_CORRUPTION`, `CONFIG_SCHED_STACK_END_CHECK` | `y`, `y`, `y` | detect corruption of linked lists and kernel stacks and stop |
| `CONFIG_LOCK_DOWN_KERNEL_FORCE_INTEGRITY` | `y` | the lockdown module in integrity mode (from memory: refuses operations that let root modify the running kernel) |
| `smep`, `smap`, `umip`, `nx` in `/proc/cpuinfo` | all present | the CPU refuses kernel execution of user pages (SMEP), kernel access to user pages outside the copy routines (SMAP), and user reads of descriptor tables |

I measured that the options are *built*; except for the sysctls below I did not test that each is *active* (boot parameters can switch some off, and `cat /proc/cmdline` shows no such parameters, only `mitigations=auto,no_guest_host,no_guest_guest`).

And the sysctls and limits that gate **who may see or use what**, read from `/proc/sys` on this machine:

| Setting | Value | Meaning |
| --- | --- | --- |
| `kernel.kptr_restrict` | 0 | kernel pointers in `/proc/kallsyms` and similar are shown **as they are** to a process allowed to read them (a hardened system sets 1 or 2, which hides them from non-root and from root, since a kernel address defeats KASLR) |
| `kernel.dmesg_restrict` | 1 | reading the kernel log needs `CAP_SYSLOG` |
| `kernel.perf_event_paranoid` | 2 | unprivileged `perf` cannot measure the kernel |
| `kernel.unprivileged_bpf_disabled` | 2 | unprivileged users cannot load eBPF programs (from memory: 2 lets an administrator change the setting, 1 would lock it until reboot) |
| `fs.suid_dumpable` | 0 | a core dump of a `setuid` program is not written |
| `fs.protected_hardlinks`, `fs.protected_symlinks` | 1, 0 | hard-linking a file you cannot write is refused; following symlinks in sticky directories is not restricted here |
| `vm.mmap_min_addr` | 4,096 | the lowest address a process may map, so a kernel null-pointer bug cannot be turned into user-controlled code (this kernel's build default, `CONFIG_LSM_MMAP_MIN_ADDR`, is 65,536; the running value is lower, so a hardened system raises it) |
| `vm.mmap_rnd_bits` | 28 | the entropy of section 8.3 |
| `kernel.randomize_va_space` | 2 | ASLR including the heap |

On a system that needs hardening you would set `kptr_restrict`, restrict `ptrace` (Yama is not built here, and its `ptrace_scope` file is absent) and turn on `protected_symlinks`. The point to take to an interview is the **table of what each protects against**, not the numbers.

---

## 9. Authentication and password storage

### 9.1 Where the identity comes from

Everything above took the user ID as given. **Authentication** is how a process comes to have one: a login program asks for a credential (a password, a key, a token, a second factor), checks it, and then does exactly the identity drop of section 3 (`setgroups`, `setresgid`, `setresuid`, then `exec` the user's shell). On Linux the checking is delegated to **PAM** (pluggable authentication modules, from memory), so a service chooses a stack of modules and the policy lives in configuration instead of code. The data is in `/etc/passwd` (world-readable account list, mode `0644` here) and `/etc/shadow` (the password hashes, mode `0640` and group `shadow` here, measured by `ls -l`; the stranger's attempt to open it in section 3.2 failed with `EACCES`). This machine has 12 `setuid` programs in the system directories (`sudo`, `su`, `passwd`, `mount` and `umount`, `chsh`, `chfn`, `gpasswd`, `newgrp`, `fusermount3` and two helpers; counted with `find / -xdev -perm -4000`, excluding the two test copies made by this tutorial) and one program with **file capabilities** (`gst-ptp-helper`, with `cap_net_bind_service,cap_net_admin,cap_sys_nice=ep`, listed by `getcap -r /`). Both are the mechanisms of sections 3 and 4 in the wild.

### 9.2 How not to store a password

The stored value must let you check a password and must not let a thief of the file recover it. Four mistakes, each with the property that fixes it:

1. **Storing the password.** A leak of the file is a leak of every password, including those reused elsewhere. Store a one-way function of the password.
2. **A fast, unsalted hash** (MD5, SHA-1, plain SHA-256). The same password always gives the same value, so one **precomputed table** (a rainbow table) cracks every account and equal values reveal equal passwords. The fix is a **salt**: a random value per user, stored next to the hash, mixed into the input.
3. **A salted fast hash.** The attacker now hashes guesses against each account, and a fast hash lets a GPU try billions per second. The fix is a **slow, tunable** function: you choose a cost so that one verification takes tens of milliseconds, which a legitimate login can afford and a billion guesses cannot.
4. **A slow function that runs well on GPUs.** Iterated hashes (SHA-512-crypt, PBKDF2) use little memory, so an attacker runs thousands in parallel. **Memory-hard** functions (scrypt, yescrypt, Argon2, from memory) require a large working set per guess, which limits the parallelism of a GPU or custom chip. bcrypt is in between (small memory, but awkward for GPUs).

### 9.3 The library, and what it prints

`libcrypt` (`crypt(3)`, with the reentrant `crypt_r`) takes a password and a **setting string**; the prefix selects the scheme, and the result is the setting followed by the hash, so **the stored string is itself the setting for the next check**. The program prints one password under six schemes, shows the salt, and times each scheme:

```c
static double time_hash(const char *pw, const char *setting, int reps, char *out, size_t outsz)
{
    struct crypt_data cd;
    memset(&cd, 0, sizeof cd);
    double t0 = now();
    const char *r = NULL;
    for (int i = 0; i < reps; i++) r = crypt_r(pw, setting, &cd);
    double t = (now() - t0) / reps;
    snprintf(out, outsz, "%s", r ? r : "(failed)");
    return t;
}

static void sect_salt(void)
{
    char a[256], b[256], c[256];
    time_hash("hunter2", "$6$saltAAAA$", 1, a, sizeof a);
    time_hash("hunter2", "$6$saltBBBB$", 1, b, sizeof b);
    time_hash("hunter2", "$6$saltAAAA$", 1, c, sizeof c);
    printf("\nsalt: same password, different salt, different stored strings\n");
    printf("  salt AAAA: %.50s\n  salt BBBB: %.50s\n  salt AAAA again: %s\n", a, b, strcmp(a, c) == 0 ? "identical to the first (deterministic)" : "different");
    struct crypt_data cd;
    memset(&cd, 0, sizeof cd);
    const char *chk = crypt_r("hunter2", a, &cd);            /* the stored string is itself the setting */
    printf("  verify: crypt(\"hunter2\", stored) == stored: %s\n", chk && strcmp(chk, a) == 0 ? "yes" : "no");
    memset(&cd, 0, sizeof cd);
    chk = crypt_r("hunter3", a, &cd);
    printf("  verify a wrong password: %s\n", chk && strcmp(chk, a) == 0 ? "yes" : "no");
}
```

```text
one password, six schemes (a setting string selects the scheme; the stored string is setting + hash)
  MD5-crypt ($1$)         34 chars  $1$saltsalt$NuzA7WTAelpl95xgBGWN60
  SHA-256-crypt ($5$)     55 chars  $5$saltsalt$myjXcpMpE2Ofk7fj9hqyNYSn6lmWG4Mqnjx.KIRRr4/
  SHA-512-crypt ($6$)     98 chars  $6$saltsalt$hRM5XZ86KXEw9UOmjigeVqFgULtFB2sgpC9lXQDfMib3Zgw7
  bcrypt ($2b$)           60 chars  $2b$05$abcdefghijklmnopqrstuuHNbAKRhpaujgo33bRWs.NLUTJO3lOy2
  yescrypt ($y$)          59 chars  $y$j9T$saltsalt$IFUVMd9IZg.1qt0VDKUmwCd8zfLLrtDA9mffjiD5skD
  DES-crypt (2 chars)     13 chars  abhfCpXqd4GrI

salt: same password, different salt, different stored strings
  salt AAAA: $6$saltAAAA$BvpXv1syGM03r.QbLVildfoRdsRfS0mZavNS08
  salt BBBB: $6$saltBBBB$uqJ2XkZI.1EyvFNIdQbkMl1nboy3.v80OeYKnk
  salt AAAA again: identical to the first (deterministic)
  verify: crypt("hunter2", stored) == stored: yes
  verify a wrong password: no
```

How to read it:

- **Every scheme's output is the setting followed by the hash**: `$1$` MD5-crypt (legacy), `$5$` and `$6$` SHA-256 and SHA-512-crypt, `$2b$` bcrypt, `$y$` yescrypt, and the two-character DES-crypt (13 characters, which truncates the password to 8 characters; from memory, and marked legacy). This `libcrypt` (glibc 2.39 with `libxcrypt`, from memory) supports all six. The manual page `crypt(5)` could not be fetched, so the list of prefixes and the recommendation of yescrypt as the default for new passwords on current distributions are from memory.
- **The same password with salts `AAAA` and `BBBB` gives different strings**, and the same salt gives the same string twice (the function is deterministic), which is what makes verification possible.
- **Verification is "hash the candidate with the stored string as the setting and compare"**: `crypt("hunter2", stored) == stored` was true and a wrong password was false. In production the comparison should be **constant-time**, so the time taken does not reveal how many leading characters matched (from memory; `strcmp` is used here only to show the principle).
- **bcrypt reads only the first 72 bytes of the password** (from memory), so a very long passphrase is silently cut; some deployments pre-hash the password to avoid it.

### 9.4 The cost parameter, measured

```text
cost: time for one hash on one core (average of the repetitions shown)
  SHA-512-crypt rounds=1000        0.296 ms per hash  ->     3377.7 guesses per second per core
  SHA-512-crypt rounds=5000        1.739 ms per hash  ->      574.9 guesses per second per core
  SHA-512-crypt rounds=50000      15.805 ms per hash  ->       63.3 guesses per second per core
  SHA-512-crypt rounds=500000    159.637 ms per hash  ->        6.3 guesses per second per core
  bcrypt cost 5                    2.308 ms per hash  ->      433.3 guesses per second per core
  bcrypt cost 10                  59.400 ms per hash  ->       16.8 guesses per second per core
  bcrypt cost 12                 232.218 ms per hash  ->        4.3 guesses per second per core
  yescrypt cost 5 (j9T)           15.070 ms per hash  ->       66.4 guesses per second per core
  yescrypt cost 7 (jDT)          325.519 ms per hash  ->        3.1 guesses per second per core
```

- **The cost is a parameter you choose and the time follows it.** SHA-512-crypt at 1,000, 5,000, 50,000 and 500,000 rounds took 0.30, 1.7, 15.8 and 159.6 ms: about 0.3 microseconds per round, linear in the round count. bcrypt's cost is a power of two (cost 5 is 2^5 iterations): 2.3 ms at cost 5, 59.4 ms at cost 10 and 232 ms at cost 12, about 25 and 4 times the previous, consistent with 2^5 and 2^2. yescrypt's cost classes (`j9T` and `jDT` here) went from 15.1 to 325.5 ms.
- **The usual target is 50 to 250 ms per verification on the login server** (from memory of common guidance), set so that an attacker's guess rate is slow while the server can still handle its peak login rate. With a single core at 15 ms (yescrypt `j9T`), one core can verify about 66 passwords a second.
- **Cost belongs in the stored string**, so you can raise it for new passwords and rehash an old one at the next successful login, with no migration downtime.

### 9.5 What the guess rate means

```text
what that rate means (yescrypt j9T measured above, one core, per stolen hash)
  4 random digits                      1.0e+04 guesses, expected    80.4 seconds
  6 random digits                      1.0e+06 guesses, expected     2.2 hours
  8 random lowercase letters           2.1e+11 guesses, expected    53.3 years
  8 random chars of 95 printable       6.6e+15 guesses, expected 1690970.6 years
  4 random words of a 7776-word list   3.7e+15 guesses, expected 933477.0 years
```

The table uses the measured rate of 66 guesses a second per core against one stolen hash, and takes **half the search space** as the expected number of guesses:

- **A 4-digit PIN falls in about 80 seconds, a 6-digit PIN in about 2 hours, on one core against a good hash.** A PIN has so little entropy that no hash can protect it; the protection has to be rate limiting and lock-out at the service, and a PIN is acceptable only where the attacker cannot get the stored hash.
- **8 random lowercase letters took 53 years on one core, and a random 8-character password from the 95 printable characters 1.7 million years.** The rate is for **one core of this machine**. An attacker with a thousand GPU cores and a faster implementation gains a large factor in one direction (a hash that is memory-hard gains less than a lightweight one), and the figure is **not** a measurement of any real attacker. Read it as the ratio between spaces, not as a promise.
- **Human-chosen passwords are not random**: they come from a small set (words, names, patterns, earlier leaks), and attackers try those first. The design consequence is that entropy requirements and **breached-password checks** matter more than composition rules, and that **a second factor** moves the risk off the password (from memory, as standard guidance).

Beyond the hash: **rate-limit and lock out** online guessing, **use a pepper** (a secret key kept outside the database and mixed in, so a stolen database alone is not enough; from memory), **never log passwords**, **do not email them**, and **do not write your own** (use the platform's password-hashing function).

---

## 10. A design review: running an untrusted converter

**The problem.** A service accepts uploaded images and converts them with a large C library full of parsers, the kind of code that has memory-safety bugs. It runs on Linux. How do you contain it?

Walk the layers in the order of section 7.5, saying for each **what it stops and what it leaves**:

1. **Process split.** The service (network-facing, no file system access beyond a spool directory) hands each upload to a **separate converter process** over a Unix socket and a passed descriptor (section 3.4, [OS 18](18_ipc_mechanisms.md)). A compromised converter is not the service.
2. **Identity.** The converter runs as a dedicated user with no groups, no files, `umask(077)`. Stops: reading other users' data by ownership. Leaves: whatever that user may touch.
3. **Capabilities.** Drop all of them, including from the bounding set; the converter needs none. Stops: root-only operations even if it somehow becomes uid 0. `no_new_privs` on top: the converter cannot run a `setuid` helper.
4. **File access.** Landlock (or SELinux, AppArmor if the distribution has them): read-only on the library directories, read and write on its one scratch directory, no network at all. Stops: `/etc`, `/home`, other uploads, connections out. Binds root.
5. **System calls.** A seccomp allow-list: `read`, `write`, `mmap`, `munmap`, `brk`, `futex`, `exit_group`, `clock_gettime`, a few more, **kill on anything else**, with the architecture check first. Stops: `socket`, `execve`, `ptrace`, `mount`, `open` of new paths, and the large unneeded surface of the kernel. Leaves: any bug in the calls that remain; a filter that is mostly number-only costs about 10 ns per call.
6. **Namespaces.** New user, PID, mount and network namespaces, a read-only root with nothing in it. Stops: seeing other processes, using the host network, seeing the host file system. Leaves: the shared kernel.
7. **Memory defences.** Build with a PIE, the stack protector, fortification and RELRO; ASLR and NX on (the defaults here). They make the bug the attacker needs to *find* harder to turn into control, and cost a few percent.
8. **Resource limits** (not covered in detail here, from memory): cgroup limits for memory, CPU and processes, `RLIMIT_*`, a wall-clock timeout, so a malicious image cannot exhaust the host.

**What remains**: a kernel bug reachable through the calls you allowed (a seccomp allow-list shrinks this and does not remove it), a side channel, and a logic bug in the service. State it, because the review is not "it is secure" but "this is what an attacker must do, and this is what they get". The standard answer to a kernel bug is a stronger boundary (a virtual machine, as Firecracker, which is what this sandbox is) at the price of start-up time and memory.

---

## What it costs

| Operation | Cost or effect (measured here, unless marked) | Why |
| --- | --- | --- |
| `getppid` through `syscall(2)`, no filter | 90 to 110 ns (baseline moved between runs) | the crossing of [OS 19](19_user_kernel_boundary.md) |
| seccomp filter looking only at architecture and number | about 10 ns added, flat from 7 to 407 instructions | the verdict is precomputed per syscall number (mechanism from memory) |
| seccomp filter that also loads an argument, 401 compares | +63.6 / +84.2 / +72.3 ns over three runs | the filter runs on every call, about 0.18 ns per compare |
| permission check by mode bits | part of `open`; not timed separately | one comparison against the inode, once per open (`read` and `write` do not repeat it) |
| `PR_SET_NO_NEW_PRIVS`, `capset`, `setresuid`, `landlock_restrict_self` | one system call each; not timed | credentials are changed once, at start-up |
| one-way hash, SHA-512-crypt 1,000 / 5,000 / 500,000 rounds | 0.296 / 1.739 / 159.637 ms | linear in the rounds, about 0.3 microseconds each |
| bcrypt cost 5 / 10 / 12 | 2.308 / 59.400 / 232.218 ms | doubles with each cost step |
| yescrypt `j9T` / `jDT` | 15.070 / 325.519 ms | memory-hard; cost classes |
| guesses per second per core against yescrypt `j9T` | about 66 | 1 / 15.07 ms |
| ASLR entropy: heap, `mmap`, `libc`, PIE executable / stack | about 28 bits / 22 bits (300 runs) | `vm.mmap_rnd_bits = 28`; stack randomised at a different granularity |
| call into a writable, non-executable page | `SIGSEGV`, `SEGV_ACCERR` | NX bit in the page table |
| stack protector (the canary check) | not timed here; from memory a few percent on call-heavy code | one compare per protected function return |

---

## Common mistakes

1. **Reading "other" as "everyone not listed".** Permission bits are checked by **first matching class**: owner, then group, then other. A group member of a file whose group bits are `---` is refused even when the other bits grant access (section 2.1, mode `0604`), and the owner of a mode `0044` file cannot read it.
2. **Thinking that deleting a file needs write permission on the file.** It needs write and search permission on the **directory** (section 2.2: a stranger unlinked a mode-`0000` file). A world-writable directory without the sticky bit lets anyone delete anyone's files; `/tmp` is `1777` for that reason.
3. **Testing permission logic as root.** Root passes almost every check (mode `0000` was readable and writable above), so a test run as root passes whatever the bits say. Test as the unprivileged identity, by forking and dropping with `setresuid`, as the programs here do.
4. **Dropping privilege in the wrong order, or not for good.** Clear groups and set the group IDs first, set the user ID last, use `setresuid(u, u, u)` (all three), and **check every return value**. A temporary `seteuid` drop leaves the saved ID at root, and any code in the process (including an attacker's) can come back (section 3.2).
5. **Relying on `chmod` to revoke access from a process that has the file open.** An open descriptor is a capability that survives the mode change and the loss of the user ID (section 3.4). To revoke, close it or end the holder.
6. **Granting a whole root or `CAP_SYS_ADMIN` when one capability would do.** A web server needs `CAP_NET_BIND_SERVICE` at most, `ping` needs `CAP_NET_RAW`. `CAP_SYS_ADMIN` is described as "the new root" in the manual page.
7. **Keeping capabilities permitted but not effective and assuming they work.** After `PR_SET_KEEPCAPS` and a `setuid`, `/proc` showed `CapPrm 000001fffeffffff` and `CapEff 0` and `bind` still failed (section 4.5). Raise what you need into the effective set, use it, then drop.
8. **Reading the group bits of `ls -l` as the group's rights when an ACL exists.** With a mask, the group bits **show the mask** (`rw-` displayed over a `group::r--` entry in section 5.2). Look at `getfacl`, or read the xattr.
9. **Trusting `umask` or the mode argument.** The mode you pass to `open(..., O_CREAT, 0666)` is the **most** the file can have; the umask removes bits and a default ACL can change the result. For a secret, set the mode explicitly with `fchmod`, or create it with `0600`.
10. **Turning SELinux off instead of reading the denial, or treating MAC as a replacement for DAC.** The policy denies by default and the audit log says exactly what; permissive mode logs without enforcing. MAC is an extra check: an access needs both the permission bits and the policy to allow it.
11. **Writing a seccomp deny-list, forgetting the architecture check, or installing the filter too early.** An allow-list cannot miss a new system call; the architecture word must be checked first; and the filter must be installed after every call the set-up needs (`unshare`, `mount`, `prctl`, `landlock_*`). Use `KILL_PROCESS`, not `KILL_THREAD` (section 7.2).
12. **Treating `SECCOMP_RET_USER_NOTIF` as a security boundary for pointer arguments.** The supervisor reads the memory at one moment and the kernel reads it again later (from memory; not on the manual page I fetched), so the decision can be about something else.
13. **Believing a namespace or a "root in a container" is safe because the user is root inside.** The capabilities inside a user namespace apply to its own resources (section 7.4), but the kernel is shared, and every syscall the sandbox allows is attack surface. Namespaces hide things; they do not shrink the kernel's code that the process can reach.
14. **Testing the canary with the default flags, or assuming it catches every overflow.** The default build here enabled fortification as well, which aborted at 17 bytes with "buffer overflow detected" before the canary was looked at; and with only the protector, 17 and 24 bytes overflowed the 16-byte buffer without a detection (section 8.4). A canary detects an overwrite that **reaches** it.
15. **Treating ASLR as protection against an information leak, or forgetting that `fork` keeps the layout and the canary.** One leaked pointer gives the base; a worker forked from a master shares the layout and the canary with every sibling.
16. **Storing passwords wrongly.** A fast hash, no salt, a cost that nobody tuned, a comparison that is not constant-time, a bcrypt password longer than 72 bytes (from memory), a PIN whose space is 10^4 (80 seconds against even a good hash, section 9.5), or a home-made scheme. Use the library's slow salted function, keep the cost in the stored string and rehash at the next login.

---

## Questions out loud

Answer each in two minutes, with a number or a diagram, before looking back (the section in brackets).

1. What is an access matrix, and what are its two ways of being stored? Which is a Unix permission bit and which is an open file descriptor? [1.1, 3.4]
2. A file is mode `0604`, owned by Alice, group `dev`. Bob is in `dev`. Can he read it? Can a stranger? Why? [2.1]
3. What do `r`, `w` and `x` mean on a directory? What permission does `unlink` need, and what does the sticky bit add? [2.2]
4. What are the real, effective and saved user IDs? Describe the life of a `setuid`-root program that drops privilege temporarily and later permanently. [3.1, 3.2]
5. In which order do you drop the group and the user ID, and why? [3.2]
6. A process opens a file as root, then becomes an unprivileged user. Can it still read? What if root then runs `chmod 0` on the file? What does that say about `chmod` as a revocation mechanism? [3.4]
7. What are Linux capabilities, and what is the difference between the permitted and the effective set? Why did `bind` fail after `PR_SET_KEEPCAPS` and `setuid`? [4.2, 4.5]
8. What does `CAP_DAC_READ_SEARCH` do that `CAP_DAC_OVERRIDE` does not? Why is `CAP_SYS_ADMIN` called "the new root"? [4.1, 4.4]
9. How do you let a program bind port 80 without running it as root? Give two ways. [4.5, 4.6]
10. What does `no_new_privs` do, and why must it be set before an unprivileged process installs a seccomp filter? [4.7, 7.2]
11. What is the mask in a POSIX ACL? What does `chmod g-w` do to a file with an ACL, and why does `ls -l` show `rw-` for a group whose entry says `r--`? [5.1, 5.3]
12. What can MAC do that DAC cannot? How do SELinux, AppArmor and Landlock differ, and which one can an unprivileged process apply to itself? [6.1 to 6.4]
13. What does a seccomp filter see, what can it return, how do two filters combine, and what does it cost? [7.2, 7.3]
14. What does a user namespace give an unprivileged user, and what does it not? Why did `sethostname` fail? [7.4]
15. What do NX, ASLR and a stack canary each stop, and what does each not? Why did the 17-byte overflow go unnoticed with the protector on? [8.2 to 8.4]
16. How do you store a password? Explain salt, cost and memory-hardness, and compute the guesses per second against one hash. [9.2 to 9.5]
17. Design the sandbox for a service that parses untrusted files, and say the order of the steps and what each layer stops. [7.5, 10]

---

## Problems

**Where to solve them.** On your own machine, as an unprivileged user and then as root, with the tools that were absent here: `id`, `stat`, `namei -l path` (shows the permission of every directory on a path), `getfacl` and `setfacl`, `getcap` and `setcap`, `capsh --print` and `capsh --drop=...`, `unshare -Ur` (a user namespace with you as root), `cat /proc/self/status`, `strace -f` to see which calls a program makes before you write its seccomp allow-list, and `sudo` for the root-only experiments. For practice problems of this kind: the `man7.org` pages `capabilities(7)`, `credentials(7)`, `seccomp(2)`, `user_namespaces(7)`, `acl(5)` and the kernel's `no_new_privs` and Landlock documents; and the wargame sites OverTheWire (Bandit, for permissions and `setuid`) and pwn.college (for sandboxes, shellcode and mitigations; both from memory, not checked). Then reproduce the tables here and compare.

**How to approach them.** For a permission question, write down the **identity** (user, groups), then check **every directory on the path** for `x`, then the file's class (owner, group, other, first match), and finally ask whether a capability or an ACL changes the answer. For an ID question, write the triple `(ruid, euid, suid)` after each call. For a sandbox question, list what the process needs, then the layers in the order of section 7.5, and say for each what it stops and what it leaves.

### Problem A (E, entry level): who can do what

Users: alice (uid 1000, group `dev`), bob (uid 1001, group `staff`), carol (uid 1002, groups `dev` and `staff`). The directory `/proj` is owned by alice, group `dev`, mode `0750` (no sticky bit). It holds three files: `a` (owner alice, group `dev`, mode `0640`), `b` (owner bob, group `staff`, mode `0664`) and `c` (owner alice, group `dev`, mode `0044`). For each of alice, bob and carol, can they: (a) read `/proj/a`; (b) read and write `/proj/b`; (c) read `/proj/c`; (d) delete `/proj/b`?

### Problem B (E, entry level): trace the IDs

A `setuid`-root program is run by uid 1000, so it starts as `(ruid, euid, suid) = (1000, 0, 0)`. Give the triple after each call and say which succeed: (1) `seteuid(1000)`; (2) `seteuid(0)`; (3) `setresuid(1000, 1000, 1000)`; (4) `seteuid(0)`. Then: what would the triple be if step (3) had been `setuid(1000)` called when the triple was `(1000, 1000, 0)`, and is it safe?

### Problem C (M, medium): the ACL and the mask

A file has the ACL `user::rw-`, `user:u2:rwx`, `group::r--`, `group:g2:rw-`, `mask::rwx`, `other::---`. (a) What mode does `ls -l` show? (b) What are the effective rights of u2, of a member of g2 who is not u2, and of a member of the owning group? (c) After `chmod g-w`, what are they, and what is the mode? (d) After `chmod 0600`? (e) A user who is in both the owning group and `g2` (and is not named): what rights?

### Problem D (M, medium): the budgets

(a) A service makes 2 million system calls a second on one core, each costing 100 ns. What fraction of the core do the system calls use, and what does adding a number-only seccomp filter (10 ns) cost, and a filter that reads arguments with 400 compares (72 ns)? (b) A login service must handle 200 verifications a second at peak on 8 cores and may use half of them. Which of these costs fit: yescrypt `j9T` (15.1 ms), yescrypt `jDT` (325.5 ms), bcrypt cost 10 (59.4 ms), SHA-512-crypt 50,000 rounds (15.8 ms)? What is the highest cost per verification that fits? (c) How many years (one core, 66.4 guesses a second, half the space) for a passphrase of 6 random words from a 7,776-word list, compared with 4 words (9.5)?

### Problem E (H, hard): the start-up sequence

A server must listen on port 443, read a private key readable only by root, parse untrusted input from the network, and append to a log file. Write the order of the start-up steps that leave it with the least power, and for each of these **wrong** orders say what fails: (a) the user ID is dropped before `bind`; (b) the user ID is dropped before the groups are cleared; (c) the seccomp filter is installed before the Landlock ruleset; (d) `no_new_privs` is never set and the process runs as the unprivileged user; (e) the log is reopened by name after sandboxing. What would you keep as a descriptor, and what would you give a separate privileged process to do?

---

## Answers

**A.** Start with `/proj` (mode `0750`, owner alice, group `dev`): alice is the owner, so `rwx`; carol is in `dev`, so the group bits `r-x`; **bob is "other", so `---`, and he cannot even traverse `/proj`.** So bob fails everything inside it, including his own file `b` (`EACCES`). (a) `a` (`0640`, group `dev`): alice reads (owner `rw-`), carol reads (group `r--`), bob no. (b) `b` (`0664`, owner bob, group `staff`): alice is neither bob nor in `staff`, so other `r--`: she **reads and cannot write**. Carol is in `staff`, so group `rw-`: **reads and writes**. Bob cannot reach it. (c) `c` (`0044`, owner alice, group `dev`): alice is the owner, so `---`: **cannot read** her own file; carol, in `dev`, has group `r--`: **reads**; bob no. (d) Deleting `b` needs `w` and `x` on `/proj`: alice has `rwx` as owner and **can delete bob's file** (no sticky bit); carol has `r-x` and cannot; bob cannot traverse. The point of the exercise: the directory decides reachability and deletion, and the first matching class decides the file.

**B.** (1) `seteuid(1000)` succeeds: `(1000, 1000, 0)`. (2) `seteuid(0)` succeeds, because the saved ID is 0: `(1000, 0, 0)`. (3) `setresuid(1000, 1000, 1000)` succeeds: `(1000, 1000, 1000)`. (4) `seteuid(0)` **fails with `EPERM`**: the triple holds no 0, so there is no way back (measured in section 3.2). If step (3) had been `setuid(1000)` at `(1000, 1000, 0)`: a process whose effective UID is not 0 sets **only the effective UID**, so it stays `(1000, 1000, 0)` and the saved root is still there: **not safe**, since `seteuid(0)` would succeed. `setuid` from a process with `euid 0` sets all three. This is why `setresuid` is used and its result checked.

**C.** (a) Owner `rw-`, group bits show the **mask** `rwx`, other `---`: mode **`0670`**. (b) u2: entry `rwx` AND mask `rwx` = **`rwx`**. A member of g2: `rw-` AND `rwx` = **`rw-`**. A member of the owning group: `r--` AND `rwx` = **`r--`** (the owning group's entry is also limited by the mask). The owner is not limited by the mask. (c) `chmod g-w` clears `w` in the mask: **`r-x`**. u2: `rwx` AND `r-x` = **`r-x`**; g2: `rw-` AND `r-x` = **`r--`**; owning group: `r--`. The mode is **`0650`**. (d) `chmod 0600` sets the mask to `---`: **everyone but the owner (and other, which is `---` anyway) loses all access**, regardless of their entries. (e) For a user matching more than one group entry, POSIX grants access if **any one** matching entry (with the mask) grants it: `rw-` from `g2`, so **read and write** (this is from memory of the POSIX rule; the fetched summary of `acl(5)` describes the order of checks but does not spell out the "any one" case, and I did not test it).

**D.** (a) 2 million calls times 100 ns is 0.2 s a second: **20 percent of the core**. A number-only filter adds 2,000,000 times 10 ns = 20 ms, **2 percent of the core** (a call goes from 100 to 110 ns). The 400-compare filter adds 2,000,000 times 72 ns = 144 ms, **14.4 percent of the core** (a call goes to 172 ns). So the filter shape matters more than the idea of having one. (b) 200 verifications a second times each cost, in core-seconds a second: `j9T` 200 x 0.0151 = **3.0** of 8 cores, which is **38 percent**: fits. `jDT` 200 x 0.3255 = **65** cores: does not fit. bcrypt cost 10 200 x 0.0594 = **11.9** cores: does not fit. SHA-512-crypt 50,000 rounds 200 x 0.0158 = **3.2** cores: fits. Half of 8 cores is 4 core-seconds a second, so the highest cost is 4 / 200 = **20 ms** per verification, and the cost parameters are chosen from the **peak** load, not the average. (c) 7,776^6 is 2.2 x 10^23; half of it at 66.4 a second is **about 5.3 x 10^13 years**, against **about 873,000 years** for 4 words (3.66 x 10^15 combinations). Each word adds log2(7,776) = 12.9 bits: 6 words is 77.5 bits, 4 words 51.7. The estimate is for one core against one good hash; a real attacker's rate differs, and what is useful is the ratio.

**E.** A good order: (1) **bind** port 443 (needs `CAP_NET_BIND_SERVICE`, or root) and `listen`; (2) **open** the private key and the log file and read the key into memory (or keep the descriptor); (3) optionally `unshare`/`pivot_root` into an empty root; (4) **`prctl(PR_SET_NO_NEW_PRIVS, 1)`**; (5) **clear supplementary groups, `setresgid`, then `setresuid`** to a dedicated user, so all capabilities go with the root user ID (and drop the bounding set); (6) **install Landlock**: read-only on library paths, no new files; (7) **install the seccomp allow-list last**, with the architecture check, allowing `accept4`, `read`, `write`, `epoll_*`, `mmap`, `futex`, `clock_gettime`, `exit_group` and the few others the code uses. Then run the worker. **Wrong orders:** (a) dropping the user ID before `bind` leaves no `CAP_NET_BIND_SERVICE` (section 4.5, plain `setuid` gave `Permission denied`), unless the capability was kept explicitly. (b) Dropping the user ID before the groups: `setgroups` then fails, and the process keeps root's supplementary groups (`shadow`, `disk`, ...), staying privileged (section 3.2). (c) Seccomp before Landlock: the filter blocks `landlock_create_ruleset`, `landlock_add_rule` and `landlock_restrict_self`, so the next step is killed or returns `ENOSYS`, or you shipped a policy without the Landlock layer if the error is ignored. (d) Without `no_new_privs` an unprivileged process cannot install the seccomp filter (`EACCES`, measured), and if it could by keeping a capability, a `setuid` helper could undo the confinement. (e) A log opened by name after sandboxing needs `open` in the allow-list and write access to the directory in the ruleset, which gives the attacker the same, so **keep the log descriptor** from step 2 and rotate by asking a privileged supervisor to hand over a new descriptor with `SCM_RIGHTS`. **Keep as descriptors:** the listening socket, the log and the key. **Give a privileged helper:** reopening files after rotation and reloading the certificate, over a Unix socket with the peer's credentials checked.

---

## Cheat sheet

- **Model**: access matrix. By column, an **ACL** (permission bits, POSIX ACL); by row, a **capability list** (an open **descriptor**). Check at `open`; credentials belong to the process. DAC is the owner's choice, MAC is a policy the owner cannot change, and both must allow.
- **Mode bits**: first matching class, **owner, then group, then other**, decides. Directory `r` lists, `w` changes entries (with `x`), `x` traverses. **Deleting needs the directory's `w`+`x`**; the **sticky bit** (`1777`, `/tmp`) restricts it to the file's owner. `umask` only removes bits at creation. Root bypasses the bits.
- **IDs**: real, effective, **saved**, filesystem. `setuid` bit gives euid and suid the owner's. **Drop for good** with `setresuid(u, u, u)`; groups first, user last; check returns. `suid` still 0 means root can come back.
- **Descriptor = capability**: survives `chmod` and the loss of the user ID; fresh opens are re-checked. Pass with `SCM_RIGHTS`; this is how privilege separation works.
- **Capabilities**: 41 (0 to 40), five sets (permitted, effective, inheritable, ambient, bounding); `CapEff` is what is checked. `uid 0` to non-zero clears them unless `PR_SET_KEEPCAPS`, which leaves effective empty. `capset` only lowers. `CAP_NET_BIND_SERVICE` (bit 10, `0x400`) for ports under 1024. `CAP_SYS_ADMIN` is "the new root". This sandbox's root lacks bit 24.
- **`no_new_privs`**: set once, inherited, **cannot be unset**; `exec` gives nothing (no `setuid`, no file capabilities). Needed for unprivileged seccomp (`EACCES` without it).
- **POSIX ACL**: `USER_OBJ`, `USER`, `GROUP_OBJ`, `GROUP`, **`MASK`**, `OTHER`; named entries need a mask. **Effective right = entry AND mask**; the mode's group bits **show the mask**; `chmod` rewrites the mask. Default ACLs are directory-only. Raw xattr: 4-byte version 2, 8-byte entries.
- **MAC**: LSM hooks, checked after DAC. **SELinux** (labels, type enforcement, default deny), **AppArmor** (path profiles), **Landlock** (self-applied, by directory and TCP port, binds root, needs `no_new_privs`). Only the accesses a ruleset **handles** are restricted.
- **seccomp**: classic BPF over `seccomp_data` (number, arch, ip, six args); verdicts `KILL_PROCESS > KILL_THREAD > TRAP > ERRNO > USER_NOTIF > TRACE > LOG > ALLOW`; filters stack, survive `fork` and `exec`, cannot be removed; **allow-list, check the arch first, install last**. Cost: number-only about 10 ns, argument-reading grows with length (+72 ns at 401 compares).
- **Namespaces**: user (full capabilities inside, none outside; maps written once; `setgroups deny`), mount, PID, net, UTS, IPC, cgroup, time. Isolation of the **view**, not of the kernel; only what you created is virtualised.
- **Memory defences**: NX (`SEGV_ACCERR`), **ASLR** (28 bits for heap, `mmap`, libs and PIE here; 22 for the stack; not a defence against a leak), **canary** (detects an overwrite that reaches it; low byte 00; shared by forked children), **FORTIFY** (aborts at `memcpy` with a known size), RELRO, PIE. Kernel: KASLR, `STRICT_KERNEL_RWX`, hardened usercopy, `VMAP_STACK`, SMEP, SMAP, `kptr_restrict`, `dmesg_restrict`.
- **Passwords**: never store them; **salted, slow, memory-hard** (yescrypt, bcrypt, scrypt, Argon2; not a fast hash); the stored string is its own setting; compare in constant time; tune the cost to peak load (15 ms yescrypt `j9T` is about 66 guesses a second per core); rate-limit and use a second factor.
- **Start-up order**: bind, open, `no_new_privs`, groups then user, Landlock, **seccomp last**; keep descriptors, use a broker for the rest.

---

## Sources

- [capabilities(7)](https://man7.org/linux/man-pages/man7/capabilities.7.html) (fetched): the capability list and descriptions (including `CAP_SYS_ADMIN` as overloaded), the five sets, the `execve` transformation formulas, the effect of user-ID changes on the sets, `SECBIT_KEEP_CAPS`, the file capability attribute `security.capability` and `CAP_SETFCAP`, the bounding set, `cap_last_cap`. The page lists 41 capabilities without a count; the count of 41 is from the header on this machine.
- [seccomp(2)](https://man7.org/linux/man-pages/man2/seccomp.2.html) (fetched): `no_new_privs` or `CAP_SYS_ADMIN` needed, the return actions and their precedence, stacking of filters, the architecture check and the x32 bypass, the `seccomp_data` fields, inheritance across `fork` and `execve`, `USER_NOTIF` with no supervisor returning `ENOSYS`, and 4-byte loads only. The page does not discuss the per-syscall caching of verdicts or the `USER_NOTIF` race on pointer arguments; both are from memory.
- [credentials(7)](https://man7.org/linux/man-pages/man7/credentials.7.html) (fetched): real, effective, saved and filesystem IDs, supplementary groups, which checks use which ID, and process IDs, groups and sessions. It leaves the `setuid`-bit behaviour of `execve` and the rules for changing IDs to other pages; the behaviour in section 3 is what the program printed.
- [user_namespaces(7)](https://man7.org/linux/man-pages/man7/user_namespaces.7.html) (fetched): capabilities inside and outside a user namespace, unprivileged creation since 3.8, the `uid_map` and `gid_map` rules (written once, `setgroups` deny), the 32-level nesting limit, overflow IDs, the treatment of `setuid` files with unmapped owners, and the dumpable-flag hazard. It does not describe `max_user_namespaces`; the value 32003 was read from `/proc/sys/user`.
- [acl(5)](https://man7.org/linux/man-pages/man5/acl.5.html) (fetched): the six entry types, the order of the access check, how the mask and the group bits correspond, valid ACLs, default ACLs. It does not say how `umask` interacts with a default ACL, how `ls` and `chmod` present the mask, or the "any matching group entry" rule, so the last is from memory.
- [no_new_privs](https://docs.kernel.org/userspace-api/no_new_privs.html) (fetched, kernel documentation): what the flag does to `setuid`, file capabilities and security modules, inheritance, irreversibility, introduction in 3.5, and the link to unprivileged seccomp.
- **Fetch failed:** `crypt(5)` (the page could not be retrieved). The scheme prefixes, which schemes are legacy, the 8-character limit of DES-crypt, the 72-byte limit of bcrypt and yescrypt as the recommended default are from memory; the strings and times in section 9 were produced by the library on this machine.
- **Not fetched, so from memory:** the Saltzer and Schroeder principles, the LSM hook list, SELinux contexts and AppArmor profiles, the Landlock history and its documentation, PAM, secure-execution mode of the loader, the Spectre and Meltdown cost, the kernel's seccomp result cache, namespace kinds beyond those used, RELRO, the exact causes of the stack-canary and ASLR weaknesses (byte-by-byte guessing, forking servers, 32-bit entropy), Argon2 and memory-hard functions, cgroup limits, and the wargame sites named under Problems.
- Measurements on this machine: Linux 6.18.44 in a Firecracker VM, 2 vCPUs, Intel Xeon at a nominal 2.10 GHz, GCC 13.3, glibc 2.39, nine programs, as root with a capability set missing `CAP_SYS_RESOURCE`. The kernel's build options come from `/proc/config.gz`. SELinux and AppArmor were not active or not built, so they are described from memory; Landlock (ABI 7) was run. The seccomp and hash timings carry the noise described under Data and timing, and the guess-rate figures are for one core and are not measurements of an attacker.
