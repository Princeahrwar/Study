# 05 — LRU Cache

> Difficulty: foundational, and consistently one of the single most-asked
> LLD/machine-coding problems across every company in this program — it
> also doubles as a pure-DSA question (LeetCode 146) with an O(1)
> requirement, so it's asked in both machine-coding *and* coding-round
> formats. Unlike the previous four problems, the core challenge here is
> **data structure design**, not a GoF pattern — a good example of when
> *no* pattern is the right answer (see `00_foundations/17`'s KISS
> section).

## Step 1 — Clarify

Assumed answers:

- Fixed capacity, set at construction.
- `get(key)` returns the value if present (and marks it most-recently-used),
  or a sentinel/`-1`/`optional` if absent.
- `put(key, value)` inserts or updates; if inserting past capacity, evicts
  the **least recently used** entry first.
- **Both operations must run in O(1) average time** — say this requirement
  back explicitly, because it's the entire reason the data structure is
  interesting; a `std::map`-only solution is O(log n) and will visibly not
  satisfy it.
- **Deferred, explicitly**: thread-safety (covered in "Extensions," cross-
  referencing `00_foundations/10_concurrency_for_lld.md`) and TTL-based
  expiry (a different, larger problem — Tier 3, #19 in the roadmap).

## Step 2 — Entities

The whole problem is really one question: **what data structure gives O(1)
lookup by key AND O(1) "move to most-recently-used" AND O(1) "evict
least-recently-used"?**

- O(1) lookup by key → a hash map.
- O(1) move-to-front / evict-from-back, with O(1) removal of an arbitrary
  node → a **doubly linked list** (singly linked can't remove an arbitrary
  node in O(1) without already having its predecessor).

**The construction that makes both O(1) simultaneously**: a hash map from
key → **iterator into a doubly linked list** that holds the actual
`(key, value)` pairs in recency order. The list's front is most-recently-used,
its back is least-recently-used.

| Entity | Responsibility |
| --- | --- |
| `LRUCache` | Owns the hash map and the list; the only public interface |
| `std::list<std::pair<Key,Value>>` | Recency-ordered storage, front = MRU, back = LRU |
| `std::unordered_map<Key, list::iterator>` | O(1) lookup, giving direct splice access into the list |

There's no class diagram with multiple collaborators here worth drawing —
correctly naming the composition of two standard containers **is** the
design. Recognising "no pattern needed, just the right container
composition" is itself the signal this problem is testing.

## Step 3 — Why `std::list` specifically, and why iterators stay valid

The trick that makes this whole approach work: **`std::list` iterators are
not invalidated by insertion or erasure elsewhere in the list** (unlike
`std::vector`, where any insertion can reallocate and invalidate every
iterator). So the hash map can safely cache an iterator into the list
across arbitrary `splice` operations — this is the one fact about
`std::list`'s iterator invalidation guarantees that makes the whole design
work, and it's worth stating explicitly if asked "why `list` and not
`vector`."

`std::list::splice` moves a node from one position to another (or within
the same list) in **O(1)**, with no copying and no iterator invalidation —
this is what implements "move to most-recently-used" for O(1).

## Step 4-5 — Implementation

```cpp
#include <list>
#include <unordered_map>
#include <optional>
#include <stdexcept>

template <typename Key, typename Value>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) throw std::invalid_argument("capacity must be > 0");
    }

    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;

        // Move the accessed node to the front (MRU) in O(1), no copy.
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    void put(const Key& key, Value value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            items_.splice(items_.begin(), items_, it->second);   // touch: now MRU
            return;
        }

        if (items_.size() == capacity_) {
            // Evict LRU: the back of the list.
            auto& lru = items_.back();
            index_.erase(lru.first);
            items_.pop_back();
        }

        items_.emplace_front(key, std::move(value));
        index_[key] = items_.begin();
    }

private:
    size_t capacity_;
    std::list<std::pair<Key, Value>> items_;                 // front = MRU, back = LRU
    std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator> index_;
};
```

**Complexity, worth stating explicitly, unprompted**: `get` — O(1) average
(hash lookup + O(1) splice). `put` — O(1) average (hash lookup/insert +
O(1) splice or pop_back). Space — O(capacity).

## Step 6 — Edge cases handled

- `capacity == 0` rejected at construction, rather than silently accepting
  a cache that can never hold anything.
- `put` on an existing key updates the value **and** refreshes recency
  (both are required by the spec — a common bug is updating the value but
  forgetting to also move it to MRU).
- `get` on a missing key returns `std::nullopt` rather than a magic sentinel
  like `-1`, which doesn't generalize past `int` values — worth a one-line
  callout if the interviewer's prompt used `-1` (LeetCode's canonical
  signature): "I'd use `-1` for an `int`-only interface as specified, or
  `optional<Value>` for a template that needs to work for any value type —
  I'll assume the latter unless you want the former."

## Extensions (what a live follow-up usually asks)

**"Make it thread-safe."** Direct application of
`00_foundations/10_concurrency_for_lld.md`'s Monitor Object pattern — wrap
every public method's body in a `std::lock_guard<std::mutex>`. Note
explicitly that both fields (`items_` and `index_`) must be protected
*together* under one lock, not independently — they're two views of one
invariant (every list node has exactly one corresponding map entry pointing
at it), so this is a mutex situation, not an atomics situation, per that
file's disambiguation.

**"Turn this into an LFU (Least Frequently Used) cache instead."** A
materially harder data-structure problem: you now need O(1) access to "the
minimum frequency bucket," which requires a frequency→list-of-keys map plus
a "current minimum frequency" pointer, updated on every access. Worth
knowing this exists as the natural next problem (Tier 1, #15 in the
roadmap) and that it's a genuinely different, harder data structure —
don't wing a full implementation live unless asked, but be able to describe
the shape: "a map from frequency to an LRU-list-of-keys at that frequency,
plus tracking the minimum frequency, so eviction is 'pop from the LRU list
at the minimum frequency bucket.'"

**"Make the eviction policy pluggable — LRU today, LFU or FIFO tomorrow,
without a rewrite."** This is the point where a GoF pattern genuinely
earns its place on this problem (unlike the base design, which correctly
has none — see the KISS note at the top of this file). Extract an
`EvictionPolicy` interface with `onAccess(key)` and `evictionCandidate()`
methods; `LRUCache` becomes a thin wrapper holding an
`std::unique_ptr<EvictionPolicy>` plus the actual `key -> value` map,
delegating recency/frequency bookkeeping entirely to the policy:

```cpp
class EvictionPolicy {
public:
    virtual void onAccess(int key) = 0;       // called on every get/put touch
    virtual void onInsert(int key) = 0;       // called once, on first insert
    virtual int evictionCandidate() const = 0; // which key to remove next
    virtual ~EvictionPolicy() = default;
};

// LRUPolicy: wraps exactly this file's std::list + index_ mechanism.
// FIFOPolicy: simpler still -- a single std::queue<int> of insertion
// order; onAccess() is a no-op (FIFO, unlike LRU, never reorders on
// touch -- eviction order depends only on insertion order), which is
// itself worth stating as the one-line distinction between the two
// policies when asked.
// LFUPolicy: the frequency-bucket structure from
// 00_foundations/13_specialized_data_structures_for_systems.md, unchanged.
```

**Why `FIFOPolicy::onAccess` is a no-op and that's not a bug**: FIFO
eviction order is fixed at insertion time and never changes based on
reads — this is the precise, single-sentence distinction between FIFO and
LRU worth having ready, since candidates who've only internalized LRU
sometimes reflexively try to "update recency" for FIFO too, which is
simply the wrong policy.

**"Add TTL-based expiry alongside LRU."** A background thread or
lazy-check-on-access approach, each with real tradeoffs to state: a
background sweep thread adds complexity and its own concurrency surface;
lazy expiry-on-access is simpler but can leave expired-but-unaccessed
entries consuming capacity indefinitely. Cross-references Tier 3 problem
#19.

**"What if capacity needs to change at runtime?"** `setCapacity(size_t)`
that evicts from the back until `items_.size() <= newCapacity` — small,
additive change, good to mention the eviction loop needs the same lock as
every other mutating operation if thread-safety was already added.

## Interview framing to reuse

"The core insight is that `std::list::splice` gives O(1) move-to-front with
no iterator invalidation, which is exactly what lets the hash map safely
cache iterators into the list across arbitrary reorderings — that's the
whole design, no GoF pattern needed here." Stating precisely *why* no
pattern is needed is a stronger signal than defaulting to one out of habit,
directly demonstrating the KISS discipline from `00_foundations/17`.

## Exercises

1. Implement the above from scratch, from memory, in under 20 minutes —
   this problem is common enough, and mechanical enough once understood,
   that it should become close to muscle memory.
2. Add the thread-safety extension and write a stress test: many threads
   doing random `get`/`put` calls concurrently against a small-capacity
   cache, then verify the cache's final size never exceeds capacity and no
   crash/corruption occurred (run under ASan/TSan for real signal).
3. Sketch (pseudocode is fine, this one's for understanding, not full
   implementation) the LFU cache's `get`/`put` logic, and identify exactly
   which operation is the hard one to keep O(1) (hint: finding *which* key
   to evict when multiple keys share the current minimum frequency).
