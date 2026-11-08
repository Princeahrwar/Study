# DSA 10.08: Counting, Digit DP, Expected Values and DP Optimisations

*Part of the [Dynamic programming plan](00_plan.md) · Stretch tutorial · Time: about 10 hours (5 reading, 5 problems) · You need: [DP fundamentals](01_dp_fundamentals.md), [One-dimensional DP](02_one_dimensional_dp.md) (counting the ways), [The knapsack family](04_the_knapsack_family.md) (sums over a window) and [Interval and game DP](05_interval_and_game_dp.md) (the cubic merge table that section 4 speeds up). There is no companion demo file: every structure is listed in this doc

The first seven tutorials taught how to *set up* a DP. This one does four things that usually come after the setup is right.

1. **Counting with a modulus.** Many DPs count, and the counts outgrow 64 bits at once. The answer is asked modulo `10^9 + 7`, and there are exact rules for doing arithmetic that way without a mistake.
2. **Digit DP.** "How many integers up to `N` have some property of their digits?" for `N` as large as `10^18`. You cannot loop over the numbers; you build them digit by digit from the left, with a flag that says whether you are still glued to `N`.
3. **Expected values.** The state is a situation and the value is a probability or an expected count: `f(s)` is the weighted average of the `f` of the states that can follow.
4. **Faster transitions.** If the recurrence is `f(i) = min over j of (f(j) + cost(j, i))`, a table of `n` cells costs `n^2` or more. Three techniques use the *shape* of the costs to cut that: **divide and conquer** optimisation, **Knuth** optimisation and the **convex hull trick**.

By the end you can:

- do the arithmetic of a counting DP modulo a prime, including the subtraction that makes a sliding window work, without a negative value or an overflow;
- write a digit DP with the `tight`, `started` and mask states, and the one-pass version that counts a digit over a range;
- write the expected-value recurrences for a random walk and for a stopping problem, and say when a cutoff answers a large input at once;
- say which condition each of the three optimisations needs, apply it, and show with a counterexample what breaks when the condition fails.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 22,507 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: dice ways against the plain recursion, with the sum of all outcomes checked against `faces^dice`; the digit counts against a loop over every number to 3,000 and over random numbers to 200,000, with known values (13 gives 6 ones, 135 gives 110 numbers with distinct digits, and so on); the digit-set count against a loop for random digit sets; the knight against an enumeration of every move sequence; the soup problem against the plain recursion for every size to 300; the die against an independent series (the sum over `t` of the probability that some face is still missing after `t` rolls); the divide-and-conquer split against the quadratic table and against every way to cut a row; Knuth's merge against the cubic table and against every merge order; the convex hull trick against the double loop, including steps of size 1 and zero fees where ties are everywhere; and pins for the cases random inputs rarely hit (zero dice, a negative number of dice, an empty digit set, more groups than numbers, no stones, one stone). Then 81 hand-made mutants of the listings were run against the tests: **66 were caught and 15 are equivalent changes**; section 8 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Counting with a modulus

**Why a modulus.** Three dice of four faces can land in 64 ways. Thirty dice of thirty faces can land in `30^30`, about `2 * 10^44`. A DP that counts ways produces such numbers, so the problem asks for the count **modulo a prime**, usually `MOD = 10^9 + 7`, and the DP reduces as it goes. The result is the true count's remainder, because addition and multiplication commute with taking remainders.

**The rules.**

- After every addition or multiplication, reduce with `% MOD`. Two numbers below `MOD` multiply to less than `10^18`, which fits in a 64-bit integer; three do not, so reduce between factors.
- After a **subtraction**, add `MOD` before reducing: `((a - b) % MOD + MOD) % MOD`. In C++ the remainder of a negative number is negative.
- A division is not allowed; it becomes a multiplication by a modular inverse (not needed in this tutorial).
- Never compare two reduced numbers to say which true count is larger.

**Dice rolls.** `dice` dice with `faces` faces each: how many ways to get the sum `target`? Let `f[d][t]` be the ways for `d` dice to sum to `t`. The last die shows a value `x` from 1 to `faces`, so `f[d][t]` is the sum of `f[d-1][t-x]`: a *window* of the previous row. Summing the window each time costs `faces` per cell; a **prefix sum** of the previous row gives the window as one subtraction, `pre[t] - pre[t - faces]`, so the whole table costs `O(dice * target)`. That subtraction is where the modulus bites: the true window sum is positive, but its two reduced prefix values can come out in either order.

```cpp
// Counting with a modulus.  When a DP counts things, the answer grows fast (the number of ways can be astronomically large), so the
// problem asks for it modulo MOD = 10^9 + 7.  Rules: reduce after EVERY addition or multiplication, keep intermediate products in 64 bits
// (two numbers below MOD multiply to below 10^18, which fits), and after a SUBTRACTION add MOD before reducing, since a % of a negative
// number is negative.
// Dice rolls: the number of ways `dice` dice with `faces` faces each can sum to `target`.  f[d][t] = the ways d dice sum to t =
// the sum of f[d-1][t-x] for x = 1..faces.  That sum is a window of the previous row, so a prefix sum gives it in O(1) (the subtraction
// is where the modulus needs care).  Time O(dice * target).
ll dice_ways(int dice, int faces, int target) {
    if (dice < 0 || faces <= 0 || target < 0) return 0;
    std::vector<ll> f(target + 1, 0), pre(target + 2, 0); f[0] = 1;
    for (int d = 1; d <= dice; ++d) {
        pre[0] = 0; for (int t = 0; t <= target; ++t) pre[t + 1] = (pre[t] + f[t]) % MOD;
        std::vector<ll> g(target + 1, 0);
        for (int t = 1; t <= target; ++t) { int lo = std::max(0, t - faces); g[t] = ((pre[t] - pre[lo]) % MOD + MOD) % MOD; }
        f.swap(g);
    }
    return f[target];
}
```

```
ways for d dice of 4 faces to sum to t (columns t = 0..12)
  d=0: 1 0 0 0 0 0 0 0 0 0 0 0 0
  d=1: 0 1 1 1 1 0 0 0 0 0 0 0 0
  d=2: 0 0 1 2 3 4 3 2 1 0 0 0 0
  d=3: 0 0 0 1 3 6 10 12 12 10 6 3 1
ways for 3 dice to sum to 7 = 12 (code gives 12); the sum of the row is 64 = 4^3
```

Row `d=3` is the answer table for three four-sided dice: 12 ways to reach 7, and the whole row adds to 64, as it must (every outcome lands on one sum; the tests check this identity for larger dice with the modulus).

Measured. For 50 dice of 500 faces and target 20,000 the count is 257,099,781 (modulo the prime); the prefix-sum version took 6.0 / 7.2 ms and the plain window sum 156 / 157 ms, with the same answer. A thousand six-sided dice with target 6,000 (the only way is all sixes, so the count is 1) took 35.1 / 34.4 ms. The pitfall is real: in the 15,000 cells of a table of 50 six-sided dice, the subtraction without `+ MOD` came out negative in 2,492 of them.

## 2. Digit DP

**The idea.** Count the integers `x` in `[0, N]` with some property of their decimal digits. `N` can be `10^18`, so no loop. Write `N` in decimal and build a number from the left, one digit at a time. At each position the number is in one of two modes:

- **tight**: the digits so far are exactly `N`'s prefix, so the next digit can be at most `N`'s digit;
- **free**: a smaller digit has already been placed, so any digit `0..9` is allowed.

A free suffix does not depend on `N` any more, so it can be counted once and reused: that is where the DP memoises. The state is `(position, tight, ...)` plus whatever the property remembers: a count of ones, the set of digits used (a 10-bit mask), and whether the number has *started* (leading zeros are not digits).

**Counting the digit 1 in 0..N** (233). The property is additive, so the table collapses to three running numbers: how many free prefixes exist, how many ones they hold in total, and how many ones `N`'s own prefix holds. Placing the next digit `d` of `N`:

- the tight prefix spawns `d` new free prefixes (digits `0..d-1`), each carrying the ones of `N`'s prefix, and one of those digits is a `1` when `d >= 2`;
- every old free prefix takes any of 10 digits: its ones are counted 10 times, and the digit `1` adds one more per prefix.

**Numbers with all digits different, in 1..N** (2376). The state is `(position, mask of digits used, tight, started)`. Leading zeros do not enter the mask, which is what `started` is for. Only the free, started states are memoised, by `(position, mask)`: there are `L * 1024` of them, and each tries 10 digits.

**Numbers written with a given set of digits** (902). With digits from a set `D` (digits 1 to 9) allowed to repeat, count the positive integers up to `N`: all strings shorter than `N`'s length count, `|D|^len` each. For the same length, walk `N`'s digits: at each position every allowed digit smaller than `N`'s digit opens a free suffix of `|D|^(remaining)`, and the walk continues only while `N`'s own digit is allowed. If it never stops, `N` itself counts.

```cpp
// Digit DP: count the integers up to N with some property of their digits.  Write N in decimal and build a number digit by digit from the
// left.  While the digits so far equal N's prefix the number is "tight" (the next digit may not exceed N's); once a smaller digit has been
// placed the number is "free" and any digit goes.  The state is (position, tight, and whatever the property needs to remember).
// Count the digit 1 in all of 0..N (233).  Keep three numbers while scanning: free_cnt = how many free prefixes there are, free_ones =
// the total ones in them, tight_ones = the ones in N's own prefix.  Placing a digit d of N: the tight prefix spawns d free ones (digits
// 0..d-1), the old free prefixes each take any of 10 digits, and one of those digits is a 1.
ll count_digit_one(ll N) {
    if (N <= 0) return 0; std::vector<int> dg; for (ll x = N; x; x /= 10) dg.push_back((int)(x % 10)); std::reverse(dg.begin(), dg.end());
    ll freeCnt = 0, freeOnes = 0, tightOnes = 0;
    for (int d : dg) {
        ll nc = freeCnt * 10 + d, no = freeOnes * 10 + freeCnt + (ll)d * tightOnes + (d >= 2 ? 1 : 0);
        freeCnt = nc; freeOnes = no; tightOnes += (d == 1);
    }
    return freeOnes + tightOnes;
}
// Count the integers in 1..N whose digits are all different (2376).  State: the set of digits used so far (a 10-bit mask), whether the
// number has started (leading zeros are not digits) and whether it is tight.  The free, started states are memoised by (position, mask).
ll count_distinct_digits(ll N) {
    if (N <= 0) return 0; std::vector<int> dg; for (ll x = N; x; x /= 10) dg.push_back((int)(x % 10)); std::reverse(dg.begin(), dg.end());
    int L = (int)dg.size(); std::vector<std::vector<ll>> memo(L + 1, std::vector<ll>(1024, -1));
    std::function<ll(int, int, bool, bool)> go = [&](int pos, int mask, bool tight, bool started) -> ll {
        if (pos == L) return started ? 1 : 0;
        if (!tight && started && memo[pos][mask] >= 0) return memo[pos][mask];
        ll total = 0; int hi = tight ? dg[pos] : 9;
        for (int d = 0; d <= hi; ++d) {
            if (started && (mask >> d & 1)) continue; bool st = started || d > 0;
            total += go(pos + 1, st ? (mask | 1 << d) : mask, tight && d == hi, st);
        }
        if (!tight && started) memo[pos][mask] = total; return total;
    };
    return go(0, 0, true, false);
}
// Count the positive integers up to N written only with digits from a given set of digits 1 to 9 (each digit may repeat) (902).  Shorter numbers: any
// choice, |D|^length.  Numbers as long as N: walk N's digits; at each position every allowed digit smaller than N's gives a free suffix,
// and the walk continues only while N's own digit is allowed.
ll count_with_digits(const std::vector<int>& D, ll N) {
    if (N <= 0 || D.empty()) return 0; std::vector<int> dg; for (ll x = N; x; x /= 10) dg.push_back((int)(x % 10)); std::reverse(dg.begin(), dg.end());
    int L = (int)dg.size(); ll k = (ll)D.size(), total = 0, pw = 1;
    for (int len = 1; len < L; ++len) { pw *= k; total += pw; }
    std::vector<ll> p(L + 1, 1); for (int i = 1; i <= L; ++i) p[i] = p[i - 1] * k;
    for (int i = 0; i < L; ++i) {
        int smaller = 0; bool has = false; for (int d : D) { if (d < dg[i]) ++smaller; if (d == dg[i]) has = true; }
        total += (ll)smaller * p[L - 1 - i]; if (!has) return total;
    }
    return total + 1;
}
```

```
count the digit 1 in 0..135, digit by digit: free prefixes (already below N), their ones, the ones in N's own prefix
  after digit 1: free=1 free_ones=0 tight_ones=1
  after digit 3: free=13 free_ones=5 tight_ones=1
  after digit 5: free=135 free_ones=69 tight_ones=1
ones in 0..135 = 70 (a plain count gives 70, code gives 70)
```

In the trace the free count after the three digits of 135 is 135: the numbers 0 to 134 are all free prefixes (they are smaller than 135), and `tight_ones` is the single 1 of the number 135 itself. The total of 70 is `69 + 1`.

Measured. For `N = 10^7` the digit-1 count is 7,000,001; the digit DP took 0.0024 / 0.0022 ms and a loop over every number took 92 / 88 ms, with the same answer. For `N = 10^18` the count of ones is 1,800,000,000,000,000,000 and the count of numbers with distinct digits is 8,877,690 (every such number has at most 10 digits, so the count stops growing), both in 1.147 / 1.035 ms together. The `started` flag matters: a version that counts a leading zero as a digit disagreed with the right answer for 9,991 of the numbers 1 to 10,000.

## 3. Expected-value DP

The state is a situation; the value is a probability or an expected number of steps; the transition is a weighted average over what can happen next. Everything else is unchanged: the same fill order, the same base cases, now in floating point (compare with a tolerance, never `==`).

**A random walk: the knight.** A knight on an `n` by `n` board makes `k` random moves, each of the 8 moves with probability 1/8, including moves that leave the board (it is then gone). What is the probability that it is still on the board? Let `p[s][cell]` be the probability of standing on `cell` after `s` moves. It is filled forward: each cell passes `1/8` of its probability to each in-board target. The answer is the sum of the last row.

**A race: soup servings.** Two soups of `n` ml each; each turn serves one of four portions with probability 1/4 each, taking `(100, 0)`, `(75, 25)`, `(50, 50)` or `(25, 75)` ml from soups A and B (a soup never goes below zero). Find the probability that A is empty first, plus half the probability that both empty together. Work in units of 25 ml. The recurrence is the average of the four successors. For large `n` the table would be huge, but A is served more heavily, so A almost surely empties first, and the answer for `n >= 4800` is within `10^-5` of 1; the code returns 1 from there. This is a *cutoff* argument, and it is checked by computing the answer just below the cutoff.

**An expected count: all faces of a die.** Roll an `n`-sided die until every face has appeared; the expected number of rolls. Let `E(k)` be the expected remaining rolls when `k` faces have been seen. A roll shows a new face with probability `(n - k) / n`, otherwise nothing changes: `E(k) = 1 + (k/n) E(k) + ((n-k)/n) E(k+1)`. Collect `E(k)` on one side: `E(k) = n / (n - k) + E(k + 1)`, with `E(n) = 0`. The state refers to itself (a roll that shows an old face), and solving the equation removes the loop. The total is `n` times the harmonic number `H(n)`.

```cpp
// Expected-value DP: the state is a situation, the value is a probability or an expected count, and the transition is the weighted
// average over what can happen next, f(s) = sum of p(s -> s') * f(s').  The same table-filling rules apply, with doubles.
// Knight on an n x n board makes k random moves (each of the 8 moves with probability 1/8, including moves that leave the board); the
// probability that it is still on the board after k moves (688).  p[step][cell] = the probability of being at that cell after `step`.
double knight_probability(int n, int k, int r, int c) {
    if (n <= 0 || r < 0 || r >= n || c < 0 || c >= n) return 0.0;
    const int dr[8] = {1, 2, 2, 1, -1, -2, -2, -1}, dc[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    std::vector<double> cur(n * n, 0.0); cur[r * n + c] = 1.0;
    for (int s = 0; s < k; ++s) {
        std::vector<double> nxt(n * n, 0.0);
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) if (cur[i * n + j] > 0)
            for (int m = 0; m < 8; ++m) { int a = i + dr[m], b = j + dc[m]; if (a >= 0 && a < n && b >= 0 && b < n) nxt[a * n + b] += cur[i * n + j] / 8.0; }
        cur.swap(nxt);
    }
    double total = 0; for (double x : cur) total += x; return total;
}
// Soup servings (808): two soups of n ml each; each turn one of four serves, with probability 1/4 each, takes (100,0), (75,25), (50,50)
// or (25,75) ml of (A, B); a soup never goes below 0.  The probability that A is empty first, plus half the probability that they
// empty together.  Work in units of 25 ml.  Once n is large, A almost surely empties first, so the answer is within 10^-5 of 1 for n >= 4800.
double soup_servings(int n) {
    if (n >= 4800) return 1.0; int m = (n + 24) / 25; std::vector<std::vector<double>> f(m + 1, std::vector<double>(m + 1, 0.0));
    for (int a = 0; a <= m; ++a) for (int b = 0; b <= m; ++b) {
        if (a == 0 && b == 0) { f[a][b] = 0.5; continue; } if (a == 0) { f[a][b] = 1.0; continue; } if (b == 0) { f[a][b] = 0.0; continue; }
        f[a][b] = 0.25 * (f[std::max(a - 4, 0)][b] + f[std::max(a - 3, 0)][std::max(b - 1, 0)] + f[std::max(a - 2, 0)][std::max(b - 2, 0)] + f[std::max(a - 1, 0)][std::max(b - 3, 0)]);
    }
    return f[m][m];
}
// Expected number of rolls of an n-faced die until every face has appeared.  E(k) = the expected further rolls when k faces have been
// seen: a roll shows a new face with probability (n - k) / n, so E(k) = 1 + (k / n) E(k) + ((n - k) / n) E(k + 1), which solves to
// E(k) = n / (n - k) + E(k + 1) with E(n) = 0.  (The total is n times the n-th harmonic number.)
double expected_rolls_all_faces(int n) {
    if (n <= 0) return 0.0; double e = 0; for (int k = n - 1; k >= 0; --k) e = (double)n / (n - k) + e; return e;
}
```

```
knight on a 3 by 3 board from the corner (0,0), counts of move sequences that stay on the board (each sequence has probability 1/8 per step)
  after 0 moves: | 1 0 0 | 0 0 0 | 0 0 0
  after 1 moves: | 0 0 0 | 0 0 1 | 0 1 0
  after 2 moves: | 2 0 1 | 0 0 0 | 1 0 0
probability after 2 moves = 4 / 64 = 0.0625 (code gives 0.0625)
```

```
expected rolls of a 3-sided die to see every face: E(k) = n / (n - k) + E(k + 1), k = faces seen so far
  E(3) = 0
  E(2) = 3/1 + E(3) = 3.0000
  E(1) = 3/2 + E(2) = 4.5000
  E(0) = 3/3 + E(1) = 5.5000
answer 5.5000 (code gives 5.5000)
```

In the knight trace the 4 sequences of two moves from the corner that stay on the 3 by 3 board give `4 / 64`; the other 60 leave the board, and the counts `2 0 1 / 0 0 0 / 1 0 0` show the knight can come back to the corner in two ways. In the die trace each term `n / (n - k)` is the expected wait for a *new* face when `k` are seen: 1 roll for the first, `3/2` for the second, `3` for the last.

Measured. The knight on a 25 by 25 board from the centre stays on the board for 100 moves with probability 0.047429 (0.44 / 0.34 ms). The soup problem for `n = 1000` has probability 0.976565, and just below the cutoff, at `n = 4799`, the answer is within 5.0 * 10^-6 of 1. The expected number of rolls to see all faces of a 100-sided die is 518.7378, which is `100 * H(100)` (the verification script computes the harmonic number independently).

## 4. Making the transition cheaper

Take `f[j][i] = min over t < i of ( f[j-1][t] + cost(t, i) )`: row `j` of the table from row `j - 1`, with a *cost* between a previous position `t` and a new one `i`. The plain table costs `O(n^2)` per row. Let `opt(i)` be the `t` that wins for `i`. Three techniques exploit regularities in `opt` or in the cost.

**(1) Divide and conquer.** Condition: `opt(i)` never decreases as `i` grows. This follows from the **quadrangle inequality** on the cost, `cost(a, c) + cost(b, d) <= cost(a, d) + cost(b, c)` for `a <= b <= c <= d`. Solve the middle `i` by scanning the allowed `t`; call its winner `best`. The left half has `opt <= best` and the right half has `opt >= best`, so recurse with those bounds. Each level of the recursion scans each `t` a bounded number of times, so a row costs `O(n log n)`. Example: split a row of non-negative numbers into `k` groups to minimise the sum of the squares of the group sums, where `cost(t, i) = (P[i] - P[t])^2` with `P` the prefix sums. The squares of range sums satisfy the inequality; their *negatives* do not, and the monotonicity breaks.

**(2) Knuth.** For the interval table of [Interval and game DP](05_interval_and_game_dp.md), `f[i][j] = min over k of f[i][k] + f[k+1][j] + sum(i..j)`, the best split satisfies `opt[i][j-1] <= opt[i][j] <= opt[i+1][j]`. So only the splits in that window are tried. On one diagonal (all ranges of the same length) the windows `[opt[i][j-1], opt[i+1][j]]` chain end to start and add up to `O(n)`, so the whole table costs `O(n^2)` instead of `O(n^3)`. The condition is again a quadrangle inequality on the cost (the sum of a range qualifies).

**(3) The convex hull trick.** Condition: the transition is `f[i] = (a term of i) + min over j of (m_j * x_i + b_j)`: the **lowest of a set of lines** at the point `x_i`. Example: stones at strictly increasing heights `h`; a jump from `j` to `i` costs `(h[i] - h[j])^2 + C`; find the cheapest way from the first stone to the last. Expand the square: `f[i] = h[i]^2 + C + min over j of ( -2 h[j] * h[i] + (f[j] + h[j]^2) )`. Each earlier stone `j` is a line with slope `-2 h[j]` and intercept `f[j] + h[j]^2`, queried at `x = h[i]`. Here the slopes arrive in decreasing order and the queries in increasing order, so a **deque** works: a new line removes the last line if the last line is never strictly lowest (a cross-multiplication test, done in 128 bits because the products overflow 64), and a query drops the front line while the next one is at least as low. Every line enters and leaves once, so the whole DP is `O(n)`. If the slopes or queries are not monotone you need a binary search on the hull, or a Li Chao tree; neither is implemented here.

```cpp
// Three optimisations of a DP whose transition is a minimum over a split point.
// (1) Divide and conquer.  Split a row of non-negative numbers into k non-empty contiguous groups to minimise the sum of the SQUARES of
// the group sums.  f[j][i] = min over t < i of f[j-1][t] + (P[i] - P[t])^2 with P the prefix sums.  The best t (the "opt") never
// decreases as i grows; so to fill a row, solve the middle i by trying only the t allowed by the bounds, then recurse left with the
// bounds up to opt and right with the bounds from opt.  Each level of the recursion scans O(n) values, so a row costs O(n log n)
// instead of O(n^2).
ll split_cost_naive(const std::vector<ll>& a, int k) {
    int n = (int)a.size(); if (k <= 0 || k > n) return n == 0 && k == 0 ? 0 : -1; const ll INF = LLONG_MAX / 4; std::vector<ll> P(n + 1, 0);
    for (int i = 0; i < n; ++i) P[i + 1] = P[i] + a[i];
    std::vector<ll> f(n + 1, INF); f[0] = 0;
    for (int j = 1; j <= k; ++j) { std::vector<ll> g(n + 1, INF); for (int i = j; i <= n; ++i) for (int t = j - 1; t < i; ++t) if (f[t] < INF) g[i] = std::min(g[i], f[t] + (P[i] - P[t]) * (P[i] - P[t])); f.swap(g); }
    return f[n];
}
static void dc_row(const std::vector<ll>& P, const std::vector<ll>& prev, std::vector<ll>& cur, int l, int r, int optl, int optr) {
    if (l > r) return; int mid = (l + r) / 2, best = -1; ll bv = LLONG_MAX;
    for (int t = optl; t <= std::min(mid - 1, optr); ++t) { ll v = prev[t] + (P[mid] - P[t]) * (P[mid] - P[t]); if (v < bv) { bv = v; best = t; } }
    cur[mid] = bv; dc_row(P, prev, cur, l, mid - 1, optl, best); dc_row(P, prev, cur, mid + 1, r, best, optr);
}
ll split_cost_dc(const std::vector<ll>& a, int k) {
    int n = (int)a.size(); if (k <= 0 || k > n) return n == 0 && k == 0 ? 0 : -1; std::vector<ll> P(n + 1, 0); for (int i = 0; i < n; ++i) P[i + 1] = P[i] + a[i];
    const ll INF = LLONG_MAX / 4; std::vector<ll> prev(n + 1, INF), cur(n + 1, INF); for (int i = 1; i <= n; ++i) prev[i] = P[i] * P[i];
    for (int j = 2; j <= k; ++j) { std::fill(cur.begin(), cur.end(), INF); dc_row(P, prev, cur, j, n, j - 1, n - 1); prev.swap(cur); }
    return prev[n];
}
// (2) Knuth.  Merging adjacent piles (cost = the combined size): f[i][j] = min over k of f[i][k] + f[k+1][j] + sum(i..j).  The best
// split satisfies opt[i][j-1] <= opt[i][j] <= opt[i+1][j], so only k in that window needs trying; summed over a diagonal the windows
// telescope to O(n), so the whole table costs O(n^2) instead of O(n^3).
ll merge_piles_cubic(const std::vector<ll>& a) {
    int n = (int)a.size(); if (n <= 1) return 0; std::vector<ll> pre(n + 1, 0); for (int i = 0; i < n; ++i) pre[i + 1] = pre[i] + a[i];
    std::vector<std::vector<ll>> f(n, std::vector<ll>(n, 0));
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) { int j = i + len - 1; f[i][j] = LLONG_MAX; for (int k = i; k < j; ++k) f[i][j] = std::min(f[i][j], f[i][k] + f[k + 1][j]); f[i][j] += pre[j + 1] - pre[i]; }
    return f[0][n - 1];
}
ll merge_piles_knuth(const std::vector<ll>& a) {
    int n = (int)a.size(); if (n <= 1) return 0; std::vector<ll> pre(n + 1, 0); for (int i = 0; i < n; ++i) pre[i + 1] = pre[i] + a[i];
    std::vector<std::vector<ll>> f(n, std::vector<ll>(n, 0)); std::vector<std::vector<int>> opt(n, std::vector<int>(n, 0));
    for (int i = 0; i < n; ++i) opt[i][i] = i;
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) {
        int j = i + len - 1; f[i][j] = LLONG_MAX; int lo = opt[i][j - 1], hi = std::min(opt[i + 1][j], j - 1);
        for (int k = lo; k <= hi; ++k) { ll c = f[i][k] + f[k + 1][j]; if (c < f[i][j]) { f[i][j] = c; opt[i][j] = k; } }
        f[i][j] += pre[j + 1] - pre[i];
    }
    return f[0][n - 1];
}
// (3) Convex hull trick.  A transition f[i] = min over j < i of  (m_j * x_i + b_j)  + extra(i) is "the lowest of a set of lines at the
// point x_i".  Example (the frog problem): stones at strictly increasing heights h, a jump from j to i costs (h[i] - h[j])^2 + C, the
// cheapest way from stone 0 to the last stone.  Expanding, f[i] = h[i]^2 + C + min over j of ( -2 h[j] * h[i] + (f[j] + h[j]^2) ), a
// line per j with slope -2 h[j] (decreasing as j grows) and a query at x = h[i] (increasing).  Keep the lines that can still be the
// lowest in a deque: a new line removes the last one if that one is never strictly lowest, and queries pop the front while the next
// line is at least as low.  Every line enters and leaves once, so the time is O(n).
ll frog_naive(const std::vector<ll>& h, ll C) {
    int n = (int)h.size(); if (n <= 1) return 0; std::vector<ll> f(n, LLONG_MAX / 4); f[0] = 0;
    for (int i = 1; i < n; ++i) for (int j = 0; j < i; ++j) f[i] = std::min(f[i], f[j] + (h[i] - h[j]) * (h[i] - h[j]) + C);
    return f[n - 1];
}
ll frog_cht(const std::vector<ll>& h, ll C) {
    int n = (int)h.size(); if (n <= 1) return 0; struct Line { ll m, b; }; std::deque<Line> dq; std::vector<ll> f(n, 0);
    auto bad = [](const Line& l1, const Line& l2, const Line& l3) { return (__int128)(l3.b - l1.b) * (l1.m - l2.m) <= (__int128)(l2.b - l1.b) * (l1.m - l3.m); };
    auto val = [](const Line& l, ll x) { return l.m * x + l.b; };
    dq.push_back({-2 * h[0], f[0] + h[0] * h[0]});
    for (int i = 1; i < n; ++i) {
        while (dq.size() >= 2 && val(dq[1], h[i]) <= val(dq[0], h[i])) dq.pop_front();
        f[i] = val(dq.front(), h[i]) + h[i] * h[i] + C; Line nl = {-2 * h[i], f[i] + h[i] * h[i]};
        while (dq.size() >= 2 && bad(dq[dq.size() - 2], dq.back(), nl)) dq.pop_back();
        dq.push_back(nl);
    }
    return f[n - 1];
}
```

```
split 3 1 4 1 5 9 2 6 into groups, cost = sum of squared group sums; row j=2 (two groups): the best cut t for each prefix length i
  i:    2 3 4 5 6 7 8
  opt:  1 2 2 3 4 5 5
three groups: naive 341, divide and conquer 341
merge piles 3 1 4 1 5 with Knuth: opt[i][j] (rows i, columns j)
  i=0:  0  0  1  1  2
  i=1:  .  1  1  1  2
  i=2:  .  .  2  2  3
  i=3:  .  .  .  3  3
  i=4:  .  .  .  .  4
cost 32 (cubic table gives 32, Knuth code gives 32); each opt is at least the one to its left and at most the one below
```

```
frog on stones at heights 0 2 3 7 8, jump cost (difference)^2 + 1; lines y = m x + b with m = -2 h[j], b = f[j] + h[j]^2
  stone 0: f=0, lines (0,0)
  stone 1 (h=2): best line at x=2 gives f=5; lines kept: (0,0) (-4,9)
  stone 2 (h=3): best line at x=3 gives f=7; lines kept: (-4,9) (-6,16)
  stone 3 (h=7): best line at x=7 gives f=24; lines kept: (-6,16) (-14,73)
  stone 4 (h=8): best line at x=8 gives f=26; lines kept: (-14,73) (-16,90)
cheapest cost 26 (plain double loop gives 26, code gives 26)
```

In the first trace the best cut for the two-group row goes `1 2 2 3 4 5 5`: it never decreases, which is exactly what the divide-and-conquer needs. The Knuth table obeys the sandwich: for example `opt[0][4] = 2` lies between `opt[0][3] = 1` and `opt[1][4] = 2`. In the frog trace the front line `(0,0)` is dropped as soon as the second line is lower at the query `x = 3`, and each later front line goes the same way once it stops being the lowest; no line is removed from the back in this example.

Measured. Splitting 3,000 numbers into 20 groups: divide and conquer took 1 / 1 ms and the quadratic table 66 / 55 ms, with the same cost, 1,090,786,111. Divide and conquer alone handled 100,000 numbers in 100 groups in 185 / 206 ms (cost 248,864,620,937). Merging 1,500 piles: Knuth took 29 / 38 ms and the cubic table 912 / 1009 ms with the same cost, 7,805,588; Knuth alone did 3,000 piles in 180 / 141 ms. The frog problem with a million stones took the convex hull trick 23 / 20 ms (cost 100,982,178,193), while the double loop took 159 / 116 ms for only 20,000 stones, with the same answer on those.

## 5. Recognising the shape

| Signal in the statement | Technique | Time |
|---|---|---|
| "number of ways", "modulo 10^9 + 7" | reduce after every operation; `+ MOD` after a subtraction | same as the DP |
| A sum over a window of the previous row | prefix sums, one subtraction | O(cells) |
| Count integers up to a huge `N` by a digit property | digit DP: `tight`, `started`, a mask or a counter | O(L * states * 10) |
| Probability of ending somewhere after `k` steps | forward table of probabilities, sum at the end | O(k * cells * moves) |
| Expected number of steps until a stopping condition | `E(s) = 1 + average E(next)`; solve any self-reference | O(states) |
| A probability that tends to 1 for huge input | cutoff, checked just below it | O(1) above |
| `min over t of f[t] + cost(t, i)`, `opt` nondecreasing | divide and conquer by rows | O(k n log n) |
| Interval table `min over k` with a range-sum cost | Knuth window | O(n^2) |
| `min over j of (m_j x_i + b_j)` with sorted slopes and queries | convex hull trick with a deque | O(n) |

## 6. Where it goes wrong

A mistake in each technique is silent: the code runs and gives a plausible number. Measured here:

- **No `+ MOD` after the subtraction in the dice window:** negative values in 2,492 of 15,000 table cells.
- **Treating a leading zero as a digit in the distinct-digits count:** wrong on 9,991 of the 10,000 numbers 1 to 10,000.
- **Using the divide-and-conquer on a cost that breaks the inequality** (the negated squares): wrong on 14,187 of 100,000 random small instances (14.2%).
- **Using the convex hull trick when the heights are not sorted** (random heights instead of increasing ones): wrong on 50,157 of 100,000 random instances (50.2%).

## 7. Testing these DPs

1. **A different method for every table:** the plain recursion for dice, a loop over every number for the digit counts, every move sequence for the knight, the plain recursion for the soup, and an independent series for the die.
2. **Identities:** the dice row sums to `faces^dice`; the expected rolls equal `n` times the harmonic number; the digit-distinct count of `10^18` equals the count of numbers with at most 10 digits.
3. **The optimised against the plain version.** Each fast version (divide and conquer, Knuth, the hull) is compared with its quadratic or cubic table on random inputs, and on inputs with many ties (zero values, unit steps), because ties are where an off-by-one in a window hides.
4. **Known values:** 27 ways for three dice to make 10, 6 ones in 0 to 13, 110 numbers up to 135 with distinct digits, 0.0625 for the knight, 0.625 for the soup at 50, 3 and 5.5 for dice of 2 and 3 faces.
5. **Pins:** zero and negative inputs, an empty digit set, more groups than numbers, one stone, no stones.

## 8. What the tests check, and what they cannot

Of the 81 mutants, 66 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Fifteen survive, and each leaves every output the tests can see unchanged. The first run left sixteen survivors; one pin (a negative number of dice must give 0, not 1 for a target of 0) killed one. Most of the rest are *widenings*: an optimisation window or a tie rule changed so that the code does more work or picks another equally good choice, which costs time and not correctness. The reasons the fifteen stay:

- **Not reducing the prefix sums of the dice table.** The prefix values then grow to at most `target` times `MOD`, far below the 64-bit limit for any table that fits in memory, and only the differences are used, which are congruent either way.
- **Starting the dice window loop at `t = 0` instead of `t = 1`.** For `t = 0` the window is empty, so the value is 0 as before.
- **Starting the prefix sums at 1 instead of 0.** Every window is a difference of two prefix values, and a constant offset cancels.
- **Not requiring `started` before skipping a used digit in the distinct-digits count.** Until the number starts, the mask is empty, so there is no used digit to skip.
- **Memoising with the key `(position, mask)` for non-started states too.** The only non-started state has the empty mask, and no started state does, so the keys never collide.
- **Not returning early for an empty digit set.** With no digits the count of shorter numbers is 0 and the walk stops at once with 0.
- **Summing the expected-roll terms in the other order (`n / (k + 1)` for `k = 0..n-1`).** It is the same set of terms, added in the opposite order: `n` times the harmonic number.
- **Taking the later `t` on a tie in divide and conquer (`<=`).** Any of several equally good cuts gives the same minimum, and the bounds stay valid for either.
- **Recursing right with the full window (`optl`, not `best`).** A wider window, so more work and the same minimum.
- **Starting the window at 0 instead of `j - 1` in each divide-and-conquer row.** The extra cuts have infinite cost from the previous row and never win.
- **Initialising `best` to the lower bound instead of -1.** The scan always runs at least once, so the initial value is overwritten.
- **Starting the Knuth window at `i` instead of `opt[i][j-1]`.** A wider window; the extra splits are never better.
- **Taking the later split on a tie in Knuth (`<=`).** Equal costs give the same table values.
- **Keeping the front line of the hull on a tie (`<` instead of `<=`).** Equal values at the query do not change any minimum.
- **Keeping a redundant middle line on a tie in the hull test (`<`).** A line that touches the envelope in one point changes no minimum.

The large inputs have no brute-force reference; they are checked against independent computations (the prefix-sum and plain versions against each other, the counts for `10^18`, the harmonic number). What the tests cannot do is prove that the states model the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass. Also, the tests verify the optimised versions on inputs that satisfy their conditions; they do not prove the quadrangle inequality, which is a property of the cost you must establish yourself.

Mutants are chosen where a bug is plausible, not at random, so "66 of 81" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Forgetting `+ MOD` after a subtraction,** or reducing only at the end so an intermediate overflows.
2. **Multiplying three reduced numbers** before reducing.
3. **Comparing two reduced counts** to decide which is larger.
4. **Digit DP without `started`:** leading zeros counted as digits.
5. **Digit DP with the wrong bound when tight:** the next digit is at most `N`'s digit, and tightness continues only if the digit equals it.
6. **Forgetting that `N` itself counts** when the walk never leaves the tight path.
7. **Memoising a tight state** by position alone: tight states depend on `N`; only free states can be shared.
8. **Comparing doubles with `==`;** use a tolerance.
9. **A self-referencing expected value left unsolved:** `E(k)` depends on itself when a step can leave the state unchanged; move it to the left side.
10. **Applying divide and conquer or Knuth without checking the condition** on the cost.
11. **Window bounds that are off by one** in Knuth (`opt[i][j-1]` to `opt[i+1][j]`, and not beyond `j - 1`).
12. **Using the hull trick on unsorted slopes or queries.**
13. **Cross-multiplying 64-bit values** in the hull test; the products can overflow, so use 128 bits.
14. **Taking a cutoff on trust:** compute the value just below it and look.

## 10. Questions to answer out loud

1. Why is the answer asked modulo a prime, and which three rules keep the arithmetic correct?
2. Why can a window sum of the previous row be one subtraction, and what must you add after it?
3. What are the `tight` and `free` modes in a digit DP, and which states can be shared?
4. What does `started` do, and what goes wrong without it?
5. How does the digit-1 count avoid a table over positions and counts?
6. How is the knight's survival probability computed, and why is the sum of the last row the answer?
7. Derive `E(k) = n / (n - k) + E(k + 1)` for the die.
8. Why is the soup answer 1 for large `n`, and how would you check the cutoff?
9. What does the divide-and-conquer optimisation need, and what is `opt`?
10. Why does Knuth's optimisation cost `O(n^2)`? What goes wrong if you use the wrong window?
11. Write the frog DP as lines. Why are a deque and a 128-bit comparison enough?
12. What would you change if the slopes were not sorted?

## 11. Problems

### 11.1 On paper

**A.** Dice: the number of ways for 3 six-sided dice to sum to 10, and for 2 five-sided dice to sum to 7.

**B.** The number of times the digit 1 is written in the numbers 0 to 100, and 0 to 1234.

**C.** How many numbers from 1 to 135 have all digits different? How many numbers up to 300 use only the digits 2 and 5? How many numbers from 1 to 1000 have all digits different?

**D.** A knight in the centre of a 3 by 3 board: the probability of staying on the board for 2 moves. A knight in the corner of a 4 by 4 board: the probability of staying on after 1 move.

**E.** The soup answer for `n = 50` and `n = 100`; the expected rolls of a 4-sided die to see every face.

**F.** Split `1 2 3 4 5` into 2 groups to minimise the sum of the squares of the group sums; split `4 1 1 4` into 2 groups; the cheapest way to merge the piles `2 7 1 8 2 8`.

**G.** The frog problem for stones at heights `0 3 4 9` with fee 2, and for `0 1 2 3 4` with fee 10.

### 11.2 On LeetCode

For each: state the DP, the fill order and (for the last group) the condition the optimisation needs before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Number of Dice Rolls With Target Sum | 1155 | Section 1: the window and the modulus |
| Numbers At Most N Given Digit Set | 902 | Counting by length, then the walk |
| Number of Digit One | 233 | The running counts |
| Count Special Integers | 2376 | The mask, `started` and `tight` |
| Knight Probability in Chessboard | 688 | A forward table of probabilities |
| Soup Servings | 808 | A race, with a cutoff |
| Minimum Cost to Merge Stones | 1000 | The cubic table before Knuth |
| Allocate Mailboxes | 1478 | Divide and conquer or a quadratic table |
| Dungeon Game | 174 | A DP run backwards, for contrast |

### 11.3 In your editor

**A.** Write the number of integers in `[0, N]` whose digit sum is a multiple of 7, by digit DP with a remainder state, and test it against a loop for `N` up to 100,000.

**B.** Write the digit DP in one generic function that takes a "transition on the state" callback and compare it with the specific listings.

**C.** Write the expected number of rolls of a die until two sixes in a row, as a three-state machine, and check it against a simulation.

**D.** Write the optimal binary search tree (given access frequencies) with Knuth's window and compare it with the cubic table.

**E.** Write the Li Chao tree and use it for the frog problem with the heights in random order; compare with the double loop.

**F.** Write the divide-and-conquer split with the **negated** cost and show a small array for which it differs from the quadratic table.

### Answers

**A.** There are 27 ways for 3 six-sided dice to make 10 and 4 ways for 2 five-sided dice to make 7.

**B.** 21 ones in 0 to 100, and 689 in 0 to 1234.

**C.** 110 numbers up to 135 have all digits different; 10 numbers up to 300 use only the digits 2 and 5 (2, 5, 22, 25, 52, 55, 222, 225, 252, 255); 738 numbers up to 1000 have all digits different (9 + 81 + 648).

**D.** From the centre of the 3 by 3 board every move leaves the board, so the probability is 0. From the corner of a 4 by 4 board 2 of the 8 moves stay on, so the probability is 0.25.

**E.** Soup: 0.625 for 50 and 0.71875 for 100. The 4-sided die needs 25/3, about 8.3333 rolls (`4 * (1 + 1/2 + 1/3 + 1/4)`).

**F.** `1 2 3 4 5` into 2 groups gives 117 (`6` and `9`: 36 + 81); `4 1 1 4` gives 50 (`5` and `5`); the piles `2 7 1 8 2 8` cost 74 to merge.

**G.** 41 for the first (a jump from 0 to 3 costs 11, then to 4 costs 3, then to 9 costs 27) and 26 for the second (one jump of 4 costs 16 + 10).

## Cheat sheet

- **Modulus:** reduce after each operation; `((a - b) % MOD + MOD) % MOD` after a subtraction; products of two fit in 64 bits.
- **Window sums:** `pre[t] - pre[t - w]`; the dice table is `O(dice * target)`.
- **Digit DP:** state `(pos, tight, started, mask or counter)`; share only free states; `N` itself counts.
- **Digit 1 in 0..N:** `free_ones = 10 * free_ones + free_cnt + d * tight_ones + (d >= 2)`.
- **Expected values:** `f(s) = sum p * f(next)`; solve a self-reference; compare doubles with a tolerance; a cutoff for huge input.
- **Divide and conquer:** `opt` nondecreasing (quadrangle inequality); recurse on the middle; `O(k n log n)`.
- **Knuth:** `opt[i][j-1] <= opt[i][j] <= opt[i+1][j]`; `O(n^2)`.
- **Convex hull trick:** lines `m x + b`; sorted slopes and queries give a deque in `O(n)`; compare with 128-bit products.
- **Where it goes wrong:** see section 6 for how often.
- **Test** against the plain version, with ties, with identities, and with pins.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- Modular arithmetic, digit DP, the expected-value recurrences, the divide-and-conquer optimisation, Knuth's optimisation and the convex hull trick are standard; the statements here are my own wording. The quadrangle inequality is named as the usual sufficient condition and is not proved here. The Li Chao tree and the optimal binary search tree (section 11.3) are named from memory of the standard account and are posed as exercises, not proved or measured.
- The harmonic-number value and the binomial-style identities used to check the benchmarks are computed by the verification script. Problem numbers and titles in section 11, and the samples of problems 1155, 902, 233, 2376, 688 and 808 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
