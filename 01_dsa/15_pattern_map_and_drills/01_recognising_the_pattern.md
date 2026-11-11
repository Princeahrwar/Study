# DSA 15.01: Recognising the Pattern

*Part of the [Pattern map and drills plan](00_plan.md) · Time: about 6 hours of reading and four timed sets · You need: the earlier DSA areas ([arrays and strings](../01_arrays_and_strings/00_plan.md) to [theory and advanced techniques](../14_theory_and_advanced_techniques/00_plan.md)); this tutorial ties them together, so skim the ones you have not met*

Every earlier tutorial taught a technique. An interview does not name the technique. It gives you a sentence and some limits, and the first five minutes decide whether you pick the technique that fits or spend half an hour on one that cannot work. This tutorial is about that choice. It gives you a way to read the **constraints**, a **table from the words of the statement to a technique**, two problems solved several ways with measured times (so you see which way wins and when it stops winning), a catalogue of **follow-up questions** and what each does to your answer, and mixed timed sets that draw from every area.

By the end you can:

- turn the limits of a problem (the size of `n`, the range of the values) into a growth rate you can afford, and know what a second of work looks like on a real machine (measured);
- go from a phrase in the statement ("shortest", "all subsets", "next greater", "k-th largest", "connected") to the technique that usually solves it, and name the area that teaches it;
- solve one problem by brute force, by a hash map and by two pointers, and say which input property makes each one valid (and what breaks when it does not: measured);
- compare sorting, a heap and selection for the k-th largest, and say how the size of `k` changes the winner;
- answer the usual follow-ups (a stream, negative numbers, little memory, updates, duplicates) by saying what changes in the solution;
- run a timed set: read, match, say the brute force, improve it, code it, test it, and stop at the limit.

**Where the numbers come from.** Code compiled with `-Wall -Wextra` (GCC 13.3). 1,180,071 checks passed under AddressSanitizer and UBSan: each counting method against the brute force over every `k` from -2 to 8 on random small arrays (non-negative and with negative values), each k-th largest method against a sort of every `k`, the stream class, and the boundaries of the budget table on both sides. I tried 16 deliberate bugs (an off-by-one in every threshold of the table, a missing empty prefix, a lookup done after the insert, a window that shrinks on equality, a heap that pops one too early, an index off by one in the selection, and others). All 16 were caught. The two-pointer count was also checked to be **wrong** on negative values, as the text claims (section 4). The lists of problems and the answers to the pattern drills are my own judgement; the LeetCode numbers are from memory.

**Data and timing.** Machine: a cloud container, Intel Xeon 2.10 GHz, GCC 13.3.0, `-O2`, one thread. Times are the best of 3 repetitions, shown as run 1 / run 2. Data is random from a seeded generator, so every run gives the same counts. The timed jobs in section 1 use **light** inner loops, so they show how fast the best case is; heavier work per step (hashing, allocation, recursion) costs several times more.

---

## 1. Read the constraints first

The size of the input tells you which growth rates can finish. A rule of thumb that most judges follow is **about 10^8 simple steps in one second**. The table maps the largest `n` to the growth rate you can afford; the function below it is the same table as code, and the tests check every boundary on both sides.

```cpp
// The growth rate that usually runs in about a second for an input of size n.
const char* budget_class(long long n) {
    if (n <= 10) return "n! (all permutations)";
    if (n <= 25) return "2^n (all subsets)";
    if (n <= 40) return "2^(n/2) (meet in the middle)";
    if (n <= 500) return "n^3";
    if (n <= 10000) return "n^2";
    if (n <= 1000000) return "n log n";
    return "n or log n";
}
```

| `n` up to | Growth rate | Typical technique |
|---|---|---|
| 10 | `n!` | try every order: permutations, backtracking |
| 25 | `2^n` | every subset: bitmask, backtracking with pruning |
| 40 | `2^(n/2)` | meet in the middle (14.03) |
| 500 | `n^3` | three nested loops: Floyd-Warshall, interval DP |
| 10,000 | `n^2` | two nested loops: simple DP over pairs, all pairs |
| 10^6 | `n log n` | sort, heap, balanced tree, binary search, segment tree |
| above 10^6 | `n` or `log n` | one pass, a formula, binary search, hashing |

Here is what that looks like on this machine: one representative job at the largest `n` of each row, with a light inner step.

```
T1 work at the largest n of each class: 10! = 3628800 permutations; 2^25 = 33554432 subsets; meet in the middle at 40: 2 * 2^20 = 2097152 sums plus a sort; 500^3 = 125000000 steps; 10000 choose 2 = 49995000 pairs; n log n at 10^6: about 1000000 * 20 steps
```

| Job | Time (ms), run 1 / run 2 |
|---|---|
| all 10! permutations of 10 numbers | 10.4 / 10.6 |
| 2^25 subsets (counting those with 12 elements) | 64.9 / 60.4 |
| meet in the middle at `n = 40` (two halves of 2^20 sums, sort, two pointers) | 154.5 / 160.8 |
| 500^3 steps (Floyd-Warshall on 500 vertices) | 60.9 / 62.9 |
| 10,000 choose 2 pairs | 30.6 / 30.1 |
| sort 10^6 integers | 67.8 / 57.9 |
| add up 10^7 integers | 5.1 / 5.9 |

Every job finished in 5 to 160 ms, well inside a second, so the table is **conservative for light steps** and about right when each step does real work (section 3 shows a hash map step costing about 10 times a two-pointer step). Use the table to **rule techniques out**: with `n = 10^5`, a quadratic method (10^10 steps) cannot finish, and you have just saved yourself twenty minutes. Three more things the constraints tell you:

- **The range of the values** can matter more than `n`. Small values (0 to 100) allow counting arrays or a DP indexed by value; huge values need hashing or compression (14.03).
- **The answer modulo something** (`10^9 + 7`) means the count is large and usually comes from DP or combinatorics.
- **`n` given as two numbers** (a grid of `m` by `n`, a graph with `V` and `E`) means the cost is a product or a sum of them, and the bound you need is on that.

## 2. From the words of the statement to a technique

Read the statement for the **verb and the shape of the answer**. The table lists the clues I use, the technique that most often follows, and where it is taught. It gives a first guess to check against the constraints, not a rule.

| Clue in the statement | First technique to try | Area |
|---|---|---|
| sorted array; find a pair or a target | two pointers, or binary search | [01](../01_arrays_and_strings/00_plan.md), [03](../03_binary_search_and_sorting/00_plan.md) |
| contiguous subarray or substring; longest, shortest, count | sliding window (if values are non-negative or the condition is monotone), else prefix sums with a hash map | [01](../01_arrays_and_strings/00_plan.md), [02](../02_hashing/00_plan.md) |
| "have I seen this", count of each, group by a key | hash set or hash map | [02](../02_hashing/00_plan.md) |
| "minimum value so that something is possible" (capacity, speed, days) | binary search on the answer | [03](../03_binary_search_and_sorting/00_plan.md) |
| "next greater", "previous smaller", span, histogram | monotonic stack | [05](../05_stacks_and_queues/00_plan.md) |
| brackets, nested structure, undo | stack | [05](../05_stacks_and_queues/00_plan.md) |
| cycle in a linked list, middle, k-th from the end | fast and slow pointers | [04](../04_linked_lists/00_plan.md) |
| "all subsets", "all arrangements", "all valid ways" with `n` up to about 20 | backtracking, bitmask | [06](../06_recursion_and_backtracking/00_plan.md) |
| shortest path in an unweighted grid or graph | BFS | [08](../08_graphs/00_plan.md) |
| shortest path with weights | Dijkstra (no negative edges), Bellman-Ford (negative edges) | [08](../08_graphs/00_plan.md) |
| dependencies, ordering, "can all be finished" | topological sort, cycle detection | [08](../08_graphs/00_plan.md) |
| "are these connected", groups that merge | union-find | [07](../07_trees/00_plan.md), [08](../08_graphs/00_plan.md) |
| tree: depth, paths, ancestors | DFS (recursion), the "return a value and update the answer" shape | [07](../07_trees/00_plan.md) |
| k largest, k most frequent, median of a stream, merge k lists | heap | [07](../07_trees/00_plan.md) |
| range sum or minimum with updates | Fenwick tree, segment tree | [07](../07_trees/00_plan.md) |
| intervals: merge, insert, rooms, overlap | sort by start, then sweep (with a heap for rooms) | [09](../09_greedy_and_intervals/00_plan.md) |
| "maximum or minimum total, choosing among options" with overlapping subproblems | dynamic programming | [10](../10_dynamic_programming/00_plan.md) |
| "locally best choice is safe" (jump game, scheduling) | greedy, proved by an exchange argument | [09](../09_greedy_and_intervals/00_plan.md) |
| powers of two, parity, "appears once", subsets as masks | bit tricks | [11](../11_bits_and_math/00_plan.md) |
| find a pattern in a text, repeated substrings | KMP, Z, rolling hash, suffix structures | [12](../12_string_algorithms/00_plan.md) |
| cache, iterator, "design a ..." | a hash map plus a list, a heap, a stack, per-key time lists | [13](../13_design_style_coding/00_plan.md) |
| `n` about 40, or "too large for `2^n`" | meet in the middle | [14](../14_theory_and_advanced_techniques/00_plan.md) |
| many queries on a static array | prefix sums, sparse table, offline sorting | [01](../01_arrays_and_strings/00_plan.md), [14](../14_theory_and_advanced_techniques/00_plan.md) |

When two rows fit, **the constraints decide**. "Shortest path" with `n` up to 10^6 cannot use an `n^2` method; "all subsets" with `n = 30` cannot use plain `2^n`. When no row fits, solve a **smaller** version by hand and look for the structure: what is repeated, what is sorted, what can be reused.

A short decision flow I use in the room:

1. Is the input ordered, or can I sort it? Sorting costs `n log n` and often opens two pointers, binary search or a sweep.
2. Do I need to remember what I have seen? A hash map. In order? A stack, queue or heap. By range? A tree or prefix sums.
3. Is there a graph, even when the statement does not say so (cells, states, dependencies)? Choose BFS, DFS, Dijkstra or union-find by what the edges mean.
4. Does a choice now affect the best later choices, with repeated subproblems? Dynamic programming. Does it **not** (and can I argue it)? Greedy.
5. Do I have to list everything? Backtracking, and check `n`.

## 3. One problem, three ways: subarrays with a given sum

The problem (LeetCode 560, number from memory): count the contiguous subarrays of an integer array whose sum is exactly `k`. Three solutions, each valid under different conditions.

```cpp
// Count the subarrays whose sum is exactly k.
// Way 1: try every pair of ends. O(n^2), works for any integers.
long long count_brute(const vector<int>& a, long long k) {
    long long count = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        long long sum = 0;
        for (size_t j = i; j < a.size(); ++j) {
            sum += a[j];
            if (sum == k) ++count;
        }
    }
    return count;
}

// Way 2: prefix sums and a hash map. O(n), works for any integers.
long long count_hash(const vector<int>& a, long long k) {
    unordered_map<long long, int> seen;
    seen[0] = 1;                       // the empty prefix
    long long sum = 0, count = 0;
    for (int x : a) {
        sum += x;
        auto it = seen.find(sum - k);  // look up before inserting this prefix
        if (it != seen.end()) count += it->second;
        ++seen[sum];
    }
    return count;
}

// Way 3: two pointers. O(n) and O(1) memory, but only for values >= 0.
long long at_most(const vector<int>& a, long long k) {
    if (k < 0) return 0;
    long long sum = 0, count = 0;
    size_t lo = 0;
    for (size_t hi = 0; hi < a.size(); ++hi) {
        sum += a[hi];
        while (sum > k) sum -= a[lo++];
        count += hi - lo + 1;          // every start in [lo, hi] works
    }
    return count;
}
long long count_window(const vector<int>& a, long long k) {
    return at_most(a, k) - at_most(a, k - 1);
}
```

- **Brute force:** for every start, extend the end and keep a running sum. `O(n^2)` time, `O(1)` memory, any integers. It is the answer you say first, and the one you test the others against.
- **Prefix sums and a hash map:** the sum of `a[i..j]` is `P[j+1] - P[i]`, so for each end look up how many earlier prefixes equal `P - k`. `O(n)` time, `O(n)` memory, **any integers**. (The empty prefix, with sum 0, must be in the map before you start, and the lookup must come **before** the insert; otherwise subarrays that start at index 0, or have `k = 0`, are lost.)
- **Two pointers:** count subarrays with sum **at most** `k` and subtract those with at most `k - 1`. `O(n)` time, `O(1)` memory, but it needs values `>= 0`: then the sum only grows when the right end moves and only shrinks when the left end moves, so one pass is enough.

A trace on `[1, 0, 2, 0, 1, 1]` with `k = 2` (the answer is in section 6, problem A): the subarrays with sum 2 are `[2]`, `[0,2]`, `[2,0]`, `[0,2,0]`, `[0,1,1]` and `[1,1]`.

Measured, with values 0 to 4 and `k = 50`:

```
T2 values 0 to 4, k = 50: n = 30000 counts brute 15101, hash 15101, window 15101 (all agree: yes); n = 1000000 counts hash 500166, window 500166 (agree: yes)
```

| Job | Time (ms), run 1 / run 2 |
|---|---|
| brute force, `n = 30,000` | 213.5 / 217.3 |
| prefix sums and hash map, `n = 30,000` | 1.9 / 2.0 |
| two pointers, `n = 30,000` | 0.4 / 0.4 |
| prefix sums and hash map, `n = 1,000,000` | 145.9 / 131.9 |
| two pointers, `n = 1,000,000` | 15.1 / 13.3 |

At 30,000 elements brute force took over 200 ms against 2 ms for the hash map: about 100 times. Quadratic growth makes it worse: at 10^6 elements it would need about 1,100 times more, so a few minutes (extrapolated from the 30,000 run, not run). The two linear methods differ by a constant: two pointers is about 10 times faster than the hash map, because it touches memory in order and never hashes. **A better growth rate is the first thing to look for; the constant factor comes second.**

### 3.1 What breaks when the condition is false

Two pointers is fastest, but it is **wrong** when the array has negative numbers: removing the left element can make the sum bigger, so the window cannot decide to shrink by looking at the sum alone. I counted it:

```
T3 2000 random arrays of 12 values from -3 to 3, k = 3: two pointers wrong on 1791, prefix sums with a hash map wrong on 0
```

On 1,791 of 2,000 random small arrays with values from -3 to 3 the two-pointer count was wrong, and it never crashed or warned, so tests on non-negative data alone would pass it. The smallest counterexample is `[1, -1]` with `k = 1` (section 10, problem B). The hash map version is right for all integers. This is the pattern for most "which technique" decisions: **each technique has a condition, and the follow-up question is usually about breaking that condition**. Say the condition out loud when you choose.

## 4. A second problem, four ways: the k-th largest

The problem (LeetCode 215, number from memory): return the `k`-th largest value of an array. Three methods, and the stream version as a class.

```cpp
// The k-th largest value (k = 1 is the maximum).
// Way 1: sort. O(n log n).
int kth_sort(vector<int> a, int k) {
    sort(a.begin(), a.end(), greater<int>());
    return a[k - 1];
}

// Way 2: a min-heap that keeps the k largest so far. O(n log k), O(k) memory.
int kth_heap(const vector<int>& a, int k) {
    priority_queue<int, vector<int>, greater<int>> heap;
    for (int x : a) {
        heap.push(x);
        if ((int)heap.size() > k) heap.pop();
    }
    return heap.top();
}

// Way 3: selection. O(n) on average.
int kth_select(vector<int> a, int k) {
    nth_element(a.begin(), a.begin() + (k - 1), a.end(), greater<int>());
    return a[k - 1];
}

// The follow-up "the data is a stream": way 2 as a class.
struct TopK {
    size_t k;
    priority_queue<int, vector<int>, greater<int>> heap;
    explicit TopK(size_t k_) : k(k_) {}
    void add(int x) {
        heap.push(x);
        if (heap.size() > k) heap.pop();
    }
    int kth() const { return heap.top(); }   // valid once k values were added
};
```

- **Sort:** `O(n log n)`. Short, hard to get wrong, and good enough when `n` is small or you need the whole order anyway.
- **Min-heap of size `k`:** keep the `k` largest values seen so far; the top is the answer. `O(n log k)` time and `O(k)` memory. It is the only one of the three that **works when the data does not fit in memory or arrives as a stream** (the `TopK` class).
- **Selection** (`nth_element`, quickselect): `O(n)` on average. It rearranges a copy of the data and needs all of it in memory.

```
T4 n = 1000000 random values: k = 10 gives 999987151, k = 500000 gives 499976914; sort, heap, selection and the stream class all agree: yes
```

| Job (`n = 1,000,000`) | Time (ms), run 1 / run 2 |
|---|---|
| sort | 70.8 / 71.3 |
| heap, `k = 10` | 7.9 / 7.2 |
| heap, `k = 500,000` | 64.7 / 63.2 |
| selection, `k = 10` | 6.6 / 6.6 |
| selection, `k = 500,000` | 8.3 / 8.2 |

Read the table by `k`. For `k = 10` the heap (about 7 to 8 ms) is about 9 times faster than sorting (about 71 ms), because it does `log 10` work per element. For `k = n/2` the heap is as slow as sorting (about 63 to 65 ms), since `log k` is now as large as `log n`. Selection was fastest in both cases, about 7 to 8 ms. So the answer to "which is best?" is a question back: **how big is `k`, does the data stream, do I need the other elements too?** That is how you turn a problem with a few valid answers into a short conversation that shows judgement.

## 5. The follow-up catalogue

Interviewers rarely stop at the first solution. The follow-up changes a condition, and each change has a usual response.

| Follow-up | What it changes | What you do |
|---|---|---|
| "Now the data is a stream" | You cannot store or re-read it | One pass with a bounded structure: a heap of size `k`, a running count, a sketch or reservoir sampling (14.02) |
| "It does not fit in memory" | Memory, not time | External sort or merge, a heap over `k` sources, hashing into buckets that fit, a probabilistic filter |
| "Values can be negative" | Monotonicity breaks | Window methods fail (section 3.1); prefix sums with a hash map, or Kadane for sums |
| "There are duplicates" | Equal keys | Decide whether equal values count once or many times; use counts in a map, or 3-way partitioning |
| "The array changes between queries" | Static tricks fail | Fenwick or segment tree instead of prefix sums; a balanced tree instead of a sorted array |
| "Many queries on the same data" | Pay once, answer many times | Preprocess: sort, prefix sums, sparse table, offline sorting (14.03), a persistent structure for old versions (14.04) |
| "Do it in `O(1)` extra space" | Memory | Two pointers, in-place swaps, reuse the input array, bit tricks; say what you give up (the input is modified) |
| "Return the actual solution, not the count" | You need the choices | Store parent pointers or reconstruct from the DP table; in DP, work backwards |
| "Make it faster for a huge `n`" | A better growth rate | Look for monotonicity (binary search), a sorted order (sweep), a data structure that answers the inner loop in `log n` |
| "What if the input is adversarial?" | Hash collisions, worst-case pivots | Randomise (14.02), use a tree, set a limit; say what happens at the worst case |
| "Make it thread-safe" | Shared state | Where the lock goes, a read-write lock, partitioning by key (10 and 13) |
| "Prove it is correct" | An argument | An invariant for loops, an exchange argument for greedy, induction for recursion |

Always answer in the same order: **what changes, what stops working, what replaces it, what it costs.**

## 6. How to run a timed set

The six steps from the DSA plan, with the time each takes in a 45-minute problem (a set of four problems is 180 minutes with a break):

| Step | What you do | Minutes |
|---|---|---|
| 1. Understand | Restate, ask about sizes, ranges, duplicates, empty input; write two examples, one an edge case | 3 to 5 |
| 2. Match | Name the pattern from section 2, check it against section 1, say the brute force and its cost | 2 to 3 |
| 3. Improve | Find the repeated work or the property that cuts it (sorted, monotone, bounded values) | 5 to 8 |
| 4. Implement | Code with clear names; narrate decisions, not syntax | 15 to 20 |
| 5. Test | Run the two examples and the edge case by hand; look for off-by-one and empty input | 5 |
| 6. State costs | Time and memory, and one follow-up you can answer | 2 |

**Stop at the limit** (E 15 minutes, M 30, H 45), read the solution, and solve it again cold the next day. Log it in the [practice log](../practice_log.md): the "pattern" column is the whole point of this tutorial, because after a few weeks the log shows which patterns you miss.

## 7. Complexities of the examples

| Problem | Method | Time | Extra memory | Needs |
|---|---|---|---|---|
| Subarrays with sum `k` | brute force | `O(n^2)` | `O(1)` | nothing |
| | prefix sums and hash | `O(n)` | `O(n)` | nothing |
| | two pointers | `O(n)` | `O(1)` | values `>= 0` |
| k-th largest | sort | `O(n log n)` | `O(n)` (a copy) | nothing |
| | heap of size `k` | `O(n log k)` | `O(k)` | nothing; works on a stream |
| | selection | `O(n)` average | `O(n)` (a copy) | all data in memory |

## 8. Common mistakes

1. **Choosing by the title.** "Subarray" does not mean "sliding window". Check the values and the condition first.
2. Writing the optimal method before you can state the brute force and its cost.
3. Ignoring the constraints, then using a method that cannot finish; or over-engineering when `n` is 20.
4. Testing only on non-negative, sorted, distinct, non-empty inputs. The two-pointer count passed all of those and failed 1,791 of 2,000 mixed ones.
5. A prefix-sum map that is missing the empty prefix, or looks up after it inserts.
6. Taking the first row of a table as the answer without checking the second condition (the values, the memory, the number of queries).
7. Not saying the **cost** of a choice (the hash map's memory, the heap's `log k`, selection modifying a copy).
8. Treating "greedy" as a guess; without an exchange argument, assume it is wrong and test it against brute force on small inputs.
9. Spending the whole set on the first problem. When the time is up, read the solution and move on.
10. Not logging the pattern after a problem, so the weak spots stay invisible.

## 9. Questions to answer out loud

1. A statement gives `n = 10^5`. Which growth rates are out, and what does that suggest?
2. What does "about 10^8 simple steps per second" assume, and when is it too optimistic?
3. A problem asks for the "shortest" something. What do you ask to choose between BFS, Dijkstra and DP?
4. Why does the two-pointer count of subarrays with sum `k` need non-negative values? Give the smallest counterexample.
5. Why must the empty prefix be in the hash map, and why does the lookup come before the insert?
6. For the k-th largest, when is the heap better than sorting, and when is it not?
7. What changes in the solution when the data arrives as a stream?
8. "Do it in `O(1)` extra space." Name two ways, and what each costs you.
9. How do you decide between greedy and dynamic programming?
10. What do you do when no row of the table fits?
11. How do you spend the 45 minutes of an interview problem?
12. What does your practice log tell you after four weeks, and what do you change?

## 10. Problems

### 10.1 On paper

**A.** Count the subarrays of `[1, 0, 2, 0, 1, 1]` with sum 2 by the brute force, by prefix sums with a hash map (write the map after each element) and by two pointers (`at most 2` minus `at most 1`).

**B.** Find the smallest array (fewest elements, values in `{-1, 0, 1}`) and a target `k = 1` for which the two-pointer count gives a wrong answer. Trace both methods.

**C.** For `[7, 3, 9, 3, 8, 1]` give the k-th largest for every `k` from 1 to 6, and draw the contents of a min-heap of size 3 after each value is added.

**D.** Use the budget table: for `n` = 8, 20, 35, 300, 5,000, 200,000 and 10^7, what growth rate can you afford, and name one technique for each.

**E.** Name the pattern (section 2) for each statement, and say what condition you would check:

1. Given a sorted array, find two numbers that add up to a target.
2. Longest substring with at most `k` distinct characters.
3. The number of ways to climb `n` stairs taking 1 or 2 steps.
4. The fewest steps from the top-left to the bottom-right of a grid with walls.
5. For each element, the next element to its right that is greater.
6. All subsets of 20 numbers that add up to a target.
7. Merge all overlapping intervals.
8. The smallest number of meeting rooms for a list of meetings.
9. The smallest capacity that ships all packages in `D` days.
10. Does a linked list have a cycle?
11. A valid order to take courses with prerequisites.
12. Group a list of words into anagram classes.
13. The longest common subsequence of two strings of length 1,000.
14. Range sums with point updates, `n = 10^5`.
15. The median of a stream of numbers.
16. The number of pairs `i < j` with `a[i] > a[j]`, `n = 10^5`.
17. Shortest paths on a graph with non-negative weights.
18. Are two vertices connected, while edges keep being added?
19. All occurrences of a pattern in a text of 10^6 characters.
20. The maximum sum of a contiguous subarray.

**F.** For problem 16 of E, write the brute force and its cost, name two better methods, and say which one you would code in 30 minutes.

### 10.2 Mixed timed sets (on LeetCode)

Numbers and titles are from memory; check a number before you rely on it. Each set takes 4 problems from different areas. Set a timer for the whole set, treat each problem by the six steps, and log the pattern you named before coding.

| Set | Problem | Number | Area | Limit |
|---|---|---|---|---|
| 1 (warm-up, 90 min) | Two Sum | 1 | 02 | 15 |
| | Longest Substring Without Repeating Characters | 3 | 01 | 30 |
| | Merge Intervals | 56 | 09 | 30 |
| | Number of Islands | 200 | 08 | 30 |
| 2 (120 min) | Subarray Sum Equals K | 560 | 02 | 30 |
| | Top K Frequent Elements | 347 | 07 | 30 |
| | Course Schedule | 207 | 08 | 30 |
| | Coin Change | 322 | 10 | 30 |
| 3 (120 min) | Daily Temperatures | 739 | 05 | 30 |
| | Kth Largest Element in an Array | 215 | 07 | 30 |
| | Word Search | 79 | 06 | 30 |
| | Lowest Common Ancestor of a Binary Search Tree | 235 | 07 | 15 |
| 4 (hard, 180 min) | Trapping Rain Water | 42 | 01 | 45 |
| | Median of Two Sorted Arrays | 4 | 03 | 45 |
| | Word Ladder | 127 | 08 | 45 |
| | Minimum Window Substring | 76 | 01 | 45 |

Rules: no hints inside the limit; at the limit, read the solution; solve the missed ones cold after 3, 7 and 21 days, as the practice log says. After the sets, repeat with **company-tagged** lists for your target companies (the plan's section 6).

### 10.3 In your editor

**A.** Add to the subarray-sum code a function that returns the **longest** subarray with sum `k` (the hash map stores the first index of each prefix), and test it against the brute force on random arrays with negative values.

**B.** Write the k-th largest of a stream with deletions of an arbitrary element (two heaps with lazy deletion, or an ordered multiset), and compare it with re-sorting on random operations.

**C.** Build a table of your own for the next three months: for each of the 20 statements of problem E, one LeetCode problem you have solved, the technique you chose and the condition you checked. Fill the gaps by solving one problem for each empty cell.

**D.** Time-box experiment: take any one hard problem from set 4, write only the brute force and the complexity in 10 minutes, then spend 20 minutes finding the repeated work. Note whether naming the brute force first helped.

### Answers

The program that checks the paper problems printed:

```
A a = [1,0,2,0,1,1], k = 2: brute 6, hash 6, window 6
B smallest counterexample for the window, k = 1: [1,-1]: true count 1, window gives 2
C a = [7,3,9,3,8,1], k-th largest for k = 1 to 6: 9 8 7 3 3 1
C heap of size 3 after each value (sorted): [7] [3,7] [3,7,9] [3,7,9] [7,8,9] [7,8,9]
D budget: 8 -> n! (all permutations); 20 -> 2^n (all subsets); 35 -> 2^(n/2) (meet in the middle); 300 -> n^3; 5000 -> n^2; 200000 -> n log n; 10000000 -> n or log n;
```

**A.** All three methods give 6. Brute force finds `[2]`, `[0,2]`, `[2,0]`, `[0,2,0]`, `[0,1,1]` and `[1,1]`. With the hash map, the prefix sums after each element are 1, 1, 3, 3, 4, 5 (and 0 for the empty prefix). For each end you look up `P - 2` among the earlier prefixes: nothing for the first two elements (`-1` is absent), 2 for the third (`P = 3`, `1` seen twice), 2 for the fourth, 0 for the fifth (`2` is absent) and 2 for the sixth (`3` seen twice): 0 + 0 + 2 + 2 + 0 + 2 = 6. For two pointers, `at most 2` counts 13 and `at most 1` counts 7, and the difference is 6.

**B.** The smallest is `[1, -1]` with `k = 1`: the true count is 1 (only `[1]`), and the two-pointer method gives 2. `at most 1` is right (3: `[1]`, `[-1]` and `[1,-1]`), but in the `at most 0` pass the sum 1 at the first element is above the limit, so the window throws the 1 away; at the second element the sum is -1 and only `[-1]` is counted, so the pass gives 1 where the true count is 2 (`[-1]` and `[1,-1]`). The difference is 3 - 1 = 2 instead of 1. The window assumes that a smaller window means a smaller sum.

**C.** The k-th largest for `k = 1` to 6 is 9, 8, 7, 3, 3, 1. The heap of size 3, after each value: `[7]`, `[3,7]`, `[3,7,9]`, `[3,7,9]` (the second 3 enters and the smallest, a 3, leaves), `[7,8,9]`, `[7,8,9]` (the 1 enters and leaves). The top, 7, is the 3rd largest.

**D.** 8: `n!` (permutations, backtracking). 20: `2^n` (bitmask over subsets). 35: `2^(n/2)`, meet in the middle. 300: `n^3` (Floyd-Warshall, interval DP). 5,000: `n^2` (DP over pairs). 200,000: `n log n` (sort, heap, segment tree). 10^7: `n` or `log n` (a single pass, a formula, binary search).

**E.** 1 two pointers (or binary search); sorted. 2 sliding window; the count of distinct characters is monotone as the window grows. 3 DP (1D, Fibonacci). 4 BFS; unweighted edges. 5 monotonic stack. 6 backtracking or bitmask; `2^20` is about a million subsets. 7 sort by start and sweep. 8 sort the starts and ends and sweep, or a heap of end times. 9 binary search on the answer; feasibility is monotone in the capacity. 10 fast and slow pointers. 11 topological sort; check for a cycle. 12 hashing with a canonical key (the sorted letters or a count vector). 13 2D DP; `1000 * 1000` fits. 14 Fenwick tree or segment tree. 15 two heaps. 16 merge sort counting, or a Fenwick tree over compressed values. 17 Dijkstra; no negative edges. 18 union-find. 19 KMP, the Z-function or a rolling hash. 20 Kadane's algorithm, or prefix sums with the minimum so far.

**F.** Brute force: two loops, `O(n^2)`, which at `n = 10^5` is about 5 * 10^9 steps, too slow. Two better methods: modify merge sort to count while merging, or a Fenwick tree over compressed values counting how many earlier values are larger. Both are `O(n log n)`. I would code the Fenwick version, since it is short and uses a structure you already know, but merge sort is the better choice if you must not use extra structures.

## Cheat sheet

- **Constraints first:** `n` up to 10 `n!`; 25 `2^n`; 40 meet in the middle; 500 `n^3`; 10^4 `n^2`; 10^6 `n log n`; above that `n` or `log n`. All of these ran in 5 to 160 ms with a light step.
- **Words to technique:** sorted pair, two pointers; contiguous range, window or prefix sums; seen before, hash; minimum feasible value, binary search; next greater, monotonic stack; shortest unweighted, BFS; weighted, Dijkstra; ordering, topological sort; connected, union-find; top `k`, heap; range with updates, Fenwick or segment tree; best total with overlapping choices, DP; list everything, backtracking.
- **Each technique has a condition.** Two pointers for subarray sums needs values `>= 0` (wrong on 1,791 of 2,000 mixed arrays); the hash map does not. Say the condition.
- **A better growth rate first.** Brute force 213 ms against 2 ms (hash) and 0.4 ms (two pointers) at `n = 30,000`.
- **k-th largest:** the heap wins for small `k` (7 to 8 ms against 71 ms for sorting) and stops winning at `k = n/2`; selection wins in both (7 to 8 ms); only the heap works on a stream.
- **Follow-ups:** say what changes, what stops working, what replaces it, what it costs.
- **In the room:** understand, match, brute force, improve, implement, test, state the costs. Stop at the limit, read, and solve cold the next day.

---

## Sources

- All code is tested here (1,180,071 checks under ASan and UBSan; 16 deliberate bugs all caught). The traces and timings are measured on one Xeon container, two runs each, with identical traces. The data is random and seeded. The timed jobs in section 1 have light inner steps and show the best case.
- The table of constraints, the table from clues to techniques, the follow-up catalogue and the problem sets are my own summary of common practice, chosen to fit the earlier tutorials. They are judgement, not measurement, apart from the timings. The LeetCode numbers and titles are from memory and were not checked.
- The explanations, traces and code are my own.
