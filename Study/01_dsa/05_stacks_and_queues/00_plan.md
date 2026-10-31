# Stacks and Queues: Area Plan

A stack and a queue are the two simplest containers, and they hide a surprising share of interview problems. This area starts with how they are built (arrays, ring buffers, a queue made of two stacks, a stack that knows its minimum), then turns to the two tricks that the interviews love: the monotonic stack and the monotonic deque, where each element is pushed and popped once, and finally to expression parsing, where the stack is the parser.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed or operation counts is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. A companion `*_demo.cpp` exists only where a tutorial starts a new kind of structure or feeds a project (tutorial 01 does, because the ring buffer is the structure under the `rkdma` project). Elsewhere the code is inside the tutorial.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_stack_and_queue_fundamentals.md`](01_stack_and_queue_fundamentals.md) + [`01_stack_and_queue_fundamentals_demo.cpp`](01_stack_and_queue_fundamentals_demo.cpp) | The two contracts (LIFO and FIFO) and why `pop` returns nothing in the C++ library; a stack on an array and on a fixed buffer with no heap; the queue trap (`erase` at the front) measured; the ring buffer with a size, then with free-running unsigned indices and a power-of-two mask (the Linux `kfifo` design, checked against source); a growing ring; the deque; a single-producer single-consumer ring with acquire and release (the shape of `io_uring` and of the `rkdma` project); the queue built from two stacks and why it costs O(1) amortised; the stack built from one queue; the min-stack in three forms; the min-queue; every class tested against the standard library | Written |
| 02 | [`02_monotonic_stack.md`](02_monotonic_stack.md) | The waiting room that stays sorted and why a pass costs at most 2n comparisons (counted: 39,980 against 199,990,000 for the direct scan on the worst inputs); next greater and smaller, previous greater and smaller from one template, with the tie rules and the fact that the two sides of one pass have opposite strictness; daily temperatures, the circular array (two laps), the online stock span with (price, span) pairs, indexes versus values; the largest rectangle in a histogram (sentinel, `>=`, overflow past `int`); trapping rain water by layers; contribution counting for the sum of subarray minimums (one side strict) with the ties shown on `[2, 2]`; remove K digits as a greedy stack; recognition table; no companion file, every listing is in the tutorial and tested against brute force | Written |
| 03 | [`03_monotonic_deque.md`](03_monotonic_deque.md) | Why a window needs a deque and not a stack; the sliding window maximum and minimum from one template (at most 2n comparisons, at most `k` indexes); the same on a fixed ring with a mask; the block method with prefix and suffix maxima; comparison counts and timings against the direct scan, a `multiset` and a lazy heap (which keeps up to n expired entries); a min-queue from a deque with tickets, compared with the two-stack queue; shortest subarray with sum at least K with negative numbers (why the window fails, prefix sums, the two deque rules, `long long`); the longest subarray with a limit on max minus min (two deques); DP with a window maximum (jump game VI); no companion file, every listing is in the tutorial and tested against brute force | Written |
| 04 | [`04_expression_evaluation_and_parsing.md`](04_expression_evaluation_and_parsing.md) | Why nesting is a stack problem; balanced brackets, the minimum removal (1249), the longest valid run (32, with a stack and with two counters), removing invalid parentheses (301, counts plus a search over runs); reverse Polish notation; the calculators (227 with a stack of terms and with one variable, 224 with a stack of group signs); the shunting-yard method with precedence, right associativity for `^`, unary minus and checked arithmetic, one `shunt` that gives either postfix or a value; recursive descent on the same grammar, why it is the same stack in another place, and the measured depth limit; decode string, simplify path, asteroid collision, validate stack sequences, backspace compare (with an O(1) space version), adjacent duplicates; no companion file, every listing is in the tutorial and tested against brute forces, a tree generator and a fuzz of 700,000 strings | Written |

---

## Problem list (about 30 with practice)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them. Difficulty labels are given only where the tutorial that lists the problem verified them; the numbers and titles of the problems in every tutorial were checked when it was written (tutorial 04 gives no difficulty labels, because none could be verified).

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Min Stack (155), Implement Queue using Stacks (232), Implement Stack using Queues (225), Design Circular Queue (622), Design Circular Deque (641), Max Stack (716, premium); in your own editor: a single-producer single-consumer ring tested with two threads, a queue with O(1) minimum. More practice: Number of Recent Calls (933), Design Browser History (1472), Design Front Middle Back Queue (1670) |
| 02 | Next Greater Element I (496), Next Greater Element II (503), Daily Temperatures (739), Online Stock Span (901), Largest Rectangle in Histogram (84), Trapping Rain Water (42), Sum of Subarray Minimums (907), Remove K Digits (402). More practice: Car Fleet (853), 132 Pattern (456), Maximal Rectangle (85), Remove Duplicate Letters (316) |
| 03 | Sliding Window Maximum (239), Shortest Subarray with Sum at Least K (862), Longest Continuous Subarray With Absolute Diff Less Than or Equal to Limit (1438), Jump Game VI (1696). More practice: Constrained Subsequence Sum (1425), Max Value of Equation (1499) |
| 04 | Valid Parentheses (20), Evaluate Reverse Polish Notation (150), Basic Calculator (224), Basic Calculator II (227), Decode String (394), Simplify Path (71), Longest Valid Parentheses (32), Min Remove to Make Valid Parentheses (1249), Basic Calculator III (772, premium); in your editor: a shunting-yard parser and a recursive-descent parser fuzzed against each other, and the depth at which recursion crashes. More practice: Asteroid Collision (735), Validate Stack Sequences (946), Remove Invalid Parentheses (301), Backspace String Compare (844), Remove All Adjacent Duplicates in String (1047) |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- Sedgewick and Wayne, *Algorithms*, section 1.3 "Bags, Queues, and Stacks": array and linked implementations, resizing, and Dijkstra's two-stack expression evaluation.
- CLRS section 10.1 "Simple array-based implementations of stacks and queues": the classic wrap-around queue.
- Linux kernel documentation and source for `kfifo` (`include/linux/kfifo.h`, `lib/kfifo.c`): the production ring buffer of tutorial 01.
- `liburing` (github.com/axboe/liburing) and `io_uring(7)`: the same ring design between user space and the kernel.
- NeetCode 150, section "Stack" (neetcode.io): the same problems with video walkthroughs.
- Algorithms for Competitive Programming (cp-algorithms.com): "Minimum Stack / Minimum Queue": the min-stack and min-queue ideas of tutorial 01, and the sliding-window minimum of tutorial 03.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Stack and queue fundamentals | Written |
| 02 | Monotonic stack | Written |
| 03 | Monotonic deque | Written |
| 04 | Expression evaluation and parsing | Written |

All four tutorials are written, so the stacks-and-queues area is complete. Solve the problems at the end of each one. The next DSA area is 06, recursion and backtracking. Track view: [`../00_plan.md`](../00_plan.md).
