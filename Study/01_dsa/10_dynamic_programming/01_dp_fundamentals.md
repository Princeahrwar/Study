# DSA 10.01: Dynamic Programming Fundamentals

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Recursion fundamentals](../06_recursion_and_backtracking/01_recursion_fundamentals.md) (recursion, base cases, the call stack) and [Hash tables](../02_hashing/03_design_a_hash_map.md) (for the memo map in section 6). There is no companion demo file: every structure is listed in this doc

**Dynamic programming** (DP) is recursion that refuses to repeat itself. Many recursive definitions ask the same smaller question over and over; DP answers each distinct question **once**, stores the answer, and reuses it. That single change turns exponential time into polynomial time for a large family of problems: counting paths, the best subset, the longest common subsequence, the cheapest tour. The code is usually short. The work is in deciding what a "question" is (the **state**), how it depends on smaller questions (the **transition**), and in what order to answer them.

This first tutorial is the method itself, on problems small enough to see everything: Fibonacci (the whole idea with nothing else in the way), the house robber, grid paths, and three problems where choosing the state is the difficulty. Later tutorials apply the method to families of problems.

By the end you can:

- recognise the two properties that make DP work (**overlapping subproblems** and **optimal substructure**) and say how many subproblems a problem has and how much work each costs, which gives the running time;
- write the same DP three ways: **top-down with memoisation**, **bottom-up with a table**, and **bottom-up with a rolling window**, and say what each costs in time, memory and stack;
- follow the five steps (state, transition, base cases, order, answer) for a new problem;
- decide when the space can be reduced, and when it cannot because the answer needs the choices (reconstruction);
- design the state so that it contains everything the future depends on, and notice when it does not;
- use a hash map for a sparse state space, and know the stack-depth limit of recursive memoisation;
- test a DP against brute force and against its own other forms.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 58,194 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: Fibonacci in all four forms for every n from 0 to 92 (the largest that fits in 64 bits), with the number of calls checked exactly (`2 F(n+1) - 1` for the plain recursion, `2n - 1` with the memo); the house robber in its memoised, tabulated and rolling forms, and the reconstructed set of houses, against every subset for up to 13 houses; the circular version against every subset with the wrap-around neighbour rule; grid paths (memoised and rolling) against plain recursion on random grids with obstacles, and at 1000 by 1000 against the binomial coefficient computed from factorials modulo the prime; the cheapest path and its recovered route against every path, with the route checked to be a valid chain of right and down moves whose cost equals the answer; the longest increasing subsequence against every subset; fewest coins against a search over how many of each coin to use; Collatz step counts against direct computation for 1 to 2,999, and the longest chain below one million (start 837799, 524 steps); and pins for the cases random inputs rarely hit (empty input, a single element, a zero-valued house, a cache that is not preset, a blocked start or end cell, an amount that cannot be made). Then 48 hand-made mutants of the listings were run against the tests: **41 were caught and 7 are equivalent changes**; section 8 says which survived and what that means. The tests also caught a real overflow in my first rolling Fibonacci (it computed one value too many and overflowed at n = 92), described in section 2.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. What makes a problem a DP

Two properties.

1. **Overlapping subproblems.** The recursion asks the same question many times. If every question were different, memoising would store answers that are never read, and plain recursion would be as good (that is divide and conquer, as in merge sort).
2. **Optimal substructure.** The answer to a question can be built from the answers to smaller questions. For an optimisation, "the best overall solution contains a best solution of the subproblem". For counting, "the count for the whole is a sum or product of counts for the parts".

The cost of a DP is simple to state:

```
running time = (number of distinct states) x (work to compute one state from smaller ones)
```

Think of the states as the nodes of a graph, with an edge from each state to the smaller states it reads. For the recursion to be well defined this graph must have **no cycles** (it is a DAG); the **order** in which a table is filled is a topological order of that graph. When a problem seems to have a cyclic dependency, either the state is wrong (it needs one more piece of information, section 5) or the problem is a shortest-path problem on a general graph, which is not DP.

## 2. Fibonacci: the whole idea

`F(0) = 0`, `F(1) = 1`, `F(n) = F(n-1) + F(n-2)`. The recursion is a direct translation of the definition, and it is exponential.

```cpp
// Fibonacci: F(0) = 0, F(1) = 1, F(n) = F(n-1) + F(n-2). The same recurrence four ways. `calls` counts how often the function is entered.
ll fib_naive(int n, ll& calls) {                     // exponential: the same F(k) is recomputed many times
    ++calls; if (n < 2) return n;
    return fib_naive(n - 1, calls) + fib_naive(n - 2, calls);
}
ll fib_memo(int n, std::vector<ll>& memo, ll& calls) {   // top-down: the recursion as written, plus a cache (-1 = not computed yet)
    ++calls; if (n < 2) return n;
    if (memo[n] >= 0) return memo[n];
    return memo[n] = fib_memo(n - 1, memo, calls) + fib_memo(n - 2, memo, calls);
}
ll fib_table(int n) {                                // bottom-up: fill the table in the order the dependencies allow
    if (n < 2) return n;
    std::vector<ll> f(n + 1); f[0] = 0; f[1] = 1;
    for (int i = 2; i <= n; ++i) f[i] = f[i - 1] + f[i - 2];
    return f[n];
}
ll fib_rolling(int n) {                              // F(i) needs only the last two values: O(1) space
    if (n < 2) return n;
    ll a = 0, b = 1; for (int i = 2; i <= n; ++i) { ll c = a + b; a = b; b = c; }   // after the step, b = F(i)
    return b;
}
```

The calls form a tree, and `F(n-2)` is computed inside `F(n-1)` as well as on its own, so the same `F(k)` appears many times. The number of calls satisfies `calls(n) = 1 + calls(n-1) + calls(n-2)`, which gives exactly `2 F(n+1) - 1` (the tests check this for every n up to 27). With the memo the count collapses: each state is *computed* once, so there are `n + 1` states, each entered at most twice (once for the computation and once from the other branch), and `calls = 2n - 1` (also checked exactly for every n).

Trace (checked by running the code):

```
n=5 F(n)=5 naive calls=15 memo calls=9
n=10 F(n)=55 naive calls=177 memo calls=19
n=20 F(n)=6765 naive calls=21891 memo calls=39
n=30 F(n)=832040 naive calls=2692537 memo calls=59
```

Measured on larger inputs: the plain recursion made 2,692,537 calls for n = 30, 29,860,703 for n = 35 and 331,160,281 for n = 40, a factor of about 11 for every 5 steps up, and `F(40) = 102,334,155` took 197.7 / 190.3 ms. The memoised, tabulated and rolling forms compute `F(90) = 2,880,067,194,370,816,120` in 517 / 346 ns, 59 / 58 ns and 40 / 40 ns per call (the memoised time includes allocating its cache each call, which is why it is slowest of the three).

Three forms, one recurrence:

- **Top-down (memoisation).** Keep the recursion exactly as defined; before computing, look in the cache; after computing, store. It is the quickest way to get a correct DP, and it only computes the states that are actually needed.
- **Bottom-up (tabulation).** Fill a table from the base cases upward in an order that guarantees every dependency is ready. No recursion, no stack, usually faster.
- **Rolling window.** If `F(i)` reads only the last two entries, keep only those: O(1) space.

The tests found a real defect in my first rolling version. It looped `n` times and returned `a`, which computes one value past `F(n)`. For `n = 92` the extra value `F(93)` does not fit in 64 bits, and the sanitizer reported a signed overflow. The version in the listing stops at `F(n)` and returns `b`. The lesson: the rolling form has its own boundary behaviour and needs its own tests, not only an agreement with the table on small n.

## 3. The five steps, and the house robber

Every DP is written by answering five questions in order:

1. **State.** What is a subproblem, in words, with its parameters?
2. **Transition.** How is a state computed from smaller states?
3. **Base cases.** Which states are known without recursion?
4. **Order.** In what order can the states be computed so that dependencies come first?
5. **Answer.** Which state is the whole problem?

*House robber.* Houses in a row with values `v[i]`; you may not rob two neighbours; maximise the total. **State** `f(i)` = the best total using only the first `i` houses. **Transition:** either skip house `i - 1` (take `f(i-1)`) or rob it (take `f(i-2) + v[i-1]`, since its neighbour is excluded): `f(i) = max(f(i-1), f(i-2) + v[i-1])`. **Base** `f(0) = 0`, `f(1) = v[0]`. **Order** increasing `i`. **Answer** `f(n)`.

```cpp
// The five steps of a DP: (1) STATE: what a subproblem is; (2) TRANSITION: how it is built from smaller ones; (3) BASE cases;
// (4) ORDER: smaller before larger; (5) ANSWER: which state is the whole problem. Example: House Robber.
// Houses in a row with values v[i]; robbing two neighbours is not allowed; maximise the total.
// State f(i) = best total using only houses 0..i-1.  Transition f(i) = max(f(i-1), f(i-2) + v[i-1]).  Base f(0) = 0, f(1) = v[0].
ll rob_memo(int i, const std::vector<ll>& v, std::vector<ll>& memo) {     // top-down
    if (i <= 0) return 0;
    if (i == 1) return v[0];
    if (memo[i] >= 0) return memo[i];
    return memo[i] = std::max(rob_memo(i - 1, v, memo), rob_memo(i - 2, v, memo) + v[i - 1]);
}
ll rob_table(const std::vector<ll>& v) {                                  // bottom-up with the whole table
    int n = (int)v.size(); if (n == 0) return 0;
    std::vector<ll> f(n + 1, 0); f[1] = v[0];
    for (int i = 2; i <= n; ++i) f[i] = std::max(f[i - 1], f[i - 2] + v[i - 1]);
    return f[n];
}
ll rob_rolling(const std::vector<ll>& v) {                                // bottom-up, two variables
    ll prev2 = 0, prev1 = 0;                                              // f(i-2), f(i-1)
    for (ll x : v) { ll cur = std::max(prev1, prev2 + x); prev2 = prev1; prev1 = cur; }
    return prev1;
}
// Which houses? The rolling form throws the table away, so the choice cannot be recovered. Keep the table and walk it backwards:
// at f(i), if f(i) == f(i-1) house i-1 was skipped, otherwise it was robbed and the walk continues at i-2.
std::vector<int> rob_choice(const std::vector<ll>& v) {
    int n = (int)v.size(); std::vector<ll> f(n + 1, 0); if (n) f[1] = v[0];
    for (int i = 2; i <= n; ++i) f[i] = std::max(f[i - 1], f[i - 2] + v[i - 1]);
    std::vector<int> pick;
    for (int i = n; i >= 1;) { if (i >= 2 && f[i] == f[i - 1]) --i; else { pick.push_back(i - 1); i -= 2; } }
    std::reverse(pick.begin(), pick.end()); return pick;
}
```

```
values 2 7 9 3 1
f(0)=0 f(1)=2 f(2)=7 f(3)=11 f(4)=11 f(5)=12
robbed houses (0-based): 0 2 4
```

The same recurrence in three forms, all agreeing with a search over every subset in the tests. At n = 10,000,000 houses the table took 54.5 / 52.2 ms and 80 MB, and the rolling form 16.9 / 15.6 ms and 16 bytes, with the same answer. The plain recursion without a memo made 2,692,537 calls for only 30 houses, for 31 distinct states: it is Fibonacci again.

**Reducing space** is a question about the transition: which earlier states does `f(i)` read? Here only `i-1` and `i-2`, so two variables suffice. For the grid in the next section a state reads the one above and the one to the left, so one row suffices. For the longest common subsequence it will be the previous row. A table that is read far back (for example `f(i)` reading `f(i/2)`) cannot be rolled.

**But the answer may need the choices.** The rolling form returns the best *value* and has thrown away how it was obtained. To recover *which* houses, keep the table and walk it backwards: at `f(i)`, if `f(i) == f(i-1)` house `i-1` was skipped, otherwise it was robbed and the walk continues at `i-2`. When there is a tie the walk prefers to skip, except at the first house, where it takes it (a zero-valued first house is reported as robbed; the sum is the same either way, and a test pins the choice). Space reduction and reconstruction trade against each other; some problems keep a parent pointer instead of the whole table.

## 4. Grids: two dimensions and a rolling row

Count the paths from the top-left to the bottom-right moving only right or down, where some cells are blocked. **State** `f(r, c)` = paths to cell `(r, c)`. **Transition** `f(r, c) = f(r-1, c) + f(r, c-1)`, and 0 if the cell is blocked. **Base** `f(0, 0) = 1`. The count can be huge, so it is taken modulo `1,000,000,007`.

```cpp
// Grid paths: count the ways from the top-left to the bottom-right moving only right or down; cells marked 1 are blocked.
// State f(r, c) = ways to reach (r, c).  Transition f(r, c) = f(r-1, c) + f(r, c-1), or 0 on a blocked cell.  Counts are modulo MOD.
const ll MOD = 1000000007LL;
ll grid_memo(int r, int c, const std::vector<std::vector<int>>& g, std::vector<std::vector<ll>>& memo) {
    if (r < 0 || c < 0 || g[r][c]) return 0;
    if (r == 0 && c == 0) return 1;
    if (memo[r][c] >= 0) return memo[r][c];
    return memo[r][c] = (grid_memo(r - 1, c, g, memo) + grid_memo(r, c - 1, g, memo)) % MOD;
}
// Row r needs only row r-1 and the cell to its left: one row of the table is enough. O(R * C) time, O(C) space.
ll grid_rolling(const std::vector<std::vector<int>>& g) {
    int R = (int)g.size(); if (!R) return 0; int C = (int)g[0].size(); if (!C) return 0;
    std::vector<ll> row(C, 0); row[0] = g[0][0] ? 0 : 1;
    for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c) {
        if (g[r][c]) { row[c] = 0; continue; }
        if (r == 0 && c == 0) continue;
        ll up = row[c], left = c ? row[c - 1] : 0;       // row[c] still holds the value of the row above
        row[c] = (up + left) % MOD;
    }
    return row[C - 1];
}
// A minimisation on the same grid: cheapest path sum; the path is recovered by walking the full table back from the end.
ll min_path_sum(const std::vector<std::vector<ll>>& a, std::vector<std::pair<int, int>>* path) {
    int R = (int)a.size(), C = (int)a[0].size(); std::vector<std::vector<ll>> f(R, std::vector<ll>(C));
    for (int r = 0; r < R; ++r) for (int c = 0; c < C; ++c) {
        ll best = (r || c) ? LLONG_MAX : 0; if (r) best = std::min(best, f[r - 1][c]); if (c) best = std::min(best, f[r][c - 1]);
        f[r][c] = a[r][c] + best;
    }
    if (path) { path->clear(); int r = R - 1, c = C - 1; path->push_back({r, c});
        while (r || c) { if (r && (!c || f[r - 1][c] <= f[r][c - 1])) --r; else --c; path->push_back({r, c}); }
        std::reverse(path->begin(), path->end()); }
    return f[R - 1][C - 1];
}
```

```
row 0: 1 1 1
row 1: 1 0 1
row 2: 1 1 2
paths=2
cheapest path sum=7 via (0,0) (0,1) (0,2) (1,2) (2,2)
```

The rolling version is worth reading closely. Filling row by row, left to right, the cell `row[c]` still holds the value from the row above at the moment it is read (it is the "up" value), and `row[c-1]` has already been updated to the current row (it is the "left" value). One array does the work of two rows. On a 1000 by 1000 grid (about 5% blocked) both gave 320,341,941 paths; the memoised version used an 8,000,000 byte table and took 16.5 / 17.3 ms, the rolling one used 8,000 bytes and took 3.9 / 3.9 ms. The unblocked 1000 by 1000 case was also checked against the binomial `C(1998, 999)` modulo the prime, computed independently from factorials.

The same table with `min` instead of `+` gives the **cheapest path**. To recover the route, walk back from the end: at each cell step to whichever of the two predecessors has the smaller table value. That needs the full table, so the rolling trick and the route are again in tension.

## 5. Designing the state

The state must contain **everything the future depends on**. If two partial solutions have the same state but different futures, the DP is wrong. Three small problems show the three typical repairs.

```cpp
// Designing the state. Three small problems where the state is the whole difficulty.
// (a) House Robber II: the houses form a circle, so the first and last are neighbours. State is unchanged; the PROBLEM is split:
//     either house 0 is never robbed (solve houses 1..n-1) or house n-1 is never robbed (solve 0..n-2).
ll rob_circle(const std::vector<ll>& v) {
    int n = (int)v.size(); if (n == 0) return 0; if (n == 1) return v[0];
    return std::max(rob_rolling(std::vector<ll>(v.begin() + 1, v.end())), rob_rolling(std::vector<ll>(v.begin(), v.end() - 1)));
}
// (b) Longest increasing subsequence: "best among the first i elements" is NOT enough, because whether a new element can extend
//     a subsequence depends on its last value. The state must name the end: f(i) = longest increasing subsequence ENDING at i.
int lis_quadratic(const std::vector<int>& a) {
    int n = (int)a.size(), best = 0; std::vector<int> f(n, 1);
    for (int i = 0; i < n; ++i) { for (int j = 0; j < i; ++j) if (a[j] < a[i]) f[i] = std::max(f[i], f[j] + 1); best = std::max(best, f[i]); }
    return best;
}
// (c) Fewest coins for an amount (greedy fails here, Greedy 01). State f(x) = fewest coins that make x exactly; -1 if impossible.
int coin_min(const std::vector<int>& coins, int amount) {
    const int INF = INT_MAX / 2; std::vector<int> f(amount + 1, INF); f[0] = 0;
    for (int x = 1; x <= amount; ++x) for (int c : coins) if (c > 0 && c <= x && f[x - c] + 1 < f[x]) f[x] = f[x - c] + 1;
    return f[amount] >= INF ? -1 : f[amount];
}
```

```
circle 2 3 2: 3 (a line would give 4, but houses 0 and 2 are neighbours)
LIS ending at each index of 10 9 2 5 3 7 101 18: 1 1 1 2 2 3 4 4
LIS length=4
coins 1 3 4, amount 6: fewest=2 (greedy largest-first would use 4+1+1 = 3 coins)
```

- **Split the problem, keep the state (circular robber).** In a circle the first and last houses are neighbours. A single `f(i)` cannot say whether house 0 was robbed. Instead of enlarging the state, split the problem into two cases that each are a line: house 0 is never robbed, or house `n-1` is never robbed. The answer is the larger of two line problems.
- **Name the end (LIS).** "The longest increasing subsequence among the first `i` elements" is not enough: whether a new element can extend a subsequence depends on its **last value**, which that state forgot. The state `f(i)` = the longest increasing subsequence **ending at** `i` carries exactly that; the answer is the maximum over all `i`. (Tutorial 02 reduces the cost from O(n^2) to O(n log n).)
- **Let the amount be the state (coin change).** Greedy fails for coins `{1, 3, 4}` and amount 6 (largest-first takes 4+1+1, three coins; 3+3 is two). `f(x)` = the fewest coins making exactly `x`; try each coin last: `f(x) = 1 + min f(x - c)`. Unreachable amounts stay at "infinity", and the code must not add one to infinity and overflow (the mutant that uses `INT_MAX` is caught by the sanitizer).

A test for any state: take two different partial solutions that reach the same state; can every continuation of one be applied to the other with the same effect on the score? If not, something is missing from the state.

## 6. Sparse states, hash maps and the stack

Sometimes the states that are actually reached are a tiny part of all states that *could* exist. Collatz: from `n` go to `n/2` if even, `3n + 1` if odd; `steps(n) = 1 + steps(next)`, `steps(1) = 0`. Starting values up to a million wander to values far above a million, so a table indexed by value would have to be enormous, but only the visited values matter.

```cpp
// Sparse state spaces. Collatz: from n go to n / 2 if n is even, else 3n + 1. Steps(n) = 1 + Steps(next), Steps(1) = 0.
// The subproblems reached from the starting values 1..N include values far above N, so a plain array of size N does not fit them;
// a hash map stores only the states that are actually reached (memoisation's advantage over a table).
int collatz_steps(uint64_t n, std::unordered_map<uint64_t, int>& memo, ll& states) {
    if (n == 1) return 0;
    auto it = memo.find(n); if (it != memo.end()) return it->second;
    int s = 1 + collatz_steps(n % 2 ? 3 * n + 1 : n / 2, memo, states);
    memo[n] = s; ++states; return s;
}
// The same chain lengths, caching only values below N in a vector (cheaper per lookup, remembers less).
int collatz_vec(uint64_t n, std::vector<int>& cache) {
    int steps = 0; uint64_t x = n;
    while (x != 1 && !(x < cache.size() && cache[x] >= 0)) { x = x % 2 ? 3 * x + 1 : x / 2; ++steps; }
    int total = steps + (x == 1 ? 0 : cache[x]);
    if (n < cache.size()) cache[n] = total;
    return total;
}
```

```
steps for 1..10: 0 1 7 2 5 8 16 3 19 6
steps(27)=111 steps(97)=118
states stored after 1..10, 27, 97: 137
```

For all starting values below 1,000,000 the longest chain starts at 837799 with 524 steps (a known result, reproduced here). Computing every chain directly walked 131,434,272 steps in all and took 209 / 178 ms. The hash-map memo stored 2,168,610 states, more than twice the number of starting values, because the chains pass through large intermediate numbers, and it took 392 / 376 ms: **slower than no memo at all**, because a hash lookup per step costs more than the arithmetic it saves. The vector cache, which remembers only values below the limit but looks them up by index, took 19 / 15 ms. The lesson is not "memoise everything" but "memoise where the lookup is cheaper than the recomputation": the storage structure is part of the algorithm.

**The stack.** Top-down memoisation recurses to depth equal to the length of the dependency chain, which for the robber is `n`. Each frame costs stack. With a 1 MiB stack limit, the memoised robber ran correctly to a depth of about 41,000 houses (a measurement in a child process; about 25 bytes per frame for this small function, which is the compiler's choice and varies by function), so the usual 8 MiB limit would allow roughly eight times that: a few hundred thousand, extrapolated and not measured. A million-house input in top-down form would overflow the stack; the bottom-up form has no such limit. When the depth can be large, tabulate, or make the recursion iterative.

## 7. Top-down or bottom-up

| | Top-down (memo) | Bottom-up (table) |
|---|---|---|
| Writing it | Copy the recurrence, add a cache | Needs the order of evaluation |
| States computed | Only those reachable from the answer | All of them |
| Stack | Depth of the dependency chain | None |
| Speed | Function-call and lookup overhead | Tight loops, can be vectorised |
| Space reduction | Awkward | Natural (rolling window) |
| Best for | Sparse states, first correct version | Dense states, final fast version |

A good habit: write the top-down version first to be sure the recurrence is right, test it against a brute force, then derive the table and the rolling form and test them against the top-down version.

## 8. What the tests check, and what they cannot

Of the 48 mutants, 41 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Seven survive, and each leaves every output the tests can see unchanged. The first run of this mutant set left ten survivors; two pins (a Collatz cache with no preset entry for 1, and a zero-valued first house in the reconstruction) killed three of them. The reasons the other seven stay:

- **`> 0` instead of `>= 0` as the "already computed" test in the memoised Fibonacci.** Every stored value for n >= 2 is at least 1, so a stored 0 never occurs and the two tests agree.
- **`> 0` instead of `>= 0` in the memoised robber.** A stored 0 (all houses so far worth nothing) is then recomputed, which costs time and never changes the value. This is the "sentinel that can be a real answer" mistake of section 9, and it is harmless here only because the recomputation is cheap.
- **Dropping the check for the cell `(0, 0)` in the rolling grid.** The row starts with `row[0] = 1`, and the general formula applied to the first cell gives `1 + 0`, the same value.
- **Presetting `row[0] = 1` even when the start cell is blocked.** The first pass of the loop then writes 0 into it because the cell is blocked.
- **`f[x - c] < f[x]` instead of `f[x - c] + 1 < f[x]` in coin change.** The two differ only when `f[x - c] + 1 == f[x]`, and then the assignment writes the same value.
- **Allowing a coin of value 0.** A zero coin can never make `f[x - 0] + 1 < f[x]` true. (A negative coin would index outside the array; the inputs are positive amounts by definition.)
- **Not storing the chain length of `n` in the Collatz vector cache.** Answers are unchanged; only the speed is, and that is not something the tests measure.

The large inputs have no brute-force reference; they are checked against independent computations (the binomial from factorials, the two forms against each other, a direct walk of the Collatz chain). What the tests cannot do is prove the recurrence is the right model of a problem: they compare the code with a brute force of the *same* definition, so a misreading of the problem would pass. Read the statement again after the tests pass.

Mutants are chosen where a bug is plausible, not at random, so "41 of 48" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Starting with the code.** Write the state in words first. A DP with an unclear state is a guess.
2. **A state that forgets something** (LIS as "best in the first `i`"). Two equal states with different futures.
3. **Wrong order.** Reading a table cell that is not yet filled. In a rolling array, overwriting a value before it is read.
4. **Base cases off by one,** or missing the empty input.
5. **Adding to "infinity"** and overflowing. Use a sentinel with room, or test before adding.
6. **Using greedy where the choice is not safe** (coins `{1, 3, 4}`). When in doubt, test against brute force.
7. **Memoising without a sentinel that cannot be a real answer.** A cache value of 0 may be a true answer (the zero-valued house).
8. **Deep recursion on a long chain,** which overflows the stack long before memory runs out.
9. **Rolling the array and then needing the route.** Decide up front whether the choices are needed.
10. **Forgetting the modulus** in counting problems, or applying it to a comparison.
11. **Believing memoisation is always faster.** A hash lookup per step can cost more than recomputing.
12. **Testing only against the DP's own other form.** All three forms can share a wrong recurrence; compare with a brute force.

## 10. Questions to answer out loud

1. What are overlapping subproblems and optimal substructure? Give a problem with the first but not the second, and one with the second but not the first.
2. How do you compute the running time of a DP from its states?
3. Why is the plain Fibonacci recursion exponential, and exactly how many calls does it make?
4. Explain memoisation versus tabulation. When would you pick each?
5. State the five steps for the house robber.
6. When can a table be reduced to a window, and what do you give up?
7. How do you recover the choices from a DP table?
8. Why is the state "best among the first `i`" wrong for the longest increasing subsequence?
9. How do you handle a circular arrangement in the house robber, and why not enlarge the state?
10. Why is greedy wrong for coins `{1, 3, 4}`?
11. When is a hash map the right memo, and when is it slower than not memoising?
12. How deep can top-down memoisation go, and what do you do about it?

## 11. Problems

### 11.1 On paper

**A.** How many calls does `fib_naive(8)` make? How many does `fib_memo(10)` make?

**B.** House robber on `1, 2, 3, 1`, on `5, 1, 1, 5`, and on `4, 1, 2, 7, 5, 3, 1`. For the last, which houses (0-based)?

**C.** The circular robber on `5, 1, 1, 5` and on `6, 7, 1, 30, 8, 2, 4`.

**D.** Paths in a 3 by 4 grid whose cell `(1, 1)` is blocked; in a 3 by 2 grid whose cell `(1, 0)` is blocked. The cheapest path sum in `{{1, 2, 5}, {3, 2, 1}}`.

**E.** The longest increasing subsequence of `1, 5, 2, 6, 3, 7` and of `4, 3, 2, 1`; the fewest coins from `{2, 5, 10}` for 27.

**F.** Fewest coins from `{3, 5}` for 7 and for 11.

**G.** Collatz steps for 6, 7 and 12.

### 11.2 On LeetCode

For each: state the five steps before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Fibonacci Number | 509 | The three forms (section 2) |
| Climbing Stairs | 70 | The same recurrence as Fibonacci |
| Min Cost Climbing Stairs | 746 | A minimisation with two predecessors |
| House Robber | 198 | Section 3 |
| House Robber II | 213 | Split the circle (section 5) |
| Unique Paths | 62 | Grid with a rolling row |
| Unique Paths II | 63 | Obstacles (section 4) |
| Longest Increasing Path in a Matrix | 329 | Memoisation on a grid (the order is not a simple scan) |

### 11.3 In your editor

**A.** Write a memoisation harness: a wrapper that counts the states visited and the calls made for any recursive function of one or two integer arguments, and use it to confirm `2n - 1` for Fibonacci and the number of states for the grid.

**B.** Reproduce the measurement of section 6 on your machine: Collatz below ten million with a direct walk, a vector cache and a hash memo. Try `reserve` on the hash map and a custom hash, and see whether the memo can beat the direct walk.

**C.** Write the house robber that also returns the robbed set without keeping the whole table (hint: a bitset of decisions, or recompute backwards), and compare memory with the table version.

**D.** Write Unique Paths with the answer as a closed-form binomial and compare with the DP for all grids up to 20 by 20. Why does the closed form stop working with obstacles?

**E.** Find the stack limit of your machine for the memoised robber (`ulimit -s`) and the largest `n` that runs, as in section 6; then convert it to bottom-up and run `n = 10^7`.

**F.** Take any DP from this tutorial, deliberately drop one term from the state, and use a brute-force comparison to find the smallest failing input.

### Answers

**A.** `fib_naive(8)` makes `2 F(9) - 1 = 2 * 34 - 1 = 67` calls. `fib_memo(10)` makes `2 * 10 - 1 = 19` calls (the code returns the same).

**B.** `1, 2, 3, 1`: rob houses 0 and 2 for 4. `5, 1, 1, 5`: rob the two 5s for 10. `4, 1, 2, 7, 5, 3, 1`: the best is 14, from houses 0, 3 and 5 (4 + 7 + 3).

**C.** `5, 1, 1, 5`: the first and last cannot both be taken, so 6 (5 and 1). `6, 7, 1, 30, 8, 2, 4`: the best is 41, from 7, 30 and 4.

**D.** Without the block a 3 by 4 grid has `C(5, 2) = 10` paths; the paths through `(1, 1)` number `2 * 3 = 6`, so 4. In the 3 by 2 grid with `(1, 0)` blocked the only way is right along the top row and then down: 1 path. The cheapest path in `{{1, 2, 5}, {3, 2, 1}}` is `1 + 2 + 2 + 1 = 6`.

**E.** `1, 5, 2, 6, 3, 7`: the longest increasing subsequence has length 4 (for example 1, 2, 3, 7). `4, 3, 2, 1`: length 1. Fewest coins from `{2, 5, 10}` for 27: 4 coins (10 + 10 + 5 + 2).

**F.** `{3, 5}` for 7: `-1` (7 is not a sum of 3s and 5s). For 11: 3 coins (5 + 3 + 3).

**G.** Steps for 6 is 8 (6, 3, 10, 5, 16, 8, 4, 2, 1), for 7 is 16, for 12 is 9 (12, 6 and then the 8 steps of 6).

## Cheat sheet

- **DP = overlapping subproblems + optimal substructure.** Time = number of states x work per state. The state graph is a DAG.
- **Five steps:** state, transition, base, order, answer. Write the state in words first.
- **Three forms:** top-down memo (copy the recurrence, add a cache), bottom-up table (fill in order), rolling window (keep only what the transition reads).
- **Plain Fibonacci makes `2 F(n+1) - 1` calls;** with a memo `2n - 1`.
- **Reduce space** when a state reads a bounded window; keep the table when the choices are needed.
- **Design the state:** it must hold everything the future depends on. Repairs: split the problem (circular robber), name the end (LIS), make the quantity the state (coins).
- **Sparse states:** a hash map memo stores only reached states, but may be slower than recomputing.
- **Top-down depth** is limited by the stack; tabulate for long chains.
- **Test** against a brute force of the same definition, and the three forms against one another.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The two properties of dynamic programming, memoisation, tabulation and the recurrences for Fibonacci, the house robber, grid paths, the longest increasing subsequence and coin change are standard; the statements here are my own wording. That 837799 has the longest Collatz chain below one million is a well-known result, reproduced here by running the code.
- Problem numbers and titles in section 11, and the samples of problems 198, 213, 63, 64, 300 and 322 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
