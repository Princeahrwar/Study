# DSA 01.06: Matrix Traversal and In-Place Tricks

*Part of the [Arrays and Strings plan](00_plan.md) · Time: about 4 hours (1.5 reading, 2.5 problems) · You need: tutorials [01](01_complexity_and_arrays_in_memory.md) and [02](02_two_pointers.md) (read and write indices, in-place work) · Companion code: [`06_matrix_and_inplace_demo.cpp`](06_matrix_and_inplace_demo.cpp)*

By the end of this tutorial you can:

- walk a matrix in any order (spiral, diagonals, rings) with borders that never go wrong, including one row, one column and the empty matrix;
- rotate and flip a square matrix in place, and say where every cell goes;
- use the input itself as scratch space: flags in the first row and column, signs as marks, values swapped to their "home" index;
- solve First Missing Positive in O(n) time and O(1) space, and Find the Duplicate Number without changing the array;
- compute the next permutation in place and explain why it is correct;
- say what each of these tricks costs (the input is changed, the code is fragile) and when to avoid them.

Every function in this tutorial is in the companion file, which compiles with `-Wall -Wextra -fsanitize=address,undefined` and checks each function against a brute force on random inputs.

---

## 1. Matrices in memory, and the checks to make first

A matrix with `m` rows and `n` columns is written `m[i][j]`: row `i` (0 to m − 1), column `j` (0 to n − 1). In C++ the usual type is `vector<vector<int>>`, where `m.size()` is the number of **rows** and `m[0].size()` the number of **columns**. Mixing these up is the most common matrix bug.

Memory layout decides speed. A built-in 2D array, and a matrix stored in one flat `vector`, are **row-major**: the element `(i, j)` is at offset `i × n + j`, so the cells of a row sit next to each other and the cells of a column are `n` elements apart (tutorial 01, section 2.4). A `vector<vector<int>>` is an array of separate row arrays: each row is contiguous, but different rows may be anywhere in memory. Either way, walking along a **row** is cheap and walking down a **column** is not.

Before writing a loop, check three shapes:

| Shape | What goes wrong |
|---|---|
| Empty matrix (`m.empty()` or `m[0].empty()`) | reading `m[0]` crashes |
| One row (`1 × n`) or one column (`m × 1`) | loops that go "back" over a row or column visit cells twice |
| Not square (`m ≠ n`) | code that swaps `(i, j)` with `(j, i)` runs off the matrix |

```cpp
using Matrix = std::vector<std::vector<int>>;
```

---

## 2. Traversal patterns

### 2.1 Spiral order with four borders (LeetCode 54)

Keep four numbers: the `top` row, `bottom` row, `left` column and `right` column of the part not yet visited. Walk the top row, the right column, the bottom row and the left column, then move every border one step inwards.

```cpp
// LeetCode 54: all cells in clockwise spiral order, using four shrinking borders.
std::vector<int> spiralOrder(const Matrix& m) {
    std::vector<int> out;
    if (m.empty() || m[0].empty()) return out;
    int top = 0, bottom = (int)m.size() - 1, left = 0, right = (int)m[0].size() - 1;
    while (top <= bottom && left <= right) {
        for (int j = left; j <= right; ++j) out.push_back(m[top][j]);              // top row, left to right
        for (int i = top + 1; i <= bottom; ++i) out.push_back(m[i][right]);        // right column, downwards
        if (top < bottom)
            for (int j = right - 1; j >= left; --j) out.push_back(m[bottom][j]);   // bottom row, right to left
        if (left < right)
            for (int i = bottom - 1; i > top; --i) out.push_back(m[i][left]);      // left column, upwards
        ++top; --bottom; ++left; --right;                                          // step into the next ring
    }
    return out;
}
```

Trace on the 3 × 4 matrix numbered 1 to 12 in row order:

```text
   1  2  3  4
   5  6  7  8
   9 10 11 12
```

| Ring | top / bottom / left / right | Visits |
|---|---|---|
| 1 | 0 / 2 / 0 / 3 | top row `1 2 3 4`; right column `8 12`; bottom row `11 10 9`; left column `5` |
| 2 | 1 / 1 / 1 / 2 | top row `6 7`; right column: none; bottom and left: skipped by the guards |

Output: `1 2 3 4 8 12 11 10 9 5 6 7`. The two `if` guards are what handle a ring that has shrunk to a **single row or column**. Ring 2 above is the single row `6 7`: without `top < bottom`, the bottom-row loop would walk the same row again and output the 6 a second time. The guards let the return trip happen only when it covers different cells. Cost: O(m × n) time, O(1) extra space besides the output.

### 2.2 Spiral order with a direction table

The border version only fits rings. A more general way to walk a grid is a **direction table**: the four moves as pairs `(dr, dc)` in turning order, a current direction, and the rule "go straight; if the next cell is outside or already visited, turn right".

```cpp
// The same order with a direction table: walk straight, turn right at a wall or a visited cell.
std::vector<int> spiralOrderDirections(const Matrix& m) {
    std::vector<int> out;
    if (m.empty() || m[0].empty()) return out;
    int rows = (int)m.size(), cols = (int)m[0].size();
    const int dr[4] = {0, 1, 0, -1}, dc[4] = {1, 0, -1, 0};    // right, down, left, up
    std::vector<std::vector<char>> seen(rows, std::vector<char>(cols, 0));
    int r = 0, c = 0, dir = 0;
    for (int step = 0; step < rows * cols; ++step) {
        out.push_back(m[r][c]);
        seen[r][c] = 1;
        int nr = r + dr[dir], nc = c + dc[dir];
        if (nr < 0 || nr >= rows || nc < 0 || nc >= cols || seen[nr][nc]) {
            dir = (dir + 1) % 4;                               // blocked: turn right
            nr = r + dr[dir];
            nc = c + dc[dir];
        }
        r = nr;
        c = nc;
    }
    return out;
}
```

Turning right is `dir = (dir + 1) % 4`. This version needs an `m × n` visited table (extra space), but the idea carries over to any path with turns and obstacles. The same pair of arrays gives the **neighbours** of a cell, which every grid problem needs:

```text
 4 neighbours:  dr = {0, 1, 0, -1}   dc = {1, 0, -1, 0}        (right, down, left, up)
 8 neighbours:  all (di, dj) with di, dj in {-1, 0, 1}, not both 0
```

Always test `0 <= r < rows && 0 <= c < cols` before touching a neighbour. Grid searches (islands, flood fill, shortest paths in a maze) in the graphs area are built on exactly this loop.

### 2.3 Diagonals: `i + j` and `i − j`

All cells on one **anti-diagonal** (running from the bottom left to the top right) have the same `i + j`. All cells on one **main diagonal** (top left to bottom right) have the same `i − j`. So "group the cells by diagonal" is "group them by `i + j`" or by `i − j`, and a check such as "every diagonal has equal values" (LeetCode 766) is a hash map or an array indexed by that number. The zig-zag order of LeetCode 498 walks the anti-diagonals one after another, in alternating directions:

```cpp
// LeetCode 498: the cells along each anti-diagonal (constant i + j), alternating direction.
std::vector<int> diagonalTraverse(const Matrix& m) {
    std::vector<int> out;
    if (m.empty() || m[0].empty()) return out;
    int rows = (int)m.size(), cols = (int)m[0].size();
    for (int d = 0; d < rows + cols - 1; ++d) {
        int iLow = std::max(0, d - cols + 1), iHigh = std::min(d, rows - 1);   // the rows that touch diagonal d
        if (d % 2 == 0)
            for (int i = iHigh; i >= iLow; --i) out.push_back(m[i][d - i]);    // up and to the right
        else
            for (int i = iLow; i <= iHigh; ++i) out.push_back(m[i][d - i]);    // down and to the left
    }
    return out;
}
```

For diagonal `d`, the valid rows run from `max(0, d − cols + 1)` to `min(d, rows − 1)` (the column is then `d − i`). On the 3 × 3 matrix numbered 1 to 9 the diagonals are `1`, `2 4`, `3 5 7`, `6 8`, `9`; the even ones are walked upwards and the odd ones downwards, giving `1 2 4 7 5 3 6 8 9`.

---

## 3. Rotating and flipping a square matrix in place (LeetCode 48)

Every flip or rotation moves cell `(i, j)` of an `n × n` matrix to a fixed place:

| Operation | Cell `(i, j)` goes to | In place, with |
|---|---|---|
| Transpose | `(j, i)` | swap each pair above the diagonal |
| Rotate 90° clockwise | `(j, n − 1 − i)` | transpose, then reverse **each row** |
| Rotate 90° counter-clockwise | `(n − 1 − j, i)` | transpose, then reverse the **order of the rows** |
| Rotate 180° | `(n − 1 − i, n − 1 − j)` | reverse the order of the rows, and reverse each row |
| Mirror left to right | `(i, n − 1 − j)` | reverse each row |
| Mirror top to bottom | `(n − 1 − i, j)` | reverse the order of the rows |

Why "transpose, then reverse each row" is a clockwise turn: the transpose sends `(i, j)` to `(j, i)`, and reversing a row sends column `j'` to `n − 1 − j'`, so `(j, i)` ends at `(j, n − 1 − i)`, which is the clockwise rotation. The code is two short loops:

```cpp
void transpose(Matrix& m) {                                    // square matrices only
    int n = (int)m.size();
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) std::swap(m[i][j], m[j][i]);   // (i, j) <-> (j, i), each pair once
}

// LeetCode 48: rotate 90 degrees clockwise, in place.
void rotateClockwise(Matrix& m) {
    transpose(m);
    for (auto& row : m) std::reverse(row.begin(), row.end());          // then mirror every row
}

void rotateCounterClockwise(Matrix& m) {
    transpose(m);
    std::reverse(m.begin(), m.end());                                  // then flip the order of the rows
}
```

The transpose loop starts the inner index at `i + 1` so each pair is swapped **once**; starting at 0 swaps every pair twice and the matrix comes back unchanged.

On the 3 × 3 matrix `1..9`: the transpose gives `[[1,4,7],[2,5,8],[3,6,9]]`, and reversing each row gives `[[7,4,1],[8,5,2],[9,6,3]]`, which is the original turned a quarter clockwise.

**A second method: four-cell cycles.** A rotation moves cells in groups of four: the top cell goes to the right, the right to the bottom, the bottom to the left, the left to the top. Handle the matrix one ring ("layer") at a time and rotate each group of four with one saved value:

```cpp
// The same clockwise rotation, one four-cell cycle at a time, layer by layer.
void rotateByCycles(Matrix& m) {
    int n = (int)m.size();
    for (int layer = 0; layer < n / 2; ++layer) {
        int first = layer, last = n - 1 - layer;
        for (int k = 0; k < last - first; ++k) {
            int saved = m[first][first + k];
            m[first][first + k] = m[last - k][first];                  // left   -> top
            m[last - k][first] = m[last][last - k];                    // bottom -> left
            m[last][last - k] = m[first + k][last];                    // right  -> bottom
            m[first + k][last] = saved;                                // top    -> right
        }
    }
}
```

Both methods are O(n²) time and O(1) space; the cycle version touches each cell once while transpose-and-reverse touches each cell about twice, but the second is shorter and much harder to get wrong. Learn the transpose version first and keep the table in your head.

**Not square.** A rotation of an `m × n` matrix is an `n × m` matrix, so there is no in-place swapping scheme; allocate the result (an in-place transposition of a non-square matrix exists, but it follows permutation cycles and is rarely asked):

```cpp
// A matrix that is not square cannot be rotated in place by these swaps: build the result.
Matrix rotateRectangle(const Matrix& m) {
    int rows = (int)m.size(), cols = (int)m[0].size();
    Matrix out(cols, std::vector<int>(rows));
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) out[j][rows - 1 - i] = m[i][j];   // (i, j) goes to (j, rows - 1 - i)
    return out;
}
```

Cache note: in a transpose, one of the two matrices is always accessed by column, so large transposes thrash the cache; CO 14, section 6.3 measures the blocked (tiled) transpose that fixes this, about 3 times faster for a 4096 × 4096 matrix of floats on the machine used there.

---

## 4. Set Matrix Zeroes (LeetCode 73)

*Question:* if a cell is 0, set its entire row and column to 0, in place.

You cannot zero the row and column the moment you see a 0, because those new zeros would trigger more zeroing. First **record** which rows and columns must be cleared, then clear them. The obvious record is two flag arrays:

```cpp
// LeetCode 73, first version: two flag arrays, O(rows + cols) extra space.
void setZeroesFlags(Matrix& m) {
    int rows = (int)m.size(), cols = (int)m[0].size();
    std::vector<char> zeroRow(rows, 0), zeroCol(cols, 0);
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            if (m[i][j] == 0) { zeroRow[i] = 1; zeroCol[j] = 1; }
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j)
            if (zeroRow[i] || zeroCol[j]) m[i][j] = 0;
}
```

O(m × n) time and O(m + n) extra space. To reach **O(1)** extra space, notice the matrix has a spare place to keep the flags: its own first row and first column. If `m[i][j]` is 0 (with `i, j ≥ 1`), set `m[i][0] = 0` ("row `i` must be cleared") and `m[0][j] = 0` ("column `j` must be cleared"). One problem remains: the first row and first column are used for flags, so their own original zeros must be remembered in two separate booleans first.

```cpp
// The same with O(1) extra space: the first row and first column hold the flags.
void setZeroes(Matrix& m) {
    int rows = (int)m.size(), cols = (int)m[0].size();
    bool firstRowZero = false, firstColZero = false;            // the flags themselves overwrite row 0 and column 0
    for (int j = 0; j < cols; ++j) if (m[0][j] == 0) firstRowZero = true;
    for (int i = 0; i < rows; ++i) if (m[i][0] == 0) firstColZero = true;
    for (int i = 1; i < rows; ++i)
        for (int j = 1; j < cols; ++j)
            if (m[i][j] == 0) { m[i][0] = 0; m[0][j] = 0; }     // flag row i and column j
    for (int i = 1; i < rows; ++i)
        for (int j = 1; j < cols; ++j)
            if (m[i][0] == 0 || m[0][j] == 0) m[i][j] = 0;
    if (firstRowZero) for (int j = 0; j < cols; ++j) m[0][j] = 0;   // last, or the flags would be destroyed early
    if (firstColZero) for (int i = 0; i < rows; ++i) m[i][0] = 0;
}
```

Trace on

```text
   0 1 2 0          0 0 0 0
   3 4 5 2    -->   0 4 5 0
   1 3 1 5          0 3 1 0
```

The first row and the first column contain zeros, so both booleans are true. The interior (rows 1 to 2, columns 1 to 3) has no zeros, so no flags are added. The clearing pass looks at `m[i][0]` and `m[0][j]`: `m[0][3]` is 0, so column 3 is cleared (`m[1][3]` and `m[2][3]`); `m[1][0]` and `m[2][0]` are not 0, so rows 1 and 2 stay. Finally the booleans clear the whole first row and the whole first column. On the 3 × 3 matrix of ones with a single 0 in the middle, the interior pass writes the flags `m[1][0] = m[0][1] = 0` and the result is the cross of zeros.

Order matters: the first row and column are cleared **last**. Doing it earlier destroys the flags that the interior still needs.

---

## 5. The input as its own table

Many problems promise that the values are in a range tied to the array's size (`1..n` for `n` numbers). Then every value is also a valid **index**, and the array can store information about itself: which values have appeared, which are missing, which are repeated. There are three ways to write that information into the array.

| Way | How | Cost |
|---|---|---|
| Move values to their home | swap each value `v` to index `v − 1` | the array ends up (nearly) sorted |
| Mark with the sign | negate `nums[v − 1]` when `v` is seen | values must be positive, so read with `abs` |
| Pack bits | store a second value in unused bits of the same cell | values must be small |

All three **change the input**. That is allowed only if the problem allows it (or if you restore it), it is not thread-safe, and it is easy to get wrong. Say so in an interview: "O(1) extra space, but it modifies the array".

### 5.1 First Missing Positive (LeetCode 41)

*Question:* find the smallest positive integer that is not in the array, in O(n) time and O(1) extra space. For `{3, 4, −1, 1}` the answer is 2; for `{1, 2, 0}` it is 3.

The answer is always between 1 and `n + 1` (`n` slots cannot hold `n + 1` different values, so if 1 to `n` are all present the answer is `n + 1`). So only the values in `1..n` matter, and everything else (zero, negatives, too large) can be ignored. Give each value `v` in that range a **home**: index `v − 1`. Walk the array and, whenever the value at position `i` is in range and not yet at its home, swap it there. Afterwards, the first position whose value is not `i + 1` tells you the answer.

```cpp
// LeetCode 41: the smallest positive integer that is not in nums. Rearranges nums.
int firstMissingPositive(std::vector<int>& nums) {
    int n = (int)nums.size();
    for (int i = 0; i < n; ++i)
        while (nums[i] >= 1 && nums[i] <= n && nums[nums[i] - 1] != nums[i])
            std::swap(nums[i], nums[nums[i] - 1]);              // send nums[i] to its home slot (value v lives at index v - 1)
    for (int i = 0; i < n; ++i)
        if (nums[i] != i + 1) return i + 1;                     // the first slot that holds the wrong value
    return n + 1;
}
```

Trace on `{3, 4, −1, 1}` (`n = 4`):

| Step | Array after the swap | What happened |
|---|---|---|
| `i = 0`, value 3 | `{−1, 4, 3, 1}` | 3 goes home to index 2; the −1 that was there arrives at index 0 and is out of range, so stop |
| `i = 1`, value 4 | `{−1, 1, 3, 4}` | 4 goes home to index 3; the 1 arrives at index 1 |
| `i = 1`, value 1 | `{1, −1, 3, 4}` | 1 goes home to index 0; the −1 arrives, out of range |
| `i = 2`, `i = 3` | unchanged | 3 and 4 are already home |

Scan: index 0 holds 1, index 1 holds −1, not 2, so the answer is **2**.

**Why it is O(n), though there is a `while` inside a `for`.** Every swap puts one value at its home index, and a value that is home never moves again (the loop stops when `nums[nums[i] − 1] == nums[i]`). So there are at most `n` swaps in total, and the scan costs `n` more. The same test also stops the loop when a **duplicate** is present: if the home slot already holds the same value, swapping would do nothing and the loop would never end. Using `if` instead of `while` leaves values in the wrong place.

### 5.2 Sign marking: duplicates and missing numbers

If the array has `n` values, all in `1..n`, then "value `v` was seen" can be stored as a **negative sign** on the element at index `v − 1`. When you meet `v` again, the sign is already negative, so `v` is a duplicate. (LeetCode 442: each value appears once or twice; return those that appear twice.)

```cpp
// LeetCode 442: every value 1..n appears once or twice; return the ones that appear twice.
std::vector<int> findDuplicates(std::vector<int>& nums) {
    std::vector<int> out;
    for (int x : nums) {
        int idx = std::abs(x) - 1;                              // x may already be negated by an earlier mark
        if (nums[idx] < 0) out.push_back(idx + 1);              // the slot is already marked: value idx + 1 was seen before
        else nums[idx] = -nums[idx];                            // mark "value idx + 1 has been seen" with the sign bit
    }
    for (int& x : nums) x = std::abs(x);                        // put the array back
    return out;
}
```

Read each value with `abs`, because an earlier mark may have made it negative. The last loop puts the array back. On `{4, 3, 2, 7, 8, 2, 3, 1}` the result is `{2, 3}`.

The same marking finds the numbers that never appear (LeetCode 448): mark every value seen, then the indices that are still positive are the missing values minus one.

```cpp
// LeetCode 448: nums has n values in 1..n; return the values in 1..n that do not appear.
std::vector<int> findDisappearedNumbers(std::vector<int>& nums) {
    for (int x : nums) {
        int idx = std::abs(x) - 1;
        if (nums[idx] > 0) nums[idx] = -nums[idx];
    }
    std::vector<int> out;
    for (int i = 0; i < (int)nums.size(); ++i) {
        if (nums[i] > 0) out.push_back(i + 1);                  // slot i was never marked: value i + 1 is missing
        else nums[i] = -nums[i];                                // put the array back
    }
    return out;
}
```

On the same array the missing values are `{5, 6}`. This only works because the values are in `1..n` and positive; a zero cannot be marked with a sign. A bitmap or a hash set does the same job without touching the input, at O(n) extra space.

### 5.3 Find the Duplicate Number (LeetCode 287)

*Question:* an array of `n + 1` numbers, each in `1..n`, so at least one value repeats. Find a repeated value **without modifying the array** and with O(1) extra space.

That rules out sorting (it moves things), negation (it changes things) and a hash set (O(n) space).

| Method | Time | Extra space | Changes the array? |
|---|---|---|---|
| Hash set | O(n) | O(n) | no |
| Sort, then compare neighbours | O(n log n) | O(1) or O(n) | yes (or a copy) |
| Sign marking (5.2) | O(n) | O(1) | yes |
| **Binary search on the value** | O(n log n) | O(1) | no |
| **Floyd's cycle detection** | **O(n)** | **O(1)** | **no** |

**Floyd's cycle detection.** Read the array as a linked list: from index `i`, the next index is `nums[i]`. Because the array holds `n + 1` entries but only `n` possible values, this walk starting at index 0 must eventually repeat an index, so it ends in a **cycle**. Index 0 itself is never in the cycle, since no value is 0. The first index of the cycle is reached from two different places (once from the walk before the cycle and once from inside it), which means that two array entries hold the same value, and that value is the duplicate.

Find the entrance of the cycle in two phases, with a slow and a fast pointer:

1. Move `slow` one step and `fast` two steps from index 0 until they meet (inside the cycle).
2. Put `slow` back at index 0, and move both one step at a time. They meet at the cycle's entrance.

```cpp
// LeetCode 287: n + 1 numbers in 1..n, so some value repeats. The array may not be changed.
int findDuplicate(const std::vector<int>& nums) {
    int slow = 0, fast = 0;                                     // treat i -> nums[i] as a linked list that starts at index 0
    do {
        slow = nums[slow];
        fast = nums[nums[fast]];                                // phase 1: fast moves two steps, slow one, until they meet in the cycle
    } while (slow != fast);
    slow = 0;
    while (slow != fast) {                                      // phase 2: both move one step; they meet at the cycle's entrance
        slow = nums[slow];
        fast = nums[fast];
    }
    return slow;
}
```

Trace on `{1, 3, 4, 2, 2}`. The walk from index 0 is `0 → 1 → 3 → 2 → 4 → 2 → 4 ...`: a tail `0, 1, 3` and a cycle `2, 4`.

| Phase | `(slow, fast)` after each round |
|---|---|
| 1 | `(1, 3)`, `(3, 4)`, `(2, 4)`, `(4, 4)`: they meet at 4 |
| 2 | start `(0, 4)`; then `(1, 2)`, `(3, 4)`, `(2, 2)`: they meet at 2 |

The duplicate is **2**.

**Why phase 2 works.** Let the tail have `μ` steps before the cycle starts and let the cycle have `λ` steps. When the pointers first meet, `slow` has taken `k` steps and `fast` `2k`; both are in the cycle, so their difference `k` is a multiple of `λ`, and `k ≥ μ`. A pointer that has taken `k` steps is `k − μ` steps past the entrance, so from the meeting point `μ` more steps bring it to position `k mod λ` past the entrance, which is the entrance itself. A pointer starting at index 0 reaches the entrance after exactly `μ` steps. Moving both together, they meet at the entrance.

**Binary search on the value.** Another way to use "n + 1 numbers in 1..n". For any `mid`, count how many numbers are `≤ mid`. If that count is more than `mid`, there are more numbers than places in `1..mid`, so (pigeonhole) the duplicate is in `1..mid`; otherwise it is above `mid`.

```cpp
// The same without chasing pointers: binary search on the value, using the pigeonhole principle.
int findDuplicateBinarySearch(const std::vector<int>& nums) {
    int lo = 1, hi = (int)nums.size() - 1;                      // the repeated value lies in lo..hi
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int atMost = (int)std::count_if(nums.begin(), nums.end(), [mid](int x) { return x <= mid; });
        if (atMost > mid) hi = mid;                             // more than mid numbers fit in 1..mid: the repeat is in there
        else lo = mid + 1;
    }
    return lo;
}
```

On `{1, 3, 4, 2, 2}`: the range 1 to 4, `mid = 2`: three numbers are `≤ 2` (1, 2, 2), more than 2, so the range becomes 1 to 2; `mid = 1`: one number is `≤ 1`, not more than 1, so the range becomes 2 to 2. The answer is 2. The binary search is easier to explain; Floyd is the one that gets O(n).

### 5.4 Two values in one cell: the Game of Life (LeetCode 289)

In the Game of Life every cell's next state depends on its eight neighbours' **current** states. If you overwrite cells while scanning, later cells would read the new values. The cure without a second grid is to store both generations in each cell: **bit 0 is the current state, bit 1 is the next state**. Read neighbours with `& 1`, write the new state into bit 1, and at the end shift everything right by one bit.

```cpp
// LeetCode 289: one step of the Game of Life, in place. Bit 0 holds the current state, bit 1 the next one.
void gameOfLife(Matrix& b) {
    int m = (int)b.size(), n = (int)b[0].size();
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) {
            int live = 0;
            for (int di = -1; di <= 1; ++di)
                for (int dj = -1; dj <= 1; ++dj) {
                    if (di == 0 && dj == 0) continue;
                    int r = i + di, c = j + dj;
                    if (r >= 0 && r < m && c >= 0 && c < n) live += b[r][c] & 1;   // read only bit 0: the current generation
                }
            if (live == 3 || (b[i][j] == 1 && live == 2)) b[i][j] |= 2;            // bit 1 = alive in the next generation
        }
    for (auto& row : b)
        for (int& x : row) x >>= 1;                             // the next generation becomes the current one
}
```

A live cell survives with 2 or 3 live neighbours, a dead cell is born with exactly 3. The condition `live == 3 || (b[i][j] == 1 && live == 2)` covers both rules at once. On `{{0,1,0},{0,0,1},{1,1,1},{0,0,0}}` the next generation is `{{0,0,0},{1,0,1},{0,1,1},{0,1,0}}`. In production code a second grid is clearer; the packed version is worth knowing as the general technique "keep the new value in spare bits of the old one".

---

## 6. Next Permutation (LeetCode 31)

*Question:* rearrange the numbers into the next larger arrangement in dictionary (lexicographic) order, in place. If the array is already the largest arrangement, turn it into the smallest. `{1, 2, 3}` becomes `{1, 3, 2}`; `{3, 2, 1}` becomes `{1, 2, 3}`.

Look at the arrangements of `{1, 2, 3}` in order: `123, 132, 213, 231, 312, 321`. Going from one to the next changes as **little as possible on the left** and re-sorts the right part. The algorithm:

1. From the right, find the **pivot**: the last index `i` with `a[i] < a[i + 1]`. Everything to its right is in non-increasing order.
2. From the right, find the first (rightmost) element `a[j]` that is **larger** than `a[i]`.
3. Swap `a[i]` and `a[j]`.
4. Reverse the part to the right of `i`.

If no pivot exists, the whole array is non-increasing, which is the largest arrangement; step 4 then reverses everything and gives the smallest.

```cpp
// LeetCode 31: rearrange into the next larger permutation; the largest wraps around to the smallest.
void nextPermutation(std::vector<int>& a) {
    if (a.size() < 2) return;                                   // nothing to rearrange
    int n = (int)a.size(), i = n - 2;
    while (i >= 0 && a[i] >= a[i + 1]) --i;                     // 1. the pivot: the last place where the order still goes up
    if (i >= 0) {
        int j = n - 1;
        while (a[j] <= a[i]) --j;                               // 2. the rightmost value that is larger than the pivot
        std::swap(a[i], a[j]);                                  // 3. swap them
    }
    std::reverse(a.begin() + i + 1, a.end());                   // 4. the tail was in decreasing order: make it increasing
}
```

Trace on `{2, 3, 6, 5, 4, 1}`: the pivot is 3 (index 1, since `3 < 6` and the part `6 5 4 1` is decreasing); the rightmost element larger than 3 is 4; swapping gives `{2, 4, 6, 5, 3, 1}`; reversing the part after index 1 (`6 5 3 1`) gives `1 3 5 6`, so the result is `{2, 4, 1, 3, 5, 6}`.

**Why it is correct.** The part to the right of the pivot is in non-increasing order, so it is already the *largest* arrangement of its elements. No change inside it can give a bigger permutation, and the pivot is the first place from the right where an increase is possible. To get the *smallest* bigger arrangement: put in the pivot's place the smallest element of the right part that is larger than the pivot (the rightmost larger one, because the part is decreasing), then arrange everything after it in the smallest possible way, which is increasing order. After the swap the right part is still non-increasing, so reversing it makes it increasing.

**Duplicates** need the comparisons `>=` in step 1 and `<=` in step 2, as in the code; with strict comparisons, equal neighbours confuse the pivot search. With `{1, 5, 1}` the pivot is index 0 and the result is `{5, 1, 1}`.

O(n) time, O(1) space. Related facts you may be asked:

- the **previous** permutation is the same algorithm with every comparison reversed;
- `std::next_permutation` does exactly this and returns `false` when it wraps around;
- a loop `do { ... } while (std::next_permutation(v.begin(), v.end()))` starting from a sorted array visits every distinct arrangement once, including arrangements of arrays with repeated values (4 elements `{1, 1, 2, 3}` give 12 arrangements, `4! / 2!`);
- the k-th permutation (LeetCode 60) can be built directly with factorials, without enumerating.

---

## 7. Where this shows up in systems

- **Layout and cache.** The same matrix is cheap to walk by rows and expensive to walk by columns; transposes, rotations and image processing are limited by memory access patterns, not by arithmetic. Tiling (CO 14) fixes it. Image pipelines and DMA engines do rotation as strided copies instead of cell swaps.
- **In-place and fixed memory.** Firmware often has no allocator or a few kilobytes of spare RAM. In-place rotation, flags stored in the data itself and bit packing are the same moves you will make there. The trade-off is the same as in the interview: no extra memory, but the data is changed and the code is easy to break.
- **Sequence numbers.** "First missing positive" is "the first gap in the sequence numbers received so far" (a receive window, a retransmission check, a log with holes). With a bitmap the answer is the first zero bit; the in-place swap version shows the same idea with the array as the bitmap.
- **Cycle detection.** Floyd's two pointers find loops in any structure where each element names the next: a corrupted free list, a hash chain that loops, a state machine that revisits a state, the period of a pseudo-random generator. The cost is O(1) memory, which matters when you cannot store the visited set.
- **Enumeration without recursion.** `next_permutation` generates every ordering of a small set in sorted order with no stack and no memory: useful to test every interleaving or every arrangement of a short list.

---

## 8. Choosing the tool

| The task | Use |
|---|---|
| Visit a grid ring by ring | four shrinking borders |
| Visit a grid along a path with turns, walls, visited cells | direction table and a visited table |
| Group cells by diagonal | `i + j` (anti-diagonal), `i − j` (main diagonal) |
| Rotate or flip a square matrix in place | transpose and reverse (or four-cell cycles) |
| Rotate a non-square matrix | build a new `n × m` matrix |
| Zero rows and columns in place | flags in the first row and column |
| Values in `1..n`, find missing or misplaced | swap to home index |
| Values in `1..n`, find duplicates or missing | sign marking (restore afterwards) |
| Find a repeat, array read-only, O(1) space | Floyd's cycle detection, or binary search on the value |
| Next arrangement in dictionary order | next permutation |
| New cell value needs old neighbour values | second buffer, or pack both values in one cell |

---

## 9. Mistakes that cost points

1. **Rows and columns swapped.** `m.size()` is the number of rows, `m[0].size()` the number of columns; check for an empty matrix first.
2. **Spiral without the single-row and single-column guards.** Cells are visited twice.
3. **Transposing with the inner loop from 0.** Every pair is swapped twice and nothing changes. Start at `i + 1`.
4. **Rotating a non-square matrix with swaps.** The shape changes; build a new matrix.
5. **Zeroing while scanning.** New zeros trigger more zeroing. Record first, clear later.
6. **Set Matrix Zeroes: clearing the first row and column before the interior,** or forgetting to remember their own zeros.
7. **Cyclic placement with `if` instead of `while`,** or without the "already home / duplicate" test (infinite loop).
8. **Sign marking on values outside `1..n`, or forgetting `abs`,** or not restoring the array.
9. **Find the Duplicate: modifying the array** when the problem forbids it, or starting phase 2 from the wrong place.
10. **Next permutation: strict comparisons with duplicates,** or forgetting to reverse the tail.
11. **Game of Life: reading a value that was already updated.** Read bit 0 only.

---

## 10. Questions you will be asked

**Rotate an image 90 degrees in place.** Transpose, then reverse each row. O(n²) time, O(1) space. Cell `(i, j)` goes to `(j, n − 1 − i)`.

**Why does transpose plus reverse give a rotation?** Transposing sends `(i, j)` to `(j, i)`, and reversing a row sends column `j'` to `n − 1 − j'`.

**Walk a matrix in spiral order.** Four borders that shrink after each ring; guard the bottom row and left column so a one-row or one-column ring is not walked twice.

**Set Matrix Zeroes in O(1) space.** Use the first row and column as flags, remember their own zeros in two booleans, clear the interior first and the first row and column last.

**First Missing Positive in O(n) time and O(1) space. Why is it O(n)?** Swap each value in `1..n` to index `value − 1`; every swap fixes one value for good, so at most `n` swaps. Then scan for the first index that does not hold `index + 1`.

**Find the duplicate without modifying the array and with O(1) space.** Treat `i → nums[i]` as a linked list and use Floyd's cycle detection, or binary search on the value with a count. Explain why the cycle entrance is the duplicate.

**Prove Floyd's phase 2.** The meeting point is `k` steps in, `k` a multiple of the cycle length; walking `μ` more steps from there returns to the entrance, and so does walking `μ` steps from the start.

**Next permutation: how and why?** Find the rightmost increase (pivot), swap it with the smallest larger value to its right, reverse the (decreasing) tail. The tail was the largest arrangement of its elements, so nothing smaller than the result exists.

**What if the matrix is too large for memory?** Work on tiles read from storage: rotation then becomes "read a tile, rotate it, write it to its destination tile", and the access pattern is blocked rather than strided.

**Why can transposing be slow, and what fixes it?** One of the two matrices is walked column by column, which misses the cache on every access. Process the matrix in tiles that fit in cache.

---

## 11. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). First ask: is this a path through a grid, a transformation of a grid, or a trick that uses the array itself?

### On paper

**Problem A: Walk it.** For the 4 × 3 matrix numbered 1 to 12 in row order (`1 2 3` / `4 5 6` / `7 8 9` / `10 11 12`) write (a) the spiral order and (b) the zig-zag diagonal order of section 2.3.
*Where:* paper. *How:* (a) draw the four borders and move them in; (b) list each anti-diagonal `i + j = d` and decide its direction.

**Problem B: Where does it go?** In a 4 × 4 matrix, where does the cell `(1, 3)` go after (a) a clockwise quarter turn, (b) a counter-clockwise quarter turn, (c) a half turn? Then write the four cells of the cycle that contains `(0, 1)` under the clockwise turn.
*Where:* paper. *How:* the table in section 3; for the cycle, apply the clockwise rule four times.

**Problem C: Home it.** Run the First Missing Positive loop of section 5.1 on `{7, 1, 2, 8, −1, 3}`. Write the array after every swap, then the answer.
*Where:* paper. *How:* `n = 6`, so only the values 1 to 6 have a home; the value `v` goes to index `v − 1`.

**Problem D: Next, next.** Give the next permutation of (a) `{1, 2, 3, 6, 5, 4}`, (b) `{5, 4, 3, 2, 1}`, (c) `{2, 2, 1}`, (d) `{1, 1, 5}`.
*Where:* paper. *How:* mark the pivot and the swap partner in each; the tail is reversed at the end.

**Problem E: Draw the list.** For `{3, 1, 3, 4, 2}` draw the walk `0 → nums[0] → ...` and mark the tail and the cycle. Which index is the cycle's entrance, and why is that the duplicate? Then run Floyd's two phases and write the `(slow, fast)` pairs.
*Where:* paper. *How:* section 5.3.

### In code

**Problem 1: Rotate Image** (LeetCode 48, medium)
*Where:* leetcode.com/problems/rotate-image. *Goal:* rotate an `n × n` matrix 90 degrees clockwise in place.
*How:* section 3: transpose, then reverse each row. Then solve it again with the four-cell cycles.

**Problem 2: Spiral Matrix** (LeetCode 54, medium)
*Where:* leetcode.com/problems/spiral-matrix. *Goal:* return the elements in spiral order.
*How:* section 2.1. Test a single row, a single column and a 1 × 1 matrix before submitting.

**Problem 3: Set Matrix Zeroes** (LeetCode 73, medium)
*Where:* leetcode.com/problems/set-matrix-zeroes. *Goal:* zero the rows and columns of every 0, in place.
*How:* section 4. First with two flag arrays, then in O(1) space.

**Problem 4: First Missing Positive** (LeetCode 41, hard)
*Where:* leetcode.com/problems/first-missing-positive. *Goal:* the smallest missing positive integer in O(n) time and O(1) extra space.
*How:* section 5.1. Put each value in `1..n` at its home index, then scan.

**Problem 5: Next Permutation** (LeetCode 31, medium)
*Where:* leetcode.com/problems/next-permutation. *Goal:* rearrange into the next larger permutation, in place.
*How:* section 6. Find the pivot, swap with the next larger value on its right, reverse the tail.

**Problem 6: Find the Duplicate Number** (LeetCode 287, medium)
*Where:* leetcode.com/problems/find-the-duplicate-number. *Goal:* the repeated number in `n + 1` values from `1..n`, without changing the array, in O(1) space.
*How:* section 5.3. Try binary search on the value first, then Floyd's cycle detection.

**More practice, once these feel easy:** Spiral Matrix II (59, fill instead of read), Diagonal Traverse (498), Transpose Matrix (867), Toeplitz Matrix (766), Game of Life (289), Find All Duplicates in an Array (442), Find All Numbers Disappeared in an Array (448), Missing Number (268), Permutation Sequence (60), Linked List Cycle II (142, the same Floyd on real nodes; Linked Lists 02), Search a 2D Matrix (74 and 240, in the binary search area). NeetCode's "Math & Geometry" and "Arrays & Hashing" lists contain 48, 54, 73 and 41 with video walkthroughs.

---

## Answers

Try before reading. LeetCode wraps these functions in `class Solution { public: ... };` and passes the vector by reference; the logic is the same.

### Problem A

(a) Spiral: `1 2 3 6 9 12 11 10 7 4 5 8`. The first ring is the border: `1 2 3`, `6 9 12`, `11 10`, `7 4`. The second ring is the single column `5 8`.
(b) Diagonals by `i + j`: `1`; `2 4`; `3 5 7`; `6 8 10`; `9 11`; `12`. Even diagonals are read upwards (bottom row first) and odd ones downwards: `1 | 2 4 | 7 5 3 | 6 8 10 | 11 9 | 12`, so the order is `1 2 4 7 5 3 6 8 10 11 9 12`.

### Problem B

(a) Clockwise: `(i, j) → (j, n − 1 − i)` = `(3, 2)`. (b) Counter-clockwise: `(n − 1 − j, i)` = `(0, 1)`. (c) Half turn: `(n − 1 − i, n − 1 − j)` = `(2, 0)`.
The clockwise cycle through `(0, 1)`: `(0, 1) → (1, 3) → (3, 2) → (2, 0) → (0, 1)`. These four cells exchange their values in one step of `rotateByCycles`.

### Problem C

| Step | Array |
|---|---|
| start | `{7, 1, 2, 8, −1, 3}` |
| `i = 1`: the 1 goes home to index 0 | `{1, 7, 2, 8, −1, 3}` |
| `i = 2`: the 2 goes home to index 1 | `{1, 2, 7, 8, −1, 3}` |
| `i = 5`: the 3 goes home to index 2 | `{1, 2, 3, 8, −1, 7}` |

The 7 and the 8 are larger than `n = 6`, and −1 is out of range, so they are never moved to a home. The scan finds index 3 holding 8, not 4, so the answer is **4**.

### Problem D

(a) The pivot is 3 (index 2), the partner is 4; swap to `{1, 2, 4, 6, 5, 3}`, then reverse `6 5 3`: **`{1, 2, 4, 3, 5, 6}`**.
(b) No pivot: **`{1, 2, 3, 4, 5}`**.
(c) No pivot (`2 >= 2`, `2 >= 1`): **`{1, 2, 2}`**.
(d) The pivot is the first 1 at index 1, the partner is 5; swap to `{1, 5, 1}`; reversing the one-element tail changes nothing: **`{1, 5, 1}`**.

### Problem E

The walk from index 0 is `0 → 3 → 4 → 2 → 3 → 4 → 2 → ...`. The tail is just `0`; the cycle is `3, 4, 2`, and its entrance is **3**. The entrance is reached from two places (index 0 holds 3, and index 2 holds 3), so the value 3 occurs twice: it is the duplicate. Floyd: phase 1 gives `(3, 4)`, `(4, 3)`, `(2, 2)` (they meet at 2); phase 2 starts at `(0, 2)` and gives `(3, 3)`, which is the answer 3.

### Problem 1: Rotate Image

`rotateClockwise(matrix)`, the function of section 3: transpose, then reverse each row. For `[[1,2,3],[4,5,6],[7,8,9]]` the result is `[[7,4,1],[8,5,2],[9,6,3]]`. `rotateByCycles` is the second solution (same result, one pass). Both are O(n²) time and O(1) space.

### Problem 2: Spiral Matrix

The function `spiralOrder` of section 2.1 is the answer. For a single row `{1, 2, 3}` it gives `1 2 3` (the guards prevent the return walk), and for the 3 × 4 example `1 2 3 4 8 12 11 10 9 5 6 7`. O(m × n) time.

### Problem 3: Set Matrix Zeroes

The function `setZeroes` of section 4 is the answer. For `{{1,1,1},{1,0,1},{1,1,1}}` it gives `{{1,0,1},{0,0,0},{1,0,1}}`. O(m × n) time, O(1) extra space; `setZeroesFlags` is the O(m + n) first version, which is a fine opening answer before you optimise.

### Problem 4: First Missing Positive

The function `firstMissingPositive` of section 5.1 is the answer. It returns 2 for `{3, 4, −1, 1}`, 3 for `{1, 2, 0}` and 1 for `{7, 8, 9, 11, 12}`. O(n) time (at most `n` swaps), O(1) space; it permutes the input.

### Problem 5: Next Permutation

The function `nextPermutation` of section 6 is the answer. `{1, 2, 3}` becomes `{1, 3, 2}`, `{3, 2, 1}` becomes `{1, 2, 3}`, `{1, 5, 1}` becomes `{5, 1, 1}`. O(n) time, O(1) space.

### Problem 6: Find the Duplicate Number

The function `findDuplicate` of section 5.3 (Floyd) is the answer, with `findDuplicateBinarySearch` as the simpler alternative. For `{1, 3, 4, 2, 2}` both give 2, for `{3, 1, 3, 4, 2}` both give 3, for `{3, 3, 3, 3, 3}` both give 3. Floyd: O(n) time, O(1) space, the array untouched.

---

## Cheat sheet

- **Check first:** empty matrix, one row, one column, rows versus columns.
- **Spiral:** borders `top, bottom, left, right`; four edges per ring; guard the bottom row with `top < bottom` and the left column with `left < right`. Or a direction table with a turn rule.
- **Diagonals:** `i + j` is constant on an anti-diagonal, `i − j` on a main diagonal.
- **Rotate (square):** clockwise = transpose, then reverse each row; counter-clockwise = transpose, then reverse the order of the rows; `(i, j) → (j, n − 1 − i)`. Transpose swaps with `j > i`. Non-square: allocate.
- **Set zeroes:** record first, clear later; O(1) space uses the first row and column as flags, two booleans for their own zeros, first row and column cleared last.
- **Values in `1..n` as indices:** swap to home (`while`, stop on "already home"), or mark with the sign (`abs` to read, restore at the end). First missing positive is in `1..n + 1`.
- **Find the duplicate (read-only, O(1)):** Floyd on `i → nums[i]` (meet, restart one pointer at 0, move both by one) or binary search on the value with a count.
- **Next permutation:** pivot = last `a[i] < a[i + 1]`; swap with the rightmost larger value; reverse the tail; no pivot means reverse everything. Use `>=` and `<=` for duplicates.
- **Packing:** keep the next state in spare bits and read the old state with a mask.
- **Say out loud:** "this uses O(1) extra space but modifies the input".

**Next:** tutorial 07, String Basics. Say "next" when you are ready.
