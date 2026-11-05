# Trees — Complete Study Plan (C++)

**Track:** Data Structures & Algorithms, from binary tree basics to Senior/Staff systems depth
**Language:** C++17 (C-style intrusive variants where they matter for kernel/embedded work)
**Pace:** Self-paced. One topic file per session: you study, ask doubts, then say "next".
**Builds on:** the Linked List track (ownership, Rule of 5, dummy nodes, recursion cost). A linked list is a tree where every node has at most one child, so everything you learned there carries over.

---

## How this will run

1. Each topic gets its own numbered tutorial file (`01_<topic>.md`, `02_<topic>.md`, ...): production-grade C++, complexity analysis, pitfalls, Mermaid diagrams, and a practice list.
2. A topic that introduces a new structure gets a companion `.cpp` file; the others keep their code inside the tutorial. Either way the code is compiled with `-Wall -Wextra -fsanitize=address,undefined` and tested, and every code block in the tutorial is checked against the tested source, so what you read is what was run.
3. You study the file and ask doubts here. I clarify or patch the file.
4. When you say "next", I write the next topic.
5. Files live in `Desktop/My Projects/Study/01_dsa/07_trees/` on your Mac, and at the same path in the Study-Plan project.

Depth over speed. Some topics (red-black deletion, lazy segment trees) are deliberately split across two files.

---

## Core vs stretch (set by the master plan)

- **Core, in the schedule:** 01–13, 15–21, 24, 25, 28, 29, 31, 34.
- **Stretch, after the schedule or if a team asks:** 14 (treaps, splay), 22–23 (lazy and advanced segment trees), 26 (rerooting; tree DP itself is in the DP area), 27 (heavy-light, centroid), 30 (interval and order-statistic trees), 32 (Merkle, k-d, R-trees), 33 (production tree code; covered by the C++ track).

The week each core topic is scheduled for is in [`../../00_MASTER_PLAN.md`](../../00_MASTER_PLAN.md), section 5.

## Curriculum Overview

10 phases, 34 topics. The topic number is the file number.

### Phase 1 — Binary Tree Foundations

**01. Binary tree fundamentals**
- Terminology with one running example; the height/depth conventions that cause off-by-one bugs (edges vs nodes, CLRS vs LeetCode)
- Full, complete, perfect, balanced, degenerate; the naming conflicts between textbooks
- Core properties with short proofs: 2^d nodes per level, height bounds, leaves = internal + 1, n + 1 null links, Catalan count of shapes
- Representations and their memory cost: linked nodes, parent pointers, index pools, implicit arrays, parent arrays, left-child right-sibling
- Node design and ownership: why `unique_ptr` children overflow the stack on deep trees, O(1)-space iterative destruction, iterative deep copy, Rule of 5 with copy-and-swap
- Recursive pre/in/post-order (one function, three visit positions), level-order BFS, basic recursive metrics, expression trees
- General (N-ary) trees: traversals, height, freeing without recursion, and converting to and from left-child right-sibling form

**02. Iterative traversals and Morris traversal**
- Explicit-stack preorder, inorder and postorder (reverse-preorder and one-stack postorder with a `last` pointer)
- All three traversals in a single pass with a state stack, and the push-in-reverse flag idiom
- A lazy inorder iterator (the BST iterator) and the kth smallest with early exit
- Morris traversal in all three orders: O(1) extra space by temporarily threading the n + 1 null links; why it must finish and restore the tree
- Parent-link successor (how the kernel's `rb_next` can work with no stack)
- Recursion depth in practice, measured: 8 MiB, 1 MiB and 64 KiB stacks, and what a frame costs
- Measured comparison of the five methods

**03. Construction and serialization**
- Which traversal pairs identify a tree uniquely, and why preorder + postorder does not (counted for every shape up to 9 nodes)
- Build from preorder + inorder and postorder + inorder in O(n) with a hash map, and without a map or recursion with a stack
- Preorder + postorder for full trees
- Serialize/deserialize without recursion: preorder with null markers, level order, compact binary (bit-per-slot shape plus zigzag varints)
- Validating a serialization by counting open slots, without building the tree
- Subtree equality: the three string pitfalls, and exact canonical subtree ids (Subtree of Another Tree, Find Duplicate Subtrees)

### Phase 2 — Binary Tree Problem Patterns

**04. Bottom-up recursion: returning information up the tree**
- Top-down (pass state down) vs bottom-up (return a summary up), how to pick, and validating a BST both ways
- Same tree, symmetric, invert (recursive and with a stack), and the direct subtree-of-another-tree test with its measured cost
- Balanced check in O(n) (not O(n log n)) with the visit counts, diameter in one pass, maximum path sum: returning one value while tracking another
- A non-recursive bottom-up fold for trees a million levels deep, and what it costs

**05. Level, view and width problems**
- Level order as a loop over levels, and the same result by depth-indexed recursion (when each is the right tool, with measured costs)
- Zigzag without reversing the queue, right/left side views in both forms
- Populating next-right pointers in O(1) space for any tree
- Columns: vertical order, top view and bottom view, and the tie rules that separate them
- Boundary traversal and its traps
- Maximum width with position indexing, and the integer overflow trap in it (signed overflow is undefined; unsigned or rebased positions are not)

**06. Paths, ancestors and LCA in a binary tree**
- Root-to-leaf paths, path sum I/II with one shared path vector, path sum III with prefix sums on a tree (the undo step, and the measured O(n h) vs O(n) cost)
- Lowest common ancestor in a binary tree: the bottom-up recursion, the version that survives a missing node, and the two-walker version with parent links; distance between two nodes
- Nodes at distance K and time-to-burn-the-tree (convert to a graph with parent links, then BFS)
- Flatten a tree to a linked list in place with O(1) space; count nodes of a complete tree in O(log² n), with measured call counts

### Phase 3 — Binary Search Trees

**07. BST core operations**
- The invariant (about whole subtrees, not parent and child), the duplicate policy, search and the pointer-to-pointer idiom for insert
- Delete in all three cases, by relinking (other nodes keep their addresses) and by copying the successor's key
- Floor, ceil, predecessor and successor straight from the root, returning `std::optional`
- Validating a BST by inorder with an `optional` previous key, with the wrong versions and the `INT_MIN` edge cases
- Why a BST degenerates on sorted input, with measured comparisons, heights and times

**08. BST problem patterns**
- The four tools: prune by comparing, walk inorder lazily, build from an order, return facts bottom-up
- Kth smallest, a BST iterator in O(h) memory (forward and backward cursors), two-sum with two cursors
- Recover a BST with two swapped keys (stack and Morris); largest BST inside a binary tree
- Build a BST from preorder in O(n) and test a preorder; sorted array or list to a height-balanced BST; LCA and range sum by pruning

**09. Inside `std::set` and `std::map`**
- What the standard library actually builds (a red-black tree in libstdc++, libc++ and MSVC), what a node costs in bytes, and why scans are cache-bound
- Inspecting a real `std::set`: red-black invariants and measured heights, iterator and reference guarantees, erase while iterating
- `lower_bound`/`upper_bound`/`equal_range`, `multiset` pitfalls, strict weak ordering, broken comparators and transparent comparators
- C++17 node handles (`extract`, `merge`); GNU `pb_ds` order-statistics tree

### Phase 4 — Self-Balancing Search Trees

**10. Rotations and AVL trees**
- Left and right rotations as the one primitive every balanced tree shares: what moves, which heights change, why the order cannot change
- AVL balance factor, the four imbalance cases (LL, RR, LR, RL), full insert and erase with repair on the way up; one repair per insert, up to `O(log n)` per erase
- The height bound (about 1.44 log₂ n) from minimum-node AVL trees and Fibonacci numbers; measured heights, depths, repair counts and size against `std::set`

**11. Red-black trees I: invariants, 2-3-4 correspondence, insertion**
- The five properties and why they bound height by 2 log₂(n + 1)
- Red-black trees as an encoding of 2-3-4 trees (this makes the cases make sense)
- Insertion fix-up, case by case, with traces
- Left-leaning red-black trees (Sedgewick) as the simpler variant

**12. Red-black trees II: deletion and a complete implementation**
- Deletion fix-up and the "double black" idea
- A full, tested `RBTree<K, V>` with an invariant checker
- AVL vs red-black: lookup-heavy vs update-heavy workloads, rotation counts

**13. The Linux kernel rbtree**
- Intrusive `rb_node`, `rb_entry`/`container_of` (links back to the linked list Phase 6)
- Why the kernel makes you write the search and insert loop yourself; `rb_link_node` + `rb_insert_color`
- Color packed into the low bit of the parent pointer; `rb_root_cached` for O(1) leftmost
- Augmented rbtrees, and where they run: the EEVDF scheduler (Linux 6.6+), timers, interval trees

**14. Randomized and amortized trees**
- Treaps (and the implicit treap as a sequence structure with O(log n) split and merge)
- Splay trees: amortized O(log n), the working-set property, when they win
- Scapegoat and AA trees briefly; skip lists vs balanced trees (links back to the linked list track)

### Phase 5 — Heaps and Priority Queues

**15. Binary heaps**
- Implicit array layout, sift-up and sift-down, build-heap in O(n) (with the proof), heapsort
- `std::priority_queue`, `std::make_heap`/`push_heap`/`pop_heap`, custom comparators

**16. Heap patterns and advanced heaps**
- Top-K, K-way merge, running median with two heaps, task scheduling
- Indexed priority queue with decrease-key (for Dijkstra and Prim); d-ary heaps and cache behavior
- Binomial, Fibonacci and pairing heaps: what they buy in theory and why they rarely win in practice

### Phase 6 — Tries and Prefix Trees

**17. Tries**
- Insert, search, prefix search, delete; child storage choices (array, map, sorted vector) and their memory costs
- Autocomplete, word search II, replace words

**18. Binary tries, radix trees and longest-prefix match**
- Binary trie for maximum-XOR problems
- Radix/Patricia trees (path compression)
- IP routing lookups: longest-prefix match, and the Linux IPv4 FIB's level-compressed trie (`fib_trie`)
- Aho-Corasick (stretch), suffix trees and arrays (awareness)

### Phase 7 — Range-Query Trees

**19. Prefix sums, sparse tables and RMQ**
- Prefix sums and difference arrays as the baseline
- Sparse table: O(1) queries for idempotent operations (min, max, gcd), and why sum does not qualify

**20. Fenwick tree (binary indexed tree)**
- The lowbit trick and the implicit tree it defines
- Point update + prefix query, range update + point query, range update + range query (two BITs)
- 2D BIT; finding the k-th element by binary lifting on the BIT

**21. Segment tree I: point update, range query**
- Recursive segment tree, custom merge functions (max subarray sum, counts)
- Iterative bottom-up segment tree in 2n memory (Al.Cash's design)
- Descending the tree: "first index with value ≥ x" in O(log n)

**22. Segment tree II: lazy propagation**
- Range add, range assign, and composing update tags correctly
- The common bugs: forgetting to push down, wrong tag composition order

**23. Segment tree III: beyond the basics**
- Persistent segment trees (k-th smallest in a range)
- Dynamic/sparse segment trees for huge coordinate ranges; coordinate compression
- Merge sort tree; offline query tricks; segment tree beats (awareness)

### Phase 8 — Trees as Graphs: Advanced Algorithms

**24. General trees, Euler tour and subtree queries**
- N-ary trees as adjacency lists; iterative DFS on 10⁵–10⁶ nodes
- Euler tour (entry/exit times): turning subtree queries into range queries
- Diameter by two BFS passes; tree centers and centroids

**25. LCA at scale**
- Binary lifting (and k-th ancestor), Euler tour + sparse table (O(1) queries), Tarjan's offline LCA
- Choosing between them by preprocessing cost, query cost and memory

**26. Tree DP and rerooting**
- Subtree DP (independent set, matching, colorings)
- Rerooting: answers for every possible root in O(n) total

**27. Heavy-light and centroid decomposition**
- Heavy-light decomposition for path queries in O(log² n)
- Centroid decomposition for distance-based queries

**28. Disjoint set union (union-find)**
- A forest of parent pointers; union by rank/size and path compression; the inverse-Ackermann bound
- Kruskal's MST, connectivity, rollback DSU for offline problems

### Phase 9 — Systems and Disk-Oriented Trees (Staff focus)

**29. B-trees and B+ trees**
- Why fanout matters when a node is a disk page or a cache line
- Search, insert with splits, delete with merges and borrowing; B+ tree leaf chaining for range scans
- Where they run: SQLite and InnoDB indexes, Btrfs/XFS; concurrency with B-link trees (Lehman–Yao); B-trees vs LSM trees

**30. Augmented trees: interval and order-statistic trees**
- Interval trees (overlap queries), order-statistic trees (rank and select)
- The kernel's generic interval tree built on the augmented rbtree

**31. Trees inside the Linux kernel, and cache-conscious layouts**
- The maple tree: an RCU-safe range B-tree that replaced the VMA rbtree and list in Linux 6.1
- XArray (the replacement for the radix tree API); RCU-friendly lookups
- Eytzinger and van Emde Boas layouts; static B-trees ("S-trees") for fast searches over sorted data

**32. Hash and spatial trees**
- Merkle trees (git, certificate transparency, anti-entropy in distributed stores)
- k-d trees, quadtrees and R-trees (awareness level, with where each is used)

### Phase 10 — Engineering and Interview Readiness

**33. Production-grade tree code in modern C++**
- Ownership choices revisited: raw + owner class, `unique_ptr`, arenas and node pools, `std::pmr`
- Writing a proper BST iterator (parent pointers vs stack), const-correctness, exception guarantees
- Benchmarking `std::map` vs a sorted `std::vector` vs a B-tree-style container

**34. Pattern-indexed problem set and interview narration**
- A curated problem list organized by pattern, not by difficulty
- How to talk through a tree problem out loud: conventions, shape assumptions, recursion depth, complexity

---

## Curated Resources

**Primary practice track (C++, video + notes):**
- Striver's Tree Series (takeUforward): notes at https://takeuforward.org/data-structure/strivers-tree-series-tree-data-structure/ and the video playlist at https://www.youtube.com/playlist?list=PLgUwDviBIf0q8Hkd7bK2Bpryj2xVJk8Vk. Best sequencing for Phases 1–3.
- Striver's Trie playlist (Phase 6): https://www.youtube.com/playlist?list=PLgUwDviBIf0pcIDCZnxhv0LkHf5KzG9zp
- Aditya Verma's Heap playlist (Phase 5 patterns): https://www.youtube.com/playlist?list=PL_z_8CaSLPWdtY9W22VjnPxG30CXNZpI9

**Theory and visual intuition:**
- Abdul Bari, data structures videos (AVL rotations, B-trees, heaps): https://www.youtube.com/@abdul_bari
- William Fiset's data structures course (heaps, BSTs, AVL, union-find, Fenwick trees with animations): https://www.freecodecamp.org/news/learn-data-structures-from-a-google-engineer/
- Interactive visualizers to watch rotations and splits happen: USFCA (David Galles), https://www.cs.usfca.edu/~galles/visualization/Algorithms.html, and VisuAlgo, https://visualgo.net
- CLRS: binary search trees, red-black trees, B-trees, augmenting data structures. The reference for proofs.

**Balanced trees:**
- Sedgewick, "Left-Leaning Red-Black Trees": https://sedgewick.io/wp-content/themes/sedgewick/papers/2008LLRB.pdf
- Princeton COS 226 balanced search trees lecture (2-3 trees to red-black): https://www.cs.princeton.edu/courses/archive/spring26/cos226/lectures/33BalancedSearchTrees.pdf

**Range queries and tree algorithms:**
- cp-algorithms (segment tree, Fenwick tree, sparse table, treap, DSU, LCA, heavy-light decomposition): https://cp-algorithms.com/
- Al.Cash, "Efficient and easy segment trees" (the 2n-memory iterative design): https://codeforces.com/blog/entry/18051
- Codeforces EDU segment tree course by pashka (ITMO): https://codeforces.com/blog/entry/80031
- Heavy-light decomposition tutorial: https://codeforces.com/blog/entry/81317

**Systems-level (Phases 4 and 9, matching your kernel/embedded and networking work):**
- Linux rbtree documentation (intrusive nodes, cached and augmented rbtrees): https://docs.kernel.org/core-api/rbtree.html
- "Introducing maple trees", LWN: https://lwn.net/Articles/845507/ and the kernel's maple tree docs: https://docs.kernel.org/core-api/maple_tree.html
- XArray documentation: https://docs.kernel.org/core-api/xarray.html
- Vincent Bernat, "IPv4 route lookup on Linux" (the LC-trie behind the IPv4 FIB): https://vincent.bernat.ch/en/blog/2017-ipv4-route-lookup-linux
- Alex Petrov, *Database Internals*, Part I (B-tree basics, implementation, variants, LSM trees)
- Ben Johnson, "SQLite Internals: Pages & B-trees": https://fly.io/blog/sqlite-internals-btree/
- Sergey Slotin, *Algorithms for Modern Hardware*, data structures chapter (Eytzinger binary search, S-trees, fast segment trees): https://en.algorithmica.org/hpc/data-structures/

**Practice:**
- LeetCode Tree, Binary Search Tree, Heap, Trie, Segment Tree and Binary Indexed Tree tags, for timed practice once a pattern is learned
- Each topic file ends with an ordered problem list for that topic

---

## Progress

| # | Topic | File | Status |
|---|---|---|---|
| 01 | Binary tree fundamentals | `01_binary_tree_fundamentals.md` + `01_binary_tree_fundamentals_demo.cpp` | Done |
| 02 | Iterative traversals and Morris traversal | `02_iterative_traversals_and_morris.md` | Done |
| 03 | Construction and serialization | `03_construction_and_serialization.md` | Done |
| 04 | Bottom-up recursion | `04_bottom_up_recursion.md` | Done |
| 05 | Level, view and width problems | `05_level_view_and_width_problems.md` | Done |
| 06 | Paths, ancestors and LCA in a binary tree | `06_paths_ancestors_and_lca.md` | Done |
| 07 | BST core operations | `07_bst_core_operations.md` + `07_bst_core_operations_demo.cpp` | Done |
| 08 | BST problem patterns | `08_bst_problem_patterns.md` | Done |
| 09 | Inside `std::set` and `std::map` | `09_inside_std_set_and_map.md` | Done |
| 10 | Rotations and AVL trees | `10_rotations_and_avl_trees.md` | Done |
| 11 | Red-black trees I: invariants, 2-3-4 correspondence, insertion | `11_red_black_trees_invariants_and_insertion.md` | Done |
| 12 | Red-black trees II: deletion and a complete implementation | `12_red_black_trees_deletion_and_full_class.md` | Done |
| 13 | The Linux kernel rbtree | `13_linux_kernel_rbtree.md` | Done |
| 14 | Randomized and amortized trees | `14_randomized_and_amortized_trees.md` | Done |
| 15 | Binary heaps | `15_binary_heaps.md` | Done |
| 16 | Heap patterns and advanced heaps | `16_heap_patterns_and_advanced_heaps.md` | Done |
| 17 | Tries | `17_tries.md` | Done |
| 18 | Binary tries, radix trees and longest-prefix match | — | Next |
| 19–34 | See the curriculum above | — | — |

---

## Next step

Topics 01 to 17 are written: `01_binary_tree_fundamentals.md`, `02_iterative_traversals_and_morris.md`, `03_construction_and_serialization.md`, `04_bottom_up_recursion.md`, `05_level_view_and_width_problems.md` and `06_paths_ancestors_and_lca.md`, `07_bst_core_operations.md` `08_bst_problem_patterns.md` and `09_inside_std_set_and_map.md` `10_rotations_and_avl_trees.md`, `11_red_black_trees_invariants_and_insertion.md`, `12_red_black_trees_deletion_and_full_class.md`, `13_linux_kernel_rbtree.md`, `14_randomized_and_amortized_trees.md`, `15_binary_heaps.md`, `16_heap_patterns_and_advanced_heaps.md` and `17_tries.md`. Study them, ask doubts, and say "next" for Topic 18.
