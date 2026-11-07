# EMB 01 — RTOS design constraints

> Prerequisite: `05_operating_systems/05_process_scheduling.md`.

## Real-time means predictable, not fast

The definition that matters: **a real-time system is one where being late is
being wrong.** A system that averages 1 us but occasionally takes 10 ms is worse,
for real-time purposes, than one that always takes 500 us.

```
  Hard real-time    a missed deadline is a system failure
                    (motor control, radio MAC timing, airbag)
  Firm real-time    a missed deadline makes the result worthless but is survivable
                    (a video frame, a beamforming update)
  Soft real-time    a missed deadline degrades quality
                    (audio buffer, UI responsiveness)
```

Most of your Wi-Fi work is firm real-time: the 802.11 MAC has timing windows —
SIFS, slot times, TXOP boundaries — where late is the same as never.

The consequence for engineering: **you optimise the worst case, not the average.**
Almost every technique below is a worst-case-bounding technique that makes the
average slightly worse.

## Priority inversion

The classic failure, and the one that famously nearly ended the Mars Pathfinder
mission:

```
  Low priority task L  takes mutex M
  High priority task H  blocks on M
  Medium priority task M2  preempts L (it is higher priority than L)
  => H waits for M2, which is unbounded. H's deadline is now unbounded.
```

Two standard solutions:

- **Priority inheritance.** While H is blocked on M, L temporarily inherits H's
  priority, so M2 cannot preempt it. `PTHREAD_PRIO_INHERIT`.
- **Priority ceiling.** Every mutex has a ceiling equal to the highest priority of
  any task that can take it; a task holding it runs at that ceiling. Prevents the
  inversion rather than correcting it, and also prevents deadlock, at the cost of
  reduced concurrency.

```c
pthread_mutexattr_t attr;
pthread_mutexattr_init(&attr);
pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
pthread_mutex_init(&m, &attr);
```

Be able to state the difference: inheritance is reactive, ceiling is preventive.

## Worst-case execution time

WCET is the upper bound on how long a piece of code can take. It is what a
schedulability argument is built from, and it is genuinely hard on modern
hardware because everything that makes the average fast makes the worst case
unpredictable:

```
  Caches           a miss is 100x a hit; cold cache after preemption
  Branch predictor mispredicts cost pipeline depth
  TLB              a miss adds a page walk
  DVFS             the core may be at a lower frequency
  Interrupts       arbitrary preemption
  Memory refresh   DRAM refresh cycles stall access
```

So real-time systems deliberately give up performance for predictability:

- **Disable or partition caches.** Cache locking, or Intel CAT / ARM MPAM to
  reserve cache ways per critical task.
- **Lock memory** with `mlockall(MCL_CURRENT | MCL_FUTURE)` — never take a page
  fault on the critical path.
- **Pre-allocate everything.** No `malloc` after initialisation. Dynamic
  allocation has unbounded worst-case latency and can fragment.
- **Pin frequency.** `performance` governor, disable deep C-states.
- **Isolate cores.** `isolcpus`, `nohz_full`, `rcu_nocbs`, IRQ affinity —
  the full set from `05_operating_systems/05_process_scheduling`.

## Schedulability

For periodic tasks, you can prove deadlines will be met.

**Rate-monotonic (fixed priority, shorter period = higher priority):**

```
  Sum(C_i / T_i) <= n * (2^(1/n) - 1)

  n=1: 100%   n=2: 82.8%   n=3: 78.0%   n->inf: 69.3%
```

Sufficient but not necessary — a set that fails this bound may still be
schedulable, which is what response-time analysis checks exactly.

**Earliest-deadline-first (dynamic priority):**

```
  Sum(C_i / T_i) <= 1.0     (100% utilisation, optimal)
```

EDF achieves full utilisation but degrades unpredictably on overload (a single
overrun can cascade), while rate-monotonic degrades predictably — the lowest
priority task misses first. That trade is the standard interview follow-up, and
it is why safety-critical systems often prefer fixed priority despite the lower
bound.

Linux's `SCHED_DEADLINE` implements EDF with admission control: it refuses to
admit a task set it cannot schedule. That connection between the theory and a
real Linux syscall is worth making.

## Memory management

```c
/* Fixed-size block pool. Deterministic O(1) alloc and free, no fragmentation.
 * This is what an RTOS gives you instead of malloc. */
typedef struct {
    void   *free_list;
    uint8_t storage[POOL_BLOCKS][BLOCK_SIZE];
} pool_t;

/* alloc: pop the head of the free list. Bounded, always. */
static inline void *pool_alloc(pool_t *p)
{
    void *b = p->free_list;
    if (b) p->free_list = *(void **)b;
    return b;                     /* NULL on exhaustion — the caller MUST handle it */
}

static inline void pool_free(pool_t *p, void *b)
{
    *(void **)b = p->free_list;
    p->free_list = b;
}
```

The rule: **allocate everything at init, never in the steady state.** Exhaustion
is handled by a documented policy — drop, degrade, or reset — not by growing.
This is the same discipline as the object pool in `10_lld/02_systems_lld/03_thread_safe_primitives`, for the same reason.

## Interrupt latency

The chain that determines your worst case:

```
  interrupt asserted
    -> longest interrupts-disabled region anywhere in the system   <- usually dominates
    -> CPU vectoring and context save
    -> ISR prologue
    -> your handler runs
```

Note what dominates: **the longest interrupt-disabled region in the entire
system**, including code you did not write. One driver holding interrupts off for
200 us sets the floor for everyone. This is why PREEMPT_RT's work is largely
about shrinking those regions, and why `ftrace`'s `irqsoff` tracer exists.

```bash
echo irqsoff > /sys/kernel/debug/tracing/current_tracer
cat /sys/kernel/debug/tracing/trace       # the longest irqs-off region, with a stack
```

## RTOS versus Linux

| | RTOS (FreeRTOS, Zephyr, ThreadX) | Linux + PREEMPT_RT |
| --- | --- | --- |
| Worst-case latency | ~1-10 us | ~10-100 us |
| Memory footprint | KB | MB |
| Drivers / networking | you write them | enormous existing stack |
| MMU / protection | often none | full |
| Certifiability | tractable | hard |

The realistic modern answer is often **both**: an RTOS or bare-metal core for the
hard-real-time loop and Linux on another core for everything else, communicating
through shared memory — AMP, as on most Wi-Fi SoCs where a small core runs the
MAC timing and the application processor runs Linux. Describing that split is
exactly the kind of thing your background makes credible.

## Interview angles

**"What makes a system real-time?"**
Bounded worst case, not low average. Give the 1 us average / 10 ms outlier
example.

**"Explain priority inversion and two fixes."**
Inheritance (reactive) and ceiling (preventive). Mention Pathfinder if it fits.

**"Why no malloc in real-time code?"**
Unbounded worst case and fragmentation. Offer fixed-size pools.

**"Rate-monotonic vs EDF?"**
69.3% vs 100% utilisation; RM degrades predictably, EDF cascades. Mention
`SCHED_DEADLINE`.

**"What dominates interrupt latency?"**
The longest interrupts-disabled region anywhere in the system. Then name the
`irqsoff` tracer.

## Exercises

1. Construct priority inversion with three `SCHED_FIFO` threads and a plain
   mutex. Measure the high-priority thread's delay. Enable `PTHREAD_PRIO_INHERIT`
   and measure again.
2. Compute the rate-monotonic bound for a task set of your own design, then
   verify by simulation whether it actually meets deadlines. Find a set that
   fails the bound but is still schedulable.
3. Write a fixed-block pool allocator and compare its worst-case latency against
   `malloc` over a million operations. Plot the tails, not the means.
4. Use the `irqsoff` tracer to find the longest interrupts-disabled region on
   your system. Identify which driver it is in.
5. Run `cyclictest` on a stock kernel and on a tuned one (isolcpus, nohz_full,
   performance governor, C-states disabled). Report both maxima. This number is
   worth carrying into interviews.
