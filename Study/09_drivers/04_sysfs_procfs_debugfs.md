# DRV 04 — sysfs, procfs, debugfs and tracing interfaces

> Prerequisite: `03_char_driver_and_model.md`.

## Choosing the right interface

Getting this choice right is a review-level judgement, and interviewers do ask.

| Interface | Rule | Stability | Use for |
| --- | --- | --- | --- |
| **sysfs** | **One value per file**, text | Stable ABI — you must keep it | Device attributes, config, counters |
| **procfs** | Process info; legacy for anything else | Stable for existing files | Do not add new non-process files |
| **debugfs** | Anything you like | **No stability guarantee** | Debugging, dumps, internal state |
| **configfs** | Userspace *creates* kernel objects | Stable | Target/object creation |
| **netlink** | Async events, bulk config | Versioned | Events, complex config |
| **ioctl** | Per-device commands | You define it | Operations, not attributes |
| **tracepoints** | Static instrumentation | Semi-stable | Observability at rate |

The two rules that catch people out:

1. **sysfs is one value per file.** A file that prints ten statistics is a review
   rejection. If you need structured or bulk data, use debugfs, netlink, or a
   tracepoint.
2. **sysfs is ABI.** Once a userspace tool reads your attribute, you cannot rename
   or remove it. debugfs exists precisely so you can be sloppy somewhere.

## sysfs attributes

```c
#include <linux/sysfs.h>

/* Read-only counter. One value, no formatting beyond the number. */
static ssize_t rx_packets_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct rk_dev *rk = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%llu\n", atomic64_read(&rk->rx_packets));
}
static DEVICE_ATTR_RO(rx_packets);

/* Read-write tunable, with validation. */
static ssize_t ring_depth_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct rk_dev *rk = dev_get_drvdata(dev);
    return sysfs_emit(buf, "%u\n", READ_ONCE(rk->ring_depth));
}

static ssize_t ring_depth_store(struct device *dev, struct device_attribute *attr,
                                const char *buf, size_t count)
{
    struct rk_dev *rk = dev_get_drvdata(dev);
    unsigned int val;
    int ret = kstrtouint(buf, 0, &val);
    if (ret)
        return ret;
    if (val < 64 || val > 8192 || !is_power_of_2(val))
        return -EINVAL;

    ret = rk_resize_ring(rk, val);
    return ret ? ret : count;          /* return the byte count on success */
}
static DEVICE_ATTR_RW(ring_depth);

/* Group them so they are created and removed atomically. */
static struct attribute *rk_attrs[] = {
    &dev_attr_rx_packets.attr,
    &dev_attr_ring_depth.attr,
    NULL,
};
ATTRIBUTE_GROUPS(rk);

/* Attach to the driver so the core creates them at probe and removes them at
 * detach — no manual create/remove, and no window where they are missing. */
static struct platform_driver rk_driver = {
    .driver = {
        .name = "rkdma",
        .dev_groups = rk_groups,
    },
    /* ... */
};
```

Details a reviewer checks: `sysfs_emit` rather than `sprintf` (it bounds-checks
against PAGE_SIZE), `kstrtouint` rather than `simple_strtoul` (deprecated, no
error reporting), returning `count` on success from a store, and using
`dev_groups` rather than manual creation.

### Making an attribute pollable

```c
/* In the driver, when the value changes: */
sysfs_notify(&dev->kobj, NULL, "link_state");

/* In userspace: poll() on the attribute fd returns on POLLPRI|POLLERR. */
struct pollfd pfd = { .fd = open(path, O_RDONLY), .events = POLLPRI | POLLERR };
poll(&pfd, 1, -1);
lseek(pfd.fd, 0, SEEK_SET);
read(pfd.fd, buf, sizeof(buf));
```

This is an underused and elegant mechanism: an event notification with no new
ABI, no netlink, and no ioctl. Knowing it exists is a nice small signal.

## debugfs

```c
#include <linux/debugfs.h>

static int rk_ring_show(struct seq_file *s, void *unused)
{
    struct rk_dev *dev = s->private;
    seq_printf(s, "head %u tail %u depth %u\n",
               dev->head, dev->tail, dev->ring_depth);
    for (unsigned i = 0; i < dev->ring_depth; i++)
        seq_printf(s, "  [%4u] flags=%08x len=%u\n",
                   i, dev->ring[i].flags, dev->ring[i].len);
    return 0;
}
DEFINE_SHOW_ATTRIBUTE(rk_ring);

/* In probe: */
dev->dbg = debugfs_create_dir("rkdma", NULL);
debugfs_create_file("ring", 0444, dev->dbg, dev, &rk_ring_fops);
debugfs_create_u32("irq_count", 0444, dev->dbg, &dev->irq_count);

/* In remove: */
debugfs_remove_recursive(dev->dbg);
```

`seq_file` handles pagination for you, so output larger than one page works
correctly — writing that by hand with `copy_to_user` is a classic source of
truncation bugs.

debugfs functions deliberately do not return errors you must check
(`debugfs_create_*` returns an ERR_PTR you may ignore), because debugfs may be
disabled entirely in the kernel config. Your driver must work without it.

## Tracepoints

For anything on a hot path, `printk` is unusable — it serialises, it allocates,
and it will change the timing you are trying to measure. Tracepoints are the
answer: near-zero cost when disabled (a patched-out jump), structured binary
output when enabled.

```c
/* include/trace/events/rkdma.h */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rkdma

#if !defined(_TRACE_RKDMA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RKDMA_H
#include <linux/tracepoint.h>

TRACE_EVENT(rkdma_desc_complete,
    TP_PROTO(u32 index, u32 len, u64 dma_addr),
    TP_ARGS(index, len, dma_addr),
    TP_STRUCT__entry(
        __field(u32, index)
        __field(u32, len)
        __field(u64, dma_addr)
    ),
    TP_fast_assign(
        __entry->index    = index;
        __entry->len      = len;
        __entry->dma_addr = dma_addr;
    ),
    TP_printk("idx=%u len=%u dma=0x%llx",
              __entry->index, __entry->len, __entry->dma_addr)
);
#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE rkdma
#include <trace/define_trace.h>
```

```c
/* In the driver hot path: */
trace_rkdma_desc_complete(idx, len, dma);
```

```bash
# Enable and read:
echo 1 > /sys/kernel/debug/tracing/events/rkdma/rkdma_desc_complete/enable
cat /sys/kernel/debug/tracing/trace_pipe

# Or with a filter, which runs in the kernel — no userspace cost for skipped events:
echo 'len > 1500' > /sys/kernel/debug/tracing/events/rkdma/rkdma_desc_complete/filter
```

The Project 02 (`rkdma`) requirement to prove zero copies "by a tracepoint, not by
inspection" is exactly this mechanism.

## ftrace and bpftrace, from the outside

```bash
# Who calls what, with timings:
trace-cmd record -p function_graph -g rk_isr_thread -- sleep 5
trace-cmd report

# Existing tracepoints anywhere in the kernel:
cat /sys/kernel/debug/tracing/available_events | grep dma

# bpftrace one-liners worth memorising:
bpftrace -e 'kprobe:rk_start_transfer { @ = count(); }'
bpftrace -e 'tracepoint:rkdma:rkdma_desc_complete { @len = hist(args->len); }'
bpftrace -e 'kprobe:vfs_read /comm == "myapp"/ { @[kstack] = count(); }'
```

Being able to write a bpftrace one-liner from memory in an interview is a strong,
concrete signal that you debug rather than guess.

## Interview angles

**"How would you expose driver statistics?"**
sysfs for individual counters (one per file, stable ABI), debugfs for bulk dumps,
tracepoints for anything per-packet. Say why each.

**"Why not just printk?"**
Serialises, allocates, perturbs timing, unbounded volume. Tracepoints are
near-free when off.

**"What is sysfs's contract?"**
One value per file, text, and it becomes ABI once shipped.

**"How does userspace learn about an asynchronous event?"**
`poll()` on a sysfs attribute with `sysfs_notify`, netlink for richer events, or
an eventfd/ioctl for a private interface.

**"How do you debug a driver in production?"**
Tracepoints with in-kernel filters, bpftrace, and dynamic debug. Emphasise you
would not need a rebuild.

## Exercises

1. Add a sysfs group to your driver with one RO counter and one RW tunable with
   validation. Attach it via `dev_groups` and confirm the attributes appear and
   disappear with bind/unbind.
2. Implement `sysfs_notify` for a state change and write a userspace program that
   `poll()`s the attribute. This is a surprisingly satisfying mechanism.
3. Add a debugfs `seq_file` that dumps your whole ring. Make the ring big enough
   that the output exceeds one page and verify no truncation.
4. Define the tracepoint above and use it to count descriptors. Then add an
   in-kernel filter and verify with `perf stat` that filtered-out events cost you
   nothing measurable.
5. Write three bpftrace one-liners against your own driver: a call count, a
   latency histogram, and a stack trace aggregation. Keep them in
   `labs/bpftrace/` as a personal reference.
