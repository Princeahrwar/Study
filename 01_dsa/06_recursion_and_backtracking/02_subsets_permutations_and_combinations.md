# DSA 06.02: Subsets, Permutations and Combinations

*Part of the [Recursion and backtracking plan](00_plan.md) · Time: about 7 hours (3 reading, 4 problems) · You need: [Recursion fundamentals](01_recursion_fundamentals.md) (the three questions, recursion trees, call counts) · Related: [Hash tables](../02_hashing/00_plan.md) (sets of answers) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- write any "list all the ways to choose or arrange" problem with one template: choose, explore, unchoose;
- generate all subsets, permutations and combinations, and say how many answers and how many calls each one makes before you run it;
- skip duplicate answers when the input has repeated values, and say why the skip rule has the exact form it has;
- solve combination sum, letter combinations, generate parentheses and palindrome partitioning from the same template;
- decide from the input size whether a search can finish at all.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 5,001 checks passed under AddressSanitizer with UBSan (GCC 13.3), against brute force (every bitmask for subsets and combinations, `std::next_permutation` for permutations, a coin-change count for combination sum, every `2n`-bit string for parentheses, every cut mask for palindrome partitions). Answer counts and call counts are exact and the same on every machine. The one timing (section 7) was measured on one machine: an x86-64 VM, 2 cores at 2.10 GHz, Ubuntu 24.04, GCC 13.3 with `-O2`, best of 3. The statements of the LeetCode problems are summarised from memory; titles were checked against pages that list them (see Sources), and no difficulty labels are given because none could be verified.

All listings need these headers: `<algorithm>`, `<string>`, `<utility>` and `<vector>`. They use `using Ints = std::vector<int>;` and `using Rows = std::vector<std::vector<int>>;`.

---

## 1. The template

### 1.1 Choose, explore, unchoose

Many problems ask for **all** the answers: all subsets, all orderings, all ways to split a string. The method is a recursion that builds one answer step by step and, at each step, tries every allowed choice:

```text
solve(state):
    if state is a complete answer: record a copy of it; return
    for each choice allowed in this state:
        choose:   add the choice to the state
        explore:  solve(state)
        unchoose: remove the choice again
```

The last line is what makes it **backtracking**: after exploring one choice, the state is put back exactly as it was, so the next choice starts from the same place. Without it, the leftovers of one branch would leak into the next.

Three questions write every problem in this tutorial. They are the three questions of tutorial 01 with a search flavour:

1. **State.** What do I carry down? A list `cur` of choices made so far, and often a position (`start`, `i`) or a flag array (`used`).
2. **Choices.** At this node, which choices are allowed? This is the `for` loop and its `if ... continue`.
3. **Answer or dead end.** When is `cur` a finished answer? When can I stop early because nothing below can be an answer (this is **pruning**, from section 4 on)?

### 1.2 The decision tree

The recursion tree of such a search is called the **decision tree**: the root is the empty state, each edge is a choice, each node is a state. The call count is the number of nodes, the stack depth is the height, and the number of answers is the number of nodes that are answers. Everything in this tutorial is read from that tree.

| Problem | State | Choices at a node | Answer when |
|---|---|---|---|
| Subsets | `cur`, `start` | any element at index `start` or later | every node |
| Permutations | `cur`, `used` | any unused element | `cur` has n items |
| Combinations | `cur`, `start` | any larger number | `cur` has k items |
| Combination sum | `cur`, `start`, `remain` | any element from `start`, with reuse | `remain` is 0 |
| Parentheses | `cur`, open, close | `(` if fewer than n open, `)` if fewer closes than opens | `cur` has 2n characters |
| Palindrome partition | `cur`, `start` | any palindromic piece starting at `start` | `start` is the end |

One detail that bites beginners: `out.push_back(cur)` **copies** `cur`, which is what you want, because `cur` keeps changing. Copying costs O(length), so the total work is at least (number of answers) times (answer length). That cost is unavoidable, since the output itself has that size.

---

## 2. Subsets

All subsets of `{1, 2, 3}`: 8 of them, from `{}` to `{1, 2, 3}`. In general 2^n subsets, because each element is in or out.

### 2.1 Three ways to write it

```cpp
// Include or exclude a[i], then go to i + 1. Answers are at the leaves.
void subsetsIE(const Ints& a, size_t i, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    if (i == a.size()) { out.push_back(cur); return; }
    subsetsIE(a, i + 1, cur, out, calls);   // exclude a[i]
    cur.push_back(a[i]);                    // choose
    subsetsIE(a, i + 1, cur, out, calls);   // explore
    cur.pop_back();                         // unchoose
}

// Start index: every node is an answer; the loop picks the next element.
void subsetsStart(const Ints& a, size_t start, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    out.push_back(cur);
    for (size_t i = start; i < a.size(); ++i) {
        cur.push_back(a[i]);
        subsetsStart(a, i + 1, cur, out, calls);
        cur.pop_back();
    }
}

// No recursion: bit i of mask says whether a[i] is in. Needs n < 63.
Rows subsetsMask(const Ints& a) {
    Rows out;
    for (unsigned long long mask = 0; mask < (1ULL << a.size()); ++mask) {
        Ints cur;
        for (size_t i = 0; i < a.size(); ++i)
            if (mask >> i & 1) cur.push_back(a[i]);
        out.push_back(cur);
    }
    return out;
}
```

**Include or exclude.** At index `i`, there are exactly two choices: leave `a[i]` out or put it in. The answer is complete only at `i == n`, so the answers are the leaves. The tree for `{1, 2}`:

```text
i=0                     {}
              exclude /    \ include 1
i=1               {}          {1}
          exclude /  \ incl 2   exclude /  \ incl 2
i=2            {}    {2}          {1}    {1,2}      <- 4 leaves = 4 answers
```

It makes `2^(n+1) - 1` calls: a full binary tree with `2^n` leaves.

**Start index.** Instead of deciding about each element, the loop picks the **next** element to add, from `start` onward, and the recursion continues after it. Now every node is an answer (the set chosen so far), and no set is generated twice, because elements are always added in index order. For `{1, 2, 3}` the calls come out as:

```text
{}
  {1}
    {1,2}
      {1,2,3}
    {1,3}
  {2}
    {2,3}
  {3}
```

This version makes exactly `2^n` calls, half of include/exclude, since there are no empty leaves to visit. For n = 10: 2,047 calls against 1,024 (measured by the counter in the code). It is the form to remember, because the same loop shape solves combinations and combination sum.

**Bitmask.** For n up to about 20 you do not need recursion: count `mask` from 0 to `2^n - 1` and let bit `i` mean "`a[i]` is in". The outputs of the three versions, for `{1, 2, 3}`:

| Version | Order of the 8 answers |
|---|---|
| Start index | `{}`, `{1}`, `{1,2}`, `{1,2,3}`, `{1,3}`, `{2}`, `{2,3}`, `{3}` |
| Include/exclude (exclude first) | `{}`, `{3}`, `{2}`, `{2,3}`, `{1}`, `{1,3}`, `{1,2}`, `{1,2,3}` |
| Bitmask | `{}`, `{1}`, `{2}`, `{1,2}`, `{3}`, `{1,3}`, `{2,3}`, `{1,2,3}` |

The same sets, three orders. In an interview the order rarely matters (LeetCode accepts any order for these problems, as far as I remember the statements); say so, and ask.

### 2.2 Duplicates: Subsets II

Now `a = {1, 2, 2}`. The start-index code would produce `{2}` twice (once with each 2), and `{1,2}` twice. We want each **distinct** set once.

The fix has two parts:

1. **Sort**, so equal values sit next to each other.
2. In the loop, **skip `a[i]` if it equals `a[i-1]` and `i > start`.**

```cpp
// a must be sorted. Skip a[i] when it equals a[i-1] and a[i-1] was not taken
// at this level (i > start).
void subsetsDup(const Ints& a, size_t start, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    out.push_back(cur);
    for (size_t i = start; i < a.size(); ++i) {
        if (i > start && a[i] == a[i - 1]) continue;
        cur.push_back(a[i]);
        subsetsDup(a, i + 1, cur, out, calls);
        cur.pop_back();
    }
}
```

Why it works. In one loop (one node), the first copy of a value starts a subtree that already contains every set the second copy could start: after choosing the first 2, the recursion continues from the second 2, so `{2}`, `{2,2}` and everything with them are generated. Choosing the second 2 first would only repeat part of that. So at one node, of several equal values you may only start with the **first**.

Why `i > start` and not `i > 0`. The first copy chosen at some level is allowed to be followed, at the next level, by the second copy: that is how `{2,2}` is made. At that next level `start` is the index of the second copy, so `i == start` and the rule does not skip it. With `i > 0` the second 2 would be skipped there too and `{2,2}` would be lost. Result for `{1, 2, 2}`:

```text
{}, {1}, {1,2}, {1,2,2}, {2}, {2,2}
```

Six distinct subsets (tested). For `{1, 2, 2, 2}` the rule gives 8 answers in 8 calls, where the plain start-index code makes 16 calls and 16 rows.

The alternative is to generate everything and put the rows into a `std::set<Ints>`. That is correct too, and simpler, but it does all the work of the duplicates and then throws it away. The skip rule never visits them.

---

## 3. Permutations

All orderings of `{1, 2, 3}`: `3! = 6`. At each position you may pick any element that is not used yet.

### 3.1 With a used array

```cpp
// used[i] says whether a[i] is already in cur.
void perms(const Ints& a, std::vector<char>& used, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    if (cur.size() == a.size()) { out.push_back(cur); return; }
    for (size_t i = 0; i < a.size(); ++i) {
        if (used[i]) continue;
        used[i] = 1;
        cur.push_back(a[i]);
        perms(a, used, cur, out, calls);
        cur.pop_back();
        used[i] = 0;
    }
}

// In place: a[0..k-1] is the chosen part, a[k..] is what is left.
void permsSwap(Ints& a, size_t k, Rows& out, long long& calls) {
    ++calls;
    if (k == a.size()) { out.push_back(a); return; }
    for (size_t i = k; i < a.size(); ++i) {
        std::swap(a[k], a[i]);
        permsSwap(a, k + 1, out, calls);
        std::swap(a[k], a[i]);
    }
}
```

The first function keeps `used[i]` flags. The decision tree for `{1, 2, 3}` has 1 root, 3 nodes at depth 1, 6 at depth 2 and 6 leaves at depth 3, which is 16 calls. The answers come out in lexicographic order when the input is sorted; the test compares with `std::next_permutation` row by row.

| n | Answers `n!` | Calls | Calls / `n!` |
|---|---|---|---|
| 0 | 1 | 1 | 1.00 |
| 1 | 1 | 2 | 2.00 |
| 2 | 2 | 5 | 2.50 |
| 3 | 6 | 16 | 2.67 |
| 4 | 24 | 65 | 2.71 |
| 5 | 120 | 326 | 2.72 |
| 6 | 720 | 1,957 | 2.72 |

The number of calls is `n! (1/0! + 1/1! + ... + 1/n!)`, which is about `e * n!`. So the cost is dominated by the leaves, and the tree is "all leaves, nearly".

### 3.2 In place with swaps

The second function needs no `used` array and no `cur`. The array itself is split in two: `a[0..k-1]` is the part chosen so far and `a[k..]` is what is left. To choose the next element, swap it into position `k`, recurse, swap back (that is the unchoose). For `{1, 2, 3}` the order is

```text
123, 132, 213, 231, 321, 312
```

which is not lexicographic. Same set of answers, same number of calls (tested). It uses less memory and is shorter, but it only works while the elements are distinct, and it gets a duplicate rule only with extra work, so for duplicates use the `used` version.

### 3.3 Duplicates: Permutations II

With `{1, 1, 2}` the plain code makes 6 rows but only 3 are different: `112`, `121`, `211`, each twice. The rule, after sorting:

```cpp
// a must be sorted. Among equal values, the left one must be used first.
void permsDup(const Ints& a, std::vector<char>& used, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    if (cur.size() == a.size()) { out.push_back(cur); return; }
    for (size_t i = 0; i < a.size(); ++i) {
        if (used[i]) continue;
        if (i > 0 && a[i] == a[i - 1] && !used[i - 1]) continue;
        used[i] = 1;
        cur.push_back(a[i]);
        permsDup(a, used, cur, out, calls);
        cur.pop_back();
        used[i] = 0;
    }
}
```

**Among equal values, always use the left one first.** If `a[i] == a[i-1]` and `a[i-1]` is not in use, then picking `a[i]` now would put the right copy before the left one, and the left copy would come later. That arrangement is the same as one where the left copy came first, so skip it. Result for `{1, 1, 2}`: `112`, `121`, `211` in 9 calls, where the plain code makes 16. For `{1, 2, 2}`: `122`, `212`, `221`, also 9 calls.

Note the difference with section 2.2. In Subsets II the question is "did I use the previous equal value **at this level of the loop**" (`i > start`). In Permutations II the loop always starts at 0, so the question is "is the previous equal value **already in `cur`**" (`used[i-1]`). Both say the same thing: equal copies are taken in left-to-right order.

An alternative that avoids the rule is `std::next_permutation` on a sorted array, which steps through the distinct permutations in order, one at a time, with no recursion and O(1) extra memory. For "print them all" it is the best tool; for "count the ones with a property while pruning", the recursion is needed because you can cut a branch early.

**Counting without storing.** If you only need the number of answers, or you handle each answer as it appears, storing them all is wasted memory. This version walks the same tree and keeps nothing:

```cpp
// Count without storing anything: how many permutations of n items?
long long countPerms(int n, int depth, std::vector<char>& used) {
    if (depth == n) return 1;
    long long total = 0;
    for (int i = 0; i < n; ++i) {
        if (used[i]) continue;
        used[i] = 1;
        total += countPerms(n, depth + 1, used);
        used[i] = 0;
    }
    return total;
}
```

It is used in section 7 to measure how fast the tree can be walked.

---

## 4. Combinations

Choose k numbers out of `1..n`, order does not matter: `{1,2}` and `{2,1}` are the same combination. The start-index form fits exactly: after picking `i`, only larger numbers may follow, so every combination is built in increasing order, once.

```cpp
// Choose k of 1..n. With prune, the loop stops where too few numbers remain.
void combine(int n, int k, int start, Ints& cur, Rows& out, long long& calls, bool prune) {
    ++calls;
    if ((int)cur.size() == k) { out.push_back(cur); return; }
    int need = k - (int)cur.size();
    int last = prune ? n - need + 1 : n;
    for (int i = start; i <= last; ++i) {
        cur.push_back(i);
        combine(n, k, i + 1, cur, out, calls, prune);
        cur.pop_back();
    }
}
```

For `n = 4`, `k = 2` the answers are `12, 13, 14, 23, 24, 34`.

### 4.1 Pruning by counting

When `cur` already has some items, `need = k - cur.size()` more are still required, and they must all come from numbers larger than the last one. If `i` is larger than `n - need + 1`, there are fewer than `need` numbers left (`i` to `n`), and the branch can never finish. So the loop stops at `n - need + 1`. This is the first **pruning rule** of the tutorial: cut a branch as soon as you can prove it has no answer below it.

| n | k | Answers `C(n,k)` | Calls with pruning | Calls without |
|---|---|---|---|---|
| 4 | 2 | 6 | 10 | 11 |
| 4 | 3 | 4 | 10 | 15 |
| 4 | 4 | 1 | 5 | 16 |
| 5 | 3 | 10 | 20 | 26 |
| 5 | 4 | 5 | 15 | 31 |
| 20 | 2 | 190 | 210 | 211 |
| 20 | 4 | 4,845 | 5,985 | 6,196 |

Two lessons. First, for small k the pruning saves little (210 against 211): the unpruned tree only wastes the calls at the very bottom. For k close to n it saves a lot (5 against 16). Second, with pruning every call is the start of at least one answer, so the number of calls is at most `(k + 1)` times the number of answers (tested for all `n` up to 9 and every `k`). That is the best you can hope for in a search: **the work is proportional to the output**.

---

## 5. Combination sum

Given positive numbers and a target, list all combinations that add up to the target, where a number may be used again and again (LeetCode 39). With `{2, 3, 6, 7}` and target 7: `{2,2,3}` and `{7}`.

Two changes from combinations. The recursion does not go to `i + 1` but to **`i`**, because the same number may be taken again; and it carries `remain`, the target minus the sum so far. The answer is `remain == 0`.

```cpp
// a sorted ascending, all values positive. Each value may be used many times,
// so the next call starts at i, not i + 1.
void combSum(const Ints& a, size_t start, int remain, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    if (remain == 0) { out.push_back(cur); return; }
    for (size_t i = start; i < a.size(); ++i) {
        if (a[i] > remain) break;
        cur.push_back(a[i]);
        combSum(a, i, remain - a[i], cur, out, calls);
        cur.pop_back();
    }
}

// a sorted. Each element once, and equal values must not give equal answers.
void combSum2(const Ints& a, size_t start, int remain, Ints& cur, Rows& out, long long& calls) {
    ++calls;
    if (remain == 0) { out.push_back(cur); return; }
    for (size_t i = start; i < a.size(); ++i) {
        if (a[i] > remain) break;
        if (i > start && a[i] == a[i - 1]) continue;
        cur.push_back(a[i]);
        combSum2(a, i + 1, remain - a[i], cur, out, calls);
        cur.pop_back();
    }
}
```

**Why it stops.** All values are positive, so every call lowers `remain` by at least 1, and the depth is at most `target` divided by the smallest value. With a zero or negative value the recursion could run forever; the problem statement promises positive values, and you should say that you rely on it.

**Why `break` and not `continue`.** The array is sorted, so when `a[i] > remain`, every later value is too big as well; `break` skips them all. On an unsorted array you would have to use `continue`, or sort first. For `{2, 3, 6, 7}`, target 7, this makes 10 calls.

**Why start at `i` and not at 0.** Starting at 0 would let the search take `2,3` and `3,2` as different answers. Starting at `i` keeps every answer in non-decreasing order, so each combination appears once.

**Each element once, with duplicates (Combination Sum II, LeetCode 40).** Now the input may contain equal values and each element may be used once. Combine two earlier rules: go to `i + 1` as in section 4, and skip equal neighbours as in section 2.2.

For `{1, 1, 2, 5}` and target 6 the only answer is `{1, 5}`, even though there are two 1s; without the skip rule it would be listed twice. This takes 8 calls, and the test compared the output with a brute force over all subsets, as a set.

---

## 6. Strings: letters, parentheses and palindromes

The same template works when the state is a string, and the choices are characters or pieces.

```cpp
void letters(const std::string& digits, size_t pos, std::string& cur,
             std::vector<std::string>& out, long long& calls) {
    static const char* keys[] = {"", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
    ++calls;
    if (pos == digits.size()) { out.push_back(cur); return; }
    for (const char* p = keys[digits[pos] - '0']; *p; ++p) {
        cur.push_back(*p);
        letters(digits, pos + 1, cur, out, calls);
        cur.pop_back();
    }
}

void parens(int n, int open, int close, std::string& cur,
            std::vector<std::string>& out, long long& calls) {
    ++calls;
    if ((int)cur.size() == 2 * n) { out.push_back(cur); return; }
    if (open < n) {
        cur.push_back('(');
        parens(n, open + 1, close, cur, out, calls);
        cur.pop_back();
    }
    if (close < open) {
        cur.push_back(')');
        parens(n, open, close + 1, cur, out, calls);
        cur.pop_back();
    }
}

bool isPal(const std::string& s, int l, int r) {
    while (l < r) if (s[l++] != s[r--]) return false;
    return true;
}

void palParts(const std::string& s, size_t start, std::vector<std::string>& cur,
              std::vector<std::vector<std::string>>& out, long long& calls) {
    ++calls;
    if (start == s.size()) { out.push_back(cur); return; }
    for (size_t end = start; end < s.size(); ++end) {
        if (!isPal(s, (int)start, (int)end)) continue;
        cur.push_back(s.substr(start, end - start + 1));
        palParts(s, end + 1, cur, out, calls);
        cur.pop_back();
    }
}
```

**Letter combinations (LeetCode 17).** Each digit maps to 3 or 4 letters, the recursion walks the digits, and the loop is over the letters of the current digit. For `"23"` the answers are `ad ae af bd be bf cd ce cf`, 9 answers in 13 calls. The number of answers is the product of the letter counts (3 or 4 per digit), so with n digits it is at most `4^n`. An empty input should give an empty list, not a list with one empty string; the wrapper has to handle that.

**Generate parentheses (LeetCode 22).** All strings of n pairs that are balanced. Do not generate all `2^(2n)` strings and filter them. Build the string left to right and allow a choice only if the prefix can still be completed:

- `(` is allowed while fewer than n have been opened;
- `)` is allowed while fewer have been closed than opened.

Every prefix the search builds is the start of a valid string, so no branch is wasted. The numbers (tested against brute force on all strings up to n = 8):

| n | Answers (Catalan) | Calls | All strings `4^n` |
|---|---|---|---|
| 1 | 1 | 3 | 4 |
| 2 | 2 | 8 | 16 |
| 3 | 5 | 22 | 64 |
| 4 | 14 | 64 | 256 |
| 5 | 42 | 196 | 1,024 |
| 6 | 132 | 625 | 4,096 |
| 7 | 429 | 2,055 | 16,384 |
| 8 | 1,430 | 6,917 | 65,536 |

At n = 8 the search makes 6,917 calls where the filter would test 65,536 strings, and the gap widens with n. The number of answers is the Catalan number `C(2n, n) / (n + 1)`.

**Palindrome partitioning (LeetCode 131).** Cut a string into pieces that are all palindromes. The state is `start`, the first character not yet in a piece; the loop picks where the next piece ends; a piece is a choice only if it is a palindrome. That test is the pruning: a non-palindromic piece is never extended. For `"aab"` the answers are `a|a|b` and `aa|b`; for `"abc"` there is one, `a|b|c`. For a string of n equal letters every cut is allowed: `2^(n-1)` answers (for n = 8: 128 answers, 256 calls). That is the worst case. Each `isPal` call costs O(length), so the work per node is not O(1); a table of palindromic substrings, built by dynamic programming, makes the test O(1) (you will meet this in the DP area).

---

## 7. Count the search space before you run it

Before writing the code, estimate how many answers there are. If the answers alone are too many to list, no cleverness will help; if the number of answers is small but the number of nodes is huge, you need pruning.

| Problem | Answers | n = 10 | n = 20 |
|---|---|---|---|
| Subsets | `2^n` | 1,024 | 1,048,576 |
| Permutations | `n!` | 3,628,800 | 2,432,902,008,176,640,000 |
| Combinations `C(n, n/2)` | | 252 | 184,756 |
| Balanced parentheses | Catalan(n) | 16,796 | 6,564,120,420 |
| Palindrome partitions of `aaa...a` | `2^(n-1)` | 512 | 524,288 |

**What the walk costs.** `countPerms` visits every permutation without storing any. Measured (best of 3, `-O2`, this VM, counting only):

| n | Permutations | Time |
|---|---|---|
| 8 | 40,320 | 1 ms |
| 9 | 362,880 | 7 ms |
| 10 | 3,628,800 | 83 ms |
| 11 | 39,916,800 | 1,064 ms |

Each step multiplies the work by about n, so n = 12 would take roughly 13 seconds and n = 13 about three minutes (extrapolated from the table; not measured). In practice, "all permutations" is for n up to about 10 or 11 and "all subsets" up to about 20 to 25; beyond that, the problem is asking for something else (a count by dynamic programming, a best answer instead of all answers, or a search that prunes).

**Memory.** Storing the answers can be the real limit. All subsets of 20 numbers hold `n * 2^(n-1) = 10,485,760` numbers in total: about 42 MB of data at 4 bytes each, before the overhead of one vector per subset. If the caller only needs to look at each answer once, pass a callback instead of filling a vector.

**Depth.** The stack depth is the length of an answer (n, or 2n), small here. It matters in tutorial 03.

---

## 8. Common mistakes

| Mistake | What happens | Fix |
|---|---|---|
| Forgetting the unchoose (`pop_back`, `used[i] = 0`, the second swap) | State leaks into the next branch: wrong or growing answers | Pair every choose with an unchoose in the same function |
| Recording `cur` by reference or pointer | All rows end up equal to the final `cur` | Store a copy (`out.push_back(cur)` copies a vector) |
| Skipping duplicates without sorting | Equal values are not neighbours; duplicates remain | Sort first |
| Using `i > 0` instead of `i > start` in Subsets II and Combination Sum II | Answers that need two equal values are lost, such as `{2,2}` | `i > start` |
| Recursing on `i + 1` where reuse is allowed | Reuse is lost: `{2,2,3}` is never found | Pass `i` |
| Recursing on `start` or 0 in the start-index form | The same set in many orders: duplicates | Pass `i` or `i + 1` |
| `break` on an unsorted array | Valid branches are cut | Sort, or use `continue` |
| Allowing zero or negative values in combination sum with reuse | Infinite recursion | Check the statement; otherwise bound the depth |
| Not estimating the number of answers | A search that cannot finish | Estimate the answer count first (section 7) |
| Returning the empty list wrongly for empty input | LeetCode 17 expects `[]`, not `[""]` | Handle the empty input before the recursion |

---

## 9. Questions to answer out loud

1. What are choose, explore and unchoose, and what goes wrong without the third?
2. How many calls does include/exclude make for n elements, and how many does the start-index form make? Why the difference?
3. How many leaves and how many calls has the permutation tree for n = 4?
4. Why does Subsets II test `i > start`? What breaks with `i > 0`?
5. What does `!used[i - 1]` mean in Permutations II, and why does it remove duplicates?
6. When is `std::next_permutation` the better tool, and when is the recursion?
7. Why does combination sum recurse on `i`, and why does combination sum II recurse on `i + 1`?
8. How do you prune combinations (n, k), and how much does it save for small k?
9. Why is generating parentheses by filtering all strings worse than building valid prefixes?
10. For n = 15, which of subsets, permutations and Catalan can you list, and which not?

---

## 10. Problems

Log every attempt in [`../practice_log.md`](../practice_log.md). Titles and numbers were checked (see Sources); statements are summarised from memory, so read the real statement first.

### 10.1 On paper

**Problem A.** Draw the start-index tree for `{1, 2, 2, 3}` with the duplicate rule, and list the answers. How many calls?

*Where:* paper. *Goal:* the 12 subsets. *How:* at each node write the loop range; cross out the skipped `2`.

**Problem B.** Draw the permutation tree for `{1, 2, 2}` with the duplicate rule. How many calls, and which branches are cut?

*Where:* paper. *Goal:* 3 answers. *How:* at the root, the second `2` is skipped because the first is unused.

**Problem C.** List combinations (5, 3) and count the calls with and without pruning.

*Where:* paper. *Goal:* 10 answers, two call counts. *How:* use the loop limit `n - need + 1` at each node.

**Problem D.** Run combination sum on `{2, 3, 5}` with target 8 by hand. List the answers and the calls.

*Where:* paper. *Goal:* all combinations with reuse. *How:* write `remain` at each node; stop a loop when `a[i] > remain`.

**Problem E.** List the strings for n = 3 in the order of the parentheses code, and count the calls.

*Where:* paper. *Goal:* 5 strings. *How:* at each node, test `open < n` and `close < open`.

**Problem F.** Each change below breaks one listing. Say what output it gives for `{1, 2, 3}` or the given input. (a) `subsetsStart` with the loop recursing on `start + 1`. (b) `perms` without `used[i] = 0` after the recursion. (c) `combSum` recursing on `i + 1`.

*Where:* paper. *Goal:* the wrong output. *How:* trace the first two or three calls.

### 10.2 In your editor

For each, write a brute force first (bitmasks or `next_permutation`), then the backtracking, and compare on random small inputs.

1. **Subsets (78).** Where: LeetCode. Goal: all subsets of distinct numbers. How: the start-index form.
2. **Subsets II (90).** Where: LeetCode. Goal: no duplicate subsets. How: sort, `i > start`.
3. **Permutations (46).** Where: LeetCode. Goal: all orderings. How: `used` array; try the swap form too.
4. **Permutations II (47).** Where: LeetCode. Goal: distinct orderings of a multiset. How: sort, `used[i-1]` rule.
5. **Combinations (77).** Where: LeetCode. Goal: all k-subsets of `1..n`. How: add the `n - need + 1` limit.
6. **Combination Sum (39).** Where: LeetCode. Goal: reuse allowed. How: recurse on `i`; sort and `break`.
7. **Combination Sum II (40).** Where: LeetCode. Goal: each once, no duplicate answers. How: recurse on `i + 1`; skip equal neighbours.
8. **Letter Combinations of a Phone Number (17).** Where: LeetCode. Goal: every letter string. How: one recursion level per digit; empty input gives `[]`.
9. **Generate Parentheses (22).** Where: LeetCode. Goal: all balanced strings. How: two counters.
10. **Palindrome Partitioning (131).** Where: LeetCode. Goal: all cuts into palindromes. How: `start` plus a loop on the end; try a table of palindromes afterwards.

**Extra.** Write `subsetsDup` without sorting, using a `std::set<Ints>` of results, and compare the number of calls with the sorted version on an input with many repeats.

---

## Answers

**Problem A.** The answers: `{}`, `{1}`, `{1,2}`, `{1,2,2}`, `{1,2,2,3}`, `{1,2,3}`, `{1,3}`, `{2}`, `{2,2}`, `{2,2,3}`, `{2,3}`, `{3}`: 12 subsets in 12 calls (every call is an answer). Without the rule there would be 16.

**Problem B.** The answers: `122`, `212`, `221`, in 9 calls (the plain code makes 16 calls for 6 rows). At the root the loop tries index 0 (value 1), index 1 (value 2) and skips index 2, because it equals index 1 which is unused. The same cut happens one level down, under `1`, where the second `2` is skipped again. Under `2` nothing is skipped: the first `2` is in use, so the second may follow.

**Problem C.** Answers: `123, 124, 125, 134, 135, 145, 234, 235, 245, 345`: 10. Calls: 20 with pruning, 26 without (both measured).

**Problem D.** Answers: `{2,2,2,2}`, `{2,3,3}`, `{3,5}` in 13 calls. For example from `{2}` with remain 6: `2` gives remain 4, then 2 gives remain 2, then 2 gives remain 0.

**Problem E.** `((()))`, `(()())`, `(())()`, `()(())`, `()()()` in that order, 22 calls.

**Problem F.** (a) With `start + 1` in the recursive call, the loop still begins at `start`, which is the depth, not the index after `i`: for `{1,2,3}` the output has 16 rows, including `{2,2}`, `{1,3,3}` and `{3,3,3}`, so elements repeat and sets appear in several orders. (b) Without `used[i] = 0` the first leaf `123` is found and every flag stays set, so the output has 1 row. (c) With `i + 1` in `combSum`, for `{2,3,6,7}` and target 7 only `{7}` is found; `{2,2,3}` is lost. These three were run on the changed code.

**Problems 10.2.** All ten follow the listings. For 77, the pruned loop limit is the difference between a pass and a time limit in the worst cases. For 17, the number of leaves is the product of the letter counts of the digits. For 22, compare your calls with the table of section 6. For 131, the number of leaves for `aaaaaaaa` is 128.

**Extra.** The set version makes the same calls as the plain start-index code (16 for `{1,2,2,2}`) and only removes the duplicate rows afterwards; the sorted version makes 8.

---

## Cheat sheet

- Template: choose, explore, unchoose; record a copy of the state at an answer.
- Subsets: start index, every node an answer, `2^n` calls (include/exclude: `2^(n+1) - 1`).
- Permutations: `used` array or swaps; `n!` leaves, about `e * n!` calls.
- Combinations: start at `i + 1`, stop the loop at `n - need + 1`.
- Combination sum: recurse on `i` with reuse, `i + 1` without; sort and `break` on `a[i] > remain`.
- Duplicates: sort first; Subsets II and Combination Sum II skip `i > start && a[i] == a[i-1]`; Permutations II skips `i > 0 && a[i] == a[i-1] && !used[i-1]`.
- Parentheses: `(` while `open < n`, `)` while `close < open`; Catalan answers.
- Palindrome partition: only palindromic pieces; worst case `2^(n-1)`.
- Count first: `n!` is out of reach beyond n of about 11, `2^n` beyond about 25.

---

## Sources

- Titles and numbers of 78, 90, 46, 47, 77, 39, 40, 17, 22 and 131 were checked against two pages that list them: [talentd.in NeetCode 150 sheet](https://www.talentd.in/fleetcode/sheets/neetcode-150) and [github.com/goalong/lc](https://github.com/goalong/lc). Between them, all ten appear. Difficulty labels are not used because I could not verify them.
- The statements of the problems are summarised from memory.
- All other claims in this tutorial come from the tests and measurements described at the top.
