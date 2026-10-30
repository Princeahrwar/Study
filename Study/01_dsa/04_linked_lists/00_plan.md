# Linked Lists — Complete Study Plan (C++)

**Track:** Data Structures & Algorithms — Foundations → Senior/Staff systems depth
**Language:** C++ (with C-style intrusive variants covered where relevant to kernel/embedded work)
**Pace:** Self-paced. One topic file per session — you study, ask doubts, then say "next" to move on.

---

## How this will run

1. Each topic gets its own numbered tutorial file (`01_<topic>.md`, `02_<topic>.md`, ...), written with production-grade C++ code, complexity analysis, pitfalls, and Mermaid diagrams — not a chat-only summary.
2. You study the file, ask doubts here, and I'll clarify or patch the file.
3. When you say "next" / "move on", I write the next topic's file.
4. Files live in `Desktop/My Projects/Study/01_dsa/04_linked_lists/` on your Mac, and at the same path in the Study-Plan project. Topic N covers Phase N.

No fixed deadline — depth over speed, per your usual approach.

---

## Curriculum Overview

### Phase 1 — Foundations of Singly Linked Lists
- What a linked list actually is at the memory level; why it exists when `std::vector`/arrays exist
- Node design in C++ (raw struct, templated node)
- Traversal, insertion (head/tail/position), deletion (head/tail/position/by value), search
- Time/space complexity for every operation
- Building a complete, leak-free `SinglyLinkedList<T>` class (constructor, destructor, copy semantics)
- Common pitfalls: dangling pointers, memory leaks, off-by-one traversal bugs

### Phase 2 — Core Patterns & Techniques
- Two-pointer technique: fast/slow pointers, finding the middle, nth node from the end
- Floyd's cycle detection (and finding the cycle's *start* node)
- Reversing a linked list: iterative, recursive, reverse in groups of k, reverse between positions m–n
- Recursion on linked lists — patterns and the hidden cost (call stack = O(n) space)
- The dummy/sentinel node technique (why it removes a whole class of edge-case bugs)

### Phase 3 — Doubly & Circular Linked Lists
- Doubly linked list: structure, insertion/deletion at any position in O(1) given a node pointer
- Singly and doubly circular linked lists; applications (round-robin scheduling, Josephus problem)

### Phase 4 — Merging & Sorting
- Merge two sorted linked lists
- Merge K sorted linked lists (min-heap approach vs divide-and-conquer)
- Sorting a linked list in O(n log n) with merge sort (and why quicksort/heapsort don't fit linked lists well)
- Intersection point of two linked lists

### Phase 5 — Classic Interview Problems
- Palindrome linked list (O(1) space approach)
- Add two numbers represented as linked lists
- Clone a linked list with a random pointer
- Flatten a multilevel doubly linked list
- Rotate a linked list
- **LRU Cache** — doubly linked list + hash map (flagship Staff-level design question)
- LFU Cache (stretch problem, same family)

### Phase 6 — Systems-Level & Advanced Topics (Staff-focused)
- Linked lists in real systems: the **Linux kernel's intrusive `list_head`** design — `container_of`, `list_add`, `list_for_each_entry_safe`
- Memory allocators & free lists — how `malloc`/custom allocators chain free blocks
- Why linked lists are often *slow in practice*: pointer chasing and cache misses vs contiguous structures (with a benchmarking exercise: `std::list` vs `std::vector`)
- **Skip lists** — probabilistic O(log n) structure; how Redis uses them for sorted sets
- **Lock-free / concurrent linked lists** — CAS, the ABA problem, `std::atomic`, a taste of hazard pointers
- XOR linked lists (space-optimized doubly linked list using pointer XORing)
- Unrolled linked lists (cache-friendlier variant, arrays-of-nodes)

### Phase 7 — Modern C++ & Engineering Considerations
- Smart pointers for linked lists — the `unique_ptr` recursive-destructor stack-overflow gotcha, and how to avoid it
- `std::list` vs `std::forward_list` vs a custom implementation — when each is the right call
- Custom allocators / memory pools for node-heavy structures
- Writing it production-grade: Rule of 5, const-correctness, exception safety, writing a proper iterator

### Phase 8 — Practice & Mock Strategy
- Curated problem set organized by pattern (not just a random list)
- How to talk through a linked-list problem out loud in an interview — the structure interviewers listen for

---

## Curated Resources

**Primary practice track (free, C++, well-structured):**
- Striver's A2Z DSA Course — Linked List section, takeUforward: https://takeuforward.org/strivers-a2z-dsa-course/strivers-a2z-dsa-course-sheet-2/
  Good for problem sequencing and clean C++ solutions with video walkthroughs.

**Theory / visual intuition:**
- Abdul Bari — Data Structures playlist: https://www.youtube.com/@abdul_bari/playlists
  Strong for building the mental model before coding.

**Practice & reference:**
- LeetCode's Linked List explore card and tag — best for timed practice once you know a pattern.
- GeeksforGeeks Linked List section — good quick reference for edge cases and variations.

**Systems-level (Phase 6, matches your kernel/embedded interest):**
- `0xAX/linux-insides` — "Data Structures in the Linux Kernel" chapter: https://github.com/0xAX/linux-insides
  Explains the kernel's intrusive `list_head` design in depth — this is the real thing, not a toy example.
- Redis source (`t_zset.c`) plus writeups on Redis's skip list implementation for sorted sets, for the skip-list topic.
- Background reading on the ABA problem and CAS-based lock-free structures (moodycamel.com's blog on solving the ABA problem is a good primer before you touch `std::atomic` linked lists).

---

## Progress

| # | Topic | Files | Status |
|---|---|---|---|
| 01 | Foundations of singly linked lists | [`01_foundations_singly_linked_list.md`](./01_foundations_singly_linked_list.md) | Done |
| 02 | Core patterns: dummy node, two pointers, Floyd's cycle detection, reversal, recursion | [`02_core_patterns_two_pointers_reversal.md`](./02_core_patterns_two_pointers_reversal.md) + [`02_two_pointers_reversal_demo.cpp`](./02_two_pointers_reversal_demo.cpp) | Done |
| 03 | Doubly and circular linked lists (sentinel ring, `std::list` and `list_head` preview, tail-held circular list, Josephus) | [`03_doubly_and_circular_lists.md`](./03_doubly_and_circular_lists.md) + [`03_doubly_and_circular_lists_demo.cpp`](./03_doubly_and_circular_lists_demo.cpp) | Done |
| 04 | Merging and sorting | — | Next |
| 05 | Classic interview problems (incl. LRU and LFU caches) | — | — |
| 06 | Systems-level and advanced topics | — | — |
| 07 | Modern C++ and engineering considerations | Folded into the C++ track (CPP 04, CPP 05) | — |
| 08 | Practice and mock strategy | Folded into DSA practice and the mock schedule | — |

---

## Next step

Topics 01 to 03 are done. Say "next" in the linked list chat for Topic 04: merging and sorting. The week each topic is scheduled for is in [`../../00_MASTER_PLAN.md`](../../00_MASTER_PLAN.md), section 5.
