# DSA 02.02: Hashing Patterns

*Part of the [Hashing plan](00_plan.md) · Time: about 5 hours (2 reading, 3 problems) · You need: tutorial [01](01_hash_tables_inside.md) (a table operation costs O(1) expected, `hashCombine`, `PairHash`) and, for two of the ideas, arrays tutorials [03](../01_arrays_and_strings/03_sliding_window.md) and [04](../01_arrays_and_strings/04_prefix_sums_and_difference_arrays.md) · Companion code: [`02_hashing_patterns_demo.cpp`](02_hashing_patterns_demo.cpp)*

By the end of this tutorial you can:

- name the question a problem asks of the table (seen before? how many? where? is my partner here? are these the same? is the neighbour here?) and read the **key** and the **value** off it;
- write each pattern in C++ in a few lines and state its time and space;
- avoid the order-of-operations bugs: ask before you store, count before or after you insert;
- turn "the same up to renaming, order or scale" into a canonical key (shape, bitmask, normalised direction);
- use a `std::vector` or a `std::pair` as a key with a correct hash, and say why a floating-point number is a bad key;
- explain why Longest Consecutive Sequence is O(n) although it has a loop inside a loop;
- decide when an array, a bitmask or a sort beats a hash map.

Every function in this tutorial is in the companion file, which compiles with `-Wall -Wextra -fsanitize=address,undefined` and checks each one against a brute-force version on random inputs or against a worked example. Tutorial 01 showed that one table operation costs O(1) expected, so the cost of a pattern is "how many table operations", and the skill is choosing what to store.

---

## 1. Which pattern is it?

A hash-table solution comes from two choices. The **key** is what must be equal for two items to count as the same. The **value** is what you need to remember about all the items that share a key. The loop around them is almost always the same: for each item, look up, decide, update.

| The problem asks | Key | Value | Pattern | Examples |
|---|---|---|---|---|
| Have I seen this before? | the item | nothing (a set) | seen-set | Contains Duplicate, Happy Number |
| How many times? | the item | a count | counter | Good Pairs, Top K Frequent |
| Where was it last (or first)? | the item | an index | position map | Contains Duplicate II, longest subarray with a property (arrays 04) |
| Is my partner here? | the partner value you need | an index or a count | complement lookup | Two Sum, 4Sum II, Subarray Sum Equals K (arrays 04) |
| Are these the same up to something? | a canonical form of the item | a count or a mapping | canonical key | Group Anagrams (arrays 07), Isomorphic Strings, Valid Sudoku, Points on a Line |
| Is the neighbour present? | the item | nothing | neighbour probe | Longest Consecutive Sequence |

**Cost model.** Each pattern makes O(1) table operations per input item, so the time is O(n) expected and the extra space is O(n) in the worst case. If the key is a string or a vector, multiply by its length (tutorial 01, section 2). Section 9 says when an array or a bitmask replaces the table.

---

## 2. Seen-sets

### 2.1 Contains Duplicate

`insert` returns a pair whose second member says whether the value was new, so one call both tests and records.

```cpp
// LeetCode 217: is any value present twice? insert() reports whether the value was new.
bool containsDuplicate(const std::vector<int>& nums) {
    std::unordered_set<int> seen;
    seen.reserve(nums.size());
    for (int x : nums)
        if (!seen.insert(x).second) return true;
    return false;
}
```

O(n) expected time, O(n) space. Sorting instead costs O(n log n) and O(1) extra space; a `bitset` works when the values have a small range.

### 2.2 Contains Duplicate II: a map of positions, or a window

Now two equal values only count if their indices differ by at most `k`. Keep the latest index of each value. Overwrite on every visit: the latest copy is the closest one to every later copy.

```cpp
// LeetCode 219: are there equal values at two different indices at most k apart? Remember the latest index of each value.
bool containsNearbyDuplicate(const std::vector<int>& nums, int k) {
    std::unordered_map<int, int> lastIndex;
    for (int i = 0; i < (int)nums.size(); ++i) {
        auto it = lastIndex.find(nums[i]);
        if (it != lastIndex.end() && i - it->second <= k) return true;
        lastIndex[nums[i]] = i;                               // always overwrite: the latest position is the closest
    }
    return false;
}
```

Trace for `[1, 2, 3, 1, 2, 3]`. With `k = 2` nothing is close enough; with `k = 3` the second 1 is 3 positions from the first.

| i | value | map before the visit | distance to the last copy | `k = 2` | `k = 3` |
|---|---|---|---|---|---|
| 0 | 1 | `{}` | none | store | store |
| 1 | 2 | `{1:0}` | none | store | store |
| 2 | 3 | `{1:0, 2:1}` | none | store | store |
| 3 | 1 | `{1:0, 2:1, 3:2}` | 3 | too far, store `1:3` | **3 <= 3, return true** |
| 4 | 2 | `{1:3, 2:1, 3:2}` | 3 | too far, store `2:4` | |
| 5 | 3 | `{1:3, 2:4, 3:2}` | 3 | too far, input ends: **false** | |

The second way keeps a set of only the last `k` values, a sliding window (arrays 03). When index `i` arrives, the value at `i - k - 1` is out of reach and leaves the set. Memory is O(min(n, k)) instead of O(distinct values).

```cpp
// The same question with a set that holds exactly the last k values.
bool containsNearbyDuplicateWindow(const std::vector<int>& nums, int k) {
    std::unordered_set<int> window;
    for (int i = 0; i < (int)nums.size(); ++i) {
        if (i > k) window.erase(nums[i - k - 1]);             // the value k + 1 positions back is out of reach now
        if (!window.insert(nums[i]).second) return true;
    }
    return false;
}
```

### 2.3 Happy Number: detecting a cycle

Replace `n` by the sum of the squares of its digits, again and again. Either the sequence reaches 1 (a happy number) or it falls into a loop. A seen-set detects the loop: the first value that comes back means "no 1 is coming".

```cpp
int digitSquareSum(int n) {
    int sum = 0;
    for (; n > 0; n /= 10) sum += (n % 10) * (n % 10);
    return sum;
}

// LeetCode 202: replace n by the sum of the squares of its digits, again and again. Does it reach 1, or loop forever?
bool isHappy(int n) {
    std::unordered_set<int> seen;
    while (n != 1 && seen.insert(n).second) n = digitSquareSum(n);   // stop at 1, or when a value comes back
    return n == 1;
}
```

| Start | Sequence |
|---|---|
| 19 | 19, 82, 68, 100, 1: happy |
| 2 | 2, 4, 16, 37, 58, 89, 145, 42, 20, then 4 again: unhappy, the loop is entered at 4 |

Why the set stays small: the first step from any `int` is at most 730 (1999999999 gives 730), and a number of four or more digits always maps to something smaller than itself, so the sequence ends up below 1000. The companion walks every start up to 5000: the longest walk before reaching 1 or repeating is 19 steps (start 269), and every unhappy start enters the same loop 4, 16, 37, 58, 89, 145, 42, 20. The table is tiny, but the same idea matters wherever a state machine can loop.

The memory can be removed. **Floyd's cycle detection** moves a slow walker one step and a fast walker two steps along the sequence. If the sequence loops, the fast walker laps the slow one inside the loop and they meet. Here 1 maps to itself, so reaching 1 is a loop of length one and the fast walker's `fast == 1` test catches it first.

```cpp
// The same with O(1) memory: a slow and a fast walker on the sequence (Floyd's cycle detection).
bool isHappyFloyd(int n) {
    int slow = n, fast = digitSquareSum(n);
    while (fast != 1 && slow != fast) {
        slow = digitSquareSum(slow);
        fast = digitSquareSum(digitSquareSum(fast));
    }
    return fast == 1;
}
```

O(1) space, and the same code finds a loop in a linked list (LeetCode 141 and 142).

---

## 3. Counting

### 3.1 Good Pairs: count before you increment

How many index pairs `i < j` have equal values? Scan left to right. A new copy of `x` forms one pair with each earlier copy of `x`, and `seen[x]` is exactly that number.

```cpp
// Number of Good Pairs (LeetCode 1512): index pairs i < j with equal values.
long long countEqualPairs(const std::vector<int>& nums) {
    std::unordered_map<int, int> seen;
    long long pairs = 0;
    for (int x : nums) pairs += seen[x]++;                    // x pairs with every earlier copy of itself
    return pairs;
}
```

`seen[x]++` reads the old count and then increments, so the new copy does not pair with itself. The result is `long long`: 100000 equal values give 4999950000 pairs, more than a 32-bit `int` holds. The same total is the sum of `c (c - 1) / 2` over the counts `c`.

### 3.2 Top K Frequent Elements

Count first, then choose the `k` values with the largest counts. Let `d` be the number of distinct values. The choice can be made in four ways.

| Method | Time | Extra space | Remark |
|---|---|---|---|
| sort the `d` pairs by count | O(n + d log d) | O(d) | simplest, fine when `d` is small |
| min-heap of size `k` | O(n + d log k) | O(d + k) | also works for a stream of updates |
| bucket sort by count | O(n) | O(n) | a count is between 1 and n |
| `std::nth_element` on the `d` pairs | O(n + d) expected | O(d) | no full order is needed |

Bucket sort files each value under its count, then reads the buckets from the largest count downward until `k` values are collected.

```cpp
// LeetCode 347 with bucket sort: count, then file every value under its count. O(n).
std::vector<int> topKFrequent(const std::vector<int>& nums, int k) {
    std::unordered_map<int, int> count;
    for (int x : nums) ++count[x];
    std::vector<std::vector<int>> byCount(nums.size() + 1);   // byCount[c] = the values that occur exactly c times
    for (const auto& [value, c] : count) byCount[c].push_back(value);
    std::vector<int> result;
    for (int c = (int)nums.size(); c >= 1 && (int)result.size() < k; --c)
        for (int value : byCount[c])
            if ((int)result.size() < k) result.push_back(value);
    return result;
}
```

The heap version keeps the best `k` seen so far; when it grows to `k + 1` entries the smallest (lowest count) is dropped.

```cpp
// LeetCode 347 with a min-heap of size k. O(n log k), and it also works on a stream of (value, count) pairs.
std::vector<int> topKFrequentHeap(const std::vector<int>& nums, int k) {
    std::unordered_map<int, int> count;
    for (int x : nums) ++count[x];
    using Entry = std::pair<int, int>;                        // (count, value)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
    for (const auto& [value, c] : count) {
        heap.push({c, value});
        if ((int)heap.size() > k) heap.pop();                 // drop the least frequent entry
    }
    std::vector<int> result;
    while (!heap.empty()) {
        result.push_back(heap.top().second);
        heap.pop();
    }
    return result;                                            // least frequent first
}
```

Trace for `[4, 1, 1, 1, 2, 2, 3, 3, 3, 3]` with `k = 2`:

| Step | State |
|---|---|
| counts | `4:1, 1:3, 2:2, 3:4` |
| buckets (count: values) | `1: [4]`, `2: [2]`, `3: [1]`, `4: [3]` |
| scan from the largest count | count 4 gives `3`, count 3 gives `1`, `k` reached: **`{3, 1}`** |

The heap version returns `{1, 3}`, the least frequent first. The problem allows any order. If two values tie for the last place, either is a correct answer, and which one you get depends on the iteration order of `unordered_map`; the companion therefore compares the **frequencies** of the answer with the `k` largest frequencies, not the values.

---

## 4. Complement lookup

The table answers "is my partner here?" in O(1), so a problem that asks for two items with a relation (sum, difference, xor) becomes: for each item, compute the partner it needs and look for it.

### 4.1 Two Sum: ask first, store afterwards

Find `i < j` with `nums[i] + nums[j] == target`. For each element ask whether `target - nums[j]` has appeared, **then** record the element.

```cpp
// LeetCode 1: indices i < j with nums[i] + nums[j] == target, or an empty vector.
std::vector<int> twoSum(const std::vector<int>& nums, int target) {
    std::unordered_map<int, int> index;                       // value -> index where it was seen
    for (int j = 0; j < (int)nums.size(); ++j) {
        auto it = index.find(target - nums[j]);               // ask first: the partner must come from an earlier index
        if (it != index.end()) return {it->second, j};
        index[nums[j]] = j;                                   // store afterwards: an element cannot pair with itself
    }
    return {};
}
```

| j | `nums[j]` | needs | table before | result |
|---|---|---|---|---|
| 0 | 2 | 7 | `{}` | not found, store `2:0` |
| 1 | 7 | 2 | `{2:0}` | found at index 0: return `{0, 1}` |

The order matters when an element is its own partner. For `[3, 3]` with target 6: at `j = 1` the table holds `3:0`, so the answer is `{0, 1}`. For `[3]` with target 6, asking first finds nothing and returns the empty vector; storing first would find the element itself and answer `{0, 0}`.

O(n) expected time and space. The alternative is to sort and move two pointers (arrays 02): O(n log n) time and O(1) extra space, but it needs the original indices kept alongside, so use it when memory is tight or the array is already sorted.

### 4.2 Counting pairs

If the question is "how many pairs", keep a count per value instead of an index. Each new `x` pairs with every earlier copy of its partner.

```cpp
// How many index pairs i < j have nums[i] + nums[j] == target?
long long countPairsWithSum(const std::vector<int>& nums, int target) {
    std::unordered_map<int, int> seen;
    long long pairs = 0;
    for (int x : nums) {
        auto it = seen.find(target - x);
        if (it != seen.end()) pairs += it->second;            // x pairs with every earlier copy of its partner
        ++seen[x];
    }
    return pairs;
}
```

Trace for `[1, 5, 3, 3, 3]` with target 6:

| x | partner | earlier copies of the partner | pairs so far | table after |
|---|---|---|---|---|
| 1 | 5 | 0 | 0 | `{1:1}` |
| 5 | 1 | 1 | 1 | `{1:1, 5:1}` |
| 3 | 3 | 0 | 1 | `{1:1, 5:1, 3:1}` |
| 3 | 3 | 1 | 2 | `{1:1, 5:1, 3:2}` |
| 3 | 3 | 2 | **4** | `{1:1, 5:1, 3:3}` |

The three 3s form 0 + 1 + 2 = 3 pairs, plus the pair (1, 5). Subarray Sum Equals K is this same loop run on prefix sums (arrays 04, section 2).

### 4.3 4Sum II: meet in the middle

Count the tuples `(a, b, c, d)`, one from each of four arrays of length `n`, that add up to 0. Trying all of them costs O(n⁴). Split the four into two halves: store every sum `a + b` with its count, then for every `c + d` look up `-(c + d)`.

```cpp
// LeetCode 454: how many (a, b, c, d), one from each array, add up to 0? Meet in the middle: O(n^2) instead of O(n^4).
int fourSumCount(const std::vector<int>& A, const std::vector<int>& B, const std::vector<int>& C, const std::vector<int>& D) {
    std::unordered_map<int, int> pairSums;                    // a + b -> number of (a, b) pairs with that sum
    for (int a : A)
        for (int b : B) ++pairSums[a + b];
    int count = 0;
    for (int c : C)
        for (int d : D) {
            auto it = pairSums.find(-(c + d));
            if (it != pairSums.end()) count += it->second;
        }
    return count;
}
```

For `A = [1, 2]`, `B = [-2, -1]`, `C = [-1, 2]`, `D = [0, 2]` the pair-sum table is `{-1:1, 0:2, 1:1}`, and the second half looks like this:

| c | d | c + d | looks for | hits |
|---|---|---|---|---|
| -1 | 0 | -1 | 1 | 1 |
| -1 | 2 | 1 | -1 | 1 |
| 2 | 0 | 2 | -2 | 0 |
| 2 | 2 | 4 | -4 | 0 |

The answer is 2. Time and space are O(n²), and the same split gives k-Sum for even k in O(n^(k/2)). A database does the same thing in a **hash join**: build a table on one input, probe it with the other.

---

## 5. Canonical forms and one-to-one mappings

Two items are "the same" when they differ only in something you do not care about: the names of letters, the order of items, the scale of a vector. Compute a **canonical form** that erases that difference and use it as the key.

| The same up to | Canonical key | Where |
|---|---|---|
| order of the letters (anagram) | the sorted letters, or 26 counts | arrays 07 |
| renaming of the letters (isomorphic) | the shape: index of the first occurrence | 5.2 |
| the group a cell belongs to (Sudoku) | one bit per digit, per group | 5.4 |
| scale and sign of a direction (collinear) | `(dx, dy)` divided by the gcd, sign fixed | 6.3 |
| which end is called source (a connection) | the two endpoints in sorted order | 8 |

### 5.1 Isomorphic Strings: a mapping in both directions

Can the letters of `s` be replaced, always the same letter by the same letter, to give `t`, with no two letters sent to one? One map is not enough. With only `s → t`, the strings `ab` and `aa` pass (`a` goes to `a`, `b` goes to `a`), although two letters collapse into one. So the code keeps `forward` and `backward`; each side checks its own consistency.

```cpp
// LeetCode 205: can the letters of s be replaced one-to-one to give t? Keep the mapping in both directions.
bool isIsomorphic(const std::string& s, const std::string& t) {
    if (s.size() != t.size()) return false;
    int forward[256], backward[256];                          // -1 = not mapped yet
    std::fill(forward, forward + 256, -1);
    std::fill(backward, backward + 256, -1);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char a = s[i], b = t[i];
        if (forward[a] == -1) forward[a] = b;                 // a is new: it must become b from now on
        else if (forward[a] != b) return false;               // a was already sent somewhere else
        if (backward[b] == -1) backward[b] = a;               // b is new: only a may become it
        else if (backward[b] != a) return false;              // b was already the image of another letter
    }
    return true;
}
```

| i | `s[i]` | `t[i]` | forward | backward | verdict |
|---|---|---|---|---|---|
| 0 | b | b | `b -> b` | `b <- b` | ok |
| 1 | a | a | `b -> b, a -> a` | `b <- b, a <- a` | ok |
| 2 | d | b | `d -> b`: `d` is new, accepted | `b` is already the image of `b`, not of `d` | **false** (`badc`, `baba`) |

For `paper` and `title` every step finds the same pairs again (`p -> t`, `a -> i`, `p -> t`, `e -> l`, `r -> e`) and the answer is true. Arrays of size 256 replace the hash tables here because a `char` is a small integer (section 9).

### 5.2 The shape of a string

A canonical key for "same up to renaming": replace every character by the **index of its first occurrence**.

```cpp
// The shape of a string: every character is replaced by the index of its first occurrence ("paper" -> 0 1 0 3 4).
std::vector<int> shapeOf(const std::string& s) {
    int first[256];
    std::fill(first, first + 256, -1);
    std::vector<int> shape;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];
        if (first[c] == -1) first[c] = (int)i;
        shape.push_back(first[c]);
    }
    return shape;
}

// Two strings are isomorphic exactly when their shapes are equal.
bool isIsomorphicByShape(const std::string& s, const std::string& t) {
    return shapeOf(s) == shapeOf(t);
}
```

| String | Shape |
|---|---|
| `paper` | 0 1 0 3 4 |
| `title` | 0 1 0 3 4 |
| `badc` | 0 1 2 3 |
| `baba` | 0 1 0 1 |

Two strings are isomorphic exactly when their shapes are equal. The shape is also a hash key, so you can group a list of words by pattern in one pass (LeetCode 890 asks for the words that match a pattern).

### 5.3 Word Pattern

The same problem with whole words on one side. The code uses the shape idea directly: two tables hold the index of the **first** appearance of each letter and each word, and at every position the two indices must be equal. `emplace` keeps the old value when the key exists, so one call both inserts and returns the first index.

```cpp
// LeetCode 290: does the sequence of words follow the pattern, letter for word, one-to-one?
// The same first-occurrence idea, with one map per side.
bool wordPattern(const std::string& pattern, const std::string& s) {
    std::vector<std::string> words;
    for (size_t i = 0; i < s.size();) {                       // words are separated by single spaces
        size_t j = s.find(' ', i);
        if (j == std::string::npos) j = s.size();
        words.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    if (words.size() != pattern.size()) return false;
    std::unordered_map<char, int> letterFirst;                // letter -> index of its first appearance
    std::unordered_map<std::string, int> wordFirst;           // word -> index of its first appearance
    for (int i = 0; i < (int)words.size(); ++i) {
        int a = letterFirst.emplace(pattern[i], i).first->second;     // emplace keeps the old value if the key exists
        int b = wordFirst.emplace(words[i], i).first->second;
        if (a != b) return false;
    }
    return true;
}
```

For `abba` and `dog dog dog dog` the words agree with the letters at first and then break:

| i | letter | word | first index of the letter | first index of the word | |
|---|---|---|---|---|---|
| 0 | a | dog | 0 | 0 | equal |
| 1 | b | dog | 1 | 0 | **different: return false** |

(`abba` with `dog cat cat dog` gives equal indices at every position and is true.)

### 5.4 Valid Sudoku: one bit per digit

No digit may repeat in a row, a column or a 3 x 3 box. Nine digits fit in a 9-bit mask, so each of the 27 groups is a small integer and the "set" is a bit test. The box number of cell `(r, c)` is `(r / 3) * 3 + c / 3`: cells `(0, 0)` and `(2, 2)` are in box 0, `(4, 7)` in box 5, `(8, 8)` in box 8.

```cpp
// LeetCode 36: no digit twice in a row, a column or a 3 x 3 box. Nine digits fit in a 9-bit mask per group.
bool isValidSudoku(const std::vector<std::string>& board) {
    int rows[9] = {}, cols[9] = {}, boxes[9] = {};            // bit d set: digit d + 1 is already used in this group
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) {
            if (board[r][c] == '.') continue;
            int bit = 1 << (board[r][c] - '1');
            int box = (r / 3) * 3 + c / 3;
            if ((rows[r] | cols[c] | boxes[box]) & bit) return false;
            rows[r] |= bit;
            cols[c] |= bit;
            boxes[box] |= bit;
        }
    return true;
}
```

Take the standard example board and change its first cell to 8. The scan goes row by row; the first conflict is at cell `(2, 2)`, which holds an 8, in box 0, where the 8 at `(0, 0)` has already set its bit. The check costs a constant 81 steps. The companion also has a version with one `unordered_set<int>` of encoded triples `(kind, group, digit)`, which is slower and shows that the mask is just a set with a tiny key range.

---

## 6. Tuples and sequences as keys

A key can be anything that has `==` and a hash. Tutorial 01 gave `PairHash` and `hashCombine`; this section adds a whole vector as a key, and a direction as a key.

### 6.1 A vector as a key: Equal Row and Column Pairs

Count the pairs (row `r`, column `c`) of an `n x n` grid whose contents are identical. Comparing every row with every column costs O(n³). Instead, count the rows in a table keyed by the **whole row**, then look up each column.

```cpp
// A hash for a whole vector: fold the elements with hashCombine (tutorial 01).
struct VectorHash {
    size_t operator()(const std::vector<int>& v) const noexcept {
        size_t h = v.size();
        for (int x : v) h = hashCombine(h, (size_t)x);
        return h;
    }
};

// LeetCode 2352: how many pairs (row r, column c) have exactly the same contents?
int equalPairs(const std::vector<std::vector<int>>& grid) {
    std::unordered_map<std::vector<int>, int, VectorHash> rowCount;
    for (const auto& row : grid) ++rowCount[row];
    int n = (int)grid.size(), pairs = 0;
    for (int c = 0; c < n; ++c) {
        std::vector<int> column(n);
        for (int r = 0; r < n; ++r) column[r] = grid[r][c];
        auto it = rowCount.find(column);
        if (it != rowCount.end()) pairs += it->second;
    }
    return pairs;
}
```

`VectorHash` folds the elements in order with `hashCombine`, and starts from the length, so a vector that is a prefix of another does not begin with the same value. It uses exactly what `==` compares: every element, in order. Time is O(n²) expected: `n` rows and `n` columns, each hashed and compared in O(n).

For `[[3, 2, 1], [1, 7, 6], [2, 7, 7]]` the rows are `[3, 2, 1]`, `[1, 7, 6]`, `[2, 7, 7]` (one copy each) and the columns are `[3, 1, 2]`, `[2, 7, 7]`, `[1, 6, 7]`. Only the second column equals a row (the third), so the answer is 1.

Why not a cheaper hash such as the sum of the elements? The 27 vectors of length 3 over `{0, 1, 2}` get 27 different values from `VectorHash`, and only 7 different sums (0 to 6): every table lookup would meet a crowd. The companion prints both counts.

### 6.2 Why a floating-point number is a poor key

A `double` can be hashed, but equality of computed values is not reliable: two different fractions can round to the same `double`, and `0.1 + 0.2` is not `0.3`. If the quantity is really a ratio of two integers, keep it as a pair of integers in lowest terms. The next problem shows what goes wrong otherwise.

### 6.3 Points on a line: a direction as a key

Given distinct points, find the most that lie on one line. Fix one point `i`. Every other point `j` lies in some direction from it, and two points `j` and `k` are on the same line through `i` exactly when the directions are equal. So count the points by direction, for each `i`; the best count plus 1 (for `i` itself) is the answer.

The direction must be canonical. The vector `(dx, dy)` and its multiples describe one direction, and `(dx, dy)` and `(-dx, -dy)` describe the same line. So divide by the gcd (the vector becomes the shortest integer vector in its direction, which also covers vertical lines with no special case) and fix the sign.

```cpp
// LeetCode 149: the most points on one line (all points are distinct). From each point, count the other points
// by direction; a direction is the pair (dx, dy) divided by its gcd, so it is exact (no floating-point slopes).
int maxPointsOnLine(const std::vector<std::pair<int, int>>& points) {
    int n = (int)points.size(), best = std::min(n, 2);
    for (int i = 0; i < n; ++i) {
        std::unordered_map<std::pair<int, int>, int, PairHash> direction;
        for (int j = i + 1; j < n; ++j) {
            int dx = points[j].first - points[i].first, dy = points[j].second - points[i].second;
            int g = std::gcd(dx, dy);                         // never 0: the points are distinct
            dx /= g;
            dy /= g;
            if (dx < 0 || (dx == 0 && dy < 0)) { dx = -dx; dy = -dy; }   // a line has two directions: keep one
            best = std::max(best, ++direction[{dx, dy}] + 1); // + 1 for point i itself
        }
    }
    return best;
}
```

Trace for the points `(1,1) (3,2) (5,3) (4,1) (2,3) (1,4)`:

| Anchor `i` | Directions to the later points (count) | Best for this anchor |
|---|---|---|
| (1,1) | `(2,1)`: 2, `(1,0)`: 1, `(1,2)`: 1, `(0,1)`: 1 | 3 |
| (3,2) | `(2,1)`: 1, `(1,-1)`: 3 | **4** |

From (3,2), the points (4,1), (2,3) and (1,4) have the raw vectors `(1,-1)`, `(-1,1)` and `(-2,2)`. All three become `(1,-1)`, so four points lie on the line `x + y = 5`. The answer is 4, in O(n²) expected time and O(n) space.

The tempting version uses the slope `dy / dx` as a `double` key. It fails for large coordinates: the points `(0,0)`, `(94911151, 94911150)` and `(94911152, 94911151)` are **not** on one line (the cross product of the two vectors is 1), but both slopes round to the same `double`. The companion runs both: the exact version answers 2, the double-slope version answers 3.

---

## 7. Longest Consecutive Sequence: probe the neighbour

Find the length of the longest run of consecutive integers in an unsorted array, for example `[100, 4, 200, 1, 3, 2]` has the run `1, 2, 3, 4`. Sorting costs O(n log n). A set does it in O(n) expected. The idea: a value is the **start** of a run when `x - 1` is absent. Walk upward only from starts.

```cpp
// LeetCode 128: the longest run of consecutive integers in an unsorted array, in O(n) expected time.
int longestConsecutive(const std::vector<int>& nums) {
    std::unordered_set<int> present(nums.begin(), nums.end());
    int best = 0;
    for (int x : present) {
        if (present.count(x - 1)) continue;                   // x is inside a run, not its start
        int length = 1;
        while (present.count(x + length)) ++length;
        best = std::max(best, length);
    }
    return best;
}
```

Trace (the loop visits the set, whose order is unspecified, and the result does not depend on it; this table uses the array order):

| x | is `x - 1` present? | action |
|---|---|---|
| 100 | no | start, `101` absent: length 1 |
| 4 | yes (3) | skip |
| 200 | no | start, `201` absent: length 1 |
| 1 | no | start, 2, 3, 4 present, 5 absent: length **4** |
| 3 | yes (2) | skip |
| 2 | yes (1) | skip |

**Why it is O(n)** although a `while` sits inside a `for`. Only the start of a run walks, and each run is walked once, so the walks together cover each distinct value once. For `d` distinct values the code makes `d` lookups for the `x - 1` tests and `d` lookups for the walks (a run of length `L` costs `L` lookups, the last one failing): exactly **2d** set lookups. The companion has a copy of the function that counts its lookups and checks `2d` on thousands of random arrays. Without the skip rule, the same input of 50000 consecutive values would need about 1.25 billion lookups (`50000 x 49999 / 2`); with it, the companion finishes in a few milliseconds (0.004 to 0.009 s over several runs on the test machine of tutorial 01, section 9, `-O2`).

Two details. Loop over the **set**, not the array: with many copies of a run's start value, the array loop would repeat the same walk for each copy. And the result is a length, so for `[]` the answer is 0 and for `[5]` it is 1.

---

## 8. Where this shows up in systems

- **Duplicate suppression in a window.** A receiver that must reject replayed or duplicated packets keeps the sequence numbers it has accepted recently. This is Contains Duplicate II with a window of `W` numbers. IPsec's anti-replay check (RFC 4303, section 3.4.3) uses a sliding window of at least 32 and preferably 64 sequence numbers. The numbers in the window are small offsets from the highest one seen, so implementations commonly keep it as a **bitmap**, which is the section 9 rule of using an array when the keys are small.
- **Counters per key.** A flow cache maps a 5-tuple to packet and byte counters; a Wi-Fi access point maps a station's MAC address to its statistics. Reporting the heaviest flows is Top K Frequent. At line rate an exact table may not fit, which is why tutorial 04 trades exactness for fixed memory (count-min sketch).
- **A canonical key for a connection.** A firewall must find the same entry for a packet and for its reply. Either order the two endpoints so that both directions give one key (a canonical form, section 5), or store both directions (Linux connection tracking keeps the original and the reply tuple of every connection in its hash table).
- **Joins.** A database hash join builds a table on the smaller input and probes it with the other: the loop of section 4.3.
- **Loops in a next-pointer chain.** Floyd's two walkers find a loop in a corrupted list or a chain of next hops without marking nodes and without memory.
- **Runs of received data.** Reassembly and selective acknowledgements work on contiguous ranges of received sequence numbers. Building the ranges from a set of numbers is the neighbour probe of section 7.

---

## 9. Choosing the tool

| The task | Use |
|---|---|
| Keys are letters, bytes or small integers (below about 10⁶) | an array indexed by the key (`int count[26]`, the 256-entry tables of 5.1); no hashing |
| A few dozen yes/no flags per group (digits 1 to 9, 26 letters) | a bitmask in one integer (5.4) |
| A window over dense sequence numbers | a bitmap or a ring buffer (section 8) |
| Count, deduplicate or group by arbitrary keys; order not needed | `unordered_map` / `unordered_set`, with `reserve(n)` when you know the size |
| Nearest value, range, k-th smallest, sorted output | `std::map` / `std::set`, or sort and binary search |
| Input already sorted, or memory is tight | sort and two pointers (arrays 02) |
| The key is a vector or a struct | a hash over exactly the fields `==` compares (6.1), or a canonical form (section 5) |
| An exact table does not fit in memory | a sketch (tutorial 04) |
| A few dozen items | a `vector` and a linear scan |

---

## 10. Mistakes that cost points

1. **Storing before asking** in a complement lookup, so an element pairs with itself (`twoSum({3}, 6)` returns `{0, 0}`).
2. **Keeping the wrong index.** Contains Duplicate II needs the **latest** index of a value; the longest subarray with a property needs the **first** (arrays 04). `emplace` keeps the first, `m[x] = i` keeps the latest.
3. **One-directional mapping** in Isomorphic Strings and Word Pattern: `ab` against `aa`, and `abba` against `dog dog dog dog`, both pass a single map.
4. **`int` for a count of pairs.** n(n - 1) / 2 no longer fits in an `int` from n = 65537 equal items.
5. **`m[key]` to test presence.** It inserts the key. Use `find`, `count` or `contains`. (In `countEqualPairs` the insert is intended.)
6. **Floating-point keys**, such as the slope `dy / dx`: different fractions can round to one value, and `dx = 0` needs a special case. Use an integer pair in lowest terms.
7. **A direction without a sign rule**, so `(1, -1)` and `(-1, 1)` count as two lines. Test with the points sorted and with the points shuffled.
8. **A weak hash for a sequence** (sum or xor of the elements), or no hash at all: `std::hash<std::vector<int>>` does not exist, so the code does not compile.
9. **Looping over the array instead of the set** in Longest Consecutive Sequence: copies of a run's start value repeat the same walk.
10. **Relying on the order of an `unordered_map`**: ties in Top K, or the order of a result, differ between libraries and between runs.
11. **A table for 26 letters or 9 digits.** It works, but an array or a bitmask is smaller and faster.

---

## 11. Questions you will be asked

**Two Sum in O(n): how, and why does one pass suffice?** For each element look up `target - x` among the earlier elements, then store `x`. If a pair exists, its second element finds the first one in the table. Asking before storing keeps an element from pairing with itself; equal values work because the second copy asks before it is stored.

**How would you do it with O(1) extra space?** Sort and use two pointers: O(n log n), and the indices are lost unless you carry them.

**Top K Frequent: which method?** Count, then: heap of size `k` for O(n + d log k) and streams; bucket sort for O(n); `nth_element` for O(n + d) expected. The heap is the usual answer; mention the bucket sort as the O(n) one.

**How do you detect a cycle without extra memory?** Floyd: two walkers, one and two steps at a time. If there is a loop the fast one laps the slow one inside it.

**How do you find "the same" items that differ in some way?** Pick a canonical form that erases the difference and use it as the key: sorted letters or counts for anagrams, first-occurrence indices for renaming, gcd and a sign rule for directions.

**Can a `vector` be a key of an `unordered_map`?** Yes, with a hash you supply that uses all the elements in order (`hashCombine`) and the default `==`. Hashing costs O(length).

**Why is Longest Consecutive Sequence O(n) with a loop inside a loop?** Only the start of a run walks, and each run is walked once, so the walks together cost one lookup per distinct value: 2d lookups in total.

**Hash map or sort?** The hash map is O(n) expected but uses O(n) memory and has no order; sorting is O(n log n), can be in place, and gives order. If the data is already sorted, or only a comparison is available, sort.

---

## 12. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If you are stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md). First ask: what is the key, what is the value, and what must be equal for two items to be "the same"?

### On paper

**Problem A: Which index?** Run `containsNearbyDuplicate` on `[7, 1, 5, 7, 1, 7]` with `k = 2`. Write the map before each step and say where the answer is found. What would a version that **keeps the first index** of each value answer?
*Where:* paper. *How:* the trace of 2.2.

**Problem B: Where does it loop?** Write the digit-square sequences of 44 and of 116 until you reach 1 or a value you have seen. For 116, which value is seen twice first?
*Where:* paper. *How:* section 2.3; the repeated value is not necessarily the start.

**Problem C: Ties.** Count `[5, 5, 5, 2, 2, 8, 8, 8, 8, 1, 3, 3]`, fill the buckets by count, and list every correct answer for `k = 3`.
*Where:* paper. *How:* section 3.2; look at the bucket that has to be cut.

**Problem D: Meet in the middle.** `A = [0, 1]`, `B = [-1, 1]`, `C = [-1, 0]`, `D = [0, 1]`. Write the table of sums `a + b`, then probe it for every `c + d`. How many tuples add up to 0?
*Where:* paper. *How:* the trace of 4.3.

### In code

**Problem 1: Contains Duplicate II** (LeetCode 219, easy)
*Where:* leetcode.com/problems/contains-duplicate-ii. *Goal:* are there two equal values at indices at most `k` apart?
*How:* section 2.2: a map of the latest index, then the window version that uses O(min(n, k)) memory. Test `k = 0` on `[1, 1]` (answer false).

**Problem 2: Happy Number** (LeetCode 202, easy)
*Where:* leetcode.com/problems/happy-number. *Goal:* does repeated digit-square summing reach 1?
*How:* section 2.3: first with a set, then with Floyd's two walkers and no set.

**Problem 3: Isomorphic Strings and Word Pattern** (LeetCode 205 and 290, easy)
*Where:* leetcode.com/problems/isomorphic-strings and /word-pattern. *Goal:* a one-to-one correspondence between letters, or between letters and words.
*How:* sections 5.1 to 5.3. Test `ab` against `aa` and `abba` against `dog dog dog dog` (both false). Then solve 205 again with the shape of 5.2.

**Problem 4: Valid Sudoku** (LeetCode 36, medium)
*Where:* leetcode.com/problems/valid-sudoku. *Goal:* check that no digit repeats in a row, a column or a box (the board need not be solvable).
*How:* section 5.4: three arrays of nine masks and the box formula. Check the formula on the corners of the board before writing the loop.

**Problem 5: Longest Consecutive Sequence** (LeetCode 128, medium)
*Where:* leetcode.com/problems/longest-consecutive-sequence. *Goal:* the length of the longest run of consecutive integers, in O(n).
*How:* section 7: a set, and walk only from values whose predecessor is absent. Add a counter and check that you make at most 2 lookups per distinct value.

**Problem 6: Top K Frequent Elements** (LeetCode 347, medium)
*Where:* leetcode.com/problems/top-k-frequent-elements. *Goal:* the `k` most frequent values, better than O(n log n).
*How:* section 3.2: write the bucket version and the heap version, and compare their answers on random inputs by frequency, not by value.

**Problem 7: 4Sum II** (LeetCode 454, medium)
*Where:* leetcode.com/problems/4sum-ii. *Goal:* the number of tuples, one value from each of four arrays, that add up to 0.
*How:* section 4.3: the table of `a + b` with counts, then a probe for each `c + d`. Then try 4Sum (LeetCode 18) with sorting and two pointers and compare the two approaches.

**Problem 8: Packet duplicate filter** (own code)
*Where:* your editor. *Goal:* `bool accept(uint32_t id)` returns false if `id` is among the last `W` accepted ids; otherwise it accepts `id` and, if the window already holds `W` ids, forgets the oldest. Test it against a brute-force scan of the window on random streams.
*How:* section 2.2: a set for membership and a queue for the order. Then try a bitmap indexed by `id mod W` for ids that arrive roughly in order, and notice that it defines the window by **value** (the last `W` numbers below the highest), which is a different rule.

**More practice, once these feel easy:** Contains Duplicate (217), Two Sum (1), Intersection of Two Arrays II (350), Number of Good Pairs (1512), Equal Row and Column Pairs (2352), Max Points on a Line (149), Ransom Note (383), Find and Replace Pattern (890), Group Anagrams (49), Longest Substring Without Repeating Characters (3), Find All Anagrams in a String (438), Top K Frequent Words (692), Fraction to Recurring Decimal (166, a cycle in the remainders). 3Sum (15) is a good example of sorting beating hashing.

---

## Answers

Try before reading. LeetCode wraps these functions in `class Solution { public: ... };`; the logic is the same.

### Problem A

| i | value | distance to the last copy | map after |
|---|---|---|---|
| 0 | 7 | none | `{7:0}` |
| 1 | 1 | none | `{7:0, 1:1}` |
| 2 | 5 | none | `{7:0, 1:1, 5:2}` |
| 3 | 7 | 3 > 2 | `{7:3, 1:1, 5:2}` |
| 4 | 1 | 3 > 2 | `{7:3, 1:4, 5:2}` |
| 5 | 7 | 2 <= 2 | **return true** |

The answer is true, found at index 5 (the copies at indices 3 and 5). A map that keeps the first index sees distance 3 at index 3 (against index 0), distance 3 at index 4 (against index 1) and distance 5 at index 5 (against index 0), never overwrites, and answers **false**: wrong.

### Problem B

44: 44, 32, 13, 10, 1: happy. 116: 116, 38, 73, 58, 89, 145, 42, 20, 4, 16, 37, then 58 again. The first value seen twice is **58**, not 116: the start (116) is outside the loop and is never seen again. The loop is 58, 89, 145, 42, 20, 4, 16, 37.

### Problem C

Counts: 8 occurs 4 times, 5 three times, 2 and 3 twice each, 1 once. Buckets: `1: [1]`, `2: [2, 3]`, `3: [5]`, `4: [8]`. The scan takes 8 and 5, then must cut the bucket for count 2. Correct answers: `{8, 5, 2}` or `{8, 5, 3}`, in any order.

### Problem D

The table of `a + b`: `{-1:1, 0:1, 1:1, 2:1}`.

| c | d | c + d | looks for | hits |
|---|---|---|---|---|
| -1 | 0 | -1 | 1 | 1 |
| -1 | 1 | 0 | 0 | 1 |
| 0 | 0 | 0 | 0 | 1 |
| 0 | 1 | 1 | -1 | 1 |

The answer is **4**, found with 4 + 4 table operations instead of 16 tuples.

### Problem 1: Contains Duplicate II

The map version of 2.2 is O(n) time and O(distinct) space. The window version erases `nums[i - k - 1]` before inserting `nums[i]` and returns true when `insert` reports a repeat: O(n) time, O(min(n, k)) space. For `k = 0` the window only ever holds the current value, so the answer is false.

### Problem 2: Happy Number

Set version: loop while `n != 1` and `insert(n)` succeeds, then return `n == 1`. Floyd version: `slow = n`, `fast = f(n)`; step `slow` once and `fast` twice until `fast == 1` or they are equal; return `fast == 1`. Every value after the first step is below 1000, so the set holds at most a few dozen numbers.

### Problem 3: Isomorphic Strings and Word Pattern

Check the lengths first. Walk both sequences together and keep two tables: letter to partner, and partner to letter. At each position, a missing entry is filled in; a present entry must equal the current partner; fail otherwise. For Word Pattern split on spaces and compare the number of words with the length of the pattern first. The shape version: `shapeOf(s) == shapeOf(t)`. All are O(n) for letters (arrays of 256 entries) and O(total length of the words) for Word Pattern.

### Problem 4: Valid Sudoku

`int rows[9], cols[9], boxes[9]` (zero-initialised), `bit = 1 << (digit - 1)`, `box = (r / 3) * 3 + c / 3`. If `(rows[r] | cols[c] | boxes[box]) & bit` the board is invalid; otherwise set the bit in all three. The corners give boxes 0, 2, 6 and 8, the centre cell `(4, 4)` gives 4. O(81).

### Problem 5: Longest Consecutive Sequence

Put the values in an `unordered_set`. For each value `x` in the set with `x - 1` absent, count `x, x + 1, x + 2, ...` while present, and keep the maximum. With the counter you get exactly 2d lookups for d distinct values. A sort-and-scan answer is O(n log n): accepted by the judge, but not the O(n) the problem asks for.

### Problem 6: Top K Frequent Elements

Count with `++count[x]`. Heap: push `(count, value)` into a min-heap, pop when the size passes `k`, and read what remains. Buckets: `byCount[c]` lists the values with count `c`; scan `c` from `n` down and collect `k` values. When comparing two implementations, compare the sorted list of the counts of the chosen values, because ties may be cut differently.

### Problem 7: 4Sum II

`pairSums[a + b]++` for all pairs of the first two arrays, then add `pairSums[-(c + d)]` for all pairs of the last two. O(n²) time and space. The sorted version of 4Sum (LeetCode 18) fixes two values and moves two pointers: O(n³) time and O(1) extra space, which wins when `n` is small and the memory of the O(n²) table is a problem; it also returns the quadruples themselves rather than a count.

### Problem 8: Packet duplicate filter

```cpp
// accept(id) is false if id is among the last `window` accepted ids. Otherwise it accepts id and forgets the oldest
// id when the window is full. A set answers "is it in the window?", a queue remembers the order.
class DuplicateFilter {
    size_t window_;
    std::unordered_set<uint32_t> recent_;
    std::deque<uint32_t> order_;
public:
    explicit DuplicateFilter(size_t window) : window_(window) {}
    bool accept(uint32_t id) {
        if (!recent_.insert(id).second) return false;         // already in the window
        order_.push_back(id);
        if (order_.size() > window_) {                        // forget the oldest
            recent_.erase(order_.front());
            order_.pop_front();
        }
        return true;
    }
};
```

The test: keep the last `W` accepted ids in a plain vector, scan it for each new id, and compare. An id that was dropped as a duplicate does not refresh its place in the window. The bitmap version (a bit per `id mod W` plus the highest id seen) uses no allocation, but a duplicate is then recognised by value distance, not by the number of packets accepted since.

---

## Cheat sheet

| Pattern | Core line | Cost |
|---|---|---|
| seen-set | `if (!seen.insert(x).second) /* repeat */` | O(1) per item |
| counter | `++count[x]`; for pairs: `pairs += count[x]++` | O(1) per item |
| position map | `last[x] = i` keeps the latest; `emplace(x, i)` keeps the first | O(1) per item |
| complement lookup | `find(target - x)` **first**, `store x` after | O(n) |
| meet in the middle | table of `a + b`, probe `-(c + d)` | O(n²) instead of O(n⁴) |
| canonical key | shape, `(dx, dy) / gcd` with a sign rule, bitmask, sorted letters | O(length of the key) |
| neighbour probe | walk up only if `x - 1` is absent | 2d lookups |
| top K | count, then buckets, heap or `nth_element` | O(n), O(n + d log k), O(n + d) |
| cycle without memory | slow and fast walker | O(1) space |

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `02_hashing_patterns_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** Hashing tutorial 03, design a hash map (Robin Hood hashing with backward-shift deletion, cuckoo hashing, the Swiss-table idea, iterators and the rehash policy, LRU cache). Say "next" when you are ready.
