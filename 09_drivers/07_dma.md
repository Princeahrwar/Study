# DRV 07 — DMA: coherent, streaming, and the stale-data failure modes

> Prerequisite: `05_interrupt_handling.md`, `04_computer_organization/19_cache_coherence.md`.
> This is the core of Project 02 (`rkdma`) and a favourite Nvidia interview topic.

## The three address spaces

The single most important idea in DMA is that **the device does not see the same
addresses you do.**

```
   Virtual address    what your code uses          (MMU translates)
   Physical address   what the CPU puts on the bus
   Bus / DMA address  what the DEVICE must be given (IOMMU translates)
```

On a simple embedded SoC, bus address equals physical address. With an IOMMU, or
an offset-mapped bus, it does not. **Never hand a device a `virt_to_phys()`
result** — always use the DMA API, which returns a `dma_addr_t` that is correct
for that device on that platform.

Getting this wrong produces a driver that works on one board and silently
corrupts memory on another. It is exactly the kind of portability trap
interviewers probe for.

## Coherent versus streaming

### Coherent (consistent) mappings

```c
dma_addr_t dma_handle;
void *cpu_addr = dma_alloc_coherent(&pdev->dev, size, &dma_handle, GFP_KERNEL);
/* ... device and CPU may both access freely, no explicit syncs needed ... */
dma_free_coherent(&pdev->dev, size, cpu_addr, dma_handle);
```

The kernel arranges that CPU and device always see the same data. On x86 the
hardware is cache-coherent so this is ordinary memory. On many ARM systems it is
mapped **uncached**, which means every CPU access goes to DRAM.

That is the trade: no sync calls, but potentially very slow CPU access. So use
coherent memory for **small, frequently-shared control structures** — descriptor
rings, doorbell-adjacent state, index words — and never for bulk payload.

### Streaming mappings

```c
/* Map an existing buffer for one transfer, in one direction. */
dma_addr_t dma = dma_map_single(&pdev->dev, buf, len, DMA_TO_DEVICE);
if (dma_mapping_error(&pdev->dev, dma))       /* ALWAYS check */
    return -ENOMEM;

rk_start_transfer(dev, dma, len);
/* ... device reads ... */

dma_unmap_single(&pdev->dev, dma, len, DMA_TO_DEVICE);

/* Scatter-gather for fragmented buffers: */
int n = dma_map_sg(&pdev->dev, sgl, nents, DMA_FROM_DEVICE);
/* n may be LESS than nents — the IOMMU can coalesce. Iterate with for_each_sg
 * over the returned count, and keep the ORIGINAL nents for unmap. */
dma_unmap_sg(&pdev->dev, sgl, nents, DMA_FROM_DEVICE);
```

Streaming mappings keep the memory cached and perform explicit cache maintenance
at map/unmap time. Use them for bulk data.

**The ownership rule is absolute**: between `dma_map_*` and `dma_unmap_*`, the
buffer belongs to the **device**. The CPU must not touch it. Violating this is
the most common DMA bug, and on a non-coherent platform it silently corrupts data
rather than faulting.

### Keeping a mapping and syncing

```c
/* For a long-lived buffer you reuse (an RX ring slot): */
dma_sync_single_for_cpu(&pdev->dev, dma, len, DMA_FROM_DEVICE);
/* ... CPU reads the received data — invalidates stale cache lines first ... */
dma_sync_single_for_device(&pdev->dev, dma, len, DMA_FROM_DEVICE);
/* ... device owns it again ... */
```

Direction matters because the cache operation differs:

| Direction | On map/for_device | On unmap/for_cpu |
| --- | --- | --- |
| `DMA_TO_DEVICE` | **clean** (flush dirty lines out) | nothing |
| `DMA_FROM_DEVICE` | invalidate | **invalidate** (drop stale lines) |
| `DMA_BIDIRECTIONAL` | clean | clean + invalidate |

Declaring the wrong direction is another silent-corruption bug, and being able to
recite this table is a strong driver-interview answer.

## The classic failure: "DMA completed but I read stale data"

This is *the* DMA interview question. There are five distinct causes and a good
answer enumerates them:

1. **Missing cache invalidate.** On a non-coherent platform, the CPU has stale
   lines for the buffer. Fix: `dma_sync_single_for_cpu` with `DMA_FROM_DEVICE`.
2. **Missing cache clean before a device read.** CPU wrote through cache, lines
   still dirty, device read DRAM. Fix: sync `for_device` with `DMA_TO_DEVICE`.
3. **Missing memory barrier.** Descriptor fields written after — from the
   device's perspective — the doorbell that told it to look. On ARM this happens
   for real. Fix: `dma_wmb()` between the descriptor write and the doorbell.
4. **Reading the descriptor before the device finished writing it.** Need
   `dma_rmb()` between reading the ownership flag and reading the rest.
5. **Buffer touched while owned by the device**, violating the ownership rule.

```c
/* Producing a descriptor — the ordering that matters: */
desc->addr = cpu_to_le64(dma_addr);
desc->len  = cpu_to_le32(len);
dma_wmb();                                  /* all descriptor writes visible first */
desc->flags = cpu_to_le32(DESC_OWN_DEVICE); /* ... then hand over ownership */
writel(1, dev->regs + RK_REG_DOORBELL);     /* ... then ring the bell */

/* Consuming a completed descriptor: */
u32 flags = le32_to_cpu(READ_ONCE(desc->flags));
if (!(flags & DESC_OWN_CPU))
    return false;
dma_rmb();                                  /* do not read the body before the flag */
len = le32_to_cpu(desc->len);
```

Note `dma_wmb()` and not `smp_wmb()`. The `smp_*` family orders CPU-to-CPU
visibility and compiles away on UP builds; the `dma_*` family orders CPU access
against a *device* and does not. That distinction is precisely the kind of detail
that separates someone who has written a driver from someone who has read about
one.

## DMA masks

```c
/* Tell the core how many address bits the device can generate. Do this in
 * probe, before any mapping. */
ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
if (ret) {
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "no usable DMA mask\n");
        return ret;
    }
}
```

If the device can only address 32 bits and the buffer is above 4 GiB, the kernel
must **bounce** it: allocate a low buffer and copy. Correct, but it silently
halves your throughput and adds a copy you thought you had eliminated. "My
zero-copy driver is doing memcpy" is usually a DMA mask problem, and that is a
great diagnostic story.

## IOMMU

An IOMMU sits between device and memory, translating bus addresses through its
own page tables. Two reasons it exists:

- **Protection.** A buggy or malicious device cannot DMA over arbitrary kernel
  memory. This is what makes device assignment to VMs and userspace drivers
  (VFIO) safe.
- **Contiguity.** Scattered physical pages can be presented to the device as one
  contiguous bus range, so `dma_map_sg` may return fewer entries than you passed.

Costs: an IOTLB miss requires a page walk, and map/unmap operations are
relatively expensive. High-rate drivers therefore keep mappings alive and reuse
them rather than mapping per packet, and `iommu.strict=0` trades some safety for
deferred invalidation. Knowing that the IOMMU has a TLB of its own, with the same
reach arithmetic as `05_operating_systems/12_virtual_memory_and_tlb.md`, is a nice connection to draw.

## Exposing the ring to userspace

Project 02 (`rkdma`)'s payoff: map the DMA buffer into a process so there are no copies at
all.

```c
static int rk_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct rk_dev *dev = filp->private_data;
    size_t size = vma->vm_end - vma->vm_start;

    if (size > dev->ring_bytes)
        return -EINVAL;
    if (vma->vm_pgoff != 0)
        return -EINVAL;

    /* For coherent memory, the DMA API provides the correct mapping,
     * including any uncached attribute the platform requires. */
    return dma_mmap_coherent(dev->dev, vma, dev->ring_cpu,
                             dev->ring_dma, size);
}
```

Then the SPSC ring discipline from `05_operating_systems/08_memory_barriers` applies across the
kernel/user boundary, with the kernel as producer.

## Interview angles

**"A DMA transfer completes but the CPU reads stale data. Every possible cause."**
The five above. Enumerating them in order, with the fix for each, is a complete
answer and few candidates give it.

**"Coherent vs streaming — when do you use each?"**
Small shared control structures vs bulk payload; the uncached-on-ARM trade.

**"What does the IOMMU buy and cost?"**
Protection and contiguity; IOTLB misses and map/unmap cost.

**"Why `dma_wmb()` and not `smp_wmb()`?"**
Device visibility, not CPU-to-CPU. `smp_*` compiles out on UP.

**"How do you get packets to userspace without a copy?"**
mmap the DMA ring, publish indices with release/acquire, and keep the ownership
discipline. This is Project 02 (`rkdma`) in one sentence.

## Exercises

1. Add a coherent descriptor ring plus streaming payload buffers to your driver.
   Instrument with a tracepoint per descriptor and confirm zero `memcpy` on the
   data path.
2. Deliberately omit `dma_sync_single_for_cpu` on a non-coherent platform (an ARM
   board, or force it with `dma-noncoherent` in devicetree) and observe the
   corruption. This is the single most instructive bug in the whole kernel track.
3. Set a 32-bit DMA mask on a machine with more than 4 GiB and confirm bounce
   buffering with `swiotlb` statistics in dmesg. Measure the throughput loss.
4. Implement `rk_mmap` and write a userspace reader that consumes the ring.
   Verify with `perf` that no copy appears in the profile.
5. Write down the barrier placement for your descriptor ring and justify each one
   in a paragraph. Then remove one and stress-test on ARM until it fails.
