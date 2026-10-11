# How to use this study system

## What this is

A complete written curriculum for moving from Senior SWE (Wi-Fi platforms, C/C++,
embedded Linux) to Senior/Staff Systems roles at Google, Nvidia, Meta, Microsoft,
Cisco and Palo Alto Networks.

Every tutorial in this tree is written to be read once carefully, then used as a
reference. They are not summaries. They assume you already write C and C++ for a
living and already know what an interrupt is. What they add is the layer above
that: the arithmetic, the failure modes, the vocabulary, and the specific way
these topics get probed in a Staff-level loop.

## The tree

    00_roadmap/          this file, the curriculum, the capstone specs
    lld/00_foundations/  Focus Area 0 — OOP, SOLID, UML, design patterns, the
                          machine-coding framework, AI scope for LLD
    lld/01_machine_coding_problems/  the interview problem set, worked in full
    os_and_arch/         Focus Areas 1 and 2 — architecture, OS internals, memory
    kernel_drivers/      Focus Area 3 — modules, interrupts, DMA, zero-copy
    lld/notes/            Focus Area 4 — systems LLD: modern C++, concurrency, lock-free
    hld/                 Focus Area 5 — distributed systems, telemetry, storage
    embedded_ai/         Focus Area 6 — RTOS, edge inference, HW/SW co-design
    interviews/          company playbooks, mock bank, behavioral framework
    capstones/           the three portfolio projects
    toolchain/           QEMU images, cross-compilers, perf scripts

Each `notes/` directory holds the tutorials, numbered in dependency order.
Each `labs/` directory is where you run things. Each `benchmarks/` directory
holds measurement code with committed results.

## How to actually study this

The failure mode with material this dense is reading it, feeling informed, and
retaining nothing. Three rules prevent that.

**1. Every tutorial ends with exercises. Do them before moving on.**
Reading a tutorial takes 30-60 minutes. Doing its exercises takes 2-4 hours.
The exercises are where the learning is; the tutorial is just the setup.

**2. Write the summary from memory, with the file closed.**
After each tutorial, write `<same-number>_summary.md` beside it in your own
words without looking. Whatever you cannot reconstruct is what you re-read
tomorrow. This is the single highest-leverage habit in the whole system.

**3. Measure, do not trust.**
Many tutorials include benchmarks. Run them on your own hardware. Your ARM
machine will disagree with the x86 numbers in the text, and the places where it
disagrees are the most valuable paragraphs you will produce in this whole
program — because nobody else in the interview pool will have them.

## Suggested order

There is no time limit on this, so depth beats speed. The dependency order is:

0. `lld/00_foundations/` then `lld/01_machine_coding_problems/` — no
   prerequisites. Fastest path to a working interview signal, and the
   recommended starting point if you're new to formal OOD/patterns.
1. `os_and_arch/` 01 through 06 — the hardware model. Everything else assumes it.
2. `os_and_arch/` 07 through 12 — the OS layer on top of that hardware.
3. `kernel_drivers/` — needs both of the above. Start Capstone 1 here.
4. `lld/notes/` — needs Focus Area 0 and the memory model from
   `os_and_arch/10`. Start Capstone 2 here.
5. `hld/` — largely independent. You can interleave this with anything.
6. `embedded_ai/` — needs `os_and_arch` and a little `hld`.
7. `interviews/` — continuous. Do one mock per week from the first week.

If you want a faster path to a specific company, read
`interviews/company_playbooks/` first and let it reorder the list for you.

## The one habit that matters most

Say it out loud. After every tutorial, explain the topic to an empty room on a
timer, as if the interviewer just asked. The gap between "I understand this" and
"I can explain this under time pressure with someone watching" is enormous, and
it only closes by rehearsal. Record yourself occasionally. It is unpleasant and
it works.
