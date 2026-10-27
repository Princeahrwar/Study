# CO 30: Hardware Security

*Part of the [Computer Organization plan](00_plan.md) · Skim tutorial · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 10](10_branch_prediction.md) (section 12: Spectre in short), [tutorial 11](11_superscalar_and_out_of_order_execution.md) (wrong-path work), [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (caches and `clflush`), [tutorial 15](15_virtual_memory_and_the_tlb.md) (page-table permissions, KPTI) and [tutorial 27](27_hardware_virtualization.md) (section 8.4: confidential VMs)*

By the end of this tutorial you can:

- state a threat model: what the attacker can run, what boundary the hardware is supposed to enforce, and which attacks cross it;
- explain how a cache timing channel works (Flush+Reload and Prime+Probe), measure the hit and miss times that make it possible, and say what it needs;
- write a comparison that takes the same time whatever the data, and explain what a compiler can undo;
- explain Meltdown and Spectre v1, v2 and v4 in a few sentences each, spot the bounds-check gadget in code, and say what each mitigation costs and where;
- say in one paragraph what Rowhammer is and why caches and speculation fixes do not touch it;
- say what NX, ASLR, stack canaries, RELRO, CET, pointer authentication and memory tagging each stop and what each misses;
- say what SGX, TrustZone and confidential VMs protect, from whom, and what they leave open;
- explain secure boot, measured boot and what a TPM adds, and read `/sys/devices/system/cpu/vulnerabilities/` on a machine you are given.

The earlier tutorials made hardware fast by keeping state: caches, predictors, buffers, queues, shared between everything that runs. This tutorial is about the price. State that is shared across a security boundary can be observed across it, and a core that guesses ahead can be made to guess wrongly on purpose. For a Senior or Staff interview this is the tutorial behind questions such as "why must this comparison be constant-time", "what did KPTI cost and why", "what does SGX actually protect", and "what happens between power-on and the first line of the kernel".

**Where the numbers come from.** The virtual machine of tutorials 14 to 29 (two vCPUs of an Intel Xeon under KVM, Linux 6.18, GCC 13) is the place I measured. **Measured here:** the gap between a cache hit and a flushed line and a byte sent through the cache and read back (section 2), the time of a naive and a constant-time compare (section 3), the cost of the software fixes for a bounds-check gadget, of retpoline and of the Speculative Store Bypass switch (section 4), a test that executes data (section 6), the spread of ASLR over 300 runs, the protections of a compiled binary, and everything under `/sys/devices/system/cpu/vulnerabilities/`. **Not measured, because this VM cannot show them:** Meltdown (the CPU is `Not affected`), the other buffer-leak attacks, Rowhammer, SGX, TDX and SEV (the flags are absent), CET, memory tagging and pointer authentication (an Arm feature), TrustZone, a TPM and secure boot (there is no UEFI and no `/dev/tpm0`). For those the facts come from published sources linked where used: the Linux kernel documentation on [Spectre](https://docs.kernel.org/admin-guide/hw-vuln/spectre.html), [attack vector controls](https://docs.kernel.org/admin-guide/hw-vuln/attack_vector_controls.html), [page-table isolation](https://docs.kernel.org/arch/x86/pti.html), [the x86 shadow stack](https://docs.kernel.org/arch/x86/shstk.html), [Arm memory tagging](https://docs.kernel.org/arch/arm64/memory-tagging-extension.html) and [Arm pointer authentication](https://docs.kernel.org/arch/arm64/pointer-authentication.html); the [Meltdown and Spectre site](https://meltdownattack.com/); Yarom and Falkner, ["FLUSH+RELOAD"](https://eprint.iacr.org/2013/448); Brumley and Boneh, ["Remote Timing Attacks are Practical"](https://crypto.stanford.edu/~dabo/papers/ssl-timing.pdf); Google's [retpoline page](https://support.google.com/faqs/answer/7625886); and the Wikipedia articles on [SGX](https://en.wikipedia.org/wiki/Software_Guard_Extensions), [TrustZone](https://en.wikipedia.org/wiki/TrustZone), the [TPM](https://en.wikipedia.org/wiki/Trusted_Platform_Module) and [UEFI](https://en.wikipedia.org/wiki/UEFI). I did not fetch the original Spectre and Meltdown papers; the gadget in section 4.3 is the textbook form of the one in the Spectre paper. Details that none of those pages gave (the NS bit and the TZASC and TZPC controllers of TrustZone, how a PCR is extended, the names `db` and `dbx` of the UEFI signature lists) are standard architecture knowledge that I did not check against Intel's or Arm's manuals; they are marked "standard" where they appear. **I wrote no exploit.** Every program here measures a timing, tests a protection or reads a setting, always on the program's own memory, and the byte "sent through the cache" is sent from one function to another in the same process.

> **The one-sentence model.** The hardware draws boundaries (user and kernel, process and process, guest and host, enclave and the rest) and then, for speed, shares state across them (caches, predictors, buffers, DRAM rows, even the clock), so an attacker either reads shared state through time (a **side channel**), makes the core compute something forbidden for a few cycles and reads what it left behind (**transient execution**), or corrupts memory directly (**Rowhammer**, memory-safety bugs); a defence removes the sharing, removes the precision, or makes the attack's first step fail (randomise, tag, sign, verify), and almost every one of them has a price you can measure.

---

## 1. Threat model: who attacks which boundary

Security questions start with "who is the attacker and what can they already do". The attacker in this tutorial can **run code** (an unprivileged process, a JavaScript function, a guest VM, a co-tenant) and can **read a clock**. They cannot read the victim's memory directly; the hardware's access checks stop that. Everything below is a way around the check, or around the clock.

| Boundary | Enforced by | Attacker | Victim | Typical attack |
|---|---|---|---|---|
| Process to process | page tables, [tutorial 15](15_virtual_memory_and_the_tlb.md) | any process | another process | cache timing (section 2), Spectre v1 and v2 |
| User to kernel | privilege level, U/S bit, SMEP and SMAP | unprivileged code | kernel memory | Meltdown, Spectre in kernel gadgets, memory-safety bugs |
| Guest to host, guest to guest | EPT/NPT and exits, [tutorial 27](27_hardware_virtualization.md) | a VM | hypervisor, other VMs | L1TF, Spectre across SMT siblings, Rowhammer |
| Enclave or secure world to everything else | SGX, TrustZone, SEV/TDX (section 7) | a malicious OS or hypervisor | the protected code and data | side channels, fault injection, bugs inside the enclave |
| Software to the machine itself | secure boot, measured boot (section 8) | anyone who can write the boot media | the whole chain | a replaced boot loader, a rollback to a vulnerable version |
| Physical | none of the above | someone holding the machine | everything | bus probing, cold boot, DMA from a device |

Two words used throughout. A **side channel** is information that leaks through how something is done (time, power, which cache line changed), not through what it returns. A **covert channel** is the same mechanism used on purpose by two parties who are not supposed to talk. Spectre-style attacks use a side channel to read a secret and a covert channel's receiving half (section 2.3) to get it out of the core.

**What this VM tells you about its own setting.** The kernel command line begins `mitigations=auto,no_guest_host,no_guest_guest`. The kernel documentation on attack vector controls defines five vectors: user to kernel, user to user, guest to host (a malicious VM trying to read hypervisor data), guest to guest, and cross-thread (an SMT sibling). `auto` enables what each CPU needs for the vectors left on, and the two `no_guest_` options switch off the two vectors that only matter to a machine that hosts untrusted VMs. This VM hosts none, and it has one thread per core (`/sys/devices/system/cpu/smt/active` reads `0`). The same line on a machine you manage for tenants would be a mistake. Reading the command line is the first step of any "why is this machine slow or exposed" question.

---

## 2. Side channels: reading shared state through time

### 2.1 The idea

If the time of an operation depends on state that a secret influenced, then whoever can measure the time learns something about the secret. The state can be anything shared:

| Shared state | What the attacker learns | Needs | Related tutorial |
|---|---|---|---|
| Data cache (L1, L2, L3) | which lines the victim touched | a timer; sharing a cache level | [14](14_cache_optimisation_and_the_real_hierarchy.md) |
| Branch predictor, BTB | which way the victim's branches went | co-residence on a core | [10](10_branch_prediction.md) |
| TLB | which pages the victim touched | co-residence on a core | [15](15_virtual_memory_and_the_tlb.md) |
| Execution ports, multiplier | what instruction mix the sibling runs | an SMT sibling | [18](18_multicore_and_multithreading.md) |
| DRAM row buffer | which rows the victim used | the same bank | [16](16_dram_memory_controllers_and_numa.md) |
| Power and frequency | how much work the victim did | a way to see them | [28](28_performance_and_power.md) |

The cache is the workhorse, because it is large, shared by default, and has a big, easy-to-read gap between hit and miss.

### 2.2 Measured: how big is the gap

The program `gap.c` (Problem A, 74 lines) times one load with `lfence; rdtscp; lfence` around it. The fences stop the core from moving the load across the timer reads ([tutorial 11](11_superscalar_and_out_of_order_execution.md)). It times a warm line, a line right after `clflush`, and an "empty" measurement (two timer reads with nothing between them) to show the cost of the timer itself. The timer ticks at 2.1 GHz (the TSC rate of this VM), so 1 tick is 0.48 ns.

```text
ticks      p1   p50   p99
empty      46    48    58   (two timer reads, nothing between them)
hit        58    60    72
miss      218   226   728   (after clflush)
threshold 143 ticks: 25 of 100000 hits look like misses, 0 of 100000 misses look like hits
```

- **The timer itself costs about 48 ticks (23 ns).** The hit is only 12 ticks above the empty measurement: an L1 hit is below what this timer can resolve, and what you read is mostly the timer.
- **The miss is 166 ticks (79 ns) above the hit** in this run, and 200 ticks (95 ns) above in the other two (p50 miss 226, 262 and 266 ticks over three runs; hit 60 to 66). The ratio of miss to hit is 3.8 to 4.4 times, and the p1 of the misses (218) is 146 ticks above the p99 of the hits (72). **A single measurement tells the two apart almost every time**: with the midpoint threshold, 25 hits in 100,000 were read as misses and no miss was read as a hit.
- **The tail of the miss is long** (p99 728 ticks, 347 ns), from refresh stalls and other tenants ([tutorial 16](16_dram_memory_controllers_and_numa.md)). A decoder must take the minimum over a few samples or pick a threshold that tolerates it.
- [Tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) recorded 29 ns for the hit and 203 ns for the flushed load with a different timer on an earlier session. The absolute miss time depends on the host and on what else it was running; the conclusion (a gap of tens to a hundred nanoseconds, easy to see) did not change.

### 2.3 Flush+Reload

The attacker and the victim share some read-only memory (a shared library, the same executable, or a page that the host merged by deduplication). The attacker runs a loop of three steps on one line of it:

1. **Flush** the line with `clflush`, which removes it from every cache level.
2. **Wait** a short time while the victim runs.
3. **Reload** the line and time it. A hit means the victim touched it in the wait; a miss means it did not.

The published attack of Yarom and Falkner ([FLUSH+RELOAD](https://eprint.iacr.org/2013/448)) works on the last-level cache, so the attacker and the victim need not share a core, and it needs shared pages. They report extracting a GnuPG 1.4.13 private key, recovering on average 96.7 % of the key bits from a single signature or decryption, between two processes and between two VMs. The lines watched belong to the square and multiply routines of the modular exponentiation, whose order follows the key bits (the mechanism the paper describes; I fetched only its abstract).

The same measurement is also the **receiving half of every transient-execution attack** (section 4): something makes a line that depends on a secret get cached, and Flush+Reload reads which line. To see that half work without attacking anything, `gap.c` sends a byte from one function to another in the same process: a probe array has 256 lines, one per possible byte; the "victim" function reads `probe[secret * 4096]`; the "receiver" times all 256 lines and returns the one that is fast.

```text
byte sent through the cache: 19869 of 20000 recovered (99.34 %), 72.6 us per byte
```

The numbers over three runs: 99.1 to 99.3 % correct, 65 to 73 µs per byte (about 14 to 15 KB/s; the 256 flushes and 256 timed misses dominate). Chance is 0.4 %. Four details are not optional, and I broke each to see (same program, only the one change; 20,000 trials):

| Change | Bytes recovered | Why |
|---|---:|---|
| none (stride 4096, scrambled order, probe array written first) | 99.3 % | the baseline |
| stride 64 instead of 4096 | 6.5 % | the adjacent-line prefetcher pulls in neighbours, so many lines look cached |
| stride 512 | 18.4 % | the same, a little less |
| read the lines in order 0, 1, 2, ... | 1.4 % | a stride prefetcher sees the pattern and runs ahead |
| no `memset` of the probe array before use | 2.3 % | an untouched anonymous page is the shared zero page, so all 256 virtual lines are one physical line |

### 2.4 Prime+Probe

When there is no shared memory, the attacker uses the cache itself as the signal. They **prime** a cache set by filling it with their own lines, **wait**, then **probe** by re-reading those lines and timing them. If the victim used the set, it evicted one of the attacker's lines, and the probe is slower. It needs no `clflush` and no shared page. It needs an **eviction set**: enough addresses that map to the same set. In the L1 data cache of this machine (48 KiB, 12 ways, 64 sets, 64-byte lines) the set is chosen by address bits 6 to 11, which lie inside the 4 KiB page offset, so addresses 4096 bytes apart share a set and the attacker needs no physical addresses. In a shared L3 the set index uses physical-address bits, and the attacker has to discover which lines collide. With an inclusive L3, evicting a line there also removes it from the private caches of the core that holds it (back-invalidation, [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md)), which is how an attacker on one core can disturb another core's L1; Intel server parts since Skylake-SP use a non-inclusive L3, so that route is narrower there.

The signal is much smaller than in Flush+Reload. Evicting one of twelve lines turns one L1 hit into an L2 hit, a few nanoseconds, against the roughly 80 to 95 ns of a flushed line. **I tried it** in the same process (a 60-line program, not included): prime all 64 sets, let a function touch one line in one set, probe the sets in reverse order, and pick the set with the largest increase over its own quiet baseline. Over several runs the right set was chosen in 9 to 34 % of single rounds, against 1.6 % for a random guess, and averaging 4, 16 or 64 rounds did not improve it reliably (accuracy even fell in one run), most likely because drift in the baseline was larger than the signal on this shared host; I did not investigate further. **So in this VM I can show that the signal exists, not that it is usable.** Real Prime+Probe attacks work with many repetitions, filtering, a quiet machine and an attacker pinned to the same core or a sibling; the weak signal is the reason they are harder than Flush+Reload and the reason cloud noise is a (partial) defence.

### 2.5 Defences

| Defence | What it removes | Cost or limit |
|---|---|---|
| Do not share memory across trust domains (no page deduplication between tenants, separate copies of libraries) | the shared page Flush+Reload needs | memory |
| Partition the cache between tenants (Intel Cache Allocation Technology, page colouring) | the shared sets Prime+Probe needs | less cache per tenant |
| Do not co-schedule different tenants on SMT siblings (core scheduling, `nosmt`) | the shared L1, ports and predictors ([tutorial 18](18_multicore_and_multithreading.md)) | up to half the threads, or less flexible scheduling |
| Coarsen or remove fine timers (browsers do this) | precision | slows an attacker, does not stop one: a counter thread is a timer |
| Write code whose memory accesses and branches do not depend on secrets (section 3) | the leak at the source | effort; sometimes speed |

The last row is the only one a library author controls.

---

## 3. Constant-time code

### 3.1 A leak with no cache involved

A comparison that stops at the first difference tells the caller how many leading bytes matched, through the time it took. That is enough to recover a secret byte by byte. Brumley and Boneh ([Remote Timing Attacks are Practical](https://crypto.stanford.edu/~dabo/papers/ssl-timing.pdf)) timed an OpenSSL-based web server from another machine on the local network, three routers and several switches away, and extracted its private key; no shared cache was needed, only the server's response time.

The rules for code that handles secrets (keys, MAC tags, passwords, plaintext of a cipher):

1. **No branch on a secret.** A branch takes different time down each path, and teaches the predictor.
2. **No memory address that depends on a secret.** A table lookup `T[secret]` touches a secret-dependent cache line, which Flush+Reload and Prime+Probe read. This is why table-based AES leaked and why hardware AES instructions and bitsliced implementations exist.
3. **No variable-time instruction on a secret.** Integer division and some older multipliers take time that depends on the operands. Some architectures let software ask for operand-independent timing (Arm's DIT bit, Intel's data-operand-independent-timing mode); check the documentation of the CPU you target.

### 3.2 Measured: an early-exit compare against a constant-time one

```c
int cmp_naive(const uint8_t *a, const uint8_t *b, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;             // leaves at the first difference
    return 1;
}
int cmp_ct(const uint8_t *a, const uint8_t *b, int n) {
    uint8_t d = 0;
    for (int i = 0; i < n; i++) d |= a[i] ^ b[i];   // always reads every byte
    __asm__ volatile("" : "+r"(d));                 // stop the compiler turning it back into an early exit
    return d == 0;
}
```

The program compares a 64-byte secret with a guess whose first `k` bytes are right and whose byte `k` is wrong (`cmp.c`, Problem C; the eight cases are interleaved inside each round so that a slow patch of the host hits all of them equally; 2,000 compares per timed sample, 4,001 samples):

```text
early-exit compare: median ns per compare (10th .. 90th percentile)
  k =  0 :   1.78   (1.78 .. 1.97)
  k =  1 :   3.11   (3.10 .. 3.42)
  k =  8 :   6.79   (6.19 .. 8.08)
  k = 16 :   7.38   (7.37 .. 11.79)
  k = 32 :  12.13   (12.12 .. 21.99)
  k = 48 :  17.34   (16.83 .. 32.42)
  k = 63 :  23.32   (21.26 .. 42.53)
  k = 64 :  40.02   (40.01 .. 47.71)
constant-time compare, all eight values of k:  22.33 or 22.34   (20.37 .. 32.1 to 32.7)
```

- **The early-exit time rises by about 0.34 ns per extra matching byte on average**: from 1.78 ns at `k` = 0 to 23.32 ns at `k` = 63, roughly one byte per cycle at the core's roughly 3 GHz. The curve is not a straight line (3.11, 6.79 and 7.38 ns at `k` = 1, 8 and 16), because the predictor handles short and long loops differently. A caller who can time the compare learns `k`.
- **The constant-time version is flat at 22.34 ns** for every `k`, to the second decimal, and costs what the worst case of the early-exit version costs. That is the price: **constant time means worst-case time every time**; a wrong guess is no longer cheap.
- **`k` = 64, all bytes right, took 40 ns here, more than `k` = 63.** In another build of the same source it took 23.6 ns. The loop is the same machine code; what changed is whether the predictor could predict the exit of a 64-iteration loop. It is a second reminder that time depends on predictor state, and it is itself information. The claim that holds in every run is the rise from 1.8 to 23 ns and the flat line.

**What an attacker does with the rise.** Guess byte 0 over all 256 values and keep the one that takes longest; then byte 1; and so on. That is at most `256 × n` guesses for an `n`-byte secret instead of `256^n`: 4,096 for a 16-byte tag instead of about 3.4 × 10^38. Noise is the defender's friend. The average slope is 0.34 ns per byte, and if each timing has standard deviation `σ`, telling two neighbouring values apart takes about `(2σ/δ)²` samples, with `δ` = 0.34 ns. With `σ` = 100 ns (a quiet local call) that is about 350,000 samples per decision; with `σ` = 10 µs (across a network) about 3.5 × 10^9, which is why remote attacks need either a bigger leak per step or very many requests, and why Brumley and Boneh's result was a surprise.

### 3.3 What goes wrong in practice

- **The compiler is not on your side.** The C standard has no notion of time, so nothing forbids a compiler from turning the OR-of-XORs into an early exit or a mask-and-select into a branch. GCC 13.3 did neither here: at `-O2` the loop has one branch, the loop condition, and at `-O3` it is vectorised with `pxor` and `por`. That is a fact about this compiler version, not a guarantee, and the empty `asm` above is insurance. The robust answer is to use a vetted routine (`CRYPTO_memcmp` in OpenSSL, `crypto_verify_32` and `sodium_memcmp` in libsodium, `crypto_memneq` in the Linux kernel, `timingsafe_bcmp` on the BSDs, `subtle.ConstantTimeCompare` in Go) and to **read the assembly** of your own.
- **Secret-dependent selection** done as `r = bit ? a : b` may compile to a branch. The branch-free form is `mask = -(uint32_t)bit; r = (a & mask) | (b & ~mask);`, and it also needs checking.
- **Tools.** Valgrind-based checks that mark secrets as uninitialised memory (the idea behind `ctgrind`) flag secret-dependent branches and addresses; statistical tests (`dudect`) time the code on two input classes and test whether the distributions differ.
- **Early exit is not only a comparison.** Rejecting a bad padding before a bad MAC, returning an error sooner for an unknown user name than for a wrong password, and a hash table whose lookup time depends on a secret key are the same bug.

---

## 4. Transient execution: Spectre, Meltdown and relatives

### 4.1 What they have in common

A core runs ahead of the branch or the permission check it is waiting for ([tutorial 10](10_branch_prediction.md), [tutorial 11](11_superscalar_and_out_of_order_execution.md)). If the guess was wrong, the results are thrown away, and **the program never sees them**, but the cache lines they touched stay in the cache. Every attack in this family has the same three parts:

1. **Make the core speculate** into code or data it should not use (train a predictor, or use a load whose permission check is late).
2. **Make the speculative work depend on a secret and touch a cache line that depends on it** (`probe[secret * 4096]`).
3. **Read which line is cached** with the Flush+Reload receiver of section 2.3.

Part 3 is the receiver already built in section 2.3 and part 2 is ordinary code, so the fixes concentrate on part 1: stop the core from speculating there, or stop the speculation from seeing the secret.

### 4.2 Meltdown, and what KPTI does

On affected cores, a load from user mode to a kernel address gets its data forwarded to the next instructions **before** the permission fault is raised. The fault does arrive, but a following instruction like `probe[value * 4096]` has already run, so kernel memory can be read, one byte at a time, through the cache. The [Meltdown site](https://meltdownattack.com/) describes it as breaking the separation between user applications and the operating system, and lists Intel processors with out-of-order execution since 1995 (except Itanium and Atom before 2013) and some Arm cores as affected. The bug is a missing ordering between the check and the forwarding; new silicon performs the check first, and this VM's CPU says `Not affected`.

**KPTI** (kernel page-table isolation) is the software fix for the CPUs that are affected. Before it, the kernel was mapped (supervisor-only) into every process's page tables, which made system calls cheap ([tutorial 15](15_virtual_memory_and_the_tlb.md), section 11.4). With KPTI, the user-mode page tables hold only what is needed to enter and leave the kernel: the entry and exit code, the interrupt descriptor table, and the small per-CPU area and task-state segment (kernel documentation on [page-table isolation](https://docs.kernel.org/arch/x86/pti.html)). A kernel address that is not mapped in user mode cannot be read speculatively, because there is no translation to forward. The price, as that page lists it: a `CR3` write on every system call, interrupt and exception entry and exit, about a hundred cycles each; global TLB entries cannot be used for kernel pages that exist in only one set of tables; and about 4 KB of extra page-table memory per process. **PCID** keeps TLB entries across the `CR3` writes; without it every write flushes the whole TLB. A workload with few kernel entries pays almost nothing; a workload with millions of small system calls or many interrupts pays the most. I could not measure KPTI here (the CPU is not affected, so the kernel does not enable it); tutorial 15 measured `getppid` at 95 to 120 ns on this VM without it.

### 4.3 Spectre v1: the bounds-check gadget

```c
if (x < array1_size)                  // the check the attacker makes the core guess wrongly
    y = array2[array1[x] * 4096];     // load 1 reads array1[x], out of bounds; load 2's address depends on it
```

This is the textbook form. The attacker calls the code many times with in-bounds `x`, so the predictor learns "taken", then once with an out-of-bounds `x`. The core predicts taken, runs both loads with `array1[x]` being a byte of someone else's memory, finds out the check failed, and discards the results. The line `array2[secret * 4096]` stays cached. A gadget needs three things, and a code reviewer should look for them:

1. a **conditional bounds check** on an index the attacker controls;
2. a **load that uses the index** (the out-of-bounds read);
3. a **second load whose address depends on the first load's value** (the part that puts the secret into the cache). Without it, nothing in the cache records the byte.

**The fixes, measured.** Three versions of the loop `if (x < n) s += a[x];`, with `a` 16 KiB (in L1) and 4,096 indexes that are always in bounds so the branch is predicted well:

```c
if (x < n)               s += a[x];                    // plain
if (x < n) { _mm_lfence(); s += a[x]; }                // a barrier after the check
if (x < n) { x &= mask_nospec(x, n); s += a[x]; }     // cmp n, x ; sbb m, m   gives m = all ones if x < n, else 0
```

| Version | ns per access (5 runs) | Compared with plain |
|---|---:|---:|
| plain | 0.31 to 0.34 | 1.0 |
| `lfence` after the check | 5.9 to 6.5 | 19.1 to 19.6 times |
| index mask after the check | 0.60 to 0.66 | 1.93 to 1.96 times |

All three computed the same sum. `lfence` does not let any later instruction start until all earlier ones have finished, so the loads, which normally overlap at about one per cycle, now run one at a time: 6 ns is some 18 cycles per access. The mask is computed from the comparison **as data**, so the load's address cannot be formed before the comparison is done, whatever the predictor guessed: it costs three instructions (compare, subtract-with-borrow, and) on the address path, and no serialisation. This is the Linux kernel's approach (its `array_index_nospec()`), and, per the documentation, it uses `lfence` in the copy-from-user path and at the `swapgs` points of interrupt, exception and NMI entry. **These costs are per gadget.** The kernel and libraries protect the sites found to be gadgets, not every load, so the whole-system cost is far below 19 times. Compiler options that protect every load (`-mspeculative-load-hardening` in Clang) are the ones whose cost shows.

### 4.4 Spectre v2: poisoning the indirect branch predictor

The victim's indirect call or jump (a function pointer, a virtual call, a `switch` table, a kernel system-call table) is predicted from a table that attacker code can train, if the table is shared across the boundary. The attacker trains it so the victim speculatively jumps to a **gadget of the attacker's choosing** (any code in the victim's address space that reads a secret and encodes it in the cache). There are four kinds of fix, and the kernel chooses among them per CPU.

| Fix | What it does | Cost, measured here or from the kernel documentation |
|---|---|---|
| **Retpoline** | replace each indirect branch by a sequence that parks speculation in a harmless loop | measured below: a well-predicted call is 5 to 8 times slower |
| **IBRS / eIBRS** | a hardware mode in which predictions made in a lower-privilege mode do not steer higher-privilege code; eIBRS is set once at boot | the kernel documentation calls eIBRS more efficient than retpoline; not measured separately here |
| **IBPB** | a barrier that clears the branch target buffer when switching to or from a restricted task | `conditional` here: only around tasks that have restricted indirect-branch speculation; a cost on each such switch |
| **STIBP** | stops a predictor being shared with the SMT sibling | on Intel, eIBRS also enables it; this VM has one thread per core |

**Retpoline** (Google's [description](https://support.google.com/faqs/answer/7625886): the name is "return" plus "trampoline") turns `jmp *%rax` into this thunk. GCC 13 generated it for `-mindirect-branch=thunk`; I disassembled the result:

```text
__x86_indirect_thunk_rax:
        call  1f                 ; pushes the address of the loop below, and the return stack buffer remembers it
2:      pause                    ; the capture loop: only the speculative path ever runs it
        lfence
        jmp   2b
1:      mov   %rax,(%rsp)        ; overwrite the return address with the real target
        ret                      ; architecturally goes to *rax; the predictor guesses the capture loop
```

The `ret` is predicted from the return stack buffer, which holds the capture loop, so the core spins harmlessly until the real target is known. It does not use the indirect predictor at all. The price is that **every** indirect call now behaves like a misprediction. Same loop, ordinary indirect calls and retpoline calls (`ind.c`, 8,192 calls through a table of four tiny functions):

| Call pattern | Ordinary, ns | Retpoline, ns |
|---|---:|---:|
| one target, always | 1.62 | 12.51 |
| four targets, repeating pattern | 1.95 | 12.65 |
| four targets, random order | 8.26 | 12.50 |

Over four runs, retpoline stayed at 12.5 to 13.3 ns whatever the pattern; an ordinary predicted call took 1.6 to 2.5 ns and a random one 8.2 to 9.8 ns. **So retpoline costs 5 to 8 times on predictable calls and 1.3 to 1.6 times on random ones**, which is why it hurts code that dispatches through function pointers in a hot loop (a kernel, an interpreter, a C++ program with virtual calls) and barely shows elsewhere. It also needs the compiler to cooperate. The first thing I got from this Ubuntu GCC was `error: '-mindirect-branch' and '-fcf-protection' are not compatible`, because the distribution turns on `-fcf-protection` by default; the working command line is `gcc -O2 -fcf-protection=none -mindirect-branch=thunk`.

**What this VM uses.** `spectre_v2` reads `Mitigation: Enhanced / Automatic IBRS; IBPB: conditional; PBRSB-eIBRS: SW sequence; BHI: Vulnerable`. The CPU has eIBRS, so the kernel uses it and not retpoline. `PBRSB-eIBRS: SW sequence` is a software sequence that clears the return stack buffer at the points where stale entries could be used. **BHI** (branch history injection) is the reason eIBRS alone is not the end: the kernel documentation notes that with eIBRS the branch history buffer is not isolated between modes, so an attacker can still steer a victim's indirect branch through the branch history; full mitigation uses a hardware control (`BHI_DIS_S`) or a software sequence that clears the history, and may need a microcode update. `BHI: Vulnerable` in a guest means this kernel sees no control that it can use. It cannot tell you whether the host has applied one. **A line inside a VM describes what the hypervisor exposes**, not necessarily what the hardware does.

### 4.5 Spectre v4: speculative store bypass

A load may run before an older store whose address is not yet known, assuming they do not overlap, and read stale data when they do ([tutorial 20](20_memory_consistency_and_atomics.md) for how the store buffer fits in). The fix is the **SSBD** control, which stops the core from doing that. On this VM `spec_store_bypass` reads `Mitigation: Speculative Store Bypass disabled via prctl`: the kernel does not switch it on globally, and a thread can ask for it. `/proc/self/status` shows the thread's state, and `prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, PR_SPEC_DISABLE, 0, 0)` changes it:

```text
at start:               Speculation_Store_Bypass: thread vulnerable
after PR_SPEC_DISABLE:  Speculation_Store_Bypass: thread mitigated
```

The cost depends on whether the program has loads sitting in the shadow of stores with late addresses. A loop that has exactly that:

```c
buf[idx] = i;                          // store; its address arrives with idx
uint32_t z = other[i & 63];            // younger load, never overlaps; always 0
idx = chain[(idx + z) & (N - 1)];      // the next idx needs z, so a load that must wait stalls the chain
```

ran at 2.1 to 2.9 ns per iteration with the bypass allowed and 4.1 to 4.7 ns with it disabled, **a factor of 1.5 to 2.1 over nine runs (2.0 in three of them)**. That is a loop built to show the effect. Code without the pattern will see less, and I did not measure real software. The programs that turn it on are those that run untrusted code inside their own process (a JIT, a script engine, a sandbox).

### 4.6 The family, as `sysfs` names it

`/sys/devices/system/cpu/vulnerabilities/` has one file per known flaw. This VM's answers, with what each flaw is:

| File | This VM | The flaw in a sentence |
|---|---|---|
| `meltdown` | Not affected | user code reads kernel memory transiently (section 4.2) |
| `spectre_v1` | Mitigation: usercopy/swapgs barriers and `__user` pointer sanitization | bounds-check bypass (section 4.3) |
| `spectre_v2` | Mitigation: Enhanced / Automatic IBRS; IBPB: conditional; PBRSB-eIBRS: SW sequence; BHI: Vulnerable | branch target injection, return stack and history (section 4.4) |
| `spec_store_bypass` | Mitigation: ... disabled via prctl | speculative store bypass (section 4.5) |
| `l1tf` | Not affected | a page-table entry marked not present can still be used to read data that is in the L1 cache; broke VM and SGX isolation |
| `mds`, `tsx_async_abort`, `mmio_stale_data`, `srbds`, `reg_file_data_sampling`, `gather_data_sampling` | Not affected | stale data left in internal buffers (fill, store, load and register buffers) is forwarded to a faulting load; the `md_clear` flag, present here, means the `VERW` instruction clears those buffers |
| `retbleed` | Not affected | on some CPUs a `ret` is predicted like an indirect branch when the return stack buffer is empty |
| `itlb_multihit`, `spec_rstack_overflow`, `tsa`, `vmscape`, `ghostwrite`, `indirect_target_selection`, `old_microcode` | Not affected | newer or vendor-specific entries; read the kernel's hardware-vulnerability pages for the one you meet |

`Not affected` means the kernel's list of CPU models and the capability bits that the hypervisor passes to the guest (`arch_capabilities` is among this VM's flags) say the flaw is absent. `Mitigation: ...` means it is present and handled. `Vulnerable` means it is present and nothing is active. In a guest, each answer is only as good as what the hypervisor exposes.

### 4.7 What the fixes cost, together

| Fix | Where it is paid | Cost |
|---|---|---|
| Index mask | each gadget | 1.9 times on the access (measured) |
| `lfence` | each gadget | 19 times on the access (measured) |
| Retpoline | every indirect call | 5 to 8 times on a predicted call, 1.3 to 1.6 times on a random one (measured) |
| eIBRS | once, at boot | no per-call cost that I measured; cheaper than retpoline per the kernel documentation |
| KPTI | every kernel entry and exit | a `CR3` write each, about a hundred cycles each per the kernel documentation; not active on this CPU |
| SSBD | threads that opt in | 2.0 times on a loop built for it (measured) |
| IBPB | switches to restricted tasks | not measured; documented as a performance cost |
| `nosmt` | the whole machine | the number of hardware threads |

Three habits follow. **Read the file before you tune**: a performance change after a kernel or microcode update is often a mitigation. **Put the mitigation where the gadget is**: a barrier everywhere is the expensive way. **Isolate, do not patch around it**, when untrusted code runs inside your process: a browser that gives each site its own process does not depend on getting every gadget right.

---

## 5. Rowhammer

[Tutorial 16](16_dram_memory_controllers_and_numa.md) (section 9.5) explains the mechanism: opening and closing a DRAM row thousands of times in one refresh window disturbs the cells in the neighbouring rows, and some bits flip although nobody wrote to them. What matters for security:

- It is an **integrity** attack. The attacker changes memory instead of reading it, for example a page-table entry or a permission bit, and gains access to memory that was never theirs. Variants have run from JavaScript, from a neighbouring cloud VM and on phones.
- It does not use speculation, the cache as a signal, or any bug in the victim. **None of the fixes of section 4 touch it.** The attacker only needs to reach DRAM repeatedly (with `clflush` or uncached access; the loop in tutorial 16 uses `clflush`).
- The defences live in the memory system: more frequent refresh; target row refresh (TRR) and, in DDR5, refresh-management features; ECC, which corrects a single flip but not several in one word; and placing security-critical structures away from rows the attacker can reach.

I did not try it. On a shared host it would be an attack on someone else's memory.

---

## 6. Defences against memory-safety bugs

A buffer overflow, a use-after-free or an out-of-bounds write gives the attacker a write primitive. The next step is turning it into control of the program. Each defence removes one step. Measured items come from this VM and from a small test program built with the defaults of this Ubuntu GCC.

### 6.1 NX: data is not code

Each page-table entry has an execute-disable bit (NX or XD on x86, XN and PXN on Arm; [tutorial 15](15_virtual_memory_and_the_tlb.md), section 4.3). A page the program can write is, by policy, not one it can run. A one-byte function (`ret`, 0xC3) in an anonymous page:

```text
one page holding the single byte 0xC3 (ret):
  run it while the page is R|W       -> SIGSEGV (instruction fetch refused)
  run it after mprotect(R|X)         -> ran
  write to the R|X page              -> SIGSEGV (write refused)
  run it in a W and X mapping        -> ran
```

The first and third lines are the defence; the last shows that **W^X is policy, not a law**: this kernel lets a program ask for a writable and executable mapping, which is what a JIT does, unless a security module forbids it. Hardened systems forbid it and make the JIT flip between W and X (or map the same memory twice). The compiled test program has `GNU_STACK` flags `RW`, so its stack is not executable either. NX ended code injection and gave birth to **code reuse**: return-oriented programming chains short instruction sequences that already exist in the program and end in `ret`. The next defences make that harder.

### 6.2 ASLR and PIE: where is anything

The loader and the kernel place the stack, the heap, the shared libraries and (for a position-independent executable) the program itself at random addresses on each run, so an attacker does not know where code or data are. I ran a small program 300 times and recorded the address of a stack variable, a `malloc` block, an `mmap` block, `main` and `printf`:

| Region | Distinct addresses of 300 | Observed spread | Page positions |
|---|---:|---:|---:|
| stack | 300 | 15.9 GiB | 2^22.0 |
| heap | 300 | 1021 GiB | 2^28.0 |
| `mmap` base (so libc too) | 300 | 1012 GiB | 2^28.0 |
| executable (`main`) | 300 | 1021 GiB | 2^28.0 |

`/proc/sys/kernel/randomize_va_space` is `2` (stack, `mmap` and heap) and `/proc/sys/vm/mmap_rnd_bits` is `28`, which matches the table: 2^28 positions of 4 KiB. Under `setarch -R` (what `gdb` does by default) the same program printed identical addresses on two runs, so **stable addresses in a debugger say nothing about production**. ASLR is a probabilistic defence with known weaknesses: **one information leak** (a pointer printed, a format-string bug, a side channel) reveals the base and defeats it; a server that **forks workers without `exec`** keeps one layout across all of them, so a crash costs the attacker no new draw; and 28 bits is a few hundred million guesses, not a barrier when crashes are free. PIE (`-fPIE -pie`; the test binary's ELF type is `DYN`) is what lets the program's own code be randomised.

### 6.3 Canaries, RELRO, and what the compiler adds

- **Stack canary.** The compiler puts a random value between the locals and the return address and checks it before returning. In the test function with a 64-byte array and a `strcpy`, the generated code loads `%fs:0x28` at entry and subtracts it again before `ret`; a function with no array has neither, and `-fno-stack-protector` removed both. It catches a linear overflow that runs through the canary. It does not catch a write that skips it, an overflow in the heap, or a leaked canary.
- **RELRO and `BIND_NOW`.** The test binary has a `GNU_RELRO` segment and the `BIND_NOW` flag: all symbols are resolved at start and the table of function pointers (the GOT) becomes read-only, so overwriting it no longer redirects a call.
- **`endbr64`.** The compiler also placed 27 `endbr64` instructions in the compiled test binary (its code, the start-up code and the call stubs): these are the landing-pad markers of Intel CET (next section). On a CPU without CET they are no-ops, which is why the default is safe to leave on.

### 6.4 Control-flow integrity in hardware

| Feature | What it checks | How |
|---|---|---|
| **CET shadow stack** (Intel, AMD) | a `ret` goes where the matching `call` came from | `call` pushes the return address on both the normal stack and a **shadow stack**, held in pages the program cannot write with ordinary stores; `ret` compares the two and raises a control-protection fault if they differ. Linux enables it per thread with `arch_prctl(ARCH_SHSTK_ENABLE)` and `exec` turns it off again, per the kernel documentation |
| **CET indirect branch tracking** | an indirect `call` or `jmp` lands on a function entry | the target must begin with `endbr64` (standard) |
| **Arm pointer authentication (PAC)** | a code or data pointer was not forged | the instruction signs a pointer with a secret key and a context value (often the stack pointer) and puts the **PAC in the unused high bits**; another instruction checks and strips it. The kernel documentation: the PAC is 55 minus the virtual-address size bits, so **7 bits** at 48-bit addresses, with five keys (two for instructions, two for data, one generic); some instructions are in the hint space and are no-ops on cores without the feature. A failed check leaves an invalid pointer that faults on use (standard) |
| **Arm BTI** | an indirect branch lands on a landing pad | the Arm counterpart of `endbr64` (standard) |

This VM exposes neither `ibt` nor `shstk` in `/proc/cpuinfo`, and PAC is an Arm feature. A 7-bit PAC is guessed by a blind attacker with probability 1/128, so the scheme relies on a wrong guess being noticed (a crash or an alarm); an attack that can try without crashing weakens it.

### 6.5 Memory tagging and protection keys

**Arm MTE** gives every 16-byte granule of memory a 4-bit **allocation tag** and every pointer a 4-bit **logical tag** in address bits 59 to 56. A load or store whose pointer tag differs from the memory's tag is a mismatch. The kernel documentation: the check mode is chosen per thread, **synchronous** (`SIGSEGV` with `SEGV_MTESERR` at the faulting access, which is not performed) or **asynchronous** (`SIGSEGV` with `SEGV_MTEAERR` later, with no address). The allocator tags a fresh block, retags it on free, and gives neighbouring blocks different tags: an overflow into the next block or a use of a freed one then mismatches. With 4 bits, a random wrong tag matches 1 time in 16, so detection is probabilistic for use-after-free and certain for adjacent overflow if the allocator keeps neighbours different. **Protection keys** (Intel and AMD `pku`, [tutorial 15](15_virtual_memory_and_the_tlb.md) section 11.3) let a thread change a page group's permission without a system call; this VM has no `pku`.

**What none of these stops:** a logic bug, a data-only attack (setting `is_admin = 1` needs no code pointer), an information leak, or a side channel. Memory-safe languages remove the bug class; the hardware features make the remaining bugs harder to turn into control and easier to notice.

---

## 7. Trusted execution: SGX, TrustZone and confidential VMs

In the normal model the operating system and the hypervisor are trusted by every program above them: they can read any of its memory. A **trusted execution environment** (TEE) takes them out of the trusted base for one piece of code or one VM, using the CPU and the memory controller. It is useful when the machine belongs to someone else (a cloud provider, a phone's owner, an administrator you do not fully trust).

### 7.1 Intel SGX: an enclave inside a process

SGX is a set of instructions with which user-level and operating-system code can define protected private regions of memory, called **enclaves**; it appeared in 2015 with the Skylake generation ([Wikipedia](https://en.wikipedia.org/wiki/Software_Guard_Extensions)). The CPU encrypts the enclave's memory, and the data is decrypted only inside the CPU, so other code, including the operating system and a hypervisor, cannot read it. You split the application: a small trusted part runs in the enclave and a larger untrusted part calls into it through defined entry points (standard: *ecalls*, and *ocalls* for the way out). A remote party can ask the hardware for a signed report of what was loaded and check it before it sends a secret to the enclave (**remote attestation**, standard). The article states the limit plainly: the protection does not cover side channels.

The history of attacks is the lesson, as the article lists it:

| Year | Attack | What it used |
|---|---|---|
| 2017 | RSA keys extracted from an enclave on the same system within five minutes (Graz) | cache and DRAM side channels |
| 2018 | Foreshadow (L1 Terminal Fault) | speculative execution (section 4.6) |
| 2019 | a timing-based fault injection (Plundervolt in the article's references; CVE-2019-11157) | privileged control of the processor's voltage and frequency |
| 2020 | SGAxe | speculative execution; leaked enclave data and the **attestation keys** |
| 2020 | Load Value Injection | speculative execution |
| 2022 | ÆPIC Leak | an architectural flaw in the interrupt controller |

The same article records that Intel deprecated SGX on its 11th and 12th generation Core processors (client) and continues it on Xeon for cloud and enterprise use.

### 7.2 Arm TrustZone: two worlds on one chip

TrustZone divides the whole system on a chip into a **Secure world** and a **Normal world**, which the [Wikipedia article](https://en.wikipedia.org/wiki/TrustZone) describes as two virtual processors backed by hardware access control, with a world switch handled by a monitor (Monitor mode in the older cores, **EL3** in Armv8, [tutorial 26](26_interrupts_exceptions_and_dma.md)). A rich operating system runs in the Normal world and a small security-specialised system runs in the Secure world, which reduces the attack surface. Typical uses it lists are DRM and preventing unapproved use of a device; Samsung Knox uses it to detect kernel modification, store certificates and attest keys. The standard mechanism (the page does not describe it): each bus transaction carries a **non-secure (NS) bit**; a memory controller (TZASC) splits DRAM into secure and non-secure regions and a protection controller (TZPC) assigns each peripheral to one world; the `SMC` instruction enters the monitor; Secure EL1 runs a trusted OS and trusted applications run above it. The article's caution is the right one: the proprietary implementations have not been disclosed for public review, so the level of assurance is unclear, and it says they are not immune from attack.

The difference from SGX is the scope. SGX protects **one application's** region from everything else on a machine whose operating system you do not trust. TrustZone protects the **platform's** secrets (device keys, fingerprint templates, payment credentials) from the operating system the user runs, using a second system the device maker supplies.

### 7.3 Confidential VMs

[Tutorial 27](27_hardware_virtualization.md) (section 8.4) describes AMD SEV, SEV-ES and SEV-SNP, Intel TDX and Arm CCA: the **whole guest** is protected from the hypervisor by per-VM memory encryption, integrity protection on newer parts, and an attestation report. You move an existing VM into one without splitting the application, but the trusted base now includes the guest's entire kernel, device I/O has to cross shared bounce buffers, and the exit-heavy paths get slower (tutorial 27).

| | SGX | TrustZone | Confidential VM |
|---|---|---|---|
| Unit protected | an enclave in a process | the Secure world of the chip | a whole VM |
| Kept out | the OS, the hypervisor, other processes | the Normal-world OS and applications | the hypervisor and the host's administrators |
| Code you must trust | the CPU and the enclave (small) | the monitor, the secure OS and its applications (vendor code) | the CPU, its firmware and the whole guest OS |
| Typical use | keys, licence checks, secure computation on someone else's machine | device keys, secure storage, DRM, payment, biometrics | moving a cloud workload without rewriting it |
| Known limits | side channels; the attacks above; client parts deprecated | a large, proprietary secure side; shared caches | a big trusted base; bounce-buffer I/O; the host can still refuse to run it |

**What a TEE does not give you.** It does not make the code inside correct (a bug in an enclave is still a bug). It does not stop the host from refusing to schedule it (**availability**). It does not remove side channels: the host still sees page-fault patterns, cache activity and timing, which is how most of the attacks above worked. And the attestation that makes it useful is only as strong as the keys behind it: SGAxe extracted them. When a question says "does SGX or TrustZone make this safe", the right answer starts with "from whom".

---

## 8. Secure boot, measured boot and the TPM

### 8.1 A chain of trust

Everything above assumes the machine started the code you think it did. The answer is a **chain**: the first code that runs is in read-only boot ROM, and a public key (or its hash) is fused into the chip. Each stage checks the signature of the next stage before it runs it: boot ROM, a first-stage loader, platform firmware (UEFI on PCs, or Arm's trusted firmware, whose runtime part stays resident at EL3 as the monitor of section 7.2), the operating-system loader, then the kernel and its modules. If a check fails, boot stops. On PCs this is **UEFI Secure Boot**: the firmware refuses to load drivers and boot loaders that are not signed with an acceptable signature ([Wikipedia](https://en.wikipedia.org/wiki/UEFI)). The keys form a hierarchy: a platform key (PK) enrolled into the firmware, key-exchange keys (KEKs), and signature lists that allow (`db`) or revoke (`dbx`) particular signers and hashes (the list names are standard). In 2011 Microsoft announced that computers certified for Windows 8 had to ship with its public key enrolled and Secure Boot enabled, and that x86 systems had to allow a custom mode or switching it off.

What it proves is **who signed the code, not that the code is free of bugs.** The same article lists signed binaries that were exploitable, firmware implementations with flaws (2013), and *BlackLotus*, reported in 2023, which bypassed Secure Boot by exploiting limitations of earlier security updates. The hard part of secure boot is **revocation and rollback**: an old, signed, vulnerable boot loader is still correctly signed, so the platform must revoke it (the `dbx` list) or refuse versions below a counter (anti-rollback counters in fuses, standard).

### 8.2 Measured boot and the TPM

Secure boot **enforces**. Measured boot **records**: each stage computes a hash of the next stage and **extends** a register in a **TPM** (trusted platform module, a security chip specified as ISO/IEC 11889), and does not stop on a mismatch. The registers are the **platform configuration registers** (PCRs), and they can only be extended, never written directly: the new value is the hash of the old value joined with the measurement. A model of the chain with SHA-256 (`pcr.py`; a model and not a TPM; the extend rule is standard), and what it prints:

```python
# pcr.py: how measured boot chains the measurements (a model of a PCR, not a real TPM)
import hashlib

def H(b): return hashlib.sha256(b).digest()
def extend(pcr, component): return H(pcr + H(component))     # new = H(old || H(component))

def boot(stages):
    pcr = bytes(32)                                          # a PCR starts as zeros after reset
    for s in stages:
        pcr = extend(pcr, s)
    return pcr.hex()[:16]

good = [b"firmware v1", b"bootloader v1", b"kernel v1", b"cmdline ro quiet"]
print("good boot            ", boot(good))
print("same boot again      ", boot(good))
print("kernel patched       ", boot([good[0], good[1], b"kernel v1 (patched)", good[3]]))
print("cmdline changed      ", boot(good[:3] + [b"cmdline ro quiet init=/bin/sh"]))
print("two stages swapped   ", boot([good[1], good[0], good[2], good[3]]))
print("a stage skipped      ", boot([good[0], good[2], good[3]]))
```

```text
good boot             768c8b596536746a
same boot again       768c8b596536746a
kernel patched        5486a25bb6840476
cmdline changed       be20e8194a5fb115
two stages swapped    a462473ca2344126
a stage skipped       a577e454492d4cd7
```

The final value depends on **every component and the order**; the same boot gives the same value; and nothing that runs later can set it back, because the hash cannot be inverted. Two uses follow.

- **Remote attestation.** The TPM signs ("quotes") the PCR values with a key that stays inside it, and a verifier compares them with known-good values before it trusts the machine with a secret or a job.
- **Sealing.** A key, for example the disk-encryption key, is stored so the TPM releases it only if the PCRs match what they were when it was sealed. Change the kernel or the command line and the disk does not unlock. The reverse effect is the common surprise: a firmware update changes a PCR, and the disk asks for the recovery key.

### 8.3 Where it breaks

The TPM article records a 2021 attack in which researchers read a full-disk-encryption key from the bus between a discrete TPM and the rest of the board and used it to decrypt an SSD: the chip was tamper-resistant, the wires were not. So a discrete TPM's protection ends at its pins, and physical attacks are a separate row in section 1. It also records CVE-2018-6622, a design flaw in the static root of trust for measurement that let an attacker reset and forge PCRs with power interruptions and state restores, fixed by hardware-specific firmware patches. Measured boot says what ran, not that it was safe, and its evidence is only worth something to a verifier who knows what the good values are.

**This VM** has no `/sys/firmware/efi` and no `/dev/tpm0`: a micro-VM starts a kernel directly, with no firmware chain inside the guest. Whatever boot attestation exists belongs to the provider below it. [Tutorial 31](31_arm_vs_x86_and_putting_it_together.md) section 11 follows the whole path from power-on to `main`, with this VM's `dmesg` timeline.

---

## 9. What you see, and what it usually is

| What you see | Likely cause | First check |
|---|---|---|
| A kernel or microcode update made a syscall-heavy or call-heavy service slower | a mitigation changed: KPTI, retpoline, IBRS, IBPB, SMT off | `cat /sys/devices/system/cpu/vulnerabilities/*`; `cat /proc/cmdline`; compare with `mitigations=off` on a test machine only |
| `spectre_v2` shows `BHI: Vulnerable` inside a VM | the guest sees no usable control | ask what the host exposes; do not conclude from the guest alone |
| `SIGSEGV` when jumping into data or a stack buffer | NX working | `readelf -l` for `GNU_STACK`; this is the defence, not a bug |
| Addresses differ on every run; stable under `gdb` | ASLR; the debugger turns it off | `cat /proc/sys/kernel/randomize_va_space`; `setarch -R` |
| A checksum or token check takes longer when more leading bytes match | an early-exit comparison | replace it with the constant-time routine of your library; measure with two input classes |
| `SIGSEGV` with `si_code` `SEGV_MTESERR` | a memory-tag mismatch: use-after-free or overflow, caught by MTE | the fault address is exact in synchronous mode; fix the bug |
| Performance counters show many cache misses in a crypto routine | table lookups indexed by a secret | use a constant-time implementation or hardware instructions |
| The disk asks for the recovery key after a firmware update | a PCR changed, so the sealed key was not released | re-seal after the update; check which PCRs the policy uses |
| A thread is slower after `PR_SPEC_DISABLE` or a sandbox flag | SSBD | `grep Speculation /proc/<pid>/status` |

---

## 10. Mistakes that cost points

1. **Calling Spectre and Meltdown "software bugs".** They are consequences of the design of fast cores. Some fixes are software (retpoline, masking, KPTI), some are microcode and new modes (IBRS, eIBRS), and the real fix is new silicon (a check before forwarding).
2. **Saying KPTI protects against Spectre.** It protects against Meltdown, by not mapping the kernel in user mode. Spectre needs the predictor fixes.
3. **Comparing secrets with `==`, `memcmp` or `strcmp`.** Tags, tokens, passwords and hashes need a constant-time comparison. Also error paths: an early "unknown user" is the same bug.
4. **Writing constant-time code in C and not reading the assembly.** The compiler may add the branch back. Use the library routine and check the output of the one you wrote.
5. **Believing constant time means fast.** It means the worst case every time: 22.3 ns against 1.8 ns for the wrong guess in section 3.2.
6. **Putting `lfence` everywhere.** 19 times on the protected access. A mask costs 2 times, and a barrier belongs only at a gadget.
7. **Thinking NX ends exploitation.** It ends code injection. Return-oriented programming reuses code that is already executable; CET shadow stacks, ASLR and pointer authentication are the next layers.
8. **Treating ASLR as strong.** Twenty-eight bits of `mmap` entropy fall to a leak, or to brute force against a worker that is forked without `exec`.
9. **"We run it in an enclave (or a confidential VM), so it is safe."** From whom? Side channels, availability, bugs inside and a large guest kernel are all still there.
10. **"Secure Boot is on, so the system is trustworthy."** It means the boot chain was signed, not that the signed code is free of bugs or that old vulnerable versions are revoked.
11. **Reading a guest's `sysfs` as the truth about the hardware.** It shows what the hypervisor exposes.
12. **Removing high-resolution timers and calling it fixed.** A counter thread is a timer. It makes the attack harder; it does not remove the channel.
13. **Timing with `rdtsc` and no fences.** The core can move the load across the timer reads and the number means nothing. Use `lfence; rdtscp; lfence` (section 2.2) and measure the timer's own cost.
14. **Believing SMT is safe because the threads are "just siblings".** They share the L1 cache, ports and predictors ([tutorial 18](18_multicore_and_multithreading.md)); the `cross_thread` vector exists for this reason.

---

## 11. Questions you will be asked

**Q1. Explain Spectre v1.** A bounds check `if (x < n)` is predicted taken because it has been taken many times. The attacker passes an out-of-range `x`. The core runs ahead, reads `a[x]` (a secret byte) and uses it as the index of a second load `b[a[x] * 4096]`, which brings one line into the cache. The check resolves false, the results are discarded, but the line stays. The attacker times the 256 candidate lines and reads the secret (Flush+Reload receiver). Three ingredients to look for in code: an attacker-controlled index, a check on it, and a load whose address depends on the loaded value. Fixes: mask the index with a data dependency (2 times at the access in my measurement), an `lfence` (19 times), or isolate untrusted code in another process.

**Q2. How is Meltdown different, and what does KPTI do?** Meltdown reads kernel memory from user mode, because on affected cores the data is forwarded to following instructions before the permission fault is raised; it is a flaw in one core design, not in a predictor. KPTI keeps almost all the kernel unmapped while in user mode, with a `CR3` switch on each kernel entry and exit (about a hundred cycles each). PCID avoids the TLB flush. Newer CPUs do not need it, as this VM shows (`meltdown: Not affected`).

**Q3. What is retpoline and why does this machine not use it?** A thunk that replaces an indirect jump or call with a call/return sequence, so the return stack buffer sends speculation into a harmless loop while the real target is loaded (section 4.4). It makes every indirect call cost about a misprediction: 1.6 against 12.5 ns for a predicted call here. A CPU with eIBRS gets the same protection from a mode set once at boot, so the kernel uses it (`Enhanced / Automatic IBRS`).

**Q4. How does Flush+Reload work, and how does Prime+Probe differ?** Flush+Reload: flush a line that attacker and victim share, wait, reload and time it. Fast means the victim touched it. Needs shared memory and a flush instruction; works across cores through the last-level cache, and across VMs (the published attack recovered 96.7 % of a GnuPG key's bits from one signature). Prime+Probe: fill a cache set with your own lines, wait, time them again; slower means the victim used the set. No shared memory or flush, but you need an eviction set and the signal is a few nanoseconds, not 80 to 95 (my in-VM attempt was too noisy to use).

**Q5. Why must a MAC comparison be constant-time, and how do you write one?** An early exit leaks the length of the matching prefix through time; with that an attacker needs about `256 × n` guesses instead of `256^n`. OR together the XOR of every byte pair and test the result once; use the library routine; check the assembly. Measured: early exit rises 0.34 ns per matching byte on average, the constant-time version is flat at 22.3 ns.

**Q6. How do you reduce side-channel risk for multiple tenants on one machine?** Do not share what the channel needs: separate cores (no SMT siblings across tenants, or `nosmt`/core scheduling), partitioned or isolated cache (Cache Allocation Technology), no page deduplication between tenants, kernel mitigations that match the threat (`mitigations=auto` with the guest vectors on), constant-time cryptography, microcode and kernel updates, and noise or coarse timers for untrusted code as a minor extra.

**Q7. What do NX, ASLR, canaries, shadow stacks, PAC and MTE each stop?** NX: executing injected data. ASLR: knowing where code and data are. Canary: a linear overflow reaching the return address. Shadow stack (CET) and PAC: a forged return address or code pointer. IBT and BTI: a jump to the middle of a function. MTE: use-after-free and heap overflows, probabilistically. ROP is stopped by the shadow stack (or PAC on returns); ASLR only makes it harder. None stops a logic bug or a data-only attack.

**Q8. What does SGX protect, and from whom?** An enclave's code and data from the operating system, the hypervisor and other processes, by encrypting the memory and decrypting it only inside the CPU, with attestation to prove what is running. It does not protect against side channels (cache, page-fault patterns, speculation), denial of service by the host, or bugs inside the enclave. Foreshadow, SGAxe and others show the side-channel point; Intel deprecated it on the 11th and 12th generation client Core parts and keeps it on Xeon.

**Q9. Compare TrustZone, SGX and a confidential VM.** TrustZone is a platform split into a Secure and a Normal world by the hardware (NS bit, memory and peripheral controllers, monitor at EL3); its protected unit is the device's secrets and its trusted base is the vendor's secure OS. SGX is per-application enclaves on a general machine with a small trusted base. A confidential VM is a whole guest under a hypervisor it does not trust, with encrypted memory and attestation, easy to adopt and with a large trusted base.

**Q10. Secure boot against measured boot, and how a TPM unseals a disk key.** Secure boot refuses to run code that is not signed by a trusted key. Measured boot hashes each stage into PCRs (`new = H(old || H(stage))`), so the final value records what ran and in what order. The disk key is sealed to the PCR values of a good boot; the TPM releases it only if they match. A changed kernel, command line or boot loader changes a PCR and the key is not released.

**Q11. What is Rowhammer, and why do Spectre fixes not help?** Repeated activation of a DRAM row flips bits in neighbouring rows, so unprivileged code can modify memory it cannot access, for example page-table entries. It is a write (integrity) attack that does not use speculation or cache timing. Defences: refresh rate, TRR and DDR5 refresh management, ECC (partial), placement of sensitive structures.

**Q12. A service got 10 % slower after a kernel upgrade. What do you check?** `/sys/devices/system/cpu/vulnerabilities/*` and `/proc/cmdline` for what is active, comparing with the old kernel; whether KPTI is now on (the CPU or its microcode flags changed, `pcid` missing) and how many kernel entries the service makes; retpoline against eIBRS for call-heavy code; SMT state; IBPB and SSBD for processes that opted in. Reproduce with the mitigation switched off on a test machine only, and decide with the threat model in hand, not with the benchmark alone.

**Q13. How does ASLR work, and why is it weak on its own?** The loader and kernel place stack, heap, libraries and a PIE executable at random page-aligned addresses (28 bits for `mmap` on x86-64 Linux here, 22 for the stack). One leaked pointer reveals the base; forked workers share a layout; crashes can be free. It is a speed bump that raises the cost of reuse attacks and works best together with shadow stacks and pointer authentication.

---

## 12. Problems

**How to use this section.** Four problems. Each has paper parts, a short program or script in the Answers that checks them, and a *Where* and *How* line saying where to check and how to start. Do the paper part first, then run the check, and fix the paper answer if they disagree. Time: about 20 to 30 minutes each. Nothing here attacks anything: the programs time your own memory or read your own settings.

### Problem A (paper, then your machine): how a cache timing channel works

In section 2.2 the timer read 48 ticks for nothing, 60 for a hit (p99 72), 226 for a flushed line (p1 218), at 2.1 GHz.

(a) Pick a threshold between hit and miss. How far is it from the slowest typical hit and from the fastest typical miss, in ticks and in nanoseconds? What is the gap between the medians?
(b) The receiver of section 2.3 uses one probe line per 4096-byte page, visits the 256 lines in a scrambled order, writes to the whole probe array first, and puts `lfence` around `rdtscp`. For each, say what goes wrong without it.
(c) Estimate the bytes per second of the channel from the miss time alone, and compare with the measured 72.6 µs per byte. Where does the rest go?
(d) *On your machine:* compile and run `gap.c` from the Answers. Then change one thing at a time (stride 64, stride 512, sequential order, no `memset`) and write down how many bytes are still recovered. On an Arm machine, say what you would have to change to get a flush.
(e) Prime+Probe needs no shared memory. Why is its signal so much smaller, and what does that do to the attacker's job?

*Where:* (a) to (c) on paper, checked by `arith.py`; (d) on any x86 Linux machine.
*How:* (a) the margin is `threshold - p99(hit)` on one side and `p1(miss) - threshold` on the other. (b) Think of the prefetchers, the page tables and the out-of-order core. (c) 256 lines at the miss time. (e) Compare "a flushed line comes from DRAM" with "one line of twelve moves from L1 to L2".

### Problem B (paper, then the compiler): spot the Spectre gadget

For each function say whether it contains a Spectre v1 gadget, what the speculative core would do, and what you would change. `mask_nospec(i, n)` is the `cmp; sbb` mask of section 4.3 (all ones if `i < n`, else 0).

```c
extern uint8_t table1[256], table2[256 * 512];
extern unsigned table1_len, len;
extern int buf[];
typedef void (*handler_t)(void);
extern handler_t handlers[16];

uint8_t get1(unsigned i) {                         // B1
    if (i < table1_len) return table2[table1[i] * 512];
    return 0;
}
int get2(unsigned i) {                             // B2
    if (i < len) return buf[i];
    return -1;
}
uint8_t get3(unsigned i) {                         // B3
    if (i < table1_len) { i &= mask_nospec(i, table1_len); return table2[table1[i] * 512]; }
    return 0;
}
void dispatch(unsigned op) {                       // B4
    if (op < 16) handlers[op]();
}
```

(e) Compile with `gcc -O2 -fcf-protection=none -S` and find, in the output for B1, the conditional jump and the two dependent loads. Do the same for B3 and find the mask.
(f) A kernel path makes 40 well-predicted indirect calls. With the measured 1.62 ns for an ordinary call and 12.51 ns for a retpoline call, how much does retpoline add per execution of the path? Compare it with a 95 to 120 ns system call and with a 5 µs request.

*Where:* (a) to (d) on paper; (e) with the compiler on the snippet; (f) on paper, checked by `arith.py`.
*How:* look for three things: a check on an attacker-controlled index, a load that uses the index, and a second load whose address comes from the first load's value.

### Problem C (paper, a script, then your machine): constant-time code

(a) Rewrite `bool check(const uint8_t *a, const uint8_t *b, size_t n)` (early exit at the first difference) so that its time does not depend on where the first difference is. Name two ways a compiler or a library could undo it.
(b) With a timing leak of 0.34 ns per matching byte, how many guesses does recovering a 16-byte tag take, against none? If one timing has standard deviation `σ` = 100 ns, then 10 µs, how many samples does it take to see a 0.34 ns step (use `(2σ/δ)²`)?
(c) A library `memcmp` compares 16 bytes at a time and exits after the first block that differs. Is that safe to use for a tag? What does the attacker learn, and what could still leak?
(d) *On your machine:* compile `cmp.c` from the Answers, run it, and plot or tabulate the early-exit time against `k`. Look at the assembly of `cmp_ct` at `-O2` and `-O3`. Change `LEN` to 16 and to 256 and say how the early-exit curve changes.

*Where:* (a) to (c) on paper, checked by `arith.py`; (d) on any x86 Linux machine.
*How:* (a) accumulate differences with OR and test once at the end. (b) 256 guesses per byte, one byte at a time. (c) think of what the attacker can see at block granularity, and of the bytes that do not fill a block.

### Problem D (paper): what do SGX, TrustZone and the rest protect

(a) For each threat, name which of these address it and one that does not, with a reason: SGX, TrustZone, a confidential VM, NX, a shadow stack (or pointer authentication), KPTI, secure boot, measured boot with a TPM, a constant-time implementation.
  1. A root-level operating system reads an application's memory on a machine you rent.
  2. A hypervisor administrator reads a guest's memory.
  3. A co-tenant on another core times cache lines to infer a key.
  4. A user process writes machine code into a stack buffer and jumps to it.
  5. A process overwrites a return address to chain code that is already in the program.
  6. Unprivileged code reads kernel memory by speculation (Meltdown).
  7. Someone with disk access replaces the boot loader.
  8. Someone with a probe on the motherboard reads a disk key as the TPM releases it.
  9. The host refuses to schedule your workload.
(b) A payment app wants a signing key that a rooted phone cannot steal. Where do you keep it, what do you trust, and what can the rooted operating system still do?
(c) A customer wants to run a database on a cloud so that the provider's administrators cannot read it. Choose between an SGX enclave, a confidential VM, and a plain VM with disk encryption. What remains open?
(d) *On your machine:* run `cat /sys/devices/system/cpu/vulnerabilities/*` (with file names, for example `grep . /sys/devices/system/cpu/vulnerabilities/*`), `cat /proc/cmdline`, `grep -o -w -E 'smep|smap|nx|pku|ibt|shstk|sgx|tdx_guest|sev' /proc/cpuinfo | sort -u`, `ls /sys/firmware/efi /dev/tpm*` and `cat /proc/sys/kernel/randomize_va_space`. Say what each answer means about your machine.

*Where:* (a) to (c) on paper, with sections 6 to 8; (d) on any Linux machine.
*How:* (a) ask "which boundary, and who is the attacker". (b) separate "cannot extract the key" from "cannot use the key". (c) compare what must be trusted in each choice.

---

## Answers

### Problem A

**(a)** The gap between the medians is 226 - 60 = **166 ticks, 79 ns**. A threshold at the midpoint, 143 ticks, is **71 ticks (34 ns)** above the p99 of the hits and **75 ticks (36 ns)** below the p1 of the misses, so it sits in a gap that neither group reaches in a typical sample. With it, 25 of 100,000 hits were read as misses and no miss as a hit (section 2.2). (`arith.py` prints these.)

**(b)** *One line per page, 4096 apart:* with 64-byte spacing the adjacent-line prefetcher brings neighbours in, so many lines look cached (6.5 % of bytes recovered); at 512 bytes 18.4 %. *Scrambled order:* a sequential sweep shows a constant stride, a stride prefetcher runs ahead and the lines look cached (1.4 %). *Writing the array first:* an anonymous page that has only been read is mapped to the shared zero page, so all 256 virtual lines are one physical line (2.3 %). *`lfence` around `rdtscp`:* the core may start the load before the first timer read or finish the second read early, so the interval would not be the load's.

**(c)** 256 misses at 226 ticks (108 ns) are 27.6 µs of the 72.6 µs measured per byte, so the miss alone allows about 36,000 bytes/s. The rest, about **176 ns per line**, is the `clflush`, the fences and the loop. The measured channel carries about 13,800 bytes per second, 110 kbit/s. It is slow, and it is enough for a key of a few hundred bits.

**(d)** The program (74 lines) is below. Expect about 99 % at the default; the variants should fall as in section 2.3. On Arm there is no `clflush`; the equivalent is `DC CIVAC` (clean and invalidate to the point of coherency), which Linux can allow at EL0 (the control bit is `SCTLR_EL1.UCI`; standard), and the timer is `CNTVCT_EL0`, which ticks at the system-counter frequency, often far slower than a core clock (standard), so the receiver needs a coarser decision, many repetitions or a counter thread.

```c
// gap.c: a cache hit against a flushed line, and a byte sent through the cache and read back.
// gcc -O2 -o gap gap.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <x86intrin.h>

#define N 100000
#define STRIDE 4096                         // one probe line per page: no neighbour or stride prefetching
static uint8_t *probe;                      // 256 lines, one for each value of a byte
static volatile uint8_t sink;

static inline uint64_t timed_load(volatile uint8_t *p) {   // time one load, in TSC ticks
    unsigned aux;
    _mm_lfence(); uint64_t t0 = __rdtscp(&aux); _mm_lfence();
    sink = *p;
    _mm_lfence(); uint64_t t1 = __rdtscp(&aux); _mm_lfence();
    return t1 - t0;
}
static int cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}
static void flush_probe(void) {
    for (int i = 0; i < 256; i++) _mm_clflush(&probe[i * STRIDE]);
    _mm_mfence();
}
static int fastest_line(uint64_t thr) {     // the reload: time all 256 lines, return the one that is cached
    int best = -1; uint64_t bt = ~0ull;
    for (int k = 0; k < 256; k++) {
        int i = (k * 167 + 13) & 255;       // scrambled order, so no prefetcher sees a pattern
        uint64_t dt = timed_load(&probe[i * STRIDE]);
        if (dt < thr && dt < bt) { bt = dt; best = i; }
    }
    return best;
}

int main(void) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(0, &s); sched_setaffinity(0, sizeof s, &s);
    probe = aligned_alloc(4096, 256 * STRIDE);
    memset(probe, 1, 256 * STRIDE);         // WRITE to every page: an untouched page reads as one shared zero page
    static uint64_t empty[N], hit[N], miss[N];
    volatile uint8_t *p = &probe[0];
    unsigned aux;
    for (int i = 0; i < N; i++) { _mm_lfence(); uint64_t a = __rdtscp(&aux); _mm_lfence();
                                  uint64_t b = __rdtscp(&aux); _mm_lfence(); empty[i] = b - a; }
    for (int i = 0; i < N; i++) { sink = *p; hit[i] = timed_load(p); }
    for (int i = 0; i < N; i++) { _mm_clflush((void *)p); _mm_mfence(); miss[i] = timed_load(p); }
    qsort(empty, N, 8, cmp); qsort(hit, N, 8, cmp); qsort(miss, N, 8, cmp);
    printf("ticks      p1   p50   p99\n");
    printf("empty   %5lu %5lu %5lu   (two timer reads, nothing between them)\n", empty[N/100], empty[N/2], empty[N*99/100]);
    printf("hit     %5lu %5lu %5lu\n", hit[N/100], hit[N/2], hit[N*99/100]);
    printf("miss    %5lu %5lu %5lu   (after clflush)\n", miss[N/100], miss[N/2], miss[N*99/100]);
    uint64_t thr = (hit[N/2] + miss[N/2]) / 2;
    int bad_hit = 0, bad_miss = 0;
    for (int i = 0; i < N; i++) { bad_hit += hit[i] >= thr; bad_miss += miss[i] < thr; }
    printf("threshold %lu ticks: %d of %d hits look like misses, %d of %d misses look like hits\n", thr, bad_hit, N, bad_miss, N);

    srand(7); int ok = 0, trials = 20000;
    struct timespec ta, tb; clock_gettime(CLOCK_MONOTONIC, &ta);
    for (int t = 0; t < trials; t++) {
        int secret = rand() & 255;
        flush_probe();
        sink = probe[secret * STRIDE];      // the "victim": its only trace is which line is now cached
        ok += fastest_line(thr) == secret;
    }
    clock_gettime(CLOCK_MONOTONIC, &tb);
    double us = ((tb.tv_sec - ta.tv_sec) * 1e9 + (tb.tv_nsec - ta.tv_nsec)) / 1e3 / trials;
    printf("byte sent through the cache: %d of %d recovered (%.2f %%), %.1f us per byte\n", ok, trials, 100.0 * ok / trials, us);
    return 0;
}
```

```text
ticks      p1   p50   p99
empty      46    48    58   (two timer reads, nothing between them)
hit        58    60    72
miss      218   226   728   (after clflush)
threshold 143 ticks: 25 of 100000 hits look like misses, 0 of 100000 misses look like hits
byte sent through the cache: 19869 of 20000 recovered (99.34 %), 72.6 us per byte
```

**(e)** In Flush+Reload the two cases are "from DRAM" and "from L1": 80 to 95 ns apart. In Prime+Probe the victim evicts one of twelve lines in a set, which turns one L1 hit into an L2 hit, a few nanoseconds, inside the jitter of the shared host (section 2.4). The attacker has to repeat many rounds, filter, run on a quiet core, and build eviction sets, and still gets fewer bits per second.

### Problem B

**B1: a gadget.** A conditional check on `i`, a load `table1[i]` that uses it, and a second load `table2[table1[i] * 512]` whose address comes from the first value. The compiled code in (e) shows exactly that. Fix: clamp `i` with `mask_nospec` before the loads (B3), or put an `lfence` after the check, or avoid the dependent second access.

**B2: not a gadget by itself.** It performs one out-of-bounds speculative read, but nothing in the function turns the value into a cache footprint. Read the callers: if one does `table[get2(i)]`, or the value is later used as an index or a pointer, the combination is a gadget, and inlining can bring them into one function. The honest answer is "not here, check where the value goes".

**B3: mitigated.** `mask_nospec` computes all ones if `i < table1_len`, else zero, from the comparison as **data**. Even if the branch is predicted wrongly, the index used for the loads is `i & 0` = 0, so no out-of-bounds address is formed.

**B4: a gadget of the v1-into-v2 kind.** The bounds check on `op` can be mispredicted. Then `handlers[op]` reads a pointer from beyond the array, from memory the attacker may influence, and the core speculatively jumps to it. Fix: mask `op` as in B3, and protect the indirect call itself (retpoline, or eIBRS on a CPU that has it).

**(e)** Compiling with `gcc -O2 -fcf-protection=none -S` gives for B1:

```text
get1:
        xorl    %eax, %eax
        cmpl    table1_len(%rip), %edi
        jb      .L5                          ; the check
        ret
.L5:
        movl    %edi, %edi
        leaq    table1(%rip), %rax
        leaq    table2(%rip), %rdx
        movzbl  (%rax,%rdi), %eax            ; load 1: table1[i]
        sall    $9, %eax                     ; times 512
        cltq
        movzbl  (%rdx,%rax), %eax            ; load 2: table2[table1[i] * 512]
        ret
```

and for B3, between the check and the loads, `cmp %rax, %rdx` / `sbb %rax, %rax` / `andl %eax, %edi`. For B4 the compiler produced `cmpl $15, %edi; ja .L12; ...; jmp *(%rax,%rdi,8)`: a check, then an indirect jump through a table.

**(f)** 40 × (12.51 - 1.62) = **436 ns** extra per execution of the path: 3.6 to 4.6 times a 95 to 120 ns system call, and 8.7 % of a 5 µs request. Retpoline is cheap per call and large for a path that is nothing but calls, which is why a kernel with eIBRS prefers the hardware mode.

### Problem C

**(a)**

```c
bool check(const uint8_t *a, const uint8_t *b, size_t n) {
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= a[i] ^ b[i];   // touch every byte, no branch on the data
    return d == 0;                                      // one decision, at the end
}
```

The loop bound `n` is public (the tag length), the loads are at public addresses, and the only data-dependent step is the final test. Ways it can be undone: the compiler, which the language does not forbid from adding an early exit once it sees that `d` can only stay nonzero; and a library routine that is "faster" for the common case (a `memcmp` that exits early, a vectorised version with a data-dependent tail). Use the vetted routine of your crypto library and look at the assembly.

**(b)** `256 × 16` = **4,096** guesses with a leak, against about **3.4 × 10^38** (256^16, or 2^128) without. For a 0.34 ns step, `(2σ/δ)²` is about **350,000** samples at `σ` = 100 ns and about **3.5 × 10^9** at `σ` = 10 µs. Noise protects, at a price in time for the attacker, and more so when the leak per step is small; a bigger leak per step (a conditional subtraction in a big-number routine, as in the remote attack) needs far fewer. `arith.py`:

```python
# arith.py: the arithmetic of Problems A, B and C
tick = 1 / 2.1                                   # ns per timer tick at 2.1 GHz
hit_p99, miss_p1, hit_p50, miss_p50, thr = 72, 218, 60, 226, 143     # ticks, from the first run in section 2.2
print("A(a) gap between the medians       : %d ticks = %.0f ns" % (miss_p50 - hit_p50, (miss_p50 - hit_p50) * tick))
print("A(a) threshold to the slowest hits : %d ticks = %.0f ns" % (thr - hit_p99, (thr - hit_p99) * tick))
print("A(a) fastest misses to threshold   : %d ticks = %.0f ns" % (miss_p1 - thr, (miss_p1 - thr) * tick))
per_byte = 72.6                                  # us per byte, measured
miss_part = 256 * miss_p50 * tick / 1000
print("A(c) 256 misses account for %.1f of the %.1f us per byte; the rest is %.0f ns per line" % (miss_part, per_byte, (per_byte - miss_part) * 1000 / 256))
print("A(c) %.0f bytes/s = %.0f kbit/s" % (1e6 / per_byte, 8e6 / per_byte / 1000))
plain, retp, c = 1.62, 12.51, 40                 # ns per call, ns per retpoline call, calls on the path
extra = c * (retp - plain)
print("B(f) extra per path execution: %.0f ns = %.1f to %.1f times a 95 to 120 ns system call, %.1f %% of a 5 us request" % (extra, extra / 120, extra / 95, extra / 5000 * 100))
n, delta = 16, 0.34
print("C(b) guesses for %d bytes: %d with a timing leak, %.2e without" % (n, 256 * n, 256 ** n))
for sigma in (100, 10_000):
    print("C(b) sigma = %5d ns: %.3g samples to resolve %.2f ns" % (sigma, (2 * sigma / delta) ** 2, delta))
print("D   a 7-bit PAC guessed blind: 1 in %d; a 4-bit memory tag: 1 in %d" % (2 ** 7, 2 ** 4))
```

```text
A(a) gap between the medians       : 166 ticks = 79 ns
A(a) threshold to the slowest hits : 71 ticks = 34 ns
A(a) fastest misses to threshold   : 75 ticks = 36 ns
A(c) 256 misses account for 27.6 of the 72.6 us per byte; the rest is 176 ns per line
A(c) 13774 bytes/s = 110 kbit/s
B(f) extra per path execution: 436 ns = 3.6 to 4.6 times a 95 to 120 ns system call, 8.7 % of a 5 us request
C(b) guesses for 16 bytes: 4096 with a timing leak, 3.40e+38 without
C(b) sigma =   100 ns: 3.46e+05 samples to resolve 0.34 ns
C(b) sigma = 10000 ns: 3.46e+09 samples to resolve 0.34 ns
D   a 7-bit PAC guessed blind: 1 in 128; a 4-bit memory tag: 1 in 16
```

**(c)** No guarantee. At block granularity the early exit reveals only whether a whole 16-byte block matched, so the attacker cannot recover a block one byte at a time (that is 256^16 guesses for the block); that is far better than a byte loop. But the `memcmp` contract promises nothing about time, many implementations treat an unaligned head or a short tail with smaller steps (which restores byte-level leaks there), a library update can change it, and a tag shorter than a block may take a different path. Use a routine that promises constant time.

**(d)** The program (64 lines) is below. Expected: the early-exit time rises linearly with `k` (about 0.34 ns per byte here) and the constant-time version is flat. With `LEN` = 16 the curve runs from 2.0 ns at `k` = 0 to 13.3 ns at `k` = 16 and stays there (every `k` of 16 or more is a full match). With `LEN` = 256 it keeps rising (41.7 ns at `k` = 64, with a much wider spread, p10 to p90 23 to 61 ns), and the shape differs from the 64-byte build (`k` = 63 took 38 ns against 23 ns): the exact curve depends on predictor state, and the rise is what holds. In the assembly of `cmp_ct` at `-O2` (no asm barrier) GCC 13.3 emits a plain loop whose only branch is the loop condition, and at `-O3` a vectorised loop (`pxor`, `por`) plus branches that depend on `n`, which is public.

```c
// cmp.c: does the time of a comparison depend on the data?
// gcc -O2 -fno-tree-vectorize -o cmp cmp.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sched.h>
#include <x86intrin.h>

#define LEN 64          // bytes compared
#define BATCH 2000      // compares inside one timed sample
#define SAMPLES 4001

__attribute__((noinline)) int cmp_naive(const uint8_t *a, const uint8_t *b, int n) {
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;             // leaves at the first difference
    return 1;
}
__attribute__((noinline)) int cmp_ct(const uint8_t *a, const uint8_t *b, int n) {
    uint8_t d = 0;
    for (int i = 0; i < n; i++) d |= a[i] ^ b[i];   // always reads every byte
    __asm__ volatile("" : "+r"(d));                 // stop the compiler turning it back into an early exit
    return d == 0;
}
typedef int (*fn)(const uint8_t *, const uint8_t *, int);
static volatile int sink;
static int cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}

static void run(const char *name, fn f, double ghz) {
    uint8_t secret[LEN], guess[8][LEN];
    int ks[8] = {0, 1, 8, 16, 32, 48, 63, 64};     // how many leading bytes of the guess are right
    for (int i = 0; i < LEN; i++) secret[i] = (uint8_t)(i * 37 + 11);
    for (int j = 0; j < 8; j++) { memcpy(guess[j], secret, LEN); if (ks[j] < LEN) guess[j][ks[j]] ^= 0x5A; }
    static uint64_t v[8][SAMPLES];
    for (int s = 0; s < SAMPLES; s++)              // the eight cases are interleaved, so drift hits all of them equally
        for (int j = 0; j < 8; j++) {
            unsigned aux; int acc = 0;
            _mm_lfence(); uint64_t t0 = __rdtscp(&aux); _mm_lfence();
            for (int r = 0; r < BATCH; r++) acc += f(secret, guess[j], LEN);
            _mm_lfence(); uint64_t t1 = __rdtscp(&aux); _mm_lfence();
            sink = acc; v[j][s] = t1 - t0;
        }
    printf("%s\n  leading bytes right : median ns per compare   (p10 .. p90)\n", name);
    for (int j = 0; j < 8; j++) {
        qsort(v[j], SAMPLES, 8, cmp);
        printf("  k = %2d : %6.2f   (%.2f .. %.2f)\n", ks[j], v[j][SAMPLES / 2] / ghz / BATCH,
               v[j][SAMPLES / 10] / ghz / BATCH, v[j][SAMPLES * 9 / 10] / ghz / BATCH);
    }
}

int main(void) {
    cpu_set_t s; CPU_ZERO(&s); CPU_SET(0, &s); sched_setaffinity(0, sizeof s, &s);
    struct timespec a, b; unsigned aux;                 // TSC ticks per ns, from a 200 ms wait on the clock
    clock_gettime(CLOCK_MONOTONIC, &a); uint64_t t0 = __rdtscp(&aux);
    do clock_gettime(CLOCK_MONOTONIC, &b); while ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec) < 200e6);
    double ghz = (__rdtscp(&aux) - t0) / ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec));
    run("early-exit compare", cmp_naive, ghz);
    run("constant-time compare", cmp_ct, ghz);
    return 0;
}
```

### Problem D

**(a)**

| # | Threat | Addressed by | Not addressed by, and why |
|---|---|---|---|
| 1 | root OS reads an application | **SGX** (encrypted enclave); a **TrustZone** secret held in the Secure world | a confidential VM (the guest OS is inside the trusted base); NX, KPTI (different threats) |
| 2 | hypervisor admin reads a guest | **confidential VM** (SEV-SNP, TDX, CCA) | TrustZone (a different split); NX, KPTI |
| 3 | co-tenant times cache lines | **constant-time code**, cache partitioning, no SMT sharing | SGX and confidential VMs (side channels are outside their model); NX |
| 4 | shellcode on the stack | **NX** | ASLR alone (hides the address, does not block execution) |
| 5 | forged return address, code reuse | **shadow stack** (CET) or **pointer authentication** | NX (the code is already executable); a canary (only a linear overflow) |
| 6 | Meltdown | **KPTI** on affected CPUs, new silicon | NX, canaries; secure boot |
| 7 | replaced boot loader | **secure boot** (refuses it); **measured boot with a TPM** (records it, and the sealed disk key is not released) | SGX, NX |
| 8 | probe on the TPM bus | a TPM whose bus is not exposed (firmware or integrated), or a PIN that the key release also needs (standard) | secure boot, measured boot: the key is in clear on the wire once released |
| 9 | host refuses to run the workload | nothing in the list: availability is outside every TEE's model | all of them |

**(b)** Keep the key in the platform's hardware-backed key store, which uses TrustZone (or a secure element): the key is generated inside and never leaves in clear. You trust the secure world's code, the hardware and the vendor. A rooted Normal-world OS **cannot extract the key** but **can still ask the secure world to use it**. So the use must be bound to something the root cannot forge: a user-authentication event checked inside the secure world (a fingerprint or PIN read over a path the OS cannot see), a confirmation of what is being signed, limits on amount and rate, and server-side checks. It can also deny service and watch timing.

**(c)** A **confidential VM** is the practical choice: the database runs unmodified, memory is encrypted and integrity-protected against the hypervisor, and attestation lets the customer release the data key only to a VM with the expected measurement. An **SGX enclave** has a smaller trusted base but requires splitting the database, and its history of side-channel attacks and the deprecation on client parts weigh against a large enclave; it fits a small key-management component. **Disk encryption on a plain VM** protects data at rest only: the key is in the guest's memory, which the hypervisor can read. What remains in either TEE choice: side channels, a denial of service by the provider, bugs in the guest or the enclave, input and output crossing shared buffers, and trust in the CPU vendor's firmware and attestation keys.

**(d)** On this VM: `meltdown`, `l1tf`, `mds`, `retbleed` and most others read `Not affected` (the CPU does not have those flaws); `spectre_v1` has barriers and pointer sanitisation; `spectre_v2` uses eIBRS and reports `BHI: Vulnerable`; `spec_store_bypass` is opt-in per thread. The command line starts `mitigations=auto,no_guest_host,no_guest_guest`. The flags `smep`, `smap`, `nx` are present and `pku`, `ibt`, `shstk`, `sgx`, `tdx_guest` and `sev` are absent. There is no `/sys/firmware/efi` and no `/dev/tpm0`. `randomize_va_space` is `2`. On a laptop or a server expect different answers; the exercise is to read each line as "this CPU has or lacks this flaw / this is on or off".

---

## Cheat sheet

- **Threat model:** the attacker runs code and reads a clock; boundaries are process, user and kernel, guest and host, enclave, boot chain, and the physical machine. Side channel = leak through how, covert channel = used on purpose.
- **Cache timing:** hit against flushed miss in this VM: 60 against 226 ticks of a 2.1 GHz timer (79 ns, 3.8 to 4.4 times; timer cost 48 ticks); threshold errors 25 in 100,000 hits and 0 in 100,000 misses; a byte sent through 256 lines was recovered 99.1 to 99.3 %, 65 to 73 µs per byte. Needed for the receiver: a page stride, a scrambled order, a written probe array (otherwise 6.5, 18.4, 1.4 and 2.3 % when each is broken). Flush+Reload needs shared pages and a flush; Prime+Probe needs eviction sets and its signal is a few ns (my in-VM attempt was 9 to 34 % against 1.6 % by chance: too noisy).
- **Constant-time code:** no branch, no address and no variable-time instruction on a secret. Early exit rose 0.34 ns per matching byte (1.78 to 23.3 ns over 63 bytes); the OR-of-XORs version was flat at 22.3 ns. Guesses `256 n` against `256^n`; samples `(2σ/δ)²`. Use the library routine and read the assembly.
- **Spectre v1:** check, load with the index, dependent load. Index mask 0.31 to 0.61 ns per access (1.9 times), `lfence` 19 times. **Meltdown:** user read of kernel memory; fixed by KPTI (a `CR3` write per kernel entry and exit, PCID keeps the TLB) or new silicon; not active here. **Spectre v2:** retpoline 1.6 against 12.5 ns for a predicted call (5 to 8 times), 8.3 against 12.5 for a random one; eIBRS instead on this CPU; IBPB, STIBP, BHI. **v4:** SSBD per thread by `prctl`, 2.0 times on a loop built for it (1.5 to 2.1).
- **Reading `sysfs`:** `Not affected` (CPU lacks the flaw), `Mitigation: ...` (on), `Vulnerable` (nothing active); in a guest it is what the hypervisor exposes. `mitigations=auto,no_guest_host,no_guest_guest` leaves the user, kernel and thread vectors on.
- **Rowhammer:** repeated row activation flips neighbouring bits; an integrity attack; refresh, TRR, DDR5 refresh management and ECC reduce it; speculation fixes do not touch it.
- **Memory-safety defences:** NX (W^X is policy: W and X mapping allowed here), SMEP/SMAP, ASLR (28 bits of `mmap`, 22 for the stack here; a leak defeats it), canary (`%fs:0x28`), RELRO and `BIND_NOW`, CET (shadow stack, `endbr64`), Arm PAC (7 bits at 48-bit addresses, 1 in 128) and BTI, MTE (4-bit tags per 16 bytes, 1 in 16, sync or async). None stops logic bugs or data-only attacks.
- **Trusted execution:** SGX enclave (encrypted memory, attestation; no side-channel protection; Foreshadow, SGAxe, LVI, Plundervolt-class, ÆPIC Leak; deprecated on 11th and 12th generation client Core). TrustZone (two worlds, NS bit, TZASC and TZPC, monitor at EL3, vendor's secure OS). Confidential VM (SEV-SNP, TDX, CCA: whole guest, large trusted base, bounce buffers). A TEE does not give availability, correctness inside, or side-channel freedom.
- **Boot:** secure boot enforces signatures (PK, KEK, `db`, `dbx`); measured boot extends PCRs (`new = H(old || H(stage))`), attests, and seals keys; neither proves the code is bug-free; revocation and rollback are the hard part; discrete TPM buses can be probed. No EFI or TPM in this VM.
- **Everything here was either measured in this VM with a short program (the cache gap and the byte sent through the cache, the early-exit and constant-time compares, the three bounds-check variants, ordinary and retpoline indirect calls, the Speculative Store Bypass switch, the NX test, the ASLR spread over 300 runs, the protections and canary code of a compiled binary, and the contents of `sysfs`, `/proc` and `/dev`), computed by a script (the PCR chaining model and the guess and sample arithmetic), taken from the published sources linked in the text (the Linux kernel documentation, the Meltdown and Spectre site, the Flush+Reload and remote-timing papers, Google's retpoline page, the Wikipedia articles on SGX, TrustZone, the TPM and UEFI), or standard architecture knowledge that I did not check against a manual and marked "standard" (the NS bit, TZASC and TZPC, the PCR extend rule, `db` and `dbx`, `endbr64` and BTI as landing pads, attestation).** I did not measure Meltdown, the buffer-leak attacks, Rowhammer, SGX, TrustZone, TDX, SEV, CET, MTE, PAC, a TPM or secure boot: this VM cannot show them. The Prime+Probe attempt was too noisy to be usable, and I say so. I fetched only the abstracts of the Flush+Reload and remote-timing papers, and not the original Spectre and Meltdown papers. **I wrote no exploit.**

**Next:** tutorial 31, ARM against x86 and the end-to-end walkthroughs (Core: encoding and micro-ops, memory-model and atomics differences, SIMD families, porting pitfalls, then what happens when you run a program, a packet arrives, a pointer is dereferenced, the machine powers on, a system call, a context switch, a page fault and `malloc` reaches DRAM). Say "next" for it.
