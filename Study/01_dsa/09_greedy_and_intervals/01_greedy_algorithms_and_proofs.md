# DSA 09.01: Greedy Algorithms and Their Proofs

*Part of the [Greedy and intervals plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Sorting patterns and comparators](../03_binary_search_and_sorting/05_sorting_patterns_and_comparators.md) (sorting by a key, strict weak orders) and [Binary heaps](../07_trees/15_binary_heaps.md) (a priority queue). There is no companion demo file: every structure is listed in this document.*

A **greedy algorithm** builds an answer one step at a time and, at every step, takes the choice that looks best right now and never reconsiders. Almost every such rule is wrong. A few are exactly right, and the whole craft is telling them apart: a greedy that is correct comes with a short argument that explains **why** the local choice cannot hurt, and a greedy without that argument is a guess. This tutorial teaches the two arguments that cover nearly everything (the **exchange argument** and **greedy stays ahead**), applies them to the classic problems (activity selection, fractional knapsack, Huffman coding, the jump games, the gas station), and then shows by measurement what a wrong greedy looks like: how often it fails, by how much, and how to find a counterexample by machine instead of by inspiration.

By the end you can:

- state the two ingredients every correct greedy has (a **greedy choice** that is safe, and **optimal substructure** after taking it) and prove the first by an exchange argument or by "stays ahead";
- write activity selection and say which of four natural rules is correct (one) and why the other three fail, with counts;
- write fractional knapsack and explain why the same rule is wrong for the 0/1 version;
- build a Huffman code, prove the greedy merge optimal, derive canonical codes from the lengths, and state the entropy bound;
- write the jump games and the gas station as one-pass scans and prove the invariant each one keeps;
- test any greedy against brute force on small random inputs and shrink a failure to a counterexample, which is how every "wrong rule" in this document was found.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 110,440 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: activity selection against a brute force over all subsets of intervals (with empty intervals and duplicates); the four rules checked never to beat the optimum, and to fail where claimed; fractional knapsack against a brute force over the extreme points of the problem; the 0/1 dynamic program against a brute force over all subsets; Huffman against an exhaustive search over every merge order, its code lengths against Kraft's equality, its canonical codes for the prefix property and by encoding and decoding random messages, its average length against the entropy bounds, and its cost against an independent two-queue implementation on 200,000 weights; the jump games against a table-filling dynamic program; the gas station against trying every start; the coin greedy against an exact dynamic program; and a 100,000-interval instance against an independent dynamic program. Then 47 hand-made mutants of the listings were run against the tests: **39 were caught and 8 are equivalent changes** (section 8).

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. What makes a greedy correct

Two properties must hold.

1. **Greedy choice property.** Some optimal solution contains the greedy first choice.
2. **Optimal substructure.** After making that choice, what remains is a smaller instance of the same problem, and an optimal solution of it, together with the choice, is optimal for the whole.

Together they give an induction: the greedy choice is safe, the rest is a smaller problem, so repeat. The work is in proving property 1, and there are two standard ways.

**Exchange argument.** Take any optimal solution `O`. If it already contains the greedy choice `g`, done. Otherwise change `O` into `O'` by swapping something of `O` for `g`, and show that `O'` is still valid and no worse. Then an optimal solution containing `g` exists. The picture: you do not claim the greedy answer is the only good one, only that you can always *bend* an optimal answer until it agrees with greedy, at no cost.

**Greedy stays ahead.** Line up the greedy solution and any other solution step by step, and show by induction that after every step greedy is at least as far along by some measure (finished earlier, reached farther, spent less). At the end, "ahead at every step" means "at least as many steps".

Both are used below. A correct greedy usually has a one-paragraph proof of one of these kinds. If you cannot find it in a few minutes, suspect the rule, and test it (section 6) before you trust it. The shortest-path and spanning-tree algorithms of earlier topics are greedy in exactly this sense: [Dijkstra](../08_graphs/05_shortest_paths_dijkstra.md) takes the nearest unfinished vertex, and Kruskal and Prim of [minimum spanning trees](../08_graphs/07_minimum_spanning_trees.md) take the lightest safe edge, justified by the cut property, which is an exchange argument.

## 2. Activity selection

Given activities as half-open intervals `[s, e)`, choose the largest set in which no two overlap (one may start exactly when another ends). Four rules come to mind: take the activity that **starts earliest**, the **shortest**, the one with the **fewest conflicts**, or the one that **finishes earliest**.

```cpp
// Activity selection. An activity is the half-open interval [s, e). Two activities are compatible when one ends no later than the
// other starts. Goal: the largest set of pairwise compatible activities.
// GREEDY CHOICE: always take the compatible activity that FINISHES first. Sort by end (ties by start) and keep an activity if it
// starts at or after the end of the last one kept. Returns the chosen indices in time order. O(n log n).
struct Iv { long long s, e; };
std::vector<int> select_activities(const std::vector<Iv>& a) {
    std::vector<int> idx(a.size()); for (size_t i = 0; i < a.size(); ++i) idx[i] = (int)i;
    std::sort(idx.begin(), idx.end(), [&](int x, int y) { return a[x].e != a[y].e ? a[x].e < a[y].e : a[x].s < a[y].s; });
    std::vector<int> out; long long last = LLONG_MIN;
    for (int i : idx) if (a[i].s >= last) { out.push_back(i); last = a[i].e; }
    return out;
}
// The same loop with a different greedy rule, to see which rules are wrong (section 3). Repeatedly pick the best remaining
// activity by the rule, then discard everything that overlaps it. O(n^2).
enum Rule { EARLIEST_START, SHORTEST, FEWEST_CONFLICTS, EARLIEST_FINISH };
static bool overlaps(const Iv& x, const Iv& y) { return !(x.e <= y.s || y.e <= x.s); }
int greedy_by_rule(const std::vector<Iv>& a, Rule rule) {
    int n = (int)a.size(), taken = 0; std::vector<char> alive(n, 1);
    for (;;) {
        int best = -1; long long bk = 0;
        for (int i = 0; i < n; ++i) if (alive[i]) {
            long long k = 0;
            if (rule == EARLIEST_START) k = a[i].s; else if (rule == SHORTEST) k = a[i].e - a[i].s; else if (rule == EARLIEST_FINISH) k = a[i].e;
            else for (int j = 0; j < n; ++j) if (j != i && alive[j] && overlaps(a[i], a[j])) ++k;
            if (best < 0 || k < bk) { best = i; bk = k; }
        }
        if (best < 0) return taken;
        ++taken; for (int j = 0; j < n; ++j) if (alive[j] && (j == best || overlaps(a[best], a[j]))) alive[j] = 0;
    }
}
```

**Only one is correct: earliest finish.** *Exchange argument.* Let `g` be the activity with the earliest finish time, and `O` an optimal set whose first activity (by finish time) is `o`. Then `finish(g) <= finish(o)`. Every other activity of `O` starts at or after `finish(o)`, hence at or after `finish(g)`, so replacing `o` by `g` keeps the set compatible and the same size. So some optimal set contains `g`. What remains is the same problem on the activities starting at or after `finish(g)`, so repeat. *Stays ahead.* If greedy picks `g1, g2, ...` and an optimal set is `o1, o2, ...` in time order, then `finish(g_i) <= finish(o_i)` for every `i` by induction (`o_(i+1)` starts after `finish(o_i) >= finish(g_i)`, so `g_(i+1)` has a candidate at least as early); hence greedy cannot run out before the optimum does.

The other three fail, and the failures show up on small instances:

- *Earliest start* takes `[0,10)` first and blocks three small activities inside it.
- *Shortest* takes `[4,6)` from `[0,5), [4,6), [5,10)`, which overlaps both of the others, and gets 1 where 2 fit.
- *Fewest conflicts* sounds careful and is also wrong, but it fails rarely, so you will not find a counterexample by hand (the last line of the trace is one found by machine).

Trace (checked by running the code), the textbook instance of eleven activities:

```
chosen: [1,4) [5,7) [8,11) [12,16)
count=4
by rule: earliest start=3 shortest=4 fewest conflicts=4 earliest finish=4
earliest start picks 1 of the 3 that fit in [0,10),[1,2),[3,4),[5,6)
shortest picks 1, optimum 2 on [0,5),[4,6),[5,10)
fewest conflicts picks 4, optimum 5, on: [16,20) [20,23) [8,13) [7,12) [6,8) [13,17) [19,25) [17,22) [20,26) [9,11) [11,15) [8,13)
```

How often each wrong rule loses is a matter of counting. On 200,000 random instances of 12 activities (starts 0 to 20, lengths 1 to 6), with the optimum from the correct rule:

| Rule | Instances where it is worse than optimal | Share |
|---|---|---|
| Earliest start | 97,877 | 48.938% |
| Shortest | 11,434 | 5.717% |
| Fewest conflicts | 90 | 0.045% |
| Earliest finish | 0 | 0.000% |

Earliest start loses on nearly half of the instances, shortest on one in eighteen, fewest conflicts on one in two thousand, and earliest finish never (as it must not). The last is the lesson of the whole topic: **a rule that fails once in two thousand random instances passes every spot check you will do by hand.**

**Cost.** One sort and one scan: O(n log n). On 1,000,000 random activities (684,145 chosen) the whole function took 148 / 141 ms (run 1 / run 2), of which the sort alone was 146 / 126 ms and the scan after sorting only 8.9 / 7.5 ms, about 6% of the sort. The sort is the cost; the greedy itself is free.

## 3. Knapsack: fractional versus 0/1

Items have weights and values; a bag holds total weight `W`. In the **fractional** version you may take part of an item. The greedy rule: take items in decreasing **value per weight**, the last one partly.

```cpp
// Fractional knapsack: items may be split. GREEDY CHOICE: take items in decreasing value per weight, the last one partly.
// Ratios are compared by cross-multiplication (in 128 bits), never by division, so ties and big numbers are exact.
// An item of weight 0 has an infinite ratio and is always taken. Returns the best total value.
struct Item { long long w, v; };
// Order by value per weight, best first; weight-0 items come first. (A comparator that treats a weight-0 item as equal to everything
// is not a strict weak order, and std::sort on it is undefined behaviour.)
static bool by_ratio(const Item& x, const Item& y) {
    if (x.w == 0 || y.w == 0) return x.w == 0 && y.w != 0;
    return (__int128)x.v * y.w > (__int128)y.v * x.w;
}
long double fractional_knapsack(std::vector<Item> it, long long cap) {
    std::sort(it.begin(), it.end(), by_ratio);
    long double total = 0; long long room = cap;
    for (auto& x : it) {
        if (x.w == 0) { total += x.v; continue; }
        if (x.w <= room) { total += x.v; room -= x.w; }
        else { total += (long double)x.v * room / x.w; break; }
    }
    return total;
}
// The 0/1 version (items are taken whole or not at all). The same ratio rule is only a heuristic here; the dynamic program is exact.
long long knapsack01_greedy(std::vector<Item> it, long long cap) {
    std::sort(it.begin(), it.end(), by_ratio);
    long long total = 0, room = cap; for (auto& x : it) if (x.w <= room) { total += x.v; room -= x.w; }
    return total;
}
long long knapsack01_dp(const std::vector<Item>& it, int cap) {
    std::vector<long long> best(cap + 1, 0);
    for (auto& x : it) for (int c = cap; c >= x.w; --c) best[c] = std::max(best[c], best[c - (int)x.w] + x.v);
    return best[cap];
}
```

*Exchange argument.* Suppose an optimal solution takes less than all of an item `i` yet takes some weight `eps` of an item `j` with a lower ratio `v_j / w_j < v_i / w_i`. Move that `eps` of capacity from `j` to `i`: the value changes by `eps * (v_i/w_i - v_j/w_j) > 0`. So no optimal solution can skip a better-ratio item while using a worse one, which is exactly what greedy does. The ratios are compared by cross-multiplying in 128 bits, never by dividing, so that ties are exact and there is no rounding; a comparator that is not a strict weak order (for example one that treats an item of weight 0 as equal to everything) is undefined behaviour in `std::sort`, so the listing puts weight-0 items first explicitly.

**The same rule is wrong for 0/1 knapsack** (each item whole or not at all), because the exchange needs to move a *fraction* of an item, which is no longer allowed. The standard example: capacity 50, items `(weight, value)` = `(10,60)`, `(20,100)`, `(30,120)`. By ratio the greedy takes the first two (value 160, and 20 of the capacity cannot fit the third); the best is the last two, 220. The fractional answer, 240, is an **upper bound** for the 0/1 problem:

```
W=50 items (w,v): (10,60) (20,100) (30,120)
ratio greedy 0/1=160 exact 0/1=220 fractional=240.0
```

How wrong: on 100,000 random instances of 8 items (weights 1 to 10, values 1 to 20, capacity half the total weight), ratio-greedy was suboptimal on 28,182 (28.2%). When wrong it still reached on average 94.5% of the optimum, and the fractional bound was on average 104.5% of the exact answer. So the failure is frequent and mild, which is the dangerous combination: the output looks plausible. The 0/1 problem needs the dynamic program shown (a topic of the next area).

## 4. Huffman coding

Given symbol frequencies, choose a **prefix-free** binary code (no codeword is the start of another, so a bit stream decodes without separators) that minimises the total number of bits `sum(weight * length)`. The greedy rule: repeatedly merge the **two lightest** subtrees into one whose weight is their sum; the depths of the leaves are the code lengths.

```cpp
// Huffman coding. Given symbol weights (frequencies), build a prefix-free binary code that minimises sum(weight * code length).
// GREEDY CHOICE: repeatedly merge the TWO LIGHTEST subtrees into one whose weight is their sum. len[i] is the depth of leaf i.
// (A single symbol gets length 0 here; real formats give it one bit.) The heap orders by (weight, id), so the result is repeatable.
// Internal nodes are numbered in creation order, so depths are filled in by one reverse pass: O(n log n) overall.
struct Huff { std::vector<int> len; long long cost = 0; };
Huff huffman(const std::vector<long long>& w) {
    int n = (int)w.size(); Huff h; h.len.assign(n, 0); if (n <= 1) return h;
    typedef std::pair<long long, int> P; std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    std::vector<int> lc(n - 1), rc(n - 1); for (int i = 0; i < n; ++i) pq.push({w[i], i});
    for (int k = 0; k < n - 1; ++k) {
        P a = pq.top(); pq.pop(); P b = pq.top(); pq.pop(); lc[k] = a.second; rc[k] = b.second; pq.push({a.first + b.first, n + k});
    }
    std::vector<int> depth(2 * n - 1, 0);
    for (int k = n - 2; k >= 0; --k) { depth[lc[k]] = depth[rc[k]] = depth[n + k] + 1; }
    for (int i = 0; i < n; ++i) { h.len[i] = depth[i]; h.cost += w[i] * depth[i]; }
    return h;
}
// Canonical codes: given only the lengths, sort symbols by (length, index) and count upward, shifting left when the length grows.
// The codes are prefix-free exactly because the lengths satisfy Kraft's inequality, and a decoder needs only the lengths.
std::vector<std::string> canonical_codes(const std::vector<int>& len) {
    int n = (int)len.size(); std::vector<int> ord(n); for (int i = 0; i < n; ++i) ord[i] = i;
    std::sort(ord.begin(), ord.end(), [&](int x, int y) { return len[x] != len[y] ? len[x] < len[y] : x < y; });
    std::vector<std::string> out(n); unsigned long long code = 0; int cur = 0;
    for (int k = 0; k < n; ++k) {
        int i = ord[k]; if (len[i] == 0) continue;
        if (k > 0 && len[ord[k - 1]] > 0) ++code;
        code <<= (len[i] - cur); cur = len[i];
        std::string s; for (int b = len[i] - 1; b >= 0; --b) s += (char)('0' + ((code >> b) & 1)); out[i] = s;
    }
    return out;
}
```

*Why it is optimal.* (1) In an optimal tree the two lightest symbols `x` and `y` can be made **siblings at the deepest level**: if the deepest siblings are `a` and `b`, swapping `x` with `a` and `y` with `b` moves lighter symbols deeper and heavier symbols shallower, which cannot increase the cost. (2) Replace the siblings `x`, `y` by one symbol of weight `w_x + w_y`: every tree for the reduced problem corresponds to a tree for the original one with cost larger by exactly `w_x + w_y`. So an optimal reduced tree, with the siblings split again, is optimal. That is the greedy choice property and the substructure; induction finishes it.

Trace (checked by running the code): the textbook frequencies `a:45 b:13 c:12 d:16 e:9 f:5`. The frequent symbol gets 1 bit, the rare ones 4. The cost is 224 against 300 for a fixed 3-bit code:

```
a weight=45 length=1 code=0
b weight=13 length=3 code=100
c weight=12 length=3 code=101
d weight=16 length=3 code=110
e weight=9 length=4 code=1110
f weight=5 length=4 code=1111
cost=224 fixed 3 bit cost=300
```

**Canonical codes.** The tree is not needed to decode, only the lengths. Sort symbols by (length, index), give the first the all-zero code, and for each next symbol add one and shift left whenever the length grows. The lengths of any prefix code satisfy **Kraft's inequality**, `sum 2^(-length) <= 1`, and a Huffman tree is full, so equality holds (the tests check `sum 2^(L - length) = 2^L` for 1000+ trees); that is exactly the condition under which this construction yields a prefix-free code. DEFLATE (zlib, gzip, PNG) stores only code lengths and rebuilds canonical codes this way (from memory).

**How close to the limit?** Shannon's entropy `H = -sum p log2 p` is a lower bound on the average bits per symbol, and Huffman's average length `L` satisfies `H <= L < H + 1` (from memory; the tests check it on 300 random distributions). Measured on a Zipf-like source over 256 symbols (weight proportional to 1/rank): entropy 6.2216 bits per symbol, Huffman 6.2579, a fixed code 8; the longest code had 11 bits.

**Cost.** O(n log n) with a heap. For n = 1,000,000 random weights the build took 454 / 358 ms (run 1 / run 2), the longest code had 39 bits, and the cost agreed with an independent two-queue method (sort once, then merge from two queues in O(n)) in the tests. With input already sorted the heap is unnecessary.

## 5. One-pass scans: jump games and the gas station

Three problems in which one running number, updated in a single scan, carries everything the past can offer.

```cpp
// Three one-pass greedy scans. In each, one running value summarises everything the past can offer.
// Jump game: a[i] is the farthest you can jump from i. Can you reach the last index? Keep reach = the farthest index reachable so far;
// if you ever stand beyond it, stop. O(n).
bool can_jump(const std::vector<int>& a) {
    long long reach = 0; int n = (int)a.size();
    for (int i = 0; i < n; ++i) { if (i > reach) return false; reach = std::max(reach, (long long)i + a[i]); }
    return true;
}
// Jump game II: the fewest jumps to reach the last index, or -1. Think of breadth-first search by levels: cur_end is the end of the
// current level (what is reachable with `jumps` jumps), far is the farthest reachable with one more jump. When i passes cur_end,
// one more jump is needed. O(n), no queue.
int min_jumps(const std::vector<int>& a) {
    int n = (int)a.size(); if (n <= 1) return 0;
    long long cur_end = 0, far = 0; int jumps = 0;
    for (int i = 0; i < n - 1; ++i) {
        if (i > far) return -1;
        far = std::max(far, (long long)i + a[i]);
        if (i == cur_end) { ++jumps; cur_end = far; if (cur_end >= n - 1) return jumps; }
    }
    return far >= n - 1 ? jumps : -1;
}
// Gas station: a circle of stations, gas[i] is what station i gives, cost[i] what the leg i -> i+1 burns. From which station can you
// drive the whole circle? If the total gas is less than the total cost, nowhere. Otherwise, whenever the tank goes negative at
// station i, no start in the stretch since the last candidate can work, so the next candidate is i + 1. Returns a valid start or -1.
int gas_start(const std::vector<int>& gas, const std::vector<int>& cost) {
    long long total = 0, tank = 0; int start = 0, n = (int)gas.size();
    for (int i = 0; i < n; ++i) { long long d = (long long)gas[i] - cost[i]; total += d; tank += d; if (tank < 0) { start = i + 1; tank = 0; } }
    return total >= 0 && n > 0 ? start % n : -1;
}
```

**Jump game** (can you reach the end when `a[i]` is the farthest you can jump from `i`?). The invariant: after processing index `i`, `reach` is the largest index reachable from `0`. If we ever stand at `i > reach` we are stuck; otherwise the last index is reachable. *Why safe:* every index up to `reach` is reachable (it lies on a jump from a reachable index to a farther one, and a jump may be shorter than the maximum), so keeping only the maximum loses nothing.

**Jump game II** (fewest jumps). This is a breadth-first search without the queue. The indices reachable in `j` jumps form a prefix `[0, cur_end]`; scanning it, `far` collects the farthest reach of one more jump; at `cur_end` we pay one jump and the prefix grows to `far`. Greedy stays ahead of any other jump sequence: after `j` jumps nothing can be farther than `cur_end`.

**Gas station.** If the total gas is less than the total cost, no start works. Otherwise a start exists, and the scan finds it: if starting at `s` the tank first goes negative at `i`, then starting at any station between `s` and `i` fails at or before `i` too (it begins with less tank than the run from `s` had there, since every prefix of the run from `s` was non-negative), so the next candidate is `i + 1`. Existence when the total is non-negative: start just after the point where the running sum is smallest, and the circular sum never dips.

```
[2,3,1,1,4] can_jump=1 min_jumps=2
[3,2,1,0,4] can_jump=0 min_jumps=-1
gas [1,2,3,4,5] cost [3,4,5,1,2] start=3
gas [2,3,4] cost [3,4,3] start=-1
```

Measured (jump game II, 30,000 indices, jumps up to 3000): the scan took 0.0223 / 0.0175 ms per call, the table-filling dynamic program 38.0 / 25.7 ms (run 1 / run 2), which is 1,469 to 1,704 times slower, with the same answer (11 jumps). The table costs O(n * maximum jump), the scan O(n).

## 6. When greedy fails

**Coin change** (fewest coins for an amount) is the classic trap. "Always take the largest coin that fits" is optimal for some coin systems and wrong for others. With coins `{1, 3, 4}` and amount 6 it takes `4+1+1` (3 coins) when `3+3` (2) is available. A system for which greedy is optimal at every amount is called **canonical**; `{1,5,10,25}` and the euro coins `{1,2,5,10,20,50,100,200}` are, `{1,5,10,21,25}` is not (63 takes 6 coins greedily and 3 exactly):

```cpp
// Where the greedy choice is wrong. Coin change: the fewest coins for an amount. Greedy takes the largest coin that fits.
// The dynamic program is exact. coins must contain 1 for every amount to be payable.
int coin_greedy(std::vector<int> coins, int amount) {
    std::sort(coins.rbegin(), coins.rend()); int used = 0;
    for (int c : coins) { used += amount / c; amount %= c; }
    return amount == 0 ? used : -1;
}
int coin_dp(const std::vector<int>& coins, int amount) {
    const int INFC = INT_MAX / 2; std::vector<int> best(amount + 1, INFC); best[0] = 0;
    for (int a = 1; a <= amount; ++a) for (int c : coins) if (c <= a && best[a - c] + 1 < best[a]) best[a] = best[a - c] + 1;
    return best[amount] >= INFC ? -1 : best[amount];
}
// A coin system is CANONICAL if greedy is optimal for every amount. Checked here for all amounts up to `limit`
// (a finite check; a published bound says that the two largest coins' sum is enough, from memory).
bool coin_canonical(const std::vector<int>& coins, int limit) {
    std::vector<int> best(limit + 1, INT_MAX / 2); best[0] = 0;
    for (int a = 1; a <= limit; ++a) for (int c : coins) if (c <= a && best[a - c] + 1 < best[a]) best[a] = best[a - c] + 1;
    for (int a = 1; a <= limit; ++a) if (coin_greedy(coins, a) != best[a]) return false;
    return true;
}
```

```
coins {1,3,4} amount 6: greedy=3 exact=2
coins {1,5,10,25} canonical up to 2000: 1
coins {1,5,10,21,25} canonical up to 200: 0 (amount 63: greedy=6 exact=3)
```

How common are non-canonical systems? Enumerating **every** system `{1, a, b, c}` with `2 <= a < b < c <= 40`: 8,363 of 9,139 (91.5%) are non-canonical, and of the 741 systems `{1, a, b}` with `b <= 40`, 501 (67.6%). Canonical systems are the exception, and real currencies are designed to be among them. The check `coin_canonical` tries every amount up to a limit; a published result bounds the amounts that need checking by the sum of the two largest coins (from memory; the tests compare that bound with a limit three times larger on 3000 random systems and they always agree).

**How to test a greedy.** Before proving anything, ask three questions and let the machine answer the third:

1. What is the greedy choice, and can I bend any optimal solution to contain it (exchange), or show greedy is ahead at each step?
2. Is what remains the same kind of problem?
3. On thousands of random tiny instances, does the greedy ever lose to brute force or to the exact algorithm? If it does, print the smallest instance.

That is how every wrong rule above was found: the "fewest conflicts" counterexample in section 2 came from a loop that generated random instances of 12 activities until greedy lost.

## 7. Recognising and choosing

| Signal in the problem | Greedy that usually works | Proof tool |
|---|---|---|
| Maximise the number of non-overlapping intervals | Sort by end, take if compatible | Exchange / stays ahead |
| Split things freely to fill a capacity | Best ratio first | Exchange |
| Minimise a weighted sum of leaf depths | Merge the two lightest | Exchange, then substructure |
| Can I reach / how few steps, each step a range | Track the farthest reach | Invariant / stays ahead |
| A circular tour with a running balance | Total test plus reset at the first failure | Prefix-sum argument |
| Pick whole items under a capacity, or exact change | Not greedy | Dynamic programming (next area) |
| Tight spanning or shortest structure on a graph | Kruskal, Prim, Dijkstra | Cut property |

## 8. What the tests check, and what they cannot

Of the 47 mutants, 39 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Eight survive, and each leaves every output the tests can see unchanged. Several of them changed the tests: the first run of this mutant set left 16 survivors, and the pin tests added for tie-breaking, exact fits, wide arithmetic, canonical codes and unpayable amounts killed eight of them. The reasons the other eight stay:

- **Taking the maximum of the last end and the new end in activity selection.** The activities are sorted by end, so the new end is never smaller than the last one; the maximum is the new end.
- **A comparator that returns "true" for two weight-0 items in both directions.** It is not a strict weak order, so in principle `std::sort` may misbehave, but with the sizes tested the output is the same. This one is a real defect that the tests cannot see, which is why the listing writes the weight-0 case explicitly.
- **Swapping the two merged children in Huffman.** Both children get the same depth, so nothing changes.
- **Incrementing the canonical code before a symbol that follows a length-0 symbol.** The extra increment adds a bit above the width that is printed, so the strings are the same.
- **Skipping a length-0 symbol with `continue` versus writing an empty code for it.** Same strings, same state afterwards.
- **Removing the `n <= 1` guard in the jump count.** With one element or none, the loop does not run and the final line returns 0 anyway.
- **`>=` instead of `==` when the scan reaches the end of the current level.** The index can never exceed the end of the level (that case returns -1 first), so the two tests agree.
- **Stopping one step later when the level reaches the last index.** The scan then finishes the loop and returns the same count.

The 100,000-interval and 200,000-weight inputs have no brute-force reference; they are checked against independent algorithms (a dynamic program with binary search, and the two-queue Huffman). What the tests cannot do is prove a greedy correct: they show that it agreed with the exact answer on the instances tried, which is evidence, not a proof, and that is why the proofs are in the text. A rule that fails once in two thousand instances is one a random test of a few hundred cases can miss.

Mutants are chosen where a bug is plausible, not at random, so "39 of 47" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Trusting a rule because it works on the examples.** The fewest-conflicts rule wins on 99.95% of random instances.
2. **Sorting activities by start time or by length** instead of by finish time.
3. **Comparing ratios with floating-point division.** Cross-multiply, in a wide type.
4. **A comparator that is not a strict weak order** (weight 0 equal to everything, `<=` instead of `<`): undefined behaviour in `std::sort`.
5. **Using the fractional rule for 0/1 knapsack,** or the largest-coin rule for an arbitrary coin system.
6. **Treating a closed and a half-open interval alike.** Decide whether `[1,3]` and `[3,5]` conflict before writing `>=` or `>`.
7. **Forgetting the single-symbol Huffman case** (length 0 here; real formats use one bit).
8. **Believing the Huffman tree is unique.** Ties can be broken differently; the cost is the same, and canonical codes remove the ambiguity.
9. **Updating `reach` before checking `i > reach`** in the jump game.
10. **Resetting the gas station start to `i` instead of `i + 1`,** or forgetting the total test.
11. **Proving the greedy choice but not the substructure,** or the reverse.

## 10. Questions to answer out loud

1. What two properties make a greedy algorithm correct?
2. Describe the exchange argument and "greedy stays ahead", and give one problem for each.
3. Why is earliest-finish correct for activity selection and earliest-start not?
4. Why is the fractional knapsack greedy correct, and exactly where does the proof break for 0/1?
5. Prove that Huffman's merge of the two lightest is safe.
6. What does a canonical Huffman code need to be rebuilt, and why does Kraft's equality matter?
7. State the entropy bounds for Huffman coding.
8. Why does the jump game need only the farthest reach? How is Jump Game II a breadth-first search?
9. In the gas station problem, why can we skip every start between the old candidate and the failure point?
10. Give a coin system where greedy fails and one where it is optimal.
11. How would you test a greedy you cannot prove?
12. Which classic graph algorithms are greedy, and what is their proof tool?

## 11. Problems

### 11.1 On paper

**A.** Run earliest-finish on the intervals `[0,3), [2,5), [4,7), [6,9), [1,2), [8,10)`. List the order examined and the chosen set.

**B.** Give a set of four activities on which "earliest start" returns 1 but the optimum is 3.

**C.** Prove that for the fractional knapsack the greedy answer is at least the 0/1 optimum. Why is it not always equal?

**D.** Build the Huffman code lengths and the cost for the weights `1, 1, 2, 3, 5, 8, 13`. What shape is the tree, and why?

**E.** Run the gas station scan on `gas = [5,1,2,3,4]`, `cost = [4,4,1,5,1]`. Which start does it return, and why not a smaller one?

**F.** Give the minimum jumps for `[2,3,0,1,4]`, `[1,1,1,1]` and `[1,0,1]`.

**G.** For coins `{1, 4, 6}` and amount 8, what does greedy give, what is optimal, and is the system canonical?

### 11.2 On LeetCode

For each: what is the greedy choice, and what is its exchange or stays-ahead argument? Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Jump Game | 55 | Farthest reach (section 5) |
| Jump Game II | 45 | Levels without a queue |
| Gas Station | 134 | Total test plus reset |
| Assign Cookies | 455 | Sort both; match the smallest satisfiable greed |
| Lemonade Change | 860 | Give back the largest bill that works first |
| Boats to Save People | 881 | Sort; pair the heaviest with the lightest that fits |
| Partition Labels | 763 | Extend the end to the last occurrence of every letter seen |
| Candy | 135 | Two passes: left to right, right to left |
| Queue Reconstruction by Height | 406 | Tallest first, insert at index `k` |

### 11.3 In your editor

**A.** Write a Huffman encoder and decoder for bytes of a file with canonical codes written as a length table; check the round trip and compare the size with the original.

**B.** Write the generic harness of section 6: a random-instance generator, a greedy, an exact solver, and a loop that prints the smallest failing instance. Use it to find a failing rule of your own invention.

**C.** Implement weighted interval scheduling (each activity has a value; maximise the total) by dynamic programming with binary search, and find an instance on which the earliest-finish greedy loses.

**D.** For coin systems of up to 5 coins, compute the canonical ones by brute force and compare with the sum-of-two-largest-coins bound.

**E.** Write the Huffman build with two queues (sorted weights) and time it against the heap version on 10 million weights.

**F.** Prove (on paper) and then check by brute force that Kruskal is the greedy of a matroid: any set of edges forming a forest can be extended, and the greedy picks the lightest.

### Answers

**A.** Sorted by end: `[1,2)`, `[0,3)`, `[2,5)`, `[4,7)`, `[6,9)`, `[8,10)`. Take `[1,2)`; skip `[0,3)` (starts before 2); take `[2,5)` (starts at 2, which equals the last end); skip `[4,7)`; take `[6,9)`; skip `[8,10)`. Three activities: `[1,2) [2,5) [6,9)` (the code returns the same).

**B.** `[0,10), [1,2), [3,4), [5,6)`. Earliest start takes `[0,10)` and then nothing else fits (1), while the three small ones fit together (the code returns 1 for the rule and 3 for earliest finish).

**C.** Any 0/1 solution is also a valid fractional solution (taking whole items), so the fractional optimum is at least the 0/1 optimum. They are equal exactly when the greedy fractional solution takes no item partly. In the example 240 > 220.

**D.** The weights are the first Fibonacci numbers; every merge produces a subtree heavier than the next weight, so the tree is a chain: lengths `6, 6, 5, 4, 3, 2, 1` and cost 78 (the code returns exactly these).

**E.** The differences `gas - cost` are `1, -3, 1, -2, 3`, total 0, so a start exists. The tank is negative at index 1, so the next candidate is 2; negative again at index 3, so the next is 4. From 4: `+3, +1, -3, +1, -2` keeps the tank non-negative, and the code returns 4. A smaller start fails because the run from it dips below zero first.

**F.** 2, 3 and -1 (the last cannot get past index 1). The code returns 2, 3 and -1.

**G.** Greedy takes 6 then 1 + 1: 3 coins. Optimal: 4 + 4, 2 coins. The system is not canonical (the code agrees).

**11.2 hints.** 455: greedy factors `[1,2,3]` with cookies `[1,1]` gives 1, and `[1,2]` with `[1,2,3]` gives 2. 860: `[5,5,5,10,20]` is true and `[5,5,10,10,20]` is false. 881: `[1,2]` limit 3 gives 1, `[3,2,2,1]` limit 3 gives 3, `[3,5,3,4]` limit 5 gives 4. 763: `"ababcbacadefegdehijhklij"` gives `9 7 8`. 135: `[1,0,2]` gives 5 and `[1,2,2]` gives 4. 406: the sample gives `[5,0] [7,0] [5,2] [6,1] [4,4] [7,1]`. 55, 45, 134: the samples used in this document give true, 2 and 3. All of these samples were checked by running my own implementations.

**11.3 hints.** C: with values the earliest-finish rule ignores the value; a long valuable interval can beat two short cheap ones. F: the matroid exchange property says that if a forest `A` is smaller than a forest `B`, some edge of `B` can be added to `A`.

---

## Cheat sheet

- **Correct greedy = safe greedy choice + optimal substructure.** Prove the choice by exchange (bend an optimal solution to contain it) or by staying ahead (induction on steps).
- **Activity selection:** sort by end; take if start >= last end. The other three natural rules are wrong (48.9%, 5.7%, 0.045% of random instances).
- **Fractional knapsack:** best value per weight first, compare by cross-multiplication. **0/1:** not greedy, dynamic programming.
- **Huffman:** merge the two lightest; canonical codes from the lengths; `H <= L < H + 1`.
- **Jump game:** farthest reach; **II:** levels without a queue; **gas station:** total test plus reset to `i + 1`.
- **Coin change:** greedy only for canonical systems; most random systems are not.
- **Test every greedy** against brute force on small random inputs and print the smallest failure.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The exchange argument and "greedy stays ahead", activity selection, fractional knapsack, Huffman's algorithm (1952), Kraft's inequality, Shannon's entropy bound, the DEFLATE use of canonical codes, and the bound of the two largest coins for canonical coin systems (Kozen and Zaks, 1994) are standard; names and dates are from memory. The textbook instances (eleven activities, the frequencies 45, 13, 12, 16, 9, 5, the three-item knapsack with value 220) are the usual ones in algorithms textbooks, from memory.
- Problem numbers and titles in section 11, and the samples of problems 55, 45, 134, 455, 860, 881, 763, 135 and 406, are LeetCode's, from memory; check a number before you rely on it.
