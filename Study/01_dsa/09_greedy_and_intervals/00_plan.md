# Greedy and Intervals: Area Plan

A greedy algorithm takes the best-looking choice at every step and never looks back. Nearly every such rule is wrong, and a few are exactly right; this area teaches how to tell them apart (the exchange argument, "greedy stays ahead", and testing against brute force), then applies the idea to the classic problems: interval scheduling, merging and sweeping intervals, and scheduling with a heap. Intervals appear in a large share of interview problems and in real systems code (timer wheels, memory-region merging, IO scheduling), so the second tutorial is as much about careful boundary handling as about cleverness.

**Language:** C++ · **Time:** about 1 week · **Order:** read the tutorial, try the problems at its end, then say "next".

---

## How each tutorial is built

1. Starts from the basics and builds up to the interview-level ideas.
2. Small examples sit inside the text, right after the idea they explain. Every claim about speed, counts or failure rates is checked by running the code.
3. Ends with problems. Each problem says **where to solve it** (paper, LeetCode, your own editor) and **how to approach it** (hints, not the answer). The answers are at the very bottom, so you can try first.
4. There is no companion demo file: the code is inside the tutorial.

---

## Tutorials

| # | Tutorial | What it covers | Status |
|---|---|---|---|
| 01 | [`01_greedy_algorithms_and_proofs.md`](01_greedy_algorithms_and_proofs.md) | The two ingredients of a correct greedy (a safe greedy choice and optimal substructure); the exchange argument and "greedy stays ahead"; activity selection with the four natural rules, only one correct, and how often each wrong rule loses (48.9%, 5.7% and 0.045% of random instances); fractional knapsack and why 0/1 differs; Huffman coding with its optimality proof, Kraft's equality, canonical codes and the entropy bound; the jump games and the gas station as one-pass scans; coin change and canonical coin systems (most random systems are not); how to hunt for a counterexample by machine | Written |
| 02 | [`02_intervals_and_sweep_line.md`](02_intervals_and_sweep_line.md) | Closed versus half-open intervals decided first (and the merge rule that is the same in both); merge, insert in O(n) and two-pointer intersection with proofs; meeting rooms I and II by heap, by two sorted arrays and by event sweep; minimum removals and minimum arrows as activity selection; the event sweep with its tie order, the difference array that replaces the sort (8 to 10 times faster than the heap at 1,000,000 intervals here), coordinate compression, union length and free gaps; an online calendar on an ordered map checking two neighbours | Written |
| 03 | [`03_greedy_scheduling_with_heaps.md`](03_greedy_scheduling_with_heaps.md) | The heap as a greedy engine in three patterns (most frequent, unlock then best, take everything then undo the worst); Task Scheduler with its closed form and Reorganize String with the exact impossibility bound; IPO and Minimum Refueling Stops (buying fuel in hindsight); Course Schedule III by Moore-Hodgson with its exchange argument; shortest processing time, Smith's rule, earliest due date and Johnson's two-machine rule, each proved by swapping and tested against every ordering; how often the wrong rule loses | Written |

---

## Problem list (about 25 with practice)

Solve these after the tutorial named in the first column. Each tutorial also lists its problems at the end, with where to solve them and how to approach them. Numbers and titles are from memory (flagged in each tutorial); check a number before you rely on it.

| Tutorial | Problems (LeetCode number) |
|---|---|
| 01 | Jump Game (55), Jump Game II (45), Gas Station (134), Assign Cookies (455), Lemonade Change (860), Boats to Save People (881), Partition Labels (763), Candy (135), Queue Reconstruction by Height (406); in your editor: Huffman encoder and decoder with canonical codes, a generic greedy-versus-brute-force harness that prints the smallest failing instance, weighted interval scheduling, a canonical-coin-system tester |
| 02 | Merge Intervals (56), Insert Interval (57), Interval List Intersections (986), Non-overlapping Intervals (435), Minimum Number of Arrows to Burst Balloons (452), Meeting Rooms (252, premium), Meeting Rooms II (253, premium), My Calendar I (729); in your editor: a sweep over events with a difference array, merging memory regions |
| 03 | Task Scheduler (621), Reorganize String (767), IPO (502), Course Schedule III (630), Minimum Number of Refueling Stops (871); in your editor: a scheduler simulator that compares shortest-job-first and earliest-deadline-first |

Log every attempt in [`../practice_log.md`](../practice_log.md).

---

## Extra practice and resources

- CLRS chapter 16 "Greedy Algorithms": activity selection, the greedy-choice property, Huffman codes, matroids.
- Kleinberg and Tardos, *Algorithm Design*, chapter 4: the exchange argument and "greedy stays ahead" taught as the two proof patterns, with interval scheduling and scheduling to minimise lateness.
- Jeff Erickson, *Algorithms*, chapter on greedy algorithms (free online): the same material with clear proofs.
- RFC 1951 (DEFLATE): section 3.2.2 describes canonical Huffman codes as used in zlib, gzip and PNG.
- NeetCode 150, sections "Greedy" and "Intervals" (neetcode.io): the same problems with video walkthroughs.

---

## Progress

| # | Tutorial | Status |
|---|---|---|
| 01 | Greedy algorithms and their proofs | Written |
| 02 | Intervals and sweep line | Written |
| 03 | Greedy scheduling with heaps | Written |

All three tutorials of this area are written. Next area: [`../10_dynamic_programming/`](../00_plan.md) (Dynamic programming). Track view: [`../00_plan.md`](../00_plan.md).
