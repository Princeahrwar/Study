# DSA 10.05: Interval and Game DP

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 10 hours (5 reading, 5 problems) · You need: [DP fundamentals](01_dp_fundamentals.md) (state, transition, the fill order) and [Grid and two-sequence DP](03_grid_and_two_sequence_dp.md) (a table over a pair of positions). There is no companion demo file: every structure is listed in this doc

In the earlier tutorials the state was an amount, a prefix, or a pair of prefixes. Here the state is a **range**: `f(i, j)` is the answer for the part of the input from `i` to `j`, and a long range is solved by **splitting** it into two shorter ranges and combining their answers. That is **interval DP**. Two things set it apart:

1. **The fill order is by length.** `f(i, j)` needs ranges strictly inside `[i, j]`, so you fill all ranges of length 1, then 2, then 3, and so on. Filling row by row, the way a grid is filled, reads cells that are not ready yet.
2. **The difficult part is choosing what the split means.** Matrix chain splits at the *last multiplication*. Burst balloons splits at the *last balloon burst*. Choosing the first burst instead tangles the sides together; choosing the last keeps them apart. Finding the right "last" is most of the work.

The same range state also drives **game DP**: two players take turns removing from the ends of a row, and `f(i, j)` is the best the player to move can do on what is left. The last section turns to games that cannot be solved range by range, and shows the shortcut that does solve them: Nim and the Sprague-Grundy idea.

By the end you can:

- fill an interval table by length and say what a split point means for matrix chain, merging piles, burst balloons, palindrome partitioning and the strange printer;
- recover the splits (the parenthesisation, the burst order) from the table;
- write the minimax recurrence for a two-player game as a difference of scores, and use the one-array version;
- explain why the first player cannot lose a stone game with an even number of piles;
- play Nim by the XOR rule, and compute Grundy values for a game built from piles.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 24,714 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: matrix chain against every parenthesisation, and the printed parenthesisation parsed and replayed so that its own cost, its matrix order and its dimensions are checked; merging piles against every merge order; burst balloons against every burst order (the real process, neighbours and all); palindrome cuts against every set of cut positions, and the longest palindromic substring against every substring; the strange printer against a breadth-first search over printing boards for **every** string over two letters up to length 8 and over three letters up to length 6; the stone game against plain minimax, with the even-length theorem checked on random arrays that include negative numbers; Nim against a search of the game tree, and Grundy values for subtraction games against that same search on several piles; and pins for the cases random inputs rarely hit (one matrix, an empty list, ties in the table, repeated neighbours, a sentinel at each end, equal dimensions, all-equal piles). Then 59 hand-made mutants of the listings were run against the tests: **55 were caught and 4 are equivalent changes**; section 8 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Interval DP: matrix chain and merging piles

**Matrix chain.** Multiplying an `a x b` matrix by a `b x c` matrix costs `a * b * c` scalar multiplications. Matrix multiplication is associative, so a product `A1 A2 ... An` can be bracketed in many ways, and the bracketing changes the cost (never the result). With `dims` listing the sizes (matrix `t` has `dims[t]` rows and `dims[t+1]` columns), find the cheapest bracketing.

The state is `f(i, j)` = the cheapest cost of computing the product of matrices `i` to `j`. Look at the **last** multiplication: it multiplies the product of `i..k` (a `dims[i] x dims[k+1]` matrix) by the product of `k+1..j` (a `dims[k+1] x dims[j+1]` matrix). So

```
f(i, i) = 0
f(i, j) = min over k in [i, j-1] of  f(i, k) + f(k+1, j) + dims[i] * dims[k+1] * dims[j+1]
```

The table has about `n^2 / 2` cells and each takes `O(n)` to fill, so the time is `O(n^3)`. Fill by length: the two pieces `[i, k]` and `[k+1, j]` are shorter than `[i, j]`.

**Merging piles.** Piles in a row may be merged only with an adjacent pile, at a cost equal to the combined size. The same split works: `f(i, j) = min over k of f(i, k) + f(k+1, j) + sum(i..j)`. The final pile has the same size whatever the order, so the sum comes from prefix sums and is added once per range.

```cpp
// Interval DP: the state is a range [i, j] and the answer for a long range is built from answers for the two shorter ranges it
// splits into.  The fill order is BY LENGTH, so both pieces of every split are ready.
// Matrix chain: matrix t has dims[t] rows and dims[t+1] columns (t = 0 .. n-1, n = dims.size() - 1).  Multiplying an a x b matrix by a
// b x c matrix costs a*b*c.  f(i, j) = the cheapest way to multiply matrices i..j.  The last multiplication splits at some k:
// f(i, j) = min over k of f(i, k) + f(k+1, j) + dims[i] * dims[k+1] * dims[j+1].
ll matrix_chain(const std::vector<ll>& dims) {
    int n = (int)dims.size() - 1; if (n <= 1) return 0;
    std::vector<std::vector<ll>> f(n, std::vector<ll>(n, 0));
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) {
        int j = i + len - 1; f[i][j] = LLONG_MAX;
        for (int k = i; k < j; ++k) f[i][j] = std::min(f[i][j], f[i][k] + f[k + 1][j] + dims[i] * dims[k + 1] * dims[j + 1]);
    }
    return f[0][n - 1];
}
// The same table with the best split k remembered, then turned into a parenthesised product such as ((A1A2)A3).  On a tie the
// smallest k wins (the loop keeps the first strictly smaller value).
std::string matrix_chain_order(const std::vector<ll>& dims) {
    int n = (int)dims.size() - 1; if (n <= 0) return "";
    std::vector<std::vector<ll>> f(n, std::vector<ll>(n, 0)); std::vector<std::vector<int>> s(n, std::vector<int>(n, 0));
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) {
        int j = i + len - 1; f[i][j] = LLONG_MAX;
        for (int k = i; k < j; ++k) { ll c = f[i][k] + f[k + 1][j] + dims[i] * dims[k + 1] * dims[j + 1]; if (c < f[i][j]) { f[i][j] = c; s[i][j] = k; } }
    }
    struct B { static std::string go(const std::vector<std::vector<int>>& s, int i, int j) {
        if (i == j) return "A" + std::to_string(i + 1);
        return "(" + go(s, i, s[i][j]) + go(s, s[i][j] + 1, j) + ")"; } };
    return B::go(s, 0, n - 1);
}
// Merging piles in a row: merging two ADJACENT piles costs their total size.  Cheapest way to end with one pile.
// f(i, j) = min over k of f(i, k) + f(k+1, j) + sum(i..j).  The sum is the same for every split, so it comes from prefix sums.
ll merge_piles(const std::vector<ll>& a) {
    int n = (int)a.size(); if (n <= 1) return 0;
    std::vector<ll> pre(n + 1, 0); for (int i = 0; i < n; ++i) pre[i + 1] = pre[i] + a[i];
    std::vector<std::vector<ll>> f(n, std::vector<ll>(n, 0));
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) {
        int j = i + len - 1; f[i][j] = LLONG_MAX;
        for (int k = i; k < j; ++k) f[i][j] = std::min(f[i][j], f[i][k] + f[k + 1][j]);
        f[i][j] += pre[j + 1] - pre[i];
    }
    return f[0][n - 1];
}
```

```
dimensions 40 20 30 10 30: four matrices A1..A4
  A1..A1: 0
  A2..A2: 0
  A3..A3: 0
  A4..A4: 0
  A1..A2: 24000, best split after A1
  A2..A3: 6000, best split after A2
  A3..A4: 9000, best split after A3
  A1..A3: 14000, best split after A1
  A2..A4: 12000, best split after A3
  A1..A4: 26000, best split after A3
minimum cost=26000, order ((A1(A2A3))A4) (code gives 26000)
```

The table shows why the best split is not obvious: for `A1..A3` the best is to split after `A1`, for `A2..A4` after `A3`, and for the whole chain after `A3`. The printed bracketing `((A1(A2A3))A4)` is the walk back through the remembered splits. On a tie the **smallest** `k` wins (the loop keeps the first strictly smaller value), and a test pins this with `1 1 1 1`, where every bracketing costs the same.

Measured. The plain recursion that tries every split without remembering results is entered exactly `3^(n-1)` times for `n` matrices: 243, 2,187, 19,683 and 177,147 calls for 6, 8, 10 and 12 matrices (the verification script checks the powers), while the table has only 21, 36, 55 and 78 cells. On 1500 random matrices the table version took 1383 / 1321 ms (cost 3,965,724). Merging 800 piles took 111 / 106 ms.

## 2. More interval DP: balloons, palindromes, the printer

**Burst balloons.** Bursting balloon `k` earns `nums[left] * nums[k] * nums[right]`, where left and right are its *current* neighbours, and the balloon disappears. Find the order that earns the most. If you think about which balloon bursts **first** in a range, the neighbours of the others change and the two sides depend on each other. So think about which bursts **last** in the open range `(l, r)`: at that moment every other balloon in the range is gone, so its neighbours are exactly `l` and `r`, and the two sides `(l, k)` and `(k, r)` are independent subproblems. Add a balloon of value 1 at each end (the sentinels) and

```
f(l, r) = max over k in (l, r) of  f(l, k) + f(k, r) + a[l] * a[k] * a[r]
```

**Palindrome partitioning II.** Cut a string into the fewest pieces so that every piece is a palindrome. There are two tables. The interval table `pal(i, j)` says whether `s[i..j]` is a palindrome: true when the two end characters match and the inside is a palindrome (the base cases are lengths 1 and 2). Then a prefix table `cuts(j)` = the fewest cuts for the first `j` characters: choose the last piece `s[i..j-1]`, which must be a palindrome, and add one cut. The same `pal` table also gives the **longest palindromic substring**.

**The strange printer.** A printer prints a run of one character over any range in one turn and can overwrite what is already there. What is the fewest number of turns to print a given string? Repeated neighbours need no extra turn, so merge them first. Then `f(i, j)` = turns for `s[i..j]`. Option one: print `s[i]` alone and solve the rest, `f(i+1, j) + 1`. Option two: for any later `k` with `s[k] == s[i]`, let the stroke that prints `s[i]` reach all the way to `k`; the stroke is shared, so the cost is `f(i, k-1) + f(k, j) - 1`.

```cpp
// Burst balloons: bursting balloon k earns nums[left] * nums[k] * nums[right] for its CURRENT neighbours.  Thinking about the FIRST
// burst is awkward (its neighbours change what the rest earn).  Think about the LAST burst k in the open range (l, r): when it goes,
// everything between l and r is gone, so its neighbours are exactly l and r, and the two sides (l, k) and (k, r) are independent.
// f(l, r) = max over k in (l, r) of f(l, k) + f(k, r) + a[l] * a[k] * a[r].  Two sentinels with value 1 are added at the ends.
ll burst_balloons(const std::vector<ll>& nums) {
    int n = (int)nums.size(); std::vector<ll> a(n + 2, 1); for (int i = 0; i < n; ++i) a[i + 1] = nums[i];
    std::vector<std::vector<ll>> f(n + 2, std::vector<ll>(n + 2, 0));
    for (int len = 2; len <= n + 1; ++len) for (int l = 0; l + len <= n + 1; ++l) {
        int r = l + len; for (int k = l + 1; k < r; ++k) f[l][r] = std::max(f[l][r], f[l][k] + f[k][r] + a[l] * a[k] * a[r]);
    }
    return f[0][n + 1];
}
// Palindrome partitioning II: the fewest cuts so that every piece is a palindrome.  Two tables.  An interval table pal(i, j) = s[i..j]
// is a palindrome, true when the end characters match and the inside is a palindrome (filled by length).  A prefix table
// cuts(j) = fewest cuts for the first j characters = min over i of cuts(i) + 1, over the palindromic pieces s[i..j-1].
int min_palindrome_cuts(const std::string& s) {
    int n = (int)s.size(); if (n == 0) return 0;
    std::vector<std::vector<char>> pal(n, std::vector<char>(n, 0));
    for (int len = 1; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) { int j = i + len - 1; pal[i][j] = s[i] == s[j] && (len <= 2 || pal[i + 1][j - 1]); }
    std::vector<int> cuts(n + 1, 0);
    for (int j = 1; j <= n; ++j) { cuts[j] = j - 1; for (int i = 0; i < j; ++i) if (pal[i][j - 1]) cuts[j] = std::min(cuts[j], i == 0 ? 0 : cuts[i] + 1); }
    return cuts[n];
}
// Longest palindromic substring from the same interval table; on a tie the leftmost of the longest wins.
std::string longest_palindrome(const std::string& s) {
    int n = (int)s.size(); if (n == 0) return "";
    std::vector<std::vector<char>> pal(n, std::vector<char>(n, 0)); int bestLen = 1, bestStart = 0;
    for (int len = 1; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) {
        int j = i + len - 1; pal[i][j] = s[i] == s[j] && (len <= 2 || pal[i + 1][j - 1]);
        if (pal[i][j] && len > bestLen) { bestLen = len; bestStart = i; }
    }
    return s.substr(bestStart, bestLen);
}
// Strange printer: a printer prints a run of one character over any range in one turn and may overwrite what is there.  Fewest turns
// to print s.  Repeated neighbours are merged first.  f(i, j) = turns for s[i..j].  Print s[i] on its own and solve the rest:
// f(i+1, j) + 1.  Or, for a later k with s[k] == s[i], let the first stroke of s[i] reach k: f(i, k-1) + f(k, j) - 1 (one stroke is shared).
int strange_printer(const std::string& s0) {
    std::string s; for (char c : s0) if (s.empty() || s.back() != c) s.push_back(c);
    int n = (int)s.size(); if (n == 0) return 0;
    std::vector<std::vector<int>> f(n, std::vector<int>(n, 0)); for (int i = 0; i < n; ++i) f[i][i] = 1;
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) {
        int j = i + len - 1; f[i][j] = f[i + 1][j] + 1;
        for (int k = i + 1; k <= j; ++k) if (s[k] == s[i]) f[i][j] = std::min(f[i][j], f[i][k - 1] + f[k][j] - 1);
    }
    return f[0][n - 1];
}
```

```
balloons 3 1 5 8 with sentinels 1 on both sides: positions 0..5 hold 1 3 1 5 8 1; coins for bursting everything strictly between l and r
  (0,2): 3, last burst at position 1
  (1,3): 15, last burst at position 2
  (2,4): 40, last burst at position 3
  (3,5): 40, last burst at position 4
  (0,3): 30, last burst at position 1
  (1,4): 135, last burst at position 3
  (2,5): 48, last burst at position 4
  (0,4): 159, last burst at position 1
  (1,5): 159, last burst at position 4
  (0,5): 167, last burst at position 4
answer f(0,5)=167 (code gives 167)
```

```
palindromic substrings of abbab (start index, then the lengths that are palindromes):
  start 0: 1 4
  start 1: 1 2
  start 2: 1 3
  start 3: 1
  start 4: 1
fewest cuts for each prefix (length 0..5): 0 0 1 1 0 1
min cuts=1 (code gives 1), longest palindromic substring: abba
```

```
strange printer on abcabc: turns for s[i..j] (rows i = 0..5, columns j = 0..5, - below the diagonal)
  i=0: 1 2 3 3 4 5
  i=1: - 1 2 3 3 4
  i=2: - - 1 2 3 3
  i=3: - - - 1 2 3
  i=4: - - - - 1 2
  i=5: - - - - - 1
turns=5 (code gives 5)
```

In the balloon trace, the whole range `(0,5)` has its last burst at position 4 (the balloon of value 8): everything to its left is burst first, with the neighbours `1` and `8`, and the last balloon earns `1 * 8 * 1 = 8`. In the printer trace, `abcabc` needs 5 turns, not 6: the two `a`s share one stroke (the split at `k = 3` gives `f(0,2) + f(3,5) - 1 = 3 + 3 - 1 = 5`), and no other pair of equal letters saves anything because the letters between them are all needed.

Measured. 800 random balloons took 142 / 141 ms (the best total is 288,870,285 coins, which fits in 64 bits). The strange printer on 400 random letters over four symbols needed 150 turns and took 5 / 5 ms; the palindrome cuts of 3000 letters over two symbols came to 451 in 33 / 32 ms.

## 3. Game DP: the stone game

Two players alternately take a pile from **either end** of a row of piles. Each wants the larger total. Define `f(i, j)` as the best value of *(my total minus the opponent's total)* for the player to move on piles `i..j`, assuming both play perfectly. Taking the left pile `a[i]` gains `a[i]`; then the **opponent** moves on `[i+1, j]` and gets an advantage `f(i+1, j)` over me, which counts against me:

```
f(i, j) = max( a[i] - f(i+1, j),  a[j] - f(i, j-1) )          f(i, i) = a[i]
```

Why the difference works as the state: the whole game is zero-sum, so one number per range captures everything about it, and the two players are the same player with the roles swapped. The first player wins or ties exactly when `f(0, n-1) >= 0`. This is the "predict the winner" problem.

The table reads only the ranges one shorter, so a **single array** suffices. When the array is updated in increasing `i` for a given length, `f[i]` still holds the range `[i, j-1]` and `f[i+1]` still holds `[i+1, j]`.

**The even-length theorem.** With an **even** number of piles the first player never loses, whatever the values. Colour the positions alternately odd and even. The first player can take every pile in the odd positions or every pile in the even positions, whichever sum is larger, by always leaving the opponent an end of the other colour. The tests check this on random even-length arrays (with negative values too).

```cpp
// Two players take turns taking a pile from either END of a row of piles; both play to maximise their own total.  State f(i, j) = the
// best (current player's total minus the other player's total) on the piles i..j.  Taking a[i] gains a[i] and then the OTHER player
// is the current player on [i+1, j], whose advantage counts against us: f(i, j) = max(a[i] - f(i+1, j), a[j] - f(i, j-1)).
// One array suffices: processing by increasing length, f[i] still holds [i, j-1] when it is updated and f[i+1] holds [i+1, j].
ll stone_game_diff(const std::vector<ll>& a) {
    int n = (int)a.size(); if (n == 0) return 0;
    std::vector<ll> f(a);
    for (int len = 2; len <= n; ++len) for (int i = 0; i + len - 1 < n; ++i) f[i] = std::max(a[i] - f[i + 1], a[i + len - 1] - f[i]);
    return f[0];
}
// "Predict the winner": the first player wins or ties when the difference is not negative.
bool predict_winner(const std::vector<ll>& a) { return stone_game_diff(a) >= 0; }
// Nim: any number of stones may be taken from ONE pile; the player who cannot move loses.  The first player wins exactly when the
// XOR of the pile sizes is not zero.
bool nim_first_wins(const std::vector<int>& piles) { int x = 0; for (int p : piles) x ^= p; return x != 0; }
// Sprague-Grundy: a pile in a game whose moves remove m stones (m in `moves`) behaves like a Nim pile of size grundy(n), where
// grundy(n) = mex { grundy(n - m) } over the legal moves (mex = the smallest non-negative integer not in the set).  Several piles
// are added by XOR, exactly as in Nim.
std::vector<int> grundy_values(int N, const std::vector<int>& moves) {
    std::vector<int> g(N + 1, 0);
    for (int n = 1; n <= N; ++n) {
        std::vector<char> seen(moves.size() + 2, 0);
        for (int m : moves) if (m <= n && g[n - m] < (int)seen.size()) seen[g[n - m]] = 1;
        int x = 0; while (seen[x]) ++x; g[n] = x;
    }
    return g;
}
bool sum_game_first_wins(const std::vector<int>& piles, const std::vector<int>& g) { int x = 0; for (int p : piles) x ^= g[p]; return x != 0; }
```

```
piles 1 5 233 7: best (mine minus theirs) for each range, by length
  length 1: [0]=1 [1]=5 [2]=233 [3]=7
  length 2: [0..1]=4 [1..2]=228 [2..3]=226
  length 3: [0..2]=229 [1..3]=-221
  length 4: [0..3]=222
difference=222, first player wins or ties: yes (code gives 222)
```

Measured. On 100,000 random even-length arrays (2 to 10 piles, values from -5 to 20) the first player lost in 0 cases, as the theorem says. On 100,000 random odd-length arrays (1 to 9 piles, values 1 to 9) the first player lost in 20,993 (21.0%): with an odd count there is no such guarantee. On 3000 piles the one-array version took 2.9 / 3.8 ms, using 3000 cells, against 50 / 49 ms for the whole table of 9,000,000 cells; both gave the difference 15,414.

## 4. Nim and the Sprague-Grundy idea

Some games cannot be solved range by range. In **Nim** there are several piles; a move takes any positive number of stones from **one** pile; the player who cannot move loses. A state is a set of pile sizes, and the number of states is huge. The classic answer: the first player wins exactly when the **XOR** of the pile sizes is not zero. (When the XOR is zero, every move makes it non-zero; when it is non-zero, some move makes it zero. The player who always moves to XOR zero eventually leaves the empty position to the opponent.)

**Sprague-Grundy** extends this to any game that is a collection of independent piles, each with its own set of allowed moves. Give each pile size `n` a **Grundy value** `g(n)` = the *mex* (smallest non-negative integer not present) of the Grundy values of the positions you can move to. A pile of that game behaves exactly like a Nim pile of size `g(n)`, and several piles combine by XOR. The Grundy values are a one-dimensional DP over the pile size; the combined game is solved by XORing the values. In Nim itself, where you may remove any number of stones, `g(n) = n` and the rule is the XOR of the sizes.

```
subtraction game with moves 1, 3, 4: Grundy value of a pile of 0..14 stones: 0 1 0 1 2 3 2 0 1 0 1 2 3 2 0
three piles 5 6 9: Grundy values 3 2 0, XOR=1, first player wins: yes
Nim piles 3 4 5: XOR=2, first player wins: yes
```

For the subtraction game with moves 1, 3 and 4, the values repeat with a period of 7: `0 1 0 1 2 3 2`. The tests compare the XOR rule against a search of the whole game tree for several piles and several move sets.

Measured. Grundy values up to 1,000,000 stones for the moves 1, 3 and 4 took 20 / 19 ms, and the period of 7 held for all of them. On 20,000 random positions of up to three piles, the XOR of the Grundy values agreed with the game-tree search in 20,000 of them.

## 5. Recognising the shape

| Signal in the statement | State | Split or move | Time |
|---|---|---|---|
| Cheapest way to combine a sequence two at a time | range `[i, j]` | the last combination, at `k` | O(n^3) |
| Order of removals where the neighbours matter | open range `(l, r)` | the **last** removal, at `k` | O(n^3) |
| Partition a string into pieces with a property | prefix, with a range table for the property | the last piece | O(n^2) |
| Fewest strokes or turns to produce a pattern | range | print one end alone, or share a stroke | O(n^3) |
| Two players take from the ends of a row | range | take the left or the right end | O(n^2) |
| Several independent piles, play to win or lose | pile size | Grundy value by mex, XOR across piles | O(N * moves) |

## 6. Why greedy fails here

For each problem there is a greedy rule that looks right. Measured on 100,000 random small instances each, a rule is "wrong" when its answer differs from the DP's:

- **Stone game, always take the larger end:** wrong on 18,781 (18.8%). Taking the larger end can open the larger pile to the opponent.
- **Burst balloons, always burst the smallest balloon first:** wrong on 53,195 (53.2%).
- **Palindrome cuts, always cut off the longest palindromic prefix:** wrong on 15,949 (15.9%).
- **Strange printer, answer = the number of runs of equal letters:** wrong on 52,995 (53.0%). Overwriting lets one stroke serve two runs.

## 7. Testing an interval or game DP

1. **A different method for every table.** Enumerate every bracketing, merge order, burst order or cut set. For the printer, search the space of boards directly: from the blank board, apply every print of one letter over one range, and record the number of turns to reach each string. The test then checks **every** string up to a length against that table, with no random sampling.
2. **Replay the answer.** The bracketing is parsed and evaluated, so a string that looks right but has the wrong cost or the wrong matrix order is caught.
3. **Properties.** The even-length stone game never loses. The XOR rule for Nim agrees with the Grundy values with `g(n) = n`. The matrix chain with equal dimensions costs `(n - 1) * d^3` in every order.
4. **Pins** for ties (the smallest split), sentinels, repeated neighbours, and the empty and one-element inputs.

## 8. What the tests check, and what they cannot

Of the 59 mutants, 55 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). Four survive, and each leaves every output the tests can see unchanged. The first run left five survivors; a pin for a tie in the matrix-chain table (`1 1 1 1` must give `(A1(A2A3))`, the smallest split) killed one. The reasons the other four stay:

- **Letting the palindrome table skip the inner check for length 3 as well as 2.** The inside of a length-3 range is a single character, which is always a palindrome, so the check it skips would have said yes.
- **Starting the prefix cut count at `j` instead of `j - 1`.** The one-character piece is always a palindrome, so the minimum is at most `cuts[j-1] + 1`, which is never more than `j - 1`; the starting value is never the final one.
- **Starting the longest-palindrome length at 0 instead of 1.** The first length-1 range (always a palindrome, starting at index 0) sets the same length and start.
- **Not merging repeated neighbours before the strange printer.** The recurrence handles an equal neighbour by itself (with `k = i + 1` it gives `f(i, i) + f(i+1, j) - 1 = f(i+1, j)`), so the merge is only a speed-up.

The large inputs have no brute-force reference; they are checked against independent computations (equal dimensions, all-ones balloons, all-equal piles, the period of the Grundy values). What the tests cannot do is prove that the state models the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass.

Mutants are chosen where a bug is plausible, not at random, so "55 of 59" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

1. **Filling the table row by row** instead of by length: a range is read before its parts exist.
2. **Choosing the first removal** in the balloon problem instead of the last; the neighbours then change under you.
3. **Forgetting the sentinels** (a balloon of value 1 at each end) or their effect on the loop bounds.
4. **Off-by-one in the split:** `k` runs over `[i, j-1]` for the closed range, over `(l, r)` for the open one.
5. **Missing the base cases** of the palindrome table (lengths 1 and 2) so the inner range is read before it exists.
6. **Adding the cost of the whole range inside the loop** over `k` for merging piles; it belongs once, after the minimum.
7. **Signs in the game recurrence:** the opponent's advantage is *subtracted*, `a[i] - f(i+1, j)`.
8. **Tracking two scores** where one difference is enough.
9. **Calling the greedy rule right** because it passed the samples; section 6 shows how often it fails.
10. **Applying the XOR rule** to a game whose piles are not independent.
11. **Forgetting the mex** is over the *positions you can move to*, including the empty set (the mex of nothing is 0).
12. **Assuming a tie wins** when the problem says the first player must win strictly.

## 10. Questions to answer out loud

1. What is the state of an interval DP, and why must the table be filled by length?
2. State the matrix-chain recurrence. What does `k` mean?
3. Why does the burst-balloons recurrence choose the *last* balloon to burst, not the first?
4. How do you get the parenthesisation back from the table, and how are ties broken?
5. Why is a second table needed for palindrome partitioning? What does each table store?
6. State the strange-printer recurrence and say what "sharing a stroke" means.
7. Why does one number (a score difference) describe a zero-sum game on a range?
8. Why can the stone-game table be reduced to a single array?
9. Why does the first player never lose with an even number of piles?
10. State the Nim rule and say why the XOR works.
11. What is a Grundy value, and what is a mex?
12. How would you solve a game where each pile is a subtraction game with moves 2 and 5?

## 11. Problems

### 11.1 On paper

**A.** Matrix chain for the dimensions `30 35 15 5 10 20 25`: the minimum cost and the bracketing.

**B.** Burst balloons: the best total for `2 3 4`, and for `1 2 3`.

**C.** Merging piles: the cheapest total for `4 1 1 4`, and for `1 1 1 1`.

**D.** Palindrome partitioning: the fewest cuts for `ababbbabbababa`; the longest palindromic substring of `abacdfgdcaba`.

**E.** The fewest turns of the strange printer for `aabbbaacc` and for `abab`.

**F.** The stone game on `3 9 1 2`: the best score difference for the first player. Does the first player win or tie on `2 4 55 6 8`?

**G.** The subtraction game with moves 1 and 2: write the Grundy values for piles of 0 to 6. With piles `4, 5, 6` does the first player win?

### 11.2 On LeetCode

For each: state what a range means, what the split or move means, and the fill order before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Burst Balloons | 312 | Section 2: the last burst |
| Palindrome Partitioning II | 132 | The two tables |
| Longest Palindromic Substring | 5 | The palindrome table, leftmost on a tie |
| Strange Printer | 664 | Sharing a stroke |
| Minimum Cost to Merge Stones | 1000 | Merging piles with a group size `k` |
| Stone Game | 877 | The even-length theorem |
| Predict the Winner | 486 | The difference table |
| Stone Game II | 1140 | A game whose move size depends on the state |
| Remove Boxes | 546 | Interval DP with an extra state |
| Guess Number Higher or Lower II | 375 | Minimax over a range |

### 11.3 In your editor

**A.** Write the minimum cost to merge stones when a move merges exactly `k` consecutive piles (the cost is still their total). When is the answer impossible, and what does the table need to remember?

**B.** Write Knuth's optimisation for merging piles, restricting `k` between the split of `[i, j-1]` and the split of `[i+1, j]`, and compare its time with the cubic table on 3000 piles.

**C.** Write the optimal binary search tree (given access frequencies, the cheapest tree) as an interval DP, and print the tree.

**D.** Write the stone game with the move "take 1 to M piles from either end" and print the best first move.

**E.** Write the Grundy values for a game where a move splits one pile into two non-empty piles of different sizes, up to 200 stones, and find the first few piles with value 0.

**F.** Write the longest palindromic subsequence as an interval DP and compare it with the LCS-with-the-reverse version of the previous tutorial.

### Answers

**A.** The minimum cost is 15,125, with the bracketing `((A1(A2A3))((A4A5)A6))`.

**B.** For `2 3 4` the best total is 36; for `1 2 3` it is 12.

**C.** For `4 1 1 4` the cheapest total is 18 (merge the two ones for 2, then add a 4 for 6, then the last 4 for 10: `2 + 6 + 10 = 18`). For `1 1 1 1` it is 8 (merge pairs for 2 and 2, then 4).

**D.** `ababbbabbababa` needs 3 cuts. The longest palindromic substring of `abacdfgdcaba` is `aba` (the leftmost of the longest).

**E.** `aabbbaacc` needs 3 turns (merge the repeats to `abac`: print `a` over the whole word, `b` in the middle, `c` at the end). `abab` needs 3.

**F.** For `3 9 1 2` the difference is 7: the first player takes the 2 on the right, leaving `3 9 1`, which gives the opponent an advantage of -5 (so the first player's advantage is `2 + 5`). For `2 4 55 6 8` the first player does not win or tie: the difference is negative.

**G.** The Grundy values for piles of 0 to 6 are `0 1 2 0 1 2 0`. The piles `4, 5, 6` have values 1, 2, 0, whose XOR is 3, which is not zero, so the first player wins.

## Cheat sheet

- **Interval DP:** state `f(i, j)`, fill **by length**, combine two shorter ranges at a split `k`.
- **Matrix chain:** `f(i,j) = min f(i,k) + f(k+1,j) + d[i] d[k+1] d[j+1]`. **Merge piles:** the same with `sum(i..j)` added once.
- **Burst balloons:** choose the **last** burst `k` in the open range `(l, r)`: `f(l,k) + f(k,r) + a[l] a[k] a[r]`, with sentinels of value 1.
- **Palindrome cuts:** a `pal(i, j)` table plus `cuts(j) = min cuts(i) + 1` over palindromic pieces. **Strange printer:** merge repeats; `f(i+1,j)+1` or share a stroke `f(i,k-1)+f(k,j)-1`.
- **Stone game:** `f(i,j) = max(a[i] - f(i+1,j), a[j] - f(i,j-1))`; the first player wins or ties when `f(0, n-1) >= 0`; an even number of piles never loses.
- **Nim:** the first player wins iff the XOR of the pile sizes is not zero. **Grundy:** `g(n) = mex{ g(n - m) }`; piles combine by XOR.
- **Greedy rules fail:** see section 6 for how often.
- **Test** with a different method, by replaying the answer, with properties, and with pins.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The interval recurrences, the stone-game difference, Nim's XOR rule and the Sprague-Grundy theorem are standard; the statements here are my own wording. The even-length argument by colouring positions is the standard one. Knuth's optimisation and the optimal binary search tree (section 11.3) are named from memory of the textbook account and are posed as exercises, not proved or measured here.
- Problem numbers and titles in section 11, and the samples of problems 312, 132, 5, 664, 877 and 486 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
