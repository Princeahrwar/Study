# Nvidia

## Centre of gravity

The most fundamentals-first loop on your list. Expect to **derive, not recall**.
Less LeetCode-hard than Meta or Google; far more "explain this mechanism and its
cost." C semantics, memory hierarchy, DMA, PCIe, driver internals, and hard
debugging stories.

This is the company where the `04_computer_organization/`, `05_operating_systems/`, `08_kernel/` and `09_drivers/` tracks pay off
most directly, and where a measured benchmark from your own machine is worth more
than any amount of system-design vocabulary.

## Prepare

- `04_computer_organization/09_pipelining_and_hazards` through `12_inside_a_modern_core`, and the cache and coherence notes after them — all of it, properly.
- `09_drivers/05_interrupt_handling` and `04` — interrupts and DMA especially.
- **Project 02 (`rkdma`)** is the single highest-value artifact for this loop.
- The CUDA memory model as a *mapping exercise* from what you know:
  warp ≈ SIMD lane group, shared memory ≈ software-managed L1, coalescing ≈ the
  cache-line argument from `04_computer_organization/13_cache_basics`, section 4.1, occupancy ≈ hiding latency with
  parallelism (the MLP idea from `04_computer_organization/11_superscalar_and_out_of_order_execution`, section 8).
- Be able to draw the memory hierarchy and put real numbers on every arrow.

## Signature questions

- "A DMA transfer completes but the CPU reads stale data. Give me every possible
  cause." (`09_drivers/07_dma` — the five causes)
- "Walk me through what happens on a cache miss. Now a TLB miss."
- "Why is this loop slow?" with a code snippet. They want the four Cs and a
  counter that discriminates them.
- "How would you debug a driver that hangs under load once a day?"
- "Explain coalesced vs uncoalesced memory access." (It is the cache-line
  argument in GPU clothing.)

## Your angle

You have measured this hardware. Lead with numbers: the ARM-vs-x86 hugepage
finding from `05_operating_systems/12_virtual_memory_and_tlb` is exactly the kind of thing that lands here,
because it shows you verify rather than repeat.

## Trap

Do not bluff CUDA depth if you have not written CUDA. Say "I haven't written CUDA
kernels; here is how I would map my knowledge of the memory hierarchy onto it" —
and then do it well. That is a better outcome than a shallow answer confidently
delivered, and they will test the confident answer.
