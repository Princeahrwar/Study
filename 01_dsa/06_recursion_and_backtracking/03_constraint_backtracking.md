# DSA 06.03: Constraint Backtracking

*Part of the [Recursion and backtracking plan](00_plan.md) · Time: about 8 hours (4 reading, 4 problems) · You need: [Subsets, permutations and combinations](02_subsets_permutations_and_combinations.md) (the template, pruning by counting, skipping equal branches) · Related: [Binary search and sorting](../03_binary_search_and_sorting/00_plan.md) (sorting before a search) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- write a search for a puzzle with rules (N-queens, Sudoku, word search, splitting a string, dividing numbers into equal groups) and check each rule at the moment you place a piece, not at the end;
- keep the "what is already taken" state in arrays or in the bits of an integer, and say what each costs;
- choose which decision to make next (the most constrained cell first) and in which order to try the numbers, and measure how much that saves;
- prune with arithmetic (length left, sum left) and skip choices that lead to the same search (equal buckets);
- say when a search is still exponential after all of this, and what to use instead.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 15,750 checks passed under AddressSanitizer with UBSan (GCC 13.3), against references such as the known N-queens counts (n = 1 to 12), a column-permutation brute force (n up to 8), an independent Sudoku solution counter that proves each test puzzle has exactly one solution, a path search that copies its visited set for word search, every cut of the digit string for IP addresses and every assignment of numbers to groups for partitioning. Call counts are exact and the same on every machine. The timings were measured on one machine: an x86-64 VM with 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, best of 3. The statements of the LeetCode problems are summarised from memory; titles were checked against pages that list them (see Sources), and no difficulty labels are given because none could be verified.

All listings need these headers: `<algorithm>`, `<functional>`, `<string>`, `<utility>` and `<vector>`. They use `using Ints = std::vector<int>;` and `using Board = std::vector<std::vector<char>>;`.

---

## 1. From listing to searching

### 1.1 Why the rules matter

In tutorial 02 every path of the decision tree was an answer or led to one: all subsets, all orderings. Now the problem has **rules**, most branches break one of them, and the question is how early you notice. Compare two ways to solve 8-queens (place 8 queens so none attacks another):

- **Generate and test.** Put one queen in each row and column (that is a permutation of the columns), then check the diagonals at the end. The tree of tutorial 02 has 109,601 calls for n = 8.
- **Check as you place.** Before putting a queen in a square, test that no queen already attacks it. The same search makes 2,057 calls.

Both find the same 92 solutions. The second is 53 times smaller because a branch that has already broken a rule is cut at once, together with everything below it. This is **pruning**, and it is the whole idea of the tutorial.

### 1.2 Four tools

| Tool | Idea | Where it appears |
|---|---|---|
| Check at placement | Test the new piece against the rules in O(1) using state you keep up to date | N-queens, Sudoku, word search |
| Order the work | Decide the most constrained thing first; try the most likely value first | Sudoku (cell order), partition (largest number first) |
| Prune by arithmetic | Stop when what is left cannot be enough or cannot fit | IP addresses (digits left), partition (sum left) |
| Skip equal branches | If two choices lead to the same search, try one | Partition (equal buckets), duplicates in tutorial 02 |

A search with only the first tool is already much better than generate and test. The others decide whether a hard instance finishes in milliseconds or in minutes, as the Sudoku numbers in section 3 will show.

The running time of a search is (number of calls) times (work per call). Pruning lowers the first factor; the shape of the state lowers the second. Section 2 walks the same pruned tree with arrays and with bits: the calls are identical and the time differs by a factor of 7.

---

## 2. N-Queens

Place `n` queens on an `n x n` board so that no two share a row, a column or a diagonal. LeetCode 51 asks for all boards, 52 for the number of boards.

### 2.1 The model

Rows are the levels: row 0 gets a queen, then row 1, and so on. That choice removes the row rule for free (one queen per row by construction) and makes the choice at each node "which column". Two queens at `(r1, c1)` and `(r2, c2)` share a diagonal when `r1 - c1 == r2 - c2` or `r1 + c1 == r2 + c2`. So three sets are enough:

- `cols[c]`: column `c` has a queen;
- `anti[r + c]`: the anti-diagonal with sum `r + c` has a queen (there are `2n - 1` of them);
- `diag[r - c + n - 1]`: the diagonal with difference `r - c` has a queen (shifted by `n - 1` so the index is not negative).

```cpp
// One queen per row. cols[c], anti[r + c] and diag[r - c + n - 1] say that column c,
// the anti-diagonal r + c or the diagonal r - c already has a queen.
struct Queens {
    int n;
    long long solutions = 0, calls = 0;
    std::vector<char> cols, anti, diag;
    Ints pos, first;                       // pos[r] = column of the queen in row r
    explicit Queens(int n_) : n(n_), cols(n_), anti(2 * n_ - 1), diag(2 * n_ - 1), pos(n_) {}
    void place(int r) {
        ++calls;
        if (r == n) { if (solutions++ == 0) first = pos; return; }
        for (int c = 0; c < n; ++c) {
            int u = r + c, d = r - c + n - 1;
            if (cols[c] || anti[u] || diag[d]) continue;
            cols[c] = anti[u] = diag[d] = 1;   // choose
            pos[r] = c;
            place(r + 1);                      // explore
            cols[c] = anti[u] = diag[d] = 0;   // unchoose
        }
    }
};
```

Choose sets three flags, unchoose clears the same three. The check `cols[c] || anti[u] || diag[d]` costs O(1). The tree for `n = 4`, written as the column of the queen in each row so far (`02` means row 0 has its queen in column 0 and row 1 in column 2):

```text
start
  0
    02  (dead end)
    03
      031  (dead end)
  1
    13
      130
        1302  <- solution
  2
    20
      203
        2031  <- solution
  3
    30
      302  (dead end)
    31  (dead end)
```

17 calls: 1 at the root, then 4, 6, 4 and 2 at depths 1 to 4. The two solutions, `1302` and `2031`, are mirror images. A dead end is a node where no column is free; it costs one call and prunes everything below it, which for the larger boards is most of the tree.

### 2.2 How much does checking early save?

| n | Solutions | Calls with checking | Calls of the permutation tree |
|---|---|---|---|
| 1 | 1 | 2 | 2 |
| 2 | 0 | 3 | 5 |
| 3 | 0 | 6 | 16 |
| 4 | 2 | 17 | 65 |
| 5 | 10 | 54 | 326 |
| 6 | 4 | 153 | 1,957 |
| 7 | 40 | 552 | 13,700 |
| 8 | 92 | 2,057 | 109,601 |
| 9 | 352 | 8,394 | 986,410 |
| 10 | 724 | 35,539 | 9,864,101 |

The solutions (the sequence 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200 for n = 1 to 12) were compared with a known list and, for n up to 8, with the column-permutation brute force. The right-hand column is the call count of the permutation tree of tutorial 02 (exact formula); a generate-and-test search would make at least that many calls. The saving grows with n: about 4 times at n = 4, 53 times at n = 8, 278 times at n = 10. The number of calls still grows about 4 times for each extra row, so this is still exponential; pruning changes the base, not the shape.

For LeetCode 51 you need the boards, not only a count: when `r == n`, build `n` strings from `pos` (row `r` has `Q` at column `pos[r]` and `.` elsewhere) and push them. For 52 only `solutions` is needed.

### 2.3 State in the bits of an integer

The three flag arrays have at most 31 entries each for `n` up to 16. Store the three sets as the bits of three `unsigned` integers instead (`cols`, and `dr` and `dl` for the two diagonal directions):

```cpp
// The same search with the three sets as bits. Bit c of cols is "column c is attacked".
// dr: attacked by a queen's down-right diagonal; dl: by its down-left diagonal.
// Going down one row, dr moves one column right (shift left), dl one column left (shift right).
long long queensBits(unsigned cols, unsigned dr, unsigned dl, unsigned full, long long& calls) {
    ++calls;
    if (cols == full) return 1;
    long long total = 0;
    unsigned avail = full & ~(cols | dr | dl);
    while (avail) {
        unsigned bit = avail & (0u - avail);          // lowest set bit
        avail ^= bit;
        total += queensBits(cols | bit, ((dr | bit) << 1) & full, (dl | bit) >> 1, full, calls);
    }
    return total;
}
```

Read it line by line:

- `avail = full & ~(cols | dr | dl)` has a 1 for every column where a queen is allowed in this row, found with three OR operations instead of a loop over `n` columns.
- `avail & (0u - avail)` isolates the lowest set bit (a trick worth remembering: in two's complement, `-x` flips every bit above the lowest 1). Each loop turn takes one candidate, so the loop runs only over free squares.
- A queen attacks down-right and down-left. Going down one row, the square it attacks on its down-right diagonal is one column to the right, which is the next higher bit, so `dr` shifts left by one; on the down-left diagonal the square is one column to the left, so `dl` shifts right by one. `& full` drops bits that fall off the board. There is no unchoose: the new masks are passed as arguments, so the caller's masks are untouched.

The search is the same, node for node: the test checks that the call counts are equal for n = 1 to 12. Only the cost of a call changes.

| n | Solutions | Calls | Arrays and flags | Bits |
|---|---|---|---|---|
| 12 | 14,200 | 856,189 | 53 ms | 7 ms |
| 13 | 73,712 | 4,674,890 | 294 ms | 37 ms |
| 14 | 365,596 | 27,358,553 | not run | 236 ms |

The bit version is about 7 to 8 times faster here. Two lessons: the state's shape is worth a factor of several even when the tree is the same, and with bits the search goes to n = 14 in a quarter of a second. (A board and its mirror image are both solutions, which gives another factor of about 2: see the extra problem.)

---

## 3. Sudoku

Fill a 9 x 9 grid with the digits 1 to 9 so that every row, every column and every 3 x 3 box has each digit exactly once; some cells are given. LeetCode 36 only checks a partly filled grid; 37 solves it.

### 3.1 The state

Three arrays of nine integers, one per row, column and box. Bit `d` of `row[r]` is 1 when digit `d + 1` is already in row `r`. A cell `(r, c)` is in box `r / 3 * 3 + c / 3`. The candidates for a cell are the digits missing from all three:

```text
candidates = ~(row[r] | col[c] | box[b]) & 0x1FF
```

(`0x1FF` keeps the nine low bits.) Placing or removing a digit `d` is `^= 1 << d` on the three integers, which is why one `toggle` function does both choose and unchoose.

### 3.2 The listing

```cpp
bool isValidSudoku(const Board& b) {               // LeetCode 36: digits only, no repeats
    int row[9] = {}, col[9] = {}, box[9] = {};
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) {
            if (b[r][c] == '.') continue;
            int m = 1 << (b[r][c] - '1'), x = r / 3 * 3 + c / 3;
            if ((row[r] | col[c] | box[x]) & m) return false;
            row[r] |= m; col[c] |= m; box[x] |= m;
        }
    return true;
}

// Bit d of row[r] means digit d + 1 is used in row r; likewise columns and boxes.
struct Sudoku {
    Board& b;
    bool mrv;                                      // true: try the cell with fewest candidates
    int row[9] = {}, col[9] = {}, box[9] = {};
    long long calls = 0;
    Sudoku(Board& board, bool useMrv) : b(board), mrv(useMrv) {
        for (int r = 0; r < 9; ++r)
            for (int c = 0; c < 9; ++c)
                if (b[r][c] != '.') toggle(r, c, b[r][c] - '1');
    }
    void toggle(int r, int c, int d) { int m = 1 << d; row[r] ^= m; col[c] ^= m; box[r / 3 * 3 + c / 3] ^= m; }
    // false when no cell is empty; otherwise the chosen cell and its candidate mask
    bool pick(int& br, int& bc, int& bm) const {
        int best = 10;
        for (int r = 0; r < 9; ++r)
            for (int c = 0; c < 9; ++c) {
                if (b[r][c] != '.') continue;
                int m = ~(row[r] | col[c] | box[r / 3 * 3 + c / 3]) & 0x1FF;
                int k = __builtin_popcount(m);
                if (k < best) { best = k; br = r; bc = c; bm = m; if (!mrv) return true; }
            }
        return best != 10;
    }
    bool solve() {
        ++calls;
        int r = 0, c = 0, m = 0;
        if (!pick(r, c, m)) return true;
        while (m) {
            int d = __builtin_ctz(m);
            m &= m - 1;
            b[r][c] = (char)('1' + d); toggle(r, c, d);        // choose
            if (solve()) return true;                          // explore
            toggle(r, c, d); b[r][c] = '.';                    // unchoose
        }
        return false;
    }
};
```

`isValidSudoku` is the answer to LeetCode 36: one pass, three masks, and an early `false` as soon as a digit repeats. The solver follows the template:

1. `pick` chooses the cell to fill next. With `mrv` false it returns the **first empty cell** in row order. With `mrv` true it scans all empty cells and returns the one with the **fewest candidates** (MRV, minimum remaining values).
2. The loop over the set bits of `m` is the loop over the candidate digits (`__builtin_ctz` finds the index of the lowest set bit and `__builtin_popcount` counts the bits; both are GCC and Clang builtins, and C++20 spells them `std::countr_zero` and `std::popcount` in `<bit>`. `m &= m - 1` clears the lowest bit.)
3. A cell with no candidates makes `m` zero: the loop does not run and `solve` returns false. That is the dead end.

The solver fills the board in place, so on success the board holds the solution; on failure it is back to the start.

### 3.3 Order is the whole difference

Three puzzles, written with 0 for an empty cell. The solver found exactly one solution for each (counted by a separate program). They are well-known puzzles as I remember them, so treat the descriptions as labels, not as claims about their origin.

```text
A (30 clues)  530070000600195000098000060800060003400803001700020006060000280000419005000080079
B (21 clues)  800000000003600000070090200050007000000045700000100030001000068008500010090000400
C (17 clues)  000000000000003085001020000000507000004000100090000000500000073002010000000040009
```

A is the example grid most tutorials use. B is widely quoted as a very hard puzzle for humans. C is a 17-clue puzzle that is hard for a program that fills cells in row order.

| Puzzle | Calls, first empty cell | Calls, fewest candidates |
|---|---|---|
| A | 4,209 | 52 |
| B | 49,559 | 10,102 |
| C | 69,175,317 | 45,268 |

Puzzle C with the first-empty-cell order takes about 69 million calls, 2.8 to 3.5 seconds in two runs here; with the fewest-candidates order it takes 45 thousand calls and about 9 milliseconds, a 1,500-fold reduction in calls. Both programs check every placement against the same rules. The difference is only **which cell is decided next**.

Why does it work? A cell with one candidate has no real choice, so deciding it first costs nothing and may shrink other cells' candidates. A cell with zero candidates proves at once that the current partial grid is a dead end, instead of being found 20 levels deeper. The rule of thumb is **fail first**: decide the most constrained variable first, so wrong guesses are refuted near the root where the tree is narrow.

A stronger solver adds *constraint propagation* (not implemented here): fill every cell that has a single candidate, and every digit that fits only one cell of a row, column or box, before guessing at all.

---

## 4. Word search

### 4.1 One word (LeetCode 79)

Given a grid of letters and a word, is there a path of adjacent cells (up, down, left, right), each cell used at most once, that spells the word? Start the search from every cell. At each step the choice is the next of the four neighbours, the rule is that the cell's letter equals the next letter and the cell is not on the path already.

The "already on the path" set is the clever part: instead of a separate visited array, **overwrite the cell with `#`** while it is on the path and put the letter back afterwards. That is choose and unchoose done on the board itself, with no extra memory.

```cpp
// Word Search (LeetCode 79): the board itself is the visited set. '#' marks a cell on the path.
bool wordDfs(Board& g, const std::string& w, size_t k, int r, int c, long long& calls) {
    ++calls;
    if (g[r][c] != w[k]) return false;
    if (k + 1 == w.size()) return true;
    static const int dr[] = {1, -1, 0, 0}, dc[] = {0, 0, 1, -1};
    char saved = g[r][c];
    g[r][c] = '#';                                             // choose
    bool found = false;
    for (int d = 0; d < 4 && !found; ++d) {
        int nr = r + dr[d], nc = c + dc[d];
        if (nr < 0 || nc < 0 || nr >= (int)g.size() || nc >= (int)g[0].size()) continue;
        found = wordDfs(g, w, k + 1, nr, nc, calls);           // explore
    }
    g[r][c] = saved;                                           // unchoose
    return found;
}

bool wordExists(Board& g, const std::string& w, long long& calls) {
    for (int r = 0; r < (int)g.size(); ++r)
        for (int c = 0; c < (int)g[0].size(); ++c)
            if (wordDfs(g, w, 0, r, c, calls)) return true;
    return false;
}
```

The first line of `wordDfs` compares the letter before anything else: a wrong letter (or a `#`, which matches no letter) ends the call at once. The search returns as soon as one path is found; if the word is not there, it explores everything that matches a prefix. With a word of length L, every step after the first has at most 3 new neighbours (the one you came from is marked), so the work for one start cell is at most about `4 * 3^(L - 1)` calls; in practice the letter check cuts it far below that.

### 4.2 Many words (LeetCode 212)

With a list of words, running the search once per word repeats work: the words `oath` and `oat` walk the same first three cells. A **trie** (a tree with one edge per letter, where each path from the root spells a prefix) holds all the words; the board search walks the board and the trie together, and stops the moment the path is not a prefix of any word.

```cpp
// Word Search II (LeetCode 212): walk the board once, with all words in a trie.
// left = how many words below this node are not found yet. At 0, stop going deeper.
struct Trie {
    struct Node { int next[26]; int word = -1; int left = 0; Node() { std::fill(next, next + 26, -1); } };
    std::vector<Node> t{Node()};
    void add(const std::string& w, int id) {
        int cur = 0;
        for (char ch : w) {
            int i = ch - 'a';
            if (t[cur].next[i] < 0) { t[cur].next[i] = (int)t.size(); t.emplace_back(); }
            cur = t[cur].next[i];
            ++t[cur].left;
        }
        t[cur].word = id;
    }
};

// Returns how many words were newly found at or below this call.
int trieDfs(Board& g, int r, int c, int node, Trie& tr, std::vector<char>& hit, long long& calls) {
    ++calls;
    char ch = g[r][c];
    if (ch == '#') return 0;
    int nx = tr.t[node].next[ch - 'a'];
    if (nx < 0 || tr.t[nx].left == 0) return 0;               // no prefix, or nothing left to find
    int got = 0;
    if (tr.t[nx].word >= 0) { hit[tr.t[nx].word] = 1; tr.t[nx].word = -1; ++got; }
    static const int dr[] = {1, -1, 0, 0}, dc[] = {0, 0, 1, -1};
    g[r][c] = '#';
    for (int d = 0; d < 4; ++d) {
        int nr = r + dr[d], nc = c + dc[d];
        if (nr < 0 || nc < 0 || nr >= (int)g.size() || nc >= (int)g[0].size()) continue;
        got += trieDfs(g, nr, nc, nx, tr, hit, calls);
    }
    g[r][c] = ch;
    tr.t[nx].left -= got;
    return got;
}

std::vector<std::string> findWords(Board& g, const std::vector<std::string>& words, long long& calls) {
    Trie tr;
    for (size_t i = 0; i < words.size(); ++i) tr.add(words[i], (int)i);
    std::vector<char> hit(words.size(), 0);
    for (int r = 0; r < (int)g.size(); ++r)
        for (int c = 0; c < (int)g[0].size(); ++c)
            trieDfs(g, r, c, 0, tr, hit, calls);
    std::vector<std::string> out;
    for (size_t i = 0; i < words.size(); ++i) if (hit[i]) out.push_back(words[i]);
    return out;
}
```

(Tries get their own tutorial in the Trees area; here only what this search needs.) Each trie node knows `left`, the number of words below it that have not been found yet. When a word is found, its id is set once and `left` goes down along the path as the recursion returns; a node with `left == 0` is cut. That second cut matters, as the numbers below show.

| Board and words | Calls: trie, one board walk | Calls: one search per word |
|---|---|---|
| The 4 x 4 board and 4 words (`oath`, `pea`, `eat`, `rain`) of the LeetCode example, 2 found | 46 | 58 |
| A random 4 x 4 board of `a` and `b`, 20 random words of length 5 or 6, 19 found | 351 | 442 |

Without the `left` cut the trie search on the second board makes 2,391 calls, more than the separate searches (checked by removing that one condition). Why: this board is dense, almost every word exists, and the separate search stops at the first path it finds, while a trie walk that does not know the words below are all found keeps walking. With the cut the trie wins again. The lesson is general: **a search that shares work must also share the knowledge of when to stop**.

---

## 5. Splitting and grouping

### 5.1 Restore IP addresses (LeetCode 93)

Given a string of digits, insert three dots to make a valid IPv4 address: four parts, each 0 to 255, no leading zeros (`0` alone is fine, `01` is not). The search chooses the length (1, 2 or 3) of each part. Two prunings, both arithmetic:

- **Digits left.** With `remaining` parts still to place and `left` digits left, the string can be finished only if `remaining <= left <= 3 * remaining`. This test at the top of each call kills most branches, and rejects a string that is too short or too long in the first call.
- **Value and zeros.** A part that starts with `0` can only be the single digit `0`, so the loop breaks after length 1. A three-digit part must not be above `255`.

```cpp
// Restore IP Addresses (LeetCode 93): four pieces of 1 to 3 digits, value 0 to 255, no leading zero.
void ipParts(const std::string& s, size_t start, Ints& ends, std::vector<std::string>& out, long long& calls) {
    ++calls;
    int parts = (int)ends.size();
    size_t left = s.size() - start;
    if (parts == 4) {
        if (left == 0) {
            std::string ip;
            size_t from = 0;
            for (int e : ends) { if (from) ip += '.'; ip += s.substr(from, e - from); from = e; }
            out.push_back(ip);
        }
        return;
    }
    size_t remaining = 4 - parts;
    if (left < remaining || left > 3 * remaining) return;      // prune on length alone
    for (size_t len = 1; len <= 3 && start + len <= s.size(); ++len) {
        if (len > 1 && s[start] == '0') break;                 // no leading zero
        if (len == 3 && s.compare(start, 3, "255") > 0) continue;
        ends.push_back((int)(start + len));
        ipParts(s, start + len, ends, out, calls);
        ends.pop_back();
    }
}
```

For `"25525511135"` the answers are `255.255.11.135` and `255.255.111.35`, found in 19 calls. For `"0000"` the only answer is `0.0.0.0`, in 5 calls. For a string of thirteen 1s the first call fails the digit count (13 is more than `3 * 4`) and returns after 1 call.

### 5.2 Equal groups (LeetCode 698 and 473)

Can an array of positive integers be split into `k` groups with equal sums? (LeetCode 473, Matchsticks to Square, is the case `k = 4`: each stick must be used whole, and the four sides must be equal.)

First, arithmetic checks that need no search: the total must be divisible by `k`, and no number may exceed the target `total / k`. Then the template: process the numbers one by one, and the choice for a number is **which bucket it goes into**. A bucket may take it only if its sum stays within the target. When all numbers are placed, every bucket is exactly full, because the total is `k * target`.

```cpp
// Partition to K Equal Sum Subsets (LeetCode 698), all values positive. Put each number,
// largest first, into a bucket that still has room. With skipSame, a bucket whose sum
// equals an earlier bucket's sum is skipped: it would lead to the same search.
bool fill(const Ints& a, size_t i, Ints& bucket, int target, long long& calls, bool skipSame) {
    ++calls;
    if (i == a.size()) return true;
    for (size_t j = 0; j < bucket.size(); ++j) {
        if (bucket[j] + a[i] > target) continue;
        if (skipSame) {
            bool same = false;
            for (size_t p = 0; p < j; ++p) if (bucket[p] == bucket[j]) same = true;
            if (same) continue;
        }
        bucket[j] += a[i];
        if (fill(a, i + 1, bucket, target, calls, skipSame)) return true;
        bucket[j] -= a[i];
    }
    return false;
}

bool canPartition(Ints a, int k, long long& calls, bool skipSame) {
    long long sum = 0;
    for (int v : a) sum += v;
    if (k <= 0 || sum % k != 0) return false;
    int target = (int)(sum / k);
    std::sort(a.begin(), a.end(), std::greater<int>());
    if (a.empty() || a[0] > target) return false;
    Ints bucket(k, 0);
    return fill(a, 0, bucket, target, calls, skipSame);
}
```

Two ideas in `fill` make a large difference:

- **Largest first.** The big numbers have the fewest places to go; deciding them first fails early. (`canPartition` sorts in descending order.)
- **Skip equal buckets.** If two buckets have the same sum right now, putting the number into either gives the same situation up to swapping the buckets' names. So try only the first of each group of equal sums. This is the same rule as skipping equal values in tutorial 02, applied to buckets.

| Input | Answer | Calls without the skip | Calls with the skip |
|---|---|---|---|
| `4 3 2 3 5 2 1`, k = 4 | yes | 8 | 8 |
| `6 5 5 5 3 3 3 3 3`, k = 3 | no | 811 | 85 |
| `2 2 2 3 3 5 5 6 6 6 6 6`, k = 4 | no | 21,405 | 161 |

When the answer is yes, the search finds a packing quickly and the skip changes little (8 and 8). When the answer is no, the whole tree must be refuted, and equal buckets multiply the tree: 133 times fewer calls on the last row. That is the usual pattern: pruning helps the failing branches, and a failing instance is the one that tests it.

It is still exponential. Splitting a set into equal-sum groups is a known hard problem (for k = 2 it is the partition problem, NP-complete), so no clever pruning removes the exponent; inputs with many equal values or a tight target are the dangerous ones. When the array has at most about 16 numbers, a bitmask dynamic programming over the subsets already used (`2^n` states) is a different way, met in the DP area.

---

## 6. A checklist for a constraint search

1. **Decision and state.** What is one decision (a row, a cell, a number, a piece length)? A good choice removes a rule by construction (one queen per row). Which rules can be tested in O(1) with a flag, counter or mask?
2. **Order.** Most constrained decision first (the Sudoku cell with fewest candidates, the largest number). Within a decision, the likeliest value first if you want one solution.
3. **Prune.** Arithmetic on what is left (digits, sum, cells) and symmetry (mirror boards, equal buckets, equal values).
4. **One answer or all?** Return `true` up the stack at the first solution and stop; or count and keep going. On `false` the unchoose runs as usual.
5. **Measure.** Count calls on a small instance with and without each prune; keep the ones that pay.

---

## 7. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Updating flags in choose but forgetting to clear one in unchoose | Squares look attacked forever; missing solutions | Put all updates in one place, as `toggle` does |
| Diagonal index `r - c` without the shift | Negative index, undefined behaviour | Use `r - c + n - 1` |
| Returning after the first success in a count-all problem, or not returning in a find-one problem | Wrong count, or minutes wasted after the answer | Decide one or all before writing the return |
| Leaving `#` in the board after a failed path | Later starts see a damaged board | Restore the letter on every exit path (the `saved` variable) |
| Checking validity only at the leaf | Generate and test: 50 to 300 times more calls | Check when placing |
| Not checking leading zeros or `255` in IP parts | Invalid addresses such as `01.1.1.1` | Break on a leading zero, compare three digits with `255` |

---

## 8. Questions to answer out loud

1. Why is "check as you place" so much better than "generate then test"? Give the N-queens numbers for n = 8.
2. How do you index the two diagonals of a board, and why does the second one need a shift?
3. What does `avail & (0u - avail)` compute, and why does it work?
4. The array and the bit versions of N-queens make the same number of calls. Where does the factor of 7 come from?
5. What is MRV, and why does it make Sudoku's hard puzzle 1,500 times cheaper in calls?
6. Why mark the board with `#`? What can go wrong?
7. Why does a trie help Word Search II, and what does the `left` counter add?
8. Which pruning rule rejects `"1111111111111"` for Restore IP Addresses in one call?
9. Why skip buckets with equal sums? Why does the skip matter more when the answer is no?

---

## 9. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 9.1 On paper

**Problem A.** Draw the search tree for 4-queens as in section 2.1, but try the columns in the order 3, 2, 1, 0. How many calls, and which solution is found first?

*Where:* paper. *Goal:* the tree and the first solution. *How:* reuse the dead-end rule: no free column in a row.

**Problem B.** In 6-queens, after queens at `(0,1)`, `(1,3)`, `(2,5)`, which squares of row 3 are free? Write the three sets (columns, `r + c`, `r - c + 5`).

*Where:* paper. *Goal:* the free squares of row 3. *How:* mark each queen's column and two diagonal indices, then test each column.

**Problem C.** Compute `avail` for a 5-queens board (`full = 11111` in binary) with `cols = 00110`, `dr = 01100`, `dl = 00001`. Which column gets the queen first, and what are the three masks for the next row?

*Where:* paper. *Goal:* the lowest free bit and the shifted masks. *How:* `~(cols | dr | dl) & full`, isolate the lowest set bit, then shift `dr` left and `dl` right.

**Problem D.** Run `ipParts` by hand on `"1111"`. How many answers, and how many calls?

*Where:* paper. *Goal:* the answer count and the number of calls. *How:* at the top of each call test `remaining <= left <= 3 * remaining`.

**Problem E.** For each input say whether the arithmetic checks reject it before any search: `{1, 2, 3, 4}` with `k = 3`; `{7, 1, 1, 1}` with `k = 2`; `{3, 3, 3, 3, 4}` with `k = 4`. For the last one, what does the search do?

*Where:* paper. *Goal:* the check that rejects, and the search for the last. *How:* total divisible by `k`; largest number against the target `total / k`.

### 9.2 In your editor

For each, write a brute force first where one is easy, then the pruned search, and compare on small random inputs.

1. **N-Queens (51).** Where: LeetCode. Goal: all boards. How: the `pos` array to strings; then compare the count with the known sequence.
2. **N-Queens II (52).** Where: LeetCode. Goal: the count. How: the bit version; try n up to 14 and time it.
3. **Valid Sudoku (36).** Where: LeetCode. Goal: validate a partly filled grid. How: three masks, one pass.
4. **Sudoku Solver (37).** Where: LeetCode. Goal: solve in place. How: row, column and box masks; try the first-empty and the fewest-candidates order and count calls.
5. **Word Search (79).** Where: LeetCode. Goal: does the word exist. How: mark with a sentinel and restore; try a pre-check of letter counts.
6. **Word Search II (212).** Where: LeetCode. Goal: all words that exist. How: trie with the `left` counter; compare calls with 79 run per word.
7. **Restore IP Addresses (93).** Where: LeetCode. Goal: all valid addresses. How: the digit-count test and the leading-zero break.
8. **Partition to K Equal Sum Subsets (698).** Where: LeetCode. Goal: possible or not. How: sort descending, skip equal buckets; count calls on a failing input.
9. **Matchsticks to Square (473).** Where: LeetCode. Goal: possible or not. How: the same function with `k = 4`.

**Extra.** Use the mirror symmetry in N-queens: search only the first row's columns below `n / 2`, double the count, and add the middle column's count when `n` is odd. Check it against the plain count for n up to 9.

---

## Answers

**Problem A.** With the columns tried in the order 3, 2, 1, 0 the tree is the mirror image of the tree in section 2.1, so it has 17 calls, and the first solution found is `2031` (the mirror of `1302`).

**Problem B.** Queens at `(0,1)`, `(1,3)`, `(2,5)` take columns 1, 3, 5; `r + c` values 1, 4, 7; `r - c + 5` values 4, 3, 2. For row 3, column 0: `r + c = 3`, `r - c + 5 = 8`, free. Column 2: `r + c = 5`, `r - c + 5 = 6`, free. Column 4: `r + c = 7`, taken. So columns 0 and 2 are free. Columns 1, 3 and 5 are in `cols`, and column 4 has `r + c = 7`, which is taken.

**Problem C.** `cols | dr | dl = 01111`, so `avail = 10000`: only column 4 (bit 4) is free, so the first queen goes there (bit `10000`). New masks: `cols = 10110`, `dr = ((01100 | 10000) << 1) & 11111 = 11000`, `dl = (00001 | 10000) >> 1 = 01000`.

**Problem D.** `"1111"` has one answer, `1.1.1.1`, found in 10 calls. The root has three children (lengths 1, 2 and 3); only the length-1 child survives the digit test, because after a part of length 2 there are 2 digits left for 3 parts. The same happens at the next levels. Counting the calls that the digit test rejects at once gives 5 of the 10; the other 5 are the root, the three kept parts and the call that completes the address.

**Problem E.** `{1, 2, 3, 4}` with `k = 3`: the total is 10, not divisible by 3, so the answer is no without any search. `{7, 1, 1, 1}` with `k = 2`: the total is 10 and the target 5, but 7 is larger than 5, so no. `{3, 3, 3, 3, 4}` with `k = 4`: the total is 16 and the target 4, both checks pass. The search puts the 4 in one bucket and the 3s in three others, and the fourth 3 fits nowhere; that takes 5 calls with the equal-bucket skip and 65 without it (both run), and the answer is no.

**Problems 9.2.** All nine follow the listings. For 37 the call counts you should see are close to the table of section 3.3 (the order in which you try digits changes them a little). For 212, compare the trie with per-word search on a dense board as in section 4.2.

**Extra.** Searching columns `0 .. n/2 - 1` of the first row, doubling, and adding the count for the middle column when `n` is odd gives the same totals as the plain search for n = 1 to 9 (checked).

---

## Cheat sheet

- Check the rules when you place; generate-and-test is 50 to 300 times more calls on 8 to 10 queens.
- N-queens: row per level; flags `cols[c]`, `anti[r + c]`, `diag[r - c + n - 1]`; 2,057 calls and 92 solutions at n = 8.
- Bits: `avail = full & ~(cols | dr | dl)`; lowest bit `x & -x`; shift `dr` left, `dl` right; about 7 times faster than flags for the same tree.
- Sudoku: masks per row, column and box; candidates `~(r | c | b) & 0x1FF`; decide the cell with the fewest candidates first (1,500 times fewer calls on the hard puzzle).
- Word search: mark the cell with a sentinel while it is on the path, restore it after; a trie shares work across words, and a `left` counter stops finished branches.
- IP addresses: `remaining <= left <= 3 * remaining`; no leading zeros; at most 255.
- Equal groups: total divisible by `k`, largest number first, skip buckets with an equal sum; still exponential.
- One answer: return `true` up the stack. All answers: keep going.

---

## Sources

- Titles and numbers of 51, 36, 79, 212, 93, 473 and 698 were checked against pages that list them: [talentd.in NeetCode 150 sheet](https://www.talentd.in/fleetcode/sheets/neetcode-150) (51, 36, 79, 212), [github.com/goalong/lc](https://github.com/goalong/lc) (79, 93), [github.com/grandyang/leetcode issue 473](https://github.com/grandyang/leetcode/issues/473) and [issue 698](https://github.com/grandyang/leetcode/issues/698). Titles of 37 and 52 were seen in the search results [cicisun.github.io, "Sudoku Solver, N-Queens, N-Queens II"](https://cicisun.github.io/2015/09/06/9.4/) and [garylai.gitbook.io, "51. & 52. N Queens"](https://garylai.gitbook.io/algorithm-and-data-structure/problems/backtrack/n-queens). Difficulty labels are not used because I could not verify them.
- The sequence of N-queens solution counts, n = 1 to 12, is from memory and was checked by running the code.
- All other claims in this tutorial come from the tests and measurements described at the top.
