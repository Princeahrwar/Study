# DSA 01.02: Two Pointers

*Part of the [Arrays and Strings plan](00_plan.md) · Time: about 4 hours (1 reading, 3 problems) · You need: [tutorial 01](01_complexity_and_arrays_in_memory.md), especially section 3.2 (read and write indices) · Companion code: [`02_two_pointers_demo.cpp`](02_two_pointers_demo.cpp)*

By the end of this tutorial you can:

- spot problems where two pointers turns an O(n²) double loop into an O(n) pass;
- explain why moving a pointer can never skip the answer;
- write the three shapes: opposite ends, one pointer per sequence, and partition zones;
- avoid the classic bugs: infinite loops, off-by-one errors, duplicate results.

---

## 1. The idea

A double loop checks every pair: n × n steps. Two pointers keeps two indices instead, and uses some **order or structure in the data** so that every step rules out a whole group of candidates at once. The pointers only ever move one way, so the total number of steps is about n.

There are three shapes. Almost every two-pointer problem is one of them:

| Shape | Pointers start | They move | Typical use |
|---|---|---|---|
| **Opposite ends** | at 0 and at n−1 | toward each other | pair sums, palindromes, container problems |
| **One pointer per sequence** | at the start of each of two sequences | both forward | merge, intersect, subsequence |
| **Partition zones** | at the ends (or one end) | boundaries between zones | partition, sort three colours, filter in place |

Two close relatives are covered elsewhere: read and write indices moving in the same direction (tutorial 01, section 3.2) and the sliding window (tutorial 03). Fast and slow pointers on linked lists are in the linked list area.

---

## 2. Opposite ends

### 2.1 Warm-up: is it a palindrome?

Compare the first and last characters, then the second and second-to-last, and so on. Stop when the pointers meet.

```cpp
bool isPalindrome(const std::string& s) {
    int l = 0, r = (int)s.size() - 1;   // int, so an empty string gives r = -1 and the loop is skipped
    while (l < r) {
        if (s[l] != s[r]) return false;
        ++l;
        --r;
    }
    return true;
}
```

`"racecar"`: compare r/r, a/a, c/c, and the pointers meet at `e`. True. O(n) time, O(1) space. We use `int`, not `size_t`, for the pointers, so an empty input cannot underflow (tutorial 01, section 2.6).

### 2.2 The core example: a pair with a given sum, in a sorted array

*Question:* the array is sorted. Are there two different elements that add up to `target`?

*Brute force:* try every pair, O(n²).

*The insight:* look at the smallest and the largest element together.

- If their sum is **too small**, the smallest element is useless. Even paired with the largest element it is too small, and everything else is smaller. So drop it: move `l` right.
- If their sum is **too big**, the largest element is useless. Even paired with the smallest it is too big. Drop it: move `r` left.
- If the sum is right, we are done.

```cpp
// Sorted array: do two different elements add up to target? Returns their indices, or {-1, -1}.
std::pair<int, int> pairWithSum(const std::vector<int>& a, int target) {
    int l = 0, r = (int)a.size() - 1;
    while (l < r) {
        long long sum = (long long)a[l] + a[r];       // long long: two ints can overflow int
        if (sum == target) return {l, r};
        if (sum < target) ++l;                        // too small: a[l] cannot pair with anything left of r
        else              --r;                        // too big: a[r] cannot pair with anything right of l
    }
    return {-1, -1};
}
```

Trace on `{1, 3, 4, 6, 8, 11}` with `target = 10`:

| l | r | a[l] + a[r] | decision |
|---|---|---|---|
| 0 | 5 | 1 + 11 = 12 | too big, drop 11: `r--` |
| 0 | 4 | 1 + 8 = 9 | too small, drop 1: `l++` |
| 1 | 4 | 3 + 8 = 11 | too big, drop 8: `r--` |
| 1 | 3 | 3 + 6 = 9 | too small, drop 3: `l++` |
| 2 | 3 | 4 + 6 = 10 | found: indices (2, 3) |

**Why it never misses the answer.** Suppose a valid pair (i, j) exists, with i < j. The pointers start outside it: l ≤ i and j ≤ r. Say we are about to move `l` because `a[l] + a[r] < target`. If `l` were equal to `i`, then since `r ≥ j` and the array is sorted, `a[i] + a[r] ≥ a[i] + a[j] = target`, which contradicts "less than target". So `l ≠ i`, and moving it is safe. The same argument covers `r` and `j`. The pointers can never jump past the answer, and they move on every step, so they either find it or meet.

**Cost:** each step moves a pointer, and the pointers cross at most n positions in total, so O(n) time and O(1) space.

**Why "sorted" matters.** In an unsorted array, a sum that is too small says nothing about `a[l]`, because a bigger partner might sit anywhere. Your options for unsorted data:

| Approach | Time | Extra space | Good when |
|---|---|---|---|
| Hash map of seen values (tutorial 01, Problem 14) | O(n) | O(n) | one query, unsorted, you need original indices |
| Sort, then two pointers | O(n log n) | O(1) | the array may be modified, only existence matters |
| Brute force | O(n²) | O(1) | n is tiny |

If you sort and the problem asks for the **original indices**, sort pairs of (value, index) instead of bare values.

### 2.3 A variation: counting pairs

*Question:* in a sorted array, how many pairs (i < j) have `a[i] + a[j] < target`?

One move can account for many pairs at once. If `a[l] + a[r] < target`, then `a[l]` also works with every element between `l+1` and `r`, because those are all ≤ `a[r]`. That is `r - l` pairs counted in a single step.

```cpp
// Sorted array: how many pairs (i < j) have a[i] + a[j] < target?
long long countPairsBelow(const std::vector<int>& a, int target) {
    long long count = 0;
    int l = 0, r = (int)a.size() - 1;
    while (l < r) {
        if ((long long)a[l] + a[r] < target) {
            count += r - l;                           // a[l] pairs with every element from l+1 to r
            ++l;
        } else {
            --r;
        }
    }
    return count;
}
```

For `{1, 2, 3, 4, 5}` and `target = 6`: `1+5 = 6` is not below 6, so `r--`; `1+4 = 5` is below, so count += 3 (pairs with 2, 3, 4) and `l++`; `2+4 = 6` no, `r--`; `2+3 = 5` yes, count += 1. Total 4.

### 2.4 Does two pointers apply? Three questions

Before you write any code, answer these:

1. **Is there an order or monotonic property?** Sorted values, or a quantity that only grows as a pointer moves (like the width of a container).
2. **What does each pointer move rule out?** You must be able to say "this element can be discarded, because ...". If you cannot finish that sentence, two pointers does not apply.
3. **When do I stop?** Usually `l < r`, sometimes `l <= r`. Decide which, and check the case with 0 and 1 elements.

---

## 3. One pointer per sequence

When you have two sorted sequences, keep one pointer in each. At every step, compare the two current elements and advance the pointer of the smaller one, because the smaller element cannot match anything later in the other sequence.

### 3.1 Intersection of two sorted arrays

```cpp
// Two sorted arrays: the values that appear in both (each value reported once).
std::vector<int> intersectSorted(const std::vector<int>& a, const std::vector<int>& b) {
    std::vector<int> out;
    size_t i = 0, j = 0;                              // one pointer per array, both only move forward
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) ++i;
        else if (a[i] > b[j]) ++j;
        else {
            if (out.empty() || out.back() != a[i]) out.push_back(a[i]);
            ++i;
            ++j;
        }
    }
    return out;
}
```

For `a = {1, 2, 2, 3, 5, 8}` and `b = {2, 2, 3, 4, 8, 9}`, the result is `{2, 3, 8}`. Each step advances at least one pointer, so the cost is O(m + n) time and O(1) extra space (not counting the output). Merging two sorted arrays into one has exactly the same shape: always take the smaller element and advance its pointer.

Alternatives: a hash set of the smaller array is O(m + n) time but O(min(m, n)) space and ignores the sorted order; if one array is tiny, binary searching each of its elements in the big one is O(m log n).

### 3.2 Is it a subsequence?

*Question:* are the characters of `s` all present in `t` in the same order (not necessarily next to each other)?

```cpp
bool isSubsequence(const std::string& s, const std::string& t) {
    size_t i = 0;                                     // how much of s we have matched so far
    for (size_t j = 0; j < t.size() && i < s.size(); ++j)
        if (t[j] == s[i]) ++i;
    return i == s.size();
}
```

`"ace"` in `"abcde"`: `a` matches at j=0, `c` at j=2, `e` at j=4, so `i` reaches 3 and the answer is true. `"aec"` fails: after `a` and `e`, no `c` follows. The pointer `j` always moves; the pointer `i` moves only on a match. O(|t|) time, O(1) space.

---

## 4. Partition zones

Sometimes the two pointers are the **borders between zones** of the array. The trick is to write down what each zone promises, and to make every step keep those promises.

*Question:* rearrange the array so that all negative numbers come before all non-negative ones. The order inside each group does not matter.

```cpp
// Rearrange so all negative numbers come before all non-negative ones (order inside each group is not kept).
void partitionNegatives(std::vector<int>& a) {
    int l = 0, r = (int)a.size() - 1;
    while (l <= r) {
        if (a[l] < 0) ++l;                            // a[l] is already in the right zone
        else std::swap(a[l], a[r--]);                 // send it to the right zone; the value swapped in is unchecked
    }
}
```

At every moment the array has three zones:

```text
 [ negatives ... ][ not yet looked at ... ][ non-negatives ... ]
 0            l-1  l                    r   r+1            n-1
```

If `a[l]` is negative, it already belongs in the left zone, so the left zone grows (`l++`). If it is not, swap it with the last unchecked element and the right zone grows (`r--`). We do **not** advance `l` after the swap, because the value that arrived at `l` has not been looked at yet. That is the single most common bug in this pattern.

Trace on `{3, -1, 4, -5, 2, -6}`:

| l | r | a[l] | action | array after |
|---|---|---|---|---|
| 0 | 5 | 3 | non-negative: swap with a[5], `r--` | -6 -1 4 -5 2 3 |
| 0 | 4 | -6 | negative: `l++` | same |
| 1 | 4 | -1 | negative: `l++` | same |
| 2 | 4 | 4 | swap with a[4], `r--` | -6 -1 2 -5 4 3 |
| 2 | 3 | 2 | swap with a[3], `r--` | -6 -1 -5 2 4 3 |
| 2 | 2 | -5 | negative: `l++` (now l > r, stop) | -6 -1 -5 2 4 3 |

O(n) time, O(1) space, each element is looked at once. This is exactly the partition step inside quicksort, and sorting three colours (Problem 5) is the same idea with three zones.

---

## 5. Complexity in one paragraph

Each pointer moves in one direction only, so the total number of moves is at most n (or m + n for two sequences): O(n) time and O(1) extra space. If you must **sort first**, add O(n log n), and that usually dominates. 3Sum, for example, sorts (n log n) and then runs an O(n) two-pointer pass for each of the n first elements, so O(n²) overall.

---

## 6. Mistakes that cost points

1. **`l < r` versus `l <= r`.** For pairs of different elements use `l < r` (a pointer must not pair with itself). In a partition, `l <= r` is needed so the last unchecked element is still examined.
2. **Moving neither pointer.** Check that every branch moves at least one pointer, or the loop never ends.
3. **Unsigned underflow.** `size_t r = a.size() - 1` is huge on an empty array. Use `int`.
4. **Skipping the duplicates step.** Problems that ask for *distinct* answers (3Sum) need explicit duplicate skipping, or you report the same answer several times.
5. **Using it on unsorted data** without a reason that rules candidates out. Ask the three questions in section 2.4.
6. **Advancing after a swap** in a partition (section 4). The swapped-in value is unchecked.
7. **Overflow.** The sum of two `int` values can overflow. Use `long long`, as the examples do.
8. **Index base.** Some problems want 1-based positions. Add 1 at the end, not in the middle of the logic.

---

## 7. Questions you will be asked

**When can two pointers replace nested loops?** When an order or monotonic property lets each step discard a group of candidates, typically sorted input, a palindrome-like symmetry, or a quantity that moves in one direction.

**Prove that the pair-sum algorithm cannot miss the answer.** Use the argument in section 2.2: the pointers start outside any valid pair, and each move is only made when the moved element cannot be part of one.

**Why does it not work on an unsorted array?** A sum that is too small or too big tells you nothing about the pointed-to element, because its partner could be anywhere. Use a hash map, or sort first.

**Two pointers, hash map or binary search for a pair sum?** Sorted and O(1) space: two pointers. Unsorted, one query, need original indices: hash map. Sorted with the first element fixed and looking for the partner: binary search (O(n log n)), which two pointers beats.

**What is the complexity of 3Sum, and can it be better?** O(n²) after the sort. No better general algorithm is known, so O(n²) is the expected answer.

**How do you handle duplicates?** Sort, then skip a value equal to the previous one at every level where you pick an element.

**How is this different from the sliding window?** Both use two indices. In a window both move in the same direction and the pair defines a *range* that you maintain; here the pointers are usually separate "candidates" that you discard. Tutorial 03 covers windows.

---

## 8. Problems

**How to use this section.** Try each problem for 15 minutes (easy) or 30 minutes (medium) before reading anything. If stuck, read only the **How** lines, then try again, and only then open the answer at the bottom. Log each attempt in the [practice log](../practice_log.md).

### On paper

**Problem A: Trace it.** Run `pairWithSum` by hand on `{2, 5, 8, 12, 15, 20}` with `target = 23`. Write the table of l, r, the sum, and the decision, as in section 2.2.
*Where:* paper. *How:* start with `l = 0, r = 5`; at each step compare the sum with 23.

**Problem B: Count it.** Run `countPairsBelow` by hand on `{1, 2, 3, 4, 5}` with `target = 7`. What is the answer, and how much is added at each step?
*Where:* paper. *How:* when the sum is below the target add `r - l`; otherwise move `r`.

### In code

**Problem 1: Valid Palindrome** (LeetCode 125, easy)
*Where:* leetcode.com/problems/valid-palindrome. *Goal:* is the string a palindrome when you ignore case and every character that is not a letter or digit?
*How:* section 2.1, but before each comparison move `l` forward past non-alphanumeric characters and `r` backward past them. Take care not to let the skipping loops run past each other (keep `l < r` in them).

**Problem 2: Two Sum II, Input Array Is Sorted** (LeetCode 167, easy)
*Where:* leetcode.com/problems/two-sum-ii-input-array-is-sorted. *Goal:* the 1-based positions of the two numbers that add to `target`, in a sorted array, using O(1) extra space.
*How:* this is section 2.2. Return `l + 1` and `r + 1`.

**Problem 3: Container With Most Water** (LeetCode 11, medium)
*Where:* leetcode.com/problems/container-with-most-water. *Goal:* choose two lines that, with the x-axis, hold the most water. The area is `min(height[l], height[r]) × (r − l)`.
*How:* start with the widest container (`l = 0`, `r = n − 1`). Every move shrinks the width, so to beat the current area you need a taller *shorter side*. Ask which line you can safely discard. Hint: moving the taller line can never help, because the area is capped by the shorter line and the width only shrinks.

**Problem 4: 3Sum** (LeetCode 15, medium)
*Where:* leetcode.com/problems/3sum. *Goal:* all unique triples that add to zero.
*How:* sort. Fix the first element `nums[i]`, then the rest is "two sum in a sorted array" for the target `-nums[i]` (section 2.2). The hard part is duplicates: skip a first value equal to the previous first value, and after you find a triple, skip repeated second values. You can stop early once `nums[i] > 0`.

**Problem 5: Sort Colors** (LeetCode 75, medium)
*Where:* leetcode.com/problems/sort-colors. *Goal:* the array holds only 0, 1 and 2. Sort it in place in one pass, without a sort function.
*How:* section 4 with three zones. Use three indices `lo`, `mid`, `hi`: everything before `lo` is 0, from `lo` to `mid` is 1, after `hi` is 2, and `[mid, hi]` is unchecked. Decide what to do for each value at `mid`, and when `mid` may advance.

**Problem 6: Trapping Rain Water** (LeetCode 42, hard; try it last)
*Where:* leetcode.com/problems/trapping-rain-water. *Goal:* how much rain water is held between the bars of an elevation map.
*How:* the water above bar `i` is `min(tallest bar to the left, tallest bar to the right) − height[i]`. First write the O(n²) version, then the version with two arrays of prefix and suffix maximums (O(n) space). The two-pointer version keeps the maximum on each side and always processes the side with the **smaller current bar**, because for that side the limiting wall is already known.

---

## Answers

Try before reading. Code is written as plain functions; LeetCode wraps it in `class Solution { public: ... };`.

### Problem A

| l | r | a[l] + a[r] | decision |
|---|---|---|---|
| 0 | 5 | 2 + 20 = 22 | too small, `l++` |
| 1 | 5 | 5 + 20 = 25 | too big, `r--` |
| 1 | 4 | 5 + 15 = 20 | too small, `l++` |
| 2 | 4 | 8 + 15 = 23 | found: indices (2, 4) |

### Problem B

| l | r | sum | decision | count |
|---|---|---|---|---|
| 0 | 4 | 1 + 5 = 6 | below 7: add r − l = 4, `l++` | 4 |
| 1 | 4 | 2 + 5 = 7 | not below: `r--` | 4 |
| 1 | 3 | 2 + 4 = 6 | below: add 2, `l++` | 6 |
| 2 | 3 | 3 + 4 = 7 | not below: `r--` | 6 |

The answer is **6**: (1,2), (1,3), (1,4), (1,5), (2,3), (2,4).

### Problem 1: Valid Palindrome

```cpp
bool isPalindromeAlnum(const std::string& s) {
    int l = 0, r = (int)s.size() - 1;
    while (l < r) {
        while (l < r && !std::isalnum((unsigned char)s[l])) ++l;   // skip spaces and punctuation
        while (l < r && !std::isalnum((unsigned char)s[r])) --r;
        if (std::tolower((unsigned char)s[l]) != std::tolower((unsigned char)s[r])) return false;
        ++l;
        --r;
    }
    return true;
}
```

O(n) time, O(1) space. The `(unsigned char)` cast avoids undefined behaviour in `isalnum` for negative `char` values. Strings with no letters or digits (like `".,"`) are palindromes.

### Problem 2: Two Sum II

```cpp
std::vector<int> twoSumSorted(const std::vector<int>& numbers, int target) {
    int l = 0, r = (int)numbers.size() - 1;
    while (l < r) {
        int sum = numbers[l] + numbers[r];
        if (sum == target) return {l + 1, r + 1};     // the problem wants 1-based positions
        if (sum < target) ++l;
        else              --r;
    }
    return {};
}
```

O(n) time, O(1) space. (Within LeetCode's limits `int` is enough; section 2.2 used `long long` as a safe habit.)

### Problem 3: Container With Most Water

```cpp
int maxArea(const std::vector<int>& height) {
    int l = 0, r = (int)height.size() - 1, best = 0;
    while (l < r) {
        best = std::max(best, std::min(height[l], height[r]) * (r - l));
        if (height[l] < height[r]) ++l;               // the shorter line limits the area: keeping it can only get worse
        else                       --r;
    }
    return best;
}
```

Why the shorter side moves: with the current pair, the area is limited by the shorter line. Keep the shorter line and move the other one, and the width shrinks while the height is still capped by that shorter line, so the area cannot improve. So the shorter line can never be part of a better container, and it is safe to discard. O(n) time, O(1) space.

### Problem 4: 3Sum

```cpp
std::vector<std::vector<int>> threeSum(std::vector<int> nums) {   // by value: we sort our own copy
    std::sort(nums.begin(), nums.end());
    std::vector<std::vector<int>> out;
    int n = (int)nums.size();
    for (int i = 0; i + 2 < n; ++i) {
        if (nums[i] > 0) break;                                    // smallest value is positive: no more triples
        if (i > 0 && nums[i] == nums[i - 1]) continue;             // same first value as before: skip
        int l = i + 1, r = n - 1;
        while (l < r) {
            long long sum = (long long)nums[i] + nums[l] + nums[r];
            if (sum < 0) ++l;
            else if (sum > 0) --r;
            else {
                out.push_back({nums[i], nums[l], nums[r]});
                ++l;
                --r;
                while (l < r && nums[l] == nums[l - 1]) ++l;       // skip duplicate second values
            }
        }
    }
    return out;
}
```

O(n²) time (the sort is only O(n log n)). Extra space is O(1) apart from the output, or O(n) if you count the copy we sort. Skipping duplicate *second* values is enough: once the first and second values are fixed, the third is determined, so no duplicate triple can be produced.

### Problem 5: Sort Colors

```cpp
void sortColors(std::vector<int>& nums) {
    int lo = 0, mid = 0, hi = (int)nums.size() - 1;
    while (mid <= hi) {
        if (nums[mid] == 0)      std::swap(nums[lo++], nums[mid++]);
        else if (nums[mid] == 1) ++mid;
        else                     std::swap(nums[mid], nums[hi--]);   // do not advance mid: the value swapped in is unchecked
    }
}
```

A 0 is swapped to the position just after the zeros; the value that comes back from there is always a 1 (or the same element), so `mid` can advance. A 2 is swapped to the back, and what arrives is unknown, so `mid` stays. One pass, O(n) time, O(1) space.

### Problem 6: Trapping Rain Water

```cpp
int trap(const std::vector<int>& height) {
    int l = 0, r = (int)height.size() - 1;
    int leftMax = 0, rightMax = 0, water = 0;
    while (l < r) {
        if (height[l] < height[r]) {                  // the left side is the limiting side
            leftMax = std::max(leftMax, height[l]);
            water += leftMax - height[l];
            ++l;
        } else {
            rightMax = std::max(rightMax, height[r]);
            water += rightMax - height[r];
            --r;
        }
    }
    return water;
}
```

Why it is correct: whenever `height[l] < height[r]`, the tallest wall at or beyond `r` is at least as tall as `leftMax` (every bar we passed on the left was shorter than some bar on the right). So the water above `l` is decided by `leftMax` alone, and we can count it now. The other branch is the mirror image. For `{0,1,0,2,1,0,1,3,2,1,2,1}` the answer is 6. O(n) time, O(1) space.

---

## Cheat sheet

- Two pointers works when order or monotonic structure lets each step **discard** candidates. Say what is discarded and why.
- Opposite ends: sorted pair sums, palindromes, container area. Sum too small: move `l`. Too big: move `r`.
- One pointer per sequence: merge, intersect, subsequence. Advance the pointer of the smaller element.
- Partition zones: write what each zone promises. After a swap, do not advance the pointer that received an unchecked value.
- Cost: O(n) for the pass, plus O(n log n) if you had to sort. Space O(1).
- Use `int` for pointers, `long long` for sums, `l < r` for pairs.
- Duplicates in the answer: sort, then skip equal values at each level.

---

## Sources

- Counts, outputs and timings in this tutorial come from the companion `02_two_pointers_demo.cpp` (it prints the number of checks it passed) on the machine described in the text. Times are that machine's, not yours.
- The algorithms and data structures are standard; for further reading see the "Extra practice and resources" section of the [area plan](00_plan.md), which lists the textbook chapters and free references for this area. The explanations and code are my own.
- LeetCode numbers and titles are from memory; check a number before you rely on it.

**Next:** tutorial 03, Sliding Window. Say "next" when you are ready.
