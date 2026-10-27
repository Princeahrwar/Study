// Tutorial 06: matrix traversal and in-place tricks. Every function the tutorial shows is here,
// followed by brute-force versions and randomized checks.
//
// Build and run:
//   g++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=address,undefined 06_matrix_and_inplace_demo.cpp -o demo && ./demo
//
// Expected last line: "<N>/<N> checks passed".

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <set>
#include <utility>
#include <vector>

static int g_checks = 0, g_passed = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (cond) ++g_passed;                                                \
        else std::printf("FAILED line %d: %s\n", __LINE__, #cond);           \
    } while (0)

// ---------------------------------------------------------------- section 2: traversal
using Matrix = std::vector<std::vector<int>>;

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

// ---------------------------------------------------------------- section 3: rotate and flip
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

// A matrix that is not square cannot be rotated in place by these swaps: build the result.
Matrix rotateRectangle(const Matrix& m) {
    int rows = (int)m.size(), cols = (int)m[0].size();
    Matrix out(cols, std::vector<int>(rows));
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) out[j][rows - 1 - i] = m[i][j];   // (i, j) goes to (j, rows - 1 - i)
    return out;
}

// ---------------------------------------------------------------- section 4: set matrix zeroes
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

// ---------------------------------------------------------------- section 5: the input as its own table
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

// ---------------------------------------------------------------- section 6: next permutation
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

// ---------------------------------------------------------------- brute forces
static Matrix randMatrix(std::mt19937& rng, int rows, int cols, int lo, int hi) {
    Matrix m(rows, std::vector<int>(cols));
    for (auto& row : m)
        for (int& x : row) x = std::uniform_int_distribution<int>(lo, hi)(rng);
    return m;
}

static Matrix numbered(int rows, int cols) {                    // 1, 2, 3, ... in row-major order
    Matrix m(rows, std::vector<int>(cols));
    int v = 1;
    for (auto& row : m)
        for (int& x : row) x = v++;
    return m;
}

// Independent spiral: take the first row, then rotate what is left counter-clockwise and repeat.
static std::vector<int> bruteSpiral(Matrix m) {
    std::vector<int> out;
    while (!m.empty() && !m[0].empty()) {
        out.insert(out.end(), m[0].begin(), m[0].end());
        m.erase(m.begin());
        if (m.empty()) break;
        int rows = (int)m.size(), cols = (int)m[0].size();
        Matrix next(cols, std::vector<int>(rows));
        for (int i = 0; i < rows; ++i)
            for (int j = 0; j < cols; ++j) next[cols - 1 - j][i] = m[i][j];   // counter-clockwise rotation
        m = next;
    }
    return out;
}

// Independent zig-zag: sort the cells by anti-diagonal, then by row up or down depending on the diagonal.
static std::vector<int> bruteDiagonal(const Matrix& m) {
    std::vector<std::pair<int, int>> cells;
    for (int i = 0; i < (int)m.size(); ++i)
        for (int j = 0; j < (int)m[0].size(); ++j) cells.push_back({i, j});
    std::sort(cells.begin(), cells.end(), [](auto a, auto b) {
        int da = a.first + a.second, db = b.first + b.second;
        if (da != db) return da < db;
        return da % 2 == 0 ? a.first > b.first : a.first < b.first;
    });
    std::vector<int> out;
    for (auto [i, j] : cells) out.push_back(m[i][j]);
    return out;
}

static Matrix bruteZeroes(const Matrix& m) {
    Matrix out = m;
    for (int i = 0; i < (int)m.size(); ++i)
        for (int j = 0; j < (int)m[0].size(); ++j)
            if (m[i][j] == 0) {
                for (int k = 0; k < (int)m[0].size(); ++k) out[i][k] = 0;
                for (int k = 0; k < (int)m.size(); ++k) out[k][j] = 0;
            }
    return out;
}

static int bruteFirstMissing(const std::vector<int>& nums) {
    std::set<int> s(nums.begin(), nums.end());
    int v = 1;
    while (s.count(v)) ++v;
    return v;
}

static Matrix bruteLife(const Matrix& b) {
    int m = (int)b.size(), n = (int)b[0].size();
    Matrix out = b;
    for (int i = 0; i < m; ++i)
        for (int j = 0; j < n; ++j) {
            int live = 0;
            for (int di = -1; di <= 1; ++di)
                for (int dj = -1; dj <= 1; ++dj)
                    if ((di || dj) && i + di >= 0 && i + di < m && j + dj >= 0 && j + dj < n) live += b[i + di][j + dj];
            out[i][j] = b[i][j] ? (live == 2 || live == 3) : (live == 3);
        }
    return out;
}

int main() {
    std::mt19937 rng(20261026);
    auto rnd = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    // examples quoted in the tutorial
    {
        Matrix g = numbered(3, 4);
        CHECK((spiralOrder(g) == std::vector<int>{1, 2, 3, 4, 8, 12, 11, 10, 9, 5, 6, 7}));
        CHECK((spiralOrderDirections(g) == std::vector<int>{1, 2, 3, 4, 8, 12, 11, 10, 9, 5, 6, 7}));
        Matrix h = numbered(3, 3);
        CHECK((diagonalTraverse(h) == std::vector<int>{1, 2, 4, 7, 5, 3, 6, 8, 9}));
        Matrix r = h;
        rotateClockwise(r);
        CHECK((r == Matrix{{7, 4, 1}, {8, 5, 2}, {9, 6, 3}}));
        Matrix t = h;
        transpose(t);
        CHECK((t == Matrix{{1, 4, 7}, {2, 5, 8}, {3, 6, 9}}));
        Matrix c = h;
        rotateCounterClockwise(c);
        CHECK((c == Matrix{{3, 6, 9}, {2, 5, 8}, {1, 4, 7}}));
        Matrix z{{1, 1, 1}, {1, 0, 1}, {1, 1, 1}};
        setZeroes(z);
        CHECK((z == Matrix{{1, 0, 1}, {0, 0, 0}, {1, 0, 1}}));
        Matrix z2{{0, 1, 2, 0}, {3, 4, 5, 2}, {1, 3, 1, 5}};
        setZeroes(z2);
        CHECK((z2 == Matrix{{0, 0, 0, 0}, {0, 4, 5, 0}, {0, 3, 1, 0}}));
        std::vector<int> v1{3, 4, -1, 1}, v2{1, 2, 0}, v3{7, 8, 9, 11, 12}, v4{1, 2, 3};
        CHECK(firstMissingPositive(v1) == 2 && firstMissingPositive(v2) == 3 && firstMissingPositive(v3) == 1 && firstMissingPositive(v4) == 4);
        CHECK((v1 == std::vector<int>{1, -1, 3, 4}));
        CHECK(findDuplicate({1, 3, 4, 2, 2}) == 2 && findDuplicate({3, 1, 3, 4, 2}) == 3 && findDuplicate({3, 3, 3, 3, 3}) == 3);
        CHECK(findDuplicateBinarySearch({1, 3, 4, 2, 2}) == 2 && findDuplicateBinarySearch({3, 1, 3, 4, 2}) == 3);
        std::vector<int> p1{1, 2, 3}, p2{1, 3, 2}, p3{3, 2, 1}, p4{1, 5, 1};
        nextPermutation(p1); nextPermutation(p2); nextPermutation(p3); nextPermutation(p4);
        CHECK((p1 == std::vector<int>{1, 3, 2}) && (p2 == std::vector<int>{2, 1, 3}) && (p3 == std::vector<int>{1, 2, 3}) && (p4 == std::vector<int>{5, 1, 1}));
        std::vector<int> d{4, 3, 2, 7, 8, 2, 3, 1}, e{4, 3, 2, 7, 8, 2, 3, 1};
        CHECK((findDuplicates(d) == std::vector<int>{2, 3}) && (d == e));
        CHECK((findDisappearedNumbers(e) == std::vector<int>{5, 6}) && (d == e));
        Matrix life{{0, 1, 0}, {0, 0, 1}, {1, 1, 1}, {0, 0, 0}};
        gameOfLife(life);
        CHECK((life == Matrix{{0, 0, 0}, {1, 0, 1}, {0, 1, 1}, {0, 1, 0}}));
        CHECK((rotateRectangle(numbered(2, 3)) == Matrix{{4, 1}, {5, 2}, {6, 3}}));
    }

    // traversal
    for (int t = 0; t < 500; ++t) {
        int rows = rnd(1, 6), cols = rnd(1, 6);
        Matrix m = randMatrix(rng, rows, cols, -9, 9);
        CHECK(spiralOrder(m) == bruteSpiral(m));
        CHECK(spiralOrderDirections(m) == bruteSpiral(m));
        CHECK(diagonalTraverse(m) == bruteDiagonal(m));
    }
    CHECK(spiralOrder({}).empty() && diagonalTraverse({}).empty());

    // rotations
    for (int n = 1; n <= 7; ++n) {
        Matrix m = numbered(n, n);
        Matrix cw(n, std::vector<int>(n)), ccw = cw, half = cw;
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                cw[i][j] = m[n - 1 - j][i];                     // clockwise: the left column becomes the top row
                ccw[i][j] = m[j][n - 1 - i];
                half[i][j] = m[n - 1 - i][n - 1 - j];
            }
        Matrix a = m, b = m, c = m;
        rotateClockwise(a);
        rotateByCycles(b);
        rotateCounterClockwise(c);
        CHECK(a == cw && b == cw && c == ccw);
        rotateClockwise(a);
        CHECK(a == half);                                       // two quarter turns
        rotateByCycles(b); rotateByCycles(b); rotateByCycles(b);
        CHECK(b == m);                                          // four quarter turns are the identity
    }
    for (int t = 0; t < 100; ++t) {
        int rows = rnd(1, 5), cols = rnd(1, 5);
        Matrix m = numbered(rows, cols);
        Matrix r = rotateRectangle(m);
        bool good = (int)r.size() == cols && (int)r[0].size() == rows;
        for (int i = 0; i < rows && good; ++i)
            for (int j = 0; j < cols; ++j) good = good && r[j][rows - 1 - i] == m[i][j];
        CHECK(good);
    }

    // set zeroes
    for (int t = 0; t < 1500; ++t) {
        int rows = rnd(1, 6), cols = rnd(1, 6);
        Matrix m = randMatrix(rng, rows, cols, 0, 3);           // many zeros
        Matrix expect = bruteZeroes(m), a = m, b = m;
        setZeroesFlags(a);
        setZeroes(b);
        CHECK(a == expect && b == expect);
    }

    // first missing positive
    for (int t = 0; t < 1500; ++t) {
        int n = rnd(0, 8);
        std::vector<int> v(n);
        for (int& x : v) x = rnd(-3, n + 3);
        std::vector<int> before = v;
        int got = firstMissingPositive(v), expect = bruteFirstMissing(before);
        std::sort(before.begin(), before.end());
        std::sort(v.begin(), v.end());
        CHECK(got == expect && v == before);                    // only swaps happened, so the multiset is unchanged
    }

    // sign marking (values 1..n)
    for (int t = 0; t < 1500; ++t) {
        int n = rnd(1, 9);
        std::vector<int> v(n);
        for (int& x : v) x = rnd(1, n);
        std::vector<int> original = v, a = v, b = v;
        std::vector<int> twice, missing;
        std::vector<int> count(n + 1, 0);
        for (int x : original) ++count[x];
        for (int x : original) if (count[x] == 2) { /* collected below in order of the second occurrence */ }
        {   // duplicates problem needs counts of 1 or 2 only
            bool valid = true;
            for (int k = 1; k <= n; ++k) valid = valid && count[k] <= 2;
            if (valid) {
                std::vector<int> seenOnce(n + 1, 0);
                for (int x : original) {
                    if (seenOnce[x]) twice.push_back(x);
                    seenOnce[x] = 1;
                }
                CHECK(findDuplicates(a) == twice && a == original);
            }
        }
        for (int k = 1; k <= n; ++k) if (count[k] == 0) missing.push_back(k);
        CHECK(findDisappearedNumbers(b) == missing && b == original);
    }

    // find the duplicate
    for (int t = 0; t < 1500; ++t) {
        int n = rnd(1, 9);
        std::vector<int> v(n + 1);
        std::iota(v.begin(), v.begin() + n, 1);                 // 1..n, then one more copy of some value
        v[n] = rnd(1, n);
        std::shuffle(v.begin(), v.end(), rng);
        std::vector<int> original = v;
        int dup = 0;
        for (int x : v) if (std::count(v.begin(), v.end(), x) == 2) dup = x;
        CHECK(findDuplicate(v) == dup && findDuplicateBinarySearch(v) == dup && v == original);
        // an arbitrary array of n + 1 values in 1..n: both methods must return some repeated value
        for (int& x : v) x = rnd(1, n);
        int f = findDuplicate(v), g = findDuplicateBinarySearch(v);
        CHECK(std::count(v.begin(), v.end(), f) >= 2 && std::count(v.begin(), v.end(), g) >= 2);
    }

    // game of life
    for (int t = 0; t < 500; ++t) {
        Matrix b = randMatrix(rng, rnd(1, 6), rnd(1, 6), 0, 1);
        Matrix expect = bruteLife(b);
        gameOfLife(b);
        CHECK(b == expect);
    }

    // next permutation: the same sequence as the standard library, duplicates included
    for (int t = 0; t < 1500; ++t) {
        std::vector<int> a(rnd(0, 7));
        for (int& x : a) x = rnd(1, 3);
        std::vector<int> b = a;
        bool wrapped = !std::next_permutation(b.begin(), b.end());
        nextPermutation(a);
        CHECK(a == b);
        if (wrapped) CHECK(std::is_sorted(a.begin(), a.end()));
    }
    {   // walking the whole cycle visits every distinct arrangement once
        std::vector<int> a{1, 1, 2, 3}, start = a;
        std::set<std::vector<int>> seen;
        int steps = 0;
        do { seen.insert(a); nextPermutation(a); ++steps; } while (a != start);
        CHECK(steps == 12 && seen.size() == 12);               // 4! / 2! = 12
    }
    std::printf("%d/%d checks passed\n", g_passed, g_checks);
    return g_passed == g_checks ? 0 : 1;
}
