# DSA 09.03: Greedy Scheduling with Heaps

*Part of the [Greedy and intervals plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Greedy algorithms and their proofs](01_greedy_algorithms_and_proofs.md) (exchange arguments), [Intervals and the sweep line](02_intervals_and_sweep_line.md) (sort by a key, then walk) and [Binary heaps](../07_trees/15_binary_heaps.md) (a priority queue). There is no companion demo file: every structure is listed in this doc

A greedy algorithm needs a way to ask "what is the best candidate right now?" again and again while the set of candidates changes. A sorted array answers once; a **heap** answers after every change in O(log n). That is the whole subject of this document: the heap as the engine of a greedy. Five classic problems use it in three different ways, and then the older theory of **scheduling rules** (shortest job first, earliest deadline, Smith's rule, Johnson's rule) shows which greedy is the right one for which goal. The same lesson as Greedy 01 holds here: a rule is right for one objective and wrong for its neighbour, and the difference is found by a short exchange argument or by a machine.

By the end you can:

- recognise the three heap-greedy patterns: **take the most frequent** (Task Scheduler, Reorganize String), **unlock, then take the best** (IPO, Minimum Refueling Stops) and **take everything, then undo the worst** (Course Schedule III);
- derive the closed form for the Task Scheduler, prove when Reorganize String is impossible, and explain the refuelling problem as "buy the fuel in hindsight";
- run Moore-Hodgson for the most jobs finished on time and prove each step with an exchange argument;
- state which rule minimises which objective: shortest processing time (sum of completion times), Smith's ratio (weighted sum), earliest due date (largest lateness), Moore-Hodgson (number of late jobs), Johnson's rule (two-machine makespan);
- compare ratios without division, in a wide type, and without a comparator that breaks `std::sort`;
- test any scheduling greedy against all permutations on small inputs and against an independent algorithm on large ones.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 33,687 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: the Task Scheduler formula and the heap simulation against a breadth-first search over every schedule (letters, copies left, cooldown left); Reorganize String against all permutations, with the output checked to be a rearrangement with no equal neighbours and the feasibility checked against the bound `max count <= (len + 1) / 2`; IPO against a search over every order of projects; refuelling against every subset of stations; Course Schedule III against every subset, and at n = 1,500 against a table `dp[c] = least total time for c courses`; the largest lateness, the sum of completion times, the weighted sum and the two-machine makespan against all orderings up to 7 jobs; IPO at n = 3,000 against a scan of every project at every step; refuelling at n = 2,500 against the farthest-reach table; Johnson's rule at n = 1,500 against its lower bound and 300 random orders; and pins for the cases random inputs rarely hit (an empty input, a cost exactly equal to the capital, a station exactly at the reach, a target reached exactly, ties for the maximum, and cross products larger than a 64-bit integer). Then 45 hand-made mutants of the listings were run against the tests: **43 were caught and 2 are equivalent changes**; section 8 says which survived and what that means. The tests also found one real defect in my first version of the Task Scheduler formula, described in section 2.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. The heap as a greedy engine

Every problem below has the same loop: keep the candidates in a priority queue; take the best one; let the choice change what is available; repeat. What differs is *where the candidates come from*:

| Pattern | Candidates | Why a heap | Problems |
|---|---|---|---|
| Take the most frequent | The letters, keyed by copies left | The key of one letter changes after each use | Task Scheduler (621), Reorganize String (767) |
| Unlock, then take the best | Sorted by a threshold; they enter the heap once reachable | The set only grows, the best is always wanted | IPO (502), Minimum Refueling Stops (871) |
| Take everything, undo the worst | Everything taken so far | When a limit breaks, remove the worst of what was taken | Course Schedule III (630) |

The third pattern is a form of **regret**: the greedy is allowed to be wrong for a while, because it keeps enough information (the heap) to repair the mistake exactly when it matters. It is the most useful pattern to know by name, because it solves problems that a plain "sort and take" cannot.

## 2. Cooldowns: Task Scheduler and Reorganize String

```cpp
// Task Scheduler: tasks named by letters, the same letter must be at least n slots apart (idle slots allowed). Fewest slots?
// Closed form: the most frequent task forces (mx - 1) full frames of n + 1 slots, then a last partial frame holding every task
// that ties for the maximum. If there are more tasks than that, no idle slot is needed and the answer is just the task count.
ll task_scheduler_formula(const std::string& t, int n) {
    if (t.empty()) return 0;   // otherwise all 26 empty counts "tie" for the maximum
    ll cnt[26] = {0}; for (char c : t) ++cnt[c - 'A'];
    ll mx = *std::max_element(cnt, cnt + 26), ties = 0; for (ll c : cnt) if (c == mx) ++ties;
    return std::max<ll>((ll)t.size(), (mx - 1) * (n + 1) + ties);
}
// The same answer by simulation: in each frame of n + 1 slots run the n + 1 tasks with the most remaining copies (a max-heap of counts).
ll task_scheduler_sim(const std::string& t, int n) {
    std::priority_queue<ll> pq; ll cnt[26] = {0}; for (char c : t) ++cnt[c - 'A']; for (ll c : cnt) if (c) pq.push(c);
    ll time = 0;
    while (!pq.empty()) {
        std::vector<ll> back; int used = 0;
        for (int k = 0; k < n + 1 && !pq.empty(); ++k) { back.push_back(pq.top() - 1); pq.pop(); ++used; }
        for (ll c : back) if (c > 0) pq.push(c);
        time += pq.empty() ? used : n + 1;
    }
    return time;
}
// Reorganize String: rearrange so that no two equal letters are adjacent, or return "". Always place the letter with the most copies
// left, unless it was just placed, in which case place the runner-up. Impossible exactly when one letter has more than (len + 1) / 2 copies.
std::string reorganize(const std::string& s) {
    int cnt[256] = {0}; for (unsigned char c : s) ++cnt[c];
    std::priority_queue<std::pair<int, char>> pq; for (int c = 0; c < 256; ++c) if (cnt[c]) pq.push({cnt[c], (char)c});
    std::string out;
    while (!pq.empty()) {
        auto a = pq.top(); pq.pop();
        if (!out.empty() && out.back() == a.second) {
            if (pq.empty()) return "";
            auto b = pq.top(); pq.pop(); out += b.second; if (--b.first > 0) pq.push(b); pq.push(a);
        } else { out += a.second; if (--a.first > 0) pq.push(a); }
    }
    return out;
}
```

**Task Scheduler.** The most frequent task `A`, with `mx` copies, needs `mx - 1` gaps between its copies, and each gap must hold `n` other slots. So the schedule has `mx - 1` frames of `n + 1` slots (one `A` and `n` others) and then a last partial frame with one `A`, plus one slot for every other task that ties for the maximum. That is `(mx - 1)(n + 1) + ties`. If there are more tasks than that, the others spill into the gaps and then extend the schedule, and no idle slot is ever needed: the answer is the number of tasks. Hence the maximum of the two. The simulation does the same thing a frame at a time by taking the `n + 1` tasks with the most copies left.

The tests found a real mistake in my first version of the formula: for an **empty** string every one of the 26 counts is 0 and all 26 "tie" for the maximum, so the formula returned `(0 - 1)(n + 1) + 26`, which is 22 for `n = 3`, not 0. The `max` with the task count does not rescue it. The guard `if (t.empty()) return 0;` is in the listing and a mutant that removes it is caught.

Trace (checked by running the code):

```
AAABBB n=2: formula=8 simulation=8 tasks=6
AAABBBCC n=2: formula=8 simulation=8 tasks=8
AAAAAABCDEFG n=2: formula=16 simulation=16 tasks=12
ABCDEFG n=5: formula=7 simulation=7 tasks=7
```

**Reorganize String.** Rearrange the letters so that no two equal letters are adjacent, or report that it cannot be done. *Impossible exactly when one letter has more than `(len + 1) / 2` copies.* If a letter has `m` copies and no two are adjacent, they occupy `m` positions with a gap between each pair, which needs `2m - 1 <= len`, that is `m <= (len + 1) / 2`. Conversely, the greedy places the letter with the most copies left unless it was just placed, in which case it places the runner-up. It keeps the invariant "the largest remaining count is at most `(remaining + 1) / 2`", which is what makes the runner-up exist whenever it is needed. The tests check both directions against all permutations.

```
aab -> aba
aaab -> (impossible)
aaabbc -> abacba
vvvlo -> vovlv
aaaabbbcc -> abacbacba
```

## 3. Unlock, then take the best: IPO and refuelling

```cpp
// IPO: with starting capital w you may finish at most k projects; a project needs capital cap[i] to start and adds profit[i].
// Greedy: among the projects you can afford, always take the most profitable. Sort by capital needed; as capital grows, move newly
// affordable projects into a max-heap of profits. (Profits are not negative, so taking one never hurts.)
ll max_capital(int k, ll w, const std::vector<ll>& profit, const std::vector<ll>& cap) {
    int n = (int)profit.size(); std::vector<int> idx(n); std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return cap[a] < cap[b]; });
    std::priority_queue<ll> pq; int i = 0;
    for (int r = 0; r < k; ++r) {
        while (i < n && cap[idx[i]] <= w) pq.push(profit[idx[i++]]);
        if (pq.empty()) break;
        w += pq.top(); pq.pop();
    }
    return w;
}
// Minimum Number of Refueling Stops: a car starts with fuel and one unit of fuel buys one unit of distance. Stations (position, fuel).
// Drive as far as possible; every station passed is a refill you could have taken later. When stuck, take the largest refill among
// the stations already passed (a max-heap). Returns -1 if the target cannot be reached.
int min_refuels(ll target, ll fuel, std::vector<std::pair<ll, ll>> st) {
    std::sort(st.begin(), st.end());
    std::priority_queue<ll> pq; size_t i = 0; ll reach = fuel; int stops = 0;
    while (reach < target) {
        while (i < st.size() && st[i].first <= reach) pq.push(st[i++].second);
        if (pq.empty()) return -1;
        reach += pq.top(); pq.pop(); ++stops;
    }
    return stops;
}
```

**IPO.** You may finish at most `k` projects. A project costs capital to *start* (`cap`) and adds `profit`; you begin with `w`. Sort the projects by the capital they require. Walk the sorted list while the capital allows and push each profit into a max-heap. Take the largest, add it to the capital (which may unlock more projects), and repeat. *Why the most profitable affordable project is a safe choice:* profits are not negative, so a larger capital never hurts, and any set of `k` projects that some order can finish can be reordered to take the best affordable one first without losing anything (exchange: swap it with whichever project the other order takes at that step; the capital after the step is at least as large, so every later project that was affordable still is).

```
profits 1 2 3, capital needed 0 1 1, start 0, k=1: final capital 1
profits 1 2 3, capital needed 0 1 1, start 0, k=2: final capital 4
profits 1 2 3, capital needed 0 1 1, start 0, k=3: final capital 6
profits 5 4 100, capital needed 0 0 6, start 0, k=2: 9; k=3: 109
```

**Minimum Refueling Stops.** The car starts with some fuel; one unit of fuel is one unit of distance; stations sit along the road. Drive as far as you can. Every station you *pass* is a refill you could have bought, but you do not need to decide yet. When the fuel runs out before the target, **buy the biggest refill among the stations already passed**: that is the regret step, and a max-heap holds the candidates. If the heap is empty you are stuck: `-1`. The count of stops is the number of times you had to reach into the heap, and the greedy reaches in only when forced, so it reaches in as few times as possible (stays-ahead: after `j` refills the greedy's reach is at least that of any strategy with `j` refills, because it always takes the largest available).

```
target 100, fuel 10, stations (pos,fuel) (10,60) (20,30) (30,30) (60,40): stops=2
target 100, fuel 1, station (10,100): stops=-1
target 100, fuel 50, station (50,50): stops=1
```

Two boundary cases are pinned in the tests: a station exactly at the current reach is usable (`<=`), and a target exactly at the reach needs no further stop (the loop runs while `reach < target`). With the heap both problems take O(n log n). Against the naive version, IPO with n = 20,000 and k = 10,000 took 2.33 / 2.90 ms with the heap and 707.6 / 625.0 ms scanning every project at every step, and the two reached the same capital (753,843). Refuelling with n = 5,000 stations took 0.37 / 0.39 ms with the heap against 10.5 / 10.5 ms for the O(n^2) table, and both gave 528 stops.

## 4. Take everything, undo the worst: Course Schedule III

```cpp
// A job has a duration and a due time (the course's last day). Jobs run one at a time from time 0 without gaps.
struct Job { ll dur, due; };
// Course Schedule III (Moore-Hodgson): most jobs that can finish by their due time. Sort by due time; add each job; if the total
// time now passes this job's due time, throw away the LONGEST job taken so far (a max-heap of durations). The count never needs
// a second look: the removed job might be the one just added.
int max_on_time(std::vector<Job> v) {
    std::sort(v.begin(), v.end(), [](const Job& a, const Job& b) { return a.due < b.due; });
    std::priority_queue<ll> pq; ll t = 0;
    for (const Job& j : v) { t += j.dur; pq.push(j.dur); if (t > j.due) { t -= pq.top(); pq.pop(); } }
    return (int)pq.size();
}
// Smallest possible largest lateness (finish minus due) over all orders: earliest due date first (Jackson's rule).
// The answer is negative when every job can finish early.
ll min_max_lateness(std::vector<Job> v) {
    std::sort(v.begin(), v.end(), [](const Job& a, const Job& b) { return a.due < b.due; });
    ll t = 0, worst = LLONG_MIN; for (const Job& j : v) { t += j.dur; worst = std::max(worst, t - j.due); }
    return v.empty() ? 0 : worst;
}
```

Courses have a duration and a last day (a due time). You take one course at a time, starting at time 0, and a course counts only if it finishes by its last day. Find the most courses. This is **Moore-Hodgson**.

Sort by due time and add the courses one by one, keeping the running total time `t` and a max-heap of the durations taken. If after adding a course `t` exceeds its due time, drop the **longest** course taken so far (which may be the one just added). Why this is right: by induction, after the first `i` courses the heap holds a feasible set of the maximum possible size that has the *smallest total time* among all such sets. Adding course `i + 1` either keeps it feasible (size one larger, time larger) or breaks it by exceeding the due time, in which case dropping the longest course restores feasibility, keeps the size the same as the best before the addition, and reduces the time as much as any single removal can. Removing the longest is the removal that leaves the most room for later courses, which is the exchange argument. The tests agree with every subset for n up to 9 and with an independent table at n = 1,500; this is evidence for the induction, not a replacement for it.

One removal is always enough: before the addition the total was within the previous due time, which is no larger than this one, so after dropping the longest course (at least as long as the added one, or the added one itself) the total is back under it. A mutant that loops with `while` instead of `if` is accordingly equivalent.

```
courses (dur,due) (100,200) (200,1300) (1000,1250) (2000,3200): on time=3
courses (5,5) (4,6) (2,6): on time=2
```

**Earliest due date first.** A different goal: minimise the largest *lateness* (finish time minus due time, which may be negative) over all jobs, all of which must run. Sort by due time (Jackson's rule). Exchange argument: take two adjacent jobs `a` then `b` with `due_a > due_b`. Both finish within the same window ending at `T`. Before the swap `b` finishes at `T` with lateness `T - due_b`; after swapping, `a` finishes at `T` with lateness `T - due_a`, which is smaller, and `b` finishes earlier. The largest of the two does not grow, so every inversion can be removed. Notice the trap: **the same sort by due time is wrong for the number of late jobs** (Moore-Hodgson is needed) and **wrong for the sum of completion times** (shortest first is needed). Section 5 puts numbers on how wrong.

```
jobs (dur,due) (3,6) (2,8) (1,9) (4,9) (3,14) (2,15): smallest largest lateness=1
sum of completion times, durations 3 1 2: given order 13, shortest first 10
```

## 5. Scheduling rules and which goal they serve

```cpp
// Sum of completion times (one machine, all jobs ready at time 0): shortest processing time first.
ll sum_completion_spt(std::vector<ll> p) {
    std::sort(p.begin(), p.end()); ll t = 0, total = 0; for (ll x : p) { t += x; total += t; } return total;
}
// Weighted sum of completion times: Smith's rule, order by processing time divided by weight, smallest first.
// Compared by cross-multiplication in 128 bits, never by division. A job is (processing time p, weight w) with w > 0.
struct WJob { ll p, w; };
ll weighted_completion_smith(std::vector<WJob> v) {
    std::sort(v.begin(), v.end(), [](const WJob& a, const WJob& b) { return (__int128)a.p * b.w < (__int128)b.p * a.w; });
    ll t = 0, total = 0; for (const WJob& j : v) { t += j.p; total += j.w * t; } return total;
}
// Two-machine flow shop: every job needs machine 1 for a, then machine 2 for b. Makespan of a given order.
struct Op { ll a, b; };
ll flow_makespan(const std::vector<Op>& v, const std::vector<int>& order) {
    ll t1 = 0, t2 = 0; for (int i : order) { t1 += v[i].a; t2 = std::max(t2, t1) + v[i].b; } return t2;
}
// Johnson's rule: jobs with a <= b first, by a ascending; then jobs with a > b, by b descending.
std::vector<int> johnson_order(const std::vector<Op>& v) {
    std::vector<int> first, second;
    for (int i = 0; i < (int)v.size(); ++i) (v[i].a <= v[i].b ? first : second).push_back(i);
    std::sort(first.begin(), first.end(), [&](int x, int y) { return v[x].a < v[y].a; });
    std::sort(second.begin(), second.end(), [&](int x, int y) { return v[x].b > v[y].b; });
    first.insert(first.end(), second.begin(), second.end()); return first;
}
```

| Objective (one machine unless stated) | Rule | Cost of the rule |
|---|---|---|
| Sum of completion times | Shortest processing time first (SPT) | O(n log n) |
| Weighted sum of completion times | Smith's rule: smallest `p / w` first | O(n log n) |
| Largest lateness | Earliest due date first | O(n log n) |
| Number of late jobs | Moore-Hodgson (sort by due, drop the longest) | O(n log n) with a heap |
| Makespan on two machines in series | Johnson's rule | O(n log n) |

**SPT.** Swap two adjacent jobs `a` then `b` starting at `t`: the sum for the pair is `2t + 2p_a + p_b`; reversed it is `2t + 2p_b + p_a`. The difference is `p_a - p_b`, so putting the shorter first never hurts, and removing every inversion sorts the jobs by `p`.

**Smith's rule.** The same swap with weights: `a` first costs `w_a(t + p_a) + w_b(t + p_a + p_b)` and `b` first costs `w_b(t + p_b) + w_a(t + p_b + p_a)`. The difference is `w_b p_a - w_a p_b`, so `a` first is no worse exactly when `p_a / w_a <= p_b / w_b`. Compare by cross-multiplication in 128 bits: with `p` up to 6 billion and `w` up to 6 billion the product is 3.6e19, more than a signed 64-bit integer holds, and the comparison overflowed to the wrong order in one of the mutants. Division would hide ties and lose precision. The comparator is a strict weak order on positive weights (a tie in the ratio means neither is smaller).

**Johnson's rule.** Every job needs machine 1 for `a`, then machine 2 for `b`; the makespan is when the last job leaves machine 2. Put the jobs with `a <= b` first in order of increasing `a`, then the jobs with `a > b` in order of decreasing `b`. The idea is to let machine 2 start early (short first `a`) and never starve it, and to end with short `b` (so machine 2 finishes soon after machine 1). The standard exchange argument shows that `i` before `j` is no worse when `min(a_i, b_j) <= min(a_j, b_i)`, which the rule implements (the proof is from memory, as is the 1954 date; the tests are exhaustive up to 7 jobs and compare to a bound beyond that).

```
jobs (p,w) (3,1) (1,3): weighted completion by Smith's rule=7
two machines, jobs (a,b) (3,6) (7,2) (4,7) (5,3) (1,4): Johnson order 5 1 3 4 2, makespan=23
```

How often does the wrong rule lose? On 100,000 random instances of 8 jobs (duration 1 to 8, due time 1 to 25), the number of jobs on time was below the Moore-Hodgson optimum for the natural "sort by due and skip any job that does not fit" in 12,088 cases (12.1%), and for "shortest first and skip" in 57,889 (57.9%). On 100,000 random instances of 7 jobs, sorting by shortest first gave a larger largest-lateness than Jackson's rule in 96,642 (96.6%), and sorting by due date gave a larger sum of completion times than SPT in 99,858 (99.9%). For the two-machine problem, on 100,000 random instances of 7 jobs (times 1 to 20) ordering by machine-1 time ascending lost to Johnson's rule in 56,831 (56.8%) and by machine-2 time descending in 56,862 (56.9%), while Johnson's rule matched the best of all 5,040 orders in every instance but 0 (that is, in all of them). At n = 100,000 jobs Johnson's rule gave a makespan of 5,047,874, exactly the lower bound `sum of a + min b` = 5,047,874 (a shuffled order gave 5,053,529, only slightly worse: with many jobs almost any order keeps both machines busy, so the rule matters most for few jobs).

At scale, one machine: Course Schedule III on 1,000,000 random courses took 114 / 114 ms and kept 884,309; the Task Scheduler on 10,000,000 random tasks took 6.2 / 5.6 ms with the formula and 217.9 / 361.9 ms with the heap simulation (the same answer, 10,000,000, which is the number of tasks: with 26 kinds and a gap of 3 no idle slot is needed); Reorganize String on 1,000,000 letters took 10 / 10 ms.

## 6. Recognising and choosing

| Signal in the problem | Tool | Note |
|---|---|---|
| "At least n apart", "no two equal adjacent" | Frequency heap, or the closed form | Impossible when the top count exceeds `(len + 1) / 2` |
| A resource that grows as you act, "pick the best you can afford" | Sort by threshold, heap of the best | Pick-ups never hurt |
| Fuel, budget or time that runs out; a choice you could have made earlier | Heap of passed options, buy the best when stuck | The regret pattern |
| Jobs with deadlines, count how many finish | Sort by due, heap of durations, drop the longest | Moore-Hodgson |
| Jobs with deadlines, all must run, minimise the worst lateness | Sort by due | Jackson's rule |
| Minimise the sum of finish times | Shortest first; weighted, smallest `p / w` | Smith's rule, cross-multiply |
| Two stages in series, minimise total time | Johnson's rule | Few jobs: the choice matters |
| A value per job and a deadline (weighted count) | Not this greedy | Dynamic programming, or a different exchange argument |

## 7. Testing a scheduling greedy

Every rule in this document was first written, then run against **every ordering** of up to seven jobs; that is how the traps in section 5 were seen. For a new rule: write the objective as a function of the order, enumerate all orders with `next_permutation` for n <= 7, compare, and shrink a failure to the smallest instance. For large inputs compare with an independent algorithm (a table, a scan) that shares no code with the greedy.

## 8. What the tests check, and what they cannot

Of the 45 mutants, 43 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Two survive, and each leaves every output the tests can see unchanged. The first run of this mutant set had no other survivors, because the pins written beforehand (an empty input, a cost exactly equal to the capital, a station exactly at the reach, a target reached exactly, cross products beyond 64 bits) already covered the boundaries. The reasons the other two stay:

- **A `while` instead of an `if` when the total passes the due time in Moore-Hodgson.** One removal always restores feasibility (the total before the addition was within a due time no larger than this one, and the longest course is at least as long as the one added), so the second pass of the loop never runs.
- **`<` instead of `<=` when splitting the flow-shop jobs into the two groups.** A job with `a == b` may go into either group; both orders give the same makespan, which is what the tests compare (they compare the makespan, not the order).

The large inputs have no brute-force reference; they are checked against independent algorithms (a table of least total times, a scan of all projects, the farthest-reach table, lower bounds, and random orders). What the tests cannot do is prove a rule: they show it agreed with exhaustive search on small instances, which supports the exchange arguments in sections 4 and 5 and does not replace them.

Mutants are chosen where a bug is plausible, not at random, so "43 of 45" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **The formula on an empty input.** All counts tie at 0; guard it.
2. **Forgetting the last partial frame** in the Task Scheduler: the ties for the maximum each need one slot.
3. **Forgetting `max(len, formula)`.** With many kinds of task no idle slot is needed.
4. **Placing the same letter twice** in Reorganize String because the top of the heap was not checked against the last letter placed.
5. **Pushing newly affordable projects with `<` instead of `<=`.** A project that costs exactly the capital is affordable.
6. **Skipping the heap-empty check** in IPO and refuelling. Popping an empty heap is undefined behaviour, and the correct result is to stop, or `-1`.
7. **Treating a station exactly at the reach as out of range.**
8. **Stopping refuelling when `reach == target` one stop late,** by writing `<=` in the loop.
9. **Sorting courses by duration or by start** instead of by due time.
10. **Popping the job just added instead of the longest** in Moore-Hodgson; the heap must hold every duration taken.
11. **Using one rule for every objective:** earliest due date minimises the largest lateness, not the number of late jobs, and not the sum of completion times.
12. **Comparing `p / w` with division or in 64 bits.** Cross-multiply in 128 bits.
13. **A Johnson sort that puts `a <= b` jobs in the wrong direction.** The first group goes by `a` ascending, the second by `b` descending.
14. **Starting the largest lateness at 0,** which turns a negative answer into 0 when every job is early.

## 10. Questions to answer out loud

1. Name the three heap-greedy patterns and one problem for each.
2. Derive the Task Scheduler formula, and say when the answer is just the task count.
3. Prove that Reorganize String is impossible exactly when the top count exceeds `(len + 1) / 2`.
4. Why is the most profitable affordable project a safe choice in IPO?
5. Explain Minimum Refueling Stops as "buy the fuel in hindsight". Why is the number of stops minimal?
6. Walk through Moore-Hodgson on a small example. Why does dropping the longest course restore feasibility with one removal?
7. State the exchange argument for earliest due date first and the objective it serves.
8. Why is the same sort wrong for the number of late jobs?
9. Prove SPT and Smith's rule by swapping adjacent jobs. What happens when the weights are 0?
10. State Johnson's rule and the intuition for each of its two groups.
11. Why is cross-multiplication in a wide type the right comparison for ratios?
12. How would you test a scheduling rule you cannot prove?

## 11. Problems

### 11.1 On paper

**A.** Task Scheduler: `AAAABBBCC` with `n = 2`; `AABBCC` with `n = 0`; `AAAAAABBCDE` with `n = 3`.

**B.** Can `aabbcc` be rearranged so that no two equal letters are adjacent? `aaaabc`? `aaabc`?

**C.** IPO: start capital 1, profits `2, 3, 10`, capital needed `1, 2, 5`. Final capital for `k = 2` and for `k = 3`.

**D.** Refuelling: target 60, start fuel 20, stations (position, fuel) `(10, 10), (30, 25), (40, 30)`. Fewest stops? Then with start fuel 15.

**E.** Course Schedule III with (duration, last day) `(3, 4), (2, 5), (4, 8), (1, 9)`. Show the total time and the heap after each course.

**F.** Smallest possible largest lateness for jobs (duration, due) `(4, 5), (2, 3), (3, 10)`.

**G.** Sum of completion times for durations `4, 2, 6` in the best order; weighted sum for (p, w) `(2, 1), (4, 4)`; Johnson's order and makespan for (a, b) `(2, 5), (6, 3), (4, 4)`.

### 11.2 On LeetCode

For each: which pattern, and which heap holds what? Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Task Scheduler | 621 | Formula and frame simulation (section 2) |
| Reorganize String | 767 | Frequency heap with the runner-up rule |
| IPO | 502 | Sort by capital, heap of profits |
| Course Schedule III | 630 | Sort by due, heap of durations, drop the longest |
| Minimum Number of Refueling Stops | 871 | Heap of passed refills |

### 11.3 In your editor

**A.** Write a scheduler simulator for a stream of jobs (arrival, duration, due) that compares shortest-job-first, earliest-deadline-first and first-come-first-served on mean completion time, largest lateness and number of late jobs; show that each wins on exactly one objective.

**B.** Extend Task Scheduler to print an actual schedule with idle slots, and check by a validator that no letter repeats within `n` slots.

**C.** Write Reorganize String for a distance `k` (equal letters at least `k` apart) with a heap and a queue of letters cooling down; check against a brute force for small inputs.

**D.** Implement the weighted version of Course Schedule III (each course has a value) and show with a brute force that the Moore-Hodgson rule fails for it.

**E.** Write the Johnson flow-shop checker: generate random instances of up to 8 jobs, enumerate all orders, and confirm the rule; then try a three-machine instance and find one where the natural extension fails.

**F.** Time the heap and the table versions of refuelling on your machine at n = 10,000 and 100,000 and compare with section 3.

### Answers

**A.** `AAAABBBCC`, `n = 2`: `mx = 4`, ties = 1, so `(4 - 1)(3) + 1 = 10`, and there are only 9 tasks, so the answer is 10 (the code returns 10). `AABBCC`, `n = 0`: no cooldown, 6 tasks, answer 6. `AAAAAABBCDE`, `n = 3`: `mx = 6`, ties = 1, `(6 - 1)(4) + 1 = 21`, against 11 tasks: 21.

**B.** `aabbcc`: the top count is 2, the bound is `(6 + 1) / 2 = 3`: possible. `aaaabc`: the top count is 4, above 3: impossible (the code returns the empty string). `aaabc`: the top count is 3 and the bound is `(5 + 1) / 2 = 3`: possible.

**C.** With capital 1 only the project costing 1 is affordable: take profit 2, capital 3. Now the project costing 2 is affordable: take profit 3, capital 6, which is the answer for `k = 2`. With `k = 3` the project costing 5 is affordable at capital 6: take 10, capital 16.

**D.** Fuel 20 reaches 20; passed station `(10, 10)`. Stuck short of 60: buy 10, reach 30, one stop; the station at 30 is now in range: buy 25, reach 55, two stops; station 40 was passed: buy 30, reach 85, three stops. The answer is 3. With fuel 15 you reach 15, buy 10 at station 10, reach 25; the next station is at 30, out of reach, heap empty: `-1`.

**E.** Sorted by due: `(3, 4)`: total 3, heap `{3}`. `(2, 5)`: total 5, within 5, heap `{3, 2}`. `(4, 8)`: total 9 exceeds 8, drop the longest (4, the course just added): total 5, heap `{3, 2}`. `(1, 9)`: total 6, within 9, heap `{3, 2, 1}`. Three courses on time.

**F.** Sorted by due: `(2, 3)` finishes at 2 (lateness -1), `(4, 5)` at 6 (lateness 1), `(3, 10)` at 9 (lateness -1). The largest lateness is 1.

**G.** Shortest first: finish times 2, 6, 12, sum 20. Smith's rule: ratios `p / w` are 2 and 1, so `(4, 4)` first: `4 * 4 + 1 * 6 = 22`. Johnson: the jobs with `a <= b` are `(2, 5)` and `(4, 4)` in order of `a`, then `(6, 3)`: order 1, 3, 2. Machine 1 finishes at 2, 6, 12; machine 2 finishes at 7, 11, 15. The makespan is 15.

## Cheat sheet

- **Heap-greedy patterns:** most frequent (621, 767); unlock then best (502, 871); take everything then drop the worst (630).
- **Task Scheduler:** `max(len, (mx - 1)(n + 1) + ties)`, with 0 for empty input. **Reorganize:** impossible iff top count > `(len + 1) / 2`.
- **IPO:** sort by capital, push when `cap <= w`, take the largest. **Refuel:** push passed refills, buy the largest when `reach < target`; `-1` if the heap is empty.
- **Moore-Hodgson:** sort by due, add, if over the due time drop the longest taken.
- **Rules:** SPT for the sum of completion times; Smith's `p / w` (cross-multiply in 128 bits) for the weighted sum; earliest due date for the largest lateness; Johnson for two machines (`a <= b` ascending by `a`, then the rest descending by `b`).
- **The wrong rule loses often:** earliest due date has a larger sum of completion times than SPT in 99.9% of random 7-job instances.
- **Test against all permutations** up to 7 jobs, then against an independent algorithm.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Shortest processing time first, Smith's ratio rule, Jackson's earliest due date rule, the Moore-Hodgson algorithm and Johnson's two-machine rule (1954) are classical results of scheduling theory; the statements and exchange arguments here are my own wording, the proof of Johnson's rule and the dates are from memory, and the Moore-Hodgson induction is a sketch.
- Problem numbers and titles in section 11, and the samples of problems 621, 767, 502, 630 and 871, are LeetCode's, from memory; check a number before you rely on it. The samples were run through the listings and give the stated results.
