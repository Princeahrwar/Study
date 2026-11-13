# DRV 05 — Interrupt handling: top halves, bottom halves and NAPI

> Prerequisite: `03_char_driver_and_model.md`, `05_operating_systems/05_process_scheduling.md`.
> Diagram: `diagrams/irq_dma_path.mmd`.

## The constraint that shapes everything

An interrupt handler runs with that interrupt line masked, on whatever core the
device was routed to, **preempting whatever was running there**. While it runs:

- It cannot sleep. No `GFP_KERNEL`, no mutexes, no `copy_*_user`.
- It adds directly to the interrupted thread's latency.
- On the same line, further interrupts are lost or coalesced.

Therefore: **do the minimum in the handler, defer the rest.** Every mechanism
below is a different answer to "defer to where?"

## Registering a handler

```c
/* Classic hard IRQ only. */
ret = request_irq(dev->irq, rk_isr, IRQF_SHARED, "rkdma", dev);

/* Preferred for anything non-trivial: hard IRQ + kernel thread. */
ret = request_threaded_irq(dev->irq,
                           rk_isr,          /* hard IRQ: quick, in atomic ctx */
                           rk_isr_thread,   /* threaded: can sleep, schedulable */
                           IRQF_ONESHOT,    /* keep line masked until thread done */
                           "rkdma", dev);

/* devm_ version: freed automatically on driver detach. */
ret = devm_request_threaded_irq(&pdev->dev, dev->irq, rk_isr, rk_isr_thread,
                                IRQF_ONESHOT, "rkdma", dev);
```

```c
static irqreturn_t rk_isr(int irq, void *data)
{
    struct rk_dev *dev = data;
    u32 status = readl(dev->regs + RK_REG_ISR);

    if (!(status & RK_IRQ_MASK))
        return IRQ_NONE;          /* shared line: not ours — MUST return this */

    writel(status, dev->regs + RK_REG_ISR);   /* acknowledge */
    return IRQ_WAKE_THREAD;                    /* hand off to the threaded half */
}

static irqreturn_t rk_isr_thread(int irq, void *data)
{
    struct rk_dev *dev = data;
    rk_process_completions(dev);               /* may sleep, may take mutexes */
    return IRQ_HANDLED;
}
```

Returning `IRQ_NONE` correctly on a shared line matters: if every driver claims
every interrupt, the kernel cannot detect a stuck line and will eventually
disable the IRQ with a "nobody cared" message.

## The deferral mechanisms, compared

| Mechanism | Context | Can sleep? | Per-CPU? | Use when |
| --- | --- | --- | --- | --- |
| **Hard IRQ** | atomic | no | — | Ack hardware, grab a pointer, wake something |
| **Threaded IRQ** | process | **yes** | no | Default choice for real work |
| **Softirq** | atomic | no | yes | Core networking/block only; you do not add new ones |
| **Tasklet** | atomic | no | serialised per tasklet | Legacy; being phased out |
| **Workqueue** | process | **yes** | configurable | Deferred work that may block |
| **NAPI** | softirq | no | yes | High-rate packet receive |

Guidance for new code, which is what an interviewer wants to hear:

- **Use threaded IRQ by default.** It is simpler than a workqueue, it is
  schedulable so RT priority applies, and it can sleep.
- **Do not write new tasklets.** They are deprecated in favour of threaded IRQs
  and BH workqueues. Knowing they are deprecated is itself a signal of currency.
- **Workqueue** when the deferred work is unrelated to the interrupt's timing —
  a reset, a firmware reload, a slow sysfs update.
- **NAPI** for packet receive, always.

```c
/* Workqueue: the modern form. */
static void rk_reset_work(struct work_struct *w)
{
    struct rk_dev *dev = container_of(w, struct rk_dev, reset_work);
    rk_full_reset(dev);                 /* can sleep freely */
}

INIT_WORK(&dev->reset_work, rk_reset_work);
schedule_work(&dev->reset_work);        /* from anywhere, including IRQ context */

/* Always flush on teardown, or the work can run after your struct is freed: */
cancel_work_sync(&dev->reset_work);
```

That last line is a real bug source: a scheduled work item holding a pointer to a
freed device is a classic use-after-free, and KASAN will catch it if you test for
it.

## Interrupt storms and NAPI

At high packet rates, one interrupt per packet is catastrophic. At 1 Mpps, with
each interrupt costing ~1-2 us of entry, handling and exit, you have spent the
entire machine on interrupt overhead and made no forward progress. Worse, under
enough load the system enters **receive livelock**: it spends all its time taking
interrupts and never runs the code that drains the queue.

NAPI solves this by switching to polling under load:

```c
/* In the hard IRQ: mask the device interrupt and schedule polling. */
static irqreturn_t rk_isr(int irq, void *data)
{
    struct rk_dev *dev = data;
    if (napi_schedule_prep(&dev->napi)) {
        rk_disable_rx_irq(dev);              /* no more interrupts for now */
        __napi_schedule(&dev->napi);
    }
    return IRQ_HANDLED;
}

/* The poll function: bounded work per call so we do not starve other softirqs. */
static int rk_poll(struct napi_struct *napi, int budget)
{
    struct rk_dev *dev = container_of(napi, struct rk_dev, napi);
    int done = 0;

    while (done < budget && rk_have_completion(dev)) {
        rk_handle_one(dev);
        done++;
    }

    if (done < budget) {
        /* Queue drained: go back to interrupt mode. */
        if (napi_complete_done(napi, done))
            rk_enable_rx_irq(dev);
    }
    /* If done == budget we are re-polled; interrupts stay off. */
    return done;
}
```

**The adaptive property is the point:** at low rates you get interrupt-driven low
latency; at high rates you automatically get polling efficiency, with no
configuration. The budget bounds how long one poll can run so other softirqs are
not starved.

This "interrupt at low rate, poll at high rate" structure recurs far beyond the
kernel — it is the same idea as spin-then-sleep in `05_operating_systems/18_ipc_mechanisms.md`, and as adaptive
batching in a telemetry pipeline. Recognising the pattern across layers is a
Staff-level observation.

### Related hardware knobs

- **Interrupt coalescing** in the NIC: fire after N packets or T microseconds.
  `ethtool -c eth0`. Trades latency for CPU.
- **MSI-X**: multiple interrupt vectors per device, so each RX queue gets its own
  vector and can be steered to its own core. This is what makes multi-queue
  scaling work at all.
- **IRQ affinity**: `/proc/irq/<n>/smp_affinity`. Pin each queue's vector to the
  core that will process it, on the right NUMA node (`05_operating_systems/13_numa.md`).

## Synchronisation with interrupt context

If data is shared between process context and a hard IRQ handler on the same CPU,
a plain spinlock deadlocks: the IRQ preempts the lock holder and spins forever.

```c
/* Process context, sharing with a hard IRQ handler: */
unsigned long flags;
spin_lock_irqsave(&dev->lock, flags);      /* disables local interrupts too */
/* ... short critical section ... */
spin_unlock_irqrestore(&dev->lock, flags);

/* Inside the IRQ handler itself, interrupts are already off: */
spin_lock(&dev->lock);
/* ... */
spin_unlock(&dev->lock);

/* Sharing with a softirq/tasklet but not a hard IRQ: */
spin_lock_bh(&dev->lock);
spin_unlock_bh(&dev->lock);
```

Rules to state crisply:

1. **Spinlocks are the only option in atomic context** — mutexes sleep.
2. **Keep critical sections short.** You are spinning other cores.
3. **`_irqsave` in process context, plain `spin_lock` inside the handler.**
4. Lockdep (`CONFIG_PROVE_LOCKING`) catches the wrong variant, and it catches it
   the first time rather than the thousandth.

Note that under `PREEMPT_RT`, most spinlocks become sleeping mutexes and hard IRQ
handlers become threads — so RT changes these rules, and knowing that is worth
saying.

## Measuring

```bash
cat /proc/interrupts                 # counts per IRQ per CPU
watch -n1 'cat /proc/interrupts'     # spot a storm or a badly-affinitised queue
cat /proc/softirqs                   # NET_RX, TIMER, etc.

# Interrupt-off latency and handler duration:
trace-cmd record -e irq:irq_handler_entry -e irq:irq_handler_exit -- sleep 5
trace-cmd report

# Worst-case interrupt-off time, the RT number that matters:
cat /sys/kernel/debug/tracing/available_tracers   # look for irqsoff
echo irqsoff > /sys/kernel/debug/tracing/current_tracer
```

A badly balanced `/proc/interrupts` — all the RX interrupts on CPU0 — is one of
the most common and most fixable production problems in networking, and spotting
it instantly is a nice practical signal.

## Interview angles

**"Why split the handler into top and bottom halves?"**
The top half runs with the line masked and adds to someone's latency. Defer
everything that is not acknowledging hardware.

**"Tasklet vs workqueue?"**
Atomic vs process context, cannot sleep vs can. Then add that tasklets are
deprecated and you would use a threaded IRQ.

**"What is NAPI and what problem does it solve?"**
Interrupt storms and receive livelock; adaptive interrupt-to-poll switching with
a bounded budget.

**"Why `spin_lock_irqsave`?"**
Self-deadlock if the IRQ handler on the same CPU takes the same lock.

**"You see 100% softirq time on CPU0. What's wrong?"**
All NIC interrupts on one core. Check MSI-X, RSS queue count, and IRQ affinity.
Check the NUMA node too.

## Exercises

1. Extend your character driver with an `hrtimer` that fires at 10 kHz and acts
   as a simulated device interrupt, using a threaded IRQ handler. This is the
   Project 02 (`rkdma`) interrupt path.
2. Implement a NAPI-style bounded poll loop on top of it. Sweep the budget from 1
   to 256 and measure both throughput and the latency of an unrelated pinned
   thread on the same core. You will see the trade directly.
3. Deliberately take a plain `spin_lock` in process context on a lock also taken
   by your IRQ handler, with lockdep enabled. Read the report carefully.
4. On any machine with a real NIC, look at `/proc/interrupts` under load, then
   re-affinitise the queues across cores and measure the throughput change.
5. Schedule a work item that touches your device struct, then unload the module
   without `cancel_work_sync`. Reproduce the use-after-free under KASAN, then fix
   it. Doing this once makes the rule permanent.
