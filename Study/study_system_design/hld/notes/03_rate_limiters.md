# 03 — Rate limiting and admission control

> Prerequisite: `01_estimation_and_budgets.md`. Algorithm code is in `lld/06`.

## What you are actually protecting

Rate limiting is **admission control**: deciding what to reject so the rest
survives. The first question in any rate-limiting design is therefore not "which
algorithm" but **"what resource am I protecting, and what happens when it is
exhausted?"**

A system without admission control does not degrade gracefully under overload —
it collapses, because queues grow, latency rises, clients retry, and the retries
add load. That feedback loop is the thing you are preventing.

## The algorithms

| Algorithm | Memory/key | Bursts | Accuracy | Use |
| --- | --- | --- | --- | --- |
| Fixed window | 1 counter | 2x at boundary | poor | Crude quotas only |
| Sliding log | O(requests) | exact | exact | Low rate, high value |
| Sliding window counter | 2 counters | good | approximate | The usual default |
| Token bucket | 2 values | configurable | good | APIs, per-client limits |
| Leaky bucket | 1 queue | none — smooths | good | Shaping outbound traffic |
| GCRA | 1 timestamp | configurable | exact | Same as token bucket, less state |

**Fixed window's failure is worth knowing precisely**: with a limit of 100/minute,
a client can send 100 at 11:59:59 and 100 at 12:00:00 — 200 requests in one
second while never violating the stated limit. That example is the standard
follow-up and you should have it ready.

**Token bucket vs leaky bucket** is the distinction people blur. Token bucket
allows a burst up to the bucket size then sustains the fill rate — right for APIs,
where a client legitimately batches. Leaky bucket emits at a constant rate and
never bursts — right for shaping traffic you send to a fragile downstream.

**GCRA** (generic cell rate algorithm) deserves a mention because it achieves
token-bucket behaviour with a single timestamp per key instead of two mutable
values, which matters when you have 10 million keys. Knowing it exists is a small
differentiator.

## Where to enforce

```
  client SDK       cheapest, but untrusted. Good for politeness, not protection.
  edge / LB        stops load before it costs you anything. Coarse keys only.
  API gateway      per-API-key limits, good visibility, one hop in.
  service          per-tenant fairness, but the request already cost you.
  database         last resort, and by then you are already in trouble.
```

Layer them. The edge stops floods; the service enforces fairness. A single layer
is either too coarse or too late.

## Distributed rate limiting

The hard part. Three designs, and the trade is always coordination cost versus
accuracy:

### 1. Central store (Redis)

```lua
-- Atomic token bucket in Redis. Atomicity matters: read-modify-write from the
-- application races between instances.
local key      = KEYS[1]
local rate     = tonumber(ARGV[1])
local capacity = tonumber(ARGV[2])
local now      = tonumber(ARGV[3])
local cost     = tonumber(ARGV[4])

local state   = redis.call('HMGET', key, 'tokens', 'ts')
local tokens  = tonumber(state[1]) or capacity
local last    = tonumber(state[2]) or now

tokens = math.min(capacity, tokens + (now - last) * rate)
local allowed = tokens >= cost
if allowed then tokens = tokens - cost end

redis.call('HMSET', key, 'tokens', tokens, 'ts', now)
redis.call('EXPIRE', key, math.ceil(capacity / rate) * 2)
return allowed and 1 or 0
```

Accurate, but adds a network round trip (~0.5 ms) to every request and makes
Redis a hard dependency on your request path. Ask: what happens when Redis is
down? Fail-open (accept everything, risk overload) or fail-closed (reject
everything, guaranteed outage)? Almost always fail-open with a local fallback —
and saying so shows you have run something.

### 2. Local buckets with periodic reconciliation

Each instance gets `limit / N` and enforces locally with no coordination.
Periodically, instances report usage and quota is redistributed toward the ones
that need it.

Fast — no round trip — and approximate. It over-limits when traffic is unevenly
distributed across instances, which is exactly when sticky routing or a hot
tenant is involved. Usually the right default, and mentioning the uneven-traffic
weakness before they ask is the move.

### 3. Consistent-hash the key to one owner

Each key is owned by exactly one instance, which enforces it exactly. No
coordination on the common path, but you need routing to the owner and a
rebalance story when instances change. Good when the client can be routed by key
anyway.

## Response behaviour

```
  HTTP 429 Too Many Requests
  Retry-After: 30
  X-RateLimit-Limit: 1000
  X-RateLimit-Remaining: 0
  X-RateLimit-Reset: 1700000000
```

Two things that matter more than the status code:

- **Tell the client when to come back.** A 429 without `Retry-After` produces a
  retry storm, which makes overload worse.
- **Require jitter in client backoff.** Without it, every rejected client retries
  at the same moment and you get a synchronised thundering herd. Exponential
  backoff *with full jitter* is the standard, and naming jitter explicitly is a
  small, real signal of operational experience.

## Beyond rate limiting: load shedding and backpressure

Rate limiting caps a client. **Load shedding** protects the system regardless of
who is asking, and it is the more Staff-level concept:

- **Shed by priority.** Drop telemetry before dropping control traffic. Requires
  that requests carry a criticality class.
- **Shed by queue depth or latency**, not just rate. If your p99 exceeds the
  budget, start rejecting — the rate that is "too much" depends on what else is
  happening.
- **CoDel-style** queue management: measure the *sojourn time* of requests in the
  queue and drop when it persists above a target. Far better than a fixed queue
  length, because it adapts to changing service time.
- **Backpressure** is the honest version: rather than dropping, tell the upstream
  to slow down. Credit-based flow control end to end, as in Capstone 3.

The sentence worth having: **"The only honest response to sustained overload is
backpressure; dropping is what you do when you cannot apply backpressure."**

## Fairness

With a shared limit, one aggressive tenant can starve everyone. Options:

- **Per-tenant limits**, simple and requires knowing tenants in advance.
- **Weighted fair queueing**: each tenant gets a queue, served in proportion to
  weight. Bounded memory per tenant, real isolation.
- **Deficit round robin**: WFQ's practical O(1) approximation, and what switches
  actually implement. Worth naming given your background.

## Interview angles

**"Design a rate limiter for an API."**
Ask: per what key, what limit, distributed, burst tolerance, failure mode. Then
token bucket, then where to enforce, then the distributed design.

**"What's wrong with fixed windows?"**
The 2x boundary burst, with the 11:59:59 example.

**"How do you rate limit across 50 servers?"**
Three designs with their trades. Say which you would pick and why, and what
happens when the coordination store is down.

**"A client is being rejected but retrying instantly. What do you do?"**
`Retry-After`, exponential backoff with jitter, and possibly a circuit breaker.

**"Rate limiting vs load shedding vs backpressure?"**
Per-client cap, system-wide protection, and upstream flow control. Having all
three distinctions crisp is a strong differentiator.

## Exercises

1. Implement all four core algorithms (fixed window, sliding window counter,
   token bucket, leaky bucket) and drive them with the same bursty trace. Plot
   admitted vs rejected for each. The differences become obvious visually.
2. Build the fixed-window boundary attack and demonstrate the 2x burst.
3. Implement local-bucket distributed limiting across 4 processes and measure the
   error versus a central Redis implementation under even and skewed traffic.
4. Implement CoDel-style shedding on a queue and compare it against a fixed
   queue-length limit under a varying service-time workload.
5. Write the admission-control section of Capstone 3: what gets shed first at 2x
   overload, and the argument for why that is the right thing to lose.
