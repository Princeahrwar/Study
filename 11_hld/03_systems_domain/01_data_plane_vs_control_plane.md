# HLD-D 01 — Data plane versus control plane

> Prerequisite: `11_hld/01_fundamentals/02_estimation_and_budgets.md`.
> The organising principle of networking and infrastructure design, and the
> concept your Wi-Fi background makes you credible on.

## The separation

```
  CONTROL PLANE                      DATA PLANE
  decides what should happen         does it, for every packet/request
  low rate (per config change)       high rate (per event)
  can be slow, complex, stateful     must be fast, simple, bounded
  consistency matters                throughput and latency matter
  failure = stale policy             failure = dropped traffic
```

The rule that falls out: **nothing on the control plane may block the data plane,
and the data plane must keep working when the control plane is entirely down.**

In your world: the AP forwards frames whether or not the controller is reachable.
That property is not an accident; it is the design.

## Why the separation is worth the complexity

1. **Different failure domains.** Controller outage should degrade
   manageability, not connectivity.
2. **Different scaling.** Data plane scales with traffic, control plane with
   configuration change rate and device count. They are unrelated.
3. **Different consistency needs.** A forwarding decision must be fast and may be
   slightly stale. A policy change must be correct and may be slow.
4. **Different code characteristics.** The data plane is small, audited, and
   allocation-free. The control plane can be a large, garbage-collected service.

## How policy reaches the data plane

This is the crux, and the interesting engineering.

```
  control plane           data plane
  ---------------------------------------------------
  compiles policy   -->   installs into a table
  (slow, complex)         (lookup only, O(1), no branching on policy shape)
```

The pattern: **the control plane pre-computes; the data plane looks up.** A flow
table, a routing FIB, an eBPF map, a compiled ACL bitmap. The data plane never
evaluates rules — it indexes.

Updating a live table without stopping traffic is where the lock-free work from
`10_lld/02_systems_lld/04_lockfree_data_structures` pays off:

```
  RCU / epoch swap    Build the new table, publish the pointer with a release
                      store, free the old one after a grace period. Readers never
                      block and never see a torn state. This is the standard
                      answer, and it is the same mechanism as kernel RCU.

  Versioned lookup    Each entry carries a generation; readers filter by the
                      current generation. Allows incremental update without a
                      full rebuild.

  Double buffering    Two tables, an atomic index. Simple and bounded, at 2x
                      memory.
```

Say "RCU-style pointer swap with epoch-based reclamation" and you have answered
the question completely.

## Fast path and slow path

Within the data plane there is a second split, and it mirrors the first:

```
  fast path    established flow: table hit, forward. Bounded work, no allocation.
  slow path    first packet of a flow: classify, consult policy, install an entry,
               then forward. Allocation and locks allowed here.
```

The security consequence matters and is a great thing to raise: **an attacker
will try to make every packet take the slow path.** A flood of single-packet
flows from spoofed sources fills your table and forces classification on
everything. Defences: a bounded slow-path budget per interval, SYN-cookie-style
statelessness until a flow is validated, and per-source rate limits on flow
creation.

This is precisely the question Palo Alto asks, and the connection to the keyed
hashing in `10_lld/02_systems_lld/04_lockfree_data_structures` is direct — mention both together.

## Real examples to cite

| System | Control plane | Data plane |
| --- | --- | --- |
| Your Wi-Fi platform | Controller: config, RRM, roaming policy | AP: frame forwarding, encryption |
| SDN / OpenFlow | Controller computes flows | Switch ASIC matches and forwards |
| Kubernetes | API server, scheduler, controllers | kube-proxy / eBPF, CNI datapath |
| Envoy / service mesh | xDS control plane | Proxy request path |
| Linux networking | `ip route`, netlink | FIB lookup in `ip_route_input` |
| Storage | Metadata / placement service | Chunk servers streaming bytes |

Being able to name the split in three different systems shows the concept
generalises for you rather than being Wi-Fi-specific.

## Designing the control plane

Properties that matter, and each is a follow-up question:

- **Declarative, not imperative.** Push desired state; let the device reconcile.
  Imperative commands lose their meaning after a reboot or a missed message.
- **Versioned config with monotonic generations,** so a device can say "I have
  generation 47" and the controller can diff.
- **Idempotent application**, so a retried push is harmless.
- **Canary cohorts**: 1% of devices, then 10%, then everything, with automatic
  rollback on a health regression. A fleet-wide bad config push is the classic
  catastrophic outage.
- **Staged rollback that does not require the controller** — the device keeps the
  last-known-good and reverts on its own if the new config fails health checks.
  This is the property that turns a fleet-wide outage into a blip.

That last point is the one that impresses. A controller-driven rollback is
useless in exactly the scenario where you need it — when the new config broke
connectivity to the controller.

## Failure behaviour

```
  control plane down     data plane keeps forwarding with last-known state
                         config changes queue; devices keep working
                         management and visibility degrade

  data plane down        traffic stops. This is the outage.
                         control plane should detect and reroute

  partition              devices must decide locally: fail-open or fail-closed?
```

**Fail-open versus fail-closed is a product decision, not a technical one**, and
saying so is a mature answer. A Wi-Fi AP that loses its controller should keep
serving clients (fail-open). A firewall that loses its policy server should
probably stop passing new traffic (fail-closed). Ask which one the interviewer
wants before you design for it.

## Interview angles

**"Why separate control and data planes?"**
Failure domains, independent scaling, different consistency needs. Then give a
concrete example from your own work.

**"How do you update a forwarding table without dropping packets?"**
RCU-style pointer swap with epoch reclamation. Mention double buffering as the
simpler alternative and its 2x memory cost.

**"What happens when the controller dies?"**
Data plane continues on last-known state; describe the degradation explicitly.

**"How do you prevent a bad config from taking down the fleet?"**
Canary cohorts, health-gated promotion, and device-side automatic revert that
does not depend on the controller.

**"How would an attacker exploit your fast/slow path split?"**
Force everything onto the slow path. Then give the three defences.

## Exercises

1. Draw the control and data planes of the system you work on today, marking
   which crossings are synchronous. Any synchronous control-plane call on the
   data path is a finding — write up why it is there.
2. Implement a double-buffered forwarding table and an RCU-style one. Measure
   reader cost and update cost for both at 4 readers.
3. Design the config push protocol for 50,000 devices: versioning, idempotency,
   canary, device-side revert. Write it as a one-page spec. This is Project 05 (`telemetrix`)'s
   control plane.
4. Build the slow-path attack against your own flow table from `10_lld/02_systems_lld/04_lockfree_data_structures`: flood
   single-packet flows and measure the degradation. Then add a slow-path budget
   and measure again.
5. Write down the fail-open/fail-closed decision for three systems you know, with
   the reasoning. It is a good five-minute exercise and it gives you examples to
   quote.
