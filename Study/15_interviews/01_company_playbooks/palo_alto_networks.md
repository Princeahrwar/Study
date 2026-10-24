# Palo Alto Networks

## Centre of gravity

Single-pass data plane thinking. Flow and session tables at line rate, zero-copy,
DPDK-class userspace networking, and — distinctively — **adversarial assumptions**
about every data structure. They will ask what an attacker does to your design.

## Prepare

- `07_networking/07_zero_copy_networking` — the zero-copy ladder, cold.
- `10_lld/02_systems_lld/03_thread_safe_primitives` and `10_lld/02_systems_lld/04_lockfree_data_structures` — rings, lock-free tables, epoch reclamation.
- `11_hld/03_systems_domain/01_data_plane_vs_control_plane` — data plane vs control plane, and especially the **fast path / slow
  path attack**.
- **Project 03 (`pfilt`)** is close to a take-home for this org. Bring a measured
  pps figure, a p99.9, and the keyed-hashing story.

## Signature questions

- "Your flow table is hash-based. I control the source addresses. What happens?"
  (Keyed hashing / SipHash. Have this instant.)
- "How do you update a rule table without dropping packets?" (RCU-style pointer
  swap, epoch reclamation.)
- "How do you get to 10 Mpps?" (Climb the ladder; do the 100 ns/packet
  arithmetic first.)
- "What happens when your session table fills?" (Bounded slow-path budget,
  eviction policy, and what you count.)
- "Where would you put this — XDP, AF_XDP, or DPDK?" (Have the trade ready.)

## Your angle

You already run a data plane in production. The phrase "I own a production data
plane" travels well here. Combine it with the adversarial framing: when you
describe a structure, volunteer its attack surface before they ask.

## Trap

Do not present a design without its overload behaviour. For this org, "what
happens when it is full / under attack / at 10x" is not a follow-up, it is the
question.
