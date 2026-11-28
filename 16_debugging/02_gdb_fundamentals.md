# DBG 02: gdb fundamentals

*Part of the [debugging plan](00_plan.md) · You need: [DBG 01](01_method_and_toolbox.md) (the method, building with `-g`, what the line table is), [C 01](../02_c_language/01_types_conversions_and_undefined_behavior.md) (types and undefined behaviour) and [CO 07](../04_computer_organization/00_plan.md) (registers and stack frames).*

Most engineers know three gdb commands (`break`, `run`, `print`) and use them like a print statement with extra steps. A staff-level interview asks what the debugger is **doing** and whether you can steer it quickly: "the program crashes in a function called from a loop, only on the 40,000th iteration, with a pointer that looks fine. Show me what you type." A weak answer is a list of commands. A strong answer is a plan: "stop only when the condition holds, so a conditional breakpoint on `i == 39999`; look at the pointer and the object it points to with `print` and `x`; step to the line that dereferences it; walk up one frame to see which caller passed the value; and if the variable says `<optimized out>`, say why and rebuild with `-O0` or read the register".

This tutorial teaches the commands you use every day, in the order a session needs them: start, stop, move, look, walk the stack. It then explains the one thing that confuses everybody (`<optimized out>`), gives the LLDB equivalents for the Mac, and ends with interview questions and scenarios. Every command shown was run for this tutorial on the same small program, and its output is what you see.

By the end you can:

- start a program under gdb with arguments, and run it in a script with `-batch`;
- set breakpoints by function, line and address, make them conditional, temporary or counted, and say how a breakpoint works inside the process;
- move with `step`, `next`, `finish`, `until`, `advance` and the instruction-level `stepi`, and say which one to use;
- look at data with `print` (formats, arrays, structs, expressions), `display`, `x` (memory), `info locals` and `ptype`, and change data with `set var` and `call`;
- read a backtrace, move between frames, and explain what `finish` does to the selected frame;
- explain why a variable shows `<optimized out>`, how to see it anyway, and which build to debug with;
- write a `.gdbinit` and a gdb script, and translate the commands to LLDB.

---

## 1. What gdb is doing

A debugger is two things stacked together.

- **Control of another process.** On Linux, gdb uses the `ptrace` system call to start or attach to the program (the **inferior**), to stop it, to read and write its memory and registers, and to resume it. The program runs as an ordinary process that gdb is allowed to freeze.
- **Understanding of the program's source.** The compiler, when given `-g`, writes **DWARF** debug information into the binary ([DBG 01](01_method_and_toolbox.md#4-what--g-writes-the-line-table-and-the-entries)): a line table (address to source line), and descriptions of functions, variables, types and where each variable lives. gdb reads it so you can say `break inv.c:10` and `print v[i].price` instead of addresses and bytes.

A **breakpoint** is implemented by writing a special one-byte instruction (`int3` on x86, byte `0xCC`) over the first byte of the target instruction. When the CPU reaches it, it raises a trap, the kernel stops the process and tells gdb, and gdb puts the original byte back for display (so `x/i` shows the real instruction, not the trap). To continue, gdb steps over the original instruction and then re-inserts the trap. A **software breakpoint** therefore needs writable code memory and costs nothing until it is hit. A **hardware breakpoint** (`hbreak`) uses the CPU's few debug registers instead, which works in read-only memory (flash) and costs nothing in code, but there are only a handful (from memory: four on x86). These mechanics are the reason a breakpoint can be set in a function before the program runs at all: gdb knows the address from the debug information, and inserts the trap when the process starts.

Two consequences matter in practice. First, **gdb can only show what the debug information describes**, so a binary without `-g` gives you addresses and registers but no variables ([DBG 04](00_plan.md) covers stripped binaries and separate debug files). Second, **the debugger sees the program as the compiler built it**, so an optimised build can legitimately have no place to hold a variable you wrote (section 8).

---

## 2. The test program and the first session

All examples use one small program, `inv.c`, an inventory with a struct array, a function that sums it, and a function that searches it. It is short enough to read at once, and has loops, a struct, an array, a string and function calls, which is what real debugging touches.

```c
struct item { char name[16]; int qty; double price; };

static double total(const struct item *v, int n) {
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double line = v[i].qty * v[i].price;
        sum += line;
    }
    return sum;
}

static int find(const struct item *v, int n, const char *name) {
    for (int i = 0; i < n; i++)
        if (strcmp(v[i].name, name) == 0) return i;
    return -1;
}
int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 3;
    struct item *stock = calloc(n, sizeof *stock);
    for (int i = 0; i < n; i++) {
        snprintf(stock[i].name, sizeof stock[i].name, "part-%d", i);
        stock[i].qty = i + 2;
        stock[i].price = 1.5 * (i + 1);
    }
    int at = find(stock, n, "part-1");
    printf("part-1 at %d, total %.2f\n", at, total(stock, n));
    free(stock);
    return 0;
}   /* plus #include <stdio.h>, <stdlib.h> and <string.h> at the top, 34 lines in all */
```

Build it for debugging and start it:

```bash
gcc -g -O0 -o inv0 inv.c        # -g: debug info; -O0: no optimisation, so variables are where you expect
gdb ./inv0                      # then, at the (gdb) prompt:  run 4
gdb --args ./inv0 4             # same, with the argument given on the command line
gdb -q -batch -x session.gdb --args ./inv0 4    # no prompt: run the script, print, exit
```

Starting variants worth knowing:

| Command | Effect |
|---|---|
| `run ARGS` (or `r`) | Start the program with those arguments, and redirections such as `run < input.txt` work |
| `set args ...` and `show args` | Set or read the arguments for the **next** run (it does not change a program that is already running) |
| `start` | Set a temporary breakpoint at `main` and run, so you stop at its first line |
| `starti` | Stop at the very first instruction, before `main`, before the C library has initialised (in the lab, in the dynamic loader's `_start`) |
| `-batch -x FILE` | Run commands from a file without a prompt, which is how you turn a debugging session into a script |
| `-q` | Skip the start-up banner |

A first session, with the output that appeared:

```text
(gdb) break total
Breakpoint 1 at 0x11f8: file inv.c, line 8.
(gdb) run 4
Breakpoint 1, total (v=0x5555555592a0, n=4) at inv.c:8
8        double sum = 0;
(gdb) bt
#0  total (v=0x5555555592a0, n=4) at inv.c:8
#1  0x00005555555553e2 in main (argc=2, argv=0x7fffffff6208) at inv.c:31
(gdb) info locals
sum = 4.6355705390658865e-310
```

Read it carefully, because it contains three of the lessons of the tutorial. The stop line says **which breakpoint, which function with its argument values, and which source line is next to run** (line 8 has not executed yet). The backtrace shows the call chain: `total` was called from `main` at line 31. And `sum` shows a **garbage value** (a tiny denormal number), because line 8 has not run yet and a local variable holds whatever was on the stack. **gdb always stops before a line, never after it**, so a variable you expect to be set by the line you are stopped on is not set yet.

---

## 3. Breakpoints

### 3.1 Where to stop

```text
break total             function: stop at its first line, after the prologue
break inv.c:10          file and line (the file name is optional when there is one file)
break 10                line in the current file
break *0x55555555520a   an exact address, even in code with no debug information
tbreak find             temporary: deleted automatically the first time it is hit
rbreak ^fin             a breakpoint on every function whose name matches the regular expression
hbreak total            a hardware breakpoint (limited number, works in flash and ROM)
```

`info breakpoints` lists them with their numbers, whether they are enabled, the condition, and **how many times each was hit**, and `delete N`, `disable N`, `enable N` manage them. The hit count is a quiet diagnostic: a breakpoint that was never hit tells you the code was never reached (or its line has no code, or the file is not the one you think).

The [manual](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/Set-Breaks.html) names the idea behind the `N.M` numbers you see in output: a location spec can resolve to **several** code locations (an inlined function, a C++ template, an overload), each of which is a separate location of one breakpoint. That matters at `-O2`, where an inlined function has a copy per call site, and `break total` may stop at a different place than you expect.

If the symbol is not known yet (a function in a shared library that has not loaded), gdb offers a **pending breakpoint** that resolves when the library loads (`set breakpoint pending on` makes that automatic).

### 3.2 Stop only when it matters

A breakpoint inside a loop is useless if it stops on every iteration. Three tools fix that.

```text
break inv.c:10 if i == 2      condition: stop only when the expression is true
condition 1 i == 3            change or add the condition on breakpoint 1
ignore 1 2                    ignore the next 2 hits of breakpoint 1, then stop
```

In the lab, a breakpoint on line 10 (the loop body) with `ignore 1 2` skipped the first two iterations and stopped with `i = 2`; then `condition 1 i == 3` made the next stop `i = 3`; `info breakpoints` afterwards reported the condition and `already hit 4 times`. Note that the hit count includes the hits that were ignored or whose condition was false, which is a frequent source of confusion.

A condition is evaluated **by gdb, inside the debugger**, on every hit of the location: the process stops, gdb evaluates, and resumes if false. That is cheap for a few hundred hits and **very slow for millions**, because each hit is a round trip between two processes. For a hot location, a condition on a rarely changing value is still fine, but a better tool for "the 40,000th iteration" is `ignore 1 39999`, which is a counter and no expression, or putting the check inside the program as a temporary `if (i == 39999) raise(SIGTRAP);` (a stop you control without the debugger's cost). A condition can call functions in the program, and can use strings (`$_streq(name, "x")` is the gdb way, from memory, and `strcmp` works if the program has it).

---

## 4. Moving through the program

Once stopped, you move. The commands differ in how they treat function calls and loops.

| Command | What it does | Use it when |
|---|---|---|
| `next` (`n`) | Run to the next source line, **stepping over** calls | You trust the callees |
| `step` (`s`) | Run to the next source line, **stepping into** calls that have debug info | You want to follow the call |
| `finish` | Run until the **selected frame** returns, then print the returned value | You went into the wrong function |
| `until` (`u`) | Like `next`, but never goes **backwards** in a loop | You are at the end of a loop body |
| `until LOCATION` / `advance LOCATION` | Run to that location, or until the current frame returns | You want to skip ahead |
| `continue` (`c`) | Run until the next breakpoint | |
| `stepi` (`si`), `nexti` (`ni`) | One **machine instruction** (into or over a call) | You are below source level |

What the lab showed:

- `finish` stopped in the caller's line and printed **`Value returned is $7 = 60`**, the return value, saved as a history item you can use again (`print $7 * 2`). The stop is in the middle of the caller's line 31 (the address shown has an offset), because the call was inside the `printf`'s argument list and that line is not finished.
- **`finish` finishes the selected frame, not the innermost.** After moving up a frame with `up` or `frame 1`, `finish` runs until *that* frame returns, which can skip a lot. In the lab the same thing happened in LLDB: selecting frame 1 and typing `finish` ran `main` to completion, and the program printed its output. Check `frame` before you `finish`.
- `until` at the end of a loop body went to the loop's increment line (9), and the next `until` ran **through the rest of the loop** and stopped at `return` on line 13, where `next` would have walked the iterations one by one.
- A `step` into a function with no debug information (a library call) behaves like `next`, so `step` does not wander into `printf`. For a function you do want to enter without debug info, use `stepi` until the call and look at the registers.

`next` on a line like `for (int i = 0; i < n; i++)` visits it **twice per iteration** (the condition, then the increment), which looks like a repeated line and is correct.

---

## 5. Looking at data

### 5.1 `print`

`print EXPR` (or `p`) evaluates a C expression in the context of the **selected frame** and shows the result. Every result is stored as `$1`, `$2` and so on (the **value history**), and `$` is the latest, so you can reuse them. Useful forms, with what the lab printed:

```text
print sum                     the variable
print v[1].name               "part-1" (a char array prints as a string, with \000 padding)
print v[0]                    {name = "part-0\000...", qty = 2, price = 1.5}
print *v@3                    the three items starting at v   (@ makes an array view, so a pointer prints as several elements)
print/x v[1].qty             0x3  (format letters: x hex, d decimal, o octal, t binary, c char, f float, a address, s string)
print/c 65                   65 'A'
print sum * 2                an expression, evaluated by gdb
print sizeof(struct item)   32
print &stock[1]              (struct item *) 0x5555555592c0
print &stock[1].qty          (int *) 0x5555555592d0
```

Two settings make big structures readable: `set print pretty on` prints one field per line, and `set print elements N` limits how many elements or characters of an array or string are shown (the default is 200, so a long buffer is cut with `...`).

Because gdb evaluates in the program's context, you can **change** the program as well as read it:

```text
set var sum = 100              assign (the lab then saw sum = 100 in the backtrace output)
print v[i].qty = 7             the same, as an expression
call find(v, n, "part-3")      call a function in the program: prints 3 in the lab
print find(v, n, "nope")       -1
```

Calling a function runs **real code in the stopped process**, so it can hit another breakpoint, deadlock if it takes a lock the stopped thread holds, or corrupt state. It is powerful for checking a hypothesis ("is this list sorted?" with a helper) and risky on a live system.

### 5.2 `display` and the value history

`display EXPR` prints the expression **after every stop**, so you watch a value while stepping, and `display/i $pc` shows the next instruction each time. `undisplay N` removes one. In the lab, `display sum` followed by `next` showed `1: sum = 3` and then the same after the next line, which is the loop's effect visible with no typing.

### 5.3 `x`: examine memory

`print` understands types. `x` looks at **raw memory**: `x/NFU ADDRESS`, where N is the count, F the format and U the unit size.

```text
x/s v[i].name       "part-2"                      a string
x/8xb v             0x70 0x61 0x72 0x74 0x2d 0x30 0x00 0x00     8 bytes in hex ("part-0")
x/3dw &v[0].qty     2 0 0                          3 words (4 bytes) as decimal
x/2i $pc            => 0x55555555520a <total+33>: mov -0x14(%rbp),%eax    two instructions, the arrow marks the next one
x/4dw $sp           four words from the stack pointer
```

Format letters are the `print` ones plus `i` for instructions, and unit letters are `b` (1 byte), `h` (2), `w` (4) and `g` (8). Use `x` when `print` cannot help: memory with no type, a buffer's real bytes (to see the padding and the terminator), the instruction at the program counter, or the stack. In the output above the struct's first bytes `70 61 72 74 2d 30` are the ASCII for `part-0`.

### 5.4 Types: `ptype` and `whatis`

`whatis EXPR` gives the type's name, and `ptype EXPR` expands it to the full definition (`ptype v` gave the `const struct item *` and the fields). `ptype/o struct item` adds **offsets and holes**, which answers layout questions directly:

```text
/* offset | size */  type = struct item {
/*   0    |   16 */    char name[16];
/*  16    |    4 */    int qty;
/* XXX  4-byte hole */
/*  24    |    8 */    double price;
                       /* total size (bytes): 32 */ }
```

(Abridged from the lab output.) That is the answer to "why is this struct 32 bytes and not 28", which is a common interview question and a source of bugs in code that writes structures to disk or the network.

### 5.5 Registers and convenience variables

`info registers` (or `info registers rip rsp`) shows registers, `print $rip`, `$sp` and `$pc` read them, and you can write them (`set $rax = 0`). **Convenience variables** start with `$` and hold anything: `set $count = 0`, `print $bpnum` (the number of the last breakpoint), and `$_exitcode`, which is `void` while the program runs and holds the exit status afterwards (the lab printed `0` after a normal exit). They are what breakpoint scripts use for counters (DBG 03).

---

## 6. The stack: backtrace and frames

Every active function call has a **frame** on the stack, holding its arguments, locals and return address. `backtrace` (`bt`) lists them from the innermost (frame 0) outwards:

```text
#0  total (v=0x5555555592a0, n=4) at inv.c:10
#1  0x00005555555553e2 in main (argc=2, argv=0x7fffffff61e8) at inv.c:31
```

Frame 0 is where the program is stopped. The address shown for the outer frames (`0x...53e2`) is the **return address**, the instruction after the call, which is why those frames point into the middle of a line. In a call chain several frames deep, this is the first thing to read after a crash: it says how the program got here.

| Command | Use |
|---|---|
| `bt`, `bt 5`, `bt -5` | The whole stack, the 5 innermost, the 5 outermost (useful when the stack is thousands deep) |
| `bt full` | Every frame **with its local variables**, in one dump (the lab printed `line = 9`, `i = 2`, `sum = 100` for frame 0, then main's `n`, `stock`, `at`) |
| `frame N`, `up`, `down` | Select a frame; `print` and `info locals` now use **that frame's** variables |
| `info frame` | The selected frame's addresses, saved registers and the caller (`called by frame at ...`) |
| `info args`, `info locals` | Arguments and locals of the selected frame |

The point of moving up is that a bug is rarely where the crash is. A function crashes on a null pointer; the question is who passed it. `up` to the caller, `print` the variable it passed, and `up` again if needed. Remember that **`finish` acts on the selected frame** (section 4).

`info locals` lists variables in scope, and a variable declared in an inner block (the loop's `i` and `line`) appears only while the program is inside the block. At the first line of `total`, `i` was `No symbol "i" in current context` in the lab, because the loop had not started and `i` is declared in the `for`; once inside, it is visible. That is scope, working as in C.

---

## 7. Making gdb comfortable

### 7.1 Source views

`list` shows source around the current line (the lab: lines 5 to 14 around line 10), `list 20,24` a range, `list find` a function. `info line find` gives the address range of a line (`Line 16 ... starts at address ... <find>`), `info source` tells you the file, the compiler producing the debug information (`GNU C17 13.3.0 ... -g -O0 ...`), and the DWARF version. That `Producer` line is an excellent first check when "the variables look wrong": it tells you whether the binary was built with `-O2`. `disassemble /s find` interleaves source lines with the machine code, so you see what each line became, and `info symbol ADDR` and `info address NAME` map between names and addresses.

### 7.2 The text user interface

`gdb -tui` (or `Ctrl-x a` inside a session) splits the terminal: a source window above, the command line below, with the current line highlighted. `layout src`, `layout asm` and `layout regs` choose what is shown, and `Ctrl-x o` switches the active window. It is an alternative to repeating `list`. It was not run here (the lab runs in batch mode, with no terminal), so the key bindings are from memory. For a richer view people use an IDE front end, or extensions such as `gdb-dashboard` and `pwndbg`, which are separate projects.

### 7.3 `.gdbinit` and scripts

gdb reads `~/.gdbinit` at start-up, and a `.gdbinit` in the current directory too if you allow it (for safety it is blocked unless the path is in `auto-load safe-path`, which is why the lab loaded its file explicitly with `-x`). Typical contents:

```text
set pagination off              # do not stop at "--Type <RET> for more--", needed in scripts
set confirm off                 # do not ask "are you sure"
set print pretty on             # structures one field per line
set history save on             # keep the command history across sessions

define pitem                    # a user-defined command
  print $arg0
end
document pitem
Print one struct item.
end
```

In the lab, loading that file and typing `pitem v[0]` at a stop printed the structure in pretty form. A `define` is a small macro: it is the way to wrap a repeated inspection (walk a linked list, print a nested structure) before you reach for the Python API (DBG 03 mentions it). Scripts are the same commands in a file, run with `-batch -x`, which is how to turn "I typed this ten times" into something you can attach to a bug report. Remember `set pagination off`, or a script waits forever for a keypress.

---

## 8. Why a variable is `<optimized out>`

This is the most common frustration, and understanding it is a mark of seniority. Build the same program with `-O2` and stop in `total`:

```text
Breakpoint 1, total (n=<optimized out>, v=0x5555555592a0) at inv.c:9
(gdb) info locals
sum = 0
(gdb) next ... next
(gdb) info locals
line = 9
i = <optimized out>
sum = 3
(gdb) bt
#0  total (n=<optimized out>, v=0x5555555592a0) at inv.c:9
#1  main (argc=<optimized out>, argv=<optimized out>) at inv.c:31
```

Four things changed from the `-O0` session, and each has a cause.

1. **Frame 1 has no return address and the frames are merged.** `info frame` reported `inlined into frame 1`. The compiler **inlined** `total` into `main`, so there is no separate call; gdb reconstructs a virtual frame from the debug information. The function's breakpoint also moved (line 9, not 8).
2. **`n` and `i` are `<optimized out>`.** The compiler is free to keep a variable in a register, keep it only for part of its life, compute it from something else, or never materialise it at all (a loop counter turned into a pointer comparison, a constant folded away). DWARF describes this with **location lists**: for each range of addresses, where the variable is. Outside its ranges, or where it never exists, gdb has nothing to show, and says so.
3. **Statements are reordered and merged.** The `next` commands jumped from line 11 to 9 to 11, and a line can be visited out of order, or skipped, because the compiler scheduled the instructions across lines.
4. **Values are right sometimes and not others.** `sum` and `line` printed correctly where they happened to be in a register, and the same variable can be missing a few instructions later.

You can see the information that exists. `info scope total` on the `-O2 -fno-inline` build printed, for the variable `n`, a **multi-location** description: "in `$rsi`" for one address range, then "a complex DWARF expression" using `DW_OP_entry_value` (meaning: the value that register had when the function was entered) for the next, then "in `$rsi`" again. The loop counter `i` was described as a constant for one range and as an arithmetic expression on a pointer for others, which is why it printed `<optimized out>` when the program was between ranges. This output is the honest answer to "why can't gdb show me `i`": the compiler never kept `i` as a number there.

What to do, in order of preference:

| Remedy | What it does | Cost |
|---|---|---|
| **Debug a `-O0 -g` build** | Every variable has a stable home | The bug may not reproduce, because timing and undefined behaviour change |
| **`-Og -g`** | Optimises only what does not hurt debugging, the intended debugging level | Not perfect: compiler developers report that `-Og` can still leave parameters unavailable after calls ([a GCC report](https://gcc.gnu.org/pipermail/gcc-bugs/2024-February/855494.html)), so it is better than `-O2`, not as reliable as `-O0` |
| **`-O2 -g -fno-inline`**, or `__attribute__((noinline))` on one function | Keeps the call and its frame | The lab's `-fno-inline` build showed `v=v@entry=...` and `n=n@entry=4`: the `@entry` form means gdb recovered the **value at function entry** |
| **Read the registers** at a stop, or use `finish` to see the return value | The value may still be in a register (the lab read `$xmm0.v2_double` for a `double`) | Needs the calling convention in your head (CO 07) |
| **Add `-fno-omit-frame-pointer`** | Keeps frame pointers, so stack traces and profilers work ([DBG 01](01_method_and_toolbox.md#32-the-frame-pointer)) | One register |
| **A temporary `volatile` read** or `printf` in the code | Forces the value to exist | Changes the code you are debugging |

The rule for interviews: **a bug that appears only at `-O2` is a hint of undefined behaviour, a race or an uninitialised variable**, not a gdb problem, and the debugging build changes the very thing you are looking for. So reproduce first (DBG 01), then choose the weakest change of build that keeps the bug and gives you the variable.

---

## 9. The Mac: LLDB equivalents

On macOS the default debugger is LLDB, and the commands are different in spelling and similar in idea. The official [GDB to LLDB command map](https://lldb.llvm.org/use/map.html) lists them all; the table has the ones this tutorial uses, and the forms for breakpoints, conditions, `next`, `finish`, `bt`, `frame variable`, `expr`, `memory read`, `register read`, `frame select` and `breakpoint list` were run here (LLDB 18.1.3, on Linux, so macOS output can differ in detail). The rest follow the official map and were not run.

| Task | gdb | LLDB |
|---|---|---|
| Break at function | `break total` | `breakpoint set --name total` (or `b total`) |
| Break at line | `break inv.c:10` | `breakpoint set --file inv.c --line 10` (or `b inv.c:10`) |
| Conditional | `break total if n == 4` | `breakpoint set --name total --condition 'n == 4'` |
| Run with arguments | `run 4` | `run 4` (or `process launch -- 4`) |
| Step over, into, out | `next`, `step`, `finish` | `next`, `step`, `finish` |
| Print | `print v[0]` | `expr -- *v` or `print v[0]` (`expr` evaluates, `frame variable` reads) |
| Locals and arguments | `info locals`, `info args` | `frame variable` |
| Backtrace | `bt` | `bt` |
| Select a frame | `frame 1`, `up` | `frame select 1`, `up` |
| Examine memory | `x/4dw v` | `memory read -c 4 -f d -s 4 v` |
| Registers | `info registers rip` | `register read rip` |
| Type | `ptype struct item` | `image lookup --type item` |
| List breakpoints | `info breakpoints` | `breakpoint list` |

Two habits transfer and one differs. `finish` **also acts on the selected frame** in LLDB (selecting frame 1 and typing `finish` ran `main` to its end, as in gdb). A condition is checked by the debugger in both. What differs is the **output**: LLDB prints the source with a caret at the position and the thread's stop reason, and it formats values as `(type) name = value`, which is more verbose. On the Mac, system integrity protection and code signing affect what can be debugged (attaching to a system process is blocked, and a binary needs the debugging entitlement), which is an environment fact, not an LLDB limitation (from memory).

---

## 10. Common mistakes

- **Expecting a variable to be set before its line runs.** gdb stops *before* the line. Step once, then print.
- **Printing a pointer and stopping there.** `print p` shows an address. Use `print *p`, `print p->field`, or `print *p@n` for an array.
- **Debugging an optimised binary and trusting `info locals`.** Check `info source` for the `Producer` line first. If it says `-O2`, `<optimized out>` and out-of-order steps are expected.
- **A breakpoint that never hits.** Wrong file, a line with no code (a comment or a declaration), code compiled out, or the build not matching the source. `info breakpoints` shows the hit count, and `info line N` shows whether the line has code.
- **A condition on a hot path.** Every hit stops the process. Use `ignore N COUNT`, or an `if` in the program.
- **Confusing `finish` with `continue`**, and forgetting that `finish` ends the **selected** frame.
- **Calling a function in the program from gdb while it holds a lock** or is in the middle of an update.
- **Debugging a different binary from the one that crashed**, or source that was edited since the build. gdb warns about source newer than the executable. Rebuild, and use the build-id ([DBG 04](00_plan.md)).
- **Forgetting `set pagination off` in a script**, so it hangs waiting for a key.
- **Using `-O0` and concluding the bug is gone.** It may only have moved (a race, an uninitialised read, undefined behaviour). Keep a failing optimised build.

---

## 11. Interview questions

**Q1. How does a breakpoint work?**
gdb replaces the first byte of the target instruction with a trap instruction (`int3`, `0xCC` on x86). When the CPU executes it, it traps into the kernel, which stops the process and notifies gdb through `ptrace`. gdb shows you the original instruction, and to continue it restores the byte, single-steps over the instruction, and re-inserts the trap. A hardware breakpoint uses a debug register instead and works in read-only memory, but there are few of them.

**Q2. `step`, `next`, `finish`, `until`, `advance`: when do you use which?**
`next` to walk the current function, `step` to enter a call, `finish` to leave the current function and read its return value, `until` to get out of a loop without walking every iteration, `advance` or `until LOCATION` to run to a place. And `stepi` when I am at instruction level.

**Q3. The program crashes on the 40,000th iteration of a loop. How do you stop there?**
`break file:line` with `ignore N 39999` (a counter, cheap), or a condition on the loop variable (evaluated on every hit, so slow for a hot loop), or a temporary check in the code that raises `SIGTRAP`. Then step from there.

**Q4. A variable shows `<optimized out>`. What does it mean and what do you do?**
The compiler had no place that holds the value at this address (register reuse, inlining, folded constants), so the debug information has no location for it, and gdb says so. Check the build with `info source`. Then rebuild with `-O0` or `-Og`, or `-fno-inline` for the one function, or read the value from a register, or use the `@entry` value gdb recovered. And treat "only at `-O2`" as a sign of undefined behaviour or a race.

**Q5. What does `finish` do, and what is a common trap?**
It runs until the **selected** frame returns and prints the value returned. If you moved up a frame first, it finishes that outer frame, which may run a long way. Check `frame` before `finish`.

**Q6. `print` against `x`?**
`print` evaluates a typed expression and formats the result by type. `x` reads raw memory at an address with a count, format and unit size, with no type. I use `x` for buffers' real bytes, the instruction at the program counter, and the stack.

**Q7. Why do you see garbage in a local at the first line of a function?**
gdb stops before a line runs, and an uninitialised local holds whatever was on the stack. Step once.

**Q8. How would you inspect an array behind a pointer?**
`print *p@n` shows `n` elements, `print p[2]` one element, `x/ndw p` the raw words, and `set print elements` controls truncation.

**Q9. How do you change a variable or call a function from gdb, and what are the risks?**
`set var x = 5`, `print x = 5`, `call f(args)`. Calling runs real code in the stopped process: it can hit breakpoints, take a lock the stopped thread already holds and deadlock, or alter state. Fine for checking a hypothesis, careless on a live system.

**Q10. How do you debug a bug that only occurs when the program is run with particular arguments and input?**
`gdb --args prog args`, `run < input`, or `set args`; a script with `-batch -x` so the session is repeatable; breakpoints placed with conditions; and the same commands saved for the bug report.

**Q11. Why does `info frame` say "inlined into frame 1"?**
The compiler inlined the function into its caller, so there is no real call frame. gdb rebuilds a virtual frame from the debug information, which is why a backtrace at `-O2` can show functions that have no call instruction.

**Q12. How would you find out why a struct is bigger than the sum of its fields?**
`ptype/o struct name` prints each field's offset and size and marks the holes. Padding comes from alignment: a 4-byte `int` followed by an 8-byte `double` leaves a 4-byte hole so the `double` is 8-aligned.

**Q13. What is the difference between debugging at `-O0` and `-O2`, and which do you choose?**
`-O0` keeps every variable and the source order, so debugging is straightforward, but the program runs differently and some bugs vanish. `-O2` is what ships. I reproduce on the shipped build first, then use the weakest change that exposes the variable I need (`-Og`, `-fno-inline`, or reading registers), and I keep the failing optimised build.

**Q14. How do you automate a repeated debugging session?**
Put the commands in a file, run `gdb -batch -x file --args prog args`, set `pagination off` and `confirm off`, and use `define` for repeated inspections. Breakpoint `commands` can print and continue (see DBG 03).

**Q15. How are gdb and LLDB different to use?**
The ideas are the same (breakpoints, frames, expression evaluation). The spelling differs (`frame variable`, `breakpoint set`, `memory read`), the output is more verbose, and the official command map covers it. Both let `finish` act on the selected frame.

---

## 12. Scenarios and where to practise

All scenarios use the `inv.c` program of section 2 (build it with `-g -O0`, and with `-g -O2`), or a program of your own that has a loop, a struct and a call chain.

**S1. Stop on the third iteration only, and look at the item being summed.**
*Where to practise:* `inv.c`, a breakpoint on the loop body.
*Approach:* `break inv.c:10 if i == 2` (or `ignore`), `run 4`, then `print v[i]`, `print/x v[i].qty`, `x/s v[i].name`, and `info breakpoints` to confirm the hit count. Compare the condition with the `ignore` form.

**S2. You stepped into the wrong function. Get out and learn what it returned.**
*Where to practise:* `break main`, `step` into `find`.
*Approach:* `finish`, read `Value returned`, then `print $` to reuse it. Then deliberately select frame 1 first and run `finish`, to see the trap.

**S3. A pointer argument looks wrong. Find who passed it.**
*Where to practise:* stop in `total`, change the argument with `set var v = 0` (not recommended on real systems), and continue to crash.
*Approach:* `bt`, `up`, `info locals` in the caller, `print` the variable that was passed, and compare with the value in frame 0.

**S4. The same program shows `<optimized out>` for its loop counter. Make the counter visible.**
*Where to practise:* `inv.c` at `-O2`.
*Approach:* `info source` to confirm the build, `info scope total` to see the location list, rebuild with `-Og`, then with `-O2 -fno-inline`, and compare what `info locals` shows. Explain the difference.

**S5. Why is `struct item` 32 bytes?**
*Where to practise:* `ptype/o struct item`, then `print sizeof`.
*Approach:* read the hole, reorder the fields (`double` first, then `int`, then the array), rebuild, and see the size change. Explain alignment.

**S6. Write a gdb script that reproduces a session and run it unattended.**
*Where to practise:* put the commands of section 2 in `session.gdb`.
*Approach:* `set pagination off`, breakpoint, `run`, `bt`, `info locals`, `finish`, `continue`, run it with `-batch -x`, and add a `define` for printing an item. Check it works from a clean shell.

**S7. Translate your gdb session to LLDB.**
*Where to practise:* the same program, `lldb -b -s commands.txt -- ./inv0 4`.
*Approach:* use the command map, run the commands, and note where output formatting and the selected-frame behaviour of `finish` differ.

---

## 13. Strong answers (outlines)

**S1.** Condition for exactness, `ignore` for a hot loop, hit count to confirm, then `print`, `x/s`, `print/x`.
**S2.** `finish`, then `print $`; always check the selected frame first.
**S3.** `bt`, `up`, look at the caller's variable; the crash site is not the cause site.
**S4.** `info source`; explain location lists; choose the weakest build that works; keep the failing optimised build; the bug may be UB.
**S5.** `ptype/o`, alignment and padding, reorder to shrink, and mention that file and network formats use explicit packing or serialisation, not raw structs.
**S6.** A script with the settings, the same commands, `-batch -x`, and a `define`, checked from a clean shell.
**S7.** Use the map; same ideas, different spelling; `finish` is on the selected frame in both.

---

## 14. Cheat sheet

```text
START      gdb --args PROG ARGS | run | start | starti | gdb -q -batch -x FILE --args PROG ARGS
BREAK      break FUNC|FILE:LINE|*ADDR | tbreak | rbreak REGEX | hbreak | break ... if COND
           condition N COND | ignore N COUNT | info breakpoints | delete|disable|enable N
MOVE       next | step | finish | until [LOC] | advance LOC | continue | stepi | nexti
LOOK       print[/x,d,c,t,f,a,s] EXPR | print *p@n | display EXPR | x/NFU ADDR | info locals|args|registers
           ptype[/o] T | whatis E | list | info line N | info source | disassemble /s FUNC
CHANGE     set var X = V | call F(A) | set $reg = V
STACK      bt | bt full | bt N | frame N | up | down | info frame
SETTINGS   set pagination off | set confirm off | set print pretty on | set print elements N
OPTIMISED  <optimized out> = no location at this pc: info source, info scope F, -Og or -fno-inline, @entry values
LLDB       breakpoint set -n F | frame variable | expr -- E | memory read | register read | frame select N
```

| Symptom | First command |
|---|---|
| Stops but variable looks wrong | `step` once more, it has not run yet |
| Breakpoint never hits | `info breakpoints` (hit count), `info line N` |
| Crash, want the caller's view | `bt`, `up`, `print` |
| Variable `<optimized out>` | `info source`, rebuild `-Og` or `-fno-inline` |
| Struct size surprise | `ptype/o` |

---

## 15. Sources and caveats

Sources used: the [GDB manual on setting breakpoints](https://www.sourceware.org/gdb/current/onlinedocs/gdb.html/Set-Breaks.html) (location specs and multiple locations, conditions, temporary and hardware breakpoints, `rbreak`, pending breakpoints); the official [GDB to LLDB command map](https://lldb.llvm.org/use/map.html); and a [GCC bug report on `-Og`](https://gcc.gnu.org/pipermail/gcc-bugs/2024-February/855494.html) (parameters unavailable after calls). The ideas follow [DBG 01](01_method_and_toolbox.md) (build for debugging, DWARF) and the C and computer-organisation tutorials for types, frames and registers.

**Run fresh for this tutorial** on a Linux 6.18 VM with `gcc` 13.3, `gdb` 15.1 and LLDB 18.1.3: every command in sections 2 to 8 on the `inv.c` program at `-O0`, `-O2`, `-Og` and `-O2 -fno-inline` (breakpoints and conditions, `ignore`, `tbreak`, `rbreak`, stepping and `until`, `finish`, `print` with formats and arrays, `display`, `x`, `ptype/o`, `bt full`, frames, `info scope`, `list`, `disassemble /s`, `.gdbinit` with `define`, batch scripts) and the LLDB forms listed in section 9. The program is tiny on purpose, so the output shows each command clearly and says nothing about large programs.

**From the cited documents, not run:** the `-Og` limitation (the bug report), the behaviour of pending breakpoints and `hbreak`. **From memory, check before quoting:** the byte `0xCC` and the trap sequence; four hardware debug registers on x86; `$_streq`; that `step` does not enter functions without debug information (the default setting); the TUI key bindings and layouts; the warning gdb gives when the source is newer than the executable; the auto-load safe-path rules; macOS entitlements and system integrity protection; and the `image lookup` form of LLDB's `ptype`. **Not run:** `-tui`, hardware breakpoints, attaching to a process, watchpoints and breakpoint `commands` (those are DBG 03), and anything on macOS.

Earlier and later: [DBG 01](01_method_and_toolbox.md), then DBG 03 in the [debugging plan](00_plan.md) (watchpoints, breakpoint commands, signals, attaching, threads and processes).
