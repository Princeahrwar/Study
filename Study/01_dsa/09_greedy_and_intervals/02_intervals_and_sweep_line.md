# DSA 09.02: Intervals and the Sweep Line

*Part of the [Greedy and intervals plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Greedy algorithms and their proofs](01_greedy_algorithms_and_proofs.md) (activity selection, exchange arguments), [Sorting patterns and comparators](../03_binary_search_and_sorting/05_sorting_patterns_and_comparators.md) (sorting by a key) and [Binary heaps](../07_trees/15_binary_heaps.md) (a priority queue). There is no companion demo file: every structure is listed in this doc

An interval problem is almost always a sorting problem in disguise: sort by the right end of the interval, walk once, and keep one or two numbers. The right key, and above all the right **boundary convention**, decide whether the walk is correct. Does `[1,4]` touch `[4,5]`? For merging the answer is "yes, join them". For meeting rooms the answer is "no, one room is enough". For balloons it is "yes, one arrow hits both". Most wrong answers to interval problems are a `<` where a `<=` was needed, so this document settles the convention first and then builds the toolkit: merge, insert, intersect, meeting rooms, minimum removals, minimum arrows, the event sweep with its difference-array shortcut, and an online calendar on an ordered map.

By the end you can:

- say, for any interval problem, whether the intervals are **closed** `[s, e]` or **half-open** `[s, e)`, and write the test and the tie order that follow from it;
- merge a list of intervals, insert one into a sorted disjoint list in O(n), and intersect two such lists with two pointers, and prove why the pointer that ends first is the one to advance;
- solve meeting rooms I and II three ways (heap, two sorted arrays, event sweep) and know that all three compute the same thing;
- reduce "fewest intervals to remove" and "fewest arrows" to the activity selection of Greedy 01 and explain why the sort key is the end;
- turn any "how many at once" question into an event sweep, and replace the sort by a **difference array** when the coordinates are small integers;
- book intervals online in O(log n) with an ordered map, knowing that only two neighbours can conflict.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 144,585 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: merge against painting the intervals on a grid (a doubled grid for closed intervals, cells for half-open) with sortedness and disjointness checked; insert against merging everything again; intersection against the AND of two painted grids, in both argument orders; meeting rooms, the event sweep (both conventions) and the difference array against a count at every integer point; minimum removals against n minus the best subset out of all 2^n subsets; minimum arrows against the best set of integer points out of all subsets; union length and free gaps against painting; the calendar against a naive scan over 1,500 random booking sequences; one run at n = 1,000,000 against a single difference array; and pins for what random inputs rarely hit (touching ends, an interval inside the last merged one, equal ends in the intersection, an interval ending exactly at the array size, the smallest possible coordinate). Then 49 hand-made mutants of the listings were run against the tests: **43 were caught and 6 are equivalent changes**; section 8 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Closed or half-open: decide first

An interval is a pair `(s, e)`. Two readings:

- **Closed** `[s, e]`: both end points belong to it. `[1,4]` and `[4,5]` share the point 4, so they **overlap**. A degenerate interval `[3,3]` is the single point 3.
- **Half-open** `[s, e)`: `s` belongs, `e` does not. `[1,4)` and `[4,5)` share nothing, so they **do not overlap**, and `[3,3)` is empty. This is the convention of C++ iterator ranges, of byte ranges `[addr, addr + len)`, and of time slots in a calendar.

Every function in this document says which one it uses. The rules to carry:

| Question | Closed | Half-open |
|---|---|---|
| Do `a` and `b` overlap? | `a.s <= b.e && b.s <= a.e` | `a.s < b.e && b.s < a.e` |
| Length of `[s, e]` / `[s, e)` | `e - s` as a real length; `e - s + 1` if you count integer points | `e - s` |
| Tie order in the event sweep | `+1` (start) before `-1` (end) | `-1` before `+1` |
| Merging rule | `next.s <= last.e` | `next.s <= last.e` |

The last row is the surprise. **Merging is the same in both:** the union of `[1,4)` and `[4,5)` is the single interval `[1,5)` (nothing is missing between them), and the union of `[1,4]` and `[4,5]` is `[1,5]` as well. Only when a gap exists, `next.s > last.e`, do the pieces stay apart. So `merge_intervals` is correct for either reading, while the *overlap* tests below (rooms, removals, arrows) each fix one reading.

| Problem (LeetCode numbers from memory) | Reading | Touching ends |
|---|---|---|
| Merge Intervals (56), Insert Interval (57), Interval List Intersections (986) | closed | merge / intersect at a point |
| Minimum Number of Arrows (452) | closed | one arrow hits both |
| Meeting Rooms I and II (252, 253), My Calendar I (729) | half-open | no conflict |
| Non-overlapping Intervals (435) | half-open | allowed to touch |

How much the convention matters is not a corner case. On 200,000 random inputs of six intervals (lengths 1 to 5) on 22 cells, 152,561 (76.3%) contained a touching pair, and the most-overlapping count came out **different** under the two readings on 76,430 (38.2%). On 100,000 intervals of length 1 to 20 on 200 cells the busiest point is covered by 6,502 intervals read as closed and 5,941 read as half-open. An off-by-one in the convention is not a rare bug; it is the usual one.

## 2. Merge, insert, intersect

```cpp
// An interval is a pair (s, e) with s <= e. Each function says whether it treats intervals as CLOSED [s, e] (the end points belong to
// it, so [1,4] and [4,5] overlap) or HALF-OPEN [s, e) (so [1,4) and [4,5) do not). The convention is the first thing to settle.
typedef long long ll;
struct Iv { ll s, e; };
// Union of intervals: sort by start, then extend the last output interval while the next one starts at or before its end.
// Touching intervals merge in both conventions (the union of [1,4) and [4,5) is the single interval [1,5)); [1,2] and [3,4) stay apart.
std::vector<Iv> merge_intervals(std::vector<Iv> v) {
    std::sort(v.begin(), v.end(), [](const Iv& a, const Iv& b) { return a.s != b.s ? a.s < b.s : a.e < b.e; });
    std::vector<Iv> out;
    for (const Iv& x : v) { if (!out.empty() && x.s <= out.back().e) out.back().e = std::max(out.back().e, x.e); else out.push_back(x); }
    return out;
}
// Insert one interval into a sorted list of disjoint intervals, in O(n), without sorting: copy what ends before it, absorb what
// overlaps it (touching counts), copy the rest.
std::vector<Iv> insert_interval(const std::vector<Iv>& v, Iv nw) {
    std::vector<Iv> out; size_t i = 0, n = v.size();
    while (i < n && v[i].e < nw.s) out.push_back(v[i++]);
    while (i < n && v[i].s <= nw.e) { nw.s = std::min(nw.s, v[i].s); nw.e = std::max(nw.e, v[i].e); ++i; }
    out.push_back(nw);
    while (i < n) out.push_back(v[i++]);
    return out;
}
// Intersection of two lists, each sorted and disjoint (closed intervals): two pointers. Take the overlap of the pair under the
// pointers, then advance the one that ends first, because it cannot overlap anything further in the other list.
std::vector<Iv> intersect_lists(const std::vector<Iv>& a, const std::vector<Iv>& b) {
    std::vector<Iv> out; size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        ll lo = std::max(a[i].s, b[j].s), hi = std::min(a[i].e, b[j].e);
        if (lo <= hi) out.push_back({lo, hi});
        if (a[i].e < b[j].e) ++i; else ++j;
    }
    return out;
}
```

**Merge.** Sort by start. The last interval in the output is the union of everything seen so far in its group. The next interval `x` has a start at least as large as every earlier start, so either it starts at or before the group's end (it joins, and the end becomes the larger of the two, because `x` may lie inside the group) or it starts after the end, and then **every later interval starts even later**, so nothing can ever reach back into the group. That is the whole proof. The `max` matters: `[1,10], [2,3]` must stay `[1,10]`, not shrink to `[1,3]` (a pin in the tests).

Trace (checked by running the code):

```
input: [8,10] [1,3] [2,6] [15,18] [17,20]
merged: [1,6] [8,10] [15,20]
touching [1,4] and [4,5]: [1,5]
apart [1,2] and [3,4]: [1,2] [3,4]
```

The cost is the sort: at n = 1,000,000 random intervals of length 1 to 50 over 20,000,000 cells, which merge down to 272,971 groups, the whole call took 107.0 / 105.6 ms, of which sorting alone took 89.2 / 87.0 and the scan alone 11.3 / 11.0. The sort is 82 to 83 % of the call; the scan is the cheap part.

**Insert.** If the list is already sorted and disjoint, do not re-sort. Three phases, one pass: copy everything that ends **before** the new interval starts (`v[i].e < nw.s`); absorb everything that starts **at or before** the new interval's end by widening the new interval (touching counts, so `<=` on the start and `<` on the end); copy the rest. O(n), and the list stays sorted because the phases are contiguous.

```
list: [1,2] [3,5] [6,7] [8,10] [12,16]
insert [4,8]: [1,2] [3,10] [12,16]
insert [5,6]: [1,2] [3,7] [8,10] [12,16]
```

**Intersect two lists.** Both lists are sorted and disjoint. Look at the pair under the two pointers: their overlap, if any, is `[max of starts, min of ends]`, nonempty when `lo <= hi` (equal means they touch at a single point, which counts for closed intervals). Then advance **the interval that ends first**. Why that one: say `a[i]` ends before `b[j]`. Every later interval of `b` starts after `b[j]` ends (the list is disjoint), so after `a[i].e`, so `a[i]` cannot overlap any of them. It is finished, and `b[j]` is not (it may still meet `a[i+1]`). Each step retires one interval, so the loop does at most `|a| + |b|` steps.

```
A: [0,2] [5,10] [13,23] [24,25]
B: [1,5] [8,12] [15,24] [25,26]
A and B: [1,2] [5,5] [8,10] [15,23] [24,24] [25,25]
```

The point results `[5,5]`, `[24,24]` and `[25,25]` are real: the closed intervals touch there. If equal ends are advanced one way or the other the output is the same (a mutant that advances the other pointer on a tie survives, and section 8 says why).

## 3. Meeting rooms

Half-open meetings `[s, e)` with `s < e`. **Rooms I**: can one person attend all? Sort by start and check that each meeting starts at or after the previous one ends. **Rooms II**: the fewest rooms. This is the same as the **largest number of meetings in progress at one moment**: at that moment each needs its own room, so you need at least that many, and the schemes below show that many suffice.

```cpp
// Meeting rooms. Half-open intervals [s, e) with s < e: a meeting that ends at 10 and one that starts at 10 can share a room.
// Rooms I: can one person attend all? Sort by start; each must start at or after the previous end.
bool can_attend_all(std::vector<Iv> v) {
    std::sort(v.begin(), v.end(), [](const Iv& a, const Iv& b) { return a.s < b.s; });
    for (size_t i = 1; i < v.size(); ++i) if (v[i].s < v[i - 1].e) return false;
    return true;
}
// Rooms II, with a heap: process meetings by start; the heap holds the end times of the rooms in use. If the earliest end is at or
// before this start, that room is reused (pop); then occupy a room (push). The largest heap size is the answer. O(n log n).
int min_rooms_heap(std::vector<Iv> v) {
    std::sort(v.begin(), v.end(), [](const Iv& a, const Iv& b) { return a.s < b.s; });
    std::priority_queue<ll, std::vector<ll>, std::greater<ll>> ends; size_t best = 0;
    for (const Iv& x : v) { if (!ends.empty() && ends.top() <= x.s) ends.pop(); ends.push(x.e); best = std::max(best, ends.size()); }
    return (int)best;
}
// Rooms II, without a heap: the rooms in use at a time are (starts so far) - (ends so far). Sort the starts and the ends separately
// and walk both; at a tie the end goes first (half-open). The pairing of a start with its own end does not matter, only the counts.
int min_rooms(const std::vector<Iv>& v) {
    int n = (int)v.size(); std::vector<ll> s(n), e(n);
    for (int i = 0; i < n; ++i) { s[i] = v[i].s; e[i] = v[i].e; }
    std::sort(s.begin(), s.end()); std::sort(e.begin(), e.end());
    int cur = 0, best = 0;
    for (int i = 0, j = 0; i < n;) { if (s[i] < e[j]) { ++cur; ++i; best = std::max(best, cur); } else { --cur; ++j; } }
    return best;
}
```

**The heap.** Process meetings in order of start. The heap holds the end times of the rooms now in use. If the earliest end is at or before this start (`<=`, half-open), that room is free: pop it. Then the meeting takes a room: push its end. The heap size is the number of rooms in use. Note a small fact the tests found: the size **never decreases** (a pop is always followed by a push), so the largest size equals the final size, and `best = max(best, size)` could be `best = size`. That mutation survives all tests for exactly this reason.

**The two sorted arrays.** The rooms in use at time `t` are the meetings that started minus the meetings that ended. So sort the starts and the ends **separately** and walk both; at a tie the end goes first (half-open). The pairing of a start with its own end is lost and does not matter: only the counts do. No heap, no structure, two sorts.

```
meetings [0,30) [5,10) [15,20): attend all=0 rooms=2 heap=2
meetings [0,10) [10,20): attend all=1 rooms=1 heap=1
meetings [1,5) [2,6) [3,7) [5,8) [6,9): rooms=3 heap=3
```

Three ways to get the same answer. On 1,000,000 random meetings over 2,000,000 cells (length 1 to 50) all of them gave 33 rooms; the times in ms:

| Method | Time (run 1 / run 2) |
|---|---|
| Heap (`min_rooms_heap`) | 105.2 / 131.9 |
| Two sorted arrays (`min_rooms`) | 158.3 / 160.3 |
| Event sweep (section 5, `max_overlap`) | 212.4 / 207.8 |
| Difference array (section 5, `max_overlap_diff`) | 13.0 / 12.7 |

The heap beat the two-array version here (probably because two sorts of n numbers cost more than one sort of n intervals plus n heap operations on a heap that never holds more than 33 entries), and the event sweep was slowest (one sort of 2n pairs). The difference array, which does no sorting at all, was 8 to 10 times faster than the heap. That last number is the lesson of section 5: when coordinates are small integers, sorting is optional.

## 4. Fewest removals, fewest arrows

```cpp
// Fewest intervals to remove so that the rest do not overlap (touching allowed, half-open): n minus the most that can be kept,
// and the most that can be kept is activity selection (Greedy 01): sort by end, keep if it starts at or after the last kept end.
int min_removals(std::vector<Iv> v) {
    std::sort(v.begin(), v.end(), [](const Iv& a, const Iv& b) { return a.e < b.e; });
    int kept = 0; ll last = LLONG_MIN;
    for (const Iv& x : v) if (x.s >= last) { ++kept; last = x.e; }
    return (int)v.size() - kept;
}
// Fewest points that hit every interval (closed): the same sort by end. Shoot at the end of the first unhit interval: it is the
// rightmost point that still hits it, so it hits as many later intervals as any point can. Touching intervals share the point.
int min_arrows(std::vector<Iv> v) {
    std::sort(v.begin(), v.end(), [](const Iv& a, const Iv& b) { return a.e < b.e; });
    int count = 0; ll shot = LLONG_MIN;
    for (const Iv& x : v) if (count == 0 || x.s > shot) { ++count; shot = x.e; }
    return count;
}
```

**Non-overlapping Intervals.** Remove the fewest intervals so that the rest do not overlap (touching allowed, half-open). That is `n` minus the **largest** non-overlapping subset, which is exactly the activity selection of [Greedy 01](01_greedy_algorithms_and_proofs.md): sort by **end**, keep an interval if it starts at or after the last kept end. The exchange argument carries over unchanged: among the intervals that can come first, the one that ends earliest leaves the most room. Sorting by *start* is the classic wrong turn (it keeps a long interval that blocks many short ones).

**Minimum arrows.** Closed intervals; one arrow at a point bursts every interval containing it; find the fewest arrows. Sort by end and shoot at the end of the first interval not yet hit. Why the *end*: any arrow that hits this interval sits at some point `p <= e`. Slide it right to `e`. It still hits this interval, and it now hits every interval that the old position hit and that is still unhit, because those all have end `>= e` (they come later in the sorted order) and start `<= p <= e`. So moving right never loses a hit, and the greedy arrow is at least as good as any. Touching intervals share the point, so the test to start a new arrow is a strict `x.s > shot`.

The two are one idea read two ways. Fewest arrows equals the largest set of pairwise **disjoint** closed intervals (a standard duality for intervals, stated here from memory), and both are found by "sort by end, take if compatible". The only difference is the touching rule: removals allow touching (`>=`), arrows do not (`>`).

```
min removals for [1,2) [2,3) [3,4) [1,3): 1
min arrows for [10,16] [2,8] [1,6] [7,12]: 2
min arrows for [1,2] [2,3] [3,4] [4,5]: 2
```

The last line shows the convention at work: on `[1,2] [2,3] [3,4] [4,5]` two arrows suffice (at 2 and 4) because neighbours touch, while the same four intervals read half-open need no removal at all (`min_removals` returns 0).

## 5. The sweep line and the difference array

To answer "how many intervals cover a point?" for all points at once, turn every interval into two **events**: `(s, +1)` and `(e, -1)`. Sort the events and walk them with a running count; the count after each event is the number covering the stretch up to the next event. The maximum of the count is the busiest point. Ties are the whole subtlety: at a shared coordinate, a closed interval ending there and one starting there both cover that point, so the `+1` must be processed first; for half-open, the `-1` first.

```cpp
// The sweep line: turn each interval into two events, (s, +1) and (e, -1), sort, and walk once keeping a running count.
// closed = true:  at the same coordinate the +1 comes first, so [1,4] and [4,5] overlap at 4.
// closed = false: the -1 comes first (half-open), so [1,4) and [4,5) never overlap.
// Returns the largest number of intervals that cover one point.
int max_overlap(const std::vector<Iv>& v, bool closed) {
    std::vector<std::pair<ll, int>> ev; ev.reserve(2 * v.size());
    for (const Iv& x : v) { ev.push_back({x.s, +1}); ev.push_back({x.e, -1}); }
    std::sort(ev.begin(), ev.end(), [closed](const std::pair<ll, int>& a, const std::pair<ll, int>& b) { return a.first != b.first ? a.first < b.first : (closed ? a.second > b.second : a.second < b.second); });
    int cur = 0, best = 0; for (auto& p : ev) { cur += p.second; best = std::max(best, cur); }
    return best;
}
// When coordinates are small integers in [0, M) there is no need to sort: a DIFFERENCE ARRAY adds +1 at s and -1 at e (half-open
// cells s..e-1), and one prefix sum gives the count at every cell. O(n + M).
int max_overlap_diff(const std::vector<Iv>& v, int M) {
    std::vector<int> d(M + 1, 0); for (const Iv& x : v) { ++d[x.s]; --d[x.e]; }
    int cur = 0, best = 0; for (int i = 0; i < M; ++i) { cur += d[i]; best = std::max(best, cur); }
    return best;
}
// Total length covered by the union (half-open) and the gaps between covered stretches ("free time").
ll union_length(const std::vector<Iv>& v) { ll t = 0; for (const Iv& x : merge_intervals(v)) t += x.e - x.s; return t; }
std::vector<Iv> free_gaps(const std::vector<Iv>& v) {
    std::vector<Iv> m = merge_intervals(v), out; for (size_t i = 1; i < m.size(); ++i) out.push_back({m[i - 1].e, m[i].s});
    return out;
}
```

```
[1,4] [4,5]: closed max overlap=2 half-open max overlap=1
five intervals: closed=4 half-open=3 diff=3
```

When coordinates are **small integers** (cells, ports, days, byte offsets), skip the sort. A **difference array** `d` adds `+1` at `d[s]` and `-1` at `d[e]`; the prefix sum `d[0] + ... + d[i]` is the count at cell `i`. The sum telescopes, which is why the two end points are enough: every cell from `s` to `e - 1` sees exactly one `+1` more than `-1`. It costs O(n + M) for M cells, with no log factor, and a second prefix sum answers "how many cells are covered" or "the first uncovered cell". For closed integer intervals use `d[e + 1] -= 1`. Section 3's table shows what it buys: 1,000,000 intervals in 13.0 / 12.7 ms against 105.2 / 131.9 ms for the heap and 212.4 / 207.8 ms for the event sweep. It needs the coordinate range to fit in memory; otherwise **compress the coordinates** (sort the distinct end points and replace each by its rank) and use the array on the ranks.

The same merge gives two more answers. **Union length** is the sum of the merged lengths (half-open: `e - s`). **Free gaps** are the spaces between consecutive merged intervals: `[m[i-1].e, m[i].s)`. This is the "find a free slot" and "merge adjacent memory regions" operation; an allocator merging free blocks and a kernel merging adjacent mappings are the same algorithm on address ranges (the kernel use is from memory).

```
union length=8
free gaps: [5,9) [12,14)
```

## 6. Online booking: the ordered map

My Calendar I: bookings arrive one at a time; accept `[s, e)` only if it overlaps nothing accepted before. Scanning all previous bookings is O(n) per request. Keep the accepted intervals in an ordered map `start -> end`. Because the accepted intervals are disjoint, they are also ordered by end, and the new interval can conflict with only **two** of them: the first booking that starts at or after `s` (it conflicts if it starts before `e`) and the last booking that starts before `s` (it conflicts if it ends after `s`). Everything further out is on the far side of one of those two. `lower_bound` finds the first in O(log n).

```cpp
// Online booking: accept [s, e) only if it overlaps nothing booked so far. Keep the booked intervals in an ordered map start -> end.
// A new interval can only conflict with its two neighbours: the first booking that starts at or after s (it conflicts if it starts
// before e) and the last booking that starts before s (it conflicts if it ends after s). O(log n) per booking.
struct Calendar {
    std::map<ll, ll> m;
    bool book(ll s, ll e) {
        auto it = m.lower_bound(s);
        if (it != m.end() && it->first < e) return false;
        if (it != m.begin()) { --it; if (it->second > s) return false; }
        m[s] = e; return true;
    }
};
```

```
book [10,20) -> accepted
book [15,25) -> rejected
book [20,30) -> accepted
book [5,10) -> accepted
book [5,11) -> rejected
book [0,100) -> rejected
book [30,40) -> accepted
```

With 30,000 random requests (start up to 100,000,000, length 1 to 3,000), 20,603 were accepted by both the map and the plain scan, and the times were 6.1 / 8.7 ms for the map against 871.9 / 832.2 ms for the scan, a factor of 96 to 143. The map code assumes `s < e`; with an empty interval (`s == e`) the half-open tests are not meaningful.

## 7. Recognising and choosing

| Signal in the problem | Tool | Key / convention |
|---|---|---|
| Combine overlapping ranges, free time, union length | Sort by start, merge | Merge rule `next.s <= last.e` for both readings |
| Add one range to a sorted disjoint list | Three-phase insert | No sort |
| Common time of two schedules | Two pointers, advance the earlier end | Closed: touching gives a point |
| Most things at once, fewest rooms or machines | Event sweep, heap, or difference array | Half-open: end before start on a tie |
| Fewest to remove so the rest are disjoint | Sort by end, activity selection | Half-open, touching allowed |
| Fewest points that hit every range | Sort by end, shoot at the end | Closed, touching shares the point |
| Small integer coordinates, many ranges | Difference array plus prefix sum | `d[e]` half-open, `d[e + 1]` closed |
| Accept bookings online | Ordered map, check two neighbours | Half-open |
| Weighted choice among overlapping ranges | Not greedy | Dynamic programming (next area) |

## 8. What the tests check, and what they cannot

Of the 49 mutants, 43 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Six survive, and each leaves every output the tests can see unchanged. The first run of this mutant set left eight survivors; pins for an interval that ends exactly at the array size and for a busiest cell at the very end of the difference array killed two of them (one was an out-of-bounds write that only the sanitizer could see). The reasons the other six stay:

- **Sorting the merge by start only, without the end as a tie-break.** Intervals with equal starts end up in either order, and the `max` makes the merged end the same.
- **Advancing the first pointer instead of the second when the two intervals end at the same coordinate (`<=` for `<`).** The interval that stays cannot overlap the next one of the other list, so the output is identical.
- **`best = ends.size()` instead of the maximum in the heap method.** The heap never shrinks (every pop is followed by a push), so the last size is the largest.
- **Taking the maximum of the shot and the new end in the arrows loop.** The intervals are sorted by end, so the new end is never smaller than the shot.
- **`upper_bound` instead of `lower_bound` in the calendar.** A booking that starts at exactly `s` is then found as the *previous* entry instead of the next, and the second test (it ends after `s`) rejects it all the same, for any nonempty interval.
- **Writing `max(e, m[s])` when inserting into the map.** An accepted booking never has a start that is already a key, so the second argument is never read.

The 1,000,000-interval run has no brute-force reference; it is checked against a single difference array, an independent algorithm that shares no code with the sorted methods. What the tests cannot do is prove the two greedy rules (end-sorted removal, end-shot arrows): they show agreement with exhaustive search on small instances, which supports the exchange arguments in section 4 and does not replace them.

Mutants are chosen where a bug is plausible, not at random, so "43 of 49" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Picking the convention last.** Decide closed or half-open before writing a `<`.
2. **Merging with `<` instead of `<=`,** which leaves `[1,4)` and `[4,5)` apart.
3. **Forgetting the `max` when extending** the last merged interval, so an inner interval shrinks it.
4. **Sorting by start for removals or arrows.** The end is the key; sorting by start needs an extra "keep the smaller end" rule.
5. **The wrong tie order in an event sweep.** Closed puts starts first; half-open puts ends first. Swapping them is silent.
6. **Starting the arrow count from `shot = LLONG_MIN` without a first-interval guard,** which breaks when an interval starts at the smallest value.
7. **A heap of ends where a room is freed with `<` instead of `<=`,** which makes back-to-back meetings need two rooms.
8. **Sizing the difference array to `M` instead of `M + 1`** when `e` can equal `M`: an out-of-bounds write.
9. **Advancing both pointers in the intersection,** or the one that ends later.
10. **Overflow in `e - s` or `s + len`** with 64-bit timestamps near the limits; use the type that fits and test the extremes.
11. **Checking only one neighbour in the calendar.** The next booking and the previous booking can each conflict.
12. **Assuming sorted input.** Insert and intersect need sorted disjoint lists; merge sorts for itself.

## 10. Questions to answer out loud

1. Closed versus half-open: which tests change, and which one does not?
2. Why does the merge rule `next.s <= last.e` work for both, and why does every later interval start after the group once a gap appears?
3. Why is insert O(n) without sorting, and what are the three phases?
4. In the intersection, why is advancing the pointer that ends first safe?
5. Prove that the fewest rooms equal the largest number of meetings in progress at once.
6. How does the two-sorted-arrays method count rooms without pairing starts with ends?
7. Why is the heap size in Rooms II never decreasing, and what does that say about the answer?
8. Why is sorting by end the right key for removals and for arrows, and why is sorting by start wrong?
9. State the exchange argument for the arrow placed at the end of the first interval.
10. What is the tie order in an event sweep for each convention, and why?
11. When does a difference array replace the sort, and what do you do when the coordinates are huge?
12. Why does the calendar only check two neighbours?

## 11. Problems

### 11.1 On paper

**A.** Merge the closed intervals `[1,3], [2,6], [8,10], [15,18]`. Then merge `[1,4]` and `[4,5]`: do the closed and half-open readings agree?

**B.** Insert `[2,5]` into `[1,3], [6,9]`. Then insert `[10,11]` into the same list.

**C.** For the meetings `[0,30), [5,10), [15,20)`, can one person attend all, and how many rooms are needed? Repeat for `[7,10), [2,4)`.

**D.** Fewest removals so that the half-open intervals are non-overlapping: `[1,2), [2,3), [3,4), [1,3)`; then three copies of `[1,2)`; then `[1,2), [2,3)`.

**E.** Fewest arrows for the closed intervals `[10,16], [2,8], [1,6], [7,12]`; for `[1,2], [3,4], [5,6], [7,8]`; for `[1,2], [2,3], [3,4], [4,5]`.

**F.** Book `[10,20)`, `[15,25)`, `[20,30)` in that order on an empty calendar. Which are accepted?

**G.** Half-open: the union length of `[1,10), [2,3), [4,5)` and the number of free gaps; then the free gap of `[1,3), [5,7)`.

### 11.2 On LeetCode

For each: which convention, which sort key, and what is the tie rule? Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Merge Intervals | 56 | Sort by start; the `max` on the end |
| Insert Interval | 57 | Three phases, no sort |
| Interval List Intersections | 986 | Two pointers, advance the earlier end |
| Non-overlapping Intervals | 435 | Sort by end (section 4) |
| Minimum Number of Arrows to Burst Balloons | 452 | Sort by end, strict `>` |
| Meeting Rooms (premium) | 252 | Sort and compare neighbours |
| Meeting Rooms II (premium) | 253 | Heap of ends, or two sorted arrays |
| My Calendar I | 729 | Ordered map and two neighbours |

### 11.3 In your editor

**A.** Merge the free regions of a simulated allocator: a vector of `(address, length)` blocks, freed in random order, merged by address; check against a bitmap of bytes.

**B.** Write My Calendar II (at most one overlap) with a difference map: an ordered map of `+1` and `-1` at each end point, rebuilt by a prefix scan for each request; compare with a brute force.

**C.** Write the sweep over events with a payload: for jobs with a CPU demand, report the first time the total demand exceeds a capacity; test against a per-cell simulation.

**D.** Coordinate compression: take 100,000 intervals with end points up to 10^18, compress to ranks, run the difference array, and check against the sorted sweep.

**E.** Time the heap, two-array and difference-array methods on your machine at n = 10^7 and compare with the table in section 3.

**F.** Write the arrows solution that sorts by **start** instead (on overlap, shrink the current shot to the smaller end) and test it against `min_arrows` on random closed intervals.

### Answers

**A.** Sorted, then merged: `[1,6]`, `[8,10]`, `[15,18]` (the code returns the same). `[1,4]` and `[4,5]` merge into `[1,5]` in both readings: the next start equals the group's end, so there is no gap.

**B.** `[2,5]` absorbs `[1,3]` (the new interval starts at 2, before the end of `[1,3]`) and stays before `[6,9]`: the result is `[1,5], [6,9]`. `[10,11]` starts after everything, so it is appended: `[1,3], [6,9], [10,11]`.

**C.** `[0,30)` contains the others, so one person cannot attend all (the second starts at 5, before 30), and two rooms are needed: `[5,10)` and `[15,20)` can share the second room. For `[7,10), [2,4)` one person can attend, and one room suffices.

**D.** For `[1,2), [2,3), [3,4), [1,3)` the best kept set is `[1,2), [2,3), [3,4)`, so remove 1. Three copies of `[1,2)` keep one, so remove 2. `[1,2), [2,3)` touch, so remove 0.

**E.** Sorted by end: `[1,6], [2,8], [7,12], [10,16]`. Shoot at 6 (hits the first two), then at 12 (hits the last two): 2 arrows. Four separated intervals need 4. For `[1,2], [2,3], [3,4], [4,5]`: shoot at 2 (hits the first two), at 4 (hits the last two): 2 arrows.

**F.** `[10,20)` accepted. `[15,25)` overlaps it: rejected. `[20,30)` starts exactly when `[10,20)` ends: accepted. So accept, reject, accept.

**G.** The union of `[1,10)` with the two inside it is `[1,10)`, length 9, and there are no gaps (the merged list has one interval). For `[1,3), [5,7)` the free gap is `[3,5)`.

## Cheat sheet

- **Decide closed or half-open first.** Closed: touching overlaps, start before end on ties. Half-open: touching does not overlap, end before start on ties.
- **Merge:** sort by start; extend while `next.s <= last.e`, with `max` on the end. Same rule for both readings.
- **Insert:** copy what ends before, absorb what starts at or before the end, copy the rest. **Intersect:** `[max of starts, min of ends]`, advance the earlier end.
- **Rooms:** heap of ends (pop when `top <= start`), or sort starts and ends separately and walk both; the answer is the most meetings in progress at once.
- **Removals and arrows:** sort by end. Removals keep if `s >= last`; arrows shoot a new one if `s > shot`.
- **Sweep:** events `(s, +1)`, `(e, -1)`; sort; running count. **Difference array** when coordinates are small integers; compress the coordinates when they are not.
- **Calendar:** ordered map; check the first start at or after `s` and the last start before it.
- **Convention matters:** in 38.2 % of small random inputs the busiest count differed between the two readings.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Merging intervals, the sweep line, the difference array, the exchange argument for sorting by end, the duality between the fewest points and the most disjoint intervals, and the use of an ordered map for interval booking are standard; the statements here are my own wording, not quotations. The kernel's merging of adjacent mappings is from memory.
- Problem numbers and titles in section 11, and the samples of problems 56, 57, 986, 435, 452, 253 and 729, are LeetCode's, from memory; check a number before you rely on it. The samples were run through the listings and give the stated results.
