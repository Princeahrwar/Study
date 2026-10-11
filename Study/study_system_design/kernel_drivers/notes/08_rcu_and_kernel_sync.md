# 08 — RCU and kernel synchronisation

> Prerequisite: `os_and_arch/10_memory_barriers.md`, `03_interrupt_handling.md`.
> RCU is the concept that most distinguishes kernel concurrency from userspace
> concurrency, and it is a frequent deep-dive topic.

## The primitive menu

| Primitive | Sleeps? | Readers concurrent? | Writer cost | Use |
| --- | --- | --- | --- | --- |
| `spinlock_t` | no | no | low | Short sections, any context |
| `mutex` | yes | no | low | Longer sections, process context |
| `rwlock_t` | no | yes | high | Rarely the right answer |
| `rw_semaphore` | yes | yes | medium | Long read-mostly sections |
| `seqlock_t` | no (readers retry) | yes | very low | Read-mostly, small data |
| **RCU** | no (readers) | yes | deferred | **Read-mostly, pointer-based** |
| `atomic_t` | no | yes | low | Counters, flags |
| `percpu` counters | no | yes | ~free | High-rate statistics |

`rwlock_t` is on the list mainly so you know to avoid it: readers still write the
shared lock word, so they contend with each other exactly as tutorial 03
describes. It usually performs worse than a plain spinlock and much worse than
RCU. Saying that is a small, real signal.

## RCU: the idea

**Readers pay nothing. Writers pay everything, and pay it later.**

Readers never block, never spin, and on non-preemptible kernels
`rcu_read_lock()` compiles to *nothing at all*. Writers publish a new version by
swapping a pointer, then wait until every pre-existing reader has finished before
freeing the old version.

The genius is the definition of "finished". RCU does not track readers. It
observes that a reader in an RCU read-side critical section cannot be
context-switched (on classic RCU), so **once every CPU has passed through a
context switch, no reader can still hold the old pointer.** That observation is
called a *grace period*, and it makes reader cost zero.

```c
/* ---- Reader ---- */
rcu_read_lock();
struct rk_config *cfg = rcu_dereference(dev->config);   /* pairs with publish */
use(cfg->depth, cfg->mode);
rcu_read_unlock();
/* cfg must not escape this section. */

/* ---- Writer ---- */
struct rk_config *old, *new;

new = kzalloc(sizeof(*new), GFP_KERNEL);
new->depth = depth;
new->mode  = mode;

mutex_lock(&dev->cfg_mutex);            /* writers still serialise with each other */
old = rcu_dereference_protected(dev->config, lockdep_is_held(&dev->cfg_mutex));
rcu_assign_pointer(dev->config, new);   /* release store: contents before pointer */
mutex_unlock(&dev->cfg_mutex);

synchronize_rcu();                      /* wait for a grace period — can SLEEP */
kfree(old);                             /* now provably unreachable */
```

Two pairings to state explicitly, because that is what gets probed:

- `rcu_assign_pointer` is a **release** store: the new object's contents are
  visible before the pointer is.
- `rcu_dereference` is a **dependency-ordered** load: the reader cannot see the
  pointer without also seeing the contents it points to.

They are the release/acquire pair from `os_and_arch/10` in kernel clothing.

### Asynchronous reclamation

`synchronize_rcu()` blocks, sometimes for milliseconds. When you cannot block, or
do not want to:

```c
struct rk_config {
    struct rcu_head rcu;
    /* ... */
};

static void rk_config_free(struct rcu_head *head)
{
    struct rk_config *cfg = container_of(head, struct rk_config, rcu);
    kfree(cfg);
}

call_rcu(&old->rcu, rk_config_free);    /* returns immediately, frees later */

/* For the common "just kfree it" case: */
kfree_rcu(old, rcu);
```

`call_rcu` from an interrupt handler is fine; `synchronize_rcu` from one is a bug.

### RCU-protected lists

```c
/* Reader: */
rcu_read_lock();
list_for_each_entry_rcu(entry, &dev->flow_list, node) {
    if (entry->key == key) { found = entry; break; }
}
rcu_read_unlock();

/* Writer, under its own lock: */
list_add_rcu(&new->node, &dev->flow_list);
list_del_rcu(&old->node);
call_rcu(&old->rcu, flow_free);
```

### RCU's limits

Be able to state these; uncritical enthusiasm for RCU is a tell:

- **Read-mostly only.** If writes are frequent, grace periods and the copying
  dominate and RCU loses to a spinlock.
- **Readers see a stale version.** That is by design. If you need a reader to see
  the very latest value, RCU is the wrong tool.
- **Memory grows** between the update and the grace period. A burst of updates
  can queue a lot of pending frees, which is why `rcu_barrier()` exists and why
  module unload must call it.
- **Sleeping in a reader is forbidden** on classic RCU. SRCU exists for when you
  must, at higher reader cost.

Variants worth knowing by name: `srcu` (sleepable readers), `rcu_bh` and
`rcu_sched` (merged into one flavour in modern kernels), and `rcu_nocbs` — the
boot parameter from tutorial 09 that moves callback processing off your isolated
cores.

## Seqlock: the other read-mostly pattern

When the protected data is small and copyable, and readers can afford to retry:

```c
/* Writer: */
write_seqlock(&dev->stats_lock);
dev->stats.rx_packets++;
dev->stats.rx_bytes += len;
write_sequnlock(&dev->stats_lock);

/* Reader: retries if a write happened during the read. */
unsigned seq;
struct rk_stats snapshot;
do {
    seq = read_seqbegin(&dev->stats_lock);
    snapshot = dev->stats;          /* must be a plain copy — no pointers */
} while (read_seqretry(&dev->stats_lock, seq));
```

Writers are cheap and readers are free unless they collide. The catch is that
readers can **livelock** under a constant stream of writers, so seqlock suits
"occasionally written, frequently read" — which is exactly a statistics block.
`jiffies` and the vDSO clock data are protected this way.

Trap worth naming: the reader must not follow pointers into data that a writer
could free, because it may be reading a torn state. Copy plain data only.

## Per-CPU variables

```c
#include <linux/percpu.h>

static DEFINE_PER_CPU(u64, rx_count);

/* Fast path: no atomics, no contention — the variable is yours. */
this_cpu_inc(rx_count);

/* Aggregation, when someone asks: */
u64 total = 0;
int cpu;
for_each_possible_cpu(cpu)
    total += per_cpu(rx_count, cpu);
```

This is tutorial 03's per-core counter pattern, provided by the kernel. Note
`get_cpu()`/`put_cpu()` disable preemption around the access if you need the
pointer to stay valid — `this_cpu_*` handles that internally, which is why it is
preferred.

## Lockdep

```
CONFIG_PROVE_LOCKING=y
```

Lockdep records the order in which locks are acquired and builds a dependency
graph. If it ever sees A-then-B and B-then-A, it reports a potential deadlock —
**even if the deadlock never actually occurs in your test**. It also catches
taking a sleeping lock in atomic context and the wrong `spin_lock` variant
against an IRQ handler.

Development kernels should always have it on. Mentioning it as your answer to
"how do you avoid deadlocks?" is much stronger than "careful design", because it
is a mechanism rather than an intention.

## Interview angles

**"What is RCU and why is it fast?"**
Readers pay nothing; writers publish a new version and defer reclamation until a
grace period proves no reader holds the old one. Then define grace period.

**"When is RCU the wrong choice?"**
Write-heavy workloads, readers needing the newest value, memory pressure from
deferred frees.

**"What does `rcu_assign_pointer` actually do?"**
A release store, so contents are visible before the pointer.

**"seqlock vs RCU?"**
seqlock for small copyable data with cheap writes and retrying readers; RCU for
pointer-based structures with non-retrying readers. Mention seqlock reader
livelock.

**"How do you count packets at 10 Mpps?"**
Per-CPU counters, summed on read. Never a shared atomic — and you can quote the
5-7x false-sharing measurement from `os_and_arch/03` to back it.

## Exercises

1. Convert a spinlock-protected config struct in your driver to RCU. Measure
   reader cost before and after with a tight read loop on 4 cores.
2. Build an RCU-protected hash table of flows and benchmark 99% reads / 1% writes
   against a `rwlock_t` version. Then shift to 50/50 and observe RCU lose.
   Producing the crossover point yourself is the lesson.
3. Implement the seqlock statistics block above and then write a pathological
   reader/writer ratio that livelocks the reader. Measure how bad it gets.
4. Deliberately create an ABBA lock ordering with lockdep enabled and read the
   report. Learn to read the "possible circular locking dependency" output.
5. Unload a module with `call_rcu` callbacks still pending and without
   `rcu_barrier()`. Reproduce the crash under KASAN, then fix it.
