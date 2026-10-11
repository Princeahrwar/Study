# 04 — Branch prediction and speculative execution

> Prerequisite: `01_cpu_pipelines_and_speculation.md`.

## Why prediction is mandatory, not an optimisation

A core fetches 15-20 stages ahead of retirement. A conditional branch's outcome
is not known until its condition is computed, deep in the back end. If the core
waited, it would stall on every branch — and roughly one instruction in five is
a branch. Prediction is not a clever trick; it is what makes a deep pipeline
possible at all.

Cost of being wrong: flush everything fetched since the branch and restart.
**15-20 cycles**, which at 3 GHz is 5-7 ns — comparable to an L2 miss. Frame it
that way and the priority becomes obvious: an unpredictable branch in a
packet-processing inner loop is a cache-miss-sized problem.

## What the predictor actually contains

Three cooperating structures. You do not need microarchitectural detail, but you
do need to know they are *separate*, because the failure modes differ:

1. **Direction predictor** — taken or not taken. Modern designs use TAGE-style
   multi-length history: several tables indexed by increasingly long global
   history, with the longest matching prediction winning. Consequence: the
   predictor can learn *patterns*, not just biases. A branch alternating
   T,N,T,N,T,N is predicted essentially perfectly. So is a repeating period-8
   pattern. Randomness is what defeats it.
2. **Branch target buffer (BTB)** — where a taken branch goes. Matters for
   indirect calls: virtual dispatch, function pointers, jump tables,
   interpreter dispatch loops.
3. **Return stack buffer (RSB)** — a small hardware stack (16-32 entries)
   matching calls to returns. Deep recursion overflows it and returns start
   mispredicting. This is a real effect in deeply recursive parsers.

The practical takeaway: **a predictable branch is nearly free; an unpredictable
branch costs an L2 miss.** Optimisation effort belongs only on the second kind.

## Measuring before you guess

```bash
perf stat -e branches,branch-misses ./app
# Misprediction rate = branch-misses / branches.
# Under ~1% is healthy. Over 5% on a hot loop is worth attention.

perf record -e branch-misses:pp ./app && perf report   # which branch?
```

Do this before applying anything below. The most common mistake with branchless
programming is applying it to branches that were already predicted at 99%.

## Branchless techniques, and their real cost

```cpp
// Branchy. If the data is sorted or otherwise biased, this is FASTER than
// anything below, because the predictor gets it right and the taken path is free.
int clamp_branch(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Branchless via cmov. No misprediction possible, but the result now has a
// data dependency on the comparison — it cannot start until v is known.
int clamp_cmov(int v, int lo, int hi) {
    v = (v < lo) ? lo : v;
    v = (v > hi) ? hi : v;
    return v;
}

// Branchless via arithmetic. Same trade, more obscure. Rarely worth the
// readability cost unless you have measured a win.
int select_mask(bool c, int a, int b) {
    const int m = -static_cast<int>(c);   // c ? ~0 : 0
    return (a & m) | (b & ~m);
}
```

**The trade is precise:** a branch costs `P(mispredict) * ~17 cycles`. A `cmov`
costs ~1 cycle of latency *added to the dependency chain*, always. So branchless
wins when misprediction probability exceeds roughly 1/17, i.e. about 6%. A branch
predicted at 95% should stay a branch.

The classic demonstration: summing values above a threshold over an array. Sorted
input makes the branch perfectly predictable and the branchy version wins
outright; shuffled input makes it a coin flip and the branchless version wins by
2-4x. Same code, same data, opposite conclusions — which is exactly why "always
write branchless" is wrong.

## Branch layout and hints

```cpp
// C++20 attributes. Affect code layout: the likely path stays in the fall-
// through, which also improves I-cache density and BTB behaviour.
if (packet_valid) [[likely]] {
    forward(packet);
} else [[unlikely]] {
    drop_and_log(packet);
}
```

These do not tell the *dynamic* predictor anything — it learns from behaviour
regardless. They tell the *compiler* how to lay out code and where to put cold
blocks. The gain is usually in instruction-cache footprint rather than
prediction accuracy, which is worth stating precisely rather than hand-waving.

Profile-guided optimisation does this properly and automatically. If you have a
representative workload, PGO beats hand-annotation:

```bash
g++ -O2 -fprofile-generate ...   && ./app < representative_workload
g++ -O2 -fprofile-use      ...   # layout, inlining and hints from real data
```

## Indirect branches: the virtual dispatch question

```cpp
// Indirect call through the vtable. The BTB predicts the target.
// Monomorphic at a call site (always the same derived type): predicted well,
// close to free. Polymorphic (varies unpredictably): a BTB miss per call.
struct Handler { virtual void handle(Packet&) = 0; virtual ~Handler() = default; };

// The common fix when the set of types is closed and known at compile time:
// static dispatch via templates, or std::variant + std::visit.
template <class Impl>
class Pipeline {
    Impl impl_;
public:
    void run(Packet& p) { impl_.handle(p); }   // direct call, inlinable
};
```

This is the real content behind "virtual functions are slow". They are not
inherently slow — a monomorphic virtual call is a well-predicted indirect branch
plus a dependent load of the vtable pointer. They become slow when the call site
is **megamorphic** and the BTB cannot keep up, and when they block inlining
(which is often the larger loss, because inlining unlocks other optimisations).

Sorting work by type before dispatch — batching all packets of one kind together
— converts a megamorphic site into a series of monomorphic ones. That is the
standard fix in packet processing, and it is a strong thing to propose unprompted.

## Speculation as a security boundary (know this exists)

Speculative execution leaves microarchitectural traces even when the work is
discarded. Spectre and Meltdown exploit that gap. You do not need exploit detail
in a systems interview, but you should know:

- The core *did* execute the wrong path and *did* move lines into the cache.
- Mitigations (`retpoline`, IBRS, `lfence` barriers, KPTI) cost real performance,
  which is why "why did this kernel upgrade slow us down?" is sometimes answered
  by mitigation changes. Check `/sys/devices/system/cpu/vulnerabilities/`.
- In a security-adjacent role (Palo Alto), knowing that speculation is a *side
  channel* and not just a performance feature is expected.

## Interview angles

**"Is branchless always faster?"**
No. Give the arithmetic: branchless wins above roughly 6% misprediction rate,
because a `cmov` adds unconditional dependency latency while a predicted branch
is free. Then say you would measure `branch-misses` first.

**"Are virtual functions slow?"**
Monomorphic sites predict well. The real costs are megamorphic BTB pressure and
blocked inlining. Offer type-batching or static dispatch as fixes.

**"Why is sorting the input making my filter faster?"**
Branch predictability, and secondarily locality. This is a classic and you should
recognise it instantly.

**"How expensive is a mispredict?"**
Pipeline depth, 15-20 cycles. Anchor it: "about the cost of an L2 miss."

## Exercises

1. Build the threshold-sum benchmark over 100M random values. Run it on sorted
   and shuffled input. Record `branch-misses` for both with `perf stat`. Then
   rewrite it branchless and rerun both cases. You should see four distinctly
   different numbers; explain each one.
2. Find the crossover empirically: generate input where the branch is taken with
   probability p, sweep p from 0 to 1 in steps of 0.05, and plot branchy vs
   branchless. Find where the lines cross and compare it to the ~6% prediction.
3. Build a megamorphic dispatch loop (5 derived types, random order) and a
   monomorphic one. Measure both. Then sort the work by type and measure again.
4. Overflow the return stack buffer: write a recursive function at depth 8, 16,
   32, 64 and measure per-call cost. Find your RSB size.
5. Read `/sys/devices/system/cpu/vulnerabilities/` on your machine and write one
   paragraph on what each enabled mitigation costs you.
