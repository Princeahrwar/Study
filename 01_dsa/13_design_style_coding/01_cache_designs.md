# DSA 13.01: Cache Designs

*Part of the [Design-style coding plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Classic linked-list problems](../04_linked_lists/05_classic_interview_problems.md) (sections 6 and 7 build the O(1) LRU and LFU caches that interviews ask for; this tutorial does not repeat them) and [Design a hash map](../02_hashing/03_design_a_hash_map.md) (section 6 has the `std::list` and `splice` version and notes on locking)*

"Implement an LRU cache" is the opening question. The follow-ups are where the interview is decided: *Why LRU and not LFU? What breaks it? How would you make it thread-safe, and then fast on many cores? How do you expire entries? What does a hit cost?* This tutorial answers those with measurements instead of folklore. It treats a cache as a **policy** (what to evict) plus an **implementation** (how cheaply you can apply the policy), and it keeps the two apart.

By the end you can:

- state the one metric that matters (the hit ratio) and the bound no policy can beat on a given trace (Belady's OPT, and the one way to beat it);
- implement and compare FIFO, random, LRU, LFU, CLOCK, segmented LRU and a TinyLFU admission filter, and say which input kills each;
- explain why LRU gets a **0% hit rate** on a loop one key longer than the cache, and why LFU can get **97.5% misses** after the popular keys change;
- show Belady's anomaly (FIFO misses more with a bigger cache) and why LRU cannot do that;
- write an LRU that lives in flat arrays with expiry times, and say what it saves over `std::list`;
- explain why a hit that changes shared structure limits concurrency, why CLOCK hits are cheap, and how lock striping (shards) trades hit ratio for throughput.

**Where the numbers come from.** The code below was compiled with `-Wall -Wextra` and tested: 12,146,680 checks passed under AddressSanitizer and UBSan, and the same test run under ThreadSanitizer reported no data race (GCC 13.3). The tests compare, on random traces, FIFO, LRU, LFU, CLOCK, segmented LRU and the TinyLFU cache with simple vector-based models (cache sizes 1 to 12, up to 300 accesses); Belady's OPT with an exhaustive search over every eviction choice; the flat LRU with expiry against a model; the count-min sketch against its definition; and four threads hammering the sharded cache, where every value returned must be the right one. Ten deliberate bugs were tried. Eight were caught at once. Two (segmented LRU without its demotion step, and a sketch that takes the maximum instead of the minimum) **escaped** the first version of the tests, so I added larger caches and longer traces and a direct check of the sketch; both are now caught. One (a `get` without its lock) is only visible to ThreadSanitizer, not to the plain test.

**Data and timing.** Traces are synthetic and seeded; they model four situations, not any real workload. Machine: a cloud container with **2 virtual cores**, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`. Times are the **best of 3 repetitions, shown for run 1 / run 2**; they are this machine's, not yours, and the multi-thread rows in section 8 are noisy (a 2-core VM). Miss ratios and traces are identical in both runs. Statements about Redis, Linux, Caffeine and other systems are from memory and marked as such.

---

## 1. What a cache design question is really about

A cache holds a small subset of a large set of items. On each access there are two outcomes: a **hit** (the item is there) or a **miss** (it must be fetched, and if the cache is full something is evicted to make room). The number that matters is the **hit ratio**, or its complement the miss ratio; everything else (latency of a hit, memory per entry, locking) is the cost of reaching a given hit ratio.

Two layers, and good answers keep them apart:

1. The **policy**: which entry to evict, and whether to admit a new one at all. It decides the hit ratio.
2. The **mechanism**: the data structures that apply the policy, such as a hash map plus a linked list. It decides the cost of a hit, the memory per entry, and what can run in parallel.

LRU and LFU as interview problems are *mechanism* questions ("make the policy O(1)"), and the earlier tutorials answer them. The rest of this tutorial is about the policy (sections 2 to 6), then the mechanism beyond the textbook (sections 7 and 8).

## 2. The policies, in code

All the policies below have the same interface: `access(key)` returns whether it was a hit, and brings the key in if not. That is enough to compute a miss ratio on any trace of keys.

- **FIFO**: evict the key that has been cached longest. A hit changes nothing.
- **Random**: evict a random key. A hit changes nothing.
- **LRU**: evict the key unused for the longest time. Every hit moves the key to the front.
- **LFU**: evict the key with the fewest uses *while it has been cached* (ties: the older use). Counts start at 1 on entry. (This model uses a `std::set`, `O(log n)`; the `O(1)` design is in Linked lists 05, section 7.)
- **CLOCK** (second chance): the keys sit in a circle with one *reference bit* each. A hit sets the bit. To evict, a hand sweeps the circle; a key whose bit is set gets it cleared and is skipped, and the first key whose bit is clear is replaced. A hit never moves anything.
- **Segmented LRU (SLRU)**: two LRU lists. A new key enters **probation**; a second hit promotes it to **protected** (80% of the room); when protected overflows, its oldest key is demoted back to probation. The victim always comes from probation first. A key that is used only once never reaches protected.

```cpp
// Each policy answers one question about an access to `key`: was it already in the cache (a hit)?  If not, it is brought in,
// and when the cache is full something is evicted first.  Only the keys matter here, not the values: the hit ratio depends on the policy alone.
struct Fifo {                                    // evict the key that has been in the cache longest, ignoring hits
    size_t cap; std::list<int> q; std::unordered_set<int> in;
    explicit Fifo(size_t c) : cap(c) {}
    bool access(int k) {
        if (in.count(k)) return true;
        if (q.size() == cap) { in.erase(q.front()); q.pop_front(); }
        q.push_back(k); in.insert(k); return false;
    }
    size_t size() const { return q.size(); }
};
struct Lru {                                     // evict the key unused for longest: a hit moves the key to the front
    size_t cap; std::list<int> q; std::unordered_map<int, std::list<int>::iterator> pos;
    explicit Lru(size_t c) : cap(c) {}
    bool access(int k) {
        auto it = pos.find(k);
        if (it != pos.end()) { q.splice(q.begin(), q, it->second); return true; }
        if (q.size() == cap) { pos.erase(q.back()); q.pop_back(); }
        q.push_front(k); pos[k] = q.begin(); return false;
    }
    size_t size() const { return q.size(); }
};
struct Lfu {                                     // evict the key used fewest times while in the cache; ties: the oldest use
    size_t cap; long long tick = 0; std::map<int, std::pair<int, long long>> info; std::set<std::tuple<int, long long, int>> order;
    explicit Lfu(size_t c) : cap(c) {}
    bool access(int k) {
        auto it = info.find(k);
        if (it != info.end()) {
            order.erase({it->second.first, it->second.second, k});
            it->second = {it->second.first + 1, ++tick}; order.insert({it->second.first, it->second.second, k}); return true;
        }
        if (info.size() == cap) { auto v = *order.begin(); order.erase(order.begin()); info.erase(std::get<2>(v)); }
        info[k] = {1, ++tick}; order.insert({1, tick, k}); return false;
    }
    size_t size() const { return info.size(); }
};
struct Clock {                                   // "second chance": a hit only sets a bit; the hand clears bits and evicts the first key without one
    size_t cap, hand = 0; vector<int> key; vector<char> ref; std::unordered_map<int, size_t> slot;
    explicit Clock(size_t c) : cap(c) {}
    bool access(int k) {
        auto it = slot.find(k);
        if (it != slot.end()) { ref[it->second] = 1; return true; }
        if (key.size() < cap) { slot[k] = key.size(); key.push_back(k); ref.push_back(0); return false; }
        while (ref[hand]) { ref[hand] = 0; hand = (hand + 1) % cap; }
        slot.erase(key[hand]); key[hand] = k; slot[k] = hand; hand = (hand + 1) % cap; return false;
    }
    size_t size() const { return key.size(); }
};
struct RandomEvict {                             // evict a random key (a fixed seed makes a run repeatable)
    size_t cap; vector<int> key; std::unordered_map<int, size_t> pos; std::mt19937_64 g{12345};
    explicit RandomEvict(size_t c) : cap(c) {}
    bool access(int k) {
        if (pos.count(k)) return true;
        if (key.size() == cap) { size_t v = g() % cap; pos.erase(key[v]); key[v] = k; pos[k] = v; return false; }
        pos[k] = key.size(); key.push_back(k); return false;
    }
    size_t size() const { return key.size(); }
};
struct Slru {                                    // two LRU lists: new keys enter "probation"; a second use promotes to "protected" (80% of the room)
    size_t cap, pcap; std::list<int> prob, prot; std::unordered_map<int, std::pair<bool, std::list<int>::iterator>> pos;
    explicit Slru(size_t c) : cap(c), pcap(std::max<size_t>(1, c * 4 / 5)) {}
    bool access(int k) {
        auto it = pos.find(k);
        if (it != pos.end()) {
            if (it->second.first) { prot.splice(prot.begin(), prot, it->second.second); return true; }          // already protected
            prob.erase(it->second.second); prot.push_front(k); it->second = {true, prot.begin()};              // promote
            if (prot.size() > pcap) { int d = prot.back(); prot.pop_back(); prob.push_front(d); pos[d] = {false, prob.begin()}; }  // demote
            return true;
        }
        if (prob.size() + prot.size() == cap) {
            if (prob.empty()) { int d = prot.back(); prot.pop_back(); pos.erase(d); } else { pos.erase(prob.back()); prob.pop_back(); }
        }
        prob.push_front(k); pos[k] = {false, prob.begin()}; return false;
    }
    size_t size() const { return prob.size() + prot.size(); }
};
```

The two benchmarks that bound every policy: **OPT**, which looks into the future, and a trace generator for each situation.

```cpp
// Belady's OPT: the best possible policy, which needs the future.  On a miss with a full cache, evict the key whose next use is farthest away.
inline long long opt_misses(const vector<int>& tr, size_t cap) {
    int n = (int)tr.size(); const int INF = 1 << 30;
    vector<int> next(n, INF); std::unordered_map<int, int> last;
    for (int i = n - 1; i >= 0; --i) { auto it = last.find(tr[i]); next[i] = it == last.end() ? INF : it->second; last[tr[i]] = i; }
    std::set<std::pair<int, int>> byNext;                        // (next use, key) for the keys in the cache
    std::unordered_map<int, int> nextOf; long long misses = 0;
    for (int i = 0; i < n; ++i) {
        auto it = nextOf.find(tr[i]);
        if (it != nextOf.end()) byNext.erase({it->second, tr[i]});
        else {
            ++misses;
            if (nextOf.size() == cap) { auto v = *byNext.rbegin(); byNext.erase(std::prev(byNext.end())); nextOf.erase(v.second); }
        }
        nextOf[tr[i]] = next[i]; byNext.insert({next[i], tr[i]});
    }
    return misses;
}
template <class P> long long misses_of(P p, const vector<int>& tr) { long long m = 0; for (int k : tr) m += !p.access(k); return m; }
```

**Belady's OPT** evicts the key whose next use is farthest away. For a cache that must insert every missed key it is the best possible: no policy can have fewer misses. It cannot be built (it needs the future), but it is the yardstick: the gap between a policy and OPT is what a better policy could still win.

```cpp
// The four access patterns used to compare the policies (keys are small integers).  All are seeded, so every run is repeatable.
inline vector<int> zipf_trace(int n, int universe, double alpha, uint64_t seed) {        // a few keys get most of the traffic
    std::mt19937_64 g(seed); vector<double> cum(universe); double s = 0;
    for (int i = 0; i < universe; ++i) { s += 1.0 / std::pow(i + 1, alpha); cum[i] = s; }
    vector<int> t(n); std::uniform_real_distribution<double> u(0, s);
    for (auto& x : t) x = (int)(std::lower_bound(cum.begin(), cum.end(), u(g)) - cum.begin());
    return t;
}
inline vector<int> loop_trace(int n, int size) { vector<int> t(n); for (int i = 0; i < n; ++i) t[i] = i % size; return t; }       // 0 1 2 ... size-1, again and again
inline vector<int> scan_mix_trace(int n, int hot, uint64_t seed) {                       // half the accesses: a hot set; half: keys never seen again
    std::mt19937_64 g(seed); vector<int> t(n); int fresh = 1000000;
    for (auto& x : t) x = (g() & 1) ? (int)(g() % hot) : fresh++;
    return t;
}
inline vector<int> shifting_trace(int n, int hot, int phase, uint64_t seed) {            // the hot set is replaced every `phase` accesses
    std::mt19937_64 g(seed); vector<int> t(n);
    for (int i = 0; i < n; ++i) t[i] = (i / phase) * hot + (int)(g() % hot);
    return t;
}
```

## 3. A small example, then the first surprises

Cache of 3, accesses `1 2 3 1 4 1 5 2 1 3`:

```
T1 cache of 3, accesses 1 2 3 1 4 1 5 2 1 3 (H hit, M miss): FIFO MMMHMMMMHM, LRU MMMHMHMMHM, LFU MMMHMHMMHM, CLOCK MMMHMHMMHM, SLRU MMMHMHMMHM; misses: FIFO 8, LRU 7, LFU 7, CLOCK 7, SLRU 7, OPT 6
```

FIFO misses once more than LRU because it evicts key 1 when 4 arrives, even though 1 was just used; the others agree here. OPT misses 6, so on this trace no policy can do better than 6.

**Belady's anomaly.** Give a cache *more* room and a policy can miss *more*. The classic trace:

```
T2 trace 1 2 3 4 1 2 5 1 2 3 4 5: misses with 3 and 4 slots: FIFO 9 and 10, LRU 10 and 8, OPT 7 and 6
```

FIFO misses 9 times with 3 slots but 10 times with 4. LRU and OPT never do that: they have the **stack property**, meaning the set of keys an LRU cache of size `c` holds is always a subset of what a cache of size `c + 1` holds, so more room can only add hits. (The tests check the numbers above exactly; I did not test the stack property as such.)

**The loop.** A loop over `c + 1` keys with a cache of `c`:

```
T3 a loop over 101 keys with a cache of 100 (100,000 accesses): miss % FIFO 100.0, LRU 100.0, LFU 100.0, CLOCK 100.0, RANDOM 2.1, SLRU 100.0, OPT 1.1
```

LRU, FIFO, LFU, CLOCK and SLRU all miss **every time**: each key is evicted just before it is needed again. Random eviction misses 2.1%, and OPT 1.1%. Read that carefully: on this input the "smart" recency policies are worse than throwing darts. A sequential scan over a table or a file a little larger than the cache is exactly this, and it is why databases and operating systems do not use plain LRU for scans (from memory).

## 4. Four traces, eight policies

Four situations, each 1,000,000 accesses, a cache of 1,000 keys. Miss ratio in percent (lower is better):

```
T4 zipf, 50000 keys, alpha 0.9 (n=1000000, cache 1000): miss % FIFO 64.6, RANDOM 64.6, LRU 60.8, LFU 51.3, CLOCK 59.5, SLRU 50.9, TINYLFU 50.9, OPT 41.2
T4 loop over 1100 keys (n=1000000, cache 1000): miss % FIFO 100.0, RANDOM 17.7, LRU 100.0, LFU 100.0, CLOCK 100.0, SLRU 100.0, TINYLFU 10.1, OPT 9.2
T4 hot set of 500 + one-time keys (n=1000000, cache 1000): miss % FIFO 70.7, RANDOM 70.7, LRU 64.0, LFU 50.1, CLOCK 60.1, SLRU 50.1, TINYLFU 50.1, OPT 50.1
T4 hot set of 800 replaced every 20000 (n=1000000, cache 1000): miss % FIFO 4.0, RANDOM 8.0, LRU 4.0, LFU 97.5, CLOCK 4.7, SLRU 9.0, TINYLFU 25.2, OPT 4.0
```

The traces: **zipf** (50,000 keys; the popularity of the key of rank `r` is proportional to `1 / r^0.9`, the usual model of skewed traffic); **loop** over 1,100 keys; **hot set plus one-time keys** (half the accesses go to 500 hot keys, half to keys never seen again, as in a scan that runs next to a working set); and a **shifting** hot set (800 keys, replaced by 800 new ones every 20,000 accesses).

What each row teaches:

- **Zipf:** the frequency policies (LFU, SLRU, TinyLFU) win clearly: 51% against LRU's 61%, because popularity is stable and counting it is the right signal. OPT, at 41%, shows there is still a lot on the table. CLOCK (59.5%) is slightly better than LRU here; I did not investigate why, but it differed from LRU on 1.7% of accesses (next line).
- **Loop:** every recency-based policy gets 100% misses; random does 17.7% and the TinyLFU admission filter does 10.1%, close to OPT's 9.2%. The filter works because it refuses to throw out a key that has been seen as often as the newcomer; the loop's keys all have equal counts, so the cache keeps the keys it has.
- **Hot set plus one-time keys:** the one-time keys flush an LRU cache (64% misses) because each of them pushes a hot key toward the exit. LFU, SLRU and TinyLFU all reach 50.1%, equal to OPT, since half the accesses are to keys that can never hit: they protect the hot set.
- **Shifting hot set:** the picture reverses. LRU and OPT sit at 4.0% (only the first access to each new key misses), CLOCK at 4.7%, but **LFU misses 97.5%**: the old hot keys have high counts and stay forever, so the new hot keys, with count 1, are evicted first, again and again (this is the "LFU never forgets" note of Linked lists 05). SLRU recovers slowly (9.0%), and TinyLFU, with its aging, at 25.2%.

No policy wins everywhere. LRU adapts fast and fails on scans; LFU is strong on stable skew and fails when popularity moves; SLRU and TinyLFU are the usual compromise. The honest interview answer is: *"it depends on the access pattern; here is how I would measure it"*, and then you describe this experiment.

How much does the cache size matter? Zipf again, miss ratio in percent for cache sizes 100 to 10,000, as `[size: LRU, LFU, TinyLFU, OPT]`:

```
T5 zipf miss % by cache size (LRU, LFU, TINYLFU, OPT): [100: 81.9, 70.2, 71.4, 63.1] [300: 72.7, 62.1, 62.2, 53.1] [1000: 60.8, 51.3, 50.9, 41.2] [3000: 47.7, 40.0, 39.2, 29.4] [10000: 30.4, 26.1, 25.3, 16.2]
```

Ten times more room (1,000 to 10,000) moves LRU from 60.8% to 30.4%: still far from free. The gap between LRU and OPT stays around 14 to 19 points at every size.

## 5. CLOCK: LRU's cheap approximation

LRU's cost is on the **hit** path: every hit rewrites several pointers (unlink, relink) in a structure shared by all users. CLOCK keeps LRU's idea (recently used keys survive) but a hit only sets one bit in the key's own slot. All the work moves to the miss path, where the hand sweeps. How close is it to LRU? On the zipf trace the two disagreed on whether an access was a hit on only a small fraction:

```
T6 on the zipf trace CLOCK and LRU gave a different answer (hit versus miss) on 16960 of 1000000 accesses (1.70 %)
```

Operating systems use CLOCK-like algorithms for page replacement for this reason, since a page-table hit sets a hardware bit and cannot run code on every access (from memory; Linux's page reclaim uses a more elaborate variant with active and inactive lists, which is the segmented idea of section 6 applied to pages, as I remember it). It is also why CLOCK variants appear in concurrent caches: a hit can be a single atomic store.

**One design choice.** I insert a new key with its bit **clear**, so it must be hit once more to survive a sweep; some descriptions insert with the bit set. The two behave differently on the first sweep, and I measured only the first.

## 6. Scan resistance: SLRU and TinyLFU

Both address the same weakness, a stream of keys that would pollute the cache.

**SLRU** is purely structural: a one-time key enters probation and leaves from probation without ever touching the protected part. That is why the hot-set-plus-scan row is 50.1% instead of LRU's 64%.

**TinyLFU** adds an **admission decision** in front of the eviction decision. Keep an estimate of how often each key was seen recently, and when the cache is full admit the newcomer only if it has been seen **more often than the key it would evict**. The estimate must be tiny in memory, so it is a **count-min sketch**: a few rows of small counters; a key increments one counter in each row (chosen by a different hash per row) and its estimate is the **minimum** of its counters. Collisions can only make a counter larger, so the estimate never under-counts (the test checks that, and the row-minimum rule directly). Counters saturate at 15, and when the number of recorded accesses reaches ten times the cache size every counter is **halved** (aging), so old popularity fades.

```cpp
// A count-min sketch estimates how often each key was seen, in a few bytes per key.  Four rows of small counters; a key
// increments one counter per row and its estimate is the MINIMUM of the four, so it can only over-count, never under-count.
struct CountMin {
    vector<uint8_t> t; size_t w, adds = 0, limit; static constexpr int ROWS = 4, MAXC = 15;
    CountMin(size_t width, size_t reset_after) : t(ROWS * width, 0), w(width), limit(reset_after) {}   // width: a power of two
    size_t idx(int r, int k) const {
        uint64_t x = (uint64_t)(uint32_t)k * 0x9E3779B97F4A7C15ull + (uint64_t)r * 0xC2B2AE3D27D4EB4Full;
        x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 32;
        return (size_t)r * w + (x & (w - 1));
    }
    int estimate(int k) const { int m = MAXC; for (int r = 0; r < ROWS; ++r) m = std::min<int>(m, t[idx(r, k)]); return m; }
    void add(int k) {
        for (int r = 0; r < ROWS; ++r) { uint8_t& c = t[idx(r, k)]; if (c < MAXC) ++c; }
        if (++adds == limit) { for (auto& c : t) c >>= 1; adds = 0; }          // aging: halve everything, so old popularity fades
    }
};
// TinyLFU admission in front of an LRU list: when the cache is full, the NEW key only gets in if the sketch says it has been seen
// more often than the key that would be evicted; otherwise the new key is dropped and the old one stays.
struct TinyLfu {
    size_t cap; Lru main; CountMin sketch;
    TinyLfu(size_t c, size_t width) : cap(c), main(c), sketch(width, 10 * c) {}
    bool access(int k) {
        sketch.add(k);
        auto it = main.pos.find(k);
        if (it != main.pos.end()) { main.q.splice(main.q.begin(), main.q, it->second); return true; }
        if (main.q.size() < cap) { main.q.push_front(k); main.pos[k] = main.q.begin(); return false; }
        int victim = main.q.back();
        if (sketch.estimate(k) > sketch.estimate(victim)) {
            main.pos.erase(victim); main.q.pop_back(); main.q.push_front(k); main.pos[k] = main.q.begin();
        }
        return false;
    }
    size_t size() const { return main.q.size(); }
};
```

With a cache of 1,000 and a sketch of 4 rows of 8,192 one-byte counters, the sketch takes 32 KiB, no matter how many different keys exist. The (Caffeine) Java cache library is built on this idea (from memory); my version is the simplest form (an LRU main area, no admission window), and the real one is more elaborate.

**Admission can beat OPT.** In section 2, OPT was "the best possible", with one condition: *it must insert every key it misses*. An admission filter may refuse. The smallest example, a cache of one slot:

```
T7 cache of 1, accesses 0 2 0: misses OPT 3, LRU 3, TINYLFU 2
```

For the accesses `0 2 0`, OPT must evict 0 to insert 2, then miss 0 again: 3 misses. TinyLFU declines to insert 2 (tied count, not greater), keeps 0, and hits it: 2 misses. The tests assert this example.

## 7. Mechanism beyond the textbook: a flat LRU with expiry

The LRU of Linked lists 05 allocates one node per entry. A production cache usually avoids that: nodes in a `std::list` are separate heap blocks, with an allocation per insert and a pointer-chase per hop. The alternative stores everything in arrays indexed by a **slot number** and links the slots with indices instead of pointers. A free list of slots replaces the allocator. The class also stores an **expiry time** per entry, with the clock passed in by the caller (so tests do not need to wait).

```cpp
// A production-shaped LRU: slots in flat arrays, the list is made of INDICES (no allocation per entry), plus an optional expiry time.
// The clock is passed in by the caller (any integer), which keeps the class testable.
struct LruMap {
    static constexpr int NIL = -1;
    size_t cap; vector<int> key, val, prv, nxt; vector<long long> exp; vector<int> freeSlots; std::unordered_map<int, int> idx; int head = NIL, tail = NIL;
    explicit LruMap(size_t c) : cap(c), key(c), val(c), prv(c), nxt(c), exp(c) { for (int i = (int)c - 1; i >= 0; --i) freeSlots.push_back(i); idx.reserve(c * 2); }
    void unlink(int s) { (prv[s] == NIL ? head : nxt[prv[s]]) = nxt[s]; (nxt[s] == NIL ? tail : prv[nxt[s]]) = prv[s]; }
    void push_front(int s) { prv[s] = NIL; nxt[s] = head; if (head != NIL) prv[head] = s; head = s; if (tail == NIL) tail = s; }
    void remove(int s) { unlink(s); idx.erase(key[s]); freeSlots.push_back(s); }
    bool get(int k, long long now, int& out) {
        auto it = idx.find(k); if (it == idx.end()) return false;
        int s = it->second;
        if (exp[s] <= now) { remove(s); return false; }                  // expired: found, but too old (lazy expiry)
        unlink(s); push_front(s); out = val[s]; return true;
    }
    void put(int k, int v, long long now, long long ttl) {              // ttl <= 0 means "never expires"
        long long e = ttl > 0 ? now + ttl : INT64_MAX;
        auto it = idx.find(k);
        if (it != idx.end()) { int s = it->second; val[s] = v; exp[s] = e; unlink(s); push_front(s); return; }
        if (freeSlots.empty()) remove(tail);                              // evict the least recently used
        int s = freeSlots.back(); freeSlots.pop_back();
        key[s] = k; val[s] = v; exp[s] = e; idx[k] = s; push_front(s);
    }
    size_t size() const { return idx.size(); }
};
// Many threads: one lock around everything, or the key space cut into shards with a lock each.
struct ShardedLru {                             // each shard is allocated on its own cache lines (alignas 64), so two locks never share one
    struct alignas(64) Shard { std::mutex mu; LruMap c; explicit Shard(size_t cap) : c(cap) {} };
    size_t n; vector<std::unique_ptr<Shard>> sh;
    ShardedLru(size_t total_cap, size_t shards) : n(shards) { for (size_t i = 0; i < shards; ++i) sh.push_back(std::make_unique<Shard>(std::max<size_t>(1, total_cap / shards))); }
    size_t which(int k) const { return (size_t)((uint32_t)k * 2654435761u >> 7) % n; }
    bool get(int k, long long now, int& out) { Shard& s = *sh[which(k)]; std::lock_guard<std::mutex> g(s.mu); return s.c.get(k, now, out); }
    void put(int k, int v, long long now, long long ttl) { Shard& s = *sh[which(k)]; std::lock_guard<std::mutex> g(s.mu); s.c.put(k, v, now, ttl); }
};
```

**Expiry is lazy.** An expired entry is not removed by a timer. It is removed when someone looks at it (`get` finds `exp <= now`, removes it, and reports a miss) or when it is the least recent and room is needed. The alternatives are a background sweeper, a min-heap of expiry times, or a timing wheel (from memory), which cost more machinery and give prompt removal; the lazy rule gives correct answers with no extra structure, at the cost of expired entries holding memory until touched or evicted.

```
T8 cache of 2; key 1 put at time 0 with a time-to-live of 5, key 2 put at time 1 with none: get(1) at time 4 -> hit (value 10); get(1) at time 5 -> miss and the size is now 1; put(3) and put(4) at time 6; get(2) at time 7 -> miss
```

(Key 1's lifetime is 5, so it is valid at time 4 and expired at time 5; `put(3)` and `put(4)` then push out key 2, the least recent.)

**What the flat layout saves.** Same zipf trace of 2,000,000 accesses over 200,000 keys, a miss followed by an insert, best of 3 (run 1 / run 2):

| Cache size | `std::list` + map (ms) | Flat arrays + map (ms) |
|---|---|---|
| 1,000 | 126.9 / 141.1 | 102.6 / 102.3 |
| 50,000 | 152.7 / 150.7 | 144.0 / 151.6 |

With 1,000 entries the flat version is faster, by about 1.2 to 1.4 times in both runs. With 50,000 entries the two are within noise of each other (the run-1 and run-2 order differ), so I cannot claim a gain there: my guess (not tested) is that the hash map's cache misses dominate once the table is large. Both versions spend most of their time in the hash map (`std::unordered_map`, one allocation per entry); an open-addressing table, as in [Design a hash map](../02_hashing/03_design_a_hash_map.md), is the next step and I did not measure it here.

## 8. Many threads

A mutex around an LRU is correct, and every `get` takes the lock because even a hit writes to the shared list. With several threads that lock becomes the bottleneck. **Lock striping** cuts the cache into `S` independent shards, each with its own lock and its own LRU of `capacity / S`; a key goes to the shard `hash(key) % S`. Threads that touch different shards do not wait for each other.

The price is hit ratio: an LRU of 1,000 split into 16 shards of 62 evicts per shard, not globally. On the zipf trace:

```
T9 zipf miss % with a cache of 1000 as one LRU: 60.76; as 16 shards of 62: 60.84
```

60.76% against 60.84%: almost nothing, because the keys spread evenly. (A skewed hash that put the hot keys into one shard would cost much more.) The shards are `alignas(64)`, so two locks never share a cache line (*false sharing* makes the cores fight over a line even though they use different locks).

Throughput: 2,000,000 operations (a `get`, and a `put` on a miss) split evenly over the threads, cache of 20,000 keys, 200,000 keys, zipf traffic and uniform traffic; times in ms, run 1 / run 2:

| Threads | Zipf, 1 lock | Zipf, 16 shards | Uniform, 1 lock | Uniform, 16 shards |
|---|---|---|---|---|
| 1 | 137.2 / 114.8 | 158.9 / 126.1 | 198.0 / 181.7 | 226.6 / 211.4 |
| 2 | 1190.4 / 1454.5 | 823.7 / 874.3 | 1815.1 / 1851.7 | 1110.5 / 1008.3 |
| 4 | 418.7 / 567.6 | 542.1 / 598.2 | 766.1 / 694.9 | 879.8 / 782.9 |

Read it with care; this is a 2-core virtual machine and the numbers are noisy.

- **One thread:** the sharded version is 10 to 16% slower (the extra hash and the indirection), as expected when there is no contention.
- **Two threads, one lock:** about **9 to 13 times slower than one thread**, on the same total work. Adding a thread to a contended lock made the program much slower, not faster: the lock and the cache lines it protects bounce between the cores.
- **Two threads, 16 shards:** better than one lock (roughly 1.4 to 1.8 times), but still several times slower than a single thread. Sharding reduced the contention without removing it; every access still writes shared memory (the list update).
- **Four threads on two cores:** faster than two threads, which I did not investigate (oversubscription changes how the threads are scheduled around the lock). I would not draw a conclusion from it.

What the experiment does show is the interview answer: *a cache whose hit mutates shared state does not scale just by adding locks*. The remedies, in the order they are usually tried: shards; making hits cheap (CLOCK, where a hit is a bit store); recording hits in a per-thread buffer and applying them to the policy in batches (the approach I remember in Caffeine, from memory); and giving each thread its own small cache in front of a shared one.

**Where these ideas show up (from memory).** Redis does not keep an exact LRU: it samples a few keys and evicts the oldest of the sample, and offers an LFU mode with a small decaying counter (the Linked lists 05 note). CPU caches use cheap approximations of LRU (tree-based pseudo-LRU) for the same reason as CLOCK. Memcached and the Linux page cache use segmented lists (hot, warm and cold, or active and inactive).

## 9. How to answer the interview question

1. **Clarify:** key and value types, capacity in entries or bytes, expiry, read-heavy or write-heavy, how many threads, what a miss costs, what happens on a miss (who fetches, and is it deduplicated).
2. **State the policy and why:** LRU is the default (adapts fast, simple); say what pattern breaks it (scans) and what you would change (SLRU or an admission filter).
3. **Give the mechanism:** hash map to node, doubly linked list in order of use, node holds its key, sentinel nodes; O(1) get and put.
4. **Say what a hit costs and what scales:** a hit writes shared state; shards or CLOCK for concurrency.
5. **Mention what you would measure:** hit ratio on a recorded trace, not on intuition.
6. **Edge cases:** capacity 0 or 1, updating an existing key counts as a use, eviction of a key that is being fetched, expiry versus eviction, values larger than capacity.

## 10. Complexities

| Operation | Time | Notes |
|---|---|---|
| LRU get / put (map + list) | `O(1)` | a hit rewrites 4 pointers |
| LFU get / put (frequency lists) | `O(1)` | see Linked lists 05; the model here is `O(log n)` |
| CLOCK hit | `O(1)`, one bit | no structure change |
| CLOCK miss | `O(1)` amortised, up to `c` hand steps once | every bit may be set |
| SLRU | `O(1)` | two lists |
| Count-min sketch add / estimate | `O(rows)` = 4 | `rows * width` bytes, independent of the number of keys |
| Belady's OPT | `O(n log c)` | offline only |
| Lazy expiry | `O(1)` extra per `get` | expired entries linger until touched or evicted |

## 11. Common mistakes

1. **Not moving the node on a `put` of an existing key.** An update is a use.
2. **Evicting from the wrong end**, or reading the victim's key after freeing it.
3. **Counting a failed `get` as a use** in LFU, or not resetting the count on re-entry (the policy of this tutorial and of LeetCode 460 is that counts live only while cached).
4. **Assuming LRU is the best answer** without naming a scan.
5. **Assuming a bigger cache never hurts**: true for LRU and OPT, false for FIFO (Belady).
6. **Treating OPT as an unbreakable bound** for caches that may refuse to insert.
7. **A sketch that takes the maximum of its rows**, or counters that never age: it over-counts and the filter freezes.
8. **Expiry by timer only**, with no check on `get`: a reader can see an expired value.
9. **Taking the lock only on `put`.** A hit mutates the list too; without the lock the structure is corrupted (the one bug here that only ThreadSanitizer sees).
10. **Sharding with a poor hash** (hot keys in one shard) or `capacity / shards` rounding to 0.
11. **Putting two locks in the same cache line**, so unrelated threads contend.
12. **Judging a policy on one trace.** The four rows of section 4 order the policies differently.

## 12. Questions to answer out loud

1. What is the hit ratio, and which two layers decide it and its cost?
2. What is Belady's OPT? Why can't it be built, and why is it still useful? When can a policy beat it?
3. Why does LRU miss on every access of a loop one key longer than the cache?
4. What input makes LFU miss 97.5% of the time, and what fixes it?
5. What is Belady's anomaly? Which policies have the stack property?
6. How does CLOCK approximate LRU, and why is a CLOCK hit cheaper?
7. How does SLRU resist a scan? What does TinyLFU decide, and with what data structure?
8. Why does a count-min sketch never under-count? Why does it age?
9. Describe the flat LRU. What does it save, and how does lazy expiry work?
10. Why does a mutex around an LRU not scale? What does lock striping cost in hit ratio?
11. What is false sharing, and what stops it?
12. How would you decide, for a real system, which policy to use?

## 13. Problems

### 13.1 On paper

Answer first, then compare with the answers at the bottom.

**A.** Cache of 2, accesses `1 2 1 3 1 2`. How many misses do FIFO, LRU, LFU, CLOCK and OPT have?

**B.** Cache of 4, accesses `0 1 2 3 4 0 1 2 3 4 ...` (20 accesses). How many misses do LRU, FIFO, CLOCK and OPT have?

**C.** The flat LRU of section 7, capacity 2: `put(1, 10, now 0, ttl 3)`, `put(2, 20, now 1, ttl 0)`, then `get(1)` at time 2, `put(3, 30)` at time 3, `get(1)` at time 3, `get(2)` at time 4, `get(3)` at time 4. Which `get`s hit, and what is the size at the end?

**D.** Cache of 2, accesses `5 6 5 7 6 5 8 5`. How many misses do LRU, LFU and OPT have?

**E.** For the Belady trace of section 3, how many misses does FIFO have with 3 slots and with 4?

**F.** Why does the sketch's estimate of a key never fall below its true count (before any aging, and below the cap of 15)?

### 13.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | Where | How to approach |
|---|---|---|---|
| LRU Cache | 146 | LeetCode | Linked lists 05, section 6; then rewrite it with arrays and indices (section 7) |
| LFU Cache | 460 | LeetCode (hard) | Frequency lists and `minFreq` (Linked lists 05, section 7) |
| All O(1) Data Structure | 432 | LeetCode (hard) | The same bucket idea as LFU: a list of counts, each holding the keys with that count |
| Design Hit Counter | 362 | LeetCode (premium) | A queue of timestamps, or a ring of buckets; expiry is lazy |
| Time Based Key-Value Store | 981 | LeetCode | Binary search over each key's timestamps (the next tutorial) |
| Design In-Memory File System | 588 | LeetCode (hard) | A trie of directories (a later tutorial in this area) |

### 13.3 In your editor

**A.** Add a **write-back** mode to `LruMap`: a dirty flag per entry and a callback that writes a dirty entry out when it is evicted. Test with a model.

**B.** Add a second policy to the simulator: **2Q** or **ARC** (look the algorithm up first) and add its row to the four traces of section 4.

**C.** Make CLOCK **concurrent**: a hit does an atomic store of the bit; a miss takes a lock for the sweep. Test under ThreadSanitizer and measure against the sharded LRU on 2 and 4 threads.

**D.** Record the miss ratio of each policy on a **real trace** from your own work (a log of keys from a service or a file system) and see whether the ranking of section 4 holds.

**E.** Replace the `std::unordered_map` in `LruMap` by an open-addressing table and repeat the measurement of section 7.

### Answers

The program that checks the paper problems printed:

```
A FIFO 5 LRU 4 LFU 4 CLOCK 4 OPT 4
B LRU 20 FIFO 20 CLOCK 20 OPT 8
C 1 0 0 1 size 1
D LRU 6 LFU 5 OPT 5
E FIFO 9 10
```

Reading it line by line:

**A.** FIFO 5, LRU 4, LFU 4, CLOCK 4, OPT 4. FIFO evicts key 1 when 3 arrives although 1 is about to be used again.

**B.** LRU 20, FIFO 20, CLOCK 20, OPT 8. Every recency-based policy misses on every access of a loop longer than the cache.

**C.** `get(1)` at time 2 hits, `get(1)` at time 3 misses (its lifetime ends at time 3, so it has expired and is removed), `get(2)` at time 4 misses (key 2 was evicted when `put(3)` needed room: it was the least recent after the hit on key 1), and `get(3)` at time 4 hits. The line prints `1 0 0 1` and the final size is 1.

**D.** LRU 6, LFU 5, OPT 5.

**E.** FIFO 9 with 3 slots and 10 with 4.

**F.** Every add of a key increments one counter in each row, so each of the key's counters is at least the key's true count; the estimate is the minimum of those counters, so it is also at least the true count. Collisions only add to a counter. The cap of 15 and the halving of aging are the two ways it can fall below the truth, which is why the statement is limited to before aging and below the cap.

## Cheat sheet

- **Metric:** hit ratio. **Layers:** policy decides the ratio; mechanism decides the cost per hit and the concurrency.
- **OPT** (farthest next use) is the bound for caches that must insert; an admission filter can beat it.
- **LRU:** adapts fast, dies on a loop of `c + 1` keys (100% misses) and is flushed by scans. **LFU:** wins on stable skew, loses (97.5% misses here) when popularity moves. **FIFO:** has Belady's anomaly. **CLOCK:** one bit per key, hits are cheap. **SLRU / TinyLFU:** scan resistance.
- **TinyLFU:** count-min sketch (4 rows, minimum of the counters, saturate at 15, halve every `10 * c` adds); admit only if the newcomer is more frequent than the victim.
- **Flat LRU:** arrays and index links, a free list, lazy expiry on `get`.
- **Threads:** a hit mutates shared state, so one lock does not scale (2 threads were 9 to 13 times slower than 1 here); shards help some; CLOCK or batched updates help more; pad shards to 64 bytes.
- **Always:** measure on a recorded trace.

---

## Sources

- All code is tested here (12,146,680 checks under ASan and UBSan; ThreadSanitizer clean); miss ratios, traces and timings are measured on one 2-core Xeon container, two runs each. The multi-thread times are noisy and specific to this machine.
- Belady (1966) for the optimal policy and the anomaly, the count-min sketch (Cormode and Muthukrishnan) and the TinyLFU admission policy (Einziger, Friedman and Manes), the segmented LRU idea, and the CLOCK algorithm are standard results, described from memory. The statements about Redis, Caffeine, Linux, Memcached and CPU caches are from memory and were not checked against their documentation here; for Redis see also the sources of Linked lists 05.
- The explanations, the traces and the code are my own. LeetCode numbers and titles are from memory; check a number before you rely on it.
