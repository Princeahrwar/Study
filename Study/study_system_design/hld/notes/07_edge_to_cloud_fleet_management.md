# 07 — Edge-to-cloud device management

> Prerequisite: `02_data_plane_vs_control_plane.md`, `06_telemetry_pipelines.md`.
> The design round where your Wi-Fi background is a genuine advantage.

## What fleet management means

Four capabilities, and they have different consistency and failure requirements:

```
  1. Inventory     what devices exist, what they are, where, what version
  2. Config        push desired state, converge, verify
  3. Software      OTA updates, safely, at scale
  4. Observability telemetry, health, remote diagnostics
```

The unifying constraint: **devices are unreliable, intermittently connected, and
you cannot physically reach them.** Every design decision follows from that. A
bricked device 3,000 km away is a truck roll.

## Connectivity model

Devices are usually behind NAT, so the cloud cannot initiate. The device
connects outbound and holds the connection.

```
  device -> outbound persistent connection (TLS, WebSocket, gRPC stream, MQTT)
         -> keepalive to survive NAT timeouts (typically 30-60 s)
         -> cloud pushes commands down the same connection

  50,000 persistent connections:
     ~10-50 KB kernel memory per connection => 0.5-2.5 GB per terminating node
     one node can hold ~50-100k connections comfortably
     => a handful of connection-terminating nodes, stateless, behind a NLB
```

Keep the connection tier **stateless**: it terminates TLS and relays. Device
state lives in a store. Then losing a node reconnects devices elsewhere with no
state loss — and you must jitter those reconnects, or a node failure becomes a
thundering herd on the survivors.

## Identity and trust

Non-negotiable, and interviewers in security-adjacent orgs will push here:

- **Per-device identity**, ideally a key in a TPM or secure element, provisioned
  at manufacture. A shared fleet credential is one extraction away from total
  compromise.
- **Mutual TLS**: the device verifies the cloud, the cloud verifies the device.
- **Short-lived tokens** derived from the device certificate for API calls.
- **Revocation** that works: a stolen device must be excludable, and CRL/OCSP at
  fleet scale is its own problem — short-lived credentials are usually the
  practical answer.
- **Signed everything**: config, firmware, commands. The device verifies before
  applying, so a compromised control plane cannot brick the fleet.

That last point is the one worth volunteering: **signing is what limits the blast
radius of a compromised control plane.**

## Configuration: desired state, not commands

```
  BAD    "set channel to 36"          imperative; meaningless after a reboot,
                                      lost if the message is missed
  GOOD   desired_state { channel: 36, power: 20, ssid: [...] }  version: 47
                                      device reconciles, idempotent, restartable
```

The reconciliation loop, which is the same pattern as Kubernetes controllers:

```
  device: report current_state + current_version
  cloud:  compare against desired_state
  cloud:  send desired_state (full, or a diff for bandwidth)
  device: apply, verify, report new current_state
  cloud:  mark converged, or alert on repeated failure to converge
```

Properties that matter: **idempotent** (re-applying is harmless), **convergent**
(the device retries until it matches), and **observable** (you can query how many
devices are on which version). "Convergence lag" — the p50 and p99 time from push
to converged — is the metric to name.

## Safe rollout

The mechanism that prevents a career-defining outage:

```
  1. Canary        1% of devices, chosen to be diverse (not all one site,
                   one hardware revision, or one firmware version)
  2. Soak          hold for a real interval — hours, not minutes. Many failures
                   only appear after a client association cycle or a reboot
  3. Health gate   automatic promotion only if metrics stay within bounds:
                   connectivity, crash rate, client count, convergence rate
  4. Staged        1% -> 5% -> 25% -> 100%, with a gate at each step
  5. Halt          any regression stops the rollout fleet-wide, automatically
```

And the property that makes it survivable:

```
  6. Device-side auto-revert
     The device keeps last-known-good. After applying a new config or image,
     it runs a self-test and starts a watchdog. If it cannot reach the cloud,
     or fails its health check, it reverts on its own and reports why.
```

**Point 6 is the one that matters most and the one candidates miss.** A
cloud-driven rollback is useless in precisely the scenario you need it: the bad
config broke the device's connectivity to the cloud. Autonomous revert turns a
fleet-wide brick into a blip. Say this unprompted.

## OTA updates

Harder than config because a failure can be unrecoverable.

```
  A/B partitions:
     - two slots; write the new image to the inactive one
     - verify signature and checksum BEFORE switching
     - set the bootloader to try the new slot ONCE
     - on successful boot, the application confirms and makes it permanent
     - on failure, the bootloader falls back automatically after N attempts
```

The essential detail is the **one-shot boot flag**: the bootloader tries the new
slot once; only a booted, healthy system marks it permanent. A device that hangs
mid-boot reverts by itself without anyone intervening.

Other things worth naming:

- **Delta updates** (bsdiff-style) to cut bandwidth — significant at 50,000
  devices and real egress cost.
- **Peer distribution** or local caching at a site, so 200 APs at one location do
  not each pull the full image over the same WAN link.
- **Bandwidth and time windows**: do not update an office AP at 2 pm.
- **Power-failure safety**: the write must be atomic with respect to loss of
  power at any instant. This is why A/B beats in-place.

## Scale and multi-tenancy

```
  Shard by tenant, then by device.
  Per-tenant quotas on API calls, telemetry volume, and series cardinality.
  Noisy-neighbour isolation: one tenant's 50,000-device reboot must not delay
  another tenant's config push.
  Regional deployment: devices connect to the nearest region; control data
  replicates asynchronously. Do NOT put a cross-region round trip on the
  device's critical path (100 ms floor, per hld/01).
```

## Observability of the fleet itself

The meta-question, and a good one to raise: how do you know the fleet is healthy?

- **Convergence rate**: what fraction is on the desired version, and the p99 lag.
- **Connectivity**: devices connected now versus expected, by site and by
  version.
- **Crash and revert rate** by firmware version — the leading indicator of a bad
  rollout.
- **The dog that did not bark**: devices that have stopped reporting are the most
  important signal and the easiest to miss, because absence does not generate an
  event. Alert on missing heartbeats, not just on bad ones.

That last point is worth stating as a principle: **monitor for absence, not only
for errors.**

## Interview angles

**"Design a system to manage 1 million IoT devices."**
Connection model and its memory arithmetic, identity, desired-state config,
staged rollout with device-side revert, OTA with A/B, and fleet observability.

**"How do you avoid bricking the fleet?"**
Canary with health gates, A/B partitions with one-shot boot, and autonomous
device-side revert that does not depend on the cloud.

**"A device has been offline for a month. What happens when it returns?"**
It reports its version, the cloud diffs against desired state, and it converges —
possibly through several intermediate versions if upgrades are not
version-skipping. Jittered, rate-limited.

**"How do you push config to 50,000 devices in 10 seconds?"**
Push the trigger, not the payload: notify over the existing connection, devices
fetch from a CDN with jitter. Then say why 10 seconds is usually the wrong
requirement.

**"What's the hardest part?"**
Safe rollout. Everything else is recoverable; a bad image on 50,000 unreachable
devices is not.

## Exercises

1. Size the connection tier for 1 M devices: memory per connection, nodes needed,
   reconnect storm behaviour after a node loss. Include the jitter calculation.
2. Design the desired-state schema and the reconciliation protocol for your own
   product. One page, including versioning and convergence reporting.
3. Implement A/B update with a one-shot boot flag on any Linux board or in a VM.
   Force a failure mid-boot and confirm automatic revert.
4. Build the staged rollout controller: cohorts, health gate, automatic halt.
   Simulate a bad config and measure how many devices were affected before the
   halt — that number is the thing the design exists to minimise.
5. Write the fleet health dashboard spec: the five metrics you would put on it
   and the alert thresholds, including the missing-heartbeat alert.
