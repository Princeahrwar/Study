# 02 — STL for system design

> Prerequisite: `01`. This file is scoped to a narrower, LLD-specific
> question than a general STL tutorial: **which container, and why, for a
> given design**, plus the STL idioms (`<algorithm>`, `std::function`,
> lambdas) that show up inside pattern implementations later in this track.
> It comes before OOP fundamentals (`03`) deliberately — containers and
> algorithms are mechanical C++ knowledge, independent of the design-thinking
> layer that starts in Phase 2 — so where this file previews a term from
> Phase 4 (Strategy, Observer), treat it as a forward pointer, not an
> assumed-known concept; the full pattern treatment is in `07`-`09`.

## Why this is a design question, not a trivia question

Every machine-coding problem so far has made a container choice you
probably didn't stop to justify out loud: `ParkingLot` used `std::set` for
sorted request queues in the elevator, `std::list` + `std::unordered_map`
for the LRU cache, `std::map` for the balance sheet in Splitwise. **The
choice was never arbitrary** — each one was picked because of a specific
complexity or ordering requirement the problem imposed. Interviewers notice
when a candidate reaches for `std::vector` by habit for something that
needs O(1) removal from the middle, or for `std::map` when insertion order
never needs to be sorted at all. This file is the reference for making that
choice deliberately and being able to say why.

## Containers, side by side

| Container | Access | Insert/erase (end) | Insert/erase (middle) | Ordering | Underlying layout | Reach for it when |
| --- | --- | --- | --- | --- | --- | --- |
| `std::vector` | O(1) random | O(1) amortized (back) | O(n) | Insertion order | Contiguous | Default choice — cache-friendly, iteration-heavy, mostly append |
| `std::deque` | O(1) random | O(1) both ends | O(n) middle | Insertion order | Chunked, non-contiguous | Need O(1) push/pop at **both** ends (e.g. a sliding window) |
| `std::list` | O(n) | O(1) anywhere, given an iterator | O(1) anywhere, given an iterator | Insertion order | Node-based, doubly linked | Need O(1) arbitrary-position insert/erase **and already hold an iterator** — the LRU cache's exact use case (`10_lld/01_machine_coding_problems/05_lru_cache.md`) |
| `std::unordered_map` | O(1) avg by key | O(1) avg | O(1) avg | None | Hash table | Default key-value lookup |
| `std::map` | O(log n) by key | O(log n) | O(log n) | **Sorted by key** | Red-black tree | Need sorted iteration, range queries (`lower_bound`/`upper_bound`), or a stable iteration order |
| `std::unordered_set` / `std::set` | Same as maps, values only | Same | Same | Same distinction | Same | Membership test; `set` when you need sorted uniqueness (the Elevator's `upRequests_`/`downRequests_` in `10_lld/01_machine_coding_problems/03_elevator_system.md` needed exactly this) |
| `std::priority_queue` | O(1) top | O(log n) push/pop | — | Max (or custom comparator) at top | Binary heap over a container | Always-want-the-extreme-element access (Task Scheduler, Dijkstra-style graph work) |
| `std::stack` / `std::queue` | O(1) top/front | O(1) | — | LIFO / FIFO | Adapter over `deque` by default | Rarely the bottleneck choice — pick the underlying container if performance matters |

## The two questions that pick the right container

1. **What's the actual operation profile?** Count reads vs. writes vs.
   middle-insertions vs. "give me sorted order" queries. A structure tuned
   for one is often bad at another — `std::map`'s O(log n) everything is a
   worse default than `unordered_map`'s O(1) average unless you specifically
   need the sort.
2. **Do you need iterator/reference stability across mutation?** This is
   the single most interview-relevant fact in this whole file, because it's
   what makes the LRU cache's design work at all (`10_lld/01_machine_coding_problems/05_lru_cache.md`):
   `std::vector`'s iterators/references/pointers can be invalidated by *any*
   insertion (reallocation) or by erasure of *any* earlier element.
   `std::list`, `std::map`, `std::unordered_map` (with default rehashing
   policy caveats — see below), and `std::deque` for elements *not* at the
   modified end, all keep references/iterators to *unaffected* elements
   valid across mutation elsewhere in the container. If a design needs to
   cache a handle into a container and keep mutating the container around
   it — as the LRU cache does — this fact decides the container, full stop.

**A caveat worth having ready**: `unordered_map` iterators are invalidated
by rehashing (which insertion can trigger), even though *references* to
existing elements are not — a subtlety worth stating precisely rather than
overclaiming "hash maps never invalidate."

## Cache locality — the reason `vector` is the real default

You already have the mental model for this from `04_computer_organization/13_cache_basics.md` and
`04_computer_organization/14_cache_optimisation_and_the_real_hierarchy.md`:
`std::vector`'s contiguous layout means sequential access streams through
cache lines efficiently, and the prefetcher can predict the access pattern.
`std::list`'s node-based layout scatters elements across the heap — even an
O(1)-per-op algorithm on a `list` can lose to an O(n) linear scan of a
`vector` in wall-clock time for small-to-medium N, purely from cache misses
per node dereference. **The Staff-level answer to "vector or list?" is
never purely about Big-O** — it names the cache-locality tradeoff and
states that for anything under a few thousand elements, benchmark before
assuming the asymptotically-better structure wins in practice.

## `<algorithm>` — decoupling logic from the container

The STL's algorithms operate on iterator ranges, not on a specific
container type — this is what lets `std::sort`, `std::find_if`, and
`std::accumulate` work identically over a `vector`, a `deque`, or a raw
array. Prefer them over hand-rolled loops in a machine-coding round: they're
shorter, harder to get subtly wrong (off-by-one errors in a hand-rolled
binary search, say), and signal STL fluency directly.

```cpp
#include <algorithm>
#include <numeric>

// find_if: locate the first available slot — replaces a hand-rolled loop
auto it = std::find_if(slots.begin(), slots.end(),
                        [&](const Slot& s) { return s.fits(vehicle); });

// transform: project every split into its amount, without a manual loop
std::vector<double> amounts;
std::transform(splits.begin(), splits.end(), std::back_inserter(amounts),
               [](const Split& s) { return s.amount; });

// accumulate: sum a field across a collection
double total = std::accumulate(splits.begin(), splits.end(), 0.0,
                                [](double sum, const Split& s) { return sum + s.amount; });

// count_if / any_of / all_of: existence and counting queries, read like English
bool anyOutOfStock = std::any_of(products.begin(), products.end(),
                                  [](const Product& p) { return p.quantity == 0; });
```

**The honest tradeoff to know**: algorithm calls can be marginally less
obvious to a reviewer unfamiliar with the STL idiom than an explicit loop,
and heavy lambda nesting can hurt readability — use them where they
genuinely clarify intent (a named operation like "find the first fitting
slot"), not reflexively for every loop in a 45-minute round where a plain
`for` is faster to write correctly under pressure.

## Function objects and lambdas — passing behaviour, not just data

This is the STL machinery that makes `Strategy` and `Observer`
(`09_design_patterns_behavioral.md`) lightweight in modern C++, as an
alternative to a full class hierarchy when the "family of algorithms" is
small, stateless-ish, and doesn't need its own named type.

```cpp
#include <functional>

// A Strategy expressed as std::function instead of an abstract base class +
// concrete subclasses — appropriate when you don't need multiple methods
// per strategy, don't need the strategy to carry meaningful internal state
// beyond what a closure captures, and want to pass ad hoc lambdas at call
// sites without declaring a new class per strategy.
class ParkingTicket {
public:
    using PricingFn = std::function<double(int hours)>;
    explicit ParkingTicket(PricingFn pricing) : pricing_(std::move(pricing)) {}
    double cost(int hours) const { return pricing_(hours); }
private:
    PricingFn pricing_;
};

// usage — no HourlyPricing class needed for a one-off strategy
ParkingTicket t([](int hours) { return hours * 10.0; });
```

**When `std::function` is the better call than a Strategy class hierarchy**:
the strategy is a single operation (one method), callers naturally want to
pass a lambda inline rather than naming and instantiating a class, and you
don't need runtime type identification of *which* strategy is active.
**When the class-hierarchy Strategy from `09` is still the better call**:
the strategy needs multiple related methods (a `PaymentStrategy` with
`charge()` *and* `refund()`), needs to hold significant internal state with
its own lifecycle, or the set of strategies is a domain concept worth naming
as types (`CreditCardPayment`, `UpiPayment`) for readability and
discoverability. Say this tradeoff explicitly if asked "why a class here and
a lambda there" across two different problems in the set — it's a real
design decision, not an inconsistency.

**The cost worth knowing**: `std::function` type-erases its callable, which
typically costs a heap allocation for anything that doesn't fit its small-
buffer optimization (implementation-defined, but a capturing lambda with
more than a couple of pointers' worth of captured state commonly exceeds
it), plus an indirect call — real, measurable overhead in a hot loop
(cross-reference `04_computer_organization/10_branch_prediction`, indirect branches), negligible in a request-scoped handler
called a few thousand times a second. `09`'s `Strategy` section already
flagged virtual dispatch's cost; `std::function` has a comparable cost
profile for the same underlying reason (type erasure needs an indirection).

## Interview angles

**"Why `std::list` here and not `std::vector`?"** — Name the iterator-
stability requirement specifically (an external structure holds handles
into this container and both are mutated independently), not just "linked
lists are good at insertion."

**"Would you use `std::function` or a class hierarchy for this Strategy?"**
— Answer with the tradeoff above: number of methods, state complexity,
whether the variants are a real domain vocabulary worth naming.

**"What's the complexity of this operation, and does the container support
it?"** — A fast, correct answer here (not a guess) is table stakes; the
table in this file is worth having memorised cold.

## Exercises

1. Take `Splitwise`'s `BalanceSheet` (`10_lld/01_machine_coding_problems/04_splitwise_expense_sharing.md`),
   currently `std::map<std::string, std::map<std::string, double>>`.
   Rewrite it with `std::unordered_map` at both levels, benchmark
   `simplify()` on a few thousand synthetic users, and note whether the
   result actually changes (the interesting finding here is usually "not
   much, because `simplify()` is dominated by the O(n log n) sort of
   creditors/debtors, not the map lookups" — confirm or refute this on your
   own machine, the same discipline as the benchmarks in the removed CO benchmarks).
2. Rewrite `Elevator`'s `upRequests_`/`downRequests_`
   (`10_lld/01_machine_coding_problems/03_elevator_system.md`) using `std::priority_queue` instead of
   `std::set`, and identify what you lose (hint: `set::erase` of an
   arbitrary served floor is O(log n); replicate that operation with a
   `priority_queue` and see what it costs you).
3. Rewrite the `HourlyPricing`/`FlatPricing` class hierarchy from `09`'s
   Strategy section as `std::function<double(int)>` instances instead, and
   write one sentence on which version you'd actually choose for a real
   `ParkingLot` and why.
4. Replace three hand-rolled loops in any solution you've written so far in
   `01_machine_coding_problems/` with `<algorithm>` calls
   (`find_if`/`any_of`/`accumulate`/`transform`), and note anywhere the
   algorithm version reads *less* clearly — that's a legitimate reason not
   to use it, not a personal failing.
