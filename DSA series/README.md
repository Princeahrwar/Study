# DSA Series (C++)

Data structures and algorithms, from foundations to Senior/Staff systems depth, one topic at a time.

- **On your Mac:** `Desktop/My Projects/DSA series/`
- **In the Study-Plan project:** `DSA series/`

---

## Study order

Linked lists come first because trees reuse its ideas directly.

| # | Track | Folder | Plan | Status |
|---|---|---|---|---|
| 1 | Linked lists | [`linked list/`](./linked%20list/) | [`00-linked-list-study-plan.md`](./linked%20list/00-linked-list-study-plan.md) | Topics 01–02 done, 03 next |
| 2 | Trees | [`trees/`](./trees/) | [`00-trees-study-plan.md`](./trees/00-trees-study-plan.md) | Topic 01 ready |

What trees reuse: ownership and the Rule of 5 (linked list Topic 01), and dummy nodes, pointer-to-pointer links and the cost of recursion depth (Topic 02). Both of those are done, so you can continue the two tracks side by side.

More tracks get added here as they are planned.

---

## Every file, in sequence

### 1. Linked lists

| # | File | What it covers | Status |
|---|---|---|---|
| 00 | [`00-linked-list-study-plan.md`](./linked%20list/00-linked-list-study-plan.md) | 8 phases, resources, progress | — |
| 01 | [`01-foundations-singly-linked-list.md`](./linked%20list/01-foundations-singly-linked-list.md) | Node design, every core operation, Rule of 5, pitfalls | Done |
| 02 | [`02-core-patterns-two-pointers-reversal.md`](./linked%20list/02-core-patterns-two-pointers-reversal.md) | Dummy node, two pointers, Floyd's cycle detection, reversal, recursion | Done |
| 02 | [`02-two-pointers-reversal-demo.cpp`](./linked%20list/02-two-pointers-reversal-demo.cpp) | Companion code for Topic 02: 49 checks | Done |
| 03 | Doubly and circular linked lists | — | Next |

### 2. Trees

| # | File | What it covers | Status |
|---|---|---|---|
| 00 | [`00-trees-study-plan.md`](./trees/00-trees-study-plan.md) | 10 phases, 34 topics, resources, progress | — |
| 01 | [`01-binary-tree-fundamentals.md`](./trees/01-binary-tree-fundamentals.md) | Terminology, shapes, properties, representations, ownership, traversals, N-ary trees | Ready |
| 01 | [`01-binary-tree-fundamentals-demo.cpp`](./trees/01-binary-tree-fundamentals-demo.cpp) | Companion code for Topic 01: 73 checks | Ready |
| 02 | Iterative traversals and Morris traversal | — | Next |

---

## How each topic works

1. Read the tutorial (`NN-*.md`) top to bottom. The diagrams are Mermaid, so open it in something that renders Mermaid (VS Code with a Mermaid preview, Obsidian, or GitHub).
2. Close it and implement the main code yourself, from memory, once.
3. Run the companion (`NN-*-demo.cpp`) and read its tests: they pin down the edge cases.
4. Do the practice list at the end of the tutorial.
5. Ask doubts in the chat where that track started, then say "next" there for the following topic.

## Building the companion code

Every companion is a single file with its own checks. On the Mac:

```text
clang++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined NN-name-demo.cpp -o demo && ./demo
```

On macOS, `g++` is Apple's clang under another name, so either command works. The sanitizers catch double frees, use-after-free, out-of-bounds access and undefined behavior; a run should end with `N/N checks passed` and nothing else. Apple's clang does not include leak detection, so to check for leaks on the Mac, build without sanitizers and run `leaks --atExit -- ./demo`. For a quick run without sanitizers, use `-O2` instead.

## File naming

- `00-<track>-study-plan.md`: the plan for a track, with its progress table.
- `NN-<topic>.md`: the tutorial for topic NN.
- `NN-<topic>-demo.cpp`: the tested companion code for topic NN, where a topic has one. The functions in the tutorial are copied from it, so what you read is what was tested. Short alternative versions shown for comparison are called out in the text.
