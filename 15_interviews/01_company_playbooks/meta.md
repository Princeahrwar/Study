# Meta

## Centre of gravity

Coding rounds are fast and dense — often two problems in one session — and graded
partly on how little you flail. Infra and kernel orgs bolt on a systems deep
dive. Design rounds weight scale reasoning and pragmatic trade-offs.

## Prepare

- Coding speed. This is the loop where raw problem-solving throughput matters
  most on your list. Practise under a strict clock.
- `10_lld/02_systems_lld/02_cpp_memory_model` through `10_lld/02_systems_lld/04_lockfree_data_structures` — concurrency questions are common in infra orgs.
- `07_networking/07_zero_copy_networking` — **eBPF and XDP fluency is the highest-leverage thing you
  can add**, and it sits one step from what you already do.
- `11_hld/01_fundamentals/15_rate_limiters` through `11_hld/03_systems_domain/02_telemetry_pipelines`.

## Signature questions

- "Count distinct flows per second across a fleet. Now do it with bounded
  memory." (HyperLogLog, or a sketch. Know one.)
- Two medium/hard algorithm problems back to back, 20 minutes each.
- "Design a service that ingests a million events per second."
- "This lock-free queue — is it correct?" with subtly wrong memory orders.

## Your angle

"I own a production data plane" plus demonstrated eBPF/XDP work. The combination
of low-level credibility and distributed-systems vocabulary is rarer than either
alone.

## Trap

Under-preparing the coding rounds because the systems material feels more
relevant. It is not how this loop is scored. Budget real time for timed practice.
