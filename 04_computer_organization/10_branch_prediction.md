# CO 10: Branch Prediction

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 3 hours (1.5 reading, 1.5 problems) · You need: [tutorial 09](09_pipelining_and_hazards.md) (the pipeline and what a control hazard costs) and [tutorial 07](07_assembly_in_practice.md) (conditional jumps, flags, and `cmov`/`csel`)*

By the end of this tutorial you can:

- explain why a core must guess the outcome of a branch, and compute what a wrong guess costs in CPI;
- trace a 1-bit and a 2-bit predictor by hand, and say why the 2-bit counter is better;
- explain what the branch target buffer and the return address stack do, and why direction and target are separate problems;
- explain why history helps (correlation), how many history bits a pattern needs, and how gshare, tournament, TAGE and perceptron predictors use it;
- say why indirect branches (virtual calls, switch tables, function pointers) are hard to predict, and what to do about it;
- decide when branchless code (`cmov`, masks) wins and when it loses, using a simple cost model;
- measure mispredictions with `perf stat`, and explain the sorted-versus-random array result;
- say in a few sentences how branch prediction connects to the Spectre attacks.

In [tutorial 09](09_pipelining_and_hazards.md) a taken branch cost 2 bubbles and "predict not taken" made correct guesses free. A real core is much deeper and several instructions wide, so a wrong guess costs about 15 to 20 cycles and dozens of instruction slots. Everything below follows from that one fact. All traces and counts in this tutorial were checked with a small model of each predictor, and the C timings were measured.

---

## 1. Why predict, and what a miss costs

At the moment the core fetches an instruction, it has only the program counter. The instruction has not been decoded, and a branch's condition may not be computed for another 15 or more cycles. Yet fetch must choose the next PC **now**, every cycle. So the front end has to answer three questions from the PC alone:

1. **Is this instruction a branch at all?**
2. **If it is conditional: taken or not taken?** (the *direction*)
3. **If taken: where to?** (the *target*)

Three different structures answer them (sections 3 to 8). If the guess is wrong, the core finds out only when the branch executes. Everything fetched after it is thrown away (a **flush**, as in [tutorial 09](09_pipelining_and_hazards.md) section 7; tutorial 11 shows how an out-of-order core recovers) and fetch restarts at the right address.

**The cost model.** About one instruction in five is a branch in typical integer code.

> extra CPI = (fraction of branches) x (misprediction rate) x (penalty in cycles)

Take 20% branches and a 17-cycle penalty:

| Prediction accuracy | Extra CPI |
|---|---|
| 99% | 0.034 |
| 95% | 0.17 |
| 90% | 0.34 |
| 50% (a coin flip) | 1.7 |

Now put this on two different machines.

- On the 5-stage pipeline of tutorial 09 the penalty is only 2 cycles, so even 95% accuracy adds just 0.02 to a CPI of 1.
- On a 4-wide core the ideal CPI is 0.25. With 95% accuracy the CPI becomes 0.25 + 0.17 = 0.42, which is **68% above** the ideal. A single misprediction throws away up to 17 x 4 = 68 instruction slots.

The deeper and wider the core, the more accuracy matters. That is why the predictor is one of the biggest and most carefully tuned parts of a modern core. The penalty is roughly 12 to 20 cycles on today's large cores; the exact value is in the vendor's optimisation manual or can be measured (section 9.2 and Problem D).

---

## 2. Static prediction: guess without any history

The simplest rules are fixed in advance.

| Rule | Idea | Weakness |
|---|---|---|
| Always not taken | keep fetching the next instruction (tutorial 09) | wrong for every loop |
| Always taken | most branches in loops go back | wrong for most `if` tests |
| **BTFNT**: backward taken, forward not taken | a *backward* branch is a loop, so it is taken; a *forward* branch is usually an error check or a rare case | still a fixed guess |
| Compiler hint | `__builtin_expect`, `[[likely]]` (section 11) | only helps layout, not the hardware |

Why BTFNT works: a loop that runs 10 times has its backward branch taken 9 times and not taken once, so the rule is right 90% of the time. A forward `if (error) { ... }` is rarely taken, so "not taken" is usually right.

A static rule has no memory, so it cannot adapt. Real cores use it only as the guess for a branch they have never seen before. For everything else they learn from history.

---

## 3. Dynamic prediction: remember what happened

### 3.1 One bit per branch

Keep a table with one bit per entry, indexed by some low bits of the branch's address (the PC). The bit remembers **the last outcome** of that branch, and the predictor says "same as last time".

Take a loop that runs 5 times (`T T T T N`) and is entered again and again:

```text
outcome       T    T    T    T    N       T    T    T    T    N
prediction    N    T    T    T    T       N    T    T    T    T
result        MISS ok   ok   ok   MISS    MISS ok   ok   ok   MISS
```

Two misses per run: the loop exit (predicted taken), and the first iteration of the next run (the last outcome was "not taken"). With 5 iterations that is 60% accuracy; with 10 iterations it is 80%. The loop-exit miss is unavoidable for any predictor without history, but the second miss is the predictor's own fault.

### 3.2 Two bits: the saturating counter

Use a **2-bit saturating counter** per entry. It counts from 0 to 3 and stops at both ends. A taken branch adds 1, a not-taken branch subtracts 1. States 0 and 1 predict "not taken", states 2 and 3 predict "taken".

| State | Name | Predicts | After taken | After not taken |
|---|---|---|---|---|
| 0 | strongly not taken | N | 1 | 0 |
| 1 | weakly not taken | N | 2 | 0 |
| 2 | weakly taken | T | 3 | 1 |
| 3 | strongly taken | T | 3 | 2 |

The key is **hysteresis**: one surprise moves a strong state to a weak state of the same prediction, so the prediction does not flip. The same 5-iteration loop, once the counter has settled:

```text
outcome       T    T    T    T    N       T    T    T    T    N
state before  2    3    3    3    3       2    3    3    3    3
prediction    T    T    T    T    T       T    T    T    T    T
result        ok   ok   ok   ok   MISS    ok   ok   ok   ok   MISS
```

After the exit, the state drops from 3 to 2 but still predicts taken, so the next run starts correctly. **One miss per run**: 80% for a 5-iteration loop, 90% for 10 iterations, 99% for 100.

The 2-bit counter has its own weakness: a branch that strictly alternates (`N T N T ...`). Starting in state 1 it goes 1, 0, 1, 0, ... and is right half the time. Starting in state 2 it goes 2, 1, 2, 1, ... and is wrong **every** time, worse than guessing. The pattern is perfectly regular, but a counter only knows "mostly taken or mostly not", not "what came before". Section 5 fixes this.

### 3.3 The table and aliasing

The table is small: 4096 two-bit counters are only 1 KB. It is indexed by low bits of the PC (without the bits that are always zero because of instruction alignment). Different branches with the same low bits land on the **same counter**. This is **aliasing** (or interference).

Example: branch X is always taken and branch Y is never taken, they share a counter, and they run alternately (X, Y, X, Y, ...).

- With a 1-bit entry, each branch resets the bit for the other: almost every prediction is wrong.
- With a 2-bit counter that starts in state 3, it hovers between 3 and 2 and always says "taken": X is always right, Y always wrong, **50% accuracy**, although each branch alone is perfectly predictable.

The remedies are more entries, more index bits, and tags (so that a different branch is recognised, not blindly trusted). The tag idea comes back in TAGE (section 6).

---

## 4. The branch target buffer (BTB)

The direction predictor says "taken", but fetch still needs a target address, and it needs it in the same cycle. Waiting for decode would waste cycles on every taken branch. The **branch target buffer** is a small cache, indexed by the fetch address:

| Field | Meaning |
|---|---|
| tag | which branch this entry belongs to (part of its PC) |
| target | where it jumped last time |
| type | conditional, unconditional jump, call, return, indirect |

Fetch looks up the BTB in parallel with the instruction cache. The front end then works like this:

```text
        +--> BTB lookup ------------> "is a branch", target, type
  PC ---+--> direction predictor ---> taken / not taken
        +--> return address stack --> target, if the type is "return"
                    |
                    v
   next PC = taken ? target : PC + fetch width
```

What happens for a taken branch:

| Situation | Cost |
|---|---|
| BTB hit and the direction is right | none, or one small bubble in a slow BTB level |
| BTB miss, but the target is a fixed offset (direct branch) | the branch is found at decode, a few cycles lost: a "re-steer" |
| Direction wrong, or target wrong (an indirect branch whose target changed) | the **full penalty**, found at execution |

So a prediction has **two halves**: direction and target. The failure modes differ, and so do the fixes.

Real cores have several BTB levels: a small, fast one that answers every cycle for the hottest branches, and larger, slower ones. On AMD's Zen 4, [Chips and Cheese measured](https://chipsandcheese.com/p/amds-zen-4-part-1-frontend-and-execution-engine) a second-level BTB of 8192 entries. A program with a very large code footprint (many hot branches) overflows the BTB and the front end stalls; tutorial 12 shows this as "front-end bound".

---

## 5. Using history: correlating predictors

### 5.1 Branches depend on other branches

A single branch's own past does not always reveal its future. Often the answer lies in the *other* branches that just ran:

```c
if (aa == 2) aa = 0;      // branch b1
if (bb == 2) bb = 0;      // branch b2
if (aa != bb) { ... }     // branch b3
```

Suppose `aa` and `bb` are each 1 or 2 (random). After b1 and b2, `aa` is 0 if b1 was taken and 1 if not (same for `bb`), so **b3 is taken exactly when b1 and b2 disagree**. Looked at alone, b3 is a coin flip, and a counter predicts it right 50% of the time. Given the outcomes of b1 and b2, it is 100% predictable. (Check with the four combinations in Problem B.)

### 5.2 The global history register and gshare

A **global history register** (GHR) is a shift register of the last k branch outcomes of the whole program (1 = taken). Each resolved branch shifts its outcome in. The predictor uses the register to pick a counter:

- **Index by history only** (2^k counters): the same history always chooses the same counter.
- **gshare**: index = (PC bits) XOR (history). It mixes *which branch* with *what happened recently*, so the same history for different branches goes to different counters. That lowers aliasing.

Example with a 16-entry table (4 index bits): PC = `0x1234`, history = `0110`. PC >> 2 = `0x48D`, whose low four bits are `1101`. XOR with `0110` gives `1011`, so the index is 11.

Some designs also keep a **local history** per branch (the last outcomes of that branch only) and index a counter table with it. Local history is good for loops with a fixed count; global history is good for correlation like b1, b2, b3.

### 5.3 How much history does a pattern need?

A history of k bits can separate a pattern only if each k-bit history is followed by the *same* outcome every time. Checked with a model for each pattern repeating forever:

| Repeating pattern | Bits needed for 100% | Why |
|---|---|---|
| `T N T N ...` | 1 | after `T` comes `N`, after `N` comes `T` |
| `T T N` | 2 | with 1 bit the history `T` is followed by `T` once and by `N` once |
| loop of 5 iterations (`T T T T N`) | 4 | with 3 bits, `T T T` is followed by `T` once and `N` once |
| loop of 10 iterations | 9 | |
| loop of n iterations | **n - 1** | the predictor must *see* all n - 1 taken branches to know the next one is the exit |

Too little history is not just less accurate: the same counter gets conflicting updates and can be worse than no history at all. And a directly indexed table has 2^k counters. 16 bits means 65,536 counters (16 KB), but 32 bits means four billion. To use long histories, designs hash them (section 6).

---

## 6. Tournament, TAGE and perceptron predictors

No history length is best for every branch: easy branches need none, correlated ones need a few bits, long loops need many. Modern predictors combine several.

**Tournament (hybrid).** Run two predictors, for example a local-history one and a global-history one, plus a **chooser**: a table of 2-bit counters that learns, per branch, which of the two is more reliable. The chooser is updated only when the two disagree. Famous in the Alpha 21264.

**TAGE** (TAgged GEometric history length). The idea:

- a base table with no history (like the plain 2-bit counters of section 3);
- several tables that use **longer and longer histories**, in a geometric series (for example 4, 8, 16, 32, 64, 128 branches; these lengths are illustrative);
- every entry carries a small **tag** (a hash of the PC and history), so a table answers only when it has really seen this exact context.

All tables are looked up in parallel. The **longest-history table whose tag matches wins**; if none matches, the base table answers. After a misprediction the predictor allocates a new entry in a table with a longer history. Easy branches stay in the cheap base table; only hard branches use up long-history entries. TAGE-style predictors have repeatedly won the academic branch-prediction championships.

**Perceptron.** Keep a small vector of weights per branch. Each of the last k outcomes counts as +1 (taken) or -1 (not taken):

```text
y = w0 + w1*x1 + w2*x2 + ... + wk*xk        predict taken if y >= 0
after the branch resolves: if the prediction was wrong, or |y| was small,
    w0 += t,  wi += t * xi                  (t = +1 if taken, -1 if not)
```

The cost grows *linearly* with the history length, so histories of dozens of bits are possible. A weight near zero means "this old branch does not matter"; a large weight means "this branch decides". The weakness: it can only learn **linearly separable** functions. "b3 = b1 AND b2" is fine, but "b3 = b1 XOR b2" (section 5.1) can never be learned exactly. A table-based predictor (gshare, TAGE) learns XOR easily, because each combination of history bits has its own counter. A weighted sum also takes time, so perceptrons are used as slower, more accurate second-level predictors.

**Two levels.** Fast and slow predictors are often combined: a simple one answers within a cycle, and a larger, more accurate one a few cycles later overrides it if they disagree (a small bubble, much cheaper than a full misprediction).

Exact designs in Intel, AMD, Apple and Arm cores are mostly not published. TAGE and perceptron are the two research designs that production predictors are usually described as resembling.

---

## 7. The return address stack (RAS)

A `ret` is a jump whose target depends on who called the function. A function called from 20 places returns to 20 different places, so the BTB's "same as last time" is usually wrong.

But returns are predictable if you remember the calls. The **return address stack** is a small hardware stack: on a `call`, push the address after the call; on a `ret`, pop it and use it as the prediction. For properly nested calls and returns it is almost always right. Typical size is 16 to 32 entries (Zen 4 has 32, [as measured by Chips and Cheese](https://chipsandcheese.com/p/amds-zen-4-part-1-frontend-and-execution-engine)).

**Overflow.** A chain of calls deeper than the stack loses the oldest entries. With a 16-entry stack and a chain of 40 nested calls from 40 different call sites, the 16 innermost returns are predicted correctly and the other **24 mispredict**. A recursive-descent parser or a deeply nested visitor can do this. Plain self-recursion is the exception: every level returns to the same address, so even after wrap-around the stack guesses right, and only the outermost return is wrong.

**Broken pairing.** Anything that returns without a matching call, or calls without a return, desynchronises the stack: `longjmp`, C++ exception unwinding through many frames, coroutine or stack switching, and code that uses `ret` as a jump. (Retpoline, section 12, does exactly that on purpose.)

---

## 8. Indirect branches

An indirect jump or call takes its target from a register or memory: a `switch` compiled to a jump table, a virtual call, a call through a function pointer, the dispatch loop of an interpreter. The target depends on **data**, so the BTB's "same as last time" is the baseline guess.

- **Monomorphic** site (always the same target): predicted almost perfectly, essentially free.
- **Few targets in a pattern**: a history-based indirect predictor (the same idea as section 5, with the history mixed into the BTB lookup) can learn them.
- **Random among several targets**: unpredictable. With 5 equally likely targets, "same as last time" is right only 1 time in 5, so **80% of the calls mispredict**.

This is the real content behind "virtual functions are slow". A well-predicted virtual call costs a load of the vtable pointer, a load of the function address, and an indirect jump, which is cheap. Two things hurt:

1. a **megamorphic** call site (many different types, in random order) mispredicts constantly;
2. the compiler cannot inline through a virtual call, and inlining is often worth more than the branch itself because it unlocks other optimisations.

Fixes, from most to least common:

| Fix | How |
|---|---|
| Batch by type | sort or bucket the work by type first, so a megamorphic site becomes a series of monomorphic runs. A standard fix in packet processing |
| Static dispatch | templates, or a closed set of types with `std::variant` and `std::visit` |
| Tell the compiler the type | `final` classes, or devirtualisation from profile data |
| Profile-guided optimisation | it can insert "if the target is the common one, call it directly" (indirect call promotion) |

---

## 9. Branchless code, conditional moves and predication

A **branchless** version turns a *control* dependence (which instruction comes next) into a *data* dependence (which value is chosen). Nothing is left to mispredict.

- **Conditional move**: `cmovcc` on x86, `csel` on AArch64 (tutorial 07). Compute both values, and a flag selects one. Base RISC-V has no conditional move; the Zicond extension adds one.
- **Masks**: `(a & m) | (b & ~m)` with `m = -(cond)`, which is all ones if the condition holds.
- **Predication**: attach the condition to the instruction itself. 32-bit Arm could make almost any instruction conditional; AArch64 kept only a few, such as `csel`, `csinc` and `ccmp`. GPUs and SIMD units use masks the same way (tutorials 21 and 22).

### 9.1 The cost model

```text
branch     :  base work  +  p x penalty            (p = misprediction rate of THIS branch)
branchless :  base work  +  work of BOTH sides  +  the select on the dependency chain
```

A predicted branch costs nothing extra. A conditional move always costs its extra instructions and one cycle on the critical path, but never a misprediction. Branchless wins when `p x penalty` exceeds the extra cost:

```text
break-even  p  =  extra cost of branchless / penalty
```

With a 16-cycle penalty and 2 extra cycles, break-even is p = 12.5%; with 1 extra cycle and a 17-cycle penalty, about 6%. So: keep a branch that is predicted well (below about 5%); consider a conditional move for a branch that is close to random.

What is `p`? For a branch that is independently taken with probability q, the best a predictor can do is guess the more likely side, so p = min(q, 1 - q). A 50/50 random branch has p = 50%. A data-dependent branch in sorted or clustered data has a small p.

### 9.2 The classic experiment

Sum all elements that are at least 128, in an array of random values from 0 to 255:

```c
long sum_over(const int *d, int n) {
    long s = 0;
    for (int i = 0; i < n; i++)
        if (d[i] >= 128) { s += d[i]; __asm__ volatile(""); }   /* the empty asm keeps a real branch */
    return s;
}
```

The branch is taken for half of the elements. Run it on the array in random order, and on the same values after sorting. Measured on an Intel Xeon at 2.1 GHz with GCC 13 and `-O2` (32,768 elements, repeated many times; your numbers will differ, the shape will not):

| Version | Random order | Sorted |
|---|---|---|
| branch (the code above) | about 4.4 ns per element | about 0.3 ns per element |
| `s += (d[i] >= 128) ? d[i] : 0;` (branchless) | about 0.6 ns | about 0.6 ns |

- **Sorted:** the first half of the array is all "not taken", the second half all "taken". The predictor is almost always right, and the branch version is the fastest.
- **Random:** half of the branches mispredict, so about one element in two pays the penalty. The 4.1 ns difference per element means about 8 ns per misprediction, which is about 17 cycles at 2.1 GHz (about 24 if the core was boosted to the 3 GHz that a similar Xeon ran at; [tutorial 28](28_performance_and_power.md) section 2.2 shows how to find the real clock). This is the penalty of section 1, measured.
- **Branchless:** the same time for either order. It is a little slower than the best case of the branch version (extra work every time) and much faster than the worst case.

The same code, the same data, and opposite conclusions depending on predictability. That is why "always write branchless code" is wrong, and why "sorting made my filter faster" is a classic.

**A trap.** Without the empty `asm`, GCC and Clang at `-O2` had already turned the `if` into a conditional move (and Clang even vectorised it), so both orders ran at the same speed. Compilers convert simple branches by themselves. Before concluding that a branch is the problem, look at the assembly (`gcc -S`, or `objdump -d`) and **measure**.

### 9.3 When branchless loses

1. **The branch is predictable.** Loop exits, error checks and sorted data cost nothing as branches, but a `cmov` still costs instructions and a cycle on the dependency chain.
2. **One side is expensive or unsafe.** `p ? p->x : 0` as a conditional move would load through a possibly null pointer, and the compiler may not do that. If one side is a long computation, branchless runs both.
3. **A slow condition.** A predicted branch lets the core run ahead: it starts the next iteration's loads while the condition is still being computed. A conditional move makes everything after it wait for the condition, so if the condition comes from a cache miss, the whole chain stalls. Searches over large arrays are the usual example.

If you know a branch is random, tell the compiler: Clang has `__builtin_unpredictable(cond)`, and GCC has `__builtin_expect_with_probability(cond, 1, 0.5)`. Both nudge it toward a conditional move.

---

## 10. Measuring before guessing

On Linux, the hardware counters tell you directly:

```bash
perf stat -e instructions,branches,branch-misses ./app
perf record -e branch-misses ./app && perf report      # which function?
perf annotate                                           # which branch instruction?
```

Two numbers matter:

- **Miss rate** = `branch-misses` / `branches`. Below about 1% is healthy. Above about 5% in a hot loop deserves a look.
- **MPKI** (mispredictions per 1000 instructions) = `branch-misses` / `instructions` x 1000. It ties the misses to the work done. As a rough guide, regular numeric loops are far below 1, general integer code is around a few, and double digits is bad.

Output looks like this (made-up numbers with the right shape, for the random-order loop above, 1 million elements):

```text
        6,000,000      instructions
        2,000,000      branches
          500,000      branch-misses     #  25.00% of all branches
```

The miss rate is 25%, not 50%, because half of the branches are the loop's own back-edge, which is always predicted. MPKI = 500,000 / 6,000,000 x 1000, about 83. On the sorted array the same counter would read a tiny fraction of a percent.

The top-down method ([tutorial 12](12_inside_a_modern_core.md) and [tutorial 28](28_performance_and_power.md)) reports "bad speculation" as a share of all pipeline slots, which tells you whether mispredictions are worth your time at all.

**Where it works.** `perf` is Linux only. Virtual machines and many cloud instances do not expose hardware counters (`perf stat` prints `<not supported>`). On a Mac, use Instruments' CPU counters, or just time the code.

**Benchmark with care.** Modern predictors can memorise a random pattern of a few dozen branches. A test that loops over a *short* random array many times looks predictable and gives wrong answers. Use data that is much longer than the predictor can memorise (thousands of elements or more).

---

## 11. Hints, code layout and profile-guided optimisation

```c
if (__builtin_expect(error, 0)) { handle_error(); }     // GCC / Clang, any C version

if (packet_valid) [[likely]] { forward(p); }            // C++20
else [[unlikely]] { drop_and_log(p); }
```

These do **not** program the hardware predictor, which learns from what the branch actually does. They tell the *compiler* how to arrange the code: the likely path becomes the fall-through (a taken branch ends the current fetch block, so straight-line code fetches faster), and unlikely code moves out of the way, which saves instruction-cache space. The gain is mostly layout, not prediction accuracy.

**Profile-guided optimisation (PGO)** does this properly, with real data instead of guesses:

```bash
g++ -O2 -fprofile-generate app.cpp -o app && ./app < typical_input   # writes profile data
g++ -O2 -fprofile-use app.cpp -o app                                  # uses it
```

The profile gives the compiler each branch's real probability, which code is hot or cold, which calls to inline, and the common target of indirect calls. It only helps if the training input looks like production.

---

## 12. Speculation is also a security boundary

Wrong-path instructions never change registers or memory, but they **do** change the caches and the predictors. Those traces survive the flush, and an attacker can read them with timing. The attacks are built on the predictors of this tutorial:

| Attack | What the attacker does |
|---|---|
| **Spectre v1** (bounds-check bypass) | trains the direction predictor so that `if (i < len) x = a[i]` is predicted taken, then passes an out-of-range `i`; the speculative load reads a secret and touches a cache line that depends on it |
| **Spectre v2** (branch target injection) | trains the BTB or indirect predictor so that the victim's indirect branch speculatively jumps to code the attacker chose |
| **Spectre-RSB** | pollutes or exploits the return address stack |

Mitigations cost performance: retpoline (replace indirect jumps with a call/return sequence that parks speculation in a harmless loop), predictor-flushing and isolation controls (IBPB, IBRS, STIBP), `lfence`, and index masking in software. If a kernel upgrade makes a workload slower, a mitigation change is a likely cause. On Linux, `/sys/devices/system/cpu/vulnerabilities/` lists what is active on your machine. [Tutorial 30](30_hardware_security.md) covers these attacks and fixes in detail, with the costs measured.

---

## 13. What this means for your code

- **A predictable branch is nearly free; an unpredictable one costs 15 to 20 cycles.** Spend effort only on the second kind, and measure first.
- **Make branches predictable by changing the data**: sort, partition, or batch items so that similar items run together.
- **Take hard-to-predict branches out of hot loops** with a conditional move, a mask or arithmetic, and check the assembly to see what the compiler really did.
- **Keep hot paths short and the common case the fall-through** (`[[likely]]`, PGO).
- **Avoid megamorphic hot call sites**: batch by type or use static dispatch.
- **Keep calls and returns balanced and recursion shallow** in hot code.
- **Do not time a branch on a tiny repeated input.** The predictor memorises it.

---

## 14. Mistakes that cost points

1. **Predicting only the direction.** A taken branch also needs a target; the BTB, the return stack and the indirect predictor handle that.
2. **Quoting an accuracy without a penalty and a width.** 95% costs 0.02 CPI on a 5-stage pipeline and 0.17 extra CPI on a 4-wide core with a 17-cycle penalty (68% of its ideal CPI of 0.25).
3. **"Branchless is always faster."** It loses when the branch is predictable, when one side is expensive or unsafe, and when it puts a slow condition on the critical path.
4. **Applying branchless code without checking.** The compiler may already have done it, or the branch may already be 99% predicted.
5. **Saying `likely` / `unlikely` train the predictor.** They change layout.
6. **Saying virtual functions are slow, full stop.** Monomorphic calls predict well; the real costs are megamorphic sites and lost inlining.
7. **Miscounting a 2-bit counter.** It predicts taken in states 2 and 3, it saturates at 0 and 3, and a loop costs one miss per run, not two.
8. **Forgetting that a loop exit needs n - 1 history bits.**
9. **Benchmarking on a short random array.** The predictor memorises it.
10. **Saying wrong-path work leaves no trace.** It leaves cache and predictor state, which is exactly what Spectre uses.

---

## 15. Questions you will be asked

**Why does a core predict branches at all?** The branch outcome is known only deep in the pipeline, and fetch needs the next address every cycle. Without a prediction, the core would stall for the whole pipeline depth on about one instruction in five.

**What does a misprediction cost, and how do you compute its effect?** The cycles from fetch to resolution plus the refill, roughly 12 to 20 on current large cores. Extra CPI = branch fraction x miss rate x penalty. On a wide core it also wastes the many instruction slots lost during those cycles.

**1-bit versus 2-bit predictor?** A 1-bit predictor repeats the last outcome and misses twice per loop run. A 2-bit saturating counter needs two wrong outcomes in a row to change its prediction, so it misses once per loop run.

**What is the BTB, and why is it separate from the direction predictor?** A cache of branch targets indexed by the fetch address. The direction predictor decides taken or not; the BTB supplies the address in the same cycle, before decode. They fail differently: a wrong direction versus a missing or wrong target.

**What is a correlating (global-history) predictor?** One that uses the recent outcomes of other branches as part of its index. Branches often depend on earlier ones, and a global history register captures that. gshare XORs the history with the PC to reduce aliasing.

**How does TAGE work, in one minute?** A base table without history plus several tagged tables with geometrically increasing history lengths. The longest matching table predicts; a new entry in a longer table is allocated after a misprediction. Easy branches stay cheap and hard ones get long histories.

**Why can a perceptron predictor not learn XOR?** It computes a weighted sum and compares it with zero, which separates inputs only with a straight line. XOR of two outcomes is not separable that way.

**What is the return address stack?** A small hardware stack, pushed by calls and popped by returns, that predicts return addresses. It fails when the call depth exceeds its size or when calls and returns are not paired.

**Why are indirect branches hard to predict?** Their target depends on data. "Same as last time" works for monomorphic sites; random targets mispredict constantly.

**Is branchless code always faster?** No. Give the formula: it wins when the misprediction rate times the penalty exceeds the extra cost of computing both sides and the select. Then say you would measure `branch-misses` first.

**Why is processing a sorted array faster?** The branch becomes predictable. Same code, same data, a different order.

**Do `likely` and `unlikely` change the hardware predictor?** No. They change code layout. PGO gives the compiler real probabilities.

**Are virtual functions slow?** A monomorphic call is a well-predicted indirect jump plus two loads. They get slow at megamorphic sites, and when they block inlining.

**How do branch predictors relate to Spectre?** Attackers train the predictors (direction for v1, target for v2, return stack for RSB) so that the victim speculatively runs code it should not, and then recover the secret from cache timing.

**Why not build a gigantic predictor?** It must answer within about a cycle (or a very few), so big tables are slow. Area and power grow, aliasing and warm-up never disappear, and past a point there is little extra accuracy to gain.

---

## 16. Problems

**How to use this section.** Try each problem before you read the answers. A, B and C are paper problems (20 to 25 minutes each). D needs a C compiler and about 30 minutes; it is a few lines of code, not a project.

### Problem A (paper): counters

A loop branch executes as `T T T T T N`, 6 times per run (5 taken, then the exit), and the loop runs 3 times in a row (18 outcomes).

(a) A 1-bit predictor starts predicting "not taken". Write the predictions, count the mispredictions in each run, and give the steady-state accuracy.
(b) A 2-bit counter starts in state 1 (weakly not taken). Write the state before each outcome, count the mispredictions per run, and give the steady-state accuracy.
(c) The outcome sequence is `N T N T N T N T`. Count the mispredictions of a 2-bit counter that starts in state 1, and of one that starts in state 2.
(d) Branch X is always taken and branch Y is never taken. They execute alternately (X, Y, X, Y, ...) and share one 2-bit counter that starts in state 3. What fraction of predictions is wrong? What happens with a 1-bit predictor?

*Where:* paper. *How:* sections 3.1 to 3.3. Use the state table: predict taken in states 2 and 3, taken adds 1, not taken subtracts 1, stop at 0 and 3.

### Problem B (paper): history and correlation

(a) A branch repeats `T T N`. What is the best accuracy of a single 2-bit counter? Explain why 1 bit of history is not enough, and write the table "history to next outcome" for 2 bits of history.
(b) A loop runs 8 iterations (7 taken, then the exit). How many history bits does a history-indexed predictor need to predict the exit? How many counters does a directly indexed table need for a loop of 20 iterations, and why does that argue for TAGE or a perceptron?
(c) gshare with a 16-entry table: PC = `0x1234`, history = `0110`. Compute the index. Then compute it for history `1011`.
(d) With `aa` and `bb` taking the values below, find the outcome of b1, b2 and b3 from section 5.1: (aa, bb) = (1, 1), (2, 1), (1, 2), (2, 2). Write b3 as a function of b1 and b2. Which of a 2-bit counter, a global-history table with 2 bits and a perceptron can predict b3 perfectly?

*Where:* paper. *How:* sections 5 and 6. For (d) follow the code line by line: b1 and b2 change `aa` and `bb` before b3 compares them.

### Problem C (paper): cost

(a) A 4-wide core has an ideal CPI of 0.25. 18% of its instructions are branches, the penalty is 16 cycles, and the accuracy is 97%. Compute the CPI.
(b) Design X has an accuracy of 94% and a 14-cycle penalty. Design Y has 97% and a 17-cycle penalty (a bigger predictor needs one more stage). Compute the CPI of each. Which is faster, and by how much?
(c) X can run at 3.4 GHz and Y only at 3.0 GHz. Compare the time per instruction.
(d) A loop costs 1 cycle per element as a well-predicted branch. A branchless version costs 3 cycles per element. The penalty is 16 cycles. The data is random, and the test passes with probability q. For q = 0.1, 0.3, 0.5 and 0.9: the best possible miss rate p, the cost per element of the branchy version, and which version wins. Where is the break-even p?

*Where:* paper and a calculator. *How:* sections 1 and 9.1. The best predictor for an independent random branch guesses the more likely side.

### Problem D (a few lines of C): sorted versus random

Save the following as `thr.c`. It fills an array with random values from 0 to 255, sorts it if you pass any argument, and times the `sum_over` function from section 9.2:

```c
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define N 32768

static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

__attribute__((noinline)) long sum_over(const int *d, int n) {
    long s = 0;
    for (int i = 0; i < n; i++)
        if (d[i] >= 128) { s += d[i]; __asm__ volatile(""); }
    return s;
}

int main(int argc, char **argv) {
    static int d[N];
    srand(1);
    for (int i = 0; i < N; i++) d[i] = rand() % 256;
    if (argc > 1) qsort(d, N, sizeof d[0], cmp);
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    long t = 0;
    for (int r = 0; r < 20000; r++) t += sum_over(d + (r & 7), N - 8);
    clock_gettime(CLOCK_MONOTONIC, &b);
    double ns = (b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec);
    printf("%s: %.2f ns per element (sum %ld)\n", argc > 1 ? "sorted" : "random",
           ns / (20000.0 * (N - 8)), t);
}
```

(a) Compile with `cc -O2 -o thr thr.c`, run `./thr` and `./thr sorted`, and compute the ratio. Estimate the cost of one misprediction in ns and in cycles (half of the elements mispredict; use your clock speed).
(b) Remove the empty `asm` from `sum_over`, rebuild, and look at the assembly (`cc -O2 -S thr.c`, then read `sum_over`). Is there a `cmov`/`csel` or a vector instruction? Time both orders again.
(c) Change the test to `d[i] >= 250` (taken for about 2% of the elements). Time both orders with the `asm` restored. Explain the result with the break-even formula.
(d) On Linux with hardware counters, run `perf stat -e instructions,branches,branch-misses ./thr` and `./thr sorted`. Compute the miss rate and the MPKI for both.

*Where:* your own machine (a VM may hide the counters; on a Mac skip (d)). *How:* sections 9.2 and 10.

---

## Answers

### Problem A

(a) Predictions and outcomes (the predictor repeats the previous outcome, and starts at N):

```text
outcome      T T T T T N | T T T T T N | T T T T T N
prediction   N T T T T T | N T T T T T | N T T T T T
```

Mispredictions: 2, 2 and 2. The first `T` of every run is predicted N (the last outcome was N), and the exit is predicted T. That is 2 of 6 wrong, a steady-state accuracy of **67%**.

(b) State before each outcome (start 1, counting up on T and down on N, limits 0 and 3):

```text
outcome       T  T  T  T  T  N | T  T  T  T  T  N | T  T  T  T  T  N
state before  1  2  3  3  3  3 | 2  3  3  3  3  3 | 2  3  3  3  3  3
prediction    N  T  T  T  T  T | T  T  T  T  T  T | T  T  T  T  T  T
```

Mispredictions: **2, 1 and 1** (the first run pays for the warm-up: the first `T` is predicted N and the exit is predicted T). Steady state: one miss per run, **83%** accuracy.

(c) Starting in state 1: the states before each outcome are 1, 0, 1, 0, ...; the predictions are N every time, so every `T` is wrong: **4 mispredictions** out of 8. Starting in state 2: the states are 2, 1, 2, 1, ...; the predictions alternate T, N, T, N, which is exactly the opposite of the outcomes, so **all 8 are wrong**. A regular pattern can be worse than random for a counter with no history.

(d) The counter moves between 3 and 2 and always predicts taken. X is always right and Y is always wrong: **50% wrong**, although each branch alone is perfectly predictable. With a 1-bit predictor, X sets the bit to 1 and Y sets it to 0, so each branch finds the other's value: after the first prediction, **every** one is wrong.

### Problem B

(a) Predicting "taken" every time is right 2 times in 3, so a counter reaches **67%** at best. With 1 bit of history, the history `T` is followed by `T` once (the first `T` of the pair) and by `N` once, so the same table entry gets opposite updates and cannot be right. With 2 bits:

| History (older, newer) | Next outcome |
|---|---|
| `T T` | `N` |
| `T N` | `T` |
| `N T` | `T` |

Every history has exactly one successor, so after warm-up the accuracy is **100%**. (Not enough history is not only less accurate: with the counters in conflict, 1 bit of history is even worse than none.)

(b) A loop of 8 iterations has 7 taken outcomes before the exit, so it needs **7** history bits (n - 1). For 20 iterations: 19 bits, so 2^19 = 524,288 counters (128 KB at 2 bits each) for one pattern, and that is before the history of any other branch is included. Counters grow exponentially with the history length. TAGE keeps long histories only for the few branches that need them (and uses tags and hashing). A perceptron needs one weight per history bit, so its size grows only linearly.

(c) PC >> 2 = `0x48D`, low four bits `1101`. History `0110`: `1101` XOR `0110` = `1011` = index **11**. History `1011`: `1101` XOR `1011` = `0110` = index **6**. The same branch uses different counters depending on the recent history.

(d)

| (aa, bb) | b1 (aa == 2) | b2 (bb == 2) | after b1, b2: aa, bb | b3 (aa != bb) |
|---|---|---|---|---|
| (1, 1) | N | N | 1, 1 | N |
| (2, 1) | T | N | 0, 1 | T |
| (1, 2) | N | T | 1, 0 | T |
| (2, 2) | T | T | 0, 0 | N |

So b3 = b1 XOR b2: taken exactly when b1 and b2 differ. A 2-bit counter on b3 alone sees a random sequence and gets about 50%. A global-history table indexed by the last two outcomes (b1, b2) has a separate counter for each of the four combinations and reaches essentially 100% (model: 99.95% over 4000 random pairs, the difference being warm-up). A perceptron cannot: XOR is not linearly separable. In the model it stayed far from perfect, while the same perceptron learned AND of the two outcomes at 100%.

### Problem C

(a) Extra CPI = 0.18 x 0.03 x 16 = 0.0864, so CPI = 0.25 + 0.0864 = **0.336**. That is 35% above the ideal 0.25.

(b) X: 0.18 x 0.06 x 14 = 0.1512, CPI = **0.401**. Y: 0.18 x 0.03 x 17 = 0.0918, CPI = **0.342**. Y is faster by a factor of 0.401 / 0.342 = **1.17** (X needs 17% more cycles per instruction). The longer penalty is more than paid for by half as many mispredictions.

(c) X: 0.4012 / 3.4 = 0.1180 ns per instruction. Y: 0.3418 / 3.0 = 0.1139 ns. Y is still faster, by about **3.6%**; the clock advantage of X eats most but not all of the CPI gap.

(d) With p = min(q, 1 - q), the branchy version costs 1 + 16p cycles per element:

| q | p | Branchy cost | Branchless cost | Winner |
|---|---|---|---|---|
| 0.1 | 0.1 | 2.6 | 3 | branchy (slightly) |
| 0.3 | 0.3 | 5.8 | 3 | branchless |
| 0.5 | 0.5 | 9.0 | 3 | branchless |
| 0.9 | 0.1 | 2.6 | 3 | branchy (slightly) |

Break-even: 1 + 16p = 3, so p = 2 / 16 = **12.5%**. Note that q = 0.9 and q = 0.1 are the same case: a branch that is almost always taken is as easy to predict as one that is almost never taken.

### Problem D

(a) Typical result: random order about 4 ns per element and sorted about 0.3 ns, a ratio of 10 or more (on the Xeon of section 9.2: 4.4 and 0.3). Half of the elements mispredict, so one miss costs about (random time - sorted time) / 0.5. With 4.1 ns per element that is about 8 ns, and 8 ns x 2.1 GHz is about 17 cycles. On your machine the result should fall in the 12 to 20 cycle range of section 1. If it does not, check the clock speed (use the real running frequency, not the label) and that the `asm` is still in the code.

(b) Without the `asm`, GCC and Clang at `-O2` replace the branch with a conditional move (`cmovg`, or `csel` on AArch64) or with vector instructions, and both orders run at the same speed (on the Xeon: 0.6 ns with GCC, 0.2 ns with Clang), far below the random-order time of the branch version. The lesson: the compiler already did the "optimisation" you were about to do, and an unmeasured branch-removal change may do nothing.

(c) With the test `>= 250`, only about 2% of the elements pass, so the branch is almost always "not taken". The gap between the orders shrinks from about 14 times to about 2 times (on the Xeon: about 0.7 ns random against 0.3 ns sorted). The miss rate p in the random order is now a few percent, not 50%, so the cost p x penalty is small. It is not zero, and it is near the 6% break-even: on this machine the branchless version (about 0.6 ns) is no longer much faster than the branch in the random order, while the branch stays the better choice on sorted or clustered data. The cost of a branch grows with its misprediction rate, so a rarely taken branch is cheap.

(d) Using the shape of section 10: for the random order the miss rate is close to 25% of all branches (half of the branches are the loop's own back-edge), and the MPKI is high (tens). For the sorted order both are tiny. MPKI = `branch-misses` / `instructions` x 1000, miss rate = `branch-misses` / `branches`.

---

## Cheat sheet

- **Why:** fetch needs the next PC every cycle, but a branch resolves deep in the pipeline. A wrong guess flushes everything after the branch.
- **Cost:** extra CPI = branch fraction x miss rate x penalty. The penalty is about 12 to 20 cycles; wider and deeper cores lose more.
- **Three questions at fetch:** is it a branch, taken or not, which target. Structures: BTB (branch and target), direction predictor, return stack, indirect predictor.
- **Static:** always N, always T, BTFNT (backward taken, forward not taken). Used only for a never-seen branch.
- **1-bit:** repeat the last outcome; 2 misses per loop run.
- **2-bit counter:** states 0 to 3, predict taken at 2 and 3, saturating. One miss per loop run; bad on strict alternation.
- **Aliasing:** different branches share a counter. Fix with more bits, tags, or hashing.
- **BTB:** cache of targets indexed by the fetch address; several levels; two halves of a prediction (direction and target) fail separately.
- **History:** global history register; gshare = PC XOR history. A loop of n iterations needs n - 1 history bits.
- **Tournament:** two predictors plus a chooser. **TAGE:** base table plus tagged tables with geometric history lengths, longest match wins. **Perceptron:** weighted sum of history bits, linear in the history length, cannot learn XOR.
- **Return stack:** push on call, pop on return, 16 to 32 entries. Fails on deeper chains of different call sites and on unpaired calls and returns.
- **Indirect branches:** monomorphic is cheap; random targets mispredict. Fix by batching by type, static dispatch, `final`, PGO.
- **Branchless:** wins when p x penalty is greater than the extra cost (about 6% to 12%); loses when the branch is predictable, one side is expensive or unsafe, or the condition is slow. Check what the compiler already did.
- **Hints:** `likely`, `__builtin_expect` change layout, not the hardware. PGO uses real data.
- **Measure:** `perf stat -e instructions,branches,branch-misses`; miss rate and MPKI; `perf record -e branch-misses` for the location.
- **Security:** speculation leaves cache and predictor traces. Spectre v1 trains the direction predictor, v2 the BTB or indirect predictor, RSB the return stack. Retpoline and friends cost performance.

**Next:** tutorial 11, Superscalar and Out-of-Order Execution (instruction-level parallelism, Tomasulo's algorithm, register renaming, the reorder buffer, and how a core recovers from a misprediction). Say "next" when you are ready.
