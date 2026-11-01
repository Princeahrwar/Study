# DSA 06.01: Recursion Fundamentals

*Part of the [Recursion and backtracking plan](00_plan.md) · Time: about 6 hours (3 reading, 3 problems) · You need: [Stack and queue fundamentals](../05_stacks_and_queues/01_stack_and_queue_fundamentals.md) (the stack contract) and [Expression evaluation and parsing](../05_stacks_and_queues/04_expression_evaluation_and_parsing.md) (section 6.3 measured a recursion depth limit) · Related: [Linked lists](../04_linked_lists/00_plan.md) · No companion file: every listing is in this tutorial.*

By the end of this tutorial you can:

- write a recursive function from three questions (smallest case, how to shrink, how to combine), and check it by induction;
- draw the call stack and the recursion tree of a call, and read off the stack space (depth) and the work (number of calls);
- say how deep a recursion can go on a normal machine, and why the answer changes between a debug and a release build;
- turn a recursion into a loop, with an accumulator or with an explicit stack, and bound the depth of a recursion that has a bad worst case;
- avoid the classic base-case and progress bugs.

**Where the numbers come from.** Every listing was compiled with `-Wall -Wextra` and tested: 494,810 checks passed under AddressSanitizer with UBSan (GCC 13.3), against references such as closed formulas, `std::sort`, `std::gcd`, a legality check of every Hanoi move and a comparison of the recursive and the explicit-stack versions. Call counts and depths are exact and the same on every machine. Stack-overflow limits and the one timing were measured on one machine: an x86-64 VM, Ubuntu 24.04, GCC 13.3, `ulimit -s` printed 8192 (an 8 MiB stack). Your limits will differ; the shapes should not. The statements of the LeetCode problems are summarised from memory; titles were checked against pages that list them (see Sources), and no difficulty labels are given because none could be verified.

All listings need these headers: `<algorithm>`, `<string>`, `<utility>` and `<vector>`.

---

## 1. The idea

### 1.1 A function that uses itself

A recursive function solves a problem by solving smaller versions of the same problem. Three questions write it:

1. **Base case.** What is the smallest input, and what is its answer without any further call?
2. **Shrink.** How do I turn a bigger input into one or more smaller ones that are closer to the base case?
3. **Combine.** Given the answers to the smaller inputs, how do I build the answer to this one?

Then you check it the way you check an induction: if the calls on smaller inputs return the right answer (the *leap of faith*), does this call return the right answer? Is every chain of calls guaranteed to reach a base case? If both are yes, the function is correct. You never need to trace the whole execution in your head.

`sumTo(n)` is the smallest example: the sum of `0..n` is `n` plus the sum of `0..n-1`, and the sum of `0..0` is 0.

### 1.2 Four shapes

| Shape | Example | Calls per level | Depth | Number of calls |
|---|---|---|---|---|
| Linear | `sumTo`, reverse a list | 1 | n | n |
| Halving | `powMod`, `gcd`, binary search | 1 | about log n | about log n |
| Branching | `fib`, Hanoi, subsets (06.02) | 2 or more | n | exponential |
| On a structure | flood fill, tree walks | one per neighbour or child | the longest path | the size of the structure |

The last two columns are the whole story of cost: the **depth** is the stack space, the **number of calls** is the time. Section 3 shows how to count both.

---

## 2. The call stack

### 2.1 What a call does

A call pushes a **frame** on the stack: the return address, the arguments, the local variables and any registers the function must save. A return pops it. A recursive function simply has several frames of the same function alive at once, each with its own copy of the arguments. For `sumTo(3)`:

| Event | Stack after the event (oldest first) |
|---|---|
| call sumTo(3) | sumTo(3) |
| call sumTo(2) | sumTo(3) sumTo(2) |
| call sumTo(1) | sumTo(3) sumTo(2) sumTo(1) |
| call sumTo(0) | sumTo(3) sumTo(2) sumTo(1) sumTo(0) |
| return 0 | sumTo(3) sumTo(2) sumTo(1) |
| return 1 | sumTo(3) sumTo(2) |
| return 3 | sumTo(3) |
| return 6 | empty |

Nothing is added until the base case returns; then the additions happen on the way back, in reverse order. That is why `n + sumTo(n - 1)` is not a loop: the `n +` is a pending job stored in the frame.

```cpp
// Linear recursion: one call, one level deeper.
long long sumTo(long long n) { return n == 0 ? 0 : n + sumTo(n - 1); }

// The same with the pending addition carried down: the recursive call is the last thing done.
long long sumToTail(long long n, long long acc = 0) { return n == 0 ? acc : sumToTail(n - 1, acc + n); }

// a^e mod m for e >= 0 and m < 2^31: halve the exponent, so the depth is about log2(e).
long long powMod(long long a, long long e, long long m) {
    if (e == 0) return 1 % m;
    long long half = powMod(a, e / 2, m);
    long long r = half * half % m;
    return e % 2 ? r * (a % m) % m : r;
}

long long gcd(long long a, long long b) { return b == 0 ? a : gcd(b, a % b); }
```

### 2.2 How deep can it go?

The stack is a fixed-size region, 8 MiB for the main thread on this machine (`ulimit -s` printed 8192; it is a per-process limit you can change). When the recursion needs more, the program is killed by a segmentation fault: it is **not** an exception you can catch. Measured on this machine with the listings above and `-O0` (no optimisation), `sumTo` worked at 250,000 levels and crashed at 270,000, which is about 32 bytes per level (8 MiB divided by about 260,000). A function with more locals costs more per level: a recursive flood fill (section 5.2) crashed on a 350 by 350 grid of land.

| Program, build | Worked at | Crashed at |
|---|---|---|
| `sumTo`, `-O0` | 250,000 levels | 270,000 |
| `sumToTail`, `-O0` | 250,000 | 270,000 |
| `sumTo`, `-O2` | 100,000,000 | never crashed |
| `sumToTail`, `-O2` | 100,000,000 | never crashed |
| `reverseRec` on a list, `-O0` | 150,000 nodes | 200,000 |
| `reverseRec` on a list, `-O2` | 800,000 nodes | 950,000 |
| `areaRec` on an n by n grid, `-O2` | n = 300 (90,000 cells) | n = 350 |

Two lessons. First, **the limit depends on the frame size**, so it depends on the compiler and its flags: a debug build overflows where a release build does not. Second, at `-O2` GCC turned `sumTo` and `sumToTail` into loops (see section 6.1), so the recursion simply disappeared. Never rely on that: C++ does not promise it.

Other stacks are much smaller. The Linux kernel gives each thread a 16 KiB stack on x86-64 (raised from 8 KiB in 2014 because deep call paths no longer fitted), so recursion in kernel code is avoided or bounded. Safety-critical C coding standards forbid it: MISRA C:2012 rule 17.2 says functions shall not call themselves, directly or indirectly. In interviews, "what is the maximum depth?" is a fair follow-up to any recursive solution.

---

## 3. Recursion trees

### 3.1 Draw the calls

Each call is a node; its children are the calls it makes. The tree for `fibNaive(4)`:

```
fib(4)
  fib(3)
    fib(2)
      fib(1)
      fib(0)
    fib(1)
  fib(2)
    fib(1)
    fib(0)
```

```cpp
// Two calls per level: the recursion tree branches. `calls` counts every call made.
long long fibNaive(int n, long long& calls) {
    ++calls;
    return n < 2 ? n : fibNaive(n - 1, calls) + fibNaive(n - 2, calls);
}

// The same function with a table: each n is computed once (memo[n] < 0 means "not yet").
long long fibMemo(int n, std::vector<long long>& memo, long long& calls) {
    ++calls;
    if (n < 2) return n;
    if (memo[n] >= 0) return memo[n];
    return memo[n] = fibMemo(n - 1, memo, calls) + fibMemo(n - 2, memo, calls);
}

// No recursion at all: only the last two values are ever needed.
long long fibIter(int n) {
    long long a = 0, b = 1;
    for (int i = 0; i < n; ++i) { long long t = a + b; a = b; b = t; }
    return a;
}
```

The tree has 9 nodes, so 9 calls. In general `fibNaive(n)` makes `2 * F(n + 1) - 1` calls, where `F` is the Fibonacci number (checked for n up to 30). `F(n)` grows by a factor of about 1.618 per step, so the call count is exponential even though the answer is small:

| n | F(n) | Calls of `fibNaive` | Calls of `fibMemo` |
|---|---|---|---|
| 10 | 55 | 177 | 19 |
| 20 | 6,765 | 21,891 | 39 |
| 30 | 832,040 | 2,692,537 | 59 |
| 40 | 102,334,155 | 331,160,281 | 79 |

The same value is computed over and over: in the tree of `fib(5)`, `fib(2)` is called 3 times and `fib(3)` twice (Problem A). On this machine `fibNaive(40)` took about 220 ms (median of 3 runs, `-O2`). `fibMemo` stores each answer in a table the first time, so `fib(n)` is computed once per `n`: it makes exactly `2n - 1` calls (19 for n = 10, 79 for n = 40). `fibIter` needs no recursion and no table, because only the last two values matter.

### 3.2 Depth versus work

The two costs read off the tree differently. **Depth** is the longest path from the root to a leaf, and it is the stack space. The **number of nodes** is the work.

| Function | Depth | Calls |
|---|---|---|
| `sumTo(n)` | n | n + 1 |
| `powMod(a, e, m)` | `floor(log2 e) + 2` | the same |
| `fibNaive(n)` | n | `2 * F(n + 1) - 1` |
| `fibMemo(n)` | n | `2n - 1` |
| `hanoi(n)` | n | `2^(n+1) - 1`, of which `2^n - 1` move a disc |

`hanoi(n)` shows how big a small tree can be: 10 discs need 1,023 moves, 20 discs need 1,048,575, and no cleverness avoids it, because the answer itself has that many moves.

```cpp
using Move = std::pair<char, char>;

// Move n discs from `from` to `to`: park n-1 on `via`, move the big one, bring the n-1 back.
void hanoi(int n, char from, char via, char to, std::vector<Move>& moves) {
    if (n == 0) return;
    hanoi(n - 1, from, to, via, moves);
    moves.push_back({from, to});
    hanoi(n - 1, via, from, to, moves);
}

// The same without recursion: a stack of pending work. A frame is either a call to expand
// (stage 0) or a move to record (stage 1). Push the work in reverse order of execution.
std::vector<Move> hanoiIter(int n) {
    struct Frame { int n; char from, via, to; int stage; };
    std::vector<Move> moves;
    std::vector<Frame> st{{n, 'A', 'B', 'C', 0}};
    while (!st.empty()) {
        Frame f = st.back();
        st.pop_back();
        if (f.n == 0) continue;
        if (f.stage == 1) { moves.push_back({f.from, f.to}); continue; }
        st.push_back({f.n - 1, f.via, f.from, f.to, 0});     // runs last
        st.push_back({f.n, f.from, f.via, f.to, 1});         // runs second
        st.push_back({f.n - 1, f.from, f.to, f.via, 0});     // runs first
    }
    return moves;
}
```

---

## 4. Base cases and progress

Every recursion bug is one of a few kinds.

| Bug | Symptom | Example |
|---|---|---|
| Missing base case | Stack overflow | `sumTo` without the `n == 0` test |
| Base case never reached | Stack overflow | shrinking by 2 when the only base case is 0 and `n` is odd |
| Base case too small or too large | Wrong values | `fibNaive` with `n < 1` as the base: `fib(1)` calls `fib(-1)` (Problem E) |
| No real progress | Stack overflow | `gcd(a, b)` calling `gcd(a, b)` |
| Copying the input on every call | Memory and time O(n^2) | passing a `std::vector` by value down a linear recursion |
| Work repeated by overlapping calls | Exponential time | `fibNaive` (section 3.1) |
| Output and recursion mixed up | Missing or doubled results | recording a move after the second call in `hanoi` |

A habit that prevents most of them: write the base case first, then write a call that visibly gets closer to it, and only then the combining step.

---

## 5. Recursion on structures

### 5.1 A linked list

To reverse a list: reverse everything after the head (leap of faith), then hang the head on the end of the reversed part.

```cpp
struct ListNode {
    int val;
    ListNode* next;
};

// Trust the call to reverse the rest, then hang the head on the end of it.
ListNode* reverseRec(ListNode* head) {
    if (!head || !head->next) return head;
    ListNode* newHead = reverseRec(head->next);
    head->next->next = head;
    head->next = nullptr;
    return newHead;
}

ListNode* reverseIter(ListNode* head) {
    ListNode* prev = nullptr;
    while (head) {
        ListNode* next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}
```

For the list `1 2 3 4` the frames, from the deepest, do this when their call returns:

| Frame (node) | What it does when its call returns |
|---|---|
| 4 | last node: return it as the new head |
| 3 | make 4 point back to 3; cut 3 -> 4 |
| 2 | make 3 point back to 2; cut 2 -> 3 |
| 1 | make 2 point back to 1; cut 1 -> 2 |

The depth is the length of the list, so a million-node list overflows the stack at `-O0` (table in section 2.2). `reverseIter` does the same with three pointers and no stack.

### 5.2 A grid

Flood fill (the heart of "number of islands" and "max area of island") visits a cell, erases it, and visits its four neighbours. The recursion depth is the length of the longest chain of cells, which on an all-land grid is the number of cells in the worst case, not the side of the grid.

```cpp
// Size of the island of '1' cells containing (r, c); the cells are erased as they are counted.
int areaRec(std::vector<std::string>& g, int r, int c) {
    if (r < 0 || c < 0 || r >= (int)g.size() || c >= (int)g[r].size() || g[r][c] != '1') return 0;
    g[r][c] = '0';
    return 1 + areaRec(g, r + 1, c) + areaRec(g, r - 1, c) + areaRec(g, r, c + 1) + areaRec(g, r, c - 1);
}

// The same with an explicit stack of cells still to visit. Mark a cell when it is pushed,
// not when it is popped, or a cell can be pushed many times.
int areaIter(std::vector<std::string>& g, int r0, int c0) {
    if (g[r0][c0] != '1') return 0;
    std::vector<std::pair<int, int>> st{{r0, c0}};
    g[r0][c0] = '0';
    int area = 0;
    while (!st.empty()) {
        auto [r, c] = st.back();
        st.pop_back();
        ++area;
        const int dr[4] = {1, -1, 0, 0}, dc[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr < 0 || nc < 0 || nr >= (int)g.size() || nc >= (int)g[nr].size() || g[nr][nc] != '1') continue;
            g[nr][nc] = '0';
            st.push_back({nr, nc});
        }
    }
    return area;
}
```

`areaRec` is correct and short, and it crashed on a 350 by 350 grid of land on this machine. `areaIter` handled 3,000 by 3,000 (9 million cells). The one trap in the iterative version is to mark a cell when it is **pushed**: if you mark it when it is popped, the same cell can be pushed by several neighbours and counted several times (Problem E).

---

## 6. From recursion to iteration

### 6.1 Tail calls

A call is a **tail call** if it is the last thing the function does: nothing is left to do after it returns. `sumToTail` carries the pending addition in a parameter, `acc`, so the recursive call is a tail call, and `gcd` is one too. A tail call does not need a new frame, so a compiler may reuse the current one: the recursion becomes a loop. The standard does not require this in C++. Measured: at `-O0` `sumToTail` crashed at 270,000 levels like `sumTo`; at `-O2` it ran 100,000,000 levels. The same happened for `sumTo`, which is not a tail call as written: the measurement shows no deep recursion, so GCC turned it into a loop too. Treat all of this as an optimisation, not a guarantee.

The conversion is mechanical when you want it yourself: the parameters become variables, the call becomes an assignment of the new values, and a jump to the top becomes a loop.

```cpp
long long sumToLoop(long long n) {
    long long acc = 0;
    while (n != 0) { acc += n; --n; }
    return acc;
}
```

### 6.2 An explicit stack

When a function does work **after** a call returns, or makes several calls (Hanoi, depth-first walks), the frames hold real state, and the iterative version keeps that state in your own stack, on the heap. Two ways to do it:

- **Work frames.** A frame is either a call to expand or an action to perform. Expanding a call pushes its pieces **in reverse order of execution**, because the stack pops the last pushed first. `hanoiIter` above does this: expand, then move, then expand again are pushed as the second call, the move, the first call.
- **A to-visit stack.** For a walk that does nothing after the children (flood fill), push the neighbours; mark them when pushed, as above.

The explicit stack lives on the heap, so its size is limited by memory, not by 8 MiB. It is also easier to inspect, to pause, and to stop early. The price is that the code no longer reads like the definition.

### 6.3 Bounding the depth

Sometimes the recursion is right and only its worst case is bad. Quicksort with the last element as the pivot recurses on both sides, and on a sorted array one side is always empty, so the depth is n. The fix keeps the time but bounds the stack: recurse on the **smaller** side and loop on the larger. The smaller side has at most half the elements, so the depth is at most log2(n) + 1 whatever the input.

```cpp
// Lomuto partition around the last element; returns the pivot's final index.
static int partitionLast(std::vector<int>& a, int lo, int hi) {
    int pivot = a[hi], i = lo;
    for (int j = lo; j < hi; ++j)
        if (a[j] < pivot) std::swap(a[i++], a[j]);
    std::swap(a[i], a[hi]);
    return i;
}

// Recurse on both sides. `depth` is the call depth, `deepest` records the maximum.
void quickNaive(std::vector<int>& a, int lo, int hi, int depth, int& deepest) {
    if (lo >= hi) return;
    deepest = std::max(deepest, depth);
    int p = partitionLast(a, lo, hi);
    quickNaive(a, lo, p - 1, depth + 1, deepest);
    quickNaive(a, p + 1, hi, depth + 1, deepest);
}

// Recurse on the smaller side and loop on the larger: each call at least halves the work,
// so the depth is at most log2(n) + 1 whatever the input.
void quickBounded(std::vector<int>& a, int lo, int hi, int depth, int& deepest) {
    while (lo < hi) {
        deepest = std::max(deepest, depth);
        int p = partitionLast(a, lo, hi);
        if (p - lo < hi - p) { quickBounded(a, lo, p - 1, depth + 1, deepest); lo = p + 1; }
        else { quickBounded(a, p + 1, hi, depth + 1, deepest); hi = p - 1; }
    }
}
```

| Input | n | `quickNaive` deepest call | `quickBounded` deepest call |
|---|---|---|---|
| sorted | 1,000 | 998 | 0 |
| reverse | 1,000 | 998 | 0 |
| random | 1,000 | 22 | 5 |
| equal | 1,000 | 998 | 0 |
| sorted | 10,000 | 9,998 | 0 |
| reverse | 10,000 | 9,998 | 0 |
| random | 10,000 | 31 | 8 |
| equal | 10,000 | 9,998 | 0 |

On sorted input with 1,000 elements `quickNaive` reached depth 998 and `quickBounded` stayed at 0, because the empty side is the one recursed on and the loop takes the long side. On random input the depths were 22 against 5. The time of both is the same, O(n^2) on sorted input with this pivot rule; only the stack is fixed. Choosing a better pivot is the subject of the sorting tutorials.

### 6.4 Choosing

| Situation | Choose |
|---|---|
| The definition is recursive and the depth is small and known (log n, tree height of a balanced tree) | Recursion |
| The depth can be as large as the input (list, grid, skewed tree) | Explicit stack or a loop |
| A tail call | A loop, by hand |
| Overlapping subproblems | Recursion with a table (memoisation), then DP |
| A recursion with a bad worst case depth | Recurse on the smaller part, loop on the rest |
| Embedded or kernel code | No recursion, or a proved depth bound |

---

## 7. Common mistakes

- No base case, or a base case the calls never reach (section 4).
- Forgetting that `n + sumTo(n - 1)` leaves work for after the return, so the stack grows.
- Trusting a release build's behaviour on a debug build, or the other way round (section 2.2).
- Passing containers by value in a recursion; pass by reference and undo your changes, as 06.02 does.
- Marking a cell visited when you pop it instead of when you push it.
- Pushing work frames in execution order instead of reverse order.
- Using `int` for a count that grows exponentially: `fib(46)` is the last Fibonacci number that fits in a signed 32-bit `int`.
- Recursing on `n - 1` and `n - 2` without a second base case.

---

## 8. Questions to answer out loud

1. What three questions write a recursive function? (1.1)
2. What does a frame hold, and when is it removed? (2.1)
3. What happens when the recursion is too deep? Can you catch it? (2.2)
4. Why does the same program overflow at `-O0` and not at `-O2`? (6.1)
5. How many calls does `fibNaive(n)` make, and why so many? (3.1)
6. What is the difference between depth and the number of calls? (3.2)
7. What does memoisation change in the recursion tree? (3.1)
8. What is a tail call, and does C++ guarantee that it is optimised? (6.1)
9. Why are work frames pushed in reverse order? (6.2)
10. How do you bound the stack of quicksort? (6.3)
11. Why is the depth of a flood fill not the side of the grid? (5.2)
12. Why do kernels and safety standards avoid recursion? (2.2)

---

## 9. Problems

I could not open LeetCode from here, so the statements below are summarised from memory: read the real statement before you code.

### 9.1 On paper

**Problem A. Count the calls.**
*Where:* paper.
*Goal:* see the repeated work.
*How:* draw the recursion tree of `fibNaive(5)`. How many calls are there? How many times is each of `fib(0)` to `fib(5)` called?

**Problem B. Hanoi by hand.**
*Where:* paper, using `hanoi`.
*Goal:* read the moves off the recursion.
*How:* list the moves for 3 discs from `A` to `C` using `B` as the spare. Which move carries the biggest disc? How many moves for 5 discs, and which move number carries the biggest one?

**Problem C. Halving.**
*Where:* paper, using `powMod`.
*Goal:* trace a logarithmic recursion.
*How:* compute `powMod(3, 13, 1000)`. For each call write `e`, the result it returns and what it did. How many frames are alive at the deepest point?

**Problem D. The list.**
*Where:* paper, using `reverseRec`.
*Goal:* see what each pending frame does.
*How:* for the list `1 2 3 4` write, for each frame from the deepest outwards, the two pointer changes it makes after its call returns. How many frames are alive at the most?

**Problem E. Three small bugs.**
*Where:* paper, then in your editor if you want to confirm.
*Goal:* know what each rule protects.
*How:* (a) In `fibNaive` use `n < 1` as the base test. What do `fib(1)`, `fib(2)` and `fib(5)` return? (b) In `hanoiIter` push the three frames in execution order instead of reverse order. What does `hanoiIter(2)` return? (c) In `areaIter` mark each cell when it is popped instead of when it is pushed. What does it return on the 2 by 2 grid `{"11", "11"}` from cell (0, 0)?

### 9.2 In your editor

**Problem 1. LeetCode 509 and 70.**
*Where:* LeetCode.
*Goal:* Fibonacci three ways, and the same idea in a puzzle.
*How:* write `fibNaive`, `fibMemo` and `fibIter` and print the call counts for n up to 40. For Climbing Stairs (the number of ways to climb `n` steps taking 1 or 2 at a time) notice that it is the same recurrence with different base cases: relate its answer to `F`.

**Problem 2. LeetCode 50, Pow(x, n).**
*Where:* LeetCode.
*Goal:* fast exponentiation with a negative exponent.
*How:* halve the exponent as in `powMod`, with `double`. Handle `n < 0` by inverting `x` and negating `n`, but think about `n = -2147483648`: negating an `int` overflows, so widen to `long long` first. Check `pow(2.0, 10)`, `pow(2.0, -2)` and `pow(1.0, INT_MIN)`.

**Problem 3. LeetCode 206, 21 and 24.**
*Where:* LeetCode.
*Goal:* recursion on a list.
*How:* write each recursively and iteratively. Build a list of a million nodes and find the length at which the recursive one crashes on your machine in a debug and in a release build. Which of the three has a tail call?

**Problem 4. LeetCode 779, K-th Symbol in Grammar.**
*Where:* LeetCode.
*Goal:* recurse on the parent, not on the whole string.
*How:* as I remember it, row 1 is `0` and each next row replaces `0` with `01` and `1` with `10`; return the `k`-th symbol of row `n`. Never build the row: symbol `k` of row `n` comes from symbol `(k + 1) / 2` of row `n - 1`, flipped when `k` is even. Then look for the closed form.

**Problem 5. LeetCode 733, 200 and 695.**
*Where:* LeetCode.
*Goal:* flood fill, recursively and with a stack.
*How:* solve each with `areaRec`-style recursion first, then with an explicit stack. Find the grid size at which the recursion crashes on your machine. Make the grids snake-shaped (one long path) and see what that does to the depth.

**Problem 6. Your own explicit stack.**
*Where:* your editor.
*Goal:* convert a recursion with work after the calls.
*How:* take `fibNaive` and write a version with a stack of frames (a frame is a pending call or a pending addition). Check that it returns the same values and makes the same number of calls as the recursion.

**More practice.**

- **LeetCode 344, Reverse String.** Reverse in place by recursing on the two ends: base case when the ends meet. The depth is half the length.
- Write `quickBounded` for descending order and for a different pivot, and test with sorted, reversed, all-equal and random arrays.

---

## Answers

**Problem A.** The tree for `fibNaive(5)` has 15 calls:

| Call | Times called |
|---|---|
| `fib(5)` | 1 |
| `fib(4)` | 1 |
| `fib(3)` | 2 |
| `fib(2)` | 3 |
| `fib(1)` | 5 |
| `fib(0)` | 3 |

`fib(2)` is called 3 times and `fib(3)` twice. The formula `2 * F(6) - 1 = 2 * 8 - 1 = 15` agrees.

**Problem B.** The moves for 3 discs are `A->C, A->B, C->B, A->C, B->A, B->C, A->C`. The 4th move carries the biggest disc. For 5 discs there are 31 moves and the biggest disc moves once, as move number 16: the first 15 moves park the 4 smaller discs on the spare peg, and the last 15 bring them over.

**Problem C.**

| e | Result | Work |
|---|---|---|
| 13 | 323 | half = 729, half*half mod m = 441, odd: times a |
| 6 | 729 | half = 27, half*half mod m = 729 |
| 3 | 27 | half = 3, half*half mod m = 9, odd: times a |
| 1 | 3 | half = 1, half*half mod m = 1, odd: times a |
| 0 | 1 | base case |

The result is 323 (3^13 is 1,594,323). Five frames are alive at the deepest point: the calls with `e` = 13, 6, 3, 1 and 0.

**Problem D.**

| Frame (node) | What it does when its call returns |
|---|---|
| 4 | last node: return it as the new head |
| 3 | make 4 point back to 3; cut 3 -> 4 |
| 2 | make 3 point back to 2; cut 2 -> 3 |
| 1 | make 2 point back to 1; cut 1 -> 2 |

Four frames are alive at the most, and the new head, 4, is passed back unchanged through all of them.

**Problem E.** (a) `fib(1)` returns `-1`, `fib(2)` returns `-1` and `fib(5)` returns `-5`: with `n < 1` as the base, `fib(1)` calls `fib(0)` and `fib(-1)` and the base case hands back `-1` for the latter. The recursion ends but the values are wrong. (b) `hanoiIter(2)` returns `B->C, A->C, A->B` instead of `A->B, A->C, B->C`: with the pushes in execution order the stack runs them in the opposite order. (c) The area is `5` instead of 4, because the cell (1, 0) is pushed twice, once by (0, 0) and once by (1, 1), before it has been popped and marked, and both copies are counted. These were run on the changed listings.

**Problem 1.** The call counts are in the table of section 3.1. Climbing Stairs has the recurrence `ways(n) = ways(n - 1) + ways(n - 2)` with `ways(1) = 1` and `ways(2) = 2`, so `ways(n) = F(n + 1)`: `ways(5)` is 8 and `ways(10)` is 89.

**Problem 2.** `pow(2.0, 10)` is 1024 and `pow(2.0, -2)` is 0.25. With `n` as a `long long` the negation of `INT_MIN` is safe; as an `int` it is undefined behaviour. `pow(1.0, INT_MIN)` is 1.

**Problem 3.** The recursive reverse of section 5.1 and the iterative one agree; the crash lengths are in the table of section 2.2 for this machine (yours differ). In the merge of two sorted lists written recursively, the recursive call is not the last operation (the node is attached after it returns), so no list function in this problem is a tail call.

**Problem 4.** Rows: `0`, `01`, `0110`, `01101001`. Symbol 5 of row 4 is `1`. Following the rule, symbol `k` of row `n` flips the parent's symbol when `k` is even, so the answer is the parity of the number of 1 bits in `k - 1`. The rule and the closed form were compared on every row up to row 12 and every `k`.

**Problem 5.** The crash size is in the table of section 2.2 for this machine. A snake-shaped grid makes the depth equal the number of land cells, which is the worst case; a grid of scattered islands has a small depth.

**Problem 6.** Push a frame `(n)` for a call and a frame `(add)` for the pending addition; when the `add` frame is popped, take two values off a value stack and push their sum. The number of frames expanded equals the number of calls of the recursion.

**More practice.** `reverse(s, l, r)`: if `l >= r` return; swap, then `reverse(s, l + 1, r - 1)`. It is a tail call, so a release build may turn it into a loop.

---

## Cheat sheet

- Three questions: base case, shrink, combine; check by induction.
- Depth is stack space; the number of calls is time.
- A frame holds the return address, arguments and locals; a stack overflow is a crash, not an exception.
- The limit depends on frame size, so on compiler and flags; do not rely on optimised-away recursion.
- `fibNaive` makes `2 * F(n + 1) - 1` calls; memoisation makes `2n - 1`.
- A tail call can be a loop, but C++ does not promise it.
- Explicit stack: push work in reverse order; mark visited cells when pushed.
- Bound the depth by recursing on the smaller part and looping on the rest.
- Kernels and safety standards avoid recursion.

---

## Sources

- Titles of 509, 70, 50, 206, 21, 24, 200, 695, 733, 779 and 344 were checked against pages that list them: [yzygitzh.github.io LeetCode summary](https://yzygitzh.github.io/algorithm/2018/11/09/leetcode-summary.html), [github.com/xindoo/leetcode](https://github.com/xindoo/leetcode) and [github.com/grandyang/leetcode issue 509](https://github.com/grandyang/leetcode/issues/509). Difficulty labels are not used because I could not verify them.
- The 16 KiB x86-64 kernel stack: [LWN, "Re: [RFC 2/2] x86_64: expand kernel stack to 16K"](https://lwn.net/Articles/600649/), a repost of a 2014 mailing-list reply.
- MISRA C:2012 rule 17.2 (required): [MathWorks Bug Finder reference](https://www.mathworks.com/help/bugfinder/ref/misrac2012rule17.2.html).
- All other claims in this tutorial come from the tests described at the top.
