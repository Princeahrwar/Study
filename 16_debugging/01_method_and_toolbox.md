# DBG 01: The debugging method and the toolbox

*Part of the [debugging plan](00_plan.md) · Time: about 4 hours · You need: [C 01](../02_c_language/01_types_conversions_and_undefined_behavior.md) (undefined behaviour), [CO 07](../04_computer_organization/00_plan.md) (registers and stack frames) and [OS 02](../05_operating_systems/00_plan.md) (processes and signals). This is the **first tutorial of the debugging track**. The rest of the track teaches tools one at a time (`gdb`, core dumps, sanitizers, `strace`, `rr`); this one teaches what comes before any tool: **how to debug**, how to read a symptom and choose the first tool for it, how to build a program so that it can be debugged, how to write assertions and log lines that pay for themselves, and the two mechanical searches that find a bug without understanding it first, `git bisect` and delta debugging.*

Debugging is the part of the job in which seniority is most visible, and the part least taught. Two engineers meet the same crash. One reads the code, guesses, edits, rebuilds and runs again, ten times, and fixes the symptom. The other spends five minutes making the failure repeat on demand, five more shrinking it to a page of input, states what must be true if the bug is where they think it is, and checks that with one command. The second one is not cleverer; they have a **method**, and the method makes the tools useful. "Tell me about the hardest bug you fixed" is a standard question in senior loops for exactly this reason: the answer shows whether you debug by guessing or by narrowing.

**Seven honest limits, stated first.**

1. **One machine, one compiler.** Everything ran on a Linux 6.18 Firecracker VM with two vCPUs, `gcc` 13.3, `clang` and `gdb` 15.1. A different compiler or version changes some results (the signed overflow test in section 3 is the example); where I know of a difference I say so.
2. **No `rr`, no `cvise`, no Mac.** Neither tool is installed here, so record and replay is not run (DBG 08 covers it), the automatic reducer `cvise` is described **from memory**, and the **LLDB equivalents** for the Mac are from memory too.
3. **The bugs are small on purpose.** A 20 line program with a planted fault shows the method cleanly; a real bug has more noise. The methods scale; the numbers do not.
4. **The race is a lost update on a counter**, not the harder bugs (DBG 06). Its failure rate (91 of 400 runs here) depends on this VM's two vCPUs and on the compiler's code for `counter++`; on another machine it will differ, which is the point of section 8.
5. **Single runs.** Times and counts are what happened once; where two runs of a script could differ I say so.
6. **The ptrace-based demonstrations** (attaching `gdb` to a live process) ran as root in the VM. A normal user on a hardened host needs `ptrace_scope` relaxed or the same user and permissions (DBG 07).
7. **Company and team practice is not covered.** How a team files, triages and closes bugs is process, not technique.

By the end you can:

- **say the method in order** (reproduce, minimise, hypothesise, test, fix, prove, and write down what you learned) and **pick the first tool from the symptom** (crash, hang, wrong output, slow, leak, intermittent), with a command for each;
- **explain why a build flag changes what the debugger can see** (measured: at `-O0 -g` `gdb` showed `n = 4` and `acc`; at `-O2 -g` every variable was `<optimized out>` and `product()` was **inlined into `main`**; `-Og -g` kept the values; `-g` added **2,317** bytes of debug sections to a small program and `-g3` added **39,429**, because it records macros);
- **see what the compiler did to undefined behaviour** (measured: `a + b < a` with `INT_MAX` and 1 printed **0** at `-O0` and `-O2`, **1** with `-fwrapv`, and UBSan printed the exact overflow message; even at `-O0` the test was compiled as `b < 0`);
- **write assertions that do not change the program** (measured: a pop inside `assert` left the stack at **1** item with asserts on and **2** with `-DNDEBUG`) and **log lines that name the call, its arguments and the error** (`connect(127.0.0.1:9) failed: errno=111 (Connection refused)`);
- **find the commit that broke something with `git bisect run`** (measured: **5 test runs** for 40 commits, `ceil(log2 n)` in general, **12** for 3,600);
- **shrink a failing input with delta debugging** (measured: **2,000 bytes to 3** (`ABZ`) in **180 runs**, with the result one where removing any byte makes it pass);
- **make an intermittent bug repeat, and say how sure you are that a fix worked** (measured: **91 of 400** runs failed unpinned, **0 of 400** pinned to one CPU, **48 of 50** with a longer loop; computed: a bug that fails 1 run in 100 needs **299** clean runs to be 95 % sure it is fixed);
- **triage a hang, a busy loop and a leak on a live process with one command each** (measured: a deadlock showed as three sleeping threads with two stuck in `pthread_mutex_lock` at lines 6 and 7; a busy loop sampled at `spin.c:1` three times; a leak grew **10 MB** a second);
- **read the line table and the entries `-g` writes** and see what the debugger uses (`addr2line` gave `product fact.c:6` for one address).

## Where the numbers come from

Each experiment is a script `sN_*.sh` in `prog/` with its output frozen in `v/`, run with `run.sh`. `ub.c` is the signed overflow test, `fact.c` is the small program for the flag and DWARF runs, `stack.c` the assert with a side effect, `log.h` and `connect.c` the log macro and its use, `parse.c` and `ddmin.py` the failing parser and the reducer, `race.c` the unlocked counter, `deadlock.c`, `spin.c` and `leak.c` the three live processes, and `dwarfsum.py` prints one function's DWARF entry. The answers to the problems are `ans_a.py`.

Every number in the text is copied from `v/*.out` or computed from a printed figure with the arithmetic shown. A number that is neither is marked **from memory**.

## Data and timing

| What | Size or time |
|---|---|
| Overflow under four flag sets (`s1_ub.sh`) | 4 compiles, under a second |
| Build flags in `gdb` (`s2_flags.sh`) | 3 sessions, 4 sizes, 2 disassemblies, about 5 s |
| Assertions and logging (`s3_assert_log.sh`) | 4 programs, under a second |
| Bisect over 40 commits (`s4_bisect.sh`) | 40 commits, 5 test runs, about 2 s |
| Delta debugging (`s5_ddmin.sh`) | 180 runs of a tiny program, about 2 s |
| Intermittent failure (`s6_repro.sh`) | 1,250 runs, about 1 minute |
| Live triage (`s7_triage.sh`) | 3 live processes, 4 `gdb` attaches, about 15 s |
| DWARF (`s8_dwarf.sh`) | one binary, 4 readers, under a second |

---


## 1. The method

Every debugging session, however strange, is the same loop. Say it out loud in an interview; it is half of the answer.

1. **Reproduce.** Make the failure happen on demand, as fast as you can. Until you can repeat it you are guessing, and you cannot tell whether a change helped.
2. **Minimise.** Shrink the input, the configuration and the code path until nothing can be removed without losing the failure (sections 7 and 8). A small failing case usually points at the cause by itself, and it becomes the regression test.
3. **Observe, then hypothesise.** Read what the failure actually says (the signal, the message, the exit status, the last log line, the stack) before forming an idea. State the hypothesis as a **prediction that could be wrong**: "if the cause is a use after free, the pointer in frame 2 will point into a block that was freed". A hypothesis you cannot test is not yet useful.
4. **Test it with the cheapest experiment that separates the hypotheses.** One breakpoint, one assertion, one `strace` line, one run with a sanitizer. Change **one thing at a time**, because two changes together tell you nothing about either.
5. **Fix the cause, not the symptom.** A `NULL` check added where the crash was reported hides the bug that produced the `NULL`. Ask why the state was possible, and where else the same mistake could be.
6. **Prove the fix with the measurement that showed the bug**, and for an intermittent bug with enough runs to mean something (section 8).
7. **Keep the evidence.** Turn the minimal case into a test, and write down the cause in the commit message. The next person with the same symptom finds it by searching.

Two habits separate fast debuggers from slow ones.

- **They look at the data before the code.** The state of the program at the moment of failure (a core dump, a log, a trace, a packet capture) is a fact; the code is only a theory of what should have happened. Compare what the program did with what you assumed it did, and the first difference is where to look.
- **They bisect everything.** Half of the commits, half of the input, half of the code path, half of the configuration. A search that halves the space finds one fault in `log2 n` steps; reading all of it takes `n`. Sections 6 and 7 are two mechanical versions of this, and section 9 shows the same idea on a running system.

### 1.1 What not to do

- **Change several things and rerun.** If the failure vanishes you do not know why, and it often returns.
- **Trust a hunch over a measurement.** Confident, wrong theories cost the most time. When the evidence disagrees with the theory, the theory is wrong.
- **Assume the bug is in the library, the compiler or the hardware.** It sometimes is, rarely, and you must **prove** it (a minimal case that fails with no other code involved, section 7) before you say so.
- **Debug a stale binary.** Check that the binary being run is the one just built (a `build-id`, a timestamp, a printed version) before spending an hour on behaviour that the current source cannot produce.
- **Fix the bug and lose the test.** A bug that has been fixed once without a test is a bug that returns.

## 2. Reading the symptom: the first tool for each

The symptom picks the first tool. The rest of the track teaches each tool in depth; this table is the map, and you should be able to produce it from memory.

| Symptom | What it means | First look | First tool | Where taught |
|---|---|---|---|---|
| **Crash** (a signal ends the process) | A fault the kernel turned into a signal: `SIGSEGV` (bad address), `SIGABRT` (an `abort`, often from `assert` or the allocator), `SIGBUS`, `SIGFPE`, `SIGILL` | The signal and the faulting address; the backtrace | A core dump opened in `gdb`; or run under `gdb`; a sanitizer if memory is the suspect | DBG 04, DBG 02, DBG 05 |
| **Hang** (alive, not progressing, no CPU) | Blocked: a lock, a read, a futex, a full pipe, a deadlock | What each thread waits on | `gdb -p PID` with `thread apply all bt`; `/proc/PID/wchan`; `strace -p` | DBG 07, DBG 06 |
| **Busy loop** (100 % CPU) | Running, not finishing: a spin, a loop that does not end, a retry storm | Where the CPU is, by sampling | A few stack samples (`gdb`, `perf top`), `strace -c` for a syscall storm | DBG 07, LX 10 |
| **Wrong output** | Logic or data error; sometimes undefined behaviour | The first place the program state differs from what you expect | A breakpoint or a print at the point where it first differs; UBSan and ASan; a diff of two runs | DBG 02, DBG 05 |
| **Slow** | Not a bug in the strict sense; a resource is the limit | Which resource: CPU, memory, disk, network, a lock | `perf`, `strace -T`, flame graphs, the USE method | LX 05, LX 10 |
| **Leak** (memory grows) | Allocations that are not freed, or a cache without a bound | Is the growth steady, or does it follow a request? | `VmRSS` over time, then LeakSanitizer, Valgrind or a heap profile | DBG 05, LX 06 |
| **Intermittent** (fails sometimes) | A race, an uninitialised value, an environment difference, a timing dependence | How often, and under what conditions | Make it frequent (section 8), then `rr` or ThreadSanitizer | DBG 06, DBG 08 |
| **Works under the debugger, fails outside** | Timing, uninitialised memory, a different environment or address layout | What the debugger changed: timing, `ASLR`, environment variables, stack contents | Compare the environments; a core dump instead of a live session | DBG 08 |
| **Fails only in release** | Undefined behaviour, a missing `volatile`, a compiler bug (rare), a timing change | The same source at `-O0` and `-O2` | UBSan and ASan on the release flags; compare the assembly | DBG 05, section 3 |

A good answer to "it crashes in production" does not start with a tool. It starts with questions: **since when** (what changed: a deploy, a config, a dependency, a traffic pattern), **how often and for whom**, **what does the evidence say** (the log, the signal, the core), and **can I reproduce it**. Section 9 shows the live triage commands.

---


## 3. Building for debugging

A debugger knows only what the compiler told it, and the optimiser removes a lot of what a debugger wants. Choosing the build flags is the first debugging decision.

| Flag | What it does for debugging |
|---|---|
| `-g` | Writes DWARF debug information: the line table, the functions, the variables and their types (section 4). It does not change the generated code, only adds sections that are not loaded when the program runs |
| `-g3` | Adds **macro definitions** (`-g` alone does not). The debugger can then evaluate `MAX_ITEMS` in an expression |
| `-O0` | No optimisation: every variable lives in memory, every line is a place to stop. The easiest to debug, and the slowest; some bugs only exist at higher levels |
| `-Og` | Optimise for debugging: the optimisations that do not hurt it. The usual choice when `-O0` is too slow and `-O2` is too opaque |
| `-O2 -g` | What ships, with debug information. Variables can be `<optimized out>`, lines are reordered, functions are inlined |
| `-fno-omit-frame-pointer` | Keeps the frame pointer register as a frame pointer, so profilers and some unwinders can walk the stack quickly (section 3.2) |
| `-fsanitize=address,undefined` | Instruments the code to catch memory errors and undefined behaviour at run time (DBG 05) |
| `-fno-inline`, `-fno-omit-frame-pointer`, `-fno-optimize-sibling-calls` | Keep the call structure the source shows, so that a backtrace matches the code |

### 3.1 What the optimiser takes away

I compiled the same 15 line program three ways and stopped `gdb` in `product()`, a loop that multiplies an array.

```c
#include <stdio.h>
#include <stdlib.h>

static long product(const int *v, int n) {
    long acc = 1;
    for (int i = 0; i < n; i++) acc *= v[i];
    return acc;
}

int main(int argc, char **argv) {
    int v[4] = {2, 3, 5, 7};
    int n = argc > 1 ? atoi(argv[1]) : 4;
    long r = product(v, n);
    printf("%ld\n", r);
    return 0;
}
```

```bash
for f in "-O0 -g" "-O2 -g" "-Og -g"; do
  gcc $f -o /tmp/fact fact.c
  echo "## gcc $f"
  gdb -q -batch -ex 'break product' -ex 'run' -ex 'info args' -ex 'info locals' -ex 'bt' /tmp/fact 2>&1 | grep -E '^(v|n|acc|i|#)' || echo "(no breakpoint hit: product() was inlined into main)"
done
```

```text
--- 1. gdb at a breakpoint in product(): what can it see?
## gcc -O0 -g
v = 0x7fffffff6150
n = 4
acc = 0
#0  product (v=0x7fffffff6150, n=4) at fact.c:5
#1  0x0000555555555251 in main (argc=1, argv=0x7fffffff6298) at fact.c:13
## gcc -O2 -g
n = <optimized out>
v = <optimized out>
acc = <optimized out>
acc = <optimized out>
i = <optimized out>
#0  0x00005555555550d5 in product (n=<optimized out>, v=<optimized out>) at fact.c:5
#1  main (argc=<optimized out>, argv=<optimized out>) at fact.c:13
## gcc -Og -g
v = 0x7fffffff6150
n = 4
i = 0
acc = 1
#0  product (v=v@entry=0x7fffffff6150, n=4) at fact.c:6
#1  0x00005555555551f4 in main (argc=<optimized out>, argv=<optimized out>) at fact.c:13
```

At **`-O0 -g`** `gdb` stops in `product`, shows the arguments, the locals and a two frame backtrace with a source line each. At **`-O2 -g`** the compiler inlined `product` into `main`, so `gdb` shows it as frame 0 and `main` as frame 1 **with no address**, which is how it prints an inlined call; every argument and local is **`<optimized out>`**, and `main`'s own `argc` and `argv` are gone too. At **`-Og -g`** the variables are back (`n = 4`, `acc = 1`, `i = 0`), `v=v@entry=...` says that the value is also known from the function's entry, and only `main`'s parameters are lost.

`<optimized out>` does not mean the value is wrong; it means the compiler no longer keeps it anywhere that the debugger's description can name at that instruction. At `-O2` a value can live in a register for part of a function, be recomputed from other values, or be dropped once it is dead. The choices when you meet it:

- Rebuild the file you care about at `-O0` or `-Og` (a `#pragma GCC optimize` or a per-file flag in the build system), and keep everything else as it was.
- Look in the caller: a value that was passed in is often still visible in the frame above.
- Print a register or memory directly (`info registers`, `x/4gx $sp`, DBG 02).
- Add a temporary `volatile` read or a log line that forces the value to be kept. This changes the optimiser's decisions, so check that the bug is still there.

Debug information has a cost in file size, not in speed, because the sections are not loaded.

```bash
echo "--- 2. size of the binary and of all its .debug_* sections together, in bytes"
for f in "-O2" "-O2 -g" "-O2 -g3" "-O0 -g"; do
  gcc $f -o /tmp/fact fact.c
  printf "%-10s file %6d  debug sections %6s\n" "$f" "$(stat -c %s /tmp/fact)" "$(sz /tmp/fact)"
done
```

```text
--- 2. size of the binary and of all its .debug_* sections together, in bytes
-O2        file  16064  debug sections      0
-O2 -g     file  19008  debug sections   2317
-O2 -g3    file  56192  debug sections  39429
-O0 -g     file  17776  debug sections   1227
```

`-g` added **2,317** bytes of debug sections to a 16 KB program (14 %); `-g3` added **39,429**, seventeen times more, almost all of it macro definitions pulled in from the system headers. At `-O0 -g` the debug sections are **1,227** bytes, about half of `-O2 -g`, because optimised code needs location lists to say where each value lives at each address. A real program has hundreds of megabytes of debug information, which is why releases keep it in **separate files** (DBG 04).

### 3.2 The frame pointer

On x86-64 a function may or may not set up a frame: save the caller's `rbp`, point `rbp` at its own frame, so that each frame links to the previous one. Compilers at `-O2` omit it (the register is then free), which saves one register and a few instructions per call.

```bash
echo "--- 3. -fno-omit-frame-pointer: how a function starts"
for f in "-O2" "-O2 -fno-omit-frame-pointer"; do
  gcc $f -c -o /tmp/fact.o fact.c
  printf "%-30s first instructions of main: " "gcc $f"
  objdump -d --no-show-raw-insn /tmp/fact.o | awk '/<main>:/{f=1;next} f&&NF{printf "%s ", $2; if(++n==3){exit}}'; echo
done
```

```text
--- 3. -fno-omit-frame-pointer: how a function starts
gcc -O2                        first instructions of main: endbr64 sub movdqa
gcc -O2 -fno-omit-frame-pointer first instructions of main: endbr64 push mov
```

Without the flag the first instructions of `main` are `endbr64`, `sub` (make room on the stack), `movdqa`; with it they are `endbr64`, **`push`**, **`mov`**, which save the old `rbp` and set the new one. Walking `rbp` links is how `perf` and eBPF profilers take a stack in a few nanoseconds, and how a hand-written crash handler can unwind in a signal handler. The alternative is DWARF unwind tables, which are exact but slow to interpret. Many distributions now build with frame pointers on for this reason (**from memory**: Fedora 38 and Ubuntu 24.04 did so, with a reported cost of a percent or two). DBG 04 covers the two unwinders and why a backtrace can come out corrupt.

### 3.3 The compiler is allowed to assume your program is correct

Undefined behaviour (C 01) is the reason a program can print different answers with different flags and no change to the source. The test `a + b < a` is a common way to check for signed overflow, and it is wrong, because signed overflow is undefined and the compiler may assume it does not happen.

```c
#include <limits.h>
#include <stdio.h>

/* Did a + b overflow?  The wrap-around test is undefined behaviour for int. */
int overflowed(int a, int b) { return a + b < a; }

int main(void) {
    volatile int a = INT_MAX;
    volatile int b = 1;
    printf("overflowed(INT_MAX, 1) = %d\n", overflowed(a, b));
    return 0;
}
```

```bash
# One source, one compiler: the answer depends on flags that are not about the language.
cd "$(dirname "$0")"
echo "--- 1. the same program under four sets of flags"
for o in "-O0" "-O2" "-O2 -fwrapv" "-O0 -fsanitize=undefined"; do
  gcc $o -o /tmp/ub ub.c && printf "%-26s " "gcc $o" && /tmp/ub 2>&1 | tr '\n' ' '; echo
done
echo "--- 2. what the compiler did with the test (the -O0 assembly of overflowed)"
gcc -O0 -S -o - ub.c | awk '/^overflowed:/{f=1} f&&/^\t(add|cmp|sar|shr|test|movl|set|js|jns)/{print} /\.size\toverflowed/{exit}' | head -8
```

```text
--- 1. the same program under four sets of flags
gcc -O0                    overflowed(INT_MAX, 1) = 0
gcc -O2                    overflowed(INT_MAX, 1) = 0
gcc -O2 -fwrapv            overflowed(INT_MAX, 1) = 1
gcc -O0 -fsanitize=undefined ub.c:5:41: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int' overflowed(INT_MAX, 1) = 1
--- 2. what the compiler did with the test (the -O0 assembly of overflowed)
        movl    %edi, -4(%rbp)
        movl    %esi, -8(%rbp)
        movl    -8(%rbp), %eax
        shrl    $31, %eax
```

With `a` equal to `INT_MAX` and `b` equal to 1 the wrap-around test should be true. It printed **0** at `-O0` and at `-O2`, and **1** with `-fwrapv` (which defines signed overflow as wrapping) and with UBSan. The assembly shows why: even at `-O0`, GCC rewrote the test, assuming no overflow, as `b < 0` (`shrl $31` takes the sign bit of `b`); it never did the addition. This surprised me; I expected `-O0` to do what the source says, and for this expression it does not. The general rule is that **optimisation level is not the only thing that decides what the compiler does with undefined behaviour**, and **the sanitizer build is a different program** (it keeps the addition so that it can check it). On another GCC or on `clang` the `-O0` result may differ. The fixes that matter: write the test without undefined behaviour (check `b > INT_MAX - a`), or use `__builtin_add_overflow`.

When a bug appears only at `-O2`, or only without `-g`, or only with the sanitizer, suspect undefined behaviour first, and run the same code under UBSan and ASan before anything else (DBG 05). The rarer alternative, a compiler bug, has to be proved (section 7).

---

## 4. What `-g` writes: the line table and the entries

DWARF is the format in which a compiler describes its output to a debugger. On GCC 13 the default is DWARF 5 (`readelf` printed `Version: 5`; `-gdwarf-4` gave 4). Two parts do most of the work.

- The **line table** maps every machine instruction address to a source file, a line and a column.
- The **debugging information entries** (DIEs) form a tree: a compilation unit holds functions (`DW_TAG_subprogram`), each with parameters (`DW_TAG_formal_parameter`), local variables (`DW_TAG_variable`) and blocks, plus types, with names, declaration lines and, for each variable, a **location**: a stack offset at `-O0`, a list of register and address ranges at `-O2`.

```bash
# What -g puts in the file: a line table (address to source line) and a tree of entries (functions, variables, types).
cd "$(dirname "$0")"
gcc -O0 -g -o /tmp/fact fact.c
echo "--- 1. the line table: instruction addresses of product() against source lines (readelf --debug-dump=decodedline)"
readelf --debug-dump=decodedline /tmp/fact 2>/dev/null | awk '$1=="fact.c" && $2>=4 && $2<=9 {print $1, $2, $3}' | head -9
echo "--- 2. the same mapping the other way: addr2line on one address"
A=$(readelf --debug-dump=decodedline /tmp/fact 2>/dev/null | awk '$1=="fact.c" && $2==6 {print $3; exit}')
echo "address $A -> $(addr2line -f -e /tmp/fact $A | tr '\n' ' ')"
echo "--- 3. the entries for product(): a function with a name, a source line, parameters and locals"
readelf --debug-dump=info /tmp/fact 2>/dev/null | python3 dwarfsum.py product
echo "--- 4. without -g the same questions have no answer"
gcc -O0 -o /tmp/fact_nog fact.c
addr2line -f -e /tmp/fact_nog $A | tr '\n' ' '; echo
```

```text
--- 1. the line table: instruction addresses of product() against source lines (readelf --debug-dump=decodedline)
fact.c 4 0x1189
fact.c 5 0x1198
fact.c 6 0x11a0
fact.c 6 0x11a7
fact.c 6 0x11a9
fact.c 6 0x11c1
fact.c 6 0x11cd
fact.c 6 0x11d1
fact.c 7 0x11d9
--- 2. the same mapping the other way: addr2line on one address
address 0x11a0 -> product fact.c:6
--- 3. the entries for product(): a function with a name, a source line, parameters and locals
subprogram        name product  (declared on line 4)
formal_parameter  name v  (declared on line 4)
formal_parameter  name n  (declared on line 4)
variable          name acc  (declared on line 5)
variable          name i  (declared on line 6)
--- 4. without -g the same questions have no answer
product fact.c:?
```

The line table lists `fact.c` line 6, the `for` loop, at **six different addresses** (`0x11a0` to `0x11d1`): one source line is several instruction ranges (initialise, test, increment). `addr2line` reads the table in the other direction, and gave **`product fact.c:6`** for `0x11a0`. The entries for `product` hold its name and line, its two parameters `v` and `n`, and its locals `acc` and `i`; at `-O0` each has a fixed stack location, which is why `gdb` can always print them. Without `-g` there are no line numbers (`fact.c:?` from the symbol table alone) and no variable names; `gdb` can still show function names from the symbol table and addresses from the disassembly, but not `acc`.

Two facts follow for later tutorials. A **stripped** binary has lost even the symbol table, so a crash shows bare addresses, and you need the unstripped file or a separate debug file with the same **build-id** to turn them back into lines (DBG 04). And the debug information describes a specific build: a line number from last week's binary in this week's source points at the wrong code, which is the stale binary mistake of section 1.1.

---


## 5. Assertions, logging and print debugging

### 5.1 Assertions state what must be true

An **assertion** is a check of something that the code relies on, written next to the code that relies on it. It turns "this should never happen" into a failure at the line where the assumption broke, instead of a corrupt value found a million instructions later. A failing `assert` names the file, the line, the function and the expression, and calls `abort`, which raises `SIGABRT` and, with core dumps on, leaves a core file to open (DBG 04).

Three rules keep assertions useful.

1. **Assert the programmer's errors, never the environment's.** An invariant (a pointer is not null here, an index is in range, a lock is held) is an assertion. A failed `malloc`, a missing file or a closed peer is an expected condition: handle it, do not abort.
2. **An assertion must have no side effects**, because release builds compile it out with `-DNDEBUG`.
3. **Say what the check means.** `assert(n >= 0 && "length must not be negative")` prints the message with the expression, because a string literal is non-zero.

```c
#include <assert.h>
#include <stdio.h>

static int items[8], top;
static void push(int v) { items[top++] = v; }
static int pop(void) { return items[--top]; }

int main(void) {
    push(3);
    push(4);
    assert(pop() == 4);      /* the side effect is inside the assert */
    printf("top after the assert: %d (should be 1)\n", top);
    return 0;
}
```

```bash
cd "$(dirname "$0")"
echo "--- 1. assert() with a side effect: the build type changes the program"
gcc -O0 -o /tmp/stack stack.c && echo "gcc -O0 (asserts on):" && /tmp/stack
gcc -O0 -DNDEBUG -o /tmp/stack stack.c && echo "gcc -O0 -DNDEBUG (asserts compiled out):" && /tmp/stack
echo "--- 2. a failing assert names the file, line, function and expression"
cat > /tmp/af.c <<'XEOF'
#include <assert.h>
static int checked(int n) { assert(n >= 0 && "length must not be negative"); return n; }
int main(void) { return checked(-1); }
XEOF
gcc -o /tmp/af /tmp/af.c && /tmp/af 2>&1 | sed 's#/tmp/##'
echo "--- 3. a log line that names the call, its arguments and the error"
gcc -o /tmp/connect connect.c && /tmp/connect 2>&1
```

```text
--- 1. assert() with a side effect: the build type changes the program
gcc -O0 (asserts on):
top after the assert: 1 (should be 1)
gcc -O0 -DNDEBUG (asserts compiled out):
top after the assert: 2 (should be 1)

--- 2. a failing assert names the file, line, function and expression
af: af.c:2: checked: Assertion `n >= 0 && "length must not be negative"' failed.
```

The pop inside the assertion ran in the first build and **did not run** in the `-DNDEBUG` build: the stack ended with **1** item with asserts on and **2** with them off, the same source printing two answers. This is the classic way to get a bug that "only happens in release". If an assertion contains a call, ask whether the program would still be right without it; if not, move the call out and assert on its result.

Assertions have two costs and one limit. They cost a branch each, which is why hot loops use them sparingly or keep a heavier check for debug builds; and they cost a crash in production, which is a decision (a service may prefer to log and continue; an embedded controller may prefer to stop in a safe state). The limit is that they only find what they check; a sanitizer (DBG 05) checks every memory access without being asked.

### 5.2 Logging that pays for itself

A log line is evidence left for a debugger who is not there. Make each one answer: **where** (file, line, function), **what** (the operation and its arguments), **what happened** (the error code and its text), and, in a real system, **when** and **for which request** (a timestamp and a request or connection identifier).

```c
#ifndef LOG_H
#define LOG_H
#include <stdio.h>

/* One line per event: level, file:line, function, then the facts.  No timestamps here so that the
   output can be compared between runs; a real logger adds one. */
#define LOG(level, fmt, ...) \
    fprintf(stderr, "%-5s %s:%d %s: " fmt "\n", level, __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) LOG("ERROR", fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...) LOG("DEBUG", fmt, ##__VA_ARGS__)
#endif
```

```c
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "log.h"

static int open_peer(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
    inet_pton(AF_INET, ip, &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) {
        /* what failed, with what arguments, and why */
        LOG_ERR("connect(%s:%d) failed: errno=%d (%s)", ip, port, errno, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int main(void) {
    LOG_DBG("opening peer");
    return open_peer("127.0.0.1", 9) < 0;
}
```

```text
--- 3. a log line that names the call, its arguments and the error
DEBUG connect.c:23 main: opening peer
ERROR connect.c:15 open_peer: connect(127.0.0.1:9) failed: errno=111 (Connection refused)
```

The line `ERROR connect.c:15 open_peer: connect(127.0.0.1:9) failed: errno=111 (Connection refused)` says which call failed, with which address and port, and why, so no one has to guess whether the failure was the address, the network or the peer. Compare the usual `error: connect failed`, which needs a rerun to learn anything. Rules that follow:

- **Log at the point of failure, once.** If every layer logs the same error as it returns it up, the log fills with repeats; log where it is handled or where context is added.
- **Log the inputs that decide the path**, not only the outcome: a request id, the key being looked up, the size of the buffer.
- **Levels exist so that you can turn them on without a rebuild.** Debug and trace are off by default and switchable for one module or one connection, because in production the line you need is usually one that nobody enabled.
- **Do not log secrets or personal data**, and avoid unbounded values (a whole request body).
- **A log line is not free.** Formatting a string on a hot path costs more than the work it describes; use a level check before the formatting, or a ring buffer that is dumped only on failure.
- **Logging changes timing.** In a concurrent program a `printf` that makes the bug disappear tells you the bug is a race (DBG 06).

### 5.3 Print debugging, and when it is the right tool

Printing a value and rerunning is not unprofessional; it is a debugger with the slowest possible cycle and no setup. It is the right tool when:

- you cannot attach a debugger (production, a container without `gdb`, a phone, a device with no JTAG);
- the bug depends on timing, and a breakpoint would stop the threads that must race;
- the failure is the **history** of many events (the order of a thousand calls), which a log shows at once and a debugger shows one at a time;
- you want the trace of **one** variable over a long run.

A debugger is the right tool when the data is large or structured (print a whole struct, follow pointers), when you do not know in advance what you will need (each rerun of a print costs a rebuild and a run), and when you need to **change** state to test a hypothesis (`set var`, `call`, DBG 03). A good practice is the hybrid: `gdb` breakpoint commands that print and continue (`dprintf`, DBG 03) give print debugging with no rebuild.

---


## 6. `git bisect`: find the commit that broke it

When something worked last month and does not now, the search for the change that broke it is mechanical. `git bisect` does a **binary search over history**: you name a good commit and a bad one, it checks out the middle, you say whether that one is good or bad, and it halves the range again. For `n` commits between them it takes at most `ceil(log2 n)` tests: **6** for 40 commits, **10** for 1,000, **14** for 10,000 (`ans_a`). Three months of a busy branch is about 3,600 commits and **12** tests.

The useful form is `git bisect run COMMAND`, where a script judges each commit and bisect does the rest unattended. The script's exit status is the verdict: **0** good, **1 to 127 except 125** bad, and **125** means "this commit cannot be tested, skip it" (it does not build, for example). Any other status above 127 aborts the bisect.

```bash
# git bisect run: find the commit that broke a test among 40, with a script as the judge.
set -e
W=/tmp/bis; rm -rf $W; mkdir $W; cd $W
git init -q . ; git config user.email a@b.c; git config user.name t
cat > main.c <<'XEOF'
#include <stdio.h>
#include "limit.h"
int main(void) { return LIMIT >= 100 ? 0 : 1; }   /* the test: the limit must stay at least 100 */
XEOF
cat > test.sh <<'XEOF'
#!/bin/sh
gcc -o t main.c || exit 125     # 125 tells bisect: this commit cannot be tested, skip it
./t
XEOF
chmod +x test.sh
for i in $(seq 1 40); do
  v=$((100 + i)); [ $i -ge 27 ] && v=$((i - 26))          # commit 27 breaks it
  echo "#define LIMIT $v" > limit.h
  echo "build $i" >> notes.txt
  git add -A; git commit -q -m "commit $i"
done
echo "--- 1. the bug is somewhere in 40 commits; HEAD fails, the first commit passes"
git checkout -q master 2>/dev/null || git checkout -q main
./test.sh && echo "HEAD passes" || echo "HEAD fails"
echo "--- 2. bisect with a script: it checks out the midpoint, runs test.sh, and narrows the range"
{ git bisect start HEAD $(git rev-list --max-parents=0 HEAD) 2>&1; git bisect run ./test.sh 2>&1; } \
  | grep -E "^(Bisecting|running|[0-9a-f]{40} is the first bad commit)" | sed -E 's/^running .*/  ran test.sh/; s/^[0-9a-f]{40}/<sha>/' \
  | awk '/ran test.sh/{n++} {print} END{print n " runs of test.sh"}'
echo "--- 3. the commit it names"
git show -s --format='%s' refs/bisect/bad
git bisect reset >/dev/null 2>&1
echo "log2(40) = 5.3, so 5 or 6 runs in theory"
```

```text
--- 1. the bug is somewhere in 40 commits; HEAD fails, the first commit passes
HEAD fails
--- 2. bisect with a script: it checks out the midpoint, runs test.sh, and narrows the range
Bisecting: 19 revisions left to test after this (roughly 4 steps)
  ran test.sh
Bisecting: 9 revisions left to test after this (roughly 3 steps)
  ran test.sh
Bisecting: 4 revisions left to test after this (roughly 2 steps)
  ran test.sh
Bisecting: 2 revisions left to test after this (roughly 1 step)
  ran test.sh
Bisecting: 0 revisions left to test after this (roughly 0 steps)
  ran test.sh
<sha> is the first bad commit
5 runs of test.sh
--- 3. the commit it names
commit 27
log2(40) = 5.3, so 5 or 6 runs in theory
```

The script builds a repository of 40 commits where commit 27 sets a limit below 100, which the test treats as a failure. `HEAD` fails and the first commit passes. Bisect then ran the test **five times**, each time the "revisions left to test" line roughly halving it (19, 9, 4, 2, 0), and named the commit with the message `commit 27`. Five runs agree with the arithmetic: 40 commits need at most 6, and this search took 5.

How to use it well:

- **Write the test first and make it fail only for the bug you are chasing.** A test that fails for two reasons sends the search to whichever failure is older. Pin everything else (inputs, seeds, environment).
- **Make the test fast and automatic**, and have it return 125 when the commit is untestable. A commit in the middle that does not compile should be skipped, not judged.
- **Bisect on the symptom, not on your theory.** The commit it names is a fact; the reason it matters is for you to find.
- **Check the end points first.** Run the test on the claimed good and bad commits yourself; a bisect between two commits that both fail, or whose test is flaky, produces a confident wrong answer.
- **A flaky test needs repetition.** For an intermittent bug a single pass does not prove a commit good (section 8): run the test enough times that a pass means something (299 passes for a bug that fails 1 run in 100, to be 95 % sure), and treat a pass with fewer runs as "unknown".
- **Merge commits and large commits limit the answer.** If the named commit is a merge, bisect again inside the merged branch; if it is a 3,000 line commit, you have narrowed the search but not found the line, and the next tool is the diff and delta debugging (section 7).
- **For a kernel or a large build**, bisecting costs a build per step; `ccache`, a faster machine and a smaller configuration pay for themselves. The same method works for a regression in a performance number if the test compares the measurement to a threshold.

`git bisect` is the debugging equivalent of a proof by contradiction: it asks only "good or bad?" and never needs you to understand the code. That is its strength on someone else's code and its limit, because the commit is a clue, not an explanation.

## 7. Delta debugging: shrink the failing input

A bug found with a large input (a 40 MB file, a 5,000 line configuration, a trace of a million requests) is hard to reason about. **Delta debugging** (the `ddmin` algorithm of Zeller and Hildebrandt) shrinks it automatically. You give it an input that fails and a test that says whether a candidate still fails; it removes pieces, keeps any removal that still fails, and stops when no single piece can be removed. The result is a **1-minimal** input, often a few lines, that is usually all you need to see the cause.

The algorithm in words: split the input into `n` chunks (start with 2). If any single chunk still fails, keep only that chunk. Otherwise, if removing any one chunk (keeping the rest) still fails, keep that. If neither works, double `n` (smaller chunks) and try again; stop when the chunks are single elements and nothing can be removed.

I wrote a 25 line version over bytes, with `parse.c` as a stand-in for a parser that mishandles an `AB` pair followed anywhere later by a `Z`.

```c
#include <stdio.h>
#include <stdlib.h>

/* A stand-in for a parser bug: a pair "AB" followed, anywhere later, by "Z" is mishandled. */
int main(void) {
    int c, seen_ab = 0, prev = 0;
    while ((c = getchar()) != EOF) {
        if (prev == 'A' && c == 'B') seen_ab = 1;
        if (seen_ab && c == 'Z') abort();
        prev = c;
    }
    return 0;
}
```

```python
#!/usr/bin/env python3
"""Delta debugging (ddmin, Zeller and Hildebrandt): shrink a failing input to one where removing
any single byte makes the failure go away.  The test is: does ./parse abort (exit by SIGABRT)?"""
import random, subprocess, sys

def fails(data: bytes) -> bool:
    r = subprocess.run(["/tmp/parse"], input=data, capture_output=True)
    return r.returncode == -6

def ddmin(data: bytes):
    n, tests, history = 2, 0, [len(data)]
    while len(data) >= 2:
        size = len(data) // n
        chunks = [data[i * size:(i + 1) * size if i < n - 1 else len(data)] for i in range(n)]
        reduced = False
        for i in range(n):                      # try each chunk alone
            tests += 1
            if fails(chunks[i]):
                data, n, reduced = chunks[i], 2, True
                break
        if not reduced:
            for i in range(n):                  # try removing each chunk
                comp = b"".join(chunks[:i] + chunks[i + 1:])
                tests += 1
                if fails(comp):
                    data, n, reduced = comp, max(n - 1, 2), True
                    break
        if reduced:
            history.append(len(data))
        elif n < len(data):
            n = min(len(data), n * 2)           # finer granularity
        else:
            break
    return data, tests, history

random.seed(7)
alphabet = b"abcdefghijklmnopqrstuvwxy0123456789 ,.;:()[]{}=+-*/\n"
data = bytearray(random.choice(alphabet) for _ in range(2000))
data[777:779] = b"AB"
data[1500] = ord("Z")
data = bytes(data)
assert fails(data) and not fails(data[:777]) and not fails(data[779:])
print("input: %d bytes; it fails; the pieces on either side of the pair do not" % len(data))
out, tests, hist = ddmin(data)
print("after ddmin: %d bytes %r in %d runs of the program" % (len(out), out, tests))
print("sizes after each successful reduction:", hist)
ok = all(not fails(out[:i] + out[i + 1:]) for i in range(len(out)))
print("removing any single byte makes it pass (1-minimal):", ok)
print("removing one byte at a time and starting over needs up to n(n+1)/2 = %d runs in the worst case" % (len(data) * (len(data) + 1) // 2))
```

```bash
cd "$(dirname "$0")"
gcc -O0 -o /tmp/parse parse.c
python3 ddmin.py
```

```text
input: 2000 bytes; it fails; the pieces on either side of the pair do not
after ddmin: 3 bytes b'ABZ' in 180 runs of the program
sizes after each successful reduction: [2000, 1500, 1000, 750, 500, 375, 250, 188, 124, 93, 62, 47, 30, 23, 14, 11, 10, 8, 6, 5, 4, 3]
removing any single byte makes it pass (1-minimal): True
removing one byte at a time and starting over needs up to n(n+1)/2 = 2001000 runs in the worst case
```

A 2,000 byte input with `AB` planted at byte 777 and `Z` at byte 1500 shrank to the 3 bytes **`ABZ`** in **180 runs** of the program. The sizes show the halving: 2000, 1500, 1000, 750, 500, 375 and so on, then the last steps (6, 5, 4, 3) where the algorithm removes single bytes. The result is 1-minimal: removing any one byte makes the program pass. The cost model: the test count is at least about `2 log2 n` (22 for 2,000 bytes) when everything halves cleanly and at worst about `n²`; 180 sits in between (`ans_a`).

Practical points:

- **The test must be reliable and fast**, because it runs hundreds of times. A flaky test makes ddmin stop early or wander; give the test a timeout and make it answer "fails" only for the failure you are chasing. If the program can fail in a **different** way on a reduced input (a new crash that is not your bug), ddmin follows the wrong bug. Match the symptom (the signal, the message, the stack), not just "it failed".
- **Reduce structured input with a structured reducer.** Removing bytes from a JSON or C file mostly produces invalid input, which fails for a boring reason. For source code **`cvise`** (a C-Reduce derivative, **from memory; not installed here**) removes lines, tokens and whole constructs and keeps the file valid, and is how compiler bugs are reported with a ten line example. Fuzzers that save a failing input often come with a minimiser (`afl-tmin`, `libFuzzer -minimize_crash`; C 11).
- **Reduce the configuration, the code path and the thread count the same way.** Comment out half of a failing test; remove half of the config; use one worker instead of eight.
- **Proving a compiler or library bug** is a reduction problem: shrink until the failing program contains nothing but the suspect call, and check it against another compiler or version. If a ten line program with defined behaviour miscompiles, it is the compiler; if UBSan flags it, it is your code.
- The minimal input becomes the **regression test**.

## 8. Intermittent bugs: make it frequent, then measure

An intermittent failure cannot be debugged until it repeats on demand, and it cannot be declared fixed until enough runs have passed. Both are arithmetic.

**Step one: measure the rate.** Run it many times and count. The program is two threads each adding 1 to a shared counter 3,000 times with no lock, so the total should be 6,000 and sometimes is not (the update `counter++` is a read, an add and a write; two threads can interleave and lose one).

```c
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define N 3000
static long counter;                       /* shared, no lock: the bug */

static void *work(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++) counter++; /* read, add, write: two threads can interleave */
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, work, NULL);
    pthread_create(&b, NULL, work, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    if (counter != 2 * N) { printf("lost %ld updates\n", 2 * N - counter); return 1; }
    return 0;
}
```

```bash
# An intermittent failure: measure how often it happens, then change the conditions to make it happen more.
cd "$(dirname "$0")"
gcc -O1 -pthread -o /tmp/race race.c; gcc -O0 -o /tmp/spin spin.c
RUNS=400
count() { f=0; for i in $(seq $RUNS); do "$@" >/dev/null 2>&1 || f=$((f+1)); done; echo $f; }
echo "--- 1. $RUNS runs, N=3000 increments per thread, 2 vCPUs: how often does it fail?"
printf "unpinned (both threads can run at once): %3d of $RUNS runs failed\n" $(count /tmp/race)
echo "--- 2. the same program pinned to one CPU: the threads never run at the same instant"
printf "taskset -c 0:                            %3d of $RUNS runs failed\n" $(count taskset -c 0 /tmp/race)
echo "--- 3. with a busy loop on each CPU stealing time slices, so a thread is preempted mid-update"
/tmp/spin & P1=$!; /tmp/spin & P2=$!
printf "two competing spinners:                  %3d of $RUNS runs failed\n" $(count /tmp/race)
kill $P1 $P2; wait 2>/dev/null
echo "--- 4. a bigger window: N=300000 per thread"
sed 's/#define N 3000/#define N 300000/' race.c > /tmp/race_big.c; gcc -O1 -pthread -o /tmp/race_big /tmp/race_big.c
printf "unpinned, N=300000:                      %3d of 50 runs failed\n" $(RUNS=50; f=0; for i in $(seq 50); do /tmp/race_big >/dev/null 2>&1 || f=$((f+1)); done; echo $f)
```

```text
--- 1. 400 runs, N=3000 increments per thread, 2 vCPUs: how often does it fail?
unpinned (both threads can run at once):  91 of 400 runs failed
--- 2. the same program pinned to one CPU: the threads never run at the same instant
taskset -c 0:                              0 of 400 runs failed
--- 3. with a busy loop on each CPU stealing time slices, so a thread is preempted mid-update
two competing spinners:                    0 of 400 runs failed
--- 4. a bigger window: N=300000 per thread
unpinned, N=300000:                       48 of 50 runs failed
```

**91 of 400** runs failed (23 %) when the two threads could run at once. Pinned to one CPU (`taskset -c 0`) it failed **0 of 400**: with one CPU the thread that starts first finishes its 3,000 increments within its time slice before the other starts, so the interleaving never happens (inferred from the result; I did not trace the scheduler). That is a diagnostic result: **a failure that disappears when you restrict the program to one CPU is a data race or a visibility problem between CPUs** (DBG 06, CO 19 and CO 20). With a longer loop (300,000 increments per thread) it failed **48 of 50**, because the window for an interleaving is larger: a bug that fails rarely can be made frequent by **widening the window** (more iterations, a `sleep` or `sched_yield` inserted between the read and the write, more threads than CPUs, more load).

One attempt did not help, and is worth recording. I ran two busy loops, one on each CPU, to steal time slices so that a thread would be preempted mid-update: **0 of 400** failed. Preemption at the exact instruction between a read and a write is rare, and the short run finished before the scheduler intervened. "Add load" is a guess; measure whether it moved the rate.

**Step two: know how many runs show a fix.** If a bug fails with probability `p` per run, the chance that `n` runs all pass by luck is `(1 - p)^n`. To be 95 % sure that a clean series means a fix, you need `n = ln(0.05) / ln(1 - p)` clean runs.

| Bug failed | `p` | Clean runs for 95 % | For 99 % |
|---|---|---|---|
| 91 of 400 | 0.23 | 12 | 18 |
| 1 in 10 | 0.1 | 29 | 44 |
| 1 in 100 | 0.01 | 299 | 459 |
| 1 in 1,000 | 0.001 | 2,995 | 4,603 |
| 1 in 10,000 | 0.0001 | 29,956 | 46,050 |

(`ans_a`.) A bug that fails once in a thousand runs and "did not fail in 20 tries after the change" has not been fixed or shown fixed; 20 clean runs has a 98 % chance of happening with the bug still there. Say this in an interview: **a fix for an intermittent bug is a statistical claim and needs a number of runs**. The same arithmetic tells you how long it takes to **catch** the bug: a failure every `1/p` runs on average, so 20 failures to study take about 2,000 runs at 1 in 100.

**Step three: make it frequent and make it deterministic.** In order of effort:

1. **Loop it** under a script with the rate counted; run it on several cores at once.
2. **Change the conditions that matter**: CPU count and pinning (`taskset`), load, `nice`, input size, timing (a sleep in the suspect window), memory pressure, and, for network code, loss and latency (`tc netem`, NET 14).
3. **Use a tool that finds the class**: ThreadSanitizer for races, ASan and Valgrind for memory, a stress option in the test framework.
4. **Record it** so that one failing run can be replayed any number of times: `rr` records a run deterministically and lets you step backwards from the failure (DBG 08). Its chaos mode is a scheduler that makes rare interleavings frequent.

Common causes of intermittent failure, to run through before anything exotic: a race (shared data without a lock or an atomic), an **uninitialised variable** (the garbage differs per run), a dependence on **time** (timeouts, clocks, scheduling), the environment (the user, the directory, an environment variable, the locale), the **address layout** (ASLR changes pointer values, and hash orders keyed on pointers), a **resource limit** (file descriptors, memory), and an external service.

---


## 9. A live process: one command for each symptom

When the process is running and you may not stop or restart it, the same method applies with tools that read it from outside. DBG 07 teaches each in depth; these are the first commands, run on three live processes.

```c
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t A = PTHREAD_MUTEX_INITIALIZER, B = PTHREAD_MUTEX_INITIALIZER;

static void *t1(void *x) { (void)x; pthread_mutex_lock(&A); usleep(100000); pthread_mutex_lock(&B); return 0; }
static void *t2(void *x) { (void)x; pthread_mutex_lock(&B); usleep(100000); pthread_mutex_lock(&A); return 0; }

int main(void) {
    pthread_t a, b;
    pthread_create(&a, 0, t1, 0);
    pthread_create(&b, 0, t2, 0);
    pthread_join(a, 0);
    pthread_join(b, 0);
    return 0;
}
```

```c
int main(void) { volatile unsigned long x = 0; for (;;) x++; }
```

```c
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    for (;;) {
        char *p = malloc(1 << 20);          /* 1 MiB, touched, never freed */
        memset(p, 1, 1 << 20);
        usleep(100000);
    }
}
```

```bash
# The first command for a hang, a busy loop and a leak, on live processes.
cd "$(dirname "$0")"
gcc -g -O0 -pthread -o /tmp/deadlock deadlock.c; gcc -g -O0 -o /tmp/leak leak.c; gcc -g -O0 -o /tmp/spin spin.c
frames() { sed -E 's/^(#[0-9]+) +0x[0-9a-f]+ in /\1 /; s/ \(.*\) at / at /; s/\(.*\)$//' ; }
echo "--- 1. 'it hangs': the process is alive, uses no CPU"
/tmp/deadlock & P=$!; sleep 1
ps -o stat=,pcpu= -p $P | awk '{print "state " $1 ", cpu " $2 "%"}'
echo "main thread wchan: $(cat /proc/$P/wchan)"
echo "threads: $(ls /proc/$P/task | wc -l); states: $(for t in /proc/$P/task/*; do awk '/^State/{printf "%s ", $2}' $t/status; done)"
gdb -q -batch -p $P -ex 'thread apply all bt' 2>&1 | grep -E '^(Thread|#)' \
  | awk '/^Thread/{if(t)print t ": " lib " frames in libc" u; t="Thread " $2; lib=0; u=""; next}
         /deadlock.c/{match($0,/in [a-z0-9_]+ /); f=substr($0,RSTART+3,RLENGTH-4); match($0,/deadlock.c:[0-9]+/); u=u ", then " f " at " substr($0,RSTART,RLENGTH); next}
         {lib++}
         END{print t ": " lib " frames in libc" u}'
kill -9 $P; wait $P 2>/dev/null
echo "--- 2. '100 % CPU': the process is running, so a stack sample shows where"
/tmp/spin & P=$!; sleep 1
ps -o stat=,pcpu= -p $P | awk '{print "state " $1 ", cpu " ($2>50?"above 50":"low") "%"}'
for i in 1 2 3; do gdb -q -batch -p $P -ex 'bt 2' 2>&1 | grep -E '^#' | frames | head -1; done
kill -9 $P; wait $P 2>/dev/null
echo "--- 3. 'memory grows': resident set size, sampled once a second"
/tmp/leak & P=$!
for i in 1 2 3; do sleep 1; awk '/VmRSS/{printf "VmRSS %d MB\n", $2/1024 + 0.5}' /proc/$P/status; done > /tmp/rss.txt
cat /tmp/rss.txt | awk '{print; v[NR]=$2} END{printf "growth per second: about %d MB (expected 10 allocations of 1 MiB)\n", (v[3]-v[1])/2 + 0.5}'
kill -9 $P; wait $P 2>/dev/null
```

```text
--- 1. 'it hangs': the process is alive, uses no CPU
state Sl, cpu 0.0%
main thread wchan: futex_do_wait
threads: 3; states: S S S
Thread 3: 6 frames in libc, then t1 at deadlock.c:6
Thread 2: 6 frames in libc, then t2 at deadlock.c:7
Thread 1: 4 frames in libc, then main at deadlock.c:13
--- 2. '100 % CPU': the process is running, so a stack sample shows where
state R, cpu above 50%
#0 main at spin.c:1
#0 main at spin.c:1
#0 main at spin.c:1
--- 3. 'memory grows': resident set size, sampled once a second
VmRSS 11 MB
VmRSS 21 MB
VmRSS 31 MB
growth per second: about 10 MB (expected 10 allocations of 1 MiB)
```

**A hang.** The process is alive (`Sl`: sleeping, multi-threaded) at **0.0 % CPU**; the main thread's `wchan` is `futex_do_wait`, the kernel function where it sleeps; all **three** threads are in `S`. One `gdb` attach with `thread apply all bt` shows where each is: threads 2 and 3 are inside `pthread_mutex_lock` (six libc frames below the user's code), one in `t1` at `deadlock.c:6` and the other in `t2` at `deadlock.c:7`, which are the **second** lock call of each function; thread 1 (main) is in `pthread_join`. Two threads each hold one mutex and wait for the other's: a **deadlock**, found from one command and without understanding anything else about the program. The fix is a lock order (DBG 06).

**A busy loop.** The state is `R` (running) at high CPU. A stack sample, taken three times, was `main at spin.c:1` every time: a loop in `main`. Repeated samples are a poor man's profiler: if all of them show one place, that is the loop; if they vary, take twenty and count (`perf top` and `perf record` do this properly, LX 10). For a loop that makes system calls, `strace -c -p PID` counts them.

**A leak.** `VmRSS` in `/proc/PID/status`, sampled once a second, grew **11, 21, 31 MB**, which is 10 MB a second, ten allocations of 1 MiB a second at the 100 ms sleep. That a number grows **steadily** says leak; a number that grows with load and falls with it is a cache or a queue. The next tool is an allocation profile or LeakSanitizer (DBG 05).

Three more first commands to know: `lsof -p PID` or `ls /proc/PID/fd | wc -l` for a file descriptor leak (compare with `ulimit -n`), `ss -tanp` for sockets, and `cat /proc/PID/stack` (root) for the kernel stack of a process stuck in the `D` state.

## 10. Interview questions

1. **A service crashes about once a day in production. What do you do?** Method: find what changed and when, collect the evidence (the core file, the log line before the crash, the signal), reproduce, minimise, then use the tool for the symptom. Say you would first make sure that core dumps are enabled and retrievable, because the second crash then costs nothing (DBG 04).
2. **A program works at `-O0` and fails at `-O2`. Why?** Undefined behaviour the optimiser exploits (signed overflow, an out-of-bounds read, an uninitialised variable, aliasing), a missing `volatile` or atomic, a race that the faster code exposes, rarely a compiler bug. First run it under UBSan and ASan with the release flags; then compare assembly (section 3.3).
3. **What is `git bisect` and when does it fail?** A binary search over commits driven by a good and a bad commit; `ceil(log2 n)` tests; automated with `bisect run` and an exit status (125 to skip). It fails when the test is flaky, when the good commit is not good, when there are many unbuildable commits, or when the bug needs two commits (an interaction).
4. **How do you debug a bug you cannot reproduce?** Measure the rate and the conditions, widen the window, change CPU count and load, use ThreadSanitizer or a record tool, and add logging that captures the state at the failure; and be honest that a fix is only shown by a number of clean runs (section 8).
5. **How do you know a fix for an intermittent bug works?** `n = ln(0.05) / ln(1 - p)` clean runs for 95 % confidence: 299 for a 1-in-100 bug.
6. **What is `<optimized out>` and what do you do about it?** The value is not stored where the debugger can name it at that instruction; rebuild the file at `-Og` or `-O0`, look in the caller, read a register, or force the value to be kept.
7. **What does `-g` change in the binary?** It adds DWARF sections (line table, entries for functions, variables and types) that are not loaded at run time and do not change the generated code (section 4).
8. **When is `printf` better than `gdb`?** When you cannot attach, when timing matters, when the history is the evidence, or when the target has no debugger; `gdb` when the data is large or you need to change state (section 5.3).
9. **What makes a good assertion? A good log line?** An assertion checks a programmer's invariant, has no side effects and says what it means; a log line says where, what, with which arguments and why it failed (sections 5.1 and 5.2).
10. **What is delta debugging?** An automatic search that shrinks a failing input to a minimal one that still fails (`ddmin`), by testing chunks and complements and splitting finer; it needs a fast, reliable test that matches the specific symptom (section 7).
11. **Tell me about the hardest bug you fixed.** Structure it as the method: the symptom and its impact; how you made it repeat; how you narrowed it (what you measured, which hypotheses you ruled out and by what experiment); the cause; the fix and how you proved it; what you changed so it cannot return (a test, an assertion, a tool in CI). Prepare one real example from your own work, with the numbers.

## Common mistakes

- **Starting with a theory and looking only for evidence for it.** Look for what would **disprove** it.
- **Changing several things between runs.**
- **Fixing the place the crash was reported.** The faulting line is where the damage was found, not where it was done.
- **Believing a fix that was tried a few times.** An intermittent bug needs the arithmetic of section 8.
- **Debugging the wrong binary.** Check the `build-id`, the build date or a printed version.
- **Putting a side effect inside an `assert`.** The release build runs a different program.
- **Debug information only on some builds.** Keep the unstripped files (or separate debug files) for every release; a crash without them is bare addresses.
- **Testing for overflow after it happened.** `a + b < a` is undefined for signed integers; check before adding, or use `__builtin_add_overflow`.
- **Blaming the compiler.** Prove it with a minimal, defined program, or find the undefined behaviour first.
- **Leaving the evidence behind.** No regression test, and a commit message that says "fix crash".

---

## 11. Problems

**Problem A. Bisect arithmetic.** A regression appeared somewhere in 1,000 commits. (a) How many test runs at most does `git bisect` need? (b) Each test takes 20 minutes (a full build and a test run); how long is the search compared with testing every commit? (c) 7 of the commits do not build and the test returns 125 for them; can the answer change? *Solve: `ceil(log2 n)`; multiply; think about what a skip does to the search.*

**Problem B. How many runs?** A bug failed 3 times in 500 runs before a change, and 0 times in 500 runs after. (a) What is the failure rate before? (b) How sure can you be that the change fixed it, and how many clean runs would you need for 95 %? (c) The same for a bug that failed 3 times in 5,000. *Solve: `p = 3/500`; `(1 - p)^500`; `n = ln(0.05) / ln(1 - p)`.*

**Problem C. Delta debugging cost.** A failing input is 100,000 bytes. (a) About how many runs does `ddmin` take at best and at worst? (b) The test takes 0.5 s; is the worst case acceptable? (c) What do you do if it is not? *Solve: `2 log2 n` and `n²`; think about a structured reducer and a faster test.*

**Problem D. Choose the first tool.** For each symptom name the first command and what you expect to read: (1) a daemon uses 100 % of one core after a config reload; (2) a program prints the wrong total only in the release build; (3) a service answers nothing, `top` shows 0 % CPU; (4) a build agent runs out of disk every two days; (5) a test fails 1 time in 200 on CI and never on your laptop.

**Problem E. Read the session.** A colleague pastes this `gdb` output from an `-O2 -g` build and says "the debugger is broken, it cannot show `len`": `#0  process (buf=<optimized out>, len=<optimized out>) at net.c:88`, `#1  0x... in handle (conn=0x5555555a3f40) at net.c:131`, `$1 = (struct conn *) 0x5555555a3f40`. What is happening, and list four things you would try, in order.

## Answers

**A.** (a) `ceil(log2 1000)` = **10** runs. (b) 10 × 20 min = **200 minutes**, about 3.3 hours, against 1,000 × 20 min = 333 hours (14 days) for one at a time. (c) The skipped commits can only widen the answer: if the first bad commit is among the commits that do not build, bisect cannot tell which of them it is, and names a **range** instead of one commit ("the first bad commit is one of these"); then look at the changes in that range by hand.

**B.** (a) `p = 3/500 = 0.006`. (b) `(1 - 0.006)^500 = 0.049`: five runs in a hundred would be clean by chance if the bug were still there, so a clean 500 is **just about 95 % sure** (`n = ln 0.05 / ln 0.994 = 498`). (c) With 3 in 5,000, `p = 0.0006`, `n = ln 0.05 / ln(1 - 0.0006) = 4,992`, so about **5,000 clean runs**; a rarer bug needs proportionally more runs. The numbers come from `ans_a`'s formula; the lesson is that "no failure in 500 runs" is evidence for a 0.6 % bug and almost none for a 0.06 % one.

**C.** (a) Best about `2 × 17 = 34` runs (`ceil(log2 100000) = 17`), worst about `n² = 10^10` (`ans_a`). In practice a failure that is caused by a small number of bytes lands near the best case: our 2,000 bytes with a 3 byte cause took 180 runs, which is 8 times the best case and 0.005 % of the worst. (b) At 0.5 s and a few hundred runs it is minutes; the worst case would be centuries, so never plan for it. (c) Use a reducer that knows the structure (`cvise` for source, a format-aware tool), make the test faster (run it in-process, cache the build, a smaller harness), and run candidates in parallel.

**D.** (1) A busy loop after a reload: a few stack samples (`gdb -p PID -batch -ex bt`, or `perf top -p PID`); expect one place repeated, probably a retry or spin on a state the reload changed; `strace -c -p` if it makes system calls. (2) Wrong total only in release: rebuild with the release flags plus `-fsanitize=undefined,address`, and run; expect an overflow, uninitialised or out-of-bounds report; compare `-O0` and `-O2` output (section 3.3). (3) No answer and no CPU: `thread apply all bt` with `gdb -p`, and `/proc/PID/wchan`; expect threads blocked in a lock, a `read` or a futex; with all in locks, a deadlock (section 9). (4) Disk fills every two days: `du -x --max-depth=2` sorted, and `lsof +L1` for deleted files a process still holds open; expect a log or a file kept open after deletion. (5) One in 200 on CI only: measure the rate under a loop; change the CPU count (`taskset`), run with ThreadSanitizer, and compare the environment (CPU count, timeouts, load, locale); expect a race or a timing dependence (section 8).

**E.** The release build has optimised `buf` and `len` away at this instruction, so the debugger is working as designed: `<optimized out>` means that the value is not held in a place it can name at this address. The caller's `conn` is intact, which shows that the frame information is fine. Try, in order: (1) look in the **caller's frame** (`up`) for the values passed in; `conn` may hold the length. (2) **Rebuild only `net.c` at `-Og`** (or `-O0`) and reproduce. (3) **Move up and down the function** (`next`, or a breakpoint a few lines earlier or later): the value may be live in a register at a different instruction, and `info registers` shows it. (4) Add a temporary read through a `volatile` pointer or a log line to keep the value, then check the bug still occurs. The wrong move is to conclude that the debugger is broken, or to rebuild everything at `-O0` and lose the bug.

---

## Cheat sheet

```text
METHOD       reproduce -> minimise -> hypothesise (a prediction) -> test one change -> fix the cause -> prove -> keep a test
FIRST TOOL   crash: core + gdb | hang: gdb -p, thread apply all bt | 100% CPU: stack samples | wrong output: ubsan/asan, break where it differs
             leak: VmRSS over time | slow: perf | intermittent: loop it, count, widen the window | release only: ubsan + compare -O0/-O2
BUILD        -g (debug info, no code change) | -g3 (macros, 17x bigger here) | -Og (debuggable) | -O2 -g (what ships) | -fno-omit-frame-pointer
OPTIMIZED OUT  rebuild that file at -Og | look in the caller | info registers | force the value to stay (volatile)
ASSERT       programmer errors only | no side effects (NDEBUG removes it) | say what it means
LOG          where, what, arguments, errno and text, request id | once at the point of failure | levels switchable at run time
BISECT       log2(n) runs | git bisect run ./test.sh | exit 0 good, 1..127 bad, 125 skip | check the end points, fix flakiness
DDMIN        shrink the failing input to 1-minimal | test must match the symptom | cvise for source | the result is the regression test
INTERMITTENT measure p | clean runs for 95%: ln(0.05)/ln(1-p) | 1 in 100 -> 299 | taskset to one CPU: vanishes -> race
LIVE         ps stat, cpu | /proc/PID/{wchan,stack,fd,status} | gdb -p PID -batch -ex 'thread apply all bt' | ls /proc/PID/fd | wc -l
DWARF        line table (address -> line) + entries (functions, variables, types, locations) | addr2line -f -e BIN ADDR
```

## Sources

**Read or run in this session:** every number tagged in the text comes from the scripts in `prog/` and their output in `v/`. **From memory** (marked where used): the history and published form of delta debugging (Zeller and Hildebrandt), `cvise` and its behaviour, `afl-tmin` and `libFuzzer -minimize_crash`, the status of frame pointers in Fedora and Ubuntu, the `git bisect run` exit status rules as the manual page states them (checked against behaviour here only for 0, 1 and 125), `tc netem`, and the LLDB equivalents. No manual page was re-read for this tutorial.
