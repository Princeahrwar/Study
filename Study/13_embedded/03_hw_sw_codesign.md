# EMB 03 — Hardware/software co-design

> Prerequisite: `02_edge_ai_acceleration.md`, `11_hld/01_fundamentals/02_estimation_and_budgets.md`.
> The most senior-flavoured topic in the tree: it is about arguing for the right
> boundary, not implementing either side.

## The question co-design answers

**What belongs in hardware, and what should stay in software?**

Hardware is faster and more power-efficient for a fixed function, and it is
essentially unchangeable for years. Software is flexible and shippable, and it
costs cycles and power. Every interesting embedded system is a negotiation
between those two facts.

The framing that works in an interview: hardware is a bet that the function will
not change. You make that bet when you are confident, and you hedge when you are
not.

## Criteria for pushing something into silicon

```
  PUSH TO HARDWARE when
    - the function is stable and standardised (AES, CRC, FFT, Ethernet MAC)
    - it runs at line rate and the CPU cannot keep up at any clock
    - power per operation matters and is dominated by this function
    - the required latency is below what software can guarantee
    - it is on every packet / sample / pixel

  KEEP IN SOFTWARE when
    - the standard is still evolving, or the policy is a product decision
    - it runs rarely (control plane, configuration, exception handling)
    - it needs field updates after ship
    - the algorithm may need tuning per customer or per deployment
    - you are not yet sure what the right algorithm is
```

The asymmetry to name explicitly: **a wrong hardware decision ships for the life
of the product; a wrong software decision ships a patch.** So the default is
software, and the burden of proof is on hardware.

## The hedge: programmable offload

Between the two extremes sits the most useful category, and naming it is the
sophisticated answer:

```
  fixed-function ASIC block     fastest, least flexible
  microcoded engine             fixed datapath, updatable control (most Wi-Fi MACs)
  programmable pipeline (P4)    match-action stages, reconfigurable
  embedded DSP / NPU            general within a domain
  FPGA                          fully reconfigurable, expensive in power and cost
  CPU with ISA extensions       AES-NI, NEON crypto, CRC32 instructions
  plain CPU                     slowest, infinitely flexible
```

**CPU instruction-set extensions are the underrated option.** AES-NI gives
hardware AES performance with zero hardware design risk and full software
flexibility, because it accelerates the primitive and leaves the protocol in
software. In your domain, ARMv8 crypto extensions do the same for CCMP.
Suggesting "accelerate the primitive, keep the protocol in software" is a strong,
concrete design instinct.

## Worked example: where to put Wi-Fi functions

This is your material; be able to defend every line.

| Function | Where | Why |
| --- | --- | --- |
| PHY modulation / demodulation | Hardware | Sample rate; no CPU can do this |
| CCMP / GCMP encryption | HW block or CPU crypto extensions | Per-packet at line rate, stable standard |
| MAC timing (SIFS, slot, TXOP) | Hardware / microcode | Microsecond-scale deadlines |
| A-MPDU aggregation | Hardware | Per-frame at line rate |
| Rate adaptation | **Software** | Algorithm is a differentiator and evolves |
| Roaming / band steering policy | **Software** | Product policy, changes per customer |
| RRM / channel selection | **Software (controller)** | Slow, global, needs fleet context |
| Packet classification / ACLs | HW TCAM if available, else software | Per-packet, but rules change |

The interesting rows are the software ones. Rate adaptation is per-packet and
latency-sensitive — the naive analysis says hardware — but it is also where
vendors differentiate and where bugs are found for years after ship. That is a
**product** argument for keeping it in software, and making a product argument in
a technical design discussion is exactly what Staff-level means.

## The interface is the design

Once you have split the function, the interface between the halves determines
whether the split works:

```
  Registers        simple, low rate. Every access is a bus round trip
                   (hundreds of ns) — never in a per-packet loop.
  Descriptor ring  the standard high-rate interface. Producer/consumer in
                   shared memory, doorbell to notify, interrupt or poll to
                   complete. Exactly 09_drivers/07_dma.
  Mailbox / queue  for commands with responses.
  Shared state     lowest overhead, hardest to get right: you need the memory
                   ordering discipline from 05_operating_systems/07_memory_barriers on both sides.
```

The rule that generalises: **make the interface batch-friendly.** A hardware
block you must poke once per packet has moved the bottleneck to the interface.
One doorbell per 32 descriptors is a different machine from one per descriptor.

This is the same amortisation argument as syscall batching in
`05_operating_systems/15_user_kernel_boundary`, and drawing that parallel out loud is a good cross-layer move.

## Power

On a PoE-powered AP or a battery device, power is the binding constraint, not
performance.

```
  Energy per operation is what matters, not operations per second.

  Fixed-function hardware:  10-100x better energy/op than a CPU for the same work
  Race to idle:             finish fast, then enter a deep sleep state.
                            Often beats running slowly, because static leakage
                            dominates at low utilisation.
  DVFS:                     dynamic power scales ~V^2*f, so halving frequency
                            and voltage is a large win — but only if you still
                            meet the deadline.
```

"Race to idle versus run slowly" is a genuinely good question to be asked and to
ask. The answer depends on whether static (leakage) or dynamic power dominates,
which depends on the process node and the idle-state depth available. Knowing it
is a real trade rather than a rule is the point.

## Making the argument

How to actually propose a hardware change, which is the Staff-level skill:

1. **Measure first.** Profile and show the function is genuinely the bottleneck
   and how much of the budget it takes.
2. **Bound the win.** Amdahl: if it is 20% of the time, perfect offload gives
   1.25x. Say the number before anyone asks.
3. **Cost the flexibility loss.** What if the standard changes? What is the
   escape hatch — can software take over at reduced performance?
4. **Cost the interface.** The offload only wins if the interface overhead is
   small relative to the work moved.
5. **Propose the hedge.** Can an ISA extension or a microcoded engine get most of
   the win with a fraction of the risk?

Step 2 is where most proposals die and should. An offload that removes 20% of the
work is rarely worth a silicon revision.

## Interview angles

**"What would you put in hardware?"**
Give the criteria, then a concrete example with the Amdahl bound. Emphasise that
stability of the function is the deciding factor.

**"Why is rate adaptation in software?"**
Differentiation, evolution, and field bugs. A product argument, not a technical
one — and say that explicitly.

**"How do the CPU and accelerator communicate?"**
Descriptor ring in shared memory with a doorbell, batch-friendly. Then the
memory-ordering requirements on both sides.

**"Race to idle or run slowly?"**
Depends on static versus dynamic power. Say it is a measurement, not a rule.

**"Your offload gave 5% end to end. What went wrong?"**
Amdahl (it was never a big enough fraction), or the interface overhead ate the
win. Both are worth naming.

## Exercises

1. Profile a real workload of yours and produce the Amdahl bound for offloading
   each of its top three functions. Most will not justify hardware — write down
   why.
2. Compare AES throughput with and without CPU crypto extensions on your ARM
   machine (`openssl speed -evp aes-128-gcm`, with and without
   `OPENSSL_armcap=0`). This is the ISA-extension argument, measured.
3. Design the descriptor-ring interface for a hypothetical offload: fields,
   ownership, doorbell policy, and completion mechanism. Specify the batching.
4. Measure race-to-idle versus a fixed lower frequency for a periodic workload on
   any board with a governor you can control. Report energy, not time.
5. Write a one-page proposal for one hardware change to a system you know,
   following the five steps above. This is a realistic Staff-level artifact and a
   good thing to have written once.
