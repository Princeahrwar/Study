# DSA 10.03: Grid and Two-Sequence DP

*Part of the [Dynamic programming plan](00_plan.md) · Time: about 9 hours (5 reading, 4 problems) · You need: [DP fundamentals](01_dp_fundamentals.md) (state, transition, rolling rows) and [One-dimensional DP](02_one_dimensional_dp.md) (prefix states and the loop-order lesson). There is no companion demo file: every structure is listed in this doc

Almost every problem on two strings, and every problem on a grid, has the same state: **a pair of positions**. On a grid the pair is a cell `(i, j)`. On two strings `a` and `b` it is a pair of prefixes: the first `i` characters of `a` and the first `j` of `b`. The table `f(i, j)` is then filled from three neighbours: the cell above, the cell to the left, and the cell diagonally up and left. Which of the three you read, and what you do with them, is the whole difference between the problems in this tutorial:

| Problem | Reads | The idea |
|---|---|---|
| Minimum path sum | above, left | the cheaper of the two ways in |
| Maximal square | above, left, diagonal | the smallest of the three, plus one |
| Longest common subsequence | above, left, diagonal | diagonal plus one on a match, else the larger of above and left |
| Edit distance | above, left, diagonal | one plus the smallest of the three, diagonal free on a match |
| Distinct subsequences | above, diagonal | skip the character, or use it |
| Interleaving, wildcard, regular expression | above, left, diagonal | a boolean table of "can these prefixes be matched" |

By the end you can:

- write the table for each of these problems, say what a cell means in words, and fill it by hand on a small input;
- reduce a table to one row (and know exactly when the diagonal value must be saved before it is overwritten);
- walk a table backwards to recover the answer itself, not just its size: the path, the common subsequence, the edit script;
- explain why the plain recursion is exponential and the table is not, with measured call counts;
- tell apart a longest common subsequence, an edit distance and a count of subsequences, and pick the right one from a problem statement.

**Where the numbers come from.** The code was compiled with `-Wall -Wextra` and tested: 69,888 checks passed under AddressSanitizer with UBSan (GCC 13.3). The tests ran: minimum path sum and its path against every monotone path of 3000 random small grids, and at 600 by 700 against a plain table; maximal square against a check of every square; the longest common subsequence against enumeration of every subsequence of the first string, its reconstructed string against both inputs, and at 3000 by 3000 against a full table; the longest palindromic subsequence against enumeration; edit distance against a breadth-first search over strings (independent of any table), plus symmetry, the triangle inequality, the length bounds, and the edit script replayed on the first string to check it produces the second at exactly the distance; distinct subsequences against enumeration of every subset and, for 100 equal letters, against the binomial coefficient C(100, 50) computed separately; interleaving, wildcard and regular-expression matching against plain recursion on random strings and patterns; and pins for the cases random inputs rarely hit (empty strings, a single row or column, negative cell values, ties in the walk back, a target longer than both parts together, patterns made only of `*`). Then 55 hand-made mutants of the listings were run against the tests: **54 were caught and 1 are equivalent changes**; section 8 says which survived and what that means.

**Data and timing.** Instances are **synthetic** and seeded. Machine: a cloud container, Intel Xeon at 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are **run 1 / run 2**, one measurement per row per run, and a claim is made only when it holds in both; they are this machine's, not yours. Counts and answers are identical in both runs.

---

## 1. Grids: minimum path sum and the maximal square

**Minimum path sum.** Move only right or down from the top-left cell to the bottom-right and add up the cell values on the way. `f(i, j)` = the cheapest path that ends at `(i, j)`. The last step into `(i, j)` came from above or from the left, so `f(i, j) = g[i][j] + min(f(i-1, j), f(i, j-1))`. The first row and first column have only one way in. (Counting the paths instead of minimising their cost is the grid-paths problem of [DP fundamentals](01_dp_fundamentals.md): replace `min` by `+`.)

The table reads only the row above and the cell to the left, so one row is enough: `f[j]` holds the value from the row above until it is overwritten, and `f[j-1]` has already been overwritten with the value for the current row, which is exactly "left". To recover the path, keep the whole table and walk back from the corner, at each cell stepping to the smaller of the two predecessors.

**Maximal square.** In a grid of `'0'` and `'1'`, find the largest square made only of `'1'` and return its area. The state is `f(i, j)` = the side of the largest all-`'1'` square whose **bottom-right corner** is `(i, j)`. Naming the corner is the whole trick: a square of side `k` with its corner at `(i, j)` exists exactly when the three squares of side `k - 1` with corners at `(i-1, j)`, `(i, j-1)` and `(i-1, j-1)` all exist, and the cell itself is `'1'`. So

```
f(i, j) = 0                                       if the cell is '0'
f(i, j) = 1 + min(f(i-1, j), f(i, j-1), f(i-1, j-1))   if the cell is '1'
```

and the answer is the largest side, squared. In the one-row version the diagonal value would be lost when `f[j]` is overwritten, so it is saved first (`diag`, set from the value of `up` that was read before the overwrite).

```cpp
// Minimum path sum from the top-left to the bottom-right, moving only right or down.  State f(i,j) = cheapest path ending at (i,j).
// f(i,j) = g[i][j] + min(f(i-1,j), f(i,j-1)); the first row and column have only one way in.  Non-empty rectangular grid.
typedef std::vector<std::vector<ll>> Grid;
ll min_path_sum(const Grid& g) {   // one row of state: f[j] is "up" until it is overwritten, f[j-1] is "left"
    int R = (int)g.size(), C = (int)g[0].size(); std::vector<ll> f(C);
    for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) {
        if (i == 0 && j == 0) f[j] = g[0][0];
        else if (i == 0) f[j] = f[j - 1] + g[i][j];
        else if (j == 0) f[j] = f[j] + g[i][j];
        else f[j] = std::min(f[j], f[j - 1]) + g[i][j];
    }
    return f[C - 1];
}
struct PathRes { ll cost; std::vector<std::pair<int, int>> cells; };
PathRes min_path(const Grid& g) {   // keeps the whole table so the path can be walked back; on a tie it came from above
    int R = (int)g.size(), C = (int)g[0].size(); Grid f(R, std::vector<ll>(C));
    for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) {
        if (i == 0 && j == 0) f[i][j] = g[i][j];
        else if (i == 0) f[i][j] = f[i][j - 1] + g[i][j];
        else if (j == 0) f[i][j] = f[i - 1][j] + g[i][j];
        else f[i][j] = std::min(f[i - 1][j], f[i][j - 1]) + g[i][j];
    }
    PathRes r; r.cost = f[R - 1][C - 1]; int i = R - 1, j = C - 1; r.cells.push_back({i, j});
    while (i > 0 || j > 0) {
        if (i == 0) --j; else if (j == 0) --i; else if (f[i - 1][j] <= f[i][j - 1]) --i; else --j;
        r.cells.push_back({i, j});
    }
    std::reverse(r.cells.begin(), r.cells.end()); return r;
}
// Largest square of '1' in a 0/1 matrix, returned as an AREA.  State f(i,j) = side of the largest all-'1' square whose BOTTOM-RIGHT
// corner is (i,j).  f(i,j) = 1 + min(up, left, diagonal) if the cell is '1', else 0.
int maximal_square(const std::vector<std::string>& m) {
    if (m.empty()) return 0;
    int R = (int)m.size(), C = (int)m[0].size(), best = 0; std::vector<int> f(C + 1, 0);   // f[j+1] is column j; f[0] is the zero border
    for (int i = 0; i < R; ++i) {
        int diag = 0;   // the old value of f[j] before this row overwrote it: the diagonal neighbour
        for (int j = 0; j < C; ++j) {
            int up = f[j + 1];
            if (m[i][j] == '1') { f[j + 1] = 1 + std::min({f[j + 1], f[j], diag}); best = std::max(best, f[j + 1]); } else f[j + 1] = 0;
            diag = up;
        }
    }
    return best * best;
}
```

Trace of both, checked by running the code:

```
grid row 0: 1 3 1   cheapest to reach: 1 4 5
grid row 1: 1 5 1   cheapest to reach: 2 7 6
grid row 2: 4 2 1   cheapest to reach: 6 8 7
min path sum=7 (rolling row gives 7), path: (0,0) (0,1) (0,2) (1,2) (2,2)
```

```
matrix row 0: 10100   side of the largest square ending here: 1 0 1 0 0
matrix row 1: 10111   side of the largest square ending here: 1 0 1 1 1
matrix row 2: 11111   side of the largest square ending here: 1 1 1 2 2
matrix row 3: 10010   side of the largest square ending here: 1 0 0 1 0
largest side=2, area=4 (code gives 4)
```

In the first trace the cheapest path goes along the top row and down the last column, for a total of 7. In the second, the row-2 entry `2 2` is a square of side 2 whose corner is in the last two columns of the third row; nothing larger exists because the fourth row has only isolated ones.

Measured on a 4000 by 4000 grid of random costs: the whole-table version and the one-row version gave the same sum, 2,026,292, and the table took 146 / 104 ms against 56 / 54 ms for the single row, so the table was slower in both runs (it touches 16 million cells of memory instead of 4000). Walking the path back needs the table and took 93 / 102 ms; the path has 7,999 cells, which is `4000 + 4000 - 1`.

## 2. Longest common subsequence

A **subsequence** keeps the order but may skip characters (`"ace"` is a subsequence of `"abcde"`). The **longest common subsequence** (LCS) of `a` and `b` is the longest string that is a subsequence of both. `f(i, j)` = the LCS length of the first `i` characters of `a` and the first `j` of `b`. Look at the last characters, `a[i-1]` and `b[j-1]`:

- If they are **equal**, some longest common subsequence can use both as its last character, so `f(i, j) = f(i-1, j-1) + 1`. (Taking the match is never worse than skipping it.)
- If they **differ**, at least one of them is not used, so `f(i, j) = max(f(i-1, j), f(i, j-1))`.
- `f(0, j) = f(i, 0) = 0`.

The plain recursion on this recurrence enters the same `(i, j)` many times. The table computes each once. With two strings that share no letters, every call takes the `max` branch and splits in two, and the number of calls is `2 * C(2n, n) - 1` for strings of length `n` each (the formula is checked below against the counts).

```cpp
// Longest common subsequence.  State f(i,j) = LCS length of the first i characters of a and the first j of b.
// Equal last characters: f(i,j) = f(i-1,j-1) + 1.  Otherwise drop one of them: f(i,j) = max(f(i-1,j), f(i,j-1)).
// The plain recursion on the same recurrence repeats subproblems; calls counts how often it is entered.
int lcs_naive(const std::string& a, const std::string& b, int i, int j, ll& calls) {
    ++calls; if (i == 0 || j == 0) return 0;
    if (a[i - 1] == b[j - 1]) return 1 + lcs_naive(a, b, i - 1, j - 1, calls);
    return std::max(lcs_naive(a, b, i - 1, j, calls), lcs_naive(a, b, i, j - 1, calls));
}
int lcs_len(const std::string& a, const std::string& b) {   // one row: f[j] is "up", f[j-1] is "left", diag is saved before overwriting
    int m = (int)b.size(); std::vector<int> f(m + 1, 0);
    for (size_t i = 0; i < a.size(); ++i) {
        int diag = 0;
        for (int j = 1; j <= m; ++j) { int up = f[j]; f[j] = a[i] == b[j - 1] ? diag + 1 : std::max(f[j], f[j - 1]); diag = up; }
    }
    return f[m];
}
std::string lcs_string(const std::string& a, const std::string& b) {   // full table, walked back from the corner; on a tie it moves up
    int n = (int)a.size(), m = (int)b.size(); std::vector<std::vector<int>> f(n + 1, std::vector<int>(m + 1, 0));
    for (int i = 1; i <= n; ++i) for (int j = 1; j <= m; ++j) f[i][j] = a[i - 1] == b[j - 1] ? f[i - 1][j - 1] + 1 : std::max(f[i - 1][j], f[i][j - 1]);
    std::string r; int i = n, j = m;
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { r.push_back(a[i - 1]); --i; --j; } else if (f[i - 1][j] >= f[i][j - 1]) --i; else --j;
    }
    std::reverse(r.begin(), r.end()); return r;
}
// Longest palindromic subsequence of s = LCS of s and its reverse.
int lps_len(const std::string& s) { return lcs_len(s, std::string(s.rbegin(), s.rend())); }
```

```
LCS table of ABCBDAB (rows) and BDCABA (columns); first row is the empty prefix of ABCBDAB
    - B D C A B A
  - 0 0 0 0 0 0 0
  A 0 0 0 0 1 1 1
  B 0 1 1 1 1 2 2
  C 0 1 1 2 2 2 2
  B 0 1 1 2 2 3 3
  D 0 1 2 2 2 3 3
  A 0 1 2 2 3 3 4
  B 0 1 2 2 3 4 4
LCS length=4, one longest common subsequence: BCBA
```

Reading the table: the answer is the bottom-right cell. To get **one** longest common subsequence, walk back from the corner: on a match, take the character and move diagonally; otherwise move toward the larger neighbour. Many strings can tie, so the result depends on the tie rule; here a tie moves up, and the table above yields `BCBA` (another valid answer is `BDAB`).

The same table gives related answers for free. The **shortest common supersequence** has length `n + m - LCS`. The **longest palindromic subsequence** of `s` is the LCS of `s` and its reverse (`lps_len` in the listing). If you may only delete characters, the fewest deletions to make two strings equal is `n + m - 2 * LCS`.

Measured. With lengths 8, 10, 12 and 14, strings with disjoint letters made the recursion enter the function 25,739, 369,511, 5,408,311 and 80,233,199 times, which are exactly `2 * C(2n, n) - 1` (the verification script computes the binomial independently); the table has only 81, 121, 169 and 225 cells for the same sizes. Two random strings of length 14 over four letters took 864,509 calls (LCS length 7): the exponential cost is real even on ordinary input, only smaller. At length 5000 (random, four letters) the LCS length was 3,254; the full table (25,010,001 cells) took 91 / 73 ms and the one-row version 61 / 46 ms, slower in both runs for the table.

## 3. Edit distance

The **edit distance** (Levenshtein distance) from `a` to `b` is the fewest single-character **insertions, deletions and substitutions** that turn `a` into `b`. `f(i, j)` = the distance between the first `i` characters of `a` and the first `j` of `b`. The borders are forced: `f(i, 0) = i` (delete everything) and `f(0, j) = j` (insert everything). Then, for the last characters:

- **Equal:** nothing to do for them, `f(i, j) = f(i-1, j-1)`.
- **Different:** `1 + min(f(i-1, j-1), f(i-1, j), f(i, j-1))`: substitute `a[i-1]` by `b[j-1]` (diagonal), delete `a[i-1]` (above), or insert `b[j-1]` (left).

The borders matter more than they look: a table whose first row and column start at 0 instead of `j` and `i` computes something else entirely (section 9 gives the measured error rate).

```
edit distance table of horse (rows) to ros (columns)
    - r o s
  - 0 1 2 3
  h 1 1 2 3
  o 2 2 1 2
  r 3 2 2 2
  s 4 3 3 2
  e 5 4 4 3
distance=3, script (M keep, S substitute, D delete, I insert): SMDMD
```

The script `SMDMD` reads: substitute `h` by `r`, keep `o`, delete `r`, keep `s`, delete `e`. Three edits. The walk back records, at each cell, which of the three moves produced its value; `M` is a free diagonal step.

Edit distance is a **metric**: it is zero exactly for equal strings, symmetric, and it obeys the triangle inequality, and it is at least the difference in lengths and at most the longer length. The tests use these properties on random strings in addition to the independent breadth-first search, and they replay the script on the first string to confirm that the result is the second and that the number of non-`M` steps equals the distance.

Measured: two random strings of length 5000 over four letters have distance 2,613, computed in 55 / 38 ms with one row. A string of length 2000 against a copy of itself with 100 random edits applied has distance 90, no more than the number of edits made (some edits cancel or overlap); the one-row distance took 2.8 / 3.4 ms and the version that also builds the script took 11.1 / 13.1 ms.

## 4. Distinct subsequences

How many ways can `t` be chosen from `s` as a subsequence, where two ways differ if they use different **positions** of `s`? `"rabbbit"` contains `"rabbit"` three times (choose which two of the three `b`s to use). The state is `f(j)` = the number of ways to build the first `j` characters of `t` from the part of `s` read so far. For each new character `c` of `s`, and each `j` with `t[j-1] == c`, the character can be **used** as the `j`-th (add `f(j-1)`, the ways to build the shorter prefix before it) or **skipped** (keep the old `f(j)`).

```cpp
// Edit distance: fewest single-character insertions, deletions and substitutions that turn a into b.
// State f(i,j) = distance between the first i characters of a and the first j of b.  f(i,0) = i, f(0,j) = j.
// Equal last characters cost nothing: f(i,j) = f(i-1,j-1).  Otherwise 1 + min(substitute f(i-1,j-1), delete f(i-1,j), insert f(i,j-1)).
int edit_distance(const std::string& a, const std::string& b) {
    int m = (int)b.size(); std::vector<int> f(m + 1); for (int j = 0; j <= m; ++j) f[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        int diag = f[0]; f[0] = (int)i;
        for (int j = 1; j <= m; ++j) { int up = f[j]; f[j] = a[i - 1] == b[j - 1] ? diag : 1 + std::min({diag, up, f[j - 1]}); diag = up; }
    }
    return f[m];
}
// The alignment itself: M = keep, S = substitute, D = delete a character of a, I = insert a character of b.  Full table, walked back.
std::string edit_script(const std::string& a, const std::string& b) {
    int n = (int)a.size(), m = (int)b.size(); std::vector<std::vector<int>> f(n + 1, std::vector<int>(m + 1));
    for (int i = 0; i <= n; ++i) for (int j = 0; j <= m; ++j) {
        if (i == 0) f[i][j] = j; else if (j == 0) f[i][j] = i;
        else f[i][j] = a[i - 1] == b[j - 1] ? f[i - 1][j - 1] : 1 + std::min({f[i - 1][j - 1], f[i - 1][j], f[i][j - 1]});
    }
    std::string ops; int i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && a[i - 1] == b[j - 1]) { ops.push_back('M'); --i; --j; }
        else if (i > 0 && j > 0 && f[i][j] == f[i - 1][j - 1] + 1) { ops.push_back('S'); --i; --j; }
        else if (i > 0 && f[i][j] == f[i - 1][j] + 1) { ops.push_back('D'); --i; }
        else { ops.push_back('I'); --j; }
    }
    std::reverse(ops.begin(), ops.end()); return ops;
}
// Distinct subsequences: how many ways can t be chosen from s as a subsequence (positions differ => ways differ), modulo MOD.
// State f(j) = ways to build the first j characters of t from the part of s read so far.  Each character of s that equals t[j-1]
// can be used (add f(j-1)) or skipped (keep f(j)).  j runs DOWNWARD so that f(j-1) still holds the value from before this character.
ll distinct_subsequences(const std::string& s, const std::string& t) {
    int m = (int)t.size(); std::vector<ll> f(m + 1, 0); f[0] = 1;
    for (char c : s) for (int j = m; j >= 1; --j) if (t[j - 1] == c) f[j] = (f[j] + f[j - 1]) % MOD;
    return f[m];
}
```

```
ways to build the prefixes of rabbit (lengths 0..6) after each character of rabbbit
  start: 1 0 0 0 0 0 0
  after r: 1 1 0 0 0 0 0
  after a: 1 1 1 0 0 0 0
  after b: 1 1 1 1 0 0 0
  after b: 1 1 1 2 1 0 0
  after b: 1 1 1 3 3 0 0
  after i: 1 1 1 3 3 3 0
  after t: 1 1 1 3 3 3 3
distinct subsequences=3 (code gives 3)
```

Look at the loop in `distinct_subsequences`: `j` runs **downward**. The update `f[j] += f[j-1]` must see `f[j-1]` as it was **before** this character of `s` was processed, otherwise the same character is used twice for two different positions of `t`. Running `j` upward is a wrong answer that looks right on many inputs; section 9 measures how often. This is the same direction rule as the 0/1 knapsack, which the next tutorial treats in full: when the same array holds both the old and the new row, the direction of the loop decides which one you read.

The listing above holds the code for sections 3 and 4: `edit_distance`, `edit_script` and `distinct_subsequences`.

## 5. Matching: interleaving, wildcard, regular expressions

These three ask a yes/no question, so the table holds booleans: `f(i, j)` = "the first `i` characters of one thing can be matched against the first `j` of another".

**Interleaving string.** Is `c` a merge of `a` and `b` that keeps the order inside each? `f(i, j)` = the first `i` of `a` and the first `j` of `b` can build the first `i + j` of `c`. The last character of that prefix of `c`, `c[i+j-1]`, came from `a` (if it equals `a[i-1]` and `f(i-1, j)`) or from `b` (if it equals `b[j-1]` and `f(i, j-1)`). The lengths must add up first, or the table can accept a `c` that is too long.

**Wildcard matching.** `'?'` matches one character, `'*'` matches any string, including the empty one. `f(i, j)` = the first `i` characters of the text match the first `j` of the pattern. A `'*'` can match nothing (`f(i, j-1)`) or swallow one more character (`f(i-1, j)`); any other pattern character must match the text character and `f(i-1, j-1)`.

**Regular expressions with `.` and `x*`.** `'.'` matches one character, and `x*` means zero or more copies of `x` (a `'*'` always follows a character). When the pattern ends in `x*`: either zero copies (`f(i, j-2)`), or `x` matches the last text character and `f(i-1, j)` (one more copy). Otherwise the characters must match and `f(i-1, j-1)`. The first row needs care: an empty text matches `a*b*c*` but not `a`.

```cpp
// Interleaving strings: is c a merge of a and b that keeps the order inside each?  f(i,j) = the first i of a and j of b can build the
// first i + j of c: the last character of c came from a or from b.
bool is_interleave(const std::string& a, const std::string& b, const std::string& c) {
    int n = (int)a.size(), m = (int)b.size(); if (n + m != (int)c.size()) return false;
    std::vector<char> f(m + 1, 0); f[0] = 1;
    for (int j = 1; j <= m; ++j) f[j] = f[j - 1] && b[j - 1] == c[j - 1];
    for (int i = 1; i <= n; ++i) {
        f[0] = f[0] && a[i - 1] == c[i - 1];
        for (int j = 1; j <= m; ++j) f[j] = (f[j] && a[i - 1] == c[i + j - 1]) || (f[j - 1] && b[j - 1] == c[i + j - 1]);
    }
    return f[m];
}
// Wildcard matching: '?' is any one character, '*' is any string (also empty).  f(i,j) = first i of s match first j of the pattern.
bool wildcard_match(const std::string& s, const std::string& p) {
    int n = (int)s.size(), m = (int)p.size(); std::vector<std::vector<char>> f(n + 1, std::vector<char>(m + 1, 0)); f[0][0] = 1;
    for (int j = 1; j <= m; ++j) f[0][j] = f[0][j - 1] && p[j - 1] == '*';
    for (int i = 1; i <= n; ++i) for (int j = 1; j <= m; ++j)
        f[i][j] = p[j - 1] == '*' ? (f[i][j - 1] || f[i - 1][j]) : ((p[j - 1] == '?' || p[j - 1] == s[i - 1]) && f[i - 1][j - 1]);
    return f[n][m];
}
// Regular expressions with '.' (any one character) and 'x*' (zero or more of x).  The whole string must match.  A '*' must follow a
// character (a pattern that starts with '*' is not valid and simply fails).  f(i,j) = first i of s match first j of the pattern.
bool regex_match(const std::string& s, const std::string& p) {
    int n = (int)s.size(), m = (int)p.size(); std::vector<std::vector<char>> f(n + 1, std::vector<char>(m + 1, 0)); f[0][0] = 1;
    for (int j = 2; j <= m; ++j) f[0][j] = p[j - 1] == '*' && f[0][j - 2];
    for (int i = 1; i <= n; ++i) for (int j = 1; j <= m; ++j) {
        if (p[j - 1] == '*') { if (j >= 2) f[i][j] = f[i][j - 2] || ((p[j - 2] == '.' || p[j - 2] == s[i - 1]) && f[i - 1][j]); }
        else f[i][j] = (p[j - 1] == '.' || p[j - 1] == s[i - 1]) && f[i - 1][j - 1];
    }
    return f[n][m];
}
```

```
wildcard table of adceb (rows) against *a*b (columns): 1 means the prefix matches
    - * a * b
  - 1 1 0 0 0
  a 0 1 1 1 0
  d 0 1 0 1 0
  c 0 1 0 1 0
  e 0 1 0 1 0
  b 0 1 0 1 1
adceb matches *a*b: 1; aa matches a*: 1; ab against .*c: 0
```

In the wildcard table the column of the first `*` is `1` in every row, because that star can swallow any prefix of the text, and the last column is `1` only on the row of the final `b`, because the pattern ends in `b`.

The plain recursion for the regular expression is exponential in the same way as the LCS recursion. With the text `"a"` repeated 25 times and the pattern `a*a*a*a*a*a*b` (no match, because of the final `b`), the recursion was entered 1,642,472 times, while the table has 364 cells. This is the pattern behind the "catastrophic backtracking" of some regular-expression engines; the table (or an automaton) is what avoids it.

## 6. Recognising the shape

| Signal in the statement | State | Transition | Time, space |
|---|---|---|---|
| Move right or down on a grid, minimise or count | cell `(i, j)` | above and left | O(RC), one row |
| Largest square or rectangle block in a 0/1 grid | corner cell | min of three neighbours, plus one | O(RC), one row |
| Longest common subsequence, or the longest palindromic one | prefixes `(i, j)` | match: diagonal + 1; else max of above, left | O(nm), one row |
| Fewest edits, deletions or insertions to make strings equal | prefixes `(i, j)` | match: diagonal; else 1 + min of three | O(nm), one row |
| Count the ways one string appears in another | prefix of `t` only | use or skip, `j` downward | O(nm), one row |
| Can this string be built or matched from these parts | prefixes `(i, j)` | booleans from the three neighbours | O(nm), one row or table |
| Print the answer itself, not only its size | the same table, kept whole | walk back from the corner | O(nm) memory |

## 7. Testing a two-sequence DP

Three kinds of independent checks, in order of strength:

1. **A different method.** Enumerate every subsequence, every path, every square, or search the graph of strings (as the edit-distance test does). Such a check shares no logic with the table.
2. **Properties.** Symmetry, the triangle inequality, the length bounds, "the edit script transforms the first string into the second", "the common subsequence returned is a subsequence of both and has the claimed length". Properties catch errors even on inputs too large for a brute force.
3. **Pins.** The published samples, the empty strings, a single row or column, ties in the walk back (the tie rule is part of the behaviour), and the inputs where the naive version is wrong.

A table is simple to get *almost* right: a one-cell slip in the border or the diagonal changes a few answers out of many. Section 9 measures how often each plausible slip is visible on small random inputs, which is also why the random tests use small inputs and many of them.

## 8. What the tests check, and what they cannot

Of the 55 mutants, 54 were caught by at least one check (a wrong answer, a sanitizer report, a hang or a crash). One survives, and it leaves every output the tests can see unchanged. The first run left two survivors; a pin for an interleaving target that is longer than the two strings together (`"a"`, `"b"` and `"abx"` must fail) killed the other. The reason the last one stays:

- **Starting `diag` at 1 instead of 0 in the maximal-square row loop.** `diag` is used only at column 0, where it sits inside `min(up, left, diag)` next to the zero border `f[0]`. The minimum is 0 whatever `diag` is, so no output changes.

The large inputs have no brute-force reference; they are checked against independent computations (a full table, a binomial coefficient, a plain loop). What the tests cannot do is prove that the state models the problem: they compare the code with a brute force of the same definition, so a misreading of the problem statement would pass.

Mutants are chosen where a bug is plausible, not at random, so "54 of 55" is a statement about those edits and not a proof about the listings. Timings depend on the machine.

## 9. Common mistakes

Four of them were measured on 100,000 random small instances each. A mistake is "wrong" on an instance when its answer differs from the correct code's.

1. **Edit distance with the borders left at 0** (`f(i, 0) = f(0, j) = 0`): wrong on 84,127 of the instances (84.1%). Most pairs of different lengths are affected.
2. **Maximal square from only the cell above and the cell to the left** (no diagonal): wrong on 17,210 (17.2%). A hole at the diagonal corner is invisible without it.
3. **Distinct subsequences with `j` running upward:** wrong on 21,544 (21.5%). The same character of `s` gets used for two positions of `t`.
4. **Longest common subsequence by greedy matching** (for each character of `a`, take the first unused match in `b`): wrong on 13,121 (13.1%). Greedy takes an early match that blocks a longer one.

More:

5. **Forgetting to save the diagonal** when a table is reduced to one row: `diag` must be read before `f[j]` is overwritten.
6. **Off-by-one between prefix length and index:** `f(i, j)` is about `a[i-1]` and `b[j-1]`.
7. **Reading the answer from the wrong cell** (`f(n-1, m-1)` instead of `f(n, m)`).
8. **Substitution cost:** edit distance with a substitution costing 2 is a different problem (insertions and deletions only), equal to `n + m - 2 * LCS`.
9. **A tie rule not stated** when the answer is a string: several answers are correct.
10. **Matching a pattern that starts with `*`** in the regular-expression table: not a valid pattern, and `p[j-2]` would read before the start.
11. **Not checking the lengths** in interleaving before running the table.
12. **Counting without a modulus** in distinct subsequences: the count can exceed 64 bits.

## 10. Questions to answer out loud

1. State the meaning of `f(i, j)` for the LCS, for edit distance and for the maximal square, each in one sentence.
2. Which neighbours does each recurrence read, and why do the grid problems need the diagonal only sometimes?
3. Why is it safe to take the match whenever the last characters are equal in the LCS?
4. How do you reduce an LCS table to one row, and what exactly must be saved?
5. How do you recover the LCS itself? Why can two correct answers differ?
6. What do the three moves of edit distance correspond to in the table?
7. Why must the first row and column of the edit-distance table be `0, 1, 2, ...`?
8. Why does the loop over `j` run downward in distinct subsequences?
9. Why does the interleaving table need the length check?
10. Why is the plain recursion for the LCS exponential? Give the call count for two strings with no common letters.
11. How would you cut memory for an LCS that must also return the string? (The idea is problem 11.3 A.)
12. How do you turn the LCS into the longest palindromic subsequence, and into the shortest common supersequence?

## 11. Problems

### 11.1 On paper

**A.** Minimum path sum of `{{2,1,3},{4,8,2},{1,1,1}}` and the path; the largest square in `1101 / 1111 / 0111`.

**B.** The LCS of `AGGTAB` and `GXTXAYB` (length and one string); the longest palindromic subsequence length of `agbcba`.

**C.** The edit distance from `sunday` to `saturday`; from `abc` to `yabd`; the script from `cat` to `cut`.

**D.** The number of distinct subsequences: `ab` in `aabb`; `ab` in `abab`; `aaaa` in `aaa`.

**E.** Is `acbd` an interleaving of `ab` and `cd`? Is `acdb`? Is `adbc`?

**F.** Does `a*d` match `abcd` (wildcard)? Does `a?` match `abc` (wildcard)? Does `a*b` match `aab` (regular expression)? Does `.*c` match `ab`? Does `ab*` match `a`?

**G.** Fill the LCS table for `ABC` (rows) and `BAC` (columns).

### 11.2 On LeetCode

For each: say the state in words, the neighbours it reads, and whether one row is enough, before you write code. Numbers and titles are from memory; check a number before you rely on it.

| Problem | Number | What to practise |
|---|---|---|
| Minimum Path Sum | 64 | Section 1 |
| Maximal Square | 221 | Corner state and the diagonal |
| Triangle | 120 | A grid whose rows grow; one row in place |
| Dungeon Game | 174 | Fill the table backwards (from the end) |
| Longest Common Subsequence | 1143 | Section 2 |
| Uncrossed Lines | 1035 | The same recurrence in disguise |
| Delete Operation for Two Strings | 583 | `n + m - 2 * LCS` |
| Minimum ASCII Delete Sum for Two Strings | 712 | The LCS recurrence with weights |
| Shortest Common Supersequence | 1092 | The LCS table and a walk back |
| Longest Palindromic Subsequence | 516 | LCS with the reverse |
| Edit Distance | 72 | Section 3 |
| Distinct Subsequences | 115 | Section 4 |
| Interleaving String | 97 | Section 5 |
| Wildcard Matching | 44 | Section 5 |
| Regular Expression Matching | 10 | Section 5 |

### 11.3 In your editor

**A.** Reduce `edit_script` to O(n + m) memory with the divide-and-conquer idea (compute the last row forwards and backwards to find where the optimal path crosses the middle row, then recurse). It is the idea of Hirschberg's algorithm; check it against the table version on random strings.

**B.** Write the weighted edit distance (a different cost for insert, delete and substitute) and the Damerau variant that also allows swapping two adjacent characters. What changes in the table?

**C.** Write the longest common substring (contiguous), which resets to 0 on a mismatch. Compare it with the subsequence version on random strings and explain the difference in the recurrence.

**D.** Write the LCS of three strings with a three-dimensional table, and the same with two dimensions of state per layer. What does the memory become?

**E.** Print every longest common subsequence (all ties), not just one. How many can there be for strings of length 10?

**F.** Implement the regular-expression matcher as a table over the pattern's states (an automaton) and compare it with the recursion on the catastrophic pattern of section 5.

### Answers

**A.** The cheapest path is 9 along the top row and down the right column, through `(0,0) (0,1) (0,2) (1,2) (2,2)` (costs 2, 1, 3, 2, 1). The largest square in the matrix has side 2, so the area is 4.

**B.** The LCS length is 4 and one string is `GTAB`. The longest palindromic subsequence of `agbcba` has length 5 (`abcba`).

**C.** From `sunday` to `saturday`: 3 (insert `a` and `t`, substitute `n` by `r`). From `abc` to `yabd`: 2 (insert `y`, substitute `c` by `d`). The script from `cat` to `cut`: `MSM`.

**D.** `ab` in `aabb`: 4. `ab` in `abab`: 3. `aaaa` in `aaa`: 0, because the pattern is longer than the text.

**E.** `acbd`: yes (a, c, b, d). `acdb`: yes (a, then `cd`, then b). `adbc`: no, because `d` comes before `c` in the target but after it in `cd`.

**F.** `a*d` against `abcd`: yes. `a?` against `abc`: no (`?` matches one character, so the pattern covers two). `a*b` against `aab` (regular expression): yes. `.*c` against `ab`: no. `ab*` against `a`: yes (zero copies of `b`).

**G.** The table has rows for the prefixes of `ABC` and columns for the prefixes of `BAC`:

```
      -  B  A  C
  -   0  0  0  0
  A   0  0  1  1
  B   0  1  1  1
  C   0  1  1  2
```

The LCS length is 2 (for example `AC` or `BC`).

## Cheat sheet

- **State:** a pair of positions. On a grid it is the cell; on two strings it is a pair of prefixes `(i, j)`. Three neighbours: above, left, diagonal.
- **Minimum path sum:** `g + min(above, left)`. **Maximal square:** `1 + min(above, left, diagonal)` if the cell is `'1'`, else 0; the answer is the side squared.
- **LCS:** match: `diagonal + 1`; else `max(above, left)`. **Edit distance:** borders `i` and `j`; match: `diagonal`; else `1 + min(diagonal, above, left)`.
- **Distinct subsequences:** `f[j] += f[j-1]` when `t[j-1] == c`, `j` **downward**, modulo a prime.
- **Interleaving, wildcard, regular expression:** boolean tables; check lengths first; `*` has two cases.
- **One row:** keep `diag` (the old `f[j-1]` before overwrite) when the diagonal is read.
- **To print the answer:** keep the table and walk back from the corner; state the tie rule.
- **Related answers:** shortest common supersequence is `n + m - LCS`; deletions only is `n + m - 2 * LCS`; the longest palindromic subsequence is the LCS with the reverse.
- **Test** with a different method, with properties, and with pins.

---

## Sources

- All counts, exit codes and timings are measured here (container, GCC 13.3, `-O2`, two runs). The code is my own implementation, tested as described in the opening.
- The recurrences for the grid problems, the longest common subsequence, edit (Levenshtein) distance, distinct subsequences, interleaving and the two matching problems are standard; the statements here are my own wording. That the cheapest alignment can be found in linear memory by Hirschberg's divide-and-conquer idea is from memory of the textbook account; the problem in section 11.3 is posed, not proved here.
- Problem numbers and titles in section 11, and the samples of problems 64, 221, 1143, 72, 115, 97, 44 and 10 used in the tests, are LeetCode's, from memory; check a number before you rely on it.
