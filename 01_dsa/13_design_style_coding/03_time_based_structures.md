# DSA 13.03: Time-Based Structures

*Part of the [Design-style coding plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Binary search](../03_binary_search_and_sorting/00_plan.md) (`upper_bound` on sorted data), heaps, and [Cache designs](01_cache_designs.md) (section 7 has lazy expiry; this tutorial goes further)*

A large share of "design" questions have a clock hidden in them: *get the value as of time t*, *how many requests in the last 5 minutes*, *allow at most 100 calls per second*, *run this after 30 seconds*, *can this meeting be booked*. The structures are small. The difficulty is in **boundaries** (is the window `[t-w, t]` or `(t-w, t]`?), in **what the clock is allowed to do** (go backwards? repeat?), and in the **memory** a naive version needs when traffic is heavy. This tutorial builds the standard set and measures where the naive versions break.

By the end you can:

- store and read values by time with a binary search (and say when an ordered map is needed instead), including a snapshot array that stores only changes;
- count events in a sliding window two ways (every event, or one bucket per tick) and say what each costs at a given rate;
- implement four rate limiters (fixed window, sliding log, sliding counter, token bucket), and show with numbers which of them lets through twice the limit;
- find the token bucket's classic bug, and the numbers that expose it;
- build a map with expiry that survives a key being written again before its deadline, and a timer facility as a heap and as a timing wheel, with measurements;
- book intervals without overlap with a neighbour check, and find the deepest overlap with a sweep.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 9,283,177 checks passed under AddressSanitizer and UBSan. The tests compare each class against a model that is deliberately simple: a list of every event, a per-tick simulation for the token bucket, a map of per-window counts for the counter, and brute-force overlap tests for the calendars. Twenty deliberate bugs were tried. Nineteen were caught at once. One (a snapshot array that appends a new entry where it should overwrite) escaped: every answer it gives is correct and only the memory is wrong, so I added a check on the number of stored entries and it is now caught.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. The traffic is synthetic and seeded; it shows how each structure behaves, not what your service sees. Statements about real systems are from memory and marked as such.

---

## 1. The clock is an input

Every class below takes the time as an **argument** (`now`, `t`), never reading a clock itself. That is what makes them testable and what interviewers look for. A few rules to state aloud:

1. **Use integer ticks.** Seconds or milliseconds in a `long long`. Floating-point time makes `<=` comparisons unreliable.
2. **Say whether time can go backwards.** A real clock can: the wall clock jumps when it is corrected (from memory: the C++ steady clock exists because the system clock is not monotonic). Here `now` never decreases, and where a class needs that, the code says so.
3. **Windows are half-open: `(t - w, t]`.** "The last 300 seconds" at `t = 300` includes second 300 and excludes second 0. Write this down before coding; every off-by-one below comes from it.
4. **Equal timestamps happen.** Two events in the same tick are normal.

```cpp
// Version 1 (LeetCode 981): timestamps for one key only grow. Append to a vector, binary search to read.
class TimeMapAppend {
    std::unordered_map<std::string, vector<std::pair<Time, std::string>>> m;
public:
    void set(const std::string& k, const std::string& v, Time t) {
        auto& h = m[k];
        if (!h.empty() && t < h.back().first) throw std::invalid_argument("timestamps must not decrease");
        if (!h.empty() && t == h.back().first) h.back().second = v;
        else h.push_back({t, v});
    }
    std::string get(const std::string& k, Time t) const {      // value with the largest timestamp <= t
        auto it = m.find(k);
        if (it == m.end()) return "";
        const auto& h = it->second;
        auto p = std::upper_bound(h.begin(), h.end(), t, [](Time x, const auto& e) { return x < e.first; });
        return p == h.begin() ? "" : std::prev(p)->second;
    }
};

// Version 2: timestamps arrive in any order. An ordered map per key, O(log m) for both calls.
class TimeMapAny {
    std::unordered_map<std::string, std::map<Time, std::string>> m;
public:
    void set(const std::string& k, const std::string& v, Time t) { m[k][t] = v; }
    std::string get(const std::string& k, Time t) const {
        auto it = m.find(k);
        if (it == m.end()) return "";
        auto p = it->second.upper_bound(t);
        return p == it->second.begin() ? "" : std::prev(p)->second;
    }
};

// Snapshot array (LeetCode 1146): per index, only the changes, tagged with the snapshot id.
class SnapshotArray {
    vector<vector<std::pair<int, int>>> h;     // h[i] = (snapshot id, value), ids increasing
    int snaps = 0;
public:
    explicit SnapshotArray(size_t n) : h(n, vector<std::pair<int, int>>{{0, 0}}) {}
    void set(size_t i, int v) {
        auto& e = h.at(i);
        if (e.back().first == snaps) e.back().second = v; else e.push_back({snaps, v});
    }
    int snap() { return snaps++; }
    int get(size_t i, int id) const {
        if (id < 0 || id >= snaps) throw std::out_of_range("snapshot id");
        const auto& e = h.at(i);
        auto p = std::upper_bound(e.begin(), e.end(), id, [](int x, const auto& q) { return x < q.first; });
        return std::prev(p)->second;
    }
    size_t stored() const { size_t s = 0; for (auto& e : h) s += e.size(); return s; }
};
```

## 2. Values by time

**Time map.** `set(key, value, t)` and `get(key, t)` returning the value with the **largest timestamp at most `t`**. If the timestamps for a key only grow, each key holds a sorted vector and `get` is `upper_bound` followed by one step back: `O(log m)` and no tree. If timestamps can arrive in any order, you need an ordered map per key.

```
T1 time map after set(foo,bar,1): get(foo,1)=bar; get(foo,3)=bar; get(foo,4)=bar2; get(foo,5)=bar2; get(foo,0)=''
T1 out of order: set(k,v5,5), set(k,v3,3), set(k,v5b,5): get(k,4)=v3, get(k,5)=v5b, get(k,2)=''
T1 the append-only version on set(foo,x,2) after timestamp 4: throws
```

The append-only version rejects a decreasing timestamp and overwrites on an equal one, which makes its rule explicit. 1,000,000 sets and 1,000,000 gets over 1,000 keys with growing timestamps (ms, run 1 / run 2):

| Version | Time |
|---|---|
| Vector per key, binary search | 357.7 / 364.0 |
| `std::map` per key | 1028.3 / 931.8 |

The vector is about 2.6 to 2.9 times faster here. Part of that is the tree's allocation per entry; I did not separate it from the pointer chasing. The ordered map is the price of accepting any order.

**Snapshot array.** `set(i, v)`, `snap()` returning a snapshot id, and `get(i, id)` returning index `i` as it was at that snapshot. Copying the array at every snapshot is `O(n)` per snapshot. Store only the **changes**: for each index, a list of `(snapshot id, value)` in increasing id order. A change made in the current snapshot overwrites the last entry; `get` is `upper_bound` on the id. It is the time map again, with the snapshot counter as the clock.

```
T2 snapshot array of 3: set(0,5), snap()=0, set(0,6), snap()=1: get(0,0)=5, get(0,1)=6, get(1,1)=0, stored entries 4
T2 array of 100000, 1000 snapshots with 10 changes before each: entries stored 109989; copying the array at every snapshot would store 100000000
```

Memory is proportional to the number of changes, not to the array size times the number of snapshots: 109,989 entries against 100,000,000.

## 3. Counting events in a window

*How many hits in the last `w` ticks?* Two designs:

- **Log:** a queue of timestamps. `hit` pushes; `count` pops everything older than the window. Exact, `O(1)` amortised, but memory is one entry per hit inside the window.
- **Ring:** `w` buckets, one per tick, indexed by `t % w`, each storing its tick and a count. A bucket whose stored tick is old is reused. Memory is `w` whatever the rate; `count` adds up `w` buckets.

```cpp
// Hits in the last w ticks, i.e. in (t - w, t]. All calls must come with non-decreasing t.
// Version 1: remember every hit. Exact, memory proportional to the hits inside the window.
class HitCounterLog {
    Time w;
    std::deque<Time> q;
public:
    explicit HitCounterLog(Time window) : w(window) {}
    void hit(Time t) { q.push_back(t); }
    long long count(Time t) {
        while (!q.empty() && q.front() <= t - w) q.pop_front();
        return (long long)q.size();
    }
    size_t stored() const { return q.size(); }
};

// Version 2: one bucket per tick of the window (t % w). Memory w, whatever the hit rate; count is O(w).
class HitCounterRing {
    Time w;
    vector<Time> stamp;
    vector<long long> cnt;
public:
    explicit HitCounterRing(Time window) : w(window), stamp((size_t)window, -1), cnt((size_t)window, 0) {}
    void hit(Time t) {
        size_t i = (size_t)(t % w);
        if (stamp[i] != t) { stamp[i] = t; cnt[i] = 0; }   // the bucket still holds an old tick: reuse it
        ++cnt[i];
    }
    long long count(Time t) const {
        long long s = 0;
        for (size_t i = 0; i < stamp.size(); ++i) if (stamp[i] > t - w && stamp[i] <= t) s += cnt[i];
        return s;
    }
};
```

```
T3 hit counter, window 300, hits at 1 2 3 300 301 (log/ring): count(300)=4/4; count(301)=4/4; count(302)=3/3; count(601)=0/0
T3 1,000,000 hits at 1000 per tick for 1000 ticks: the log held up to 300000 timestamps, the ring held 300 buckets
```

(The 301st tick reuses the bucket of tick 1, which is why the bucket stores its own tick.) The two give identical answers in the tests. At 1,000 hits per tick the log held 300,000 timestamps; the ring held 300 buckets. Their running times for 1,000,000 hits and 1,000 counts were close (ms, run 1 / run 2): log 2.6 / 2.8, ring 3.3 / 2.0. So the reason to pick the ring is **memory**, not speed. The log is the answer when the rate is low or you need the timestamps themselves.

## 4. Rate limiters

*Allow at most `limit` requests per `window` ticks.* Four standard answers. All take `now` and return whether the request may proceed.

```cpp
// All four answer allow(now) for "may this request go through?". `now` never decreases.
// 1. Fixed window: a counter per window of w ticks. Cheapest, but a burst at a window edge doubles the limit.
class FixedWindow {
    long long limit; Time w; Time win = -1; long long n = 0;
public:
    FixedWindow(long long lim, Time window) : limit(lim), w(window) {}
    bool allow(Time now) {
        Time id = now / w;
        if (id != win) { win = id; n = 0; }
        if (n < limit) { ++n; return true; }
        return false;
    }
};

// 2. Sliding window log: remember the accepted times. Exact, memory up to `limit`.
class SlidingLog {
    long long limit; Time w; std::deque<Time> q;
public:
    SlidingLog(long long lim, Time window) : limit(lim), w(window) {}
    bool allow(Time now) {
        while (!q.empty() && q.front() <= now - w) q.pop_front();
        if ((long long)q.size() < limit) { q.push_back(now); return true; }
        return false;
    }
};

// 3. Sliding window counter: two fixed windows, the previous one weighted by how much of it still overlaps.
class SlidingCounter {
    long long limit; Time w; Time cur = -1; long long prev = 0, curN = 0;
public:
    SlidingCounter(long long lim, Time window) : limit(lim), w(window) {}
    bool allow(Time now) {
        Time id = now / w;
        if (id != cur) { prev = (id == cur + 1) ? curN : 0; curN = 0; cur = id; }   // a gap of 2+ windows forgets everything
        Time elapsed = now - id * w;
        long long est = prev * (w - elapsed) / w + curN;
        if (est < limit) { ++curN; return true; }
        return false;
    }
};

// 4. Token bucket: `capacity` tokens, one more every `period` ticks. The level is kept in ticks
//    (capacity * period when full), so no fraction of a token is ever lost.
class TokenBucket {
    Time period, cap, level, last = 0;
public:
    TokenBucket(long long capacity, Time per) : period(per), cap(capacity * per), level(capacity * per) {}
    bool allow(Time now, long long cost = 1) {
        Time gain = now - last;
        level = gain >= cap ? cap : std::min(cap, level + gain);     // the first test avoids overflow on a huge gap
        last = now;
        Time need = cost * period;
        if (level >= need) { level -= need; return true; }
        return false;
    }
};
```

**Fixed window** counts per window `[k w, (k+1) w)`. One integer of state. But requests at the end of one window and the start of the next are counted separately. **Sliding log** keeps the accepted times: exact, `O(limit)` memory. **Sliding counter** keeps the current and the previous window's counts and estimates the sliding count as `prev * (fraction of the previous window still inside) + current`: two integers, approximate. **Token bucket** keeps a level that refills with time and is spent per request: it allows a **burst** up to its capacity, then the steady rate.

A burst across a window boundary, limit 100 per 1,000 ticks: 100 requests at tick 999, then 100 at tick 1000:

```
T4 100 requests at tick 999 and 100 at tick 1000, limit 100 per 1000 ticks: accepted fixed window 200, sliding log 100, sliding counter 100, token bucket (50, one per 20 ticks) 50
```

The fixed window accepted **200**, twice the limit, in two adjacent ticks. The log and the counter held to 100. The token bucket (capacity 50, one token per 20 ticks, so also 50 per 1,000 ticks) accepted its burst of 50 and nothing more. Under random bursts (20,000 bursts of 1 to 300 requests, 3,008,845 requests in all), the **most accepted in any window of 1,000 ticks**:

```
T5 bursty traffic, 3008845 requests, limit 100 per 1000 ticks: most accepted in any 1000 ticks: fixed window 200, sliding log 100, sliding counter 198, token bucket 99
T5 requests accepted: fixed window 1320410, sliding log 1095326, sliding counter 1093412, token bucket 713861
```

- The fixed window reached 200, as predicted.
- The sliding log never exceeded 100: it is exact.
- The sliding counter reached **198**. The estimate assumes the previous window's requests were spread evenly. When they were all packed at the end of that window, the estimate undercounts, and the limiter lets almost double through. It is a good limiter for smooth traffic and a weak one against adversarial bursts.
- The token bucket's bound is its capacity plus the refill over the window: 50 + 50 = 100. It stayed at 99. It also accepted the fewest requests (713,861), because after a burst it must wait for tokens.

How close is the counter to the exact log on steady traffic that is slightly above the limit?

```
T6 1,000,000 steady requests (about 125 per 1000 ticks, limit 100): accepted exact log 791913, sliding counter 797485 (0.70% apart), same decision on 77.88% of requests, most in any 1000 ticks for the counter 105
```

Totals are within 0.7%, but they made the **same decision on only 77.9%** of the requests, and the counter went to 105 in a window. "Approximately equal in total" does not mean "equal per request".

**The token bucket bug.** The natural version keeps an integer token count and a time of last refill: `tokens += (now - last) / period; last = now`. When requests come more often than `period`, the quotient is 0 each time, `last` moves to `now` anyway, and the fractions of a token are **thrown away forever**. The class above avoids this by keeping the level in **ticks** (a full bucket is `capacity * period`); time adds ticks directly and a request subtracts `cost * period`. Nothing is rounded. A request every 9 ticks against a refill of one token per 10 ticks should accept 90%:

```
T7 token bucket 5 tokens, one per 10 ticks, a request every 9 ticks, 10000 requests: kept fractions accepts 9004 (90.0%); resetting 'last' to now accepts 5 (0.05%)
```

The wrong version accepted 5 requests in total, the initial burst, and then starved. The tests check the correct one against a model that adds one tick at a time, and separately check that a gap of four quintillion ticks neither overflows nor lets through more than the capacity.

Memory and cost: fixed window and counter use a few integers and `O(1)` time; the log uses up to `limit` timestamps; the bucket uses two integers. For a service with many clients, each client has its own limiter, so the per-client state is what you multiply. With several threads, the state of one limiter needs a lock or atomic updates; I did not test a concurrent version.

## 5. Things that expire

**Expiring map.** Cache designs (section 7) expired entries lazily, when they were read. Memory is only freed when someone looks. To free it on a schedule you need to know **which entries expire next**: a min-heap of `(expiry, key)`. The trap is that a key can be written again before its first deadline. The heap still holds the old deadline, and removing the key at that deadline would delete the **new** entry. Each entry therefore carries a version number, and `purge` removes a key only when the heap entry's version is the key's current one.

```cpp
// A map whose entries expire. Reads check the expiry; purge() frees memory using a min-heap of expiry times.
// A key can be put again before its first deadline, so every heap entry carries a version number.
class ExpiringMap {
    struct Rec { int val; Time exp; long long ver; };
    struct Ev { Time exp; int key; long long ver; bool operator>(const Ev& o) const { return exp > o.exp; } };
    std::unordered_map<int, Rec> m;
    std::priority_queue<Ev, vector<Ev>, std::greater<Ev>> pq;
    long long ver = 0;
public:
    void put(int k, int v, Time now, Time ttl) {
        if (ttl <= 0) throw std::invalid_argument("ttl");
        Time e = now + ttl;
        m[k] = {v, e, ++ver};
        pq.push({e, k, ver});
    }
    std::optional<int> get(int k, Time now) {
        auto it = m.find(k);
        if (it == m.end()) return std::nullopt;
        if (it->second.exp <= now) { m.erase(it); return std::nullopt; }
        return it->second.val;
    }
    size_t purge(Time now) {                   // remove everything that has expired; returns how many
        size_t n = 0;
        while (!pq.empty() && pq.top().exp <= now) {
            Ev e = pq.top(); pq.pop();
            auto it = m.find(e.key);
            if (it != m.end() && it->second.ver == e.ver) { m.erase(it); ++n; }   // skip entries put again since
        }
        return n;
    }
    size_t size() const { return m.size(); }
};

// Timers, version 1: a min-heap. schedule and fire are both O(log n). Ties fire in scheduling order.
class TimerHeap {
    using E = std::tuple<Time, long long, int>;                // (fire time, sequence number, id)
    std::priority_queue<E, vector<E>, std::greater<E>> pq;
    Time now_ = 0; long long seq = 0;
public:
    void schedule(Time delay, int id) { if (delay < 1) throw std::invalid_argument("delay"); pq.push({now_ + delay, seq++, id}); }
    vector<int> tick() {                       // advance one tick, return the ids that are due
        ++now_;
        vector<int> out;
        while (!pq.empty() && std::get<0>(pq.top()) <= now_) { out.push_back(std::get<2>(pq.top())); pq.pop(); }
        return out;
    }
};

// Timers, version 2: a hashed timing wheel. A timer goes into slot (fire time % n); a tick looks at one slot.
// schedule is O(1); a tick costs the number of timers in the slot, and a timer further away than n
// ticks stays in its slot until its own tick comes round.
class TimerWheel {
    size_t n;
    vector<vector<std::pair<Time, int>>> slot;                 // (fire time, id), in scheduling order
    Time now_ = 0;
public:
    explicit TimerWheel(size_t slots) : n(slots), slot(slots) {}
    void schedule(Time delay, int id) {
        if (delay < 1) throw std::invalid_argument("delay");
        Time at = now_ + delay;
        slot[(size_t)(at % (Time)n)].push_back({at, id});
    }
    vector<int> tick() {
        ++now_;
        auto& s = slot[(size_t)(now_ % (Time)n)];
        vector<int> out;
        size_t keep = 0;
        for (auto& e : s) { if (e.first == now_) out.push_back(e.second); else s[keep++] = e; }
        s.resize(keep);
        return out;
    }
};
```

```
T8 expiring map: put(1,10,t0,ttl5), put(2,20,t0,ttl10), put(1,11,t3,ttl10): get(1,6)=11; purge(6) removes 0, size 2; get(2,10)=miss; purge(13) removes 1, size 0
```

(Key 1 was put again at time 3, so its first deadline at 5 is stale: `purge(6)` removes nothing and `get(1, 6)` still sees the second value. Key 2 expired at 10, so `get(2, 10)` misses, and the later purge only has to remove key 1.)

**Timers: heap and wheel.** *Run this callback after `d` ticks.* The heap version is `O(log n)` to schedule and to fire. A **timing wheel** is an array of `n` slots; a timer firing at tick `T` goes into slot `T % n`, and each tick looks at exactly one slot. Scheduling is `O(1)`; a tick costs the size of the slot. A timer further away than `n` ticks shares a slot with nearer ones and waits there until its own tick (the entry stores its fire time).

```
T9 timers with delays 3 1 3 5 1 9 as ids 1 to 6, wheel of 4 slots: tick 1: 2 5; tick 3: 1 3; tick 5: 4; tick 9: 6; wheel and heap agree: yes
```

(Timers 2, 4, 5 and 6 fire at ticks 1, 5, 1 and 9, and all four sit in slot 1 of 4: at tick 1 only 2 and 5 fire, and 4 and 6 wait.) The two give the same firing order in the tests. The cost, with 100,000 live timers where every fired timer is rescheduled (ms, run 1 / run 2):

| Delays | Wheel (1,024 slots) | Heap |
|---|---|---|
| Up to 1,000 ticks, 3,000,000 firings | 60.3 / 43.7 | 580.4 / 566.4 |
| Up to 100,000 ticks, 300,000 firings | 38.8 / 39.1 | 65.5 / 64.0 |

```
T11 100,000 live timers with delays up to 1000, 3,000,000 firings: wheel and heap fired the same timers in the same order: yes
T11 the same with delays up to 100,000 and 300,000 firings (1024 slots): same result yes
```

When the delays fit inside the wheel, it is about **10 to 13 times faster** than the heap here. When delays are 100 times longer than the wheel, each slot holds about a hundred timers and a tick scans them all; the advantage drops to about 1.5 to 1.7 times. The fix used in practice is a hierarchy of wheels (seconds, minutes, hours), where long timers sit in a coarse wheel and move down as they approach (from memory; this tutorial builds only the single wheel). Cancelling a timer is not implemented: with a heap the usual answer is a flag checked when the timer pops; with a wheel, a flag or a per-slot erase.

## 6. Intervals in time

**Calendar I** (accept a booking only if it overlaps nothing). Bookings are half-open `[s, e)`, so `[10, 20)` and `[20, 30)` are fine. Keep the accepted bookings in an ordered map from start to end. A new `[s, e)` can only conflict with its **two neighbours**: the first booking starting at or after `s` (conflict if it starts before `e`) and the last one starting before `s` (conflict if it ends after `s`). `O(log n)`.

**Calendar III** (accept everything, report the deepest overlap). Add `+1` at each start and `-1` at each end in an ordered map, and sweep: the running sum's maximum is the answer. It costs `O(n)` per booking; a segment tree with lazy updates (or coordinate compression) gives `O(log n)` and is the follow-up question.

```cpp
// Half-open intervals [s, e): [10,20) and [20,30) do not overlap.
// Calendar I (LeetCode 729): accept a booking only if it overlaps nothing. Look at the two neighbours only.
class CalendarI {
    std::map<Time, Time> b;                    // start -> end of the accepted bookings, which never overlap
public:
    bool book(Time s, Time e) {
        if (s >= e) throw std::invalid_argument("empty interval");
        auto nxt = b.lower_bound(s);           // first booking starting at or after s
        if (nxt != b.end() && nxt->first < e) return false;
        if (nxt != b.begin() && std::prev(nxt)->second > s) return false;
        b[s] = e;
        return true;
    }
};

// Calendar III (LeetCode 732): accept everything, report the largest number of bookings that overlap.
// A map of +1 at each start and -1 at each end; a sweep gives the overlap. O(n) per booking.
class CalendarIII {
    std::map<Time, int> d;
public:
    int book(Time s, Time e) {
        if (s >= e) throw std::invalid_argument("empty interval");
        ++d[s]; --d[e];
        int cur = 0, best = 0;
        for (auto& kv : d) { cur += kv.second; best = std::max(best, cur); }
        return best;
    }
};
```

```
T10 calendar I: book(10,20)=true; book(15,25)=false; book(20,30)=true
T10 calendar III, books (10,20) (50,60) (10,40) (5,15) (5,10) (25,55): largest overlap after each -> 1 1 2 3 3 3
```

For 20,000 random bookings, the neighbour check against a scan of all accepted bookings (ms, run 1 / run 2):

| Method | Time |
|---|---|
| Ordered map, two neighbours | 7.2 / 5.8 |
| Scan of all accepted bookings | 500.5 / 486.9 |

```
T12 20000 random bookings: accepted 18192 by the map version, 18192 by the scan (equal: yes)
```

Both accepted the same 18,192 bookings. The scan is about 70 to 100 times slower, and it is quadratic: it would be hopeless at a million bookings.

## 7. How to answer a time-based design question

1. **Pin down the time model:** units, whether `now` can repeat or go backwards, whether timestamps come in order.
2. **Write the window or interval convention** with a tiny example (`(t-w, t]`, `[s, e)`).
3. **Choose exact or approximate** and say the price: the log is exact and uses memory per event; buckets or counters use fixed memory and are approximate or fixed-resolution.
4. **Name the structure by the query:** "largest timestamp at most t" is a binary search; "next to expire" is a heap or a wheel; "neighbours of a point" is an ordered map; "deepest overlap" is a sweep.
5. **Test the edges:** empty, one event, an event exactly on a boundary, two events in the same tick, a gap longer than the window, a very large timestamp.
6. **Then threads and scale:** per-client state, one lock per limiter, sharding by key.

## 8. Complexities

| Structure | Per call | Memory |
|---|---|---|
| Time map, vector per key | set `O(1)`, get `O(log m)` | one entry per set |
| Time map, ordered map | `O(log m)` | one tree node per set |
| Snapshot array | set `O(1)`, get `O(log c)` | one entry per change |
| Hit counter, log | `O(1)` amortised | hits in the window |
| Hit counter, ring | hit `O(1)`, count `O(w)` | `w` buckets |
| Fixed window / counter / bucket | `O(1)` | 2 to 3 integers |
| Sliding log | `O(1)` amortised | up to `limit` |
| Expiring map | put `O(log n)`, get `O(1)`, purge `O(log n)` per expired entry | map plus heap |
| Timer heap | `O(log n)` | `n` |
| Timer wheel | schedule `O(1)`, tick = slot size | `n` timers plus the slots |
| Calendar I | `O(log n)` | `n` |
| Calendar III (sweep) | `O(n)` per booking | `2n` points |

## 9. Common mistakes

1. A window that is closed on both ends (`[t-w, t]`), so it covers `w + 1` ticks.
2. Floating-point or mixed units for time.
3. A hit-counter ring that does not store the tick in the bucket, so a bucket from a previous lap is counted.
4. Resetting a fixed-window counter in the wrong place, or trusting it near a window edge.
5. A sliding counter that forgets to zero the previous count after a gap of two or more windows.
6. Treating the sliding counter as exact: it reached 198 against a limit of 100 under bursts.
7. A token bucket that stores integer tokens and sets `last = now`: the refill is lost whenever requests come faster than the period.
8. A level that is not capped at the capacity, so a long quiet period stores unlimited burst; a gap that overflows `level + gap`.
9. Expiry that removes by key from the heap without a version: a key written again is deleted early.
10. A `get` that treats `exp == now` as alive, when the entry expires at `exp`.
11. A timing wheel that fires everything in the slot instead of only the timers whose own tick has come.
12. Calendar bookings with `<=` where `<` is needed, rejecting two meetings that touch.
13. A snapshot array that copies the array or appends an entry for every `set`.

## 10. Questions to answer out loud

1. Why do these classes take `now` as an argument? What can go wrong with a real clock?
2. Which interval convention do you use for windows and for bookings, and why does it matter?
3. When does a time map need a tree instead of a vector?
4. Why does a snapshot array store only changes? What is its `get`?
5. Compare the hit-counter log and ring: memory, speed, exactness.
6. Which rate limiters let through twice the limit, and when?
7. How does a token bucket allow a burst and still bound the rate? What is its bound over a window?
8. Why does `last = now` break the token bucket? How does keeping the level in ticks fix it?
9. Why does the expiring map need versions in its heap?
10. How does a timing wheel get `O(1)` scheduling? What happens when a delay is longer than the wheel?
11. Why does Calendar I look at two neighbours only?
12. How would you make Calendar III `O(log n)`?

## 11. Problems

### 11.1 On paper

**A.** Append-only time map: `set(a, x, 2)`, `set(a, y, 5)`, `set(a, z, 9)`. What does `get(a, t)` return for `t = 1, 2, 4, 5, 8, 10`?

**B.** Hit counter, window 5, hits at times 1, 2, 2, 4. Then `count(4)` and `count(6)`. Then hits at 6 and 9, and `count(9)`, `count(10)`, `count(11)`.

**C.** Limit 3 per window of 10 ticks. Requests at 8, 9, 9, 10, 11, 12, 19, 20. For each of the fixed window, the sliding log and the sliding counter: which are accepted?

**D.** Token bucket, capacity 2, one token per 5 ticks, starting full. Requests at 0, 0, 0, 3, 5, 12, 12, 12. Which are accepted?

**E.** A timing wheel of 3 slots. At time 0 schedule delays 2 (id 1), 5 (id 2) and 2 (id 3). After tick 2, schedule delay 1 (id 4) and delay 4 (id 5). What fires at each tick up to 7? Which timers share a slot?

**F.** Bookings `[1,5)`, `[3,8)`, `[8,10)`, `[2,9)`, `[5,8)`. Which does Calendar I accept? What does Calendar III report after each?

**G.** Show that a token bucket with integer tokens and `last = now` accepts almost nothing when requests come every 9 ticks and the refill is one token per 10 ticks.

### 11.2 On LeetCode

Numbers and titles are from memory; check a number before you rely on it. Several are premium problems.

| Problem | Number | How to approach |
|---|---|---|
| Time Based Key-Value Store | 981 | Section 2: a vector per key and `upper_bound` |
| Snapshot Array | 1146 | Section 2: per index, a list of changes tagged with the snapshot id |
| Design Hit Counter | 362 (premium) | Section 3: the log or the ring |
| Logger Rate Limiter | 359 (premium) | A map from message to the next time it may print; bound the memory |
| My Calendar I | 729 | Section 6: two neighbours in an ordered map |
| My Calendar III | 732 (hard) | The sweep, then a segment tree |
| My Calendar II | 731 | Keep the bookings, and the overlaps of two, as two lists; reject a triple |

### 11.3 In your editor

**A.** Add **bounded memory** to the message logger (359): drop entries older than the window with a queue, and test that memory stays bounded.

**B.** Build a **two-level timing wheel** (fine ticks, coarse ticks that cascade) and repeat the measurement of section 5 for delays up to a million.

**C.** Add **cancel** to the timer heap and to the wheel, and measure the cost of cancelling half of the timers.

**D.** Make the token bucket thread-safe with one lock, then with atomic compare-and-swap on a packed level, and measure on 2 and 4 threads.

**E.** Replace the sweep in Calendar III with a **segment tree** over compressed coordinates, and compare for 100,000 bookings.

### Answers

The program that checks the paper problems printed:

```
A '' 'x' 'x' 'y' 'y' 'z'
B count(4)=4/4 count(6)=3/3 count(9)=2/2 count(10)=2/2 count(11)=1/1
C fixed YYYYYYnY log YYYnnnYY counter YYYnYnYY
D YYnnYYnn
E tick1: ; tick2: 1 3 ; tick3: 4 ; tick4: ; tick5: 2 ; tick6: 5 ; tick7: ;
F I YnYnY III 1 2 2 3 3
```

**A.** Empty for `t = 1`; `x` for 2 and 4; `y` for 5 and 8; `z` for 10. (The result `''` means no value yet.)

**B.** `count(4)` is 4; `count(6)` is 3 (the window is `(1, 6]`: the hits at 2, 2 and 4). After the hits at 6 and 9: `count(9)` is 2 (the window `(4, 9]` holds 6 and 9), `count(10)` is 2, `count(11)` is 1 (only 9). The log and the ring agree.

**C.** Fixed window: `YYYYYYnY`. The first window `[0,10)` takes 8, 9, 9; the second `[10,20)` takes 10, 11, 12 and refuses 19; 20 starts a new window. Sliding log: `YYYnnnYY`. The three accepted at 8 and 9 block everything until they leave the window `(t-10, t]` at 19. Sliding counter: `YYYnYnYY`, which differs from both. At 10 the estimate is 3 (the whole previous window) and it refuses; at 11 the previous window is weighted `9/10`, the integer estimate is 2, and it accepts where the exact log refused; at 12 the estimate is back at 3.

**D.** `YYnnYYnn`. Two tokens go at time 0; at 3 the level is 3 ticks, below the 5 needed; at 5 it is 5, enough for one; at 12 the level is 7, enough for one more and not for two.

**E.** Tick 1: nothing. Tick 2: ids 1 and 3. Id 4 (delay 1, scheduled at time 2) fires at tick 3. Tick 4: nothing. Tick 5: id 2. Id 5 (delay 4, scheduled at time 2) fires at tick 6. Tick 7: nothing. Ids 1 and 3 (firing at 2) and id 2 (firing at 5) share slot 2 of 3, since 2 and 5 are both 2 modulo 3: id 2 stays in the slot when the tick-2 timers fire. Ids 4 (firing at 3) and 5 (firing at 6) share slot 0 in the same way.

**F.** Calendar I: `YnYnY` (`[3,8)` overlaps `[1,5)`; `[2,9)` overlaps; `[5,8)` touches both accepted bookings and overlaps neither). Calendar III: 1, 2, 2, 3, 3.

**G.** With one request every 9 ticks, `(now - last) / period = 9 / 10 = 0` on every request, and `last` is set to `now`, so the elapsed ticks are discarded each time and tokens never come back. After the 5 initial tokens, nothing is accepted: 5 of 10,000 in the measurement.

## Cheat sheet

- **Time:** integer ticks, passed in; windows `(t - w, t]`; intervals `[s, e)`; say whether time can go backwards.
- **Time map:** vector per key + `upper_bound` (2.6 to 2.9 times faster than a map per key here); ordered map if timestamps are out of order. **Snapshot array:** store changes only (109,989 entries against 100,000,000).
- **Hit counter:** log (exact, memory per hit) or ring (memory `w`, `count` is `O(w)`).
- **Rate limiters:** fixed window lets through 2x the limit at an edge; sliding log is exact; sliding counter is approximate (reached 198 of 100 under bursts); token bucket allows a burst of its capacity. Keep the bucket level in **ticks**.
- **Expiry:** heap of `(expiry, key, version)`; the version stops a re-written key from being deleted early. **Timers:** the wheel is `O(1)` and 10 to 13 times faster than the heap while delays fit the wheel, 1.5 to 1.7 times when they do not; hierarchical wheels for long delays.
- **Calendars:** I = two neighbours in an ordered map; III = sweep of `+1` / `-1` (`O(n)` per booking), segment tree for `O(log n)`.

---

## Sources

- All code is tested here (9,283,177 checks under ASan and UBSan; 20 deliberate bugs, all caught after one test was added). Traces and timings are measured on one Xeon container, two runs each, with identical traces. The traffic is synthetic.
- The designs (token bucket, sliding window counter, hashed timing wheel, the sweep for overlapping intervals) are standard; I describe them from memory and tested them here. The statements about the system and steady clocks, hierarchical timing wheels and the way real systems use them are from memory and were not checked.
- LeetCode numbers and titles are from memory; check a number before you rely on it. The explanations, traces and code are my own.
