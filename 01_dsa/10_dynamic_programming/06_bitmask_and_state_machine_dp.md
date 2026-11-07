# DSA 10.06: Bitmask and State-Machine DP

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 10 hours (5 reading, 5 problems) · You need: [DP fundamentals](01_dp_fundamentals.md) (state, transition, the fill order) and [The knapsack family](04_the_knapsack_family.md) (loop direction, using each item once). There is no companion demo file: every structure is listed in this doc

So far the state was a number, a prefix or a range. This tutorial has three other kinds of state, and each one is a way of squeezing a **small description of "what has happened so far"** into the table index:

1. **A set, stored as a bitmask.** When there are at most about 20 items, a *subset* of them is a number whose bits say which items are in. `f(mask)` or `f(mask, last)` is then an ordinary DP table with `2^n` rows. This is **bitmask DP**. It solves the travelling-salesman tour, assignment, and partition problems that are exponential by brute force and still exponential here, but with a base of 2 instead of a factorial.
2. **A mode, stored as a small label.** In the stock problems you are either holding a share or not, maybe also "cooling down". The state is `(day, mode)`, and each day moves along the edges of a small machine. That is **state-machine DP**.
3. **A frontier, stored as a mask.** To fill a grid cell by cell, the only thing the future needs to know about the past is a thin strip of cells at the frontier. That strip is the state: **profile DP**.

The tool is the same in all three: choose the smallest description of the past that the future depends on, and make it the index.

By the end you can:

- do the bit operations (set, test, clear, count, lowest bit, loop over all subsets of a mask) without looking them up, and say what `2^n` memory and `3^n` time mean for the largest `n` you can run;
- write the travelling-salesman recurrence, recover the tour, and write the assignment and the hats problem with the right choice of what the mask ranges over;
- decide when a problem should be a mask over the items, over the people, or a BFS over `(mask, node)`;
- write each stock problem as a state machine, with the right order of updates, and say why a shortcut such as "take the biggest rises" fails;
- count tilings of a grid with a profile that moves one cell at a time.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 31,707 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the tour against every permutation of the cities, with the printed tour replayed so that it visits every city once and its own cost is checked, plus a line-shaped case whose best tour is known without searching (twice the span); assignment against every permutation, and against sorted pairing for costs `|a - b|`; hats against a plain recursion, and against `40 * 39 * ... * 31` for ten people who like every hat; the equal-sum partition against a recursion that fills buckets; the shortest walk through all nodes against Floyd-Warshall plus every visiting order; work sessions against a bin-packing recursion; every stock problem against a brute force over all action sequences (buy, sell, rest), with the transaction limit, the cooldown and the fee as options, plus relations between them; domino tilings against a recursion that tiles the first free cell, against 12,988,816 for the 8 by 8 board, and against Fibonacci for a board two cells wide; grids with no two adjacent ones against enumeration; and pins for the cases random inputs rarely hit (no cities, one city, an empty list, a negative limit, a limit larger than the number of days). Then 69 hand-made mutants of the listings were run against the tests: **58 were caught and 11 are equivalent changes**; section 7 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Bitmask DP: tours, assignments and hats

**The bit toolkit.** A set of items `0..n-1` is an integer `mask`. Bit `j` is 1 when item `j` is in the set.

| Want | Write | Note |
|---|---|---|
| add `j` | `mask \| (1 << j)` | |
| remove `j` | `mask ^ (1 << j)` | only when `j` is in |
| is `j` in | `mask >> j & 1` | or `mask & (1 << j)` |
| all `n` items | `(1 << n) - 1` | for `n >= 31` use `1LL << n`; you will not get there, the table would not fit |
| size of the set | `__builtin_popcount(mask)` | `popcountll` for 64 bits |
| lowest member | `__builtin_ctz(mask)` | undefined for `mask == 0` |
| drop the lowest member | `mask & (mask - 1)` | |

The table has `2^n` rows, so `n = 20` is a million rows and `n = 25` is thirty-three million: the practical limit is about 20 items, a little more if each row is one byte.

**Travelling salesman (Held-Karp).** There are `n` cities and `d[i][j]` is the cost of going from `i` to `j` (it need not be symmetric). Start at city 0, visit every other city exactly once and come back; find the cheapest tour. Trying every order costs `(n-1)!`. The DP needs a different state: not the order, only **which cities are done and which one you stand on**, because the cheapest way to finish does not care how you got there.

```
f(mask, j) = cheapest path that starts at 0, visits exactly the cities in mask, and stands at j   (j in mask)
f({j}, j)  = d[0][j]
f(mask, j) = min over k in mask, k != j of  f(mask minus j, k) + d[k][j]
answer     = min over j of  f(all, j) + d[j][0]
```

City 0 is left out of the mask (city `c` is bit `c - 1`), which halves the table. There are `2^(n-1) * (n-1)` states and each tries `n - 1` predecessors, so the time is `O(2^n * n^2)`. The code pushes forward: from `f(mask, j)` it relaxes `f(mask + k, k)`, which is the same as the pull form. A second array `par` remembers the predecessor, and the tour is read back from the last city.

**Assignment.** `n` workers, `n` jobs, `c[i][j]` = cost of giving job `j` to worker `i`, one job per worker; the cheapest total. The workers are handled in order, so the state is the set of jobs already used, and the *worker is not stored*: it is `popcount(mask)`. This is the general trick: when the order is fixed, the size of the set tells you the position.

**Hats.** `n` people (at most 10), hats numbered 1 to 40, `likes[p]` = the hats person `p` likes. Count the ways everyone wears a hat they like, all different, modulo `10^9 + 7`. A mask over the *hats* would need `2^40` rows. A mask over the *people* needs `2^10`. So the hats become the **stages**: go through hat 1, hat 2, ..., hat 40, and for each hat either skip it or give it to one person who likes it and does not yet have a hat. The mask says who already has a hat. Masks run **downward** in each stage, exactly like the 0/1 knapsack, so that a hat is not given twice.

```cpp
// Bitmask DP: when the items are few (about 20 or fewer), a SET of them is a number whose bits say which items are in it, and the set
// itself is the state.  Bit j of mask is 1 when item j is in the set.  Useful operations: mask | (1 << j) adds j, mask & (1 << j)
// tests j, mask ^ (1 << j) removes j, __builtin_popcount(mask) counts the items, (1 << n) - 1 is the full set.
// Travelling salesman (Held-Karp): visit every city once, start and end at city 0, cheapest total.  d[i][j] is the cost of going
// from i to j (it need not be symmetric).  f(mask, j) = the cheapest way to start at 0, visit exactly the cities in mask (city 0 is left
// out of the mask, city c is bit c-1) and stand at city j (j in mask).  f(mask, j) = min over k in mask, k != j of f(mask - j, k) + d[k][j].
struct Tour { ll cost; std::vector<int> order; };
Tour tsp_tour(const std::vector<std::vector<ll>>& d) {
    int n = (int)d.size(); Tour t; t.cost = 0; if (n == 0) return t; t.order.push_back(0); if (n == 1) return t;
    int m = n - 1, full = (1 << m) - 1; const ll INF = LLONG_MAX / 4;
    std::vector<ll> f((size_t)(full + 1) * m, INF); std::vector<signed char> par((size_t)(full + 1) * m, -1);
    for (int j = 0; j < m; ++j) f[(size_t)(1 << j) * m + j] = d[0][j + 1];
    for (int mask = 1; mask <= full; ++mask) for (int j = 0; j < m; ++j) {
        if (!(mask >> j & 1)) continue; ll cur = f[(size_t)mask * m + j]; if (cur >= INF) continue;
        for (int k = 0; k < m; ++k) if (!(mask >> k & 1)) {
            size_t to = (size_t)(mask | 1 << k) * m + k; ll c = cur + d[j + 1][k + 1]; if (c < f[to]) { f[to] = c; par[to] = (signed char)j; }
        }
    }
    ll best = INF; int last = 0;
    for (int j = 0; j < m; ++j) { ll c = f[(size_t)full * m + j] + d[j + 1][0]; if (c < best) { best = c; last = j; } }
    t.cost = best; std::vector<int> rev; int mask = full, j = last;
    while (j >= 0) { rev.push_back(j + 1); int p = par[(size_t)mask * m + j]; mask ^= 1 << j; j = p; }
    for (int i = (int)rev.size() - 1; i >= 0; --i) t.order.push_back(rev[i]);
    return t;
}
// Assignment: worker i must get exactly one job, each job to one worker, cost c[i][j]; the cheapest total.  The workers are taken in
// order, so the state is just the set of jobs already used: f(mask) = the cheapest way to give the first popcount(mask) workers the
// jobs in mask.  Time O(2^n * n).
ll assignment_min(const std::vector<std::vector<ll>>& c) {
    int n = (int)c.size(); if (n == 0) return 0; const ll INF = LLONG_MAX / 4; std::vector<ll> f((size_t)1 << n, INF); f[0] = 0;
    for (int mask = 0; mask < (1 << n) - 1; ++mask) {
        if (f[mask] >= INF) continue; int i = __builtin_popcount(mask);
        for (int j = 0; j < n; ++j) if (!(mask >> j & 1)) f[mask | 1 << j] = std::min(f[mask | 1 << j], f[mask] + c[i][j]);
    }
    return f[(1 << n) - 1];
}
// Number of ways to wear different hats (n people, hats numbered 1 to 40, likes[p] = the hats person p likes; everyone wears a hat they
// like and no two share one), modulo MOD.  Too many hats for a mask, so the mask is over the PEOPLE (at most 10) and the hats are the
// stages.  f(mask) = ways to give the people in mask a hat among the hats seen so far.  Masks run downward so each hat is used once.
ll hats_ways(const std::vector<std::vector<int>>& likes) {
    int n = (int)likes.size(); std::vector<std::vector<int>> byHat(41);
    for (int p = 0; p < n; ++p) for (int h : likes[p]) byHat[h].push_back(p);
    std::vector<ll> f((size_t)1 << n, 0); f[0] = 1;
    for (int h = 1; h <= 40; ++h) for (int mask = (1 << n) - 1; mask >= 0; --mask) {
        if (!f[mask]) continue;
        for (int p : byHat[h]) if (!(mask >> p & 1)) f[mask | 1 << p] = (f[mask | 1 << p] + f[mask]) % MOD;
    }
    return f[(1 << n) - 1];
}
```

```
four cities, city 0 is the start; mask bit c-1 is city c. f(mask, last city) = cheapest path from city 0 through exactly the cities in mask, ending at last
  mask 001: end@1=10
  mask 010: end@2=15
  mask 011: end@1=50 end@2=45
  mask 100: end@3=20
  mask 101: end@1=45 end@3=35
  mask 110: end@2=50 end@3=45
  mask 111: end@1=70 end@2=65 end@3=75
best tour cost=80 (code gives 80), order: 0 2 3 1 0
```

```
assignment of 3 workers to 3 jobs, costs rows 9 2 7 / 6 4 3 / 5 8 1; f(mask of jobs used) with that many workers placed
  jobs 000: 0
  jobs 001: 9
  jobs 010: 2
  jobs 011: 8
  jobs 100: 7
  jobs 101: 12
  jobs 110: 5
  jobs 111: 9
cheapest=9 (code gives 9)
```

Read the first trace from the bottom. The costs are `0 10 15 20 / 10 0 35 25 / 15 35 0 30 / 20 25 30 0`. With all three other cities visited (`mask 111`) the path ends at city 1 for 70, at city 2 for 65 or at city 3 for 75; closing the tour adds `d[j][0]`, which is 10, 15 and 20, giving 80, 80 and 95. The cheapest total is 80, reached from city 1 or from city 2: these are the same tour read in opposite directions (`0 1 3 2 0` and `0 2 3 1 0`), and the code keeps the first, `0 2 3 1 0`, because it only replaces on a strictly smaller cost. In the second trace the worker is not in the state: `jobs 011` means "two workers have been placed, on jobs 0 and 1", and the cheapest cost of that is 8.

Measured. For 10 cities there are 362,880 orders to try, and the plain permutation search found the same cost as the table (159); the table has only 4,608 states. For 16 and 20 cities the table has 491,520 and 9,961,472 states. Times: the permutation search for 10 cities took 2 / 2 ms, the table 0.14 / 0.11 ms; 16 cities took 18 / 14 ms and 20 cities 358 / 397 ms (a permutation search for 20 cities would need 19! orders, about `1.2 * 10^17`, so it was not run). Assignment of 20 workers (1,048,576 masks) took 57 / 72 ms and gave 319,926, which equals the answer of sorted pairing for the costs `|a - b|` (the check is in the tests). The hats problem with 10 people who like all 40 hats counted 502,474,470 ways (equal to `40 * 39 * ... * 31` modulo the prime, checked in the tests) in 1 / 1 ms.

## 2. Partition, shortest walk and work sessions

**Partition into k equal-sum subsets.** Given positive numbers, can they be split into `k` groups of equal sum? The sum must be divisible by `k`, call the share `target`, and no number may exceed it. Now fill the groups **one at a time**. The state is the set of numbers already placed, and the one extra fact needed is how full the current group is. But that is determined by the mask: the placed numbers sum to some `S`, and the group being filled holds `S mod target`. So the table stores just that remainder.

```
cur[mask] = amount in the group being filled after placing the numbers in mask   (-1 if unreachable)
cur[0] = 0
cur[mask | i] = (cur[mask] + nums[i]) mod target     if cur[mask] + nums[i] <= target
answer = (cur[all] == 0)
```

A group that becomes exactly full wraps to 0 and the next number starts a new group. The time is `O(2^n * n)`; compare the backtracking of [constraint backtracking](../06_recursion_and_backtracking/03_constraint_backtracking.md), which can revisit the same set of placed numbers by many routes.

**Shortest walk through all nodes.** In a connected graph with `n <= 12` nodes, find the shortest walk that visits every node (nodes and edges may repeat). The state is `(mask of visited nodes, node you stand on)` and every move costs 1, so a **breadth-first search** over those states finds the answer: the first time a state with the full mask comes off the queue. The BFS starts from every node at once, as `(1 << i, i)` with distance 0. This is the same idea as [BFS on states](../08_graphs/04_bfs_on_states_and_grids.md): the state is bigger than "where am I".

**Minimum work sessions.** Tasks with durations; a session can hold any set of tasks whose total is at most `limit`; find the fewest sessions. State `f(mask)` = the fewest sessions for the tasks in `mask`. The last session is some **subset** `s` of `mask` with `sum(s) <= limit`:

```
f(mask) = min over non-empty subsets s of mask with sum(s) <= limit of  f(mask minus s) + 1
```

To list the non-empty subsets of `mask`, use the loop `for (s = mask; s; s = (s - 1) & mask)`: subtracting 1 turns the lowest set bit of `s` into 0 and all the bits below it into 1; the `& mask` throws away the bits that are not in `mask`, so what is left is the next smaller subset. The subsets come out in decreasing order, and the last one is the empty set, which stops the loop. Summed over all masks the work is `3^n`, because each of the `n` items is either not in `mask`, in `mask` but not in `s`, or in `s`: three choices. The sum of each subset is built once per mask with the lowest bit, `sum[mask] = sum[mask & (mask - 1)] + tasks[ctz(mask)]`.

```cpp
// Partition into k subsets of equal sum (positive numbers).  State f(mask) = the amount in the bucket being filled after the numbers in
// mask have been placed, one bucket at a time; -1 if the mask cannot be reached.  Adding a number to a bucket that is not full is
// allowed, and a full bucket starts a new one (the amount wraps to 0 through the remainder).  All numbers placed and the amount 0 means k
// buckets are exactly full.
bool can_partition_k(const std::vector<int>& nums, int k) {
    int n = (int)nums.size(); if (n == 0) return k >= 1; if (k <= 0) return false;
    ll total = 0; for (int x : nums) total += x; if (total % k) return false;
    int target = (int)(total / k); for (int x : nums) if (x > target) return false;
    std::vector<int> cur((size_t)1 << n, -1); cur[0] = 0;
    for (int mask = 0; mask < (1 << n) - 1; ++mask) {
        if (cur[mask] < 0) continue;
        for (int i = 0; i < n; ++i) if (!(mask >> i & 1) && cur[mask] + nums[i] <= target) cur[mask | 1 << i] = (cur[mask] + nums[i]) % target;
    }
    return cur[(1 << n) - 1] == 0;
}
// Shortest path visiting every node of a connected undirected graph (nodes may be revisited, edges re-used).  The state is
// (set of nodes visited, node we stand on), every move costs 1, so a breadth-first search over those states finds the answer: the
// first time a state with the full set appears.  It starts from every node at once.
int shortest_path_all_nodes(const std::vector<std::vector<int>>& g) {
    int n = (int)g.size(); if (n <= 1) return 0; int full = (1 << n) - 1;
    std::vector<int> dist((size_t)(full + 1) * n, -1); std::queue<std::pair<int, int>> q;
    for (int i = 0; i < n; ++i) { dist[(size_t)(1 << i) * n + i] = 0; q.push({1 << i, i}); }
    while (!q.empty()) {
        auto [mask, u] = q.front(); q.pop(); int d = dist[(size_t)mask * n + u]; if (mask == full) return d;
        for (int v : g[u]) { int nm = mask | 1 << v; if (dist[(size_t)nm * n + v] < 0) { dist[(size_t)nm * n + v] = d + 1; q.push({nm, v}); } }
    }
    return -1;
}
// Minimum number of work sessions: tasks with times (each at most limit); a session holds tasks of total time at most limit.  State
// f(mask) = the fewest sessions for the tasks in mask.  The last session is a SUBSET s of mask: f(mask) = min over subsets s with
// sum(s) <= limit of f(mask without s) + 1.  Enumerating all the subsets of every mask costs 3^n in total.
int min_sessions(const std::vector<int>& tasks, int limit) {
    int n = (int)tasks.size(); if (n == 0) return 0; int full = (1 << n) - 1;
    std::vector<int> sum((size_t)full + 1, 0), f((size_t)full + 1, INT_MAX / 2);
    for (int mask = 1; mask <= full; ++mask) { int low = __builtin_ctz(mask); sum[mask] = sum[mask & (mask - 1)] + tasks[low]; }
    f[0] = 0;
    for (int mask = 1; mask <= full; ++mask)
        for (int s = mask; s; s = (s - 1) & mask) if (sum[s] <= limit) f[mask] = std::min(f[mask], f[mask ^ s] + 1);
    return f[full];
}
```

```
numbers 4 3 2 3 5 2 1 into 4 buckets of 5: 102 of 128 masks are reachable; the full mask ends with bucket amount 0, so the answer is yes (code gives 1)
submasks of 1011 in decreasing order: 1011 1010 1001 1000 0011 0010 0001
```

The third trace shows the second thing to notice about the partition table: of the 128 masks of seven numbers, only 102 are reachable at all. An unreachable mask is a set of numbers that cannot be placed in order without a group overflowing, and the loop skips it. The trace also lists the submasks of `1011` (decimal 11) in decreasing order: 7 subsets, which is `2^3 - 1`, since three bits are set.

Measured. Partition of 20 numbers (total 100) into 4 groups took 63 / 71 ms and the answer is 1 (yes). The work-sessions DP on 16 tasks with limit 12 needed 7 sessions and took 46 / 49 ms; the number of (mask, submask) pairs it visits is `3^16 =` 43,046,721. The shortest walk through all nodes of a random connected graph with 16 nodes and 22 edges had length 17 and took 2 / 2 ms.

## 3. Stock problems as state machines

On each day you see a price and may buy one share, sell one share or do nothing, holding at most one share. The six classic variants differ only in the **rules about moving between modes**.

Think of a small machine whose states are the modes. The table is `f(day, mode)` = the best profit so far if you are in that mode at the end of the day, and every day applies the edges.

**Unlimited transactions.** Two modes: `free` (no share) and `hold`. A day moves `hold -> free` by selling (`+p`) and `free -> hold` by buying (`-p`), or stays.

```
free' = max(free, hold + p)         hold' = max(hold, free - p)
```

Both new values are computed from the **old** pair before either is stored, so a share cannot be bought and sold on the same day for an extra gain. The answer is `free` at the end. (This equals the sum of all the day-to-day rises, which the tests check.)

**At most k transactions.** Now the machine must remember how many transactions are used. Use `buy[j]` = best profit holding the share bought in transaction `j`, and `sell[j]` = best profit after finishing `j` transactions. Day by day, for `j = 1..k`: `buy[j] = max(buy[j], sell[j-1] - p)` and `sell[j] = max(sell[j], buy[j] + p)`. If `k >= n / 2` the limit never binds (a transaction needs two days), so use the unlimited version: that also protects the code from a huge `k` such as a billion, which would otherwise allocate a billion cells. A non-positive `k` or fewer than two days gives 0.

**Cooldown.** After a sale, you cannot buy on the next day. Three modes: `hold`, `sold` (you sold today) and `rest` (free and allowed to buy).

```
hold' = max(hold, rest - p)       sold' = hold + p       rest' = max(rest, sold)
```

Buying comes only from `rest`, never from `sold`; that is the cooldown. The answer is `max(sold, rest)`.

**A fee.** The unlimited machine with the fee subtracted at each sale: `free' = max(free, hold + p - fee)`. The fee is charged **once per transaction**, on the sell edge only. (Subtracting it on the buy as well would charge twice.)

```cpp
// Stock problems as state machines.  Each day you are in one of a few STATES (holding a share or not, maybe "cooling down") and may
// move along an edge (buy, sell, rest).  f(day, state) = the best profit so far; the next day's values come from the previous day's.
// Unlimited transactions: two states, free and hold.
ll stock_unlimited(const std::vector<ll>& prices) {
    ll freeS = 0, hold = LLONG_MIN / 4;
    for (ll p : prices) { ll nf = std::max(freeS, hold + p), nh = std::max(hold, freeS - p); freeS = nf; hold = nh; }
    return freeS;
}
// At most k transactions (a transaction is a buy and its sell): buy[j] = best profit while holding the share bought in transaction j,
// sell[j] = best profit after finishing j transactions.  If k >= n / 2 the limit never binds.
ll stock_k(const std::vector<ll>& prices, ll k) {
    int n = (int)prices.size(); if (n < 2 || k <= 0) return 0; if (k >= n / 2) return stock_unlimited(prices);
    std::vector<ll> buy(k + 1, LLONG_MIN / 4), sell(k + 1, 0);
    for (ll p : prices) for (int j = 1; j <= k; ++j) { buy[j] = std::max(buy[j], sell[j - 1] - p); sell[j] = std::max(sell[j], buy[j] + p); }
    return sell[k];
}
// Cooldown: after a sale you cannot buy the next day.  Three states: hold, just sold today, rest (free and allowed to buy).
ll stock_cooldown(const std::vector<ll>& prices) {
    ll hold = LLONG_MIN / 4, sold = LLONG_MIN / 4, rest = 0;
    for (ll p : prices) { ll nh = std::max(hold, rest - p), ns = hold + p, nr = std::max(rest, sold); hold = nh; sold = ns; rest = nr; }
    return std::max(sold, rest);
}
// A fee is paid on every sale.  Two states again.
ll stock_fee(const std::vector<ll>& prices, ll fee) {
    ll freeS = 0, hold = LLONG_MIN / 4;
    for (ll p : prices) { ll nf = std::max(freeS, hold + p - fee), nh = std::max(hold, freeS - p); freeS = nf; hold = nh; }
    return freeS;
}
```

```
cooldown machine on prices 1 2 3 0 2: (hold, sold, rest) after each day
  start :  -inf -inf 0
  day 1 p=1: -1 -inf 0
  day 2 p=2: -1 1 0
  day 3 p=3: -1 2 1
  day 4 p=0: 1 -1 2
  day 5 p=2: 1 3 2
best=3 (code gives 3)
```

```
at most 2 transactions on 3 3 5 0 0 3 1 4: (buy1 sell1 buy2 sell2) after each day
  day 1 p=3: -3 0 -3 0
  day 2 p=3: -3 0 -3 0
  day 3 p=5: -3 2 -3 2
  day 4 p=0: 0 2 2 2
  day 5 p=0: 0 2 2 2
  day 6 p=3: 0 3 2 5
  day 7 p=1: 0 3 2 5
  day 8 p=4: 0 4 2 6
best=6 (code gives 6)
```

The cooldown trace has columns `hold sold rest` and shows `sold` as `-inf` until a sale is possible. The best, 3, ends in `sold` on day 5: buy at 1 and sell at 2 on day 2 (profit 1), rest on day 3 (the cooldown), buy at 0 on day 4 and sell at 2 on day 5 (profit 2). Selling on day 3 instead would block the cheap buy on day 4. In the second trace, the four columns are `buy1 sell1 buy2 sell2`; on day 4 the price drops to 0, `buy1` becomes 0 and `buy2` becomes 2, because the profit of the first transaction (2, from 3 to 5) pays for the second purchase, and the final answer 6 is that first trade (profit 2) plus the second trade 0 to 4 (profit 4).

**Why not a shortcut?** Each machine has a plausible greedy rule. Measured on 100,000 random price lists of 2 to 9 days with prices 0 to 9, a rule is "wrong" when it differs from the machine:

- **Cooldown ignored** (use the unlimited answer): wrong on 34,698 (34.7%).
- **Fee ignored** (use the unlimited answer for a fee of 2): wrong on 89,079 (89.1%).
- **At most 2 transactions as the best trade in the first half plus the best in the second half:** wrong on 45,728 (45.7%). The best two trades need not be separated at the middle.
- **At most 2 transactions as the sum of the two biggest day-to-day rises:** wrong on 27,119 (27.1%). Two consecutive rises are one trade, and the rise of a trade can span a dip.

Measured, for speed. On 1,000,000 prices, at most 2 transactions took 2 / 3 ms (best profit 1,998), the cooldown machine 1 / 1 ms (131,832,335), and a limit of a billion transactions took 1 / 1 ms and gave 166,494,803, the same as the unlimited answer.

## 4. Profile DP on a grid

**The idea.** Fill an `R` by `C` grid cell by cell in row order. When you are at cell `(r, c)`, the cells that matter for the future are a strip of exactly `C` cells: the ones in the current row from `c` onwards and the ones in the next row up to `c - 1`. Whatever happened further back is finished. A mask with one bit per column says, for each cell of the strip, whether it is already taken. That mask is the **profile**, and the state is `(cell, mask)`. The table is `2^C` per cell, so `C` must be small, about 12 to 16, and `R` can be large.

**Domino tilings.** How many ways can an `R` by `C` board be covered with dominoes (each 1 by 2 or 2 by 1)? Bit `c` is 1 when the strip cell of column `c` is already covered by a domino sticking out of an earlier cell. At cell `(r, c)`:

- if its bit is 1, the cell is already covered: clear the bit and move on;
- otherwise place a **vertical** domino (covers this cell and the one below: set bit `c`, for the next row; only if there is a next row) or a **horizontal** one (covers this cell and the one to the right: set bit `c + 1`; only if there is a next column and that cell is free).

After the last cell the count with mask 0 is the answer. Because the board is walked one cell at a time, there are only two moves per state, so the time is `O(R * C * 2^C)`.

**A different scheme: row masks.** Count the 0/1 grids with no two 1s adjacent (sideways or up and down). Here the state is the whole previous row as a mask. A row is allowed if it has no two adjacent 1s (`m & (m >> 1) == 0`), and two consecutive rows are compatible if they share no column (`a & b == 0`). With `ok` the list of allowed masks (the Fibonacci number `F(C + 2)` of them), the transition is a double loop over `ok`. It is simpler than the cell-by-cell form but costs `O(R * |ok|^2)`, which is why large `C` needs the finer scheme.

```cpp
// Profile DP: the grid is filled cell by cell and the state is the PROFILE, a mask of one bit per column describing the cells at the
// frontier.  Counting the ways to tile an R x C board with dominoes (1 x 2, either way).  Cells are visited in row order.  Bit c of the
// mask is 1 when the cell of column c at the frontier (the current row for columns not yet reached, the next row for columns passed) is
// already covered by a domino that sticks out of an earlier cell.  At the cell (r, c): if its bit is 1 it is covered, so clear the bit and
// move on; otherwise place a vertical domino (cover this cell and the one below: set the bit for the next row) or a horizontal one
// (cover this cell and the one to the right: set the next column's bit).  The count after the last cell with the mask 0 is the answer.
ll domino_tilings(int R, int C) {
    if (R <= 0 || C <= 0) return 1; std::vector<ll> cur((size_t)1 << C, 0), nxt((size_t)1 << C); cur[0] = 1;
    for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c) {
        std::fill(nxt.begin(), nxt.end(), 0);
        for (int mask = 0; mask < (1 << C); ++mask) {
            ll v = cur[mask]; if (!v) continue;
            if (mask >> c & 1) nxt[mask ^ (1 << c)] += v;
            else {
                if (r + 1 < R) nxt[mask | 1 << c] += v;
                if (c + 1 < C && !(mask >> (c + 1) & 1)) nxt[mask | 1 << (c + 1)] += v;
            }
        }
        cur.swap(nxt);
    }
    return cur[0];
}
// Row-mask DP: count the 0/1 grids of R rows and C columns with no two 1s next to each other (sideways or up and down).  State = the
// mask of the previous row; a row mask is allowed when it has no two adjacent 1s, and two consecutive rows must not share a column.
ll independent_grids(int R, int C) {
    if (R <= 0 || C <= 0) return 1; std::vector<int> ok; for (int m = 0; m < (1 << C); ++m) if (!(m & (m >> 1))) ok.push_back(m);
    std::vector<ll> f(ok.size(), 1);
    for (int r = 1; r < R; ++r) { std::vector<ll> g(ok.size(), 0); for (size_t a = 0; a < ok.size(); ++a) for (size_t b = 0; b < ok.size(); ++b) if (!(ok[a] & ok[b])) g[b] += f[a]; f.swap(g); }
    ll total = 0; for (ll x : f) total += x; return total;
}
```

```
domino tilings of a 2 by 3 board; the mask has one bit per column (bit c = column c); counts by mask after each cell
  after cell (0,0): 001=1 010=1
  after cell (0,1): 000=1 011=1 101=1
  after cell (0,2): 001=1 100=1 111=1
  after cell (1,0): 000=1 110=2
  after cell (1,1): 100=3
  after cell (1,2): 000=3
tilings=3 (code gives 3)
```

Read the trace row by row: after `(0,0)` there are two profiles, `001` (vertical domino, so column 0 is covered in the next row) and `010` (horizontal, so column 1 is covered). The last line says there are 3 tilings of a 2 by 3 board. A board with an odd number of cells has none: the count ends at 0, and the tests check that.

Measured. Domino tilings: 36 for 4 by 4, 12,988,816 for 8 by 8, 258,584,046,368 for 10 by 10 and 53,060,477,521,960,000 for 12 by 12 (computed in 0.3 / 0.4 ms; the answers fit in 64 bits). Grids with no two adjacent ones: 660,647,962,955 for 8 by 8 and 2,030,049,051,145,980,160 for 10 by 10 (the latter in 0.2 / 0.2 ms).

## 5. Recognising the shape

| Signal in the statement | State | Time and space |
|---|---|---|
| Visit every item once, the cost depends on the previous one | (set visited, last item) | `O(2^n n^2)`, `O(2^n n)` |
| Match each of `n` things to one of `n` others | set of the others used; the position is `popcount` | `O(2^n n)` |
| `n` is tiny but a second dimension (hats) is large | mask over the tiny side, loop over the large side as stages | `O(H * 2^n * n)` |
| Group the items into buckets of fixed size | mask of placed items, plus the fill of the open bucket | `O(2^n n)` |
| Group the items into the fewest bins | mask, with the last bin chosen as a submask | `O(3^n)` |
| Shortest walk or path that must visit all of a few things | BFS over (set visited, node) | `O(2^n n + edges)` |
| Buy and sell with a rule about what you may do next | (day, mode), the machine's edges | `O(n * modes)` |
| Fill a grid with a narrow side, local constraints | (cell, profile mask of the frontier) | `O(R C 2^C)` |

## 6. Testing a bitmask, machine or profile DP

1. **A different method for every table.** Every permutation of the cities, every assignment, a recursion that fills buckets, Floyd-Warshall plus every order for the walk, and a brute force over all action sequences for the stocks. The brute force never uses a mask, so a wrong bit operation cannot hide in both.
2. **Replay the answer.** The tour is replayed: it must start at 0, visit every city once and cost exactly what the table says.
3. **Properties.** Fee 0 equals unlimited; the cooldown answer is at most the unlimited one; at most 1 transaction is at most 2; the line-shaped tour costs twice the span; sorted pairing solves `|a - b|` costs; a one-row grid counts Fibonacci numbers.
4. **Known counts.** 12,988,816 tilings of the 8 by 8 board; hats for ten people who like everything is `40 * 39 * ... * 31`.
5. **Pins** for empty and single-item inputs, a limit of zero, a negative limit, an empty list of numbers with `k = 0`, and a transaction limit bigger than the number of days.
6. **Bit operations in the sanitiser's view.** The tables are indexed by masks; any off-by-one in `1 << n` or a shift by a negative number is reported by AddressSanitizer or UBSan, which is why every run uses both.

## 7. What the tests check, and what they cannot

Of the 69 mutants, 58 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Eleven survive, and each leaves every output the tests can see unchanged. The first run left thirteen survivors; two pins killed two: an empty list of numbers with `k = 0` or `k = -1` (which the partition must call impossible, not possible), and a negative transaction limit for `stock_k` (the sanitizer reports the negative-size table). The reasons the other eleven stay:

- **Replacing a tie by `<=` when relaxing the tour table.** A tie means two equally cheap routes; the mutant keeps the later one, so it may print a different tour of the same cost. The tests check the cost, the validity of the replayed tour and the brute-force minimum, all of which are unchanged.
- **Reading `c[j][i]` instead of `c[i][j]` in the assignment.** The cheapest assignment of the transposed matrix is the same number: matching workers to jobs is the same as matching jobs to workers.
- **Skipping the unreachable test only when the mask is non-zero (`f[mask] >= INF && mask > 0`).** `f[0]` is 0, never infinite, so the extra condition changes nothing.
- **Skipping the divisibility test when `k = 1` (`total % k && k > 1`).** Any total is divisible by 1.
- **Rejecting a number only when it exceeds `target + 1`.** A number equal to `target + 1` can never be placed (`cur + x <= target` fails for every `cur`), so the full mask is unreachable and the answer is still no. The early exit is only a speed-up.
- **Accepting any non-negative amount for the full mask (`>= 0` instead of `== 0`).** When every number is placed the amount is the total modulo `target`, which is 0, so the two tests agree.
- **Enumerating `s = s - 1` without `& mask` in the work-sessions loop.** The extra values of `s` are not subsets of `mask`, but `f[mask ^ s] + 1` for such an `s` is never smaller than `f[mask]`: the set `mask ^ s` contains `mask` minus `s`, `f` can only grow with the set, and the part of `mask` that `s` covers fits in one session. It is the correct answer, found three times slower.
- **Switching to the unlimited answer only when `k > n / 2`.** At `k = n / 2` the table runs instead of the shortcut; both are correct when the limit does not bind.
- **Switching to the unlimited answer only when `k >= n`.** The same reasoning with a later threshold: it changes which correct method runs, not the answer.
- **Updating `sell[j]` before `buy[j]` on the same day.** The only difference is a buy and a sell at the same price on the same day, which gains nothing.
- **Setting the bit for a vertical domino even in the last row.** That bit is never cleared, so the final mask is not 0 and the run is not counted in `cur[0]`.

The large inputs have no brute-force reference; they are checked against independent computations (sorted pairing, the factorial product, the known tiling count, Fibonacci). What the tests cannot do is prove that the state models the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass.

Mutants are chosen where a bug is plausible, not at random, so "58 of 69" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 8. Common mistakes

1. **A mask over the wrong thing:** hats with a mask over 40 hats instead of 10 people; always put the mask on the small side.
2. **`1 << n` overflow** for `n >= 31`; and forgetting the table is `2^n` entries, not `n`.
3. **Forgetting to leave out the start city** (or to add the return edge), so the tour does not close.
4. **Using the worker index in the assignment** instead of `popcount(mask)`.
5. **Mask order in the hats problem:** ascending masks let one hat be given twice; run them downward.
6. **Not wrapping the open bucket** in the partition, so a full group does not start a new one.
7. **Enumerating subsets with `s - 1` and no `& mask`**, which visits numbers that are not subsets.
8. **Counting `3^n` as `2^n`**: submask enumeration over all masks is much slower.
9. **Updating the stock modes in place** so that a buy and a sell on the same day see each other's new value; compute the new values from the old ones.
10. **Charging the fee twice**, or on the buy and on the sell.
11. **Letting a cooldown buy come from `sold`** instead of `rest`.
12. **Allocating `k + 1` cells for a limit of a billion;** cap `k` by `n / 2`.
13. **A vertical domino in the last row,** or a horizontal one across the right edge or into a covered cell.
14. **A profile of `2^C` with `C` large;** put the narrow side in the mask (transpose the board).

## 9. Questions to answer out loud

1. What does the bitmask hold, and why is `2^n` the limit on `n`?
2. Write the Held-Karp recurrence. Why is the start city left out of the mask?
3. Why is the worker not part of the assignment state?
4. In the hats problem, why is the mask over the people and not over the hats? What are the stages?
5. Why must the masks run downward within one hat?
6. In the equal-sum partition, why does one number per mask suffice for the open group?
7. How does `s = (s - 1) & mask` enumerate subsets, and why is the total `3^n`?
8. Why can a shortest walk through all nodes be found by BFS, and what are the states?
9. State the three-mode cooldown machine. Where is the cooldown enforced?
10. Why are the stock updates computed from the old values?
11. How does the at-most-k machine avoid allocating a huge table?
12. What is the profile in domino tiling, and what are the two moves at a free cell?
13. When would you use row masks instead of a cell-by-cell profile?

## 10. Problems

### 10.1 On paper

**A.** Travelling salesman for the cost matrix with rows `0 2 9 10`, `1 0 6 4`, `15 7 0 8`, `6 3 12 0`: the cheapest tour and its cost.

**B.** Assignment: the cheapest total for the costs `4 1 3 / 2 0 5 / 3 2 2`, and for the 4 by 4 matrix `10 19 8 15 / 10 18 7 17 / 13 16 9 14 / 12 19 8 18`.

**C.** Hats: three people who like `{1, 2}`, `{2, 3}`, `{1, 3}`: the number of ways; and three people who all like `{1, 2, 3}`.

**D.** Can `3 3 3 3 6` be split into 3 groups of equal sum? Can `2 2 2 2 3 4 5` be split into 4? The fewest work sessions of length 7 for the tasks `2 3 3 4 5`.

**E.** Stocks: the best profit with at most 2 transactions for `2 1 4 5 2 9 7`; with cooldown for `1 2 4`; with a fee of 1 for `1 5 3 8`; and with unlimited transactions for `1 5 3 8`.

**F.** The number of domino tilings of a 2 by 4 board and of a 3 by 2 board; the number of 2 by 3 grids of 0s and 1s with no two adjacent 1s.

**G.** The shortest walk visiting all nodes of the graph with edges 0-1, 0-2, 1-2, 2-3.

### 10.2 On LeetCode

For each: state what the mask (or machine) holds, the transition, and the fill order before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Shortest Path Visiting All Nodes | 847 | BFS over (mask, node) |
| Partition to K Equal Sum Subsets | 698 | Section 2: the open bucket |
| Minimum Number of Work Sessions to Finish the Tasks | 1986 | Submask enumeration |
| Number of Ways to Wear Different Hats | 1434 | A mask over the small side |
| Best Time to Buy and Sell Stock | 121 | One transaction |
| Best Time to Buy and Sell Stock II | 122 | Unlimited, two modes |
| Best Time to Buy and Sell Stock III | 123 | At most 2 |
| Best Time to Buy and Sell Stock IV | 188 | At most k, the `n / 2` cap |
| Best Time to Buy and Sell Stock with Cooldown | 309 | Three modes |
| Best Time to Buy and Sell Stock with Transaction Fee | 714 | Fee on the sell edge |

### 10.3 In your editor

**A.** Write the assignment with the reconstruction of which worker gets which job, and test it against the permutation search.

**B.** Write the travelling salesman for an open path (no return to the start), starting anywhere, and compare with the closed tour on the same matrix.

**C.** Write the count of Hamiltonian paths in a graph of 18 nodes with a mask of nodes and the last node.

**D.** Write the count of the ways to tile a 3 by `n` board with dominoes with a row-mask recurrence and compare it with the profile code for `n` up to 12.

**E.** Write the stock machine for "at most k transactions with a cooldown" and test it against a brute force.

**F.** Write the profile DP for the grid with no two adjacent ones using the cell-by-cell scheme and compare the counts with the row-mask version.

### Answers

**A.** The cheapest tour costs 21 and visits `0 2 3 1 0` (costs 9, 8, 3 and 1).

**B.** The first costs 5 and the 4 by 4 matrix costs 49.

**C.** The first has 2 ways and the second has 6 (every assignment of three hats to three people).

**D.** `3 3 3 3 6` can be split into 3 groups of 6, so yes. `2 2 2 2 3 4 5` has total 20 and shares of 5, but the 4 would need a 1, so no. The tasks `2 3 3 4 5` need 3 sessions of length 7.

**E.** At most 2 transactions on `2 1 4 5 2 9 7` give 11 (1 to 5, then 2 to 9). The cooldown on `1 2 4` gives 3. The fee of 1 on `1 5 3 8` gives 7. Unlimited on `1 5 3 8` gives 9.

**F.** A 2 by 4 board has 5 tilings and a 3 by 2 board has 3. There are 17 grids of size 2 by 3.

**G.** The shortest walk has 3 edges (for instance 1, 0, 2, 3).

## Cheat sheet

- **Bitmask DP:** the state is a set as an integer; the table has `2^n` rows; use `popcount` for the position and `mask & (mask - 1)` / `ctz` for the lowest member; the limit is about 20 items.
- **Held-Karp:** `f(mask, j) = min f(mask - j, k) + d[k][j]`; the start city is left out; `O(2^n n^2)`.
- **Assignment:** worker = `popcount(mask)`; mask over the jobs.
- **Hats:** mask over the small side (people); the large side (hats) are the stages; masks downward.
- **Open bucket:** one number per mask, the fill of the current bucket; wrap on full.
- **Submasks:** `for (s = mask; s; s = (s - 1) & mask)`; the total is `3^n`.
- **Walk through all nodes:** BFS over (mask, node).
- **Stocks:** unlimited `free' = max(free, hold + p)`, `hold' = max(hold, free - p)`; at most k with `buy[j]`, `sell[j]` and the cap `k >= n / 2`; cooldown with `hold, sold, rest`; the fee once, on the sell.
- **Profile DP:** `(cell, mask of the frontier)`; two moves at a free cell; narrow side in the mask.
- **Shortcuts fail:** see section 3 for how often.
- **Test** with a different method, by replaying the answer, with properties and known counts, and with pins.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Held-Karp, the assignment by subsets, the stock-problem state machines and profile DP are standard; the statements here are my own wording. The count 12,988,816 for the 8 by 8 board is checked by the tests against the recursion, not taken from a table.
- Problem numbers and titles in section 10, and the samples of problems 847, 698, 1986, 1434 and the stock series used in the tests, are LeetCode's, from memory; check a number before you rely on it.
