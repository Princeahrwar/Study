# 04 — Distributed caching

> Prerequisite: `01_estimation_and_budgets.md`.

## Start by justifying the cache

A cache is a correctness liability you accept for a latency win. So the first
questions are: what is the hit rate going to be, what is the cost of a stale
read, and what happens when the cache is empty?

If the hit rate is 50%, you have added a hop and a consistency problem for half
the requests. Caches earn their place at 90%+, and the way to predict that is the
access distribution: caching works when access is **skewed** (Zipf-like), which
most real workloads are, and does not when it is uniform.

## Placement

```
  client / browser     free, uncontrolled, hard to invalidate
  CDN / edge           great for static; geographic locality
  in-process (L1)      ~100 ns, no network, but N copies to invalidate
  distributed (L2)     ~0.5 ms, shared, one source of truth
  database buffer pool free, already there, often forgotten
```

The two-tier pattern — small in-process L1 in front of a shared L2 — is the
standard shape. L1 absorbs the hot keys at ~100 ns; L2 gives a shared, larger
cache at ~0.5 ms. The cost is that L1 invalidation is now a distributed problem,
usually solved with a short TTL rather than real invalidation.

## Patterns

### Cache-aside (lazy loading)

```
  read:   check cache -> miss -> read DB -> populate cache -> return
  write:  write DB -> INVALIDATE cache (do not update it)
```

The default, and the one to name first. Two details that matter:

- **Invalidate on write, do not update.** Updating races: two concurrent writers
  can leave the cache holding the older value. Invalidation is idempotent and
  safe. This is a genuinely good detail to volunteer.
- Cache only what is read more than once. Populating on every miss pollutes the
  cache with single-use entries.

### Read-through / write-through / write-behind

- **Read-through**: the cache itself loads on miss. Cleaner application code, and
  the cache library owns the stampede protection.
- **Write-through**: write to cache and DB synchronously. Cache is never stale;
  writes are slower.
- **Write-behind**: write to cache, flush to DB asynchronously. Fast writes, and
  **you will lose data on a crash**. Only for data where that is acceptable —
  counters, view tallies, last-seen timestamps.

## The three failure modes you must know

### Cache stampede (thundering herd)

A hot key expires. A thousand concurrent requests miss simultaneously and all hit
the database.

```
  Fix 1  Per-key lock / single-flight: the first miss loads, the rest wait.
  Fix 2  Probabilistic early expiry: each reader independently refreshes with
         probability rising as the TTL approaches, so one refreshes early and
         alone.
  Fix 3  Serve stale while revalidating in the background.
```

Fix 2 is elegant and under-known: `if (now - delta * beta * log(rand()) >= expiry)
refresh()`. Mentioning it by name (XFetch / probabilistic early recomputation) is
a nice touch.

### Cache penetration

Requests for keys that **do not exist** always miss and always hit the database.
Trivially weaponised by an attacker requesting random ids.

```
  Fix 1  Cache the negative result, with a short TTL.
  Fix 2  Bloom filter of existing keys in front of the cache.
```

### Cache avalanche

Many keys expire at the same moment — typically because they were all populated
together at startup or after a deploy.

```
  Fix    Jitter the TTLs. ttl = base + rand(0, base * 0.1)
```

Jitter appears in this tree three times now (retry backoff, device reporting,
cache TTL). The general principle — **never let independent actors synchronise** —
is worth stating as a principle, because it shows pattern recognition.

## Consistent hashing

The mechanism for distributing keys across nodes such that adding or removing one
node moves only `1/N` of the keys, rather than remapping everything.

```
  hash each node to several points on a 2^32 ring   (virtual nodes)
  hash each key to a point
  key belongs to the first node clockwise from it

  virtual nodes (100-200 per physical node) matter because:
    - they smooth the load distribution
    - they spread a departing node's keys across ALL survivors, not just one
```

Without virtual nodes, removing a node dumps its entire share onto its single
clockwise neighbour, which then falls over — a cascading failure. That failure
mode is the reason virtual nodes exist, and explaining it is better than just
naming the technique.

**Rendezvous hashing (HRW)** is the alternative: hash `(key, node)` for every
node and pick the highest. Simpler, no ring, equally good distribution, at O(N)
per lookup instead of O(log N). For small N it is strictly nicer and worth
knowing as an answer to "any alternative?"

## Eviction

| Policy | Cost | Good for |
| --- | --- | --- |
| LRU | O(1), but every read is a write | General purpose |
| LFU | O(1) with a counter | Stable hot sets |
| CLOCK / second-chance | O(1), **reads stay read-only** | Concurrent caches |
| ARC | adaptive | Mixed patterns, patent history |
| TinyLFU / W-TinyLFU | small sketch | Modern default (Caffeine) |
| Random | free | Surprisingly decent |

The point from `lld/06` is worth repeating because it is the practical one:
**exact LRU makes reads into writes**, which serialises a concurrent cache. CLOCK
approximates LRU with a reference bit and keeps reads lock-free. That is why
production caches rarely implement textbook LRU.

## Interview angles

**"Design a distributed cache."**
Placement and tiering, cache-aside, consistent hashing with virtual nodes,
eviction, and the three failure modes with fixes.

**"Why invalidate rather than update on write?"**
Concurrent writers can reorder and leave a stale value; invalidation is
idempotent.

**"A hot key expires and the database falls over. Fix it."**
Single-flight, probabilistic early expiry, or serve-stale-while-revalidate.

**"Why virtual nodes?"**
Load smoothing, and preventing a departing node's load from landing entirely on
one neighbour.

**"What's the hit rate going to be, and how do you know?"**
Access skew. If the distribution is uniform, argue against the cache.

## Exercises

1. Implement consistent hashing with virtual nodes. Measure key movement when
   adding a 5th node to a 4-node ring, with 1 and with 150 virtual nodes each.
   Plot the distribution.
2. Implement rendezvous hashing and compare distribution quality and lookup cost
   at N = 5, 50 and 500.
3. Build the stampede: 1000 threads, one key, TTL expiry. Then implement all
   three fixes and measure database load for each.
4. Implement CLOCK eviction and compare its hit rate against exact LRU on a
   Zipf-distributed trace. Then compare throughput at 8 threads — CLOCK should
   lose slightly on hit rate and win decisively on throughput.
5. Take a real access log you have and compute its skew. Predict the hit rate for
   a cache holding 1%, 5% and 10% of the key space. Then simulate and check your
   prediction.
